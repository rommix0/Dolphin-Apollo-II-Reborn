#include <stdio.h>
#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "engine/rom_engine.h"

static machine g_m;

/* What the DSP actually received (spec §1.3). raw[0] is the buffer left over from before the
   first swap. After that each raw frame is the next produced frame, or a replay of the previous
   one when the main loop was late. The last produced frame may be cut off by the final DSP reset. */
void suite_raw_stream(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    frame_list produced = { 0 }, raw = { 0 };
    machine_status st = rom_engine_speak_streams(&g_m, rom, len, NULL, "Hello world.", &produced, &raw);
    if (st != MACHINE_IDLE)
        machine_dump(&g_m, stderr);
    CHECK_EQ(st, MACHINE_IDLE);
    print_unmodelled(&g_m);
    CHECK_EQ(g_m.unmodelled_count, 0);
    CHECK(produced.count > 0);
    CHECK(raw.count > 0);
    CHECK(g_m.dsp_resets >= 2);                  /* speech start ($B323) and stop ($B398) */
    CHECK_EQ(g_m.raw_discarded, 0);
    size_t aligned = 0;
    for (size_t i = 0; i < raw.count; i++)
        if (raw.frames[i][18] == 0xFF && raw.frames[i][19] == 0xFF)
            aligned++;
    CHECK_EQ(aligned, raw.count);                /* every frame ends with the $FFFF word */

    size_t next = 0, replays = 0;
    for (size_t i = 1; i < raw.count; i++) {
        if (next < produced.count && memcmp(raw.frames[i], produced.frames[next], APOLLO_FRAME_BYTES) == 0) {
            next++;
        } else if (next > 0 && memcmp(raw.frames[i], produced.frames[next - 1], APOLLO_FRAME_BYTES) == 0) {
            replays++;
        } else {
            fprintf(stderr, "  raw frame %zu is neither produced frame %zu nor a replay\n", i, next);
            g_test_failures++;
            break;
        }
    }
    CHECK(next + 1 >= produced.count);
    printf("  raw: %zu frames for %zu produced (%zu matched, %zu replays), %llu DSP resets\n",
           raw.count, produced.count, next, replays, (unsigned long long)g_m.dsp_resets);
    frame_list_free(&produced);
    frame_list_free(&raw);
}
