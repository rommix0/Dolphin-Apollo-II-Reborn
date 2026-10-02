/* Dev tool (M3 spec §4): writes random single-instruction vectors from cpu740 for ghidra740's
   ValidateModule.java, and optionally the blank 64 KB image the validation imports. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu740/vec740.h"

static int usage(void)
{
    fprintf(stderr, "usage: cpu740_vectors -o FILE [-n PER_OPCODE] [--seed N] [--blank FILE]\n"
                    "  -n PER_OPCODE  vectors per executable opcode (default 64)\n"
                    "  --seed N       random seed (default 1)\n"
                    "  --blank FILE   also write a 65536-byte all-zero image\n");
    return 1;
}

int main(int argc, char **argv)
{
    const char *out_path = NULL, *blank_path = NULL;
    unsigned n = 64;
    uint32_t seed = 1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc)
            out_path = argv[++i];
        else if (!strcmp(argv[i], "-n") && i + 1 < argc)
            n = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
            seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--blank") && i + 1 < argc)
            blank_path = argv[++i];
        else
            return usage();
    }
    if (!out_path || n == 0)
        return usage();

    FILE *f = fopen(out_path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", out_path);
        return 1;
    }
    int lines = vec740_write(f, n, seed);
    if (fclose(f) != 0 || lines < 0) {
        fprintf(stderr, "cannot write %s\n", out_path);
        return 1;
    }
    if (blank_path) {
        static uint8_t zero[0x10000];   /* zero-initialised */
        FILE *b = fopen(blank_path, "wb");
        bool ok = b && fwrite(zero, 1, sizeof zero, b) == sizeof zero;
        if (b && fclose(b) != 0)
            ok = false;
        if (!ok) {
            fprintf(stderr, "cannot write %s\n", blank_path);
            return 1;
        }
    }
    printf("%d vector lines written to %s\n", lines, out_path);
    return 0;
}
