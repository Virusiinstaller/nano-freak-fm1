/* SPDX-License-Identifier: GPL-3.0-only */
/* NANO's performance layer (the Nano Freak test bed's arpeggiator extras, Impress / Chance, the MIDI CC map and the MIDI
 * OUT of the arp and the sequencer). Included by seq.c before the arp.
 *
 * The extras live in common track parameters that only a DRUM track uses (its lane levels P_LN0..P_LN4); a NANO
 * track reads them as below, and ui.c apply_preset_to / nano_track_loaded give them these values on a load:
 *   P_LN0  ARP LENGTH   0..127 -> 1..16 steps (the loop of the pattern; AS PLAYED: the keys it remembers); 127 = 16
 *   P_LN1  ARP CHORD    0 off, 1..8 Maj Min 7 Min7 Maj7 Sus4 Dim7 Power (above 8: off)
 *   P_LN2  ARP TYPE     0 mono, 1 chord (each step of the pattern plays the chord on its note)
 *   P_LN3  IMPRESS      0..127: how much of the Chance variation is heard (0: the pattern as written, bit for bit)
 *   P_LN4  CHANCE       0..127: how hard the next roll is; a change of the value rolls
 * Impress and Chance vary the gate (a gate that comes out at nothing leaves the step out), the velocity (a little) and, from
 * 60 % Impress, the octave of a step: one status per step, shared by the arp and the sequencer (the test bed's nf_var.h).
 *
 * MIDI: fm_seq_out (GLO, the app's page) is where the notes of the arp and the sequencer go: 0 LOCAL (as always), 1 BOTH,
 * 2 MIDI only (silent here). fm_cc_out: 1 = a change of a NANO control on the device sends its CC. The CC map is the test
 * bed's (nano_cc[]): a NANO track answers to it on MIDI IN. */
#define NANO_T(t) (ENGINES[eng_idx((t)->eng_req)] == &ENG_NANO)
static volatile uint8_t fm_seq_out;                     /* 0 LOCAL, 1 BOTH, 2 MIDI */
static volatile uint8_t fm_cc_out;                      /* 0 off, 1 on */

static uint32_t nano_alen(const track_t *t) { return 1u + ((uint32_t)t->p[P_LN0] & 127u) * 15u / 127u; }
static uint32_t nano_achord(const track_t *t) { return (uint32_t)t->p[P_LN1] <= 8u ? (uint32_t)t->p[P_LN1] : 0u; }
static uint32_t nano_atype(const track_t *t) { return t->p[P_LN2] == 1; }
static const uint8_t NANO_RC[8][4] = {{0, 4, 7, 12}, {0, 3, 7, 12}, {0, 4, 7, 10}, {0, 3, 7, 10},
                                       {0, 4, 7, 11}, {0, 5, 7, 12}, {0, 3, 6, 9}, {0, 7, 12, 19}};

/* ------------------------------------------------------ Impress / Chance --- */
/* the status of each step in a byte: the gate in the top six bits (Q5, -32..31) and the octave in the low two (1 = +1, 2 = -1);
 * the velocity's is the gate's of another step (a fixed shuffle): 64 bytes in all, the RAM is tight */
static int8_t nv_s[NSTEP];
static int32_t nv_rand(void) { return (int32_t)(rng() >> 16) - 32768; }   /* -32768 .. 32767 */
static void nv_roll(uint32_t chance)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++) {
        int32_t g = (nv_s[i] >> 2) + (((nv_rand() * (int32_t)chance / 127) + (nv_rand() < 0 ? -512 : 512)) >> 10);   /* (a random walk) */
        int32_t o = nv_s[i] & 3;
        if (nv_rand() + 32768 < (int32_t)chance * 516)
            o = nv_rand() + 32768 < 7864 ? (nv_rand() < 0 ? 2 : 1) : 0;
        nv_s[i] = (int8_t)(clamp(g, -31, 31) * 4 + o);
    }
}
static int32_t nv_imp(const track_t *t) { return NANO_T(t) ? (int32_t)(t->p[P_LN3] & 127) : 0; }
/* the gate of step i (a share of the step, /128) from the written one; under 5 the step is left out */
static int32_t nv_gate(const track_t *t, uint32_t i, int32_t base)
{
    int32_t imp = nv_imp(t);
    return imp ? clamp(base + ((imp * (nv_s[i & 63u] >> 2)) >> 5), 0, 128) : base;
}
static uint32_t nv_vel(const track_t *t, uint32_t i, uint32_t vel)
{
    int32_t imp = nv_imp(t), q;
    if (!imp)
        return vel;
    q = (imp * (nv_s[(i * 7u + 13u) & 63u] >> 2)) >> 5;
    return (uint32_t)clamp((int32_t)vel + (((int32_t)vel * q * 155) >> 16), 1, 127);
}
static int32_t nv_oct(const track_t *t, uint32_t i)
{
    uint32_t o = (uint32_t)nv_s[i & 63u] & 3u;
    return nv_imp(t) > 76 ? (o == 1u ? 12 : o == 2u ? -12 : 0) : 0;
}
/* PATTERN's dice for step i (0..7): the key press's seed run through a xorshift */
static uint32_t nv_patt(const track_t *t, uint32_t i)
{
    uint32_t x = t->arp_seed ? t->arp_seed : 1u, k;
    for (k = 0; k <= i; k++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
    }
    return x >> 16;
}
/* once a block (voice.c track_render) for a NANO part: a new CHANCE rolls the dice */
static void nano_poll(track_t *t)
{
    if ((uint8_t)t->p[P_LN4] != t->nano_ch) {
        t->nano_ch = (uint8_t)t->p[P_LN4];
        nv_roll((uint32_t)(t->p[P_LN4] & 127));
    }
}
/* the performance parameters of a NANO track as a load leaves them (ui.c apply_preset_to, tests/hostsim.c host_preset) */
static void nano_track_loaded(track_t *t)
{
    if (t->eng_req != ENGI_NANO)
        return;
    t->p[P_LN0] = 127;
    t->p[P_LN1] = t->p[P_LN2] = t->p[P_LN3] = t->p[P_LN4] = 0;
    t->nano_ch = 0;
}

/* ---------------------------------------------- the arp / sequencer to MIDI OUT --- */
static void so_on(track_t *t, uint32_t n, uint32_t v)
{
    if (fm_seq_out)
        midi_out_event(0x09u | (0x90u | trk_midi_ch(trk_index(t))) << 8 | n << 16 | v << 24);
    if (fm_seq_out != 2u)
        trk_note_on(t, n, v);
}
static void so_off(track_t *t, uint32_t n)
{
    if (fm_seq_out)
        midi_out_event(0x08u | (0x80u | trk_midi_ch(trk_index(t))) << 8 | n << 16);
    trk_note_off(t, n);
}

/* ------------------------------------------------------------ the CC map --- */
enum { NC_LIN, NC_SPR, NC_SW, NC_FXS, NC_FXD, NC_FXC, NC_VOICE, NC_BPM, NC_PLAY, NC_TRANSP, NC_ARPPAT };
static const struct { uint8_t cc, kind, id, n; } nano_cc[] = {
    {2, NC_LIN, P_LN3, 0}, {3, NC_LIN, P_LN4, 0}, {5, NC_LIN, P_GLIDE, 0}, {7, NC_LIN, P_LEVEL, 0},
    {9, NC_SPR, P_E0, 15}, {10, NC_LIN, P_E1, 0}, {12, NC_LIN, P_E2, 0}, {13, NC_LIN, P_E3, 0}, {14, NC_LIN, P_E4, 0},
    {15, NC_LIN, P_E7, 0}, {17, NC_SPR, P_FTYPE, 3}, {18, NC_FXS, P_ED_FX, 0}, {19, NC_FXD, P_ED_FX, 0},
    {20, NC_FXC, P_ED_FX, 0}, {21, NC_SPR, P_FM1_LEVEL, 3}, {23, NC_LIN, P_E5, 0}, {24, NC_LIN, P_FM1_REL, 0},
    {26, NC_LIN, P_ED_FLT, 0}, {28, NC_LIN, P_FM1_SUS, 0}, {29, NC_LIN, P_SUS, 0}, {30, NC_SPR, P_LWAVE, 6},
    {31, NC_LIN, P_FM2_ATK, 0}, {64, NC_SW, P_AHOLD, 0}, {65, NC_VOICE, P_VOICE, 0}, {66, NC_LIN, P_DETUNE, 0},
    {67, NC_LIN, P_ASWING, 0}, {72, NC_PLAY, P_AMODE, 0}, {73, NC_ARPPAT, P_AMODE, 14}, {74, NC_LIN, P_AOCT, 0},
    {75, NC_LIN, P_LN0, 0}, {76, NC_SW, P_LN2, 0}, {77, NC_SPR, P_LN1, 9}, {79, NC_TRANSP, 0, 0},
    {81, NC_LIN, P_SLEN, 0}, {82, NC_LIN, P_SGATE, 0}, {83, NC_LIN, P_E6, 0}, {90, NC_LIN, P_REV, 0},
    {91, NC_BPM, 0, 0}, {93, NC_LIN, P_LRATE, 0}, {94, NC_LIN, P_LRATE, 0}, {102, NC_LIN, P_FM1_ATK, 0},
    {103, NC_LIN, P_FM1_DEC, 0}, {105, NC_LIN, P_ATK, 0}, {106, NC_LIN, P_DEC, 0}, {110, NC_LIN, P_DLY, 0},
};
/* the test bed's arp pattern order -> seq.c AM_* */
static const uint8_t NANO_APAT[14] = {1, 2, 3, 5, 4, 15, 16, 9, 10, 17, 18, 14, 19, 20};
static int32_t nano_ctl_get(const track_t *t, uint32_t k);

/* CC cc = v on a NANO track: 1 = it is NANO's (the app's map never sees it) */
static int nano_cc_in(track_t *t, uint32_t cc, uint32_t v)
{
    uint32_t k, x;
    for (k = 0; k < NELEM(nano_cc) && nano_cc[k].cc != cc; k++)
        ;
    if (k == NELEM(nano_cc))
        return 0;
    {
        uint32_t id = nano_cc[k].id, n = nano_cc[k].n;
        const param_desc_t *d = id < P_COUNT ? param_desc_of(eng_idx(t->eng_req), id) : 0;
        int16_t *p = t->p;
        switch (nano_cc[k].kind) {
        case NC_LIN:
            if (d && d->max > d->min)
                p[id] = (int16_t)(d->min + ((int32_t)v * (d->max - d->min) + 63) / 127);
            break;
        case NC_SPR:
            p[id] = (int16_t)(v * n >> 7);
            break;
        case NC_SW:
            p[id] = v >= 64u;
            break;
        case NC_FXS:
            p[P_ED_FX] = (int16_t)((p[P_ED_FX] & ~1) | (v >= 64u));
            break;
        case NC_FXD:
        case NC_FXC:
            x = (v * 7u + 63u) / 127u;
            if (nano_cc[k].kind == NC_FXD)
                p[P_ED_FX] = (int16_t)((p[P_ED_FX] & ~0x0E) | (int32_t)(x << 1));
            else
                p[P_ED_FX] = (int16_t)((p[P_ED_FX] & ~0x70) | (int32_t)(x << 4));
            break;
        case NC_VOICE:
            p[P_VOICE] = (int16_t)(v < 32u ? V_MONO : v < 64u ? V_UNISON : V_POLY);
            break;
        case NC_BPM:
            song.g[G_BPM] = (int16_t)(20u + v * 280u / 127u);
            break;
        case NC_PLAY:
            if (v >= 64u && !p[P_AMODE])
                p[P_AMODE] = 1;
            else if (v < 64u)
                p[P_AMODE] = 0;
            break;
        case NC_TRANSP:
            transport_req = v >= 64u ? 1 : 2;
            break;
        default:                                        /* NC_ARPPAT */
            p[P_AMODE] = NANO_APAT[v * 14u >> 7];
            break;
        }
    }
    return 1;
}

/* the value (0..127) CC k of the table has on track t: what a change of its control sends */
static int32_t nano_ctl_get(const track_t *t, uint32_t k)
{
    uint32_t id = nano_cc[k].id, n = nano_cc[k].n, i;
    const param_desc_t *d = id < P_COUNT ? param_desc_of(eng_idx(t->eng_req), id) : 0;
    const int16_t *p = t->p;
    int32_t x;
    switch (nano_cc[k].kind) {
    case NC_LIN:
        return d && d->max > d->min ? clamp(((p[id] - d->min) * 127 + (d->max - d->min) / 2) / (d->max - d->min), 0, 127) : 0;
    case NC_SPR:
        return clamp(((2 * clamp(p[id], 0, (int32_t)n - 1) + 1) * 128 / (int32_t)n) / 2, 0, 127);
    case NC_SW:
        return p[id] ? 127 : 0;
    case NC_FXS:
        return (p[P_ED_FX] & 1) ? 127 : 0;
    case NC_FXD:
        return ((p[P_ED_FX] >> 1) & 7) * 127 / 7;
    case NC_FXC:
        return ((p[P_ED_FX] >> 4) & 7) * 127 / 7;
    case NC_VOICE:
        return p[P_VOICE] == V_POLY ? 112 : p[P_VOICE] == V_UNISON ? 48 : 16;
    case NC_PLAY:
        return p[P_AMODE] ? 127 : 0;
    case NC_ARPPAT:
        for (i = 0; i < 14u; i++)
            if (NANO_APAT[i] == p[P_AMODE]) {
                x = (int32_t)((2u * i + 1u) * 128u / 14u / 2u);
                return x;
            }
        return 0;
    default:
        return -1;
    }
}

/* the main loop: a NANO control changed (motion_capture): its CC(s) go out, in the audio ISR's hands (midi_out_event has one
 * producer: nano_cc_flush) */
#define NCO_Q 4u
static volatile uint32_t nco_q[NCO_Q];
static volatile uint8_t nco_w, nco_r;
static void nano_cc_out(const track_t *t, uint32_t id)
{
    uint32_t k;
    if (!fm_cc_out || !NANO_T(t))
        return;
    for (k = 0; k < NELEM(nano_cc); k++) {
        int32_t v;
        if (nano_cc[k].id != id || nano_cc[k].kind == NC_TRANSP || nano_cc[k].kind == NC_BPM)
            continue;
        v = nano_ctl_get(t, k);
        if (v < 0 || (uint8_t)(nco_w - nco_r) >= NCO_Q)
            continue;
        nco_q[nco_w % NCO_Q] = 0x0Bu | (0xB0u | trk_midi_ch(trk_index(t))) << 8 | (uint32_t)nano_cc[k].cc << 16 | (uint32_t)v << 24;
        __asm__ volatile("" ::: "memory");
        nco_w++;
    }
}
static void nano_cc_flush(void)                         /* the audio ISR, once a block */
{
    while (nco_r != nco_w) {
        midi_out_event(nco_q[nco_r % NCO_Q]);
        nco_r++;
    }
}

/* ---- the NANO sequencer (the test bed's nf_seq): up to 64 steps, each a pitch or a rest and a length (a semiquaver, quaver, crotchet
 * or minim). A step is a step_t: note[0] its pitch, n 1 (a rest: n 0, time ST_REST), acc bits 0..1 its length code. The Random and
 * Mutate generators keep the pattern they replaced in the step's own spare bytes (note[1] / note[2] the pitch, acc bits 2..3 and 4..5
 * the length), so a position of 0 puts it back and no RAM is spent. Their positions and settings are P_FM2_DEC (RANDOM), P_FM2_SUS
 * (MUTATE), P_FM2_REL (scale), P_FM2_LEVEL (octaves - 1), P_FM3_ATK (rests, steps of 5 %), P_FM3_DEC (the length a recorded note gets). */
#define NS_REST 255u
static uint32_t nano_sq_ticks(const step_t *s) { return 1u << (s->acc & 3u); }
static uint32_t nano_sq_rest(const step_t *s) { return s->time == ST_REST || !s->n; }
static uint32_t nano_sq_pos(const track_t *t, uint32_t idx)             /* the semiquavers before step idx */
{
    uint32_t k, p = 0;
    for (k = 0; k < idx && k < NSTEP; k++)
        p += nano_sq_ticks(&t->step[k]);
    return p;
}
/* the length of step idx in samples: its semiquavers; a one-semiquaver step is swung as the grid's off-beats are */
static uint32_t nano_sq_len(const track_t *t, uint32_t period, uint32_t idx)
{
    uint32_t n = nano_sq_ticks(&t->step[idx % NSTEP]);
    return n == 1u ? swing_step_len(t, period, nano_sq_pos(t, idx)) : period * n;
}
static void nano_sq_put(step_t *s, uint32_t note, uint32_t dur)         /* note 255: a rest */
{
    s->acc = (uint8_t)((s->acc & ~3u) | (dur & 3u));
    s->hit = 0;
    s->flags = 0;
    s->probability = 0;
    s->vel = 100;
    if (note == NS_REST) {
        s->n = 0;
        s->time = ST_REST;
        s->note[0] = 0;
    } else {
        s->n = 1;
        s->time = ST_NOTE;
        s->note[0] = (uint8_t)note;
    }
}
static uint32_t nano_sq_note(const step_t *s) { return nano_sq_rest(s) ? NS_REST : s->note[0]; }

static const uint8_t NS_SC[7][12] = {{0, 3, 5, 7, 10}, {0, 2, 4, 7, 9}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 3, 5, 6, 7, 10},
                                     {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}};
static const uint8_t NS_SCN[7] = {5, 5, 7, 7, 7, 6, 12};
static uint32_t nano_sq_a;
static uint32_t nano_sq_rnd(void)                                       /* the test bed's mulberry32 */
{
    uint32_t x;
    nano_sq_a += 0x6D2B79F5u;
    x = nano_sq_a;
    x = (x ^ (x >> 15)) * (x | 1u);
    x ^= x + (x ^ (x >> 7)) * (x | 61u);
    return x ^ (x >> 14);
}
static uint32_t nano_sq_lt(uint32_t r, uint32_t num, uint32_t den)      /* r / 2^32 < num / den */
{
    return (uint64_t)r * den < ((uint64_t)num << 32);
}
static uint32_t nano_sq_below(uint32_t r, uint32_t n) { return (uint32_t)(((uint64_t)r * n) >> 32); }   /* floor(r / 2^32 * n) */
static int32_t nano_sq_abs(int32_t x) { return x < 0 ? -x : x; }
static uint32_t nano_sq_root(uint32_t last) { return last < 24u ? 24u : last > 84u ? 84u : last; }
#define NS_RND_POS(t) ((t)->p[P_FM2_DEC])
#define NS_MUT_POS(t) ((t)->p[P_FM2_SUS])
#define NS_RANGE(t) ((uint32_t)(t)->p[P_FM2_LEVEL] % 3u + 1u)
#define NS_SCALE(t) ((uint32_t)(t)->p[P_FM2_REL] % 7u)
#define NS_RESTS(t) ((uint32_t)(t)->p[P_FM3_ATK] % 11u)
#define NS_ENT(t) ((uint32_t)(t)->p[P_FM3_DEC] & 3u)
static void nano_sq_keep_r(track_t *t)                                  /* the pattern that is there, kept for Random's Off */
{
    uint32_t k;
    for (k = 0; k < NSTEP; k++) {
        step_t *s = &t->step[k];
        s->note[1] = (uint8_t)nano_sq_note(s);
        s->acc = (uint8_t)((s->acc & ~(3u << 2)) | ((s->acc & 3u) << 2));
    }
}
static void nano_sq_keep_m(track_t *t)
{
    uint32_t k;
    for (k = 0; k < NSTEP; k++) {
        step_t *s = &t->step[k];
        s->note[2] = (uint8_t)nano_sq_note(s);
        s->acc = (uint8_t)((s->acc & ~(3u << 4)) | ((s->acc & 3u) << 4));
    }
}
static void nano_sq_back(track_t *t, uint32_t m)                        /* m 0: Random's pattern back, 1: Mutate's base */
{
    uint32_t k;
    for (k = 0; k < NSTEP; k++) {
        step_t *s = &t->step[k];
        nano_sq_put(s, m ? s->note[2] : s->note[1], (s->acc >> (m ? 4 : 2)) & 3u);
    }
}
/* Random: position i (1..99) is a seed; the same position gives the same pattern. 0 puts the pattern back */
static void nano_sq_random(track_t *t, uint32_t i, uint32_t last)
{
    uint32_t k, pool[36], np = 0, o, q, root = nano_sq_root(last), sc = NS_SCALE(t), len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
    if (!i) {
        if (NS_RND_POS(t))
            nano_sq_back(t, 0);
        NS_RND_POS(t) = 0;
        NS_MUT_POS(t) = 0;
        return;
    }
    if (!NS_RND_POS(t))
        nano_sq_keep_r(t);
    NS_RND_POS(t) = (int16_t)i;
    NS_MUT_POS(t) = 0;
    nano_sq_a = i * 2654435761u + 1013904223u;
    for (o = 0; o < NS_RANGE(t); o++)
        for (q = 0; q < NS_SCN[sc]; q++)
            pool[np++] = NS_SC[sc][q] + 12u * o;
    for (k = 0; k < len; k++) {
        uint32_t r = nano_sq_rnd(), d = nano_sq_lt(r, 3, 10) ? 0u : nano_sq_lt(r, 65, 100) ? 1u : nano_sq_lt(r, 9, 10) ? 2u : 3u, n = NS_REST;
        if (k == 0u) {
            nano_sq_rnd();                                              /* (the pitch is drawn, then the root replaces it) */
            n = root;
        } else if (!nano_sq_lt(nano_sq_rnd(), NS_RESTS(t), 20)) {
            n = root + pool[nano_sq_below(nano_sq_rnd(), np)];
            n = n > 127u ? 127u : n;
        }
        nano_sq_put(&t->step[k], n, d);
    }
}
/* Mutate: position i (1..99) is a seed that reworks about a quarter of the steps of the pattern that was there (its base, kept
 * while the position is not 0): a note moves one or two degrees along the scale, jumps an octave, becomes a rest, or changes length;
 * a rest gets a note. 0 puts the base back */
static void nano_sq_mutate(track_t *t, uint32_t i, uint32_t last)
{
    uint32_t k, root = nano_sq_root(last), sc = NS_SCALE(t), len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), nt = 0, o, q;
    int32_t tones[7 * 12];
    if (!i) {
        if (NS_MUT_POS(t))
            nano_sq_back(t, 1);
        NS_MUT_POS(t) = 0;
        return;
    }
    if (!NS_MUT_POS(t))
        nano_sq_keep_m(t);
    NS_MUT_POS(t) = (int16_t)i;
    nano_sq_a = i * 2246822519u + 374761393u;
    for (o = 0; o < 7u; o++)
        for (q = 0; q < NS_SCN[sc]; q++)
            tones[nt++] = (int32_t)root + NS_SC[sc][q] + 12 * ((int32_t)o - 2);
    nano_sq_back(t, 1);
    for (k = 0; k < len; k++) {
        step_t *s = &t->step[k];
        uint32_t m, base = s->note[2], dur = (s->acc >> 4) & 3u;
        int32_t b, j;
        if (!nano_sq_lt(nano_sq_rnd(), 28, 100))
            continue;
        m = nano_sq_rnd();
        if (base == NS_REST) {                                      /* a rest gets a note */
            int32_t at = 0, x;
            for (j = 0, b = 0; j < (int32_t)nt; j++)
                if (nano_sq_abs(tones[j] - (int32_t)root) < nano_sq_abs(tones[b] - (int32_t)root))
                    b = j;
            at = b + (int32_t)nano_sq_below(nano_sq_rnd(), NS_SCN[sc] * 2u);
            x = tones[at < (int32_t)nt ? at : (int32_t)nt - 1];
            nano_sq_put(s, (uint32_t)clamp(x, 0, 127), dur);
            continue;
        }
        if (nano_sq_lt(m, 4, 10)) {                                 /* along the scale */
            int32_t dir = nano_sq_lt(nano_sq_rnd(), 1, 2) ? -1 : 1, st;
            for (j = 0, b = 0; j < (int32_t)nt; j++)
                if (nano_sq_abs(tones[j] - (int32_t)base) < nano_sq_abs(tones[b] - (int32_t)base))
                    b = j;
            st = (int32_t)(1u + nano_sq_below(nano_sq_rnd(), 2u));
            b = clamp(b + dir * st, 0, (int32_t)nt - 1);
            nano_sq_put(s, (uint32_t)clamp(tones[b], 0, 127), dur);
        } else if (nano_sq_lt(m, 6, 10)) {                          /* an octave */
            int32_t x = (int32_t)base + (nano_sq_lt(nano_sq_rnd(), 1, 2) ? -12 : 12);
            nano_sq_put(s, (uint32_t)clamp(x, 0, 127), dur);
        } else if (nano_sq_lt(m, 8, 10)) {
            nano_sq_put(s, NS_REST, dur);
        } else {
            nano_sq_put(s, base, (dur + 1u + nano_sq_below(nano_sq_rnd(), 3u)) & 3u);
        }
    }
}
