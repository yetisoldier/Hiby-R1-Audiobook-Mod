# P8: Route-Table Research for Clean Back Navigation

## Problem
The current Audiobooks route uses `genre\Audiobook` which reuses the stock Genre routing infrastructure. This means pressing Back from the Audiobooks list passes through the stock Genres page before reaching the launcher. This is the most visible UX issue in the firmware mod.

## Research Method
- Static xref analysis of `/usr/bin/hiby_player` from the R1 (pulled via ADB)
- Capstone MIPS disassembly of 865,666 instructions
- Route string scan for UTF-16LE path patterns
- Route candidate listing with priority ranking
- Previous RAM-only probes documented in firmware_improvement_plan.md

## Findings

### Available Route Strings in hiby_player
| Address | Route String | Notes |
|---------|-------------|-------|
| 0x0035bb10 | `data\playlist\` | Playlist data route |
| 0x0035cdbc | `artist_all\` | All artists |
| 0x0035cdd4 | `artist\` | Single artist |
| 0x0035cdf4 | `genre_all\` | All genres |
| 0x0035ce0c | `genre\` | Single genre (current route) |
| 0x0035ce1c | `search\` | Search |
| 0x00360808 | `a:\Audiobooks\_views\Titles\*` | Mod-generated view |
| 0x00360888 | `a:\Audiobooks\_views\Authors\*` | Mod-generated view |
| 0x00360908 | `a:\Audiobooks\_views\Bookmarks\*` | Mod-generated view |
| 0x00360988 | `a:\Audiobooks\.\*` | Audiobook folder root |

### Route Candidates Tested (RAM-only, documented in firmware_improvement_plan.md)
| Route | Result | Status |
|-------|--------|--------|
| `genre\Audiobook` | Works, Back goes through Genres | Current release path |
| `genre\` | Opens global Genres | Not useful |
| `album\Audiobook` | Title-like list, same Genres Back stack | Rejected |
| `album\` | Global Albums or "No music found" | Rejected |
| `artist\` | Opens global Genres | Rejected |
| `artist\Audiobook` | Not tested | Low priority |
| `data\playlist\` | Playlists | Not useful for audiobooks |
| `genre-simple` | Duplicate rows | Rejected |
| `artist-simple` | Part-level lists under Music | Rejected |
| `artist-chain` | Wrong screen | Rejected |
| `m3u` | Playlists | Rejected |
| `format` | Format-based | Rejected |

### Static Xref Analysis
Key function call sites identified:
- `shared_media_open` at `0x0049e200` — 2 callsites found
- `shared_listview_handler` at `0x00490be0` — no direct callsites
- Listview handlers: `vg_listview_artist` (0x00490ec0), `vg_listview_album` (0x00490ee0), `vg_listview_genre` (0x00490f40), `vg_listview_book` (0x00490fa0)

### Native Hub Approach
The current firmware (1.6.16.5) uses a native hub with view rows that open generated filesystem views under `/Audiobooks/_views/`. This provides Title, Author, Series, and Folders entry points. The Back stack from these views still shares stock behavior.

The `1.6.16.6-nativehub-s2-dev` experiment tried passing `$s2` register into the title-route opener to get a cleaner Back stack. Result: **unsafe** — tapping Titles rebooted the R1.

## Conclusion

**No safe route-table modification has been found that eliminates the Genres Back stack.** All stock route alternatives either:
1. Open the wrong screen (global Genres, Albums, Artists)
2. Produce duplicate multipart rows
3. Show part-level lists under Music headings
4. Cause reboots (register experiments)

The current `genre\Audiobook` route with the native hub is the safest available option. The Back stack contamination is a **fundamental limitation of reusing stock routing infrastructure** — the only true fix would require a custom list view implementation in `hiby_player`, which needs deeper binary/UI reverse engineering.

## Recommendation

1. **Keep the current `genre\Audiobook` route** — it's the most stable option
2. **Document the Back navigation quirk** in user-facing README (already done)
3. **Future research target**: the `vg_listview_book` handler at `0x00490fa0` — this is a stock "book" listview that might provide a cleaner route if its callback chain can be mapped
4. **Do NOT attempt more register experiments** — the $s2 experiment caused a reboot

## Status
P8 is **research-complete**. No firmware changes recommended. The Back navigation limitation is documented as a known constraint. Future work would require custom UI code injection, which is beyond the current modification scope.