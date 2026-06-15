#ifndef ECHO_CLIENT_H
#define ECHO_CLIENT_H

#define MAX_PAYLOAD 1400
#define CLIENT_INTERNAL_SRC_ADDR RTE_IPV4(198, 18, 0, 1)
#define CLIENT_INTERNAL_DST_ADDR RTE_IPV4(198, 18, 0, 2)
#define CLIENT_INTERNAL_SRC_L4 49152
#define CLIENT_INTERNAL_DST_L4 49153
#define CLIENT_FINISH_WAIT_SECS 2

#include <stdint.h>

#include "common.h"

struct client_conf {
    struct rte_ether_addr dst_mac;
    char payload[MAX_PAYLOAD + 1];

    uint16_t payload_len;
    uint16_t burst_size;
    uint64_t duration_secs;
};

#define DEFAULT_CLIENT_PAYLOAD_LEN 64

/* Returns:
 *  >0 : user requested help (program should exit successfully)
 *  <0 : invalid args
 *   0 : ok
 */
int parse_client_args(int argc, char **argv, struct app *app,
                struct client_conf *conf);

uint16_t client_packet_len(const struct client_conf *conf);

int client_app_init(int argc, char **argv);
void client_app_run(void);

#endif /* ECHO_CLIENT_H */
