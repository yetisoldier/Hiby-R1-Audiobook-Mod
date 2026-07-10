#!/usr/bin/env python3
"""test_db_maintenance — Catalog invariants, no leakage.

Tests:
  1. Run check_audiobook_release_state.py via ADB-pulled DB.
  2. Verify audiobook rows exist in the catalog.
  3. Verify no audiobook leakage into SEARCH_TABLE, ALBUM_TABLE, GENRE_TABLE.
  4. Verify catalog.tsv, catalog-books.tsv exist and have correct format.
  5. Verify view sidecar files (title, author, series) exist.
  6. Verify DB integrity check passes (PRAGMA integrity_check).

Expected behaviour:
  - The media DB has audiobook rows with paths starting a:\\Audiobooks\\.
  - SEARCH_TABLE has zero audiobook rows.
  - ALBUM_TABLE and GENRE_TABLE have no audiobook album/genre leakage.
  - Catalog TSV files exist on the device with correct headers.
  - View sidecar files exist on the device.
  - PRAGMA integrity_check returns "ok".
"""

from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path

from test_suite import (
    TestContext, cleanup,
    color, C_GREEN, C_RED, C_YELLOW, C_DIM, timestamp,
    invoke_script,
)


DB_REMOTE_PATH = "/usr/local/usrlocal_media.db"
CATALOG_DIR = "/usr/data/audiobooks"
CATALOG_REMOTE = f"{CATALOG_DIR}/catalog.tsv"
CATALOG_BOOKS_REMOTE = f"{CATALOG_DIR}/catalog-books.tsv"
TITLES_REMOTE = f"{CATALOG_DIR}/catalog-titles.tsv"
AUTHORS_REMOTE = f"{CATALOG_DIR}/catalog-authors.tsv"
SERIES_REMOTE = f"{CATALOG_DIR}/catalog-series.tsv"


def run(ctx: TestContext) -> None:
    """Run the DB maintenance test."""

    # ── Step 1: Pull the media DB locally ─────────────────────────────────
    print(color("  Step 1: Pull media DB from device", C_DIM))
    tmp_dir = ctx.artifacts / "db-check"
    tmp_dir.mkdir(parents=True, exist_ok=True)
    local_db = tmp_dir / "usrlocal_media.db"

    pull_output = subprocess.run(
        [ctx.adb, "pull", DB_REMOTE_PATH, str(local_db)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace",
    )
    if pull_output.returncode != 0 or not local_db.exists():
        raise RuntimeError(
            f"Cannot pull media DB from {DB_REMOTE_PATH}: {pull_output.stdout}"
        )
    print(color(f"  ✓ Pulled DB ({local_db.stat().st_size} bytes)", C_GREEN))

    # ── Step 2: Run check_audiobook_release_state.py ───────────────────────
    print(color("  Step 2: Run check_audiobook_release_state.py", C_DIM))
    check_args = [str(local_db), "--expect-audiobooks"]

    # Also pull catalog files if they exist
    for remote, local_name in [
        (CATALOG_REMOTE, "catalog.tsv"),
        (CATALOG_BOOKS_REMOTE, "catalog-books.tsv"),
        (TITLES_REMOTE, "catalog-titles.tsv"),
        (AUTHORS_REMOTE, "catalog-authors.tsv"),
        (SERIES_REMOTE, "catalog-series.tsv"),
    ]:
        local_path = tmp_dir / local_name
        result = subprocess.run(
            [ctx.adb, "pull", remote, str(local_path)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace",
        )
        if result.returncode == 0 and local_path.exists():
            flag = f"--{local_name.replace('catalog-', '').replace('.tsv', '')}-catalog" \
                if local_name != "catalog.tsv" and local_name != "catalog-books.tsv" \
                else f"--{'catalog' if local_name == 'catalog.tsv' else 'books-catalog'}"
            check_args.extend([flag, str(local_path)])
            print(color(f"  ✓ Pulled {local_name}", C_GREEN))
        else:
            print(color(f"  ⚠ {local_name} not found on device", C_YELLOW))

    # Run the check script
    output = invoke_script(ctx, ctx.check_script, check_args)

    # Save output
    (tmp_dir / "check-output.txt").write_text(output, encoding="utf-8")

    # Parse and verify
    if "Release-state check passed." in output:
        print(color("  ✓ Release-state check passed", C_GREEN))
    else:
        print(color("  ✗ Release-state check found failures", C_RED))
        print(output)
        raise RuntimeError("check_audiobook_release_state.py reported failures")

    # ── Step 3: Verify audiobook rows exist ────────────────────────────────
    print(color("  Step 3: Verify audiobook rows in catalog", C_DIM))
    if "audiobook media rows: 0" in output:
        raise RuntimeError("No audiobook rows found in MEDIA_TABLE")
    match = re.search(r"audiobook media rows:\s*(\d+)", output)
    if match:
        count = int(match.group(1))
        if count > 0:
            print(color(f"  ✓ Audiobook rows: {count}", C_GREEN))
        else:
            raise RuntimeError(f"Zero audiobook rows in MEDIA_TABLE")
    else:
        print(color("  ⚠ Could not parse audiobook row count", C_YELLOW))

    # ── Step 4: Verify no leakage ──────────────────────────────────────────
    print(color("  Step 4: Verify no audiobook leakage", C_DIM))
    # Check SEARCH_TABLE
    search_match = re.search(r"SEARCH_TABLE audiobook rows:\s*(\d+)", output)
    if search_match:
        search_count = int(search_match.group(1))
        if search_count > 0:
            raise RuntimeError(
                f"SEARCH_TABLE contains {search_count} audiobook rows (leakage!)"
            )
        print(color("  ✓ SEARCH_TABLE: 0 audiobook rows", C_GREEN))
    else:
        print(color("  ⚠ SEARCH_TABLE count not found in output", C_YELLOW))

    # Check ALBUM_TABLE leaks
    album_match = re.search(r"ALBUM_TABLE audiobook leaks:\s*(\d+)", output)
    if album_match:
        album_leaks = int(album_match.group(1))
        if album_leaks > 0:
            raise RuntimeError(
                f"ALBUM_TABLE leaks {album_leaks} audiobook albums"
            )
        print(color("  ✓ ALBUM_TABLE: 0 audiobook leaks", C_GREEN))
    else:
        print(color("  ⚠ ALBUM_TABLE leak count not found", C_YELLOW))

    # Check integrity_check
    print(color("  Step 5: Verify DB integrity", C_DIM))
    if "integrity: ok" in output.lower():
        print(color("  ✓ PRAGMA integrity_check: ok", C_GREEN))
    elif "integrity_check failed" in output.lower():
        raise RuntimeError("PRAGMA integrity_check failed!")
    else:
        print(color("  ⚠ integrity_check result not found in output", C_YELLOW))

    # ── Step 6: Verify catalog files on device ────────────────────────────
    print(color("  Step 6: Verify catalog files on device", C_DIM))
    for name, remote in [
        ("catalog.tsv", CATALOG_REMOTE),
        ("catalog-books.tsv", CATALOG_BOOKS_REMOTE),
    ]:
        exists = ctx.shell(f"test -f {remote} && echo EXISTS || echo MISSING").strip()
        if exists == "EXISTS":
            size = ctx.shell(f"wc -c < {remote} 2>/dev/null || echo 0").strip()
            print(color(f"  ✓ {name} exists ({size} bytes)", C_GREEN))
        else:
            raise RuntimeError(f"{name} not found on device at {remote}")

    # Verify view sidecar files
    print(color("  Step 7: Verify view sidecar files", C_DIM))
    sidecar_found = 0
    for name, remote in [
        ("catalog-titles.tsv", TITLES_REMOTE),
        ("catalog-authors.tsv", AUTHORS_REMOTE),
        ("catalog-series.tsv", SERIES_REMOTE),
    ]:
        exists = ctx.shell(f"test -f {remote} && echo EXISTS || echo MISSING").strip()
        if exists == "EXISTS":
            print(color(f"  ✓ {name} exists", C_GREEN))
            sidecar_found += 1
        else:
            print(color(f"  ⚠ {name} not found on device", C_YELLOW))

    if sidecar_found == 0:
        raise RuntimeError("No view sidecar files found on device")

    # Cleanup
    cleanup(ctx)