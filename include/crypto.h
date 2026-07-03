#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/aes.h>

#ifdef _WIN32
#include <winsock2.h>
typedef SOCKET socket_t;
#else
typedef int socket_t;
#endif

// Encryption constants
#define AES_KEY_SIZE 32        // 256 bits
#define AES_IV_SIZE 16         // 128 bits
#define RSA_KEY_SIZE 2048      // RSA key size in bits
#define MAX_ENCRYPTED_SIZE 8192 // Maximum size for encrypted data chunks

// Encryption context structure
typedef struct {
    unsigned char aes_key[AES_KEY_SIZE];
    unsigned char aes_iv[AES_IV_SIZE];
    RSA *rsa_keypair;
    RSA *peer_public_key;
    int is_encrypted;
    int is_server;
    unsigned char psk_hash[32];  // SHA-256 of the pre-shared key, 0 if unset
} crypto_context_t;

// Key management functions
int crypto_init(crypto_context_t *ctx, int is_server);
void crypto_cleanup(crypto_context_t *ctx);
int crypto_generate_aes_key(crypto_context_t *ctx);
int crypto_generate_rsa_keypair(crypto_context_t *ctx);

// Key exchange functions
int crypto_export_public_key(crypto_context_t *ctx, unsigned char **key_data, int *key_len);
int crypto_import_public_key(crypto_context_t *ctx, const unsigned char *key_data, int key_len);
int crypto_encrypt_aes_key(crypto_context_t *ctx, unsigned char **encrypted_key, int *encrypted_len);
int crypto_decrypt_aes_key(crypto_context_t *ctx, const unsigned char *encrypted_key, int encrypted_len);

// Encryption/Decryption functions
int crypto_encrypt_data(crypto_context_t *ctx, const unsigned char *plaintext, int plaintext_len,
                       unsigned char **ciphertext, int *ciphertext_len);
int crypto_decrypt_data(crypto_context_t *ctx, const unsigned char *ciphertext, int ciphertext_len,
                       unsigned char **plaintext, int *plaintext_len);

// Network wrapper functions
int crypto_send(socket_t socket_fd, crypto_context_t *ctx, const void *data, size_t len, int flags);
int crypto_recv(socket_t socket_fd, crypto_context_t *ctx, void *buffer, size_t len, int flags);

// Pre-shared key authentication
void crypto_set_psk(crypto_context_t *ctx, const char *psk);
int crypto_send_psk_challenge(socket_t socket_fd, crypto_context_t *ctx, int flags);
int crypto_recv_psk_challenge(socket_t socket_fd, crypto_context_t *ctx, int flags);

// Utility functions
void crypto_print_error(void);
int crypto_secure_random(unsigned char *buffer, int length);

#endif // CRYPTO_H