#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include "crypto.h"  // for socket_t

// Send all bytes (retries partial sends)
int send_all(socket_t fd, const void *data, size_t len, int flags);

// Receive all bytes (retries partial receives)
int recv_all(socket_t fd, void *data, size_t len, int flags);

// Extract base filename from path (handles both / and \ regardless of OS)
const char *find_base_name(const char *path);

#endif // UTIL_H
