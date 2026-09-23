#include "common.h"
#include "server_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
typedef DWORD WINAPI thread_return_t;
typedef LPVOID thread_arg_t;
typedef CRITICAL_SECTION WorkerMutex;
#define start_thread(handle, entry, arg) ((*(handle) = CreateThread(NULL, 0, entry, arg, 0, NULL)) != NULL)
#define worker_mutex_init(m) InitializeCriticalSection(m)
#define worker_mutex_lock(m) EnterCriticalSection(m)
#define worker_mutex_unlock(m) LeaveCriticalSection(m)
#else
#include <pthread.h>
#include <sys/time.h>
typedef void *thread_return_t;
typedef void *thread_arg_t;
typedef pthread_mutex_t WorkerMutex;
#define start_thread(handle, entry, arg) (pthread_create(handle, NULL, entry, arg) == 0)
#define worker_mutex_init(m) pthread_mutex_init(m, NULL)
#define worker_mutex_lock(m) pthread_mutex_lock(m)
#define worker_mutex_unlock(m) pthread_mutex_unlock(m)
#endif

#define MAX_WORKERS 64
#define CLIENT_TIMEOUT_SECONDS 20

static WorkerMutex worker_lock;
static unsigned active_workers;

typedef struct {
    socket_t socket_fd;
} ClientWorker;

static int reserve_worker(void) {
    int reserved = 0;
    worker_mutex_lock(&worker_lock);
    if (active_workers < MAX_WORKERS) {
        ++active_workers;
        reserved = 1;
    }
    worker_mutex_unlock(&worker_lock);
    return reserved;
}

static void release_worker(void) {
    worker_mutex_lock(&worker_lock);
    --active_workers;
    worker_mutex_unlock(&worker_lock);
}

static void configure_client_timeout(socket_t socket_fd) {
#ifdef _WIN32
    DWORD timeout_ms = CLIENT_TIMEOUT_SECONDS * 1000;
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO,
                     (const char *)&timeout_ms, (int)sizeof(timeout_ms));
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO,
                     (const char *)&timeout_ms, (int)sizeof(timeout_ms));
#else
    struct timeval timeout = {CLIENT_TIMEOUT_SECONDS, 0};
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO,
                     &timeout, (socklen_t)sizeof(timeout));
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO,
                     &timeout, (socklen_t)sizeof(timeout));
#endif
}

static socket_t create_listener(const char *host, const char *port) {
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    socket_t listener = INVALID_SOCKET;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = host ? 0 : AI_PASSIVE;
    if (getaddrinfo(host, port, &hints, &addresses) != 0) return INVALID_SOCKET;

    for (address = addresses; address; address = address->ai_next) {
        int reuse_address = 1;
        listener = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (listener == INVALID_SOCKET) continue;
        (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                         (const char *)&reuse_address, (int)sizeof(reuse_address));
        if (bind(listener, address->ai_addr, (socket_length_t)address->ai_addrlen) == 0 &&
            listen(listener, 32) == 0) break;
        close_socket(listener);
        listener = INVALID_SOCKET;
    }
    freeaddrinfo(addresses);
    return listener;
}

static void send_response(socket_t socket_fd, const char *request) {
    char response[SERVER_RESPONSE_SIZE];
    char line[SERVER_RESPONSE_SIZE + 2];
    server_state_process(request, response, sizeof(response));
    snprintf(line, sizeof(line), "%s\n", response);
    if (send_all(socket_fd, line, strlen(line)) != 0) print_socket_error("send");
}

static thread_return_t client_worker(thread_arg_t raw) {
    ClientWorker *worker = (ClientWorker *)raw;
    socket_t socket_fd = worker->socket_fd;
    char request[1024];
    free(worker);
    if (receive_line(socket_fd, request, sizeof(request)) == 0) send_response(socket_fd, request);
    close_socket(socket_fd);
    release_worker();
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : NULL;
    const char *port = argc > 2 ? argv[2] : "5000";
    socket_t listener;

    if (argc > 3) {
        fprintf(stderr, "Uso: %s [indirizzo-bind] [porta]\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (network_startup() != 0) {
        print_socket_error("network_startup");
        return EXIT_FAILURE;
    }
    if (server_state_init() != 0) {
        fprintf(stderr, "Could not initialize server state\n");
        network_cleanup();
        return EXIT_FAILURE;
    }
    worker_mutex_init(&worker_lock);
    listener = create_listener(host, port);
    if (listener == INVALID_SOCKET) {
        print_socket_error("listen");
        network_cleanup();
        return EXIT_FAILURE;
    }

    printf("Battleship server listening on %s:%s\n", host ? host : "0.0.0.0", port);
    fflush(stdout);
    for (;;) {
        socket_t client_socket = accept(listener, NULL, NULL);
        ClientWorker *worker;
        if (client_socket == INVALID_SOCKET) {
            print_socket_error("accept");
            continue;
        }
        configure_client_timeout(client_socket);
        if (!reserve_worker()) {
            close_socket(client_socket);
            continue;
        }
        worker = (ClientWorker *)malloc(sizeof(*worker));
        if (!worker) {
            close_socket(client_socket);
            release_worker();
            continue;
        }
        worker->socket_fd = client_socket;
#ifdef _WIN32
        {
            HANDLE thread;
            if (start_thread(&thread, client_worker, worker)) CloseHandle(thread);
            else { free(worker); close_socket(client_socket); release_worker(); }
        }
#else
        {
            pthread_t thread;
            if (start_thread(&thread, client_worker, worker)) pthread_detach(thread);
            else { free(worker); close_socket(client_socket); release_worker(); }
        }
#endif
    }
}
