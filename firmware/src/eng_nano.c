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
 * then a state variable low-pass (CUT RES KTR, ANALOG's own filter code) so the first builds are playable. The Nano Freak
 * filter, envelopes and modulation matrix replace this later; this filter is the stand-in.
 *
 * The saws and pulse come from dsp.c (osc_saw, a 32-bit phase, polyBLEP in Q15); the pulse is the difference of two saws, a
 * saw one width behind minus the saw: zero mean, up at phase 0 and down at the width, as the test bed's, at full level (not
 * the half level of osc_pulse(), whose edges are the other way round, which matters when it is crossfaded into the saw).
 * State: ph[0] master, ph[1] the synced slave, ph[2] the sub; s[0] s[1] the filter, s[2] the restart correction owed to the
 * next sample. 32-bit multiplies per sample; the 64-bit one is once a block (the slave's increment). */
static const char *const N_NANO_TYPE[] = {"BASIC"};

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

static void nano_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[1] = v->ph[0];                              /* the slave starts with the master */
    v->ph[2] = v->ph[0] >> 1;                         /* the sub: an octave down, in step with it */
    v->s[0] = v->s[1] = 0;                            /* filter */
    v->s[2] = 0;                                      /* no restart pending */
}

static __attribute__((noinline)) void nano_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    nano_w w;
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
};

static const engine_t ENG_NANO = {
    .name = "NANO",
    .page_title = {"OSC", "FLT"},
    .edit = {
        {"TYPE", F_ENUM, 0, 0, 0, N_NANO_TYPE, 0},
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
