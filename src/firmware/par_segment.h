#ifndef FW_PAR_SEGMENT_H
#define FW_PAR_SEGMENT_H
/* The segment reader of the playback set-up and its leaves (ROM $BD07-$BD32 and $BFC3-$C038), decompiled (M4.4a
   spec 2.2). Every routine takes the registers the ROM has at its entry in r (r->p is not read: no routine here
   reads an entry flag) and returns in r the A, X and Y the ROM leaves at its RTS; a register the routine does not
   touch is left as it came in. */
#include "fw.h"

#define PAR_CLAMP_MIN      1      /* par_clamp: LDA #1 */
#define PAR_CLAMP_MAX      0x3F   /* par_clamp: LDA #$3F */
#define PAR_VOICING_FLOOR  2      /* par_scale_voicing: LDA #2, at @B0, and for 0 at @B1-@B3 */

/* $BD07: par_row_ptr += par_row_len, 16 bits (the next row of N bytes). A = the new low byte; X and Y untouched. */
void par_next_row(fw *f, fw_regs *r);
/* $BD14: par_row_ptr += par_param_stride, 16 bits (the same row of the next parameter, 5N bytes on). A = the new
   high byte; X and Y untouched. */
void par_next_param(fw *f, fw_regs *r);
/* $BD24: A clamped by the N flag of two 8-bit differences (CMP; BPL): 1 when A - 1 has bit 7 set (A = 0 or
   $81-$FF), $3F when A - $40 has bit 7 clear (A = $40-$80), else A (1-$3F). X and Y untouched. */
void par_clamp(fw *f, fw_regs *r);
/* $BFC3: a voicing value v = A scaled by @B (cmd_voicing). 1 and 8 pass at every @B. Otherwise @B0 gives 2;
   @B1 v/4; @B2 v/16 + v/4 + 1; @B3 v/8 + v/4; @B4 v/16 + v/8 + v/4; @B5 v/2; @B6 v/8 + v/2; @B7 v/4 + v/2;
   @B8 and above v. Each term truncates; v = 0 gives 2 at @B1-@B3 (0 at @B4-@B7). FW_TMP_76 is scratch at @B2,
   @B3 (not for v = 0), @B4, @B6 and @B7. A = the result; X and Y untouched. */
void par_scale_voicing(fw *f, fw_regs *r);

#endif
