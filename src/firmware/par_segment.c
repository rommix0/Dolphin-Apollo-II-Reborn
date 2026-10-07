#include "par_segment.h"
#include "fw_ops.h"

/* par_scale_voicing, one helper per @B block that keeps a term in FW_TMP_76; v is not 0 at @B2 and @B3 (their
   BEQ $BFD1 gives 2 first), and may be 0 at @B4, @B6 and @B7. */

/* $BFE5-$BFEE (@B2): v/4 in FW_TMP_76, then v/16 + FW_TMP_76 + 1 (SEC; ADC). */
static uint8_t voicing_b2(fw *f, uint8_t v)
{
    fw_wr8(f, FW_TMP_76, (uint8_t)(v >> 2));
    uint8_t c = 1;                                           /* SEC */
    return fw_adc((uint8_t)(v >> 4), fw_rd8(f, FW_TMP_76), &c);
}

/* $BFF6-$BFFE (@B3): v/4 in FW_TMP_76, then v/8 + FW_TMP_76 (CLC; ADC). */
static uint8_t voicing_b3(fw *f, uint8_t v)
{
    fw_wr8(f, FW_TMP_76, (uint8_t)(v >> 2));
    uint8_t c = 0;                                           /* CLC */
    return fw_adc((uint8_t)(v >> 3), fw_rd8(f, FW_TMP_76), &c);
}

/* $C004-$C014 (@B4): v/4 in FW_TMP_76; v/8 (PHA) + FW_TMP_76 back into FW_TMP_76; then v/16 (PLA; LSR) +
   FW_TMP_76. */
static uint8_t voicing_b4(fw *f, uint8_t v)
{
    fw_wr8(f, FW_TMP_76, (uint8_t)(v >> 2));
    uint8_t c = 0;                                           /* CLC */
    fw_wr8(f, FW_TMP_76, fw_adc((uint8_t)(v >> 3), fw_rd8(f, FW_TMP_76), &c));
    c = 0;                                                   /* CLC */
    return fw_adc((uint8_t)(v >> 4), fw_rd8(f, FW_TMP_76), &c);
}

/* $C021-$C029 (@B6): v/2 in FW_TMP_76, then v/8 + FW_TMP_76. */
static uint8_t voicing_b6(fw *f, uint8_t v)
{
    fw_wr8(f, FW_TMP_76, (uint8_t)(v >> 1));
    uint8_t c = 0;                                           /* CLC */
    return fw_adc((uint8_t)(v >> 3), fw_rd8(f, FW_TMP_76), &c);
}

/* $C02F-$C036 (@B7): v/2 in FW_TMP_76, then v/4 + FW_TMP_76. */
static uint8_t voicing_b7(fw *f, uint8_t v)
{
    fw_wr8(f, FW_TMP_76, (uint8_t)(v >> 1));
    uint8_t c = 0;                                           /* CLC */
    return fw_adc((uint8_t)(v >> 2), fw_rd8(f, FW_TMP_76), &c);
}

void par_next_row(fw *f, fw_regs *r)
{
    uint8_t c = 0;                                           /* CLC */
    r->a = fw_adc(fw_rd8(f, PAR_ROW_PTR), fw_rd8(f, PAR_ROW_LEN), &c);
    fw_wr8(f, PAR_ROW_PTR, r->a);
    if (c)                                                   /* BCC $BD13 */
        fw_inc8(f, PAR_ROW_PTR + 1);
}

void par_next_param(fw *f, fw_regs *r)
{
    uint8_t c = 0;                                           /* CLC */
    fw_wr8(f, PAR_ROW_PTR, fw_adc(fw_rd8(f, PAR_ROW_PTR), fw_rd8(f, PAR_PARAM_STRIDE), &c));
    r->a = fw_adc(fw_rd8(f, PAR_ROW_PTR + 1), fw_rd8(f, PAR_PARAM_STRIDE + 1), &c);
    fw_wr8(f, PAR_ROW_PTR + 1, r->a);
}

void par_clamp(fw *f, fw_regs *r)
{
    (void)f;
    if ((uint8_t)(r->a - 1) & 0x80)                          /* CMP #1; BPL $BD2B */
        r->a = PAR_CLAMP_MIN;
    else if (!((uint8_t)(r->a - 0x40) & 0x80))               /* CMP #$40; BPL $BD30 */
        r->a = PAR_CLAMP_MAX;
}

void par_scale_voicing(fw *f, fw_regs *r)
{
    uint8_t v = r->a;
    if (v == 8 || v == 1)                                    /* $BFC3-$BFC9: BEQ $BFD3 */
        return;
    switch (fw_rd8(f, CMD_VOICING)) {                        /* PHA; LDA cmd_voicing; CMP #n; BNE; PLA */
    case 0:                                                  /* $BFD0-$BFD3 */
        r->a = PAR_VOICING_FLOOR;
        break;
    case 1:                                                  /* $BFD8-$BFDD */
        r->a = v == 0 ? PAR_VOICING_FLOOR : (uint8_t)(v >> 2);
        break;
    case 2:                                                  /* $BFE2-$BFEE */
        r->a = v == 0 ? PAR_VOICING_FLOOR : voicing_b2(f, v);
        break;
    case 3:                                                  /* $BFF3-$BFFE */
        r->a = v == 0 ? PAR_VOICING_FLOOR : voicing_b3(f, v);
        break;
    case 4:                                                  /* $C003-$C014: no test for 0 */
        r->a = voicing_b4(f, v);
        break;
    case 5:                                                  /* $C019-$C01B */
        r->a = (uint8_t)(v >> 1);
        break;
    case 6:                                                  /* $C020-$C029 */
        r->a = voicing_b6(f, v);
        break;
    case 7:                                                  /* $C02E-$C036 */
        r->a = voicing_b7(f, v);
        break;
    default:                                                 /* $C037-$C038: PLA, v unchanged */
        break;
    }
}
