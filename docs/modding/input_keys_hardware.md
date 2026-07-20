# Input keys and hardware controls

Why the hardware buttons did nothing inside the app, how the hook reads them
anyway, the key device/code map, evdev injection, volume, and persistent boot
ADB. Source: [`audiobook_app/hook.c`](../../audiobook_app/hook.c),
[`audiobook_app/player.c`](../../audiobook_app/player.c).

## Root cause: "buttons do nothing in the app"

`hiby_player` reads ALL input devices in its MAIN thread and `EVIOCGRAB`s the
key devices exclusively. The hook blocks the main thread inside `ui_run`
(`hook_b` runs in the main thread from the tile callback), so key events queue
unread on `hiby_player`'s own grabbed fds. A fresh `open()` of
`/dev/input/eventN` from the hook gets NOTHING (`EBUSY` grab — a standalone
`EVIOCGRAB` returns `-1`).

**Fix:** we are the SAME process as `hiby_player`, so its already-open
grabbed fds are in our fd table. Scan `/proc/self/fd` readlinks to find
`/dev/input/event0/2/3`, `dup()` the fd (shares the same open file description
= same grab + queued events), set `O_NONBLOCK`, add to the `select()` loop,
drain the queued key events. Works even though we can't `EVIOCGRAB`
(`hiby_player` holds it).

## Key devices and codes

Empirically confirmed via injection (2026-07-17):

```text
/dev/input/event0  md-gpio-keys    KEY_POWER = 116 (short press TOGGLES backlight
                                   50<->0, NO system suspend; adb stays alive)
                                   KEY_NEXT = 163
/dev/input/event1  hyn_ts          touchscreen (the hook EVIOCGRABs this itself)
/dev/input/event2  jz adc keyboard KEY_PLAYPAUSE = 164, KEY_PREVIOUS = 165
/dev/input/event3  earpods_adc     KEY_VOLUMEUP = 115, KEY_VOLUMEDOWN = 114
                                   (also 33/35/36 = F/H/J earpod remote)
```

### Gotcha — KEY_POWER is not in any static capabilities bitmask
`KEY_POWER` (116) is NOT in any device's advertised capabilities/key bitmask
(the vendor kernel doesn't advertise it) but IS emitted at runtime —
confirmed by injecting 116 into event0 and watching the backlight drop to 0.
Don't filter keys by the capabilities bitmask or you'll drop POWER.

## Evdev injection reaches the grab holder

Writing to `/dev/input/eventN` injects into the device stream delivered to
ALL clients including the grabber. A fresh `O_WRONLY`/`O_RDWR` `open()`
SUCCEEDS even while `hiby_player` `EVIOCGRAB`s the device (grab blocks another
grab, not an open). `tools/adb_inject_key_event.py` does this with a 16-byte
MIPS `input_event` struct (`struct <llHHl>`: timeval 8 B + type/2 + code/2 +
value/4, 32-bit MIPS little-endian).

This is how the hook can toggle stock music (hand-back pause) and how ADB
automation can drive the UI. See [bluetooth_avrcp.md](./bluetooth_avrcp.md)
for the `KEY_PLAYPAUSE` sequence and [adb_automation_screenshots.md](./adb_automation_screenshots.md)
for automation.

## Volume persistence and the mixer API

Volume is via dlopen'd libasound:

```c
snd_mixer_open / attach / load / selem_register / first_elem / elem_next
  / selem_get_name / selem_has_playback_volume / selem_set_playback_volume_all
  / selem_get_playback_volume_range
```

Attach `"default"`, find elements named `"Left"`/`"Right"`, set both.
Persist to `/usr/data/.audiobook_volume` (0..100).

- **0 = loudest** on the CS43131 DAC (numid 1/2, 0..255). See
  [audio_decode_alsa.md](./audio_decode_alsa.md).
- Step in raw mixer units (`MIX_STEP = 5`, ~2-2.5 dB/press) — 10% steps are
  too coarse (~12 dB/press).
- Act on `ev.value == 2` (autorepeat) for `KEY_VOLUMEUP`/`KEY_VOLUMEDOWN` so
  hold-to-ramp works; other keys are press-only (`ev.value == 1`).
- The AVRCP device is `/dev/input/eventN` named `"<dev> (AVRCP)"` — see
  [bluetooth_avrcp.md](./bluetooth_avrcp.md).

## Persistent boot ADB via `S90adb` in rootfs

ADB is NOT persistent by default. Persistent boot-ADB needs `/etc/init.d/S90adb`
(0755) in the rootfs; `rcS` runs it at boot → it calls `T90adb start` ONLY when
System → USB working mode == 1 (Device). `T90adb` checks
`/sys/class/android_usb/android0/functions` (ABSENT on this device) →
dispatches to **`S440adb`** (configfs `/sys/kernel/config/usb_gadget/adb_demo`
+ FunctionFS `/dev/usb-ffs/adb` + `adbd`) = exactly what the tap-About session
ADB uses (adbd parent = `/sbin/adbserver.sh 440`). `T90adb` is T-prefixed →
NOT run by `rcS` at boot (`rcS` only runs `S??*`); stock has no `S90adb`, so ADB
is off by default and only our hook's durable call started it. Built via
`-EnableBootAdb`.

### Gotcha — the multiple dead-ends before `S90adb`
- A durable-ADB TOGGLE via a marker file on `/usr/data` (v2.0.7) didn't
  survive reboot because (a) `/usr/data` hit 100% during a scan → `ENOSPC` on
  the 2-byte marker file (v2.0.8 era), AND (b) the SD (`/usr/data/mnt/sd_0`)
  is NOT mounted yet when `hook_init` runs — `hiby_player` (launcher host)
  starts early in boot, before the exFAT SD mounts (v2.0.9–v2.0.11 era).
- v2.0.11 fix: a detached background shell polling
  `[ -f /usr/data/mnt/sd_0/.audiobook_adb_durable ] || [ -f
  /usr/data/audiobook_adb_durable ]` once/sec for up to 20 s, then if found
  runs `sleep 2; ps | grep -q '[a]dbd' || /etc/init.d/T90adb start`.
- v2.0.14 abandoned the toggle entirely for always-on `S90adb` in rootfs.

### Gotcha — ADB gadget sometimes needs a physical USB replug after reboot
to re-enumerate even with the durable marker set.

### USB-DAC mutual exclusion
ADB does NOT block USB-DAC — there is a single USB gadget controller and the
modes are mutually exclusive by design. `S90adb`'s `read_usb_working_mode`
guard skips ADB when mode != 1, so DAC mode works (ADB off that session);
Device mode has ADB. This is why boot-ADB and the USB DAC unlock ship together
cleanly. See [brick_lessons_build_categories.md](./brick_lessons_build_categories.md).