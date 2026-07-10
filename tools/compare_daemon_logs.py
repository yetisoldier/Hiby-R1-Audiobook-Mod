#!/usr/bin/env python3
"""
compare_daemon_logs.py — Compare shell daemon and C daemon (shadow mode) logs.

Parses both log files, matches entries by timestamp within a 2-second window,
and flags discrepancies in path detection, position reads, autostart triggers,
restore decisions, save decisions, and completion detection.

Usage:
    python3 tools/compare_daemon_logs.py \\
        --shell-log resume-daemon.log \\
        --c-log resume-daemon-c.log \\
        --out-dir work/log-comparison

Exit code: 0 if logs match, 1 if discrepancies found.
"""

import argparse
import json
import os
import re
import sys
from datetime import datetime, timedelta
from pathlib import Path


# ── Log line parsing ────────────────────────────────────────────────

# Shell daemon timestamp format: "2024-01-15T12:34:56+0000 message"
# C daemon timestamp format:     "2024-01-15T12:34:56+0000 message" (ISO 8601)
# Both use the same format from log_msg().
TIMESTAMP_RE = re.compile(
    r'^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{4})\s+(.*)$'
)


def parse_timestamp(ts_str):
    """Parse an ISO 8601 timestamp with +HHMM offset into a datetime."""
    # Python's fromisoformat doesn't handle +HHMM (no colon) before 3.11
    # Manually convert: "2026-07-10T10:00:00+0000" -> "2026-07-10T10:00:00+00:00"
    if len(ts_str) >= 25 and ts_str[-5] in '+-' and ts_str[-4:].isdigit():
        ts_str = ts_str[:-2] + ':' + ts_str[-2:]
    try:
        return datetime.fromisoformat(ts_str)
    except ValueError:
        return None


def parse_log_line(line):
    """Parse a single log line into (timestamp, message) or None."""
    line = line.rstrip('\n\r')
    if not line:
        return None
    m = TIMESTAMP_RE.match(line)
    if not m:
        return None
    ts = parse_timestamp(m.group(1))
    if ts is None:
        return None
    return (ts, m.group(2))


def parse_log_file(path):
    """Parse a log file into a list of (timestamp, message) tuples."""
    entries = []
    try:
        with open(path, 'r', errors='replace') as f:
            for line in f:
                parsed = parse_log_line(line)
                if parsed:
                    entries.append(parsed)
    except FileNotFoundError:
        print(f"Error: log file not found: {path}", file=sys.stderr)
        return None
    return entries


# ── Log entry classification ────────────────────────────────────────

# Categories of log entries we compare
CATEGORIES = [
    'path_detection',
    'position_read',
    'autostart_trigger',
    'restore_decision',
    'save_decision',
    'completion_detection',
    'other',
]


def classify_entry(message):
    """Classify a log message into a category for comparison."""
    msg_lower = message.lower()

    # Autostart trigger (check before path/position since it mentions marker)
    if 'marker seq' in msg_lower or 'autostart' in msg_lower:
        return 'autostart_trigger'

    # Completion detection
    if 'completed' in msg_lower or 'completion' in msg_lower or 'start-over' in msg_lower or 'start_over' in msg_lower:
        return 'completion_detection'

    # Restore decision (check before save/position since restore mentions pos)
    if 'would do: restore' in msg_lower or 'restore' in msg_lower and ('target' in msg_lower or 'path=' in msg_lower):
        return 'restore_decision'

    # Save decision (check before position since save mentions pos)
    if 'would do: save' in msg_lower or ('save' in msg_lower and ('position' in msg_lower or 'path=' in msg_lower)):
        return 'save_decision'

    # Path detection
    if 'audiobook path=' in msg_lower or 'path preview' in msg_lower:
        return 'path_detection'

    # Position read
    if 'pos=' in msg_lower or ('position' in msg_lower and 'read' in msg_lower):
        return 'position_read'

    return 'other'


def extract_key_info(message, category):
    """Extract comparable key information from a log message."""
    if category == 'path_detection':
        # Extract path value
        m = re.search(r'path=([^\s]+)', message)
        if m:
            return {'path': m.group(1)}
        return {}

    elif category == 'position_read':
        # Extract position value
        m = re.search(r'pos=(\d+)', message)
        if m:
            return {'position_ms': int(m.group(1))}
        m = re.search(r'position_ms=(\d+)', message)
        if m:
            return {'position_ms': int(m.group(1))}
        return {}

    elif category == 'autostart_trigger':
        # Extract seq value
        m = re.search(r'seq[:\s]+(\d+)', message)
        if m:
            return {'seq': int(m.group(1))}
        return {}

    elif category == 'restore_decision':
        # Extract target/position info
        info = {}
        m = re.search(r'target_pos=(\d+)', message)
        if m:
            info['target_pos'] = int(m.group(1))
        m = re.search(r'target=(\d+)', message)
        if m:
            info['target'] = int(m.group(1))
        m = re.search(r'path=([^\s]+)', message)
        if m:
            info['path'] = m.group(1)
        return info

    elif category == 'save_decision':
        # Extract position/path info
        info = {}
        m = re.search(r'position_ms=(\d+)', message)
        if m:
            info['position_ms'] = int(m.group(1))
        m = re.search(r'pos=(\d+)', message)
        if m:
            info['position_ms'] = int(m.group(1))
        m = re.search(r'path=([^\s]+)', message)
        if m:
            info['path'] = m.group(1)
        return info

    elif category == 'completion_detection':
        return {'message': message.lower()}

    return {}


# ── Log comparison ──────────────────────────────────────────────────

def match_entries(shell_entries, c_entries, window_seconds=2):
    """Match log entries from both logs by timestamp within a window.

    Returns:
        matches: list of (shell_entry, c_entry) tuples for matched pairs
        shell_only: list of shell entries with no C counterpart
        c_only: list of C entries with no shell counterpart
    """
    matches = []
    shell_used = [False] * len(shell_entries)
    c_used = [False] * len(c_entries)
    window = timedelta(seconds=window_seconds)

    # For each shell entry, find the closest C entry within the window
    for si, (s_ts, s_msg) in enumerate(shell_entries):
        s_cat = classify_entry(s_msg)
        if s_cat == 'other':
            continue  # Skip unclassified entries

        best_ci = -1
        best_delta = None

        for ci, (c_ts, c_msg) in enumerate(c_entries):
            if c_used[ci]:
                continue
            c_cat = classify_entry(c_msg)
            if c_cat != s_cat:
                continue

            delta = abs(s_ts - c_ts)
            if delta <= window:
                if best_delta is None or delta < best_delta:
                    best_delta = delta
                    best_ci = ci

        if best_ci >= 0:
            matches.append(((s_ts, s_msg, s_cat),
                           (c_entries[best_ci][0], c_entries[best_ci][1], s_cat)))
            shell_used[si] = True
            c_used[best_ci] = True

    shell_only = [(shell_entries[i][0], shell_entries[i][1], classify_entry(shell_entries[i][1]))
                  for i in range(len(shell_entries))
                  if not shell_used[i] and classify_entry(shell_entries[i][1]) != 'other']

    c_only = [(c_entries[i][0], c_entries[i][1], classify_entry(c_entries[i][1]))
              for i in range(len(c_entries))
              if not c_used[i] and classify_entry(c_entries[i][1]) != 'other']

    return matches, shell_only, c_only


def compare_matched_entries(matches):
    """Compare matched entry pairs and find discrepancies.

    Returns:
        discrepancies: list of (timestamp, category, shell_info, c_info, description)
    """
    discrepancies = []

    for (s_ts, s_msg, s_cat), (c_ts, c_msg, c_cat) in matches:
        s_info = extract_key_info(s_msg, s_cat)
        c_info = extract_key_info(c_msg, c_cat)

        if s_cat == 'path_detection':
            s_path = s_info.get('path', '')
            c_path = c_info.get('path', '')
            if s_path and c_path and s_path != c_path:
                discrepancies.append((s_ts, s_cat, s_info, c_info,
                    f"Path mismatch: shell={s_path} c={c_path}"))

        elif s_cat == 'position_read':
            s_pos = s_info.get('position_ms')
            c_pos = c_info.get('position_ms')
            if s_pos is not None and c_pos is not None:
                # Allow small difference (within 500ms — timing jitter)
                if abs(s_pos - c_pos) > 500:
                    discrepancies.append((s_ts, s_cat, s_info, c_info,
                        f"Position mismatch: shell={s_pos} c={c_pos}"))

        elif s_cat == 'autostart_trigger':
            s_seq = s_info.get('seq')
            c_seq = c_info.get('seq')
            if s_seq is not None and c_seq is not None and s_seq != c_seq:
                discrepancies.append((s_ts, s_cat, s_info, c_info,
                    f"Seq mismatch: shell={s_seq} c={c_seq}"))

        elif s_cat == 'restore_decision':
            s_target = s_info.get('target_pos') or s_info.get('target')
            c_target = c_info.get('target_pos') or c_info.get('target')
            if s_target is not None and c_target is not None:
                if abs(s_target - c_target) > 1000:
                    discrepancies.append((s_ts, s_cat, s_info, c_info,
                        f"Restore target mismatch: shell={s_target} c={c_target}"))

        elif s_cat == 'save_decision':
            s_pos = s_info.get('position_ms')
            c_pos = c_info.get('position_ms')
            if s_pos is not None and c_pos is not None:
                if abs(s_pos - c_pos) > 500:
                    discrepancies.append((s_ts, s_cat, s_info, c_info,
                        f"Save position mismatch: shell={s_pos} c={c_pos}"))

    return discrepancies


# ── Report generation ──────────────────────────────────────────────

def generate_report(matches, shell_only, c_only, discrepancies, out_dir):
    """Generate comparison report and write to out_dir."""
    out_path = Path(out_dir)
    out_path.mkdir(parents=True, exist_ok=True)

    # Summary
    summary = {
        'total_shell_entries': len(matches) + len(shell_only),
        'total_c_entries': len(matches) + len(c_only),
        'matched_pairs': len(matches),
        'shell_only_entries': len(shell_only),
        'c_only_entries': len(c_only),
        'discrepancies': len(discrepancies),
        'categories': {},
    }

    for cat in CATEGORIES:
        cat_matches = [m for m in matches if m[0][2] == cat]
        cat_shell_only = [e for e in shell_only if e[2] == cat]
        cat_c_only = [e for e in c_only if e[2] == cat]
        cat_discrepancies = [d for d in discrepancies if d[1] == cat]
        summary['categories'][cat] = {
            'matched': len(cat_matches),
            'shell_only': len(cat_shell_only),
            'c_only': len(cat_c_only),
            'discrepancies': len(cat_discrepancies),
        }

    # Write JSON report
    report_file = out_path / 'comparison-report.json'
    report = {
        'summary': summary,
        'discrepancies': [
            {
                'timestamp': d[0].isoformat() if d[0] else None,
                'category': d[1],
                'shell_info': d[2],
                'c_info': d[3],
                'description': d[4],
            }
            for d in discrepancies
        ],
        'shell_only': [
            {
                'timestamp': e[0].isoformat(),
                'category': e[2],
                'message': e[1],
            }
            for e in shell_only
        ],
        'c_only': [
            {
                'timestamp': e[0].isoformat(),
                'category': e[2],
                'message': e[1],
            }
            for e in c_only
        ],
    }

    with open(report_file, 'w') as f:
        json.dump(report, f, indent=2)

    # Write human-readable summary
    text_file = out_path / 'comparison-summary.txt'
    with open(text_file, 'w') as f:
        f.write("=== Daemon Log Comparison Report ===\n\n")
        f.write(f"Shell entries:  {summary['total_shell_entries']}\n")
        f.write(f"C entries:      {summary['total_c_entries']}\n")
        f.write(f"Matched pairs:   {summary['matched_pairs']}\n")
        f.write(f"Shell-only:      {summary['shell_only_entries']}\n")
        f.write(f"C-only:          {summary['c_only_entries']}\n")
        f.write(f"Discrepancies:   {summary['discrepancies']}\n")
        f.write("\n--- By Category ---\n")
        for cat in CATEGORIES:
            cs = summary['categories'][cat]
            f.write(f"  {cat:25s}  matched={cs['matched']:4d}  "
                    f"shell_only={cs['shell_only']:4d}  "
                    f"c_only={cs['c_only']:4d}  "
                    f"discrepancies={cs['discrepancies']:4d}\n")

        if discrepancies:
            f.write("\n--- Discrepancies ---\n")
            for d in discrepancies:
                f.write(f"  [{d[0].isoformat() if d[0] else '?'}] {d[1]}: {d[4]}\n")

        if shell_only:
            f.write("\n--- Shell-only entries (no C counterpart) ---\n")
            for e in shell_only[:50]:  # Limit to first 50
                f.write(f"  [{e[0].isoformat()}] {e[2]}: {e[1]}\n")
            if len(shell_only) > 50:
                f.write(f"  ... and {len(shell_only) - 50} more\n")

        if c_only:
            f.write("\n--- C-only entries (no shell counterpart) ---\n")
            for e in c_only[:50]:
                f.write(f"  [{e[0].isoformat()}] {e[2]}: {e[1]}\n")
            if len(c_only) > 50:
                f.write(f"  ... and {len(c_only) - 50} more\n")

    return summary


def print_summary(summary):
    """Print a brief summary to stdout."""
    print(f"\n=== Log Comparison Summary ===")
    print(f"  Shell entries:  {summary['total_shell_entries']}")
    print(f"  C entries:      {summary['total_c_entries']}")
    print(f"  Matched pairs:   {summary['matched_pairs']}")
    print(f"  Shell-only:      {summary['shell_only_entries']}")
    print(f"  C-only:          {summary['c_only_entries']}")
    print(f"  Discrepancies:   {summary['discrepancies']}")
    print()
    for cat in CATEGORIES:
        cs = summary['categories'][cat]
        if cs['matched'] or cs['shell_only'] or cs['c_only'] or cs['discrepancies']:
            print(f"  {cat:25s}  matched={cs['matched']:4d}  "
                  f"shell_only={cs['shell_only']:4d}  "
                  f"c_only={cs['c_only']:4d}  "
                  f"discrepancies={cs['discrepancies']:4d}")
    print()


# ── Main ────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Compare shell daemon and C daemon (shadow mode) logs."
    )
    parser.add_argument(
        '--shell-log', required=True,
        help='Path to the shell daemon log file'
    )
    parser.add_argument(
        '--c-log', required=True,
        help='Path to the C daemon shadow mode log file'
    )
    parser.add_argument(
        '--out-dir', default='.',
        help='Directory for output files (default: current dir)'
    )
    parser.add_argument(
        '--window', type=int, default=2,
        help='Match window in seconds (default: 2)'
    )
    args = parser.parse_args()

    # Parse both logs
    shell_entries = parse_log_file(args.shell_log)
    if shell_entries is None:
        return 1

    c_entries = parse_log_file(args.c_log)
    if c_entries is None:
        return 1

    print(f"Parsed {len(shell_entries)} shell entries, {len(c_entries)} C entries")

    # Match entries
    matches, shell_only, c_only = match_entries(
        shell_entries, c_entries, args.window
    )

    # Compare matched entries
    discrepancies = compare_matched_entries(matches)

    # Generate report
    summary = generate_report(matches, shell_only, c_only, discrepancies, args.out_dir)
    print_summary(summary)

    print(f"Report written to: {args.out_dir}/comparison-report.json")
    print(f"Summary written to: {args.out_dir}/comparison-summary.txt")

    # Exit code: 0 if no discrepancies, 1 if discrepancies found
    if summary['discrepancies'] > 0:
        print(f"\nDISCREPANCIES FOUND ({summary['discrepancies']} issues)")
        return 1
    else:
        print("\nNo discrepancies found — logs match")
        return 0


if __name__ == '__main__':
    sys.exit(main())