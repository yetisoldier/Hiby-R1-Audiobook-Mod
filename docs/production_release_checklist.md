# Production Release Checklist

This checklist tracks the current public HiBy R1 audiobook firmware release and
the verification evidence that should be checked before publishing.

## Current Release Candidate

- GitHub release: `v1.6.1`
- Firmware marker: `1.6.18-audiobook`
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Package: `work\audiobook-firmware-1.6.18-audiobook\r1-audiobooks-1.6.18-audiobook.upt`
- Package MD5: `e3dba87c24ef84196ec1c91fe3c3e26a`
- Package SHA256: `e42d70d84bf3353391c16fa60f83f399d2624226d2792f3c7882d9a1bbe45253`
- Rootfs MD5: `dd47cf5f338d70ecab1f8be108529505`
- Rootfs SHA256: `bfac581b61ff87c133bb5eb085a5ce5bb56db10678bae84697fae04d8697f8e6`
- Visible About version is expected to truncate because the stock UI does not
  show the full suffix.

## Verified Changes Since v1.6.0

- Renamed the Audiobooks hub `Scan` row to `Refresh Library`.
- Replaced the old stock text-book scan action with an audiobook library refresh.
- `Refresh Library` opens the generated `Titles` view as visible feedback while
  the refresh runs in the background.
- Added `/usr/bin/r1_audiobook_refresh.sh` for a manual refresh request path,
  stale-safe locking, refresh logging, and immediate DB/catalog maintenance.
- Updated the DB watcher so manual refresh requests made during the boot/post-
  flash watcher window are still processed.
- Kept native Audiobooks hub rows for `Titles`, `Authors`, `Series`, and
  `Folders`.
- All `v1.6.0` generated title/author/series views, folder-based audiobook
  detection, resume behavior, Native DSD, Bluetooth SBC XQ, and USB DAC-related
  behavior is otherwise retained.

## Local Verification

Local package verification passed on 2026-06-22:

```powershell
python tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-1.6.18-audiobook `
  --upt-name r1-audiobooks-1.6.18-audiobook.upt `
  --expected-version 1.6.18-audiobook `
  --expected-label "HiBy R1 Audiobook FW 1.6.18" `
  --require-db-maintenance `
  --expect-audiobook-launcher-icon `
  --expect-native-dsd `
  --expect-sbc-xq `
  --expect-usb-dac-mode `
  --expect-native-hub-launcher `
  --expect-native-hub-view-rows `
  --expect-current-hashes
```

The default verifier now points at this release package, so the shorter command
is also valid:

```powershell
python tools\verify_r1_audiobook_build.py `
  --require-db-maintenance `
  --expect-audiobook-launcher-icon `
  --expect-native-dsd `
  --expect-sbc-xq `
  --expect-usb-dac-mode `
  --expect-native-hub-launcher `
  --expect-native-hub-view-rows `
  --expect-current-hashes
```

## Device Verification

The public `1.6.18-audiobook` package was flashed and installed-device
verification passed on the test R1 on 2026-06-22. The latest installed-device
artifacts are under `work\installed-release-verification\20260622-150507`.

Command used:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 1.6.18-audiobook `
  -ExpectNativeDsd `
  -ExpectBluetoothSbcXq `
  -ExpectUsbDacMode `
  -AllowStagedFirmware `
  -CaptureFramebuffer
```

Verified installed state:

- `/etc/r1_audiobook_version` and `/usr/resource/config.json` reported
  `1.6.18-audiobook`.
- Native DSD, Bluetooth SBC XQ, and USB DAC markers were present.
- Resume daemon and DB watcher were running.
- DB helper `--needs-maintenance` reported no required maintenance.
- DB integrity was `ok`.
- Audiobooks contained 135 media rows across six books on the test SD card.
- Title, author, and series catalogs were present and pulled.
- Music search and album tables had no audiobook leakage.
- No known development artifacts remained under `/usr/data/audiobooks`.
- A framebuffer capture was saved with the verification artifacts.
- Audiobook detection is based on `/Audiobooks` folder location, not an exact
  genre tag.

## Publish Commands

After release assets are staged under `firmware\releases\v1.6.1`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v1.6.1 `
  -Name "HiBy R1 Audiobook Mod v1.6.1" `
  -BodyFile firmware\releases\v1.6.1\README.md `
  -Assets "firmware\releases\v1.6.1\r1-audiobooks-1.6.18-audiobook.upt,firmware\releases\v1.6.1\MD5SUMS.txt,firmware\releases\v1.6.1\SHA256SUMS.txt"
```

Then verify:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v1.6.1 `
  -VerifyOnly `
  -Assets "firmware\releases\v1.6.1\r1-audiobooks-1.6.18-audiobook.upt,firmware\releases\v1.6.1\MD5SUMS.txt,firmware\releases\v1.6.1\SHA256SUMS.txt"
```

## Known Limitations

- ADB is not persistent in practice on the test device. It must be manually
  enabled after reboot/update for verification.
- From the Folders root, edge-back is more reliable than the left arrow.
- The generated `_views` folder may be visible under Folders.
- There is no audiobook search UI; browse by Titles, Authors, Series, or Folders.
- The DB helper provides practical fallback metadata, not a full audiobook tag
  parser. Clean folder structure and numbered multipart files still matter.
- USB DAC and Bluetooth SBC XQ are lightly tested compared with the audiobook
  features.
