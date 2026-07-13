# Bob — Holistic Project Re-evaluation: HiBy R1 Audiobook Firmware Mod

**Date:** 2026-07-13  
**Reviewer:** Bob, Principal Software Architect  
**Scope:** Complete from-scratch evaluation of the entire project — Phase 1 (binary patching) vs Phase 2 (standalone app), launcher mechanism, touch IC, firmware strategy, and the most pragmatic path to Eric's goals.

---

## Executive Summary

**The standalone app approach is architecturally correct and should be continued.** The launcher callback patch is verified working in runtime memory. The flag-file mechanism is verified end-to-end (flag creation → wrapper detection → hiby_player kill → app launch). The reason the Audiobooks tile doesn't open the app is not a software bug — it's a **dead touchscreen IC** that generates zero input events.

**The most critical immediate action is a hard power cycle of the R1** to reset the CST8xx touch IC. Once touch works, the existing v1.7.8 firmware should work as designed.

Phase 1 (binary patching hiby_player's stock view) had 17 working releases but was architecturally fragile — it depended on framebuffer pixel detection, touch event injection, and timing-based UI guessing. Phase 2 (standalone app) is the right long-term path and is closer to production than it appears.

However, the standalone app has critical bugs that must be fixed before it can be trusted for daily use. The most serious is that `library_refresh()` on startup calls `db_clear_library()` which deletes all progress and bookmarks — directly violating the central promise of the audiobook feature.

---

## 1. Should We Continue the Standalone App or Go Back to Binary Patching?

### Verdict: Continue the standalone app. Do not go back to Phase 1.

### Reasoning

**Phase 1 (binary patching hiby_player's stock audiobook view) had 17 working releases, but:**

- It required a 2,500-line shell resume daemon with 80+ environment variables
- It depended on framebuffer pixel detection to identify which screen was visible
- It injected synthetic touch events (`/dev/input/event1`) to auto-tap track rows
- It used `/proc/PID/mem` reads to scrape playback position from hiby_player's memory
- It required M3U playlist files as the user-visible book abstraction
- Back navigation was contaminated by reusing stock genre routing
- Every stock firmware update would shift all hardcoded addresses
- The track-list flash (2-5 seconds) was an inherent limitation
- Resume reliability depended on UI timing, not deterministic events

These are not fixable problems — they are architectural limitations of trying to make hiby_player behave like an audiobook app through external automation.

**Phase 2 (standalone app) is architecturally superior because:**

- The app owns the UI, queue, playback, and resume — no guessing
- Book selection is deterministic (tap title → callback → app opens book)
- Resume is event-driven (app writes progress on pause, exit, tick)
- No framebuffer pixel detection, no touch injection, no timing windows
- No dependency on hiby_player's internal memory layout
- The stock music player is untouched (zero regression risk for music)
- The app is 2.5MB static MIPS ELF — tiny compared to hiby_player's 16MB RSS
- The flag-file launcher mechanism is proven to work (verified live on device today)

**Phase 2 is stuck on one hardware issue (dead touch IC), not on a software architecture problem.** The callback patch is correct. The dispatch chain is correct. The wrapper script is correct. The app launches when the flag file is created. The only missing piece is touch input to trigger the tile tap.

### When would going back to Phase 1 make sense?

Only if:
1. The standalone app's playback engine proves fundamentally broken on the R1 (ALSA can't open, decoders crash, etc.)
2. The touch IC is permanently dead and cannot be fixed (hardware failure, not just power state)
3. Eric explicitly prefers the Phase 1 UX with its 2-5 second track-list flash and timing-based resume

None of these are currently true. The app launched successfully today (PID 24082). The touch IC issue is almost certainly a power-state corruption fixable by a hard power cycle.

---

## 2. How to Actually Launch the Standalone App

### Current Mechanism (Verified Working)

The launch chain is:

1. **Callback table patch:** Entry 10 (`_apps_vg_ebook`) at file offset `0x481FD0` points to code cave at `0x0075E000`
2. **Code cave:** Calls `open("/tmp/.r1_audiobook_launch", O_CREAT|O_WRONLY|O_TRUNC, 0644)` via PLT, then `close(fd)`, then returns
3. **Wrapper script (`hiby_player.sh`):** Polls for flag file every 50ms while hiby_player runs. On detection: kills hiby_player (SIGTERM → SIGKILL), clears framebuffer, launches `r1_audiobook_app` in foreground
4. **App exit:** Wrapper clears framebuffer and relaunches hiby_player

**Verification performed today (2026-07-13):**
- Runtime memory at `0x00891FD0` contains `0x0075E000` (our callback) ✓
- Runtime memory at `0x0075E000` contains our code cave instructions ✓
- Dispatch chain at `0x0049D71C-0x0049D734` loads `widget->0x168` and calls via `jalr t9` ✓
- Creating `/tmp/.r1_audiobook_launch` manually caused the wrapper to kill hiby_player and launch the app (PID 24082) ✓
- The app ran successfully (we killed it manually to test the relaunch cycle) ✓
- hiby_player was relaunched by the wrapper after the app exited (PID 24094) ✓

**The mechanism works. The only reason it doesn't trigger on tile tap is that the touchscreen IC is dead** (IRQ count stays at 115, no input events generated).

### Fix the Touch IC First

Per `bob-touch-analysis.md`, the recommended fix is:

1. **Hard power cycle:** Hold power button 10-15 seconds, wait 30 seconds, power on
2. If that doesn't work: **GPIO hardware reset** of the CST8xx via ADB (toggle reset pin)
3. If that doesn't work: **I2C register dump** during active touch for diagnosis
4. If all else fails: **Disassemble the touch driver** (`cst8xx_touch.ko`) to find the silent skip path

**Once touch works, the existing v1.7.8 firmware should launch the audiobook app on tile tap.** No code changes needed for the basic launch mechanism.

### Alternative Launch Fallbacks (If Touch Remains Unreliable)

If the touch IC has a recurring reliability problem, consider these fallbacks:

#### Fallback A: Physical Button Combo (Recommended Fallback)

Hook a physical button handler in hiby_player (e.g., long-press play/pause for 3 seconds) to create the flag file. Physical buttons use `/dev/input/event0` (gpio-keys), which is independent of the touchscreen driver.

**Implementation:** Find the EV_KEY handler in hiby_player, patch it to detect the combo, and call the same `open()`/`close()` flag file code cave. This requires finding the button event handler address in the binary.

**Pros:** Independent of touchscreen, reliable, no additional processes  
**Cons:** Requires another binary patch, less intuitive than a tile tap

#### Fallback B: Auto-Launch on Boot (Skip hiby_player)

Replace `hiby_player.sh` to launch the audiobook app directly on boot, with a button-hold-during-boot to fall back to stock hiby_player.

**Implementation:**
```sh
#!/bin/sh
# Check for force-stock-player flag (set by holding a button during boot)
if [ -f /tmp/.force_stock_player ]; then
    exec /usr/bin/hiby_player.stock
fi
# Launch audiobook app directly
/usr/bin/r1_audiobook_app
# On exit, launch stock player
exec /usr/bin/hiby_player.stock
```

**Pros:** Zero latency, no binary patching, app is the default  
**Cons:** User loses access to stock player by default, requires physical interaction to access music

**Recommendation:** Do not implement either fallback until after testing whether the hard power cycle fixes the touch IC. The touch IC issue is almost certainly transient, not permanent.

---

## 3. What Improvements Make Phase 2 Production-Quality?

The standalone app has the right architecture but several critical implementation bugs. These must be fixed before the app can be trusted for daily use.

### Critical Bugs (Must Fix Before Any User-Facing Release)

#### Bug 1: Startup scan deletes all progress and bookmarks

**Location:** `app/src/main.c` line ~140, `app/src/scanner.c:305-309`, `app/src/db.c:63-65`

**Problem:** When the app starts, if the library needs refresh, it calls `db_clear_library()` which deletes the `progress`, `bookmarks`, `chapters`, `tracks`, and `books` tables. This means every library refresh destroys all resume positions.

**Fix:** Make library refresh transactional and preserve progress/bookmarks:
1. Remove `db_clear_library()` from the normal refresh path
2. Scan into a temporary table, diff against existing, apply only changes in a single transaction
3. Never delete `progress` or `bookmarks` rows — only delete `tracks` and `chapters` for books that are genuinely gone from the filesystem
4. Map changed track paths to existing progress records by `book_key` + `ordinal`

**Priority:** P0 — this directly violates the spec's central promise (§7.1: "Switching from an audiobook to music must not discard audiobook progress")

#### Bug 2: M4B embedded chapters conflated with playback tracks

**Location:** `app/src/scanner.c:221-229`, `app/src/db.c:366-404`

**Problem:** M4B embedded chapters are stored in the `chapters` table, but `db_query_chapters_for_playback()` returns them as a `track_list` with `path` set to chapter title text (not a file path). When the player tries to open these, it will fail because the "path" is a title string, not an audio file.

**Fix:** Separate the chapter model from the track model:
1. `db_query_tracks_for_playback()` must always return playable file tracks (one row per actual file)
2. `db_query_chapters_for_display()` should return chapter metadata (title, start_ms, end_ms) for display
3. For M4B files with embedded chapters, the player should seek within the single track, not switch to a different "track"

**Priority:** P0 — M4B is a core audiobook format and must work correctly

#### Bug 3: M4B parser not large-file-safe

**Location:** `app/src/m4b_decoder.c:49,194,203-207,363`, `app/build.sh`

**Problem:** Uses `fseek()`/`ftell()` with `long` casts, stores ADTS offsets as `uint32_t`, and does not compile with `_FILE_OFFSET_BITS=64`. Audiobook M4B files can exceed 2GB, which will overflow 32-bit offsets.

**Fix:**
1. Add `-D_FILE_OFFSET_BITS=64` to `app/build.sh` compile flags
2. Replace `fseek()`/`ftell()` with `fseeko()`/`ftello()` and use `off_t` (64-bit on MIPS with the define)
3. Change ADTS offset storage from `uint32_t` to `uint64_t`
4. Test with a >2GB M4B file

**Priority:** P1 — won't affect small files but will silently corrupt large audiobooks

#### Bug 4: FAAD2 M4B decoder is not reentrant

**Location:** `app/src/m4b_decoder.c:243-249,338,351-353`

**Problem:** Uses global `mp4config`/`mp4read` state from FAAD2's frontend. If the scanner extracts M4B metadata while M4B playback is active, the global state will corrupt.

**Fix:** Serialize all M4B access — either:
1. Use a mutex around all M4B decoder calls, or
2. Move scanning to a separate helper process (the existing `r1_audiobook_db_maint` binary), or
3. Don't scan during playback (the DB maintenance daemon already handles this)

**Priority:** P1 — only triggers if scan and playback overlap

### High-Priority Improvements

#### Improvement 5: Playback speed is not implemented

**Location:** `app/src/player.c:330-335`

**Problem:** `player_set_speed()` changes the state variable but doesn't time-stretch the PCM stream. Audio plays at 1.0x regardless of the speed setting.

**Fix:** Implement a simple WSOLA or overlap-add time-stretch between decoder output and ALSA output. For MIPS with limited CPU, a simple resampling approach (skip/duplicate samples) is acceptable as a first pass, though it will change pitch. For pitch preservation, use a lightweight WSOLA implementation.

**Priority:** P2 — speed control is in the spec (§11) but not a launch blocker

#### Improvement 6: Seek implementation is incomplete

**Location:** `app/src/player.c:319-327`

**Problem:** `pending_seek_ms` is stored but the actual seek derives from `player->position_ms`, not the requested value. Skip forward/backward won't work correctly.

**Fix:** Use `pending_seek_ms` as the seek target, not the current position. After seek, verify the actual position is within tolerance and retry once if not.

**Priority:** P1 — skip controls are essential for audiobook use

#### Improvement 7: Smart rewind doesn't match spec

**Location:** `app/src/resume.c:57-63`

**Problem:** Current: 10s after 5min, 20s after 15min. Spec: 0s under 5min, 5s for 5-60min, 10s for 1-24h, 15-20s after 24h/reboot.

**Fix:** Implement the spec's tiered rewind table.

**Priority:** P2 — nice to have but not a launch blocker

#### Improvement 8: Scanner is a skeleton

**Location:** `app/src/scanner.c`

**Problem:** Doesn't parse `book.json`, doesn't parse embedded metadata (ID3, MP4 atoms), estimates duration from file size, doesn't populate authors/series, doesn't handle direct files under `/Audiobooks` as separate books, is case-sensitive for extensions.

**Fix:** For the initial release, at minimum:
1. Parse `book.json` sidecar files
2. Read basic ID3 tags (TALB, TPE2, TIT2) for MP3 files
3. Handle direct files under `/Audiobooks/*.m4b` as separate books
4. Make extension matching case-insensitive
5. Use the existing `r1_audiobook_db_maint.c` as the scanner backend (it already has this logic)

**Priority:** P1 — the scanner must produce usable library data

#### Improvement 9: Bluetooth routing unproven

**Location:** `app/src/alsa.c:37-54`, `app/src/config.c:16`

**Problem:** Opens `/dev/snd/pcmC0D0p` directly via ioctl, bypassing libasound. This means BlueALSA routing (Bluetooth headphones) doesn't work through the standard plugin path.

**Fix:** Either:
1. Use libasound (`snd_pcm_open`) with device name configurable (allows `bluealsa` as a device), or
2. Keep direct ioctl for internal DAC and add a separate BlueALSA path that writes to the bluealsa PCM device, or
3. For the initial release, mark Bluetooth as unsupported and document it

**Priority:** P2 — internal DAC works, Bluetooth is a future enhancement

#### Improvement 10: Runtime assets are now packaged (verified)

**Status:** Fixed in v1.7.8 — `/usr/share/audiobooks/fonts/msyh.ttf` and `/usr/share/audiobooks/hiby-theme/` are present on the device.

No action needed.

---

## 4. The Most Pragmatic Path to Eric's Goals

### Eric wants:
1. Tap Audiobooks tile → browse books → tap book → resume plays
2. All standalone on device, no PC
3. Per-book resume that survives reboots, music listening, SD card swaps
4. Audiobooks separated from music
5. Preserve all stock music functionality

### Shortest reliable path:

#### Step 1: Fix the touchscreen (TODAY)
- Hard power cycle the R1 (hold power 10-15s, wait 30s, power on)
- Verify touch IRQ count increases when tapping
- If touch works, the existing v1.7.8 firmware should launch the app on tile tap

#### Step 2: Verify end-to-end launch (TODAY, after touch fix)
- Tap the Audiobooks tile
- Verify `/tmp/.r1_audiobook_launch` appears
- Verify `r1_audiobook_app` launches within 2-3 seconds
- Verify the app shows the Titles screen
- Verify Back exits to the launcher

#### Step 3: Fix the destructive scan bug (1-2 days)
- Remove `db_clear_library()` from the normal startup path
- Make library refresh incremental and transactional
- Preserve progress and bookmarks across refreshes
- This is the single most critical software bug

#### Step 4: Fix M4B playback (2-3 days)
- Separate chapter model from track model
- Fix large-file safety (`_FILE_OFFSET_BITS=64`, `fseeko`, 64-bit offsets)
- Test with real M4B files from Eric's library

#### Step 5: Verify on-device playback (1 day)
- Push app and assets via ADB
- Run `--scan-only` to populate the library
- Play an MP3 multi-file book: verify track transitions, seek, pause/resume
- Play an M4B file: verify open, seek, chapter display
- Verify resume after app exit and relaunch
- Verify resume after reboot

#### Step 6: Fix seek/skip (1-2 days)
- Fix `pending_seek_ms` usage in player.c
- Test skip forward/backward from Now Playing

#### Step 7: Basic UX polish (2-3 days)
- Ensure Now Playing shows: cover, title, author, chapter, position, remaining time
- Ensure Home shows: Continue Listening, Titles, Authors, Folders, Refresh
- Ensure Back navigation is consistent

#### Step 8: First standalone-app release (1 day)
- Build firmware package with the fixed app
- Flash, verify, smoke test
- Tag as v1.8.0

**Estimated total time to first usable standalone-app release: 7-10 days of focused work.**

### What we get from Phase 1 that we should keep:

- The **scan-skip patch** (prevents audiobook files from appearing in music library)
- The **resource text patches** (launcher label "Audiobooks", icon)
- The **DB maintenance daemon** (`r1_audiobook_db_maint.c`) for library scanning
- The **resume records** in `/usr/data/audiobooks/resume.d/` (migration source)
- The **catalog files** (`catalog.tsv`, `catalog-books.tsv`) for backward compatibility

### What we should NOT keep from Phase 1:

- The 2,500-line shell resume daemon (replaced by app's built-in resume)
- The framebuffer detection / auto-tap logic
- The M3U playlist-based view system
- The `r1_audiobook_direct_open.c` helper (app handles track selection directly)
- The `r1_audiobook_memscan.c` helper (app owns playback state)
- The explorer marker and title autostart marker patches (app doesn't need them)

---

## 5. Touch IC Fix Assessment

### Current State (Verified Today)

- **hyn_ts IRQ count:** 115 (static — does not increase on physical touch)
- **Touch driver:** `cst8xx_touch.ko` (Hynitron driver adapted for CST8xx)
- **I2C bus:** I2C0 (IRQ 69, count 42216 — active, so I2C bus itself works)
- **Input devices:** `event1` registered as `hyn_ts` with `EV=ABS`

### Root Cause (from `bob-touch-analysis.md`)

The CST8xx touch IC is in a corrupted power state. The IC asserts IRQ and fills data registers, but the data format doesn't match what the driver's `hyn_read_touchdata()` expects. The driver reads the data, finds no valid touch points, and returns without calling `input_event()`. This is a kernel-level issue — the rootfs flash didn't change the driver.

Warm reboots don't fix it because the PMIC doesn't cut power to I2C peripherals on soft reboot. The IC maintains its bad state across reboots.

### Fix Options (Ranked)

| Option | Likelihood | Effort | Risk | Action |
|--------|-----------|--------|------|--------|
| 1. Hard power cycle (10-15s hold) | ~70% | Trivial | None | **Do this first** |
| 2. GPIO hardware reset via ADB | ~55% | Low | Low | Fallback if #1 fails |
| 3. I2C register dump during touch | ~50% diagnostic | Low-Med | Low | Fallback if #2 fails |
| 4. Driver disassembly | ~40% fix, HIGH diagnostic | Med-High | None (read-only) | Last resort |
| 5. Revert to stock firmware | HIGH for restoring touch | Medium | Low | Nuclear option |

### Is This Permanent?

**Almost certainly not permanent.** The touch IC worked before the flash sequence. The flash process triggered a power-state issue that a full power cycle should resolve. This is a known class of issue with CST8xx touch controllers on embedded MIPS platforms.

### Can We Prevent It on Future Flashes?

Potentially — by adding a touch IC reset to the boot sequence:

1. **GPIO reset on boot:** Add to an init script (`S89touch_reset.sh` or similar):
   ```sh
   #!/bin/sh
   # Reset CST8xx touch IC on boot
   # Toggle GPIO PA17 (touch reset pin)
   echo 17 > /sys/class/gpio/export 2>/dev/null
   echo out > /sys/class/gpio/gpio17/direction 2>/dev/null
   echo 0 > /sys/class/gpio/gpio17/value
   usleep 100000  # 100ms
   echo 1 > /sys/class/gpio/gpio17/value
   usleep 300000  # 300ms
   ```

2. **Module reload after reset:** `rmmod cst8xx_touch && insmod /module_driver/cst8xx_touch.ko`

This would force a hardware reset of the touch IC on every boot, preventing the corrupted state from persisting. However, the exact GPIO pin number needs verification from the device tree or schematic.

---

## 6. Firmware Update Strategy

### Current Approach

- Flash directly to `mtd1` (kernel) and `mtd2` (rootfs) via `nandwrite`/`flashcp`
- The OTA dual-bank system is not used because `rootfs2` (`mtd4`) is only 24MB — too small for the 37.6MB rootfs
- SD card mounted at `/usr/data/mnt/sd_0` (fixed from `/mnt/sd_0` in v1.7.8)

### Assessment

**The direct-flash approach is sustainable and correct for this project.** The OTA dual-bank system was designed for stock HiBy firmware updates, not for third-party mods that are larger than the secondary bank.

**However, the recovery path must be clear:**

1. **Stock recovery:** Always keep a known-good stock 1.6 `r1.upt` on the SD card. If anything goes wrong, flash stock via the normal R1 update flow.
2. **ADB recovery:** If the device boots but touch is dead, ADB is the recovery path. The v1.7.8 firmware enables ADB unconditionally on boot (`S90adb`), which is correct.
3. **Black screen recovery:** If a flash produces a black screen, the R1's hardware recovery mode (hold a button combo during boot) can flash from SD card. This is a stock HiBy feature.

### Recommendations

1. **Keep direct flash to mtd1/mtd2.** It's the most reliable path for firmware that doesn't fit in rootfs2.
2. **Always verify pre-flash hashes.** The `patch_hiby_player.py` script verifies MD5/SHA256 of the stock binary before patching — this must never be skipped.
3. **Always verify post-flash state.** Run the verification script after every flash.
4. **Keep ADB enabled on boot.** It's the only recovery path when touch is dead. The `S90adb` unconditional start is correct for dev builds.
5. **For public releases,** consider making ADB opt-in (via a flag file on SD card) to avoid the security exposure. But for Eric's personal device, unconditional ADB is fine.
6. **Add a touch IC reset to the boot sequence.** This prevents the touch corruption issue from recurring on future flashes.

---

## 7. Concrete Action Items

### Immediate (Today)

1. **Hard power cycle the R1** — Hold power button 10-15 seconds, wait 30 seconds, power on. This should reset the CST8xx touch IC.

2. **Verify touch works** — After power cycle, check `cat /proc/interrupts | grep hyn_ts` before and after tapping the screen. The count should increase.

3. **Test the Audiobooks tile** — If touch works, tap the Audiobooks tile. Verify:
   - `/tmp/.r1_audiobook_launch` appears (`adb shell ls /tmp/.r1_audiobook_launch`)
   - `r1_audiobook_app` launches (`adb shell pidof r1_audiobook_app`)
   - App shows the Titles screen
   - Back exits to launcher

### Short-Term (1-3 days)

4. **Fix destructive scan bug** in `app/src/scanner.c` and `app/src/db.c`:
   - Remove `db_clear_library()` from normal refresh path
   - Make `library_refresh()` incremental: scan to temp tables, diff, apply changes in one transaction
   - Never delete `progress` or `bookmarks` rows during scan
   - File: `app/src/scanner.c` lines 305-309, `app/src/db.c` lines 63-65

5. **Fix M4B chapter/track separation** in `app/src/db.c`:
   - `db_query_tracks_for_playback()` must return only playable file tracks (M4B = one track)
   - `db_query_chapters_for_display()` returns chapter metadata for display
   - Player seeks within the single M4B track for chapter navigation
   - File: `app/src/db.c` lines 366-404

6. **Fix seek implementation** in `app/src/player.c`:
   - Use `pending_seek_ms` as the seek target, not current position
   - File: `app/src/player.c` lines 319-327

### Medium-Term (3-7 days)

7. **Add `_FILE_OFFSET_BITS=64`** to `app/build.sh` compile flags for large M4B file support.

8. **Replace `fseek()`/`ftell()` with `fseeko()`/`ftello()`** in `app/src/m4b_decoder.c` — lines 49, 194, 203-207, 363.

9. **Change ADTS offset storage** from `uint32_t` to `uint64_t` in `app/src/m4b_decoder.c` line 64.

10. **Implement basic scanner improvements** in `app/src/scanner.c`:
    - Parse `book.json` sidecar files
    - Case-insensitive extension matching (`.MP3`, `.M4B`, `.FLAC`)
    - Handle direct files under `/Audiobooks/` as separate books
    - Use `r1_audiobook_db_maint.c` logic or call it as a helper

11. **Add touch IC reset to boot sequence:**
    - Create `S89touch_reset.sh` init script
    - Toggle GPIO reset pin on boot
    - Reload touch driver module
    - File: `tools/firmware_overlay.json` (add new init script)

12. **Implement skip forward/backward** in `app/src/player.c`:
    - 15s backward, 30s forward (configurable)
    - Use the fixed seek mechanism

### Long-Term (1-2 weeks)

13. **Implement playback speed** with a lightweight time-stretch:
    - Simple approach: sample skip/duplicate (changes pitch but works on MIPS)
    - Better approach: WSOLA overlap-add (preserves pitch)
    - File: `app/src/player.c`, new `app/src/stretch.c`

14. **Implement smart rewind** per spec tiers:
    - < 5min: 0s, 5-60min: 5s, 1-24h: 10s, > 24h: 15-20s, reboot: 10-20s
    - File: `app/src/resume.c` lines 57-63

15. **Bluetooth routing** — either use libasound or add a BlueALSA output path:
    - File: `app/src/alsa.c`, `app/src/config.c`

16. **Pin/vendor FAAD2** to a specific commit in `app/build.sh` instead of downloading `master.zip`.

17. **Add on-device smoke tests** — extend the test suite to verify:
    - App launches and shows titles
    - MP3 book plays, pauses, resumes
    - M4B book opens and seeks
    - Progress survives app exit and relaunch
    - Progress survives reboot

---

## 8. Risk Assessment and Recovery Plan

### Risk Matrix

| Risk | Severity | Likelihood | Mitigation | Recovery |
|------|----------|-----------|-----------|----------|
| Touch IC permanently dead | Critical | Low (~10%) | Hard power cycle; GPIO reset; add boot reset script | Use physical button combo fallback or auto-launch app on boot |
| App crashes on launch | High | Medium | Verify assets installed, DB exists, framebuffer available | Wrapper relaunches hiby_player; user can retry |
| Destructive scan erases progress | Critical | High (current bug exists) | Fix scan to be incremental/transactional | Restore from `resume.d/` JSON records (migration source) |
| M4B playback fails | High | Medium | Test with real files; fix chapter/track model | Fall back to MP3-only support initially |
| ALSA audio doesn't work | High | Low | Direct ioctl path is simple and proven on X1600 | Debug ALSA hw params; fall back to stock player for audio |
| Memory pressure (OOM) | Medium | Low (app is 2.5MB static) | Keep cover cache small; don't load full font into RAM | Reduce cover thumbnail size; lazy-load font subsets |
| Binary patch breaks on stock update | High | N/A (locked to stock 1.6) | Patcher verifies MD5/SHA256 before patching | Never update stock firmware; keep stock 1.6 base |
| Wrapper script bug causes boot loop | High | Low | Crash counter reboots after 5 failures | Boot into recovery with stock `r1.upt` on SD card |
| Touch IC corruption recurs after flash | Medium | Medium | Add boot-time GPIO reset of touch IC | Hard power cycle after each flash |

### Recovery Plan

#### If the standalone app approach fails completely:

1. **Revert to Phase 1 v1.7.0A** — the last working binary-patching release. It has 17 releases of testing and works with framebuffer-based auto-tap.
2. **Keep the scan-skip patch and resource patches** — those are compatible with Phase 1.
3. **Use the old shell resume daemon** — it's fragile but battle-tested.
4. **Accept the limitations** — track-list flash, timing-based resume, no M4B chapter support.

#### If the touch IC never works again:

1. **Use ADB tap injection** (`r1_adb_control.py`) as the primary input method (not practical for daily use)
2. **Physical button combo** to launch the app (requires a new binary patch)
3. **Auto-launch app on boot** — skip hiby_player entirely, use the R1 as a dedicated audiobook player
4. **Replace the touch IC** (hardware repair — requires opening the device)

#### If a flash produces a black screen:

1. Hold the R1 recovery button combo during boot
2. Flash stock 1.6 `r1.upt` from SD card
3. Re-flash the audiobook firmware after verifying stock works

#### If progress is lost:

1. Check `/usr/data/audiobooks/resume.d/` for JSON resume records
2. These are written by the app on every save and can be used to restore progress
3. The catalog files (`catalog.tsv`, `catalog-books.tsv`) can rebuild the library

---

## 9. Summary Assessment

| Question | Answer |
|----------|--------|
| Continue standalone app or go back to Phase 1? | **Continue standalone app.** Phase 1 is architecturally fragile. Phase 2 is correct and closer to working than it appears. |
| How to launch the app? | **Existing flag-file mechanism is verified working.** The touch IC is the blocker, not the software. Hard power cycle should fix it. |
| What makes Phase 2 production-quality? | **Fix destructive scan, fix M4B chapters, fix seek, implement skip controls.** Everything else is polish. |
| Most pragmatic path? | **Fix touch → verify launch → fix destructive scan → fix M4B → basic UX → release v1.8.0.** 7-10 days. |
| Touch IC fix? | **Hard power cycle first.** If that fails, GPIO reset. If that fails, driver analysis. Almost certainly not permanent. |
| Firmware update strategy? | **Direct flash to mtd1/mtd2 is correct.** Keep stock 1.6 `r1.upt` for recovery. Add touch IC reset to boot sequence. |
| Risk? | **Primary risk is the touch IC, which is likely fixable.** Secondary risk is the destructive scan bug, which is fixable in code. Recovery path is always: revert to stock, then re-flash. |

---

## 10. Confidence Assessment

| Area | Confidence | Reasoning |
|------|-----------|-----------|
| Callback patch correctness | **Very High** | Verified in runtime memory; dispatch chain traced; code cave disassembled |
| Flag-file mechanism | **Very High** | Tested live: manual flag creation launched the app successfully |
| Touch IC diagnosis | **High** | IRQ count static, I2C bus active, consistent with corrupted power state |
| Standalone app architecture | **High** | Correct separation of concerns; app owns playback, UI, and resume |
| App launch readiness | **High** | App binary (2.5MB) is installed, assets are present, app runs |
| App playback correctness | **Medium** | ALSA path is simple but untested with real audio on device; M4B has bugs |
| App resume correctness | **Medium-Low** | Destructive scan bug is critical; seek is broken; smart rewind doesn't match spec |
| Overall project direction | **High** | Phase 2 is the right path. The blockers are implementation bugs, not architecture. |

---

*End of holistic re-evaluation.*