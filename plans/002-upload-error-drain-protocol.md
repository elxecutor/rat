# Plan 002: Drain incoming file data on upload open failure

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report — do not improvise. When done, update the status row for this plan
> in `plans/README.md` unless a reviewer dispatched you and told you they
> maintain the index.
>
> **Drift check (run first)**: `git diff --stat fd5c459..HEAD -- src/client.c`
> If the in-scope file changed since this plan was written, compare the
> "Current state" excerpts against the live code before proceeding; on a
> mismatch, treat it as a STOP condition.

## Status

- **Priority**: P1
- **Effort**: S
- **Risk**: LOW
- **Depends on**: 001 (same function, same area — merge conflicts if done in parallel; do this after 001 or combine them)
- **Category**: bug
- **Planned at**: commit `fd5c459`, 2026-07-03

## Why this matters

In `handle_upload` on the client, the file size is received BEFORE `fopen()` is called. If `fopen()` fails (disk full, permissions, path not found), the function returns an error but leaves the incoming file data unconsumed on the encrypted socket. The server has already started sending file data after the ACK and expects the client to read it. Every subsequent `crypto_recv` call (for the next command response) will pick up part of the orphaned file data instead — silently desynchronizing the entire command protocol.

The fix: when `fopen()` fails after receiving the file size, drain all `file_remaining` bytes from the socket before returning.

## Current state

`src/client.c` — function `handle_upload`, lines ~704–719:

```c
    // Receive file size (two uint32_t in network order)
    uint32_t fsz_hi_net = 0, fsz_lo_net = 0;
    crypto_recv(client->client_fd, &client->crypto_ctx, &fsz_hi_net, sizeof(fsz_hi_net), 0);
    crypto_recv(client->client_fd, &client->crypto_ctx, &fsz_lo_net, sizeof(fsz_lo_net), 0);
    uint64_t file_remaining = ((uint64_t)ntohl(fsz_hi_net) << 32) | ntohl(fsz_lo_net);
    
    file = fopen(final_path, "wb");
    if (file == NULL) {
        char error_msg[512];
        char display_path[200];
        strncpy(display_path, final_path, sizeof(display_path) - 1);
        display_path[sizeof(display_path) - 1] = '\0';
        snprintf(error_msg, sizeof(error_msg), "Error: Cannot create file %s - %s", display_path, strerror(errno));
        send_response_with_prompt(client, error_msg);
        return;
    }
```

The `return` on the error path exits without draining `file_remaining` bytes.

Downstream, lines ~720–740 handle the happy path with the drain loop:

```c
    // Receive file content (exactly file_remaining bytes)
    while (file_remaining > 0) {
        int to_read = (file_remaining > BUFFER_SIZE) ? BUFFER_SIZE : (int)file_remaining;
        bytes_received = crypto_recv(client->client_fd, &client->crypto_ctx, buffer, to_read, 0);
        ...
```

## Commands you will need

| Purpose   | Command                  | Expected on success |
|-----------|--------------------------|---------------------|
| Build     | `make clean && make`     | exit 0, no errors  |

## Scope

**In scope**:
- `src/client.c` — the error path of `handle_upload` only

**Out of scope**:
- `src/server.c` — the server side has a symmetric issue in `handle_download` when `fopen()` fails. Check it but do not fix it in this plan (note the finding).
- Any other function

## Git workflow

- Branch: `advisor/002-upload-error-drain`
- Commit message: `fix: drain incoming upload data on open failure to prevent protocol desync`
- Do NOT push or open a PR unless instructed.

## Steps

### Step 1: Add a drain helper or inline the drain loop

In `src/client.c`, inside `handle_upload`, replace the `fopen` failure block with a version that drains remaining data before returning:

```c
    file = fopen(final_path, "wb");
    if (file == NULL) {
        char error_msg[512];
        char display_path[200];
        strncpy(display_path, final_path, sizeof(display_path) - 1);
        display_path[sizeof(display_path) - 1] = '\0';
        snprintf(error_msg, sizeof(error_msg), "Error: Cannot create file %s - %s", display_path, strerror(errno));
        send_response_with_prompt(client, error_msg);
        
        // Drain remaining file data to keep protocol in sync
        {
            char drain_buf[BUFFER_SIZE];
            uint64_t remain = file_remaining;
            while (remain > 0) {
                int to_read = (remain > BUFFER_SIZE) ? BUFFER_SIZE : (int)remain;
                int n = crypto_recv(client->client_fd, &client->crypto_ctx, drain_buf, to_read, 0);
                if (n <= 0) break;
                remain -= n;
            }
        }
        return;
    }
```

The drain loop is identical in structure to the happy-path receive loop below but writes to a throwaway buffer instead of the file. It must use `file_remaining` (captured before `fopen`) to know how many bytes to drain.

**Verify**: `make clean && make` exits 0 with no new warnings.

### Step 2: Verify the same issue does not exist in the server's `handle_download`

Check `src/server.c`'s `handle_download`. If `fopen` fails after receiving the file size, does it also drain? The current code may already handle this (it received the file size and the drain may be inline). Read it to confirm.

If the server has the same bug, note it in `plans/README.md` under "Findings considered and rejected" or as a dependency for a follow-up plan — do not fix it here.

**Verify**: `grep -n 'fopen.*= NULL\|fopen.*== NULL' src/server.c` — read the surrounding context. Report in your summary whether the same issue exists server-side.

## Test plan

After building:

1. Start server + client
2. Upload a file to a path that will cause `fopen` to fail:
   - On Linux: `upload /etc/shadow /nonexistent/dir/` (the path doesn't exist, `fopen` fails)
   - Or: `upload src/client.c /dev/full` (writes to /dev/full to simulate ENOSPC — though `fopen` on `/dev/full` succeeds, writes fail later — so use the first approach)
3. After the error response, run a simple command like `ls` and verify it produces correct output (proving the protocol is still in sync)

## Done criteria

ALL must hold:

- [ ] `make clean && make` exits 0
- [ ] The `fopen` failure path in `handle_upload` drains `file_remaining` bytes before returning
- [ ] After triggering an upload open error, a subsequent `ls` command returns correct output (verified manually)
- [ ] `plans/README.md` status row updated

## STOP conditions

Stop and report back if:

- The code at the cited locations doesn't match the excerpts
- The drain loop doesn't use `file_remaining` (the file size variable captured before `fopen`) — double-check `file_remaining` is in scope at the drain point
- You discover the drain needs to happen BEFORE `send_response_with_prompt` (which sends data itself) — if so, reorder so the drain happens first, then the response

## Maintenance notes

- The drain loop duplicates the happy-path receive loop's structure in exchange for correctness. If the receive logic changes (e.g., different chunking), both loops must be updated in lockstep.
- A future refactoring could unify them into a shared `receive_file_data(int fd, FILE *file)` that either writes to a file handle or discards when `file == NULL`.
