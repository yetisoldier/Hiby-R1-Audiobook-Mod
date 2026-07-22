# Library scan, chapters, and storage

The moov-mmap scan-hang fix, M4B chapter stsc parsing, SQLite-on-exFAT safety,
the chronic `/usr/data` near-full partition and the app-level guards, and the
SD-primary position/bookmark store. Source:
[`audiobook_app/scan.c`](../../audiobook_app/scan.c),
[`audiobook_app/tags.c`](../../audiobook_app/tags.c),
[`audiobook_app/library.c`](../../audiobook_app/library.c),
[`audiobook_app/player.c`](../../audiobook_app/player.c) +
[`audiobook_app/posstore.h`](../../audiobook_app/posstore.h),
[`audiobook_app/bookmark_sd.c`](../../audiobook_app/bookmark_sd.c).

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

Confirmed end-to-end: scan completed the WHOLE library. DB: 52 books, 298
tracks, 993 chapters (up from 5 books when it hung). The 15.3 MB book = 80
chapters (was 0 — the hung book; now parses via mmap). 19 books with 0
chapters — ALL single-file MP3 audiobooks (MP3 has no embedded chapter track
so `embedded_chapters=0` is correct/expected). No M4B is missing chapters.

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
which re-parses and replaces the 1-chapter with 58. Multi-file MP3 books keep
their synthesized 1-chapter-per-track (unchanged).

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
`/usr/data/mnt/sd_0/.audiobook_pos/` via tmp+rename. `save_progress` calls
`pos_save_sd` (authoritative) and then attempts `audiobook_save_progress`
(best-effort library.db mirror). If the refresh worker owns the DB writer lock,
the mirror is skipped instead of blocking playback. `cmd_play` resume reads `pos_load_sd`
first, falls back to `library.db` only for pre-2.0.9 positions (migration).
Bookmarks (v2.0.16) are likewise SD-primary —
`/usr/data/mnt/sd_0/.audiobook_pos/<book_id>.bm`, atomic temp+rename; the
`bookmarks` DB table is INERT; a one-time per-book DB→SD migration runs on
first bookmark-screen open. See [wsola_seek_resume.md](./wsola_seek_resume.md)
for the resume-side details.

The point: a full `/usr/data` can NEVER lose the place or refuse a bookmark.
The SD store is authoritative; the DB is a best-effort mirror.
