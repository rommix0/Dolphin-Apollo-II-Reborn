/* apollo2: text -> speech through the original Apollo 2 firmware (emulated) and a Holmes-style
   parallel-formant synthesizer at 10 kHz (spec R1, R2). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio/playback.h"
#include "audio/wav.h"
#include "engine/rom_id.h"
#include "render/render.h"

#define APOLLO2_MAX_TEXT 1500   /* keeps the speech inside ROM_ENGINE_SPEAK_CYCLES (about 200 s) */

static machine g_machine;

static int usage(void)
{
    fprintf(stderr,
            "usage: apollo2 [options] TEXT\n"
            "       apollo2 [options] --text-file FILE\n"
            "  -o FILE            write a WAV file (10 kHz, mono, 16-bit) instead of playing live\n"
            "  --voice a|b        speaker table: a = male (default), b = non-male (@K1)\n"
            "  --engine E         firmware engine: rom (default) or hybrid (ported routines in C)\n"
            "  --rom PATH         ROM image (default: $APOLLO2_ROM or UK_V2.01_4.04_ROM.BIN)\n"
            "  --rom-any          accept a ROM whose MD5 is not the UK V2.01 4.04 image (with a warning)\n"
            "  --hyp LIST         synthesizer hypothesis switches, comma-separated: b11,b13\n"
            "  --stream S         frame stream fed to the synthesizer: produced (default) or raw\n"
            "  --prosody N        intonation strength 0-7, sent as @Rn (default 4 = the firmware's\n"
            "                     unscaled contour; the device's own default is 3)\n"
            "  --text-file FILE   read the text from FILE (first line; device control codes allowed)\n"
            "  --audio-check      report whether live 10 kHz playback is available, then exit\n"
            "TEXT may contain the device's @ commands (docs/apollo2.txt section 6); at most %d characters.\n"
            "exit codes: 0 ok, 1 usage/ROM/file error, 2 emulation fault, 3 cycle limit, 4 audio error, 5 unmodelled access logged\n",
            APOLLO2_MAX_TEXT);
    return 1;
}

static bool load_rom(const char *path, uint8_t *rom)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    bool ok = fread(rom, 1, MACHINE_ROM_SIZE, f) == MACHINE_ROM_SIZE && fgetc(f) == EOF;
    fclose(f);
    return ok;
}

/* First line of a file (CR/LF stripped) in a static buffer; NULL if unreadable. */
static const char *read_text_file(const char *path)
{
    static char buf[APOLLO2_MAX_TEXT + 2];
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    char *eol = strpbrk(buf, "\r\n");
    if (eol)
        *eol = '\0';
    return buf;
}

static bool parse_hyp(const char *list, synth_options *o)
{
    char buf[64];
    if (strlen(list) >= sizeof buf)
        return false;
    strcpy(buf, list);
    for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
        if (!strcmp(t, "b11"))
            o->b11_pulse_width = true;
        else if (!strcmp(t, "b13"))
            o->b13_reset = true;
        else
            return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    static uint8_t rom[MACHINE_ROM_SIZE];
    const char *rom_path = getenv("APOLLO2_ROM");
    const char *out_path = NULL, *text = NULL, *text_file = NULL;
    bool rom_any = false;
    render_options opt;
    render_default_options(&opt);

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *val = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--audio-check")) {
            bool ok = playback_supported(SYNTH_RATE);
            printf("live playback at %d Hz: %s\n", SYNTH_RATE, ok ? "available" : "NOT available");
            return ok ? 0 : 4;
        } else if (!strcmp(a, "-o") && val) {
            out_path = val;
            i++;
        } else if (!strcmp(a, "--voice") && val && (!strcmp(val, "a") || !strcmp(val, "b"))) {
            opt.voice = val[0];
            i++;
        } else if (!strcmp(a, "--engine") && val) {
            if (!strcmp(val, "rom"))
                opt.engine = RENDER_ENGINE_ROM;
            else if (!strcmp(val, "hybrid"))
                opt.engine = RENDER_ENGINE_HYBRID;
            else {
                fprintf(stderr, "engine '%s' is not available: use rom or hybrid (native arrives in milestone 5)\n", val);
                return 1;
            }
            i++;
        } else if (!strcmp(a, "--rom") && val) {
            rom_path = val;
            i++;
        } else if (!strcmp(a, "--rom-any")) {
            rom_any = true;
        } else if (!strcmp(a, "--hyp") && val) {
            if (!parse_hyp(val, &opt.synth))
                return usage();
            i++;
        } else if (!strcmp(a, "--stream") && val && (!strcmp(val, "produced") || !strcmp(val, "raw"))) {
            opt.stream = !strcmp(val, "raw") ? RENDER_STREAM_RAW : RENDER_STREAM_PRODUCED;
            i++;
        } else if (!strcmp(a, "--prosody") && val && val[0] >= '0' && val[0] <= '7' && val[1] == '\0') {
            opt.prosody = val[0] - '0';
            i++;
        } else if (!strcmp(a, "--text-file") && val) {
            text_file = val;
            i++;
        } else if (a[0] == '-' && a[1] != '\0') {
            return usage();
        } else if (!text) {
            text = a;
        } else {
            return usage();
        }
    }
    if ((text == NULL) == (text_file == NULL))
        return usage();
    if (text_file && !(text = read_text_file(text_file))) {
        fprintf(stderr, "cannot read %s\n", text_file);
        return 1;
    }
    if (!*text) {
        fprintf(stderr, "no text to speak (TEXT or the first line of the file is empty)\n");
        return 1;
    }
    if (strlen(text) > APOLLO2_MAX_TEXT) {
        fprintf(stderr, "text too long: %zu characters (at most %d)\n", strlen(text), APOLLO2_MAX_TEXT);
        return 1;
    }
    if (!rom_path)
        rom_path = "UK_V2.01_4.04_ROM.BIN";
    if (!load_rom(rom_path, rom)) {
        fprintf(stderr, "cannot load a %d-byte ROM from %s\n", MACHINE_ROM_SIZE, rom_path);
        return 1;
    }
    if (!rom_id_accept(rom_path, rom, sizeof rom, rom_any, stderr))
        return 1;

    render_result r;
    machine_status st = render_text(&g_machine, rom, sizeof rom, text, &opt, &r);
    if (st == MACHINE_FAULT) {
        machine_dump(&g_machine, stderr);
        return 2;
    }
    if (st == MACHINE_CYCLE_LIMIT) {
        fprintf(stderr, "cycle limit reached before the firmware went idle\n");
        return 3;
    }
    if (r.clipped)
        fprintf(stderr, "warning: %llu samples clipped\n", (unsigned long long)r.clipped);
    int rc = 0;
    if (g_machine.unmodelled_count || g_machine.unmodelled_dropped) {   /* spec section 4: never accept silently */
        fprintf(stderr, "the firmware made accesses the machine model does not cover:\n");
        machine_print_unmodelled(&g_machine, stderr);
        rc = 5;
    }
    if (out_path) {
        if (!wav_write(out_path, r.samples, r.count, SYNTH_RATE)) {
            fprintf(stderr, "cannot write %s\n", out_path);
            rc = 1;
        }
    } else if (r.count > 0) {
        char err[128];
        if (!playback_pcm16(r.samples, r.count, SYNTH_RATE, err, sizeof err)) {
            fprintf(stderr, "live playback failed: %s\n", err);
            rc = 4;
        }
    }
    render_result_free(&r);
    return rc;
}
