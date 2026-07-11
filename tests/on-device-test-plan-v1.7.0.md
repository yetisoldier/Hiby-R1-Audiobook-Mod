# HiBy R1 v1.7.0 On-Device Validation Plan

Device: HiBy R1
Firmware: HiBy R1 Audiobook Firmware Mod v1.7.0
Target serial: `ingenic_2233`
Repository: `/home/yetisoldier/projects/hiby-r1-codex/`

## Purpose

Validate the flashed firmware on the actual device, not just in code.
This plan is designed so Jarvis can execute the checks step by step with:

- `adb` shell commands
- `tools/r1_adb_control.py`
- daemon log inspection
- screenshot evidence

The plan is intentionally adversarial. Assume the first pass will expose defects.

## Scope

Cover all of the following:

- Music playback
- Audiobook hub navigation
- Audiobook sorting in Titles, Authors, and Series
- Audiobook/Music separation
- Resume save/restore
- Auto-tap behavior
- Music -> Audiobook transition
- Audiobook -> Music transition
- C daemon health
- Edge cases:
  - single-track `m4b`
  - very long books with 100+ tracks
  - empty audiobook folders
  - special characters in names
  - rapid back-and-forth switching
  - sleep/wake during playback
  - Bluetooth playback
  - SD card hot-swap, if supported by the device state

## Known Device Paths

- Daemon log: `/usr/data/audiobooks/resume-daemon.log`
- Daemon config: `/usr/data/audiobooks/resume-daemon.conf`
- Resume files: `/usr/data/audiobooks/resume.d/*.json`
- Catalog files:
  - `/usr/data/audiobooks/catalog.tsv`
  - `/usr/data/audiobooks/catalog-books.tsv`
  - `/usr/data/audiobooks/catalog-view-title.tsv`
  - `/usr/data/audiobooks/catalog-view-author.tsv`
  - `/usr/data/audiobooks/catalog-view-series.tsv`

## Evidence Rules

For every test, capture at least one of:

- screenshot
- log snippet
- shell output
- resume JSON file
- process list

If a test is only inferred from source code or assumptions, mark it inconclusive.

## Device Control Notes

`tools/r1_adb_control.py` does not take a device serial directly. Use one of these approaches:

- keep only `ingenic_2233` connected
- or point `--adb` at a wrapper that resolves to `adb -s ingenic_2233`

Use raw `adb -s ingenic_2233 shell ...` for filesystem, process, and log checks.

Recommended screenshot/classification commands:

```bash
python3 tools/r1_adb_control.py classify --adb <ADB_BIN> --label <label>
python3 tools/r1_adb_control.py screenshot --adb <ADB_BIN> --label <label> --output <path>
python3 tools/r1_adb_control.py preset --adb <ADB_BIN> main-audiobooks
python3 tools/r1_adb_control.py preset --adb <ADB_BIN> main-music
python3 tools/r1_adb_control.py row --adb <ADB_BIN> 1 --kind title
python3 tools/r1_adb_control.py row --adb <ADB_BIN> 1 --kind list
python3 tools/r1_adb_control.py back --adb <ADB_BIN>
python3 tools/r1_adb_control.py key --adb <ADB_BIN> playpause
```

Recommended shell checks:

```bash
adb -s ingenic_2233 shell tail -n 80 /usr/data/audiobooks/resume-daemon.log
adb -s ingenic_2233 shell ps | grep -E 'r1_audiobook_resume_daemon|r1_audiobook_db_watch|hiby_player'
adb -s ingenic_2233 shell ls -la /usr/data/audiobooks/resume.d/
adb -s ingenic_2233 shell cat /usr/data/audiobooks/resume-daemon.conf
```

## Test Data Requirements

Before starting, confirm the media library contains at least one representative example for each case below. If a fixture is missing, record the test as `SKIP` with the missing fixture noted.

- Normal multi-track audiobook
- Music-only album or track
- Single-track `m4b` audiobook
- Very long audiobook with 100+ tracks
- Empty folder under `/Audiobooks`
- Book/folder names containing special characters
- Bluetooth audio output path available for pairing
- SD card hot-swap path available, if the device hardware/setup supports it

Suggested naming conventions for fixtures:

- `Audiobooks/Smoke/Normal Book/`
- `Audiobooks/Edge/Single Track.m4b`
- `Audiobooks/Edge/Long Book 100+ Tracks/`
- `Audiobooks/Edge/Empty Folder/`
- `Audiobooks/Edge/Special !@#$ Characters/`
- `Music/Reference Album/`

## Pass/Fail Criteria

Pass only when:

- the expected screen appears
- playback state matches the action
- daemon processes remain alive
- logs do not show crashes or unintended activity
- resume state is written and restored correctly
- separation rules hold in both directions
- edge cases behave as documented

Fail when:

- playback does not start or stops unexpectedly
- the wrong screen opens
- audiobooks leak into Music or Music leaks into Audiobooks
- resume position is wrong or missing
- daemon logs show crashes, segfaults, repeated restarts, or malformed state
- a listed edge case breaks browsing, playback, or resume

## Quick Smoke

Target runtime: about 5 minutes.

Purpose: prove the device is generally healthy after flash.

### Smoke 1: Daemon health

1. Check processes.
   ```bash
   adb -s ingenic_2233 shell ps | grep -E 'r1_audiobook_resume_daemon|r1_audiobook_db_watch|hiby_player'
   ```
2. Check the daemon log tail.
   ```bash
   adb -s ingenic_2233 shell tail -n 40 /usr/data/audiobooks/resume-daemon.log
   ```

Expected:

- `hiby_player` is running
- `r1_audiobook_resume_daemon` is running
- `r1_audiobook_db_watch` is running if the release uses it
- no crash, segfault, or repeated restart messages

Pass criteria:

- all required processes are present
- log tail is readable and shows normal startup/steady-state entries

### Smoke 2: Open Audiobooks

1. Capture launcher state.
   ```bash
   python3 tools/r1_adb_control.py classify --adb <ADB_BIN> --label smoke-launcher
   ```
2. Open Audiobooks.
   ```bash
   python3 tools/r1_adb_control.py preset --adb <ADB_BIN> main-audiobooks
   ```
3. Capture the opened screen.
   ```bash
   python3 tools/r1_adb_control.py classify --adb <ADB_BIN> --label smoke-audiobooks-open
   ```

Expected:

- launcher transitions to an audiobook list screen
- the hub does not open Music, Settings, or a crash/blank screen

Pass criteria:

- screen classification is `list`
- screenshot visually shows the audiobook list

### Smoke 3: Start playback

1. Tap row 1 in the audiobook list.
   ```bash
   python3 tools/r1_adb_control.py row --adb <ADB_BIN> 1 --kind title
   ```
2. Wait a few seconds.
3. Classify the screen.
   ```bash
   python3 tools/r1_adb_control.py classify --adb <ADB_BIN> --label smoke-now-playing
   ```

Expected:

- playback starts
- screen becomes `now-playing`

Pass criteria:

- Now Playing screen is visible
- audio is playing or has visibly entered paused/playing state correctly

### Smoke 4: Resume sanity

1. Play for at least 20 seconds.
2. Back out once or twice to leave the current book.
3. Re-enter the same title.
4. Confirm the resume JSON exists.
   ```bash
   adb -s ingenic_2233 shell ls -t /usr/data/audiobooks/resume.d/*.json
   ```
5. Inspect the log for save/restore activity.
   ```bash
   adb -s ingenic_2233 shell tail -n 60 /usr/data/audiobooks/resume-daemon.log
   ```

Expected:

- a resume record is written
- re-entering the same book restores playback instead of always starting from zero

Pass criteria:

- the resume file exists
- the restore path is visible in logs or screen state
- the device returns to the same book cleanly

## Full Validation

Target runtime: 30 to 60 minutes.

Execute in order unless a test explicitly depends on a prior failure.
If a test fails, stop, capture evidence, and note whether the next test is still meaningful.

### FV-01 Device and daemon baseline

Purpose:

- confirm the device is stable before UI testing

Steps:

1. Check ADB connectivity.
   ```bash
   adb -s ingenic_2233 devices -l
   ```
2. Check process health.
   ```bash
   adb -s ingenic_2233 shell ps | grep -E 'r1_audiobook_resume_daemon|r1_audiobook_db_watch|hiby_player'
   ```
3. Capture current screen.
   ```bash
   python3 tools/r1_adb_control.py classify --adb <ADB_BIN> --label baseline
   ```
4. Read the daemon log tail.
   ```bash
   adb -s ingenic_2233 shell tail -n 80 /usr/data/audiobooks/resume-daemon.log
   ```

Expected:

- the serial is authorized and responsive
- required processes are alive
- log has no crash indicators

Pass criteria:

- no missing process
- no restart loop
- no log corruption

### FV-02 Music playback basic

Purpose:

- verify normal music still works

Steps:

1. Open Music.
   ```bash
   python3 tools/r1_adb_control.py preset --adb <ADB_BIN> main-music
   ```
2. If a list appears, tap row 1.
   ```bash
   python3 tools/r1_adb_control.py row --adb <ADB_BIN> 1 --kind list
   ```
3. Capture a screenshot after playback begins.
   ```bash
   python3 tools/r1_adb_control.py screenshot --adb <ADB_BIN> --label music-playback
   ```
4. Pause.
   ```bash
   python3 tools/r1_adb_control.py key --adb <ADB_BIN> playpause
   ```
5. Seek if a now-playing screen is visible.
   ```bash
   python3 tools/r1_adb_control.py seek --adb <ADB_BIN> 50
   ```
6. Skip forward/back.
   ```bash
   python3 tools/r1_adb_control.py key --adb <ADB_BIN> next
   python3 tools/r1_adb_control.py key --adb <ADB_BIN> prev
   ```

Expected:

- Music opens normally
- a track can be started from the list
- pause/play toggles without leaving Music
- skip and seek do not freeze the UI

Pass criteria:

- Music playback is reachable and controllable
- no crash, reboot, or blank screen

### FV-03 Music playlists and browsing

Purpose:

- verify browsing and playlist entry points behave

Steps:

1. Browse a music list.
2. Open a second row if available.
3. Confirm the screen remains music-scoped.
4. Return to the launcher.

Expected:

- music browsing does not redirect to Audiobooks
- back navigation returns to launcher without a crash

Pass criteria:

- the music path is intact
- no audiobook controls or views appear in Music

### FV-04 Audiobook hub entry

Purpose:

- verify the hub opens correctly from the launcher

Steps:

1. Return to launcher.
2. Open Audiobooks.
3. Capture screenshot.

Expected:

- the Audiobooks hub opens to the native audiobook list surface
- it does not show Music albums or a crash screen

Pass criteria:

- screen classification is `list`
- screenshot shows the audiobook UI

### FV-05 Titles view

Purpose:

- verify Titles view opens and is usable

Steps:

1. Open Audiobooks.
2. Enter Titles.
3. Capture a screenshot of the top of the list.
4. Scroll or move through the list if more than five rows exist.
5. Tap a title row and observe the next state.

Expected:

- Titles opens successfully
- the list is populated
- tapping a row enters the expected book flow

Pass criteria:

- Titles is reachable and navigable
- row taps do not dead-end or crash

### FV-06 Authors view

Purpose:

- verify Authors view opens and contains sorted book rows

Steps:

1. Open Audiobooks.
2. Enter Authors.
3. Capture a screenshot of the first visible rows.
4. Scroll through the list.
5. Tap at least one author entry and validate the follow-up list or book open behavior.

Expected:

- Authors opens successfully
- rows are grouped and sorted by author as intended
- selecting an author does not break back navigation

Pass criteria:

- Authors is reachable
- list content matches author grouping

### FV-07 Series view

Purpose:

- verify Series view opens and only contains series-backed books

Steps:

1. Open Audiobooks.
2. Enter Series.
3. Capture a screenshot.
4. Confirm standalone books are absent.
5. Tap a series entry.

Expected:

- Series opens successfully
- books with series metadata are listed
- standalone books are not forced into fake series entries

Pass criteria:

- the series view is populated only where expected
- selecting a series entry behaves normally

### FV-08 Audiobook sorting order

Purpose:

- verify sort order in each audiobook view

Steps:

1. Open Titles and inspect the visible order.
2. Open Authors and inspect the visible order.
3. Open Series and inspect the visible order.
4. Record the first visible and last visible entries in each view.
5. If alphabetical jump/rail is present, verify it matches the displayed ordering.

Expected:

- Titles are sorted by title
- Authors are sorted by author
- Series are sorted by series metadata
- sort order should be stable after back/enter cycles and refreshes

Pass criteria:

- visible ordering is correct in every view
- order does not reshuffle between repeated opens

Notes:

- If the device has fewer fixtures than needed to prove ordering robustly, add test data before re-running.

### FV-09 Audiobook and Music separation

Purpose:

- verify audiobooks do not leak into Music and music does not appear in Audiobooks

Steps:

1. Open Music and inspect the top-level views.
2. Search/browse the music catalog enough to confirm no audiobook folders or rows are visible.
3. Open Audiobooks and inspect the hub views.
4. Confirm music albums/tracks are not present in audiobook catalogs.
5. Check the release-state verification script if needed.
   ```bash
   python3 tools/check_audiobook_release_state.py /tmp/pulled-db --expect-audiobooks
   ```
   Use the live DB path or a pulled copy from the device as appropriate.

Expected:

- Music is free of audiobook leakage
- Audiobooks contain only audiobook content
- search/album/genre leakage stays at zero

Pass criteria:

- no audiobook rows in Music-facing tables
- no Music rows in Audiobook-facing views

### FV-10 Resume save and restore

Purpose:

- verify save records are written and later restored

Steps:

1. Start an audiobook.
2. Play long enough to exceed the save threshold.
3. Back out of the book.
4. Re-enter the same book.
5. Inspect the newest resume JSON.
   ```bash
   adb -s ingenic_2233 shell ls -t /usr/data/audiobooks/resume.d/*.json | head -1
   adb -s ingenic_2233 shell cat <newest-file>
   ```
6. Check the position, track, and path fields.

Expected:

- a resume file exists
- position is positive for multi-track playback
- re-entering the same book restores to the saved point or saved track

Pass criteria:

- the restored position is close to the saved position
- the resume record path matches the opened audiobook

### FV-11 Track restoration

Purpose:

- verify the daemon restores the correct track, not just the correct book

Steps:

1. Use a multi-track audiobook.
2. Advance to a non-first track.
3. Leave the book.
4. Re-enter it.
5. Confirm the daemon restores the same track or the correct nearby track.
6. Inspect logs for track-restore or direct-open evidence.

Expected:

- track index persists
- the restore path does not always snap to track 1

Pass criteria:

- the reopened book starts on the expected track
- log evidence shows a restore action rather than a fresh start only

### FV-12 Auto-tap on idle entry

Purpose:

- verify auto-tap fires when entering an audiobook from idle

Steps:

1. Return to launcher and let the device idle briefly.
2. Open Audiobooks.
3. Select a title that should trigger auto-tap.
4. Observe whether the track list is skipped.
5. Check the log for auto-tap or direct-open evidence.

Expected:

- playback starts without manual track selection when auto-tap is functioning
- the correct track is targeted

Pass criteria:

- the book lands in Now Playing directly, or
- the log shows a valid auto-tap followed by the correct playback target

Fail criteria:

- the track list appears and never advances
- the wrong track starts

### FV-13 Music -> Audiobook transition

Purpose:

- verify switching away from Music into Audiobooks restores correctly

Steps:

1. Start music playback.
2. Leave Music.
3. Open Audiobooks.
4. Re-enter the same audiobook.
5. Inspect resume and log state.

Expected:

- leaving music does not break audiobook restore
- audiobook resume still works after Music was active

Pass criteria:

- audiobook resumes normally
- music playback does not leave the daemon in a bad state

### FV-14 Audiobook -> Music transition

Purpose:

- verify leaving audiobook playback for music and then returning works

Steps:

1. Start audiobook playback.
2. Open Music and start a track.
3. Let music play briefly.
4. Return to Audiobooks.
5. Reopen the same book.

Expected:

- music plays normally after audiobook playback
- audiobook resumes after music without corruption

Pass criteria:

- the audiobook restores after the round trip
- no stale-book or stale-root symptoms appear

### FV-15 C daemon health during active playback

Purpose:

- validate daemon stability while the player is busy

Steps:

1. Keep an audiobook playing for 30+ seconds.
2. Tail the daemon log.
   ```bash
   adb -s ingenic_2233 shell tail -n 120 /usr/data/audiobooks/resume-daemon.log
   ```
3. Check for restarts or signal errors.
   ```bash
   adb -s ingenic_2233 shell ps | grep -E 'r1_audiobook_resume_daemon'
   ```
4. Optionally inspect the daemon config for enabled features.
   ```bash
   adb -s ingenic_2233 shell cat /usr/data/audiobooks/resume-daemon.conf
   ```

Expected:

- daemon keeps running
- logging continues normally
- no crash, hang, or spam loop

Pass criteria:

- process remains alive
- log cadence looks normal
- no repeated startup banner or restart markers

### FV-16 Rapid back-and-forth switching

Purpose:

- stress the transition logic

Steps:

1. Alternate between Music and Audiobooks at least 5 times.
2. On every return to Audiobooks, select the same book.
3. On every return to Music, start the same music track if possible.
4. Capture a screenshot after each switch or every second switch.
5. Tail the log for stale-root or crash indicators.

Expected:

- the UI stays responsive
- no lost state after repeated switching
- no stale resume data overwrites

Pass criteria:

- all transitions succeed
- no unexpected book resets
- no crash or reboot

### FV-17 Device sleep/wake during audiobook playback

Purpose:

- verify resume state survives suspend/resume

Steps:

1. Start audiobook playback.
2. Put the device to sleep.
3. Wake it after a short interval.
4. Observe whether playback resumes or remains in the correct paused state.
5. Re-enter the book if needed.
6. Inspect the resume JSON and log tail.

Expected:

- sleep/wake does not corrupt playback state
- resume remains valid after wake

Pass criteria:

- the book reopens to the correct point
- daemon continues running after wake

### FV-18 Bluetooth playback

Purpose:

- validate the audio path when Bluetooth output is active

Steps:

1. Pair/connect Bluetooth audio output if not already connected.
2. Start music playback over Bluetooth.
3. Confirm the player remains responsive.
4. Open Audiobooks and repeat playback.
5. Verify resume behavior after switching outputs.

Expected:

- Bluetooth playback works
- audiobook logic does not break when BT is the active sink

Pass criteria:

- audio plays through BT
- no output switch crashes or UI stalls

### FV-19 SD card hot-swap, if applicable

Purpose:

- verify catalog and resume handling when media disappears and returns

Steps:

1. Confirm the device is using removable SD media.
2. Start from a stable audiobook state.
3. Remove the SD card only if the hardware/setup supports it safely.
4. Wait for the UI and daemon to react.
5. Reinsert the SD card.
6. Confirm the catalog and resume files recover.
7. Tail the log for rebuild or maintenance messages.

Expected:

- the device handles removal without corruption
- reinsertion rebuilds or reuses the catalog cleanly

Pass criteria:

- catalog returns
- no permanent daemon failure
- no malformed resume data

## Edge-Case Specific Checks

Run these after the main flow if the fixture exists.

### EC-01 Single-track `m4b`

Purpose:

- verify a one-file book does not require track-index logic to succeed

Checks:

- open the book
- confirm resume record is created
- confirm position may legitimately stay at `0` or a minimal value until playback commits
- confirm back/return behavior is sane

Pass criteria:

- playback works
- resume file is valid
- no false failure because the book only has one track

### EC-02 Very long audiobook, 100+ tracks

Purpose:

- validate scalability and track-selection logic

Checks:

- open the long book
- scroll deep into the list
- select a far-down track
- leave and re-enter
- verify restore does not degrade or time out

Pass criteria:

- the UI remains responsive
- the correct track still restores
- log volume remains sane

### EC-03 Empty audiobook folder

Purpose:

- validate empty-state behavior

Checks:

- open the empty folder or empty view path
- confirm the UI does not crash
- confirm the user gets a meaningful empty state

Pass criteria:

- no crash or hang
- empty content is handled gracefully

### EC-04 Special characters in names

Purpose:

- verify filesystem and UI quoting do not break on punctuation/Unicode-like characters

Checks:

- open the book/folder
- confirm the title renders
- confirm resume file path handling is correct
- confirm back navigation still works

Pass criteria:

- special characters do not break browsing, save, or restore

### EC-05 Rapid repeated resume restoration

Purpose:

- stress repeated save/restore cycles

Checks:

- enter a book
- leave and re-enter it several times quickly
- confirm the same book resumes consistently

Pass criteria:

- no stale bookmark overwrite
- no oscillation between books

## Recommended Execution Order

1. FV-01 Device and daemon baseline
2. Quick Smoke
3. FV-02 Music playback basic
4. FV-04 Audiobook hub entry
5. FV-05 Titles view
6. FV-06 Authors view
7. FV-07 Series view
8. FV-08 Audiobook sorting order
9. FV-09 Audiobook and Music separation
10. FV-10 Resume save and restore
11. FV-11 Track restoration
12. FV-12 Auto-tap on idle entry
13. FV-13 Music -> Audiobook transition
14. FV-14 Audiobook -> Music transition
15. FV-15 C daemon health during active playback
16. FV-16 Rapid back-and-forth switching
17. FV-17 Device sleep/wake during audiobook playback
18. FV-18 Bluetooth playback
19. FV-19 SD card hot-swap, if applicable
20. EC-01 to EC-05, fixture-dependent

## Recording Template

Use this template when recording each test result:

- Test ID:
- Fixture:
- Steps run:
- Expected:
- Actual:
- Status:
- Evidence:
- Notes:

## Useful Automation Baseline

The automated smoke/full suite is a good baseline, but it does not replace the manual edge-case pass.

Run these first if the device is stable:

```bash
python3 tests/test_suite.py --suite smoke --json-report work/test-smoke.json
python3 tests/test_suite.py --suite full --json-report work/test-full.json
```

If the automated suite passes, continue with the edge-case and transition checks above.
If it fails, use the screenshots, logs, and JSON report to isolate the first regression before continuing.
