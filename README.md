# HiBy R1 Audiobook Firmware

Unofficial audiobook firmware mod for the normal HiBy R1 on stock firmware 1.6.
Not for the R1 MIDI or other HiBy players.

This is unofficial firmware tested on one normal HiBy R1. Reinstalling stock
firmware should reverse it, but use it at your own risk. Keep a known-good stock
1.6 `r1.upt` available for recovery.

## Screenshots

<p>
  <img src="docs/images/main-menu-audiobooks.png" alt="HiBy R1 main menu showing Audiobooks" width="240">
  <img src="docs/images/audiobook-title-list.png" alt="Audiobook title list on the HiBy R1" width="240">
</p>

## Install

### Manual Install

1. Download `r1-audiobooks-1.7.0A.upt` from the
   [latest release](https://github.com/yetisoldier/Hiby-R1-Audiobook-Mod/releases/tag/v1.7.0A).
2. Copy it to the SD-card root and rename it to exactly `r1.upt`. The R1 will
   not recognize the update otherwise.
3. Safely eject/remount the SD card if you copied it outside the player.
4. On the R1, run the normal firmware update from the device UI.
5. Wait for the update to report success and reboot.
6. After a successful boot, delete or rename SD-root `r1.upt` so the updater
   does not keep offering the same update.
7. Go to Music and run `Update Database`, then wait for the scan to complete.

### ADB-Assisted Install

If you have ADB set up, see [DEVELOPMENT.md](DEVELOPMENT.md) for the verified
staging workflow. The staging script verifies the package, refuses known bad
packages, and pushes the file to the SD card.

## After Installing

1. Remove or rename SD-root `r1.upt`.
2. Put music under `/Music` and audiobooks under `/Audiobooks`.
3. Run the normal on-device Music scan/update.
4. Wait about a minute for the audiobook DB watcher, or reboot once.
5. Open `Audiobooks` from the main launcher.
6. Choose `Refresh Library`, `Titles`, `Authors`, `Series`, or `Folders`.
7. Confirm book rows appear and a title/author/series row starts playback on
   the Now Playing screen.
8. Confirm normal Music Albums and Search do not list audiobooks.

## What Changes From Stock

From a UI and day-to-day use perspective:

- The main launcher label `Books` is renamed to `Audiobooks`.
- Opening `Audiobooks` shows a native Audiobooks hub instead of the old
  text-book menu.
- The Audiobooks hub has `Refresh Library`, `Titles`, `Authors`, `Series`, and
  `Folders`.
- `Refresh Library` rebuilds the audiobook catalog/views in the background and
  opens `Titles` as visible feedback.
- `Titles` shows generated book-title playlist rows.
- `Authors` shows generated `Author - Title` rows.
- `Series` shows generated series rows for books that have series metadata or a
  series-like folder structure. Standalone books are not forced into a fake
  series.
- `Folders` opens the SD-card `/Audiobooks` folder for normal folder browsing.
- The launcher uses a dedicated Audiobooks icon.
- Tapping a title/author/series row starts playback through the stock audio
  player path and switches to the Now Playing screen.
- The firmware remembers a separate resume point for each audiobook, including
  multipart books, across listening to music and across reboots.
- An audiobook must play for at least 15 seconds before the current position is
  saved. Very quick starts, wrong taps, and short previews are intentionally
  ignored.
- If a multipart book resumes from a later file, the runtime attempts to select
  the saved file directly and seek to the saved position.
- If playback reaches within 45 seconds of the end of the whole book, the book
  is treated as completed; the next title tap starts it from the beginning.
- Pressing Back from generated Audiobooks views usually returns to the
  Audiobooks hub, and edge-back returns cleanly from the Folders root.
- Audiobook files are kept out of normal Music Albums and Search catalog
  tables. The database keeps one internal `Audiobook` route row so the
  Audiobooks section can open reliably after scans.
- Folder browsing still works, so files under `/Audiobooks` can still be found
  through file/explorer style views.
- The old text-file Books/TXT reader launcher flow is replaced by Audiobooks.
- Native DSD is enabled for the analog output path.
- Bluetooth starts with SBC XQ quality enabled when SBC is used and the
  receiving device supports it.
- USB DAC related settings/flags are unlocked. On the test R1, USB audio input
  worked after a clean reboot with the USB working mode set from the stock
  settings.
- The About/version strings show the custom build, although the R1 UI may
  truncate the visible suffix to something like `1.6.18-`.

The stock Music player behavior is otherwise intentionally preserved: normal
music playback, Now Playing, progress bar, physical controls, and the file
explorer remain stock-style.

## Audio Unlock Notes

- **Native DSD**: play DSD files normally; analog output is configured for
  native DSD.
- **Bluetooth SBC XQ**: automatic when SBC is used and the device supports it.
- **USB DAC**: exposed through stock Settings/System USB working mode. Lightly
  tested.

## Folder And Metadata Expectations

Use these SD-card folders:

```text
/Music
/Audiobooks
```

Recommended audiobook layout:

```text
/Audiobooks/Author/Year - Book Title/01 - Chapter.mp3
/Audiobooks/Author/Year - Book Title/02 - Chapter.mp3
```

Single-file books such as `.m4b` files are fine:

```text
/Audiobooks/Author/Year - Book Title/Book Title.m4b
```

For best results, especially if you want series support:

```text
/Audiobooks/Author/Series/2020 - Book Title [Series 02]/01 - Chapter.mp3
/Audiobooks/Author/2021 - Standalone Book/01 - Chapter.mp3
```

Standalone books do not need a fake series folder.

Metadata recommendations:

- `TALB` / Album = book title
- `TIT2` / Title = chapter or file display title
- `TPE2` / Album Artist = author
- `TPE1` / Artist = author, narrator, or both
- `TCOM` / Composer = narrator (optional)
- Track numbers or numbered filenames for multipart books

The genre tag does not need to be exactly `Audiobook`; files under
`/Audiobooks` are normalized into the Audiobooks section by the on-device
helper after Music -> Update Database runs. If metadata is missing, the helper
derives basic author, book title, chapter/title, and order from folder and
filename structure. MP3Tag works well for this; the Seanap/Plex-style
convention (album = book title, album artist = author) is a good fit.

## FAQ

**Will this work on the R1 MIDI?**
No. This mod is for the normal HiBy R1 on stock firmware 1.6 only.

**How do I revert to stock firmware?**
Put an official stock HiBy R1 1.6 `r1.upt` on the SD card and run the normal R1
update/recovery flow.

**What if the update boots to a black screen?**
Use the normal R1 flash/recovery flow with a stock 1.6 `r1.upt`. This is a
known risk with any unofficial firmware.

**Does ADB persist after reboot?**
ADB does not persist in practice on the test R1; it must be manually re-enabled
after reboot or update.

**Is there an audiobook search?**
There is currently no audiobook search UI. Browse by Titles, Authors, Series, or
Folders.

**What happened to the old Books/TXT reader?**
The old text-file Books/TXT reader launcher flow is replaced by Audiobooks.

**Why does the About screen show a shortened version?**
The stock UI truncates the visible suffix. The full `1.6.18-audiobook` marker is
in `/etc/r1_audiobook_version` and `/usr/resource/config.json`.

**What is the `_views` folder under Folders?**
It contains the playlist files used by the `Titles`, `Authors`, and `Series`
generated views. It is safe to ignore.

## Known Quirks

- Back navigation from generated views usually returns to the Audiobooks hub.
  Edge-back is most reliable from the Folders root.
- The Folders view may show the generated `_views` folder (playlist files).
- Title selection can take a second or two. Multipart resume may briefly show
  the track list while landing on the saved file.
- Resume saves are delayed until 15 seconds of playback.
- The stock play-mode UI is shared with music; the firmware guards audiobook
  play mode while an audiobook is active.
- If the SD card is replaced, run Music scan/update and wait or reboot so the
  watcher can rebuild catalogs.
- Per-book resume records survive SD-card replacement but may not match a
  different card's reorganized files.

## Attribution And Sources

This project is unofficial and is not affiliated with or endorsed by HiBy. HiBy,
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

## Links

- [Releases](RELEASES.md) — consolidated release hash table
- [Development](DEVELOPMENT.md) — build, verify, flash, and test documentation
- [Roadmap](ROADMAP.md) — forward-looking work items
- [Changelog](CHANGELOG.md) — release history
- [Dev History Archive](docs/dev-history/firmware-improvement-plan-archive.md) — historical development tracking