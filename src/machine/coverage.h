#ifndef COVERAGE_H
#define COVERAGE_H
/* Execution coverage of the emulated firmware, for the Ghidra project rebuild (M3 spec §5.1).
   Spaces: rom = main ROM and the default $4000-$7FFF window; low = $4000-$7FFF while P2.7 = 1
   (file 0x0000-0x3FFF); ramcode = $0200-$03FE (the boot copy of $ADFE-$AFFC); ram = other RAM. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum { COV_ROM, COV_LOW, COV_RAMCODE, COV_RAM, COV_SPACE_COUNT } cov_space;

typedef struct { uint8_t from_space, to_space; uint16_t from, to; } cov_edge;

typedef struct {
    uint8_t exec[COV_SPACE_COUNT][0x10000 / 8];   /* one bit per executed instruction address */
    cov_edge *edges;                              /* indirect JMP/JSR: unique (from, to) pairs */
    size_t edge_count, edge_cap;
    bool oom;                                     /* an edge could not be stored */
} coverage;

void coverage_init(coverage *c);
void coverage_free(coverage *c);
void coverage_mark(coverage *c, cov_space s, uint16_t pc);
bool coverage_hit(const coverage *c, cov_space s, uint16_t pc);
size_t coverage_count(const coverage *c, cov_space s);
void coverage_add_edge(coverage *c, cov_space from_space, uint16_t from, cov_space to_space, uint16_t to);
const char *coverage_space_name(cov_space s);
/* A header comment, then "x SPACE ADDR" lines in space order and address order, then
   "e FROMSPACE FROM TOSPACE TO" lines sorted the same way. Returns false on an I/O error or lost edge. */
bool coverage_write(const coverage *c, FILE *out);

#endif
