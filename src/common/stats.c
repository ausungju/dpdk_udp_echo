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
	uint64_t now_tsc, uint64_t prev_tsc)
{
	double seconds = 0.0;
	double rx_pps = 0.0;
	double tx_pps = 0.0;
	double rx_mbps = 0.0;
	double tx_mbps = 0.0;

	if (now_tsc > prev_tsc)
		seconds = (double)(now_tsc - prev_tsc) / rte_get_tsc_hz();
	if (seconds > 0.0) {
		rx_pps = (curr->rx - prev->rx) / seconds;
		tx_pps = (curr->tx - prev->tx) / seconds;
		rx_mbps = ((curr->rx_bytes - prev->rx_bytes) * 8.0) / seconds / 1000000.0;
		tx_mbps = ((curr->tx_bytes - prev->tx_bytes) * 8.0) / seconds / 1000000.0;
	}

	fprintf(fp,
		"%s,%s,%d,%u,%u,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.0f,%.0f,%.2f,%.2f\n",
		timestamp, scope, lcore_id, port_id, rxq, txq,
		curr->rx, curr->tx, curr->rx_bytes, curr->tx_bytes,
		curr->dropped, curr->echoed, curr->ignored,
		curr->requests, curr->replies,
		rx_pps, tx_pps, rx_mbps, tx_mbps);

	printf("%s scope=%s lcore=%d port=%u rxq=%u txq=%u "
		"rx=%" PRIu64 " tx=%" PRIu64 " drop=%" PRIu64
		" rx_pps=%.0f tx_pps=%.0f rx_mbps=%.2f tx_mbps=%.2f\n",
		timestamp, scope, lcore_id, port_id, rxq, txq,
		curr->rx, curr->tx, curr->dropped,
		rx_pps, tx_pps, rx_mbps, tx_mbps);
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
		fprintf(fp, "timestamp,scope,lcore,port,rxq,txq,rx,tx,rx_bytes,tx_bytes,drop,echoed,ignored,requests,replies,rx_pps,tx_pps,rx_mbps,tx_mbps\n");
		printf("[INFO] CSV file created: %s\n", filename);
		header_printed = 1;
	}

	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);
	write_stats_row(fp, timestamp, "aggregate", -1, app->port_id,
		UINT16_MAX, UINT16_MAX, &snapshot, &app->prev_stats,
		now_tsc, app->prev_stats_tsc);

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
			worker_app->prev_stats_tsc);
		worker_app->prev_stats = lcore_snapshot;
		worker_app->prev_stats_tsc = now_tsc;
	}

	fflush(fp);
	fclose(fp);

	app->prev_stats = snapshot;
	app->prev_stats_tsc = now_tsc;
}
