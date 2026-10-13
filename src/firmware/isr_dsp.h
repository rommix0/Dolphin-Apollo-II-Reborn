#ifndef FW_ISR_DSP_H
#define FW_ISR_DSP_H
/* Speech timing and the DSP (ROM $ACE8-$ACF9, $B2D9-$B397, $B465-$B472), decompiled (M4.6b spec 2.1, 2.6). Each
   routine takes the registers at entry in r and leaves in r the A, X and Y the ROM leaves at its RTS (or RTI); r->p
   is not used: no caller reads a flag, and the PHP/SEI ... PLP pairs only hold interrupts off, which the C, taking
   no emulated time, needs no more (M4.6b spec 3.5). SFRs are read and written through the host in the ROM's order
   (fw_sfr*), each SEB/CLB as a read then a write. */
#include "fw.h"

/* The SFRs of speech timing (M4.6b spec 2.5; ghidra740/annotations/sfr.txt) */
#define SFR_T2_PRE      0xDE
#define SFR_T2_RELOAD   0xDF
#define SFR_T3_PRE      0xE0
#define SFR_T3_RELOAD   0xE1
#define SFR_P23_FUNC    0xED
#define SFR_P3          0xEE   /* bits 3-6: the inverted volume level; bit 0 set by isr_timer_start */
#define SFR_P2          0xF0   /* bit 0: the DSP reset line */
#define SFR_ICR2        0xFE   /* timers 1-3: enable bits 0, 2, 4, request bits 1, 3, 5 */

/* $ACE8: A = the volume 0-15 (0 silent): P3 bits 3-6 = its inverted level, (A ^ $F) & $F shifted left 3
   (util_shl3), kept in the scratch byte $75; the other P3 bits kept (AND #$87). A = the value written to P3; X and Y
   untouched. */
void isr_set_volume(fw *f, fw_regs *r);
/* $B46C: isr_set_volume with A = cmd_volume >> 1. */
void isr_volume_half(fw *f, fw_regs *r);

#endif
