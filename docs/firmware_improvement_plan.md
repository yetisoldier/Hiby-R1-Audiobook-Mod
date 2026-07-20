# Firmware Improvement Plan

> **⚠️ SUPERSEDED — historical record (pre-2.0, v1.6.x era).** This plan
> tracked the resume-daemon line through `1.6.11-audiobook`. The current line
> is the NativeApp pivot (v2.0.17) — for current status see `CHANGELOG.md`,
> [`audiobook_firmware_architecture.md`](./audiobook_firmware_architecture.md),
> and [`modding/`](./modding/). Retained as historical context.

This plan originally tracked post-`1.6.4-audiobook` work and now records which
items have landed in the `1.6.11-audiobook` release.

## Current Stable Baseline

- Public release: `v1.3.0`, firmware marker `1.6.11-audiobook`.
- Based on stock HiBy R1 firmware 1.6 for the normal R1.
- Audiobooks are separated from Music albums/genres/search, have a launcher
  entry, use the stock Now Playing screen, and keep per-book resume state.
- The installed DB watcher/helper can rebuild catalogs from `/Music` and
  `/Audiobooks` after the user runs Music -> Update Database.

## Current Verified Release

- `1.6.11-audiobook` is installed and verified on the test R1.
- It keeps the self-contained DB/catalog path, guarded audiobook play-mode
  correction, near-miss transport fallback, row-tap verification for title-list
  resume, selected-title memscan helper, and capped runtime logs.
- Post-flash testing opened Audiobooks, selected `Ice Like Fire`, restored to
  the saved position around 17 minutes, and paused playback afterward. A short
  runtime monitor showed no reboot or daemon duplication.

## v1.4.0 Development Focus

- Branch: `codex/r1-v1.4-stability-resume`.
- First stability target: reduce background work while normal music is playing.
- The resume daemon now treats explicit `/Music` playback as a quiet state:
  it keeps position reads and resume saves at zero, and throttles audiobook
  title-marker memory polling even if an audiobook was played recently.
- The DB watcher now ignores timestamp-only media DB churn by default. It still
  runs at boot and after size-changing database scans, but skips ordinary
  playback mtime updates so it does not rebuild audiobook tables during music.
- Runtime-only device testing on `1.6.11-audiobook` showed music playback with
  `position_reads=0`, `saves=0`, marker polling reduced to 4 polls per minute,
  and DB watcher mtime-only changes logged as skipped instead of running the
  helper.

## Useful New Information From hiby-modding

See also `docs/hiby_modding_org_review_2026-06-16.md` for the latest org-level
review and collaboration recommendation.

- `hiby-mods` documents a fuller HiBy media database builder, including wider
  format-code coverage, sidecar cover names, LRC paths, and scan/collation
  behavior.
- `hiby-mods` documents that oversized cover art can make these low-memory
  players slower; 360x360 JPEG sidecar art is a good target to test on R1.
- `hiby-mods` and `hiby_os_crack` document the OTA format, MD5 chain, boot
  sequence, stock init script behavior, and rootfs layout. Most of that matches
  the safety checks we already added, but it gives us more pre-flash verifier
  ideas.
- `hiby_os_crack` confirms that full QEMU system emulation is still research
  work because the X1600 peripherals are not modeled. User-mode or host-native
  helper testing is the practical short-term path.

## Implemented On `codex/r1-hiby-modding-integration`

- 2026-06-18 installed-device validation: the flashed
  `1.6.16.3-single-daemon-dev` build passed the stricter installed verifier
  with native DSD, SBC XQ, USB DAC mode, DB maintenance, play-mode guard,
  context-start guard, and a 600-second zero-audiobook retry window enabled.
  The verifier now checks that the resume daemon and DB watcher each have
  exactly one top-level process and that their pidfiles match those roots,
  avoiding false positives from short-lived child shells.
- Live ADB smoke testing opened Audiobooks from the main launcher, selected
  `The Road`, auto-started playback, restored to the saved position, crossed
  the 15-second resume-save threshold, and paused playback afterward. Artifacts
  from the successful installed verification are under
  `work\installed-release-verification\20260618-092713`, and the successful
  full live smoke run is under
  `work\live-audiobook-smoke\20260618-093726`.
- `tools\adb_live_audiobook_smoke.ps1` now captures an end-to-end live-device
  smoke trail: screen before/after, optional back-reset gestures, Audiobooks
  open, title-row start, playback wait, daemon log tail, process-root checks,
  and optional pause. Use it after future flashes when the R1 is on the main
  menu, or pass `-ResetBacks` to back out of Now Playing/list screens first.
  On the current UI stack, four reset-backs returns from Now Playing to the
  launcher, while three reset-backs returns from an audiobook track list to the
  launcher. The smoke runner now refuses to tap the Audiobooks launcher unless
  the screen classifier sees the launcher first, so it will not accidentally
  drive the same coordinates on a stock Music list.
- `tools\r1_adb_control.py` now has a coarse `classify` command and
  `screenshot --classify` option. The classifier distinguishes launcher, list,
  and Now Playing screens from framebuffer pixels without OCR, which lets
  unattended smoke tests catch missed taps and wrong-route screens.
- The native DB helper now strips the same leading punctuation and articles used
  by the hiby-modding database tooling before filling `character`,
  `pinyin_charater`, and before sorting generated title/album rows.
- The offline Python DB tool and release-state checker now use the same
  normalization. The release checker also validates audiobook
  `pinyin_charater` in `MEDIA2_TABLE` so the title-list side rail and sort key
  stay in sync.
- The native DB helper now writes dedicated book-level view sidecars:
  `/usr/data/audiobooks/catalog-view-title.tsv`,
  `/usr/data/audiobooks/catalog-view-author.tsv`, and
  `/usr/data/audiobooks/catalog-view-series.tsv`. These are not visible UI
  routes yet; they are pre-sorted data sources for safer Title / Author /
  Series view experiments. Standalone books are intentionally omitted from the
  series view.
- The local DB helper fixture now includes article-prefixed book titles,
  unpadded multipart track names, and a longer audiobook path.
- The local and release-state checks now validate the three view sidecars, so a
  future UI patch can rely on stable headers, normalized side-rail fields, and
  matching audiobook book roots.
- `tools\r1_audiobook_ui_route_lab.py` now lists likely stock media-list route
  candidates, scans `hiby_player` for route-like UTF-16 strings, and generates
  a RAM-only ADB test script for the next connected-device UI session.
- The firmware builder has an opt-in `-IncludeAudiobookLauncherIcon` switch
  that replaces the old Books launcher icon with same-size audiobook icons for
  the stock themes. The verifier can require this with
  `--expect-audiobook-launcher-icon`.
- The firmware builder now also has opt-in native Audiobooks hub switches:
  `-IncludeAudiobookNativeHubTitleRow` restores the stock Books hub and routes
  its `Titles` row to the current audiobook title/resume path, while
  `-IncludeAudiobookNativeHubLauncher` makes the Audiobooks launcher open the
  native hub root instead of restoring a previous Files subpage. The
  `-IncludeAudiobookNativeHubFolderRows` routes the old `Authors`/`Folders`
  rows to the native explorer rooted at `/Audiobooks`. This is a development
  path toward deeper UI integration; it is not yet a release replacement for
  the current direct title launcher.
- The verifier can require the native hub patches with
  `--expect-native-hub-title-row`, `--expect-native-hub-launcher`, and
  `--expect-native-hub-folder-rows`, and the RAM-only
  `tools\adb_probe_native_audiobook_hub.py` helper can test the same
  hub/title/folder row patches without flashing when ADB shell is healthy.
- Live testing of the first native hub launcher builds showed the private
  `titles` resource key fixes the hub row list, but the original `Titles` row
  cave opened the global Music `Genres` root. A `rowctx` candidate changed the
  row cave to pass the higher-level hub/list object (`move a0, s0`) into the
  existing audiobook route helper instead of the stock child-parent pointer
  (`lw a0, 0xd8(s0)`), but tapping `Titles` rebooted the R1. Treat that
  candidate as unsafe; the native hub still needs a better title-route or
  custom-list implementation.
- The DB watcher zero-audiobook retry window is now 600 seconds instead of 180
  seconds. This is intended to self-recover slower SD-card/mount/database
  startup cases where the first boot pass temporarily sees zero audiobook rows
  even though `/Audiobooks` appears later.
- The firmware builder has an opt-in `-DisableBatdLogger` switch. When enabled,
  it removes the guarded stock `/usr/bin/batd -v -s -t5 -o
  /mnt/sd_0/batlog.txt` launch block from `usr/bin/hiby_player.sh` and writes
  `batd_logger=disabled` to the custom firmware marker. The extracted stock
  rootfs used here does not include `/usr/bin/batd`, so this may be a no-op on
  our base firmware unless the binary exists on a device/runtime variant.
- `tools\r1_hiby_player_cave_audit.py` now audits executable zero-filled
  regions in an extracted `hiby_player` ELF and reports whether known patch and
  probe addresses are in executable file-backed mappings. This replaces
  guesswork for future RAM-only player probes.
- `tools\adb_probe_music_row.py arm-play-open` now requires an explicit
  audited `--play-open-probe-addr`, rejects the known-bad `0x75df00` address,
  refuses to overlap current audiobook caves, checks `/proc/<pid>/maps` for an
  executable `hiby_player` mapping, verifies that the destination bytes are
  empty before patching the running process, and refuses to clear the scratch
  buffer unless it is writable and already zero.
- The older music-row and album-marker RAM probes now use the same scratch
  safety check. The music-row scratch was moved out of the live `0x8b1f00`
  range and into high-BSS slack at `0x8e4600`.

Validated locally on 2026-06-16 with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_local_dev_sanity.ps1
```

That run covered PowerShell parsing, shell syntax, Python compile, resume daemon
logic, the Windows DB helper fixture, the MIPS DB helper fixture through
WSL/QEMU, and Git whitespace checks.

A device-test dev package was also built and verified locally:

- Package:
  `work\audiobook-firmware-1.6.17-ui-dev\r1-audiobooks-1.6.17-ui-dev.upt`
- Firmware marker: `1.6.17-ui-dev`
- MD5: `ea30a03d09d56828ae1d89bd03b6d44a`
- SHA256:
  `183e6116d7687057d904b86143e565d3cb3e184ad14fc25870db1c0be76bc20f`
- Rootfs MD5: `edc3d8241154234b1c6174bb7ebe20b1`
- Rootfs SHA256:
  `efe626f223f55ae23aab8c44a7b910f7695bd87ffa4b184b9f1437d9d2039cf6`
- Verification command:

```powershell
python tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-1.6.17-ui-dev `
  --upt-name r1-audiobooks-1.6.17-ui-dev.upt `
  --expected-version 1.6.17-ui-dev `
  --expected-label "HiBy R1 Audiobook FW 1.6.17" `
  --require-db-maintenance `
  --expect-batd-disabled `
  --expect-audiobook-launcher-icon
```

This package is intentionally not a public release until the R1 confirms the
normalized audiobook list behavior, the optional `batd` removal during real
music/audiobook playback, and the launcher icon rendering on the actual screen.

A follow-up direct-open dev package was built, verified, and staged on
2026-06-16:

- Package:
  `work\audiobook-firmware-1.6.18-directopen-dev\r1-audiobooks-1.6.18-directopen-dev.upt`
- Firmware marker: `1.6.18-directopen-dev`
- MD5: `29d724c7d158c1f1465493764ccaf65f`
- SHA256:
  `5b053622ee85f602441d7b71ba60ab2ef6c65d28cf472a4ebae98b821fe71a3e`
- Rootfs MD5: `e902d4d1c8e473447f0ffe5ac31ec5a2`
- Rootfs SHA256:
  `a030f482ba21ef71f9851b28a8bfdfd5f7fff1ebb7275f0cb5e72e7d792ddc1e`
- Staged as `/usr/data/mnt/sd_0/r1.upt` after byte count, MD5, and SHA-256
  verification.

This candidate adds the native one-shot direct-open helper and daemon fast path.
Live helper testing on the connected R1 opened `The Remaining Aftermath`
directly to `21/44` by forcing row index `20` while tapping visible row 1.

Post-flash testing found that `1.6.18-directopen-dev` had bundled an older
`r1_audiobook_db_maint` helper that did not understand the new
`--titles-catalog`, `--authors-catalog`, and `--series-catalog` flags used by
the watcher. The helper was rebuilt and the verifier now checks those binary
strings so that mismatch cannot pass pre-flash validation again.

Corrected direct-open dev package:

- Package:
  `work\audiobook-firmware-1.6.19-directopen-fix-dev\r1-audiobooks-1.6.19-directopen-fix-dev.upt`
- Firmware marker: `1.6.19-directopen-fix-dev`
- MD5: `c4c89309f5f60c16d9b587d29de7fdee`
- SHA256:
  `ed1edc7770229ad957306525ad9a9848226881ea71e28ec967cc7805ca0c7238`
- Rootfs MD5: `4cb1cc46aa55bfa9d437e7be19d13e60`
- Rootfs SHA256:
  `843b804d792bf6b8e92c7411ad2d444a62e5708c65b003ff799773240535c7fa`

This candidate keeps the direct-open helper, adds the pre-play direct-start
path for saved multipart tracks, and includes the corrected DB helper/view
catalog support.

Core ADB scripts now resolve `.tools\platform-tools\adb.exe` automatically when
the older `C:\Program Files\Software Fix\adb.exe` path is missing. This keeps
staging, debug collection, runtime installation, and `r1_adb_control.py`
usable from a clean shell without extra `--adb` arguments.

The follow-up DB watcher hardening waits for a stable media DB signature before
the first boot-time audiobook maintenance pass. This is meant to avoid racing
the stock Music -> Update Database scan after a new SD card or fresh firmware
flash, where an early helper run can otherwise see SQLite disk I/O errors or a
partial catalog while the stock scanner is still rewriting `usrlocal_media.db`.
The init script now exports a 180 second boot stability timeout with 3 second
polling, and the verifier requires those settings plus the watcher-side
`wait_for_stable_db boot` guard.

Boot-stability dev package built, verified, and staged on 2026-06-16:

- Package:
  `work\audiobook-firmware-1.6.20-dbboot-stable-dev\r1-audiobooks-1.6.20-dbboot-stable-dev.upt`
- Firmware marker: `1.6.20-dbboot-stable-dev`
- MD5: `3a79e38bbed6b7308ef57a03b0ec66f0`
- SHA256:
  `a475019004c531542d805e1274c4e8b0c581b01c88f010357057fc8bded3b2b8`
- Rootfs MD5: `19410fe3995fa76113a7e0ac5f8af0fa`
- Rootfs SHA256:
  `51092318907a6dbe7b12324aabe9afa6e089fad7885e284e265556aac5a92502`
- Staged as `/usr/data/mnt/sd_0/r1.upt` after local verification and remote
  byte count, MD5, and SHA-256 checks.
- Suggested post-flash verifier:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass `
    -File tools\adb_verify_installed_audiobook_release.ps1 `
    -ExpectedVersion 1.6.20-dbboot-stable-dev `
    -RequireDbMaintenance `
    -RequirePlayModeGuard `
    -RequireDbBootStabilityGuard `
    -AllowStagedFirmware `
    -CaptureFramebuffer
  ```

Post-flash testing on 2026-06-16:

- Installed marker and resource version both reported
  `1.6.20-dbboot-stable-dev`.
- The installed verifier passed with `-RequireDbBootStabilityGuard`: resume
  daemon running, DB watcher running, play-mode byte `3`, SQLite integrity OK,
  135 audiobook media rows, 6 book rows, and no audiobook leakage into
  `SEARCH_TABLE`, `ALBUM_TABLE`, or `GENRE_TABLE`.
- The new DB watcher boot path behaved as intended:
  `wait-stable reason=boot-wait`, then `stable ... age=15s`, then a successful
  boot maintenance run with 114 music tracks and 135 audiobook tracks. This
  avoided the earlier boot-time SQLite disk I/O failure seen on `1.6.19`.
- ADB title-row playback test opened `Ice Like Fire`. The stock Now Playing
  screen briefly showed `Unknown`, then populated cover art, title, artist, MP3
  info, and restored to about `17:36`.
- After running past the 15 second save guard, the `Ice Like Fire` resume record
  advanced to `1115407` ms and playback was paused at about `18:39`.
- A multipart follow-up test opened `Squirrel Seeks Chipmunk 1/3`, restored to
  about `02:10`, and was paused at about `02:32`.
- That quick book-switch test exposed a minor direct-start context quirk: after
  leaving `Ice Like Fire`, the daemon briefly attempted a visible-probe using
  the previous book context before the new Squirrel path appeared. It recovered
  and restored Squirrel correctly, but this is a good next target for reducing
  wrong pre-play attempts when switching books quickly.

## 1.6.21 Context-Guard Candidate

This candidate targets the quick book-switch quirk found during 1.6.20 testing.
When a title tap is matched only by recent audiobook context or a relaxed title
marker, the daemon now disables the selected-title memscan root shortcut for
that pre-play direct-start attempt. It can still inspect the freshly opened
track list and catalog memory, but it should not reuse the previous book root
before the newly selected book path appears.

Development package built, verified, and staged on 2026-06-17:

- Package:
  `work\audiobook-firmware-1.6.21-context-guard-dev\r1-audiobooks-1.6.21-context-guard-dev.upt`
- Firmware marker: `1.6.21-context-guard-dev`
- Visible label: `HiBy R1 Audiobook FW 1.6.21`
- MD5: `0c1ba28bb6f6bec680c2fcd2d686aa19`
- SHA256:
  `ebb2332a9014e5b754d94ed7c325995f99eb68dd9752881b0c69e81bd3528154`
- Rootfs MD5: `2bb8a4a474ab05912445524dfa223808`
- Rootfs SHA256:
  `c817dce559414bfd76d095a86dcdd86d892bcbb0b7d519972570124d5a0ab142`
- Staged as `/usr/data/mnt/sd_0/r1.upt`; the previous staged package was backed
  up on the SD card as `r1.upt.previous-20260617-070338.bak`.

Local validation passed:

- `tools\verify_r1_audiobook_build.py` with DB maintenance, disabled batd
  logger, audiobook launcher icon, direct-open helper, boot-stable DB watcher,
  and context-start guard checks.
- `tools\run_local_dev_sanity.ps1`, including Windows and QEMU MIPS DB helper
  fixtures.
- The installed 1.6.20 firmware still passed the device verifier after staging
  1.6.21, using `-AllowStagedFirmware`.

Suggested post-flash verifier:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 1.6.21-context-guard-dev `
  -RequireDbMaintenance `
  -RequirePlayModeGuard `
  -RequireDbBootStabilityGuard `
  -RequireContextStartGuard `
  -AllowStagedFirmware `
  -CaptureFramebuffer
```

Most important live test after flashing:

1. Start or resume one audiobook such as `Ice Like Fire`.
2. Leave playback and select a different audiobook title quickly, preferably a
   multipart one.
3. Confirm the new book resumes at its saved file/position and the daemon log
   does not show a stale `book-title direct-start memscan root=` from the
   previous book immediately before the new book opens.

Post-flash and runtime testing on 2026-06-17:

- Installed marker and resource version both reported
  `1.6.21-context-guard-dev`.
- The installed verifier passed with `-RequireContextStartGuard`: resume daemon
  running, DB watcher running, play-mode byte `3`, SQLite integrity OK, 135
  audiobook media rows, 6 book rows, and no audiobook leakage into
  `SEARCH_TABLE`, `ALBUM_TABLE`, or `GENRE_TABLE`.
- `Ice Like Fire` restored from the title list to about `18:45`.
- Switching from `Ice Like Fire` to `Squirrel Seeks Chipmunk` did not log a
  stale previous-book memscan root, and restored Squirrel to about `02:43`.
- Switching from Squirrel to `When You Are Engulfed in Flames` reached saved
  track `27/30` with the native direct-open helper and restored to about
  `01:57`.
- This test exposed a performance/polish issue: context-only title starts still
  spent about 20 seconds probing stale/fresh memory pointers before falling back
  to first-track start.

## 1.6.22 Fast-Context Candidate

This candidate keeps the 1.6.21 stale-root guard and further improves quick
book switching. When a title tap is matched only through recent audiobook
context or relaxed matching, the daemon now skips pre-play direct-start memory
scans entirely. It taps the first track quickly, then uses the existing
track-restore and direct-open paths to jump to the saved file and position.

Runtime-only testing on the connected R1 confirmed the fix before packaging:

- Starting from `When You Are Engulfed in Flames 27/30`, switching to
  `Squirrel Seeks Chipmunk` logged:
  `book-title direct-start skipped reason=context`.
- The new Squirrel path appeared about two seconds after the title marker and
  restored to the saved position with `after_position_response=216@216`.
- The Now Playing screen showed Squirrel at about `03:38`.

Development package built, verified, and staged on 2026-06-17:

- Package:
  `work\audiobook-firmware-1.6.22-fast-context-dev\r1-audiobooks-1.6.22-fast-context-dev.upt`
- Firmware marker: `1.6.22-fast-context-dev`
- Visible label: `HiBy R1 Audiobook FW 1.6.22`
- MD5: `83a0d3e7cd9b5c7ca398fea2a47b3b45`
- SHA256:
  `834eefb1ceedb0a151e17567777c2fd69d55ea670fb25caa5b83f4b05ffc96c8`
- Rootfs MD5: `984114b16f6dd0e98197c483724abcf6`
- Rootfs SHA256:
  `2bad13a81d4e3b397c110944e59c38b4128ac574ad4d0d2f3ba92e5883fac5a1`
- Staged as `/usr/data/mnt/sd_0/r1.upt`; the previous staged package was backed
  up on the SD card as `r1.upt.previous-20260617-072435.bak`.

Suggested post-flash verifier:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 1.6.22-fast-context-dev `
  -RequireDbMaintenance `
  -RequirePlayModeGuard `
  -RequireDbBootStabilityGuard `
  -RequireContextStartGuard `
  -AllowStagedFirmware `
  -CaptureFramebuffer
```

## 1.6.23 DB Watcher Lock Candidate

This candidate keeps the 1.6.22 fast context-switch behavior and hardens the
on-device audiobook database watcher after a post-flash verifier found that the
watcher was not running. The device had a stale `/usr/data/audiobooks/db-maint.lock`
whose saved PID had been reused by another process, so the previous watcher
logic exited as if another watcher was active.

Runtime-only testing on the connected R1 confirmed both fixes before packaging:

- A patched watcher recovered from the real stale lock and started.
- A fake stale lock pointing at the live `hiby_player` PID logged
  `stale-lock-live-pid-not-watcher`, recovered the lock, and started.
- The patched TERM trap let `start-stop-daemon -K` stop the watcher cleanly, then
  a single replacement watcher was started.

Development package built, verified, and staged on 2026-06-17:

- Package:
  `work\audiobook-firmware-1.6.23-dbwatch-lock-dev\r1-audiobooks-1.6.23-dbwatch-lock-dev.upt`
- Firmware marker: `1.6.23-dbwatch-lock-dev`
- Visible label: `HiBy R1 Audiobook FW 1.6.23`
- MD5: `c32159d55a5cbadc03c6ac3b8b779d16`
- SHA256:
  `4235a9addd653097899e55dcd1316074d7ab016229d84b250f0b2dae760ba561`
- Rootfs MD5: `8019bb55f8f78180f4eae30e1aabccb3`
- Rootfs SHA256:
  `ab59e9f4eb68d0396c1dc6d06a3a21612f3cce841a03f2fb51518329d650fcc0`
- Staged as `/usr/data/mnt/sd_0/r1.upt`; the previous staged `1.6.22` package
  was backed up on the SD card as `r1.upt.previous-20260617-080309.bak`.
- Installed verification passed on the connected R1 under
  `work\installed-release-verification\20260617-080533`.
- Follow-up live service testing on the flashed firmware confirmed:
  - fake stale lock pointing at live `hiby_player` logged
    `stale-lock-live-pid-not-watcher` and recovered;
  - `start-stop-daemon -K` stopped the watcher;
  - normal init restart left one DB watcher active.
- Live UI testing on the flashed firmware confirmed:
  - launcher Audiobooks icon opens the audiobook list/detail route;
  - `Ice Like Fire` restored from its saved bookmark and the progress bar moved;
  - after 15 seconds of playback, the `Ice Like Fire` resume record updated;
  - switching to Music and playing `All Night Parking (interlude)` produced a
    full non-audiobook stats window with `position_reads=0` and `saves=0`;
  - returning from Music to `Ice Like Fire` restored to about `22:06`, matching
    the saved `1326799` ms bookmark.
- Route-stack quirk remains: returning from an audiobook title/detail route may
  require stepping through `Genres -> Audiobook`, then broader `Genres`, before
  the launcher.
- RAM-only route probes on 2026-06-17 tested `genre-selected-audiobook`,
  `album-audiobook`, `album-selected-audiobook`, `direct-filter`,
  `artist-audiobook`, and `artist-selected-audiobook`. None provided a cleaner
  release path: the useful candidates either kept the same Back stack or opened
  global Genres/Music/`No music found` screens. The next UI-view improvement
  should target native list/query internals or a catalog-backed custom entry
  point, not only launcher route strings.
- Final post-route installed verification passed under
  `work\installed-release-verification\20260617-083216`: DB integrity OK,
  135 audiobook media rows, 6 book rows, no audiobook leakage into Music
  search/albums/genres, play-mode guard active, context-start guard active, and
  DB watcher lock/stability checks present.

Suggested post-flash verifier:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 1.6.23-dbwatch-lock-dev `
  -RequireDbMaintenance `
  -RequirePlayModeGuard `
  -RequireDbBootStabilityGuard `
  -RequireContextStartGuard `
  -AllowStagedFirmware `
  -CaptureFramebuffer
```

## 1.6.24 Exact-Catalog Candidate

This candidate keeps the 1.6.23 behavior and tightens the resume daemon's
catalog lookups. The path/root helpers now match exact TSV fields instead of
using substring `grep -F`, which avoids wrong-row resume behavior when one book
or file path is a prefix of another.

It also changes the release tooling default for shareable builds: the firmware
package no longer embeds a personal `r1_audiobook_catalog.tsv` seed catalog.
The on-device DB watcher remains responsible for rebuilding
`/usr/data/audiobooks/catalog.tsv` from the inserted SD card.

- Package:
  `work\audiobook-firmware-1.6.24-exact-catalog-dev\r1-audiobooks-1.6.24-exact-catalog-dev.upt`
- Firmware marker: `1.6.24-exact-catalog-dev`
- Visible label: `HiBy R1 Audiobook FW 1.6.24`
- MD5: `6a9cda191e4bb9772fa32a87185811cf`
- SHA256:
  `6374f07bc245eb68e9d04147ee5267260b94a191ffdafe603492424537af94de`
- Rootfs MD5: `2b9e7760a867ce1ca1784dc0c36a5f7d`
- Rootfs SHA256:
  `9362dccc4df0cbef2bc886cd7132611c709f1cce60ec1d089fcb567c08f823af`
- Local verification passed with:

```powershell
python tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-1.6.24-exact-catalog-dev `
  --upt-name r1-audiobooks-1.6.24-exact-catalog-dev.upt `
  --expected-version 1.6.24-exact-catalog-dev `
  --expected-label "HiBy R1 Audiobook FW 1.6.24" `
  --require-db-maintenance `
  --expect-batd-disabled `
  --expect-audiobook-launcher-icon
```

Suggested post-flash verifier:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_verify_installed_audiobook_release.ps1 `
  -ExpectedVersion 1.6.24-exact-catalog-dev `
  -RequireDbMaintenance `
  -RequirePlayModeGuard `
  -RequireDbBootStabilityGuard `
  -RequireContextStartGuard `
  -AllowStagedFirmware `
  -CaptureFramebuffer
```

## Safe Near-Term Work

1. Harden the on-device DB helper while preserving the current UI/resume path.
2. Add repeatable local fixture tests for DB helper behavior.
3. Build a candidate helper binary and test it through ADB as a runtime-only
   replacement before building or flashing a new firmware package.
4. Only after runtime tests pass, build a new firmware candidate with a new
   version marker.

## Implemented For 1.6.7 Candidate

- Multipart title-tap resume no longer relies on stock hardware Next/Previous
  fallback by default. Live testing showed that those buttons can move through
  tracks in a non-linear database order for at least one multipart audiobook.
  The daemon now prefers direct visible-row selection and stops safely if that
  cannot prove it reached the saved track.
- A narrow near-miss transport fallback is enabled after direct/visible row
  recovery gets close but not exact. It only runs when the persisted play-mode
  byte is already the audiobook sequential target, and it limits the number of
  Next/Previous steps so it cannot skip through a whole book.
- The native DB helper now recognizes `.iso` files during fallback scans,
  matching the broader HiBy format table used by the hiby-modding database
  updater.
- The native DB helper now fills empty `album_pic_path` fields from sidecar
  files in the same folder, using this priority:
  `cover.jpg`, `folder.jpg`, `front.jpg`, `albumart.jpg`, plus `.jpeg` and
  `.png` variants.
- The native DB helper now fills empty `lrc_path` when a same-stem `.lrc` file
  sits beside the audio file.
- The offline `add_audiobooks_to_media_db.py` experiment tool now mirrors those
  `.iso`, cover, and LRC behaviors.
- `tools/test_r1_db_maint_local_fixture.py` creates a disposable fake SD card
  and seed DB, runs a Windows build of the C helper, and verifies `.mp3`,
  `.m4b`, `.iso`, cover, LRC, catalog, and release-state invariants.
- The resume catalog now includes a backward-compatible `book_key` column based
  on author plus book title when available. New resume records include the same
  key so future builds can survive simple audiobook folder renames or rebuilds
  more gracefully while still reading existing root-path records.
- The catalog also carries optional `series` and `series_part` columns derived
  from Seanap/Plex-style folders. Books without a series folder keep those
  fields blank.
- The DB helper now writes `/usr/data/audiobooks/catalog-books.tsv`, a
  one-row-per-book sidecar with author, title, stable book key, optional series,
  optional series part, track count, and first media ID. This prepares the data
  layer for Author / Title / Series view experiments without changing the
  release UI yet.
- The resume daemon now uses a slower idle polling interval when playback is not
  on an audiobook path. Active audiobook resume still uses the tuned fast
  interval, but normal music playback does less background work.
- Multipart title-tap resume now has a pre-play direct-start path. When the
  daemon can identify the selected book from the stock track-list memory, it
  reads the saved resume record and taps the saved track row before playback
  starts. This should avoid briefly playing each earlier file; if the memory
  scan fails, the daemon falls back to the current first-track-plus-correction
  behavior.
- `tools/test_r1_resume_daemon_logic_wsl.ps1` runs shell-level logic tests for
  the resume daemon's direct-track geometry, disable switches, and saved-track
  title selection without launching the daemon loop.
- `tools/test_r1_db_maint_qemu_wsl.ps1` runs the same DB helper fixture through
  WSL and `qemu-mipsel-static`, executing the real MIPS helper binary instead
  of the Windows test executable.
- `tools/r1_adb_control.py macro edge-back` wraps the reliable left-edge swipe
  back gesture used during live testing.

## Audible-Inspired Feature Notes

Audible-style audiobook players commonly emphasize reliable resume, chapter
navigation, sleep timers, bookmarks/notes, narration speed, offline playback,
and simple driving/car controls. On the R1, reliable resume and chapter/file
navigation are the best immediate fit because they can ride the stock playback
engine. Bookmarks, notes, speed control, and a dedicated sleep-timer UI would
require deeper UI work or a new input convention, so they should wait until the
current resume and browsing path is production-stable.

## Test Path Before Any Firmware Build

1. Build a Windows test helper:

   ```powershell
   $zig = (Resolve-Path .deps\zig\zig-x86_64-windows-0.16.0\zig.exe).Path
   $sqlite = (Resolve-Path .deps\sqlite\sqlite-amalgamation-3530200).Path
   & $zig cc -target x86_64-windows-gnu -O2 -I $sqlite `
     -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION `
     -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_OMIT_DEPRECATED `
     tools\r1_audiobook_db_maint.c (Join-Path $sqlite 'sqlite3.c') `
     -o work\native-db-maint\r1_audiobook_db_maint_win_test.exe
   ```

2. Run the local fixture:

   ```powershell
   python tools\test_r1_db_maint_local_fixture.py `
     --helper work\native-db-maint\r1_audiobook_db_maint_win_test.exe
   ```

3. Build the R1 helper:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass `
     -File tools\build_r1_db_maint_helper.ps1 `
     -OutFile work\native-db-maint\r1_audiobook_db_maint_enhanced
   ```

4. Run the MIPS helper under WSL/QEMU:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass `
     -File tools\test_r1_db_maint_qemu_wsl.ps1 `
     -Helper work\native-db-maint\r1_audiobook_db_maint_enhanced
   ```

5. Run the resume daemon logic test:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass `
     -File tools\test_r1_resume_daemon_logic_wsl.ps1
   ```

6. Runtime-only device test, no flashing:
   copy the enhanced helper to `/usr/data/audiobooks/bin/`, run the stock
   Music -> Update Database scan, wait for the watcher, then verify that any
   test book with `cover.jpg` and matching `.lrc` gets those paths in the DB.

7. If runtime-only testing passes, build a new development candidate and
   run the full pre-flash verifier before staging.

## Next Device Tests For This Branch

1. Runtime-only install the new MIPS DB helper and run Music -> Update Database.
   Verify:
   - audiobooks with `Audiobook`, custom audiobook-like, or blank genre tags all
     appear in Audiobooks;
   - Music Albums/Genres still do not show audiobook albums/genres;
   - a title like `The Amber Book` or `The Hobbit` appears under `A`/`H`, not
     `T`, if such a fixture is available on the SD card.
2. Before treating `-DisableBatdLogger` as a real performance improvement, check
   whether `/usr/bin/batd` exists or a `batd` process runs on the device.
3. If `batd` exists, flash a dev candidate with `-DisableBatdLogger` and test
   normal music responsiveness for at least an hour with Bluetooth both off and
   on.
4. Confirm no `/mnt/sd_0/batlog.txt` growth during playback on the
   `-DisableBatdLogger` build.
5. Re-run a multipart audiobook resume check after the DB helper change to make
   sure normalized sorting does not disturb saved-file lookup.
6. For direct-resume research, arm the guarded play-open tracer with an audited
   cave such as `0x760708`, select a normal music row and an audiobook track row,
   then read `PLAY_OPEN_SCRATCH` to compare the stock function arguments:

   ```powershell
   python tools\r1_hiby_player_cave_audit.py `
     --binary work\audiobook-firmware-1.6.17-ui-dev\squashfs-root\usr\bin\hiby_player `
     --min-size 96

   python tools\adb_probe_music_row.py `
     --play-open-probe-addr 0x760708 `
     arm-play-open

   python tools\adb_probe_music_row.py read-play-open

   python tools\adb_probe_music_row.py `
     --play-open-probe-addr 0x760708 `
     restore-play-open
   ```

   Expected value: this is not a release feature yet. It should tell us whether
   the stock media-open path receives stable media/list objects that the resume
   daemon can reuse for a true direct start instead of visible row taps.
   Live testing confirmed that track-row taps hit `0x49e200`; row 1 captured
   `a2=0`, row 3 captured `a2=2`, and the list object identified itself as
   `vg_listview_songs_of_an_album_and_a_genre`. The next test should try a
   controlled direct invocation with the current list object and a chosen `a2`
   row index.
   That controlled test passed: forcing `a2=4` while tapping row 1 opened
   The Road part 5, and forcing `a2=20` while tapping row 1 in the 44-track
   The Remaining Aftermath list opened part 21. This confirms off-screen direct
   track selection is possible once the correct book's track list is open.

## Next Live Tests

When the R1 is available again, the most useful tests are:

1. Install the latest resume runtime without flashing:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass `
     -File tools\adb_install_audiobook_resume_runtime.ps1 `
     -CatalogSource work\audiobook-resume-catalog.tsv `
     -RestoreEnabled
   ```

2. From the Audiobooks title list, tap a multipart book with a saved position
   on a later file. Expected result: the track list may appear briefly, but the
   daemon should tap the saved track directly instead of audibly playing each
   earlier track. The daemon should also force Now Playing play mode to
   sequential list-loop mode (`/usr/data/user.ini` offset `0x250`, value `3`) so
   shuffle or single-track repeat cannot make multipart books advance out of
   order.

3. If the result is odd, collect a debug bundle before rebooting:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass `
     -File tools\adb_collect_audiobook_resume_debug.ps1
   ```

   The same collector is also useful after normal-music lag, freezes, or random
   reboots once ADB is available again.

4. For battery or normal-music responsiveness checks, run the read-only runtime
   monitor while music is playing:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass `
     -File tools\adb_monitor_r1_runtime.ps1 `
     -DurationMinutes 120 `
     -IntervalSeconds 60
   ```

5. Before building or flashing another candidate, run the consolidated local
   sanity wrapper:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass `
     -File tools\run_local_dev_sanity.ps1
   ```

6. Use the unified ADB controller to capture screens and drive common UI steps
   when manual tapping is inconvenient:

   ```powershell
   python tools\r1_adb_control.py macro open-audiobooks
   python tools\r1_adb_control.py row 1 --after-screenshot
   python tools\r1_adb_control.py key playpause
   ```

   See `docs\adb_control_tools.md` for the full preset list.

7. Repeat once with pre-play direct start disabled to compare fallback behavior:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass `
     -File tools\adb_install_audiobook_resume_runtime.ps1 `
     -CatalogSource work\audiobook-resume-catalog.tsv `
     -RestoreEnabled `
     -DisableBookTitleDirectTrackPreplay
   ```

## 1.6.25 Zero-Position Restore Candidate

`1.6.25-zero-restore-dev` keeps the 1.6.24 exact-catalog behavior and fixes a
title-list resume edge case found during live testing. When a book title is
selected from Audiobooks, the stock player can briefly report the correct file
at `00:00`. Older daemon logic waited for a positive playback position before
restoring, so a saved bookmark could be missed in that title-start window. The
daemon now allows restore at zero only while the guarded title-autostart path is
active; normal playback still ignores zero-position restores.

- Package:
  `work\audiobook-firmware-1.6.25-zero-restore-dev\r1-audiobooks-1.6.25-zero-restore-dev.upt`
- Firmware marker: `1.6.25-zero-restore-dev`
- Visible label: `HiBy R1 Audiobook FW 1.6.25`
- MD5: `d302b3ed5525b0600cfa5ca5c00a228c`
- SHA256:
  `cd73c114d30db7e7dd67c683d0f61ab44c4e9377830a288002d6d7c35bf78026`
- Rootfs MD5: `e778e6a84ff31fd0ee5c76008714877e`
- Rootfs SHA256:
  `76688e81a0f903561b1a76242385468315ff1ce72f16c1bfcd101072e430b1e3`

Live test result: forced `Ice Like Fire` to `00:00`, backed out to the
Audiobooks title list, selected the title again, and the patched daemon restored
directly back to `22:10`.

Verify with:

```powershell
python tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-1.6.25-zero-restore-dev `
  --upt-name r1-audiobooks-1.6.25-zero-restore-dev.upt `
  --expected-version 1.6.25-zero-restore-dev `
  --expected-label "HiBy R1 Audiobook FW 1.6.25" `
  --require-db-maintenance `
  --expect-batd-disabled `
  --expect-audiobook-launcher-icon
```

## 1.6.26 Context-Switch Candidate

`1.6.26-context-switch-dev` keeps the 1.6.25 zero-position restore fix and
adds a safer multipart title-switch path. The live failure case was selecting
`When You Are Engulfed In Flames` from the Audiobooks title list while another
audiobook context was active. The older 1.6.25 daemon started track 1 and later
overwrote the saved track-27 bookmark. The patched daemon lets context starts
use fresh list/catalog scans while disabling stale memscan roots, and defers
unresolved title-start saves so a deeper multipart bookmark is not replaced
before restore completes.

- Package:
  `work\audiobook-firmware-1.6.26-context-switch-dev\r1-audiobooks-1.6.26-context-switch-dev.upt`
- Firmware marker: `1.6.26-context-switch-dev`
- Visible label: `HiBy R1 Audiobook FW 1.6.26`
- MD5: `dc0724a74cb3ed271ccdb5b9c40595c5`
- SHA256:
  `a076703ebbd82e07df0f96b4a578daa10d6b1e11a82e0a4ed610081d5c6e9a99`
- Rootfs MD5: `99e91dd20fd63ccc16600cb3f5242500`
- Rootfs SHA256:
  `55141127ecda483925ffb1d7289bfe2071e910579a0de5ee73362373bac69925`

Live test result: restored the Engulfed record to track 27/30 at `281756 ms`,
selected the book title from Audiobooks, and the patched runtime direct-opened
track 27/30 then restored to `282s`. The record stayed on track 27.

Verify with:

```powershell
python tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-1.6.26-context-switch-dev `
  --upt-name r1-audiobooks-1.6.26-context-switch-dev.upt `
  --expected-version 1.6.26-context-switch-dev `
  --expected-label "HiBy R1 Audiobook FW 1.6.26" `
  --require-db-maintenance `
  --expect-batd-disabled `
  --expect-audiobook-launcher-icon
```

## 1.6.16.7 Stable Route Recovery Candidate

`1.6.16.7-stable-route-dev` returns to the known-good direct Audiobooks title
route after the native-hub `$s2` title-row test proved unsafe. It keeps the
resume runtime, DB watcher/catalog repair, audiobook launcher icon, USB DAC
mode, native DSD toggle, and Bluetooth SBC-XQ unlocks.

Validation on 2026-06-18:

- Package build passed.
- Offline verification passed with DB maintenance, audiobook icon, native DSD,
  SBC-XQ, and USB DAC checks enabled.
- The package was staged to `/usr/data/mnt/sd_0/r1.upt` for flashing.
- Hashes:
  - Package MD5 `438aefc7252894030f9923bb62895128`
  - Package SHA256
    `3db34eee7f9a96c19e76b1e29d18f8f79d5ca54e962f5848163a1886e31260e3`
  - `hiby_player` MD5 `09997a636c94112ff76c85a6d4a8d0ff`
  - `hiby_player` SHA256
    `f49ea55a48c1bdf1398a2a6672b1d596516650f7ebe77846ba7c33a5cfee329c`

Post-flash focus:

- Confirm the launcher Audiobooks tile returns to the stable title list.
- Confirm title-tap playback reaches Now Playing and resume still works.
- Confirm normal music remains responsive.

## 1.6.16.6 Native Hub S2 Test Candidate

`1.6.16.6-nativehub-s2-dev` is a deeper UI test build, not a release
candidate. It keeps the stable resume/DB/audio feature set, restores the native
Books hub as an Audiobooks hub, and changes the experimental native-hub title
row cave to pass `$s2` into the existing audiobook title-route opener. This is
intended to test whether the hub can provide a cleaner path toward Title /
Author / Series views and the single-Back behavior.

Live result: unsafe. The native hub opened and Back from the hub returned to the
launcher in one tap, but `Titles` was not usable. With the resume daemon
running, the old back-stack guard misidentified the path and returned to the
launcher; with the daemon stopped, tapping `Titles` rebooted/dropped ADB.
Future native hub work should target the Books list generator/select wrapper
rather than more register guesses into the media route helper.

Validation before flashing:

- Package build and offline verification passed with native hub title,
  launcher, and folder-row checks enabled.
- Hashes:
  - Package MD5 `3ad80f51167cc2655c59ccf6f18b3ffa`
  - Package SHA256
    `c9d1afde3c2ef520a2bcb06e7341d43408d2f75845cd8ff7b0eb8b9689e2f73d`
  - `hiby_player` MD5 `774ca68d3aa59b710adcf6394277a747`
  - `hiby_player` SHA256
    `46609913be8038566d3f317daaea5b0010cc796fa953c1781c1601f9a4c88aa9`
- Staged to the SD card as `/usr/data/mnt/sd_0/r1.upt` for device flashing.

Expected test focus after flashing:

- Audiobooks tile should open the native Audiobooks hub.
- `Titles` should open the audiobook title list rather than global Genres.
- `Authors` and `Folders` currently open the native explorer rooted at
  `/Audiobooks`; this is a stepping stone, not true metadata Authors/Series
  yet.
- Back from the hub should be checked for one-tap return to the main launcher.
- Title playback/resume should still pass the normal live smoke test.

## 1.6.16.5 Track-List Return Candidate

`1.6.16.5-tracklist-return-dev` keeps the 1.6.16.4 launcher-only guard and
adds one more launcher-entry cleanup: if the stock Audiobooks launcher restores
a remembered audiobook track list, the resume daemon backs out to the book title
list instead of leaving the user inside the previous book. This keeps launcher
entry passive while still preserving normal title-tap resume behavior.

Validation on 2026-06-18:

- Local daemon logic tests passed, including the launcher-visible track-list
  return case and the delayed/invisible no-return guard.
- Full local sanity passed, including Windows and QEMU DB-maintenance fixtures.
- Hot-patched installed-runtime verification passed under
  `work\installed-release-verification\20260618-150325`.
- A live title-list playback smoke test passed under
  `work\live-audiobook-smoke\20260618-150221`.
- The flashable package
  `work\audiobook-firmware-1.6.16.5-tracklist-return-dev\r1-audiobooks-1.6.16.5-tracklist-return-dev.upt`
  passed offline verification and was staged to the SD card as `r1.upt`.

## 1.6.16.4 Launcher-Only Guard Candidate

`1.6.16.4-launcher-guard-dev` keeps the current 1.6.16 feature set and
tightens the title-start daemon around main-launcher entry. A launcher-origin
Audiobooks marker now establishes recent Audiobooks context but never taps the
first visible track by itself, even if the stock UI restores a remembered track
list instead of the top title list. Real title selections still use the
`context`, `path`, or `catalog` paths and keep the resume behavior.

Validation on 2026-06-18:

- Local daemon logic tests passed with launcher-visible and launcher-delayed
  track-list regression cases.
- Full local sanity passed, including Windows and QEMU DB-maintenance fixtures.
- A hot-patched runtime on the device opened Audiobooks from the launcher to the
  title list without starting playback.
- A title-row tap still started playback through `reason=context` and restored
  `The Road` to its saved position.
- Installed-runtime verification passed under
  `work\installed-release-verification\20260618-144302`.
- A live title-row smoke test passed under
  `work\live-audiobook-smoke\20260618-144314`.
- The flashable package
  `work\audiobook-firmware-1.6.16.4-launcher-guard-dev\r1-audiobooks-1.6.16.4-launcher-guard-dev.upt`
  passed offline verification and was staged to the SD card as `r1.upt`.

The ADB control preset for the main Audiobooks tile was also moved to the icon
center (`360,390`) after the old label coordinate proved ambiguous when the UI
was sitting inside Music.

## 1.6.28 SD-Ready Candidate

`1.6.28-sd-ready-dev` keeps the 1.6.27 launcher guard and adds a DB watcher
retry for fresh flash/reboot timing. If the boot maintenance pass reports zero
audiobook tracks, the watcher polls briefly for actual audio files under
`/Audiobooks` and reruns the catalog builder once the SD card is ready. This
targets the observed post-flash state where the helper ran successfully but
cataloged zero audiobooks even though the card contents appeared moments later.

## 1.6.27 Launcher-Guard Candidate

`1.6.27-launcher-guard-dev` keeps the 1.6.26 context-switch fix and targets a
new launcher-entry edge case found during ADB validation. A pure Audiobooks tile
tap can increment the title marker while stale title-list pointers from the
previous book are still present. The old daemon trusted that as a launcher title
selection, could use a stale memscan root, and could fall back to tapping the
first visible row.

The patched daemon disables memscan roots for `launcher` markers and skips the
first-row fallback for launcher-only markers. Real title selections still keep
the context fallback path.

Runtime test result: after installing the patched daemon without flashing, a
short Audiobooks tile tap opened the title list and logged
`book-title touch-first skipped reason=launcher` instead of starting a random
book. Local sanity and installed-release verification both passed with the new
guard.

## Longer-Term Research

- Real `hiby_player` UI work: a native Audiobooks page would be cleaner than the
  current genre-route and daemon workaround, but it requires deeper binary/UI
  reverse engineering.
- Current native-view-row candidate:
  `1.6.17-audiobook` is the public-labeled release candidate. It keeps the stock
  Audiobooks hub and opens generated `Titles`, `Authors`, `Series`, and
  `Folders` filesystem views with a native sub-back label/callback helper.
  Installed validation showed friendly headers for Titles / Authors / Series,
  series-only filtering, working playback, and multipart resume from a
  generated title playlist. A RAM-only path probe changed Folders to
  `a:\Audiobooks\.\*`, which preserves the folder contents while displaying
  the friendly `Folders` header. The public-labeled package passed installed
  verification under `work\installed-release-verification\20260622-141615`; the
  remaining checks are longer real-world freeze-free navigation after release
  and more SD-card swap/update-database soak time.
- Static UI xref work now has a repeatable report at
  `work\static-xrefs\hiby_player-1.6.23-xrefs.md`, generated by
  `tools\r1_hiby_player_static_xrefs.py`. The useful next RAM-only targets are
  the stock media route records around `0x00787040` and listview handlers such
  as `vg_listview_artist -> 0x00490ec0`, `vg_listview_album -> 0x00490ee0`,
  `vg_listview_genre -> 0x00490f40`, and `vg_listview_book -> 0x00490fa0`.
  This is the next path for a cleaner Audiobooks submenu or Author/Series
  views.
- A first RAM-only route-table experiment changed the stock `genre` record to
  open `vg_listview_albums_of_a_genre` directly. It did open an `Audiobook` list
  without first showing the broader Genres parent, but the list contained
  duplicate multipart rows and Back still returned to global Genres. Keep it as
  a research helper, not a firmware change.
- Route-callback probes showed the current launcher enters the simple
  `0x004f01c0` route callback with the stock `0x007870a0` genre-chain record
  and `genre\Audiobook` argument. Switching that call to the chained callback
  opened global Genres, so the callback target alone is not a fix.
- Stock route-record pointer tests (`genre-simple`, `album`, `artist-simple`,
  `artist-chain`, `m3u`, and `format`) were all rejected in RAM. They either
  duplicated multipart books, opened part-level lists under stock Music
  headings, or fell into Playlists/Format. Keep these helpers for research, but
  do not promote them to firmware without a custom view/query patch.
- Callback disassembly now confirms why: the simple route callback only reads
  the route record's `view` and `child` pointers, while the chained callback
  reads `view`, `child`, and `next`. The current launcher calls the simple
  callback with the genre-chain record, so callback-field-only edits cannot fix
  Back behavior.
- `tools\adb_test_audiobook_route_table_matrix.py` is ready for the next clean
  live session. It tests one route-record field combination at a time in RAM,
  making it safer to isolate duplicate-row and Back-stack causes before any
  firmware patch is considered.
- Installed verification now pulls and validates the title, author, and series
  sidecar catalogs. The first run with the stricter verifier passed under
  `work\installed-release-verification\20260617-093819`.
- After reverting the RAM-only route-table experiment, installed verification
  passed again under `work\installed-release-verification\20260617-084639`.
- True direct resume: DMR/socket commands can seek within the currently loaded
  local file, but they have not been able to switch the stock local player to a
  different audiobook file. The `0x49e200` shared media-open path is now proven
  on-device for off-screen row indexes, and `1.6.18-directopen-dev` packages a
  one-shot native helper that can force the next stock row-open call to the
  saved zero-based track index. The older visible-row swipe/tap path remains as
  fallback if the helper refuses to arm or times out.
- Author/Title/Series audiobook subviews are tracked in
  `docs\audiobook_views_research.md`. Title is the current stable route;
  Author may be testable through stock artist routes but needs catalog isolation;
  Series now has catalog data when Seanap/Plex-style folders are present, but
  still needs custom UI/query work.
- Better metadata parsing inside the on-device helper: possible for simple ID3
  tags, but higher risk and more code size. The current safer path is still to
  let the stock scanner provide metadata when possible and use folder/file
  fallback otherwise.
- QEMU system emulation: useful someday, but not ready for release validation.
  Local host-native helper tests and live ADB runtime tests remain more useful
  right now.
- Playback-mode handling: live R1 snapshots mapped `/usr/data/user.ini` offset
  `0x250` (`592` decimal) as the Now Playing play-mode byte. The observed
  normal-R1 tap cycle is `1 -> 2 -> 0 -> 3 -> 1`; framebuffer crops showed
  `1=single-track loop`, `2=random/shuffle`, `0=random/shuffle`, and
  `3=list loop`. Development builds now target value `3` for active
  audiobooks, using the Now Playing framebuffer guard before tapping the mode
  button.
