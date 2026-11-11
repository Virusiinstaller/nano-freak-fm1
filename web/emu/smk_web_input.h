/* SPDX-License-Identifier: GPL-3.0-only */
/* The SMK-37 in the browser (felucca_web.c with BOARD_SMK37): what firmware/hal/smk_input.h gives the firmware, without
 * the hardware. The page hands over what the scan would find: a key with its velocity, a pad hit or release, the
 * 18 buttons, encoder detents, the faders (0..127) and the wheels (raw 0..1023, as mux C reads them); the logic past
 * the scan (the outer keys' queue, the UI-owned pads, the edges, the virtual MASTER pot on K1) is smk_input.h's, copied.
 * Included before tests/hostsim.c: seq.c and the MIDI OUT (smk_midi_out.c) take from it. */
#pragma once
#include <stdint.h>

#define FM1_INPUT_IDLE() ((void)0)
#define FM1_NBTN 18u
#define FM1_NKEY 37u
#define FM1_NENC 8u
#define SMK_NPAD 16u
#define SMK_NLED 19u
#define SMK_KEY_F3 5u
enum { FM1_BTN_OCT_DOWN = 4, FM1_BTN_OCT_UP = 5 };
enum { SMK_BTN_KNOB_BANK = 0, SMK_BTN_FADER_BANK = 1, SMK_BTN_LEFT = 2, SMK_BTN_RIGHT = 3 };
#define FM1_OCT_BOTH (1u << FM1_BTN_OCT_DOWN | 1u << FM1_BTN_OCT_UP)
#ifndef FM1_TICKS_PER_US
#define FM1_TICKS_PER_US 24u
#endif
static uint32_t fm1_ticks(void);

static const uint8_t SMK_WHITE_NOTE[SMK_NPAD] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static volatile uint8_t smk_pad_grid;
#define SMK_PAD_OF_WHITE(w) (smk_pad_grid ? (w) : (w) < 8u ? (w) + 8u : (w) - 8u)
static const int8_t SMK_PIANO_NOTE[SMK_NPAD] = {7, 9, 11, 12, 14, 16, 18, 19, 8, 10, -1, 13, 15, 17, -1, 20};
static volatile uint8_t smk_pad_piano;
static volatile uint8_t smk_pad_ui;
static volatile uint16_t smk_pad_hits;
#define SMK_PADMQ 32u
static volatile uint16_t smk_padmq[SMK_PADMQ];
static volatile uint8_t smk_padmq_w, smk_padmq_r;
static uint8_t smk_pad_note[SMK_NPAD];
#define SMK_WHITE_OF_PAD(p) (smk_pad_grid ? (p) : (p) < 8u ? (p) + 8u : (p) - 8u)
#define SMK_TRIM_FLASH 0xC1000u
static int smk_trim_load(const uint8_t *rec) { (void)rec; return -1; }

static volatile struct {
    uint32_t notes, knotes, pnotes, buttons, pressed, released, notes_pressed;
    uint64_t keys;
    uint8_t key_vel[FM1_NKEY];
    int16_t enc_steps[FM1_NENC];
    uint8_t fader[4];
    uint16_t wheel[2], pedal, batt;
    uint32_t frames;
} fm1_in;

#define SMK_EXTQ 32u
static volatile uint16_t smk_extq[SMK_EXTQ];
static volatile uint8_t smk_extq_w, smk_extq_r;
static uint8_t fm1_led[SMK_NLED];

static void smk__extq(uint32_t k, uint32_t vel)
{
    uint8_t w = smk_extq_w;
    if ((uint8_t)(w + 1u) % SMK_EXTQ == smk_extq_r)
        return;
    smk_extq[w] = (uint16_t)(k << 8 | vel);
    smk_extq_w = (uint8_t)((w + 1u) % SMK_EXTQ);
}

static void smk__key_event(uint32_t k, uint32_t vel)     /* vel 0 = release */
{
    if (vel)
        fm1_in.key_vel[k] = (uint8_t)vel;
    if (k >= SMK_KEY_F3 && k < SMK_KEY_F3 + 27u) {
        uint32_t bit = 1u << (k - SMK_KEY_F3);
        if (vel) {
            fm1_in.knotes |= bit;
            fm1_in.notes_pressed |= bit;
        } else {
            fm1_in.knotes &= ~bit;
        }
        fm1_in.notes = fm1_in.knotes | fm1_in.pnotes;
    } else {
        smk__extq(k, vel);
    }
    if (vel)
        fm1_in.keys |= (uint64_t)1u << k;
    else
        fm1_in.keys &= ~((uint64_t)1u << k);
}

static void smk__padq(uint32_t pad, uint32_t vel)
{
    int32_t n0 = smk_pad_piano ? SMK_PIANO_NOTE[pad] : (int32_t)SMK_WHITE_NOTE[SMK_WHITE_OF_PAD(pad)];
    uint32_t n, bit;
    if (smk_pad_ui && !smk_pad_note[pad]) {
        uint8_t w = smk_padmq_w;
        if (vel)
            smk_pad_hits |= (uint16_t)(1u << pad);
        if ((uint8_t)(w + 1u) % SMK_PADMQ != smk_padmq_r) {
            smk_padmq[w] = (uint16_t)(pad << 8 | (vel & 0x7Fu));
            smk_padmq_w = (uint8_t)((w + 1u) % SMK_PADMQ);
        }
        return;
    }
    if (vel) {
        if (n0 < 0)
            return;
        smk_pad_note[pad] = (uint8_t)(n0 + 1);
    } else {
        if (!smk_pad_note[pad])
            return;
        n0 = smk_pad_note[pad] - 1;
        smk_pad_note[pad] = 0;
    }
    n = (uint32_t)n0;
    bit = 1u << n;
    if (vel) {
        fm1_in.key_vel[SMK_KEY_F3 + n] = (uint8_t)vel;
        fm1_in.pnotes |= bit;
        fm1_in.notes_pressed |= bit;
    } else {
        fm1_in.pnotes &= ~bit;
    }
    fm1_in.notes = fm1_in.knotes | fm1_in.pnotes;
}

static void fm1_input_init(void) {}
static void fm1_input_tick(void) {}
static void fm1_input_scan(void) {}
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = fm1_in.enc_steps[e % FM1_NENC];
    fm1_in.enc_steps[e % FM1_NENC] = 0;
    return s;
}
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t p = fm1_in.pressed;
    if (released)
        *released = fm1_in.released;
    fm1_in.pressed = fm1_in.released = 0;
    return p;
}
static uint32_t fm1_input_note_edges(void)
{
    uint32_t p = fm1_in.notes_pressed;
    fm1_in.notes_pressed = 0;
    return p;
}
static int32_t smk_padmidi_take(void)
{
    int32_t v = -1;
    if (smk_padmq_r != smk_padmq_w) {
        v = smk_padmq[smk_padmq_r];
        smk_padmq_r = (uint8_t)((smk_padmq_r + 1u) % SMK_PADMQ);
    }
    return v;
}
static int32_t smk_extkey_take(void)
{
    int32_t v = -1;
    if (smk_extq_r != smk_extq_w) {
        v = smk_extq[smk_extq_r];
        smk_extq_r = (uint8_t)((smk_extq_r + 1u) % SMK_EXTQ);
    }
    return v;
}
static void fm1_led_key(uint32_t id, int on)
{
    if (id < SMK_NLED)
        fm1_led[id] = (uint8_t)(on != 0);
}

/* the virtual MASTER pot on K1, as smk_input.h fm1_adc_read */
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
#define SMK_ENC_MASTER 0u
#define SMK_MASTER_SLOW_MS 150u
static int32_t smk_master = 724;
static void fm1_adc_init(void) {}
static int32_t fm1_adc_read(uint32_t ch)
{
    if (ch == FM1_ADC_BATT)
        return 620;
    if (ch == FM1_ADC_MASTER) {
        static uint32_t t_last;
        int32_t s = fm1_enc_take(SMK_ENC_MASTER);
        if (s) {
            uint32_t now = fm1_ticks(), dt = (now - t_last) / (1000u * FM1_TICKS_PER_US);
            t_last = now;
            if ((s == 1 || s == -1) && dt >= SMK_MASTER_SLOW_MS) {
                int32_t pct = smk_master * 100 / 1023 + s;
                pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
                smk_master = (pct * 1023 + 99) / 100;
            } else
                smk_master += s * 24;
            smk_master = smk_master < 0 ? 0 : smk_master > 1023 ? 1023 : smk_master;
        }
        return smk_master;
    }
    return -1;
}

/* hal/smk_compat.h: no FM-1 LED glow or animations on the SMK */
static void fm1_led_dim_level(uint32_t lo) { (void)lo; }
static void fm1_led_anim_start(uint32_t glow) { (void)glow; }
static int fm1_led_anim_on(void) { return 0; }
static void fm1_led_idle_start(uint32_t glow) { (void)glow; }
static int fm1_led_idle_on(void) { return 0; }
static void fm1_led_anim_stop(void) {}

/* lcd.c's RGB pads: their colours for the page (0x00RRGGBB per hardware pad) */
#define PADLED_N 16u
static uint32_t padled_rgb[PADLED_N];
static uint32_t padled_hold_until;
static void padled_set(uint32_t pad, uint8_t r, uint8_t g, uint8_t b)
{
    if (pad < PADLED_N)
        padled_rgb[pad] = (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}
static void padled_flush(void) {}
