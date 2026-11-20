/* SPDX-License-Identifier: GPL-3.0-only */
/* Nano Freak: the whole user interface of the product firmware (FELUCCA_FMINI_ONLY=1). It is the test bed's
 * screen and controls (github.com/Virusiinstaller/nano-freak: dsp/nf_ui.c is the painter, index.html the pages), on the FM-1's
 * panel: the group buttons FX ENV LFO SCL (VOICE) ARP SEQ EDIT (MATRIX) GLO SAVE, HOME for OSC, SELECT steps through the pages of a
 * group, KNOB 1..4 edit its four controls, ALGORITHM the oscillator type, PRESETS the preset list, OCT- / OCT+ the keys' octave,
 * PLAY the arpeggiator. main.c calls na_input / na_leds / na_draw / na_splash / na_init (as fa_* of fmini_app.c).
 *
 * The controls are a table (NI_*: a label, how many steps or 0..100, the track parameter they are): the painter reads the
 * values through nf_ui_set / nf_ui_str, as the test bed's page does, one band of 120 rows at a time.
 * Included by felucca.c before main.c. */
#if FELUCCA_FMINI_ONLY
#include "nano_gfx.c"
#include "nano_ui.c"

enum { NG_OSC, NG_ENV, NG_FX, NG_LFO, NG_MATRIX, NG_ARP, NG_SEQ, NG_VOICE, NG_GLOBAL, NG_PRESET, NG_COUNT };   /* = the painter's UI_GROUP */

/* the controls */
enum {
    NI_NONE = -1,
    NI_WAVE, NI_TIMBRE, NI_SHAPE, NI_FOLD, NI_POS,
    NI_CUTOFF, NI_RESO, NI_FTYPE, NI_FAMT, NI_FSLOPE, NI_FDRIVE, NI_FCLIP,
    NI_RVBMIX, NI_RVBSIZE, NI_RVBDAMP, NI_RVBPRE, NI_DLYMIX, NI_DLYTIME, NI_DLYFB, NI_DLYTONE,
    NI_GLIDE, NI_GLMODE, NI_IMPRESS, NI_CHANCE,
    NI_ATTACK, NI_DECAY, NI_SUSTAIN, NI_RISE, NI_FALL, NI_HOLD, NI_CAMT, NI_CMODE,
    NI_LSHAPE, NI_LRATE, NI_LMORPH, NI_UDETUNE,
    NI_ARPRATE, NI_ARPRANGE, NI_ARPPAT, NI_ARPLEN, NI_SWING, NI_ARPLATCH, NI_ARPTYPE, NI_ARPCHORD,
    NI_PBANK, NI_PSLOT,
    NI_SQPAT, NI_SQRND, NI_SQMUT, NI_SQLEN, NI_SQSCALE, NI_SQOCT, NI_SQSWING, NI_SQSTEP, NI_SQNOTE, NI_SQDUR, NI_SQREST, NI_SQENT, NI_SQRESTS, NI_SQGATE,
    NI_MXCOL, NI_MXROW, NI_MXON, NI_MXAMT, NI_AS1, NI_AS2, NI_AS3, NI_AS4,
    NI_GBT, NI_GBTA, NI_GROLE, NI_GCLK, NI_MMODE, NI_MCH, NI_MPORT, NI_MKEY, NI_MLOCAL, NI_MCLKO, NI_MTHI, NI_MTHO, NI_MINCH,   /* GLOBAL: (with the six below and NI_BENDRNG, these are na_g[]) */
    NI_BDEV, NI_BACT, NI_SUPATCH, NI_SUBOOT, NI_SUPAGE, NI_SURADIO, NI_BENDRNG,
    NI_SSLOT, NI_PCUR, NI_PCHR,
    NI_BENDSPD, NI_AMPMOD, NI_SM1, NI_SM2, NI_SM3, NI_SM4, NI_CDEST, NI_CDEPTH, NI_LSYNC, NI_FOLD2,
    NI_FMODEL,
    NI_COUNT
};
static const char *const NT_FTYPE[] = {"LPF", "BPF", "HPF"};
static const char *const NT_SLOPE[] = {"12 DB", "24 DB"};
static const char *const NT_FMODEL[] = {"SVF", "LADDER"};
static const char *const NT_OFFON[] = {"OFF", "ON"};
static const char *const NT_AMPENV[] = {"GATE ONLY", "ENV ON"};
static const char *const NT_GLMODE[] = {"TIME", "SYNC", "RATE"};
static const char *const NT_LSHAPE[] = {"SINE", "TRI", "SAW", "SQUARE", "S&H", "GLIDE"};
static const char *const NT_CMODE[] = {"ENV", "RUN", "LOOP"};
static const char *const NT_CDEST[] = {"PITCH", "OSC K2", "OSC K3", "CUTOFF", "A1", "A2", "A3", "A4"};
static const char *const NT_PAT[] = {"UP", "DOWN", "UP/DOWN", "ORDER", "RANDOM", "PATTERN", "AS PLAYED", "CONVERGE", "DIVERGE", "HOLLOW", "JEOPARDY", "CHORD", "BROKEN", "ALTERNATING"};
static const char *const NT_CHORD[] = {"OFF", "MAJ", "MIN", "7", "MIN7", "MAJ7", "SUS4", "DIM7", "POWER"};
static const char *const NT_ONOFF[] = {"OFF", "ON"};
static const char *const NT_TYPE[] = {"MONO", "CHORD"};
static const char *const NT_SQSCALE[] = {"MIN PENT", "MAJ PENT", "MAJOR", "MINOR", "DORIAN", "BLUES", "CHROMATIC"};
static const char *const NT_SQDUR[] = {"1/16", "1/8", "1/4", "1/2"};
static const char *const NT_SQTYPE[] = {"NOTE", "REST"};
static const char *const NT_SQPAT[] = {"A", "B"};
static const char *const NT_BANK[] = {"USER", "FACTORY"};
static const char *const NT_ROLE[] = {"DEVICE", "HOST"};
static const char *const NT_CLK[] = {"INTERNAL", "MIDI"};
static const char *const NT_MMODE[] = {"LOCAL", "BOTH", "MIDI"};
static const char *const NT_PORT[] = {"OFF", "DIN", "USB", "DIN+USB", "BLE", "DIN+BLE", "USB+BLE", "ALL"};
static const char *const NT_THRU[] = {"DIN", "USB", "BLE"};
static const char *const NT_BACT[] = {"CONNECT", "SCAN", "DISCON.", "FORGET"};
static const char *const NT_SUPAGE[] = {"OSC", "LAST"};
static const char *const NT_SURAD[] = {"OFF", "LAST"};
static const char *const NT_MXCOL[] = {"PITCH", "OSC K2", "OSC K3", "CUTOFF", "ASSIGN 1", "ASSIGN 2", "ASSIGN 3", "ASSIGN 4"};
static const char *const NT_MXROW[] = {"CYC ENV", "ENV", "LFO", "KEY"};
/* the destinations an ASSIGN column can take: the firmware's modulation destinations that are not a column already */
static const char *const NT_ASG[] = {"OSC K1", "OSC K4", "RESONANCE", "KEY TRACK", "LFO RATE", "AMP", "PAN", "DISTORTION", "CHORUS", "DELAY", "REVERB", "VIBRATO"};
typedef struct { const char *lb; uint8_t n; const char *const *tx; } nk_t;   /* n 0: continuous 0..100 */
static const nk_t NK[NI_COUNT] = {
    [NI_WAVE] = {"WAVE", 0, 0}, [NI_TIMBRE] = {"TIMBRE", 0, 0}, [NI_SHAPE] = {"SHAPE", 0, 0}, [NI_FOLD] = {"FOLD", 0, 0}, [NI_POS] = {"POSITION", 0, 0},
    [NI_CUTOFF] = {"CUTOFF", 0, 0}, [NI_RESO] = {"RESO", 0, 0}, [NI_FTYPE] = {"TYPE", 3, NT_FTYPE}, [NI_FAMT] = {"FILT AMT", 0, 0},
    [NI_FSLOPE] = {"SLOPE", 2, NT_SLOPE}, [NI_FMODEL] = {"MODEL", 2, NT_FMODEL}, [NI_FDRIVE] = {"DRIVE", 0, 0}, [NI_FCLIP] = {"CLIP", 0, 0},
    [NI_RVBMIX] = {"MIX", 0, 0}, [NI_RVBSIZE] = {"SIZE", 0, 0}, [NI_RVBDAMP] = {"DAMP", 0, 0},
    [NI_RVBPRE] = {"PRE-DELAY", 0, 0}, [NI_DLYTONE] = {"TONE", 0, 0}, [NI_DLYMIX] = {"MIX", 0, 0}, [NI_DLYTIME] = {"TIME", 0, 0}, [NI_DLYFB] = {"FEEDBACK", 0, 0},
    [NI_GLIDE] = {"GLIDE", 0, 0}, [NI_GLMODE] = {"MODE", 3, NT_GLMODE}, [NI_IMPRESS] = {"IMPRESS", 0, 0}, [NI_CHANCE] = {"CHANCE", 0, 0},
    [NI_ATTACK] = {"ATTACK", 0, 0}, [NI_DECAY] = {"DECAY", 0, 0}, [NI_SUSTAIN] = {"SUSTAIN", 0, 0},
    [NI_RISE] = {"RISE", 0, 0}, [NI_FALL] = {"FALL", 0, 0}, [NI_HOLD] = {"HOLD", 0, 0}, [NI_CAMT] = {"AMOUNT", 0, 0}, [NI_CMODE] = {"MODE", 3, NT_CMODE},
    [NI_LSHAPE] = {"SHAPE", 6, NT_LSHAPE}, [NI_LRATE] = {"RATE", 0, 0}, [NI_LMORPH] = {"MORPH", 0, 0}, [NI_UDETUNE] = {"DETUNE", 0, 0},
    [NI_ARPRATE] = {"RATE", 0, 0}, [NI_ARPRANGE] = {"RANGE", 4, 0}, [NI_ARPPAT] = {"PATTERN", 14, NT_PAT}, [NI_ARPLEN] = {"LENGTH", 16, 0},
    [NI_SWING] = {"SWING", 0, 0}, [NI_ARPLATCH] = {"LATCH", 2, NT_ONOFF}, [NI_ARPTYPE] = {"TYPE", 2, NT_TYPE}, [NI_ARPCHORD] = {"CHORD", 9, NT_CHORD},
    [NI_PBANK] = {"BANK", 1, NT_BANK}, [NI_PSLOT] = {"PRESET", 0, 0},
    [NI_SQPAT] = {"PATTERN", 2, NT_SQPAT}, [NI_SQRND] = {"RANDOM", 100, 0}, [NI_SQMUT] = {"MUTATE", 100, 0}, [NI_SQLEN] = {"LENGTH", 64, 0},
    [NI_SQSCALE] = {"SCALE", 7, NT_SQSCALE}, [NI_SQOCT] = {"OCTAVES", 3, 0}, [NI_SQSWING] = {"SWING", 0, 0}, [NI_SQSTEP] = {"STEP", 64, 0},
    [NI_SQNOTE] = {"NOTE", 128, 0}, [NI_SQDUR] = {"LENGTH", 4, NT_SQDUR}, [NI_SQREST] = {"TYPE", 2, NT_SQTYPE}, [NI_SQENT] = {"REC LENGTH", 4, NT_SQDUR},
    [NI_SQRESTS] = {"RESTS", 11, 0}, [NI_SQGATE] = {"GATE", 10, 0},
    [NI_MXCOL] = {"COLUMN", 8, NT_MXCOL}, [NI_MXROW] = {"ROW", 4, NT_MXROW}, [NI_MXON] = {"ON", 2, NT_ONOFF}, [NI_MXAMT] = {"AMOUNT", 0, 0},
    [NI_AS1] = {"A1", 12, NT_ASG}, [NI_AS2] = {"A2", 12, NT_ASG}, [NI_AS3] = {"A3", 12, NT_ASG}, [NI_AS4] = {"A4", 12, NT_ASG},
    [NI_GBT] = {"BLUETOOTH", 2, NT_ONOFF}, [NI_GBTA] = {"BT AUDIO", 2, NT_ONOFF}, [NI_GROLE] = {"BLE ROLE", 2, NT_ROLE}, [NI_GCLK] = {"CLOCK", 2, NT_CLK},
    [NI_MMODE] = {"ARP OUT", 3, NT_MMODE}, [NI_MCH] = {"OUT CH", 16, 0}, [NI_MPORT] = {"OUT PORT", 8, NT_PORT}, [NI_MKEY] = {"KEY OUT", 2, NT_ONOFF},
    [NI_MLOCAL] = {"LOCAL", 2, NT_ONOFF}, [NI_MCLKO] = {"CLOCK OUT", 2, NT_ONOFF}, [NI_MTHI] = {"THRU IN", 3, NT_THRU}, [NI_MTHO] = {"THRU OUT", 8, NT_PORT},
    [NI_MINCH] = {"IN CH", 16, 0}, [NI_BENDRNG] = {"RANGE", 25, 0}, [NI_BENDSPD] = {"SPEED", 0, 0}, [NI_AMPMOD] = {"AMP ENV", 2, NT_AMPENV}, [NI_FOLD2] = {"FOLD", 0, 0}, [NI_LSYNC] = {"SYNC", 2, NT_OFFON}, [NI_CDEST] = {"DEST", 8, NT_CDEST}, [NI_CDEPTH] = {"DEPTH", 0, 0}, [NI_SM1] = {"M1", 2, NT_OFFON}, [NI_SM2] = {"M2", 2, NT_OFFON}, [NI_SM3] = {"M3", 2, NT_OFFON}, [NI_SM4] = {"M4", 2, NT_OFFON}, [NI_BDEV] = {"DEVICE", 8, 0}, [NI_BACT] = {"ACTION", 4, NT_BACT}, [NI_SUPATCH] = {"POWER-ON", 18, 0},
    [NI_SUBOOT] = {"BOOT SCR", 2, NT_ONOFF}, [NI_SUPAGE] = {"START PG", 2, NT_SUPAGE}, [NI_SURADIO] = {"RADIO", 2, NT_SURAD},
    [NI_SSLOT] = {"SAVE TO", 16, 0}, [NI_PCUR] = {"CURSOR", 10, 0}, [NI_PCHR] = {"LETTER", 38, 0},
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
static const nsub_t NS_OSC[] = {{"OSCILLATOR", 0, {NI_WAVE, NI_TIMBRE, NI_SHAPE, NI_FOLD}}, {"STRINGS", 1, {NI_POS, NI_FOLD2, NI_NONE, NI_NONE}}};
static const nsub_t NS_ENV[] = {{"ENVELOPE", 0, {NI_ATTACK, NI_DECAY, NI_SUSTAIN, NI_FAMT}}, {"CYCLING ENV", 1, {NI_RISE, NI_FALL, NI_HOLD, NI_CAMT}},
                                {"CYCLING MODE", 2, {NI_CMODE, NI_CDEST, NI_CDEPTH, NI_NONE}}, {"AMP ENVELOPE", 3, {NI_AMPMOD, NI_NONE, NI_NONE, NI_NONE}}};
static const nsub_t NS_FX[] = {{"FILTER", 0, {NI_CUTOFF, NI_RESO, NI_FTYPE, NI_FAMT}}, {"FILTER 2", 1, {NI_FSLOPE, NI_FDRIVE, NI_FCLIP, NI_FMODEL}},
                               {"REVERB", 2, {NI_RVBMIX, NI_RVBSIZE, NI_RVBDAMP, NI_RVBPRE}}, {"DELAY", 3, {NI_DLYMIX, NI_DLYTIME, NI_DLYFB, NI_DLYTONE}},
                               {"GLIDE", 4, {NI_GLIDE, NI_GLMODE, NI_NONE, NI_NONE}}, {"PITCH BEND", 5, {NI_BENDRNG, NI_BENDSPD, NI_NONE, NI_NONE}}, {"IMPRESS / CHANCE", 6, {NI_IMPRESS, NI_CHANCE, NI_NONE, NI_NONE}}};
static const nsub_t NS_LFO[] = {{"LFO", 0, {NI_LSHAPE, NI_LRATE, NI_LSYNC, NI_LMORPH}}};
static const nsub_t NS_VOICE[] = {{"MONO", 0, {NI_NONE, NI_NONE, NI_NONE, NI_NONE}}, {"UNISON", 1, {NI_UDETUNE, NI_NONE, NI_NONE, NI_NONE}},
                                  {"PARAPHONIC", 2, {NI_NONE, NI_NONE, NI_NONE, NI_NONE}}, {"POLY", 3, {NI_NONE, NI_NONE, NI_NONE, NI_NONE}}};
static const nsub_t NS_ARP[] = {{"ARPEGGIO", 0, {NI_ARPRATE, NI_ARPRANGE, NI_ARPPAT, NI_ARPLEN}}, {"ARP TIMING", 1, {NI_SWING, NI_ARPLATCH, NI_NONE, NI_NONE}},
                                {"ARP TYPE", 2, {NI_ARPTYPE, NI_ARPCHORD, NI_NONE, NI_NONE}}};
static const nsub_t NS_MX[] = {{"MATRIX", 0, {NI_MXCOL, NI_MXROW, NI_MXON, NI_MXAMT}}, {"ASSIGN", 1, {NI_AS1, NI_AS2, NI_AS3, NI_AS4}}};
static const nsub_t NS_SEQ[] = {{"SEQUENCE", 0, {NI_ARPRATE, NI_SQPAT, NI_SQRND, NI_SQMUT}}, {"SEQ LENGTH", 1, {NI_SQLEN, NI_SQSCALE, NI_SQOCT, NI_SQSWING}},
                                {"STEP EDIT", 2, {NI_SQSTEP, NI_SQNOTE, NI_SQDUR, NI_SQREST}}, {"RANDOM", 3, {NI_SQENT, NI_SQRESTS, NI_SQRND, NI_SQGATE}},
                                {"MOD SMOOTH", 4, {NI_SM1, NI_SM2, NI_SM3, NI_SM4}}};
static const nsub_t NS_PRE[] = {{"BROWSE", 0, {NI_PBANK, NI_PSLOT, NI_NONE, NI_NONE}}, {"SAVE", 1, {NI_SSLOT, NI_PCUR, NI_PCHR, NI_NONE}}};
static const nsub_t NS_GLO[] = {{"BLUETOOTH", 0, {NI_GBT, NI_GBTA, NI_GROLE, NI_GCLK}}, {"MIDI OUT", 1, {NI_MMODE, NI_MCH, NI_MPORT, NI_MKEY}},
                                {"MIDI ROUTING", 2, {NI_MLOCAL, NI_MCLKO, NI_MTHI, NI_MTHO}}, {"MIDI IN", 3, {NI_MINCH, NI_GCLK, NI_NONE, NI_NONE}},
                                {"BLE DEVICES", 4, {NI_BDEV, NI_BACT, NI_NONE, NI_NONE}}, {"STARTUP", 5, {NI_SUPATCH, NI_SUBOOT, NI_SUPAGE, NI_SURADIO}}};
#define NGRP(t, u, a) {t, u, (uint8_t)(sizeof(a) / sizeof((a)[0])), a}
static const ngrp_t NGR[NG_COUNT] = {
    [NG_OSC] = NGRP("OSCILLATOR", 0, NS_OSC), [NG_ENV] = NGRP("ENV", 1, NS_ENV), [NG_FX] = NGRP("FX", 2, NS_FX), [NG_LFO] = NGRP("LFO", 3, NS_LFO),
    [NG_MATRIX] = NGRP("MATRIX", 4, NS_MX), [NG_ARP] = NGRP("ARP", 5, NS_ARP), [NG_SEQ] = NGRP("SEQ", 6, NS_SEQ), [NG_VOICE] = NGRP("VOICE", 7, NS_VOICE), [NG_GLOBAL] = NGRP("GLOBAL", 8, NS_GLO), [NG_PRESET] = NGRP("PRESETS", 9, NS_PRE),
};

static uint8_t na_cur, na_root = 48, na_rec, na_seqmode, na_grp, na_sub[NG_COUNT], na_pat, na_slot, na_dirty = 1;
static int8_t na_ost = -1;
/* the GLOBAL settings (ids NI_GBT.. in order), saved in the flash settings record (favorites.factory[6]: no engine of this firmware
 * uses that row), and the PRESETS state: the bank (0 USER, 1 FACTORY), the sound's name, the SAVE page's slot, name and cursor */
#define NA_NG 20u
#define GI(id) na_g[(id) - NI_GBT]
static uint8_t na_g[NA_NG] = {0, 0, 0, 0, 1, 0, 3, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0};
static uint8_t na_bank = 1, na_sslot, na_scur, na_conf, na_gsel;
static char na_name[16], na_chr[11];
static const char NA_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
#define NA_NCH 38u
#define NA_USER 16u

static char na_hint[44];
static void na_str(char *d, const char *s);
static void na_say(const char *s) { na_str(na_hint, s); }

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
static void fa_seq_note(track_t *t, uint32_t note);                      /* (seq.c's hook of every key: the root of Random, REC) */
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

/* ---- MATRIX: the 4 x 8 grid (the test bed's without its PRESSURE row: the FM-1 has no pressure) on the firmware's four routing slots (mod.c). A route is one slot: its source is the grid's
 * row, its destination the column; with the four slots taken a new route is refused. An ASSIGN column is a destination of the list. */
static const uint8_t NA_MXSRC[4] = {MS_CYC, MS_ENV, MS_LFO, MS_KEY};
static const uint8_t NA_MXDST[4] = {MD_PITCH, MD_E1 + 2, MD_E1 + 3, MD_CUT};   /* (MD_E1 + n is P_En) */
static const uint8_t NA_ASGD[12] = {MD_E1 + 1, MD_E1 + 4, MD_E1 + 6, MD_E1 + 7, MD_RATE, MD_AMP, MD_PAN, MD_DIST, MD_CHO, MD_DLY, MD_REV, MD_VIB};
static uint8_t na_as[4] = {2, 4, 5, 0}, na_mxr, na_mxc;               /* the ASSIGN columns' destinations, the selected point */
static int8_t na_mxkeep[4][8];                                       /* a route switched off keeps its amount here */
static uint8_t na_mxoff[4];                                          /* bit c: the route is off */
static uint32_t na_mxdst(uint32_t c) { return c < 4u ? NA_MXDST[c] : NA_ASGD[na_as[c - 4u] % 12u]; }
static int32_t na_mxslot(uint32_t r, uint32_t c)                     /* the slot of the route, or -1 */
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        if (TSEL->p[P_M1SRC + 3u * k] == NA_MXSRC[r] && TSEL->p[P_M1DST + 3u * k] == (int16_t)na_mxdst(c))
            return (int32_t)k;
    return -1;
}
static int32_t na_mxamt(uint32_t r, uint32_t c)                       /* percent, -100..100 */
{
    int32_t k = na_mxslot(r, c);
    if (k < 0)
        return 0;
    if ((na_mxoff[r] >> c) & 1u)
        return na_mxkeep[r][c];
    return na_cl(TSEL->p[P_M1AMT + 3 * k] * 100 / 64, -100, 100);
}
static void na_mxset(uint32_t r, uint32_t c, int32_t pct, int32_t on)  /* 0 for the amount drops the route */
{
    track_t *t = TSEL;
    int32_t k = na_mxslot(r, c);
    pct = na_cl(pct, -100, 100);
    if (k < 0) {
        if (!pct)
            return;
        for (k = 0; k < 4; k++)
            if (!t->p[P_M1SRC + 3 * k] || !t->p[P_M1DST + 3 * k] || !t->p[P_M1AMT + 3 * k])
                break;
        if (k == 4) {
            na_say("MATRIX FULL: 4 ROUTES");
            return;
        }
        na_put(P_M1SRC + 3u * (uint32_t)k, NA_MXSRC[r]);
        na_put(P_M1DST + 3u * (uint32_t)k, (int32_t)na_mxdst(c));
    }
    na_mxoff[r] = (uint8_t)(on ? na_mxoff[r] & ~(1u << c) : na_mxoff[r] | (1u << c));
    na_mxkeep[r][c] = (int8_t)pct;
    if (!pct) {                                                      /* a route of nothing is a free slot */
        na_put(P_M1SRC + 3u * (uint32_t)k, 0);
        na_put(P_M1DST + 3u * (uint32_t)k, 0);
        na_mxoff[r] &= (uint8_t)~(1u << c);
    }
    na_put(P_M1AMT + 3u * (uint32_t)k, on ? pct * 64 / 100 : 0);
}
/* CYCLING MODE's DEST and DEPTH write the CycEnv row of the matrix (the test bed's cycRoute): the depth (50 % = none, 0..100 -> -100..+100 %) goes to the
 * chosen destination and the old one is cleared. The tracker is what the matrix was left at; a sound load re-reads it from the sound. */
static int8_t na_cyc_d = -1;
static int16_t na_cyc_a;
static void na_cyc_sync(track_t *t)
{
    na_cyc_d = (int8_t)clamp(t->p[P_FM4_SUS], 0, 7);
    na_cyc_a = (int16_t)((na_f100(t->p[P_FM4_DEC]) - 50) * 2);
}
static void na_cyc_route(void)
{
    track_t *t = TSEL;
    int32_t d = clamp(t->p[P_FM4_SUS], 0, 7), a = (na_f100(t->p[P_FM4_DEC]) - 50) * 2;
    if (na_cyc_d == d && na_cyc_a == a)
        return;
    if (na_cyc_d >= 0 && na_cyc_d != d)
        na_mxset(0, (uint32_t)na_cyc_d, 0, 0);
    if (!(na_cyc_d < 0 && a == 0))
        na_mxset(0, (uint32_t)d, a, a != 0);
    na_cyc_d = (int8_t)d;
    na_cyc_a = (int16_t)a;
}

/* ---- SEQ: the steps are the track's (nano_ctl.c); the cursor and the record flag are the screen's */
/* A / B: the pattern in the track is the one playing and edited; the other waits in the next track (its steps, and its Random and Mutate positions
 * P_FM2_DEC / P_FM2_SUS: the generators keep what they replaced in the steps themselves). Changing pattern swaps the two. Not saved with a sound. */
static uint8_t na_sqb;
static void na_sq_swap(void)
{
    track_t *a = TSEL, *b = &trk[((uint32_t)song.sel + 1u) % NTRK];
    uint32_t i;
    int16_t x;
    for (i = 0; i < NSTEP; i++) {
        step_t s = a->step[i];
        a->step[i] = b->step[i];
        b->step[i] = s;
    }
    x = a->p[P_FM2_DEC], a->p[P_FM2_DEC] = b->p[P_FM2_DEC], b->p[P_FM2_DEC] = x;
    x = a->p[P_FM2_SUS], a->p[P_FM2_SUS] = b->p[P_FM2_SUS], b->p[P_FM2_SUS] = x;
}
static void na_cyc_sync(track_t *t);
static void na_sq_b_reset(track_t *t)                    /* a sound came in: pattern A, and B starts as a copy of it */
{
    na_cyc_sync(t);
    track_t *b = &trk[((uint32_t)(t - trk) + 1u) % NTRK];
    uint32_t i;
    na_sqb = 0;
    for (i = 0; i < NSTEP; i++)
        b->step[i] = t->step[i];
    b->p[P_FM2_DEC] = b->p[P_FM2_SUS] = 0;
}
static void na_sq_reroll(void)                           /* the scale, the rests and the octaves redo a Random that is on */
{
    track_t *t = TSEL;
    if (t->p[P_FM2_DEC])
        nano_sq_random(t, (uint32_t)t->p[P_FM2_DEC], na_root);
}
static void na_sq_new(void)                              /* a step written by hand or recorded is a new base for Random and Mutate */
{
    TSEL->p[P_FM2_DEC] = 0;
    TSEL->p[P_FM2_SUS] = 0;
}
static const step_t *na_sq_step(void) { return &TSEL->step[na_cur % NSTEP]; }
/* a key: it is the root Random and Mutate build on; with REC armed in SEQ it is recorded: stopped, into the step under the cursor (which moves on),
 * playing, over the pitch of the step that sounds */
static void fa_seq_note(track_t *t, uint32_t note)
{
    if (note > 127u)
        return;
    na_root = (uint8_t)note;
    if (na_rec && na_seqmode && t == &trk[0]) {
        uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), i;
        if (song.playing) {
            step_t *s;
            i = t->seq_idx % len;
            s = &t->step[i];
            nano_sq_put(s, note, nano_sq_rest(s) ? NS_ENT(t) : (uint32_t)(s->acc & 3u));
        } else {
            i = na_cur % len;
            nano_sq_put(&t->step[i], note, NS_ENT(t));
            na_cur = (uint8_t)((i + 1u) % len);
        }
        na_sq_new();
        na_dirty = 1;
    }
}

/* the value of control id: 0..100, or the step */
/* the settings take effect (the MIDI ones are the firmware's: seq.c, nano_ctl.c, usb.c); id: the one that changed, or -1 */
static void na_g_save(void)
{
    uint8_t *f = favorites.factory[6];
    uint32_t i;
    f[0] = 0xB6;
    f[1] = 1;
    for (i = 0; i < NA_NG; i++)
        f[2u + i] = na_g[i];
    settings_later();                                      /* (written once the view is left or after a while: ui.c) */
}
static void na_g_apply(int32_t id)
{
    fm_seq_out = GI(NI_MMODE);
    fm_out_mask = GI(NI_MPORT);
    mch_set(0, GI(NI_MCH));                               /* the out channel: track 1's own (MIDI IN is the app's: fm_midi_ch) */
    song.g[G_ROUTE] = ROUTE_TRACK;
    fm_midi_ch = (uint8_t)(GI(NI_MINCH) + 1u);
    fm_midi_src = 3;
    fm_bt = GI(NI_GBT);                                   /* (no radio driver yet: nothing arrives from it) */
    song.g[G_CLOCK] = GI(NI_GCLK) ? 1 : 0;                /* MIDI clock: the USB one */
    fm_local = GI(NI_MLOCAL);
    fm_key_out = GI(NI_MKEY);
    fm_clk_out = GI(NI_MCLKO);
    fm_thru_in = (uint8_t)(GI(NI_MTHI) == 0u ? 2u : GI(NI_MTHI) == 1u ? 1u : 3u);   /* sources: 1 USB, 2 DIN (TRS), 3 BLE */
    fm_thru_out = GI(NI_MTHO);
    if (id >= 0 && id != NI_BDEV && id != NI_BACT)
        na_g_save();
}
static void na_g_load(void)
{
    const uint8_t *f = favorites.factory[6];
    static const uint8_t MX[NA_NG] = {1, 1, 1, 1, 2, 15, 7, 1, 1, 1, 2, 7, 15, 7, 3, 17, 1, 1, 1, 28};
    uint32_t i;
    if (f[0] == 0xB6 && f[1] == 1)
        for (i = 0; i < NA_NG; i++)
            na_g[i] = f[2u + i] <= MX[i] ? f[2u + i] : na_g[i];
    if (!GI(NI_SURADIO))
        GI(NI_GBT) = 0;                                    /* RADIO: OFF at power-on, or LAST */
    na_g_apply(-1);
}

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
    case NI_FMODEL: return p[P_FM4_REL] == 1;
    case NI_FDRIVE: return ((p[P_ED_FX] >> 1) & 7) * 100 / 7;
    case NI_FCLIP: return ((p[P_ED_FX] >> 4) & 7) * 100 / 7;
    case NI_RVBMIX: return na_f100(p[P_REV]);
    case NI_RVBSIZE: return na_f100(song.g[G_RSIZE]);
    case NI_RVBDAMP: return na_f100(song.g[G_RDAMP]);
    case NI_RVBPRE: return na_f100(p[P_LN5]);
    case NI_DLYTONE: return na_f100(song.g[G_DCOLOR]);
    case NI_DLYMIX: return na_f100(p[P_DLY]);
    case NI_DLYTIME: return na_f100(song.g[G_DRREV]);
    case NI_DLYFB: return na_cl(song.g[G_DFDBK], 0, 120) * 100 / 120;
    case NI_BENDRNG: return GI(NI_BENDRNG) ^ 12;               /* semitones 0..24 (stored xor 12: a settings record without it reads as 12) */
    case NI_BENDSPD: return na_f100(p[P_FM3_SUS]);
    case NI_SM1: case NI_SM2: case NI_SM3: case NI_SM4: return (p[P_FM3_REL] >> (id - NI_SM1)) & 1;   /* the four mod tracks' smoothing (4 bits of P_FM3_REL) */
    case NI_FOLD2: return na_f100(p[P_LN6]);
    case NI_LSYNC: return p[P_LSYNC] ? 1 : 0;
    case NI_CDEST: return na_cl(p[P_FM4_SUS], 0, 7);
    case NI_CDEPTH: return na_f100(p[P_FM4_DEC]);
    case NI_AMPMOD: return p[P_FM4_ATK] ? 0 : 1;                /* (stored inverted: 0 = ENVELOPE ON, the default) */
    case NI_GLIDE: return na_f100(p[P_GLIDE]);
    case NI_GLMODE: return p[P_GLMODE] == 1 ? 0 : p[P_GLMODE] == 2 ? 1 : 2;     /* the page's TIME SYNC RATE <- P_GLMODE's 1 2 0 */
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
    case NI_SQPAT: return na_sqb;
    case NI_SQRND: return na_cl(p[P_FM2_DEC], 0, 99);
    case NI_SQMUT: return na_cl(p[P_FM2_SUS], 0, 99);
    case NI_SQLEN: return na_cl(p[P_SLEN], 1, 64) - 1;
    case NI_SQSCALE: return NS_SCALE(t);
    case NI_SQOCT: return (int32_t)NS_RANGE(t) - 1;
    case NI_SQSWING: return na_cl(p[P_SSWING], 0, 100);
    case NI_SQSTEP: return na_cur;
    case NI_SQNOTE: return nano_sq_rest(na_sq_step()) ? 60 : na_sq_step()->note[0];
    case NI_SQDUR: return na_sq_step()->acc & 3;
    case NI_SQREST: return nano_sq_rest(na_sq_step());
    case NI_SQENT: return NS_ENT(t);
    case NI_SQRESTS: return NS_RESTS(t);
    case NI_SQGATE: return na_cl((p[P_SGATE] * 100 + 63) / 127 / 10 - 1, 0, 9);
    case NI_MXCOL: return na_mxc;
    case NI_MXROW: return na_mxr;
    case NI_MXON: return !((na_mxoff[na_mxr] >> na_mxc) & 1u);
    case NI_MXAMT: return na_mxamt(na_mxr, na_mxc);
    case NI_AS1: case NI_AS2: case NI_AS3: case NI_AS4: return na_as[id - NI_AS1];
    case NI_PBANK: return na_bank;
    case NI_PSLOT: return na_slot;
    case NI_SSLOT: return na_sslot;
    case NI_PCUR: return na_scur;
    case NI_PCHR: {
        uint32_t c;
        for (c = 0; c < NA_NCH; c++)
            if (NA_CHARS[c] == na_chr[na_scur])
                return (int32_t)c;
        return 0;
    }
    default:
        return id >= NI_GBT && id <= NI_SURADIO ? GI(id) : 0;
    }
}
/* set control id to v (0..100 or the step) */
static void na_set(int32_t id, int32_t v)
{
    track_t *t = TSEL;
    const nk_t *k = &NK[id];
    v = id == NI_MXAMT ? na_cl(v, -100, 100) : na_cl(v, 0, k->n ? (int32_t)k->n - 1 : 100);
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
    case NI_FMODEL: na_put(P_FM4_REL, v); break;
    case NI_FDRIVE: na_put(P_ED_FX, (t->p[P_ED_FX] & ~(7 << 1)) | (((v * 7 + 50) / 100) << 1)); break;
    case NI_FCLIP: na_put(P_ED_FX, (t->p[P_ED_FX] & ~(7 << 4)) | (((v * 7 + 50) / 100) << 4)); break;
    case NI_RVBMIX: na_put(P_REV, na_f127(v)); break;
    case NI_RVBSIZE: song.g[G_RSIZE] = (int16_t)na_f127(v); break;
    case NI_RVBDAMP: song.g[G_RDAMP] = (int16_t)na_f127(v); break;
    case NI_RVBPRE: na_put(P_LN5, na_f127(v)); break;
    case NI_DLYTONE: song.g[G_DCOLOR] = (int16_t)na_f127(v); break;
    case NI_DLYMIX: na_put(P_DLY, na_f127(v)); break;
    case NI_DLYTIME: song.g[G_DRREV] = (int16_t)na_f127(v); break;
    case NI_DLYFB: song.g[G_DFDBK] = (int16_t)(v * 120 / 100); break;
    case NI_BENDRNG: GI(NI_BENDRNG) = (uint8_t)(v ^ 12); na_g_save(); break;
    case NI_BENDSPD: na_put(P_FM3_SUS, na_f127(v)); break;
    case NI_SM1: case NI_SM2: case NI_SM3: case NI_SM4: na_put(P_FM3_REL, (t->p[P_FM3_REL] & ~(1 << (id - NI_SM1))) | (v << (id - NI_SM1))); break;
    case NI_FOLD2: na_put(P_LN6, na_f127(v)); break;
    case NI_LSYNC: na_put(P_LSYNC, v ? 1 : 0); break;
    case NI_CDEST: na_put(P_FM4_SUS, v); na_cyc_route(); break;
    case NI_CDEPTH: na_put(P_FM4_DEC, na_f127(v)); na_cyc_route(); break;
    case NI_AMPMOD: na_put(P_FM4_ATK, v ? 0 : 1); break;
    case NI_GLIDE: na_put(P_GLIDE, na_f127(v)); break;
    case NI_GLMODE: na_put(P_GLMODE, v == 0 ? 1 : v == 1 ? 2 : 0); break;
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
    case NI_SQPAT:
        if (v != na_sqb) {
            na_sq_swap();
            na_sqb = (uint8_t)v;
        }
        break;
    case NI_SQRND: nano_sq_random(t, (uint32_t)v, na_root); break;
    case NI_SQMUT: nano_sq_mutate(t, (uint32_t)v, na_root); break;
    case NI_SQLEN: na_put(P_SLEN, v + 1); break;
    case NI_SQSCALE: na_put(P_FM2_REL, v); na_sq_reroll(); break;
    case NI_SQOCT: na_put(P_FM2_LEVEL, v); na_sq_reroll(); break;
    case NI_SQSWING: na_put(P_SSWING, v); break;
    case NI_SQSTEP: na_cur = (uint8_t)v; break;
    case NI_SQNOTE: {
        step_t *s = &t->step[na_cur % NSTEP];
        nano_sq_put(s, (uint32_t)v, s->acc & 3u);
        na_sq_new();
        break;
    }
    case NI_SQDUR: {
        step_t *s = &t->step[na_cur % NSTEP];
        s->acc = (uint8_t)((s->acc & ~3u) | (uint32_t)v);
        na_sq_new();
        break;
    }
    case NI_SQREST: {
        step_t *s = &t->step[na_cur % NSTEP];
        nano_sq_put(s, v ? NS_REST : 60u, s->acc & 3u);
        na_sq_new();
        break;
    }
    case NI_SQENT: na_put(P_FM3_DEC, v); break;
    case NI_SQRESTS: na_put(P_FM3_ATK, v); na_sq_reroll(); break;
    case NI_SQGATE: na_put(P_SGATE, (v + 1) * 127 / 10); break;
    case NI_SSLOT: na_sslot = (uint8_t)v; na_conf = 0; break;
    case NI_PCUR: na_scur = (uint8_t)v; na_conf = 0; break;
    case NI_PCHR: na_chr[na_scur] = NA_CHARS[v]; na_conf = 0; break;
    case NI_GBT: case NI_GBTA: case NI_GROLE: case NI_GCLK: case NI_MMODE: case NI_MCH: case NI_MPORT: case NI_MKEY: case NI_MLOCAL: case NI_MCLKO:
    case NI_MTHI: case NI_MTHO: case NI_MINCH: case NI_BDEV: case NI_BACT: case NI_SUPATCH: case NI_SUBOOT: case NI_SUPAGE: case NI_SURADIO:
        GI(id) = (uint8_t)v;
        na_gsel = (uint8_t)(id - NI_SUPATCH);
        na_g_apply(id);
        break;
    case NI_MXCOL: na_mxc = (uint8_t)v; break;
    case NI_MXROW: na_mxr = (uint8_t)v; break;
    case NI_MXON: na_mxset(na_mxr, na_mxc, na_mxamt(na_mxr, na_mxc), v); break;
    case NI_MXAMT: na_mxset(na_mxr, na_mxc, v, !((na_mxoff[na_mxr] >> na_mxc) & 1u)); break;
    case NI_AS1: case NI_AS2: case NI_AS3: case NI_AS4: {          /* the routes of that column follow */
        uint32_t c = (uint32_t)(id - NI_AS1) + 4u, r;
        int32_t amt[4], on[4];
        for (r = 0; r < 4u; r++) {
            amt[r] = na_mxamt(r, c);
            on[r] = !((na_mxoff[r] >> c) & 1u);
            na_mxset(r, c, 0, 0);
        }
        na_as[c - 4u] = (uint8_t)v;
        for (r = 0; r < 4u; r++)
            if (amt[r])
                na_mxset(r, c, amt[r], on[r]);
        break;
    }
    default: break;
    }
}
static void na_load(uint32_t n);
static void na_g_apply(int32_t id);
static uint32_t na_pre_n(void);

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

/* the text of control id's value */
static void na_text(int32_t id, char *out)
{
    const nk_t *k = &NK[id];
    int32_t v = na_get(id);
    out[0] = 0;
    if (k->tx) {
        na_str(out, k->tx[v]);
    } else if (id == NI_MCH || id == NI_MINCH) {
        na_str(out, "CH ");
        fmt_int(out + 3, v + 1);
    } else if (id == NI_PSLOT || id == NI_SSLOT) {
        if (v + 1 < 10)
            na_str(out, "0");
        fmt_int(out + str_len(out), v + 1);
    } else if (id == NI_PCUR) {
        fmt_int(out, v + 1);
    } else if (id == NI_PCHR) {
        out[0] = NA_CHARS[v] == ' ' ? '_' : NA_CHARS[v];
        out[1] = 0;
    } else if (id == NI_BDEV) {
        na_str(out, "-");
    } else if (id == NI_SUPATCH) {
        if (v == 0)
            na_str(out, "INIT");
        else if (v == 1)
            na_str(out, "LAST");
        else {
            na_str(out, "USER ");
            fmt_int(out + 5, v - 1);
        }
    } else if (id == NI_BENDRNG) {
        fmt_int(out, v * 2);
        na_cat(out, " STEPS");
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
    } else if (id == NI_MXAMT) {
        fmt_int(out, v < 0 ? -v : v);
        na_str(out + str_len(out), "%");
        if (v >= 0)
            na_cat(out, "");
        else {
            uint32_t n = str_len(out), i;
            for (i = n + 1u; i > 0u; i--)
                out[i] = out[i - 1u];
            out[0] = '-';
        }
    } else if (id == NI_SQRND || id == NI_SQMUT) {
        if (!v)
            na_str(out, "OFF");
        else {
            na_str(out, id == NI_SQRND ? "ROLL " : "MUT ");
            fmt_int(out + str_len(out), v);
        }
    } else if (id == NI_SQLEN || id == NI_SQSTEP) {
        fmt_int(out, v + 1);
    } else if (id == NI_SQOCT) {
        fmt_int(out, v + 1);
        na_cat(out, " OCT");
    } else if (id == NI_SQSWING) {
        fmt_int(out, 50 + v / 4);
        na_cat(out, "%");
    } else if (id == NI_SQRESTS) {
        fmt_int(out, v * 5);
        na_cat(out, "%");
    } else if (id == NI_SQGATE) {
        fmt_int(out, (v + 1) * 10);
        na_cat(out, "%");
    } else if (id == NI_SQNOTE) {
        static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        na_str(out, NN[v % 12]);
        fmt_int(out + str_len(out), v / 12 - 1);
    } else if (id == NI_PSLOT) {
        fmt_int(out, v + 1);
    } else {
        fmt_int(out, v);
        na_cat(out, "%");
    }
}


/* ---------------------------------------------------------- the waveform preview --- */
/* The oscillator page's picture: two cycles of the sound, drawn from the type and its three knobs. It is a sketch in integer
 * maths (the shape each type makes), not the engine's output: the test bed's page renders the real engine into it; here
 * the audio ISR owns the voices and a render from the screen thread would race it. WAVETABLE is the real table. */
#define NW_LEN 120
static int32_t nw_saw(uint32_t ph) { return (int32_t)ph >> 16; }
static int32_t nw_tri(uint32_t ph) { int32_t x = (int32_t)(ph >> 16) - 32768; x = x < 0 ? -x : x; return (x - 16384) * 2; }
static int32_t nw_sqr(uint32_t ph) { return ph < 0x80000000u ? 28000 : -28000; }
static int32_t nw_fold(int32_t x)                          /* a triangle fold of x (the range 32768 is the fold point) */
{
    x = (x + 32768) & 0x3FFFF;
    x = x > 0x1FFFF ? 0x3FFFF - x : x;
    return x * 2 - 0x1FFFF + 0;                            /* (a fold back and forth) */
}
static int32_t na_wave_at(uint32_t ty, uint32_t ph, uint32_t i, const int16_t *p)
{
    int32_t a = p[P_E1], b = p[P_E2], c = p[P_E3], x, y, k;
    switch (ty) {
    case 0: {                                              /* BASICWAVES: MORPH sine > tri > saw > square, SYM skews, SUB a square an octave down */
        uint32_t u = ph >> 16, d = (uint32_t)(32768 + (b - 64) * 240);
        int32_t m = a * 3, f = m & 127, w0, w1;
        u = u < d ? u * 32768u / d : 32768u + (u - d) * 32768u / (65536u - d);
        ph = u << 16;
        w0 = m < 128 ? nano_sin824(ph) : m < 256 ? nw_tri(ph) : nw_saw(ph);
        w1 = m < 128 ? nw_tri(ph) : m < 256 ? nw_saw(ph) : nw_sqr(ph);
        if (m >= 381) f = 127, w0 = w1;
        y = w0 + (((w1 - w0) * f) >> 7);
        return y + (nw_sqr(ph >> 1 | (i >= NW_LEN / 2u ? 0x80000000u : 0u)) * c >> 8);
    }
    case 7: case 13: case 11: case 10:                    /* SUPERWAVE, V.ANALOG, SAW, BASS: saws a little apart */
        k = ty == 7 ? b / 6 : ty == 13 ? a / 6 : 4 + c / 20;
        y = nw_saw(ph) + nw_saw(ph + (uint32_t)(ph / 1000u) * (uint32_t)k) + nw_saw(ph - (uint32_t)(ph / 1000u) * (uint32_t)k);
        return ty == 13 ? y / 3 + (nw_sqr(ph) * c >> 8) : ty == 10 ? y / 3 + (nw_sqr(ph >> 1) >> 1) : y / 3;
    case 12: {                                             /* WAVETABLE: the table E1 and the wave E2 of it, as the engine reads them */
        const int8_t *w = NANO_WT + (uint32_t)(a * 16 / 128) * 16u * 129u + (uint32_t)(b * 15 / 127) * 129u;
        return nano_wt_rd(w, ph) * 4;
    }
    case 1:                                                /* TWO OP FM: the ratio E1 (1..8), the amount E2 */
        x = 1 + a * 7 / 127;
        return nano_sin824(ph + (uint32_t)nano_sin824(ph * (uint32_t)x) * (uint32_t)(b * 20861 / 20) * 1u);
    case 2:                                                /* WAVESHAPER: a wave pushed into a fold, AMOUNT E2, ASYM E3 */
        x = a < 64 ? nano_sin824(ph) : nw_tri(ph);
        return nw_fold(x * (1 + b / 24) + (c - 64) * 300);
    case 3:                                                /* FORMANT: a sine of the formant's pitch under a window */
        x = nw_saw(ph) + 32768;
        y = (65535 - x) >> 1;
        return (nano_sin824(ph * (uint32_t)(2 + b / 16)) >> 1) * (y >> 7) >> 8;
    case 5: case 6: case 8:                                /* CHORDS, MODAL, HARMONIC: harmonics, the knob moving the weight up */
        y = 0;
        for (k = 1; k <= 8; k++) {
            int32_t wgt = 1000 / k + (a - 64) * (k - 4) * 3 + (ty == 8 ? 0 : (k & 1) * (c - 64));
            y += (nano_sin824(ph * (uint32_t)k) >> 4) * (wgt < 0 ? 0 : wgt) >> 7;
        }
        return y;
    case 4: {                                              /* NOISE */
        static uint32_t r;
        r = r * 1664525u + 1013904223u + i;
        return (int32_t)(r >> 16) - 32768;
    }
    default:                                               /* KARPLUS, STRINGS: a saw dying away */
        return (nw_saw(ph) * (NW_LEN - (int32_t)i)) / NW_LEN;
    }
}
static void na_wave_sketch(void)
{
    int8_t *w = nf_ui_wave();
    int32_t v[NW_LEN], pk = 1, i;
    const track_t *t = TSEL;
    uint32_t ty = NOSC[na_type()].ty;
    for (i = 0; i < NW_LEN; i++) {
        v[i] = na_wave_at(ty == 14u ? 9u : ty, (uint32_t)i * (uint32_t)(0xFFFFFFFFu / (NW_LEN / 2)), (uint32_t)i, t->p);
        if (ty != 14u && t->p[P_E4] > 0)                   /* FOLD */
            v[i] = nw_fold(v[i] * (1 + t->p[P_E4] / 20)) >> 0;
        pk = v[i] > pk ? v[i] : -v[i] > pk ? -v[i] : pk;
    }
    for (i = 0; i < NW_LEN; i++)
        w[i] = (int8_t)(v[i] * 127 / pk);
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
    na_str(nf_ui_str(UIS_PRESET), na_name);
    nf_ui_set(UI_OCT, song.octave);
    nf_ui_set(UI_POLY, t->p[P_VOICE] == V_MONO ? 0 : t->p[P_VOICE] == V_UNISON ? 1 : t->p[P_VOICE] == V_POLY ? 3 : 0);
    nf_ui_set(UI_PLAY, na_seqmode ? song.playing != 0u : na_arp_on());
    nf_ui_set(UI_TYPE, (int)ty);
    nf_ui_set(UI_NTYPES, 15);
    nf_ui_set(UI_STEP, -1);
    nf_ui_set(UI_P, 0);
    if (na_grp == NG_OSC)
        na_wave_sketch();
    switch (na_grp) {
    case NG_ENV:
        nf_ui_set(UI_P, na_get(NI_ATTACK)), nf_ui_set(UI_P + 1, na_get(NI_DECAY)), nf_ui_set(UI_P + 2, na_get(NI_SUSTAIN));
        if (sp->ui == 1u)
            nf_ui_set(UI_P, na_get(NI_RISE)), nf_ui_set(UI_P + 1, na_get(NI_FALL)), nf_ui_set(UI_P + 2, na_get(NI_HOLD));
        nf_ui_set(UI_P + 3, sp->ui == 0u ? 1 : na_get(NI_CMODE));      /* (the envelope page: the amp envelope on) */
        if (sp->ui == 3u)
            nf_ui_set(UI_P, na_get(NI_AMPMOD));
        break;
    case NG_FX:
        if (sp->ui == 0u)
            nf_ui_set(UI_P, na_get(NI_CUTOFF)), nf_ui_set(UI_P + 1, na_get(NI_RESO)), nf_ui_set(UI_P + 2, na_get(NI_FTYPE)), nf_ui_set(UI_P + 3, na_get(NI_FSLOPE));
        else if (sp->ui == 1u)
            nf_ui_set(UI_P, na_get(NI_FSLOPE)), nf_ui_set(UI_P + 1, na_get(NI_FDRIVE)), nf_ui_set(UI_P + 2, na_get(NI_FCLIP)), nf_ui_set(UI_P + 3, na_get(NI_FMODEL));
        else if (sp->ui == 2u)
            nf_ui_set(UI_P, na_get(NI_RVBMIX)), nf_ui_set(UI_P + 1, na_get(NI_RVBSIZE)), nf_ui_set(UI_P + 2, na_get(NI_RVBDAMP)), nf_ui_set(UI_P + 3, na_get(NI_RVBPRE));
        else if (sp->ui == 3u)
            nf_ui_set(UI_P, na_get(NI_DLYMIX)), nf_ui_set(UI_P + 1, na_get(NI_DLYTIME)), nf_ui_set(UI_P + 2, na_get(NI_DLYFB)), nf_ui_set(UI_P + 3, na_get(NI_DLYTONE));
        else if (sp->ui == 5u)
            nf_ui_set(UI_P, na_get(NI_BENDRNG) * 2), nf_ui_set(UI_P + 1, na_get(NI_BENDSPD));
        else if (sp->ui == 4u)
            nf_ui_set(UI_P, na_get(NI_GLIDE)), nf_ui_set(UI_P + 1, na_get(NI_GLMODE));
        else
            nf_ui_set(UI_P, na_get(NI_IMPRESS)), nf_ui_set(UI_P + 1, na_get(NI_CHANCE));
        break;
    case NG_LFO:
        nf_ui_set(UI_P, na_get(NI_LSHAPE)), nf_ui_set(UI_P + 1, na_get(NI_LRATE)), nf_ui_set(UI_P + 2, na_get(NI_LSYNC)), nf_ui_set(UI_P + 3, na_get(NI_LMORPH));
        break;
    case NG_VOICE:
        nf_ui_set(UI_P, na_get(NI_UDETUNE));
        break;
    case NG_ARP:
        nf_ui_set(UI_P, na_pat), nf_ui_set(UI_P + 1, na_get(NI_ARPRANGE) + 1), nf_ui_set(UI_P + 2, 0), nf_ui_set(UI_P + 3, na_get(NI_ARPLEN) + 1);
        nf_ui_set(UI_P + 4, na_get(NI_SWING)), nf_ui_set(UI_P + 5, na_get(NI_ARPLATCH)), nf_ui_set(UI_P + 6, na_get(NI_ARPTYPE)), nf_ui_set(UI_P + 7, na_get(NI_ARPCHORD));
        break;
    case NG_SEQ: {
        uint32_t k;
        nf_ui_set(UI_P, na_get(NI_SQPAT)), nf_ui_set(UI_P + 1, na_get(NI_SQRND)), nf_ui_set(UI_P + 2, na_get(NI_SQLEN) + 1), nf_ui_set(UI_P + 3, na_get(NI_SQENT));
        nf_ui_set(UI_P + 8, (na_get(NI_SQGATE) + 1) * 10), nf_ui_set(UI_P + 10, na_get(NI_SQSCALE)), nf_ui_set(UI_P + 11, na_get(NI_SQRESTS) * 5);
        nf_ui_set(UI_P + 12, na_get(NI_SQOCT) + 1), nf_ui_set(UI_P + 13, na_get(NI_SQMUT));
        for (k = 0; k < NSTEP; k++)
            nf_ui_set(UI_SEQ + (int)k, (int)(nano_sq_note(&t->step[k]) | (uint32_t)(t->step[k].acc & 3u) << 8));
        nf_ui_set(UI_P + 4, na_get(NI_SM1)), nf_ui_set(UI_P + 5, na_get(NI_SM2)), nf_ui_set(UI_P + 6, na_get(NI_SM3)), nf_ui_set(UI_P + 7, na_get(NI_SM4));
        nf_ui_set(UI_SEQCUR, na_cur), nf_ui_set(UI_SEQREC, na_rec);
        nf_ui_set(UI_STEP, song.playing && na_seqmode ? (int)(t->seq_idx % (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP)) : -1);
        break;
    }
    case NG_MATRIX: {
        uint32_t r, c;
        for (r = 0; r < 4u; r++)
            for (c = 0; c < 8u; c++) {
                int32_t m = na_mxamt(r, c);
                nf_ui_set(UI_MX + (int)(r * 8u + c), (na_mxoff[r] >> c) & 1u ? (m >= 0 ? m + 1000 : m - 1000) : m);
            }
        nf_ui_set(UI_SEL_ROW, na_mxr), nf_ui_set(UI_SEL_COL, na_mxc);
        if (sp->ui == 0u) {
            char *h = nf_ui_str(UIS_L0);
            na_str(h, NT_MXROW[na_mxr]);
            na_cat(h, " > ");
            na_cat(h, NT_MXCOL[na_mxc]);
            na_cat(h, " ");
            na_text(NI_MXAMT, b);
            na_cat(h, b);
            if ((na_mxoff[na_mxr] >> na_mxc) & 1u)
                na_cat(h, " OFF");
        }
        break;
    }
    case NG_GLOBAL:
        if (sp->ui == 0u)
            nf_ui_set(UI_P, GI(NI_GBT)), nf_ui_set(UI_P + 1, GI(NI_GBTA)), nf_ui_set(UI_P + 2, GI(NI_GROLE)), nf_ui_set(UI_P + 3, GI(NI_GCLK));
        else if (sp->ui == 1u)
            nf_ui_set(UI_P, GI(NI_MMODE)), nf_ui_set(UI_P + 1, GI(NI_MCH)), nf_ui_set(UI_P + 2, GI(NI_MPORT)), nf_ui_set(UI_P + 3, GI(NI_MKEY));
        else if (sp->ui == 2u)
            nf_ui_set(UI_P, GI(NI_MLOCAL)), nf_ui_set(UI_P + 1, GI(NI_MCLKO)), nf_ui_set(UI_P + 2, GI(NI_MTHI)), nf_ui_set(UI_P + 3, GI(NI_MTHO));
        else if (sp->ui == 3u)
            nf_ui_set(UI_P, GI(NI_MINCH)), nf_ui_set(UI_P + 1, GI(NI_GCLK));
        else if (sp->ui == 4u) {                            /* no radio driver in this firmware: the list says so */
            na_str(nf_ui_str(UIS_ROW0), "*NO BLUETOOTH RADIO DRIVER");
            na_str(nf_ui_str(UIS_ROW0 + 1), "*YET IN THIS FIRMWARE");
            na_str(nf_ui_str(UIS_ROW0 + 2), NT_BACT[GI(NI_BACT)]);
            nf_ui_set(UI_P, 2), nf_ui_set(UI_P + 1, 3);
        } else {
            char *r;
            r = nf_ui_str(UIS_ROW0), na_str(r, "POWER-ON  "), na_text(NI_SUPATCH, r + 10);
            r = nf_ui_str(UIS_ROW0 + 1), na_str(r, "BOOT SCREEN  "), na_cat(r, NT_ONOFF[GI(NI_SUBOOT)]);
            r = nf_ui_str(UIS_ROW0 + 2), na_str(r, "START PAGE  "), na_cat(r, NT_SUPAGE[GI(NI_SUPAGE)]);
            r = nf_ui_str(UIS_ROW0 + 3), na_str(r, "RADIO  "), na_cat(r, NT_SURAD[GI(NI_SURADIO)]);
            na_str(nf_ui_str(UIS_ROW0 + 4), "*SAVED AUTOMATICALLY");
            nf_ui_set(UI_P, na_gsel < 4u ? na_gsel : 0), nf_ui_set(UI_P + 1, 5);
        }
        break;
    case NG_PRESET:
        if (sp->ui == 1u) {                                  /* SAVE: the slot, the name, the cursor under it */
            char *r = nf_ui_str(UIS_ROW0);
            uint32_t c;
            na_str(r, "SLOT "), na_text(NI_SSLOT, r + 5);
            if (up_used(na_sslot)) {
                char nm[16];
                up_name(na_sslot, nm);
                nm[8] = 0;
                na_cat(r, "  REPLACES "), na_cat(r, nm);
            } else
                na_cat(r, "  EMPTY");
            r = nf_ui_str(UIS_ROW0 + 1), na_str(r, "NAME "), na_cat(r, na_chr);
            r = nf_ui_str(UIS_ROW0 + 2), na_str(r, "     ");
            for (c = 0; c < na_scur; c++)
                na_cat(r, " ");
            na_cat(r, "^");
            na_str(nf_ui_str(UIS_ROW0 + 3), na_conf ? "SAVE YES / HOME NO" : "SAVE TO CONFIRM");
            nf_ui_set(UI_P, 1), nf_ui_set(UI_P + 1, 4);
        } else {                                             /* BROWSE: six rows round the preset shown */
            uint32_t n = na_pre_n(), first = na_slot < 3u ? 0u : na_slot + 3u >= n ? (n > 6u ? n - 6u : 0u) : na_slot - 3u, rows = n < 6u ? n : 6u;
            for (i = 0; i < rows; i++) {
                char *r = nf_ui_str(UIS_ROW0 + (int)i), nm[16];
                uint32_t k = first + i;
                if (na_bank)
                    na_str(nm, ENGINES[ENGI_NANO]->presets[k].name);
                else if (up_used(k))
                    up_name(k, nm);
                else
                    na_str(nm, "EMPTY");
                r[0] = 0;
                if (!na_bank && !up_used(k))
                    na_cat(r, "*");
                if (k + 1u < 10u)
                    na_cat(r, "0");
                fmt_int(b, (int32_t)(k + 1u));
                na_cat(r, b);
                na_cat(r, " ");
                na_cat(r, nm);
            }
            nf_ui_set(UI_P, (int)(na_slot - first));
            nf_ui_set(UI_P + 1, (int)rows);
        }
        break;
    default:
        break;
    }
    na_dirty = 0;
}

/* ------------------------------------------------------------- input --- */
static uint32_t na_pre_n(void) { return na_bank ? na_npre() : NA_USER; }
static void na_pat_from_amode(void)                       /* the arp pattern the sound's P_AMODE says */
{
    uint32_t i;
    na_pat = 0;
    for (i = 0; i < 14u; i++)
        if (na_arp_on() && NANO_APAT[i] == TSEL->p[P_AMODE])
            na_pat = (uint8_t)i;
}
/* the generators' own values (nano_ctl.c): a sound load must not leave Random / Mutate "on" with no copy of the pattern they replaced
 * (the track defaults put 127 in P_FM2_SUS, so the first turn of MUTATE to 0 wrote an empty copy over the sequence). The test bed's defaults:
 * SCALE 0, OCTAVES 2, RESTS 20 %, REC LENGTH a quaver */
static void na_sq_defaults(track_t *t)
{
    t->p[P_FM2_DEC] = t->p[P_FM2_SUS] = 0;
    t->p[P_FM2_REL] = 0;
    t->p[P_FM2_LEVEL] = 1;
    t->p[P_FM3_ATK] = 4;
    t->p[P_FM3_DEC] = 2;
}
static void na_load(uint32_t n)                           /* factory preset n: the sound, the arp and the performance values it starts with */
{
    track_t *t = TSEL;
    n %= na_npre();
    na_slot = (uint8_t)n;
    na_bank = 1;
    apply_preset_to(t, n);
    na_sq_defaults(t);
    na_sq_b_reset(t);
    na_str(na_name, ENGINES[ENGI_NANO]->presets[n].name);
    na_pat = 0;
    na_arp_pat_sync();
    na_g_apply(-1);
    na_ost = -1;
    na_dirty = 1;
    ui.force = 1;
}
static void na_load_user(uint32_t k)                      /* user slot k (an empty one is only said) */
{
    na_slot = (uint8_t)k;
    na_bank = 0;
    na_dirty = 1;
    if (!up_used(k)) {
        na_say("EMPTY SLOT");
        return;
    }
    up_load(k);
    TSEL->p[P_FM2_DEC] = TSEL->p[P_FM2_SUS] = 0;              /* (no copy of a pattern to put back) */
    na_sq_b_reset(TSEL);
    up_name(k, na_name);
    na_pat_from_amode();
    na_g_apply(-1);
    na_ost = -1;
    ui.force = 1;
}
static void na_pre_go(uint32_t n)                         /* the preset n of the bank on show */
{
    if (na_bank)
        na_load(n);
    else
        na_load_user(n % NA_USER);
}
static void na_pre_step(int32_t s)
{
    uint32_t n = na_pre_n();
    na_pre_go((na_slot + n + (s > 0 ? 1u : n - 1u)) % n);
}
static void na_save_start(void)                           /* the SAVE page: this sound's name, the first free slot */
{
    uint32_t i, k;
    for (i = 0; i < 10u; i++)
        na_chr[i] = ' ';
    na_chr[10] = 0;
    for (i = 0; i < 10u && na_name[i]; i++) {
        char c = na_name[i];
        uint32_t j, ok = 0;
        for (j = 0; j < NA_NCH; j++)
            ok |= NA_CHARS[j] == c;
        na_chr[i] = ok ? c : ' ';
    }
    na_scur = 0;
    na_conf = 0;
    na_sslot = na_bank ? 0 : na_slot;
    for (k = 0; k < NA_USER; k++)
        if (!up_used(k)) {
            na_sslot = (uint8_t)k;
            break;
        }
}
static void na_do_save(void)
{
    char nm[16];
    uint32_t i, n = 10;
    int rc;
    while (n && na_chr[n - 1u] == ' ')
        n--;
    for (i = 0; i < n; i++)
        nm[i] = na_chr[i];
    nm[n] = 0;
    if (!n) {
        na_str(nm, "USER ");
        if (na_sslot + 1u < 10u)
            na_cat(nm, "0");
        fmt_int(nm + str_len(nm), na_sslot + 1);
    }
    na_conf = 0;
    rc = up_store(na_sslot, nm);
    if (rc == 0 || rc == 3) {
        na_str(na_name, nm);
        na_bank = 0;
        na_slot = na_sslot;
        na_sub[NG_PRESET] = 0;
        na_say("SAVED ");
        if (na_sslot + 1u < 10u)
            na_cat(na_hint, "0");
        fmt_int(na_hint + str_len(na_hint), na_sslot + 1);
    } else
        na_say(rc == 2 ? "STOP TO SAVE" : "NOT SAVED");
    na_dirty = 1;
}
static void na_save_key(void)                             /* SAVE: open the page; on it, ask, then save */
{
    if (na_grp == NG_PRESET && na_cursub() == 1u) {
        if (!na_conf) {
            na_conf = 1;
            na_say("SAVE YES / HOME NO");
        } else
            na_do_save();
    } else {
        na_grp = NG_PRESET;
        na_sub[NG_PRESET] = 1;
        na_save_start();
        na_say("PICK A SLOT AND NAME, THEN SAVE");
    }
    na_dirty = 1;
}
/* a knob's speed-dependent steps: a slow turn is 2 values a detent (a 0..127 knob crosses its range in ~64 detents), a faster one
 * 3, 5, 8, 12, 18 by the time per detent, always in the turn's direction; a reversal or a pause starts again slow */
static uint32_t na_tl[16];
static int32_t na_acc(uint32_t role, int32_t s, int32_t range)
{
    uint32_t now = fm1_ms, dt, a = (uint32_t)(s < 0 ? -s : s), last = na_tl[role & 15u];
    int32_t m;
    if (range <= 32 || !s)
        return s;
    dt = (now - (last & 0x7FFFFFFFu)) & 0x7FFFFFFFu;
    na_tl[role & 15u] = (now & 0x7FFFFFFFu) | (s > 0 ? 0x80000000u : 0u);
    if (!last || ((last >> 31) != (s > 0 ? 1u : 0u)) || dt > 150u)
        return s * 2;
    dt /= a;
    m = dt >= 80u ? 2 : dt >= 50u ? 3 : dt >= 30u ? 5 : dt >= 18u ? 8 : dt >= 9u ? 12 : 18;
    return s * m;
}

static void na_knob(uint32_t k, int32_t s)
{
    const nsub_t *sp = &NGR[na_grp].sub[na_cursub()];
    int32_t id = sp->k[k], v;
    if (id == NI_NONE)
        return;
    if (na_grp == NG_PRESET && sp->ui == 0u) {                /* BROWSE: K1 the bank, K2 the preset (it loads) */
        if (k == 0u) {
            na_bank = (uint8_t)(s > 0 ? 1 : 0);
            na_slot = na_slot < na_pre_n() ? na_slot : 0;
            na_say(NT_BANK[na_bank]);
            na_dirty = 1;
        } else if (k == 1u)
            na_pre_step(s);
        return;
    }
    v = na_get(id) + (id == NI_MXAMT ? na_acc(EN_K1 + k, s, 100) : (NK[id].n ? (NK[id].n > 16 ? na_acc(EN_K1 + k, s, NK[id].n - 1) : s > 0 ? 1 : -1) : id == NI_FDRIVE || id == NI_FCLIP ? (s > 0 ? 15 : -15) : na_acc(EN_K1 + k, s, 100)));   /* (DRIVE, CLIP: seven steps) */
    na_set(id, v);
    na_say(NK[id].lb);
    na_cat(na_hint, " ");
    na_text(id, na_hint + str_len(na_hint));
    na_dirty = 1;
}
static void na_group(uint32_t g)
{
    na_grp = (uint8_t)(na_grp == g ? NG_OSC : g);
    na_conf = 0;
    if (GI(NI_SUPAGE) && na_grp < NG_GLOBAL && favorites.factory[6][24] != na_grp) {   /* START PAGE: LAST */
        favorites.factory[6][24] = na_grp;
        settings_later();
    }
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
            na_group(TO[i]);
        }
    if ((pressed >> panel.btn[B_HOME]) & 1u) {
        na_grp = NG_OSC;
        na_conf = 0;
        na_dirty = 1;
    }
    if ((pressed >> panel.btn[B_SAVE]) & 1u)
        na_save_key();
    if ((pressed >> panel.btn[B_ARP]) & 1u) {                 /* ARP and SEQ are the two ways the keys play a pattern: one at a time */
        na_seqmode = 0;
        if (song.playing)
            transport_req = 2;
    }
    if ((pressed >> panel.btn[B_SEQ]) & 1u) {
        na_seqmode = 1;
        na_put(P_AMODE, 0);
    }
    if ((pressed >> panel.btn[B_REC]) & 1u) {
        na_rec = !na_rec;
        if (na_rec && !na_seqmode) {
            na_seqmode = 1;
            na_put(P_AMODE, 0);
            na_grp = NG_SEQ;
        }
        na_say(na_rec ? "REC ON: PLAY KEYS" : "REC OFF");
        na_dirty = 1;
    }
    if ((pressed >> panel.btn[B_PLAY]) & 1u) {
        if (na_seqmode) {
            transport_req = (song.playing || transport_req == 1u) ? 2u : 1u;
            na_say(transport_req == 1u ? "SEQ PLAY" : "SEQ STOP");
        } else {
            na_put(P_AMODE, na_arp_on() ? 0 : NANO_APAT[na_pat]);
            na_say(na_arp_on() ? "ARP ON" : "ARP OFF");
        }
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

/* ---- the touch strip's three keys (the FM-1's last three: F5, up, down). F5 cycles what the other two do: BEND (hold to bend the pitch, release to
 * return: RANGE and SPEED on FX > PITCH BEND), IMPRESS or CHANCE (hold to step the amount, 4 % every 110 ms). seq.c keyboard_block hands the keys here. */
static uint8_t na_strip_mode, nb_up, nb_dn;
static int32_t nb_v;                                     /* the bend, -32768..32768 (Q15 of the range) */
static uint32_t nb_t;                                    /* samples since the amount last stepped */
static uint32_t nano_strip_key(uint32_t k, uint32_t down)
{
#if BOARD_SMK37
    (void)k, (void)down;
    return 0;
#else
    if (k < 24u || k > 26u)
        return 0;
    if (k == 24u) {
        if (down) {
            na_strip_mode = (uint8_t)((na_strip_mode + 1u) % 3u);
            nb_up = nb_dn = 0;
            nb_t = 0;
        }
    } else if (k == 25u) {
        nb_up = (uint8_t)down;
    } else {
        nb_dn = (uint8_t)down;
    }
    return 1;
#endif
}
static void nano_strip_tick(void)                        /* once a block */
{
#if !BOARD_SMK37
    int32_t tgt = na_strip_mode == 0u ? (nb_up && !nb_dn ? 32768 : nb_dn && !nb_up ? -32768 : 0) : 0;
    uint32_t ti = (uint32_t)song.sel % NTRK;
    if (nb_v != tgt) {
        /* the whole stroke takes 50 ms + (100 - SPEED) x 8 ms (the test bed's ramp) */
        int32_t ms = 50 + (127 - trk[ti].p[P_FM3_SUS]) * 800 / 127, st = (int32_t)(32768u * CTL * 1000u / (44100u * (uint32_t)ms));
        nb_v += clamp(tgt - nb_v, -st, st);
        midi_bend_target[ti] = (int32_t)(((int64_t)nb_v * (int32_t)(GI(NI_BENDRNG) ^ 12) * 256) >> 15);
    }
    if (na_strip_mode && (nb_up != nb_dn)) {
        nb_t += CTL;
        if (nb_t >= 4851u) {                             /* 110 ms */
            int16_t *q = &trk[ti].p[na_strip_mode == 1u ? P_LN3 : P_LN4];   /* (written here, in the audio block: not through na_put) */
            nb_t = 0;
            *q = (int16_t)clamp(*q + (nb_up ? 5 : -5), 0, 127);
        }
    } else {
        nb_t = 0;
    }
#endif
}

static void na_input(void)
{
    uint32_t pressed = fm1_input_edges(0), k;
    int32_t s;
    (void)fm1_input_note_edges();
    if (panel_took_moved() | pressed | fm1_in.notes | fm1_in.buttons)
        scrn.idle = fm1_ms;
    fm1_irq_off();                                          /* (the MIDI IN notes' flags: nothing here shows them) */
    midi_seen = 0;
    fm1_irq_on();
    midi_pc_poll();                                         /* Program Change: a preset */
    settings_later_poll();
    song.sel = 0;
    na_buttons(pressed);
    if ((s = panel_enc(EN_SELECT)) != 0) {                  /* the next / previous page of the group */
        uint32_t n = na_nsub();
        if (na_grp == NG_PRESET && na_cursub() == 0u)
            na_pre_step(s);
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
        na_sub[NG_PRESET] = 0;
        na_pre_step(s);
    }
    for (k = 0; k < 4u; k++)
        if ((s = panel_enc(EN_K1 + k)) != 0)
            na_knob(k, s);
}

/* The LED picture is built off-line and copied one byte per column: clearing and relighting would let the 10 kHz scan catch the dark
 * gap and flicker. */
#if BOARD_SMK37
/* SMK-37 Pro: one flag per id. Ids 0..18 are the button LEDs (smk_input.h); the note keys (no LEDs of their own)
 * take LED_KEY0 + k: ui_leds shows the white keys F3..G5 on the 16 RGB pads (smk_input.h SMK_WHITE_NOTE) */
#define LED_BUF 64u
#define LED_KEY0 32u
static uint8_t smk_fbank, smk_fbank_new;           /* (main.c smk_faders: the SMK faders of Felucca's MIXER; unused by the Nano Freak) */
static void led_pos_init(void) {}
static void led_put(uint8_t *nl, uint32_t id, int on)
{
    if (id < LED_BUF && on)
        nl[id] = 1;
}
static void led_clear(uint8_t *nl, uint32_t id)
{
    if (id < LED_BUF)
        nl[id] = 0;
}
#else
#define LED_BUF FM1_NCOL
#define LED_KEY0 14u                               /* the note keys' ids: 14..40 */
static uint8_t led_pos[41];                        /* (col << 3) | row bit, 0xFF = none */

static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}

static void led_put(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}

/* PLAY's second, green LED: not in the key matrix (no key there), found on the hardware at column 8, row PA9 (bit 1) */
#define LED_PLAY_GREEN ((8u << 3) | 1u)
static void led_clear(uint8_t *nl, uint32_t id)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF)
        nl[q >> 3] &= (uint8_t)~(1u << (q & 7u));
}
#endif
#if BOARD_SMK37
static int smk_btn_free(uint32_t id)
{
    uint32_t b;
    for (b = 0; b < NB; b++)
        if (panel.btn[b] == id)
            return 0;
    return 1;
}

/* SMK-37 Pro: the picture of ui_leds on the panel. The 19 button LEDs are on / off: lit (nl) and breathing (nb) show
 * lit, the glow layers have no LED level here. The 16 RGB pads are the white keys F3..G5 (smk_input.h SMK_WHITE_NOTE)
 * in the theme's colour: lit full, breathing dark .. full and back (~1.1 s, as fm1_input.h), the mid level (nm: the
 * grid's beats, the scale's root) a quarter, the glow of a key that does something (og: the DRUM grid's lanes, sg:
 * SCALE LEDS) a sixteenth; the other keys' idle glow is off: off is off on the pads */
static void smk_leds(const uint8_t *nl, const uint8_t *nb, const uint8_t *nm, const uint8_t *og, const uint8_t *sg)
{
    uint32_t c, th = T_THEME, br = (fm1_ms >> 2) & 0xFFu;
    uint32_t r = (th >> 11) << 3 | (th >> 13), g = ((th >> 5) & 0x3Fu) << 2 | ((th >> 9) & 3u), b = (th & 0x1Fu) << 3 | ((th >> 2) & 7u);
    br = br < 128u ? br * 2u : (255u - br) * 2u;        /* the breath: 0..254 over ~1 s */
    for (c = 0; c < SMK_NLED; c++)
        fm1_led[c] = (uint8_t)(nl[c] | nb[c]);
    if (smk_btn_free(SMK_BTN_KNOB_BANK))
        fm1_led[SMK_BTN_KNOB_BANK] = ui.menu != 0;      /* KNOB BANK lit while the menu is up */
    if ((int32_t)(fm1_ms - padled_hold_until) < 0)      /* (console padtest has them) */
        return;
    for (c = 0; c < SMK_NPAD; c++) {                    /* white key c on its pad (the top row first) */
        uint32_t id = LED_KEY0 + SMK_WHITE_NOTE[c];
        uint32_t lv = nl[id] ? 256u : nb[id] ? br + 2u : nm[id] ? 64u : og[id] | sg[id] ? 16u : 0u;
        padled_set(SMK_PAD_OF_WHITE(c), (uint8_t)(r * lv >> 8), (uint8_t)(g * lv >> 8), (uint8_t)(b * lv >> 8));
    }
}
#endif

static void na_leds(void)
{
    uint8_t nl[LED_BUF] = {0}, nd[LED_BUF] = {0}, nb[LED_BUF] = {0}, nm[LED_BUF] = {0};
    uint32_t k;
    static uint8_t ready;
    if (fm1_led_anim_on())
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
    led_put(nl, panel.btn[B_PLAY], na_seqmode ? song.playing != 0u : na_arp_on());
    led_put(nl, panel.btn[B_REC], na_rec != 0u);
    led_put(nl, panel.btn[B_OCTDN], song.octave < 0);
    led_put(nl, panel.btn[B_OCTUP], song.octave > 0);
    {
        uint32_t c = fm1_in.notes | nano_sound_keys();
        for (k = 0; k < 27u; k++)
            led_put(nl, LED_KEY0 + k, (int)((c >> k) & 1u));
    }
#if BOARD_SMK37
    {
        static const uint8_t Z[LED_BUF];                      /* (no DRUM-grid lanes, no SCALE LEDS) */
        smk_leds(nl, nb, nm, Z, Z);                           /* the 19 button LEDs (ui_input.c) */
    }
#else
    for (k = 0; k < FM1_NCOL; k++) {
        fm1_led_dim[k] = nd[k];
        fm1_led_breath[k] = nb[k];
        fm1_led_mid[k] = nm[k];
        fm1_led[k] = nl[k];
    }
#endif
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
    na_g_load();                                            /* (the settings are in: persist_boot ran) */
    if (!GI(NI_SUBOOT))
        return;
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
    na_bank = 1;
    na_str(na_name, ENGINES[ENGI_NANO]->presets[0].name);
    na_g_load();
    {   /* POWER-ON: INIT (the first preset), LAST (as the autosave left it), or a user preset */
        uint32_t pp = GI(NI_SUPATCH);
        if (pp == 0u)
            na_load(0);
        else if (pp >= 2u && up_used(pp - 2u))
            na_load_user(pp - 2u);
    }
    na_grp = NG_OSC;
    if (GI(NI_SUPAGE) && favorites.factory[6][24] < NG_GLOBAL && favorites.factory[6][0] == 0xB6)
        na_grp = favorites.factory[6][24];
    na_dirty = 1;
    ui_idle |= 1u;
    ui.force = 1;
}
static void na_smk_faders(void) {}
#endif /* FELUCCA_FMINI_ONLY */
