# Flash and recovery flow

How firmware packages are structured, how they are applied, and how to flash
and recover the R1 over ADB without losing data. If you build or flash
anything, read this and [brick_lessons_build_categories.md](./brick_lessons_build_categories.md)
first.

## The `.upt` package format

A `.upt` is:

```text
[ ~104 KB zero/header ][ rootfs.squashfs verbatim ]
```

Confirmed by inspection: `upt[104448:]` is byte-identical to the squashfs it
contains. The leading ~104 KB is header/padding; the live payload is the
squashfs. The build produces the squashfs with `mksquashfs -comp lzo -b 131072
-all-root -pf <manifest>` from the extracted stock rootfs plus the audiobook
additions, then prepends the header to form the `.upt`. See
`tools/build_r1_audiobook_firmware.ps1`.

## MTD layout

```text
mtd0  uboot      (bootloader)
mtd1  kernel
mtd2  rootfs     45 MB  (LIVE rootfs — what the device boots from)
mtd3  kernel2
mtd4  rootfs2    (recovery / alternate rootfs)
mtd5  ota        512 KB (OTA MARKER only — NOT a firmware image)
mtd6  userdata
```

**`mtd5` is only 512 KB.** Any `nandwrite` to `mtd5` you see in
`hiby_player`'s strings is writing the OTA *marker*, not the squashfs. The
squashfs flows to `mtd2`/`mtd4` via a different path inside `hiby_player`. Do
not try to manually replicate the raw-NAND write of a full rootfs — that is
brick territory with no recovery until someone physically wakes the device.

## How `hiby_player` flashes (the menu path)

`hiby_player` does the flash itself. Its strings show the path it follows:
`%s/sd_0/%s.upt` → it reads the `.upt` from the SD card, extracts the squashfs,
nandwrites it to the rootfs partition, writes an OTA marker to `mtd5`, calls
`/usr/bin/bootmode.sh Recovery`, and reboots. The bootloader sees the marker,
boots the recovery kernel, which validates and applies the staged package, and
reboots.

## ADB-driven data-preserving flash (the modder's path)

This is the byte-for-byte equivalent of the menu's firmware-update path, but
driven from ADB. It **preserves** `/usr/data` (library.db, resume positions,
bookmarks, BT pairings). Do NOT use `/data/recovery_all` for this — see the
factory-reset trap below.

Full sequence (~90–100 s end to end):

```bash
# 1. Stage + md5-verify the .upt on the SD mount
MSYS_NO_PATHCONV=1 adb -s <serial> push work/.../r1-audiobooks-X.Y.Z.upt \
    /usr/data/mnt/sd_0/r1.upt
adb shell md5sum /usr/data/mnt/sd_0/r1.upt   # confirm it matches

# 2. Write the ota:kernel2 boot-MODE marker to mtd5
MSYS_NO_PATHCONV=1 adb -s <serial> shell /usr/bin/bootmode.sh Recovery

# 3. Reboot into recovery (bootmode.sh does NOT reboot — see gotcha)
adb -s <serial> reboot

# 4. Wait ~90-100 s; ADB returns on the new version. Verify:
adb shell cat /etc/r1_audiobook_version
```

`bootmode.sh Recovery` does `flash_erase /dev/mtd5 0 1` + `nandwrite -s 0 -p
/dev/mtd5 -` to write a 256-byte `ota:kernel2` marker. After `adb reboot`, the
bootloader sees `ota:kernel2`, boots the recovery kernel, which validates +
applies the staged `/usr/data/mnt/sd_0/r1.upt`, and reboots into the new
firmware.

## Gotchas

### `bootmode.sh` does NOT reboot
`/usr/bin/bootmode.sh Recovery` **only writes the marker to `mtd5`**; it does
not reboot. You must run `adb reboot` separately afterward. A first flash
attempt that wrote the marker but never rebooted left the device on the old
version with nothing applied; the second attempt (with an explicit `adb
reboot`) worked. (Earlier notes implied `bootmode.sh` rebooted — it does not.)

### `bootmode.sh` argument matters
- `bootmode.sh Recovery` writes `ota:kernel2` → recovery applies the staged
  `.upt` (firmware update, data-preserving). **Use this.**
- `bootmode.sh` with no arg or `*` writes `ota:kernel` instead — a different
  boot mode, not the firmware-update path.

### DO NOT confuse with `/data/recovery_all` (FACTORY RESET)
`/data/recovery_all` + `S39_recovery.recovery` → `/usr/bin/recovery_all.sh`
is a **factory reset**: `recovery_all.sh` just does `rm -rf /data/*`. It
wipes `/usr/data` — `library.db`, `.audiobook_pos` resume files, BT pairings,
everything. Never use this for a firmware update. Only `bootmode.sh Recovery`
is the data-preserving firmware-update path.

### git-bash mangles unquoted `/usr/...` paths
On Windows git-bash, `adb shell /usr/bin/bootmode.sh Recovery` without
`MSYS_NO_PATHCONV=1` gets path-translated to
`C:/Program Files/Git/usr/bin/bootmode.sh` → a silent no-op (adb finds nothing
to run, the device just reboots normally with no marker written). Always
prefix `MSYS_NO_PATHCONV=1` for `adb shell <abs-path>`. The same applies to
`adb push` destination paths.

### `adb_stage_verified_firmware.ps1` stages stale firmware by default
The repo's staging script defaults to staging the old 1.6.16.6 build — it
does not know the NativeApp pivot output path. For a NativeApp build, `adb
push` the `.upt` to `/usr/data/mnt/sd_0/r1.upt` directly (with
`MSYS_NO_PATHCONV=1`), md5-verify, and back up the prior staged image first
(direct push gives no auto-backup):

```bash
adb shell cp /usr/data/mnt/sd_0/r1.upt /usr/data/mnt/sd_0/r1.upt.prev.bak
```

### The SD mounts at `/usr/data/mnt/sd_0`
NOT `/mnt/sd_0` (which is an empty read-only stub). Stage the `.upt` under
`/usr/data/mnt/sd_0/`, the path `bootmode.sh`/recovery reads from.

## Revert / recovery path

If a flash bricks the device, recover by **SD-card force-flash** to a
known-good `.upt`:

1. Put a known-good `r1.upt` (stock 1.6, or `2.0A` / a prior good release) at
   the SD-card root.
2. Force the device into its SD firmware-update flow (the bootloader's
   recovery path reads the SD root even when the main rootfs is broken).
3. Once it boots the known-good firmware, re-stage and re-flash normally.

This was used to recover from the v2.0.1 and v2.0.2 bricks (back to `2.0A`,
md5 `5153a5a8`). See [brick_lessons_build_categories.md](./brick_lessons_build_categories.md)
for why those bricked. Keep a known-good stock 1.6 `r1.upt` around before
flashing anything experimental.

## Don't manually nandwrite raw

Replicating `hiby_player`'s internal raw-NAND write flow by hand (outside the
`bootmode.sh`/recovery path) is the fastest way to brick the device with no
recovery until someone physically intervenes. The OTA-marker + recovery-kernel
path exists precisely to keep flashes safe. Use it.