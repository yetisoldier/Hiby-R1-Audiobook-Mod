# Task 1 Binary Patch Analysis

## Patch Goal

Patch `hiby_player` so title taps in the audiobook flow stop going through the track-list path and instead go straight into `shared_media_open` from the native title-open wrapper at `0x540A80`.

This is a first-draft firmware patch, not a final behavioral guarantee. The important structural change is that the wrapper branch is intercepted early, audiobook paths are recognized locally, and non-audiobook paths continue through the stock wrapper unchanged.

## Patch Strategy

1. Redirect the first instruction pair at `0x540A80` to a new code cave.
2. Recreate the original prologue and selection lookup in the cave so the stock wrapper state is preserved.
3. Detect audiobook paths with an ASCII substring test for `Audiobooks`.
4. If the path matches, read the track index from the shared scratch slot at `0x8E4400` and call `shared_media_open` directly.
5. If the path does not match, restore `a0` and jump back into the stock wrapper at `0x540AD0` so the original route creation continues normally.
6. Keep the patch behind the explicit `--audiobook-direct-open` flag in `tools/patch_hiby_player.py`.

## Chosen Code Cave

- Cave address: `0x360F00`
- Why this location: it sits after the existing native-hub marker/view caves in the same zero-padded slack region, and nothing else in `patch_hiby_player.py` currently claims that offset.
- Size: the direct-open stub is 57 words before the trailing `Audiobooks` string, so the cave needs a little over 228 bytes for code plus string data.
- Safety property: the patch only touches a new cave and the 8-byte entry stub at `0x540A80`; the rest of the stock wrapper remains intact.

## MIPS Flow

The cave keeps the original wrapper shape and only changes the dispatch decision.

1. Save `ra`, `s0`, `s1`, and `s2` on the stack.
2. Call `0x456BE0` to get the current selection object.
3. Follow the same object/vtable path the stock wrapper uses to obtain the current title/path string.
4. Call `0x46E040` exactly like the original wrapper does.
5. Preserve the resolved context pointer in `s2`.
6. Compare the resolved path against the literal `Audiobooks` using `strstr` at `0x839F80`.
7. On a match, load the track index from `0x8E4400`, clear `a3`, restore `a1` to the title path string, and call `shared_media_open` at `0x49E200`.
8. On a miss, restore `a0` from `s2` and jump back to `0x540AD0`, which is the stock continuation that still builds the normal route.
9. Return through the same wrapper epilogue shape the stock code uses.

## How Audiobook Paths Are Detected

The direct-open wrapper treats any title path containing the ASCII substring `Audiobooks` as an audiobook path.

That is intentionally simple:

- It matches the `/Audiobooks` layout used by the project.
- It does not require the player to understand the daemon catalog or resume store.
- It keeps music paths on the stock branch.

If the substring test fails, the wrapper falls back to the stock title-open path unchanged.

## How `shared_media_open` Is Called

The call is made with the stock wrapper state preserved as much as possible:

- `a0` comes from the resolved wrapper context loaded from `s0->field_D8`.
- `a1` is restored to the resolved title/path string in `s1`.
- `a2` is loaded from the shared scratch slot at `0x8E4400`.
- `a3` is cleared to zero in this first draft.

The scratch-slot convention is the key bridge to the daemon:

- If the daemon does nothing, `0x8E4400` is zero and the player opens track 0.
- If the daemon later writes a saved zero-based track index there before the tap, the player will use that value instead.

That satisfies the current safe default and leaves room for the resume-aware path in the later daemon task.

## Register Preservation

The cave preserves the callee-saved registers it touches:

- `s0`
- `s1`
- `s2`
- `ra`

Caller-saved registers are allowed to be clobbered by the helper calls because the wrapper rebuilds the values it needs before the direct-open call or the stock continuation.

## Back Navigation

Back behavior is preserved by returning through the stock wrapper epilogue instead of building a new UI surface.

- Audiobook hits jump straight to `shared_media_open` and then exit via the normal success epilogue.
- Non-audiobook hits return to the stock continuation at `0x540AD0`, so the original route stack is still created for music and other content.

Because the patch does not create a track-list screen, there is no extra playlist layer to unwind with Back.

## Risk Mitigation

- The patch is behind `--audiobook-direct-open`, so it can be disabled independently of the rest of the audiobook patches.
- The entry hook only replaces the first 8 bytes at `0x540A80`; the rest of the function is still present in the binary.
- The audiobook check is narrowly scoped to the path substring `Audiobooks`, which keeps the music path on the stock behavior.
- The fallback path returns to the stock wrapper continuation instead of duplicating the entire native flow.
- The scratch-slot default of zero means a missing daemon update does not crash the player or block playback.

## Daemon Dependency Notes

This patch is only the player-side half of the story.

The daemon should eventually:

- populate `0x8E4400` with the saved zero-based track index before the title tap,
- clear or refresh that scratch slot when a new book starts,
- and stop relying on the late `.m3u` marker path for title taps once the direct-open wrapper is stable.

Until that daemon work lands, the player-side patch still behaves safely by defaulting to track 0.
