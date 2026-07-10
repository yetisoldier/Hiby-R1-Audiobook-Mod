/*
 * log.h — structured logging with timestamps and size-based rotation
 *
 * Spec section 7.  Each line is prefixed with an ISO 8601 timestamp.
 * Rotation occurs when the file exceeds log_max_bytes.
 */

#ifndef LOG_H
#define LOG_H

#include <stdint.h>

void log_init(const char *path, uint32_t max_bytes);
void log_close(void);
void log_msg(const char *fmt, ...);
void log_rotate_if_needed(void);

/* stderr fallback for messages before log_init() */
void log_stderr(const char *fmt, ...);

#endif /* LOG_H */