#ifndef FW_DATA_H
#define FW_DATA_H
/* Readable firmware tables converted from the ROM (M4.0 spec section 4), each pinned to its annotated ROM
   address. fw_data.c encodes them into the data image the C code reads (fw_rom8). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../fw.h"

/* One rule (format: the annotation of tn_rules_punct). left: the context before the key, in reading order
   (the ROM stores it nearest character first); consumed: the characters after the key the rule consumes (the
   first n of its right context); right: the rest of the right context; output: phonetic text for tn_phonetic. */
typedef struct { const char *left, *consumed, *right, *output; } fw_rule;
typedef struct { const fw_rule *rules; uint16_t count; } fw_rule_list;   /* the ROM ends each list with $FF */
/* Rule lists laid out back to back from addr (in the LOW overlay when low), size bytes in all. */
typedef struct {
    const char *name;
    uint16_t addr, size;
    bool low;
    const fw_rule_list *const *lists;
    uint16_t count;
} fw_rule_table;
/* A context-symbol list (% & @): $00 CR LF, then each entry followed by CR LF, then $00. */
typedef struct { const char *name; uint16_t addr; const char *const *entries; uint8_t count; } fw_affix_list;
/* A length-prefixed phonetic text (M4.1 spec 4): the length byte strlen(text), the text, then tail, the bytes the
   ROM keeps after the counted text (a CR, or nothing). size: the annotated bytes in all. */
typedef struct { const char *name; uint16_t addr, size; const char *text, *tail; } fw_phonetic_text;

/* A command pattern record (M4.2 spec 4): the pattern (* any character, # a digit, both matched on the case-mapped
   text), its 0 end, then the handler's ROM address (cmd_addr.h), low byte first. */
typedef struct { const char *pattern; uint16_t handler; } fw_cmd_pattern;
/* A cmd_query_table entry: a setting letter and the zero-page address of the setting (fw_ram.h). */
typedef struct { char letter; uint8_t setting; } fw_cmd_query;
/* A fold of cmd_case_map: the codes first..last map to `to`, or with shift each code c to to + (c - first). */
typedef struct { uint8_t first, last, to; bool shift; } fw_case_fold;

/* Pinned addresses (ghidra740/annotations: text_norm.txt, lts.txt) */
#define TN_CHAR_RULES_LO   0x4000   /* low byte of each character's list address */
#define TN_CHAR_RULES_HI   0x4100
#define LTS_PAIR_INDEX     0x4200   /* the address of lts_pairs_a..z per key letter */
#define LTS_PAIRS_A        0x4234   /* lts_pairs_a..z: 26 tables of 26 words, 52 bytes apart */
#define UNUSED_9824        0x9824   /* a $00 before lts_suffixes that no code reads */
#define LTS_SUFFIXES       0x9825
#define LTS_SIBILANTS      0x9861
#define LTS_AT_CONSONANTS  0x9884
#define LTS_CHAR_CLASS     0x9AB4

/* Pinned addresses of the command data (ghidra740/annotations: command.txt, params.txt; M4.2 spec 4) */
#define CMD_IMM_PATTERNS    0x989C   /* the immediate pattern records, then their $FF end */
#define CMD_IMM_TABLE       0x98A7   /* ptr[3]: each immediate record, then the end */
#define CMD_PATTERNS        0x98AD   /* the deferred pattern records, then their $FF end */
#define CMD_TABLE           0x994F   /* ptr[32]: the records in match order, then the end */
#define CMD_QUERY_TABLE     0x998F   /* 18 x (letter, zero-page setting), then 0 */
#define CMD_CASE_MAP        0x99B4   /* byte[256] */
#define CMD_SPEED_MAPS      0xC87B   /* ptr[10], indexed by speed - 1 */
#define CMD_SPEED_LIMITS    0xC88F   /* byte[9], indexed by speed - 1 */
#define CMD_SPEED_MAP_DATA  0xC898   /* not converted (prosody's, M4.3): cmd_speed_maps point into it */
#define PAR_VOICE_A_COUNT   0xD469   /* byte: the phoneme count heading the voice A table (M4.4) */
#define PAR_VOICE_B_COUNT   0xE5E2   /* byte: the same for voice B */
#define PAR_VOICE_A         0xD46A   /* the M4.4 voice A table, not converted; the C writes only its address */
#define PAR_VOICE_B         0xE5E3   /* the M4.4 voice B table, not converted; the C writes only its address */

/* lts_char_class bits */
#define CC_LETTER       0x80   /* also the apostrophe and the underscore */
#define CC_VOWEL        0x40   /* A E I O U Y (also the underscore and lower-case x) */
#define CC_FRONT_VOWEL  0x20   /* E I Y */
#define CC_NASAL        0x10   /* M N */
#define CC_VOICED       0x08   /* B D G L M N R V W */
#define CC_CONSONANT    0x04
#define CC_HIGH         0x02   /* some codes above $7F only */
#define CC_PUNCT        0x01   /* punctuation or symbol */

extern const fw_rule_table tn_rules_ctrl, tn_rules_punct, tn_rules_digits, tn_rules_punct2, lts_rules_letters,
    tn_rules_symbols3, tn_rules_lower, tn_rules_symbols4, tn_rules_high;
extern const fw_rule_table lts_rules_a, lts_rules_b, lts_rules_c, lts_rules_d, lts_rules_e, lts_rules_f,
    lts_rules_g, lts_rules_h, lts_rules_i, lts_rules_j, lts_rules_k, lts_rules_l, lts_rules_m, lts_rules_n,
    lts_rules_o, lts_rules_p, lts_rules_q, lts_rules_r, lts_rules_s, lts_rules_t, lts_rules_u, lts_rules_v,
    lts_rules_w, lts_rules_x, lts_rules_y, lts_rules_z;
extern const fw_rule_list *const lts_pairs_a[26], *const lts_pairs_b[26], *const lts_pairs_c[26],
    *const lts_pairs_d[26], *const lts_pairs_e[26], *const lts_pairs_f[26], *const lts_pairs_g[26],
    *const lts_pairs_h[26], *const lts_pairs_i[26], *const lts_pairs_j[26], *const lts_pairs_k[26],
    *const lts_pairs_l[26], *const lts_pairs_m[26], *const lts_pairs_n[26], *const lts_pairs_o[26],
    *const lts_pairs_p[26], *const lts_pairs_q[26], *const lts_pairs_r[26], *const lts_pairs_s[26],
    *const lts_pairs_t[26], *const lts_pairs_u[26], *const lts_pairs_v[26], *const lts_pairs_w[26],
    *const lts_pairs_x[26], *const lts_pairs_y[26], *const lts_pairs_z[26];

extern const fw_rule_list *const tn_char_lists[256];          /* tn_char_rules_lo/hi */
extern const fw_rule_list *const *const lts_pair_lists[26];   /* [key - 'A'][second - 'A'], NULL = none */
extern const fw_rule_table *const fw_rule_tables[];           /* every rule table, in address order per space */
extern const size_t fw_rule_table_count;
extern const fw_affix_list fw_affix_lists[];
extern const size_t fw_affix_list_count;
extern const fw_phonetic_text fw_phonetic_texts[];
extern const size_t fw_phonetic_text_count;
extern const uint8_t lts_char_class[256];
extern const fw_cmd_pattern fw_cmd_imm_patterns[], fw_cmd_patterns[];
extern const size_t fw_cmd_imm_pattern_count, fw_cmd_pattern_count;
extern const uint8_t fw_cmd_table[];   /* cmd_table in match order, as indices into fw_cmd_patterns */
extern const size_t fw_cmd_table_count;
extern const fw_cmd_query fw_cmd_query_table[];
extern const size_t fw_cmd_query_count;
extern const fw_case_fold fw_cmd_case_folds[];
extern const size_t fw_cmd_case_fold_count;
extern const uint16_t fw_cmd_speed_maps[10];
extern const uint8_t fw_cmd_speed_limits[9];
extern const uint8_t fw_par_voice_a_count, fw_par_voice_b_count;

/* Encodes every table into img at its ROM address. False with a reason in err on any inconsistency. */
bool fw_image_build(fw_image *img, char *err, size_t errlen);
/* The image, built on first use; NULL (reason printed to stderr) if it does not build. */
const fw_image *fw_image_get(void);

#endif
