#define OPENSSL_SUPPRESS_DEPRECATED

#include "../include/crypto.h"
#include <errno.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

// Initialize crypto context
int crypto_init(crypto_context_t *ctx, int is_server) {
    if (!ctx) {
        return -1;
    }
    
    memset(ctx, 0, sizeof(crypto_context_t));
    ctx->is_server = is_server;
    ctx->is_encrypted = 0;
    
    // Initialize OpenSSL (no-op in 1.1+)
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();
#endif
    
    // Generate RSA keypair
    if (crypto_generate_rsa_keypair(ctx) != 0) {
        crypto_cleanup(ctx);
        return -1;
    }
    
    return 0;
}

// Cleanup crypto context
void crypto_cleanup(crypto_context_t *ctx) {
    if (!ctx) {
        return;
    }
    
    if (ctx->rsa_keypair) {
        RSA_free(ctx->rsa_keypair);
        ctx->rsa_keypair = NULL;
    }
    
    if (ctx->peer_public_key) {
        RSA_free(ctx->peer_public_key);
        ctx->peer_public_key = NULL;
    }
    
    // Clear sensitive data
    memset(ctx->aes_key, 0, AES_KEY_SIZE);
    memset(ctx->aes_iv, 0, AES_IV_SIZE);
    
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    EVP_cleanup();
    ERR_free_strings();
#endif
}

// Generate AES key and IV
int crypto_generate_aes_key(crypto_context_t *ctx) {
    if (!ctx) {
        return -1;
    }
    
    // Generate random AES key
    if (RAND_bytes(ctx->aes_key, AES_KEY_SIZE) != 1) {
        crypto_print_error();
        return -1;
    }
    
    // Generate random IV
    if (RAND_bytes(ctx->aes_iv, AES_IV_SIZE) != 1) {
        crypto_print_error();
        return -1;
    }
    
    return 0;
}

// Generate RSA keypair
int crypto_generate_rsa_keypair(crypto_context_t *ctx) {
    if (!ctx) {
        return -1;
    }
    
    EVP_PKEY_CTX *pkey_ctx = NULL;
    EVP_PKEY *pkey = NULL;
    
    // Create context for key generation
    pkey_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!pkey_ctx) {
        crypto_print_error();
        return -1;
    }
    
    // Initialize key generation
    if (EVP_PKEY_keygen_init(pkey_ctx) <= 0) {
        crypto_print_error();
        EVP_PKEY_CTX_free(pkey_ctx);
        return -1;
    }
    
    // Set key size
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(pkey_ctx, RSA_KEY_SIZE) <= 0) {
        crypto_print_error();
        EVP_PKEY_CTX_free(pkey_ctx);
        return -1;
    }
    
    // Generate the key
    if (EVP_PKEY_keygen(pkey_ctx, &pkey) <= 0) {
        crypto_print_error();
        EVP_PKEY_CTX_free(pkey_ctx);
        return -1;
    }
    
    // Extract RSA key
    ctx->rsa_keypair = EVP_PKEY_get1_RSA(pkey);
    if (!ctx->rsa_keypair) {
        crypto_print_error();
        EVP_PKEY_free(pkey);
        EVP_PKEY_CTX_free(pkey_ctx);
        return -1;
    }
    
    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(pkey_ctx);
    
    return 0;
}

// Export public key to data buffer
int crypto_export_public_key(crypto_context_t *ctx, unsigned char **key_data, int *key_len) {
    if (!ctx || !ctx->rsa_keypair || !key_data || !key_len) {
        return -1;
    }
    
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) {
        crypto_print_error();
        return -1;
    }
    
    // Write public key to BIO
    if (PEM_write_bio_RSA_PUBKEY(bio, ctx->rsa_keypair) != 1) {
        crypto_print_error();
        BIO_free(bio);
        return -1;
    }
    
    // Get data from BIO
    char *bio_data;
    long bio_len = BIO_get_mem_data(bio, &bio_data);
    if (bio_len <= 0) {
        BIO_free(bio);
        return -1;
    }
    
    // Allocate buffer and copy data
    *key_data = malloc(bio_len + 1);
    if (!*key_data) {
        BIO_free(bio);
        return -1;
    }
    
    memcpy(*key_data, bio_data, bio_len);
    (*key_data)[bio_len] = '\0';
    *key_len = bio_len;
    
    BIO_free(bio);
    return 0;
}

// Import peer's public key
int crypto_import_public_key(crypto_context_t *ctx, const unsigned char *key_data, int key_len) {
    if (!ctx || !key_data || key_len <= 0) {
        return -1;
    }
    
    BIO *bio = BIO_new_mem_buf(key_data, key_len);
    if (!bio) {
        crypto_print_error();
        return -1;
    }
    
    ctx->peer_public_key = PEM_read_bio_RSA_PUBKEY(bio, NULL, NULL, NULL);
    if (!ctx->peer_public_key) {
        crypto_print_error();
        BIO_free(bio);
        return -1;
    }
    
    BIO_free(bio);
    return 0;
}

// Encrypt AES key with peer's RSA public key
int crypto_encrypt_aes_key(crypto_context_t *ctx, unsigned char **encrypted_key, int *encrypted_len) {
    if (!ctx || !ctx->peer_public_key || !encrypted_key || !encrypted_len) {
        return -1;
    }
    
    int rsa_size = RSA_size(ctx->peer_public_key);
    *encrypted_key = malloc(rsa_size);
    if (!*encrypted_key) {
        return -1;
    }
    
    // Create key package (AES key + IV)
    unsigned char key_package[AES_KEY_SIZE + AES_IV_SIZE];
    memcpy(key_package, ctx->aes_key, AES_KEY_SIZE);
    memcpy(key_package + AES_KEY_SIZE, ctx->aes_iv, AES_IV_SIZE);
    
    // Encrypt with RSA
    *encrypted_len = RSA_public_encrypt(sizeof(key_package), key_package, 
                                       *encrypted_key, ctx->peer_public_key, 
                                       RSA_PKCS1_OAEP_PADDING);
    
    if (*encrypted_len == -1) {
        crypto_print_error();
        free(*encrypted_key);
        *encrypted_key = NULL;
        return -1;
    }
    
    return 0;
}

// Decrypt AES key with our RSA private key
int crypto_decrypt_aes_key(crypto_context_t *ctx, const unsigned char *encrypted_key, int encrypted_len) {
    if (!ctx || !ctx->rsa_keypair || !encrypted_key || encrypted_len <= 0) {
        return -1;
    }
    
    unsigned char key_package[AES_KEY_SIZE + AES_IV_SIZE];
    
    int decrypted_len = RSA_private_decrypt(encrypted_len, encrypted_key, 
                                           key_package, ctx->rsa_keypair, 
                                           RSA_PKCS1_OAEP_PADDING);
    
    if (decrypted_len != sizeof(key_package)) {
        crypto_print_error();
        return -1;
    }
    
    // Extract AES key and IV
    memcpy(ctx->aes_key, key_package, AES_KEY_SIZE);
    memcpy(ctx->aes_iv, key_package + AES_KEY_SIZE, AES_IV_SIZE);
    
    ctx->is_encrypted = 1;
    return 0;
}

// Encrypt data using AES
int crypto_encrypt_data(crypto_context_t *ctx, const unsigned char *plaintext, int plaintext_len,
                       unsigned char **ciphertext, int *ciphertext_len) {
    if (!ctx || !ctx->is_encrypted || !plaintext || plaintext_len <= 0 || 
        !ciphertext || !ciphertext_len) {
        return -1;
    }
    
    EVP_CIPHER_CTX *cipher_ctx = EVP_CIPHER_CTX_new();
    if (!cipher_ctx) {
        crypto_print_error();
        return -1;
    }
    
    // Allocate output buffer (input size + block size for padding)
    int max_ciphertext_len = plaintext_len + AES_BLOCK_SIZE;
    *ciphertext = malloc(max_ciphertext_len);
    if (!*ciphertext) {
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    
    // Initialize encryption
    if (EVP_EncryptInit_ex(cipher_ctx, EVP_aes_256_cbc(), NULL, ctx->aes_key, ctx->aes_iv) != 1) {
        crypto_print_error();
        free(*ciphertext);
        *ciphertext = NULL;
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    
    int len;
    // Encrypt data
    if (EVP_EncryptUpdate(cipher_ctx, *ciphertext, &len, plaintext, plaintext_len) != 1) {
        crypto_print_error();
        free(*ciphertext);
        *ciphertext = NULL;
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    *ciphertext_len = len;
    
    // Finalize encryption
    if (EVP_EncryptFinal_ex(cipher_ctx, *ciphertext + len, &len) != 1) {
        crypto_print_error();
        free(*ciphertext);
        *ciphertext = NULL;
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    *ciphertext_len += len;
    
    EVP_CIPHER_CTX_free(cipher_ctx);
    return 0;
}

// Decrypt data using AES
int crypto_decrypt_data(crypto_context_t *ctx, const unsigned char *ciphertext, int ciphertext_len,
                       unsigned char **plaintext, int *plaintext_len) {
    if (!ctx || !ctx->is_encrypted || !ciphertext || ciphertext_len <= 0 || 
        !plaintext || !plaintext_len) {
        return -1;
    }
    
    EVP_CIPHER_CTX *cipher_ctx = EVP_CIPHER_CTX_new();
    if (!cipher_ctx) {
        crypto_print_error();
        return -1;
    }
    
    // Allocate output buffer
    *plaintext = malloc(ciphertext_len + AES_BLOCK_SIZE);
    if (!*plaintext) {
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    
    // Initialize decryption
    if (EVP_DecryptInit_ex(cipher_ctx, EVP_aes_256_cbc(), NULL, ctx->aes_key, ctx->aes_iv) != 1) {
        crypto_print_error();
        free(*plaintext);
        *plaintext = NULL;
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    
    int len;
    // Decrypt data
    if (EVP_DecryptUpdate(cipher_ctx, *plaintext, &len, ciphertext, ciphertext_len) != 1) {
        crypto_print_error();
        free(*plaintext);
        *plaintext = NULL;
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    *plaintext_len = len;
    
    // Finalize decryption
    if (EVP_DecryptFinal_ex(cipher_ctx, *plaintext + len, &len) != 1) {
        crypto_print_error();
        free(*plaintext);
        *plaintext = NULL;
        EVP_CIPHER_CTX_free(cipher_ctx);
        return -1;
    }
    *plaintext_len += len;
    
    EVP_CIPHER_CTX_free(cipher_ctx);
    return 0;
}

// Secure send with encryption
int crypto_send(socket_t socket_fd, crypto_context_t *ctx, const void *data, size_t len, int flags) {
    if (!ctx || !data || len == 0) {
        return -1;
    }
    
    // If encryption is not set up, send plain data
    if (!ctx->is_encrypted) {
        return send(socket_fd, data, len, flags);
    }
    
    unsigned char *encrypted_data = NULL;
    int encrypted_len = 0;
    
    // Encrypt the data
    if (crypto_encrypt_data(ctx, (const unsigned char *)data, len, 
                           &encrypted_data, &encrypted_len) != 0) {
        return -1;
    }
    
    // Send length first (4 bytes, network byte order) with retry
    uint32_t net_len = htonl(encrypted_len);
    {
        const unsigned char *p = (const unsigned char *)&net_len;
        size_t remain = sizeof(net_len);
        while (remain > 0) {
            int sent = send(socket_fd, p, remain, flags);
            if (sent <= 0) {
                free(encrypted_data);
                return -1;
            }
            p += sent;
            remain -= sent;
        }
    }
    
    // Send encrypted data with retry
    {
        const unsigned char *p = encrypted_data;
        size_t remain = (size_t)encrypted_len;
        while (remain > 0) {
            int sent = send(socket_fd, p, remain, flags);
            if (sent <= 0) {
                free(encrypted_data);
                return -1;
            }
            p += sent;
            remain -= sent;
        }
    }
    free(encrypted_data);
    return (int)len;
}

// Secure receive with decryption
int crypto_recv(socket_t socket_fd, crypto_context_t *ctx, void *buffer, size_t len, int flags) {
    if (!ctx || !buffer || len == 0) {
        return -1;
    }
    
    // If encryption is not set up, receive plain data
    if (!ctx->is_encrypted) {
        return recv(socket_fd, buffer, len, flags);
    }
    
    // Receive length first with retry
    uint32_t net_len;
    {
        unsigned char *p = (unsigned char *)&net_len;
        size_t remain = sizeof(net_len);
        while (remain > 0) {
            int received = recv(socket_fd, p, remain, flags);
            if (received <= 0) {
                return -1;
            }
            p += received;
            remain -= received;
        }
    }
    
    uint32_t encrypted_len = ntohl(net_len);
    if (encrypted_len > MAX_ENCRYPTED_SIZE) {
        return -1; // Sanity check
    }
    
    // Receive encrypted data with retry
    unsigned char *encrypted_data = malloc(encrypted_len);
    if (!encrypted_data) {
        return -1;
    }
    
    {
        unsigned char *p = encrypted_data;
        size_t remain = encrypted_len;
        while (remain > 0) {
            int received = recv(socket_fd, p, remain, flags);
            if (received <= 0) {
                free(encrypted_data);
                return -1;
            }
            p += received;
            remain -= received;
        }
    }
    
    // Decrypt the data
    unsigned char *decrypted_data = NULL;
    int decrypted_len = 0;
    
    if (crypto_decrypt_data(ctx, encrypted_data, encrypted_len, 
                           &decrypted_data, &decrypted_len) != 0) {
        free(encrypted_data);
        return -1;
    }
    
    free(encrypted_data);
    
    // Copy to output buffer
    if (decrypted_len > (int)len) {
        free(decrypted_data);
        return -1; // Buffer too small
    }
    
    memcpy(buffer, decrypted_data, decrypted_len);
    free(decrypted_data);
    
    return decrypted_len;
}

// Print OpenSSL errors
void crypto_print_error(void) {
    unsigned long err;
    while ((err = ERR_get_error()) != 0) {
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        fprintf(stderr, "OpenSSL Error: %s\n", err_buf);
    }
}

// Generate secure random bytes
int crypto_secure_random(unsigned char *buffer, int length) {
    if (!buffer || length <= 0) {
        return -1;
    }
    
    if (RAND_bytes(buffer, length) != 1) {
        crypto_print_error();
        return -1;
    }
    
    return 0;
}

// Compute SHA-256 hash of the pre-shared key and store it in ctx
void crypto_set_psk(crypto_context_t *ctx, const char *psk) {
    if (!ctx || !psk) return;
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    EVP_MD_CTX *mdctx = EVP_MD_CTX_create();
#else
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
#endif
    if (!mdctx) return;
    if (EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL) == 1 &&
        EVP_DigestUpdate(mdctx, psk, strlen(psk)) == 1) {
        unsigned int len = 32;
        EVP_DigestFinal_ex(mdctx, ctx->psk_hash, &len);
    }
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    EVP_MD_CTX_destroy(mdctx);
#else
    EVP_MD_CTX_free(mdctx);
#endif
}

// Send the PSK challenge: flag byte 0x01 + 32-byte hash
int crypto_send_psk_challenge(socket_t socket_fd, crypto_context_t *ctx, int flags) {
    if (!ctx || !ctx->psk_hash[0]) return -1;
    
    unsigned char flag = 0x01;
    {
        const unsigned char *p = &flag;
        size_t remain = 1;
        while (remain > 0) {
            int sent = send(socket_fd, p, remain, flags);
            if (sent <= 0) return -1;
            p += sent;
            remain -= sent;
        }
    }
    {
        const unsigned char *p = ctx->psk_hash;
        size_t remain = 32;
        while (remain > 0) {
            int sent = send(socket_fd, p, remain, flags);
            if (sent <= 0) return -1;
            p += sent;
            remain -= sent;
        }
    }
    return 0;
}

// Receive and verify the peer's PSK hash
// Uses MSG_PEEK to avoid consuming the first byte when no PSK challenge exists
// (the first byte is then the key-length MSB for the RSA key exchange)
// Returns: 0 on match, -1 on mismatch/error, 1 if peer has no PSK
int crypto_recv_psk_challenge(socket_t socket_fd, crypto_context_t *ctx, int flags) {
    if (!ctx || !ctx->psk_hash[0]) return -1;
    
    // Peek at the first byte without consuming it
    unsigned char flag = 0;
    int peeked = recv(socket_fd, &flag, 1, flags | MSG_PEEK);
    if (peeked <= 0) return 1;
    
    if (flag == 0x00) {
        // Peer has no PSK (first byte of key length) — leave it for key exchange
        return 1;
    }
    if (flag != 0x01) {
        return -1;
    }
    
    // Consume the flag byte
    recv(socket_fd, &flag, 1, flags);
    
    unsigned char peer_hash[32];
    {
        unsigned char *p = peer_hash;
        size_t remain = 32;
        while (remain > 0) {
            int received = recv(socket_fd, p, remain, flags);
            if (received <= 0) return -1;
            p += received;
            remain -= received;
        }
    }
    
    return (memcmp(ctx->psk_hash, peer_hash, 32) == 0) ? 0 : -1;
}