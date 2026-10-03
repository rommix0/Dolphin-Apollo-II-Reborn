#ifndef FW_CMD_RUN_H
#define FW_CMD_RUN_H
/* The deferred-command dispatcher (ROM $A880-$A8BF), decompiled (M4.2 spec 2.4). */
#include "fw.h"

/* $A880: x = a cmd_text position. Runs every queued command due there (while cmd_next < cmd_count and
   cmd_next_pos = x), in queue order. A command with a C handler gets cmd_run_next's bookkeeping from the C and
   runs as C. Any other command (bank_cmd_select, an unknown address) runs through the ROM cmd_run_next, which
   does its own bookkeeping. X is kept (cmd_run_done); the ROM leaves Y = cmd_next. */
void cmd_run_due(fw *f, uint8_t x);

#endif
