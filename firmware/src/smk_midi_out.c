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
