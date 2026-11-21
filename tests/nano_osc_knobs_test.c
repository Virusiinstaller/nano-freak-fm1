/* SPDX-License-Identifier: GPL-3.0-only
 * OSCILLATOR KNOBS: for each of the 15 oscillator types, K1..K4 (turned from their lowest to their highest) must change the sound
 * that comes out and the waveform sketch the painter draws, and K4's label must be the test bed's (SYNC, STRETCH, MORPH, DAMP or FOLD). */
#define FELUCCA_FMINI_ONLY 1
#include "nano_host.h"
#include "../firmware/src/nano_app.c"
#include <stdio.h>
#include <math.h>
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
static void nframe(void) { host_ticks += 16000u; fm1_ms += 16u; na_input(); }
static void npress(uint32_t label)
{
    fm1_in.buttons |= 1u << panel.btn[label]; host_pressed |= 1u << panel.btn[label]; nframe();
    fm1_in.buttons &= ~(1u << panel.btn[label]); nframe();
}
static void nturn(uint32_t role, int32_t d) { host_enc[panel.enc[role]] += d * panel.dir[role]; nframe(); host_ticks += 200000u; }

#define MAXS (44100 * 4)
static double y[MAXS];
static uint32_t ny;
static void render(double sec)        /* appends to y[] (left channel) */
{
    int32_t o[2u * CTL];
    uint32_t want = (uint32_t)(sec * 44100.0), i, k;
    ny = 0;
    for (i = 0; i < want && ny + CTL <= MAXS; i += CTL) {
        memset(o, 0, sizeof o);
        events_block(CTL);
        mix_block(o, CTL);
        for (k = 0; k < CTL; k++) y[ny++] = o[2u * k] / 32768.0;
    }
}
static double rms(uint32_t a, uint32_t b)
{
    double s = 0; uint32_t i;
    if (b > ny) b = ny;
    for (i = a; i < b; i++) s += y[i] * y[i];
    return b > a ? sqrt(s / (b - a)) : 0;
}
static double pitch(uint32_t a, uint32_t b)       /* zero crossings -> Hz */
{
    uint32_t i, c = 0;
    if (b > ny) b = ny;
    for (i = a + 1; i < b; i++) if ((y[i - 1] < 0) != (y[i] < 0)) c++;
    return c * 44100.0 / 2.0 / (b - a);
}
/* how many different pitches the signal plays, in 60 ms windows (a change of more than 4 %) */
static uint32_t pitch_steps(double *lo, double *hi)
{
    uint32_t w = 2646, a, n = 0; double last = 0, f;
    *lo = 1e9; *hi = 0;
    for (a = 0; a + w <= ny; a += w) {
        if (rms(a, a + w) < 0.003) continue;
        f = pitch(a, a + w);
        if (f < *lo) *lo = f;
        if (f > *hi) *hi = f;
        if (last == 0 || fabs(f - last) > 0.04 * last) n++;
        last = f;
    }
    return n;
}
static void kdown(uint32_t k) { fm1_in.notes |= 1u << k; }
static void kup(uint32_t k) { fm1_in.notes &= ~(1u << k); }
static void settle(void) { uint32_t b; int32_t o[2u * CTL]; for (b = 0; b < 400; b++) { events_block(CTL); mix_block(o, CTL); } }
static void fresh(uint32_t preset)
{
    uint32_t i, k;
    song.playing = 0; transport_req = 0;
    na_load(preset);
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_AHOLD] = 0;
        trk[i].p[P_AMODE] = 0;
        for (k = 0; k < 16u && trk[i].nheld; k++) input_off(&trk[i], trk[i].held[0]);
        trk[i].nheld = 0;
    }
    fm1_in.notes = 0; na_cur = 0; na_root = 60;
    settle();
}


static int32_t vpc(void) { uint32_t q, b = 0; for (q = 1; q < NVOICE; q++) if (trk[0].v[q].active && trk[0].v[q].gate && (!trk[0].v[b].gate || trk[0].v[q].age > trk[0].v[b].age)) b = q; return trk[0].v[b].pitch_cur; }
static uint8_t ev[512]; static double evt[512]; static uint32_t nev;
static double ev_rms;
static void observe(double sec)
{
    int32_t o[2u * CTL]; uint32_t blk = (uint32_t)(sec * 44100.0) / CTL, b, v, k; uint8_t cur[128]; double e = 0; uint32_t cnt = 0;
    memset(cur, 0, sizeof cur); nev = 0;
    for (b = 0; b < blk; b++) {
        uint8_t now[128];
        memset(o, 0, sizeof o);
        mix_block(o, CTL);
        memset(now, 0, sizeof now);
        for (v = 0; v < NVOICE; v++) if (trk[0].v[v].active) now[trk[0].v[v].note & 127u] = 1;
        for (k = 0; k < 128u; k++) if (now[k] && !cur[k] && nev < 512u) { ev[nev] = (uint8_t)k; evt[nev++] = b * CTL / 44100.0; }
        memcpy(cur, now, sizeof cur);
        for (k = 0; k < CTL; k++) { e += (double)o[2u * k] * o[2u * k] / (32768.0 * 32768.0); cnt++; }
    }
    ev_rms = sqrt(e / cnt);
}
static void show(const char *what)
{
    uint32_t i;
    printf("%-34s %3u notes, rms %.3f:", what, nev, ev_rms);
    for (i = 0; i < nev && i < 16u; i++) printf(" %d", ev[i]);
    if (nev) printf("  (every %.0f ms)", nev > 1 ? (evt[nev - 1] - evt[0]) / (nev - 1) * 1000.0 : 0.0);
    printf("\n");
}
static void goto_page(uint32_t btn, uint32_t sb) { uint32_t j; npress(B_HOME); npress(btn); for (j = 0; j < 12u && na_cursub() != sb; j++) nturn(EN_SELECT, 1); }
static void kn(uint32_t k, int32_t d) { nturn(EN_K1 + k, d); }

typedef struct { double L, B, P, SL, SB, SP; } prof_t;      /* level, brightness, pitch (Hz), and their spread over 30 ms windows */
static prof_t profile(uint32_t from)
{
    prof_t r; double l[100], b[100], p[100], ml = 0, mb = 0, mp = 0; uint32_t m, n = 0;
    memset(&r, 0, sizeof r);
    for (m = 0; from + (m + 1u) * 1323u <= ny && n < 100u; m++) {
        uint32_t a = from + m * 1323u, e = a + 1323u, q; double de = 0;
        for (q = a + 1u; q < e; q++) de += (y[q] - y[q - 1u]) * (y[q] - y[q - 1u]);
        l[n] = rms(a, e); b[n] = l[n] > 1e-4 ? sqrt(de / 1322.0) / l[n] : 0; p[n] = pitch(a, e);
        ml += l[n]; mb += b[n]; mp += p[n]; n++;
    }
    if (!n) return r;
    r.L = ml / n; r.B = mb / n; r.P = mp / n;
    for (m = 0; m < n; m++) { r.SL += (l[m] - r.L) * (l[m] - r.L); r.SB += (b[m] - r.B) * (b[m] - r.B); r.SP += (p[m] - r.P) * (p[m] - r.P); }
    r.SL = sqrt(r.SL / n); r.SB = sqrt(r.SB / n); r.SP = sqrt(r.SP / n);
    return r;
}
static void pshow(const char *w, prof_t p) { printf("%-34s level %.3f (sd %.3f)  bright %.3f (sd %.3f)  pitch %.1f (sd %.1f)\n", w, p.L, p.SL, p.B, p.SB, p.P, p.SP); }
/* a bright held note, 0.9 s, then its release 0.5 s: ny holds both */
static prof_t take(void) { kdown(12); render(0.9); kup(12); { prof_t p = profile(0); render(0.5); return p; } }
static void setup_bright(void) { fresh(0); trk[0].p[P_E0] = 0; trk[0].p[P_E1] = 120; trk[0].p[P_E5] = 70; trk[0].p[P_E6] = 0; trk[0].p[P_SUS] = 100; }
#define SET(btn, pg, k, v) do { goto_page(btn, pg); kn(k, -127); kn(k, v); npress(B_HOME); } while (0)


static double wa[MAXS];
static int8_t sk[120];
static void grab_sketch(int8_t *d) { uint32_t i; int8_t *w; na_sync(); w = nf_ui_wave(); for (i = 0; i < 120u; i++) d[i] = w[i]; }
int main(void)
{
    uint32_t ty, k, i, nbad = 0;
    int8_t skA[120], skB[120];
    static const char *const K4L[15] = {"SYNC", "FOLD", "MORPH", "STRETCH", "FOLD", "FOLD", "FOLD", "FOLD", "FOLD", "FOLD", "FOLD", "FOLD", "FOLD", "FOLD", "DAMP"};
    setvbuf(stdout, 0, _IONBF, 0);
    { uint32_t gi; for (gi = 0; gi < G_COUNT; gi++) song.g[gi] = GP[gi].def; }
    host_tracks_init(); panel_init(); na_init(); song.master_q12 = 4096;
    settle();
    for (ty = 0; ty < 15u; ty++) {
        for (k = 1; k <= 4u; k++) {
            prof_t pa, pb; double dw = 0, ea = 0, dl, db, dp, sd = 0, sn = 0, tail[2] = {0, 0};
            int snd, skf, pass; uint32_t n;
            for (pass = 0; pass < 2; pass++) {
                fresh(0);
                trk[0].p[P_E0] = NOSC[ty].ty; trk[0].p[P_E1] = ty == 0u ? 127 : 64; trk[0].p[P_E2] = 64; trk[0].p[P_E3] = NOSC[ty].ty == 11 ? 127 : 64; trk[0].p[P_E4] = ty == 14u ? 64 : 0;
                trk[0].p[P_E5] = 127; trk[0].p[P_E6] = 0; trk[0].p[P_E7] = 64; trk[0].p[P_SUS] = 100; trk[0].p[P_ATK] = 0; trk[0].p[P_ED_FX] = 0;
                trk[0].p[P_E0 + k] = pass ? (NOSC[ty].ty == 13 && k == 2u ? 64 : 127) : 0;   /* (V.ANALOG SHAPE: the sync square fills the whole period above ~80, a flat line the sketch centres away) */
                goto_page(B_HOME, 0); nframe(); nframe();
                grab_sketch(pass ? skB : skA);
                CHECK(!strcmp(nf_ui_str(UIS_L0 + 3), K4L[ty]), "%s: K4 is labelled %s, not %s", NOSC[ty].name, nf_ui_str(UIS_L0 + 3), K4L[ty]);
                {                                                  /* the note as the engine test plays it (A3, velocity 100): the keys' own low note starves Strings */
                    static int32_t bb[CTL]; uint32_t q, z;
                    trk_note_on(&trk[0], 57, 100);
                    for (ny = 0, q = 0; q + CTL <= 26460u; q += CTL) { track_render(&trk[0], bb, CTL); for (z = 0; z < CTL; z++) y[ny++] = bb[z] / 32768.0; }
                    trk_note_off(&trk[0], 57);
                }
                tail[pass] = rms(2200, 4400) * 0 + rms(8800, 11000) + rms(15000, 17000) + rms(22000, 24000);
                { prof_t p = profile(4000); if (pass) pb = p; else pa = p; }
                n = ny > 8000 ? 8000 : ny;
                if (!pass) { for (i = 0; i < n; i++) wa[i] = y[i]; }
                else { for (i = 4000; i < n; i++) { dw += fabs(y[i] - wa[i]); ea += fabs(wa[i]) + fabs(y[i]); } }
            }
            for (i = 0; i < 120u; i++) { sd += abs(skA[i] - skB[i]); sn += abs(skA[i]) + abs(skB[i]); }
            dl = fabs(pb.L - pa.L) / (pa.L + pb.L + 1e-6) * 200; db = fabs(pb.B - pa.B) / (pa.B + pb.B + 1e-6) * 200; dp = fabs(pb.P - pa.P) / (pa.P + pb.P + 1e-6) * 200;
            dw = ea > 0 ? dw / ea * 100 : 0; sd = sn > 0 ? sd / sn * 100 : 0;
            snd = (dl > 3 || db > 5 || dp > 3 || (NOSC[ty].ty != 4 && dw > 15));
            if (NOSC[ty].ty == 14 && k == 4u)                                  /* STRINGS DAMP: how long it rings */
                { snd = fabs(tail[1] - tail[0]) > 0.03 * (tail[0] + tail[1]); printf("  STRINGS DAMP ring-out level %.5f (0) / %.5f (127)\n", tail[0], tail[1]); }
            skf = sd > 2.0;
            printf("%-11s K%u | sound %5.1f %5.1f %5.1f %5.1f | sketch %5.1f%s%s\n", NOSC[ty].name, k, dl, db, dp, dw, sd, snd ? "" : "  NO SOUND CHANGE", skf ? "" : "  NO SKETCH CHANGE");
            CHECK(snd, "%s K%u does not change the sound", NOSC[ty].name, k);
            CHECK(skf, "%s K%u does not change the waveform sketch", NOSC[ty].name, k);
            if (!snd || !skf) nbad++;
        }
    }
    /* POLY: successive single notes rotate through the voices; Strings, Modal, Pluck and the SAW comb keep state for four only */
    {
        static const int tys[4] = {14, 6, 9, 11}; static const char *const tn[4] = {"STRINGS", "MODAL", "KARPLUS", "SAW comb"};
        uint32_t ti, nn;
        for (ti = 0; ti < 4u; ti++) {
            double first = 0, worst = 1e9;
            fresh(0);
            trk[0].p[P_VOICE] = V_POLY; trk[0].p[P_E0] = (int16_t)tys[ti]; trk[0].p[P_E1] = 64; trk[0].p[P_E2] = 64; trk[0].p[P_E3] = 127; trk[0].p[P_E4] = 100;
            trk[0].p[P_E5] = 127; trk[0].p[P_E7] = 64; trk[0].p[P_SUS] = 100;
            for (nn = 0; nn < 14u; nn++) {
                static int32_t bb[CTL]; double s = 0; uint32_t q, z, note = 52u + nn % 5u;
                trk_note_on(&trk[0], note, 100);
                for (q = 0; q < 11025u; q += CTL) { track_render(&trk[0], bb, CTL); for (z = 0; z < CTL; z++) s += (double)bb[z] * bb[z]; }
                trk_note_off(&trk[0], note);
                for (q = 0; q < 17640u; q += CTL) track_render(&trk[0], bb, CTL);
                s = sqrt(s / 11025.0);
                if (!nn) first = s;
                if (s < worst) worst = s;
            }
            printf("POLY %-8s 14 single notes: first %.0f, quietest %.0f\n", tn[ti], first, worst);
            CHECK(worst > 0.5 * first, "%s goes silent or dry on a later note in POLY (the voice rotation passes the four it keeps state for)", tn[ti]);
        }
    }
    /* GLIDE in POLY: a separate note slides from the previous one (it used to start from whichever free voice sat nearest, often no slide) */
    {
        uint32_t vm, q, nn, slid = 0;
        for (vm = 0; vm < 2u; vm++) {
            static int32_t bb[CTL]; int32_t seen = 0;
            fresh(0);
            CHECK(trk[0].p[P_GLMODE] == 1, "GLIDE does not start in TIME mode");
            trk[0].p[P_VOICE] = vm ? V_POLY : V_MONO; trk[0].p[P_E0] = 0; trk[0].p[P_GLIDE] = 30; trk[0].p[P_REL] = 30; trk[0].p[P_SUS] = 100;
            for (nn = 0; nn < 6u; nn++) {                       /* 36, 48, 36, 48 ..: each note should start a long way from its pitch */
                uint32_t note = 36u + 12u * (nn & 1u), vi;
                trk_note_on(&trk[0], note, 100);
                for (q = 0; q < 2205u; q += CTL) track_render(&trk[0], bb, CTL);
                for (vi = 0; vi < NVOICE; vi++)
                    if (trk[0].v[vi].active && trk[0].v[vi].note == note && nn && abs(trk[0].v[vi].pitch_cur - trk[0].v[vi].pitch16) > 96) seen++;
                for (q = 0; q < 28665u; q += CTL) track_render(&trk[0], bb, CTL);   /* the glide finishes while the note is held */
                trk_note_off(&trk[0], note);
                for (q = 0; q < 4410u; q += CTL) track_render(&trk[0], bb, CTL);
            }
            printf("GLIDE %s: %d of 5 later notes still sliding after 50 ms\n", vm ? "POLY" : "MONO", (int)seen);
            CHECK(seen >= 4, "%s: a later note does not slide from the one before", vm ? "POLY" : "MONO");
            slid += (uint32_t)seen;
        }
    }
    /* the sketch sits on the zero line (its mean is nil) and keeps its scale: turning a knob that makes the sound softer lowers the peaks */
    {
        uint32_t kk; int8_t s0[120]; int worst = 0;
        for (ty = 0; ty < 15u; ty++)
            for (kk = 0; kk < 5u; kk++) {
                int sum = 0, pk = 0;
                fresh(0);
                trk[0].p[P_E0] = NOSC[ty].ty; trk[0].p[P_E1] = 64; trk[0].p[P_E2] = 64; trk[0].p[P_E3] = 64; trk[0].p[P_E4] = 0;
                if (kk) trk[0].p[P_E0 + kk] = (int16_t)(kk * 30);
                goto_page(B_HOME, 0); nframe(); nframe();
                grab_sketch(s0);
                for (i = 0; i < 120u; i++) { sum += s0[i]; if (abs(s0[i]) > pk) pk = abs(s0[i]); }
                if (abs(sum / 120) > worst) worst = abs(sum / 120);
                CHECK(abs(sum / 120) <= 3, "%s: the sketch is not centred on zero (mean %d)", NOSC[ty].name, sum / 120);
            }
        printf("   sketch centring: the largest mean offset is %d of 127\n", worst);
    }
    printf("%u of 60 knob/type pairs without a sound or sketch change\n", nbad);
    return fails ? 1 : 0;
}
