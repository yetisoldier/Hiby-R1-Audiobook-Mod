# Changelog

All public releases are for the normal HiBy R1 on stock firmware 1.6. Do not install these packages on the R1 MIDI.

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
