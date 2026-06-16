#include <inttypes.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#include <rte_lcore.h>

#include "common.h"

static void get_csv_filename(char *filename, size_t size)
{
	time_t now = time(NULL);
	struct tm *timeinfo = localtime(&now);
	strftime(filename, size, "logs/%Y-%m-%d_%H-%M-%S.csv", timeinfo);
}

static struct stats snapshot_stats(const struct app *app)
{
	return (struct stats){
		.rx = stats_read_u64(&app->stats.rx),
		.tx = stats_read_u64(&app->stats.tx),
		.rx_bytes = stats_read_u64(&app->stats.rx_bytes),
		.tx_bytes = stats_read_u64(&app->stats.tx_bytes),
		.dropped = stats_read_u64(&app->stats.dropped),
		.echoed = stats_read_u64(&app->stats.echoed),
		.ignored = stats_read_u64(&app->stats.ignored),
		.requests = stats_read_u64(&app->stats.requests),
		.replies = stats_read_u64(&app->stats.replies),
	};
}

static void write_stats_row(FILE *fp, const char *timestamp, const char *scope,
	int lcore_id, uint16_t port_id, uint16_t rxq, uint16_t txq,
	const struct stats *curr, const struct stats *prev,
	uint64_t now_tsc, uint64_t prev_tsc, uint32_t link_speed_mbps)
{
	double seconds = 0.0;
	double rx_pps = 0.0;
	double tx_pps = 0.0;
	double rx_mbps = 0.0;
	double tx_mbps = 0.0;
	double rx_wire_gbps = 0.0;
	double tx_wire_gbps = 0.0;
	double rx_link_util = 0.0;
	double tx_link_util = 0.0;
	double link_gbps = 0.0;

	if (now_tsc > prev_tsc)
		seconds = (double)(now_tsc - prev_tsc) / rte_get_tsc_hz();
	if (seconds > 0.0) {
		rx_pps = (curr->rx - prev->rx) / seconds;
		tx_pps = (curr->tx - prev->tx) / seconds;
		rx_mbps = ((curr->rx_bytes - prev->rx_bytes) * 8.0) / seconds / 1000000.0;
		tx_mbps = ((curr->tx_bytes - prev->tx_bytes) * 8.0) / seconds / 1000000.0;
		rx_wire_gbps = (((curr->rx_bytes - prev->rx_bytes) +
				((curr->rx - prev->rx) * 24.0)) * 8.0) / seconds / 1000000000.0;
		tx_wire_gbps = (((curr->tx_bytes - prev->tx_bytes) +
				((curr->tx - prev->tx) * 24.0)) * 8.0) / seconds / 1000000000.0;
	}
	if (link_speed_mbps != 0) {
		link_gbps = link_speed_mbps / 1000.0;
		rx_link_util = (rx_wire_gbps / link_gbps) * 100.0;
		tx_link_util = (tx_wire_gbps / link_gbps) * 100.0;
	}

	fprintf(fp,
		"%s,%s,%d,%u,%u,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.0f,%.0f,%.2f,%.2f,%.3f,%.3f,%.2f,%.2f\n",
		timestamp, scope, lcore_id, port_id, rxq, txq,
		curr->rx, curr->tx, curr->rx_bytes, curr->tx_bytes,
		curr->dropped, curr->echoed, curr->ignored,
		curr->requests, curr->replies,
		rx_pps, tx_pps, rx_mbps, tx_mbps, rx_wire_gbps, tx_wire_gbps,
		rx_link_util, tx_link_util);
}

static void print_console_summary(const char *timestamp, uint16_t port_id,
	const struct stats *curr, const struct stats *prev,
	uint64_t now_tsc, uint64_t prev_tsc, uint32_t link_speed_mbps,
	uint32_t target_rate_mbps)
{
	const double wire_overhead_bytes = 24.0;
	double seconds = 0.0;
	double rx_pps = 0.0;
	double tx_pps = 0.0;
	double rx_wire_gbps = 0.0;
	double tx_wire_gbps = 0.0;
	double rx_link_util = 0.0;
	double tx_link_util = 0.0;
	double link_gbps = 0.0;
	double rx_gap_mbps = 0.0;
	double tx_gap_mbps = 0.0;
	double rx_pps_gap = 0.0;
	double tx_pps_gap = 0.0;
	double drop_ps = 0.0;
	double tx_attempt_mpps = 0.0;
	double target_mbps = link_speed_mbps;
	double gap_threshold_mbps = 0.0;
	const char *tx_reason = "ok";
	uint64_t rx_delta = curr->rx - prev->rx;
	uint64_t tx_delta = curr->tx - prev->tx;
	uint64_t rx_bytes_delta = curr->rx_bytes - prev->rx_bytes;
	uint64_t tx_bytes_delta = curr->tx_bytes - prev->tx_bytes;
	uint64_t drop_delta = curr->dropped - prev->dropped;

	if (now_tsc > prev_tsc)
		seconds = (double)(now_tsc - prev_tsc) / rte_get_tsc_hz();
	if (seconds > 0.0) {
		rx_pps = rx_delta / seconds;
		tx_pps = tx_delta / seconds;
		rx_wire_gbps = ((rx_bytes_delta + (rx_delta * wire_overhead_bytes)) *
				8.0) / seconds / 1000000000.0;
		tx_wire_gbps = ((tx_bytes_delta + (tx_delta * wire_overhead_bytes)) *
				8.0) / seconds / 1000000000.0;
		drop_ps = drop_delta / seconds;
	}
	if (target_rate_mbps != 0)
		target_mbps = target_rate_mbps;

	if (link_speed_mbps != 0) {
		link_gbps = link_speed_mbps / 1000.0;
		rx_link_util = (rx_wire_gbps / link_gbps) * 100.0;
		tx_link_util = (tx_wire_gbps / link_gbps) * 100.0;
	}
	if (target_mbps != 0.0) {
		rx_gap_mbps = target_mbps - (rx_wire_gbps * 1000.0);
		tx_gap_mbps = target_mbps - (tx_wire_gbps * 1000.0);
		gap_threshold_mbps = target_mbps * 0.001;
		if (rx_delta != 0) {
			double avg_rx_wire_bytes = (rx_bytes_delta / (double)rx_delta) +
				wire_overhead_bytes;
			rx_pps_gap = ((target_mbps * 1000000.0) /
					(avg_rx_wire_bytes * 8.0)) - rx_pps;
		}
		if (tx_delta != 0) {
			double avg_tx_wire_bytes = (tx_bytes_delta / (double)tx_delta) +
				wire_overhead_bytes;
			tx_pps_gap = ((target_mbps * 1000000.0) /
					(avg_tx_wire_bytes * 8.0)) - tx_pps;
		}
	}
	if (rx_gap_mbps < 0.0)
		rx_gap_mbps = 0.0;
	if (tx_gap_mbps < 0.0)
		tx_gap_mbps = 0.0;
	if (rx_pps_gap < 0.0)
		rx_pps_gap = 0.0;
	if (tx_pps_gap < 0.0)
		tx_pps_gap = 0.0;
	tx_attempt_mpps = (tx_pps + drop_ps) / 1000000.0;
	if (tx_gap_mbps > gap_threshold_mbps && drop_ps > 0.0)
		tx_reason = "tx_backpressure";
	else if (tx_gap_mbps > gap_threshold_mbps)
		tx_reason = "needs_more_pps";

	printf("%s port=%u target=%.0fM rx=%.3fG(%.2f%%link gap=%.0fMbps/%.0fpps) "
		"tx=%.3fG(%.2f%%link gap=%.0fMbps/%.0fpps) "
		"tx_attempt=%.2fMpps unsent/s=%.2fM reason=%s\n",
		timestamp, port_id, target_mbps, rx_wire_gbps, rx_link_util,
		rx_gap_mbps, rx_pps_gap, tx_wire_gbps, tx_link_util,
		tx_gap_mbps, tx_pps_gap, tx_attempt_mpps, drop_ps / 1000000.0,
		tx_reason);
}


void print_stats(struct app *app, const struct app *apps,
	const bool *lcore_enabled)
{
	static char filename[64] = {0};
	static int header_printed = 0;
	struct stats snapshot = snapshot_stats(app);
	uint64_t now_tsc = rte_rdtsc();
	char timestamp[32] = {0};
	time_t time_now = time(NULL);
	struct tm *timeinfo = localtime(&time_now);
	FILE *fp;
	unsigned int lcore_id;

	if (filename[0] == '\0')
		get_csv_filename(filename, sizeof(filename));

	fp = fopen(filename, "a");
	if (fp == NULL) {
		perror("fopen");
		return;
	}

	if (!header_printed) {
		fprintf(fp, "timestamp,scope,lcore,port,rxq,txq,rx,tx,rx_bytes,tx_bytes,drop,echoed,ignored,requests,replies,rx_pps,tx_pps,rx_mbps,tx_mbps,rx_wire_gbps,tx_wire_gbps,rx_link_util,tx_link_util\n");
		printf("[INFO] CSV file created: %s\n", filename);
		header_printed = 1;
	}

	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);
	write_stats_row(fp, timestamp, "aggregate", -1, app->port_id,
		UINT16_MAX, UINT16_MAX, &snapshot, &app->prev_stats,
		now_tsc, app->prev_stats_tsc, app->link_speed_mbps);
	print_console_summary(timestamp, app->port_id, &snapshot, &app->prev_stats,
		now_tsc, app->prev_stats_tsc, app->link_speed_mbps,
		app->target_rate_mbps);

	RTE_LCORE_FOREACH(lcore_id) {
		struct stats lcore_snapshot;
		struct app *worker_app;

		if (!lcore_enabled[lcore_id])
			continue;
		worker_app = (struct app *)&apps[lcore_id];
		lcore_snapshot = snapshot_stats(worker_app);
		write_stats_row(fp, timestamp, "lcore", (int)lcore_id,
			worker_app->port_id, worker_app->rx_queue_id,
			worker_app->tx_queue_id, &lcore_snapshot,
			&worker_app->prev_stats, now_tsc,
			worker_app->prev_stats_tsc, worker_app->link_speed_mbps);
		worker_app->prev_stats = lcore_snapshot;
		worker_app->prev_stats_tsc = now_tsc;
	}

	fflush(fp);
	fclose(fp);

	app->prev_stats = snapshot;
	app->prev_stats_tsc = now_tsc;
}
