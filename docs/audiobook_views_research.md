# Audiobook View Research

> **⚠️ SUPERSEDED — historical record (pre-2.0, v1.6.x era).** This research
> explored stock-route / generated-`_views` browsing, which the NativeApp
> pivot (v2.0.17) replaced with an in-process UI. See
> [`audiobook_firmware_architecture.md`](./audiobook_firmware_architecture.md)
> and [`modding/`](./modding/). Retained as historical context.

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

## Native Listview Descriptor Pass - 2026-06-17

The next deeper integration target is the native listview descriptor table, not
only the media route records. `tools\r1_hiby_player_listview_descriptor_report.py`
now scans `hiby_player` for listview descriptors and records their generator and
selection callbacks. The focused stock report is saved at:

```text
work\static-xrefs\hiby_player-stock-book-listview-descriptors.md
```

The key stock Books descriptors are:

```text
vg_listview_main_book   generator 0x00540b60   select 0x00540ca0
vg_listview_book_list   generator 0x005408a0   select 0x00540a80
vg_listview_book_recent generator 0x00540a40   select 0x00540f00
vg_listview_book_collect generator 0x00540a60  select 0x00540ee0
vg_listview_book        generator 0x0053fae0   select 0x0053fca0
```

This confirms the R1 still contains a real native Books hub and native Books
sub-list screens. The current public firmware bypasses that hub to open
`genre\Audiobook` directly, which is why it needs runtime help for title taps
and Back-stack cleanup. A cleaner long-term path is to restore the native hub
and replace selected Books list sources with audiobook-aware sources.

A RAM-only proof already showed that restoring `vg_listview_main_book` gives a
clean native Audiobooks hub. Patching the hub row-1 jump-table entry from
`0x00540d08` to a small cave at `0x0075de60` made the visible Audiobooks row open
the current title list successfully. That exact title-row patch is now available
as an opt-in local firmware patch:

```powershell
python tools\patch_hiby_player.py work\rootfs\usr\bin\hiby_player `
  -o work\patched\hiby_player.native-hub-title-row `
  --audiobook-native-hub-title-row `
  --audiobook-title-autostart-marker
```

Local guarded patching passed. The resulting player hash is:

```text
MD5    2f3402ac9c164c2d2d3e0b24da8af1f5
SHA256 7ba414fb3d6d2c22a05fefcc057a96092bd514565670f86325a5eb63a70ff678
```

The production direct-entry patch still reproduces the known
`1.6.16.2-audiobook` player hash, so this native hub work is isolated and
opt-in.

The next native-hub candidate also reuses the stock Books -> Files opening path
instead of a music route. The stock firmware already opens the old text-book
folder through `vg_listview_explorer` with an `a:\book\*` argument. The
development patch redirects two hub rows to the same native explorer screen but
passes `a:\Audiobooks\*` from a code cave instead. This is still folder-based
browsing, not true metadata Authors or Series, but it is a deeper integration
than the current release because it stays inside the stock hub/listview system.

Current expected behavior for
`work\audiobook-firmware-1.6.16-nativehub-folders-dev\r1-audiobooks-1.6.16-nativehub-folders-dev.upt`:

```text
Main menu -> Audiobooks        opens the native Audiobooks hub
Audiobooks -> Titles           opens the current audiobook title/resume path
Audiobooks -> Authors          opens the native explorer rooted at /Audiobooks
Audiobooks -> Folders          opens the native explorer rooted at /Audiobooks
Audiobooks -> Recently played  still uses the stock Books recent row
```

The row labels are patched through `book.ini`; the row behavior is patched in
`hiby_player`. The native hub labels are kept short for the R1 screen:
`Scan`, `Titles`, `Authors`, `Folders`, and `Recent`. The folder rows use this
guarded byte patch:

```text
row 1 jump 0x0078d27c -> 0x0075de60  (Titles -> audiobook title path)
row 2 jump 0x0078d280 -> 0x0075dea0  (Authors -> /Audiobooks explorer)
row 3 jump 0x0078d284 -> 0x0075dea0  (Folders -> /Audiobooks explorer)
path cave 0x0075df00 -> UTF-16 "a:\Audiobooks\*"
```

The verified local package hashes are:

```text
r1-audiobooks-1.6.16-nativehub-folders-dev.upt
  MD5    1df1a5b731b55b0f4d23efaf781c1415
  SHA256 2b644bcbd4608773b665f54f593bbc1470cb8eddb0ace967bfe2a40ed4546b49

hiby_player
  MD5    e694128fbf286bd360529a58f3848a36
  SHA256 0969904b12edda54ee7e6f82b3f97f187f61a776d18cfe1f1a1fbdd147f5238a
```

Cave-aware static reports for this candidate are saved at:

```text
work\static-xrefs\hiby_player-nativehub-folders-book-listview-descriptors.md
work\static-xrefs\hiby_player-nativehub-folders-title-cave-calls.md
work\static-xrefs\hiby_player-nativehub-folders-ui-create-calls-with-caves.md
work\static-xrefs\hiby_player-nativehub-folders-xrefs-with-caves.md
```

Those reports confirm that the stock Books listview descriptors are unchanged,
the title cave calls the existing audiobook root opener at `0x0075dbc0`, and
the folder cave calls the native listview creator with
`vg_listview_explorer` plus `a:\Audiobooks\*`.

This candidate is useful for device testing, but it should not be treated as a
release candidate yet. `Authors` and `Folders` currently duplicate the same
folder browser. True metadata views still require a custom list source backed
by the audiobook sidecars. `Recently played` also needs either a redirect or a
real audiobook-aware recent list before the hub feels fully polished.

Live testing of the follow-up native hub work confirmed that the folder rows
can open the native explorer rooted at `/Audiobooks`. It also exposed a native
state restore issue: after visiting the explorer rows and backing out to the
launcher, tapping the Audiobooks launcher could restore the previous Files
subpage instead of opening the hub root. The next guarded candidate therefore
adds a native hub launcher callback at `0x0075daec`. That callback follows the
stock launcher shape but always creates `vg_listview_main_book` with the stock
`a:\book` key, so the Audiobooks tile should enter the hub root each time.

The same candidate also stops the hub title row from reusing the shared `ebook`
resource string. The launcher and page title can keep saying `Audiobooks`,
while the row label points to a private code-cave string, `Titles`.

The first native launcher build opened the hub root correctly, but its title
label patch used literal `Titles` as a resource key. The stock hub generator
stopped after `Scan`. The next build added a private `titles` resource key and
`<titles>Titles</titles>` in `book.ini`; that fixed the visible row list:
`Scan`, `Titles`, `Authors`, `Folders`, and `Recent`.

Live testing of that `titleskey` build showed the row-label fix is good, but
the Titles action still opened the global Music `Genres` root. Adding a
temporary `Audiobook` row back into `GENRE_TABLE` and `GENRE2_TABLE` did not
fix it, so this is not just a hidden-genre catalog issue. A `rowctx` candidate
changed the title-row cave at `0x0075de60` from:

```text
lw a0, 0xd8(s0)
```

to:

```text
move a0, s0
```

The intent was to pass the same higher-level list object used by the older
launcher route instead of the stock child-parent pointer used by `0x004961e0`.
Live testing on 2026-06-18 showed that tapping `Titles` on this build rebooted
the R1, so this candidate is unsafe and is not kept as the default native-hub
patch.

Current recovery/dev package after the failed row-context test:

```text
work\audiobook-firmware-1.6.16.2-recovery-dev\r1-audiobooks-1.6.16.2-recovery-dev.upt

hiby_player MD5    dac7b58717097ef2a75ae5887478ef16
hiby_player SHA256 0f7622d5674a1ecdb00c228251e29be543c3a6eb1d4cfdd86f74c8eb8e30d782
package MD5        99601a6d7d3563344b27061896a05d7d
package SHA256     349235ff69f898f200c31d6463dbaf55246d9e33a4c24fab912a362c78af50a1
```

Local verification passes with:

```powershell
python tools\verify_r1_audiobook_build.py `
  --out-dir work\audiobook-firmware-1.6.16.2-recovery-dev `
  --upt-name r1-audiobooks-1.6.16.2-recovery-dev.upt `
  --expected-version 1.6.16.2-recovery-dev `
  --expected-label "HiBy R1 Audiobook FW 1.6.16.2 recovery dev" `
  --require-db-maintenance `
  --expect-audiobook-launcher-icon `
  --expect-native-dsd `
  --expect-sbc-xq `
  --expect-usb-dac-mode
```

Device validation still needs to confirm:

```text
Main menu -> Audiobooks returns to the known-good direct title list.
Title taps still resume correctly.
The DB watcher logs zero_audio_retry=600s.
Normal Music playback still feels responsive.
```

Deeper static mapping of the stock Books hub gives the future custom-list hook
points:

```text
vg_listview_main_book generator 0x00540b60 creates the 5-row hub.
vg_listview_main_book select    0x00540ca0 reads the selected row and jumps
                                through the table at 0x0078d278.

row 1 stock target 0x00540d08 -> open vg_listview_book_list with key "book"
row 2 stock target 0x00540e2c -> open vg_listview_book_collect with key "book_collect"
row 3 stock target 0x00540d48 -> open vg_listview_explorer with path a:\book\*
row 4 stock target 0x00540db4 -> open vg_listview_book_recent with key "book_recent"
```

`vg_listview_book_collect` and `vg_listview_book_recent` both reuse the same
tap handler as `vg_listview_book_list`:

```text
book_collect_select 0x00540ee0 -> jump to 0x00540a80
book_recent_select  0x00540f00 -> jump to 0x00540a80
```

That means a real custom Title / Author / Series implementation probably needs
only one patched list select path once the generator can present the right
rows. The current stock select handler at `0x00540a80` eventually calls
`0x005401c0(parent_view, selected_path)` to open the old TXT-reader book path.
For audiobooks, that final open step is the place to replace TXT reader launch
with the existing audiobook title/resume opener or with a direct media-id/book
root opener.

The generator side is harder but now scoped. `book_list_generator` at
`0x005408a0` allocates standard row objects and fills them through the stock
list item virtual methods. It reads its source from HiBy's existing Books data
structures. The R3 Pro II DB Manager patch gives a practical pattern for
customizing this kind of list without adding a brand-new UI framework:

1. Set a small global mode flag before opening the reused listview.
2. Redirect the stock generator to a wrapper.
3. In stock mode, jump back to the original generator body.
4. In audiobook mode, load labels/actions from our sidecar files.
5. Redirect the shared select handler so the selected row opens audio instead
   of the TXT reader.

That is the best current path toward true `Titles`, `Authors`, and `Series`
views. It is more invasive than the folder-row candidate, but it should remove
more runtime UI tricks once implemented.

Author and Series should not be implemented by populating the stock Music
artist/album-artist tables unless we accept audiobook leakage into Music views.
The better path is a sidecar-backed native Books sub-list:

1. Keep `vg_listview_main_book` as the Audiobooks hub.
2. Relabel the hub rows as Title, Author, Series, Files, and Recently Played.
3. Patch hub row entries to open native list screens in a mode flag, following
   the mode-flag pattern used by the R3 Pro II DB Manager patch.
4. Replace or wrap the Books list generator so it reads
   `/usr/data/audiobooks/catalog-view-title.tsv`,
   `/usr/data/audiobooks/catalog-view-author.tsv`, or
   `/usr/data/audiobooks/catalog-view-series.tsv`.
5. Replace or wrap the Books list selection handler so row taps call the
   existing audiobook title-open/resume path instead of the old TXT reader.

This would remove a large part of the current UI workaround layer. It does not
yet remove the resume daemon entirely, because saving per-book progress,
completed-book handling, and failed seek recovery still need runtime state.
However, a native list source would make Title / Author / Series browsing and
Back behavior much more like stock firmware.

One caution from the live research session: device-side ADB can become wedged
after failed process-memory probes even while `adb devices` still lists the R1.
Keep large temporary memory reads under `/tmp`, not `/usr/data`, and reboot /
re-enable ADB before continuing live tests if `adb shell` reports
`error: closed`.

## 1.6.16.5 Static Refresh - 2026-06-18

After flashing `1.6.16.5-tracklist-return-dev`, the installed-device verifier
and live smoke test passed. The smoke tool now uses a longer synthetic tap and
retries the Audiobooks launcher open once if the first injected tap is ignored.

Fresh static reports were generated from the exact `1.6.16.5` `hiby_player`
binary:

```text
work\static-xrefs\hiby_player-1.6.16.5-tracklist-return-xrefs.md
work\static-xrefs\hiby_player-1.6.16.5-tracklist-return-listview-descriptors.md
work\static-xrefs\hiby_player-1.6.16.5-tracklist-return-ui-calls.md
```

The reports confirm the current player still has the stock Books listview
descriptors and that the known Books hub row actions are descriptor/table
driven rather than directly called. `vg_listview_book_list`,
`vg_listview_book_collect`, and `vg_listview_book_recent` still share the same
selection path, so a real Title / Author / Series implementation should focus
on wrapping the native Books list generator/select path instead of trying more
simple launcher-route string swaps.

The native hub probe now checks whether `/proc/<pid>/mem` is still readable
before each process-memory read and retries with a fresh `hiby_player` PID if
the player restarts mid-probe. This does not make native-hub RAM probes risk
free, but it removes one stale-PID failure mode observed during read-only
inspection.

Follow-up live testing showed that even chunked heap scans can make ADB return
`error: closed` on the R1. The device recovered without rebooting, but this is
too fragile for routine live development. Future native-hub work should prefer
flash-time static patches and avoid live heap-object patching unless the test
explicitly needs it.

Static cave analysis showed why the native hub title row is sensitive to the
parent pointer. The direct Audiobooks launcher cave obtains a context pointer
from the launcher object, validates it, then calls the route callback. The old
native title-row cave passed `lw a0, 0xd8(s0)` directly into the same route
callback; the unsafe row-context variant passed `s0` and rebooted during live
testing. The next candidate instead passes `s2`, the original hub event/context
pointer preserved by `vg_listview_main_book` select:

```text
work\audiobook-firmware-1.6.16.6-nativehub-s2-dev\r1-audiobooks-1.6.16.6-nativehub-s2-dev.upt

hiby_player MD5    774ca68d3aa59b710adcf6394277a747
hiby_player SHA256 46609913be8038566d3f317daaea5b0010cc796fa953c1781c1601f9a4c88aa9
package MD5        3ad80f51167cc2655c59ccf6f18b3ffa
package SHA256     c9d1afde3c2ef520a2bcb06e7341d43408d2f75845cd8ff7b0eb8b9689e2f73d
```

Offline verification passed and the package was staged to the SD card as
`r1.upt` on 2026-06-18. Live validation showed this candidate is not safe
enough to keep on the device:

```text
Main menu -> Audiobooks opens the native Audiobooks hub.
Back from the hub returns to the main launcher in one tap.
Titles initially returned to the launcher because the old back-stack guard
  misidentified the native hub title path.
With the resume daemon stopped, Titles rebooted/dropped ADB.
Authors opened the stock explorer, but it restored the previous folder rather
  than forcing /Audiobooks every time.
```

The follow-up `1.6.16.7-stable-route-dev` package returns to the known-good
direct audiobook title route while native hub work continues offline:

```text
work\audiobook-firmware-1.6.16.7-stable-route-dev\r1-audiobooks-1.6.16.7-stable-route-dev.upt

hiby_player MD5    09997a636c94112ff76c85a6d4a8d0ff
hiby_player SHA256 f49ea55a48c1bdf1398a2a6672b1d596516650f7ebe77846ba7c33a5cfee329c
package MD5        438aefc7252894030f9923bb62895128
package SHA256     3db34eee7f9a96c19e76b1e29d18f8f79d5ca54e962f5848163a1886e31260e3
```

For native Title / Author / Series, the next implementation should wrap the
stock Books list generator/select path at `0x005408a0` / `0x00540a80` and feed
it the sidecar catalog files. More title-row calls into the media route helper
are likely to repeat the current failure pattern: `lw a0,0xd8(s0)` is stable
but lands on the wrong global music route, while row/list objects such as `$s0`
and `$s2` can reboot the player.

## 1.6.16.6 Native View Rows / Sub-Back Candidate - 2026-06-22

The current deeper UI candidate keeps the native Audiobooks hub but stops trying
to make the stock title row call the music genre route. Instead, the hub rows
open generated filesystem views:

- `Titles` -> `a:\Audiobooks\_views\Titles\*`
- `Authors` -> `a:\Audiobooks\_views\Authors\*`
- `Series` -> `a:\Audiobooks\_views\Series\*`
- `Folders` -> `a:\Audiobooks\.\*`

The generated `*.m3u` rows are written by the DB maintainer, so playback still
uses the stock media player path and the existing resume daemon can match the
selected book. To keep the UI fast and avoid nested generated folders, the
Author and Series views were flattened to single playlist rows such as
`Author - Title.m3u` and `Series - 02 - Title.m3u`.

Live RAM-only validation showed that simply opening the generated folders
worked, but the page title remained `Files` and Back behavior could be sticky.
The follow-up helper at `0x00760d50` now:

1. clears any stale `vg_listview_explorer`;
2. opens the requested generated view path with the stock explorer opener;
3. registers a stock `hiby_set_sub_back` entry using the requested view label;
4. installs the stock explorer Back callback on the created list object.

Runtime testing after the RAM patch confirmed:

- Titles, Authors, and Series show friendly headers instead of `Files`;
- Folders opens the correct `/Audiobooks` root with a friendly `Folders`
  header by routing through `a:\Audiobooks\.\*`;
- Series only showed series-tagged books on the test card;
- selecting a generated title playlist still played through Now Playing;
- a saved multipart bookmark for `Ice Like Fire` restored around `26:05`;
- Back from a generated view now goes to the Audiobooks folder root, then back
  to the Audiobooks hub. One-back-to-hub is still not solved, but this is
  predictable and did not freeze during the test.

The public-labeled follow-up package is:

```text
work\audiobook-firmware-1.6.17-audiobook\r1-audiobooks-1.6.17-audiobook.upt

package MD5        e8491f65ead4ef7a34163a67c7ee7007
package SHA256     47b6b2aa85f0f14d13d659f0f3f987808f7d389a7a32bf7e54676388e6f82523
rootfs MD5         d8c6a46cb4dc90624042f89224f611e6
hiby_player MD5    cf6014c0a4e6188ce0823348af49aba1
hiby_player SHA256 2c4fbcf817bc66b6e545d24a7a90d464bd720b5c069980c4e428e5e9f8a31d59
```

Offline verification passed with `--expect-native-hub-view-rows` and
`--expect-native-hub-launcher`.

Installed verification of the public-labeled `1.6.17-audiobook` package passed
on 2026-06-22 with artifacts under
`work\installed-release-verification\20260622-141615`. The device reported
`1.6.17-audiobook`, DB integrity was `ok`, the test card had 135 audiobook rows
across six books, title/author/series catalogs were present, one internal
`Audiobook` route row was present, Music album/search leakage was zero, and one
resume daemon plus one DB watcher were running. UI smoke opened the launcher
Audiobooks hub, displayed `Scan`, `Titles`, `Authors`, `Series`, and `Folders`,
and launched a generated title row into Now Playing with resume.

Installed flash validation on 2026-06-22 confirmed the package reports
`1.6.16.6-view-subback-dev`, passes installed-release verification, and has one
resume daemon plus one DB watcher. Live UI checks showed:

- Audiobooks opens to the native hub with `Scan`, `Titles`, `Authors`,
  `Series`, and `Folders`.
- `Titles`, `Authors`, and `Series` open the generated view folders with the
  intended page headers.
- `Series` lists only the series-tagged books on the test SD card.
- `Folders` originally opened `TF:\Audiobooks\` with the stock `Files` page
  header and close icon. A RAM-only path probe changed the route to
  `a:\Audiobooks\.\*`, which kept the same folder contents but displayed the
  intended `Folders` header and normal audiobook back arrow.
- Edge-back from generated views returns to the Audiobooks hub, with a brief
  transition ghost that clears after a second or two.
- Starting `Ice Like Fire` from the generated `Titles` playlist switched to Now
  Playing, restored to `26:05`, and the progress counter advanced.

Post-flash validation of `1.6.16.6-folder-polish-dev` on 2026-06-22 passed the
installed-release verifier with 135 audiobook rows, six books, one
route-visible internal `Audiobook` genre row, and no Music album/genre/search
leakage. The launcher shows the Audiobooks icon; opening Audiobooks from the
tile label shows the native hub; `Folders` displays the intended `Folders`
header with `TF:\Audiobooks\.\`; and `Titles` opens the generated title playlist
view. From the Folders root, the left arrow can be sticky, but edge-back returns
to the Audiobooks hub.
