/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/smk_midi_out_test.c: a UART that records what is sent; tx_done follows t_done */
#pragma once
#include <stdint.h>
static uint8_t t_sent[1024];
static uint32_t t_n, t_init;
static int t_done = 1;
static void smk_uart1_tx_init(void) { t_init++; }
static void smk_uart1_tx_byte(uint32_t b) { t_sent[t_n++ % 1024u] = (uint8_t)b; }
static uint32_t smk_uart1_tx_done(void) { return (uint32_t)t_done; }
