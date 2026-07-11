# Bob Re-assessment - 2026-07-11

Reviewer: Bob, Principal Software Architect  
Scope: Re-assess the hybrid architecture, earlier Bob documents, Forge's current work, and current `app/src` implementation.

## Executive Verdict

The hybrid direction still holds: keep stock `hiby_player` for music and ship audiobooks as a standalone app. I would not return to binary-patching `hiby_player` for audiobook playback.

However, the current code is not ready for a flash that Eric can trust. The largest blockers are no longer conceptual architecture questions. They are implementation and packaging correctness:

1. The app currently refreshes the library destructively on every start and deletes `progress` and `bookmarks`.
2. M4B files with embedded chapters can be turned into non-playable queue rows.
3. Runtime UI assets/fonts are referenced from `/usr/share/audiobooks`, but the firmware overlay does not currently install them.
4. The ALSA path is direct kernel PCM, not libasound/bluealsa plugin routing, so on-device audio routing remains unproven.
5. The old firmware packaging still contains legacy daemon/direct-open/touch artifacts that should not be part of the final standalone-app architecture unless deliberately kept as a separate compatibility layer.

Overall confidence in the architecture path: **medium-high**.  
Confidence in the current code as flash-ready: **low to medium** until the blockers above are fixed and smoke-tested on device.

## Inputs Reviewed

Documents reviewed:

- `docs/audiobook-app-architecture.md`
- `docs/hybrid-architecture-evaluation.md`
- `docs/phase2-implementation-plan.md`
- `docs/auto-tap-architecture-analysis.md`
- `docs/fdk-aac-design-review.md`
- `docs/audiobook-feature-spec.md`
- `docs/audiobook-app-test-plan.md`

Source reviewed:

- `app/build.sh`
- `app/src/decoder.c`
- `app/src/m4b_decoder.c`
- `app/src/m4b_decoder.h`
- `app/src/player.c`
- `app/src/ui.c`
- `app/src/main.c`

Additional implementation files inspected because they affect architecture:

- `app/src/db.c`
- `app/src/scanner.c`
- `app/src/resume.c`
- `app/src/alsa.c`
- `app/src/config.c`
- `app/src/font.c`
- `app/src/cover.c`
- `tools/firmware_overlay.json`
- `tools/r1_audiobook_launch.sh`

Verification run:

- `sh app/build.sh` passes.
- `file build/r1_audiobook_app` reports a 32-bit little-endian MIPS statically linked ELF.
- Binary size is about 2.56 MB.
- `readelf -d build/r1_audiobook_app` reports no dynamic section.
- `python3 app/tests/test_app.py` passes, but it does not run all pytest tests.
- `pytest -q app/tests/test_app.py` fails one test: `test_resume_seek_helper` still calls `player_resume_seek_ms()` with a stale `track_list *` shape instead of the current `playback_queue *` API.

## Previous Decisions

| Previous decision | Verdict | Revision |
|---|---|---|
| Hybrid standalone app + stock `hiby_player` for music | Holds | Still the best product boundary. The direct app now has enough code to prove the approach is buildable. |
| No audiobook binary patching of `hiby_player` | Holds | Keep this as the final target. Do not let old native-hub/direct-open patch flags leak into the standalone app release. |
| Custom app-owned playback engine | Holds with caveats | Correct for avoiding stock-player fragility, but it gives up stock M4B/speed/Bluetooth behavior. The code must explicitly earn those back. |
| Dedicated SQLite audiobook database | Holds | Current implementation violates the incremental/persistent intent by deleting progress/bookmarks during scan. |
| Event-driven resume daemon owns persistence | Needs revision | Current app directly writes SQLite and JSON resume records. Either finish the daemon as the persistence owner or simplify the architecture so the app owns progress and the daemon is only a lifecycle/power-event bridge. Do not keep two writers. |
| `libmp4ff` + `libfaad2` decoder matrix | Needs revision | Current build uses FAAD2's `mp4read` frontend and NeAAC API, not `libmp4ff`. That can be acceptable short term, but the architecture doc should say so honestly. |
| FDK-AAC static preferred over firmware `libfdk-aac.so` | Partially holds | Do not depend on the firmware `.so`. But since static FAAD2 now builds, switching to FDK-AAC is not the next best move unless licensing, AAC profile support, or seek tests fail. |
| Phase 2 direct-open / auto-tap plans | Superseded | These docs are obsolete for the approved standalone app path. Keep them only as historical analysis. |
| UI polish after playback correctness | Still correct | HiBy assets are needed for launch polish, but UI work should not outrun persistence, M4B, and on-device audio proof. |

## 1. Is The Hybrid Approach Still Right?

Yes. With fresh eyes, I still recommend the standalone audiobook app.

The biggest argument against the hybrid path is that the R1 stock firmware already has valuable audiobook-adjacent behavior: M4B support, speed controls, sleep shutdown, Bluetooth routing, hardware controls, and a proven audio pipeline. The feature spec's original instinct to reuse the stock engine was reasonable.

But the cost of using that stock engine for audiobook semantics remains too high. The previous patch/touch/framebuffer route was fragile, update-sensitive, and hard to validate. A standalone app gives us deterministic book selection, private queues, direct resume, completion semantics, and a clean rollback boundary.

What I underweighted earlier:

- Replacing the playback engine means the app must re-prove sample-rate handling, Bluetooth output, underrun recovery, hardware buttons, sleep/power events, and M4B edge cases.
- The packaging path is now just as important as the app binary. A correct app that ships without fonts/theme assets or with old patch flags can still fail after flash.
- The resume daemon boundary is less settled than the architecture doc implies. Current code is app-local persistence with optional IPC, not daemon-owned persistence.

Despite those caveats, returning to `hiby_player` binary patching would be a worse long-term bet.

## 2. Is The Architecture Blueprint Still Accurate?

Conceptually, mostly yes. Operationally, it needs an update pass.

Still accurate:

- Separate app for audiobooks.
- Stock music player preserved.
- SQLite as audiobook source of truth.
- Book-bounded queue.
- Direct framebuffer UI.
- Direct touch input.
- Decoder abstraction.
- Explicit M4B/AAC backend.
- No framebuffer pixel detection or synthetic touch in the final app path.

Needs updating:

- The source tree in the blueprint is idealized. Actual code is currently flat under `app/src`, not split into `ui/`, `db/`, `player/`, `library/`, etc.
- The font stack changed from FreeType/Noto to `stb_truetype` + `msyh.ttf`.
- The M4B backend changed from `libmp4ff + libfaad2` to FAAD2 `mp4read` + NeAAC API.
- The daemon role is not implemented as specified. The app writes progress itself and does not wait for a daemon `RESUME_PLAN`.
- The build system is shell + `zig cc`, not `build.zig`.
- The architecture says incremental scan; current scanner is destructive.
- The architecture says package assets under `/usr/share/audiobooks`; the overlay currently installs the app binary but not the font/theme tree.
- The memory budget should be revised: `msyh.ttf` is about 2.8 MB and `font_open()` loads the whole font into RAM.
- Playback speed is not implemented. `player_set_speed()` changes the state variable only.
- Bluetooth routing is not implemented as the blueprint describes. Current code opens a kernel PCM device directly.

Wrong or incomplete decisions from earlier:

- I treated "custom ALSA" as if it could preserve stock routing with modest work. The current direct-ioctl implementation is lean, but it bypasses libasound plugins and therefore does not naturally give us BlueALSA routing.
- I did not make the dependency packaging requirements concrete enough. The app now depends on runtime assets, so the firmware overlay must install them.
- I did not call out the need for large-file-safe M4B parsing strongly enough. On a 32-bit MIPS target, audiobook M4Bs can exceed 2 GB.

## 3. Is FAAD2 The Right M4B Decoder?

Short answer: **FAAD2 is acceptable for the immediate MVP if its GPL/patent implications are acceptable and if M4B seek/open tests pass on real files. Do not switch to firmware `libfdk-aac.so`. Do not switch to static FDK-AAC right now unless FAAD2 fails correctness or licensing requirements.**

Current evidence:

- `app/build.sh` now downloads FAAD2, compiles all `libfaad` sources plus `frontend/mp4read.c`, and links them into the static MIPS binary.
- A clean local build succeeds.
- The binary is static and under the current size budget.

Concerns with the current FAAD2 path:

- The build downloads `master.zip` from GitHub, not a pinned release or commit. That is not reproducible.
- FAAD2 is GPL. Static linking means the app distribution must be compatible with GPL obligations. This may be fine for a personal/open mod, but it should be explicit.
- FAAD2's README also warns about AAC patent royalties. That is not unique to FAAD2, but it should be acknowledged.
- The current MP4 parser path uses FAAD2 frontend globals such as `mp4config`. That is not reentrant. A scanner refresh that opens M4B metadata while M4B playback is active can corrupt decoder state.
- Large-file safety is suspect. `m4b_decoder.c` uses `fseek()`, `ftell()`, casts offsets to `long`, stores ADTS offsets in `uint32_t`, and does not compile with `_FILE_OFFSET_BITS=64`.
- Seek correctness depends on `mp4read_seek()` and sample index mapping, not just the AAC decoder.
- The scanner stores embedded M4B chapters in `chapters`, and `db_query_chapters()` then returns those chapters as if they were playback tracks, with `path` set to chapter title text. That can make M4B-with-chapters playback fail.

Best path:

1. Keep static FAAD2 for the immediate run.
2. Pin FAAD2 to a known commit or vendor it under `third_party/` with license files.
3. Add `_FILE_OFFSET_BITS=64` and audit `mp4read`/chapter parser for files over 2 GB.
4. Add a non-stripped build artifact or map file so decoder symbols can be verified.
5. Serialize all M4B decoder use, or split scanner chapter extraction from playback so the FAAD frontend globals cannot race.
6. Fix the chapter-vs-track model before using embedded M4B chapters.
7. Test representative M4B files: small LC-AAC, HE-AAC/SBR, long file, chaptered file, no-chapter file, corrupt file, seek near beginning/middle/end.

Fallback if FAAD2 fails:

- Use a statically linked FDK-AAC source build plus a real 64-bit-safe MP4 parser.
- Keep the static app model.
- Use a stock-libc helper process only if static decoder integration cannot be made reliable.
- Do not dynamically load the firmware `libfdk-aac.so` from the musl static app.

## 4. Are Forge's Current Directions Correct?

Mostly yes, but Forge should tighten the order.

### Linking FAAD2

Correct direction for now. It now builds locally, so do not spend the next cycle replacing it with FDK-AAC unless real M4B files fail or GPL is unacceptable.

Required follow-up:

- Pin/vendor FAAD2.
- Add large-file flags and tests.
- Add real M4B open/seek/resume tests.
- Fix the M4B chapter queue bug before declaring M4B support complete.

### UI polish with HiBy assets

Correct, but this has become a packaging blocker, not just polish.

The app default font path is `/usr/share/audiobooks/fonts/msyh.ttf`, and UI code loads theme PNGs from `/usr/share/audiobooks/hiby-theme/...`. `tools/firmware_overlay.json` currently installs `usr/bin/r1_audiobook_app` and launcher scripts, but I found no install entries for `/usr/share/audiobooks/fonts/msyh.ttf` or `/usr/share/audiobooks/hiby-theme`.

If that remains true in Forge's branch, the flashed app will fail `ui_init()` when `font_open()` cannot open the font.

### Swipe-right and mini-player

Good as a usability improvement, but not a release blocker. It should not be allowed to mask playback, resume, or packaging failures.

### Firmware rebuild

Correct only after a preflight cleanup:

- Include font and theme assets in the overlay.
- Make the launcher script validate assets and fail clearly.
- Ensure old `hiby_player` audiobook route/direct-open/touch automation flags are disabled for this standalone-app firmware.
- Ensure old daemon/watch scripts do not fight the app for resume/library ownership.
- Push-run the app over ADB before flashing the full firmware.

## 5. Is The Test Plan Sufficient?

It is a strong start, but not sufficient yet.

Important gaps/additions:

- Replace "FDK-AAC compiled statically" with "AAC backend compiled statically" or make the expected backend configurable.
- `nm build/r1_audiobook_app` will not prove FAAD2 symbols because the binary is stripped with `-s`. Either produce an unstripped sidecar or change the evidence method.
- Add a test that a library refresh preserves progress, bookmarks, and stable book IDs.
- Add a test that startup scan does not delete `progress`.
- Add a test for a chaptered M4B proving playback still opens the real file, not chapter-title pseudo tracks.
- Add >2 GB M4B/container offset tests or at least static checks for `_FILE_OFFSET_BITS=64`, `fseeko`, `ftello`, and 64-bit offsets.
- Add an asset packaging test for `/usr/share/audiobooks/fonts/msyh.ttf` and `/usr/share/audiobooks/hiby-theme`.
- Add a launcher preflight test: missing font/theme should not cause five silent restart attempts.
- Add tests for direct files under `/Audiobooks` becoming separate books, not one giant root book.
- Add uppercase extension tests: `.MP3`, `.M4B`, `.FLAC`.
- Add scanner tests for multiple standalone `.m4b` files outside `/Audiobooks`.
- Add on-device internal DAC playback test for MP3, FLAC, WAV, M4B.
- Add on-device Bluetooth playback/routing test, or explicitly mark Bluetooth unsupported for this release.
- Add sample-rate tests: 22.05 kHz, 32 kHz, 44.1 kHz, 48 kHz.
- Add shutdown/power-loss tests that verify progress survives after the app receives SIGTERM and after abrupt kill.
- Add "old runtime conflict" tests: no legacy touch automation paths active, no old daemon overwrites app progress.
- Add memory/CPU checks during scan, cover rendering, and M4B playback.
- Add UI screenshot/fb checks that text fits and the app is not blank.

## 6. Biggest Risk Right Now

The biggest architecture risk is **false readiness**: the binary builds and the UI is improving, but core audiobook invariants can still fail after flash.

The most flash-blocking immediate risk is missing runtime assets. If the font/theme assets are not packaged, the app can fail to start on device.

The most serious product risk is persistence correctness. `main.c` calls `library_refresh()` on startup, `library_refresh()` calls `db_clear_library()`, and `db_clear_library()` deletes `progress` and `bookmarks`. That directly violates the feature's central promise: a book must resume after reboot and return visits.

The highest M4B-specific risk is chapter handling. As written, embedded chapter rows can replace playable track rows and break playback for exactly the kind of M4B files Eric cares about.

## 7. Architectural Concerns In The Current Code

### Critical

1. **Startup scan deletes progress and bookmarks.**
   - `app/src/main.c:148` calls `library_refresh()` every app start.
   - `app/src/scanner.c:305-309` calls `db_clear_library()`.
   - `app/src/db.c:63-65` deletes `chapters`, `tracks`, `progress`, `bookmarks`, `book_search`, and `books`.
   - This must be fixed before any user-facing flash.

2. **Embedded M4B chapters are conflated with playback tracks.**
   - `app/src/scanner.c:221-229` stores M4B chapters in the `chapters` table.
   - `app/src/db.c:366-404` returns `chapters` as a `track_list`, with `path` populated from chapter title.
   - `app/src/ui.c:509` passes that list to `player_open_book()`.
   - `app/src/player.c:70` opens `track->path`. For a chaptered M4B, that can be a title string, not an audio path.

3. **M4B parser is not clearly large-file-safe.**
   - `app/src/m4b_decoder.c:49`, `194`, `203-207`, and `363` use `fseek()`/`ftell()` and `long` casts.
   - `app/src/m4b_decoder.c:64` stores ADTS offsets as `uint32_t`.
   - `app/build.sh` does not define `_FILE_OFFSET_BITS=64`.

4. **M4B decode path is not reentrant.**
   - `app/src/m4b_decoder.c:243-249`, `338`, and `351-353` use global `mp4config`/`mp4read` state.
   - The scanner and player must not open M4B files concurrently in the same process.

5. **Runtime assets are not packaged.**
   - `app/src/config.c:17` requires `/usr/share/audiobooks/fonts/msyh.ttf`.
   - `app/src/ui.c` uses `/usr/share/audiobooks/hiby-theme/...`.
   - `tools/firmware_overlay.json:32-40` installs the app and launcher, but I found no overlay entries for the font/theme directories.

### High

6. **Direct ALSA ioctl path may not satisfy the R1's actual routing needs.**
   - `app/src/config.c:16` defaults to `/dev/snd/pcmC0D0p`.
   - `app/src/alsa.c:37-54` requests exact kernel HW params with `SNDRV_PCM_HW_PARAMS_NORESAMPLE`.
   - This is lean, but it bypasses libasound plugins and does not prove BlueALSA behavior.

7. **Playback speed is a state variable only.**
   - `app/src/player.c:330-335` sets `player->speed`, but the PCM stream is not time-stretched.

8. **Seek path is wrong for general book-position seeks.**
   - `app/src/player.c:319-327` stores `pending_seek_ms`.
   - `app/src/player.c:165-168` derives local seek from current `player->position_ms`, not the requested `pending_seek_ms`.
   - Resume open currently avoids most of this because it seeks during `open_transport_locked()`, but real skip/seek controls will need this fixed.

9. **Smart rewind tiers do not match the spec.**
   - `app/src/resume.c:57-63` rewinds 10 seconds after 5 minutes and 20 seconds after 15 minutes.
   - The spec says 0 seconds under 5 minutes, 5 seconds from 5-60 minutes, 10 seconds from 1-24 hours, and 15-20 seconds after 24 hours/reboot.

10. **Timestamps mix monotonic and wall-clock meanings.**
    - `ab_now_ms()` is monotonic and used for `last_played_at`.
    - `completed_at` uses `time(NULL)`.
    - Continue Listening display and multi-day smart rewind need wall-clock time; short protection windows need monotonic time. These should be separate fields or clearly separate semantics.

11. **The scanner is still a skeleton.**
    - It does not parse `book.json`.
    - It does not parse normal embedded metadata.
    - It estimates duration from file size.
    - It does not populate authors/series.
    - It does not handle direct files under `/Audiobooks` as separate books.
    - It does not scan standalone `.m4b` outside `/Audiobooks`.
    - It is case-sensitive for extensions.

12. **Cover loading is not bounded.**
    - `app/src/cover.c:37-50` decodes full images into RGB565 memory.
    - Large cover art can spike memory. Decode-to-bounded-size or reject oversized source dimensions.

13. **Font memory budget changed.**
    - `app/src/font.c:33-54` loads the whole TTF into RAM.
    - `msyh.ttf` is about 2.8 MB, which exceeds the earlier font budget and should be included in memory testing.

14. **Old runtime artifacts remain in packaging.**
    - `tools/firmware_overlay.json:403-430` still lists old direct-open/touch helper assets in mode overrides.
    - These may be harmless if not installed/enabled, but the final standalone package should be cleaned so verification is not ambiguous.

## 8. Recommended Priority After Forge's Current Run

Do not make the next milestone "more UI". Make it "flash-safe MVP".

Priority order:

1. **Package preflight**
   - Install `/usr/share/audiobooks/fonts/msyh.ttf`.
   - Install `/usr/share/audiobooks/hiby-theme`.
   - Add launcher checks for app, font, theme directory, writable app root, readable framebuffer, readable touch input, and writable DB path.

2. **Stop destructive scanning**
   - Remove `db_clear_library()` from normal startup.
   - Make refresh transactional and preserve `progress`/`bookmarks`.
   - Keep stable `book_key` and remap changed track IDs.

3. **Fix track vs chapter modeling**
   - `db_query_tracks_for_playback()` must always return playable file tracks.
   - `db_query_chapters_for_display()` should return display chapters.
   - For M4B chapters, a chapter row should reference the parent track and a start/end offset, not become a fake track path.

4. **Harden M4B**
   - Pin/vendor FAAD2.
   - Add `_FILE_OFFSET_BITS=64`.
   - Audit/replace `fseek`/`ftell` with large-file-safe APIs.
   - Add real M4B fixture tests.
   - Serialize M4B metadata extraction and playback, or move scanning to a separate helper process.

5. **On-device playback proof**
   - Push app and assets over ADB.
   - Run `--scan-only`.
   - Start app foreground.
   - Play MP3/WAV/FLAC/M4B through internal output.
   - Verify no `hiby_player` crash.
   - Then test Bluetooth or explicitly defer it.

6. **Resolve daemon ownership**
   - Either finish daemon request/response for `RESUME_PLAN`, or make the app the explicit progress owner and disable old resume-daemon behavior for standalone app playback.
   - Avoid two independent writers for the same resume records.

7. **Fix tests**
   - Make pytest pass.
   - Update the formal test plan's AAC/backend assumptions.
   - Add tests listed above before any "ready to flash" sign-off.

8. **Then continue UI polish**
   - Mini-player and swipe-right are good.
   - Next useful UI work is Now Playing accuracy: active chapter title, current/remaining time, track/chapter count, and visible errors.

## Final Recommendation To Forge

Forge's broad direction is correct. The standalone app is the right path, and static FAAD2 is acceptable as the immediate AAC/M4B backend if licensing is acceptable.

But Forge should not proceed from "builds and has themed UI" directly to "flash it" without fixing the persistence and M4B chapter model. Those are architectural correctness failures, not cosmetic bugs.

Minimum "ready for risky device smoke" bar:

- App binary builds static.
- Assets are packaged.
- App starts on device.
- Library scan does not erase progress.
- One MP3 multi-file book resumes correctly.
- One normal M4B opens and seeks.
- One chaptered M4B still opens the actual media file.
- Old `hiby_player` patches are disabled except any consciously retained scanner/music-exclusion patch.
- Music playback still works.

After that, confidence rises from low/medium to medium/high.

