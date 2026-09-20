#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static socket_t create_listening_socket(const char *host, const char *port) {
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    socket_t listening_socket = INVALID_SOCKET;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = (host == NULL) ? AI_PASSIVE : 0;

    if (getaddrinfo(host, port, &hints, &addresses) != 0) {
        print_socket_error("getaddrinfo");
        return INVALID_SOCKET;
    }

    for (address = addresses; address != NULL; address = address->ai_next) {
        int reuse_address = 1;
        listening_socket = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (listening_socket == INVALID_SOCKET) {
            continue;
        }

        (void)setsockopt(
            listening_socket,
            SOL_SOCKET,
            SO_REUSEADDR,
            (const char *)&reuse_address,
            (int)sizeof(reuse_address)
        );

        if (bind(listening_socket, address->ai_addr, (socket_length_t)address->ai_addrlen) == 0 &&
            listen(listening_socket, 8) == 0) {
            break;
        }

        close_socket(listening_socket);
        listening_socket = INVALID_SOCKET;
    }

    freeaddrinfo(addresses);
    return listening_socket;
}

static void print_usage(const char *program) {
    fprintf(stderr, "Uso: %s [host] [porta] [--once]\n", program);
}

int main(int argc, char **argv) {
    const char *host = NULL;
    const char *port = "5000";
    int once = 0;

    if (argc > 4) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (argc >= 2 && strcmp(argv[1], "--once") != 0) {
        host = argv[1];
    }
    if (argc >= 3 && strcmp(argv[2], "--once") != 0) {
        port = argv[2];
    }
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--once") == 0) {
            once = 1;
        }
    }

    if (network_startup() != 0) {
        print_socket_error("network_startup");
        return EXIT_FAILURE;
    }

    socket_t listening_socket = create_listening_socket(host, port);
    if (listening_socket == INVALID_SOCKET) {
        print_socket_error("server socket");
        network_cleanup();
        return EXIT_FAILURE;
    }

    printf("Server in ascolto su %s:%s\n", host == NULL ? "0.0.0.0" : host, port);
    fflush(stdout);

    do {
        struct sockaddr_storage client_address;
        socket_length_t client_address_length = (socket_length_t)sizeof(client_address);
        socket_t client_socket = accept(
            listening_socket,
            (struct sockaddr *)&client_address,
            &client_address_length
        );
        if (client_socket == INVALID_SOCKET) {
            print_socket_error("accept");
            close_socket(listening_socket);
            network_cleanup();
            return EXIT_FAILURE;
        }

        char message[1024];
        if (receive_line(client_socket, message, sizeof(message)) == 0) {
            printf("Client: %s\n", message);
            const char response[] = "Hello from server!\n";
            if (send_all(client_socket, response, strlen(response)) != 0) {
                print_socket_error("send");
            }
        } else {
            print_socket_error("recv");
        }

        close_socket(client_socket);
    } while (!once);

    close_socket(listening_socket);
    network_cleanup();
    return EXIT_SUCCESS;
}
