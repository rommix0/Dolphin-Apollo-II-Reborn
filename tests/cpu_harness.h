#ifndef CPU_HARNESS_H
#define CPU_HARNESS_H
/* Flat 64 KB memory for CPU tests. Each test file gets its own copy. */
#include <string.h>
#include "cpu740/cpu740.h"

typedef struct { uint8_t mem[0x10000]; } flatmem;

static flatmem g_mem;
static cpu740 g_cpu;

static inline uint8_t fm_read(void *ctx, uint16_t a) { return ((flatmem *)ctx)->mem[a]; }
static inline void fm_write(void *ctx, uint16_t a, uint8_t v) { ((flatmem *)ctx)->mem[a] = v; }

static const cpu740_bus g_bus = { &g_mem, fm_read, fm_write };

/* Clear memory, copy code to org, point RESET at it, reset the CPU. */
static inline void load(uint16_t org, const uint8_t *code, size_t n)
{
    memset(&g_mem, 0, sizeof g_mem);
    memcpy(g_mem.mem + org, code, n);
    g_mem.mem[0xFFFE] = (uint8_t)org;
    g_mem.mem[0xFFFF] = (uint8_t)(org >> 8);
    cpu740_reset(&g_cpu, &g_bus);
}

/* Execute `steps` steps; returns the total cycles reported. */
static inline int run(int steps)
{
    int total = 0;
    for (int i = 0; i < steps; i++)
        total += cpu740_step(&g_cpu, &g_bus);
    return total;
}
#endif
