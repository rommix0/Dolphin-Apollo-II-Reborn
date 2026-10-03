#ifndef FW_TN_RULES_H
#define FW_TN_RULES_H
/* The rule-format helpers of text normalisation (ROM $B0C3-$B171), decompiled (M4.0 spec section 3.4). */
#include "fw.h"

#define TN_APPEND_PHONETIC 0xB154   /* still in ROM (spec 3.3): it also serves cmd_put_phonetic */

/* tn_append_phonetic: A = len at (tn_rule_ptr),Y: the len bytes after it go to tn_phonetic (ROM call). */
void tn_append_phonetic_rom(fw *f, uint8_t len, uint8_t y);
/* $B0C3: append the spoken name of c (the output of the first rule of its list); returns X = the old tn_pos,
   and tn_pos += 1. */
uint8_t tn_say_char(fw *f, uint8_t c);
/* $B138: advance *y past the context bytes (>= $1E) of the rule at tn_rule_ptr; returns that count or
   length byte. */
uint8_t tn_rule_skip_text(fw *f, uint8_t *y);
/* $B142: tn_rule_ptr += *y + 1 + a (past the count or length byte at *y and the a bytes it counts); *y = 0. */
void tn_rule_skip_field(fw *f, uint8_t a, uint8_t *y);

#endif
