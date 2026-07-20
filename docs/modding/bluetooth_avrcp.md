# Bluetooth A2DP / AVRCP / SBC XQ

How the audiobook app streams to Bluetooth headphones/speakers over BlueALSA,
the EBUSY-contention root cause and force-take, the hand-back on exit, AVRCP
remote control, and the resume race. Source: [`audiobook_app/player.c`](../../audiobook_app/player.c).

## BlueALSA architecture

The R1 runs BlueZ + BlueALSA. The predefined ALSA device `pcm.bluealsa`
(type plug → type bluealsa, dev `00:..:00` = most-recent, profile `a2dp`) does
automatic rate/format conversion of S16 native-rate output. There is no
`/etc/asound.conf` rerouting the default device, so you must open `bluealsa`
explicitly.

```c
snd_pcm_open(&pcm, "bluealsa", SND_PCM_STREAM_PLAYBACK, 0);
```

## The EBUSY contention root cause

With stock music playing over BT, our `snd_pcm_hw_params` on `bluealsa` gets
`-EBUSY` (-16) → falls back to wired → BT speaker is silent. `aplay -D
bluealsa` while stock music plays reproduces "Unable to install hw params"
(same EBUSY).

**Root cause:** the stock music engine IS `hiby_player` (the same process —
the LD_PRELOAD host; there is no separate music process). It holds the
BlueALSA A2DP sink PCM **exclusively** via the bluealsa ALSA plugin; BlueALSA
allows one client. `hiby_player`'s bluealsa transport socket is fd 38
(`socket:[2573]`), an `AF_UNIX` socket paired to bluealsa (pid 1033) fd 16
(`socket:[2572]`). The hook blocks the stock main thread (`ui_run`) so it
never releases the PCM on its own → EBUSY persists all session. `aplay` with
exact params (22050/2/S16, 500 ms/100 ms buffer/period) WORKS once bluealsa is
free → EBUSY is pure contention, not a constraint rejection.

### Gotcha — PAUSING stock music does NOT free the slot
ALSA keeps the transport open while paused (`hiby_player`'s socket count
stayed 11 playing→paused). Only STOPPING/closing the PCM or force-killing
frees the slot. The user can only PAUSE stock music (no stop button on this
firmware), so "stop music to use BT" is unactionable → **force-take is
necessary.**

### Gotcha — the BT speaker does NOT auto-reconnect
After `bluetoothctl disconnect` (65 s+ still `Connected: no`), reconnect
requires `bluetoothctl connect <addr>` from the device. Stock never stops on
its own across transport loss — it keeps "playing on the device, not over the
speaker"; if BT comes back, stock auto-resumes.

## Force-take the slot — `bt_release_native_hold`

On the first `snd_pcm_hw_params` EBUSY (BT only):

1. Find the bluealsa pid (scan `/proc/*/comm` for `"bluealsa"`).
2. Iterate `/proc/self/fd`; for each `AF_UNIX` socket whose `SO_PEERCRED` peer
   pid == bluealsa pid, `shutdown(SHUT_RDWR)` + `close()` it → signals EOF to
   bluealsa → releases the PCM slot.
3. Retry `open` + `hw_params` (loop 5 attempts with a fresh plug reopen).

`struct ucred` is NOT visible without `_GNU_SOURCE` → declare a local
`{pid_t pid; uid_t uid; gid_t gid;}`. `SO_PEERCRED` on a bluealsa socketpair
end returns the creator pid (bluealsa).

**Risk:** shutting the stock audio thread's fd → it sees `EPIPE`/`EBADF` and
stops (its main thread is blocked, so it won't reopen until we exit
audiobook). If this crashes `hiby_player` = full UI death → revert. Verified
on-device (force-take + auto-resume worked; an earlier freeze was from
adb-driven stuck-state manipulation, NOT from this path).

## Per-track re-detect + output state machine

```c
g_pl.output     // OUT_WIRED=0 / OUT_BT=1
force_wired     // sticky "don't try BT again this track"
bt_fell_back    // 1 = already fell back to wired this track
bt_pcm_path[128]
bt_mixer, bt_mix_elem   // lazy
```

`bt_detect()` runs `popen("bluealsa-cli list-pcms 2>/dev/null")` and picks the
first line containing `"a2dp"` and `"/sink"` (a2dpsrc/sink = playback). It runs
only at track-open / fallback. `setup_alsa`: detect (unless `force_wired`);
open `bluealsa` or fall back to `plughw:0,0`/`hw:0,0`; per-track re-eval
(close+reopen when output changed); BT open fail → wired; BT `hw_params` hard
error → close + `force_wired` + recurse once (bounded). `open_pcm_retry` is a
helper with ~1 s budget.

`bt_mixer_open`/`close`: `snd_mixer_attach("bluealsa")`, first playback-volume
element (name varies per device → generic discovery). `mix_apply` is branched:
BT linear 0=silent..max from `volume_pct`; wired unchanged (0=loud inverted).
BT mixer missing → no-op.

### `pcm_write_or_fallback(buf, frames)`
`writei → recover → retry once`; if the retry is still `< 0` on BT and
`!bt_fell_back`: close the BT pcm, `force_wired`, `setup_alsa`, `mix_apply`,
re-issue. Position unchanged (brief glitch, no jump). Used by ALL 4 decode
write sites. `force_wired`/`bt_fell_back` reset at NEW-track open (after the
re-seek fast path) in both `open_track` and `open_track_aac`, so the next
track re-detects BT. `player_shutdown` closes `bt_mixer`.

## Hand-back on exit — `bt_hand_back_to_stock`

`player_shutdown` MUST call `close_mh()` + `close_pcm()` BEFORE the
`dlclose()`s — `CMD_QUIT` only set `running=0`, so the PCM (wired DAC or
bluealsa slot) was LEAKED → stock music wouldn't play again after exiting
audiobook.

Closing our PCM frees the slot, but stock's fd is already dead and nothing
re-triggers stock to re-open → "playing" UI, no sound, error. FIX: on exit,
if `g_pl.output == OUT_BT`, run a detached
`bluetoothctl disconnect <addr>; sleep 1; bluetoothctl connect <addr>` (addr
parsed from `bt_pcm_path` `/org/bluealsa/hci0/dev_XX_.._XX/...`: after
`/dev/`, then `_`→`:`, 17-char MAC guard). The A2DP disconnect/reconnect makes
BlueALSA recreate the PCM and stock re-grabs it. Brief ~2 s BT blip on
audiobook exit.

### v2.0.15 fix — do NOT inject a pause on hand-back
Stock auto-RESUMES the song across transport loss → "random song auto-plays"
+ BT error flash. Earlier builds tried to inject a pause BEFORE the
disconnect/reconnect. v2.0.12 tried `dup` of `hiby_player`'s READ-ONLY event2
fd → `write()` `EBADF`. v2.0.13 fixed that with a fresh
`open("/dev/input/event2", O_WRONLY/O_RDWR)` (succeeds even while `hiby_player`
`EVIOCGRAB`s it — grab blocks another grab, not an open) and wrote 3×16-byte
`input_event` structs to toggle stock music. **But v2.0.15 removed the
injection entirely:** stock does NOT auto-resume on reconnect, so a bare
disconnect/connect leaves stock **healthy-PAUSED**. The injected
`KEY_PLAYPAUSE` was the only thing starting music. Do not re-add it.

### KEY_PLAYPAUSE byte sequence (48 B, 3×16, 32-bit MIPS LE, timeval=8 B)
```text
press   00*8 01 00 A4 00 01 00 00 00
release 00*8 01 00 A4 00 00 00 00 00
sync    00*16
```
`KEY_PLAYPAUSE = 164`. The `printf '\x...'` escapes are DOUBLED (`\x`) in the C
literal so the string holds a literal backslash-x for busybox `printf` to
turn into bytes (no NUL in the C string). See
[input_keys_hardware.md](./input_keys_hardware.md) for evdev injection.

## AVRCP remote (event4)

A BT speaker registers a virtual input `/dev/input/eventN` named
`"<dev> (AVRCP)"` when connected (Sony SRS-XB43 = event4). It is NOT
`EVIOCGRAB`bed by `hiby_player` (it didn't exist at its startup) → a fresh
`open()` works. `find_avrcp_dev` scans `/sys/class/input/*/device/name` for
`"(AVRCP)"`. `avrcp_open` (dup or fresh, `O_NONBLOCK`) at `open_input` and
retried every 2 s in the event loop (BT can connect mid-session). Added to the
`select` set + read loop.

`getevent -lt` showed the single play/pause button alternates
`KEY_PLAYCD=200` / `KEY_PAUSECD=201` per press → treat both (+ `KEY_PLAYPAUSE=
164` fallback) as `player_toggle`. On read error (link gone) close so we reopen
later. `ui_state_t` gained `int avrcp_fd; uint64_t avrcp_next_open_ms;`.

## Resume race via AVRCP PLAY

User taps book Play (`CMD_PLAY(10, -1)`), but the player thread is in a 20 ms
idle `usleep` and hasn't run `cmd_play` yet → `g_pl.state` still
`PLAYER_STOPPED`. The BT speaker auto-sends AVRCP PLAY when we take the A2DP
slot → `player_toggle` reads STOPPED → `submit_play(g_pl.last_book, 0)` → a
SECOND `CMD_PLAY(10, 0)`. Both run: first resumes @5669 s, second re-seeks @0
→ "reset to beginning." The `0` was the bug: `player_toggle`'s STOPPED branch
passed `0` (beginning) with a comment claiming "resume saved." FIX: pass
`-1` (resume from saved). Now the race is benign.

## SBC XQ unlock

`-EnableBluetoothSbcXq` adds `--sbc-quality=xq` to the BlueALSA launch in
`/usr/bin/bt_init`. This was bundled into the bricked v2.0.1 and dropped from
v2.0.2+; restored in v2.0.17 after verifying in isolation. Because the
audiobook app drives `pcm.bluealsa` directly for BT output, SBC XQ applies to
audiobook-over-BT as well as stock music. See
[brick_lessons_build_categories.md](./brick_lessons_build_categories.md) for
why it must be verified alone before bundling.