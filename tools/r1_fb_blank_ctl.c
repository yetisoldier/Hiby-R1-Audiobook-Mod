#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 2 ||
        (strcmp(argv[1], "blank") != 0 && strcmp(argv[1], "unblank") != 0)) {
        fprintf(stderr, "usage: %s blank|unblank\n", argv[0]);
        return 2;
    }

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "open /dev/fb0: %s\n", strerror(errno));
        return 1;
    }

    int value = strcmp(argv[1], "blank") == 0
        ? FB_BLANK_POWERDOWN
        : FB_BLANK_UNBLANK;
    int rc = ioctl(fd, FBIOBLANK, (void *)(long)value);
    if (rc < 0)
        fprintf(stderr, "FBIOBLANK %d: %s\n", value, strerror(errno));
    close(fd);
    printf("FBIOBLANK value=%d rc=%d\n", value, rc);
    return rc < 0 ? 1 : 0;
}
