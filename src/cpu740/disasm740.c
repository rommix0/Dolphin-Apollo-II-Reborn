#include <stdio.h>
#include "cpu740/cpu740.h"
#include "cpu740/optable.h"

int cpu740_disasm(uint16_t pc, cpu740_peek_fn peek, void *ctx, char *out, size_t outlen)
{
    uint8_t op = peek(ctx, pc);
    const cpu740_opinfo info = cpu740_optable()[op];
    uint8_t b1 = peek(ctx, (uint16_t)(pc + 1));
    uint8_t b2 = peek(ctx, (uint16_t)(pc + 2));
    uint16_t word = (uint16_t)(b1 | b2 << 8);
    uint16_t rel2 = (uint16_t)(pc + 2 + (int8_t)b1);   /* target of a 2-byte relative branch */
    uint16_t rel3 = (uint16_t)(pc + 3 + (int8_t)b2);   /* target of BBS/BBC n,$zz,rel */
    const char *n = cpu740_mnem_name(info.mnem);
    int bit = op >> 5;

    if (info.mnem == M_ILL) {
        snprintf(out, outlen, "DB $%02X", op);
        return 1;
    }
    switch (info.mode) {
    case AM_IMP:        snprintf(out, outlen, "%s", n); break;
    case AM_ACC:        snprintf(out, outlen, "%s A", n); break;
    case AM_IMM:        snprintf(out, outlen, "%s #$%02X", n, b1); break;
    case AM_ZP:         snprintf(out, outlen, "%s $%02X", n, b1); break;
    case AM_ZPX:        snprintf(out, outlen, "%s $%02X,X", n, b1); break;
    case AM_ZPY:        snprintf(out, outlen, "%s $%02X,Y", n, b1); break;
    case AM_ABS:        snprintf(out, outlen, "%s $%04X", n, word); break;
    case AM_ABX:        snprintf(out, outlen, "%s $%04X,X", n, word); break;
    case AM_ABY:        snprintf(out, outlen, "%s $%04X,Y", n, word); break;
    case AM_IND:        snprintf(out, outlen, "%s ($%04X)", n, word); break;
    case AM_IZX:        snprintf(out, outlen, "%s ($%02X,X)", n, b1); break;
    case AM_IZY:        snprintf(out, outlen, "%s ($%02X),Y", n, b1); break;
    case AM_REL:        snprintf(out, outlen, "%s $%04X", n, rel2); break;
    case AM_ZPI:        snprintf(out, outlen, "%s ($%02X)", n, b1); break;
    case AM_SPG:        snprintf(out, outlen, "%s \\$FF%02X", n, b1); break;
    case AM_LDM:        snprintf(out, outlen, "%s #$%02X,$%02X", n, b1, b2); break;
    case AM_BIT_A:      snprintf(out, outlen, "%s%d A", n, bit); break;
    case AM_BIT_ZP:     snprintf(out, outlen, "%s%d $%02X", n, bit, b1); break;
    case AM_BIT_A_REL:  snprintf(out, outlen, "%s%d A,$%04X", n, bit, rel2); break;
    case AM_BIT_ZP_REL: snprintf(out, outlen, "%s%d $%02X,$%04X", n, bit, b1, rel3); break;
    default:            snprintf(out, outlen, "%s ?", n); break;
    }
    return cpu740_mode_length(info.mode);
}
