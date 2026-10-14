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
    if (s->enabled)
        frame_list_add(s->list, frame);
}

machine_status rom_engine_speak(machine *m, const uint8_t *rom, size_t len, const char *text,
                                bool include_boot, frame_list *out)
{
    return rom_engine_speak_cfg(m, rom, len, NULL, text, include_boot, out);
}

/* One engine run: its machine, its frame sinks, its input and watch, and the idle handler's count of idles. */
typedef struct {
    machine *m;
    frame_sink sink, raw_sink;
    const engine_input *in;
    const engine_watch *watch;
    unsigned idles;
} engine_run;

/* The machine's frame callback: the watch, then the produced-frame sink. */
static void on_produced(void *user, const uint8_t frame[APOLLO_FRAME_BYTES])
{
    engine_run *e = user;
    if (e->watch && e->watch->on_frame)
        e->watch->on_frame(e->watch->user, e->m);
    on_frame(&e->sink, frame);
}

/* The engine's idle handler (M4.6c2 spec 3.1), called by the engine's loop at machine_run's MACHINE_IDLE and by the
   C's input wait alike: the watch first; at the first idle (the end of boot) the counters reset, the sinks on, the
   input fed and the speaking phase's deadline set; at the next the run stops. */
static machine_idle_action on_idle(machine *m, void *user)
{
    engine_run *e = user;
    if (e->watch && e->watch->on_idle)
        e->watch->on_idle(e->watch->user, m);
    if (e->idles++ > 0)
        return MACHINE_STOP;
    m->frames_produced = 0;
    m->lower_window_reads = 0;
    m->raw_frames = 0;
    m->dsp_resets = 0;
    m->raw_discarded = 0;
    e->sink.enabled = true;
    e->raw_sink.enabled = true;
    if (!machine_feed_serial(m, e->in->bytes, e->in->n) ||
        (e->in->cr && !machine_feed_serial(m, (const uint8_t *)"\r", 1))) {
        m->fault = MFAULT_INPUT_OVERFLOW;   /* input longer than the serial queue */
        return MACHINE_STOP;
    }
    m->phase_deadline = m->cpu.cycles + ROM_ENGINE_SPEAK_CYCLES;
    return MACHINE_FEED;
}

machine_status rom_engine_run_input(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                    engine_setup_fn setup, void *user, const engine_input *in, bool include_boot,
                                    frame_list *produced, frame_list *raw, const engine_watch *watch)
{
    engine_run e = { m, { produced, include_boot }, { raw, false }, in, watch, 0 };
    machine_status st;
    if (!machine_init(m, rom, len, cfg))
        return MACHINE_FAULT;
    if (setup)
        setup(m, user);
    if (m->fault != MFAULT_NONE)   /* the setup could not install its hooks */
        return MACHINE_FAULT;
    machine_set_outputs(m, on_produced, &e);
    if (raw)
        machine_set_raw_output(m, on_frame, &e.raw_sink);
    machine_set_idle_handler(m, on_idle, &e);
    m->phase_deadline = m->cpu.cycles + ROM_ENGINE_BOOT_CYCLES;
    for (;;) {
        uint64_t left = m->phase_deadline > m->cpu.cycles ? m->phase_deadline - m->cpu.cycles : 0;
        st = machine_run(m, left);
        if (st == MACHINE_CYCLE_LIMIT && m->cpu.cycles < m->phase_deadline)
            continue;   /* a feed inside a hook (the C's input wait) moved the deadline on */
        if (st != MACHINE_IDLE || machine_idle(m) == MACHINE_STOP)
            break;
    }
    if (st == MACHINE_IDLE && m->fault != MFAULT_NONE)
        st = MACHINE_FAULT;                   /* the input did not fit */
    machine_set_outputs(m, NULL, NULL);       /* the sinks and the handler live on this stack frame */
    machine_set_raw_output(m, NULL, NULL);
    machine_set_idle_handler(m, NULL, NULL);
    return st;
}

machine_status rom_engine_run(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                              engine_setup_fn setup, void *user, const char *text, bool include_boot,
                              frame_list *produced, frame_list *raw)
{
    engine_input in = { (const uint8_t *)text, strlen(text), true };
    return rom_engine_run_input(m, rom, len, cfg, setup, user, &in, include_boot, produced, raw, NULL);
}

machine_status rom_engine_speak_cfg(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                    const char *text, bool include_boot, frame_list *out)
{
    return rom_engine_run(m, rom, len, cfg, NULL, NULL, text, include_boot, out, NULL);
}

machine_status rom_engine_speak_streams(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                        const char *text, frame_list *produced, frame_list *raw)
{
    return rom_engine_run(m, rom, len, cfg, NULL, NULL, text, false, produced, raw);
}
