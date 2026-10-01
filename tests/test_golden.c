#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "engine/rom_engine.h"

static const char *const k_corpus[] = {
    "hello", "prose", "numbers", "punctuation", "letters_k_to_z", "abbreviations",
};

static machine g_m;

/* Whole file as a NUL-terminated string (binary mode, so no CRLF translation); NULL if missing. */
static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = malloc((size_t)size + 1);
    size_t n = s ? fread(s, 1, (size_t)size, f) : 0;
    fclose(f);
    if (!s)
        return NULL;
    s[n] = '\0';
    return s;
}

static bool write_file(const char *path, const char *s)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(s, 1, strlen(s), f) == strlen(s);
    return fclose(f) == 0 && ok;
}

static void strip_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

static void report_diff(const char *name, const char *got, const char *want)
{
    size_t line = 1, i = 0;
    while (got[i] && got[i] == want[i]) {
        if (got[i] == '\n')
            line++;
        i++;
    }
    fprintf(stderr, "  %s: produced frames differ from golden at frame %zu\n", name, line);
}

void suite_golden(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    bool update = getenv("APOLLO2_UPDATE_GOLDEN") != NULL;

    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char txt_path[128], frm_path[128];
        snprintf(txt_path, sizeof txt_path, "tests/corpus/%s.txt", k_corpus[c]);
        snprintf(frm_path, sizeof frm_path, "tests/golden/%s.frm", k_corpus[c]);
        char *text = read_file(txt_path);
        CHECK(text != NULL);
        if (!text)
            continue;
        strip_newline(text);

        frame_list fl = { 0 };
        machine_status st = rom_engine_speak(&g_m, rom, len, text, false, &fl);
        if (st != MACHINE_IDLE)
            machine_dump(&g_m, stderr);
        CHECK_EQ(st, MACHINE_IDLE);
        CHECK(fl.count > 0);
        char *hex = frame_list_hex(&fl);

        if (update) {
            CHECK(write_file(frm_path, hex));
            printf("  updated %s (%zu frames)\n", frm_path, fl.count);
        } else {
            char *want = read_file(frm_path);
            if (!want) {
                fprintf(stderr, "  missing %s: run once with APOLLO2_UPDATE_GOLDEN=1 and review\n", frm_path);
                g_test_failures++;
            } else {
                if (strcmp(hex, want) != 0) {
                    report_diff(k_corpus[c], hex, want);
                    g_test_failures++;
                }
                free(want);
            }
        }
        free(hex);
        frame_list_free(&fl);
        free(text);
    }
}
