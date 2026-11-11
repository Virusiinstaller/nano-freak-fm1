/* SPDX-License-Identifier: GPL-3.0-only */
/* SMK-37: MIDI OUT on the TRS jack (hal/smk_uart_tx.h) and the output's settings (GLO > MIDI SETTINGS, fmini_app.c):
 * MIDI OUT ALL / USB / TRS (where the keys, the wheels and the rest go) and MIDI OUT CH 1..16 (seq.c trk_midi_ch).
 * usb.c midi_out_event hands each USB-MIDI packet here too, before its "USB configured" check, so the jack works
 * without a computer. The pattern is the fm1-to-smk37 kit's (docs/06-mapping-patterns.md, "TRS jack: MIDI out"):
 *  - bytes: each packet by its CIN (8..E channel messages, C / D two bytes, F one realtime byte, 2 / 3 system common);
 *    SysEx and a packet whose status does not match its CIN are dropped; no running status;
 *  - a 256-byte ring of whole messages (a full ring drops the message, never half of it), drained from the 10 kHz
 *    TIMER5 tick (nested in the audio render too): one byte when the last has gone, never a wait loop (~2500 B/s);
 *  - a byte not gone in 30 ticks (3 ms) is given up; after 4 in a row the jack goes quiet until MIDI OUT is set again.
 * Not yet confirmed on a unit. */
#include "smk_uart_tx.h"

static volatile uint8_t smk_out_mode;                 /* MIDI OUT: 0 ALL (USB and TRS), 1 USB, 2 TRS */
static volatile uint8_t smk_out_ch;                   /* MIDI OUT CH: 0..15 (shown 1..16) */

/* the controls as a MIDI controller (the owner's, 2026-11-09): K1..K8 and the four faders send fixed CCs on MIDI OUT CH,
 * whatever they do in FMini-1 (CCs FMini-1's MIDI IN CC list does not use out of the box, so a loop back does nothing);
 * the 16 pads send notes 36..51 on channel 10 (the bottom row 36..43, the top row 44..51, as drum pads), with their
 * velocity and release. The keys go as they always did (seq.c key_on, ext_keys_block). The knobs are encoders: each
 * keeps a value 0..127 (from 64) that its turns move; K1 is the virtual MASTER pot: its 0..127 */
#define SMK_CC_KNOB0 102u                             /* K1..K8: CC 102..109 */
#define SMK_CC_FADER0 110u                            /* faders 1..4: CC 110..113 */
#define SMK_PAD_CH 9u                                 /* channel 10 */
#define SMK_PAD_NOTE(p) ((p) < 8u ? 44u + (p) : 36u + (p) - 8u)   /* hardware pads 0..7 are the top row */
static uint8_t smk_knob_v[8] = {64, 64, 64, 64, 64, 64, 64, 64};
/* the main loop's MIDI OUT messages, to the audio ISR (midi_out_event has one producer: the audio ISR) */
#define SMK_UQ 64u
static volatile uint32_t smk_uq[SMK_UQ];
static volatile uint32_t smk_uq_w, smk_uq_r;
static void smk_out_ui(uint32_t pkt)                  /* the main loop */
{
    uint32_t w = smk_uq_w;
    if (w - smk_uq_r >= SMK_UQ)
        return;                                       /* full: dropped */
    smk_uq[w % SMK_UQ] = pkt;
    __asm__ volatile("" ::: "memory");
    smk_uq_w = w + 1u;
}
static int smk_out_ui_take(uint32_t *pkt)             /* the audio ISR */
{
    uint32_t r = smk_uq_r;
    if (r == smk_uq_w)
        return 0;
    *pkt = smk_uq[r % SMK_UQ];
    __asm__ volatile("" ::: "memory");
    smk_uq_r = r + 1u;
    return 1;
}
static void smk_out_cc(uint32_t cc, uint32_t v)       /* the main loop: a CC on MIDI OUT CH */
{
    smk_out_ui(0x0Bu | (0xB0u | (smk_out_ch & 15u)) << 8 | (cc & 0x7Fu) << 16 | (v & 0x7Fu) << 24);
}
static void smk_out_knob(uint32_t enc, int32_t s)     /* encoder enc (0 = K1 .. 7 = K8) turned s detents (panel_enc) */
{
    int32_t v;
    if (enc >= 8u || !s)
        return;
    v = (int32_t)smk_knob_v[enc] + s;
    v = v < 0 ? 0 : v > 127 ? 127 : v;
    if ((uint32_t)v == smk_knob_v[enc])
        return;
    smk_knob_v[enc] = (uint8_t)v;
    smk_out_cc(SMK_CC_KNOB0 + enc, (uint32_t)v);
}

#define TRS_RING 256u
static volatile uint8_t trs_ring[TRS_RING];
static volatile uint32_t trs_w, trs_r;               /* the producer (midi_out_event) / the consumer (TIMER5) */
static uint8_t trs_on, trs_busy, trs_wait, trs_fail;
static volatile uint8_t trs_dead;

static void smk_trs_init(void)                        /* main.c, before timer5_start() */
{
    smk_uart1_tx_init();
    trs_on = 1;
}

static void smk_trs_reset(void)                       /* MIDI OUT set again: a jack given up may try again */
{
    trs_fail = 0;
    trs_dead = 0;
}

static void smk_trs_put(uint32_t pkt)                 /* a USB-MIDI packet (midi_out_event: the audio ISR) */
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu, n, i, w;
    uint8_t b[3];
    if (!trs_on || trs_dead)
        return;
    if (cin >= 8u && cin <= 0xEu) {
        if ((st >> 4) != cin)
            return;                                   /* (a track with MIDI OFF can make status 0xFF) */
        n = (cin == 0xCu || cin == 0xDu) ? 2u : 3u;
    } else if (cin == 0xFu) {
        if (st < 0xF8u)
            return;
        n = 1u;
    } else if (cin == 2u || cin == 3u) {
        if (st < 0xF1u || st >= 0xF7u)
            return;
        n = cin;
    } else
        return;                                       /* SysEx and the rest */
    b[0] = (uint8_t)st;
    b[1] = (uint8_t)((pkt >> 16) & 0x7Fu);
    b[2] = (uint8_t)((pkt >> 24) & 0x7Fu);
    w = trs_w;
    if (TRS_RING - (w - trs_r) < n)
        return;                                       /* full: the whole message is dropped */
    for (i = 0; i < n; i++)
        trs_ring[(w + i) % TRS_RING] = b[i];
    __asm__ volatile("" ::: "memory");
    trs_w = w + n;
}

static void smk_trs_tick(void)                        /* TIMER5, every tick (also nested in the audio render) */
{
    uint32_t r;
    if (!trs_on || trs_dead)
        return;
    if (trs_busy) {
        if (!smk_uart1_tx_done()) {
            if (++trs_wait < 30u)
                return;
            if (++trs_fail >= 4u) {                   /* no UART answering: quiet until MIDI OUT is set again */
                trs_dead = 1;
                trs_busy = 0;
                trs_r = trs_w;
                return;
            }
        } else
            trs_fail = 0;
        trs_busy = 0;
    }
    r = trs_r;
    if (r == trs_w)
        return;
    smk_uart1_tx_byte(trs_ring[r % TRS_RING]);
    trs_r = r + 1u;
    trs_busy = 1;
    trs_wait = 0;
}
