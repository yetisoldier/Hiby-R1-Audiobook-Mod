# Production Release Checklist

This checklist tracks the current public HiBy R1 audiobook firmware release and
the verification evidence that should be checked before publishing.

## Current Release Candidate

- GitHub release: `v1.6.2`
- Firmware marker: `1.6.16.5-audiobook`
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Package: `work\audiobook-firmware\r1-audiobooks-dev-safe.upt`
- Package MD5: `64fd718252935d0ebf220b43e1f86a0e`
- Package SHA256: `1410a718778b269a49165ef6fd6f0a8c67466ae600333332c3e989ff66952def`
- Rootfs MD5: `17622256b464b81026463c278dc93e5f`
- Rootfs SHA256: `6784c7341b54ea8877520154dcda21d6e497e3d571903e95eef96810915d6b32`
- Visible About version is expected to truncate because the stock UI does not
  show the full suffix.

## Verified Changes Since v1.6.1

- Replaced the old `Series` hub row with `Bkmarks`.
- Added manual bookmark save from the Now Playing screen via long-press Back.
- Added generated bookmark playlist views under `/Audiobooks/_views/Bookmarks`.
- Added bookmark-aware restore selection so opening a bookmark can prefer its
  saved position over a newer ordinary resume point for the same book.
- Relaxed the late backward-restore guard for bookmark-backed restores only.
- Kept `Refresh Library`, `Titles`, `Authors`, and `Folders`.
- All `v1.6.1` refresh-library behavior, folder-based audiobook detection,
  multipart resume, Native DSD, Bluetooth SBC XQ, and USB DAC-related behavior
  is otherwise retained.

## Local Verification

Local package verification passed on 2026-06-24:

```powershell
python tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware `
  --upt-name r1-audiobooks-dev-safe.upt `
  --expected-version 1.6.16.5-audiobook `
  --expected-label "HiBy R1 Audiobook FW 1.6.16.5" `
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

The public `1.6.16.5-audiobook` package was flashed and installed-device
verification passed on the test R1 on 2026-06-24.

Command used:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 1.6.16.5-audiobook `
  -ExpectNativeDsd `
  -ExpectBluetoothSbcXq `
  -ExpectUsbDacMode `
  -AllowStagedFirmware `
  -CaptureFramebuffer
```

Verified installed state:

- `/etc/r1_audiobook_version` reported `1.6.16.5-audiobook`.
- Native DSD, Bluetooth SBC XQ, and USB DAC markers were present.
- Resume daemon, bookmark monitor, and DB watcher were running.
- `Bkmarks` appeared in the Audiobooks hub.
- Long-press Back wrote a bookmark record and generated bookmark playlist view.
- Opening the saved bookmark launched the correct book and restored near the
  saved `214649 ms` position at roughly `03:44`.
- Audiobook detection is still based on `/Audiobooks` folder location, not an
  exact genre tag.

## Publish Commands

After release assets are staged under `firmware\releases\v1.6.2`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v1.6.2 `
  -Name "HiBy R1 Audiobook Mod v1.6.2" `
  -BodyFile firmware\releases\v1.6.2\README.md `
  -Assets "firmware\releases\v1.6.2\r1-audiobooks-1.6.16.5-audiobook.upt,firmware\releases\v1.6.2\MD5SUMS.txt,firmware\releases\v1.6.2\SHA256SUMS.txt"
```

Then verify:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v1.6.2 `
  -VerifyOnly `
  -Assets "firmware\releases\v1.6.2\r1-audiobooks-1.6.16.5-audiobook.upt,firmware\releases\v1.6.2\MD5SUMS.txt,firmware\releases\v1.6.2\SHA256SUMS.txt"
```

## Known Limitations

- ADB is not persistent in practice on the test device. It must be manually
  enabled after reboot/update for verification.
- From the Folders root, edge-back is more reliable than the left arrow.
- The generated `_views` folder may be visible under Folders.
- There is no audiobook search UI; browse by Titles, Authors, Bkmarks, or Folders.
- The DB helper provides practical fallback metadata, not a full audiobook tag
  parser. Clean folder structure and numbered multipart files still matter.
- USB DAC and Bluetooth SBC XQ are lightly tested compared with the audiobook
  features.
