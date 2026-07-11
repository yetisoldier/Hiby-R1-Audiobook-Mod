# Audiobook Architecture Redesign Specification

## Design Principle

A book is one logical library item with an ordered internal queue and a book-level resume record. The user selects and listens to a book, not a collection of audio files.

## Current Architecture (What We're Replacing)

- Books are represented as `.m3u` playlist files in `_views/Titles/`, `_views/Authors/`, `_views/Series/`
- Tapping a title opens a folder view showing `.m3u` files
- Tapping a `.m3u` opens a track list showing individual MP3/M4B files
- User must manually tap a track to start playback
- C daemon tries to detect the track list via framebuffer and auto-tap
- This is fragile, slow (2-5s delay), and memory-intensive (OOM on 56 MB device)

## Target Architecture

### Book as Logical Item

- Tapping a book title opens playback **directly on the Now Playing screen**
- No playlist, folder, or track-selection screen appears
- A multi-file audiobook behaves exactly like a single-file audiobook
- Internal track/file boundaries visible only when user deliberately opens chapter list

### Book Open Behavior

- **New book** (no resume record): starts from beginning (track 1, position 0)
- **Partially listened book**: resumes from correct file and exact position
- **Completed book**: starts again from beginning

### Title-Open Handler (Binary Patch in hiby_player)

When a title is tapped in the native hub:
1. Look up the book's resume record from `/usr/data/audiobooks/resume.d/`
2. Resolve the saved track index and position
3. Call `shared_media_open` (0x49E200) directly with the correct track
4. Skip `vg_listview_explorer` and track list views entirely
5. Land on Now Playing screen

Key addresses:
- `0x540A80` — native hub title-open wrapper
- `0x540B0C` — follow-up call in title-open path
- `0x5401C0` — deeper open helper
- `0x49E200` — shared_media_open (force-play target)

### Metadata (Keep Existing Approach)

- Catalog files: `catalog.tsv`, `catalog-books.tsv`, `catalog-view-title.tsv`, `catalog-view-author.tsv`, `catalog-view-series.tsv`
- Book keys: `v1_<author>_<title>` format
- Resume records: JSON in `/usr/data/audiobooks/resume.d/` with schema_version 3
- Fields: `book_key`, `root_hiby_path`, `current_path`, `track_index`, `track_count`, `position_ms`, `completed`

### Audiobook/Music Separation

- Audiobooks have a dedicated top-level Audiobooks section
- Audiobook files must NOT appear in:
  - Music Songs, Albums, Artists, Genres
  - Music search results, Recently Played, Most Played
  - Shuffle All, Music-generated queues
  - Automatic playback after final music track
- Separation based on file location under `/Audiobooks` root
- This is already implemented via scanner skip + DB maintenance

### C Daemon Role (Simplified)

The daemon no longer needs framebuffer detection or auto-tap. Its role becomes:
1. **Position tracking**: Read position/duration from hiby_player memory
2. **Save management**: Write resume records to `/usr/data/audiobooks/resume.d/`
3. **Restore coordination**: When hiby_player opens a book (detected via path change), read the saved position and seek to it
4. **Track advancement**: Detect track completion and update resume record
5. **Completed detection**: Detect when a book is finished and mark as completed

The daemon drops:
- Framebuffer reading (/dev/fb0)
- Screen classification (audiobook_track_list_visible)
- Touch event injection (/dev/input/event1)
- Auto-tap logic (idle scan, fast-poll, idle_autotap_fired)
- All framebuffer-related memory allocation

This dramatically reduces memory usage and eliminates the OOM problem.

### Chapter List (Optional)

- User can open a chapter list from Now Playing (stock R1 feature)
- Chapter list shows individual tracks/files
- User can tap a chapter to jump to it
- This is the stock player's existing track list, accessed differently

### Playback Behavior

- **Gapless playback**: Use stock R1 gapless engine for track-to-track transitions
- **Track advancement**: Automatic when current track ends (stock behavior)
- **Playback speed**: Stock R1 speed controls apply
- **Sleep shutdown**: Stock R1 sleep timer works
- **Power-off resume**: Daemon saves position before shutdown

### Views

- **Titles**: All books sorted alphabetically by title
- **Authors**: Books grouped by author, sorted by author then title
- **Series**: Books with series metadata, sorted by series then title
- **Library**: All books (stock folder view, kept for power users)

### Memory Budget (56 MB Total)

- hiby_player: ~40 MB
- bluetooth stack: ~3 MB
- adbd: ~1 MB
- db_watch.sh: ~0.5 MB
- C daemon (simplified, no framebuffer): ~0.5 MB (100 KB binary, ~400 KB RSS)
- Free: ~11 MB

## Implementation Phases

### Phase 1: Binary Patch — Direct Book Open
- Patch `0x540A80` title-open handler to call `shared_media_open` directly
- Resolve saved track from resume record (or track 1 for new books)
- Skip track list view entirely
- Keep daemon for position save/restore only (no framebuffer)

### Phase 2: Daemon Simplification
- Remove framebuffer detection code from state.c
- Remove auto-tap logic (idle scan, fast-poll, touch injection)
- Remove ui.c framebuffer classification
- Keep position tracking, save management, restore coordination
- Reduce binary size further (estimated ~80 KB without framebuffer code)

### Phase 3: Polish
- Chapter list access from Now Playing
- Series view population
- Edge case handling (empty books, corrupt resume records)
- On-device validation