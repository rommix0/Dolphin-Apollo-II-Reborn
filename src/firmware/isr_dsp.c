#include "isr_dsp.h"
#include "fw_ops.h"

#define P3_KEPT  0x87   /* isr_set_volume $ACF1 LDA #$87 / AND P3: the bits the volume leaves */

/* ---- isr_set_volume $ACE8, isr_volume_half $B46C ---- */

void isr_set_volume(fw *f, fw_regs *r)
{
    fw_wr8(f, FW_TMP_75, util_shl3((uint8_t)((r->a ^ 0x0F) & 0x0F)));   /* EOR #$F; AND #$F; JSR util_shl3; STA $75 */
    r->a = (uint8_t)((fw_sfr8(f, SFR_P3) & P3_KEPT) | fw_rd8(f, FW_TMP_75));   /* LDA #$87; AND P3; ORA $75 */
    fw_wr_sfr8(f, SFR_P3, r->a);                                          /* STA P3 */
}

void isr_volume_half(fw *f, fw_regs *r)
{
    r->a = (uint8_t)(fw_rd8(f, CMD_VOLUME) >> 1);                         /* LDA cmd_volume; LSR A */
    isr_set_volume(f, r);
}
