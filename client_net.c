#include "client_net.h"

#include "common.h"

#include <stdio.h>
#include <string.h>

#define CLIENT_MESSAGE_CAPACITY 1024

static socket_t connect_to_server(const char *host, const char *port) {
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    socket_t connection = INVALID_SOCKET;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &addresses) != 0) {
        return INVALID_SOCKET;
    }

    for (address = addresses; address != NULL; address = address->ai_next) {
        connection = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (connection == INVALID_SOCKET) {
            continue;
        }

        if (connect(connection, address->ai_addr, (socket_length_t)address->ai_addrlen) == 0) {
            break;
        }

        close_socket(connection);
        connection = INVALID_SOCKET;
    }

    freeaddrinfo(addresses);
    return connection;
}

int client_request(
    const char *host,
    const char *port,
    const char *request,
    char *response,
    size_t response_size
) {
    char framed_request[CLIENT_MESSAGE_CAPACITY];
    socket_t connection;
    int request_length;
    int result;

    if (host == NULL || port == NULL || request == NULL || response == NULL || response_size < 2) {
        return -1;
    }

    request_length = snprintf(framed_request, sizeof(framed_request), "%s\n", request);
    if (request_length < 0 || (size_t)request_length >= sizeof(framed_request)) {
        return -1;
    }

    connection = connect_to_server(host, port);
    if (connection == INVALID_SOCKET) {
        return -1;
    }

    result = send_all(connection, framed_request, (size_t)request_length);
    if (result == 0) {
        result = receive_line(connection, response, response_size);
    }

    close_socket(connection);
    return result;
}
