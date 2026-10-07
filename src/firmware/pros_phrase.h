#ifndef FW_PROS_PHRASE_H
#define FW_PROS_PHRASE_H
/* The phrase pass of the prosody (ROM $CEEC-$D128: pros_mark_phrases with its exit block, and pros_store_phrase_len),
   decompiled (M4.3b spec 2.2). */
#include "fw.h"

#define PROS_PHRASE_LEN_MARK  0x80   /* ORA #$80: bit 7 of a stored phrase count (pros_declination_step masks it) */

/* $D0FD: pros_phrase_len into pros_sym_lengthen and pros_sym_shorten at pros_pos, with bit 7 unless
   FW_PROS_PUNCT_NOT_KIND3 is set (the punctuation stored before, later in the text, had no class bit 3); then
   pros_phrase_len = 0, and FW_PROS_PUNCT_NOT_KIND3 = 1 when pros_class has no bit 3 (PHC_KIND_3), else 0.
   Returns the A it leaves: 1, or pros_class when its bit 3 is set. The ROM leaves X = pros_pos, Y untouched. */
uint8_t pros_store_phrase_len(fw *f);
/* $CEF6: the backward pass. pros_pos counts down from its value on entry (tn_phon_len, as pros_mark_stress leaves it;
   0 wraps to 255) to 1; per position: isr_par_ready, then a phrase's phonetic symbols counted, each punctuation's
   count stored (pros_store_phrase_len) with its phrase type's five pros_params and the declination step
   (pros_declination), the accent ($60), the nucleus marks (PSF_NUCLEUS) and the context counts. At 0 the exit block
   $CEEC stores the first phrase's count and pros_sym_flags[0] = pros_word_pos.
   r in: the registers at entry; only Y is read, by the first isr_par_ready ($CF28). r out: the A, X, Y the ROM
   leaves at its RTS ($CEF5): A = pros_word_pos, X = pros_pos (0), Y as the last position left it. */
void pros_mark_phrases(fw *f, fw_regs *r);

#endif
