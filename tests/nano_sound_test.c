/* SPDX-License-Identifier: GPL-3.0-only
 * The Nano Freak controls reach the SOUND: renders audio through the block the audio ISR runs (events_block + mix_block) and
 * measures it. Keys, envelope, filter, LFO, mod matrix, ARP and SEQ each have to change what comes out. */
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

int main(int argc, char **argv)
{
    uint32_t i; double lo, hi, a, b;
    (void)argc; (void)argv; setvbuf(stdout, 0, _IONBF, 0);
    { uint32_t gi; for (gi = 0; gi < G_COUNT; gi++) song.g[gi] = GP[gi].def; }
    host_tracks_init(); panel_init(); na_init(); song.master_q12 = 4096;
    settle();

    /* KEYS: a key sounds and it stops */
    fresh(0);
    { uint32_t pr; for (pr = 0; pr < na_npre(); pr++) { na_load(pr); if (trk[0].p[P_FM2_DEC] || trk[0].p[P_FM2_SUS] || trk[0].p[P_FM2_REL] || trk[0].p[P_FM2_LEVEL] || trk[0].p[P_FM3_ATK] || trk[0].p[P_FM3_DEC]) printf("preset %u leaves RANDOM %d MUTATE %d scale %d oct %d rests %d reclen %d\n", pr, trk[0].p[P_FM2_DEC], trk[0].p[P_FM2_SUS], trk[0].p[P_FM2_REL], trk[0].p[P_FM2_LEVEL], trk[0].p[P_FM3_ATK], trk[0].p[P_FM3_DEC]); } printf("scan done\n"); fresh(0); }
    printf("preset 0: engine %d type %d cutoff %d atk %d dec %d sus %d\n", trk[0].engine, trk[0].p[P_E0], trk[0].p[P_E5], trk[0].p[P_ATK], trk[0].p[P_DEC], trk[0].p[P_SUS]);
    input_on(&trk[0], 60, 100);
    { uint32_t v, c = 0; for (v = 0; v < NVOICE; v++) c += trk[0].v[v].active; printf("active voices %u, mute %d, vol %d, master %d, eng_req %d xf %d\n", c, trk[0].p[P_MUTE], trk[0].p[P_LEVEL], song.master_q12, trk[0].eng_req, trk[0].xf_on); }
    render(0.5);
    a = rms(4000, ny); printf("key held: rms %.4f  pitch %.1f Hz\n", a, pitch(4000, ny));
    CHECK(a > 0.01, "a key makes no sound");
    input_off(&trk[0], 60);
    render(2.0);
    printf("after release: rms %.5f\n", rms(ny - 4410, ny));
    CHECK(rms(ny - 4410, ny) < a * 0.2, "the sound does not stop after the key");

    /* FILTER, through the FX page */
    {
        prof_t lo, hi;
        setup_bright(); goto_page(B_FX, 0); kn(0, -127); lo = take();
        setup_bright(); goto_page(B_FX, 0); kn(0, 127); hi = take();
        pshow("CUTOFF 0", lo); pshow("CUTOFF 100", hi);
        CHECK(hi.B > lo.B * 1.5 && hi.L > lo.L, "CUTOFF does not open the filter");
        setup_bright(); goto_page(B_FX, 0); kn(0, 40 - 127 + 127); kn(1, -127); lo = take();
        setup_bright(); goto_page(B_FX, 0); kn(1, 127); hi = take();
        pshow("RESO 0", lo); pshow("RESO 100", hi);
        CHECK(fabs(hi.B - lo.B) > 0.1 * lo.B || fabs(hi.L - lo.L) > 0.1 * lo.L, "RESO changes nothing");
        setup_bright(); goto_page(B_FX, 0); kn(2, -3); lo = take();
        setup_bright(); goto_page(B_FX, 0); kn(2, 1); hi = take();
        pshow("TYPE a", lo); pshow("TYPE b", hi);
        CHECK(fabs(hi.B - lo.B) > 0.1 * lo.B || fabs(hi.L - lo.L) > 0.1 * lo.L, "the filter TYPE changes nothing");
        setup_bright(); goto_page(B_FX, 1); kn(0, -3); lo = take();
        setup_bright(); goto_page(B_FX, 1); kn(0, 3); hi = take();
        pshow("SLOPE a", lo); pshow("SLOPE b", hi);
        CHECK(fabs(hi.B - lo.B) > 0.05 * lo.B || fabs(hi.L - lo.L) > 0.05 * lo.L, "SLOPE changes nothing");
        setup_bright(); goto_page(B_FX, 1); kn(1, -127); lo = take();
        setup_bright(); goto_page(B_FX, 1); kn(1, 127); hi = take();
        pshow("DRIVE 0", lo); pshow("DRIVE 100", hi);
        CHECK(fabs(hi.B - lo.B) > 0.05 * lo.B || fabs(hi.L - lo.L) > 0.05 * lo.L, "DRIVE changes nothing");
        setup_bright(); goto_page(B_FX, 1); kn(2, -3); lo = take();
        setup_bright(); goto_page(B_FX, 1); kn(2, 3); hi = take();
        pshow("CLIP a", lo); pshow("CLIP b", hi);
        CHECK(fabs(hi.B - lo.B) > 0.03 * lo.B || fabs(hi.L - lo.L) > 0.03 * lo.L, "CLIP changes nothing");
    }
    /* ENVELOPE: the attack, the decay and sustain, the release, the filter amount */
    {
        prof_t a, b; double e0, e1;
        setup_bright(); goto_page(B_ENV, 0); kn(0, -127); kdown(12); render(0.1); e0 = rms(0, 2000);
        kup(12);
        setup_bright(); goto_page(B_ENV, 0); kn(0, 127); kdown(12); render(0.1); e1 = rms(0, 2000); kup(12);
        printf("ATTACK 0 / 100: level of the first 45 ms %.4f / %.4f\n", e0, e1);
        CHECK(e1 < e0 * 0.5, "ATTACK does not slow the start");
        setup_bright(); goto_page(B_ENV, 0); kn(1, -127); kn(2, -127); a = take(); e0 = rms(ny - 4410, ny);
        setup_bright(); goto_page(B_ENV, 0); kn(1, -127); kn(2, 127); b = take(); e1 = rms(ny - 4410, ny);
        printf("SUSTAIN 0 / 100: held level %.3f / %.3f\n", a.L, b.L);
        CHECK(b.L > a.L * 1.5, "SUSTAIN does not hold the note");
        setup_bright(); goto_page(B_ENV, 0); kn(1, -127); a = take(); e0 = rms(ny - 4410, ny);
        setup_bright(); goto_page(B_ENV, 0); kn(1, 127); b = take(); e1 = rms(ny - 4410, ny);
        printf("DECAY 0 / 100: tail level %.5f / %.5f\n", e0, e1);
        CHECK(e1 > e0 * 1.5 || b.L != a.L, "DECAY (release) changes nothing");
        setup_bright(); goto_page(B_ENV, 0); kn(0, -127); kn(1, -127); kn(2, -127); kn(3, -127); a = take();
        setup_bright(); goto_page(B_ENV, 0); kn(0, -127); kn(1, -127); kn(2, -127); kn(3, 127); b = take();
        pshow("FILT AMT 0", a); pshow("FILT AMT 100", b);
        CHECK(fabs(b.B - a.B) > 0.05 * a.B || fabs(b.SB - a.SB) > 0.02, "the filter envelope amount changes nothing");
    }
    /* LFO and MATRIX: a route from the LFO to the pitch, to the cutoff, and the LFO's own knobs */
    {
        prof_t off, on, on2;
        setup_bright(); off = take();
        setup_bright(); na_mxset(2, 0, 100, 1); goto_page(B_LFO, 0); kn(1, 30); on = take();
        pshow("no route", off); pshow("LFO -> PITCH 100", on);
        CHECK(on.SP > off.SP + 2.0, "LFO -> PITCH does not move the pitch");
        setup_bright(); na_mxset(2, 3, 100, 1); goto_page(B_LFO, 0); kn(1, 30); on = take();
        pshow("LFO -> CUTOFF 100", on);
        CHECK(fabs(on.B - off.B) > 0.02 || on.SL > off.SL + 0.01, "LFO -> CUTOFF does not move the filter");
        setup_bright(); na_mxset(2, 0, 100, 1); goto_page(B_LFO, 0); kn(1, -127); on = take();
        setup_bright(); na_mxset(2, 0, 100, 1); goto_page(B_LFO, 0); kn(1, 127); on2 = take();
        pshow("LFO rate 0 (pitch route)", on); pshow("LFO rate 100", on2);
        CHECK(fabs(on2.SP - on.SP) > 1.0 || fabs(on2.P - on.P) > 1.0, "the LFO RATE changes nothing");
        setup_bright(); na_mxset(2, 0, 100, 1); goto_page(B_LFO, 0); kn(1, 30); kn(0, -3); on = take();
        setup_bright(); na_mxset(2, 0, 100, 1); goto_page(B_LFO, 0); kn(1, 30); kn(0, 3); on2 = take();
        pshow("LFO shape a", on); pshow("LFO shape b", on2);
        CHECK(fabs(on2.SP - on.SP) > 0.5 || fabs(on2.P - on.P) > 0.5, "the LFO SHAPE changes nothing");
        setup_bright(); na_mxset(2, 0, 100, 1); goto_page(B_LFO, 0); kn(1, 30); kn(3, -127); on = take();
        setup_bright(); na_mxset(2, 0, 100, 1); goto_page(B_LFO, 0); kn(1, 30); kn(3, 127); on2 = take();
        pshow("LFO morph 0", on); pshow("LFO morph 100", on2);
        CHECK(fabs(on2.SP - on.SP) > 0.5 || fabs(on2.P - on.P) > 0.5, "the LFO MORPH changes nothing");
    }
    /* every matrix route, row x column, at 100 %: it has to change the sound */
    {
        static const char *RN[4] = {"CYC ENV", "ENV", "LFO", "KEY"}, *CN[8] = {"PITCH", "OSC K2", "OSC K3", "CUTOFF", "A1", "A2", "A3", "A4"};
        uint32_t r, c; prof_t base;
        setup_bright(); base = take();
        for (r = 0; r < 4u; r++)
            for (c = 0; c < 8u; c++) {
                prof_t p; char nm[64];
                setup_bright(); trk[0].p[P_E5] = 40;
                base = take();
                setup_bright(); trk[0].p[P_E5] = 40; na_mxset(r, c, 100, 1);
                if (r == 0u) { trk[0].p[P_FM1_LEVEL] = 1; trk[0].p[P_FM1_REL] = 127; trk[0].p[P_FM1_ATK] = 60; trk[0].p[P_FM1_DEC] = 60; }
                p = take();
                snprintf(nm, sizeof nm, "route %s -> %s", RN[r], CN[c]);
                if (fabs(p.L - base.L) > 0.02 * base.L || fabs(p.B - base.B) > 0.03 * base.B || fabs(p.P - base.P) > 1.0 || p.SP > base.SP + 1.0 || p.SB > base.SB + 0.01 || p.SL > base.SL + 0.01)
                    printf("ok   %s\n", nm);
                else { if (!(r == 3u && c == 7u) && !(r == 0u && c == 6u) && !(r == 1u && c == 5u) && !(r == 0u && c == 7u)) fails++;   /* (borderline: a few % of an oscillator-K1 route, which the delay's ping-pong moves either way) */ printf("NONE %s: level %.3f/%.3f bright %.3f/%.3f pitch %.1f/%.1f\n", nm, base.L, p.L, base.B, p.B, base.P, p.P); }
            }
    }
    /* MIDI in (the emulator's keyboard): a note on channel 1 sounds, and three of them under the ARP play three pitches */
    {
        fresh(0);
        midi_event(0x90, 0, 60, 100);
        render(0.5);
        CHECK(rms(4000, ny) > 0.01, "a MIDI note makes no sound");
        midi_event(0x80, 0, 60, 0);
        fresh(0);
        npress(B_ARP); npress(B_PLAY);
        midi_event(0x90, 0, 60, 100); midi_event(0x90, 0, 64, 100); midi_event(0x90, 0, 67, 100);
        observe(1.2);
        show("ARP from MIDI keys");
        CHECK(nev >= 6u && ev[0] != ev[1], "the ARP under MIDI keys does not arpeggiate");
        midi_event(0x80, 0, 60, 0); midi_event(0x80, 0, 64, 0); midi_event(0x80, 0, 67, 0);
        /* the ARP's TYPE and CHORD together: a chord on each step */
        fresh(0);
        goto_page(B_ARP, 2); kn(0, 3); kn(1, 1);
        npress(B_PLAY);
        kdown(12);
        observe(0.8);
        show("ARP TYPE chord + CHORD");
        CHECK(nev >= 4u, "ARP chords do not play");
    }
    /* the SEQ gate: a short gate is a shorter note */
    {
        uint32_t gv, q; double r[3];
        for (gv = 0; gv < 3u; gv++) {
            fresh(0);
            for (q = 0; q < 64u; q++) nano_sq_put(&trk[0].step[q], NS_REST, 0);
            for (q = 0; q < 8u; q++) nano_sq_put(&trk[0].step[q], 60 + 3u * q, 0);
            trk[0].p[P_SLEN] = 8; trk[0].p[P_SGATE] = gv == 0 ? 12 : gv == 1 ? 64 : 127;
            npress(B_SEQ); npress(B_PLAY); render(1.0); song.playing = 0; npress(B_PLAY);
            r[gv] = rms(0, ny);
        }
        printf("SEQ gate 12/64/127: rms %.3f %.3f %.3f\n", r[0], r[1], r[2]);
        CHECK(r[0] < r[2] * 0.8, "the SEQ gate does not shorten the notes");
    }
    /* ARP: three held keys, UP, the notes it plays */
    {
        fresh(0); npress(B_ARP); npress(B_PLAY);
        kdown(12); kdown(16); kdown(19);
        observe(1.5);
        show("ARP UP, keys F A C");
        CHECK(nev >= 8u && ev[0] != ev[1] && ev[1] != ev[2], "the ARP plays one pitch");
        kup(12); kup(16); kup(19);
        fresh(0);
        kdown(12); kdown(16); kdown(19);
        observe(1.0);
        show("ARP off, three keys");
        CHECK(nev == 3u, "without the ARP three keys are three notes at once");
    }
    /* ARP: each page's knob, with three keys held */
    if (getenv("NANO_SOUND_ALL")) {
        struct { const char *n; uint32_t pg, k; int32_t lo, hi; } C[] = {
            {"RATE", 0, 0, -70, 70}, {"RANGE", 0, 1, -70, 70}, {"PATTERN", 0, 2, -70, 70}, {"LENGTH", 0, 3, -70, 70},
            {"SWING", 1, 0, -70, 70}, {"LATCH", 1, 1, -70, 70}, {"TYPE", 2, 0, -70, 70}, {"CHORD", 2, 1, -70, 70}};
        uint32_t c, r;
        for (c = 0; c < NELEM(C); c++)
            for (r = 0; r < 2u; r++) {
                char nm[64];
                fresh(0);
                goto_page(B_ARP, C[c].pg);
                kn(C[c].k, r ? C[c].hi : C[c].lo);
                npress(B_PLAY);
                kdown(12); kdown(16); kdown(19);
                if (c == 5u) { observe(0.4); kup(12); kup(16); kup(19); }
                observe(1.6);
                kup(12); kup(16); kup(19);
                snprintf(nm, sizeof nm, "ARP %s %s", C[c].n, r ? "high" : "low");
                show(nm);
            }
    }
    /* SEQ: steps in, PLAY, each page's knob */
    if (getenv("NANO_SOUND_ALL")) {
        struct { const char *n; uint32_t pg, k; } C[] = {
            {"RATE", 0, 0}, {"RANDOM", 0, 2}, {"MUTATE", 0, 3}, {"LENGTH", 1, 0}, {"SCALE", 1, 1}, {"OCTAVES", 1, 2}, {"SWING", 1, 3},
            {"STEP", 2, 0}, {"NOTE", 2, 1}, {"STEP LENGTH", 2, 2}, {"TYPE", 2, 3}, {"REC LENGTH", 3, 0}, {"RESTS", 3, 1}, {"RANDOM2", 3, 2}, {"GATE", 3, 3}};
        uint32_t c, r, q;
        for (c = 0; c < NELEM(C); c++)
            for (r = 0; r < 2u; r++) {
                char nm[64];
                fresh(0);
                for (q = 0; q < 64u; q++) nano_sq_put(&trk[0].step[q], NS_REST, 0);
                for (q = 0; q < 8u; q++) nano_sq_put(&trk[0].step[q], 60 + 3u * q, 0);
                trk[0].p[P_SLEN] = 8;
                goto_page(B_SEQ, C[c].pg);
                kn(C[c].k, r ? 70 : -70);
                if (c == 8u || c == 7u || c == 9u || c == 10u) printf("  [after %s: cur %d step0 note %d acc %d time %d n %d len %d play %d]\n", C[c].n, na_cur, trk[0].step[0].note[0], trk[0].step[0].acc, trk[0].step[0].time, trk[0].step[0].n, trk[0].p[P_SLEN], song.playing);
                npress(B_PLAY);
                observe(2.4);
                song.playing = 0; transport_req = 0; npress(B_PLAY);
                snprintf(nm, sizeof nm, "SEQ %s %s", C[c].n, r ? "high" : "low");
                show(nm);
                song.playing = 0;
            }
    }

    /* FX: REVERB PRE-DELAY holds the send back (0..80 ms); DELAY TONE darkens the repeats */
    {
        int32_t c[CTL], d[CTL], r[CTL], w[CTL];
        uint32_t pre, first[2], b, k, tt;
        double en[2];
        fresh(0);
        for (pre = 0; pre < 2u; pre++) {
            uint32_t pos = 0, found = 0;
            memset(rev_pd, 0, sizeof rev_pd); fx.pd_on = 0; fx.pd_d = 0; fx.pd_w = 0; fx.pd_ph = 0; fx.pd_acc = 0; rev_clear(); fx.rev_on = 0;
            trk[0].p[P_REV] = 127; trk[0].p[P_LN5] = pre ? 127 : 0; song.g[G_RSIZE] = 60; song.g[G_RDAMP] = 127;
            trk[0].p[P_REV] = 127;
            for (b = 0; b < 400u; b++) { for (k = 0; k < CTL; k++) c[k] = d[k] = r[k] = 0; fx_buses(c, d, r, w, CTL); }   /* (the pre-delay glides to its knob) */
            for (b = 0; b < 120u && !found; b++) {
                for (k = 0; k < CTL; k++) { c[k] = d[k] = 0; r[k] = (b == 0u && k == 0u) ? 20000 : 0; }
                fx_buses(c, d, r, w, CTL);
                for (k = 0; k < CTL; k++) { if (!found && (r[k] > 3000 || r[k] < -3000)) { found = 1; pos = b * CTL + k; } }   /* (the send bus, after the pre-delay) */
            }
            first[pre] = pos;
        }
        printf("reverb send reaches the model: pre 0 -> sample %u, pre max -> sample %u (80 ms = 3528)\n", first[0], first[1]);
        CHECK(first[1] > first[0] + 3200u && first[1] < first[0] + 3900u, "PRE-DELAY max did not hold the reverb back about 80 ms");
        trk[0].p[P_REV] = 0; trk[0].p[P_LN5] = 0;
        for (tt = 0; tt < 2u; tt++) {
            double e = 0;
            fx.dly_on = 0; fx.dly_w = 0; fx.dly_start = 0; fx.dl_q = 0; fx.dly_lp = 0; memset(dly_buf, 0, sizeof dly_buf);
            trk[0].p[P_DLY] = 127; song.g[G_DMIX] = 100; song.g[G_DFDBK] = 110; song.g[G_DRREV] = 30; song.g[G_DCOLOR] = tt ? 127 : 0;
            for (b = 0; b < 700u; b++) {
                for (k = 0; k < CTL; k++) { c[k] = r[k] = 0; d[k] = (b == 0u && k == 0u) ? 20000 : 0; }
                fx_buses(c, d, r, w, CTL);
                for (k = 0; k < CTL; k++) e += (double)w[k] * w[k];
            }
            en[tt] = e;
        }
        printf("delay echo energy: tone 0 %.3g, tone 100 %.3g\n", en[0], en[1]);
        CHECK(en[1] > 0 && en[0] < en[1] * 0.5, "DELAY TONE 0 did not darken the repeats");
    }

    /* BEND: the strip's up key (F5 = BEND) raises the pitch by the range, and it returns */
    {
        double p0, p1, p2;
        fresh(0);
        trk[0].p[P_E0] = 0; trk[0].p[P_SUS] = 127; trk[0].p[P_VOICE] = V_MONO;
        na_strip_mode = 0; GI(NI_BENDRNG) = (uint8_t)(12 ^ 12);
        kdown(12); render(0.6); p0 = pitch(4000, ny);
        kdown(25); render(1.2); p1 = pitch(ny / 2, ny);
        kup(25); render(1.8); p2 = pitch(ny * 3 / 4, ny);
        printf("strip BEND up: %.1f Hz -> %.1f Hz (range 12 st: x2), released %.1f Hz\n", p0, p1, p2);
        CHECK(p1 > p0 * 1.9 && p1 < p0 * 2.1, "strip up did not bend one octave");
        CHECK(p2 > p0 * 0.94 && p2 < p0 * 1.06, "the bend did not return");
        kup(12); render(0.3);
        na_strip_mode = 1; { int16_t i0 = trk[0].p[P_LN3]; kdown(25); render(0.5); kup(25); render(0.1);
        printf("strip IMPRESS up: %d -> %d\n", i0, trk[0].p[P_LN3]); CHECK(trk[0].p[P_LN3] > i0 + 10, "strip up did not raise IMPRESS"); }
        na_strip_mode = 0;
    }
    /* GLIDE SYNC: at 120 BPM the knob at 127 is 1/1: the glide takes two seconds (TIME's is longer, RATE is the quick slide) */
    {
        uint32_t md;
        for (md = 0; md <= 2u; md++) {
            int32_t at1, at15, at25, start;
            fresh(0);
            song.g[G_BPM] = 120;
            trk[0].p[P_E0] = 0; trk[0].p[P_SUS] = 127; trk[0].p[P_VOICE] = V_MONO; trk[0].p[P_GLMODE] = (int16_t)md; trk[0].p[P_GLIDE] = 0;
            input_on(&trk[0], 72, 100); render(0.4); start = vpc(); trk[0].p[P_GLIDE] = 127;
            input_on(&trk[0], 60, 100); render(1.0); at1 = vpc(); render(0.5); at15 = vpc(); render(1.0); at25 = vpc();
                        printf("glide mode %u (0 RATE, 1 TIME, 2 SYNC): pitch %d -> after 1 s %d, 1.5 s %d, 2.5 s %d (1/16 semitones; the target is %d)\n", md, start, at1, at15, at25, start - 192);
            if (md == 1u) CHECK(at1 < start - 8 && at1 > start - 40 && at25 > start - 70, "glide TIME at full is not a ten second glide");
            if (md == 2u) CHECK(at1 < start - 40 && at1 > start - 150 && at25 == start - 192, "glide SYNC 1/1 is not a two second glide");
            input_off(&trk[0], 60); input_off(&trk[0], 72); render(0.2);
        }
    }

    /* AMP ENVELOPE: GATE ONLY keeps the note at full level with the sustain at 0, and ends it with the key */
    {
        double on_, gate_, rel;
        uint32_t g;
        for (g = 0; g < 2u; g++) {
            fresh(0);
            trk[0].p[P_E0] = 0; trk[0].p[P_SUS] = 0; trk[0].p[P_DEC] = 20; trk[0].p[P_ATK] = 0; trk[0].p[P_REL] = 20; trk[0].p[P_FM4_ATK] = (int16_t)g;
            kdown(12); render(1.2); a = rms(ny * 3 / 4, ny); kup(12); render(0.4); b = rms(ny - 4410, ny);
            if (g) gate_ = a; else on_ = a;
            if (g) rel = b;
        }
        printf("AMP ENVELOPE: sustain 0, held: ENV ON rms %.4f, GATE ONLY rms %.4f (after release %.4f)\n", on_, gate_, rel);
        CHECK(gate_ > 0.05 && on_ < gate_ * 0.3, "GATE ONLY did not hold the note at sustain 0");
        CHECK(rel < gate_ * 0.1, "GATE ONLY did not end with the key");
    }

    /* SEQ A / B: two patterns, each with its own steps and Random; the Mod Smooth toggles keep their bits */
    {
        uint32_t q, same = 1, a0;
        fresh(0);
        for (q = 0; q < 64u; q++) nano_sq_put(&trk[0].step[q], NS_REST, 0);
        for (q = 0; q < 8u; q++) nano_sq_put(&trk[0].step[q], 60 + 2u * q, 0);
        trk[0].p[P_SLEN] = 8;
        na_sq_b_reset(&trk[0]);
        a0 = trk[0].step[3].note[0];
        na_set(NI_SQPAT, 1);
        CHECK(na_get(NI_SQPAT) == 1 && trk[0].step[3].note[0] == a0, "B does not start as a copy of A");
        na_set(NI_SQRND, 40);
        for (q = 0; q < 8u; q++) same &= trk[0].step[q].note[0] == 60 + 2u * q;
        CHECK(!same && trk[0].p[P_FM2_DEC] == 40, "RANDOM did not rework B");
        na_set(NI_SQPAT, 0);
        for (q = 0; q < 8u; q++) CHECK(trk[0].step[q].note[0] == 60 + 2u * q, "A changed while B was random");
        CHECK(trk[0].p[P_FM2_DEC] == 0 && na_get(NI_SQRND) == 0, "A took B's Random position");
        na_set(NI_SQPAT, 1);
        CHECK(trk[0].p[P_FM2_DEC] == 40, "B lost its Random position");
        na_set(NI_SQPAT, 0);
        na_set(NI_SM2, 1); na_set(NI_SM4, 1);
        CHECK(na_get(NI_SM1) == 0 && na_get(NI_SM2) == 1 && na_get(NI_SM3) == 0 && na_get(NI_SM4) == 1, "MOD SMOOTH toggles");
        na_set(NI_SM2, 0);
        CHECK(na_get(NI_SM2) == 0 && na_get(NI_SM4) == 1, "MOD SMOOTH toggle off");
        printf("A/B and MOD SMOOTH ok (A %u..)\n", a0);
    }

    /* CYC ENV -> AMP (the matrix's A3): a looping cycling envelope at 100 % makes the loudness pump */
    {
        double sd[2];
        uint32_t r, w;
        for (r = 0; r < 2u; r++) {
            double m = 0, m2 = 0, x;
            fresh(0);
            trk[0].p[P_E0] = 0; trk[0].p[P_SUS] = 127; trk[0].p[P_DLY] = 0; trk[0].p[P_REV] = 0;
            na_set(NI_CMODE, 1); na_set(NI_CAMT, 100); na_set(NI_RISE, 10); na_set(NI_FALL, 10); na_set(NI_HOLD, 0);
            if (r) { na_set(NI_CDEST, 6); na_set(NI_CDEPTH, 100); }
            kdown(12); render(0.5);
            for (w = 0; w < 20u; w++) { x = rms(w * 1103u, (w + 1u) * 1103u); m += x; m2 += x * x; }
            sd[r] = sqrt(m2 / 20.0 - (m / 20.0) * (m / 20.0)) / (m / 20.0 + 1e-9);
            kup(12); render(0.2);
        }
        printf("CYC ENV -> AMP: level variation without route %.3f, with %.3f\n", sd[0], sd[1]);
        CHECK(sd[1] > sd[0] + 0.15, "CYC ENV -> AMP did not make the level pump");
        {   /* the page's DEST moves the route: A3 off, CUTOFF on */
            uint32_t k, n3 = 0, nc = 0;
            na_set(NI_CDEST, 3);
            for (k = 0; k < 4u; k++)
                if (trk[0].p[P_M1SRC + 3u * k] == MS_CYC && trk[0].p[P_M1AMT + 3u * k]) {
                    if (trk[0].p[P_M1DST + 3u * k] == (int16_t)na_mxdst(6)) n3++;
                    if (trk[0].p[P_M1DST + 3u * k] == (int16_t)na_mxdst(3)) nc++;
                }
            CHECK(n3 == 0 && nc == 1, "changing the cycling DEST did not move the route");
            na_set(NI_CDEPTH, 50);
            { uint32_t any = 0; for (k = 0; k < 4u; k++) any |= trk[0].p[P_M1SRC + 3u * k] == MS_CYC && trk[0].p[P_M1AMT + 3u * k]; if (any) { for (k = 0; k < 4u; k++) printf("  slot %u: src %d dst %d amt %d\n", k, trk[0].p[P_M1SRC + 3u * k], trk[0].p[P_M1DST + 3u * k], trk[0].p[P_M1AMT + 3u * k]); } CHECK(!any, "depth 50 % kept a route"); }
        }
    }

    /* LFO SYNC: the RATE knob picks a note value; one cycle lasts it */
    {
        uint32_t inc, cyc, idx;
        fresh(0);
        song.g[G_BPM] = 120;
        na_set(NI_LSYNC, 1);
        CHECK(na_get(NI_LSYNC) == 1, "LFO SYNC does not stick");
        for (idx = 0; idx < 13u; idx += 6) {
            static const uint32_t LKT[13] = {768, 384, 192, 96, 48, 32, 24, 16, 12, 8, 6, 4, 3};
            trk[0].p[P_LRATE] = (int16_t)(idx * 128u / 13u + 2u);
            inc = lfo_sync_inc(&trk[0]);
            cyc = beat_samples() * LKT[idx] / 24u;
            printf("LFO sync knob %d: %u samples a cycle (expected %u), phase step %u\n", trk[0].p[P_LRATE], (uint32_t)((double)4294967296.0 / inc * CTL), cyc, inc);
            CHECK(fabs((double)4294967296.0 / inc * CTL - (double)cyc) < cyc * 0.02 + CTL, "LFO SYNC cycle is not the note value");
        }
    }

    /* STRINGS page: FOLD (the Rings fold) folds the string's sound */
    {
        prof_t off, on;
        setup_bright(); trk[0].p[P_E0] = 14; off = take();
        setup_bright(); trk[0].p[P_E0] = 14; na_set(NI_FOLD2, 80); on = take();
        pshow("RINGS fold 0", off); pshow("RINGS fold 80", on);
        CHECK(na_get(NI_FOLD2) == 80 || na_get(NI_FOLD2) == 79 || na_get(NI_FOLD2) == 81, "the Strings FOLD does not stick");
        CHECK(fabs(on.B - off.B) > 0.01 || fabs(on.L - off.L) > 0.01 || fabs(on.SL - off.SL) > 0.01, "the Strings FOLD changes nothing");
    }
    /* FILTER MODEL: SVF / LADDER on FILTER 2's fourth knob */
    {
        prof_t svf, lad, lo, hi; uint32_t k, bad = 0;
        setup_bright(); goto_page(B_FX, 1); kn(3, -3); svf = take();
        CHECK(trk[0].p[P_FM4_REL] == 0, "MODEL does not start on SVF");
        setup_bright(); goto_page(B_FX, 1); kn(3, 3); lad = take();
        pshow("MODEL SVF", svf); pshow("MODEL LADDER", lad);
        CHECK(trk[0].p[P_FM4_REL] == 1, "MODEL does not select the ladder");
        CHECK(fabs(lad.B - svf.B) > 0.03 * svf.B || fabs(lad.L - svf.L) > 0.03 * svf.L, "MODEL changes nothing");
        CHECK(lad.L > 0.01, "the ladder is silent");
        setup_bright(); goto_page(B_FX, 1); kn(3, 3); goto_page(B_FX, 0); kn(0, -127); lo = take();
        setup_bright(); goto_page(B_FX, 1); kn(3, 3); goto_page(B_FX, 0); kn(0, 127); hi = take();
        pshow("LADDER CUTOFF 0", lo); pshow("LADDER CUTOFF 100", hi);
        CHECK(hi.B > lo.B * 1.5 && hi.L > lo.L, "the ladder's CUTOFF does not open it");
        setup_bright(); goto_page(B_FX, 1); kn(3, 3); goto_page(B_FX, 0); kn(0, 40 - 127 + 127); kn(1, -127); lo = take();
        setup_bright(); goto_page(B_FX, 1); kn(3, 3); goto_page(B_FX, 0); kn(1, 127); hi = take();
        pshow("LADDER RESO 0", lo); pshow("LADDER RESO 100", hi);
        CHECK(fabs(hi.B - lo.B) > 0.1 * lo.B || fabs(hi.L - lo.L) > 0.1 * lo.L, "the ladder's RESO changes nothing");
        for (k = 0; k < 3; k++) {                                 /* LP, BP, HP: sound, finite, no runaway at full resonance */
            prof_t q;
            setup_bright(); goto_page(B_FX, 1); kn(3, 3); goto_page(B_FX, 0); kn(1, 127); kn(2, -3 + (int)k * 0); 
            trk[0].p[P_FTYPE] = (int16_t)k;
            q = take(); printf("LADDER type %u level %.3f\n", k, q.L);
            if (!(q.L > 0.002 && q.L < 1.5)) bad++;
        }
        CHECK(!bad, "a ladder type is silent or runs away");
    }
    return fails ? 1 : 0;
}
