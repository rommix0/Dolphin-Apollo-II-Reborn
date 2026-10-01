#include "test.h"
#include "cpu_harness.h"

static void test_reset(void)
{
    static const uint8_t code[] = { 0xEA };
    load(0x8000, code, sizeof code);
    CHECK_EQ(g_cpu.pc, 0x8000);
    CHECK_EQ(g_cpu.s, 0xFF);
    CHECK(g_cpu.p & CPU740_I);
    CHECK_EQ(g_cpu.fault, CPU740_OK);
}

static void test_lda_flags(void)
{
    static const uint8_t code[] = { 0xA9, 0x00, 0xA9, 0x80 };   /* LDA #$00; LDA #$80 */
    load(0x8000, code, sizeof code);
    run(1); CHECK_EQ(g_cpu.a, 0x00); CHECK(g_cpu.p & CPU740_Z); CHECK(!(g_cpu.p & CPU740_N));
    run(1); CHECK_EQ(g_cpu.a, 0x80); CHECK(!(g_cpu.p & CPU740_Z)); CHECK(g_cpu.p & CPU740_N);
}

static void test_adc_sbc(void)
{
    static const uint8_t code[] = {
        0x18, 0xA9, 0x50, 0x69, 0x50,   /* CLC; LDA #$50; ADC #$50 -> $A0, V N, no C */
        0x18, 0xA9, 0xFF, 0x69, 0x01,   /* CLC; LDA #$FF; ADC #$01 -> $00, C Z */
        0x38, 0xA9, 0x05, 0xE9, 0x06,   /* SEC; LDA #$05; SBC #$06 -> $FF, borrow (C clear), N */
    };
    load(0x8000, code, sizeof code);
    run(3); CHECK_EQ(g_cpu.a, 0xA0); CHECK(g_cpu.p & CPU740_V); CHECK(g_cpu.p & CPU740_N); CHECK(!(g_cpu.p & CPU740_C));
    run(3); CHECK_EQ(g_cpu.a, 0x00); CHECK(g_cpu.p & CPU740_C); CHECK(g_cpu.p & CPU740_Z);
    run(3); CHECK_EQ(g_cpu.a, 0xFF); CHECK(!(g_cpu.p & CPU740_C)); CHECK(g_cpu.p & CPU740_N);
}

static void test_compare(void)
{
    static const uint8_t code[] = { 0xA9, 0x05, 0xC9, 0x05, 0xC9, 0x06 };  /* LDA #5; CMP #5; CMP #6 */
    load(0x8000, code, sizeof code);
    run(2); CHECK(g_cpu.p & CPU740_Z); CHECK(g_cpu.p & CPU740_C);
    run(1); CHECK(!(g_cpu.p & CPU740_Z)); CHECK(!(g_cpu.p & CPU740_C)); CHECK(g_cpu.p & CPU740_N);
}

static void test_indirect_y(void)
{
    static const uint8_t code[] = { 0xA0, 0x02, 0xB1, 0x10 };   /* LDY #2; LDA ($10),Y */
    load(0x8000, code, sizeof code);
    g_mem.mem[0x10] = 0x00; g_mem.mem[0x11] = 0x30; g_mem.mem[0x3002] = 0x5A;
    run(2); CHECK_EQ(g_cpu.a, 0x5A);
}

static void test_jsr_rts(void)
{
    static const uint8_t code[] = { 0x20, 0x10, 0x80, 0xEA };   /* $8000 JSR $8010; $8003 NOP */
    load(0x8000, code, sizeof code);
    g_mem.mem[0x8010] = 0x60;                                   /* RTS */
    run(1);
    CHECK_EQ(g_cpu.pc, 0x8010);
    CHECK_EQ(g_mem.mem[0x01FF], 0x80);                          /* return address - 1, high first */
    CHECK_EQ(g_mem.mem[0x01FE], 0x02);
    run(1);
    CHECK_EQ(g_cpu.pc, 0x8003);
    CHECK_EQ(g_cpu.s, 0xFF);
}

static void test_branches(void)
{
    /* $8000 LDA #0; $8002 BEQ +2 -> $8006; $8006 BNE -6 not taken -> $8008 */
    static const uint8_t code[] = { 0xA9, 0x00, 0xF0, 0x02, 0xEA, 0xEA, 0xD0, 0xFA };
    load(0x8000, code, sizeof code);
    run(2); CHECK_EQ(g_cpu.pc, 0x8006);
    run(1); CHECK_EQ(g_cpu.pc, 0x8008);
}

static void test_backward_loop(void)
{
    static const uint8_t code[] = { 0xA2, 0x03, 0xCA, 0xD0, 0xFD };  /* LDX #3; loop: DEX; BNE loop */
    load(0x8000, code, sizeof code);
    run(7);
    CHECK_EQ(g_cpu.x, 0);
    CHECK_EQ(g_cpu.pc, 0x8005);
}

static void test_jmp_indirect_page_wrap(void)
{
    static const uint8_t code[] = { 0x6C, 0xFF, 0x30 };         /* JMP ($30FF) */
    load(0x8000, code, sizeof code);
    g_mem.mem[0x30FF] = 0x34; g_mem.mem[0x3000] = 0x12; g_mem.mem[0x3100] = 0x99;
    run(1);
    CHECK_EQ(g_cpu.pc, 0x1234);
}

static void test_shifts(void)
{
    /* SEC; LDA #$81; ROL A -> $03 C; ROR A -> $81 C; ASL A -> $02 C; LSR A -> $01 no C */
    static const uint8_t code[] = { 0x38, 0xA9, 0x81, 0x2A, 0x6A, 0x0A, 0x4A };
    load(0x8000, code, sizeof code);
    run(3); CHECK_EQ(g_cpu.a, 0x03); CHECK(g_cpu.p & CPU740_C);
    run(1); CHECK_EQ(g_cpu.a, 0x81); CHECK(g_cpu.p & CPU740_C);
    run(1); CHECK_EQ(g_cpu.a, 0x02); CHECK(g_cpu.p & CPU740_C);
    run(1); CHECK_EQ(g_cpu.a, 0x01); CHECK(!(g_cpu.p & CPU740_C));
}

static void test_inc_dec_memory(void)
{
    static const uint8_t code[] = { 0xE6, 0x10, 0xC6, 0x11 };   /* INC $10; DEC $11 */
    load(0x8000, code, sizeof code);
    g_mem.mem[0x10] = 0xFF; g_mem.mem[0x11] = 0x01;
    run(1); CHECK_EQ(g_mem.mem[0x10], 0x00); CHECK(g_cpu.p & CPU740_Z);
    run(1); CHECK_EQ(g_mem.mem[0x11], 0x00); CHECK(g_cpu.p & CPU740_Z);
}

static void test_php_plp(void)
{
    static const uint8_t code[] = { 0x38, 0x08, 0x18, 0x28 };   /* SEC; PHP; CLC; PLP */
    load(0x8000, code, sizeof code);
    run(4);
    CHECK(g_cpu.p & CPU740_C);
    CHECK(g_cpu.p & CPU740_B);
    CHECK_EQ(g_cpu.s, 0xFF);
}

static void test_irq_entry_and_rti(void)
{
    static const uint8_t code[] = { 0x58, 0xEA, 0xEA };          /* CLI; NOP; NOP */
    load(0x8000, code, sizeof code);
    g_mem.mem[0xFFF8] = 0x00; g_mem.mem[0xFFF9] = 0x90;          /* line 2 vector -> $9000 */
    g_mem.mem[0x9000] = 0x40;                                    /* RTI */
    run(1);
    cpu740_set_irq(&g_cpu, 2, true);
    CHECK_EQ(run(1), 7);
    CHECK_EQ(g_cpu.pc, 0x9000);
    CHECK(g_cpu.p & CPU740_I);
    CHECK_EQ(g_mem.mem[0x01FF], 0x80);
    CHECK_EQ(g_mem.mem[0x01FE], 0x01);
    CHECK(!(g_mem.mem[0x01FD] & CPU740_B));                      /* hardware IRQ pushes B clear */
    cpu740_set_irq(&g_cpu, 2, false);
    run(1);
    CHECK_EQ(g_cpu.pc, 0x8001);
    CHECK(!(g_cpu.p & CPU740_I));
}

static void test_irq_priority_and_mask(void)
{
    static const uint8_t code[] = { 0xEA, 0x58, 0xEA };          /* NOP (I set by reset); CLI; NOP */
    load(0x8000, code, sizeof code);
    g_mem.mem[0xFFFA] = 0x00; g_mem.mem[0xFFFB] = 0xA0;          /* line 1 -> $A000 */
    g_mem.mem[0xFFF8] = 0x00; g_mem.mem[0xFFF9] = 0x90;          /* line 2 -> $9000 */
    cpu740_set_irq(&g_cpu, 1, true);
    cpu740_set_irq(&g_cpu, 2, true);
    run(1); CHECK_EQ(g_cpu.pc, 0x8001);                         /* masked: the NOP ran */
    run(1);                                                      /* CLI */
    run(1); CHECK_EQ(g_cpu.pc, 0xA000);                         /* lower line number wins */
}

static void test_faults(void)
{
    static const uint8_t illegal[] = { 0x89 };
    static const uint8_t sed[] = { 0xF8 };
    static const uint8_t brk[] = { 0x00 };
    load(0x8000, illegal, sizeof illegal);
    CHECK_EQ(run(1), 0);
    CHECK_EQ(g_cpu.fault, CPU740_FAULT_ILLEGAL);
    CHECK_EQ(g_cpu.fault_pc, 0x8000);
    CHECK_EQ(g_cpu.fault_opcode, 0x89);
    CHECK_EQ(g_cpu.pc, 0x8000);
    CHECK_EQ(run(1), 0);                                         /* stays faulted */
    load(0x8000, sed, sizeof sed);
    run(1); CHECK_EQ(g_cpu.fault, CPU740_FAULT_UNSUPPORTED);
    load(0x8000, brk, sizeof brk);
    run(1); CHECK_EQ(g_cpu.fault, CPU740_FAULT_UNSUPPORTED);
}

void suite_cpu_base(void)
{
    test_reset();
    test_lda_flags();
    test_adc_sbc();
    test_compare();
    test_indirect_y();
    test_jsr_rts();
    test_branches();
    test_backward_loop();
    test_jmp_indirect_page_wrap();
    test_shifts();
    test_inc_dec_memory();
    test_php_plp();
    test_irq_entry_and_rti();
    test_irq_priority_and_mask();
    test_faults();
}
