# Modder Start Here

This page is for people who want to understand or extend the HiBy R1 audiobook
firmware work, not just install the finished package.

The current public firmware is `v1.6.1`, firmware marker
`1.6.18-audiobook`. It targets the normal HiBy R1 on stock firmware 1.6. It is
not tested on the R1 MIDI or other HiBy players.

## Safety Baseline

Keep these rules in mind before changing or flashing anything:

- Keep an official stock HiBy R1 1.6 `r1.upt` available for recovery.
- Do not flash a package until `tools\verify_r1_audiobook_build.py` passes.
- Use RAM-only ADB experiments first when testing route or UI patches.
- Treat `hiby_player` binary edits as high risk. A bad executable mode or bad
  code cave can boot to a black screen.
- Public builds should not enable persistent ADB by default.
- A normal reboot with `r1.upt` on the SD card does not flash by itself; the R1
  update flow still has to be invoked from the device.
- If a custom build fails to boot, reinstall the official stock firmware with
  the normal R1 update/recovery process.

## Recommended Reading Order

1. `docs\audiobook_firmware_architecture.md`
   Explains how the current mod is wired together: binary patches, resources,
   database helper, watcher, refresh action, generated views, and resume daemon.

2. `docs\build_flash_verify_runbook.md`
   End-to-end workflow for extracting stock firmware, building, verifying,
   staging, flashing, installed verification, and publishing.

3. `docs\investigation.md`
   Original stock-firmware findings: OTA layout, rootfs, `hiby_player`, media
   database schema, Books/text-reader behavior, and early prototype ideas.

4. `docs\audiobook_views_research.md`
   Route experiments and the path that led to the native Audiobooks hub with
   `Titles`, `Authors`, `Series`, `Folders`, and `Refresh Library`.

5. `docs\adb_control_tools.md`
   Live device control, screenshots, taps, screen classification, boot-ADB
   development option, and recovery helpers.

6. `docs\production_release_checklist.md`
   What must pass before a public release is trusted.

7. `docs\github_release_process.md`
   The repeatable GitHub release process and the API verification trap to avoid.

## Repository Map For Developers

- `tools\build_r1_audiobook_firmware.ps1`
  Builds a full `.upt` package from extracted stock firmware plus opt-in patches.

- `tools\verify_r1_audiobook_build.py`
  Local pre-flash verifier. It checks package hashes, rootfs contents, file
  modes, binary patch bytes, resources, runtime scripts, audio unlocks, and DB
  helper features.

- `tools\patch_hiby_player.py`
  Guarded `hiby_player` binary patcher. This is where launcher, hub, route, and
  row-cave patches are encoded.

- `tools\patch_r1_resource_text.py`
  Resource/localization patcher for user-visible text such as `Audiobooks`,
  `Refresh Library`, `Titles`, `Authors`, `Series`, and `Folders`.

- `tools\r1_audiobook_db_maint.c`
  Static MIPS helper source. It scans `/Music` and `/Audiobooks`, repairs the
  HiBy media DB, keeps audiobooks out of Music catalog/search tables, and writes
  audiobook catalogs and generated view playlists.

- `tools\r1_audiobook_db_watch.sh`
  On-device watcher that waits for the media DB to settle, runs the helper,
  handles SD-card DB mirrors, retries transient zero-row/locked DB states, and
  responds to manual refresh requests.

- `tools\r1_audiobook_refresh.sh`
  Manual refresh helper called from the `Refresh Library` row.

- `tools\r1_audiobook_resume_daemon.sh`
  On-device resume runtime. It tracks per-book position, handles completed-book
  behavior, restores multipart books, and keeps normal Music playback quiet.

- `tools\r1_audiobook_memscan.c` and `tools\r1_audiobook_direct_open.c`
  Small native helpers used by the resume runtime for safer title/track
  selection.

- `tools\r1_adb_control.py`
  Non-persistent live-control console for screenshots, taps, swipes, playback
  keys, and screen classification.

- `firmware\seed\usrlocal_media.seed.db`
  Empty seed schema used when the device has no usable media DB.

- `firmware\releases\`
  Public release packages, checksums, and release notes.

## What The Current Firmware Adds

The current mod keeps the stock audio player path but changes how audiobooks
are cataloged and launched:

- Main launcher `Books` becomes `Audiobooks`.
- Audiobooks opens a native hub with `Refresh Library`, `Titles`, `Authors`,
  `Series`, and `Folders`.
- `/Audiobooks` content is normalized into audiobook-only rows and generated
  playlist views.
- Normal Music albums/search tables stay clean.
- Each audiobook has an internal resume record.
- Multipart books can resume near the saved track and position.
- `Refresh Library` gives visible feedback by opening `Titles` and triggers a
  background rebuild.
- Native DSD, Bluetooth SBC XQ, and USB DAC-related flags are enabled, but they
  are not required for audiobook behavior.

## Development Strategy That Worked

The successful path was not to replace the whole player. The stable strategy was
to keep stock playback and build around it:

1. Let the stock scanner create or update the base media DB.
2. Repair/normalize the DB on-device after scans.
3. Add only small, guarded `hiby_player` binary patches for launcher/hub entry.
4. Use generated M3U-style view folders for title/author/series browsing.
5. Use a resume daemon for audiobook-only behavior instead of changing the stock
   player core.
6. Verify every release both locally and on the real R1.

## Things That Did Not Work Well

These paths are documented so future modders can avoid repeating them:

- Reusing the stock text Books reader directly for audio caused txt-reader
  launches, progress-bar mismatch, and playback errors.
- Early audio shims could start backend playback but did not keep Now Playing
  and progress state synchronized.
- Several direct/private route experiments opened the wrong stock views or
  rebooted the R1.
- Full QEMU system emulation is not yet a substitute for device testing because
  the X1600 peripherals and UI/audio stack are not modeled well enough.
- Persistent ADB is useful for development but should stay out of public builds
  unless a user explicitly opts into a dev build.

## External References

The project learned from the public HiBy/Rockbox/modding ecosystem. See the
attribution section in the root README and the detailed review in
`docs\hiby_modding_org_review_2026-06-16.md`.

When borrowing from other projects, keep licensing and device-target differences
front and center. R3 Pro II binaries or patches should not be copied blindly to
the R1.
