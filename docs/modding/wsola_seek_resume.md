# WSOLA speed, seek, and resume

How the app does pitch-preserved playback speed (WSOLA), the MP3 1.5x cutoff
fix, smart rewind on resume, save-on-quit, and the SD-primary position store.
Source: [`audiobook_app/wsola.c`](../../audiobook_app/wsola.c),
[`audiobook_app/wsola.h`](../../audiobook_app/wsola.h),
[`audiobook_app/player.c`](../../audiobook_app/player.c),
[`audiobook_app/player.c`](../../audiobook_app/player.c)
+ [`audiobook_app/posstore.h`](../../audiobook_app/posstore.h).

## WSOLA time-stretch (pitch preserved)

`audiobook_app/wsola.{c,h}` is a stateful streaming WSOLA (Hann window, 50%
overlap, cross-correlation onset search ±15 ms). The `wsola_t` is embedded
directly in `g_pl` (~80 KB fixed BSS, NO malloc, NO OOM path — the key
difference from the libjpeg covers that kept freezing). API:

```c
wsola_init(rate, ch, speed);
wsola_feed(in, frames);
wsola_drain(out, max) -> frames;
```

Parameters are rate-adaptive:
```text
N   = rate * 40 / 1000          (~40 ms frame)
Hs  = N / 2                      (hop, synthesis)
Ha  = Hs * speed                 (hop, analysis — speedup -> Ha > Hs
                                  -> output shorter -> faster)
delta = rate * 15 / 1000         (search radius)
```

Re-init on track open (both MP3 + AAC paths, AFTER `setup_alsa`) AND on
`player_set_speed` when a track is open (flushes ~40 ms; tiny glitch).
Position credit is UNCHANGED: `content_frames = wr_total * speed`,
`advanced = content_frames * 1000 / rate`. Speed cycle `{1000, 1100, 1250,
1500}`. **1.0x is EXACT passthrough** (WSOLA bypassed) so the confirmed-clean
1.0x path can't regress.

## The MP3 1.5x "cut off speech" bug

AAC feeds WSOLA ~1024 samples per `decode_step` (fine). MP3 feeds up to
**16384** samples (a full `PCM_BUF_SAMPLES` chunk). `in_buf` was only 16384
shorts, so MP3 chunk + leftover OVERFLOWED `wsola_feed`, which DROPPED frames
from the FRONT to make room → permanently discarding content AND driving
`ideal_off` negative (corrupting the nominal cursor → search ranges went
`[-delta, -…]` → garbage onsets). At 1.5x the extra discard crossed the
perceptual threshold; 1.1/1.25 masked it. "Only MP3" = large-chunk feed;
"only 1.5x" = perceptual threshold.

**FIX (3 parts):**
1. Right-size `in_buf` to `WSOLA_IN_FRAMES * CH_MAX` (12288 frames).
2. `wsola_feed` overflow handler NEVER eats the `ideal_off` look-behind
   (clamps drop to `ideal_off`, truncates incoming tail only as last resort)
   so `ideal_off` can't go negative.
3. `decode_step` MP3 path feeds in SUB=4096 sub-chunks, draining after each →
   `in_have` stays ≤ leftover(~need)+4096 ≈ 7700 < 12288, no overflow, no
   content loss. AAC path unchanged (small feed, no overflow).

Tuning knobs: increase `delta` (search radius, more CPU) for better overlap
matches; or shorten `N` (frame). The feed-overflow fix is the real one.

## Smart rewind on resume

`AUDIOBOOK_RESTORE_REWIND_MS` was the OLD shell-daemon setting, NOT used by
the NativeApp. A 5 s rewind is wired into the NativeApp resume path directly:
`player.c::cmd_play`, the `start_ms < 0` (resume-from-saved) branch:

```c
into = p.position_ms;
if (into > RESUME_REWIND_MS) into -= RESUME_REWIND_MS;   // #define RESUME_REWIND_MS 5000
else into = 0;
```

It clamps at the saved track's start (no cross-track back-walk — fine for
M4B=one-long-track and long MP3 files). It does NOT apply to bookmark/chapter
jumps (those use `start_ms >= 0`, absolute).

## Save-on-quit (defensive)

`CMD_QUIT` never saved — it just set `running=0`, so the final position was
only as fresh as the 5 s periodic throttle (up to 5 s lost on every exit, and
if `.pos` was missing the next session resumed ~5 s in and overwrote with
~0). FIX: `CMD_QUIT` calls `save_progress(0)` before `running=0`. `g_pl.db` is
still open then (`db_close` is in `ui_run` AFTER `player_shutdown`).

## SD-primary position store (v2.0.9+)

`pos_save_sd` writes `<book_id>.pos`
(`track_ordinal/track_pos_ms/book_elapsed_ms/completed/ts`) to
`/usr/data/mnt/sd_0/.audiobook_pos/` via tmp+rename (atomic-ish on exFAT).
`save_progress` calls `pos_save_sd` (authoritative) AND
`audiobook_save_progress` (best-effort library.db mirror for the list "%"
display; return ignored). `cmd_play` resume reads `pos_load_sd` first, falls
back to `audiobook_get_progress` (library.db) only for pre-2.0.9 positions
(migration). So a full `/usr/data` can NEVER lose the place.

`audiobook_cleanup_orphans` calls `pos_remove_sd` for pruned books (no stale
`.pos`). See [library_scan_storage.md](./library_scan_storage.md) for why
`/usr/data` is chronically near-full and why the SD-primary store matters.

### Gotchas
- SD is `/usr/data/mnt/sd_0` NOT `/mnt/sd_0` (empty stub).
- exFAT `symlink=0` is irrelevant (no symlinks used).
- `mkdir` is idempotent (ignore `EEXIST`).
- `fscanf` skips whitespace so `\n`-separated fields parse fine.

## Bookmarks are also SD-primary (v2.0.16)

`bookmark_sd.{c,h}` writes `/usr/data/mnt/sd_0/.audiobook_pos/<book_id>.bm`,
format `created_at \t track_id \t pos \t bookpos \t label`, atomic temp+rename,
`created_at` = bookmark id. `library.c` add/list/delete delegate to it; the
`bookmarks` DB table is INERT; a one-time per-book DB→SD migration runs on
first bookmark-screen open (an empty marker is left when there are 0 rows).
`scan.c:948` orphan-prune drops `.bm` via `bookmark_remove_book_sd`. Adding a
bookmark touches ONLY the SD → a full `/usr/data` can no longer lose a
bookmark / flip "Book not found" (immunity by construction). The `.bm` blast
radius = one book's marks, not the library DB. See
[library_scan_storage.md](./library_scan_storage.md).