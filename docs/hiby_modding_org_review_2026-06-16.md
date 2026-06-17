# hiby-modding Organization Review - 2026-06-16

Source reviewed: <https://github.com/hiby-modding>

## Summary

The hiby-modding organization is worth tracking and collaborating with, but I
would not immediately move this project into their repos. Their active work is
mostly shared HiBy OS tooling plus R3 Pro II firmware modifications. Our project
is a focused, tested R1 audiobook firmware with a different user-facing goal and
a working public release history.

Best path: keep this repo as the canonical R1 audiobook firmware, open a
friendly discussion or issue with hiby-modding, cross-link the projects, and
offer specific reusable pieces back upstream once licensing/maintainer intent is
clear.

## Repositories Reviewed

- `hiby-modding/hiby-mods`
  - Active custom firmware collection, currently tested on R3 Pro II.
  - Useful ideas: Database Manager, PC database updater, sorting patch,
    album-art sizing, ADB entry, UI/theme changes, Bluetooth startup work.
- `hiby-modding/hiby_os_crack`
  - Shared firmware unpack/repack, rootfs, R1/R3 specs, and QEMU research.
  - Useful ideas: R1 hardware reference, QEMU notes, rootfs structure, safer
    documentation around update/recovery behavior.
- `hiby-modding/Meowby-R1`
  - R1-specific overlay/Nix workflow mirrored from Codeberg.
  - Useful ideas: overlay-only build discipline, battery/BT/ADB notes, R1
    config patches. Some audio-output patches are not audiobook-specific and
    should stay optional.
- `hiby-modding/.github`
  - Organization profile only.

## Best Improvement Candidates For This Firmware

1. DB/sidebar sort field audit
   - `hiby-mods` documents the `character` and `pinyin_charater` fields and the
     patched collation behavior in detail.
   - Our offline Python DB tool strips articles and punctuation. The shipped
     native C helper currently skips punctuation/space but does not strip leading
     articles, so audiobook title sidebar behavior may still be weaker than the
     newer hiby-mods database model.
   - Low-risk next dev work: align `r1_audiobook_db_maint.c`,
     `check_audiobook_release_state.py`, and fixture tests with the documented
     normalization rules, then test the right-side alphabetical jump list on the
     device.

2. On-device Audiobook/Database Manager
   - `hiby-mods` documents a binary-patched Settings submenu that copies
     `usrlocal_media.db` between internal storage and SD, then calls the stock
     DB close/reopen and UI refresh functions.
   - For our firmware, the same architectural pattern could eventually become an
     "Audiobook Tools" or "Rebuild Audiobooks" entry. That could remove the
     wait-for-watcher ambiguity after Music -> Update Database.
   - Risk: this requires deeper `hiby_player` code-cave patching. It should be
     explored in RAM or a dev-only build before any public release.

3. ADB/App Sync toggle
   - `hiby-mods` has a more user-facing ADB toggle concept than our current
     dev-only boot ADB wrapper.
   - Our release firmware should still avoid persistent ADB by default, but a
     dev build could borrow the marker-file idea and use the stock ADB activation
     function instead of init-only boot ADB.

4. Battery and responsiveness hygiene
   - `Meowby-R1` removes the guarded `batd` launch from `hiby_player.sh`,
     avoiding an SD card battery log every five seconds when `/usr/bin/batd` is
     present.
   - It also documents disabling the red LED breathing animation and shortening
     Bluetooth discoverable/pairable timeouts.
   - Low-risk next step: inspect device/runtime builds to see whether `batd` is
     actually present and running. If yes, consider a release-note-visible
     battery tweak.
     Bluetooth changes should be optional because audiobook users may rely on BT.

5. Build/release hygiene
   - `Meowby-R1` stores overlays instead of full firmware trees and uses a clean
     reference root to generate patches.
   - Our scripted build is already guarded, but an overlay manifest for the exact
     rootfs changes would make code review and upstream collaboration easier.

6. Album art performance guidance
   - `hiby-mods` recommends 360x360 JPEG cover art because oversized covers cost
     CPU/RAM on X1600-class players.
   - Our helper already picks sidecar cover files. We can improve README guidance
     for audiobook cover art size, especially for `.m4b` users with large
     embedded covers.

7. Track/path edge cases
   - hiby-mods issue discussions mention path-length-related missing tracks and
     album-order behavior. Our helper should get a fixture for very long
     audiobook paths and unpadded track names (`1`, `10`, `2`) to protect
     multipart book ordering.

## Things To Avoid Or Keep Optional

- Do not import R3 Pro II prebuilt `hiby_player` binaries. They target a
  different device/firmware and would be high brick risk on R1.
- Do not fold in output-power, gain-table, or DSD/native-output changes by
  default. They are interesting, but they change audio behavior outside the
  audiobook goal and raise support risk.
- Do not make ADB persistent in public builds by default.
- Do not remove locales/fonts or replace large font files unless we have a clear
  R1-specific reason and flash-size verification.
- Do not rely on full QEMU system emulation for release confidence yet. Current
  QEMU notes are useful for kernel/rootfs research, but device testing remains
  required for UI/audio behavior.

## Merge/Collaboration Recommendation

Recommended:

- Open a hiby_os_crack Discussion or a hiby-mods issue introducing the R1
  audiobook firmware, linking our release and docs.
- Ask whether they want an R1 audiobook section/link under the org, or whether
  they prefer this repo to stay independent and cross-linked.
- Offer reusable pieces:
  - R1 audiobook DB helper/watcher design.
  - R1 ADB framebuffer/touch automation.
  - Release verification/staging scripts.
  - Notes on Books -> Audiobooks route and per-book resume.
- Ask about license/permission before copying code or binary patch material.

Not recommended yet:

- Moving this repo wholesale into hiby-modding.
- Replacing our build flow with their R3 Pro II mod tool.
- Publishing a combined firmware with non-audiobook patches before we have a
  stable audiobook branch and a clear opt-in matrix.

## Suggested Next Dev Branch

`codex/r1-hiby-modding-integration`

Scope:

1. Add C helper normalization tests for article/punctuation/sidebar fields.
2. Align native DB helper sort/sidebar behavior with the documented HiBy collation
   model where it is safe.
3. Add long-path and unpadded-track fixture cases.
4. Inspect whether `batd` launches in stock R1 1.6 and document any battery tweak
   as opt-in or release-safe.
5. Prototype, but do not release, a settings-page "Audiobook Tools" concept based
   on the DB Manager patch pattern.

## Implemented Locally From This Review

- Local mirrors were created under `references/` for `hiby-mods`,
  `hiby_os_crack`, and `Meowby-R1`. These are ignored by Git and treated as
  research references, not vendored release code.
- The native R1 DB helper now matches the documented HiBy collation behavior for
  the parts we control safely:
  - strip leading punctuation,
  - strip leading articles `the, der, die, das, les, il, lo, la, le, el`,
  - use the normalized title for `character`, `pinyin_charater`, and C-side
    title/album sorting.
- The offline Python DB tool and release-state checker were aligned with the
  same normalization. The checker now validates audiobook `pinyin_charater`
  values too.
- The DB helper fixture now covers:
  - article-prefixed book titles,
  - unpadded multipart tracks (`1`, `2`, `10`),
  - longer audiobook paths that still run on the Windows test helper,
  - stock rows whose genre tags would otherwise leak audiobooks into Music.
- Stock R1 1.6 was confirmed to contain a guarded launch block for
  `/usr/bin/batd -v -s -t5 -o /mnt/sd_0/batlog.txt` in
  `usr/bin/hiby_player.sh`, but the extracted stock rootfs used here does not
  include `/usr/bin/batd`.
- The firmware builder now has an explicit `-DisableBatdLogger` option that
  removes that SD-card logger block and records `batd_logger=disabled` in
  `/etc/r1_audiobook_version`. The verifier has a matching
  `--expect-batd-disabled` check.

Local validation passed with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_local_dev_sanity.ps1
```

Device validation is still needed before releasing these changes.
