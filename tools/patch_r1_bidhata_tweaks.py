#!/usr/bin/env python3
"""Apply community (bidhata/Hiby-R1-Mod) config + init tweaks to an extracted
rootfs tree. These are the firmware-level unlocks/tweaks that do NOT require
swapping hiby_player (PEQ needs the 1.7-beta player and is excluded here because
it breaks the audiobook LD_PRELOAD hook) and do NOT require a code-cave injection
(BT sink is deferred). Everything here is config JSON, shell-script, inittab, or
a new init script -- no hiby_player binary changes (those are in patch_hiby_player.py).

All edits are auditable: each change is recorded and printed. Idempotent where
practical. Run on the squashfs-root tree before mksquashfs.

Reference: https://github.com/bidhata/Hiby-R1-Mod (README + Old_New_Mods docs).
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Any) -> None:
    # Keep the existing 4-space indent style used by HiBy resource JSON.
    path.write_text(json.dumps(data, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")


def _find_section(data: list, type_name: str) -> dict | None:
    for section in data:
        if isinstance(section, dict) and section.get("type") == type_name:
            return section
    return None


def set_config_volume_unlock(rootfs: Path) -> list[str]:
    """config.json: raise headset volume cap 50->100 and disable the EU volume
    warning popup. Affects stock music playback; our audiobook app drives the
    ALSA mixer directly and is unaffected."""
    path = rootfs / "usr" / "resource" / "config.json"
    data = load_json(path)
    vol = _find_section(data, "volume")
    if vol is None:
        return [f"{path}: no 'volume' section, skipped"]
    changes: list[str] = []
    if vol.get("vol_warn_enable") == 1:
        vol["vol_warn_enable"] = 0
        changes.append("config.json: vol_warn_enable 1->0")
    for entry in vol.get("lock_vol", []):
        if isinstance(entry, dict) and entry.get("headset") == 50:
            entry["headset"] = 100
            changes.append("config.json: lock_vol[headset] 50->100")
    write_json(path, data)
    return changes


def set_config_function_flags(rootfs: Path) -> list[str]:
    """config.json fcn0: enable SD-card album-art + music-DB caching, and the
    hidden CUE-explorer / OTG-scan / auto-scroll / speed-play flags."""
    path = rootfs / "usr" / "resource" / "config.json"
    data = load_json(path)
    fcn = _find_section(data, "function")
    if fcn is None:
        return [f"{path}: no 'function' section, skipped"]
    items = fcn.setdefault("fcn0", [])
    # Build a quick lookup so we can update existing entries in place.
    by_key: dict[str, dict] = {}
    for item in items:
        if isinstance(item, dict) and len(item) == 1:
            by_key[next(iter(item))] = item
    changes: list[str] = []
    want = {
        "tf_image_cache_enable": 1,
        "tf_music_db_enable": 1,
        "explorer_in_cue_enable": 1,
        "otg_scan_enable": 1,
        "auto_scroll_playplane": 1,
        "speed_play_enable": 1,
    }
    for key, value in want.items():
        if key in by_key and by_key[key][key] != value:
            by_key[key][key] = value
            changes.append(f"config.json fcn0: {key} -> {value}")
        elif key not in by_key:
            items.append({key: value})
            changes.append(f"config.json fcn0: +{key} = {value}")
    write_json(path, data)
    return changes


def set_set_functions_flags(rootfs: Path) -> list[str]:
    """set_functions.json + midi_set_functions.json: enable dac_charge_disable,
    dac_feedback, standby (USB DAC / usb_mode is handled by the audio-unlock
    patcher's --usb-dac). Skip car_mode / double_touch_wakeup / volume_meter
    (bidhata marks these buggy/not-working)."""
    changes: list[str] = []
    want = {
        "dac_charge_disable": 1,   # USB charging noise reduction
        "dac_feedback": 1,         # USB DAC feedback UI toggle
        "standby": 1,               # expose standby/sleep settings (see Pause-Before-Standby caveat)
    }
    for rel in ("usr/resource/set_functions.json", "usr/resource/midi_set_functions.json"):
        path = rootfs / rel
        if not path.exists():
            continue
        data = load_json(path)
        for section in data:
            if not isinstance(section, dict) or not isinstance(section.get("funs"), list):
                continue
            by_key = {}
            for item in section["funs"]:
                if isinstance(item, dict) and len(item) == 1:
                    by_key[next(iter(item))] = item
            for key, value in want.items():
                if key in by_key and by_key[key][key] != value:
                    by_key[key][key] = value
                    changes.append(f"{rel}: {key} -> {value}")
        write_json(path, data)
    return changes


def patch_mount_ubifs(rootfs: Path) -> list[str]:
    """/usr/bin/mount_ubifs.sh: change `mount -o sync` to `mount -o noatime` on
    the UBIFS user-data mount. Reduces NAND write amplification + speeds up
    settings/saves (bidhata UBIFS_Mount_Optimization.md)."""
    path = rootfs / "usr" / "bin" / "mount_ubifs.sh"
    text = path.read_text(encoding="utf-8")
    changes: list[str] = []
    pattern = re.compile(r"mount -o sync -t ubifs ")
    new_text, n = pattern.subn("mount -o noatime -t ubifs ", text)
    if n:
        path.write_text(new_text, encoding="utf-8")
        changes.append(f"{path}: sync -> noatime ({n} site)")
    return changes


def patch_axp2101(rootfs: Path) -> list[str]:
    """module_driver/axp2101.sh: battery-health + efficiency params on the AXP2101
    PMIC insmod line:
      charge_voltage_limit 4400 -> 4350 (~95% cap, less cell stress)
      charge_term_current 100 -> 50 (slower final trickle, truer 100%)
      dcdc3_always_pwmmode 1 -> 0 (allow PFM under low load, saves standby power)
    """
    path = rootfs / "module_driver" / "axp2101.sh"
    text = path.read_text(encoding="utf-8")
    changes: list[str] = []
    subs = [
        ("charge_voltage_limit=4400", "charge_voltage_limit=4350"),
        ("charge_term_current=100", "charge_term_current=50"),
        ("dcdc3_always_pwmmode=1", "dcdc3_always_pwmmode=0"),
    ]
    for old, new in subs:
        if old in text:
            text = text.replace(old, new, 1)
            changes.append(f"{path}: {old} -> {new}")
    path.write_text(text, encoding="utf-8")
    return changes


def disable_serial_console(rootfs: Path) -> list[str]:
    """/etc/inittab: comment out the root shell on the hardware serial pins
    (console::respawn:-/bin/sh # GENERIC_SERIAL). Frees a tiny bit of memory and
    closes an unused root shell. UART debugging is lost (not relevant for end
    users)."""
    path = rootfs / "etc" / "inittab"
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    changes: list[str] = []
    for i, line in enumerate(lines):
        if line.strip() == "console::respawn:-/bin/sh # GENERIC_SERIAL":
            lines[i] = "#console::respawn:-/bin/sh # GENERIC_SERIAL  (disabled by bidhata tweak)\n"
            changes.append(f"{path}: serial console shell disabled (line {i+1})")
    path.write_text("".join(lines), encoding="utf-8")
    return changes


BIDHATA_TWEAKS_INIT = """#!/bin/sh
# S95bidhata_tweaks -- community performance tweaks (bidhata/Hiby-R1-Mod).
# Runs late in boot. Safe: each tweak is best-effort and silent on failure.

case "$1" in
    start)
        # SD-card read-ahead 128KB -> 2MB: smoother library scans / large FLAC.
        for q in /sys/block/mmcblk*/queue/read_ahead_kb; do
            [ -w "$q" ] && echo 2048 > "$q" 2>/dev/null
        done

        # Keep inode/dentry caches around longer (default 100 -> 50) so UI assets,
        # album art, and the music DB stay cached in RAM.
        sysctl -w vm.vfs_cache_pressure=50 >/dev/null 2>&1
        ;;
    stop)
        ;;
esac

exit 0
"""


def install_tweaks_init(rootfs: Path) -> list[str]:
    """Install /etc/init.d/S95bidhata_tweaks (SD read-ahead + sysctl)."""
    path = rootfs / "etc" / "init.d" / "S95bidhata_tweaks"
    path.write_text(BIDHATA_TWEAKS_INIT, encoding="utf-8")
    try:
        path.chmod(0o755)
    except OSError:
        pass  # Windows filesystem; mksquashfs pseudo file sets the mode
    return [f"{path}: installed (SD read-ahead 2MB + vm.vfs_cache_pressure=50)"]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rootfs", type=Path, help="Extracted rootfs tree (squashfs-root)")
    parser.add_argument(
        "--power-tweaks",
        action="store_true",
        help="Also apply the AXP2101 PMIC battery params + disable the inittab "
        "serial console shell. OFF by default: these touch the power-on / "
        "early-init path and can stop the device from booting if a value is "
        "wrong for this hardware. Only enable once the values have been "
        "verified on-device.",
    )
    args = parser.parse_args()
    rootfs: Path = args.rootfs
    if not rootfs.is_dir():
        raise SystemExit(f"not a directory: {rootfs}")

    fns = [
        set_config_volume_unlock,
        set_config_function_flags,
        patch_mount_ubifs,
        install_tweaks_init,
    ]
    if args.power_tweaks:
        # set_functions (dac_charge_disable/standby) touches the charge path;
        # axp2101 is the PMIC; inittab is early init. All gated behind
        # --power-tweaks because they can affect boot/power-on.
        fns += [set_set_functions_flags, patch_axp2101, disable_serial_console]

    all_changes: list[str] = []
    if args.power_tweaks:
        all_changes.append("NOTE: --power-tweaks enabled (AXP2101 PMIC + inittab) -- boot-path changes")
    for fn in fns:
        try:
            all_changes.extend(fn(rootfs))
        except Exception as exc:  # noqa: BLE001 -- auditable, don't abort the whole build
            all_changes.append(f"!! {fn.__name__}: FAILED ({exc})")

    print("bidhata config/init tweaks:")
    if not all_changes:
        print("  (no changes)")
    for c in all_changes:
        print(f"  {c}")


if __name__ == "__main__":
    main()