# Hiby R1 v2.0.21 — Build + Flash Session (2026-07-22)

## Source commits on branch `codex/r1-hiby-modding-integration`

Branch HEAD: `c61904c stability: eliminate button/volume stalls (root cause fix)`

Commits in this version:
| Commit | Description |
|--------|------------|
| c61904c | Button/volume stall fix (tmpfs vol_save + persistent log fd) |
| 871eb5d | Folder-hierarchy drill-down to Folders view |
| 75bb811 | Screen-dark freeze fix + EVIOCGNAME non-consuming probe |
| 4c1ab16 | Now-Playing FF/RW skip (Prev -30s / Next +60s) |
| 11cf964 | BEGIN/COMMIT scan wrap + ROLLBACK on SQLITE_FULL |
| 5c305fd | ALSA hw_params diagnostic logging |

## Build

- **Hook .so**: `work/native-app/libaudiobook_hook.so` — 1,657,896B (includes debug dbopen logging)
- **.upt**: `work/audiobook-firmware/r1-audiobooks-2.0.21.upt` — 42,221,568 bytes
  - **md5**: `9841a1b5bc272fab0c23fe25e60fcf3a`
- **Build command**:
  ```
  tools/build_r1_audiobook_firmware.ps1 `
    -Rootfs work/original/rootfs.squashfs `
    -IncludeAudiobookNativeApp `
    -EnableBootAdb `
    -UnlockNativeDsd `
    -EnableBluetoothSbcXq `
    -UnlockUsbDacMode `
    -OutputUpt "work/audiobook-firmware/r1-audiobooks-2.0.21.upt"
  ```
- **Flags**: `-IncludeAudiobookNativeApp -EnableBootAdb -UnlockNativeDsd -EnableBluetoothSbcXq -UnlockUsbDacMode`

## Flash procedure (OTA recovery)

```bash
# Push .upt to SD card
MSYS_NO_PATHCONV=1 adb push work/audiobook-firmware/r1-audiobooks-2.0.21.upt /usr/data/mnt/sd_0/r1.upt

# Stage OTA marker (writes to mtd5 "ota" flag partition)
MSYS_NO_PATHCONV=1 adb shell 'sh /usr/bin/bootmode.sh Recovery'

# Reboot into recovery to apply
MSYS_NO_PATHCONV=1 adb shell 'reboot'
```

## Post-flash device state

| Item | Value |
|------|-------|
| hiby_player | Running (PID 926, system_main_thr) |
| hook .so loaded | Yes — 1,657,896B @ `/usr/lib/` |
| hook hash | `5f0d1d0f6a0d6a4eb01730002013fe9c` |
| library.db | Preserved (466,944B) on SD at `.audiobook_library/` |
| Audiobooks dir | Intact (Cormac McCarthy, D. J. Molles, Dale Carnegie, Dean Koontz, etc.) |
| .audiobook_volume | Preserved |
| Boot ADB byte | `0x00 0x00` — OTA reset cleared it. Manual re-enable required via System→USB setting on device. |

## Known issues in this build

- **Hook has debug dbopen logging** — `fopen("/tmp/.audiobook_dbdbg")` in library.c. Remove for release.
- **Boot ADB not persisted** — OTA recovery zeros `/usr/data/user.ini` offset 1856 mode byte. Must re-enable on-device (System → USB working mode → Device/ADB) + power cycle.
- **.upt is dev build only** — NOT published as a public release.

## Previous release reference

| Item | Value |
|------|-------|
| Version | v2.0.20 (latest public release) |
| .upt size | 42,217,472 bytes |
| .upt md5 | `64b408be7d3a8c92a3ca0cb8b0119503` |

## Next steps (for release when ready)

1. Remove dbopen debug logging from `library.c` (the `fopen("/tmp/.audiobook_dbdbg")` calls)
2. Rebuild hook + .upt without debug output
3. Update verifier rootfs hashes in `tools/verify_r1_audiobook_build.py`
4. Publish via `publish_github_release.ps1`
