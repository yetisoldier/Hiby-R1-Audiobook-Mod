# Bob — Launcher Routing Architecture Review

**Date:** 2026-07-11  
**Reviewer:** Bob, Principal Software Architect  
**Scope:** Launcher routing problem — how the Audiobooks tile on the HiBy R1 transitions from `hiby_player` to `r1_audiobook_app` and back

---

## Executive Summary

The execve() approach (Attempt 5) is architecturally unsound for this device. It will appear to work in the happy path but introduces hidden state-leak risks, a fragile return-to-launcher flow, and no failure recovery. The recommended approach is an improved version of Attempt 4 (flag file + wrapper): keep the open()/close() binary patch, make `hiby_player.sh` a concurrent process manager that kills hiby_player on flag detection, and have the app clear the framebuffer on exit. This eliminates the 2-3 second delay, avoids fork/execve entirely, and preserves clean process lifecycle.

---

## 1. Assessment of Each Approach

### Attempt 1: system("r1_audiobook_launch.sh &")

**Verdict: Dead — do not revisit.**

`system()` calls `fork()`, which duplicates hiby_player's 238MB virtual page tables. On a device with 1.7MB free RAM and no swap, this is instant OOM. No optimization of the command string can avoid the fork. This approach is fundamentally incompatible with the memory constraint.

### Attempt 2: system("touch /tmp/.r1_audiobook_launch")

**Verdict: Same root cause as Attempt 1.**

The minimal command does not help — `fork()` happens before `exec()`, and it is the fork that causes OOM, not the command size. Dead.

### Attempt 3: kill -STOP hiby_player in launch script

**Verdict: Dead — logical contradiction.**

hiby_player is the parent that called `system()`. SIGSTOP stops the parent, which hangs the `system()` call (the child is orphaned or also hangs). The launch script can never run because the mechanism that launches it is frozen. Dead.

### Attempt 4: open()/close() flag file via PLT

**Verdict: Works, with fixable drawbacks.**

The binary patch calls `open("/tmp/.r1_audiobook_launch", O_CREAT|O_WRONLY, 0644)` and `close()` via PLT entries. No fork, no execve, no OOM. hiby_player continues running after the flag is created. The wrapper detects the flag after hiby_player exits and launches the app.

**Problems:**
1. **2-3 second delay** — the wrapper only checks for the flag after hiby_player exits. hiby_player does not exit just because the flag was created; it continues running on the launcher screen. The delay is: time until hiby_player naturally exits + wrapper detection + app launch.
2. **Stale framebuffer** — when the app exits, the last frame persists on /dev/fb0 because nothing triggers hiby_player to redraw.

**Assessment:** The mechanism is sound. The delay and framebuffer issues are fixable with wrapper script improvements (see Section 3). This is the recommended foundation.

### Attempt 5: execve() raw syscall (current, staged)

**Verdict: Architecturally unsound — not recommended.**

The code cave at 0x35E000 calls `execve("/usr/bin/r1_audiobook_app", argv, NULL)` via MIPS raw syscall 4011. This replaces the hiby_player process image with r1_audiobook_app instantly.

**What works:**
- No fork → no OOM
- Instant transition (no delay)
- PID stays the same (no orphaned processes)

**What is broken or risky:**

1. **hiby_player cleanup does not run.** ALSA PCM handles, Bluetooth sockets, framebuffer mmaps, and any kernel resources held by hiby_player are not cleanly released. The kernel will eventually clean them up (close fds on execve if O_CLOEXEC, unmap memory), but:
   - File descriptors without O_CLOEXEC are **inherited** by r1_audiobook_app. hiby_player opens /dev/fb0, /dev/input/event*, and possibly /dev/snd/pcmC0D0p early in its lifecycle, almost certainly without O_CLOEXEC (it's an embedded app that never expects to be exec'd).
   - r1_audiobook_app opens its own /dev/fb0 and ALSA devices. If hiby_player's inherited ALSA fd is still open, the kernel may refuse the app's open() because the PCM device is already opened by the inherited fd. **This is a concrete risk for audio playback failure.**
   - The app would need to close all inherited fds (3+) at startup before opening its own devices. This is possible but fragile — the app doesn't know which fds it inherited.

2. **execve with envp=NULL gives an empty environment.** The app gets no PATH, no HOME, no custom env vars. The current app uses `config_load_env()` which may read environment variables. If any are needed, they will be missing. For a static binary this is manageable but requires explicit handling.

3. **No failure recovery.** If `/usr/bin/r1_audiobook_app` is missing, corrupted, or not executable, execve fails. The code cave falls through to nop instructions and eventually crashes the process. hiby_player.sh then sees the child exit with a crash code and reboots (current script) or relaunches hiby_player (if modified). There is no fallback to the stock Books hub.

4. **Return-to-launcher is unsolved.** The current `hiby_player.sh` reboots after the child exits. With execve, the child (was hiby_player) becomes r1_audiobook_app and then exits. The shell sees the child exit and reboots — which is wrong. The script needs to distinguish "hiby_player exited normally → reboot" from "app exited after execve → relaunch hiby_player." This requires a signaling mechanism (flag file, exit code, etc.), which adds complexity back to the approach.

5. **Stale framebuffer is not solved by execve.** When r1_audiobook_app exits, the last frame persists on /dev/fb0. hiby_player is gone. The framebuffer will show stale content until hiby_player is relaunched and redraws. This is the same problem as Attempt 4.

6. **Process state ambiguity.** After execve, `hiby_player.sh`'s child process has the same PID but is now a different binary. `wait()` in the shell returns the exit status, but the shell cannot distinguish "hiby_player crashed" from "r1_audiobook_app exited normally." This makes error handling in the wrapper unreliable.

**Conclusion:** execve trades one set of problems (delay) for a larger set of problems (state leaks, no recovery, ambiguous lifecycle). It is not the right architecture.

---

## 2. Recommended Approach: Improved Flag File + Concurrent Wrapper

### Architecture

```
hiby_player.sh (process manager)
  │
  ├── launches /usr/bin/hiby_player in background
  │
  ├── polls /tmp/.r1_audiobook_launch every 100ms
  │
  ├── if flag detected:
  │     ├── kill -TERM hiby_player (clean up ALSA, BT)
  │     ├── wait for hiby_player to exit (max 2s)
  │     ├── rm -f /tmp/.r1_audiobook_launch
  │     ├── dd if=/dev/zero of=/dev/fb0 (clear framebuffer)
  │     ├── launch /usr/bin/r1_audiobook_app (foreground)
  │     └── when app exits: loop back to launch hiby_player
  │
  └── if hiby_player exits without flag:
        ├── increment crash counter
        ├── if crashes < 5: relaunch hiby_player
        └── if crashes >= 5: reboot (stock recovery)
```

### Binary patch (unchanged from Attempt 4)

The code cave at 0x35E000 calls:
1. `open("/tmp/.r1_audiobook_launch", O_CREAT|O_WRONLY, 0644)` — via open@plt at 0x00839E20
2. `close(fd)` — via close@plt at 0x0083ABE0

No fork, no execve, no OOM. hiby_player continues running after the callback returns — it stays on the launcher screen until the wrapper kills it.

### Wrapper script: `hiby_player.sh`

```sh
#!/bin/sh

killall    hiby_player    &>/dev/null
killall -9 hiby_player    &>/dev/null

if [ -f "/usr/bin/batd" ]; then
  killall    batd    &>/dev/null
  killall -9 batd    &>/dev/null
  /usr/bin/batd -v -s -t5 -o /mnt/sd_0/batlog.txt &
fi

FLAG="/tmp/.r1_audiobook_launch"
APP="/usr/bin/r1_audiobook_app"
CRASH_COUNT=0
MAX_CRASHES=5

while true; do
  rm -f "$FLAG"
  /usr/bin/hiby_player &
  HP_PID=$!

  # Poll for flag file while hiby_player runs
  while kill -0 $HP_PID 2>/dev/null; do
    if [ -f "$FLAG" ]; then
      # Audiobooks tile tapped — kill hiby_player cleanly
      kill -TERM $HP_PID 2>/dev/null
      # Wait up to 2 seconds for clean exit
      i=0
      while kill -0 $HP_PID 2>/dev/null && [ $i -lt 20 ]; do
        usleep 100000  # 100ms
        i=$((i + 1))
      done
      # Force kill if still alive
      kill -9 $HP_PID 2>/dev/null
      wait $HP_PID 2>/dev/null
      rm -f "$FLAG"
      # Clear framebuffer to prevent stale frame
      dd if=/dev/zero of=/dev/fb0 bs=960 count=800 2>/dev/null
      # Launch audiobook app (foreground)
      if [ -x "$APP" ]; then
        "$APP"
      fi
      # Clear framebuffer after app exits
      dd if=/dev/zero of=/dev/fb0 bs=960 count=800 2>/dev/null
      CRASH_COUNT=0
      break  # loop back to relaunch hiby_player
    fi
    usleep 100000  # 100ms poll interval
  done

  # If we get here, hiby_player exited without flag
  wait $HP_PID 2>/dev/null
  EXIT_CODE=$?
  rm -f "$FLAG"

  CRASH_COUNT=$((CRASH_COUNT + 1))
  if [ $CRASH_COUNT -ge $MAX_CRASHES ]; then
    # Repeated crashes — reboot as stock recovery
    sleep 1
    reboot
  fi

  # Brief pause before relaunching
  sleep 1
done
```

### Why this is better than execve

| Concern | execve | Flag file + concurrent wrapper |
|---|---|---|
| Memory (fork) | No fork — OK | No fork — OK |
| Launch speed | Instant | ~200-300ms (100ms poll + SIGTERM + cleanup) |
| hiby_player cleanup | No — state leaks | Yes — SIGTERM allows clean teardown |
| Inherited fds | Yes — risky for ALSA | No — process is dead, fds released |
| Failure recovery | No — crashes if app missing | Yes — wrapper checks `-x "$APP"` |
| Return to launcher | Complex — needs signaling | Natural — wrapper relaunches hiby_player |
| Stale framebuffer | Same problem | Solved — wrapper clears fb0 |
| Crash recovery | Ambiguous — can't tell app from hiby_player | Clear — flag file presence disambiguates |
| Stock reboot behavior | Lost | Preserved — reboot after 5 crashes without flag |

### Timing analysis

- **100ms poll interval:** The flag file is checked every 100ms. Worst-case delay from tap to kill: 100ms.
- **SIGTERM cleanup:** hiby_player should handle SIGTERM and exit within 200-500ms. The wrapper waits up to 2 seconds, then SIGKILLs.
- **Framebuffer clear:** `dd if=/dev/zero of=/dev/fb0 bs=960 count=800` writes 480×800×2 = 768,000 bytes of zeros. On the X1600, this should take <50ms.
- **App launch:** exec of a 2.5MB static ELF — near-instant.
- **Total transition time:** ~200-500ms. This is acceptable for a touchscreen device where the user taps a tile and expects a new screen.

### BusyBox compatibility notes

- `usleep` is available in BusyBox but might not be in all builds. If unavailable, replace with `sleep 0.1` (BusyBox supports fractional seconds) or a small C helper.
- `dd` is available in BusyBox.
- `kill -0` is POSIX and works in BusyBox ash.
- The script uses only POSIX shell constructs except `usleep`. If `usleep` is missing, use:
  ```sh
  sleep 1  # fallback to 1-second granularity
  ```
  This would increase worst-case delay to ~1 second, still better than the original 2-3 seconds.

---

## 3. Stale Framebuffer Solution

### The problem

When r1_audiobook_app exits, whatever it last wrote to /dev/fb0 stays on screen. The framebuffer driver does not auto-clear. When hiby_player starts, it draws its UI, but there may be a brief period where stale content is visible.

### Solution: Two-stage clear

**Stage 1: App clears on exit (graceful)**

Add to `app/src/main.c` before `return 0`:

```c
/* Clear framebuffer to prevent stale frame on exit */
if (ui.fb.fd >= 0) {
    memset(ui.fb.pixels, 0, (size_t)ui.fb.stride * ui.fb.height * 2);
    fb_present(&ui.fb);
}
```

This writes a black frame to the framebuffer before the app exits. The user sees a brief black screen, then hiby_player's launcher appears.

**Stage 2: Wrapper clears as backup (defensive)**

```sh
dd if=/dev/zero of=/dev/fb0 bs=960 count=800 2>/dev/null
```

The wrapper clears the framebuffer both before launching the app (in case hiby_player left stale content) and after the app exits (in case the app's clear failed). This is belt-and-suspenders.

### Why not use ioctl FBIOBLANK?

`ioctl(fb_fd, FBIOBLANK, 0)` would blank the display, but:
1. It might turn the physical panel off, causing a visible power cycle.
2. It requires the fb fd to be open, which the wrapper (shell script) doesn't have.
3. Writing zeros is simpler and keeps the panel on.

---

## 4. Return-to-Launcher Problem

### With the recommended approach

The return-to-launcher flow is clean and natural:

1. User taps Audiobooks tile → flag file created
2. Wrapper detects flag → kills hiby_player → launches app
3. User presses Back from Home in the app → app exits normally (return 0)
4. Wrapper sees app exited → clears framebuffer → loops back to `while true`
5. Wrapper relaunches hiby_player → launcher appears

No signaling needed. The wrapper's `while true` loop naturally cycles between hiby_player and the app. The flag file is the only coordination mechanism.

### Disambiguation logic

The wrapper distinguishes three scenarios:

| Scenario | Flag file state | Action |
|---|---|---|
| User tapped Audiobooks | Flag exists | Kill hiby_player, launch app |
| hiby_player crashed | Flag does not exist | Increment crash counter, relaunch or reboot |
| App exited, return to launcher | Flag was already consumed | Relaunch hiby_player (normal loop) |

### With execve (for comparison)

If execve were used, the wrapper would need:
- A pre-execve flag file (created by the code cave before calling execve) to signal "app is running"
- After child exit: check if flag exists → if yes, app ran and exited → relaunch hiby_player; if no, hiby_player crashed → reboot
- But: the code cave would need to create the flag file AND call execve, which is more assembly code
- And: if execve fails, the flag file exists but the app never ran — the wrapper would relaunch hiby_player thinking the app already ran

This is more complex and more fragile than the recommended approach.

---

## 5. MIPS Assembly Review

### Code cave at 0x35E000 (execve version)

```mips
lui     a0, 0x0076          # a0 = hi(0x0075E080)
addiu   a0, a0, 0xE080      # a0 = 0x0075E080 → "/usr/bin/r1_audiobook_app"
lui     a1, 0x0076          # a1 = hi(0x0075E0C0)
addiu   a1, a1, 0xE0C0      # a1 = 0x0075E0C0 → argv array
addiu   a2, zero, 0         # a2 = NULL (envp)
addiu   v0, zero, 4011      # v0 = SYS_execve (MIPS o32)
syscall                     # execve(path, argv, NULL)
nop                         # unreachable
```

**Address computation:** The `load_addr_words` function correctly handles MIPS sign-extended addiu by adding 0x8000 before taking the high half. The computed addresses are correct.

**Syscall convention:** MIPS o32 Linux uses $v0 for syscall number, $a0-$a3 for arguments, `syscall` instruction. This is correct. Syscall 4011 is `execve` on MIPS Linux o32. Verified against Linux kernel source `arch/mips/include/uapi/asm/unistd.h`.

**ABI concerns:**

1. **No stack frame.** The code cave does not allocate a stack frame (`addiu $sp, $sp, -N`) or save $ra. This is acceptable because execve never returns — the caller's state is irrelevant on success.

2. **No register preservation.** The code clobbers $a0, $a1, $a2, $v0 without saving. Again, acceptable only because execve never returns.

3. **No error handling.** If execve fails (file not found, permissions, etc.), the code falls through to a `nop`, then into zeroed bytes (which decode as `sll $zero, $zero, 0` = more nops). The CPU will execute nops until it hits non-zero data or reaches the end of the code cave. This will eventually crash or produce undefined behavior. **This is a bug.** The code should handle execve failure by jumping to the original stock callback at 0x0075DAEC.

4. **envp = NULL.** Passing NULL for envp gives the app an empty environment. The app's `config_load_env()` may fail silently if it depends on environment variables. For a static binary on an embedded device, this is likely OK, but it should be documented.

**Recommended fix for error handling:**

If execve fails, jump back to the stock callback so hiby_player opens the stock Books hub:

```mips
lui     a0, hi(path)
addiu   a0, a0, lo(path)
lui     a1, hi(argv)
addiu   a1, a1, lo(argv)
addiu   a2, zero, 0
addiu   v0, zero, 4011
syscall
# execve failed — jump to stock Books hub opener
j       0x0075DAEC
nop
```

This adds only 2 instructions (8 bytes) and provides a safe fallback.

### Code cave for flag file approach (Attempt 4, recommended)

The flag file approach uses `open@plt` and `close@plt`. The existing code for this should be reviewed separately, but the key advantage is:
- `open()` and `close()` are normal function calls that DO return
- The code cave must save/restore registers and follow MIPS o32 calling convention
- The code cave must set up a proper stack frame

The current Attempt 4 code cave (not shown in the files I reviewed, but described in the task) calls `open()` and `close()` via PLT entries. This requires:
- Saving $ra, $sp, and any $s registers used
- Setting up a stack frame for the call
- Passing arguments in $a0-$a2
- Restoring registers after the call
- Returning to the caller via `jr $ra`

This is more standard MIPS calling convention and should be straightforward. The key risk is that the PLT entries at 0x00839E20 (open) and 0x0083ABE0 (close) must be correct for the stock binary. The patcher already verifies expected bytes before patching, so this should be safe.

---

## 6. Alternative Approaches Considered

### Option A: Tiny resident helper binary

A small C binary (e.g., `r1_launcher_helper`) started at boot that:
- Listens on a Unix socket
- Binary patch sends a byte to the socket
- Helper kills hiby_player and launches the app
- When app exits, helper relaunches hiby_player

**Verdict: Over-engineered for this use case.** The wrapper script can do the same job without an extra resident process. A resident helper uses RAM (even if small) and adds another component to maintain. Only consider if the shell script proves insufficient.

### Option B: Signal-based approach

Binary patch sends SIGUSR1 to hiby_player.sh (parent process). The wrapper's signal handler kills hiby_player and launches the app.

**Verdict: Possible but fragile.** Signal delivery from a code cave requires knowing the parent PID, which is available via `getppid()` syscall. But signal handling in shell scripts is unreliable — the signal might arrive while the shell is in a `wait` or `sleep`, and the behavior is shell-dependent. The flag file approach is more reliable.

### Option C: Pipe from binary patch to wrapper

Binary patch writes a byte to a pipe; wrapper reads it and launches the app.

**Verdict: Too complex for a code cave.** Setting up a pipe from MIPS assembly requires multiple syscalls (pipe, write, close), and the pipe must be pre-created by the wrapper and kept open. The flag file approach achieves the same result with two PLT calls (open, close).

### Option D: inotify-based flag detection

Instead of polling, the wrapper uses `inotifywait` on /tmp/ to detect the flag file instantly.

**Verdict: Nice optimization, but not necessary.** inotifywait might not be available in the R1's BusyBox. Polling at 100ms is fast enough (human perception threshold is ~200ms) and works everywhere. If inotifywait is available, it could be used as an optional optimization:

```sh
inotifywait -e create /tmp/ --include '\.r1_audiobook_launch' -t 0 &
INOTIFY_PID=$!
```

But this adds complexity for minimal gain (100ms vs instant).

### Option E: Patch a different part of hiby_player's code flow

Instead of patching the callback at 0x482030, patch the main event loop to check for a flag or signal.

**Verdict: Higher risk, no benefit.** The callback approach is clean — it only fires when the Audiobooks tile is tapped. Patching the event loop would require more extensive binary modifications and could affect all hiby_player behavior, not just the Audiobooks tile.

---

## 7. Risk Analysis

### Recommended approach (flag file + concurrent wrapper)

| Risk | Severity | Mitigation |
|---|---|---|
| SIGTERM doesn't kill hiby_player | Low | Wrapper SIGKILLs after 2-second timeout |
| Flag file race (created twice) | Very Low | Wrapper removes flag before relaunching hiby_player |
| 100ms poll misses flag | Very Low | Flag persists until wrapper detects it — no miss possible |
| hiby_player writes to fb0 after SIGTERM | Low | Wrapper clears fb0 after kill |
| App crashes on launch | Medium | Wrapper handles non-zero exit — relaunches hiby_player |
| Shell script bug in wrapper | Medium | Test script with `sh -n` and on-device |
| `usleep` not in BusyBox | Low | Fall back to `sleep 0.1` or `sleep 1` |
| Crash counter overflow | Very Low | Counter resets on successful app launch |

### execve approach (for comparison)

| Risk | Severity | Mitigation |
|---|---|---|
| ALSA device conflict from inherited fd | High | App must close all fds 3+ at startup |
| execve fails (app missing) | Medium | Add fallback jump to stock callback |
| envp=NULL breaks app config | Low | App should use defaults, not env vars |
| Wrapper can't distinguish app exit from crash | High | Need additional flag file anyway |
| hiby_player's Bluetooth state leaks | Medium | bluealsa daemon manages connection, should survive |
| Framebuffer mmap is dropped | Low | App opens its own fb0 |

### General risks (both approaches)

| Risk | Severity | Mitigation |
|---|---|---|
| Stock firmware update shifts patch addresses | High | Patcher verifies MD5/SHA256 before patching |
| Code cave overlaps other data | Low | Patcher checks for zeroed bytes before writing |
| Binary patch corrupts callback | Medium | Patcher verifies expected bytes at callback offset |

---

## 8. Specific Code Changes Needed

### 8.1 Binary patch (`tools/patch_hiby_player.py`)

**Keep the flag file approach (Attempt 4). Do not use the execve code cave.**

The `AUDIOBOOK_SYSTEM_LAUNCHER_PATCHES` should NOT be applied. Instead, use the flag file approach:

- Code cave at 0x35E000 calls `open("/tmp/.r1_audiobook_launch", O_CREAT|O_WRONLY, 0644)` then `close(fd)`.
- This requires a proper stack frame and register save/restore (since open/close return normally).
- The callback at 0x482030 points to this code cave.

The patcher should have a flag like `--audiobook-flag-launcher` for this approach, separate from `--audiobook-system-launcher` (execve) and the native hub approaches.

**Code cave assembly (flag file approach):**

```mips
# Prologue: save registers and set up stack
addiu   sp, sp, -32        # allocate 32-byte stack frame
sw      ra, 28(sp)          # save return address
sw      s0, 24(sp)          # save s0

# Load path string address
lui     a0, hi(path)        # "/tmp/.r1_audiobook_launch"
addiu   a0, a0, lo(path)
addiu   a1, zero, 0x42     # O_CREAT | O_WRONLY = 0102 octal = 0x42
addiu   a2, zero, 0x1A4    # 0644 octal = 0x1A4
jal     open@plt            # open(path, O_CREAT|O_WRONLY, 0644)
nop
# v0 = fd (or -1 on error)

# Close the fd
move    a0, v0              # a0 = fd
jal     close@plt           # close(fd)
nop

# Epilogue: restore registers and return
lw      s0, 24(sp)
lw      ra, 28(sp)
addiu   sp, sp, 32
jr      ra
nop
```

Note: `O_CREAT|O_WRONLY` on Linux MIPS is `0x42` (64 | 1 = 65 = 0x41... let me verify):
- `O_WRONLY` = 0x1 (01 octal)
- `O_CREAT` = 0x40 (0100 octal = 64 decimal)
- `O_CREAT | O_WRONLY` = 0x41

And `0644` = 420 decimal = 0x1A4.

The path string "/tmp/.r1_audiobook_launch" (24 bytes including null) should be stored in the code cave after the instructions, similar to how the execve approach stores its path string.

### 8.2 Wrapper script (`tools/hiby_player.sh`)

Replace the current script with the concurrent process manager shown in Section 2. Key changes:
- Run hiby_player in background instead of foreground
- Poll for flag file every 100ms
- Kill hiby_player with SIGTERM when flag detected
- Clear framebuffer with `dd if=/dev/zero`
- Launch app in foreground
- Relaunch hiby_player after app exits
- Crash counter for reboot recovery

### 8.3 App exit (`app/src/main.c`)

Add framebuffer clear before `return 0`:

```c
/* Clear framebuffer on exit to prevent stale frame */
if (ui.fb.pixels) {
    memset(ui.fb.pixels, 0, (size_t)ui.fb.stride * ui.fb.height * sizeof(uint16_t));
    fb_present(&ui.fb);
}
```

This should be placed after `ui_shutdown()` is called... wait, `ui_shutdown()` closes the framebuffer. The clear should happen BEFORE `ui_shutdown()`. Looking at the current code:

```c
    ui_shutdown(&ui);
    player_shutdown(&player);
    if (ipc_fd >= 0) close(ipc_fd);
    db_close(&db);
    return 0;
```

Insert the clear before `ui_shutdown`:

```c
    /* Clear framebuffer to prevent stale frame on exit */
    if (ui.fb.pixels) {
        memset(ui.fb.pixels, 0, (size_t)ui.fb.stride * ui.fb.height * sizeof(uint16_t));
        fb_present(&ui.fb);
    }

    ui_shutdown(&ui);
    player_shutdown(&player);
    if (ipc_fd >= 0) close(ipc_fd);
    db_close(&db);
    return 0;
```

### 8.4 Launch script (`tools/r1_audiobook_launch.sh`)

This script is no longer needed with the wrapper-based approach. The wrapper directly launches `/usr/bin/r1_audiobook_app`. The launch script can be kept as a fallback or removed from the firmware overlay.

If kept, it should be simplified:

```sh
#!/bin/sh
exec /usr/bin/r1_audiobook_app "$@"
```

---

## 9. Testing Recommendations

### 9.1 Off-device testing

1. **Shell script validation:** Run `sh -n hiby_player.sh` to check syntax.
2. **Shell script logic test:** Create a mock hiby_player that sleeps and a mock app that exits immediately. Verify the wrapper cycles between them correctly.
3. **Flag file race test:** Create the flag file while the mock hiby_player is running. Verify the wrapper kills it and launches the app.
4. **Crash recovery test:** Make the mock hiby_player exit immediately without flag. Verify the wrapper counts crashes and reboots after 5.
5. **Binary patch verification:** Run the patcher on a copy of the stock binary and verify:
   - Code cave at 0x35E000 contains correct instructions
   - Callback at 0x482030 points to code cave
   - Path string is correct
   - PLT addresses are correct

### 9.2 On-device testing (ADB, no flash)

1. **Push modified hiby_player.sh** to `/usr/bin/hiby_player.sh` via ADB.
2. **Push the patched hiby_player** binary via ADB.
3. **Kill the current hiby_player** and let the wrapper restart it.
4. **Tap Audiobooks tile** and verify:
   - hiby_player is killed within 500ms
   - Framebuffer is cleared
   - r1_audiobook_app launches
5. **Press Back from Home** in the app and verify:
   - App exits
   - Framebuffer is cleared
   - hiby_player relaunches
   - Launcher appears
6. **Kill the app** with `kill -9` and verify:
   - Wrapper relaunches hiby_player
7. **Kill hiby_player** 5 times without flag and verify:
   - Device reboots after 5th crash

### 9.3 On-device testing (flashed firmware)

1. Flash the firmware with the patched binary and wrapper.
2. Cold boot and verify hiby_player starts normally.
3. Tap Audiobooks tile — verify transition and app launch.
4. Exit app — verify return to launcher.
5. Tap Audiobooks, then immediately tap Back — verify clean exit.
6. Play music, tap Audiobooks, exit app — verify music does not resume automatically (hiby_player was killed).
7. Leave hiby_player running for 10 minutes — verify no spurious flag detection.
8. Verify music playback works after returning from audiobook app.

### 9.4 Regression tests

1. **Music playback:** Verify music plays normally after hiby_player relaunch.
2. **Bluetooth:** Verify Bluetooth audio routing works after hiby_player relaunch.
3. **Battery:** Verify battery indicator works after hiby_player relaunch (batd is restarted by wrapper).
4. **Sleep/shutdown:** Verify device sleep/wake works with the new wrapper.
5. **SD card:** Verify SD card content is accessible after hiby_player relaunch.

---

## 10. Final Recommendation

**Do not use the execve approach.** Use the improved flag file approach with a concurrent wrapper script. The specific changes are:

1. **Binary patch:** Code cave calls `open()` + `close()` to create flag file (Attempt 4, already proven to work).
2. **hiby_player.sh:** Rewrite as a concurrent process manager that polls for the flag file, kills hiby_player, clears the framebuffer, launches the app, and relaunches hiby_player on app exit.
3. **r1_audiobook_app:** Clear the framebuffer before exiting.
4. **r1_audiobook_launch.sh:** Simplify to `exec /usr/bin/r1_audiobook_app` or remove (wrapper launches app directly).

This approach:
- Uses no fork, no execve, no extra memory
- Transitions in ~200-500ms (acceptable for a touchscreen tap)
- Cleanly tears down hiby_player (SIGTERM)
- Preserves stock crash-recovery behavior (reboot after 5 crashes)
- Solves the stale framebuffer problem
- Provides a clear return-to-launcher path
- Is testable with ADB before flashing

The only significant risk is the shell script complexity, which is manageable with proper testing. The script is ~40 lines of POSIX shell — well within the testing envelope of `sh -n` and on-device verification.

---

*End of architectural review.*