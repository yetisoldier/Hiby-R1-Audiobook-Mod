# HiBy R1 Audiobook Firmware Investigation

> **⚠️ SUPERSEDED — historical record.** This document captures the original
> pre-2.0 (v1.6.x) investigation: the stock OTA layout, rootfs, `hiby_player`,
> media DB schema, and early prototype ideas that led to the resume-daemon /
> stock-route approach. The current firmware (v2.0.17) uses the **NativeApp
> pivot** — an in-process `LD_PRELOAD` hook into `hiby_player` — which replaced
> that approach. For the current architecture read
> [`audiobook_firmware_architecture.md`](./audiobook_firmware_architecture.md)
> and the [`docs/modding/`](./modding/) knowledge base. The findings below are
> retained as historical reference; do not follow them as a current build path.

Working target: HiBy R1 stock firmware 1.6, normal R1, not MiDi.

## Stock Firmware Source

- Official R1 1.6 download page links to Google Drive folder `1A2RIMdvuZRzGCNMY9F81vtGAAs2FJ46W`.
- Downloaded stock package to `stock/r1.upt`.
- `r1.upt` is an ISO 9660 image containing `ota_config.in` and `ota_v0/`.
- `ota_v0/` contains chunked `xImage` and chunked `rootfs.squashfs`.
- Reconstructed files:
  - `work/original/rootfs.squashfs`
  - `work/original/xImage`

## Root Filesystem

- `rootfs.squashfs` is SquashFS 4.0, LZO compressed, block size 131072.
- 7-Zip can inspect and extract it for analysis on Windows.
- 7-Zip skips Linux symlinks during extraction on Windows; this is fine for analysis but not for repacking.
- Repacking uses the portable SquashFS 4.3 tools extracted under `.deps/squashfs/tools/squashfs-tools`.
- A round-trip extract/repack has been tested with LZO compression and block size 131072.

## Main Application

- Main UI/player binary: `/usr/bin/hiby_player`
- Architecture: ELF32, MIPS, little endian, executable.
- Binary has useful strings and dynamic symbols, but internal application functions are mostly not symbolized.
- Important strings found:
  - `/data/usrlocal_media.db`
  - `/data/mnt/sd_0/.temp/usrlocal_media.db`
  - `book.db`
  - `MEDIA_TABLE`
  - `BOOK_TABLE`
  - `BOOK_RECENT_TABLE`
  - `BOOKMARK_TABLE`
  - `break_point`
  - `auto_scroll_playplane`

## Existing Native Features

- The Books app is a text reader rooted at `/book`, with:
  - favorites
  - recent items
  - bookmarks
  - persistent file position in `BOOKMARK_TABLE.filepos`
- Playback Settings already include:
  - `Resume play from last`
  - options appear to be `(none)`, `Track`, and `Position`
  - `Automatically slide to the playback interface` exists in strings/localization, but it was not obvious in the visible play settings JSON.
- Firmware 1.6 already supports `.m4b` according to HiBy's 1.4 changelog and the media updater reference.

## Media Database

Primary database path appears to be `/usr/data/usrlocal_media.db`, also referenced internally as `/data/usrlocal_media.db`.

Core media schema includes:

```sql
MEDIA_TABLE(
  id INT,
  path TEXT COLLATE NOCASE,
  name TEXT COLLATE NOCASE,
  album TEXT COLLATE NOCASE,
  artist TEXT COLLATE NOCASE,
  genre TEXT COLLATE NOCASE,
  year INT,
  dis_id INT,
  ck_id INT,
  has_child_file INT,
  begin_time INT,
  end_time INT,
  cue_id INT,
  character TEXT COLLATE NOCASE,
  size INT,
  sample_rate INT,
  bit_rate INT,
  bit INT,
  channel INT,
  format INT,
  quality TEXT COLLATE NOCASE,
  album_pic_path TEXT COLLATE NOCASE,
  lrc_path TEXT COLLATE NOCASE,
  track_gain REAL,
  track_peak REAL,
  ...
)
```

Stock code also uses duplicate/staging tables such as `MEDIA2_TABLE`, `MEDIA3_TABLE`, `SEARCH_TABLE`, `HISTORY_TABLE`, `RECENT_TABLE`, `ARTIST_TABLE`, `ALBUM_TABLE`, `GENRE_TABLE`, `ALBUM_ARTIST_TABLE`, `FORMAT_TABLE`, `COUNT_TABLE`, `CTIME_TABLE`, and `MTIME_TABLE`.

## Safe Prototype Path

The lowest-risk first milestone is not a firmware flash:

1. Let the R1 scan the SD card normally.
2. Pull `/usr/data/usrlocal_media.db` over ADB.
3. Filter rows whose `path` begins with `a:\Audiobooks\`.
4. Push the filtered DB back or load it through an eventual Database Manager-style patch.

This proves the "audiobooks do not show in Music" requirement without touching rootfs or boot partitions.

## Deeper Firmware Patch Ideas

The ideal native solution likely needs binary patching:

1. Exclude `/Audiobooks/` during scanner insertion into `MEDIA_TABLE` or filter normal Music queries with `path NOT LIKE 'a:\Audiobooks\%'`.
2. Reuse existing Album list/listview machinery for an Audiobooks section:
   - query albums where `path LIKE 'a:\Audiobooks\%'`
   - album metadata is already the book title, matching the requested model.
3. Add/repurpose a launcher tile or Books tile to open the audiobook-filtered album view.
4. Use existing audio player for playback so Now Playing, codecs, `.m4b`, Bluetooth, EQ, etc. remain stock.
5. For audiobook-specific resume, investigate whether stock `break_point = Position` is only global or stores enough history per path. If not, a new per-path position store is needed.

## ADB Notes

ADB executable found locally:

```powershell
C:\Program Files\Software Fix\adb.exe
```

Stock ADB enable flow, from community notes:

1. Settings -> About
2. Tap "About" 10 times
3. Connect USB
4. Run `adb devices -l`

No device was visible during initial investigation.

## Live Device Findings

Collected from the connected R1 over ADB on 2026-06-09.

- Device is running the stock 1.6 `/usr/bin/hiby_player` and `config.json` from the downloaded firmware.
- Root filesystem is read-only SquashFS; `/usr/data` is writable UBIFS; SD card is mounted at `/usr/data/mnt/sd_0`.
- Stock ADB boot support exists but is not run automatically: `/etc/init.d/T90adb` starts ADB, while `/etc/init.d/rcS` only runs `/etc/init.d/S??*`.
- The writable `/usr/data/disableadb` marker blocks ADB startup when present, but on this device it was already absent. Both stock ADB backends, `/etc/init.d/adb/S310adb` and `/etc/init.d/adb/S440adb`, check that marker before starting.
- For development firmware builds, `tools/build_r1_audiobook_firmware.ps1 -EnableBootAdb` installs a hardened `S90adb` wrapper. It still requires `/usr/data/enable_boot_adb` and Device mode. Public release builds omit the script.
- Static strings in `hiby_player` point the stock USB UI toward `/data/user.ini`, `usb_mode`, `usb_working_mode`, `/usr/bin/adbon`, and `/usr/bin/adboff`.
- The default WSL `objdump` can read the ELF sections but cannot disassemble MIPS. `tools/install_mips_binutils_wsl.ps1` downloads `binutils-mipsel-linux-gnu` into `.deps`, and `tools/mips_objdump_wsl.ps1` runs the extracted `mipsel-linux-gnu-objdump` with the required local library path.
- MIPS data inspection shows a stock settings table with adjacent numeric IDs:
  `usb_working_mode` is ID `8` and `usb_mode` is ID `9`. The live USB-mode
  snapshot should therefore watch for small binary changes near those setting
  slots in `/data/user.ini`, not only whole-file hash changes.
- Live Auto -> Device testing found the stock `USB working mode` value at
  `/usr/data/user.ini` offset `0x740` (`1856` decimal): Auto is `0`, Device is
  `1`. Development boot ADB uses this existing UI value plus a separate opt-in
  marker instead of adding a settings page.
- Current SD card has `/Audiobooks`, `/Books`, and `/Music`.
- Current media DB has 114 normal music tracks in `MEDIA_TABLE`/`MEDIA2_TABLE`, all under `a:\Music\`.
- Current audiobook records are not in `MEDIA_TABLE`; they are in `BOOK_TABLE`, `BOOK_RECENT_TABLE`, `BOOKMARK_TABLE`, and a few `HISTORY_TABLE` rows.
- Current `BOOK_TABLE` has one row per book, using the first audio file for multi-file books.
- Current `user.ini` contains the playback resume value `none`.
- Prior experiments under `/usr/data` include:
  - a RAM scanner-skip patch replacing `System Volume Information` with `Audiobooks`
  - a book-row audio shim binary
  - helper scripts for position read/seek sidecar JSON

## Reproducible Local Patches

`tools/patch_hiby_player.py` now verifies the stock 1.6 binary and copies it to a requested output path. The risky binary experiments are opt-in flags rather than defaults.

Default safe output:

```powershell
python tools\patch_hiby_player.py work\rootfs\usr\bin\hiby_player -o work\patched\hiby_player.default-safe
```

Output checked on 2026-06-09:

- MD5: `ad69fa8377fb85b01ed5d65fe976b19a`
- SHA256: `8398e1e1295e83b033bf7b8c39932fff3f620831f5a91682869554047b26f6b2`

Historical experimental shim output can still be reproduced for analysis:

```powershell
python tools\patch_hiby_player.py work\rootfs\usr\bin\hiby_player --scan-skip --book-audio-shim -o work\patched\hiby_player.experimental-shim
```

Historical shim output checked on 2026-06-09:

- MD5: `6395aa506ca6577ab12eb6cfead04096`
- SHA256: `f83af3b7690dad4faf1cae2d297dc59441ea6f532805c25c3e4b10bc569e9e07`
- The patched call at `0x540b0c` targets the code cave at `0x75daec`.

`tools/patch_r1_resource_text.py` patches English UI labels so the repurposed Books section appears as Audiobooks.

## Offline Firmware Package

`tools/build_r1_audiobook_firmware.ps1` builds a conservative development OTA package without touching the device:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_r1_audiobook_firmware.ps1
```

Generated package on 2026-06-09:

- `work/audiobook-firmware/r1-audiobooks-dev-safe.upt`
- UPT MD5: `f0759f913f011289a3eeb6ef8da9e399`
- UPT SHA256: `88f71d6b748646aaa5dfc08747959c2eb378250bec3aebcd84ee12554a49eff8`
- Rootfs MD5: `0574354b74af722a8d359d755c8957d6`
- Rootfs SHA256: `cc959c26abd79f73f060d79e718fff3859d3f8f1d0fdfef29cac72d5229e35c0`
- `/usr/bin/hiby_player` MD5 inside the unsquashed rootfs: `ad69fa8377fb85b01ed5d65fe976b19a`
- Development ADB persistence: later dev builds can include `/etc/init.d/S90adb`. The hardened wrapper checks `/usr/data/user.ini` offset `0x740`, requires `/usr/data/enable_boot_adb`, and only starts ADB when the stock `System -> USB working mode` setting is `Device`.
- v2.0.27 testing showed that the old delayed retry could steal the USB controller from mass storage and leave its LUN holding the SD block device. Current release builds omit boot ADB, and development builds use a delayed one-shot transition with no retry loop.
- English resource labels are patched from Books/E-book to Audiobooks.
- No experimental `hiby_player` binary patches are applied by default.

Verification performed:

- The packaged `xImage` chunks reconstruct exactly to the stock `work/original/xImage`.
- The packaged `rootfs.squashfs` chunks reconstruct exactly to `work/audiobook-firmware/rootfs.squashfs`.
- The OTA chunk manifests match the MD5 of every chunk.
- The chained chunk filename hashes match the stock OTA naming pattern.
- The final rootfs independently unsquashes and contains the stock player bytes plus English Audiobooks labels.

## Open Risks

- The old `r1-audiobooks-test.upt` package should be treated as a historical prototype, not a flash candidate. It contains the rough Books-row audio shim, and live RAM testing showed that path can play audio but does not correctly drive the stock Now Playing progress UI.
- The current `r1-audiobooks-dev-safe.upt` package is safer but still not a final flash candidate. It only provides resource labels and persistent development ADB; it does not yet create the separate audiobook media view or per-book resume behavior.
- Multi-file books currently enter the Books table as one row pointing at the first audio file. A real album-style chapter list needs more work.
- Per-book resume is not solved by the binary shim alone. Stock `break_point = position` appears to be global/current-track state, while the desired behavior needs a per-book store that survives music playback and reboot.
- No package has been flashed.
- `tools/adb_live_test_patched_player.ps1` codifies the historical temporary relaunch test and requires `-IUnderstandThisRestartsUi` before it will stop the stock UI and launch the patched binary.
- Live relaunch test on 2026-06-09:
  - Patched binary ran from `/usr/data/codex_audiobook_test_20260609-082312/hiby_player`.
  - Process stayed alive, but the R1 screen was unresponsive.
  - Log showed framebuffer/DMA init errors (`Access hgl dma failed`, `Access fb failed`).
  - Recovery was successful with `adb reboot`.
  - After reboot, stock wrapper `/usr/bin/hiby_player.sh` and stock `/usr/bin/hiby_player` were running again, with stock player MD5 `ad69fa8377fb85b01ed5d65fe976b19a`.
- `tools/adb_runtime_patch_hiby_player.py` remains available for reproducing the historical RAM shim test. Its dry-run verifies the stock binary and selects the live `/usr/bin/hiby_player` PID without writing process memory.
- RAM patch test on 2026-06-09:
  - Plain `/proc/<pid>/mem` writes could read text memory but did not modify the read-only executable mapping.
  - A tiny MIPS ptrace helper successfully attached to the live stock player, wrote the patch words, detached, and left the UI responsive.
  - Live readback verified all patch regions:
    - scanner skip string at `0x75be70`
    - book audio code cave at `0x75daec`
    - dialog call NOP at `0x540b40`
    - book-row hook at `0x540b0c`
  - Selecting an audiobook from the Books/Audiobooks list started backend playback.
  - `r1_audiobook_resume_helper position` advanced from `125000` ms to `140000` ms, confirming the audio engine position is moving even though the visible list screen remained at `00:00`.
  - The app did not automatically transition from the Books/Audiobooks list to Now Playing.
  - The current media DB placed the played audiobook at the top of `HISTORY_TABLE`, but per-book resume is still not implemented.
  - Follow-up test patch at `0x75db64` changed the playback builder fourth argument from the shim's zeroed scratch metadata (`addiu $a3, $sp, 0xaa8`) to the likely Books row metadata pointer (`addiu $a3, $s1, -4`).
  - That follow-up caused an on-device `error 13 playback failure`.
  - The instruction was reverted in RAM to `addiu $a3, $sp, 0xaa8`, restoring the previous working shim behavior.
  - Additional RAM probes showed the selected Books path reaches the text-reader opener through callsite `0x540b0c`. Adding a stock Now Playing refresh event after the shim caused the player/device to reboot, so UI event injection in this path is unsafe.
  - A DB-only experiment added one audiobook (`Squirrel Seeks Chipmunk`) as a normal media row in `MEDIA_TABLE` and `MEDIA2_TABLE`, with catalog/count tables refreshed. After reboot, it appeared in Music and played correctly with the normal Now Playing screen and moving progress bar. The original on-device DB was restored from `/usr/data/usrlocal_media.db.codexbak_20260609-095510`; the successful test DB copy is saved locally at `work/db-experiment-success-20260609/usrlocal_media.with-audiobook-media.db`.

## Current Direction

The stable path is to treat audiobooks as real media rows and reuse the stock music playback UI, not to make Books/text-reader rows call the playback engine directly.

The remaining firmware work should focus on:

1. Getting `/Audiobooks/` audio into media-shaped database rows.
2. Hiding those rows from normal Music views.
3. Reusing an existing launcher entry (likely Books/Audiobooks) to open an audiobook-only media view.
4. Testing stock `Resume play from last -> Position` with the real-media audiobook path before adding a custom per-book resume store.

## Audiobook Media Row Generator

`tools/add_audiobooks_to_media_db.py` now builds audiobook media rows from an ADB scan, a manifest, or a local SD-card copy. The all-audiobook candidate DB generated from the connected R1 contains 135 audiobook rows across 6 book albums:

- Calypso
- Holidays on Ice
- Ice Like Fire
- Squirrel Seeks Chipmunk
- These Rebel Waves
- When You Are Engulfed in Flames

The candidate was installed as a reversible DB-only live test. After reboot/playback, the pulled live DB had integrity check `ok` and retained all 135 audiobook media rows. The live DB MD5 changed during playback, which is expected because stock firmware writes history/recent state into the same database.

For local SD-card copies, the generator can optionally read tags with `ffprobe`. The firmware-facing mapping is intentionally limited to fields the R1 media schema can use directly:

- `TALB` / album: book title
- `TIT2` / title: chapter or file display title
- `TPE2` / album artist: author
- `TPE1` / artist: author/narrator display value
- release date/year: media year
- track number: chapter order

The generated genre is forced to `Audiobook` even when the source tag has finer-grained genre values, because the separate Audiobooks route needs a stable stock media category key.

## Audiobooks Launcher Experiment

`tools/patch_hiby_player.py` now has an opt-in `--audiobook-launcher-genre` patch. It writes a small callback into the existing zero-filled code cave at file offset `0x35daec` and changes the Books launcher callback pointer at file offset `0x482030` from `0x53bb20` to `0x75daec`.

The current callback opens the existing stock `Genre -> Albums of Genre` media route with the hardcoded wide route string `genre\Audiobook` and a separate selected-genre argument pointing at `Audiobook`. This is deliberately UI-only: it does not alter playback code and relies on audiobook files already being present as normal media rows.

Earlier callback attempts were corrected after live testing showed two MIPS delay-slot mistakes, the wrong route shape, and a bad MIPS high-half load for the code-cave string. The working version uses `.set noreorder`, restores the stack in the `jr` delay slot, loads the route string at `0x75db80` with `lui $a2, 0x76; addiu $a2, $a2, -0x2480`, and stores the selected-genre pointer at the caller's fifth argument slot before calling `0x4f01c0`.

The same opt-in patch now also redirects the Books open function itself. It writes a root-argument helper at file offset `0x35dbc0` and patches the start of `0x540f20` at file offset `0x140f20` to jump there. This covers launcher paths that reach the Books app opener without using the original tile-specific callback.

The same patch set can be applied to the running stock process in RAM with:

```powershell
python tools\adb_runtime_patch_hiby_player.py --patch-set audiobook-launcher `
  --apply --i-understand-this-writes-process-memory
```

This is still experimental. A reboot discards the RAM patch.

Live RAM testing also showed that the launcher view keeps an instantiated heap copy of the Books tile. Patching only the static `.data` template at `0x892030` did not affect the already-running launcher object; the old callback pointer was also present in the heap at `0x114158c` during that session. For a flashed or startup-time patch the static template should be sufficient, but live RAM tests may also need the current heap object patched.

Live result: after patching the heap callback and Books opener in RAM with the corrected route, tapping the Books/Audiobooks launcher tile opened the media-backed Audiobooks section instead of the stock text-reader Books menu. With the split-catalog DB installed, the user confirmed that selecting a book opens its tracks/files, selecting a track starts playback, Now Playing opens automatically, and the progress bar moves.

Known back-stack limitation: entering Audiobooks from the main launcher opens the Audiobook title list directly, but the stock route keeps a Genres parent on the navigation stack. One Back returns to Genres and a second Back returns to the launcher. Post-flash RAM-only tests tried a direct-filter helper, `genre_all\Audiobook`, and `album\Audiobook`; the direct-filter helper did not open Audiobooks, `genre_all\Audiobook` still backed through Genres, and `album\Audiobook` produced a redraw glitch while still landing on Genres after Back. The current `genre\Audiobook` route remains the lowest-risk behavior.

## Split Music Catalog Live Result

The split-catalog DB installed on the device keeps audiobook rows in `MEDIA_TABLE` and `MEDIA2_TABLE`, but removes them from the normal Music catalog/search/count tables.

Checked installed DB after boot:

- Integrity: `ok`
- `MEDIA_TABLE`: 249 rows
- audiobook media rows: 135
- `SEARCH_TABLE` audiobook rows: 0
- audiobook albums in `ALBUM_TABLE`: 0
- `Audiobook` in `GENRE_TABLE`: 0
- `COUNT_TABLE`: `[114, 11, 4, 8, 4]`

Human-visible Music checks:

- Music -> Albums: no audiobooks shown.
- Music -> Genres: no `Audiobook` genre shown.
- Music -> All and Music -> Files/Explorer can still reach audiobook files, but only when deliberately browsing/selecting the Audiobooks folder. This is acceptable for the current target behavior.

## Resume Runtime Live Result

The on-device resume runtime now consists of:

- `tools/r1_audiobook_resume_daemon.sh`
- existing MIPS helper `r1_audiobook_resume_helper`
- installer `tools/adb_install_audiobook_resume_runtime.ps1`
- optional resume catalog from `tools/write_audiobook_resume_catalog.py`
- generated touchscreen Next stream from `tools/adb_inject_touch_event.py`

The daemon reads the current stock playback path from `/usr/data/user.ini` at offset `0x28`, reads playback position from a live `hiby_player` memory field by default, and stores per-book JSON records under `/usr/data/audiobooks/resume.d`. The live-memory source was added after DMR/socket instability made the older helper-read loop too fragile during idle and post-restart states.

The resume catalog is generated on the PC from the copied split media DB and pushed to `/usr/data/audiobooks/catalog.tsv`:

```powershell
python tools\write_audiobook_resume_catalog.py `
  work\live-db-before-split-20260609-105855\usrlocal_media.split-catalog.db `
  -o work\audiobook-resume-catalog.tsv

powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_install_audiobook_resume_runtime.ps1 `
  -CatalogSource work\audiobook-resume-catalog.tsv
```

The installer now defaults to save-only mode:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_install_audiobook_resume_runtime.ps1 `
  -CatalogSource work\audiobook-resume-catalog.tsv `
  -PositionSource memory
```

Use `-RestoreEnabled` only for deliberate restore tests. `tools/adb_test_audiobook_seek_restore.py` refuses to seek unless playback position is advancing.

The generated live catalog contains 6 books and 135 tracks. With the catalog installed, the live Sedaris multipart resume record now includes:

- `media_id: 1002`
- `track_index: 2`
- `track_count: 30`
- `chapter_title: "When You Are Engulfed in Flames 02/30"`

The daemon also has a 15-second new-track commit guard. If a saved multipart book is on one file and a different file from the same book starts, the daemon defers overwriting the saved record until playback of the different file reaches `AUDIOBOOK_NEW_TRACK_COMMIT_MS` (default `15000`). This protects the saved location from an accidental wrong-track tap while still letting intentional manual moves take over quickly.

Forward correct-file restore is now live-tested. The user tapped the stock Now Playing Next control while a multipart Sedaris audiobook was playing; an input capture showed a touchscreen event on `/dev/input/event1` at about `x=356, y=735`. Replaying that event changed tracks through the stock UI. `tools/adb_inject_touch_event.py` now generates the same 400-byte tap stream without depending on captured timestamps. In a controlled daemon test, the active file was `05-30.mp3` near 3 seconds and the saved record pointed at `07-30.mp3` at `65000` ms. The daemon logged:

- `track-restore start current=5 saved=7 steps=2`
- `track-restore next step=1/2 ... 06-30.mp3`
- `track-restore next step=2/2 ... 07-30.mp3`
- `restore ... saved_ms=65000`
- `after_position_response=66@66`

Live tests on 2026-06-09:

- Controlled restore: a saved `120000` ms record restored playback from about 10 seconds to 120 seconds.
- Music interruption: while an audiobook was saved around `303000` ms, the user played a normal music file, then returned to the same audiobook file. The daemon saw the audiobook restart around 2 seconds and restored to `303000` ms. The user confirmed it jumped to about the 5-minute mark.
- Next-file/chapter: pressing Next on the audiobook Now Playing screen advanced from `01-30.mp3` to `02-30.mp3`. The daemon updated the book record to track `02-30.mp3` and the new position.
- After a clean reboot, `/data/dmr_streamer` was healthy again and the guarded seek test verified `40s -> 44s`, then a seek back to `24s`. A daemon-level restore test then saved `Ice Like Fire` at `45504` ms, sought playback to about 6 seconds, restarted the daemon with `-RestoreEnabled`, and verified the daemon restored to `45@45`.
- Runtime storage check on 2026-06-10: the live runtime used about 2.1 MB under `/usr/data/audiobooks`, with about 11.7 MB still free on `/usr/data`. In the flashed build, the daemon/helper live in rootfs and `/usr/data` should mainly hold small per-book bookmark JSON files, so replacing the SD card should not stop normal player startup.
- Staging on 2026-06-10: `tools\adb_stage_verified_firmware.ps1` copied the verified custom package to `/usr/data/mnt/sd_0/r1.upt`, verified byte count, MD5 `4ba760f577dcb52e3a35d6d1117b5dfd`, and SHA256 `59f2450a06e4ebce76644505038f207abd113bebfd022f600c3c0d81ed38946d`. The previous stock recovery package was backed up as `/usr/data/mnt/sd_0/r1.upt.previous-20260610-104950.bak` with MD5 `494e7bfbd46d623ceb56938c042f576e`.
- Forward correct-file restore: with Sedaris `05-30.mp3` active, a saved Sedaris record for `07-30.mp3` at `65000` ms made the daemon replay two verified UI Next taps, land on `07-30.mp3`, and seek to `66@66`.
- Physical key mapping: a 25-second input capture mapped physical Next to `event0` code `163`, physical Previous to `event2` code `165`, and Play/Pause to `event2` code `164`.
- Backward correct-file restore: with Sedaris saved on `08-30.mp3` at `210506` ms, playback was moved to `10-30.mp3`. Restarting the daemon made it replay physical Previous twice (`10 -> 09 -> 08`) and then seek to `211@211`.
- Title-selection marker: the shared Genre -> Album list opener at `0x49FE40` records a sequence counter and the selected album pointer in scratch address `0x8E4000`, then executes the original opener prologue and continues at `0x49FE48`. The Audiobooks launcher/root helpers additionally write source magic `0xA0B00515` and the expected next title-list sequence into the same marker area, allowing the daemon to identify a title list opened by the Audiobooks launcher.
- Title-only start live tests: from the Audiobooks title list, tapping `Ice Like Fire` opened the track list, the daemon autostarted the first visible track with the 960-byte captured touch shape at `x=203,y=197`, playback switched to Now Playing, and the saved position restored to about `02:03`. Strict-guard tests have logged `reason=catalog`, `reason=context`, and, after the source-marker patch, `reason=launcher`.
- After stock recovery, the strict catalog pointer was layout-sensitive and rejected one title tap. Restarting the live daemon with `AUDIOBOOK_INTERVAL_SECONDS=1`, `AUDIOBOOK_BOOK_TITLE_AUTOSTART_DELAY_SECONDS=1`, and the temporary trusted-title-list guard made title playback start in about two seconds. The daemon now has a stricter `AUDIOBOOK_BOOK_TITLE_CONTEXT_SECONDS=300` fallback plus the launcher source marker, so normal Audiobooks title taps no longer need the global relaxed guard.
- A stale DMR socket inherited by `adbd` caused `bind socket error !!!` and `connect /data/dmr_streamer: Connection refused` after restarting `hiby_player` from an ADB shell. A normal reboot cleared the inherited socket and restored DMR helper behavior. The daemon now closes inherited socket fds on startup; a live ADB-launched restart closed 7 inherited sockets and left the daemon with no socket fds.

Important limitation: no package has been flashed yet. The live behavior is currently proven through RAM hooks, ADB-installed runtime files, and a rebuilt but unflashed development package.

## DMR Command Probe Result

`r1_audiobook_resume_helper` was straced to understand why it can read/seek while direct socket probes could not. It writes `1` to the `hiby_player` process at virtual address `0x00c34e14`, sends the DMR command over `/data/dmr_streamer`, then restores that value to `0`. That address is heap/layout-dependent and was unmapped after one reboot, so the daemon no longer uses the helper as its default position source.

Two probe helpers were added:

- `tools/adb_probe_dmr_command.py` temporarily patches the existing resume helper command string for short commands.
- `tools/adb_send_dmr_command.py` builds a tiny MIPS Unix-socket writer, opens the same gate with `/proc/<pid>/mem`, sends an arbitrary command, and closes the gate.

Tested gated commands that did not change the current local audiobook file:

- `next`
- `play@1`
- `play@1002`
- `set_uri:file:///usr/data/mnt/sd_0/...03-30.mp3`
- `set_position@3@0` to `/data/dmr_streamer`
- `set_position@3@0` to `/data/dmr_control`

Conclusion: the DMR socket path is suitable for position and seek, but not for selecting/switching local media rows in the current stock local-playback mode. Correct-file auto-resume now uses verified physical Next/Previous key packets for track correction. Title-only book selection uses a UI/media-list marker plus a guarded first-track tap and has passed a visible title-list live test.

## Direct Play-Open Research

The next resume improvement target is the stock shared media-open function at
`0x49e200`. If we can capture and later recreate the arguments used when the
stock UI opens a selected media row, multipart audiobook resume may be able to
jump directly to the saved file instead of visibly tapping through the track
list.

The first live play-open probe used `0x75df00` as a code cave and caused a live
R1 reboot. `tools\r1_hiby_player_cave_audit.py` now audits executable
zero-filled regions in an extracted `hiby_player` ELF before any new RAM probe
is attempted. Against the current `1.6.17-ui-dev` binary it reports:

- `0x75df00` is file offset `0x35df00`, in an executable file-backed mapping,
  and all zero, but remains blocked as known-bad from live testing.
- Current audiobook launcher/book-open/title-marker helpers occupy nearby
  addresses under `0x75daec` through `0x75de80`.
- A cleaner candidate such as `0x760708` has about `0x3d0` zero bytes in the
  same executable file-backed mapping, while the play-open probe body is only
  about `0x70` bytes.

`tools\adb_probe_music_row.py arm-play-open` now requires an explicit audited
`--play-open-probe-addr`. It refuses the known-bad address, refuses overlap with
current audiobook helper caves, checks `/proc/<pid>/maps` for an executable
`hiby_player` mapping, and verifies the chosen range is still zero before
patching. The next connected-device experiment should use this guarded path,
then compare recorded register/object values from music and audiobook row
selection before attempting any direct invocation.

Live test update on 2026-06-16: the guarded `0x760708` code cave armed on the
installed `1.6.15-audiobook` player, but the device rebooted shortly after UI
navigation. Post-reboot memory inspection found that the old play-open scratch
buffer at `0x8b2100` was not empty (`0x120` bytes contained six non-zero bytes),
so the tracer's pre-arm scratch clear likely zeroed live player state. Clean
high-BSS candidates `0x8e4200`, `0x8e4400`, and `0x8e4800` were all zero after
the reboot. The play-open tracer now uses `0x8e4400` and refuses to clear any
scratch range unless it is writable and already zero.

Follow-up inspection also found the older music-row probe scratch at `0x8b1f00`
was not empty (`0x120` bytes contained 55 non-zero bytes). That probe now uses
`0x8e4600`, and the music-row plus album-marker probes now apply the same
writable/zero scratch guard before writing to the live player process.

After moving scratch to guarded high-BSS addresses, a no-UI arm/read/restore
smoke test passed with ADB still connected. A live track-list test then captured
the stock call at `0x49e200` while selecting `The Road` audiobook tracks:

- Row 1 tap: `a0=0x00ff6ca4`, `a1=0x7fd4f680`, `a2=0`,
  `a3=0x010941c0`, `ra=0x0049ff8c`.
- Row 3 tap: same `a0`, `a1`, and `a3`, but `a2=2` and incoming `s1=2`.
- The `a0` object begins with ASCII
  `vg_listview_songs_of_an_album_and_a_genre`.

This strongly suggests that `0x49e200` can open a track by list-view object plus
selected zero-based row index. The next direct-resume experiment should happen
while the saved book's track list is open: call or trampoline into `0x49e200`
with the captured current list-view context and the saved track index in `a2`.
If the function accepts an off-screen index, multipart resume can jump directly
to the correct file without row swipes or transport stepping. If it only accepts
loaded/visible rows, this still gives a cleaner target for reducing the current
visible-row workaround.

Follow-up live result: `tools\adb_probe_music_row.py` now has
`arm-play-open-override`, which records the original call but forces `a2` to a
chosen zero-based row index before replaying the stock prologue. Two tests
passed on the installed `1.6.15-audiobook` player:

- Visible control: from `The Road` track list, tapping row 1 while forcing
  `a2=4` opened `The Road (2007) - pt05`, showing `5/5`.
- Off-screen proof: from `The Remaining Aftermath` track list, tapping visible
  row 1 while forcing `a2=20` opened `Aftermath 20-43`, showing `21/44`.

This proves the stock open function accepts off-screen row indexes from the
loaded book track list. The production path is now feasible: when the daemon
sees a saved multipart book's track list, it can arm a one-shot row-index
override for `0x49e200`, tap the first visible row, and let the stock function
open the saved part directly. That should remove the audible/visible stepping
through prior tracks.

Native helper update on 2026-06-16: `tools\r1_audiobook_direct_open.c` packages
the proven override as a static MIPS helper. It validates the audited executable
probe cave (`0x760708`), validates the guarded writable scratch range
(`0x8e4400`), patches the next `0x49e200` call only, waits for the call or a
timeout, restores the stock prologue, clears the probe cave, and exits. A live
no-tap test on the installed `1.6.15-audiobook` player armed, timed out after
500 ms, and restored cleanly. A live track-list test from `The Remaining
Aftermath` then kept the helper running through ADB, forced zero-based row
`20`, tapped visible row 1, and opened `Aftermath 20-43`, showing `21/44`.
The helper logged `direct-open called count=1 original_row=0 override_row=20
restored`.

The daemon now has a direct-open fast path before the older swipe/tap fallback.
Firmware candidate `1.6.18-directopen-dev` includes the helper in `/usr/bin`,
copies it to `/usr/data/audiobooks/bin` at boot, and enables it with
`AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED=1`. Local verification passed and the
package was staged to the SD card as `/usr/data/mnt/sd_0/r1.upt`.

Post-flash testing of `1.6.18-directopen-dev` exposed a packaging mismatch: the
DB watcher invoked the new title/author/series sidecar flags, but the firmware
contained an older DB helper binary that rejected those options and exited with
`rc=2`. Rebuilding the MIPS helper and installing it live fixed the boot run and
generated `catalog-view-title.tsv`, `catalog-view-author.tsv`, and
`catalog-view-series.tsv` from the device DB. The verifier now checks the
helper binary for those flags before a build can pass.

Corrected package `1.6.19-directopen-fix-dev` was built and locally verified on
2026-06-16:

- UPT: `work\audiobook-firmware-1.6.19-directopen-fix-dev\r1-audiobooks-1.6.19-directopen-fix-dev.upt`
- UPT MD5: `c4c89309f5f60c16d9b587d29de7fdee`
- UPT SHA256: `ed1edc7770229ad957306525ad9a9848226881ea71e28ec967cc7805ca0c7238`
- Rootfs MD5: `4cb1cc46aa55bfa9d437e7be19d13e60`
- Rootfs SHA256: `843b804d792bf6b8e92c7411ad2d444a62e5708c65b003ff799773240535c7fa`

ADB disappeared before this corrected package could be staged to the SD card.
When ADB returns, stage it with `tools\adb_stage_verified_firmware.ps1`; that
script now prefers the repo-local `.tools\platform-tools\adb.exe` if the old
system ADB path is not present.

## Full Development Package

An opt-in full development package was built with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_r1_audiobook_firmware.ps1 `
  -OutDir work\audiobook-firmware-1.6.2 `
  -OutputUpt work\audiobook-firmware-1.6.2\r1-audiobooks-1.6.2-audiobook.upt `
  -IncludeAudiobookLauncherGenre `
  -IncludeAudiobookTitleAutoStartMarker `
  -IncludeAudiobookResumeRuntime `
  -AudiobookResumeCatalog work\audiobook-resume-catalog.tsv
```

The rebuilt package was verified with:

```powershell
python tools\verify_r1_audiobook_build.py --expect-current-hashes
```

Mode-preserving package, locally verified but not flashed:

- UPT: `work\audiobook-firmware-1.6.2\r1-audiobooks-1.6.2-audiobook.upt`
- UPT MD5: `1022a7dfe0cb9e73f35494e81f45b58f`
- UPT SHA256: `9138fd1e91c008205f81857095c50341898d535fae11cc42edec6ed12556e519`
- Rootfs MD5: `72f66d82f9647ea2faa83457d3301c90`
- Rootfs SHA256: `da8e19dfc5ff085ea53969cd2d740fd5b66facbe279ec328f9082405e4086030`
- Player MD5 inside rootfs: `09997a636c94112ff76c85a6d4a8d0ff`

`tools\verify_r1_audiobook_build.py` now compares all 5488 stock paths and all 482 stock symlink targets from `work\original\rootfs.squashfs` against the rebuilt rootfs, requires all rebuilt entries to be root-owned, and checks the added audiobook runtime file modes plus the launcher source-marker bytes, direct-selector packets, the About-screen label `HiBy R1 Audiobook FW 1.6.2`, `/usr/resource/config.json` product version `1.6.2-audiobook`, `/etc/r1_audiobook_version` marker, and the BusyBox-safe resume init script. The first custom install booted successfully and showed the custom version marker, but `/etc/init.d/S91audiobook_resume.sh` used a `case` form that the R1 shell rejected with `unexpected newline (expecting ")")`; the fixed build removes that `case` block. The second custom install booted and parsed the init script, but BusyBox `start-stop-daemon -x script` created a pid file for a daemon that exited before logging. The current build launches `/bin/sh -- r1_audiobook_resume_daemon.sh`, which was confirmed live to log and stay running, and starts the daemon with the same tuned live environment. This build includes the launcher-source-marker title autostart path, replacing the global trusted-title-list fallback, and includes the daemon reset needed when a title tap restarts the same file that was already playing. Multipart track correction polls for each stock Next/Previous transition instead of waiting a fixed full settle window, and the new-track overwrite guard is now 15 seconds. The daemon can also right-swipe out of Now Playing, use timed list swipes and row taps to land near the saved part, then recompute fallback steps from the actually selected track. In live Sedaris testing the list reopened near 08/30 and restored to 10/30 with two Next presses; a later ADB-driven title-list tap landed near 08/30 and restored to the saved 11/30 with three Next presses. When a stale DMR socket refused the time seek, the failed-restore save guard protected the deeper saved position. A follow-up UI-seek fallback reads live track duration from `hiby_player` memory, verifies that the framebuffer row matches the Now Playing seek bar, computes the progress-bar coordinate, emits a short touchscreen tap, and verifies the restored position from memory; live testing restored track 13 to `811153` ms for an `808889` ms target after DMR refused. The daemon now marks a book completed when final-track playback is within 45 seconds of the end, skips resume for completed books, and clears completion when the user starts the book again; this passed a live `Holidays on Ice` synthetic-completed start-over test. The framebuffer guard counted 439 matching seek-bar pixels on Now Playing, 82 on title-list captures, and 0 on track-list captures, with the live default threshold set to 300. The daemon also closes inherited socket fds on startup to avoid participating in stale DMR socket inheritance, backs off repeated failed DMR seek restores from 30 seconds toward 5 minutes, throttles failed-restore save-guard logs to one line per 30-second playback bucket, buckets title-start wait/skip logs to 5-second intervals, and resets title-autostart state only once per title-tap sequence. Future staging should use `tools\adb_stage_verified_firmware.ps1`, which runs the local verifier first, refuses the known-bad MD5s, verifies the remote temp file, backs up an existing different target file, renames the uploaded file to `r1.upt`, then verifies the final byte count plus MD5/SHA-256 when available.

Previous `1.6.1-audiobook` post-flash verification on 2026-06-10: the package installed cleanly, and the user manually re-enabled ADB for verification. `/etc/r1_audiobook_version` and `/usr/resource/config.json` both reported `1.6.1-audiobook`; the visible About page showed `HiBy R1 1.6.1-a`, apparently truncating the longer suffix. `/etc/init.d/S91audiobook_resume.sh` parsed cleanly on-device, had zero CR bytes when pulled back to Windows, and started the resume daemon from init as PID 1014 with a fresh `12:26:03` start log. `/usr/data/user.ini` had the saved-media slot nulled after boot, and a framebuffer capture after backing out of About showed the main launcher instead of reopening the Engulfed audiobook. A post-flash ADB smoke test opened Audiobooks from the launcher and started `Squirrel Seeks Chipmunk` from the title list; the daemon logged `book-title autostart reason=launcher`, playback reached Now Playing with a moving progress bar, and the test paused playback afterward. That smoke test left a real `Squirrel Seeks Chipmunk` resume record at `40751` ms. The SD-card update trigger was then renamed from `r1.upt` to `r1-audiobooks-1.6.1-audiobook-installed-20260610.upt`, preserving the verified package without leaving the updater trigger active.

Known bad artifacts:

- UPT: `work\audiobook-firmware-full-dev-fixed\r1-audiobooks-full-dev-fixed.BAD-black-screen-20260609.upt`
- UPT MD5: `3bed523d5843522186164029139db7b1`
- UPT SHA256: `5bb628f33f0f0239333f6eb04988f5593fdc6c7fb919ecec3a63770c260f6042`
- Rootfs MD5: `4b5340f2eea44b5893fb92b70180e111`
- Rootfs SHA256: `a70ae7c3625c3d5a7850e4a2807ecb15566cd3587e810764911dbe6e41625dde`
- Player MD5 inside rootfs: `4a1729fbb3c8cff520487e75317aa0ac`
- Resume helper MD5 inside rootfs: `7d025ddc44b69ee27c83358b8df4f45d`
- Resume daemon MD5 inside rootfs: `68637068425a23cb1719e87720ab5b00`
- Touch event MD5 inside rootfs: `49a00ac268c37fd2911c61ccf7a03f39`
- First-track touch event MD5 inside rootfs: `f5cf6462f609a54b17d2c5bfd0461e67`

This package was flashed on 2026-06-09. The updater reported success, but the R1 rebooted to a black screen with no ADB. It is quarantined locally and its MD5 is blocked by `tools\adb_stage_verified_firmware.ps1`. Follow-up inspection found broader mode/ownership drift in the repacked rootfs; for example, `/bin/busybox` was not preserved as stock `-rwsr-xr-x`.
- Next key event MD5 inside rootfs: `d939a6191a85f125d5bb62dde728d603`
- Previous key event MD5 inside rootfs: `909ba365650d77c290159f85cabcfb5c`
- Seed catalog MD5 inside rootfs: `01a163da3a9a00a874e464a5b180eb20`

The earlier full-dev package flashed on 2026-06-09 is also known bad and has been quarantined as `work\audiobook-firmware-full-dev\r1-audiobooks-full-dev.BAD-nonexec-hiby-player.upt`. It booted to a black screen because `/usr/bin/hiby_player` was repacked as `-rw-r--r--`; stock is `-rwxrwxr-x`. The current mode-preserving build uses `mksquashfs -all-root -pf` with escaped paths for filenames containing spaces.
