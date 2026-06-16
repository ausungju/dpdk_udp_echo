#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rte_errno.h>
#include <rte_lcore.h>

#include "common.h"

volatile bool force_quit;

void signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM)
		force_quit = true;
}

uint16_t app_get_enabled_lcores(unsigned int *lcores, uint16_t max_lcores)
{
	unsigned int lcore_id;
	const unsigned int main_lcore = rte_get_main_lcore();
	uint16_t count = 0;

	if (count < max_lcores)
		lcores[count] = main_lcore;
	count++;

	RTE_LCORE_FOREACH_WORKER(lcore_id) {
		if (count < max_lcores)
			lcores[count] = lcore_id;
		count++;
	}

	return count;
}

void init_worker_app(struct app *dst, const struct app *src,
	uint16_t queue_id)
{
	*dst = *src;
	dst->rx_queue_id = queue_id;
	dst->tx_queue_id = queue_id;
	dst->port_started = false;
	memset(&dst->stats, 0, sizeof(dst->stats));
	memset(&dst->prev_stats, 0, sizeof(dst->prev_stats));
	dst->prev_stats_tsc = rte_rdtsc();
}

void aggregate_stats(struct stats *dst, const struct app *apps,
	uint16_t count)
{
	memset(dst, 0, sizeof(*dst));
	for (uint16_t i = 0; i < count; i++) {
		dst->rx += stats_read_u64(&apps[i].stats.rx);
		dst->tx += stats_read_u64(&apps[i].stats.tx);
		dst->rx_bytes += stats_read_u64(&apps[i].stats.rx_bytes);
		dst->tx_bytes += stats_read_u64(&apps[i].stats.tx_bytes);
		dst->dropped += stats_read_u64(&apps[i].stats.dropped);
		dst->echoed += stats_read_u64(&apps[i].stats.echoed);
		dst->ignored += stats_read_u64(&apps[i].stats.ignored);
		dst->requests += stats_read_u64(&apps[i].stats.requests);
		dst->replies += stats_read_u64(&apps[i].stats.replies);
	}
}

void free_unsent_mbufs(struct app *app,
	struct rte_mbuf **bufs, uint16_t nb_tx, uint16_t nb_total)
{
	if (nb_tx < nb_total) {
		stats_add_u64(&app->stats.dropped, nb_total - nb_tx);
		for (uint16_t i = nb_tx; i < nb_total; i++)
			rte_pktmbuf_free(bufs[i]);
	}
}

static int init_port(struct app *app)
{
	struct rte_eth_conf port_conf = {0};
	struct rte_eth_dev_info dev_info;
	struct rte_eth_link link = {0};
	struct rte_eth_rxconf rxconf;
	struct rte_eth_txconf txconf;
	uint16_t nb_rxd = RX_DESC_DEFAULT;
	uint16_t nb_txd = TX_DESC_DEFAULT;
	uint16_t rx_queue_count = app->rx_queue_id + 1;
	uint16_t tx_queue_count = app->tx_queue_id + 1;
	uint64_t rss_hf = 0;
	int ret;

	ret = rte_eth_dev_info_get(app->port_id, &dev_info);
	if (ret != 0)
		return ret;

	app->rss_enabled = false;
	if (rx_queue_count > 1) {
		rss_hf = RTE_ETH_RSS_NONFRAG_IPV4_UDP;
		rss_hf &= dev_info.flow_type_rss_offloads;
		if (rss_hf == 0) {
			rss_hf = RTE_ETH_RSS_IPV4;
			rss_hf &= dev_info.flow_type_rss_offloads;
		}
		if (rss_hf != 0) {
			port_conf.rxmode.mq_mode = RTE_ETH_MQ_RX_RSS;
			port_conf.rx_adv_conf.rss_conf.rss_hf = rss_hf;
			app->rss_enabled = true;
		} else {
			rx_queue_count = 1;
		}
	}

	if (rx_queue_count > dev_info.max_rx_queues ||
			tx_queue_count > dev_info.max_tx_queues) {
		fprintf(stderr,
			"port %u supports max RX/TX queues %u/%u, requested %u/%u\n",
			app->port_id, dev_info.max_rx_queues, dev_info.max_tx_queues,
			rx_queue_count, tx_queue_count);
		return -EINVAL;
	}

	if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
		port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

	ret = rte_eth_dev_configure(app->port_id, rx_queue_count,
		tx_queue_count, &port_conf);
	if (ret < 0)
		return ret;

	ret = rte_eth_dev_adjust_nb_rx_tx_desc(app->port_id, &nb_rxd, &nb_txd);
	if (ret < 0)
		return ret;

	rxconf = dev_info.default_rxconf;
	rxconf.offloads = port_conf.rxmode.offloads;
	for (uint16_t q = 0; q < rx_queue_count; q++) {
		ret = rte_eth_rx_queue_setup(app->port_id, q, nb_rxd,
			rte_eth_dev_socket_id(app->port_id), &rxconf, app->mbuf_pool);
		if (ret < 0)
			return ret;
	}

	txconf = dev_info.default_txconf;
	txconf.offloads = port_conf.txmode.offloads;
	for (uint16_t q = 0; q < tx_queue_count; q++) {
		ret = rte_eth_tx_queue_setup(app->port_id, q, nb_txd,
			rte_eth_dev_socket_id(app->port_id), &txconf);
		if (ret < 0)
			return ret;
	}

	ret = rte_eth_dev_start(app->port_id);
	if (ret < 0)
		return ret;
	app->port_started = true;

	if (app->promiscuous_on) {
		ret = rte_eth_promiscuous_enable(app->port_id);
		if (ret != 0)
			return ret;
	}

	ret = rte_eth_macaddr_get(app->port_id, &app->port_mac);
	if (ret != 0)
		return ret;

	if (rte_eth_link_get(app->port_id, &link) == 0 && link.link_status)
		app->link_speed_mbps = link.link_speed;
	else
		app->link_speed_mbps = 0;

	printf("port %u started, rxq %u, txq %u, RSS %s, link %u Mbps, MAC " RTE_ETHER_ADDR_PRT_FMT "\n",
		app->port_id, rx_queue_count, tx_queue_count,
		app->rss_enabled ? "on" : "off",
		app->link_speed_mbps,
		RTE_ETHER_ADDR_BYTES(&app->port_mac));
	return 0;
}

int init_runtime(struct app *app)
{
	char pool_name[64];
	int ret;

	if (!app->port_set) {
		uint16_t port_id;

		RTE_ETH_FOREACH_DEV(port_id) {
			app->port_id = port_id;
			app->port_set = true;
			break;
		}
		if (!app->port_set) {
			fputs("no DPDK Ethernet ports found\n", stderr);
			return -1;
		}
	}

	if (!rte_eth_dev_is_valid_port(app->port_id)) {
		fprintf(stderr, "DPDK port %u is not present\n", app->port_id);
		return -1;
	}

	{
		size_t len = 0;
		for (; app->name[len] != '\0' && len + 6 < sizeof(pool_name); len++)
			pool_name[len] = app->name[len];
		pool_name[len++] = '_';
		pool_name[len++] = 'p';
		pool_name[len++] = 'o';
		pool_name[len++] = 'o';
		pool_name[len++] = 'l';
		pool_name[len] = '\0';
	}
	app->mbuf_pool = rte_pktmbuf_pool_create(pool_name, NUM_MBUFS,
		MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
		rte_socket_id());
	if (app->mbuf_pool == NULL) {
		fputs("cannot create mbuf pool: ", stderr);
		fputs(rte_strerror(rte_errno), stderr);
		fputc('\n', stderr);
		return -1;
	}

	app->timer_period_cycles = app->timer_period_secs * rte_get_tsc_hz();
	app->prev_stats_tsc = rte_rdtsc();

	ret = init_port(app);
	if (ret < 0) {
		fprintf(stderr, "cannot initialize DPDK port %u: %s\n",
			app->port_id, rte_strerror(-ret));
		return -1;
	}

	return 0;
}

void stop_port(struct app *app)
{
	if (!app->port_started)
		return;

	printf("closing port %u...", app->port_id);
	fflush(stdout);
	if (rte_eth_dev_stop(app->port_id) != 0)
		printf(" stop failed");
	rte_eth_dev_close(app->port_id);
	app->port_started = false;
	printf(" done\n");
}

void app_exit(struct app *app, const char *message)
{
	if (message != NULL)
		fputs(message, stderr);
	if (app != NULL)
		stop_port(app);
	rte_eal_cleanup();
	exit(EXIT_FAILURE);
}
