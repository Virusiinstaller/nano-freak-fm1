/* SPDX-License-Identifier: GPL-3.0-only
 * The Nano Freak screen and controls (nano_app.c): draws every page with the real painter to PPMs (OUT/na_<n>.ppm), and checks
 * that SELECT / ALGORITHM / PRESETS / KNOB 1..4 reach the parameters the test bed's pages are for. */
#define FELUCCA_FMINI_ONLY 1
#include "nano_host.h"
#include "../firmware/src/nano_app.c"
#include <stdio.h>
#include "nano_seq_table.h"
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
static uint32_t stephash(void) { uint32_t h = na_cur, k; const uint8_t *b = (const uint8_t *)trk[0].step; for (k = 0; k < sizeof trk[0].step; k++) h = h * 31u + b[k]; for (k = 0; k < NA_NG; k++) h = h * 31u + na_g[k]; return h; }
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
        static const uint8_t BTN[8] = {B_FX, B_ENV, B_LFO, B_SCL, B_ARP, B_EDIT, B_SEQ};
        for (g = 0; g < 7; g++) {
            npress(BTN[g]);
            for (sb = 0; sb < 8; sb++) {
                uint32_t k;
                if (na_grp == NG_MATRIX) {                   /* (the cycling page's DEST and DEPTH, turned before, made a route: the four slots are free here) */
                    for (k = 0; k < 4u; k++) TSEL->p[P_M1SRC + 3u * k] = TSEL->p[P_M1DST + 3u * k] = TSEL->p[P_M1AMT + 3u * k] = 0;
                }
                draw();
                dump(dir, n++);
                for (k = 0; k < 4u; k++) {
                    const nsub_t *sp = &NGR[na_grp].sub[na_cursub()];
                    int32_t id = sp->k[k];
                    uint32_t before = 0, after = 0;
                    if (id == NI_NONE || id == NI_MXON || id == NI_SQPAT) continue;       /* (MXON: a route is needed, tested below) */
                    before = stephash() + na_pat + 100u * na_mxc + 1000u * na_mxr + 10000u * na_as[0] + 100000u * na_as[1] + 1000000u * na_as[2] + 10000000u * na_as[3];
                    for (i = 0; i < P_COUNT; i++) before = before * 31u + (uint32_t)TSEL->p[i];
                    for (i = 0; i < G_COUNT; i++) before = before * 31u + (uint32_t)song.g[i];
                    nturn(EN_K1 + k, 3);
                    after = stephash() + na_pat + 100u * na_mxc + 1000u * na_mxr + 10000u * na_as[0] + 100000u * na_as[1] + 1000000u * na_as[2] + 10000000u * na_as[3];
                    for (i = 0; i < P_COUNT; i++) after = after * 31u + (uint32_t)TSEL->p[i];
                    for (i = 0; i < G_COUNT; i++) after = after * 31u + (uint32_t)song.g[i];
                    if (before == after) {                       /* (at its top: the other way) */
                        nturn(EN_K1 + k, -3);
                        after = stephash() + na_pat + 100u * na_mxc + 1000u * na_mxr + 10000u * na_as[0] + 100000u * na_as[1] + 1000000u * na_as[2] + 10000000u * na_as[3];
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
    /* SEQ: Random and Mutate give the test bed's patterns */
    {
        track_t *t = &trk[0];
        uint32_t c, k, j;
        int bad = 0;
        for (c = 0; c < sizeof NANO_SQ_REF / sizeof NANO_SQ_REF[0]; c++) {
            const nano_sq_ref_t *r = &NANO_SQ_REF[c];
            for (k = 0; k < 64u; k++) nano_sq_put(&t->step[k], NS_REST, 0);
            t->p[P_SLEN] = (int16_t)r->len; t->p[P_FM2_REL] = (int16_t)r->scale; t->p[P_FM2_LEVEL] = (int16_t)(r->oct - 1); t->p[P_FM3_ATK] = (int16_t)r->rest;
            t->p[P_FM2_DEC] = t->p[P_FM2_SUS] = 0;
            nano_sq_random(t, (uint32_t)r->seed, (uint32_t)r->root);
            for (k = 0; k < (uint32_t)r->len; k++)
                if (nano_sq_note(&t->step[k]) != r->n[k] || (t->step[k].acc & 3u) != r->d[k]) { bad++; if (bad < 4) printf("random case %u step %u: %u/%u, test bed %u/%u\n", (unsigned)c, (unsigned)k, (unsigned)nano_sq_note(&t->step[k]), (unsigned)(t->step[k].acc & 3u), r->n[k], r->d[k]); }
            for (j = 0; j < 5u; j++) {
                t->p[P_FM2_REL] = (int16_t)r->m[j].scale;
                nano_sq_mutate(t, (uint32_t)r->m[j].seed, (uint32_t)r->root);
                for (k = 0; k < 64u; k++)
                    if (nano_sq_note(&t->step[k]) != r->m[j].n[k] || (t->step[k].acc & 3u) != r->m[j].d[k]) { bad++; if (bad < 4) printf("mutate case %u/%u step %u: %u/%u, test bed %u/%u\n", (unsigned)c, (unsigned)j, (unsigned)k, (unsigned)nano_sq_note(&t->step[k]), (unsigned)(t->step[k].acc & 3u), r->m[j].n[k], r->m[j].d[k]); }
                nano_sq_mutate(t, 0, (uint32_t)r->root);                      /* the base back */
            }
            nano_sq_random(t, 0, (uint32_t)r->root);                           /* and Random off: the empty pattern back */
            for (k = 0; k < 64u; k++)
                if (!nano_sq_rest(&t->step[k])) { bad++; if (bad < 4) printf("random off case %u step %u not back\n", (unsigned)c, (unsigned)k); }
        }
        CHECK(!bad, "Random / Mutate differ from the test bed in %d places", bad);
        /* step lengths: a quaver is two semiquavers of the clock; the steps before it move the swing's off-beat */
        for (k = 0; k < 64u; k++) nano_sq_put(&t->step[k], 60, 0);
        t->p[P_SSWING] = 0; song.g[G_SWING] = 0;
        nano_sq_put(&t->step[1], 62, 1);
        CHECK(nano_sq_len(t, 1000, 1) == 2000 && nano_sq_len(t, 1000, 0) == 1000 && nano_sq_ticks(&t->step[1]) == 2, "a quaver step lasts two semiquavers");
        nano_sq_put(&t->step[1], 62, 3);
        CHECK(nano_sq_len(t, 1000, 1) == 8000 && nano_sq_pos(t, 2) == 9, "a minim step lasts eight");
        t->p[P_SLEN] = 16;
    }
    /* SEQ keys: REC armed, stopped, the keys go into the steps from the cursor; the root of Random is the last key */
    {
        track_t *t = &trk[0];
        npress(B_HOME);
        npress(B_SEQ);
        CHECK(na_seqmode == 1 && na_grp == NG_SEQ && !na_arp_on(), "SEQ is a group and a mode, the arp is off");
        song.playing = 0; na_cur = 0; t->p[P_SLEN] = 16; t->p[P_FM3_DEC] = 1;
        npress(B_REC);
        CHECK(na_rec == 1, "REC arms");
        fa_seq_note(t, 64); fa_seq_note(t, 67);
        CHECK(t->step[0].note[0] == 64 && t->step[1].note[0] == 67 && na_cur == 2 && (t->step[0].acc & 3) == 1, "two keys, two steps of the REC LENGTH (%d %d)", t->step[0].note[0], t->step[1].note[0]);
        CHECK(na_root == 67, "the last key is Random's root");
        npress(B_REC);
        fa_seq_note(t, 70);
        CHECK(t->step[2].note[0] != 70 || na_cur != 3, "REC off: a key records nothing");
        npress(B_PLAY);
        CHECK(transport_req == 1, "PLAY starts the sequencer in SEQ");
        transport_req = 0;
        npress(B_ARP);
        npress(B_HOME);
    }
    /* GLOBAL: every page draws; the settings reach the firmware; START-UP and the BLE page are listed */
    {
        uint32_t pg;
        npress(B_HOME);
        npress(B_GLO);
        CHECK(na_grp == NG_GLOBAL, "GLO opens GLOBAL");
        for (pg = 0; pg < 6u; pg++) {
            na_sub[NG_GLOBAL] = (uint8_t)pg;
            draw();
            dump(dir, n++);
        }
        na_sub[NG_GLOBAL] = 1;                             /* MIDI OUT */
        nturn(EN_K1, 1);
        CHECK(fm_seq_out == 2, "SEQ/ARP OUT: MIDI (%d)", fm_seq_out);
        nturn(EN_K1, -1);
        CHECK(fm_seq_out == 1, "SEQ/ARP OUT: BOTH");
        nturn(EN_K2, 1); nturn(EN_K2, 1); nturn(EN_K2, 1);
        CHECK(GI(NI_MCH) == 3 && trk_midi_ch(0) == 3 && song.g[G_ROUTE] == ROUTE_TRACK, "OUT CH 4 is track 1's channel (%d)", (int)trk_midi_ch(0));
        nturn(EN_K3, -1);
        CHECK(fm_out_mask == 2, "OUT PORT USB is the mask 2 (%d)", fm_out_mask);
        nturn(EN_K4, -1);
        CHECK(!fm_key_out, "KEY OUT off");
        nturn(EN_K4, 1);
        na_sub[NG_GLOBAL] = 2;                             /* ROUTING */
        nturn(EN_K1, -1);
        CHECK(!fm_local, "LOCAL off");
        nturn(EN_K1, 1);
        nturn(EN_K2, 1);
        CHECK(fm_clk_out, "CLOCK OUT on");
        nturn(EN_K3, 1);
        CHECK(fm_thru_in == 1, "THRU IN USB is the source 1 (%d)", fm_thru_in);
        for (i = 0; i < 5u; i++) nturn(EN_K4, 1);
        CHECK(fm_thru_out == 5, "THRU OUT %d", fm_thru_out);
        for (i = 0; i < 9u; i++) nturn(EN_K4, -1);
        na_sub[NG_GLOBAL] = 3;                             /* MIDI IN */
        for (i = 0; i < 4u; i++) nturn(EN_K1, 1);
        CHECK(fm_midi_ch == 5, "IN CH 5 (%d)", fm_midi_ch);
        nturn(EN_K2, 1);
        CHECK(song.g[G_CLOCK] == 1, "CLOCK: MIDI");
        nturn(EN_K2, -1);
        CHECK(song.g[G_CLOCK] == 0, "CLOCK: internal");
        for (i = 0; i < 9u; i++) nturn(EN_K1, -1);
        /* thru sends what came in on the source to the ports, never a clock; CLOCK OUT sends 24 a beat */
        {
            uint32_t q0 = mo_w, k;
            fm_thru_out = 2; fm_thru_in = 1;
            usb.config = 1;
            nano_thru(0x09u | 0x90u << 8 | 60u << 16 | 100u << 24, 1);
            nano_thru(0x09u | 0x90u << 8 | 60u << 16 | 100u << 24, 2);
            nano_thru(0x0Fu | 0xF8u << 8, 1);
            CHECK(mo_w - q0 == 1, "thru: one note from the source, none from another, no clock (%u)", (unsigned)(mo_w - q0));
            fm_thru_out = 0;
            q0 = mo_w; fm_out_mask = 2; fm_clk_out = 1; song.g[G_CLOCK] = 0; song.playing = 1; song.g[G_BPM] = 120;
            for (k = 0; k < 22050u / 64u; k++) nano_clock_out(64);          /* half a second: a beat at 120 */
            CHECK(mo_w - q0 >= 24u && mo_w - q0 <= 26u, "CLOCK OUT: a START and 24 clocks in a beat (%u)", (unsigned)(mo_w - q0));
            song.playing = 0; nano_clock_out(64);
            fm_clk_out = 0; fm_out_mask = 7;
        }
        /* START-UP rows, BLE rows */
        na_sub[NG_GLOBAL] = 5;
        for (i = 0; i < 3u; i++) nturn(EN_K1, 1);
        CHECK(GI(NI_SUPATCH) == 4 && favorites.factory[6][0] == 0xB6 && favorites.factory[6][2 + 15] == 4, "POWER-ON is saved in the settings row");
        for (i = 0; i < 9u; i++) nturn(EN_K1, -1);
        nturn(EN_K1, 1);
        draw();
        dump(dir, n++);
        npress(B_HOME);
    }
    /* PRESETS: the bank, SAVE into the user bank, and back */
    {
        track_t *t = TSEL;
        uint32_t k;
        for (k = 0; k < UP_SLOTS; k++) up_put(k, 0);
        na_load(5);
        npress(B_HOME);
        nturn(EN_PRESET, 1);
        CHECK(na_grp == NG_PRESET && na_bank == 1 && na_slot == 6, "the preset knob opens the list and steps the factory bank");
        npress(B_HOME);
        npress(B_SAVE);
        CHECK(na_grp == NG_PRESET && na_cursub() == 1 && !na_conf, "SAVE opens the save page");
        CHECK(na_sslot == 0, "the first free slot");
        nturn(EN_K2, 0);
        na_scur = 0;
        nturn(EN_K3, 1);                                   /* letter: ' ' -> the next of the list */
        draw();
        dump(dir, n++);
        t->p[P_E1] = 77;
        npress(B_SAVE);
        CHECK(na_conf == 1, "the first SAVE asks");
        draw();
        dump(dir, n++);
        song.playing = 0;
        npress(B_SAVE);
        CHECK(up_used(0) && na_bank == 0 && na_slot == 0, "the second SAVE stores the sound in slot 1");
        { char nm[16]; up_name(0, nm); CHECK(nm[0] != 0, "named (%s)", nm); }
        t->p[P_E1] = 10;
        na_pre_go(0);
        CHECK(t->p[P_E1] == 77, "the user preset loads back (%d)", t->p[P_E1]);
        na_bank = 0;
        na_load_user(3);
        CHECK(na_slot == 3 && t->p[P_E1] == 77, "an empty slot changes nothing");
        na_bank = 1;
        npress(B_SAVE);
        CHECK(na_sslot == 1, "SAVE again: the next free slot (%d)", na_sslot);
        npress(B_HOME);
        na_grp = NG_PRESET; na_sub[NG_PRESET] = 0;
        draw();
        dump(dir, n++);
        npress(B_HOME);
        na_bank = 1;
        na_load(0);
        GI(NI_SUPATCH) = 1;
    }
    /* MATRIX: a route is a slot of the firmware's matrix */
    {
        track_t *t = TSEL;
        uint32_t k;
        for (k = 0; k < 4u; k++) t->p[P_M1SRC + 3u * k] = t->p[P_M1DST + 3u * k] = t->p[P_M1AMT + 3u * k] = 0;
        npress(B_EDIT);
        na_sub[NG_MATRIX] = 0;
        na_as[0] = 2; na_as[1] = 4; na_as[2] = 5; na_as[3] = 0;
        na_set(NI_MXROW, 2); na_set(NI_MXCOL, 3);                  /* LFO > CUTOFF */
        na_set(NI_MXAMT, 50);
        CHECK(t->p[P_M1SRC] == MS_LFO && t->p[P_M1DST] == MD_CUT && t->p[P_M1AMT] == 32, "LFO > CUTOFF 50%% is slot 1 (src %d dst %d amt %d)", t->p[P_M1SRC], t->p[P_M1DST], t->p[P_M1AMT]);
        CHECK(na_get(NI_MXAMT) == 50, "the amount reads back");
        na_set(NI_MXON, 0);
        CHECK(t->p[P_M1AMT] == 0 && na_get(NI_MXAMT) == 50, "off: the slot is silent, the amount is kept");
        na_set(NI_MXON, 1);
        CHECK(t->p[P_M1AMT] == 32, "on again: 50%%");
        na_set(NI_MXROW, 1); na_set(NI_MXCOL, 0); na_set(NI_MXAMT, -30);
        CHECK(t->p[P_M1SRC + 3] == MS_ENV && t->p[P_M1DST + 3] == MD_PITCH && t->p[P_M1AMT + 3] == -19, "ENV > PITCH -30%% is slot 2");
        na_set(NI_MXROW, 0); na_set(NI_MXCOL, 1); na_set(NI_MXAMT, 20);
        na_set(NI_MXROW, 3); na_set(NI_MXCOL, 2); na_set(NI_MXAMT, 20);
        na_set(NI_MXROW, 2); na_set(NI_MXCOL, 1); na_set(NI_MXAMT, 40);
        CHECK(na_get(NI_MXAMT) == 0, "the fifth route is refused");
        na_set(NI_MXROW, 3); na_set(NI_MXCOL, 2); na_set(NI_MXAMT, 0);
        CHECK(t->p[P_M1SRC + 9] == 0, "an amount of 0 frees the slot");
        na_set(NI_MXROW, 2); na_set(NI_MXCOL, 4); na_set(NI_MXAMT, 25);   /* LFO > ASSIGN 1 (RESONANCE) */
        CHECK(t->p[P_M1DST + 9] == MD_E1 + 6, "ASSIGN 1 is RESONANCE by default");
        na_set(NI_AS1, 4);                                                 /* LFO RATE */
        CHECK(t->p[P_M1DST + 9] == MD_RATE && na_get(NI_MXAMT) == 25, "ASSIGN 1 changed: the route follows");
        /* the LFO group's page 2: the same four ASSIGN columns on K1..K4, named on the page */
        {
            int32_t before = na_get(NI_AS2);
            npress(B_HOME); npress(B_LFO);
            CHECK(na_cursub() == 0u && nf_ui_get(UI_NSUB) == 2, "LFO has two pages (%d)", nf_ui_get(UI_NSUB));
            nturn(EN_SELECT, 1);
            CHECK(na_grp == NG_LFO && na_cursub() == 1u, "SELECT opens the LFO's page 2");
            nturn(EN_K2, 1);
            CHECK(na_get(NI_AS2) != before, "K2 on the LFO's page 2 changes ASSIGN 2 (%d -> %d)", before, na_get(NI_AS2));
            na_sync();
            CHECK(nf_ui_str(UIS_V1)[0] != 0 && nf_ui_str(UIS_L1)[0] == 'A', "the page names A2 and its destination ('%s' '%s')", nf_ui_str(UIS_L1), nf_ui_str(UIS_V1));
            draw();
            dump(dir, n++);
            nturn(EN_SELECT, -1);
            CHECK(na_cursub() == 0u, "SELECT back to the LFO page");
        }
        nturn(EN_SELECT, 1);
        draw();
        dump(dir, n++);
        na_sub[NG_MATRIX] = 0;
        draw();
        dump(dir, n++);
        npress(B_HOME);
    }
    /* the oscillator list: ALGORITHM steps through the 15 types and back */
    {
        uint32_t seen = 0;
        for (i = 0; i < 15u; i++) {
            nturn(EN_ALGO, 1);
            seen |= 1u << TSEL->p[P_E0];
            draw();
            dump(dir, 100 + (int)i);
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
    /* the arpeggiator LENGTH: every one of its 16 values reads back as set (an odd one used to fall to the one below, so 1 could not be left) */
    {
        int32_t lv;
        for (lv = 0; lv < 16; lv++) { na_set(NI_ARPLEN, lv); CHECK(na_get(NI_ARPLEN) == lv, "ARP LENGTH %d reads back %d", lv + 1, na_get(NI_ARPLEN) + 1); }
    }
    /* VOICE pages are the track's voice mode */
    {
        npress(B_SCL);
        while (na_cursub() != 0u) nturn(EN_SELECT, 1);
        CHECK(TSEL->p[P_VOICE] == V_MONO, "VOICE page 1: MONO");
        nturn(EN_SELECT, 1);
        CHECK(TSEL->p[P_VOICE] == V_UNISON, "page 2: UNISON");
        nturn(EN_SELECT, 1);
        CHECK(TSEL->p[P_VOICE] == V_PARA, "page 3: PARAPHONIC");
        na_sync();
        CHECK(nf_ui_get(UI_POLY) == 2, "the footer shows PARA (%d)", nf_ui_get(UI_POLY));
        nturn(EN_SELECT, 1);
        CHECK(TSEL->p[P_VOICE] == V_POLY, "page 4: POLY");
        npress(B_HOME);
    }
    printf("%s (%d pictures)\n", fails ? "nano_app_test FAIL" : "nano_app_test ok", n);
    /* the footer's strip indicator: the mode F5 picked and its amount */
    {
        na_strip_mode = 0; nb_v = 32768 / 2; draw();
        CHECK(nf_ui_get(UI_STRIP) == 1 && nf_ui_get(UI_STRIPV) == (GI(NI_BENDRNG) ^ 12) * 5, "BEND is not shown with half of the range (%d)", nf_ui_get(UI_STRIPV));
        nb_v = -32768; draw();
        CHECK(nf_ui_get(UI_STRIPV) == -(GI(NI_BENDRNG) ^ 12) * 10, "a full down bend is not minus the range (%d)", nf_ui_get(UI_STRIPV));
        na_strip_mode = 1; TSEL->p[P_LN3] = 127; draw();
        CHECK(nf_ui_get(UI_STRIP) == 2 && nf_ui_get(UI_STRIPV) == 100, "IMPRESS is not shown as 100 %% (%d)", nf_ui_get(UI_STRIPV));
        na_strip_mode = 2; TSEL->p[P_LN4] = 0; draw();
        CHECK(nf_ui_get(UI_STRIP) == 3 && nf_ui_get(UI_STRIPV) == 0, "CHANCE is not shown as 0 %% (%d)", nf_ui_get(UI_STRIPV));
        na_strip_mode = 0; nb_v = 0;
    }
    return fails ? 1 : 0;
}
