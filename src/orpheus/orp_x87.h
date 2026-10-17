#ifndef ORPHEUS_ORP_X87_H
#define ORPHEUS_ORP_X87_H
/* The x87's 80-bit arithmetic (64-bit significand, round to nearest even), for the few places where a3s.dll truncates
   or compares an extended-precision result: there a double's rounding can land on the other side of an integer.
   Finite values only; no infinities or NaNs. */
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t m;   /* significand, bit 63 set; 0 for zero */
    int32_t e;    /* the value is m * 2^(e - 63) */
    bool neg;
} orp_x80;

orp_x80 x80_from_double(double d);   /* fld qword: exact */
orp_x80 x80_from_int(int32_t i);     /* fild: exact */
double x80_to_double(orp_x80 a);     /* fstp qword: rounds to nearest even, subnormals included */
orp_x80 x80_add(orp_x80 a, orp_x80 b);
orp_x80 x80_sub(orp_x80 a, orp_x80 b);
orp_x80 x80_mul(orp_x80 a, orp_x80 b);
orp_x80 x80_div(orp_x80 a, orp_x80 b);   /* b nonzero */
int x80_cmp(orp_x80 a, orp_x80 b);       /* -1, 0 or 1 */
int32_t x80_trunc(orp_x80 a);            /* _ftol: toward zero; |a| < 2^31 */

#endif
