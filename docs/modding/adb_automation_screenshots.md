# ADB automation and screenshot pitfalls

How to drive the R1 over ADB without bricking the session: safe screenshot
capture, the `application/octet-stream` crash, the pre-Read safeguard, the
vision sub-agent pattern, preset UI coordinates, and the app log truth source.
Source: [`tools/r1_adb_control.py`](../../tools/r1_adb_control.py),
[`tools/adb_capture_fb0.py`](../../tools/adb_capture_fb0.py),
[`tools/safe_image_check.py`](../../tools/safe_image_check.py),
[`tools/adb_inject_key_event.py`](../../tools/adb_inject_key_event.py).

## The `application/octet-stream` API crash

Capturing a screenshot the wrong way on Windows produces a malformed PNG;
reading it via an image API rejects it with
`invalid image: expected image mime type, got "application/octet-stream"` and
freezes the session. This is the single most common ADB-automation footgun.

### BAD vs GOOD capture
- BAD: `adb shell screencap -p > screen.png` — CRLF translation corrupts the
  PNG on Windows.
- GOOD: `adb exec-out screencap -p > screen.png` — preserves raw binary bytes.

### Never `Read` `.raw` framebuffer as an image
`adb_capture_fb0.py` produces both a `.png` and a `.raw`; the `.raw` is RGB565
binary data, NOT a valid image format. Reading it as an image fails or
crashes the session.

### Files need a real image extension
Files without `.png`/`.jpg`/`.jpeg` are sent as `application/octet-stream`
and rejected. Always write with a proper extension.

## Repo safe paths (already correct)

- `tools/adb_capture_fb0.py` reads `/dev/fb0` via `dd`, converts RGB565 to PNG
  locally, **validates PNG magic bytes** after writing, and exits with an
  error if invalid (`--no-verify` only if you know what you're doing).
- `tools/r1_adb_control.py screenshot` writes to
  `work/adb-control/screenshots/`; **raw `.raw` files are placed in a `raw/`
  subdir** so they don't sit next to `.png`s as a trap. PNG validation runs
  after every capture; a bad PNG raises `RuntimeError` before any API call can
  fail.

## Pre-Read safeguard script

Run `py -3 tools/safe_image_check.py <path>` BEFORE any `Read` on an image
file. It validates magic bytes and prints an unavoidable VISION SAFETY WARNING
if the file is an image, forcing an explicit choice between the safe path
(delegate to a vision agent — always correct when the main model lacks
vision) and the unsafe path (`Read` directly, only when the main model is
confirmed vision-capable). The script never auto-delegates; it is a
human-in-the-loop guard rail.

Prevention checklist:
1. Run `safe_image_check.py` first.
2. Verify the `.png`/`.jpg` extension.
3. Check magic bytes: `xxd -l 8 file.png` should start with `89 50 4E 47`.
4. Never `Read` `.raw` framebuffer files.
5. On Windows always use `adb exec-out screencap -p`.

## Vision-capable sub-agent for non-vision main models

If the active model lacks vision support, the main thread must NEVER `Read`
image files directly — delegate to a vision sub-agent. (On this project the
main model + Agent sonnet lack image input; `gemma4:26b-a4b-it-qat` in local
Ollama is confirmed vision-capable.)

## Preset UI coordinates (480×800, v2.0.16/17 layout)

`tools/r1_adb_control.py` drives taps at fixed coordinates:

```text
Mark (add bookmark)   = (66, 748)
Play/Pause            = (124, 592)
Bookmarks screen      = (124, 668)
```

The `main-audiobooks` preset navigates to the audiobook app from the launcher.
App logs go to `/tmp/.audiobook_hook.log` — when the screenshot classifier
reports "unknown", the log is the truth source.

## App log truth source

The hook logs to `/tmp/.audiobook_hook.log`. It is **cleared on reboot**.
The nohup `tail -f` dies with the adb session, so on a device freeze run the
tail as a **background task on the host** so it survives the freeze:

```bash
adb shell tail -f /tmp/.audiobook_hook.log &   # captures live across a freeze
```

When the screenshot classifier says "unknown", read the log to see what state
the app is actually in (it prints `FBIOPAN_DISPLAY#N`, state transitions,
decode params, BT detection, etc.).

## Device selection gotchas

### `r1_adb_control.py` has no `-s` flag
The tool does not accept a serial positional. Use the `ANDROID_SERIAL` env
var:

```bash
ANDROID_SERIAL=ingenic_2233 py -3 tools/r1_adb_control.py screenshot
```

### Two ADB devices — only one is the R1
A second device on the same machine (`ZY22JFZHDT`) is NOT the R1 — it lacks
`hiby_player`/`ota_info`. The R1 enumerates as `ingenic_2233`. Target
`adb -s ingenic_2233` for all R1 work. Confirm with
`adb -s <serial> shell cat /etc/r1_audiobook_version`.

### git-bash path mangling
`adb shell /usr/bin/bootmode.sh Recovery` from git-bash without
`MSYS_NO_PATHCONV=1` gets mangled to `C:/Program Files/Git/usr/bin/bootmode.sh`
→ a silent no-op. Always prefix `MSYS_NO_PATHCONV=1` for `adb shell
<abs-path>`. See [flash_and_recovery.md](./flash_and_recovery.md).

## Programmatic screen state without vision

For programmatic screen state, prefer `tools/r1_adb_control.py screenshot
--classify` / `pixstat` / `ascii` over image input. The classifier gives a
state label; `pixstat` gives pixel statistics; `ascii` gives an ASCII
rendition. These work without any vision model and are deterministic.