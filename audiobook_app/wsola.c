/* wsola.c — streaming WSOLA time-scale modification. See wsola.h for the
 * algorithm overview and the feed/drain contract. */
#include "wsola.h"

#include <string.h>
#include <math.h>

/* ---- init --------------------------------------------------------------- */

void wsola_init(wsola_t *w, long rate, int channels, int speed_permille) {
    memset(w, 0, sizeof(*w));

    if (channels < 1) channels = 1;
    if (channels > WSOLA_CH_MAX) channels = WSOLA_CH_MAX;
    if (rate < 8000) rate = 8000;
    if (rate > 51200) rate = 51200;
    if (speed_permille < 800) speed_permille = 800;
    if (speed_permille > 2000) speed_permille = 2000;

    w->rate = (int)rate;
    w->ch = channels;
    w->speed = speed_permille;

    /* ~40 ms frame, even, clamped to [16, N_MAX]. */
    int N = (int)(rate * 40 / 1000);
    if (N < 16) N = 16;
    if (N > WSOLA_N_MAX) N = WSOLA_N_MAX;
    N &= ~1;                          /* even so Hs = N/2 is exact */
    w->N = N;
    w->Hs = N / 2;
    w->Lr = N - w->Hs;                /* = N/2 for 50% overlap */
    w->delta = (int)(rate * 15 / 1000);
    if (w->delta > N) w->delta = N;
    w->Ha = w->Hs * speed_permille / 1000;
    if (w->Ha < 1) w->Ha = 1;
    w->need = 2 * w->delta + N;       /* input frames required for one step */

    /* Hann window over N (periodic: w[i] = 0.5 - 0.5*cos(2*pi*i/N)). */
    for (int i = 0; i < N; i++)
        w->win[i] = (float)(0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (double)N));

    w->in_have = 0;
    w->ideal_off = w->delta;          /* start with full look-behind "available" */
    w->have_tail = 0;
}

/* ---- input buffer helpers ----------------------------------------------- */

/* Drop `n` frames from the front of in_buf, adjusting ideal_off and in_have. */
static void in_drop(wsola_t *w, int n) {
    if (n <= 0) return;
    if (n > w->in_have) n = w->in_have;
    int ch = w->ch;
    memmove(w->in_buf, w->in_buf + n * ch, (size_t)(w->in_have - n) * ch * sizeof(short));
    w->in_have -= n;
    w->ideal_off -= n;
}

void wsola_feed(wsola_t *w, const short *in, int frames) {
    if (frames <= 0) return;
    int ch = w->ch;
    int cap = (int)(sizeof(w->in_buf) / sizeof(short) / ch);
    /* Safety net for an overfed buffer. With the caller's sub-chunk feed/drain
     * loop (player.c) this never triggers; if it does, we free room from the
     * front but NEVER eat the `ideal_off` look-behind (that would push the
     * nominal cursor negative and corrupt the search), and only as an absolute
     * last resort truncate the incoming tail. Losing content here means the
     * caller didn't drain between feeds — don't do that. */
    if (w->in_have + frames > cap) {
        int need_room = (w->in_have + frames) - cap;
        int drop = need_room;
        if (drop > w->ideal_off) drop = w->ideal_off;   /* keep the look-behind */
        if (drop > 0) in_drop(w, drop);                 /* ideal_off stays >= 0 */
        need_room -= drop;
        if (need_room > 0) {                            /* still short: truncate tail */
            if (frames > need_room) frames -= need_room;
            else frames = 0;
        }
    }
    int room = cap - w->in_have;
    if (frames > room) frames = room;   /* final guard */
    if (frames <= 0) return;
    memcpy(w->in_buf + w->in_have * ch, in, (size_t)frames * ch * sizeof(short));
    w->in_have += frames;
}

/* ---- one synthesis step (produces Hs frames into out_acc, emits Hs) ------ *
 * Returns 1 if a step was produced, 0 if not enough input. Emits Hs interleaved
 * frames into `out` (caller guarantees out has Hs*ch shorts). */
static int wsola_step(wsola_t *w, short *out) {
    int ch = w->ch;
    int N = w->N, Hs = w->Hs, Lr = w->Lr, delta = w->delta;
    if (w->in_have < w->need) return 0;

    int p0 = w->ideal_off;            /* nominal onset (== delta invariant) */
    int pbest = p0;

    /* Cross-correlation search for the onset that best aligns with the previous
     * frame's tail. Normalized correlation (cosine-like) so loud candidates get
     * no unfair advantage. Skipped on the first step (no reference). */
    if (w->have_tail) {
        int lo = p0 - delta, hi = p0 + delta;
        if (lo < 0) lo = 0;
        if (hi > w->in_have - Lr) hi = w->in_have - Lr;
        double best = -1e300;
        for (int p = lo; p <= hi; p++) {
            const short *a = w->in_buf + p * ch;
            const short *b = w->prev_tail;
            int64_t dot = 0, ea = 0, eb = 0;
            for (int i = 0; i < Lr; i++) {
                int sa = a[i * ch];   /* mono ref; for stereo use ch0 as the
                                       * similarity metric (voice is mono here) */
                int sb = b[i * ch];
                dot += (int64_t)sa * sb;
                ea += (int64_t)sa * sa;
                eb += (int64_t)sb * sb;
            }
            double denom = sqrt((double)ea * (double)eb);
            double c = (denom > 1.0) ? (double)dot / denom
                      : (dot > 0 ? 0.0 : -1.0);
            if (c > best) { best = c; pbest = p; }
        }
    }

    /* Windowed frame -> out_acc (overlap-add). For stereo, window both ch. */
    const short *src = w->in_buf + pbest * ch;
    for (int i = 0; i < N; i++) {
        float wi = w->win[i];
        for (int c = 0; c < ch; c++)
            w->out_acc[i * ch + c] += wi * (float)src[i * ch + c];
    }

    /* Save this frame's last Lr windowed samples as the next reference. */
    for (int i = 0; i < Lr; i++) {
        float wi = w->win[(N - Lr) + i];
        for (int c = 0; c < ch; c++) {
            float v = wi * (float)src[((N - Lr) + i) * ch + c];
            int iv = (int)(v + (v >= 0 ? 0.5f : -0.5f));
            if (iv > 32767) iv = 32767;
            if (iv < -32768) iv = -32768;
            w->prev_tail[i * ch + c] = (short)iv;
        }
    }
    w->have_tail = 1;

    /* Emit the first Hs samples of out_acc, then shift the accumulator left by
     * Hs (carry the upper N-Hs for the next overlap) and zero the freed tail. */
    for (int i = 0; i < Hs; i++) {
        for (int c = 0; c < ch; c++) {
            float v = w->out_acc[i * ch + c];
            int iv = (int)(v + (v >= 0 ? 0.5f : -0.5f));
            if (iv > 32767) iv = 32767;
            if (iv < -32768) iv = -32768;
            out[i * ch + c] = (short)iv;
        }
    }
    memmove(w->out_acc, w->out_acc + Hs * ch, (size_t)(N - Hs) * ch * sizeof(float));
    memset(w->out_acc + (N - Hs) * ch, 0, (size_t)Hs * ch * sizeof(float));

    /* Advance the nominal analysis onset by Ha and drop the consumed frames
     * from the front of in_buf so ideal_off returns to delta (look-behind kept). */
    w->ideal_off += w->Ha;
    int drop = w->ideal_off - w->delta;
    in_drop(w, drop);                 /* ideal_off -= drop -> back to delta */
    return 1;
}

/* ---- drain -------------------------------------------------------------- */

int wsola_drain(wsola_t *w, short *out, int max_frames) {
    int ch = w->ch;
    int Hs = w->Hs;
    int produced = 0;
    while (produced + Hs <= max_frames && w->in_have >= w->need) {
        if (!wsola_step(w, out + produced * ch)) break;
        produced += Hs;
    }
    return produced;
}