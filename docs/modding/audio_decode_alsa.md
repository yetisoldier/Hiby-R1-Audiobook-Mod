# Audio decode and ALSA routing

How the audiobook app decodes MP3 and M4B/AAC and writes PCM to the wired DAC
or Bluetooth A2DP sink, plus the ALSA/DAC gotchas that cost real debugging
time. Bluetooth-specific output flow is in [bluetooth_avrcp.md](./bluetooth_avrcp.md);
this file covers the decode + wired-ALSA side. Source:
[`audiobook_app/player.c`](../../audiobook_app/player.c),
[`audiobook_app/mp4_audio.c`](../../audiobook_app/mp4_audio.c).

## MP3 — HiBy's `libmp3.so` is stock libmpg123 with the readers STUBBED

`/usr/lib/libmp3.so` is a stock libmpg123 build, but HiBy stubbed the file-IO
opener path:

- `mpg123_open` / `mpg123_open_fd` always return `-1` with errcode `0`
  ("No error"). The internal opener `func2` @ `0x14990` is an 8-byte stub:
  `jr $ra; li $v0,-1`.
- `mpg123_format` returns 0 (success) but `mpg123_getformat` still reports the
  file's native rate, and `mpg123_read` outputs the native rate — **format
  resampling is also stripped.** mpg123 cannot resample on this device.

Everything else behaves normally: `mpg123_new/init/getformat/read/seek/length/
tell/delete/strerror`.

**The working path:** open the file yourself with `open(path, O_RDONLY)`,
then `mpg123_replace_reader_handle(mh, read_cb, seek_cb, cleanup_cb)` +
`mpg123_open_handle(mh, (void*)(intptr_t)fd)`. Callback signatures: `off_t`
is 32-bit (no LFS), `int read(void*, void*, size_t)`, `off_t seek(void*, off_t,
int)`, `void cleanup(void*)`. Make `cleanup` a no-op and manage the fd
yourself (close on track switch) to avoid a double-close.

### Gotcha — the dlsym stringify macro
A tidy macro `#define SYM(h,p,t) p=(t)dlsym(h,#p)` silently returns NULL
because `#p` stringifies the *pointer variable name* (`x_mpg123_new`), not the
symbol. Use explicit string args:

```c
#define SYM(h,ptr,sym,t) ptr=(t)dlsym(h,sym)
SYM(lib, x_mpg123_new,  "mpg123_new",  mpg123_handle*(*)(void));
```

## MP3 — libmp3.so garbles 22050 Hz MPEG-2 (LSF) stereo → use minimp3_ex

`libmp3.so` decodes 44100 Hz music fine but garbles 22050 Hz MPEG-2 stereo
audiobook PCM: L channel = full-scale clipping noise (peak 32767, ZCR 0.50),
R = low-freq rumble (~300 Hz), L-R correlation 0.000 — not voice. It also
misreports duration (3.35 h vs the real 6.6 h).

**Fix:** compile `vendor/minimp3_ex.h` + `minimp3.h` (`#define
MINIMP3_IMPLEMENTATION` in `player.c`) directly into the hook `.so`. Use
`mp3dec_ex_open_cb` + read/seek callbacks wired to your own fd.
`mp3d_sample_t = int16_t`, little-endian on MIPS → matches ALSA `S16_LE`.

### Gotcha — `MP3D_SEEK_TO_SAMPLE` builds a 14.5 MB index → OOM-freeze
`mp3dec_ex_seek(MP3D_SEEK_TO_SAMPLE)` builds a full frame index via
`mp3dec_load_index` on the first seek. For a 6.6 h / 193 MB / 56 kbps file
that's ~910K×16 B ≈ 14.5 MB plus realloc-doubling transients. The device has
~56 MB total RAM (~20 MB free, `hiby_player` ~15 MB RSS) → OOMs the system,
adb returns "error: closed", device "freezes."

Use `mp3dec_ex_open_cb` with `MP3D_SEEK_TO_BYTE | MP3D_DO_NOT_SCAN` and seek to
byte offset `= seek_ms * bitrate_kbps / 8`. No index, no scan, instant, ~0
memory, ~26 ms accurate (it syncs forward to the next frame after the byte).
Verified clean: centroid 556 Hz / ZCR 0.06 / L-R corr 0.993 / clip 0 = voice.

### Gotcha — never A/B-compare two paths through the SAME suspect library
Comparing two device decode paths against each other (e.g. sequential vs
seek, both through the broken `libmp3.so`) is worthless — both can be garbage
and "match." This shipped a non-fix firmware (md5 `09a42a3f`). Always compare
against an independent known-good reference decoder (ffmpeg on the host).

## ALSA wired output — `plughw:0,0`, not `hw:0,0`

Card 0 is `hiby-sound-card` driving a CS43131 DAC. `snd_pcm_open` of `hw:0,0`
works and is **free when `hiby_player` is idle in audiobook mode** (because
`hiby_player` is the LD_PRELOAD host — same process, so its `hw:0,0` hold is
ours).

`snd_pcm_set_params` is flaky ("Unable to get period size" / "Rate doesn't
match") — set hw/sw params manually: `hw_params any → RW_INTERLEAVED → S16_LE
→ channels → set_rate_near → set_buffer_time_near(500000) →
set_period_time_near(100000) → hw_params → get_period_size`; sw_params
`current → set_start_threshold → set_avail_min(period) → sw_params → prepare`.

### Gotcha — `start_threshold` MUST be 1
With `start_threshold = period`, the first `snd_pcm_writei` **deadlocks
forever** (the stream never starts, the buffer fills, no avail). Set
`start_threshold = 1` so playback starts on the first write. This is a
hard-to-spot hang because everything else looks correct.

### Gotcha — hardware supports only DISCRETE rates
`{8000, 16000, 32000, 44100, 48000, 88200, 96000}`. NOT 11025/22050/24000.
`set_rate_near` snaps 22050→16000, 24000→32000. Since mpg123 can't resample
(stripped) and minimp3 outputs the native rate, open **`plughw:0,0`** (not
`hw:0,0`) — plughw's rate plugin resamples native→hw and accepts any rate.
`hw:0,0` with a non-supported file rate plays at the wrong speed.

- Use `snd_pcm_drop` (not `drain`) on stop/end — `drain` can hang.
- On `writei < 0`, call `snd_pcm_recover(pcm, err, 1)` then retry once.
- There is **no `/proc/asound` and no
  `/sys/class/sound/pcmC0D0p/sub0/status`** — no procfs/sysfs signal for
  "substream active." Playback verification = the decode loop running at
  real-time (`frames_written ≈ wall_seconds × rate`) with no deadlock.

## CS43131 DAC volume

`amixer -c 0 contents` shows "Left Playback Volume" (numid 1, INTEGER 0..255)
and "Right Playback Volume" (numid 2). No dB TLV. Audio plays audibly at value
0, so **0 = 0 dB = loudest** (CS43131 attenuation convention; 255 ≈ min). Code
has `#define VOL_AT_ZERO_IS_MAX 1` in `player.c`.

### Gotcha — the analog stage rails; there is no software loudness lever
The R1's analog output stage clips when average level is pushed. Source
audiobooks are mastered ~-18 dBFS (RMS 3852); +11 dB drives the amps past rail
→ clipping distortion regardless of which software gain you use (tanh,
brick-wall, parabolic speech-boost all clipped WORSE with more gain). The
only clean path is **no digital gain (passthrough) + max hardware volume.**

### Gotcha — DO NOT change numid=7 (output port) during playback
The Output Port Switch (numid 7, 0-5): **only PORT 0 produces sound**; ports
1-5 are silent (disconnected routings). Changing numid=7 mid-playback drops
the CS43131's I2S lock; returning to 0 does NOT re-lock it (the open PCM
stream never restarts), so the DAC goes silent even though PCM is open +
writing + mixer maxed + unmute. Requires a kernel reboot to restore. Leave
numid=7 at 0.

### Volume step granularity
Stepping `volume_pct` by 10% per press = ~25 mixer codes/press (~12 dB if
0.5 dB/code) = too coarse. Step in RAW MIXER UNITS (`MIX_STEP = 5`, ~51 steps
end-to-end, ~2-2.5 dB/press). The R1 does not reliably emit `ev.value == 2`,
so v2.0.22 treats value 1/0 as hold start/release and generates repeat steps
from the UI timer. Mixer work is queued to the player thread; the UI only shows
an immediate preview percentage. Other keys are press-only so Play/Pause does
not toggle-repeat. See [input_keys_hardware.md](./input_keys_hardware.md).

### Wired and Bluetooth volume are separate

BlueALSA can expose its mixer after the PCM path becomes available. Accepting a
failed first read as a real percentage caused quiet startup, a sudden loud jump
on Volume Down, and a reset after pause/resume. The player now retries the BT
mixer open/read, keeps `wired_volume_pct` and `bt_volume_pct` separately, and
restores the appropriate value when the output changes. All read/write/reopen
operations run on the player thread so they cannot race decode or UI input.

### CS43131 zero-cross ramp
The 1-2 s "sometimes" delay on volume changes is the DAC waiting for a zero
crossing on quiet passages. No zero-cross-disable control is exposed. The
hook-side change is fast; the delay is in the DAC.

## M4B / AAC — dlopen the device's `libfdk-aac.so`

The device ships `/usr/lib/libfdk-aac.so` (v2.0.1, ~1.06 MB). The hook
`dlopen`s it optionally. A self-contained demuxer lives in
[`audiobook_app/mp4_audio.c`](../../audiobook_app/mp4_audio.c) (deliberately
duplicating `tags.c`'s box primitives to avoid touching the working
scan/chapter path). Symbols used (all present via `nm -D`):
`aacDecoder_Open/Close/ConfigRaw/Fill/DecodeFrame/GetStreamInfo`. Try
`libfdk-aac.so` then `.so.2`. Missing → M4B → `fmt_unsupported`, MP3
unaffected. `INT_PCM = int16_t` (16-bit output).

### Gotcha 1 — `TRANSPORT_TYPE` `TT_MP4_RAW = 0`, NOT 2
In `libSYS/include/FDK_audio.h`: `TT_MP4_RAW=0, TT_MP4_ADIF=1, TT_MP4_ADTS=2`.
Using 2 = ADTS mode → the decoder hunts for `0xFFF` sync in raw AAC →
`AAC_DEC_NOT_ENOUGH_BITS (0x1002)` every frame. Call
`aacDecoder_Open(transportType=0, nrOfLayers=1)`.

### Gotcha 2 — `aacDecoder_DecodeFrame` is 4-param in v2.0.1
`(handle, INT_PCM* pTimeData, INT timeDataSize, UINT flags)` — NO
`pBytesConsumed` (older versions had it).

### Gotcha 3 — `CStreamInfo` field order
`aacdecoder_lib.h`: `sampleRate, frameSize, numChannels` (all INT), THEN
`pChannelType`/`pChannelIndices` pointers, then `aacSampleRate/profile/aot/
channelConfig...`. Had `numChannels` and `frameSize` swapped → read
`ch=1024` (=frameSize), `frameSize=2` (=numChannels). Use a prefix struct
`{int sampleRate, frameSize, numChannels; void *p1, *p2;}`. For AAC-LC mono
22050: `sampleRate=22050, frameSize=1024, numChannels=2` (fdk-aac upmixes
mono→stereo by default, `AAC_PCM_MIN_OUTPUT_CHANNELS=2`).

### Gotcha 4 — `stsz` entries are BIG-ENDIAN
The on-demand `stsz` cache `pread`s raw bytes into `uint32_t` and must
`bswap32` each entry before arithmetic. `chunk_off`/`stsz` header are fine
(decoded via `qt_read32` in open) — only the on-demand cache path skipped
endian conversion → got `0x6E010000` instead of `366`.

### Gotcha 5 — Raw transport Fill/Decode flow
`aacDecoder_Fill` copies one AU into the internal buffer (`bytesValid`→0
after); call `aacDecoder_DecodeFrame` ONCE per Fill (do NOT gate on
`bytesValid > 0` — it's 0 after Fill). The FIRST decode of a fresh handle
errors once (`AAC_DEC_UNKNOWN=5` with the `AACDEC_INTR` flag, or
`NOT_ENOUGH_BITS` without) — this is **priming**; skip it and subsequent
frames decode fine. `AACDEC_INTR = 1u` (flag bit).

### Gotcha 6 (shipped bug) — `GetStreamInfo` is invalid after the priming decode
`GetStreamInfo` is only valid after the first SUCCESSFUL decode, NOT after
the priming (intr) decode which ERRORS. A single priming decode →
`GetStreamInfo` right after → "aac streaminfo invalid (rate/ch)" → M4B
refused (MP3 unaffected). FIX: prime in a LOOP (frames 0,1,2,3; intr on the
first; `AACDEC_INTR=1`) reading `GetStreamInfo` after each until rate/ch
populate (frame 1 succeeds). THEN re-open a FRESH handle (Close/Open/
ConfigRaw) for actual decode + `aac_need_intr=1` so the first real
`DecodeFrame` signals a clean restart on a clean bitreservoir. `decode_step`
skips the one priming-error frame (returns, `aac_sample++`, no ALSA write).

### Demux (RAM-frugal)
Chunk-granularity index (`chunk_off` + `chunk_samples` + `chunk_first`, ~256
entries → KB). Per-sample `stsz` is NOT retained: `const_size` fast path
(offset arithmetic), else a per-chunk `stsz` slice `pread` on demand from the
`stsz` body's absolute file offset (recorded before `moov` is freed). `stts`
is kept as a small RLE for ms↔sample seek. `moov` is slurped transiently then
freed. Guards: `moov > 16 MB` → error, `chunks > 100000` → error, `stts`
entries > 8192. Probe RSS ~2 MB (VmHWM 4060 kB). AAC seek =
`mp4_audio_seek_sample` (stts ms→frame) — instant, no per-sample index.

### Raw `.aac` (ADTS) is NOT supported
`mp4_audio` is MP4-container only. Only M4B/M4A are allowed in
`book_is_playable`. HE-AAC/SBR is untested — streaminfo priming should still
work; if an HE-AAC file fails, try more priming frames before output.

## MP4 box constants used

```
moov 0x6d6f6f76  trak 0x7472616b  mdia 0x6d646961  mdhd 0x6d646864
minf 0x6d696e66  stbl 0x7374626c  stsd 0x73747364  stts 0x73747473
stsz 0x7374737a  stsc 0x73747363  stco 0x7374636f  co64 0x636f3634
hdlr 0x68646c72  soun 0x736f756e  mp4a 0x6d703461  enca 0x656e6361
esds 0x65736473
```

`mdhd` timescale at body+12 (v0) or body+20 (v1). `stsd` body = 8-byte prelude
(ver+flags+entry_count) then box-entries; `mp4a` body = 16-byte prelude then
child boxes (`esds` found by scanning the `mp4a` body for the 'esds' fourcc +
validating size). `esds`: ver+flags(4) → ES_Descriptor(0x03, varlen) →
ES_ID(2)+flags(1) [skip dep/URL/OCR per flags bits 7/6/5] →
DecoderConfigDescriptor(0x04, varlen) → skip 13 bytes →
DecoderSpecificInfo(0x05, varlen) → ASC bytes. Varlen descriptor size = up to
4 bytes, each `(byte & 0x7f) << shift`, stop when `!(byte & 0x80)`.

## VBR resume byte math (MP3)

`seek_byte_target()`: if minimp3 set `dec.samples` (VBR/Xing tag — populated
even with `MP3D_DO_NOT_SCAN`; probe confirmed total_samples matched the true
6.6 h), compute `byte = seek_ms * file_size / real_duration_ms` where
`real_duration_ms = dec.samples / channels / rate * 1000` (true avg bitrate).
Falls back to `seek_ms * bitrate_kbps / 8` when `dec.samples == 0` (CBR —
exact for CBR). `fstat(g_pl.track_fd)` for file size. The first-frame bitrate
is a slight underestimate for VBR (56 kbps first vs ~65 avg) → byte-seek lands
a few seconds early; minimp3 still syncs forward to voice, negligible for
resume.

## De-risk FIRST

Before flashing any decode change, run a standalone cross-compiled probe.
Probes used here: `probe_dump.c`, `probe_cmp.c`, `probe_scan.c`,
`probe_seek_byte.c`, `probe_aac.c`, `probe_prog_decode.c`. Build target:

```
zig cc -target mipsel-linux-gnueabihf.2.22 -Os -s -fPIE -pie -lm -ldl \
  -Iaudiobook_app -Ivendor -Ivendor/libjpeg
```

Static is NOT supported for this glibc target. See the meta-lesson in
[README.md](./README.md).
