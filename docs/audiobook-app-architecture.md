# HiBy R1 Audiobook App Architecture

Status: implementation blueprint for Forge
Source of truth:
- `docs/audiobook-feature-spec.md`
- `docs/hybrid-architecture-evaluation.md`
- current runtime code in `src/`

## Purpose

This document turns the approved hybrid decision into a buildable architecture:

- Keep stock `hiby_player` for music.
- Add a standalone audiobook app for the Audiobooks tile.
- Reuse the existing C daemon as an event-driven persistence service.
- Keep the existing catalog/resume metadata model as the migration source.
- Replace UI automation and polling with app-owned playback and explicit IPC.

The central rule remains the spec rule:

> The user selects and listens to a book, not a collection of files.

---

## 1. System Overview

### 1.1 Component model

The final system has three runtime components:

1. `hiby_player`
   - Stock music player.
   - Left unchanged for music playback.
   - No audiobook binary patching in the player core.

2. `r1_audiobook_app`
   - New full-screen audiobook application.
   - Owns browsing, book selection, now playing UI, queueing, and audio playback for audiobooks.
   - Exits back to the launcher when the user leaves audiobooks.

3. `r1_audiobook_resume_daemon`
   - Existing C daemon, refactored into an event-driven service.
   - Owns resume persistence, smart rewind, accidental-start protection, and completion logic.
   - No framebuffer reads, no touch injection, no player-memory scraping in the final audiobook path.

### 1.2 Mode switching

The device operates in mutually exclusive playback modes:

#### Music mode

- Active:
  - stock launcher
  - stock `hiby_player`
  - `bluealsa`
  - any stock media services already present
- Inactive:
  - `r1_audiobook_app`
  - `r1_audiobook_resume_daemon` event loop sleeps unless a refresh or playback event occurs

#### Audiobook mode

- Active:
  - stock launcher in foreground only while starting or exiting
  - `r1_audiobook_app`
  - `r1_audiobook_resume_daemon`
  - `bluealsa`
  - `dbus/system` if already required for ALSA Bluetooth routing
- Inactive:
  - `hiby_player` is not the audiobook runtime

#### Launcher flow

```text
boot
  -> stock launcher
     -> Music tile
        -> stock hiby_player
     -> Audiobooks tile
        -> /usr/bin/r1_audiobook_launch.sh
           -> /usr/bin/r1_audiobook_app
              -> now playing / library / settings
              -> exit
        -> stock launcher
```

### 1.3 Process lifecycle

#### Boot

1. System boots into HiByOS.
2. Init scripts start `bluealsa`, `dbus/system`, and the audiobook resume daemon if configured.
3. The stock launcher appears.

#### Launcher to app

1. User taps the Audiobooks tile.
2. The launcher resolves the tile to `/usr/bin/r1_audiobook_launch.sh`.
3. The wrapper verifies the app binary, environment, and optional database readiness.
4. The wrapper `exec`s `/usr/bin/r1_audiobook_app`.
5. The app connects to the daemon and opens the Home screen.

#### App exit

1. User presses Back from Home or chooses Exit.
2. The app flushes pending progress events.
3. The app closes ALSA, database handles, socket handles, and framebuffer state.
4. The process exits.
5. Control returns to the launcher.

#### Crash / fallback

- If the app cannot start, the wrapper falls back to stock behavior instead of wedging the shell.
- If the app crashes after launch, the launcher remains available and the next tap still enters the wrapper.
- The stock `hiby_player` binary is never modified for this behavior.

### 1.4 What runs when

| Mode | Foreground UI | Audio engine | Resume service | Scanner |
|---|---|---|---|---|
| Boot / launcher | stock launcher | none | daemon idle | none |
| Music | stock launcher or `hiby_player` | stock engine | daemon idle | none |
| Audiobook browsing | `r1_audiobook_app` | app ALSA player | daemon active | none |
| Audiobook playback | `r1_audiobook_app` | app ALSA player | daemon active | none |
| Library refresh | `r1_audiobook_app` + helper | none or paused playback | daemon active | on-demand helper |
| Exit to launcher | stock launcher | none | daemon idle or draining | none |

### 1.5 Architectural decision

**Decision:** Separate audiobook playback from the stock music player.

**Why:** It removes the fragile coupling to stock player internals, preserves music behavior, and allows a deterministic book-level queue.

**Alternatives considered:** Binary patching `hiby_player`, or replacing the whole firmware player stack.

**Why not the alternatives:** Both increase maintenance burden and make the audiobook feature harder to keep stable across firmware changes.

---

## 2. Audiobook App Architecture

### 2.1 Source tree

Use a dedicated app tree instead of mixing the new app into the daemon source:

```text
app/
  build.zig
  include/
    audiobook_app.h
    audiobook_config.h
    audiobook_ipc.h
    audiobook_db.h
    audiobook_player.h
    audiobook_resume.h
    audiobook_scanner.h
    audiobook_ui.h
  src/
    main.c
    app.c
    app_state.c
    ipc/
      server.c
      client.c
      protocol.c
    db/
      db.c
      migrations.c
      queries.c
    library/
      library.c
      scanner.c
      grouping.c
      metadata.c
      covers.c
    player/
      player.c
      decoder.c
      queue.c
      stretch.c
      alsa.c
    resume/
      resume_client.c
      resume_models.c
    ui/
      fb.c
      input.c
      font.c
      theme.c
      screens/
        home.c
        titles.c
        now_playing.c
        chapters.c
        settings.c
        authors.c
        series.c
        folders.c
        finished.c
    util/
      file.c
      string.c
      time.c
      lru.c
  assets/
    fonts/
    icons/
    app.manifest.json
  tests/
```

### 2.2 Module breakdown

#### `ui/`

Responsibilities:

- Draw every screen on the 480x800 framebuffer.
- Handle touch gestures and physical Back behavior.
- Maintain the navigation stack.
- Render cover art, list rows, progress bars, and control buttons.

Inputs:

- `book_row`, `track_row`, `progress_row`
- touch events from `/dev/input/event1`
- playback state from `player/`
- scanner/library results from `db/`

Outputs:

- screen transitions
- user actions as high-level commands
- paint requests to `/dev/fb0`

Suggested interfaces:

```c
typedef enum {
    UI_SCREEN_HOME,
    UI_SCREEN_TITLES,
    UI_SCREEN_NOW_PLAYING,
    UI_SCREEN_CHAPTERS,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_AUTHORS,
    UI_SCREEN_SERIES,
    UI_SCREEN_FOLDERS,
    UI_SCREEN_FINISHED,
} ui_screen_id;

int ui_init(struct ui_context *ui, const struct ui_config *cfg);
int ui_push_screen(struct ui_context *ui, ui_screen_id screen, const void *payload);
int ui_pop_screen(struct ui_context *ui);
int ui_render(struct ui_context *ui);
int ui_handle_touch(struct ui_context *ui, const struct input_event *ev);
int ui_handle_key(struct ui_context *ui, int keycode, int pressed);
```

#### `db/`

Responsibilities:

- Own the SQLite schema.
- Migrate schema versions.
- Expose query APIs for titles, authors, series, folders, progress, bookmarks, and search.
- Import legacy `catalog.tsv` and `resume.d` records.

Inputs:

- `library.db`
- migration files
- legacy `catalog.tsv`, `catalog-books.tsv`, `resume.d/*.json`

Outputs:

- query results for UI and playback
- persisted schema changes

Suggested interfaces:

```c
int db_open(struct audiobook_db *db, const char *path);
int db_migrate(struct audiobook_db *db, uint32_t target_version);
int db_import_legacy_catalog(struct audiobook_db *db, const char *catalog_tsv);
int db_import_resume_records(struct audiobook_db *db, const char *resume_dir);
int db_query_home(struct audiobook_db *db, struct book_list *out);
int db_query_titles(struct audiobook_db *db, struct book_list *out);
int db_query_authors(struct audiobook_db *db, struct author_list *out);
int db_query_series(struct audiobook_db *db, struct series_list *out);
int db_query_chapters(struct audiobook_db *db, int book_id, struct chapter_list *out);
int db_search(struct audiobook_db *db, const char *needle, struct search_results *out);
```

#### `library/`

Responsibilities:

- Coordinate filesystem scanning.
- Group files into logical books.
- Extract metadata and cover art.
- Hand the results to `db/`.

Inputs:

- configured audiobook roots
- filesystem metadata
- `book.json`
- embedded tags

Outputs:

- normalized `book`, `track`, `chapter`, `cover` records

Suggested interfaces:

```c
int library_refresh(struct audiobook_db *db,
                    const struct library_roots *roots,
                    bool incremental,
                    struct library_refresh_report *report);
int library_refresh_root(struct audiobook_db *db,
                         const char *root_path,
                         struct library_refresh_report *report);
```

#### `player/`

Responsibilities:

- Open one book queue at a time.
- Decode audio frames.
- Feed PCM to ALSA.
- Handle gapless transitions, seek, speed, and EOF.
- Emit state events for the daemon.

Inputs:

- ordered track list
- resume target
- playback speed
- output backend selection

Outputs:

- PCM to ALSA
- `audiobook_ipc` events to the daemon
- playback state for UI

Suggested interfaces:

```c
int player_open_book(struct audiobook_player *p,
                     const struct book_row *book,
                     const struct track_list *tracks,
                     const struct progress_row *resume);
int player_play(struct audiobook_player *p);
int player_pause(struct audiobook_player *p);
int player_stop(struct audiobook_player *p);
int player_seek_ms(struct audiobook_player *p, uint64_t position_ms);
int player_set_speed(struct audiobook_player *p, float speed);
int player_next_track(struct audiobook_player *p);
int player_previous_track(struct audiobook_player *p);
int player_poll(struct audiobook_player *p, struct player_snapshot *out);
```

#### `resume/`

Responsibilities:

- Track the current book-open lifecycle.
- Decide when to save.
- Apply smart rewind rules.
- Protect meaningful progress from accidental starts.
- Send or receive IPC resume instructions.

Inputs:

- playback events
- elapsed time since last save
- saved progress

Outputs:

- save decisions
- resume targets

Suggested interfaces:

```c
int resume_bind_book(struct resume_state *r,
                     const struct book_row *book,
                     const struct progress_row *progress);
int resume_on_event(struct resume_state *r,
                    const struct audiobook_event *ev,
                    struct resume_action *out);
int resume_build_save(struct resume_state *r,
                      struct progress_row *out);
uint32_t resume_smart_rewind_ms(uint64_t paused_seconds,
                                bool rebooted,
                                uint32_t saved_position_ms);
```

#### `ipc/`

Responsibilities:

- Define the local protocol between app and daemon.
- Send event packets over a Unix domain socket.
- Optionally support a simple request-response handshake.

Inputs:

- app lifecycle events
- playback updates

Outputs:

- daemon commands
- ACKs and resume plans

Suggested interfaces:

```c
int ipc_client_connect(const char *socket_path);
int ipc_server_listen(const char *socket_path);
int ipc_send(int fd, const struct audiobook_ipc_frame *frame);
int ipc_recv(int fd, struct audiobook_ipc_frame *frame, int timeout_ms);
```

### 2.3 Memory budget

The app must fit comfortably inside the available ~17 MB with the stock system services still present.

Recommended steady-state budget:

| Module | Steady-state RSS budget |
|---|---:|
| framebuffer UI + input | 1.2 MB |
| font atlas + layout | 0.6 MB |
| SQLite db layer | 0.8 MB |
| player core | 2.0 MB |
| decoder buffers | 1.5 MB |
| cover art cache in RAM | 0.8 MB |
| IPC + resume state | 0.2 MB |
| app total | 7.1 MB |

Transient budgets:

- Scanner / import helper: up to 2-3 MB transiently during refresh.
- Cover decode spikes: up to one full cover image plus destination buffer.

### 2.4 Build system

Primary cross-compile path:

- `zig cc -target mipsel-linux-musleabi`
- output: static or mostly static MIPS ELF binaries

Recommended compile flags:

```text
-O2 or -Oz
-static where practical
-fno-stack-protector
-ffunction-sections -fdata-sections
-Wl,--gc-sections
```

Recommended build entrypoints:

- `tools/build_audiobook_app.py`
- `tools/build_resume_daemon.py`
- `tools/build_firmware.py`

The build should prefer Zig because the repo already uses it successfully for the daemon and helpers. GCC is an acceptable fallback only if the MIPS Zig toolchain is unavailable.

### 2.5 Runtime dependencies

Keep runtime dependencies small:

- `libasound` for ALSA output
- `libsqlite3` for the library database
- `pthread` and `m` as needed by the player and UI
- `dbus` only if `bluealsa` requires it on the target rootfs

Prefer vendored or in-tree code for:

- framebuffer drawing
- touch handling
- image scaling
- font rasterization
- time-stretching

### 2.6 Architectural decision

**Decision:** Use a dedicated app tree and a thin IPC contract.

**Why:** It keeps the audiobook code isolated from the daemon and from stock `hiby_player`, which makes the system easier to reason about and test.

**Alternatives considered:** Monolithic daemon, in-player patching, or reuse of a general media framework.

**Why not the alternatives:** They either reintroduce coupling to HiBy internals or add unnecessary weight.

---

## 3. Audio Playback Engine

### 3.1 Player design

Use a custom ALSA-based engine in the app process:

- one private queue per book
- sequential playback only
- no shuffle
- no global music queue crossover
- gapless track transitions when decoder and file format permit

The player owns:

- decoded PCM ring buffers
- track open/close state
- ALSA PCM handle
- playback speed state
- current book queue pointer

### 3.2 Decoder matrix

The player should support the required audiobook formats via specific decoders:

| Format | Recommended backend |
|---|---|
| MP3 | `minimp3` or equivalent tiny MP3 decoder |
| M4B / M4A / AAC-in-MP4 | `libmp4ff` + `libfaad2` |
| FLAC | `dr_flac` |
| APE | Monkey's Audio decoder SDK / `MACLib` |
| WAV | `dr_wav` |

Implementation rule:

- Encapsulate all decoder differences behind one `decoder_vtable`.
- If a backend is unavailable on the device, gate it at build time rather than changing the queue model.

Suggested decoder interface:

```c
typedef struct decoder_vtable {
    int  (*open)(struct decoder *d, const char *path);
    int  (*read_pcm)(struct decoder *d, int16_t *dst, size_t frames);
    int  (*seek_ms)(struct decoder *d, uint64_t position_ms);
    int  (*duration_ms)(struct decoder *d, uint64_t *out);
    void (*close)(struct decoder *d);
} decoder_vtable;
```

### 3.3 Queue management

The queue is book-bounded and private to the active book:

- Queue input is the ordered track list for one book.
- Queue output is the single currently playing book only.
- The player does not accept external tracks until the book is closed.
- The queue advances strictly in book order.
- Repeat and shuffle are disabled by default for audiobook mode.

Recommended in-memory queue structure:

```c
typedef struct {
    uint32_t book_id;
    size_t track_count;
    size_t current_index;
    struct track_desc *tracks;
    bool repeat_book;
    bool shuffle;
} playback_queue;
```

### 3.4 Gapless transitions

Gapless playback should be implemented with pre-open and pre-roll:

1. Keep the current decoder alive near the end of the track.
2. Open the next decoder before the current file drains fully.
3. Prime the next PCM buffer.
4. Switch the ALSA writer to the next track without stopping the output stream.

Use a small prefetch window, typically 250-500 ms.

The critical rule is:

- never tear down ALSA between adjacent chapters unless a decode error occurs

### 3.5 Seek implementation

Seek is time-based, not byte-based.

Seek path:

1. Pause decode output.
2. Ask the active decoder to seek to the requested timestamp.
3. Flush stale PCM in the ring buffer.
4. Resume audio output from the new position.
5. Report the actual position to the daemon.

Per-format details:

- MP3: sample-accurate seek through decoder index or frame scan.
- FLAC: seek to sample offset.
- APE: use the decoder's native sample seek.
- WAV: direct byte offset math from sample rate, channels, and bit depth.
- M4B/M4A: use container time seek, then decoder resume.

### 3.6 Playback speed without pitch change

Use a time-stretch stage between decoder output and ALSA output.

Recommended approach:

- default implementation: `SoundTouch`
- if `SoundTouch` proves too heavy on the R1, replace it with a compile-time WSOLA/overlap-add stage in `player/stretch.c`

The app must keep pitch stable and preserve natural speech as much as the platform allows.

Settings:

- default 1.0x
- presets: 0.75x, 0.9x, 1.0x, 1.1x, 1.25x, 1.5x, 1.75x, 2.0x

### 3.7 EOF detection and track advancement

EOF detection should come from the decoder and PCM writer, not screen heuristics.

End-of-track sequence:

1. Decoder returns end of stream or the PCM writer drains completely.
2. Player emits `TRACK_ENDED`.
3. If more tracks remain, player opens the next track.
4. If the final track ends naturally, player emits `BOOK_COMPLETED`.
5. The daemon marks the book complete.

### 3.8 Bluetooth routing

Use ALSA output selection through `bluealsa` when Bluetooth is active.

Recommended device model:

- internal DAC: `default` or the stock ALSA PCM route
- Bluetooth A2DP: `bluealsa`

The app should not hardcode hardware addresses in the UI. Instead, make output selection a setting or a resolved ALSA PCM name.

### 3.9 IPC contract to the daemon

Use a Unix domain socket, not shared memory or pipes.

Recommended socket:

- `/usr/data/audiobooks/run/resume.sock`

Recommended framing:

- `SOCK_SEQPACKET`
- fixed header + typed payload

Suggested header:

```c
typedef enum {
    AB_EVT_BOOK_OPENED = 1,
    AB_EVT_TRACK_CHANGED,
    AB_EVT_POSITION_TICK,
    AB_EVT_PAUSED,
    AB_EVT_RESUMED,
    AB_EVT_SEEK_REQUESTED,
    AB_EVT_SEEK_COMPLETE,
    AB_EVT_EOF_REACHED,
    AB_EVT_BOOK_COMPLETED,
    AB_EVT_APP_EXITING,
    AB_EVT_SCAN_REQUEST,
} audiobook_event_type;

typedef struct {
    uint32_t magic;       /* 'ABIP' */
    uint16_t version;     /* 1 */
    uint16_t type;        /* audiobook_event_type */
    uint32_t payload_len;
    uint64_t seq;
} audiobook_ipc_header;
```

Event payloads should carry:

- `book_id`
- `book_key`
- `track_id`
- `track_ordinal`
- `position_ms`
- `duration_ms`
- `playback_speed_x100`
- `play_state`
- `completed`
- `reason`

Daemon response types:

- `RESUME_PLAN`
- `SAVE_ACK`
- `SCAN_DONE`
- `ERROR`

### 3.10 Architectural decision

**Decision:** Use a dedicated ALSA playback engine with small decoder backends.

**Why:** It is the smallest architecture that can still satisfy book-bounded queue semantics, chapter advance, smart seek, Bluetooth output, and speed control.

**Alternatives considered:** mpd, deadbeef, or reusing the stock HiBy engine as a black box.

**Why not the alternatives:** They either add unnecessary daemon weight or do not naturally fit book-level control and speed handling.

---

## 4. UI Framework

### 4.1 Rendering approach

Use direct framebuffer rendering to `/dev/fb0`.

Do not use:

- SDL2
- LVGL
- a windowing system

Reason:

- no X11 or Wayland stack on the target
- low RAM budget
- deterministic control over the 480x800 RGB565 framebuffer

Render model:

- one back buffer in RAM
- dirty-rectangle repaint
- RGB565 output
- simple alpha blending for text and icons

### 4.2 Touch input

Read touch events directly from `/dev/input/event1`.

Implementation details:

- open device with `O_RDONLY | O_NONBLOCK`
- parse `struct input_event`
- translate multi-touch or single-touch coordinates into screen-space gestures
- maintain a small gesture state machine for taps, swipes, long presses, and edge back gestures

### 4.3 Screen layouts

Use a consistent 480x800 layout system with a top header, main list or cover area, and a bottom action bar.

#### Home

Contents:

- Continue Listening
- Titles
- Authors
- Series
- Folders
- Finished
- Refresh Library

Suggested layout:

- top header: app title and output status
- hero row: Continue Listening
- 2-column or stacked list of library shortcuts
- refresh action pinned near the bottom

#### Titles list

Each row should show:

- cover thumbnail
- book title
- author
- progress bar
- percentage complete or remaining time

One row equals one logical book.

#### Now Playing

Show:

- large cover art
- book title
- author
- chapter title
- chapter ordinal and total chapters
- position in chapter
- remaining time
- total progress
- transport controls
- speed control
- sleep timer
- bookmarks button
- chapter list button

#### Chapter list

Each row should show:

- chapter ordinal
- chapter title
- chapter duration
- currently playing marker

#### Settings

Recommended settings:

- playback speed default
- skip backward interval
- skip forward interval
- smart rewind on/off
- output backend selection
- scan roots
- cache clear
- sleep timer defaults

### 4.4 Navigation model

Use a stack-based navigation model.

Rules:

- Home is the base screen.
- Tapping a title pushes Now Playing.
- Opening chapter list pushes Chapter List.
- Back pops the top screen.
- Back from Home exits the app.
- Long-press actions should never replace Back behavior.

Recommended deep-link behavior:

- If a user taps a title from Home, pressing Back from Now Playing returns to Home.
- If a chapter is opened from Chapter List, Back returns to Now Playing.

### 4.5 Font rendering

Use a bundled TrueType font with FreeType rasterization.

Recommended stack:

- `freetype2`
- one bundled font family under `/usr/share/audiobooks/fonts/`

Recommended fonts:

- `Noto Sans Regular`
- `Noto Sans Bold`

Why FreeType:

- reliable glyph coverage
- good scaling on 480x800
- safe on low memory when paired with a small glyph atlas

Avoid relying on system fontconfig.

### 4.6 Cover art loading and caching

Cover art precedence follows the spec:

1. `book.json` sidecar path
2. `cover.jpg`
3. `folder.jpg`
4. `front.jpg`
5. embedded cover in first track
6. embedded cover in another track
7. default icon

Recommended decoder stack:

- `stb_image.h` for JPEG/PNG/BMP input
- `stb_image_resize2.h` or an equivalent in-tree scaler

Cache strategy:

- decode once
- scale to display-sized thumbnail
- write to `/usr/data/audiobooks/cache/covers/<cover_fingerprint>.rgb565`
- keep a small in-memory LRU cache for the currently visible list

Recommended cache budget:

- 4-8 MB on disk for covers
- 1 MB or less in RAM for the visible window

### 4.7 Architectural decision

**Decision:** Direct framebuffer rendering with a lightweight retained UI.

**Why:** It is the smallest and most deterministic solution for the R1 display and input stack.

**Alternatives considered:** LVGL and SDL2.

**Why not the alternatives:** They add layers the device does not need and would spend RAM on abstraction rather than on playback.

---

## 5. Database Schema

### 5.1 Database location

Primary database:

- `/usr/data/audiobooks/library.db`

Supporting runtime directories:

- `/usr/data/audiobooks/cache/`
- `/usr/data/audiobooks/cache/covers/`
- `/usr/data/audiobooks/cache/search/`
- `/usr/data/audiobooks/resume.d/` for migration compatibility and exports

### 5.2 Schema versioning

Store the schema version in a `schema_meta` table and migrate using `BEGIN IMMEDIATE`.

Migration rules:

- migrations must be idempotent
- every schema change must have a monotonic version number
- on failure, rollback the transaction and keep the previous valid database
- never partially apply a schema change

Recommended schema version table:

```sql
CREATE TABLE IF NOT EXISTS schema_meta (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
```

### 5.3 Core tables

#### `library_roots`

Tracks scan roots and their status.

```sql
CREATE TABLE library_roots (
  root_id INTEGER PRIMARY KEY,
  path TEXT NOT NULL UNIQUE,
  label TEXT,
  enabled INTEGER NOT NULL DEFAULT 1,
  last_scan_started_at INTEGER,
  last_scan_completed_at INTEGER,
  last_scan_status TEXT,
  last_scan_error TEXT,
  last_seen_mtime INTEGER,
  last_seen_size INTEGER
);
```

#### `authors`

```sql
CREATE TABLE authors (
  author_id INTEGER PRIMARY KEY,
  sort_name TEXT NOT NULL,
  display_name TEXT NOT NULL UNIQUE
);
```

#### `series`

```sql
CREATE TABLE series (
  series_id INTEGER PRIMARY KEY,
  sort_name TEXT NOT NULL,
  display_name TEXT NOT NULL UNIQUE
);
```

#### `books`

```sql
CREATE TABLE books (
  book_id INTEGER PRIMARY KEY,
  book_key TEXT NOT NULL UNIQUE,
  title TEXT NOT NULL,
  sort_title TEXT NOT NULL,
  author_id INTEGER REFERENCES authors(author_id),
  narrator TEXT,
  series_id INTEGER REFERENCES series(series_id),
  series_number REAL,
  root_path TEXT NOT NULL,
  cover_path TEXT,
  cover_cache_path TEXT,
  total_duration_ms INTEGER NOT NULL DEFAULT 0,
  track_count INTEGER NOT NULL DEFAULT 0,
  fingerprint TEXT,
  date_added INTEGER,
  date_modified INTEGER,
  last_played_at INTEGER,
  completed INTEGER NOT NULL DEFAULT 0,
  completed_at INTEGER,
  playback_speed REAL NOT NULL DEFAULT 1.0
);
```

#### `tracks`

```sql
CREATE TABLE tracks (
  track_id INTEGER PRIMARY KEY,
  book_id INTEGER NOT NULL REFERENCES books(book_id) ON DELETE CASCADE,
  ordinal INTEGER NOT NULL,
  disc_number INTEGER NOT NULL DEFAULT 1,
  track_number INTEGER NOT NULL DEFAULT 0,
  path TEXT NOT NULL UNIQUE,
  title TEXT NOT NULL,
  sort_title TEXT NOT NULL,
  duration_ms INTEGER NOT NULL DEFAULT 0,
  embedded_chapters INTEGER NOT NULL DEFAULT 0,
  file_size INTEGER NOT NULL DEFAULT 0,
  file_mtime INTEGER NOT NULL DEFAULT 0,
  fingerprint TEXT,
  UNIQUE(book_id, ordinal)
);
```

#### `chapters`

```sql
CREATE TABLE chapters (
  chapter_id INTEGER PRIMARY KEY,
  track_id INTEGER NOT NULL REFERENCES tracks(track_id) ON DELETE CASCADE,
  ordinal INTEGER NOT NULL,
  title TEXT,
  start_ms INTEGER NOT NULL,
  end_ms INTEGER NOT NULL,
  bookmarkable INTEGER NOT NULL DEFAULT 1,
  UNIQUE(track_id, ordinal)
);
```

#### `progress`

```sql
CREATE TABLE progress (
  book_id INTEGER PRIMARY KEY REFERENCES books(book_id) ON DELETE CASCADE,
  track_id INTEGER REFERENCES tracks(track_id),
  track_ordinal INTEGER NOT NULL DEFAULT 1,
  position_ms INTEGER NOT NULL DEFAULT 0,
  total_book_elapsed_ms INTEGER NOT NULL DEFAULT 0,
  playback_speed REAL NOT NULL DEFAULT 1.0,
  last_played_at INTEGER NOT NULL DEFAULT 0,
  completed INTEGER NOT NULL DEFAULT 0,
  completed_at INTEGER NOT NULL DEFAULT 0,
  last_saved_at INTEGER NOT NULL DEFAULT 0,
  protected_until_ms INTEGER NOT NULL DEFAULT 0
);
```

#### `bookmarks`

```sql
CREATE TABLE bookmarks (
  bookmark_id INTEGER PRIMARY KEY,
  book_id INTEGER NOT NULL REFERENCES books(book_id) ON DELETE CASCADE,
  track_id INTEGER REFERENCES tracks(track_id),
  position_ms INTEGER NOT NULL,
  total_book_position_ms INTEGER NOT NULL,
  label TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);
```

#### `scan_state`

```sql
CREATE TABLE scan_state (
  scan_id INTEGER PRIMARY KEY,
  root_id INTEGER REFERENCES library_roots(root_id),
  started_at INTEGER,
  finished_at INTEGER,
  status TEXT,
  changed_count INTEGER NOT NULL DEFAULT 0,
  error TEXT
);
```

#### `settings`

```sql
CREATE TABLE settings (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL,
  scope TEXT NOT NULL DEFAULT 'global'
);
```

### 5.4 Indexes

Create indexes for the common home-screen and playback paths:

```sql
CREATE INDEX idx_books_title_sort ON books(sort_title);
CREATE INDEX idx_books_author ON books(author_id, sort_title);
CREATE INDEX idx_books_series ON books(series_id, series_number, sort_title);
CREATE INDEX idx_books_continue ON progress(completed, last_played_at DESC);
CREATE INDEX idx_tracks_book_ordinal ON tracks(book_id, ordinal);
CREATE INDEX idx_tracks_path ON tracks(path);
CREATE INDEX idx_bookmarks_book_created ON bookmarks(book_id, created_at DESC);
```

Recommended FTS5 table:

```sql
CREATE VIRTUAL TABLE book_search USING fts5(
  book_id UNINDEXED,
  title,
  author,
  narrator,
  series,
  chapter_titles,
  tokenize = 'unicode61 remove_diacritics 2'
);
```

### 5.5 Query model

Common queries:

- Home:
  - `Continue Listening` = active books ordered by `last_played_at DESC`
  - `Finished` = `completed = 1`
- Titles:
  - order by `sort_title`
- Authors:
  - group by `author_id`
- Series:
  - group by `series_id`, order by `series_number`
- Folders:
  - order by `root_path`
- Search:
  - `book_search MATCH ?`

### 5.6 Legacy migration path

Keep the existing metadata model as the import source:

- `catalog.tsv`
- `catalog-books.tsv`
- `resume.d/*.json`

Migration order:

1. Create `library.db`.
2. Import legacy catalog rows into `books` and `tracks`.
3. Import `resume.d` into `progress`.
4. Rebuild `book_search`.
5. Mark schema migration complete.

Important rule:

- `catalog.tsv` and `resume.d` are migration sources and compatibility artifacts, not the final runtime source of truth.

### 5.7 Architectural decision

**Decision:** Use SQLite as the audiobook library source of truth.

**Why:** It supports incremental updates, fast queries, transactional migrations, and clean separation from stock music metadata.

**Alternatives considered:** Flat files only, or continuing to depend on the stock media DB.

**Why not the alternatives:** They make search, filtering, and incremental scan behavior harder to stabilize.

---

## 6. Scanner and Library System

### 6.1 Scan roots

Default audiobook root:

- `/Audiobooks`

The scanner must also treat standalone `.m4b` files outside the root as audiobooks unless explicitly excluded.

### 6.2 Separation from music

Path-based exclusion is mandatory:

- anything under `/Audiobooks` is audiobook content
- anything under `/Music` stays in music
- standalone `.m4b` outside `/Audiobooks` are audiobook content
- ambiguous formats outside `/Audiobooks` stay in music unless explicitly designated otherwise

Never allow audiobook rows to leak into the music search or music queue.

### 6.3 Book grouping rules

Use the spec precedence order:

1. `book.json`
2. embedded metadata
3. directory structure
4. filename inference

Supported layouts include:

```text
/Audiobooks/Book Title/01 - Chapter.mp3
/Audiobooks/Author/Book Title/01 - Chapter.mp3
/Audiobooks/Author/Series/Book Title/Disc 1/01.mp3
/Audiobooks/Book Title.m4b
```

Disc and part folders that should merge into the parent book include:

- `CD1`
- `CD 2`
- `Disc 1`
- `Disk 2`
- `Part 1`
- `Volume 1`

Never merge books solely because the album tag matches.

### 6.4 Track ordering

Sort by:

1. disc number
2. track number
3. embedded chapter start time
4. natural numeric filename order
5. alphabetical filename order

Natural numeric order means `1, 2, 10`, not `1, 10, 2`.

### 6.5 Metadata extraction

Extract and normalize:

- title
- author
- narrator
- series
- series_number
- chapter title
- duration
- embedded chapters
- file size
- file mtime
- fingerprint

Recommended precedence for each field:

1. `book.json`
2. consistent embedded metadata
3. book folder name
4. parent folder name
5. filename
6. placeholder

### 6.6 Cover art discovery

Cover precedence:

1. `book.json` sidecar path
2. `cover.jpg`
3. `folder.jpg`
4. `front.jpg`
5. embedded cover from first track
6. embedded cover from another track
7. default icon

### 6.7 Incremental scanning

The scanner must be incremental by default:

1. Read configured roots.
2. Compare path, size, and modification time.
3. Reparse only new or changed files.
4. Apply changes in a single transaction.
5. Retain the previous valid catalog if the scan fails.
6. Remove records only after confirming files are really gone.

The scanner should not run continuously in the background when nothing changed.

### 6.8 Relationship to `tools/r1_audiobook_db_maint.c`

The existing helper already contains much of the folder and metadata logic needed for the library scan. In the final architecture:

- keep its grouping and cleanup heuristics
- stop treating it as a stock-music DB repair tool
- repurpose it as the on-demand scanner/importer for `library.db`

Recommended split:

- `tools/r1_audiobook_db_maint.c` becomes the scan/import worker
- `app/src/library/` consumes its results or runs the same grouping logic in-process for refresh

### 6.9 Architectural decision

**Decision:** Use incremental on-demand scanning plus SQLite storage.

**Why:** It is the safest fit for the device's RAM limits and the spec's separation rules.

**Alternatives considered:** Continuous polling, or relying on the stock media DB.

**Why not the alternatives:** They either waste resources or make audiobook isolation brittle.

---

## 7. Resume and Progress System

### 7.1 Event-driven resume model

Resume is driven by events, not by guessing UI state.

Resume flow:

1. App opens a book.
2. App sends `BOOK_OPENED`.
3. Daemon loads saved progress.
4. Daemon computes smart rewind if appropriate.
5. Daemon returns a `RESUME_PLAN`.
6. App loads the selected track.
7. App seeks after decoder-ready confirmation.
8. App sends `SEEK_COMPLETE`.
9. Daemon saves progress on ticks and lifecycle boundaries.

### 7.2 IPC events

Minimum event set between app and daemon:

- `BOOK_OPENED`
- `PLAYBACK_STARTED`
- `POSITION_TICK`
- `PAUSED`
- `RESUMED`
- `SEEK_REQUESTED`
- `SEEK_COMPLETE`
- `TRACK_CHANGED`
- `EOF_REACHED`
- `BOOK_COMPLETED`
- `APP_EXITING`
- `SCAN_REQUEST`

### 7.3 Smart rewind

Use the spec's pause-duration rules:

| Pause duration | Rewind |
|---|---|
| < 5 minutes | 0 ms |
| 5 to 60 minutes | 5 seconds |
| 1 to 24 hours | 10 seconds |
| > 24 hours | 15 to 20 seconds |
| after reboot | 10 to 20 seconds |

Make smart rewind configurable and optionally disabled.

### 7.4 Accidental-start protection

If a book with saved progress is accidentally started from the beginning, the saved position must remain protected until one of these happens:

- user explicitly chooses Restart Book
- playback continues from the beginning for a configured guard interval
- the user seeks or navigates intentionally
- the new playback position moves beyond the old resume point

Implementation detail:

- the daemon should mark an existing progress record as protected while the book is in the start-guard window
- the app should not overwrite a good resume point with a fresh zero unless the user explicitly requests restart

### 7.5 Completion detection

Mark a book finished only when:

- the final track reaches natural EOF
- or the user explicitly selects Mark as Finished

Do not mark complete merely because a seek landed near the end.

When complete:

- set `completed = 1`
- set `completed_at`
- move the book out of Continue Listening
- make tapping it start from the beginning

### 7.6 Atomic write requirements

Critical progress writes must be atomic:

- write to a temp file or temp SQLite transaction
- `fsync()` the data
- `rename()` into place if using JSON compatibility files
- `fsync()` the directory that contains the file

If SQLite is the primary store, progress commits must still be wrapped in one transaction with `BEGIN IMMEDIATE` and `COMMIT`.

### 7.7 What to port from the existing daemon code

Keep or port from the current `src/` implementation:

- `src/resume.c`
- `src/resume.h`
- the atomic record write behavior
- smart rewind calculation
- completion and protection state logic
- the stable `book_key` and `safe_id()` concepts

Most of the current player-memory and UI-automation code should not survive into the final audiobook path:

- `src/player.c`
- `src/proc_mem.c`
- `src/helpers.c`
- `src/ui.c`

### 7.8 Architectural decision

**Decision:** Resume is an explicit event protocol with atomic persistence.

**Why:** It is the only reliable way to satisfy the spec's correctness requirements without screen timing heuristics.

**Alternatives considered:** Polling, framebuffer inspection, or pure app-local state only.

**Why not the alternatives:** They either fail under low-memory conditions or do not survive crashes and power loss as reliably.

---

## 8. Daemon Integration

### 8.1 Daemon evolution

Current daemon state:

- polling loop
- stock player memory reads
- helper subprocesses
- UI and path heuristics

Final daemon state:

- event consumer
- progress persistence
- smart rewind and completion policy
- optional scan/import coordination

### 8.2 IPC mechanism

Use a Unix domain socket.

Recommended choices:

- path: `/usr/data/audiobooks/run/resume.sock`
- type: `SOCK_SEQPACKET`
- protocol: typed binary frames or length-prefixed JSON, but not a shared-memory design

Why Unix socket:

- local only
- easy to debug
- no extra daemon or broker
- message boundaries are preserved

### 8.3 What the daemon does on each event

#### `BOOK_OPENED`

- load or create the progress record
- determine whether the book is completed
- apply smart rewind if enabled
- send back a `RESUME_PLAN`

#### `PLAYBACK_STARTED` / `RESUMED`

- update the active book state
- clear any stale protection timers if appropriate

#### `POSITION_TICK`

- debounce progress writes
- update last played time
- update current position and track

#### `PAUSED`

- force an immediate save
- preserve the current track and position

#### `SEEK_REQUESTED`

- save the pre-seek position
- set a short protection window so the seek does not get misinterpreted as a bad start

#### `TRACK_CHANGED`

- commit the current track progress
- update the active track pointer
- keep the book-level queue state in sync

#### `EOF_REACHED`

- if not the final track, advance
- if final track, mark completion

#### `APP_EXITING`

- flush any pending progress
- close the active book state cleanly

### 8.4 What the daemon does when idle

The daemon must sleep when nothing is happening.

Idle behavior:

- block on the Unix socket
- use a timerfd only for deferred save flushes
- avoid polling `/proc`
- avoid polling the framebuffer
- avoid scanning the filesystem unless a refresh is requested

### 8.5 Config changes needed

Current config values in `src/config.c` are dominated by stock-player and UI-automation settings. In the final daemon config, remove or deprecate:

- player memory address fields
- framebuffer/touch automation fields
- direct-open helper fields
- track-list polling fields
- UI seek fallback fields

Keep or add:

- resume directory
- database path
- socket path
- save debounce interval
- smart rewind thresholds
- completion threshold
- scan refresh timeout
- output log path

### 8.6 Code that stays, goes, changes

#### Keep

- `src/config.c`
- `src/config.h`
- `src/log.c`
- `src/log.h`
- `src/resume.c`
- `src/resume.h`
- the stable `book_key` and record path logic

#### Change

- `src/main.c`
  - replace the polling loop with socket/event loop logic
- `src/state.c`
  - turn the runtime state machine into an event reducer
- `src/catalog.c`
  - keep for migration and lookup support, but not as the main runtime library store

#### Remove from the final audiobook runtime

- `src/player.c`
- `src/player.h`
- `src/proc_mem.c`
- `src/proc_mem.h`
- `src/helpers.c`
- `src/helpers.h`
- `src/ui.c`
- `src/ui.h`
- `src/shadow.c`
- `src/shadow.h` if it remains migration-only

### 8.7 Architectural decision

**Decision:** Convert the daemon from polling to event-driven state handling.

**Why:** The app already owns playback. The daemon should persist and arbitrate progress, not chase the stock player.

**Alternatives considered:** Keeping memory polling as a backup path.

**Why not the alternative:** It reintroduces the very fragility the hybrid architecture is meant to remove.

---

## 9. Launcher Redirect

### 9.1 Redirect strategy

Do not patch `hiby_player` for the Audiobooks tile.

Use a data-only launcher redirect:

- patch launcher resource text so the tile is labeled `Audiobooks`
- patch the launcher target entry so it resolves to the audiobook launcher wrapper

Recommended target:

- `/usr/bin/r1_audiobook_launch.sh`

### 9.2 Resource patch details

The existing resource patch script already handles the user-facing strings:

- `tools/patch_r1_resource_text.py`

Keep that script for labels such as:

- `Audiobooks`
- `Titles`
- `Authors`
- `Series`
- `Folders`
- `Refresh Library`

For the actual route target, patch the launcher's data entry in the stock resource/config layer rather than the player binary.

Recommended launcher data file:

- `usr/resource/config.json`

Recommended target field:

- the launcher entry that maps the Books/Audiobooks tile to the command or route for `/usr/bin/r1_audiobook_launch.sh`

If the stock launcher uses a route id rather than a literal path, patch the route target field in the same data file instead of inventing a new mechanism.

### 9.3 Wrapper behavior

`/usr/bin/r1_audiobook_launch.sh` should:

1. validate required runtime paths
2. optionally create the cache and data directories
3. optionally trigger a one-time refresh if the library is missing
4. `exec /usr/bin/r1_audiobook_app`
5. fall back to stock behavior if the app cannot start

The wrapper must not daemonize.

### 9.4 Exit behavior

The app exits back to the launcher by terminating normally.

Requirements:

- close ALSA cleanly
- flush pending resume state
- close SQLite cleanly
- close the socket to the daemon
- return `0` or a clean nonzero code that the wrapper can log

### 9.5 Crash fallback

Crash behavior should be safe:

- the launcher remains intact
- the route target is still data-only
- the user can tap the tile again after a crash
- if the app binary is missing or invalid, the wrapper can fall back to stock `hiby_player`

### 9.6 Architectural decision

**Decision:** Redirect the tile through a wrapper script and launcher data.

**Why:** It gives a clean rollback path and avoids touching the stock player binary.

**Alternatives considered:** `hiby_player` code cave routing or touch automation.

**Why not the alternatives:** Those reintroduce binary fragility and hidden control flow.

---

## 10. Build and Packaging

### 10.1 Build the audiobook app

Recommended build command pattern:

```text
zig cc -target mipsel-linux-musleabi -O2
```

Build outputs:

- `build/r1_audiobook_app`
- optional host test binary

Suggested app link line:

```text
-lasound -lsqlite3 -lpthread -lm -ldl
```

Only add extra libraries when the chosen decoder or font stack truly requires them.

### 10.2 Build the daemon

Keep the current daemon build flow in `tools/build_resume_daemon.py`, but slim the source list to the final event-driven runtime.

Current build script:

- `tools/build_resume_daemon.py`

Final daemon binary:

- `build/r1_audiobook_resume_daemon`

Recommended source set after refactor:

- `main.c`
- `config.c`
- `log.c`
- `resume.c`
- `state.c`
- `ipc.c`
- `db_import.c` or `catalog.c` if needed for migration

Remove the polling-only sources from the final daemon build.

### 10.3 Package into UPT firmware

Use the existing firmware build pipeline:

- `tools/build_firmware.py`
- `tools/build_firmware_overlay.json`
- `tools/build_r1_upt.py`

Final packaging sequence:

1. build app
2. build daemon
3. build scanner/import helper
4. install binaries into the rootfs overlay
5. patch launcher resources
6. build the squashfs
7. wrap the UPT firmware

### 10.4 Rootfs layout

Recommended install locations:

```text
/usr/bin/r1_audiobook_app
/usr/bin/r1_audiobook_launch.sh
/usr/bin/r1_audiobook_resume_daemon
/usr/bin/r1_audiobook_db_maint
/usr/bin/r1_audiobook_refresh.sh
/usr/share/audiobooks/fonts/
/usr/share/audiobooks/icons/
/usr/data/audiobooks/
/usr/data/audiobooks/library.db
/usr/data/audiobooks/cache/
/usr/data/audiobooks/cache/covers/
/usr/data/audiobooks/resume.d/
/usr/data/audiobooks/run/
```

### 10.5 Build dependencies

Host-side build tools:

- Zig
- Python 3
- `mksquashfs`
- `unsquashfs`
- `sqlite3` development headers or amalgamation

Runtime dependencies on the device:

- ALSA
- SQLite
- Bluetooth route support already present in the stock firmware

### 10.6 Packaging decision

**Decision:** Keep the existing UPT firmware format and rootfs overlay flow.

**Why:** It minimizes delivery risk and preserves the known-good update path.

**Alternatives considered:** New update packaging or a custom rootfs rewrite.

**Why not the alternatives:** They create unnecessary release engineering work for this feature.

---

## 11. Development Phases

### 11.1 Phase 1: App shell + launcher handoff

Goal:

- the Audiobooks tile launches the new app
- the app opens Home
- Back exits to launcher
- no playback engine work yet beyond stubs

Files to create:

- `app/src/main.c`
- `app/src/app.c`
- `app/src/ui/fb.c`
- `app/src/ui/input.c`
- `app/src/ui/screens/home.c`
- `app/src/ipc/client.c`
- `app/src/db/db.c`
- `app/src/db/migrations.c`
- `app/build.zig`
- `tools/build_audiobook_app.py`
- `tools/r1_audiobook_launch.sh`

Success criteria:

- app starts on device
- framebuffer paints correctly
- touch input works
- launcher returns cleanly on exit

Dependencies:

- launcher redirect data
- font loading
- basic DB open/migration

### 11.2 Phase 2: Playback engine

Goal:

- play one book end to end
- support all required codecs
- handle sequential track advance
- basic seek and speed

Files to create:

- `app/src/player/player.c`
- `app/src/player/decoder.c`
- `app/src/player/queue.c`
- `app/src/player/stretch.c`
- `app/src/player/alsa.c`
- `app/src/ui/screens/now_playing.c`
- `app/src/ui/screens/chapters.c`

Success criteria:

- MP3, FLAC, WAV, M4B, and APE books play
- gapless chapter advance works
- seek works reliably
- Bluetooth route works through ALSA

Dependencies:

- decoder backends
- cover art loading
- progress event IPC

### 11.3 Phase 3: Resume / correctness

Goal:

- resume state survives pause, exit, reboot, and power loss
- smart rewind works
- accidental-start protection works
- completion detection is correct

Files to create or refactor:

- `src/main.c`
- `src/state.c`
- `src/resume.c`
- `src/resume.h`
- `app/src/resume/resume_client.c`
- `app/src/ipc/protocol.c`

Success criteria:

- resume accuracy within roughly two seconds
- completed books start from the beginning
- progress is not overwritten by accidental taps

Dependencies:

- stable event protocol
- transaction-safe persistence

### 11.4 Phase 4: Audiobook UX

Goal:

- ship the audiobook-specific screens and navigation model
- expose authors, series, folders, finished, search, bookmarks, and sleep timer

Files to create:

- `app/src/ui/screens/authors.c`
- `app/src/ui/screens/series.c`
- `app/src/ui/screens/folders.c`
- `app/src/ui/screens/finished.c`
- `app/src/ui/screens/settings.c`
- `app/src/ui/screens/home.c`
- `app/src/ui/screens/titles.c`

Success criteria:

- the UI matches the spec screens
- the chapter list is accessible from Now Playing
- Back behavior is consistent

Dependencies:

- database indexes
- cover cache
- book and progress queries

### 11.5 Phase 5: Hardening + packaging

Goal:

- make the feature reliable on the actual device
- package the final UPT
- validate rollback

Files to update:

- `tools/build_firmware.py`
- `tools/firmware_overlay.json`
- `tools/patch_r1_resource_text.py`
- `tools/verify_r1_audiobook_build.py`
- `tools/test_*` harnesses

Success criteria:

- clean boot after install
- no launcher regression
- no music regression
- no low-memory instability
- safe uninstall or rollback path exists

Dependencies:

- end-to-end smoke tests
- import/migration tests
- crash recovery verification

### 11.6 Phase sequencing rule

Do not spend effort polishing secondary library views until:

- direct book playback works
- resume works
- completion works
- the app survives reboot and power-off cycles

### 11.7 Architectural decision

**Decision:** Build in five incremental phases.

**Why:** Each phase produces a testable artifact and limits risk.

**Alternatives considered:** Building the entire feature in one pass.

**Why not the alternative:** It would make root-cause analysis and rollback much harder.

---

## 12. Risk Assessment and Mitigations

### 12.1 Playback engine bugs

Risk:

- decoder edge cases
- ALSA underruns
- chapter transition glitches
- seek inaccuracies

Mitigation:

- build host-side decoder tests
- run device smoke tests on several book formats
- validate chapter advance and EOF on multi-file books

### 12.2 Memory pressure

Risk:

- the app or cover cache could overrun the remaining RAM

Mitigation:

- keep a tight module budget
- use small buffers
- decode covers to a display-sized thumbnail only
- avoid SDL2, LVGL, FFmpeg-sized general frameworks, and background polling

### 12.3 Database corruption

Risk:

- interrupted scan or interrupted write could break the library

Mitigation:

- SQLite transactions
- atomic rename for compatibility exports
- keep the previous valid database until the new one is committed
- maintain schema versioning

### 12.4 Launcher redirect failure

Risk:

- route patch points to the wrong command
- launcher data format differs from expectations

Mitigation:

- keep the redirect data-only
- use a wrapper script
- preserve stock `hiby_player`
- validate on-device before release

### 12.5 Bluetooth output instability

Risk:

- `bluealsa` may change latency or route behavior

Mitigation:

- keep Bluetooth as a selectable ALSA output path
- test both internal DAC and Bluetooth before release
- do not mix routing control with UI control logic

### 12.6 Scanner mistakes

Risk:

- a bad grouping rule could merge two books incorrectly
- a missing tag could hide a book

Mitigation:

- favor folder boundaries
- never merge solely on album tag
- keep the legacy catalog import as a fallback reference
- validate against known fixtures

### 12.7 Crash / rollback plan

Rollback strategy:

1. remove or disable the launcher redirect data entry
2. keep the stock music player intact
3. leave the stock launcher and stock audio stack in place
4. uninstall the audiobook binaries and data files if needed

The rollback does not require reversing any `hiby_player` binary patch because the final design does not use one.

### 12.8 What to prototype first

Prototype order:

1. direct launch and framebuffer paint
2. one-book playback with ALSA
3. socket IPC to the daemon
4. resume write/read loop
5. scanner import path
6. cover art cache

### 12.9 Architectural decision

**Decision:** Prototype the playback engine before polishing the UI.

**Why:** Playback correctness is the highest-risk part of the system and the one least forgiving of late discovery.

**Alternatives considered:** UI-first development.

**Why not the alternative:** A polished interface is useless if the engine cannot seek, advance, or resume correctly.

---

## Final Architecture Summary

The implementation Forge should build is:

- a standalone audiobook app at `/usr/bin/r1_audiobook_app`
- a launcher wrapper at `/usr/bin/r1_audiobook_launch.sh`
- an event-driven resume daemon at `/usr/bin/r1_audiobook_resume_daemon`
- a SQLite-backed library at `/usr/data/audiobooks/library.db`
- an on-demand scanner/import path derived from `tools/r1_audiobook_db_maint.c`
- a direct framebuffer UI on `/dev/fb0`
- a direct ALSA playback engine with per-book private queues
- a Unix-socket IPC contract for progress and resume
- a data-only launcher redirect that leaves stock `hiby_player` untouched

That combination satisfies the approved hybrid strategy and gives Forge a concrete path to code immediately.
