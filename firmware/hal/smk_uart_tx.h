/* SPDX-License-Identifier: GPL-3.0-only */
/* SMK-37: the TRS jack is a MIDI OUT, UART1 TX on PC7, 31250 baud (the FM-1's TRS is a MIDI IN on PH8, a key-matrix row
 * on the SMK: never set that up here). The setup is the stock firmware's, as the fm1-to-smk37 kit describes it
 * (docs/01-hardware.md section 9, docs/08-pitfalls.md): IOMAP_CON3 bit 7 clear and [23:20] = 1 (UT1 TX onto an output
 * channel); PC7 pull-up and pull-down, output low, input enabled; UART1 CON0 0x3400 (pendings cleared), the baud from
 * the 48 MHz clock, then UTEN, no RX. Not yet confirmed on a unit.
 *
 *   smk_uart1_tx_init()      once, before TIMER5 starts
 *   smk_uart1_tx_byte(b)     send one byte (clears TPND first); never waits
 *   smk_uart1_tx_done()      1 when the last byte has gone (CON0 bit 15, TPND)
 */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_gpio.h"

#define SMK_UT1_CON0  (*(volatile uint32_t *)0x12100u)
#define SMK_UT1_CON1  (*(volatile uint32_t *)0x12104u)
#define SMK_UT1_BAUD  (*(volatile uint32_t *)0x12108u)
#define SMK_UT1_BUF   (*(volatile uint32_t *)0x1210Cu)
#define SMK_CLK_CON1  (*(volatile uint32_t *)0x10010u)   /* [11:10] UART clock: 1 = PLL48M */
#define SMK_IOMAP3    (*(volatile uint32_t *)0x51028u)   /* UT1: b7 fixed IO, [23:20] TX output channel */
#define SMK_TX_PIN    (1u << 7)                         /* PC7 */

FM1_INLINE void smk_uart1_tx_init(void)
{
    SMK_UT1_CON0 = 0x3400u;                             /* off, pendings cleared */
    SMK_UT1_CON1 = 0;
    SMK_CLK_CON1 = (SMK_CLK_CON1 & ~(3u << 10)) | (1u << 10);
    SMK_IOMAP3 = (SMK_IOMAP3 & ~(0x80u | (0xFu << 20))) | (1u << 20);
    FM1_PR(FM1_PC, FM1_PU) |= SMK_TX_PIN;
    FM1_PR(FM1_PC, FM1_PD) |= SMK_TX_PIN;
    FM1_PR(FM1_PC, FM1_OUT) &= ~SMK_TX_PIN;
    FM1_PR(FM1_PC, FM1_DIR) &= ~SMK_TX_PIN;              /* an output */
    FM1_PR(FM1_PC, FM1_DIE) |= SMK_TX_PIN;
    SMK_UT1_BAUD = 48000000u / 31250u / 4u - 1u;        /* 383 */
    __asm__ volatile("csync" ::: "memory");
    SMK_UT1_CON0 = 0x3400u;
    __asm__ volatile("csync" ::: "memory");
    SMK_UT1_CON0 |= 1u;                                 /* UTEN last; no RX, no IRQ */
}

FM1_INLINE void smk_uart1_tx_byte(uint32_t b)
{
    SMK_UT1_CON0 |= 1u << 13;                           /* clear TPND */
    SMK_UT1_BUF = b & 0xFFu;
}

FM1_INLINE uint32_t smk_uart1_tx_done(void)
{
    return (SMK_UT1_CON0 >> 15) & 1u;
}
