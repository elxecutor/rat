#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#ifdef __linux__
#include <linux/fb.h>
#endif
#include <signal.h>
#include <time.h>
#include <stdint.h>
#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#endif
#include <sys/shm.h>

#ifdef __linux__
#define DEFAULT_FPS 10
#define DEFAULT_DURATION 30

volatile int recording = 1;

void signal_handler(int sig) {
    recording = 0;
}

// Simple BMP header structure
typedef struct {
    char signature[2];
    uint32_t file_size;
    uint32_t reserved;
    uint32_t data_offset;
    uint32_t header_size;
    uint32_t width;
    uint32_t height;
    uint16_t planes;
    uint16_t bits_per_pixel;
    uint32_t compression;
    uint32_t image_size;
    uint32_t x_pixels_per_meter;
    uint32_t y_pixels_per_meter;
    uint32_t colors_used;
    uint32_t important_colors;
} __attribute__((packed)) bmp_header_t;

void write_bmp_header(FILE *file, int width, int height) {
    bmp_header_t header = {0};
    
    header.signature[0] = 'B';
    header.signature[1] = 'M';
    header.data_offset = sizeof(bmp_header_t);
    header.header_size = 40;
    header.width = width;
    header.height = height;
    header.planes = 1;
    header.bits_per_pixel = 24;
    header.compression = 0;
    header.image_size = width * height * 3;
    header.file_size = header.data_offset + header.image_size;
    
    fwrite(&header, sizeof(header), 1, file);
}

int capture_x11_screen(const char *filename_prefix, int fps, int duration) {
    Display *display;
    Window root;
    XImage *image;
    int screen;
    unsigned int width, height;
    char filename[256];
    FILE *output;
    time_t start_time, current_time;
    int frame_count = 0;
    int sleep_time = 1000000 / fps; // microseconds
    
    // Open X11 display
    display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Error: Cannot open X11 display\n");
        return -1;
    }
    
    screen = DefaultScreen(display);
    root = RootWindow(display, screen);
    
    // Get screen dimensions
    width = DisplayWidth(display, screen);
    height = DisplayHeight(display, screen);
    
    printf("X11 Screen: %dx%d\n", width, height);
    printf("Recording at %d FPS for %d seconds\n", fps, duration);
    
    // Create output filename with timestamp
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    snprintf(filename, sizeof(filename), "%s_%04d%02d%02d_%02d%02d%02d.raw",
             filename_prefix,
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    
    output = fopen(filename, "wb");
    if (!output) {
        fprintf(stderr, "Error: Cannot create output file\n");
        XCloseDisplay(display);
        return -1;
    }
    
    // Write a simple header with dimensions and fps
    fwrite(&width, sizeof(width), 1, output);
    fwrite(&height, sizeof(height), 1, output);
    fwrite(&fps, sizeof(fps), 1, output);
    
    printf("Recording to: %s\n", filename);
    time(&start_time);
    
    while (recording && frame_count < (fps * duration)) {
        time(&current_time);
        if (current_time - start_time >= duration) {
            break;
        }
        
    // Capture screen
    image = XGetImage(display, root, 0, 0, width, height, AllPlanes, ZPixmap);
    if (!image) {
        // Try alternative method with different parameters
        image = XGetImage(display, root, 0, 0, width, height, ~0L, ZPixmap);
        if (!image) {
            fprintf(stderr, "Error: Cannot capture screen\n");
            break;
        }
    }        // Convert and write frame data (simple RGB format)
        for (unsigned int y = 0; y < height; y++) {
            for (unsigned int x = 0; x < width; x++) {
                unsigned long pixel = XGetPixel(image, x, y);
                
                // Extract RGB components (assuming 24-bit color)
                unsigned char r = (pixel >> 16) & 0xFF;
                unsigned char g = (pixel >> 8) & 0xFF;
                unsigned char b = pixel & 0xFF;
                
                fwrite(&r, 1, 1, output);
                fwrite(&g, 1, 1, output);
                fwrite(&b, 1, 1, output);
            }
        }
        
        XDestroyImage(image);
        frame_count++;
        
        if (frame_count % fps == 0) {
            printf("Recorded %d seconds (%d frames)\n", frame_count / fps, frame_count);
        }
        
        // Sleep to maintain frame rate
        usleep(sleep_time);
    }
    
    fclose(output);
    XCloseDisplay(display);
    
    printf("Recording completed: %s (%d frames)\n", filename, frame_count);
    printf("To convert to video:\n");
    printf("ffmpeg -f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i %s output.mp4\n", 
           width, height, fps, filename);
    
    return 0;
}

int capture_framebuffer_screen(const char *filename_prefix, int fps, int duration) {
    int fb_fd;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    char *framebuffer;
    long screensize;
    char filename[256];
    FILE *output;
    time_t start_time, current_time;
    int frame_count = 0;
    int sleep_time = 1000000 / fps;
    
    // Open framebuffer device
    fb_fd = open("/dev/fb0", O_RDONLY);
    if (fb_fd < 0) {
        fprintf(stderr, "Error: Cannot open framebuffer device\n");
        return -1;
    }
    
    // Get screen info
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        fprintf(stderr, "Error: Cannot get framebuffer info\n");
        close(fb_fd);
        return -1;
    }
    
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        fprintf(stderr, "Error: Cannot get variable screen info\n");
        close(fb_fd);
        return -1;
    }
    
    printf("Framebuffer: %dx%d, %d bpp\n", 
           vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
    
    // Calculate screen size
    screensize = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
    
    // Map framebuffer to memory
    framebuffer = (char*)mmap(0, screensize, PROT_READ, MAP_SHARED, fb_fd, 0);
    if (framebuffer == MAP_FAILED) {
        fprintf(stderr, "Error: Cannot map framebuffer\n");
        close(fb_fd);
        return -1;
    }
    
    // Create output filename
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    snprintf(filename, sizeof(filename), "%s_%04d%02d%02d_%02d%02d%02d.raw",
             filename_prefix,
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    
    output = fopen(filename, "wb");
    if (!output) {
        fprintf(stderr, "Error: Cannot create output file\n");
        munmap(framebuffer, screensize);
        close(fb_fd);
        return -1;
    }
    
    // Write header
    fwrite(&vinfo.xres, sizeof(vinfo.xres), 1, output);
    fwrite(&vinfo.yres, sizeof(vinfo.yres), 1, output);
    fwrite(&fps, sizeof(fps), 1, output);
    
    printf("Recording framebuffer to: %s\n", filename);
    time(&start_time);
    
    while (recording && frame_count < (fps * duration)) {
        time(&current_time);
        if (current_time - start_time >= duration) {
            break;
        }
        
        // Copy framebuffer data
        fwrite(framebuffer, screensize, 1, output);
        frame_count++;
        
        if (frame_count % fps == 0) {
            printf("Recorded %d seconds (%d frames)\n", frame_count / fps, frame_count);
        }
        
        usleep(sleep_time);
    }
    
    fclose(output);
    munmap(framebuffer, screensize);
    close(fb_fd);
    
    printf("Recording completed: %s (%d frames)\n", filename, frame_count);
    
    return 0;
}

void save_single_screenshot_bmp(const char *filename) {
    Display *display;
    Window root;
    XImage *image;
    int screen;
    unsigned int width, height;
    FILE *output;
    
    display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Error: Cannot open X11 display for screenshot\n");
        return;
    }
    
    screen = DefaultScreen(display);
    root = RootWindow(display, screen);
    width = DisplayWidth(display, screen);
    height = DisplayHeight(display, screen);
    
    image = XGetImage(display, root, 0, 0, width, height, AllPlanes, ZPixmap);
    if (!image) {
        // Try alternative method with different parameters
        image = XGetImage(display, root, 0, 0, width, height, ~0L, ZPixmap);
        if (!image) {
            fprintf(stderr, "Error: Cannot capture screenshot\n");
            XCloseDisplay(display);
            return;
        }
    }
    
    output = fopen(filename, "wb");
    if (!output) {
        fprintf(stderr, "Error: Cannot create screenshot file\n");
        XDestroyImage(image);
        XCloseDisplay(display);
        return;
    }
    
    // Write BMP header
    write_bmp_header(output, width, height);
    
    // Write pixel data (BMP format is bottom-up)
    for (int y = height - 1; y >= 0; y--) {
        for (unsigned int x = 0; x < width; x++) {
            unsigned long pixel = XGetPixel(image, x, y);
            
            unsigned char b = pixel & 0xFF;
            unsigned char g = (pixel >> 8) & 0xFF;
            unsigned char r = (pixel >> 16) & 0xFF;
            
            fwrite(&b, 1, 1, output);
            fwrite(&g, 1, 1, output);
            fwrite(&r, 1, 1, output);
        }
        
        // BMP rows must be padded to 4-byte boundary
        int padding = (4 - (width * 3) % 4) % 4;
        for (int p = 0; p < padding; p++) {
            fputc(0, output);
        }
    }
    
    fclose(output);
    XDestroyImage(image);
    XCloseDisplay(display);
    
    printf("Screenshot saved: %s (%dx%d)\n", filename, width, height);
}

int main(int argc, char *argv[]) {
    int fps = DEFAULT_FPS;
    int duration = DEFAULT_DURATION;
    int screenshot_mode = 0;
    char filename_prefix[256] = "screenrec";
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--screenshot") == 0) {
            screenshot_mode = 1;
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--fps") == 0) {
            if (i + 1 < argc) {
                fps = atoi(argv[++i]);
                if (fps <= 0 || fps > 60) fps = DEFAULT_FPS;
            }
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--duration") == 0) {
            if (i + 1 < argc) {
                duration = atoi(argv[++i]);
                if (duration <= 0) duration = DEFAULT_DURATION;
            }
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                strncpy(filename_prefix, argv[++i], sizeof(filename_prefix) - 1);
                filename_prefix[sizeof(filename_prefix) - 1] = '\0';
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Screen Recorder Usage:\n");
            printf("  %s [options]\n\n", argv[0]);
            printf("Options:\n");
            printf("  -s, --screenshot     Take a single screenshot (BMP format)\n");
            printf("  -f, --fps <n>        Recording frame rate (default: %d)\n", DEFAULT_FPS);
            printf("  -d, --duration <n>   Recording duration in seconds (default: %d)\n", DEFAULT_DURATION);
            printf("  -o, --output <name>  Output filename prefix (default: screenrec)\n");
            printf("  -h, --help           Show this help\n\n");
            printf("Examples:\n");
            printf("  %s                           # Record 30s at 10fps\n", argv[0]);
            printf("  %s -f 15 -d 60               # Record 60s at 15fps\n", argv[0]);
            printf("  %s -s -o screenshot          # Take single screenshot\n", argv[0]);
            return 0;
        }
    }
    
    // Set up signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    if (screenshot_mode) {
        // Single screenshot mode
        char screenshot_name[256];
        time_t rawtime;
        struct tm *timeinfo;
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        
        snprintf(screenshot_name, sizeof(screenshot_name), "%s_%04d%02d%02d_%02d%02d%02d.bmp",
                 filename_prefix,
                 timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                 timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
        
        save_single_screenshot_bmp(screenshot_name);
        return 0;
    }
    
    printf("Screen Recorder Starting...\n");
    printf("Press Ctrl+C to stop early\n\n");
    
    // Try X11 first, fall back to framebuffer
    if (getenv("DISPLAY") != NULL) {
        printf("Using X11 method...\n");
        if (capture_x11_screen(filename_prefix, fps, duration) == 0) {
            return 0;
        }
        printf("X11 capture failed, trying framebuffer...\n");
    }
    
    printf("Using framebuffer method...\n");
    if (capture_framebuffer_screen(filename_prefix, fps, duration) == 0) {
        return 0;
    }
    
    fprintf(stderr, "Error: All screen capture methods failed\n");
    fprintf(stderr, "Make sure you have access to X11 display or /dev/fb0\n");
    
    return 1;
}
#else
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    fprintf(stderr, "Error: This module is Linux-only\n");
    return 1;
}
#endif