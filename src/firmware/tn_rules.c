#include "tn_rules.h"
#include "data/fw_data.h"

static uint8_t rule_byte(fw *f, uint8_t y) { return fw_ind_y(f, (uint8_t)TN_RULE_PTR, y); }

void tn_append_phonetic_rom(fw *f, uint8_t len, uint8_t y)
{
    fw_regs r = { len, 0, y, 0 };
    fw_rom_call(f, TN_APPEND_PHONETIC, &r, FW_ROM_CALL_BUDGET);   /* returns A = $FF */
}

uint8_t tn_rule_skip_text(fw *f, uint8_t *y)
{
    uint8_t a;
    while ((a = rule_byte(f, *y)) >= 0x1E)
        (*y)++;
    return a;
}

void tn_rule_skip_field(fw *f, uint8_t a, uint8_t *y)
{
    uint8_t y1 = (uint8_t)(*y + 1);
    fw_wr8(f, FW_TMP_78, y1);
    /* CLC/ADC $78 then CLC/ADC tn_rule_ptr: the 8-bit sum a + y + 1 is added to the 16-bit pointer */
    fw_wr16(f, TN_RULE_PTR, (uint16_t)(fw_rd16(f, TN_RULE_PTR) + (uint8_t)(a + y1)));
    *y = 0;
}

uint8_t tn_say_char(fw *f, uint8_t c)
{
    uint8_t y = 0;
    fw_wr8(f, TN_RULE_PTR, fw_rom8(f, (uint16_t)(TN_CHAR_RULES_LO + c)));
    fw_wr8(f, TN_RULE_PTR + 1, fw_rom8(f, (uint16_t)(TN_CHAR_RULES_HI + c)));
    uint8_t n = tn_rule_skip_text(f, &y);   /* past the left context to the count n */
    tn_rule_skip_field(f, n, &y);           /* past n and the n consumed characters */
    tn_rule_skip_text(f, &y);               /* past the rest of the right context to the length */
    tn_append_phonetic_rom(f, rule_byte(f, y), y);
    uint8_t x = fw_rd8(f, TN_POS);
    fw_wr8(f, TN_POS, (uint8_t)(x + 1));
    return x;
}
