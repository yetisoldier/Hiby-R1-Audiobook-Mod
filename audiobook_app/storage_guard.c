#include "storage_guard.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define SD_PLATFORM_CONTROL \
    "/sys/devices/platform/md_ingenic,mmc.1/power/control"
#define SD_HOST_CONTROL \
    "/sys/devices/platform/md_ingenic,mmc.1/mmc_host/mmc1/power/control"
#define SD_CARD_CONTROL \
    "/sys/devices/platform/md_ingenic,mmc.1/mmc_host/mmc1/mmc1:0001/power/control"
#define SD_CARD_STATUS \
    "/sys/devices/platform/md_ingenic,mmc.1/mmc_host/mmc1/mmc1:0001/power/runtime_status"

typedef struct {
    const char *path;
    char original[8];
    int changed;
} control_entry_t;

static control_entry_t g_controls[] = {
    { SD_PLATFORM_CONTROL, "", 0 },
    { SD_HOST_CONTROL, "", 0 },
    { SD_CARD_CONTROL, "", 0 },
};
static int g_acquired;
static int g_bad_status_checks;
static int g_worker_missing_checks;

extern int get_log_fd(void);

static void sg_log(const char *fmt, ...) {
    char body[224];
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    int n = snprintf(line, sizeof(line), "[storage] %s\n", body);
    if (n <= 0) return;
    if ((size_t)n > sizeof(line)) n = (int)sizeof(line);
    int fd = get_log_fd();
    if (fd >= 0) (void)write(fd, line, (size_t)n);
}

static int read_text(const char *path, char *buf, size_t size) {
    if (!path || !buf || size < 2) return -1;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, size - 1);
    int saved = errno;
    close(fd);
    errno = saved;
    if (n <= 0) return -1;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'
                     || buf[n - 1] == ' ' || buf[n - 1] == '\t'))
        n--;
    buf[n] = '\0';
    return 0;
}

static int write_text(const char *path, const char *value) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    size_t len = strlen(value);
    ssize_t n = write(fd, value, len);
    int saved = errno;
    close(fd);
    errno = saved;
    return n == (ssize_t)len ? 0 : -1;
}

int storage_guard_acquire(void) {
    if (g_acquired) return 0;
    int held = 0;
    for (size_t i = 0; i < sizeof(g_controls) / sizeof(g_controls[0]); i++) {
        control_entry_t *entry = &g_controls[i];
        entry->changed = 0;
        entry->original[0] = '\0';
        if (read_text(entry->path, entry->original,
                      sizeof(entry->original)) != 0) {
            sg_log("control read failed path=%s errno=%d", entry->path, errno);
            continue;
        }
        if (strcmp(entry->original, "on") == 0) {
            held++;
            continue;
        }
        if (write_text(entry->path, "on") == 0) {
            entry->changed = 1;
            held++;
        } else {
            sg_log("control hold failed path=%s errno=%d", entry->path, errno);
        }
    }
    g_acquired = 1;
    g_bad_status_checks = 0;
    g_worker_missing_checks = 0;

    char status[24] = "unknown";
    (void)read_text(SD_CARD_STATUS, status, sizeof(status));
    sg_log("hold acquired=%d/%d status=%s", held,
           (int)(sizeof(g_controls) / sizeof(g_controls[0])), status);
    return held == (int)(sizeof(g_controls) / sizeof(g_controls[0])) ? 0 : -1;
}

void storage_guard_release(void) {
    if (!g_acquired) return;
    int restored = 0;
    for (int i = (int)(sizeof(g_controls) / sizeof(g_controls[0])) - 1;
         i >= 0; i--) {
        control_entry_t *entry = &g_controls[i];
        if (!entry->changed) continue;
        const char *value = entry->original[0] ? entry->original : "auto";
        if (write_text(entry->path, value) == 0) {
            restored++;
        } else {
            sg_log("control restore failed path=%s errno=%d",
                   entry->path, errno);
        }
        entry->changed = 0;
    }
    g_acquired = 0;
    sg_log("hold released restored=%d", restored);
}

static int mmc_worker_present(void) {
    DIR *proc = opendir("/proc");
    if (!proc) return 1; /* Do not report a false failure if procfs is busy. */
    struct dirent *de;
    int found = 0;
    while ((de = readdir(proc)) != NULL) {
        if (!isdigit((unsigned char)de->d_name[0])) continue;
        char path[64], comm[32];
        snprintf(path, sizeof(path), "/proc/%s/comm", de->d_name);
        if (read_text(path, comm, sizeof(comm)) == 0
            && strcmp(comm, "mmcqd/1") == 0) {
            found = 1;
            break;
        }
    }
    closedir(proc);
    return found;
}

void storage_guard_poll(void) {
    if (!g_acquired) return;

    if (mmc_worker_present()) {
        g_worker_missing_checks = 0;
    } else if (++g_worker_missing_checks == 2) {
        sg_log("diagnostic: mmcqd/1 missing on two checks");
    }

    char status[24];
    if (read_text(SD_CARD_STATUS, status, sizeof(status)) != 0) return;
    if (strcmp(status, "active") == 0) {
        g_bad_status_checks = 0;
    } else if (++g_bad_status_checks == 2) {
        sg_log("diagnostic: SD runtime_status=%s on two checks", status);
    }
}
