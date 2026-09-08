# HiBy R1 Audiobook Mod v2.0.29

This release is for the normal HiBy R1 only. It is based on stock HiBy R1
firmware 1.6 and is not for the R1 MIDI.

The About screen should show `HiBy R1 2.0.29` after installation.

## What changed since v2.0.28

### Fixed: screen could stay black after the backlight timer expired

HiBy's automatic backlight timeout uses two separate operations: it blanks the
display and then asks the device to suspend. The Audiobooks app already
converted the first operation into its lightweight screen-off mode, but the
later suspend request could still race with it. In that state a book and the
physical audio controls could keep working while the screen would not wake.

While Audiobooks is open, v2.0.29 now blocks only that exact stock suspend
request and uses the app's lightweight backlight-only blank instead. The power
button or a touchscreen double-tap can wake the display while playback
continues. HiBy's normal screen timeout and suspend behavior is unchanged
outside Audiobooks.

This release intentionally does not change audiobook decoding or Bluetooth
routing. Those reports are being investigated separately.

## Installation

1. Download `r1-audiobooks-2.0.29.upt`.
2. Rename it to exactly `r1.upt`.
3. Copy it to the root of the R1 SD card.
4. On the R1, choose System -> Firmware update -> Via SD-card.
5. Confirm the update and wait for the reboot.
6. Delete or rename `r1.upt` after the update.
7. Run Music -> Update Database when using a new or changed SD card.
8. Open Audiobooks and tap **Refresh Library** when audiobook contents change.

Recommended layout:

```text
/Music
/Audiobooks/Author/Book Title/01 - Chapter.mp3
/Audiobooks/Author/Book Title/Book Title.m4b
```

The location under `/Audiobooks` is authoritative; the Genre tag does not need
to be `Audiobook`.

## Verification

- The full local regression suite passed.
- The strict production verifier passed every stock path, file mode, symlink,
  ownership, launcher, catalog, USB, audio-unlock, and OTA check.
- Persistent boot ADB remains disabled, preserving normal SD-card USB storage.
- Long-duration device testing is still recommended for this intermittent
  timeout-related failure.

## Known limitations

- Audiobook playback stops when leaving the Audiobooks app.
- There is no audiobook text search; use Titles, Authors, Series, or Folders.
- ADB, USB mass storage, and USB DAC share one controller and cannot be active
  simultaneously.
- Public firmware does not keep ADB enabled after reboot.
- The NativeApp supports MP3 and AAC audio in M4B/M4A containers. A browser
  downloader may save another codec even when a filename looks familiar.

Keep a copy of official HiBy R1 1.6 firmware for recovery. This is unofficial
firmware tested on one normal R1; install it at your own risk.
