#include <stdio.h>
#include <stdlib.h>
#include "rom_fixture.h"

const uint8_t *test_rom(size_t *len)
{
    static uint8_t rom[0x10000];
    static int state;   /* 0 = not tried, 1 = loaded, -1 = missing or wrong size */
    if (state == 0) {
        const char *path = getenv("APOLLO2_ROM");
        FILE *f = fopen(path ? path : "UK_V2.01_4.04_ROM.BIN", "rb");
        state = -1;
        if (f) {
            if (fread(rom, 1, sizeof rom, f) == sizeof rom && fgetc(f) == EOF)
                state = 1;
            fclose(f);
        }
    }
    if (state != 1)
        return NULL;
    *len = sizeof rom;
    return rom;
}

void print_unmodelled(const machine *m)
{
    for (unsigned i = 0; i < m->unmodelled_count; i++)
        fprintf(stderr, "  unmodelled %s $%04X at PC $%04X\n", m->unmodelled[i].write ? "write" : "read",
                m->unmodelled[i].addr, m->unmodelled[i].pc);
}
