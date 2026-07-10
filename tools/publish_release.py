#!/usr/bin/env python3
"""Publish a GitHub release with assets for the HiBy R1 audiobook firmware.

Converted from publish_github_release.ps1.
Uses the ``gh`` CLI for release creation and asset upload instead of the
GitHub REST API + Git Credential Manager used by the PowerShell original.

Features:
  - Create or reuse a release for a given tag
  - Upload assets (with optional replacement of existing ones)
  - Verify-only mode to inspect a release without modifying it
  - Body text from inline argument or from a file
"""

from __future__ import annotations

import argparse
import logging
import subprocess
import sys
from pathlib import Path

logger = logging.getLogger(__name__)

DEFAULT_REPO = "yetisoldier/Hiby-R1-Audiobook-Mod"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def resolve_path_strict(path_value: str) -> Path:
    """Resolve a path and assert it exists."""
    p = Path(path_value).resolve()
    if not p.exists():
        raise FileNotFoundError(f"Missing path: {path_value}")
    return p


def run_gh(args: list[str], *, check: bool = True, capture: bool = False) -> subprocess.CompletedProcess:
    """Run a ``gh`` CLI command and return the completed process."""
    cmd = ["gh", *args]
    logger.debug("gh command: %s", " ".join(cmd))
    return subprocess.run(
        cmd,
        capture_output=capture,
        text=True,
        check=check,
    )


def gh_release_exists(repo: str, tag: str) -> bool:
    """Return True if a GitHub release exists for *tag*."""
    result = subprocess.run(
        ["gh", "release", "view", tag, "--repo", repo],
        capture_output=True,
        text=True,
    )
    return result.returncode == 0


def gh_create_release(
    repo: str,
    tag: str,
    name: str,
    target_commitish: str,
    body: str,
    draft: bool,
    prerelease: bool,
) -> None:
    """Create a new GitHub release."""
    cmd = [
        "gh", "release", "create", tag,
        "--repo", repo,
        "--title", name,
        "--notes", body,
        "--target", target_commitish,
    ]
    if draft:
        cmd.append("--draft")
    if prerelease:
        cmd.append("--prerelease")
    run_gh(cmd[1:], check=True)


def gh_upload_asset(
    repo: str,
    tag: str,
    asset_path: str,
    *,
    replace: bool,
) -> None:
    """Upload an asset to a release, optionally deleting an existing one first."""
    if replace:
        asset_name = Path(asset_path).name
        # Try to delete existing asset if it exists.
        delete_cmd = [
            "gh", "release", "delete-asset",
            tag, asset_name,
            "--repo", repo,
            "--yes",
        ]
        subprocess.run(delete_cmd, capture_output=True, text=True)
        # Non-zero exit is fine if the asset doesn't exist yet.

    cmd = [
        "gh", "release", "upload", tag,
        asset_path,
        "--repo", repo,
    ]
    if replace:
        cmd.append("--clobber")
    run_gh(cmd[1:], check=True)


def gh_view_release(repo: str, tag: str) -> str:
    """Return the ``gh release view`` output as text."""
    result = run_gh(
        ["release", "view", tag, "--repo", repo],
        check=True,
        capture=True,
    )
    return result.stdout


def get_default_target_commitish() -> str:
    """Return the current HEAD commit SHA."""
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout.strip()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Publish a GitHub release with assets for the HiBy R1 audiobook firmware.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )

    parser.add_argument(
        "--repository",
        default=DEFAULT_REPO,
        help="GitHub repository (owner/name).",
    )
    parser.add_argument(
        "--tag",
        required=True,
        help="Git tag for the release.",
    )
    parser.add_argument(
        "--name",
        default="",
        help="Release title (defaults to tag name).",
    )
    parser.add_argument(
        "--target-commitish",
        default="",
        help="Target commitish for the release (defaults to current HEAD).",
    )
    parser.add_argument(
        "--body-file",
        default="",
        help="Path to a file containing the release body text.",
    )
    parser.add_argument(
        "--body",
        default="",
        help="Inline release body text.",
    )
    parser.add_argument(
        "--github-user",
        default="yetisoldier",
        help="GitHub username (unused with gh CLI, kept for compatibility).",
    )
    parser.add_argument(
        "--assets",
        nargs="*",
        default=[],
        help="Asset file paths to upload (comma or space separated values accepted).",
    )
    parser.add_argument("--draft", action="store_true", help="Create as a draft release.")
    parser.add_argument("--prerelease", action="store_true", help="Create as a prerelease.")
    parser.add_argument(
        "--replace-assets",
        action="store_true",
        help="Replace existing assets with the same name.",
    )
    parser.add_argument(
        "--verify-only",
        action="store_true",
        help="Only view/verify the release; do not create or upload.",
    )

    return parser


def parse_assets(raw_assets: list[str]) -> list[str]:
    """Split asset arguments on commas and strip whitespace."""
    result: list[str] = []
    for item in raw_assets:
        for part in item.split(","):
            trimmed = part.strip()
            if trimmed:
                result.append(trimmed)
    return result


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = build_parser()
    args = parser.parse_args(argv)

    # --- Resolve asset paths -------------------------------------------------
    asset_specs = parse_assets(args.assets)
    asset_paths: list[str] = []
    for spec in asset_specs:
        try:
            resolved = resolve_path_strict(spec)
            asset_paths.append(str(resolved))
        except FileNotFoundError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 2

    # --- Verify-only mode ----------------------------------------------------
    if args.verify_only:
        try:
            output = gh_view_release(args.repository, args.tag)
        except subprocess.CalledProcessError:
            print(
                f"ERROR: Release for tag '{args.tag}' not found in {args.repository}.",
                file=sys.stderr,
            )
            return 2
        print(output)
        # Check expected assets are present.
        for ap in asset_paths:
            asset_name = Path(ap).name
            if asset_name not in output:
                print(f"ERROR: Release is missing expected asset: {asset_name}", file=sys.stderr)
                return 2
        return 0

    # --- Resolve body text ---------------------------------------------------
    body = args.body
    if args.body_file:
        try:
            body_path = resolve_path_strict(args.body_file)
        except FileNotFoundError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 2
        body = body_path.read_text(encoding="utf-8")

    # --- Resolve name --------------------------------------------------------
    name = args.name or args.tag

    # --- Resolve target commitish --------------------------------------------
    target_commitish = args.target_commitish
    if not target_commitish:
        try:
            target_commitish = get_default_target_commitish()
        except subprocess.CalledProcessError:
            print("ERROR: Unable to resolve current HEAD for target_commitish", file=sys.stderr)
            return 2

    # --- Create or reuse release ---------------------------------------------
    if gh_release_exists(args.repository, args.tag):
        print(f"Using existing release: {args.tag}")
    else:
        try:
            gh_create_release(
                repo=args.repository,
                tag=args.tag,
                name=name,
                target_commitish=target_commitish,
                body=body,
                draft=args.draft,
                prerelease=args.prerelease,
            )
            print(f"Created release: {args.tag}")
        except subprocess.CalledProcessError as exc:
            print(f"ERROR: Failed to create release: {exc}", file=sys.stderr)
            return 2

    # --- Upload assets -------------------------------------------------------
    for ap in asset_paths:
        asset_name = Path(ap).name
        try:
            gh_upload_asset(
                repo=args.repository,
                tag=args.tag,
                asset_path=ap,
                replace=args.replace_assets,
            )
            print(f"Uploaded: {asset_name} ({Path(ap).stat().st_size} bytes)")
        except subprocess.CalledProcessError as exc:
            print(f"ERROR: Asset upload failed for '{asset_name}': {exc}", file=sys.stderr)
            return 2

    # --- Summary -------------------------------------------------------------
    try:
        output = gh_view_release(args.repository, args.tag)
        print(output)
    except subprocess.CalledProcessError:
        pass

    return 0


if __name__ == "__main__":
    sys.exit(main())