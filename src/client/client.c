#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <rte_byteorder.h>
#include <rte_common.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_pause.h>
#include <rte_udp.h>

#include "client.h"
#include "common.h"

static struct app app = APP_INITIALIZER("echo_client");
static struct client_conf client_conf;
static struct app lcore_apps[RTE_MAX_LCORE];
static bool lcore_enabled[RTE_MAX_LCORE];
static uint8_t request_templates[RTE_MAX_LCORE][sizeof(struct rte_ether_hdr) +
	sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr) + MAX_PAYLOAD];
static uint16_t request_pkt_len;
static uint16_t worker_count;

static uint64_t client_pacing_cycles_per_burst(void)
{
	const double wire_overhead_bytes = 24.0;
	double target_pps;
	double worker_pps;
	double cycles;

	if (client_conf.rate_mbps == 0 || worker_count == 0)
		return 0;

	target_pps = (client_conf.rate_mbps * 1000000.0) /
		((request_pkt_len + wire_overhead_bytes) * 8.0);
	worker_pps = target_pps / worker_count;
	if (worker_pps <= 0.0)
		return 0;

	cycles = (rte_get_tsc_hz() * (double)client_conf.burst_size) / worker_pps;
	if (cycles < 1.0)
		return 1;
	return (uint64_t)(cycles + 0.5);
}

static void prepare_request_template(struct app *worker_app, uint8_t *template_buf)
{
	struct rte_ether_hdr *eth = NULL;
	struct rte_ipv4_hdr *ip = NULL;
	struct rte_udp_hdr *udp = NULL;
	char *payload = NULL;
	const uint16_t udp_len = sizeof(*udp) + client_conf.payload_len;
	const uint16_t ip_len = sizeof(*ip) + udp_len;
	const uint16_t src_port = (uint16_t)(CLIENT_INTERNAL_SRC_L4 + worker_app->tx_queue_id);
	const uint32_t src_ip = CLIENT_INTERNAL_SRC_ADDR + worker_app->tx_queue_id;

	request_pkt_len = client_packet_len(&client_conf);
	memset(template_buf, 0, request_pkt_len);
	eth = (struct rte_ether_hdr *)template_buf;

	rte_ether_addr_copy(&client_conf.dst_mac, &eth->dst_addr);
	rte_ether_addr_copy(&app.port_mac, &eth->src_addr);
	eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

	ip = (struct rte_ipv4_hdr *)(eth + 1);
	ip->version_ihl = 0x45;
	ip->total_length = rte_cpu_to_be_16(ip_len);
	ip->time_to_live = 64;
	ip->next_proto_id = IPPROTO_UDP;
	ip->src_addr = rte_cpu_to_be_32(src_ip);
	ip->dst_addr = rte_cpu_to_be_32(CLIENT_INTERNAL_DST_ADDR);

	udp = (struct rte_udp_hdr *)(ip + 1);
	udp->src_port = rte_cpu_to_be_16(src_port);
	udp->dst_port = rte_cpu_to_be_16(CLIENT_INTERNAL_DST_L4);
	udp->dgram_len = rte_cpu_to_be_16(udp_len);
	udp->dgram_cksum = 0;

	payload = (char *)(udp + 1);
	memcpy(payload, client_conf.payload, client_conf.payload_len);
	fill_ipv4_checksum(ip);
}

static void maybe_print_aggregate_stats(uint64_t cur_tsc, uint64_t *prev_tsc)
{
	if (app.timer_period_cycles == 0 || cur_tsc - *prev_tsc <= app.timer_period_cycles)
		return;

	*prev_tsc = cur_tsc;
	aggregate_stats(&app.stats, lcore_apps, RTE_MAX_LCORE);
	print_stats(&app, lcore_apps, lcore_enabled);
}

static void transmit_requests(struct app *worker_app, struct rte_mbuf **bufs)
{
	uint16_t nb_build = client_conf.burst_size;
	uint16_t nb_tx = 0;
	uint8_t *request_template = request_templates[rte_lcore_id()];

	if (nb_build == 0)
		return;

	if (rte_pktmbuf_alloc_bulk(worker_app->mbuf_pool, bufs, nb_build) != 0) {
		stats_add_u64(&worker_app->stats.dropped, nb_build);
		return;
	}

	for (uint16_t i = 0; i < nb_build; i++) {
		void *data = rte_pktmbuf_append(bufs[i], request_pkt_len);

		if (data == NULL) {
			stats_add_u64(&worker_app->stats.dropped, nb_build);
			for (uint16_t j = 0; j < nb_build; j++)
				rte_pktmbuf_free(bufs[j]);
			return;
		}

		memcpy(data, request_template, request_pkt_len);
	}

	nb_tx = tx_burst_with_retry(worker_app->port_id,
		worker_app->tx_queue_id, bufs, nb_build);
	stats_add_u64(&worker_app->stats.tx, nb_tx);
	stats_add_u64(&worker_app->stats.tx_bytes, (uint64_t)nb_tx * request_pkt_len);
	stats_add_u64(&worker_app->stats.requests, nb_tx);

	free_unsent_mbufs(worker_app, bufs, nb_tx, nb_build);
}

static int client_worker_main(__rte_unused void *arg)
{
	struct app *worker_app;
	struct rte_mbuf *bufs[MAX_PKT_BURST];
	uint64_t prev_tsc = rte_rdtsc();
	uint64_t stop_tx_tsc = 0;
	uint64_t finish_deadline = 0;
	const uint64_t hz = rte_get_tsc_hz();
	const uint64_t finish_wait_tsc = hz * CLIENT_FINISH_WAIT_SECS;
	uint64_t pacing_cycles = 0;
	uint64_t next_tx_tsc = 0;
	const unsigned int lcore_id = rte_lcore_id();

	if (!lcore_enabled[lcore_id])
		return 0;

	worker_app = &lcore_apps[lcore_id];
	pacing_cycles = client_pacing_cycles_per_burst();
	if (pacing_cycles != 0)
		next_tx_tsc = rte_rdtsc() +
			(pacing_cycles * worker_app->tx_queue_id) / worker_count;
	if (client_conf.duration_secs != 0)
		stop_tx_tsc = rte_rdtsc() + client_conf.duration_secs * hz;

	printf("client lcore=%u port=%u rxq=%u txq=%u src_ip=%u.%u.%u.%u src_port=%u dst_mac=" RTE_ETHER_ADDR_PRT_FMT " packet_len=%u rate_mbps=%u pacing_cycles=%" PRIu64 "\n",
		lcore_id, worker_app->port_id, worker_app->rx_queue_id,
		worker_app->tx_queue_id,
		((CLIENT_INTERNAL_SRC_ADDR + worker_app->tx_queue_id) >> 24) & 0xff,
		((CLIENT_INTERNAL_SRC_ADDR + worker_app->tx_queue_id) >> 16) & 0xff,
		((CLIENT_INTERNAL_SRC_ADDR + worker_app->tx_queue_id) >> 8) & 0xff,
		(CLIENT_INTERNAL_SRC_ADDR + worker_app->tx_queue_id) & 0xff,
		(uint16_t)(CLIENT_INTERNAL_SRC_L4 + worker_app->tx_queue_id),
		RTE_ETHER_ADDR_BYTES(&client_conf.dst_mac),
		client_packet_len(&client_conf), client_conf.rate_mbps, pacing_cycles);

	while (!force_quit) {
		const uint64_t cur_tsc = rte_rdtsc();
		bool tx_done = false;
		uint16_t nb_rx;

		if (lcore_id == rte_get_main_lcore())
			maybe_print_aggregate_stats(cur_tsc, &prev_tsc);

		nb_rx = rte_eth_rx_burst(worker_app->port_id, worker_app->rx_queue_id,
			bufs, client_conf.burst_size);
		if (nb_rx != 0) {
			uint64_t rx_bytes = 0;

			stats_add_u64(&worker_app->stats.rx, nb_rx);
			for (uint16_t i = 0; i < nb_rx; i++) {
				rx_bytes += rte_pktmbuf_pkt_len(bufs[i]);
				rte_pktmbuf_free(bufs[i]);
			}
			stats_add_u64(&worker_app->stats.rx_bytes, rx_bytes);
			stats_add_u64(&worker_app->stats.replies, nb_rx);
		}

		if (stop_tx_tsc != 0 && cur_tsc >= stop_tx_tsc)
			tx_done = true;

		if (tx_done) {
			if (finish_deadline == 0) {
				finish_deadline = cur_tsc + finish_wait_tsc;
			} else if (cur_tsc > finish_deadline) {
				force_quit = true;
			}
			rte_pause();
			continue;
		}

		if (pacing_cycles != 0) {
			const uint64_t tx_tsc = rte_rdtsc();

			if (tx_tsc < next_tx_tsc) {
				rte_pause();
				continue;
			}
			next_tx_tsc += pacing_cycles;
			if (tx_tsc > next_tx_tsc &&
					tx_tsc - next_tx_tsc > pacing_cycles)
				next_tx_tsc = tx_tsc + pacing_cycles;
		}

		transmit_requests(worker_app, bufs);
	}

	return 0;
}

void client_app_run(void)
{
	unsigned int lcore_id;
	int ret;

	ret = rte_eal_mp_remote_launch(client_worker_main, NULL, CALL_MAIN);
	if (ret != 0)
		app_exit(&app, "failed to launch client lcores\n");

	RTE_LCORE_FOREACH_WORKER(lcore_id) {
		if (rte_eal_wait_lcore(lcore_id) < 0)
			app_exit(&app, "failed to join client worker\n");
	}

	aggregate_stats(&app.stats, lcore_apps, RTE_MAX_LCORE);
	print_stats(&app, lcore_apps, lcore_enabled);
	stop_port(&app);
}

int client_app_init(int argc, char **argv)
{
	unsigned int lcores[MAX_APP_WORKERS];
	int ret;

	ret = parse_client_args(argc, argv, &app, &client_conf);
	if (ret != 0)
		return ret;

	memset(lcore_enabled, 0, sizeof(lcore_enabled));
	memset(lcore_apps, 0, sizeof(lcore_apps));
	memset(request_templates, 0, sizeof(request_templates));

	worker_count = app_get_enabled_lcores(lcores, MAX_APP_WORKERS);
	if (worker_count == 0)
		app_exit(&app, "no enabled lcores found\n");

	app.rx_queue_id = worker_count - 1;
	app.tx_queue_id = worker_count - 1;
	app.target_rate_mbps = client_conf.rate_mbps;
	if (init_runtime(&app) < 0)
		app_exit(&app, NULL);

	for (uint16_t i = 0; i < worker_count; i++) {
		const unsigned int lcore_id = lcores[i];

		lcore_enabled[lcore_id] = true;
		init_worker_app(&lcore_apps[lcore_id], &app, i);
		lcore_apps[lcore_id].timer_period_cycles = 0;
		prepare_request_template(&lcore_apps[lcore_id], request_templates[lcore_id]);
		printf("client map lcore=%u rxq=%u txq=%u\n", lcore_id, i, i);
	}

	printf("client using %u lcores and %u queue pairs\n",
		worker_count, worker_count);
	return 0;
}
