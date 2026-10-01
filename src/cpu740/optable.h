#ifndef CPU740_OPTABLE_H
#define CPU740_OPTABLE_H
#include <stdint.h>

typedef enum {
    AM_IMP, AM_ACC, AM_IMM, AM_ZP, AM_ZPX, AM_ZPY, AM_ABS, AM_ABX, AM_ABY,
    AM_IND, AM_IZX, AM_IZY, AM_REL,
    AM_ZPI,         /* JMP/JSR ($zz) */
    AM_SPG,         /* JSR \$FFzz (special page) */
    AM_LDM,         /* LDM #imm,$zz */
    AM_BIT_A,       /* SEB/CLB n,A */
    AM_BIT_ZP,      /* SEB/CLB n,$zz */
    AM_BIT_A_REL,   /* BBS/BBC n,A,rel */
    AM_BIT_ZP_REL,  /* BBS/BBC n,$zz,rel */
    AM_COUNT
} cpu740_mode;

typedef enum {
    M_ILL,
    M_ADC, M_AND, M_ASL, M_BCC, M_BCS, M_BEQ, M_BIT, M_BMI, M_BNE, M_BPL, M_BRA, M_BRK,
    M_BVC, M_BVS, M_CLC, M_CLD, M_CLI, M_CLV, M_CMP, M_CPX, M_CPY, M_DEC, M_DEX, M_DEY,
    M_EOR, M_INC, M_INX, M_INY, M_JMP, M_JSR, M_LDA, M_LDX, M_LDY, M_LSR, M_NOP, M_ORA,
    M_PHA, M_PHP, M_PLA, M_PLP, M_ROL, M_ROR, M_RTI, M_RTS, M_SBC, M_SEC, M_SED, M_SEI,
    M_STA, M_STX, M_STY, M_TAX, M_TAY, M_TSX, M_TXA, M_TXS, M_TYA,
    /* 740 extensions */
    M_BBC, M_BBS, M_CLB, M_SEB, M_CLT, M_SET, M_COM, M_LDM, M_RRF, M_STP, M_TST, M_WIT,
    M_MUL, M_DIV,
    M_COUNT
} cpu740_mnem;

typedef struct { uint8_t mnem; uint8_t mode; } cpu740_opinfo;

const cpu740_opinfo *cpu740_optable(void);
int cpu740_mode_length(int mode);
const char *cpu740_mnem_name(int mnem);

#endif
