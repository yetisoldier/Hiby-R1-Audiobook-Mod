# Release Notes: 1.6.7-audiobook

> **⚠️ Historical (pre-2.0, v1.6.x era).** This is an old release draft for
> the resume-daemon line. The current release is v2.0.17 (NativeApp pivot) —
> see [`release_recovery_notes.md`](./release_recovery_notes.md) and
> [`production_release_checklist.md`](./production_release_checklist.md).
> Retained as a historical record.

Status: published as GitHub release `v1.1.0`.

This build is for the normal HiBy R1 only. It is based on stock HiBy R1 firmware 1.6, not the R1 MIDI.

## Download Asset

- File: `r1-audiobooks-1.6.7-audiobook.upt`
- Local path: `work\audiobook-firmware-1.6.7-candidate\r1-audiobooks-1.6.7-audiobook.upt`
- MD5: `7a5b0267811de7198039aa96144f3f8c`
- SHA256: `2ac14cdd858f91af99cff8365c5d0664ca3d01233a89bc82e8ba010c7dfcbd78`

## Changes Since 1.6.4

- Keeps all `1.6.4-audiobook` behavior: Audiobooks launcher tile, separated audiobook catalog, normal Now Playing screen, per-book resume, and self-contained on-device catalog rebuild after Music -> Update Database.
- Adds guarded audiobook play-mode correction. When an audiobook is active on Now Playing, the daemon can switch the player back to the observed sequential/list-loop mode so multipart books advance in order.
- Improves multipart resume when the stock title-list route lands near the saved file but not exactly on it. The daemon now has a narrow near-miss fallback that can step a few tracks with the R1's own Next/Previous transport instead of giving up.
- Normalizes files under `/Audiobooks` to the internal `Audiobook` route regardless of the original genre tag, including custom genres such as `Tolkien Audiobook` or blank genre tags, after the on-device database helper runs.
- Keeps exact resume by default. The optional smart-rewind setting is present internally, but it is off unless deliberately enabled for testing.
- Keeps lower-power idle polling when playback is not on an audiobook path.

## Verified On Test Device

Installed-device verification passed on 2026-06-11:

- Firmware marker reports `1.6.7-audiobook`.
- Resume daemon and DB watcher start from boot.
- SD-root `r1.upt` was removed after installation.
- No audiobook rows leak into normal Music search, album, or genre catalog tables.
- Regression fixtures pass for custom audiobook genre tags and blank audiobook genre tags under `/Audiobooks`.
- The installed DB has 135 audiobook rows and six audiobook books on the test SD card.
- No known development artifacts remained active under `/usr/data/audiobooks`.

Live smoke test:

- From the Audiobooks title list, selecting `When You Are Engulfed in Flames` initially landed near the saved multipart file.
- The new near-miss fallback stepped from `13/30` to `15/30` using two Next events.
- Playback then restored to the saved `15/30` position around `05:30`.

## Install

1. Keep a copy of the official HiBy R1 stock 1.6 firmware in case you want to go back.
2. Download `r1-audiobooks-1.6.7-audiobook.upt`.
3. Rename it to exactly `r1.upt`.
4. Copy `r1.upt` to the root of the SD card.
5. On the R1, run the normal firmware update from SD card.
6. After the update succeeds and the player reboots, delete or rename `r1.upt` from the SD card.
7. On the R1, go to Music and run `Update Database`, then wait for the scan to complete.
8. Open Audiobooks.

## Recommended SD Card Layout

```text
/Music
/Audiobooks
```

Recommended audiobook layout:

```text
/Audiobooks/Author/Book Title/01 - Chapter.mp3
/Audiobooks/Author/Book Title/02 - Chapter.mp3
```

Single-file `.m4b` books should also work.

For best results, set the album tag to the book title and the artist/album artist to the author. The firmware can fall back to folder and file names when tags are not perfect.

## Known Quirks

- Pressing Back from Audiobooks first goes to a Genres screen, then Back again returns to the main menu.
- Multipart resume may briefly show or play nearby files while the daemon corrects to the saved file.
- There is no audiobook search UI; browse by scrolling through titles.
- The About screen may show a shortened version string like `1.6.7-a`.
- This replaces the old text Books feature with Audiobooks.
- This is unofficial firmware tested on one normal HiBy R1. Use at your own risk.
