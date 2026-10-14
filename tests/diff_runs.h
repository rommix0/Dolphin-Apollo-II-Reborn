#ifndef DIFF_RUNS_H
#define DIFF_RUNS_H
/* The extra runs of the end-to-end differential tests (M4.6c2 spec 4.1, M5a spec 4.3), shared by diff_main (the ROM
   engine against the all-C hybrid) and diff_native (the hybrid against the native engine): each one's input, its
   configuration, an injection at the n-th idle or frame event, the ROM address its ROM run executes and the status it
   ends with. An injection is written against a neutral view of the state, so that it applies to the machine or the
   board alike. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "board/m50734.h"
#include "engine/frame_list.h"

/* What an injection may change: RAM, the m50734 state (its input pins) and the serial queue. */
typedef struct {
    uint8_t *ram;
    m50734 *sfr;
    uint8_t *queue;
    size_t *q_head, *q_tail;
} diff_view;

typedef enum { AT_IDLE, AT_SPEECH } inject_at;   /* the n-th idle; the n-th frame event after the first idle */

/* An extra run: its input, its configuration, an injection at the n-th idle or frame event (inject NULL: none), the
   ROM address its ROM run must execute and the status every engine must end with. */
typedef struct {
    const char *name, *text;
    uint8_t p4, p2, p0;               /* ORed into the default input pins of P4, P2, P0 */
    void (*inject)(const diff_view *v);
    inject_at at;
    unsigned n;
    uint16_t target;
    bool target_ramcode;
    engine_status want;
    uint8_t p3;                       /* ORed into the default input pins of P3 (the baud jumpers) */
} extra_run;

extern const extra_run diff_extra_runs[];
extern const size_t diff_extra_run_count;

#endif
