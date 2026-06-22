# Production Release Checklist

This checklist tracks the current public HiBy R1 audiobook firmware release and
the verification evidence that should be checked before publishing.

## Current Release Candidate

- GitHub release: `v1.6.0`
- Firmware marker: `1.6.17-audiobook`
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Package: `work\audiobook-firmware-1.6.17-audiobook\r1-audiobooks-1.6.17-audiobook.upt`
- Package MD5: `e8491f65ead4ef7a34163a67c7ee7007`
- Package SHA256: `47b6b2aa85f0f14d13d659f0f3f987808f7d389a7a32bf7e54676388e6f82523`
- Rootfs MD5: `d8c6a46cb4dc90624042f89224f611e6`
- Rootfs SHA256: `687b83dff23319af917e19af9bb1bc1c95a7f6c915e852d175385b1c4e9d6b5f`
- Visible About version is expected to truncate because the stock UI does not
  show the full suffix.

## Verified Changes Since 1.6.16.5

- Native Audiobooks hub with `Scan`, `Titles`, `Authors`, `Series`, and
  `Folders`.
- Generated title, author, and series playlist views under `/Audiobooks/_views`.
- `Folders` opens `/Audiobooks` with the friendly `Folders` header when entered
  from the Audiobooks hub.
- DB helper repairs the internal `Audiobook` route row/count required by the
  native hub after scans and SD swaps.
- DB helper avoids embedded-NUL text fields for audiobook media rows.
- Release verification checks generated view catalogs, route-row repair state,
  and embedded-NUL audiobook fields.
- All `v1.5.4` SD-swap, folder-based audiobook detection, resume, Native DSD,
  Bluetooth SBC XQ, and USB DAC behavior is retained.

## Local Verification

Local package verification passed on 2026-06-22:

```powershell
python tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-1.6.17-audiobook `
  --upt-name r1-audiobooks-1.6.17-audiobook.upt `
  --expected-version 1.6.17-audiobook `
  --expected-label "HiBy R1 Audiobook FW 1.6.17" `
  --require-db-maintenance `
  --expect-audiobook-launcher-icon `
  --expect-native-dsd `
  --expect-sbc-xq `
  --expect-usb-dac-mode `
  --expect-native-hub-launcher `
  --expect-native-hub-view-rows
```

The default verifier also supports:

```powershell
python tools\verify_r1_audiobook_build.py `
  --require-db-maintenance `
  --expect-audiobook-launcher-icon `
  --expect-native-dsd `
  --expect-sbc-xq `
  --expect-usb-dac-mode `
  --expect-current-hashes
```

## Device Verification

The public `1.6.17` package passed installed verification on the test R1 on
2026-06-22. The latest verification artifacts are under
`work\installed-release-verification\20260622-141615`.

Command used:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 1.6.17-audiobook `
  -ExpectNativeDsd `
  -ExpectBluetoothSbcXq `
  -ExpectUsbDacMode `
  -AllowStagedFirmware `
  -CaptureFramebuffer
```

Verified installed state:

- `/etc/r1_audiobook_version` and `/usr/resource/config.json` report
  `1.6.17-audiobook`.
- Native DSD, Bluetooth SBC XQ, and USB DAC markers are present.
- Resume daemon and DB watcher were running.
- SD-root `r1.upt` was allowed during staged verification; remove or rename it
  after manual install.
- DB integrity was `ok`.
- Audiobooks contained 135 media rows across six books on the test SD card.
- Title, author, and series sidecar catalogs were present and pulled.
- One internal `Audiobook` route row exists with the correct audiobook count.
- Music search and album tables had no audiobook leakage.
- No known active development artifacts remained under `/usr/data/audiobooks`.
- Audiobook detection is based on `/Audiobooks` folder location, not an exact
  genre tag.
- UI smoke after flashing opened the launcher Audiobooks hub, showed `Scan`,
  `Titles`, `Authors`, `Series`, and `Folders`, and launched a generated title
  row into Now Playing with resume around the saved position.

## Publish Commands

After release assets are staged under `firmware\releases\v1.6.0`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v1.6.0 `
  -Name "HiBy R1 Audiobook Mod v1.6.0" `
  -BodyFile firmware\releases\v1.6.0\README.md `
  -Assets "firmware\releases\v1.6.0\r1-audiobooks-1.6.17-audiobook.upt,firmware\releases\v1.6.0\MD5SUMS.txt,firmware\releases\v1.6.0\SHA256SUMS.txt"
```

Then verify:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v1.6.0 `
  -VerifyOnly `
  -Assets "firmware\releases\v1.6.0\r1-audiobooks-1.6.17-audiobook.upt,firmware\releases\v1.6.0\MD5SUMS.txt,firmware\releases\v1.6.0\SHA256SUMS.txt"
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
