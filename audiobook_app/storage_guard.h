#ifndef AUDIOBOOK_STORAGE_GUARD_H
#define AUDIOBOOK_STORAGE_GUARD_H

/* Keep the removable SD path out of runtime autosuspend while the audiobook
 * UI owns it. This avoids the stock X1600 MMC driver's rapid suspend/resume
 * cycle without affecting the stock player after Audiobooks exits. */
int storage_guard_acquire(void);
void storage_guard_release(void);

/* Lightweight diagnostic check. It only reads procfs/sysfs and logs when the
 * MMC worker is missing or the card remains in a transitional PM state. */
void storage_guard_poll(void);

#endif
