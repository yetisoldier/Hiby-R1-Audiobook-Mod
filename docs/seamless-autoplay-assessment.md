# Seamless Audiobook Auto-Play: Architecture Assessment

> **Date:** 2026-07-10  
> **Status:** Design assessment — no code  
> **Context:** The current auto-tap approach in `state.c` (`auto_tap_first_track()`) is not working. This document evaluates all feasible approaches and recommends a path forward.

---

## 1. Problem Statement

### 1.1 The Goal

When the user taps a title in Audiobooks → Titles, playback should start automatically at the saved position (or beginning if new). The user should experience: **tap title → playback starts**. No track list should be visible.

### 1.2 The Current Auto-Tap Approach (Broken)

The existing `auto_tap_first_track()` in `state.c` (lines ~900–970) tries to detect when a `.m3u` playlist opens by checking if the path slot from `user.ini` offset 40 contains `_views` and `.m3u`. When both are present, it sleeps `autotap_delay_ms` and then taps the first track via `touch_first_track()`.

**Why it fails:**

1. The path slot in `user.ini` (offset 40) shows the **currently loaded audio file**, not the `.m3u` playlist. When the stock player opens a `.m3u`, it immediately loads the first track — the path changes from the `.m3u` to the first `.mp3` before the daemon's 2–5 second poll interval can catch the `.m3u` in the slot.

2. The daemon polls every 2–5 seconds. The stock player transitions from playlist-view to track-loading in well under 1 second. By the time the daemon reads the path, it's already `a:\Audiobooks\Author\Book\01.mp3`, not `a:\Audiobooks\_views\Titles\Book.m3u`.

3. Diagnostic output confirms this: `at_fired=0 at_skipped=2` — the auto-tap never fires because the `.m3u` path is never seen in the slot.

**Root cause:** The path slot is a *post-load* indicator. It reflects what's playing, not what view is open. The `.m3u` is a transient playlist reference that the stock player resolves to a concrete track file immediately.

### 1.3 The Marker Limitation

The book-title autostart marker at `0x8E4000` (written by a code cave at `0x35DE00`) fires when the `genre\Audiobook` route opens an album list — not when a `.m3u` file is tapped in the filesystem explorer view. The Titles view uses the filesystem explorer (`vg_listview_explorer`), which goes through a different code path than `vg_listview_albums_of_a_genre`. So the marker does not fire for the Titles view tap.

The daemon has fallback detection reasons (`path`, `catalog`, `context`, `relaxed`), but these all trigger *after* playback has already started on track 1 — they cannot pre-arm the direct-open helper before the track loads.

---

## 2. Approach Evaluation

### Approach A: Detect Playlist Open via Framebuffer

**Concept:** The daemon polls `/dev/fb0` to detect when the "track list" screen appears (the intermediate screen between tapping a `.m3u` and seeing the Now Playing screen). When the track list is visible, the daemon taps row 1 before the user even notices.

**Mechanism:**
- The daemon already has `audiobook_track_list_visible()` in `ui.c` which reads framebuffer pixels to classify the current screen.
- The daemon would poll the framebuffer at a high rate (every 100–200ms) when it knows the user is in the Titles view (via the context window).
- When `audiobook_track_list_visible()` returns true, immediately inject `touch_first_track()`.

**Feasibility:** Medium. The framebuffer classification functions exist and are already used for the back-guard. The track-list screen has distinguishing pixel signatures (subheader + header pattern).

**Pros:**
- No new binary patches needed — uses existing framebuffer read + touch injection.
- Daemon-side only — testable via runtime install, no flashing.
- Works for any path that ends up showing a track list, not just `.m3u` from `_views/`.
- Can pre-arm the direct-open helper before tapping, if the daemon knows which book is being opened (from the context window + catalog lookup).

**Cons:**
- **Timing race:** The daemon must detect the track-list screen and tap within ~200–500ms before the user notices it. This requires high-frequency framebuffer polling, which increases CPU usage during the context window.
- **False positives:** The track-list screen may look similar to other list views (e.g., album track lists in Music). The `audiobook_track_list_visible()` function already differentiates title-list from track-list, but edge cases exist.
- **No book identity:** The framebuffer tells us *what screen* is showing, not *which book* was tapped. Without knowing the book, the daemon can't pre-arm direct-open with the saved track index. It can only tap track 1 and then rely on the post-load track-restore path (swipe + tap, which is visible and slow).
- **Battery:** Polling the framebuffer every 100–200ms during the context window adds CPU overhead. The context window can be 30+ seconds.

**UX Quality:** Medium. The track list may flash briefly (100–300ms). Track restore for multi-track books still requires visible swipe gestures unless direct-open can be coordinated.

**Risk:** Low — no binary patches, framebuffer reads are safe.

**Effort:** Medium. ~3–5 days to implement high-frequency framebuffer polling, screen detection, and coordinated auto-tap.

**Verdict:** **Viable as a component** but insufficient alone. Solves the "tap first track" problem but not the "start on the saved track" problem. Best combined with Approach E (marker extension) or B (pre-arm on context).

---

### Approach B: Pre-Arm Direct-Open on Context Window

**Concept:** Instead of detecting the playlist open reactively, pre-arm the direct-open helper *before* the user taps a title. When the Titles list is visible and the daemon is in an audiobook context window, arm the direct-open helper in "detect book from path" mode. When the user taps a title, the stock player calls `shared_media_open` — the direct-open helper intercepts and forces the saved track index.

**Mechanism:**
- When the daemon detects the user is in the Titles view (via framebuffer `audiobook_title_list_visible()` + context window), it enters a "pre-arm" state.
- In pre-arm state, the daemon arms the direct-open helper with a special mode: instead of a fixed row index, the helper reads the path being opened, looks up the resume record, and overrides the row index to the saved track.
- When `shared_media_open` fires, the helper intercepts it, reads the path, queries the catalog/resume record, and forces the correct track.

**Feasibility:** Low–Medium. The direct-open helper (`r1_audiobook_direct_open.c`) is a ptrace-based tool that patches `shared_media_open` at `0x49E200`. Currently it accepts a fixed row index. Extending it to do a runtime path lookup (read the path from `user.ini` or from the player's memory, query the catalog, determine the saved track index) would require significant helper enhancement. The helper runs as a separate process with ptrace access — it can read player memory but has no catalog access.

**Pros:**
- Eliminates the need to detect the playlist at all — the helper intercepts at the `shared_media_open` level.
- The correct track starts playing immediately, no visible track-list flash.
- No new binary patches needed — uses existing direct-open infrastructure.

**Cons:**
- **Helper complexity:** The direct-open helper would need to be substantially enhanced to do runtime path-to-track-index resolution. Currently it's a simple ptrace wrapper that patches a row index. Making it do catalog lookups requires either:
  - Embedding catalog parsing in the helper (significant C code addition), or
  - The daemon pre-computing a "book path → saved track index" map and passing it to the helper (feasible but complex — the helper would need to match the opening path against the map).
- **Pre-arm timing:** The helper's ptrace patch is not persistent — it has a timeout (currently 6 seconds). If the user browses the Titles list for more than 6 seconds before tapping, the pre-arm expires. The daemon would need to re-arm periodically.
- **Unknown book:** The daemon doesn't know which book the user will tap. The helper must resolve the book at intercept time, which means the helper must read the path being opened. The `shared_media_open` function receives the path as an argument — the helper could read it from the player's registers/stack at intercept time. But this requires MIPS ABI knowledge ($a0 register on MIPS contains the first argument).
- **Ptrace race:** The helper must be armed and waiting when `shared_media_open` fires. If the arm expires or the daemon re-arms at the wrong moment, the intercept fails.
- **Multiple books:** If the user scrolls through the Titles list, the daemon can't predict which book will be tapped. Pre-arming with a single book's index is wrong. Pre-arming with "detect from path" mode is the only option, and that's complex.

**UX Quality:** High — no track list flash, correct track plays immediately. But only if the helper successfully resolves the book at intercept time.

**Risk:** Medium. Ptrace patching of `shared_media_open` is already used and tested, but extending the helper to do runtime path resolution adds complexity. A bug in the helper could crash the player.

**Effort:** High. ~5–8 days to enhance the helper with path resolution, test ptrace register reading on MIPS, and coordinate with the daemon.

**Verdict:** **Promising but complex.** The best UX outcome if it works, but the helper enhancement is non-trivial and the timing/coordination risk is significant. Could be pursued as a Phase 2+ enhancement after a simpler approach is working.

---

### Approach C: Binary Patch to Auto-Tap in hiby_player

**Concept:** Modify the view row callback in `hiby_player` so that after opening the `.m3u` playlist, it automatically calls `shared_media_open` on the first track. The resume daemon then handles track restore via the existing path-change detection.

**Mechanism:**
- A new code cave in `hiby_player` that hooks into the playlist-open completion callback.
- After the stock player builds the track list from the `.m3u`, the code cave injects a `jal shared_media_open` call with row index 0.
- The resume daemon detects the path change and does track restore + seek.

**Feasibility:** Low. This requires finding the exact callback address after the playlist-open completes, ensuring the right registers are set for `shared_media_open`, and not corrupting the stock player's state. Previous register experiments ($s2 in `1.6.16.6-nativehub-s2-dev`) caused a reboot. The MIPS ABI and register conventions make it very difficult to inject a function call without corrupting the caller's state.

**Pros:**
- The track list would never be visible — playback starts immediately.
- No daemon timing coordination needed — the patch fires synchronously.

**Cons:**
- **High reboot risk:** Previous binary patch experiments that modified register usage in `hiby_player` caused the R1 to reboot. The `$s2` experiment is documented as a known-bad package (`3ad80f51167cc2655c59ccf6f18b3ffa`).
- **No undo:** A binary patch that causes a reboot loop requires a full reflash via SD card. The risk of bricking is real if the patch corrupts the boot path.
- **Register safety:** MIPS calling convention requires `$a0`–`$a3` for arguments, `$t0`–`$t9` are caller-saved, `$s0`–`$s7` must be preserved. Injecting a `jal` instruction clobbers `$ra` and potentially `$t*` registers. The stock code after the injected call may depend on register values that get corrupted.
- **Code cave space:** Available code caves are small (0x70 bytes at most). A MIPS function call setup + call + restore sequence needs ~20–40 bytes, which fits, but leaves no room for error handling.
- **No fallback:** If the patch is wrong, there's no graceful degradation — the player crashes or reboots.

**UX Quality:** Theoretically perfect — no track list flash at all. But only if the patch works.

**Risk:** **High.** This is the most dangerous approach. The $s2 experiment proved that register manipulation in `hiby_player` can reboot the device. A code cave that calls `shared_media_open` requires careful register preservation that has not been proven safe.

**Effort:** Medium-High. ~5–10 days of RAM-only probing, plus the risk of repeated reboots during testing.

**Verdict:** **Rejected.** The risk of rebooting the R1 is too high. Previous binary patch experiments in this area have failed. The project's architecture review explicitly classifies binary patches to `hiby_player` as High Risk. This approach violates the "smallest stable surface" philosophy.

---

### Approach D: Use the `genre\Audiobook` Route (Old Approach)

**Concept:** Instead of the filesystem view rows showing `.m3u` files, change the Titles view to use the `genre\Audiobook` route which starts playback directly when tapping an album. This is the old approach used before the native hub.

**Mechanism:**
- The Titles row cave would open `genre\Audiobook` instead of `a:\Audiobooks\_views\Titles\*`.
- The stock `vg_listview_albums_of_a_genre` view shows albums (one row per book).
- Tapping an album opens the track list (same problem as now — user sees track list).
- But the autostart marker fires on this route, so the daemon can pre-arm and auto-tap.

**Feasibility:** Medium. The route itself works (it was the original approach). The P8 route-table research exhaustively tested alternatives and concluded that `genre\Audiobook` is the safest available route.

**Pros:**
- The autostart marker fires — the daemon's existing autostart infrastructure works.
- No new binary patches needed (the route was used before).
- The daemon can pre-arm direct-open and auto-tap the first track.

**Cons:**
- **Back navigation stack:** The `genre\Audiobook` route passes through the stock Genres page on Back. This was the original UX problem that the native hub was designed to fix. P8 research concluded no clean route exists.
- **Track list still visible:** Even with the marker firing, the stock player still shows the track list before the daemon can tap. The marker fires when the album list opens, not when a specific album is tapped. The daemon must wait for the track list to appear, then tap.
- **Genre contamination:** The `genre\Audiobook` route means audiobooks appear under the stock Genres view, which is undesirable.
- **Regression:** Moving back to the old route abandons the native hub's clean entry point (Title, Author, Series, Folders rows).

**UX Quality:** Medium-Low. The Back navigation quirk returns, and the track list still flashes. The only improvement over current is that the autostart marker fires reliably.

**Risk:** Low for the route itself (it was used before and is known to work). Medium for UX regression (the Back stack issue was a significant user complaint).

**Effort:** Low. ~1–2 days to switch the route back and test.

**Verdict:** **Rejected as a standalone solution.** The Back navigation regression is unacceptable. However, the marker-firing reliability of this route is valuable — see Approach E which extends marker coverage to the filesystem view.

---

### Approach E: Hybrid — Filesystem View + Extended Autostart Marker

**Concept:** Keep the native hub with filesystem view rows (current architecture), but extend the autostart marker to also fire when a `.m3u` file is tapped from the explorer view. The resume daemon already polls the marker — if we make it fire for view-row taps too, the existing autostart mechanism handles everything.

**Mechanism:**
- Add a second hook point for the autostart marker code cave. Currently the marker hooks into `vg_listview_albums_of_a_genre` at `0x09FE40`. Add a second hook at the explorer's file-open callback (the code path that fires when a `.m3u` is tapped in `vg_listview_explorer`).
- When the explorer opens a `.m3u` file, the hook increments the marker sequence number at `0x8E4000`, just like the genre route does.
- The daemon detects the marker change, looks up the resume record, arms direct-open if needed, and taps the first track.

**Feasibility:** Medium. This requires finding the explorer's file-open callback address in `hiby_player` and adding a second hook point in the code cave. The existing marker code cave at `0x35DE00` has the infrastructure for writing the marker — it just needs an additional trigger.

The key question is: **what address does the explorer's `.m3u` open callback use?** This requires static analysis of `hiby_player` to find the explorer file-open path. The `shared_media_open` function at `0x49E200` is called *after* the track is selected, not when the `.m3u` opens. The `.m3u` open path goes through the explorer's file-type handler, which builds the track list and shows it. We need to hook *before* the track list is shown.

Alternative: hook at the point where the track list is *populated* from the `.m3u` (after parsing, before display). At that point, the daemon could pre-arm and tap the first track before the screen renders.

**Pros:**
- Uses the existing marker infrastructure — the daemon's autostart path is already built and tested.
- No visible track list if the marker fires early enough and the daemon taps fast enough.
- Keeps the native hub (no Back navigation regression).
- Graceful degradation: if the marker doesn't fire for some path, the daemon falls back to context + path-change detection.

**Cons:**
- **New binary patch:** Requires a second hook point in `hiby_player`. Any new binary patch is Moderate Risk (per the architecture review). It must be tested carefully via RAM-only probing first.
- **Finding the hook point:** Requires static analysis of the explorer's file-open path. The `hiby_player` binary is not symbolized. Finding the right callback address needs careful disassembly work.
- **Timing:** Even with the marker firing, the stock player may render the track list before the daemon can tap. The marker fires in `hiby_player`'s code path — the daemon polls it asynchronously. There's an inherent delay of at least one poll interval (1–2 seconds with current config, could be reduced to 200–500ms).
- **Register safety:** Adding a second hook point has the same register safety concerns as Approach C, though less severe since the hook only writes a marker (doesn't call `shared_media_open`).

**UX Quality:** Medium-High. The track list may flash briefly (200–500ms) depending on daemon poll frequency and tap speed. But the marker fires reliably, so the autostart + direct-open + track-restore chain works.

**Risk:** Medium. A new binary patch is required, but it's a *write a marker value* patch (similar to the existing `0x35DE00` cave), not a *call a function* patch (like Approach C). Writing a value to a known RAM location is much safer than injecting a function call.

**Effort:** Medium. ~4–6 days for static analysis to find the hook point, implement the second hook in `patch_hiby_player.py`, test via RAM-only, and tune timing.

**Verdict:** **Strong candidate.** Extends existing infrastructure with a minimal new patch. The new patch is low-complexity (write a value, not call a function). Keeps the native hub. Enables the existing autostart + direct-open chain. The main risk is finding the right hook point, which is a research problem, not a safety problem.

---

### Approach F: Custom List View in hiby_player

**Concept:** Build a custom list view in `hiby_player` that shows book titles (not files) and calls `shared_media_open` directly when tapped. This is "Option D" from the seamless-audiobook-design spec and the `audiobook_views_research.md` research.

**Mechanism:**
- Wrap the stock `vg_listview_book_list` generator at `0x005408a0` with a mode-flag pattern.
- Feed sidecar catalog files (`catalog-view-title.tsv`) as the list source.
- Replace the select handler at `0x00540a80` to call the audiobook title-open path directly, which would call `shared_media_open` with the correct track, bypassing the track list entirely.

**Feasibility:** Very Low on R1. The `audiobook_views_research.md` documents this as requiring deep binary patching of the stock Books list generator. The R3 Pro II uses a mode-flag + generator wrapper pattern, but this is unproven on R1. The `$s2` register experiment (`1.6.16.6-nativehub-s2-dev`) caused a reboot when trying to pass arguments to the generator. The stock generator wrapper on R1 is a different code path than R3 Pro II.

**Pros:**
- Completely seamless — no track list flash, no intermediate screens.
- Clean Back navigation (stays within native hub).
- Foundation for Author/Series views with real metadata.
- No daemon timing coordination needed — the patch handles everything synchronously.

**Cons:**
- **Highest risk:** Deep binary patching of the stock list generator. The `$s2` experiment proved this area is dangerous on R1.
- **Unproven pattern:** The R3 Pro II mode-flag + generator wrapper pattern has not been validated on R1. The R1's `vg_listview_book_list` may have different register conventions, stack layouts, or call sequences.
- **Long development cycle:** Weeks of RAM-only probing, with repeated reboot risk during testing.
- **No graceful degradation:** If the generator wrapper has a bug, the R1 reboots. There's no fallback path.
- **Out of scope:** The seamless-audiobook-design spec explicitly defers this to "Future evolution" after the seamless experience is validated via a simpler approach.

**UX Quality:** Perfect — true one-tap playback with no intermediate screens.

**Risk:** **Very High.** This is the most invasive change possible. The architecture review classifies this as High Risk. The `$s2` experiment is a known-bad package that caused reboots.

**Effort:** Very High. Weeks of reverse engineering and RAM-only testing, with high probability of needing multiple iterations.

**Verdict:** **Rejected for this iteration.** Too high-risk given the current state. The $s2 experiment proved this area is unsafe on R1. Should only be pursued after the seamless experience is working via a safer approach, and only with extensive RAM-only testing that proves the generator wrapper pattern is safe on R1.

---

## 3. Comparative Summary

| Approach | UX Quality | Risk | Effort | New Patches? | Track List Flash? | Saved Track? | Reboot Risk |
|---|---|---|---|---|---|---|---|
| **A: Framebuffer detect** | Medium | Low | Medium (3-5d) | No | Brief (100-300ms) | Only via post-load restore | Very Low |
| **B: Pre-arm on context** | High | Medium | High (5-8d) | No | None | Yes (if helper resolves book) | Low (ptrace) |
| **C: Binary patch auto-tap** | Perfect | **High** | Medium-High (5-10d) | Yes | None | Via post-load restore | **High** |
| **D: genre\Audiobook route** | Medium-Low | Low | Low (1-2d) | No | Brief | Via autostart + direct-open | Very Low |
| **E: Extended marker** | Medium-High | Medium | Medium (4-6d) | Yes (minimal) | Brief (200-500ms) | Yes via autostart + direct-open | Low |
| **F: Custom list view** | Perfect | **Very High** | Very High (weeks) | Yes (deep) | None | Yes (synchronous) | **Very High** |

---

## 4. Recommendation

### Primary Recommendation: Approach E + Approach A (Fallback)

**Approach E (Extended Autostart Marker)** as the primary mechanism, with **Approach A (Framebuffer Detection)** as a fallback layer.

#### Architecture

```
User taps title in Titles view
  → Stock explorer opens .m3u playlist
  → [NEW] Extended marker hook fires, increments seq at 0x8E4000
  → Resume daemon detects marker change within 200-500ms
  → Daemon looks up resume record for detected book
  → If saved_index > 1: arms direct-open helper with row_index = saved_index - 1
  → Daemon waits for track-list screen (framebuffer check, max 3s)
  → Daemon taps first track (touch_first_track)
  → shared_media_open fires
    → If direct-open armed: intercepts, forces saved track index
    → If not armed (new book): opens track 1
  → Playback starts on correct track
  → Daemon seeks to saved position
  → User sees Now Playing (track list flashed for ~200-500ms)

Fallback: If marker doesn't fire (hook missed, different code path):
  → Daemon's context window detects audiobook context
  → Daemon polls framebuffer for track-list screen
  → Daemon taps first track
  → Post-load track restore handles saved track via swipe+tap
```

#### Why This Combination

1. **E solves the detection problem:** The current auto-tap fails because the daemon can't see the `.m3u` in the path slot. The extended marker fires at the right moment in `hiby_player`'s code, giving the daemon a reliable signal.

2. **A provides fallback coverage:** If the marker hook misses (e.g., a `.m3u` opened from a different path, or the hook point is wrong), the framebuffer detection catches the track-list screen and taps anyway. This is strictly a fallback — slower but still functional.

3. **E enables direct-open pre-arm:** Because the marker fires *before* the track list renders, the daemon has a window to arm the direct-open helper. This means the correct track can be selected transparently — no visible swipe gestures.

4. **A provides screen verification:** Even with the marker, the daemon should verify the track-list screen is visible before tapping. Framebuffer check prevents tapping into the wrong screen.

5. **No high-risk binary patches:** The only new patch is a marker-write hook (write a sequence number to a RAM location), which is the same pattern as the existing `0x35DE00` cave. No function calls are injected. No registers are repurposed beyond reading `$a0` for the path (which is already the standard MIPS calling convention).

6. **Graceful degradation:** 
   - Marker fires + direct-open works → best case, correct track plays
   - Marker fires + direct-open fails → track 1 plays, daemon does post-load restore
   - Marker misses + framebuffer detects → track 1 plays, daemon does post-load restore
   - Marker misses + framebuffer misses → user manually taps track (current behavior)

#### What's Needed

| Component | Change | Risk |
|---|---|---|
| `patch_hiby_player.py` | Add second marker hook at explorer file-open callback | Medium (new patch, but write-only) |
| `hiby_player` binary | New code cave at unused region (e.g., `0x360B00`–`0x360BFF`) | Medium (needs RAM-only testing) |
| `src/state.c` | Replace `auto_tap_first_track()` path-based detection with marker-based detection | Low (daemon-side, no patch) |
| `src/state.c` | Add framebuffer-based track-list detection as fallback | Low (daemon-side) |
| `src/state.c` | Add pre-arm direct-open before auto-tap when marker fires | Low (uses existing helper) |
| `src/config.c` | Add `autotap_screen_check` and `autotap_fb_poll_us` config params | Low (additive) |
| `src/ui.c` | Add high-frequency track-list polling function | Low (read-only fb access) |

### Secondary Recommendation (Future): Approach B

Once the E+A approach is working and stable, consider enhancing the direct-open helper to support "detect book from path" mode (Approach B). This would eliminate the track-list flash entirely by intercepting `shared_media_open` with runtime path resolution. This is a natural evolution — the E+A approach provides the autostart signal, and B enhances the interception. But B is too complex for the initial implementation.

---

## 5. Risk Assessment

### 5.1 What Could Reboot the R1

| Risk Source | Approach | Probability | Mitigation |
|---|---|---|---|
| New binary patch in `hiby_player` (marker hook) | E | Low-Medium | RAM-only testing before flashing; write-only patch (no function calls); use same pattern as existing `0x35DE00` cave |
| Direct-open helper ptrace patch | E (pre-arm) | Very Low | Helper already tested; has timeout + auto-restore; validates probe cave addresses |
| Framebuffer read during high-frequency polling | A | Very Low | Read-only access; already used for back-guard |
| Touch event injection | E+A | Very Low | Already used for track selection, back navigation, seek bar |
| Register corruption from new hook | E | Low | Hook only reads `$a0` (path argument) and writes to a fixed RAM address; does not modify any registers; uses a code cave in unused space |

### 5.2 What Could Break Existing Functionality

| Risk | Probability | Mitigation |
|---|---|---|
| New hook interferes with stock explorer file-open | Low | Hook only writes a marker value; does not modify control flow; returns to original instruction immediately |
| High-frequency framebuffer polling increases CPU | Low | Only active during context window (30s after audiobook activity); negligible compared to existing polling |
| Auto-tap fires on wrong screen | Low | Framebuffer screen verification before tapping; max wait timeout |
| Direct-open helper times out during pre-arm | Low | Helper timeout is 6s; daemon falls back to track 1 + post-load restore |
| Music playback affected | Very Low | Auto-tap only fires when marker changes; music paths don't trigger marker; framebuffer check confirms track-list screen |

### 5.3 Risk Ranking

1. **Finding the hook point (E):** The explorer file-open callback must be located via static analysis. If it can't be found, E falls back to A-only (which works but with visible track-list flash). This is a research risk, not a safety risk.
2. **Hook point correctness (E):** The hook must not corrupt the explorer's state. Writing a marker value is safe, but the hook code itself (save registers, write marker, restore registers, return) must be correct MIPS assembly. This is testable via RAM-only probing.
3. **Timing coordination (E+A):** The daemon must detect the marker, arm direct-open, verify the screen, and tap — all within ~500ms. If timing is off, the track list flashes longer or the tap misses. Configurable delays and fallback paths mitigate this.

---

## 6. Can This Be Done Without New Binary Patches?

**Partially.** The fallback layer (Approach A: framebuffer detection) requires **no new binary patches**. It uses existing framebuffer reads and touch injection. However, without the extended marker (Approach E), the daemon has no reliable signal that a title was tapped — it can only detect the track-list screen after it appears, which means:

1. The track list is visible for at least 100–300ms before the daemon can tap.
2. The daemon can't pre-arm direct-open (it doesn't know which book was tapped).
3. Track restore for multi-track books requires visible swipe gestures (post-load restore).

With the extended marker (Approach E), the daemon gets an early signal, can pre-arm direct-open, and can tap before the track list fully renders. The new binary patch is minimal (write a marker value, same pattern as existing patch), but it does require static analysis and testing.

**If no new binary patches are acceptable:** Approach A alone provides a degraded but functional experience. The track list flashes briefly, and multi-track restore uses swipe+tap. This is still better than the current behavior (user must manually tap a track).

**Recommendation:** Implement A first (no patches, immediate improvement), then add E (minimal patch, significant improvement) after static analysis confirms the hook point.

---

## 7. Implementation Phases

### Phase 1: Framebuffer Auto-Tap (No Binary Patches)

**Goal:** The daemon detects the track-list screen via framebuffer and auto-taps the first track.

**Steps:**
1. Add high-frequency framebuffer polling function to `ui.c` (polls every 200ms during context window)
2. Replace the broken path-based `auto_tap_first_track()` in `state.c` with framebuffer-based detection
3. When `audiobook_track_list_visible()` returns true, tap first track
4. Test: tap a title, verify track list flashes briefly, playback starts automatically

**Deliverable:** Runtime-installable daemon update. No flashing required.

**Acceptance:**
- Tapping a title starts playback within 1–2 seconds
- Track list is visible briefly (100–500ms)
- New books start from track 1
- Multi-track books start from track 1, then daemon does post-load restore

**Risk:** Very Low — daemon-side only, no binary patches.

### Phase 2: Static Analysis for Hook Point

**Goal:** Find the explorer file-open callback address in `hiby_player` for the extended marker hook.

**Steps:**
1. Disassemble `hiby_player` around the explorer file-open path
2. Trace from the `.m3u` tap handler through to the track-list population
3. Identify a safe hook point where a marker write won't corrupt state
4. Document the hook address, registers, and call sequence
5. Test the hook via RAM-only probing (write marker, verify daemon detects it)

**Deliverable:** Documented hook point with RAM-only test results.

**Risk:** Low — RAM-only testing, no persistent changes.

### Phase 3: Extended Marker + Direct-Open Pre-Arm

**Goal:** The marker fires on `.m3u` open from the explorer view. The daemon pre-arms direct-open and auto-taps before the track list is fully visible.

**Steps:**
1. Add the second marker hook to `patch_hiby_player.py`
2. Modify `state_maybe_autostart()` to pre-arm direct-open before tapping
3. Tune timing: marker detection → direct-open arm → framebuffer verify → tap
4. Test: tap a title with saved_index > 1, verify correct track plays, minimal flash

**Deliverable:** Firmware package with extended marker patch.

**Acceptance:**
- Tapping a title with saved_index > 1 starts playback on the saved track within 2 seconds
- Track list flash is < 500ms
- If direct-open fails, falls back to Phase 1 behavior (auto-tap + post-load restore)
- Music playback unaffected

**Risk:** Medium — new binary patch, but write-only and tested via RAM-only first.

### Phase 4: Tuning and Edge Cases

**Goal:** Tune timing parameters and test all edge cases from the seamless-audiobook-design spec.

**Deliverable:** Tuned configuration and test report.

---

## 8. Why the Current Auto-Tap Approach Fails — Technical Detail

The `auto_tap_first_track()` function in `state.c` (lines ~900–970) checks:

```c
/* Must contain _views/ (or _views\) and .m3u */
bool has_views = false;
bool has_m3u   = false;
// ... scans path for "_views" + separator and ".m3u"
```

This check runs inside `state_poll_cycle()` when the path changes (line ~1015):

```c
/* Phase 2: Auto-tap first track when .m3u playlist opens */
auto_tap_first_track(rt, cfg, path);
```

The `path` variable is populated from `current_path_from_hex()`, which reads the `user.ini` offset 40 path slot. This slot contains the **currently playing audio file path**, not the currently open view path.

**Timeline of a title tap:**

```
T=0ms:   User taps "Book.m3u" in Titles view
T=50ms:  Stock explorer opens Book.m3u, parses playlist entries
T=100ms: Stock player builds track list from .m3u contents
T=150ms: Stock player displays track list view on screen
T=200ms: Stock player auto-loads track 1 (some firmware behavior)
         OR user would manually tap a track
T=2000ms: Daemon poll cycle reads path slot → sees "a:\Audiobooks\Author\Book\01.mp3"
T=2000ms: Daemon checks: has_views? No (path is the .mp3, not the .m3u)
T=2000ms: Daemon skips auto-tap → at_skipped++
```

The `.m3u` path is in the slot for at most ~100–200ms (between parse and track load). The daemon's 2–5 second poll interval makes it statistically impossible to catch.

Even reducing the poll interval to 200ms wouldn't reliably catch it — the timing would need to align perfectly with the ~100ms window.

**This is a fundamental design flaw:** The path slot is not a view-state indicator. It's a playback-state indicator. Using it to detect view transitions is unreliable.

---

## 9. Conclusion

The current auto-tap approach is fundamentally broken because it uses a playback-state indicator (path slot) to detect a view transition. No amount of tuning or timing adjustment can fix this — the `.m3u` is simply not in the path slot long enough for the daemon to see it.

The recommended path forward is:

1. **Immediate (Phase 1):** Switch to framebuffer-based detection (Approach A). This works today with no binary patches and provides a degraded but functional auto-play experience.

2. **Near-term (Phase 2-3):** Add the extended autostart marker (Approach E) to provide reliable, early detection of title taps. This enables direct-open pre-arm and minimizes the track-list flash.

3. **Future:** Consider Approach B (pre-arm with runtime path resolution) to eliminate the track-list flash entirely. This requires significant helper enhancement but builds on the E+A foundation.

The high-risk approaches (C: binary patch auto-tap, F: custom list view) are rejected due to reboot risk. The old route (D: `genre\Audiobook`) is rejected due to Back navigation regression.

---

*End of assessment.*