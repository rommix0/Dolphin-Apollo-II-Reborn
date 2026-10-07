#ifndef FW_OPS_H
#define FW_OPS_H

/* Small steps the ported routines share (M4.4a spec 2.6). */

#include "fw.h"
#include "tn_convert.h"   /* ROM_ISR_PAR_READY */

/* JSR isr_par_ready with the registers the ROM has at the JSR (it preserves A, X and Y). */
static inline void fw_par_ready(fw *f, fw_regs r) { fw_rom_call(f, ROM_ISR_PAR_READY, &r, FW_ROM_CALL_BUDGET); }

/* ADC: a + m + the carry in *c; the carry out back in *c. */
static inline uint8_t fw_adc(uint8_t a, uint8_t m, uint8_t *c)
{
    unsigned s = (unsigned)a + m + *c;
    *c = (uint8_t)(s >> 8);
    return (uint8_t)s;
}

/* INC addr: mod 256. */
static inline void fw_inc8(fw *f, uint16_t addr) { fw_wr8(f, addr, (uint8_t)(fw_rd8(f, addr) + 1)); }

/* LDA arr,X; CLC; ADC #n; STA arr,X: an 8-bit add into an array byte. */
static inline void fw_add8(fw *f, uint16_t arr, uint8_t x, uint8_t n)
{
    fw_wr8(f, (uint16_t)(arr + x), (uint8_t)(fw_rd8(f, (uint16_t)(arr + x)) + n));
}

#endif
