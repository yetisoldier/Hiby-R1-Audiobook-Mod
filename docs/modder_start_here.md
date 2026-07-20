# Modder Start Here

This page is for people who want to understand or extend the HiBy R1 audiobook
firmware work, not just install the finished package.

The current public firmware is **v2.0.17** (about-screen label "HiBy R1
2.0.17"). It targets the normal HiBy R1 on stock firmware 1.6. It is not
tested on the R1 MIDI or other HiBy players.

> The v2.0.x line is the "NativeApp" pivot: the audiobook app is an in-process
> `LD_PRELOAD` hook into `hiby_player`, replacing the older v1.6.x resume-daemon
> / stock-route approach. The deep reverse-engineering reference for the
> current design lives in [`docs/modding/`](./modding/) — start there.

## Where to read first

1. [`docs/modding/README.md`](./modding/README.md) — the knowledge-base index
   + the cross-cutting meta-lessons (de-risk FIRST, fixed-BSS over malloc,
   reflash-not-hot-swap, catch logs before rebooting).
2. [`docs/modding/hook_architecture.md`](./modding/hook_architecture.md) — the
   LD_PRELOAD hook is the foundation everything else builds on.
3. [`docs/modding/brick_lessons_build_categories.md`](./modding/brick_lessons_build_categories.md)
   — **read before building or flashing anything.** Two builds bricked the
   device; this explains which change categories are dangerous and the
   cardinal rule for avoiding it.
4. [`docs/audiobook_firmware_architecture.md`](./audiobook_firmware_architecture.md)
   — the high-level architecture overview.
5. [`docs/build_flash_verify_runbook.md`](./build_flash_verify_runbook.md) —
   end-to-end build, flash, verify, and publish workflow.
6. [`docs/adb_control_tools.md`](./adb_control_tools.md) — live device control;
   see also [`docs/modding/adb_automation_screenshots.md`](./modding/adb_automation_screenshots.md)
   for the screenshot/vision pitfalls.
7. [`docs/production_release_checklist.md`](./production_release_checklist.md)
   and [`docs/github_release_process.md`](./github_release_process.md) — what
   must pass before a public release is trusted.

## Safety baseline

Keep these rules in mind before changing or flashing anything:

- Keep a known-good `r1.upt` (stock 1.6, or a prior good release like `2.0A`)
  available for SD-card recovery.
- Do not flash a package until `tools\verify_r1_audiobook_build.py` passes
  (legacy resume-runtime FAILs are EXPECTED for NativeApp — only unlock +
  NativeApp checks need to pass).
- **Test hook changes via reflash, not hot-swap.** Killing a running
  `hiby_player` and LD_PRELOAD-ing a different hook leaves the input grabs
  stuck → frozen touch. Bake the change into a `.upt` and reflash. See
  [`modding/hook_architecture.md`](./modding/hook_architecture.md).
- **Never bundle PMIC / boot-mode / mount-option / unverified-binary-patch
  changes.** Verify each on-device in isolation first. See
  [`modding/brick_lessons_build_categories.md`](./modding/brick_lessons_build_categories.md).
- Flash via `bootmode.sh Recovery` + `adb reboot` (data-preserving). Never use
  `/data/recovery_all` (factory reset). See
  [`modding/flash_and_recovery.md`](./modding/flash_and_recovery.md).
- If a custom build fails to boot, SD-card force-flash a known-good `r1.upt`.

## Repository map for developers

NativeApp pivot (current):

- `audiobook_app\` — the in-process app + hook source: `hook.c` (LD_PRELOAD
  hook + tile trampoline + `ioctl`/`FBIOPAN_DISPLAY` render hook), `ui.c`,
  `render.c`, `player.c` (decode + ALSA + BT), `cover.c`/`pngdec.c`,
  `wsola.c`, `mp4_audio.c`, `scan.c`, `tags.c`, `library.c`,
  `pos_save_sd.c`, `bookmark_sd.c`.
- `tools\build_r1_audiobook_firmware.ps1` — builds the full `.upt` (Zig
  cross-compile + squashfs + `.upt` packaging).
- `tools\build_r1_audiobook_hook.ps1` — builds just the hook `.so` + smoke
  helper.
- `tools\patch_r1_audio_feature_unlocks.py` — the three stock unlocks
  (Native DSD, BT SBC XQ, USB DAC mode).
- `tools\verify_r1_audiobook_build.py` — local pre-flash verifier.
- `tools\r1_adb_control.py`, `adb_capture_fb0.py`, `adb_inject_key_event.py`,
  `safe_image_check.py` — ADB automation + screenshot safety.
- `tools\adb_stage_verified_firmware.ps1`,
  `adb_verify_installed_audiobook_release.ps1`,
  `publish_github_release.ps1` — staging, installed verification, publishing.
- `firmware\releases\` — public release packages, checksums, release notes.

Legacy (v1.6.x, not used by the NativeApp pivot but kept in the tree):
`tools\patch_hiby_player.py`, `tools\r1_audiobook_db_maint.c`,
`r1_audiobook_db_watch.sh`, `r1_audiobook_refresh.sh`,
`r1_audiobook_resume_daemon.sh`, `r1_audiobook_memscan.c`,
`r1_audiobook_direct_open.c`. These drove the pre-2.0 resume-daemon /
generated-`_views` design. The historical docs
([`docs/investigation.md`](./investigation.md),
[`docs/audiobook_app_feature_reference.md`](./audiobook_app_feature_reference.md))
carry superseded banners.

## What the current firmware adds

The v2.0.x mod keeps the stock player untouched and runs the audiobook app
**in-process** inside `hiby_player`:

- The Audiobooks launcher tile runs the hook's `hook_b` instead of the stock
  cave code, drawing the audiobook UI into the framebuffer before each pan.
- `/Audiobooks` is scanned into a SQLite `library.db` (UBIFS on `/usr/data`)
  with cover thumbnails, chapters, and bookmarks.
- MP3 (minimp3_ex) and M4B/AAC (dlopen `libfdk-aac`) decode to ALSA wired
  (`plughw:0,0`) or BlueALSA (`pcm.bluealsa`) with AVRCP + wired fallback.
- WSOLA pitch-preserved speed, smart-rewind resume, sleep timer, draggable
  scrub, SD-primary positions + bookmarks.
- Native DSD, Bluetooth SBC XQ, and USB DAC mode (since v2.0.17) — pure
  stock-resource / shell-config unlocks, independent of the audiobook app.
- Boot ADB (since v2.0.15) via `/etc/init.d/S90adb`.

## Development strategy that worked

The successful path was not to replace the whole player. The stable strategy
was to keep stock playback and build around it — and for v2.0.x specifically,
to take over one launcher tile and the framebuffer pan path in-process rather
than patch `hiby_player` binary caves or run a sidecar daemon:

1. `LD_PRELOAD` a hook into `hiby_player`; trampoline the Audiobooks tile.
2. Draw the UI in the `ioctl`/`FBIOPAN_DISPLAY` path so HiBy's display loop +
   touch controller stay alive.
3. Decode in-hook (minimp3_ex, dlopen libfdk-aac) and write to ALSA/BlueALSA.
4. Store positions + bookmarks SD-primary; mirror to SQLite best-effort.
5. Verify every release both locally and on the real R1 — and de-risk decode/
   image/ALSA changes with a standalone cross-compiled probe before flashing.

## Things that did not work well

Documented so future modders avoid repeating them (full detail in
[`docs/modding/`](./modding/)):

- Hot-swapping a different hook into a running `hiby_player` → frozen touch
  (must reflash to test a hook change).
- stb_image for covers → 17.3 MB OOM (use the device's libjpeg 9.x with
  downscale-on-decode).
- Progressive JPEG without a memory cap → kernel OOM kills `hiby_player`
  → hard freeze.
- `MP3D_SEEK_TO_SAMPLE` → 14.5 MB index → OOM (use `MP3D_SEEK_TO_BYTE`).
- A/B-comparing two paths through the same broken library → shipped a
  non-fix (compare against an independent reference decoder).
- Bundling PMIC + USB-DAC + brightness-binary-patch changes (v2.0.1) and
  `mount_ubifs.sh` `sync`→`noatime` (v2.0.2) → bricked the device.
- Full QEMU system emulation is not a substitute for device testing (X1600
  peripherals + UI/audio stack not modeled well enough).

## External references

The project learned from the public HiBy/Rockbox/modding ecosystem. See the
attribution section in the root `README.md` and the detailed review in
[`docs/hiby_modding_org_review_2026-06-16.md`](./hiby_modding_org_review_2026-06-16.md).
When borrowing from other projects, keep licensing and device-target
differences front and center — R3 Pro II binaries or patches should not be
copied blindly to the R1.