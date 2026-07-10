/*
 * proc_mem.h — process memory reading via pread on /proc/PID/mem
 *
 * Spec section 4.  Direct pread() calls, no shell subprocesses.
 * Handles ESRCH (process gone), EPERM (no access), EIO gracefully.
 */

#ifndef PROC_MEM_H
#define PROC_MEM_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

/* Forward declaration — catalog_db defined in catalog.h, but we
   avoid including it here to keep module boundaries clean. */
struct catalog_db;

/* Open /proc/PID/mem for reading.  Returns fd >= 0 or -1 on error. */
int  proc_mem_open(pid_t pid);

/* Read N bytes at address into buf.  Returns bytes read or -1 on error.
   Loops on partial reads and EINTR. */
int  proc_mem_read(int fd, void *buf, size_t len, uint32_t addr);

/* Read a little-endian u32 at address.  Returns 0 on success, -1 on error. */
int  proc_mem_read_u32le(int fd, uint32_t addr, uint32_t *out);

/* Decode a u32 from a hex string (8 hex chars, little-endian).
   Returns 0 on success, -1 on malformed input. */
int  proc_mem_u32le_from_hex(const char *hex8, uint32_t *out);

/* Check if memory at addr contains the given byte pattern.
   Returns true if pattern found, false otherwise. */
bool proc_mem_contains(int fd, uint32_t addr, size_t count,
                       const void *pattern, size_t pattern_len);

/* Check if memory at addr contains any line from the album patterns.
   cat_ptr points to a catalog_db with album_patterns populated.
   Returns true if any album pattern is found in the memory range. */
bool proc_mem_contains_catalog_album(int fd, uint32_t addr, size_t count,
                                     const struct catalog_db *cat);

/* Find first catalog path that appears in memory at addr.
   On success, writes the path to out_path and returns 0.
   Returns -1 if no match found. */
int  proc_mem_first_catalog_path(int fd, uint32_t addr, size_t count,
                                const struct catalog_db *cat,
                                char *out_path, size_t out_len);

/* Close the proc mem fd. */
void proc_mem_close(int fd);

#endif /* PROC_MEM_H */