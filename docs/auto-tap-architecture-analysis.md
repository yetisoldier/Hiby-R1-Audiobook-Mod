# Auto-Tap Architecture Analysis

> **Date:** 2026-07-11  
> **Scope:** Make audiobook auto-tap reliable and fast on the HiBy R1 without destabilizing the player or the resume daemon.  
> **Bottom line:** Do not rely on the current idle framebuffer scan as the primary trigger. The best near-term design is a hybrid: move the trigger earlier in `hiby_player`, then use a short, millisecond-resolution daemon-side arm window to tap immediately. Keep the existing idle scan only as fallback.

## 1. Current State

The current implementation has two partially overlapping paths in `src/state.c`:

- `auto_tap_first_track_fb()` fast-polls `/dev/fb0` after an audiobook path change.
- The idle path in `state_poll_cycle()` scans the framebuffer when the daemon thinks the player is idle and not already on an audiobook path.

There is also an attempted marker hook in `tools/patch_hiby_player.py`:

- `AUDIOBOOK_EXPLORER_MARKER` hooks the `.m3u` open callback at `0x4EFE00`.
- That hook is too late for the problem we are trying to solve.

The failure modes are real and structural:

- The `.m3u` hook fires only after the user has already tapped a playlist entry.
- The idle scan is gated by `!path_preview_is_audiobook(pp)`, which is inconsistent when the player keeps a music path in `user.ini`.
- The daemon loop is second-granularity by default (`interval_seconds = 5`), so the best-case response time is already above the target.
- `auto_tap_first_track_fb()` uses `time(NULL)` for its deadline, so the code is not truly millisecond-accurate even though the poll delay is expressed in ms.
- `audiobook_track_list_visible()` allocates memory on each call, which is not ideal on a device with roughly 2 MB free at runtime.

## 2. What the Code Is Actually Doing

### 2.1 Daemon-side behavior

In `src/state.c`, `state_poll_cycle()` does three things relevant to auto-tap:

1. Reads the `user.ini` path preview.
2. If the path is audiobook-like, enters tracking / restore logic.
3. If the path is not audiobook-like, runs the idle framebuffer scan.

The important detail is that the idle scan only happens in the non-audiobook branch:

- `if (cfg->autotap_enabled && !path_preview_is_audiobook(pp)) { ... }`

That means the idle scan is not a universal "track list visible" detector. It is intentionally suppressed whenever the daemon thinks the player is already in audiobook mode.

### 2.2 Framebuffer classifier behavior

`src/ui.c:audiobook_track_list_visible()` uses fixed white-pixel regions to classify the screen. That part is fine as a heuristic. The problem is not the classifier itself; the problem is when and how often it is called.

The classifier is also built on `fb_read_rows()`, which mallocs a buffer per probe. That is acceptable for occasional use, but not ideal for a tight retry loop on a memory-constrained device.

### 2.3 Player-side patch behavior

In `tools/patch_hiby_player.py`, the attempted marker hook at `0x4EFE00` is explicitly documented as the `.m3u` file-open callback. That is one step too late in the UI chain:

- native hub title row tap
- explorer view creation
- `.m3u` list shown
- user taps `.m3u`
- `.m3u` callback fires

By the time the callback fires, the stock player is already past the point where a "pre-play" signal is useful.

## 3. Option Review

### 3.1 Daemon-side fix only

This is the weakest option.

What it can improve:

- Make the idle scan less brittle.
- Reduce the chance of the `idle_autotap_fired` flag getting stuck.
- Cut some allocation churn.

What it cannot fix:

- It cannot reliably detect the UI transition that starts playback.
- It cannot guarantee under-1-second response with the current second-based poll loop.
- It still depends on framebuffer polling, which is inherently reactive and race-prone.

If we keep any daemon-only work, it should be treated as a fallback and hardening pass, not as the primary architecture.

### 3.2 Binary patch at `.m3u` open

This should not be the primary hook.

Why:

- It fires after the track list is already on screen.
- It is too late to eliminate the visible intermediate state.
- It only helps if the daemon can react fast enough, which is not guaranteed today.

If the goal is "tap first track automatically," the `.m3u` callback is the wrong anchor point.

### 3.3 Binary patch earlier in the player

This is the right place to intervene if we want a reliable trigger.

The earlier hook should be one of:

- the native hub title-row open helper,
- the explorer row path that creates `vg_listview_explorer`,
- or the view-push point immediately before the explorer is shown.

These are better than the `.m3u` open callback because they fire when the book is being selected, not after the user has already entered the playlist list.

The right goal for the hook is not "tap track 1 inside `hiby_player`". The right goal is "emit a trustworthy selection event early enough that the daemon can arm immediately."

### 3.4 Hybrid approach

This is the best answer.

Use the player patch only as an early event source. Keep the daemon as the actuator.

Why this is the best compromise:

- The player patch gives you the earliest useful signal.
- The daemon already knows catalog metadata and resume state.
- The daemon can choose row 1 or the saved track index.
- The player patch can stay simple if it only writes a marker or a small payload.
- The daemon remains the single place that decides whether to tap, restore, or fall back.

### 3.5 Completely new approach

The missing idea is this: the feature is not really about "detect when the track list appears." It is about "detect when the user chose a book."

That means the best trigger is not the `.m3u` open callback. It is the book-selection callback before the track list is shown.

If you can reliably identify the book at that point, you can:

- arm a short fast-poll window,
- or bypass the track list entirely by resolving the saved track up front.

## 4. Recommendation

### Primary recommendation

Move the trigger earlier in `hiby_player` and treat the daemon as a short-lived armed executor.

Concretely:

1. Replace the `.m3u`-open marker hook with a hook at the title-row / explorer-open stage.
2. Have that hook write a marker or payload into the existing scratch area.
3. In the daemon, add a new armed state that polls at 100-200 ms for no more than about 1 second after that trigger.
4. Tap the first track immediately when the track list is visible.
5. Keep the current idle scan as a slow fallback only.

### Why this is the best path

- It addresses the actual event boundary, not a late consequence.
- It avoids forcing all correctness onto framebuffer timing.
- It gives a path to sub-second behavior without trying to make the entire daemon run at 200 ms forever.
- It keeps the player patch simpler and safer than a direct `shared_media_open` bypass.

## 5. Daemon-Side Fixes Worth Doing

These are worth doing, but only as supporting changes.

### 5.1 Replace second-based timing with monotonic milliseconds in the autotap path

`auto_tap_first_track_fb()` currently computes deadlines with `time(NULL)`. That is too coarse for a <1 second goal.

Use `clock_gettime(CLOCK_MONOTONIC)` or an equivalent monotonic millisecond clock for:

- arm deadline,
- poll timeout,
- de-bounce window,
- and one-shot state expiry.

### 5.2 Reuse a framebuffer scratch buffer

`fb_read_rows()` should not allocate every time in the hot path.

Recommended change:

- add one reusable scratch buffer in `daemon_runtime` or a static module buffer,
- size it for the largest scan region actually used,
- reuse it across probes,
- reopen on error only when needed.

This reduces allocator churn and lowers the risk of fragmentation on a tiny-memory system.

### 5.3 Keep `/dev/fb0` open while armed

Opening and closing the framebuffer every 200 ms is unnecessary overhead.

Prefer:

- open once when the short arm window starts,
- reuse the fd during the polling burst,
- close it when the arm window ends or on error.

### 5.4 Fix the state machine, not just the flag

`idle_autotap_fired` is a brittle boolean.

Replace or augment it with:

- a generation counter tied to the selection trigger,
- an armed-until timestamp,
- and explicit reset on any new book-selection event.

That removes the "flag got stuck until screen changed" failure mode.

### 5.5 Keep the slow idle scan as fallback only

The idle scan can stay, but it should not be the primary mechanism.

If it remains:

- lower the cadence only when the daemon is actually armed,
- otherwise leave it at a low-frequency fallback,
- and never rely on it to catch the first tap of a new selection.

## 6. Player-Side Hook Recommendation

### Do not patch `0x4EFE00` as the primary hook

That hook is the wrong semantic level.

### Prefer one of these earlier hook points

- the native hub title-row selection callback,
- the explorer open helper that creates `vg_listview_explorer`,
- or the first view-push point before the explorer is displayed.

### If the goal is zero flash

The more aggressive option is to bypass the track list entirely and jump toward `shared_media_open` or the direct-open helper with the saved track already resolved.

That is possible in theory, but it is a later optimization, not the first fix I would ship.

Why not start there:

- It is more invasive.
- It increases the risk of player crashes.
- It requires more careful MIPS ABI and argument handling.
- It is harder to debug than a marker-plus-daemon design.

## 7. Risk / Benefit Summary

| Approach | Reliability | Latency | Risk | Recommendation |
|---|---:|---:|---:|---|
| Idle framebuffer scan only | Low | Medium | Low | Keep as fallback only |
| `.m3u` callback hook | Low | Medium | Medium | Not primary |
| Earlier title-row / explorer-open hook + daemon arm window | High | Low | Medium | Best option |
| Direct `shared_media_open` bypass | High if correct | Very low | High | Later optimization |

## 8. Final Verdict

The current architecture is close, but the trigger is anchored too late.

The best solution is a hybrid:

- use a player-side hook at the book-selection stage, not the `.m3u` open stage,
- use the daemon to execute a very short, monotonic-ms fast poll,
- and keep the existing idle framebuffer scan only as a safety net.

If you want the shortest path to a reliable <1 second experience, that is the design to implement.
