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

    printf(fails ? "USB playback store: %d FAILED\n" : "uac_out: all ok\n", fails);
    return fails != 0;
}
