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
 * TYPE 8 is HARMONIC (Braids' Harmo): PEAK, WIDTH, CHORUS, STRETCH (E4).
 *
 * TYPE 14 is RINGS (Mutable Instruments Rings): MODEL, STRUCTURE, BRIGHTNESS, DAMPING, POSITION on E7.
 * TYPE 13 is V.ANALOG (Plaits' virtual analog): DETUNE, SYNC, SHAPE.
 * TYPE 12 is WAVETABLE (16 tables of 16 waves): TABLE, POSITION, CHORUS, MORPH.
 * TYPE 11 is SAW (Braids' SAW_SYNC / SAW_COMB / CSAW): WAVE, E2, E3.
 * TYPE 10 is BASS (Braids' SQUARE_SUB / SAW_SUB): WAVE, WIDTH, SUB.
 * TYPE 9 is PLUCK (Braids' KarplusStr): BOW, POSITION, DECAY; four voices (the delay lines).
 *
 * then a state variable low-pass (CUT RES KTR, ANALOG's own filter code) so the first builds are playable. The Nano Freak
 * filter, envelopes and modulation matrix replace this later; this filter is the stand-in.
 *
 * The saws and pulse come from dsp.c (osc_saw, a 32-bit phase, polyBLEP in Q15); the pulse is the difference of two saws, a
 * saw one width behind minus the saw: zero mean, up at phase 0 and down at the width, as the test bed's, at full level (not
 * the half level of osc_pulse(), whose edges are the other way round, which matters when it is crossfaded into the saw).
 * State: ph[0] master, ph[1] the synced slave, ph[2] the sub; s[0] s[1] the filter, s[2] the restart correction owed to the
 * next sample. 32-bit multiplies per sample; the 64-bit one is once a block (the slave's increment). */
static const char *const N_NANO_TYPE[] = {"BASIC", "TWO OP FM", "WAVESHAPER", "FORMANT", "NOISE", "CHORDS", "MODAL", "SWARM", "HARMONIC", "PLUCK", "BASS", "SAW", "WAVETABLE", "V.ANALOG", "RINGS"};

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
    uint32_t hm[40];                                    /* HARMONIC's nano_hm_t (below) */
    uint32_t pl[12];                                    /* PLUCK's nano_pl_t (below) */
    uint32_t bs[12];                                    /* BASS's nano_bs_t (below) */
    uint32_t sa[16];                                    /* SAW's nano_sa_t (below) */
    uint32_t wt[4];                                     /* WAVETABLE's nano_wt_t (below) */
    uint32_t va[20];                                    /* V.ANALOG's nano_va_t (below) */
    uint32_t ri[80];                                    /* RINGS's nano_ri_t (below) */
} nano_su_t;
#define NANO_SLOTS NVOICE
#define NANO_PL_V 4
#define NANO_PL_DL 1026
ENG_MEM_FITS("NANO", sizeof(nano_su_t) * NANO_SLOTS + NANO_PL_V * NANO_PL_DL * 2);

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

/* HARMONIC (Braids' Harmo, in the Nano Freak test bed): twelve sine harmonics whose levels follow a moving spectral peak,
 * rendered at half rate and interpolated. E1 PEAK (where the peak sits), E2 WIDTH (its width and a second peak), E3 CHORUS
 * (a second, slightly sharp copy crossfaded in), E4 STRETCH (partial n at n (1 + 0.01 STRETCH (n^2 - 1)), 0 is harmonic).
 * State in the slot union (nano_hm_t). */
#define NANO_HARM 12
typedef struct {
    uint32_t phase, phase2;
    int32_t amp[NANO_HARM], prev, stretched;
    uint32_t sph[NANO_HARM], sph2[NANO_HARM];
} nano_hm_t;

static inline int32_t nano_sin824(uint32_t ph)
{
    int32_t a = NANO_BSIN[ph >> 24], b = NANO_BSIN[(ph >> 24) + 1];
    return a + (((b - a) * (int32_t)((ph >> 8) & 0xffff)) >> 16);
}

static __attribute__((noinline)) void nano_render_hm(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    nano_su_t *U = nano_slot(t, v);
    nano_hm_t *H;
    int32_t target[NANO_HARM], amp[NANO_HARM], p0 = p[P_E1] * 258, p1, chorus = p[P_E3] * 516, stretch = p[P_E4] * 258, prev, c;
    int32_t peak, second_peak, second_amount, sqrt2, sqrt_w, width, total = 0, atten, ic1 = v->s[0], ic2 = v->s[1];
    uint32_t pinc[NANO_HARM], pinc2[NANO_HARM], phase, phase2, inc, inc2, i, j, nn = n & ~1u;
    int32_t cut = (p[P_E5] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    int st = stretch > 0;
    tsvf_t flt;
    if (!U)
        return;
    H = (nano_hm_t *)U;
    p1 = clamp((p[P_E2] << 8) + m->shape - (64 << 8), 0, 127 << 8);
    p1 = (p1 + (p1 >> 7)) >> 0;                                          /* 0..32767 */
    if (chorus > 65535)
        chorus = 65535;
    phase = H->phase;
    phase2 = H->phase2;
    prev = H->prev;
    inc = m->inc << 1;
    inc2 = inc + (uint32_t)(((uint64_t)inc * (uint32_t)((int64_t)chorus * 97300 / 65536)) >> 24);   /* up to about 10 cents sharp */
    peak = (NANO_HARM * p0) >> 7;
    second_peak = (peak >> 1) + NANO_HARM * 128;
    second_amount = (p1 * p1) >> 15;
    sqrt2 = p1 < 16384 ? p1 >> 6 : 511 - (p1 >> 6);
    sqrt_w = (sqrt2 * sqrt2) >> 10;
    width = sqrt_w * sqrt_w + 4;
    for (i = 0; i < NANO_HARM; i++) {
        int32_t x = (int32_t)i << 8, d, g;
        d = x - peak;
        g = 32768 * 128 / (128 + d * d / width);
        d = x - second_peak;
        g += second_amount * 128 / (128 + d * d / width);
        total += g;
        target[i] = g;
    }
    atten = 2147483647 / total;
    if (st && !H->stretched)
        for (i = 0; i < NANO_HARM; i++) {
            H->sph[i] = phase * (i + 1);
            H->sph2[i] = phase2 * (i + 1);
        }
    H->stretched = st;
    for (i = 0; i < NANO_HARM; i++) {
        uint64_t pi = (uint64_t)inc * (i + 1), pi2 = (uint64_t)inc2 * (i + 1);
        if (st) {                                                        /* partial n at n (1 + 0.01 STRETCH (n^2 - 1)) */
            uint32_t rr = 65536u + (uint32_t)((((stretch > 32768 ? 32768 : stretch) * 655) >> 15) * ((i + 1) * (i + 1) - 1));
            pi = (uint64_t)inc * (i + 1) * rr >> 16;
            pi2 = (uint64_t)inc2 * (i + 1) * rr >> 16;
        }
        pinc[i] = (uint32_t)pi;
        pinc2[i] = (uint32_t)pi2;
        if (st ? (int64_t)(pi >> 16) > 0x4000 : (int32_t)(inc >> 16) * (int32_t)(i + 1) > 0x4000)
            target[i] = 0;
        else
            target[i] = (target[i] * atten) >> 16;
        amp[i] = H->amp[i];
    }
    c = chorus;
    tsvf_coef(&flt, cut, p[P_E6]);
    for (j = 0; j < nn; j += 2) {
        int32_t a = 0, b = 0, r, k;
        phase += inc;
        phase2 += inc2;
        for (i = 0; i < NANO_HARM; i++) {
            if (st) {
                H->sph[i] += pinc[i];
                H->sph2[i] += pinc2[i];
                a += (nano_sin824(H->sph[i]) * amp[i]) >> 15;
                if (c)
                    b += (nano_sin824(H->sph2[i]) * amp[i]) >> 15;
            } else {
                a += (nano_sin824(phase * (i + 1)) * amp[i]) >> 15;
                if (c)
                    b += (nano_sin824(phase2 * (i + 1)) * amp[i]) >> 15;
            }
            amp[i] += (target[i] - amp[i]) >> 8;
        }
        a = clamp(a, -32767, 32767);
        b = clamp(b, -32767, 32767);
        r = c ? (int32_t)(((int64_t)a * (131072 - c) + (int64_t)b * c) >> 17) : a;
        for (k = 0; k < 2; k++) {
            int32_t x = k ? r : (r + prev) >> 1, s = soft_knee(analog_tap(&flt, 0, x >> 1, &ic1, &ic2, 0), 16000) << 1;
            out[j + k] += voice_amp(s, m, j + k) << 1;
        }
        prev = r;
    }
    H->phase = phase;
    H->phase2 = phase2;
    H->prev = prev;
    for (i = 0; i < NANO_HARM; i++)
        H->amp[i] = amp[i];
    v->s[0] = ic1;
    v->s[1] = ic2;
}

/* Braids' AnalogOscillator (polyBLEP square, variable saw, saw, CSAW), shared by BASS and SAW: the increment is given
 * directly (2^32 = one cycle a sample), blocks are at most NANO_AO_MAX samples. State per oscillator in nano_ao_t. */
#define NANO_AO_MAX 32
enum { NANO_A_SQUARE, NANO_A_VSAW, NANO_A_SAW, NANO_A_CSAW };
typedef struct {
    uint32_t phase, prev_inc;
    int32_t next_sample, disc, high;
} nano_ao_t;

static inline int32_t nano_this_blep(uint32_t t) { if (t > 65535u) t = 65535u; return (int32_t)(t * t >> 18); }
static inline int32_t nano_next_blep(uint32_t t) { if (t > 65535u) t = 65535u; t = 65535u - t; return -(int32_t)(t * t >> 18); }
static inline uint32_t nano_d16(uint32_t i) { return (i >> 16) ? (i >> 16) : 1u; }

static void nano_ao_render(nano_ao_t *o, int shape, uint32_t inc, int32_t parameter, int32_t aux, const uint8_t *sync_in,
                           int16_t *buffer, uint8_t *sync_out, uint32_t size)
{
    uint32_t phase_increment, inc_inc, phase = o->phase, n;
    int32_t next_sample = o->next_sample, disc = o->disc, high = o->high;
    if (inc > 0x80000000u)
        inc = 0x80000000u;
    if (inc < 1u)
        inc = 1u;
    if (!o->prev_inc)
        o->prev_inc = inc;
    phase_increment = o->prev_inc;
    inc_inc = o->prev_inc < inc ? (inc - o->prev_inc) / size : ~((o->prev_inc - inc) / size);
    if (shape == NANO_A_SQUARE && parameter > 32000)
        parameter = 32000;
    if (shape == NANO_A_VSAW && parameter < 1024)
        parameter = 1024;
    for (n = 0; n < size; n++) {
        int32_t sync_reset = 0, self_reset = 0, transition_during_reset = 0, this_sample;
        uint32_t reset_time = 0, pw = 0;
        phase_increment += inc_inc;
        if (shape == NANO_A_SQUARE)
            pw = (uint32_t)(32768 - parameter) << 16;
        else if (shape == NANO_A_VSAW)
            pw = (uint32_t)parameter << 16;
        else if (shape == NANO_A_CSAW) {
            pw = (uint32_t)parameter * 49152u;
            if (pw < 8u * phase_increment)
                pw = 8u * phase_increment;
        }
        this_sample = next_sample;
        next_sample = 0;
        if (sync_in && sync_in[n]) {
            uint32_t phase_at_reset;
            reset_time = (uint32_t)(sync_in[n] - 1) << 9;
            phase_at_reset = phase + (65535u - reset_time) * (phase_increment >> 16);
            sync_reset = 1;
            if (shape == NANO_A_SQUARE || shape == NANO_A_VSAW) {
                if (phase_at_reset < phase || (!high && phase_at_reset >= pw))
                    transition_during_reset = 1;
            } else if (shape == NANO_A_SAW) {
                if (phase_at_reset < phase)
                    transition_during_reset = 1;
            } else {
                if (phase_at_reset < phase || (!high && phase_at_reset >= pw))
                    transition_during_reset = 1;
            }
            if (shape == NANO_A_SQUARE) {
                if (phase_at_reset >= pw) {
                    this_sample -= nano_this_blep(reset_time);
                    next_sample -= nano_next_blep(reset_time);
                }
            } else if (shape == NANO_A_SAW) {
                int32_t d = (int32_t)(phase_at_reset >> 17);
                this_sample -= d * nano_this_blep(reset_time) >> 15;
                next_sample -= d * nano_next_blep(reset_time) >> 15;
            } else if (shape == NANO_A_VSAW) {
                int32_t before = (int32_t)(phase_at_reset >> 18) + (int32_t)((phase_at_reset - pw) >> 18);
                int32_t after = (int32_t)((0u - pw) >> 18), d = after - before;
                this_sample += d * nano_this_blep(reset_time) >> 15;
                next_sample += d * nano_next_blep(reset_time) >> 15;
            } else if (phase >= pw) {
                int32_t before, d;
                disc = -2048 + (aux >> 2);
                before = (int32_t)(phase_at_reset >> 18);
                d = (int32_t)(int16_t)disc - before;
                this_sample += d * nano_this_blep(reset_time) >> 15;
                next_sample += d * nano_next_blep(reset_time) >> 15;
            }
        }
        phase += phase_increment;
        if (phase < phase_increment)
            self_reset = 1;
        if (sync_out)
            sync_out[n] = phase < phase_increment ? (uint8_t)(phase / (phase_increment >> 7) + 1) : 0;
        if (shape == NANO_A_SAW) {
            if ((transition_during_reset || !sync_reset) && self_reset) {
                uint32_t t = phase / nano_d16(phase_increment);
                this_sample -= nano_this_blep(t);
                next_sample -= nano_next_blep(t);
            }
        } else {
            while (transition_during_reset || !sync_reset) {
                if (!high) {
                    uint32_t t;
                    if (phase < pw)
                        break;
                    t = (phase - pw) / nano_d16(phase_increment);
                    if (shape == NANO_A_SQUARE) {
                        this_sample += nano_this_blep(t);
                        next_sample += nano_next_blep(t);
                    } else if (shape == NANO_A_VSAW) {
                        this_sample -= nano_this_blep(t) >> 1;
                        next_sample -= nano_next_blep(t) >> 1;
                    } else {
                        int32_t d = (int32_t)(int16_t)(phase >> 18) - (int32_t)(int16_t)disc;
                        this_sample += (int16_t)d * nano_this_blep(t) >> 15;
                        next_sample += (int16_t)d * nano_next_blep(t) >> 15;
                    }
                    high = 1;
                }
                if (high) {
                    uint32_t t;
                    if (!self_reset)
                        break;
                    self_reset = 0;
                    t = phase / nano_d16(phase_increment);
                    if (shape == NANO_A_SQUARE) {
                        this_sample -= nano_this_blep(t);
                        next_sample -= nano_next_blep(t);
                    } else if (shape == NANO_A_VSAW) {
                        this_sample -= nano_this_blep(t) >> 1;
                        next_sample -= nano_next_blep(t) >> 1;
                    } else {
                        int32_t d;
                        disc = -2048 + (aux >> 2);
                        d = (int32_t)(int16_t)disc - 16383;
                        this_sample += (int16_t)d * nano_this_blep(t) >> 15;
                        next_sample += (int16_t)d * nano_next_blep(t) >> 15;
                    }
                    high = 0;
                }
            }
        }
        if (sync_reset) {
            phase = reset_time * (phase_increment >> 16);
            high = 0;
        }
        switch (shape) {
        case NANO_A_SQUARE:
            next_sample += phase < pw ? 0 : 32767;
            buffer[n] = (int16_t)((this_sample - 16384) << 1);
            break;
        case NANO_A_VSAW:
            next_sample += (int32_t)(phase >> 18);
            next_sample += (int32_t)((phase - pw) >> 18);
            buffer[n] = (int16_t)((this_sample - 16384) << 1);
            break;
        case NANO_A_SAW:
            next_sample += (int32_t)(phase >> 17);
            buffer[n] = (int16_t)((this_sample - 16384) << 1);
            break;
        default:
            next_sample += phase < pw ? disc : (int32_t)(phase >> 18);
            buffer[n] = (int16_t)((this_sample - 8192) << 1);
            break;
        }
    }
    o->prev_inc = phase_increment;
    o->phase = phase;
    o->next_sample = next_sample;
    o->high = high;
    o->disc = disc;
}

/* BASS (Braids' SQUARE_SUB / SAW_SUB, in the Nano Freak test bed): a square or variable saw with a square one or two
 * octaves down. E1 WAVE (square, saw), E2 WIDTH (pulse width / saw shape), E3 SUB (below the middle two octaves down, above
 * one octave down; the mix is zero in the middle and full at both ends). State: two nano_ao_t and the last SUB. */
typedef struct {
    nano_ao_t a, b;
    int32_t prev_p1;
} nano_bs_t;

static __attribute__((noinline)) void nano_render_bs(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    nano_su_t *U = nano_slot(t, v);
    nano_bs_t *B;
    int16_t x[NANO_AO_MAX], y[NANO_AO_MAX];
    int32_t ic1 = v->s[0], ic2 = v->s[1], p0, p1;
    int32_t cut = (p[P_E5] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    uint32_t i, j, size, inc = m->inc;
    tsvf_t flt;
    if (!U)
        return;
    B = (nano_bs_t *)U;
    p0 = clamp((p[P_E2] << 8) + m->shape - (64 << 8), 0, 127 << 8);
    p0 = p0 + (p0 >> 7);                                                 /* 0..32767 */
    p1 = p[P_E3] * 258;
    tsvf_coef(&flt, cut, p[P_E6]);
    for (i = 0; i < n; i += size) {
        int32_t start = B->prev_p1, delta = p1 - B->prev_p1, incr, xx = 0;
        size = n - i < NANO_AO_MAX ? n - i : NANO_AO_MAX;
        nano_ao_render(&B->a, p[P_E1] >= 64 ? NANO_A_VSAW : NANO_A_SQUARE, inc, p0, 0, 0, x, 0, size);
        nano_ao_render(&B->b, NANO_A_SQUARE, p1 < 16384 ? inc >> 2 : inc >> 1, 0, 0, 0, y, 0, size);
        incr = 32767 / (int32_t)size;
        for (j = 0; j < size; j++) {
            int32_t q, gain, sample, s;
            xx += incr;
            q = start + (delta * xx >> 15);
            gain = (q < 16384 ? (16383 - q) : (q - 16384)) << 1;
            sample = (x[j] * (65535 - gain) + y[j] * gain) >> 16;
            s = soft_knee(analog_tap(&flt, 0, (sample * 3) >> 3, &ic1, &ic2, 0), 16000) << 1;
            out[i + j] += voice_amp(s, m, i + j) << 1;
        }
        B->prev_p1 = p1;
    }
    v->s[0] = ic1;
    v->s[1] = ic2;
}

/* PLUCK (Braids' KarplusStr, in the Nano Freak test bed): a string made of a delay line that is averaged at random, struck by
 * noise. E1 BOW (noise fed in while the note is held), E2 POSITION (where it is struck), E3 DECAY. One string per voice (Braids'
 * three are for a mono voice's overlapping notes), rendered at half rate. The delay lines (about 2 KB each) follow the slots in
 * the part's engine memory and only the first NANO_PL_V voices have one: the rest are silent. */
typedef struct {
    uint32_t size, write_ptr, shift, mask, init_ptr, phase, inc, max_inc, rng, strike;
    int32_t prev;
} nano_pl_t;

static int16_t *nano_dl(track_t *t, voice_t *v)
{
    uint32_t i = (uint32_t)(v - t->v);
    if (t < &trk[0] || t >= &trk[NPART] || i >= NANO_PL_V)
        return 0;
    return (int16_t *)((uint8_t *)ENG_MEM(t - trk) + sizeof(nano_su_t) * NANO_SLOTS) + i * NANO_PL_DL;
}

static __attribute__((noinline)) void nano_render_pl(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    nano_su_t *U = nano_slot(t, v);
    int16_t *dl = nano_dl(t, v);
    nano_pl_t *P;
    uint32_t inc_full = (m->inc > 0x7FFFFFFFu ? 0x7FFFFFFFu : m->inc) << 1, i, nn = n & ~1u, update_prob, rng;
    int32_t p0 = p[P_E3] * 258, p1, bow = p[P_E1] * 258, loss, prev, ic1 = v->s[0], ic2 = v->s[1];
    int32_t cut = (p[P_E5] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    tsvf_t flt;
    if (!U || !dl)
        return;
    P = (nano_pl_t *)U;
    p1 = clamp((p[P_E2] << 8) + m->shape - (64 << 8), 0, 127 << 8);
    p1 = p1 + (p1 >> 7);                                                 /* 0..32767 */
    if (P->strike) {
        int32_t increment = (int32_t)inc_full, width = (3 * p1) >> 1;
        P->shift = 0;
        while (increment > (2 << 22)) {
            increment >>= 1;
            P->shift++;
        }
        P->size = 1024u >> P->shift;
        P->mask = P->size - 1;
        P->write_ptr = 0;
        P->max_inc = inc_full << 1;
        P->inc = inc_full;
        P->init_ptr = (uint32_t)((int64_t)P->size * (8192 + width) >> 16);
        P->strike = 0;
        for (i = 0; i < NANO_PL_DL; i++)
            dl[i] = 0;
    }
    P->inc = inc_full < P->max_inc ? inc_full : P->max_inc;
    update_prob = p0 < 16384 ? 65535u : (uint32_t)(131072 - (p0 >> 3) * 31);
    loss = 4096 - (int32_t)(inc_full >> 14);
    loss = loss < 256 ? 256 : loss;
    loss = p0 < 16384 ? (loss * (16384 - p0)) >> 14 : 0;
    prev = P->prev;
    rng = P->rng;
    tsvf_coef(&flt, cut, p[P_E6]);
    for (i = 0; i < nn; i += 2) {
        int32_t sample = 0, k;
        if (P->init_ptr) {
            int32_t ex;
            P->init_ptr--;
            rng = rng * 1664525u + 1013904223u;
            ex = (dl[P->init_ptr] + 3 * (int16_t)(rng >> 16)) >> 2;
            dl[P->init_ptr] = (int16_t)ex;
            sample = ex;
        } else {
            uint32_t read_ptr, wp = P->write_ptr;
            P->phase += P->inc;
            read_ptr = ((P->phase >> (22 + P->shift)) + 2) & P->mask;
            while (wp != read_ptr) {
                uint32_t next = (wp + 1) & P->mask;
                int32_t a = dl[wp], b = dl[next];
                rng = rng * 1664525u + 1013904223u;
                if ((rng & 0xffff) <= update_prob) {
                    int32_t sum = a + b;
                    sum = sum < 0 ? -(-sum >> 1) : (sum >> 1);
                    if (loss)
                        sum = (sum * (32768 - loss)) >> 15;
                    if (bow) {                                           /* the Nano Freak addition: noise into the string while it is held */
                        rng = rng * 1664525u + 1013904223u;
                        sum += ((int16_t)(rng >> 16) * bow) >> 22;
                        sum = clamp(sum, -32767, 32767);
                    }
                    dl[wp] = (int16_t)sum;
                }
                if (wp == 0)
                    dl[P->size] = dl[0];
                wp = next;
            }
            P->write_ptr = wp;
            {
                uint32_t ph = P->phase >> P->shift;
                int32_t a = dl[ph >> 22], b = dl[(ph >> 22) + 1];
                sample = a + (((b - a) * (int32_t)((ph >> 6) & 0xffff)) >> 16);
            }
        }
        sample = clamp(sample, -32767, 32767);
        for (k = 0; k < 2; k++) {
            int32_t x = k ? sample : (prev + sample) >> 1, s = soft_knee(analog_tap(&flt, 0, x >> 1, &ic1, &ic2, 0), 16000) << 1;
            out[i + k] += voice_amp(s, m, i + k) << 1;
        }
        prev = sample;
    }
    P->prev = prev;
    P->rng = rng;
    v->s[0] = ic1;
    v->s[1] = ic2;
}

/* SAW (Braids' SAW_SYNC / SAW_COMB / CSAW, in the Nano Freak test bed): E1 WAVE (sync, comb, CS80), E2 and E3 as below.
 * SYNC: two saws, the second hard-synced to the first and E2 tunes it up to five octaves, E3 crossfades them. COMB: a saw
 * through a tuned comb, E2 detunes the comb from the key (63 is in tune), E3 is the feedback (the middle is none, up is
 * positive, down is negative). CS80: a saw with a notch that E2 moves and E3 offsets. The comb's delay line is the PLUCK
 * line (NANO_PL_V voices have one, NANO_COMB samples of it are used: the lowest comb pitch is about 43 Hz; the others sound
 * dry). State in the slot union (nano_sa_t). */
#define NANO_COMB 1024
typedef struct {
    nano_ao_t a, b;
    int32_t prev_p1, finc, fstart;
    uint32_t ptr;
} nano_sa_t;

static __attribute__((noinline)) void nano_render_sa(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    nano_su_t *U = nano_slot(t, v);
    nano_sa_t *S;
    int16_t *dl = nano_dl(t, v), x[NANO_AO_MAX], y[NANO_AO_MAX];
    uint8_t sync[NANO_AO_MAX];
    int32_t ic1 = v->s[0], ic2 = v->s[1], p0, p1, model = (p[P_E1] * 3) / 127;
    int32_t cut = (p[P_E5] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    uint32_t i, j, size, inc = m->inc;
    tsvf_t flt;
    if (!U)
        return;
    S = (nano_sa_t *)U;
    p0 = clamp((p[P_E2] << 8) + m->shape - (64 << 8), 0, 127 << 8);
    p0 = p0 + (p0 >> 7);                                                 /* 0..32767 */
    p1 = p[P_E3] * 258;
    tsvf_coef(&flt, cut, p[P_E6]);
    for (i = 0; i < n; i += size) {
        size = n - i < NANO_AO_MAX ? n - i : NANO_AO_MAX;
        if (model == 0) {
            int32_t start = S->prev_p1, delta = p1 - S->prev_p1, incr = 32767 / (int32_t)size, xx = 0;
            nano_ao_render(&S->a, NANO_A_SAW, inc, 0, 0, 0, x, sync, size);
            nano_ao_render(&S->b, NANO_A_SAW, nano_inc_c(inc, 768u + (uint32_t)(p0 >> 3), 0x80000000u), 0, 0, sync, y, 0, size);
            for (j = 0; j < size; j++) {
                int32_t q, bal;
                xx += incr;
                q = start + (delta * xx >> 15);
                bal = q << 1;
                x[j] = (int16_t)((((x[j] * (65535 - bal) + y[j] * bal) >> 16) >> 2) * 3);
            }
            S->prev_p1 = p1;
        } else if (model == 1) {
            uint32_t target, q, di, df, res_i;
            int32_t res;
            nano_ao_render(&S->a, NANO_A_SAW, inc, 0, 0, 0, x, 0, size);
            target = nano_inc_c(inc >> 5, 768u + 3840u + (uint32_t)((p0 - 16384) >> 2), 0x48000000u);   /* the key's increment, p0 semitones off */
            if (!S->fstart) {
                S->finc = (int32_t)target;
                S->fstart = 1;
            }
            S->finc = (int32_t)((15u * (uint32_t)S->finc + target) >> 4);
            q = 0xFFFFFFFFu / ((uint32_t)S->finc >> 8 ? (uint32_t)S->finc >> 8 : 1u);   /* the delay, 2^32 / inc samples, in Q8 */
            if (q > ((NANO_COMB - 2) << 8))
                q = (NANO_COMB - 2) << 8;
            di = q >> 8;
            df = (q & 255u) << 8;
            res_i = (uint32_t)(p1 << 1) > 65535u ? 65535u : (uint32_t)(p1 << 1);
            {
                int32_t a0 = NANO_BOD[res_i >> 8], b0 = NANO_BOD[(res_i >> 8) + 1];
                res = a0 + (((b0 - a0) * (int32_t)(res_i & 0xff)) >> 8);
            }
            if (dl) {
                uint32_t ptr = S->ptr & (NANO_COMB - 1);
                for (j = 0; j < size; j++) {
                    int32_t in = x[j], a0, b0, delayed, fb, o;
                    uint32_t off = ptr + 2 * NANO_COMB - di;
                    a0 = dl[off & (NANO_COMB - 1)];
                    b0 = dl[(off - 1) & (NANO_COMB - 1)];
                    delayed = a0 + (((b0 - a0) * (int32_t)(df >> 1)) >> 15);
                    fb = ((delayed * res) >> 15) + (in >> 1);
                    fb = clamp(fb, -32767, 32767);
                    dl[ptr] = (int16_t)fb;
                    o = clamp((in + (delayed << 1)) >> 1, -32767, 32767);
                    x[j] = (int16_t)o;
                    ptr = (ptr + 1) & (NANO_COMB - 1);
                }
                S->ptr = ptr;
            }
        } else {
            int32_t shift;
            nano_ao_render(&S->a, NANO_A_CSAW, inc, p0, p1, 0, x, 0, size);
            shift = (32767 - p1) >> 4;
            for (j = 0; j < size; j++)
                x[j] = (int16_t)clamp(((x[j] + shift) * 13) >> 3, -32767, 32767);
        }
        for (j = 0; j < size; j++) {
            int32_t s = soft_knee(analog_tap(&flt, 0, x[j] >> 1, &ic1, &ic2, 0), 16000) << 1;
            out[i + j] += voice_amp(s, m, i + j) << 1;
        }
    }
    v->s[0] = ic1;
    v->s[1] = ic2;
}

/* WAVETABLE (the Nano Freak test bed's player, after Braids' RenderWavetables): 16 tables of 16 single-cycle waves of 129 int8,
 * linear interpolation along the cycle and between neighbouring waves, two readings a sample at half the increment. E1 TABLE,
 * E2 POSITION (smoothed over the block), E3 CHORUS (a second reader up to 10 cents sharp, crossfaded in), E4 MORPH (a
 * crossfade toward the same position in the next table). State in the slot union (nano_wt_t). */
typedef struct {
    uint32_t phase, phase2;
    int32_t pos, started;
} nano_wt_t;

/* one wave read at a phase, Q8 (an int8 of 127 is 32512) */
static inline int32_t nano_wt_rd(const int8_t *w, uint32_t ph)
{
    uint32_t i = ph >> 25;
    int32_t a = w[i], b = w[i + 1];
    return (a << 8) + (((b - a) * (int32_t)((ph >> 9) & 0xffffu)) >> 8);
}

static __attribute__((noinline)) void nano_render_wt(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    nano_su_t *U = nano_slot(t, v);
    nano_wt_t *W;
    const int8_t *base0, *base1;
    int32_t ic1 = v->s[0], ic2 = v->s[1], tab = (p[P_E1] * 16) / 127, p1, target, pos, dpos, cq = p[P_E3] * 258, mq = p[P_E4] * 258;
    int32_t cut = (p[P_E5] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    uint32_t i, inc = (m->inc > 1932735283u ? 1932735283u : m->inc) >> 1, inc2, ph, ph2, d;
    tsvf_t flt;
    if (!U)
        return;
    W = (nano_wt_t *)U;
    if (tab > 15)
        tab = 15;
    p1 = clamp((p[P_E2] << 8) + m->shape - (64 << 8), 0, 127 << 8);
    p1 = p1 + (p1 >> 7);                                                 /* 0..32767 */
    target = p1 * 30;                                                    /* 0 .. 15 waves, Q16 */
    if (!W->started) {
        W->pos = target;
        W->started = 1;
    }
    pos = W->pos;
    dpos = (target - pos) / (int32_t)n;
    d = (((inc >> 12) * (uint32_t)(cq >> 3)) >> 6) * 49u >> 7;           /* inc x 0.006 x CHORUS */
    inc2 = inc + d;
    ph = W->phase;
    ph2 = W->phase2;
    base0 = NANO_WT + tab * 16 * 129;
    base1 = NANO_WT + ((tab + 1) & 15) * 16 * 129;
    tsvf_coef(&flt, cut, p[P_E6]);
    for (i = 0; i < n; i++) {
        int32_t ip, fr, acc = 0, k, x;
        const int8_t *a0, *a1, *b0, *b1;
        pos += dpos;
        ip = pos >> 16;
        if (ip > 14)
            ip = 14;
        if (ip < 0)
            ip = 0;
        fr = pos - (ip << 16);
        if (fr > 65536)
            fr = 65536;
        a0 = base0 + ip * 129;
        a1 = a0 + 129;
        b0 = base1 + ip * 129;
        b1 = b0 + 129;
        for (k = 0; k < 2; k++) {
            int32_t sa, c, w;
            ph += inc;
            sa = nano_wt_rd(a0, ph);
            sa += (((nano_wt_rd(a1, ph) - sa) >> 1) * (fr >> 1)) >> 15;
            if (mq > 0) {
                w = nano_wt_rd(b0, ph);
                w += (((nano_wt_rd(b1, ph) - w) >> 1) * (fr >> 1)) >> 15;
                sa += (((w - sa) >> 1) * mq) >> 14;
            }
            if (cq > 0) {
                ph2 += inc2;
                c = nano_wt_rd(a0, ph2);
                c += (((nano_wt_rd(a1, ph2) - c) >> 1) * (fr >> 1)) >> 15;
                if (mq > 0) {
                    w = nano_wt_rd(b0, ph2);
                    w += (((nano_wt_rd(b1, ph2) - w) >> 1) * (fr >> 1)) >> 15;
                    c += (((w - c) >> 1) * mq) >> 14;
                }
                sa += (((c - sa) >> 1) * cq) >> 15;
            }
            acc += sa;
        }
        x = acc >> 1;
        x = soft_knee(analog_tap(&flt, 0, x >> 1, &ic1, &ic2, 0), 16000) << 1;
        out[i] += voice_amp(x, m, i) << 1;
    }
    W->phase = ph;
    W->phase2 = ph2;
    W->pos = target;
    v->s[0] = ic1;
    v->s[1] = ic2;
}

/* V.ANALOG (Plaits' virtual analog, variant 2, in the Nano Freak test bed): a hard-synced square (the "double varishape" out
 * of Plaits' two) mixed with a variable saw / triangle / notch saw. E1 DETUNE (the saw's interval from the key: unison in the
 * middle, fifths and octaves toward the ends), E2 SYNC (the square's width, its sync ratio, its level), E3 SHAPE (the saw's
 * width and notch, the balance). Plaits' own monster-sync output is not used by the test bed and is not ported. Values are
 * Q16 (65536 is one), phases and increments u32 (2^32 is one cycle). State in the slot union (nano_va_t). */
typedef struct {
    uint32_t mph, sph, ppw, high, mf, sf, aph, appw, ahigh, af, started;
    int32_t nxt, anxt, aws, sg, wg;
} nano_va_t;

static inline int32_t nano_vb_this(uint32_t t) { if (t > 65535u) t = 65535u; return (int32_t)((t * t) >> 17); }
static inline int32_t nano_vb_next(uint32_t t) { if (t > 65535u) t = 65535u; t = 65536u - t; return -(int32_t)((t * t) >> 17); }
static inline int32_t nano_vi_next(uint32_t t)
{
    int32_t t1, t2, t4;
    if (t > 65536u)
        t = 65536u;
    t1 = (int32_t)(t >> 1);
    t2 = (t1 * t1) >> 16;
    t4 = (t2 * t2) >> 16;
    return 12288 - t1 + ((3 * t2) >> 1) - t4;
}
static inline int32_t nano_vi_this(uint32_t t) { if (t > 65535u) t = 65535u; return nano_vi_next(65536u - t); }
static inline int32_t nano_squash15(int32_t x) { int32_t x2 = (x * x) >> 15; return (x2 * ((3 << 15) - 2 * x)) >> 15; }
static inline uint32_t nano_vd(int32_t d) { return d < 1 ? 1u : (d > 65535 ? 65535u : (uint32_t)d); }   /* a divisor in Q16 steps */

static __attribute__((noinline)) void nano_render_va(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static const int32_t ivl[5] = {0, 449, 769, 1217, 1537};             /* 0, 7.01, 12.01, 19.01, 24.01 semitones, Q6 */
    const int16_t *p = t->p;
    nano_su_t *U = nano_slot(t, v);
    nano_va_t *V;
    int32_t hq = p[P_E1] * 258, tq, mq = p[P_E3] * 258, d, d4, ip, fp, sq, s64, r6s, sqg, swg, sgt, wgt, nstep = (int32_t)n;
    int32_t ic1 = v->s[0], ic2 = v->s[1], ws_t, sw, wgv, sgv, mfi, sfi, afi, wsi, sgi, wgi, aws;
    int32_t cut = (p[P_E5] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    uint32_t i, inc = m->inc > 0x40000000u ? 0x40000000u : m->inc, mf_t, sf_t, af_t, sqp, ap, ap16, su, sd, mf, sf, af, sgn;
    tsvf_t flt;
    if (!U)
        return;
    V = (nano_va_t *)U;
    tq = clamp((p[P_E2] << 8) + m->shape - (64 << 8), 0, 127 << 8);
    tq = tq + (tq >> 7);                                                 /* 0..32767 */
    d = ((hq >> 1) * 67174 >> 14) - 33587;                               /* 2.05 h - 1.025 */
    d = clamp(d, -32768, 32768);
    sgn = d < 0;
    if (d < 0)
        d = -d;
    d4 = ((d >> 1) * 131068) >> 14;                                      /* x 3.9999, Q15 */
    ip = d4 >> 15;
    fp = d4 & 32767;
    sq = nano_squash15(nano_squash15(fp));
    s64 = ivl[ip] + (((ivl[ip + 1] - ivl[ip]) * sq) >> 15);
    r6s = 2304 + (sgn ? -s64 : s64);                                     /* the detune in semitones x 64, from two octaves down */
    af_t = nano_inc_c(inc >> 2, (uint32_t)r6s, 0x40000000u);
    mf_t = inc;
    if (tq > 16384) {
        int32_t x = tq - 16384;
        sf_t = nano_inc_c(inc, 768u + (uint32_t)((((x * x) >> 15) * 12288) >> 15), 0x40000000u);   /* (t - 0.5)^2 x 4 x 48 semitones */
    } else {
        sf_t = inc;
    }
    {   /* the square's width 1.3 t - 0.15, 0.005 .. 0.5 */
        int32_t w16 = (((tq >> 1) * 85197) >> 14) - 9830;
        w16 = clamp(w16, 328, 32768);
        sqp = w16 >= 32768 ? 0x7FFFFFFFu : (uint32_t)w16 << 16;
    }
    if (sqp < 2u * sf_t)
        sqp = 2u * sf_t;
    if (sqp > 0u - 2u * sf_t)
        sqp = 0u - 2u * sf_t;
    {   /* the saw's width, its shape toward the notch, its gain */
        int32_t w = mq < 16384 ? mq + 16384 : 65536 - 2 * mq;
        w = clamp((w * 36045) >> 15, 164, 32768);
        ap = w >= 32768 ? 0xFFFFFFFFu : (uint32_t)w << 17;
        if (ap < 2u * af_t)
            ap = 2u * af_t;
        if (ap > 0u - 2u * af_t)
            ap = 0u - 2u * af_t;
        ws_t = clamp(655360 - 42 * mq, 0, 65536);
        swg = clamp(8 * (65536 - 2 * mq), 1311, 65536);
    }
    sqg = tq * 16 > 65536 ? 65536 : tq * 16;
    if (sqg >= swg) {
        sgt = 19661;
        wgt = (int32_t)(((uint32_t)swg << 15) / (uint32_t)sqg);
    } else {
        wgt = 32768;
        sgt = (int32_t)((((uint32_t)sqg << 15) / (uint32_t)swg) * 39322u >> 16);
    }
    if (!V->started) {
        V->started = 1;
        V->mf = mf_t;
        V->sf = sf_t;
        V->af = af_t;
        V->aws = ws_t;
        V->sg = sgt;
        V->wg = wgt;
        V->ppw = sqp;
        V->appw = ap;
    }
    mfi = (int32_t)(mf_t - V->mf) / nstep;
    sfi = (int32_t)(sf_t - V->sf) / nstep;
    afi = (int32_t)(af_t - V->af) / nstep;
    wsi = (ws_t - V->aws) / nstep;
    sgi = (sgt - V->sg) / nstep;
    wgi = (wgt - V->wg) / nstep;
    mf = V->mf;
    sf = V->sf;
    af = V->af;
    aws = V->aws;
    sgv = V->sg;
    wgv = V->wg;
    ap16 = ap >> 16 ? ap >> 16 : 1u;
    su = 0xFFFFFFFFu / ap16;
    sd = 0xFFFFFFFFu / (65536u - ap16);
    sw = 0;
    tsvf_coef(&flt, cut, p[P_E6]);
    for (i = 0; i < n; i++) {
        int32_t this_s = V->nxt, this_a = V->anxt, sqo, sawo, o, nsaw, tri_v, notch_amt, s;
        uint32_t rt = 0, wrapped, reset = 0, tdr = 0;
        mf += (uint32_t)mfi;
        sf += (uint32_t)sfi;
        af += (uint32_t)afi;
        aws += wsi;
        sgv += sgi;
        wgv += wgi;
        V->nxt = 0;
        V->anxt = 0;
        /* the hard-synced square */
        V->mph += mf;
        if (V->mph < mf) {
            uint32_t sar;
            rt = V->mph / nano_vd((int32_t)(mf >> 16));
            if (rt > 65535u)
                rt = 65535u;
            sar = V->sph + (65535u - rt) * (sf >> 16);
            reset = 1;
            if (sar < V->sph)
                tdr = 1;
            if (!V->high && sar >= sqp)
                tdr = 1;
            if (sar >= sqp) {
                this_s -= nano_vb_this(rt);
                V->nxt -= nano_vb_next(rt);
            }
        }
        V->sph += sf;
        wrapped = V->sph < sf;
        while (tdr || !reset) {
            if (!V->high) {
                int32_t den;
                uint32_t tt;
                if (!wrapped && V->sph < sqp)
                    break;
                den = (int32_t)(V->ppw - sqp) + (int32_t)sf;
                tt = (V->sph - sqp) / (den < 65536 ? 1u : (uint32_t)den >> 16);
                this_s += nano_vb_this(tt);
                V->nxt += nano_vb_next(tt);
                V->high = 1;
            }
            if (V->high) {
                uint32_t tt;
                if (!wrapped)
                    break;
                wrapped = 0;
                tt = V->sph / nano_vd((int32_t)(sf >> 16));
                this_s -= nano_vb_this(tt);
                V->nxt -= nano_vb_next(tt);
                V->high = 0;
            }
        }
        if (reset) {
            V->sph = rt * (sf >> 16);
            V->high = 0;
        }
        V->nxt += V->sph >= sqp ? 65536 : 0;
        V->ppw = sqp;
        sqo = 2 * this_s - 65536;
        /* the saw / triangle / notch saw */
        notch_amt = 65536 - aws;
        V->aph += af;
        wrapped = V->aph < af;
        if (!V->ahigh && !wrapped && V->aph >= ap) {
            int32_t tstep = (int32_t)(af / ap16) + (int32_t)(af / (65536u - ap16)), notch, den = (int32_t)(V->appw - ap) + (int32_t)af;
            uint32_t tt = (V->aph - ap) / (den < 65536 ? 1u : (uint32_t)den >> 16);
            tstep = ((tstep >> 1) * aws) >> 15;
            notch = (((78643 - (int32_t)ap16) >> 2) * notch_amt) >> 14;
            this_a += ((notch >> 1) * nano_vb_this(tt)) >> 15;
            V->anxt += ((notch >> 1) * nano_vb_next(tt)) >> 15;
            this_a -= (tstep * nano_vi_this(tt)) >> 16;
            V->anxt -= (tstep * nano_vi_next(tt)) >> 16;
            V->ahigh = 1;
        } else if (wrapped) {
            int32_t tstep = (int32_t)(af / ap16) + (int32_t)(af / (65536u - ap16)), notch = (((78643 >> 2) * notch_amt) >> 14);
            uint32_t tt = V->aph / nano_vd((int32_t)(af >> 16));
            tstep = ((tstep >> 1) * aws) >> 15;
            this_a -= ((notch >> 1) * nano_vb_this(tt)) >> 15;
            V->anxt -= ((notch >> 1) * nano_vb_next(tt)) >> 15;
            this_a += (tstep * nano_vi_this(tt)) >> 16;
            V->anxt += (tstep * nano_vi_next(tt)) >> 16;
            V->ahigh = 0;
        }
        if (V->aph < ap) {
            nsaw = (int32_t)(V->aph >> 16);
            tri_v = (int32_t)((((V->aph >> 16) * su) >> 16));
        } else {
            nsaw = 78643;
            tri_v = 65536 - (int32_t)(((((V->aph - ap) >> 16) * sd) >> 16));
        }
        V->anxt += (((nsaw >> 2) * notch_amt) >> 14) + (((tri_v >> 1) * aws) >> 15);
        V->appw = ap;
        sawo = (((2 * this_a - 65536) >> 2) * 54613) >> 14;              /* / 1.2 */
        o = (((sawo >> 1) * wgv) >> 15) + (((sqo >> 1) * sgv) >> 15);
        s = soft_knee(analog_tap(&flt, 0, o >> 2, &ic1, &ic2, 0), 16000) << 1;
        out[i] += voice_amp(s, m, i) << 1;
    }
    (void)sw;
    V->mf = mf;
    V->sf = sf;
    V->af = af;
    V->aws = aws;
    V->sg = sgv;
    V->wg = wgv;
    v->s[0] = ic1;
    v->s[1] = ic2;
}

/* A precise state variable filter for resonators, as Rings has them (Rings' own tan approximation, worked out per block in 64 bit).
 * The trapezoidal SVF of nano_svf_coef, rearranged for a low frequency: a1 = 1 - b1 and the small coefficients b1 (about g^2 + g k),
 * a2 (about g) and a3 (about g^2) are 12 bit mantissas with their own shifts, and the two products that add only a hundredth of the
 * state or less (b1 s1 and a3 v3) keep their remainder from one sample to the next, so that a low mode holds its tuning and its
 * decay (a Q13 a3 is a few units at 220 Hz). k24 is the damping 1 / Q, Q24. Per mode state: s1, s2 and the two remainders. */
typedef struct {
    int32_t b1r, a2r, a3r, nb1, n2, n3;                                  /* the shifts include the 13 of the mantissa */
} nano_hs_t;

static void nano_hs_coef(nano_hs_t *c, uint32_t inc, uint32_t k24, int dirty)
{
    uint64_t f28, f2, inner, term, g24, gg, d48, t1, t2, t3;
    uint32_t a1_16;
    int32_t n;
    if (inc > 0x7D70A3D7u)
        inc = 0x7D70A3D7u;
    f28 = inc >> 4;
    f2 = (f28 * f28) >> 28;
    if (dirty)                                                           /* the exciter's own approximation: f (pi + 0.3736 pi^3 f^2) */
        term = 843314857ull + ((f2 * 3109541550ull) >> 28);
    else {
        inner = 2713357990ull + ((14975313081ull * f2) >> 28);
        term = 843314857ull + ((f2 * inner) >> 28);
    }
    g24 = ((f28 * term) >> 28) >> 4;                                     /* tan(pi f), Q24 */
    if (g24 < 1)
        g24 = 1;
    gg = g24 * (g24 + k24);                                              /* g (g + k), Q48 */
    d48 = (1ull << 48) + gg;
    a1_16 = 0xFFFFFFFFu / (uint32_t)(d48 >> 32);                         /* 1 / (1 + g (g + k)), Q16 */
    t1 = (gg >> 16) * a1_16;                                             /* b1 = 1 - a1, Q48 */
    t2 = g24 * a1_16;                                                    /* a2, Q40 */
    t3 = ((g24 * g24) >> 16) * a1_16;                                    /* a3, Q48 */
    for (n = 0; n < 17 && (t1 >> (35 - n)) < 4096u; n++)
        ;
    c->nb1 = 13 + n;
    c->b1r = (int32_t)(t1 >> (35 - n));
    for (n = 0; n < 17 && (t2 >> (27 - n)) < 4096u; n++)
        ;
    c->n2 = 13 + n;
    c->a2r = (int32_t)(t2 >> (27 - n));
    for (n = 0; n < 17 && (t3 >> (35 - n)) < 4096u; n++)
        ;
    c->n3 = 13 + n;
    c->a3r = (int32_t)(t3 >> (35 - n));
    if (c->b1r > 8191)
        c->b1r = 8191;
    if (c->a2r > 8191)
        c->a2r = 8191;
    if (c->a3r > 8191)
        c->a3r = 8191;
}

/* one sample: st[0], st[1] the states, st[2]..st[5] the remainders; returns the band-pass, the low-pass goes to *lp */
static inline int32_t nano_hs_step(const nano_hs_t *c, int32_t *st, int32_t in, int32_t *lp)
{
    int32_t s1 = st[0], s2 = st[1], v3 = in - s2, acc, d1, e1, e2, e3, v1, v2;
    acc = c->b1r * s1 + st[2];
    d1 = acc >> c->nb1;
    st[2] = acc - (d1 << c->nb1);
    acc = c->a3r * v3 + st[3];
    e3 = acc >> c->n3;
    st[3] = acc - (e3 << c->n3);
    acc = c->a2r * v3 + st[4];
    e1 = acc >> c->n2;
    st[4] = acc - (e1 << c->n2);
    acc = c->a2r * s1 + st[5];
    e2 = acc >> c->n2;
    st[5] = acc - (e2 << c->n2);
    v1 = s1 - d1 + e1;
    v2 = s2 + e2 + e3;
    st[0] = clamp(2 * v1 - s1, -65536, 65536);
    st[1] = clamp(2 * v2 - s2, -65536, 65536);
    *lp = v2;
    return v1;
}

/* RINGS (Mutable Instruments Rings, MIT, as in the Nano Freak test bed): E1 MODEL, E2 STRUCTURE, E3 BRIGHTNESS, E4 DAMPING, and
 * POSITION on E7 (KTR is not used by this type: the filter does not track the key). Models: 0 MODAL (the resonator bank struck at the
 * note-on); 3 FM (below); 1 SYMPA and 4 QUANT are still to come and are silent; 2 STRING (below). MODAL: twelve modes (Rings' 64 / 4 - 4) at the
 * harmonics of the key stretched by STRUCTURE's stiffness, the odd ones minus the even ones weighted by a cosine of POSITION.
 * Only the first NANO_MODAL_POLY voices sound. Left out against Rings: the exciter's 2^(2 f^2) term;
 * State in the slot union (nano_ri_t). */
typedef struct {
    int32_t st[NANO_MODES][6];                           /* the resonators: two states and four remainders */
    int32_t ex[6], strike;
} nano_ri_t;

/* RINGS' FM voice (rings/dsp/fm_voice.cc, no audio input so the envelope follower is just the strike): a carrier phase-modulated by a
 * modulator at a ratio picked by STRUCTURE, the modulation depth set by BRIGHTNESS with an envelope, DAMPING the envelopes' decay,
 * POSITION (E7) the feedback (below the middle the modulator's, from phase feedback; above it the modulator's self feedback).
 * State overlays the modal bank's (nano_rf_t); the envelopes decay once a block. Q15 samples, Q30 envelopes. */
typedef struct {
    uint32_t cph, mph;
    int32_t prev, amp_env, bri_env, gain, amount, started;
} nano_rf_t;

/* 2^(e / 32768), Q16, for e from 0 to 8 octaves */
static uint32_t nano_pow2_q16(uint32_t e)
{
    uint32_t oct = e >> 15, fr = e & 32767u, ix = (fr * 24u) >> 15, f5 = ((fr * 24u) & 32767u) >> 10, a = NANO_P2[ix], b = NANO_P2[ix + 1];
    return (a + (((b - a) * f5) >> 5)) << oct;
}

static void nano_render_rf(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m, nano_ri_t *S)
{
    const int16_t *p = t->p;
    nano_rf_t *F = (nano_rf_t *)S->st;
    int32_t hq, bq, dq, pq, fbp, mfb, pf, bv, amin, amax, env_amt, ic1 = v->s[0], ic2 = v->s[1], amp_e, bri_e, i, target, prev, gain, amount, aslew;
    int32_t cut = (p[P_E5] << 8) + m->cutoff;
    uint32_t inc = m->inc > 0x7FFFFFFFu ? 0x7FFFFFFFu : m->inc, minc, ratio6, cph, mph, ix, fx;
    tsvf_t flt;
    hq = clamp((p[P_E2] << 8) + m->shape - (64 << 8), 0, 127 << 8);
    hq = hq + (hq >> 7);
    bq = p[P_E3] * 258;
    dq = p[P_E4] * 258;
    pq = p[P_E7] * 258;
    ix = (uint32_t)hq >> 8;
    fx = (uint32_t)hq & 255u;
    ratio6 = NANO_RFMQ[ix] + (((NANO_RFMQ[ix + 1] - NANO_RFMQ[ix]) * fx) >> 8);
    minc = nano_inc_c(inc, ratio6, 0x7FFFFFFFu);
    if (!F->started) {
        F->started = 1;
        F->amp_env = F->bri_env = 1 << 30;
    }
    if (S->strike) {
        F->amp_env = F->bri_env = 1 << 30;
        S->strike = 0;
    }
    env_amt = dq < 29491 ? 65536 : (int32_t)(((32768 - dq) * 10 * 2));                 /* 1 below 0.9, else (1 - damping) 10; Q16 */
    {   /* the envelopes' decay over this block: e^-x, x = 6.9078 n / rt60, rt60 = 0.1 s 2^(8 damping) and 2^(7 damping) */
        uint32_t x1 = (uint32_t)(((uint64_t)n * 6727000u) / nano_pow2_q16((uint32_t)dq * 8u));        /* Q16: 6.9078 n / (0.1 s x 44100 x 2^(8 d)) */
        uint32_t x2 = (uint32_t)(((uint64_t)n * 6727000u) / nano_pow2_q16((uint32_t)dq * 7u));
        uint32_t f1 = 65536u - x1 + ((x1 * x1) >> 17) - (((((x1 * x1) >> 16) * x1) >> 16) * 10923u >> 16);
        uint32_t f2 = 65536u - x2 + ((x2 * x2) >> 17) - (((((x2 * x2) >> 16) * x2) >> 16) * 10923u >> 16);
        F->amp_env = (int32_t)(((uint64_t)F->amp_env * (f1 > 65536u ? 0 : f1)) >> 16);
        F->bri_env = (int32_t)(((uint64_t)F->bri_env * (f2 > 65536u ? 0 : f2)) >> 16);
    }
    amp_e = F->amp_env >> 15;                                                            /* Q15 */
    bri_e = F->bri_env >> 15;
    bv = (bq * bq) >> 15;                                                                /* brightness^2 */
    amin = bv < 16384 ? 0 : bv * 2 - 32768;
    amax = bv < 16384 ? bv * 2 : 32768;
    {
        int32_t fe = 16384 + (((bri_e - 16384) * (env_amt >> 1)) >> 15);                 /* 0.5 + envelope_amount (env - 0.5) */
        target = (amin + ((amax * fe) >> 15)) * 2;                                      /* Q15, up to 4 */
    }
    aslew = 164 + ((amax * 492) >> 15);                                                  /* 0.005 + 0.015 max */
    fbp = (pq - 16384) * 2;                                                              /* feedback_param, Q15 -1..1 */
    pf = fbp < 0 ? ((fbp * fbp) >> 16) : 0;                                              /* 0.5 fb^2 for fb < 0 */
    mfb = fbp > 0 ? ((fbp * fbp) >> 17) : 0;                                             /* 0.25 fb^2 for fb > 0 */
    cph = F->cph;
    mph = F->mph;
    prev = F->prev;
    gain = F->gain;
    amount = F->amount;
    tsvf_coef(&flt, cut, p[P_E6]);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t e = target - amount, mod, car, g_t, s, k, pp;
        uint32_t minc2;
        e = e > aslew ? aslew : (e < -aslew ? -aslew : e);
        amount += e;
        pp = (prev * pf) >> 15;
        minc2 = minc + (uint32_t)(int32_t)(((int32_t)(minc >> 16) * pp) << 1);
        mph += minc2;
        cph += inc;
        mod = sine_i(mph + (uint32_t)(((prev * mfb) >> 15) << 17));
        car = sine_i(cph + (uint32_t)((int32_t)(((int64_t)amount * mod) >> 15) << 17));
        prev += ((car - prev) * 3277) >> 15;
        g_t = 32768 - (((32768 - amp_e) * (env_amt >> 1)) >> 15);                        /* 1 + envelope_amount (amp_env - 1) */
        k = 164 + (((amount > 131071 ? 131071 : amount) * 1475) >> 15);
        gain += ((g_t - gain) * (k > 32768 ? 32768 : k)) >> 15;
        s = (car * gain) >> 15;
        s = (s * 22938) >> 15;                                                           /* the model gain 0.7 */
        s = soft_knee(analog_tap(&flt, 0, s >> 1, &ic1, &ic2, 0), 16000) << 1;
        out[i] += voice_amp(s, m, (uint32_t)i) << 1;
    }
    F->cph = cph;
    F->mph = mph;
    F->prev = prev;
    F->gain = gain;
    F->amount = amount;
    v->s[0] = ic1;
    v->s[1] = ic2;
}

/* RINGS' STRING model (rings/dsp/string.cc and plucker.h, as in the Nano Freak test bed): a Karplus-Strong string excited by a plucker
 * (a burst of noise through a comb whose delay is POSITION, then a low-pass at the brightness), damped each pass by Rings' damping filter
 * (a two-tap FIR whose BRIGHTNESS sets the highs, then a low-pass at the damping cutoff) whose gain gives the DAMPING decay. STRUCTURE below
 * the middle bends the bridge (the pitch dips with the amplitude), above it stretches the string with an all-pass (and, near the top, a
 * noise on the delay). The output is the string minus the same line read at POSITION (a comb). The three lines share the voice's PLUCK
 * region: the string 512 samples, the stretch all-pass 256 and the plucker's comb 256, so a note below about 86 Hz runs the string at a
 * half or a quarter of the sample rate (as Rings does below its delay line) and the lowest note is about 22 Hz. Left out against Rings:
 * linear reads in place of Hermite, the dispersion ramps (they hold their block's value), the voice is monophonic (one string per voice).
 * Internal amplitude Q13 (1.0 = 8192). State overlays the modal bank's (nano_rs_t). */
#define NANO_RS_STR 512
typedef struct {
    nano_hs_t iirc, plc;
    int32_t iir[6], pls[6];
    int32_t x, x1, res, c0, c1, c0inc, c1inc, delay, srcph;
    uint32_t wp, awp, cwp, rng, remaining, period;
    int32_t gain, pdcx, pdcy, pdcr, acx, acy, acr, curved, dnoise, out0, out1, aux0, aux1;
} nano_rs_t;

_Static_assert(sizeof(nano_rs_t) <= sizeof(((nano_ri_t *)0)->st), "nano_rs_t overlays the modal bank");

static inline uint32_t nano_mulq16(uint32_t a, uint32_t b16)
{
    return (a >> 16) * b16 + (((a & 0xffffu) * b16) >> 16);
}

static inline int32_t nano_rs_rd(const int16_t *l, uint32_t mask, uint32_t wp, uint32_t d)
{
    uint32_t i0 = (wp + (d >> 16)) & mask;
    int32_t a = l[i0], b = l[(i0 + 1) & mask];
    return a + (((b - a) * (int32_t)((d & 0xffffu) >> 1)) >> 15);
}

static void nano_render_rs(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m, nano_ri_t *S)
{
    const int16_t *p = t->p;
    int16_t *sl = nano_dl(t, v), *al, *cl;
    nano_rs_t *R = (nano_rs_t *)S->st;
    int32_t tmp[CTL];
    int32_t hq, bq, dq, pq, disp, ic1 = v->s[0], ic2 = v->s[1], cut = (p[P_E5] << 8) + m->cutoff, i, ti, cut6, comp16, pos16, br16, damp16;
    int32_t sp16 = 0, apg = 0, noa = 0, bcv = 0, acm = 0, nf12 = 0, invn = 0, dstep;
    uint32_t f0 = m->inc > 0x7D70A3D7u ? 0x7D70A3D7u : m->inc, dtot, d, shift = 0, ratio16, wp, awp;
    tsvf_t flt;
    if (!sl)
        return;
    al = sl + NANO_RS_STR;
    cl = al + 256;
    if (f0 < 65536u)
        f0 = 65536u;
    hq = clamp((p[P_E2] << 8) + m->shape - (64 << 8), 0, 127 << 8);
    hq = hq + (hq >> 7);
    bq = p[P_E3] * 258;
    dq = p[P_E4] * 258;
    pq = p[P_E7] * 258;
    dtot = (uint32_t)((1ull << 48) / f0);                                /* 1 / frequency, Q16 samples */
    d = dtot < (4u << 16) ? (4u << 16) : dtot;
    while (d > (500u << 16) && shift < 2) {
        d >>= 1;
        shift++;
    }
    if (d > (504u << 16))
        d = 504u << 16;
    ratio16 = 65536u >> shift;
    pos16 = (16384 - (((pq > 16384 ? pq - 16384 : 16384 - pq) * 32113) >> 15)) << 1;            /* 0.5 - 0.98 |position - 0.5|, Q16 */
    disp = hq < 7864 ? ((hq - 7864) * 136511) >> 15 : (hq > 8520 ? ((hq - 8520) * 44283) >> 15 : 0);
    if (disp > 0) {
        int32_t a = (disp * (65536 - disp)) >> 15, na = disp > 24576 ? (disp - 24576) * 4 : 0;
        sp16 = (a * 31130) >> 15;
        apg = -(int32_t)((20251 * disp) / (4915 + disp));
        na = na > 32767 ? 32767 : na;
        noa = (((na * na) >> 15) * 819) >> 15;
    } else if (disp < 0) {
        acm = -disp;
        bcv = (((acm * acm) >> 15) * 328) >> 15;
    }
    {   /* the noise filter 2^(4 (b - 1)) and its 1 / (0.2 + f) */
        uint32_t val = nano_pow2_q16((uint32_t)(4 * (32768 - bq)));
        int32_t nf16 = (int32_t)(0xFFFFFFFFu / val);
        nf12 = nf16 >> 4;
        invn = 268435456 / (13107 + nf16);
    }
    {   /* the damping: the loss per pass for a decay of 0.07 2^(8 lf) s, the filter's brightness and cutoff, and the shift of its delay */
        int32_t lf = (dq * (65536 - dq)) >> 15, xi, b2 = (bq * bq) >> 14;
        uint64_t val = nano_pow2_q16((uint32_t)(lf * 8)), x = (((uint64_t)10 * dtot) << 30) / (3087ull * val), y, term, r;
        uint32_t k;
        if (x > 11364000000ull)
            x = 11364000000ull;
        xi = (int32_t)(x >> 30);
        y = ((x & 0x3FFFFFFFull) * 744261118ull) >> 30;
        r = 1ull << 30;
        term = r;
        for (k = 1; k < 8; k++) {
            term = ((term * y) >> 30) / k;
            r = (k & 1) ? r - term : r + term;
        }
        damp16 = xi + 14 > 31 ? 0 : (int32_t)(r >> (xi + 14));
        br16 = b2;
        cut6 = 1536 + ((((dq * dq) >> 15) * 3072) >> 15) + ((((bq * bq) >> 15) * 1536) >> 15);
        cut6 = cut6 > 5376 ? 5376 : cut6;
        ti = dq >= 31130 ? (dq - 31130) * 20 : 0;
        ti = ti > 32767 ? 32767 : ti;
        damp16 += (ti * (65536 - damp16)) >> 15;
        br16 += (ti * (65536 - br16)) >> 15;
        cut6 += (ti * (8192 - cut6)) >> 15;
        damp16 = damp16 > 65535 ? 65535 : damp16;
    }
    {
        int32_t ix = cut6 >> 6, fr = cut6 & 63;
        comp16 = NANO_RSVF[ix] + (((int32_t)NANO_RSVF[ix + 1] - NANO_RSVF[ix]) * fr >> 6);
    }
    {
        int32_t c0t = (int32_t)(((uint32_t)damp16 * (uint32_t)((65536 + br16) >> 1)) >> 8), c1t = (int32_t)(((uint32_t)damp16 * (uint32_t)((65536 - br16) >> 2)) >> 8);
        if (S->strike) {
            int32_t c = (bq * (65536 - bq)) >> 15, ratio15 = ((pq * 29491) >> 15) + 1638;
            uint32_t cp = (uint32_t)(((uint64_t)ratio15 * dtot) >> 15), k;
            for (k = 0; k < sizeof *R / 4; k++)
                ((uint32_t *)R)[k] = 0;
            for (k = 0; k < NANO_PL_DL; k++)
                sl[k] = 0;
#ifdef NANO_RS_TESTSEED
            R->rng = 0x21u;                                              /* the reference comparison runs both on Rings' own seed */
#else
            R->rng = (0x21u ^ (v->age * 2654435761u) ^ ((uint32_t)v->note << 16)) | 1u;
#endif
            R->remaining = cp >> 16;
            while (cp >= (255u << 16))
                cp >>= 1;
            R->period = cp;
            R->gain = ((32768 - pq) * 26214) >> 15;
            nano_hs_coef(&R->plc, nano_inc_c(nano_inc_c(f0, (uint32_t)(768 + (((c - 16384) * 96 * 64) >> 15)), 0x7DF3B645u), 768 + 36 * 64, 0x7DF3B645u), 1u << 24, 1);
            R->delay = (int32_t)d;
            R->srcph = 65536;
            R->c0 = c0t;
            R->c1 = c1t;
            S->strike = 0;
        }
        nano_hs_coef(&R->iirc, nano_inc_c(f0, (uint32_t)(768 + cut6), 0x7DF3B645u), 33554432u, 0);
        dstep = ((int32_t)d - R->delay) / (int32_t)n;
        R->c0inc = (c0t - R->c0) / (int32_t)n;
        R->c1inc = (c1t - R->c1) / (int32_t)n;
    }
    for (i = 0; i < (int32_t)n; i++) {                                   /* the plucker: noise into a comb, low-passed, DC blocked */
        int32_t x = 0, lp, tt;
        if (R->remaining) {
            R->rng = R->rng * 1664525u + 1013904223u;
            x = (int16_t)(R->rng >> 16) >> 2;
            R->remaining--;
        }
        x += (R->gain * nano_rs_rd(cl, 255, R->cwp, R->period)) >> 15;
        cl[R->cwp] = (int16_t)clamp(x, -32767, 32767);
        R->cwp = (R->cwp - 1) & 255;
        nano_hs_step(&R->plc, R->pls, x, &lp);
        tt = R->pdcy * 15 + R->pdcr;
        R->pdcr = tt & 0xffff;
        R->pdcy += lp - R->pdcx - (tt >> 16);
        R->pdcx = lp;
        tmp[i] = R->pdcy;
    }
    tsvf_coef(&flt, cut, p[P_E6]);
    wp = R->wp;
    awp = R->awp;
    for (i = 0; i < (int32_t)n; i++) {
        int32_t ob, ab, x;
        R->srcph += (int32_t)ratio16;
        if (R->srcph > 65536) {
            uint32_t dc, cd, dd, ap;
            int32_t val, lp, acc, y;
            R->srcph -= 65536;
            R->delay += dstep;
            R->c0 += R->c0inc;
            R->c1 += R->c1inc;
            dc = (uint32_t)R->delay;
            cd = nano_mulq16(dc, (uint32_t)pos16);
            dd = nano_mulq16(dc, (uint32_t)comp16) - 65536u;
            if (dd < (2u << 16))
                dd = 2u << 16;
            if (disp) {
                R->rng = R->rng * 1664525u + 1013904223u;
                val = (int16_t)(R->rng >> 16);
                val = (val * invn) >> 12;
                R->dnoise += ((val - R->dnoise) * nf12) >> 12;
                dd = nano_mulq16(dd, (uint32_t)(65536 + ((R->dnoise * noa) >> 14) - ((R->curved * bcv) >> 12)));
                ap = nano_mulq16(dd, (uint32_t)sp16);
                if (ap >= (4u << 16) && dd - ap >= (4u << 16) && (ap >> 16) < 256) {
                    int32_t rd, w;
                    val = nano_rs_rd(sl, 511, wp, dd - ap);
                    rd = al[(awp + (ap >> 16)) & 255];
                    w = clamp(val + ((apg * rd) >> 15), -32767, 32767);
                    al[awp] = (int16_t)w;
                    awp = (awp - 1) & 255;
                    val = rd - ((apg * w) >> 15);
                } else
                    val = nano_rs_rd(sl, 511, wp, dd);
                acc = R->acy * 30 + R->acr;                                  /* the AC blocking: a high-pass at 20 Hz */
                R->acr = acc & 0xffff;
                R->acy += val - R->acx - (acc >> 16);
                R->acx = val;
                val += (acm * (R->acy - val)) >> 15;
                y = (val < 0 ? -val : val) - 205;                            /* the bridge: (|v| + (|v| - 0.025)) with a sign */
                R->curved = y > 0 ? (val > 0 ? 2 * y : -3 * y) : 0;
            } else
                val = nano_rs_rd(sl, 511, wp, dd);
            val += tmp[i];
            val = clamp(val, -20000, 20000);
            acc = (R->c0 >> 8) * R->x + (R->c1 >> 8) * (val + R->x1) + R->res;      /* the damping filter */
            y = acc >> 16;
            R->res = acc - (y << 16);
            R->x1 = R->x;
            R->x = val;
            nano_hs_step(&R->iirc, R->iir, y, &lp);
            val = clamp(lp, -16000, 16000);
            sl[wp] = (int16_t)val;
            wp = (wp - 1) & 511;
            R->out1 = R->out0;
            R->aux1 = R->aux0;
            R->out0 = val;
            R->aux0 = nano_rs_rd(sl, 511, wp, cd);
        }
        ob = R->out1 + (((R->out0 - R->out1) * (R->srcph >> 1)) >> 15);
        ab = R->aux1 + (((R->aux0 - R->aux1) * (R->srcph >> 1)) >> 15);
        x = clamp(((ob - ab) * 11) >> 1, -24000, 24000);                 /* x 1.4 (Rings' gain) x 4 (Q13 to Q15) */
        x = soft_knee(analog_tap(&flt, 0, x >> 1, &ic1, &ic2, 0), 16000) << 1;
        out[i] += voice_amp(x, m, (uint32_t)i) << 1;
    }
    R->wp = wp;
    R->awp = awp;
    v->s[0] = ic1;
    v->s[1] = ic2;
}

static __attribute__((noinline)) void nano_render_ri(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    nano_su_t *U = nano_slot(t, v);
    nano_ri_t *S;
    int32_t tmp[CTL], acc[CTL], gn[NANO_MODES], model = (p[P_E1] * 5) / 127, hq, bq, dq, pq, i, mi, ic1 = v->s[0], ic2 = v->s[1];
    int32_t cut = (p[P_E5] << 8) + m->cutoff;
    uint32_t f0 = m->inc;
    tsvf_t flt;
    nano_hs_t ex, md[NANO_MODES];
    if (!U || n > CTL || (uint32_t)(v - t->v) >= NANO_MODAL_POLY)
        return;
    S = (nano_ri_t *)U;
    if (model == 3) {
        nano_render_rf(t, v, out, n, m, S);
        return;
    }
    if (model == 2) {
        nano_render_rs(t, v, out, n, m, S);
        return;
    }
    if (model != 0)
        return;                                                          /* SYMPA and QUANT are not ported yet */
    hq = clamp((p[P_E2] << 8) + m->shape - (64 << 8), 0, 127 << 8);
    hq = hq + (hq >> 7);                                                 /* STRUCTURE 0..32767 */
    bq = p[P_E3] * 258;
    dq = p[P_E4] * 258;
    pq = p[P_E7] * 258;
    {   /* the exciter: an impulse through a low-pass (Q 1.5) at the key x 2^((c - 0.5) 8), c = b (2 - b) */
        int32_t c = (bq * (65536 - bq)) >> 15, stri = 0;
        uint32_t fc = nano_inc_c(f0 >> 4, (uint32_t)(768 + 48 * 64 + (((c - 16384) * 96 * 64) >> 15)), 0x66666666u);
        nano_hs_coef(&ex, fc, 11184811u, 1);
        if (S->strike) {
            uint32_t r = 0xFFFFFFFFu / (fc >> 8 ? fc >> 8 : 1u);        /* 256 / fc */
            stri = (int32_t)(r > 3000000u ? 3000000u : r) >> 1;          /* 0.25 / fc, Q9: small enough for the 17 bit states */
        }
        {
            for (i = 0; i < (int32_t)n; i++) {
                int32_t lp;
                nano_hs_step(&ex, S->ex, i == 0 ? stri : 0, &lp);
                tmp[i] = lp << 2;                                        /* x 0.125, back from Q9, and the headroom */
                acc[i] = 0;
            }
            S->strike = 0;
        }
    }
    {   /* the resonators */
        int32_t si = hq >> 7, sf = (hq & 127) << 8, stiff = (NANO_RSTIF[si] << 4) + (int32_t)(((int64_t)(NANO_RSTIF[si + 1] - NANO_RSTIF[si]) * sf) >> 11);   /* Q24 */
        int32_t e = (int32_t)(((int64_t)dq * 217651) >> 15), qs, qm, u, bb, ql, rate, stretch = 1 << 24, st = stiff, cc, y0, y1, sign;
        {
            int32_t oct = e >> 15, fr = e & 32767, ix = (fr * 24) >> 15;
            uint32_t f5 = (((uint32_t)fr * 24u) & 32767u) >> 10, a = NANO_P2[ix], b = NANO_P2[ix + 1];
            qs = (int32_t)((((a + (((b - a) * f5) >> 5)) << oct)) >> 8);   /* Q8 */
        }
        qs = (qs * qs) >> 8;
        qm = (500 * qs) >> 8;                                            /* q = 500 x 10^(4 damping) */
        bb = (bq * bq) >> 15;                                            /* Rings is given brightness^2 */
        u = 32768 - hq;                                                  /* (1 - structure)^8 */
        u = (u * u) >> 15;
        u = (u * u) >> 15;
        u = (u * u) >> 15;
        bb = (bb * (32768 - ((u * 6554) >> 15))) >> 15;
        ql = ((((bb * (65536 - bb)) >> 15) * 27853) >> 15) + 4915;       /* b (2 - b) 0.85 + 0.15 */
        rate = (((hq * (65536 - hq)) >> 15) * 3277) >> 15;               /* structure (2 - structure) 0.1 */
        {   /* the cosine of POSITION: CosineOscillator::InitApproximate, Q15 */
            int32_t f = pq - 8192;
            sign = 16;
            if (f < 0)
                f = -f;
            else if (f > 16384)
                f -= 16384;
            else
                sign = -16;
            cc = sign * ((f * (32768 - 2 * f)) >> 15);                   /* Q15, |c| <= 2 */
            y1 = cc >> 2;
            y0 = 16384;
        }
        for (mi = 0; mi < NANO_MODES; mi++) {
            uint64_t h = (uint64_t)f0 * (uint32_t)(mi + 1);
            uint32_t mf = h > 0x90000000ull ? 0x7D70A3D7u : (uint32_t)((h * (uint32_t)stretch) >> 24), qq;
            int32_t w0 = y0, wv = w0 + 16384;                            /* the weight, 0..1 in Q15 */
            y0 = ((cc * w0) >> 15) - y1;
            y1 = w0;
            if (mf >= 0x7D70A3D7u) {
                gn[mi] = 0;
                mf = 0x7D70A3D7u;
            } else {
                gn[mi] = (mi & 1) ? -wv : wv;
            }
            qq = 256u + (uint32_t)(((uint64_t)mf * (uint32_t)qm) >> 24);   /* 1 + f q, Q8 */
            nano_hs_coef(&md[mi], mf, 0xFFFFFFFFu / qq, 0);
            stretch += st;
            st = (int32_t)(((int64_t)st * (st < 0 ? 30474 : 32113)) >> 15);
            ql += (rate * (32768 - ql)) >> 15;
            qm = (int32_t)(((int64_t)qm * ql) >> 15);
        }
    }
    for (mi = 0; mi < NANO_MODES; mi++) {
        const nano_hs_t *c = &md[mi];
        if (!gn[mi])
            continue;
        for (i = 0; i < (int32_t)n; i++) {
            int32_t lp;
            acc[i] += (gn[mi] * nano_hs_step(c, S->st[mi], tmp[i], &lp)) >> 15;
        }
    }
    tsvf_coef(&flt, cut, p[P_E6]);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t s = soft_knee(analog_tap(&flt, 0, clamp(soft_knee(acc[i], 16000), -32768, 32767) >> 1, &ic1, &ic2, 0), 16000) << 1;
        out[i] += voice_amp(s, m, (uint32_t)i) << 1;
    }
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
    if (t->p[P_E0] == 14) {                               /* RINGS: a clean slot and a strike */
        nano_su_t *U = nano_slot(t, v);
        if (U) {
            uint32_t k;
            for (k = 0; k < sizeof *U / 4; k++)
                ((uint32_t *)U)[k] = 0;
            ((nano_ri_t *)U)->strike = 1;
        }
    }
    if (t->p[P_E0] == 13) {                               /* V.ANALOG: a clean slot, everything starts at the knobs */
        nano_su_t *U = nano_slot(t, v);
        if (U) {
            uint32_t k;
            for (k = 0; k < sizeof *U / 4; k++)
                ((uint32_t *)U)[k] = 0;
        }
    }
    if (t->p[P_E0] == 12) {                               /* WAVETABLE: a clean slot, phases at zero, the position starts at the knob */
        nano_su_t *U = nano_slot(t, v);
        if (U) {
            uint32_t k;
            for (k = 0; k < sizeof *U / 4; k++)
                ((uint32_t *)U)[k] = 0;
        }
    }
    if (t->p[P_E0] == 11) {                               /* SAW: a clean slot, the comb empty, SYNC's balance at the knob */
        nano_su_t *U = nano_slot(t, v);
        if (U) {
            nano_sa_t *S = (nano_sa_t *)U;
            int16_t *dl = nano_dl(t, v);
            uint32_t k;
            for (k = 0; k < sizeof *U / 4; k++)
                ((uint32_t *)U)[k] = 0;
            S->prev_p1 = t->p[P_E3] * 258;
            S->a.disc = S->b.disc = -16383;
            for (k = 0; dl && k < NANO_PL_DL; k++)
                dl[k] = 0;
        }
    }
    if (t->p[P_E0] == 10) {                               /* BASS: a clean slot, the sub level starts at the knob */
        nano_su_t *U = nano_slot(t, v);
        if (U) {
            nano_bs_t *B = (nano_bs_t *)U;
            uint32_t k;
            for (k = 0; k < sizeof *U / 4; k++)
                ((uint32_t *)U)[k] = 0;
            B->prev_p1 = t->p[P_E3] * 258;
        }
    }
    if (t->p[P_E0] == 9) {                                /* PLUCK: the string is struck at the first block */
        nano_su_t *U = nano_slot(t, v);
        if (U) {
            nano_pl_t *P = (nano_pl_t *)U;
            uint32_t k;
            for (k = 0; k < sizeof *U / 4; k++)
                ((uint32_t *)U)[k] = 0;
            P->strike = 1;
            P->rng = (0x21u ^ (v->age * 2654435761u) ^ ((uint32_t)v->note << 16)) | 1u;
        }
    }
    if (t->p[P_E0] == 8) {                                /* HARMONIC: a clean slot, phases at zero */
        nano_su_t *U = nano_slot(t, v);
        if (U) {
            uint32_t k;
            for (k = 0; k < sizeof *U / 4; k++)
                ((uint32_t *)U)[k] = 0;
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
    if (p[P_E0] == 8) {
        nano_render_hm(t, v, out, n, m);
        return;
    }
    if (p[P_E0] == 9) {
        nano_render_pl(t, v, out, n, m);
        return;
    }
    if (p[P_E0] == 10) {
        nano_render_bs(t, v, out, n, m);
        return;
    }
    if (p[P_E0] == 11) {
        nano_render_sa(t, v, out, n, m);
        return;
    }
    if (p[P_E0] == 12) {
        nano_render_wt(t, v, out, n, m);
        return;
    }
    if (p[P_E0] == 13) {
        nano_render_va(t, v, out, n, m);
        return;
    }
    if (p[P_E0] == 14) {
        nano_render_ri(t, v, out, n, m);
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
    /* HARMONIC: TYPE PEAK WIDTH CHORUS STRETCH | CUT RES KTR */
    {"ORGAN DRAWBARS", {8, 30, 70, 20, 0, 127, 0, 64}, {2, 70, 100, 30}, 0, 0, FX(0, 20, 20, 20), PAT(0)},
    {"SPECTRAL SWEEP", {8, 70, 40, 60, 0, 127, 0, 64}, {20, 80, 100, 60}, 0, 0, FX(0, 30, 30, 30), PAT(0)},
    {"STRETCHED BELL", {8, 20, 30, 30, 70, 127, 0, 64}, {0, 80, 0, 70}, 0, 0, FX(0, 40, 30, 40), PAT(0)},
    /* PLUCK: TYPE BOW POSITION DECAY 0 | CUT RES KTR */
    {"HARP", {9, 0, 40, 90, 0, 127, 0, 64}, {0, 50, 100, 50}, 0, 0, FX(0, 30, 30, 30), PAT(0)},
    {"MUTED PLUCK", {9, 0, 70, 30, 0, 127, 0, 64}, {0, 30, 100, 30}, 0, 0, FX(0, 20, 20, 20), PAT(0)},
    {"BOWED STRING", {9, 70, 50, 110, 0, 127, 0, 64}, {20, 80, 100, 60}, 0, 0, FX(0, 30, 30, 30), PAT(0)},
    /* BASS: TYPE WAVE WIDTH SUB 0 | CUT RES KTR */
    {"SUB SQUARE", {10, 0, 0, 20, 0, 127, 0, 64}, {0, 60, 100, 40}, 0, 0, FX(0, 0, 0, 0), PAT(0)},
    {"SAW SUB LOW", {10, 90, 70, 10, 0, 100, 20, 64}, {0, 70, 100, 40}, 0, 0, FX(0, 0, 0, 0), PAT(0)},
    {"OCTAVE BASS", {10, 0, 40, 110, 0, 110, 10, 64}, {0, 60, 100, 40}, 0, 0, FX(0, 0, 0, 0), PAT(0)},
    /* SAW: TYPE WAVE E2 E3 0 | CUT RES KTR (WAVE 0 sync, 64 comb, 127 CS80) */
    {"HARD SYNC SAW", {11, 0, 50, 64, 0, 127, 0, 64}, {0, 60, 100, 40}, 0, 0, FX(0, 0, 0, 0), PAT(0)},
    {"COMB PLUCKER", {11, 64, 63, 120, 0, 127, 0, 64}, {0, 50, 60, 40}, 0, 0, FX(0, 20, 20, 20), PAT(0)},
    {"CS80 BRASS", {11, 127, 60, 90, 0, 110, 0, 64}, {20, 60, 100, 40}, 0, 0, FX(0, 20, 20, 20), PAT(0)},
    /* WAVETABLE: TYPE TABLE POSITION CHORUS MORPH | CUT RES KTR */
    {"SINSAW SWEEP", {12, 0, 40, 30, 0, 127, 0, 64}, {0, 60, 100, 40}, 0, 0, FX(0, 20, 20, 20), PAT(0)},
    {"PWM TABLE", {12, 16, 70, 60, 0, 127, 0, 64}, {0, 60, 100, 40}, 0, 0, FX(0, 20, 20, 20), PAT(0)},
    {"VOWEL TABLE", {12, 40, 50, 40, 40, 127, 0, 64}, {10, 60, 100, 50}, 0, 0, FX(0, 30, 30, 30), PAT(0)},
    /* V.ANALOG: TYPE DETUNE SYNC SHAPE 0 | CUT RES KTR */
    {"VA SYNC LEAD", {13, 64, 100, 20, 0, 127, 0, 64}, {0, 60, 100, 40}, 0, 0, FX(0, 20, 20, 20), PAT(0)},
    {"VA FIFTH SAW", {13, 90, 20, 40, 0, 127, 0, 64}, {0, 60, 100, 40}, 0, 0, FX(0, 20, 20, 20), PAT(0)},
    {"VA NOTCH PAD", {13, 70, 30, 100, 0, 110, 0, 64}, {40, 70, 100, 70}, 0, 0, FX(0, 30, 30, 30), PAT(0)},
    /* RINGS (modal model): TYPE MODEL STRUCTURE BRIGHTNESS DAMPING | CUT RES POSITION */
    {"RINGS BELL", {14, 0, 85, 90, 100, 127, 0, 64}, {0, 40, 127, 60}, 0, 0, FX(0, 30, 30, 30), PAT(0)},
    {"RINGS MARIMBA", {14, 0, 40, 50, 60, 127, 0, 45}, {0, 40, 127, 40}, 0, 0, FX(0, 20, 20, 20), PAT(0)},
    {"RINGS GLASS", {14, 0, 110, 100, 110, 127, 0, 80}, {0, 40, 127, 70}, 0, 0, FX(0, 40, 40, 40), PAT(0)},
    /* RINGS (FM model, MODEL 77) */
    {"RINGS FM BELL", {14, 77, 80, 90, 70, 127, 0, 64}, {0, 40, 127, 60}, 0, 0, FX(0, 30, 30, 30), PAT(0)},
    {"RINGS FM KEYS", {14, 77, 40, 70, 50, 127, 0, 64}, {0, 40, 127, 40}, 0, 0, FX(0, 20, 20, 20), PAT(0)},
    {"RINGS FM BASS", {14, 77, 20, 100, 40, 127, 0, 64}, {0, 40, 127, 30}, 0, 0, FX(0, 0, 0, 0), PAT(0)},
    /* RINGS (string model, MODEL 64) */
    {"RINGS GUITAR", {14, 64, 32, 90, 80, 127, 0, 30}, {0, 40, 127, 60}, 0, 0, FX(0, 20, 20, 20), PAT(0)},
    {"RINGS HARP", {14, 64, 32, 100, 100, 127, 0, 20}, {0, 40, 127, 70}, 0, 0, FX(0, 30, 30, 30), PAT(0)},
    {"RINGS PIANO", {14, 64, 70, 60, 70, 127, 0, 25}, {0, 40, 127, 50}, 0, 0, FX(0, 20, 20, 20), PAT(0)},
};

static const engine_t ENG_NANO = {
    .name = "NANO",
    .page_title = {"OSC", "FLT"},
    .edit = {
        {"TYPE", F_ENUM, 0, 14, 0, N_NANO_TYPE, 0},
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
    .mem = sizeof(nano_su_t) * NANO_SLOTS + NANO_PL_V * NANO_PL_DL * 2,
    .keep = 0x03,                /* the filter */
};
