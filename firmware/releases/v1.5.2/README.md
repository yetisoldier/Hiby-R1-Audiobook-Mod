# HiBy R1 Audiobook Mod v1.5.2

Hotfix release for the normal HiBy R1 only. This is based on stock HiBy R1 firmware 1.6 and is not intended for the R1 MIDI.

## Download

- File: `r1-audiobooks-1.6.16.2-audiobook.upt`
- Firmware marker: `1.6.16.2-audiobook`
- MD5: `80c0d7295c2d55575870c4d226e83be9`
- SHA256: `3109fea179b816dcdd4c1536b8973f527ef8f8b2d628942317f6b4ded62ca4c6`
- Installed verification: passed on the test R1 on 2026-06-17 after a new-card database update, with Audiobooks opening to the title list instead of `No music found`.

## What Changed Since v1.5.1

- Fixed the remaining new-SD-card scan case where Audiobooks could still show `No music found` after Music -> Update Database.
- The DB watcher now detects same-size stock DB rewrites that put audiobook rows back under their original genre tags.
- The DB helper now has a fast repair-needed check so normal playback timestamp churn can still be skipped safely.
- Large-card scan repair should feel less laggy because the firmware repairs the primary DB once, then copies the repaired DB to the mirror DB paths instead of fully reprocessing each mirror.
- The audiobook resume daemon now polls every 2 seconds during active audiobook playback instead of every 1 second, while keeping the 15-second resume save cadence.

All v1.5.0/v1.5.1 audiobook features are otherwise retained: Audiobooks launcher, title-list start, per-book resume, multipart resume, audiobook/music separation, Native DSD flag, Bluetooth SBC XQ, and unlocked USB DAC mode settings.

## Install

1. Download `r1-audiobooks-1.6.16.2-audiobook.upt`.
2. Rename it to exactly `r1.upt`.
3. Copy it to the root of the SD card.
4. Run the normal firmware update from the R1 UI.
5. After the update succeeds and the player reboots, delete or rename `r1.upt` on the SD card.
6. Go to Music and run `Update Database`.
7. Wait for the scan to complete, then give the watcher about a minute or reboot once before opening Audiobooks.

Recommended SD card folders:

```text
/Music
/Audiobooks
```

The genre tag does not need to be exactly `Audiobook`. Files under `/Audiobooks` are treated as audiobooks by folder location.

## Known Quirks

- The About screen may shorten the visible version suffix.
- Audiobook positions are saved only after at least 15 seconds of playback.
- There is still no audiobook search UI.
- This replaces the old text Books launcher flow.
- Keep a stock HiBy R1 1.6 firmware file handy in case you want to revert.
