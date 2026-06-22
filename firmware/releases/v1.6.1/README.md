# HiBy R1 Audiobook Mod v1.6.1

Hotfix release for the normal HiBy R1 only. This is based on stock HiBy R1 firmware 1.6 and is not intended for the R1 MIDI.

## Download

- File: `r1-audiobooks-1.6.18-audiobook.upt`
- Firmware marker: `1.6.18-audiobook`
- MD5: `e3dba87c24ef84196ec1c91fe3c3e26a`
- SHA256: `e42d70d84bf3353391c16fa60f83f399d2624226d2792f3c7882d9a1bbe45253`
- Local package verification: passed.
- Installed-device verification: passed on the test R1 after flashing the public `1.6.18-audiobook` package.

## What Changed Since v1.6.0

- The Audiobooks hub now shows `Refresh Library`, `Titles`, `Authors`, `Series`, and `Folders`.
- `Refresh Library` replaces the misleading stock text-book `Scan` action.
- Tapping `Refresh Library` gives visible feedback by opening the `Titles` view.
- The same tap starts an audiobook library refresh in the background and logs start/completion under `/usr/data/audiobooks/refresh.log`.
- Manual refresh also rebuilds generated title/author/series views.
- A boot-timing edge case was fixed so a refresh request tapped during startup is not swallowed by the watcher.
- All `v1.6.0` audiobook hub, resume, folder-based audiobook detection, Native DSD, Bluetooth SBC XQ, and USB DAC-related behavior is retained.

## Install

1. Download `r1-audiobooks-1.6.18-audiobook.upt`.
2. Rename it to exactly `r1.upt`.
3. Copy it to the root of the SD card.
4. Run the normal firmware update from the R1 UI.
5. After the update succeeds and the player reboots, delete or rename `r1.upt` on the SD card.
6. Go to Music and run `Update Database`.
7. Wait for the scan to complete, then open Audiobooks.

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
- The generated `_views` folder may appear under Audiobooks -> Folders.
- From the Folders root, edge-back is more reliable than the left arrow.
- This replaces the old text Books launcher flow.
- Keep a stock HiBy R1 1.6 firmware file handy in case you want to revert.

## Developer Documentation

The repository includes separate developer docs for people who want to build,
modify, or audit the firmware:

- `docs/modder_start_here.md`
- `docs/audiobook_firmware_architecture.md`
- `docs/build_flash_verify_runbook.md`
