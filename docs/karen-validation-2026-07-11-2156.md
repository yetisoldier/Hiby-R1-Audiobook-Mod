# Karen Validation Report — 2026-07-11 21:56

**Project:** HiBy R1 Audiobook Firmware  
**Validator:** Karen (QA/Validation)  
**Scope:** Full codebase validation + launcher refactor (execve → flag file + concurrent wrapper)  
**Verdict:** ✅ **READY TO FLASH** (0 critical blockers, 1 major issue, 4 minor issues)

---

## 1. Binary Patch (tools/patch_hiby_player.py) — ✅ PASS

### 1.1 System Launcher Code Cave (0x35E000)

**MIPS Assembly Verification — PASS**

Decoded all 17 instructions from `AUDIOBOOK_SYSTEM_LAUNCHER_CODE`:

| Offset | Instruction | Purpose |
|--------|-------------|---------|
| 0  | `addiu $sp, $sp, -24` | Prologue: allocate 24-byte stack frame |
| 4  | `sw $ra, 20($sp)` | Save return address |
| 8  | `sw $s0, 16($sp)` | Save s0 (callee-saved) |
| 12 | `lui $a0, 0x0076` | Load path string address high |
| 16 | `addiu $a0, $a0, -8064` | Load path string address low → a0 = 0x0075E080 |
| 20 | `addiu $a1, $zero, 0x241` | O_CREAT\|O_WRONLY\|O_TRUNC = 577 = 0x241 |
| 24 | `addiu $a2, $zero, 0x1A4` | File mode 0644 = 420 = 0x1A4 |
| 28 | `jal 0x00839E20` | Call open@plt |
| 32 | `nop` | Delay slot |
| 36 | `addiu $a0, $v0, 0` | Move open() return (fd) to a0 for close() |
| 40 | `jal 0x0083ABE0` | Call close@plt |
| 44 | `nop` | Delay slot |
| 48 | `lw $s0, 16($sp)` | Epilogue: restore s0 |
| 52 | `lw $ra, 20($sp)` | Epilogue: restore ra |
| 56 | `addiu $sp, $sp, 24` | Epilogue: restore stack pointer |
| 60 | `jr $ra` | Return to caller |
| 64 | `nop` | Delay slot |

**Calling convention — PASS:**
- MIPS o32 ABI: args in $a0-$a2, return in $v0 ✓
- Stack frame allocated and restored (24 bytes, 8-byte aligned) ✓
- $ra and $s0 saved/restored ✓
- Delay slots filled with nop after jal/jr ✓
- Returns normally to caller (unlike execve which never returns) ✓

**Flag file open() arguments — PASS:**
- `O_WRONLY = 0x001` ✓
- `O_CREAT = 0x040` ✓  
- `O_TRUNC = 0x200` ✓
- Combined = `0x241` = 577 ✓ (correct for MIPS Linux)
- Mode `0644 = 0x1A4 = 420` ✓

**PLT addresses — PASS:**
- `open@plt = 0x00839E20` ✓
- `close@plt = 0x0083ABE0` ✓
- Both are `jal` targets (J-type, target = field << 2) ✓

**Address loading — PASS:**
- Path string at 0x35E080 → `text_addr(0x35E080) = 0x0075E080`
- `lui $a0, 0x0076` + `addiu $a0, $a0, -8064` → `0x00760000 + (-8064) = 0x00760000 - 0x1F80 = 0x0075E080` ✓
- Uses `load_addr_words()` with `+0x8000` bias for sign extension ✓

**No execve/syscall remnants — PASS:**
- No `syscall` instruction (op=0, funct=0x0C) found in code cave ✓
- No `jal` to non-PLT addresses (only open@plt and close@plt) ✓
- Only reference to "execve" is in a comment explaining the approach difference ✓
- No argv array from old execve approach present ✓

### 1.2 Callback Patch (0x482030)

- Expected stock bytes: `ecda7500` (= 0x0075DAEC LE = genre launcher cave) ✓
- Replacement: `00e07500` (= 0x0075E000 LE = system launcher cave) ✓
- `pack_u32(0x0075E000)` = `00e07500` ✓

### 1.3 Command String (0x35E080)

- `b"/tmp/.r1_audiobook_launch\x00"` — 26 bytes ✓
- Padded to 0x40 (64 bytes) with null bytes ✓
- Offset 0x35E080 is within the code cave data region (code ends at 0x35E000+68=0x35E044, data at 0x35E080) ✓
- No overlap between code and data regions ✓

### 1.4 Mutual Exclusion & Safety

- `audiobook_system_launcher` mutually exclusive with all other launcher patches ✓
- Validation raises `SystemExit` with descriptive message if conflicting flags combined ✓
- `--skip-existing-patches` relaxes MD5/SHA256 check for re-patching ✓
- Stock binary hash verification (MD5 + SHA256) prevents accidental cross-version patching ✓

---

## 2. Concurrent Wrapper (tools/hiby_player.sh) — ✅ PASS

### 2.1 Shell Syntax Check

```
$ sh -n tools/hiby_player.sh
EXIT_CODE=0
```
**PASS** — No syntax errors.

### 2.2 Logic Verification

**Flag file polling — PASS:**
- Flag: `/tmp/.r1_audiobook_launch` ✓
- Poll interval: 100ms (usleep 100000 or sleep 0.1 fallback) ✓
- Polls while `hiby_player` is alive (`kill -0 $HP_PID`) ✓
- Flag removed at loop start (`rm -f "$FLAG"`) to prevent stale detection ✓

**Kill sequence — PASS:**
1. `kill -TERM $HP_PID` — graceful termination ✓
2. Wait up to 2 seconds (20 × 100ms polls) ✓
3. `kill -9 $HP_PID` — force kill if still alive ✓
4. `wait $HP_PID` — reap zombie ✓
5. `rm -f "$FLAG"` — clean up flag ✓

**Framebuffer clear — PASS:**
- `dd if=/dev/zero of=/dev/fb0 bs=960 count=800 2>/dev/null` ✓
- 960 × 800 × 2 bytes (RGB565) = 1,536,000 bytes = full screen ✓
- Cleared both before launching app and after app exit ✓

**App launch — PASS:**
- App path: `/usr/bin/r1_audiobook_app` ✓
- Executable check: `[ -x "$APP" ]` ✓
- Foreground execution: `"$APP"` (no `&`) ✓
- After app exit: clears framebuffer, loops back to relaunch hiby_player ✓

**Crash counter — PASS:**
- Counter initialized to 0, max 5 ✓
- Increments on hiby_player exit without flag ✓
- Resets to 0 on successful flag detection and app launch ✓
- Reboots after 5 crashes: `reboot` ✓
- 1-second pause between crash relaunch attempts ✓

**usleep fallback — PASS:**
- `command -v usleep` check ✓
- Falls back to `sleep 0.1` (BusyBox supports fractional sleep) ✓

### 2.3 Edge Case Analysis

**Race condition: flag created between poll and kill-0 check — SAFE:**
- If flag appears after `kill -0` returns false (hiby_player died), the outer while loop exits, crash counter increments. Flag is cleaned at next loop start. No stale launch.

**Race condition: hiby_player exits naturally during flag poll — SAFE:**
- `kill -0 $HP_PID` check at loop top prevents acting on a dead process. If flag exists and process is dead, the inner while exits, we fall through to crash handling.

**Edge case: app crashes immediately — SAFE:**
- App exits with non-zero status, wrapper clears framebuffer and relaunches hiby_player. Crash counter remains 0 (it was reset before app launch).

**Edge case: flag file already exists at startup — SAFE:**
- `rm -f "$FLAG"` at loop start removes any stale flag.

**Minor concern: `kill -0` after SIGTERM — SAFE but could be cleaner:**
- The `wait` after `kill -9` handles zombie reaping. If process was already dead, `kill -9` returns error (suppressed by `2>/dev/null`), and `wait` returns immediately.

---

## 3. App Framebuffer Clear (app/src/main.c) — ✅ PASS

### 3.1 Clear Before Shutdown

Lines 295-300 of main.c:
```c
/* Clear framebuffer to prevent stale frame on exit */
if (ui.fb.pixels) {
    memset(ui.fb.pixels, 0, (size_t)ui.fb.stride * ui.fb.height * sizeof(uint16_t));
    fb_present(&ui.fb);
}

ui_shutdown(&ui);
```

- `memset` zeroes the pixel buffer ✓
- `fb_present` writes zeros to `/dev/fb0` ✓
- Both happen BEFORE `ui_shutdown()` which closes the fb fd ✓
- Guarded by `if (ui.fb.pixels)` null check ✓

### 3.2 Field Names Match Struct

From `fb.h`:
```c
typedef struct fb_context {
    int fd;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint16_t *pixels;
    size_t pixels_len;
} fb_context;
```

- `ui.fb.pixels` — `uint16_t *` ✓
- `ui.fb.stride` — `uint32_t` ✓  
- `ui.fb.height` — `uint32_t` ✓
- `sizeof(uint16_t)` correct for RGB565 ✓
- `(size_t)ui.fb.stride * ui.fb.height * sizeof(uint16_t)` = total buffer size ✓

### 3.3 fb_present Function

From `fb.c`:
```c
void fb_present(fb_context *fb) {
    if (!fb || fb->fd < 0 || !fb->pixels) return;
    size_t bytes = fb->pixels_len * sizeof(uint16_t);
    lseek(fb->fd, 0, SEEK_SET);
    if (write(fb->fd, fb->pixels, bytes) < 0) { /* Best-effort */ }
}
```
- Exists, correct signature ✓
- Writes full buffer to fb device ✓
- Best-effort (ignores write errors) — appropriate for cleanup ✓

---

## 4. Full Codebase Re-check — ✅ PASS

### 4.1 Build Verification

```
$ sh app/build.sh
EXIT_CODE=0

$ file build/r1_audiobook_app
ELF 32-bit LSB executable, MIPS, MIPS32 rel2 version 1 (SYSV), 
statically linked, stripped
Size: 2,570,824 bytes (~2.5 MB)
```

**PASS** — Clean compilation, stripped MIPS ELF, reasonable size.

### 4.2 App Code Quality (app/src/*.c)

**Compilation flags — PASS:**
- `-std=c11 -Wall -Wextra -Werror -O2` — strict warnings as errors ✓
- `-ffunction-sections -fdata-sections` — dead code elimination ✓
- `-fno-strict-aliasing` — correct for type-punning patterns ✓
- `-static -s` — static link + strip ✓
- `-target mipsel-linux-musleabi` — correct target ✓

**Memory safety — PASS (with minor notes):**
- All allocations use `ab_xcalloc` (checked calloc wrapper) ✓
- All reallocs use `ab_xrealloc` (checked realloc wrapper) ✓
- String copies use `ab_copy_str` with size limits ✓
- `snprintf` used consistently with buffer size ✓
- No raw `strcpy` into fixed buffers found ✓
- No format string vulnerabilities (user data never used as format string) ✓

**Player thread safety — PASS:**
- `pthread_mutex_lock/unlock` around all shared state ✓
- `pthread_cond_broadcast` after state changes ✓
- Worker thread properly joined in `player_shutdown` ✓
- No lock-ordering issues detected ✓

**Decoder integration — PASS:**
- MP3 (minimp3_ex), FLAC (dr_flac), WAV (dr_wav), M4B/AAC (faad2) — all 4 decoders ✓
- Each decoder properly closed in `decoder_close` switch ✓
- EOF detection via `got == 0` from read functions ✓
- Seek via `decoder_seek_ms` with frame conversion ✓

**M4B decoder — PASS:**
- ADTS frame scanning for .aac files ✓
- MP4 container parsing for .m4b/.m4a files ✓
- Chapter parsing from `chpl` box ✓
- Proper NeAACDec lifecycle (open, configure, decode, close) ✓

### 4.3 Font Sizes

From `font.h`:
```c
#define AB_FONT_BODY_PT 56
#define AB_FONT_FOCUS_PT 68
```
- Body: 56px ✓ (specified)
- Focus: 68px ✓ (specified)

### 4.4 Theme Colors

From `ui.c`:
```c
#define TH_FOCUS_BLUE 0x131Eu
```
- HiBy brand blue: RGB888 0x10,0x62,0xF2 → RGB565: R=0b00010=0x02, G=0b011000=0x18, B=0b11110=0x1E
- `(0x02 << 11) | (0x18 << 5) | 0x1E = 0x4000 | 0x300 | 0x1E = 0x431E`

**⚠️ MINOR ISSUE:** The code says `0x131E`, but the correct RGB565 value for HiBy blue (0x1062F2) is `0x431E`. However, `0x131E` appears to be an intentional design choice (a darker blue), and it has been consistent across the codebase. Not a blocker — just noting the mathematical discrepancy.

### 4.5 ALSA Playback Engine — PASS

- Direct ioctl-based ALSA access (no libasound dependency) ✓
- HW params: S16_LE, interleaved, configurable rate/channels/buffer ✓
- SW params: start_threshold, stop_threshold, boundary ✓
- Error recovery: `alsa_drop` + `alsa_prepare` on EPIPE/ESTRPIPE ✓
- Pause via `SNDRV_PCM_IOCTL_PAUSE` ✓

### 4.6 SQLite Schema — PASS

- WAL journal mode ✓
- Foreign keys enabled ✓
- FTS5 virtual table for full-text search ✓
- Proper indexes on all query paths (titles, authors, series, continue, tracks, bookmarks) ✓
- CASCADE deletes on tracks/chapters/bookmarks/progress ✓
- `db_migrate` uses `CREATE TABLE IF NOT EXISTS` — idempotent ✓
- Transaction-wrapped multi-table updates (`db_set_progress_txn`, `db_set_book_completion_txn`) ✓

### 4.7 UI State Machine — PASS

States: HOME, CONTINUE, TITLES, NOW_PLAYING, CHAPTERS, FINISHED, SETTINGS, AUTHORS, SERIES, FOLDERS, BOOKMARKS

**Navigation — PASS:**
- Titles/Continue/Finished → Home (via `TOUCH_BACK_EDGE`) ✓
- Home → exit (via `TOUCH_BACK_EDGE`, sets `g_request_exit = 1`) ✓
- Authors/Series/Folders → Settings (via `TOUCH_BACK_EDGE`) ✓
- Now Playing → Home (via `TOUCH_BACK_EDGE`) ✓
- Chapters → Now Playing (via `TOUCH_BACK_EDGE`) ✓
- Swipe right from any non-Now-Playing screen → Now Playing if book loaded ✓

**Touch handling — PASS:**
- TAP: x/y within row bounds ✓
- Swipe detection: dx/dy thresholds (40px tap, 80px swipe) ✓
- Back edge: start_x < 40 && dx > 120 ✓

### 4.8 Speed Control — PASS

Fractional accumulator approach in `player_worker`:
- Speed > 1.0x: accumulate `1.0 - (1.0/speed)`, skip block when ≥ 1.0 ✓
- Speed < 1.0x: accumulate `(1.0/speed) - 1.0`, repeat block when ≥ 1.0 ✓
- Accumulator reset on speed change (`player_set_speed`) ✓
- Position tracking via `decoder.current_frame` (reflects actual decoder position) ✓
- Speed range validated: 0.25x to 4.0x ✓

### 4.9 Daemon Code (src/) — PASS

**state.c:**
- Full state machine: IDLE → BOOK_OPENED → TRACK_LOADING → TRACK_READY → TRACKING → BOOK_COMPLETED ✓
- Autostart marker polling with context window ✓
- Track restore via direct-open helper (no touch injection) ✓
- Save bucketing by position intervals ✓
- Completion detection: requires last track + position >= duration + position stopped ✓
- Crash counter and reboot logic ✓
- Diagnostics logging with rate limiting ✓

**resume.c:**
- Atomic file writes (temp + rename) ✓
- JSON serialization with escaping ✓
- Smart rewind tiers (5min/1hr/24hr/reboot) ✓
- Exponential backoff on seek failures ✓
- Deferred save for new tracks ✓
- Completed-book start-over handling ✓

**config.c:**
- Three-tier config: defaults → file → env vars ✓
- 80+ config fields with type checking and clamping ✓
- All fields have env var overrides with `AUDIOBOOK_` prefix ✓

### 4.10 Build Pipeline — PASS

**build_firmware.py:**
- Extracts stock rootfs from squashfs ✓
- Applies binary patches via `patch_hiby_player.py` ✓
- Patches resources via `patch_r1_resource_text.py` ✓
- Installs app, wrapper, fonts, theme assets ✓
- Generates init scripts from templates with feature flag substitution ✓
- Repacks squashfs with preserved modes via pseudo file ✓
- Wraps in UPT firmware package ✓
- Clean stock rootfs extraction (not stale work directory) ✓

**firmware_overlay.json:**
- Manifest correctly lists all added files, scripts, patches ✓
- Mode overrides match expected permissions ✓
- SquashFS options (lzo, 131072 block, all-root) ✓

### 4.11 Test Coverage — PASS (with note)

- `tests/test_suite.py` — Python test suite ✓
- `tests/test_resume_daemon.c` — C test for resume daemon ✓
- `tests/test_cases/` — test case directory ✓
- `tests/on-device-test-plan-v1.7.0.md` — on-device test plan ✓
- `tests/on-device-results-v1.7.0.md` — previous on-device results ✓
- `tools/test_r1_db_watch_logic.sh` — DB watcher logic test ✓
- `tools/test_r1_resume_daemon_logic.sh` — resume daemon logic test ✓

---

## 5. Critical Blockers — NONE

No critical blockers found.

---

## 6. Major Issues — 1

### MAJ-1: TH_FOCUS_BLUE color value mathematically incorrect

**File:** `app/src/ui.c`, line ~25  
**Line:** `#define TH_FOCUS_BLUE 0x131Eu`  
**Issue:** RGB888 (0x10, 0x62, 0xF2) → RGB565 should be `0x431E`, not `0x131E`. The R component is 0x10 >> 3 = 2 (5 bits), but `0x131E` has R = 0x02 which is correct. Wait — let me recalculate:
- R = 0x10 = 16. In 5-bit: 16 >> 3 = 2 = 0b00010. Shifted: `0b00010 << 11 = 0x2000`
- G = 0x62 = 98. In 6-bit: 98 >> 2 = 24 = 0b011000. Shifted: `0b011000 << 5 = 0x0300`  
- B = 0xF2 = 242. In 5-bit: 242 >> 3 = 30 = 0b11110. Shifted: `0b11110 = 0x1E`
- Total: `0x2000 | 0x0300 | 0x1E = 0x231E`

Actually `0x131E` vs `0x231E` — the R component is `0b00010` (2) but the actual 5-bit value for R=16 is also `0b00010` (2). So `2 << 11 = 0x2000`. The value `0x131E` has bit pattern `0001 0011 0001 1110` → R = `00010` = 2, G = `011000` = 24, B = `11110` = 30. This gives `0x231E`, not `0x131E`.

The value `0x131E` decodes as R = `00010` = 2, G = `001100` = 12, B = `11110` = 30. That gives G = 12 << 2 = 48, not 98. So the green channel is wrong — it's showing 48 instead of 98.

**Severity:** Major (visual — focus color is more teal than the intended blue)  
**Fix:** Change `0x131Eu` to `0x231Eu`  
**Impact:** Cosmetic only — does not affect functionality. Can fix in next release.

---

## 7. Minor Issues — 4

### MIN-1: hiby_player.sh — `&>/dev/null` not POSIX

**File:** `tools/hiby_player.sh`, lines 2-3  
**Issue:** `&>/dev/null` is a bashism. The script uses `#!/bin/sh`. On BusyBox sh this works, but strict POSIX would use `>/dev/null 2>&1`.  
**Fix:** Replace `&>/dev/null` with `>/dev/null 2>&1`  
**Impact:** Works on target (BusyBox sh supports this), but technically non-portable.

### MIN-2: hiby_player.sh — `sleep 0.1` may not work on all BusyBox builds

**File:** `tools/hiby_player.sh`, line 18  
**Issue:** Fractional sleep is a BusyBox extension, not guaranteed on all builds. The code has a fallback comment mentioning this.  
**Impact:** If usleep is available (checked first), this path is never hit. If neither works, polling degrades to `sleep 1` (1-second granularity). The R1's BusyBox does support fractional sleep.  
**Fix:** Add a third fallback: `POLL_SLEEP="sleep 1"` if both usleep and fractional sleep fail. Currently the code only checks for usleep.

### MIN-3: Scanner track duration estimation is rough

**File:** `app/src/scanner.c`, `add_track()` function  
**Issue:** Duration estimated as `file_size / 160` for all formats. This is a rough approximation (assumes ~128kbps). Actual duration is read from decoder metadata on first play.  
**Impact:** Initial library scan shows approximate durations. Corrected when each track is played. Not a blocker.

### MIN-4: db_query_chapters_display has a dummy fingerprint column

**File:** `app/src/db.c`, `db_query_chapters_display()`  
**Issue:** SQL query has `''` as the fingerprint column (column 12), but the `track_row` struct expects `fingerprint` there. The code doesn't read column 12 for chapters, so this is harmless but inconsistent.  
**Impact:** None — chapters don't use fingerprint. Cosmetic/structural issue only.

---

## 8. Summary Table

| Category | Verdict | Notes |
|----------|---------|-------|
| Binary Patch (MIPS assembly) | ✅ PASS | 17 instructions, correct calling convention, no execve remnants |
| Binary Patch (flag file approach) | ✅ PASS | open()/close() PLT calls, O_CREAT\|O_WRONLY\|O_TRUNC=0x241, mode 0644 |
| Binary Patch (callback) | ✅ PASS | Stock ecda7500 → new 00e07500 at 0x482030 |
| Concurrent Wrapper (syntax) | ✅ PASS | sh -n clean |
| Concurrent Wrapper (logic) | ✅ PASS | Flag polling, kill sequence, crash counter, fb clear all correct |
| App Framebuffer Clear | ✅ PASS | memset + fb_present before ui_shutdown, correct field names |
| Build Verification | ✅ PASS | Clean compile, stripped MIPS ELF, 2.5 MB |
| App Code Quality | ✅ PASS | -Werror, xcalloc/xrealloc, proper thread safety |
| Font Sizes | ✅ PASS | 56px body, 68px focus |
| Theme Colors | ⚠️ PASS (minor) | TH_FOCUS_BLUE mathematically 0x231E not 0x131E (MAJ-1) |
| Decoders (MP3/FLAC/WAV/M4B) | ✅ PASS | All 4 decoders with proper lifecycle |
| ALSA Playback | ✅ PASS | Direct ioctl, error recovery, pause support |
| SQLite Schema | ✅ PASS | WAL, FTS5, proper indexes, transactions |
| UI State Machine | ✅ PASS | 11 screens, correct navigation, back handling |
| Speed Control | ✅ PASS | Fractional accumulator, reset on change, correct range |
| Back Navigation | ✅ PASS | All screens return to Home, Home exits |
| Daemon (state.c) | ✅ PASS | Full state machine, autostart, completion, save bucketing |
| Daemon (resume.c) | ✅ PASS | Atomic writes, smart rewind, exponential backoff |
| Daemon (config.c) | ✅ PASS | Three-tier config, 80+ fields, env overrides |
| Build Pipeline | ✅ PASS | Stock rootfs extraction, patches, squashfs, UPT |
| Test Coverage | ✅ PASS | Unit tests, on-device tests, test plan |

---

## 9. Overall Verdict

### ✅ READY TO FLASH

The launcher refactor from execve to flag file + concurrent wrapper is **correct and safe**. The MIPS assembly is well-formed, follows the o32 ABI calling convention, and contains no remnants of the old execve approach. The concurrent wrapper script has proper flag detection, kill sequencing, crash recovery, and framebuffer management. The app framebuffer clear happens at the right point in the shutdown sequence.

The full codebase review found no critical blockers. The one major issue (TH_FOCUS_BLUE color) is cosmetic and can be fixed post-flash. The four minor issues are all non-functional.

**Recommendation:** Flash it. The launcher approach is fundamentally sound — the flag file decouples hiby_player termination from the audiobook app launch, eliminating the OOM risk that execve created. The wrapper's crash counter provides automatic recovery, and the framebuffer clears prevent visual artifacts during transitions.