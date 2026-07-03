#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../include/util.h"
#include <errno.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
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

// Perform SOCKS5 handshake over an already-connected socket to reach a .onion host
int socks5_connect(socket_t proxy_fd, const char *host, uint16_t port) {
    size_t hostlen = strlen(host);
    if (hostlen > 255) return -1;

    // 1. SOCKS5 greeting: VER=5, NMETHODS=1, METHODS=[0x00 (no auth)]
    unsigned char greet[] = {5, 1, 0};
    if (send_all(proxy_fd, greet, 3, 0) != 0) return -1;
    unsigned char greet_resp[2];
    if (recv_all(proxy_fd, greet_resp, 2, 0) != 0) return -1;
    if (greet_resp[0] != 5 || greet_resp[1] != 0) return -1;

    // 2. CONNECT request: VER=5, CMD=1 (CONNECT), RSV=0, ATYP=3 (DOMAINNAME)
    unsigned char req[5 + 256 + 2];
    size_t pos = 0;
    req[pos++] = 5;
    req[pos++] = 1;
    req[pos++] = 0;
    req[pos++] = 3;
    req[pos++] = hostlen;
    memcpy(&req[pos], host, hostlen);
    pos += hostlen;
    uint16_t port_net = htons(port);
    memcpy(&req[pos], &port_net, 2);
    pos += 2;

    if (send_all(proxy_fd, req, pos, 0) != 0) return -1;

    // 3. Read response: VER, REP, RSV, ATYP
    unsigned char resp[4];
    if (recv_all(proxy_fd, resp, 4, 0) != 0) return -1;
    if (resp[0] != 5 || resp[1] != 0) return -1;

    // Skip BND.ADDR and BND.PORT
    if (resp[3] == 1) {
        recv_all(proxy_fd, resp, 6, 0);       // IPv4 + port
    } else if (resp[3] == 3) {
        recv_all(proxy_fd, resp, 1, 0);       // length byte
        recv_all(proxy_fd, resp, resp[0] + 2, 0);  // addr + port
    } else if (resp[3] == 4) {
        recv_all(proxy_fd, resp, 18, 0);      // IPv6 + port
    }

    return 0;
}
