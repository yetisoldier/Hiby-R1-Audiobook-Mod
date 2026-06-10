# Production Release Checklist

This checklist tracks what must be true before calling the HiBy R1 audiobook firmware a production release instead of a beta/release-candidate.

## Current Release Candidate

- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Current custom version marker: `1.6.4-audiobook`.
- Current package MD5: `71c8d0d94bf50529a06aa9a31350f595`.
- Current package SHA256: `02b286676d93ec683307820e1ef40288f34ef21a42a24f5cbda361f2d3733b7b`.
- Visible About version should render as a truncated `HiBy R1 1.6.4-a` style string because the UI truncates the longer suffix.
- Self-contained DB maintenance helper SHA256: `de40a30fda504366a137f4c6fa57670d05039108355c7f85a4a6199c7d280377`.
- Embedded seed DB MD5: `7dc472d4d9d086d22efbff24ab2fce13`.
- Local package verifier passed on 2026-06-10 with `--require-db-maintenance --expect-current-hashes`.
- Installed release-clean DB/catalog checker status: passed after the `1.6.4-audiobook` install on 2026-06-10.
- Post-flash verifier artifacts: `work\installed-release-verification\20260610-153805`.
- Installed package archive: `/usr/data/mnt/sd_0/r1-audiobooks-1.6.4-audiobook-installed-20260610-1531.upt`.
- Installed helper fresh-DB rebuild test for previous build: `work\native-db-maint\installed-helper-fresh-db-test-20260610-150158`, passed with 135 rebuilt audiobook rows and no Music album/genre/search leakage.
- Live `1.6.4` seed-rebuild test before flash: `work\watcher-seed-rebuild-test-20260610-152923`, passed from a deleted DB/catalog with 114 rebuilt Music rows, 135 rebuilt Audiobook rows, and no audiobook Music catalog leakage.
- Pre-install DB/catalog backups: copied locally under `work\release-db-install-backups\20260610-130148` and copied to SD under `/usr/data/mnt/sd_0/.r1-audiobook-backups/release-db-20260610-130148`; the large internal DB backup was removed after SD verification.
- Post-reboot check on 2026-06-10: daemon and DB watcher started from init, `user.ini` had no audiobook saved-last references, SD-root `r1.upt` was renamed to the installed archive, and the checked DB/catalog passed release invariants after boot writes.
- Development artifact cleanup on 2026-06-10: moved 20 known development leftovers from `/usr/data/audiobooks` to `/usr/data/mnt/sd_0/.r1-audiobook-backups/dev-artifacts/dev-archive-20260610-131619`; active runtime files, catalog, resume records, and logs remained in place.
- Reusable installed-release verifier passed on 2026-06-10 with artifacts under `work\installed-release-verification\20260610-140926`; no known development artifacts remained afterward.

## Required Before Production

- Fresh stock install test: `1.6.4-audiobook` flashed and verified with the installed DB watcher/helper/seed path without a PC/ADB DB install. Still useful as a final real-world smoke test with a different SD card, the normal on-device Music scan, and the watcher.
- Reboot behavior: boot-to-launcher passed after the installed custom package and release-clean DB/catalog. Still worth repeating after a longer audiobook/music usage session.
- Resume matrix: single-file resume passed post-reboot with `Squirrel Seeks Chipmunk` restoring to `41@41`. Installed-package multipart title-tap resume also passed: selecting `When You Are Engulfed in Flames` direct-selected track `14/30` after 3 list swipes and restored to `906@906` for the saved `905206 ms` bookmark; the record then updated to `925419 ms` after brief playback. Previous live tests covered music interruption and natural track transition behavior. The live `1.6.2` daemon also passed a completed-book start-over test with `Holidays on Ice`: a synthetic `completed: true` record started from the beginning and cleared completion after playback began.
- Music separation: post-reboot DB/catalog check passed with no audiobook search/album/genre leakage. UI evidence also showed Music All listing normal music only and the Genres list had no `Audiobook` entry where it would sort alphabetically.
- SD-card behavior: verify boot and Music still work when the SD card is removed or replaced; audiobook resume files may be absent, but the player must not fail. The DB watcher is designed to skip work when the SD root is absent and rebuild from `/Audiobooks` after the next stock scan.
- Updater hygiene: passed after the `1.6.4` install; SD-root `r1.upt` was renamed to `/usr/data/mnt/sd_0/r1-audiobooks-1.6.4-audiobook-installed-20260610-1531.upt`.
- Recovery package: keep stock 1.6 `r1.upt`, the custom package, hashes, and recovery instructions together.
- Device cleanup: development-only files have been archived to SD. Future cleanup runs should still dry-run first and preserve `catalog.tsv`, `catalog-roots.txt`, `catalog-albums.txt`, `resume.d`, and logs.

## Known Limitations

- ADB is not persistent in practice on the test device. The user manually enables ADB after reboot/update for verification.
- Audiobooks uses the stock `genre\Audiobook` media route. It opens directly to the audiobook title list, but Back goes to the Genres page first; a second Back returns to the launcher.
- The visible About suffix is expected to truncate to `1.6.4-a`.
- The current scanner/database path can reuse the stock on-device Music scan, then a firmware-installed watcher/helper updates audiobook rows and catalog tables. If the media DB is missing, the watcher seeds a valid empty schema and the helper scans `/Music` plus `/Audiobooks` itself. It does not parse full audiobook tags itself; when the stock scanner did not provide metadata, it derives title/author/book from folder and filename structure.

## Release-Safe Commands

Build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_r1_audiobook_firmware.ps1 `
  -OutDir work\audiobook-firmware-1.6.4 `
  -OutputUpt work\audiobook-firmware-1.6.4\r1-audiobooks-1.6.4-audiobook.upt `
  -IncludeAudiobookLauncherGenre `
  -IncludeAudiobookTitleAutoStartMarker `
  -IncludeAudiobookResumeRuntime `
  -IncludeAudiobookDbMaintenance `
  -AudiobookResumeCatalog work\audiobook-resume-catalog.tsv
```

Verify:

```powershell
python tools\verify_r1_audiobook_build.py --require-db-maintenance
```

Optional legacy PC/ADB DB catalog build for diagnostics:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_build_release_audiobook_db.ps1
```

Install checked DB and catalog:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_install_release_audiobook_db.ps1 `
  -Database work\release-db-candidate\20260610-125907\usrlocal_media.release-candidate.db `
  -Catalog work\release-db-candidate\20260610-125907\catalog.release-candidate.tsv `
  -RestartResumeDaemon `
  -MoveRemoteBackupsToSd `
  -IUnderstandThisModifiesDevice
```

Stage:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_stage_verified_firmware.ps1 -IUnderstandThisStagesFirmware
```

Post-reboot installed-release verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_verify_installed_audiobook_release.ps1 `
  -CaptureFramebuffer
```

Dry-run development artifact archive:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_archive_audiobook_dev_artifacts.ps1 `
  -RemoteArchiveRoot /usr/data/mnt/sd_0/.r1-audiobook-backups/dev-artifacts
```
