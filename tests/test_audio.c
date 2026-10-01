#include <stdio.h>
#include <string.h>
#include "test.h"
#include "audio/playback.h"
#include "audio/wav.h"

static void test_wav_bytes(void)
{
    static const int16_t samples[3] = { 1, -2, 0x1234 };
    static const uint8_t want[] = {
        'R', 'I', 'F', 'F', 42, 0, 0, 0, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
        0x10, 0x27, 0, 0, 0x20, 0x4E, 0, 0, 2, 0, 16, 0,     /* 10000 Hz, 20000 bytes/s, align 2, 16 bit */
        'd', 'a', 't', 'a', 6, 0, 0, 0,
        0x01, 0x00, 0xFE, 0xFF, 0x34, 0x12,
    };
    uint8_t got[64];
    CHECK(wav_write("build/test_audio.wav", samples, 3, 10000));
    FILE *f = fopen("build/test_audio.wav", "rb");
    CHECK(f != NULL);
    if (!f)
        return;
    size_t n = fread(got, 1, sizeof got, f);
    fclose(f);
    CHECK_EQ(n, sizeof want);
    CHECK(memcmp(got, want, sizeof want) == 0);
    CHECK(!wav_write("build/no_such_dir/x.wav", samples, 3, 10000));
}

/* Playback itself is not exercised here (tests must stay silent). The format query must work
   whenever a device exists. */
static void test_playback_query(void)
{
    if (!playback_supported(10000))
        SKIP("no waveOut device accepts 10 kHz mono 16-bit PCM");
    CHECK(playback_supported(10000));
}

void suite_audio(void)
{
    test_wav_bytes();
    test_playback_query();
}
