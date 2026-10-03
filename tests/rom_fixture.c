#include <stdio.h>
#include <stdlib.h>
#include "rom_fixture.h"
#include "engine/rom_id.h"

const uint8_t *test_rom(size_t *len)
{
    static uint8_t rom[0x10000];
    static int state;   /* 0 = not tried, 1 = loaded, -1 = missing, wrong size or not the UK V2.01 4.04 image */
    if (state == 0) {
        const char *path = getenv("APOLLO2_ROM");
        FILE *f = fopen(path ? path : "UK_V2.01_4.04_ROM.BIN", "rb");
        state = -1;
        if (f) {
            if (fread(rom, 1, sizeof rom, f) == sizeof rom && fgetc(f) == EOF)
                state = 1;
            fclose(f);
        }
        if (state == 1 && !rom_id_is_apollo2(rom, sizeof rom)) {
            char hex[33];
            rom_id_md5_hex(rom, sizeof rom, hex);
            fprintf(stderr, "  ROM %s has MD5 %s, not the UK V2.01 4.04 image (%s): ROM tests are skipped\n",
                    path ? path : "UK_V2.01_4.04_ROM.BIN", hex, ROM_ID_MD5);
            state = -1;
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

char *test_read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    char *s = malloc((size_t)size + 1);
    size_t n = s ? fread(s, 1, (size_t)size, f) : 0;
    fclose(f);
    if (!s)
        return NULL;
    s[n] = '\0';
    return s;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

bool test_parse_hex(const char *hex, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int hi = hex_digit(hex[2 * i]), lo = hi < 0 ? -1 : hex_digit(hex[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return true;
}
