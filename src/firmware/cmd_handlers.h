#ifndef FW_CMD_HANDLERS_H
#define FW_CMD_HANDLERS_H
/* The deferred command handlers (ROM $A8C0-$ABDE), decompiled (M4.2 spec 2.2). The ROM enters each one by
   JMP (cmd_vector) from cmd_run_next, with X = its cmd_arg_index entry and A = boot_clause[X], and each one ends
   with JMP cmd_run_done, which restores X; cmd_run_due then reloads Y. So a handler has no register output. Only
   the dispatcher (cmd_run.c) calls them. */
#include "fw.h"

typedef void cmd_handler(fw *f, uint8_t x, uint8_t a);

#endif
