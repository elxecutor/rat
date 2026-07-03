#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#ifdef __linux__
#include <linux/videodev2.h>
#endif
#include <signal.h>
#include <time.h>
#include <errno.h>

#ifdef __linux__
#define CAPTURE_COUNT 100
#define DEFAULT_DEVICE "/dev/video0"

struct buffer {
    void *start;
    size_t length;
};

volatile int capturing = 1;

void signal_handler(int sig) {
    capturing = 0;
}

int xioctl(int fd, int request, void *arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (-1 == r && EINTR == errno);
    return r;
}

int main(int argc, char *argv[]) {
    int fd;
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;
    struct buffer *buffers;
    unsigned int n_buffers;
    char *device = DEFAULT_DEVICE;
    char filename[256];
    FILE *output_file;
    time_t start_time;
    int frame_count = 0;
    
    // Parse command line arguments
    if (argc > 1) {
        device = argv[1];
    }
    
    // Generate filename with timestamp
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    snprintf(filename, sizeof(filename), "webcam_%04d%02d%02d_%02d%02d%02d.yuv",
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    
    // Open video device
    fd = open(device, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "Error: Cannot open video device %s\n", device);
        return 1;
    }
    
    // Query device capabilities
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        fprintf(stderr, "Error: Device does not support V4L2\n");
        close(fd);
        return 1;
    }
    
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "Error: Device does not support video capture\n");
        close(fd);
        return 1;
    }
    
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "Error: Device does not support streaming\n");
        close(fd);
        return 1;
    }
    
    printf("Device: %s\n", cap.card);
    printf("Driver: %s\n", cap.driver);
    printf("Version: %u.%u.%u\n", 
           (cap.version >> 16) & 0xFF,
           (cap.version >> 8) & 0xFF,
           cap.version & 0xFF);
    
    // Set video format
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
    
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        fprintf(stderr, "Error: Cannot set video format\n");
        close(fd);
        return 1;
    }
    
    printf("Video format: %dx%d\n", fmt.fmt.pix.width, fmt.fmt.pix.height);
    
    // Request buffers
    memset(&req, 0, sizeof(req));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        fprintf(stderr, "Error: Cannot request buffers\n");
        close(fd);
        return 1;
    }
    
    if (req.count < 2) {
        fprintf(stderr, "Error: Insufficient buffer memory\n");
        close(fd);
        return 1;
    }
    
    // Allocate buffers
    buffers = calloc(req.count, sizeof(*buffers));
    if (!buffers) {
        fprintf(stderr, "Error: Cannot allocate buffer memory\n");
        close(fd);
        return 1;
    }
    
    for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = n_buffers;
        
        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            fprintf(stderr, "Error: Cannot query buffer %d\n", n_buffers);
            free(buffers);
            close(fd);
            return 1;
        }
        
        buffers[n_buffers].length = buf.length;
        buffers[n_buffers].start = mmap(NULL, buf.length,
                                       PROT_READ | PROT_WRITE,
                                       MAP_SHARED, fd, buf.m.offset);
        
        if (MAP_FAILED == buffers[n_buffers].start) {
            fprintf(stderr, "Error: Cannot map buffer %d\n", n_buffers);
            free(buffers);
            close(fd);
            return 1;
        }
    }
    
    // Queue buffers
    for (unsigned int i = 0; i < n_buffers; ++i) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            fprintf(stderr, "Error: Cannot queue buffer %d\n", i);
            free(buffers);
            close(fd);
            return 1;
        }
    }
    
    // Start streaming
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        fprintf(stderr, "Error: Cannot start streaming\n");
        free(buffers);
        close(fd);
        return 1;
    }
    
    // Open output file
    output_file = fopen(filename, "wb");
    if (!output_file) {
        fprintf(stderr, "Error: Cannot create output file\n");
        free(buffers);
        close(fd);
        return 1;
    }
    
    // Set up signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Capturing webcam to %s...\n", filename);
    printf("Press Ctrl+C to stop\n");
    
    time(&start_time);
    
    // Capture loop
    while (capturing && frame_count < CAPTURE_COUNT) {
        fd_set fds;
        struct timeval tv;
        int r;
        
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        
        r = select(fd + 1, &fds, NULL, NULL, &tv);
        
        if (-1 == r) {
            if (EINTR == errno) continue;
            fprintf(stderr, "Error: select() failed\n");
            break;
        }
        
        if (0 == r) {
            fprintf(stderr, "Error: select() timeout\n");
            break;
        }
        
        // Dequeue buffer
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        
        if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            switch (errno) {
                case EAGAIN:
                    continue;
                case EIO:
                    // Could ignore EIO, see spec
                default:
                    fprintf(stderr, "Error: Cannot dequeue buffer\n");
                    break;
            }
            break;
        }
        
        // Write frame data
        fwrite(buffers[buf.index].start, buf.bytesused, 1, output_file);
        frame_count++;
        
        if (frame_count % 10 == 0) {
            printf("Captured %d frames\n", frame_count);
        }
        
        // Requeue buffer
        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            fprintf(stderr, "Error: Cannot requeue buffer\n");
            break;
        }
    }
    
    // Stop streaming
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(fd, VIDIOC_STREAMOFF, &type);
    
    // Cleanup
    for (unsigned int i = 0; i < n_buffers; ++i) {
        munmap(buffers[i].start, buffers[i].length);
    }
    
    free(buffers);
    fclose(output_file);
    close(fd);
    
    printf("Capture completed: %s (%d frames)\n", filename, frame_count);
    printf("To convert to video: ffmpeg -f rawvideo -pix_fmt yuyv422 -s 640x480 -r 25 -i %s output.mp4\n", filename);
    
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