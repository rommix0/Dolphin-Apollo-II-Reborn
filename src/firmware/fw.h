#ifndef FW_H
#define FW_H
/* Decompiled Apollo 2 firmware on the firmware's own memory image (M4.0 spec section 3).
   Nothing under src/firmware/ includes machine/ or cpu740/ headers: build.bat syntax-checks this directory
   without the src include path. */
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include "fw_ram.h"

#define FW_RAM_SIZE  0x4000
#define FW_TMP_78    0x0078   /* zero-page scratch, unnamed in the annotations: a saved Y, byte counters */
#define FW_TMP_75    0x0075   /* zero-page scratch, unnamed in the annotations: tn_convert_clause's saved X */
#define FW_TMP_76    0x0076   /* zero-page scratch, unnamed in the annotations: cmd_scan_clause's resume index,
                                 cmd_set_prosody's parameter index */
#define FW_TMP_34    0x0034   /* zero-page scratch, unnamed in the annotations: cmd_hex_byte's high digit */
#define FW_TMP_35    0x0035   /* zero-page scratch, unnamed in the annotations: cmd_hex_byte's low digit */
#define FW_TMP_1809  0x1809   /* scratch, unnamed in the annotations: cmd_hex_byte's high nibble */
#define FW_UTT_CONTENT         0x006B   /* unnamed in the annotations: "the utterance has content", $FF (LDM) at each
                                           word pause and each phoneme triple; boot_loop_play tests it */
#define FW_PROS_STRESS_TOGGLE  0x1C0D   /* unnamed in the annotations: pros_phrase_gen's "a stress or an accent seen
                                           in this word" (0 at each word start). pros_mark_stress: the syllable's
                                           stress, $FF at its start, at each word end and at '}', 0 at '~', '{' and
                                           'x', inverted ($CC45) at the onset of each later syllable of a word.
                                           pros_mark_phrases writes it (0 at its start and at each space or
                                           punctuation, 1 at '{') and never reads it */
#define FW_PROS_WORD_U         0x1C1F   /* unnamed in the annotations: the U term (pros_params[20]) that the first
                                           symbol of a word adds to pros_term_stress; 0 after it */
/* The state bytes of the stress and phrase passes (pros_mark_stress $CABD, pros_mark_phrases $CEF6 and
   pros_store_phrase_len $D0FD), unnamed in the annotations; no other routine uses them (M4.3b spec 2.6). */
#define FW_PROS_NEXT_SYM           0x1C06   /* pros_mark_stress: the next symbol after pros_pos that has a class
                                               (class 0, the markers, skipped), or a space if one comes first; $0D
                                               when the scan reaches tn_phon_len (after a space too) */
#define FW_PROS_NEXT_FEATURES      0x1C07   /* pros_mark_stress: the features of FW_PROS_NEXT_SYM (0 for a space and at
                                               the end) */
#define FW_PROS_NEXT_PHON          0x1C08   /* pros_mark_stress: the next symbol that has a class, past a space; $0D at
                                               the end. The vowel path replaces it with tn_phonetic[pros_pos + 2]
                                               when it is '1' or 'L' ($CDBF) */
#define FW_PROS_NEXT_PHON_FEATURES 0x1C09   /* pros_mark_stress: the features of FW_PROS_NEXT_PHON (0 at the end) */
#define FW_PROS_SYLLABLES          0x1C0B   /* pros_mark_stress: the syllables of the word so far (a vowel counts when
                                               a consonant or no features follow, $CD65), stored at the word's end
                                               in pros_sym_lengthen and pros_sym_shorten, then 0. pros_mark_phrases:
                                               that count, read back at each space and punctuation ($CF6B), of the
                                               word it walks next (the one before it in the text); 5 or more
                                               shortens each of its phonetic symbols by 4 ($D0B5) */
#define FW_PROS_VOWEL_SEEN         0x1C0C   /* pros_mark_stress: a vowel seen in this word (1 on the vowel path, 0 at
                                               each word end) */
#define FW_PROS_TILDE_SHORTEN      0x1C11   /* pros_mark_stress: 1 at its start and at '~', 0 at each word end and
                                               while FW_PROS_ACCENT_PENDING is set ($CC81); while set, each symbol's
                                               shortening count gets 3 ($CC92). A space copies it into
                                               FW_PROS_ACCENT_PENDING ($CEA2) */
#define FW_PROS_WRITE_ONLY_1C12    0x1C12   /* pros_mark_stress, on the vowel path: 1 when FW_PROS_NEXT_PHON is '1' or
                                               'L' ($CDCB), else 0 ($CDD2); never read */
#define FW_PROS_FINAL              0x1C13   /* pros_mark_stress: the vowel's syllable ends a phrase (the class bit 0,
                                               punctuation, of the first non-consonant from pros_pos + 2 on, $CDEA).
                                               pros_mark_phrases: walking back through a word's last syllable (1 at
                                               its start and at each space and punctuation, 0 at the consonant
                                               before a vowel, $D079); a vowel outside it is shortened by 3 */
#define FW_PROS_BACKQUOTE          0x1C14   /* pros_mark_stress: a backquote seen ($CC12), so the next '?' becomes '.'
                                               ($CEB3); 0 at its start, at that '?' and at $CEC2 */
#define FW_PROS_PUNCT_NOT_KIND3    0x1C15   /* pros_store_phrase_len: the punctuation it stored at last (the next one
                                               in the text) had no class bit 3 (PHC_KIND_3 is set for . : ; ?), so
                                               this store gets no bit 7. Cleared by pros_mark_stress ($CAE0) only */
#define FW_PROS_FINAL_LENGTHEN     0x1C16   /* pros_mark_phrases: the phrase-final syllable's lengthening: 8
                                               (PHC_KIND_3) after a punctuation with class bit 3 ($D057); 0 at its
                                               start, at each space and punctuation, and at the consonant before a
                                               vowel ($D07C). While set, each phonetic symbol's lengthening count
                                               gets 8 ($D084); while clear, a vowel's shortening gets 7 ($D0A5) */
#define FW_PROS_ACCENT_PENDING     0x1C19   /* pros_mark_stress: lengthen the word's symbols by 4 ($CC84): 1 at its
                                               start and at each word end but a space, 0 at '~', and at a space
                                               FW_PROS_TILDE_SHORTEN (1 after a function word). pros_mark_phrases: an
                                               accent still to place: 1 at its start and at each '~'; the next
                                               stressed vowel walked gets the accent $60 and clears it ($D0E1) */
#define FW_PROS_PREV_FEATURES_COPY 0x1C1A   /* pros_mark_stress: pros_prev_features as stored at $CD58, but 0 at its
                                               start and at each word end ($CEDE), where pros_prev_features is kept:
                                               the previous symbol's features within the word ($CD81) */
#define FW_PROS_WRITE_ONLY_1C1C    0x1C1C   /* pros_mark_phrases: 1 at its start ($CF0F) and at each space and
                                               punctuation ($CF79); never read */
#define FW_PROS_NUCLEUS_MASK       0x1C1E   /* pros_mark_phrases: ORed into each phonetic symbol's pros_sym_flags
                                               ($D0E7): 7 at its start and at each '?' '!' ',' ':' '.' ';', 0 after
                                               the first symbol whose flags then have the stress bit $40 ($D0F7).
                                               pros_mark_stress clears it ($CAE3) */

/* Cycle budgets of a ROM call from C (M4.1 spec 3.1): a helper that does not return within FW_ROM_CALL_BUDGET
   is a fault; a call that plays speech (prosody and playback) gets FW_ROM_CALL_SPEECH_BUDGET. */
#define FW_ROM_CALL_BUDGET         10000000ULL
#define FW_ROM_CALL_SPEECH_BUDGET  1000000000ULL

typedef struct { uint8_t a, x, y, p; } fw_regs;

/* How a ROM call from C ended: returned; failed (a fault, or out of budget); or unwound: the ROM reset the
   stack past the call's own frame (bank_enter, M4.2 spec 3), so it can never return. */
typedef enum { FW_CALL_FAILED = 0, FW_CALL_RETURNED, FW_CALL_UNWOUND } fw_call_result;

/* The machine or native host. In hybrid runs it is the emulator (src/engine/thunks.c). */
typedef struct fw_host {
    void *ctx;
    /* Runs the routine at addr, still in ROM, to its RTS, within budget cycles. A, X and Y in and out; the
       routine starts with the machine's current flags, and p returns its exit flags. */
    fw_call_result (*rom_call)(void *ctx, uint16_t addr, fw_regs *regs, uint64_t budget);
    /* P2 bit 7, the lower-ROM window, as SEB7/CLB7 $F0 set and clear it. */
    void (*set_low_window)(void *ctx, bool on);
} fw_host;

/* Converted ROM data at its original addresses (built by src/firmware/data/fw_data.c). */
typedef struct fw_image {
    uint8_t rom[0x10000];          /* $4000-$FFFF with the window off; index = CPU address */
    uint8_t rom_ok[0x10000 / 8];   /* bit set = converted byte */
    uint8_t low[0x4000];           /* $4000-$7FFF with the window on; index = CPU address - $4000 */
    uint8_t low_ok[0x4000 / 8];
} fw_image;

typedef struct fw {
    uint8_t *ram;                  /* the firmware's RAM $0000-$3FFF (the SFR bytes $DA-$FF are not data) */
    const fw_image *img;
    const fw_host *host;           /* NULL: no ROM to call, no port to drive */
    bool low_window;               /* P2 bit 7: $4000-$7FFF read the LOW overlay */
    bool fault;                    /* a read outside the converted data, or a failed ROM call */
    uint16_t fault_addr;
    jmp_buf *trap;                 /* when set, a fault longjmps here and the routine is abandoned */
    bool unwound;                  /* a ROM call reset the stack: the C was abandoned (not a fault; M4.2 spec 3) */
    unsigned tn_flushes;           /* mid-clause flushes by tn_convert_clause (a statistic for the tests) */
} fw;

/* RAM by name; a < FW_RAM_SIZE. */
static inline uint8_t fw_rd8(const fw *f, uint16_t a) { return f->ram[a]; }
static inline void fw_wr8(fw *f, uint16_t a, uint8_t v) { f->ram[a] = v; }
static inline uint16_t fw_rd16(const fw *f, uint16_t a) { return (uint16_t)(f->ram[a] | f->ram[a + 1] << 8); }
static inline void fw_wr16(fw *f, uint16_t a, uint16_t v)
{
    f->ram[a] = (uint8_t)v;
    f->ram[a + 1] = (uint8_t)(v >> 8);
}

void fw_init(fw *f, uint8_t *ram, const fw_image *img, const fw_host *host);
/* A ROM data byte as the CPU reads it at addr (>= $4000) under the current window. A byte outside the
   converted data is a fault: it longjmps to f->trap when set, else reads $FF. */
uint8_t fw_rom8(fw *f, uint16_t addr);
/* A data byte anywhere: RAM below $4000 (an SFR address is a fault), else fw_rom8. */
uint8_t fw_mem8(fw *f, uint16_t addr);
/* (zp),Y: the byte at the little-endian pointer in zero page zp, plus y. */
uint8_t fw_ind_y(fw *f, uint8_t zp, uint8_t y);
/* SEB7/CLB7 P2: sets the window and tells the host. */
void fw_set_low_window(fw *f, bool on);
/* Calls a routine still in ROM through the host, within budget cycles. True when it returned. Without a host, or
   when the call fails, a fault. When the ROM reset the stack (unwound), f->unwound is set and the routine is
   abandoned like a fault (a longjmp to f->trap), but it is not a fault. */
bool fw_rom_call(fw *f, uint16_t addr, fw_regs *regs, uint64_t budget);

#endif
