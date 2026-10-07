#ifndef FW_PAR_ADDR_H
#define FW_PAR_ADDR_H
/* ROM code addresses of the playback set-up (M4.4a spec 2.2) and of the three frame-builder routines that
   par_setup_voice calls (2.4), checked against build/ghidra740/dump and the JSR/JMP operands in the ROM image. */
#define ROM_PAR_SETUP_VOICE        0xB3A4   /* called by cmd_apply_pitch $A845; the C cmd_apply_pitch calls the C */
#define ROM_FRM_BUILD_REST_FRAME   0xB41D   /* M4.5; called by par_setup_voice $B404 */
#define ROM_FRM_COPY_REST_FRAME    0xB445   /* M4.5; called by par_setup_voice $B407, $B416 (and par_play $B8DA) */
#define ROM_FRM_CLAMP_MARK_SPACE   0xB930   /* M4.5; called by par_setup_voice $B40A, $B419 (and par_play $B8E0,
                                               frm_finish_frame $B9BD) */
#define ROM_PROS_MICROPROSODY      0xB983   /* called by par_read_segment $BD67 */
#define ROM_PAR_PLAY_START         0xBAF2   /* called by boot_play_body $B2B6 */
#define ROM_PAR_RESET_TRACKS       0xBB35   /* called by par_play_start $BAF2 */
#define ROM_PAR_NEXT_SEGMENT       0xBCF3   /* called by par_play $B900 */
#define ROM_PAR_NEXT_ROW           0xBD07   /* called by par_setup_voice $B3BE, $B3C7; par_boundary_cur_dom $BF09,
                                               $BF14; par_boundary_next_dom $BF4E, $BF59; par_read_dominance $BF7B,
                                               $BF8D, $BFBF */
#define ROM_PAR_NEXT_PARAM         0xBD14   /* called by frm_build_rest_frame $B431, par_read_segment $BD9D,
                                               par_boundary_jump $BEAC */
#define ROM_PAR_CLAMP              0xBD24   /* called by frm_raise_formants $B970, $B97D; par_read_segment $BD8F;
                                               par_read_dominance $BF75; par_boundary_value $C0BF */
#define ROM_PAR_READ_SEGMENT       0xBD33   /* called by par_next_segment $BCFD */
#define ROM_PROS_SCALE_TARGET      0xBDD3   /* called by par_play_start $BAFE, par_read_segment $BD64 */
#define ROM_PAR_BOUNDARY_PASS      0xBE6A   /* called by par_next_segment $BD00 */
#define ROM_PAR_BOUNDARY_JUMP      0xBE94   /* called by par_boundary_pass $BE7F */
#define ROM_PAR_STORE_BOUNDARY     0xBEB5   /* called by par_boundary_jump $BEA9, par_boundary_value $C0AE */
#define ROM_PAR_BOUNDARY_CUR_DOM   0xBEE2   /* called by par_boundary_pass $BE90 */
#define ROM_PAR_BOUNDARY_NEXT_DOM  0xBF27   /* called by par_boundary_pass $BE8C */
#define ROM_PAR_READ_DOMINANCE     0xBF6C   /* called by par_boundary_cur_dom $BEFE, par_boundary_next_dom $BF43 */
#define ROM_PAR_SCALE_VOICING      0xBFC3   /* called by par_read_segment $BD96, par_read_dominance $BF87 */
#define ROM_PAR_BOUNDARY_VALUE     0xC039   /* called by par_boundary_cur_dom $BF19, par_boundary_next_dom $BF5E */
#define ROM_PAR_PLAN_TRACKS        0xC0D2   /* called by par_next_segment $BD03 */
#define ROM_PAR_SCALE_DURATION     0xC29C   /* called by par_boundary_cur_dom $BF03, $BF0E; par_boundary_next_dom
                                               $BF48, $BF53 */

#endif
