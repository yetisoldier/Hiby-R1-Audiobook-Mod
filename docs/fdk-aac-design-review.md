# FDK-AAC Design Review

Date: 2026-07-11  
Reviewer: Bob, Architect

## Executive Summary

Using the device's `libfdk-aac.so.2.0.1` directly from the current audiobook app is **not** the best design choice.

The app is built as a `mipsel-linux-musleabi` static binary, while the device library is a stock-system shared object that was built for the firmware's libc environment. That creates a deployment and ABI risk that is unnecessary for an audiobook player.

**Recommended approach:** vendor FDK-AAC source and compile it into the app as a static backend, while keeping MP4/M4B container parsing separate from AAC decode.

That preserves the current build model, avoids firmware coupling, and keeps the app resilient to future firmware changes.

---

## 1. Recommended Approach

### Options considered

#### a) `dlopen()` / `dlsym()` the device library at runtime

Not recommended.

This only works safely when the shared object is built for the same libc/ABI family as the host process. A musl-linked app should not depend on a glibc-built DSO from the stock firmware. Even if the loader appears to accept the file, symbol resolution and libc expectations are not a stable foundation for production use.

This option also makes AAC support disappear if the firmware library changes, moves, or is removed.

#### b) Drop `-static` and link dynamically with `-lfdk-aac`

Not recommended.

This would require aligning the app with the stock firmware's dynamic loader and libc model. That is a larger architectural shift than the feature warrants, and it weakens the app's portability and recovery story. It also still leaves the app coupled to firmware packaging.

#### c) Build FDK-AAC from source and link it statically

**Recommended.**

Reasons:

- Keeps the current static-binary strategy intact.
- Avoids cross-libc ABI risk.
- Removes dependence on a firmware-provided `.so`.
- Makes behavior deterministic across firmware updates.
- Allows the app to continue starting even if the firmware removes or replaces the native library.

If size is a concern, build only the AAC profile(s) the app needs rather than enabling every upstream codec mode.

### Final recommendation

Use **static FDK-AAC from source** for AAC decoding. Treat the firmware's `libfdk-aac.so` as an implementation detail of `hiby_player`, not as a dependency of the new audiobook app.

---

## 2. ABI Compatibility Assessment

### Short answer

There is a real ABI risk. A musl-compiled process should not assume it can safely `dlopen()` a glibc-built shared library on MIPS.

### Why this is risky

- The app is built with `zig cc -target mipsel-linux-musleabi -static -s`.
- The firmware library lives in the stock system's libc world, not the app's musl world.
- glibc DSOs can depend on symbol versioning and runtime conventions that musl does not provide.
- Shared objects often rely on the loader and libc family they were built against, even when the public API looks simple.

### Practical conclusion

`dlopen()` is only a safe design when the plugin and host process share a compatible libc/ABI contract. That is not established here, so the design should not depend on it.

### Safe alternatives

- Static vendor build of FDK-AAC in the app.
- A separate helper process compiled against the stock firmware libc, with IPC between processes.

Of those, the static vendor build is the cleanest fit for the current architecture.

---

## 3. Firmware Packaging Impact

### If we statically vendor FDK-AAC

No special rootfs overlay is required for AAC decode.

The app ships its own decoder code, so firmware updates that remove or replace `/usr/lib/libfdk-aac.so.2.0.1` do not affect audiobook playback.

### If we tried to rely on the firmware `.so`

We would need:

- A runtime compatibility check before enabling AAC/M4B playback.
- A fallback path when the library is missing or fails to load.
- Ongoing validation against each firmware release.

That is fragile and not worth it for core playback functionality.

### Bottom line

Do **not** make the rootfs overlay depend on the native library being present. The overlay can stay focused on app assets, configs, and launch integration.

---

## 4. Architecture Document Impact

### Playback engine

Section 3.2 of `docs/audiobook-app-architecture.md` should be updated.

Current text says:

- `M4B / M4A / AAC-in-MP4` -> `libmp4ff` + `libfaad2`

Recommended replacement:

- `M4B / M4A / AAC-in-MP4` -> `MP4 container parser + libfdk-aac`

Important nuance:

- `libfdk-aac` is the AAC decoder, not the MP4/M4B container demuxer.
- The app still needs a container parser to locate samples, timestamps, and embedded chapter/metadata boxes.
- If `libmp4ff` is already the chosen parser, keep it; only swap the AAC decode backend.

### Build process

The build section should be updated to say:

- build FDK-AAC from source as part of the app build, or vendor it as a static library
- do not depend on the firmware's shared object
- keep the main app `-static`

### Memory budget

No material redesign is needed.

FDK-AAC itself should fit inside the existing decoder budget. The current `decoder buffers` line item can stay as-is unless testing shows the MP4 parser or AAC state needs more headroom.

### Dependency list

Remove any requirement for `libfaad2`.

Keep or add:

- MP4/M4B container parser
- static FDK-AAC backend
- existing `libsqlite3`, `pthread`, `m`

No new runtime dependency on the firmware's `libfdk-aac.so` should be added.

### Crash recovery design

No structural change is required, but AAC initialization failures should be treated as normal decoder failure paths:

- fail the book open cleanly
- preserve current state
- surface a user-visible error
- do not crash the app

If a runtime-loaded backend is ever attempted, library load failure should degrade to a predictable unsupported-format error, not a fatal process error.

---

## 5. Build Process Changes

### Current state

`app/build.sh` currently builds a fully static binary and links only `-ldl -lm -lpthread` in addition to the object sources.

### Required changes

- Add FDK-AAC source files or a static archive build step.
- Wire the AAC backend into `app/src/decoder.c` and `app/src/decoder.h`.
- Keep the main app static.
- Continue to avoid a hard runtime dependency on the firmware library.

### Suggested build rule

Use a narrow FDK-AAC build configuration for audiobook playback only. That keeps binary size and CPU cost down while preserving compatibility with M4B/AAC content.

### Not recommended

Do not convert the whole app to a dynamically linked binary just to consume this one decoder.

---

## 6. App Code Changes

### Decoder layer

The current decoder dispatch only handles:

- WAV
- FLAC
- MP3

It needs a new AAC/M4B path.

Recommended changes:

- extend `decoder_kind`
- add an MP4/M4B container-backed AAC decoder implementation
- keep all AAC-specific details behind the decoder abstraction
- preserve the current player contract so the rest of the app does not care which backend is in use

### Player layer

`app/src/player.c` should not need a redesign.

It already opens a track, seeks after decoder open, and writes PCM to ALSA. The main requirement is that the new AAC backend expose the same open/seek/read/close behavior as the current decoders.

### Error handling

Add clear failure paths for:

- unsupported AAC profile
- bad MP4 container
- missing chapter metadata
- decoder init failure

Those should map to a normal playback error, not a crash.

---

## 7. Risk Assessment

### Primary risks

1. **ABI mismatch**
   - The biggest risk if the app tries to load the firmware `.so` directly.

2. **Firmware drift**
   - A future firmware update could remove, rename, or replace the shared object.

3. **Container/codec split**
   - FDK-AAC alone is not enough for M4B.
   - The app still needs MP4 sample extraction and timestamp mapping.

4. **Seek correctness**
   - AAC-in-MP4 seek behavior depends on the container parser and sample index handling, not just the decoder library.

5. **Licensing/build friction**
   - Bringing in FDK-AAC source means the build must explicitly own that dependency instead of inheriting it from the firmware.

### Mitigations

- statically vendor FDK-AAC
- keep MP4 parsing explicit and test it separately
- make AAC support optional at build time, but not firmware-dependent
- add playback tests for M4B open/seek/resume on device

---

## 8. Fallback Plan If `dlopen()` Fails

If the project insists on experimenting with runtime loading, the fallback should be:

1. Probe the library at startup.
2. If it loads, enable the AAC backend.
3. If it does not load, fall back to the statically compiled backend or disable AAC playback cleanly.

However, I do **not** recommend making the app depend on this path.

The better fallback is simply to never depend on `dlopen()` for core audiobook playback.

---

## 9. Other Design Impacts

### IPC between app and daemon

No direct impact.

The resume daemon contract does not change because the AAC backend is a local playback detail.

### Music/audiobook mode switching

No change.

This remains a separate audiobook app and does not alter `hiby_player`.

### Bluetooth routing (`bluealsa`)

No change.

Audio still ends at ALSA, so backend choice does not affect routing.

### Framebuffer/UI rendering

No change.

The UI only needs to show the same track/book state and any decode errors.

### Database schema and M4B chapter extraction

No schema change is required just because the AAC backend changes.

If chapter extraction currently relies on MP4 metadata, that parser may need to be shared with the decoder path, but the logical schema itself stays the same.

---

## 10. Overall Verdict

**CONCERNS FOUND**

The feature itself is viable, but the proposed use of the device's native `libfdk-aac.so` is not safe as a direct dependency for the current static musl app.

Proceed only if the implementation is changed to:

- vendor and statically link FDK-AAC, or
- move AAC decode into a separate stock-libc helper process

The rest of the audiobook architecture does not require major changes beyond the decoder/backend and build-system updates described above.
