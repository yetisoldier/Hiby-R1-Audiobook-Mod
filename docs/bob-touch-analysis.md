# Bob — Root Cause Analysis: R1 Touchscreen Failure After Firmware Flash

## Executive Summary

The touch controller IC (CST8xx) is asserting interrupts and populating its I2C data registers, but the kernel driver's threaded IRQ handler is reading the data and discarding it without calling `input_event()`. This is a kernel-level issue — not a userspace problem — meaning the rootfs flash is unlikely to be the direct cause. The most probable root cause is that the touch IC is in a corrupted/confused power state that a warm reboot cannot fix.

## Root Cause Analysis

### Most Likely Root Cause (~55% confidence): Touch IC in Corrupted Power State

The CST8xx touch IC is asserting IRQ and filling its data registers, but the data format or content doesn't match what the driver's `hyn_read_touchdata()` function expects as valid touch data. The driver reads the registers (clearing them), finds no valid touch points, and returns from the handler without calling `input_event()`.

**Why the firmware flash triggered this:**
- The rootfs flash process involves multiple reboots and potentially USB power cycling
- The touch IC's power rail likely stayed energized during warm reboots (common on MIPS embedded platforms — the PMIC doesn't cut power to I2C peripherals on soft reboot)
- A brown-out, voltage fluctuation, or incomplete I2C transaction during a reboot cycle put the IC into an inconsistent internal state
- The IC's firmware/state machine is now outputting data in a format the driver considers "no touch" even when touches occur

**Supporting evidence:**
- The failed software reset (i2cset 0xA5 0x03) that made the IC stop responding entirely — the IC is in a state where it's partially functional but not properly initialized
- The driver is stock HiBy and was NOT changed by the flash — if it worked before, it should work now, unless the IC's state changed
- The high spurious count (81) with unhandled=0 — the IRQs are real (correlated with touches) but the data is invalid

### Secondary Possibility (~20%): Driver Bug Exposed by IC State

The `cst8xx_touch.ko` module has `hyn_ts` branding strings but loads as `cst8xx_touch`. This is a Hynitron driver adapted for the CST8xx family. The chip ID read is 0x11 at register 0xA6. If the driver's `hyn_read_touchdata()` function has a code path that checks a status byte or chip revision and silently skips `input_event()` for unrecognized values, this would produce exactly the observed behavior.

### Tertiary Possibility (~15%): Rootfs Flash Side Effect

Less likely because the issue is kernel-level (no events on the input device node itself). The new `hiby_player.sh` wrapper or launcher could be performing an `EVIOCGRAB` on `/dev/input/event1` — but this would still generate events, just redirect them. Since zero bytes are produced, this is unlikely.

### Remote Possibility (~10%): Threaded IRQ Scheduling Problem

Effectively ruled out — the I2C registers ARE being cleared (which happens in the threaded handler), so the thread IS running.

## Solution Options (Ranked)

### Option 1: Full Power Cycle ⭐ RECOMMENDED FIRST
- **Likelihood:** HIGH (~70%)
- **Effort:** TRIVIAL — physical access only
- **Risk:** NONE

Hold the power button for 10-15 seconds to force a hard power-off (not a soft reboot), wait 30 seconds, power on. This cuts power to the touch IC, forcing its internal state machine to reinitialize from scratch. Warm reboots don't cycle the IC's power rail, so the IC maintains its bad state across reboots.

### Option 2: GPIO Hardware Reset of Touch IC via ADB
- **Likelihood:** MEDIUM-HIGH (~55%)
- **Effort:** LOW — ADB shell commands
- **Risk:** LOW

Toggle the touch IC's reset GPIO (PA17) to force a hardware reset, then reload the driver:
```bash
# Find the GPIO sysfs number for PA17
# Pull reset low (active), wait 100ms, release, wait 300ms
# Then: rmmod cst8xx_touch && insmod /module_driver/cst8xx_touch.ko
```

The GPIO reset pin forces a complete hardware reset of the IC's internal state machine — more thorough than the I2C software reset that failed.

### Option 3: Driver Binary Analysis
- **Likelihood:** MEDIUM (~40% for fixing, HIGH for diagnosis)
- **Effort:** MEDIUM-HIGH — MIPS disassembly
- **Risk:** NONE (read-only)

Disassemble `cst8xx_touch.ko` to find the exact code path where it decides not to call `input_event()`. This tells us whether it's a permanent bug or a state-dependent issue.

### Option 4: I2C Register Dump During Active Touch
- **Likelihood:** MEDIUM (~50% for diagnosis)
- **Effort:** LOW-MEDIUM
- **Risk:** LOW

Read all I2C registers from the touch IC WHILE a finger is pressing the screen. This would reveal whether the IC is outputting valid touch data that the driver is misinterpreting, or garbage data that the driver correctly discards.

### Option 5: Revert to Stock Firmware
- **Likelihood:** HIGH for restoring touch (if it worked before)
- **Effort:** MEDIUM — need to rebuild and reflash
- **Risk:** LOW — stock firmware is known-good

If touch was working before the flash and none of the above fixes it, reflashing stock firmware would confirm whether the issue is firmware-related or hardware-related.

## Recommended Action Plan (When Eric Returns)

1. **Power cycle the R1** (hold power button 10-15 seconds, wait 30s, power on)
2. **Check if touch works** — if yes, we're done
3. **If touch still doesn't work**, try GPIO hardware reset via ADB (Option 2)
4. **If GPIO reset doesn't work**, do an I2C register dump during active touch (Option 4)
5. **If all else fails**, disassemble the touch driver to find the silent skip path (Option 3)

## Key Insight

This is almost certainly NOT caused by our firmware changes. The kernel and touch driver are stock HiBy (Dec 29 2025) and were not modified by the flash. The problem is between the touch IC hardware and the kernel driver — the flash process likely triggered a power state issue in the touch IC that requires a full power cycle to resolve.