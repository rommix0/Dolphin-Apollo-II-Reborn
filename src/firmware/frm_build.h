#ifndef FW_FRM_BUILD_H
#define FW_FRM_BUILD_H
/* The frame builder (ROM $B41D-$B44F and $B917-$B982), decompiled (M4.5 spec 2.2, 2.6): the rest frame that
   par_setup_voice builds and copies into both frame buffers, and the steps that finish a producer frame (the frame
   at isr_fill_ptr) after the tracks: the snapshot of its parameter bytes, the mark-space clamp of b11 and the formant
   raise at high pitch codes. Each routine takes the registers at entry in r and leaves in r the A, X and Y that the
   ROM leaves at its RTS. r->p is not used: none reads an entry flag, and no caller reads a flag after any of them.
   The producer frame is read with fw_ind_y and written with fw_wr_ind_y on isr_fill_ptr; ROM tables are read with
   fw_rom8 at their ROM addresses. */
#include "fw.h"

#define FRM_PAUSE_PHONEME     0x3B   /* $B428 LDY #$3B: the pause phoneme, whose FN, F1, F2, F3 the rest frame takes */
#define FRM_MARK_SPACE_CAP    0xA0   /* $B93C: b11 is at most 160 - 2 x b10 (unsigned) ... */
#define FRM_MARK_SPACE_BASE   0x2F   /* $B947: ... at least 47 - 2 x b10 unless that has bit 7 set ... */
#define FRM_MARK_SPACE_FLOOR  0x0B   /* $B954: ... and at least 11 */
#define FRM_RAISE_PITCH       0x20   /* $B962: pitch codes b10 from 32 up raise F1 and F2 */

/* $B41D: frm_rest_frame ($2000) = frm_template, copied 1-based for Y = 20 down to 1; then its FN, F1, F2, F3 (b1, b3,
   b5, b7, X = 1, 3, 5, 7) = row FRM_PAUSE_PHONEME of the target rows at par_row_ptr, moved on one parameter block
   by par_next_param after each; then par_row_ptr = par_params_ptr. par_setup_voice leaves par_row_ptr at
   par_params_ptr (P1's target row) before the call. r in: not read (Y and X are loaded first). r out: A =
   par_params_ptr's high byte, X = 9 (the INX / INX loop ends at CPX #8), Y = FRM_PAUSE_PHONEME. */
void frm_build_rest_frame(fw *f, fw_regs *r);

/* $B445: the 20 bytes of frm_rest_frame into the producer frame, Y = 19 down to 0 (DEY / BPL). r in: X passes
   through. r out: A = frm_rest_frame[0], X unchanged, Y = $FF. */
void frm_copy_rest_frame(fw *f, fw_regs *r);

/* $B917: frm_snap_prev = frm_snap (1-based, Y = 10 down to 1); then frm_snap[X - 1] = the producer frame's byte
   frm_snapshot_map[X - 1], X = 10 down to 1 (b10, AHF, A3, A2, A1, ALF, FN, F1, F2, F3 in frm_snap[0..9]).
   r in: not read. r out: A = the frame byte of the last entry read (frm_snapshot_map[0], the pitch code b10), X = 0,
   Y = that map entry. */
void frm_snapshot(fw *f, fw_regs *r);

/* $B930: with c = b10 (the pitch code), FW_TMP_75 = 2c (8 bits, ASL). b11 = @M (cmd_mark_space); then b11 =
   160 - 2c when b11 is above it (CMP / BCS, unsigned; 160 - 2c wraps in 8 bits for c > 80); then b11 = 47 - 2c
   when that 8-bit difference has bit 7 clear (BMI) and is not below b11 (CMP / BCC); then b11 = 11 when b11 is not
   above 11. Each write goes to the frame (up to four). r in: X passes through. r out: A = $0B, X unchanged,
   Y = FRAME_MARK_SPACE (11). */
void frm_clamp_mark_space(fw *f, fw_regs *r);

/* $B95D: d = b10 - 32 (8 bits). With d's bit 7 set (BPL not taken) it returns at once: A = d, Y = FRAME_PITCH (10).
   Otherwise FW_TMP_75 = d >> 2; F1 (b3) = par_clamp(F1 + (d >> 2)), then F2 (b5) = par_clamp(F2 + (d >> 3)), each
   sum 8 bits (CLC / ADC). r in: X passes through (par_clamp keeps it). r out: A = d and Y = 10 (the early return),
   else A = the new F2 and Y = FRAME_F2 (5); X unchanged. */
void frm_raise_formants(fw *f, fw_regs *r);

#endif
