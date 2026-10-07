#include "par_boundary.h"

void par_scale_duration(fw *f, fw_regs *r)
{
    if (r->a == 0)                                           /* CMP #0; BEQ $C2B3 */
        return;
    fw_wr8(f, FW_TMP_78, r->y);                              /* STY $78 */
    if (fw_rd8(f, CMD_SPEED) >= PAR_FAST_SPEED) {            /* LDY cmd_speed; CPY #5; BCC $C2B1 */
        uint8_t i = r->a < PAR_SPEED_MAP_LAST ? r->a : PAR_SPEED_MAP_LAST;   /* CMP #$1F; BCC $C2AE; LDA #$1F */
        r->a = fw_ind_y(f, CMD_SPEED_MAP, i);                /* TAY; LDA ($3A),Y */
    }
    r->y = fw_rd8(f, FW_TMP_78);                             /* LDY $78 */
}
