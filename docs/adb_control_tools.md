# R1 ADB Control Tools

`tools/r1_adb_control.py` is a small non-persistent control console for live
R1 testing. It uses the same safe mechanisms already proven during audiobook
development:

- screenshots read `/dev/fb0` and convert the 480x800 RGB565 framebuffer to PNG
- taps, drags, and playback buttons write short Linux input-event streams to
  `/dev/input`
- generated input streams are temporary and do not change firmware files or
  patch process memory

ADB still has to be enabled manually after a reboot.

## Boot ADB For Development

Persistent ADB should stay a developer option, not a normal release default.
Always-on ADB changes the device's debug/security posture and may have a small
power cost while plugged into USB.

Stock firmware includes `/etc/init.d/T90adb`, but the boot script only runs
`/etc/init.d/S??*`. Development firmware can opt into boot ADB by building with
`-EnableBootAdb`, which copies the stock helper to `/etc/init.d/S90adb` and adds
`boot_adb=enabled` to `/etc/r1_audiobook_version`.

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

The live helper only toggles `/usr/data/disableadb`; it does not modify the
read-only rootfs and does not restart the current ADB session. If the installed
firmware lacks `/etc/init.d/S90adb`, a new development firmware must be built
with `-EnableBootAdb`.

## Finding A UI Toggle

The stock resources already expose `System -> USB device mode`, with a `Dock`
choice that appears to be the UI side of ADB/dock mode. That is the preferred
place for a user-facing toggle if live testing confirms where the selection is
persisted.

Capture a before snapshot:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_snapshot_r1_settings.ps1 `
  -Label before-usb-mode
```

Change `System -> USB device mode` on the R1, then capture the after snapshot
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

If the diff identifies the saved USB/Dock setting, a development boot script can
honor that same value so the existing UI setting becomes the boot-ADB toggle.
A brand-new settings menu item is possible in theory, but it would require
deeper `hiby_player` UI binary patching and carries more black-screen risk than
reusing the stock setting.

Static firmware notes that narrow the live test:

- `hiby_player` embeds `/data/user.ini`, `usb_mode`, `usb_working_mode`,
  `VG_LISTVIEW_USB_MODE`, `/usr/bin/adbon`, and `/usr/bin/adboff`.
- The resource route for `USB device mode` is `hl_usb_mode_d` from
  `usr/resource/hl_json/hl_sys_set_a.json`.
- `set_functions.json` enables `usb_working_mode` and lists `usb_mode`, so both
  should be watched in settings snapshots.
- `/usr/bin/adbon` stops mass storage and starts `/etc/init.d/adb/S440adb`;
  `/usr/bin/adboff` stops ADB and restarts mass storage.
- Both stock ADB backends, `S310adb` and `S440adb`, refuse to start when
  `/usr/data/disableadb` exists.

For binary settings files such as `/data/user.ini`, the snapshot comparison
report includes byte-level detail from `tools\compare_binary_settings.py`.
That detail is often more useful than a whole-file hash because it shows nearby
UTF-16LE strings and changed offsets.

## Basic Checks

```powershell
python tools\r1_adb_control.py devices
```

```powershell
python tools\r1_adb_control.py screenshot --label main-menu
```

Screenshots are saved under `work\adb-control\screenshots\` by default.

## Launcher And List Navigation

From the main launcher:

```powershell
python tools\r1_adb_control.py preset main-audiobooks --after-screenshot
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

Scroll a list upward:

```powershell
python tools\r1_adb_control.py drag 240 735 240 250 --after-screenshot
```

The coordinate presets are based on the stock 480x800 R1 UI and the captured
Audiobooks screenshots. If the screen is not on the expected page, use a
screenshot first.

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
