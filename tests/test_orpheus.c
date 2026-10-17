#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "orpheus/orp_map.h"
#include "orpheus/orp_synth.h"
#include "orpheus/orp_x87.h"
#include "synth/params.h"

/* tests/orpheus/NAME.par (16 parameters per line) and NAME.pcm (the Unicorn oracle's int16 LE output for them),
   both from xtras/orpheus/orpheus_gendata.py. */
#define MAX_FRAMES 2000

static int16_t g_par[MAX_FRAMES][ORP_PARAMS];
static int16_t g_want[MAX_FRAMES * ORP_FRAME_SAMPLES];
static orp_synth g_s;

static size_t read_par(const char *path)
{
    FILE *f = fopen(path, "r");
    size_t n = 0;
    if (!f)
        return 0;
    for (;;) {
        int v[ORP_PARAMS], got = 0;
        for (int i = 0; i < ORP_PARAMS && fscanf(f, "%d", &v[i]) == 1; i++)
            got++;
        if (got != ORP_PARAMS || n == MAX_FRAMES)
            break;
        for (int i = 0; i < ORP_PARAMS; i++)
            g_par[n][i] = (int16_t)v[i];
        n++;
    }
    fclose(f);
    return n;
}

static size_t read_pcm(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    size_t n = fread(g_want, sizeof g_want[0], MAX_FRAMES * ORP_FRAME_SAMPLES, f);
    fclose(f);
    return n;
}

/* Every sample of NAME equals the oracle's; returns the clipped count. */
static uint64_t check_item(const char *name)
{
    char par[128], pcm[128];
    snprintf(par, sizeof par, "tests/orpheus/%s.par", name);
    snprintf(pcm, sizeof pcm, "tests/orpheus/%s.pcm", name);
    size_t frames = read_par(par), samples = read_pcm(pcm);
    CHECK(frames > 0);
    CHECK_EQ(samples, frames * ORP_FRAME_SAMPLES);
    if (frames == 0 || samples != frames * ORP_FRAME_SAMPLES)
        return 0;
    orp_init(&g_s);
    size_t mismatches = 0;
    for (size_t k = 0; k < frames; k++) {
        int16_t out[ORP_FRAME_SAMPLES];
        orp_frame(&g_s, g_par[k], k == 0, out);
        for (int n = 0; n < ORP_FRAME_SAMPLES; n++) {
            int16_t want = g_want[k * ORP_FRAME_SAMPLES + n];
            if (out[n] != want && mismatches++ < 5)
                fprintf(stderr, "  %s: frame %zu sample %d: got %d, want %d\n", name, k, n, out[n], want);
        }
    }
    if (mismatches)
        fprintf(stderr, "  %s: %zu of %zu samples differ from the oracle\n", name, mismatches, samples);
    CHECK_EQ(mismatches, 0);
    return orp_clipped(&g_s);
}

/* A tbyte as 20 hex digits: sign and biased exponent, then the significand. */
static orp_x80 tbyte(const char *hex)
{
    unsigned se;
    unsigned long long m;
    orp_x80 x = { 0, 0, false };
    if (sscanf(hex, "%4x%16llx", &se, &m) != 2)
        return x;
    x.neg = (se >> 15) != 0;
    x.m = m;
    x.e = m ? (int32_t)(se & 0x7FFF) - 16383 : 0;
    return x;
}

static bool same(orp_x80 a, orp_x80 b)
{
    return a.m == b.m && a.neg == b.neg && (a.m == 0 || a.e == b.e);
}

static double from_bits(unsigned long long bits)
{
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

/* tests/orpheus/x87.txt (xtras/orpheus/orpheus_x87vectors.py): Unicorn's own x87 results. */
static void test_x87(void)
{
    FILE *f = fopen("tests/orpheus/x87.txt", "r");
    CHECK(f != NULL);
    if (!f)
        return;
    char op[8], a[24], b[24], r[24], d[24];
    int lines = 0, bad = 0;
    while (fscanf(f, "%7s %23s %23s %23s %23s", op, a, b, r, d) == 5) {
        lines++;
        bool ok;
        if (!strcmp(op, "trunc")) {
            ok = x80_trunc(tbyte(a)) == atoi(r);
        } else if (!strcmp(op, "cvt")) {
            double want = from_bits(strtoull(d, NULL, 16)), got = x80_to_double(tbyte(a));
            ok = memcmp(&want, &got, sizeof got) == 0;
        } else {
            orp_x80 x = tbyte(a), y = tbyte(b), got;
            got = !strcmp(op, "add") ? x80_add(x, y) : !strcmp(op, "sub") ? x80_sub(x, y)
                : !strcmp(op, "mul") ? x80_mul(x, y) : x80_div(x, y);
            double want_d = from_bits(strtoull(d, NULL, 16)), got_d = x80_to_double(got);
            ok = same(got, tbyte(r)) && memcmp(&want_d, &got_d, sizeof got_d) == 0;
        }
        if (!ok && bad++ < 5)
            fprintf(stderr, "  x87: %s %s %s: want %s %s\n", op, a, b, r, d);
    }
    fclose(f);
    CHECK(lines == 3100);
    CHECK_EQ(bad, 0);
    CHECK(x80_to_double(x80_from_double(-0.0)) == 0.0 && x80_from_double(-0.0).neg);
    CHECK_EQ(x80_trunc(x80_from_int(-123456)), -123456);
}

static void test_oracle_items(void)
{
    static const char *const items[] = { "hello", "prose", "breathy", "long_clause" };
    for (size_t i = 0; i < sizeof items / sizeof items[0]; i++)
        CHECK_EQ(check_item(items[i]), 0);
    CHECK(check_item("stress") > 0);   /* its over-range frames saturate where the oracle's do */
}

/* The C mapping gives the 16 values orpheus_map.py wrote into NAME.par for every Apollo frame of NAME.frm. */
static void check_map(const char *name)
{
    char frm[128], par[128];
    snprintf(frm, sizeof frm, "tests/orpheus/%s.frm", name);
    snprintf(par, sizeof par, "tests/orpheus/%s.par", name);
    size_t frames = read_par(par);
    FILE *f = fopen(frm, "r");
    CHECK(f != NULL && frames > 0);
    if (!f)
        return;
    static uint8_t apollo[MAX_FRAMES][SYNTH_FRAME_BYTES];
    char line[128];
    size_t n = 0;
    while (n < MAX_FRAMES && fgets(line, sizeof line, f))
        if (test_parse_hex(line, apollo[n], SYNTH_FRAME_BYTES))
            n++;
    fclose(f);
    CHECK_EQ(n, frames);
    float centre = params_f0_centre(&apollo[0][0], n);
    int bad = 0;
    for (size_t k = 0; k < n && k < frames; k++) {
        int16_t p[ORP_PARAMS];
        orp_map_frame(apollo[k], centre, p);
        if (memcmp(p, g_par[k], sizeof p) != 0 && bad++ < 3) {
            fprintf(stderr, "  %s frame %zu:", name, k);
            for (int i = 0; i < ORP_PARAMS; i++)
                fprintf(stderr, " %d/%d", p[i], g_par[k][i]);
            fprintf(stderr, " (got/want)\n");
        }
    }
    CHECK_EQ(bad, 0);
}

static void test_map(void)
{
    static const char *const items[] = { "hello", "prose", "breathy", "long_clause" };
    for (size_t i = 0; i < sizeof items / sizeof items[0]; i++)
        check_map(items[i]);
    uint8_t f[SYNTH_FRAME_BYTES] = { 0 };
    int16_t p[ORP_PARAMS];
    f[4] = 63;   /* A2: offset 0 */
    f[2] = 1;    /* A1 code 1 is silence */
    f[9] = 1;    /* V 1: noise only */
    orp_map_frame(f, 0.0f, p);
    CHECK_EQ(p[6], 63);
    CHECK_EQ(p[5], 0);
    CHECK_EQ(p[12], 0);
    CHECK_EQ(p[15], 0);
}

void suite_orpheus(void)
{
    test_x87();
    test_oracle_items();
    test_map();
}
