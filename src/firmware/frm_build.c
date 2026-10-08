#include "frm_build.h"
#include "par_segment.h"
#include "data/fw_data.h"

/* ---- frm_build_rest_frame $B41D ---- */

/* $B41D-$B426: LDA $B782,Y / STA $1FFF,Y for Y = 20 down to 1 (DEY / BNE): frm_rest_frame = frm_template.
   Leaves A = frm_template[0] and Y = 0. */
static void copy_template(fw *f, fw_regs *r)
{
    r->y = FRAME_SIZE;
    do {
        r->a = fw_rom8(f, (uint16_t)(FRM_TEMPLATE - 1 + r->y));
        fw_wr8(f, (uint16_t)(FRM_REST_FRAME - 1 + r->y), r->a);
        r->y--;
    } while (r->y != 0);
}

/* $B428-$B438: Y = the pause phoneme; for X = 1, 3, 5, 7 (INX / INX, CPX #8 / BCC): frm_rest_frame[X] =
   (par_row_ptr),Y, then par_next_param (the same row of the next parameter). Leaves X = 9. */
static void pause_formants(fw *f, fw_regs *r)
{
    r->y = FRM_PAUSE_PHONEME;
    r->x = FRAME_FN;
    do {
        r->a = fw_ind_y(f, PAR_ROW_PTR, r->y);
        fw_wr8(f, (uint16_t)(FRM_REST_FRAME + r->x), r->a);
        par_next_param(f, r);
        r->x += 2;
    } while (r->x < FRAME_F3 + 1);
}

void frm_build_rest_frame(fw *f, fw_regs *r)
{
    copy_template(f, r);
    pause_formants(f, r);
    r->a = fw_rd8(f, PAR_PARAMS_PTR);          /* $B43A-$B444: par_row_ptr = par_params_ptr */
    fw_wr8(f, PAR_ROW_PTR, r->a);
    r->a = fw_rd8(f, PAR_PARAMS_PTR + 1);
    fw_wr8(f, PAR_ROW_PTR + 1, r->a);
}

/* ---- frm_copy_rest_frame $B445 ---- */

void frm_copy_rest_frame(fw *f, fw_regs *r)
{
    r->y = FRAME_SIZE - 1;                     /* LDY #$13 */
    do {
        r->a = fw_rd8(f, (uint16_t)(FRM_REST_FRAME + r->y));
        fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);
        r->y--;
    } while (!(r->y & 0x80));                  /* DEY / BPL */
}

/* ---- frm_snapshot $B917 ---- */

/* $B917-$B920: LDA $16CC,Y / STA $16DC,Y for Y = 10 down to 1: frm_snap_prev = frm_snap. */
static void save_snapshot(fw *f, fw_regs *r)
{
    r->y = 10;
    do {
        r->a = fw_rd8(f, (uint16_t)(FRM_SNAP - 1 + r->y));
        fw_wr8(f, (uint16_t)(FRM_SNAP_PREV - 1 + r->y), r->a);
        r->y--;
    } while (r->y != 0);
}

/* $B922-$B92D: for X = 10 down to 1: Y = frm_snapshot_map[X - 1] ($B772,X), frm_snap[X - 1] ($16CC,X) = the
   producer frame's byte Y. */
static void take_snapshot(fw *f, fw_regs *r)
{
    r->x = 10;
    do {
        r->y = fw_rom8(f, (uint16_t)(FRM_SNAPSHOT_MAP - 1 + r->x));
        r->a = fw_ind_y(f, ISR_FILL_PTR, r->y);
        fw_wr8(f, (uint16_t)(FRM_SNAP - 1 + r->x), r->a);
        r->x--;
    } while (r->x != 0);
}

void frm_snapshot(fw *f, fw_regs *r)
{
    save_snapshot(f, r);
    take_snapshot(f, r);
}

/* ---- frm_clamp_mark_space $B930 ---- */

/* $B930-$B93A: Y = 10, A = b10; INY; ASL A: FW_TMP_75 = 2c; b11 = @M. */
static void mark_space(fw *f, fw_regs *r)
{
    r->y = FRAME_PITCH;
    r->a = fw_ind_y(f, ISR_FILL_PTR, r->y);
    r->y++;
    r->a = (uint8_t)(r->a << 1);
    fw_wr8(f, FW_TMP_75, r->a);
    r->a = fw_rd8(f, CMD_MARK_SPACE);
    fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);
}

/* $B93C-$B945: A = 160 - 2c (SEC / SBC, 8 bits); CMP b11 / BCS: written when A < b11 (unsigned). */
static void upper_cap(fw *f, fw_regs *r)
{
    r->a = (uint8_t)(FRM_MARK_SPACE_CAP - fw_rd8(f, FW_TMP_75));
    if (r->a < fw_ind_y(f, ISR_FILL_PTR, r->y))
        fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);
}

/* $B947-$B952: A = 47 - 2c (SEC / SBC, 8 bits); BMI: skipped when bit 7 is set; CMP b11 / BCC: written when
   A >= b11 (unsigned). */
static void lower_bound(fw *f, fw_regs *r)
{
    r->a = (uint8_t)(FRM_MARK_SPACE_BASE - fw_rd8(f, FW_TMP_75));
    if (!(r->a & 0x80) && r->a >= fw_ind_y(f, ISR_FILL_PTR, r->y))
        fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);
}

/* $B954-$B95A: A = 11; CMP b11 / BCC: written when 11 >= b11. */
static void floor_11(fw *f, fw_regs *r)
{
    r->a = FRM_MARK_SPACE_FLOOR;
    if (r->a >= fw_ind_y(f, ISR_FILL_PTR, r->y))
        fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);
}

void frm_clamp_mark_space(fw *f, fw_regs *r)
{
    mark_space(f, r);
    upper_cap(f, r);
    lower_bound(f, r);
    floor_11(f, r);
}

/* ---- frm_raise_formants $B95D ---- */

/* LDY #n; CLC; ADC (fill),Y; JSR par_clamp; STA (fill),Y: frame byte n += A, clamped to 1-63 ($B96B-$B973 for F1,
   $B978-$B980 for F2). */
static void raise(fw *f, fw_regs *r, uint8_t n)
{
    r->y = n;
    r->a = (uint8_t)(r->a + fw_ind_y(f, ISR_FILL_PTR, r->y));
    par_clamp(f, r);
    fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);
}

void frm_raise_formants(fw *f, fw_regs *r)
{
    r->y = FRAME_PITCH;                        /* $B95D-$B966: SEC; SBC #$20; BPL, else RTS */
    r->a = (uint8_t)(fw_ind_y(f, ISR_FILL_PTR, r->y) - FRM_RAISE_PITCH);
    if (r->a & 0x80)
        return;
    r->a >>= 2;                                /* $B967-$B969: LSR, LSR, STA $75 */
    fw_wr8(f, FW_TMP_75, r->a);
    raise(f, r, FRAME_F1);
    r->a = (uint8_t)(fw_rd8(f, FW_TMP_75) >> 1);   /* $B975-$B977: LDA $75; LSR */
    raise(f, r, FRAME_F2);
}
