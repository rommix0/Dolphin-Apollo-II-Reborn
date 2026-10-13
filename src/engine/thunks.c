#include <string.h>
#include "engine/thunks.h"
#include "firmware/lts.h"
#include "firmware/tn_rules.h"
#include "firmware/tn_convert.h"
#include "firmware/cmd_match.h"
#include "firmware/cmd_scan.h"
#include "firmware/cmd_run.h"
#include "firmware/cmd_apply.h"
#include "firmware/cmd_play_event.h"
#include "firmware/fw_ops.h"
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
#include "firmware/par_track.h"
#include "firmware/par_play.h"
#include "firmware/frm_build.h"
#include "firmware/ser.h"
#include "firmware/cmd_imm.h"
#include "firmware/isr_serial.h"
#include "firmware/isr_dsp.h"
#include "firmware/data/fw_data.h"

#ifdef _MSC_VER
#pragma warning(disable : 4611)   /* setjmp with no C++ objects to destroy: the trap of fw faults */
#endif

#define P2_LOWER_WINDOW 0x80

static bool window_on(const machine *m) { return (m50734_port_pins(&m->sfr, 2) & P2_LOWER_WINDOW) != 0; }

/* The firmware's view of P2 read back from the machine's pins (M4.6b spec 3.4, M4.6c1 spec 3.1): the window and the
   selected bank, after anything the ROM or an interrupt handler may have written. */
static void sync_p2(fw_bridge *b)
{
    b->f.low_window = window_on(b->m);
    b->f.bank = m50734_port_pins(&b->m->sfr, 2) & P2_BANK_BITS;
}

/* A ROM call from C, hooked (machine_rom_call) or not (machine_rom_call_unhooked), within the bridge deadline; the
   window and the bank are read back after it. */
static fw_call_result rom_call(fw_bridge *b, uint16_t addr, fw_regs *r, uint64_t budget, bool unhooked)
{
    if (b->deadline) {   /* the outermost thunk's bound: at most the cycles left, none once it has passed */
        uint64_t now = b->m->cpu.cycles;
        if (now >= b->deadline)
            return FW_CALL_FAILED;
        if (budget > b->deadline - now)
            budget = b->deadline - now;
    }
    machine_regs mr = { r->a, r->x, r->y, b->m->cpu.p };   /* the routine starts with the machine's flags */
    machine_call_result res = unhooked ? machine_rom_call_unhooked(b->m, addr, &mr, budget)
                                       : machine_rom_call(b->m, addr, &mr, budget);
    r->a = mr.a;
    r->x = mr.x;
    r->y = mr.y;
    r->p = mr.p;
    sync_p2(b);
    if (res == MCALL_RETURNED)
        return FW_CALL_RETURNED;
    return res == MCALL_UNWOUND ? FW_CALL_UNWOUND : FW_CALL_FAILED;
}

static fw_call_result host_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    return rom_call(ctx, addr, r, budget, false);
}

/* The NUL-bug fallback's ROM cmd_run_next (M4.6c1 spec 3.4): its own hook does not fire on this entry. */
static fw_call_result host_rom_call_unhooked(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    return rom_call(ctx, addr, r, budget, true);
}

/* A ROM byte under another bank (M4.6c1 spec 3.4): the bus as the CPU sees it, $FF for an empty socket. */
static uint8_t host_read_bank(void *ctx, uint16_t addr)
{
    return machine_peek(((fw_bridge *)ctx)->m, addr);
}

/* A bank entry (M4.6c1 spec 3.4): the machine goes on at addr with the state bank_enter leaves at its JMP: SP $FF
   (LDX #$FF / TXS), the I flag set (SEI), A and X. run_frame's unwound branch leaves the PC there. */
static void host_enter(void *ctx, uint16_t addr, uint8_t a, uint8_t x)
{
    machine *m = ((fw_bridge *)ctx)->m;
    m->cpu.s = 0xFF;
    m->cpu.p |= CPU740_I;
    m->cpu.a = a;
    m->cpu.x = x;
    m->cpu.pc = addr;
}

/* SEB7/CLB7 $F0: a read-modify-write of P2 through the bus, as the CPU does it */
static void host_set_low_window(void *ctx, bool on)
{
    machine *m = ((fw_bridge *)ctx)->m;
    uint8_t v = m->bus.read(m->bus.ctx, M50734_P2);
    m->bus.write(m->bus.ctx, M50734_P2, (uint8_t)(on ? v | P2_LOWER_WINDOW : v & ~P2_LOWER_WINDOW));
}

/* The firmware's SFR read and write (M4.6a spec 3.1): through the bus, as the CPU's read and write, side effects
   included (a read of UART_RX $E5 clears receive full; an unmodelled SFR is logged as the CPU's access would be).
   The C reads and writes each SFR where and in the order the ROM does. */
static uint8_t host_read_sfr(void *ctx, uint8_t addr)
{
    machine *m = ((fw_bridge *)ctx)->m;
    return m->bus.read(m->bus.ctx, addr);
}

static void host_write_sfr(void *ctx, uint8_t addr, uint8_t value)
{
    machine *m = ((fw_bridge *)ctx)->m;
    m->bus.write(m->bus.ctx, addr, value);
}

/* The firmware's hardware wait (M4.6a spec 3.3, M4.6b spec 3.2), on the emulated CPU, within the bridge deadline:
   - FW_WAIT_UART_TX: ser_tx_wait $C500 (BBC1 UART_STATUS,$C500 / RTS) runs as host code through host_rom_call, with
     the machine's own A, X and Y, so with interrupts live and the ROM's cycles. $C500 is never hooked.
   - FW_WAIT_INTERRUPT: the CPU idles as at a WIT at the hook's PC (never the idle WIT $C602, so no serial byte is
     delivered) until an interrupt has been handled, its handler ROM or C (machine_wait_interrupt).
   - FW_WAIT_TIMER2, FW_WAIT_TIMER3: isr_timer_start's loops $B372 and $B37F run with I set, as inside its PHP/SEI
     (machine_run_loop).
   at is the C's report of the loop; the hybrid host runs the kind's own. The window and the bank are read back after
   the wait, as after a ROM call: an interrupt handler may have run. */
static bool host_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    fw_bridge *b = ctx;
    machine *m = b->m;
    (void)at;
    if (kind == FW_WAIT_UART_TX) {
        fw_regs r = { m->cpu.a, m->cpu.x, m->cpu.y, 0 };
        return rom_call(b, FW_WAIT_UART_TX_LOOP, &r, FW_ROM_CALL_BUDGET, false) == FW_CALL_RETURNED;
    }
    uint64_t budget = FW_ROM_CALL_BUDGET;
    if (b->deadline) {   /* the outermost thunk's bound, as host_rom_call's */
        uint64_t now = m->cpu.cycles;
        if (now >= b->deadline)
            return false;
        if (budget > b->deadline - now)
            budget = b->deadline - now;
    }
    bool ok;
    switch (kind) {
    case FW_WAIT_INTERRUPT:
        ok = machine_wait_interrupt(m, budget);
        break;
    case FW_WAIT_TIMER2:
        ok = machine_run_loop(m, FW_WAIT_TIMER2_LOOP, FW_WAIT_TIMER2_EXIT, budget);
        break;
    case FW_WAIT_TIMER3:
        ok = machine_run_loop(m, FW_WAIT_TIMER3_LOOP, FW_WAIT_TIMER3_EXIT, budget);
        break;
    default:
        return false;
    }
    sync_p2(b);
    return ok;
}

/* STA DSP_DATA (M4.6b spec 3.1): the byte written to $8000 through the bus, as the CPU's STA writes it, so the
   machine counts it (port_writes) and groups it into the raw frame stream as before. */
static void host_dsp_write(void *ctx, uint8_t byte)
{
    machine *m = ((fw_bridge *)ctx)->m;
    m->bus.write(m->bus.ctx, APOLLO_DSP_PORT, byte);
}

/* The frame event (M4.6b spec 3.1): the capture the machine makes when the ROM reaches $B306. The C timer interrupt
   never executes $B306, so no frame is captured twice. */
static void host_frame_done(void *ctx)
{
    machine_capture_frame(((fw_bridge *)ctx)->m);
}

bool fw_bridge_init(fw_bridge *b, machine *m)
{
    const fw_image *img = fw_image_get();
    b->m = m;
    b->thunk_calls = 0;
    b->unwinds = 0;
    b->deadline = 0;
    b->host.ctx = b;
    b->host.rom_call = host_rom_call;
    b->host.set_low_window = host_set_low_window;
    b->host.read_sfr = host_read_sfr;
    b->host.write_sfr = host_write_sfr;
    b->host.wait = host_wait;
    b->host.dsp_write = host_dsp_write;
    b->host.frame_done = host_frame_done;
    b->host.read_bank = host_read_bank;
    b->host.enter = host_enter;
    b->host.rom_call_unhooked = host_rom_call_unhooked;
    fw_init(&b->f, m->ram, img, &b->host);
    return img != NULL;
}

/* The register mapping of one routine: reads its input registers into locals before calling the C (a ROM call
   made from inside the C overwrites the machine's registers), then sets its output registers. */
typedef void (*thunk_regs)(fw *f, cpu740 *c);

/* The frame every thunk shares: set the fault trap (saving an outer thunk's, as a ROM call from C can reach
   another thunk on the same bridge), sync the window and the bank, run the routine; then restore the trap and
   either leave the machine where an unwound ROM call or a bank entry (fw_enter) left it, report the fault, or
   emulate the RTS (an interrupt handler's RTI when
   isr is set, M4.6a spec 3.2). The outermost thunk (no deadline active) also bounds the whole call: b->deadline =
   its entry cycles + FW_ROM_CALL_SPEECH_BUDGET, cleared however it ends, so that a C wait that never ends
   (par_play's, a whole utterance inside one hook call) faults (thunks.h). */
static bool run_frame(machine *m, fw_bridge *b, thunk_regs regs, bool isr)
{
    if (m != b->m) {   /* registers come from m, RAM and host calls from b->m: they must be one machine */
        m->fault = MFAULT_FIRMWARE;
        m->fault_addr = m->cpu.pc;
        return true;
    }
    b->thunk_calls++;
    uint16_t entry = m->cpu.pc;
    uint8_t entry_sp = m->cpu.s;
    bool outermost = b->deadline == 0;
    if (outermost)
        b->deadline = m->cpu.cycles + FW_ROM_CALL_SPEECH_BUDGET;
    jmp_buf trap, *saved = b->f.trap;
    b->f.trap = &trap;
    sync_p2(b);
    if (setjmp(trap) == 0)
        regs(&b->f, &m->cpu);
    b->f.trap = saved;
    if (outermost)
        b->deadline = 0;
    if (b->f.unwound) {   /* a ROM call reset the stack or the C entered a bank: the C is abandoned, the machine
                             goes on where the ROM went or at the bank entry */
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
    if (isr)
        machine_return_interrupt(m);
    else
        machine_return(m);
    return true;
}

static bool run_thunk(machine *m, fw_bridge *b, thunk_regs regs) { return run_frame(m, b, regs, false); }

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

/* boot_loop_play $A7D4: A, X, Y in; none is read (A is loaded first; X and Y flow into isr_par_hold, which keeps them,
   and par_play_start, which writes Y and keeps X). Out: A = the byte at FW_UTT_CONTENT ($6B) and X, Y untouched when
   it does not play ($A7DE RTS); else the A, X and Y isr_speech_stop leaves (its RTS at $B3A3). The C's ROM calls
   overwrite the machine's registers, so all three are set from the C. Nothing reads them after a call: boot_reset
   ($A687) jumps to boot_loop_text, whose boot_get_clause writes X and A first ($A697 LDX #1, $A699 LDA #$20);
   boot_selftest_sound ($A7BA) returns to $A657 (BBS0 boot_dip, then LDX #0 or JMP $A66B: LDM, CLI, the isr_par_* calls
   that keep the registers, and cmd_apply_speaker, whose par_setup_voice reads none); cmd_say_phonetic ($A87C,
   unreachable) returns; the C tn_convert_clause (and the ROM's $B207 JMP $B1E9) writes X first (LDX #0). No caller
   reads a flag. It waits on the timer interrupt (par_play), so diff_play checks it, not diff_lts. Declared: A, X, Y. */
static void boot_loop_play_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    boot_loop_play(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
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

/* isr_set_volume $ACE8: A in (the volume). A = the value written to P3 ($ACF7 STA P3, after AND P3 / ORA $75), X and
   Y untouched, out. boot_reset ($A642) calls boot_read_dip next, which loads A first ($ADE4 LDA P4) and reads no X
   or Y; boot_loop_play calls par_play after $B2C4, which writes A, X and Y before reading them (its thunk), and
   isr_speech_stop after $B2D2, which keeps all three to boot_loop_play's exit (its thunk); isr_volume_half ($B46F)
   returns them to its callers (its row); cmd_ev_set_volume ($BCEF) returns them to par_play's $B892 JMP par_play
   (LDX first). No caller reads a flag. The C boot_loop_play, isr_volume_half and cmd_ev_set_volume call the C, so
   in "all" mode the hook fires from boot_reset only. Declared: A, X, Y. */
static void isr_set_volume_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_set_volume(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
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

/* isr_timer_dsp $B2D9, an interrupt handler (isr): entered by the CPU through vec_int2_timers $FFF8, with the PC and P
   pushed and I set. A, X and Y in; every path leaves them as they came (the RTI at $B2DF touches none; the word path
   pushes A and Y at $B2E0-$B2E2 and pulls them at $B31F-$B321), and the RTI pulls P and the PC
   (machine_return_interrupt): the interrupted code goes on with its own registers. The C makes no ROM call (the DSP
   port and the frame event go through the host), so the machine's registers stay; they are set back from the entry
   values all the same. No outputs declared (M4.6a spec 3.2). */
static void isr_timer_dsp_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_timer_dsp(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* isr_speech_start $B323: no register input (X and A are written first: $B323 LDX #0, $B32B LDA). A = 0 ($B344 LDA
   #0) and X = 0 (isr_wait_frames' DEX) out, Y untouched (isr_dsp_reset, isr_timer_start and isr_wait_frames keep A
   and Y). Its one caller, boot_play_body ($B2B9), calls isr_volume_half next, which loads A first ($B46C LDA
   cmd_volume) and hands X and Y on to isr_wait_frame, isr_set_volume and par_play (their rows). No caller reads a
   flag. It waits on the timer interrupt (isr_wait_frames), so diff_play checks it, not diff_lts. The C
   boot_loop_play calls the C, so it fires only while the ROM boot_loop_play runs. Declared: A, X, Y. */
static void isr_speech_start_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_speech_start(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* isr_wait_frames $B356: X in (the count). X = 0 out, A and Y untouched. Its one caller, isr_speech_start ($B352),
   returns ($B355 RTS), so X and A (0, $B344) and Y are its exit registers (its row). No caller reads a flag. It waits
   on the timer interrupt, so diff_play checks it, not diff_lts. The C isr_speech_start calls the C, so it fires only
   while the ROM isr_speech_start runs. Declared: A, X, Y. */
static void isr_wait_frames_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_wait_frames(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* isr_timer_start $B35D: A, X and Y in and out untouched (LDM, DEC, SEB, CLB and the two BBC loops; PHP / PLP keep
   the flags). Its one caller, isr_speech_start ($B34D), loads X next ($B350 LDX #1) and returns A and Y as its exit
   registers (its row). No caller reads a flag. The C isr_speech_start calls the C, so it fires only while the ROM
   isr_speech_start runs. Declared: A, X, Y. */
static void isr_timer_start_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_timer_start(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* isr_dsp_reset $B38B: A, X and Y in and out untouched (CLB, SEB, CLB; PHP / PLP keep the flags). isr_speech_start
   ($B34A) calls isr_timer_start next, which keeps them (its row); isr_speech_stop ($B39E) clears boot_speaking bit 0
   and returns them as its exit registers (its row). No caller reads a flag. The C isr_speech_start and
   isr_speech_stop call the C, so it fires only while one of them runs as ROM. Declared: A, X, Y. */
static void isr_dsp_reset_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_dsp_reset(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* isr_speech_stop $B398: A, X and Y in and out untouched (LDM, LDM, isr_dsp_reset, CLB). Its one caller,
   boot_play_body ($B2D5), returns ($B2D8 RTS), so they are boot_loop_play's exit registers (its thunk), which no
   caller reads. No caller reads a flag. The C boot_loop_play calls the C, so it fires only while the ROM
   boot_loop_play runs. Declared: A, X, Y. */
static void isr_speech_stop_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_speech_stop(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_setup_voice $B3A4: A, X, Y in; none is read (Y reaches frm_build_rest_frame, whose first instruction is
   LDY #$14 at $B41D). A = $0B, X = 9 and Y = $0B out, as the frame builder leaves them: frm_build_rest_frame's loop ends
   at X = 9 ($B436 CPX #8), frm_clamp_mark_space ends with LDA #$0B and Y = $0A + 1 ($B930-$B954); the C calls the C
   frame builder (M4.5), and all three are set from the C. Its one ROM caller, cmd_apply_pitch ($A845),
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

/* frm_build_rest_frame $B41D: nothing in (LDY #$14 first; X and A are loaded before they are read). A =
   par_params_ptr's high byte, X = 9 and Y = $3B out (the RTS at $B444). Its one ROM caller, par_setup_voice ($B404),
   calls frm_copy_rest_frame next, which loads Y and A first ($B445 LDY #$13, $B447 LDA) and leaves X; so do the
   frm_clamp_mark_space and frm_copy_rest_frame calls after it ($B40A, $B416, $B419), so X reaches par_setup_voice's
   callers (its thunk). No caller reads a flag. The C par_setup_voice calls the C (M4.5), so the hook fires only
   while the ROM par_setup_voice runs. Declared: A, X, Y. */
static void frm_build_rest_frame_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_build_rest_frame(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* frm_copy_rest_frame $B445: X passes through (Y and A are loaded first). A = frm_rest_frame[0], X untouched and Y =
   $FF out. par_setup_voice ($B407, $B416) calls frm_clamp_mark_space next, which loads Y and A first ($B930 LDY
   #$0A, $B932 LDA) and leaves X, so X reaches par_setup_voice's callers (its thunk); par_play ($B8DA) calls
   frm_snapshot next, which loads Y, A and X first ($B917 LDY, $B919 LDA, $B922 LDX). No caller reads a flag.
   Declared: A, X, Y. */
static void frm_copy_rest_frame_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_copy_rest_frame(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* isr_wait_frame $B465: A, X and Y in and out untouched (BBC0, LDM). boot_play_body loads A next ($B2C2 LDA cmd_volume,
   $B2D0 LDA #0) and hands X and Y on to isr_set_volume and par_play or isr_speech_stop (their rows); isr_wait_frames
   ($B359 DEX) reads X; par_play ($B8D2) loads A next ($B8D5 LDA #3) and hands X and Y to frm_copy_rest_frame, which
   loads Y first and keeps X (its row). No caller reads a flag. It waits on the timer interrupt, so diff_play checks
   it, not diff_lts. The C boot_loop_play, isr_wait_frames and par_play call the C, so it fires only while one of
   them runs as ROM. Declared: A, X, Y. */
static void isr_wait_frame_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_wait_frame(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* isr_volume_half $B46C: no register input (LDA cmd_volume first). A = the value written to P3, X and Y untouched, out
   (isr_set_volume). Its callers, boot_play_body $B2BC and $B2CA, call isr_wait_frame next (BBC0 / LDM: none read),
   then load A ($B2C2 LDA cmd_volume, $B2D0 LDA #0) and hand X and Y to isr_set_volume, par_play and isr_speech_stop
   (their rows). No caller reads a flag. The C boot_loop_play calls the C, so it fires only while the ROM
   boot_loop_play runs. Declared: A, X, Y. */
static void isr_volume_half_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_volume_half(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_play $B877: A, X, Y in; none is read before it is written (X at $B877 LDX cmd_ev_next; A and Y reach only
   ROM calls and C routines that write them first: isr_par_ready and ser_tx_service keep them, par_step_tracks
   starts with LDX #$0A, cmd_run_event with LDA cmd_ev_next). Out: as cmd_ev_sound_end leaves them, kept by the last
   isr_par_ready ($B8F7) to the RTS at $B8FA. The C's ROM calls overwrite the machine's registers, so all three are
   set from the C. Its one caller, boot_play_body ($B2C7), calls isr_volume_half next, which loads A first ($B46C
   LDA cmd_volume) and reads no X or Y; no caller reads a flag. The hook fires on the JSR only: once it has run,
   par_play's own JMP par_play ($B892, $B8AE, $B914) never runs. It waits on the timer interrupt, so diff_play checks
   it, not diff_lts. Declared: A, X, Y. */
static void par_play_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_play(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* frm_snapshot $B917: nothing in (LDY #$0A first, then LDA; LDX #$0A at $B922). A = the producer frame's b10, X = 0
   and Y = 10 (frm_snapshot_map[0], the last entry read) out. par_play ($B8DD) and frm_finish_frame ($B9BA) call
   frm_clamp_mark_space next, which loads Y and A first and leaves X: in frm_finish_frame X is loaded again at $B9C5
   (LDX frm_source_index) after frm_raise_formants, which keeps it; in par_play it passes frm_transition_flags,
   the waits (isr_par_ready, PHA / PLA) and the next frm_copy_rest_frame, which keep it, or cmd_ev_sound_end and
   the last isr_par_ready, which return it as par_play's exit X (its thunk). No caller reads a flag. Declared: A,
   X, Y. */
static void frm_snapshot_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_snapshot(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* frm_clamp_mark_space $B930: X passes through (Y and A are loaded first). A = $0B, X untouched and Y = 11 out.
   par_setup_voice calls frm_copy_rest_frame after $B40A (LDY and LDA first, X kept) and returns after $B419, so A,
   X and Y reach its callers (its thunk); par_play ($B8E0) calls frm_transition_flags next, which loads A and Y first
   ($BA3F LDA #0, $BA44 LDY #9) and keeps X; frm_finish_frame ($B9BD) calls frm_raise_formants next, which loads Y and
   A first and keeps X, then loads X ($B9C5). No caller reads a flag. Declared: A, X, Y. */
static void frm_clamp_mark_space_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_clamp_mark_space(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* frm_raise_formants $B95D: X passes through (Y and A are loaded first; par_clamp keeps X). A = b10 - 32 and Y = 10
   when that has bit 7 set (the RTS at $B966), else A = the new F2 and Y = 5; X untouched. Its one caller,
   frm_finish_frame ($B9C0), loads Y, X and A next ($B9C3 LDY #$10, $B9C5 LDX, $B9C8 LDA): none is read. No caller
   reads a flag. Declared: A, X, Y. */
static void frm_raise_formants_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_raise_formants(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* frm_finish_frame $B9B5: A, X and Y in, the same out (PHA / TXA / PHA / TYA / PHA at $B9B5-$B9B9, PLA / TAY / PLA /
   TAX / PLA at $BA39-$BA3D; the pushes are dead stack at the RTS). par_play ($B8C3, $B90E) clears isr_frame_flag
   (LDM) and then writes X ($B877 LDX cmd_ev_next) or calls isr_par_ready (PHA / PLA), isr_wait_frame or
   par_step_tracks, none of which reads them, before it writes A, X and Y (par_step_tracks' thunk). No caller reads
   a flag. The C par_play calls the C (M4.5), so the hook fires only while the ROM par_play runs. Declared: A, X,
   Y. */
static void frm_finish_frame_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_finish_frame(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* frm_transition_flags $BA3F: X passes through (A and Y are loaded first). A = frm_flags, X untouched and Y = $0D
   out (the RTS at $BAEB). frm_finish_frame ($BA36) restores A, X and Y next (PLA / TAY / PLA / TAX / PLA); par_play
   ($B8E3) waits (isr_par_ready, PHA / PLA), then calls frm_copy_rest_frame (LDY and LDA first, X kept) or, after
   the last rest frame, cmd_ev_sound_end (A as it came in unless it restores; X and Y kept) and isr_par_ready, which
   return them as par_play's exit registers (its thunk). No caller reads a flag. Declared: A, X, Y. */
static void frm_transition_flags_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_transition_flags(f, &r);
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
   before par_play's LDY pros_seg_count ($B8B1). No caller reads a flag. It does not fire in "all" mode: the C
   boot_loop_play calls the C (M4.4b), so it fires only while the ROM boot_loop_play runs. Declared: A, X, Y. */
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

/* cmd_run_event $BB74: Y in (handed to the handler, which does not read it); A and X are written first. A, X and Y
   out as the handler leaves them (the C handler, or the ROM one through rom_call). Its one ROM caller, par_play
   ($B88F), goes on at $B892 JMP par_play: LDX cmd_ev_next ($B877), then LDA par_seg_index ($B87F) or, past the
   event test, isr_par_ready (PHA / PLA) and LDY pros_seg_count ($B8B1), par_next_segment or par_step_tracks (each
   loads A, X and Y before reading them), or isr_wait_frame and LDA #3 ($B8D5). No caller reads a flag (JMP, LDX
   follow). From the ROM par_play only. Declared: A, X, Y. */
static void cmd_run_event_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    cmd_run_event(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* cmd_ev_index $BBA0: no register input; A, X and Y untouched (TST / DEC). Entered by cmd_run_event's JMP
   (cmd_ev_vector) ($BB9D); its RTS returns to par_play $B892 (JMP par_play: LDX, as cmd_run_event's row). No caller
   reads a flag. Declared: A, X, Y. */
static void cmd_ev_index_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    cmd_ev_index(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* cmd_ev_sound_start $BBA7: A in (the effect; X and Y are written before they are read: TAX at $BBAA, LDY #0 at
   $BC36). A = 0, X = cmd_ev_next << 1 and Y = the end's index out. Entered by cmd_run_event's JMP (cmd_ev_vector)
   ($BB9D); its RTS returns to par_play $B892 (JMP par_play: LDX, as cmd_run_event's row). No caller reads a flag.
   Declared: A, X, Y. */
static void cmd_ev_sound_start_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    cmd_ev_sound_start(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* cmd_ev_sound_end $BC80: no register input. A = cmd_sound_state[6] when it restores, else as it came in; X and Y
   untouched. Entered by cmd_run_event's JMP (cmd_ev_vector) ($BB9D), returning to par_play $B892 (JMP par_play:
   LDX, as cmd_run_event's row), and called by par_play $B8F4, which calls isr_par_ready next (PHA / PLA, A, X and Y
   preserved) and returns ($B8FA RTS) to boot_play_body $B2CA, JSR isr_volume_half (LDA cmd_volume first). No caller
   reads a flag. From the ROM par_play only. Declared: A, X, Y. */
static void cmd_ev_sound_end_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    cmd_ev_sound_end(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* cmd_ev_set_voicing $BCEA: A in; A, X and Y untouched (STA cmd_voicing). Entered by cmd_run_event's JMP
   (cmd_ev_vector) ($BB9D); its RTS returns to par_play $B892 (JMP par_play: LDX, as cmd_run_event's row). No caller
   reads a flag. Declared: A, X, Y. */
static void cmd_ev_set_voicing_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    cmd_ev_set_voicing(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* cmd_ev_set_volume $BCED: A in (X and Y reach isr_set_volume, which does not read them). A, X and Y out as
   isr_set_volume leaves them (A = the new P3 value, X and Y untouched). Entered by cmd_run_event's JMP
   (cmd_ev_vector) ($BB9D); its RTS returns to par_play $B892 (JMP par_play: LDX, as cmd_run_event's row). No caller
   reads a flag. Declared: A, X, Y. */
static void cmd_ev_set_volume_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    cmd_ev_set_volume(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_next_segment $BCF3: A, X, Y in; none is read (A is loaded first, X is set to 0 by par_read_segment, Y is
   loaded). A, X and Y out as par_plan_tracks leaves them (A = its last result or 1, X = what the boundary pass
   left, Y = 0). Its one ROM caller, par_play ($B900), loads A next ($B903 LDA par_frames_left); then either
   $B8B1 LDY, or isr_par_ready (which preserves A, X and Y) and par_step_tracks, which loads X and Y before reading
   them. No caller reads a flag. It does not fire in "all" mode: the C par_play calls the C (M4.4b), so it fires only while the ROM par_play runs.
   Declared: A, X, Y. */
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
   it first). No caller reads a flag (INX, CPX, DEC follow). Every caller is C since M4.5 (frm_build_rest_frame), so
   the hook fires only while a caller runs as ROM (the "alone" modes). Declared: A, X, Y. */
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
   caller reads a flag (STA, CPX or JMP follow). Every caller is C since M4.5 (frm_raise_formants), so the hook
   fires only while a caller runs as ROM (the "alone" modes). Declared: A, X, Y. */
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

/* par_step_tracks $C0FF: nothing in (LDX #$0A first; A and Y are loaded before they are read). A = pros_play_pitch
   >> 2, X = 0, Y = 10 out. Its ROM caller par_play ($B8C0, $B90B) calls frm_finish_frame next, which pushes A, X and
   Y and restores them ($B9B5-$B9B9, $BA39-$BA3D; its frm_snapshot loads Y and X first, $B917, $B922); then par_play
   writes X ($B877 LDX cmd_ev_next) or calls isr_par_ready (PHA / PLA), isr_wait_frame or par_step_tracks, none of
   which reads them, before it writes A, X and Y. No caller reads a flag. Declared: A, X, Y. */
static void par_step_tracks_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_step_tracks(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_track_sequential $C14C: X (the parameter) in. A = the frame byte's value, X untouched, Y = its index
   (par_slot_map) out. par_step_tracks reads X ($C10F DEX; BNE) and loads A ($C101, $C118) and Y ($C112) before
   reading them. No caller reads a flag. Declared: A, X, Y. */
static void par_track_sequential_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_track_sequential(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* util_interp_step $C1A1: A (the target) and Y (the steps) in, util_interp_cur / _steps / _target in RAM. A = the
   value, X untouched, Y = 0, Y - 2 or the quotient out. The callers store A at once: par_track_sequential ($C166
   STA $16FC,X; $C198 STA $17BF,X), par_track_overlap ($C213 STA $170C,X), par_track_start ($C268 STA $16FC,X) and
   par_track_end ($C291 STA $175C,X) with X; par_step_tracks ($C127 STA pros_play_pitch, LSR, LSR) reads A only. Y is
   loaded before it is read on every path ($C169/$C19B/$C216 LDY $B796,X, $C12C LDY #$0A; after par_track_start and
   par_track_end's RTS, par_track_overlap's $C1F5 STA and par_track_end's $C26C LDY, or $C1FB STA and $C1FE LDY).
   No caller reads a flag. Declared: A, X, Y. */
static void util_interp_step_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    util_interp_step(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_track_overlap $C1F2: X (the parameter) in. A = the median written, X untouched, Y = the frame byte's index out.
   par_step_tracks reads X ($C109 JMP $C10F: DEX; BNE) and loads A and Y before reading them. No caller reads a flag.
   Declared: A, X, Y. */
static void par_track_overlap_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_track_overlap(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_track_start $C24A: X in. A = the start track's value, X untouched, Y = 0 or util_interp_step's out.
   par_track_overlap reads A ($C1F5 STA par_start_track) and X (par_track_end's $C26C LDY $176C,X), which loads Y
   first. No caller reads a flag. Declared: A, X, Y. */
static void par_track_start_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_track_start(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_track_end $C26C: X in. A = the end track's value, X untouched, Y = the wait count, or util_interp_step's out.
   par_track_overlap reads A ($C1FB STA par_end_track) and X ($C204 LDA $170C,X), and loads Y first ($C1FE LDY
   par_frames_left). No caller reads a flag. Declared: A, X, Y. */
static void par_track_end_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_track_end(f, &r);
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

/* The registers in, a serial routine (ser.c, cmd_imm.c, isr_serial.c), the registers out: the routines that take and
   leave A, X and Y. */
static void ser_regs(fw *f, cpu740 *c, void (*fn)(fw *, fw_regs *))
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    fn(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* isr_int1 $C2B4, an interrupt handler (isr): entered by the CPU through vec_int1_serial $FFFA, with the PC and P
   pushed and I set. A, X and Y in; every path leaves them as they came (the serial paths push and pull them,
   $C33C-$C340 / $C376-$C37A; the parallel data path $C2F3-$C2F7 / $C336-$C33A; the early parallel exits, RTI at
   $C2BF, $C2D5 and $C2E8, touch none), and the RTI pulls P and the PC (machine_return_interrupt): the interrupted
   code goes on with its own registers. The C makes no ROM call, so the machine's registers stay; they are set back
   from the entry values all the same. No outputs declared (M4.6a spec 3.2). */
static void isr_int1_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_int1(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* isr_int4_host_line $C3E0, an interrupt handler (isr): entered through vec_int4_timerx $FFF4. A, X and Y in and
   out as they came (isr_host_line_high and _low keep them), the RTI at $C3E6 / $C3EA (machine_return_interrupt).
   No outputs declared. */
static void isr_int4_host_line_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_int4_host_line(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* isr_host_line_high $C3EB and isr_host_line_low $C3F4: A, X and Y in and out as they came (LDM, SEB/CLB, LDM).
   isr_int4_host_line returns from the interrupt next ($C3E6 / $C3EA RTI), so the interrupted code reads all three;
   boot_init_host_lines returns ($AC77 / $AC7D RTS) to boot_reset, which goes on at boot_reentry_warm ($A662 JMP
   $A66B): isr_par_hold and isr_par_ready push and pull A ($C5DE, $C5D6), then cmd_apply_speaker ($A675) takes X and Y
   (its thunk). No caller reads a flag. Declared: A, X, Y. */
static void isr_host_line_high_regs(fw *f, cpu740 *c) { ser_regs(f, c, isr_host_line_high); }
static void isr_host_line_low_regs(fw *f, cpu740 *c) { ser_regs(f, c, isr_host_line_low); }

/* ser_rx_byte $C37E: A in (the byte); X and Y passed to cmd_imm_match or ser_rx_put. A, X and Y out as the path
   taken leaves them (the mute: A = ser_rx_write's high byte; the command character: Y = 0; else as cmd_imm_match or
   ser_rx_put leaves them; X kept on every path). Its one caller, isr_int1 ($C32E, $C356, $C36F), pulls Y, X and A
   next ($C336-$C33A after SEI / SEB0 / PLP / NOP; $C35B-$C37A after PLP / NOP and the UART and port writes) and reads
   no flag. Declared: A, X, Y. */
static void ser_rx_byte_regs(fw *f, cpu740 *c) { ser_regs(f, c, ser_rx_byte); }

/* ser_rx_put $C3CC: A in. A and X out as they came, Y = 0 or the new page ($C3DC STY). ser_rx_byte returns; its
   caller pulls the registers. ser_rx_flush_cmd reads X next ($C40C INX) and loads A first ($C404 LDA $2020,X), and
   leaves Y to its own exit (its thunk); boot_queue_string reloads Y first ($C625 LDY $78) and loads A ($C61C). No
   caller reads a flag. Declared: A, X, Y. */
static void ser_rx_put_regs(fw *f, cpu740 *c) { ser_regs(f, c, ser_rx_put); }

/* ser_rx_flush_cmd $C3FA: A and X in and out as they came (pushed at $C3FA-$C3FC, pulled at $C40F-$C411); Y as its
   last ser_rx_put leaves it. cmd_imm_match ($C449) and cmd_imm_index_add ($C492) go on at cmd_imm_done, which pulls
   A after restoring zero page and keeps X and Y to cmd_imm_match's exit (its thunk). No caller reads a flag.
   Declared: A, X, Y. */
static void ser_rx_flush_cmd_regs(fw *f, cpu740 *c) { ser_regs(f, c, ser_rx_flush_cmd); }

/* cmd_imm_match $C413: A in (the byte); X passed through; Y loaded first. A out as it came ($C481 PLA), X kept
   (cmd_match_table, ser_rx_flush_cmd and both handlers keep it), Y = the matched entry's offset (a partial match,
   $C450), as ser_rx_flush_cmd leaves it (no match, @I+) or 0 (@I?, $C466). Its one caller, ser_rx_byte, returns
   ($C3AB RTS) to isr_int1, which pulls the registers. No caller reads a flag. Declared: A, X, Y. */
static void cmd_imm_match_regs(fw *f, cpu740 *c) { ser_regs(f, c, cmd_imm_match); }

/* ser_tx_get $C4B5: nothing read in (LDA #$15 first; Y is loaded before it is read). A = the byte or NAK, Y = the new
   count (0 when it was 0) or as it came (nothing pending), X untouched. isr_int1 ($C30D STA P1) and ser_tx_service
   ($C58D JSR ser_tx_send, which sends A) read A; isr_int1 then pulls Y, X and A ($C336-$C33A), and ser_tx_service's
   Y reaches its own callers (its thunk). The PLP restores the flags it came with: no caller reads one. Declared: A,
   X, Y. */
static void ser_tx_get_regs(fw *f, cpu740 *c) { ser_regs(f, c, ser_tx_get); }

/* ser_tx_put $C4DD: A in (read back from the stack, $C4E3). A, X and Y out as they came (pulled at $C4FA-$C4FE).
   ser_tx_records reads Y and X next ($C5C1 INY, $C5C2 DEX); ser_tx_hex pulls A ($C57B, $C57F) and keeps X and Y to
   its exit; cmd_imm_index_query loads A next ($C49A LDA, $C4AE LDA) and leaves X and Y to cmd_imm_done, which keeps
   them; cmd_reply_setting ($C643; C since M4.2, which calls the C) reads Y and X next ($C646 LDA $9990,Y, $C649
   STX); cmd_echo ($AB99; C since M4.2) goes on at cmd_run_done ($A8BD PLA / TAX / RTS to cmd_run_due, which reloads
   Y at $A880) and reads none. No caller reads a flag. Declared: A, X, Y. */
static void ser_tx_put_regs(fw *f, cpu740 *c) { ser_regs(f, c, ser_tx_put); }

/* ser_tx_send $C504: A in. A, X and Y out as they came (PHP / PLP keep the flags). Its one caller, ser_tx_service
   ($C590), loads A next (LDA ser_tx_pending) and passes X and Y to its own exit (its thunk). Declared: A, X, Y. */
static void ser_tx_send_regs(fw *f, cpu740 *c) { ser_regs(f, c, ser_tx_send); }

/* ser_hex_digits $C53A: in, X and Y and its caller's last two pushes, the bytes at S+3 and S+4 at the hook (LDA
   $0105,X / $0106,X after its two pushes and TSX: absolute,X, so the addresses are $0105 + (S - 2) and the next);
   they become the digits in place. A = X, X and Y out (PLA / TAY / PLA / TAX). Its one caller, ser_tx_hex, pulls
   the digits and A ($C577-$C57F) and passes X and Y through ser_tx_put to its own exit (its thunk). No caller reads
   a flag. Declared: A, X, Y. */
static void ser_hex_digits_regs(fw *f, cpu740 *c)
{
    uint16_t at = (uint16_t)(0x0105 + (uint8_t)(c->s - 2));
    uint8_t pair[2] = { fw_rd8(f, at), fw_rd8(f, (uint16_t)(at + 1)) };
    fw_regs r = { c->a, c->x, c->y, 0 };
    ser_hex_digits(f, &r, pair);
    fw_wr8(f, at, pair[0]);
    fw_wr8(f, (uint16_t)(at + 1), pair[1]);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* ser_hex_digit $C55A: A in. A = the digit, Y = A & $0F, X untouched. Its one caller, ser_hex_digits, stores A with
   X ($C545 STA $0105,X, $C552 STA $0106,X) and restores Y at its end. No caller reads a flag. Declared: A, X, Y. */
static void ser_hex_digit_regs(fw *f, cpu740 *c) { ser_regs(f, c, ser_hex_digit); }

/* ser_tx_hex $C571: A in. A, X and Y out as they came (its first push pulled last; ser_hex_digits and ser_tx_put
   keep X and Y). cmd_imm_index_query loads A next ($C49F LDA #'T'); ser_tx_records loads A ($C5B3) or Y ($C5B8 LDY
   #0) and restores all three at its end; cmd_reply_setting ($C650; C since M4.2) reloads A ($C653 LDA $75) and
   returns to cmd_query, which goes on at cmd_run_done ($A8BD PLA / TAX / RTS): none read. No caller reads a flag.
   Declared: A, X, Y. */
static void ser_tx_hex_regs(fw *f, cpu740 *c) { ser_regs(f, c, ser_tx_hex); }

/* ser_tx_service $C581: A, X, Y in (A pushed and pulled, X and Y passed to ser_tx_get and ser_tx_send). A and X out
   as they came, Y as ser_tx_get leaves it when it ran. Its ROM caller boot_get_clause ($A6D7) goes back to $A6C2
   (LDA ser_rx_write) and reads X, the clause length, at $A706 / $A70D (STA $1B00,X); Y is loaded first ($A6EB LDY
   #0). The C par_play calls the C. No caller reads a flag. Declared: A, X, Y. */
static void ser_tx_service_regs(fw *f, cpu740 *c) { ser_regs(f, c, ser_tx_service); }

/* ser_tx_records $C5A4: A, X and Y in and out (pushed at $C5A4-$C5A8, pulled at $C5D0-$C5D4). Its one caller,
   cmd_list_languages ($ABCD; C since M4.2, which calls the C), goes on at cmd_run_done ($A8BD PLA / TAX / RTS to
   cmd_run_due, which reloads Y at $A880): none read. Declared: A, X, Y. */
static void ser_tx_records_regs(fw *f, cpu740 *c) { ser_regs(f, c, ser_tx_records); }

/* isr_par_ready $C5D6: A, X and Y in and out as they came (PHA at $C5D6, PLA at $C5EA; X and Y untouched).
   boot_get_clause reads A and X after $A6FC ($A700 CMP #$0D, $A706 / $A70D STA $1B00,X) and hands all three to
   ser_tx_service after $A6D0 (its thunk); boot_reset ($A672) calls cmd_apply_speaker, which takes X and Y (its thunk);
   boot_queue_string ($C62B) pulls Y and A next ($C62E-$C630). No caller reads a flag (CLI, BBC0, JSR or PLA follow).
   The C callers (tn_convert_clause, cmd_scan_clause, par_play, the prosody passes) call the C, so in "all" mode the
   hook fires from boot_reset, boot_get_clause and boot_queue_string. Declared: A, X, Y. */
static void isr_par_ready_regs(fw *f, cpu740 *c) { ser_regs(f, c, isr_par_ready); }

/* isr_par_hold $C5DE: A, X and Y in and out as they came (PHA at $C5DE, PLA at $C5EA; X and Y untouched). boot_reset
   ($A66F) calls isr_par_ready (which keeps them) and cmd_apply_speaker, which takes X and Y (its thunk);
   boot_get_clause ($A6E4) reads X ($A706 / $A70D STA $1B00,X) after loading Y and A ($A6EB LDY #0, $A6ED LDA);
   boot_loop_play ($B2B3) calls par_play_start, which reads none (its thunk); boot_queue_string ($C617) reads the Y it
   set before ($C615 LDY #0; $C61C LDA ($46),Y). No caller reads a flag. The C boot_loop_play calls the C, so in
   "all" mode the hook fires from boot_reset, boot_get_clause and boot_queue_string. Declared: A, X, Y. */
static void isr_par_hold_regs(fw *f, cpu740 *c) { ser_regs(f, c, isr_par_hold); }

/* ser_rx_next_page $C5EC: Y in; Y = the next page out, A and X untouched. ser_rx_put ($C3DC) stores Y and returns;
   boot_get_clause ($A6F8) stores Y, then reads A (the byte it loaded, $A700 CMP #$0D) and X ($A706 / $A70D STA
   $1B00,X). No caller reads a flag. Declared: A, X, Y. */
static void ser_rx_next_page_regs(fw *f, cpu740 *c) { ser_regs(f, c, ser_rx_next_page); }

/* ser_tx_next_page $C5F5: Y in; Y = the next page out, A and X untouched. ser_tx_get ($C4C9) and ser_tx_put ($C4F3)
   store Y (STY); then ser_tx_get loads Y ($C4CB LDY ser_tx_count) and ser_tx_put pulls Y, X and A. No caller reads a
   flag. Declared: A, X, Y. */
static void ser_tx_next_page_regs(fw *f, cpu740 *c) { ser_regs(f, c, ser_tx_next_page); }

/* util_shl4 $C656: A in; A << 4 out (8 bits); X and Y untouched. Callers read A: cmd_apply_pitch ($A838 ORA
   cmd_prosody, $A842 STA pros_pitch_centre), cmd_set_formant ($A955 STA $75), cmd_set_mark_space ($A9B0 STA $75),
   cmd_hex_byte ($B458 STA $1809); the first two of these read X next ($A957, $A9B2 INX), which util_shl4 leaves. No
   caller reads a flag (ORA, STA follow; the carry is not read). All four callers are C and inline it: in "all"
   mode it never fires. Declared: A, X, Y. */
static void util_shl4_regs(fw *f, cpu740 *c)
{
    (void)f;
    c->a = util_shl4(c->a);
}

/* util_shl3 $C657: A in; A << 3 out (8 bits); X and Y untouched. Callers read A: isr_set_volume ($ACEF STA $75),
   cmd_ev_sound_start ($BBAA TAX); the ROM util_shl4 falls into it ($C656 ASL), so its callers' reads apply too (A,
   above). No caller reads a flag (STA, TAX follow). Both callers are C (cmd_ev_sound_start since M4.4b,
   isr_set_volume since M4.6b) and inline it: in "all" mode it never fires. Declared: A, X, Y. */
static void util_shl3_regs(fw *f, cpu740 *c)
{
    (void)f;
    c->a = util_shl3(c->a);
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
THUNK(boot_loop_play)
THUNK(cmd_run_due)
THUNK(isr_set_volume)
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
THUNK(isr_speech_start)
THUNK(isr_wait_frames)
THUNK(isr_timer_start)
THUNK(isr_dsp_reset)
THUNK(isr_speech_stop)
THUNK(par_setup_voice)
THUNK(frm_build_rest_frame)
THUNK(frm_copy_rest_frame)
THUNK(isr_wait_frame)
THUNK(isr_volume_half)
THUNK(par_play)
THUNK(frm_snapshot)
THUNK(frm_clamp_mark_space)
THUNK(frm_raise_formants)
THUNK(pros_microprosody)
THUNK(frm_finish_frame)
THUNK(frm_transition_flags)
THUNK(par_play_start)
THUNK(par_reset_tracks)
THUNK(cmd_run_event)
THUNK(cmd_ev_index)
THUNK(cmd_ev_sound_start)
THUNK(cmd_ev_sound_end)
THUNK(cmd_ev_set_voicing)
THUNK(cmd_ev_set_volume)
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
THUNK(par_step_tracks)
THUNK(par_track_sequential)
THUNK(util_interp_step)
THUNK(par_track_overlap)
THUNK(par_track_start)
THUNK(par_track_end)
THUNK(par_scale_duration)
THUNK(ser_rx_byte)
THUNK(ser_rx_put)
THUNK(isr_host_line_high)
THUNK(isr_host_line_low)
THUNK(ser_rx_flush_cmd)
THUNK(cmd_imm_match)
THUNK(ser_tx_get)
THUNK(ser_tx_put)
THUNK(ser_tx_send)
THUNK(cmd_hex_value)
THUNK(ser_hex_digits)
THUNK(ser_hex_digit)
THUNK(ser_tx_hex)
THUNK(ser_tx_service)
THUNK(ser_tx_records)
THUNK(isr_par_ready)
THUNK(isr_par_hold)
THUNK(ser_rx_next_page)
THUNK(ser_tx_next_page)
THUNK(util_shl4)
THUNK(util_shl3)
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
/* The hook of an interrupt handler: its frame exits like RTI. */
#define ISR_THUNK(name) \
    static bool t_##name(machine *m, void *user) { return run_frame(m, user, name##_regs, true); }
ISR_THUNK(isr_timer_dsp)
ISR_THUNK(isr_int1)
ISR_THUNK(isr_int4_host_line)
#undef ISR_THUNK

const fw_replacement fw_replacements[] = {
    { "cmd_apply_speaker",  0xA78C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_apply_speaker },
    { "boot_loop_play",     0xA7D4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_loop_play, true },
    { "cmd_run_due",        0xA880, FW_OUT_X | FW_OUT_Y, t_cmd_run_due },
    { "isr_set_volume",     0xACE8, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_set_volume },
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
    { "isr_timer_dsp",      0xB2D9, 0,                   t_isr_timer_dsp, false, true },
    { "isr_speech_start",   0xB323, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_speech_start, true },
    { "isr_wait_frames",    0xB356, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_wait_frames, true },
    { "isr_timer_start",    0xB35D, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_timer_start },
    { "isr_dsp_reset",      0xB38B, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_dsp_reset },
    { "isr_speech_stop",    0xB398, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_speech_stop },
    { "par_setup_voice",    0xB3A4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_setup_voice },
    { "frm_build_rest_frame", 0xB41D, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_build_rest_frame },
    { "frm_copy_rest_frame", 0xB445, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_copy_rest_frame },
    { "isr_wait_frame",     0xB465, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_wait_frame, true },
    { "isr_volume_half",    0xB46C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_volume_half },
    { "par_play",           0xB877, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_play, true },
    { "frm_snapshot",       0xB917, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_snapshot },
    { "frm_clamp_mark_space", 0xB930, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_clamp_mark_space },
    { "frm_raise_formants", 0xB95D, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_raise_formants },
    { "pros_microprosody",  0xB983, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_microprosody },
    { "frm_finish_frame",   0xB9B5, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_finish_frame },
    { "frm_transition_flags", 0xBA3F, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_transition_flags },
    { "par_play_start",     0xBAF2, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_play_start },
    { "par_reset_tracks",   0xBB35, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_reset_tracks },
    { "cmd_run_event",      0xBB74, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_run_event },
    { "cmd_ev_index",       0xBBA0, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_ev_index },
    { "cmd_ev_sound_start", 0xBBA7, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_ev_sound_start },
    { "cmd_ev_sound_end",   0xBC80, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_ev_sound_end },
    { "cmd_ev_set_voicing", 0xBCEA, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_ev_set_voicing },
    { "cmd_ev_set_volume",  0xBCED, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_ev_set_volume },
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
    { "par_step_tracks",    0xC0FF, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_step_tracks },
    { "par_track_sequential", 0xC14C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_track_sequential },
    { "util_interp_step",   0xC1A1, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_util_interp_step },
    { "par_track_overlap",  0xC1F2, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_track_overlap },
    { "par_track_start",    0xC24A, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_track_start },
    { "par_track_end",      0xC26C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_track_end },
    { "par_scale_duration", 0xC29C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_scale_duration },
    { "isr_int1",           0xC2B4, 0,                   t_isr_int1, false, true },
    { "ser_rx_byte",        0xC37E, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_rx_byte },
    { "ser_rx_put",         0xC3CC, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_rx_put },
    { "isr_int4_host_line", 0xC3E0, 0,                   t_isr_int4_host_line, false, true },
    { "isr_host_line_high", 0xC3EB, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_host_line_high },
    { "isr_host_line_low",  0xC3F4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_host_line_low },
    { "ser_rx_flush_cmd",   0xC3FA, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_rx_flush_cmd },
    { "cmd_imm_match",      0xC413, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_imm_match },
    { "ser_tx_get",         0xC4B5, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_tx_get },
    { "ser_tx_put",         0xC4DD, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_tx_put },
    { "ser_tx_send",        0xC504, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_tx_send },
    { "cmd_hex_value",      0xC520, FW_OUT_A | FW_OUT_X | FW_OUT_Y | FW_OUT_Z, t_cmd_hex_value },
    { "ser_hex_digits",     0xC53A, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_hex_digits },
    { "ser_hex_digit",      0xC55A, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_hex_digit },
    { "ser_tx_hex",         0xC571, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_tx_hex },
    { "ser_tx_service",     0xC581, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_tx_service },
    { "ser_tx_records",     0xC5A4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_tx_records },
    { "isr_par_ready",      0xC5D6, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_par_ready },
    { "isr_par_hold",       0xC5DE, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_par_hold },
    { "ser_rx_next_page",   0xC5EC, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_rx_next_page },
    { "ser_tx_next_page",   0xC5F5, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_tx_next_page },
    { "util_shl4",          0xC656, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_util_shl4 },
    { "util_shl3",          0xC657, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_util_shl3 },
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

bool fw_hook(machine *m, const fw_replacement *r, machine_hook_fn fn, void *user)
{
    return r->ramcode ? machine_hook_ramcode(m, r->addr, fn, user) : machine_hook(m, r->addr, fn, user);
}

bool fw_install(machine *m, fw_bridge *b, const char *only)
{
    if (only) {
        const fw_replacement *r = fw_replacement_find(only);
        return r && fw_hook(m, r, r->thunk, b);
    }
    for (size_t i = 0; i < fw_replacement_count; i++)
        if (!fw_hook(m, &fw_replacements[i], fw_replacements[i].thunk, b))
            return false;
    return true;
}
