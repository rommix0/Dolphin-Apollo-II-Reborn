#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "engine/rom_id.h"

static void md5_is(const char *msg, const char *want)
{
    char hex[33];
    rom_id_md5_hex((const uint8_t *)msg, strlen(msg), hex);
    CHECK_STR(hex, want);
}

void suite_rom_id(void)
{
    md5_is("", "d41d8cd98f00b204e9800998ecf8427e");
    md5_is("abc", "900150983cd24fb0d6963f7d28e17f72");
    md5_is("The quick brown fox jumps over the lazy dog", "9e107d9d372bb6826bd81d3542a419d6");
    md5_is("12345678901234567890123456789012345678901234567890123456789012345678901234567890",
           "57edf4a22be3c955ac49da2e2107b67a");   /* 80 bytes: two blocks */

    static uint8_t zeros[0x10000];
    CHECK(!rom_id_is_apollo2(zeros, sizeof zeros));
    CHECK(!rom_id_is_apollo2(zeros, 100));
    CHECK(!rom_id_accept("zeros", zeros, sizeof zeros, false, stderr));
    CHECK(rom_id_accept("zeros", zeros, sizeof zeros, true, stderr));   /* --rom-any: a warning only */

    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    /* test_rom only returns the image when it passes the MD5 check, so check the pinned constant and the
       rejection of a one-byte change directly. */
    char hex[33];
    rom_id_md5_hex(rom, len, hex);
    CHECK_STR(hex, ROM_ID_MD5);
    CHECK(rom_id_is_apollo2(rom, len));
    CHECK(rom_id_accept("rom", rom, len, false, stderr));
    static uint8_t bad[0x10000];
    CHECK_EQ(len, sizeof bad);
    memcpy(bad, rom, sizeof bad);
    bad[0x1234] ^= 1;
    CHECK(!rom_id_is_apollo2(bad, sizeof bad));
    CHECK(!rom_id_accept("flipped", bad, sizeof bad, false, stderr));
    CHECK(rom_id_accept("flipped", bad, sizeof bad, true, stderr));
}
