/* SPDX-License-Identifier: GPL-3.0-only */
/* SMK-37 encoders (firmware/hal/smk_input.h smk__encoders): detents at any speed, bounce, a missed state, both ways.
 *   cc -DBOARD_SMK37=1 -Ifirmware/hal -o smk_encoder_test tests/smk_encoder_test.c */
#include <stdio.h>
#include "smk_input.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void set_state(uint32_t e, uint32_t st)             /* bit 0 A, bit 1 B: their matrix contacts */
{
    uint32_t i;
    for (i = 0; i < 2u; i++) {
        uint32_t code = SMK_ENCC[e][i];
        if ((st >> i) & 1u)
            fm1_in.raw[code >> 4] |= 1u << (code & 15u);
        else
            fm1_in.raw[code >> 4] &= ~(1u << (code & 15u));
    }
}
static int32_t take(uint32_t e) { int32_t s = fm1_in.enc_steps[e]; fm1_in.enc_steps[e] = 0; return s; }

/* turn encoder e by n detents (sign = way), frames_per_state frames each state (0: some states missed) */
static const uint8_t CW[4] = {0, 2, 3, 1};                 /* the +1 way: 0 -> 2 -> 3 -> 1 -> 0 */
static void turn(uint32_t e, int32_t n, uint32_t fps, uint32_t skip_every)
{
    int32_t i, k = 0, dir = n > 0 ? 1 : -1, steps = (n > 0 ? n : -n) * 4;
    uint32_t f, idx = 0;
    for (i = 0; i < steps; i++) {
        idx = (idx + 4u + (uint32_t)dir) % 4u;
        if (skip_every && ++k % (int32_t)skip_every == 0 && i != steps - 1)
            continue;                                      /* this state is never seen (too fast for the frame) */
        set_state(e, CW[idx]);
        for (f = 0; f < (fps ? fps : 1u); f++)
            smk__encoders();
    }
}

int main(void)
{
    uint32_t e = 3;
    set_state(e, 0);
    smk__encoders();                                       /* the first frame: rest state 0 */
    turn(e, 5, 3, 0);
    CHECK(take(e) == 5, "slow: 5 detents");
    turn(e, -5, 3, 0);
    CHECK(take(e) == -5, "slow back: -5");
    turn(e, 20, 1, 0);
    CHECK(take(e) == 20, "fast (one frame a state): 20");
    turn(e, -20, 1, 0);
    CHECK(take(e) == -20, "fast back: -20");
    turn(e, 30, 1, 3);
    CHECK(take(e) == 30, "faster (every third state missed): 30");
    turn(e, -1, 2, 0);                                     /* (a turn back starts slow: a missed state is read the way
                                                            * the knob was going) */
    turn(e, -29, 1, 2);
    CHECK(take(e) == -30, "faster back (every second state missed): -30");
    {   /* bounce at rest and half a detent there and back: nothing */
        set_state(e, 2); smk__encoders(); set_state(e, 0); smk__encoders();
        set_state(e, 2); smk__encoders(); set_state(e, 3); smk__encoders(); set_state(e, 2); smk__encoders();
        set_state(e, 0); smk__encoders();
        CHECK(take(e) == 0, "bounce / half a detent: nothing");
    }
    {   /* bouncy contacts while turning (each state flickers back once): still one per detent */
        int32_t i;
        for (i = 0; i < 6; i++) {
            set_state(e, 2); smk__encoders(); set_state(e, 0); smk__encoders(); set_state(e, 2); smk__encoders();
            set_state(e, 3); smk__encoders(); set_state(e, 1); smk__encoders(); set_state(e, 3); smk__encoders();
            set_state(e, 1); smk__encoders(); set_state(e, 0); smk__encoders();
        }
        CHECK(take(e) == 6, "bouncy: 6 detents");
    }
    if (fails)
        return 1;
    printf("smk_encoder_test: ok\n");
    return 0;
}
