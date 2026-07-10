#!/usr/bin/env python3
"""Run the resume daemon logic test script.

Converted from test_r1_resume_daemon_logic_wsl.ps1.
On Linux, simply runs ``tools/test_r1_resume_daemon_logic.sh`` directly
instead of wrapping through WSL.
"""

from __future__ import annotations

import argparse
import logging
import subprocess
import sys
from pathlib import Path

logger = logging.getLogger(__name__)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run the resume daemon logic test script.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = build_parser()
    args = parser.parse_args(argv)

    repo_root = Path(__file__).resolve().parent.parent
    test_script = repo_root / "tools" / "test_r1_resume_daemon_logic.sh"

    if not test_script.exists():
        print(f"ERROR: Test script not found: {test_script}", file=sys.stderr)
        return 2

    result = subprocess.run(["sh", str(test_script)], cwd=str(repo_root))
    if result.returncode != 0:
        print(f"ERROR: Resume daemon logic test failed with exit code {result.returncode}", file=sys.stderr)
        return result.returncode

    print("Resume daemon logic test passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())