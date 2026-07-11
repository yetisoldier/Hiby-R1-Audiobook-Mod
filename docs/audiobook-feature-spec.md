# HiBy R1 Audiobook Feature Specification

> Source: Eric Antonson, based on ChatGPT recommendation + project experience
> Date: 2026-07-11
> Status: Target specification — guide for all implementation work

## 1. Purpose

The audiobook feature should make the HiBy R1 behave like a dedicated audiobook player while preserving its existing music-player functionality.

The central design principle is:

**The user selects and listens to a book, not a collection of audio files.**

A multi-file audiobook must therefore behave exactly like a single-file audiobook from the user's perspective. The internal track or file boundaries should be visible only when the user deliberately opens the chapter list.

When the user taps a book:
- A new book starts from the beginning.
- A partially listened book resumes from the correct file and exact position within that file.
- A completed book starts again from the beginning.
- Playback opens directly on the Now Playing screen.
- No playlist, folder, or intermediate track-selection screen appears.

## 2. HiBy R1 Foundation

The R1 already contains much of the playback infrastructure needed for audiobooks. It runs HiByOS on Linux, supports microSD cards up to 2 TB, and provides a stock audio engine with gapless playback, playback-speed controls, sleep shutdown, database scanning, progress seeking, and power-off playback memory. Official firmware 1.4 added M4B playback support.

The audiobook implementation should reuse the stock decoder and audio-output pipeline. It should add an audiobook library, book-level queue management, audiobook-specific controls, and reliable progress persistence around that engine.

It should not attempt to replace the R1 audio engine.

## 3. Core User Experience

### 3.1 Audiobooks as a separate media type

Audiobooks must have a dedicated top-level Audiobooks section.
Audiobook files must not appear in:
- Music Songs
- Music Albums
- Music Artists
- Music Genres
- Music search results
- Music Recently Played
- Music Most Played
- Shuffle All
- Music-generated queues
- Automatic playback after the final music track

The separation must be based primarily on the file's location under an audiobook root such as:
```
/Audiobooks
```

M4B files should also be recognised as audiobooks by file type, even when placed outside the normal audiobook directory. MP3, FLAC, M4A, OGG, Opus, and other ambiguous audio formats should normally require placement under /Audiobooks or an explicit audiobook designation.

A post-scan database filter can be retained as a safety measure, but path-based exclusion should happen before music-library rows are generated whenever possible.

### 3.2 Audiobook home screen

The Audiobooks section should provide:
- Continue Listening
- Titles
- Authors
- Series
- Folders
- Finished
- Refresh Library

On the R1's small display, Continue Listening can be a single prominent row at the top rather than a separate screen.

The default Titles view should show one row per logical book, not one row per file.

Each book row should display as much of the following as the screen permits:
- Cover art
- Book title
- Author
- Current chapter
- Percentage complete
- Time remaining
- Finished indicator

### 3.3 Tapping a title

Tapping a book title must immediately execute the following sequence:
1. Resolve the selected book_id.
2. Load the book's ordered internal track list.
3. Read its saved progress record.
4. Select the saved track, or track 1 for a new book.
5. Start playback.
6. Seek to the saved position after the decoder reports that the track is ready.
7. Open the Now Playing screen.

The user should never be required to:
- Open an M3U file.
- View a list of MP3 files.
- Select the first track.
- Remember which track was playing.
- Manually seek to the previous position.

The transition should not briefly flash a track list or rely on an injected touch event.

## 4. The Logical Book Model

Every audiobook should be represented internally as one Book object.

```
Book
  book_id
  title
  sort_title
  author
  narrator
  series
  series_number
  root_path
  cover_path
  total_duration_ms
  track_count
  fingerprint
  date_added
  date_modified
  completed
```

Each book contains one or more ordered Track records:

```
Track
  track_id
  book_id
  ordinal
  disc_number
  track_number
  path
  title
  duration_ms
  embedded_chapters
```

A single M4B or MP3 audiobook is therefore a Book containing one Track. A 150-file MP3 audiobook is a Book containing 150 Tracks.

The playback layer receives the complete ordered Track collection as one private book queue.

## 5. Identifying and Grouping Books

### 5.1 Supported directory layouts

The scanner should support at least these common layouts:
```
/Audiobooks/Book Title/01 - Chapter.mp3
/Audiobooks/Book Title/02 - Chapter.mp3
/Audiobooks/Author/Book Title/01 - Chapter.mp3
/Audiobooks/Author/Book Title/02 - Chapter.mp3
/Audiobooks/Author/Series/Book Title/Disc 1/01.mp3
/Audiobooks/Author/Series/Book Title/Disc 2/01.mp3
/Audiobooks/Book Title.m4b
```

Folder-based grouping is a sensible fallback. Each folder can be treated as a book, each file directly under an audiobook root as a separate book, or nested author folders as organisational containers.

### 5.2 Grouping rules

The recommended grouping order is:
1. Explicit sidecar metadata.
2. Embedded book metadata.
3. Directory structure.
4. Filename-based inference.

A simple optional sidecar file should be supported:
```
book.json
```

Example:
```json
{
  "title": "The Book Title",
  "author": "Author Name",
  "narrator": "Narrator Name",
  "series": "Series Name",
  "series_number": 2,
  "cover": "cover.jpg"
}
```

### 5.3 Disc and part folders

Folders named like the following must not become separate books:
```
CD1, CD 2, Disc 1, Disk 2, Part 1, Part 02, Volume 1
```

They should be interpreted as subdivisions of the nearest parent book folder. The scanner should merge these directories into one logical book and use the disc or part number as the first sorting key.

### 5.4 Track order

Tracks should be sorted using this precedence:
1. Disc number
2. Track number
3. Embedded chapter start time
4. Natural numeric filename order
5. Alphabetical filename order

Natural numeric sorting means:
```
1.mp3
2.mp3
10.mp3
```
not:
```
1.mp3
10.mp3
2.mp3
```

### 5.5 Avoiding incorrect merges

Two books should not be merged solely because they have the same Album tag. Folder boundaries must take precedence unless:
- The folders are recognised disc or part subdivisions.
- An explicit sidecar joins them.
- All strong metadata and file fingerprints indicate that they form one book.

## 6. Metadata Handling

> **Project note:** Keep the existing metadata approach (catalog.tsv, catalog-books.tsv, book keys v1_<author>_<title>, resume records in /usr/data/audiobooks/resume.d/) unless the existing approach conflicts with a specific requirement here.

### 6.1 Recommended embedded metadata

For multi-file MP3 books:
- Album = Book title
- Album Artist = Author
- Artist = Author, narrator, or both
- Title = Chapter title
- Composer = Narrator, optional
- Track Number = Chapter or file order
- Disc Number = Disc or part order
- Genre = Audiobook, optional

Series information can be read from:
- Grouping or Content Group
- Series-specific custom tags
- Folder structure
- book.json

Genre should never be the only way a book is identified.

### 6.2 Metadata precedence

For each field:
1. book.json
2. Consistent embedded metadata
3. Book folder name
4. Parent folder name
5. Filename
6. A safe placeholder such as "Unknown Author"

Conflicting tags should not prevent a book from loading.

### 6.3 Chapter metadata

For a multi-file audiobook, each file should be treated as a chapter unless it contains its own embedded chapter markers.

For a single MP3 file, ID3 CHAP frames should be parsed when present.

For M4B files:
- Parse embedded chapter markers when the stock media parser exposes them.
- Display embedded chapter titles.
- Fall back to one chapter covering the entire file when no chapter information exists.

The chapter list should combine both models.

## 7. Resume and Progress Behaviour

### 7.1 Separate progress for every book

Each book must have its own independent progress record:
```
Progress
  book_id
  track_id
  track_ordinal
  position_ms
  total_book_elapsed_ms
  playback_speed
  last_played_at
  completed
  completed_at
```

Switching from one audiobook to another must not overwrite either book's position.
Switching from an audiobook to music must not discard audiobook progress.
Returning to the audiobook after playing music must restore:
- The correct book
- The correct file
- The correct time within that file
- The audiobook-specific playback speed
- The audiobook queue

### 7.2 When progress should be saved

Progress should be saved:
- Every 5 to 10 seconds during playback
- When paused
- Before seeking
- After seeking
- Before changing tracks
- Immediately after changing tracks
- When Bluetooth disconnects
- When headphones are removed, if playback pauses
- When leaving the Now Playing screen
- When switching to music
- When the screen turns off
- Before sleep shutdown
- Before normal power-off
- When the player process exits
- When the microSD card is removed

Writes should be debounced. Lifecycle events must trigger an immediate save.
The file or database update must be atomic.

### 7.3 Accidental taps

An accidental start should not erase a meaningful existing position.
If a book with saved progress is started at track 1, position 0, the old position should remain protected until one of the following occurs:
- The user explicitly selects Restart Book.
- Playback continues from the beginning for at least a configurable period.
- The user seeks or navigates intentionally.
- The book reaches a later position than the previous resume point.

A new book can begin saving normally after approximately 5 to 10 seconds of playback.

### 7.4 Smart rewind on resume

| Pause duration | Rewind |
|---|---|
| < 5 minutes | 0 (exact position) |
| 5 to 60 minutes | 5 seconds |
| 1 to 24 hours | 10 seconds |
| > 24 hours | 15 to 20 seconds |
| After device reboot | 10 to 20 seconds |

Smart rewind should be configurable and may be disabled.

### 7.5 Restoring playback

Resume must use an event-driven state machine:
1. Load the saved track.
2. Wait for a confirmed track-loaded or decoder-ready event.
3. Validate the reported duration.
4. Clamp the saved position to the valid track duration.
5. Seek to the saved position.
6. Confirm that the actual position is within tolerance.
7. Retry once if the initial seek was ignored.
8. Start or remain paused according to the requested action.

Resume should not be triggered by guessing when a screen has finished drawing.

### 7.6 Missing or changed tracks

If the saved track no longer exists:
1. Try to match its relative filename.
2. Try its stored track ordinal.
3. Select the nearest previous available track.
4. Clamp the saved total-book position into the rebuilt book timeline.
5. Notify the user only if no reasonable mapping is possible.

A renamed book folder should retain progress where possible by matching a content fingerprint.

## 8. Book Completion

A book should be marked finished when:
- The final track reaches natural end-of-file, or
- The user selects Mark as Finished.

Simply seeking near the end should not immediately mark the book finished.

When a book is finished:
- Progress displays 100 percent.
- It appears in Finished.
- It is removed from Continue Listening.
- Tapping it starts playback from the beginning.
- A Restart Book action clears the finished state and progress.
- Mark as Unfinished restores it to the active library.

## 9. Playback Queue Behaviour

### 9.1 Book-bounded queue

Every audiobook must have a private queue containing only its own tracks.

At the end of a track:
- Play the next track in the book.
- Continue without user interaction.
- Update the progress record to the new track.
- Avoid an audible gap when the files permit gapless transition.

At the end of the final track:
- Stop playback.
- Mark the book finished.
- Do not begin another audiobook.
- Do not return to the music queue.
- Do not loop back to track 1 unless the user explicitly enabled repeat.

### 9.2 Disable inappropriate music modes

While an audiobook is active:
- Shuffle must be disabled.
- Single-track repeat should be hidden or disabled by default.
- List loop should be disabled by default.
- Play Through Albums and Play Through Folders must not escape the current book.
- Music autoplay rules must not apply.
- The user's music playback mode must be restored when returning to music.

### 9.3 Selecting a chapter manually

Selecting a chapter from the chapter list should:
- Keep the complete book queue loaded.
- Start the selected chapter from its beginning.
- Update the book's active progress.
- Continue to subsequent chapters normally.

It must not create a new queue containing only that chapter.

## 10. Audiobook Now Playing Screen

The audiobook Now Playing screen should display:
- Book cover
- Book title
- Author
- Current chapter title
- Chapter number and total chapters
- Current position in chapter
- Remaining time in chapter
- Overall book progress
- Remaining time in book
- Play and pause
- Skip backward
- Skip forward
- Previous chapter
- Next chapter
- Playback speed
- Sleep timer
- Chapter list
- Bookmark command

### 10.1 Skip controls

Recommended defaults:
- Skip backward: 15 seconds
- Skip forward: 30 seconds

Options: 10s, 15s, 30s, 45s, 60s

### 10.2 Previous chapter behaviour

When Previous Chapter is pressed:
- If more than 10 seconds into the chapter, return to the beginning of the current chapter.
- If within the first 10 seconds, move to the previous chapter.

### 10.3 Physical buttons

Recommended audiobook mappings:
- Play/Pause short press: Play or pause
- Next short press: Next chapter
- Volume buttons: Volume

The audiobook implementation must not interfere with hardware reset, firmware recovery, or boot-time button combinations.

## 11. Playback Speed

- Default speed is 1.0x.
- Speed is remembered per book.
- A global default may be set for new books.
- Speed changes should not alter the saved media timestamp.
- Remaining-time estimates should account for the active speed.
- Speech pitch should remain natural where the stock DSP supports pitch correction.

Presets: 0.75x, 0.9x, 1.0x, 1.1x, 1.25x, 1.5x, 1.75x, 2.0x

Only speed values proven stable on the R1 should be exposed.

## 12. Sleep Timer

Recommended choices: 10min, 15min, 30min, 45min, 60min, 90min, End of chapter, End of current track

At expiration:
1. Save progress.
2. Fade audio over several seconds.
3. Pause playback.
4. Optionally power off if the user selected Pause and Power Off.

The next resume should use smart rewind from the position at which the fade began.

## 13. Bookmarks

Each bookmark stores:
```
book_id
track_id
position_ms
total_book_position_ms
label
created_at
```

Default labels: `Chapter 12, 18:42`

Actions: Add, View, Play from, Rename, Delete

Bookmarks should follow reliable playback and resume functionality in development priority.

## 14. Cover Art

Cover-art precedence:
1. Sidecar path specified in book.json
2. cover.jpg
3. folder.jpg
4. front.jpg
5. Embedded cover from the first track
6. Embedded cover from another track
7. Default audiobook icon

The scanner should cache a display-sized image (max ~1024x1024).

## 15. Starting Playback from Folders

Audiobook-specific behaviour must also apply when the user enters /Audiobooks through a generic folder browser.

Tapping a book folder should offer Play or Resume Book.
Tapping a specific chapter should:
- Resolve the parent book.
- Load the entire book queue.
- Start the selected chapter.
- Continue through the remaining book.

An audiobook file must never lose book-level resume handling merely because it was opened through Folders rather than Titles.

## 16. Search, Authors, and Series

Audiobook search must operate only on the audiobook catalog.
Searchable fields: Title, Author, Narrator, Series, Chapter title

Author view: each author once, followed by their books.

Series view:
- Show only books with real series information.
- Sort by numeric series position.
- Support decimal positions such as 1.5.
- Place books with missing series numbers after numbered books.
- Avoid creating artificial series from ordinary author folders.

## 17. Recommended Storage Architecture

Audiobooks should use a separate database from the stock music catalog.

Suggested tables:
```
books
tracks
chapters
progress
bookmarks
library_roots
scan_state
settings
```

A dedicated database prevents stock music scans from:
- Deleting audiobook-specific metadata.
- Recreating audiobook rows in Music.
- Mixing playback history.
- Corrupting resume records.
- Treating chapters as songs.

The stock music database should contain no normal audiobook media rows unless the stock playback engine absolutely requires temporary queue entries.

### 17.1 Incremental scanning

The audiobook scanner should:
- Read configured audiobook roots.
- Compare path, size, and modification time.
- Reparse only new or changed files.
- Build updates inside a transaction.
- Retain the previous valid catalog if scanning fails.
- Remove records only after confirming that files are genuinely gone.
- Avoid scanning generated _views or playlist directories.
- Avoid running continuously when the card has not changed.

## 18. Recommended R1 Playback Architecture

### 18.1 Correct long-term architecture

```
Native audiobook list
  |
  v
Book selection callback
  |
  v
Load Book object and progress
  |
  v
Create ordered private queue
  |
  v
Open saved track directly
  |
  v
Wait for decoder-ready event
  |
  v
Seek to saved position
  |
  v
Open Now Playing
```

The resume service should be responsible for:
- Progress persistence
- Playback-event monitoring
- Resume state
- Completion detection
- Smart rewind

It should **not** be responsible for:
- Reading framebuffer pixels
- Guessing which screen is visible
- Injecting touch events
- Automatically tapping track rows
- Navigating the interface
- Using timing delays to simulate user input

### 18.2 Why the current playlist approach is unsuitable

The current experimental architecture represents title views as M3U files. Selecting one opens the stock playlist track list, after which automated taps, framebuffer checks, timing delays, and a ptrace-based direct-open helper attempt to start the proper track.

This architecture may be useful for reverse-engineering and proof-of-concept work, but it should not be the final product architecture.

### 18.3 Preferred R1 integration point

The preferred solution is a custom audiobook list or a reused native Books list with a patched selection callback.

The selection callback should receive a book identifier and invoke a dedicated `play_book(book_id)` function. That function should construct the queue and call the stock media-open path directly with the desired track index.

The current project research already identifies a custom list generator and selection handler as the route capable of eliminating the track-list flash and fixing Back navigation, although it is also recognised as a more invasive binary modification.

The implementation should therefore focus reverse-engineering effort on one stable native selection hook rather than several UI automation mechanisms.

### 18.4 Temporary fallback

If a native book selection callback cannot yet be implemented, M3U files may remain as an internal queue-construction format, but they should never be presented to the user.

A fallback implementation must still:
- Open the M3U programmatically.
- Select the saved track programmatically.
- Never display or interact with the track-list UI.
- Avoid framebuffer detection.
- Avoid synthetic touch input.
- Confirm playback using engine state rather than screen state.

## 19. Performance and Battery Requirements

The audiobook service should remain asleep when:
- No audiobook is playing.
- No scan is pending.
- No resume action is in progress.

During playback it should react to playback events or poll only the minimum required state.

It must not continuously:
- Capture the framebuffer.
- Poll large database files.
- Traverse the SD card.
- Attach and detach from the player process.
- Generate playlist views.
- Rewrite progress multiple times per second.

## 20. Error Handling

The player should handle these conditions gracefully:
- Removed SD card
- Corrupt file
- Unsupported format
- Database corruption
- Scan during playback
- Clock changes

## 21. Acceptance Criteria

### Library
- Audiobooks never appear in Music after scan, reboot, card removal, or card reinsertion.
- Each book appears exactly once.
- Single-file and multi-file books look identical in the Titles view.
- Disc folders are merged correctly.
- Books with incomplete metadata remain usable.
- Duplicate titles by different authors remain separate.

### Playback
- Tapping a new book begins track 1 at the beginning.
- Tapping a partially listened book resumes the saved track and position.
- No track list appears during launch.
- No synthetic touch is required.
- Track transitions are automatic and correctly ordered.
- The queue stops after the final track.
- Shuffle and music repeat modes cannot disrupt book order.

### Resume
- Position survives pause.
- Position survives switching to music.
- Position survives switching to another audiobook.
- Position survives a normal reboot.
- Position survives system sleep shutdown.
- Position remains valid after a sudden power loss.
- Resume accuracy is within approximately two seconds.
- A failed seek is detected and retried.

### Completion
- Natural end of the final track marks the book finished.
- Finished books leave Continue Listening.
- Reopening a finished book starts from the beginning.
- Manually seeking near the end does not incorrectly erase progress.

### Reliability
- No persistent UI lag after browsing Folders.
- No persistent lag after switching between music and audiobooks.
- No abnormal shutdown battery drain.
- No background scanning loop.
- A corrupt book cannot prevent other books from loading.
- Removing one book cannot delete another book's progress.

## 22. Recommended Development Order

### Phase 1: Foundation
- Dedicated audiobook database
- Reliable /Audiobooks scanner
- Music-library exclusion
- One logical Book object per title
- Deterministic track ordering

### Phase 2: Core playback
- Native title list
- Direct book launch
- Private book queue
- Per-book track and position resume
- Correct track transitions
- Completion detection

### Phase 3: Audiobook player interface
- Book and chapter progress
- Skip forward and backward
- Chapter list
- Playback speed
- Audiobook sleep timer
- Finished and Continue Listening views

### Phase 4: Library enhancements
- Authors
- Series
- Search
- Sidecar metadata
- Improved cover caching
- Rename and move detection

### Phase 5: Advanced features
- Bookmarks
- Smart rewind
- Silence skipping
- Per-book equalisation or volume boost
- Additional physical-button options

**No effort should be spent polishing Authors, Series, generated views, or advanced controls until direct title playback and reliable resume work without UI automation.**

## 23. Definition of Done

The audiobook feature is complete when a user can copy either of these to the SD card:
```
/Audiobooks/Author/Book Title/001.mp3
/Audiobooks/Author/Book Title/002.mp3
```
or:
```
/Audiobooks/Author/Book Title.m4b
```

Then update the library, see one book entry, tap it once, listen, switch to music, power the R1 off, return several days later, tap the title again, and continue from the correct spoken sentence without ever needing to know which file or track contained it.

That is the standard the implementation should be built around.