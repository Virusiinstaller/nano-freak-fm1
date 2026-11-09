/* SPDX-License-Identifier: GPL-3.0-only */
/* SMK-37 TRS MIDI OUT (firmware/src/smk_midi_out.c): USB-MIDI packets to bytes, the ring, the pace, the give-up.
 *   cc -Itests/smk_fake -o smk_midi_out_test tests/smk_midi_out_test.c   (the fake UART first on the include path) */
#include <stdio.h>
#include <string.h>
#include "../firmware/src/smk_midi_out.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint32_t pkt(uint32_t cin, uint32_t st, uint32_t d1, uint32_t d2) { return cin | st << 8 | d1 << 16 | d2 << 24; }
static void drain(uint32_t ticks) { while (ticks--) smk_trs_tick(); }
static void reset_all(void)
{
    trs_w = trs_r = 0; trs_busy = trs_wait = trs_fail = 0; trs_dead = 0; t_n = 0; t_done = 1;
}

int main(void)
{
    static const uint8_t want[] = {0x90, 60, 100, 0x80, 60, 0, 0xE0, 0x00, 0x40, 0xB3, 1, 64, 0xC0, 5, 0xD0, 7, 0xF8};
    uint32_t i;
    CHECK(!t_init, "the UART set up before smk_trs_init");
    smk_trs_put(pkt(9, 0x90, 60, 100));
    CHECK(trs_w == 0, "before init nothing is queued");
    smk_trs_init();
    CHECK(t_init == 1, "smk_trs_init sets the UART up once");
    reset_all();
    smk_trs_put(pkt(9, 0x90, 60, 100));
    smk_trs_put(pkt(8, 0x80, 60, 0));
    smk_trs_put(pkt(0xE, 0xE0, 0x00, 0x40));
    smk_trs_put(pkt(0xB, 0xB3, 1, 64));
    smk_trs_put(pkt(0xC, 0xC0, 5, 0));
    smk_trs_put(pkt(0xD, 0xD0, 7, 0));
    smk_trs_put(pkt(0xF, 0xF8, 0, 0));
    smk_trs_put(pkt(4, 0xF0, 0x7E, 0x7F));                 /* SysEx: dropped */
    smk_trs_put(pkt(9, 0xFF, 1, 2));                       /* a status that does not match its CIN: dropped */
    smk_trs_put(pkt(0xF, 0x90, 0, 0));                     /* CIN F with a channel status: dropped */
    drain(200);
    CHECK(t_n == sizeof want, "bytes sent %u, want %u", t_n, (unsigned)sizeof want);
    CHECK(!memcmp(t_sent, want, sizeof want), "the bytes differ");

    /* one byte per tick while the UART says done; none while it is busy */
    reset_all();
    t_done = 0;
    smk_trs_put(pkt(9, 0x90, 60, 100));
    smk_trs_tick();
    CHECK(t_n == 1, "the first byte goes at once (%u)", t_n);
    drain(10);
    CHECK(t_n == 1, "no byte while the last has not gone (%u)", t_n);
    t_done = 1;
    drain(2);
    CHECK(t_n == 3, "the rest once it has gone (%u)", t_n);

    /* a full ring drops whole messages */
    reset_all();
    for (i = 0; i < 100u; i++)
        smk_trs_put(pkt(9, 0x90, i & 0x7Fu, 100));
    CHECK(trs_w - trs_r == 255u, "a full ring holds whole messages only (%u)", trs_w - trs_r);
    drain(1000);
    CHECK(t_n == 255u && t_sent[252] == 0x90, "the queued messages go out whole (%u)", t_n);

    /* a UART that never finishes: 4 bytes given up, then quiet until MIDI OUT is set again */
    reset_all();
    t_done = 0;
    for (i = 0; i < 10u; i++)
        smk_trs_put(pkt(9, 0x90, 60, 100));
    drain(30u * 6u);
    CHECK(trs_dead, "the jack goes quiet after 4 bytes in a row not gone");
    CHECK(t_n <= 5u, "it stops sending (%u)", t_n);
    i = t_n;
    smk_trs_put(pkt(9, 0x90, 60, 100));
    drain(100);
    CHECK(t_n == i && trs_w == trs_r, "nothing queued or sent while quiet");
    smk_trs_reset();
    t_done = 1;
    smk_trs_put(pkt(9, 0x90, 61, 100));
    drain(10);
    CHECK(!trs_dead && t_n == i + 3u, "MIDI OUT set again: the jack sends again (%u)", t_n - i);

    /* the knobs: a value 0..127 from 64 per encoder, one CC on MIDI OUT CH each move, none at an end */
    {
        uint32_t pk;
        smk_out_ch = 2;
        smk_out_knob(4, 3);                                /* K5 */
        CHECK(smk_out_ui_take(&pk) && pk == (0x0Bu | 0xB2u << 8 | 106u << 16 | 67u << 24), "K5 +3: CC106 = 67 on channel 3 (%08x)", pk);
        smk_out_knob(4, -100);
        CHECK(smk_out_ui_take(&pk) && (pk >> 24) == 0u, "K5 down to 0");
        smk_out_knob(4, -1);
        CHECK(!smk_out_ui_take(&pk), "K5 at 0: nothing more");
        smk_out_cc(SMK_CC_FADER0 + 3u, 127u);
        CHECK(smk_out_ui_take(&pk) && ((pk >> 16) & 0x7Fu) == 113u, "fader 4: CC113");
        CHECK(SMK_PAD_NOTE(8) == 36u && SMK_PAD_NOTE(15) == 43u && SMK_PAD_NOTE(0) == 44u && SMK_PAD_NOTE(7) == 51u,
              "pads: bottom row 36..43, top row 44..51");
    }

    if (fails)
        return 1;
    printf("smk_midi_out_test: ok\n");
    return 0;
}
