# SD Runtime-Power Stability

This note documents the overnight freeze investigated for v2.0.23, the
evidence that separated it from an out-of-memory failure, and the deliberately
scoped mitigation in `audiobook_app/storage_guard.c`.

## Observed failure

The test R1 remained reachable through ADB after the UI stopped responding.
At roughly 13 hours 52 minutes of uptime, the kernel recorded a null-pointer
Oops in `try_to_wake_up` from the `mmcqd/1` worker. The worker exited, and the
main `hiby_player` thread remained in uninterruptible `D` state inside
`__sync_dirty_buffer`. A direct one-sector read from the SD card also hung.

The removable card was a 256 GB exFAT card. Its runtime-power state was
`resuming`, while all three relevant `power/control` files were set to `auto`:

```text
/sys/devices/platform/md_ingenic,mmc.1/power/control
/sys/devices/platform/md_ingenic,mmc.1/mmc_host/mmc1/power/control
/sys/devices/platform/md_ingenic,mmc.1/mmc_host/mmc1/mmc1:0001/power/control
```

The stock kernel and `soc_msc.ko` hashes matched the unmodified 1.6 firmware.
Available memory remained around 18 MB and there was no OOM report. This was an
MMC runtime suspend/resume failure, not evidence that the audiobook UI had
exhausted RAM.

Forensic output from that incident is retained locally under
`artifacts/freeze-20260723-071405/`; it is intentionally not required at
runtime.

## Mitigation

`storage_guard_acquire()` runs immediately before the audiobook UI event loop.
It reads and remembers each control value, writes `on`, and logs how many holds
succeeded. `storage_guard_release()` runs after the UI exits and restores only
values changed by the guard, in card-to-platform order.

The scope matters:

- The SD card is held active only while the audiobook app owns SD-backed media,
  the library catalog, positions, and bookmarks.
- The stock launcher and Music player receive their original power policy back.
- There is no boot daemon, global `pm_async=0`, kernel replacement, or permanent
  SD-card power hold.
- Fixed-size buffers and short procfs/sysfs reads keep the guard's memory cost
  bounded.

`storage_guard_poll()` runs every 30 seconds while the UI is active. It checks
that `mmcqd/1` exists and that the card reports `active`. A diagnostic line is
written only after two consecutive bad checks. The poll does not attempt an
automatic reboot or force a recovery action.

## Resume-write changes

The same release reduces SD metadata pressure:

- Authoritative `.audiobook_pos/<book_id>.pos` checkpoints: every 15 seconds.
- SQLite progress mirror: at most every 60 seconds.
- Pause, stop, completion, and app exit: immediate exact save and DB mirror.
- Identical consecutive state is coalesced.

The sidecar remains authoritative. A stale or busy SQLite mirror can affect a
list percentage temporarily, but it does not lose the actual resume position.

## Testing cautions

Do not hot-reload `hiby_player` to test this hook. During investigation, an
in-place restart failed in the stock `sahd_open` audio driver while requesting
an 8 MB contiguous DMA allocation. Total free RAM was adequate, but physical
memory was fragmented. A clean firmware reboot initialized the same hook
without that failure.

Validate changes by building a `.upt`, flashing it through the normal updater,
and checking:

1. Outside Audiobooks, all three controls return to `auto` and the card can
   become `suspended`.
2. Inside Audiobooks, all three controls are `on` and runtime status is
   `active`.
3. `mmcqd/1` remains present.
4. Pause and app exit update the position and DB immediately.
5. `dmesg` contains no Oops, allocation failure, or I/O error.

The original freeze was intermittent, so a long idle soak remains useful even
after the deterministic lifecycle checks pass.
