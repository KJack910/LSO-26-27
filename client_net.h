#ifndef BATTLESHIP_CLIENT_NET_H
#define BATTLESHIP_CLIENT_NET_H

#include <stddef.h>

typedef struct ClientPresence ClientPresence;

int client_request(
    const char *host,
    const char *port,
    const char *request,
    char *response,
    size_t response_size
);

int client_presence_start(const char *host, const char *port,
                          const char *pid, const char *sid, const char *token,
                          ClientPresence **presence);
void client_presence_stop(ClientPresence *presence);

#endif
