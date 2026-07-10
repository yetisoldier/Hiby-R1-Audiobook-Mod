# Architecture Review — HiBy R1 Audiobook Firmware Mod

**Date:** 2026-07-10  
**Repository:** `hiby-r1-codex`  
**Current release:** `v1.6.1` / firmware marker `1.6.18-audiobook`  
**Base firmware:** stock HiBy R1 1.6 (normal R1, not R1 MIDI)  
**Reviewer:** Architect (Jarvis ecosystem)

---

## 1. Architecture Assessment

### 1.1 Strengths

**Conservative modification surface.** The mod follows a deliberate "smallest stable surface" philosophy. The stock audio engine, codec handling, Now Playing screen, Bluetooth path, EQ, and physical controls remain untouched. All audiobook behavior is layered around the stock playback path via binary patches in unused code caves, shell daemons, and native MIPS helpers. This minimizes regression risk and preserves stock device behavior for music playback.

**Multi-layer verification pipeline.** The project has a mature build-flash-verify cycle:
- `verify_r1_audiobook_build.py` performs pre-flash sanity checks on the package (hashes, helper binary strings, rootfs modes, file counts, symlink targets).
- `adb_verify_installed_audiobook_release.ps1` runs post-flash on-device verification (version markers, daemon/watcher process status, DB integrity, catalog invariants, framebuffer capture).
- `run_local_dev_sanity.ps1` runs PowerShell parsing, shell syntax, Python compile, resume daemon logic, and QEMU MIPS helper fixture tests before pushing.
- `adb_stage_verified_firmware.ps1` refuses known-bad package MD5s, verifies temp/final byte counts and hashes, and backs up existing staged files.
- Every release candidate has documented hashes, post-flash test results, and artifact directory references.

**Self-contained on-device operation.** After flashing, the firmware does not require a PC or ADB. The DB watcher seeds a missing database from an embedded seed DB, scans `/Music` and `/Audiobooks`, rebuilds catalogs, and mirrors repaired databases across all three DB locations (`/usr/data/`, `/data/`, SD-root). The resume daemon saves per-book state under `/usr/data/audiobooks/resume.d` and survives SD-card swaps. This is a significant architectural improvement over the earlier PC-assisted DB workflow.

**Iterative risk mitigation.** The development history shows a pattern of identifying failure modes and hardening against them specifically:
- Black-screen boot failures from rootfs mode drift → mode-preserving `mksquashfs -all-root -pf` with verified file modes.
- Stale DMR sockets → daemon closes inherited socket FDs on startup.
- Stock scanner overwriting audiobook genre normalization → `--needs-maintenance` content check, not just mtime.
- DB watcher stale locks → PID liveness check, stale-lock recovery.
- Boot-time SQLite I/O errors → `wait_for_stable_db` boot guard with 180-second timeout.
- Quick book-switch overwriting deep bookmarks → context-start guard, stale memscan root disabling, deferred saves.

**Clean separation of concerns.** The architecture has clear component boundaries:
- `hiby_player` binary patches handle launcher entry and hub navigation.
- `r1_audiobook_db_maint.c` handles all database operations (scan, normalize, catalog, view generation).
- `r1_audiobook_db_watch.sh` handles lifecycle (boot, scan detection, stale lock recovery, SD-card readiness).
- `r1_audiobook_resume_daemon.sh` handles playback state tracking and restore.
- `r1_audiobook_direct_open.c` handles one-shot track-index override for multipart resume.
- `r1_audiobook_memscan.c` handles live memory reads from `hiby_player` for position and duration.

**Practical fallback metadata.** The DB helper derives reasonable author, title, chapter, and ordering metadata from folder and filename structure when tags are missing. This is not a full tag parser, but it handles enough real-world cases to make the firmware usable without requiring users to have perfect metadata.

### 1.2 Weaknesses

**Shell-based resume daemon complexity.** The resume daemon (`r1_audiobook_resume_daemon.sh`) is a 2,500-line POSIX shell script with complex state management, memory polling, UI automation (framebuffer pixel checks, touchscreen event injection), and multiple fallback paths. Shell is the wrong language for this level of logic. It is difficult to test, difficult to debug, and prone to subtle bugs around quoting, variable expansion, and signal handling. The daemon's 80+ environment-variable tunables make configuration management complex.

**Fragile binary patching via hardcoded addresses.** All `hiby_player` patches target hardcoded memory addresses (`0x35DAEC`, `0x482030`, `0x360708`, etc.) that are specific to the exact stock 1.6 binary. Any stock firmware update would shift these addresses and break every patch. The verifier checks exact bytes before patching, which prevents silent corruption, but a stock update would require a full re-analysis of the new binary.

**Back-navigation stack contamination.** The Audiobooks entry point reuses the stock `genre\Audiobook` route, which means Back navigation passes through stock Genres pages. Multiple RAM-only route experiments (`genre-simple`, `album`, `artist-simple`, `artist-chain`, `m3u`, `format`, `direct-filter`) all failed to produce a cleaner Back stack. The native hub approach (`1.6.17-audiobook`) improved this, but the Back stack still shares stock view behavior. This is a fundamental limitation of reusing stock routing infrastructure.

**No audiobook search UI.** The current firmware supports only browsing by Titles, Authors, Series, and Folders. There is no text-based search for audiobooks. The stock search infrastructure queries `SEARCH_TABLE`, which deliberately excludes audiobook rows. Building a separate audiobook search would require either a custom UI view (deep `hiby_player` work) or a parallel search table and route.

**Resume daemon relies on UI automation.** Multipart track selection, seek-bar restore, and title-list navigation all depend on injecting touchscreen events and reading framebuffer pixels. This is inherently fragile — it depends on exact screen layout, timing, and stock UI state. The direct-open helper (`r1_audiobook_direct_open.c`) improved this by intercepting the stock media-open function, but the fallback paths still use UI automation.

**Single-device testing.** All testing has been on one normal HiBy R1. The README acknowledges this. Device-to-device inconsistency is a known risk (see Root Cause Analysis below). There is no multi-device test farm or CI for on-device behavior.

**Documentation sprawl.** The project has extensive documentation (`README.md` alone is ~30KB), but information is spread across many files with overlapping content. The `firmware_improvement_plan.md` is 700+ lines tracking development candidates through many versions, making it hard to find current status. The `README.md` mixes user-facing install instructions with developer-facing build commands, hash listings, and internal testing notes.

**PowerShell-centric tooling.** The build and verification tooling is overwhelmingly PowerShell-based, which ties development to Windows. While WSL is used for MIPS helper testing via QEMU, the primary build scripts (`build_r1_audiobook_firmware.ps1`, `adb_stage_verified_firmware.ps1`, `verify_r1_audiobook_build.py`, etc.) assume a Windows + PowerShell environment. This limits contributor onboarding from Linux/macOS developers.

---

## 2. Root Cause Analysis: Device-to-Device Inconsistency

The project documentation and development history reveal several root causes for behavior differences across devices or across SD-card/scan cycles on the same device. These are labeled RC1 through RC7.

### RC1: Stock scanner genre normalization overwrite

**Symptom:** Audiobooks disappear from the Audiobooks section after a subsequent Music → Update Database scan, even though the files are still on the SD card.

**Root cause:** The stock HiBy scanner rewrites media database rows during scans, restoring original genre tags from file metadata. The DB helper normalizes audiobook rows to have `Audiobook` genre (for the route to work), but the stock scanner can overwrite those normalized values back to the original genre tags without changing the database file size. Earlier watcher logic skipped same-size DB changes as mtime-only churn, so the denormalized rows were not detected.

**Fix applied:** The DB helper now has a `--needs-maintenance` check that scans actual `/Audiobooks` folder paths and compares them with existing audiobook rows in the media DB. The watcher runs a `content-repair-mtime` pass when DB contents need repair, even if the file size is unchanged. Folder location now wins over genre metadata.

**Status:** Fixed in `v1.5.2` / `1.6.16.2-audiobook`.

### RC2: SD-root database mirror not normalized

**Symptom:** After Music → Update Database on a fresh SD card, Audiobooks shows "No music found" even though files exist under `/Audiobooks`.

**Root cause:** The stock UI can read the SD-root media database copy at `/usr/data/mnt/sd_0/usrlocal_media.db` rather than the internal copy at `/usr/data/usrlocal_media.db`. Earlier versions of the watcher only normalized the internal and `/data/` copies, leaving the SD-root copy with un-normalized or missing audiobook rows.

**Fix applied:** The DB watcher now runs the maintainer against all three database locations: `/usr/data/usrlocal_media.db`, `/data/usrlocal_media.db`, and `/usr/data/mnt/sd_0/usrlocal_media.db`. Repaired databases are mirrored to all active locations.

**Status:** Fixed in `v1.5.1` / `1.6.16.1-audiobook`.

### RC3: Boot-time SQLite disk I/O race

**Symptom:** After flashing new firmware or inserting a new SD card, the first boot maintenance pass fails with SQLite disk I/O errors, leaving the audiobook catalog empty or partial.

**Root cause:** The DB watcher's boot maintenance pass races with the stock Music → Update Database scanner. If the stock scanner is still rewriting `usrlocal_media.db` when the helper tries to open it, SQLite reports disk I/O errors because the file is being actively rewritten. The original watcher ran the helper immediately after boot with no stability check.

**Fix applied:** The watcher now exports a `wait_for_stable_db boot` guard: it polls the database file signature (size + mtime) for 180 seconds with 3-second intervals, waiting for the file to stabilize before running the helper. The verifier requires these settings.

**Status:** Fixed in `1.6.20-dbboot-stable-dev` and carried forward.

### RC4: Stale DB watcher lock PID reuse

**Symptom:** After a reboot or crash, the DB watcher does not start. The device boots with no audiobook catalog maintenance.

**Root cause:** The watcher used a lock file with a saved PID. After a reboot, the saved PID could be reused by an unrelated process (e.g., `hiby_player`). The watcher's lock check only verified that *some* process with that PID was running, not that it was actually the watcher. The watcher would exit, believing another watcher was already active.

**Fix applied:** The lock check now verifies that the process owning the PID is actually a watcher process (checking the command line), not just any process. If a stale lock points at a live non-watcher PID, the watcher logs `stale-lock-live-pid-not-watcher`, recovers the lock, and starts. The TERM trap also allows clean `start-stop-daemon -K` stops.

**Status:** Fixed in `1.6.23-dbwatch-lock-dev` and carried forward.

### RC5: Quick book-switch bookmark overwrite

**Symptom:** Selecting a different audiobook from the title list while another audiobook is playing causes the daemon to overwrite the previously playing book's saved bookmark with the new book's early position, losing the deep resume point.

**Root cause:** When a title tap is matched only through recent audiobook context or relaxed matching, the daemon could reuse stale memory-scan roots from the previously playing book. It would then attempt a pre-play direct-start using the wrong book's root, fail to find the new book's saved track, and fall back to first-track start. The new track's early position would then be saved, overwriting the previous book's deeper bookmark.

**Fix applied:** The daemon now disables memscan roots for context-only starts, skips pre-play direct-start memory scans when the book identity is not confirmed, and defers saving unresolved title-start positions so a deeper bookmark is not replaced before restore completes. The `1.6.22-fast-context-dev` candidate further improved this by skipping pre-play direct-start memory scans entirely for context-only matches, falling back to first-track start plus existing track-restore paths.

**Status:** Fixed in `1.6.26-context-switch-dev` and carried forward.

### RC6: Zero-position restore miss

**Symptom:** Selecting a book title from the Audiobooks list fails to restore to the saved bookmark position when the stock player briefly reports the correct file at `00:00`.

**Root cause:** The daemon's restore logic waited for a positive playback position before attempting a seek. If the stock player opened the correct file but reported `00:00` during the title-start window, the daemon would skip the restore, assuming no saved position was relevant.

**Fix applied:** The daemon now allows restore at zero position while the guarded title-autostart path is active. Normal playback still ignores zero-position restores to avoid false positives.

**Status:** Fixed in `1.6.25-zero-restore-dev` and carried forward.

### RC7: Launcher-only marker triggering first-row tap

**Symptom:** Tapping the Audiobooks launcher tile (without selecting a specific book) causes the daemon to start playing a random audiobook, because the daemon interprets the launcher marker as a title selection.

**Root cause:** A pure Audiobooks tile tap increments the title marker, but stale title-list pointers from the previously open book can still be present. The daemon trusted this as a launcher title selection, used a stale memscan root, and fell back to tapping the first visible row — starting whatever book was previously open.

**Fix applied:** The daemon now disables memscan roots for `launcher` markers and skips the first-row fallback for launcher-only markers. Real title selections still use the context fallback path with full validation.

**Status:** Fixed in `1.6.27-launcher-guard-dev` and carried forward.

---

## 3. Optimization Recommendations

Recommendations are prioritized by impact and risk. Effort estimates are approximate.

### P1: Rewrite resume daemon in C (High impact, Medium risk, High effort ~2-3 weeks)

The 2,500-line shell resume daemon is the project's biggest technical debt. It handles position tracking, multipart track selection, UI automation, seek-bar restore, bookmark management, and play-mode guarding — all in POSIX shell with 80+ environment variables. A C implementation would:

- Eliminate shell quoting/expansion bugs.
- Reduce CPU overhead (no per-poll subshell spawning).
- Enable proper unit testing with deterministic fixtures.
- Allow structured logging instead of ad-hoc `printf` to a log file.
- Reduce memory footprint (shell + helper subprocesses vs. single binary).
- Make the UI automation paths (framebuffer pixel checks, touch event injection) more reliable through direct `/dev/fb0` and `/dev/input/event*` access.

**Risk:** A C rewrite must be tested thoroughly on-device before replacing the working shell daemon. The current shell daemon, while complex, is battle-tested. Recommend building the C version as a parallel binary, running both in shadow mode (C logs what it *would* do, shell still acts), then switching over after extended live testing.

### P2: Cross-platform build tooling (Medium impact, Low risk, Medium effort ~1 week)

The PowerShell-centric build tooling limits contributor onboarding. Recommendation:

- Rewrite `build_r1_audiobook_firmware.ps1` as a Python script with `argparse`. The existing Python tools (`verify_r1_audiobook_build.py`, `patch_hiby_player.py`, `add_audiobooks_to_media_db.py`) already prove Python is capable.
- Replace `adb_stage_verified_firmware.ps1` with a Python script using `subprocess` for ADB calls.
- Keep the PowerShell scripts as thin wrappers for Windows users who prefer them, but have Python be the canonical path.
- This also enables CI/CD in GitHub Actions (Linux runner) for pre-flash verification.

### P3: Overlay-based build system (Medium impact, Low risk, Medium effort ~1 week)

Currently the build script re-packs the full rootfs from a known extracted stock base. The `Meowby-R1` project from hiby-modding uses an overlay-only approach: store only the changed files and a manifest, then apply the overlay to a stock rootfs at build time. Benefits:

- Smaller diff to review in version control.
- Clearer audit trail of what changed from stock.
- Easier collaboration with hiby-modding (they can consume the overlay).
- Faster builds (no full rootfs repack when overlay is small).

### P4: Article/punctuation normalization in native helper (Low impact, Low risk, Low effort ~2 days)

The `hiby-mods` project documents a fuller collation behavior for the `character` and `pinyin_charater` sidebar fields. The native C helper currently strips leading punctuation and spaces but does not strip leading articles (`the`, `der`, `die`, `das`, `les`, `il`, `lo`, `la`, `le`, `el`). The offline Python DB tool already does this. Aligning the C helper would improve the alphabetical side-rail jump list for audiobook titles like "The Amber Book" (should appear under `A`, not `T`).

**Risk:** Very low — this only affects sort/display fields, not playback or routing. Test with the existing local fixture.

### P5: Album art size guidance and enforcement (Low impact, Low risk, Low effort ~1 day)

The hiby-modding community recommends 360×360 JPEG cover art because oversized covers cost CPU/RAM on X1600-class players. The helper already picks sidecar cover files but does not validate or downsize them. Recommendation:

- Add a README section with cover art size guidance for audiobook users.
- Optionally, add a `--check-cover-size` flag to the DB helper that warns (not errors) when cover files exceed 500KB or 1000×1000 pixels.
- Do not auto-resize covers on-device (too risky for a MIPS helper).

### P6: Consolidate documentation (Low impact, Low risk, Low effort ~1 day)

The project has significant documentation overlap. `README.md` (30KB+), `investigation.md`, `firmware_improvement_plan.md` (700+ lines), `release_recovery_notes.md`, `build_flash_verify_runbook.md`, and `CHANGELOG.md` all contain overlapping information at different levels of currency. Recommendation:

- Split `README.md` into a concise user-facing `README.md` (install, usage, known quirks) and a `DEVELOPMENT.md` for build/verify/flash instructions.
- Move the `firmware_improvement_plan.md` candidate history into `docs/dev-history/` as archived notes, keeping only a forward-looking `ROADMAP.md`.
- Consolidate hash listings into a single `RELEASES.md` with a table format.

### P7: Automated regression test suite for on-device behavior (High impact, Medium risk, High effort ~2 weeks)

The project has strong local fixture tests (DB helper, resume daemon logic) but on-device testing is still manual. Recommendation:

- Extend `adb_live_audiobook_smoke.ps1` (or a Python replacement) into a full regression suite that:
  - Opens Audiobooks from the launcher.
  - Selects a known book, verifies playback starts.
  - Pauses, backs out, re-selects, verifies resume position.
  - Switches books, verifies no bookmark corruption.
  - Plays music, verifies daemon quiet state.
  - Runs DB maintenance, verifies catalog invariants.
- Run this suite after every flash before declaring a release ready.
- This is the path to multi-device confidence without a large test farm.

### P8: Route-table research for clean Back navigation (Medium impact, High risk, Medium effort ~1-2 weeks)

The Back-navigation stack contamination is the most visible UX wart. The current `genre\Audiobook` route passes through stock Genres pages. Multiple RAM-only experiments failed to find a better route. The native hub approach improved the entry point but the Back stack still shares stock behavior. Recommendation:

- Continue `r1_audiobook_ui_route_lab.py` research with the static xref data (`hiby_player-1.6.23-xrefs.md`).
- Target the stock media route records around `0x00787040` and listview handlers (`vg_listview_artist`, `vg_listview_album`, `vg_listview_genre`, `vg_listview_book`).
- Test candidates in RAM-only mode (no flashing) using `adb_test_audiobook_route_table_matrix.py`.
- Only promote to firmware if a route produces a clean one-Back-to-launcher behavior without duplicate rows or wrong-screen fallbacks.
- This is high-risk because it touches core routing infrastructure in `hiby_player`.

---

## 4. Risk Assessment

### Safe to Change (Low Risk)

| Component | Why Safe | Recommended Changes |
|---|---|---|
| DB helper (`r1_audiobook_db_maint.c`) | Runs as a separate process, does not touch `hiby_player` or stock UI. Changes are testable via local fixture and QEMU. | Add article normalization, cover-size checks, better tag parsing, more format codes. |
| DB watcher (`r1_audiobook_db_watch.sh`) | Separate process, does not touch stock playback. Boot/stability guards are additive. | Tighten lock logic, add more stale-PID recovery, improve logging. |
| Refresh helper (`r1_audiobook_refresh.sh`) | Simple trigger script, only writes a request marker. | Any change is low-risk. |
| Resource text patches | Only change display strings in resource files, not code paths. | Add translations for other languages. |
| Launcher icon generation | Only replaces image resources. | New icon styles are safe. |
| Seed DB (`r1_usrlocal_media_seed.db`) | Only used when the stock DB is missing or empty. | Schema additions for future features are safe if backward-compatible. |
| Catalog sidecar files | Data-only files, not consumed by stock UI. | Add new columns for future view experiments. |
| Verification tools | Read-only or pre-flash checks. | Adding new checks is always safe. |
| Build script options | Additive flags behind opt-in switches. | New `-Include*` or `-Disable*` flags are low-risk if off by default. |
| `firmware_improvement_plan.md` documentation | Not code. | Reorganize, archive, consolidate. |

### Moderate Risk (Test Before Merge)

| Component | Why Moderate | Mitigation |
|---|---|---|
| Resume daemon tunables (80+ env vars) | Changing defaults affects on-device behavior, but changes are testable via runtime-only install before flashing. | Test every change via `adb_install_audiobook_resume_runtime.ps1` before building firmware. |
| Direct-open helper (`r1_audiobook_direct_open.c`) | Patches live `hiby_player` memory at runtime. Incorrect addresses could crash the player. | The helper validates audited probe caves and writable scratch ranges before patching. Keep the timeout-and-restore behavior. |
| Memscan helper (`r1_audiobook_memscan.c`) | Reads live process memory. Incorrect offsets return garbage. | Offset changes require live testing. Keep the helper read-only. |
| Init scripts (`S91audiobook_resume.sh`, `S92audiobook_db_maint.sh`) | Run at boot. Parse errors can prevent daemons from starting. | Always test with `sh -n` and on-device after flash. Keep LF-only line endings (BusyBox compatibility). |
| Firmware version markers | Wrong markers confuse users about what's installed. | Always verify via `adb_verify_installed_audiobook_release.ps1`. |
| SD-card view generation | Writes playlist files under `/Audiobooks/_views/`. Incorrect paths could confuse folder browsing. | Test with the existing fixture suite. |

### High Risk (Careful Review Required)

| Component | Why High Risk | Mitigation |
|---|---|---|
| `hiby_player` binary patches | Hardcoded addresses in a specific stock binary. Wrong bytes can cause black-screen boot, reboot loops, or UI corruption. Any stock firmware update shifts all addresses. | Always verify exact expected bytes before patching (already done). Never apply patches from other HiBy models. Keep the known-bad MD5 blocklist. Document the address map in `audiobook_firmware_architecture.md`. |
| Rootfs repacking | Mode/ownership drift can cause black-screen boot (experienced and documented). | Always use `mksquashfs -all-root -pf` with mode-preserving pseudo files. Verify all 5488 stock paths and 482 symlinks. Never use a repacker that doesn't preserve modes. |
| Stock route-table modifications | Touches core routing infrastructure. RAM-only experiments showed most route candidates produce duplicate rows, wrong screens, or reboots. | Keep as RAM-only research. Never promote to firmware without extensive live testing. Document rejected candidates. |
| Native hub `$s2` register experiments | The `1.6.16.6-nativehub-s2-dev` candidate proved unsafe — tapping `Titles` rebooted the R1. | Do not use `$s2` register passing. Target the Books list generator/select wrapper instead for future native hub work. |
| Boot ADB in public builds | Persistent ADB is a security exposure on a shared device. | Keep boot ADB opt-in (`-EnableBootAdb` for dev builds only). Public builds must not include it. |
| Audio unlocks (Native DSD, SBC XQ, USB DAC) | These modify stock audio feature flags. They are lightly tested compared to audiobook features. | Keep them separable from audiobook builds via independent build switches. Test independently. |
| Removing `batd` logger | Modifies `hiby_player.sh` startup. If `batd` exists on some firmware variants, removing it could affect battery monitoring. | Keep as opt-in (`-DisableBatdLogger`). Document that the extracted stock rootfs here does not include `/usr/bin/batd`. |

---

## 5. Migration Considerations: Codex → Jarvis Ecosystem

### 5.1 Current State

The project was developed in a "Codex" workflow — a Windows + PowerShell + Python + WSL/QEMU development environment, with manual ADB testing against a single physical R1 device. The repository is on GitHub at `yetisoldier/Hiby-R1-Audiobook-Mod` with a clean branch structure (`main` plus feature branches under `codex/*`).

Migration to the Jarvis ecosystem (Bob/Forge/Karen) would involve transferring development ownership and tooling to a different agent/team workflow. The following considerations apply.

### 5.2 What Transfers Cleanly

**Source code and build inputs.** All source files (`r1_audiobook_db_maint.c`, `r1_audiobook_resume_daemon.sh`, `r1_audiobook_direct_open.c`, `r1_audiobook_memscan.c`, `r1_audiobook_db_watch.sh`, `r1_audiobook_refresh.sh`, `patch_hiby_player.py`, etc.) are portable. They have no dependency on the Codex workflow itself.

**Documentation.** All docs are Markdown and transfer directly. The `docs/` directory is well-organized.

**Git history.** The repository has clean, descriptive commits. Branch names are clear (`codex/r1-v1.4-stability-resume`, `codex/r1-hiby-modding-integration`, etc.). The `codex/*` prefix should be renamed to the Jarvis convention (e.g., `jarvis/*` or `forge/*`) going forward.

**Verification tooling.** Python tools (`verify_r1_audiobook_build.py`, `patch_hiby_player.py`, `test_r1_db_maint_local_fixture.py`, `check_audiobook_release_state.py`, etc.) are portable. The QEMU/WSL test path works on any Linux system with `qemu-mipsel-static`.

**Local references.** The `references/` directory (hiby-modding mirrors) is Git-ignored research material. It should not be committed but can be re-created from the hiby-modding org on GitHub.

### 5.3 What Requires Adaptation

**PowerShell build scripts → Python.** The primary build and ADB scripts are PowerShell (`.ps1`). For the Jarvis ecosystem, these should be rewritten in Python (see P2 above). The scripts to convert:

- `build_r1_audiobook_firmware.ps1` → `build_firmware.py`
- `adb_stage_verified_firmware.ps1` → `stage_firmware.py`
- `adb_verify_installed_audiobook_release.ps1` → `verify_installed.py`
- `adb_install_audiobook_resume_runtime.ps1` → `install_resume_runtime.py`
- `adb_build_release_audiobook_db.ps1` → `build_release_db.py`
- `adb_install_release_audiobook_db.ps1` → `install_release_db.py`
- `run_local_dev_sanity.ps1` → `run_dev_sanity.py`
- `extract_r1_firmware.ps1` → `extract_firmware.py`
- `build_r1_db_maint_helper.ps1` → `build_db_helper.py`
- `build_r1_memscan_helper.ps1` → `build_memscan.py`
- `build_r1_direct_open_helper.ps1` → `build_direct_open.py`

The conversion is mechanical: each PowerShell script maps to a Python script with `argparse` arguments replacing PowerShell parameters, and `subprocess.run` replacing PowerShell command invocations.

**ADB path resolution.** PowerShell scripts resolve ADB from `.tools\platform-tools\adb.exe` or the system PATH. On Linux/macOS, ADB is typically installed system-wide. The Python replacements should use `shutil.which('adb')` with a fallback to a repo-local path.

**WSL/QEMU → native Linux.** The current workflow uses WSL Ubuntu 24.04 for MIPS helper testing via `qemu-mipsel-static`. On a native Linux system (which Jarvis/Forge likely runs on), this is simpler — `qemu-mipsel-static` can be installed directly via `apt install qemu-user-static`. The `test_r1_db_maint_qemu_wsl.ps1` wrapper becomes a simple shell script.

**SquashFS tools.** The build uses portable SquashFS 4.3 tools extracted under `.deps/squashfs/tools/squashfs-tools`. On Linux, `squashfs-tools` can be installed via `apt install squashfs-tools`. Verify that the LZO compression and block size 131072 match.

**MIPS toolchain.** The project downloads `binutils-mipsel-linux-gnu` into `.deps`. On Linux, install `binutils-mipsel-linux-gnu` via `apt`. The `mips_objdump_wsl.ps1` wrapper becomes a direct call to `mipsel-linux-gnu-objdump`.

**Zig compiler.** The DB helper is built with Zig (`zig cc -target mipsel-linux-musl`). Install Zig on the build system or use a version-pinned download matching the current `.deps/zig/zig-x86_64-linux-0.16.0/` path.

### 5.4 What Does Not Transfer

**Physical device access.** The single test R1 is physically connected to the development machine. The Jarvis ecosystem agent would need either physical access to the device or a remote ADB connection (USB-over-IP or `adb connect` over TCP/IP). This is the hardest migration constraint — all on-device testing requires the physical R1.

**Windows-specific binaries.** The Windows test helper (`r1_audiobook_db_maint_win_test.exe`) built with `zig cc -target x86_64-windows-gnu` is for Windows-only local testing. On Linux, build a native test helper with `zig cc -target x86_64-linux-gnu` or just use the MIPS helper under QEMU directly.

**Git Credential Manager token workaround.** The `publish_github_release.ps1` script uses Windows-specific Git Credential Manager for GitHub authentication. Replace with `gh` CLI or a GitHub token environment variable on Linux.

### 5.5 Migration Steps

1. **Clone and audit.** Clone the repo into the Jarvis workspace. Run `run_local_dev_sanity` equivalent (after Python conversion) to verify all tests pass.

2. **Convert build scripts to Python.** Start with `build_r1_audiobook_firmware.ps1` and `verify_r1_audiobook_build.py` (already Python). Then convert the ADB staging and verification scripts. Keep PowerShell wrappers as thin shims for backward compatibility.

3. **Set up Linux build environment.** Install `squashfs-tools`, `qemu-user-static`, `binutils-mipsel-linux-gnu`, `zig`, Python 3, and `adb`. Verify the build produces a byte-identical `.upt` package.

4. **Establish device access.** Connect the test R1 to the build machine via USB or set up remote ADB. Verify `adb devices` sees the R1.

5. **Run the full verification cycle.** Build, stage, flash, and verify a release candidate. Compare all hashes against the published release. If they match, the migration is complete.

6. **Rename branch convention.** New feature branches should use the Jarvis convention (e.g., `jarvis/feature-name` or `forge/feature-name`). Keep `main` as the release branch.

7. **Set up CI.** GitHub Actions on a Linux runner can run `verify_r1_audiobook_build.py`, Python compile checks, and QEMU helper fixture tests on every push. On-device tests remain manual.

8. **Update attribution.** The README's "Attribution And Sources" section should note the migration from Codex to Jarvis ecosystem development. The project author and license remain unchanged.

### 5.6 Risks of Migration

- **Build reproducibility.** The current Windows build produces specific hashes. A Linux build must produce identical output for the release to be trust-compatible. The SquashFS repack and Zig cross-compilation should be deterministic, but this must be verified.
- **Loss of WSL test path.** The WSL-based QEMU test path is well-understood. A native Linux path is simpler but the test scripts need rewriting.
- **Tool version drift.** The `.deps/` directory pins specific versions of Zig, SQLite, SquashFS tools, and MIPS binutils. Linux package versions may differ. Pin versions in a `requirements.txt` or `Dockerfile` for reproducibility.
- **Single device.** The physical R1 remains a single point of failure for on-device testing. If the device is damaged or lost during migration, development stalls until a replacement is obtained.

---

## Appendix A: Component Inventory

| File | Lines | Language | Role |
|---|---|---|---|
| `r1_audiobook_resume_daemon.sh` | 2,500 | Shell | Resume state tracking, UI automation, bookmark management |
| `r1_audiobook_db_maint.c` | 2,344 | C | On-device DB scanner, catalog builder, view generator |
| `r1_audiobook_db_watch.sh` | 744 | Shell | DB lifecycle management, boot guard, stale lock recovery |
| `patch_hiby_player.py` | 928 | Python | Binary patcher for `hiby_player` (launcher, hub, audio unlocks) |
| `r1_audiobook_direct_open.c` | 437 | C | One-shot track-index override for multipart resume |
| `r1_audiobook_memscan.c` | 215 | C | Live memory reader for playback position/duration |
| `r1_audiobook_refresh.sh` | 103 | Shell | Refresh trigger script |
| `r1_adb_control.py` | — | Python | Unified ADB control console (screenshots, taps, macros) |
| `verify_r1_audiobook_build.py` | — | Python | Pre-flash package verifier |
| `build_r1_audiobook_firmware.ps1` | — | PowerShell | Firmware build wrapper |
| Total tools (all files) | ~26,160 | Mixed | Full tooling inventory |

## Appendix B: Patch Address Map

| Address | Purpose |
|---|---|
| `0x35DAEC` | Native hub launcher callback cave |
| `0x482030` | Launcher callback table entry → native hub |
| `0x35DF40` | Private resource key for hub title row |
| `0x360708–0x360798` | Row caves: Titles, Authors, Series, Folders |
| `0x360808–0x360988` | UTF-16LE route/path strings for views |
| `0x360A08` | Refresh Library row cave |
| `0x360A80` | Refresh helper command string |
| `0x38D278–0x38D288` | Hub row jump-table entries |
| `0x360D50` | Explorer cleanup/open helper |
| `0x49E200` | Shared media-open function (direct-open target) |
| `0x760708` | Audited probe cave for play-open tracing |
| `0x8E4000` | Title-list marker in RAM |
| `0x8E4400` | Writable scratch range for direct-open |
| `0x8E4600` | Music-row scratch (moved from live `0x8b1f00` range) |
| `0x9115148` (decimal) | Player playback position field in `hiby_player` memory |
| `0x9115252` (decimal) | Player track duration field in `hiby_player` memory |

## Appendix C: Known Bad Packages

| Package | MD5 | Failure Mode |
|---|---|---|
| `full-dev-fixed.BAD-black-screen-20260609` | `3bed523d5843522186164029139db7b1` | Black screen: rootfs mode/ownership drift (`/bin/busybox` lost setuid) |
| `full-dev.BAD-nonexec-hiby-player` | — | Black screen: `/usr/bin/hiby_player` repacked as `-rw-r--r--` |
| `1.6.16.6-nativehub-s2-dev` | `3ad80f51167cc2655c59ccf6f18b3ffa` | Unsafe: tapping `Titles` reboots the R1 (register `$s2` experiment) |

---

*End of architecture review.*