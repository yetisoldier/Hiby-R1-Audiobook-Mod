# Phase 2 Core Playback Implementation Plan

This plan compares the audiobook feature spec with the current firmware architecture and code, then turns that into an implementation order for Phase 2: Core Playback.

Sources reviewed:
- `docs/audiobook-feature-spec.md`
- `docs/audiobook_firmware_architecture.md`
- `docs/seamless-audiobook-design.md`
- `docs/architecture-review-2026-07-10.md`
- `docs/audiobook-architecture-redesign.md`
- `docs/auto-tap-architecture-analysis.md`
- `src/state.c`
- `src/state.h`
- `src/ui.c`
- `src/resume.c`
- `src/catalog.c`
- `tools/patch_hiby_player.py`
- `work/hiby_player.disasm`

## A. Gap Analysis

### 1. Core user experience and direct launch

- Already implemented: The project already has a dedicated Audiobooks entry point, generated audiobook views, and a separate resume store under `/usr/data/audiobooks/resume.d/`.
- Already implemented but needs to change: The current launch path still routes through generated `.m3u` files and a stock track-list screen. `src/state.c` still contains `auto_tap_first_track_fb()`, and the main poll loop still calls it on path changes.
- Missing entirely: A direct book-open path that lands on Now Playing with no playlist flash, no track-list flash, and no synthetic tap.
- Contradicts the spec: The framebuffer-based auto-tap path, touch injection, and back-guard touch correction are all explicitly disallowed by the spec.

### 2. Book model and internal queue

- Already implemented: `catalog.c` provides path-based lookup, track index, track count, media ID, and book-key retrieval for flat audiobook rows.
- Already implemented but needs to change: The current model is still row-centric and path-centric. It does not expose a first-class `Book` plus ordered `Track` collection in the runtime.
- Missing entirely: A real book object that owns the ordered queue and can be opened directly by book ID, not by playlist file or track row.
- Contradicts the spec: The current `.m3u` playlist wrapper is an implementation convenience, not the user-facing book model the spec requires.

### 3. Resume and progress behavior

- Already implemented: `src/resume.c` already stores schema version 3 records with `book_key`, `root_hiby_path`, `current_path`, `track_index`, `track_count`, `position_ms`, and `completed`.
- Already implemented but needs to change: `resume.c` can compute restore targets and save progress, but its restore functions are still partially stubbed or orchestration-only. `state.c` still drives restore around screen/path heuristics instead of a clean playback event boundary.
- Missing entirely: Event-driven resume keyed off decoder-ready or track-loaded state, smart rewind policy by pause duration, and safe handling for track changes without UI guessing.
- Contradicts the spec: The current restore flow still depends on framebuffer screen recognition, synthetic taps, and timing windows.

### 4. Completion handling

- Already implemented: `state.c` can detect end-of-book conditions by comparing current position against duration and mark the resume record completed.
- Already implemented but needs to change: Completion logic is tied into the existing state machine and must be preserved after the UI automation code is removed.
- Missing entirely: User actions such as Mark as Finished, Restart Book, and explicit unfinished state handling.
- Contradicts the spec: Completion should not depend on proximity to a visual screen state or on the `.m3u` track-list launch path.

### 5. Playback queue behavior and chapter transitions

- Already implemented: The code already knows how to look up track index, track count, and path ordering from the catalog.
- Already implemented but needs to change: The daemon currently uses touch-row selection, swipe geometry, and key-event fallbacks to get to the right track.
- Missing entirely: A private book queue that survives track-to-track transitions without exposing a music-style queue to the user.
- Contradicts the spec: The auto-tap and row-selection logic is exactly the kind of UI automation the spec says must be removed.

### 6. Now Playing, speed, sleep, bookmarks

- Already implemented: The stock player already owns the real Now Playing screen, playback speed, sleep timer, and physical control behavior.
- Already implemented but needs to change: The audiobook layer still tries to manipulate the player through screen heuristics and touch synthesis instead of letting stock playback run once the correct track is opened.
- Missing entirely: Audiobook-specific UI polish such as chapter list, bookmark UI, and book-progress display.
- Contradicts the spec: Any attempt to keep using injected taps to manage playback state is outside the desired architecture.

### 7. Folder entry, search, authors, series, storage architecture

- Already implemented: The helper and catalog pipeline already track enough metadata to support title, author, and series views at the data level.
- Already implemented but needs to change: The current architecture still stores audiobook data in a flattened catalog plus generated view files, rather than a dedicated audiobook database.
- Missing entirely: Search scoped only to audiobook catalog, true author and series views, and the phase-3 book/chapter UI.
- Contradicts the spec: Re-using the stock music database as the primary audiobook store would reintroduce the exact leakage the spec forbids.

### 8. Performance, memory, and operational safety

- Already implemented: The architecture review confirms the project already uses a conservative patch surface and keeps the stock decoder, Now Playing, Bluetooth, EQ, and physical controls untouched.
- Already implemented but needs to change: `src/ui.c` allocates framebuffer buffers and writes synthetic input event streams; `src/state.c` opens `/dev/fb0`, polls it, and uses sleep-based delay chains.
- Missing entirely: A no-UI-automation playback path that stays asleep when idle and only reacts to playback state.
- Contradicts the spec: Framebuffer reading, screen classification, touch injection, and timing-based user simulation are all explicitly disallowed.

## B. Architecture Decision: How To Trigger Direct Book Open

The spec requires that tapping a title go directly to Now Playing with no intermediate screen. The current architecture does not satisfy that because the `.m3u` route is late, the framebuffer detector is reactive, and the touch injector is the mechanism that makes the experience appear to work.

### Option 1: Patch the title-open handler at `0x540A80`

- Feasibility: High enough to be the best near-term path. The disassembly shows `0x540A80` is a wrapper around the title-open path, and `0x540B0C` immediately hands off to `0x5401C0`. That is the right level to redirect if we want a book-selection hook rather than a generic list helper.
- Crash risk: Moderate. This is still a `hiby_player` patch with MIPS calling-convention risk, but it is a localized wrapper rather than a global list engine edit.
- No track-list flash: Yes, if the wrapper is patched to call `shared_media_open` directly with the saved track index and then returns through the stock cleanup path.
- Implementation complexity: Medium to high, because the hook must preserve registers, resolve the resume record, and pass the right track index without disturbing the stock back stack.
- Back navigation: Best of the four options. Because the entry point remains the stock title-open wrapper, Back can still unwind through the native hub / Titles stack instead of a playlist stack.

### Option 2: Patch the explorer open path for audiobook `.m3u`

- Feasibility: Medium. The disassembly around `0x4EFE00` confirms this is a `.m3u` callback, which is reachable and patchable.
- Crash risk: Lower than a deep ABI rewrite, but still non-trivial because the explorer path is shared and the open helper is a generic route.
- No track-list flash: No, not reliably. This hook fires after the playlist/file view is already on screen, which is too late for the spec requirement.
- Implementation complexity: Medium. The path is easier to intercept than the title wrapper, but the user experience remains wrong even if the patch is stable.
- Back navigation: Worse than option 1. You are still traversing the explorer and playlist stack, so Back inherits an extra UI layer.

### Option 3: Keep `.m3u` as an internal queue format but open it programmatically

- Feasibility: Medium. The stock format can remain an internal implementation detail, and the current code already knows how to map a book to its track list.
- Crash risk: Lower than a full UI rewrite if it stays close to stock open semantics.
- No track-list flash: Only if it is truly opened programmatically from a book-selection hook. If it still goes through the existing `.m3u` user path, it fails the spec.
- Implementation complexity: Medium. As a fallback pattern this is attractive, but it still depends on a clean trigger point and direct-open logic, so it does not really simplify the problem.
- Back navigation: Acceptable if invoked from a native book-selection callback, but still second-best because the hidden `.m3u` indirection makes the stack harder to reason about.

### Option 4: Build a custom audiobook list view with its own selection callback

- Feasibility: Technically possible, but it is the most invasive option and requires more reverse engineering of `hiby_player` UI plumbing.
- Crash risk: Highest. This touches more stock UI behavior, more route code, and more stateful list handling.
- No track-list flash: Yes, eventually. This is the cleanest long-term user experience.
- Implementation complexity: High to very high. It is not the right Phase 2 move if the goal is to finish core playback on the existing schedule.
- Back navigation: Best long term, because the list view can own its own navigation model, but that benefit does not outweigh the implementation cost right now.

### Recommendation

Recommend Option 1: patch the title-open handler at `0x540A80`, with `0x540B0C` as the immediate follow-up anchor, and redirect the flow straight into `shared_media_open` with the saved track index.

Why this is the right choice:
- It targets the actual book-selection boundary, not the late `.m3u` callback.
- It is the only option that can plausibly remove the track-list flash without introducing a brand-new UI surface.
- It keeps the stock audio engine and stock Now Playing screen intact.
- It gives the best chance of preserving the expected Back stack.
- It lets the daemon stop pretending to be a UI robot and instead focus on progress persistence, restore coordination, completion detection, and smart rewind.

## C. Daemon Changes

### Remove

- Remove framebuffer classification from `src/ui.c`: `fb_white_pixels_region`, `ui_seek_screen_ready`, `audiobook_subheader_visible`, `audiobook_title_list_visible`, `audiobook_track_list_visible`, and `audiobook_global_back_target_visible`.
- Remove synthetic input generation from `src/ui.c`: `send_input_event`, `emit_touch_abs_frame`, `write_touch_tap_stream`, `ui_send_event_file`, `touch_generated_tap`, `touch_first_track`, `touch_back_to_track_list`, `touch_track_row`, `touch_track_swipe_up`, `track_next`, and `track_prev`.
- Remove screen-guard and play-mode enforcement that depend on synthesized input: `ui_seek_restore`, `ensure_audiobook_play_mode`, and the back-guard tap logic.
- Remove auto-tap state from `src/state.h`: `autotap_last_path`, `autotap_fired_at`, `autotap_fast_poll_until`, and `idle_autotap_fired`.
- Remove auto-tap orchestration from `src/state.c`: `auto_tap_first_track_fb()` entirely, the call at path-change entry, and the idle framebuffer scan in the non-audiobook branch.
- Remove sleep-based simulation logic from `src/state.c`: the 1-second follow-up tap, the track-row tap retries, and the back-to-track-list sleep chains used to work around UI timing.

### Keep

- Keep `src/resume.c` record persistence: `existing_record_for_path`, `save_position`, `restore_target_ms`, and the failure-tracking helpers.
- Keep `src/state.c` progress bookkeeping: last path, restored path, completed path, deferred overwrite protection, save bucketing, and completion detection.
- Keep `src/catalog.c` lookups: catalog row lookup, book-key lookup, and track-count/index association.
- Keep the main poll loop structure in `src/state.c`, but repurpose it from UI detection to playback-state monitoring.
- Keep the stock-player memory readers in `src/player.h` and the underlying implementations they call, because the daemon still needs position, duration, and path inspection.

### Add

- Add event-driven resume state in `src/state.c`: a dedicated "book opened, waiting for decoder-ready" state that does not assume a screen has drawn.
- Add direct-book-open coordination in `src/state.c`: when a book opens, load the saved resume record, resolve the saved track, and let the player open it directly instead of tapping into it.
- Add smart rewind policy in `src/resume.c` or a new helper module: rewind amount based on pause duration and reboot state, not on framebuffer timing.
- Add accidental-tap protection in `src/resume.c`: keep the previous position protected until playback has actually continued far enough or the user explicitly restarts.
- Add completion confirmation that depends on playback progress and end-of-book conditions, not UI state.
- Add verification hooks for open-state transitions, so restore can be retried once if the first seek is ignored.

### Practical interpretation for Phase 2

Phase 2 should not try to build a new player UI. It should make the existing stock player behave like a book player by:
- opening the correct track directly,
- preserving the correct resume record,
- saving progress reliably while playback is active,
- and never guessing about screens.

That means the daemon becomes a playback-state manager, not a touch robot.

## D. Implementation Plan

### Task 1: Patch the `hiby_player` title-open path to direct-open the saved track

- Files affected: `tools/patch_hiby_player.py`, `work/hiby_player.disasm` notes, and the generated patch artifact used by the build scripts.
- Complexity: L
- Dependencies: Finalized hook choice at `0x540A80` / `0x540B0C`, plus confirmation that the stock cleanup path is preserved.
- What it does: Replace the current title-open flow with a code cave that resolves the selected book, looks up the saved resume record, and calls `shared_media_open` directly with the saved zero-based track index.
- Verification criteria: Tapping a title goes straight to Now Playing, the track list does not appear, the stock audio engine still plays the correct track, and Back returns to the expected audiobook surface.

### Task 2: Remove all framebuffer and touch-injection plumbing from the daemon

- Files affected: `src/state.c`, `src/state.h`, `src/ui.c`, `src/ui.h`
- Complexity: L
- Dependencies: None, but it is safer to do after Task 1 has a verified player-side launch path.
- What it does: Delete the auto-tap path, delete screen classification, delete event-file touch generation, and delete the idle framebuffer detection loop.
- Verification criteria: The daemon binary no longer references `/dev/fb0`, no longer writes synthetic input events, and still builds cleanly with the remaining progress/save logic.

### Task 3: Rework the daemon state machine around playback events instead of screen state

- Files affected: `src/state.c`, `src/state.h`, `src/resume.c`, `src/player.h`
- Complexity: L
- Dependencies: Task 2 should land first so the old UI automation does not mask state-machine issues.
- What it does: Preserve path tracking, progress persistence, restore coordination, and completion detection, while adding a clean "opened -> ready -> restored -> tracking" flow.
- Verification criteria: A partially listened book resumes the correct file and position without any framebuffer checks, and a completed book starts from the beginning.

### Task 4: Add smart rewind and accidental-start protection

- Files affected: `src/resume.c`, `src/state.c`, possibly a small new helper source if the policy is cleaner when isolated
- Complexity: M
- Dependencies: Task 3, because smart rewind belongs to the same restore flow.
- What it does: Apply rewind windows based on pause duration, protect an old bookmark from being overwritten by an accidental restart at track 1 / position 0, and only commit a fresh record after playback is clearly real.
- Verification criteria: A short pause resumes exactly or near-exactly; longer pauses rewind by a configurable amount; a brief accidental tap does not destroy the old resume point.

### Task 5: Tighten completion detection and finish/restart behavior

- Files affected: `src/state.c`, `src/resume.c`, `src/resume.h`
- Complexity: M
- Dependencies: Task 3, because completion handling must stay inside the new event-driven state machine.
- What it does: Keep the existing natural end-of-book detection, but make the finished state and restart behavior explicit and safe.
- Verification criteria: Final-track EOF marks the book complete, finished books restart from the beginning, and manual seeking near the end does not erase progress.

### Task 6: Update patcher verification and regression tests for the new launch path

- Files affected: `tools/patch_hiby_player.py`, verification scripts, and any smoke-test harnesses that assert the old playlist behavior.
- Complexity: M
- Dependencies: Tasks 1 to 5 should exist so the tests can check the new flow end to end.
- What it does: Add assertions that the `.m3u` track list is not visible on launch, the direct-open hook is installed, and the daemon no longer depends on framebuffer state.
- Verification criteria: Automated tests prove title tap -> Now Playing, resume correctness, and no synthetic input path remains active in the shipped build.

## E. Risk Assessment

### What could break existing music playback

- The biggest risk is accidental overreach in the `hiby_player` patch. If the hook is inserted too broadly, it could affect non-audiobook title paths or generic open helpers.
- The second risk is removing code that other features quietly depended on. `src/ui.c` contains audiobook-specific helpers, but some of them are shared by the current audiobook flow, so removing them must be coordinated with the new player-side path.
- The third risk is overfitting the resume logic to one book shape and accidentally regressing track ordering or first-track start for new books.

### What could crash `hiby_player`

- A bad MIPS ABI handoff at `0x540A80` or `0x540B0C` can clobber saved registers or return to the wrong address.
- A wrong track index can dereference the wrong object inside `shared_media_open` or its follow-up helper.
- Patching the wrong shared helper, especially `0x5401C0`, could destabilize unrelated stock list flows.
- Any patch that assumes the wrong stock binary revision can write valid bytes to the wrong address and break the player outright.

### Rollback plan if the binary patch fails

- Keep the patcher gated behind explicit flags so the stock binary can remain untouched until the hook is verified.
- Retain the known-good current package artifact and the existing byte-level checks so reverting is a matter of swapping back to the previous patch set, not reconstructing the release from scratch.
- If the direct-open hook fails during bring-up, temporarily disable the new hook and ship a dev-only build with the old behavior only for diagnostics, not as the intended architecture.
- Do not attempt to salvage a broken patch by expanding the UI automation path. That would move in the wrong direction relative to the spec.

### Minimal viable change

- The smallest meaningful step toward the spec is: patch the title-open wrapper at `0x540A80` so a title tap opens the correct audiobook track directly, and remove the framebuffer/touch auto-tap path from the daemon.
- That gives the largest user-visible win with the smallest architecture surface: no track-list flash, no screen guessing, no synthetic taps, and less RAM pressure.
- After that, the remaining work is mostly correctness hardening for resume, smart rewind, and completion.

## Summary

The current implementation is strong on cataloging and resume bookkeeping, but it still uses the wrong mechanism to start playback. The Phase 2 target should be to move the launch decision into `hiby_player` at the title-open boundary and make the daemon a playback-state manager only.

Recommended execution order:
1. Direct-open title taps from `0x540A80`.
2. Delete framebuffer and touch automation from the daemon.
3. Rework restore, smart rewind, and completion around event-driven playback state.
4. Verify that music playback and stock controls remain untouched.
