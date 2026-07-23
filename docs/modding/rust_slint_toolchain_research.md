# Ingenic Toolchain and Rust/Slint Research

Research date: 2026-07-23

Sources:

- [baijz/ingenic-toolchain](https://gitee.com/baijz/ingenic-toolchain)
- [Slint discussion #12357](https://github.com/slint-ui/slint/discussions/12357)
- [RustAudio/cpal issue #1277](https://github.com/RustAudio/cpal/issues/1277)
- [nanowave-player/nanowave](https://github.com/nanowave-player/nanowave)

## Summary

The Ingenic GCC package is a useful independent compiler, sysroot, binutils,
GDB, and QEMU reference for the R1. A local WSL smoke test confirmed that its
GNU compiler can emit the same fundamental ABI used by this project:

- ELF32
- little-endian MIPS
- MIPS32r2
- o32 ABI
- hard-float, double-precision FP ABI
- dynamic loader `/lib/ld.so.1`

It is therefore suitable for standalone ABI probes and for checking binaries
produced by Zig. It is not yet a reason to replace the tested Zig build path.

Rust and Slint are technically viable on the R1, but the public Nanowave work is
still experimental. Its deployment notes are explicitly non-functional, and
its current LinuxKMS backend brings in fontconfig, eudev, libinput, libevdev,
and related dynamic libraries. Those dependencies and their runtime ownership
of display and input are a poor fit for the R1's limited free RAM and for this
project's in-process stock-player integration.

## Ingenic Toolchain Findings

The mirror contains `mips-gcc520-glibc222`, including:

- Ingenic GCC 5.2.0
- GNU binutils
- GDB
- QEMU user tooling
- glibc 2.22 sysroot variants
- uClibc compiler variants

The repository README is only a placeholder, so the contents and compiler
output are more useful than its documentation.

The validated command prefix is:

```sh
mips-gcc520-glibc222/bin/mips-linux-gnu-
```

The compiler defaults reported:

```text
-mabi=32
-march=mips32r2
-mhard-float
-mdouble-float
```

The target must still be given `-mel` explicitly. A validated minimal build is:

```sh
mips-linux-gnu-gcc -mel -mabi=32 -mhard-float -Os probe.c -o probe
```

The probe and the released `libaudiobook_hook.so` agree on ELF class,
endianness, MIPS32r2, o32, and hard-float ABI. The hook currently requires
symbols no newer than `GLIBC_2.17`, which remains below the toolchain's glibc
2.22 baseline.

### Windows Checkout Warning

Do not run this toolchain directly from a normal Windows checkout. The archive
contains case-colliding Linux headers and Unix symlinks. NTFS/Git for Windows
checks some symlinks out as tiny text files and cannot represent all header
names faithfully.

Clone or extract it onto a case-sensitive Linux filesystem in WSL. When Gitee
network cloning is unreliable, a safe recovery is:

1. Clone the repository on Windows only to obtain its Git object data.
2. Run `git archive` for the desired commit.
3. Extract the tar archive inside WSL, under the Linux filesystem rather than
   `/mnt/c`.

## Slint and Font Handling

Slint discussion #12357 is directly about building for the HiBy R1. It confirms
that a software-rendered Slint application can compile for 32-bit MIPS.

The reported font issue was not proof that every Slint app must scan system
fonts. Fontique enables fontconfig through its `system` feature. Applications
can instead move toward bundled fonts or a custom platform integration. In the
reported R1 experiment, the immediate font failure was ultimately caused by a
wrong font path relative to the process working directory.

For this project, a future Slint experiment should:

- bundle one small, known font;
- avoid system font discovery and cache generation where possible;
- avoid LinuxKMS/libinput if the UI is hosted inside `hiby_player`;
- measure resident memory on the device before adding application features.

## cpal on 32-bit MIPS

cpal issue #1277 affects its PulseAudio backend: that backend imports
`AtomicU64`, which is unavailable on this 32-bit MIPS target. This does not
invalidate Rust audio in general, and it does not affect the current C player,
which uses ALSA and BlueALSA directly.

A Rust experiment should either disable cpal's PulseAudio feature or use direct
ALSA/BlueALSA bindings. Replacing the current tested audio path with
cpal/PulseAudio would add risk without improving audiobook playback.

## Nanowave Assessment

Nanowave provides valuable examples for:

- Buildroot and Docker cross-toolchain setup;
- Rust target configuration;
- Slint software-renderer builds;
- fontconfig packaging;
- R1 framebuffer/KMS and input experimentation.

Its current MIPS configuration targets musl while the R1 stock userspace is
glibc. The linked Slint investigation also records a 32-bit `timerfd_settime`
failure suspected to be an ABI or structure-layout mismatch and a later plan to
return to GNU/glibc.

Do not copy Nanowave's current deployment sequence into this project. It kills
`hiby_player.sh` and `hiby_player` before launching the new application. Our
device testing found that restarting the stock player in-place can fail its
large contiguous audio DMA allocation after memory fragmentation. A clean boot
is the reliable boundary for replacing or preloading the stock process.

## Recommended Path

Keep the production audiobook application in C and retain the current Zig
toolchain for releases. Use these new resources in a separate experimental
track:

1. Add the Ingenic compiler as an independent ABI smoke-test tool.
2. Compile and run tiny GNU/glibc Rust or C probes before attempting a UI.
3. Prototype a minimal Rust framebuffer/input application without audio.
4. Measure binary size, RSS, startup time, and idle stability on the device.
5. If Slint remains attractive, build a custom lightweight platform/backend
   that uses known R1 framebuffer and input paths with bundled fonts.
6. Keep any full standalone-player experiment separate from the production
   LD_PRELOAD firmware until it survives reboot, suspend, SD swap, Bluetooth,
   and overnight-idle testing.

This research improves the project's development options and confidence in its
ABI, but it does not justify changing the current public firmware runtime.
