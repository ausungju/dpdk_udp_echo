#include <stdio.h>
#include <stdlib.h>

#include "client.h"
#include "common.h"

int main(int argc, char **argv)
{
    int ret;

    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");
    }
    argc -= ret;
    argv += ret;

    force_quit = false;
    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        app_exit(NULL, "Failed to install SIGINT handler\n");
    }
    if (signal(SIGTERM, signal_handler) == SIG_ERR) {
        app_exit(NULL, "Failed to install SIGTERM handler\n");
    }

    ret = client_app_init(argc, argv);
    if (ret > 0) {
        rte_eal_cleanup();
        return 0;
    }
    if (ret < 0) {
        app_exit(NULL, "Invalid application arguments\n");
    }

    client_app_run();

    rte_eal_cleanup();
    printf("bye\n");
    return 0;
}
