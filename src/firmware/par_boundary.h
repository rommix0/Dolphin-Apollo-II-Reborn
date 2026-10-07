#ifndef FW_PAR_BOUNDARY_H
#define FW_PAR_BOUNDARY_H
/* The boundary pass of the playback set-up (ROM $BE6A-$C0D1 and $C29C-$C2B3), decompiled (M4.4a spec 2.2): at each
   segment, the boundary value and the transition durations of each parameter, by the dominance rules. r in = the
   registers at entry (r->p is not read); r out = the A, X and Y the ROM leaves at its RTS. */
#include "fw.h"

#define PAR_FAST_SPEED      5      /* par_scale_duration: CPY #5, the first speed (cmd_speed) that maps durations */
#define PAR_SPEED_MAP_LAST  0x1F   /* par_scale_duration: CMP #$1F; LDA #$1F, the last index of a 32-byte map */

/* $C29C: a transition duration A. 0 passes, and FW_TMP_78 is not written. Otherwise FW_TMP_78 = Y; at cmd_speed >= 5,
   A = cmd_speed_map[min(A, 31)] (through the pointer at $3A); slower speeds leave A. Y is restored from FW_TMP_78,
   X untouched. */
void par_scale_duration(fw *f, fw_regs *r);

#endif
