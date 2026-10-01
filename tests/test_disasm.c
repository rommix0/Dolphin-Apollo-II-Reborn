#include "test.h"
#include "cpu740/cpu740.h"

static uint8_t g_img[0x10000];

static uint8_t peek(void *ctx, uint16_t a)
{
    (void)ctx;
    return g_img[a];
}

static void expect(uint16_t pc, const uint8_t *bytes, size_t n, const char *want, int want_len)
{
    char text[48];
    memset(g_img, 0, sizeof g_img);
    memcpy(g_img + pc, bytes, n);
    int len = cpu740_disasm(pc, peek, NULL, text, sizeof text);
    CHECK_STR(text, want);
    CHECK_EQ(len, want_len);
}

void suite_disasm(void)
{
    /* Encodings taken from the Apollo ROM listing */
    expect(0xA61C, (const uint8_t[]){ 0x3C, 0xF9, 0xEF }, 3, "LDM #$F9,$EF", 3);
    expect(0xA629, (const uint8_t[]){ 0x12 }, 1, "CLT", 1);
    expect(0xA657, (const uint8_t[]){ 0x07, 0x10, 0x08 }, 3, "BBS0 $10,$A662", 3);
    expect(0xB2E5, (const uint8_t[]){ 0xB1, 0x2A }, 2, "LDA ($2A),Y", 2);
    expect(0xB2E7, (const uint8_t[]){ 0x8D, 0x00, 0x80 }, 3, "STA $8000", 3);
    expect(0xBB9D, (const uint8_t[]){ 0x6C, 0x04, 0x12 }, 3, "JMP ($1204)", 3);
    expect(0xC46A, (const uint8_t[]){ 0xB2, 0x4A }, 2, "JMP ($4A)", 2);
    expect(0x0313, (const uint8_t[]){ 0x02, 0x4C }, 2, "JSR ($4C)", 2);
    expect(0x8000, (const uint8_t[]){ 0x22, 0x10 }, 2, "JSR \\$FF10", 2);
    expect(0x8000, (const uint8_t[]){ 0xE3, 0x01 }, 2, "BBS7 A,$8003", 2);
    expect(0x8000, (const uint8_t[]){ 0x6B }, 1, "SEB3 A", 1);
    expect(0xB2D9, (const uint8_t[]){ 0xBF, 0xFE }, 2, "CLB5 $FE", 2);
    expect(0x8000, (const uint8_t[]){ 0x0A }, 1, "ASL A", 1);
    expect(0x8000, (const uint8_t[]){ 0x3A }, 1, "INC A", 1);
    expect(0x8000, (const uint8_t[]){ 0xB6, 0x10 }, 2, "LDX $10,Y", 2);
    expect(0x8000, (const uint8_t[]){ 0xA1, 0x10 }, 2, "LDA ($10,X)", 2);
    expect(0xA60E, (const uint8_t[]){ 0x9D, 0x00, 0x02 }, 3, "STA $0200,X", 3);
    expect(0xA63C, (const uint8_t[]){ 0xD0, 0xFB }, 2, "BNE $A639", 2);
    expect(0x8000, (const uint8_t[]){ 0x89 }, 1, "DB $89", 1);
}
