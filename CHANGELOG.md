# Changelog

All public releases are for the normal HiBy R1 on stock firmware 1.6. Do not install these packages on the R1 MIDI.

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
