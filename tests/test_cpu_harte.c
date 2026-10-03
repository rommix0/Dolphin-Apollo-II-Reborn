/* Tom Harte's 6502 SingleStepTests (MIT; tests/data/cpu6502_vectors.LICENSE): an independent check of
   cpu740 on the 6502 base set (M4.0 spec section 2.1). tools/harte_subset.py applies the filters; the masks
   here are the 740's documented differences from the 6502:
   - P bit 5 is the 740's T flag, not a constant 1: it is cleared in the initial state and not compared.
   - P bit 4 (B) is not stored by the 6502; cpu740 keeps it set (reset sets B, PLP/RTI force it, PHP pushes P
     as stored). The initial state loads B set, and B is not compared in P.
   - The status byte PHP pushes is compared with bit 5 masked (T = 0 on the 740, 1 on the 6502).
   Cycle counts are not compared: cpu740's counts are approximate (spec section 3). */
#include <stdio.h>
#include <stdlib.h>
#include "test.h"
#include "cpu_harness.h"

typedef struct { uint16_t pc; uint8_t s, a, x, y, p, n; const uint8_t *ram; } hstate;

static const uint8_t *parse_state(const uint8_t *q, const uint8_t *end, hstate *st)
{
    if (end - q < 8)
        return NULL;
    st->pc = (uint16_t)(q[0] | q[1] << 8);
    st->s = q[2];
    st->a = q[3];
    st->x = q[4];
    st->y = q[5];
    st->p = q[6];
    st->n = q[7];
    q += 8;
    if (end - q < 3 * st->n)
        return NULL;
    st->ram = q;
    return q + 3 * st->n;
}

static uint8_t *read_all(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = size > 0 ? malloc((size_t)size) : NULL;
    *len = data ? fread(data, 1, (size_t)size, f) : 0;
    fclose(f);
    return data;
}

void suite_cpu_harte(void)
{
    size_t len = 0;
    uint8_t *data = read_all("tests/data/cpu6502_vectors.dat", &len);
    CHECK(data != NULL);
    if (!data)
        return;
    CHECK(len >= 12 && memcmp(data, "HARTE740", 8) == 0);
    uint32_t count = (uint32_t)(data[8] | data[9] << 8 | data[10] << 16 | (uint32_t)data[11] << 24);
    const uint8_t *q = data + 12, *end = data + len;
    static unsigned runs[256], fails[256];
    memset(runs, 0, sizeof runs);
    memset(fails, 0, sizeof fails);
    unsigned reported = 0;
    for (uint32_t i = 0; i < count && q; i++) {
        hstate in = {0}, out = {0};
        uint8_t op = *q++;
        q = parse_state(q, end, &in);
        if (q)
            q = parse_state(q, end, &out);
        CHECK(q != NULL);
        if (!q)
            break;
        memset(&g_mem, 0, sizeof g_mem);
        for (unsigned k = 0; k < in.n; k++)
            g_mem.mem[in.ram[3 * k] | in.ram[3 * k + 1] << 8] = in.ram[3 * k + 2];
        memset(&g_cpu, 0, sizeof g_cpu);
        g_cpu.pc = in.pc;
        g_cpu.s = in.s;
        g_cpu.a = in.a;
        g_cpu.x = in.x;
        g_cpu.y = in.y;
        g_cpu.p = (uint8_t)((in.p & ~CPU740_T) | CPU740_B);
        int cycles = cpu740_step(&g_cpu, &g_bus);
        bool ok = cycles > 0 && g_cpu.pc == out.pc && g_cpu.s == out.s && g_cpu.a == out.a &&
                  g_cpu.x == out.x && g_cpu.y == out.y &&
                  ((g_cpu.p ^ out.p) & ~(CPU740_T | CPU740_B)) == 0;
        for (unsigned k = 0; k < out.n; k++) {
            uint16_t addr = (uint16_t)(out.ram[3 * k] | out.ram[3 * k + 1] << 8);
            uint8_t mask = (op == 0x08 && addr == 0x100 + in.s) ? (uint8_t)~CPU740_T : 0xFF;
            if ((g_mem.mem[addr] ^ out.ram[3 * k + 2]) & mask)
                ok = false;
        }
        runs[op]++;
        if (!ok) {
            fails[op]++;
            if (reported++ < 20)
                fprintf(stderr, "  $%02X at %04X: got PC=%04X S=%02X A=%02X X=%02X Y=%02X P=%02X (%d cycles), "
                        "want PC=%04X S=%02X A=%02X X=%02X Y=%02X P=%02X\n", op, in.pc, g_cpu.pc, g_cpu.s,
                        g_cpu.a, g_cpu.x, g_cpu.y, g_cpu.p, cycles, out.pc, out.s, out.a, out.x, out.y, out.p);
        }
    }
    CHECK(q == end);
    unsigned ops = 0;
    for (int op = 0; op < 256; op++) {
        if (runs[op])
            ops++;
        if (fails[op]) {
            fprintf(stderr, "  opcode $%02X: %u of %u vectors fail\n", op, fails[op], runs[op]);
            g_test_failures++;
        }
    }
    printf("  %u vectors over %u opcodes\n", (unsigned)count, ops);
    CHECK_EQ(ops, 149);
    free(data);
}
