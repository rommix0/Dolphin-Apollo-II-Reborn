/* Scratch: hex .frm frames -> src/synth -> 10 kHz WAV, with the output filters switchable. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "synth/synth.h"

static void put32(FILE *f, unsigned v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, unsigned short v) { fwrite(&v, 2, 1, f); }

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: frm2wav IN.frm OUT.wav [nopeak] [nolpf]\n");
        return 1;
    }
    FILE *in = fopen(argv[1], "r");
    if (!in)
        return 1;
    static uint8_t fr[20000][SYNTH_FRAME_BYTES];
    size_t n = 0;
    char line[256];
    while (n < 20000 && fgets(line, sizeof line, in)) {
        if (strlen(line) < 2 * SYNTH_FRAME_BYTES)
            continue;
        for (int i = 0; i < SYNTH_FRAME_BYTES; i++) {
            unsigned b;
            sscanf(line + 2 * i, "%2x", &b);
            fr[n][i] = (uint8_t)b;
        }
        n++;
    }
    fclose(in);
    synth_options so;
    synth_default_options(&so);
    for (int a = 3; a < argc; a++) {
        if (!strcmp(argv[a], "nopeak"))
            so.peak_filter = false;
        if (!strcmp(argv[a], "nolpf"))
            so.lowpass = false;
    }
    so.f0_centre_hz = params_f0_centre(&fr[0][0], n);
    synth s;
    synth_init(&s, &so);
    int16_t *pcm = malloc(n * SYNTH_FRAME_SAMPLES * sizeof *pcm);
    for (size_t i = 0; i < n; i++)
        synth_frame(&s, fr[i], pcm + i * SYNTH_FRAME_SAMPLES);
    unsigned bytes = (unsigned)(n * SYNTH_FRAME_SAMPLES * 2);
    FILE *o = fopen(argv[2], "wb");
    fwrite("RIFF", 1, 4, o); put32(o, 36 + bytes); fwrite("WAVEfmt ", 1, 8, o);
    put32(o, 16); put16(o, 1); put16(o, 1); put32(o, SYNTH_RATE); put32(o, SYNTH_RATE * 2); put16(o, 2); put16(o, 16);
    fwrite("data", 1, 4, o); put32(o, bytes); fwrite(pcm, 2, n * SYNTH_FRAME_SAMPLES, o);
    fclose(o);
    int peak = 0;
    for (size_t i = 0; i < n * SYNTH_FRAME_SAMPLES; i++)
        peak = abs(pcm[i]) > peak ? abs(pcm[i]) : peak;
    printf("%s: %zu frames, peak %d, clipped %llu\n", argv[2], n, peak, (unsigned long long)s.clipped);
    return 0;
}
