# Production Release Checklist

This checklist tracks what was verified for the current HiBy R1 audiobook firmware release and what remains useful for follow-up validation.

## Current Public Release

- Public release: GitHub `v1.4.0`, firmware marker `1.6.15-audiobook`.
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Package MD5: `8f3ecb1f377493b84dbe80d947c89ecd`.
- Package SHA256: `fa637ed2e4d6f21bf77014f6fc9bbcb9aed10aa6b3b58b8c52dd387465f639dc`.
- Installed verification passed on 2026-06-11 under `work\installed-release-verification\20260611-200902`.
- Self-contained DB maintenance passed live seed-rebuild testing under `work\watcher-seed-rebuild-test-20260610-152923`.

## Verified Release Build

- Version marker: `1.6.15-audiobook`.
- Package: `work\audiobook-firmware-1.6.15-dbwatch-lock-candidate\r1-audiobooks-1.6.15-audiobook.upt`.
- Package MD5: `8f3ecb1f377493b84dbe80d947c89ecd`.
- Package SHA256: `fa637ed2e4d6f21bf77014f6fc9bbcb9aed10aa6b3b58b8c52dd387465f639dc`.
- Rootfs MD5: `6570f9b846ae9a7756b2bef7d3b83212`.
- Rootfs SHA256: `359ca24ddace9de794af82b589ebb75c2adb6e5b53630222d00fe8a5ba7240d3`.
- Visible About version is expected to render as a truncated `HiBy R1 1.6.15-` style string because the stock UI truncates the longer suffix.
- Local package verifier passed on 2026-06-11 with `--require-db-maintenance`.
- Installed release-clean verifier passed on 2026-06-11 under `work\installed-release-verification\20260611-200902`.
- Installed package archive: none; SD-root `r1.upt` was removed after the successful flash.
- Post-flash state: daemon and DB watcher started from init, one DB watcher process was active, the watcher lock PID matched that process, SD-root `r1.upt` was absent, checked DB/catalog passed release invariants, log rotation was installed in both runtime scripts, the DB watcher lock was installed, and no known development artifacts remained active under `/usr/data/audiobooks`.
- Live post-flash music smoke test passed: normal music playback produced zero audiobook position reads/saves and DB mtime-only churn was skipped.
- Live post-flash resume smoke test passed: selecting `Holidays on Ice` from Audiobooks restored to the saved position around 12 minutes, DB mtime-only churn was skipped, and playback was paused afterward.
- Installed book catalog report from `work\installed-release-verification\20260611-200902\catalog-books.tsv`: six books, two authors, two series, four standalone books, and three multipart books.

## Recommended Follow-Up After Publishing

- Real-world listening: continue using `1.6.15-audiobook` for normal music plus audiobook sessions, including switching between music and at least two books, and watch for freezes, random reboots, battery drain, or failed resume.
- Reboot behavior: repeat a reboot after normal listening and confirm the player returns to the launcher, starts the daemon/watcher, and keeps Audiobooks responsive.
- Fresh-card behavior: if practical, test a second SD card or a renamed `/Audiobooks` tree, run Music -> Update Database, and confirm the watcher rebuilds catalogs without PC/ADB DB installation.
- Music separation: installed verifier already passed DB/catalog checks; optionally repeat UI checks in Music Albums, Genres, Search, and Files after longer use.
- Recovery package: keep stock HiBy R1 1.6 `r1.upt`, the `1.6.15` release, hashes, and recovery instructions together.
- Release packaging: upload `r1-audiobooks-1.6.15-audiobook.upt`, update README install text, create release notes that call out the DB watcher lock, mtime-only DB skip, staging helper cleanup, and retained multipart resume behavior.
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
  -OutDir work\audiobook-firmware-1.6.15-dbwatch-lock-candidate `
  -OutputUpt work\audiobook-firmware-1.6.15-dbwatch-lock-candidate\r1-audiobooks-1.6.15-audiobook.upt `
  -IncludeAudiobookLauncherGenre `
  -IncludeAudiobookTitleAutoStartMarker `
  -IncludeAudiobookResumeRuntime `
  -IncludeAudiobookDbMaintenance `
  -AudiobookResumeCatalog work\audiobook-resume-catalog-v1.4.tsv `
  -CustomVersionId 1.6.15-audiobook `
  -CustomVersionLabel "HiBy R1 Audiobook FW 1.6.15"
```

Verify:

```powershell
python tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-1.6.15-dbwatch-lock-candidate `
  --upt-name r1-audiobooks-1.6.15-audiobook.upt `
  --expected-version 1.6.15-audiobook `
  --expected-label "HiBy R1 Audiobook FW 1.6.15" `
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
  -Package work\audiobook-firmware-1.6.15-dbwatch-lock-candidate\r1-audiobooks-1.6.15-audiobook.upt `
  -BuildOutDir work\audiobook-firmware-1.6.15-dbwatch-lock-candidate `
  -ExpectedVersion 1.6.15-audiobook `
  -ExpectedLabel "HiBy R1 Audiobook FW 1.6.15" `
  -IUnderstandThisStagesFirmware
```

Post-reboot installed-release verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 1.6.15-audiobook `
  -RequirePlayModeGuard `
  -CaptureFramebuffer
```

Dry-run development artifact archive:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_archive_audiobook_dev_artifacts.ps1 `
  -RemoteArchiveRoot /usr/data/mnt/sd_0/.r1-audiobook-backups/dev-artifacts
```
