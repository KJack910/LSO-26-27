#ifndef BATTLESHIP_CLIENT_NET_H
#define BATTLESHIP_CLIENT_NET_H

#include <stddef.h>

int client_request(
    const char *host,
    const char *port,
    const char *request,
    char *response,
    size_t response_size
);

#endif
