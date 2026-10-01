#include "cpu740/cpu740.h"
#include "cpu740/optable.h"

/* Approximate cycle counts per addressing mode; only the timer pacing depends on them. */
static const uint8_t k_mode_cycles[AM_COUNT] = {
    /* IMP ACC IMM ZP ZPX ZPY ABS ABX ABY IND IZX IZY REL ZPI SPG LDM BA BZ BAR BZR */
       2,  2,  2,  3, 4,  4,  4,  4,  4,  5,  6,  5,  2,  5,  5,  4,  2, 5, 4,  5,
};

typedef struct {
    uint16_t ea;      /* effective address; for AM_IMM the address of the operand byte */
    uint16_t target;  /* branch target */
    uint8_t imm;      /* LDM immediate */
    uint8_t bit;      /* bit number for SEB/CLB/BBS/BBC */
} operand;

static uint8_t rd(const cpu740_bus *b, uint16_t a) { return b->read(b->ctx, a); }
static void wr(const cpu740_bus *b, uint16_t a, uint8_t v) { b->write(b->ctx, a, v); }
static uint8_t fetch(cpu740 *c, const cpu740_bus *b) { return rd(b, c->pc++); }

static void push(cpu740 *c, const cpu740_bus *b, uint8_t v)
{
    wr(b, (uint16_t)(0x0100 | c->s), v);
    c->s--;
}

static uint8_t pull(cpu740 *c, const cpu740_bus *b)
{
    c->s++;
    return rd(b, (uint16_t)(0x0100 | c->s));
}

static void set_flag(cpu740 *c, uint8_t f, bool on)
{
    if (on)
        c->p |= f;
    else
        c->p &= (uint8_t)(0xFF ^ f);
}

static void set_nz(cpu740 *c, uint8_t v)
{
    set_flag(c, CPU740_N, (v & 0x80) != 0);
    set_flag(c, CPU740_Z, v == 0);
}

static uint16_t read16_zp(const cpu740_bus *b, uint8_t zp)
{
    uint8_t lo = rd(b, zp);
    uint8_t hi = rd(b, (uint8_t)(zp + 1));
    return (uint16_t)(lo | hi << 8);
}

void cpu740_reset(cpu740 *c, const cpu740_bus *b)
{
    c->a = c->x = c->y = 0;
    c->s = 0xFF;
    c->p = CPU740_I | CPU740_B;
    c->waiting = false;
    c->irq_lines = 0;
    c->cycles = 0;
    c->fault = CPU740_OK;
    c->fault_pc = 0;
    c->fault_opcode = 0;
    uint8_t lo = rd(b, 0xFFFE);
    uint8_t hi = rd(b, 0xFFFF);
    c->pc = (uint16_t)(lo | hi << 8);
}

void cpu740_set_irq(cpu740 *c, int line, bool asserted)
{
    uint32_t bit = 1u << line;
    if (asserted)
        c->irq_lines |= bit;
    else
        c->irq_lines &= ~bit;
}

static int take_irq(cpu740 *c, const cpu740_bus *b)
{
    int line = 0;
    while (!(c->irq_lines & (1u << line)))
        line++;
    uint16_t vec = (uint16_t)(0xFFFC - 2 * line);
    push(c, b, (uint8_t)(c->pc >> 8));
    push(c, b, (uint8_t)c->pc);
    push(c, b, (uint8_t)(c->p & (0xFF ^ CPU740_B)));
    c->p |= CPU740_I;
    uint8_t lo = rd(b, vec);
    uint8_t hi = rd(b, (uint16_t)(vec + 1));
    c->pc = (uint16_t)(lo | hi << 8);
    return 7;
}

static operand decode(cpu740 *c, const cpu740_bus *b, uint8_t op, int mode)
{
    operand o = { 0 };
    uint8_t lo, hi, zp;
    int8_t off;
    o.bit = (uint8_t)(op >> 5);
    switch (mode) {
    case AM_IMM: o.ea = c->pc++; break;
    case AM_ZP:  o.ea = fetch(c, b); break;
    case AM_ZPX: o.ea = (uint8_t)(fetch(c, b) + c->x); break;
    case AM_ZPY: o.ea = (uint8_t)(fetch(c, b) + c->y); break;
    case AM_ABS: lo = fetch(c, b); hi = fetch(c, b); o.ea = (uint16_t)(lo | hi << 8); break;
    case AM_ABX: lo = fetch(c, b); hi = fetch(c, b); o.ea = (uint16_t)((lo | hi << 8) + c->x); break;
    case AM_ABY: lo = fetch(c, b); hi = fetch(c, b); o.ea = (uint16_t)((lo | hi << 8) + c->y); break;
    case AM_IND: {
        lo = fetch(c, b); hi = fetch(c, b);
        uint16_t ptr = (uint16_t)(lo | hi << 8);
        uint16_t ptr_hi = (uint16_t)((ptr & 0xFF00) | ((ptr + 1) & 0x00FF));  /* 6502 page wrap */
        lo = rd(b, ptr);
        hi = rd(b, ptr_hi);
        o.ea = (uint16_t)(lo | hi << 8);
        break;
    }
    case AM_IZX: zp = (uint8_t)(fetch(c, b) + c->x); o.ea = read16_zp(b, zp); break;
    case AM_IZY: zp = fetch(c, b); o.ea = (uint16_t)(read16_zp(b, zp) + c->y); break;
    case AM_REL:
    case AM_BIT_A_REL:
        off = (int8_t)fetch(c, b);
        o.target = (uint16_t)(c->pc + off);
        break;
    case AM_ZPI: zp = fetch(c, b); o.ea = read16_zp(b, zp); break;
    case AM_SPG: o.ea = (uint16_t)(0xFF00 | fetch(c, b)); break;
    case AM_LDM: o.imm = fetch(c, b); o.ea = fetch(c, b); break;
    case AM_BIT_ZP: o.ea = fetch(c, b); break;
    case AM_BIT_ZP_REL:
        o.ea = fetch(c, b);
        off = (int8_t)fetch(c, b);
        o.target = (uint16_t)(c->pc + off);
        break;
    default: break;   /* AM_IMP, AM_ACC, AM_BIT_A: no operand bytes */
    }
    return o;
}

static void adc(cpu740 *c, uint8_t v)
{
    unsigned sum = (unsigned)c->a + v + (c->p & CPU740_C);
    set_flag(c, CPU740_V, ((~(c->a ^ v) & (c->a ^ sum)) & 0x80) != 0);
    set_flag(c, CPU740_C, sum > 0xFF);
    c->a = (uint8_t)sum;
    set_nz(c, c->a);
}

static void compare(cpu740 *c, uint8_t reg, uint8_t v)
{
    set_flag(c, CPU740_C, reg >= v);
    set_nz(c, (uint8_t)(reg - v));
}

static uint8_t shift(cpu740 *c, int mnem, uint8_t v)
{
    uint8_t carry_in = (uint8_t)(c->p & CPU740_C);
    uint8_t r;
    switch (mnem) {
    case M_ASL: set_flag(c, CPU740_C, (v & 0x80) != 0); r = (uint8_t)(v << 1); break;
    case M_ROL: set_flag(c, CPU740_C, (v & 0x80) != 0); r = (uint8_t)((v << 1) | carry_in); break;
    case M_LSR: set_flag(c, CPU740_C, (v & 0x01) != 0); r = (uint8_t)(v >> 1); break;
    default:    set_flag(c, CPU740_C, (v & 0x01) != 0); r = (uint8_t)((v >> 1) | (carry_in << 7)); break;
    }
    set_nz(c, r);
    return r;
}

static bool branch_taken(const cpu740 *c, int mnem)
{
    switch (mnem) {
    case M_BPL: return !(c->p & CPU740_N);
    case M_BMI: return (c->p & CPU740_N) != 0;
    case M_BVC: return !(c->p & CPU740_V);
    case M_BVS: return (c->p & CPU740_V) != 0;
    case M_BCC: return !(c->p & CPU740_C);
    case M_BCS: return (c->p & CPU740_C) != 0;
    case M_BNE: return !(c->p & CPU740_Z);
    case M_BEQ: return (c->p & CPU740_Z) != 0;
    default:    return true;   /* BRA */
    }
}

int cpu740_step(cpu740 *c, const cpu740_bus *b)
{
    if (c->fault != CPU740_OK)
        return 0;
    if (c->waiting) {
        if (!c->irq_lines) {
            c->cycles += 4;
            return 4;
        }
        c->waiting = false;   /* any asserted line ends WIT, even while I is set */
    }
    if (c->irq_lines && !(c->p & CPU740_I)) {
        int n = take_irq(c, b);
        c->cycles += (uint64_t)n;
        return n;
    }

    uint16_t op_pc = c->pc;
    uint8_t op = fetch(c, b);
    const cpu740_opinfo info = cpu740_optable()[op];
    operand o = decode(c, b, op, info.mode);
    int cycles = k_mode_cycles[info.mode];
    cpu740_fault fault = CPU740_OK;
    uint8_t v;

    switch (info.mnem) {
    case M_LDA: c->a = rd(b, o.ea); set_nz(c, c->a); break;
    case M_LDX: c->x = rd(b, o.ea); set_nz(c, c->x); break;
    case M_LDY: c->y = rd(b, o.ea); set_nz(c, c->y); break;
    case M_STA: wr(b, o.ea, c->a); break;
    case M_STX: wr(b, o.ea, c->x); break;
    case M_STY: wr(b, o.ea, c->y); break;

    case M_ADC:
    case M_SBC:
        if (c->p & CPU740_D) {   /* decimal mode is never used by the Apollo ROM */
            fault = CPU740_FAULT_UNSUPPORTED;
            break;
        }
        v = rd(b, o.ea);
        adc(c, info.mnem == M_ADC ? v : (uint8_t)(0xFF ^ v));
        break;
    case M_AND: c->a &= rd(b, o.ea); set_nz(c, c->a); break;
    case M_ORA: c->a |= rd(b, o.ea); set_nz(c, c->a); break;
    case M_EOR: c->a ^= rd(b, o.ea); set_nz(c, c->a); break;
    case M_CMP: compare(c, c->a, rd(b, o.ea)); break;
    case M_CPX: compare(c, c->x, rd(b, o.ea)); break;
    case M_CPY: compare(c, c->y, rd(b, o.ea)); break;
    case M_BIT:
        v = rd(b, o.ea);
        set_flag(c, CPU740_N, (v & 0x80) != 0);
        set_flag(c, CPU740_V, (v & 0x40) != 0);
        set_flag(c, CPU740_Z, (c->a & v) == 0);
        break;

    case M_ASL: case M_ROL: case M_LSR: case M_ROR:
        if (info.mode == AM_ACC) {
            c->a = shift(c, info.mnem, c->a);
        } else {
            v = rd(b, o.ea);
            wr(b, o.ea, shift(c, info.mnem, v));
            cycles += 2;
        }
        break;
    case M_INC:
    case M_DEC: {
        int delta = info.mnem == M_INC ? 1 : -1;
        if (info.mode == AM_ACC) {   /* 740 INC A / DEC A */
            c->a = (uint8_t)(c->a + delta);
            set_nz(c, c->a);
        } else {
            v = (uint8_t)(rd(b, o.ea) + delta);
            wr(b, o.ea, v);
            set_nz(c, v);
            cycles += 2;
        }
        break;
    }

    case M_INX: c->x++; set_nz(c, c->x); break;
    case M_INY: c->y++; set_nz(c, c->y); break;
    case M_DEX: c->x--; set_nz(c, c->x); break;
    case M_DEY: c->y--; set_nz(c, c->y); break;
    case M_TAX: c->x = c->a; set_nz(c, c->x); break;
    case M_TAY: c->y = c->a; set_nz(c, c->y); break;
    case M_TXA: c->a = c->x; set_nz(c, c->a); break;
    case M_TYA: c->a = c->y; set_nz(c, c->a); break;
    case M_TSX: c->x = c->s; set_nz(c, c->x); break;
    case M_TXS: c->s = c->x; break;

    case M_CLC: set_flag(c, CPU740_C, false); break;
    case M_SEC: set_flag(c, CPU740_C, true); break;
    case M_CLI: set_flag(c, CPU740_I, false); break;
    case M_SEI: set_flag(c, CPU740_I, true); break;
    case M_CLD: set_flag(c, CPU740_D, false); break;
    case M_CLV: set_flag(c, CPU740_V, false); break;
    case M_SED: fault = CPU740_FAULT_UNSUPPORTED; break;
    case M_NOP: break;

    case M_BPL: case M_BMI: case M_BVC: case M_BVS:
    case M_BCC: case M_BCS: case M_BNE: case M_BEQ: case M_BRA:
        if (branch_taken(c, info.mnem)) {
            c->pc = o.target;
            cycles += 1;
        }
        break;

    case M_JMP:
        c->pc = o.ea;
        if (info.mode == AM_ABS)
            cycles = 3;
        break;
    case M_JSR:
        push(c, b, (uint8_t)((c->pc - 1) >> 8));   /* address of the instruction's last byte */
        push(c, b, (uint8_t)(c->pc - 1));
        c->pc = o.ea;
        cycles = info.mode == AM_ABS ? 6 : info.mode == AM_ZPI ? 7 : 5;
        break;
    case M_RTS: {
        uint8_t lo = pull(c, b);
        uint8_t hi = pull(c, b);
        c->pc = (uint16_t)((lo | hi << 8) + 1);
        cycles = 6;
        break;
    }
    case M_RTI: {
        c->p = (uint8_t)(pull(c, b) | CPU740_B);
        uint8_t lo = pull(c, b);
        uint8_t hi = pull(c, b);
        c->pc = (uint16_t)(lo | hi << 8);
        cycles = 6;
        if (c->p & CPU740_T)
            fault = CPU740_FAULT_TMODE;
        break;
    }
    case M_PHA: push(c, b, c->a); cycles = 3; break;
    case M_PHP: push(c, b, c->p); cycles = 3; break;
    case M_PLA: c->a = pull(c, b); set_nz(c, c->a); cycles = 4; break;
    case M_PLP:
        c->p = (uint8_t)(pull(c, b) | CPU740_B);
        cycles = 4;
        if (c->p & CPU740_T)
            fault = CPU740_FAULT_TMODE;
        break;
    case M_BRK: fault = CPU740_FAULT_UNSUPPORTED; break;   /* never used: usually a runaway PC */

    /* 740 extensions */
    case M_BBS:
    case M_BBC: {
        v = info.mode == AM_BIT_A_REL ? c->a : rd(b, o.ea);
        bool bit_set = ((v >> o.bit) & 1) != 0;
        if (bit_set == (info.mnem == M_BBS)) {
            c->pc = o.target;
            cycles += 2;
        }
        break;
    }
    case M_SEB:
    case M_CLB: {
        uint8_t mask = (uint8_t)(1u << o.bit);
        if (info.mode == AM_BIT_A) {
            c->a = info.mnem == M_SEB ? (uint8_t)(c->a | mask) : (uint8_t)(c->a & ~mask);
        } else {
            v = rd(b, o.ea);
            wr(b, o.ea, info.mnem == M_SEB ? (uint8_t)(v | mask) : (uint8_t)(v & ~mask));
        }
        break;
    }
    case M_LDM: wr(b, o.ea, o.imm); break;
    case M_TST: set_nz(c, rd(b, o.ea)); break;
    case M_COM:
        v = (uint8_t)~rd(b, o.ea);
        set_nz(c, v);
        wr(b, o.ea, v);
        cycles += 2;
        break;
    case M_RRF:
        v = rd(b, o.ea);
        wr(b, o.ea, (uint8_t)((v << 4) | (v >> 4)));
        cycles = 8;
        break;
    case M_CLT: set_flag(c, CPU740_T, false); break;
    case M_WIT: c->waiting = true; break;
    case M_SET: case M_STP: case M_MUL: case M_DIV:
        fault = CPU740_FAULT_UNSUPPORTED;   /* verified unused by the Apollo ROM (spec §1.1) */
        break;

    default: fault = CPU740_FAULT_ILLEGAL; break;
    }

    if (fault != CPU740_OK) {
        c->fault = fault;
        c->fault_pc = op_pc;
        c->fault_opcode = op;
        c->pc = op_pc;
        return 0;
    }
    c->cycles += (uint64_t)cycles;
    return cycles;
}
