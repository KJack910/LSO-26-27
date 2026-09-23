#ifndef BATTLESHIP_SERVER_STATE_H
#define BATTLESHIP_SERVER_STATE_H

#include <stddef.h>

#define SERVER_RESPONSE_SIZE 2048

/* Initialize volatile player identities and synchronized game state. */
int server_state_init(void);
/* Process one protocol line and write its single-line response. */
void server_state_process(const char *request, char *response, size_t response_size);

#endif
