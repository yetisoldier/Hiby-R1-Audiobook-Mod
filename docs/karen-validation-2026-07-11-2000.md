# Karen Validation Report — 2026-07-11 20:00

## Comprehensive Code Validation of HiBy R1 Audiobook Project

**Validator:** Karen (QA/Validation)  
**Scope:** Full codebase review of `/home/yetisoldier/projects/hiby-r1-codex`  
**Purpose:** Final validation before rebuild and reflash, with emphasis on the critical launcher routing failure

---

## Executive Summary

The project has a well-structured standalone app (`r1_audiobook_app`) with a clean UI, working decoder pipeline, and solid database layer. However, **the #1 blocker is architectural**: no mechanism in the build pipeline actually makes `hiby_player` launch `r1_audiobook_launch.sh` when the Audiobooks tile is tapped. The binary patch routes to the stock hub, and the `command` field in `launcher.ini` is ignored by `hiby_player`.

**Overall verdict: FAIL — do not flash until launcher routing is fixed.**

---

## A. Launcher Routing — **FAIL** (CRITICAL BLOCKER)

### A.1 Binary Patch Analysis (`tools/patch_hiby_player.py`)

**Confirmed:** `AUDIOBOOK_NATIVE_HUB_LAUNCHER_PATCHES` routes the Audiobooks tile callback to a code cave at `0x35DAEC` that calls the **stock Books hub opener at `0x00540F20`**, not our standalone app.

The code cave disassembly shows:
```
0x35db48: jal 0x00540f20    ← calls stock Books hub opener
```

This means tapping the Audiobooks tile opens the native HiBy Books/audiobook hub view with relabeled rows (Titles/Authors/Series/Folders), NOT our `r1_audiobook_app` binary. The shell wrapper (`r1_audiobook_launch.sh`) and app binary are never invoked.

**Root cause:** The patch architecture evolved through several approaches (genre routing, native hub, view rows) but none of them implement the actual handoff to `/usr/bin/r1_audiobook_launch.sh`. All approaches modify `hiby_player` to call internal stock functions rather than executing an external process.

### A.2 Resource Patch Analysis (`tools/patch_r1_resource_text.py`)

**Confirmed:** The `LAUNCHER_INSERTIONS` dict inserts `<command>/usr/bin/r1_audiobook_launch.sh</command>` into `launcher.ini`. However, `hiby_player` does not read or execute the `command` field from `launcher.ini`. This field is dead metadata — it has no functional effect.

The resource patch correctly relabels "ebook" → "Audiobooks" and changes row labels, but the `command` insertion is non-functional.

### A.3 Recommended Fix Approaches (Priority Order)

#### Option 1: Replace `hiby_player.sh` wrapper (RECOMMENDED — lowest risk)

The stock `hiby_player.sh` is a shell script that launches `/usr/bin/hiby_player`. Replace it with a wrapper that:
1. Launches `hiby_player` in the background
2. Monitors the framebuffer or input events for Audiobooks tile taps
3. Kills `hiby_player` and launches `r1_audiobook_app` when detected

**Pros:** No binary patching needed for launcher routing. Clean separation.
**Cons:** Requires framebuffer polling or input event interception. Adds latency.

#### Option 2: Binary patch with `system()` call

Patch the Audiobooks tile callback in `hiby_player` to jump to a code cave that calls `system("/usr/bin/r1_audiobook_launch.sh &")` instead of the stock hub opener.

The code cave would need to:
1. Save registers (already done in current cave prologue)
2. Load the command string address into `$a0`
3. Call `system()` (PLT entry at `0x0083AD80` — already identified in the patch file)
4. Restore registers and return

**Pros:** Direct, fast, no wrapper overhead. The `system()` PLT address is already known.
**Cons:** Requires careful MIPS assembly. `system()` blocks until the child exits, so the app must daemonize or the player will freeze. Use `system("/usr/bin/r1_audiobook_launch.sh &")` with trailing `&`.

**Implementation sketch:**
```python
# In patch_hiby_player.py, replace AUDIOBOOK_NATIVE_HUB_LAUNCHER_CODE with:
# Code cave that calls system("/usr/bin/r1_audiobook_launch.sh &")
LAUNCHER_CMD = b"/usr/bin/r1_audiobook_launch.sh &\x00"
LAUNCHER_CMD_OFFSET = 0x35DF40  # reuse this space for the command string

AUDIOBOOK_SYSTEM_LAUNCHER_CODE = pack_words(
    ins_addiu(29, 29, -0x20),       # save stack
    ins_sw(31, 29, 0x1C),           # save ra
    ins_sw(16, 29, 0x18),           # save s0
    # Load command string address
    *load_addr_words(4, text_addr(LAUNCHER_CMD_OFFSET)),
    # Call system()
    ins_jal(AUDIOBOOK_NATIVE_HUB_VIEW_SYSTEM_PLT_ADDR),  # 0x0083AD80
    0,  # nop (delay slot)
    # Restore and return
    ins_lw(31, 29, 0x1C),
    ins_lw(16, 29, 0x18),
    ins_addiu(2, 0, 1),             # return 1 (success)
    ins_jr(31),
    ins_addiu(29, 29, 0x20),
)
```

#### Option 3: Use `r1_audiobook_refresh.sh` mechanism

The refresh script already uses `system()` via the binary patch's `AUDIOBOOK_NATIVE_HUB_VIEW_REFRESH_CMD`. A similar pattern could be used for the launcher itself, but this would require a different code cave that runs before the stock hub opens.

#### Option 4: LD_PRELOAD interception

Create a shared library that intercepts a key function call in `hiby_player` and redirects to `r1_audiobook_app`. This requires identifying a suitable function to intercept.

**Cons:** Complex, fragile across firmware updates.

### A.4 Verdict

**Current state:** FAIL. The Audiobooks tile opens the stock player, not the standalone app.  
**Required action:** Implement Option 2 (binary patch with `system()`) or Option 1 (wrapper script). Option 2 is preferred because it directly replaces the callback without adding a monitoring layer.

---

## B. App Code Quality — **PASS** (with minor issues)

### B.1 Font Sizes

- `AB_FONT_BODY_PT = 28` (in `app/src/font.h`)
- `AB_FONT_FOCUS_PT = 34` (in `app/src/font.h`)

**Verdict:** PASS. Body font at 28px and focus at 34px are within the specified 28-34px body / 34-38px focus range for a 480x800 screen.

### B.2 Theme Colors

- `TH_FOCUS_BLUE = 0x131Eu` (in `app/src/ui.c`)

**Verification:** RGB888 0x1062F2 → R=0x10=16 → 5-bit: 0b00010 = 2; G=0x62=98 → 6-bit: 0b011000 = 24; B=0xF2=242 → 5-bit: 0b11110 = 30.  
RGB565 = (2 << 11) | (24 << 5) | 30 = 0x131E. ✅ **Correct.**

### B.3 Decoder Integration (M4B/MP3/FLAC/WAV)

- **WAV:** `drwav` integration in `decoder.c` — ✅ correct
- **FLAC:** `drflac` integration in `decoder.c` — ✅ correct
- **MP3:** `minimp3_ex` integration in `decoder.c` — ✅ correct
- **M4B/M4A/AAC:** `m4b_decoder.c` using faad2 (NeAAC) — ✅ correct
  - ADTS frame scanning for .aac files
  - MP4 container parsing for .m4b/.m4a
  - Chapter parsing from `chpl` box

**Minor issue:** In `decoder.c` line for MP3 total_frames calculation:
```c
dec->total_frames = dec->channels > 0 ? dec->u.mp3.samples / dec->channels : dec->u.mp3.samples;
```
This is samples / channels = frames, which is correct for interleaved PCM.

### B.4 ALSA Playback Engine

`app/src/alsa.c` uses raw ALSA ioctl interface (not libasound):
- `SNDRV_PCM_IOCTL_HW_PARAMS` for hardware setup
- `SNDRV_PCM_IOCTL_WRITEI_FRAMES` for PCM writes
- `SNDRV_PCM_IOCTL_PAUSE` for pause/resume
- Error recovery via `SNDRV_PCM_IOCTL_PREPARE` after EPIPE

**Verdict:** PASS. Clean implementation for embedded device. Handles underrun recovery.

**Minor issue:** `alsa_pause()` may not work on all ALSA drivers. If the hardware doesn't support pause, the fallback should drain and prepare. Currently it just returns the ioctl result.

### B.5 SQLite Database Schema

`db.c` schema is comprehensive:
- `books`, `tracks`, `chapters`, `progress`, `bookmarks`, `authors`, `series`, `library_roots`, `scan_state`, `settings`
- FTS5 virtual table `book_search` for full-text search
- Proper indices on all query paths
- WAL journal mode for concurrent reads
- Foreign key cascading deletes

**Verdict:** PASS. Schema is well-designed.

**Minor issue:** `db_query_chapters_display` uses `COALESCE(c.title,'')` for both `path` and `title` columns (columns 5 and 6), which means the track `path` field is populated with the chapter title, not a file path. This is intentional (chapters are display-only) but could confuse downstream code that expects `path` to be a file path.

### B.6 UI State Machine and Rendering

`ui.c` implements a screen-based state machine:
- `UI_SCREEN_HOME` → `UI_SCREEN_CONTINUE` / `UI_SCREEN_TITLES` / `UI_SCREEN_NOW_PLAYING` / etc.
- Touch handling with tap, swipe, and back-edge gestures
- Mini player bar when a book is loaded
- Now Playing screen with cover art, progress bar, and playback controls

**Verdict:** PASS. UI logic is sound.

**Issues found:**
1. **No scrolling** (Minor): Titles/Authors/Series/Folders screens cap at 6 items (`i < 6`). Large libraries will show incomplete lists. No scroll/pagination mechanism exists.
2. **Missing back navigation from Home** (Minor): There's no way to exit the app from the home screen. The back-edge gesture only navigates within app screens.
3. **Settings screen is incomplete** (Minor): Playback speed and sleep timer rows display text descriptions but have no interactive controls.
4. **Now Playing time display** (Minor): `snprintf(status, sizeof(status), "%.2fx %llum left", ...)` uses `%llum` which is non-standard. Should use `%llu` with a cast or `%lu` on 32-bit MIPS.

### B.7 Player Thread

`player.c` implements a threaded playback engine:
- Dedicated `player_worker` thread with mutex/cond synchronization
- Decoder read → stereo conversion → ALSA write pipeline
- Track advance on EOF
- Seek support via `decoder_seek_ms`

**Verdict:** PASS. Thread safety is correct.

**Issue:** Speed control (`player_set_speed`) sets `player->speed` but **never actually applies it** to the decoder output. The decoder reads at native speed regardless. This is a **major functional gap** — speed control is non-functional.

### B.8 Resume Logic

`app/src/resume.c` (app-side) implements:
- `resume_smart_rewind_ms()` with tiers: <5min=0, <1hr=5s, <24hr=10s, >24hr/reboot=20s
- Atomic JSON record writing with temp file + rename
- `resume_read_record()` with manual JSON parsing

**Verdict:** PASS. Smart rewind logic is correct and matches spec.

### B.9 IPC

`app/src/ipc.c` uses `SOCK_SEQPACKET` Unix domain sockets with a framed protocol:
- 8-byte header (magic, version, type, payload_len, seq)
- Event payload structure

**Verdict:** PASS. Clean IPC design.

---

## C. Build Pipeline — **PASS** (with issues)

### C.1 `tools/build_firmware.py`

- Extracts stock rootfs, patches hiby_player, patches resources, installs app, repacks squashfs, builds UPT
- Correctly checks for stripped binary
- Installs app binary, launch script, fonts, theme assets
- Generates init scripts for resume daemon and DB maintenance

**Verdict:** PASS for what it does. The issue is that what it does (patching the binary to call the stock hub) doesn't achieve the goal (launching the standalone app).

### C.2 `tools/build_firmware_overlay.py`

- Overlay-based approach using `firmware_overlay.json` manifest
- Same patch logic as `build_firmware.py`
- The `patch_binary` section in the manifest only applies `--scan-skip` by default, which is correct for the scanner skip
- No launcher routing patch is applied by default

**Issue:** The overlay manifest `patch_binary` section only has `--scan-skip`. The build scripts accept flags for native hub patches but these are not in the default manifest. When invoked manually with `--include-audiobook-native-hub-launcher`, it applies the stock-hub-routing patch.

### C.3 Firmware Overlay Contents

Files included in the overlay:
- ✅ `usr/bin/r1_audiobook_app` (app binary)
- ✅ `usr/bin/r1_audiobook_launch.sh` (launch wrapper)
- ✅ `usr/share/audiobooks/fonts/msyh.ttf` (font)
- ✅ `usr/share/audiobooks/hiby-theme/` (theme assets)
- ✅ `usr/bin/r1_audiobook_resume_daemon` (daemon)
- ✅ `usr/bin/r1_audiobook_db_maint` (DB maintenance)
- ✅ Init scripts S91, S92

**Missing:**
- ❌ `usr/bin/r1_audiobook_refresh.sh` — referenced by the native hub view rows patch but **not included** in the overlay manifest's `add_files`. The `build_firmware.py` script also does not copy it. If the native hub view rows patch is enabled, the refresh command string at `0x360A80` points to `/usr/bin/r1_audiobook_refresh.sh` which won't exist.
- ❌ `usr/bin/r1_audiobook_db_watch.sh` is included but `r1_audiobook_resume_daemon.sh` (the wrapper) is listed but the shell fallback `r1_audiobook_resume_daemon_shell.sh` is also included — both are correct.

### C.4 Binary Stripping

`app/build.sh` uses `-static -s` flags in the Zig compiler invocation, which produces a stripped binary. The `build_firmware.py` script checks for "not stripped" and logs a warning.

**Verdict:** PASS.

### C.5 UPT Packaging

Uses `build_r1_upt.py` to wrap rootfs.squashfs + xImage into UPT format.

**Verdict:** PASS (assumed — no issues found in the packaging script structure).

---

## D. Daemon Code — **PASS**

### D.1 `src/state.c`

- Full state machine: `STATE_IDLE → BOOK_OPENED → TRACK_LOADING → TRACK_READY → TRACKING → BOOK_COMPLETED`
- Marker polling for autostart detection
- Direct-open track selection (no touch injection — correctly removed)
- Save bucketing with configurable intervals
- Completion detection with natural EOF check (position stopped + at end of last track)

**Verdict:** PASS. Framebuffer/touch code has been fully removed. State machine is clean.

**Issue:** `state_book_root` has a double `out[out_len - 1] = '\0'` on lines 42-43 (harmless but sloppy).

### D.2 `src/resume.c` (daemon-side)

- JSON record CRUD with atomic writes
- Smart rewind with tiered logic (matches app-side)
- Failure tracking with exponential backoff
- Save bucketing and deferred save logic

**Verdict:** PASS. Comprehensive and well-structured.

**Issue:** Multiple instances of double/triple `failure_path[sizeof(...) - 1] = '\0'` (lines 250-252, 265-267). Harmless but sloppy code.

### D.3 `src/config.c`

- Three-tier configuration: defaults → config file → env vars
- 80+ config fields with type checking and clamping
- Comprehensive env var override support

**Verdict:** PASS. Robust configuration system.

**Issue:** The `CFG_STR` field size computation using "next field offset" is fragile. If fields are reordered, string buffer sizes could change silently. Better to use explicit field sizes.

### D.4 Smart Rewind Logic

Both app-side and daemon-side smart rewind use the same tier structure:
- < 5 minutes pause: 0ms rewind
- 5 min – 1 hour: 5000ms
- 1 – 24 hours: 10000ms
- > 24 hours or reboot: 20000ms

**Verdict:** PASS. Logic is consistent between both components.

---

## E. Test Coverage — **PARTIAL PASS**

### E.1 `tools/test_phase2_regression.py`

Tests:
1. ✅ No `/dev/fb0` in daemon source
2. ✅ No touch injection in daemon source
3. ✅ Arm-window config exists
4. ✅ State machine has required states
5. ✅ Smart rewind config fields exist
6. ✅ Resume record has completed field
7. ✅ Daemon compiles clean and binary < 150KB

**Verdict:** PASS. All regression tests cover the Phase 2 refactor.

### E.2 `tests/test_suite.py`

ADB-driven on-device test suite with 8 test cases:
- `test_launcher`, `test_playback`, `test_resume`, `test_book_switch`
- `test_music_idle`, `test_db_maintenance`, `test_play_mode`, `test_navigation`

**Issue:** These tests require a connected device and ADB. They cannot validate the launcher routing issue because they assume the app is already running.

### E.3 `app/tests/test_app.py`

Host-side tests that compile and test app functions:
1. ✅ Schema and scanner (creates test WAV files, runs `--scan-only`, validates DB)
2. ✅ Resume logic (smart rewind tiers, atomic record write/read)
3. ✅ Resume seek computation
4. ✅ Track upsert IDs (RETURNING clause)
5. ✅ IPC protocol (socket communication)

**Verdict:** PASS. Good unit test coverage for core logic.

### E.4 Test Coverage Gaps

1. **No test for launcher routing** — the #1 issue is completely untested
2. **No test for speed control** — `player_set_speed` doesn't apply to decoder output
3. **No test for cover art loading** — `cover.c` not tested
4. **No test for M4B decoder** — faad2 integration not tested
5. **No test for ALSA playback** — hardware-dependent, understandable
6. **No test for UI rendering** — no framebuffer mock
7. **No test for large library scrolling** — UI caps at 6 items with no scroll

---

## Critical Blockers (MUST FIX before flash)

### BLOCKER-1: Launcher routing does not launch the standalone app

**Severity:** Critical  
**Impact:** Tapping Audiobooks tile opens stock player, not `r1_audiobook_app`  
**Root cause:** `AUDIOBOOK_NATIVE_HUB_LAUNCHER_PATCHES` calls stock hub opener at `0x00540F20`  
**Fix:** Implement binary patch with `system()` call (Option 2 in Section A.3) or wrapper script (Option 1)

### BLOCKER-2: Playback speed control is non-functional

**Severity:** Critical  
**Impact:** User cannot change playback speed  
**Root cause:** `player_set_speed()` sets `player->speed` but the value is never used in the decode/write loop. The decoder reads at native speed.  
**Fix:** Implement speed control via:
- Simple: skip/repeat frames in `write_stereo_block()` based on speed ratio
- Better: Use libswresample or a simple resampler for pitch-correct speed change
- Quick hack: For 2x speed, skip every other frame; for 0.5x, repeat each frame

### BLOCKER-3: `r1_audiobook_refresh.sh` not included in firmware overlay

**Severity:** Critical (only if native hub view rows patch is enabled)  
**Impact:** The refresh row in the native hub calls a missing script  
**Fix:** Add `r1_audiobook_refresh.sh` to `firmware_overlay.json` `add_files` section

---

## Major Issues (SHOULD FIX before release)

### MAJOR-1: No scrolling in list views

**File:** `app/src/ui.c`  
**Lines:** All list rendering loops cap at 5-6 items (`i < 6`, `i < 5`)  
**Impact:** Libraries with more than 6 books/authors/series/folders will show incomplete lists  
**Fix:** Implement touch-based scroll tracking with `scroll` offset in `ui_context`

### MAJOR-2: No way to exit the app

**File:** `app/src/ui.c`  
**Impact:** Once in the app, there's no clean way to return to the HiBy launcher  
**Fix:** Handle `TOUCH_BACK_EDGE` on `UI_SCREEN_HOME` to exit (kill the process, which will return to hiby_player)

### MAJOR-3: `command` field in `launcher.ini` is dead code

**File:** `tools/patch_r1_resource_text.py`, lines 38-40  
**Impact:** Creates a false impression that the launch mechanism is configured  
**Fix:** Remove `LAUNCHER_INSERTIONS` or add a comment that it's non-functional metadata

### MAJOR-4: Resume daemon wrapper copies legacy artifacts that don't exist

**File:** `tools/build_firmware_overlay.py`, `RESUME_BOOT_SCRIPT`  
**Lines:** References `r1_audiobook_resume_helper`, `r1_audiobook_memscan`, `r1_audiobook_direct_open`, touch event `.bin` files  
**Impact:** The boot script tries to copy files that aren't installed, producing errors  
**Fix:** The `build_firmware.py` version already has a cleaned-up script that doesn't reference these. Use the `build_firmware.py` RESUME_BOOT_SCRIPT or remove the legacy file references from the overlay build script.

---

## Minor Issues (CAN FIX later)

### MINOR-1: Double null-terminator assignments in daemon resume.c

**Files:** `src/resume.c` lines 250-252, 265-267  
**Impact:** Harmless, but sloppy

### MINOR-2: `state_book_root` double null-terminator

**File:** `src/state.c` line 42-43  
**Impact:** Harmless

### MINOR-3: Now Playing time format string

**File:** `app/src/ui.c`  
**Line:** `snprintf(status, sizeof(status), "%.2fx %llum left", ...)`  
**Fix:** Use `%llu` with `(unsigned long long)` cast (already done for `remain`)

### MINOR-4: Settings screen has no interactive controls

**File:** `app/src/ui.c`, `UI_SCREEN_SETTINGS`  
**Impact:** Playback speed and sleep timer are display-only, not interactive  
**Fix:** Add tap handlers for speed/timer rows with cycling values

### MINOR-5: `alsa_pause()` may fail silently on hardware that doesn't support it

**File:** `app/src/alsa.c`  
**Fix:** Add fallback: drain + prepare if pause ioctl fails

### MINOR-6: Config string field size computation is fragile

**File:** `src/config.c`, `CFG_STR` handling  
**Fix:** Use explicit field sizes instead of computing from adjacent struct offsets

### MINOR-7: `db_query_chapters_display` puts chapter title in track `path` field

**File:** `app/src/db.c`  
**Impact:** Semantically incorrect — `path` should be a file path, not a chapter title  
**Fix:** Document this as intentional or use a separate display struct

---

## Specific Code Fixes Needed

### Fix 1: Launcher routing (BLOCKER-1)

**File:** `tools/patch_hiby_player.py`  
**Action:** Add a new patch set `AUDIOBOOK_SYSTEM_LAUNCHER_PATCHES` that:
1. Writes a command string `/usr/bin/r1_audiobook_launch.sh &\x00` to a code cave area
2. Writes MIPS code that calls `system()` at PLT `0x0083AD80` with the command string
3. Patches the Audiobooks callback at `0x482030` to jump to the new code cave

**File:** `tools/build_firmware.py` and `tools/build_firmware_overlay.py`  
**Action:** Add a new `--include-audiobook-system-launcher` flag that selects this patch

### Fix 2: Speed control (BLOCKER-2)

**File:** `app/src/player.c`  
**Action:** In `player_worker()`, after `decoder_read_frames()`, apply speed ratio:
```c
if (player->speed != 1.0f && player->speed > 0.0f) {
    // For 2x: output every other frame
    // For 0.5x: repeat each frame twice
    // For 1.5x: use simple resampling
    // Simplest: skip/repeat frames
    if (player->speed > 1.0f) {
        size_t skip = (size_t)(player->speed - 0.5f);
        frames = frames / (skip + 1);
    }
    // Better: implement a simple linear resampler
}
```

### Fix 3: Add refresh.sh to overlay (BLOCKER-3)

**File:** `tools/firmware_overlay.json`  
**Action:** Add entry:
```json
{
  "target": "usr/bin/r1_audiobook_refresh.sh",
  "source": "tools/r1_audiobook_refresh.sh",
  "mode": "0755",
  "description": "Audiobook library refresh script"
}
```

### Fix 4: Clean up overlay RESUME_BOOT_SCRIPT (MAJOR-4)

**File:** `tools/build_firmware_overlay.py`  
**Action:** Replace the RESUME_BOOT_SCRIPT with the cleaned-up version from `build_firmware.py` that doesn't reference legacy artifacts (touch events, memscan, direct_open, resume_helper).

### Fix 5: Remove dead `command` insertion (MAJOR-3)

**File:** `tools/patch_r1_resource_text.py`  
**Action:** Remove or comment out `LAUNCHER_INSERTIONS`:
```python
# LAUNCHER_INSERTIONS = {
#     "command": "/usr/bin/r1_audiobook_launch.sh",
# }
# NOTE: The `command` field in launcher.ini is not read by hiby_player.
# It is dead metadata with no functional effect.
```

### Fix 6: Add scroll support (MAJOR-1)

**File:** `app/src/ui.c`  
**Action:** Track `ui->scroll` offset and use `TOUCH_SWIPE_UP`/`TOUCH_SWIPE_DOWN` on list screens to adjust it. Render items starting from `scroll` index.

### Fix 7: Add app exit on back-edge from home (MAJOR-2)

**File:** `app/src/ui.c`  
**Action:** Add handler:
```c
if (ui->screen == UI_SCREEN_HOME && ev->action == TOUCH_BACK_EDGE) {
    g_running = 0;  // Need to expose this or use a flag
    return 0;
}
```

---

## Summary Table

| Category | Verdict | Blockers | Major | Minor |
|----------|---------|----------|-------|-------|
| A. Launcher Routing | **FAIL** | 1 | 1 | 0 |
| B. App Code Quality | **PASS** | 1 | 0 | 4 |
| C. Build Pipeline | **PASS** | 1 | 1 | 0 |
| D. Daemon Code | **PASS** | 0 | 0 | 3 |
| E. Test Coverage | **PARTIAL** | 0 | 0 | 7 gaps |
| **Total** | **FAIL** | **3** | **2** | **7** |

---

## Recommended Next Steps

1. **Fix BLOCKER-1 first** — implement `system()`-based launcher patch in `patch_hiby_player.py`
2. **Fix BLOCKER-2** — implement speed control in `player.c`
3. **Fix BLOCKER-3** — add `r1_audiobook_refresh.sh` to overlay manifest
4. **Fix MAJOR-4** — clean up overlay boot script to match `build_firmware.py` version
5. **Fix MAJOR-3** — remove dead `command` insertion from resource patch
6. **Rebuild and test on device** — verify Audiobooks tile launches `r1_audiobook_app`
7. **Add launcher routing test** — verify the callback executes the launch script
8. **Fix MAJOR-1 and MAJOR-2** — add scrolling and app exit for production use

---

*End of validation report.*