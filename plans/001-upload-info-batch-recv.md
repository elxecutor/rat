# Plan 001: Batch-receive upload info instead of one byte at a time

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report — do not improvise. When done, update the status row for this plan
> in `plans/README.md` unless a reviewer dispatched you and told you they
> maintain the index.
>
> **Drift check (run first)**: `git diff --stat fd5c459..HEAD -- src/client.c src/server.c`
> If any in-scope file changed since this plan was written, compare the
> "Current state" excerpts against the live code before proceeding; on a
> mismatch, treat it as a STOP condition.

## Status

- **Priority**: P1
- **Effort**: S
- **Risk**: LOW
- **Depends on**: none
- **Category**: bug
- **Planned at**: commit `fd5c459`, 2026-07-03

## Why this matters

The client's `handle_upload` function reads the upload info string ("destination|filename\n") one character at a time over an encrypted channel. Each `crypto_recv` call decrypts a full AES block (16 bytes) just to return 1 byte of plaintext. For a typical 50-character upload info, this means 50 separate decryption operations and 50 network round-trips instead of 1. On slow or congested links this can trigger the 1-second server read timeout before the upload info finishes arriving, causing uploads to fail silently.

The fix: batch-receive into a reasonably sized buffer, then scan for the newline terminator.

## Current state

`src/client.c` — function `handle_upload`, lines ~634–657:

```c
    // Receive upload info in format "destination|filename\n"
    memset(upload_info, 0, sizeof(upload_info));
    int info_pos = 0;
    char temp_char;
    
    // Read upload info character by character until newline
    while (info_pos < (int)(sizeof(upload_info) - 1)) {
        bytes_received = crypto_recv(client->client_fd, &client->crypto_ctx, &temp_char, 1, 0);
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                send_response_with_prompt(client, "Error: Connection closed during upload info reception");
            } else {
                send_response_with_prompt(client, "Error: Failed to receive upload info");
            }
            return;
        }
        
        if (temp_char == '\n') {
            break; // End of upload info
        }
        
        upload_info[info_pos++] = temp_char;
    }
    upload_info[info_pos] = '\0';
```

`upload_info` is `char[512]`. The loop calls `crypto_recv` once per byte.

## Commands you will need

| Purpose   | Command                  | Expected on success |
|-----------|--------------------------|---------------------|
| Build     | `make clean && make`     | exit 0, no errors  |

## Scope

**In scope** (the only files you should modify):
- `src/client.c` — the `handle_upload` function only

**Out of scope** (do NOT touch, even though they look related):
- `src/server.c` — the server side of upload is fine (it sends the whole string in one `crypto_send`)
- Any other function or file

## Git workflow

- Branch: `advisor/001-upload-info-batch-recv`
- Commit message style: conventional commits — `fix: batch-recv upload info instead of char-by-char`
- Do NOT push or open a PR unless the operator instructed it.

## Steps

### Step 1: Replace the character-by-character loop with a single batched recv

Locate the block in `src/client.c` starting at the `// Receive upload info` comment. Replace the char-by-char loop with:

```c
    // Receive upload info in format "destination|filename\n"
    memset(upload_info, 0, sizeof(upload_info));
    int bytes_received = crypto_recv(client->client_fd, &client->crypto_ctx,
                                     upload_info, sizeof(upload_info) - 1, 0);
    if (bytes_received <= 0) {
        send_response_with_prompt(client, "Error: Failed to receive upload info");
        return;
    }
    upload_info[bytes_received] = '\0';

    // Find the newline terminator (server sends "destination|filename\n")
    char *nl = strchr(upload_info, '\n');
    if (nl) {
        *nl = '\0';
    }
```

After this change:
- `upload_info` contains the full string in one shot (minus the trailing `\n`)
- The `int info_pos = 0;` and `char temp_char;` variables above the block are no longer needed — remove them along with the old loop
- The parse logic that follows (searching for `|`, copying to `destination`/`filename`, ACK send) stays exactly as-is

**Verify**: `make clean && make` — builds without errors or warnings (only pre-existing `_GNU_SOURCE` redefinition warnings are OK).

### Step 2: Verify variable cleanliness

Check that no stale variables remain from the deleted char-by-char logic:

- `src/client.c` should no longer contain `int info_pos` or `char temp_char` declarations in `handle_upload` (they're local to that function)
- The `bytes_received` variable at the top of `handle_upload` is reused — that's fine

**Verify**: `grep -n 'info_pos|temp_char' src/client.c` should return nothing in `handle_upload`. There will be no matches at all.

## Test plan

There are no tests in this project. After building, do a quick protocol-level sanity check:

1. Start the server: `./bin/server &`
2. Start the client: `./bin/client &`
3. At the `>> ` prompt, run `upload src/client.c` (a small file that exists on the server)
4. Verify the server prints `"Uploading file: src/client.c"` followed by `"File uploaded successfully"` (or similar)
5. Kill both processes: `pkill -INT server client`

The upload should succeed without hanging or timing out.

## Done criteria

ALL must hold:

- [ ] `make clean && make` exits 0
- [ ] The char-by-char `crypto_recv` loop (one byte per call in `handle_upload`) is removed
- [ ] The replacement uses a single `crypto_recv` with a buffer of at least `sizeof(upload_info) - 1`
- [ ] `grep -n 'info_pos\|temp_char' src/client.c` returns no matches
- [ ] `plans/README.md` status row updated

## STOP conditions

Stop and report back (do not improvise) if:

- The code at the locations above doesn't match the excerpts (the codebase has drifted)
- `make` produces any error (warnings about `_GNU_SOURCE` redefinition are pre-existing and OK)
- The newline-terminated upload info protocol doesn't match what the server sends (check `src/server.c`'s `handle_upload` — it constructs `upload_info` as `"%s|%s\n"` and sends it in one `crypto_send`)

## Maintenance notes

- The server always sends the upload info followed by a `\n` in a single `crypto_send` call. One `crypto_recv` on the client side receives the whole thing because `crypto_recv` reads one complete encrypted packet per call (length-prefixed internally). If the protocol is ever changed to send the info without a trailing newline, the `strchr(upload_info, '\n')` line in this plan must be revisited.
- The `upload_info` buffer (512 bytes) limits the combined length of destination path + separator + filename to 511 bytes. `strncpy` into 256-byte `destination`/`filename` already truncates silently — that's a separate, pre-existing limitation not addressed here.
