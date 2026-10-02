#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "cpu740/vec740.h"

static int write_to(const char *path, unsigned n, uint32_t seed)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    int lines = vec740_write(f, n, seed);
    fclose(f);
    return lines;
}

void suite_vec740(void)
{
    int n1 = write_to("build/test_vec740_a.txt", 4, 7);
    int n2 = write_to("build/test_vec740_b.txt", 4, 7);
    CHECK(n1 > 256);
    CHECK_EQ(n1, n2);
    char *a = test_read_file("build/test_vec740_a.txt");
    char *b = test_read_file("build/test_vec740_b.txt");
    CHECK(a != NULL && b != NULL);
    if (!a || !b) {
        free(a);
        free(b);
        return;
    }
    CHECK(strcmp(a, b) == 0);                      /* deterministic for a seed */

    char kind[256] = { 0 };
    int lda_checked = 0;
    for (char *line = strtok(a, "\n"); line; line = strtok(NULL, "\n")) {
        unsigned pc, op;
        if (line[0] == '#')
            continue;
        CHECK(line[0] == 'V' || line[0] == 'D');
        CHECK(sscanf(line + 2, "pc=%4x op=%2x", &pc, &op) == 2);
        if (op > 0xFF)
            continue;
        if (kind[op] != 'V')
            kind[op] = line[0];
        if (line[0] == 'V' && op == 0xA9) {        /* LDA #imm: check the field meanings once */
            unsigned imm, ra, rx, ry, rs, rp, rpc;
            const char *post = strstr(line, " | A=");
            CHECK(sscanf(line, "V pc=%4x op=A9,%2x", &pc, &imm) == 2);
            CHECK(post != NULL);
            CHECK(strstr(line, " r= | ") != NULL);   /* reads nothing but its own bytes */
            if (post && sscanf(post, " | A=%2x X=%2x Y=%2x S=%2x P=%2x PC=%4x", &ra, &rx, &ry, &rs, &rp, &rpc) == 6) {
                CHECK_EQ(ra, imm);
                CHECK_EQ(rpc, (pc + 2) & 0xFFFF);
                CHECK_EQ((rp >> 1) & 1, imm == 0);   /* Z */
                CHECK_EQ((rp >> 7) & 1, imm >> 7);   /* N */
                lda_checked++;
            }
        }
    }
    for (int op = 0; op < 256; op++)
        if (!kind[op])
            fprintf(stderr, "  opcode %02X missing\n", op), g_test_failures++;
    CHECK_EQ(kind[0xA9], 'V');
    CHECK_EQ(kind[0x03], 'V');   /* BBS0 A,rel */
    CHECK_EQ(kind[0x3C], 'V');   /* LDM */
    CHECK_EQ(kind[0x28], 'V');   /* PLP (retried when the pulled byte has T set) */
    CHECK_EQ(kind[0x42], 'D');   /* STP */
    CHECK_EQ(kind[0xC2], 'D');   /* WIT */
    CHECK_EQ(kind[0x89], 'D');   /* STA # does not exist on the 740 */
    CHECK(lda_checked > 0);
    free(a);
    free(b);
}
