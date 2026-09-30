#include "client_net.h"

#include "common.h"
#include "server_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
typedef DWORD presence_thread_return_t;
#else
#include <pthread.h>
#include <unistd.h>
typedef void *presence_thread_return_t;
#endif

#define CLIENT_MESSAGE_CAPACITY 1024
#define PRESENCE_TEXT_CAPACITY 128

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

struct ClientPresence {
    char host[PRESENCE_TEXT_CAPACITY];
    char port[PRESENCE_TEXT_CAPACITY];
    char pid[PRESENCE_TEXT_CAPACITY];
    char sid[PRESENCE_TEXT_CAPACITY];
    char token[PRESENCE_TEXT_CAPACITY];
    volatile int running;
#ifdef _WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
};

static presence_thread_return_t presence_worker(void *raw) {
    ClientPresence *presence = (ClientPresence *)raw;
    char request[CLIENT_MESSAGE_CAPACITY];
    char response[CLIENT_MESSAGE_CAPACITY];
    int waited;
    int was_paused = 0;

    while (presence->running) {
        snprintf(request, sizeof(request), "HEARTBEAT|%s|%s|%s", presence->pid, presence->sid, presence->token);
        if (client_request(presence->host, presence->port, request, response, sizeof(response)) == 0) {
            if (strncmp(response, "OK|PAUSED", 9) == 0) {
                if (!was_paused) {
                    printf("\n\n[!] ATTENZIONE: L'avversario si e' disconnesso! (Premi INVIO per aggiornare)\n> ");
                    fflush(stdout);
                    was_paused = 1;
                }
            } else if (strncmp(response, "ERR|session unavailable", 23) == 0) {
                printf("\n\n[!] ATTENZIONE: Partita annullata/vinta per abbandono! (Premi INVIO per aggiornare)\n> ");
                fflush(stdout);
                break;
            } else {
                was_paused = 0;
            }
        }
        for (waited = 0; waited < SERVER_HEARTBEAT_INTERVAL_SECONDS && presence->running; ++waited) {
#ifdef _WIN32
            Sleep(1000);
#else
            sleep(1);
#endif
        }
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

int client_presence_start(const char *host, const char *port,
                          const char *pid, const char *sid, const char *token,
                          ClientPresence **out_presence) {
    ClientPresence *presence;
    if (!host || !port || !pid || !sid || !token || !out_presence) return -1;
    presence = (ClientPresence *)calloc(1, sizeof(*presence));
    if (!presence) return -1;
    snprintf(presence->host, sizeof(presence->host), "%s", host);
    snprintf(presence->port, sizeof(presence->port), "%s", port);
    snprintf(presence->pid, sizeof(presence->pid), "%s", pid);
    snprintf(presence->sid, sizeof(presence->sid), "%s", sid);
    snprintf(presence->token, sizeof(presence->token), "%s", token);
    presence->running = 1;
#ifdef _WIN32
    presence->thread = CreateThread(NULL, 0, presence_worker, presence, 0, NULL);
    if (!presence->thread) {
        free(presence);
        return -1;
    }
#else
    if (pthread_create(&presence->thread, NULL, presence_worker, presence) != 0) {
        free(presence);
        return -1;
    }
#endif
    *out_presence = presence;
    return 0;
}

void client_presence_stop(ClientPresence *presence) {
    if (!presence) return;
    presence->running = 0;
#ifdef _WIN32
    WaitForSingleObject(presence->thread, INFINITE);
    CloseHandle(presence->thread);
#else
    pthread_join(presence->thread, NULL);
#endif
    free(presence);
}
