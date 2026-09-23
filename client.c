#include "client_ui.h"

#include "common.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    const char *port = argc > 2 ? argv[2] : "5000";

    if (argc > 3) {
        fprintf(stderr, "Uso: %s [host] [porta]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (network_startup() != 0) {
        print_socket_error("network_startup");
        return EXIT_FAILURE;
    }

    int result = client_run(host, port);
    network_cleanup();
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
