/* Capture the framebuffer page currently scanned out by the HiBy R1.
 *
 * Plain reads from /dev/fb0 return only page zero. The stock UI uses a
 * 480x1600 virtual framebuffer and pans between two 480x800 pages, so page
 * zero can be stale. This helper queries yoffset, mmaps the framebuffer, and
 * writes the visible RGB565 page to the requested file.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int write_all(int fd, const uint8_t *buf, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *output = argc > 1 ? argv[1] : "/tmp/r1-visible-fb.raw";
    int fb_fd = open("/dev/fb0", O_RDONLY);
    if (fb_fd < 0) {
        fprintf(stderr, "open /dev/fb0: %s\n", strerror(errno));
        return 1;
    }

    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) < 0 ||
        ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) < 0) {
        fprintf(stderr, "framebuffer ioctl: %s\n", strerror(errno));
        close(fb_fd);
        return 1;
    }

    size_t page_bytes = (size_t)fix.line_length * var.yres;
    size_t offset = (size_t)fix.line_length * var.yoffset;
    size_t map_bytes = fix.smem_len;
    if (offset + page_bytes > map_bytes) {
        fprintf(stderr, "invalid geometry: offset=%lu page=%lu map=%lu\n",
                (unsigned long)offset, (unsigned long)page_bytes,
                (unsigned long)map_bytes);
        close(fb_fd);
        return 1;
    }

    uint8_t *fb = mmap(NULL, map_bytes, PROT_READ, MAP_SHARED, fb_fd, 0);
    if (fb == MAP_FAILED) {
        fprintf(stderr, "mmap framebuffer: %s\n", strerror(errno));
        close(fb_fd);
        return 1;
    }

    int out_fd = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0 || write_all(out_fd, fb + offset, page_bytes) < 0) {
        fprintf(stderr, "write %s: %s\n", output, strerror(errno));
        if (out_fd >= 0) close(out_fd);
        munmap(fb, map_bytes);
        close(fb_fd);
        return 1;
    }

    close(out_fd);
    munmap(fb, map_bytes);
    close(fb_fd);
    printf("xres=%u yres=%u yres_virtual=%u yoffset=%u stride=%u bytes=%lu\n",
           var.xres, var.yres, var.yres_virtual, var.yoffset, fix.line_length,
           (unsigned long)page_bytes);
    return 0;
}
