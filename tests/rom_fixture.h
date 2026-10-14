#ifndef ROM_FIXTURE_H
#define ROM_FIXTURE_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "machine/machine.h"
/* Returns the 64 KB Apollo ROM (APOLLO2_ROM or ./UK_V2.01_4.04_ROM.BIN), or NULL if missing or if it is not the UK V2.01 4.04 image (MD5).
   The only place the tests look for the ROM: every call sets g_test_rom_asked (test.h); with APOLLO2_NO_ROM set to a
   non-empty value it returns NULL without looking (M5b spec 4.1). */
const uint8_t *test_rom(size_t *len);
/* Prints every logged unmodelled access of m to stderr (one line each). */
void print_unmodelled(const machine *m);
/* Whole file as a NUL-terminated malloc'ed string (binary mode, no CRLF translation); NULL if missing. */
char *test_read_file(const char *path);
/* Parses 2*n hex digits into out. Returns false on a non-hex character. */
bool test_parse_hex(const char *hex, uint8_t *out, size_t n);
#endif
