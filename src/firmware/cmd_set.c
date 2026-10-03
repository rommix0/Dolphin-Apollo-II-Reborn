/* The setting handlers of the deferred commands (ROM $A902-$ABDE), decompiled (M4.2 spec 2.2). Each one is
   entered with X = its argument index and A = boot_clause[X] (cmd_handlers.h). */
#include "cmd_handlers.h"
#include "cmd_match.h"
#include "tn_rules.h"
#include "data/fw_data.h"

static uint8_t clause_at(const fw *f, uint8_t x) { return fw_rd8(f, (uint16_t)(BOOT_CLAUSE + x)); }

/* The one-digit setters: setting = cmd_hex_value(a) when it is below limit. The ROM's CMP #0 / BCC is dead,
   because CMP #0 always sets C. */
static void set_below(fw *f, uint16_t setting, uint8_t a, uint8_t limit)
{
    uint8_t v = cmd_hex_value(f, a);
    if (v < limit)
        fw_wr8(f, setting, v);
}

/* $A868: append the length-prefixed phonetic text at tn_rule_ptr to tn_phonetic. The ROM preserves A and Y and
   returns the X tn_append_phonetic leaves (cmd_run_done restores the handler's X at the end). */
static uint8_t cmd_put_phonetic(fw *f, uint8_t x)
{
    uint8_t y = 0;
    tn_append_phonetic(f, fw_ind_y(f, (uint8_t)TN_RULE_PTR, 0), &x, &y);
    return x;
}

void cmd_say_version(fw *f, uint8_t x, uint8_t a)
{
    if (a != '?')
        return;
    fw_wr16(f, TN_RULE_PTR, CMD_VERSION_ENGINE_TEXT);
    x = cmd_put_phonetic(f, x);
    fw_wr16(f, TN_RULE_PTR, CMD_VERSION_LANG_TEXT);
    cmd_put_phonetic(f, x);                        /* X as the first call left it, as in the ROM */
}

void cmd_set_phrase_pause(fw *f, uint8_t x, uint8_t a)
{
    (void)x;
    set_below(f, CMD_PHRASE_PAUSE, a, 0x10);
}

void cmd_set_word_pause(fw *f, uint8_t x, uint8_t a)
{
    (void)x;
    set_below(f, CMD_WORD_PAUSE, a, 10);
}

void cmd_set_mark_space(fw *f, uint8_t x, uint8_t a)
{
    (void)a;                                            /* reloaded after the re-anchoring */
    x = cmd_find_back(f, 'M', x);                       /* $75 = 'M' */
    uint8_t v = cmd_hex_value(f, clause_at(f, x));
    if (v >= 4)
        return;
    fw_wr8(f, FW_TMP_75, util_shl4(v));
    v = cmd_hex_value(f, clause_at(f, ++x));
    if (v >= 0x10)
        return;
    fw_wr8(f, CMD_MARK_SPACE, (uint8_t)(v + fw_rd8(f, FW_TMP_75)));
}

void cmd_set_spell(fw *f, uint8_t x, uint8_t a)
{
    (void)x;
    set_below(f, CMD_SPELL, a, 2);                      /* the ROM: BEQ stores 0, then CMP #2 / BCS */
}

void cmd_set_punct(fw *f, uint8_t x, uint8_t a)
{
    (void)x;
    set_below(f, CMD_PUNCT, a, 2);
}

void cmd_set_hyper(fw *f, uint8_t x, uint8_t a)
{
    (void)x;
    set_below(f, CMD_HYPER, a, 2);
}

void cmd_set_phonetic(fw *f, uint8_t x, uint8_t a)
{
    (void)x;
    set_below(f, CMD_PHONETIC, a, 2);
}

void cmd_set_formant(fw *f, uint8_t x, uint8_t a)
{
    (void)a;
    x = cmd_find_back(f, 'U', x);                       /* $75 = 'U' */
    uint8_t d = cmd_hex_value(f, clause_at(f, x));      /* the parameter 0-9 */
    if (d >= 10)
        return;
    uint8_t v = cmd_hex_value(f, clause_at(f, ++x));
    if (v >= 0x10)
        return;
    fw_wr8(f, FW_TMP_75, util_shl4(v));
    v = cmd_hex_value(f, clause_at(f, ++x));
    if (v >= 0x10)
        return;
    fw_wr8(f, FW_TMP_75, (uint8_t)(v + fw_rd8(f, FW_TMP_75)));
    uint8_t sign = clause_at(f, ++x);                   /* raw, not case-mapped */
    if (sign == '-')
        fw_wr8(f, (uint16_t)(CMD_FORMANT_OFS + d), (uint8_t)(0 - fw_rd8(f, FW_TMP_75)));
    else if (sign == '+')
        fw_wr8(f, (uint16_t)(CMD_FORMANT_OFS + d), fw_rd8(f, FW_TMP_75));
}

void cmd_ignore(fw *f, uint8_t x, uint8_t a)
{
    (void)f;
    (void)x;
    (void)a;
}
