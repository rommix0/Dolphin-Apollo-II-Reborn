/* Converted from the Apollo 2 ROM by tools/extract_tables.py (M4.2 spec section 4), then reviewed.
   tests/test_firmware_data.c re-encodes it and compares it byte for byte with the ROM. */
#include <stddef.h>
#include "fw_data.h"
#include "../cmd_addr.h"

/* cmd_imm_patterns ($989C): the immediate commands, matched at interrupt level by cmd_imm_match.
   The encoder writes the $FF end, then cmd_imm_table ($98A7): each record's address, then the
   end's. */
const fw_cmd_pattern fw_cmd_imm_patterns[] = {
    { "I+", ROM_CMD_IMM_INDEX_ADD },            /*  0: $989C */
    { "I\?", ROM_CMD_IMM_INDEX_QUERY },         /*  1: $98A1 */
};
const size_t fw_cmd_imm_pattern_count = sizeof fw_cmd_imm_patterns / sizeof fw_cmd_imm_patterns[0];

/* The deferred command patterns ($98AD), in ROM order: * matches any character, # a digit, both on
   the case-mapped text. A record is the pattern, its 0 end and its handler; the encoder writes the
   $FF end after the last. */
const fw_cmd_pattern fw_cmd_patterns[] = {
    { "\?", ROM_CMD_SAY_VERSION },              /*  0: $98AD */
    { "*\?", ROM_CMD_QUERY },                   /*  1: $98B1 */
    { "A*", ROM_CMD_SET_VOLUME },               /*  2: $98B6 */
    { "D*", ROM_CMD_SET_PHRASE_PAUSE },         /*  3: $98BB */
    { "E*", ROM_CMD_ECHO },                     /*  4: $98C0 */
    { "F*", ROM_CMD_SET_PITCH },                /*  5: $98C5 */
    { "H#", ROM_CMD_SET_HYPER },                /*  6: $98CA */
    { "M**", ROM_CMD_SET_MARK_SPACE },          /*  7: $98CF */
    { "P#", ROM_CMD_SET_PUNCT },                /*  8: $98D5 */
    { "Q#", ROM_CMD_SET_WORD_PAUSE },           /*  9: $98DA */
    { "R#", ROM_CMD_SET_PROSODY },              /* 10: $98DF */
    { "R***", ROM_CMD_SET_PROSODY },            /* 11: $98E4 */
    { "S#", ROM_CMD_SET_SPELL },                /* 12: $98EB */
    { "V#", ROM_CMD_SET_VOICE },                /* 13: $98F0 */
    { "W#", ROM_CMD_SET_SPEED },                /* 14: $98F5 */
    { "X#", ROM_CMD_SET_PHONETIC },             /* 15: $98FA */
    { "U#*+", ROM_CMD_SET_FORMANT },            /* 16: $98FF */
    { "U#*-", ROM_CMD_SET_FORMANT },            /* 17: $9906 */
    { "U#**+", ROM_CMD_SET_FORMANT },           /* 18: $990D */
    { "U#**-", ROM_CMD_SET_FORMANT },           /* 19: $9915 */
    { "$#", ROM_CMD_SET_SOURCE },               /* 20: $991D */
    { "B#", ROM_CMD_SET_VOICING },              /* 21: $9922 */
    { "I+", ROM_CMD_MARK_INDEX },               /* 22: $9927 */
    { "T*", ROM_CMD_SOUND },                    /* 23: $992C */
    { "=#", ROM_BANK_CMD_SELECT },              /* 24: $9931 */
    { "L", ROM_CMD_LIST_LANGUAGES },            /* 25: $9936 */
    { "K#", ROM_CMD_SET_SPEAKER },              /* 26: $993A */
    { "G#", ROM_CMD_IGNORE },                   /* 27: $993F */
    { "I^", ROM_CMD_IGNORE },                   /* 28: $9944 */
    { "+*", ROM_CMD_IGNORE },                   /* 29: $9949 */
};
const size_t fw_cmd_pattern_count = sizeof fw_cmd_patterns / sizeof fw_cmd_patterns[0];

/* cmd_table ($994F): the patterns in match order (first match wins), as indices into
   fw_cmd_patterns. The encoder writes each record's address, then the address of the $FF end. */
const uint8_t fw_cmd_table[] = {
    0,    /*  0: "?" */
    1,    /*  1: "*?" */
    2,    /*  2: "A*" */
    3,    /*  3: "D*" */
    4,    /*  4: "E*" */
    5,    /*  5: "F*" */
    6,    /*  6: "H#" */
    7,    /*  7: "M**" */
    8,    /*  8: "P#" */
    9,    /*  9: "Q#" */
    10,   /* 10: "R#" */
    11,   /* 11: "R***" */
    12,   /* 12: "S#" */
    13,   /* 13: "V#" */
    14,   /* 14: "W#" */
    15,   /* 15: "X#" */
    16,   /* 16: "U#*+" */
    17,   /* 17: "U#*-" */
    18,   /* 18: "U#**+" */
    19,   /* 19: "U#**-" */
    20,   /* 20: "$#" */
    21,   /* 21: "B#" */
    22,   /* 22: "I+" */
    23,   /* 23: "T*" */
    24,   /* 24: "=#" */
    25,   /* 25: "L" */
    26,   /* 26: "K#" */
    27,   /* 27: "G#" */
    27,   /* 28: "G#" again: the ROM lists this record twice */
    28,   /* 29: "I^" */
    29,   /* 30: "+*" */
};
const size_t fw_cmd_table_count = sizeof fw_cmd_table / sizeof fw_cmd_table[0];

/* cmd_query_table ($998F): the settings a @c? query reports, as the letter and the zero-page
   setting. The encoder writes the 0 end. */
const fw_cmd_query fw_cmd_query_table[] = {
    { 'A', CMD_VOLUME },
    { 'B', CMD_VOICING },
    { 'C', BOOT_SELFTEST_FAIL },
    { 'D', CMD_PHRASE_PAUSE },
    { 'F', CMD_PITCH },
    { 'H', CMD_HYPER },
    { 'K', CMD_SPEAKER },
    { 'M', CMD_MARK_SPACE },
    { 'P', CMD_PUNCT },
    { 'Q', CMD_WORD_PAUSE },
    { 'R', CMD_PROSODY },
    { 'S', CMD_SPELL },
    { 'V', CMD_VOICE },
    { 'W', CMD_SPEED },
    { 'X', CMD_PHONETIC },
    { 'Z', BOOT_DIP },
    { '=', BANK_CURRENT },
    { '$', CMD_SOURCE },
};
const size_t fw_cmd_query_count = sizeof fw_cmd_query_table / sizeof fw_cmd_query_table[0];

/* cmd_case_map ($99B4): the identity, except these folds { first, last, to, shift }: the codes
   first..last map to to, or with shift each code c to to + (c - first). The encoder writes all
   256 codes. */
const fw_case_fold fw_cmd_case_folds[] = {
    { 0x00, 0x0C, ' ', false },         /* $00-$0C to space */
    { 0x0E, 0x1A, ' ', false },         /* $0E-$1A to space */
    { 0x1C, 0x1F, ' ', false },         /* $1C-$1F to space */
    { '[', '[', '(', false },           /* $5B to $28 */
    { ']', ']', ')', false },           /* $5D to $29 */
    { 'a', 'z', 'A', true },            /* $61-$7A to $41-$5A */
    { 0x7F, 0x7F, ' ', false },         /* $7F to space */
    { 0x81, 0x81, 0x9A, false },        /* $81 to $9A */
    { 0x82, 0x82, 0x90, false },        /* $82 to $90 */
    { 0x84, 0x84, 0x8E, false },        /* $84 to $8E */
    { 0x86, 0x86, 0x8F, false },        /* $86 to $8F */
    { 0x87, 0x87, 0x80, false },        /* $87 to $80 */
    { 0x91, 0x91, 0x92, false },        /* $91 to $92 */
    { 0x94, 0x94, 0x99, false },        /* $94 to $99 */
    { 0x9B, 0x9B, ' ', false },         /* $9B to space */
    { 0x9D, 0x9F, ' ', false },         /* $9D-$9F to space */
    { 0xA8, 0xAA, ' ', false },         /* $A8-$AA to space */
    { 0xAD, 0xBF, ' ', false },         /* $AD-$BF to space */
    { 0xC1, 0xDF, ' ', false },         /* $C1-$DF to space */
    { 0xF9, 0xFA, ' ', false },         /* $F9-$FA to space */
    { 0xFE, 0xFF, ' ', false },         /* $FE-$FF to space */
};
const size_t fw_cmd_case_fold_count = sizeof fw_cmd_case_folds / sizeof fw_cmd_case_folds[0];

/* cmd_speed_maps ($C87B): per speed 1-9 (indexed by speed - 1) its 32-byte duration map in
   cmd_speed_map_data, which is prosody's (M4.3) and not converted here */
const uint16_t fw_cmd_speed_maps[10] = {
    CMD_SPEED_MAP_DATA + 32 * 0,    /* speed 1 */
    CMD_SPEED_MAP_DATA + 32 * 1,    /* speed 2 */
    CMD_SPEED_MAP_DATA + 32 * 2,    /* speed 3 */
    CMD_SPEED_MAP_DATA + 32 * 3,    /* speed 4 */
    CMD_SPEED_MAP_DATA + 32 * 4,    /* speed 5 */
    CMD_SPEED_MAP_DATA + 32 * 5,    /* speed 6 */
    CMD_SPEED_MAP_DATA + 32 * 6,    /* speed 7 */
    CMD_SPEED_MAP_DATA + 32 * 7,    /* speed 8 */
    CMD_SPEED_MAP_DATA + 32 * 8,    /* speed 9 */
    CMD_SPEED_MAP_DATA + 32 * 8,    /* entry 10 repeats entry 9 */
};
/* cmd_speed_limits ($C88F): per speed 1-9 the value cmd_apply_speed puts in cmd_speed_limit */
const uint8_t fw_cmd_speed_limits[9] = { 7, 7, 7, 7, 4, 2, 0, 0, 0 };

/* par_voice_a_count ($D469), par_voice_b_count ($E5E2): the phoneme counts at the head of the
   voice tables (M4.4), read by cmd_select_voice_a and cmd_select_voice_b */
const uint8_t fw_par_voice_a_count = 86, fw_par_voice_b_count = 86;
