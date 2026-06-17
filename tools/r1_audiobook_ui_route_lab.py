#!/usr/bin/env python3
"""Plan and prepare safe Audiobooks UI route experiments.

This tool does not patch firmware. The only command that writes to the R1 is
the generated ADB script, which uses the existing RAM-only launcher route patcher
and is reboot-reversible.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from adb_test_audiobook_launcher_route_variant import ROUTE_PRESETS
from patch_hiby_player import AUDIOBOOK_LAUNCHER_ROUTE, AUDIOBOOK_LAUNCHER_SELECTED_GENRE


DEFAULT_BINARY = Path(
    "work/audiobook-firmware-1.6.28-sd-ready-dev/"
    "squashfs-root/usr/bin/hiby_player"
)
ROUTE_FIELD_BYTES = len(AUDIOBOOK_LAUNCHER_ROUTE)
SELECTED_FIELD_BYTES = len(AUDIOBOOK_LAUNCHER_SELECTED_GENRE)


@dataclass(frozen=True)
class RouteCandidate:
    name: str
    route: str
    selected: str
    priority: int
    note: str
    expected: str


CANDIDATES = [
    RouteCandidate(
        "title",
        "genre\\Audiobook",
        "Audiobook",
        0,
        "Known-good release path.",
        "Audiobook title list; Back first returns to Genres.",
    ),
    RouteCandidate(
        "genre-root",
        "genre\\",
        "",
        2,
        "Bare stock genre route.",
        "Likely global Genres list; useful as a control.",
    ),
    RouteCandidate(
        "album-root",
        "album\\",
        "",
        2,
        "Bare stock album route.",
        "Likely global Albums or No music found depending on DB isolation.",
    ),
    RouteCandidate(
        "artist-root",
        "artist\\",
        "",
        2,
        "Bare stock artist route.",
        "Previously opened the global stock Genres list on device.",
    ),
    RouteCandidate(
        "artist-all-root",
        "artist_all\\",
        "",
        3,
        "Alternate stock artist route.",
        "Unknown; may be global and not audiobook-filtered.",
    ),
    RouteCandidate(
        "genre-all-root",
        "genre_all\\",
        "",
        3,
        "Alternate stock genre route.",
        "Unknown; may be global and not audiobook-filtered.",
    ),
    RouteCandidate(
        "search-root",
        "search\\",
        "",
        3,
        "Stock search route.",
        "Unknown; higher chance of an unusable page for Audiobooks.",
    ),
    RouteCandidate(
        "book-root",
        "book\\",
        "",
        3,
        "Stock book/text-reader route control.",
        "Expected to reopen the old txt reader path, not audio playback.",
    ),
    RouteCandidate(
        "book-drive-root",
        "a:\\book\\",
        "",
        3,
        "Absolute stock book/text-reader storage route control.",
        "Expected to target txt books on storage, not audiobook media rows.",
    ),
    RouteCandidate(
        "artist-selected-audiobook",
        "artist\\",
        "Audiobook",
        3,
        "Tests whether the selected argument filters a root artist route.",
        "Tested on device: opened global Genres with No music found, not authors.",
    ),
    RouteCandidate(
        "album-selected-audiobook",
        "album\\",
        "Audiobook",
        3,
        "Tests whether the selected argument filters a root album route.",
        "Tested on device: opened a global/empty Music route with No music found.",
    ),
    RouteCandidate(
        "genre-selected-audiobook",
        "genre\\",
        "Audiobook",
        3,
        "Separates route prefix from selected genre argument.",
        "Tested on device: opened the global Genres list, not the title list.",
    ),
    RouteCandidate(
        "artist-audiobook",
        "artist\\Audiobook",
        "Audiobook",
        3,
        "Tests an inline filter on the stock artist route.",
        "Tested on device: no usable audiobook author view.",
    ),
    RouteCandidate(
        "album-audiobook",
        "album\\Audiobook",
        "Audiobook",
        2,
        "Tests an inline filter on the stock album route.",
        "Tested on device: title-like list, but same Genres Back stack.",
    ),
]


def encode_utf16_field(text: str, size: int) -> bytes:
    raw = text.encode("utf-16le") + b"\x00\x00"
    if len(raw) > size:
        raise ValueError(f"{text!r} needs {len(raw)} bytes but field holds {size}")
    return raw.ljust(size, b"\x00")


def fits(candidate: RouteCandidate) -> bool:
    try:
        encode_utf16_field(candidate.route, ROUTE_FIELD_BYTES)
        encode_utf16_field(candidate.selected, SELECTED_FIELD_BYTES)
        return True
    except ValueError:
        return False


def route_strings_from_binary(path: Path) -> list[tuple[int, str]]:
    data = path.read_bytes()
    out: list[tuple[int, str]] = []
    i = 0
    while i < len(data) - 8:
        j = i
        chars: list[str] = []
        while j + 1 < len(data):
            lo = data[j]
            hi = data[j + 1]
            if hi == 0 and 32 <= lo < 127:
                chars.append(chr(lo))
                j += 2
            else:
                break
        if len(chars) >= 4:
            text = "".join(chars)
            lowered = text.lower()
            if "\\" in text and any(
                word in lowered
                for word in (
                    "album",
                    "artist",
                    "genre",
                    "search",
                    "book",
                    "playlist",
                    "music",
                )
            ):
                out.append((i, text))
            i = max(j, i + 2)
        else:
            i += 2
    return out


def print_candidates(*, include_risky: bool) -> None:
    rows = [candidate for candidate in CANDIDATES if include_risky or candidate.priority <= 2]
    print(f"route field: {ROUTE_FIELD_BYTES} bytes; selected field: {SELECTED_FIELD_BYTES} bytes")
    print("")
    for candidate in rows:
        route_bytes = len(candidate.route.encode("utf-16le")) + 2
        selected_bytes = len(candidate.selected.encode("utf-16le")) + 2
        preset = "yes" if candidate.name in ROUTE_PRESETS else "no"
        fit = "yes" if fits(candidate) else "no"
        print(candidate.name)
        print(f"  route:    {candidate.route!r} ({route_bytes}/{ROUTE_FIELD_BYTES} bytes)")
        print(f"  selected: {candidate.selected!r} ({selected_bytes}/{SELECTED_FIELD_BYTES} bytes)")
        print(f"  preset:   {preset}; fits: {fit}; priority: {candidate.priority}")
        print(f"  note:     {candidate.note}")
        print(f"  expect:   {candidate.expected}")
        print("")


def powershell_quote(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def write_live_script(path: Path, *, include_risky: bool, pause_between: bool) -> None:
    candidates = [
        candidate
        for candidate in CANDIDATES
        if candidate.name != "title" and (include_risky or candidate.priority <= 2) and fits(candidate)
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "$ErrorActionPreference = 'Stop'",
        "python tools\\r1_adb_control.py devices",
        "Write-Host ''",
        "Write-Host 'Start from the main launcher before each candidate. Reboot restores the flashed route.'",
        "Write-Host ''",
    ]
    for candidate in candidates:
        label = candidate.name.replace("_", "-")
        lines.extend(
            [
                f"Write-Host '== {candidate.name} =='",
                "python tools\\adb_test_audiobook_launcher_route_variant.py "
                f"--preset {candidate.name} --apply --i-understand-this-writes-process-memory",
                f"python tools\\r1_adb_control.py screenshot --label {powershell_quote(label + '-before-open')}",
                "python tools\\r1_adb_control.py preset main-audiobooks --after-screenshot",
                "Start-Sleep -Seconds 2",
                f"python tools\\r1_adb_control.py screenshot --label {powershell_quote(label + '-result')}",
                "python tools\\adb_test_audiobook_launcher_route_variant.py "
                "--preset title --apply --i-understand-this-writes-process-memory",
            ]
        )
        if pause_between:
            lines.extend(
                [
                    "Write-Host 'Record what opened, then return to the main launcher.'",
                    "Read-Host 'Press Enter for the next route'",
                ]
            )
        lines.append("Write-Host ''")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote: {path}")


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command")

    list_parser = sub.add_parser("list", help="List route candidates.")
    list_parser.add_argument("--include-risky", action="store_true")

    scan_parser = sub.add_parser("scan-binary", help="Scan a hiby_player binary for route-like UTF-16 strings.")
    scan_parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)

    script_parser = sub.add_parser("make-script", help="Generate a PowerShell route test script.")
    script_parser.add_argument("--output", type=Path, default=Path("work/ui-route-lab/test-route-candidates.ps1"))
    script_parser.add_argument("--include-risky", action="store_true")
    script_parser.add_argument("--no-pauses", action="store_true")

    args = parser.parse_args()
    command = args.command or "list"

    if command == "list":
        print_candidates(include_risky=args.include_risky)
        return 0
    if command == "scan-binary":
        if not args.binary.exists():
            raise SystemExit(f"binary not found: {args.binary}")
        for offset, text in route_strings_from_binary(args.binary):
            print(f"0x{offset:08x}\t{text}")
        return 0
    if command == "make-script":
        write_live_script(
            args.output,
            include_risky=args.include_risky,
            pause_between=not args.no_pauses,
        )
        return 0
    parser.error(f"unknown command: {command}")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
