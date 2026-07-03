#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../include/util.h"
#include <errno.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/socket.h>
#endif

// Send all bytes (retries partial sends)
int send_all(socket_t fd, const void *data, size_t len, int flags) {
    const unsigned char *p = (const unsigned char *)data;
    while (len > 0) {
        int sent = send(fd, p, len, flags);
        if (sent <= 0) return -1;
        p += sent;
        len -= sent;
    }
    return 0;
}

// Receive all bytes (retries partial receives)
int recv_all(socket_t fd, void *data, size_t len, int flags) {
    unsigned char *p = (unsigned char *)data;
    while (len > 0) {
        int received = recv(fd, p, len, flags);
        if (received <= 0) return -1;
        p += received;
        len -= received;
    }
    return 0;
}

// Extract base filename from path (handles both / and \ regardless of OS)
const char *find_base_name(const char *path) {
    const char *p1 = strrchr(path, '/');
    const char *p2 = strrchr(path, '\\');
    const char *base = (p1 > p2) ? p1 : p2;
    return base ? base + 1 : path;
}
