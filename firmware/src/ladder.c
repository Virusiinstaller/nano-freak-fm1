/* SPDX-License-Identifier: GPL-3.0-only
 * The Minimoog-style ladder filter, kept from FMini-1 (where it was fmini_render's filter) as a stand-alone block for the Nano Freak.
 *
 * After the Minimoog's (tools/ladder_filter.py has the laws and the reasoning, the CA-72 project's calibration the
 * measurements): four equal one-pole stages, each a differential pair (tanh in front), the last stage's output fed back to
 * the first through a lag pole (the emphasis network), solved without a delay in the loop (zero-delay feedback: each stage
 * is a trapezoidal integrator, G = g / (1 + g), and the feedback is found from the stages' states before the sample is
 * processed). The lag pole puts the loop's 180-degree point at 0.686 of the stage pole: the filter sings at 1.578 times
 * its -3 dB corner, and it sings when the feedback k reaches 2.92. The pitch works in 1/16 semitones of the singing
 * pitch (MIDI x 16): CUTOFF is the dial -5 .. +5 (LADDER_CUT), EMPHASIS the dial 0 .. 10 (LADDER_K) with the hardware's passband
 * loss (LADDER_TRIM). With the input silent and EMPHASIS past 8 the ladder sings a sine at the singing pitch.
 *
 * Use, once a block:  ladder_coef(&k, pitch16, emph_q16);  ladder_run(&state, &k, in, out, n, hp);
 *   pitch16: the singing pitch, MIDI x 16: ladder_cut(cutoff_q16) for the dial, plus whatever tracks the key, the envelope, the LFO
 *            (16 per semitone; keep it inside LADDER_GM0 * 16 .. LADDER_FOSC_MAX16)
 *   emph_q16: EMPHASIS 0..127 with 16 fractional bits (a smoothed knob: the steps become ramps)
 *   in, out: signals of +-32768 (tanh(1)); out may be in; the output is the filtered signal with the passband's loss applied (Q15 scale)
 *   hp: 0 the low-pass (the last stage), 1 the high-pass ((1 - LP)^4 of the same stages)
 * Integer only; 64 bit nowhere. Needs the engine interface's dsp.c (softclip, clamp) and ladder_tables.h before it. */

typedef struct {
    int32_t s[5];                            /* the four stages' states and the lag pole's, Q8 (a quiet ladder at a low cutoff moves less than a LSB a sample) */
} ladder_t;

typedef struct {
    int32_t G16, gx16, c0, c1, c2, c3, inv14, kinv12, trim12;   /* G Q16; the prediction's coefficients c Q14; 1/(1 + k Gx G^4) Q14; k/(...) Q12; the loss Q12 */
} ladder_k_t;

/* a * k / 65536 rounded to the nearest (mulq16 floors: four stages of it leave a bias that a quiet, low ladder cannot sing against) */
static inline int32_t ladder_mulr(int32_t a, uint32_t k)
{
    return (int32_t)(((a >> 16) * (int32_t)k) + (int32_t)(((uint32_t)(a & 0xFFFF) * k + 32768u) >> 16));
}

static void ladder_reset(ladder_t *f)
{
    f->s[0] = f->s[1] = f->s[2] = f->s[3] = f->s[4] = 0;
}

/* CUTOFF 0..127 (Q16, 16 fractional bits) -> the singing pitch, MIDI x 16 (the dial's -5 .. +5) */
static int32_t ladder_cut(int32_t knob_q16)
{
    int32_t kn = clamp(knob_q16, 0, 127 << 16), i0 = kn >> 16, fr = (kn >> 8) & 255, lc = LADDER_CUT[i0];
    if (i0 < 127)
        lc += ((LADDER_CUT[i0 + 1] - lc) * fr) >> 8;
    return lc;
}

/* the coefficients for one block: the stage pole G and the lag pole's from the tables (one entry a semitone, linear between), the
 * feedback solved ahead. pitch16 is clamped into the tables. */
static void ladder_coef(ladder_k_t *k, int32_t pitch16, int32_t emph_q16)
{
    int32_t lc = clamp(pitch16, LADDER_GM0 * 16, LADDER_FOSC_MAX16);
    uint32_t ix = (uint32_t)((lc >> 4) - LADDER_GM0), fr = (uint32_t)lc & 15u, gq, gxq, g2, g3, g4, om;
    int32_t kn = clamp(emph_q16, 0, 127 << 16), i0 = kn >> 16, fk = (kn >> 8) & 255, k12, trim12, den;
    gq = LADDER_G[ix] + (uint32_t)(((int32_t)(LADDER_G[ix + 1] - LADDER_G[ix]) * (int32_t)fr) >> 4);
    gxq = LADDER_GX[ix] + (uint32_t)(((int32_t)(LADDER_GX[ix + 1] - LADDER_GX[ix]) * (int32_t)fr) >> 4);
    k->G16 = (int32_t)((gq + 128u) >> 8);
    k->gx16 = (int32_t)((gxq + 128u) >> 8);
    om = 65536u - (uint32_t)k->G16;
    g2 = ((uint32_t)k->G16 * (uint32_t)k->G16) >> 16;
    g3 = (g2 * (uint32_t)k->G16) >> 16;
    g4 = (g3 * (uint32_t)k->G16) >> 16;
    k->c3 = (int32_t)(om >> 2);                                   /* y4 = G^4 u + c0 s0 + c1 s1 + c2 s2 + c3 s3 */
    k->c2 = (int32_t)(((uint32_t)k->G16 * om) >> 18);
    k->c1 = (int32_t)((g2 * om) >> 18);
    k->c0 = (int32_t)((g3 * om) >> 18);
    k12 = LADDER_K[i0];                                           /* EMPHASIS: the feedback gain k and the passband's loss */
    trim12 = LADDER_TRIM[i0];
    if (i0 < 127) {
        k12 += ((LADDER_K[i0 + 1] - k12) * fk) >> 8;
        trim12 += ((LADDER_TRIM[i0 + 1] - trim12) * fk) >> 8;
    }
    den = 65536 + ((k12 * (int32_t)((((uint32_t)gxq >> 8) * g4) >> 16)) >> 12);        /* 1 + k Gx G^4: the loop around the whole sample */
    k->inv14 = (int32_t)((1u << 30) / (uint32_t)den);
    k->kinv12 = (k12 * k->inv14) >> 14;
    k->trim12 = trim12;
}

static void ladder_run(ladder_t *f, const ladder_k_t *k, const int32_t *in, int32_t *out, uint32_t n, uint32_t hp)
{
    int32_t s0 = f->s[0], s1 = f->s[1], s2 = f->s[2], s3 = f->s[3], sx = f->s[4];
    const int32_t c0 = k->c0, c1 = k->c1, c2 = k->c2, c3 = k->c3, inv14 = k->inv14, kinv12 = k->kinv12, trim12 = k->trim12;
    const uint32_t G16 = (uint32_t)k->G16, gx16 = (uint32_t)k->gx16;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x = softclip(in[i]), S, fbc, u, xin, v1, v2, v3, v4, y1, y2, y3, y4, w, o;   /* the input overloads softly */
        S = (c0 * ((s0 + 128) >> 8) + c1 * ((s1 + 128) >> 8) + c2 * ((s2 + 128) >> 8) + c3 * ((s3 + 128) >> 8)) >> 14;   /* what the stages already hold */
        fbc = sx + ladder_mulr((S << 8) - sx, gx16);                   /* through the lag pole (Q8) */
        u = ((x * inv14) >> 14) - ((kinv12 * ((fbc + 128) >> 8)) >> 12);  /* the input less the feedback, the loop solved */
        xin = softclip(u) << 8;                                        /* the first stage's input (Q8) */
        v1 = ladder_mulr(xin - s0, G16);                               /* each stage is a differential pair: tanh in front of it */
        y1 = v1 + s0;
        s0 = y1 + v1;
        v2 = ladder_mulr((softclip((y1 + 128) >> 8) << 8) - s1, G16);
        y2 = v2 + s1;
        s1 = y2 + v2;
        v3 = ladder_mulr((softclip((y2 + 128) >> 8) << 8) - s2, G16);
        y3 = v3 + s2;
        s2 = y3 + v3;
        v4 = ladder_mulr((softclip((y3 + 128) >> 8) << 8) - s3, G16);
        y4 = v4 + s3;
        s3 = y4 + v4;
        w = ladder_mulr(y4 - sx, gx16);                                /* the lag pole follows the output */
        sx += w + w;
        /* HP: the stages are one-pole lows H; (1 - H)^4 = 1 - 4H + 6H^2 - 4H^3 + H^4 on the stages' outputs (linear, the states as they are) */
        o = hp ? xin - 4 * y1 + 6 * y2 - 4 * y3 + y4 : y4;
        out[i] = (((o + 128) >> 8) * trim12) >> 12;
    }
    f->s[0] = s0;
    f->s[1] = s1;
    f->s[2] = s2;
    f->s[3] = s3;
    f->s[4] = sx;
}
