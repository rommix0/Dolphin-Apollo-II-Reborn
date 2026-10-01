#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "engine/rom_engine.h"

static machine g_m;

static void print_frames(const frame_list *fl)
{
    char *hex = frame_list_hex(fl);
    fputs(hex, stderr);
    free(hex);
}

/* "zoo" needs the K-Z rules, which live in file 0x0000-0x3FFF behind P2.7 (spec §1.2). */
static void test_zoo_uses_lower_rom_and_sounds_like_z(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    frame_list fl = { 0 };
    machine_status st = rom_engine_speak(&g_m, rom, len, "zoo", false, &fl);
    if (st != MACHINE_IDLE)
        machine_dump(&g_m, stderr);
    CHECK_EQ(st, MACHINE_IDLE);
    print_unmodelled(&g_m);
    CHECK_EQ(g_m.unmodelled_count, 0);
    CHECK(fl.count > 10);
    CHECK(g_m.lower_window_reads > 0);
    /* Phoneme Z targets V (b9) = 36 and AHF (b8) = 56: some frame must be close to that */
    bool z_like = false;
    for (size_t i = 0; i < fl.count; i++)
        if (fl.frames[i][9] >= 28 && fl.frames[i][9] <= 44 && fl.frames[i][8] >= 45)
            z_like = true;
    CHECK(z_like);
    if (!z_like)
        print_frames(&fl);
    printf("  zoo: %zu frames, %llu lower-window reads\n", fl.count,
           (unsigned long long)g_m.lower_window_reads);
    frame_list_free(&fl);
}

static void test_deterministic(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    frame_list a = { 0 }, b = { 0 };
    CHECK_EQ(rom_engine_speak(&g_m, rom, len, "hello world", false, &a), MACHINE_IDLE);
    print_unmodelled(&g_m);
    CHECK_EQ(g_m.unmodelled_count, 0);
    CHECK_EQ(rom_engine_speak(&g_m, rom, len, "hello world", false, &b), MACHINE_IDLE);
    print_unmodelled(&g_m);
    CHECK_EQ(g_m.unmodelled_count, 0);
    CHECK(a.count > 0);
    CHECK_EQ(a.count, b.count);
    if (a.count == b.count)
        CHECK(memcmp(a.frames, b.frames, a.count * APOLLO_FRAME_BYTES) == 0);
    frame_list_free(&a);
    frame_list_free(&b);
}

static void test_hex_format(void)
{
    uint8_t frames[2][APOLLO_FRAME_BYTES] = { { 0x01, 0xAB }, { 0xFF } };
    frame_list fl;
    fl.frames = frames;
    fl.count = 2;
    fl.cap = 2;
    char *hex = frame_list_hex(&fl);
    CHECK_STR(hex, "01AB000000000000000000000000000000000000\n"
                   "FF00000000000000000000000000000000000000\n");
    free(hex);
}

void suite_rom_speak(void)
{
    test_hex_format();
    test_zoo_uses_lower_rom_and_sounds_like_z();
    test_deterministic();
}
