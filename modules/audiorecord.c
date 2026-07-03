#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#ifdef __linux__
#include <linux/soundcard.h>
#endif
#include <signal.h>
#include <time.h>

#ifdef __linux__
#define SAMPLE_RATE 44100
#define CHANNELS 1
#define BITS_PER_SAMPLE 16
#define BUFFER_SIZE 4096
#define RECORD_DURATION 30  // seconds

volatile int recording = 1;

void signal_handler(int sig) {
    recording = 0;
}

int main(int argc, char *argv[]) {
    int audio_fd;
    int format = AFMT_S16_LE;
    int channels = CHANNELS;
    int rate = SAMPLE_RATE;
    int duration = RECORD_DURATION;
    char buffer[BUFFER_SIZE];
    char filename[256];
    FILE *output_file;
    time_t start_time, current_time;
    
    // Parse command line arguments
    if (argc > 1) {
        duration = atoi(argv[1]);
        if (duration <= 0) duration = RECORD_DURATION;
    }
    
    // Generate filename with timestamp
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    snprintf(filename, sizeof(filename), "audio_%04d%02d%02d_%02d%02d%02d.raw",
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    
    // Open audio device
    audio_fd = open("/dev/dsp", O_RDONLY);
    if (audio_fd < 0) {
        // Try alternative devices
        audio_fd = open("/dev/dsp0", O_RDONLY);
        if (audio_fd < 0) {
            audio_fd = open("/dev/audio", O_RDONLY);
            if (audio_fd < 0) {
                fprintf(stderr, "Error: Cannot open audio device\n");
                return 1;
            }
        }
    }
    
    // Set audio format
    if (ioctl(audio_fd, SNDCTL_DSP_SETFMT, &format) < 0) {
        fprintf(stderr, "Error: Cannot set audio format\n");
        close(audio_fd);
        return 1;
    }
    
    // Set channels
    if (ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &channels) < 0) {
        fprintf(stderr, "Error: Cannot set audio channels\n");
        close(audio_fd);
        return 1;
    }
    
    // Set sample rate
    if (ioctl(audio_fd, SNDCTL_DSP_SPEED, &rate) < 0) {
        fprintf(stderr, "Error: Cannot set sample rate\n");
        close(audio_fd);
        return 1;
    }
    
    // Open output file
    output_file = fopen(filename, "wb");
    if (!output_file) {
        fprintf(stderr, "Error: Cannot create output file\n");
        close(audio_fd);
        return 1;
    }
    
    // Set up signal handler for graceful exit
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Recording audio to %s for %d seconds...\n", filename, duration);
    printf("Sample rate: %d Hz, Channels: %d, Format: 16-bit\n", rate, channels);
    
    time(&start_time);
    
    // Recording loop
    while (recording) {
        time(&current_time);
        if (current_time - start_time >= duration) {
            break;
        }
        
        ssize_t bytes_read = read(audio_fd, buffer, BUFFER_SIZE);
        if (bytes_read > 0) {
            fwrite(buffer, 1, bytes_read, output_file);
        } else if (bytes_read < 0) {
            fprintf(stderr, "Error reading from audio device\n");
            break;
        }
        
        // Small delay to prevent excessive CPU usage
        usleep(1000);
    }
    
    // Cleanup
    fclose(output_file);
    close(audio_fd);
    
    printf("Recording completed: %s\n", filename);
    printf("To convert to WAV: sox -r %d -c %d -b %d -e signed-integer %s output.wav\n", 
           rate, channels, BITS_PER_SAMPLE, filename);
    
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