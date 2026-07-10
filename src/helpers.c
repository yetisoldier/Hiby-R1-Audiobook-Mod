/*
 * helpers.c — external helper subprocess management
 *
 * Spec section 4.3.  Fork/exec helpers with timeout, capture stdout,
 * kill on timeout.  Uses fork/execvp/waitpid with alarm-based timeout.
 */

#include "helpers.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>

/* ── Signal-safe child management ─────────────────────────────────── */

static volatile sig_atomic_t alarm_fired = 0;
static pid_t current_child = 0;

static void alarm_handler(int sig) {
    (void)sig;
    alarm_fired = 1;
    if (current_child > 0) {
        kill(current_child, SIGKILL);
    }
}

/* ── Core run helper ─────────────────────────────────────────────── */

int helpers_run(const char *argv[], int timeout_seconds,
                helper_result *result) {
    if (!argv || !argv[0]) return -1;

    if (result) {
        memset(result, 0, sizeof(*result));
    }

    /* Create pipe for stdout capture */
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    /* Set up alarm for timeout */
    struct sigaction old_sa;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = alarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, &old_sa);

    alarm_fired = 0;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        sigaction(SIGALRM, &old_sa, NULL);
        return -1;
    }

    if (pid == 0) {
        /* Child: redirect stdout to pipe, exec helper */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        /* Close stderr to avoid noise */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execvp(argv[0], (char *const *)argv);
        /* If exec fails, exit with 127 */
        _exit(127);
    }

    /* Parent */
    close(pipefd[1]);
    current_child = pid;

    if (timeout_seconds > 0) {
        alarm(timeout_seconds);
    }

    /* Read stdout in a loop */
    char buf[4096];
    size_t total = 0;
    while (1) {
        ssize_t n = read(pipefd[0], buf + total, sizeof(buf) - total - 1);
        if (n > 0) {
            total += (size_t)n;
            if (total >= sizeof(buf) - 1) break;
            continue;
        }
        if (n == 0) break;
        if (errno == EINTR) {
            if (alarm_fired) break;
            continue;
        }
        break;
    }
    close(pipefd[0]);

    /* Wait for child */
    int status = 0;
    int wait_rc;
    while ((wait_rc = waitpid(pid, &status, 0)) < 0 && errno == EINTR) {
        if (alarm_fired) {
            /* Child should have been killed by signal handler */
            continue;
        }
    }

    /* Clear alarm */
    alarm(0);
    current_child = 0;
    sigaction(SIGALRM, &old_sa, NULL);

    /* Copy captured stdout to result */
    if (result) {
        size_t copy = total < sizeof(result->stdout_buf) - 1 ? total : sizeof(result->stdout_buf) - 1;
        memcpy(result->stdout_buf, buf, copy);
        result->stdout_buf[copy] = '\0';
        result->stdout_len = copy;
    }

    if (alarm_fired) {
        if (result) {
            result->timed_out = 1;
            result->exit_code = -1;
        }
        log_msg("helper timeout: %s (pid %d killed after %ds)",
                argv[0], (int)pid, timeout_seconds);
        return -2;
    }

    if (wait_rc < 0) {
        if (result) result->exit_code = -1;
        return -1;
    }

    int exit_code = 0;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_code = -1;
        if (result) result->exit_code = -1;
    }

    if (result) result->exit_code = exit_code;
    return exit_code;
}

/* ── High-level helper invocations ────────────────────────────────── */

int helpers_seek(uint32_t seconds, uint16_t verify_delay_ms,
                 uint8_t verify_tolerance, const char *helper_path,
                 int timeout_seconds) {
    char sec_str[16], delay_str[16], tol_str[16];
    snprintf(sec_str,   sizeof(sec_str),   "%u", seconds);
    snprintf(delay_str, sizeof(delay_str), "%u", verify_delay_ms);
    snprintf(tol_str,   sizeof(tol_str),   "%u", verify_tolerance);

    const char *argv[] = { helper_path, "--seek", sec_str,
                           "--verify-delay", delay_str,
                           "--verify-tolerance", tol_str, NULL };
    helper_result res;
    return helpers_run(argv, timeout_seconds, &res);
}

int helpers_memscan_root(pid_t pid, const char *memscan_path,
                         const char *catalog_books_path,
                         char *out_root, size_t out_len,
                         int timeout_seconds) {
    char pid_str[16];
    snprintf(pid_str, sizeof(pid_str), "%d", (int)pid);

    const char *argv[] = { memscan_path, "--pid", pid_str,
                           "--catalog-books", catalog_books_path, NULL };
    helper_result res;
    int rc = helpers_run(argv, timeout_seconds, &res);

    if (rc == 0 && out_root && out_len > 0) {
        /* Helper outputs the root path on stdout */
        size_t copy = res.stdout_len < out_len - 1 ? res.stdout_len : out_len - 1;
        memcpy(out_root, res.stdout_buf, copy);
        out_root[copy] = '\0';
        /* Trim trailing whitespace */
        char *end = out_root + copy;
        while (end > out_root && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ')) {
            end--;
            *end = '\0';
        }
    }
    return rc;
}

int helpers_direct_open(pid_t pid, uint32_t row_index,
                        uint32_t probe_addr, uint32_t scratch_addr,
                        uint32_t timeout_ms, uint32_t arm_delay_us,
                        const char *direct_open_path,
                        int timeout_seconds) {
    char pid_str[16], row_str[16], probe_str[16], scratch_str[16];
    char timeout_str[16], arm_str[16];
    snprintf(pid_str,     sizeof(pid_str),     "%d", (int)pid);
    snprintf(row_str,     sizeof(row_str),     "%u", row_index);
    snprintf(probe_str,   sizeof(probe_str),   "0x%x", probe_addr);
    snprintf(scratch_str, sizeof(scratch_str), "0x%x", scratch_addr);
    snprintf(timeout_str, sizeof(timeout_str), "%u", timeout_ms);
    snprintf(arm_str,     sizeof(arm_str),     "%u", arm_delay_us);

    const char *argv[] = { direct_open_path, "--pid", pid_str,
                           "--row", row_str,
                           "--probe-addr", probe_str,
                           "--scratch-addr", scratch_str,
                           "--timeout-ms", timeout_str,
                           "--arm-delay-us", arm_str, NULL };
    helper_result res;
    return helpers_run(argv, timeout_seconds, &res);
}