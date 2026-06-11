# HiBy R1 Audiobook Firmware Release Notes

These notes are for the normal HiBy R1 on stock firmware 1.6, not the R1 MIDI.

## Next Development Candidate

- Custom version marker: `1.6.7-audiobook`
- Firmware package: `work\audiobook-firmware-1.6.7-candidate\r1-audiobooks-1.6.7-audiobook.upt`
- Firmware MD5: `7a5b0267811de7198039aa96144f3f8c`
- Firmware SHA256: `2ac14cdd858f91af99cff8365c5d0664ca3d01233a89bc82e8ba010c7dfcbd78`
- Rootfs MD5: `c6346d46b2927d8719425117a5d0dd17`
- Rootfs SHA256: `1f059947f8150d9c750b2bae896efff24026876b8abaaff8b5bbe4ce7159fd1f`
- `hiby_player` MD5: `09997a636c94112ff76c85a6d4a8d0ff`

Local verification on 2026-06-11 passed with `--require-db-maintenance`. This candidate keeps the self-contained DB maintenance path from `1.6.4-audiobook`, guarded Now Playing play-mode correction from the `1.6.6` test build, and optional smart-rewind scaffolding via `AUDIOBOOK_RESTORE_REWIND_MS`, defaulted to exact resume. It adds a narrow near-miss transport fallback for multipart resume: when title-list/visible-row recovery lands a few tracks away and the player is already in the audiobook sequential mode, the daemon can use the R1's own Next/Previous transport to step to the saved track instead of giving up or skipping through the whole book. A runtime-only live test on the R1 moved Sedaris `13/30 -> 15/30` with two Next events, and a normal title-list flow restored Sedaris `15/30` to the saved position.

This candidate was staged to the SD card as `/usr/data/mnt/sd_0/r1.upt` on 2026-06-11 with matching byte count, MD5, and SHA-256. After flashing, the SD-root updater trigger was archived as `/usr/data/mnt/sd_0/r1-audiobooks-1.6.7-audiobook-installed-20260611-094611.upt`.

Post-flash installed-device verification passed on 2026-06-11 with artifacts under `work\installed-release-verification\20260611-094702`: `/etc/r1_audiobook_version` and `/usr/resource/config.json` report `1.6.7-audiobook`, the resume daemon and DB watcher are running, play-mode byte `3` is active, SD-root `r1.upt` is absent, `user.ini` has no saved-last audiobook references, `/usr/data` has about 13.5 MB free, DB integrity is `ok`, both media tables contain 135 audiobook rows, six audiobook books were cataloged, and there is no audiobook leakage into Music search, album, or genre tables. No known development artifacts remained in active `/usr/data/audiobooks`.

A live post-flash smoke test from the Audiobooks title list selected `When You Are Engulfed in Flames`, initially landed near the saved multipart position, exercised the new near-miss transport fallback from `13/30` to `15/30` with two Next events, restored to the saved `15/30` position at about `05:30`, and was paused afterward.

After flashing this candidate, run installed-device verification with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 1.6.7-audiobook `
  -RequirePlayModeGuard `
  -CaptureFramebuffer
```

## Current Shareable Candidate

- Custom version marker: `1.6.4-audiobook`
- Expected visible About version: truncated `HiBy R1 1.6.4-a` style string.
- Firmware package: `work\audiobook-firmware-1.6.4\r1-audiobooks-1.6.4-audiobook.upt`
- Firmware MD5: `71c8d0d94bf50529a06aa9a31350f595`
- Firmware SHA256: `02b286676d93ec683307820e1ef40288f34ef21a42a24f5cbda361f2d3733b7b`
- Rootfs MD5: `2d88686810d7b6782b56386776af7a52`
- Rootfs SHA256: `2da94366031bdaeac8c0908fccf3988d29e4296ed10776c54b5cd2504e88d3da`
- `hiby_player` MD5: `09997a636c94112ff76c85a6d4a8d0ff`
- DB helper SHA256: `de40a30fda504366a137f4c6fa57670d05039108355c7f85a4a6199c7d280377`
- Seed DB MD5: `7dc472d4d9d086d22efbff24ab2fce13`

The `1.6.4-audiobook` candidate is still stock HiBy R1 1.6 as the base image. It keeps the `1.6.3` resume behavior and adds a self-contained DB maintenance watcher/helper/seed so normal use should be: copy music to `/Music`, copy audiobooks to `/Audiobooks`, run the stock on-device Music scan, wait for the watcher to process the settled DB, and open Audiobooks. If the media DB is missing or empty, the watcher copies the embedded seed schema and the helper scans `/Music` plus `/Audiobooks` itself. This replaces the PC/ADB DB install path for normal shareable use.

Local verification on 2026-06-10 passed with `--require-db-maintenance --expect-current-hashes`: stock rootfs modes and symlink targets were preserved, rootfs entries are root-owned, the resume runtime is present, the DB maintenance helper/watcher/seed are present with expected modes, the watcher passes `/Music` to the helper, and the watcher seeds a missing media DB.

Live self-contained rebuild testing on 2026-06-10 passed before flashing `1.6.4`: starting from a deleted active DB/catalog, the updated watcher copied the embedded seed DB and the helper rebuilt 114 `/Music` rows plus 135 `/Audiobooks` rows entirely on-device. The pulled DB under `work\watcher-seed-rebuild-test-20260610-152923` had integrity `ok`, 249 rows in both playback tables, six audiobook books, zero audiobook rows in `SEARCH_TABLE`, zero album leaks, and no `Audiobook` genre. The verified package was staged to `/usr/data/mnt/sd_0/r1.upt` with matching byte count, MD5, and SHA-256.

Post-flash verification on 2026-06-10 passed for `1.6.4-audiobook` with artifacts under `work\installed-release-verification\20260610-153805`: `/etc/r1_audiobook_version` and `/usr/resource/config.json` both report `1.6.4-audiobook`, the framebuffer shows the normal launcher with `Audiobooks`, the resume daemon is running, the audiobook DB watcher is running, DB maintenance helper files are installed in rootfs and `/usr/data/audiobooks/bin`, SD-root `r1.upt` is absent after archiving, `user.ini` has no saved-last audiobook references, `/usr/data` has about 18.3 MB free, the live DB/catalog release-state check passed, and no known development artifacts remain under `/usr/data/audiobooks`. The pulled installed DB has 114 `/Music` rows and 135 `/Audiobooks` rows in both `MEDIA_TABLE` and `MEDIA2_TABLE`. The installed package archive is `/usr/data/mnt/sd_0/r1-audiobooks-1.6.4-audiobook-installed-20260610-1531.upt`.

The previous `1.6.3-audiobook` package passed post-flash verification on 2026-06-10 under `work\installed-release-verification\20260610-150139`: `/etc/r1_audiobook_version` and `/usr/resource/config.json` both report `1.6.3-audiobook`, the resume daemon is running, the audiobook DB watcher is running, DB maintenance helper files are installed in rootfs and `/usr/data/audiobooks/bin`, SD-root `r1.upt` is absent, `user.ini` has no saved-last audiobook references, `/usr/data` has about 11 MB free, the live DB/catalog release-state check passed, and no known development artifacts remain under `/usr/data/audiobooks`. The installed package archive is `/usr/data/mnt/sd_0/r1-audiobooks-1.6.3-audiobook-installed-20260610-150000.upt`.

The previous `1.6.2-audiobook` package also passed post-flash verification under `work\installed-release-verification\20260610-140926`, but it relied on a PC/ADB-installed DB/catalog for normal shareable setup.

## Install Flow

1. Keep a known-good stock `r1.upt` available before flashing.
2. Verify the custom package:

```powershell
python tools\verify_r1_audiobook_build.py --require-db-maintenance
```

3. Stage the package to the SD card as `r1.upt`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_stage_verified_firmware.ps1 -IUnderstandThisStagesFirmware
```

4. Install from the R1 updater UI.
5. After update success and reboot, manually enable ADB again. ADB is not persistent on the test device.
6. Rename or remove SD-root `r1.upt` after a successful install so the updater does not keep offering the same package.

## Database And Catalog

For `1.6.4-audiobook`, the normal database path should be self-contained on the R1:

1. Put music under `/Music`.
2. Put audiobooks under `/Audiobooks`.
3. Run the stock Music scan/update on the device.
4. Wait about a minute, or reboot once, then open Audiobooks.

The watcher skips scan work when the SD root is absent. If metadata is already in the stock DB it preserves it; otherwise it derives enough title/author/book information from folder and filename structure. If the media DB is missing or empty, it seeds a valid empty schema first, then the helper scans `/Music` and `/Audiobooks` directly.

Legacy diagnostic path: build a release-clean DB/catalog pair from the connected R1:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_build_release_audiobook_db.ps1
```

Install the checked pair:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_install_release_audiobook_db.ps1 `
  -Database work\release-db-candidate\20260610-125907\usrlocal_media.release-candidate.db `
  -Catalog work\release-db-candidate\20260610-125907\catalog.release-candidate.tsv `
  -RestartResumeDaemon `
  -MoveRemoteBackupsToSd `
  -IUnderstandThisModifiesDevice
```

The installed 2026-06-10 release-clean DB/catalog passed these checks: 135 audiobook rows in both `MEDIA_TABLE` and `MEDIA2_TABLE`, 6 audiobook book roots, 0 audiobook rows in `SEARCH_TABLE`, 0 audiobook album leaks, and no `Audiobook` genre in the Music catalog tables.

After a normal reboot on 2026-06-10 with the previous `1.6.1-audiobook` build, the R1 came back to the main launcher, the resume daemon started from init as PID 1017, `user.ini` had no audiobook saved-last references, and a pulled copy of the DB/catalog still passed the release-state checker after stock reboot writes. A post-reboot title tap on `Squirrel Seeks Chipmunk` opened Now Playing and restored to `41@41`; the resulting resume record saved `position_ms=45217`. A post-reboot multipart title tap on `When You Are Engulfed in Flames` direct-selected track `14/30` after 3 list swipes and restored to `906@906` for the saved `905206 ms` bookmark; after brief playback and pause, the resume record was still on track `14/30` at `925419 ms`.

Development-only leftovers were archived to `/usr/data/mnt/sd_0/.r1-audiobook-backups/dev-artifacts/dev-archive-20260610-131619`. Active runtime files, catalog files, resume records, and logs were left under `/usr/data/audiobooks`.

Backups from the installed 2026-06-10 DB/catalog update:

- Local backup dir: `work\release-db-install-backups\20260610-130148`
- SD-card DB backup: `/usr/data/mnt/sd_0/.r1-audiobook-backups/release-db-20260610-130148/usrlocal_media.db.pre-release-20260610-130148.bak`
- SD-card catalog backup: `/usr/data/mnt/sd_0/.r1-audiobook-backups/release-db-20260610-130148/catalog.tsv.pre-release-20260610-130148.bak`
- Pre-install DB backup MD5: `776775878efc771d0a086564075878b8`
- Pre-install catalog backup MD5: `01a163da3a9a00a874e464a5b180eb20`

The large pre-install DB backup was moved off internal `/usr/data` after verification. That raised internal free space to about 8.3 MB, and the player does not depend on the SD backup folder to boot or play music.

## Recovery

If the custom package fails to boot or the screen stays black after an update, use the R1 flash/recovery flow with the stock 1.6 `r1.upt`. After the stock restore finishes, boot normally and manually enable ADB before continuing development.

Known bad packages are blocked by `tools\adb_stage_verified_firmware.ps1` and should not be staged:

- `work\audiobook-firmware-full-dev\r1-audiobooks-full-dev.BAD-nonexec-hiby-player.upt`
- `work\audiobook-firmware-full-dev-fixed\r1-audiobooks-full-dev-fixed.BAD-black-screen-20260609.upt`

## Verification Targets

- Boot lands on the main launcher, not an audiobook Now Playing screen.
- Main launcher shows `Audiobooks` instead of `Books`.
- Audiobooks opens the book-title list and a title tap starts playback, goes to Now Playing, and resumes the saved book/file/position.
- Music Albums and Genres do not show audiobooks.
- Files/Explorer can still see the Audiobooks folder, which is acceptable.
- Reboot plus manual ADB enable still leaves the daemon running and the installed DB/catalog release-clean.
- For `1.6.4-audiobook`, the DB maintenance watcher is running and either a stock Music scan followed by the watcher, or a missing-DB seed rebuild, populates Music and Audiobooks without any PC/ADB DB install.

Run the installed-release verifier after a reboot:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_verify_installed_audiobook_release.ps1 `
  -CaptureFramebuffer
```

## Known Limitations

- ADB does not persist in practice; it has to be manually enabled after reboot/update.
- Audiobooks uses the stock `genre\Audiobook` route. Back from the audiobook title list first lands on Genres, and a second Back returns to the launcher.
- The `1.6.4` DB/catalog path can use the stock on-device Music scanner plus a firmware-installed maintenance helper, or seed a missing DB and scan `/Music` itself. It does not fully parse audiobook tags itself; fallback metadata comes from folders and filenames when the stock scanner has not already populated fields.
- Development artifacts under `/usr/data/audiobooks` can be reviewed with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_archive_audiobook_dev_artifacts.ps1 `
  -RemoteArchiveRoot /usr/data/mnt/sd_0/.r1-audiobook-backups/dev-artifacts
```
