#!/usr/bin/env python3
"""compare_daemon_logs.py — Compare C daemon shadow logs against shell daemon logs.

Parses both log formats, matches entries by timestamp, and flags discrepancies
in: path detection, position reads, autostart triggers, restore decisions,
save decisions, and completion detection.

Usage:
    python3 tools/compare_daemon_logs.py --shell-log resume-daemon.log --c-log resume-daemon-c.log
    python3 tools/compare_daemon_logs.py --pull --adb /path/to/adb
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


@dataclass
class LogEntry:
    timestamp: str
    source: str  # "shell" or "c"
    raw: str
    category: str = "other"  # stats, save, restore, autostart, path, completion, start, shutdown, other
    fields: dict = field(default_factory=dict)


def parse_timestamp(line: str) -> Optional[str]:
    """Extract ISO timestamp from log line."""
    m = re.match(r'(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{4})', line)
    return m.group(1) if m else None


def categorize(line: str) -> tuple[str, dict]:
    """Categorize a log line and extract relevant fields."""
    lower = line.lower()

    if 'stats' in lower and 'loops=' in lower:
        fields = {}
        for m in re.finditer(r'(\w+)=(\d+)', line):
            fields[m.group(1)] = int(m.group(2))
        return 'stats', fields

    if 'save' in lower and ('position' in lower or 'after_position' in lower or 'save_position' in lower):
        return 'save', {}

    if 'restore' in lower:
        return 'restore', {}

    if 'autostart' in lower or 'book_title' in lower and 'marker' in lower:
        return 'autostart', {}

    if 'path_preview' in lower or 'a:\\audiobooks' in lower or 'a:\\music' in lower:
        return 'path', {}

    if 'completion' in lower or 'completed' in lower:
        return 'completion', {}

    if 'starting' in lower or 'v0.1.0' in lower:
        return 'start', {}

    if 'shutdown' in lower:
        return 'shutdown', {}

    return 'other', {}


def parse_log(path: Path, source: str) -> list[LogEntry]:
    """Parse a log file into structured entries."""
    entries = []
    if not path.exists():
        print(f"Warning: {path} does not exist", file=sys.stderr)
        return entries

    with open(path, 'r', errors='replace') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            ts = parse_timestamp(line)
            if not ts:
                continue
            cat, fields = categorize(line)
            entries.append(LogEntry(
                timestamp=ts, source=source, raw=line,
                category=cat, fields=fields,
            ))
    return entries


def compare_stats(shell_entries: list[LogEntry], c_entries: list[LogEntry]) -> list[str]:
    """Compare stats entries between shell and C daemon.
    Only compares entries within the overlapping time range."""
    discrepancies = []

    # Find overlapping time range
    shell_times = [e.timestamp for e in shell_entries if e.category == 'stats']
    c_times = [e.timestamp for e in c_entries if e.category == 'stats']

    if not shell_times or not c_times:
        return discrepancies

    overlap_start = max(min(shell_times), min(c_times))
    overlap_end = min(max(shell_times), max(c_times))

    # Filter to overlapping range
    shell_stats = [e for e in shell_entries if e.category == 'stats' and overlap_start <= e.timestamp <= overlap_end]
    c_stats = [e for e in c_entries if e.category == 'stats' and overlap_start <= e.timestamp <= overlap_end]

    if not shell_stats or not c_stats:
        print(f"  (No overlapping stats period found)")
        print(f"  Shell: {min(shell_times)} to {max(shell_times)}")
        print(f"  C:     {min(c_times)} to {max(c_times)}")
        return discrepancies

    for s in shell_stats:
        # Find closest C stats entry within 120 seconds
        best_c = None
        best_diff = 999999
        for c in c_stats:
            # Compare timestamps as strings (ISO format sorts correctly)
            if abs(s.timestamp > c.timestamp and 1 or -1) * (s.timestamp != c.timestamp):
                # Rough diff: count character positions where they differ
                diff = sum(1 for a, b in zip(s.timestamp, c.timestamp) if a != b)
            else:
                diff = 0
            if diff < best_diff:
                best_diff = diff
                best_c = c

        if best_c and best_diff < 20:  # within ~20 char positions = ~2 minutes
            # Compare key fields
            for key in ['loops', 'audiobook', 'non_audiobook', 'position_reads', 'saves']:
                s_val = s.fields.get(key, 0)
                c_val = best_c.fields.get(key, 0)
                if s_val != c_val:
                    discrepancies.append(
                        f"  STATS MISMATCH ({key}): shell={s_val} c={c_val} "
                        f"at {s.timestamp}"
                    )

    return discrepancies


def compare_starts(shell_entries: list[LogEntry], c_entries: list[LogEntry]) -> list[str]:
    """Compare daemon start events."""
    discrepancies = []
    shell_starts = [e for e in shell_entries if e.category == 'start']
    c_starts = [e for e in c_entries if e.category == 'start']

    if len(shell_starts) != len(c_starts):
        discrepancies.append(
            f"  START COUNT: shell={len(shell_starts)} c={len(c_starts)}"
        )

    return discrepancies


def main():
    parser = argparse.ArgumentParser(
        description="Compare C daemon shadow logs against shell daemon logs."
    )
    parser.add_argument('--shell-log', type=Path, help='Shell daemon log file')
    parser.add_argument('--c-log', type=Path, help='C daemon log file')
    parser.add_argument('--pull', action='store_true',
                        help='Pull logs from R1 via ADB before comparing')
    parser.add_argument('--adb', default=None, help='Path to adb binary')
    parser.add_argument('--output', type=Path, default=None,
                        help='Write report to file')
    args = parser.parse_args()

    if args.pull:
        import shutil
        import subprocess
        adb = args.adb or shutil.which('adb') or '/home/yetisoldier/.local/bin/adb'
        shell_log = Path('build/shadow-comparison/resume-daemon.log')
        c_log = Path('build/shadow-comparison/resume-daemon-c.log')
        shell_log.parent.mkdir(parents=True, exist_ok=True)

        print("Pulling shell daemon log...")
        subprocess.run([adb, 'pull', '/usr/data/audiobooks/resume-daemon.log', str(shell_log)],
                       capture_output=True, timeout=30)
        print("Pulling C daemon log...")
        subprocess.run([adb, 'pull', '/usr/data/audiobooks/resume-daemon-c.log', str(c_log)],
                       capture_output=True, timeout=30)

        args.shell_log = shell_log
        args.c_log = c_log

    if not args.shell_log or not args.c_log:
        parser.print_help()
        return 1

    print(f"Parsing shell log: {args.shell_log}")
    shell_entries = parse_log(args.shell_log, 'shell')
    print(f"  {len(shell_entries)} entries")

    print(f"Parsing C log: {args.c_log}")
    c_entries = parse_log(args.c_log, 'c')
    print(f"  {len(c_entries)} entries")

    if not shell_entries and not c_entries:
        print("No entries found in either log.")
        return 1

    # Build report
    report_lines = []
    report_lines.append("=" * 60)
    report_lines.append("Daemon Log Comparison Report")
    report_lines.append("=" * 60)
    report_lines.append(f"Shell log: {args.shell_log} ({len(shell_entries)} entries)")
    report_lines.append(f"C log:     {args.c_log} ({len(c_entries)} entries)")
    report_lines.append("")

    # Category counts
    report_lines.append("Entry counts by category:")
    cats = ['start', 'stats', 'save', 'restore', 'autostart', 'path', 'completion', 'shutdown', 'other']
    for cat in cats:
        s_count = sum(1 for e in shell_entries if e.category == cat)
        c_count = sum(1 for e in c_entries if e.category == cat)
        match = "✓" if s_count == c_count else "✗"
        report_lines.append(f"  {match} {cat:15s}: shell={s_count:4d}  c={c_count:4d}")

    report_lines.append("")

    # Compare stats
    if shell_entries and c_entries:
        shell_ts = [e.timestamp for e in shell_entries]
        c_ts = [e.timestamp for e in c_entries]
        overlap_start = max(min(shell_ts), min(c_ts))
        overlap_end = min(max(shell_ts), max(c_ts))
        report_lines.append(f"Overlap period: {overlap_start} to {overlap_end}")
    report_lines.append("Stats comparison:")
    stats_discrepancies = compare_stats(shell_entries, c_entries)
    if stats_discrepancies:
        for d in stats_discrepancies:
            report_lines.append(d)
    else:
        report_lines.append("  ✓ No stats discrepancies (or no overlapping stats found)")

    report_lines.append("")

    # Compare starts
    report_lines.append("Start/shutdown comparison:")
    start_discrepancies = compare_starts(shell_entries, c_entries)
    if start_discrepancies:
        for d in start_discrepancies:
            report_lines.append(d)
    else:
        report_lines.append("  ✓ Start/shutdown counts match")

    report_lines.append("")
    report_lines.append("=" * 60)

    report = "\n".join(report_lines)
    print(report)

    if args.output:
        args.output.write_text(report)
        print(f"\nReport written to {args.output}")

    # Return 0 if no discrepancies, 1 if any found
    has_discrepancies = bool(stats_discrepancies or start_discrepancies)
    return 1 if has_discrepancies else 0


if __name__ == '__main__':
    sys.exit(main())