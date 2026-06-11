# Production Release Checklist

This checklist tracks what was verified for the current HiBy R1 audiobook firmware release and what remains useful for follow-up validation.

## Current Public Release

- Public release: GitHub `v1.3.0`, firmware marker `1.6.11-audiobook`.
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Package MD5: `208b7312800e4c26af79d9af7cd5570d`.
- Package SHA256: `bd353cd343d9968c532b2df8a9fd6ee74cb2e8dff66531bbb10a55bb5734abad`.
- Installed verification passed on 2026-06-11 under `work\installed-release-verification\20260611-180112`.
- Self-contained DB maintenance passed live seed-rebuild testing under `work\watcher-seed-rebuild-test-20260610-152923`.

## Verified Release Build

- Version marker: `1.6.11-audiobook`.
- Package: `work\audiobook-firmware-1.6.11-logcap-candidate\r1-audiobooks-1.6.11-audiobook.upt`.
- Package MD5: `208b7312800e4c26af79d9af7cd5570d`.
- Package SHA256: `bd353cd343d9968c532b2df8a9fd6ee74cb2e8dff66531bbb10a55bb5734abad`.
- Rootfs MD5: `34ac94a36a27ab32a082f340e2db260c`.
- Rootfs SHA256: `85bab0efbeb3f6931195a968eae02d3576b6edb2b0bb4f85682c5408b6a9c15c`.
- Visible About version is expected to render as a truncated `HiBy R1 1.6.11-a` style string because the stock UI truncates the longer suffix.
- Local package verifier passed on 2026-06-11 with `--require-db-maintenance`.
- Installed release-clean verifier passed on 2026-06-11 under `work\installed-release-verification\20260611-180112`.
- Installed package archive: `/usr/data/mnt/sd_0/r1-audiobooks-1.6.11-audiobook-installed-20260611.upt`.
- Post-flash state: daemon and DB watcher started from init, SD-root `r1.upt` was absent, checked DB/catalog passed release invariants, log rotation was installed in both runtime scripts, and no known development artifacts remained active under `/usr/data/audiobooks`.
- Live post-flash resume smoke test passed: selecting `Ice Like Fire` from Audiobooks restored to the saved position around 17 minutes and was paused afterward.
- Installed book catalog report from `work\installed-release-verification\20260611-180112\catalog-books.tsv`: six books, two authors, two series, four standalone books, and three multipart books.
- Three-minute runtime monitor after flashing under `work\runtime-monitor\post-1.6.11-flash-short`: no reboot, one resume daemon, one DB watcher, stable internal free space, and small capped logs.

## Recommended Follow-Up After Publishing

- Real-world listening: continue using `1.6.11-audiobook` for normal music plus audiobook sessions, including switching between music and at least two books, and watch for freezes, random reboots, battery drain, or failed resume.
- Reboot behavior: repeat a reboot after normal listening and confirm the player returns to the launcher, starts the daemon/watcher, and keeps Audiobooks responsive.
- Fresh-card behavior: if practical, test a second SD card or a renamed `/Audiobooks` tree, run Music -> Update Database, and confirm the watcher rebuilds catalogs without PC/ADB DB installation.
- Music separation: installed verifier already passed DB/catalog checks; optionally repeat UI checks in Music Albums, Genres, Search, and Files after longer use.
- Recovery package: keep stock HiBy R1 1.6 `r1.upt`, the `1.6.11` release, hashes, and recovery instructions together.
- Release packaging: upload `r1-audiobooks-1.6.11-audiobook.upt`, update README install text, create release notes that call out row-tap verification, memscan title detection, capped logs, and retained near-miss multipart resume fallback.
- Device cleanup: development-only files have been archived to SD. Future cleanup runs should still dry-run first and preserve `catalog.tsv`, `catalog-books.tsv`, `catalog-albums.txt`, `resume.d`, and logs.

## Known Limitations

- ADB is not persistent in practice on the test device. The user manually enables ADB after reboot/update for verification.
- Audiobooks uses the stock `genre\Audiobook` media route. It opens directly to the audiobook title list, but Back goes to the Genres page first; a second Back returns to the launcher.
- The visible About suffix is expected to truncate to a `1.6.x-a` style string.
- The current scanner/database path can reuse the stock on-device Music scan, then a firmware-installed watcher/helper updates audiobook rows and catalog tables. If the media DB is missing, the watcher seeds a valid empty schema and the helper scans `/Music` plus `/Audiobooks` itself. It does not parse full audiobook tags itself; when the stock scanner did not provide metadata, it derives title/author/book from folder and filename structure.

## Release-Safe Commands

Build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_r1_audiobook_firmware.ps1 `
  -OutDir work\audiobook-firmware-1.6.11-logcap-candidate `
  -OutputUpt work\audiobook-firmware-1.6.11-logcap-candidate\r1-audiobooks-1.6.11-audiobook.upt `
  -IncludeAudiobookLauncherGenre `
  -IncludeAudiobookTitleAutoStartMarker `
  -IncludeAudiobookResumeRuntime `
  -IncludeAudiobookDbMaintenance `
  -AudiobookResumeCatalog work\audiobook-resume-catalog.tsv `
  -CustomVersionId 1.6.11-audiobook `
  -CustomVersionLabel "HiBy R1 Audiobook FW 1.6.11"
```

Verify:

```powershell
python tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-1.6.11-logcap-candidate `
  --upt-name r1-audiobooks-1.6.11-audiobook.upt `
  --expected-version 1.6.11-audiobook `
  --expected-label "HiBy R1 Audiobook FW 1.6.11" `
  --require-db-maintenance
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
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_stage_verified_firmware.ps1 `
  -Package work\audiobook-firmware-1.6.11-logcap-candidate\r1-audiobooks-1.6.11-audiobook.upt `
  -BuildOutDir work\audiobook-firmware-1.6.11-logcap-candidate `
  -ExpectedVersion 1.6.11-audiobook `
  -ExpectedLabel "HiBy R1 Audiobook FW 1.6.11" `
  -IUnderstandThisStagesFirmware
```

Post-reboot installed-release verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 1.6.11-audiobook `
  -RequirePlayModeGuard `
  -CaptureFramebuffer
```

Dry-run development artifact archive:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_archive_audiobook_dev_artifacts.ps1 `
  -RemoteArchiveRoot /usr/data/mnt/sd_0/.r1-audiobook-backups/dev-artifacts
```
