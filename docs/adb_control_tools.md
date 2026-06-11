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
