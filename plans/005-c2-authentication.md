# Plan 005: Add pre-shared key authentication to the C2 handshake

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report — do not improvise. When done, update the status row for this plan
> in `plans/README.md` unless a reviewer dispatched you and told you they
> maintain the index.
>
> **Drift check (run first)**: `git diff --stat fd5c459..HEAD -- src/client.c src/server.c include/crypto.h src/crypto.c`
> If any in-scope file changed since this plan was written, compare the
> "Current state" excerpts against the live code before proceeding; on a
> mismatch, treat it as a STOP condition.

## Status

- **Priority**: P2 (high impact, but higher effort and risk than 001–004)
- **Effort**: L (multi-day, including testing)
- **Risk**: MED — an auth protocol is subtle; a flawed implementation is worse than none
- **Depends on**: none (but do 001–004 first since they touch the same files; merge conflicts are likely)
- **Category**: security
- **Planned at**: commit `fd5c459`, 2026-07-03

## Why this matters

The current C2 channel uses RSA+AES encryption (via `crypto.c`) to protect **confidentiality**, but there is no **identity verification**. The server accepts any TCP connection and immediately performs a key exchange, granting full shell access to whoever controls the other end of the socket. Anyone who can reach the server's port (including on localhost via a compromised adjacent process) gets unauthenticated command execution.

The fix: add a pre-shared key (PSK) exchange stage before the RSA key exchange. Both sides must prove knowledge of a shared secret before the session proceeds. The PSK is compiled into the client binary and configurable via environment variable on the server.

## Current state

The handshake flow in `src/server.c` (`accept_client` → `perform_key_exchange`) and `src/client.c` (`connect_to_server` → `perform_client_key_exchange`):

1. Server generates RSA keypair, sends its public key → client
2. Client sends its public key → server
3. Server generates AES key, encrypts with client's public key, sends it → client
4. Client decrypts AES key
5. All subsequent traffic is AES-256-CBC encrypted

There's no step that verifies either side knows a shared secret.

`include/crypto.h` — `crypto_context_t` holds RSA keys and AES key material. `crypto_init(ctx, is_server)` generates the RSA keypair.

`src/crypto.c` — key exchange functions (`crypto_export_public_key`, `crypto_import_public_key`, `crypto_encrypt_aes_key`, `crypto_decrypt_aes_key`).

## Commands you will need

| Purpose      | Command                    | Expected on success |
|--------------|----------------------------|---------------------|
| Build        | `make clean && make`       | exit 0              |
| Quick test   | `echo "test" | sha256sum`  | produce a hash      |

## Scope

**In scope**:
- `include/crypto.h` — add `psk_hash` field to `crypto_context_t`; add `crypto_set_psk` and `crypto_verify_psk` declarations
- `src/crypto.c` — implement `crypto_set_psk` (hash the PSK into `psk_hash`) and `crypto_verify_psk` (compare received hash against stored hash)
- `src/server.c` — in `accept_client` or `perform_key_exchange`, send a PSK challenge and verify the response
- `src/client.c` — in `connect_to_server` or `perform_client_key_exchange`, receive the PSK challenge and respond

**Out of scope**:
- PSK provisioning UI or config file parsing — the PSK is set via `crypto_set_psk(psk_string)` called from `main()` in each binary
- Certificate-based auth or public-key infrastructure — that's a different plan
- Encrypting the PSK challenge with the already-exchanged RSA key — the PSK check happens BEFORE the RSA key exchange, on the raw TCP stream, so that unauthenticated peers never trigger RSA key generation. Alternatively, it can piggyback on the existing RSA exchange. **Recommendation: do the PSK check on the raw TCP stream before the RSA handshake**, so unauthenticated connections never consume RSA key generation resources.

## Git workflow

- Branch: `advisor/005-c2-auth`
- Commit message: `feat: add pre-shared key authentication to C2 handshake`
- Do NOT push or open a PR unless instructed.

## Steps

### Step 1: Add PSK fields and declarations to `include/crypto.h`

Add to `crypto_context_t` (inside `include/crypto.h`):

```c
typedef struct {
    unsigned char aes_key[AES_KEY_SIZE];
    unsigned char aes_iv[AES_IV_SIZE];
    RSA *rsa_keypair;
    RSA *peer_public_key;
    int is_encrypted;
    int is_server;
    unsigned char psk_hash[32];  // SHA-256 of the pre-shared key, 0 if unset
} crypto_context_t;
```

Add new function declarations:

```c
// Pre-shared key authentication
void crypto_set_psk(crypto_context_t *ctx, const char *psk);
int crypto_send_psk_challenge(socket_t socket_fd, crypto_context_t *ctx, int flags);
int crypto_recv_psk_challenge(socket_t socket_fd, crypto_context_t *ctx, int flags);
```

`crypto_set_psk` stores the SHA-256 hash of the PSK string in `ctx->psk_hash`. `crypto_send_psk_challenge` sends the 32-byte hash to the peer. `crypto_recv_psk_challenge` receives 32 bytes from the peer and compares them to the local hash, returning 0 on match or -1 on mismatch.

**Verify**: `make clean && make` exits 0.

### Step 2: Implement PSK functions in `src/crypto.c`

**`crypto_set_psk`** — compute SHA-256 of the PSK string:

```c
void crypto_set_psk(crypto_context_t *ctx, const char *psk) {
    if (!ctx || !psk) return;
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) return;
    if (EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL) == 1 &&
        EVP_DigestUpdate(mdctx, psk, strlen(psk)) == 1) {
        unsigned int len = 32;
        EVP_DigestFinal_ex(mdctx, ctx->psk_hash, &len);
    }
    EVP_MD_CTX_free(mdctx);
}
```

SHA-256 is available via `openssl/evp.h` (already included). `EVP_MD_CTX_new`/`EVP_MD_CTX_free` are OpenSSL 1.1+ APIs; for OpenSSL 1.0.x use `EVP_MD_CTX_create`/`EVP_MD_CTX_destroy` — wrap with `#if OPENSSL_VERSION_NUMBER < 0x10100000L` if needed.

**`crypto_send_psk_challenge`** — send the 32-byte hash:

```c
int crypto_send_psk_challenge(socket_t socket_fd, crypto_context_t *ctx, int flags) {
    if (!ctx || !ctx->psk_hash[0]) return -1;  // PSK not set
    // Send the 32-byte hash directly over the socket (no encryption — PSK check happens before key exchange)
    return send_all(socket_fd, ctx->psk_hash, 32, flags);
}
```

(Use `send_all` from `server.c`/`client.c` — the helper functions are available in each file.)

**`crypto_recv_psk_challenge`** — receive and verify:

```c
int crypto_recv_psk_challenge(socket_t socket_fd, crypto_context_t *ctx, int flags) {
    if (!ctx || !ctx->psk_hash[0]) return -1;
    unsigned char peer_hash[32];
    if (recv_all(socket_fd, peer_hash, 32, flags) != 0) return -1;
    return (memcmp(ctx->psk_hash, peer_hash, 32) == 0) ? 0 : -1;
}
```

`send_all`/`recv_all` are defined as `static` in `client.c` and `server.c`. The crypto functions need access to them. Two approaches:
- **Recommended**: make `send_all` and `recv_all` non-static and declare them in `crypto.h` (but that leaks implementation detail)
- **Alternative**: inline the send/recv loops directly in `crypto_send_psk_challenge` / `crypto_recv_psk_challenge` — this duplicates code but keeps the crypto module self-contained

The plan recommends the **inline approach** to avoid modifying the helper signatures. The loops are trivial (3 lines each).

**Verify**: `make clean && make` exits 0.

### Step 3: Insert PSK check into the server's handshake

In `src/server.c`, inside `accept_client` (after `accept` succeeds, before `crypto_init`):

```c
// PSK authentication (if configured)
if (server->crypto_ctx.psk_hash[0] != 0) {
    // Server sends its hash first, then verifies client's response
    if (crypto_send_psk_challenge(server->client_fd, &server->crypto_ctx, 0) != 0 ||
        crypto_recv_psk_challenge(server->client_fd, &server->crypto_ctx, 0) != 0) {
        printf("[!] PSK authentication failed — client rejected\n");
        close(server->client_fd);
        server->client_fd = INVALID_SOCKET;
        return -1;
    }
    printf("[*] PSK authentication successful\n");
}
```

The order matters: server sends its hash first, then receives the client's. Both sides must agree on this order.

**Verify**: `make clean && make` exits 0.

### Step 4: Insert PSK check into the client's handshake

In `src/client.c`, inside `connect_to_server` (after socket connects, before `crypto_init`):

```c
// PSK authentication (if configured)
if (client->crypto_ctx.psk_hash[0] != 0) {
    // Client receives server's hash first, then sends its own
    if (crypto_recv_psk_challenge(client->client_fd, &client->crypto_ctx, 0) != 0 ||
        crypto_send_psk_challenge(client->client_fd, &client->crypto_ctx, 0) != 0) {
        printf("Error: PSK authentication failed\n");
        return -1;
    }
}
```

The order mirrors the server: recv then send.

**Verify**: `make clean && make` exits 0.

### Step 5: Configure the PSK from each binary's entry point

In `src/client.c` `main()`, before `connect_to_server`:

```c
    // Pre-shared key for server authentication
    const char *psk = getenv("RAT_PSK");
    if (psk) {
        crypto_set_psk(&client.crypto_ctx, psk);
    }
```

In `src/server.c` `main()`, before `accept_client`:

```c
    // Pre-shared key for client authentication
    const char *psk = getenv("RAT_PSK");
    if (psk) {
        crypto_set_psk(&server.crypto_ctx, psk);
    }
```

When `RAT_PSK` is unset, the PSK hash remains all-zeros, and the PSK check is skipped entirely (backward compatible). When set on both sides, the check is enforced.

**Verify**: `make clean && make` exits 0.

### Step 6: Test both modes

**No PSK (backward compatible)**:

1. Start server without `RAT_PSK`: `./bin/server &`
2. Start client without `RAT_PSK`: `./bin/client &`
3. Run a command at `>> ` prompt: `ls` — should work normally

**PSK set (matching)**:

1. Kill previous processes: `pkill -INT server client`
2. Start server with PSK: `RAT_PSK="hunter2" ./bin/server &`
3. Start client with PSK: `RAT_PSK="hunter2" ./bin/client &`
4. Run `ls` — should work normally

**PSK mismatch**:

1. Kill previous processes
2. Start server: `RAT_PSK="correct" ./bin/server &`
3. Start client: `RAT_PSK="wrong" ./bin/client &`
4. Client should print `"Error: PSK authentication failed"` and exit
5. Server should print `"[!] PSK authentication failed"` and continue listening

**PSK on server only**:

1. Kill previous processes
2. Start server: `RAT_PSK="secret" ./bin/server &`
3. Start client without PSK: `./bin/client &`
4. Client fails — server requires PSK, client doesn't provide it

**PSK on client only**:

1. Kill previous processes
2. Start server without PSK: `./bin/server &`
3. Start client: `RAT_PSK="secret" ./bin/client &`
4. Server accepts (no PSK configured), client waits for server's PSK challenge that never comes — timeout? Behavior depends on implementation. **The server-side check is the authoritative one**: if the server doesn't require PSK, connections are accepted. The client-side PSK is a "verify the server" mechanism. If the server sends no challenge, `crypto_recv_psk_challenge` blocks on `recv_all` waiting for 32 bytes that never arrive. Handle this by adding a 2-second `setsockopt(SO_RCVTIMEO)` before the recv, and on timeout treat it as "server has no PSK" — accept the connection (backward compat). This is important: without a timeout, a client with PSK will hang forever against a server without PSK.

## Test plan

All five test scenarios above must pass:

1. No PSK on either side → full access (backward compat)
2. Matching PSK → full access
3. Mismatched PSK → connection rejected
4. Server PSK only → client must adapt (timeout or explicit signal)
5. Client PSK only → server ignores, client adapts

## Done criteria

ALL must hold:

- [ ] `make clean && make` exits 0
- [ ] `include/crypto.h` has `psk_hash[32]` field and new function declarations
- [ ] `src/crypto.c` implements `crypto_set_psk`, `crypto_send_psk_challenge`, `crypto_recv_psk_challenge`
- [ ] `src/server.c` performs PSK challenge in `accept_client` before crypto init
- [ ] `src/client.c` performs PSK response in `connect_to_server` before crypto init
- [ ] Both binaries accept `RAT_PSK` environment variable
- [ ] All 5 test scenarios pass
- [ ] `plans/README.md` status row updated

## STOP conditions

Stop and report back if:

- The PSK check requires OpenSSL 1.1+ SHA-256 APIs (`EVP_MD_CTX_new` not available in 1.0.x) — wrap with version guard
- The raw `recv_all` call in `crypto_recv_psk_challenge` blocks indefinitely when the peer doesn't send a PSK challenge (the timeout scenario in test 4/5) — add a 2-second `SO_RCVTIMEO` before the recv
- The `send_all`/`recv_all` helpers are `static` in both `client.c` and `server.c` and can't be called from crypto.c — the plan already recommends inline loops; if they're not accessible, inline them
- You discover that the PSK hash is sent in the clear before the RSA handshake (it is — this is intentional; the PSK is never reused, so passive capture doesn't help an attacker replay it)

## Maintenance notes

- The PSK is hashed with SHA-256 before transmission, so the raw key material never leaves the process. The hash is reusable as a proof of knowledge. If an attacker captures the hash, they can replay it — but only until the PSK is rotated (which is external to this plan).
- A future improvement could bind the PSK to the RSA key exchange: hash the PSK along with the ephemeral RSA public keys (station-to-station protocol). That would prevent replay attacks entirely but requires a more complex protocol change.
- The `RAT_PSK` environment variable leaks the key into the process's environment (visible via `/proc/pid/environ`). On a production C2 server, consider reading from a file with restricted permissions instead. For this plan, env var is sufficient — it matches the project's current level of operational security.
- If the client's `crypto_recv_psk_challenge` times out (server has no PSK), the socket has been in non-blocking-ish mode. Reset `SO_RCVTIMEO` to 0 (blocking) after the timeout branch to avoid affecting subsequent `recv_all` calls.
