HiBy R1 Audiobook Mod v2.0.17
=============================

A focused update for the HiBy R1 on stock firmware 1.6. It installs the
in-process audiobook app (the "Audiobooks" tile) alongside the stock player,
and restores three general device/music feature unlocks that the NativeApp
pivot had dropped. Do not install this on the R1 MIDI.

Install over v2.0.4 (or any v2.0.x). Your library, resume positions,
bookmarks, and Bluetooth pairings are preserved.

About-screen label after install: "HiBy R1 2.0.17".

What's new in 2.0.17
--------------------

Restored stock-feature unlocks (USB DAC, Native DSD, Bluetooth SBC XQ)
- These three unlocks were carried by every pre-2.0 release (v1.5.0 through
  v1.6.3) but were left out of the v2.0.x builds after the NativeApp pivot. The
  tooling was never removed; v2.0.17 simply turns the three build switches back
  on. They are pure stock-resource / shell-config tweaks - no binary, boot,
  PMIC, or mount changes - so the audiobook app and its hook are byte-identical
  to v2.0.16.
- **USB DAC mode**: unlocks the USB-DAC working mode and related Settings flags
  so you can switch System -> USB working mode to DAC and use the R1 as a USB
  DAC. USB-DAC and boot-ADB share the one USB gadget controller and stay
  mutually exclusive by USB working mode (Device = ADB on; DAC = USB audio out,
  ADB off that session), so this is complementary to the boot-ADB shipped since
  v2.0.15, not a conflict.
- **Native DSD**: enables native DSD on the analog output path for the stock
  Music player.
- **Bluetooth SBC XQ**: raises BlueALSA's SBC encoding quality when the
  receiving device supports it. Because the audiobook app drives the BlueALSA
  device directly for Bluetooth output, this also applies to audiobook-over-BT;
  on-device testing confirmed no regression versus v2.0.16's Bluetooth path.

Everything else is unchanged from 2.0.16: SD-primary bookmarks, PNG and
progressive-JPEG covers, Bluetooth A2DP output with AVRCP remote and wired
fallback, SD-primary resume positions, boot ADB, and the storage-full scan
guard.

Install
-------
1. Copy r1-audiobooks-2.0.17.upt to the root of your SD card.
2. On the device, run the firmware update from the SD card and confirm.
3. The device reboots into recovery, applies the package, and reboots into
   2.0.17. The Audiobooks tile launches the audiobook app.

Verify the checksums with MD5SUMS.txt / SHA256SUMS.txt after copying.

Feedback and issues: please report on the GitHub repository.