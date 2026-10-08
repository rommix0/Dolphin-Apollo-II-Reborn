#include <string.h>
#include "engine/thunks.h"
#include "firmware/lts.h"
#include "firmware/tn_rules.h"
#include "firmware/tn_convert.h"
#include "firmware/cmd_match.h"
#include "firmware/cmd_scan.h"
#include "firmware/cmd_run.h"
#include "firmware/cmd_apply.h"
#include "firmware/pros_seg.h"
#include "firmware/pros_decl.h"
#include "firmware/ph_expand.h"
#include "firmware/pros_gen.h"
#include "firmware/pros_make.h"
#include "firmware/pros_stress.h"
#include "firmware/pros_phrase.h"
#include "firmware/par_segment.h"
#include "firmware/pros_play.h"
#include "firmware/par_boundary.h"
#include "firmware/par_start.h"
#include "firmware/data/fw_data.h"

#ifdef _MSC_VER
#pragma warning(disable : 4611)   /* setjmp with no C++ objects to destroy: the trap of fw faults */
#endif

#define P2_LOWER_WINDOW 0x80

static bool window_on(const machine *m) { return (m50734_port_pins(&m->sfr, 2) & P2_LOWER_WINDOW) != 0; }

static fw_call_result host_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    fw_bridge *b = ctx;
    machine_regs mr = { r->a, r->x, r->y, b->m->cpu.p };   /* the routine starts with the machine's flags */
    machine_call_result res = machine_rom_call(b->m, addr, &mr, budget);
    r->a = mr.a;
    r->x = mr.x;
    r->y = mr.y;
    r->p = mr.p;
    b->f.low_window = window_on(b->m);
    if (res == MCALL_RETURNED)
        return FW_CALL_RETURNED;
    return res == MCALL_UNWOUND ? FW_CALL_UNWOUND : FW_CALL_FAILED;
}

/* SEB7/CLB7 $F0: a read-modify-write of P2 through the bus, as the CPU does it */
static void host_set_low_window(void *ctx, bool on)
{
    machine *m = ((fw_bridge *)ctx)->m;
    uint8_t v = m->bus.read(m->bus.ctx, M50734_P2);
    m->bus.write(m->bus.ctx, M50734_P2, (uint8_t)(on ? v | P2_LOWER_WINDOW : v & ~P2_LOWER_WINDOW));
}

/* The firmware's SFR read. Only UART_STATUS $E7, the one register it polls (par_play $B895), is read, as the CPU's
   LDA reads it, through the bus; it has no side effect (m50734.c: bit 1, transmitter ready, always set; bit 0,
   receive full). Any other SFR reads $FF without reaching the bus, so that no read side effect (UART_RX $E5 clears
   receive full) can happen behind the ROM's back. */
static uint8_t host_read_sfr(void *ctx, uint8_t addr)
{
    machine *m = ((fw_bridge *)ctx)->m;
    return addr == M50734_UART_STATUS ? m->bus.read(m->bus.ctx, addr) : 0xFF;
}

bool fw_bridge_init(fw_bridge *b, machine *m)
{
    const fw_image *img = fw_image_get();
    b->m = m;
    b->thunk_calls = 0;
    b->unwinds = 0;
    b->host.ctx = b;
    b->host.rom_call = host_rom_call;
    b->host.set_low_window = host_set_low_window;
    b->host.read_sfr = host_read_sfr;
    fw_init(&b->f, m->ram, img, &b->host);
    return img != NULL;
}

/* The register mapping of one routine: reads its input registers into locals before calling the C (a ROM call
   made from inside the C overwrites the machine's registers), then sets its output registers. */
typedef void (*thunk_regs)(fw *f, cpu740 *c);

/* The frame every thunk shares: set the fault trap (saving an outer thunk's, as a ROM call from C can reach
   another thunk on the same bridge), sync the window, run the routine; then restore the trap and either leave the
   machine where an unwound ROM call left it, report the fault, or emulate the RTS. */
static bool run_thunk(machine *m, fw_bridge *b, thunk_regs regs)
{
    if (m != b->m) {   /* registers come from m, RAM and host calls from b->m: they must be one machine */
        m->fault = MFAULT_FIRMWARE;
        m->fault_addr = m->cpu.pc;
        return true;
    }
    b->thunk_calls++;
    uint16_t entry = m->cpu.pc;
    uint8_t entry_sp = m->cpu.s;
    jmp_buf trap, *saved = b->f.trap;
    b->f.trap = &trap;
    b->f.low_window = window_on(m);
    if (setjmp(trap) == 0)
        regs(&b->f, &m->cpu);
    b->f.trap = saved;
    if (b->f.unwound) {   /* a ROM call reset the stack: the C is abandoned, the machine goes on where the ROM went */
        b->f.unwound = false;
        b->unwinds++;
        return true;
    }
    if (b->f.fault) {
        if (m->fault == MFAULT_NONE) {
            m->fault = MFAULT_FIRMWARE;
            m->fault_addr = b->f.fault_addr;
        }
        m->cpu.pc = entry;   /* a failed ROM call leaves the PC inside the called routine and SP on its sentinel frame */
        m->cpu.s = entry_sp;
        return true;
    }
    machine_return(m);
    return true;
}

/* cmd_apply_speaker $A78C: X, Y in (handed to par_setup_voice, which does not read them). A = the cmd_speaker it
   read, X and Y as par_setup_voice leaves them, out. boot_reset reads all three: after its second call ($A675),
   bank_load_ramcode saves X in $76 and Y in $78; A is preserved into bank_scan and, after the first call
   ($A651), through boot_selftest_sound. */
static void cmd_apply_speaker_regs(fw *f, cpu740 *c)
{
    uint8_t x = c->x, y = c->y;
    c->a = cmd_apply_speaker(f, &x, &y);
    c->x = x;
    c->y = y;
}

/* cmd_run_due $A880: X in. X out unchanged (every handler ends at cmd_run_done's PLA/TAX). Y out = cmd_next,
   as its last LDY leaves it. The only ROM caller, tn_convert_clause, reads X after it ($B20F CPX tn_pos, then
   LDA $1A00,X) and writes Y before reading it; Y is declared because the ROM leaves it defined. */
static void cmd_run_due_regs(fw *f, cpu740 *c)
{
    uint8_t x = c->x;
    cmd_run_due(f, x);
    c->x = x;
    c->y = fw_rd8(f, CMD_NEXT);
}

/* cmd_scan_clause $ACFA: no register input or output (boot_loop_text calls tn_convert_clause next, which starts
   with LDM/LDX #0 and reads no register) */
static void cmd_scan_clause_regs(fw *f, cpu740 *c)
{
    (void)c;
    cmd_scan_clause(f);
}

/* cmd_match_table $ADBF: no register input (X is passed through); Y out, A = $FF at the end marker (Y = 0), else
   the entry X (cmd_match_pattern's PLA/TAX), X preserved. cmd_imm_match reads Y and the RAM it leaves; the ROM
   cmd_scan_clause reads Y and X; A only reaches the dead stack (ser_rx_flush_cmd's PHA). All three are declared. */
static void cmd_match_table_regs(fw *f, cpu740 *c)
{
    uint8_t x = c->x;
    uint8_t y = cmd_match_table(f);
    c->y = y;
    c->a = y ? x : 0xFF;
}

/* lts_affix_next $AF11: Y in; A, Y out */
static void lts_affix_next_regs(fw *f, cpu740 *c)
{
    uint8_t y = c->y;
    c->a = lts_affix_next(f, &y);
    c->y = y;
}

/* lts_affix_prev $AF1C: Y in; A, Y out */
static void lts_affix_prev_regs(fw *f, cpu740 *c)
{
    uint8_t y = c->y;
    c->a = lts_affix_prev(f, &y);
    c->y = y;
}

/* lts_sym_vowel $B007: A out */
static void lts_sym_vowel_regs(fw *f, cpu740 *c) { c->a = lts_sym_vowel(f); }

/* lts_match_symbol $B05E: A, Y in; A out, Y preserved */
static void lts_match_symbol_regs(fw *f, cpu740 *c)
{
    uint8_t a = c->a, y = c->y;
    c->a = lts_match_symbol(f, a, y);
    c->y = y;
}

/* tn_say_char $B0C3: A in; X out */
static void tn_say_char_regs(fw *f, cpu740 *c)
{
    uint8_t ch = c->a;
    c->x = tn_say_char(f, ch);
}

/* lts_match_list $B0E3: A = $FF and Z (set = no rule matched) out */
static void lts_match_list_regs(fw *f, cpu740 *c)
{
    bool matched = lts_match_list(f);
    c->a = 0xFF;
    c->p = (uint8_t)(matched ? c->p & ~CPU740_Z : c->p | CPU740_Z);
}

/* tn_rule_skip_text $B138: Y in; A, Y out */
static void tn_rule_skip_text_regs(fw *f, cpu740 *c)
{
    uint8_t y = c->y;
    c->a = tn_rule_skip_text(f, &y);
    c->y = y;
}

/* tn_rule_skip_field $B142: A, Y in; Y (= 0) out */
static void tn_rule_skip_field_regs(fw *f, cpu740 *c)
{
    uint8_t a = c->a, y = c->y;
    tn_rule_skip_field(f, a, &y);
    c->y = y;
}

/* tn_append_phonetic $B154: A (length), X, Y in; A = $FF, X, Y out (set exactly, though cmd_put_phonetic
   restores A and Y) */
static void tn_append_phonetic_regs(fw *f, cpu740 *c)
{
    uint8_t len = c->a, x = c->x, y = c->y;
    c->a = tn_append_phonetic(f, len, &x, &y);
    c->x = x;
    c->y = y;
}

/* lts_convert_char $B172: A in; no register output (tn_convert_clause clears P2.7 and reloads X) */
static void lts_convert_char_regs(fw *f, cpu740 *c)
{
    uint8_t ch = c->a;
    lts_convert_char(f, ch);
}

/* tn_convert_clause $B1E6: no register input or output (boot_loop_text returns straight after it) */
static void tn_convert_clause_regs(fw *f, cpu740 *c)
{
    (void)c;
    tn_convert_clause(f);
}

/* par_setup_voice $B3A4: A, X, Y in; none is read (Y reaches frm_build_rest_frame, whose first instruction is
   LDY #$14 at $B41D). A = $0B, X = 9 and Y = $0B out, as its ROM calls leave them: frm_build_rest_frame's loop ends
   at X = 9 ($B436 CPX #8), frm_clamp_mark_space ends with LDA #$0B and Y = $0A + 1 ($B930-$B954). The C's ROM calls
   overwrite the machine's registers, so all three are set from the C. Its one ROM caller, cmd_apply_pitch ($A845),
   restores A (PLA at $A848) and returns; X and Y reach RAM: cmd_apply_speaker ($A798) calls cmd_apply_speed, which
   keeps A and Y and does not touch X, and after boot_reset's second call ($A675) bank_load_ramcode stores them
   (STY $78, STX $76 at $A601/$A603). No caller reads a flag. The C cmd_apply_pitch calls the C directly, so the
   hook fires only while the ROM cmd_apply_pitch runs (its "alone" mode). Declared: A, X, Y. */
static void par_setup_voice_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_setup_voice(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* pros_microprosody $B983: no register input (it loads A first). A = par_next_phoneme, or pros_play_target plus or
   minus 5 (stored or not), out; X and Y untouched. The only caller, par_read_segment ($BD67), reads Y next ($BD6A
   CPY #0, $BD70 DEY, $BD71 LDA ($24),Y) and X ($BD76 STA $17BF,X), and loads A first ($BD71). No caller reads a
   flag (CPY follows). Declared: A, X, Y. */
static void pros_microprosody_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    pros_microprosody(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_play_start $BAF2: A, X, Y in; none is read (X passes through untouched, A and Y are loaded). A = 0
   ($BB2C), X = the entry X, Y = pros_seg_phoneme[0] ($BB0A TAY) out. Its one ROM caller, boot_play_body ($B2B6),
   calls isr_speech_start next, which writes X ($B323 LDX #0) and A ($B32B LDA) before reading them; Y is not read
   before par_play's LDY pros_seg_count ($B8B1). No caller reads a flag. It fires in "all" mode (boot_loop_play
   stays ROM until M4.4b). Declared: A, X, Y. */
static void par_play_start_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_play_start(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_reset_tracks $BB35: A, X and Y preserved (PHA / TXA / PHA / TYA / PHA, then PLA / TAY / PLA / TAX / PLA at
   $BB6E-$BB72); no ROM call in the C, so the thunk leaves all three. Its one ROM caller, par_play_start ($BAF2),
   calls pros_reset_seg_ptrs next (A = $0E, X and Y untouched); then pros_scale_target pushes and restores X and Y
   ($BDD3-$BDD6, $BE48-$BE4B), and X leaves par_play_start as its exit X while Y is replaced at $BB0A (TAY). No
   caller reads a flag (JSR follows). Declared: A, X, Y. */
static void par_reset_tracks_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_reset_tracks(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_next_segment $BCF3: A, X, Y in; none is read (A is loaded first, X is set to 0 by par_read_segment, Y is
   loaded). A, X and Y out as par_plan_tracks leaves them (A = its last result or 1, X = what the boundary pass
   left, Y = 0). Its one ROM caller, par_play ($B900), loads A next ($B903 LDA par_frames_left); then either
   $B8B1 LDY, or isr_par_ready (which preserves A, X and Y) and par_step_tracks, which loads X and Y before reading
   them. No caller reads a flag. It fires in "all" mode (par_play stays ROM until M4.4b). Declared: A, X, Y. */
static void par_next_segment_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_next_segment(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_next_row $BD07: no register input. A = par_row_ptr's new low byte out; X and Y untouched. par_setup_voice
   reads A ($B3C1 STA par_row2_ptr, $B3CA STA par_params_ptr); par_boundary_cur_dom and par_boundary_next_dom read
   Y ($BF0C, $BF51 LDA ($32),Y; at $BF17/$BF5C PLA / TAX write A and X); par_read_dominance reads X ($BF83 CPX #9
   after $BF7B, $BF9D TXA after $BF8D) and Y ($BF90 LDA ($32),Y), and its last call ($BFBF) hands A, X and Y to its
   callers, which read Y ($BF01, $BF46) and X ($BF06, $BF56 STA $171D,X). No caller reads a flag (STA, LDA, LDY,
   PLA or RTS follow). Declared: A, X, Y. */
static void par_next_row_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_next_row(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_next_param $BD14: no register input. A = par_row_ptr's new high byte out; X and Y untouched. Callers read X
   (frm_build_rest_frame $B434 INX; par_read_segment $BDA0 CPX #$0B; par_boundary_jump's next par_store_boundary,
   $BEB5 STA $16ED,X) and Y (the (par_row_ptr),Y loads at $B42C, $BD83, $BEA7); each loads A before reading it
   ($B43A, $BD83/$BDA4, $BEA7 LDA; or RTS at $BEB4 into par_boundary_pass's RTS and par_plan_tracks, which loads
   it first). No caller reads a flag (INX, CPX, DEC follow). Reached from ROM in every mode through
   frm_build_rest_frame (M4.5). Declared: A, X, Y. */
static void par_next_param_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_next_param(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_clamp $BD24: A in; A clamped out, X and Y untouched. frm_raise_formants reads A and Y ($B973, $B980 STA
   ($2C),Y); par_read_segment reads X ($BD92 CPX #$0A, $BD99 STA $17BF,X) and A (there, or through
   par_scale_voicing), and Y at the next $BD83; par_read_dominance reads A ($BF78 STA par_dominated_target) and X
   ($BF83 CPX #9); par_boundary_value hands A and X to par_store_boundary ($C0C2 JMP $C0AE; $BEB5 STA $16ED,X). No
   caller reads a flag (STA, CPX or JMP follow). Reached from ROM in every mode through frm_raise_formants (M4.5).
   Declared: A, X, Y. */
static void par_clamp_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_clamp(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_read_segment $BD33: A, X, Y in; none is read (LDX #0 first). A = par_boundary[1] (the copy loop's last LDA),
   X = 0 ($BDD0 LDX #0) and Y = 0 out. Its one ROM caller, par_next_segment ($BCFD), calls par_boundary_pass next,
   which loads A and Y first ($BE6A LDY, $BE6D LDA) and passes X on: the stores of par_boundary_jump
   (par_store_boundary, STA $16ED,X) and the TXA / PHA of the dominance loops ($BEF3, $BF38) read it. No caller
   reads a flag. Declared: A, X, Y. */
static void par_read_segment_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_read_segment(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* pros_scale_target $BDD3: Y in (A and X are written first; TYA / PHA, TXA / PHA). X and Y restored, A = the entry
   Y (the last PLA), out. par_play_start ($BAFE) loads A and Y next ($BB01 LDA, $BB0A TAY) and leaves X to its own
   exit; par_read_segment ($BD64) calls pros_microprosody, which loads A first and leaves X and Y, then reads Y
   ($BD6A CPY #0, $BD70 DEY) and X ($BD76 STA $17BF,X). No caller reads a flag (LDA or JSR follow). Declared: A, X,
   Y. */
static void pros_scale_target_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    pros_scale_target(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_boundary_pass $BE6A: X in (the running store index, handed down to par_store_boundary and par_boundary_value;
   par_read_segment leaves it 0 at $BDD0, but the thunk takes the machine's); A and Y are written first (LDY
   par_next_phoneme, LDA (par_rank_ptr),Y). A, X and Y out as par_boundary_jump, par_boundary_next_dom or
   par_boundary_cur_dom leaves them (RTS at $BE82, $BE8F, $BE93). The only caller, par_next_segment ($BD00), calls
   par_plan_tracks next ($BD03), which writes Y and A first (LDY #$0A, LDA #0) and never reads X; then it returns to
   par_play ($B903 LDA par_frames_left), which writes Y ($B8B1 LDY) or X (par_step_tracks' LDX #$0A at $C0FF, after
   isr_par_ready, which preserves them) before reading them. No caller reads a flag. Declared: A, X, Y. */
static void par_boundary_pass_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_boundary_pass(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_boundary_jump $BE94: X in (the store index; A and Y are written first, LDY #$0A, LDA #0). A = par_row_ptr's
   high byte (par_next_param's), X + 10 and Y = par_next_phoneme out (RTS at $BEB4). The only caller,
   par_boundary_pass ($BE7F), returns at once ($BE82), and nothing reads them (par_boundary_pass's thunk comment).
   Declared: A, X, Y. */
static void par_boundary_jump_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_boundary_jump(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_store_boundary $BEB5: A (the boundary value) and X (the store index) in. A = par_trans_dur, X + 1 and Y
   untouched out (RTS at $BEE1). par_boundary_jump ($BEA9) calls par_next_param next, which writes A and keeps X
   and Y: its loop reads Y at $BEA7 (LDA (par_row_ptr),Y) and hands X to the next par_store_boundary. In
   par_boundary_value ($C0AE) the RTS at $C0B1 follows, and X reaches the next parameter's TXA in the dominance loop
   ($BEF3, $BF38). No caller reads a flag (JSR, RTS). Declared: A, X, Y. */
static void par_store_boundary_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_store_boundary(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_boundary_cur_dom $BEE2: X in (the store index; A and Y are written first, AND #0 and LDY). A = 10, X + 10 and
   Y as par_boundary_value leaves it out (RTS at $BF26). The only caller, par_boundary_pass ($BE90), returns at once
   ($BE93), and nothing reads them (par_boundary_pass's thunk comment). Declared: A, X, Y. */
static void par_boundary_cur_dom_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_boundary_cur_dom(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_boundary_next_dom $BF27: as par_boundary_cur_dom (RTS at $BF6B); the only caller, par_boundary_pass ($BE8C),
   returns at once ($BE8F). Declared: A, X, Y. */
static void par_boundary_next_dom_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_boundary_next_dom(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_read_dominance $BF6C: X = the parameter p in (CPX #9 at $BF83; A and Y are written first). A = par_row_ptr's
   low byte (par_next_row's), X = p and Y = par_dominant_phoneme out (RTS at $BFC2). Its callers read Y next
   (LDA (par_row_ptr),Y at $BF01 in par_boundary_cur_dom, $BF46 in par_boundary_next_dom) and X after
   par_scale_duration, which keeps X and Y (STA $171D,X at $BF06, $BF56); A is overwritten by that LDA. No caller
   reads a flag. Declared: A, X, Y. */
static void par_read_dominance_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_read_dominance(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_scale_voicing $BFC3: A in; A scaled out, X and Y untouched. par_read_segment ($BD96) reads A and X ($BD99
   STA $17BF,X, $BD9C INX) and Y at the next $BD83; par_read_dominance ($BF87) reads A ($BF8A STA par_fixed), Y
   ($BF90 LDA ($32),Y) and X ($BF9D TXA). No caller reads a flag (STA follows). Declared: A, X, Y. */
static void par_scale_voicing_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_scale_voicing(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_boundary_value $C039: X (the store index) and Y (kept unless the halves path) in; A is written first (LDA #0).
   A = par_trans_dur (par_store_boundary's), X + 1, and Y = 0 on the halves path, else as it came in, out (RTS at
   $C0B1). Its callers, the dominance loops of par_boundary_cur_dom ($BF19) and par_boundary_next_dom ($BF5E),
   write A next (INC / LDA par_param_index); the next parameter reads X (TXA at $BEF3, $BF38) and writes Y
   (par_read_dominance's LDY at $BF6C) before reading it; after the tenth they return to par_boundary_pass (its thunk
   comment). No caller reads a flag. Declared: A, X, Y. */
static void par_boundary_value_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_boundary_value(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_plan_tracks $C0D2: A, X, Y in; none is read (LDY #$0A first, A loaded, X untouched). A = the last
   difference or 1, X = the entry X, Y = 0 out. Its one ROM caller, par_next_segment ($BD03), returns at once
   ($BD06 RTS), so what par_next_segment's caller reads applies: nothing. No caller reads a flag. Declared:
   A, X, Y. */
static void par_plan_tracks_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_plan_tracks(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_scale_duration $C29C: A and Y in (Y is saved in $78 and restored). A out; X untouched; Y as it came in.
   par_boundary_cur_dom reads A and X at $BF06 (STA $171D,X), Y at $BF0C (LDA ($32),Y) and A at $BF11 (STA
   par_trans_dur); par_boundary_next_dom reads A at $BF4B, Y at $BF51, A and X at $BF56. No caller reads a flag
   (STA follows). Declared: A, X, Y. */
static void par_scale_duration_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_scale_duration(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* cmd_hex_value $C520: A in; A and Z out, X and Y preserved (cmd_upcase keeps X). bank_cmd_select passes A on to
   bank_enter, and bank_load_ramcode saves X and Y in $76 and $78; the ROM handlers read A; cmd_set_voice
   branches on Z ($AA7A BNE). No caller reads N or C. */
static void cmd_hex_value_regs(fw *f, cpu740 *c)
{
    uint8_t v = cmd_hex_value(f, c->a);
    c->a = v;
    c->p = (uint8_t)(v == 0 ? c->p | CPU740_Z : c->p & ~CPU740_Z);
}

/* ph_append_phrase_pause $C9B8: no register input; A, X and Y out as they came in (PHA / TYA / PHA, then the exit
   JMP $CA6D into pros_append_segment's PLA / TAY / PLA / RTS; X untouched). The C makes no ROM call, so the thunk
   leaves all three as they are. The only caller, pros_phrase_gen ($D1DA, $D1DD), calls it twice in a row (the
   second call pushes the first's A and Y again), then LDA #0 at $D1E0; X and Y then reach isr_par_ready at $D155
   through JMP $D152 ($D209, $D30F). No caller reads a flag. Declared: A, X, Y. */
static void ph_append_phrase_pause_regs(fw *f, cpu740 *c)
{
    (void)c;
    ph_append_phrase_pause(f);
}

/* ph_append_pause $CA15: A in (the duration). A = $80 (its last LDA #$80), Y = the old pros_seg_count low byte
   (its LDY), X untouched, out. pros_phrase_gen reads A: at the end of the text ($D190) the second JSR at $D193
   takes the $80 the first leaves as its duration, and after a word pause ($D352) A goes on to isr_par_ready at
   $D155 (LDM does not change A). Y and X leave pros_phrase_gen after the second end pause ($D196-$D1A1 change
   neither) as its own Y and X outputs. No caller reads a flag (LDY $78, the next LDY, or LDA follows).
   Declared: A, X, Y. */
static void ph_append_pause_regs(fw *f, cpu740 *c)
{
    c->y = ph_append_pause(f, c->a);
    c->a = PROS_SEG_FLAGS_GEN;
}

/* pros_append_segment $CA3B: A, X, Y in (to isr_par_ready); A, X, Y out as they came in (isr_par_ready preserves
   them; the ROM pushes A and Y after it and pulls them at $CA6D). They are set back from the locals because the
   ROM call from C overwrites the machine's registers. The only caller, pros_phrase_gen ($D3CC), loads A next
   (LDA pros_sym_flag); on its BBC4 path to $D152 X and Y reach isr_par_ready at $D155, else LDY $78 reloads Y.
   No caller reads a flag. Declared: A, X, Y. */
static void pros_append_segment_regs(fw *f, cpu740 *c)
{
    uint8_t a = c->a, x = c->x, y = c->y;
    pros_append_segment(f, a, x, y);
    c->a = a;
    c->x = x;
    c->y = y;
}

/* pros_reset_seg_ptrs $CA71: A = $0E (its last LDA, the high byte of pros_seg_flags) out; X and Y untouched (no
   ROM call in the C). pros_make_segments ($CAB0) calls pros_mark_stress next, whose first isr_par_ready ($CB1F)
   gets this Y (X is written first, $CAEC TAX); par_play_start ($BAF5) loads A next, and pros_scale_target pushes
   and restores X and Y. No caller reads a flag (LDA follows both calls). Declared: A, X, Y. */
static void pros_reset_seg_ptrs_regs(fw *f, cpu740 *c)
{
    pros_reset_seg_ptrs(f);
    c->a = (uint8_t)(PROS_SEG_FLAGS >> 8);
}

/* pros_make_segments $CA92: A, X, Y in; only Y is read, by pros_mark_stress's first isr_par_ready ($CB1F; A is
   written by the parameter copies, X by pros_mark_stress's TAX at $CAEC). A, X and Y out as pros_phrase_gen leaves
   them (RTS at $CABC). The C's ROM calls overwrite the machine's registers, so all three are set from the C. Its
   ROM callers, boot_reset ($A684), boot_selftest_sound ($A7B7) and cmd_say_phonetic ($A879, unreachable), call
   boot_loop_play next ($A687, $A7BA, $A87C), which loads A from $6B first ($A7D4) and, when that is 0 or the host
   aborted, returns with X and Y untouched; the C tn_convert_clause hands them to boot_loop_play the same way. No
   reader of X or Y was found; no caller reads a flag. They are defined, so all three are declared. */
static void pros_make_segments_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    pros_make_segments(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* pros_mark_stress $CABD: Y in (it reaches isr_par_ready at $CB1F at position 0; A and X are written first, $CABD
   LDA #0 and $CAEC TAX). A and Y out as at the last isr_par_ready ($CB1F preserves them), X = pros_pos
   (= tn_phon_len, $CB25 LDX), as the RTS at $CB2C leaves them. The only ROM caller, pros_make_segments ($CAB3),
   calls pros_mark_phrases next ($CAB6), which writes A ($CEF6 LDA #0) and X ($CF23 LDX pros_pos) before reading
   them; its first isr_par_ready ($CF28) gets this Y (when tn_phon_len is 1 its BEQ $CEEC leaves first, and the Y
   passes on to pros_phrase_gen, whose pros_declination_step writes Y at $D41D). No caller reads a flag (JSR and
   LDA follow). Declared: A, X, Y. */
static void pros_mark_stress_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    pros_mark_stress(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* pros_mark_phrases $CEF6: A, X, Y in; only Y is read, by its first isr_par_ready ($CF28; A and X are loaded first,
   $CF1A-$CF23). A = pros_word_pos, X = pros_pos (0, pros_store_phrase_len's LDX) and Y as the last position left it
   (the entry Y if none), out: its RTS at $CEF5. The C's ROM calls overwrite the machine's registers, so all three are
   set from the C. The only caller, pros_make_segments ($CAB6), calls pros_phrase_gen next, whose first instruction
   JSR pros_declination ($D129) writes A, X and Y before reading them (LDA pros_baseline_end; LDX pros_pos and LDY #0
   in pros_declination_step): no reader was found. No caller reads a flag. They are defined, so all three are
   declared. */
static void pros_mark_phrases_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    pros_mark_phrases(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* pros_store_phrase_len $D0FD: no register input. A (1, or pros_class when its bit 3 is set) and X = pros_pos (its
   LDX) out, Y untouched (no ROM call in the C). Both callers are in pros_mark_phrases: after $CF97 it loads A (LDA
   pros_symbol) and reads no X or Y before JSR pros_declination ($D04D), which writes X and Y first (LDX pros_pos,
   LDY #0); after $CEEC it loads A (LDA pros_word_pos) and returns, and X and Y go on to pros_make_segments, where
   pros_phrase_gen's JSR pros_declination writes them first. No reader was found; no caller reads a flag. They are
   defined, so A, X and Y are declared. */
static void pros_store_phrase_len_regs(fw *f, cpu740 *c)
{
    c->a = pros_store_phrase_len(f);
    c->x = fw_rd8(f, PROS_POS);
}

/* pros_phrase_gen $D129: no register input. A, X and Y out as the ROM leaves them: A = pros_seg_count's low byte
   before the final DEC, X = pros_pos (= tn_phon_len), Y = the second end pause's LDY pros_seg_count. Its one ROM
   caller, pros_make_segments, returns at once ($CABC RTS); its callers (boot_reset $A687, boot_selftest_sound $A7BA
   and the C tn_convert_clause) call boot_loop_play next, which loads A from $6B and, when that is 0, returns with X
   and Y untouched. No reader of them was found; they are defined, so all three are declared. */
static void pros_phrase_gen_regs(fw *f, cpu740 *c)
{
    uint8_t x, y;
    c->a = pros_phrase_gen(f, &x, &y);
    c->x = x;
    c->y = y;
}

/* pros_declination $D3DD: no register input; A (the step, or the restored pros_baseline on the rising path),
   X = pros_pos and Y = the step before its negation out. pros_mark_phrases ($D04D) loads A next and reloads X at
   $CF23, but Y reaches isr_par_ready at $CF28 (or leaves pros_mark_phrases through $CEEC, which writes no Y); in
   pros_phrase_gen ($D129) X and Y reach isr_par_ready at $D155. No caller reads a flag (LDA follows both calls).
   Declared: A, X, Y. */
static void pros_declination_regs(fw *f, cpu740 *c)
{
    c->a = pros_declination(f);
    c->x = fw_rd8(f, PROS_POS);
    c->y = pros_declination_y(f);
}

/* pros_declination_step $D40D: no register input; A = Y = pros_decl_step and X = pros_pos (its LDX) out. Reached
   by the JSR at $D3F2 (pros_declination's rising path: its X and Y then leave pros_declination unchanged) and by
   pros_declination's BCC / BEQ at $D3E3 / $D3E5. The hook is on the PC, so it also fires on that tail-branch: the
   stack then holds pros_declination's return address, and the thunk's emulated RTS pops it as the ROM's RTS at
   $D43D does, handing A, X and Y to pros_declination's caller. Declared: A, X, Y (pros_declination's outputs). */
static void pros_declination_step_regs(fw *f, cpu740 *c)
{
    uint8_t step = pros_declination_step(f);
    c->a = step;
    c->x = fw_rd8(f, PROS_POS);
    c->y = step;
}

/* ph_lengthen_count $D43E: A in; Y = A = the step count out, X untouched. The only caller, pros_phrase_gen ($D3A6),
   loads A (LDA ph_dur_range) and counts Y down (DEY at $D3B0), writing X (TAX) before reading it; no flag is read.
   Declared: A and Y (both defined as the count; Y is the one read). */
static void ph_lengthen_count_regs(fw *f, cpu740 *c)
{
    uint8_t n = ph_lengthen_count(f, c->a);
    c->a = n;
    c->y = n;
}

/* The hook of each routine: its register mapping run in the shared thunk frame, user = the bound fw_bridge. */
#define THUNK(name) \
    static bool t_##name(machine *m, void *user) { return run_thunk(m, user, name##_regs); }
THUNK(cmd_apply_speaker)
THUNK(cmd_run_due)
THUNK(cmd_scan_clause)
THUNK(cmd_match_table)
THUNK(lts_affix_next)
THUNK(lts_affix_prev)
THUNK(lts_sym_vowel)
THUNK(lts_match_symbol)
THUNK(tn_say_char)
THUNK(lts_match_list)
THUNK(tn_rule_skip_text)
THUNK(tn_rule_skip_field)
THUNK(tn_append_phonetic)
THUNK(lts_convert_char)
THUNK(tn_convert_clause)
THUNK(par_setup_voice)
THUNK(pros_microprosody)
THUNK(par_play_start)
THUNK(par_reset_tracks)
THUNK(par_next_segment)
THUNK(par_next_row)
THUNK(par_next_param)
THUNK(par_clamp)
THUNK(par_read_segment)
THUNK(pros_scale_target)
THUNK(par_boundary_pass)
THUNK(par_boundary_jump)
THUNK(par_store_boundary)
THUNK(par_boundary_cur_dom)
THUNK(par_boundary_next_dom)
THUNK(par_read_dominance)
THUNK(par_scale_voicing)
THUNK(par_boundary_value)
THUNK(par_plan_tracks)
THUNK(par_scale_duration)
THUNK(cmd_hex_value)
THUNK(ph_append_phrase_pause)
THUNK(ph_append_pause)
THUNK(pros_append_segment)
THUNK(pros_reset_seg_ptrs)
THUNK(pros_make_segments)
THUNK(pros_mark_stress)
THUNK(pros_mark_phrases)
THUNK(pros_store_phrase_len)
THUNK(pros_phrase_gen)
THUNK(pros_declination)
THUNK(pros_declination_step)
THUNK(ph_lengthen_count)
#undef THUNK

const fw_replacement fw_replacements[] = {
    { "cmd_apply_speaker",  0xA78C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_apply_speaker },
    { "cmd_run_due",        0xA880, FW_OUT_X | FW_OUT_Y, t_cmd_run_due },
    { "cmd_scan_clause",    0xACFA, 0,                   t_cmd_scan_clause },
    { "cmd_match_table",    0xADBF, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_match_table },
    { "lts_affix_next",     0xAF11, FW_OUT_A | FW_OUT_Y, t_lts_affix_next },
    { "lts_affix_prev",     0xAF1C, FW_OUT_A | FW_OUT_Y, t_lts_affix_prev },
    { "lts_sym_vowel",      0xB007, FW_OUT_A,            t_lts_sym_vowel },
    { "lts_match_symbol",   0xB05E, FW_OUT_A | FW_OUT_Y, t_lts_match_symbol },
    { "tn_say_char",        0xB0C3, FW_OUT_X,            t_tn_say_char },
    { "lts_match_list",     0xB0E3, FW_OUT_A | FW_OUT_Z, t_lts_match_list },
    { "tn_rule_skip_text",  0xB138, FW_OUT_A | FW_OUT_Y, t_tn_rule_skip_text },
    { "tn_rule_skip_field", 0xB142, FW_OUT_Y,            t_tn_rule_skip_field },
    { "tn_append_phonetic", 0xB154, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_tn_append_phonetic },
    { "lts_convert_char",   0xB172, 0,                   t_lts_convert_char },
    { "tn_convert_clause",  0xB1E6, 0,                   t_tn_convert_clause },
    { "par_setup_voice",    0xB3A4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_setup_voice },
    { "pros_microprosody",  0xB983, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_microprosody },
    { "par_play_start",     0xBAF2, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_play_start },
    { "par_reset_tracks",   0xBB35, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_reset_tracks },
    { "par_next_segment",   0xBCF3, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_next_segment },
    { "par_next_row",       0xBD07, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_next_row },
    { "par_next_param",     0xBD14, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_next_param },
    { "par_clamp",          0xBD24, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_clamp },
    { "par_read_segment",   0xBD33, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_read_segment },
    { "pros_scale_target",  0xBDD3, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_scale_target },
    { "par_boundary_pass",  0xBE6A, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_boundary_pass },
    { "par_boundary_jump",  0xBE94, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_boundary_jump },
    { "par_store_boundary", 0xBEB5, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_store_boundary },
    { "par_boundary_cur_dom", 0xBEE2, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_boundary_cur_dom },
    { "par_boundary_next_dom", 0xBF27, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_boundary_next_dom },
    { "par_read_dominance", 0xBF6C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_read_dominance },
    { "par_scale_voicing",  0xBFC3, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_scale_voicing },
    { "par_boundary_value", 0xC039, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_boundary_value },
    { "par_plan_tracks",    0xC0D2, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_plan_tracks },
    { "par_scale_duration", 0xC29C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_scale_duration },
    { "cmd_hex_value",      0xC520, FW_OUT_A | FW_OUT_X | FW_OUT_Y | FW_OUT_Z, t_cmd_hex_value },
    { "ph_append_phrase_pause", 0xC9B8, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ph_append_phrase_pause },
    { "ph_append_pause",    0xCA15, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ph_append_pause },
    { "pros_append_segment", 0xCA3B, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_append_segment },
    { "pros_reset_seg_ptrs", 0xCA71, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_reset_seg_ptrs },
    { "pros_make_segments", 0xCA92, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_make_segments },
    { "pros_mark_stress",   0xCABD, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_mark_stress },
    { "pros_mark_phrases",  0xCEF6, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_mark_phrases },
    { "pros_store_phrase_len", 0xD0FD, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_store_phrase_len },
    { "pros_phrase_gen",    0xD129, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_phrase_gen },
    { "pros_declination",   0xD3DD, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_declination },
    { "pros_declination_step", 0xD40D, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_declination_step },
    { "ph_lengthen_count",  0xD43E, FW_OUT_A | FW_OUT_Y, t_ph_lengthen_count },
};
const size_t fw_replacement_count = sizeof fw_replacements / sizeof fw_replacements[0];

const fw_replacement *fw_replacement_find(const char *name)
{
    for (size_t i = 0; i < fw_replacement_count; i++)
        if (!strcmp(fw_replacements[i].name, name))
            return &fw_replacements[i];
    return NULL;
}

bool fw_install(machine *m, fw_bridge *b, const char *only)
{
    if (only) {
        const fw_replacement *r = fw_replacement_find(only);
        return r && machine_hook(m, r->addr, r->thunk, b);
    }
    for (size_t i = 0; i < fw_replacement_count; i++)
        if (!machine_hook(m, fw_replacements[i].addr, fw_replacements[i].thunk, b))
            return false;
    return true;
}
