/* The converted tables (M4.0 spec section 4.3): the encoder builds, covers exactly the converted ranges, and
   (with the ROM) matches the ROM byte for byte. */
#include <stdio.h>
#include "test.h"
#include "rom_fixture.h"
#include "cpu740/optable.h"
#include "firmware/data/fw_data.h"

typedef struct { uint32_t from, to; } range;   /* [from, to) in CPU addresses */
static const range k_rom[] = { { 0x4000, 0x8FF4 }, { 0x9800, 0xA397 }, { 0xA7BE, 0xA7D4 }, { 0xACC7, 0xACE8 },
                               { 0xADFE, 0xAFFE }, { 0xB473, 0xB77D }, { 0xB783, 0xB7A1 }, { 0xB7A7, 0xB7B1 },
                               { 0xBAEC, 0xBAF2 }, { 0xC561, 0xC571 }, { 0xC65B, 0xC675 }, { 0xC67B, 0xC9B8 },
                               { 0xD44A, 0xF77B }, { 0xFFD0, 0xFFD4 }, { 0xFFE0, 0xFFE8 }, { 0xFFEA, 0xFFEE },
                               { 0xFFF4, 0x10000 } };
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
        if (bit(img->rom_ok, a) != in(k_rom, sizeof k_rom / sizeof k_rom[0], a)) {
            fprintf(stderr, "  $%04X: converted %d, expected %d\n", a, bit(img->rom_ok, a), in(k_rom, sizeof k_rom / sizeof k_rom[0], a));
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
    CHECK_EQ(fw_phonetic_text_count, 5);
    CHECK_EQ(fw_cmd_imm_pattern_count, 2);
    CHECK_EQ(fw_cmd_pattern_count, 30);
    CHECK_EQ(fw_cmd_table_count, 31);   /* G# is listed twice */
    CHECK_EQ(fw_cmd_table[27], fw_cmd_table[28]);
    CHECK_EQ(fw_cmd_query_count, 18);
    CHECK_EQ(fw_cmd_case_fold_count, 21);
    unsigned ph_lists = 0, ph_triples = 0, ph_pauses = 0;
    for (int s = 0; s < 256; s++) {
        ph_lists += fw_ph_lists[s].count > 0;
        ph_triples += fw_ph_lists[s].count;
        ph_pauses += fw_ph_lists[s].word_pause;
    }
    CHECK_EQ(ph_lists, 67);
    CHECK_EQ(ph_triples, 101);
    CHECK_EQ(ph_pauses, 1);
    CHECK(fw_ph_lists[' '].word_pause);           /* the space's list is only the word-pause item */
    CHECK_EQ(fw_ph_lengthen_steps[0], 0x60);      /* $D44A: the RTS of ph_lengthen_count */
    CHECK_EQ(fw_cmd_speed_map_data[8][31], 1);    /* speed 9: every duration is 1 frame */

    /* The playback data (M4.4a spec 3) */
    unsigned bad_target = 0, bad_prop = 0, b_differs = 0;
    for (int ph = 0; ph < PAR_PHONEMES; ph++) {
        const fw_par_phoneme *a = &fw_par_voice_a[ph], *b = &fw_par_voice_b[ph];
        b_differs += a->rank != b->rank || a->spare != b->spare;
        for (int k = 0; k < 10; k++) {
            const fw_par_rule *ra = &a->p[k], *rb = &b->p[k];
            bad_target += ra->target < 1 || ra->target > 63 || rb->target < 1 || rb->target > 63;
            bad_prop += ra->prop > 8 || rb->prop > 8;
            b_differs += ra->prop != rb->prop || ra->ext_dur != rb->ext_dur || ra->int_dur != rb->int_dur;
            if (k >= 4)
                b_differs += ra->target != rb->target || ra->fixed != rb->fixed;
        }
    }
    CHECK_EQ(bad_target, 0);   /* every target is 1-63, below $80: par_read_segment's BMI at $BD85 is never taken */
    CHECK_EQ(bad_prop, 0);     /* every proportion is 0-8: par_ofs_weight = 8 - proportion never wraps */
    CHECK_EQ(b_differs, 0);    /* voice B differs only in the targets and fixed contributions of P1-P4 */
    CHECK_EQ(fw_par_voice_a[59].rank, 31);   /* the pause */
    CHECK_EQ(fw_par_target_bias[3], 26);     /* P4 F3 */
    CHECK_EQ(fw_frm_snapshot_map[0], FRAME_PITCH);
    CHECK_EQ(fw_par_slot_map[0], FRAME_FN);                  /* P1 */
    CHECK_EQ(fw_par_slot_map[9], FRAME_V);                   /* P10 */
    CHECK_EQ(fw_frm_template[FRAME_SOURCE], fw_frm_source_codes[0]);   /* the rest frame's b16: the default source */
    CHECK_EQ(fw_frm_template[FRAME_FILTER], fw_frm_filter_codes[0]);   /* and b17: the male filter */
    CHECK_EQ(fw_frm_template[FRAME_END_MARKER], 0xFF);
    CHECK_EQ(fw_frm_template[FRAME_END_MARKER + 1], 0xFF);
    unsigned sound_segments = 0, sound_longest = 0, sound_refs = 0;
    for (int x = 0; x < CMD_SOUND_EFFECTS; x++) {
        const fw_cmd_sound_effect *s = &fw_cmd_sound_effects[x];
        const fw_cmd_sound *t = &fw_cmd_sound_table[x];
        sound_segments += s->count;
        if (s->count > sound_longest)
            sound_longest = s->count;
        sound_refs += t->phoneme == s->phoneme && t->duration == s->duration && t->pitch == s->pitch &&
                      t->flags == fw_cmd_sound_flags;
    }
    CHECK_EQ(sound_segments, 120);             /* 18 flags + 120 * 3 + 17 ends = 395 bytes */
    CHECK_EQ(sound_longest, CMD_SOUND_MAX);    /* the shared flag array covers the longest effect */
    CHECK_EQ(sound_refs, CMD_SOUND_EFFECTS);   /* entry x: effect x's arrays and the shared flags */

    /* The bank code (M4.6c1 spec 3.5) */
    CHECK_EQ(fw_bank_ramcode_count, 130);      /* $0210-$0312 */
    CHECK_EQ(fw_bank_lts_code_count, 117);     /* $AF11-$AFFD */
    CHECK_EQ(fw_bank_entry_code_count, 2);     /* SEI / JMP boot_reentry_cold */
    CHECK_EQ(fw_opcode_count, 57);

    /* The boot data (M4.6c2 spec 2.4) */
    CHECK_EQ(strlen(fw_boot_selftest_ok_cmds), 6);      /* Ctrl-X, "@T5, " */
    CHECK_EQ(strlen(fw_boot_selftest_fail_cmds), 14);   /* Ctrl-X, "@T7 @T7 @T7, " */
    CHECK_EQ(fw_boot_selftest_ok_cmds[0], 0x18);
    CHECK_EQ(fw_boot_selftest_fail_cmds[0], 0x18);
    CHECK_EQ(fw_boot_prosody_defaults[0], fw_boot_prosody_defaults[5]);    /* '.' and ',' start on one baseline */
    CHECK(fw_boot_prosody_defaults[10] > fw_boot_prosody_defaults[0]);    /* '?' starts higher */
    CHECK_EQ(fw_boot_prosody_defaults[23], 0);                             /* X, which nothing reads */
    /* The vector table (M5a spec 3.4) */
    CHECK_EQ(fw_vec_int3_step, fw_vec_int0);    /* lines 3 and 0 share isr_unused, an RTI */
    CHECK_EQ(fw_vec_reset, 0xA61C);             /* boot_reset */
    CHECK(fw_vec_int4_timerx != fw_vec_int1_serial && fw_vec_int1_serial != fw_vec_int2_timers);
}

/* fw_opcodes against the emulator's 740 opcode table (src/cpu740/optable.c; M4.6c1 spec 3.5): each opcode decodes
   to its mnemonic and addressing mode, and a bit instruction's does for each of the 8 bits (opcode + $20 x bit). */
static void test_listing_opcodes(void)
{
    static const uint8_t k_mode[] = {
        [FWM_IMP] = AM_IMP, [FWM_ACC] = AM_ACC, [FWM_IMM] = AM_IMM, [FWM_ZP] = AM_ZP, [FWM_ZPX] = AM_ZPX,
        [FWM_ABS] = AM_ABS, [FWM_ABX] = AM_ABX, [FWM_ABY] = AM_ABY, [FWM_IZY] = AM_IZY, [FWM_REL] = AM_REL,
        [FWM_BIT_ZP] = AM_BIT_ZP, [FWM_BIT_A_REL] = AM_BIT_A_REL, [FWM_BIT_ZP_REL] = AM_BIT_ZP_REL,
    };
    const cpu740_opinfo *t = cpu740_optable();
    for (size_t k = 0; k < fw_opcode_count; k++) {
        const fw_opcode *o = &fw_opcodes[k];
        bool bit_op = o->mode == FWM_BIT_ZP || o->mode == FWM_BIT_A_REL || o->mode == FWM_BIT_ZP_REL;
        for (unsigned b = 0; b < (bit_op ? 8u : 1u); b++) {
            uint8_t op = (uint8_t)(o->opcode + 0x20 * b);
            CHECK_STR(cpu740_mnem_name(t[op].mnem), fw_mnem_names[o->mnem]);
            CHECK_EQ(t[op].mode, k_mode[o->mode]);
        }
        for (size_t j = 0; j < k; j++)
            CHECK(fw_opcodes[j].mnem != o->mnem || fw_opcodes[j].mode != o->mode);   /* each pair once */
    }
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
    CHECK_EQ(compared, 49104);   /* M4.0's 35,214, M4.1's phonetic texts (97), M4.2's command data (567),
                                    M4.3a's prosody data (2,319), M4.4a's playback data (9,495), M4.4b's
                                    par_slot_map (10), M4.5's frame builder data (794), M4.6a's ser_hex_chars
                                    (16), M4.6c1's bank code (532: $ADFE-$AFFD and 20 header bytes), M4.6c2's
                                    boot data (48: the self-test strings, 22, and boot_prosody_defaults, 26) and
                                    M5a's vector table (12) */
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
    test_listing_opcodes();
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM): converted tables not compared");
    test_against_rom(img, rom);
}
