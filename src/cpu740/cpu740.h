#ifndef CPU740_H
#define CPU740_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    CPU740_C = 0x01, CPU740_Z = 0x02, CPU740_I = 0x04, CPU740_D = 0x08,
    CPU740_B = 0x10, CPU740_T = 0x20, CPU740_V = 0x40, CPU740_N = 0x80,
};

typedef enum {
    CPU740_OK = 0,
    CPU740_FAULT_ILLEGAL,      /* opcode the M50734 does not implement */
    CPU740_FAULT_UNSUPPORTED,  /* SET/SED/STP/MUL/DIV/BRK: verified unused by the Apollo ROM */
    CPU740_FAULT_TMODE,        /* T flag restored as 1 by PLP/RTI */
} cpu740_fault;

typedef struct {
    void *ctx;
    uint8_t (*read)(void *ctx, uint16_t addr);
    void (*write)(void *ctx, uint16_t addr, uint8_t value);
} cpu740_bus;

typedef struct {
    uint8_t a, x, y, s, p;
    uint16_t pc;
    bool waiting;          /* executed WIT: halted until any IRQ line is asserted */
    uint32_t irq_lines;    /* bit n set = line n asserted; vector = 0xFFFC - 2n */
    uint64_t cycles;
    cpu740_fault fault;
    uint16_t fault_pc;
    uint8_t fault_opcode;
} cpu740;

void cpu740_reset(cpu740 *cpu, const cpu740_bus *bus);
int cpu740_step(cpu740 *cpu, const cpu740_bus *bus);
void cpu740_set_irq(cpu740 *cpu, int line, bool asserted);

#endif
