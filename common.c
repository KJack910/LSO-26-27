#include "common.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
int network_startup(void) {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : -1;
}

void network_cleanup(void) {
    WSACleanup();
}

void close_socket(socket_t socket_fd) {
    closesocket(socket_fd);
}

void print_socket_error(const char *operation) {
    fprintf(stderr, "%s failed with socket error %d\n", operation, WSAGetLastError());
}
#else
int network_startup(void) {
    return 0;
}

void network_cleanup(void) {
}

void close_socket(socket_t socket_fd) {
    close(socket_fd);
}

void print_socket_error(const char *operation) {
    perror(operation);
}
#endif

int send_all(socket_t socket_fd, const char *data, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        size_t remaining = length - sent;
        int chunk = remaining > 2147483647u ? 2147483647 : (int)remaining;
        int result = send(socket_fd, data + sent, chunk, 0);
        if (result == SOCKET_ERROR || result == 0) {
            return -1;
        }
        sent += (size_t)result;
    }
    return 0;
}

int receive_line(socket_t socket_fd, char *buffer, size_t buffer_size) {
    size_t used = 0;

    if (buffer_size < 2) {
        return -1;
    }

    while (used < buffer_size - 1) {
        int received = recv(socket_fd, buffer + used, (int)(buffer_size - 1 - used), 0);
        if (received == SOCKET_ERROR || received == 0) {
            return used == 0 ? -1 : 0;
        }

        used += (size_t)received;
        buffer[used] = '\0';
        char *newline = strchr(buffer, '\n');
        if (newline != NULL) {
            *newline = '\0';
            return 0;
        }
    }

    buffer[buffer_size - 1] = '\0';
    return 0;
}
