# Changelog

All public releases are for the normal HiBy R1 on stock firmware 1.6. Do not install these packages on the R1 MIDI.

## v1.7.0A - 2026-07-11

Firmware marker: `1.7.0A`

Major architecture release. The resume daemon is rewritten in C, the auto-tap system uses framebuffer detection, and the automated test suite passes 3/3 on-device.

### C Resume Daemon (Major Change)

- **Replaced the shell-based resume daemon with a compiled C daemon** (`r1_audiobook_resume_daemon` v0.1.0). The C daemon runs in LIVE mode (shadow mode retired) and is faster, more reliable, and more capable than the shell daemon.
- The C daemon is a single 153 KB static MIPS binary with 11 internal modules and 170 unit tests.
- Polling is every 2 seconds during playback and every 5 seconds when idle, with 200 ms fast-poll during auto-tap sequences.
- Position saves are committed after 15 seconds of playback (configurable), with a 3-second minimum save threshold to prevent saving near-zero positions.
- The daemon reads the `hiby_player` process memory directly via `/proc/<pid>/mem` for position, duration, and track metadata. No more fragile shell parsing.

### Auto-Tap (Approach A: Framebuffer-Based)

- **The auto-tap system now uses framebuffer detection** instead of path-based `.m3u` detection. The daemon captures `/dev/fb0`, classifies the screen (launcher, list, now-playing), and injects touch events via `/dev/input/event1`.
- When a user taps an audiobook title, the daemon detects the track-list screen via framebuffer, reads the saved track index from the resume record, and taps the correct track row automatically.
- A follow-up tap is injected after playback starts to land on the Now Playing screen.
- Auto-tap fires reliably (`at_fired` > 0) where the old path-based detection always failed (`at_fired=0`).

### Autostart and Context

- The daemon activates an autostart context when entering an audiobook from idle. This allows the daemon to pre-arm and auto-tap before the track list fully renders.
- Music-to-audiobook and audiobook-to-music transitions are handled correctly. Leaving music and entering an audiobook restores the saved position and track.
- The autostart context has a 5-minute window (configurable) after which it deactivates.

### Explorer Marker Bug Fix

- Fixed a register encoding bug in the explorer marker code cave at `0x360E38`. The `lw`/`sw` instructions used `t0` (register 8) as the base instead of `s0` (register 16). Since `s0` held the marker address (`0x8E4000`) but `t0` contained garbage, the marker was never written. Fixed: `0x8D080000` → `0x8E080000`, `0xAD080000` → `0xAE080000`.

### Automated Test Suite

- Added a regression test suite (`tests/test_suite.py`) with 3 smoke tests: `test_launcher`, `test_playback`, `test_resume`.
- Added retry logic (3 retries with backoff) to `capture_screenshot` and `invoke_control` to handle USB ADB transient failures.
- Reduced per-command timeout from 120s to 30s to prevent hung tests from eating the entire test budget.
- **3/3 smoke tests pass on R1 hardware** (launcher 35s, playback 123s, resume 123s).

### On-Device Validation

- Comprehensive on-device validation plan covering 19 full tests + 5 edge cases.
- **19/19 applicable tests pass.** Music playback, audiobook hub, sorting, separation, resume, auto-tap, transitions, daemon health, and edge cases (m4b, 99 tracks, special characters).
- Daemon health: 0 crashes, 1 start (initial), 25 resume records, normal log cadence.

### Phase 2 Static Analysis

- Confirmed that `0x4EFE00` is the `.m3u` file-open callback via string analysis ("m3u" at `0x780E2C`).
- The explorer marker fires when a `.m3u` is tapped from the folder view, not when a title row is tapped from the native hub.
- Phase 3 (extended marker + direct-open pre-arm) deprioritized — the existing auto-tap works well enough.

### Other Changes

- PowerShell build scripts ported to Python (24 scripts).
- Overlay-based build system.
- Documentation consolidated.
- Roadmap updated with Phase 2 findings.

### Package

- Package: `r1-audiobooks-1.7.0A.upt` (42,369,024 bytes)
- MD5: `787a205618b35d75822c0f2d8517ed1f`
- SHA256: `8335ced3d273f32c57d33eddc74792c754691b428d9d6fa95eef31c6adaeebad`
- Rootfs MD5: `f92e1afe2bb082d4a03118e39dd904e1`
- Rootfs SHA256: `b2cd5633f61f31dc405092e9f05ce478a67764285a349d74132f3bc9800aa77c`
- Base firmware: stock HiBy R1 1.6 for the normal R1
- All v1.6.1 features retained: native Audiobooks hub, generated title/author views, DB maintenance, resume, audio unlocks.

## v1.6.1 - 2026-06-22

Firmware marker: `1.6.18-audiobook`

Hotfix for `v1.6.0`.

- Renamed the Audiobooks hub's misleading `Scan` row to `Refresh Library`.
- Replaced the stock text-book scan action behind that row with an audiobook-aware refresh action.
- Tapping `Refresh Library` now opens `Titles` as visible feedback, instead of appearing to do nothing.
- The refresh action writes a request marker and starts an immediate background audiobook catalog/view rebuild.
- Refresh logs are written to `/usr/data/audiobooks/refresh.log` with start, row-count, mirror-copy, and completion lines.
- Fixed a boot-timing edge case where a manual refresh request could be swallowed if tapped during the post-boot watcher window.
- Local verification passed for `r1-audiobooks-1.6.18-audiobook.upt`.
- Live installed-device verification passed on the test R1 with the matching `1.6.17.2-refresh-dev` build: `Refresh Library` rebuilt 135 audiobook tracks, opened the title list, DB integrity was `ok`, Music album/search leakage remained zero, and resume/DB watcher processes were running.
- Built package `r1-audiobooks-1.6.18-audiobook.upt`. MD5: `e3dba87c24ef84196ec1c91fe3c3e26a`; SHA256: `e42d70d84bf3353391c16fa60f83f399d2624226d2792f3c7882d9a1bbe45253`.
- Rootfs MD5: `dd47cf5f338d70ecab1f8be108529505`; Rootfs SHA256: `bfac581b61ff87c133bb5eb085a5ce5bb56db10678bae84697fae04d8697f8e6`.
- All `v1.6.0` native Audiobooks hub, generated title/author/series views, folder-based audiobook detection, resume behavior, Native DSD, Bluetooth SBC XQ, and USB DAC-related behavior is otherwise retained.

## v1.6.0 - 2026-06-22

Firmware marker: `1.6.17-audiobook`

Feature release after `v1.5.4`.

- Reworked the Audiobooks entry from the direct title-list flow into a native Audiobooks hub.
- The Audiobooks hub now contains `Scan`, `Titles`, `Authors`, `Series`, and `Folders`.
- `Titles` opens generated book-title playlist rows.
- `Authors` opens generated `Author - Title` playlist rows.
- `Series` opens generated series rows for books that have series metadata or series-like folders. Standalone books remain out of the Series view instead of being forced into a fake series.
- `Folders` opens the SD-card `/Audiobooks` folder and now displays a friendly `Folders` page header instead of the stock `Files` header when opened from the Audiobooks hub.
- Added a native sub-back helper for generated Audiobooks views so Titles, Authors, Series, and Folders use Audiobooks-specific page labels and return paths.
- The DB watcher/helper now generates the `/Audiobooks/_views` playlist folders used by the Titles, Authors, and Series hub views after Music -> Update Database.
- The DB helper now preserves and repairs one internal `Audiobook` route row with the correct audiobook count so the Audiobooks hub can keep finding books after scans and SD-card swaps.
- The `--needs-maintenance` check now detects missing or stale audiobook route rows and triggers repair instead of silently accepting a DB that would break the Audiobooks hub.
- Audiobook media rows now use normal SQLite text binding instead of embedded-NUL text binding, and release verification now checks for embedded-NUL audiobook text fields.
- Release-state verification now checks the internal Audiobook route row count, generated view catalogs, and route-row repair strings.
- Kept `v1.5.4` SD-card swap behavior: the watcher still tracks SD-root DB changes, can promote a clean/current SD database back to internal storage, and mirrors repaired databases.
- Kept folder-based audiobook detection: files under `/Audiobooks` do not need an exact `Audiobook` genre tag.
- Kept per-book resume, multipart resume, completed-book restart behavior, the 15-second save guard, Native DSD enablement, Bluetooth SBC XQ, and unlocked USB DAC-related settings.
- Known quirk: `Folders` may show the generated `_views` folder because that is where the on-device title/author/series playlist views live.
- Known quirk: from the Folders root, edge-back is more reliable than the left arrow for returning to the Audiobooks hub.
- Built package `r1-audiobooks-1.6.17-audiobook.upt`. MD5: `e8491f65ead4ef7a34163a67c7ee7007`; SHA256: `47b6b2aa85f0f14d13d659f0f3f987808f7d389a7a32bf7e54676388e6f82523`.
- Rootfs MD5: `d8c6a46cb4dc90624042f89224f611e6`; Rootfs SHA256: `687b83dff23319af917e19af9bb1bc1c95a7f6c915e852d175385b1c4e9d6b5f`.
- Local package verification passed with native hub view rows, DB maintenance, Audiobooks icon, Native DSD, Bluetooth SBC XQ, and USB DAC mode enabled. Installed-device verification passed on the test R1 after flashing this public-labeled package: version markers reported `1.6.17-audiobook`, DB integrity was `ok`, 135 audiobook rows across six books were present, title/author/series catalogs were pulled, one resume daemon and one DB watcher were running, and the title flow reached Now Playing with resume.

## v1.5.4 - 2026-06-22

Firmware marker: `1.6.16.5-audiobook`

Hotfix for `v1.5.3`.

- Fixed SD-card swap behavior where Music -> Update Database could update the SD-card media DB while the internal active media DB still contained rows from the previous SD card.
- The DB watcher now tracks SD-root media DB signature changes in addition to the internal DB.
- When the SD-root `usrlocal_media.db` is clean/current and the internal DB needs repair, the watcher promotes the SD DB back to the internal DB, runs the maintainer, and mirrors the repaired DB back to all active DB locations.
- Live installed-device regression test forced the internal DB to an old-card state with 298 audiobook rows while the current SD card had 135 audiobook files. The watcher logged `primary-copy reason=boot`, rebuilt 135 audiobook rows, regenerated catalogs, and left both primary and SD-root DBs with no maintenance needed.
- Installed-device verification passed afterward with DB integrity `ok`, 135 audiobook rows across 6 books, zero audiobook album/genre/search leakage, one resume daemon, one DB watcher, and about 31 MB free under `/usr/data`.
- Built package `r1-audiobooks-1.6.16.5-audiobook.upt`. MD5: `f6a0e65af41c7990f03e342fef995bad`; SHA256: `efd77a5a6f83879e76089ace072657891ff2e5475c4f0e82d812f728ad4e2816`.
- Rootfs MD5: `1797f124a92177605e776615144f323a`; Rootfs SHA256: `cf2076de6c700abd24d66dc587ac3109786829e5f589f4068e61988b0a481325`.
- All `v1.5.3` folder-based audiobook detection, resume, UI, Native DSD, Bluetooth SBC XQ, and USB DAC mode behavior is otherwise retained.

## v1.5.3 - 2026-06-22

Firmware marker: `1.6.16.4-audiobook`

Hotfix for `v1.5.2`.

- Fixed a second new-SD/new-scan case where Audiobooks could show `No music found` after Music -> Update Database even though audiobook files existed under `/Audiobooks`.
- Root cause: the stock scanner can produce a valid media database with zero audiobook rows when files under `/Audiobooks` do not have a genre tag that the stock app treats as Audiobook. The previous watcher only detected misnormalized existing audiobook rows, so a same-size DB with missing audiobook rows could be skipped.
- The DB helper now scans the actual `/Audiobooks` folder during its fast `--needs-maintenance` check and compares those paths with existing audiobook rows in the media DB.
- Folder location now wins over genre metadata: files under `/Audiobooks` are repaired into the Audiobooks section even when the genre is blank, custom, or not exactly `Audiobook`.
- Live installed-device regression test forced a same-size primary media DB with zero audiobook rows while 298 audiobook files were present on the SD card. The watcher detected `content-repair-mtime`, rebuilt 298 audiobook rows, regenerated the title/author/series catalogs, and mirrored the repaired DB to `/data/usrlocal_media.db` and the SD-root DB copy.
- Installed-device verification passed afterward with DB integrity `ok`, 298 audiobook rows across 52 books, zero audiobook album/genre/search leakage, one resume daemon, one DB watcher, and about 18 MB free under `/usr/data`.
- Built package `r1-audiobooks-1.6.16.4-audiobook.upt`. MD5: `d6ebce37c653f3756b54a7b5c3725788`; SHA256: `eefd1f060babf5930d7bae4be481d7f580edf225a128d17ab6130beced4dd404`.
- Rootfs MD5: `8728cd7ad4734f3f36efdfe6d0c1093a`; Rootfs SHA256: `394db7b39571f3cc95f04ceec1195f1fedb0abe3ac2a3dec3dbf5f7c3461c152`.
- All `v1.5.2` audiobook, resume, UI, Native DSD, Bluetooth SBC XQ, and USB DAC mode behavior is otherwise retained.

## v1.5.2 - 2026-06-17

Firmware marker: `1.6.16.2-audiobook`

Hotfix for `v1.5.1`.

- Fixed the remaining new-SD-card scan case where Audiobooks could still show `No music found` after Music -> Update Database.
- Root cause: after the DB helper normalized audiobook rows, the stock scanner could rewrite those rows back to their original genre tags without changing the media DB file size. The `v1.5.1` watcher skipped same-size DB changes as mtime-only churn, so it could miss that rewrite.
- The DB helper now has a cheap `--needs-maintenance` check that reports whether `/Audiobooks` rows are misnormalized or leaked into search.
- The DB watcher now uses that check before skipping same-size DB changes and runs a `content-repair-mtime` pass when the DB contents need repair.
- Reduced scan-time lag by repairing the primary media DB once, then copying the repaired DB to the `/data` and SD-root mirror locations instead of fully reprocessing each mirror DB.
- Reduced active audiobook resume polling from 1 second to 2 seconds while keeping the 15-second resume save cadence.
- Live runtime testing on the regression SD card confirmed all three DB copies report 298 audiobook rows, zero misnormalized audiobook rows, and zero audiobook rows in search after repair.
- Built package `r1-audiobooks-1.6.16.2-audiobook.upt`. MD5: `80c0d7295c2d55575870c4d226e83be9`; SHA256: `3109fea179b816dcdd4c1536b8973f527ef8f8b2d628942317f6b4ded62ca4c6`.
- Rootfs MD5: `35ffdbb9b401c03f1742782da0104b55`; Rootfs SHA256: `127b90ddfc92ecf2e668368e31422b5eb090c47e86011c391c30dd4b4ec4c475`.

## v1.5.1 - 2026-06-17

Firmware marker: `1.6.16.1-audiobook`

Hotfix for `v1.5.0`.

- Fixed a new-SD-card regression where Audiobooks could show `No music found` after running Music -> Update Database, even though files existed under `/Audiobooks`.
- Root cause: on some scans the stock UI reads the SD-root media database copy at `/usr/data/mnt/sd_0/usrlocal_media.db`; `1.6.16-audiobook` normalized `/usr/data/usrlocal_media.db` and `/data/usrlocal_media.db`, but did not always normalize the SD-root DB copy.
- The DB watcher now runs the audiobook maintainer against `/usr/data/usrlocal_media.db`, `/data/usrlocal_media.db`, and `/usr/data/mnt/sd_0/usrlocal_media.db` when those database files exist.
- Live verification on the regression SD card confirmed the SD-root database has integrity `ok`, contains 298 audiobook rows with normalized `Audiobook` genre values in `MEDIA_TABLE` and `MEDIA2_TABLE`, and keeps Audiobooks out of Music Search, Albums, and Genres.
- Live UI verification confirmed the Audiobooks launcher opens the title list instead of `No music found` after the hotfix.
- Built public package `r1-audiobooks-1.6.16.1-audiobook.upt`. MD5: `d30527750a071602a67f1eceb462f8cc`; SHA256: `085495646039eafb496279d3ef2625671783552ad069150c3e959e5c219d7f3f`.
- Rootfs MD5: `7a0b2a3d001ea53b079b79fbcf9c5933`; Rootfs SHA256: `26c9b68e49a3761930dcae3c95b172905d8e88108c68f59be44ffe3c0a96d942`.
- All `1.6.16-audiobook` UI, resume, audio unlock, and catalog features are otherwise retained.

## v1.5.0 - 2026-06-17

Firmware marker: `1.6.16-audiobook`

This release consolidates all development work since `1.6.15-audiobook`.

- Added the new Audiobooks launcher icon while keeping the launcher label as `Audiobooks`.
- Reduced the Audiobooks double-Back quirk. After the firmware sees the Audiobooks title/list screen, one Back from that area now triggers a guarded cleanup that returns to the main launcher instead of leaving the user on the stock Genres page.
- Improved multipart title-start resume. The runtime now favors the faster first-track plus direct-open correction path for launcher/context starts, so it can jump to the saved file and position more quickly.
- Added a direct-open helper for title-list starts. This improves the path from tapping a book title to landing on the saved multipart file.
- Added a restore-settle guard after direct-open or track correction so the stock player and Now Playing screen settle before position restore/UI seek runs. This fixes the live title-switch race found after `1.6.15`.
- Hardened audiobook title switching while another audiobook is already playing. The daemon avoids stale memory roots from the previously playing book and protects deeper saved bookmarks from accidental overwrite.
- Fixed title-list resume when the player opens a selected book at `00:00`; guarded title-start restores can now seek back to the saved bookmark instead of waiting for normal playback to advance.
- Kept the 15-second new-track commit guard and changed steady-state audiobook position saves to a 15-second cadence, reducing unnecessary internal writes while preserving resume behavior.
- Added lower-overhead path checks during normal music/non-audiobook playback so music use stays away from the heavier audiobook resume logic.
- Improved DB watcher startup and restart behavior. The watcher now waits for boot scan stability, recovers stale locks, force-clears stuck old watcher processes when needed, and exits cleanly on stop/restart.
- Improved fresh SD-card and late-mount behavior. If the first boot/update pass sees no audiobook files because the SD card is not ready yet, the watcher retries when `/Audiobooks` appears.
- Added missing/empty media DB recovery: the watcher can seed a valid empty media database, then the helper can scan `/Music` and `/Audiobooks` itself.
- Extended audiobook catalogs written under `/usr/data/audiobooks/` to include title, author, and series sidecar views for future UI work and easier debugging. The visible Audiobooks UI is still the title list.
- Kept audiobook isolation from normal Music search, album, and genre tables.
- Added Native DSD enablement for the analog output path.
- Added Bluetooth SBC XQ launch configuration for Bluetooth audio quality when SBC is used and the receiving device supports it.
- Unlocked USB DAC related settings/flags. Light real-device testing confirmed USB audio input could play through the R1 and out to a Bluetooth speaker after a clean reboot.
- Kept boot ADB out of public builds by default.
- Added local and installed verification coverage for the new audio unlocks, DB watcher boot stability, context-start guard, play-mode guard, sidecar catalogs, and framebuffer capture.
- Built public package `r1-audiobooks-1.6.16-audiobook.upt`. MD5: `4938a5d3f74204995a1bb297175da463`; SHA256: `ba3b16dc63e35abfc22cd0ac9e4324a5a2e3834ad894c42fd310f30f99c3f1e0`.
- Rootfs MD5: `48abe53dc5e83e8eeb045dfd8f4a3d17`; Rootfs SHA256: `adfdf99eefdeb2693aa8cf610780b05e60404f7d8e6dac33b0b5ef6b1c1d69ca`.
- Local package verification passed with DB maintenance, Audiobooks launcher icon, Native DSD, Bluetooth SBC XQ, USB DAC mode, and the Back/title-start runtime checks.

## v1.4.0 - 2026-06-11

Firmware marker: `1.6.15-audiobook`

- Added a DB watcher lock so duplicate watcher processes exit cleanly instead of running multiple maintenance loops.
- Improved same-size DB signature checks so music and audiobook playback timestamp churn is skipped instead of triggering maintenance.
- Verified normal music playback stays on the low-overhead path with zero audiobook position reads/saves.
- Verified audiobook title playback still restores through the Now Playing screen and resumes to the saved point.
- Hardened the ADB firmware staging helper so failed uploads remove temporary files.
- Extended local and installed-release verification to assert the DB watcher lock and mtime-only skip behavior.

## v1.3.0 - 2026-06-11

Firmware marker: `1.6.11-audiobook`

- Improved title-list resume for multipart books with row-tap verification and near-miss correction.
- Added a selected-title memory-scan helper for some title-list flows.
- Capped resume and DB maintenance log growth on the device.
- Kept audiobook rows out of Music Albums, Genres, and Search.

## v1.2.0 - 2026-06-11

Firmware marker: `1.6.9-audiobook`

- Reduced resume-daemon work during normal music playback and non-audiobook playback.
- Improved runtime stability after early public feedback about lag during music selection and shuffle.

## v1.1.0 - 2026-06-11

Firmware marker: `1.6.7-audiobook`

- Improved self-contained on-device DB maintenance.
- Continued the transition away from PC/ADB-only database setup.

## v1.0.0 - 2026-06-10

Firmware marker: `1.6.4-audiobook`

- First public audiobook firmware release.
- Renamed Books to Audiobooks.
- Added audiobook-only title browsing and stock Now Playing playback.
- Added per-book resume, including multipart books.
- Kept `/Audiobooks` content out of normal Music Albums and Genres.
- Added on-device DB maintenance after Music -> Update Database.
