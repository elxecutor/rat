# Plan 006: Extract duplicated helper functions into a shared utility file

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report — do not improvise. When done, update the status row for this plan
> in `plans/README.md` unless a reviewer dispatched you and told you they
> maintain the index.
>
> **Drift check (run first)**: `git diff --stat fd5c459..HEAD -- src/client.c src/server.c include/`
> If any in-scope files changed since this plan was written, compare the
> "Current state" excerpts against the live code before proceeding; on a
> mismatch, treat it as a STOP condition.

## Status

- **Priority**: P2
- **Effort**: S
- **Risk**: LOW
- **Depends on**: none
- **Category**: tech-debt
- **Planned at**: commit `fd5c459`, 2026-07-03

## Why this matters

Three helper functions — `send_all`, `recv_all`, and `find_base_name` — are identically implemented as `static` functions in both `src/client.c` and `src/server.c`. Any bug fix or enhancement must be applied twice. In practice, the copies will diverge: one gets fixed, the other doesn't, and the inconsistency surfaces as a hard-to-diagnose runtime bug.

Moving them to a shared file also prepares for future expansion: the crypto module's `crypto_send_psk_challenge` (from plan 005) needs access to `send_all`/`recv_all`, which it currently can't use because they're `static` in each file.

## Current state

**`src/client.c`**, lines ~37–68:

```c
// Helper: send all bytes (retries partial sends)
static int send_all(socket_t fd, const void *data, size_t len, int flags) {
    const unsigned char *p = (const unsigned char *)data;
    while (len > 0) {
        int sent = send(fd, p, len, flags);
        if (sent <= 0) return -1;
        p += sent;
        len -= sent;
    }
    return 0;
}

// Helper: recv all bytes (retries partial receives)
static int recv_all(socket_t fd, void *data, size_t len, int flags) {
    unsigned char *p = (unsigned char *)data;
    while (len > 0) {
        int received = recv(fd, p, len, flags);
        if (received <= 0) return -1;
        p += received;
        len -= received;
    }
    return 0;
}

// Helper: extract base filename from path (handles both / and \ regardless of OS)
static const char *find_base_name(const char *path) {
    const char *p1 = strrchr(path, '/');
    const char *p2 = strrchr(path, '\\');
    const char *base = (p1 > p2) ? p1 : p2;
    return base ? base + 1 : path;
}
```

**`src/server.c`**, lines ~34–63: identical 29-line block.

Both files use `socket_t` (from `include/crypto.h`) and `send`/`recv` (from platform headers). No other dependencies.

## Commands you will need

| Purpose   | Command                  | Expected on success |
|-----------|--------------------------|---------------------|
| Build     | `make clean && make`     | exit 0, no errors  |

## Scope

**In scope**:
- `include/util.h` (create) — declarations for the three shared helpers
- `src/util.c` (create) — implementations of the three shared helpers
- `src/client.c` — remove the `static` helper definitions, add `#include "../include/util.h"`
- `src/server.c` — remove the `static` helper definitions, add `#include "../include/util.h"`

**Out of scope**:
- `src/crypto.c` — no changes (it already includes `crypto.h`; any future crypto function that needs `send_all` should include `util.h`)
- `modules/` — not touched
- Any renaming of the functions or changing their signatures — pure extraction, no behavioral changes

## Git workflow

- Branch: `advisor/006-shared-helpers`
- Commit message: `refactor: extract send_all, recv_all, find_base_name into shared util.h/util.c`
- Do NOT push or open a PR unless instructed.

## Steps

### Step 1: Create `include/util.h`

```c
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
```

**Verify**: `make clean && make` — this will fail at first because the .c file doesn't exist yet and the callers haven't been updated. That's expected. Continue to step 2.

### Step 2: Create `src/util.c`

Move the `send_all`, `recv_all`, and `find_base_name` implementations verbatim from `src/client.c` (lines ~37–68) into a new file `src/util.c`.

Add these includes at the top:

```c
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../include/util.h"
#include <errno.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/socket.h>
#endif
```

(The `send`/`recv` functions are available via `crypto.h` → `winsock2.h` on Windows and via `<sys/socket.h>` on Linux.)

Remove the `static` keyword from all three functions (they're now exported symbols).

**Verify**: `make clean && make` — this will fail because `client.c` and `server.c` still define `static` versions of these functions (duplicate symbols). Move to step 3.

### Step 3: Update `src/client.c`

1. Remove the three helper function blocks (lines ~37–68, the `send_all`, `recv_all`, and `find_base_name` definitions).
2. Add `#include "../include/util.h"` after the existing `#include "../include/crypto.h"` line.
3. Update `Makefile` to compile `src/util.c`. In the `CLIENT_SRC` and `SERVER_SRC` variables (lines 52–53):

```makefile
# Source files
UTIL_SRC = $(SRC_DIR)/util.c
CLIENT_SRC = $(SRC_DIR)/client.c $(SRC_DIR)/persistence.c $(SRC_DIR)/crypto.c $(UTIL_SRC)
SERVER_SRC = $(SRC_DIR)/server.c $(SRC_DIR)/crypto.c $(UTIL_SRC)
```

**Verify**: `make clean && make` exits 0.

### Step 4: Update `src/server.c`

1. Remove the three helper function blocks (lines ~34–63).
2. Add `#include "../include/util.h"` after `#include "../include/crypto.h"`.

**Verify**: `make clean && make` exits 0.

### Step 5: Final verification

1. Confirm no stale static helper definitions remain:
   - `grep -n 'static int send_all\|static int recv_all\|static const char \*find_base_name' src/client.c` — no matches
   - `grep -n 'static int send_all\|static int recv_all\|static const char \*find_base_name' src/server.c` — no matches
2. Confirm the shared versions exist:
   - `grep -n '^int send_all\|^int recv_all\|^const char \*find_base_name' src/util.c` — 3 matches (non-static)
3. Confirm both callers include the header:
   - `grep '#include.*util\.h' src/client.c src/server.c` — 2 matches

**Verify**: `make clean && make` exits 0.

## Test plan

Build, then run a quick functional check:

1. `make clean && make`
2. Start server: `./bin/server &`
3. Start client: `./bin/client &`
4. Run `ls`, `pwd`, `cd /tmp`, `pwd` — the `find_base_name` function is used in prompt generation, so verify the prompt updates correctly
5. Upload a file: `upload Makefile /tmp/` (the `find_base_name` function is also used in upload info parsing)
6. Verify `./bin/server` and `./bin/client` are both around 50KB smaller (shared code, no longer duplicated per binary) — unlikely to be noticeable, but check `ls -lh bin/`

## Done criteria

ALL must hold:

- [ ] `include/util.h` exists and declares the 3 functions
- [ ] `src/util.c` exists and implements the 3 functions (non-static)
- [ ] `src/client.c` no longer defines `send_all`, `recv_all`, or `find_base_name`
- [ ] `src/server.c` no longer defines `send_all`, `recv_all`, or `find_base_name`
- [ ] Both `src/client.c` and `src/server.c` include `"../include/util.h"`
- [ ] `Makefile`'s `CLIENT_SRC` and `SERVER_SRC` include `$(UTIL_SRC)`
- [ ] `make clean && make` exits 0
- [ ] Functional test (start server + client, run commands) passes
- [ ] `plans/README.md` status row updated

## STOP conditions

Stop and report back if:

- The `socket_t` type from `crypto.h` is not visible when compiling `util.c` — verify the include order: `util.h` includes `crypto.h`, `util.c` includes `util.h` first, so `socket_t` is available when the function bodies use it
- The `send`/`recv` symbols are not available on either platform — on Linux they come from `<sys/socket.h>` (guarded with `#ifndef _WIN32` in `util.c`), on Windows from `<winsock2.h>` (via `crypto.h`)
- `make` produces "duplicate symbol" errors — that means a `static` helper wasn't fully removed from one of the source files

## Maintenance notes

- The `send_all` and `recv_all` functions are now the single source of truth for partial-send/recv handling. Any future code that does raw socket I/O (e.g., new authentication steps, protocol extensions) should use these.
- `find_base_name` is the single source of truth for cross-platform path base-name extraction. If a future contributor adds support for a path that uses `:` as a separator (e.g., macOS HFS+ paths), update this function in one place.
- The `util.c` and `util.h` files are now the natural place for other small cross-cutting utilities. Keep them focused — if they grow beyond ~200 lines, split by concern.
