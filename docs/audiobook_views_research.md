# Audiobook View Research

The current public firmware opens Audiobooks directly to the title list by
reusing the stock `genre\Audiobook` route. That route is stable and keeps Music
albums/genres/search clean, but it does not provide a custom Audiobooks submenu.

## Feasibility

- Title: already implemented. The Audiobooks launcher opens the stock
  Genre -> Albums-of-Genre view for the synthetic `Audiobook` genre.
- Author: possible only with more research. Stock `hiby_player` has artist and
  album-artist list views, but the mod currently removes audiobook authors from
  the normal Music catalog tables so they do not leak into Music -> Artists.
  A plain stock artist route may therefore show normal music artists, no
  audiobook authors, or otherwise behave globally.
- Series: not currently available as a stock media view. We would need a
  reliable series source plus a custom view or query path. The Seanap/Plex
  folder convention now gives us a useful source for future experiments when a
  book is stored as `Audiobooks\Author\Series\Book Folder`; standalone books at
  `Audiobooks\Author\Book Folder` intentionally keep blank series fields.

## Seanap/Plex Layout Support

The DB helper now writes optional `series` and `series_part` columns to
`catalog.tsv`. These are derived from Seanap/Plex-style folders when present:

```text
Audiobooks\Author\Series\2020 - Book Title [Series 02]\01 - Chapter.mp3
```

This is catalog metadata only. It does not change playback behavior yet, and it
does not require every book to be in a series.

Development builds after `1.6.4-audiobook` also write a book-level sidecar:

```text
/usr/data/audiobooks/catalog-books.tsv
```

That file has one row per book with:

```text
root_hiby_path, album, author, book_key, series, series_part, track_count, first_media_id
```

This is a safer foundation for Author / Title / Series experiments than trying
to infer books from per-track rows at runtime. Standalone books keep blank
`series` and `series_part` fields.

Development builds on `codex/r1-hiby-modding-integration` add three more
book-level sidecars:

```text
/usr/data/audiobooks/catalog-view-title.tsv
/usr/data/audiobooks/catalog-view-author.tsv
/usr/data/audiobooks/catalog-view-series.tsv
```

These files are UI groundwork only; the installed player does not yet show a
custom Title / Author / Series submenu. They give a future UI patch pre-sorted
book rows with the same `character` and `pinyin_charater` normalization used by
the stock side rail. The series view intentionally omits standalone books so a
book without series metadata stays in the normal title/author paths instead of
being grouped under a fake series.

To inspect that sidecar during development:

```powershell
python tools\audiobook_catalog_report.py path\to\catalog-books.tsv
```

The report groups the same book-level catalog by author, series, and title and
keeps standalone books separate from series entries.

Live catalog report on 2026-06-11 from
`work\installed-release-verification\20260611-081553\catalog-books.tsv` showed
6 books, 2 authors, 2 series, 4 standalone books, and 3 multipart books. The
series view would be sparse but valid: `Snow Like Ashes` has `Ice Like Fire`
as part `2`, and `These Rebel Waves` has `These Rebel Waves` as part `1`.

## Safe Test Tool

`tools\adb_test_audiobook_launcher_route_variant.py` can temporarily change the
running Audiobooks launcher route in RAM. It does not edit firmware or rootfs;
rebooting the R1 restores the installed route.

`tools\r1_audiobook_ui_route_lab.py` wraps the route research into a repeatable
workflow. It can list route candidates, scan a `hiby_player` binary for
route-like UTF-16 strings, and generate a PowerShell live-test script for the
next connected-device session:

```powershell
python tools\r1_audiobook_ui_route_lab.py list
python tools\r1_audiobook_ui_route_lab.py scan-binary
python tools\r1_audiobook_ui_route_lab.py make-script `
  --output work\ui-route-lab\test-route-candidates.ps1
```

The generated script applies one RAM-only route candidate at a time, captures a
screenshot, taps the Audiobooks launcher tile, captures the result, and restores
the known-good `genre\Audiobook` route. It pauses between candidates so the
tester can record what opened and return to the main launcher.

Dry-run current state:

```powershell
python tools\adb_test_audiobook_launcher_route_variant.py
```

Restore the known-good title route in RAM:

```powershell
python tools\adb_test_audiobook_launcher_route_variant.py `
  --preset title `
  --apply --i-understand-this-writes-process-memory
```

Try an author-route experiment:

```powershell
python tools\adb_test_audiobook_launcher_route_variant.py `
  --preset artist `
  --apply --i-understand-this-writes-process-memory
```

If the R1 glitches, crashes, or shows the wrong list, reboot to return to the
flashed route.

The `artist\` route was tested in RAM on 2026-06-11. It patched and reverted
cleanly, but opening Audiobooks landed on the global stock Genres list, not an
audiobook-author list. That makes the plain stock artist route unsuitable as an
author-view shortcut.

The current `1.6.23-dbwatch-lock-dev` binary still exposes the same stock
media route strings (`artist_all\`, `artist\`, `album\`, `genre_all\`,
`genre\`, `search\`) plus the old text-reader routes (`a:\book\`,
`a:\book\*`). The route lab now defaults to the 1.6.23 player binary and lists
the book routes as explicit controls, but those are expected to open the txt
reader path rather than an audiobook media view.

`tools\adb_test_audiobook_direct_filter_route.py` tested a more direct filtered
album route in RAM on 2026-06-11. The patch applied and reverted cleanly, but
opening Audiobooks showed `No music found` instead of the title list. That route
is not a usable fix for the Back-stack quirk unless the underlying route/query
arguments are discovered more precisely.

Additional live route probes on 2026-06-17 tested the current development
player with RAM-only patches and screenshot capture:

```text
genre-selected-audiobook   global Genres list, not audiobook titles
album-audiobook            title-like list, but same Genres Back stack
album-selected-audiobook   global/empty music route with No music found
direct-filter              No music found and Music grid Back stack
artist-audiobook           no usable author view
artist-selected-audiobook  global Genres list with No music found
```

These tests make the simple route-string path look exhausted for now. The
current `genre\Audiobook` title route remains the safest release behavior. A
true Title / Author / Series submenu likely needs deeper `hiby_player`
list-view/query patching or a custom catalog-backed entry point instead of
only swapping the launcher route and selected argument.

Static xref tooling added on 2026-06-17 gives a better next target than route
strings alone:

```powershell
python tools\r1_hiby_player_static_xrefs.py `
  --output work\static-xrefs\hiby_player-1.6.23-xrefs.md
```

The current report decodes the 1.6.23 `hiby_player` and finds the stock media
route table around `0x00787040`:

```text
0x00787040 album -> vg_listview_album -> vg_listview_songs_of_an_album
0x00787058 artist -> vg_listview_artist -> vg_listview_songs_of_artist_all
0x00787070 artist -> vg_listview_artist -> vg_listview_albums_of_an_artist -> vg_listview_songs_of_an_album_and_an_artist
0x00787088 genre -> vg_listview_genre -> vg_listview_songs_of_a_genre
0x007870a0 genre -> vg_listview_genre -> vg_listview_albums_of_a_genre -> vg_listview_songs_of_an_album_and_a_genre
```

It also identifies the core listview handlers:

```text
vg_listview_album                 -> 0x00490ee0
vg_listview_artist                -> 0x00490ec0
vg_listview_genre                 -> 0x00490f40
vg_listview_albums_of_an_artist   -> 0x00490f00
vg_listview_songs_of_an_album     -> 0x00490f20
vg_listview_book                  -> 0x00490fa0
```

Most category listview handlers are tiny pointer-table trampolines into the
shared listview path at `0x00490be0`; the route-table callbacks at `0x004f01c0`
and `0x004effc0` build the simple and chained child-list flows. The report does
not show direct `jal` references for those handlers because they are invoked
through data tables, which is exactly why route-table probes are the better
next step.

That points the next UI research toward RAM-only probes on the stock listview
handlers or a custom route-table entry, rather than more launcher string swaps.

`tools\adb_test_audiobook_route_table_direct.py` tests one such route-table
entry in RAM. It temporarily changes the stock `genre` route record so
`genre\Audiobook` opens `vg_listview_albums_of_a_genre` directly with
`vg_listview_songs_of_an_album_and_a_genre` as its child list. Live testing on
2026-06-17 showed this opens an `Audiobook` list instead of the broader Genres
parent, but it is not production-ready: the list showed duplicate multipart
book rows, and Back still landed on the global Genres screen. The helper is
useful for research, but the release firmware should keep the current route
until a cleaner table record or custom list source is found.

`tools\adb_probe_route_callback.py` then traced the two route callbacks in RAM.
The chained callback at `0x004effc0` did not fire when opening Audiobooks from
the current launcher. The simple callback at `0x004f01c0` did fire, with
`$a1=0x007870a0` pointing at the stock genre-chain route record,
`$a2=genre\Audiobook`, and `$s1=Audiobook`. Changing the launcher to call the
chained callback instead opened the global Genres page, so the callback target
alone is not a Back-stack fix.

`tools\adb_test_audiobook_launcher_record.py` tested the stock route-record
pointer passed by the launcher without changing the rest of the code cave.
Live RAM tests on 2026-06-17 showed:

```text
genre-simple    duplicate multipart book rows; Back returned to global Genres
album           duplicate multipart book rows under Albums; Back to global Albums
artist-simple   duplicate title rows under Audiobook; Back to global Artists
artist-chain    part-level book rows under Artists; Back to global Artists
m3u             opened the stock Playlists page
format          part-level rows under Format
```

All route-record tests were reverted in RAM. The result is that stock route
records can expose useful clues, but none is a production replacement for the
current title view. A cleaner Back path or native Title / Author / Series menu
likely needs either a custom route record with the correct view stack semantics
or a deeper listview/query patch.

Static disassembly of the route callbacks was saved to:

```text
work\static-xrefs\hiby_player-1.6.23-route-callback-disasm.txt
```

The simple callback at `0x004f01c0` reads only the route record's `view`
(`+0x08`) and `child` (`+0x0c`) fields. The chained callback at `0x004effc0`
reads `view`, `child`, and `next` (`+0x10`). That explains the current launcher
behavior: it calls the simple callback with the stock genre-chain record, so it
uses `vg_listview_genre -> vg_listview_albums_of_a_genre` and ignores the
record's chained callback field. The double-Back quirk is therefore likely
caused by the view stack built for that parent/child pair, not by the callback
pointer alone.

`tools\adb_test_audiobook_route_table_matrix.py` is the next RAM-only helper.
It patches individual fields of the stock `0x007870a0` record so future device
sessions can isolate whether `view`, `child`, `next`, or callback choices cause
duplicate rows or bad Back behavior. Start with conservative variants such as
`callback-simple-only`, `next-empty-only`, and `songs-of-genre-simple`; revert
after each test.

Live testing on 2026-06-17 also showed that `direct-view-only` is not a safe
default matrix candidate: after the route patch opened Audiobooks, ADB dropped
and the player had to come back through a reboot/manual ADB enable. The wrapper
therefore keeps direct-view variants out of the default run; they should only be
used intentionally when testing a risky UI route branch.

For a clean connected-device session, the wrapper below applies each variant,
opens Audiobooks, captures screenshots, presses Back once, and reverts before
moving to the next variant:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\adb_run_audiobook_route_matrix_tests.ps1
```

ADB screenshots on 2026-06-16 with the smaller test SD card confirmed that the
current database/catalog has all 6 book rows, but opening Audiobooks through the
stock `genre\Audiobook` route can land with the first title row hidden under the
`Audiobook` subheader. On that card, `Calypso` was present in the DB and became
visible after a small downward drag, while the initial list showed only
`Holidays on Ice`, `Ice Like Fire`, `Squirrel Seeks Chipmunk`,
`These Rebel Waves`, and `When You Are Engulfed in Flames`. Treat this as a UI
positioning/clipping issue, not a DB rebuild failure.

The same ADB check also showed a messy back stack: Back from the audiobook title
list returned to the stock `Genres` page, and a later Back landed on the Music
grid rather than the main launcher. That matches the user-visible double-back
quirk and suggests that a true fix probably needs a cleaner route or custom
entry point, not only a label/icon change.

Development build `1.6.34-title-context-rc1` adds a pragmatic runtime guard for
that quirk. Instead of finding a new route, the resume daemon self-arms from a
small framebuffer signature for the `Audiobook` subheader. If the next screen
looks like the stock global category page without that subheader, it sends two
guarded stock Back gestures. Live ADB testing showed one user Back from the
Audiobooks title list returning to the main launcher with
`back-guard extra-back after audiobook list count=2` in the daemon log. A later
timing pass tightened idle UI sampling to one second and moved the Back guard
ahead of deeper title-marker polling, which made the same guard work in the
stricter quick open/back path. This is a workaround, not a true custom route: it
should reduce the visible double-Back behavior while the deeper Title / Author /
Series UI remains future work.

## Likely Path Forward

The safest route is to keep the current title view for release builds and test
route variants in RAM. The first target is a cleaner direct title route that
does not leave Genres on the Back stack. The second target is an author route
that can see audiobook authors without exposing audiobooks in Music.

If a stock route variant works, we can promote it to the firmware patch with
small code-cave changes. If stock routes cannot do this, a true Author / Title /
Series submenu will require deeper `hiby_player` UI/query patching. The
extended catalog gives that future work author/title/series data to build on,
but the visible submenu itself remains higher risk.
