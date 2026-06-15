#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

void print_common_usage(void)
{
	printf("  -p PORTMASK: hexadecimal DPDK port mask; first set port is used\n"
	       "  --port PORT: DPDK port id to use\n"
	       "  -P: enable promiscuous mode\n"
	       "  -T PERIOD: print performance stats every PERIOD seconds (default 1, 0 disables)\n");
}

int parse_portmask(const char *arg, uint16_t *port_id)
{
	char *end = NULL;
	unsigned long long mask;

	errno = 0;
	mask = strtoull(arg, &end, 16);
	if (errno != 0 || arg[0] == '\0' || end == NULL || *end != '\0' ||
			mask == 0)
		return -1;

	for (uint16_t port = 0; port < RTE_MAX_ETHPORTS && port < 64; port++) {
		if ((mask & (1ULL << port)) != 0) {
			*port_id = port;
			return 0;
		}
	}

	return -1;
}

int parse_u32(const char *arg, uint32_t min, uint32_t max,
	uint32_t *value)
{
	char *end = NULL;
	unsigned long n;

	errno = 0;
	n = strtoul(arg, &end, 10);
	if (errno != 0 || arg[0] == '\0' || end == NULL || *end != '\0' ||
			n < min || n > max)
		return -1;

	*value = (uint32_t)n;
	return 0;
}

int parse_u64(const char *arg, uint64_t min, uint64_t max,
	uint64_t *value)
{
	char *end = NULL;
	unsigned long long n;

	errno = 0;
	n = strtoull(arg, &end, 10);
	if (errno != 0 || arg[0] == '\0' || end == NULL || *end != '\0' ||
			n < min || n > max)
		return -1;

	*value = (uint64_t)n;
	return 0;
}

int parse_timer(const char *arg, uint64_t *timer_secs)
{
	return parse_u64(arg, 0, MAX_TIMER_PERIOD, timer_secs);
}

int parse_mac(const char *arg, struct rte_ether_addr *addr)
{
	if (strcmp(arg, "broadcast") == 0) {
		for (unsigned int i = 0; i < RTE_ETHER_ADDR_LEN; i++)
			addr->addr_bytes[i] = 0xff;
		return 0;
	}

	for (unsigned int i = 0; i < RTE_ETHER_ADDR_LEN; i++) {
		char *endptr;
		unsigned long value = strtoul(arg, &endptr, 16);
		if (arg == endptr || value > UINT8_MAX)
			return -1;
		addr->addr_bytes[i] = (uint8_t)value;
		if (i + 1 < RTE_ETHER_ADDR_LEN) {
			if (*endptr != ':')
				return -1;
			arg = endptr + 1;
		} else {
			if (*endptr != '\0')
				return -1;
		}
	}

	return 0;
}