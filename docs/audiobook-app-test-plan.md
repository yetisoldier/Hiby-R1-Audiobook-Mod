# HiBy R1 Audiobook App — Formal Test Plan

## Purpose

Every check in this plan must produce a PASS or FAIL with concrete evidence.
"I read it and it looks fine" is not acceptable. Each check requires either:
- A command output showing the expected result
- A code snippet proving the behavior
- A file inspection showing the expected content

## Phase 1: Build Verification

### 1.1 Clean compile
**Command:** `rm -f build/r1_audiobook_app && sh app/build.sh 2>&1`
**Expected:** Zero errors, zero warnings
**Evidence:** Full compiler output

### 1.2 Binary type verification
**Command:** `file build/r1_audiobook_app`
**Expected:** `ELF 32-bit LSB executable, MIPS, MIPS32 rel2 version 1 (SYSV), statically linked, stripped`
**Evidence:** file output

### 1.3 Binary size check
**Command:** `ls -la build/r1_audiobook_app`
**Expected:** Under 5 MB (5,242,880 bytes)
**Evidence:** ls output with byte count

### 1.4 Binary architecture check
**Command:** `readelf -h build/r1_audiobook_app | grep -E 'Machine|Class|Data'`
**Expected:** MIPS, ELF32, little-endian
**Evidence:** readelf output

### 1.5 No shared library dependencies (static check)
**Command:** `readelf -d build/r1_audiobook_app 2>/dev/null | grep NEEDED`
**Expected:** No output (fully static, no NEEDED entries)
**Evidence:** readelf output (empty = pass)

## Phase 2: Source Code Static Analysis

### 2.1 No strncpy without NUL termination
**Command:** `grep -n "strncpy" app/src/*.c | grep -v "sizeof.*- 1.*=.*'\\\\0'" | grep -v "//.*strncpy"`
**Then verify:** Each strncpy call is followed by explicit `dest[...] = '\0';` within 2 lines
**Expected:** Every strncpy has NUL termination
**Evidence:** grep output + manual verification of each match

### 2.2 No buffer overflow risks
**Check:** All fixed-size buffers have bounds-checked writes (snprintf, not sprintf; strncat with size limit; memcpy with size check)
**Command:** `grep -n "sprintf\|strcat\|gets\|strcpy(" app/src/*.c`
**Expected:** Zero matches (only snprintf, strncat, strncpy with bounds)
**Evidence:** grep output

### 2.3 No null pointer dereference patterns
**Check:** Every pointer dereference is preceded by a null check OR the pointer is guaranteed non-null by construction
**Focus areas:** player.c (queue items), db.c (sqlite3_stmt results), ui.c (book list items), scanner.c (dirent results)
**Evidence:** Code review with line numbers

### 2.4 No file descriptor leaks
**Check:** Every open() has a matching close() on all code paths (including error paths)
**Focus areas:** alsa.c (pcm fd), fb.c (framebuffer fd), touch.c (input fd), db.c (sqlite handles), ipc.c (socket fd), decoder.c (file handles via codec libs)
**Evidence:** Code review showing open/close pairs

### 2.5 Thread safety in player.c
**Check:** All shared state in player.c is accessed under pthread_mutex_lock/unlock
**Focus:** player->state, player->position_ms, player->pending_seek, player->book_loaded, player->want_playing, player->track_changed, player->eof_reached
**Evidence:** Code review showing lock/unlock around each shared state access

### 2.6 No use-after-free
**Check:** No pointer is used after free() or close() in any code path
**Focus:** queue_free → queue items, db close → db queries, decoder_close → decoder reads, alsa_close → alsa writes
**Evidence:** Code review

## Phase 3: Config and Packaging Verification

### 3.1 Config field alignment
**Check:** Every env var exported by the init script maps to a real FIELD() in config.c and a real member in config.h
**Command:** 
```
adb shell "grep 'AUDIOBOOK_' /etc/init.d/S91audiobook_resume.sh" | extract var names
grep 'FIELD(' app/src/config.c | extract env names
```
**Expected:** Every env var in init script exists in config FIELD table
**Evidence:** Cross-reference table

### 3.2 Config defaults are sane
**Check:** Every config default is a reasonable value (not garbage, not 0 where nonzero expected)
**Focus:** position_addr, duration_addr, marker_addr, sample_rate, buffer_size, arm_window_ms, arm_poll_ms
**Evidence:** Config defaults table with sanity check

### 3.3 Wrapper script is a shell script
**Command:** `head -1 tools/r1_audiobook_resume_daemon_wrapper.sh`
**Expected:** `#!/bin/sh`
**Evidence:** head output

### 3.4 Launcher script is a shell script
**Command:** `head -1 tools/r1_audiobook_launch.sh`
**Expected:** `#!/bin/sh`
**Evidence:** head output

### 3.5 Firmware overlay includes app
**Command:** `grep "r1_audiobook_app" tools/firmware_overlay.json`
**Expected:** Entry with target "usr/bin/r1_audiobook_app" and mode "0755"
**Evidence:** grep output

### 3.6 Firmware overlay includes launcher
**Command:** `grep "r1_audiobook_launch" tools/firmware_overlay.json`
**Expected:** Entry with target "usr/bin/r1_audiobook_launch.sh" and mode "0755"
**Evidence:** grep output

### 3.7 UPT file exists and is valid
**Command:** `ls -la r1-audiobooks-1.9.0-audiobook.upt && file r1-audiobooks-1.9.0-audiobook.upt`
**Expected:** File exists, size 40-50 MB, is data (not empty, not corrupt)
**Evidence:** ls + file output

### 3.8 UPT contains the app binary
**Command:** Mount UPT, extract rootfs, verify app binary exists and is MIPS ELF
**Expected:** /usr/bin/r1_audiobook_app present, MIPS ELF, statically linked
**Evidence:** Extraction output + file check

### 3.9 UPT contains the launcher script (not ELF)
**Command:** Extract launcher from UPT, verify it starts with #!/bin/sh
**Expected:** Shell script, not binary
**Evidence:** head output from extracted file

### 3.10 No touch/back_guard/play_mode/ui_seek env vars in init script
**Command:** `grep -E 'TOUCH_|BACK_GUARD|PLAY_MODE|UI_SEEK' /etc/init.d/S91audiobook_resume.sh`
**Expected:** No matches (these were removed from the daemon)
**Evidence:** grep output (empty = pass)

## Phase 4: Spec Compliance Checklist

### 4.1 §3.3 Direct title tap
**Check:** User taps a book title → goes directly to Now Playing (no track list, no .m3u)
**Verify in code:** ui.c touch handler opens book directly, no intermediate screen
**Evidence:** Code path from touch tap to player_open_book with no list view in between

### 4.2 §7.1 Separate progress per book
**Check:** Each book has its own progress record (progress table, book_id as primary key)
**Verify in code:** db.c schema has `progress(book_id PRIMARY KEY REFERENCES books)`
**Evidence:** Schema output

### 4.3 §7.3 Accidental-start protection
**Check:** protected_until_ms is set when opening a book with existing progress, and position saves are suppressed until playback exceeds the protected point
**Verify in code:** ui.c sets protected_until_ms on book open; main.c checks protected_until_ms before saving
**Evidence:** Code review with line numbers

### 4.4 §7.4 Smart rewind tiers
**Check:** resume_smart_rewind_ms() returns correct values:
- < 300s → 0
- 300-3600s → rewind_short_ms (5000)
- 3600-86400s → rewind_medium_ms (10000)
- > 86400s → rewind_long_ms (20000)
- No timestamp → rewind_long_ms (reboot)
**Verify in code:** resume.c function with exact threshold values
**Evidence:** Code review

### 4.5 §8 Completion on natural EOF only
**Check:** Completion requires pos >= dur AND position was not advancing (not a seek)
**Verify in code:** main.c EOF handler checks natural end, not position threshold
**Evidence:** Code review

### 4.6 §8.2 Completed books restart from beginning
**Check:** Opening a completed book sets completed=0 and starts from track 1, position 0
**Verify in code:** ui.c or main.c checks rec.completed on book open
**Evidence:** Code review

### 4.7 §9.1 Book-bounded queue
**Check:** Queue only contains tracks from the current book, stops at end, no crossover to other books or music
**Verify in code:** queue.c enforces book_id boundary; player.c stops at end of last track
**Evidence:** Code review

### 4.8 §9.2 No shuffle/repeat in audiobook mode
**Check:** No shuffle or repeat logic in player.c or queue.c
**Command:** `grep -i "shuffle\|repeat\|random" app/src/player.c app/src/queue.c`
**Expected:** Zero matches
**Evidence:** grep output

### 4.9 §17 Separate audiobook database
**Check:** App uses /usr/data/audiobooks/library.db, NOT usrlocal_media.db
**Command:** `grep "media.db\|usrlocal" app/src/*.c`
**Expected:** Zero matches
**Evidence:** grep output

### 4.10 §18.2 No framebuffer pixel detection
**Check:** Framebuffer is only used for rendering (writing pixels), not for screen detection (reading pixels)
**Command:** `grep -n "fb_read\|mmap.*PROT_READ\|lseek.*fb" app/src/fb.c app/src/ui.c`
**Expected:** fb.c mmap is PROT_READ|PROT_WRITE for rendering only, no pixel-reading detection logic
**Evidence:** Code review

### 4.11 §18.3 Event-driven, not polling
**Check:** Main loop uses touch_poll with timeout (not busy-loop), daemon uses IPC events (not continuous polling)
**Verify in code:** main.c event loop, ipc.c event reception
**Evidence:** Code review

### 4.12 §19 No continuous polling when idle
**Check:** When no book is playing and no touch input, the app sleeps (doesn't burn CPU)
**Verify in code:** main.c has appropriate sleep/poll timeout
**Evidence:** Code review

## Phase 5: M4B/AAC Decoder Verification

### 5.1 FDK-AAC compiled statically
**Command:** `nm build/r1_audiobook_app | grep -i "aacDecoder_Open\|aacDecoder_Decode"`
**Expected:** Symbols present in static binary
**Evidence:** nm output

### 5.2 M4B file extension handling
**Command:** `grep -n "m4b\|m4a\|aac" app/src/decoder.c`
**Expected:** M4B/M4A/AAC file extensions are handled
**Evidence:** grep output

### 5.3 MP4 container parser exists
**Command:** `grep -n "mp4\|atom\|moov\|mdat\|stts\|stsz" app/src/*.c`
**Expected:** MP4 box/atom parsing code exists
**Evidence:** grep output

### 5.4 M4B seek works
**Check:** decoder_seek_ms for M4B finds the right AAC frame in the MP4 container
**Verify in code:** M4B decoder seek implementation
**Evidence:** Code review

### 5.5 M4B duration detection
**Check:** Duration is read from MP4 metadata or computed from AAC frame count
**Verify in code:** Duration calculation in M4B decoder
**Evidence:** Code review

## Phase 6: UI Verification

### 6.1 HiBy font loaded
**Command:** `grep -n "msyh.ttf\|stbtt_InitFont\|stbtt_LoadFont" app/src/font.c`
**Expected:** msyh.ttf is loaded via stb_truetype
**Evidence:** grep output

### 6.2 HiBy button PNGs used
**Command:** `grep -n "btn_play\|btn_pause\|btn_next\|btn_prev" app/src/ui.c`
**Expected:** Now Playing screen uses the actual HiBy PNG button assets
**Evidence:** grep output

### 6.3 Theme colors applied
**Command:** `grep -n "0x1062F2\|0xFFFF\|0xC8DCED\|0xCCE8CF" app/src/ui.c`
**Expected:** HiBy theme colors (blue highlight, white text, light backgrounds) are used
**Evidence:** grep output

### 6.4 Cover art loading
**Command:** `grep -n "stbi_load\|cover.jpg\|folder.jpg\|default_cover" app/src/cover.c app/src/ui.c`
**Expected:** Cover art is loaded via stb_image, with fallback to default cover
**Evidence:** grep output

### 6.5 Swipe-right to Now Playing
**Command:** `grep -n "SWIPE_RIGHT\|swipe_right\|now_playing" app/src/touch.h app/src/touch.c app/src/ui.c`
**Expected:** Swipe right gesture is detected and navigates to Now Playing
**Evidence:** grep output

### 6.6 Mini-player on Home screen
**Command:** `grep -n "mini.player\|now_playing_bar\|mini_bar" app/src/ui.c`
**Expected:** Mini-player bar exists on Home screen when a book is active
**Evidence:** grep output

### 6.7 Screen list completeness
**Command:** `grep "UI_SCREEN_" app/src/ui.h`
**Expected:** HOME, TITLES, NOW_PLAYING, CHAPTERS, FINISHED, AUTHORS, SERIES, FOLDERS, SETTINGS
**Evidence:** grep output

## Phase 7: Device Smoke Test (Before Flash)

### 7.1 Push app to device
**Command:** `adb push build/r1_audiobook_app /usr/data/audiobooks/bin/r1_audiobook_app && adb shell chmod 755 /usr/data/audiobooks/bin/r1_audiobook_app`
**Expected:** File pushed successfully

### 7.2 Run app via ADB
**Command:** `adb shell "/usr/data/audiobooks/bin/r1_audiobook_app --scan-only 2>&1"`
**Expected:** App scans /Audiobooks, creates library.db, exits cleanly

### 7.3 Verify database created
**Command:** `adb shell "ls -la /usr/data/audiobooks/library.db"`
**Expected:** SQLite database file exists with nonzero size

### 7.4 Run app in foreground
**Command:** `adb shell "/usr/data/audiobooks/bin/r1_audiobook_app &" then capture framebuffer`
**Expected:** App renders to framebuffer, UI is visible (not a blank screen)

### 7.5 Verify no hiby_player crash
**Command:** `adb shell "ps | grep hiby_player"`
**Expected:** hiby_player still running (our app didn't crash it)

### 7.6 Kill app cleanly
**Command:** `adb shell "kill \$(pidof r1_audiobook_app)"`
**Expected:** App exits cleanly, no zombie process, framebuffer returns to launcher

### 7.7 M4B playback test
**Command:** Find an .m4b file on the SD card, run app, open the book, verify audio plays
**Expected:** Audio output from the M4B file (not silence, not an error)

## Phase 8: Regression — Stock Music Still Works

### 8.1 Music database intact
**Command:** `adb shell "/usr/data/audiobooks/bin/r1_audiobook_db_maint --verbose"`
**Expected:** Music tracks and audiobook tracks counted correctly

### 8.2 No audiobook leakage in music
**Command:** After running Update Database, verify no audiobook files appear in Music
**Expected:** Audiobooks not in Music Songs, Albums, Artists, or Genres

### 8.3 Stock player still runs
**Command:** `adb shell "ps | grep hiby_player"`
**Expected:** hiby_player running normally, no crashes

## Sign-off

All checks must PASS before staging firmware on device.
Any FAIL requires a fix and re-test of the failed check plus all dependent checks.

QA Agent: ___________
Date: ___________
Verdict: _____ READY TO FLASH  _____ ISSUES FOUND