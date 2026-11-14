/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 The Nano Freak authors (github.com/Virusiinstaller/nano-freak-fm1) */
/* NANO (Nano Freak): the Freak Synthesizer engine. Integer only, like the rest of the firmware.
 *
 * TYPE 0 is BASIC WAVES, ported from the browser test bed (nano-freak dsp/nf_bw.c, floating point, MIT):
 *   MORPH  0..0.5  a pulse crossfades into a saw; 0.5..1  a second saw (offset by SYM) fades in beside it
 *   SYM    the pulse width (it fades out as the pulse becomes a saw) and the offset of the second saw
 *   SUB    a sine an octave below
 *   SYNC   0 off; up from there a hard-synced slave at 1x..5x the pitch, restarted by the master (band-limited: the restart
 *          step is corrected with a polyBLEP, as the test bed does)
 * TYPE 1 is TWO OP FM (nano-freak dsp/nf_fm.c, Plaits): RATIO (the quantizer), INDEX, FEEDBACK on the same knobs; no oversampling.
 *
 * TYPE 2 is WAVESHAPER (nf_ws.c, Plaits): WAVE (a transfer curve morph), FOLD, SLOPE; tables in eng_nano_tables.h
 *   (tools/gen_nano_tables.py).
 *
 * TYPE 3 is FORMANT (nf_grain.c, Plaits): RATIO, FORMANT, SHAPE.
 *
 * TYPE 4 is NOISE (nf_noise.c, Plaits): MODE, CLOCK, RES.
 *
 * TYPE 5 is CHORDS (nf_chord.c, Plaits): CHORD, INVERSION, WAVE; per-voice state in the part's engine memory.
 *
 * TYPE 6 is MODAL (nf_modal.c, Plaits): STRUCTURE, BRIGHT, DAMP; twelve resonators, four voices.
 *
 * TYPE 7 is SWARM (Braids' SuperWave): WAVE, DETUNE, MIX.
 *
 * then a state variable low-pass (CUT RES KTR, ANALOG's own filter code) so the first builds are playable. The Nano Freak
 * filter, envelopes and modulation matrix replace this later; this filter is the stand-in.
 *
 * The saws and pulse come from dsp.c (osc_saw, a 32-bit phase, polyBLEP in Q15); the pulse is the difference of two saws, a
 * saw one width behind minus the saw: zero mean, up at phase 0 and down at the width, as the test bed's, at full level (not
 * the half level of osc_pulse(), whose edges are the other way round, which matters when it is crossfaded into the saw).
 * State: ph[0] master, ph[1] the synced slave, ph[2] the sub; s[0] s[1] the filter, s[2] the restart correction owed to the
 * next sample. 32-bit multiplies per sample; the 64-bit one is once a block (the slave's increment). */
static const char *const N_NANO_TYPE[] = {"BASIC", "TWO OP FM", "WAVESHAPER", "FORMANT", "NOISE", "CHORDS", "MODAL", "SWARM"};

typedef struct {
    int32_t t, u, rev;           /* MORPH: pulse -> saw (Q15), the second saw's level (Q15), 1 / (1 + u) (Q15) */
    uint32_t pw, off;            /* the pulse width and the second saw's offset (phase, 32 bit) */
    uint32_t hi;                 /* MORPH above the middle: two saws */
} nano_w;

/* one BASIC sample at phase ph with the band-limiting step inc (0: the naive value, no polyBLEP: the sync step uses it) */
static inline int32_t nano_wave(const nano_w *w, uint32_t ph, uint32_t inc)
{
    int32_t a = osc_saw(ph, inc), b;
    if (w->hi) {
        b = osc_saw(ph + w->off, inc);
        return (((a + mulq15(b, w->u)) >> 1) * w->rev) >> 14;
    }
    b = osc_saw(ph - w->pw, inc);
    return (((b - a) >> 1) * (32768 - w->t) + (a >> 1) * w->t) >> 14;     /* the pulse (b - a: up at 0, down at the width) at full level, then the saw */
}

/* TWO OP FM: the test bed's ratio quantizer (nf_lut_fm_frequency_quantizer, Plaits, MIT) as semitones + 12, Q6 (0..3072),
 * indexed by the RATIO knob (0..127; entry 129 is the last); and 2^(k/24) in Q16, k = 0..24 (half semitones over one octave) */
static const uint16_t NANO_FMR[130] = {
    0, 0, 0, 10, 10, 10, 57, 104, 150, 197, 244, 291, 337,
    384, 384, 384, 413, 442, 471, 500, 500, 500, 534, 567, 601, 634,
    668, 701, 735, 768, 768, 768, 778, 778, 778, 825, 872, 918, 965,
    1012, 1059, 1105, 1152, 1152, 1152, 1181, 1210, 1239, 1268, 1268, 1268, 1298,
    1328, 1358, 1388, 1388, 1388, 1425, 1462, 1499, 1536, 1536, 1536, 1546, 1546,
    1546, 1576, 1606, 1636, 1667, 1667, 1667, 1722, 1778, 1833, 1889, 1889, 1889,
    1920, 1920, 1920, 1953, 1985, 1985, 1985, 2036, 2036, 2036, 2090, 2145, 2145,
    2145, 2184, 2224, 2264, 2304, 2304, 2304, 2337, 2369, 2369, 2369, 2398, 2427,
    2457, 2486, 2486, 2486, 2518, 2551, 2551, 2551, 2585, 2620, 2654, 2688, 2688,
    2688, 2736, 2784, 2832, 2880, 2928, 2976, 3024, 3072, 3072, 3072, 3072, 3072
};
static const uint32_t NANO_P2[25] = {
    65536, 67456, 69433, 71468, 73562,
    75717, 77936, 80220, 82570, 84990,
    87480, 90043, 92682, 95398, 98193,
    101070, 104032, 107080, 110218, 113448,
    116772, 120194, 123715, 127341, 131072
};

/* the modulator's increment: inc * 2^((r6 - 768) / 768) (r6: semitones + 12 in Q6 as above); 64-bit multiply once per block */
static uint32_t nano_inc_c(uint32_t inc, uint32_t r6, uint64_t cap)
{
    uint32_t oct = r6 / 768u, rem = r6 % 768u, k = rem / 32u, f = rem % 32u;
    uint32_t a = NANO_P2[k], b = NANO_P2[k + 1], fac = a + (((b - a) * f) >> 5);      /* Q16, 1..2 */
    uint64_t x = ((uint64_t)inc * fac) << oct;                                         /* x 2^oct, still Q16; the table starts at -1 octave */
    x >>= 17;
    return x > cap ? (uint32_t)cap : (uint32_t)x;
}

static uint32_t nano_fm_inc(uint32_t inc, uint32_t r6) { return nano_inc_c(inc, r6, 0x50000000u); }

static uint32_t nano_isqrt(uint32_t x)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x)
        b >>= 2;
    while (b) {
        if (x >= r + b) {
            x -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

/* TWO OP FM (Plaits' two-operator FM, simplified: no oversampling): E1 RATIO (the quantizer's modulator ratio), E2 INDEX
 * (0..2 cycles of phase modulation, squared, tamed as the modulator rises), E3 FEEDBACK (64 none; above: the modulator feeds
 * itself, below: the carrier feeds the modulator's pitch). ph[0] carrier, ph[1] modulator, s[2] the last carrier sample, s[3] the DC blocker. */
static void nano_render_fm(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    tsvf_t flt;
    uint32_t i, pc = v->ph[0], pm = v->ph[1], inc = m->inc, minc = nano_fm_inc(inc, NANO_FMR[p[P_E1] > 129 ? 129 : p[P_E1]]);
    int32_t idx = clamp((p[P_E2] << 8) + m->shape - (64 << 8), 0, 127 << 8), prev = v->s[2], tam, fbp, fbn;
    int32_t ic1 = v->s[0], ic2 = v->s[1], fb = p[P_E3] - 64, amt, dcb = v->s[3];
    int32_t cut = (p[P_E5] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    idx = (idx * 32768 / (127 << 8));                                            /* 0..32768 */
    tam = (int32_t)(0x50000000u - minc) >> 16;                                   /* the modulator above ~ 2.7 kHz: less index, none at the cap */
    tam = tam >= 12288 ? 32768 : (tam * 2731) >> 10;
    tam = (tam * tam) >> 15;
    amt = (((idx * idx) >> 15) * tam) >> 15;                                     /* timbre^2 * taming, Q15 */
    amt = (amt * 512) >> 15;                                                     /* 2 cycles at the top, Q8 */
    fbp = fb > 0 ? ((fb * fb) >> 2) : 0;                                         /* 0.25 (fb/64)^2 cycles, Q12 */
    fbn = fb < 0 ? ((fb * fb) >> 1) : 0;                                         /* 0.5 (fb/64)^2, Q12 */
    tsvf_coef(&flt, cut, p[P_E6]);
    for (i = 0; i < n; i++) {
        uint32_t mi = minc;
        int32_t mo, co, s;
        if (fbn) {                                                       /* the modulator's pitch follows the carrier: x (1 + prev * 0.5 fb^2), 0..1.9 */
            int32_t sc = 4096 + ((prev * fbn) >> 15);
            sc = sc < 0 ? 0 : sc > 7782 ? 7782 : sc;
            mi = (uint32_t)(((uint64_t)minc * (uint32_t)sc) >> 12);
        }
        pm += mi;
        mo = sine_i(pm + ((uint32_t)(prev * fbp) << 5));
        pc += inc;
        co = sine_i(pc + ((uint32_t)(mo * amt) << 9));
        prev += (co - prev) >> 4;
        dcb += (co - dcb) >> 11;                                  /* FM at equal frequencies has a DC term: a 3 Hz blocker */
        co -= dcb;
        s = soft_knee(analog_tap(&flt, 0, co >> 1, &ic1, &ic2, 0), 16000) << 1;
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = pc;
    v->ph[1] = pm;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = prev;
    v->s[3] = dcb;
}

#include "eng_nano_tables.h"

/* WAVESHAPER's taming (Plaits' tame()): how much of the effect is left when the harmonics it makes would pass the Nyquist
 * frequency. f0 Q16 (cycles per sample), h the multiple of it, Q8, order the filter order; result Q15 (cubed). One 32-bit
 * divide per call, once a block. */
static int32_t nano_tame(int32_t f0, int32_t hq8, int32_t order)
{
    int32_t f = (int32_t)(((int64_t)f0 * hq8) >> 8), mx = 32768 / order, den = 32768 - mx, num = f - mx, a;
    if (num <= 0)
        a = 32768;
    else if (num >= den)
        a = 0;
    else
        a = 32768 - (num << 15) / den;
    return (((a * a) >> 15) * a) >> 15;
}

/* WAVESHAPER (Plaits' waveshaper, simplified: the slope oscillator without its band-limiting, the folder interpolated
 * linearly): E1 WAVE (the transfer curve, a morph through five), E2 FOLD (how far into the folder), E3 SLOPE (the slope
 * oscillator's width, a triangle to a saw). ph[0] the slope oscillator, ph[1] and ph[2] the shape and the fold gain the
 * last block ended on (Q15, ramped through the block), s[2] the DC blocker, s[3] -1 on a new note (no ramp from stale values). */
static void nano_render_ws(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const int16_t *tabs[5] = {NANO_WS[0], NANO_WS[1], NANO_WS[2], NANO_WS[3], NANO_WS[4]};
    tsvf_t flt;
    uint32_t i, ph = v->ph[0], inc = m->inc;
    int32_t hq = p[P_E1] * 258, mo = p[P_E3] * 258, tq = clamp((p[P_E2] << 8) + m->shape - (64 << 8), 0, 127 << 8), f0 = (int32_t)(inc >> 16);
    int32_t pq, rup, rdn, slope, sa, att, shape_t, wf_t, shape_m, wf_m, dshape, dwf, ic1 = v->s[0], ic2 = v->s[1], dcb = v->s[2];
    uint32_t pw32;
    int32_t cut = (p[P_E5] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    tq += tq >> 7;                                                      /* 0..127 << 8 -> 0..32767 */
    pq = 16384 + ((mo * 14746) >> 15);                                  /* the width, 0.5 .. 0.95 */
    pw32 = (uint32_t)pq << 17;
    rup = (1 << 29) / pq;                                               /* 1 / width, Q14 */
    rdn = (1 << 25) / (32768 - pq);                                     /* 1 / (1 - width), Q10 */
    slope = 768 + ((((mo > 16384) ? mo - 16384 : 16384 - mo) * 5 * 256) >> 15);          /* 3 + |morph - 0.5| * 5, Q8 */
    sa = ((hq > 16384) ? hq - 16384 : 16384 - hq) * 2;                  /* how far the curve is from the middle, Q15 */
    att = nano_tame(f0, slope, 16);
    shape_t = 16384 + (((hq - 16384) * att) >> 15);
    wf_t = nano_tame(f0, (slope * (768 + ((((sa * att) >> 15) * 5 * 256) >> 15))) >> 8, 12);
    wf_t = 983 + ((((tq * wf_t) >> 15) * 15073) >> 15);                 /* 0.03 + 0.46 * FOLD * taming */
    shape_m = v->s[3] == -1 ? shape_t : (int32_t)v->ph[1];
    wf_m = v->s[3] == -1 ? wf_t : (int32_t)v->ph[2];
    dshape = ((shape_t - shape_m) << 8) / (int32_t)n;                   /* Q8 per sample */
    dwf = ((wf_t - wf_m) << 8) / (int32_t)n;
    shape_m <<= 8;
    wf_m <<= 8;
    tsvf_coef(&flt, cut, p[P_E6]);
    for (i = 0; i < n; i++) {
        int32_t o, pos, wi, fr, x, y, mix, idx, fi, fold, si, sf;
        const int16_t *s1, *s2;
        uint32_t sh;
        shape_m += dshape;
        wf_m += dwf;
        if (ph < pw32) {
            o = (int32_t)(((ph >> 17) * (uint32_t)rup) >> 14);          /* the rising part */
        } else {
            o = 32768 - (int32_t)((((ph - pw32) >> 17) * (uint32_t)rdn) >> 10);
        }
        o = o * 2 - 32768;
        pos = ((o * 127) >> 7) + 32768;
        pos = pos < 0 ? 0 : pos > 65280 ? 65280 : pos;
        wi = pos >> 8;
        fr = pos & 255;
        sh = (uint32_t)(shape_m >> 8) * 131068u;                        /* shape * 3.9999, Q15 */
        si = (int32_t)(sh >> 15);
        si = si > 3 ? 3 : si;
        sf = (int32_t)(sh & 32767u);
        s1 = tabs[si];
        s2 = tabs[si + 1];
        x = s1[wi] + (((s1[wi + 1] - s1[wi]) * fr) >> 8);
        y = s2[wi] + (((s2[wi + 1] - s2[wi]) * fr) >> 8);
        mix = x + (((y - x) * sf) >> 15);
        idx = ((mix * (wf_m >> 8)) >> 15) + 16384;
        idx = idx < 0 ? 0 : idx > 32702 ? 32702 : idx;
        fi = idx >> 6;
        fold = NANO_FOLD[fi] + (((NANO_FOLD[fi + 1] - NANO_FOLD[fi]) * (idx & 63)) >> 6);
        dcb += (fold - dcb) >> 11;
        fold -= dcb;
        fold = soft_knee(analog_tap(&flt, 0, fold >> 1, &ic1, &ic2, 0), 16000) << 1;
        out[i] += voice_amp(fold, m, i) << 1;
        ph += inc;
    }
    v->ph[0] = ph;
    v->ph[1] = (uint32_t)(shape_m >> 8);
    v->ph[2] = (uint32_t)(wf_m >> 8);
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = dcb;
    v->s[3] = 0;
}

/* FORMANT (Plaits' grain formant oscillator): two windowed sine grains per carrier cycle. E1 RATIO (the second formant's
 * ratio to the first, -2 .. +2 octaves, and at the low end a part of the carrier's sine leaks in), E2 FORMANT (the first
 * formant's pitch, note 24 .. 108), E3 SHAPE (the carrier's window). ph[0] the carrier, ph[1] and ph[2] the two formants' phases,
 * s[2] the sample held back one (with its band-limiting correction), s[3] the DC blocker. The window's shape is held for a block. */
typedef struct {
    int32_t mode, g, b16, r1, r2, sc;                 /* the window: mode 0..2 and its constants */
    int32_t bl, rb;                                   /* the leak (Q15) and 1 / (1 + leak) (Q15) */
} nano_g;

static inline int32_t nano_win(const nano_g *w, uint32_t cp)       /* the carrier window at phase cp, Q15, 0 .. 16384 */
{
    uint32_t x = cp >> 16, ph;
    if (w->mode == 0) {
        uint32_t y = (x * (uint32_t)w->g) >> 8;
        ph = 0xC0000000u + ((y > 65536u ? 65536u : y) << 16);
    } else if (w->mode == 1) {
        uint32_t y = (int32_t)x < w->b16 ? (x * (uint32_t)w->r1) >> 8 : 32768u + (((x - (uint32_t)w->b16) * (uint32_t)w->r2) >> 16);
        ph = 0xC0000000u + (y << 16);
    } else {
        uint32_t y = 16384u + ((x * (uint32_t)w->sc) >> 8);
        ph = (y > 49152u ? 49152u : y) << 16;
    }
    return (sine_i(ph) + 32768) >> 2;
}

static inline int32_t nano_grain(const nano_g *w, int32_t win, uint32_t fp)
{
    return (win * (((sine_i(fp) + w->bl) * w->rb) >> 15)) >> 15;
}

static void nano_render_fo(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    nano_g w;
    tsvf_t flt;
    uint32_t i, pc = v->ph[0], pf1 = v->ph[1], pf2 = v->ph[2], inc = m->inc, f1, f2;
    int32_t hq = p[P_E1] * 258, tq = clamp((p[P_E2] << 8) + m->shape - (64 << 8), 0, 127 << 8), mq = p[P_E3] * 258;
    int32_t k, sh, si, sf, t3, bleed, held = v->s[2], dcb = v->s[3], ic1 = v->s[0], ic2 = v->s[1], a;
    int32_t cut = (p[P_E5] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    tq += tq >> 7;                                                       /* 0..32767 */
    f1 = nano_fm_inc(3185015u, (uint32_t)((84 * 64 * tq) >> 15) + 768u);     /* note 24 + 84 * FORMANT: note 24's increment, up */
    if (f1 > 0x40000000u)
        f1 = 0x40000000u;
    f2 = nano_fm_inc(f1 >> 1, (uint32_t)((hq * 3072) >> 15));            /* x 2^((-24 + 48 * RATIO) / 12) */
    if (f2 > 0x40000000u)
        f2 = 0x40000000u;
    bleed = hq < 16384 ? 32768 - 2 * hq : 0;
    bleed = (bleed * (65536 - bleed)) >> 16;                             /* b * (2 - b), Q15 */
    w.bl = bleed;
    w.rb = (int32_t)((1u << 30) / (uint32_t)(32768 + bleed));            /* 1 / (1 + b), Q15 */
    k = 32768 - (int32_t)(inc >> 17) * 24;
    k = k < 0 ? 0 : k;
    sh = 10813 + (((mq - 10813) * k) >> 15);                             /* 0.33 + (SHAPE - 0.33) * k */
    sh = sh * 3;
    si = sh >> 15;
    sf = sh & 32767;
    if (si == 0) {
        t3 = 32768 - sf;
        t3 = (((t3 * t3) >> 15) * t3) >> 15;
        w.mode = 0;
        w.g = 256 + ((t3 * 3840) >> 15);                                 /* 1 + 15 t^3, Q8 */
    } else if (si == 1) {
        t3 = 32768 - sf;
        t3 = (((t3 * t3) >> 15) * t3) >> 15;
        w.mode = 1;
        w.b16 = 66 + ((t3 * 32702) >> 15);
        w.r1 = (1 << 23) / w.b16;                                        /* 0.5 / b, Q8 */
        w.r2 = (int32_t)((1u << 31) / (uint32_t)(65536 - w.b16));        /* 0.5 / (1 - b), Q16 */
    } else {
        t3 = sf;
        t3 = (((t3 * t3) >> 15) * t3) >> 15;
        w.mode = 2;
        w.sc = 128 + ((t3 * 3712) >> 15);                                /* 0.5 + 14.5 t^3, Q8 */
    }
    a = ((int32_t)(inc >> 16) * 1931) >> 10;                             /* the DC blocker's pole: 0.3 f0 */
    a = a > 32768 ? 32768 : a;
    tsvf_coef(&flt, cut, p[P_E6]);
    for (i = 0; i < n; i++) {
        int32_t s = held, d;                                             /* the output lags one sample, as Plaits': the step's first part goes to the sample before the wrap */
        uint32_t pn = pc + inc;
        held = 0;
        if (pn < pc) {                                                   /* the carrier wrapped inside this sample */
            uint32_t f = pn / ((inc >> 16) | 1u), back;                  /* the part of a sample since, Q16 */
            int32_t rt, om, b1, b2, a0;
            f = f > 65535u ? 65535u : f;
            back = 65536u - f;
            rt = (int32_t)(f >> 1);                                      /* Q15 */
            om = 32768 - rt;
            b1 = nano_grain(&w, nano_win(&w, 0xFFFFFFFFu), pf1 + (uint32_t)(((uint64_t)f1 * back) >> 16));
            b2 = nano_grain(&w, nano_win(&w, 0xFFFFFFFFu), pf2 + (uint32_t)(((uint64_t)f2 * back) >> 16));
            a0 = nano_grain(&w, nano_win(&w, 0), 0);
            d = (a0 - b1) + (a0 - b2);
            s += ((d >> 1) * ((rt * rt) >> 15)) >> 15;                   /* polyBLEP of the restart: this sample ... */
            held -= ((d >> 1) * ((om * om) >> 15)) >> 15;                /* ... and the next */
            pf1 = (uint32_t)(((uint64_t)f1 * f) >> 16);                  /* the formants restart with the carrier */
            pf2 = (uint32_t)(((uint64_t)f2 * f) >> 16);
        } else {
            pf1 += f1;
            pf2 += f2;
        }
        held += nano_grain(&w, nano_win(&w, pn), pf1) + nano_grain(&w, nano_win(&w, pn), pf2);
        pc = pn;
        dcb += ((s - dcb) * a) >> 15;
        s -= dcb;
        s = soft_knee(analog_tap(&flt, 0, s >> 1, &ic1, &ic2, 0), 16000) << 1;     /* the grains are hot: as the other types, half into the filter */
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = pc;
    v->ph[1] = pf1;
    v->ph[2] = pf2;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = held;
    v->s[3] = dcb;
}

/* NOISE (Plaits' noise engine, its main output): clocked noise (a new random value at the CLOCK rate, band-limited; above a
 * quarter of the sample rate the raw noise fades in) through a state variable filter that follows the key, from low-pass
 * through band-pass to high-pass. E1 MODE, E2 CLOCK (note 0 .. 128 as a rate), E3 RES (Q 0.5 .. 512, the input is scaled to
 * suit). The filter is the firmware's trapezoidal SVF (dsp.c) with its coefficients held for the block. ph[0] the clock,
 * s[2] the held value, s[3] the sample owed to the next, s[4] s[5] the SVF, s[6] the generator. */
static int32_t nano_cut(int32_t p16) { return clamp(((p16 - 360) * 4785) >> 8, 0, 127 << 8); }

static void nano_render_no(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    tsvf_t nf, flt;
    uint32_t i, pc = v->ph[0], cinc;
    int32_t hq = p[P_E1] * 258, tq = clamp((p[P_E2] << 8) + m->shape - (64 << 8), 0, 127 << 8), mq = p[P_E3] * 258;
    int32_t kq, qq8, oct, frac, fac, gain, hg, bg, lg, amount, sample = v->s[2], nxt = v->s[3], s4 = v->s[4], s5 = v->s[5];
    int32_t rng = v->s[6], ic1 = v->s[0], ic2 = v->s[1], sq;
    int32_t cut = (p[P_E5] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    tq += tq >> 7;                                                       /* 0..32767 */
    cinc = nano_inc_c(3185015u >> 2, (uint32_t)((128 * 64 * tq) >> 15) + 768u, 0xFFFFFFFFull);   /* note 0 up to note 128 * CLOCK */
    amount = ((int32_t)(cinc >> 16) - 16384) * 2;                        /* 4 (f - 0.25), Q15 */
    amount = amount < 0 ? 0 : amount > 32768 ? 32768 : amount;
    oct = (mq * 10) >> 15;                                               /* RES: Q = 0.5 * 2^(10 RES) */
    frac = (((mq * 10) & 32767) * 24) >> 15;                             /* in half semitones: 0..23 */
    {
        uint32_t f = (((uint32_t)((mq * 10) & 32767) * 24u) & 32767u) >> 10;      /* 0..31 between two table points */
        uint32_t a = NANO_P2[frac], b = NANO_P2[frac + 1];
        fac = (int32_t)(a + (((b - a) * f) >> 5));                       /* 2^frac, Q16 */
    }
    kq = (int32_t)(((uint32_t)(8192u >> oct) << 16) / (uint32_t)fac);    /* 1 / Q, Q12 */
    kq = kq < 8 ? 8 : kq;
    qq8 = (int32_t)((((uint32_t)128 << oct) * (uint32_t)fac) >> 16);     /* Q, Q8 */
    sq = (int32_t)nano_isqrt((uint32_t)(((qq8 + 128) * 40 * (int32_t)(m->inc >> 16)) >> 8));     /* sqrt((0.5 + Q) 40 f0), Q8 */
    gain = (int32_t)((1u << 23) / (uint32_t)(sq < 1 ? 1 : sq));          /* 1 / sqrt, Q15 */
    gain = gain > 262144 ? 262144 : gain;
    hg = hq > 16384 ? 32768 - 2 * hq : 0;                                /* -2 mode + 1, only past the middle: <= 0 */
    lg = hq < 16384 ? 32768 - 2 * hq : 0;
    bg = 32768 - 2 * (hq > 16384 ? hq - 16384 : 16384 - hq);
    tsvf_coef_k(&nf, nano_cut(v->pitch16), kq);
    tsvf_coef(&flt, cut, p[P_E6]);
    for (i = 0; i < n; i++) {
        int32_t raw, this_s, c, xin, v3, v1, v2, hp, o, s;
        uint32_t pn = pc + cinc;
        rng ^= rng << 13;
        rng ^= (int32_t)((uint32_t)rng >> 17);
        rng ^= rng << 5;
        raw = (int32_t)((uint32_t)rng >> 16) - 32768;
        this_s = nxt;
        nxt = 0;
        if (pn < pc) {                                                   /* the clock ticked: a new value, with its band-limiting step */
            uint32_t f = pn / ((cinc >> 16) | 1u);
            int32_t rt, om, d = raw - sample;
            f = f > 65535u ? 65535u : f;
            rt = (int32_t)(f >> 1);
            om = 32768 - rt;
            this_s += ((d >> 1) * ((rt * rt) >> 15)) >> 15;
            nxt -= ((d >> 1) * ((om * om) >> 15)) >> 15;
            sample = raw;
        }
        nxt += sample;
        c = this_s + (((raw - this_s) * amount) >> 15);
        pc = pn;
        xin = (c * (gain >> 4)) >> 11;
        xin = clamp(xin, -(1 << 18), 1 << 18);
        v3 = xin - s5;
        v1 = (nf.a1 * s4 + nf.a2 * v3) >> 13;
        v2 = s5 + ((nf.a2 * s4 + nf.a3 * v3) >> 13);
        s4 = clamp(2 * v1 - s4, -150000, 150000);
        s5 = clamp(2 * v2 - s5, -150000, 150000);
        hp = xin - ((kq * v1) >> 12) - v2;
        o = ((hp >> 3) * hg + (v1 >> 3) * bg + (v2 >> 3) * lg) >> 12;
        o = clamp(o, -49152, 49152);                                     /* a resonant peak: kept inside the voice's range */
        s = soft_knee(analog_tap(&flt, 0, o >> 1, &ic1, &ic2, 0), 16000) << 1;
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = pc;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = sample;
    v->s[3] = nxt;
    v->s[4] = s4;
    v->s[5] = s5;
    v->s[6] = rng;
}

/* CHORDS (Plaits' chord engine): five voices play a chord from one key. E1 CHORD (eleven chords), E2 INVERSION (rotates the
 * chord through five voices and octaves), E3 WAVE (the string machine's registrations, then a 15-wave table). Each voice
 * has two oscillators crossfaded by E3: the divide-down string machine (four saws an octave apart, polyBLEP, gains from
 * the registration, shifted up the keyboard) and the wavetable one (128-point integrated waves, differentiated and
 * low-passed). The state is a slot per voice in the part's engine memory (engines.c eng_mem, as PHYS's); the parameters
 * are held for the block, their amplitudes ramped. */
#define NANO_CH_V 5
typedef struct {
    uint32_t pdd[NANO_CH_V], pwt[NANO_CH_V];            /* the two oscillators' phases */
    int32_t lp[NANO_CH_V], dlp[NANO_CH_V], dprev[NANO_CH_V];   /* the wavetable's differentiator and low-pass */
    int32_t add[NANO_CH_V], awt[NANO_CH_V];             /* the amplitudes the last block ended on, Q15 */
    int32_t morph, timbre, chord;                       /* smoothed WAVE and INVERSION (Q15), the chord now */
} nano_ch_t;
#define NANO_MODES 12
#define NANO_MODAL_POLY 4                                /* MODAL: the voices that sound (a bank of resonators each) */
typedef struct {
    int32_t s1[NANO_MODES], s2[NANO_MODES];             /* the resonators */
    int32_t ex1, ex2, hlp, rng;                         /* the excitation's filter, the smoothed STRUCTURE, the dust */
} nano_mo_t;
typedef union {
    nano_ch_t ch;
    nano_mo_t mo;
    uint32_t sw[10];                                    /* SWARM's nano_sw_t (below) */
} nano_su_t;
#define NANO_SLOTS NVOICE
ENG_MEM_FITS("NANO", sizeof(nano_su_t) * NANO_SLOTS);

static nano_su_t *nano_slot(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < NANO_SLOTS ? (nano_su_t *)ENG_MEM(t - trk) + i : 0;
}

static const uint16_t NANO_CH_FADE[NANO_CH_V] = {18022, 15401, 16056, 16712, 17367};      /* 0.55 0.47 0.49 0.51 0.53, Q15 */

static __attribute__((noinline)) void nano_render_ch(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    nano_su_t *U = nano_slot(t, v);
    nano_ch_t *S = U ? &U->ch : 0;
    int32_t acc[CTL];
    int32_t hq = p[P_E1] * 258, tq = clamp((p[P_E2] << 8) + m->shape - (64 << 8), 0, 127 << 8), mq = p[P_E3] * 258;
    int32_t harm[8], ratio_ok[NANO_CH_V] = {0, 0, 0, 0, 0}, amps[NANO_CH_V] = {0, 0, 0, 0, 0}, vq, ii, ifr, rot, nrot, i, j, r, ri, rf, wq, wi, wf;
    uint32_t ratios[NANO_CH_V] = {0, 0, 0, 0, 0}, inc0;
    tsvf_t flt;
    int32_t ic1 = v->s[0], ic2 = v->s[1];
    int32_t cut = (p[P_E5] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    if (!S || n > CTL)
        return;
    tq += tq >> 7;
    S->morph += ((mq - S->morph) * 3277) >> 15;                           /* 0.1 per block */
    S->timbre += ((tq - S->timbre) * 3277) >> 15;
    vq = hq * 11 + ((hq * 7209) >> 15) - 16384;                           /* the chord, with a little hysteresis: x 1.02 x 11 - 0.5 */
    vq += (vq > S->chord * 32768) ? -2458 : 2458;
    vq = (vq + 16384) >> 15;
    S->chord = vq < 0 ? 0 : vq > 10 ? 10 : vq;
    r = 32768 - ((S->morph * 70451) >> 15);                               /* the registration: 1 - 2.15 WAVE, over the first half */
    r = r < 0 ? 0 : r;
    r = r * 7 - (r >> 10);
    ri = r >> 15;
    rf = r & 32767;
    for (i = 0; i < 6; i++)
        harm[i] = NANO_REG[ri][i] + (((NANO_REG[ri + 1][i] - NANO_REG[ri][i]) * rf) >> 15);
    harm[6] = harm[7] = 0;
    wq = ((S->morph - 17531) * 70451) >> 15;                              /* the wave: (WAVE - 0.535) 2.15 of the table */
    wq = wq < 0 ? 0 : wq > 32767 ? 32767 : wq;
    wq *= 14;
    wi = wq >> 15;
    wf = wq & 32767;
    if (wi > 13) {
        wi = 13;
        wf = 32767;
    }
    vq = S->timbre * 20;                                                  /* the inversion */
    ii = vq >> 15;
    ifr = vq & 32767;
    nrot = ii / 4;
    rot = ii % 4;
    for (i = 0; i < 4; i++) {
        int32_t e = (3 + ii - i) / 4, tg = (i - nrot + NANO_CH_V) % NANO_CH_V, pv = (tg + NANO_CH_V - 1) % NANO_CH_V;
        uint32_t b = NANO_CHB[S->chord][i], rr = e >= 2 ? b << (e - 2) : b >> (2 - e);
        if (i == rot) {
            ratios[tg] = rr;
            ratios[pv] = rr * 2;
            amps[pv] = (8192 * ifr) >> 15;
            amps[tg] = (8192 * (32768 - ifr)) >> 15;
        } else if (i < rot) {
            ratios[pv] = rr;
            amps[pv] = 8192;
        } else {
            ratios[tg] = rr;
            amps[tg] = 8192;
        }
    }
    (void)ratio_ok;
    inc0 = (uint32_t)(((uint64_t)m->inc * 65405u) >> 16);                 /* x 0.998 */
    for (i = 0; i < (int32_t)n; i++)
        acc[i] = 0;
    for (j = 0; j < NANO_CH_V; j++) {
        uint32_t ninc = (uint32_t)(((uint64_t)inc0 * ratios[j]) >> 16);
        int32_t wt = ((S->morph - NANO_CH_FADE[j]) * 50), dd, ddg, tdd, twt, ad, aw, sd, sw;
        wt = wt < 0 ? 0 : wt > 32768 ? 32768 : wt;
        ninc = ninc > 0x7FFFFFFFu ? 0x7FFFFFFFu : ninc;
        ddg = 131072 - (int32_t)(ninc >> 17) * 32;
        ddg = ddg < 0 ? 0 : ddg > 32768 ? 32768 : ddg;
        dd = (((32768 - wt) * ddg) >> 15);
        tdd = (amps[j] * dd) >> 15;
        twt = (amps[j] * wt) >> 15;
        if (tdd == 0 && S->add[j] == 0 && twt == 0 && S->awt[j] == 0)
            continue;
        ad = S->add[j] << 8;
        aw = S->awt[j] << 8;
        sd = ((tdd - S->add[j]) << 8) / (int32_t)n;
        sw = ((twt - S->awt[j]) << 8) / (int32_t)n;
        {   /* the string machine */
            int32_t sh = ninc > 0x40000000u ? 6 : ninc > 0x20000000u ? 4 : ninc > 0x10000000u ? 2 : 0, rg[7], g[4], k;
            uint32_t incb = ninc >> (sh >> 1), ph = S->pdd[j];
            if (ninc > 0x40000000u * 2u - 1u)
                sh = 8;
            for (k = 0; k < 7; k++)
                rg[k] = k < sh ? 0 : harm[k - sh];
            g[0] = rg[0] + 2 * rg[1];
            g[1] = rg[2] - rg[1] + 2 * rg[3];
            g[2] = rg[4] - rg[3] + 2 * rg[5];
            g[3] = rg[6] - rg[5];
            if (sh < 8 && (ad || sd)) {
                for (i = 0; i < (int32_t)n; i++) {
                    int32_t x = 0;
                    ad += sd;
                    for (k = 0; k < 4; k++)
                        if (g[k])
                            x += (osc_saw(ph << k, incb << k) * g[k]) >> 15;
                    acc[i] += (x * (ad >> 8)) >> 15;
                    ph += incb;
                }
            } else {
                ph += incb * n;
            }
            S->pdd[j] = ph;
        }
        if (aw || sw) {   /* the wavetable oscillator */
            uint32_t winc = ninc + (ninc >> 8), ph = S->pwt[j];
            int32_t c, lp = S->lp[j], dl = S->dlp[j], dp = S->dprev[j], G, k2;
            const int16_t *t0 = NANO_CHW[wi], *t1 = NANO_CHW[wi + 1];
            winc = winc > 0x40000000u ? 0x40000000u : winc;
            c = (int32_t)(winc >> 17) * 128;                              /* min(128 f, 1), Q15 */
            c = c > 32768 ? 32768 : c;
            G = (int32_t)(((uint64_t)1 << 38) / (winc | 1u));
            G = G > (1 << 20) ? (1 << 20) : G;
            k2 = 65536 - (int32_t)(winc >> 15);                           /* x (1 - 2 f) */
            for (i = 0; i < (int32_t)n; i++) {
                uint32_t pi = ph >> 25;
                int32_t pf = (int32_t)((ph >> 10) & 32767u), a0 = t0[pi], b0 = t0[pi + 1], a1 = t1[pi], b1 = t1[pi + 1], x0, x1, in, y;
                x0 = a0 + (((b0 - a0) * pf) >> 15);
                x1 = a1 + (((b1 - a1) * pf) >> 15);
                in = x0 + (((x1 - x0) * wf) >> 15);
                dl += ((((in - dp) - dl) >> 1) * c) >> 14;
                dp = in;
                lp += (((dl - lp) >> 1) * c) >> 14;
                aw += sw;
                y = (lp * G) >> 8;
                y = (y * (aw >> 8)) >> 15;
                acc[i] += (y * (k2 >> 1)) >> 15;
                ph += winc;
            }
            S->pwt[j] = ph;
            S->lp[j] = lp;
            S->dlp[j] = dl;
            S->dprev[j] = dp;
        }
        S->add[j] = tdd;
        S->awt[j] = twt;
    }
    tsvf_coef(&flt, cut, p[P_E6]);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t s = soft_knee(analog_tap(&flt, 0, acc[i] >> 1, &ic1, &ic2, 0), 16000) << 1;
        out[i] += voice_amp(s, m, (uint32_t)i) << 1;
    }
    v->s[0] = ic1;
    v->s[1] = ic2;
}

/* MODAL (Plaits' modal voice with continuous dust excitation): dust through a low-pass rings a bank of resonators at the
 * harmonics of the key, stretched by the structure's stiffness. E1 STRUCTURE (the inharmonicity, as material), E2
 * BRIGHT (the excitation's colour and the dust's density), E3 DAMP (how long they ring). Twelve of Plaits' 24 modes, and only
 * the first NANO_MODAL_POLY voices sound: a bank of resonators per voice is the engine's cost. The resonators are the
 * trapezoidal SVF of dsp.c with coefficients worked out per block from the mode's frequency. */
static void nano_svf_coef(tsvf_t *c, uint32_t inc, int32_t k)       /* inc: cycles per sample, 2^32 = 1; k: damping, Q12 */
{
    int32_t sn, cs, g, den;
    inc = inc > 0x66666666u ? 0x66666666u : inc;
    sn = sine_i(inc >> 1);
    cs = sine_i((inc >> 1) + 0x40000000u);
    g = (int32_t)(((uint32_t)(sn < 1 ? 1 : sn) << 12) / (uint32_t)(cs < 1 ? 1 : cs));      /* tan(pi f), Q12 */
    den = 4096 + ((g * (g + k)) >> 12);
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
}

static __attribute__((noinline)) void nano_render_mo(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    nano_su_t *U = nano_slot(t, v);
    nano_mo_t *S = U ? &U->mo : 0;
    int32_t tmp[CTL], acc[CTL], gn[NANO_MODES];
    tsvf_t md[NANO_MODES], ex, flt;
    int32_t hq = p[P_E1] * 258, bq0 = clamp((p[P_E2] << 8) + m->shape - (64 << 8), 0, 127 << 8), dq0 = p[P_E3] * 258;
    int32_t bq, dq, bb, dens, dust16, amp, si, sf, stiff, st, str3, qs, qm, ql, e, k, i, mi, ic1 = v->s[0], ic2 = v->s[1];
    uint32_t f0 = m->inc, f0c, exinc, rng;
    int32_t cut = (p[P_E5] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    if (!S || n > CTL || (uint32_t)(v - t->v) >= NANO_MODAL_POLY)
        return;
    bq0 += bq0 >> 7;
    S->hlp += ((hq - S->hlp) * 875) >> 15;
    hq = S->hlp;
    dens = (bq0 * bq0) >> 15;
    dust16 = 3 + ((((dens * dens) >> 15) * 65533) >> 15);                 /* 0.00005 + density^2 */
    bq = bq0 + ((32768 - bq0) / 5);                                       /* the accent: + 0.25 * 0.8 (1 - x) */
    dq = dq0 + ((32768 - dq0) / 5);
    bb = (bq * (65536 - bq)) >> 15;                                       /* b (2 - b) */
    exinc = nano_inc_c(f0, (uint32_t)(768 + 24 * 64 + (((bb - 16384) * 36 * 64) >> 15)), 0x66666666u);     /* 4 f0 x 2^((b (2 - b) - 0.5) 3) */
    nano_svf_coef(&ex, exinc, 5851);                                      /* Q 0.7 */
    amp = ((131072 - 3 * (dust16 >> 1)) * 26214) >> 15;                   /* (4 - 3 dust) x 0.8 */
    si = hq >> 9;
    sf = (hq & 511) << 6;
    stiff = NANO_STIF[si] + (((NANO_STIF[si + 1] - NANO_STIF[si]) * sf) >> 15);        /* Q12 */
    st = stiff;
    str3 = 4096 + st;
    st = (st * (st < 0 ? 30474 : 32113)) >> 15;
    str3 += st;                                                           /* the third harmonic's stretch: f0 is set to land it right */
    f0c = (uint32_t)(((uint64_t)f0 << 12) / (uint32_t)str3);
    e = (dq * 217651) >> 15;                                              /* 2^(damping 79.7 / 12), Q15 octaves */
    {
        int32_t oct = e >> 15, fr = e & 32767, ix = (fr * 24) >> 15;
        uint32_t f5 = (((uint32_t)fr * 24u) & 32767u) >> 10, a = NANO_P2[ix], b = NANO_P2[ix + 1];
        qs = (int32_t)((((a + (((b - a) * f5) >> 5)) << oct)) >> 8);       /* Q8 */
    }
    qs = (qs * qs) >> 8;                                                  /* Q8: q_sqrt^2 */
    qm = (500 * qs) >> 8;
    bq = (bq * (32768 - ((hq * 9830) >> 15))) >> 15;                      /* the resonators' brightness: x (1 - 0.3 STRUCTURE) (1 - 0.3 DAMP) */
    bq = (bq * (32768 - ((dq * 9830) >> 15))) >> 15;
    ql = ((((bq * (65536 - bq)) >> 15) * 27853) >> 15) + 4915;            /* b (2 - b) 0.85 + 0.15 */
    st = stiff;
    {
        int32_t stretch = 4096;
        for (mi = 0; mi < NANO_MODES; mi++) {
            uint32_t mf = (uint32_t)(((uint64_t)f0c * (uint32_t)(mi + 1) * (uint32_t)stretch) >> 12);
            int32_t att, x;
            mf = mf > 0x66666666u ? 0x66666666u : mf;
            att = 32768 - (int32_t)(mf >> 16);
            gn[mi] = (NANO_MAMP[mi] * att) >> 15;
            x = (int32_t)(((uint64_t)(mf >> 16) * (uint32_t)qm) >> 16);
            k = 4096 / (1 + x);
            nano_svf_coef(&md[mi], mf, k < 1 ? 1 : k);
            stretch += st;
            st = (st * (st < 0 ? 30474 : 32113)) >> 15;
            qm = (qm * ql) >> 15;
        }
    }
    rng = (uint32_t)S->rng;
    {   /* the dust through the low-pass */
        int32_t s1 = S->ex1, s2 = S->ex2;
        for (i = 0; i < (int32_t)n; i++) {
            int32_t d = 0, v3, v1, v2;
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            if ((int32_t)(rng >> 16) < dust16)
                d = (((int32_t)(rng >> 1) & 32767) * amp) >> 15;
            v3 = d - s2;
            v1 = (ex.a1 * s1 + ex.a2 * v3) >> 13;
            v2 = s2 + ((ex.a2 * s1 + ex.a3 * v3) >> 13);
            s1 = clamp(2 * v1 - s1, -150000, 150000);
            s2 = clamp(2 * v2 - s2, -150000, 150000);
            tmp[i] = v2 >> 2;
            acc[i] = 0;
        }
        S->ex1 = s1;
        S->ex2 = s2;
        S->rng = (int32_t)rng;
    }
    for (mi = 0; mi < NANO_MODES; mi++) {
        int32_t s1 = S->s1[mi], s2 = S->s2[mi];
        const tsvf_t *c = &md[mi];
        for (i = 0; i < (int32_t)n; i++) {
            int32_t v3 = tmp[i] - s2, v1 = (c->a1 * s1 + c->a2 * v3) >> 13, v2 = s2 + ((c->a2 * s1 + c->a3 * v3) >> 13);
            s1 = clamp(2 * v1 - s1, -150000, 150000);
            s2 = clamp(2 * v2 - s2, -150000, 150000);
            acc[i] += (gn[mi] * v1) >> 15;
        }
        S->s1[mi] = s1;
        S->s2[mi] = s2;
    }
    tsvf_coef(&flt, cut, p[P_E6]);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t s = soft_knee(analog_tap(&flt, 0, clamp(soft_knee(acc[i] << 1, 16000), -32768, 32767), &ic1, &ic2, 0), 11000) << 1;
        out[i] += voice_amp(s, m, (uint32_t)i) << 1;
    }
    v->s[0] = ic1;
    v->s[1] = ic2;
}

/* SWARM (Braids' SuperWave, in the Nano Freak test bed): seven oscillators, the centre in tune and six spread by DETUNE,
 * through an overdrive and a high-pass that follows the key. E1 WAVE (saw, square, triangle, sine), E2 DETUNE, E3 MIX (the six
 * against the centre). The six start at random phases on each note. State in the slot union (nano_sw_t). */
typedef struct {
    uint32_t ph[7];
    int32_t bp, lp, rng;
} nano_sw_t;

static inline int32_t nano_swv(uint32_t ph, int32_t wave)         /* -4096 .. 4095 */
{
    switch (wave) {
    case 1:
        return (ph >> 31) ? 4095 : -4096;
    case 2: {
        int32_t q = (int32_t)(ph >> 18);
        return (q < 8192 ? q : 16383 - q) - 4096;
    }
    case 3: {
        int32_t a = NANO_BSIN[ph >> 24], b = NANO_BSIN[(ph >> 24) + 1];
        return (a + (((b - a) * (int32_t)((ph >> 8) & 0xffff)) >> 16)) >> 3;
    }
    default:
        return (int32_t)(ph >> 19) - 4096;
    }
}

static __attribute__((noinline)) void nano_render_sw(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    nano_su_t *U = nano_slot(t, v);
    nano_sw_t *W;
    uint32_t inc[7], i, k, inc0 = m->inc;
    int32_t wave = p[P_E1] >= 96 ? 3 : p[P_E1] >> 5, detune, vol, den, f, bp, lp, ic1 = v->s[0], ic2 = v->s[1];
    int32_t cut = (p[P_E5] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    tsvf_t flt;
    if (!U)
        return;
    W = (nano_sw_t *)U;
    detune = clamp((p[P_E2] << 8) + m->shape - (64 << 8), 0, 127 << 8);
    detune = ((detune + (detune >> 7)) >> 0);                            /* 0..32767 */
    detune += 1024;
    detune = (detune * detune) >> 9;
    vol = (p[P_E3] * 256) / 127;
    for (k = 0; k < 7; k++) {                                            /* inc x 2^(detune (k - 3) / 65536 / 128 / 12), a straight line at these sizes */
        int32_t d = (int32_t)((((int64_t)(inc0 >> 8) * detune * ((int32_t)k - 3)) * 1893) >> 30);
        inc[k] = (uint32_t)((int32_t)inc0 + d);
    }
    f = 2 * sine_i((inc0 > 0x20000000u ? 0x20000000u : inc0) >> 1);      /* the high-pass: 2 sin(pi f0), as Braids', capped at sr / 8 */
    den = 256 + 6 * vol;
    k = (uint32_t)((7 * 65536) / den);
    bp = W->bp;
    lp = W->lp;
    tsvf_coef(&flt, cut, p[P_E6]);
    for (i = 0; i < n; i++) {
        int32_t c, sm, notch, hp, o, sx, s;
        uint32_t j;
        for (j = 0; j < 7; j++)
            W->ph[j] += inc[j];
        c = nano_swv(W->ph[3], wave);
        sx = 0;
        for (j = 0; j < 7; j++)
            if (j != 3)
                sx += nano_swv(W->ph[j], wave);
        sm = ((((c * 256 + sx * vol) >> 3) * (int32_t)k) >> 13);
        {
            uint32_t ix = (uint32_t)(sm + 32768);
            int32_t a = NANO_BOD[ix >> 8], b = NANO_BOD[(ix >> 8) + 1];
            sm = a + (((b - a) * (int32_t)(ix & 0xff)) >> 8);
        }
        notch = sm - ((bp * 65534) >> 15);
        lp += (f * bp) >> 15;
        lp = clamp(lp, -32767, 32767);
        hp = notch - lp;
        bp += (f * hp) >> 15;
        o = clamp(hp, -32767, 32767);
        s = soft_knee(analog_tap(&flt, 0, o >> 1, &ic1, &ic2, 0), 16000) << 1;
        out[i] += voice_amp(s, m, i) << 1;
    }
    W->bp = bp;
    W->lp = lp;
    v->s[0] = ic1;
    v->s[1] = ic2;
}

static void nano_note_on(track_t *t, voice_t *v)
{
    v->ph[1] = v->ph[0];                              /* the slave starts with the master */
    v->ph[2] = v->ph[0] >> 1;                         /* the sub: an octave down, in step with it */
    v->s[0] = v->s[1] = 0;                            /* filter */
    v->s[2] = 0;                                      /* no restart pending */
    v->s[3] = t->p[P_E0] == 2 ? -1 : 0;              /* FM: no DC yet; WAVESHAPER: -1, start from this block's own values */
    if (t->p[P_E0] == 2)
        v->ph[0] = 0;
    if (t->p[P_E0] == 4) {                                /* NOISE: the clock restarts at once; a new random run per note */
        v->ph[0] = 0xFFFFFFFFu;
        v->s[4] = v->s[5] = 0;
        v->s[6] = (int32_t)((0x9E3779B9u ^ (v->age * 2654435761u) ^ ((uint32_t)v->note << 8)) | 1u);
    }
    if (t->p[P_E0] == 5) {                                /* CHORDS: a clean slot, the smoothers start at the knobs */
        nano_su_t *U = nano_slot(t, v);
        if (U) {
            uint32_t k;
            for (k = 0; k < sizeof *U / 4; k++)
                ((uint32_t *)U)[k] = 0;
            U->ch.morph = t->p[P_E3] * 258;
            U->ch.timbre = t->p[P_E2] * 258;
        }
    }
    if (t->p[P_E0] == 7) {                                /* SWARM: the six start at random phases */
        nano_su_t *U = nano_slot(t, v);
        if (U) {
            nano_sw_t *W = (nano_sw_t *)U;
            uint32_t k, x = 0x21u ^ (v->age * 2654435761u) ^ ((uint32_t)v->note << 16);
            for (k = 0; k < sizeof *U / 4; k++)
                ((uint32_t *)U)[k] = 0;
            for (k = 1; k < 7; k++) {
                x = x * 1664525u + 1013904223u;
                W->ph[k] = x;
            }
        }
    }
    if (t->p[P_E0] == 6) {                                /* MODAL: a clean bank, the seed from the voice */
        nano_su_t *U = nano_slot(t, v);
        if (U) {
            uint32_t k;
            for (k = 0; k < sizeof *U / 4; k++)
                ((uint32_t *)U)[k] = 0;
            U->mo.hlp = t->p[P_E1] * 258;
            U->mo.rng = (int32_t)((0x2545F491u ^ (v->age * 2654435761u)) | 1u);
        }
    }
    if (t->p[P_E0] == 3)
        v->ph[0] = v->ph[1] = v->ph[2] = 0;               /* FORMANT: the carrier and both formants start at zero */
}

static __attribute__((noinline)) void nano_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    nano_w w;
    if (p[P_E0] == 1) {
        nano_render_fm(t, v, out, n, m);
        return;
    }
    if (p[P_E0] == 2) {
        nano_render_ws(t, v, out, n, m);
        return;
    }
    if (p[P_E0] == 3) {
        nano_render_fo(t, v, out, n, m);
        return;
    }
    if (p[P_E0] == 4) {
        nano_render_no(t, v, out, n, m);
        return;
    }
    if (p[P_E0] == 5) {
        nano_render_ch(t, v, out, n, m);
        return;
    }
    if (p[P_E0] == 6) {
        nano_render_mo(t, v, out, n, m);
        return;
    }
    if (p[P_E0] == 7) {
        nano_render_sw(t, v, out, n, m);
        return;
    }
    tsvf_t flt;
    uint32_t i, ph = v->ph[0], ps = v->ph[1], pb = v->ph[2], inc = m->inc, incs = inc, k = (uint32_t)p[P_E4];
    int32_t morph = clamp((p[P_E1] << 8) + m->shape - (64 << 8), 0, 127 << 8), mq, sym = p[P_E2] * 258;
    int32_t sub = p[P_E3] * 258, d, ic1 = v->s[0], ic2 = v->s[1], pend = v->s[2];
    int32_t cut = (p[P_E5] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    mq = morph + (morph >> 7);                                    /* 0..127 << 8 -> 0..32767 */
    w.hi = mq > 16384;
    w.t = w.hi ? 32768 : mq * 2;
    w.u = w.hi ? (mq - 16384) * 2 : 0;
    w.rev = (1 << 30) / (32768 + w.u);                            /* once a block: a 32-bit divide */
    d = (((sym - 16384) * 941) >> 10);                            /* (SYM - 0.5) * 0.92 */
    d = (d * (32768 - w.t)) >> 15;                                /* fading out as the pulse becomes a saw */
    w.pw = 0x80000000u + (uint32_t)d * 131072u;
    w.off = (uint32_t)sym << 16;                                  /* SYM * half a cycle */
    if (k) {                                                      /* the slave: 1x .. 5x, never past 0.45 of the sample rate */
        uint32_t r8 = 256u + (k * 1024u + 63u) / 127u;
        uint64_t x = ((uint64_t)inc * r8) >> 8;
        incs = x > 0x73333333u ? 0x73333333u : (uint32_t)x;
    }
    tsvf_coef(&flt, cut, p[P_E6]);
    for (i = 0; i < n; i++) {
        int32_t s, sn;
        uint32_t pn = ph + inc;
        if (!k) {
            s = nano_wave(&w, ph, inc);
            ps = ph;
        } else {
            s = nano_wave(&w, ps, incs) + pend;
            pend = 0;
            if (pn < ph) {                                        /* the master wrapped inside this sample: the slave restarts */
                uint32_t f = pn / ((inc >> 16) | 1u);             /* the part of a sample since, 0..65535 */
                int32_t omb, e, vb, va, step;
                if (f > 65535u)
                    f = 65535u;
                omb = (int32_t)(f >> 1);                          /* 1 - e, Q15 (e: how far ahead of this sample the restart is) */
                e = 32768 - omb;
                vb = nano_wave(&w, ps + (incs >> 15) * (uint32_t)e, 0);   /* the slave where it was cut, naive */
                va = nano_wave(&w, 0, 0);                                 /* and where it restarts */
                step = va - vb;
                s += ((step >> 1) * ((omb * omb) >> 15)) >> 15;           /* polyBLEP of the restart: this sample and the next */
                pend = -(((step >> 1) * ((e * e) >> 15)) >> 15);
                ps = f * (incs >> 16);
            } else {
                ps += incs;
            }
        }
        sn = sine_i(pb);
        s = (s + mulq15(sn, sub)) >> 1;                           /* (wave + sub) / 2 */
        s = soft_knee(analog_tap(&flt, 0, s >> 1, &ic1, &ic2, 0), 16000) << 1;
        ph = pn;
        pb += inc >> 1;
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = ph;
    v->ph[1] = ps;
    v->ph[2] = pb;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = pend;
}

static const preset_t NANO_PRESETS[] = {
    /* TYPE MORPH SYM SUB SYNC | CUT RES KTR */
    {"INIT SAW", {0, 127, 0, 0, 0, 110, 15, 64}, {2, 70, 100, 40}, 0, 0, FX(0, 10, 20, 15), PAT(0)},
    {"SQUARE", {0, 0, 64, 0, 0, 100, 20, 64}, {2, 70, 100, 40}, 0, 0, FX(0, 10, 20, 15), PAT(0)},
    {"PWM", {0, 0, 40, 0, 0, 105, 15, 64}, {10, 80, 100, 50}, 0, 0, FX(0, 40, 20, 30), PAT(5)},
    {"WIDE SAWS", {0, 100, 50, 0, 0, 100, 20, 64}, {30, 80, 110, 60}, 0, 0, FX(0, 50, 20, 40), PAT(5)},
    {"SYNC LEAD", {0, 127, 0, 0, 55, 100, 30, 64}, {2, 70, 90, 40}, 20, 1, FX(0, 10, 40, 25), PAT(4)},
    {"SUB BASS", {0, 40, 64, 100, 0, 80, 25, 64}, {0, 60, 100, 20}, 0, 1, FX(0, 0, 0, 10), PAT(8)},
    /* TWO OP FM: TYPE RATIO INDEX FEEDBACK 0 | CUT RES KTR */
    {"FM KEYS", {1, 30, 45, 64, 0, 127, 0, 64}, {2, 70, 60, 50}, 0, 0, FX(0, 20, 20, 20), PAT(0)},
    {"FM BELL", {1, 70, 70, 64, 0, 127, 0, 64}, {0, 100, 0, 80}, 0, 0, FX(0, 40, 30, 40), PAT(0)},
    {"FM BASS", {1, 30, 55, 90, 0, 110, 10, 64}, {0, 50, 90, 20}, 0, 1, FX(0, 0, 0, 10), PAT(8)},
    /* WAVESHAPER: TYPE WAVE FOLD SLOPE 0 | CUT RES KTR */
    {"WS FOLD", {2, 40, 70, 40, 0, 127, 0, 64}, {2, 70, 100, 40}, 0, 0, FX(0, 20, 20, 20), PAT(0)},
    {"WS BUZZ", {2, 100, 90, 64, 0, 110, 15, 64}, {0, 60, 100, 20}, 0, 1, FX(0, 10, 10, 20), PAT(8)},
    {"WS LEAD", {2, 20, 50, 100, 0, 127, 10, 64}, {5, 80, 90, 40}, 25, 1, FX(0, 20, 30, 25), PAT(4)},
    /* FORMANT: TYPE RATIO FORMANT SHAPE 0 | CUT RES KTR */
    {"VOWEL", {3, 64, 50, 40, 0, 127, 0, 64}, {5, 80, 100, 40}, 0, 0, FX(0, 20, 20, 25), PAT(0)},
    {"FORMANT LEAD", {3, 90, 80, 60, 0, 127, 10, 64}, {5, 80, 90, 40}, 25, 1, FX(0, 20, 30, 25), PAT(4)},
    {"GRAIN BASS", {3, 30, 25, 20, 0, 110, 10, 64}, {0, 60, 100, 20}, 0, 1, FX(0, 0, 0, 10), PAT(8)},
    /* NOISE: TYPE MODE CLOCK RES 0 | CUT RES KTR */
    {"WIND", {4, 20, 110, 50, 0, 127, 0, 64}, {40, 100, 100, 80}, 0, 0, FX(0, 40, 30, 30), PAT(0)},
    {"HISS PERC", {4, 100, 120, 20, 0, 127, 0, 64}, {0, 60, 0, 30}, 0, 0, FX(0, 10, 10, 20), PAT(0)},
    {"PITCHED NOISE", {4, 64, 100, 100, 0, 127, 0, 64}, {2, 70, 100, 40}, 0, 0, FX(0, 20, 20, 20), PAT(0)},
    /* CHORDS: TYPE CHORD INVERSION WAVE 0 | CUT RES KTR */
    {"STRING CHORD", {5, 127, 20, 10, 0, 127, 0, 64}, {20, 80, 100, 60}, 0, 0, FX(0, 30, 30, 30), PAT(0)},
    {"M7 ORGAN", {5, 100, 40, 40, 0, 127, 0, 64}, {2, 60, 100, 30}, 0, 0, FX(0, 20, 20, 20), PAT(0)},
    {"M9 TABLE", {5, 50, 60, 100, 0, 120, 10, 64}, {10, 80, 100, 50}, 0, 0, FX(0, 30, 30, 30), PAT(0)},
    /* MODAL: TYPE STRUCTURE BRIGHT DAMP 0 | CUT RES KTR */
    {"MARIMBA", {6, 20, 70, 40, 0, 127, 0, 64}, {0, 40, 100, 40}, 0, 0, FX(0, 20, 20, 20), PAT(0)},
    {"METAL BAR", {6, 90, 90, 70, 0, 127, 0, 64}, {0, 60, 100, 60}, 0, 0, FX(0, 30, 30, 30), PAT(0)},
    {"GLASS RAIN", {6, 60, 110, 90, 0, 127, 0, 64}, {10, 80, 100, 70}, 0, 0, FX(0, 40, 30, 40), PAT(0)},
    /* SWARM: TYPE WAVE DETUNE MIX 0 | CUT RES KTR */
    {"SUPER SAW", {7, 0, 60, 127, 0, 127, 0, 64}, {5, 80, 100, 40}, 0, 0, FX(0, 20, 20, 25), PAT(0)},
    {"SWARM PAD", {7, 96, 40, 100, 0, 110, 0, 64}, {40, 90, 100, 90}, 0, 0, FX(0, 40, 30, 40), PAT(0)},
    {"DETUNED SQUARES", {7, 32, 90, 90, 0, 127, 10, 64}, {5, 80, 90, 40}, 25, 1, FX(0, 20, 30, 25), PAT(4)},
};

static const engine_t ENG_NANO = {
    .name = "NANO",
    .page_title = {"OSC", "FLT"},
    .edit = {
        {"TYPE", F_ENUM, 0, 7, 0, N_NANO_TYPE, 0},
        {"MORPH", F_PCT, 0, 127, 64, 0, 0},
        {"SYM", F_PCT, 0, 127, 64, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
        {"SYNC", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 100, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"KTR", F_PCT, 0, 127, 64, 0, 0},
    },
    .presets = NANO_PRESETS,
    .npresets = NELEM(NANO_PRESETS),
    .note_on = nano_note_on,
    .render = nano_render,
    .knob = {P_E1, P_E2, P_E3, P_E4},
    .mem = sizeof(nano_su_t) * NANO_SLOTS,
    .keep = 0x03,                /* the filter */
};
