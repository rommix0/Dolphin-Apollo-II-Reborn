#include "test.h"
#include "cpu740/optable.h"

void suite_optable(void)
{
    const cpu740_opinfo *t = cpu740_optable();
    int legal = 0;
    for (int op = 0; op < 256; op++)
        if (t[op].mnem != M_ILL)
            legal++;
    CHECK_EQ(legal, 231);   /* 151 documented 6502 + 64 bit ops + 16 other 740 opcodes */

    CHECK_STR(cpu740_mnem_name(t[0xA9].mnem), "LDA"); CHECK_EQ(t[0xA9].mode, AM_IMM);
    CHECK_STR(cpu740_mnem_name(t[0x91].mnem), "STA"); CHECK_EQ(t[0x91].mode, AM_IZY);
    CHECK_EQ(t[0x89].mnem, M_ILL);                     /* STA # does not exist */
    CHECK_STR(cpu740_mnem_name(t[0x3C].mnem), "LDM"); CHECK_EQ(cpu740_mode_length(t[0x3C].mode), 3);
    CHECK_STR(cpu740_mnem_name(t[0xEF].mnem), "SEB"); CHECK_EQ(t[0xEF].mode, AM_BIT_ZP);
    CHECK_STR(cpu740_mnem_name(t[0x37].mnem), "BBC"); CHECK_EQ(t[0x37].mode, AM_BIT_ZP_REL);
    CHECK_STR(cpu740_mnem_name(t[0xE3].mnem), "BBS"); CHECK_EQ(t[0xE3].mode, AM_BIT_A_REL);
    CHECK_STR(cpu740_mnem_name(t[0x7B].mnem), "CLB"); CHECK_EQ(t[0x7B].mode, AM_BIT_A);
    CHECK_STR(cpu740_mnem_name(t[0x22].mnem), "JSR"); CHECK_EQ(t[0x22].mode, AM_SPG);
    CHECK_STR(cpu740_mnem_name(t[0x02].mnem), "JSR"); CHECK_EQ(t[0x02].mode, AM_ZPI);
    CHECK_STR(cpu740_mnem_name(t[0xB2].mnem), "JMP"); CHECK_EQ(t[0xB2].mode, AM_ZPI);
    CHECK_STR(cpu740_mnem_name(t[0x1A].mnem), "DEC"); CHECK_EQ(t[0x1A].mode, AM_ACC);
    CHECK_STR(cpu740_mnem_name(t[0x3A].mnem), "INC"); CHECK_EQ(t[0x3A].mode, AM_ACC);
    CHECK_STR(cpu740_mnem_name(t[0x80].mnem), "BRA");
    CHECK_STR(cpu740_mnem_name(t[0xC2].mnem), "WIT");
    CHECK_STR(cpu740_mnem_name(t[0x12].mnem), "CLT");
    CHECK_STR(cpu740_mnem_name(M_DIV), "DIV");
    CHECK_EQ(cpu740_mode_length(AM_IMP), 1);
    CHECK_EQ(cpu740_mode_length(AM_IZX), 2);
    CHECK_EQ(cpu740_mode_length(AM_IND), 3);
}
