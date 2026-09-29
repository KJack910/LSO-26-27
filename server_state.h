#ifndef BATTLESHIP_SERVER_STATE_H
#define BATTLESHIP_SERVER_STATE_H

#include <stddef.h>
#include <time.h>

#define SERVER_RESPONSE_SIZE 2048
#define SERVER_TOKEN_SIZE 33
#define SERVER_HEARTBEAT_INTERVAL_SECONDS 5
#define SERVER_HEARTBEAT_STALE_SECONDS (SERVER_HEARTBEAT_INTERVAL_SECONDS * 2)
#define SERVER_DISCONNECT_TIMEOUT_SECONDS 60

/* Initialize volatile player identities and synchronized game state. */
int server_state_init(void);
/* Process one protocol line and write its single-line response. */
void server_state_process(const char *request, char *response, size_t response_size);
/* Mark disconnected players and resolve expired reconnection windows. */
void server_state_maintenance(void);
/* Deterministic clock entry point used by tests. */
void server_state_maintenance_at(time_t now);

#endif
