#include <string.h>
#include "machine/machine.h"

#define P2_BANK_MASK  0x7C
#define P2_MAIN_BANK  0x70
#define P2_LOWER_HALF 0x80
#define P2_DSP_RESET  0x01
#define P0_BUSY       0x20
#define ICR2_T3_ENABLE 0x10

void machine_default_config(machine_config *cfg)
{
    /* Switches off (serial mode); P2 pulled to $70 so the main ROM is selected at reset. */
    static const uint8_t k_default[5] = { 0x00, 0x00, 0x70, 0x00, 0x00 };
    memcpy(cfg->port_in, k_default, sizeof k_default);
    cfg->cycle_num = cfg->cycle_den = 1;
    cfg->coverage = NULL;
}

static bool is_sfr(uint16_t a) { return a >= M50734_SFR_FIRST && a <= 0x00FF; }

static bool main_rom_selected(const machine *m)
{
    return (m50734_port_pins(&m->sfr, 2) & P2_BANK_MASK) == P2_MAIN_BANK;
}

static bool lower_half_selected(const machine *m)
{
    return (m50734_port_pins(&m->sfr, 2) & P2_LOWER_HALF) != 0;
}

/* Coverage space of an instruction address under the current banking (M3 spec §5.1). */
static cov_space coverage_space(const machine *m, uint16_t pc)
{
    if (pc >= 0x8000)
        return COV_ROM;
    if (pc >= 0x4000)
        return lower_half_selected(m) ? COV_LOW : COV_ROM;
    return pc >= 0x0200 && pc <= 0x03FE ? COV_RAMCODE : COV_RAM;
}

/* Spec §4: executing from SFR space or from a deselected ROM bank is a runaway PC. */
static bool pc_escaped(const machine *m)
{
    uint16_t pc = m->cpu.pc;
    return is_sfr(pc) || (pc >= 0x8000 && !main_rom_selected(m));
}

static void note_unmodelled(machine *m, uint16_t addr, bool write)
{
    for (unsigned i = 0; i < m->unmodelled_count; i++)
        if (m->unmodelled[i].addr == addr && m->unmodelled[i].write == write)
            return;
    if (m->unmodelled_count < MACHINE_MAX_UNMODELLED) {
        machine_access *a = &m->unmodelled[m->unmodelled_count++];
        a->addr = addr;
        a->pc = m->op_pc;
        a->write = write;
    } else {
        m->unmodelled_dropped++;
    }
}

static uint8_t bus_read(void *ctx, uint16_t a)
{
    machine *m = ctx;
    if (is_sfr(a)) {
        if (!m50734_is_modelled((uint8_t)a) || m50734_is_write_only((uint8_t)a))
            note_unmodelled(m, a, false);
        return m50734_read(&m->sfr, (uint8_t)a);
    }
    if (a < 0x4000)
        return m->ram[a];
    if (a < 0x8000) {
        if (lower_half_selected(m)) {
            m->lower_window_reads++;
            return m->rom[a - 0x4000];
        }
        return m->rom[a];
    }
    return main_rom_selected(m) ? m->rom[a] : 0xFF;
}

static void bus_write(void *ctx, uint16_t a, uint8_t v)
{
    machine *m = ctx;
    if (is_sfr(a)) {
        if (!m50734_is_modelled((uint8_t)a))
            note_unmodelled(m, a, true);
        m50734_write(&m->sfr, (uint8_t)a, v);
        if ((m->sfr.p0_func & 0x20) && (m->sfr.port_dir[0] & 0x20))
            m->fault = MFAULT_DME;   /* data-memory strobe: never used by the Apollo ROM */
        bool reset = (m50734_port_pins(&m->sfr, 2) & P2_DSP_RESET) != 0;
        if (reset && !m->dsp_reset_level) {   /* rising edge: the DSP restarts at word 0 */
            m->dsp_resets++;
            m->raw_discarded += m->raw_pos;
            m->raw_pos = 0;
        }
        m->dsp_reset_level = reset;
        return;
    }
    if (a < 0x4000) {
        m->ram[a] = v;
        return;
    }
    if (a == APOLLO_DSP_PORT) {
        m->port_writes++;
        m->raw_buf[m->raw_pos++] = v;
        if (m->raw_pos == APOLLO_FRAME_BYTES) {
            m->raw_pos = 0;
            m->raw_frames++;
            if (m->on_raw)
                m->on_raw(m->raw_user, m->raw_buf);
        }
        return;
    }
    note_unmodelled(m, a, true);
}

uint8_t machine_peek(const machine *m, uint16_t a)
{
    if (is_sfr(a))
        return 0;
    if (a < 0x4000)
        return m->ram[a];
    if (a < 0x8000)
        return lower_half_selected(m) ? m->rom[a - 0x4000] : m->rom[a];
    return main_rom_selected(m) ? m->rom[a] : 0xFF;
}

bool machine_init(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg)
{
    machine_config def;
    if (len != MACHINE_ROM_SIZE)
        return false;
    if (!cfg) {
        machine_default_config(&def);
        cfg = &def;
    }
    memset(m, 0, sizeof *m);
    memcpy(m->rom, rom, len);
    m->cycle_num = cfg->cycle_num ? cfg->cycle_num : 1;
    m->cycle_den = cfg->cycle_den ? cfg->cycle_den : 1;
    m->coverage = cfg->coverage;
    m50734_reset(&m->sfr, cfg->port_in);
    m->dsp_reset_level = (m50734_port_pins(&m->sfr, 2) & P2_DSP_RESET) != 0;
    m->bus.ctx = m;
    m->bus.read = bus_read;
    m->bus.write = bus_write;
    cpu740_reset(&m->cpu, &m->bus);
    return true;
}

void machine_set_outputs(machine *m, machine_frame_fn on_frame, void *user)
{
    m->on_frame = on_frame;
    m->user = user;
}

void machine_set_raw_output(machine *m, machine_frame_fn on_raw, void *user)
{
    m->on_raw = on_raw;
    m->raw_user = user;
}

bool machine_feed_serial(machine *m, const uint8_t *bytes, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        size_t next = (m->q_tail + 1) % MACHINE_QUEUE_SIZE;
        if (next == m->q_head)
            return false;
        m->queue[m->q_tail] = bytes[i];
        m->q_tail = next;
    }
    return true;
}

static bool at_idle(const machine *m)
{
    return m->cpu.waiting && m->cpu.pc == (uint16_t)(APOLLO_IDLE_WIT_PC + 1);
}

/* Spec §4: one byte per idle WIT, after the previous byte was read, while P0.5 is not busy. */
static void maybe_deliver(machine *m)
{
    if (!at_idle(m))
        return;
    uint8_t busy = (uint8_t)(m50734_port_pins(&m->sfr, 0) & P0_BUSY);
    if (!m->busy_level_known) {   /* the "not busy" level is the one seen at the first idle */
        m->busy_idle_level = busy;
        m->busy_level_known = true;
    }
    if (m->sfr.uart_rx_full || m->q_head == m->q_tail || busy != m->busy_idle_level)
        return;
    m50734_uart_receive(&m->sfr, m->queue[m->q_head]);
    m->q_head = (m->q_head + 1) % MACHINE_QUEUE_SIZE;
}

static void capture_frame(machine *m)
{
    uint8_t frame[APOLLO_FRAME_BYTES];
    uint16_t buf = (uint16_t)(m->ram[APOLLO_PRODUCER_PTR] | m->ram[APOLLO_PRODUCER_PTR + 1] << 8);
    for (int i = 0; i < APOLLO_FRAME_BYTES; i++)
        frame[i] = machine_peek(m, (uint16_t)(buf + i));
    m->frames_produced++;
    if (m->on_frame)
        m->on_frame(m->user, frame);
}

static void sync_irqs(machine *m)
{
    uint32_t lines = m50734_irq_lines(&m->sfr);
    for (int i = 0; i < M50734_LINE_COUNT; i++)
        cpu740_set_irq(&m->cpu, i, ((lines >> i) & 1) != 0);
}

machine_status machine_run(machine *m, uint64_t max_cycles)
{
    uint64_t end = m->cpu.cycles + max_cycles;
    while (m->cpu.cycles < end) {
        maybe_deliver(m);
        sync_irqs(m);
        if (at_idle(m) && m->q_head == m->q_tail && !m->sfr.uart_rx_full &&
            !(m->sfr.icr2 & ICR2_T3_ENABLE) && !m->cpu.irq_lines)
            return MACHINE_IDLE;
        if (pc_escaped(m)) {
            m->fault = MFAULT_ESCAPED_PC;
            return MACHINE_FAULT;
        }
        if (m->cpu.pc == APOLLO_SWAP_PC && !m->cpu.waiting)
            capture_frame(m);
        m->op_pc = m->cpu.pc;
        m->history[m->history_pos++ % MACHINE_HISTORY] = m->cpu.pc;
        cov_space cov_from = COV_ROM;
        bool cov_indirect = false;
        if (m->coverage) {
            /* Mirrors cpu740_step: a waiting CPU wakes on any IRQ line; an unmasked IRQ is taken
               instead of the instruction at op_pc. Only an executed instruction is recorded. */
            bool wakes = !m->cpu.waiting || m->cpu.irq_lines;
            bool irq = m->cpu.irq_lines && !(m->cpu.p & CPU740_I);
            if (wakes && !irq) {
                uint8_t op = machine_peek(m, m->op_pc);
                cov_from = coverage_space(m, m->op_pc);
                coverage_mark(m->coverage, cov_from, m->op_pc);
                cov_indirect = op == 0x6C || op == 0xB2 || op == 0x02;   /* JMP (abs), JMP (zp), JSR (zp) */
            }
        }
        int cycles = cpu740_step(&m->cpu, &m->bus);
        if (cycles == 0) {
            m->fault = MFAULT_CPU;
            return MACHINE_FAULT;
        }
        if (cov_indirect)
            coverage_add_edge(m->coverage, cov_from, m->op_pc, coverage_space(m, m->cpu.pc), m->cpu.pc);
        uint64_t acc = m->cycle_rem + (uint64_t)cycles * m->cycle_num;
        m->cycle_rem = acc % m->cycle_den;
        m50734_tick(&m->sfr, (int)(acc / m->cycle_den));
        if (m->fault != MFAULT_NONE)
            return MACHINE_FAULT;
    }
    return MACHINE_CYCLE_LIMIT;
}

static uint8_t peek_cb(void *ctx, uint16_t a) { return machine_peek(ctx, a); }

void machine_dump(const machine *m, FILE *out)
{
    static const char *const k_faults[] = {
        "none", "cpu", "DME (data-memory strobe) enabled", "escaped PC (SFR space or deselected ROM bank)",
        "input longer than the serial queue",
    };
    static const char *const k_cpu_faults[] = {
        "ok", "illegal opcode", "unsupported instruction (SET/SED/STP/MUL/DIV/BRK)", "T flag set",
    };
    const cpu740 *c = &m->cpu;
    fprintf(out, "machine fault: %s", k_faults[m->fault]);
    if (m->fault == MFAULT_CPU)
        fprintf(out, " (%s, opcode $%02X at $%04X)", k_cpu_faults[c->fault], c->fault_opcode, c->fault_pc);
    fprintf(out, "\nA=%02X X=%02X Y=%02X S=%02X P=%02X PC=%04X cycles=%llu waiting=%d\n",
            c->a, c->x, c->y, c->s, c->p, c->pc, (unsigned long long)c->cycles, c->waiting ? 1 : 0);
    unsigned n = m->history_pos < MACHINE_HISTORY ? m->history_pos : MACHINE_HISTORY;
    fprintf(out, "last %u instructions:\n", n);
    for (unsigned i = 0; i < n; i++) {
        uint16_t pc = m->history[(m->history_pos - n + i) % MACHINE_HISTORY];
        char text[48];
        cpu740_disasm(pc, peek_cb, (void *)m, text, sizeof text);
        fprintf(out, "  %04X  %s\n", pc, text);
    }
    machine_print_unmodelled(m, out);
}

void machine_print_unmodelled(const machine *m, FILE *out)
{
    for (unsigned i = 0; i < m->unmodelled_count; i++)
        fprintf(out, "unmodelled %s $%04X (first at PC $%04X)\n",
                m->unmodelled[i].write ? "write" : "read", m->unmodelled[i].addr, m->unmodelled[i].pc);
    if (m->unmodelled_dropped)
        fprintf(out, "unmodelled log full: %u more accesses not listed\n", m->unmodelled_dropped);
}
