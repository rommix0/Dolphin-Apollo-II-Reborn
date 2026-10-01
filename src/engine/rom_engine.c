#include <stdlib.h>
#include <string.h>
#include "engine/rom_engine.h"

typedef struct {
    frame_list *list;
    bool enabled;
} frame_sink;

static void on_frame(void *user, const uint8_t frame[APOLLO_FRAME_BYTES])
{
    frame_sink *s = user;
    frame_list *fl = s->list;
    if (!s->enabled)
        return;
    if (fl->count == fl->cap) {
        size_t cap = fl->cap ? fl->cap * 2 : 256;
        void *grown = realloc(fl->frames, cap * sizeof fl->frames[0]);
        if (!grown)
            abort();
        fl->frames = grown;
        fl->cap = cap;
    }
    memcpy(fl->frames[fl->count++], frame, APOLLO_FRAME_BYTES);
}

void frame_list_free(frame_list *fl)
{
    free(fl->frames);
    fl->frames = NULL;
    fl->count = fl->cap = 0;
}

char *frame_list_hex(const frame_list *fl)
{
    static const char k_hex[] = "0123456789ABCDEF";
    char *s = malloc(fl->count * (APOLLO_FRAME_BYTES * 2 + 1) + 1);
    char *p = s;
    if (!s)
        abort();
    for (size_t i = 0; i < fl->count; i++) {
        for (int b = 0; b < APOLLO_FRAME_BYTES; b++) {
            *p++ = k_hex[fl->frames[i][b] >> 4];
            *p++ = k_hex[fl->frames[i][b] & 0x0F];
        }
        *p++ = '\n';
    }
    *p = '\0';
    return s;
}

machine_status rom_engine_speak(machine *m, const uint8_t *rom, size_t len, const char *text,
                                bool include_boot, frame_list *out)
{
    return rom_engine_speak_cfg(m, rom, len, NULL, text, include_boot, out);
}

machine_status rom_engine_speak_cfg(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                    const char *text, bool include_boot, frame_list *out)
{
    frame_sink sink;
    machine_status st;
    sink.list = out;
    sink.enabled = include_boot;
    if (!machine_init(m, rom, len, cfg))
        return MACHINE_FAULT;
    machine_set_outputs(m, on_frame, &sink);
    st = machine_run(m, ROM_ENGINE_BOOT_CYCLES);
    if (st == MACHINE_IDLE) {
        m->frames_produced = 0;
        m->lower_window_reads = 0;
        sink.enabled = true;
        if (machine_feed_serial(m, (const uint8_t *)text, strlen(text)) &&
            machine_feed_serial(m, (const uint8_t *)"\r", 1))
            st = machine_run(m, ROM_ENGINE_SPEAK_CYCLES);
        else {
            m->fault = MFAULT_INPUT_OVERFLOW;   /* text longer than the input queue */
            st = MACHINE_FAULT;
        }
    }
    machine_set_outputs(m, NULL, NULL);   /* sink lives on this stack frame */
    return st;
}
