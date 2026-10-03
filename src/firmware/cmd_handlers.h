#ifndef FW_CMD_HANDLERS_H
#define FW_CMD_HANDLERS_H
/* The deferred command handlers (ROM $A8C0-$ABDE), decompiled (M4.2 spec 2.2). The ROM enters each one by
   JMP (cmd_vector) from cmd_run_next, with X = its cmd_arg_index entry and A = boot_clause[X], and each one ends
   with JMP cmd_run_done, which restores X; cmd_run_due then reloads Y. So a handler has no register output. Only
   the dispatcher (cmd_run.c) calls them. */
#include "fw.h"

typedef void cmd_handler(fw *f, uint8_t x, uint8_t a);

/* The setters without an apply helper (cmd_set.c) */
void cmd_say_version(fw *f, uint8_t x, uint8_t a);        /* $A902 @?: the version texts to tn_phonetic */
void cmd_set_phrase_pause(fw *f, uint8_t x, uint8_t a);   /* $A91F @D: 0-$F */
void cmd_set_word_pause(fw *f, uint8_t x, uint8_t a);     /* $A98A @Q: 0-9 */
void cmd_set_mark_space(fw *f, uint8_t x, uint8_t a);     /* $A99A @M: two digits, 0-$3F */
void cmd_set_spell(fw *f, uint8_t x, uint8_t a);          /* $AA4F @S: 0-1 */
void cmd_set_punct(fw *f, uint8_t x, uint8_t a);          /* $AAF5 @P: 0-1 */
void cmd_set_hyper(fw *f, uint8_t x, uint8_t a);          /* $AB4D @H: 0-1 */
void cmd_set_phonetic(fw *f, uint8_t x, uint8_t a);       /* $AB9F @X: 0-1 */
void cmd_set_formant(fw *f, uint8_t x, uint8_t a);        /* $A92F @U: parameter, two digits, sign */
void cmd_ignore(fw *f, uint8_t x, uint8_t a);             /* $ABDC @G, @I^, @+: nothing */

#endif
