#ifndef SOCKET_COMMON_H
#define SOCKET_COMMON_H

#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#endif

#include <stddef.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
typedef int socket_length_t;
#else
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
typedef socklen_t socket_length_t;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

int network_startup(void);
void network_cleanup(void);
void close_socket(socket_t socket_fd);
void print_socket_error(const char *operation);
int send_all(socket_t socket_fd, const char *data, size_t length);
int receive_line(socket_t socket_fd, char *buffer, size_t buffer_size);

#endif
