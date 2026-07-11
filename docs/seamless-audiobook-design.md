# Seamless Audiobook Experience — Architecture Specification

> **Author:** Bob (Principal Software Architect)
> **Audience:** Forge (implementer), Karen (test engineer)
> **Project:** HiBy R1 Audiobook Firmware Mod
> **Date:** 2026-07-10
> **Status:** Design-only. No code in this document.

---

## Executive Summary

This specification defines how to transform the HiBy R1 audiobook experience from a file-and-track-oriented flow into a seamless book-oriented flow. The user thinks in terms of **books**, not files. They tap a title → it plays from the saved position (or beginning if new). Track structure is invisible.

The design leverages three existing infrastructure pieces:

1. **The title autostart marker** — a code cave at `0x35DE00` that writes a sequence number to a RAM location when a genre→album list opens. The resume daemon already polls this marker to detect title taps.
2. **The direct-open helper** (`r1_audiobook_direct_open.c`) — a ptrace-based helper that intercepts the stock `shared_media_open` function at `0x49E200` and forces a specific zero-based track index.
3. **The resume daemon's track-restore state machine** — already handles direct-open triggers, swipe+tap track selection, seek-bar restore, and near-miss transport correction.

The core change is **Option C: combine the direct-open helper with the existing autostart marker to auto-play the saved track** — eliminating the intermediate track-list screen entirely. When the user taps a title in the generated view, the resume daemon detects the marker, looks up the saved resume record, uses the direct-open helper to force the saved track index, and then seeks to the saved position. The user never sees the track list.

---

## 1. Current Flow Analysis

### 1.1 Current User Journey

```
User taps "Audiobooks" launcher tile
  → Native hub opens (Scan, Titles, Authors, Series, Folders)
  
User taps "Titles"
  → Explorer opens /Audiobooks/_views/Titles/
  → Shows list of .m3u playlist files (one per book)
  
User taps a .m3u file (e.g., "The Amber Book.m3u")
  → Stock player opens the playlist
  → Shows track list (individual .mp3 files)
  → User must tap a track to start playback
  
User taps a track
  → Playback begins from track start (position 0)
  → Resume daemon detects new audiobook path
  → If autostart marker fired: daemon attempts track restore + seek
  → If no autostart: daemon waits, then attempts restore based on path change
```

### 1.2 Current Binary Patch Flow (Title Selection)

```
[hiby_player] Title row tapped
  → explorer_row_cave at 0x360708 executes
  → Calls explorer_open_helper at 0x360D50
  → Opens stock vg_listview_explorer with path "a:\Audiobooks\_views\Titles\*"
  → Shows .m3u files

[hiby_player] .m3u file tapped
  → Stock explorer opens the .m3u as a playlist
  → Stock player builds a track list from the .m3u contents
  → Shows track list view (vg_listview_songs_of_an_album or similar)

[hiby_player] Track tapped
  → Stock row-open callback fires
  → Calls shared_media_open at 0x49E200
  → Playback starts on the selected track
```

### 1.3 Current Resume Daemon Flow

```
[resume daemon] Main loop iteration
  → Reads path preview from user.ini offset 40
  → Polls title marker at 0x8E4000 (if autostart enabled)
  → If marker seq changed: maybe_autostart_book_title()
    → Reads album_ptr, track_list_ptr from marker memory
    → Determines match_reason (launcher/path/catalog/context/relaxed)
    → If reason allows preplay: book_title_direct_start_saved_track()
      → Looks up resume record for detected book
      → If saved_index > 1: tries direct_open helper to force track index
      → Falls back to swipe+tap track selection
      → Falls back to touch_first_track() (just taps track 1)
    → If reason=launcher: skips first-row tap, returns
    → Otherwise: touch_first_track() to enter track list
  → If path changed: maybe_restore_track() + maybe_restore() (seek)
```

### 1.4 Pain Points in Current Flow

| Pain Point | Root Cause |
|---|---|
| User sees track list after tapping a title | The .m3u opens a stock playlist view showing individual files |
| User must manually tap a track | The stock explorer always shows the playlist contents before playing |
| Track restore is a workaround | The daemon intercepts *after* the track list is already visible |
| Resume depends on UI timing | Touch event injection and framebuffer pixel checks are fragile |
| Autostart marker only fires for genre routes | The marker cave hooks into the genre→album list-open path, not the explorer path |
| Back navigation is messy | Generated view folders share stock explorer Back stack behavior |

---

## 2. Proposed Flow

### 2.1 Desired User Journey

```
User taps "Audiobooks" launcher tile
  → Native hub opens (Scan, Titles, Authors, Series, Folders)

User taps "Titles"
  → Explorer opens /Audiobooks/_views/Titles/
  → Shows list of book titles (one row per .m3u file)

User taps a book title
  → Playback starts automatically
    - If never played: track 1, position 0
    - If previously played: saved track, saved position
  → User goes directly to Now Playing screen
  → User never sees a track list

User presses Next/Prev physical buttons
  → Moves between tracks/chapters within the book seamlessly
  → Stock player handles track advance in list-loop mode
```

### 2.2 Proposed Binary Patch Flow (Title Selection with Auto-Play)

```
[hiby_player] Title row tapped
  → explorer_row_cave at 0x360708 executes (unchanged)
  → Opens stock vg_listview_explorer with path "a:\Audiobooks\_views\Titles\*"
  → Shows .m3u files (unchanged)

[hiby_player] .m3u file tapped
  → Stock explorer opens the .m3u as a playlist
  → Stock player builds track list from .m3u contents
  → Stock player shows track list view

  *** NEW: explorer_row_cave auto-taps first track ***
  → After a short delay (300-500ms), injected touch event taps row 1
  → Stock row-open callback fires for track 1
  → Calls shared_media_open at 0x49E200

  *** NEW: direct-open helper intercepts shared_media_open ***
  → If resume daemon has armed direct-open with saved track index:
    → shared_media_open receives forced row index (saved track - 1)
    → Playback starts on the saved track instead of track 1
  → If direct-open is not armed (new book, no resume record):
    → shared_media_open uses original row index (0 = track 1)
    → Playback starts from track 1

[resume daemon] Detects playback started
  → Path classification: audiobook
  → Autostart marker seq changed
  → Looks up resume record
  → If saved position > restore_min_ms: seek to saved position
  → Position restore complete
```

### 2.3 Sequence Diagram: New Book (Never Played)

```
User            hiby_player          Resume Daemon       Direct-Open Helper
  |                  |                    |                     |
  | tap "Titles"     |                    |                     |
  |----------------->|                    |                     |
  |                  | opens view         |                     |
  |<-----------------|                    |                     |
  | tap "Book Title" |                    |                     |
  |----------------->|                    |                     |
  |                  | opens .m3u         |                     |
  |                  | shows track list   |                     |
  |                  |---auto-tap row 1-->|                     |
  |                  |                    |                     |
  |                  | shared_media_open  |                     |
  |                  | (row 0 = track 1)  |                     |
  |                  |--> playback starts |                     |
  |                  |                    |                     |
  |                  |                    | marker seq changed  |
  |                  |                    | lookup record: none |
  |                  |                    | no restore needed   |
  |                  |                    | start tracking pos  |
  |<--- Now Playing--|                    |                     |
```

### 2.4 Sequence Diagram: Previously Played Book (Resume)

```
User            hiby_player          Resume Daemon       Direct-Open Helper
  |                  |                    |                     |
  | tap "Titles"     |                    |                     |
  |----------------->|                    |                     |
  |                  | opens view         |                     |
  |<-----------------|                    |                     |
  | tap "Book Title" |                    |                     |
  |----------------->|                    |                     |
  |                  | opens .m3u         |                     |
  |                  | shows track list   |                     |
  |                  |                    |                     |
  |                  |                    | marker seq changed  |
  |                  |                    | lookup record:      |
  |                  |                    |   track=3 pos=12:34 |
  |                  |                    | arm direct-open     |
  |                  |                    |   row_index=2       |
  |                  |                    |--------call-------->|
  |                  |                    |                     | ptrace attach
  |                  |                    |                     | patch 0x49E200
  |                  |                    |                     |<--armed
  |                  |                    |                     |
  |                  | auto-tap row 1     |                     |
  |                  | (triggers          |                     |
  |                  |  shared_media_open)|                     |
  |                  |                    |                     | intercept call
  |                  |                    |                     | override row=2
  |                  |                    |                     | restore original
  |                  |                    |                     |<--done
  |                  |                    |                     |
  |                  |--> track 3 plays   |                     |
  |                  |                    | seek to 12:34       |
  |                  |                    | via helper          |
  |                  |                    |                     |
  |<--- Now Playing--|                    |                     |
  |  (at 12:34)      |                    |                     |
```

---

## 3. Architecture Options Evaluated

### Option A: Change View Row Callback to Auto-Tap First Track

**Description:** After the stock explorer opens the .m3u playlist and shows the track list, inject a touch event to tap row 1 automatically. The resume daemon then handles track restore via the existing autostart marker path.

**Pros:**
- Minimal binary patch changes (none needed in hiby_player)
- Resume daemon already has touch event injection capability
- Works even if direct-open helper fails (falls back to track 1)

**Cons:**
- User briefly sees the track list before auto-tap fires
- Timing-dependent: auto-tap must wait for track list to render, but not too long
- Does not solve the track-restore problem alone — still needs direct-open or swipe+tap

**Verdict:** Useful as a **fallback layer** but insufficient alone.

### Option B: Use a Different Stock Route That Starts Playback Directly

**Description:** Change the title row to use a stock route that opens playback directly (e.g., `album\` route, `genre\` route with auto-play) instead of the explorer → .m3u → track list path.

**Pros:**
- No track list ever shown
- Cleaner UX

**Cons:**
- P8 route-table research exhaustively tested all available stock routes. None produce a clean direct-playback path without showing a track list or causing duplicate rows, wrong screens, or reboots.
- The `genre\Audiobook` route shows an album list (one row per book), but tapping a row still opens a track list.
- No stock route provides "tap title → immediately play" behavior.

**Verdict:** **Rejected.** No viable stock route exists. P8 research is conclusive.

### Option C: Direct-Open Helper Intercepts Media-Open (Recommended)

**Description:** Combine the auto-tap approach (Option A) with the direct-open helper. The resume daemon detects the title marker, looks up the saved track index, arms the direct-open helper to intercept `shared_media_open`, then the auto-tap triggers playback. The direct-open helper forces the saved track index, so the correct track plays immediately. The daemon then seeks to the saved position.

**Pros:**
- The user sees the track list for only a fraction of a second (auto-tap fires within 300-500ms)
- The correct track is selected transparently (direct-open forces saved track index)
- No new binary patches needed in hiby_player — uses existing code caves
- Resume daemon already has all the infrastructure (marker polling, direct-open trigger, seek restore)
- Graceful degradation: if direct-open fails, falls back to track 1 + swipe+tap restore

**Cons:**
- Brief flash of track list is still visible (300-500ms)
- Direct-open helper uses ptrace, which is safe but adds complexity
- Timing between auto-tap and direct-open arm must be coordinated

**Verdict:** **Recommended.** Best balance of UX improvement, implementation risk, and use of existing infrastructure.

### Option D: Custom List View with Patched Generator/Select (Future)

**Description:** Follow the path identified in `audiobook_views_research.md`: wrap the stock Books list generator (`0x005408a0`) and select handler (`0x00540a80`) to create a custom audiobook list view backed by sidecar catalog files. The select handler would call the audiobook title-open path directly instead of the TXT reader, bypassing the track list entirely.

**Pros:**
- No track list flash — completely seamless
- Clean Back navigation (stays within native hub)
- Foundation for Author/Series views with real metadata

**Cons:**
- Requires deep binary patching of the stock Books list generator — the most invasive change attempted
- The `$s2` register experiment (1.6.16.6-nativehub-s2-dev) caused a reboot
- The R3 Pro II DB Manager pattern (mode flag + generator wrapper) is unproven on R1
- High risk of bricking the device if the generator wrapper has bugs
- Long development cycle (weeks of RAM-only probing)

**Verdict:** **Future evolution.** Too high-risk for the current iteration. Should be pursued after the seamless experience is validated via Option C, and only with extensive RAM-only testing.

### Decision: Option C + Option A Fallback

Implement Option C (direct-open intercept + auto-tap) as the primary mechanism, with Option A (auto-tap alone) as the fallback when direct-open is unavailable or fails. This provides:

1. Immediate UX improvement (no manual track selection)
2. Low implementation risk (reuses existing infrastructure)
3. Graceful degradation (auto-tap → track 1 → daemon track restore)
4. Forward compatibility (can be replaced by Option D in the future)

---

## 4. Binary Patch Changes

### 4.1 Changes Needed: None (Primarily)

The key insight is that **no new binary patches to hiby_player are required** for Option C. The existing patch infrastructure already provides:

- The native hub launcher (`0x35DAEC`) and view rows (`0x360708`–`0x360798`)
- The explorer open helper (`0x360D50`)
- The title autostart marker (`0x35DE00`) with hook at `0x09FE40`
- The direct-open helper's probe cave at `0x760708` and scratch at `0x8E4400`

### 4.2 Optional Enhancement: Auto-Tap Code Cave

**New code cave needed:** A small MIPS code snippet that injects a touch-tap on the first track row after a configurable delay, triggered when the track list view appears.

**Location:** Use an existing unused code cave region. The region at `0x360A08` (currently the Refresh row cave) has 0x70 bytes. If the Refresh row is relocated or shared, use `0x360B00`–`0x360BFF` (currently unused in the patch map).

However, the auto-tap is **better implemented in the resume daemon** rather than as a hiby_player code cave, because:

1. The daemon already has touch event injection infrastructure
2. The daemon can coordinate timing between marker detection, direct-open arm, and auto-tap
3. A code cave cannot easily coordinate with the resume daemon's state machine
4. A daemon-side auto-tap is testable without flashing

**Decision:** Implement auto-tap in the resume daemon, not as a hiby_player code cave.

### 4.3 Title Autostart Marker: Verify Coverage

The title autostart marker at `0x35DE00` hooks into the genre→album list-open path at `0x09FE40`. The current hook fires when the stock `vg_listview_albums_of_a_genre` opens.

**Critical question:** Does the marker fire when a .m3u file is tapped in the explorer view?

**Analysis:** The marker hooks into the genre route's album-open callback, not the explorer's file-open callback. When the user taps a .m3u in the Titles view (`/Audiobooks/_views/Titles/*.m3u`), the stock explorer opens the .m3u as a playlist. This may go through a different code path than the genre→album route.

**Two scenarios:**

1. **Marker fires on .m3u open:** The .m3u playlist open triggers the same `shared_media_open` path that the marker hooks into. In this case, the existing marker works and no patch change is needed.

2. **Marker does NOT fire on .m3u open:** The .m3u playlist open goes through the explorer's file-open path, not the genre→album path. In this case, a new hook is needed.

**Risk mitigation:** The resume daemon's `maybe_autostart_book_title()` already has multiple match reasons (launcher, path, catalog, context, relaxed). Even if the marker does not fire, the daemon can detect the title selection via:
- **Path change detection:** The daemon polls `user.ini` offset 40 for the current path. When playback starts on an audiobook path, the daemon detects the change.
- **Context window:** If the daemon was recently in an audiobook context (within `BOOK_TITLE_CONTEXT_SECONDS`), it can infer a title selection.

**Required action:** Test whether the marker fires on .m3u open. If not, add a second hook point at the explorer's file-open callback, or rely on path-change detection + context window.

### 4.4 Summary of Binary Patch Changes

| Patch Area | Change Required | Risk |
|---|---|---|
| `0x35DAEC` (native hub launcher) | None | — |
| `0x360708`–`0x360798` (view row caves) | None | — |
| `0x360D50` (explorer open helper) | None | — |
| `0x35DE00` (title autostart marker) | Verify coverage; possibly add second hook | Low |
| `0x49E200` (shared_media_open) | None (direct-open helper handles at runtime) | — |
| `0x760708` (direct-open probe cave) | None | — |
| `0x8E4400` (direct-open scratch) | None | — |

---

## 5. Resume Daemon Changes

### 5.1 New Configuration Parameters

Add these to the daemon config (both shell and C versions):

```
# Auto-tap configuration
AUDIOBOOK_AUTOTAP_ENABLED=1          # Enable auto-tap of first track after .m3u open
AUDIOBOOK_AUTOTAP_DELAY_MS=400       # Delay before auto-tap (ms after track list appears)
AUDIOBOOK_AUTOTAP_SCREEN_CHECK=1     # Check framebuffer for track list before tapping
AUDIOBOOK_AUTOTAP_MAX_WAIT_MS=3000   # Maximum wait for track list to appear

# Direct-open coordination
AUDIOBOOK_DIRECT_OPEN_PRE_ARM=1      # Arm direct-open before auto-tap (pre-arm mode)
AUDIOBOOK_DIRECT_OPEN_PRE_ARM_DELAY_MS=200  # Delay between arm and auto-tap
```

### 5.2 Modified Autostart Flow

The `maybe_autostart_book_title()` function is modified to implement the seamless flow:

```
maybe_autostart_book_title(seq)
│
├─ Determine match_reason (unchanged)
├─ Set autostart window (unchanged)
├─ Clear restore state (unchanged)
│
├─ LOOKUP resume record
│   ├─ Get track_path from memscan/catalog (unchanged)
│   ├─ Get root from track_path (unchanged)
│   ├─ Get record from resume.d (unchanged)
│   ├─ If no record: NEW BOOK → goto AUTO_TAP_FIRST_TRACK
│   └─ If record exists: RESUME BOOK → goto RESUME_FLOW
│
├─ AUTO_TAP_FIRST_TRACK:
│   ├─ Sleep AUTOTAP_DELAY_MS
│   ├─ Verify track list visible (framebuffer check, if enabled)
│   ├─ touch_first_track()  — tap row 1
│   └─ Return (playback starts on track 1, daemon tracks position)
│
├─ RESUME_FLOW:
│   ├─ saved_index = record.track_index
│   ├─ saved_pos = record.position_ms
│   ├─ completed = record.completed
│   │
│   ├─ If completed: goto AUTO_TAP_FIRST_TRACK (start over)
│   │
│   ├─ If saved_index == 1:
│   │   ├─ No track switch needed
│   │   ├─ goto AUTO_TAP_FIRST_TRACK (auto-tap track 1)
│   │   └─ After playback starts: seek to saved_pos
│   │
│   ├─ If saved_index > 1 AND DIRECT_OPEN_PRE_ARM:
│   │   ├─ Arm direct-open helper with row_index = (saved_index - 1)
│   │   ├─ Sleep AUTOTAP_DELAY_MS (wait for track list to render)
│   │   ├─ Verify track list visible (framebuffer check, if enabled)
│   │   ├─ touch_first_track() — tap row 1
│   │   │   (This triggers shared_media_open, which direct-open intercepts)
│   │   ├─ Direct-open helper forces row = (saved_index - 1)
│   │   ├─ Wait for direct-open to complete (timeout 6s)
│   │   ├─ Verify playback started on saved track (path check)
│   │   └─ After playback starts: seek to saved_pos
│   │
│   └─ If saved_index > 1 AND direct-open unavailable:
│       ├─ Sleep AUTOTAP_DELAY_MS
│       ├─ touch_first_track() — tap row 1 (start on track 1)
│       ├─ Fall back to existing track_restore path:
│       │   ├─ direct_track_select (swipe + tap saved row)
│       │   └─ visible_track_select / near_miss_transport
│       └─ After track switch: seek to saved_pos
```

### 5.3 New Function: `auto_tap_first_track()`

```shell
# Shell version (pseudo-code for design clarity)
auto_tap_first_track() {
    [ "$AUTOTAP_ENABLED" = 1 ] || return 1
    
    # Wait for track list to render
    wait_start=$(monotonic_ms)
    while [ $(( $(monotonic_ms) - wait_start )) -lt "$AUTOTAP_MAX_WAIT_MS" ]; do
        if [ "$AUTOTAP_SCREEN_CHECK" = 1 ]; then
            audiobook_track_list_visible && break
        else
            sleep 0.1
            break
        fi
        sleep 0.05
    done
    
    # Additional settle delay
    sleep "$AUTOTAP_DELAY_MS"  # In shell: sleep with millisecond precision via helper
    
    # Tap first track
    touch_first_track || return 1
    return 0
}
```

### 5.4 New Function: `arm_direct_open_pre()`

```shell
arm_direct_open_pre() {
    pid=$1
    saved_index=$2
    saved_path=$3
    
    [ "$DIRECT_OPEN_PRE_ARM" = 1 ] || return 1
    [ "$BOOK_TITLE_DIRECT_OPEN_ENABLED" = 1 ] || return 1
    [ -x "$DIRECT_OPEN_HELPER" ] || return 1
    
    # Launch direct-open helper in background
    "$DIRECT_OPEN_HELPER" \
        --pid "$pid" \
        --row-index $((saved_index - 1)) \
        --probe-addr "$DIRECT_OPEN_PROBE_ADDR" \
        --scratch-addr "$DIRECT_OPEN_SCRATCH_ADDR" \
        --timeout-ms "$DIRECT_OPEN_TIMEOUT_MS" &
    direct_open_pid=$!
    
    # Brief delay for ptrace arm to complete
    sleep_direct_open_arm_delay
    
    return 0
}
```

### 5.5 Modified `book_title_direct_start_saved_track()`

The existing function is preserved but wrapped in the new flow:

```shell
book_title_direct_start_saved_track() {
    pid=$1
    track_list_ptr=$2
    catalog_scan_ptr=$3
    allow_memscan_root=${4:-1}
    
    # ... existing lookup logic (unchanged) ...
    # Gets: track_path, root, record, saved_path, saved_pos, saved_index
    
    # NEW: If pre-arm mode, arm direct-open then auto-tap
    if [ "$DIRECT_OPEN_PRE_ARM" = 1 ] && [ "$saved_index" -gt 1 ]; then
        arm_direct_open_pre "$pid" "$saved_index" "$saved_path"
        auto_tap_first_track
        # Wait for direct-open helper to complete
        wait "$direct_open_pid" 2>/dev/null
        direct_open_status=$?
        if [ "$direct_open_status" -eq 0 ]; then
            # Direct-open succeeded, verify track
            book_title_verify_selected_track "$saved_path" "$saved_index" 1 \
                "book-title pre-arm direct-open" "$path_before_direct"
            return $?
        fi
        # Direct-open failed, fall through to existing swipe+tap path
        log "book-title pre-arm direct-open failed, falling back to swipe+tap"
        touch_back_to_track_list || return 1
        sleep "$BOOK_TITLE_DIRECT_TRACK_RETURN_DELAY_SECONDS"
    fi
    
    # ... existing swipe+tap fallback (unchanged) ...
}
```

### 5.6 Seek Restore (Unchanged)

The seek restore flow remains the same. After the correct track is playing, the daemon:
1. Reads the current position from `hiby_player` memory
2. If position differs from saved_pos by more than `RESTORE_MIN_MS`:
   - Calls the seek helper to jump to `saved_pos`
   - Falls back to UI seek-bar tap if helper fails
3. Sets `restored_path = current_path`
4. Begins normal position tracking

---

## 6. DB/Catalog Changes

### 6.1 No Schema Changes Required

The existing catalog files already contain all fields needed for the seamless flow:

| File | Fields Used | Purpose |
|---|---|---|
| `catalog.tsv` | root, track_index, track_count, path, title, album, book_key | Track-level lookup |
| `catalog-books.tsv` | root_hiby_path, album, author, book_key, series, series_part, track_count, first_media_id | Book-level lookup |
| `catalog-view-title.tsv` | Pre-sorted title rows with character/pinyin | Title view generation |
| `catalog-view-author.tsv` | Pre-sorted author rows | Author view generation |
| `catalog-view-series.tsv` | Pre-sorted series rows | Series view generation |

### 6.2 Resume Records (Unchanged)

The resume record JSON schema (version 3) already stores:

```json
{
  "track_index": 3,        // Used by direct-open helper (row_index = track_index - 1)
  "position_ms": 1234567,  // Used by seek restore
  "completed": false,      // Used to decide start-over vs resume
  "current_path": "a:\\Audiobooks\\Book\\03.mp3"  // Used for verification
}
```

No new fields are needed.

### 6.3 Optional: Book-Level Progress Field

For future book-level progress display (not required for seamless playback):

Add a computed field to `catalog-books.tsv`:

```
progress_fraction  — computed from resume.d: position_ms / total_duration_ms
```

This would allow the UI to show "45% read" indicators. However, computing total duration requires summing track durations from the media DB, which is not currently stored in the catalog. This is a **future enhancement**, not needed for the seamless flow.

### 6.4 M3U View Generation (Unchanged)

The generated .m3u files under `/Audiobooks/_views/Titles/`, `/Authors/`, `/Series/` remain unchanged. Each .m3u contains the track list for one book. The seamless flow works by intercepting *after* the .m3u is opened, so the .m3u format does not need to change.

---

## 7. Track Transparency

### 7.1 Auto-Advance Between Tracks

The stock player's list-loop mode (which the resume daemon enforces via `ensure_audiobook_play_mode()`) already handles auto-advance:

- When a track ends, the stock player advances to the next track in the playlist
- The resume daemon detects the path change and updates the saved record
- No additional work is needed for auto-advance

**Verification needed:** Confirm that the stock player's list-loop mode works correctly with .m3u playlists opened from the explorer view. The daemon's `play_mode_enforce` logic sets the play mode to sequential (not shuffle/repeat-one) when an audiobook is active.

### 7.2 Physical Next/Prev Button Behavior

The R1's physical Next/Prev buttons map to stock track-next/track-prev commands. In list-loop mode:

- **Next button:** Skips to the next track in the playlist
- **Prev button:** Skips to the previous track (or restarts current track if near beginning)

The resume daemon's `track_next()` and `track_prev()` functions already handle path-change detection after button presses. The daemon updates the saved record with the new track index.

**No changes needed** for physical button behavior — it already works transparently.

### 7.3 Book-Level Progress Display

**Current state:** The Now Playing screen shows track-level progress (e.g., "3:45 / 12:30" for the current track).

**Desired state:** Show book-level progress (e.g., "2:34:15 / 8:45:00" for the whole book).

**Feasibility:** Very low. The Now Playing screen is rendered by the stock player UI code. Changing it to show book-level progress would require:
- Patching the stock duration display field to show cumulative book duration
- Patching the stock position display field to show cumulative book position
- Computing these values from the catalog's track list + current position

This is deep UI reverse engineering in `hiby_player`'s rendering code. It is **out of scope** for this design. The track-level progress display is acceptable — the user sees progress within the current chapter/part, which is useful for audiobooks.

**Future enhancement:** If the custom list view (Option D) is implemented, a book-progress bar could be added to the title list screen instead.

### 7.4 Track Number Display

The stock Now Playing screen may show "3/15" or similar track numbering. This is actually **useful** for audiobooks — it tells the user which chapter they're on. No change needed.

---

## 8. Risk Assessment

### 8.1 Risk Matrix

| Risk | Probability | Impact | Mitigation | Risk Level |
|---|---|---|---|---|
| Auto-tap fires before track list renders | Medium | Low (tap missed, user sees track list) | Framebuffer screen check + max wait delay | **Low** |
| Direct-open helper fails to intercept | Low | Medium (falls back to track 1, daemon corrects) | Existing swipe+tap fallback path | **Low** |
| Direct-open helper ptrace causes crash | Very Low | High (player crash) | Helper validates ranges, timeout, auto-restore | **Medium** |
| Autostart marker doesn't fire on .m3u open | Medium | Medium (no pre-arm, relies on path detection) | Context window + path-change detection fallback | **Medium** |
| Auto-tap taps wrong row | Low | Medium (plays wrong track) | Framebuffer screen verification before tap | **Low** |
| Resume record has stale track index | Low | Low (plays wrong track briefly, daemon corrects) | Track verify function + near-miss transport | **Low** |
| Battery life impact from auto-tap | Very Low | Low (one extra touch event per title tap) | Negligible compared to existing daemon polling | **Very Low** |
| Music playback affected | Very Low | High (breaks core function) | Auto-tap only fires when autostart marker changes; music paths don't trigger marker | **Very Low** |
| Shell daemon → C daemon migration conflict | Medium | Medium | Complete C daemon cutover before seamless feature | **Medium** |

### 8.2 Safe Changes (Low Risk)

| Change | Why Safe |
|---|---|
| Auto-tap in resume daemon | Only injects touch events, no binary patches. Testable via runtime install without flashing. |
| Direct-open pre-arm mode | Uses existing helper, existing probe cave, existing scratch range. Helper has timeout and auto-restore. |
| New config parameters | Additive to existing config, defaults preserve current behavior. |
| Track list screen check | Read-only framebuffer access, already used for back-guard and seek-bar. |

### 8.3 Moderate Risk Changes

| Change | Why Moderate | Mitigation |
|---|---|---|
| Autostart marker coverage on .m3u open | May require a new hook point in hiby_player if the explorer path doesn't trigger the genre→album marker. | Test on device first. If not covered, rely on context+path detection (no patch needed). Only add a new hook if context detection is insufficient. |
| Timing coordination (arm → tap → intercept) | Three operations must happen in the right order within a narrow window. | Generous timeouts, fallback paths, configurable delays for tuning. |
| Shell → C daemon migration | The seamless feature should be built on the C daemon, not the shell daemon. | Complete the shadow-mode migration first, or implement seamless in shell and port to C. |

### 8.4 High Risk Changes (Not in This Design)

| Change | Why High Risk | Status |
|---|---|---|
| Custom list view (Option D) | Deep binary patching of stock generator/select | Future evolution, not this iteration |
| New hiby_player code caves for auto-tap | Unnecessary — daemon handles auto-tap | Rejected |
| Route table modifications | P8 research showed all variants fail | Rejected |
| Now Playing UI changes for book progress | Deep UI rendering patches | Out of scope |

---

## 9. Implementation Phases

### Phase 1: Verify Marker Coverage (1-2 days)

**Goal:** Determine whether the title autostart marker fires when a .m3u file is opened from the explorer view.

**Steps:**
1. On the R1 with current firmware, open Audiobooks → Titles → tap a .m3u file
2. Check the resume daemon log for `book-title autostart` entries
3. Check the marker memory at `0x8E4000` via ADB for seq changes
4. If marker fires: proceed to Phase 2
5. If marker does NOT fire: test context-window + path-change detection reliability

**Deliverable:** Test results documenting marker coverage. If marker doesn't fire, document the fallback detection path.

**Risk:** None — read-only testing on current firmware.

### Phase 2: Implement Auto-Tap in Resume Daemon (3-5 days)

**Goal:** The resume daemon automatically taps the first track after a .m3u is opened, eliminating the manual track selection step.

**Steps:**
1. Add `AUTOTAP_*` config parameters to the daemon config
2. Implement `auto_tap_first_track()` function
3. Modify `maybe_autostart_book_title()` to call `auto_tap_first_track()` after marker detection
4. For new books (no resume record): auto-tap track 1
5. For previously played books with saved_index == 1: auto-tap track 1, then seek
6. For previously played books with saved_index > 1: auto-tap track 1, then rely on existing track-restore path (swipe+tap)

**Deliverable:** Runtime-installable daemon update that auto-taps the first track. Testable without flashing.

**Acceptance criteria:**
- Tapping a title in the Titles view automatically starts playback within 1 second
- User does not need to manually tap a track
- New books start from track 1, position 0
- Previously played books with track_index == 1 resume at saved position
- Previously played books with track_index > 1 eventually reach the saved track (via swipe+tap restore)

**Risk:** Low — daemon-side change only, no firmware patch.

### Phase 3: Implement Direct-Open Pre-Arm (3-5 days)

**Goal:** The direct-open helper intercepts the media-open call to force the saved track index, making track restore instant instead of requiring visible swipe+tap gestures.

**Steps:**
1. Add `DIRECT_OPEN_PRE_ARM` config parameter
2. Implement `arm_direct_open_pre()` function
3. Modify `book_title_direct_start_saved_track()` to pre-arm before auto-tap
4. Coordinate timing: arm → delay → auto-tap → intercept → verify
5. Fall back to swipe+tap if direct-open fails

**Deliverable:** Runtime-installable daemon update with direct-open pre-arm mode.

**Acceptance criteria:**
- Tapping a title with saved_index > 1 starts playback on the saved track within 2 seconds
- No visible swipe gestures on the track list
- If direct-open fails, falls back to Phase 2 behavior (auto-tap + swipe+tap)
- Direct-open timeout (6s) does not block normal playback

**Risk:** Medium — uses ptrace at runtime, but helper has extensive safety checks.

### Phase 4: Testing and Tuning (3-5 days)

**Goal:** Tune timing parameters and test edge cases on the device.

**Test matrix:**

| Scenario | Expected Behavior |
|---|---|
| New book, single track | Auto-tap → plays from start |
| New book, multi-track | Auto-tap track 1 → plays from start |
| Resume book, track 1, pos 5:00 | Auto-tap track 1 → seek to 5:00 |
| Resume book, track 3, pos 12:34 | Direct-open track 3 → seek to 12:34 |
| Resume book, track 15/15, near end | Completion check → start from track 1 |
| Completed book | Start over from track 1, position 0 |
| Quick switch between two books | Each resumes at its own position |
| Book with 50+ tracks | Direct-open handles high index |
| Direct-open timeout | Fallback to swipe+tap |
| Music playback then back to audiobook | No auto-tap during music |
| Launcher tap (no title selected) | No auto-tap (launcher guard) |

**Tuning parameters:**
- `AUTOTAP_DELAY_MS` — too short: tap misses; too long: visible flash
- `AUTOTAP_MAX_WAIT_MS` — too short: misses slow renders; too long: sluggish
- `DIRECT_OPEN_PRE_ARM_DELAY_MS` — must be long enough for ptrace arm, short enough to not delay auto-tap

**Deliverable:** Tuned configuration values and test report.

### Phase 5: Port to C Daemon (if not done in Phase 2-3)

**Goal:** If the seamless feature was implemented in the shell daemon, port it to the C daemon per the C rewrite spec.

**Steps:**
1. Add `autotap_*` fields to `daemon_config` struct
2. Implement `auto_tap_first_track()` in `state.c`
3. Implement `arm_direct_open_pre()` in `helpers.c`
4. Modify `state_maybe_autostart()` in `state.c`
5. Add unit tests for new functions
6. Shadow-mode test alongside shell daemon

**Deliverable:** C daemon with seamless feature, tested in shadow mode.

### Phase 6: Firmware Integration (1-2 days)

**Goal:** Bake the tuned daemon and config into the firmware package.

**Steps:**
1. Build new rootfs with updated daemon binary
2. Set tuned config defaults in daemon config file
3. Run full verification: `verify_r1_audiobook_build.py`
4. Flash to device
5. Run full regression test suite: `python3 tests/test_suite.py --suite full`
6. Verify music playback unaffected

**Deliverable:** Release-ready firmware package with seamless audiobook experience.

---

## 10. Component Impact Summary

| Component | Changes Required | Implementation Phase |
|---|---|---|
| `hiby_player` binary patches | None (verify marker coverage only) | Phase 1 |
| `r1_audiobook_resume_daemon.sh` (shell) | Auto-tap, pre-arm, config params | Phase 2-3 |
| C daemon (`src/state.c`, `src/helpers.c`, `src/config.c`) | Port of shell changes | Phase 5 |
| `r1_audiobook_direct_open.c` | None (existing helper is sufficient) | — |
| `r1_audiobook_memscan.c` | None | — |
| `r1_audiobook_db_maint.c` | None | — |
| `r1_audiobook_db_watch.sh` | None | — |
| `patch_hiby_player.py` | None (no new patches) | — |
| `generate_audiobook_m3u_views.py` | None | — |
| Catalog files (`catalog.tsv`, `catalog-books.tsv`) | None | — |
| Resume records (`resume.d/*.json`) | None (schema unchanged) | — |
| Firmware build script | Update daemon binary in rootfs | Phase 6 |
| Verification tools | Add auto-tap behavior to smoke tests | Phase 4 |
| Test suite | Add seamless flow test cases | Phase 4 |

---

## 11. Battery Life Analysis

### 11.1 Additional Work from Seamless Flow

| Operation | Frequency | CPU Cost | Battery Impact |
|---|---|---|---|
| Auto-tap touch event injection | Once per title tap | Negligible (1 touch event) | Negligible |
| Direct-open ptrace arm | Once per title tap (if resume) | ~50ms of ptrace overhead | Negligible |
| Framebuffer screen check | Once per auto-tap | ~10ms of fb read | Negligible |
| Total additional per title tap | — | <100ms | <0.01% per tap |

### 11.2 No Change to Daemon Polling

The daemon's main loop polling frequency is unchanged. The auto-tap and pre-arm only execute during the autostart window (triggered by marker change), not during steady-state playback. During normal playback, the daemon's CPU usage is the same as before.

### 11.3 Conclusion

**No measurable battery life impact.** The seamless flow adds at most a few hundred milliseconds of CPU time per title tap, which happens at most a few times per day in typical use.

---

## 12. Observability

### 12.1 New Log Events

```
# Auto-tap events
auto-tap wait screen_visible=X delay_ms=Y max_wait_ms=Z
auto-tap fired row=1 screen_verified=1
auto-tap skipped reason=no_track_list timeout_ms=N

# Direct-open pre-arm events
direct-open pre-arm pid=P row_index=R saved_index=S
direct-open pre-arm armed probe=0xADDR scratch=0xADDR
direct-open pre-arm fired count=1 original_row=0 override_row=R
direct-open pre-arm timed_out after_ms=6000
direct-open pre-arm failed status=N

# Seamless flow summary
seamless title-tap book=BOOK_TITLE track_index=T position_ms=P method=direct-open|swipe-tap|track-1 elapsed_ms=E
```

### 12.2 Diagnostics

Add to the periodic stats log:

```
stats ... autotaps=N autotap_misses=N direct_open_pre_arm=N direct_open_pre_arm_failed=N
```

---

## 13. Test Plan for Karen

### 13.1 Functional Requirements

| ID | Requirement | Priority |
|---|---|---|
| SEAM-001 | Tapping a title in Titles view starts playback automatically | P0 |
| SEAM-002 | New books start from track 1, position 0 | P0 |
| SEAM-003 | Previously played books resume at saved track and position | P0 |
| SEAM-004 | User does not need to manually select a track | P0 |
| SEAM-005 | Physical Next/Prev buttons move between tracks within the book | P0 |
| SEAM-006 | Auto-advance to next track when current track ends | P1 |
| SEAM-007 | Completed books start over from the beginning | P1 |
| SEAM-008 | Quick switch between books preserves each book's position | P0 |
| SEAM-009 | Music playback is unaffected | P0 |
| SEAM-010 | Launcher tap (without title selection) does not start playback | P0 |

### 13.2 Edge Cases

| ID | Scenario | Expected Behavior |
|---|---|---|
| SEAM-E01 | Book with 1 track, no resume record | Auto-tap → plays from start |
| SEAM-E02 | Book with 50 tracks, resume at track 45 | Direct-open track 45 → seek |
| SEAM-E03 | Direct-open helper times out | Fallback to swipe+tap restore |
| SEAM-E04 | Direct-open helper crashes | Fallback to swipe+tap restore, no player crash |
| SEAM-E05 | Track list doesn't render within max wait | Auto-tap skipped, user sees track list (manual selection) |
| SEAM-E06 | Resume record has track_index beyond track_count | Treat as new book, start from track 1 |
| SEAM-E07 | .m3u file is empty or corrupt | Stock player shows "No music found", no auto-tap |
| SEAM-E08 | User taps title, then immediately taps Back before playback starts | Daemon detects path not audiobook, cancels autostart |
| SEAM-E09 | SD card swapped, catalog rebuilt, resume records still present | Resume works with new catalog paths |
| SEAM-E10 | Daemon restarts mid-restore | Restore state cleared, next title tap starts fresh |

### 13.3 Performance Expectations

| Metric | Target | Measurement |
|---|---|---|
| Time from title tap to playback start (new book) | < 1.5s | ADB log timestamps |
| Time from title tap to playback start (resume, track 1) | < 2.0s | ADB log timestamps |
| Time from title tap to playback start (resume, track N) | < 3.0s | ADB log timestamps |
| Visible track list flash duration | < 500ms | Framebuffer capture |
| Battery life delta | < 1% | Day-over-day comparison |

### 13.4 Suggested Test Cases for Karen

1. **SEAM-T01: New book auto-play**
   - Open Audiobooks → Titles → tap a book never played
   - Verify: playback starts within 1.5s, track 1, position 0
   - Verify: no manual track selection needed

2. **SEAM-T02: Resume same-track book**
   - Play a book to track 1, position 5:00, back out
   - Tap same title → verify: resumes at track 1, position ~5:00

3. **SEAM-T03: Resume multi-track book (direct-open)**
   - Play a book to track 3, position 12:34, back out
   - Tap same title → verify: resumes at track 3, position ~12:34
   - Verify: no visible swipe gestures in track list

4. **SEAM-T04: Resume multi-track book (fallback)**
   - Disable direct-open helper (config flag)
   - Play a book to track 3, position 12:34, back out
   - Tap same title → verify: eventually reaches track 3, position ~12:34
   - Verify: swipe+tap fallback is used

5. **SEAM-T05: Completed book restart**
   - Play a book to the end (within completion threshold)
   - Tap same title → verify: starts from track 1, position 0

6. **SEAM-T06: Quick book switch**
   - Play book A to track 2, position 3:00
   - Back out, tap book B, play to track 1, position 1:00
   - Back out, tap book A → verify: resumes at track 2, position ~3:00
   - Back out, tap book B → verify: resumes at track 1, position ~1:00

7. **SEAM-T07: Music unaffected**
   - Play an audiobook, back out
   - Open Music → play a music album
   - Verify: no auto-tap events in daemon log
   - Verify: music plays normally

8. **SEAM-T08: Launcher guard**
   - Tap Audiobooks launcher tile (don't select a title)
   - Verify: no auto-tap fires
   - Verify: no audiobook starts playing

9. **SEAM-T09: Physical button navigation**
   - Play a multi-track book
   - Press Next → verify: advances to next track
   - Press Prev → verify: returns to previous track
   - Verify: daemon updates saved record with new track index

10. **SEAM-T10: Auto-advance at track end**
    - Play a multi-track book, let track 1 finish naturally
    - Verify: stock player advances to track 2
    - Verify: daemon detects track change and updates saved record

---

## 14. ADR: Seamless Audiobook Auto-Play Approach

### Problem

The current audiobook flow requires users to manually select a track from a track list after tapping a book title. This breaks the mental model of "tap a book → it plays."

### Decision

Implement **Option C: Direct-open helper intercept + auto-tap fallback**. The resume daemon detects title selection via the autostart marker, arms the direct-open helper with the saved track index, auto-taps the first track to trigger playback, and the direct-open helper intercepts the media-open call to force the correct track. Position is then restored via the existing seek helper.

### Alternatives Considered

1. **Option A (auto-tap only):** Insufficient — does not solve track restore for multi-track books without visible swipe gestures.
2. **Option B (different stock route):** Rejected — P8 research exhaustively proved no stock route provides direct playback.
3. **Option D (custom list view):** Too high-risk for current iteration — requires deep generator/select patching that caused reboots in prior experiments.

### Advantages

- No new hiby_player binary patches needed
- Uses existing, tested infrastructure (autostart marker, direct-open helper, track restore)
- Graceful degradation through multiple fallback layers
- Testable via runtime install without flashing

### Disadvantages

- Brief track list flash (~300-500ms) before auto-tap fires
- Depends on timing coordination between daemon, helper, and stock UI
- Direct-open helper uses ptrace (inherent complexity)

### Tradeoffs

- Accepting a brief track list flash avoids the high risk of deep UI patching
- Accepting timing dependency avoids the high risk of custom list view implementation
- The flash is a minor cosmetic issue, not a functional problem

### Decision Rationale

The R1 is a MIPS embedded device with a closed-source player binary. Deep UI patching has caused reboots (1.6.16.6-nativehub-s2-dev). The project's core philosophy is "modify the smallest stable surface." Option C extends existing infrastructure rather than creating new patch surfaces. The brief track list flash is an acceptable tradeoff for dramatically reduced implementation risk.

### Consequences

- The seamless experience is "95% seamless" — there is a brief flash of the track list
- A future Option D implementation can eliminate the flash entirely
- The daemon's autostart flow becomes more complex but follows established patterns
- The C daemon migration must be completed before or alongside this feature

### Future Revisit Criteria

- When the custom list view (Option D) is proven safe in RAM-only testing
- When user feedback indicates the track list flash is unacceptable
- When the Books list generator wrapper pattern is validated on R1

### Dependencies

- Title autostart marker patch (`--audiobook-title-autostart-marker`)
- Direct-open helper (`r1_audiobook_direct_open`)
- Resume daemon (shell or C) with autostart support
- Catalog files with track_index and book_key fields

---

## 15. Future Evolution

### 15.1 Phase 7+ (Future): Custom List View (Option D)

After the seamless experience is validated and stable, pursue the custom list view approach from `audiobook_views_research.md`:

1. Wrap `vg_listview_book_list` generator at `0x005408a0` with a mode-flag pattern
2. Feed sidecar catalog files (`catalog-view-title.tsv`, etc.) as the list source
3. Replace the select handler at `0x00540a80` to call the audiobook title-open path
4. This eliminates the track list entirely — true one-tap playback
5. Also enables clean Author and Series views with real metadata

### 15.2 Phase 8+ (Future): Book-Level Progress

1. Compute cumulative book duration from catalog track durations
2. Add a progress field to `catalog-books.tsv`
3. Display book-level progress on the title list (if custom list view is implemented)

### 15.3 Phase 9+ (Future): Smart Rewind on Resume

Already designed in the feature reference: optionally seek a few seconds before the saved point. Controlled by `AUDIOBOOK_RESTORE_REWIND_MS` config parameter.

---

## Appendix A: Address Map Reference

| Address | Purpose | Used by Seamless Flow? |
|---|---|---|
| `0x35DAEC` | Native hub launcher callback cave | Yes (entry point, unchanged) |
| `0x35DE00` | Title autostart marker cave | Yes (marker detection) |
| `0x35DE60` | Native hub title row cave | No (rejected approach) |
| `0x360708` | Titles row cave | Yes (opens Titles view, unchanged) |
| `0x360808` | Titles path string | Yes (view path, unchanged) |
| `0x360D50` | Explorer open helper | Yes (opens explorer, unchanged) |
| `0x49E200` | shared_media_open | Yes (intercepted by direct-open at runtime) |
| `0x760708` | Direct-open probe cave | Yes (direct-open helper, unchanged) |
| `0x8E4000` | Title marker RAM location | Yes (marker polling, unchanged) |
| `0x8E4400` | Direct-open scratch range | Yes (direct-open helper, unchanged) |

## Appendix B: Configuration Parameter Reference

### New Parameters for Seamless Flow

| Parameter | Default | Description |
|---|---|---|
| `AUDIOBOOK_AUTOTAP_ENABLED` | `1` | Enable auto-tap of first track after .m3u open |
| `AUDIOBOOK_AUTOTAP_DELAY_MS` | `400` | Delay (ms) before auto-tap after track list appears |
| `AUDIOBOOK_AUTOTAP_SCREEN_CHECK` | `1` | Verify track list visible via framebuffer before tapping |
| `AUDIOBOOK_AUTOTAP_MAX_WAIT_MS` | `3000` | Maximum wait for track list to render |
| `AUDIOBOOK_DIRECT_OPEN_PRE_ARM` | `1` | Arm direct-open before auto-tap (pre-arm mode) |
| `AUDIOBOOK_DIRECT_OPEN_PRE_ARM_DELAY_MS` | `200` | Delay between arm and auto-tap |

### Existing Parameters Used by Seamless Flow

| Parameter | Default | Description |
|---|---|---|
| `AUDIOBOOK_BOOK_TITLE_AUTOSTART_ENABLED` | `1` | Master switch for autostart |
| `AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED` | `1` | Enable direct-open helper |
| `AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED` | `1` | Enable pre-play direct-start |
| `AUDIOBOOK_DIRECT_OPEN_PROBE_ADDR` | `0x760708` | Direct-open probe code cave |
| `AUDIOBOOK_DIRECT_OPEN_SCRATCH_ADDR` | `0x8E4400` | Direct-open scratch range |
| `AUDIOBOOK_DIRECT_OPEN_TIMEOUT_MS` | `6000` | Direct-open helper timeout |
| `AUDIOBOOK_BOOK_TITLE_AUTOSTART_DELAY_SECONDS` | `2` | Delay after marker before acting |
| `AUDIOBOOK_BOOK_TITLE_CONTEXT_SECONDS` | `30` | Context window after audiobook activity |
| `AUDIOBOOK_RESTORE_MIN_MS` | `1000` | Minimum position to attempt seek restore |

---

*End of specification.*