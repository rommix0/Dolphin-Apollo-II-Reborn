#include <stdbool.h>
#include <string.h>
#include "cpu740/cpu740.h"
#include "cpu740/optable.h"
#include "cpu740/vec740.h"

#define VEC_MAX_ACCESS 16

/* Flat memory whose unknown bytes get random values on first read, which are then logged. */
typedef struct {
    uint8_t mem[0x10000];
    uint8_t known[0x10000 / 8];
    uint32_t rng;
    uint16_t rd_addr[VEC_MAX_ACCESS], wr_addr[VEC_MAX_ACCESS];
    uint8_t rd_val[VEC_MAX_ACCESS], wr_val[VEC_MAX_ACCESS];
    int nrd, nwr;
    bool overflow;
} vecmem;

static vecmem g_vm;

static uint32_t rnd(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

static bool is_known(const vecmem *v, uint16_t a) { return ((v->known[a >> 3] >> (a & 7)) & 1) != 0; }
static void set_known(vecmem *v, uint16_t a) { v->known[a >> 3] |= (uint8_t)(1u << (a & 7)); }

static uint8_t vm_read(void *ctx, uint16_t a)
{
    vecmem *v = ctx;
    if (!is_known(v, a)) {
        v->mem[a] = (uint8_t)rnd(&v->rng);
        set_known(v, a);
        if (v->nrd < VEC_MAX_ACCESS) {
            v->rd_addr[v->nrd] = a;
            v->rd_val[v->nrd++] = v->mem[a];
        } else {
            v->overflow = true;
        }
    }
    return v->mem[a];
}

static void vm_write(void *ctx, uint16_t a, uint8_t val)
{
    vecmem *v = ctx;
    set_known(v, a);
    v->mem[a] = val;
    for (int i = 0; i < v->nwr; i++) {
        if (v->wr_addr[i] == a) {
            v->wr_val[i] = val;
            return;
        }
    }
    if (v->nwr < VEC_MAX_ACCESS) {
        v->wr_addr[v->nwr] = a;
        v->wr_val[v->nwr++] = val;
    } else {
        v->overflow = true;
    }
}

static uint8_t vm_peek(void *ctx, uint16_t a) { return ((const vecmem *)ctx)->mem[a]; }

/* Operand bytes: mostly random, with $FF and $00 often enough to reach the zero-page and page wraps. */
static uint8_t operand_byte(uint32_t *rng)
{
    uint32_t r = rnd(rng);
    switch (r & 7) {
    case 0:  return 0xFF;
    case 1:  return 0x00;
    default: return (uint8_t)(r >> 8);
    }
}

static bool decode_only(int mnem)
{
    switch (mnem) {
    case M_ILL: case M_WIT: case M_BRK: case M_SED: case M_SET: case M_STP: case M_MUL: case M_DIV:
        return true;
    default:
        return false;
    }
}

static void put_list(char *out, size_t n, const uint16_t *addr, const uint8_t *val, int count)
{
    size_t pos = 0;
    out[0] = '\0';
    for (int i = 0; i < count && pos + 9 < n; i++)
        pos += (size_t)snprintf(out + pos, n - pos, "%s%04X:%02X", i ? "," : "", addr[i], val[i]);
}

int vec740_write(FILE *out, unsigned per_opcode, uint32_t seed)
{
    const cpu740_opinfo *table = cpu740_optable();
    const cpu740_bus bus = { &g_vm, vm_read, vm_write };
    uint32_t rng = seed ? seed : 1;
    int lines = 0;

    for (int op = 0; op < 256; op++) {
        int mnem = table[op].mnem;
        int len = mnem == M_ILL ? 1 : cpu740_mode_length(table[op].mode);
        unsigned want = decode_only(mnem) ? 1 : per_opcode, done = 0, tries = 0;
        while (done < want && tries++ < 20 * want + 20) {
            uint8_t bytes[3] = { 0 };
            char ops[12], text[48], rd[VEC_MAX_ACCESS * 8 + 1], wr[VEC_MAX_ACCESS * 8 + 1];
            uint16_t pc = (uint16_t)(0x4000 + rnd(&rng) % 0x3FF0);
            memset(&g_vm, 0, sizeof g_vm);
            bytes[0] = (uint8_t)op;
            for (int i = 1; i < len; i++)
                bytes[i] = operand_byte(&rng);
            for (int i = 0; i < len; i++) {
                g_vm.mem[(uint16_t)(pc + i)] = bytes[i];
                set_known(&g_vm, (uint16_t)(pc + i));
            }
            if (len == 1)
                snprintf(ops, sizeof ops, "%02X", bytes[0]);
            else if (len == 2)
                snprintf(ops, sizeof ops, "%02X,%02X", bytes[0], bytes[1]);
            else
                snprintf(ops, sizeof ops, "%02X,%02X,%02X", bytes[0], bytes[1], bytes[2]);
            cpu740_disasm(pc, vm_peek, &g_vm, text, sizeof text);
            if (decode_only(mnem)) {
                fprintf(out, "D pc=%04X op=%s | %s\n", pc, ops, text);
                lines++;
                done++;
                continue;
            }

            cpu740 c;
            memset(&c, 0, sizeof c);
            c.a = (uint8_t)rnd(&rng);
            c.x = (uint8_t)rnd(&rng);
            c.y = (uint8_t)rnd(&rng);
            c.s = (uint8_t)(0x10 + rnd(&rng) % 0xE0);
            c.p = (uint8_t)((rnd(&rng) & (uint32_t)~(CPU740_D | CPU740_T)) | CPU740_B);
            c.pc = pc;
            const cpu740 before = c;
            g_vm.rng = rnd(&rng) | 1;
            if (cpu740_step(&c, &bus) == 0 || c.waiting || g_vm.overflow)
                continue;   /* e.g. PLP/RTI pulling T = 1: try another state */
            put_list(rd, sizeof rd, g_vm.rd_addr, g_vm.rd_val, g_vm.nrd);
            put_list(wr, sizeof wr, g_vm.wr_addr, g_vm.wr_val, g_vm.nwr);
            fprintf(out, "V pc=%04X op=%s a=%02X x=%02X y=%02X s=%02X p=%02X r=%s | "
                         "A=%02X X=%02X Y=%02X S=%02X P=%02X PC=%04X w=%s | %s\n",
                    pc, ops, before.a, before.x, before.y, before.s, before.p, rd,
                    c.a, c.x, c.y, c.s, c.p, c.pc, wr, text);
            lines++;
            done++;
        }
        if (done < want)
            fprintf(out, "# opcode %02X: only %u of %u vectors (cpu740 faulted on the other tries)\n", op, done, want);
    }
    return ferror(out) ? -1 : lines;
}
