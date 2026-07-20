# HiBy R1 Audiobook Firmware

A self-contained audiobook app for the normal HiBy R1, based on stock HiBy R1
firmware 1.6. **Not for the R1 MIDI.**

> **v2.0.0 ("2.0 A") is the current release.** It replaces the older v1.6.x
> resume-daemon / stock-route approach with a dedicated in-process audiobook app
> (`audiobook_app/`) that draws its own UI and drives audio through ALSA.

## Current release

- **Version marker:** `2.0A`
- **About-screen label:** `HiBy R1 2.0 A`
- **Download:** <https://github.com/yetisoldier/Hiby-R1-Audiobook-Mod/releases/tag/v2.0.0>
- **Package:** `r1-audiobooks-2.0A.upt` (rename to `r1.upt` to install)
- **UPT MD5:** `5153a5a80e9a4acdc9d2748011b0c34d`
- **UPT SHA256:** `ab954621a02f7610563775d3e3770b69fff793ab9e13c324669dac77c1d5e1c8`
- **Persistent boot-ADB:** disabled (public release). Use a `-EnableBootAdb`
 development build for testing.
- **Base firmware:** stock HiBy R1 1.6 (normal R1).

Before flashing, keep a known-good stock 1.6 `r1.upt` for recovery. This is
unofficial firmware tested on one personal normal HiBy R1. Reinstalling stock
firmware should reverse it, but use it at your own risk. Do not install it on
the R1 MIDI or other HiBy players unless you are prepared to recover the device.

## Screenshots

<p>
 <img src="docs/screenshots/01-home.png" alt="Audiobooks home menu" width="200">
 <img src="docs/screenshots/02-titles.png" alt="Titles list with cover thumbnails" width="200">
 <img src="docs/screenshots/03-detail.png" alt="Book detail page" width="200">
 <img src="docs/screenshots/04-now-playing.png" alt="Now Playing with cover and progress handle" width="200">
</p>
<p>
 <img src="docs/screenshots/05-chapters.png" alt="Chapter list" width="200">
 <img src="docs/screenshots/06-bookmarks.png" alt="Bookmarks list" width="200">
 <img src="docs/screenshots/07b-scanning.png" alt="Refresh: scanning library" width="200">
 <img src="docs/screenshots/08-launcher-after-exit.png" alt="HiBy launcher after exiting the app" width="200">
</p>

Captured on the test R1. Full set in [`docs/screenshots/`](docs/screenshots/).

## How it works

The audiobook app runs **in-process** inside `hiby_player`:

- A small `LD_PRELOAD` library (`audiobook_app/hook.c`) installs a trampoline on
 the launcher's Audiobooks tile callback. Tapping the tile runs our app instead
 of the stock cave code.
- An `ioctl` hook intercepts `FBIOPAN_DISPLAY` and draws our UI to the target
 framebuffer buffer *before* each pan. This keeps HiBy's display loop (and the
 touch controller) alive while showing our UI.
- The event loop reads touch and the hardware keys, and the player engine
 decodes MP3 / M4B and writes PCM straight to ALSA.
- Exiting the app clears the framebuffer and returns immediately so the HiBy
 launcher redraws cleanly.

The app and its UI, player, library scanner, and chapter/bookmark storage live
in `audiobook_app/` and a tiny native helper. The stock Music player, file
browser, Bluetooth, USB, and system UI are otherwise untouched.

## Features

**Library**
- Home menu: Continue, Titles, Authors, Series, Folders, Finished, Refresh.
- Scrollable list views with **cover-art thumbnails** (libjpeg, decode-on-demand,
 progressive-JPEG guard).
- Swipe left from a list -> Now Playing.

**Playback**
- **MP3** and **M4B/AAC** (AAC via the device's `libfdk-aac`, `dlopen`'d).
- **Per-book + multipart resume** across reboots and book switching, with a
 5-second smart rewind on resume.
- **Now Playing**: cover art, title/author/duration, and a **draggable progress
 handle** (scrub seek; tapping the bar elsewhere does not jump).
- **Playback speed** 1.0 / 1.1 / 1.25 / 1.5x via **WSOLA time-stretch**
 (pitch preserved; 1.0x exact passthrough). Persists.
- **Sleep timer** Off / 15 / 30 / 60 min with live countdown; auto-pauses and
 saves on expiry.
- **M4B chapters** parsed from the embedded QuickTime chapter track (stsc-aware)
 or Nero `chpl`; MP3 books get one chapter per file. Tap a chapter to seek.
- **Bookmarks**: tap **Mark** on Now Playing to add; tap to jump; long-press to
 delete.

**Hardware**
- Power button toggles backlight (audio keeps playing dark; double-tap wakes).
- Play/pause, prev/next, volume keys in-app. Volume is fine-stepped (~2-2.5 dB)
 with hold-to-ramp.
- Back is always top-left on every in-app screen.

## Expected behavior

- **First run with an SD card:** put audiobooks under `/Audiobooks`, open the
 Audiobooks tile, and tap **Home -> Refresh**. The screen shows a
 "Scanning library..." banner, then a green "Library refreshed" flash. This
 scans `/Audiobooks`, builds the library DB, and caches chapters. Music under
 `/Music` is unaffected.
- **Re-scanning after a chapter fix:** Refresh re-parses M4B chapters, so if a
 book showed only one chapter it will be replaced with the full list after a
 Refresh.
- **Leaving the app:** when you swipe or back out of the Audiobooks app to the
 HiBy launcher, **audiobook playback stops**. Audio is tied to the app being
 open. Exiting while playing returns cleanly to the launcher (no black screen,
 no power-button kick needed). Background playback on the launcher is a
 planned future improvement, not in this release.
- **Resume:** opening a book you were listening to resumes near your last
 position, rewound 5 seconds. Bookmark and chapter jumps go to the exact
 saved timestamp.

## Install

1. Download `r1-audiobooks-2.0A.upt` from the release page.
2. Rename it to exactly `r1.upt` (the R1 will not recognize the update otherwise).
3. Copy it to the root of the SD card.
4. On the R1, run the normal firmware update (System -> Firmware Update -> Local).
5. Wait for success and the reboot.
6. After boot, delete or rename `r1.upt` on the SD card so the updater stops
 offering it.
7. Open the Audiobooks tile and tap **Refresh** to scan `/Audiobooks`.

Recommended SD-card layout:

```text
/Music
/Audiobooks/Author/Year - Book Title/01 - Chapter.mp3
/Audiobooks/Author/Year - Book Title/Book Title.m4b
```

Metadata that helps: Album = book title, Title = chapter/file title,
Album artist = author, and numbered files for multipart books. The app derives
fallbacks from folders/filenames when tags are missing. The genre tag does not
need to be `Audiobook` - anything under `/Audiobooks` is treated as one.

## Building from source

The firmware is built offline from the extracted stock 1.6 rootfs plus the
audiobook app. The audiobook app (`audiobook_app/`) is cross-compiled to MIPS32
with `zig cc` (target `mipsel-linux-gnueabihf.2.22`).

Build the hook/app shared library:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_r1_audiobook_hook.ps1
```

Build the public-release firmware (no boot-ADB, version 2.0 A):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_r1_audiobook_firmware.ps1 `
 -OutDir work\audiobook-firmware-2.0A `
 -OutputUpt work\audiobook-firmware-2.0A\r1-audiobooks-2.0A.upt `
 -IncludeAudiobookNativeApp `
 -CustomVersionId 2.0A `
 -CustomVersionLabel "HiBy R1 2.0 A"
```

Development builds add `-EnableBootAdb` for persistent ADB (installs
`/etc/init.d/S90adb`). **Do not** include `-EnableBootAdb` in public releases.

The NativeApp build is mutually exclusive with the legacy resume-daemon
switches (`-IncludeAudiobookLauncherGenre`, `-IncludeAudiobookResumeRuntime`,
`-IncludeAudiobookDbMaintenance`, etc.) - use `-IncludeAudiobookNativeApp`
alone.

## Architecture and docs

- `docs/audiobook_app_feature_reference.md` - feature map (note: partially
 historical; the top section reflects the current NativeApp).
- `docs/audiobook_firmware_architecture.md` - firmware build/flash architecture.
- `docs/build_flash_verify_runbook.md` - build, flash, and verify runbook.
- `docs/adb_control_tools.md` - live ADB control (screenshots, taps, presets).
- `docs/github_release_process.md` - GitHub Release publishing runbook.
- `docs/production_release_checklist.md` - release and verification checklist.
- `docs/screenshots/` - README screenshots.
- `firmware/releases/v2.0.0/` - v2.0.0 release notes, checksums, and package.
- `CHANGELOG.md` - release history.

## Attribution and sources

This project is unofficial and not affiliated with or endorsed by HiBy. HiBy,
HiBy R1, and the stock firmware remain HiBy's work.

Information and techniques used while building this mod came from:

- [HiBy R1 User Manual](https://guide.hiby.com/en/docs/products/audio_player/hiby_r1/guide)
- [HiBy R1 firmware 1.6 update page](https://store.hiby.com/apps/help-center#hc-r1-firmware-v16-update)
- [Rockbox HiBy Port wiki](https://www.rockbox.org/wiki/HibyPort)
- [bidhata/Hiby-R1-Mod](https://github.com/bidhata/Hiby-R1-Mod)
- [SuperTaiyaki/hiby-firmware-tools](https://github.com/SuperTaiyaki/hiby-firmware-tools)
- [hiby-modding/hiby-mods](https://github.com/hiby-modding/hiby-mods)
- [hiby-modding/hiby_os_crack](https://github.com/hiby-modding/hiby_os_crack)
- [seanap/Plex-Audiobook-Guide](https://github.com/seanap/Plex-Audiobook-Guide)

The audiobook-specific behavior was developed and tested on a personal normal
HiBy R1 through local reverse engineering, live ADB testing, and repeated
stock-firmware recovery tests.