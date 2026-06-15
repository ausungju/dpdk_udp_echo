#include <inttypes.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#include "common.h"

static void get_csv_filename(char *filename, size_t size)
{
	time_t now = time(NULL);
	struct tm *timeinfo = localtime(&now);
	strftime(filename, size, "logs/%Y-%m-%d_%H-%M-%S.csv", timeinfo);
}

void print_stats(struct app *app)
{
	static char filename[64] = {0};
	static int header_printed = 0;
	struct stats snapshot = {
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
	const struct stats *s = &snapshot;
	struct stats *p = &app->prev_stats;
	uint64_t now = rte_rdtsc();
	uint64_t diff_tsc = now - app->prev_stats_tsc;
	double seconds = diff_tsc ? (double)diff_tsc / rte_get_tsc_hz() : 0.0;
	double rx_pps = 0.0;
	double tx_pps = 0.0;
	double rx_mbps = 0.0;
	double tx_mbps = 0.0;
	char timestamp[32] = {0};
	time_t time_now = time(NULL);
	struct tm *timeinfo = localtime(&time_now);
	FILE *fp;

	if (seconds > 0.0) {
		rx_pps = (s->rx - p->rx) / seconds;
		tx_pps = (s->tx - p->tx) / seconds;
		rx_mbps = ((s->rx_bytes - p->rx_bytes) * 8.0) / seconds / 1000000.0;
		tx_mbps = ((s->tx_bytes - p->tx_bytes) * 8.0) / seconds / 1000000.0;
	}

	if (filename[0] == '\0')
		get_csv_filename(filename, sizeof(filename));

	fp = fopen(filename, "a");
	if (fp == NULL) {
		perror("fopen");
		return;
	}

	if (!header_printed) {
		fprintf(fp, "timestamp,rx,tx,rx_bytes,tx_bytes,drop,echoed,ignored,requests,replies,rx_pps,tx_pps,rx_mbps,tx_mbps\n");
		printf("[INFO] CSV file created: %s\n", filename);
		header_printed = 1;
	}

	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);
	fprintf(fp,
		"%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.0f,%.0f,%.2f,%.2f\n",
		timestamp,
		s->rx, s->tx, s->rx_bytes, s->tx_bytes, s->dropped, s->echoed,
		s->ignored, s->requests, s->replies,
		rx_pps, tx_pps, rx_mbps, tx_mbps);
	fflush(fp);
	fclose(fp);

	app->prev_stats = snapshot;
	app->prev_stats_tsc = now;
}
