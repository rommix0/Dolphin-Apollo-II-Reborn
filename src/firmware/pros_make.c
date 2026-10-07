#include "pros_make.h"
#include "pros_gen.h"
#include "pros_phrase.h"
#include "pros_seg.h"
#include "pros_stress.h"

/* $CA92-$CAAD: LDA pros_params[i]; STA, the phrase parameters of a statement (@R A-E), in the ROM's order. */
static void copy_params(fw *f)
{
    static const struct { uint16_t to; uint8_t index; } copies[] = {
        { PROS_STRESS_PARAM, 2 }, { PROS_NUCLEUS_PARAM, 3 }, { PROS_FINAL_PARAM, 4 },
        { PROS_BASELINE, 0 }, { PROS_BASELINE_END, 1 },
    };
    for (unsigned i = 0; i < sizeof copies / sizeof copies[0]; i++)
        fw_wr8(f, copies[i].to, fw_rd8(f, (uint16_t)(PROS_PARAMS + copies[i].index)));
}

void pros_make_segments(fw *f, fw_regs *r)
{
    copy_params(f);
    pros_reset_seg_ptrs(f);                             /* $CAB0 */
    r->a = (uint8_t)(PROS_SEG_FLAGS >> 8);              /* its last LDA #$0E; X and Y untouched */
    pros_mark_stress(f, r);                             /* $CAB3: the entry Y reaches its first isr_par_ready */
    pros_mark_phrases(f, r);                            /* $CAB6: pros_mark_stress's Y reaches its first one */
    r->a = pros_phrase_gen(f, &r->x, &r->y);            /* $CAB9 (it reads no register), then RTS $CABC */
}
