// v4l2_capture.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <linux/videodev2.h>

#define DEVICE       "/dev/video0"
#define WIDTH        1280
#define HEIGHT       720
#define NUM_BUFFERS  4
#define NUM_FRAMES   10

struct buffer { void *start; size_t length; };

static int xioctl(int fd, unsigned long req, void *arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r;
}

int main(void) {
    int fd = open(DEVICE, O_RDWR);
    if (fd < 0) { perror("open " DEVICE); return 1; }

    struct v4l2_capability cap;
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) { perror("QUERYCAP"); return 1; }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "Устройство не поддерживает видеозахват\n"); return 1;
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "Устройство не поддерживает streaming\n"); return 1;
    }

    struct v4l2_format fmt = {0};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = WIDTH;
    fmt.fmt.pix.height      = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) { perror("S_FMT"); return 1; }

    printf("Формат: %ux%u, fourcc=%.4s, sizeimage=%u\n",
           fmt.fmt.pix.width, fmt.fmt.pix.height,
           (char *)&fmt.fmt.pix.pixelformat, fmt.fmt.pix.sizeimage);

    struct v4l2_requestbuffers req = {0};
    req.count  = NUM_BUFFERS;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) { perror("REQBUFS"); return 1; }

    struct buffer *bufs = calloc(req.count, sizeof(*bufs));
    if (!bufs) { perror("calloc"); return 1; }

    for (unsigned i = 0; i < req.count; i++) {
        struct v4l2_buffer b = {0};
        b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index  = i;
        if (xioctl(fd, VIDIOC_QUERYBUF, &b) < 0) { perror("QUERYBUF"); return 1; }
        bufs[i].length = b.length;
        bufs[i].start  = mmap(NULL, b.length, PROT_READ | PROT_WRITE,
                              MAP_SHARED, fd, b.m.offset);
        if (bufs[i].start == MAP_FAILED) { perror("mmap"); return 1; }
    }

    for (unsigned i = 0; i < req.count; i++) {
        struct v4l2_buffer b = {0};
        b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index  = i;
        if (xioctl(fd, VIDIOC_QBUF, &b) < 0) { perror("QBUF"); return 1; }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) { perror("STREAMON"); return 1; }
    printf("Захват запущен, сохраняю %d кадров...\n", NUM_FRAMES);

    for (int n = 0; n < NUM_FRAMES; n++) {
        fd_set fds;
        FD_ZERO(&fds); FD_SET(fd, &fds);
        struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
        int r = select(fd + 1, &fds, NULL, NULL, &tv);
        if (r <= 0) { fprintf(stderr, "select timeout/err\n"); break; }

        struct v4l2_buffer b = {0};
        b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd, VIDIOC_DQBUF, &b) < 0) { perror("DQBUF"); break; }

        char name[64];
        snprintf(name, sizeof(name), "frame_%02d.jpg", n);
        FILE *f = fopen(name, "wb");
        if (f) {
            fwrite(bufs[b.index].start, 1, b.bytesused, f);
            fclose(f);
            printf("  %s (%u байт)\n", name, b.bytesused);
        }

        if (xioctl(fd, VIDIOC_QBUF, &b) < 0) { perror("QBUF"); break; }
    }

    xioctl(fd, VIDIOC_STREAMOFF, &type);
    for (unsigned i = 0; i < req.count; i++) munmap(bufs[i].start, bufs[i].length);
    free(bufs);
    close(fd);
    return 0;
}