#!/usr/bin/env python3
"""Apply small, auditable HiBy R1 audio feature unlocks to an extracted rootfs."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Any) -> None:
    path.write_text(json.dumps(data, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")


def set_function_flag(data: Any, flag: str, value: int) -> bool:
    changed = False
    if not isinstance(data, list):
        raise ValueError("set_functions JSON root is not a list")
    for section in data:
        if not isinstance(section, dict):
            continue
        funs = section.get("funs")
        if not isinstance(funs, list):
            continue
        for item in funs:
            if isinstance(item, dict) and flag in item and item[flag] != value:
                item[flag] = value
                changed = True
    return changed


def set_config_function_flag(data: Any, flag: str, value: int) -> bool:
    changed = False
    if not isinstance(data, list):
        raise ValueError("config JSON root is not a list")
    for section in data:
        if not isinstance(section, dict) or section.get("type") != "function":
            continue
        fcn0 = section.get("fcn0")
        if not isinstance(fcn0, list):
            continue
        for item in fcn0:
            if isinstance(item, dict) and flag in item and item[flag] != value:
                item[flag] = value
                changed = True
    return changed


def patch_native_dsd(rootfs: Path) -> list[str]:
    path = rootfs / "usr" / "resource" / "ot_devices.json"
    data = load_json(path)
    changed: list[str] = []
    for device in data.get("DEVICES", []):
        if isinstance(device, dict) and device.get("Name") == "analog":
            if device.get("AnalogDsdNative") != "native":
                device["AnalogDsdNative"] = "native"
                changed.append("AnalogDsdNative")
            break
    else:
        raise ValueError("analog device not found in ot_devices.json")
    if changed:
        write_json(path, data)
    return [f"{path}: {', '.join(changed)}"] if changed else []


def patch_usb_dac(rootfs: Path) -> list[str]:
    changed_files: list[str] = []
    function_flags = {
        "usb_mode": 1,
        "dac_feedback": 1,
        "car_mode": 1,
        "standby": 1,
        "about": 1,
    }
    for rel in ("usr/resource/set_functions.json", "usr/resource/midi_set_functions.json"):
        path = rootfs / rel
        data = load_json(path)
        changed_flags = [
            flag for flag, value in function_flags.items() if set_function_flag(data, flag, value)
        ]
        if changed_flags:
            write_json(path, data)
            changed_files.append(f"{path}: {', '.join(changed_flags)}")

    config_path = rootfs / "usr" / "resource" / "config.json"
    config = load_json(config_path)
    if set_config_function_flag(config, "dac_to_store", 1):
        write_json(config_path, config)
        changed_files.append(f"{config_path}: dac_to_store")

    return changed_files


def patch_sbc_xq(rootfs: Path) -> list[str]:
    path = rootfs / "usr" / "bin" / "bt_init"
    text = path.read_text(encoding="utf-8", errors="replace")
    had_cr = "\r" in text
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    old = "/usr/bin/bluealsa -p a2dp-source --a2dp-volume &"
    new = "/usr/bin/bluealsa -p a2dp-source --a2dp-volume --sbc-quality=xq &"
    if new in text:
        if had_cr:
            path.write_text(text, encoding="utf-8", newline="\n")
            return [f"{path}: LF line endings"]
        return []
    if old not in text:
        raise ValueError(f"expected BlueALSA launch command not found in {path}")
    text = text.replace(old, new, 1)
    path.write_text(text, encoding="utf-8", newline="\n")
    return [f"{path}: --sbc-quality=xq"]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("rootfs", type=Path)
    parser.add_argument("--native-dsd", action="store_true")
    parser.add_argument("--usb-dac", action="store_true")
    parser.add_argument("--sbc-xq", action="store_true")
    args = parser.parse_args()

    if not any((args.native_dsd, args.usb_dac, args.sbc_xq)):
        raise SystemExit("No feature unlocks requested")

    rootfs = args.rootfs
    if not (rootfs / "usr").is_dir():
        raise SystemExit(f"Could not find extracted rootfs: {rootfs}")

    changes: list[str] = []
    if args.native_dsd:
        changes.extend(patch_native_dsd(rootfs))
    if args.usb_dac:
        changes.extend(patch_usb_dac(rootfs))
    if args.sbc_xq:
        changes.extend(patch_sbc_xq(rootfs))

    for change in changes:
        print(change)
    if not changes:
        print("audio feature unlocks already applied")


if __name__ == "__main__":
    main()
