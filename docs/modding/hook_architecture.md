# Hook architecture: LD_PRELOAD into `hiby_player`

The audiobook app is not a separate process. It is a shared library
(`libaudiobook_hook.so`, ~1.6 MB) that is `LD_PRELOAD`-ed into the stock
launcher process `hiby_player` and runs **in-process**: it hijacks the
launcher's Audiobooks tile callback, draws its own UI into the framebuffer
just before each display pan, and reads touch + hardware keys from the same
process's already-open input fds. Source: [`audiobook_app/hook.c`](../../audiobook_app/hook.c).

This is the foundation everything else builds on. If you want to extend the UI,
change the rendering path, or add a new screen, understand this first.

## Why in-process

`hiby_player` is the launcher AND the stock music engine, and it `EVIOCGRAB`s
the key input devices exclusively in its main thread. A separate audiobook
process cannot read those keys (a fresh `open()` of `/dev/input/eventN` gets
`EBUSY` on the grab). But because the hook is `LD_PRELOAD`-ed into
`hiby_player`, it shares `hiby_player`'s fd table — the already-open grabbed
fds are visible via `/proc/self/fd` and can be `dup()`-ed. See
[input_keys_hardware.md](./input_keys_hardware.md).

Same reasoning for audio: `hiby_player` holds the ALSA `hw:0,0` device, and for
Bluetooth the BlueALSA A2DP sink slot. Sharing the process means we share those
holds. See [audio_decode_alsa.md](./audio_decode_alsa.md) and
[bluetooth_avrcp.md](./bluetooth_avrcp.md).

## Two hooks

### Hook B — the Audiobooks tile trampoline

The stock launcher dispatches tile taps through per-tile callback caves
in the `hiby_player` binary. The Audiobooks tile's cave lives at a fixed
address in the **stock-1.6** `hiby_player`:

```c
#define TILE_CAVE_ADDR     0x0075DAECu  /* code-cave: patched tile callback */
#define TILE_CAVE_PAGE     0x0075D000u
#define TILE_CAVE_PROLOGUE 0x27bdffe8u /* addiu sp,sp,-0x18 — sanity check  */
#define FB_MMAP_PTR_VADDR 0x008b4c14u /* .bss: hiby_player's fb mmap pointer */
```

At hook init, `install_hook_b()`:

1. Reads the first instruction at `TILE_CAVE_ADDR` and asserts it equals
   `TILE_CAVE_PROLOGUE` (`0x27bdffe8`, `addiu sp,sp,-0x18`). This is a
   **version guard**: if the prologue differs, the binary is not stock-1.6 and
   the install aborts with a logged error instead of corrupting random code.
2. Saves the 4 original instructions at the cave.
3. `mprotect(TILE_CAVE_PAGE, 0x2000, RW)` — make the code page writable.
4. Overwrites the cave with a 4-instruction MIPS trampoline
   (`build_trampoline`) that loads `&hook_b` into `$t9` and jumps to it:
   `lui t9, hi; addiu t9, t9, lo; jr t9; nop`.
5. `mprotect(TILE_CAVE_PAGE, 0x2000, RX)` — restore read+execute.
6. `flush_icache()` — see the cacheflush gotcha below.

When the user taps the Audiobooks tile, the stock dispatcher jumps into the
cave and lands in `hook_b()` instead of the stock cave code. `hook_b` sets
`audiobook_mode = 1`, captures `hiby_player`'s framebuffer mmap pointer from
`FB_MMAP_PTR_VADDR` (a `.bss` slot `hiby_player` fills at startup), opens
`/dev/input/event1` for touch, and runs the UI event loop in the main thread.
On exit, it releases touch/key ownership, stops audiobook drawing, and returns
from the callback. The trampoline remains installed for the process lifetime
so the next Audiobooks tap can enter the app again; the saved instructions are
used only by the crash-degradation path.

### Hook A — the `ioctl`/`FBIOPAN_DISPLAY` render hook

The LD_PRELOAD library intercepts libc `ioctl()`. `hiby_player`'s render
thread calls `ioctl(fb_fd, FBIOPAN_DISPLAY, &vscreeninfo)` on every frame
(~30 fps) to pan the framebuffer. Our wrapper checks: if `audiobook_mode` is
set AND the request is `FBIOPAN_DISPLAY`, it draws the current audiobook UI
into the target buffer (using the `yoffset` from `vscreeninfo`) **before**
calling the real `ioctl`. Then the pan proceeds normally.

This is the key insight: **we do not suppress the pan.** An earlier design
(Hook A trampoline) suppressed `hgl_fb_display`'s pan entirely to take over
the screen — but the touch controller needs `hiby_player`'s periodic display
activity to stay alive, so suppressing the pan killed touch. Drawing *before*
the pan keeps HiBy's display loop (and the touch controller) alive while
showing our UI, because we overwrite `hiby_player`'s content right before it
is panned to the panel.

The framebuffer pointer used for drawing is read from the same
`FB_MMAP_PTR_VADDR` `.bss` slot:

```c
uint16_t *fb = *(uint16_t **)FB_MMAP_PTR_VADDR;  /* hiby_player's fb mmap base */
```

`audiobook_mode` is the single flag that toggles the whole takeover. On exit
it goes to 0 and the `ioctl` hook stops drawing.

## Framebuffer handoff on exit

The stock framebuffer is double-buffered (480x1600 virtual, two 480x800
pages). The audiobook pan loop overwrites both pages. Clearing both on exit
left the LCD black until HiBy happened to repaint, often 5-10 seconds later;
cycling the power button forced that repaint.

The current handoff is deterministic:

1. Before entering Audiobooks, capture the page selected by
   `FBIOGET_VSCREENINFO.yoffset` (768,000 bytes).
2. On exit, stop audiobook drawing and copy that launcher snapshot to both
   pages.
3. Pan the original launcher `yoffset` immediately.
4. Start a detached, bounded 1.5-second monitor. It hashes both pages every
   50 ms and pans a page when HiBy redraws it but leaves it hidden.
5. Free the snapshot and handoff context. The worker terminates early if
   Audiobooks is reopened.

The monitor exists only during the callback-to-launcher transition; it is not
an always-running process or thread. Live tests measured about 590 ms for an
idle exit and 760 ms while playback was active, with the first launcher tap
visible immediately.

## Build

```text
zig cc -target mipsel-linux-gnueabihf.2.22 -shared -fPIC \
  -fvisibility=hidden -fno-common -Os -s \
  -I <zig>/lib/libc/include/any-linux-any \
  -o libaudiobook_hook.so hook.c ui.c render.c library.c scan.c tags.c sqlite3.c
```

- **`mipsel-linux-gnueabihf.2.22`** — the `.2.22` pins **glibc 2.22** (the
  stock rootfs ABI). A newer glibc (2.28/2.33) boots into a reset loop — see
  [brick_lessons_build_categories.md](./brick_lessons_build_categories.md).
- **`-fvisibility=hidden -fno-common`** — hide the hook's symbols so they do
  not collide with `hiby_player`'s own symbol table (we are in-process; a
  symbol clash would silently bind our function to `hiby_player`'s or vice
  versa). `-fno-common` avoids tentative-definition merges.
- Static linking is NOT supported for this glibc target; build a shared lib.

## Gotchas

### The cacheflush kernel bug → bare `sync`
Self-modifying code requires an instruction-cache flush after writing the
trampoline. The portable call is `__builtin___clear_cache(addr, addr+len)`,
which issues the `cacheflush` syscall. On this device's kernel that syscall
path is buggy, so `flush_icache()` emits a bare MIPS `sync` instead:

```c
static void flush_icache(void *addr, size_t len) {
    (void)addr; (void)len;
    __asm__ __volatile__("sync" ::: "memory");
}
```

If you add new self-modifying-code paths, keep using `sync` (or verify the
`cacheflush` syscall works on your target before relying on it).

### The hot-swap testing trap (must reflash to test a hook change)
Killing a running `hiby_player` and `LD_PRELOAD`-ing a different hook from
`/data` does **not** give you a clean test. The killed process's
`EVIOCGRAB`/input grabs stay stuck (the kernel does not release them
cleanly on SIGKILL here), so the new `hiby_player` renders the launcher but
touch never fires → the new hook_b never runs → "frozen" (alive, but dead to
input). Two freezes were confirmed this way.

**To test a hook change, bake it into a firmware `.upt` and reflash.** A
fresh boot gives clean input/fb state. See
[flash_and_recovery.md](./flash_and_recovery.md).

### Addresses target stock-1.6 only
`TILE_CAVE_ADDR`, `TILE_CAVE_PROLOGUE`, and `FB_MMAP_PTR_VADDR` are offsets
into the **stock-1.6** `hiby_player` binary. Swapping in a different
`hiby_player` (e.g. a 1.7-beta build needed for PEQ) moves these and the
prologue guard aborts — the hook will not install. This is why PEQ is
excluded from this mod. See
[brick_lessons_build_categories.md](./brick_lessons_build_categories.md).

### `SIGHUP` ignore
The hook ignores `SIGHUP` (`signal(SIGHUP, SIG_IGN)`). `hiby_player`'s
parent receives SIGHUP when its session changes; without the ignore, the
signal would terminate the process and take the audiobook app with it.

## Lightweight screen blank (audio plays dark)

`hiby_player`'s full blank path writes the backlight to 0, then
`FBIOBLANK FB_BLANK_POWERDOWN`, then `echo mem > /sys/power/state` (suspend).
That suspends the device and stops audio.

The hook does a **lightweight** blank instead: write `0` to
`/sys/class/backlight/backlight_pwm0/brightness` only. The screen goes dark,
but panning continues (touch IC stays alive → double-tap wake works) and the
decode thread keeps running → **audiobook plays with the screen off**. Wake
on power press or touchscreen double-tap. Restoring to 50 wakes. No
`FBIOBLANK`, no suspend, ADB survives. This is exactly the control path
`hiby_player` itself uses for the backlight sysfs node.

Stock idle handling can still request `FBIOBLANK` while the audiobook UI owns
the display. A hard blank leaves brightness reporting its previous nonzero
value, so brightness-only state tracking cannot tell that the panel is off.
Audio and hardware controls continue working, but the framebuffer refuses
`FBIOPAN_DISPLAY` with `EBUSY` and the screen may appear permanently black.

The hook prevents that split state in three layers:

1. While audiobook mode is active, stock hard-blank requests are intercepted
   and published to the UI event loop as lightweight blank requests.
2. Every media or volume key performs an idempotent framebuffer unblank before
   its normal action.
3. An `EBUSY` pan is treated as a missed hard blank. The UI unblanks the
   framebuffer, turns only the backlight off, and records the screen as
   blanked. One power press or a double-tap can then wake it normally.

The framebuffer unblank uses a direct `SYS_ioctl` call so it does not recurse
through the preload hook. No monitor thread or extra allocation is involved.
On-device stress testing forced 30 hard blanks in succession; all converted
and woke with one power press while RSS, thread count, and descriptor count
remained unchanged.

## Render coordinates

The R1 framebuffer is 480x800, RGB565, stride 960 bytes (480 px * 2 B):

```c
#define FB_W 480
#define FB_H 800
#define FB_BPP 16
#define FB_STRIDE 960        /* bytes per line */
#define FB_BUF_SIZE (FB_STRIDE * FB_H)   /* 768000 bytes per buffer */
```

`render_blit_rgb565(r, dx, dy, dw, dh, src, sw, sh)` is the nearest-neighbor
scaled blit, clipped to the screen. Now-Playing cover is 220 px centered at
y=84; controls fixed at y=650 (cover ends ~620, ~30 px gap). The detail
"title page" cover is 200 px left with a 1 px border, title/author/duration
in the right column (x=232, w=232). See [`audiobook_app/render.c`](../../audiobook_app/render.c)
and [`audiobook_app/ui.c`](../../audiobook_app/ui.c).

## What this enables / where to go next

- Adding a screen: model it as a `ui_state_t` variant, render in the `ioctl`
  hook path, drive it from the event loop in `hook_b`'s `ui_run`.
- New hardware-key handling: see [input_keys_hardware.md](./input_keys_hardware.md).
- New audio output paths: see [audio_decode_alsa.md](./audio_decode_alsa.md)
  and [bluetooth_avrcp.md](./bluetooth_avrcp.md).
- Changing what ships in rootfs and how to flash it safely: see
  [flash_and_recovery.md](./flash_and_recovery.md) and
  [brick_lessons_build_categories.md](./brick_lessons_build_categories.md).
