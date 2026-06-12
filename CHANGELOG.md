# Changelog

All public releases are for the normal HiBy R1 on stock firmware 1.6. Do not install these packages on the R1 MIDI.

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
