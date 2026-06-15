#ifndef ECHO_SERVER_H
#define ECHO_SERVER_H

#include <stdint.h>

#include "common.h"

struct server_conf {
    uint16_t burst_size;
};

int parse_server_args(int argc, char **argv, struct app *app,
                struct server_conf *conf);

int server_app_init(int argc, char **argv);
void server_app_run(void);

#endif /* ECHO_SERVER_H */
