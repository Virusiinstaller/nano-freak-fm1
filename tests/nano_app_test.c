/* SPDX-License-Identifier: GPL-3.0-only
 * The Nano Freak screen and controls (nano_app.c): draws every page with the real painter to PPMs (OUT/na_<n>.ppm), and checks
 * that SELECT / ALGORITHM / PRESETS / KNOB 1..4 reach the parameters the test bed's pages are for. */
#define UI_TEST_NO_MAIN 1
#define FELUCCA_FMINI_ONLY 1
#define FELUCCA_NANO_APP 1
#include "ui_test.c"
#include "../firmware/src/fmini_rec.c"
#include "../firmware/src/fmini_bank.c"
#include "../firmware/src/nano_app.c"
#include <stdio.h>
static void dump(const char *dir, int n)
{
    char p[256];
    uint32_t i;
    FILE *f;
    snprintf(p, sizeof p, "%s/na_%d.ppm", dir, n);
    f = fopen(p, "wb");
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);
        uint8_t b[3] = {(uint8_t)((c >> 11) * 255u / 31u), (uint8_t)(((c >> 5) & 63u) * 255u / 63u), (uint8_t)((c & 31u) * 255u / 31u)};
        fwrite(b, 1, 3, f);
    }
    fclose(f);
}
static void nframe(void) { host_ticks += 16000u; fm1_ms += 16u; na_input(); }
static void npress(uint32_t label)
{
    fm1_in.buttons |= 1u << panel.btn[label];
    host_pressed |= 1u << panel.btn[label];
    nframe();
    fm1_in.buttons &= ~(1u << panel.btn[label]);
    nframe();
}
static void nturn(uint32_t role, int32_t d)
{
    host_enc[panel.enc[role]] += d * panel.dir[role];
    nframe();
    host_ticks += 200000u;
}
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
static void draw(void) { na_dirty = 1; ui.force = 1; na_draw(); }
int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    int n = 0, g, sb;
    uint32_t i;
    { uint32_t gi; for (gi = 0; gi < G_COUNT; gi++) song.g[gi] = GP[gi].def; }
    panel_init();
    na_init();
    CHECK(TSEL->eng_req == ENGI_NANO, "the instrument is NANO at power-on");
    CHECK(trk[1].p[P_MUTE] == 1 && trk[0].p[P_MUTE] == 0, "track 1 plays, the rest is muted");
    na_splash();
    dump(dir, n++);
    /* every page of every group draws, with its four knobs moving something */
    {
        static const uint8_t BTN[8] = {B_FX, B_ENV, B_LFO, B_SCL, B_ARP};
        for (g = 0; g < 5; g++) {
            npress(BTN[g]);
            for (sb = 0; sb < 8; sb++) {
                uint32_t k;
                draw();
                dump(dir, n++);
                for (k = 0; k < 4u; k++) {
                    const nsub_t *sp = &NGR[na_grp].sub[na_cursub()];
                    int32_t id = sp->k[k];
                    uint32_t before = 0, after = 0;
                    if (id == NI_NONE) continue;
                    before = na_pat;
                    for (i = 0; i < P_COUNT; i++) before = before * 31u + (uint32_t)TSEL->p[i];
                    for (i = 0; i < G_COUNT; i++) before = before * 31u + (uint32_t)song.g[i];
                    nturn(EN_K1 + k, 3);
                    after = na_pat;
                    for (i = 0; i < P_COUNT; i++) after = after * 31u + (uint32_t)TSEL->p[i];
                    for (i = 0; i < G_COUNT; i++) after = after * 31u + (uint32_t)song.g[i];
                    if (before == after) {                       /* (at its top: the other way) */
                        nturn(EN_K1 + k, -3);
                        after = na_pat;
                        for (i = 0; i < P_COUNT; i++) after = after * 31u + (uint32_t)TSEL->p[i];
                        for (i = 0; i < G_COUNT; i++) after = after * 31u + (uint32_t)song.g[i];
                    }
                    CHECK(before != after, "group %d page %d knob %d (control %d) does not move", (int)na_grp, (int)na_cursub(), (int)k, (int)id);
                }
                nturn(EN_SELECT, 1);
            }
            npress(B_HOME);
        }
    }
    /* the oscillator list: ALGORITHM steps through the 15 types and back */
    {
        uint32_t seen = 0;
        for (i = 0; i < 15u; i++) {
            nturn(EN_ALGO, 1);
            seen |= 1u << TSEL->p[P_E0];
        }
        CHECK(seen == 0x7FFFu, "ALGORITHM reaches all 15 NANO types (%x)", (unsigned)seen);
        CHECK(na_type() == 0, "15 steps come round");
    }
    /* the preset list */
    {
        nturn(EN_PRESET, 1);
        CHECK(na_grp == NG_PRESET && na_slot == 1, "PRESETS opens the list and loads the next");
        draw();
        dump(dir, n++);
        for (i = 0; i < na_npre(); i++) nturn(EN_PRESET, 1);
        CHECK(na_slot == 1, "the list comes round");
        npress(B_HOME);
        CHECK(na_grp == NG_OSC, "HOME is OSC");
    }
    /* the oscillator page, the three labels and Rings' second page */
    {
        nturn(EN_ALGO, 1);
        draw();
        CHECK(na_nsub() == 1, "one OSC page off Rings");
        while (TSEL->p[P_E0] != (int)NA_RINGS) nturn(EN_ALGO, 1);
        CHECK(na_nsub() == 2, "two OSC pages on Rings");
        draw();
        dump(dir, n++);
    }
    /* ARP: PLAY toggles it; the pattern is the track's mode */
    {
        npress(B_HOME);
        npress(B_ARP);
        na_sub[NG_ARP] = 0;
        na_pat = 0;
        CHECK(!na_arp_on(), "arp off");
        npress(B_PLAY);
        CHECK(na_arp_on() && TSEL->p[P_AMODE] == NANO_APAT[na_pat], "PLAY turns the arp on with the pattern");
        nturn(EN_K1 + 2, 1);                       /* (page 1: K3 is the pattern) */
        CHECK(TSEL->p[P_AMODE] == NANO_APAT[na_pat] && na_pat == 1, "a pattern turn changes the mode while it plays");
        npress(B_PLAY);
        CHECK(!na_arp_on(), "PLAY again: off");
        CHECK(na_pat == 1, "the pattern is kept while it is off");
    }
    /* VOICE pages are the track's voice mode */
    {
        npress(B_SCL);
        while (na_cursub() != 0u) nturn(EN_SELECT, 1);
        CHECK(TSEL->p[P_VOICE] == V_MONO, "VOICE page 1: MONO");
        nturn(EN_SELECT, 1);
        CHECK(TSEL->p[P_VOICE] == V_UNISON, "page 2: UNISON");
        nturn(EN_SELECT, 2);
        CHECK(TSEL->p[P_VOICE] == V_POLY, "page 4: POLY");
        npress(B_HOME);
    }
    printf("%s (%d pictures)\n", fails ? "nano_app_test FAIL" : "nano_app_test ok", n);
    return fails ? 1 : 0;
}
