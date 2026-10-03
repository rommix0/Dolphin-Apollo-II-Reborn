#include <string.h>
#include "engine/thunks.h"
#include "firmware/lts.h"
#include "firmware/tn_rules.h"
#include "firmware/data/fw_data.h"

#ifdef _MSC_VER
#pragma warning(disable : 4611)   /* setjmp with no C++ objects to destroy: the trap of fw faults */
#endif

#define P2_LOWER_WINDOW 0x80

static bool window_on(const machine *m) { return (m50734_port_pins(&m->sfr, 2) & P2_LOWER_WINDOW) != 0; }

static bool host_rom_call(void *ctx, uint16_t addr, fw_regs *r)
{
    fw_bridge *b = ctx;
    machine_regs mr = { r->a, r->x, r->y, b->m->cpu.p };   /* the routine starts with the machine's flags */
    bool ok = machine_rom_call(b->m, addr, &mr, FW_ROM_CALL_BUDGET);
    r->a = mr.a;
    r->x = mr.x;
    r->y = mr.y;
    r->p = mr.p;
    b->f.low_window = window_on(b->m);
    return ok;
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
   another thunk on the same bridge), sync the window, run the routine; then restore the trap and either report
   the fault or emulate the RTS. */
static bool run_thunk(machine *m, fw_bridge *b, thunk_regs regs)
{
    if (m != b->m) {   /* registers come from m, RAM and host calls from b->m: they must be one machine */
        m->fault = MFAULT_FIRMWARE;
        m->fault_addr = m->cpu.pc;
        return true;
    }
    b->thunk_calls++;
    uint16_t entry = m->cpu.pc;
    jmp_buf trap, *saved = b->f.trap;
    b->f.trap = &trap;
    b->f.low_window = window_on(m);
    if (setjmp(trap) == 0)
        regs(&b->f, &m->cpu);
    b->f.trap = saved;
    if (b->f.fault) {
        if (m->fault == MFAULT_NONE) {
            m->fault = MFAULT_FIRMWARE;
            m->fault_addr = b->f.fault_addr;
        }
        m->cpu.pc = entry;   /* a failed ROM call leaves the PC inside the called routine */
        return true;
    }
    machine_return(m);
    return true;
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

/* lts_convert_char $B172: A in; no register output (tn_convert_clause clears P2.7 and reloads X) */
static void lts_convert_char_regs(fw *f, cpu740 *c)
{
    uint8_t ch = c->a;
    lts_convert_char(f, ch);
}

/* The hook of each routine: its register mapping run in the shared thunk frame, user = the bound fw_bridge. */
#define THUNK(name) \
    static bool t_##name(machine *m, void *user) { return run_thunk(m, user, name##_regs); }
THUNK(lts_affix_next)
THUNK(lts_affix_prev)
THUNK(lts_sym_vowel)
THUNK(lts_match_symbol)
THUNK(tn_say_char)
THUNK(lts_match_list)
THUNK(tn_rule_skip_text)
THUNK(tn_rule_skip_field)
THUNK(lts_convert_char)
#undef THUNK

const fw_replacement fw_replacements[] = {
    { "lts_affix_next",     0xAF11, FW_OUT_A | FW_OUT_Y, t_lts_affix_next },
    { "lts_affix_prev",     0xAF1C, FW_OUT_A | FW_OUT_Y, t_lts_affix_prev },
    { "lts_sym_vowel",      0xB007, FW_OUT_A,            t_lts_sym_vowel },
    { "lts_match_symbol",   0xB05E, FW_OUT_A | FW_OUT_Y, t_lts_match_symbol },
    { "tn_say_char",        0xB0C3, FW_OUT_X,            t_tn_say_char },
    { "lts_match_list",     0xB0E3, FW_OUT_A | FW_OUT_Z, t_lts_match_list },
    { "tn_rule_skip_text",  0xB138, FW_OUT_A | FW_OUT_Y, t_tn_rule_skip_text },
    { "tn_rule_skip_field", 0xB142, FW_OUT_Y,            t_tn_rule_skip_field },
    { "lts_convert_char",   0xB172, 0,                   t_lts_convert_char },
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
