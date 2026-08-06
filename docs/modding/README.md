# HiBy R1 modding knowledge base

This directory is the reverse-engineering reference for the HiBy R1 audiobook
firmware mod. It captures what was learned the hard way — through local reverse
engineering, live ADB testing, and repeated stock-firmware recovery — so other
modders can apply it to their own custom work on the R1 (and on related Ingenic
X1600-class HiBy players).

The user-facing overview lives in the repo [`README.md`](../../README.md). The
high-level firmware architecture lives in
[`docs/audiobook_firmware_architecture.md`](../audiobook_firmware_architecture.md).
This `docs/modding/` directory is the deep technical layer: each file covers one
area with the **claim, the specifics, and the gotchas**.

## Start here

If you have a device, ADB, and this repo:

1. Read [`docs/modder_start_here.md`](../modder_start_here.md) for device/ADB setup.
2. Skim [`hook_architecture.md`](./hook_architecture.md) — the LD_PRELOAD hook is
   the foundation everything else builds on.
3. Read [`brick_lessons_build_categories.md`](./brick_lessons_build_categories.md)
   **before** you build or flash anything. Two firmware builds bricked the test
   device; that file explains exactly which change categories are dangerous and
   the cardinal rule for avoiding it.
4. Then read the topic you actually care about.

## Topic index

| File | What it covers |
|------|----------------|
| [hook_architecture.md](./hook_architecture.md) | LD_PRELOAD `libaudiobook_hook.so` into `hiby_player`; tile-cave trampoline; `ioctl`/`FBIOPAN_DISPLAY` render hook; framebuffer + lightweight blank; the hot-swap testing trap. |
| [flash_and_recovery.md](./flash_and_recovery.md) | `.upt` format, MTD layout, the ADB-driven data-preserving flash sequence, `bootmode.sh` semantics, recovery vs factory reset, revert path. |
| [audio_decode_alsa.md](./audio_decode_alsa.md) | HiBy `libmp3.so` stubbed readers, minimp3_ex, ALSA `plughw:0,0`, CS43131 DAC volume, the port-switch pitfall, M4B/AAC via `libfdk-aac`, VBR resume. |
| [bluetooth_avrcp.md](./bluetooth_avrcp.md) | BlueALSA `pcm.bluealsa`, the EBUSY contention root cause + force-take, hand-back on exit, AVRCP remote, SBC XQ, the resume race. |
| [input_keys_hardware.md](./input_keys_hardware.md) | Why buttons did nothing (dup the grabbed fds), key devices/codes, evdev injection, volume, hardened ADB/storage transitions, optional boot ADB, and USB-mode mutual exclusion. |
| [cover_art.md](./cover_art.md) | Why not stb_image, device libjpeg 9.x dlopen, setjmp error handling, progressive-JPEG OOM + memory cap, thumbnail pre-warm starvation, PNG via `libz`. |
| [wsola_seek_resume.md](./wsola_seek_resume.md) | WSOLA time-stretch (pitch-preserved), the MP3 1.5x cutoff fix, smart rewind on resume, save-on-quit, SD-primary positions. |
| [library_scan_storage.md](./library_scan_storage.md) | The moov-mmap scan-hang fix, M4B and MP3 ID3 chapter parsing, indexed folder navigation, SQLite-on-exFAT, the chronic `/usr/data` near-full + guards, SD-primary store. |
| [sd_runtime_stability.md](./sd_runtime_stability.md) | v2.0.23 overnight-freeze forensics, the stock Ingenic MMC runtime-PM failure, app-scoped SD power hold, reduced resume writes, and validation cautions. |
| [rust_slint_toolchain_research.md](./rust_slint_toolchain_research.md) | Ingenic GCC/glibc toolchain ABI validation, Windows/WSL checkout hazards, and the practical limits of the current Rust/Slint/Nanowave path. |
| [adb_automation_screenshots.md](./adb_automation_screenshots.md) | Screenshot/vision pitfalls, the octet-stream crash, safe capture tools, preset UI coords, the app log truth source. |
| [brick_lessons_build_categories.md](./brick_lessons_build_categories.md) | The v2.0.1/v2.0.2 bricks, the cardinal rule (never bundle risky categories), safe vs dangerous change sets, build reproducibility. |

## Cross-cutting meta-lessons

These apply across every topic and are the most reusable takeaways for anyone
doing similar work.

### De-risk FIRST, always
Before flashing any change that touches decode, image decode, memory allocation,
or ALSA, write a **standalone cross-compiled probe** and push it to `/tmp` on the
device. The probe dies alone; `hiby_player` dying takes the whole device. Probes
used here: `probe_dump.c`, `probe_cmp.c`, `probe_scan.c`, `probe_seek_byte.c`,
`probe_aac.c`, `probe_cover_jpeg.c`, `probe_thumb_batch.c` (with a SIGALRM
watchdog that prints "HANG on cover N" and `_exit`s), `probe_mmap.c`,
`probe_sqlite_exfat.c`, `probe_prog_decode.c`. Build target:

```
zig cc -target mipsel-linux-gnueabihf.2.22 -Os -s -fPIE -pie -lm -ldl \
  -Iaudiobook_app -Ivendor -Ivendor/libjpeg
```

Static linking is NOT supported for this glibc target — build DYNAMIC, push to
`/tmp`, `chmod 755`.

### Compare against an independent reference, not two suspect paths
A/B-comparing two device decode paths against *each other* (e.g. sequential vs
seek through the same broken library) is worthless — both can be garbage and
"match." This mistake shipped a non-fix firmware. Always compare against an
independent known-good reference decoder (e.g. ffmpeg on the host).

### Prefer fixed BSS buffers over malloc
Wherever a structure has a bounded size, embed it as fixed BSS instead of
malloc'ing it. Examples: `wsola_t` (~80 KB) embedded in `g_pl`; chapter-parse
static arrays (`deltas/sizes/offsets/sample_off[4096]`, stsc `fc/spc/sdi[256]`).
No malloc → no OOM path → no freeze risk. The libjpeg covers that kept freezing
the device were the malloc/large-coefficient-buffer exception — see
[cover_art.md](./cover_art.md).

### Test hook changes via reflash, not hot-swap
Killing a running `hiby_player` and LD_PRELOAD-ing a different hook from `/data`
leaves the killed process's EVIOCGRAB/input grabs stuck → the new `hiby_player`
renders the launcher but touch never works → "frozen" (alive, hook never fires).
Two freezes were confirmed this way. To test a hook change, **bake it into a
firmware `.upt` and reflash** (fresh boot = clean input/fb state). See
[hook_architecture.md](./hook_architecture.md).

### Catch logs BEFORE rebooting
The nohup `tail -f /tmp/.audiobook_hook.log` dies with the adb session. On a
device freeze, run `adb shell tail -f /tmp/.audiobook_hook.log` as a **background
task on the host** — it captures live and survives the device freeze. The log is
cleared on reboot. See [adb_automation_screenshots.md](./adb_automation_screenshots.md).

### Path and process facts that bite
- The SD card mounts at **`/usr/data/mnt/sd_0`**, NOT `/mnt/sd_0` (which is an
  empty read-only stub).
- The audiobook DB lives at `/usr/data/audiobooks/library.db`; library root is
  `/usr/data/mnt/sd_0/Audiobooks/...`.
- The real `hiby_player` pid is the `system_main_thr /usr/bin/hiby_player`
  process. The `hiby_player.sh` shell wrapper has no audio fds — do NOT `grep |
  head -1` for "hiby" (it matches the shell wrapper first). Match the binary
  path `/usr/bin/hiby_player`.
- On Windows git-bash, ADB shell paths starting with `/usr/...` get mangled to
  `C:/Program Files/Git/usr/...` unless you prefix `MSYS_NO_PATHCONV=1`. See
  [flash_and_recovery.md](./flash_and_recovery.md).

## Conventions used in these docs

- **Addresses target stock-1.6 `hiby_player`** unless noted. Swapping that binary
  (e.g. for a 1.7-beta needed for PEQ) breaks the LD_PRELOAD hook — see
  [brick_lessons_build_categories.md](./brick_lessons_build_categories.md).
- *Gotcha* callouts mark the non-obvious traps that cost real debugging time.
- Source file references use repo-relative paths (`audiobook_app/player.c`,
  `tools/build_r1_audiobook_firmware.ps1`).

This project is unofficial and not affiliated with HiBy. See the repo
[`README.md`](../../README.md) attribution section for the external sources and
projects that informed this work.
