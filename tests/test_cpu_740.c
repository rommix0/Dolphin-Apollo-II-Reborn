#include "test.h"
#include "cpu_harness.h"

static void test_bbs_bbc_zp(void)
{
    static const uint8_t code[] = {
        0x07, 0x10, 0x02,   /* $8000 BBS0 $10,$8005 */
        0xEA, 0xEA,
        0x37, 0x10, 0x02,   /* $8005 BBC1 $10,$800A */
        0xEA, 0xEA, 0xEA,
    };
    load(0x8000, code, sizeof code);
    g_mem.mem[0x10] = 0x01;
    run(1); CHECK_EQ(g_cpu.pc, 0x8005);   /* bit 0 set: taken */
    run(1); CHECK_EQ(g_cpu.pc, 0x800A);   /* bit 1 clear: taken */
    load(0x8000, code, sizeof code);
    g_mem.mem[0x10] = 0x02;
    run(1); CHECK_EQ(g_cpu.pc, 0x8003);   /* bit 0 clear: falls through */
}

static void test_bbs_bbc_acc(void)
{
    /* LDA #$80; $8002 BBS7 A,$8005; $8005 BBC7 A,$8008 (not taken -> $8007) */
    static const uint8_t code[] = { 0xA9, 0x80, 0xE3, 0x01, 0xEA, 0xF3, 0x01, 0xEA, 0xEA };
    load(0x8000, code, sizeof code);
    run(2); CHECK_EQ(g_cpu.pc, 0x8005);
    run(1); CHECK_EQ(g_cpu.pc, 0x8007);
}

static void test_seb_clb(void)
{
    /* SEB7 $20; CLB0 $21; LDA #0; SEB3 A; CLB3 A */
    static const uint8_t code[] = { 0xEF, 0x20, 0x1F, 0x21, 0xA9, 0x00, 0x6B, 0x7B };
    load(0x8000, code, sizeof code);
    g_mem.mem[0x21] = 0xFF;
    run(1); CHECK_EQ(g_mem.mem[0x20], 0x80);
    run(1); CHECK_EQ(g_mem.mem[0x21], 0xFE);
    run(2); CHECK_EQ(g_cpu.a, 0x08);
    run(1); CHECK_EQ(g_cpu.a, 0x00);
}

static void test_ldm_tst_com_rrf(void)
{
    /* LDM #$F9,$EF; TST $EF; COM $EF; RRF $EF */
    static const uint8_t code[] = { 0x3C, 0xF9, 0xEF, 0x64, 0xEF, 0x44, 0xEF, 0x82, 0xEF };
    load(0x8000, code, sizeof code);
    run(1); CHECK_EQ(g_mem.mem[0xEF], 0xF9);
    run(1); CHECK(g_cpu.p & CPU740_N); CHECK(!(g_cpu.p & CPU740_Z));
    run(1); CHECK_EQ(g_mem.mem[0xEF], 0x06); CHECK(!(g_cpu.p & CPU740_N));
    run(1); CHECK_EQ(g_mem.mem[0xEF], 0x60);
}

static void test_inc_dec_acc(void)
{
    static const uint8_t code[] = { 0xA9, 0xFF, 0x3A, 0x1A };   /* LDA #$FF; INC A; DEC A */
    load(0x8000, code, sizeof code);
    run(2); CHECK_EQ(g_cpu.a, 0x00); CHECK(g_cpu.p & CPU740_Z);
    run(1); CHECK_EQ(g_cpu.a, 0xFF); CHECK(g_cpu.p & CPU740_N);
}

static void test_jumps(void)
{
    static const uint8_t jsr_zpi[] = { 0x02, 0x4C, 0xEA };      /* $8000 JSR ($4C); $8002 NOP */
    static const uint8_t jsr_spg[] = { 0x22, 0x10, 0xEA };      /* $8000 JSR \$FF10 */
    static const uint8_t jmp_zpi[] = { 0xB2, 0x4A };            /* JMP ($4A) */
    static const uint8_t bra[] = { 0x80, 0xFE };                /* BRA * */

    load(0x8000, jsr_zpi, sizeof jsr_zpi);
    g_mem.mem[0x4C] = 0x00; g_mem.mem[0x4D] = 0x90; g_mem.mem[0x9000] = 0x60;
    run(1); CHECK_EQ(g_cpu.pc, 0x9000);
    run(1); CHECK_EQ(g_cpu.pc, 0x8002);

    load(0x8000, jsr_spg, sizeof jsr_spg);
    g_mem.mem[0xFF10] = 0x60;
    run(1); CHECK_EQ(g_cpu.pc, 0xFF10);
    run(1); CHECK_EQ(g_cpu.pc, 0x8002);

    load(0x8000, jmp_zpi, sizeof jmp_zpi);
    g_mem.mem[0x4A] = 0x34; g_mem.mem[0x4B] = 0x12;
    run(1); CHECK_EQ(g_cpu.pc, 0x1234);

    load(0x8000, bra, sizeof bra);
    run(3); CHECK_EQ(g_cpu.pc, 0x8000);
}

static void test_t_flag(void)
{
    static const uint8_t clt[] = { 0x12 };
    static const uint8_t set[] = { 0x32 };
    static const uint8_t plp_t[] = { 0xA9, 0x20, 0x48, 0x28 };  /* LDA #$20; PHA; PLP */
    load(0x8000, clt, sizeof clt);
    g_cpu.p |= CPU740_T;
    run(1); CHECK(!(g_cpu.p & CPU740_T)); CHECK_EQ(g_cpu.fault, CPU740_OK);
    load(0x8000, set, sizeof set);
    run(1); CHECK_EQ(g_cpu.fault, CPU740_FAULT_UNSUPPORTED);
    load(0x8000, plp_t, sizeof plp_t);
    run(3); CHECK_EQ(g_cpu.fault, CPU740_FAULT_TMODE);
}

static void test_wit(void)
{
    static const uint8_t code[] = { 0x58, 0xC2, 0xEA };          /* CLI; WIT; NOP */
    load(0x8000, code, sizeof code);
    g_mem.mem[0xFFF8] = 0x00; g_mem.mem[0xFFF9] = 0x90; g_mem.mem[0x9000] = 0x40;
    run(2); CHECK(g_cpu.waiting); CHECK_EQ(g_cpu.pc, 0x8002);
    run(5); CHECK(g_cpu.waiting); CHECK_EQ(g_cpu.pc, 0x8002);   /* halted with no IRQ */
    cpu740_set_irq(&g_cpu, 2, true);
    run(1); CHECK(!g_cpu.waiting); CHECK_EQ(g_cpu.pc, 0x9000);
}

static void test_unsupported(void)
{
    static const uint8_t stp[] = { 0x42 };
    static const uint8_t mul[] = { 0x62, 0x10 };
    static const uint8_t div[] = { 0xE2, 0x10 };
    load(0x8000, stp, sizeof stp); run(1); CHECK_EQ(g_cpu.fault, CPU740_FAULT_UNSUPPORTED);
    load(0x8000, mul, sizeof mul); run(1); CHECK_EQ(g_cpu.fault, CPU740_FAULT_UNSUPPORTED);
    load(0x8000, div, sizeof div); run(1); CHECK_EQ(g_cpu.fault, CPU740_FAULT_UNSUPPORTED);
}

static void test_fault_paths(void)
{
    /* RTI restoring T=1 faults */
    static const uint8_t rti[] = { 0x40 };
    load(0x8000, rti, sizeof rti);
    g_cpu.s = 0xFC;
    g_mem.mem[0x01FD] = 0x20;  /* P with T set */
    g_mem.mem[0x01FE] = 0x00;  /* return address low */
    g_mem.mem[0x01FF] = 0x90;  /* return address high */
    run(1);
    CHECK_EQ(g_cpu.fault, CPU740_FAULT_TMODE);
    CHECK_EQ(g_cpu.fault_pc, 0x8000);

    /* Decimal mode reaching ADC faults */
    static const uint8_t adc_decimal[] = { 0xA9, 0x08, 0x48, 0x28, 0x69, 0x01 };  /* LDA #$08; PHA; PLP; ADC #$01 */
    load(0x8000, adc_decimal, sizeof adc_decimal);
    run(3);
    CHECK_EQ(g_cpu.fault, CPU740_OK);
    CHECK(g_cpu.p & CPU740_D);
    run(1);
    CHECK_EQ(g_cpu.fault, CPU740_FAULT_UNSUPPORTED);
    CHECK_EQ(g_cpu.fault_pc, 0x8004);
}

void suite_cpu_740(void)
{
    test_bbs_bbc_zp();
    test_bbs_bbc_acc();
    test_seb_clb();
    test_ldm_tst_com_rrf();
    test_inc_dec_acc();
    test_jumps();
    test_t_flag();
    test_wit();
    test_unsupported();
    test_fault_paths();
}
