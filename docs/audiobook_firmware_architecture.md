# Audiobook Firmware Architecture

This document is the high-level architecture of the current (v2.0.x
"NativeApp" pivot) audiobook firmware. It is intended for firmware developers
and modders. For the deep reverse-engineering reference, see the
[`docs/modding/`](./modding/) knowledge base — each topic below links to the
relevant file there.

> **Historical note:** The pre-2.0 (v1.6.x) design used a shell resume-daemon,
> a media-DB helper, and hiby_player binary-patch caves at fixed offsets. That
> approach was replaced by the in-process LD_PRELOAD hook described here. The
> old design is preserved as a historical record in
> [`docs/investigation.md`](./investigation.md) and
> [`docs/audiobook_app_feature_reference.md`](./audiobook_app_feature_reference.md),
> both of which carry a superseded banner.

## Design goal

The mod does not replace HiBy OS or the stock player. It keeps the stock
audio engine, codec handling, Bluetooth, USB, and system UI untouched, and
adds an audiobook app that runs **in-process** inside `hiby_player` via an
`LD_PRELOAD` hook. The guiding rule is the same as before: modify the smallest
stable surface that produces the user-visible audiobook behavior — but the
mechanism changed from "patch hiby_player + run a daemon" to "LD_PRELOAD a
library that takes over one launcher tile and the framebuffer pan path."

## High-level components

```text
LD_PRELOAD hook (libaudiobook_hook.so, ~1.6 MB)
  - Hook B: trampoline on the Audiobooks tile callback cave
            (stock-1.6 hiby_player address 0x0075DAEC).
  - Hook A: ioctl() intercept on FBIOPAN_DISPLAY; draws the audiobook
            UI into the target fb buffer BEFORE each pan so HiBy's
            display loop + touch controller stay alive.
  - Event loop: reads touch (event1) + hardware keys (dup'd grabbed
            fds of event0/2/3) + AVRCP (eventN) in hiby_player's main thread.
  - Player engine: decodes MP3 (minimp3_ex) / M4B-AAC (dlopen libfdk-aac)
            and writes PCM to ALSA wired (plughw:0,0) or BlueALSA (BT).
  - Library scan / chapters / bookmarks / positions (audiobook_app/).

Native helper (r1_audiobook_smoke)
  - Tiny native binary for the smoke test path.

Stock rootfs config tweaks (since v2.0.17)
  - Native DSD, BT SBC XQ, USB DAC working mode (pure JSON + one shell flag).

Build tooling
  - tools/build_r1_audiobook_firmware.ps1 (Zig cross-compile + squashfs + .upt)
  - tools/patch_r1_audio_feature_unlocks.py (the three stock unlocks)

ADB + verification tools
  - tools/r1_adb_control.py, adb_capture_fb0.py, adb_inject_key_event.py,
    safe_image_check.py, verify_r1_audiobook_build.py,
    adb_stage_verified_firmware.ps1
```

See [`modding/hook_architecture.md`](./modding/hook_architecture.md) for the
full hook design (the two hooks, the trampoline installer, the cacheflush
workaround, the hot-swap testing trap).

## Rootfs additions

The release installs these into the read-only rootfs:

- `/usr/lib/libaudiobook_hook.so` — the LD_PRELOAD library (production lives
  in read-only rootfs, NOT on `/usr/data`).
- The native helper binary.
- `/etc/init.d/S90adb` (when built with `-EnableBootAdb`) — persistent boot
  ADB. See [`modding/input_keys_hardware.md`](./modding/input_keys_hardware.md).
- `/etc/r1_audiobook_version` — the version marker (records enabled features).

Runtime state lives under writable storage:

```text
/usr/data/audiobooks/
  library.db            (SQLite, best-effort mirror; /usr/data is UBIFS)
/usr/data/mnt/sd_0/     (SD card — the authoritative store)
  Audiobooks/...        (library root)
  .audiobook_pos/       (SD-primary positions + bookmarks)
    <book_id>.pos
    <book_id>.bm
  .covercache/          (.r565 cover caches next to source covers)
```

See [`modding/library_scan_storage.md`](./modding/library_scan_storage.md)
for why the DB is on `/usr/data` (UBIFS, power-fail-safe) while positions and
bookmarks are SD-primary.

## UI and rendering

The hook draws its own UI into `hiby_player`'s framebuffer mmap (pointer
captured from the `.bss` slot at `0x008b4c14`) just before each
`FBIOPAN_DISPLAY` pan. Framebuffer is 480×800 RGB565, stride 960. Render
helpers in `audiobook_app/render.c` (`render_blit_rgb565`); screens and the
event loop in `audiobook_app/ui.c`. A lightweight blank (backlight sysfs
only, no FBIOBLANK/suspend) lets audio play with the screen off. See
[`modding/hook_architecture.md`](./modding/hook_architecture.md).

## Audio path

- **MP3** decoded in-hook with minimp3_ex (HiBy's `libmp3.so` has stubbed
  readers + stripped resampling and garbles 22050 Hz MPEG-2 stereo).
- **M4B/AAC** demuxed by `audiobook_app/mp4_audio.c` and decoded by the
  device's dlopen'd `libfdk-aac.so`.
- Output: ALSA `plughw:0,0` (wired, CS43131 DAC) or BlueALSA `pcm.bluealsa`
  (Bluetooth A2DP), with wired fallback when no BT sink.
- Volume: CS43131 DAC mixer, 0=loudest, raw-unit steps.

See [`modding/audio_decode_alsa.md`](./modding/audio_decode_alsa.md) and
[`modding/bluetooth_avrcp.md`](./modding/bluetooth_avrcp.md).

## Playback speed, seek, resume

- **WSOLA time-stretch** (pitch preserved) for 1.0/1.1/1.25/1.5x; 1.0x is
  exact passthrough. See [`modding/wsola_seek_resume.md`](./modding/wsola_seek_resume.md).
- **Resume** is per-book + multipart across reboots, with a 5 s smart rewind.
  Positions are SD-primary; the exact final position is saved on quit.
- **Bookmarks** are SD-primary (one `.bm` per book, atomic temp+rename).

## Library scan / chapters / storage

- Scan walks `/Audiobooks`, builds `library.db`, caches chapters.
- M4B chapters parsed from the embedded QuickTime chapter track
  (stsc-aware) or Nero `chpl`. See
  [`modding/library_scan_storage.md`](./modding/library_scan_storage.md).
- The `moov` atom is memory-mapped (not malloc'd) so books with large (15 MB+)
  `moov` atoms don't OOM the scan.
- `/usr/data` is chronically near-full (the stock music DB rebuilds there
  every boot); app-level guards abort a scan cleanly with a red flash if free
  space is too low.

## Audio unlocks (since v2.0.17)

Three general device/music stock-feature unlocks, applied by
`tools/patch_r1_audio_feature_unlocks.py`:

- **Native DSD** — `AnalogDsdNative: native` in `ot_devices.json` (was `dop`).
- **Bluetooth SBC XQ** — `--sbc-quality=xq` added to the BlueALSA launch in
  `/usr/bin/bt_init`.
- **USB DAC mode** — `usb_mode`, `dac_feedback`, `car_mode`, `standby`,
  `about`, `dac_to_store` in `set_functions.json` /
  `midi_set_functions.json` / `config.json`.

These were carried by every pre-2.0 release (v1.5.0–v1.6.3) but dropped at the
v2.0.0 NativeApp pivot; v2.0.17 restores them. They are pure
resource/shell-config tweaks (no binary, boot, PMIC, or mount changes), so
they are independent of the audiobook app and its hook. USB DAC and boot-ADB
share the single USB gadget controller and are mutually exclusive by USB
working mode, so both ship together. SBC XQ applies to the audiobook BT path
too (the app drives `pcm.bluealsa` directly).

## Build and flash

The firmware is built offline from the extracted stock 1.6 rootfs plus the
audiobook app, cross-compiled with `zig cc` (target
`mipsel-linux-gnueabihf.2.22`). See
[`docs/build_flash_verify_runbook.md`](./build_flash_verify_runbook.md) for
the full command, and [`modding/brick_lessons_build_categories.md`](./modding/brick_lessons_build_categories.md)
for the build-flag safe set + the change categories that have bricked the
device. Flashing is data-preserving via `bootmode.sh Recovery` + `adb reboot`;
see [`modding/flash_and_recovery.md`](./modding/flash_and_recovery.md).

## Known limitations

- No background audiobook on the launcher (audio is tied to the app being
  open; a planned future improvement).
- No full custom audiobook search UI.
- PEQ requires a 1.7-beta `hiby_player` that breaks the LD_PRELOAD hook →
  excluded.
- ADB is gated on USB working mode == Device (one USB gadget controller;
  mutually exclusive with USB-DAC by mode).
- Full QEMU system emulation is not a release-confidence replacement.

## Safer extension points

Good next places to experiment (all in-process, low brick risk):

- New UI screens modeled as `ui_state_t` variants drawn in the `ioctl` hook.
- Additional codecs behind the dlopen pattern (libfdk-aac is the template).
- ADB automation + screen classification.
- Optional dev-build features behind build switches.

High-risk areas (see [`modding/brick_lessons_build_categories.md`](./modding/brick_lessons_build_categories.md)):

- PMIC / boot-mode / mount-option / unverified-binary-patch changes — verify
  each on-device in isolation before bundling. Two builds bricked the device
  by bundling these.
- Replacing core stock playback.
- Importing patches from other HiBy models without R1-specific byte checks.
- Flashing a package whose rootfs modes were not verified.