# Production Release Checklist

This checklist tracks the current public HiBy R1 audiobook firmware release and
the verification evidence that should be checked before publishing. For the
deep build/flash reference see
[`docs/build_flash_verify_runbook.md`](./build_flash_verify_runbook.md); for the
change categories that have bricked the device see
[`docs/modding/brick_lessons_build_categories.md`](./modding/brick_lessons_build_categories.md).

## Current Release Candidate

- GitHub release: `v2.0.18`
- About-screen label: `HiBy R1 2.0.18`
- Base firmware: stock HiBy R1 1.6 for the normal R1, not R1 MIDI.
- Source branch: `codex/r1-hiby-modding-integration` (`main` has no
  `audiobook_app/`; release `--target` = codex).
- Package: `r1-audiobooks-2.0.18.upt` (42,213,376 B)
- Package MD5: `113cac24a543f8c3b681472aeaa00302`
- Package SHA256: `0c524714f58b00ba75701fa3423c5f3edabbd249218afbc45c21b9dc20cadbed`
- Build flags: `-IncludeAudiobookNativeApp -EnableBootAdb -UnlockNativeDsd
  -EnableBluetoothSbcXq -UnlockUsbDacMode -CustomVersionId 2.0.18
  -CustomVersionLabel "HiBy R1 2.0.18"`
- The only audiobook code change vs v2.0.17 is the scanner tag reader
  (`audiobook_app/tags.c`) — the duration fix below. The hook lib is
  `libaudiobook_hook.so` 1,644,716 B. The three stock unlocks are unchanged.

## Verified Changes Since v2.0.17

- **Fixed long-audiobook duration under-reporting** (scanner tag reader,
  `audiobook_app/tags.c`). Two independent causes: MP3 VBR files were estimated
  from the first frame's bitrate only (no Xing / Info / VBRI header parse), so
  VBR books whose first frame was a low-bitrate silence frame read as a fraction
  of their real length; and M4B books with a large `moov` atom had their `mvhd`
  duration corrupted by an atom walker that overran the 256 KB read buffer.
  The scanner now parses the Xing / Info / VBRI headers for MP3 and memory-maps
  the whole `moov` (via the existing `read_moov` helper) and reads `mvhd`
  directly for M4B. The MP3 reader also seeks to the first real MPEG frame
  regardless of ID3v2 size. Verified on-device against all 298 files on the test
  library: 44 books corrected, no regressions (Trilobyte 5.1 h → 25.5 h; Dad Is
  Fat 2.7 h → 5.4 h; Johnny 8.9 h → 11.0 h; Saint Odd 1.9 h → 9.3 h). After
  install a single Refresh Library re-scan stores the corrected durations;
  resume positions and bookmarks are untouched.
- Everything else is unchanged from v2.0.17: the three stock-feature unlocks
  (USB DAC, Native DSD, Bluetooth SBC XQ), SD-primary bookmarks, PNG +
  progressive-JPEG covers, Bluetooth A2DP output with AVRCP + wired fallback,
  SD-primary resume positions, boot ADB, the storage-full scan guard.

## Local Verification

Build (see [`docs/build_flash_verify_runbook.md`](./build_flash_verify_runbook.md)
§3 for the full command), then run the verifier:

```powershell
python tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-2.0.18 `
  --upt-name r1-audiobooks-2.0.18.upt `
  --expected-version 2.0.18 `
  --expected-label "HiBy R1 2.0.18" `
  --expect-native-dsd `
  --expect-sbc-xq `
  --expect-usb-dac-mode `
  --expect-current-hashes
```

For NativeApp builds the legacy resume-runtime / DB-watcher checks FAIL —
this is EXPECTED (v2.0.16/v2.0.17 fail them identically); only the
unlock-specific + NativeApp checks need to pass. Do not add
`--require-db-maintenance` / `--expect-native-hub-launcher` /
`--expect-native-hub-view-rows` (v1.6.x legacy flags that contradict the pivot).

Also confirm the `etc/r1_audiobook_version` marker inside the rootfs shows
`native_dsd=enabled`, `bluetooth_sbc_xq=enabled`, `usb_dac_mode=enabled`,
`boot_adb=enabled`, and version `2.0.18`. Generate `MD5SUMS.txt` /
`SHA256SUMS.txt` for the `.upt`.

## Device Verification (v2.0.18 flash sequence)

The data-preserving ADB flash path (target `ingenic_2233`, the R1 — not
`ZY22JFZHDT`):

```bash
# Stage + md5-verify
MSYS_NO_PATHCONV=1 adb -s ingenic_2233 push work/audiobook-firmware-2.0.18/r1-audiobooks-2.0.18.upt /usr/data/mnt/sd_0/r1.upt
adb -s ingenic_2233 shell md5sum /usr/data/mnt/sd_0/r1.upt

# Write the ota:kernel2 marker, then REBOOT separately (bootmode.sh does not)
MSYS_NO_PATHCONV=1 adb -s ingenic_2233 shell /usr/bin/bootmode.sh Recovery
adb -s ingenic_2233 reboot
# ~90-100 s -> ADB returns on 2.0.18
```

Verified installed state (on-device, 2026-07-20):

- `/etc/r1_audiobook_version` shows `2.0.18` with `native_dsd` /
  `bluetooth_sbc_xq` / `usb_dac_mode` / `boot_adb` = enabled.
- Clean boot (no freeze loop); data preserved (`library.db` intact,
  data-preserving recovery flash); boot ADB auto-started after clean reboot.
- The NativeApp launches via `preset main-audiobooks`; hook log
  `/tmp/.audiobook_hook.log` shows `FBIOPAN_DISPLAY` active.
- **Duration fix confirmed:** after a Refresh Library re-scan, `library.db`
  `books.total_duration_ms` corrected for all previously under-reported books
  (e.g. Trilobyte 5.106 h → 25.529 h, Dad Is Fat 2.705 h → 5.444 h, Johnny
  8.851 h → 11.030 h, Saint Odd 1.858 h → 9.292 h), matching the on-device
  probe ground truth for all 298 files.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 2.0.18 `
  -ExpectNativeDsd `
  -ExpectBluetoothSbcXq `
  -ExpectUsbDacMode `
  -AllowStagedFirmware `
  -CaptureFramebuffer
```

**Deferred to user (peripheral-dependent):** USB DAC functional toggle with a
USB cable; BT SBC XQ playback with a sink. The flags are verified present; the
user tests after publish.

## Publish Commands

After release assets are staged under `firmware\releases\v2.0.18`
(`r1-audiobooks-2.0.18.upt`, `MD5SUMS.txt`, `SHA256SUMS.txt`, `RELEASE_NOTES.md`):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v2.0.18 `
  -Name "HiBy R1 Audiobook Mod v2.0.18" `
  -BodyFile firmware\releases\v2.0.18\RELEASE_NOTES.md `
  -Assets "firmware\releases\v2.0.18\r1-audiobooks-2.0.18.upt,firmware\releases\v2.0.18\MD5SUMS.txt,firmware\releases\v2.0.18\SHA256SUMS.txt"
```

Then verify (see [`docs/github_release_process.md`](./github_release_process.md)
for the v2.0.x gotchas — ASCII BodyFile, target codex branch, 503→422 retry,
git-bash backslash stripping):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish_github_release.ps1 `
  -Tag v2.0.18 `
  -VerifyOnly `
  -Assets "firmware\releases\v2.0.18\r1-audiobooks-2.0.18.upt,firmware\releases\v2.0.18\MD5SUMS.txt,firmware\releases\v2.0.18\SHA256SUMS.txt"
```

## Known Limitations

- No background audiobook on the launcher (audio is tied to the app being
  open; a planned future improvement).
- No audiobook search UI; browse by Titles, Authors, Series, Folders.
- USB DAC and Bluetooth SBC XQ are verified present; functional playback is
  user-tested with the relevant peripheral.
- The internal `/usr/data` partition is chronically near-full (the stock music
  DB rebuilds there every boot); the scan aborts cleanly with a red flash if
  too low. See [`docs/modding/library_scan_storage.md`](./modding/library_scan_storage.md).
- PEQ requires a 1.7-beta `hiby_player` that breaks the LD_PRELOAD hook →
  excluded.