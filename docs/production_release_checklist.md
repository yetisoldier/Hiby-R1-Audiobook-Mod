# Production Release Checklist

This checklist tracks what must be true before calling the HiBy R1 audiobook firmware a production release instead of a beta/release-candidate.

## Current Public Release

- Public release: GitHub `v1.0.0`, firmware marker `1.6.4-audiobook`.
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Package MD5: `71c8d0d94bf50529a06aa9a31350f595`.
- Package SHA256: `02b286676d93ec683307820e1ef40288f34ef21a42a24f5cbda361f2d3733b7b`.
- Installed verification passed on 2026-06-10 under `work\installed-release-verification\20260610-153805`.
- Self-contained DB maintenance passed live seed-rebuild testing under `work\watcher-seed-rebuild-test-20260610-152923`.

## Verified Next Candidate

- Candidate version marker: `1.6.7-audiobook`.
- Candidate package: `work\audiobook-firmware-1.6.7-candidate\r1-audiobooks-1.6.7-audiobook.upt`.
- Candidate package MD5: `7a5b0267811de7198039aa96144f3f8c`.
- Candidate package SHA256: `2ac14cdd858f91af99cff8365c5d0664ca3d01233a89bc82e8ba010c7dfcbd78`.
- Candidate rootfs MD5: `c6346d46b2927d8719425117a5d0dd17`.
- Candidate rootfs SHA256: `1f059947f8150d9c750b2bae896efff24026876b8abaaff8b5bbe4ce7159fd1f`.
- Visible About version is expected to render as a truncated `HiBy R1 1.6.7-a` style string because the stock UI truncates the longer suffix.
- Local package verifier passed on 2026-06-11 with `--require-db-maintenance`.
- Installed release-clean verifier passed on 2026-06-11 under `work\installed-release-verification\20260611-094702`.
- Installed package archive: `/usr/data/mnt/sd_0/r1-audiobooks-1.6.7-audiobook-installed-20260611-094611.upt`.
- Post-flash state: daemon and DB watcher started from init, `user.ini` had no audiobook saved-last references, SD-root `r1.upt` was absent, checked DB/catalog passed release invariants, and no known development artifacts remained active under `/usr/data/audiobooks`.
- Live post-flash resume smoke test passed: selecting `When You Are Engulfed in Flames` from Audiobooks exercised the new near-miss transport fallback from `13/30` to `15/30` with two Next events, then restored the saved `15/30` position around `05:30`.
- Installed book catalog report from `work\installed-release-verification\20260611-094702\catalog-books.tsv`: six books, two authors, two series, four standalone books, and three multipart books.
- Two-minute paused runtime monitor after the smoke test under `work\runtime-monitor\post-1.6.7-paused-20260611-0958`: CPU samples were about 90% idle, battery reported full on USB, and the resume daemon plus DB watcher stayed resident.

## Required Before Promoting 1.6.7

- Real-world listening: use `1.6.7-audiobook` for normal music plus audiobook sessions, including switching between music and at least two books, and watch for freezes, random reboots, battery drain, or failed resume.
- Reboot behavior: repeat a reboot after normal listening and confirm the player returns to the launcher, starts the daemon/watcher, and keeps Audiobooks responsive.
- Fresh-card behavior: if practical, test a second SD card or a renamed `/Audiobooks` tree, run Music -> Update Database, and confirm the watcher rebuilds catalogs without PC/ADB DB installation.
- Music separation: installed verifier already passed DB/catalog checks; optionally repeat UI checks in Music Albums, Genres, Search, and Files after longer use.
- Recovery package: keep stock HiBy R1 1.6 `r1.upt`, the `1.6.7` candidate, hashes, and recovery instructions together before publishing.
- Release packaging: if promoted, upload `r1-audiobooks-1.6.7-audiobook.upt`, update README install text, create release notes that call out the play-mode guard and near-miss multipart resume fallback, and keep `1.6.4` available as the previous stable release.
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
  -OutDir work\audiobook-firmware-1.6.7-candidate `
  -OutputUpt work\audiobook-firmware-1.6.7-candidate\r1-audiobooks-1.6.7-audiobook.upt `
  -IncludeAudiobookLauncherGenre `
  -IncludeAudiobookTitleAutoStartMarker `
  -IncludeAudiobookResumeRuntime `
  -IncludeAudiobookDbMaintenance `
  -AudiobookResumeCatalog work\audiobook-resume-catalog.tsv `
  -CustomVersionId 1.6.7-audiobook `
  -CustomVersionLabel "HiBy R1 Audiobook FW 1.6.7"
```

Verify:

```powershell
python tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-1.6.7-candidate `
  --upt-name r1-audiobooks-1.6.7-audiobook.upt `
  --expected-version 1.6.7-audiobook `
  --expected-label "HiBy R1 Audiobook FW 1.6.7" `
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
  -Package work\audiobook-firmware-1.6.7-candidate\r1-audiobooks-1.6.7-audiobook.upt `
  -BuildOutDir work\audiobook-firmware-1.6.7-candidate `
  -ExpectedVersion 1.6.7-audiobook `
  -ExpectedLabel "HiBy R1 Audiobook FW 1.6.7" `
  -IUnderstandThisStagesFirmware
```

Post-reboot installed-release verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 1.6.7-audiobook `
  -RequirePlayModeGuard `
  -CaptureFramebuffer
```

Dry-run development artifact archive:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_archive_audiobook_dev_artifacts.ps1 `
  -RemoteArchiveRoot /usr/data/mnt/sd_0/.r1-audiobook-backups/dev-artifacts
```
