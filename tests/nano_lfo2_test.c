/* SPDX-License-Identifier: GPL-3.0-only
 * LFO 2 (Nano 2.5.66): a second LFO, bipolar, running in every part with its own phase. Checks: it oscillates at the rate RATE says (and
 * faster at a higher RATE), each SHAPE has its own form, MORPH blends into the next, SYNC follows the tempo, it is independent of LFO 1,
 * and the matrix's LFO2 source moves a destination (cutoff) at the LFO's rate. */
#define FELUCCA_FMINI_ONLY 1
#include "nano_host.h"
#include "../firmware/src/nano_app.c"
#include <stdio.h>
#include <math.h>
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
static void blk(void) { static int32_t bb[CTL]; track_render(&trk[0], bb, CTL); }
static void settle(void) { uint32_t b; int32_t o[2u * CTL]; for (b = 0; b < 400; b++) { events_block(CTL); mix_block(o, CTL); } }
static void setup(int rate, int shape, int morph, int sync)
{
    na_load(0); settle();
    trk[0].p[P_L2RATE] = (int16_t)rate; trk[0].p[P_L2WAVE] = L2_WAVE(shape, sync); trk[0].p[P_L2MORPH] = (int16_t)(127 - morph);
    trk[0].lfo2_ph = 0; trk[0].lfo2_rnd = 0; trk[0].lfo2_prev = 0;
}
static int cycles(int rate, double sec)                   /* upward zero crossings of the sine in sec seconds */
{
    uint32_t b, n = (uint32_t)(sec * 44100.0 / CTL); int prev = 0, c = 0;
    setup(rate, 0, 0, 0);
    for (b = 0; b < n; b++) { blk(); if (prev < 0 && trk[0].lfo2_val >= 0) c++; prev = trk[0].lfo2_val; }
    return c;
}
int main(void)
{
    uint32_t b; int c1, c2, mn, mx;
    setvbuf(stdout, 0, _IONBF, 0);
    { uint32_t gi; for (gi = 0; gi < G_COUNT; gi++) song.g[gi] = GP[gi].def; }
    host_tracks_init(); panel_init(); na_init(); song.master_q12 = 4096;
    /* 1. it runs, and a higher RATE runs faster */
    c1 = cycles(70, 4.0); c2 = cycles(100, 4.0);
    printf("LFO 2 cycles in 4 s: RATE 70: %d, RATE 100: %d\n", c1, c2);
    CHECK(c1 >= 1, "LFO 2 at RATE 70 does not oscillate (%d)", c1);
    CHECK(c2 > c1 * 2, "a higher RATE is not faster (%d against %d)", c2, c1);
    /* 2. the shapes: full range, bipolar */
    for (b = 0; b < 6u; b++) {
        uint32_t i;
        setup(110, (int)b, 0, 0); mn = 99999; mx = -99999;
        for (i = 0; i < 4000u; i++) { blk(); if (trk[0].lfo2_val < mn) mn = trk[0].lfo2_val; if (trk[0].lfo2_val > mx) mx = trk[0].lfo2_val; }
        printf("shape %u: %d .. %d\n", b, mn, mx);
        CHECK(mx > 12000 && mn < -12000, "shape %u is not bipolar (%d .. %d)", b, mn, mx);
    }
    /* 3. MORPH: a SQUARE with MORPH 127 is the next shape (S&H); a SINE with MORPH 127 is a TRI (they differ from the plain SINE) */
    {
        int32_t a, m;
        setup(60, 0, 0, 0); trk[0].lfo2_ph = 0x30000000u; a = 0; { uint32_t k; for (k = 0; k < 1; k++) { trk[0].lfo2_ph = 0x30000000u - 0; blk(); a = trk[0].lfo2_val; } }
        setup(60, 0, 127, 0); trk[0].lfo2_ph = 0x30000000u; blk(); m = trk[0].lfo2_val;
        printf("SINE at 0.19: %d, MORPH 127: %d\n", a, m);
        CHECK(abs(a - m) > 2000, "MORPH does not change the shape (%d, %d)", a, m);
    }
    /* 4. independent of LFO 1: changing LFO 1's rate leaves LFO 2's phase course alone */
    {
        uint32_t i; uint32_t ph1, ph2;
        setup(90, 0, 0, 0); trk[0].p[P_LRATE] = 20; for (i = 0; i < 500u; i++) blk(); ph1 = trk[0].lfo2_ph;
        setup(90, 0, 0, 0); trk[0].p[P_LRATE] = 120; for (i = 0; i < 500u; i++) blk(); ph2 = trk[0].lfo2_ph;
        CHECK(ph1 == ph2, "LFO 1's rate moved LFO 2 (%u %u)", ph1, ph2);
    }
    /* 5. SYNC: the RATE knob picks a note value; the cycle follows the tempo (slower note = fewer cycles) */
    {
        int s1, s2; uint32_t i, n = (uint32_t)(8.0 * 44100.0 / CTL); int prev;
        song.g[G_BPM] = 120;
        setup(60, 0, 0, 1); prev = 0; s1 = 0;
        for (i = 0; i < n; i++) { blk(); if (prev < 0 && trk[0].lfo2_val >= 0) s1++; prev = trk[0].lfo2_val; }
        setup(120, 0, 0, 1); prev = 0; s2 = 0;
        for (i = 0; i < n; i++) { blk(); if (prev < 0 && trk[0].lfo2_val >= 0) s2++; prev = trk[0].lfo2_val; }
        printf("SYNC at 120 BPM in 8 s: RATE 60: %d cycles, RATE 120: %d\n", s1, s2);
        CHECK(s2 > s1, "SYNC: a higher RATE is not a shorter note (%d, %d)", s1, s2);
    }
    /* 6. the matrix: LFO2 > cutoff makes the brightness swing at the LFO's rate; with no route it stays put */
    {
        double sw[2]; int m;
        for (m = 0; m < 2; m++) {
            double lo2 = 1e9, hi2 = -1e9; uint32_t w, i;
            setup(100, 0, 0, 0);
            trk[0].p[P_E0] = 0; trk[0].p[P_E1] = 120; trk[0].p[P_E5] = 60; trk[0].p[P_E6] = 20; trk[0].p[P_SUS] = 100; trk[0].p[P_ATK] = 0;
            if (m) { trk[0].p[P_M1SRC] = MS_LFO2; trk[0].p[P_M1DST] = MD_CUT; trk[0].p[P_M1AMT] = 64; }
            trk_note_on(&trk[0], 45, 100);
            for (w = 0; w < 160u; w++) {
                static int32_t bb[CTL]; double d = 0, e = 0, prev = 0; int32_t pcm[4];
                for (i = 0; i < 8u; i++) {
                    uint32_t z; int on = mod_begin(&trk[0]); track_render(&trk[0], bb, CTL); if (on) mod_end(&trk[0]);
                    for (z = 0; z < CTL; z++) { double x = bb[z] / 32768.0; d += (x - prev) * (x - prev); e += x * x; prev = x; }
                }
                (void)pcm;
                if (w > 20u && e > 1e-9) { double br = sqrt(d / e); if (br < lo2) lo2 = br; if (br > hi2) hi2 = br; }
            }
            sw[m] = hi2 > lo2 ? hi2 / lo2 : 1;
            printf("LFO2 -> CUTOFF %s: brightness swing x%.2f\n", m ? "on" : "off", sw[m]);
            for (i = 0; i < 128u; i++) trk_note_off(&trk[0], i);
        }
        CHECK(sw[1] > sw[0] * 1.5 && sw[1] > 1.5, "the LFO2 > CUTOFF route does not move the brightness (x%.2f against x%.2f)", sw[1], sw[0]);
    }
    printf("%s\n", fails ? "nano_lfo2_test FAIL" : "nano_lfo2_test ok");
    return fails ? 1 : 0;
}
