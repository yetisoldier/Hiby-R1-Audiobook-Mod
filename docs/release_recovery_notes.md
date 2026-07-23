# HiBy R1 Audiobook Firmware Release Notes

These notes are for the normal HiBy R1 on stock firmware 1.6, not the R1 MIDI.

> **Current release:** v2.0.23, firmware marker `2.0.23`, package
> `r1-audiobooks-2.0.23.upt`. The early v1.6.x sections below are retained as
> historical recovery records; current release details are in
> [`firmware/releases/v2.0.23/RELEASE_NOTES.md`](../firmware/releases/v2.0.23/RELEASE_NOTES.md).

## Current Release

- GitHub release: `v1.6.1`
- Custom version marker: `1.6.18-audiobook`
- Firmware package: `work\audiobook-firmware-1.6.18-audiobook\r1-audiobooks-1.6.18-audiobook.upt`
- Firmware MD5: `e3dba87c24ef84196ec1c91fe3c3e26a`
- Firmware SHA256: `e42d70d84bf3353391c16fa60f83f399d2624226d2792f3c7882d9a1bbe45253`
- Rootfs MD5: `dd47cf5f338d70ecab1f8be108529505`
- Rootfs SHA256: `bfac581b61ff87c133bb5eb085a5ce5bb56db10678bae84697fae04d8697f8e6`
- `hiby_player` MD5: `cd4d2812ab3425174b52925766424d2b`

Local package verification passed on 2026-06-22. The public
`1.6.18-audiobook` package was then flashed and installed-device verification
passed on the test R1. Installed artifacts are under
`work\installed-release-verification\20260622-150507`.

Installed verification confirmed:

- Native DSD, Bluetooth SBC XQ, and USB DAC markers are present.
- Resume daemon and DB watcher are running.
- DB helper `--needs-maintenance` reports no required maintenance.
- DB integrity is `ok`.
- Audiobooks contains 135 media rows across 6 books on the test SD card.
- Title, author, and series sidecar catalogs are present.
- Music search and album tables have no audiobook leakage.
- The Audiobooks hub contains `Refresh Library`, `Titles`, `Authors`, `Series`,
  and `Folders`.
- `Refresh Library` opens the Titles view as feedback, writes a manual refresh
  request, and logs refresh completion under `/usr/data/audiobooks/refresh.log`.
- No known development artifacts remain under `/usr/data/audiobooks`.
- Audiobooks rows are rebuilt from files under `/Audiobooks`, so genre tags do
  not need to be exactly `Audiobook`.

## Install

1. Keep a known-good stock HiBy R1 1.6 `r1.upt` available for recovery.
2. Copy `r1-audiobooks-1.6.18-audiobook.upt` to the SD-card root.
3. Rename the copied file to exactly `r1.upt`.
4. Run the firmware update from the R1 UI.
5. After the update succeeds and the player reboots, delete or rename SD-root
   `r1.upt`.
6. Go to Music and run `Update Database`, then wait for the scan to complete.
7. Open Audiobooks.

## Verify

After flashing, optional ADB verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 1.6.18-audiobook `
  -ExpectNativeDsd `
  -ExpectBluetoothSbcXq `
  -ExpectUsbDacMode `
  -CaptureFramebuffer
```

## Recovery

If the player ever fails to boot normally, reinstall the official stock HiBy R1
1.6 firmware with the normal SD-card recovery/update process. This mod is based
on stock 1.6 and should be reversible by reinstalling stock firmware.

Keep these together for recovery and comparison:

- Official HiBy R1 1.6 stock `r1.upt`
- This release package
- `MD5SUMS.txt`
- `SHA256SUMS.txt`

## Previous Release

The previous public release was `v1.6.0`, firmware marker
`1.6.17-audiobook`.

---

## v2.0.23 note (2026-07-23, SD runtime stability)

The current public release is **v2.0.23** (about-screen label "HiBy R1
2.0.23").

- GitHub release: `v2.0.23`
- Package: `r1-audiobooks-2.0.23.upt` (42,217,472 B)
- MD5: `11ddcf7e8d93eefc1038662d4d324830`
- SHA256: `c366a2b5a78a7943b20fab619a8e20d26c61d17f374dab66e34436f99f40f653`
- Source branch: `codex/sd-runtime-stability`

It adds an app-scoped SD runtime-power guard and reduces periodic resume writes
after an overnight freeze was traced to the stock MMC resume path. See
[`modding/sd_runtime_stability.md`](./modding/sd_runtime_stability.md).

## v2.0.22 note (2026-07-22, NativeApp stability line)

The previous public release was **v2.0.22** (about-screen label "HiBy R1
2.0.22"). Its hook is byte-identical to the hands-on-tested RC11 hook.

- GitHub release: `v2.0.22`
- Package: `r1-audiobooks-2.0.22.upt` (42,213,376 B)
- MD5: `d5dfdf3e0977d9339ab0ae862f4b3bf5`
- SHA256: `28dd05c76b203ea29298a7a59eafc036431e1c5b18760455913e751048f7f141`
- Source branch: `codex/r1-hiby-modding-integration`

The normal data-preserving ADB recovery flow is unchanged:

```bash
MSYS_NO_PATHCONV=1 adb -s ingenic_2233 push work/audiobook-firmware-2.0.22/r1-audiobooks-2.0.22.upt /usr/data/mnt/sd_0/r1.upt
adb -s ingenic_2233 shell md5sum /usr/data/mnt/sd_0/r1.upt
MSYS_NO_PATHCONV=1 adb -s ingenic_2233 shell /usr/bin/bootmode.sh Recovery
adb -s ingenic_2233 reboot
```

The public v2.0.20 package came from a separate UTF-8/Cyrillic experiment and
is not the base of this stability line. Users who require Cyrillic text should
remain on v2.0.20; everyone else should use v2.0.23.

## v2.0.18 note (2026-07-20, NativeApp pivot)

An earlier public release, **v2.0.18** (about-screen label "HiBy R1
2.0.18"). It is the v2.0.x "NativeApp" pivot (in-process `LD_PRELOAD` hook into
`hiby_player`), not the v1.6.x resume-daemon line described above. The sections
above are retained as the historical v1.6.1 record.

- GitHub release: `v2.0.18`
- Package: `r1-audiobooks-2.0.18.upt` (42,213,376 B)
- MD5: `113cac24a543f8c3b681472aeaa00302`
- SHA256: `0c524714f58b00ba75701fa3423c5f3edabbd249218afbc45c21b9dc20cadbed`
- Source branch: `codex/r1-hiby-modding-integration` (`main` has no
  `audiobook_app/`; release `--target` = codex).
- Only audiobook code change vs v2.0.17: the scanner tag reader
  (`audiobook_app/tags.c`) so long audiobooks report their real duration (MP3
  VBR Xing/Info/VBRI parse + M4B moov-mmap mvhd). The three stock unlocks
  (USB DAC, Native DSD, SBC XQ) are unchanged. After install, open the
  Audiobooks app and tap Refresh Library once to correct stored durations.

### Flash (data-preserving, ADB-driven)

```bash
MSYS_NO_PATHCONV=1 adb -s ingenic_2233 push work/audiobook-firmware-2.0.18/r1-audiobooks-2.0.18.upt /usr/data/mnt/sd_0/r1.upt
adb -s ingenic_2233 shell md5sum /usr/data/mnt/sd_0/r1.upt
MSYS_NO_PATHCONV=1 adb -s ingenic_2233 shell /usr/bin/bootmode.sh Recovery
adb -s ingenic_2233 reboot
```

The same gotchas as v2.0.17 apply (see the v2.0.17 note below): `bootmode.sh
Recovery` does NOT reboot (run `adb reboot` separately); `MSYS_NO_PATHCONV=1`
required on git-bash; use `bootmode.sh Recovery` not `/data/recovery_all`;
target `ingenic_2233`.

## v2.0.17 note (2026-07-20, NativeApp pivot)

An earlier public release, **v2.0.17** (about-screen label "HiBy R1 2.0.17").
Superseded by v2.0.18 above. It is the v2.0.x "NativeApp" pivot (in-process
`LD_PRELOAD` hook into `hiby_player`), not the v1.6.x resume-daemon line
described above. The sections above are retained as the historical v1.6.1
record.

- GitHub release: `v2.0.17`
- Package: `r1-audiobooks-2.0.17.upt` (42,213,376 B)
- MD5: `57ff9f1cd47420fe0ac71231139adf5d`
- SHA256: `1d50f0aa2b217d0d68135dd5dfa22912f2f1911f921e2736f1f46eac80282fa5`
- Source branch: `codex/r1-hiby-modding-integration` (`main` has no
  `audiobook_app/`; release `--target` = codex).
- Restores the USB DAC, Native DSD, and Bluetooth SBC XQ unlocks the pivot had
  dropped. No audiobook code changed from v2.0.16.

### Flash (data-preserving, ADB-driven)

```bash
MSYS_NO_PATHCONV=1 adb -s ingenic_2233 push work/release-v2.0.17/r1-audiobooks-2.0.17.upt /usr/data/mnt/sd_0/r1.upt
adb -s ingenic_2233 shell md5sum /usr/data/mnt/sd_0/r1.upt
MSYS_NO_PATHCONV=1 adb -s ingenic_2233 shell /usr/bin/bootmode.sh Recovery
adb -s ingenic_2233 reboot
```

**Gotchas corrected here:**

- `bootmode.sh Recovery` does **NOT reboot** — it only writes the
  `ota:kernel2` marker to `mtd5`. You must run `adb reboot` separately.
  (Earlier notes implied it rebooted; it does not.) A first flash that wrote
  the marker but never rebooted left the device on v2.0.16 with nothing
  applied; the second attempt (explicit `adb reboot`) worked.
- `MSYS_NO_PATHCONV=1` is required on Windows git-bash so `/usr/...` is not
  mangled to `C:/Program Files/Git/usr/...` (silent no-op).
- Use `bootmode.sh Recovery` (writes `ota:kernel2` → data-preserving firmware
  update). Do NOT use `/data/recovery_all` + `S39_recovery.recovery` — that is
  a factory reset (`rm -rf /data/*`) that wipes `/usr/data` (library.db, resume
  positions, bookmarks, BT pairings).
- Target `ingenic_2233` (the R1); a second device on the same machine
  (`ZY22JFZHDT`) is NOT the R1.

### Recovery / revert

If a flash bricks, SD-card force-flash a known-good `r1.upt` (stock 1.6, or a
prior good release like `2.0A`). The bootloader's recovery path reads the SD
root even when the main rootfs is broken. This is how v2.0.1 and v2.0.2 were
recovered — see [`docs/modding/brick_lessons_build_categories.md`](./modding/brick_lessons_build_categories.md)
for what bricked them and the cardinal rule for avoiding it.

Full flash + recovery reference: [`docs/modding/flash_and_recovery.md`](./modding/flash_and_recovery.md).
Current-release checklist: [`docs/production_release_checklist.md`](./production_release_checklist.md).
