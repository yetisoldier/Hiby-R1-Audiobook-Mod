# HiBy R1 Audiobook Mod v2.0.28

This release is for the normal HiBy R1 only. It is based on stock HiBy R1
firmware 1.6 and is not for the R1 MIDI.

The About screen should show `HiBy R1 2.0.28` after installation.

## What changed since v2.0.27

### Audiobooks stay out of Music

This release fixes fresh installations where files under `/Audiobooks` also
appeared in HiBy's Music section after Update Database.

The Audiobooks app and HiBy Music use separate databases. Previous v2.x builds
correctly scanned the audiobook library, but HiBy's own scanner could still add
the same files to Music. Opening Audiobooks now starts a short background
cleanup that removes only root `/Audiobooks` entries from the known Music
database copies and repairs the affected catalog counts and indexes.

The cleanup does not depend on refresh order. After running Music -> Update
Database, open Audiobooks once and the catalogs will be separated. The worker
then exits; it is not an always-running service and adds no idle battery or RAM
usage.

Playback, resume, chapters, bookmarks, display wake, USB handling, and the
Audiobooks user interface are unchanged from v2.0.27.

## Installation

1. Download `r1-audiobooks-2.0.28.upt`.
2. Rename it to exactly `r1.upt`.
3. Copy it to the root of the R1 SD card.
4. On the R1, choose System -> Firmware update -> Via SD-card.
5. Confirm the update and wait for the reboot.
6. Delete or rename `r1.upt` after the update.
7. Run Music -> Update Database when using a new or changed SD card.
8. Open Audiobooks and tap **Refresh Library** when the audiobook contents
   changed. Opening Audiobooks also performs the Music-catalog separation.

Recommended layout:

```text
/Music
/Audiobooks/Author/Book Title/01 - Chapter.mp3
/Audiobooks/Author/Book Title/Book Title.m4b
```

The location under `/Audiobooks` is authoritative; the Genre tag does not need
to be `Audiobook`.

## Verification

- A synthetic regression database with shared music/audiobook metadata passed.
- Two captured R1 databases removed 135 and 298 audiobook rows while preserving
  all 114 and 7,396 legitimate Music rows respectively.
- Catalog counts, format/time indexes, SQLite integrity, and idempotence passed.
- The full local sanity suite and strict production firmware verifier passed.
- Persistent boot ADB remains disabled, preserving normal SD-card USB storage.

## Known limitations

- Audiobook playback stops when leaving the Audiobooks app.
- There is no audiobook text search; use Titles, Authors, Series, or Folders.
- ADB, USB mass storage, and USB DAC share one controller and cannot be active
  simultaneously.
- Public firmware does not keep ADB enabled after reboot.

Keep a copy of official HiBy R1 1.6 firmware for recovery. This is unofficial
firmware tested on one normal R1; install it at your own risk.
