/* SPDX-License-Identifier: GPL-3.0-only
 * PARAPHONIC (P_VOICE = V_PARA): the voices sum and go through ONE filter that follows the newest voice. Against POLY (a filter per voice):
 * a single note is the same sound; a chord is as loud; a new key re-opens the filter for the notes already held (POLY leaves them alone);
 * the voices are allocated as in POLY; the shared filter is silent and clean when nothing plays; both filter models work. */
#define FELUCCA_FMINI_ONLY 1
#include "nano_host.h"
#include "../firmware/src/nano_app.c"
#include <stdio.h>
#include <math.h>
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
#define MAXS 88200
static double y[MAXS];
static uint32_t ny;
static void run(double sec)                   /* appends to y[] */
{
    static int32_t bb[CTL];
    uint32_t q, z, want = (uint32_t)(sec * 44100.0);
    for (q = 0; q + CTL <= want && ny + CTL <= MAXS; q += CTL) { track_render(&trk[0], bb, CTL); for (z = 0; z < CTL; z++) y[ny++] = bb[z] / 32768.0; }
}
static double rms(uint32_t a, uint32_t b) { double s = 0; uint32_t i; if (b > ny) b = ny; for (i = a; i < b; i++) s += y[i] * y[i]; return b > a ? sqrt(s / (b - a)) : 0; }
static double bright(uint32_t a, uint32_t b)       /* rms of the first difference over the rms */
{
    double d = 0; uint32_t i; if (b > ny) b = ny;
    for (i = a + 1; i < b; i++) d += (y[i] - y[i - 1]) * (y[i] - y[i - 1]);
    return rms(a, b) > 1e-6 ? sqrt(d / (b - a - 1)) / rms(a, b) : 0;
}
static void settle(void) { uint32_t b; int32_t o[2u * CTL]; for (b = 0; b < 400; b++) { events_block(CTL); mix_block(o, CTL); } }
static void setup(int mode, int type, int model)
{
    na_load(0); settle();
    trk[0].p[P_VOICE] = (int16_t)mode; trk[0].p[P_FM4_REL] = (int16_t)model;
    trk[0].p[P_E0] = (int16_t)type; trk[0].p[P_E1] = type == 0 ? 120 : 64; trk[0].p[P_E2] = 64; trk[0].p[P_E3] = type == 11 ? 127 : 64; trk[0].p[P_E4] = type == 14 ? 64 : 0;
    trk[0].p[P_E5] = 80; trk[0].p[P_E6] = 20; trk[0].p[P_E7] = 64; trk[0].p[P_SUS] = 100; trk[0].p[P_ATK] = 0; trk[0].p[P_ED_FX] = 0; trk[0].p[P_ED_FLT] = 0;
    trk[0].p[P_AHOLD] = 0; trk[0].p[P_AMODE] = 0;
}
static void clear_notes(void) { uint32_t q; static int32_t bb[CTL]; for (q = 0; q < 128u; q++) trk_note_off(&trk[0], q); for (q = 0; q < 200u; q++) track_render(&trk[0], bb, CTL); }
int main(void)
{
    static const int tys[15] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    uint32_t ti, model, na;
    setvbuf(stdout, 0, _IONBF, 0);
    { uint32_t gi; for (gi = 0; gi < G_COUNT; gi++) song.g[gi] = GP[gi].def; }
    host_tracks_init(); panel_init(); na_init(); song.master_q12 = 4096;
    /* 1. one note: PARA is as loud as POLY on every oscillator type and filter model */
    for (model = 0; model < 2u; model++)
        for (ti = 0; ti < 15u; ti++) {
            double l[2], b[2]; int m;
            for (m = 0; m < 2; m++) {
                setup(m ? V_PARA : V_POLY, tys[ti], (int)model);
                ny = 0; trk_note_on(&trk[0], 57, 100); run(0.5);
                l[m] = rms(4000, 20000); b[m] = bright(4000, 20000);
                clear_notes();
            }
            printf("type %2d %s: POLY %.4f PARA %.4f  bright %.3f %.3f\n", tys[ti], model ? "LADDER" : "SVF   ", l[0], l[1], b[0], b[1]);
            CHECK(l[1] > 1e-4, "type %d model %u: PARA is silent", tys[ti], model);
            if (tys[ti] == 7 || tys[ti] == 9 || tys[ti] == 14)       /* the excited types (noise, pluck, strings) differ from one note to the next */
                continue;
            CHECK(l[1] > l[0] * 0.7 && l[1] < l[0] * 1.4, "type %d model %u: PARA level %.4f against POLY %.4f", tys[ti], model, l[1], l[0]);
            CHECK(fabs(b[1] - b[0]) < 0.15 * b[0] + 0.01, "type %d model %u: PARA brightness %.3f against POLY %.3f", tys[ti], model, b[1], b[0]);
        }
    /* 2. a chord of three is as loud as in POLY, and allocated as POLY (three voices) */
    {
        double l[2]; int m;
        for (m = 0; m < 2; m++) {
            uint32_t q, c = 0;
            setup(m ? V_PARA : V_POLY, 0, 0);
            ny = 0; trk_note_on(&trk[0], 48, 100); trk_note_on(&trk[0], 52, 100); trk_note_on(&trk[0], 55, 100); run(0.5);
            for (q = 0; q < NVOICE; q++) c += trk[0].v[q].active;
            CHECK(c == 3u, "mode %d: %u voices for three notes", m, c);
            l[m] = rms(4000, 20000);
            clear_notes();
        }
        printf("chord: POLY %.4f PARA %.4f\n", l[0], l[1]);
        CHECK(l[1] > l[0] * 0.6 && l[1] < l[0] * 1.6, "chord level PARA %.4f against POLY %.4f", l[1], l[0]);
    }
    /* 3. the shared filter follows the newest voice: a held note, the filter's envelope settled at its sustain; a second key at the lowest velocity (nothing
     * of its own to hear) opens the filter again: in PARA the held note gets brighter, in POLY it does not */
    {
        double r[2]; int m, key;
        for (m = 0; m < 2; m++) {
            double w[2];
            for (key = 0; key < 2; key++) {                     /* the same note and window with and without the second key */
                setup(m ? V_PARA : V_POLY, 0, 0);
                trk[0].p[P_E5] = 30; trk[0].p[P_E6] = 0; trk[0].p[P_ED_FLT] = 100; trk[0].p[P_DEC] = 80; trk[0].p[P_SUS] = 15;
                trk[0].p[P_FM4_ATK] = 0;
                ny = 0; trk_note_on(&trk[0], 45, 100); run(1.0);
                na = ny;
                if (key) { uint32_t q; trk_note_on(&trk[0], 52, 1); for (q = 0; q < NVOICE; q++) if (trk[0].v[q].active && trk[0].v[q].note == 52) trk[0].v[q].vel = 1; }   /* (Nano plays every key at full velocity: turn this one down by hand) */
                run(0.05);
                w[key] = bright(na, ny);
                clear_notes();
            }
            r[m] = w[1] / (w[0] > 1e-6 ? w[0] : 1e-6);
            printf("%s: held note brightness %.3f alone, %.3f with a second key (x%.2f)\n", m ? "PARA" : "POLY", w[0], w[1], r[m]);
        }
        CHECK(r[1] > 3.0 * r[0] && r[1] > 2.0, "PARA: a new key does not re-open the filter (x%.2f)", r[1]);
        CHECK(r[0] < 1.4, "POLY: a new key changed the held note (x%.2f)", r[0]);
    }
    /* 4. silence: nothing plays, nothing comes out, and the shared state is clear, so the next note starts clean */
    {
        uint32_t q, ok = 1;
        setup(V_PARA, 0, 0); trk[0].p[P_E6] = 100;
        ny = 0; trk_note_on(&trk[0], 57, 127); run(0.3); clear_notes();
        ny = 0; run(0.1);
        for (q = 0; q < ny; q++) if (y[q] != 0) ok = 0;
        CHECK(ok, "PARA: output after the release is not silent");
        for (q = 0; q < 5u; q++) if (nano_pst[0][q]) ok = 0;
        CHECK(ok, "PARA: the shared filter's state is not cleared when nothing sounds");
    }
    /* 5. drive and clip act on the shared filter's output (the same SVF/LADDER controls); a resonant, driven chord stays finite and inside the scale */
    {
        double pk = 0; uint32_t q;
        setup(V_PARA, 0, 0); trk[0].p[P_E6] = 120; trk[0].p[P_ED_FX] = (7 << 1) | (7 << 4);
        ny = 0; trk_note_on(&trk[0], 40, 127); trk_note_on(&trk[0], 47, 127); trk_note_on(&trk[0], 52, 127); trk_note_on(&trk[0], 59, 127); run(0.6);
        for (q = 0; q < ny; q++) if (fabs(y[q]) > pk) pk = fabs(y[q]);
        printf("hot chord peak %.3f\n", pk);
        CHECK(pk < 1.5 && pk > 0.01, "a hot PARA chord peaks at %.3f", pk);
        clear_notes();
    }
    printf("%s\n", fails ? "nano_para_test FAIL" : "nano_para_test ok");
    return fails ? 1 : 0;
}
