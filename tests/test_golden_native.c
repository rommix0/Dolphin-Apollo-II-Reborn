/* The native engine reproduces every golden with no ROM (M5a spec 4.4): each corpus text, and the boot golden with the
   boot frames, through native_engine_speak: ENGINE_IDLE, no unmodelled access and byte-identical frames. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "engine/native_engine.h"

static const char *const k_corpus[] = { CORPUS_NAMES };
static native_engine g_e;

static void strip_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

/* One golden: name's text (with the boot frames if boot) against tests/golden/<name>.frm. */
static void check(const char *name, const char *text, bool boot, size_t *frames)
{
    char path[128];
    snprintf(path, sizeof path, "tests/golden/%s.frm", name);
    char *want = test_read_file(path);
    CHECK(want != NULL);
    if (!want)
        return;
    frame_list fl = { 0 };
    engine_status st = native_engine_speak(&g_e, NULL, text, boot, &fl, NULL);
    if (st != ENGINE_IDLE)
        fprintf(stderr, "  native %s: status %d, fault %d at $%04X\n", name, (int)st, (int)g_e.b.fault,
                g_e.b.fault_addr);
    CHECK_EQ(st, ENGINE_IDLE);
    CHECK_EQ(g_e.b.unmodelled_count, 0);
    char *got = frame_list_hex(&fl);
    if (strcmp(got, want) != 0) {
        size_t line = 1;
        for (size_t i = 0; got[i] && got[i] == want[i]; i++)
            line += got[i] == '\n';
        fprintf(stderr, "  native %s: frames differ from the golden at frame %zu\n", name, line);
        g_test_failures++;
    }
    *frames += fl.count;
    free(got);
    free(want);
    frame_list_free(&fl);
}

void suite_golden_native(void)
{
    size_t frames = 0, files = 0;
    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char path[128];
        snprintf(path, sizeof path, "tests/corpus/%s.txt", k_corpus[c]);
        char *text = test_read_file(path);
        CHECK(text != NULL);
        if (!text)
            continue;
        strip_newline(text);
        check(k_corpus[c], text, false, &frames);
        files++;
        free(text);
    }
    check("boot", "", true, &frames);   /* the startup announcement */
    files++;
    CHECK_EQ(files, 48);
    CHECK_EQ(frames, 23281);
    printf("  %zu goldens, %zu frames reproduced on the native engine\n", files, frames);
}
