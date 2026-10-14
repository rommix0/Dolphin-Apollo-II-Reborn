#include <stdbool.h>
#include "cpu740/optable.h"

static cpu740_opinfo g_table[256];
static bool g_built;

static const char *const k_names[M_COUNT] = {
    "???",
    "ADC", "AND", "ASL", "BCC", "BCS", "BEQ", "BIT", "BMI", "BNE", "BPL", "BRA", "BRK",
    "BVC", "BVS", "CLC", "CLD", "CLI", "CLV", "CMP", "CPX", "CPY", "DEC", "DEX", "DEY",
    "EOR", "INC", "INX", "INY", "JMP", "JSR", "LDA", "LDX", "LDY", "LSR", "NOP", "ORA",
    "PHA", "PHP", "PLA", "PLP", "ROL", "ROR", "RTI", "RTS", "SBC", "SEC", "SED", "SEI",
    "STA", "STX", "STY", "TAX", "TAY", "TSX", "TXA", "TXS", "TYA",
    "BBC", "BBS", "CLB", "SEB", "CLT", "SET", "COM", "LDM", "RRF", "STP", "TST", "WIT",
    "MUL", "DIV",
};

static void set(int op, cpu740_mnem mnem, cpu740_mode mode)
{
    g_table[op].mnem = (uint8_t)mnem;
    g_table[op].mode = (uint8_t)mode;
}

static void build(void)
{
    static const cpu740_mnem alu[8] = { M_ORA, M_AND, M_EOR, M_ADC, M_STA, M_LDA, M_CMP, M_SBC };
    static const struct { int off; cpu740_mode mode; } alu_modes[8] = {
        { 0x01, AM_IZX }, { 0x05, AM_ZP },  { 0x09, AM_IMM }, { 0x0D, AM_ABS },
        { 0x11, AM_IZY }, { 0x15, AM_ZPX }, { 0x19, AM_ABY }, { 0x1D, AM_ABX },
    };
    static const cpu740_mnem shifts[4] = { M_ASL, M_ROL, M_LSR, M_ROR };
    static const struct { int op; cpu740_mnem mnem; } branches[] = {
        { 0x10, M_BPL }, { 0x30, M_BMI }, { 0x50, M_BVC }, { 0x70, M_BVS }, { 0x80, M_BRA },
        { 0x90, M_BCC }, { 0xB0, M_BCS }, { 0xD0, M_BNE }, { 0xF0, M_BEQ },
    };
    static const struct { int op; cpu740_mnem mnem; } implied[] = {
        { 0x00, M_BRK }, { 0x08, M_PHP }, { 0x18, M_CLC }, { 0x28, M_PLP }, { 0x38, M_SEC },
        { 0x40, M_RTI }, { 0x48, M_PHA }, { 0x58, M_CLI }, { 0x60, M_RTS }, { 0x68, M_PLA },
        { 0x78, M_SEI }, { 0x88, M_DEY }, { 0x8A, M_TXA }, { 0x98, M_TYA }, { 0x9A, M_TXS },
        { 0xA8, M_TAY }, { 0xAA, M_TAX }, { 0xB8, M_CLV }, { 0xBA, M_TSX }, { 0xC8, M_INY },
        { 0xCA, M_DEX }, { 0xD8, M_CLD }, { 0xE8, M_INX }, { 0xEA, M_NOP }, { 0xF8, M_SED },
        { 0x12, M_CLT }, { 0x32, M_SET }, { 0x42, M_STP }, { 0xC2, M_WIT },
    };
    int i, j;

    for (i = 0; i < 8; i++)
        for (j = 0; j < 8; j++)
            if (!(alu[i] == M_STA && alu_modes[j].mode == AM_IMM))
                set(i * 0x20 + alu_modes[j].off, alu[i], alu_modes[j].mode);
    for (i = 0; i < 4; i++) {
        int b = i * 0x20;
        set(b + 0x06, shifts[i], AM_ZP);
        set(b + 0x0A, shifts[i], AM_ACC);
        set(b + 0x0E, shifts[i], AM_ABS);
        set(b + 0x16, shifts[i], AM_ZPX);
        set(b + 0x1E, shifts[i], AM_ABX);
    }
    set(0xE6, M_INC, AM_ZP); set(0xEE, M_INC, AM_ABS); set(0xF6, M_INC, AM_ZPX); set(0xFE, M_INC, AM_ABX);
    set(0xC6, M_DEC, AM_ZP); set(0xCE, M_DEC, AM_ABS); set(0xD6, M_DEC, AM_ZPX); set(0xDE, M_DEC, AM_ABX);
    set(0xA2, M_LDX, AM_IMM); set(0xA6, M_LDX, AM_ZP); set(0xAE, M_LDX, AM_ABS);
    set(0xB6, M_LDX, AM_ZPY); set(0xBE, M_LDX, AM_ABY);
    set(0xA0, M_LDY, AM_IMM); set(0xA4, M_LDY, AM_ZP); set(0xAC, M_LDY, AM_ABS);
    set(0xB4, M_LDY, AM_ZPX); set(0xBC, M_LDY, AM_ABX);
    set(0x86, M_STX, AM_ZP); set(0x8E, M_STX, AM_ABS); set(0x96, M_STX, AM_ZPY);
    set(0x84, M_STY, AM_ZP); set(0x8C, M_STY, AM_ABS); set(0x94, M_STY, AM_ZPX);
    set(0xE0, M_CPX, AM_IMM); set(0xE4, M_CPX, AM_ZP); set(0xEC, M_CPX, AM_ABS);
    set(0xC0, M_CPY, AM_IMM); set(0xC4, M_CPY, AM_ZP); set(0xCC, M_CPY, AM_ABS);
    set(0x24, M_BIT, AM_ZP); set(0x2C, M_BIT, AM_ABS);
    set(0x4C, M_JMP, AM_ABS); set(0x6C, M_JMP, AM_IND); set(0x20, M_JSR, AM_ABS);
    for (i = 0; i < (int)(sizeof branches / sizeof branches[0]); i++)
        set(branches[i].op, branches[i].mnem, AM_REL);
    for (i = 0; i < (int)(sizeof implied / sizeof implied[0]); i++)
        set(implied[i].op, implied[i].mnem, AM_IMP);

    /* 740 extensions */
    set(0x02, M_JSR, AM_ZPI); set(0xB2, M_JMP, AM_ZPI); set(0x22, M_JSR, AM_SPG);
    set(0x3C, M_LDM, AM_LDM); set(0x44, M_COM, AM_ZP); set(0x64, M_TST, AM_ZP); set(0x82, M_RRF, AM_ZP);
    set(0x1A, M_DEC, AM_ACC); set(0x3A, M_INC, AM_ACC);
    set(0x62, M_MUL, AM_ZPX); set(0xE2, M_DIV, AM_ZPX);
    for (i = 0; i < 8; i++) {
        set(0x03 + 0x20 * i, M_BBS, AM_BIT_A_REL);
        set(0x13 + 0x20 * i, M_BBC, AM_BIT_A_REL);
        set(0x07 + 0x20 * i, M_BBS, AM_BIT_ZP_REL);
        set(0x17 + 0x20 * i, M_BBC, AM_BIT_ZP_REL);
        set(0x0B + 0x20 * i, M_SEB, AM_BIT_A);
        set(0x1B + 0x20 * i, M_CLB, AM_BIT_A);
        set(0x0F + 0x20 * i, M_SEB, AM_BIT_ZP);
        set(0x1F + 0x20 * i, M_CLB, AM_BIT_ZP);
    }
}

const cpu740_opinfo *cpu740_optable(void)
{
    if (!g_built) {
        build();
        g_built = true;
    }
    return g_table;
}

int cpu740_mode_length(int mode)
{
    switch (mode) {
    case AM_IMP: case AM_ACC: case AM_BIT_A:
        return 1;
    case AM_ABS: case AM_ABX: case AM_ABY: case AM_IND: case AM_LDM: case AM_BIT_ZP_REL:
        return 3;
    default:
        return 2;
    }
}

const char *cpu740_mnem_name(int mnem)
{
    return (mnem >= 0 && mnem < M_COUNT) ? k_names[mnem] : "???";
}
