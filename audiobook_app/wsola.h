/* wsola.h — streaming WSOLA time-scale modification (pitch-preserving speed).
 *
 * Changes playback tempo WITHOUT changing pitch, unlike linear resampling (which
 * shifts pitch with speed). Used by player.c when speed != 1.0x; at 1.0x the
 * player bypasses this entirely (exact passthrough).
 *
 * WSOLA = Waveform Similarity Overlap-Add: output is built from overlapping
 * windowed input frames. The analysis hop is Ha = Hs*speed (speedup -> Ha > Hs
 * -> fewer output frames per input -> faster), and a cross-correlation search
 * picks the input onset that best phase-aligns with the previous output frame,
 * avoiding the clicks that plain OLA would produce at frame boundaries.
 *
 * Stateful streaming: caller feeds decoded PCM in (wsola_feed) and drains
 * time-stretched PCM out (wsola_drain). One wsola_t lives in g_pl (global BSS,
 * ~48 KB fixed — no malloc, no OOM path). Re-init on track open and on speed
 * change (a speed change changes Ha, so flush + re-prime; ~40 ms latency).
 */
#ifndef AUDIOBOOK_WSOLA_H
#define AUDIOBOOK_WSOLA_H

#include <stdint.h>

/* Fixed max sizes so wsola_t needs no runtime allocation. N_MAX covers sample
 * rates up to ~51 kHz (N = rate*40/1000); CH_MAX = 2 (device content is mono). */
#define WSOLA_N_MAX 2048
#define WSOLA_CH_MAX 2

/* Input ring capacity in FRAMES. Must hold (leftover after a drain, which is <
 * `need` ~ 2*delta+N ≤ ~3600) PLUS one feed sub-chunk (player.c feeds MP3's large
 * 16K-sample decode chunks in SUB=4096 sub-chunks so this buffer never has to
 * hold a whole chunk). 12288 gives comfortable margin for any supported rate. */
#define WSOLA_IN_FRAMES 12288

typedef struct {
    /* Parameters (set by wsola_init, rate-adaptive). */
    int rate;
    int ch;
    int speed;      /* permille, 1000 = 1.0x */
    int N;          /* frame length (samples) */
    int Hs;         /* synthesis hop = N/2 (50% overlap) */
    int Lr;         /* overlap length = N - Hs (= N/2) */
    int delta;      /* search radius (samples, ~15 ms) */
    int Ha;         /* analysis hop = Hs * speed/1000 */
    int need;       /* input frames needed for one step = 2*delta + N */

    float win[WSOLA_N_MAX];                 /* Hann window (N entries) */

    /* Sliding input buffer (interleaved). in_buf[0..in_have*ch) holds samples;
     * ideal_off is the frame index of the next nominal analysis onset within
     * in_buf. After each step we drop consumed frames from the front so ideal_off
     * stays == delta (keeps `delta` look-behind for the search). */
    short in_buf[WSOLA_IN_FRAMES * WSOLA_CH_MAX];
    int in_have;
    int ideal_off;

    /* Overlap-add accumulator (N frames, float to avoid accumulating rounding
     * error). Carries the upper N-Hs samples between steps; emit Hs per step. */
    float out_acc[WSOLA_N_MAX * WSOLA_CH_MAX];

    /* Last Lr windowed samples of the previous added frame — the correlation
     * reference for the next step's onset search. have_tail=0 on first step
     * (no reference -> use the nominal onset, no search). */
    short prev_tail[WSOLA_N_MAX * WSOLA_CH_MAX];
    int have_tail;
} wsola_t;

/* (Re-)initialise for the given rate/channels/speed. Zeros all state, builds the
 * Hann window, computes N/Hs/Lr/delta/Ha from rate. Safe to call mid-stream
 * (flushes ~40 ms; the caller uses this on speed change). */
void wsola_init(wsola_t *w, long rate, int channels, int speed_permille);

/* Append `frames` interleaved S16 samples to the input buffer. */
void wsola_feed(wsola_t *w, const short *in, int frames);

/* Produce time-stretched output into `out` (interleaved), up to `max_frames`.
 * Returns the number of frames written. Stops when there isn't enough input for
 * a full step or `out` is full; the caller feeds more and calls again. */
int wsola_drain(wsola_t *w, short *out, int max_frames);

#endif /* AUDIOBOOK_WSOLA_H */