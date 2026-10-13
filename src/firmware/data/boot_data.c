/* Converted from the Apollo 2 ROM (M4.6c2 spec 2.4), then reviewed: the boot's command strings and the power-up
   prosody parameters. tests/test_firmware_data.c re-encodes them and compares them byte for byte with the ROM. */
#include "fw_data.h"

/* The self-test's command strings ($A7BE, $A7C5): boot_selftest_sound points cmd_text_ptr at one and
   boot_queue_string queues it into the input ring up to its zero end (C's terminating 0, which the encoder writes).
   Each starts with $18 (Ctrl-X, the cancel), then @T sound effects: one short buzz when the self-test passes, three
   long ones when it fails. */
const char fw_boot_selftest_ok_cmds[7] = "\030@T5, ";
const char fw_boot_selftest_fail_cmds[15] = "\030@T7 @T7 @T7, ";

/* A pitch term, added to each pitch target mod 256 (pros_target, $D2DF: ADC): a negative one is its two's
   complement. */
#define TERM(n) ((uint8_t)(n))

/* boot_prosody_defaults ($C65B): the power-up values of pros_params (boot_load_prosody_defaults), one per @R letter.
   A-T are five per phrase type: the start and end baselines (pros_baseline, pros_baseline_end; a pitch target adds
   pros_baseline / 8), then the stress, nucleus and final terms (pros_mark_phrases, pros_phrase_gen). U-Z are the word
   terms of pros_phrase_gen. */
const uint8_t fw_boot_prosody_defaults[26] = {
    180,         /* A: '.' and ';' (and the default): the start baseline */
    25,          /* B: the end baseline */
    TERM(8),     /* C: the stress term */
    TERM(-10),   /* D: the nucleus term */
    TERM(-20),   /* E: the final term */
    180,         /* F: ',' and ':': the start baseline */
    25,          /* G: the end baseline */
    TERM(8),     /* H: the stress term */
    TERM(0),     /* I: the nucleus term */
    TERM(5),     /* J: the final term */
    195,         /* K: '?': the start baseline */
    25,          /* L: the end baseline */
    TERM(10),    /* M: the stress term */
    TERM(10),    /* N: the nucleus term */
    TERM(25),    /* O: the final term */
    180,         /* P: '!': the start baseline */
    25,          /* Q: the end baseline */
    TERM(8),     /* R: the stress term */
    TERM(10),    /* S: the nucleus term */
    TERM(0),     /* T: the final term */
    TERM(0),     /* U: pros_term_stress of a word's first symbol */
    TERM(0),     /* V: added to pros_term_tail at a word's last symbol */
    TERM(15),    /* W: the accent */
    0,           /* X: no reader */
    TERM(1),     /* Y: a later stress of a word */
    TERM(0),     /* Z: pros_term_stress of a word's later symbols */
};
