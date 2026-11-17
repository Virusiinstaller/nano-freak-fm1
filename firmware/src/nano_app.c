/* SPDX-License-Identifier: GPL-3.0-only */
/* Nano Freak: the whole user interface of the product firmware (FELUCCA_FMINI_ONLY=1, FELUCCA_NANO_APP=1). It is the test bed's
 * screen and controls (github.com/Virusiinstaller/nano-freak: dsp/nf_ui.c is the painter, index.html the pages), on the FM-1's
 * panel: the group buttons FX ENV LFO SCL (VOICE) ARP SEQ EDIT (MATRIX) GLO SAVE, HOME for OSC, SELECT steps through the pages of a
 * group, KNOB 1..4 edit its four controls, ALGORITHM the oscillator type, PRESETS the preset list, OCT- / OCT+ the keys' octave,
 * PLAY the arpeggiator. main.c calls na_input / na_leds / na_draw / na_splash / na_init (as fa_* of fmini_app.c).
 *
 * The controls are a table (NI_*: a label, how many steps or 0..100, the track parameter they are): the painter reads the
 * values through nf_ui_set / nf_ui_str, as the test bed's page does, one band of 120 rows at a time.
 * Included by felucca.c before main.c. */
#if FELUCCA_FMINI_ONLY && FELUCCA_NANO_APP
#include "nano_gfx.c"
#include "nano_ui.c"

enum { NG_OSC, NG_ENV, NG_FX, NG_LFO, NG_MATRIX, NG_ARP, NG_SEQ, NG_VOICE, NG_GLOBAL, NG_PRESET, NG_COUNT };   /* = the painter's UI_GROUP */

/* the controls */
enum {
    NI_NONE = -1,
    NI_WAVE, NI_TIMBRE, NI_SHAPE, NI_FOLD, NI_POS,
    NI_CUTOFF, NI_RESO, NI_FTYPE, NI_FAMT, NI_FSLOPE, NI_FDRIVE, NI_FCLIP,
    NI_RVBMIX, NI_RVBSIZE, NI_RVBDAMP, NI_DLYMIX, NI_DLYTIME, NI_DLYFB,
    NI_GLIDE, NI_GLMODE, NI_IMPRESS, NI_CHANCE,
    NI_ATTACK, NI_DECAY, NI_SUSTAIN, NI_RISE, NI_FALL, NI_HOLD, NI_CAMT, NI_CMODE,
    NI_LSHAPE, NI_LRATE, NI_LMORPH, NI_UDETUNE,
    NI_ARPRATE, NI_ARPRANGE, NI_ARPPAT, NI_ARPLEN, NI_SWING, NI_ARPLATCH, NI_ARPTYPE, NI_ARPCHORD,
    NI_PBANK, NI_PSLOT,
    NI_COUNT
};
static const char *const NT_FTYPE[] = {"LPF", "BPF", "HPF"};
static const char *const NT_SLOPE[] = {"12 DB", "24 DB"};
static const char *const NT_GLMODE[] = {"TIME", "RATE"};
static const char *const NT_LSHAPE[] = {"SINE", "TRI", "SAW", "SQUARE", "S&H", "GLIDE"};
static const char *const NT_CMODE[] = {"ENV", "LOOP", "GATE"};
static const char *const NT_PAT[] = {"UP", "DOWN", "UP/DOWN", "ORDER", "RANDOM", "PATTERN", "AS PLAYED", "CONVERGE", "DIVERGE", "HOLLOW", "JEOPARDY", "CHORD", "BROKEN", "ALTERNATING"};
static const char *const NT_CHORD[] = {"OFF", "MAJ", "MIN", "7", "MIN7", "MAJ7", "SUS4", "DIM7", "POWER"};
static const char *const NT_ONOFF[] = {"OFF", "ON"};
static const char *const NT_TYPE[] = {"MONO", "CHORD"};
static const char *const NT_BANK[] = {"FACTORY"};
typedef struct { const char *lb; uint8_t n; const char *const *tx; } nk_t;   /* n 0: continuous 0..100 */
static const nk_t NK[NI_COUNT] = {
    [NI_WAVE] = {"WAVE", 0, 0}, [NI_TIMBRE] = {"TIMBRE", 0, 0}, [NI_SHAPE] = {"SHAPE", 0, 0}, [NI_FOLD] = {"FOLD", 0, 0}, [NI_POS] = {"POSITION", 0, 0},
    [NI_CUTOFF] = {"CUTOFF", 0, 0}, [NI_RESO] = {"RESO", 0, 0}, [NI_FTYPE] = {"TYPE", 3, NT_FTYPE}, [NI_FAMT] = {"FILT AMT", 0, 0},
    [NI_FSLOPE] = {"SLOPE", 2, NT_SLOPE}, [NI_FDRIVE] = {"DRIVE", 0, 0}, [NI_FCLIP] = {"CLIP", 0, 0},
    [NI_RVBMIX] = {"MIX", 0, 0}, [NI_RVBSIZE] = {"SIZE", 0, 0}, [NI_RVBDAMP] = {"DAMP", 0, 0},
    [NI_DLYMIX] = {"MIX", 0, 0}, [NI_DLYTIME] = {"TIME", 0, 0}, [NI_DLYFB] = {"FEEDBACK", 0, 0},
    [NI_GLIDE] = {"GLIDE", 0, 0}, [NI_GLMODE] = {"MODE", 2, NT_GLMODE}, [NI_IMPRESS] = {"IMPRESS", 0, 0}, [NI_CHANCE] = {"CHANCE", 0, 0},
    [NI_ATTACK] = {"ATTACK", 0, 0}, [NI_DECAY] = {"DECAY", 0, 0}, [NI_SUSTAIN] = {"SUSTAIN", 0, 0},
    [NI_RISE] = {"RISE", 0, 0}, [NI_FALL] = {"FALL", 0, 0}, [NI_HOLD] = {"HOLD", 0, 0}, [NI_CAMT] = {"AMOUNT", 0, 0}, [NI_CMODE] = {"MODE", 3, NT_CMODE},
    [NI_LSHAPE] = {"SHAPE", 6, NT_LSHAPE}, [NI_LRATE] = {"RATE", 0, 0}, [NI_LMORPH] = {"MORPH", 0, 0}, [NI_UDETUNE] = {"DETUNE", 0, 0},
    [NI_ARPRATE] = {"RATE", 0, 0}, [NI_ARPRANGE] = {"RANGE", 4, 0}, [NI_ARPPAT] = {"PATTERN", 14, NT_PAT}, [NI_ARPLEN] = {"LENGTH", 16, 0},
    [NI_SWING] = {"SWING", 0, 0}, [NI_ARPLATCH] = {"LATCH", 2, NT_ONOFF}, [NI_ARPTYPE] = {"TYPE", 2, NT_TYPE}, [NI_ARPCHORD] = {"CHORD", 9, NT_CHORD},
    [NI_PBANK] = {"BANK", 1, NT_BANK}, [NI_PSLOT] = {"PRESET", 0, 0},
};

/* the oscillator list as the page has it: name, the labels of KNOB 1..3, the NANO engine's TYPE (P_E0) */
typedef struct { const char *name, *l1, *l2, *l3; uint8_t ty; } nosc_t;
static const nosc_t NOSC[15] = {
    {"BASICWAVES", "MORPH", "SYM", "SUB", 0}, {"SUPERWAVE", "WAVE", "DETUNE", "VOLUME", 7}, {"WAVETABLE", "TABLE", "POSITION", "CHORUS", 12},
    {"HARMONIC", "CONTENT", "SCULPTING", "CHORUS", 8}, {"KARPLUSSTR", "BOW", "POSITION", "DECAY", 9}, {"V.ANALOG", "DETUNE", "SHAPE", "WAVE", 13},
    {"WAVESHAPER", "WAVE", "AMOUNT", "ASYM", 2}, {"TWO OP.FM", "RATIO", "AMOUNT", "SHAPE", 1}, {"FORMANT", "INTERVAL", "FORMANT", "SHAPE", 3},
    {"CHORDS", "CHORD", "INV/TRANSP", "WAVEFORM", 5}, {"MODAL", "INHARM", "TIMBRE", "DECAY", 6}, {"NOISE", "WAVE", "TIMBRE", "SHAPE", 4},
    {"BASS", "MODEL", "SHAPE", "SUB", 10}, {"SAW", "MODEL", "PARAM 1", "PARAM 2", 11}, {"STRINGS", "MODEL", "STRUCTURE", "BRIGHT", 14},
};
#define NA_RINGS 14u

/* the groups: pages (sub), each with the painter's page number and the four controls */
typedef struct { const char *name; uint8_t ui; int8_t k[4]; } nsub_t;
typedef struct { const char *title; uint8_t ui, n; const nsub_t *sub; } ngrp_t;
static const nsub_t NS_OSC[] = {{"OSCILLATOR", 0, {NI_WAVE, NI_TIMBRE, NI_SHAPE, NI_FOLD}}, {"STRINGS", 1, {NI_POS, NI_NONE, NI_NONE, NI_NONE}}};
static const nsub_t NS_ENV[] = {{"ENVELOPE", 0, {NI_ATTACK, NI_DECAY, NI_SUSTAIN, NI_FAMT}}, {"CYCLING ENV", 1, {NI_RISE, NI_FALL, NI_HOLD, NI_CAMT}},
                                {"CYCLING MODE", 2, {NI_CMODE, NI_NONE, NI_NONE, NI_NONE}}};
static const nsub_t NS_FX[] = {{"FILTER", 0, {NI_CUTOFF, NI_RESO, NI_FTYPE, NI_FAMT}}, {"FILTER 2", 1, {NI_FSLOPE, NI_FDRIVE, NI_FCLIP, NI_NONE}},
                               {"REVERB", 2, {NI_RVBMIX, NI_RVBSIZE, NI_RVBDAMP, NI_NONE}}, {"DELAY", 3, {NI_DLYMIX, NI_DLYTIME, NI_DLYFB, NI_NONE}},
                               {"GLIDE", 4, {NI_GLIDE, NI_GLMODE, NI_NONE, NI_NONE}}, {"IMPRESS / CHANCE", 6, {NI_IMPRESS, NI_CHANCE, NI_NONE, NI_NONE}}};
static const nsub_t NS_LFO[] = {{"LFO", 0, {NI_LSHAPE, NI_LRATE, NI_NONE, NI_LMORPH}}};
static const nsub_t NS_VOICE[] = {{"MONO", 0, {NI_NONE, NI_NONE, NI_NONE, NI_NONE}}, {"UNISON", 1, {NI_UDETUNE, NI_NONE, NI_NONE, NI_NONE}},
                                  {"PARAPHONIC", 2, {NI_NONE, NI_NONE, NI_NONE, NI_NONE}}, {"POLY", 3, {NI_NONE, NI_NONE, NI_NONE, NI_NONE}}};
static const nsub_t NS_ARP[] = {{"ARPEGGIO", 0, {NI_ARPRATE, NI_ARPRANGE, NI_ARPPAT, NI_ARPLEN}}, {"ARP TIMING", 1, {NI_SWING, NI_ARPLATCH, NI_NONE, NI_NONE}},
                                {"ARP TYPE", 2, {NI_ARPTYPE, NI_ARPCHORD, NI_NONE, NI_NONE}}};
static const nsub_t NS_PRE[] = {{"BROWSE", 0, {NI_PBANK, NI_PSLOT, NI_NONE, NI_NONE}}};
#define NGRP(t, u, a) {t, u, (uint8_t)(sizeof(a) / sizeof((a)[0])), a}
static const ngrp_t NGR[NG_COUNT] = {
    [NG_OSC] = NGRP("OSCILLATOR", 0, NS_OSC), [NG_ENV] = NGRP("ENV", 1, NS_ENV), [NG_FX] = NGRP("FX", 2, NS_FX), [NG_LFO] = NGRP("LFO", 3, NS_LFO),
    [NG_ARP] = NGRP("ARP", 5, NS_ARP), [NG_VOICE] = NGRP("VOICE", 7, NS_VOICE), [NG_PRESET] = NGRP("PRESETS", 9, NS_PRE),
};

static uint8_t na_grp, na_sub[NG_COUNT], na_pat, na_slot, na_dirty = 1;
static int8_t na_ost = -1;
static char na_hint[44];

/* the keys of the notes that sound (the arpeggio, the sequence, MIDI): lit like the keys held */
static uint32_t nano_sound_keys(void)
{
    const track_t *t = &trk[0];
    uint32_t m = 0, i, k;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate)
            for (k = 0; k < 27u; k++)
                if (kb_map(t, k) == t->v[i].note)
                    m |= 1u << k;
    return m;
}
static void fa_seq_note(track_t *t, uint32_t note) { (void)t; (void)note; }   /* (ui.c / seq.c hooks of the FMini-1 screen) */
static void fa_factory_extras(track_t *t, uint32_t n) { (void)t; (void)n; }

static int32_t na_cl(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static int32_t na_f127(int32_t v) { return (v * 127 + 50) / 100; }                 /* 0..100 -> 0..127 */
static int32_t na_f100(int32_t v) { return (v * 100 + 63) / 127; }                 /* 0..127 -> 0..100 */
static uint32_t na_type(void)                                                       /* the oscillator list's index of the track's TYPE */
{
    uint32_t i, e = (uint32_t)TSEL->p[P_E0];
    for (i = 0; i < 15u; i++)
        if (NOSC[i].ty == e)
            return i;
    return 0;
}
static uint32_t na_nsub(void)
{
    uint32_t n = NGR[na_grp].n;
    return na_grp == NG_OSC && (uint32_t)TSEL->p[P_E0] != NA_RINGS ? 1u : n;
}
static uint32_t na_cursub(void)
{
    uint32_t n = na_nsub();
    return na_sub[na_grp] >= n ? 0u : na_sub[na_grp];
}
static void na_put(uint32_t id, int32_t v)                                          /* a track parameter, seen by the motion recorder and the CC out */
{
    track_t *t = TSEL;
    t->p[id] = (int16_t)v;
    motion_capture(t, id, t->p[id]);
}
static uint32_t na_arp_on(void) { return TSEL->p[P_AMODE] != 0; }
static void na_arp_pat_sync(void)                                                   /* P_AMODE follows the pattern while the arp is on */
{
    uint32_t i;
    if (!na_arp_on())
        return;
    for (i = 0; i < 14u; i++)
        if (NANO_APAT[i] == TSEL->p[P_AMODE])
            na_pat = (uint8_t)i;
}

/* the value of control id: 0..100, or the step */
static int32_t na_get(int32_t id)
{
    const track_t *t = TSEL;
    const int16_t *p = t->p;
    switch (id) {
    case NI_WAVE: return na_f100(p[P_E1]);
    case NI_TIMBRE: return na_f100(p[P_E2]);
    case NI_SHAPE: return na_f100(p[P_E3]);
    case NI_FOLD: return na_f100(p[P_E4]);
    case NI_POS: return na_f100(p[P_E7]);
    case NI_CUTOFF: return na_f100(p[P_E5]);
    case NI_RESO: return na_f100(p[P_E6]);
    case NI_FTYPE: return na_cl(p[P_FTYPE], 0, 2);
    case NI_FAMT: return na_cl(p[P_ED_FLT], 0, 63) * 100 / 63;
    case NI_FSLOPE: return p[P_ED_FX] & 1;
    case NI_FDRIVE: return ((p[P_ED_FX] >> 1) & 7) * 100 / 7;
    case NI_FCLIP: return ((p[P_ED_FX] >> 4) & 7) * 100 / 7;
    case NI_RVBMIX: return na_f100(p[P_REV]);
    case NI_RVBSIZE: return na_f100(song.g[G_RSIZE]);
    case NI_RVBDAMP: return na_f100(song.g[G_RDAMP]);
    case NI_DLYMIX: return na_f100(p[P_DLY]);
    case NI_DLYTIME: return na_f100(song.g[G_DRREV]);
    case NI_DLYFB: return na_cl(song.g[G_DFDBK], 0, 120) * 100 / 120;
    case NI_GLIDE: return na_f100(p[P_GLIDE]);
    case NI_GLMODE: return p[P_GLMODE] ? 0 : 1;
    case NI_IMPRESS: return na_f100(p[P_LN3]);
    case NI_CHANCE: return na_f100(p[P_LN4]);
    case NI_ATTACK: return na_f100(p[P_ATK]);
    case NI_DECAY: return na_f100(p[P_DEC]);
    case NI_SUSTAIN: return na_f100(p[P_SUS]);
    case NI_RISE: return na_f100(p[P_FM1_ATK]);
    case NI_FALL: return na_f100(p[P_FM1_DEC]);
    case NI_HOLD: return na_f100(p[P_FM1_SUS]);
    case NI_CAMT: return na_f100(p[P_FM1_REL]);
    case NI_CMODE: return na_cl(p[P_FM1_LEVEL], 0, 2);
    case NI_LSHAPE: return na_cl(p[P_LWAVE], 0, 5);
    case NI_LRATE: return na_f100(p[P_LRATE]);
    case NI_LMORPH: return na_f100(p[P_FM2_ATK]);
    case NI_UDETUNE: return na_f100(p[P_DETUNE]);
    case NI_ARPRATE: return na_cl((song.g[G_BPM] - 40) / 2, 0, 100);
    case NI_ARPRANGE: return na_cl(p[P_AOCT], 1, 4) - 1;
    case NI_ARPPAT: return na_pat;
    case NI_ARPLEN: return (int32_t)nano_alen(t) - 1;
    case NI_SWING: return na_cl(p[P_ASWING], 0, 100);
    case NI_ARPLATCH: return p[P_AHOLD] != 0;
    case NI_ARPTYPE: return nano_atype(t);
    case NI_ARPCHORD: return (int32_t)nano_achord(t);
    case NI_PSLOT: return na_slot;
    default: return 0;
    }
}
/* set control id to v (0..100 or the step) */
static void na_set(int32_t id, int32_t v)
{
    track_t *t = TSEL;
    const nk_t *k = &NK[id];
    v = na_cl(v, 0, k->n ? (int32_t)k->n - 1 : 100);
    switch (id) {
    case NI_WAVE: na_put(P_E1, na_f127(v)); break;
    case NI_TIMBRE: na_put(P_E2, na_f127(v)); break;
    case NI_SHAPE: na_put(P_E3, na_f127(v)); break;
    case NI_FOLD: na_put(P_E4, na_f127(v)); break;
    case NI_POS: na_put(P_E7, na_f127(v)); break;
    case NI_CUTOFF: na_put(P_E5, na_f127(v)); break;
    case NI_RESO: na_put(P_E6, na_f127(v)); break;
    case NI_FTYPE: na_put(P_FTYPE, v); break;
    case NI_FAMT: na_put(P_ED_FLT, (v * 63 + 50) / 100); break;
    case NI_FSLOPE: na_put(P_ED_FX, (t->p[P_ED_FX] & ~1) | v); break;
    case NI_FDRIVE: na_put(P_ED_FX, (t->p[P_ED_FX] & ~(7 << 1)) | (((v * 7 + 50) / 100) << 1)); break;
    case NI_FCLIP: na_put(P_ED_FX, (t->p[P_ED_FX] & ~(7 << 4)) | (((v * 7 + 50) / 100) << 4)); break;
    case NI_RVBMIX: na_put(P_REV, na_f127(v)); break;
    case NI_RVBSIZE: song.g[G_RSIZE] = (int16_t)na_f127(v); break;
    case NI_RVBDAMP: song.g[G_RDAMP] = (int16_t)na_f127(v); break;
    case NI_DLYMIX: na_put(P_DLY, na_f127(v)); break;
    case NI_DLYTIME: song.g[G_DRREV] = (int16_t)na_f127(v); break;
    case NI_DLYFB: song.g[G_DFDBK] = (int16_t)(v * 120 / 100); break;
    case NI_GLIDE: na_put(P_GLIDE, na_f127(v)); break;
    case NI_GLMODE: na_put(P_GLMODE, v ? 0 : 1); break;
    case NI_IMPRESS: na_put(P_LN3, na_f127(v)); break;
    case NI_CHANCE: na_put(P_LN4, na_f127(v)); break;
    case NI_ATTACK: na_put(P_ATK, na_f127(v)); break;
    case NI_DECAY: na_put(P_DEC, na_f127(v)); na_put(P_REL, na_f127(v)); break;
    case NI_SUSTAIN: na_put(P_SUS, na_f127(v)); break;
    case NI_RISE: na_put(P_FM1_ATK, na_f127(v)); break;
    case NI_FALL: na_put(P_FM1_DEC, na_f127(v)); break;
    case NI_HOLD: na_put(P_FM1_SUS, na_f127(v)); break;
    case NI_CAMT: na_put(P_FM1_REL, na_f127(v)); break;
    case NI_CMODE: na_put(P_FM1_LEVEL, v); break;
    case NI_LSHAPE: na_put(P_LWAVE, v); break;
    case NI_LRATE: na_put(P_LRATE, na_f127(v)); break;
    case NI_LMORPH: na_put(P_FM2_ATK, na_f127(v)); break;
    case NI_UDETUNE: na_put(P_DETUNE, na_f127(v)); break;
    case NI_ARPRATE: song.g[G_BPM] = (int16_t)(40 + v * 2); break;
    case NI_ARPRANGE: na_put(P_AOCT, v + 1); break;
    case NI_ARPPAT:
        na_pat = (uint8_t)v;
        if (na_arp_on())
            na_put(P_AMODE, NANO_APAT[v]);
        break;
    case NI_ARPLEN: na_put(P_LN0, (v * 127 + 7) / 15); break;
    case NI_SWING: na_put(P_ASWING, v); break;
    case NI_ARPLATCH: na_put(P_AHOLD, v); break;
    case NI_ARPTYPE: na_put(P_LN2, v); break;
    case NI_ARPCHORD: na_put(P_LN1, v); break;
    default: break;
    }
}
static void na_load(uint32_t n);

static uint32_t na_npre(void) { return ENGINES[ENGI_NANO]->npresets; }
static void na_str(char *d, const char *s)
{
    uint32_t i = 0;
    while (s[i] && i < 43u) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}
static void na_cat(char *d, const char *s)
{
    na_str(d + str_len(d), s);
}
static void na_say(const char *s) { na_str(na_hint, s); }

/* the text of control id's value */
static void na_text(int32_t id, char *out)
{
    const nk_t *k = &NK[id];
    int32_t v = na_get(id);
    out[0] = 0;
    if (k->tx) {
        na_str(out, k->tx[v]);
    } else if (id == NI_ARPRANGE) {
        fmt_int(out, v + 1);
        na_cat(out, " OCT");
    } else if (id == NI_ARPLEN) {
        fmt_int(out, v + 1);
    } else if (id == NI_ARPRATE) {
        fmt_int(out, 40 + v * 2);
        na_cat(out, " BPM");
    } else if (id == NI_SWING) {
        fmt_int(out, 50 + v / 4);
        na_cat(out, "%");
    } else if (id == NI_PSLOT) {
        fmt_int(out, v + 1);
    } else {
        fmt_int(out, v);
        na_cat(out, "%");
    }
}

/* everything the painter shows, for the page the cursor is on */
static void na_sync(void)
{
    const ngrp_t *gp = &NGR[na_grp];
    uint32_t ns = na_nsub(), sb = na_cursub(), i, ty = na_type();
    const nsub_t *sp = &gp->sub[sb];
    const track_t *t = TSEL;
    char b[24];
    nf_ui_set(UI_GROUP, gp->ui);
    nf_ui_set(UI_SUB, sp->ui);
    nf_ui_set(UI_NSUB, (int)ns);
    na_str(nf_ui_str(UIS_TITLE), gp->title);
    {
        char *s = nf_ui_str(UIS_SUB);
        na_str(s, sp->name);
        if (ns > 1u) {
            na_cat(s, " ");
            fmt_int(b, (int32_t)sb + 1);
            na_cat(s, b);
            na_cat(s, "/");
            fmt_int(b, (int32_t)ns);
            na_cat(s, b);
        }
    }
    na_str(nf_ui_str(UIS_TYPE), NOSC[ty].name);
    for (i = 0; i < 4u; i++) {
        int32_t id = sp->k[i];
        const char *lb = id == NI_NONE ? "" : NK[id].lb;
        if (na_grp == NG_OSC && sb == 0u && i < 3u)
            lb = i == 0u ? NOSC[ty].l1 : i == 1u ? NOSC[ty].l2 : NOSC[ty].l3;
        na_str(nf_ui_str(UIS_L0 + (int)i), lb);
        if (id == NI_NONE) {
            nf_ui_str(UIS_V0 + (int)i)[0] = 0;
            nf_ui_set(UI_N0 + (int)i, -1);
        } else {
            na_text(id, nf_ui_str(UIS_V0 + (int)i));
            nf_ui_set(UI_N0 + (int)i, NK[id].n ? 0 : na_get(id));
        }
    }
    na_str(nf_ui_str(UIS_HINT), na_hint);
    na_str(nf_ui_str(UIS_PRESET), ENGINES[ENGI_NANO]->presets[na_slot % na_npre()].name);
    nf_ui_set(UI_OCT, song.octave);
    nf_ui_set(UI_POLY, t->p[P_VOICE] == V_MONO ? 0 : t->p[P_VOICE] == V_UNISON ? 1 : t->p[P_VOICE] == V_POLY ? 3 : 0);
    nf_ui_set(UI_PLAY, na_arp_on());
    nf_ui_set(UI_TYPE, (int)ty);
    nf_ui_set(UI_NTYPES, 15);
    nf_ui_set(UI_STEP, -1);
    nf_ui_set(UI_P, 0);
    switch (na_grp) {
    case NG_ENV:
        nf_ui_set(UI_P, na_get(NI_ATTACK)), nf_ui_set(UI_P + 1, na_get(NI_DECAY)), nf_ui_set(UI_P + 2, na_get(NI_SUSTAIN));
        if (sp->ui == 1u)
            nf_ui_set(UI_P, na_get(NI_RISE)), nf_ui_set(UI_P + 1, na_get(NI_FALL)), nf_ui_set(UI_P + 2, na_get(NI_HOLD));
        nf_ui_set(UI_P + 3, sp->ui == 0u ? 1 : na_get(NI_CMODE));      /* (the envelope page: the amp envelope on) */
        break;
    case NG_FX:
        if (sp->ui == 0u)
            nf_ui_set(UI_P, na_get(NI_CUTOFF)), nf_ui_set(UI_P + 1, na_get(NI_RESO)), nf_ui_set(UI_P + 2, na_get(NI_FTYPE)), nf_ui_set(UI_P + 3, na_get(NI_FSLOPE));
        else if (sp->ui == 1u)
            nf_ui_set(UI_P, na_get(NI_FSLOPE)), nf_ui_set(UI_P + 1, na_get(NI_FDRIVE)), nf_ui_set(UI_P + 2, na_get(NI_FCLIP));
        else if (sp->ui == 2u)
            nf_ui_set(UI_P, na_get(NI_RVBMIX)), nf_ui_set(UI_P + 1, na_get(NI_RVBSIZE)), nf_ui_set(UI_P + 2, na_get(NI_RVBDAMP)), nf_ui_set(UI_P + 3, 0);
        else if (sp->ui == 3u)
            nf_ui_set(UI_P, na_get(NI_DLYMIX)), nf_ui_set(UI_P + 1, na_get(NI_DLYTIME)), nf_ui_set(UI_P + 2, na_get(NI_DLYFB)), nf_ui_set(UI_P + 3, 50);
        else if (sp->ui == 4u)
            nf_ui_set(UI_P, na_get(NI_GLIDE)), nf_ui_set(UI_P + 1, na_get(NI_GLMODE) ? 2 : 0);
        else
            nf_ui_set(UI_P, na_get(NI_IMPRESS)), nf_ui_set(UI_P + 1, na_get(NI_CHANCE));
        break;
    case NG_LFO:
        nf_ui_set(UI_P, na_get(NI_LSHAPE)), nf_ui_set(UI_P + 1, na_get(NI_LRATE)), nf_ui_set(UI_P + 2, 0), nf_ui_set(UI_P + 3, na_get(NI_LMORPH));
        break;
    case NG_VOICE:
        nf_ui_set(UI_P, na_get(NI_UDETUNE));
        break;
    case NG_ARP:
        nf_ui_set(UI_P, na_pat), nf_ui_set(UI_P + 1, na_get(NI_ARPRANGE) + 1), nf_ui_set(UI_P + 2, 0), nf_ui_set(UI_P + 3, na_get(NI_ARPLEN) + 1);
        nf_ui_set(UI_P + 4, na_get(NI_SWING)), nf_ui_set(UI_P + 5, na_get(NI_ARPLATCH)), nf_ui_set(UI_P + 6, na_get(NI_ARPTYPE)), nf_ui_set(UI_P + 7, na_get(NI_ARPCHORD));
        break;
    case NG_PRESET: {                                    /* the list: six rows round the preset shown */
        uint32_t n = na_npre(), first = na_slot < 3u ? 0u : na_slot + 3u >= n ? (n > 6u ? n - 6u : 0u) : na_slot - 3u, rows = n < 6u ? n : 6u;
        for (i = 0; i < rows; i++) {
            char *r = nf_ui_str(UIS_ROW0 + (int)i);
            fmt_int(b, (int32_t)(first + i + 1u));
            r[0] = 0;
            if (first + i + 1u < 10u)
                na_cat(r, "0");
            na_cat(r, b);
            na_cat(r, " ");
            na_cat(r, ENGINES[ENGI_NANO]->presets[first + i].name);
        }
        nf_ui_set(UI_P, (int)(na_slot - first));
        nf_ui_set(UI_P + 1, (int)rows);
        break;
    }
    default:
        break;
    }
    na_dirty = 0;
}

/* ------------------------------------------------------------- input --- */
static void na_load(uint32_t n)                           /* preset n of the list: the sound, the arp and the performance values it starts with */
{
    track_t *t = TSEL;
    n %= na_npre();
    na_slot = (uint8_t)n;
    apply_preset_to(t, n);
    na_pat = 0;
    na_arp_pat_sync();
    na_ost = -1;
    na_dirty = 1;
    ui.force = 1;
}
static void na_knob(uint32_t k, int32_t s)
{
    const nsub_t *sp = &NGR[na_grp].sub[na_cursub()];
    int32_t id = sp->k[k], v;
    if (id == NI_NONE || na_grp == NG_PRESET)
        return;
    fmg_force_page = 0;
    v = na_get(id) + (NK[id].n ? (s > 0 ? 1 : -1) : id == NI_FDRIVE || id == NI_FCLIP ? (s > 0 ? 15 : -15) : fmg_acc(EN_K1 + k, s, 100));   /* (DRIVE, CLIP: seven steps) */
    fmg_force_page = -1;
    na_set(id, v);
    na_say(NK[id].lb);
    na_cat(na_hint, " ");
    na_text(id, na_hint + str_len(na_hint));
    na_dirty = 1;
}
static void na_group(uint32_t g)
{
    na_grp = (uint8_t)(na_grp == g ? NG_OSC : g);
    if (na_grp == NG_VOICE)                                  /* the page is the track's mode */
        na_sub[NG_VOICE] = TSEL->p[P_VOICE] == V_MONO ? 0u : TSEL->p[P_VOICE] == V_UNISON ? 1u : 3u;
    na_dirty = 1;
}
static void na_buttons(uint32_t pressed)
{
    static const uint8_t BT[8] = {B_FX, B_ENV, B_LFO, B_SCL, B_ARP, B_SEQ, B_EDIT, B_GLO};
    static const uint8_t TO[8] = {NG_FX, NG_ENV, NG_LFO, NG_VOICE, NG_ARP, NG_SEQ, NG_MATRIX, NG_GLOBAL};
    uint32_t i;
    for (i = 0; i < 8u; i++)
        if ((pressed >> panel.btn[BT[i]]) & 1u) {
            if (NGR[TO[i]].n == 0u) {                          /* (not on the device yet) */
                na_say("NOT YET ON THE FM-1");
                na_dirty = 1;
            } else
                na_group(TO[i]);
        }
    if ((pressed >> panel.btn[B_HOME]) & 1u) {
        na_grp = NG_OSC;
        na_dirty = 1;
    }
    if ((pressed >> panel.btn[B_PLAY]) & 1u) {
        na_put(P_AMODE, na_arp_on() ? 0 : NANO_APAT[na_pat]);
        na_say(na_arp_on() ? "ARP ON" : "ARP OFF");
        na_dirty = 1;
    }
    if ((pressed >> panel.btn[B_OCTDN]) & 1u) {
        song.octave = song.octave > -3 ? song.octave - 1 : song.octave;
        na_dirty = 1;
    }
    if ((pressed >> panel.btn[B_OCTUP]) & 1u) {
        song.octave = song.octave < 3 ? song.octave + 1 : song.octave;
        na_dirty = 1;
    }
}
static void na_voice_sync(void)                            /* the VOICE page the cursor is on is the track's mode */
{
    track_t *t = TSEL;
    uint32_t sb = na_cursub(), want = sb == 0u ? V_MONO : sb == 1u ? V_UNISON : V_POLY;
    if (na_grp != NG_VOICE || t->p[P_VOICE] == (int16_t)want)
        return;
    t->p[P_VOICE] = (int16_t)want;
    t->p[P_PRIO] = 1;
    panic_req |= (uint8_t)(1u << trk_index(t));
    motion_capture(t, P_VOICE, t->p[P_VOICE]);
}

static void na_input(void)
{
    uint32_t pressed = fm1_input_edges(0), k;
    int32_t s;
    (void)fm1_input_note_edges();
    if (panel_took_moved() | pressed | fm1_in.notes | fm1_in.buttons)
        scrn.idle = fm1_ms;
    midi_take();                                            /* MIDI IN notes */
    midi_pc_poll();                                         /* Program Change: a preset */
    settings_later_poll();
    song.sel = 0;
    na_buttons(pressed);
    if ((s = panel_enc(EN_SELECT)) != 0) {                  /* the next / previous page of the group */
        uint32_t n = na_nsub();
        if (na_grp == NG_PRESET)
            na_load((na_slot + na_npre() + (s > 0 ? 1u : na_npre() - 1u)) % na_npre());
        else
            na_sub[na_grp] = (uint8_t)((na_cursub() + (s > 0 ? 1u : n - 1u)) % n);
        na_voice_sync();
        na_dirty = 1;
    }
    if ((s = panel_enc(EN_ALGO)) != 0) {                    /* the oscillator type */
        uint32_t ty = (na_type() + (s > 0 ? 1u : 14u)) % 15u;
        na_put(P_E0, NOSC[ty].ty);
        na_say(NOSC[ty].name);
        na_dirty = 1;
    }
    if ((s = panel_enc(EN_PRESET)) != 0) {                  /* the preset list: it opens, and every step loads */
        if (na_grp != NG_PRESET) {
            na_grp = NG_PRESET;
            na_sub[NG_PRESET] = 0;
        }
        na_load((na_slot + na_npre() + (s > 0 ? 1u : na_npre() - 1u)) % na_npre());
    }
    for (k = 0; k < 4u; k++)
        if ((s = panel_enc(EN_K1 + k)) != 0)
            na_knob(k, s);
}

static void na_leds(void)
{
    uint8_t nl[LED_BUF] = {0}, nd[LED_BUF] = {0}, nb[LED_BUF] = {0}, nm[LED_BUF] = {0};
    uint32_t k;
    static uint8_t ready;
    if (idle_leds() || fm1_led_anim_on())
        return;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    for (k = 0; k < NB; k++)
        led_put(nd, panel.btn[k], 1);
    for (k = 0; k < 27u; k++)
        led_put(nd, LED_KEY0 + k, 1);
    {
        static const uint8_t GB[NG_COUNT] = {B_HOME, B_ENV, B_FX, B_LFO, B_EDIT, B_ARP, B_SEQ, B_SCL, B_GLO, B_SAVE};
        led_put(nl, panel.btn[GB[na_grp]], 1);
    }
    led_put(nl, panel.btn[B_PLAY], na_arp_on());
    led_put(nl, panel.btn[B_OCTDN], song.octave < 0);
    led_put(nl, panel.btn[B_OCTUP], song.octave > 0);
    {
        uint32_t c = fm1_in.notes | nano_sound_keys();
        for (k = 0; k < 27u; k++)
            led_put(nl, LED_KEY0 + k, (int)((c >> k) & 1u));
    }
    for (k = 0; k < FM1_NCOL; k++) {
        fm1_led_dim[k] = nd[k];
        fm1_led_breath[k] = nb[k];
        fm1_led_mid[k] = nm[k];
        fm1_led[k] = nl[k];
    }
}

/* -------------------------------------------------------------- draw --- */
static void na_paint(uint32_t band)
{
    uint32_t i;
    cv_begin(240, 120, 0);
    nf_ui_paint_band(cv_px, (int)band * 120, 120);
    for (i = 0; i < 240u * 120u; i++)                        /* (the canvas holds the LCD's byte order) */
        cv_px[i] = swap16(cv_px[i]);
    cv_blit(0, band * 120u);
}
static void na_draw(void)
{
    uint32_t band;
    static uint32_t prev = 0xFFFFFFFFu;
    uint32_t sig = 0x811C9DC5u, i;
    if (na_dirty)
        na_sync();
    for (i = 0; i < (uint32_t)UI_COUNT; i++)
        sig = (sig ^ (uint32_t)nf_ui_get((int)i)) * 16777619u;
    for (i = 0; i < (uint32_t)UIS_COUNT; i++) {
        const char *c = nf_ui_str((int)i);
        while (*c)
            sig = (sig ^ (uint8_t)*c++) * 16777619u;
    }
    if (sig == prev && !ui.force)
        return;
    prev = sig;
    ui.force = 0;
    for (band = 0; band < 2u; band++)
        na_paint(band);
}
static void na_splash(void)
{
    uint32_t band;
    nf_ui_init();
    nf_ui_set(UI_GROUP, 10);
    na_str(nf_ui_str(UIS_HINT), FMINI_VERSION);
    for (band = 0; band < 2u; band++)
        na_paint(band);
}
static void na_init(void)
{
    uint32_t i;
    nf_ui_init();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if (t->eng_req != ENGI_NANO || i) {
            set_engine_of(t, ENGI_NANO);
            apply_preset_to(t, 0);
            t->eng_req = ENGI_NANO;
            t->engine = t->eng_req;
        }
        t->p[P_MUTE] = i ? 1 : 0;
    }
    song.sel = 0;
    na_pat = 0;
    na_arp_pat_sync();
    na_slot = 0;
    na_grp = NG_OSC;
    na_dirty = 1;
    ui_idle |= 1u;
    ui.force = 1;
}
static void na_smk_faders(void) {}
#endif /* FELUCCA_FMINI_ONLY && FELUCCA_NANO_APP */
