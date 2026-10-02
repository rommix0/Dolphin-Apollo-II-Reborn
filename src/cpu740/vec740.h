#ifndef VEC740_H
#define VEC740_H
/* Random single-instruction vectors from cpu740, for validating the Ghidra MELPS740 module (M3 spec §4).
   One line per vector:
     V pc=PPPP op=XX[,XX..] a=XX x=XX y=XX s=XX p=XX r=AAAA:VV[,..] | A=XX X=XX Y=XX S=XX P=XX PC=PPPP w=AAAA:VV[,..] | TEXT
     D pc=PPPP op=XX[,XX..] | TEXT
   V: executed. r = bytes read other than the instruction's own (value read), w = bytes written (last value).
   D: decode only (ILL, WIT, BRK, SED, SET, STP, MUL, DIV: cpu740 faults on them or WIT only waits).
   p always has D = 0, T = 0, B = 1; s is in $10-$EF, so the stack page never wraps.
   per_opcode V vectors are written for each executable opcode, one D line for each decode-only one. */
#include <stdint.h>
#include <stdio.h>

int vec740_write(FILE *out, unsigned per_opcode, uint32_t seed);   /* lines written, -1 on an I/O error */

#endif
