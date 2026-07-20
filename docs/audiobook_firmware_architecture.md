# Audiobook Firmware Architecture

This document describes how the public `v1.6.1` / `1.6.18-audiobook` firmware
works internally. It is intended for firmware developers and modders.

## Design Goal

The mod does not replace HiBy OS or the stock player. It keeps the stock audio
engine, codec handling, Now Playing screen, Bluetooth behavior, EQ path, and
physical controls, then adds audiobook-specific catalog, UI entry, and resume
behavior around that stock path.

The guiding rule is: modify the smallest stable surface that can produce the
user-visible audiobook behavior.

## High-Level Components

```text
Launcher / Resources
  Books label and icon become Audiobooks.

hiby_player binary patches
  Audiobooks launcher opens a native hub.
  Hub rows open generated audiobook views or run refresh.

Media DB helper
  Scans /Music and /Audiobooks.
  Normalizes audiobook rows.
  Rebuilds Music catalog tables without audiobook leakage.
  Writes audiobook catalogs and generated view playlists.

DB watcher
  Runs after boot and after media DB changes.
  Repairs internal and SD-root DB mirrors.
  Handles manual refresh requests.

Resume daemon
  Tracks audiobook-only playback state.
  Saves per-book resume records.
  Restores track and position on title starts.

ADB and verification tools
  Stage packages, verify installed state, capture framebuffer, and automate
  smoke tests.
```

## Rootfs Additions

The release installs these main files into the read-only rootfs:

- `/etc/init.d/S91audiobook_resume.sh`
- `/etc/init.d/S92audiobook_db_maint.sh`
- `/etc/r1_audiobook_version`
- `/usr/bin/r1_audiobook_resume_daemon.sh`
- `/usr/bin/r1_audiobook_resume_helper`
- `/usr/bin/r1_audiobook_memscan`
- `/usr/bin/r1_audiobook_direct_open`
- `/usr/bin/r1_audiobook_db_maint`
- `/usr/bin/r1_audiobook_db_watch.sh`
- `/usr/bin/r1_audiobook_refresh.sh`
- `/usr/bin/r1_usrlocal_media_seed.db`
- generated touch/key event packet files used by the resume runtime

Runtime state is stored under writable internal storage:

```text
/usr/data/audiobooks/
  catalog.tsv
  catalog-books.tsv
  catalog-view-title.tsv
  catalog-view-author.tsv
  catalog-view-series.tsv
  refresh.request
  refresh.log
  db-maint.log
  resume-daemon.log
  resume.d/
```

Generated SD-card views are written under:

```text
/Audiobooks/_views/Titles/
/Audiobooks/_views/Authors/
/Audiobooks/_views/Series/
```

Those generated folders are practical route targets for the stock file/list
views. They may also appear when browsing `Audiobooks -> Folders`.

## UI And Resource Changes

The stock `Books` launcher text is patched to `Audiobooks` through resource
files. The old text-reader flow is replaced for the normal launcher path.

`tools\patch_r1_resource_text.py` handles the user-facing text. In the current
release the native hub rows are:

```text
Refresh Library
Titles
Authors
Series
Folders
```

`tools\generate_audiobook_launcher_icons.py` replaces the stock Books launcher
icons with same-size audiobook icons for the stock themes.

## hiby_player Binary Patches

`tools\patch_hiby_player.py` applies guarded byte patches to the stock R1 1.6
`/usr/bin/hiby_player`. The verifier checks the exact expected bytes before a
package is trusted.

Important current patch areas:

- `0x35DAEC`
  Native hub launcher callback cave.

- `0x482030`
  Launcher callback table entry that points the Audiobooks tile at the native
  hub launcher cave.

- `0x35DF40`
  Private resource key used for the native hub title row.

- `0x360708`, `0x360738`, `0x360768`, `0x360798`
  Row caves for `Titles`, `Authors`, `Series`, and `Folders`.

- `0x360808`, `0x360888`, `0x360908`, `0x360988`
  UTF-16LE route/path strings for the generated views and folder root.

- `0x360A08`
  `Refresh Library` row cave. It calls `system("/usr/bin/r1_audiobook_refresh.sh &")`
  and then opens the `Titles` view so the user gets visible feedback.

- `0x360A80`
  Refresh helper command string.

- `0x38D278` through `0x38D288`
  Native hub row jump-table entries for refresh/title/author/series/folder rows.

- `0x360D50`
  Explorer cleanup/open helper used by generated view rows.

The release intentionally avoids larger invasive changes to the stock media
player core. Risky route-table experiments are kept as RAM-only ADB tools when
possible.

## Media Database Model

The stock R1 media DB is SQLite. The important public behavior is:

- Music files live under `/Music`.
- Audiobook files live under `/Audiobooks`.
- The exact genre tag does not need to be `Audiobook`.
- Audiobook rows are normalized by folder location.
- Normal Music albums and search tables should not show audiobook rows.
- One internal `Audiobook` route row is retained so the native Audiobooks hub
  can open reliably.

The helper source is `tools\r1_audiobook_db_maint.c`.

The helper:

- Seeds a missing/empty DB from `r1_usrlocal_media_seed.db`.
- Scans `/Music` when music rows are missing.
- Scans `/Audiobooks` and ignores generated `_views` folders.
- Fills practical fallback title/author/album metadata from folders and names.
- Normalizes audiobook genre routing.
- Removes audiobook leakage from normal Music search and album tables.
- Writes title, author, series, and book-level catalogs.
- Generates playlist-style view folders under `/Audiobooks/_views`.
- Repairs stale route row counts after SD-card swaps.
- Avoids embedded NUL text fields in audiobook rows.

The DB watcher mirrors repaired databases between:

```text
/usr/data/usrlocal_media.db
/data/usrlocal_media.db
/usr/data/mnt/sd_0/usrlocal_media.db
```

The SD-root mirror matters because the stock UI can read that copy after scans.

## Refresh Library

`Refresh Library` exists because the old stock `Scan` label was misleading and
the stock text-book scan was not useful for audiobooks.

The flow is:

1. User taps `Refresh Library`.
2. `hiby_player` row cave calls `/usr/bin/r1_audiobook_refresh.sh &`.
3. The row cave opens the generated `Titles` view as visible feedback.
4. `r1_audiobook_refresh.sh` writes `/usr/data/audiobooks/refresh.request`.
5. The helper also runs `r1_audiobook_db_maint` immediately with generated view
   options.
6. The DB watcher notices the request marker and forces a maintenance pass if
   needed.
7. Refresh details are written to `/usr/data/audiobooks/refresh.log`.

The watcher includes a boot-window guard so refresh requests made soon after
startup are not swallowed.

## Resume Runtime

The resume daemon is a shell runtime backed by small native helpers and stock
input events. It is deliberately audiobook-specific.

Important behaviors:

- Per-book records are stored under `/usr/data/audiobooks/resume.d`.
- New-track and steady-state saves are delayed to 15 seconds. This avoids
  saving accidental taps, previews, or wrong starts.
- Finishing within 45 seconds of the end of the whole book marks it completed.
  The next title start begins from the start.
- Leaving an audiobook to play normal Music quiets the daemon and avoids
  repeated position reads or DB churn.
- Multipart resume first tries to select the saved file directly. Fallbacks use
  visible-row selection or near-miss transport correction.
- UI seek fallback is guarded so it only runs on a Now Playing-like screen.
- The daemon protects deeper bookmarks from being overwritten after a failed
  restore or unresolved title start.
- Audiobook play mode is guarded toward sequential playback while an audiobook
  is active.

This resume layer is a workaround for the stock player's global-ish resume
behavior. It is not a clean public player API.

## Title, Author, And Series Views

The current firmware provides Title, Author, and Series entry points by
generating view folders and playlist files:

- `Titles` shows one row per book title.
- `Authors` shows `Author - Title` style rows.
- `Series` shows only books with series metadata or series-like folder
  structure. Standalone books are not forced into fake series.

The Seanap/Plex-style folder shape is supported:

```text
/Audiobooks/Author/Series/2020 - Book Title [Series 02]/01 - Chapter.mp3
/Audiobooks/Author/2021 - Standalone Book/01 - Chapter.mp3
```

Album and album-artist tags are still preferred, but the helper can fall back to
folder names.

## Audio Unlocks

Since v2.0.17 the release also enables three non-audiobook stock-feature
unlocks (applied by `tools/patch_r1_audio_feature_unlocks.py`):

- Native DSD marker for analog output (`AnalogDsdNative: native` in
  `ot_devices.json`).
- Bluetooth SBC XQ startup option (`--sbc-quality=xq` in `/usr/bin/bt_init`).
- USB DAC-related settings/flags (`usb_mode`, `dac_feedback`, `car_mode`,
  `standby`, `about`, `dac_to_store` in `set_functions.json` /
  `midi_set_functions.json` / `config.json`).

These were carried by every pre-2.0 release (v1.5.0-v1.6.3) but were dropped at
the v2.0.0 NativeApp pivot and are restored in v2.0.17. They are pure
resource/shell-config tweaks (no binary, boot, PMIC, or mount changes), so they
are independent of the audiobook app and its hook. USB DAC and boot-ADB share the
single USB gadget controller and are mutually exclusive by USB working mode, so
both can ship together. SBC XQ applies to the audiobook BT path too (the app
drives `pcm.bluealsa` directly) and was on-device verified not to regress BT
output. Future builds should keep them separable so audiobook bug fixes can be
evaluated independently.

## Performance And Battery Notes

The public firmware avoids continuous heavy background work:

- The DB watcher does not rebuild for mtime-only playback churn by default.
- The resume daemon uses lower-rate idle polling outside audiobook context.
- Logs are capped and rotated.
- Normal Music playback should not trigger audiobook resume saves.

If users report lag or random reboots, collect:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_collect_audiobook_resume_debug.ps1
```

Also check whether unusually large embedded covers or sidecar images are making
the stock UI slow. The broader HiBy modding community recommends modest JPEG
cover sizes on X1600-class players.

## Known Limitations

- No full custom audiobook search UI exists.
- Generated `_views` may be visible from folder browsing.
- Back navigation is better than earlier builds but still shares stock view
  behavior.
- Resume is strong but still partly uses UI/memory/input workarounds.
- ADB must be manually re-enabled after reboot on the tested public build.
- Full QEMU system emulation is not a release-confidence replacement.

## Safer Extension Points

Good next places to experiment:

- Improve generated view naming and sorting.
- Add more DB fixture cases for path length, cover art, formats, and tags.
- Improve ADB smoke tests and screen classification.
- Prototype deeper UI routes in RAM before flashing.
- Add optional dev-build features behind build switches.

High-risk areas:

- Replacing core stock playback.
- Changing boot scripts without local and installed verification.
- Importing patches from other HiBy models without R1-specific byte checks.
- Flashing a package whose rootfs modes were not verified.
