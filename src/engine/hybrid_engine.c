#include <string.h>
#include "engine/hybrid_engine.h"
#include "engine/thunks.h"

typedef struct {
    fw_bridge bridge;
    const char *only;
    bool ok;
} hybrid_setup;

static void setup(machine *m, void *user)
{
    hybrid_setup *h = user;
    h->ok = fw_bridge_init(&h->bridge, m) && fw_install(m, &h->bridge, h->only);
    if (!h->ok)
        m->fault = MFAULT_FIRMWARE;
}

machine_status hybrid_engine_speak(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                   const char *only, const char *text, bool include_boot,
                                   frame_list *produced, frame_list *raw, hybrid_stats *stats)
{
    hybrid_setup h;
    frame_list scratch = { 0 };
    memset(&h, 0, sizeof h);
    h.only = only;
    machine_status st = rom_engine_run(m, rom, len, cfg, setup, &h, text, include_boot,
                                       produced ? produced : &scratch, raw);
    machine_clear_hooks(m);   /* the bridge lives on this stack frame */
    frame_list_free(&scratch);
    if (stats) {
        stats->thunk_calls = h.bridge.thunk_calls;
        stats->tn_flushes = h.bridge.f.tn_flushes;
    }
    return h.ok ? st : MACHINE_FAULT;
}
