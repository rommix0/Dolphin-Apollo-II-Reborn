#ifndef ROM_ID_H
#define ROM_ID_H
/* Identity of the ROM image (spec section 1: UK V2.01 4.04, MD5 below; M4.0 spec section 2.2). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define ROM_ID_MD5 "74460801a399983b1a929f57e5ab57d7"

void rom_id_md5(const uint8_t *data, size_t len, uint8_t out[16]);
void rom_id_md5_hex(const uint8_t *data, size_t len, char out[33]);
/* True when rom is the 64 KB UK V2.01 4.04 image the goldens were recorded from. */
bool rom_id_is_apollo2(const uint8_t *rom, size_t len);
/* CLI policy: true for that image; for any other image prints a warning to err and returns true when any
   (--rom-any) is set, else prints the reason to err and returns false. */
bool rom_id_accept(const char *path, const uint8_t *rom, size_t len, bool any, FILE *err);

#endif
