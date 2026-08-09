# Library scan, metadata, chapters, and storage

The metadata and publisher-description scan, moov-mmap scan-hang fix, M4B
chapter stsc parsing, SQLite-on-exFAT safety, the chronic `/usr/data` near-full
partition and the app-level guards, and the SD-primary position/bookmark store.
Source:
[`audiobook_app/scan.c`](../../audiobook_app/scan.c),
[`audiobook_app/tags.c`](../../audiobook_app/tags.c),
[`audiobook_app/library.c`](../../audiobook_app/library.c),
[`audiobook_app/music_catalog.c`](../../audiobook_app/music_catalog.c),
[`audiobook_app/player.c`](../../audiobook_app/player.c) +
[`audiobook_app/posstore.h`](../../audiobook_app/posstore.h),
[`audiobook_app/bookmark_sd.c`](../../audiobook_app/bookmark_sd.c).

## Stock Music catalog isolation (v2.0.x)

The native audiobook catalog and HiBy's stock Music catalog are independent.
HiBy's Update Database scanner still walks the whole SD card, so a fresh stock
database can contain both `/Music` and `/Audiobooks` even though the native app
correctly scanned its own `library.db`. Older development devices could hide
this because a pre-2.0 maintenance build had already cleaned their stock DB.

`music_catalog.c` fixes that boundary on every Audiobooks app entry. A
short-lived background worker opens each existing HiBy DB location
(`/usr/data/usrlocal_media.db`, `/data/usrlocal_media.db`, and the SD-root
copy), deduplicates aliases by device/inode, and removes only root
`/Audiobooks` paths in a transaction. It then reconciles the stock Music
search rows, named catalog counts, format counts, total counts, and time
indexes. Missing copies are normal. A just-finished stock scan lock is retried
three times. There is no resident watcher and therefore no idle polling,
memory, or battery overhead.

Host regression coverage is in `tools/test_music_catalog_cleanup.py` and
`tools/test_music_catalog_cleanup.ps1`. It always runs a committed synthetic
HiBy-schema fixture (including shared music/audiobook metadata) and also uses
captured device DBs when available. It verifies zero audiobook leakage,
preserves every legitimate Music row exactly, checks catalog counts and SQLite
integrity, and verifies that a second cleanup is a no-op.

## moov mmap fix — scan hang on big-moov M4B (v2.0.4)

Symptom: audiobook scan (Home → Refresh) hung partway through. M4B `moov`
sizes for a real library (moov-at-start, offset 28): 9.0 / 7.7 / 11.1 / 11.8
MB (all parsed OK), **15.3 MB (HUNG)**, 13.5 MB. Two more were moov-at-end
(~1.5 GB offset).

**Root cause:** `read_moov` (tags.c) loaded the ENTIRE moov atom into a
`malloc`'d buffer + `read()` the whole thing. The device has ~14-19 MB RAM free
during scan; `malloc(15.3 MB)` overcommit-succeeds, then `read()` faulting in
15.3 MB with only ~14 MB free → memory-pressure thrash (40 s+ stall, NOT a
clean OOM-kill — the process stayed alive = frozen screen). The threshold is
~free-RAM; books ≤12 MB fit, 15 MB tipped over. NOT an infinite loop —
`parse_qt_chapter_track`/`parse_stts/stsz/stco/stsc` are all bounded with 1 M
guards + 4096/256 caps; `audio_read_tags` for M4B only calls the fast 256 KB
`parse_m4b`.

**FIX (tags.c):** `read_moov` now **mmaps** the moov's file region
(`PROT_READ`, `MAP_PRIVATE`, page-aligned offset) instead of malloc+read.
The parser only dereferences atom headers + the small chapter-track sample
tables (stts/stsz/stco/stsc) — it never touches the multi-MB audio stsz/stts
bodies — so only those pages fault in. A 15 MB moov costs ~few KB of RAM, not
15 MB. Signature changed to `read_moov(path, &out_len, &out_map,
&out_map_len)`; the caller `audio_read_chapters` does
`if (map_base) munmap(map_base, map_len); else free(moov);`. Fallback: if mmap
returns `MAP_FAILED`, malloc+read small moovs only (≤`MOOV_MALLOC_MAX` = 8
MB) and leave `*out_map = NULL`; large moovs return NULL → placeholder
chapters. A mmap failure can never re-introduce the hang.

Validated on-device BEFORE flashing (de-risked): cross-compiled
`work/scan-hang-probe/probe_mmap.c` (Zig `mipsel-linux-gnueabihf.2.22`, DYNAMIC
— static NOT supported for this glibc target; 4 KB binary, push to `/tmp`,
`chmod 755`). Ran against the 15.3 MB moov: `mmap OK: 16777216 bytes at
0x75fa0000`, scattered reads OK, `bytes[28..35]=00 e8 c9 26 6d 6f 6f 76`
(moov size `0x00e8c926` = 15,255,846 + type "moov"). `MemAvailable` after
faulting 4 pages: ~17.5 MB (only ~2 MB drop including the probe process) →
16 MB mmap cost ~16 KB RAM, not 16 MB.

### exFAT read-only mmap works on this kernel
4.4.94+ in-tree Samsung exfat (NOT FUSE — mount opts
`bps=512,errors=remount-ro,delayed_meta`). The exFAT/sqlite concern was about
*locking*, not read-only mmap of a data file.

### mmap offset / LFS note
No `-D_FILE_OFFSET_BITS=64` in the build; `off_t` is 32-bit. Fine for this
library — all M4B files <2 GB, moov-at-end offsets (~1.5 GB) are under the 2.1
GB signed-32-bit limit. A future >2 GB moov-at-end file would return NULL →
placeholder (acceptable, no hang).

Pre-MP3-CHAP validation: scan completed the WHOLE library. DB: 52 books, 298
tracks, 993 chapters (up from 5 books when it hung). The 15.3 MB book = 80
chapters (was 0 — the hung book; now parses via mmap). 19 books with 0
chapters because the parser did not yet read MP3 ID3 chapter frames. No M4B
was missing chapters.

## M4B chapter extraction — QT chapter trak packs all samples in ONE chunk

Each M4B showed 1 "Opening Credits" chapter spanning the whole file. An M4B
has TWO traks — audio + a QuickTime chapter trak (`hdlr` subtype `text`, with
`gmhd` text-media header). The chapter trak's sample tables: `stts` = 58
entries (one per chapter), `stsz` = 58 sizes, but `stco` = **1 chunk offset** —
all 58 text samples packed into a SINGLE chunk (`stsc` = 1 run, 58
samples/chunk). Old code did `n = min(n_dur, n_sz, n_off) = 1` and assumed
one-sample-per-chunk (stco offsets map 1:1 to samples), so it emitted only the
FIRST sample ("Opening Credits", 0..track_dur). The other 57 `stts` durations
were ignored.

Nero `chpl` is ALSO present (under `moov/udta/chpl`, size 1113) and DOES
contain real names, BUT this file's `chpl` body is NON-STANDARD — no
`version+flags+count` header; the body starts mid-string `"g Credits"` (tail
of "Opening Credits"). `parse_nero_chpl` reads `count = qt_read32(body+4)` =
`"Cred"` = 1,132,635,236 > 4096 → returns 0. So `chpl` contributes nothing; the
QT track is the canonical source. Fixing the `chpl` parser is NOT worth it.

**FIX (tags.c):** Added `parse_stsc` (sample-to-chunk: runs of
`first_chunk`/`samples_per_chunk`/`sample_desc_index`). `parse_qt_chapter_track`
now resolves per-sample file offsets via stsc+stsz+stco: walk samples, track
current chunk + bytes-within-chunk; sample `i`'s offset = `stco[chunk] +
sum(sizes of earlier samples in the same chunk)`; advance the run when the
chunk index crosses `first_chunk[run+1]`. `n = n_dur` (capped 4096), require
`n_dur`/`n_sz` nonzero; fall back to legacy 1:1 only if `stsc` is absent.
Validated in Python BEFORE building: produces 58 chapters — "Opening
Credits" (0..28474 ms), "Chapter 1" (28474..694880 ms), … "Epilogue"
(33586270..34064090 ms). Static arrays (`deltas/sizes/offsets/sample_off[4096]`,
stsc `fc/spc/sdi[256]`) — no malloc, no OOM risk.

### Gotcha — chapters are DB-cached at scan time
`scan.c`: `delete_chapters_for_track` then `audio_read_chapters` →
`upsert_chapter` per chapter, all at SCAN time. Flashing the fix alone does
NOT repair existing rows — the user must **tap Home → Refresh** to re-scan,
which re-parses and replaces the 1-chapter with 58.

## MP3 ID3 CHAP/CTOC extraction (v2.0.25 development)

Some single-file MP3 audiobooks contain real chapter metadata in ID3v2.3 or
ID3v2.4 `CHAP` frames, optionally ordered by a top-level ordered `CTOC`.
Earlier builds ignored those frames and exposed one placeholder for the whole
file.

`tags.c` now streams the outer ID3 tag frame-by-frame. Large `APIC` cover-art
frames are skipped with `fseeko` rather than loaded into RAM. Only bounded
`CHAP`/`CTOC` payloads are read:

- 64 MiB maximum declared ID3 tag size
- 4,096 maximum outer frames inspected
- 64 KiB maximum individual CHAP/CTOC payload
- 512 maximum chapters retained

Nested `TIT2` supplies the chapter title; missing titles become `Chapter N`.
Invalid end times are derived from the next chapter start or track duration.
Top-level ordered CTOC children are honored, with timestamp ordering as the
fallback. Tag-level unsynchronization and compressed/encrypted frames are
rejected instead of allocating an unbounded rewrite buffer.

On-device validation after Refresh: 52 books, 298 tracks, 1,150 chapter rows.
Six single-file MP3 books exposed embedded chapters, including 23 for *Day By
Day Armageddon* and 31 each for *Trilobyte* and *Southlands*. Tapping Chapter 2
in *Day By Day Armageddon* issued one direct seek to 3,055,746 ms and began
playback at 50:55. MP3 files without `CHAP` retain the existing fallback:
multi-file books expose one chapter per file, while a single file exposes one
placeholder.

As with M4B metadata, users must select **Refresh Library** after installing a
build that adds or changes chapter parsing.

## Indexed Folders navigation (v2.0.25 development)

Folder taps previously caused three synchronous catalog passes: a touch-time
re-query, a parent rebuild, and then a child rebuild. Since framebuffer panning
runs on the same event thread, a large catalog could look black or frozen while
audio continued.

Folder selection now uses the row already held in the render cache, updates the
destination before rebuilding, and performs one indexed subtree query using
`idx_books_root_path`. On-device navigation through four nested levels rebuilt
each level in 1-2 ms. Logs also report folder rows hidden by the 128-row cap,
path segments beyond 255 bytes, and refused overlong paths.

## Publisher descriptions (v2.0.24)

The detail screen can show the publisher summary without loading media metadata
while the user scrolls. The scanner extracts and normalizes one description per
book, then stores it in a small separate table:

```sql
CREATE TABLE IF NOT EXISTS book_metadata(
  book_id INTEGER PRIMARY KEY REFERENCES books(id) ON DELETE CASCADE,
  description TEXT NOT NULL DEFAULT ''
);
```

The table remains separate from `books` so libraries without descriptions do
not enlarge the hot title-list rows or their render cache. During a scan,
`scan.c` keeps the first non-empty description found among a book's tracks and
upserts it after the book and tracks are cataloged. Orphan cleanup removes the
row automatically through the foreign-key cascade.

Supported sources:

- MP3: ID3v2 `COMM`, including its encoding, language, and short-description
  prefix.
- M4B: `moov/udta/meta/ilst/ldes`, falling back to `desc`.

`tags.c` converts supported UTF-8/UTF-16 text, strips common HTML markup,
decodes common entities and Audible punctuation, collapses whitespace, and
caps the stored text at 2047 bytes. Malformed or unsupported metadata is
ignored; it cannot block the rest of the book scan.

Descriptions are scan-time metadata, like chapters. After upgrading an existing
catalog to v2.0.24 or later, run **Audiobooks -> Refresh Library** once to
populate `book_metadata`. Books with no usable description continue to use the
same detail layout and simply leave the description area blank.

## SQLite-on-exFAT PROVEN safe (DELETE journal, no WAL)

`work/cover-probe/probe_sqlite_exfat.c` over the project's `sqlite3.c`
amalgamation: small + 6 MB write, close+reopen durability all pass on the
real SD mount. Stock uses DELETE journal (no WAL — no `-wal`/`-shm` files). So
position-save on SD is safe (v2.0.9+).

## `/usr/data` UBIFS (36 MB) chronic near-full — the root cause of "tile won't open" + "scan stalls"

Why `/usr/data` (not SD) holds the DB:
1. SQLite needs a real POSIX FS with byte-range locking + journal — SD is
   exFAT (known corruption risk via the FUSE exfat driver); `/usr/data` is
   UBIFS (journaled, wear-leveled, power-fail safe).
2. SD is removable/unmountable (USB mass storage, physical swap) — DB on the
   card would vanish on unmount.
3. Position saves every few seconds need a power-fail-safe FS.

Stock HiBy music app does the same (writes `usrlocal_media.db` to BOTH
`/usr/data` and SD).

Real `/usr/data` consumers (NOT cover cache — that's dead code):
- Dev cruft (one-time, won't recur in production — the production hook lives
  in read-only rootfs `/usr/lib/libaudiobook_hook.so`).
- **Stock music DB `usrlocal_media.db` ~5.3 MB, bounded by music-library size,
  REBUILDS on `/usr/data` on every music scan** (recurring). The audiobook
  `library.db` is <1 MB.

`/usr/data` is mostly unreclaimable UBIFS overhead (~26 MB of 36 MB) + the 6 MB
music DB that rebuilds there. **`/usr/data` cannot be durably freed.** After
cleanup + reboot, `/usr/data` was STILL 92% full (only 2.9 MB free) — the
reboot triggered the stock music scanner, which rebuilt `usrlocal_media.db`
back onto `/usr/data`, re-consuming the freed space. The partition is
chronically near-full because of the recurring stock music DB.

### Gotcha — the symlink approach is DEAD
Symlinked `/usr/data/usrlocal_media.db` → an SD copy worked briefly, but after
a music scan post-reboot, `/usr/data/usrlocal_media.db` went from symlink → a
6 MB regular file (mtime 15:45), `/usr/data` back to 1.7 MB free. The scanner
insists on a regular file (it `unlink`s + recreates on every scan, clobbering
any symlink). Can't redirect the 6 MB DB off `/usr/data`. Abandoned.

### Guards added (v2.0.3, APP-LEVEL ONLY — no boot/PMIC/mount/binary risk)
1. Pre-scan free-space guard: `statvfs(AUDIOBOOK_DATA_DIR)` at the top of
   `audiobook_scan_library`; `SCAN_MIN_FREE_BYTES = 1*1024*1024` (lowered
   from 2 MB because the stock music DB keeps `/usr/data` chronically near-full
   ~1.8 MB free — a 2 MB guard would false-block almost always; 1 MB still
   catches a genuinely-full partition). If free < threshold →
   `progress(5,0,0,"storage full")` + return -1 (best-effort: a `statvfs`
   failure falls through, lets sqlite surface the real write error).
2. `sqlite3_exec(db, "VACUUM", NULL, NULL, NULL)` after
   `audiobook_cleanup_orphans` — compacts `library.db` after prunes so
   card-swap pruned rows don't bloat on the tiny partition. Best-effort (needs
   ~DB-size free temp; DB <1 MB, ≥1 MB free at scan start).
3. UI loud failure: `HOME_REFRESH` captures `audiobook_scan_library`'s return;
   on `<0` sets `refresh_err_until_ms = now_ms()+3500` → a red "Scan failed:
   storage full" flash (`COL_RED`) instead of the green "Library refreshed".
   `uint64_t refresh_err_until_ms` in `ui_state_t`.

### v2.0.22: nonblocking, transactional refresh

Refresh no longer runs on the UI/event thread. `scan_worker_main` opens a
private SQLite connection and performs the scan there while touch, keys,
framebuffer drawing, and audio continue. The event thread polls a small guarded
result structure and rebuilds render caches only after the worker has closed its
connection. Cover prewarming pauses during the scan to preserve RAM and SD I/O.

The scanner wraps catalog mutation in `BEGIN`/`COMMIT`; every error path runs
`ROLLBACK`. A failed or full-storage scan therefore leaves the previous complete
catalog instead of a half-updated database or stale journal. The app also waits
for an active worker before tearing down UI/DB state, avoiding the exit-time
black screen seen when a refresh outlived the app.

Catalog writers share `g_db_write_lock`. Progress is different: the SD `.pos`
write happens first and remains authoritative, then `audiobook_db_write_trylock`
attempts the optional DB mirror. If Refresh owns the writer lock, the mirror is
skipped rather than blocking the decoder.

### The scan ALREADY self-cleans (scan.c)
`upsert_book`/`upsert_track` use `INSERT ... ON CONFLICT(book_key) DO UPDATE`
(re-scan overwrites, no duplication); `audiobook_cleanup_orphans()` runs at the
end of every scan, stats every book's `root_path` + every track's `path`,
DELETEs the ones whose files no longer exist (cascades via `ON DELETE
CASCADE` to tracks/chapters/progress/bookmarks). So the audiobook DB is
bounded by the CURRENT library — it does NOT accumulate across scans/card
swaps.

## SD-primary position + bookmark store

`pos_save_sd` writes `<book_id>.pos`
(`track_ordinal/track_pos_ms/book_elapsed_ms/completed/ts`) to
`/usr/data/mnt/sd_0/.audiobook_pos/` via tmp+rename. Since v2.0.23, the
authoritative sidecar checkpoints every 15 seconds and the best-effort
`library.db` progress mirror runs at most every 60 seconds. Pause, stop,
completion, and app exit still mirror immediately. If the refresh worker owns
the DB writer lock, the mirror is skipped instead of blocking playback.
`cmd_play` reads `pos_load_sd` first and falls back to `library.db` only for
pre-2.0.9 positions during migration.
Bookmarks (v2.0.16) are likewise SD-primary —
`/usr/data/mnt/sd_0/.audiobook_pos/<book_id>.bm`, atomic temp+rename; the
`bookmarks` DB table is INERT; a one-time per-book DB→SD migration runs on
first bookmark-screen open. See [wsola_seek_resume.md](./wsola_seek_resume.md)
for the resume-side details.

The SD sidecars are authoritative; the DB progress row is a best-effort mirror.
See [sd_runtime_stability.md](./sd_runtime_stability.md) for the app-scoped
runtime-power hold that protects this SD-backed state while Audiobooks is open.
