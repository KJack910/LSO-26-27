#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static socket_t connect_to_server(const char *host, const char *port) {
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    socket_t client_socket = INVALID_SOCKET;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &addresses) != 0) {
        print_socket_error("getaddrinfo");
        return INVALID_SOCKET;
    }

    for (address = addresses; address != NULL; address = address->ai_next) {
        client_socket = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (client_socket == INVALID_SOCKET) {
            continue;
        }
        if (connect(client_socket, address->ai_addr, (socket_length_t)address->ai_addrlen) == 0) {
            break;
        }
        close_socket(client_socket);
        client_socket = INVALID_SOCKET;
    }

    freeaddrinfo(addresses);
    return client_socket;
}

static void print_usage(const char *program) {
    fprintf(stderr, "Uso: %s [host] [porta] [messaggio]\n", program);
}

int main(int argc, char **argv) {
    const char *host = argc >= 2 ? argv[1] : "127.0.0.1";
    const char *port = argc >= 3 ? argv[2] : "5000";
    const char *message = argc >= 4 ? argv[3] : "Hello from client!";

    if (argc > 4) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (network_startup() != 0) {
        print_socket_error("network_startup");
        return EXIT_FAILURE;
    }

    socket_t client_socket = connect_to_server(host, port);
    if (client_socket == INVALID_SOCKET) {
        print_socket_error("connect");
        network_cleanup();
        return EXIT_FAILURE;
    }

    char message_with_newline[1024];
    int message_length = snprintf(message_with_newline, sizeof(message_with_newline), "%s\n", message);
    if (message_length < 0 || (size_t)message_length >= sizeof(message_with_newline)) {
        fprintf(stderr, "Messaggio troppo lungo.\n");
        close_socket(client_socket);
        network_cleanup();
        return EXIT_FAILURE;
    }

    if (send_all(client_socket, message_with_newline, (size_t)message_length) != 0) {
        print_socket_error("send");
        close_socket(client_socket);
        network_cleanup();
        return EXIT_FAILURE;
    }

    char response[1024];
    if (receive_line(client_socket, response, sizeof(response)) != 0) {
        print_socket_error("recv");
        close_socket(client_socket);
        network_cleanup();
        return EXIT_FAILURE;
    }
    printf("Server: %s\n", response);

    close_socket(client_socket);
    network_cleanup();
    return EXIT_SUCCESS;
}
