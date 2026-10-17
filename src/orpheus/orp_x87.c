#include <string.h>
#include "orpheus/orp_x87.h"

static orp_x80 zero(bool neg)
{
    orp_x80 z = { 0, 0, neg };
    return z;
}

static void mul64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo)
{
    uint64_t a0 = (uint32_t)a, a1 = a >> 32, b0 = (uint32_t)b, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (uint32_t)p01 + (uint32_t)p10;
    *lo = mid << 32 | (uint32_t)p00;
    *hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

/* The 128-bit magnitude hi:lo (with sticky: something nonzero below lo) times 2^(e - 127), rounded to 64 bits. */
static orp_x80 round_pack(bool neg, int32_t e, uint64_t hi, uint64_t lo, bool sticky)
{
    while (!(hi >> 63)) {
        hi = hi << 1 | lo >> 63;
        lo <<= 1;
        e--;
    }
    bool guard = lo >> 63, rest = (lo << 1) != 0 || sticky;
    if (guard && (rest || (hi & 1))) {
        if (++hi == 0) {
            hi = 1ULL << 63;
            e++;
        }
    }
    orp_x80 r = { hi, e, neg };
    return r;
}

orp_x80 x80_from_double(double d)
{
    uint64_t bits;
    memcpy(&bits, &d, sizeof bits);
    bool neg = bits >> 63;
    int32_t ex = (int32_t)(bits >> 52 & 0x7FF);
    uint64_t frac = bits & ((1ULL << 52) - 1);
    if (ex == 0) {
        if (frac == 0)
            return zero(neg);
        return round_pack(neg, -1074 + 127, 0, frac, false);   /* subnormal: frac * 2^-1074 */
    }
    orp_x80 r = { (frac | 1ULL << 52) << 11, ex - 1023, neg };
    return r;
}

orp_x80 x80_from_int(int32_t i)
{
    if (i == 0)
        return zero(false);
    uint64_t m = i < 0 ? (uint64_t)(-(int64_t)i) : (uint64_t)i;
    return round_pack(i < 0, 127, 0, m, false);
}

double x80_to_double(orp_x80 a)
{
    uint64_t bits = (uint64_t)a.neg << 63;
    if (a.m != 0) {
        int32_t e = a.e;
        uint64_t mant;
        bool guard, rest;
        if (e >= -1022) {
            mant = a.m >> 11;
            guard = a.m >> 10 & 1;
            rest = (a.m & 0x3FF) != 0;
        } else {   /* subnormal: units of 2^-1074 */
            int s = -(e + 1011);
            mant = s >= 64 ? 0 : a.m >> s;
            uint64_t rem = s >= 64 ? (s == 64 ? a.m : 0) : a.m << (64 - s);
            guard = s > 64 ? false : rem >> 63;
            rest = s > 64 ? true : (rem << 1) != 0;
        }
        if (guard && (rest || (mant & 1)))
            mant++;
        if (e >= -1022) {
            if (mant >> 53) {
                mant >>= 1;
                e++;
            }
            bits |= (uint64_t)(e + 1023) << 52 | (mant & ((1ULL << 52) - 1));
        } else {
            bits |= mant;   /* a carry into 2^52 gives the smallest normal */
        }
    }
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

orp_x80 x80_add(orp_x80 a, orp_x80 b)
{
    if (a.m == 0)
        return b.m == 0 ? zero(a.neg && b.neg) : b;
    if (b.m == 0)
        return a;
    if (a.e < b.e || (a.e == b.e && a.m < b.m)) {
        orp_x80 t = a;
        a = b;
        b = t;
    }
    int32_t d = a.e - b.e;
    uint64_t bhi = b.m, blo = 0;
    bool sticky = false;
    if (d >= 128) {
        bhi = 0;
        sticky = true;
    } else if (d >= 64) {
        sticky = d > 64 && (bhi & ((1ULL << (d - 64)) - 1)) != 0;
        blo = d == 64 ? bhi : bhi >> (d - 64);
        bhi = 0;
    } else if (d > 0) {
        blo = bhi << (64 - d);
        bhi >>= d;
    }
    uint64_t hi, lo;
    int32_t e = a.e;
    if (a.neg == b.neg) {
        lo = blo;
        uint64_t sum = a.m + bhi;
        bool carry = sum < a.m;
        hi = sum;
        if (carry) {
            sticky = sticky || (lo & 1);
            lo = lo >> 1 | hi << 63;
            hi = hi >> 1 | 1ULL << 63;
            e++;
        }
    } else {   /* |a| >= |b|; with sticky, b is a little more than bhi:blo */
        lo = 0 - blo;
        hi = a.m - bhi - (blo != 0);
        if (sticky) {
            if (lo == 0)
                hi--;
            lo--;
        }
        if (hi == 0 && lo == 0 && !sticky)
            return zero(false);
    }
    return round_pack(a.neg, e, hi, lo, sticky);
}

orp_x80 x80_sub(orp_x80 a, orp_x80 b)
{
    b.neg = !b.neg;
    return x80_add(a, b);
}

orp_x80 x80_mul(orp_x80 a, orp_x80 b)
{
    bool neg = a.neg != b.neg;
    if (a.m == 0 || b.m == 0)
        return zero(neg);
    uint64_t hi, lo;
    mul64(a.m, b.m, &hi, &lo);
    return round_pack(neg, a.e + b.e + 1, hi, lo, false);
}

orp_x80 x80_div(orp_x80 a, orp_x80 b)
{
    bool neg = a.neg != b.neg;
    if (a.m == 0)
        return zero(neg);
    int32_t e = a.e - b.e;
    uint64_t r, low;
    if (a.m >= b.m) {   /* quotient (a.m / b.m) * 2^63 */
        r = a.m >> 1;
        low = a.m << 63;
    } else {            /* (a.m / b.m) * 2^64 */
        r = a.m;
        low = 0;
        e--;
    }
    uint64_t q = 0;
    for (int i = 63; i >= 0; i--) {   /* r < b.m throughout */
        bool top = r >> 63;
        r = r << 1 | (low >> i & 1);
        q <<= 1;
        if (top || r >= b.m) {
            r -= b.m;
            q |= 1;
        }
    }
    if (r > b.m - r || (r == b.m - r && (q & 1))) {
        if (++q == 0) {
            q = 1ULL << 63;
            e++;
        }
    }
    orp_x80 res = { q, e, neg };
    return res;
}

int x80_cmp(orp_x80 a, orp_x80 b)
{
    if (a.m == 0 && b.m == 0)
        return 0;
    if (a.m == 0)
        return b.neg ? 1 : -1;
    if (b.m == 0)
        return a.neg ? -1 : 1;
    if (a.neg != b.neg)
        return a.neg ? -1 : 1;
    int mag = a.e != b.e ? (a.e < b.e ? -1 : 1) : a.m != b.m ? (a.m < b.m ? -1 : 1) : 0;
    return a.neg ? -mag : mag;
}

int32_t x80_trunc(orp_x80 a)
{
    if (a.m == 0 || a.e < 0)
        return 0;
    int32_t v = (int32_t)(a.m >> (63 - a.e));
    return a.neg ? -v : v;
}
