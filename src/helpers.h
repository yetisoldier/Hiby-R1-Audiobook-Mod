/*
 * helpers.h — external helper subprocess management
 *
 * Spec section 4.3.  Fork/exec helpers with timeout, capture stdout,
 * kill on timeout.
 */

#ifndef HELPERS_H
#define HELPERS_H

#include <stdint.h>
#include <sys/types.h>

/* Result of a helper invocation */
typedef struct {
    int    exit_code;       /* -1 if killed by timeout */
    int    timed_out;       /* 1 if killed due to timeout */
    char   stdout_buf[4096];/* captured stdout (null-terminated) */
    size_t stdout_len;      /* actual bytes captured */
} helper_result;

/* Run a helper binary with the given args and timeout.
 *
 * argv[0] is the path to the helper binary.
 * timeout_seconds: 0 = no timeout.
 *
 * Returns: exit code (0 = success), -1 on fork/exec failure,
 *          -2 if timed out (result.timed_out = 1).
 */
int helpers_run(const char *argv[], int timeout_seconds,
                helper_result *result);

/* High-level helper invocations (spec section 4.3) */

/* Seek helper: seek to a position in the player.
 * Returns helper exit code or -2 on timeout. */
int helpers_seek(uint32_t seconds, uint16_t verify_delay_ms,
                 uint8_t verify_tolerance, const char *helper_path,
                 int timeout_seconds);

/* Memscan helper: find book root in player memory.
 * Returns helper exit code or -2 on timeout.  On success, out_root is filled. */
int helpers_memscan_root(pid_t pid, const char *memscan_path,
                         const char *catalog_books_path,
                         char *out_root, size_t out_len,
                         int timeout_seconds);

/* Direct-open helper: trigger direct track open.
 * Returns helper exit code or -2 on timeout. */
int helpers_direct_open(pid_t pid, uint32_t row_index,
                        uint32_t probe_addr, uint32_t scratch_addr,
                        uint32_t timeout_ms, uint32_t arm_delay_us,
                        const char *direct_open_path,
                        int timeout_seconds);

#endif /* HELPERS_H */