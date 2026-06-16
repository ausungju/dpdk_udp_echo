#include <stdlib.h>
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
#include <rte_ring.h>
#include <rte_udp.h>

#include "common.h"
#include "server.h"

#define SERVER_SW_RING_SIZE 4096

static struct app app = APP_INITIALIZER("echo_server");
static struct server_conf server_conf;
static struct app lcore_apps[RTE_MAX_LCORE];
static bool lcore_enabled[RTE_MAX_LCORE];
static struct rte_ring *worker_rings[RTE_MAX_LCORE];
static unsigned int worker_lcores[MAX_APP_WORKERS];
static uint16_t lcore_slot[RTE_MAX_LCORE];
static uint16_t worker_count;
static bool use_sw_fanout;

enum server_mode_override {
	SERVER_MODE_AUTO = 0,
	SERVER_MODE_SW_FANOUT_ON,
	SERVER_MODE_SW_FANOUT_OFF,
};

static enum server_mode_override server_mode = SERVER_MODE_AUTO;

static bool prepare_echo_packet(struct app *worker_app, struct rte_mbuf *m)
{
	struct rte_ether_hdr *eth = NULL;
	struct rte_ipv4_hdr *ip = NULL;
	struct rte_udp_hdr *udp = NULL;
	struct rte_ether_addr peer_mac;
	rte_be32_t peer_ip;
	rte_be16_t peer_l4;
	uint16_t total_len;
	uint16_t udp_len;

	if (!mbuf_has_contiguous_headers(m,
				sizeof(*eth) + sizeof(*ip) + sizeof(*udp)))
		return false;

	eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
	if (eth->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4))
		return false;

	ip = (struct rte_ipv4_hdr *)(eth + 1);
	if (ip->version_ihl != 0x45 || ip->next_proto_id != IPPROTO_UDP ||
			!ipv4_is_unfragmented(ip))
		return false;

	udp = (struct rte_udp_hdr *)(ip + 1);
	total_len = rte_be_to_cpu_16(ip->total_length);
	udp_len = rte_be_to_cpu_16(udp->dgram_len);
	if (total_len < sizeof(*ip) + sizeof(*udp) ||
			rte_pktmbuf_pkt_len(m) < sizeof(*eth) + total_len ||
			udp_len < sizeof(*udp) || udp_len > total_len - sizeof(*ip))
		return false;

	rte_ether_addr_copy(&eth->src_addr, &peer_mac);
	rte_ether_addr_copy(&peer_mac, &eth->dst_addr);
	rte_ether_addr_copy(&worker_app->port_mac, &eth->src_addr);

	peer_ip = ip->src_addr;
	ip->src_addr = ip->dst_addr;
	ip->dst_addr = peer_ip;
	if (ip->time_to_live != 64) {
		ip->time_to_live = 64;
		fill_ipv4_checksum(ip);
	}

	peer_l4 = udp->src_port;
	udp->src_port = udp->dst_port;
	udp->dst_port = peer_l4;
	udp->dgram_cksum = 0;

	return true;
}

static void maybe_print_aggregate_stats(uint64_t cur_tsc, uint64_t *prev_tsc)
{
	if (app.timer_period_cycles == 0 || cur_tsc - *prev_tsc <= app.timer_period_cycles)
		return;

	*prev_tsc = cur_tsc;
	aggregate_stats(&app.stats, lcore_apps, RTE_MAX_LCORE);
	print_stats(&app, lcore_apps, lcore_enabled);
}

static void load_server_mode(void)
{
	const char *value = getenv("ECHO_SW_FANOUT");

	if (value == NULL || strcmp(value, "") == 0 || strcmp(value, "auto") == 0) {
		server_mode = SERVER_MODE_AUTO;
		return;
	}
	if (strcmp(value, "on") == 0) {
		server_mode = SERVER_MODE_SW_FANOUT_ON;
		return;
	}
	if (strcmp(value, "off") == 0) {
		server_mode = SERVER_MODE_SW_FANOUT_OFF;
		return;
	}

	fprintf(stderr, "invalid ECHO_SW_FANOUT=%s, expected auto|on|off\n", value);
	app_exit(&app, NULL);
}


static void process_echo_burst(struct app *worker_app,
	struct rte_mbuf **bufs, uint16_t nb_rx)
{
	uint16_t echo_len[MAX_PKT_BURST];
	uint16_t nb_echo = 0;
	uint16_t nb_tx;
	uint16_t echoed = 0;
	uint16_t ignored = 0;
	uint64_t rx_bytes = 0;
	uint64_t echo_bytes = 0;
	uint64_t tx_bytes = 0;

	if (nb_rx == 0)
		return;

	for (uint16_t i = 0; i < nb_rx; i++) {
		const uint16_t len = rte_pktmbuf_pkt_len(bufs[i]);

		rx_bytes += len;
		if (prepare_echo_packet(worker_app, bufs[i])) {
			echo_len[nb_echo] = len;
			echo_bytes += len;
			bufs[nb_echo++] = bufs[i];
			echoed++;
		} else {
			ignored++;
			rte_pktmbuf_free(bufs[i]);
		}
	}

	stats_add_u64(&worker_app->stats.rx, nb_rx);
	stats_add_u64(&worker_app->stats.rx_bytes, rx_bytes);
	if (echoed != 0)
		stats_add_u64(&worker_app->stats.echoed, echoed);
	if (ignored != 0)
		stats_add_u64(&worker_app->stats.ignored, ignored);

	if (nb_echo == 0)
		return;

	nb_tx = rte_eth_tx_burst(worker_app->port_id, worker_app->tx_queue_id,
		bufs, nb_echo);
	if (nb_tx > nb_echo)
		nb_tx = nb_echo;
	stats_add_u64(&worker_app->stats.tx, nb_tx);
	if (nb_tx == nb_echo) {
		tx_bytes = echo_bytes;
	} else {
		for (uint16_t i = 0; i < nb_tx; i++)
			tx_bytes += echo_len[i];
	}
	if (tx_bytes != 0)
		stats_add_u64(&worker_app->stats.tx_bytes, tx_bytes);

	free_unsent_mbufs(worker_app, bufs, nb_tx, nb_echo);
}

static void free_dropped_dispatch(struct app *worker_app,
	struct rte_mbuf **bufs, uint16_t start, uint16_t count)
{
	if (count == 0)
		return;
	stats_add_u64(&worker_app->stats.dropped, count);
	for (uint16_t i = start; i < start + count; i++)
		rte_pktmbuf_free(bufs[i]);
}

static int server_poll_mode_main(void)
{
	struct app *worker_app;
	struct rte_mbuf *bufs[MAX_PKT_BURST];
	uint64_t prev_tsc = rte_rdtsc();
	const unsigned int lcore_id = rte_lcore_id();

	if (!lcore_enabled[lcore_id])
		return 0;

	worker_app = &lcore_apps[lcore_id];
	printf("server lcore=%u port=%u rxq=%u txq=%u burst=%u\n",
		lcore_id, worker_app->port_id, worker_app->rx_queue_id,
		worker_app->tx_queue_id, server_conf.burst_size);

	while (!force_quit) {
		uint16_t nb_rx;
		const uint64_t cur_tsc = rte_rdtsc();

		if (lcore_id == rte_get_main_lcore())
			maybe_print_aggregate_stats(cur_tsc, &prev_tsc);

		nb_rx = rte_eth_rx_burst(worker_app->port_id, worker_app->rx_queue_id,
			bufs, server_conf.burst_size);
		if (nb_rx == 0) {
			rte_pause();
			continue;
		}
		process_echo_burst(worker_app, bufs, nb_rx);
	}

	return 0;
}

static int server_sw_dispatch_main(void)
{
	struct app *main_app = &lcore_apps[rte_lcore_id()];
	struct rte_mbuf *rx_bufs[MAX_PKT_BURST];
	struct rte_mbuf *local_bufs[MAX_PKT_BURST];
	struct rte_mbuf *ring_bufs[MAX_APP_WORKERS][MAX_PKT_BURST];
	uint16_t ring_counts[MAX_APP_WORKERS];
	uint16_t local_count;
	uint16_t rr_slot = 0;
	uint64_t prev_tsc = rte_rdtsc();
	const unsigned int main_lcore = rte_lcore_id();

	printf("server dispatcher lcore=%u port=%u rxq=%u workers=%u\n",
		main_lcore, main_app->port_id, main_app->rx_queue_id, worker_count);

	while (!force_quit) {
		const uint64_t cur_tsc = rte_rdtsc();
		uint16_t nb_rx;

		maybe_print_aggregate_stats(cur_tsc, &prev_tsc);
		nb_rx = rte_eth_rx_burst(main_app->port_id, 0, rx_bufs,
			server_conf.burst_size);
		if (nb_rx == 0) {
			rte_pause();
			continue;
		}

		memset(ring_counts, 0, sizeof(ring_counts));
		local_count = 0;
		for (uint16_t i = 0; i < nb_rx; i++) {
			const uint16_t slot = rr_slot++ % worker_count;
			const unsigned int target_lcore = worker_lcores[slot];

			if (target_lcore == main_lcore) {
				local_bufs[local_count++] = rx_bufs[i];
			} else {
				ring_bufs[slot][ring_counts[slot]++] = rx_bufs[i];
			}
		}

		if (local_count != 0)
			process_echo_burst(main_app, local_bufs, local_count);

		for (uint16_t slot = 0; slot < worker_count; slot++) {
			struct app *target_app;
			unsigned int target_lcore;
			unsigned int enqueued;
			uint16_t count;

			count = ring_counts[slot];
			if (count == 0)
				continue;
			target_lcore = worker_lcores[slot];
			if (target_lcore == main_lcore)
				continue;
			target_app = &lcore_apps[target_lcore];
			enqueued = rte_ring_enqueue_burst(worker_rings[target_lcore],
				(void * const *)ring_bufs[slot], count, NULL);
			if (enqueued < count)
				free_dropped_dispatch(target_app, ring_bufs[slot], enqueued,
					count - enqueued);
		}
	}

	return 0;
}

static int server_sw_worker_main(void)
{
	struct app *worker_app = &lcore_apps[rte_lcore_id()];
	struct rte_mbuf *bufs[MAX_PKT_BURST];

	printf("server worker lcore=%u txq=%u ring=%s\n",
		rte_lcore_id(), worker_app->tx_queue_id,
		worker_rings[rte_lcore_id()] != NULL ? "on" : "off");

	while (!force_quit) {
		uint16_t nb_rx;

		nb_rx = (uint16_t)rte_ring_dequeue_burst(worker_rings[rte_lcore_id()],
			(void **)bufs, MAX_PKT_BURST, NULL);
		if (nb_rx == 0) {
			rte_pause();
			continue;
		}
		process_echo_burst(worker_app, bufs, nb_rx);
	}

	return 0;
}

static int server_worker_main(__rte_unused void *arg)
{
	const unsigned int lcore_id = rte_lcore_id();

	if (!lcore_enabled[lcore_id])
		return 0;
	if (!use_sw_fanout)
		return server_poll_mode_main();
	if (lcore_id == rte_get_main_lcore())
		return server_sw_dispatch_main();
	return server_sw_worker_main();
}

void server_app_run(void)
{
	unsigned int lcore_id;
	int ret;

	ret = rte_eal_mp_remote_launch(server_worker_main, NULL, CALL_MAIN);
	if (ret != 0)
		app_exit(&app, "failed to launch server lcores\n");

	RTE_LCORE_FOREACH_WORKER(lcore_id) {
		if (rte_eal_wait_lcore(lcore_id) < 0)
			app_exit(&app, "failed to join server worker\n");
	}

	aggregate_stats(&app.stats, lcore_apps, RTE_MAX_LCORE);
	print_stats(&app, lcore_apps, lcore_enabled);
	stop_port(&app);
}

int server_app_init(int argc, char **argv)
{
	char ring_name[64];
	int ret;

	ret = parse_server_args(argc, argv, &app, &server_conf);
	if (ret != 0)
		return ret;

	load_server_mode();

	memset(lcore_enabled, 0, sizeof(lcore_enabled));
	memset(lcore_apps, 0, sizeof(lcore_apps));
	memset(worker_rings, 0, sizeof(worker_rings));
	memset(worker_lcores, 0, sizeof(worker_lcores));
	for (unsigned int i = 0; i < RTE_MAX_LCORE; i++)
		lcore_slot[i] = UINT16_MAX;

	worker_count = app_get_enabled_lcores(worker_lcores, MAX_APP_WORKERS);
	if (worker_count == 0)
		app_exit(&app, "no enabled lcores found\n");

	app.rx_queue_id = worker_count - 1;
	app.tx_queue_id = worker_count - 1;
	if (init_runtime(&app) < 0)
		app_exit(&app, NULL);

	switch (server_mode) {
	case SERVER_MODE_SW_FANOUT_ON:
		use_sw_fanout = worker_count > 1;
		break;
	case SERVER_MODE_SW_FANOUT_OFF:
		use_sw_fanout = false;
		break;
	case SERVER_MODE_AUTO:
	default:
		use_sw_fanout = worker_count > 1 && !app.rss_enabled;
		break;
	}
	for (uint16_t i = 0; i < worker_count; i++) {
		const unsigned int lcore_id = worker_lcores[i];

		lcore_enabled[lcore_id] = true;
		lcore_slot[lcore_id] = i;
		init_worker_app(&lcore_apps[lcore_id], &app, i);
		lcore_apps[lcore_id].timer_period_cycles = 0;
		if (use_sw_fanout && lcore_id != rte_get_main_lcore()) {
			snprintf(ring_name, sizeof(ring_name), "srv_ring_%u", lcore_id);
			worker_rings[lcore_id] = rte_ring_create(ring_name,
				SERVER_SW_RING_SIZE, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);
			if (worker_rings[lcore_id] == NULL)
				app_exit(&app, "failed to create software fanout ring\n");
		}
		printf("server map lcore=%u rxq=%u txq=%u\n", lcore_id, i, i);
	}

	if (use_sw_fanout)
		printf("server mode: software fanout (%s)\n",
			server_mode == SERVER_MODE_SW_FANOUT_ON ? "forced" : "auto");
	else
		printf("server mode: direct polling (%s)\n",
			server_mode == SERVER_MODE_SW_FANOUT_OFF ? "forced" : "auto");
	if (!app.rss_enabled && !use_sw_fanout && worker_count > 1)
		printf("server warning: RSS unavailable and software fanout disabled, traffic may stay on one core\n");
	printf("server using %u lcores and %u queue pairs\n",
		worker_count, worker_count);
	return 0;
}
