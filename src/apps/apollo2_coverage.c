/* Dev tool (M3 spec §5.1): runs the firmware through boot and every golden-corpus text and writes the
   union of the executed instruction addresses and the indirect JMP/JSR targets, for ghidra740. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/rom_engine.h"
#include "engine/rom_id.h"
#include "machine/coverage.h"
#include "../../tests/corpus_list.h"

static machine g_m;
static coverage g_cov;
static uint8_t g_rom[MACHINE_ROM_SIZE];
static const char *const k_corpus[] = { CORPUS_NAMES };

static int usage(void)
{
    fprintf(stderr, "usage: apollo2_coverage [--rom PATH] [--rom-any] -o FILE [TEXTFILE...]\n"
                    "  runs boot, every tests/corpus/<name>.txt of the golden corpus, then each TEXTFILE\n"
                    "  --rom PATH  ROM image (default: $APOLLO2_ROM or UK_V2.01_4.04_ROM.BIN)\n"
                    "  --rom-any   accept a ROM whose MD5 is not the UK V2.01 4.04 image (with a warning)\n");
    return 1;
}

static bool load_rom(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    bool ok = fread(g_rom, 1, sizeof g_rom, f) == sizeof g_rom && fgetc(f) == EOF;
    fclose(f);
    return ok;
}

/* Whole file, binary, trailing CR/LF removed (as the golden test does). NULL if unreadable. */
static char *read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = size >= 0 ? malloc((size_t)size + 1) : NULL;
    size_t n = s ? fread(s, 1, (size_t)size, f) : 0;
    fclose(f);
    if (!s)
        return NULL;
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        n--;
    s[n] = '\0';
    return s;
}

static bool run(const char *label, const char *text, bool boot)
{
    machine_config cfg;
    frame_list fl = { 0 };
    machine_default_config(&cfg);
    cfg.coverage = &g_cov;
    machine_status st = rom_engine_speak_cfg(&g_m, g_rom, sizeof g_rom, &cfg, text, boot, &fl);
    frame_list_free(&fl);
    if (st != MACHINE_IDLE) {
        fprintf(stderr, "%s: the firmware did not reach idle (status %d)\n", label, (int)st);
        machine_dump(&g_m, stderr);
        return false;
    }
    if (g_m.unmodelled_count || g_m.unmodelled_dropped) {
        fprintf(stderr, "%s: the firmware made unmodelled accesses\n", label);
        machine_print_unmodelled(&g_m, stderr);
        return false;
    }
    return true;
}

static bool run_file(const char *label, const char *path)
{
    char *text = read_text(path);
    if (!text) {
        fprintf(stderr, "cannot read %s\n", path);
        return false;
    }
    bool ok = run(label, text, false);
    free(text);
    return ok;
}

int main(int argc, char **argv)
{
    const char *rom_path = getenv("APOLLO2_ROM");
    const char *out_path = NULL;
    bool rom_any = false;
    int first_extra = argc;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rom") && i + 1 < argc) {
            rom_path = argv[++i];
        } else if (!strcmp(argv[i], "--rom-any")) {
            rom_any = true;
        } else if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            out_path = argv[++i];
        } else if (argv[i][0] == '-') {
            return usage();
        } else {
            first_extra = i;
            break;
        }
    }
    if (!out_path)
        return usage();
    if (!rom_path)
        rom_path = "UK_V2.01_4.04_ROM.BIN";
    if (!load_rom(rom_path)) {
        fprintf(stderr, "cannot load a %d-byte ROM from %s\n", MACHINE_ROM_SIZE, rom_path);
        return 1;
    }
    if (!rom_id_accept(rom_path, g_rom, sizeof g_rom, rom_any, stderr))
        return 1;

    coverage_init(&g_cov);
    bool ok = run("boot", "", true);
    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char path[160];
        snprintf(path, sizeof path, "tests/corpus/%s.txt", k_corpus[c]);
        ok = run_file(k_corpus[c], path) && ok;
    }
    for (int i = first_extra; i < argc; i++)
        ok = run_file(argv[i], argv[i]) && ok;
    if (!ok) {
        coverage_free(&g_cov);
        return 2;
    }

    FILE *f = fopen(out_path, "wb");
    bool written = f && coverage_write(&g_cov, f);
    if (f && fclose(f) != 0)
        written = false;
    if (!written) {
        fprintf(stderr, "cannot write %s\n", out_path);
        coverage_free(&g_cov);
        return 1;
    }
    printf("rom %zu, low %zu, ramcode %zu, ram %zu instruction addresses; %zu indirect edges\n",
           coverage_count(&g_cov, COV_ROM), coverage_count(&g_cov, COV_LOW),
           coverage_count(&g_cov, COV_RAMCODE), coverage_count(&g_cov, COV_RAM), g_cov.edge_count);
    if (coverage_count(&g_cov, COV_RAM))
        fprintf(stderr, "finding: instructions executed in RAM outside $0200-$03FE (see the 'x ram' lines)\n");
    coverage_free(&g_cov);
    return 0;
}
