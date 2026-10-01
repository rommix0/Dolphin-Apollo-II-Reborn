#ifndef ROM_FIXTURE_H
#define ROM_FIXTURE_H
#include <stddef.h>
#include <stdint.h>
/* Returns the 64 KB Apollo ROM (APOLLO2_ROM or ./UK_V2.01_4.04_ROM.BIN), or NULL if missing. */
const uint8_t *test_rom(size_t *len);
#endif
