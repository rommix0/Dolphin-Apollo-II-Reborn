/* The converted tables (M4.0 spec section 4.3): the encoder builds, covers exactly the converted ranges, and
   (with the ROM) matches the ROM byte for byte. */
#include <stdio.h>
#include "test.h"
#include "rom_fixture.h"
#include "firmware/data/fw_data.h"

typedef struct { uint32_t from, to; } range;   /* [from, to) in CPU addresses */
static const range k_rom[] = { { 0x4000, 0x8FF4 }, { 0x9824, 0x989C }, { 0x9AB4, 0x9BB4 } };
static const range k_low[] = { { 0x4010, 0x7832 } };

static bool bit(const uint8_t *ok, uint32_t i) { return (ok[i >> 3] >> (i & 7)) & 1; }

static bool in(const range *r, size_t n, uint32_t a)
{
    for (size_t i = 0; i < n; i++)
        if (a >= r[i].from && a < r[i].to)
            return true;
    return false;
}

static void test_coverage(const fw_image *img)
{
    for (uint32_t a = 0; a < 0x10000; a++) {
        if (bit(img->rom_ok, a) != in(k_rom, 3, a)) {
            fprintf(stderr, "  $%04X: converted %d, expected %d\n", a, bit(img->rom_ok, a), in(k_rom, 3, a));
            g_test_failures++;
            break;
        }
    }
    for (uint32_t a = 0x4000; a < 0x8000; a++) {
        if (bit(img->low_ok, a - 0x4000) != in(k_low, 1, a)) {
            fprintf(stderr, "  LOW $%04X: converted %d, expected %d\n", a, bit(img->low_ok, a - 0x4000),
                    in(k_low, 1, a));
            g_test_failures++;
            break;
        }
    }
}

static void test_counts(void)
{
    unsigned lists = 0, rules = 0;
    for (size_t t = 0; t < fw_rule_table_count; t++) {
        lists += fw_rule_tables[t]->count;
        for (unsigned i = 0; i < fw_rule_tables[t]->count; i++)
            rules += fw_rule_tables[t]->lists[i]->count;
    }
    CHECK_EQ(fw_rule_table_count, 35);
    CHECK_EQ(lists, 531);
    CHECK_EQ(rules, 3766);
    CHECK_EQ(fw_affix_list_count, 3);
}

static void test_against_rom(const fw_image *img, const uint8_t *rom)
{
    unsigned bad = 0, compared = 0;
    for (uint32_t a = 0x4000; a < 0x10000; a++) {
        if (!bit(img->rom_ok, a))
            continue;
        compared++;
        if (img->rom[a] != rom[a] && bad++ < 10)
            fprintf(stderr, "  $%04X: converted %02X, ROM %02X\n", a, img->rom[a], rom[a]);
    }
    for (uint32_t a = 0x4000; a < 0x8000; a++) {
        if (!bit(img->low_ok, a - 0x4000))
            continue;
        compared++;
        if (img->low[a - 0x4000] != rom[a - 0x4000] && bad++ < 10)
            fprintf(stderr, "  LOW $%04X: converted %02X, ROM %02X\n", a, img->low[a - 0x4000], rom[a - 0x4000]);
    }
    printf("  %u converted bytes compared with the ROM, %u differ\n", compared, bad);
    CHECK_EQ(bad, 0);
}

void suite_firmware_data(void)
{
    const fw_image *img = fw_image_get();
    CHECK(img != NULL);
    if (!img)
        return;
    test_coverage(img);
    test_counts();
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM): converted tables not compared");
    test_against_rom(img, rom);
}
