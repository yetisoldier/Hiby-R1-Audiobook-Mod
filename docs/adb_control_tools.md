# R1 ADB Control Tools

`tools/r1_adb_control.py` is a small non-persistent control console for live
R1 testing. It uses the same safe mechanisms already proven during audiobook
development:

- screenshots query and capture the currently visible 480x800 RGB565
  framebuffer page, then convert it to PNG
- taps, drags, and playback buttons write short Linux input-event streams to
  `/dev/input`
- generated input streams are temporary and do not change firmware files or
  patch process memory

The R1 framebuffer has two vertically stacked pages. A raw read starts at page
zero even when `FBIOGET_VSCREENINFO.yoffset` says page one is visible.
`r1_adb_control.py screenshot` now compiles and installs the read-only
`tools/r1_fb_capture.c` helper automatically, then captures the page at the
current yoffset. If the pinned Zig toolchain is unavailable it warns and falls
back to page zero. The manual build and capture commands are in
[`docs/modding/adb_automation_screenshots.md`](./modding/adb_automation_screenshots.md).

> **See also:** [`docs/modding/adb_automation_screenshots.md`](./modding/adb_automation_screenshots.md)
> for the screenshot/vision pitfalls (the `application/octet-stream` crash,
> safe capture, the pre-Read safeguard, the vision sub-agent pattern), preset
> coordinates for the v2.0.16/17 UI layout, and the app log truth source. Key
> points: use `adb exec-out screencap -p` (not `adb shell screencap -p >`, which
> CRLF-corrupts the PNG on Windows); never `Read` `.raw` framebuffer files;
> `r1_adb_control.py` has no `-s` flag — set `ANDROID_SERIAL=<serial>` to target
> a specific device (the R1 enumerates as `ingenic_2233`; a second device like
> `ZY22JFZHDT` is NOT the R1).

ADB still has to be enabled manually after a reboot.

The ADB scripts prefer the repo-local platform-tools binary at
`.tools\platform-tools\adb.exe` when no explicit `-Adb`/`--adb` value is passed.
They still accept a custom ADB path if a different SDK install should be used.

## Boot ADB For Development

Persistent ADB should stay a developer option, not a normal release default.
Always-on ADB changes the device's debug/security posture and may have a small
power cost while plugged into USB.

Stock firmware includes `/etc/init.d/T90adb`, but the boot script only runs
`/etc/init.d/S??*`. Development firmware can opt into boot ADB by building with
`-EnableBootAdb`, which installs `/etc/init.d/S90adb` and adds
`boot_adb=enabled` to `/etc/r1_audiobook_version`. Installation alone does not
enable persistence: `/usr/data/enable_boot_adb` must also exist, and
`System -> USB working mode` must be `Device`. The wrapper waits 20 seconds for
HiBy's USB setup, performs one guarded transition, and does not retry/rebind.

Check the currently connected device:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_manage_boot_adb.ps1
```

Allow boot ADB on the next reboot, if the installed firmware has `S90adb`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_manage_boot_adb.ps1 `
  -Action enable
```

Block boot ADB on the next reboot:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_manage_boot_adb.ps1 `
  -Action disable
```

The live helper creates or removes `/usr/data/enable_boot_adb` and maintains the
legacy `/usr/data/disableadb` guard. It does not modify the read-only rootfs and
does not restart the current ADB session. If the installed firmware lacks
`/etc/init.d/S90adb`, a new development firmware must be built with
`-EnableBootAdb`. Public releases omit it.

## Finding A UI Toggle

The stock resources expose `System -> USB working mode`. Live testing confirmed
that this is a usable stock UI toggle for development boot ADB.

Capture a before snapshot:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_snapshot_r1_settings.ps1 `
  -Label before-usb-mode
```

Change `System -> USB working mode` on the R1, then capture the after snapshot
and compare it with the folder printed by the first command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_snapshot_r1_settings.ps1 `
  -Label after-usb-mode `
  -CompareTo work\settings-snapshots\YYYYMMDD-HHMMSS-before-usb-mode
```

Or use the guided wrapper, which captures before, waits while the R1 setting is
changed, then captures and compares after:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_probe_usb_mode_toggle.ps1
```

The comparison report is written into the second snapshot folder as
`settings-snapshot-comparison.txt`. You can also compare two existing snapshots
without touching the device:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\compare_r1_settings_snapshots.ps1 `
  -Before work\settings-snapshots\YYYYMMDD-HHMMSS-before-usb-mode `
  -After work\settings-snapshots\YYYYMMDD-HHMMSS-after-usb-mode
```

If the diff identifies the saved USB working mode setting, a development boot script can
honor that same value so the existing UI setting becomes the boot-ADB toggle.
A brand-new settings menu item is possible in theory, but it would require
deeper `hiby_player` UI binary patching and carries more black-screen risk than
reusing the stock setting.

Static firmware notes that narrow the live test:

- `hiby_player` embeds `/data/user.ini`, `usb_mode`, `usb_working_mode`,
  `VG_LISTVIEW_USB_MODE`, `/usr/bin/adbon`, and `/usr/bin/adboff`.
- The resource route for `USB working mode` is `hl_usb_mode_d` from
  `usr/resource/hl_json/hl_sys_set_a.json`.
- `set_functions.json` enables `usb_working_mode` and lists `usb_mode`, so both
  should be watched in settings snapshots.
- The stock settings table stores `usb_working_mode` as ID `8` and `usb_mode`
  as ID `9`, which may help interpret byte-level `/data/user.ini` diffs.
- v2.0.27 replaces `/usr/bin/adbon` and `/usr/bin/adboff` with serialized
  wrappers around a shared USB gadget helper. ADB mode releases stale
  mass-storage LUNs and mounts the SD locally. Mass-storage mode runs detached,
  unmounts the SD first, and restores ADB if a busy filesystem prevents export.
- Transition diagnostics are written to `/tmp/r1-usb-mode.log`.
- Both stock ADB backends, `S310adb` and `S440adb`, refuse to start when
  `/usr/data/disableadb` exists.
- Live testing found `USB working mode` in `/usr/data/user.ini` at offset
  `0x740` (`1856` decimal): Auto is byte value `0`, Device is byte value `1`.
  The opt-in development `S90adb` wrapper uses that byte so boot ADB only starts
  when the stock UI setting is `Device`.

For binary settings files such as `/data/user.ini`, the snapshot comparison
report includes byte-level detail from `tools\compare_binary_settings.py`.
That detail is often more useful than a whole-file hash because it shows nearby
UTF-16LE strings and changed offsets.

## Basic Checks

```powershell
python tools\r1_adb_control.py devices
```

```powershell
python tools\r1_adb_control.py processes
```

```powershell
python tools\r1_adb_control.py screenshot --label main-menu
```

Screenshots are saved under `work\adb-control\screenshots\` by default.

For input injection, `--event event1` and `--event /dev/input/event1` are both
accepted. The connected R1 currently exposes the touchscreen as `event1`, but
the shorthand is easier to read in logs.

If RAM route experiments leave the UI player stopped or visually wedged while
ADB still works, `tools\adb_hold_hiby_player.ps1` can hold a hidden foreground
ADB session running `/usr/bin/hiby_player` without rebooting the device:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_hold_hiby_player.ps1 -ForceRestart
```

Use this only for development recovery. The stock wrapper reboots the device
when `hiby_player` exits, so the helper stops the wrapper before forcing a
restart and records the local holder PID under `work\adb-control`.

## Framebuffer Blank/Wake Test

`r1_fb_blank_ctl` reproduces the panel-power state where audio keeps playing
but framebuffer pans fail with `EBUSY`. It is a development helper and is not
installed in release firmware.

Build and push it:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\build_r1_fb_blank_ctl.ps1

adb -s ingenic_2233 push `
  work\fb-blank-test\r1_fb_blank_ctl /tmp/r1_fb_blank_ctl
adb -s ingenic_2233 shell chmod 755 /tmp/r1_fb_blank_ctl
```

With an audiobook playing, force a hard blank:

```powershell
adb -s ingenic_2233 shell /tmp/r1_fb_blank_ctl blank
```

The audiobook UI should convert it to a lightweight blank within one pan tick.
`brightness` should read `0`, and one power press or a touchscreen double-tap
should restore the saved brightness. The helper can always issue a direct
development recovery unblank:

```powershell
adb -s ingenic_2233 shell /tmp/r1_fb_blank_ctl unblank
```

For syscall-level diagnosis, attach `strace` to `hiby_player` and look for an
`FBIOPAN_DISPLAY = -1 EBUSY` followed by `FBIOBLANK, 0`. Do not leave `strace`
attached during normal playback; it adds substantial scheduling overhead.

## Launcher And List Navigation

From the main launcher:

```powershell
python tools\r1_adb_control.py preset main-audiobooks --after-screenshot
```

The launcher presets are calibrated from current framebuffer captures of the
six-icon stock launcher layout, including the stock theme-aware Books glyph
used by the Audiobooks tile.

The `tap` command also accepts the same preset names as a shortcut:

```powershell
python tools\r1_adb_control.py tap main-audiobooks --after-screenshot
```

The screenshot-assisted macro captures before and after images:

```powershell
python tools\r1_adb_control.py macro open-audiobooks
```

From the audiobook title list:

```powershell
python tools\r1_adb_control.py row 1 --after-screenshot
python tools\r1_adb_control.py row 2 --after-screenshot
python tools\r1_adb_control.py row 3 --after-screenshot
```

Back arrow:

```powershell
python tools\r1_adb_control.py preset soft-back --after-screenshot
```

Now Playing uses a higher back-arrow hit target:

```powershell
python tools\r1_adb_control.py preset now-playing-back --after-screenshot
```

Edge-swipe back, which is more reliable on Now Playing and some grid/list
screens:

```powershell
python tools\r1_adb_control.py macro edge-back --after-screenshot
```

Scroll a list upward:

```powershell
python tools\r1_adb_control.py drag 240 735 240 250 --after-screenshot
```

The coordinate presets are based on the stock 480x800 R1 UI and the captured
Audiobooks screenshots. If the screen is not on the expected page, use a
screenshot first.

## Audiobooks Route Lab

For Author / Title / Series UI research, use the route lab to prepare a
RAM-only test pass:

```powershell
python tools\r1_audiobook_ui_route_lab.py list
python tools\r1_audiobook_ui_route_lab.py make-script `
  --output work\ui-route-lab\test-route-candidates.ps1
```

Then, with the R1 on the main launcher and ADB enabled:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File work\ui-route-lab\test-route-candidates.ps1
```

The generated script tests one route at a time, captures screenshots, and
restores the known-good Audiobooks title route after each candidate. Rebooting
also restores the flashed route because the route patch is RAM-only.

## Firmware Staging

Use the guarded staging wrapper when a verified package needs to be placed on
the R1 for flashing:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\stage_r1_firmware_package.ps1 `
  -IUnderstandThisStagesFirmware
```

The wrapper first prefers ADB and delegates to
`tools\adb_stage_verified_firmware.ps1`. If ADB is not available, it will only
copy to a single removable drive that already looks like the R1 SD card by
containing `/Music`, `/Audiobooks`, or an existing `r1.upt`. It refuses fixed
PC drives and refuses ambiguous removable-drive situations unless `-SdRoot` is
provided.

## Playback Controls

Physical button events:

```powershell
python tools\r1_adb_control.py key playpause
python tools\r1_adb_control.py key next
python tools\r1_adb_control.py key prev
```

Now Playing seek bar tap by percentage:

```powershell
python tools\r1_adb_control.py seek 35 --after-screenshot
```

The seek command only taps the visible progress bar; it does not verify the
result. Use the resume helper or the existing guarded seek test when position
verification matters.

Now Playing play-mode button:

```powershell
python tools\r1_adb_control.py preset now-playing-mode --after-screenshot
```

Live settings snapshots on the R1 found the persisted play-mode byte in
`/usr/data/user.ini` at offset `0x250` (`592` decimal). The observed tap cycle
on the normal R1 was `1 -> 2 -> 0 -> 3 -> 1`. Framebuffer crops showed
`1=single-track loop`, `2=random/shuffle`, `0=random/shuffle`, and `3=list loop`.
Audiobook runtime builds after `1.6.4-audiobook` target value `3` while an
audiobook is active, guarded by the Now Playing framebuffer check so the daemon
does not tap the mode coordinate from the wrong screen.

## Dry Runs

Dry runs generate the local input-event file and print the target action without
requiring a connected device:

```powershell
python tools\r1_adb_control.py preset main-audiobooks --dry-run
python tools\r1_adb_control.py macro open-audiobooks --dry-run
```

## Useful Live Test Flow

```powershell
python tools\r1_adb_control.py screenshot --label start
python tools\r1_adb_control.py macro open-audiobooks
python tools\r1_adb_control.py row 1 --after-screenshot
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_collect_audiobook_resume_debug.ps1
```

This is enough to drive the common title-tap resume test when the R1 is
connected, while still leaving manual device control available at any point.
