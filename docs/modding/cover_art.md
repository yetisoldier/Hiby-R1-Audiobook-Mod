# Cover art and image decode

Why the app does not use stb_image, how it decodes JPEG via the device's
libjpeg, the progressive-JPEG OOM trap and the memory-cap fix, the
thumbnail pre-warm starvation bug, and PNG via `libz`. Source:
[`audiobook_app/cover.c`](../../audiobook_app/cover.c),
[`audiobook_app/pngdec.c`](../../audiobook_app/pngdec.c),
[`audiobook_app/tags.c`](../../audiobook_app/tags.c).

## Why not stb_image — 17.3 MB OOM

Covers are 2400×2400 JPEG. `stb_image` decodes 1:1 → output buffer =
2400×2400×3 = 17.3 MB. The device has only ~16 MB `MemAvailable`
(`hiby_player` ~15 MB RSS). A stb probe FAILED: "outofmem." Same OOM class as
the minimp3 `SEEK_TO_SAMPLE` and AAC frame-index traps. `stb_image` has NO
downscale-on-decode option. Don't use it here.

## Use the device's `libjpeg.so.9.4.0` (IJG libjpeg 9.x)

`/usr/lib/libjpeg.so.9.4.0` — IJG libjpeg 9.x, soname `.9`, `LIBJPEG_9.0` ABI,
`JPEG_LIB_VERSION=90`, ~211 KB. `scale_num`/`scale_denom` decodes a JPEG
directly to 1/8 (2400→300) by skipping DCT coefficients → ~270 KB buffer (no
17 MB step). Probe peak RSS 1424 kB (cost ~436 kB).

All 8 `dlsym`'d functions: `jpeg_std_error, jpeg_CreateDecompress,
jpeg_destroy_decompress, jpeg_stdio_src, jpeg_read_header,
jpeg_start_decompress, jpeg_read_scanlines, jpeg_finish_decompress`.

### Vendored headers
`vendor/libjpeg/{jpeglib.h,jmorecfg.h,jconfig.h,jdct.h,jerror.h,jinclude.h,
jpegint.h}` from cloudflare/jpegtran (IJG v9.1). The `.9` soname stayed
constant across ALL 9.x → struct layout stable → 9.1 header structs match the
device's 9e build. Validated: `jpeg_CreateDecompress` checks
`structsize == sizeof(...)`; a probe printed
`sizeof(jpeg_decompress_struct)=488` and did NOT error → exact match.

### Gotcha — dlsym STANDARD IJG names, not the renamed macros
cloudflare's header `#define`-RENAMES symbols (`jpeg_std_error`→`jStdError`
etc.) for their own SIMD fork. The DEVICE exports STANDARD IJG names →
`dlsym` the real names, NEVER call the renamed macros. `jconfig.h` is written
by hand (`HAVE_STDDEF_H, HAVE_UNSIGNED_CHAR/SHORT, BITS_IN_JSAMPLE=8`). Need
`#include <stddef.h>, <stdlib.h>` BEFORE `jpeglib.h` (for `size_t`).

### Gotcha — setjmp/longjmp error handler (or you kill `hiby_player`)
libjpeg's default `error_exit` calls `exit()` → would KILL `hiby_player` (we
are in-process). Install a custom `my_error_exit` that `longjmp`s back;
`cover_get` returns NULL (no cover) on any error (graceful). NEVER use the
default error manager in-process.

## Progressive JPEG OOM — the major thumbnail-freeze root cause

IJG libjpeg (NOT libjpeg-turbo) decodes a **baseline** JPEG with
`scale_denom=8` via single-pass DCT scaling → ~300 KB working set. But a
**progressive** JPEG forces it to buffer ALL DCT coefficients regardless of
`scale_denom` → ~16 MB for a 2400×2400 cover. At idle (`MemAvailable` ~20 MB)
the 16 MB allocation barely fits so a probe survived; under ~8 MB baseline
pressure (simulating app+player) the progressive decode CRASHED THE WHOLE
DEVICE (kernel OOM, adb closed).

All 10 device covers were 2400×2400; exactly 1 was progressive (664 KB). In
real Titles-list order the progressive cover lands ~5th → 4 thumbnails load,
the 5th decode OOMs → `hiby_player` killed → hard freeze. The same trap
affects the 220 px Now-Playing cover path (same `decode_cover_to`).

### FIX v1 (v2.0.8) — unconditional bail
`if (cinfo.progressive_mode) longjmp(g_jmp, 1)` before `start_decompress`
(which is where the coefficient buffer is allocated). A progressive cover
renders as no-cover (graceful, no gap). Validated: all 9 baseline covers
decoded at steady ~9.4 MB RSS, 1 progressive skipped, no crash, `MemAvailable`
held ~11.8 MB.

### FIX v2 (v2.0.8 final) — libjpeg memory cap (shipped)
The unconditional bail dropped ~half the library's covers (32 progressive
books; only ~4 are genuinely too big at 2400²=11.3 MB coeffs; the other 28
are ≤1000² = ≤2 MB coeffs and decode fine). Replace the bail with:

```c
cinfo.mem->max_memory_to_use = clamp(MemAvailable - 3072, 1024, 8192) * 1024;
```

after `read_header` (`read_mem_avail_kb()` reads `/proc/meminfo` MemAvailable).
libjpeg checks the cap when allocating the coefficient buffer and refuses it
(→ `longjmp`) BEFORE touching pages, so huge progressives bail gracefully (no
OOM-kill/freeze); small/medium ones render; baseline (tiny single-pass
buffer) is unaffected. On-device PROVEN: 2400² progressive → BAILED (avail
unchanged), 1000/500/300² progressive + 600² baseline → RENDERED.

### If progressive covers MUST show art
On-device conversion is impossible (decode-progressive = the same 16 MB OOM).
Only host-side re-encode (ImageMagick/libjpeg on Windows) of the progressive
JPEG to baseline works — one-time per cover.

## Thumbnail pre-warm starvation

`draw_list` sets `ui->thumb_warm_target` = the FIRST visible book whose
thumbnail isn't RAM-cached; the event loop decodes ONE per tick via
`cover_thumb_prewarm`. If that decode FAILS (progressive cover, bails at
`read_header`), the book stays uncached → STILL "first uncached visible" next
frame → re-targeted every tick → **starves every book below it of pre-warm**.
One undecodable cover blocks the whole tail of the list.

**FIX:** `cover_thumb_prewarm` returns `int` (1=now cached, 0=failed). On 0,
the event loop calls `thumb_mark_failed(ui, bid)`; `draw_list` skips failed
ids (`!thumb_is_failed(ui, bid)`). `ui_state_t.thumb_failed[64]` +
`thumb_failed_n` (bounded set, linear scan — book_ids are small ints). Cleared
in `navigate_to` so a fresh visit re-tries (a progressive bail is nearly free —
bails at `read_header` before `start_decompress`, so one retry per session is
nothing). Result: a perpetually-failing cover is skipped, the walk advances,
all decodable covers warm. The failing cover stays gracefully blank (no gap).

**LESSON:** "first uncached X" targeting + a failing X = starvation of
everything after X. Always pair targeting with a failure/skip set so one bad
item advances the walk.

## PNG covers — `pngdec.{c,h}` over dlopen'd `libz.so`

No libpng on the device. `audiobook_app/pngdec.{c,h}` is a streaming
PNG→RGB565 decoder over dlopen'd `libz.so` (~290 lines, bit-exact vs PIL
on-device across all color types, gradients, 240² downsample).

Gotchas:
- Declare `z_stream` inline (ABI frozen).
- `inflateInit_(&s, zlibVersion(), sizeof(z_stream))` with a RUNTIME version
  string (else `Z_VERSION_ERROR`). Use plain `inflateInit` (RFC1950 zlib
  IDAT), NOT `inflateInit2`.
- All consecutive IDAT chunks = ONE zlib stream (never `inflateEnd` between
  IDATs).
- `had_idat` flag so the first post-IHDR chunk (often sRGB/pHYs) doesn't
  wrongly skip a CRC.
- Bail on Adam7 interlace / palette (type 3) / non-8-bit.
- `png_decode_to_rgb565(path, px, out)` returns 1=ok / 0=fail.

`cover.c` dispatches by extension (`.png` → pngdec, else libjpeg), sharing the
`.r565` cache. `tags.c audio_extract_cover(path, out_base, out_path, len)`
writes `.jpg`/`.png` by detected signature (MP3 APIC, M4B `covr` flag
`0x0d||0x0e`); `scan.c` builds an extension-less `cache_base` under
`.covercache/`.

## Cover caches and paths

`cover.h`: `cover_get(sqlite3 *db, int book_id)` → `const uint16_t*`
(`COVER_PX=220` RGB565 cache, ~96.8 KB), `cover_clear()`, `cover_shutdown()`.
Forward-decl `typedef struct sqlite3 sqlite3` (don't pull the full SQLite
header). `cover.c`: libjpeg dlopen (lazy, optional; missing → no covers),
adaptive `pick_denom` (`>=8*COVER_PX`→/8, `>=4*`→/4, `>=2*`→/2, else /1 so
small covers don't blur), row-streamed decode (JPOOL_IMAGE 1-row sarray),
downscale to 220 RGB565, one-book cache.

The `.r565` cache is written next to the source cover on the SD card
(`build_r565_path` builds it from `cover_path`, which is under
`/usr/data/mnt/sd_0/Audiobooks/...`). 56 px thumb cache slot = 56×56×2 = 6.3
KB; 16-slot LRU = ~100 KB max. 220 px Now-Playing cache = 96 KB, NOT freed
when leaving Now Playing (`cover_clear` only on shutdown) — a contributor but
small.

`AUDIOBOOK_COVER "/usr/data/audiobooks/cache/covers"` (library.h:20) is
DEAD CODE — `upsert_book` passes `""` for `cover_cache_path` and nothing writes
there. Covers are NOT a `/usr/data` hog. See
[library_scan_storage.md](./library_scan_storage.md) for the real
`/usr/data` consumers.

## De-risk FIRST (learned twice)

A standalone decode-N-covers probe should have run BEFORE flashing the
thumbnail builds. Build 3b8ddb0b STILL FROZE after 4 thumbnails loaded (the
structural render-path fix was insufficient — the bomb is per-decode, not
per-frame). Standalone `work/phase4-probe/probe_thumb_batch.c <root>
<baselineMB> [skip]` walks the library, decodes every cover.jpg to 56 px (same
`pick_denom` + row loop as `cover.c`) with a 5 s SIGALRM watchdog (prints
"HANG on cover N" + `_exit` on hang), prints per-cover dims/`progressive_mode`/
`MemAvailable`/`VmHWM`/`mallinfo`. Build:

```
zig cc -target mipsel-linux-gnueabihf.2.22 -Os -s -fPIE -pie -lm -ldl \
  -Ivendor -Ivendor/libjpeg
```

See the meta-lesson in [README.md](./README.md).