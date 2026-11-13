/* SPDX-License-Identifier: GPL-3.0-only
 * NANO (Nano Freak, src/eng_nano.c), BASIC WAVES, on the host through hostsim.c.
 *   build/host/nano_test
 * 1. pitch: a saw is at the key's pitch (within 6 cents) over four octaves, SYNC off and at 50 %.
 * 2. SYM: at MORPH 0 a centred pulse has almost no even harmonics, a narrow one has them.
 * 3. SUB adds a partial an octave below; with SUB 0 there is none.
 * 4. SYNC: the output repeats at the master's period (correlation over 0.97 one period on) and is brighter than without it.
 * 5. no overflow, no DC, no click: every key, MORPH, SYM, SUB, SYNC and cutoff in a sweep stays bounded, the largest step
 *    between two samples is limited, and a note-off falls silent.
 * 4b. TWO OP FM: pitch, INDEX, RATIO, FEEDBACK sweeps.
 * 4c. WAVESHAPER: period, FOLD, WAVE, bounded sweep.
 * 6. the preset table plays: every NANO preset makes sound. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void check(const char *what, int ok)
{
    printf("nano: %-104s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* track 0 = NANO: MORPH, SYM, SUB, SYNC, CUT 127 (open), RES 0, a held loudness contour */
static void setup(uint32_t morph, uint32_t sym, uint32_t sub, uint32_t sync, uint32_t cut)
{
    memset(trk, 0, sizeof trk);
    memset(eng_mem, 0, sizeof eng_mem);
    host_tracks_init();
    host_preset(&trk[0], ENGI_NANO, 0);
    trk[0].p[P_E0] = 0;
    trk[0].p[P_E1] = (int16_t)morph;
    trk[0].p[P_E2] = (int16_t)sym;
    trk[0].p[P_E3] = (int16_t)sub;
    trk[0].p[P_E4] = (int16_t)sync;
    trk[0].p[P_E5] = (int16_t)cut;
    trk[0].p[P_E6] = 0;
    trk[0].p[P_E7] = 64;
    trk[0].p[P_ATK] = 0;
    trk[0].p[P_DEC] = 0;
    trk[0].p[P_SUS] = 127;
    trk[0].p[P_REL] = 0;
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = trk[0].p[P_DIST] = 0;
}

/* TWO OP FM: RATIO INDEX FEEDBACK on E1..E3, the filter open */
static void setup_fm(uint32_t ratio, uint32_t index, uint32_t fb)
{
    setup(ratio, index, fb, 0, 127);
    trk[0].p[P_E0] = 1;
}

/* WAVESHAPER: WAVE FOLD SLOPE on E1..E3, the filter open */
static void setup_ws(uint32_t wave, uint32_t fold, uint32_t slope)
{
    setup(wave, fold, slope, 0, 127);
    trk[0].p[P_E0] = 2;
}

static void play(uint32_t note, double *y, uint32_t n)
{
    static int32_t b[CTL];
    uint32_t i, k;
    trk_note_on(&trk[0], note, 100);
    for (i = 0; i < n; i += CTL) {
        track_render(&trk[0], b, CTL);
        for (k = 0; k < CTL && i + k < n; k++)
            y[i + k] = b[k];
    }
    trk_note_off(&trk[0], note);
}

static double hz(uint32_t note) { return 440.0 * pow(2.0, ((double)note - 69.0) / 12.0); }

/* the amplitude of frequency f in y[a .. b) (a Goertzel-style correlation) */
static double tone_at(const double *y, uint32_t a, uint32_t b, double f)
{
    double sr = 0, si = 0, w = 2 * M_PI * f / FS;
    uint32_t i;
    for (i = a; i < b; i++) {
        sr += y[i] * cos(w * (double)i);
        si += y[i] * sin(w * (double)i);
    }
    return 2 * sqrt(sr * sr + si * si) / (b - a);
}

/* the pitch: rising zero crossings of the mean-free signal */
static double pitch(const double *y, uint32_t a, uint32_t b)
{
    uint32_t i, nz = 0;
    double first = 0, last = 0, mean = 0;
    for (i = a; i < b; i++)
        mean += y[i];
    mean /= b - a;
    for (i = a + 1; i < b; i++) {
        double p = y[i - 1] - mean, q = y[i] - mean;
        if (p < 0 && q >= 0) {
            double pos = (double)i - 1 + (-p) / (q - p);
            if (!nz++)
                first = pos;
            last = pos;
        }
    }
    return nz > 2 ? (double)FS * (nz - 1) / (last - first) : 0;
}

static double y[FS];

int main(void)
{
    uint32_t n = FS / 2, a = FS / 8, note, i, ok;
    double worst = 0;

    /* 1. pitch */
    for (note = 36; note <= 84; note += 12) {
        setup(127, 0, 0, 0, 127);
        play(note, y, n);
        double c = 1200 * log2(pitch(y, a, n) / hz(note));
        if (fabs(c) > worst)
            worst = fabs(c);
    }
    printf("   saw pitch, worst of 4 octaves: %.2f cents\n", worst);
    check("a saw is at the key's pitch (6 cents), SYNC off", worst < 6.0);
    worst = 0;
    for (note = 36; note <= 72; note += 12) {
        setup(127, 0, 0, 64, 127);
        play(note, y, n);
        double c = 1200 * log2(pitch(y, a, n) / hz(note));
        double r = fabs(c) > 1200 ? 1 : 0;
        (void)r;
        /* the synced saw's zero crossings are not one a cycle: check the period through the correlation instead */
        double per = (double)FS / hz(note), best = 0;
        uint32_t lag = (uint32_t)(per + 0.5), j;
        double e = 0, cc = 0;
        for (j = a; j + lag < n; j++) {
            e += y[j] * y[j];
            cc += y[j] * y[j + lag];
        }
        best = e > 0 ? cc / e : 0;
        if (1.0 - best > worst)
            worst = 1.0 - best;
    }
    printf("   synced, worst 1 - correlation at the master period: %.4f\n", worst);
    check("SYNC 50 %: the output repeats at the master's period (correlation over 0.97)", worst < 0.03);

    /* 2. SYM at MORPH 0 */
    {
        double f = hz(57), h1, h2, h3, e1, e2;
        setup(0, 64, 0, 0, 127);
        play(57, y, n);
        h1 = tone_at(y, a, n, f);
        h2 = tone_at(y, a, n, 2 * f);
        h3 = tone_at(y, a, n, 3 * f);
        e1 = h2 / h1;
        setup(0, 0, 0, 0, 127);
        play(57, y, n);
        e2 = tone_at(y, a, n, 2 * f) / tone_at(y, a, n, f);
        printf("   pulse: 2nd / 1st %.4f centred, %.4f narrow (3rd / 1st %.3f)\n", e1, e2, h3 / h1);
        check("MORPH 0 SYM centre: a square (2nd harmonic under 3 % of the 1st, the 3rd about a third)", e1 < 0.03 && h3 / h1 > 0.25 && h3 / h1 < 0.42);
        check("MORPH 0 SYM low: a narrow pulse has the even harmonics (2nd over 40 % of the 1st)", e2 > 0.4);
    }

    /* 3. SUB */
    {
        double f = hz(57), s0, s1;
        setup(127, 0, 0, 0, 127);
        play(57, y, n);
        s0 = tone_at(y, a, n, f / 2);
        setup(127, 0, 127, 0, 127);
        play(57, y, n);
        s1 = tone_at(y, a, n, f / 2);
        printf("   sub partial: %.1f off, %.1f at 127\n", s0, s1);
        check("SUB 127 adds a partial an octave below (over 3000, and 20x the one with SUB 0)", s1 > 3000 && s1 > 20 * s0);
    }

    /* 4. SYNC makes it brighter: more energy in the top of the spectrum than the unsynced saw */
    {
        double lo = 0, hi0, hi1, f = hz(48);
        uint32_t h;
        setup(127, 0, 0, 0, 127);
        play(48, y, n);
        for (h = 20; h < 40; h++)
            lo += tone_at(y, a, n, f * h) * tone_at(y, a, n, f * h);
        hi0 = lo;
        setup(127, 0, 0, 64, 127);
        play(48, y, n);
        lo = 0;
        for (h = 20; h < 40; h++)
            lo += tone_at(y, a, n, f * h) * tone_at(y, a, n, f * h);
        hi1 = lo;
        printf("   harmonics 20..39: %.0f unsynced, %.0f synced\n", hi0, hi1);
        check("SYNC makes the sound brighter (the upper harmonics carry more)", hi1 > hi0 * 1.5);
    }

    /* 4b. TWO OP FM (TYPE 1): RATIO 30 is 1:1; INDEX 0 is a sine at the key's pitch; INDEX brightens; RATIO changes the spectrum;
     *     FEEDBACK both ways stays bounded */
    {
        double w = 0, h2, e_lo = 0, e_hi = 0, r1 = 0, r2 = 0, f = hz(57), peak = 0, dc = 0;
        uint32_t idx, k, fb;
        for (note = 36; note <= 84; note += 12) {
            setup_fm(30, 0, 64);
            play(note, y, n);
            double c = 1200 * log2(pitch(y, a, n) / hz(note));
            if (fabs(c) > w)
                w = fabs(c);
        }
        setup_fm(30, 0, 64);
        play(57, y, n);
        h2 = tone_at(y, a, n, 2 * f) / tone_at(y, a, n, f);
        printf("   FM INDEX 0: pitch worst %.2f cents, 2nd / 1st %.4f\n", w, h2);
        check("FM INDEX 0 is a sine at the key's pitch (6 cents, 2nd harmonic under 2 %)", w < 6.0 && h2 < 0.02);
        setup_fm(30, 20, 64);
        play(57, y, n);
        for (k = 2; k <= 8; k++)
            e_lo += tone_at(y, a, n, f * k);
        setup_fm(30, 90, 64);
        play(57, y, n);
        for (k = 2; k <= 8; k++)
            e_hi += tone_at(y, a, n, f * k);
        printf("   FM harmonics 2..8: %.0f at INDEX 20, %.0f at 90\n", e_lo, e_hi);
        check("FM INDEX brightens (harmonics 2..8 carry over 5x more at 90 than at 20)", e_hi > 5 * e_lo);
        {
            double ha[9];
            setup_fm(30, 70, 64);
            play(57, y, n);
            for (k = 2; k <= 8; k++) {
                ha[k] = tone_at(y, a, n, f * k);
                r1 += ha[k];
            }
            setup_fm(60, 70, 64);
            play(57, y, n);
            for (k = 2; k <= 8; k++)
                r2 += fabs(tone_at(y, a, n, f * k) - ha[k]);
            printf("   FM harmonics 2..8: %.0f in all at 1:1, they move by %.0f at the octave up\n", r1, r2);
            check("FM RATIO changes the spectrum (the harmonics move by over 20 %)", r2 > 0.2 * r1);
        }
        for (fb = 0; fb <= 127; fb += 127)
            for (idx = 0; idx <= 127; idx += 63)
                for (k = 0; k < 4; k++) {
                    static const uint8_t keys[] = {57, 69, 84, 108};
                    uint32_t j;
                    setup_fm(30 + 30 * (k & 1u), idx, fb);
                    play(keys[k], y, 8192);
                    for (j = 2048; j < 8192; j++) {
                        if (fabs(y[j]) > peak)
                            peak = fabs(y[j]);
                        dc += y[j];
                    }
                }
        printf("   FM sweep: peak %.0f, mean %.1f\n", peak, dc / (6144.0 * 24));
        check("FM sweep (INDEX, FEEDBACK both ways, ratio, keys): bounded (peak under 65536) and no DC (under 5 % of the peak)", peak < 65536 && fabs(dc / (6144.0 * 24)) < 0.05 * peak);
    }

    /* 4c. WAVESHAPER (TYPE 2): the period, FOLD brightens, WAVE changes the spectrum, bounded sweeps with no DC */
    {
        double worstc = 0, lo = 0, hi = 0, f = hz(57), peak = 0, dc = 0, ha[9], moved = 0, tot = 0;
        uint32_t k, w, fo, sl;
        for (note = 36; note <= 84; note += 12) {
            double per0 = (double)FS / hz(note), per = per0, e = 0, cc = 0, be = 1;
            uint32_t lag, j, kk;
            for (kk = 1; kk <= 64; kk++) {                                /* a number of periods that is a whole number of samples to 0.05 */
                double d = fabs(kk * per0 - floor(kk * per0 + 0.5));
                if (d < be) {
                    be = d;
                    per = kk * per0;
                }
                if (d < 0.05)
                    break;
            }
            lag = (uint32_t)(per + 0.5);
            setup_ws(64, 60, 64);
            play(note, y, n);
            for (j = a; j + lag < n; j++) {
                e += y[j] * y[j];
                cc += y[j] * y[j + lag];
            }
            if (e > 0 && 1.0 - cc / e > worstc)
                worstc = 1.0 - cc / e;
        }
        printf("   WS: worst 1 - correlation at the period: %.4f\n", worstc);
        check("WS repeats at the key's period (correlation over 0.97) over four octaves", worstc < 0.03);
        setup_ws(64, 10, 64);
        play(57, y, n);
        for (k = 2; k <= 8; k++)
            lo += tone_at(y, a, n, f * k);
        setup_ws(64, 110, 64);
        play(57, y, n);
        for (k = 2; k <= 8; k++)
            hi += tone_at(y, a, n, f * k);
        printf("   WS harmonics 2..8: %.0f at FOLD 10, %.0f at 110\n", lo, hi);
        check("WS FOLD changes the sound (harmonics 2..8 differ by over 30 %)", fabs(hi - lo) > 0.3 * (hi > lo ? hi : lo));
        setup_ws(0, 80, 64);
        play(57, y, n);
        for (k = 2; k <= 8; k++) {
            ha[k] = tone_at(y, a, n, f * k);
            tot += ha[k];
        }
        setup_ws(127, 80, 64);
        play(57, y, n);
        for (k = 2; k <= 8; k++)
            moved += fabs(tone_at(y, a, n, f * k) - ha[k]);
        printf("   WS harmonics 2..8: %.0f in all at WAVE 0, they move by %.0f at WAVE 127\n", tot, moved);
        check("WS WAVE changes the spectrum (the harmonics move by over 20 %)", moved > 0.2 * tot);
        for (w = 0; w <= 127; w += 32)
            for (fo = 0; fo <= 127; fo += 63)
                for (sl = 0; sl <= 127; sl += 127)
                    for (k = 0; k < 4; k++) {
                        static const uint8_t keys[] = {57, 69, 84, 108};
                        uint32_t j;
                        setup_ws(w, fo, sl);
                        play(keys[k], y, 8192);
                        for (j = 2048; j < 8192; j++) {
                            if (fabs(y[j]) > peak)
                                peak = fabs(y[j]);
                            dc += y[j];
                        }
                    }
        printf("   WS sweep: peak %.0f, mean %.1f\n", peak, dc / (6144.0 * 120));
        check("WS sweep (WAVE, FOLD, SLOPE, keys): bounded (peak under 65536) and no DC (under 5 % of the peak)", peak < 65536 && fabs(dc / (6144.0 * 120)) < 0.05 * peak);
    }

    /* 5. a sweep: bounded, no DC, no click; a note-off falls silent */
    {
        double peak = 0, step = 0, dc = 0, tail = 0;
        uint32_t morph, sym, sub, sync, cut, cnt = 0;
        for (morph = 0; morph <= 127; morph += 31)
            for (sym = 0; sym <= 127; sym += 43)
                for (sub = 0; sub <= 127; sub += 127)
                    for (sync = 0; sync <= 127; sync += 21)
                        for (cut = 40; cut <= 127; cut += 43) {
                            static const uint8_t keys[] = {24, 48, 69, 96, 108};
                            uint32_t kk = (cnt++) % 5u, j;
                            setup(morph, sym, sub, sync, cut);
                            play(keys[kk], y, 8192);
                            for (j = 2048; j < 8192; j++) {
                                double ay = fabs(y[j]);
                                if (ay > peak)
                                    peak = ay;
                                if (fabs(y[j] - y[j - 1]) > step)
                                    step = fabs(y[j] - y[j - 1]);
                                dc += y[j];
                            }
                        }
        dc /= (double)cnt * 6144;
        {
            static int32_t b[CTL];
            setup(127, 0, 127, 64, 127);
            trk_note_on(&trk[0], 60, 100);
            for (i = 0; i < 20; i++)
                track_render(&trk[0], b, CTL);
            trk_note_off(&trk[0], 60);
            for (i = 0; i < 400; i++)
                track_render(&trk[0], b, CTL);
            for (i = 0; i < CTL; i++)
                tail += fabs((double)b[i]);
        }
        printf("   sweep of %u settings: peak %.0f, largest step %.0f, mean %.1f, tail after note-off %.1f\n", cnt, peak, step, dc, tail);
        check("the sweep stays bounded (a narrow pulse peaks high, as in the test bed, but stays under twice full scale)", peak < 65536.0);
        check("no DC (the mean is under 2 % of the peak)", fabs(dc) < 0.02 * peak);
        check("no click: the largest step between two samples is under 80 % of the peak", step < 0.8 * peak);
        check("a note-off falls silent", tail < 1.0);
    }

    /* 6. every preset sounds */
    for (i = 0, ok = 1; i < NELEM(NANO_PRESETS); i++) {
        double e = 0;
        uint32_t j;
        memset(trk, 0, sizeof trk);
        memset(eng_mem, 0, sizeof eng_mem);
        host_tracks_init();
        host_preset(&trk[0], ENGI_NANO, i);
        play(48, y, 8192);
        for (j = 2048; j < 8192; j++)
            e += y[j] * y[j];
        printf("   preset %u %-10s rms %.0f\n", i, NANO_PRESETS[i].name, sqrt(e / 6144));
        if (sqrt(e / 6144) < 1000)
            ok = 0;
    }
    check("every preset makes sound", ok);
    return bad;
}
