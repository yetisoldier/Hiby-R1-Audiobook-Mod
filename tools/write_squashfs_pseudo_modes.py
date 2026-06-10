#!/usr/bin/env python3
"""Write mksquashfs pseudo-file mode overrides from a reference SquashFS image."""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


LINE_RE = re.compile(
    r"^(?P<mode>\S+)\s+(?P<owner>.+?)\s+\d+\s+"
    r"\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}\s+(?P<path>.+)$"
)


def perm_bits(triad: str, special: int) -> int:
    value = 0
    if triad[0] == "r":
        value |= 4
    if triad[1] == "w":
        value |= 2
    if triad[2] in ("x", "s", "t"):
        value |= 1
    return value | special


def mode_to_octal(mode: str) -> str:
    if len(mode) < 10:
        raise ValueError(f"unexpected mode string: {mode}")
    owner_special = 4 if mode[3] in ("s", "S") else 0
    group_special = 2 if mode[6] in ("s", "S") else 0
    other_special = 1 if mode[9] in ("t", "T") else 0
    special = owner_special + group_special + other_special
    owner = perm_bits(mode[1:4], 0)
    group = perm_bits(mode[4:7], 0)
    other = perm_bits(mode[7:10], 0)
    return f"0{special}{owner}{group}{other}"


def escape_pseudo_path(path: str) -> str:
    path = path.replace("\\", "\\\\")
    return re.sub(r"(\s)", r"\\\1", path)


def iter_modes(rootfs: Path, unsquashfs: Path) -> list[str]:
    proc = subprocess.run(
        [str(unsquashfs), "-ll", str(rootfs)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if proc.returncode not in (0, 1):
        raise SystemExit(proc.stdout)

    lines: list[str] = []
    for raw_line in proc.stdout.splitlines():
        match = LINE_RE.match(raw_line)
        if not match:
            continue
        mode = match.group("mode")
        if mode.startswith("l"):
            continue
        path = match.group("path")
        path = path.split(" -> ", 1)[0]
        if not path.startswith("squashfs-root/"):
            continue
        relative = path.removeprefix("squashfs-root/")
        if relative == "":
            continue
        lines.append(
            f"{escape_pseudo_path(relative)} m {mode_to_octal(mode)} 0 0"
        )
    return lines


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rootfs", type=Path, required=True)
    parser.add_argument("--unsquashfs", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    lines = iter_modes(args.rootfs, args.unsquashfs)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"wrote {len(lines)} mode overrides to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
