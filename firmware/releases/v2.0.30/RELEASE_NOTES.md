# HiBy R1 Audiobook Mod v2.0.30

This release is for the normal HiBy R1 only. It is based on stock HiBy R1
firmware 1.6 and is not for the R1 MIDI.

The About screen should show `HiBy R1 2.0.30` after installation.

## What changed since v2.0.29

### More stable wired playback

- Mono audiobooks are now sent to the wired DAC through the same stable stereo
  hardware path used by stock playback. The audio remains centered and its
  timing is unchanged.
- Wired playback uses a small bounded audio queue and full hardware periods to
  prevent slow storage work or track transitions from starving the DAC.
- A persistent audio-output error now pauses and saves the book position instead
  of letting the timer move while no sound is produced.

### Smoother multipart books and more reliable seeking

- Compatible MP3 parts reuse the open audio device and prepare the next part in
  the background. This reduces pauses and avoids unnecessary DAC restarts.
- MP3 seek calculations now skip large embedded cover-art metadata, improving
  resume and skip accuracy near the beginning of a part.
- Repeated forward and back skips inside one MP3 part no longer risk a false end
  of file that jumps to the next part.

### Resume no longer interrupts audio

- The 15-second progress checkpoint now runs on a low-priority background
  worker instead of the real-time decoder thread.
- The live checkpoint is tied to the physical SD card. Swapping cards cannot
  apply the position from a different card with a reused library ID.
- Pause, stop, book completion, and leaving the app still save the exact
  position to the SD card and update the library percentage.

A new book must play for at least 15 seconds to receive its first automatic
checkpoint. Pausing or leaving the app saves immediately.

### Physical buttons and large-library performance

- Physical Next and Previous now use the R1's correct Linux key codes. In the
  Audiobooks app they skip forward 60 seconds and back 30 seconds.
- Music-catalog cleanup now updates only groups affected by audiobook rows and
  runs at low priority. On the captured 7,694-row test database it improved
  from more than six minutes to about 1.3 seconds while preserving all 7,396
  Music rows.
- Play/Pause, volume buttons, hold-to-ramp volume, and the volume indicator were
  revalidated with the new playback path.

## Installation

1. Download `r1-audiobooks-2.0.30.upt`.
2. Rename it to exactly `r1.upt`.
3. Copy it to the root of the R1 SD card.
4. On the R1, choose System -> Firmware update -> Via SD-card.
5. Confirm the update and wait for the reboot.
6. Delete or rename `r1.upt` after the update.
7. Run Music -> Update Database when using a new or changed SD card.
8. Open Audiobooks and tap **Refresh Library** after changing audiobook files.

Recommended layout:

```text
/Music
/Audiobooks/Author/Book Title/01 - Chapter.mp3
/Audiobooks/Author/Book Title/Book Title.m4b
```

The location under `/Audiobooks` is authoritative; the Genre tag does not need
to be `Audiobook`.

## Verification

- The complete local regression suite and strict production verifier passed.
- Small and 6,002-Music-row synthetic catalog tests passed, as did both captured
  R1 databases.
- On the test R1, a mono multipart MP3 resumed near a part boundary, crossed the
  boundary, and handled forward/back skips on both sides without false EOF.
- Physical Play/Pause, Next, Previous, and volume controls passed.
- The final playback soak recorded zero AIC DMA-address faults and exited
  cleanly to the HiBy launcher.
- Persistent boot ADB remains disabled, preserving normal SD-card USB storage.

## Known limitations

- Audiobook playback stops when leaving the Audiobooks app.
- There is no audiobook text search; use Titles, Authors, Series, or Folders.
- ADB, USB mass storage, and USB DAC share one controller and cannot be active
  simultaneously.
- Public firmware does not keep ADB enabled after reboot.
- UTF-8/Cyrillic text support from v2.0.20 is not included in this stability
  line.
- The NativeApp supports MP3 and AAC audio in M4B/M4A containers. A browser
  downloader may save another codec even when a filename looks familiar.

Keep a copy of official HiBy R1 1.6 firmware for recovery. This is unofficial
firmware tested on one normal R1; install it at your own risk.
