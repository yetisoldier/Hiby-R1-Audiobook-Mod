#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PLAY_OPEN_JUMP 0x0049e200UL
#define PLAY_OPEN_CONTINUE 0x0049e208UL
#define PLAY_OPEN_ORIG0 0x27bdf320UL
#define PLAY_OPEN_ORIG1 0xafb70cd4UL
#define PLAY_OPEN_MAGIC 0xc0de49e2UL
#define PLAY_OPEN_SCRATCH_DEFAULT 0x008e4400UL
#define PLAY_OPEN_PROBE_DEFAULT 0x00760708UL
#define PROBE_CLEAR_BYTES 0x120U
#define SCRATCH_BYTES 0x120U
#define MAX_ROW_INDEX 9999UL

enum {
    R_ZERO = 0,
    R_A0 = 4,
    R_A1 = 5,
    R_A2 = 6,
    R_A3 = 7,
    R_T0 = 8,
    R_T1 = 9,
    R_T2 = 10,
    R_T3 = 11,
    R_S0 = 16,
    R_S1 = 17,
    R_S2 = 18,
    R_S3 = 19,
    R_S4 = 20,
    R_S5 = 21,
    R_S6 = 22,
    R_S7 = 23,
    R_GP = 28,
    R_SP = 29,
    R_RA = 31,
};

struct block {
    unsigned long addr;
    const uint32_t *words;
    size_t word_count;
    const char *label;
};

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s --pid PID --row-index ZERO_BASED_ROW [options]\n"
            "\n"
            "Temporarily overrides the next hiby_player shared media-open row index.\n"
            "Options:\n"
            "  --probe-addr ADDR    executable code cave, default 0x%lx\n"
            "  --scratch-addr ADDR  writable scratch range, default 0x%lx\n"
            "  --timeout-ms MS      wait before restoring, default 6000\n",
            argv0, PLAY_OPEN_PROBE_DEFAULT, PLAY_OPEN_SCRATCH_DEFAULT);
}

static unsigned long parse_ulong(const char *text) {
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 0);
    if (errno || !end || *end) {
        fprintf(stderr, "invalid number: %s\n", text);
        exit(2);
    }
    return value;
}

static uint32_t ins_lui(int rt, uint16_t imm) {
    return (15u << 26) | ((uint32_t)rt << 16) | imm;
}

static uint32_t ins_ori(int rt, int rs, uint16_t imm) {
    return (13u << 26) | ((uint32_t)rs << 21) | ((uint32_t)rt << 16) | imm;
}

static uint32_t ins_sw(int rt, int base, int16_t off) {
    return (43u << 26) | ((uint32_t)base << 21) | ((uint32_t)rt << 16) |
           ((uint16_t)off);
}

static uint32_t ins_lw(int rt, int base, int16_t off) {
    return (35u << 26) | ((uint32_t)base << 21) | ((uint32_t)rt << 16) |
           ((uint16_t)off);
}

static uint32_t ins_addiu(int rt, int rs, int16_t imm) {
    return (9u << 26) | ((uint32_t)rs << 21) | ((uint32_t)rt << 16) |
           ((uint16_t)imm);
}

static uint32_t ins_or(int rd, int rs, int rt) {
    return ((uint32_t)rs << 21) | ((uint32_t)rt << 16) | ((uint32_t)rd << 11) |
           0x25u;
}

static uint32_t ins_j(unsigned long addr) {
    return (2u << 26) | ((uint32_t)(addr >> 2) & 0x03ffffffu);
}

static void emit_li(uint32_t *code, size_t *n, int reg, unsigned long value) {
    code[(*n)++] = ins_lui(reg, (uint16_t)((value >> 16) & 0xffffu));
    code[(*n)++] = ins_ori(reg, reg, (uint16_t)(value & 0xffffu));
}

static size_t build_probe(uint32_t *code, unsigned long scratch_addr, unsigned long row_index) {
    size_t n = 0;
    emit_li(code, &n, R_T0, scratch_addr);
    emit_li(code, &n, R_T1, PLAY_OPEN_MAGIC);
    code[n++] = ins_sw(R_T1, R_T0, 0);
    code[n++] = ins_sw(R_RA, R_T0, 4);
    code[n++] = ins_sw(R_A0, R_T0, 8);
    code[n++] = ins_sw(R_A1, R_T0, 12);
    code[n++] = ins_sw(R_A2, R_T0, 16);
    code[n++] = ins_sw(R_A3, R_T0, 20);
    code[n++] = ins_sw(R_SP, R_T0, 24);
    code[n++] = ins_sw(R_S0, R_T0, 28);
    code[n++] = ins_sw(R_S1, R_T0, 32);
    code[n++] = ins_sw(R_S2, R_T0, 36);
    code[n++] = ins_sw(R_S3, R_T0, 40);
    code[n++] = ins_sw(R_S4, R_T0, 44);
    code[n++] = ins_sw(R_S5, R_T0, 48);
    code[n++] = ins_sw(R_S6, R_T0, 52);
    code[n++] = ins_sw(R_S7, R_T0, 56);
    code[n++] = ins_sw(R_GP, R_T0, 60);
    code[n++] = ins_lw(R_T2, R_T0, 64);
    code[n++] = ins_addiu(R_T2, R_T2, 1);
    code[n++] = ins_sw(R_T2, R_T0, 64);
    emit_li(code, &n, R_T3, row_index);
    code[n++] = ins_sw(R_T3, R_T0, 68);
    code[n++] = ins_or(R_A2, R_T3, R_ZERO);
    code[n++] = ins_addiu(R_SP, R_SP, -3296);
    code[n++] = ins_sw(R_S7, R_SP, 3284);
    code[n++] = ins_j(PLAY_OPEN_CONTINUE);
    code[n++] = 0;
    return n;
}

static int read_all_pread(int fd, unsigned char *buf, size_t len, unsigned long addr) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = pread(fd, buf + done, len - done, (off_t)(addr + done));
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        done += (size_t)n;
    }
    return 0;
}

static int read_mem(int pid, unsigned long addr, void *buf, size_t len) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/mem", pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int rc = read_all_pread(fd, (unsigned char *)buf, len, addr);
    close(fd);
    return rc;
}

static int range_is_zero_or_magic(int pid, unsigned long addr, size_t len,
                                  uint32_t magic, int allow_magic) {
    unsigned char buf[SCRATCH_BYTES];
    if (len > sizeof(buf)) return 0;
    if (read_mem(pid, addr, buf, len) != 0) return 0;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] != 0) {
            uint32_t found = 0;
            if (allow_magic && len >= 4) memcpy(&found, buf, sizeof(found));
            return allow_magic && found == magic;
        }
    }
    return 1;
}

static int range_is_zero(int pid, unsigned long addr, size_t len) {
    unsigned char buf[PROBE_CLEAR_BYTES];
    if (len > sizeof(buf)) return 0;
    if (read_mem(pid, addr, buf, len) != 0) return 0;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] != 0) return 0;
    }
    return 1;
}

static int maps_contains_range(int pid, unsigned long addr, size_t len,
                               int require_writable, int require_executable_player) {
    char maps_path[64];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    FILE *fp = fopen(maps_path, "r");
    if (!fp) return 0;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        unsigned long start = 0, end = 0;
        char perms[8] = {0};
        char path[256] = {0};
        int matched = sscanf(line, "%lx-%lx %7s %*s %*s %*s %255[^\n]",
                             &start, &end, perms, path);
        if (matched < 3) continue;
        if (start <= addr && addr + len <= end) {
            fclose(fp);
            if (require_writable && !strchr(perms, 'w')) return 0;
            if (require_executable_player) {
                if (!strchr(perms, 'x')) return 0;
                if (!strstr(path, "hiby_player")) return 0;
            }
            return 1;
        }
    }

    fclose(fp);
    return 0;
}

static int wait_for_attach(int pid) {
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return -1;
    }
    return 0;
}

static int ptrace_write_blocks(int pid, const struct block *blocks, size_t block_count) {
    if (ptrace(PTRACE_ATTACH, pid, 0, 0) != 0) {
        fprintf(stderr, "ptrace attach failed: %s\n", strerror(errno));
        return -1;
    }
    if (wait_for_attach(pid) != 0) {
        fprintf(stderr, "waitpid after attach failed: %s\n", strerror(errno));
        ptrace(PTRACE_DETACH, pid, 0, 0);
        return -1;
    }

    int failed = 0;
    for (size_t b = 0; b < block_count && !failed; b++) {
        for (size_t i = 0; i < blocks[b].word_count; i++) {
            unsigned long addr = blocks[b].addr + (unsigned long)(i * 4);
            uint32_t word = blocks[b].words[i];
            if (ptrace(PTRACE_POKETEXT, pid, (void *)addr, (void *)(uintptr_t)word) != 0) {
                fprintf(stderr, "ptrace poke failed for %s at 0x%lx: %s\n",
                        blocks[b].label, addr, strerror(errno));
                failed = 1;
                break;
            }
        }
    }

    if (ptrace(PTRACE_DETACH, pid, 0, 0) != 0) {
        fprintf(stderr, "ptrace detach failed: %s\n", strerror(errno));
        return -1;
    }
    return failed ? -1 : 0;
}

static int play_open_state(int pid, unsigned long probe_addr, int *is_original, int *is_ours) {
    uint32_t actual[2] = {0, 0};
    uint32_t patch[2] = {ins_j(probe_addr), 0};
    if (read_mem(pid, PLAY_OPEN_JUMP, actual, sizeof(actual)) != 0) return -1;
    *is_original = actual[0] == PLAY_OPEN_ORIG0 && actual[1] == PLAY_OPEN_ORIG1;
    *is_ours = actual[0] == patch[0] && actual[1] == patch[1];
    return 0;
}

static int restore_patch(int pid, unsigned long probe_addr) {
    int is_original = 0;
    int is_ours = 0;
    if (play_open_state(pid, probe_addr, &is_original, &is_ours) != 0) {
        fprintf(stderr, "could not read play-open state before restore\n");
        return -1;
    }
    if (!is_original && !is_ours) {
        fprintf(stderr, "play-open function is in an unknown state; refusing restore\n");
        return -1;
    }

    uint32_t orig[2] = {PLAY_OPEN_ORIG0, PLAY_OPEN_ORIG1};
    uint32_t zeros[PROBE_CLEAR_BYTES / 4];
    memset(zeros, 0, sizeof(zeros));
    struct block blocks[2];
    size_t count = 0;
    if (is_ours) {
        blocks[count++] = (struct block){PLAY_OPEN_JUMP, orig, 2, "play-open-original"};
    }
    blocks[count++] = (struct block){probe_addr, zeros, sizeof(zeros) / sizeof(zeros[0]), "probe-clear"};
    if (ptrace_write_blocks(pid, blocks, count) != 0) return -1;
    return 0;
}

static int arm_patch(int pid, unsigned long probe_addr, unsigned long scratch_addr,
                     unsigned long row_index, size_t *probe_words_out) {
    if (!maps_contains_range(pid, probe_addr, PROBE_CLEAR_BYTES, 0, 1)) {
        fprintf(stderr, "probe range 0x%lx-0x%lx is not executable hiby_player memory\n",
                probe_addr, probe_addr + PROBE_CLEAR_BYTES);
        return -1;
    }
    if (!maps_contains_range(pid, scratch_addr, SCRATCH_BYTES, 1, 0)) {
        fprintf(stderr, "scratch range 0x%lx-0x%lx is not writable memory\n",
                scratch_addr, scratch_addr + SCRATCH_BYTES);
        return -1;
    }

    int is_original = 0;
    int is_ours = 0;
    if (play_open_state(pid, probe_addr, &is_original, &is_ours) != 0) {
        fprintf(stderr, "could not read play-open state before arm\n");
        return -1;
    }
    if (!is_original) {
        fprintf(stderr, "play-open function is not in stock state; refusing arm\n");
        return -1;
    }
    if (!range_is_zero(pid, probe_addr, PROBE_CLEAR_BYTES)) {
        fprintf(stderr, "probe range 0x%lx is not empty; refusing arm\n", probe_addr);
        return -1;
    }
    if (!range_is_zero_or_magic(pid, scratch_addr, SCRATCH_BYTES, PLAY_OPEN_MAGIC, 1)) {
        fprintf(stderr, "scratch range 0x%lx is not empty or stale direct-open data\n", scratch_addr);
        return -1;
    }

    uint32_t scratch_zeros[SCRATCH_BYTES / 4];
    uint32_t probe_zeros[PROBE_CLEAR_BYTES / 4];
    uint32_t probe_code[PROBE_CLEAR_BYTES / 4];
    uint32_t jump_patch[2] = {ins_j(probe_addr), 0};
    memset(scratch_zeros, 0, sizeof(scratch_zeros));
    memset(probe_zeros, 0, sizeof(probe_zeros));
    memset(probe_code, 0, sizeof(probe_code));
    size_t probe_words = build_probe(probe_code, scratch_addr, row_index);
    *probe_words_out = probe_words;

    struct block blocks[] = {
        {scratch_addr, scratch_zeros, sizeof(scratch_zeros) / sizeof(scratch_zeros[0]), "scratch-clear"},
        {probe_addr, probe_zeros, sizeof(probe_zeros) / sizeof(probe_zeros[0]), "probe-clear"},
        {probe_addr, probe_code, probe_words, "probe-code"},
        {PLAY_OPEN_JUMP, jump_patch, 2, "play-open-jump"},
    };
    return ptrace_write_blocks(pid, blocks, sizeof(blocks) / sizeof(blocks[0]));
}

static unsigned long monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec * 1000UL + (unsigned long)(ts.tv_nsec / 1000000UL);
}

static int scratch_called(int pid, unsigned long scratch_addr, uint32_t *count_out,
                          uint32_t *original_row_out, uint32_t *override_row_out) {
    uint32_t scratch[18];
    memset(scratch, 0, sizeof(scratch));
    if (read_mem(pid, scratch_addr, scratch, sizeof(scratch)) != 0) return 0;
    if (scratch[0] != PLAY_OPEN_MAGIC) return 0;
    *count_out = scratch[16];
    *original_row_out = scratch[4];
    *override_row_out = scratch[17];
    return scratch[16] > 0;
}

int main(int argc, char **argv) {
    int pid = 0;
    unsigned long row_index = (unsigned long)-1;
    unsigned long probe_addr = PLAY_OPEN_PROBE_DEFAULT;
    unsigned long scratch_addr = PLAY_OPEN_SCRATCH_DEFAULT;
    unsigned long timeout_ms = 6000;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
            pid = (int)parse_ulong(argv[++i]);
        } else if (strcmp(argv[i], "--row-index") == 0 && i + 1 < argc) {
            row_index = parse_ulong(argv[++i]);
        } else if (strcmp(argv[i], "--probe-addr") == 0 && i + 1 < argc) {
            probe_addr = parse_ulong(argv[++i]);
        } else if (strcmp(argv[i], "--scratch-addr") == 0 && i + 1 < argc) {
            scratch_addr = parse_ulong(argv[++i]);
        } else if (strcmp(argv[i], "--timeout-ms") == 0 && i + 1 < argc) {
            timeout_ms = parse_ulong(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (pid <= 0 || row_index == (unsigned long)-1 || row_index > MAX_ROW_INDEX ||
        timeout_ms == 0 || timeout_ms > 60000UL ||
        (probe_addr & 3UL) || (scratch_addr & 3UL)) {
        usage(argv[0]);
        return 2;
    }

    size_t probe_words = 0;
    if (arm_patch(pid, probe_addr, scratch_addr, row_index, &probe_words) != 0) {
        return 3;
    }
    fprintf(stderr, "direct-open armed pid=%d row_index=%lu probe=0x%lx scratch=0x%lx words=%lu\n",
            pid, row_index, probe_addr, scratch_addr, (unsigned long)probe_words);

    unsigned long deadline = monotonic_ms() + timeout_ms;
    uint32_t count = 0;
    uint32_t original_row = 0;
    uint32_t override_row = 0;
    int called = 0;
    while (monotonic_ms() < deadline) {
        if (scratch_called(pid, scratch_addr, &count, &original_row, &override_row)) {
            called = 1;
            break;
        }
        usleep(50000);
    }

    int restore_rc = restore_patch(pid, probe_addr);
    if (restore_rc != 0) {
        fprintf(stderr, "direct-open restore failed\n");
        return 5;
    }
    if (!called) {
        fprintf(stderr, "direct-open timed out after %lums; restored\n", timeout_ms);
        return 4;
    }

    fprintf(stderr, "direct-open called count=%u original_row=%u override_row=%u restored\n",
            count, original_row, override_row);
    return 0;
}
