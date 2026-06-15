#include <getopt.h>
#include <stdio.h>
#include <stdint.h>

#include "server.h"

static void usage(const char *prgname, const struct server_conf *conf)
{
	printf("%s [EAL options] -- [options]\n"
	       "UDP Echo server for NIC throughput testing.\n\n",
		prgname);
	print_common_usage();
	printf("  --burst N: RX/TX burst size, 1-%u (default %u)\n"
	       "  -h: show this help\n",
		MAX_PKT_BURST, conf->burst_size);
}

int parse_server_args(int argc, char **argv, struct app *app,
				struct server_conf *conf)
{
	enum {
		OPT_PORT = 256,
		OPT_BURST,
	};

	static const char short_options[] = "hp:PT:";
	static const struct option long_options[] = {
		{ "port", required_argument, NULL, OPT_PORT },
		{ "burst", required_argument, NULL, OPT_BURST },
		{ NULL, 0, NULL, 0 },
	};

	int opt;
	int option_index;

	conf->burst_size = MAX_PKT_BURST;

	while ((opt = getopt_long(argc, argv, short_options, long_options,
			&option_index)) != -1) {
		uint32_t value;

		switch (opt) {
		case 'h':
			usage(argv[0], conf);
			return 1;
		case 'p':
			if (parse_portmask(optarg, &app->port_id) < 0) {
				app_exit(app, "invalid DPDK port mask\n");
			}
			app->port_set = true;
			break;
		case 'P':
			app->promiscuous_on = true;
			break;
		case 'T':
			if (parse_timer(optarg, &app->timer_period_secs) < 0) {
				app_exit(app, "invalid timer period\n");
			}
			break;
		case OPT_PORT:
			if (parse_u32(optarg, 0, UINT16_MAX, &value) < 0) {
				app_exit(app, "invalid DPDK port id\n");
			}
			app->port_id = (uint16_t)value;
			app->port_set = true;
			break;
		case OPT_BURST:
			if (parse_u32(optarg, 1, MAX_PKT_BURST, &value) < 0) {
				app_exit(app, "invalid burst size\n");
			}
			conf->burst_size = (uint16_t)value;
			break;
		default:
			usage(argv[0], conf);
			app_exit(app, "invalid application arguments\n");
		}
	}

	optind = 1;
	return 0;
}

