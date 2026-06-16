#include <getopt.h>
#include <stdio.h>
#include <stdint.h>

#include <rte_udp.h>

#include "client.h"

static void fill_payload_pattern(struct client_conf *conf, uint16_t len)
{
	for (uint16_t i = 0; i < len; i++)
		conf->payload[i] = (char)('A' + (i % 26));
	conf->payload[len] = '\0';
	conf->payload_len = len;
}

static void usage(const char *prgname, const struct client_conf *conf)
{
	printf("%s [EAL options] -- [options]\n"
	       "UDP Echo client traffic generator for NIC throughput testing.\n\n",
		prgname);
	print_common_usage();
	printf("  --burst N: TX/RX burst size, 1-%u (default %u)\n"
	       "  --payload-len N: generated payload length, 0-%u (default %u)\n"
	       "  --duration SEC: transmit duration, 0 means unlimited (default 0)\n"
	       "  --rate-mbps N: target L1 wire rate in Mbps, 0 means unlimited (default 0)\n"
	       "  --dst-mac MAC|broadcast: destination Ethernet address (default broadcast)\n"
	       "  -h: show this help\n",
		MAX_PKT_BURST, conf->burst_size,
		MAX_PAYLOAD, DEFAULT_CLIENT_PAYLOAD_LEN);
}

uint16_t client_packet_len(const struct client_conf *conf)
{
	return (uint16_t)(sizeof(struct rte_ether_hdr) +
					sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr) +
					conf->payload_len);
}

int parse_client_args(int argc, char **argv, struct app *app,
				struct client_conf *conf)
{

	for (unsigned int i = 0; i < RTE_ETHER_ADDR_LEN; i++)
		conf->dst_mac.addr_bytes[i] = 0xff;
	conf->payload_len = DEFAULT_CLIENT_PAYLOAD_LEN;
	conf->burst_size = MAX_PKT_BURST;
	conf->rate_mbps = 0;
	conf->duration_secs = 0;
	fill_payload_pattern(conf, conf->payload_len);
	static const char short_options[] = "hp:PT:";


	enum {
		OPT_PORT = 256,
		OPT_BURST,
		OPT_DST_MAC,
		OPT_PAYLOAD_LEN,
		OPT_DURATION,
		OPT_RATE_MBPS,
	};

	static const struct option long_options[] = {
		{ "port", required_argument, NULL, OPT_PORT },
		{ "burst", required_argument, NULL, OPT_BURST },
		{ "dst-mac", required_argument, NULL, OPT_DST_MAC },
		{ "payload-len", required_argument, NULL, OPT_PAYLOAD_LEN },
		{ "duration", required_argument, NULL, OPT_DURATION },
		{ "rate-mbps", required_argument, NULL, OPT_RATE_MBPS },
		{ NULL, 0, NULL, 0 },
	};

	int opt;
	int option_index;


	while ((opt = getopt_long(argc, argv, short_options, long_options,
			&option_index)) != -1) {
		uint32_t value32;
		uint64_t value64;

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
			if (parse_u32(optarg, 0, UINT16_MAX, &value32) < 0) {
				app_exit(app, "invalid DPDK port id\n");
			}
			app->port_id = (uint16_t)value32;
			app->port_set = true;
			break;
		case OPT_BURST:
			if (parse_u32(optarg, 1, MAX_PKT_BURST,
					&value32) < 0) {
				app_exit(app, "invalid burst size\n");
			}
			conf->burst_size = (uint16_t)value32;
			break;
		case OPT_DST_MAC:
			if (parse_mac(optarg, &conf->dst_mac) < 0) {
				app_exit(app, "invalid destination MAC address\n");
			}
			break;
		case OPT_PAYLOAD_LEN:
			if (parse_u32(optarg, 0, MAX_PAYLOAD, &value32) < 0) {
				app_exit(app, "invalid payload length\n");
			}
			fill_payload_pattern(conf, (uint16_t)value32);
			break;
		case OPT_DURATION:
			if (parse_u64(optarg, 0, UINT64_MAX, &value64) < 0) {
				app_exit(app, "invalid duration\n");
			}
			conf->duration_secs = value64;
			break;
		case OPT_RATE_MBPS:
			if (parse_u32(optarg, 0, UINT32_MAX, &value32) < 0) {
				app_exit(app, "invalid rate-mbps\n");
			}
			conf->rate_mbps = value32;
			break;
		default:
			usage(argv[0], conf);
			app_exit(app, "invalid application arguments\n");
		}
	}

	optind = 1;
	return 0;
}
