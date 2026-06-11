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

## Safe Test Tool

`tools\adb_test_audiobook_launcher_route_variant.py` can temporarily change the
running Audiobooks launcher route in RAM. It does not edit firmware or rootfs;
rebooting the R1 restores the installed route.

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

## Likely Path Forward

The safest route is to keep the current title view for release builds and test
route variants in RAM. If an author route proves usable, the next problem is
database isolation: audiobook authors must be visible to Audiobooks without
leaking into Music. If stock routes cannot do that, a true Author / Title /
Series submenu will require deeper `hiby_player` UI/query patching. The
extended catalog gives that future work author/title/series data to build on.
