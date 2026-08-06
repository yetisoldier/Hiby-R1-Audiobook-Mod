# Brick lessons and build-risk categories

Two firmware builds bricked the test R1. This file is the cardinal rule and
the change-category map so the next modder doesn't repeat them. **Read this
before you build or flash anything.**

## The cardinal rule (twice reinforced)

**NEVER ship PMIC / boot-mode / mount-option / unverified-binary-patch changes
BUNDLED together; verify each on-device in ISOLATION first.**

The safe release set is:

```powershell
-IncludeAudiobookNativeApp -CustomVersionId <ver> -CustomVersionLabel "HiBy R1 <ver>"
```

...plus, since v2.0.17, the three audio-unlock flags (`-UnlockNativeDsd
-EnableBluetoothSbcXq -UnlockUsbDacMode` - pure JSON/shell config, verified
safe in isolation). Public builds must omit `-EnableBootAdb`. That option is
development-only, explicitly marker-gated, and documented in
[input_keys_hardware.md](./input_keys_hardware.md).

All **app-level hook changes** (moov mmap, storage guards, SD-primary
positions, BT force-take/hand-back, durable ADB, cover fixes, WSOLA, etc.) are
safe to bundle — they are in-process code in the hook `.so`, not boot/PMIC/
mount/binary-patch changes.

The risky categories are **boot / PMIC / mount / binary-patch**. That is the
shortlist. Everything below is the long version of why.

## v2.0.1 BRICKED — DO NOT ship (md5 `6b575de0`)

Built with `-UnlockNativeDsd -EnableBluetoothSbcXq -UnlockUsbDacMode
-IncludeBidhataTweaks -BidhataBrightnessClamp -BidhataFileLimit`. Flashed →
device would NOT turn on. Recovered by re-flashing via SD card. Prime suspects
(root cause not isolated — the device wouldn't boot to probe):

1. **AXP2101 PMIC param changes** (`dcdc3_always_pwmmode=0` + charge
   4400→4350/100→50 — this chip runs the power-on sequence).
2. **USB DAC unlock** `usb_mode=1` (alters boot/USB behavior).
3. **brightness binary patch** (unverified at runtime — a wrong match crashes
   `hiby_player` → black screen = "won't turn on" appearance).

Note: the three audio unlocks themselves were later proven safe (shipped in
v2.0.17). They went down with v2.0.1 because they were *bundled* with the
PMIC/binary-patch suspects, not because they were the cause.

## v2.0.2 ALSO BRICKED — DO NOT ship (md5 `57ec6e9b`)

Built with `-IncludeAudiobookNativeApp -IncludeBidhataTweaks
-BidhataFileLimit` (dropped audio unlocks + brightness + power-tweaks). The
ONLY boot-path change vs `2.0A` was `mount_ubifs.sh` `sync`→`noatime`, which
broke the data-partition mount (noatime rejected or remount failed) → the OS
can't read settings → freeze/reset loop (ADB still on, uptime 0 min — running
off read-only squashfs rootfs). The SD card also wouldn't mount via ADB (vfat
"Invalid argument", exfat "I/O error") because the automount daemon couldn't
start without `/usr/data`. Recovered via SD-card force-flash back to `2.0A`
(md5 `5153a5a8`).

The bidhata `patch_mount_ubifs` (`sync`→`noatime`) tweak is now **known-bad on
this hardware — leave it OUT of any future build unless verified alone.**

## What was deferred (re-add ONE AT A TIME with on-device verification)

### Audio unlocks — DONE (v2.0.17)
- Native DSD (`-UnlockNativeDsd`, `AnalogDsdNative=native` in `ot_devices.json`,
  was `dop`).
- BT SBC XQ (`-EnableBluetoothSbcXq`, bluealsa `--sbc-quality=xq` in `bt_init`).
- USB DAC mode (`-UnlockUsbDacMode`, `usb_mode=1` in `config.json` +
  `set_functions.json` + `midi_set_functions.json` + `dac_feedback`).

These are pure JSON/shell config via `tools/patch_r1_audio_feature_unlocks.py`
— NOT binary/boot/PMIC/mount. They are NOT in the build script's
mutual-exclusion guard (`build_r1_audiobook_firmware.ps1:165` only blocks the
legacy audiobook patches), so they combine cleanly with
`-IncludeAudiobookNativeApp`. USB DAC + boot-ADB share the one USB gadget
controller (mutually exclusive by USB working mode).

### bidhata config/init tweaks (`tools/patch_r1_bidhata_tweaks.py`)
- `config.json`: `vol_warn_enable` 1→0, `lock_vol[headphone]` 50→100, `fcn0`:
  `tf_image_cache_enable`/`tf_music_db_enable` 0→1, +
  `explorer_in_cue_enable`/`otg_scan_enable`/`auto_scroll_playplane`/
  `speed_play_enable=1`.
- `set_functions.json` + `midi_set_functions.json`: `dac_charge_disable` 0→1,
  `standby` 0→1, `dac_feedback` (already 1 from `--usb-dac`).
- `/usr/bin/mount_ubifs.sh` (`sync`→`noatime` — **KNOWN BAD**, leave out).
- `module_driver/axp2101.sh` (`charge_voltage_limit` 4400→4350,
  `charge_term_current` 100→50, `dcdc3_always_pwmmode` 1→0 — **PMIC, BRICK
  SUSPECT**).
- `/etc/inittab` line 33 (comment out `console::respawn:-/bin/sh` serial root
  shell).
- new `/etc/init.d/S95bidhata_tweaks` (SD read-ahead 128 KB→2 MB via
  `/sys/block/mmcblk*/queue/read_ahead_kb=2048`, sysctl
  `vm.vfs_cache_pressure=50`).

The patcher is idempotent (2nd run = only `S95` reinstall) and auditable
(prints every change). The `--power-tweaks` flag (OFF by default in v2.0.2+)
gates the AXP2101 PMIC params + inittab serial-console disable +
`set_functions` charge-path.

### Binary patches to stock `hiby_player` (`tools/patch_hiby_player.py`)
- `--bidhata-brightness` (signature-based MIPS idiom match
  `BRIGHTNESS_CLAMP_SIG`, `sltiu 0x04→0x00`; landed at `0x67440` on our build
  — **UNVERIFIED at runtime, brick suspect**).
- `--bidhata-file-limit` (4 `ORI` immediate patches: `0xEDF74`, `0x2DE3B0`
  imm `0xC350→0xFDE8` = 50000→65000 files; `0x2DFEA0`, `0x2E937C` imm
  `0xC350→0xFDE8` — verified in the shipped binary).

### PEQ
Requires swapping the stock-1.6 `hiby_player` for a 1.7-beta binary → breaks
the LD_PRELOAD hook (it targets stock-1.6 addresses — see
[hook_architecture.md](./hook_architecture.md)). EXCLUDED.

### Other deferred
BT sink (needs code-cave injection into `hiby_player`), theme/font/splash,
MDB/LDB gain tables (ambiguous), Mono/Stereo toggle (ALSA route config).

## Build reproducibility

The build is reproducible from the repo EXCEPT two gitignored external deps
(by design):

- The **Zig toolchain** (`.deps/zig/`) — install via
  `tools/build_r1_db_maint_helper.ps1`; the build script throws if missing.
- The **stock base firmware image** (`firmware/base/`) — proprietary, large;
  the `.upt` is built by patching it.

Build target for the hook + native helper:

```
zig cc -target mipsel-linux-gnueabihf.2.22 -Os -s -fPIE -pie -lm -ldl \
  -Iaudiobook_app -Ivendor -Ivendor/libjpeg
```

**Static is NOT supported for this glibc target** — build DYNAMIC. A newer
glibc (2.28/2.33) boots into a reset loop; the `.2.22` pins the stock ABI.

### Build flags
Safe releases: `-IncludeAudiobookNativeApp -CustomVersionId <ver>
-CustomVersionLabel "HiBy R1 <ver>"` plus the three audio-unlock flags. Do not
add `-EnableBootAdb` to a public build. The NativeApp build is mutually exclusive
with the legacy resume-daemon switches
(`-IncludeAudiobookLauncherGenre`, `-IncludeAudiobookResumeRuntime`,
`-IncludeAudiobookDbMaintenance`, etc.) — the guard at
`build_r1_audiobook_firmware.ps1:165` enforces it. Use
`-IncludeAudiobookNativeApp` alone (plus the safe extras).

### Staging gotcha
Always pass the package, output directory, expected version, expected label,
and feature switches explicitly to `adb_stage_verified_firmware.ps1`. The tool
supports NativeApp packages, performs local verification, stages through a
temporary file, and verifies the device-side MD5/SHA256 before replacing
`/usr/data/mnt/sd_0/r1.upt`. See
[flash_and_recovery.md](./flash_and_recovery.md).

## Revert path

Keep a known-good stock 1.6 `r1.upt` (or a prior good release like `2.0A`)
around before flashing anything experimental. If a flash bricks, SD-card
force-flash back to it — see [flash_and_recovery.md](./flash_and_recovery.md).
This is how v2.0.1 and v2.0.2 were recovered.
