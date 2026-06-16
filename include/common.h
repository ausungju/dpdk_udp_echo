#ifndef ECHO_COMMON_H
#define ECHO_COMMON_H

#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>

#include <rte_byteorder.h>
#include <rte_common.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

#define MAX_PKT_BURST 64
#define RX_DESC_DEFAULT 2048
#define TX_DESC_DEFAULT 2048
#define TX_BURST_RETRY 32
#define MEMPOOL_CACHE_SIZE 256
#define NUM_MBUFS 65535
#define MAX_TIMER_PERIOD 86400
#define MAX_APP_WORKERS RTE_MAX_LCORE

struct stats {
	uint64_t rx;
	uint64_t tx;
	uint64_t rx_bytes;
	uint64_t tx_bytes;
	uint64_t dropped;
	uint64_t echoed;
	uint64_t ignored;
	uint64_t requests;
	uint64_t replies;
};

struct app {
	const char *name;
	uint16_t port_id;
	uint16_t rx_queue_id;
	uint16_t tx_queue_id;
	bool port_set;
	bool port_started;
	bool promiscuous_on;
	bool rss_enabled;
	uint32_t link_speed_mbps;
	uint32_t target_rate_mbps;
	uint64_t timer_period_secs;
	uint64_t timer_period_cycles;
	uint64_t prev_stats_tsc;
	struct stats prev_stats;
	struct rte_mempool *mbuf_pool;
	struct rte_ether_addr port_mac;
	struct stats stats;
};

#define APP_INITIALIZER(name_) \
	{ \
		.name = (name_), \
		.timer_period_secs = 1, \
	}

void print_common_usage(void);
int parse_portmask(const char *arg, uint16_t *port_id);
int parse_u32(const char *arg, uint32_t min, uint32_t max,
	uint32_t *value);
int parse_u64(const char *arg, uint64_t min, uint64_t max,
	uint64_t *value);
int parse_timer(const char *arg, uint64_t *timer_secs);
int parse_mac(const char *arg, struct rte_ether_addr *addr);
uint16_t app_get_enabled_lcores(unsigned int *lcores, uint16_t max_lcores);
void init_worker_app(struct app *dst, const struct app *src,
	uint16_t queue_id);
void aggregate_stats(struct stats *dst, const struct app *apps,
	uint16_t count);
int init_runtime(struct app *app);
void app_exit(struct app *app, const char *message);
void print_stats(struct app *app, const struct app *apps,
	const bool *lcore_enabled);
void stop_port(struct app *app);
uint16_t tx_burst_with_retry(uint16_t port_id, uint16_t queue_id,
	struct rte_mbuf **bufs, uint16_t nb_total);
void free_unsent_mbufs(struct app *app,
	struct rte_mbuf **bufs, uint16_t nb_tx, uint16_t nb_total);

extern volatile bool force_quit;
void signal_handler(int signum);

static inline void stats_add_u64(uint64_t *value, uint64_t delta)
{
	__atomic_fetch_add(value, delta, __ATOMIC_RELAXED);
}

static inline void stats_inc_u64(uint64_t *value)
{
	stats_add_u64(value, 1);
}

static inline uint64_t stats_read_u64(const uint64_t *value)
{
	return __atomic_load_n(value, __ATOMIC_RELAXED);
}

static inline bool mbuf_has_contiguous_headers(const struct rte_mbuf *m, uint32_t len)
{
	return rte_pktmbuf_pkt_len(m) >= len && rte_pktmbuf_data_len(m) >= len;
}

static inline bool ipv4_is_unfragmented(const struct rte_ipv4_hdr *ip)
{
	return (rte_be_to_cpu_16(ip->fragment_offset) & 0x3fff) == 0;
}

static inline void fill_ipv4_checksum(struct rte_ipv4_hdr *ip)
{
	ip->hdr_checksum = 0;
	ip->hdr_checksum = rte_ipv4_cksum(ip);
}

#endif /* ECHO_COMMON_H */
