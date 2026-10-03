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

bool fw_bridge_init(fw_bridge *b, machine *m)
{
    const fw_image *img = fw_image_get();
    b->m = m;
    b->thunk_calls = 0;
    b->unwinds = 0;
    b->host.ctx = b;
    b->host.rom_call = host_rom_call;
    b->host.set_low_window = host_set_low_window;
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
THUNK(cmd_hex_value)
THUNK(ph_append_phrase_pause)
THUNK(ph_append_pause)
THUNK(pros_append_segment)
THUNK(pros_reset_seg_ptrs)
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
    { "cmd_hex_value",      0xC520, FW_OUT_A | FW_OUT_X | FW_OUT_Y | FW_OUT_Z, t_cmd_hex_value },
    { "ph_append_phrase_pause", 0xC9B8, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ph_append_phrase_pause },
    { "ph_append_pause",    0xCA15, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ph_append_pause },
    { "pros_append_segment", 0xCA3B, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_append_segment },
    { "pros_reset_seg_ptrs", 0xCA71, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_reset_seg_ptrs },
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
