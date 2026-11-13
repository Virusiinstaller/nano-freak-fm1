/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 The Nano Freak authors (github.com/Virusiinstaller/nano-freak-fm1) */
/* NANO (Nano Freak): the MicroFreak-style engine. Integer only, like the rest of the firmware.
 *
 * TYPE 0 is BASIC WAVES, ported from the browser test bed (nano-freak dsp/nf_bw.c, floating point, MIT):
 *   MORPH  0..0.5  a pulse crossfades into a saw; 0.5..1  a second saw (offset by SYM) fades in beside it
 *   SYM    the pulse width (it fades out as the pulse becomes a saw) and the offset of the second saw
 *   SUB    a sine an octave below
 *   SYNC   0 off; up from there a hard-synced slave at 1x..5x the pitch, restarted by the master (band-limited: the restart
 *          step is corrected with a polyBLEP, as the test bed does)
 * TYPE 1 is TWO OP FM (nano-freak dsp/nf_fm.c, Plaits): RATIO (the quantizer), INDEX, FEEDBACK on the same knobs; no oversampling.
 *
 * then a state variable low-pass (CUT RES KTR, ANALOG's own filter code) so the first builds are playable. The Nano Freak
 * filter, envelopes and modulation matrix replace this later; this filter is the stand-in.
 *
 * The saws and pulse come from dsp.c (osc_saw, a 32-bit phase, polyBLEP in Q15); the pulse is the difference of two saws, a
 * saw one width behind minus the saw: zero mean, up at phase 0 and down at the width, as the test bed's, at full level (not
 * the half level of osc_pulse(), whose edges are the other way round, which matters when it is crossfaded into the saw).
 * State: ph[0] master, ph[1] the synced slave, ph[2] the sub; s[0] s[1] the filter, s[2] the restart correction owed to the
 * next sample. 32-bit multiplies per sample; the 64-bit one is once a block (the slave's increment). */
static const char *const N_NANO_TYPE[] = {"BASIC", "TWO OP FM"};

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
static uint32_t nano_fm_inc(uint32_t inc, uint32_t r6)
{
    uint32_t oct = r6 / 768u, rem = r6 % 768u, k = rem / 32u, f = rem % 32u;
    uint32_t a = NANO_P2[k], b = NANO_P2[k + 1], fac = a + (((b - a) * f) >> 5);      /* Q16, 1..2 */
    uint64_t x = ((uint64_t)inc * fac) << oct;                                         /* x 2^oct, still Q16; the table starts at -1 octave */
    x >>= 17;
    return x > 0x50000000u ? 0x50000000u : (uint32_t)x;
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

static void nano_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[1] = v->ph[0];                              /* the slave starts with the master */
    v->ph[2] = v->ph[0] >> 1;                         /* the sub: an octave down, in step with it */
    v->s[0] = v->s[1] = 0;                            /* filter */
    v->s[2] = 0;                                      /* no restart pending */
    v->s[3] = 0;                                      /* FM: no DC yet */
}

static __attribute__((noinline)) void nano_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    nano_w w;
    if (p[P_E0] == 1) {
        nano_render_fm(t, v, out, n, m);
        return;
    }
    tsvf_t flt;
    uint32_t i, ph = v->ph[0], ps = v->ph[1], pb = v->ph[2], inc = m->inc, incs = inc, k = (uint32_t)p[P_E4];
    int32_t morph = clamp((p[P_E1] << 8) + m->shape - (64 << 8), 0, 127 << 8), mq, sym = p[P_E2] * 258;
    int32_t sub = p[P_E3] * 258, d, ic1 = v->s[0], ic2 = v->s[1], pend = v->s[2];
    int32_t cut = (p[P_E5] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    int32_t fk = ANALOG_K(p[P_E6]);
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
};

static const engine_t ENG_NANO = {
    .name = "NANO",
    .page_title = {"OSC", "FLT"},
    .edit = {
        {"TYPE", F_ENUM, 0, 1, 0, N_NANO_TYPE, 0},
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
    .keep = 0x03,                /* the filter */
};
