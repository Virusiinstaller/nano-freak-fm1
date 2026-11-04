/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Guilherme Ruppert (FMini-1 fork) */
/* Host test of the USB audio playback store in src/usb.c (FELUCCA_UAC_OUT, step 2): uac_out_store turns an
 * EP3 OUT packet (16-bit stereo, little endian) into pb_ring frames (L | R << 16). Checked here: the byte
 * layout, a partial frame (n not a multiple of 4), the largest packet, wrap-around of the ring index, a full
 * ring (the packet is dropped whole and the frames already in the ring stay intact), and the room that the
 * reader (step 3) gives back by advancing pb_r. The SIE (uac_out_rx, uac_out_stream) is not touched. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 0
#define FELUCCA_CDC 0
#define FELUCCA_UAC 1
#define FELUCCA_UAC_OUT 1
#define HALF_FRAMES 128
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"   /* SIE register macros (never touched here) */
#include "../firmware/src/usb.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
}

/* a packet of f frames: frame i = (L, R) = (i * 3 + 1, -(i + 1)), 16 bits each */
static uint8_t pkt[UAC_MAXP];
static void make_packet(uint32_t f)
{
    uint32_t i;
    for (i = 0; i < f; i++) {
        uint16_t l = (uint16_t)(i * 3u + 1u), r = (uint16_t)(0u - (i + 1u));
        pkt[4u * i] = (uint8_t)l;
        pkt[4u * i + 1u] = (uint8_t)(l >> 8);
        pkt[4u * i + 2u] = (uint8_t)r;
        pkt[4u * i + 3u] = (uint8_t)(r >> 8);
    }
}

static uint32_t want(uint32_t i)
{
    uint16_t l = (uint16_t)(i * 3u + 1u), r = (uint16_t)(0u - (i + 1u));
    return (uint32_t)l | (uint32_t)r << 16;
}

static void reset_ring(void)
{
    pb_w = 0;
    pb_r = 0;
    pb_frames = 0;
}

int main(void)
{
    uint32_t i, f, ok_all;

    /* 1. the byte layout: one frame, L = 0x1234, R = 0xFFFE (-2) */
    reset_ring();
    pkt[0] = 0x34; pkt[1] = 0x12; pkt[2] = 0xFE; pkt[3] = 0xFF;
    check("one frame stored (4 bytes)", uac_out_store(pkt, 4) == 1 && pb_w == 1);
    check("frame = L (low 16 bits) | R << 16, little endian", pb_ring[0] == (0x1234u | 0xFFFEu << 16));

    /* 2. a partial frame: 7 bytes give one frame, the 3 bytes of the rest are not kept */
    reset_ring();
    make_packet(2);
    check("7 bytes: one whole frame, the rest dropped (pb_w 1)", uac_out_store(pkt, 7) == 1 && pb_w == 1);

    /* 3. the largest packet (49 frames, UAC_MAXP bytes): every frame in order */
    reset_ring();
    make_packet(UAC_MAXP / 4u);
    check("largest packet (49 frames) stored", uac_out_store(pkt, UAC_MAXP) == 1 && pb_w == UAC_MAXP / 4u);
    ok_all = 1;
    for (i = 0; i < UAC_MAXP / 4u; i++)
        ok_all &= pb_ring[i] == want(i);
    check("49 frames: each frame = its packet's frame, in order", ok_all);

    /* 4. wrap-around: the index starts 3 short of the end of the ring; 10 frames wrap over it */
    reset_ring();
    pb_w = PB_N - 3u;
    pb_r = pb_w;
    make_packet(10);
    check("10 frames at the end of the ring: stored", uac_out_store(pkt, 40) == 1 && pb_w == PB_N + 7u);
    ok_all = 1;
    for (i = 0; i < 10; i++)
        ok_all &= pb_ring[(PB_N - 3u + i) & (PB_N - 1u)] == want(i);
    check("the frames wrap: slots PB_N-3 .. PB_N-1, 0 .. 6, in order", ok_all);

    /* 5. a full ring: fill it with 49-frame packets; a packet that does not fit is dropped whole */
    reset_ring();
    make_packet(UAC_MAXP / 4u);
    f = 0;
    while (uac_out_store(pkt, UAC_MAXP))
        f++;
    check("the ring takes packets until fewer than 49 frames are free", pb_w - pb_r > PB_N - 49u && pb_w <= PB_N);
    {
        uint32_t before = pb_w, frames_before = pb_frames;
        int dropped = uac_out_store(pkt, UAC_MAXP) == 0;
        check("a packet that does not fit is dropped: nothing written, pb_w unchanged",
              dropped && pb_w == before && pb_frames == frames_before);
        make_packet(1);
        check("a one-frame packet still fits in the free slots (if any)",
              PB_N - (pb_w - pb_r) == 0 || uac_out_store(pkt, 4) == 1);
    }
    ok_all = 1;
    for (i = 0; i < 49; i++)
        ok_all &= pb_ring[i] == want(i);
    check("the frames stored before the drop are intact", ok_all);

    /* 6. the reader gives room back by advancing pb_r (step 3): a full packet fits again */
    pb_r += 100u;
    make_packet(UAC_MAXP / 4u);
    check("after the reader took 100 frames, a 49-frame packet fits", uac_out_store(pkt, UAC_MAXP) == 1);

    /* ---- the reader (audio ISR, usb_play_mix) ---- */
    {
        static int32_t out[2 * 64];
        uint32_t k;
        int32_t l0, r0;

        /* 7. nothing plays before PB_PRIME frames; then the frames, at MASTER's level, in order */
        reset_ring();
        uac_out_alt = 1;
        pb_play = 0;
        make_packet(UAC_MAXP / 4u);
        for (k = 0; k < 2; k++)
            uac_out_store(pkt, UAC_MAXP);                  /* 98 frames */
        memset(out, 0, sizeof out);
        usb_play_mix(out, 32, 4096);
        ok_all = 1;
        for (i = 0; i < 64; i++)
            ok_all &= out[i] == 0;
        check("98 frames (under PB_PRIME 128): nothing plays", ok_all && pb_play == 0 && pb_r == 0);

        for (k = 0; k < 2; k++)
            uac_out_store(pkt, 4 * 15);                    /* 98 + 30 = 128 */
        usb_play_mix(out, 32, 4096);
        check("128 frames: the reader starts and takes 32", pb_play == 1 && pb_r == 32);
        ok_all = 1;
        for (i = 0; i < 32; i++) {
            l0 = (int32_t)(int16_t)(pb_ring[i] & 0xFFFFu);
            r0 = (int32_t)(int16_t)(pb_ring[i] >> 16);
            ok_all &= out[2 * i] == l0 && out[2 * i + 1] == r0;
        }
        check("MASTER 4096: each frame added as it is, in order", ok_all);

        /* 8. MASTER 2048: half; the sum is saturated at +-32767 */
        reset_ring();
        pb_play = 1;
        make_packet(UAC_MAXP / 4u);
        for (k = 0; k < 4; k++)
            uac_out_store(pkt, UAC_MAXP);                  /* 196 frames, in the middle of the band */
        memset(out, 0, sizeof out);
        usb_play_mix(out, 32, 2048);
        ok_all = 1;
        for (i = 0; i < 32; i++) {
            l0 = (int32_t)(int16_t)(pb_ring[i] & 0xFFFFu);
            ok_all &= out[2 * i] == ((l0 * 2048) >> 12);
        }
        check("MASTER 2048: half the level (arithmetic shift)", ok_all);
        for (i = 0; i < 64; i++)                           /* the reader is at frame 32: its L is 97, its R -33 */
            out[i] = (i & 1u) ? -32760 : 32760;
        usb_play_mix(out, 32, 4096);
        check("a loud DAC signal plus the playback: saturated at +32767 / -32768",
              out[0] == 32767 && out[1] == -32768);

        /* 9. the host is fast: one frame dropped while the fill is above TARGET + BAND */
        reset_ring();
        pb_play = 1;
        make_packet(UAC_MAXP / 4u);
        for (k = 0; k < 9; k++)
            uac_out_store(pkt, UAC_MAXP);                  /* 441 frames > 384 */
        memset(out, 0, sizeof out);
        usb_play_mix(out, 32, 4096);
        check("fill 441 (above 384): one frame dropped, the reader takes 33", pb_skip == 1 && pb_r == 33);

        /* 10. the host is slow: the last frame repeated for one frame when the fill is below 128 */
        reset_ring();
        pb_play = 1;
        pb_last_l = -7;
        pb_last_r = 9;
        make_packet(UAC_MAXP / 4u);
        for (k = 0; k < 2; k++)
            uac_out_store(pkt, UAC_MAXP);                  /* 98 frames < 128 */
        memset(out, 0, sizeof out);
        usb_play_mix(out, 32, 4096);
        check("fill 98 (below 128): the first frame repeats, the reader takes 31",
              pb_rep == 1 && pb_r == 31 && out[0] == -7 && out[1] == 9);
        check("... and the frames after it are the ring's, in order",
              out[2] == (int32_t)(int16_t)(pb_ring[0] & 0xFFFFu) && out[2 * 31] == (int32_t)(int16_t)(pb_ring[30] & 0xFFFFu));

        /* 11. the ring runs dry mid-block: silence for the rest, and primes again */
        reset_ring();
        pb_play = 1;
        make_packet(20);
        uac_out_store(pkt, 80);                            /* 20 frames: below 128, so block 0 repeats one */
        memset(out, 0, sizeof out);
        usb_play_mix(out, 32, 4096);
        check("20 frames, 32 asked: the frames then silence, pb_under 1, not playing",
              pb_under == 1 && pb_play == 0 && pb_r == 20 && out[2 * 20] != 0 && out[2 * 21] == 0);

        /* 12. SET_INTERFACE / reset: the flag makes the reader start empty, from the writer's index */
        reset_ring();
        pb_play = 1;
        make_packet(UAC_MAXP / 4u);
        uac_out_store(pkt, UAC_MAXP);
        pb_flush = 1;
        usb_play_mix(out, 32, 4096);
        check("flush: pb_r moves to pb_w, not playing, flag cleared", pb_r == pb_w && pb_play == 0 && pb_flush == 0);

        /* 13. alt 0: the reader plays nothing and consumes nothing */
        reset_ring();
        pb_play = 1;
        make_packet(UAC_MAXP / 4u);
        for (k = 0; k < 4; k++)
            uac_out_store(pkt, UAC_MAXP);
        uac_out_alt = 0;
        memset(out, 0, sizeof out);
        usb_play_mix(out, 32, 4096);
        check("alt 0: nothing consumed, nothing mixed", pb_r == 0 && out[0] == 0 && pb_play == 0);
        uac_out_alt = 1;

        /* 14. a whole session: the host's clock 0.04 % slow (44100 Hz against the DAC's 44117.6 Hz), 10 s of
         * 32-frame blocks: no dry ring, and the frames the reader repeats are about the frames it is short by */
        {
            uint32_t blocks = 0, written = 0, target;
            reset_ring();
            pb_play = 0;
            pb_under = pb_rep = pb_skip = 0;
            while (blocks < 13786u) {                      /* 10 s at 44117.6 Hz, 32 frames a block */
                target = (uint32_t)((uint64_t)(blocks + 1u) * 32u * 44100u / 44117u);
                while (written < target) {
                    uint32_t f = target - written > 45u ? 45u : target - written;
                    make_packet(f);
                    uac_out_store(pkt, 4u * f);
                    written += f;
                }
                usb_play_mix(out, 32, 4096);
                blocks++;
            }
            /* the drift of 17.6 frames a second, 10 s: about 176 frames more are read than written */
            check("10 s, host 0.04 % slow: no dry ring", pb_under == 0 && pb_play == 1);
            check("10 s: the repeats follow the drift (about 176 frames, +-25 %)",
                  pb_rep >= 132u && pb_rep <= 220u && pb_skip == 0);
        }
    }

    printf(fails ? "USB playback store: %d FAILED\n" : "uac_out: all ok\n", fails);
    return fails != 0;
}
