# Hybrid Architecture Evaluation: Separate Audiobook App + Stock `hiby_player`

## Executive Recommendation

**Recommend Approach B: keep stock `hiby_player` for music, and ship audiobooks as a separate standalone app.**

That is the best fit for the spec and the hardware because it:

- Removes audiobook behavior from the fragile stock player path.
- Lets audiobooks own their own UI, queue, and resume model.
- Preserves the stock music experience without binary patching it.
- Avoids framebuffer detection, touch injection, and timing-based UI guessing.
- Keeps the change surface small enough to maintain across stock firmware drift.

Approach C, a full custom rootfs, is architecturally cleaner than the current patch stack but is too large a rewrite for the stated goal of delivering the audiobook spec while preserving stock music behavior. Approach A can work in isolated cases, but it keeps the project coupled to HiBy binary internals and long-term patch fragility.

The hybrid app should use a **dedicated audiobook database** and a **minimal custom playback engine** rather than mpd or deadbeef for the production path. mpd is acceptable as a prototype backend, but it is a poor long-term fit for playback speed, chapter semantics, and strict book-bounded queue behavior. deadbeef is heavier than the problem needs. A small custom ALSA-based player is the best technical fit for the device and the spec.

## Comparison Matrix

Scoring scale:

- `5` = cleanly satisfies the requirement
- `4` = satisfies with minor caveats
- `3` = satisfies, but with noticeable compromise
- `2` = partial / awkward fit
- `1` = poor fit
- `0` = not viable

| Spec requirement | Approach A: binary patch `hiby_player` | Approach B: separate audiobook app + stock music | Approach C: full custom rootfs |
|---|---|---|---|
| `§3.3` Direct title tap, no intermediate screen | `2/5` - possible only through fragile player hooks and restore choreography | `5/5` - app can jump straight from title row to Now Playing | `5/5` - full control over launch path |
| `§9` Book-bounded private queue | `3/5` - achievable, but only by leaning on stock playlist behavior | `5/5` - app owns the queue and can keep it private | `5/5` - app owns the whole player stack |
| `§18` No framebuffer, no touch injection, no timing delays | `1/5` - the patch path was built around stock UI assumptions and remains coupled to them | `5/5` - no need for UI automation at all | `5/5` - no need for stock UI automation |
| `§17` Separate audiobook database | `3/5` - separate catalogs/resume exist, but stock DB coupling remains | `5/5` - natural fit for a dedicated SQLite library | `5/5` - full control over storage model |
| `§7` Reliable resume, smart rewind, accidental-start protection | `3/5` - the current code implements much of this, but it is still tied to the stock-player path | `5/5` - event-driven resume is straightforward when the app owns playback | `5/5` - also straightforward, but at much higher scope |
| `§8` Completion detection on natural EOF | `4/5` - current daemon logic can detect EOF, but depends on stock-player timing | `5/5` - backend EOF event is direct and reliable | `5/5` - same advantage as B |
| `§19` Daemon stays asleep when idle, no continuous polling | `2/5` - polling still exists and is coupled to stock player state | `5/5` - daemon can be event-driven and wake only for playback events | `4/5` - possible, but only after rebuilding music mode too |
| `§21` Acceptance criteria | `2/5` - fragile and update-sensitive | `5/5` - best overall fit to the spec | `4/5` - feature-complete but misaligned with preserving stock music behavior |
| `§23` Definition of done | `2/5` - high maintenance and repeated patch work | `5/5` - clear finish line with isolated app and small system integration | `3/5` - finish line is blurred by the need to rebuild music functionality |

### Overall assessment

| Approach | Functional fit | Delivery risk | Maintenance burden | Verdict |
|---|---:|---:|---:|---|
| A | Medium | High | High | Not recommended |
| B | High | Medium | Low to medium | **Recommended** |
| C | High for audiobooks, low for product scope | High | Very high | Not recommended for this project |

### Effort and timeline

| Approach | MVP effort | Spec-complete effort | Notes |
|---|---:|---:|---|
| A | `1-3 weeks` | `4-8+ weeks` and recurring patch work | Fastest to tweak, but every stock update can reopen the problem |
| B | `2-4 weeks` | `6-10 weeks` | Best balance of reuse, isolation, and testability |
| C | `2-4 months` | `4-6+ months` | Highest scope because music parity becomes part of the project |

## Why Approach B Wins

Approach B best matches the actual product boundary:

- The user wants an audiobook experience that feels distinct from music.
- The device already has a working music player that should be preserved.
- The fragile part of the current system is not cataloging; it is trying to make the stock player behave like an audiobook app.
- A separate audiobook app removes that mismatch.

The current codebase already contains reusable building blocks for B:

- `src/catalog.c` already maps book roots, track order, and book keys.
- `src/resume.c` already implements atomic resume persistence, completion state, and smart rewind rules.
- `src/state.c` already models book lifecycle and track transitions.

Those pieces can be moved behind an app/daemon boundary instead of being forced to cooperate with stock UI behavior.

This also matches the design guidance in `§18.2` and `§18.3`:

- The generated M3U playlist model is a migration artifact, not the long-term runtime model.
- The preferred integration point is the book-selection boundary in the audiobook app itself, not the stock track-list screen or playlist open path.

## Recommended Hybrid Architecture

### 1. High-level system shape

The hybrid system should have three clear surfaces:

1. **Stock `hiby_player` for music**
   - Left untouched for playback behavior.
   - No audiobook-specific binary patching in the player core.

2. **Standalone audiobook app**
   - Full-screen framebuffer UI at `480x800`.
   - Owns library browsing, book open flow, chapter list, bookmarks, Now Playing UI, and playback control.
   - Exits cleanly back to the stock launcher when the user leaves audiobooks.

3. **Event-driven audiobook persistence service**
   - Reuses the current C daemon’s resume/catalog/completion logic.
   - Stops doing screen detection and polling.
   - Receives playback events from the app and persists resume state.

### 2. App structure

Recommended app modules:

- `ui/` - book grid/list views, Now Playing screen, chapter list, finished/continue views, settings
- `library/` - scans `/Audiobooks`, resolves grouping rules, builds the audiobook database
- `db/` - SQLite schema, migrations, indexes, FTS search
- `player/` - custom ALSA playback engine, track queue, gapless transitions, seek, speed
- `resume/` - book open, progress save/restore, smart rewind, accidental-start protection
- `ipc/` - event channel to the daemon and any launcher handoff messages

The app should expose a single user-facing model:

- **Book** as the primary item
- **Track** as the internal chapter/file boundary
- **Chapter list** as an intentional secondary view, not the first screen

The UI must not expose M3U files as the user-visible abstraction. They are implementation details only.

### 3. Playback backend choice

#### Recommended production backend: custom ALSA-based player

Use a lightweight custom playback engine that:

- Decodes audiobook formats directly.
- Streams to the existing `bluealsa` ALSA output path.
- Maintains a private in-memory queue for the current book only.
- Emits deterministic events for:
  - track loaded
  - track change
  - pause/resume
  - seek complete
  - end of file
  - app exit

Why this is the best fit:

- It can implement exact book-bounded queue semantics.
- It can support playback speed in a controlled way.
- It avoids a second generic music daemon.
- It keeps the runtime smaller than deadbeef and more spec-aligned than mpd.

#### Why not mpd as the production backend

mpd is useful if you want a quick headless queue engine, but it is a weak fit for this spec because:

- Playback speed is not a first-class audiobook feature in mpd.
- Chapter metadata and book-local queue semantics become awkward.
- You still need a separate control layer for resume and bookmarks.
- It adds another daemon and protocol surface without solving the audiobook-specific UX.

#### Why not deadbeef headless as the production backend

deadbeef is a reasonable general music player, but it is the wrong shape for this device and task:

- It is heavier than a dedicated audiobook player needs to be.
- Headless operation is an adaptation, not its native design center.
- The app would still need a custom book model, resume layer, and UI.

### 4. Launcher redirect

The launcher redirect should be **data-driven**, not a `hiby_player` code patch.

Recommended shape:

- Keep the existing resource-text patch path for labels such as `Audiobooks`, `Titles`, `Authors`, `Series`, and `Folders`.
- Add a launcher resource/config entry that resolves the Audiobooks tile to `/usr/bin/r1_audiobook_app`.
- Keep the redirect in the stock launcher/resource layer, not in the player binary.

If the launcher only accepts route IDs and not executable paths, the redirect should still be implemented as a small resource overlay or launcher config entry, not a stock-player binary patch.

The important boundary is this:

- **Allowed:** data-only launcher config changes, labels, icons, and app target routing
- **Not needed:** any `hiby_player` code cave work for audiobook launch

### 5. How the C daemon fits

Reuse the current daemon as an event-driven service, not a polling robot.

Responsibilities it should keep:

- Atomic resume record writes
- Smart rewind policy
- Accidental-start protection
- Completion detection
- Book/track lookup against the audiobook database
- Restore target calculation

Responsibilities it should drop:

- `/dev/fb0` reads
- Touch injection
- Screen classification
- UI timing windows
- Track-list guessing
- Stock-player memory scraping

Suggested IPC contract between app and daemon:

- `book_opened(book_id, track_id, position_ms, action)`
- `playback_started(track_id)`
- `playback_position(track_id, position_ms)`
- `track_changed(track_id)`
- `paused(position_ms)`
- `seek_requested(position_ms)`
- `eof_reached(track_id)`
- `app_exiting()`

The daemon can then:

- write the resume record,
- decide whether to apply smart rewind,
- protect old progress from accidental starts,
- mark completion only on natural EOF,
- and stay asleep when nothing is happening.

### 6. Mode switching

The two modes should be mutually exclusive:

- **Audiobook mode**
  - App is foreground and owns audio output.
  - Stock `hiby_player` is not the active playback surface.
  - App handles all audiobook-specific playback, persistence, and UI.

- **Music mode**
  - User exits the app and returns to the launcher.
  - Stock `hiby_player` is then used exactly as shipped for music.

Operationally:

- The audiobook app should release ALSA/bluealsa cleanly on exit.
- The launcher should provide a simple return path to the stock home screen.
- There should be no background “both players active” state.

### 7. Separate audiobook database

Use a dedicated SQLite database, not a repurposed music database.

Suggested runtime files:

- `/usr/data/audiobooks/library.db`
- `/usr/data/audiobooks/resume.d/` for compatibility or backup exports
- `/usr/data/audiobooks/cache/` for covers, precomputed sort keys, and search indexes

Suggested tables:

- `books`
- `tracks`
- `progress`
- `bookmarks`
- `series`
- `authors`
- `settings`
- `queue_state`

Suggested indexes:

- title
- author
- series
- root path
- fingerprint
- completed flag
- last played time

This directly satisfies the separate-database requirement and gives the app a clean runtime source of truth.

## Memory Budget Analysis

The current measured state leaves about `17.6 MB` available with `hiby_player` running.

In hybrid mode, the stock player is not the audiobook runtime, so its RSS can be replaced by a smaller custom app.

### Current measured baseline

| Component | RSS |
|---|---:|
| `hiby_player` | `16 MB` |
| `bluealsa` | `1.3 MB` |
| `adbd` | `1.9 MB` |
| `dbus/system` | `~2 MB` |
| `buff/cache` | `16 MB` |
| Available | `17.6 MB` |

### Expected hybrid audiobook mode

| Component | Estimated RSS |
|---|---:|
| `r1_audiobook_app` UI + player | `4-8 MB` |
| `r1_audiobook_resume_daemon` | `0.5-1.0 MB` |
| `bluealsa` | `1.3 MB` |
| `adbd` | `1.9 MB` |
| `dbus/system` | `~2 MB` |
| SQLite page cache / library state | `1-3 MB` |

Expected practical outcome:

- Audiobook mode should fit comfortably within the current memory envelope.
- The hybrid design should use less RAM than the stock `hiby_player` path if the app is kept lean.
- A custom ALSA player is safer on memory than mpd or deadbeef.

## Build and Packaging Plan

### Packaging shape

Use the existing rootfs overlay and UPT packaging flow.

Add:

- `r1_audiobook_app` binary
- app assets: icons, fonts, cover placeholders, config
- SQLite schema bootstrap or migration files
- optional compatibility export for `resume.d`

Keep:

- `bluealsa`
- `dbus/system`
- stock `hiby_player`
- the current firmware update format

### Rootfs layout

Suggested install locations:

- `/usr/bin/r1_audiobook_app`
- `/usr/data/audiobooks/`
- `/usr/data/audiobooks/library.db`
- `/usr/data/audiobooks/logs/`
- `/usr/data/audiobooks/cache/`

### Build flow

1. Build the audiobook app and playback engine for MIPS.
2. Generate or migrate the audiobook SQLite database schema.
3. Install the app and assets into the overlay.
4. Apply resource-text patches for labels and launcher text.
5. Apply the launcher redirect data entry.
6. Build the UPT package with the existing packaging pipeline.
7. Validate on device with smoke tests before release.

### Compatibility handling

The current `catalog.tsv` / `resume.d` model can be retained during transition as:

- a migration source,
- a debugging export,
- or a compatibility cache for current tooling.

But the app runtime should use the SQLite library as source of truth.

## Development Phases

### Phase 1: App shell and launcher handoff

- Build the standalone app frame and navigation shell.
- Add the launcher redirect to the app.
- Show Titles and Continue Listening from the new database.
- Keep playback simple and local.

### Phase 2: Production playback engine

- Implement the custom ALSA playback core.
- Add private per-book queue semantics.
- Add chapter transition handling and natural EOF detection.
- Support seek and basic skip controls.

### Phase 3: Resume and correctness

- Port the resume logic into the event-driven model.
- Add smart rewind.
- Add accidental-start protection.
- Add completion and restart/unfinished behavior.

### Phase 4: Audiobook-specific UX

- Chapter list
- Bookmarks
- Sleep timer
- Playback speed controls
- Finished / Continue Listening views

### Phase 5: Hardening and packaging

- Memory and performance tuning
- On-device smoke tests
- Crash recovery
- Database migration validation
- Release packaging

## Risk Assessment

### Main risks

1. **Playback engine correctness**
   - The hardest part is not UI, it is getting decode, queueing, gapless transitions, speed, and seek behavior right.

2. **Codec support**
   - The app must support the formats the target users actually have.
   - This is manageable, but it must be scoped deliberately.

3. **Launcher redirect discovery**
   - The exact stock launcher data path may take a little reverse engineering.
   - This is still far safer than patching `hiby_player`.

4. **Mode handoff**
   - The app must exit cleanly and release audio resources.
   - This is a normal application problem, not a binary patch problem.

5. **Speed control implementation**
   - If speed is a hard requirement, the backend must support it natively.
   - This is the strongest reason to prefer a custom ALSA engine over mpd.

### Mitigations

- Keep stock music playback untouched.
- Ship the audiobook app in stages, with clear device smoke tests after each milestone.
- Use an explicit database schema and migration path.
- Prefer deterministic event-driven playback state over polling.
- Keep the launcher redirect data-only.

## Why Not Approach A

Approach A keeps the system coupled to the stock player and its binary structure.

That creates long-term problems:

- patch fragility across firmware revisions,
- difficult debugging when launcher assumptions change,
- ongoing risk of regressions in stock music behavior,
- and a maintenance burden that grows with every HiBy update.

Even though the current codebase has already removed the worst framebuffer/touch automation, A still inherits the wrong basic boundary: it asks `hiby_player` to become an audiobook app.

That is the wrong place to build the feature.

## Why Not Approach C

Approach C is the cleanest technical abstraction, but it is the wrong product tradeoff for this project.

It asks the team to replace the whole player stack, not just deliver audiobooks:

- music playback parity becomes a second large project,
- stock HiBy behavior is lost,
- test scope expands dramatically,
- and the user no longer gets the benefit of the existing, validated music surface.

If the goal were “replace the entire device firmware experience,” C would be the right direction.

For this task, it is too much.

## Final Decision

**Choose Approach B.**

It is the only option that:

- cleanly meets the audiobook spec,
- preserves the stock music player,
- avoids fragile binary patch coupling,
- keeps memory usage within budget,
- and gives the maintainable long-term architecture.
