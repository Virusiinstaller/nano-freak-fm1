/* SPDX-License-Identifier: GPL-3.0-only
 * The ladder filter (firmware/src/ladder.c, tables: tools/ladder_filter.py), on its own: no engine, no host, only the block.
 *   1. it sings: silent input after a kick, CUTOFF at the middle, EMPHASIS at the top: a sine (THD under 2 %) at the pitch the law gives
 *      (1073 Hz at the dial's 0), and the pitch follows the pitch16 input at one volt an octave (12 cents over four octaves).
 *   2. the CUTOFF dial: the singing pitch at -3 .. +2 is the hardware's (110.8 206.6 472.2 1073.8 2344 5188 Hz) within a quarter tone.
 *   3. EMPHASIS: no singing below the dial's 7.6, singing from 8.5; on noise the passband falls and the peak rises as the hardware's.
 *   4. no runaway, no DC: a loud saw through every cutoff, emphasis and pitch stays bounded.
 *   5. the high-pass output cuts a low note and passes a high one. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#define NVOICE 12
#define NPART 4
#define CTL 32
#include "felucca_tables.h"
#include "../firmware/engines/src/felucca_engine.h"
#include "../firmware/engines/src/dsp.c"
#include "../firmware/src/ladder_tables.h"
#include "../firmware/src/ladder.c"
#define FS 44100u
#define BLK 64u

static int bad;
static void check(const char *what, int ok)
{
    printf("ladder: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static double midi_hz(double n) { return 440.0 * pow(2.0, (n - 69.0) / 12.0); }
static double cents(double f, double ref) { return 1200.0 * log2(f / ref); }

/* run the filter over in[0..n) (NULL: silence after a kick of 8000 in the first sample) with the knobs held; out as doubles */
static void run(double *y, uint32_t n, int32_t pitch16, int32_t emph_q16, uint32_t hp, const int32_t *in)
{
    static int32_t a[BLK], b[BLK];
    ladder_t f;
    ladder_k_t k;
    uint32_t i, j;
    ladder_reset(&f);
    ladder_coef(&k, pitch16, emph_q16);
    for (i = 0; i < n; i += BLK) {
        for (j = 0; j < BLK; j++)
            a[j] = in ? in[i + j] : (i + j == 0 ? 8000 : 0);
        ladder_run(&f, &k, a, b, BLK, hp);
        for (j = 0; j < BLK; j++)
            y[i + j] = b[j];
    }
}
static void analyse(const double *y, uint32_t a, uint32_t b, double *f, double *rms, double *thd)
{
    uint32_t i, nz = 0;
    double first = 0, last = 0, mean = 0, e = 0, sr = 0, si = 0, w;
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
    *f = nz > 2 ? (double)FS * (nz - 1) / (last - first) : 0;
    for (i = a; i < b; i++)
        e += (y[i] - mean) * (y[i] - mean);
    *rms = sqrt(e / (b - a));
    w = 2 * M_PI * *f / FS;
    for (i = a; i < b; i++) {
        sr += (y[i] - mean) * cos(w * (double)i);
        si += (y[i] - mean) * sin(w * (double)i);
    }
    sr = 2 * sr / (b - a);
    si = 2 * si / (b - a);
    {
        double fund = (sr * sr + si * si) / 2;
        *thd = *rms > 0 ? sqrt(fabs(e / (b - a) - fund) / (e / (b - a))) : 0;
    }
}
#define NF 4096
static void welch(const double *y, uint32_t n, double *ps)
{
    static double re[NF], im[NF];
    uint32_t i, j, len, seg = 0, a, k;
    for (k = 0; k < NF / 2; k++)
        ps[k] = 0;
    for (a = 0; a + NF <= n; a += NF / 2, seg++) {
        for (i = 0; i < NF; i++) {
            re[i] = y[a + i] * (0.5 - 0.5 * cos(2 * M_PI * i / NF));
            im[i] = 0;
        }
        for (i = 1, j = 0; i < NF; i++) {
            uint32_t bit = NF >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j) {
                double t = re[i]; re[i] = re[j]; re[j] = t;
                t = im[i]; im[i] = im[j]; im[j] = t;
            }
        }
        for (len = 2; len <= NF; len <<= 1) {
            double ang = -2 * M_PI / len;
            for (i = 0; i < NF; i += len)
                for (j = 0; j < len / 2; j++) {
                    double wr = cos(ang * j), wi = sin(ang * j);
                    double xr = re[i + j + len / 2] * wr - im[i + j + len / 2] * wi;
                    double xi = re[i + j + len / 2] * wi + im[i + j + len / 2] * wr;
                    re[i + j + len / 2] = re[i + j] - xr, im[i + j + len / 2] = im[i + j] - xi;
                    re[i + j] += xr, im[i + j] += xi;
                }
        }
        for (k = 0; k < NF / 2; k++)
            ps[k] += re[k] * re[k] + im[k] * im[k];
    }
    for (k = 0; k < NF / 2; k++)
        ps[k] /= seg ? seg : 1;
}

int main(void)
{
    static double y[FS * 9];
    static int32_t xin[FS * 9];
    uint32_t i, N = FS * 2;
    double f, r, t, f0;

    /* 1. sings, and follows the pitch input at 1 V/oct */
    run(y, N, ladder_cut(64 << 16), 127 << 16, 0, 0);
    analyse(y, FS, N, &f, &r, &t);
    f0 = f;
    printf("   dial 0, EMPHASIS 10: %.1f Hz, rms %.0f, THD %.2f %%\n", f, r, t * 100);
    check("the input silent, CUTOFF 64, EMPHASIS 127: a sine at about 1.07 kHz", r > 800 && t < 0.02 && fabs(cents(f, 1073.8)) < 60);
    {
        uint32_t st[5] = {0, 7, 12, 24, 36};
        int ok = 1;
        for (i = 0; i < 5u; i++) {
            double want = f0 * pow(2.0, st[i] / 12.0);
            run(y, N, ladder_cut(64 << 16) + (int32_t)st[i] * 16, 127 << 16, 0, 0);
            analyse(y, FS, N, &f, &r, &t);
            printf("   +%u semitones: %.1f Hz (want %.1f, %+.1f cents), rms %.0f, THD %.2f %%\n", st[i], f, want, cents(f, want), r, t * 100);
            ok &= fabs(cents(f, want)) < 12 && t < 0.03 && r > 500;
        }
        check("the pitch follows pitch16 at 1 V/oct (four octaves, within 12 cents)", ok);
    }

    /* 2. the dial */
    {
        static const struct { double dial, hz; } M[6] = {{-3, 110.8}, {-2, 206.6}, {-1, 472.2}, {0, 1073.8}, {1, 2344}, {2, 5188}};
        int ok = 1;
        for (i = 0; i < 6u; i++) {
            uint32_t knob = (uint32_t)((M[i].dial + 5.0) * 12.7 + 0.5);
            run(y, N, ladder_cut((int32_t)knob << 16), 127 << 16, 0, 0);
            analyse(y, FS, N, &f, &r, &t);
            printf("   dial %+.0f (knob %u): %.1f Hz (hardware %.1f, %+.0f cents)\n", M[i].dial, knob, f, M[i].hz, cents(f, M[i].hz));
            ok &= fabs(cents(f, M[i].hz)) < 70 && r > 200;
        }
        check("the singing pitch at the dial's -3 .. +2 is the hardware's within a quarter tone", ok);
    }

    /* 3. EMPHASIS: singing or not */
    {
        uint32_t em[6] = {96, 100, 104, 108, 112, 127};
        int ok_off = 1, ok_on = 1;
        for (i = 0; i < 6u; i++) {
            run(y, N, ladder_cut(64 << 16), (int32_t)em[i] << 16, 0, 0);
            analyse(y, FS, N, &f, &r, &t);
            printf("   EMPHASIS %u (dial %.2f): rms %.0f, %.1f Hz\n", em[i], em[i] * 10.0 / 127, r, f);
            if (em[i] <= 96)
                ok_off &= r < 30;
            if (em[i] >= 108)
                ok_on &= r > 500;
        }
        check("EMPHASIS below 7.6 on the dial does not sing, from 8.5 it does", ok_off && ok_on);
    }

    /* 3b. EMPHASIS on noise: the passband's loss and the peak over it (CA-72 sessions E, J, N) */
    {
        static double ps0[NF / 2], ps[NF / 2];
        static const struct { uint32_t dial10; double loss, peak; } R[6] = {{30, -4.0, 2.6}, {40, -8.0, 9.3}, {50, -9.5, 11.3}, {60, -10.4, 14.0}, {70, -11.9, 20.4}, {75, -12.5, 25.0}};
        uint32_t nn = FS * 8, j, ok = 1, kb = (uint32_t)(70.0 / ((double)FS / NF)), s = 0x2F6E2B1u;
        double ref = 0;
        for (i = 0; i < nn; i++) {                       /* white noise, +-3000 */
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            xin[i] = (int32_t)(s >> 20) - 2048 + ((int32_t)(s & 0x3FF) - 512);
            xin[i] = xin[i] * 3000 / 2560;
        }
        run(y, nn, ladder_cut(64 << 16), 0, 0, xin);
        welch(y + FS, nn - FS, ps0);
        {
            uint32_t q;
            for (q = kb - 2; q <= kb + 2; q++)
                ref += ps0[q] / 5;
        }
        for (j = 0; j < 6u; j++) {
            uint32_t emph = (uint32_t)(R[j].dial10 * 12.7 / 10 + 0.5), k, kp = 0;
            double pass = 0, peak = 0, loss, over;
            run(y, nn, ladder_cut(64 << 16), (int32_t)emph << 16, 0, xin);
            welch(y + FS, nn - FS, ps);
            for (k = kb - 2; k <= kb + 2; k++)
                pass += ps[k] / ref / 5;
            for (k = 4; k < NF / 2; k++) {
                double rr = 0;
                uint32_t q;
                for (q = k - 2 < 1 ? 1 : k - 2; q <= k + 2 && q < NF / 2; q++)
                    rr += ps[q] / ref / 5;
                if (rr > peak) {
                    peak = rr;
                    kp = k;
                }
            }
            loss = 10 * log10(pass);
            over = 10 * log10(peak / pass);
            printf("   EMPHASIS %.1f: passband %+.1f dB (hardware %+.1f), peak %+.1f dB over it (hardware %+.1f) at %.0f Hz\n", R[j].dial10 / 10.0,
                   loss, R[j].loss, over, R[j].peak, kp * (double)FS / NF);
            ok &= fabs(loss - R[j].loss) < 2.0 && fabs(over - R[j].peak) < (j < 4 ? 3.5 : 8.0);
        }
        check("EMPHASIS 3 .. 7.5 on noise: the passband falls and the peak rises as the hardware's (loss within 2 dB, peak within 3.5 dB)", ok);
    }

    /* 4. bounded: a loud saw through every corner */
    {
        static const uint32_t CU[5] = {0, 32, 64, 96, 127}, EM[5] = {0, 64, 100, 112, 127}, NT[3] = {36, 60, 96};
        uint32_t a, b, c, k, ok = 1;
        double worst = 0, dc = 0;
        for (a = 0; a < 5u; a++)
            for (b = 0; b < 5u; b++)
                for (c = 0; c < 3u; c++) {
                    double mx = 0, mean = 0, ph = 0, inc = midi_hz(NT[c]) / FS;
                    for (k = 0; k < FS / 2; k++) {
                        ph += inc;
                        if (ph >= 1.0)
                            ph -= 1.0;
                        xin[k] = (int32_t)((2.0 * ph - 1.0) * 24000.0 * 3.0);    /* three saws at full level: the mixer overloaded */
                    }
                    run(y, FS / 2, ladder_cut((int32_t)CU[a] << 16) + ((int32_t)NT[c] - 41) * 16, (int32_t)EM[b] << 16, 0, xin);
                    for (k = FS / 4; k < FS / 2; k++) {
                        mx = fmax(mx, fabs(y[k]));
                        mean += y[k];
                    }
                    mean /= FS / 4;
                    worst = fmax(worst, mx);
                    dc = fmax(dc, fabs(mean));
                    ok &= mx < 4.0 * 24000 && isfinite(mx);
                }
        printf("   loudest %.0f, largest mean %.0f\n", worst, dc);
        check("a loud saw through every cutoff, emphasis and pitch stays below 4 x 24000 (no runaway)", ok);
    }

    /* 5. the high-pass output */
    {
        static double yl[FS / 2], yh[FS / 2];
        double lo_lp, lo_hp, hi_lp, hi_hp, ph;
        uint32_t a = FS / 8, b = FS / 2, k, m;
        int c;
        for (c = 0; c < 4; c++) {
            double inc = midi_hz(c < 2 ? 36 : 96) / FS, rr = 0;
            ph = 0;
            for (k = 0; k < b; k++) {
                ph += inc;
                if (ph >= 1.0)
                    ph -= 1.0;
                xin[k] = (int32_t)((2.0 * ph - 1.0) * 24000.0);
            }
            run(c < 2 ? yl : yh, b, ladder_cut(64 << 16), 0, (uint32_t)(c & 1), xin);
            for (m = a; m < b; m++)
                rr += (c < 2 ? yl[m] : yh[m]) * (c < 2 ? yl[m] : yh[m]);
            rr = sqrt(rr / (b - a));
            if (c == 0) lo_lp = rr; else if (c == 1) lo_hp = rr; else if (c == 2) hi_lp = rr; else hi_hp = rr;
        }
        printf("   HP: C2 LP %.0f HP %.0f (%.1f dB), C7 LP %.0f HP %.0f (%+.1f dB)\n", lo_lp, lo_hp, 20 * log10(lo_hp / lo_lp), hi_lp, hi_hp, 20 * log10(hi_hp / hi_lp));
        check("the high-pass output cuts a low note by 14 dB or more and passes a high one more than the low-pass", lo_hp < lo_lp * 0.2 && hi_hp > hi_lp * 1.5);
    }
    return bad ? 1 : 0;
}
