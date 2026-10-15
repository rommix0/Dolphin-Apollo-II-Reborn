# M4.2 The Command Parser in C — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the firmware's main-level command path to C and prove it against the ROM, live and with the goldens: the `@` matcher, the clause scan, the dispatcher and every deferred handler. Fix the hybrid engine's fault on `@=1` with a non-local exit.

**Architecture:**
- **Bridge.** A ROM call from C that resets the stack ("unwound") abandons the C call chain, and the machine goes on where the ROM went (Task 1).
- **Corpus.** Five new corpus texts with rom-engine goldens reach every command the corpus missed (Task 2).
- **Data.** The command tables become readable C (Task 3).
- **New C files.** The new routines live in `src/firmware/cmd_*.c` and run on the firmware's RAM image through the `fw` context.
- **Dispatcher.** It looks up the handler address in RAM in a C table (`k_handlers` in `cmd_run.c`). A handler without a C port (`bank_cmd_select`, unknown addresses) runs through the ROM `cmd_run_next`.
- **Hooks.** Only the five entries that ROM code still calls are hooked, giving 16 replacements and 17 `golden_hybrid` modes.

**Tech Stack:** C11 with MSVC 2022 (`build.bat`, `/W4 /WX`) and Python 3 (`tools/extract_tables.py`).

**Spec:** `docs/superpowers/specs/2026-10-03-m4-2-command-parser-design.md` (M4.2 spec). Its foundations are:
- the M4.0 spec, `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md` (§8 "As built");
- the M4.1 spec, `docs/superpowers/specs/2026-10-03-m4-1-text-normalisation-design.md` (§7 "As built").

The parent spec is `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 8).

## Global Constraints

**Build and run**
- Build and test from the repo root with the Bash tool (Git Bash): `cmd //c ".\build.bat test [SUITE_FILTER]"`.
- In a git worktree, first run `export APOLLO2_ROM="C:/Users/abart/Desktop/Dolphin Apollo 2 ROM/UK_V2.01_4.04_ROM.BIN"`, because the ROM lives only in the main checkout.
- C11, `/W4 /WX`, `/wd4244` (8-bit truncation is intentional).
  - Fix warnings in code. Never add `/wd` flags or pragmas.
  - The only pragma allowed is the existing 4611 one in `thunks.c`. `setjmp` therefore stays out of the tests.
- Every `.c` file under `src/` and `tests/` must have a **unique basename**, because `build.bat` puts all objects in `build\`.
- Never run `build\apollo2.exe` without `-o FILE`, because it plays audio on the user's speakers.
- Audio stays 10 kHz native, with no resampling.

**What may and may not be committed**
- Never commit the ROM, any `*.BIN`/`*.bin`, `*.mp3`, `apollo2_excerpt.wav`, `*.sla`, `build/` or `apollo_ghidra/`.
- Committed files contain no ROM bytes as hex blobs. Converted data is readable C: strings, numbers, named addresses.
- Stage files by explicit path only. Never use `git add -A` or `git add .`.

**User-owned files and tools**
- Never stage, move or delete the user's untracked files: `docs/*` other than `docs/superpowers/`, `pics/`, `session_memory.json`, `apollo2_for_aksel.wav`, `apollo2_for_aksel.mp3`.
- Never modify anything under `C:\ghidra_12.1.3_PUBLIC`, the user's Ghidra GUI project, or `ghidra740/` (including `ghidra740/coverage/`). Only read the annotations and the dumps.
- Disassembly dumps for checking the C against the ROM (git-ignored, read-only) are in the main checkout:
  - `C:\Users\abart\Desktop\Dolphin Apollo 2 ROM\build\ghidra740\dump\rom_<ADDR>_<name>.txt`
  - `...\dump\ramcode_<ADDR>_<name>.txt`

**Correctness rules**
- The existing goldens in `tests/golden/*.frm` stay byte-identical. The five new goldens are generated once by the rom engine in Task 2. If anything changes another golden, stop and report.
- Nothing under `src/firmware/` includes a `machine/` or `cpu740/` header. `build.bat` checks this.
- The ported C keeps the ROM's behaviour exactly, quirks included (M4.2 spec §2.6):
  - every RAM write, scratch included (`$34`, `$35`, `$75`, `$76`, `$77`, `$78`, `$1809`);
  - the event queue's index arithmetic;
  - reads past the clause end;
  - the X/Y flow through ROM calls.
- Writes to the dead stack (below SP at exit) need not be reproduced, because `diff_lts` excludes it.
- ROM data is read only through `fw_rom8` at its ROM address, from the converted image.
- `diff_lts` failures are fixed by finding the root cause in the dump. Never loosen a comparison or an assertion.

**Commits**
- End every commit message with exactly `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
## File Map

| File | Task | Responsibility |
|---|---|---|
| `src/machine/machine.h`, `src/machine/machine.c`, `src/firmware/fw.h`, `src/firmware/fw.c`, `src/engine/thunks.h`, `src/engine/thunks.c`, `src/engine/hybrid_engine.h`, `src/engine/hybrid_engine.c`, `tests/test_machine_hooks.c`, `tests/test_fw.c`, `tests/test_fw_tn.c`, `tests/test_diff_lts.c`, `tests/test_golden_hybrid.c` | 1 | the non-local exit: an unwound ROM call abandons the C; flush skips only in `tn_convert_clause` |
| `tests/corpus/cmd_misc.txt`, `index_marks.txt`, `cmd_args.txt`, `cmd_unmatched.txt`, `bank_select.txt`, their goldens `tests/golden/<name>.frm`, `tests/corpus_list.h`, `tests/test_golden_hybrid.c` | 2 | five corpus texts with rom-engine goldens; the `bank_select` unwind assertion (`runs_c_dispatch`) |
| `src/firmware/cmd_addr.h`, `src/firmware/data/cmd_data.c`, `src/firmware/data/fw_data.h`, `src/firmware/data/fw_data.c`, `tools/extract_tables.py`, `tests/test_firmware_data.c` | 3 | the command tables as readable C; the ROM code addresses of the command parser |
| `src/firmware/cmd_match.h`, `src/firmware/cmd_match.c`, `src/firmware/fw.h`, `src/engine/thunks.c`, `tests/test_fw_cmd.c`, `tests/suites.h` | 4 | the matcher in C, hooks `cmd_match_table` and `cmd_hex_value`; the unit-test harness |
| `src/firmware/cmd_scan.h`, `src/firmware/cmd_scan.c`, `src/engine/thunks.c`, `tests/test_fw_cmd.c` | 5 | `cmd_scan_clause` in C and its hook |
| `src/firmware/cmd_run.h`, `src/firmware/cmd_run.c`, `src/firmware/cmd_handlers.h`, `src/firmware/tn_convert.c`, `src/firmware/tn_convert.h`, `src/engine/thunks.c`, `tests/test_fw_cmd.c`, `tests/test_fw_tn.c`, `tests/test_golden_hybrid.c` | 6 | the dispatcher in C with the ROM `cmd_run_next` fallback; hook `cmd_run_due`; C calls C from `tn_convert_clause` |
| `src/firmware/cmd_set.c`, `src/firmware/cmd_handlers.h`, `src/firmware/cmd_run.c`, `tests/test_fw_cmd.c` | 7 | the setters without an apply helper |
| `src/firmware/cmd_apply.h`, `src/firmware/cmd_apply.c`, `src/firmware/cmd_set.c`, `src/firmware/cmd_handlers.h`, `src/firmware/cmd_run.c`, `src/engine/thunks.c`, `tests/test_fw_cmd.c` | 8 | the apply helpers, hook `cmd_apply_speaker`, and the setters that use them |
| `src/firmware/cmd_event.c`, `src/firmware/cmd_reply.c`, `src/firmware/cmd_handlers.h`, `src/firmware/cmd_run.c`, `tests/test_fw_cmd.c` | 9 | the event producers and the replies |
| `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`, `docs/superpowers/specs/2026-10-03-m4-2-command-parser-design.md`, `README.md` | 10 | parent spec rev 9, M4.2 status and "As built", README |

---

### Task 1: The non-local exit: an unwound ROM call abandons the C (spec §3)

**Files:**
- Modify: `src/machine/machine.h`, `src/machine/machine.c`, `src/firmware/fw.h`, `src/firmware/fw.c`, `src/engine/thunks.h`, `src/engine/thunks.c`, `src/engine/hybrid_engine.h`, `src/engine/hybrid_engine.c`, `tests/test_machine_hooks.c`, `tests/test_fw.c`, `tests/test_fw_tn.c`, `tests/test_diff_lts.c`, `tests/test_golden_hybrid.c`

**Interfaces:**
- Produces (used by every later task):

```c
/* machine.h */
typedef enum { MCALL_FAILED = 0, MCALL_RETURNED, MCALL_UNWOUND } machine_call_result;
machine_call_result machine_rom_call(machine *m, uint16_t addr, machine_regs *regs, uint64_t max_cycles);
/* fw.h */
typedef enum { FW_CALL_FAILED = 0, FW_CALL_RETURNED, FW_CALL_UNWOUND } fw_call_result;
/* fw_host */ fw_call_result (*rom_call)(void *ctx, uint16_t addr, fw_regs *regs, uint64_t budget);
/* struct fw gains */ bool unwound;
/* thunks.h: fw_bridge gains */ uint64_t unwinds;
/* hybrid_engine.h: hybrid_stats gains */ unsigned unwinds;
```

- Every mock `rom_call` in the tests returns `fw_call_result` from now on.

ROM facts behind this task (checked while writing the plan):
- `bank_cmd_select` ($ABAF) calls `bank_enter` (RAMCODE $0210).
- With a bank present, `bank_enter` does `LDX #$FF / TXS` at $021F/$0221. It then jumps to `bank_entry_warm` ($FFD4 → `boot_reentry_warm`) or, after clearing zero page, to `bank_entry_cold`.
- So the first instruction that lifts SP above any caller's frame is the `TXS` at $0221, and the CPU is then at $0222.
- `apollo2 --engine hybrid -o x.wav "@=1 Hello world."` exits 2 today ("firmware C … at $A880"). `--engine rom` exits 0.

- [ ] **Step 1: Write the failing tests**

`tests/test_machine_hooks.c`:
- In `test_rom_call`, replace `CHECK(machine_rom_call(&g_m, 0xC100, &r, 1000));` with `CHECK_EQ(machine_rom_call(&g_m, 0xC100, &r, 1000), MCALL_RETURNED);`, and replace `CHECK(!machine_rom_call(&g_m, 0xC300, &r2, 1000));   /* never returns: the budget ends it */` with `CHECK_EQ(machine_rom_call(&g_m, 0xC300, &r2, 1000), MCALL_FAILED);   /* never returns: the budget ends it */`.
- In `hook_calls_rom`, replace `CHECK(machine_rom_call(m, 0xC100, &r, 1000));` with `CHECK_EQ(machine_rom_call(m, 0xC100, &r, 1000), MCALL_RETURNED);`.
- In `test_rom_call_real_rom`, replace `CHECK(machine_rom_call(&g_m, 0xC656, &r, 1000));   /* util_shl4: A = A * 16 */` with `CHECK_EQ(machine_rom_call(&g_m, 0xC656, &r, 1000), MCALL_RETURNED);   /* util_shl4: A = A * 16 */`.
- Add above `void suite_machine_hooks(void)`:

```c
static machine_call_result g_outer, g_inner;

/* $C100 hooked: calls $C200 in ROM; $C200: JSR $C300 / RTS. */
static bool hook_outer(machine *m, void *user)
{
    (void)user;
    machine_regs r = { 0, 0, 0, m->cpu.p };
    g_outer = machine_rom_call(m, 0xC200, &r, 1000);
    if (g_outer == MCALL_RETURNED)
        machine_return(m);
    return true;   /* unwound: the machine goes on where the ROM went */
}

/* $C300 hooked: calls $C400 in ROM, which resets the stack: LDX #$FF / TXS / JMP $9007. */
static bool hook_inner(machine *m, void *user)
{
    (void)user;
    machine_regs r = { 0, 0, 0, m->cpu.p };
    g_inner = machine_rom_call(m, 0xC400, &r, 1000);
    if (g_inner == MCALL_RETURNED)
        machine_return(m);
    return true;
}

/* A ROM call whose routine lifts SP above the call's frame (a stack reset) can never return: it reports
   MCALL_UNWOUND at once, with PC and SP where the routine left them (M4.2 spec 3). A hook inside an outer
   rom_call that unwinds unwinds the outer call too. */
static void test_rom_call_unwound(void)
{
    static const uint8_t reset[] = { 0xA2, 0xFF, 0x9A, 0x4C, 0x07, 0x90 };   /* LDX #$FF / TXS / JMP $9007 */
    static const uint8_t call_c300[] = { 0x20, 0x00, 0xC3, 0x60 };            /* JSR $C300 / RTS */

    /* direct: a hook at $C100 calls $C400 */
    load_main(0xC100);
    put(0xC400, reset, sizeof reset);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    g_inner = MCALL_FAILED;
    CHECK(machine_hook(&g_m, 0xC100, hook_inner, NULL));
    run_to(&g_m, 0xC100);
    CHECK(machine_step(&g_m));                   /* the hook runs; its ROM call unwinds */
    CHECK_EQ(g_inner, MCALL_UNWOUND);
    CHECK_EQ(g_m.cpu.pc, 0xC403);                /* right after the TXS */
    CHECK_EQ(g_m.cpu.s, 0xFF);
    run_to(&g_m, 0x9007);                        /* and the machine runs on from there */
    CHECK_EQ(g_m.ram[0x10], 0x00);               /* the STA after the JSR never ran */

    /* nested: hook $C100 -> ROM $C200 -> JSR $C300 hooked -> ROM $C400 resets the stack */
    load_main(0xC100);
    put(0xC200, call_c300, sizeof call_c300);
    put(0xC400, reset, sizeof reset);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    g_outer = g_inner = MCALL_FAILED;
    CHECK(machine_hook(&g_m, 0xC100, hook_outer, NULL));
    CHECK(machine_hook(&g_m, 0xC300, hook_inner, NULL));
    run_to(&g_m, 0xC100);
    CHECK(machine_step(&g_m));
    CHECK_EQ(g_inner, MCALL_UNWOUND);
    CHECK_EQ(g_outer, MCALL_UNWOUND);            /* the outer call sees the same reset */
    CHECK_EQ(g_m.cpu.pc, 0xC403);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    CHECK_EQ(g_m.fault, MFAULT_NONE);
}
```

- In `suite_machine_hooks`, add `test_rom_call_unwound();` after `test_rom_call();`.

`tests/test_fw.c`:
- Replace the whole `host_call` function with:

```c
static fw_call_result g_call_result = FW_CALL_RETURNED;

static fw_call_result host_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    (void)ctx;
    (void)addr;
    r->a = 0x42;
    g_budget = budget;
    return g_call_result;
}
```

- After the line `CHECK(!f.fault);` that follows `CHECK_EQ(g_budget, FW_ROM_CALL_BUDGET);`, add:

```c
    g_call_result = FW_CALL_UNWOUND;   /* the ROM reset the stack: abandoned, not a fault (M4.2 spec 3) */
    fw_init(&f, g_ram, &g_img, &caller);
    CHECK(!f.unwound);
    CHECK(!fw_rom_call(&f, 0xA893, &r, FW_ROM_CALL_BUDGET));
    CHECK(f.unwound);
    CHECK(!f.fault);
    g_call_result = FW_CALL_FAILED;
    fw_init(&f, g_ram, &g_img, &caller);
    CHECK(!fw_rom_call(&f, 0xA893, &r, FW_ROM_CALL_BUDGET));
    CHECK(f.fault);
    CHECK(!f.unwound);
    g_call_result = FW_CALL_RETURNED;
```

`tests/test_fw_tn.c`: replace the end of `mock_rom_call` and its return type. The signature line becomes `static fw_call_result mock_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)`, and the return statement becomes:

```c
    return addr == ROM_ISR_PAR_READY || addr == ROM_CMD_RUN_DUE || addr == ROM_PROS_MAKE_SEGMENTS ||
                   addr == ROM_BOOT_LOOP_PLAY
               ? FW_CALL_RETURNED
               : FW_CALL_FAILED;   /* registers come back unchanged, as these leave X alone */
```

`tests/test_golden_hybrid.c`, in `test_thunk_faults`, after case (3) (after `CHECK_EQ(g_m.fault, MFAULT_NONE);`), add:

```c
    /* (4) a ROM call from the C that resets the stack (bank_enter's LDX #$FF / TXS): the C is abandoned, no
       fault; the machine goes on where the ROM went (M4.2 spec 3) */
    CHECK(machine_init(&g_m, rom, len, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    CHECK(machine_hook(&g_m, ROM_ISR_PAR_READY, reset_stack_hook, NULL));
    r = fw_replacement_find("tn_convert_clause");
    CHECK(r != NULL);
    if (!r)
        return;
    g_m.cpu.pc = r->addr;
    g_m.cpu.s = 0xF0;                            /* a caller's frame below the stack reset */
    CHECK(r->thunk(&g_m, &b));
    CHECK_EQ(g_m.fault, MFAULT_NONE);
    CHECK_EQ(g_m.cpu.pc, BOOT_RESET_ADDR);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    CHECK_EQ(b.unwinds, 1);
    CHECK(!b.f.unwound);
    CHECK(b.f.trap == NULL);
```

and above `test_thunk_faults` add:

```c
#define BOOT_RESET_ADDR 0xA61C   /* any ROM address: where reset_stack_hook "jumps" */

/* Stands for bank_enter's stack reset: SP = $FF and a jump elsewhere. */
static bool reset_stack_hook(machine *m, void *user)
{
    (void)user;
    m->cpu.s = 0xFF;
    m->cpu.pc = BOOT_RESET_ADDR;
    return true;
}
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test machine_hooks"`
Expected: a compile error, because `MCALL_RETURNED` and `machine_call_result` are undefined.

- [ ] **Step 3: Implement**

`src/machine/machine.h`: replace the comment and declaration of `machine_rom_call` with:

```c
/* Calls the routine at addr with A/X/Y/P from regs, as if by JSR from MACHINE_ROM_CALL_SENTINEL, and steps
   until it returns there. Hooks, interrupts (unless held) and frame capture work normally. MCALL_RETURNED: regs
   holds the exit registers, the PC is restored and the other registers keep their exit values. MCALL_UNWOUND:
   an instruction lifted SP above the call's frame without returning to it (a stack reset, M4.2 spec 3), so the
   routine can never return; the call stops right after that instruction, with PC and SP where the routine left
   them. MCALL_FAILED: a fault, or max_cycles passed first. Precondition: it does not clear cpu.waiting, so it
   must not be called while the CPU is waiting at WIT (hooks never fire then). */
typedef enum { MCALL_FAILED = 0, MCALL_RETURNED, MCALL_UNWOUND } machine_call_result;
machine_call_result machine_rom_call(machine *m, uint16_t addr, machine_regs *regs, uint64_t max_cycles);
```

`src/machine/machine.c`: replace `machine_rom_call` with:

```c
machine_call_result machine_rom_call(machine *m, uint16_t addr, machine_regs *regs, uint64_t max_cycles)
{
    cpu740 *c = &m->cpu;
    uint16_t saved_pc = c->pc, ret = (uint16_t)(MACHINE_ROM_CALL_SENTINEL - 1);   /* RTS adds 1 */
    uint8_t s0 = c->s;
    m->ram[0x100 + c->s] = (uint8_t)(ret >> 8);   /* pushed like JSR: high byte first */
    c->s--;
    m->ram[0x100 + c->s] = (uint8_t)ret;
    c->s--;
    c->a = regs->a;
    c->x = regs->x;
    c->y = regs->y;
    c->p = regs->p;
    c->pc = addr;
    uint64_t end = c->cycles + max_cycles;
    while (!(c->pc == MACHINE_ROM_CALL_SENTINEL && c->s == s0)) {
        if (c->cycles >= end || !machine_step(m))
            return MCALL_FAILED;
        if (c->s > s0 && c->pc != MACHINE_ROM_CALL_SENTINEL)
            return MCALL_UNWOUND;   /* the sentinel's frame was popped without returning to it */
    }
    regs->a = c->a;
    regs->x = c->x;
    regs->y = c->y;
    regs->p = c->p;
    c->pc = saved_pc;
    return MCALL_RETURNED;
}
```

`src/firmware/fw.h`:
- Before `/* The machine or native host. ...`, add:

```c
/* How a ROM call from C ended: returned; failed (a fault, or out of budget); or unwound: the ROM reset the
   stack past the call's own frame (bank_enter, M4.2 spec 3), so it can never return. */
typedef enum { FW_CALL_FAILED = 0, FW_CALL_RETURNED, FW_CALL_UNWOUND } fw_call_result;
```

- In `fw_host`, replace the `rom_call` member and its comment with:

```c
    /* Runs the routine at addr, still in ROM, to its RTS, within budget cycles. A, X and Y in and out; the
       routine starts with the machine's current flags, and p returns its exit flags. */
    fw_call_result (*rom_call)(void *ctx, uint16_t addr, fw_regs *regs, uint64_t budget);
```

- In `struct fw`, after `jmp_buf *trap;` and its comment, add:

```c
    bool unwound;                  /* a ROM call reset the stack: the C was abandoned (not a fault; M4.2 spec 3) */
```

- Replace the comment of `fw_rom_call` with:

```c
/* Calls a routine still in ROM through the host, within budget cycles. True when it returned. Without a host, or
   when the call fails, a fault. When the ROM reset the stack (unwound), f->unwound is set and the routine is
   abandoned like a fault (a longjmp to f->trap), but it is not a fault. */
```

`src/firmware/fw.c`:
- In `fw_init`, after `f->trap = NULL;`, add `f->unwound = false;`.
- Replace `fw_rom_call` with:

```c
bool fw_rom_call(fw *f, uint16_t addr, fw_regs *regs, uint64_t budget)
{
    fw_call_result r = FW_CALL_FAILED;
    if (f->host && f->host->rom_call)
        r = f->host->rom_call(f->host->ctx, addr, regs, budget);
    if (r == FW_CALL_RETURNED)
        return true;
    if (r == FW_CALL_UNWOUND) {
        f->unwound = true;   /* the ROM abandoned this call chain, and so does the C */
        if (f->trap)
            longjmp(*f->trap, 1);
        return false;
    }
    fault(f, addr);
    return false;
}
```

`src/engine/thunks.h`:
- Replace the `fw_bridge` typedef line with:

```c
/* The firmware context of one machine: its RAM, the converted data image, the emulator as host.
   thunk_calls: thunks run so far; unwinds: C call chains abandoned at a stack reset (M4.2 spec 3). */
typedef struct fw_bridge { machine *m; fw f; fw_host host; uint64_t thunk_calls, unwinds; } fw_bridge;
```

- In the header comment, replace `maps the routine's registers to its C function, emulates the RTS.` with `maps the routine's registers to its C function and emulates the RTS, unless a ROM call from the C reset the stack (M4.2 spec 3): then the machine goes on where the ROM went.`

`src/engine/thunks.c`:
- Replace `host_rom_call` with:

```c
static fw_call_result host_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    fw_bridge *b = ctx;
    machine_regs mr = { r->a, r->x, r->y, b->m->cpu.p };   /* the routine starts with the machine's flags */
    machine_call_result res = machine_rom_call(b->m, addr, &mr, budget);
    r->a = mr.a;
    r->x = mr.x;
    r->y = mr.y;
    r->p = mr.p;
    b->f.low_window = window_on(b->m);
    if (res == MCALL_RETURNED)
        return FW_CALL_RETURNED;
    return res == MCALL_UNWOUND ? FW_CALL_UNWOUND : FW_CALL_FAILED;
}
```

- In `fw_bridge_init`, after `b->thunk_calls = 0;`, add `b->unwinds = 0;`.
- In `run_thunk`, after `b->f.trap = saved;`, add:

```c
    if (b->f.unwound) {   /* a ROM call reset the stack: the C is abandoned, the machine goes on where the ROM went */
        b->f.unwound = false;
        b->unwinds++;
        return true;
    }
```

- Replace the comment above `run_thunk` with:

```c
/* The frame every thunk shares: set the fault trap (saving an outer thunk's, as a ROM call from C can reach
   another thunk on the same bridge), sync the window, run the routine; then restore the trap and either leave the
   machine where an unwound ROM call left it, report the fault, or emulate the RTS. */
```

`src/engine/hybrid_engine.h`: in `hybrid_stats`, after the `tn_flushes` member, add:

```c
    unsigned unwinds;       /* C call chains abandoned at a stack reset (@= entering a bank, M4.2 spec 3) */
```

`src/engine/hybrid_engine.c`: after `stats->tn_flushes = h.bridge.f.tn_flushes;`, add `stats->unwinds = (unsigned)h.bridge.unwinds;`.

`tests/test_golden_hybrid.c`: in `check`, replace `hybrid_stats stats = { 0, 0 };` with `hybrid_stats stats = { 0, 0, 0 };`.

`tests/test_diff_lts.c` (a flush is legal only in `tn_convert_clause`; this closes the M4.1 follow-up):
- Change `#define DIFF_MAX        16` to `#define DIFF_MAX        24`.
- In `observe`, replace:

```c
        if (g_flushed) {
            g_flush_skips++;   /* golden_hybrid covers this call end to end */
            return false;
        }
        g_checked[o - g_obs]++;
        snprintf(why, sizeof why, "the ROM routine did not return (fault %d)", (int)g_ref.fault);
```

with:

```c
        if (g_flushed && !strcmp(o->r->name, "tn_convert_clause")) {
            g_flush_skips++;   /* golden_hybrid covers this call end to end */
            return false;
        }
        g_checked[o - g_obs]++;
        if (g_flushed)
            snprintf(why, sizeof why, "a mid-clause flush outside tn_convert_clause");
        else
            snprintf(why, sizeof why, "the ROM routine did not return (fault %d)", (int)g_ref.fault);
```

- In the header comment, replace `held: it is skipped and counted (golden_hybrid covers it, M4.1 spec 5.1). */` with `held: it is skipped and counted (golden_hybrid covers it, M4.1 spec 5.1), in tn_convert_clause only. A call whose ROM copy resets the stack (@= entering a bank) stops right after the reset in both copies and is compared like any other (M4.2 spec 3). */`.

- [ ] **Step 4: Run the tests**

Run, in turn:
- `cmd //c ".\build.bat test machine_hooks"`
- `cmd //c ".\build.bat test fw"`
- `cmd //c ".\build.bat test golden_hybrid"`
- `cmd //c ".\build.bat test"`

Expected:
- `suite_machine_hooks`, `suite_fw`, `suite_fw_lts` and `suite_fw_tn` are ok.
- `golden_hybrid` prints `27 corpus files x 12 modes`, with no frame differences.
- All 29 suites are ok, with no warnings.

Then check the regression end to end, with `SP` set to a scratch directory outside the repo:

```bash
./build/apollo2.exe --engine rom -o "$SP/eq1_rom.wav" "@=1 Hello world."; echo $?
./build/apollo2.exe --engine hybrid -o "$SP/eq1_hybrid.wav" "@=1 Hello world."; echo $?
cmp "$SP/eq1_rom.wav" "$SP/eq1_hybrid.wav" && echo same
```

Expected: `0`, `0`, `same`.

- [ ] **Step 5: Commit**

```bash
git add src/machine/machine.h src/machine/machine.c src/firmware/fw.h src/firmware/fw.c src/engine/thunks.h src/engine/thunks.c src/engine/hybrid_engine.h src/engine/hybrid_engine.c tests/test_machine_hooks.c tests/test_fw.c tests/test_fw_tn.c tests/test_diff_lts.c tests/test_golden_hybrid.c
git commit -m "Non-local exit: a ROM call that resets the stack abandons the C (fixes the hybrid fault on @=1); flush skips only in tn_convert_clause

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: New corpus texts and their goldens (spec §5.1)

**Files:**
- Create: `tests/corpus/cmd_misc.txt`, `tests/corpus/index_marks.txt`, `tests/corpus/cmd_args.txt`, `tests/corpus/cmd_unmatched.txt`, `tests/corpus/bank_select.txt`, and their five goldens `tests/golden/<name>.frm` (generated)
- Modify: `tests/corpus_list.h`, `tests/test_golden_hybrid.c`

**Interfaces:**
- Consumes: Task 1's `hybrid_stats.unwinds`.
- Produces:
  - the corpus names `"cmd_misc"`, `"index_marks"`, `"cmd_args"`, `"cmd_unmatched"` and `"bank_select"`;
  - `static bool runs_c_dispatch(const char *only)` in `tests/test_golden_hybrid.c`, which Task 6 extends with `"cmd_run_due"`.

The texts were checked while writing this plan. With `apollo2_coverage`, each reaches these addresses, and none of them is reached by the existing corpus or boot:

| Text | Addresses only it reaches |
|---|---|
| `cmd_misc` | `$AB99` (`cmd_echo`), `$ABBE` (`cmd_list_languages`), `$ABDC` (`cmd_ignore`), `$C5A4` (`ser_tx_records`) |
| `index_marks` | `$AB2E` (`cmd_mark_index`), `$BBA0` (`cmd_ev_index`), `$C483`, `$C495`, `$C46A` (the immediate path) |
| `cmd_args` | `$B450` (`cmd_hex_byte`), `$A9FE` (the `@R` letter path), `$AA7C` (`@V0`), `$C52B` (`:` as Z) |
| `cmd_unmatched` | `$AD58` (an unmatched `@` stays in the text) |
| `bank_select` | `$ABAF` (`bank_cmd_select`), RAMCODE `$0210`–`$0232` (`bank_enter`), `$FFD4` (the warm re-entry) |

All five render under `--engine rom`. Four already render identically under `--engine hybrid`. `bank_select` faulted before Task 1.

- [ ] **Step 1: Write the corpus texts**

Each file holds exactly one line, ending with a newline:

| File | Line |
|---|---|
| `tests/corpus/cmd_misc.txt` | `@E! @L @G1 @I^ @+x Hello there.` |
| `tests/corpus/index_marks.txt` | `@I+ One. @I+ Two. @I? Three.` |
| `tests/corpus/cmd_args.txt` | `@RA80 @V0 Silent. @V1 @Z?@C?@N? @Q: Back. @A: Loud.` |
| `tests/corpus/cmd_unmatched.txt` | `Price @ ten @ 5. @u1+ @u12+ Go.` |
| `tests/corpus/bank_select.txt` | `Bank five. @=5 Still here. @=1 Gone.` |

- [ ] **Step 2: Add them to the corpus list**

In `tests/corpus_list.h`:
- Replace the last line of `CORPUS_NAMES`, `"formant_mod", "version", "volume", "mute", "queries", "long_clause"`, with:

```c
    "formant_mod", "version", "volume", "mute", "queries", "long_clause", \
    "cmd_misc", "index_marks", "cmd_args", "cmd_unmatched", "bank_select"
```

- Replace the comment line `   long_clause (M4.1) is one clause long enough to fill tn_phonetic, so tn_convert_clause speaks it mid-clause. */` with:

```c
   long_clause (M4.1) is one clause long enough to fill tn_phonetic, so tn_convert_clause speaks it mid-clause.
   The last five (M4.2) reach the commands the rest never does: replies and ignored commands, the immediate
   @I+ / @I?, unusual arguments, an @ that matches no command, and @= (an empty socket, then a bank entered). */
```

- [ ] **Step 3: Run the golden suite to see it fail**

Run: `cmd //c ".\build.bat test golden"`
Expected: FAIL, with a `missing tests/golden/<name>.frm: run once with APOLLO2_UPDATE_GOLDEN=1 and review` line for each of the five.

- [ ] **Step 4: Generate the goldens with the rom engine**

Run: `APOLLO2_UPDATE_GOLDEN=1 cmd //c ".\build.bat test golden"`

Then run `git status --short tests/golden`. Expected: exactly five lines, `?? tests/golden/<name>.frm` for the five new names.

If any existing `.frm` shows as modified, STOP and report: the oracle is not deterministic, and nothing may be committed.

- [ ] **Step 5: Confirm the coverage**

Run (the coverage file goes under `build/`, which is git-ignored):

```bash
./build/apollo2_coverage.exe -o build/m42.cov
for a in AB99 ABBE ABDC C5A4 AB2E BBA0 C483 C495 C46A B450 A9FE AA7C C52B AD58 ABAF FFD4; do grep -q "^x rom $a$" build/m42.cov && echo "$a ok" || echo "$a MISSING"; done
grep -c "^x ramcode 0221$" build/m42.cov
```

Expected: 16 `ok` lines and `1`. If any address is MISSING, STOP and report.

- [ ] **Step 6: Assert the unwind in `golden_hybrid`**

In `tests/test_golden_hybrid.c`:
- Change `check` to return the whole `hybrid_stats`. Replace its comment, signature and the first early return:

```c
/* Compares the hybrid frames of one corpus file with its golden; returns what the C did. */
static hybrid_stats check(const uint8_t *rom, size_t len, const char *only, const char *name, const char *text,
                          bool boot)
{
    hybrid_stats stats = { 0, 0, 0 };
    char path[128];
    snprintf(path, sizeof path, "tests/golden/%s.frm", name);
    char *want = test_read_file(path);
    CHECK(want != NULL);
    if (!want)
        return stats;
    frame_list fl = { 0 };
```

  Delete the old line `hybrid_stats stats = { 0, 0, 0 };` further down, and change the final `return stats.tn_flushes;` to `return stats;`.

- Above `check_corpus`, add:

```c
/* The modes in which the C reaches cmd_run_due's dispatch, so @= entering a bank resets the stack under C and the
   C call chain unwinds (M4.2 spec 5.3). */
static bool runs_c_dispatch(const char *only)
{
    return !only || !strcmp(only, "tn_convert_clause");
}
```

- In `check_corpus`, replace:

```c
        unsigned flushes = check(rom, len, only, k_corpus[c], text, false);
        if (!strcmp(k_corpus[c], "long_clause") && (!only || !strcmp(only, "tn_convert_clause")) && flushes == 0) {
```

  with:

```c
        hybrid_stats st = check(rom, len, only, k_corpus[c], text, false);
        if (!strcmp(k_corpus[c], "bank_select") && runs_c_dispatch(only) && st.unwinds == 0) {
            fprintf(stderr, "  hybrid (%s) bank_select: the C never unwound at the bank entry\n", only ? only : "all");
            g_test_failures++;
        }
        if (!strcmp(k_corpus[c], "long_clause") && (!only || !strcmp(only, "tn_convert_clause")) &&
            st.tn_flushes == 0) {
```

- In the header comment, after `(all-routines mode and tn_convert_clause alone; M4.1 spec 5.2).`, add ` On bank_select, @=1 enters a bank: where the C runs the dispatch, its call chain must unwind (M4.2 spec 5.3).`

- [ ] **Step 7: Run the full suite**

Run: `cmd //c ".\build.bat test"`
Expected:
- All 29 suites are ok.
- `golden_hybrid` prints `32 corpus files x 12 modes`.
- `diff_lts` shows 0 mismatches and `1 mid-clause flush calls skipped`. The `bank_select` calls of `tn_convert_clause` that reset the stack are compared, not skipped.
- `suite_synth` includes the new texts in its corpus peak. If it reports clipping, or a peak outside −12…−1 dBFS, STOP and report. Do not change any calibration.

- [ ] **Step 8: Commit**

```bash
git add tests/corpus/cmd_misc.txt tests/corpus/index_marks.txt tests/corpus/cmd_args.txt tests/corpus/cmd_unmatched.txt tests/corpus/bank_select.txt tests/golden/cmd_misc.frm tests/golden/index_marks.frm tests/golden/cmd_args.frm tests/golden/cmd_unmatched.frm tests/golden/bank_select.frm tests/corpus_list.h tests/test_golden_hybrid.c
git commit -m "Corpus: five texts for the commands the corpus never reached (replies, @I+/@I?, arguments, a stray @, @=), goldens from the rom engine; golden_hybrid asserts the bank entry unwinds the C

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: The command data as readable C (spec §4)

**Files:**
- Create: `src/firmware/cmd_addr.h`, `src/firmware/data/cmd_data.c` (generated by the tool, then reviewed)
- Modify: `tools/extract_tables.py`, `src/firmware/data/fw_data.h`, `src/firmware/data/fw_data.c`, `tests/test_firmware_data.c`

**Interfaces:**
- Consumes: `fw_ram.h` names (through `fw.h`), the M4.0 encoder helpers in `fw_data.c` (`put`, `put_text`, `put_word`, `fail`).
- Produces:
  - `src/firmware/cmd_addr.h` with exactly the conventions' names: the 24 deferred handlers (`ROM_CMD_SAY_VERSION` … `ROM_CMD_IGNORE`, and `ROM_BANK_CMD_SELECT`), the 2 immediate handlers (`ROM_CMD_IMM_INDEX_ADD`, `ROM_CMD_IMM_INDEX_QUERY`), the 5 event handlers (`ROM_CMD_EV_INDEX`, `ROM_CMD_EV_SOUND_START`, `ROM_CMD_EV_SOUND_END`, `ROM_CMD_EV_SET_VOICING`, `ROM_CMD_EV_SET_VOLUME`) and the 5 ROM routines (`ROM_CMD_RUN_NEXT`, `ROM_PAR_SETUP_VOICE`, `ROM_SER_TX_PUT`, `ROM_SER_TX_HEX`, `ROM_SER_TX_RECORDS`). `cmd_data.c` includes it here; Task 5's tests, Task 6's `cmd_run.c` and `tests/test_fw_tn.c`, Task 8's `cmd_apply.c` and Task 9's `cmd_event.c` and `cmd_reply.c` include it later.
  - In `fw_data.h`: the data addresses `CMD_IMM_PATTERNS 0x989C`, `CMD_IMM_TABLE 0x98A7`, `CMD_PATTERNS 0x98AD`, `CMD_TABLE 0x994F`, `CMD_QUERY_TABLE 0x998F`, `CMD_CASE_MAP 0x99B4`, `CMD_SPEED_MAPS 0xC87B`, `CMD_SPEED_LIMITS 0xC88F`, `PAR_VOICE_A_COUNT 0xD469`, `PAR_VOICE_B_COUNT 0xE5E2`, plus three that are not converted: `CMD_SPEED_MAP_DATA 0xC898` (the base of the speed-map pointers), and `PAR_VOICE_A 0xD46A` and `PAR_VOICE_B 0xE5E3` (the M4.4 voice tables, whose address alone `cmd_select_voice_a`/`_b` write). The C reads the tables only through `fw_rom8` at these addresses: Task 4 reads `CMD_IMM_TABLE`, `CMD_TABLE` and `CMD_CASE_MAP`; Task 5 `CMD_TABLE`; Task 8 `CMD_SPEED_MAPS`, `CMD_SPEED_LIMITS`, `PAR_VOICE_A_COUNT` and `PAR_VOICE_B_COUNT` (and writes `PAR_VOICE_A`/`PAR_VOICE_B` as values); Task 9 `CMD_QUERY_TABLE`.
  - In `fw_data.h`: the types `fw_cmd_pattern`, `fw_cmd_query`, `fw_case_fold`; the arrays `fw_cmd_imm_patterns[]`, `fw_cmd_patterns[]`, `fw_cmd_table[]`, `fw_cmd_query_table[]`, `fw_cmd_case_folds[]`, `fw_cmd_speed_maps[10]`, `fw_cmd_speed_limits[9]`, the bytes `fw_par_voice_a_count`, `fw_par_voice_b_count`, and their counts. Only the encoder and `test_firmware_data` use them.
  - Encoder step 7 in `fw_image_build`.

**ROM facts checked** (byte by byte in `UK_V2.01_4.04_ROM.BIN`, file offset = CPU address, and in the dumps):
- **Immediate block, $989C–$98AC (17 bytes):** `I+` 0, handler $C483 (bytes `49 2B 00 83 C4`) at $989C; `I?` 0, handler $C495 at $98A1; the `$FF` end at $98A6; `cmd_imm_table` at $98A7 = 3 pointers $989C, $98A1, $98A6. The pointer table ends with the pointer to the end marker, exactly like `cmd_table`. `cmd_imm_match` loads it at $C437/$C43A (`LDM #$A7`, `LDM #$98`).
- **Deferred records, $98AD–$994E (162 bytes):** 30 records (pattern, 0, handler low, high), then `$FF` at $994E. The record order is exactly the `cmd_table` order with the duplicate removed: `G#` ($993F) is stored once.
- **`cmd_table`, $994F–$998E (64 bytes):** 32 pointers. Entries 0–27 point at records 0–27 in order; **entry 28 points at $993F again** (record 27, `G#`); entries 29–30 point at records 28–29 (`I^`, `+*`); entry 31 points at the end marker $994E. The survey's "entries 27 and 28 both point at $993F" is right (0-based). `cmd_scan_clause` loads it at $AD36/$AD39.
- **`cmd_query_table`, $998F–$99B3 (37 bytes):** 18 (letter, zero-page) pairs then 0, in the order A B C D F H K M P Q R S V W X Z = $; every address has exactly one `fw_ram.h` name. `cmd_reply_setting` reads both `$998F,Y` and `$9990,Y` (the setting byte) — Task 9 reads `CMD_QUERY_TABLE + 1`.
- **`cmd_case_map`, $99B4–$9AB3:** 21 folds from the identity, exactly the survey's list (CR $0D and ESC $1B stay; `[`→`(`, `]`→`)`; a–z; DEL; the 7 high-code folds; 10 runs of high codes to space). It ends where the M4.0 `lts_char_class` ($9AB4) begins, so `$9804–$9BB3` becomes one converted block.
- **`cmd_speed_maps` $C87B (20 bytes) and `cmd_speed_limits` $C88F (9 bytes):** pointers $C898 + 32·i for i = 0–8, and a 10th equal to the 9th ($C998); limits 7, 7, 7, 7, 4, 2, 0, 0, 0. The map data at $C898 is annotated `cmd_speed_map_data` (the spec's `ROM_PROS_SPEED_MAPS` is named `CMD_SPEED_MAP_DATA` here, after the annotation). `cmd_apply_speed` reads `$C88F,Y` and `$C87B,Y`/`$C87C,Y` ($A851, $A85A, $A85F).
- **Voice counts:** $D469 = $E5E2 = 86 ($56). `cmd_select_voice_a`/`_b` read them at $A802/$A818. They also load the table addresses $D46A / $E5E3 as **immediates** (`par_voice_a`, `par_voice_b`); those are code constants, not data, and are not converted here. They get the names `PAR_VOICE_A` and `PAR_VOICE_B` in `fw_data.h`, which Task 8's `cmd_select_voice_a`/`_b` write into `par_table_ptr`.
- **Code addresses:** every `cmd_addr.h` address has a dump of that name in `build/ghidra740/dump/` (`rom_A902_cmd_say_version.txt` … `rom_C5A4_ser_tx_records.txt`). The event handler addresses are the producers' immediates: `cmd_sound` $A8E5–$A8F4 (`#$A7 #$BB`, `#$80 #$BC`), `cmd_set_volume` $AB1E/$AB23 (`#$ED #$BC`), `cmd_set_voicing` $AB89/$AB8E (`#$EA #$BC`), `cmd_mark_index` $AB3D/$AB42 (`#$A0 #$BB`). No address in the conventions differs from the ROM.
- **Byte count:** 17 + 162 + 64 + 37 + 256 + 29 + 2 = 567, so `compared` goes from 35,311 to 35,878.

- [ ] **Step 1: Write the failing test**

In `tests/test_firmware_data.c`:
- Replace the `k_rom` definition:

```c
static const range k_rom[] = { { 0x4000, 0x8FF4 }, { 0x9804, 0x989C }, { 0x9AB4, 0x9BB4 }, { 0xACC7, 0xACE8 },
                               { 0xF75B, 0xF77B } };
```

with:

```c
static const range k_rom[] = { { 0x4000, 0x8FF4 }, { 0x9804, 0x9BB4 }, { 0xACC7, 0xACE8 }, { 0xC87B, 0xC898 },
                               { 0xD469, 0xD46A }, { 0xE5E2, 0xE5E3 }, { 0xF75B, 0xF77B } };
```

- In `test_counts`, after `CHECK_EQ(fw_phonetic_text_count, 5);`, add:

```c
    CHECK_EQ(fw_cmd_imm_pattern_count, 2);
    CHECK_EQ(fw_cmd_pattern_count, 30);
    CHECK_EQ(fw_cmd_table_count, 31);   /* G# is listed twice */
    CHECK_EQ(fw_cmd_table[27], fw_cmd_table[28]);
    CHECK_EQ(fw_cmd_query_count, 18);
    CHECK_EQ(fw_cmd_case_fold_count, 21);
```

- In `test_against_rom`, replace:

```c
    CHECK_EQ(compared, 35311);   /* M4.0's 35,214 plus the five phonetic texts (97 bytes) */
```

with:

```c
    CHECK_EQ(compared, 35878);   /* M4.0's 35,214, M4.1's phonetic texts (97) and M4.2's command data (567) */
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmd //c ".\build.bat test firmware_data"`
Expected: a compile error, `error C2065: 'fw_cmd_imm_pattern_count': undeclared identifier` (and the other new names).

- [ ] **Step 3: Create `src/firmware/cmd_addr.h`**

```c
#ifndef FW_CMD_ADDR_H
#define FW_CMD_ADDR_H
/* ROM code addresses of the command parser (M4.2 spec 2), checked against build/ghidra740/dump. They are code,
   not data: the pattern records hold the handler addresses, the scan copies them into cmd_handlers and the event
   producers write the event handlers into cmd_ev_handlers, byte for byte; the C dispatcher switches on them. */

/* Deferred command handlers, in pattern order: entered by cmd_run_next's JMP (cmd_vector) with X = the argument's
   boot_clause index and A = boot_clause[X]; each ends with JMP cmd_run_done ($A8BD) */
#define ROM_CMD_SAY_VERSION       0xA902   /* "?" */
#define ROM_CMD_QUERY             0xABD3   /* "*?" */
#define ROM_CMD_SET_VOLUME        0xAB05   /* "A*" */
#define ROM_CMD_SET_PHRASE_PAUSE  0xA91F   /* "D*" */
#define ROM_CMD_ECHO              0xAB99   /* "E*" */
#define ROM_CMD_SET_PITCH         0xAA24   /* "F*" */
#define ROM_CMD_SET_HYPER         0xAB4D   /* "H#" */
#define ROM_CMD_SET_MARK_SPACE    0xA99A   /* "M**" */
#define ROM_CMD_SET_PUNCT         0xAAF5   /* "P#" */
#define ROM_CMD_SET_WORD_PAUSE    0xA98A   /* "Q#" */
#define ROM_CMD_SET_PROSODY       0xA9DC   /* "R#", "R***" */
#define ROM_CMD_SET_SPELL         0xAA4F   /* "S#" */
#define ROM_CMD_SET_VOICE         0xAA74   /* "V#" */
#define ROM_CMD_SET_SPEED         0xA9C9   /* "W#" */
#define ROM_CMD_SET_PHONETIC      0xAB9F   /* "X#" */
#define ROM_CMD_SET_FORMANT       0xA92F   /* "U#*+", "U#*-", "U#**+", "U#**-" */
#define ROM_CMD_SET_SOURCE        0xAB5D   /* "$#" */
#define ROM_CMD_SET_VOICING       0xAB70   /* "B#" */
#define ROM_CMD_MARK_INDEX        0xAB2E   /* "I+" */
#define ROM_CMD_SOUND             0xA8C0   /* "T*" */
#define ROM_BANK_CMD_SELECT       0xABAF   /* "=#": bank switching (P2), stays in ROM */
#define ROM_CMD_LIST_LANGUAGES    0xABBE   /* "L" */
#define ROM_CMD_SET_SPEAKER       0xAA61   /* "K#" */
#define ROM_CMD_IGNORE            0xABDC   /* "G#" (listed twice in cmd_table), "I^", "+*" */

/* Immediate command handlers: cmd_imm_match's JMP (cmd_imm_vector) at interrupt level (stay in ROM) */
#define ROM_CMD_IMM_INDEX_ADD     0xC483   /* "I+" */
#define ROM_CMD_IMM_INDEX_QUERY   0xC495   /* "I?" */

/* Command event handlers: queued in cmd_ev_handlers by the event producers, run at playback by cmd_run_event
   (stay in ROM) */
#define ROM_CMD_EV_INDEX          0xBBA0   /* cmd_mark_index */
#define ROM_CMD_EV_SOUND_START    0xBBA7   /* cmd_sound, first event */
#define ROM_CMD_EV_SOUND_END      0xBC80   /* cmd_sound, second event */
#define ROM_CMD_EV_SET_VOICING    0xBCEA   /* cmd_set_voicing */
#define ROM_CMD_EV_SET_VOLUME     0xBCED   /* cmd_set_volume */

/* ROM routines the C calls through fw_rom_call (isr_par_ready: ROM_ISR_PAR_READY in tn_convert.h) */
#define ROM_CMD_RUN_NEXT          0xA893   /* dispatch cmd_handlers[cmd_next] (the dispatcher's fallback); X kept */
#define ROM_PAR_SETUP_VOICE       0xB3A4   /* rebuild the voice set-up and both DSP frames (M4.4/M4.5) */
#define ROM_SER_TX_PUT            0xC4DD   /* queue A as a reply byte; A, X, Y preserved */
#define ROM_SER_TX_HEX            0xC571   /* queue A as two ASCII hex digits, low nibble first; A preserved */
#define ROM_SER_TX_RECORDS        0xC5A4   /* queue the record reply ser_reply_id/_count/_size/_ptr describe */

#endif
```

- [ ] **Step 4: Declare the addresses, types and arrays in `src/firmware/data/fw_data.h`**

- After the `fw_phonetic_text` typedef line (`typedef struct { const char *name; uint16_t addr, size; const char *text, *tail; } fw_phonetic_text;`), add:

```c
/* A command pattern record (M4.2 spec 4): the pattern (* any character, # a digit, both matched on the case-mapped
   text), its 0 end, then the handler's ROM address (cmd_addr.h), low byte first. */
typedef struct { const char *pattern; uint16_t handler; } fw_cmd_pattern;
/* A cmd_query_table entry: a setting letter and the zero-page address of the setting (fw_ram.h). */
typedef struct { char letter; uint8_t setting; } fw_cmd_query;
/* A fold of cmd_case_map: the codes first..last map to `to`, or with shift each code c to to + (c - first). */
typedef struct { uint8_t first, last, to; bool shift; } fw_case_fold;
```

- After the line `#define LTS_CHAR_CLASS     0x9AB4`, add:

```c

/* Pinned addresses of the command data (ghidra740/annotations: command.txt, params.txt; M4.2 spec 4) */
#define CMD_IMM_PATTERNS    0x989C   /* the immediate pattern records, then their $FF end */
#define CMD_IMM_TABLE       0x98A7   /* ptr[3]: each immediate record, then the end */
#define CMD_PATTERNS        0x98AD   /* the deferred pattern records, then their $FF end */
#define CMD_TABLE           0x994F   /* ptr[32]: the records in match order, then the end */
#define CMD_QUERY_TABLE     0x998F   /* 18 x (letter, zero-page setting), then 0 */
#define CMD_CASE_MAP        0x99B4   /* byte[256] */
#define CMD_SPEED_MAPS      0xC87B   /* ptr[10], indexed by speed - 1 */
#define CMD_SPEED_LIMITS    0xC88F   /* byte[9], indexed by speed - 1 */
#define CMD_SPEED_MAP_DATA  0xC898   /* not converted (prosody's, M4.3): cmd_speed_maps point into it */
#define PAR_VOICE_A_COUNT   0xD469   /* byte: the phoneme count heading the voice A table (M4.4) */
#define PAR_VOICE_B_COUNT   0xE5E2   /* byte: the same for voice B */
#define PAR_VOICE_A         0xD46A   /* the M4.4 voice A table, not converted; the C writes only its address */
#define PAR_VOICE_B         0xE5E3   /* the M4.4 voice B table, not converted; the C writes only its address */
```

- After `extern const uint8_t lts_char_class[256];`, add:

```c
extern const fw_cmd_pattern fw_cmd_imm_patterns[], fw_cmd_patterns[];
extern const size_t fw_cmd_imm_pattern_count, fw_cmd_pattern_count;
extern const uint8_t fw_cmd_table[];   /* cmd_table in match order, as indices into fw_cmd_patterns */
extern const size_t fw_cmd_table_count;
extern const fw_cmd_query fw_cmd_query_table[];
extern const size_t fw_cmd_query_count;
extern const fw_case_fold fw_cmd_case_folds[];
extern const size_t fw_cmd_case_fold_count;
extern const uint16_t fw_cmd_speed_maps[10];
extern const uint8_t fw_cmd_speed_limits[9];
extern const uint8_t fw_par_voice_a_count, fw_par_voice_b_count;
```

No name collides with `fw_ram.h` (it has `CMD_SPEED_MAP`, `CMD_SPEED_LIMIT`, `CMD_TABLE_PTR`, `CMD_IMM_VECTOR`, all different).

- [ ] **Step 5: Extend the converter and generate the data file**

`tools/extract_tables.py`:
- Replace the module docstring:

```python
"""One-time converter (M4.0 spec 4.2): the ROM's character rules, letter-to-sound rules, affix lists and
character classes -> readable C in src/firmware/data/. The output is a first draft: review it, annotate it by
hand if useful, and commit it. Nothing runs this at build or test time; tests/test_firmware_data.c checks the
committed C against the ROM.
usage: python tools/extract_tables.py [--phonetic-only] [ROM]   (default $APOLLO2_ROM, else UK_V2.01_4.04_ROM.BIN)"""
```

with:

```python
"""One-time converter (M4.0 spec 4.2): the ROM's character rules, letter-to-sound rules, affix lists and
character classes, the phonetic texts (M4.1) and the command tables (M4.2) -> readable C in src/firmware/data/.
The output is a first draft: review it, annotate it by hand if useful, and commit it. Nothing runs this at build
or test time; tests/test_firmware_data.c checks the committed C against the ROM.
usage: python tools/extract_tables.py [--phonetic-only | --command-only] [ROM]
       (ROM: default $APOLLO2_ROM, else UK_V2.01_4.04_ROM.BIN)"""
```

- After the `PHONETIC_TEXTS = [...]` block (its last line is `]`, after `("cmd_version_lang_text", 0xF75B, 32),`), add:

```python
# The command data (M4.2 spec 4), as annotated in ghidra740/annotations/command.txt and params.txt
CMD_IMM_PATTERNS, CMD_IMM_TABLE, CMD_PATTERNS, CMD_TABLE = 0x989C, 0x98A7, 0x98AD, 0x994F
CMD_QUERY_TABLE, CMD_CASE_MAP = 0x998F, 0x99B4
CMD_SPEED_MAPS, CMD_SPEED_LIMITS, CMD_SPEED_MAP_DATA = 0xC87B, 0xC88F, 0xC898
PAR_VOICE_A_COUNT, PAR_VOICE_B_COUNT = 0xD469, 0xE5E2
# The handlers of the pattern records, named as in src/firmware/cmd_addr.h
CMD_HANDLERS = {
    0xC483: "ROM_CMD_IMM_INDEX_ADD", 0xC495: "ROM_CMD_IMM_INDEX_QUERY",
    0xA902: "ROM_CMD_SAY_VERSION", 0xABD3: "ROM_CMD_QUERY", 0xAB05: "ROM_CMD_SET_VOLUME",
    0xA91F: "ROM_CMD_SET_PHRASE_PAUSE", 0xAB99: "ROM_CMD_ECHO", 0xAA24: "ROM_CMD_SET_PITCH",
    0xAB4D: "ROM_CMD_SET_HYPER", 0xA99A: "ROM_CMD_SET_MARK_SPACE", 0xAAF5: "ROM_CMD_SET_PUNCT",
    0xA98A: "ROM_CMD_SET_WORD_PAUSE", 0xA9DC: "ROM_CMD_SET_PROSODY", 0xAA4F: "ROM_CMD_SET_SPELL",
    0xAA74: "ROM_CMD_SET_VOICE", 0xA9C9: "ROM_CMD_SET_SPEED", 0xAB9F: "ROM_CMD_SET_PHONETIC",
    0xA92F: "ROM_CMD_SET_FORMANT", 0xAB5D: "ROM_CMD_SET_SOURCE", 0xAB70: "ROM_CMD_SET_VOICING",
    0xAB2E: "ROM_CMD_MARK_INDEX", 0xA8C0: "ROM_CMD_SOUND", 0xABAF: "ROM_BANK_CMD_SELECT",
    0xABBE: "ROM_CMD_LIST_LANGUAGES", 0xAA61: "ROM_CMD_SET_SPEAKER", 0xABDC: "ROM_CMD_IGNORE",
}
```

- Above `def main():` (after `write_phonetic_texts`, keeping two blank lines between functions), add:

```python
def cchar(c):
    """A code as a C character constant if it is printable ASCII, else in hex."""
    if 0x20 <= c < 0x7F and chr(c) not in "'\\":
        return "'%s'" % chr(c)
    return "0x%02X" % c


def zero_page_names():
    """Address -> name of the zero-page variables, from src/firmware/fw_ram.h."""
    names = {}
    with open(os.path.join("src", "firmware", "fw_ram.h"), encoding="utf-8") as f:
        for line in f:
            p = line.split()
            if len(p) >= 3 and p[0] == "#define" and p[2].startswith("0x") and int(p[2], 16) < 0x100:
                names[int(p[2], 16)] = p[1]
    return names


def write_command_data(rom):
    """src/firmware/data/cmd_data.c: the command parser's tables (M4.2 spec 4). The pattern records are kept;
    the $FF ends, the pointer tables and the query table's 0 end are left to the encoder."""
    def word(a):
        return rom[a] | rom[a + 1] << 8

    def records(at):
        """The pattern records from at to the $FF end: [(address, pattern, handler)], and the end's address."""
        recs = []
        while rom[at] != 0xFF:
            n = rom.index(0, at) - at
            assert n > 0, "empty pattern at $%04X" % at
            recs.append((at, rom[at:at + n], word(at + n + 1)))
            at += n + 3
        return recs, at

    def pointers(at, recs, end):
        """The pointer table at at, up to the pointer to end: indices into recs, and the address after it."""
        starts, order = [a for a, _, _ in recs], []
        while word(at) != end:
            assert word(at) in starts, "pointer at $%04X is no pattern record" % at
            order.append(starts.index(word(at)))
            at += 2
        return order, at + 2

    def emit_records(lines, name, recs):
        lines.append("const fw_cmd_pattern %s[] = {" % name)
        for i, (a, pat, h) in enumerate(recs):
            assert h in CMD_HANDLERS, "pattern at $%04X: handler $%04X has no name" % (a, h)
            entry = "    { %s, %s }," % (cstr(pat), CMD_HANDLERS[h])
            lines.append(entry.ljust(48) + "/* %2d: $%04X */" % (i, a))
        lines.append("};")
        lines.append("const size_t %s_count = sizeof %s / sizeof %s[0];" % (name[:-1], name, name))
        lines.append("")

    head = HEADER.replace("M4.0 spec section 4", "M4.2 spec section 4")
    lines = [head.replace('#include "fw_data.h"\n', '#include "fw_data.h"\n#include "../cmd_addr.h"\n').rstrip("\n")
             + "\n"]
    imm, end = records(CMD_IMM_PATTERNS)
    order, at = pointers(CMD_IMM_TABLE, imm, end)
    assert end + 1 == CMD_IMM_TABLE and order == list(range(len(imm))) and at == CMD_PATTERNS, "cmd_imm_table"
    lines += ["/* cmd_imm_patterns ($%04X): the immediate commands, matched at interrupt level by cmd_imm_match."
              % CMD_IMM_PATTERNS,
              "   The encoder writes the $FF end, then cmd_imm_table ($%04X): each record's address, then the"
              % CMD_IMM_TABLE,
              "   end's. */"]
    emit_records(lines, "fw_cmd_imm_patterns", imm)

    recs, end = records(CMD_PATTERNS)
    order, at = pointers(CMD_TABLE, recs, end)
    assert end + 1 == CMD_TABLE and at == CMD_QUERY_TABLE, "cmd_table"
    lines += ["/* The deferred command patterns ($%04X), in ROM order: * matches any character, # a digit, both on"
              % CMD_PATTERNS,
              "   the case-mapped text. A record is the pattern, its 0 end and its handler; the encoder writes the",
              "   $FF end after the last. */"]
    emit_records(lines, "fw_cmd_patterns", recs)
    lines += ["/* cmd_table ($%04X): the patterns in match order (first match wins), as indices into" % CMD_TABLE,
              "   fw_cmd_patterns. The encoder writes each record's address, then the address of the $FF end. */",
              "const uint8_t fw_cmd_table[] = {"]
    seen = set()
    for i, k in enumerate(order):
        again = " again: the ROM lists this record twice" if k in seen else ""
        seen.add(k)
        lines.append(("    %d," % k).ljust(10) + "/* %2d: \"%s\"%s */" % (i, recs[k][1].decode("ascii"), again))
    lines += ["};", "const size_t fw_cmd_table_count = sizeof fw_cmd_table / sizeof fw_cmd_table[0];", ""]

    zp, at, query = zero_page_names(), CMD_QUERY_TABLE, []
    while rom[at]:
        assert rom[at + 1] in zp, "cmd_query_table at $%04X: no RAM name for $%02X" % (at, rom[at + 1])
        query.append((rom[at], zp[rom[at + 1]]))
        at += 2
    assert at + 1 == CMD_CASE_MAP, "cmd_query_table"
    lines += ["/* cmd_query_table ($%04X): the settings a @c? query reports, as the letter and the zero-page"
              % CMD_QUERY_TABLE,
              "   setting. The encoder writes the 0 end. */",
              "const fw_cmd_query fw_cmd_query_table[] = {"]
    lines += ["    { %s, %s }," % (cchar(letter), name) for letter, name in query]
    lines += ["};", "const size_t fw_cmd_query_count = sizeof fw_cmd_query_table / sizeof fw_cmd_query_table[0];", ""]

    cmap, folds, c = rom[CMD_CASE_MAP:CMD_CASE_MAP + 256], [], 0
    while c < 256:
        if cmap[c] == c:
            c += 1
            continue
        shift = c < 255 and cmap[c + 1] == cmap[c] + 1 and cmap[c + 1] != c + 1
        last = c
        while last < 255 and cmap[last + 1] != last + 1 and \
                cmap[last + 1] == (cmap[c] + last + 1 - c if shift else cmap[c]):
            last += 1
        folds.append((c, last, cmap[c], shift))
        c = last + 1
    lines += ["/* cmd_case_map ($%04X): the identity, except these folds { first, last, to, shift }: the codes"
              % CMD_CASE_MAP,
              "   first..last map to to, or with shift each code c to to + (c - first). The encoder writes all",
              "   256 codes. */",
              "const fw_case_fold fw_cmd_case_folds[] = {"]
    for first, last, to, shift in folds:
        span = "$%02X" % first if first == last else "$%02X-$%02X" % (first, last)
        dest = "$%02X-$%02X" % (to, to + last - first) if shift else "space" if to == 0x20 else "$%02X" % to
        entry = "    { %s, %s, %s, %s }," % (cchar(first), cchar(last), cchar(to), "true" if shift else "false")
        lines.append(entry.ljust(40) + "/* %s to %s */" % (span, dest))
    lines += ["};", "const size_t fw_cmd_case_fold_count = sizeof fw_cmd_case_folds / sizeof fw_cmd_case_folds[0];",
              ""]

    assert CMD_SPEED_MAPS + 20 == CMD_SPEED_LIMITS and CMD_SPEED_LIMITS + 9 == CMD_SPEED_MAP_DATA
    maps = [(word(CMD_SPEED_MAPS + 2 * i) - CMD_SPEED_MAP_DATA) // 32 for i in range(10)]
    assert all(word(CMD_SPEED_MAPS + 2 * i) == CMD_SPEED_MAP_DATA + 32 * k and 0 <= k < 9
               for i, k in enumerate(maps)), "cmd_speed_maps"
    lines += ["/* cmd_speed_maps ($%04X): per speed 1-9 (indexed by speed - 1) its 32-byte duration map in"
              % CMD_SPEED_MAPS,
              "   cmd_speed_map_data, which is prosody's (M4.3) and not converted here */",
              "const uint16_t fw_cmd_speed_maps[10] = {"]
    for i, k in enumerate(maps):
        note = "speed %d" % (i + 1) if k == i else "entry %d repeats entry %d" % (i + 1, k + 1)
        lines.append(("    CMD_SPEED_MAP_DATA + 32 * %d," % k).ljust(36) + "/* %s */" % note)
    lines += ["};",
              "/* cmd_speed_limits ($%04X): per speed 1-9 the value cmd_apply_speed puts in cmd_speed_limit */"
              % CMD_SPEED_LIMITS,
              "const uint8_t fw_cmd_speed_limits[9] = { %s };"
              % ", ".join(str(v) for v in rom[CMD_SPEED_LIMITS:CMD_SPEED_LIMITS + 9]),
              "",
              "/* par_voice_a_count ($%04X), par_voice_b_count ($%04X): the phoneme counts at the head of the"
              % (PAR_VOICE_A_COUNT, PAR_VOICE_B_COUNT),
              "   voice tables (M4.4), read by cmd_select_voice_a and cmd_select_voice_b */",
              "const uint8_t fw_par_voice_a_count = %d, fw_par_voice_b_count = %d;"
              % (rom[PAR_VOICE_A_COUNT], rom[PAR_VOICE_B_COUNT])]
    with open(os.path.join(OUT, "cmd_data.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote cmd_data.c in", OUT)
```

- In `main()`, replace:

```python
    args = [a for a in sys.argv[1:] if a != "--phonetic-only"]
    path = args[0] if args else os.environ.get("APOLLO2_ROM", "UK_V2.01_4.04_ROM.BIN")
    rom = open(path, "rb").read()
    assert len(rom) == 0x10000, "need the 64 KB ROM"
    write_phonetic_texts(rom)
    if "--phonetic-only" in sys.argv:   # M4.1: leave the reviewed M4.0 tables alone
        return
```

with:

```python
    args = [a for a in sys.argv[1:] if a not in ("--phonetic-only", "--command-only")]
    path = args[0] if args else os.environ.get("APOLLO2_ROM", "UK_V2.01_4.04_ROM.BIN")
    rom = open(path, "rb").read()
    assert len(rom) == 0x10000, "need the 64 KB ROM"
    if "--command-only" in sys.argv:   # M4.2: leave the reviewed M4.0 and M4.1 files alone
        write_command_data(rom)
        return
    write_phonetic_texts(rom)
    if "--phonetic-only" in sys.argv:   # M4.1: leave the reviewed M4.0 tables alone
        return
    write_command_data(rom)
```

The tool's asserts check the layout facts listed above (each block ends where the next begins, every `cmd_table` pointer is a record start, every handler and every query address has a name, the speed maps are `$C898 + 32·k`). It reads the RAM names from `src/firmware/fw_ram.h`, so run it from the repo root.

Run: `python tools/extract_tables.py --command-only "$APOLLO2_ROM"`
Expected: `wrote cmd_data.c in src\firmware\data`. `git status --short src/firmware/data` must show exactly ` M src/firmware/data/fw_data.h` and `?? src/firmware/data/cmd_data.c`: the M4.0 and M4.1 data files are untouched. (A full run without a flag would overwrite the hand-reviewed M4.0 files; do not run it.)

The generated file must read exactly (review it against this):

```c
/* Converted from the Apollo 2 ROM by tools/extract_tables.py (M4.2 spec section 4), then reviewed.
   tests/test_firmware_data.c re-encodes it and compares it byte for byte with the ROM. */
#include <stddef.h>
#include "fw_data.h"
#include "../cmd_addr.h"

/* cmd_imm_patterns ($989C): the immediate commands, matched at interrupt level by cmd_imm_match.
   The encoder writes the $FF end, then cmd_imm_table ($98A7): each record's address, then the
   end's. */
const fw_cmd_pattern fw_cmd_imm_patterns[] = {
    { "I+", ROM_CMD_IMM_INDEX_ADD },            /*  0: $989C */
    { "I\?", ROM_CMD_IMM_INDEX_QUERY },         /*  1: $98A1 */
};
const size_t fw_cmd_imm_pattern_count = sizeof fw_cmd_imm_patterns / sizeof fw_cmd_imm_patterns[0];

/* The deferred command patterns ($98AD), in ROM order: * matches any character, # a digit, both on
   the case-mapped text. A record is the pattern, its 0 end and its handler; the encoder writes the
   $FF end after the last. */
const fw_cmd_pattern fw_cmd_patterns[] = {
    { "\?", ROM_CMD_SAY_VERSION },              /*  0: $98AD */
    { "*\?", ROM_CMD_QUERY },                   /*  1: $98B1 */
    { "A*", ROM_CMD_SET_VOLUME },               /*  2: $98B6 */
    { "D*", ROM_CMD_SET_PHRASE_PAUSE },         /*  3: $98BB */
    { "E*", ROM_CMD_ECHO },                     /*  4: $98C0 */
    { "F*", ROM_CMD_SET_PITCH },                /*  5: $98C5 */
    { "H#", ROM_CMD_SET_HYPER },                /*  6: $98CA */
    { "M**", ROM_CMD_SET_MARK_SPACE },          /*  7: $98CF */
    { "P#", ROM_CMD_SET_PUNCT },                /*  8: $98D5 */
    { "Q#", ROM_CMD_SET_WORD_PAUSE },           /*  9: $98DA */
    { "R#", ROM_CMD_SET_PROSODY },              /* 10: $98DF */
    { "R***", ROM_CMD_SET_PROSODY },            /* 11: $98E4 */
    { "S#", ROM_CMD_SET_SPELL },                /* 12: $98EB */
    { "V#", ROM_CMD_SET_VOICE },                /* 13: $98F0 */
    { "W#", ROM_CMD_SET_SPEED },                /* 14: $98F5 */
    { "X#", ROM_CMD_SET_PHONETIC },             /* 15: $98FA */
    { "U#*+", ROM_CMD_SET_FORMANT },            /* 16: $98FF */
    { "U#*-", ROM_CMD_SET_FORMANT },            /* 17: $9906 */
    { "U#**+", ROM_CMD_SET_FORMANT },           /* 18: $990D */
    { "U#**-", ROM_CMD_SET_FORMANT },           /* 19: $9915 */
    { "$#", ROM_CMD_SET_SOURCE },               /* 20: $991D */
    { "B#", ROM_CMD_SET_VOICING },              /* 21: $9922 */
    { "I+", ROM_CMD_MARK_INDEX },               /* 22: $9927 */
    { "T*", ROM_CMD_SOUND },                    /* 23: $992C */
    { "=#", ROM_BANK_CMD_SELECT },              /* 24: $9931 */
    { "L", ROM_CMD_LIST_LANGUAGES },            /* 25: $9936 */
    { "K#", ROM_CMD_SET_SPEAKER },              /* 26: $993A */
    { "G#", ROM_CMD_IGNORE },                   /* 27: $993F */
    { "I^", ROM_CMD_IGNORE },                   /* 28: $9944 */
    { "+*", ROM_CMD_IGNORE },                   /* 29: $9949 */
};
const size_t fw_cmd_pattern_count = sizeof fw_cmd_patterns / sizeof fw_cmd_patterns[0];

/* cmd_table ($994F): the patterns in match order (first match wins), as indices into
   fw_cmd_patterns. The encoder writes each record's address, then the address of the $FF end. */
const uint8_t fw_cmd_table[] = {
    0,    /*  0: "?" */
    1,    /*  1: "*?" */
    2,    /*  2: "A*" */
    3,    /*  3: "D*" */
    4,    /*  4: "E*" */
    5,    /*  5: "F*" */
    6,    /*  6: "H#" */
    7,    /*  7: "M**" */
    8,    /*  8: "P#" */
    9,    /*  9: "Q#" */
    10,   /* 10: "R#" */
    11,   /* 11: "R***" */
    12,   /* 12: "S#" */
    13,   /* 13: "V#" */
    14,   /* 14: "W#" */
    15,   /* 15: "X#" */
    16,   /* 16: "U#*+" */
    17,   /* 17: "U#*-" */
    18,   /* 18: "U#**+" */
    19,   /* 19: "U#**-" */
    20,   /* 20: "$#" */
    21,   /* 21: "B#" */
    22,   /* 22: "I+" */
    23,   /* 23: "T*" */
    24,   /* 24: "=#" */
    25,   /* 25: "L" */
    26,   /* 26: "K#" */
    27,   /* 27: "G#" */
    27,   /* 28: "G#" again: the ROM lists this record twice */
    28,   /* 29: "I^" */
    29,   /* 30: "+*" */
};
const size_t fw_cmd_table_count = sizeof fw_cmd_table / sizeof fw_cmd_table[0];

/* cmd_query_table ($998F): the settings a @c? query reports, as the letter and the zero-page
   setting. The encoder writes the 0 end. */
const fw_cmd_query fw_cmd_query_table[] = {
    { 'A', CMD_VOLUME },
    { 'B', CMD_VOICING },
    { 'C', BOOT_SELFTEST_FAIL },
    { 'D', CMD_PHRASE_PAUSE },
    { 'F', CMD_PITCH },
    { 'H', CMD_HYPER },
    { 'K', CMD_SPEAKER },
    { 'M', CMD_MARK_SPACE },
    { 'P', CMD_PUNCT },
    { 'Q', CMD_WORD_PAUSE },
    { 'R', CMD_PROSODY },
    { 'S', CMD_SPELL },
    { 'V', CMD_VOICE },
    { 'W', CMD_SPEED },
    { 'X', CMD_PHONETIC },
    { 'Z', BOOT_DIP },
    { '=', BANK_CURRENT },
    { '$', CMD_SOURCE },
};
const size_t fw_cmd_query_count = sizeof fw_cmd_query_table / sizeof fw_cmd_query_table[0];

/* cmd_case_map ($99B4): the identity, except these folds { first, last, to, shift }: the codes
   first..last map to to, or with shift each code c to to + (c - first). The encoder writes all
   256 codes. */
const fw_case_fold fw_cmd_case_folds[] = {
    { 0x00, 0x0C, ' ', false },         /* $00-$0C to space */
    { 0x0E, 0x1A, ' ', false },         /* $0E-$1A to space */
    { 0x1C, 0x1F, ' ', false },         /* $1C-$1F to space */
    { '[', '[', '(', false },           /* $5B to $28 */
    { ']', ']', ')', false },           /* $5D to $29 */
    { 'a', 'z', 'A', true },            /* $61-$7A to $41-$5A */
    { 0x7F, 0x7F, ' ', false },         /* $7F to space */
    { 0x81, 0x81, 0x9A, false },        /* $81 to $9A */
    { 0x82, 0x82, 0x90, false },        /* $82 to $90 */
    { 0x84, 0x84, 0x8E, false },        /* $84 to $8E */
    { 0x86, 0x86, 0x8F, false },        /* $86 to $8F */
    { 0x87, 0x87, 0x80, false },        /* $87 to $80 */
    { 0x91, 0x91, 0x92, false },        /* $91 to $92 */
    { 0x94, 0x94, 0x99, false },        /* $94 to $99 */
    { 0x9B, 0x9B, ' ', false },         /* $9B to space */
    { 0x9D, 0x9F, ' ', false },         /* $9D-$9F to space */
    { 0xA8, 0xAA, ' ', false },         /* $A8-$AA to space */
    { 0xAD, 0xBF, ' ', false },         /* $AD-$BF to space */
    { 0xC1, 0xDF, ' ', false },         /* $C1-$DF to space */
    { 0xF9, 0xFA, ' ', false },         /* $F9-$FA to space */
    { 0xFE, 0xFF, ' ', false },         /* $FE-$FF to space */
};
const size_t fw_cmd_case_fold_count = sizeof fw_cmd_case_folds / sizeof fw_cmd_case_folds[0];

/* cmd_speed_maps ($C87B): per speed 1-9 (indexed by speed - 1) its 32-byte duration map in
   cmd_speed_map_data, which is prosody's (M4.3) and not converted here */
const uint16_t fw_cmd_speed_maps[10] = {
    CMD_SPEED_MAP_DATA + 32 * 0,    /* speed 1 */
    CMD_SPEED_MAP_DATA + 32 * 1,    /* speed 2 */
    CMD_SPEED_MAP_DATA + 32 * 2,    /* speed 3 */
    CMD_SPEED_MAP_DATA + 32 * 3,    /* speed 4 */
    CMD_SPEED_MAP_DATA + 32 * 4,    /* speed 5 */
    CMD_SPEED_MAP_DATA + 32 * 5,    /* speed 6 */
    CMD_SPEED_MAP_DATA + 32 * 6,    /* speed 7 */
    CMD_SPEED_MAP_DATA + 32 * 7,    /* speed 8 */
    CMD_SPEED_MAP_DATA + 32 * 8,    /* speed 9 */
    CMD_SPEED_MAP_DATA + 32 * 8,    /* entry 10 repeats entry 9 */
};
/* cmd_speed_limits ($C88F): per speed 1-9 the value cmd_apply_speed puts in cmd_speed_limit */
const uint8_t fw_cmd_speed_limits[9] = { 7, 7, 7, 7, 4, 2, 0, 0, 0 };

/* par_voice_a_count ($D469), par_voice_b_count ($E5E2): the phoneme counts at the head of the
   voice tables (M4.4), read by cmd_select_voice_a and cmd_select_voice_b */
const uint8_t fw_par_voice_a_count = 86, fw_par_voice_b_count = 86;
```

If the tool's output differs from this, the ROM wins. Commit the tool's output and report the difference.

- [ ] **Step 6: Encoder step 7 in `src/firmware/data/fw_data.c`**

- After `put_word` (before `bool fw_image_build`), add:

```c
/* Command pattern records from at, then the $FF end; addr[i] = where record i landed. Returns the end's address. */
static uint32_t put_cmd_patterns(enc *e, uint32_t at, const fw_cmd_pattern *p, size_t n, uint16_t *addr)
{
    for (size_t i = 0; i < n; i++) {
        if (!p[i].pattern[0] || (uint8_t)p[i].pattern[0] == 0xFF)
            fail(e, "command pattern at $%04X is empty or starts with $FF", (unsigned)at);
        addr[i] = (uint16_t)at;
        at = put_text(e, false, at, p[i].pattern, false);
        put(e, false, at++, 0x00);
        put_word(e, at, p[i].handler);
        at += 2;
    }
    put(e, false, at, 0xFF);
    return at;
}

/* A pattern pointer table at at: the records in order (indices into addr; order NULL: each record once), then the
   end's address. Returns the address after the table. */
static uint32_t put_cmd_table(enc *e, uint32_t at, const uint16_t *addr, size_t n, const uint8_t *order,
                              size_t entries, uint32_t end)
{
    for (size_t i = 0; i < entries; i++) {
        size_t k = order ? order[i] : i;
        if (k >= n) {
            fail(e, "pointer table entry at $%04X names pattern %u of %u", (unsigned)at, (unsigned)k, (unsigned)n);
            return at;
        }
        put_word(e, at, addr[k]);
        at += 2;
    }
    put_word(e, at, (uint16_t)end);
    return at + 2;
}

static void check_end(enc *e, const char *what, uint32_t at, uint32_t next)
{
    if (at != next)
        fail(e, "%s ends at $%04X, the next table starts at $%04X", what, (unsigned)at, (unsigned)next);
}
```

- In `fw_image_build`, before `    return e.ok;`, add (after step 6, the phonetic texts):

```c
    /* 7. The command data (M4.2 spec 4): the pattern records with each table's $FF end and pointer table, the
       query table, cmd_case_map, the speed tables and the two voice counts. Each block ends where the next
       begins. */
    {
        uint16_t pat[64];
        if (fw_cmd_imm_pattern_count > 64 || fw_cmd_pattern_count > 64) {
            fail(&e, "more than 64 command patterns");
            return false;
        }
        uint32_t end = put_cmd_patterns(&e, CMD_IMM_PATTERNS, fw_cmd_imm_patterns, fw_cmd_imm_pattern_count, pat);
        check_end(&e, "cmd_imm_patterns", end + 1, CMD_IMM_TABLE);
        uint32_t at = put_cmd_table(&e, CMD_IMM_TABLE, pat, fw_cmd_imm_pattern_count, NULL,
                                    fw_cmd_imm_pattern_count, end);
        check_end(&e, "cmd_imm_table", at, CMD_PATTERNS);
        end = put_cmd_patterns(&e, CMD_PATTERNS, fw_cmd_patterns, fw_cmd_pattern_count, pat);
        check_end(&e, "fw_cmd_patterns", end + 1, CMD_TABLE);
        at = put_cmd_table(&e, CMD_TABLE, pat, fw_cmd_pattern_count, fw_cmd_table, fw_cmd_table_count, end);
        check_end(&e, "cmd_table", at, CMD_QUERY_TABLE);
        at = CMD_QUERY_TABLE;
        for (size_t i = 0; i < fw_cmd_query_count; i++) {
            if (!fw_cmd_query_table[i].letter)
                fail(&e, "cmd_query_table entry %u has letter 0", (unsigned)i);
            put(&e, false, at++, (uint8_t)fw_cmd_query_table[i].letter);
            put(&e, false, at++, fw_cmd_query_table[i].setting);
        }
        put(&e, false, at++, 0x00);
        check_end(&e, "cmd_query_table", at, CMD_CASE_MAP);
        uint8_t map[256];
        bool folded[256] = { false };
        for (int c = 0; c < 256; c++)
            map[c] = (uint8_t)c;
        for (size_t i = 0; i < fw_cmd_case_fold_count; i++) {
            const fw_case_fold *f = &fw_cmd_case_folds[i];
            if (f->first > f->last)
                fail(&e, "case fold %u: first above last", (unsigned)i);
            for (int c = f->first; c <= f->last; c++) {
                int to = f->shift ? f->to + (c - f->first) : f->to;
                if (to > 0xFF || to == c || folded[c])
                    fail(&e, "case fold %u: $%02X folded twice, to itself or past $FF", (unsigned)i, (unsigned)c);
                folded[c] = true;
                map[c] = (uint8_t)to;
            }
        }
        for (int c = 0; c < 256; c++)
            put(&e, false, CMD_CASE_MAP + (uint32_t)c, map[c]);
        for (int i = 0; i < 10; i++)
            put_word(&e, CMD_SPEED_MAPS + 2u * (uint32_t)i, fw_cmd_speed_maps[i]);
        for (int i = 0; i < 9; i++)
            put(&e, false, CMD_SPEED_LIMITS + (uint32_t)i, fw_cmd_speed_limits[i]);
        put(&e, false, PAR_VOICE_A_COUNT, fw_par_voice_a_count);
        put(&e, false, PAR_VOICE_B_COUNT, fw_par_voice_b_count);
    }
```

Notes for the reviewer: the case-fold loop uses `int` (not `unsigned`) so the `to == c` test has no signed/unsigned comparison under `/W4`; `last = 0xFF` terminates because `c` is an `int`. Each fold must change its codes (`to == c` is an error), and no code may be folded twice, so the list is the exact set of exceptions. The query loop starts at `CMD_QUERY_TABLE` even if `cmd_table` ended elsewhere, so a layout error is reported by `check_end` and does not cascade.

- [ ] **Step 7: Run the tests**

Run: `cmd //c ".\build.bat test firmware_data"`
Expected: `35878 converted bytes compared with the ROM, 0 differ` and `suite_firmware_data` ok. (The `build.bat` syntax check of `src\firmware` without `/Isrc` passes: `cmd_data.c` includes `"../cmd_addr.h"`, a relative path.)

Then run: `cmd //c ".\build.bat test"`
Expected: all 29 suites ok, with no warnings; `golden_hybrid` prints `32 corpus files x 12 modes (all, then each routine alone)`. No other suite reads the new arrays; the image's other bytes are unchanged, so `fw_lts`, `fw_tn`, `diff_lts` and `golden_hybrid` are unaffected.

- [ ] **Step 8: Commit**

```bash
git add src/firmware/cmd_addr.h src/firmware/data/cmd_data.c src/firmware/data/fw_data.h src/firmware/data/fw_data.c tools/extract_tables.py tests/test_firmware_data.c
git commit -m "Converted data: the command tables (patterns, cmd_table, query table, case map, speed tables, voice counts) as readable C; cmd_addr.h (M4.2 4)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: The matcher in C, hooks for `cmd_match_table` and `cmd_hex_value`, and `tests/test_fw_cmd.c` (spec §2.2, §2.3, §2.6, §5.4)

**Files:**
- Create: `src/firmware/cmd_match.h`, `src/firmware/cmd_match.c`, `tests/test_fw_cmd.c`
- Modify: `src/firmware/fw.h` (four scratch names), `src/engine/thunks.c`, `tests/suites.h` (append `SUITE(suite_fw_cmd)`)

**Interfaces:**
- Consumes:
  - Task 1's `fw_call_result` (`FW_CALL_FAILED`, `FW_CALL_RETURNED`, `FW_CALL_UNWOUND`), the return type of `fw_host.rom_call`;
  - Task 3's `CMD_IMM_TABLE`, `CMD_TABLE` and `CMD_CASE_MAP` (`data/fw_data.h`), and the converted bytes at `$989C-$9AB3`;
  - M4.0/M4.1's `fw_ind_y`, `fw_rom8`, `fw_rd16`/`fw_wr16`, `FW_TMP_75`, `FW_TMP_78` and `ROM_ISR_PAR_READY` (`tn_convert.h`).
- Produces (used by Tasks 5-9):

```c
/* fw.h */
#define FW_TMP_76    0x0076
#define FW_TMP_34    0x0034
#define FW_TMP_35    0x0035
#define FW_TMP_1809  0x1809
/* cmd_match.h */
static inline uint8_t util_shl4(uint8_t a) { return (uint8_t)(a << 4); }
uint8_t cmd_upcase(fw *f, uint8_t c);
uint8_t cmd_hex_value(fw *f, uint8_t c);
uint8_t cmd_hex_byte(fw *f);
uint8_t cmd_find_back(fw *f, uint8_t letter, uint8_t x);
uint8_t cmd_match_pattern(fw *f);
uint8_t cmd_match_table(fw *f);
```

  The replacements `"cmd_match_table"` at `0xADBF` (outputs `FW_OUT_A | FW_OUT_X | FW_OUT_Y`) and `"cmd_hex_value"` at `0xC520` (outputs `FW_OUT_A | FW_OUT_X | FW_OUT_Y | FW_OUT_Z`), 13 routines in all.

  `tests/test_fw_cmd.c` and its one test harness, which Tasks 5-9 reuse and extend (they define no mock or setup of their own):
  - `MAX_CALLS`, `rom_call_record` (`addr`, `a`, `x`, `y`, `budget`), `g_ram`, `g_f`, `g_host`, `g_calls`, `g_ncalls`;
  - `mock_rom_call`, which records every ROM call and answers `ROM_ISR_PAR_READY` (Task 6 adds `ROM_CMD_RUN_NEXT`, Task 8 `ROM_PAR_SETUP_VOICE`, Task 9 the `ser_tx_*` routines);
  - `setup()` (zeroed RAM, the mock host, the converted image), `entry()`, `point()`, `table_setup()` and the `P_*` cmd_table entries.

**ROM facts checked (in the dumps named):**
- `cmd_match_pattern` (`rom_AD90`) inlines the case map (`LDA $99B4,X`) instead of calling `cmd_upcase`. It reads the text byte first: a zero there returns Y before the pattern byte is read. So `"I+"` followed by a zero returns 2 through the text test, not through the pattern's zero. It returns with `PLA; TAX`, so A = the entry X on every return.
- `cmd_match_table` (`rom_ADBF`) reads the pointer bytes at `(cmd_table_ptr),ofs` and `(cmd_table_ptr),(ofs+1) & $FF`, and re-reads `cmd_table_ofs` on every pass. At the end marker it returns with A = $FF and Y = 0; after a match it returns with A = the entry X. X is preserved.
- After `JSR cmd_match_table`, `cmd_imm_match` (`rom_C413`) reads Y (`CPY #0`, then `LDA (cmd_pat_ptr),Y`), `cmd_pat_ptr` and `cmd_table_ofs`. A only reaches `ser_rx_flush_cmd`'s `PHA` (`rom_C3FA`, the dead stack), and X is not read before `cmd_imm_done` restores the zero page. The ROM `cmd_scan_clause` (`rom_ACFA`) reads Y and X (`STX $76` at `$AD63`). No caller reads a flag: `CPY #0` comes first. Declared: A, X, Y.
- `cmd_hex_value` (`rom_C520`): the split is `BMI` on `u - 'A'` (bit 7 of the difference), not an unsigned compare. So $80 (from $87 through the case map) gives $49, and $E0 gives $B0. Z = (result == 0) from the final `ADC`. X and Y are preserved, because `cmd_upcase` pushes and pulls X and nothing touches Y.
- Callers of `cmd_hex_value`:
  - `bank_cmd_select` (`rom_ABAF`) JSRs `bank_load_ramcode` next (`rom_A600`). That routine saves Y in `$78` and X in `$76` (`STY $78`, `STX $76`) and keeps A for `bank_enter` (`PHA`/`PLA`). Its first flag-setting instruction is `LDX P2`, so no flag from `cmd_hex_value` survives.
  - `cmd_set_voice` branches on Z (`$AA7A BNE`).
  - The other 16 ROM handlers start with `CMP #0`/`CMP #1`, and `cmd_hex_byte` with `JSR util_shl4` or `CLC` (checked with `grep -A1 "JSR cmd_hex_value" rom_*.txt`).
  - Declared: A, X, Y, Z. N and C are read by no caller.
- `cmd_upcase` (`rom_C50D`) writes the case-mapped byte into its own stack slot (`STA $0102,X`). That is the dead stack after its RTS, so the C does not reproduce it.
- `cmd_find_back` (`rom_ABDF`) decrements X before each read and stops when X reaches 0, so it never reads `boot_clause[0]`. When nothing matches it returns X = 1. It always writes `$75` = the letter.
- `cmd_hex_byte` (`rom_B450`) shifts the high value with `util_shl4` (`rom_C656`: `ASL` plus `util_shl3`, i.e. A << 4, 8 bits). A high digit 'G'..'Z' (16-35) loses its top bits: 'G' gives $00.
- Data addresses agree with the ROM: `cmd_imm_match` loads `$98A7` (`LDM $A7/$98`); `cmd_scan_clause` loads `$994F`; the case map is `$99B4`. The cmd_table entries 27 and 28 both point at `$993F` (`G#`), and entry 31 points at the `$FF` at `$994E`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_fw_cmd.c`:

```c
/* Unit checks of the command parser on the converted data (M4.2 spec 5.4), with a mock host that records every
   ROM call. The full proof is the live differential test (diff_lts) and the hybrid goldens (golden_hybrid). */
#include <string.h>
#include "test.h"
#include "firmware/cmd_match.h"
#include "firmware/tn_convert.h"
#include "firmware/data/fw_data.h"

#define MAX_CALLS 1024

/* cmd_table entries (match order, spec 4) */
enum {
    P_VERSION = 0,          /* ?    */
    P_QUERY = 1,            /* *?   */
    P_VOLUME = 2,           /* A*   */
    P_HYPER = 6,            /* H#   */
    P_MARK_SPACE = 7,       /* M**  */
    P_PROSODY = 10,         /* R#   */
    P_PROSODY_LETTER = 11,  /* R*** */
    P_SPEED = 14,           /* W#   */
    P_IGNORE_G = 27,        /* G#, listed twice (27 and 28) */
    P_END = 31              /* the $FF end marker */
};

typedef struct { uint16_t addr; uint8_t a, x, y; uint64_t budget; } rom_call_record;

static uint8_t g_ram[FW_RAM_SIZE];
static fw g_f;
static fw_host g_host;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;

/* Records every ROM call and answers the routines the C calls as the ROM would; any other call fails. */
static fw_call_result mock_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    (void)ctx;
    if (g_ncalls < MAX_CALLS) {
        rom_call_record *c = &g_calls[g_ncalls];
        c->addr = addr;
        c->a = r->a;
        c->x = r->x;
        c->y = r->y;
        c->budget = budget;
    }
    g_ncalls++;
    /* isr_par_ready leaves A, X and Y as they were: the registers come back unchanged */
    return addr == ROM_ISR_PAR_READY ? FW_CALL_RETURNED : FW_CALL_FAILED;
}

/* Zeroed RAM, the mock host, the converted image, no calls recorded. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    g_host.ctx = &g_f;
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = NULL;   /* no command routine in C drives P2 (bank_cmd_select stays in ROM) */
    g_ncalls = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
}

/* Entry i of a pointer table (cmd_table, cmd_imm_table) in the converted data: the address of its pattern. */
static uint16_t entry(uint16_t table, unsigned i)
{
    return (uint16_t)(fw_rom8(&g_f, (uint16_t)(table + 2 * i)) | fw_rom8(&g_f, (uint16_t)(table + 2 * i + 1)) << 8);
}

/* cmd_text_ptr = boot_clause + 2, holding text and then zeros; cmd_pat_ptr = pat. */
static void point(const char *text, uint16_t pat)
{
    memset(g_ram + BOOT_CLAUSE, 0, 256);
    memcpy(g_ram + BOOT_CLAUSE + 2, text, strlen(text));
    fw_wr16(&g_f, CMD_TEXT_PTR, BOOT_CLAUSE + 2);
    fw_wr16(&g_f, CMD_PAT_PTR, pat);
}

/* point(text), with cmd_table_ptr = table and cmd_table_ofs = ofs. */
static void table_setup(uint16_t table, uint8_t ofs, const char *text)
{
    point(text, 0);
    fw_wr16(&g_f, CMD_TABLE_PTR, table);
    g_ram[CMD_TABLE_OFS] = ofs;
}

static void test_table_layout(void)   /* the entries the tests below rely on */
{
    setup();
    CHECK_EQ(fw_rom8(&g_f, entry(CMD_TABLE, P_VERSION)), '?');
    CHECK_EQ(fw_rom8(&g_f, entry(CMD_TABLE, P_QUERY)), '*');
    CHECK_EQ(fw_rom8(&g_f, entry(CMD_TABLE, P_VOLUME)), 'A');
    CHECK_EQ(fw_rom8(&g_f, entry(CMD_TABLE, P_HYPER)), 'H');
    CHECK_EQ(fw_rom8(&g_f, entry(CMD_TABLE, P_MARK_SPACE)), 'M');
    CHECK_EQ(fw_rom8(&g_f, entry(CMD_TABLE, P_PROSODY)), 'R');
    CHECK_EQ(fw_rom8(&g_f, (uint16_t)(entry(CMD_TABLE, P_PROSODY_LETTER) + 3)), '*');
    CHECK_EQ(fw_rom8(&g_f, entry(CMD_TABLE, P_SPEED)), 'W');
    CHECK_EQ(fw_rom8(&g_f, entry(CMD_TABLE, P_IGNORE_G)), 'G');
    CHECK_EQ(entry(CMD_TABLE, P_IGNORE_G + 1), entry(CMD_TABLE, P_IGNORE_G));
    CHECK_EQ(fw_rom8(&g_f, entry(CMD_TABLE, P_END)), 0xFF);
    CHECK(!g_f.fault);
}

static void test_upcase(void)
{
    setup();
    CHECK_EQ(cmd_upcase(&g_f, 'a'), 'A');
    CHECK_EQ(cmd_upcase(&g_f, 'Q'), 'Q');
    CHECK_EQ(cmd_upcase(&g_f, '['), '(');
    CHECK_EQ(cmd_upcase(&g_f, 0x05), ' ');     /* control codes become spaces ... */
    CHECK_EQ(cmd_upcase(&g_f, 0x0D), 0x0D);    /* ... except CR and ESC */
    CHECK_EQ(cmd_upcase(&g_f, 0x87), 0x80);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

static void test_hex_value(void)
{
    static const struct { uint8_t c, v; } k[] = {
        { '0', 0 }, { '9', 9 }, { 'a', 10 }, { 'A', 10 }, { 'Z', 35 }, { 'z', 35 },
        { ':', 35 }, { '@', 35 },              /* $C52B: ':'..'@' count as 'Z' */
        { '+', 0xFB }, { '[', 0xF8 },          /* below '0' (after the case map: '[' is '(') it wraps */
        { 0x87, 0x49 }, { 0xE0, 0xB0 },        /* BMI on c - 'A': the case map's $80 gives $49, $E0 gives $B0 */
    };
    setup();
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        CHECK_EQ(cmd_hex_value(&g_f, k[i].c), k[i].v);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

static void test_hex_byte(void)
{
    setup();
    g_ram[FW_TMP_34] = '8';
    g_ram[FW_TMP_35] = '0';
    CHECK_EQ(cmd_hex_byte(&g_f), 0x80);
    CHECK_EQ(g_ram[FW_TMP_1809], 0x80);        /* the high nibble, kept in $1809 */
    g_ram[FW_TMP_34] = 'a';
    g_ram[FW_TMP_35] = 'f';
    CHECK_EQ(cmd_hex_byte(&g_f), 0xAF);
    CHECK_EQ(g_ram[FW_TMP_1809], 0xA0);
    g_ram[FW_TMP_34] = 'G';                    /* 16: shifted out of the byte, as in the ROM */
    g_ram[FW_TMP_35] = '1';
    CHECK_EQ(cmd_hex_byte(&g_f), 0x01);
    CHECK_EQ(g_ram[FW_TMP_1809], 0x00);
    CHECK(!g_f.fault);
}

static void test_find_back(void)
{
    setup();
    memcpy(g_ram + BOOT_CLAUSE, " @m45", 5);
    CHECK_EQ(cmd_find_back(&g_f, 'M', 4), 3);  /* the lower-case letter matches through the case map; X = after it */
    CHECK_EQ(g_ram[FW_TMP_75], 'M');
    memcpy(g_ram + BOOT_CLAUSE, " @MM5", 5);
    CHECK_EQ(cmd_find_back(&g_f, 'M', 4), 4);  /* an argument holding the letter re-anchors on it (@MM5) */
    memcpy(g_ram + BOOT_CLAUSE, " Mab", 4);
    CHECK_EQ(cmd_find_back(&g_f, 'M', 3), 2);  /* index 1 is still searched */
    memcpy(g_ram + BOOT_CLAUSE, "M@ab", 4);
    g_ram[FW_TMP_75] = 0;
    CHECK_EQ(cmd_find_back(&g_f, 'M', 3), 1);  /* not found down to index 1: X = 1; index 0 is never read */
    CHECK_EQ(g_ram[FW_TMP_75], 'M');
    CHECK(!g_f.fault);
}

static void test_match_pattern(void)
{
    setup();
    point("Ax", entry(CMD_TABLE, P_VOLUME));
    CHECK_EQ(cmd_match_pattern(&g_f), 2);      /* '*' takes anything */
    point("a!", entry(CMD_TABLE, P_VOLUME));
    CHECK_EQ(cmd_match_pattern(&g_f), 2);      /* the text is case-mapped */
    point("h5", entry(CMD_TABLE, P_HYPER));
    CHECK_EQ(cmd_match_pattern(&g_f), 2);      /* '#' takes a digit */
    point("Hx", entry(CMD_TABLE, P_HYPER));
    CHECK_EQ(cmd_match_pattern(&g_f), 0);
    point("H:", entry(CMD_TABLE, P_HYPER));
    CHECK_EQ(cmd_match_pattern(&g_f), 0);      /* '9' + 1 */
    point("H/", entry(CMD_TABLE, P_HYPER));
    CHECK_EQ(cmd_match_pattern(&g_f), 0);      /* '0' - 1 */
    point("Q5", entry(CMD_TABLE, P_SPEED));
    CHECK_EQ(cmd_match_pattern(&g_f), 0);      /* mismatch */
    point("M12345", entry(CMD_TABLE, P_MARK_SPACE));
    CHECK_EQ(cmd_match_pattern(&g_f), 3);      /* the pattern's zero ends a full match */
    point("M1", entry(CMD_TABLE, P_MARK_SPACE));
    CHECK_EQ(cmd_match_pattern(&g_f), 2);      /* a zero in the text: partial, Y = the index of the zero */
    point("R", entry(CMD_TABLE, P_PROSODY_LETTER));
    CHECK_EQ(cmd_match_pattern(&g_f), 1);
    point("", entry(CMD_TABLE, P_VOLUME));
    CHECK_EQ(cmd_match_pattern(&g_f), 0);      /* a zero at once: 0, as a mismatch */
    point("?", entry(CMD_TABLE, P_VERSION));
    CHECK_EQ(cmd_match_pattern(&g_f), 1);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

static void test_match_table(void)
{
    setup();
    table_setup(CMD_TABLE, 0, "W5 ");
    CHECK_EQ(cmd_match_table(&g_f), 2);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 2 * P_SPEED);           /* left at the matching entry */
    CHECK_EQ(fw_rd16(&g_f, CMD_PAT_PTR), entry(CMD_TABLE, P_SPEED));

    table_setup(CMD_TABLE, 0, "r5AB");                      /* R# and R*** both match: the first wins */
    CHECK_EQ(cmd_match_table(&g_f), 2);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 2 * P_PROSODY);

    table_setup(CMD_TABLE, 0, "w?");                        /* *? comes before W#: a query */
    CHECK_EQ(cmd_match_table(&g_f), 2);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 2 * P_QUERY);

    table_setup(CMD_TABLE, 0, "g1");                        /* the duplicate G#: the first copy */
    CHECK_EQ(cmd_match_table(&g_f), 2);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 2 * P_IGNORE_G);

    table_setup(CMD_TABLE, 0, "R");                         /* a zero after the letter: *? matches partially */
    CHECK_EQ(cmd_match_table(&g_f), 1);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 2 * P_QUERY);

    table_setup(CMD_TABLE, 0, "Zq");                        /* nothing matches: Y = 0 at the $FF end */
    CHECK_EQ(cmd_match_table(&g_f), 0);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 2 * P_END);
    CHECK_EQ(fw_rd16(&g_f, CMD_PAT_PTR), entry(CMD_TABLE, P_END));
    CHECK_EQ(fw_rom8(&g_f, fw_rd16(&g_f, CMD_PAT_PTR)), 0xFF);

    table_setup(CMD_IMM_TABLE, 0, "I+");                    /* the immediate table */
    CHECK_EQ(cmd_match_table(&g_f), 2);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 0);
    table_setup(CMD_IMM_TABLE, 0, "I?");
    CHECK_EQ(cmd_match_table(&g_f), 2);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 2);
    table_setup(CMD_IMM_TABLE, 0, "I");                     /* partial: cmd_imm_match waits for the next byte */
    CHECK_EQ(cmd_match_table(&g_f), 1);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 0);
    table_setup(CMD_IMM_TABLE, 2, "I+");                    /* resumed from entry 1 (ser_cmd_table_ofs) */
    CHECK_EQ(cmd_match_table(&g_f), 0);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 4);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

void suite_fw_cmd(void)
{
    CHECK(fw_image_get() != NULL);
    test_table_layout();
    test_upcase();
    test_hex_value();
    test_hex_byte();
    test_find_back();
    test_match_pattern();
    test_match_table();
}
```

In `tests/suites.h`, append after `SUITE(suite_fw_tn)`:

```c
SUITE(suite_fw_cmd)
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmd //c ".\build.bat test fw_cmd"`
Expected: a compile error, because `firmware/cmd_match.h` does not exist.

- [ ] **Step 3: Implement**

`src/firmware/fw.h`: replace

```c
#define FW_TMP_75    0x0075   /* zero-page scratch, unnamed in the annotations: tn_convert_clause's saved X */
```

with

```c
#define FW_TMP_75    0x0075   /* zero-page scratch, unnamed in the annotations: tn_convert_clause's saved X */
#define FW_TMP_76    0x0076   /* zero-page scratch, unnamed in the annotations: cmd_scan_clause's resume index,
                                 cmd_set_prosody's parameter index */
#define FW_TMP_34    0x0034   /* zero-page scratch, unnamed in the annotations: cmd_hex_byte's high digit */
#define FW_TMP_35    0x0035   /* zero-page scratch, unnamed in the annotations: cmd_hex_byte's low digit */
#define FW_TMP_1809  0x1809   /* scratch, unnamed in the annotations: cmd_hex_byte's high nibble */
```

Create `src/firmware/cmd_match.h`:

```c
#ifndef FW_CMD_MATCH_H
#define FW_CMD_MATCH_H
/* The command matcher and the argument helpers of the command parser (M4.2 spec 1, 2.6), decompiled. Patterns
   and tables are read from the converted data; the text from RAM (stale bytes after the clause included). */
#include "fw.h"

static inline uint8_t util_shl4(uint8_t a) { return (uint8_t)(a << 4); }   /* ROM util_shl4 $C656, inlined */

/* $C50D: cmd_case_map[c] (upper case; control codes, DEL and many codes above $7F become spaces). */
uint8_t cmd_upcase(fw *f, uint8_t c);
/* $C520: the value of an argument character after cmd_upcase: '0'-'9' -> 0-9, 'A'-'Z' -> 10-35, ':'-'@' -> 35;
   others wrap ('+' -> $FB). Callers range-check it. Z of the ROM exit = (result == 0). */
uint8_t cmd_hex_value(fw *f, uint8_t c);
/* $B450: the byte of the two hex characters in $34 (high) and $35 (low); $1809 = the high value << 4. */
uint8_t cmd_hex_byte(fw *f);
/* $ABDF: $75 = letter; searching back from boot_clause[x - 1] for a character whose upper case is letter,
   stopping at index 1: returns the index after it, or 1 if there is none (boot_clause[0] is never read). */
uint8_t cmd_find_back(fw *f, uint8_t letter, uint8_t x);
/* $AD90: compares the text at cmd_text_ptr (case-mapped) with the pattern at cmd_pat_ptr ('*' any character,
   '#' a digit): returns the index of the first zero (the pattern's end: a full match; the text's: a partial
   one), or 0 on a mismatch. */
uint8_t cmd_match_pattern(fw *f);
/* $ADBF: from entry cmd_table_ofs of the pointer table at cmd_table_ptr, the first pattern that matches: returns
   cmd_match_pattern's Y and leaves cmd_pat_ptr and cmd_table_ofs at that entry; 0 at the $FF end marker. */
uint8_t cmd_match_table(fw *f);

#endif
```

Create `src/firmware/cmd_match.c`:

```c
#include "cmd_match.h"
#include "data/fw_data.h"

uint8_t cmd_upcase(fw *f, uint8_t c) { return fw_rom8(f, (uint16_t)(CMD_CASE_MAP + c)); }

uint8_t cmd_hex_value(fw *f, uint8_t c)
{
    uint8_t a = cmd_upcase(f, c);
    if (a >= 0x3A && a < 0x41)
        a = 0x5A;                                       /* $C52B: ':'..'@' count as 'Z' */
    uint8_t d = (uint8_t)(a - 0x41);                    /* SEC; SBC #'A' */
    return (d & 0x80) ? (uint8_t)(d + 0x11)             /* BMI: bit 7 of the difference decides, not a compare */
                      : (uint8_t)(d + 0x0A);
}

uint8_t cmd_hex_byte(fw *f)
{
    fw_wr8(f, FW_TMP_1809, util_shl4(cmd_hex_value(f, fw_rd8(f, FW_TMP_34))));
    return (uint8_t)(cmd_hex_value(f, fw_rd8(f, FW_TMP_35)) + fw_rd8(f, FW_TMP_1809));
}

uint8_t cmd_find_back(fw *f, uint8_t letter, uint8_t x)
{
    fw_wr8(f, FW_TMP_75, letter);
    for (;;) {
        x--;
        if (x == 0 || cmd_upcase(f, fw_rd8(f, (uint16_t)(BOOT_CLAUSE + x))) == fw_rd8(f, FW_TMP_75))
            break;
    }
    return (uint8_t)(x + 1);
}

uint8_t cmd_match_pattern(fw *f)
{
    uint8_t y = 0xFF;
    for (;;) {
        y++;
        uint8_t t = fw_ind_y(f, CMD_TEXT_PTR, y);
        if (t == 0)
            return y;                                   /* the text ends: a partial match (0 at index 0) */
        uint8_t u = cmd_upcase(f, t);                   /* the ROM inlines the table read (LDA $99B4,X) */
        uint8_t p = fw_ind_y(f, CMD_PAT_PTR, y);
        if (p == 0)
            return y;                                   /* the pattern ends: a full match */
        if (p == '*')
            continue;
        if (p == '#') {
            if (u < '0' || u >= ':')
                return 0;
            continue;
        }
        if (u != p)
            return 0;
    }
}

uint8_t cmd_match_table(fw *f)
{
    for (;;) {
        uint8_t ofs = fw_rd8(f, CMD_TABLE_OFS);
        fw_wr8(f, CMD_PAT_PTR, fw_ind_y(f, CMD_TABLE_PTR, ofs));
        fw_wr8(f, CMD_PAT_PTR + 1, fw_ind_y(f, CMD_TABLE_PTR, (uint8_t)(ofs + 1)));
        if (fw_ind_y(f, CMD_PAT_PTR, 0) == 0xFF)
            return 0;                                   /* the end marker */
        uint8_t y = cmd_match_pattern(f);
        if (y != 0)
            return y;
        fw_wr8(f, CMD_TABLE_OFS, (uint8_t)(ofs + 2));   /* INC; INC */
    }
}
```

`src/engine/thunks.c`:
- Replace

```c
#include "firmware/tn_convert.h"
```

with

```c
#include "firmware/tn_convert.h"
#include "firmware/cmd_match.h"
```

- Replace

```c
/* lts_affix_next $AF11: Y in; A, Y out */
```

with

```c
/* cmd_match_table $ADBF: no register input (X is passed through); Y out, A = $FF at the end marker (Y = 0), else
   the entry X (cmd_match_pattern's PLA/TAX), X preserved. cmd_imm_match reads Y and the RAM it leaves; the ROM
   cmd_scan_clause reads Y and X; A only reaches the dead stack (ser_rx_flush_cmd's PHA). All three are declared. */
static void cmd_match_table_regs(fw *f, cpu740 *c)
{
    uint8_t x = c->x;
    uint8_t y = cmd_match_table(f);
    c->y = y;
    c->a = y ? x : 0xFF;
}

/* lts_affix_next $AF11: Y in; A, Y out */
```

- Replace

```c
/* The hook of each routine: its register mapping run in the shared thunk frame, user = the bound fw_bridge. */
```

with

```c
/* cmd_hex_value $C520: A in; A and Z out, X and Y preserved (cmd_upcase keeps X). bank_cmd_select passes A on to
   bank_enter, and bank_load_ramcode saves X and Y in $76 and $78; the ROM handlers read A; cmd_set_voice
   branches on Z ($AA7A BNE). No caller reads N or C. */
static void cmd_hex_value_regs(fw *f, cpu740 *c)
{
    uint8_t v = cmd_hex_value(f, c->a);
    c->a = v;
    c->p = (uint8_t)(v == 0 ? c->p | CPU740_Z : c->p & ~CPU740_Z);
}

/* The hook of each routine: its register mapping run in the shared thunk frame, user = the bound fw_bridge. */
```

- Replace

```c
THUNK(lts_affix_next)
```

with

```c
THUNK(cmd_match_table)
THUNK(lts_affix_next)
```

- Replace

```c
THUNK(tn_convert_clause)
#undef THUNK
```

with

```c
THUNK(tn_convert_clause)
THUNK(cmd_hex_value)
#undef THUNK
```

- Replace

```c
    { "lts_affix_next",     0xAF11, FW_OUT_A | FW_OUT_Y, t_lts_affix_next },
```

with

```c
    { "cmd_match_table",    0xADBF, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_match_table },
    { "lts_affix_next",     0xAF11, FW_OUT_A | FW_OUT_Y, t_lts_affix_next },
```

- Replace

```c
    { "tn_convert_clause",  0xB1E6, 0,                   t_tn_convert_clause },
```

with

```c
    { "tn_convert_clause",  0xB1E6, 0,                   t_tn_convert_clause },
    { "cmd_hex_value",      0xC520, FW_OUT_A | FW_OUT_X | FW_OUT_Y | FW_OUT_Z, t_cmd_hex_value },
```

The table stays in address order.

**Faithfulness check.** Before committing, read each dump side by side with its C and confirm every branch, every RAM write and every 8-bit wrap:
- `rom_AD90_cmd_match_pattern.txt`: the text byte is tested before the pattern byte; `#` tests the case-mapped byte against `'0'` (`BCC`) and `':'` (`BCS`).
- `rom_ADBF_cmd_match_table.txt`: the second pointer byte is read at `(ofs+1) & $FF` (`INY`); `cmd_table_ofs` is re-read after `INC; INC`.
- `rom_C50D_cmd_upcase.txt`.
- `rom_C520_cmd_hex_value.txt`: `BMI` on the `SBC` result.
- `rom_B450_cmd_hex_byte.txt` with `rom_C656_util_shl4.txt`.
- `rom_ABDF_cmd_find_back.txt`: `DEX` comes before the zero test; `INX` comes at the end.

For the thunk outputs, re-read:
- `rom_C413_cmd_imm_match.txt` and `rom_C3FA_ser_rx_flush_cmd.txt`;
- `rom_ACFA_cmd_scan_clause.txt` (`$AD54-$AD63`);
- `rom_ABAF_bank_cmd_select.txt`, `rom_A600_bank_load_ramcode.txt` and `rom_AA74_cmd_set_voice.txt` (`$AA7A`).

Confirm that no other caller of `$C520` reads a flag before setting it: `grep -A1 "JSR cmd_hex_value" build/ghidra740/dump/rom_*.txt` shows only `CMP`, `BNE` ($AA7A), `JSR` and `CLC` after the call. Record the result in the report.

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test fw_cmd"`, then `cmd //c ".\build.bat test diff_lts"`, then `cmd //c ".\build.bat test golden_hybrid"`, then `cmd //c ".\build.bat test"`
Expected:
- `suite_fw_cmd                 ok`, `1 suite(s), 0 failure(s), 0 skip(s)`.
- `diff_lts`: 13 lines `<name> N calls checked`, each with N > 0. Among them:
  - `cmd_match_table      N calls checked`, reached from the ROM `cmd_scan_clause` on every clause with a `@`, and from the interrupt-level `cmd_imm_match` on every `@` the host sends;
  - `cmd_hex_value        N calls checked`, reached from the ROM handlers and `bank_cmd_select`.

  There are 0 mismatches, and the flush line is as after Task 2 (`1 mid-clause flush calls skipped`).
- `golden_hybrid`: `32 corpus files x 14 modes (all, then each routine alone)`, with no frame differences.
- All 30 suites ok: `30 suite(s), 0 failure(s), 0 skip(s)`, with no warnings, and the firmware syntax check passing.

If `diff_lts` reports a mismatch, find the root cause against the dump and fix the C faithfully. Never loosen the comparison or drop a declared output.

- [ ] **Step 5: Commit**

```bash
git add src/firmware/cmd_match.h src/firmware/cmd_match.c src/firmware/fw.h src/engine/thunks.c tests/test_fw_cmd.c tests/suites.h
git commit -m "The command matcher in C: cmd_match_pattern/_table, cmd_upcase, cmd_hex_value/_byte, cmd_find_back; hooks cmd_match_table and cmd_hex_value (M4.2 2.2)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: `cmd_scan_clause` in C, and its hook (spec §1, §2.3, §2.5, §2.6)

**Files:**
- Create: `src/firmware/cmd_scan.h`, `src/firmware/cmd_scan.c`
- Modify: `src/engine/thunks.c`, `tests/test_fw_cmd.c`

**Interfaces:**
- Consumes:
  - Task 4's `cmd_match_table`, `cmd_upcase`, `FW_TMP_76`, and its test harness: `g_ram`, `g_f`, `g_calls`, `g_ncalls`, `MAX_CALLS`, `mock_rom_call`, `setup()`, `entry()` and `P_*`;
  - Task 3's `CMD_TABLE` and `ROM_CMD_SET_SPEED`/`ROM_CMD_SET_VOLUME` (`cmd_addr.h`, tests only);
  - `FW_TMP_78`, `ROM_ISR_PAR_READY` (`tn_convert.h`) and `fw_rom_call` with `FW_ROM_CALL_BUDGET`.
- Produces:
  - `void cmd_scan_clause(fw *f);` and the replacement `"cmd_scan_clause"` at `0xACFA` with no declared outputs (14 routines in all);
  - in `tests/test_fw_cmd.c`: `count(addr)` (ROM calls to addr), `check_par_ready_calls()`, `put_clause(s)` (s at `boot_clause`) and `scan_setup(clause)`. Tasks 6-9 use `count` and `put_clause`.

**ROM facts checked (`rom_ACFA_cmd_scan_clause.txt` unless named):**
- The case map is skipped on `BBS0 cmd_phonetic`, i.e. on **bit 0** of `cmd_phonetic`, not on a non-zero value. `cmd_set_phonetic` (`rom_AB9F`) stores only 0 or 1, so the two agree on every reachable state; the C tests bit 0. The map applies only to codes ≥ $21 (`CMP #$21; BCC`).
- `$78` is written in two places:
  - at `$AD4F`, before the match (the saved Y);
  - at `$AD1B`, for **every case-mapped character** (`STY $78; TAY; LDA $99B4,Y; LDY $78`).

  So the final `$78` is the Y of the last mapped character, or of the last `cmd_char`, whichever came later.
- The loop is a do-while (`$AD09` … `CPX boot_clause_len; BCC`). So the scan reads one character even when `boot_clause_len` = 0, and then sets `boot_clause_len` to 1. This cannot be reached, because `boot_get_clause` always puts ' ' at index 0. Every pass re-reads `boot_clause_len`, `cmd_char` and `cmd_phonetic`. An interrupt during `isr_par_ready` can change `cmd_char` (`ser_rx_mute`), so the C re-reads them after each ROM call as well.
- `isr_par_ready` (`rom_C5D6`, with the shared tail `PLA; RTS` at `$C5EA` in `rom_C5DE`) does `PHA` … `PLA` and touches neither X nor Y. It preserves A, X and Y and reads only `boot_dip`. At the JSR, X is the read index and Y the write index. A is $FF on the first pass; after that it is the last byte stored: the cmd_text character (mapped or raw), the raw `cmd_char`, or, after a match, the handler's high byte.
- The match path:
  - `$76` = X + Y, 8-bit (X is the index after the `cmd_char`, Y the matched length);
  - `cmd_arg_index[n]` = `$76` - 1 (`DEC A`);
  - `cmd_positions[n]` = `$78`;
  - the handler bytes are read at `(cmd_pat_ptr),Y+1` and `,Y+2` (each `INY` is 8-bit) and written to `$150B + (n<<1 & $FF)` and `$150C + (n<<1 & $FF)`. The address is absolute, so the second byte can reach `$160A`.

  The match test is `CPY #0` alone, so a partial match on a zero byte is queued too, with the handler bytes read from inside the pattern.
- The no-match path:
  - Y comes back from `$78`;
  - `cmd_char` is re-read (`LDA cmd_char`) and stored raw, with no case map;
  - `$76` is not written.
- `cmd_text_ptr` = `$1B00` + X: `LDM #0`, then `CLC; ADC` X. The carry is never set, so the `INC` of the high byte at `$AD4D` is dead.
- At the end:
  - `boot_clause_len` = Y;
  - `cmd_next_pos` = `cmd_positions[0]`, which is $FF when no command was queued;
  - `cmd_positions[cmd_count]` is not reset.

  The exit registers are A = `cmd_positions[0]`, X = 0, Y = the length. `boot_loop_text` (`rom_A68D`) JSRs `tn_convert_clause` next, and its ROM version (`rom_B1E6`) starts with `LDM #0,tn_resume_pos; LDX #0` and reads no register. So no outputs are declared, as the spec says.

- [ ] **Step 1: Write the failing tests**

In `tests/test_fw_cmd.c`:
- After `#include "firmware/cmd_match.h"`, add:

```c
#include "firmware/cmd_scan.h"
#include "firmware/cmd_addr.h"
```

- After `table_setup`, add:

```c
static unsigned count(uint16_t addr)
{
    unsigned n = 0;
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++)
        n += g_calls[i].addr == addr;
    return n;
}

/* Every ROM call is isr_par_ready with the strict budget. */
static void check_par_ready_calls(void)
{
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++) {
        CHECK_EQ(g_calls[i].addr, ROM_ISR_PAR_READY);
        CHECK_EQ(g_calls[i].budget, FW_ROM_CALL_BUDGET);
    }
}

/* boot_clause = s (the bytes after it unchanged). */
static void put_clause(const char *s) { memcpy(g_ram + BOOT_CLAUSE, s, strlen(s)); }

/* setup(), then boot_clause = clause (then zeros), boot_clause_len = its length, cmd_char = '@'. */
static void scan_setup(const char *clause)
{
    setup();
    put_clause(clause);
    g_ram[BOOT_CLAUSE_LEN] = (uint8_t)strlen(clause);
    g_ram[CMD_CHAR] = '@';
}
```

- Above `void suite_fw_cmd(void)`, add:

```c
static void test_scan_command(void)
{
    scan_setup(" @W5Hi");
    g_ram[CMD_COUNT] = 7;                       /* both reset by the scan */
    g_ram[CMD_NEXT] = 7;
    cmd_scan_clause(&g_f);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 3);        /* the cmd_text length */
    CHECK(memcmp(g_ram + CMD_TEXT, " HI", 3) == 0);
    CHECK_EQ(g_ram[CMD_COUNT], 1);
    CHECK_EQ(g_ram[CMD_NEXT], 0);
    CHECK_EQ(g_ram[CMD_POSITIONS], 1);          /* cut out at cmd_text position 1 */
    CHECK_EQ(g_ram[CMD_NEXT_POS], 1);
    CHECK_EQ(g_ram[CMD_ARG_INDEX], 3);          /* the boot_clause index of '5' */
    CHECK_EQ(fw_rd16(&g_f, CMD_HANDLERS), ROM_CMD_SET_SPEED);
    CHECK_EQ(g_ram[FW_TMP_76], 4);              /* the resume index */
    CHECK_EQ(g_ram[FW_TMP_78], 2);              /* last written by the case map of 'i' (Y = 2) */
    CHECK_EQ(fw_rd16(&g_f, CMD_TEXT_PTR), BOOT_CLAUSE + 2);
    CHECK_EQ(fw_rd16(&g_f, CMD_TABLE_PTR), CMD_TABLE);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 2 * P_SPEED);
    CHECK_EQ(fw_rd16(&g_f, CMD_PAT_PTR), entry(CMD_TABLE, P_SPEED));
    /* isr_par_ready once per character read (X = 0, 1, 4, 5), with the registers the ROM has at $AD09: A = $FF,
       then the last byte stored (' ', the handler's high byte, 'H') */
    static const uint8_t want_a[] = { 0xFF, ' ', ROM_CMD_SET_SPEED >> 8, 'H' };
    static const uint8_t want_x[] = { 0, 1, 4, 5 };
    static const uint8_t want_y[] = { 0, 1, 1, 2 };
    CHECK_EQ(g_ncalls, 4);
    for (unsigned i = 0; i < 4 && i < g_ncalls; i++) {
        CHECK_EQ(g_calls[i].a, want_a[i]);
        CHECK_EQ(g_calls[i].x, want_x[i]);
        CHECK_EQ(g_calls[i].y, want_y[i]);
    }
    check_par_ready_calls();
    CHECK(!g_f.fault);
}

static void test_scan_two_commands(void)
{
    scan_setup(" @W5@A3Go");
    g_ram[CMD_POSITIONS + 2] = 0x77;            /* cmd_positions[cmd_count] is not reset */
    cmd_scan_clause(&g_f);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 3);
    CHECK(memcmp(g_ram + CMD_TEXT, " GO", 3) == 0);
    CHECK_EQ(g_ram[CMD_COUNT], 2);
    CHECK_EQ(g_ram[CMD_POSITIONS], 1);          /* both due at position 1, in queue order */
    CHECK_EQ(g_ram[CMD_POSITIONS + 1], 1);
    CHECK_EQ(g_ram[CMD_POSITIONS + 2], 0x77);
    CHECK_EQ(g_ram[CMD_ARG_INDEX], 3);
    CHECK_EQ(g_ram[CMD_ARG_INDEX + 1], 6);
    CHECK_EQ(fw_rd16(&g_f, CMD_HANDLERS), ROM_CMD_SET_SPEED);
    CHECK_EQ(fw_rd16(&g_f, CMD_HANDLERS + 2), ROM_CMD_SET_VOLUME);
    CHECK_EQ(g_ram[CMD_NEXT_POS], 1);
    CHECK_EQ(count(ROM_ISR_PAR_READY), 5);      /* X = 0, 1, 4, 7, 8 */
    check_par_ready_calls();
    CHECK(!g_f.fault);
}

static void test_scan_unmatched(void)
{
    scan_setup(" @Zq");                         /* no pattern matches "Zq" */
    g_ram[FW_TMP_76] = 0x99;
    cmd_scan_clause(&g_f);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 4);
    CHECK(memcmp(g_ram + CMD_TEXT, " @ZQ", 4) == 0);
    CHECK_EQ(g_ram[CMD_COUNT], 0);
    CHECK_EQ(g_ram[CMD_POSITIONS], 0xFF);
    CHECK_EQ(g_ram[CMD_NEXT_POS], 0xFF);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 2 * P_END);
    CHECK_EQ(g_ram[FW_TMP_76], 0x99);           /* not written on this path */
    CHECK_EQ(g_ram[FW_TMP_78], 3);
    CHECK_EQ(count(ROM_ISR_PAR_READY), 4);

    scan_setup(" zZq");                         /* the cmd_char is stored raw, not case-mapped */
    g_ram[CMD_CHAR] = 'z';
    cmd_scan_clause(&g_f);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 4);
    CHECK(memcmp(g_ram + CMD_TEXT, " zZQ", 4) == 0);
    CHECK_EQ(g_ram[CMD_COUNT], 0);
    check_par_ready_calls();
    CHECK(!g_f.fault);
}

static void test_scan_case_map(void)
{
    scan_setup(" a[b]\x05\x7F");
    cmd_scan_clause(&g_f);
    static const uint8_t want[] = { ' ', 'A', '(', 'B', ')', 0x05, ' ' };   /* codes below $21 stay raw */
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], sizeof want);
    CHECK(memcmp(g_ram + CMD_TEXT, want, sizeof want) == 0);
    CHECK_EQ(g_ram[FW_TMP_78], 6);              /* the Y of the last mapped character (DEL) */
    CHECK(!g_f.fault);
}

static void test_scan_phonetic(void)
{
    scan_setup(" ab[@W5c");
    g_ram[CMD_PHONETIC] = 1;                    /* @X1: copied raw, commands still cut out */
    cmd_scan_clause(&g_f);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 5);
    CHECK(memcmp(g_ram + CMD_TEXT, " ab[c", 5) == 0);
    CHECK_EQ(g_ram[CMD_COUNT], 1);
    CHECK_EQ(g_ram[CMD_POSITIONS], 4);

    scan_setup(" ab[");
    g_ram[CMD_PHONETIC] = 2;                    /* BBS0: only bit 0 counts */
    cmd_scan_clause(&g_f);
    CHECK(memcmp(g_ram + CMD_TEXT, " AB(", 4) == 0);
    check_par_ready_calls();
    CHECK(!g_f.fault);
}

static void test_scan_nul_partial(void)
{
    scan_setup(" @R");                          /* a zero after the letter: *? matches partially (Y = 1) */
    cmd_scan_clause(&g_f);
    CHECK_EQ(g_ram[CMD_COUNT], 1);              /* queued as if it had matched (the latent ROM bug) */
    CHECK_EQ(g_ram[CMD_POSITIONS], 1);
    CHECK_EQ(g_ram[CMD_ARG_INDEX], 2);
    CHECK_EQ(g_ram[FW_TMP_76], 3);
    uint16_t pat = entry(CMD_TABLE, P_QUERY);   /* the handler bytes come from inside "*?" */
    CHECK_EQ(fw_rd16(&g_f, CMD_PAT_PTR), pat);
    CHECK_EQ(g_ram[CMD_HANDLERS], fw_rom8(&g_f, (uint16_t)(pat + 2)));
    CHECK_EQ(g_ram[CMD_HANDLERS + 1], fw_rom8(&g_f, (uint16_t)(pat + 3)));
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 1);
    CHECK(!g_f.fault);
}

static void test_scan_empty(void)
{
    scan_setup(" ");
    g_ram[BOOT_CLAUSE_LEN] = 0;                 /* do-while: one character is still read */
    cmd_scan_clause(&g_f);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 1);
    CHECK_EQ(g_ram[CMD_TEXT], ' ');
    CHECK_EQ(count(ROM_ISR_PAR_READY), 1);
    check_par_ready_calls();
    CHECK(!g_f.fault);
}
```

- In `suite_fw_cmd`, after `test_match_table();`, add:

```c
    test_scan_command();
    test_scan_two_commands();
    test_scan_unmatched();
    test_scan_case_map();
    test_scan_phonetic();
    test_scan_nul_partial();
    test_scan_empty();
```

Traces used for the expected values (from `rom_ACFA`, Y = the write index):
- `" @W5Hi"`:
  - X=0 `' '` is raw; Y becomes 1.
  - X=1 `'@'`: `cmd_text_ptr` = `$1B02`, `$78` = 1, `W#` gives Y = 2, `$76` = 4, `arg` = 3, `pos` = 1; X becomes 4 and Y 1.
  - X=4 `'H'`: `$78` = 1; Y becomes 2.
  - X=5 `'i'`: `$78` = 2; Y becomes 3.
- `" @Zq"`:
  - `'@'` gets no match, so `'@'` goes to `cmd_text[1]`.
  - `'Z'` gives `$78` = 2, then `'q'` gives `$78` = 3.
- `" @R"`:
  - `'?'` mismatches at index 0.
  - `*?` takes `'R'`; then `boot_clause[3]` = 0 returns Y = 1. `$76` = 2 + 1 = 3, and the handler bytes are pattern bytes 2 and 3.
- `" ab[@W5c"`, phonetic mode: `'@'` sits at index 4 with Y = 4.

- [ ] **Step 2: Run it to see it fail**

Run: `cmd //c ".\build.bat test fw_cmd"`
Expected: a compile error, because `firmware/cmd_scan.h` does not exist.

- [ ] **Step 3: Implement**

Create `src/firmware/cmd_scan.h`:

```c
#ifndef FW_CMD_SCAN_H
#define FW_CMD_SCAN_H
/* The clause scan of the command parser (ROM $ACFA), decompiled (M4.2 spec 1). */
#include "fw.h"

/* $ACFA: copies boot_clause (boot_clause_len characters, at least one) to cmd_text, codes >= $21 through
   cmd_case_map unless bit 0 of cmd_phonetic is set. At each cmd_char the text after it is matched against
   cmd_table: a match (any Y != 0, a partial one on a zero byte included) is left out of cmd_text and queued as
   command n = cmd_count++ (cmd_positions[n] = its cmd_text position, cmd_handlers[n] = the two bytes after the
   matched part of the pattern, cmd_arg_index[n] = the boot_clause index of its last character); an unmatched
   cmd_char stays as text, raw. Then boot_clause_len = the cmd_text length and cmd_next_pos = cmd_positions[0]
   ($FF set at the start). isr_par_ready (ROM) runs before each character is read. Scratch: $76, $78. */
void cmd_scan_clause(fw *f);

#endif
```

Create `src/firmware/cmd_scan.c`:

```c
#include "cmd_scan.h"
#include "cmd_match.h"
#include "tn_convert.h"
#include "data/fw_data.h"

void cmd_scan_clause(fw *f)
{
    uint8_t a = 0xFF, x = 0, y = 0;                 /* X: boot_clause read index, Y: cmd_text write index */
    fw_wr8(f, CMD_COUNT, y);
    fw_wr8(f, CMD_NEXT, y);
    fw_wr8(f, (uint16_t)(CMD_POSITIONS + x), a);    /* cmd_positions[0] = $FF: no command yet */
    do {                                            /* $AD09: one boot_clause character */
        fw_regs r = { a, x, y, 0 };
        fw_rom_call(f, ROM_ISR_PAR_READY, &r, FW_ROM_CALL_BUDGET);   /* parallel mode: ready for the next byte */
        a = r.a;
        x = r.x;
        y = r.y;
        a = fw_rd8(f, (uint16_t)(BOOT_CLAUSE + x++));
        if (a != fw_rd8(f, CMD_CHAR)) {
            if (!(fw_rd8(f, CMD_PHONETIC) & 1) && a >= 0x21) {   /* BBS0: bit 0 only; control codes stay raw */
                fw_wr8(f, FW_TMP_78, y);                         /* Y saved around the table read */
                a = cmd_upcase(f, a);
            }
            fw_wr8(f, (uint16_t)(CMD_TEXT + y++), a);
            continue;
        }
        /* $AD36: a command character: match the text after it against cmd_table */
        fw_wr16(f, CMD_TABLE_PTR, CMD_TABLE);
        fw_wr8(f, CMD_TABLE_OFS, 0);
        fw_wr16(f, CMD_TEXT_PTR, (uint16_t)(BOOT_CLAUSE + x));  /* 0 + X never carries: the INC at $AD4D is dead */
        fw_wr8(f, FW_TMP_78, y);
        uint8_t len = cmd_match_table(f);
        if (len == 0) {                                          /* $AD58: no command: the character stays, raw */
            y = fw_rd8(f, FW_TMP_78);
            a = fw_rd8(f, CMD_CHAR);
            fw_wr8(f, (uint16_t)(CMD_TEXT + y++), a);
            continue;
        }
        /* $AD63: queue command n, cut out of cmd_text. Any len != 0 counts, a partial match on a zero byte too:
           its handler bytes then come from inside the pattern (the ROM's latent bug, kept). */
        uint8_t n = fw_rd8(f, CMD_COUNT);
        fw_wr8(f, CMD_COUNT, (uint8_t)(n + 1));
        uint8_t resume = (uint8_t)(x + len);
        fw_wr8(f, FW_TMP_76, resume);
        fw_wr8(f, (uint16_t)(CMD_ARG_INDEX + n), (uint8_t)(resume - 1));   /* its last matched character */
        fw_wr8(f, (uint16_t)(CMD_POSITIONS + n), fw_rd8(f, FW_TMP_78));
        uint8_t h = (uint8_t)(n << 1);                                     /* ASL: an 8-bit index */
        fw_wr8(f, (uint16_t)(CMD_HANDLERS + h), fw_ind_y(f, CMD_PAT_PTR, (uint8_t)(len + 1)));
        a = fw_ind_y(f, CMD_PAT_PTR, (uint8_t)(len + 2));
        fw_wr8(f, (uint16_t)(CMD_HANDLERS + 1 + h), a);
        x = fw_rd8(f, FW_TMP_76);
        y = fw_rd8(f, FW_TMP_78);
    } while (x < fw_rd8(f, BOOT_CLAUSE_LEN));       /* $AD27 */
    fw_wr8(f, BOOT_CLAUSE_LEN, y);
    fw_wr8(f, CMD_NEXT_POS, fw_rd8(f, CMD_POSITIONS));
}
```

`src/engine/thunks.c`:
- Replace

```c
#include "firmware/cmd_match.h"
```

with

```c
#include "firmware/cmd_match.h"
#include "firmware/cmd_scan.h"
```

- Replace

```c
/* cmd_match_table $ADBF: no register input (X is passed through); Y out, A = $FF at the end marker (Y = 0), else
```

with

```c
/* cmd_scan_clause $ACFA: no register input or output (boot_loop_text calls tn_convert_clause next, which starts
   with LDM/LDX #0 and reads no register) */
static void cmd_scan_clause_regs(fw *f, cpu740 *c)
{
    (void)c;
    cmd_scan_clause(f);
}

/* cmd_match_table $ADBF: no register input (X is passed through); Y out, A = $FF at the end marker (Y = 0), else
```

- Replace

```c
THUNK(cmd_match_table)
```

with

```c
THUNK(cmd_scan_clause)
THUNK(cmd_match_table)
```

- Replace

```c
    { "cmd_match_table",    0xADBF, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_match_table },
```

with

```c
    { "cmd_scan_clause",    0xACFA, 0,                   t_cmd_scan_clause },
    { "cmd_match_table",    0xADBF, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_match_table },
```

After this task, the ROM `cmd_scan_clause` no longer runs in the hybrid's all-routines mode. `cmd_match_table`'s hook is then entered from the interrupt-level `cmd_imm_match`; the C scan calls the C `cmd_match_table` directly. `diff_lts` still checks both, because it runs the ROM engine.

**Faithfulness check.** Before committing, read `rom_ACFA_cmd_scan_clause.txt` side by side with this C, one instruction at a time. Confirm:
- every RAM write: `cmd_count`, `cmd_next`, `cmd_positions[0]`, `cmd_text[Y]`, `$78` at `$AD1B` and `$AD4F`, `cmd_table_ptr`, `cmd_table_ofs`, `cmd_text_ptr`, `$76`, `cmd_arg_index[n]`, `cmd_positions[n]`, both handler bytes, `boot_clause_len` and `cmd_next_pos`;
- every 8-bit wrap: `X++`, `Y++`, X + Y, `n << 1`, `Y+1`, `Y+2`;
- the do-while end test `CPX boot_clause_len; BCC`;
- the `BBS0` test.

Then read `rom_C5D6_isr_par_ready.txt` and its tail at `$C5EA` (in `rom_C5DE_isr_par_hold.txt`): A, X and Y are preserved, and only `boot_dip` is read. Read `rom_A68D_boot_loop_text.txt` and the first instructions of `rom_B1E6_tn_convert_clause.txt`: no register is read after the scan. Record the result in the report.

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test fw_cmd"`, then `cmd //c ".\build.bat test diff_lts"`, then `cmd //c ".\build.bat test golden_hybrid"`, then `cmd //c ".\build.bat test"`
Expected:
- `suite_fw_cmd                 ok`.
- `diff_lts`: 14 lines, each with N > 0, including `cmd_scan_clause      N calls checked` (one call per clause, in every corpus file and at boot) and `cmd_match_table      N calls checked`. There are 0 mismatches, and the flush line is as after Task 2.
- `golden_hybrid`: `32 corpus files x 15 modes (all, then each routine alone)`, with no frame differences.
- All 30 suites ok, with no warnings, and the firmware syntax check passing.

If `diff_lts` reports a mismatch for `cmd_scan_clause`, the message names the first RAM address that differs. Find the root cause against `rom_ACFA_cmd_scan_clause.txt` (`$78` and `$76` are the likely suspects) and fix the C faithfully. Never loosen the comparison.

- [ ] **Step 5: Commit**

```bash
git add src/firmware/cmd_scan.h src/firmware/cmd_scan.c src/engine/thunks.c tests/test_fw_cmd.c
git commit -m "cmd_scan_clause in C: the clause scan cuts commands out of cmd_text and queues them, isr_par_ready per character; hooked for boot_loop_text (M4.2 2.3)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: The dispatcher in C, with the ROM `cmd_run_next` as fallback; the C `tn_convert_clause` calls it directly (spec §2.3, §2.4)

**Files:**
- Create: `src/firmware/cmd_run.h`, `src/firmware/cmd_run.c`, `src/firmware/cmd_handlers.h`
- Modify: `src/firmware/tn_convert.c`, `src/firmware/tn_convert.h`, `src/engine/thunks.c`, `tests/test_fw_tn.c`, `tests/test_fw_cmd.c`, `tests/test_golden_hybrid.c`

**Interfaces:**
- Consumes:
  - Task 1: `fw_call_result` (`FW_CALL_FAILED`, `FW_CALL_RETURNED`, `FW_CALL_UNWOUND`) as the host's `rom_call` result; `fw_rom_call` returns false (and longjmps under a trap) for a failed or unwound call.
  - Task 2: `runs_c_dispatch` in `tests/test_golden_hybrid.c`, and the `bank_select` unwind assertion that uses it.
  - Task 3: `src/firmware/cmd_addr.h` (`ROM_CMD_RUN_NEXT`, `ROM_BANK_CMD_SELECT`).
  - Task 4: the harness of `tests/test_fw_cmd.c` (`rom_call_record`, `g_ram`, `g_f`, `g_calls`, `g_ncalls`, `mock_rom_call`, `setup()`, `suite_fw_cmd`).
  - Task 5: the `cmd_scan_clause` thunk, its `THUNK` line and its table row in `src/engine/thunks.c` (this task's go before them, in address order).
- Produces (used by Tasks 7–9):

```c
/* cmd_run.h */
void cmd_run_due(fw *f, uint8_t x);
/* cmd_handlers.h */
typedef void cmd_handler(fw *f, uint8_t x, uint8_t a);
/* cmd_run.c: the lookup Tasks 7-9 add rows to, before the { 0, NULL } row */
static const struct { uint16_t addr; cmd_handler *fn; } k_handlers[];
```

- The replacement `"cmd_run_due"` at `0xA880` with outputs `FW_OUT_X | FW_OUT_Y` (15 routines after this task).
- In `tests/test_fw_cmd.c`: the record fields `next`, `next_pos` and `vector` (the dispatcher's RAM at each ROM call), `g_before` and `g_touched`, the mock's answer to `ROM_CMD_RUN_NEXT` (the ROM's bookkeeping), and `queue(n, pos, h, arg)`. Tasks 7–9 use them.
- Removes: `ROM_CMD_RUN_DUE` from `tn_convert.h` (no C calls `$A880` in ROM any more; its only users, `tn_convert.c` and `tests/test_fw_tn.c`, are rewritten here).

**ROM facts checked while writing this task** (`rom_A880_cmd_run_due.txt`, `rom_A893_cmd_run_next.txt`, `rom_B1E6_tn_convert_clause.txt`, `rom_ABAF_bank_cmd_select.txt`, `rom_A600_bank_load_ramcode.txt`):
- `cmd_run_due` is `LDY cmd_next / CPY cmd_count / BCS done / CPX cmd_next_pos / BNE done / JSR cmd_run_next / BRA` back to the `LDY`. So on exit X is the entry X (every handler returns through `cmd_run_done`'s `PLA / TAX`), and Y = `cmd_next` from the last `LDY`. A is the entry A if nothing ran, else X (the `PLA`).
- Its only caller is `tn_convert_clause`. After `$B20C` it reads X (`$B20F CPX tn_pos`, then `LDA $1A00,X` at `$B21B`/`$B22D`) and writes Y before reading it (`$B21E LDY`, `$B230 TAY`). After `$B29E` it returns straight to `boot_loop_text`, which returns. So X is the only output any caller reads; the thunk also declares Y, because the ROM leaves it defined (stricter). A is not declared: no caller reads it.
- `cmd_run_next` reads Y nowhere, but the handler it jumps to gets Y unchanged, and **`bank_cmd_select` → `bank_load_ramcode` saves Y in `$78` (`$A601 STY $78`) and X in `$76`.** So the fallback `fw_rom_call` must pass Y = `cmd_next` (cmd_run_due's `LDY`) and X = the position exactly; A is overwritten by `cmd_run_next`'s first `TXA`.
- `cmd_run_next` writes `cmd_vector` ($1489, RAM), `cmd_next` (`INC`, 8-bit) and `cmd_next_pos` = `cmd_positions[cmd_next]` (the new value); the handler index is `(cmd_next * 2) & $FF` (`ASL A / TAX`), the argument index is `cmd_arg_index[cmd_next - 1]` (`DEX`, 8-bit), A = `boot_clause[X]`.

- [ ] **Step 1: Write the failing tests**

`tests/test_fw_cmd.c` (Task 4's harness, extended so that the mock plays the ROM `cmd_run_next`):
- Replace

```c
#include "firmware/cmd_addr.h"
```

with

```c
#include "firmware/cmd_addr.h"
#include "firmware/cmd_handlers.h"
#include "firmware/cmd_run.h"
```

- Replace

```c
typedef struct { uint16_t addr; uint8_t a, x, y; uint64_t budget; } rom_call_record;
```

with

```c
/* One ROM call, with the dispatcher's RAM as the C left it at the call. */
typedef struct {
    uint16_t addr;
    uint8_t a, x, y;
    uint64_t budget;
    uint8_t next, next_pos;
    uint16_t vector;
} rom_call_record;
```

- Replace

```c
static unsigned g_ncalls;
```

with

```c
static unsigned g_ncalls;
static uint8_t g_before[FW_RAM_SIZE];   /* RAM as a test left it: the C must not touch it before a ROM cmd_run_next */
static unsigned g_touched;              /* ROM cmd_run_next calls that found RAM changed since g_before */
```

- In `mock_rom_call`, replace

```c
        c->budget = budget;
    }
    g_ncalls++;
    /* isr_par_ready leaves A, X and Y as they were: the registers come back unchanged */
    return addr == ROM_ISR_PAR_READY ? FW_CALL_RETURNED : FW_CALL_FAILED;
```

with

```c
        c->budget = budget;
        c->next = g_ram[CMD_NEXT];
        c->next_pos = g_ram[CMD_NEXT_POS];
        c->vector = (uint16_t)(g_ram[CMD_VECTOR] | g_ram[CMD_VECTOR + 1] << 8);
    }
    g_ncalls++;
    if (addr == ROM_CMD_RUN_NEXT) {
        /* cmd_run_next: count it if the C changed RAM since g_before, then play the ROM's bookkeeping
           (cmd_next += 1, cmd_next_pos = cmd_positions[cmd_next]) so that cmd_run_due moves on */
        g_touched += memcmp(g_ram, g_before, sizeof g_ram) != 0;
        uint8_t n = (uint8_t)(g_ram[CMD_NEXT] + 1);
        g_ram[CMD_NEXT] = n;
        g_ram[CMD_NEXT_POS] = g_ram[CMD_POSITIONS + n];
        memcpy(g_before, g_ram, sizeof g_ram);
        return FW_CALL_RETURNED;   /* X back as it was (cmd_run_done) */
    }
    /* isr_par_ready leaves A, X and Y as they were: the registers come back unchanged */
    return addr == ROM_ISR_PAR_READY ? FW_CALL_RETURNED : FW_CALL_FAILED;
```

- In `setup`, replace

```c
    g_ncalls = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
```

with

```c
    g_ncalls = 0;
    g_touched = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
```

- Above `void suite_fw_cmd(void)`, add:

```c
/* Queues command n as cmd_scan_clause does: due at cmd_text position pos, handler address h, argument at
   boot_clause[arg]; cmd_count = n + 1, cmd_next_pos = cmd_positions[0]. */
static void queue(uint8_t n, uint8_t pos, uint16_t h, uint8_t arg)
{
    g_ram[CMD_POSITIONS + n] = pos;
    g_ram[CMD_HANDLERS + 2 * n] = (uint8_t)h;
    g_ram[CMD_HANDLERS + 2 * n + 1] = (uint8_t)(h >> 8);
    g_ram[CMD_ARG_INDEX + n] = arg;
    g_ram[CMD_COUNT] = (uint8_t)(n + 1);
    g_ram[CMD_NEXT_POS] = g_ram[CMD_POSITIONS];
}

static void test_run_due_nothing_queued(void)
{
    setup();                                    /* cmd_count = 0; cmd_next_pos = 0 matches X = 0 */
    memcpy(g_before, g_ram, sizeof g_ram);
    cmd_run_due(&g_f, 0);
    CHECK_EQ(g_ncalls, 0);
    CHECK(memcmp(g_ram, g_before, sizeof g_ram) == 0);
    CHECK(!g_f.fault);
}

static void test_run_due_position(void)
{
    setup();
    queue(0, 3, ROM_BANK_CMD_SELECT, 5);
    g_ram[CMD_POSITIONS + 1] = 0xFF;            /* the scan leaves it stale */
    memcpy(g_before, g_ram, sizeof g_ram);
    cmd_run_due(&g_f, 2);                       /* not due at 2 */
    CHECK_EQ(g_ncalls, 0);
    cmd_run_due(&g_f, 3);
    CHECK_EQ(g_ncalls, 1);
    CHECK_EQ(g_calls[0].addr, ROM_CMD_RUN_NEXT);
    CHECK_EQ(g_calls[0].x, 3);                  /* X = the position, as at $A88D */
    CHECK_EQ(g_calls[0].y, 0);                  /* Y = cmd_next ($A880 LDY): bank_load_ramcode saves it */
    CHECK_EQ(g_calls[0].budget, FW_ROM_CALL_BUDGET);
    CHECK_EQ(g_touched, 0);                     /* the C left the bookkeeping to the ROM */
    CHECK_EQ(g_calls[0].vector, 0);
    CHECK_EQ(g_ram[CMD_NEXT], 1);               /* the mock's (the ROM's) bookkeeping */
    cmd_run_due(&g_f, 3);                       /* cmd_next = cmd_count: nothing more */
    CHECK_EQ(g_ncalls, 1);
    CHECK(!g_f.fault);
}

/* Commands due at one position run in queue order; an unknown handler address (the latent NUL bug of the scan,
   spec 2.6) goes to the ROM like bank_cmd_select. */
static void test_run_due_queue_order(void)
{
    setup();
    queue(0, 2, ROM_BANK_CMD_SELECT, 4);
    queue(1, 2, 0x1234, 6);
    queue(2, 5, ROM_BANK_CMD_SELECT, 8);
    g_ram[CMD_POSITIONS + 3] = 0xFF;
    memcpy(g_before, g_ram, sizeof g_ram);
    cmd_run_due(&g_f, 2);
    CHECK_EQ(g_ncalls, 2);
    cmd_run_due(&g_f, 5);
    CHECK_EQ(g_ncalls, 3);
    for (unsigned i = 0; i < 3; i++) {
        CHECK_EQ(g_calls[i].addr, ROM_CMD_RUN_NEXT);
        CHECK_EQ(g_calls[i].x, i < 2 ? 2 : 5);
        CHECK_EQ(g_calls[i].y, i);
        CHECK_EQ(g_calls[i].next, i);
    }
    CHECK_EQ(g_touched, 0);
    CHECK_EQ(g_ram[CMD_NEXT], 3);
    CHECK(!g_f.fault);
}
```

- In `suite_fw_cmd`, after `test_scan_empty();`, add:

```c
    test_run_due_nothing_queued();
    test_run_due_position();
    test_run_due_queue_order();
```

`tests/test_fw_tn.c`: replace the whole file (as Task 1 left it) with:

```c
/* Unit checks of tn_convert_clause and tn_append_phonetic on the converted data, with a mock host that records
   every ROM call. tn_convert_clause runs the deferred commands through the C cmd_run_due; the tests queue
   commands whose handler stays in ROM (bank_cmd_select), so the mock sees each one as a cmd_run_next call. The
   full proof is the live differential test (diff_lts) and the hybrid goldens (golden_hybrid). */
#include <string.h>
#include "test.h"
#include "firmware/cmd_addr.h"
#include "firmware/tn_convert.h"
#include "firmware/tn_rules.h"
#include "firmware/data/fw_data.h"

#define MAX_CALLS 1024

typedef struct { uint16_t addr; uint8_t a, x, y, phon_len; uint64_t budget; } rom_call_record;

static uint8_t g_ram[FW_RAM_SIZE];
static fw g_f;
static fw_host g_host;
static bool g_window;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;

/* Records every call with tn_phon_len at that moment. cmd_run_next ($A893) plays the ROM's bookkeeping
   (cmd_next += 1, cmd_next_pos = cmd_positions[cmd_next]); the other helpers leave the registers unchanged. */
static fw_call_result mock_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    fw *f = ctx;
    if (g_ncalls < MAX_CALLS) {
        rom_call_record *c = &g_calls[g_ncalls];
        c->addr = addr;
        c->a = r->a;
        c->x = r->x;
        c->y = r->y;
        c->phon_len = fw_rd8(f, TN_PHON_LEN);
        c->budget = budget;
    }
    g_ncalls++;
    if (addr == ROM_CMD_RUN_NEXT) {
        uint8_t n = (uint8_t)(fw_rd8(f, CMD_NEXT) + 1);
        fw_wr8(f, CMD_NEXT, n);
        fw_wr8(f, CMD_NEXT_POS, fw_rd8(f, (uint16_t)(CMD_POSITIONS + n)));
        return FW_CALL_RETURNED;
    }
    if (addr == ROM_ISR_PAR_READY || addr == ROM_PROS_MAKE_SEGMENTS || addr == ROM_BOOT_LOOP_PLAY)
        return FW_CALL_RETURNED;
    return FW_CALL_FAILED;
}

static void mock_window(void *ctx, bool on)
{
    (void)ctx;
    g_window = on;
}

static void setup_n(const char *text, size_t n)
{
    memset(g_ram, 0, sizeof g_ram);
    g_host.ctx = &g_f;
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = mock_window;
    g_window = false;
    g_ncalls = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
    memcpy(g_ram + CMD_TEXT, text, n);
    g_ram[BOOT_CLAUSE_LEN] = (uint8_t)n;
    g_ram[CMD_VOICE] = 1;
}

static void setup(const char *text) { setup_n(text, strlen(text)); }

/* Queues command n as cmd_scan_clause does, due at cmd_text position pos, with a handler that stays in ROM. */
static void queue(uint8_t n, uint8_t pos)
{
    g_ram[CMD_POSITIONS + n] = pos;
    fw_wr16(&g_f, (uint16_t)(CMD_HANDLERS + 2 * n), ROM_BANK_CMD_SELECT);
    g_ram[CMD_ARG_INDEX + n] = n;
    g_ram[CMD_COUNT] = (uint8_t)(n + 1);
    g_ram[CMD_NEXT_POS] = g_ram[CMD_POSITIONS];
}

static unsigned count(uint16_t addr)
{
    unsigned n = 0;
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++)
        n += g_calls[i].addr == addr;
    return n;
}

/* Every helper call has the strict budget. */
static void check_helper_budgets(void)
{
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++)
        if (g_calls[i].addr == ROM_ISR_PAR_READY || g_calls[i].addr == ROM_CMD_RUN_NEXT)
            CHECK_EQ(g_calls[i].budget, FW_ROM_CALL_BUDGET);
}

/* the spoken name of c: the output of the first rule of its list */
static const char *name_of(uint8_t c) { return tn_char_lists[c]->rules[0].output; }

static void test_no_commands_queued(void)
{
    setup("Ab c");                              /* cmd_count = 0, though cmd_next_pos = 0 matches */
    tn_convert_clause(&g_f);
    CHECK_EQ(count(ROM_CMD_RUN_NEXT), 0);
    CHECK_EQ(g_ram[CMD_NEXT], 0);
    CHECK_EQ(fw_rd16(&g_f, CMD_VECTOR), 0);
    CHECK(!g_f.fault);
}

static void test_phonetic_mode(void)
{
    setup("Ab c");
    g_ram[CMD_PHONETIC] = 1;
    g_ram[CMD_EV_COUNT] = 9;
    for (uint8_t n = 0; n < 5; n++)
        queue(n, n);                            /* one command due at each position 0-4 */
    tn_convert_clause(&g_f);
    CHECK_EQ(g_ram[TN_PHON_LEN], 4);
    CHECK(memcmp(g_ram + TN_PHONETIC, "Ab c", 4) == 0);
    CHECK_EQ(g_ram[TN_POS], 4);
    CHECK_EQ(g_ram[CMD_EV_COUNT], 0);
    CHECK_EQ(count(ROM_ISR_PAR_READY), 4);
    /* each command runs before its character is copied, with X = its position; the one due at the clause end
       never runs, because phonetic mode has no final cmd_run_due */
    CHECK_EQ(count(ROM_CMD_RUN_NEXT), 4);
    unsigned k = 0;
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++) {
        if (g_calls[i].addr != ROM_CMD_RUN_NEXT || k >= 4)
            continue;
        CHECK_EQ(g_calls[i].x, k);
        CHECK_EQ(g_calls[i].y, k);              /* Y = cmd_next */
        CHECK_EQ(g_calls[i].phon_len, k);
        k++;
    }
    CHECK_EQ(g_ram[CMD_NEXT], 4);
    check_helper_budgets();
    CHECK(!g_f.fault);
}

static void test_copy_and_final_run_due(void)
{
    setup(".");
    g_ram[FW_TMP_75] = 0xAA;                    /* the copy must overwrite it with X = 0 */
    queue(0, 0);
    queue(1, 1);                                /* due at the clause end */
    tn_convert_clause(&g_f);
    CHECK_EQ(g_ram[TN_PHON_LEN], 1);
    CHECK_EQ(g_ram[TN_PHONETIC], '.');
    CHECK_EQ(g_ram[FW_TMP_75], 0);              /* X saved around the copy */
    CHECK_EQ(g_ram[TN_POS], 1);
    CHECK_EQ(count(ROM_CMD_RUN_NEXT), 2);
    CHECK_EQ(g_calls[1].addr, ROM_CMD_RUN_NEXT);   /* after isr_par_ready, before the copy */
    CHECK_EQ(g_calls[1].x, 0);
    CHECK_EQ(g_calls[1].phon_len, 0);
    CHECK_EQ(g_calls[g_ncalls - 1].addr, ROM_CMD_RUN_NEXT);   /* the final cmd_run_due, after the copy */
    CHECK_EQ(g_calls[g_ncalls - 1].x, 1);
    CHECK_EQ(g_calls[g_ncalls - 1].y, 1);
    CHECK_EQ(g_calls[g_ncalls - 1].phon_len, 1);
    check_helper_budgets();
    CHECK(!g_f.fault);
}

static void test_punct_spoken(void)
{
    setup(".");
    g_ram[CMD_PUNCT] = 1;                       /* @P1 */
    tn_convert_clause(&g_f);
    const char *name = name_of('.');
    CHECK_EQ(g_ram[TN_PHON_LEN], strlen(name));
    CHECK(memcmp(g_ram + TN_PHONETIC, name, strlen(name)) == 0);
    CHECK_EQ(g_ram[TN_POS], 1);
    CHECK(!g_f.fault);
}

static void test_vowelless_word_spelled(void)
{
    setup(" BCD");                              /* the leading space puts the word at tn_key_pos 1 */
    tn_convert_clause(&g_f);
    CHECK_EQ(g_ram[CMD_SPELL], 3);              /* automatic spelling */
    char want[64] = " ";
    strcat(want, name_of('B'));
    strcat(want, name_of('C'));
    strcat(want, name_of('D'));
    CHECK_EQ(g_ram[TN_PHON_LEN], strlen(want));
    CHECK(memcmp(g_ram + TN_PHONETIC, want, strlen(want)) == 0);
    CHECK_EQ(g_ram[TN_IN_WORD] & 1, 1);
    CHECK_EQ(g_ram[TN_POS], 4);
    CHECK(!g_f.fault);
}

static void test_digit_to_rules(void)
{
    setup("7");
    tn_convert_clause(&g_f);
    CHECK(g_ram[TN_PHON_LEN] > 0);
    CHECK_EQ(g_ram[TN_IN_WORD] & 1, 1);         /* digits set tn_in_word like letters */
    CHECK(!g_window);                           /* CLB7 P2 after the rules */
    CHECK_EQ(g_ram[TN_POS], 1);
    CHECK(!g_f.fault);
}

static void test_flush(void)
{
    char text[255];
    for (int i = 0; i < 255; i++)
        text[i] = (char)('A' + i % 26);
    setup_n(text, sizeof text);
    g_ram[CMD_PHONETIC] = 1;                    /* verbatim: one phonetic byte per character */
    tn_convert_clause(&g_f);
    CHECK_EQ(g_f.tn_flushes, 1);
    CHECK_EQ(g_ram[TN_RESUME_POS], 250);
    CHECK_EQ(g_ram[TN_PHON_LEN], 5);            /* the second pass converted text[250..254] */
    CHECK(memcmp(g_ram + TN_PHONETIC, text + 250, 5) == 0);
    CHECK_EQ(g_ram[TN_POS], 255);
    CHECK_EQ(count(ROM_PROS_MAKE_SEGMENTS), 1);
    CHECK_EQ(count(ROM_BOOT_LOOP_PLAY), 1);
    for (unsigned i = 0; i + 1 < g_ncalls && i + 1 < MAX_CALLS; i++) {
        if (g_calls[i].addr != ROM_PROS_MAKE_SEGMENTS)
            continue;
        CHECK_EQ(g_calls[i].a, TN_PHONETIC_FLUSH);   /* A = tn_phon_len, X = tn_pos, as at $B201 */
        CHECK_EQ(g_calls[i].x, 250);
        CHECK_EQ(g_calls[i].budget, FW_ROM_CALL_SPEECH_BUDGET);
        CHECK_EQ(g_calls[i + 1].addr, ROM_BOOT_LOOP_PLAY);
        CHECK_EQ(g_calls[i + 1].budget, FW_ROM_CALL_SPEECH_BUDGET);
    }
    check_helper_budgets();
    CHECK(!g_f.fault);
}

static void put_rule_text(void)   /* a length byte 3, then "abc", at $0300 = tn_rule_ptr */
{
    static const uint8_t text[] = { 3, 'a', 'b', 'c' };
    memcpy(g_ram + 0x0300, text, sizeof text);
    fw_wr16(&g_f, TN_RULE_PTR, 0x0300);
}

static void test_append_voice_on(void)
{
    setup("");
    put_rule_text();
    g_ram[TN_PHON_LEN] = 2;
    uint8_t x = 0x55, y = 0;
    CHECK_EQ(tn_append_phonetic(&g_f, 3, &x, &y), 0xFF);
    CHECK_EQ(x, 5);
    CHECK_EQ(y, 3);                             /* on the last byte copied */
    CHECK_EQ(g_ram[TN_PHON_LEN], 5);
    CHECK(memcmp(g_ram + TN_PHONETIC + 2, "abc", 3) == 0);
    CHECK_EQ(g_ram[FW_TMP_78], 0);              /* counted down */
    CHECK_EQ(g_ncalls, 0);                      /* no ROM call */
    CHECK(!g_f.fault);
}

static void test_append_voice_off(void)
{
    setup("");
    put_rule_text();
    g_ram[CMD_VOICE] = 0;
    g_ram[TN_PHON_LEN] = 2;
    uint8_t x = 0x55, y = 0;
    CHECK_EQ(tn_append_phonetic(&g_f, 3, &x, &y), 0xFF);
    CHECK_EQ(x, 2);                             /* X = tn_phon_len */
    CHECK_EQ(y, 0);
    CHECK_EQ(g_ram[TN_PHON_LEN], 2);
    CHECK_EQ(g_ram[TN_PHONETIC + 2], 0);
    CHECK_EQ(g_ram[FW_TMP_78], 3);              /* the length stays */
    CHECK(!g_f.fault);
}

static void test_append_empty(void)
{
    setup("");
    put_rule_text();
    g_ram[FW_TMP_78] = 0x99;
    uint8_t x = 0x55, y = 7;
    CHECK_EQ(tn_append_phonetic(&g_f, 0, &x, &y), 0xFF);
    CHECK_EQ(x, 0x55);
    CHECK_EQ(y, 7);
    CHECK_EQ(g_ram[FW_TMP_78], 0x99);
    CHECK_EQ(g_ram[TN_PHON_LEN], 0);
    CHECK(!g_f.fault);
}

void suite_fw_tn(void)
{
    CHECK(fw_image_get() != NULL);
    test_append_voice_on();
    test_append_voice_off();
    test_append_empty();
    test_no_commands_queued();
    test_phonetic_mode();
    test_copy_and_final_run_due();
    test_punct_spoken();
    test_vowelless_word_spelled();
    test_digit_to_rules();
    test_flush();
}
```

What changed against the file as Task 1 left it, and why:
- The mock no longer answers `ROM_CMD_RUN_DUE`. It plays the ROM `cmd_run_next`'s bookkeeping instead, because the C `cmd_run_due` now loops until `cmd_next` moves.
- The records also keep Y and `tn_phon_len`.
- The old exact `cmd_run_due` call sequence (0, 0, 1, 1, 2, 2, 3) cannot be seen any more, because C calls C. Its observable effects are checked instead:
  - each command runs with X = its position, before its character is copied;
  - in phonetic mode, a command due at the clause end never runs;
  - in normal mode, it runs in the final call, after the copy;
  - with `cmd_count` = 0, nothing runs.
- `check_helper_budgets` checks `ROM_CMD_RUN_NEXT` in place of `ROM_CMD_RUN_DUE`.

`tests/test_golden_hybrid.c`: in `runs_c_dispatch` (Task 2), replace

```c
    return !only || !strcmp(only, "tn_convert_clause");
```

with

```c
    return !only || !strcmp(only, "tn_convert_clause") || !strcmp(only, "cmd_run_due");
```

This is the only change to the file: from now on `bank_select` must unwind in all-routines mode, in `tn_convert_clause` alone and in `cmd_run_due` alone (spec 5.3).

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw_cmd"`
Expected: a compile error, because `firmware/cmd_handlers.h` is missing (C1083).

- [ ] **Step 3: Implement**

`src/firmware/cmd_handlers.h`:

```c
#ifndef FW_CMD_HANDLERS_H
#define FW_CMD_HANDLERS_H
/* The deferred command handlers (ROM $A8C0-$ABDE), decompiled (M4.2 spec 2.2). The ROM enters each one by
   JMP (cmd_vector) from cmd_run_next, with X = its cmd_arg_index entry and A = boot_clause[X], and each one ends
   with JMP cmd_run_done, which restores X; cmd_run_due then reloads Y. So a handler has no register output. Only
   the dispatcher (cmd_run.c) calls them. */
#include "fw.h"

typedef void cmd_handler(fw *f, uint8_t x, uint8_t a);

#endif
```

`src/firmware/cmd_run.h`:

```c
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
```

`src/firmware/cmd_run.c`:

```c
#include <stddef.h>
#include "cmd_run.h"
#include "cmd_addr.h"
#include "cmd_handlers.h"

/* The C handlers by ROM address: the bytes cmd_scan_clause copies into cmd_handlers. A command whose address is
   not here runs through the ROM cmd_run_next (spec 2.4). The NULL row ends the table. */
static const struct { uint16_t addr; cmd_handler *fn; } k_handlers[] = {
    { 0, NULL },
};

static cmd_handler *find_handler(uint16_t addr)
{
    for (unsigned i = 0; k_handlers[i].fn; i++)
        if (k_handlers[i].addr == addr)
            return k_handlers[i].fn;
    return NULL;
}

/* $A893 cmd_run_next, the handler it jumps to, and $A8BD cmd_run_done. False when the ROM call failed or
   unwound; that only returns here without a trap. */
static bool run_next(fw *f, uint8_t x)
{
    uint8_t n = fw_rd8(f, CMD_NEXT);
    uint8_t i = (uint8_t)(n << 1);                                      /* ASL A / TAX: an 8-bit index */
    uint16_t addr = (uint16_t)(fw_rd8(f, (uint16_t)(CMD_HANDLERS + i)) |
                               fw_rd8(f, (uint16_t)(CMD_HANDLERS + 1 + i)) << 8);
    cmd_handler *h = find_handler(addr);
    if (!h) {
        /* No C handler: the ROM cmd_run_next does everything, starting from RAM as the C found it. Registers
           as at $A88D: X = the position, Y = cmd_next (the LDY at $A880; bank_load_ramcode saves it in $78), and
           A = X, which cmd_run_next's TXA makes it anyway. */
        fw_regs r = { x, x, n, 0 };
        return fw_rom_call(f, ROM_CMD_RUN_NEXT, &r, FW_ROM_CALL_BUDGET);
    }
    fw_wr16(f, CMD_VECTOR, addr);                                       /* cmd_vector is RAM: kept exact */
    n++;                                                                /* INC cmd_next */
    fw_wr8(f, CMD_NEXT, n);
    fw_wr8(f, CMD_NEXT_POS, fw_rd8(f, (uint16_t)(CMD_POSITIONS + n)));  /* LDX cmd_next / LDA $148B,X */
    uint8_t ax = fw_rd8(f, (uint16_t)(CMD_ARG_INDEX + (uint8_t)(n - 1)));   /* DEX / LDA $160B,X / TAX */
    h(f, ax, fw_rd8(f, (uint16_t)(BOOT_CLAUSE + ax)));                  /* JMP ($1489) */
    return true;                                                        /* cmd_run_done: X back to x */
}

void cmd_run_due(fw *f, uint8_t x)
{
    while (fw_rd8(f, CMD_NEXT) < fw_rd8(f, CMD_COUNT) && x == fw_rd8(f, CMD_NEXT_POS)) {
        if (!run_next(f, x))
            return;
    }
}
```

`src/firmware/tn_convert.c`:
- After `#include "tn_convert.h"`, add `#include "cmd_run.h"`.
- Replace the comment line above `rom_call_x`:

```c
/* A ROM helper that reads only X (isr_par_ready, cmd_run_due); returns the X it leaves. */
```

with:

```c
/* A ROM helper that reads only X (isr_par_ready); returns the X it leaves. */
```

- Replace:

```c
            for (;;) {                                          /* $B20A: the deferred commands up to tn_pos */
                x = rom_call_x(f, ROM_CMD_RUN_DUE, fw_rd8(f, TN_KEY_POS));
                if (x >= fw_rd8(f, TN_POS))
```

with:

```c
            for (;;) {                                          /* $B20A: the deferred commands up to tn_pos */
                x = fw_rd8(f, TN_KEY_POS);
                cmd_run_due(f, x);                              /* X kept: every handler ends at cmd_run_done */
                if (x >= fw_rd8(f, TN_POS))
```

- Replace:

```c
            if (x >= fw_rd8(f, BOOT_CLAUSE_LEN)) {
                rom_call_x(f, ROM_CMD_RUN_DUE, x);
                return;
            }
```

with:

```c
            if (x >= fw_rd8(f, BOOT_CLAUSE_LEN)) {
                cmd_run_due(f, x);
                return;
            }
```

`src/firmware/tn_convert.h`: delete the line

```c
#define ROM_CMD_RUN_DUE         0xA880   /* X = a clause position: run the deferred commands due there; X kept */
```

Then run `grep -rn "ROM_CMD_RUN_DUE" src tests`. It must print nothing.

`src/engine/thunks.c` (the rows stay in address order, so this task's go before Task 5's):
- Replace

```c
#include "firmware/cmd_scan.h"
```

with

```c
#include "firmware/cmd_scan.h"
#include "firmware/cmd_run.h"
```

- Replace

```c
/* cmd_scan_clause $ACFA: no register input or output (boot_loop_text calls tn_convert_clause next, which starts
```

with

```c
/* cmd_run_due $A880: X in. X out unchanged (every handler ends at cmd_run_done's PLA/TAX). Y out = cmd_next,
   as its last LDY leaves it. The only ROM caller, tn_convert_clause, reads X after it ($B20F CPX tn_pos, then
   LDA $1A00,X) and writes Y before reading it; Y is declared because the ROM leaves it defined. */
static void cmd_run_due_regs(fw *f, cpu740 *c)
{
    uint8_t x = c->x;
    cmd_run_due(f, x);
    c->x = x;
    c->y = fw_rd8(f, CMD_NEXT);
}

/* cmd_scan_clause $ACFA: no register input or output (boot_loop_text calls tn_convert_clause next, which starts
```

- Replace

```c
THUNK(cmd_scan_clause)
```

with

```c
THUNK(cmd_run_due)
THUNK(cmd_scan_clause)
```

- Replace

```c
    { "cmd_scan_clause",    0xACFA, 0,                   t_cmd_scan_clause },
```

with

```c
    { "cmd_run_due",        0xA880, FW_OUT_X | FW_OUT_Y, t_cmd_run_due },
    { "cmd_scan_clause",    0xACFA, 0,                   t_cmd_scan_clause },
```

**Faithfulness check.** Before committing, read `rom_A880_cmd_run_due.txt` and `rom_A893_cmd_run_next.txt` (including `cmd_run_done` at `$A8BD`) side by side with `cmd_run.c`, and confirm the following:
- the loop test (unsigned `cmd_next < cmd_count`, then `X == cmd_next_pos`);
- the 8-bit handler index `(cmd_next << 1) & $FF`;
- the three RAM writes: `cmd_vector` (both bytes), `cmd_next`, and `cmd_next_pos` taken from the incremented index;
- the argument index `cmd_next - 1` (8-bit);
- A = `boot_clause[X]`.

Then confirm in `rom_B1E6_tn_convert_clause.txt` that the X flow around `$B20C` and `$B29E` matches the edited `tn_convert.c`. Confirm in `rom_ABAF_bank_cmd_select.txt` and `rom_A600_bank_load_ramcode.txt` that the fallback's Y reaches `$78`. Record the result in the report.

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test fw_cmd"`, then `cmd //c ".\build.bat test fw_tn"`, then `cmd //c ".\build.bat test diff_lts"`, then `cmd //c ".\build.bat test golden_hybrid"`, then `cmd //c ".\build.bat test"`
Expected:
- `suite_fw_cmd` and `suite_fw_tn` ok.
- `diff_lts`: 15 lines `<name> N calls checked`, each with N > 0, and 0 mismatches, including `cmd_run_due          N calls checked`. Every handler still runs in ROM (through `cmd_run_next`), so this checks the dispatch, the bookkeeping and the X/Y outputs. The `bank_select` file's `@=1` call is an unwound call, and it is compared (Task 1). The flush line is as after Task 2.
- `golden_hybrid`: `32 corpus files x 16 modes (all, then each routine alone)`, all identical. There must be no "never flushed" message and no "never unwound" message: `bank_select` unwinds in all-routines mode, in `tn_convert_clause` alone and in `cmd_run_due` alone.
- All 30 suites ok, with no warnings, and the firmware syntax check passing.

If `diff_lts` reports a mismatch for `cmd_run_due`, find the root cause against the dumps and fix the C faithfully. Never loosen the comparison.

- [ ] **Step 5: Commit**

```bash
git add src/firmware/cmd_run.h src/firmware/cmd_run.c src/firmware/cmd_handlers.h src/firmware/tn_convert.c src/firmware/tn_convert.h src/engine/thunks.c tests/test_fw_cmd.c tests/test_fw_tn.c tests/test_golden_hybrid.c
git commit -m "Dispatcher in C: cmd_run_due/cmd_run_next with the ROM cmd_run_next as fallback; C tn_convert_clause calls it directly; hooked for the ROM tn_convert_clause (M4.2 2.4)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: The setter handlers that need no apply helper (spec §2.2)

**Files:**
- Create: `src/firmware/cmd_set.c`
- Modify: `src/firmware/cmd_handlers.h`, `src/firmware/cmd_run.c`, `tests/test_fw_cmd.c`

**Interfaces:**
- Consumes:
  - Task 4: `cmd_hex_value`, `cmd_find_back`, `util_shl4` (`cmd_match.h`), `FW_TMP_75`, and the harness of `tests/test_fw_cmd.c` (`g_ram`, `g_f`, `g_calls`, `g_ncalls`, `setup()`);
  - Task 3: the handler addresses in `cmd_addr.h` (`ROM_CMD_SAY_VERSION`, `ROM_CMD_SET_PHRASE_PAUSE`, `ROM_CMD_SET_FORMANT`, `ROM_CMD_SET_WORD_PAUSE`, `ROM_CMD_SET_MARK_SPACE`, `ROM_CMD_SET_SPELL`, `ROM_CMD_SET_PUNCT`, `ROM_CMD_SET_HYPER`, `ROM_CMD_SET_PHONETIC`, `ROM_CMD_IGNORE`, and `ROM_BANK_CMD_SELECT`, `ROM_CMD_RUN_NEXT` in the tests);
  - Task 5: `put_clause()` in the tests;
  - Task 6: `cmd_handler`, `cmd_run_due`, the `k_handlers` table with its `{ 0, NULL }` row, and in the tests `queue()`, `g_before` and the record fields `next`, `next_pos`, `vector`;
  - M4.1: `tn_append_phonetic` (`tn_rules.h`), and the version texts at `$9804`/`$F75B`, already converted (`fw_phonetic_texts`).
- Produces: in `cmd_handlers.h`, `cmd_say_version`, `cmd_set_phrase_pause`, `cmd_set_word_pause`, `cmd_set_mark_space`, `cmd_set_spell`, `cmd_set_punct`, `cmd_set_hyper`, `cmd_set_phonetic`, `cmd_set_formant` and `cmd_ignore`, all of type `cmd_handler`; their 10 rows in `k_handlers`. `cmd_put_phonetic` stays static in `cmd_set.c`, and `cmd_set.c` gets a static `clause_at(f, x)` that Task 8 uses. There is no new hook, so the count stays at 15 routines and 16 modes.

**ROM facts checked while writing this task** (`rom_A902_cmd_say_version.txt`, `rom_A868_cmd_put_phonetic.txt`, `rom_A91F`, `rom_A98A`, `rom_AA4F`, `rom_AAF5`, `rom_AB4D`, `rom_AB9F`, `rom_A99A_cmd_set_mark_space.txt`, `rom_A92F_cmd_set_formant.txt`, `rom_ABDC_cmd_ignore.txt`, `rom_ABDF_cmd_find_back.txt`, `rom_C520_cmd_hex_value.txt`):
- The one-digit setters accept these values of `cmd_hex_value` (unsigned; the `CMP #0 / BCC` is dead):
  - `@D`: < $10;
  - `@Q`: < 10;
  - `@S`, `@P`, `@H`, `@X`: < 2. `cmd_set_spell` gets the same range a different way (`BEQ` stores 0, then `CMP #2 / BCS`).
- `cmd_say_version` compares the raw A (`boot_clause[X]`) with `'?'` before doing anything. It sets `tn_rule_ptr` to `$9804`, then to `$F75B`, and each time `cmd_put_phonetic` passes the length byte at `(tn_rule_ptr),0` to `tn_append_phonetic` with Y = 0. The X it passes is overwritten without being read, and `cmd_run_done` restores X.
- `cmd_set_mark_space` re-anchors with `cmd_find_back('M')` (which writes `$75 = 'M'`). It takes the first digit (< 4) as `$75 = digit << 4`, then the second (< $10), and stores `cmd_mark_space = second + $75` (CLC/ADC).
- `cmd_set_formant` re-anchors with `cmd_find_back('U')`. It takes the parameter d (< 10), then the high digit (< $10) as `$75 = high << 4`, then the low digit (< $10) as `$75 += low`. The sign is the **raw** byte after them: `'-'` stores `0 − $75`, `'+'` stores `$75`, at `cmd_formant_ofs[d]`. Any other byte stores nothing. In the single-digit forms the sign fails as the low digit (`'+'` → $FB), so `$75` keeps `high << 4`.
- `cmd_ignore` is a bare `JMP cmd_run_done`.

- [ ] **Step 1: Write the failing tests**

In `tests/test_fw_cmd.c`, add these tests after `test_run_due_queue_order` (they use Task 4's `setup()`, Task 5's `put_clause()` and Task 6's `queue()` and `g_before`):

```c
/* A C handler gets cmd_run_next's writes from the C (cmd_vector, cmd_next, cmd_next_pos) and runs with
   A = boot_clause[cmd_arg_index]; a ROM handler after it in the queue still goes to the ROM. */
static void test_run_due_c_handler(void)
{
    setup();
    g_ram[BOOT_CLAUSE + 6] = '1';
    queue(0, 0, ROM_CMD_SET_SPELL, 6);
    g_ram[CMD_POSITIONS + 1] = 0x42;
    cmd_run_due(&g_f, 0);
    CHECK_EQ(g_ncalls, 0);
    CHECK_EQ(g_ram[CMD_SPELL], 1);
    CHECK_EQ(fw_rd16(&g_f, CMD_VECTOR), ROM_CMD_SET_SPELL);
    CHECK_EQ(g_ram[CMD_NEXT], 1);
    CHECK_EQ(g_ram[CMD_NEXT_POS], 0x42);

    setup();
    g_ram[BOOT_CLAUSE + 6] = '1';
    queue(0, 4, ROM_CMD_SET_SPELL, 6);
    queue(1, 4, ROM_BANK_CMD_SELECT, 9);
    g_ram[CMD_POSITIONS + 2] = 0x77;
    cmd_run_due(&g_f, 4);
    CHECK_EQ(g_ram[CMD_SPELL], 1);
    CHECK_EQ(g_ncalls, 1);                      /* only bank_cmd_select goes to the ROM */
    CHECK_EQ(g_calls[0].addr, ROM_CMD_RUN_NEXT);
    CHECK_EQ(g_calls[0].x, 4);
    CHECK_EQ(g_calls[0].y, 1);
    CHECK_EQ(g_calls[0].next, 1);               /* the C's bookkeeping for the first command */
    CHECK_EQ(g_calls[0].next_pos, 4);
    CHECK_EQ(g_calls[0].vector, ROM_CMD_SET_SPELL);
    CHECK_EQ(g_ram[CMD_NEXT], 2);
    CHECK_EQ(g_ram[CMD_NEXT_POS], 0x77);
    CHECK(!g_f.fault);
}

/* The one-digit setters: accepted and rejected values (cmd_hex_value, through the case map). */
static void test_set_ranges(void)
{
    static const struct { cmd_handler *h; uint16_t setting; uint8_t ok, want, bad; } k[] = {
        { cmd_set_phrase_pause, CMD_PHRASE_PAUSE, 'f', 15, 'G' },   /* 0-$F */
        { cmd_set_word_pause,   CMD_WORD_PAUSE,   '9', 9,  'A' },   /* 0-9 */
        { cmd_set_spell,        CMD_SPELL,        '1', 1,  '2' },   /* 0-1 */
        { cmd_set_punct,        CMD_PUNCT,        '1', 1,  '2' },
        { cmd_set_hyper,        CMD_HYPER,        '1', 1,  '2' },
        { cmd_set_phonetic,     CMD_PHONETIC,     '1', 1,  '2' },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        setup();
        g_ram[k[i].setting] = 0x5A;
        k[i].h(&g_f, 3, k[i].bad);
        CHECK_EQ(g_ram[k[i].setting], 0x5A);
        k[i].h(&g_f, 3, '+');                   /* cmd_hex_value('+') = $FB */
        CHECK_EQ(g_ram[k[i].setting], 0x5A);
        k[i].h(&g_f, 3, '0');
        CHECK_EQ(g_ram[k[i].setting], 0);
        k[i].h(&g_f, 3, k[i].ok);
        CHECK_EQ(g_ram[k[i].setting], k[i].want);
        CHECK_EQ(g_ncalls, 0);
        CHECK(!g_f.fault);
    }
}

static void test_set_mark_space(void)
{
    setup();
    put_clause(" @m2a");                        /* M**: X = 4, the last digit; lower case through the map */
    cmd_set_mark_space(&g_f, 4, 'a');
    CHECK_EQ(g_ram[CMD_MARK_SPACE], 0x2A);
    CHECK_EQ(g_ram[FW_TMP_75], 0x20);           /* the first digit times 16 */

    setup();
    put_clause(" @M3G");                        /* the second digit out of range */
    g_ram[CMD_MARK_SPACE] = 0x11;
    cmd_set_mark_space(&g_f, 4, 'G');
    CHECK_EQ(g_ram[CMD_MARK_SPACE], 0x11);
    CHECK_EQ(g_ram[FW_TMP_75], 0x30);

    setup();
    put_clause(" @MM5");                        /* re-anchors on the second M: the first digit is the 5 */
    g_ram[CMD_MARK_SPACE] = 0x11;
    cmd_set_mark_space(&g_f, 4, '5');
    CHECK_EQ(g_ram[CMD_MARK_SPACE], 0x11);
    CHECK_EQ(g_ram[FW_TMP_75], 'M');            /* cmd_find_back's letter, left there */
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

static void check_formant_clear(void)
{
    for (int i = 0; i < 10; i++)
        CHECK_EQ(g_ram[CMD_FORMANT_OFS + i], 0);
}

static void test_set_formant(void)
{
    setup();
    put_clause(" @u312+");                      /* U#**+: X = 6, the sign */
    cmd_set_formant(&g_f, 6, '+');
    CHECK_EQ(g_ram[CMD_FORMANT_OFS + 3], 0x12);
    CHECK_EQ(g_ram[FW_TMP_75], 0x12);

    setup();
    put_clause(" @U9A0-");
    cmd_set_formant(&g_f, 6, '-');
    CHECK_EQ(g_ram[CMD_FORMANT_OFS + 9], 0x60);    /* 0 - $A0 */
    CHECK_EQ(g_ram[FW_TMP_75], 0xA0);

    setup();
    put_clause(" @U312x");                      /* neither sign: nothing stored */
    cmd_set_formant(&g_f, 6, 'x');
    check_formant_clear();
    CHECK_EQ(g_ram[FW_TMP_75], 0x12);

    setup();
    put_clause(" @U31+");                       /* U#*+: matched, but the sign fails as the low digit */
    cmd_set_formant(&g_f, 5, '+');
    check_formant_clear();
    CHECK_EQ(g_ram[FW_TMP_75], 0x10);

    setup();
    put_clause(" @U3U+");                       /* re-anchors on the second U: the parameter is the '+' */
    cmd_set_formant(&g_f, 5, '+');
    check_formant_clear();
    CHECK_EQ(g_ram[FW_TMP_75], 'U');
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

static const char *version_text(const char *name)
{
    for (size_t i = 0; i < fw_phonetic_text_count; i++)
        if (!strcmp(fw_phonetic_texts[i].name, name))
            return fw_phonetic_texts[i].text;
    return NULL;
}

static void test_say_version(void)
{
    const char *e = version_text("cmd_version_engine_text"), *l = version_text("cmd_version_lang_text");
    CHECK(e != NULL && l != NULL);
    if (!e || !l)
        return;
    size_t ne = strlen(e), nl = strlen(l);
    setup();
    g_ram[CMD_VOICE] = 1;
    g_ram[TN_PHON_LEN] = 2;
    cmd_say_version(&g_f, 1, '?');
    CHECK_EQ(g_ram[TN_PHON_LEN], 2 + ne + nl);
    CHECK(memcmp(g_ram + TN_PHONETIC + 2, e, ne) == 0);
    CHECK(memcmp(g_ram + TN_PHONETIC + 2 + ne, l, nl) == 0);
    CHECK_EQ(fw_rd16(&g_f, TN_RULE_PTR), 0xF75B);    /* left at the language text */
    CHECK_EQ(g_ram[FW_TMP_78], 0);              /* tn_append_phonetic counted down */
    CHECK_EQ(g_ncalls, 0);

    setup();                                    /* A is not '?': nothing */
    g_ram[CMD_VOICE] = 1;
    cmd_say_version(&g_f, 1, '!');
    CHECK_EQ(fw_rd16(&g_f, TN_RULE_PTR), 0);
    CHECK_EQ(g_ram[TN_PHON_LEN], 0);
    CHECK(!g_f.fault);
}

static void test_ignore(void)
{
    setup();
    put_clause(" @G1");
    memcpy(g_before, g_ram, sizeof g_ram);
    cmd_ignore(&g_f, 3, '1');
    CHECK(memcmp(g_ram, g_before, sizeof g_ram) == 0);
    CHECK_EQ(g_ncalls, 0);
}
```

In `suite_fw_cmd`, after `test_run_due_queue_order();`, add:

```c
    test_run_due_c_handler();
    test_set_ranges();
    test_set_mark_space();
    test_set_formant();
    test_say_version();
    test_ignore();
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw_cmd"`
Expected: a compile error, because `cmd_set_spell` (and the other handlers) are undeclared.

- [ ] **Step 3: Implement**

`src/firmware/cmd_handlers.h`: replace

```c
typedef void cmd_handler(fw *f, uint8_t x, uint8_t a);

#endif
```

with

```c
typedef void cmd_handler(fw *f, uint8_t x, uint8_t a);

/* The setters without an apply helper (cmd_set.c) */
void cmd_say_version(fw *f, uint8_t x, uint8_t a);        /* $A902 @?: the version texts to tn_phonetic */
void cmd_set_phrase_pause(fw *f, uint8_t x, uint8_t a);   /* $A91F @D: 0-$F */
void cmd_set_word_pause(fw *f, uint8_t x, uint8_t a);     /* $A98A @Q: 0-9 */
void cmd_set_mark_space(fw *f, uint8_t x, uint8_t a);     /* $A99A @M: two digits, 0-$3F */
void cmd_set_spell(fw *f, uint8_t x, uint8_t a);          /* $AA4F @S: 0-1 */
void cmd_set_punct(fw *f, uint8_t x, uint8_t a);          /* $AAF5 @P: 0-1 */
void cmd_set_hyper(fw *f, uint8_t x, uint8_t a);          /* $AB4D @H: 0-1 */
void cmd_set_phonetic(fw *f, uint8_t x, uint8_t a);       /* $AB9F @X: 0-1 */
void cmd_set_formant(fw *f, uint8_t x, uint8_t a);        /* $A92F @U: parameter, two digits, sign */
void cmd_ignore(fw *f, uint8_t x, uint8_t a);             /* $ABDC @G, @I^, @+: nothing */

#endif
```

`src/firmware/cmd_set.c`:

```c
/* The setting handlers of the deferred commands (ROM $A902-$ABDE), decompiled (M4.2 spec 2.2). Each one is
   entered with X = its argument index and A = boot_clause[X] (cmd_handlers.h). */
#include "cmd_handlers.h"
#include "cmd_match.h"
#include "tn_rules.h"

#define CMD_VERSION_ENGINE_TEXT 0x9804   /* cmd_version_engine_text (converted in M4.1, fw_phonetic_texts) */
#define CMD_VERSION_LANG_TEXT   0xF75B   /* cmd_version_lang_text */

static uint8_t clause_at(const fw *f, uint8_t x) { return fw_rd8(f, (uint16_t)(BOOT_CLAUSE + x)); }

/* The one-digit setters: setting = cmd_hex_value(a) when it is below limit. The ROM's CMP #0 / BCC is dead,
   because CMP #0 always sets C. */
static void set_below(fw *f, uint16_t setting, uint8_t a, uint8_t limit)
{
    uint8_t v = cmd_hex_value(f, a);
    if (v < limit)
        fw_wr8(f, setting, v);
}

/* $A868: append the length-prefixed phonetic text at tn_rule_ptr to tn_phonetic. The ROM preserves A and Y and
   returns the X tn_append_phonetic leaves (cmd_run_done restores the handler's X at the end). */
static uint8_t cmd_put_phonetic(fw *f, uint8_t x)
{
    uint8_t y = 0;
    tn_append_phonetic(f, fw_ind_y(f, (uint8_t)TN_RULE_PTR, 0), &x, &y);
    return x;
}

void cmd_say_version(fw *f, uint8_t x, uint8_t a)
{
    if (a != '?')
        return;
    fw_wr16(f, TN_RULE_PTR, CMD_VERSION_ENGINE_TEXT);
    x = cmd_put_phonetic(f, x);
    fw_wr16(f, TN_RULE_PTR, CMD_VERSION_LANG_TEXT);
    cmd_put_phonetic(f, x);                        /* X as the first call left it, as in the ROM */
}

void cmd_set_phrase_pause(fw *f, uint8_t x, uint8_t a)
{
    (void)x;
    set_below(f, CMD_PHRASE_PAUSE, a, 0x10);
}

void cmd_set_word_pause(fw *f, uint8_t x, uint8_t a)
{
    (void)x;
    set_below(f, CMD_WORD_PAUSE, a, 10);
}

void cmd_set_mark_space(fw *f, uint8_t x, uint8_t a)
{
    (void)a;                                            /* reloaded after the re-anchoring */
    x = cmd_find_back(f, 'M', x);                       /* $75 = 'M' */
    uint8_t v = cmd_hex_value(f, clause_at(f, x));
    if (v >= 4)
        return;
    fw_wr8(f, FW_TMP_75, util_shl4(v));
    v = cmd_hex_value(f, clause_at(f, ++x));
    if (v >= 0x10)
        return;
    fw_wr8(f, CMD_MARK_SPACE, (uint8_t)(v + fw_rd8(f, FW_TMP_75)));
}

void cmd_set_spell(fw *f, uint8_t x, uint8_t a)
{
    (void)x;
    set_below(f, CMD_SPELL, a, 2);                      /* the ROM: BEQ stores 0, then CMP #2 / BCS */
}

void cmd_set_punct(fw *f, uint8_t x, uint8_t a)
{
    (void)x;
    set_below(f, CMD_PUNCT, a, 2);
}

void cmd_set_hyper(fw *f, uint8_t x, uint8_t a)
{
    (void)x;
    set_below(f, CMD_HYPER, a, 2);
}

void cmd_set_phonetic(fw *f, uint8_t x, uint8_t a)
{
    (void)x;
    set_below(f, CMD_PHONETIC, a, 2);
}

void cmd_set_formant(fw *f, uint8_t x, uint8_t a)
{
    (void)a;
    x = cmd_find_back(f, 'U', x);                       /* $75 = 'U' */
    uint8_t d = cmd_hex_value(f, clause_at(f, x));      /* the parameter 0-9 */
    if (d >= 10)
        return;
    uint8_t v = cmd_hex_value(f, clause_at(f, ++x));
    if (v >= 0x10)
        return;
    fw_wr8(f, FW_TMP_75, util_shl4(v));
    v = cmd_hex_value(f, clause_at(f, ++x));
    if (v >= 0x10)
        return;
    fw_wr8(f, FW_TMP_75, (uint8_t)(v + fw_rd8(f, FW_TMP_75)));
    uint8_t sign = clause_at(f, ++x);                   /* raw, not case-mapped */
    if (sign == '-')
        fw_wr8(f, (uint16_t)(CMD_FORMANT_OFS + d), (uint8_t)(0 - fw_rd8(f, FW_TMP_75)));
    else if (sign == '+')
        fw_wr8(f, (uint16_t)(CMD_FORMANT_OFS + d), fw_rd8(f, FW_TMP_75));
}

void cmd_ignore(fw *f, uint8_t x, uint8_t a)
{
    (void)f;
    (void)x;
    (void)a;
}
```

`src/firmware/cmd_run.c`: in `k_handlers`, replace

```c
    { 0, NULL },
```

with

```c
    { ROM_CMD_SAY_VERSION,      cmd_say_version },
    { ROM_CMD_SET_PHRASE_PAUSE, cmd_set_phrase_pause },
    { ROM_CMD_SET_FORMANT,      cmd_set_formant },
    { ROM_CMD_SET_WORD_PAUSE,   cmd_set_word_pause },
    { ROM_CMD_SET_MARK_SPACE,   cmd_set_mark_space },
    { ROM_CMD_SET_SPELL,        cmd_set_spell },
    { ROM_CMD_SET_PUNCT,        cmd_set_punct },
    { ROM_CMD_SET_HYPER,        cmd_set_hyper },
    { ROM_CMD_SET_PHONETIC,     cmd_set_phonetic },
    { ROM_CMD_IGNORE,           cmd_ignore },
    { 0, NULL },
```

**Faithfulness check.** Before committing, read each handler's dump side by side with its C, and confirm every accepted range, every `$75` write and its value, the 8-bit `INX` steps, and the raw (not case-mapped) compares: `'?'` in `cmd_say_version`, and the sign in `cmd_set_formant`. The dumps are:
- `rom_A902_cmd_say_version.txt` with `rom_A868_cmd_put_phonetic.txt` and `rom_B154_tn_append_phonetic.txt`;
- `rom_A91F_cmd_set_phrase_pause.txt`, `rom_A98A_cmd_set_word_pause.txt`, `rom_AA4F_cmd_set_spell.txt`, `rom_AAF5_cmd_set_punct.txt`, `rom_AB4D_cmd_set_hyper.txt`, `rom_AB9F_cmd_set_phonetic.txt`;
- `rom_A99A_cmd_set_mark_space.txt` and `rom_A92F_cmd_set_formant.txt`, with `rom_ABDF_cmd_find_back.txt` and `rom_C520_cmd_hex_value.txt`;
- `rom_ABDC_cmd_ignore.txt`.

Record the result in the report.

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test fw_cmd"`, then `cmd //c ".\build.bat test diff_lts"`, then `cmd //c ".\build.bat test golden_hybrid"`, then `cmd //c ".\build.bat test"`
Expected:
- `suite_fw_cmd` ok.
- `diff_lts`: 15 lines `<name> N calls checked`, each with N > 0, and 0 mismatches. The line `cmd_run_due          N calls checked` now covers the ten C handlers, reached by `version`, `pauses`, `mark_space`, `spell`, `punct_mode`, `hypermode`, `phonetic`, `formant_mod` and `cmd_misc` (`@G`, `@I^`, `@+`).
- `golden_hybrid`: `32 corpus files x 16 modes (all, then each routine alone)`, all identical, with the flush and unwind assertions holding.
- All 30 suites ok, with no warnings, and the firmware syntax check passing.

Then count the C handlers: `grep -cE '^\s*\{\s*ROM_CMD_[A-Z_]+,\s*cmd_[a-z_]+\s*\},' src/firmware/cmd_run.c` must print `10`.

- [ ] **Step 5: Commit**

```bash
git add src/firmware/cmd_set.c src/firmware/cmd_handlers.h src/firmware/cmd_run.c tests/test_fw_cmd.c
git commit -m "Command handlers in C: version, pauses, mark space, spell, punct, hyper, phonetic, formant, ignore (M4.2 2.2)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: The apply helpers, the `cmd_apply_speaker` thunk, and the handlers that use them (spec §2.2, §2.3)

**Files:**
- Create: `src/firmware/cmd_apply.h`, `src/firmware/cmd_apply.c`
- Modify: `src/firmware/cmd_set.c`, `src/firmware/cmd_handlers.h`, `src/firmware/cmd_run.c`, `src/engine/thunks.c`, `tests/test_fw_cmd.c`

**Interfaces:**
- Consumes:
  - Task 3: `CMD_SPEED_MAPS` (`$C87B`), `CMD_SPEED_LIMITS` (`$C88F`), `PAR_VOICE_A_COUNT` (`$D469`), `PAR_VOICE_B_COUNT` (`$E5E2`), `PAR_VOICE_A` (`$D46A`) and `PAR_VOICE_B` (`$E5E3`) in `fw_data.h`, and `ROM_PAR_SETUP_VOICE`, `ROM_CMD_SET_SPEED`, `ROM_CMD_SET_PROSODY`, `ROM_CMD_SET_PITCH`, `ROM_CMD_SET_SPEAKER`, `ROM_CMD_SET_VOICE` and `ROM_CMD_SET_SOURCE` in `cmd_addr.h`;
  - Task 4: `cmd_hex_value`, `cmd_hex_byte`, `cmd_upcase`, `cmd_find_back`, `util_shl4`, the scratch names `FW_TMP_34`, `FW_TMP_35`, `FW_TMP_75`, `FW_TMP_76` and `FW_TMP_1809` (`fw.h`), and the harness of `tests/test_fw_cmd.c` (`g_ram`, `g_f`, `g_calls`, `g_ncalls`, `MAX_CALLS`, `mock_rom_call`, `setup()`);
  - Task 5: `count()` and `put_clause()` in the tests; the `cmd_match_table` thunk's position in `thunks.c`;
  - Task 6: the `cmd_run_due` thunk, `THUNK` line and table row in `thunks.c` (this task's go before them, in address order), the `#include "firmware/cmd_run.h"` line there, and `k_handlers` with its `{ 0, NULL }` row;
  - Task 7: `cmd_set.c` with `clause_at`, and the last declaration in `cmd_handlers.h` (`cmd_ignore`).
- Produces:

```c
/* cmd_apply.h */
uint8_t cmd_apply_speaker(fw *f, uint8_t *x, uint8_t *y);
void cmd_select_voice_a(fw *f);
void cmd_select_voice_b(fw *f);
void cmd_apply_pitch(fw *f, uint8_t *x, uint8_t *y);
void cmd_apply_speed(fw *f);
void cmd_clear_formant(fw *f);
```

- Also produces `cmd_set_pitch`, `cmd_set_speed`, `cmd_set_prosody`, `cmd_set_source`, `cmd_set_speaker` and `cmd_set_voice` (`cmd_handlers.h`), with 6 more `k_handlers` rows (16 in all).
- The replacement `"cmd_apply_speaker"` at `0xA78C` with outputs `FW_OUT_A | FW_OUT_X | FW_OUT_Y` (16 routines).
- In `tests/test_fw_cmd.c`: the mock's answer to `ROM_PAR_SETUP_VOICE` (X = 9, Y = $0B, as the ROM leaves them) and `check_call(i, addr, a, x, y)`, which Task 9 uses.

**ROM facts checked while writing this task** (`rom_A78C_cmd_apply_speaker.txt`, `rom_A7FE`/`rom_A814` (shared tail `$A828`), `rom_A832_cmd_apply_pitch.txt`, `rom_A84A_cmd_apply_speed.txt`, `rom_A7DF_cmd_clear_formant.txt`, `rom_B3A4_par_setup_voice.txt` with `rom_BD07`, `rom_BD14`, `rom_B41D`, `rom_B445`, `rom_B930`, `rom_A61C_boot_reset.txt`, `rom_A600_bank_load_ramcode.txt`, `ramcode_0241_bank_scan.txt`, `rom_C612_boot_queue_string.txt`, and the six handler dumps):
- **`par_setup_voice` reads none of A, X or Y.**
  - It starts with `LDA par_count`.
  - Its `par_next_row` calls touch only A.
  - X is first written at `$B3DA LDX #2`, Y at `frm_build_rest_frame`'s `LDY #$14`.
- **It returns X = 9 and Y = $0B.** `frm_build_rest_frame`'s loop ends at `CPX #8` with X = 9, and `frm_clamp_mark_space` does `LDY #$0A / INY`. `cmd_apply_pitch` restores only A (`PHA … PLA`), so X and Y come back changed from `cmd_apply_pitch` and from `cmd_apply_speaker`.
  - The survey lists `cmd_apply_pitch` as "A preserved" and says nothing about X and Y. The spec says the `cmd_apply_speaker` hook needs "none beyond what boot_reset reads".
  - But `boot_reset`'s second call (`$A675`) is followed by `bank_load_ramcode`, which stores X in `$76` and Y in `$78`. A goes through `bank_load_ramcode` (`PHA/PLA`), `bank_scan` and, after the first call (`$A651`), `boot_selftest_sound` → `boot_queue_string`, which preserve it.
  - So the thunk declares and sets **A, X and Y**. The C passes X and Y to `fw_rom_call` and returns what the ROM leaves, so the values are exact by construction.
- `cmd_apply_speaker` returns A = the `cmd_speaker` it read at `$A78C`. `cmd_select_voice_a/b` push and pull A, and the apply routines preserve it. Any non-zero speaker selects voice B, and `cmd_select_voice_b` then writes `cmd_speaker` = 1.
- `cmd_select_voice_a/b` write `cmd_speaker`, `par_count` = the count byte (`$D469`/`$E5E2`, both 86), `par_table_ptr` = the address after it (`$D46A`/`$E5E3`), and `par_spare` (2 bytes) = 0.
- `cmd_apply_pitch` computes `cmd_source_prosody` = `(cmd_source << 4) | cmd_prosody`, an **ORA**, and `pros_pitch_centre` = `cmd_pitch << 4`. A at the `JSR par_setup_voice` is `pros_pitch_centre`.
- `cmd_apply_speed`:
  - `Y = cmd_speed - 1` (8-bit) indexes `cmd_speed_limits` directly.
  - The maps use index `(Y << 1) & $FF`, at `$C87B,Y` and `$C87C,Y`.
  - The ROM bytes are limits 7,7,7,7,4,2,0,0,0 and maps `$C898 + 32·i`, the 10th repeating the 9th.
- `cmd_clear_formant` zeroes `$2014–$201E`: Y counts down from 11 with `DEY` before the `STA`, so the 10 offsets and `par_ofs` are cleared.
- Handlers:
  - `@F`: `'+'` → `cmd_pitch_step` = $10 and `'-'` → 0, both raw compares with no apply. A digit < $10 sets `cmd_pitch`, `cmd_pitch_step` = 8, then applies the pitch.
  - `@W`: 1 ≤ v < 10, then `cmd_apply_speed`.
  - `@$`: v < 8, then `cmd_apply_pitch`.
  - `@K`: v = 0 or 1, through `BEQ` and `CMP #1 / BNE`, then `cmd_apply_speaker`.
  - `@V`: `cmd_clear_formant` first, always. Then the **`BNE` at `$AA7A` on `cmd_hex_value`'s Z**: Z is set exactly when the result is 0, which happens only for `'0'`. On Z: `cmd_voice` = 0, with no apply. Values 1–6 set the presets; any other value does nothing more.
  - `@R` re-anchors with `cmd_find_back('R')`. Its digit path compares the **raw** byte: `'0'` ≤ c < `'8'` → `cmd_prosody` = c − `'0'`, then `cmd_apply_pitch`. A byte ≥ `'8'` takes the letter path: `cmd_upcase`, then `'A'`–`'Z'` → `$76` = letter − `'A'`, `$34`/`$35` = the next two bytes upcased, `cmd_hex_byte`, and `pros_params[$76]` = the result. The letter path has no apply.
- Every handler enters with Y = `cmd_next` − 1 (cmd_run_due's `LDY`, before cmd_run_next's `INC`), and nothing before the apply call changes it: `cmd_hex_value` and `cmd_upcase` keep Y, and `cmd_clear_formant` restores it. The C passes that Y and the handler's X on to `par_setup_voice`, as the ROM does, though it reads neither.

- [ ] **Step 1: Write the failing tests**

In `tests/test_fw_cmd.c`:
- Replace

```c
#include "firmware/cmd_addr.h"
#include "firmware/cmd_handlers.h"
```

with

```c
#include "firmware/cmd_addr.h"
#include "firmware/cmd_apply.h"
#include "firmware/cmd_handlers.h"
```

- In `mock_rom_call`, replace

```c
        return FW_CALL_RETURNED;   /* X back as it was (cmd_run_done) */
    }
```

with

```c
        return FW_CALL_RETURNED;   /* X back as it was (cmd_run_done) */
    }
    if (addr == ROM_PAR_SETUP_VOICE) {
        /* par_setup_voice reads none of A, X, Y and leaves X = 9 and Y = $0B, as the ROM's does
           (frm_build_rest_frame's loop ends at X = 9, frm_clamp_mark_space's at Y = $0B) */
        r->x = 9;
        r->y = 0x0B;
        return FW_CALL_RETURNED;
    }
```

- Add these tests after `test_ignore`:

```c
/* Call i went to addr with A, X, Y and the strict budget. */
static void check_call(unsigned i, uint16_t addr, uint8_t a, uint8_t x, uint8_t y)
{
    CHECK(i < g_ncalls && i < MAX_CALLS);
    if (i >= g_ncalls || i >= MAX_CALLS)
        return;
    CHECK_EQ(g_calls[i].addr, addr);
    CHECK_EQ(g_calls[i].a, a);
    CHECK_EQ(g_calls[i].x, x);
    CHECK_EQ(g_calls[i].y, y);
    CHECK_EQ(g_calls[i].budget, FW_ROM_CALL_BUDGET);
}

static void test_apply_pitch(void)
{
    setup();
    g_ram[CMD_SOURCE] = 7;
    g_ram[CMD_PROSODY] = 2;
    g_ram[CMD_PITCH] = 9;
    uint8_t x = 0x31, y = 0x42;
    cmd_apply_pitch(&g_f, &x, &y);
    CHECK_EQ(g_ram[CMD_SOURCE_PROSODY], 0x72);
    CHECK_EQ(g_ram[PROS_PITCH_CENTRE], 0x90);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_PAR_SETUP_VOICE, 0x90, 0x31, 0x42);   /* A = pros_pitch_centre, as at $A845 */
    CHECK_EQ(x, 9);                             /* as par_setup_voice leaves them */
    CHECK_EQ(y, 0x0B);
    CHECK(!g_f.fault);
}

static void test_apply_speed(void)
{
    static const uint8_t k_limits[9] = { 7, 7, 7, 7, 4, 2, 0, 0, 0 };   /* cmd_speed_limits */
    for (uint8_t s = 1; s <= 9; s++) {
        setup();
        g_ram[CMD_SPEED] = s;
        cmd_apply_speed(&g_f);
        CHECK_EQ(g_ram[CMD_SPEED_LIMIT], k_limits[s - 1]);
        CHECK_EQ(fw_rd16(&g_f, CMD_SPEED_MAP), 0xC898 + 32 * (s - 1));    /* the 32-byte maps from $C898 */
        CHECK_EQ(g_ncalls, 0);
        CHECK(!g_f.fault);
    }
}

static void test_apply_speaker(void)
{
    setup();
    g_ram[CMD_SPEED] = 3;
    fw_wr16(&g_f, PAR_SPARE, 0xBEEF);
    uint8_t x = 1, y = 2;
    CHECK_EQ(cmd_apply_speaker(&g_f, &x, &y), 0);
    CHECK_EQ(g_ram[CMD_SPEAKER], 0);
    CHECK_EQ(g_ram[PAR_COUNT], 86);
    CHECK_EQ(fw_rd16(&g_f, PAR_TABLE_PTR), 0xD46A);    /* par_voice_a */
    CHECK_EQ(fw_rd16(&g_f, PAR_SPARE), 0);
    CHECK_EQ(g_ram[CMD_SPEED_LIMIT], 7);
    CHECK_EQ(count(ROM_PAR_SETUP_VOICE), 1);
    check_call(0, ROM_PAR_SETUP_VOICE, 0, 1, 2);
    CHECK_EQ(x, 9);
    CHECK_EQ(y, 0x0B);

    setup();
    g_ram[CMD_SPEED] = 3;
    g_ram[CMD_SPEAKER] = 5;                     /* any non-zero speaker selects voice B */
    CHECK_EQ(cmd_apply_speaker(&g_f, &x, &y), 5);    /* A = the cmd_speaker read at $A78C */
    CHECK_EQ(g_ram[CMD_SPEAKER], 1);
    CHECK_EQ(g_ram[PAR_COUNT], 86);
    CHECK_EQ(fw_rd16(&g_f, PAR_TABLE_PTR), 0xE5E3);    /* par_voice_b */
    CHECK(!g_f.fault);
}

static void test_clear_formant(void)
{
    setup();
    memset(g_ram + CMD_FORMANT_OFS, 0x55, 12);
    cmd_clear_formant(&g_f);
    for (int i = 0; i <= 10; i++)
        CHECK_EQ(g_ram[CMD_FORMANT_OFS + i], 0);    /* $2014-$201E: the ten offsets and par_ofs */
    CHECK_EQ(g_ram[PAR_OFS_WEIGHT], 0x55);
}

static void test_set_pitch(void)
{
    setup();
    g_ram[CMD_NEXT] = 3;                        /* the handler's Y is cmd_next - 1 */
    g_ram[CMD_PITCH] = 0x0C;
    cmd_set_pitch(&g_f, 7, '+');
    CHECK_EQ(g_ram[CMD_PITCH_STEP], 0x10);
    cmd_set_pitch(&g_f, 7, '-');
    CHECK_EQ(g_ram[CMD_PITCH_STEP], 0);
    cmd_set_pitch(&g_f, 7, 'G');
    CHECK_EQ(g_ram[CMD_PITCH], 0x0C);
    CHECK_EQ(g_ncalls, 0);                      /* + - and a rejected digit do not apply */
    cmd_set_pitch(&g_f, 7, 'b');
    CHECK_EQ(g_ram[CMD_PITCH], 0x0B);
    CHECK_EQ(g_ram[CMD_PITCH_STEP], 8);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_PAR_SETUP_VOICE, 0xB0, 7, 2);
    CHECK(!g_f.fault);
}

static void test_set_speed(void)
{
    setup();
    g_ram[CMD_SPEED] = 3;
    g_ram[CMD_SPEED_LIMIT] = 0xEE;
    cmd_set_speed(&g_f, 3, '0');                /* 1-9 only */
    cmd_set_speed(&g_f, 3, 'A');
    CHECK_EQ(g_ram[CMD_SPEED], 3);
    CHECK_EQ(g_ram[CMD_SPEED_LIMIT], 0xEE);
    cmd_set_speed(&g_f, 3, '9');
    CHECK_EQ(g_ram[CMD_SPEED], 9);
    CHECK_EQ(g_ram[CMD_SPEED_LIMIT], 0);
    CHECK_EQ(fw_rd16(&g_f, CMD_SPEED_MAP), 0xC998);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

static void test_set_source(void)
{
    setup();
    g_ram[CMD_NEXT] = 1;
    g_ram[CMD_PROSODY] = 3;
    cmd_set_source(&g_f, 4, '8');               /* 0-7 only */
    CHECK_EQ(g_ram[CMD_SOURCE], 0);
    CHECK_EQ(g_ncalls, 0);
    cmd_set_source(&g_f, 4, '7');
    CHECK_EQ(g_ram[CMD_SOURCE], 7);
    CHECK_EQ(g_ram[CMD_SOURCE_PROSODY], 0x73);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_PAR_SETUP_VOICE, 0, 4, 0);
    CHECK(!g_f.fault);
}

static void test_set_speaker(void)
{
    setup();
    g_ram[CMD_SPEED] = 3;
    g_ram[CMD_NEXT] = 1;
    cmd_set_speaker(&g_f, 4, '2');              /* 0 or 1 only */
    CHECK_EQ(g_ram[CMD_SPEAKER], 0);
    CHECK_EQ(g_ncalls, 0);
    cmd_set_speaker(&g_f, 4, '1');
    CHECK_EQ(g_ram[CMD_SPEAKER], 1);
    CHECK_EQ(fw_rd16(&g_f, PAR_TABLE_PTR), 0xE5E3);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_PAR_SETUP_VOICE, 0, 4, 0);
    CHECK(!g_f.fault);
}

static void test_set_voice(void)
{
    static const struct { uint8_t voicing, mark_space, pitch, speaker, source; } k_want[6] = {
        { 8, 0x14, 8, 0, 0 },    { 8, 0x28, 6, 0, 0 },    { 7, 0x0A, 9, 0, 0 },
        { 7, 0x14, 0x0B, 1, 7 }, { 6, 0x0A, 0x0A, 1, 7 }, { 8, 0x14, 0x0C, 1, 7 },
    };
    for (uint8_t v = 1; v <= 6; v++) {
        setup();
        g_ram[CMD_SPEED] = 3;
        g_ram[CMD_FORMANT_OFS + 4] = 0x33;
        cmd_set_voice(&g_f, 2, (uint8_t)('0' + v));
        CHECK_EQ(g_ram[CMD_VOICE], v);
        CHECK_EQ(g_ram[CMD_VOICING], k_want[v - 1].voicing);
        CHECK_EQ(g_ram[CMD_MARK_SPACE], k_want[v - 1].mark_space);
        CHECK_EQ(g_ram[CMD_PITCH], k_want[v - 1].pitch);
        CHECK_EQ(g_ram[CMD_SPEAKER], k_want[v - 1].speaker);
        CHECK_EQ(g_ram[CMD_SOURCE], k_want[v - 1].source);
        CHECK_EQ(g_ram[CMD_FORMANT_OFS + 4], 0);
        CHECK_EQ(fw_rd16(&g_f, PAR_TABLE_PTR), v <= 3 ? 0xD46A : 0xE5E3);
        CHECK_EQ(g_ncalls, 1);
        check_call(0, ROM_PAR_SETUP_VOICE, (uint8_t)(k_want[v - 1].pitch << 4), 2, 0xFF);
        CHECK(!g_f.fault);
    }

    setup();                                    /* @V0: the voice off, no apply ($AA7A BNE on Z) */
    g_ram[CMD_VOICE] = 1;
    g_ram[CMD_PITCH] = 0x0C;
    g_ram[CMD_FORMANT_OFS] = 0x33;
    cmd_set_voice(&g_f, 2, '0');
    CHECK_EQ(g_ram[CMD_VOICE], 0);
    CHECK_EQ(g_ram[CMD_FORMANT_OFS], 0);
    CHECK_EQ(g_ram[CMD_PITCH], 0x0C);
    CHECK_EQ(g_ncalls, 0);

    setup();                                    /* 7: no preset; only the formant offsets are cleared */
    g_ram[CMD_VOICE] = 1;
    g_ram[CMD_FORMANT_OFS] = 0x33;
    cmd_set_voice(&g_f, 2, '7');
    CHECK_EQ(g_ram[CMD_VOICE], 1);
    CHECK_EQ(g_ram[CMD_FORMANT_OFS], 0);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

static void test_set_prosody(void)
{
    setup();
    put_clause(" @R3");                         /* R#: X = 3 */
    g_ram[CMD_NEXT] = 1;
    cmd_set_prosody(&g_f, 3, '3');
    CHECK_EQ(g_ram[CMD_PROSODY], 3);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_PAR_SETUP_VOICE, 0, 3, 0);   /* X = cmd_find_back's */

    setup();
    put_clause(" @RR12");                       /* R***: re-anchors on the second R, so the digit path */
    cmd_set_prosody(&g_f, 5, '2');
    CHECK_EQ(g_ram[CMD_PROSODY], 1);
    CHECK_EQ(g_ncalls, 1);

    setup();
    put_clause(" @RA80");                       /* R***: letter A, hex $80 */
    cmd_set_prosody(&g_f, 5, '0');
    CHECK_EQ(g_ram[PROS_PARAMS + 0], 0x80);
    CHECK_EQ(g_ram[FW_TMP_76], 0);
    CHECK_EQ(g_ram[FW_TMP_34], '8');
    CHECK_EQ(g_ram[FW_TMP_35], '0');
    CHECK_EQ(g_ram[FW_TMP_1809], 0x80);         /* cmd_hex_byte's scratch: the high digit times 16 */
    CHECK_EQ(g_ncalls, 0);                      /* the letter path does not apply */

    setup();
    put_clause(" @rz1f");                       /* lower case through cmd_upcase: Z (25), $1F */
    cmd_set_prosody(&g_f, 5, 'f');
    CHECK_EQ(g_ram[PROS_PARAMS + 25], 0x1F);
    CHECK_EQ(g_ram[FW_TMP_35], 'F');

    setup();
    put_clause(" @R9");                         /* 8 and 9 take the letter path and fail it */
    g_ram[FW_TMP_76] = 0x99;
    cmd_set_prosody(&g_f, 3, '9');
    CHECK_EQ(g_ram[CMD_PROSODY], 0);
    CHECK_EQ(g_ram[FW_TMP_76], 0x99);
    CHECK_EQ(g_ram[FW_TMP_75], 'R');
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}
```

- In `suite_fw_cmd`, after `test_ignore();`, add:

```c
    test_apply_pitch();
    test_apply_speed();
    test_apply_speaker();
    test_clear_formant();
    test_set_pitch();
    test_set_speed();
    test_set_source();
    test_set_speaker();
    test_set_voice();
    test_set_prosody();
```

(In `test_set_voice`, `CMD_NEXT` is 0 after `setup()`, so the handler's Y is $FF.)

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw_cmd"`
Expected: a compile error, because `firmware/cmd_apply.h` is missing (C1083).

- [ ] **Step 3: Implement**

`src/firmware/cmd_apply.h`:

```c
#ifndef FW_CMD_APPLY_H
#define FW_CMD_APPLY_H
/* The settings helpers of the command handlers (ROM $A78C-$A867), decompiled (M4.2 spec 2.2). */
#include "fw.h"

/* $A78C: the voice tables of cmd_speaker (0: voice A, else voice B), then cmd_apply_pitch and cmd_apply_speed.
   *x and *y go on to par_setup_voice and come back as it leaves them (X = 9, Y = $0B). Returns A = the
   cmd_speaker it read; every callee preserves A. */
uint8_t cmd_apply_speaker(fw *f, uint8_t *x, uint8_t *y);
/* $A7FE / $A814, with their shared tail $A828: cmd_speaker = 0 / 1, par_count = the voice table's count byte,
   par_table_ptr = the table after it (par_voice_a / par_voice_b), par_spare = 0. */
void cmd_select_voice_a(fw *f);
void cmd_select_voice_b(fw *f);
/* $A832: cmd_source_prosody = cmd_source * 16 | cmd_prosody, pros_pitch_centre = cmd_pitch * 16, then the ROM
   par_setup_voice (M4.4) rebuilds the voice set-up, with A = pros_pitch_centre, X = *x and Y = *y (it reads
   none of them). *x and *y return what it leaves; cmd_apply_pitch restores only A. */
void cmd_apply_pitch(fw *f, uint8_t *x, uint8_t *y);
/* $A84A: cmd_speed_limit = cmd_speed_limits[cmd_speed - 1], cmd_speed_map = cmd_speed_maps[cmd_speed - 1]. */
void cmd_apply_speed(fw *f);
/* $A7DF: $2014-$201E (cmd_formant_ofs and par_ofs) = 0. */
void cmd_clear_formant(fw *f);

#endif
```

`src/firmware/cmd_apply.c`:

```c
#include "cmd_apply.h"
#include "cmd_addr.h"
#include "cmd_match.h"
#include "data/fw_data.h"

/* $A7FE / $A814: the count byte is data (converted); the table address is an immediate (LDA #$6A / #$D4,
   LDA #$E3 / #$E5), so the table itself (M4.4) is never read here */
static void select_voice(fw *f, uint8_t speaker, uint16_t count_addr, uint16_t table)
{
    fw_wr8(f, CMD_SPEAKER, speaker);
    fw_wr8(f, PAR_COUNT, fw_rom8(f, count_addr));
    fw_wr16(f, PAR_TABLE_PTR, table);
    fw_wr16(f, PAR_SPARE, 0);                                   /* the shared tail $A828 */
}

void cmd_select_voice_a(fw *f) { select_voice(f, 0, PAR_VOICE_A_COUNT, PAR_VOICE_A); }
void cmd_select_voice_b(fw *f) { select_voice(f, 1, PAR_VOICE_B_COUNT, PAR_VOICE_B); }

void cmd_apply_pitch(fw *f, uint8_t *x, uint8_t *y)
{
    fw_wr8(f, CMD_SOURCE_PROSODY, (uint8_t)(util_shl4(fw_rd8(f, CMD_SOURCE)) | fw_rd8(f, CMD_PROSODY)));
    uint8_t a = util_shl4(fw_rd8(f, CMD_PITCH));
    fw_wr8(f, PROS_PITCH_CENTRE, a);
    fw_regs r = { a, *x, *y, 0 };
    fw_rom_call(f, ROM_PAR_SETUP_VOICE, &r, FW_ROM_CALL_BUDGET);
    *x = r.x;
    *y = r.y;
}

void cmd_apply_speed(fw *f)
{
    uint8_t i = (uint8_t)(fw_rd8(f, CMD_SPEED) - 1);                     /* DEC A / TAY */
    fw_wr8(f, CMD_SPEED_LIMIT, fw_rom8(f, (uint16_t)(CMD_SPEED_LIMITS + i)));
    i = (uint8_t)(i << 1);                                              /* ASL A / TAY */
    fw_wr8(f, CMD_SPEED_MAP, fw_rom8(f, (uint16_t)(CMD_SPEED_MAPS + i)));
    fw_wr8(f, CMD_SPEED_MAP + 1, fw_rom8(f, (uint16_t)(CMD_SPEED_MAPS + 1 + i)));
}

uint8_t cmd_apply_speaker(fw *f, uint8_t *x, uint8_t *y)
{
    uint8_t a = fw_rd8(f, CMD_SPEAKER);
    if (a == 0)
        cmd_select_voice_a(f);
    else
        cmd_select_voice_b(f);
    cmd_apply_pitch(f, x, y);
    cmd_apply_speed(f);
    return a;
}

void cmd_clear_formant(fw *f)
{
    for (uint8_t y = 11; y-- != 0;)                                     /* DEY / STA $2014,Y / BNE */
        fw_wr8(f, (uint16_t)(CMD_FORMANT_OFS + y), 0);
}
```

`src/firmware/cmd_handlers.h`: replace

```c
void cmd_ignore(fw *f, uint8_t x, uint8_t a);             /* $ABDC @G, @I^, @+: nothing */

#endif
```

with

```c
void cmd_ignore(fw *f, uint8_t x, uint8_t a);             /* $ABDC @G, @I^, @+: nothing */

/* The setters with an apply helper (cmd_set.c) */
void cmd_set_pitch(fw *f, uint8_t x, uint8_t a);          /* $AA24 @F: + - or 0-$F */
void cmd_set_speed(fw *f, uint8_t x, uint8_t a);          /* $A9C9 @W: 1-9 */
void cmd_set_prosody(fw *f, uint8_t x, uint8_t a);        /* $A9DC @R: 0-7, or a letter and two digits */
void cmd_set_source(fw *f, uint8_t x, uint8_t a);         /* $AB5D @$: 0-7 */
void cmd_set_speaker(fw *f, uint8_t x, uint8_t a);        /* $AA61 @K: 0-1 */
void cmd_set_voice(fw *f, uint8_t x, uint8_t a);          /* $AA74 @V: 0 (off) or presets 1-6 */

#endif
```

`src/firmware/cmd_set.c`:
- Replace

```c
#include "cmd_handlers.h"
```

with

```c
#include "cmd_handlers.h"
#include "cmd_apply.h"
```

- Replace

```c
static uint8_t clause_at(const fw *f, uint8_t x) { return fw_rd8(f, (uint16_t)(BOOT_CLAUSE + x)); }
```

with

```c
static uint8_t clause_at(const fw *f, uint8_t x) { return fw_rd8(f, (uint16_t)(BOOT_CLAUSE + x)); }

/* The Y a handler is entered with: cmd_run_due's LDY cmd_next ($A880), before cmd_run_next's INC. Nothing a
   handler calls before its apply helper changes Y, so it reaches par_setup_voice as in the ROM (which does not
   read it). */
static uint8_t run_y(const fw *f) { return (uint8_t)(fw_rd8(f, CMD_NEXT) - 1); }
```

- Append at the end of the file (after `cmd_ignore`):

```c
void cmd_set_pitch(fw *f, uint8_t x, uint8_t a)
{
    if (a == '+') {
        fw_wr8(f, CMD_PITCH_STEP, 0x10);
        return;
    }
    if (a == '-') {
        fw_wr8(f, CMD_PITCH_STEP, 0);
        return;
    }
    uint8_t v = cmd_hex_value(f, a);
    if (v >= 0x10)
        return;
    fw_wr8(f, CMD_PITCH, v);
    fw_wr8(f, CMD_PITCH_STEP, 8);
    uint8_t y = run_y(f);
    cmd_apply_pitch(f, &x, &y);
}

void cmd_set_speed(fw *f, uint8_t x, uint8_t a)
{
    (void)x;
    uint8_t v = cmd_hex_value(f, a);
    if (v < 1 || v >= 10)
        return;
    fw_wr8(f, CMD_SPEED, v);
    cmd_apply_speed(f);
}

void cmd_set_prosody(fw *f, uint8_t x, uint8_t a)
{
    x = cmd_find_back(f, 'R', x);                       /* $75 = 'R' */
    a = clause_at(f, x);                                /* raw: the digit test is not case-mapped */
    if (a < '0')
        return;
    if (a < '8') {                                      /* a digit 0-7: the prosody */
        fw_wr8(f, CMD_PROSODY, (uint8_t)(a - '0'));
        uint8_t y = run_y(f);
        cmd_apply_pitch(f, &x, &y);
        return;
    }
    a = cmd_upcase(f, a);                               /* $A9F7: a letter, then two hex digits */
    if (a < 'A' || a > 'Z')
        return;
    fw_wr8(f, FW_TMP_76, (uint8_t)(a - 'A'));
    fw_wr8(f, FW_TMP_34, cmd_upcase(f, clause_at(f, ++x)));
    fw_wr8(f, FW_TMP_35, cmd_upcase(f, clause_at(f, ++x)));
    a = cmd_hex_byte(f);
    fw_wr8(f, (uint16_t)(PROS_PARAMS + fw_rd8(f, FW_TMP_76)), a);
}

void cmd_set_source(fw *f, uint8_t x, uint8_t a)
{
    uint8_t v = cmd_hex_value(f, a);
    if (v >= 8)
        return;
    fw_wr8(f, CMD_SOURCE, v);
    uint8_t y = run_y(f);
    cmd_apply_pitch(f, &x, &y);
}

void cmd_set_speaker(fw *f, uint8_t x, uint8_t a)
{
    uint8_t v = cmd_hex_value(f, a);
    if (v > 1)
        return;
    fw_wr8(f, CMD_SPEAKER, v);
    uint8_t y = run_y(f);
    cmd_apply_speaker(f, &x, &y);
}

void cmd_set_voice(fw *f, uint8_t x, uint8_t a)
{
    /* the presets 1-6: 1-3 with speaker 0 and source 0, 4-6 with speaker 1 and source 7 */
    static const struct { uint8_t voicing, mark_space, pitch; } k_presets[6] = {
        { 8, 0x14, 8 },    { 8, 0x28, 6 },    { 7, 0x0A, 9 },
        { 7, 0x14, 0x0B }, { 6, 0x0A, 0x0A }, { 8, 0x14, 0x0C },
    };
    cmd_clear_formant(f);
    uint8_t v = cmd_hex_value(f, a);
    if (v == 0) {                                       /* $AA7A: BNE on cmd_hex_value's Z; @V0 turns the voice off */
        fw_wr8(f, CMD_VOICE, 0);
        return;
    }
    if (v > 6)
        return;
    fw_wr8(f, CMD_VOICING, k_presets[v - 1].voicing);
    fw_wr8(f, CMD_MARK_SPACE, k_presets[v - 1].mark_space);
    fw_wr8(f, CMD_PITCH, k_presets[v - 1].pitch);
    fw_wr8(f, CMD_SPEAKER, (uint8_t)(v >= 4 ? 1 : 0));
    fw_wr8(f, CMD_SOURCE, (uint8_t)(v >= 4 ? 7 : 0));
    fw_wr8(f, CMD_VOICE, v);
    uint8_t y = run_y(f);
    cmd_apply_speaker(f, &x, &y);
}
```

`src/firmware/cmd_run.c`: in `k_handlers`, replace

```c
    { 0, NULL },
```

with

```c
    { ROM_CMD_SET_SPEED,        cmd_set_speed },
    { ROM_CMD_SET_PROSODY,      cmd_set_prosody },
    { ROM_CMD_SET_PITCH,        cmd_set_pitch },
    { ROM_CMD_SET_SPEAKER,      cmd_set_speaker },
    { ROM_CMD_SET_VOICE,        cmd_set_voice },
    { ROM_CMD_SET_SOURCE,       cmd_set_source },
    { 0, NULL },
```

`src/engine/thunks.c` (the rows stay in address order, so this task's go before Task 6's):
- Replace

```c
#include "firmware/cmd_run.h"
```

with

```c
#include "firmware/cmd_run.h"
#include "firmware/cmd_apply.h"
```

- Replace

```c
/* cmd_run_due $A880: X in. X out unchanged (every handler ends at cmd_run_done's PLA/TAX). Y out = cmd_next,
```

with

```c
/* cmd_apply_speaker $A78C: X, Y in (handed to par_setup_voice, which does not read them). A = the cmd_speaker it
   read, X and Y as par_setup_voice leaves them, out. boot_reset reads all three: after its second call ($A675),
   bank_load_ramcode saves X in $76 and Y in $78; A is preserved into bank_scan and, after the first call
   ($A651), through boot_selftest_sound. */
static void cmd_apply_speaker_regs(fw *f, cpu740 *c)
{
    uint8_t x = c->x, y = c->y;
    c->a = cmd_apply_speaker(f, &x, &y);
    c->x = x;
    c->y = y;
}

/* cmd_run_due $A880: X in. X out unchanged (every handler ends at cmd_run_done's PLA/TAX). Y out = cmd_next,
```

- Replace

```c
THUNK(cmd_run_due)
```

with

```c
THUNK(cmd_apply_speaker)
THUNK(cmd_run_due)
```

- Replace

```c
    { "cmd_run_due",        0xA880, FW_OUT_X | FW_OUT_Y, t_cmd_run_due },
```

with

```c
    { "cmd_apply_speaker",  0xA78C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_apply_speaker },
    { "cmd_run_due",        0xA880, FW_OUT_X | FW_OUT_Y, t_cmd_run_due },
```

**Faithfulness check.** Before committing, read side by side with the C, and confirm every write, range and branch:
- `rom_A78C_cmd_apply_speaker.txt`, `rom_A7FE_cmd_select_voice_a.txt` and `rom_A814_cmd_select_voice_b.txt` (the immediates `$D46A`/`$E5E3` = `PAR_VOICE_A`/`PAR_VOICE_B`, and the tail `$A828`), `rom_A832_cmd_apply_pitch.txt` (the ORA, and A at the `JSR`), `rom_A84A_cmd_apply_speed.txt` (the two index computations) and `rom_A7DF_cmd_clear_formant.txt` (11 bytes);
- `rom_AA24_cmd_set_pitch.txt`, `rom_A9C9_cmd_set_speed.txt`, `rom_A9DC_cmd_set_prosody.txt` (the raw digit test, the letter path, `$76`/`$34`/`$35`), `rom_AB5D_cmd_set_source.txt`, `rom_AA61_cmd_set_speaker.txt` and `rom_AA74_cmd_set_voice.txt` (the `BNE` at `$AA7A` and every preset), with `rom_B450_cmd_hex_byte.txt` and `rom_C50D_cmd_upcase.txt`.

Then:
- Re-read `rom_B3A4_par_setup_voice.txt` and its callees `rom_BD07_par_next_row.txt`, `rom_BD14_par_next_param.txt`, `rom_B41D_frm_build_rest_frame.txt`, `rom_B445_frm_copy_rest_frame.txt` and `rom_B930_frm_clamp_mark_space.txt`. Confirm that none of them reads A, X or Y before writing it, and that the routine returns X = 9 and Y = $0B.
- Re-read `rom_A61C_boot_reset.txt` (`$A651`, `$A675`), `rom_A600_bank_load_ramcode.txt` and `rom_C612_boot_queue_string.txt` for the thunk's outputs.

Record the result in the report.

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test fw_cmd"`, then `cmd //c ".\build.bat test diff_lts"`, then `cmd //c ".\build.bat test golden_hybrid"`, then `cmd //c ".\build.bat test"`
Expected:
- `suite_fw_cmd` ok.
- `diff_lts`: 16 lines `<name> N calls checked`, each with N > 0, and 0 mismatches, including `cmd_apply_speaker    N calls checked`. Its calls come from the boot run (`$A651`, `$A675`), the `bank_select` re-entry, and the ROM `cmd_set_speaker`/`cmd_set_voice` (`voice_b`, `presets`). `cmd_run_due` now also covers the six new C handlers, including `@V0` and `@R` with a letter (`cmd_args`).
- `golden_hybrid`: `32 corpus files x 17 modes (all, then each routine alone)`, all identical, with the flush and unwind assertions holding.
- All 30 suites ok, with no warnings, and the firmware syntax check passing.

Then count the C handlers: `grep -cE '^\s*\{\s*ROM_CMD_[A-Z_]+,\s*cmd_[a-z_]+\s*\},' src/firmware/cmd_run.c` must print `16`.

If `diff_lts` reports an X or Y mismatch for `cmd_apply_speaker`, check against `rom_B3A4` which values `par_setup_voice` leaves. Never drop an output to make it pass.

- [ ] **Step 5: Commit**

```bash
git add src/firmware/cmd_apply.h src/firmware/cmd_apply.c src/firmware/cmd_set.c src/firmware/cmd_handlers.h src/firmware/cmd_run.c src/engine/thunks.c tests/test_fw_cmd.c
git commit -m "Apply helpers in C (speaker, voices, pitch via ROM par_setup_voice, speed, formant clear), hooked for boot_reset; handlers pitch, speed, prosody, source, speaker, voice (M4.2 2.2)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Event producers and replies in C (spec §2.2 "Event producers", "Replies"; §2.4; §2.5)

**Files:**
- Create: `src/firmware/cmd_event.c`, `src/firmware/cmd_reply.c`
- Modify: `src/firmware/cmd_handlers.h`, `src/firmware/cmd_run.c`, `tests/test_fw_cmd.c`

**Interfaces:**
- Consumes:
  - `cmd_hex_value` (Task 4, `cmd_match.h`) and `FW_TMP_75` (Task 4, `fw.h`);
  - the `cmd_handler` typedef and the `k_handlers` table with its `{ 0, NULL }` row (Task 6, `cmd_handlers.h` / `cmd_run.c`), and `cmd_run_due` (Task 6, `cmd_run.h`; used by the tests only);
  - the last declaration of `cmd_handlers.h`, `cmd_set_voice` (Task 8);
  - `ROM_CMD_SOUND`, `ROM_CMD_SET_VOLUME`, `ROM_CMD_SET_VOICING`, `ROM_CMD_MARK_INDEX`, `ROM_CMD_ECHO`, `ROM_CMD_QUERY`, `ROM_CMD_LIST_LANGUAGES`, `ROM_CMD_EV_INDEX`, `ROM_CMD_EV_SOUND_START`, `ROM_CMD_EV_SOUND_END`, `ROM_CMD_EV_SET_VOICING`, `ROM_CMD_EV_SET_VOLUME`, `ROM_SER_TX_PUT`, `ROM_SER_TX_HEX`, `ROM_SER_TX_RECORDS`, `ROM_CMD_RUN_NEXT` and `ROM_BANK_CMD_SELECT` (Task 3, `cmd_addr.h`);
  - `CMD_QUERY_TABLE` (Task 3, `data/fw_data.h`);
  - `fw_call_result` / `FW_CALL_RETURNED` (Task 1, `fw.h`);
  - the harness of `tests/test_fw_cmd.c`: `g_ram`, `g_f`, `g_ncalls`, `mock_rom_call` and `setup()` (Task 4), `count()` and `put_clause()` (Task 5), the mock's `ROM_CMD_RUN_NEXT` bookkeeping (Task 6), `check_call()` and the mock's `ROM_PAR_SETUP_VOICE` answer (Task 8). All its includes are already there.
- Produces:
  - `void cmd_sound(fw *f, uint8_t x, uint8_t a);` ($A8C0), `cmd_set_volume` ($AB05), `cmd_set_voicing` ($AB70) and `cmd_mark_index` ($AB2E), in `cmd_event.c`;
  - `cmd_echo` ($AB99), `cmd_query` ($ABD3, with a static `cmd_reply_setting` $C632) and `cmd_list_languages` ($ABBE), in `cmd_reply.c`;
  - 7 more `k_handlers` rows. After this task the table has **23 rows**: the 24 deferred handlers minus `bank_cmd_select` (Task 7 adds 10, Task 8 adds 6, this task adds 7). Only `bank_cmd_select` ($ABAF) and unknown addresses fall back to the ROM `cmd_run_next`.
  - in `tests/test_fw_cmd.c`: `g_clobber`, and the mock's answers to `ROM_SER_TX_PUT`, `ROM_SER_TX_HEX` and `ROM_SER_TX_RECORDS`.
- No new hook. Nothing in ROM calls these routines except the computed jump, which is now the C dispatcher. `cmd_reply_setting`'s only caller is `cmd_query`. The table stays at 16 replacements, so `golden_hybrid` keeps 17 modes.

**ROM facts checked** (all addresses in the conventions match the dumps and the ROM bytes; nothing contradicts them):
1. `cmd_sound`'s second event (sound end) is written with the offset in the instruction: `STA $1208,X`, `STA $1308,X`, `STA $1309,X`, with X = `ASL cmd_ev_count`. The address is 16-bit and does **not** wrap with X. At n = 127 (X = $FE), the start event is entry 127. The end event's position lands on `$1306` (the low byte of `cmd_ev_handlers[0]`), and its handler lands on `$1406/$1407` (`cmd_ev_args[0..1]`), not on entry 0. Only X itself wraps (n ≥ 128 → entry n − 128). The survey's "positions/handlers index wraps" holds for X, not for the instruction offsets.
2. Arguments are `STA $1406,X` (and `$1407,X` for `@T`) with X = n, unwrapped. So n ≥ 128 writes into `cmd_count` ($1486) and beyond, up to $1506 (`@T` at n = 255). This agrees with survey §2.5.
3. `cmd_sound` accepts every hex value ≤ $10, plus $21 ('X'/'x'), which it maps to $10. So `@TG` is effect 16 exactly like `@TX`. The survey's "or x = effect 16" is incomplete. 'H' (17) and up are rejected, except 'X'.
4. `cmd_mark_index` calls no `cmd_hex_value`. It tests the raw A (= `boot_clause[X]`) against '+' and writes **no argument**, so `cmd_ev_args[n]` stays stale.
5. `cmd_query` reads `$1AFF,X` raw. `cmd_reply_setting` compares that raw byte with the upper-case table letters, so a lower-case query (`@a?`) matches no table letter and gets no reply. The scan's case map does not apply, because the pattern `*?` matches any byte.
6. `cmd_reply_setting` reads Y after `JSR ser_tx_put` (`LDA $9990,Y`) and X after it (`STX $77`), so it relies on `ser_tx_put` preserving X and Y. The C therefore takes X and Y from the registers the call returns, as `tn_convert_clause` does. Nothing reads X or Y after `JSR ser_tx_hex` (`LDA $75; RTS`, then `cmd_query` exits through `cmd_run_done`, which reloads X). `ser_tx_put`, `ser_tx_hex` (via `ser_hex_digits`) and `ser_tx_records` push A, X and Y and pull them back. As input, `ser_tx_put` and `ser_tx_hex` read only A (`ser_tx_put` reads it from the stack, `LDA $0103,X` after TSX), and `ser_tx_records` reads none.
7. Y at a handler's entry is `cmd_next − 1`: `cmd_run_due` does `LDY cmd_next` before `JSR cmd_run_next`, which increments only the RAM byte. `cmd_echo` and `cmd_list_languages` pass this Y on to `ser_tx_put` / `ser_tx_records`, which only save and restore it (dead stack). The C passes it anyway, for exactness.
8. `cmd_query_table` at $998F is `A $14, B $13, C $0F, D $1A, F $17, H $0D, K $1D, M $18, P $1B, Q $19, R $15, S $0B, V $11, W $12, X $0C, Z $10, = $0E, $ $1C`, then 0. That is 18 pairs at offsets 0–34, and the 0 at offset 36 ends the loop. All the zero-page addresses are below $DA, so `LDA $00,X` reads RAM.
9. `cmd_list_languages` uses `LDM` (A unchanged), so `ser_tx_records` is entered with the handler's A = `boot_clause[X]`. `ser_reply_ptr` = $2026 = `bank_id_table`, and 7 records × 14 bytes = 98 bytes, the table's size.

- [ ] **Step 1: Write the failing tests**

In `tests/test_fw_cmd.c` (Task 4's harness, extended once more):
- Replace

```c
static unsigned g_touched;              /* ROM cmd_run_next calls that found RAM changed since g_before */
```

with

```c
static unsigned g_touched;              /* ROM cmd_run_next calls that found RAM changed since g_before */
static bool g_clobber;                  /* ser_tx_put returns X ^ $40 and Y = 2 */
```

- In `mock_rom_call`, replace

```c
        r->y = 0x0B;
        return FW_CALL_RETURNED;
    }
```

with

```c
        r->y = 0x0B;
        return FW_CALL_RETURNED;
    }
    if (addr == ROM_SER_TX_PUT || addr == ROM_SER_TX_HEX || addr == ROM_SER_TX_RECORDS) {
        /* the ser_tx_* routines push and pull A, X and Y: the registers come back as they went, unless g_clobber
           proves that cmd_reply_setting carries on with the X and Y that ser_tx_put returns */
        if (g_clobber && addr == ROM_SER_TX_PUT) {
            r->x ^= 0x40;
            r->y = 2;                              /* the 'B' entry of cmd_query_table */
        }
        return FW_CALL_RETURNED;
    }
```

- In `setup`, replace

```c
    g_touched = 0;
```

with

```c
    g_touched = 0;
    g_clobber = false;
```

- Above `void suite_fw_cmd(void)`, add these tests (they use `setup()`, `put_clause()`, `count()` and `check_call()`; because the mock plays the ROM `cmd_run_next`'s bookkeeping (Task 6), a handler missing from `k_handlers` shows up as a counted fallback call, not as an endless `cmd_run_due` loop):

```c
static void test_sound(void)
{
    setup();
    put_clause(" @T5 ");
    g_ram[TN_PHON_LEN] = 0x2A;
    g_ram[CMD_EV_COUNT] = 1;
    g_ram[CMD_EV_POSITIONS + 3] = 0x77;            /* high bytes of positions 1 and 2: never written */
    g_ram[CMD_EV_POSITIONS + 5] = 0x77;
    cmd_sound(&g_f, 3, g_ram[BOOT_CLAUSE + 3]);
    CHECK_EQ(g_ram[CMD_EV_COUNT], 3);              /* two events */
    CHECK_EQ(g_ram[CMD_EV_ARGS + 1], 5);
    CHECK_EQ(g_ram[CMD_EV_ARGS + 2], 5);
    CHECK_EQ(g_ram[CMD_EV_POSITIONS + 2], 0x2A);   /* low byte = tn_phon_len */
    CHECK_EQ(g_ram[CMD_EV_POSITIONS + 3], 0x77);
    CHECK_EQ(g_ram[CMD_EV_POSITIONS + 4], 0x2A);
    CHECK_EQ(g_ram[CMD_EV_POSITIONS + 5], 0x77);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS + 2), ROM_CMD_EV_SOUND_START);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS + 4), ROM_CMD_EV_SOUND_END);
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* Accepted arguments queue with the value; rejected ones leave the queue untouched (-1). */
typedef struct { char c; int want; } event_arg;

static void check_event_args(cmd_handler *h, const event_arg *k, size_t n, uint8_t events, uint16_t handler)
{
    for (size_t i = 0; i < n; i++) {
        setup();
        g_ram[TN_PHON_LEN] = 0x10;
        h(&g_f, 3, (uint8_t)k[i].c);
        if (k[i].want < 0) {
            CHECK_EQ(g_ram[CMD_EV_COUNT], 0);
            CHECK_EQ(g_ram[CMD_EV_ARGS], 0);
            CHECK_EQ(g_ram[CMD_EV_POSITIONS], 0);
            CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS), 0);
        } else {
            CHECK_EQ(g_ram[CMD_EV_COUNT], events);
            CHECK_EQ(g_ram[CMD_EV_ARGS], k[i].want);
            CHECK_EQ(g_ram[CMD_EV_POSITIONS], 0x10);
            CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS), handler);
        }
        CHECK(!g_f.fault);
    }
}

static void test_event_args(void)
{
    /* @T: 0-$10 and 'X' ($21 -> $10); 'G' is 16 too; 'H' (17), ':' (35) and '+' ($FB) are rejected */
    static const event_arg k_sound[] = { { '0', 0 }, { 'f', 15 }, { 'x', 16 }, { 'X', 16 }, { 'G', 16 },
                                         { 'H', -1 }, { ':', -1 }, { '+', -1 } };
    /* @A: 0-F */
    static const event_arg k_volume[] = { { '0', 0 }, { '6', 6 }, { 'f', 15 }, { 'G', -1 }, { ':', -1 } };
    /* @B: 0-9 */
    static const event_arg k_voicing[] = { { '0', 0 }, { '9', 9 }, { 'A', -1 }, { 'x', -1 } };
    check_event_args(cmd_sound, k_sound, sizeof k_sound / sizeof k_sound[0], 2, ROM_CMD_EV_SOUND_START);
    check_event_args(cmd_set_volume, k_volume, sizeof k_volume / sizeof k_volume[0], 1, ROM_CMD_EV_SET_VOLUME);
    check_event_args(cmd_set_voicing, k_voicing, sizeof k_voicing / sizeof k_voicing[0], 1, ROM_CMD_EV_SET_VOICING);
}

static void test_mark_index(void)
{
    setup();
    put_clause(" @I+ ");
    g_ram[TN_PHON_LEN] = 0x12;
    g_ram[CMD_EV_COUNT] = 2;
    g_ram[CMD_EV_ARGS + 2] = 0x99;                 /* stale: @I+ writes no argument */
    cmd_mark_index(&g_f, 3, g_ram[BOOT_CLAUSE + 3]);
    CHECK_EQ(g_ram[CMD_EV_COUNT], 3);
    CHECK_EQ(g_ram[CMD_EV_POSITIONS + 4], 0x12);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS + 4), ROM_CMD_EV_INDEX);
    CHECK_EQ(g_ram[CMD_EV_ARGS + 2], 0x99);
    CHECK_EQ(g_ncalls, 0);
    setup();
    cmd_mark_index(&g_f, 3, '^');                  /* only a raw '+' queues */
    CHECK_EQ(g_ram[CMD_EV_COUNT], 0);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS), 0);
    CHECK(!g_f.fault);
}

static void test_event_index_wrap(void)
{
    /* @A at n = 128: the argument goes to $1406 + 128 = $1486 = cmd_count; position and handler wrap to entry 0 */
    setup();
    g_ram[TN_PHON_LEN] = 0x31;
    g_ram[CMD_EV_COUNT] = 128;
    g_ram[CMD_COUNT] = 5;
    cmd_set_volume(&g_f, 3, '7');
    CHECK_EQ(g_ram[CMD_COUNT], 7);
    CHECK_EQ(g_ram[CMD_EV_POSITIONS], 0x31);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS), ROM_CMD_EV_SET_VOLUME);
    CHECK_EQ(g_ram[CMD_EV_COUNT], 129);

    /* @T at n = 127: X = $FE. The end event's offset is in the instruction ($1208,X / $1308,X), so it does not
       wrap: its position lands on cmd_ev_handlers[0]'s low byte ($1306), its handler on cmd_ev_args[0..1] */
    setup();
    g_ram[TN_PHON_LEN] = 0x31;
    g_ram[CMD_EV_COUNT] = 127;
    cmd_sound(&g_f, 3, '3');
    CHECK_EQ(g_ram[CMD_EV_ARGS + 127], 3);         /* $1485 */
    CHECK_EQ(g_ram[CMD_COUNT], 3);                 /* $1486 */
    CHECK_EQ(g_ram[CMD_EV_POSITIONS + 0xFE], 0x31);                             /* $1304 */
    CHECK_EQ(g_ram[CMD_EV_HANDLERS], 0x31);                                     /* $1306 = $1208 + $FE */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS + 0xFE), ROM_CMD_EV_SOUND_START);    /* $1404 */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_ARGS), ROM_CMD_EV_SOUND_END);                 /* $1406 = $1308 + $FE */
    CHECK_EQ(g_ram[CMD_EV_POSITIONS], 0);          /* entry 0's position untouched */
    CHECK_EQ(g_ram[CMD_EV_COUNT], 129);

    /* @B at n = 255: the argument at $1406 + 255 = $1505 (inside cmd_positions); X = $FE; the count wraps to 0 */
    setup();
    g_ram[TN_PHON_LEN] = 0x31;
    g_ram[CMD_EV_COUNT] = 255;
    cmd_set_voicing(&g_f, 3, '4');
    CHECK_EQ(g_ram[CMD_EV_ARGS + 255], 4);
    CHECK_EQ(g_ram[CMD_EV_POSITIONS + 0xFE], 0x31);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS + 0xFE), ROM_CMD_EV_SET_VOICING);
    CHECK_EQ(g_ram[CMD_EV_COUNT], 0);

    /* @I+ at n = 128: entry 0, and no argument (cmd_count stays) */
    setup();
    g_ram[TN_PHON_LEN] = 0x31;
    g_ram[CMD_EV_COUNT] = 128;
    g_ram[CMD_COUNT] = 5;
    cmd_mark_index(&g_f, 3, '+');
    CHECK_EQ(g_ram[CMD_COUNT], 5);
    CHECK_EQ(g_ram[CMD_EV_POSITIONS], 0x31);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_HANDLERS), ROM_CMD_EV_INDEX);
    CHECK_EQ(g_ram[CMD_EV_COUNT], 129);
    CHECK(!g_f.fault);
}

static void test_echo(void)
{
    setup();
    put_clause(" @E! ");
    g_ram[CMD_NEXT] = 1;                           /* the dispatcher advanced it: Y = cmd_next - 1 = 0 */
    cmd_echo(&g_f, 3, g_ram[BOOT_CLAUSE + 3]);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_SER_TX_PUT, '!', 3, 0);
    CHECK(!g_f.fault);
}

static void test_query_known(void)
{
    setup();
    put_clause(" @A? ");
    g_ram[CMD_NEXT] = 1;
    g_ram[CMD_VOLUME] = 6;
    g_ram[TN_RESUME_POS] = 0x99;
    cmd_query(&g_f, 3, g_ram[BOOT_CLAUSE + 3]);    /* X = the '?'; the letter is boot_clause[X - 1] */
    CHECK_EQ(g_ram[FW_TMP_75], 'A');
    CHECK_EQ(g_ram[TN_RESUME_POS], 3);             /* X parked in $77 */
    CHECK_EQ(g_ncalls, 2);
    check_call(0, ROM_SER_TX_PUT, 'A', 3, 0);      /* Y = the table offset */
    check_call(1, ROM_SER_TX_HEX, 6, 3, 0);        /* cmd_volume ($14) */

    setup();
    put_clause(" @$? ");                           /* the last entry: offset 34, cmd_source ($1C) */
    g_ram[CMD_SOURCE] = 0x42;
    cmd_query(&g_f, 3, '?');
    CHECK_EQ(g_ncalls, 2);
    check_call(0, ROM_SER_TX_PUT, '$', 3, 34);
    check_call(1, ROM_SER_TX_HEX, 0x42, 3, 34);

    setup();                                       /* X = 0 reads $1AFF = cmd_text[255] */
    g_ram[CMD_TEXT + 255] = 'W';
    g_ram[CMD_SPEED] = 3;
    cmd_query(&g_f, 0, 0);
    CHECK_EQ(g_ncalls, 2);
    check_call(0, ROM_SER_TX_PUT, 'W', 0, 26);
    check_call(1, ROM_SER_TX_HEX, 3, 0, 26);
    CHECK(!g_f.fault);
}

static void test_query_unknown(void)
{
    static const char *const k_texts[] = { " @N? ", " @a? ", " @?? " };   /* not listed; lower case is raw */
    for (size_t i = 0; i < sizeof k_texts / sizeof k_texts[0]; i++) {
        setup();
        put_clause(k_texts[i]);
        g_ram[TN_RESUME_POS] = 0x99;
        cmd_query(&g_f, 3, '?');
        CHECK_EQ(g_ram[FW_TMP_75], (uint8_t)k_texts[i][2]);
        CHECK_EQ(g_ram[TN_RESUME_POS], 0x99);
        CHECK_EQ(g_ncalls, 0);
        CHECK(!g_f.fault);
    }
}

/* cmd_reply_setting carries on with the X and Y that ser_tx_put returns (the ROM relies on it preserving them) */
static void test_query_returned_regs(void)
{
    setup();
    put_clause(" @A? ");
    g_clobber = true;
    g_ram[CMD_VOLUME] = 6;
    g_ram[CMD_VOICING] = 9;
    cmd_query(&g_f, 3, '?');
    CHECK_EQ(g_ram[TN_RESUME_POS], 0x43);          /* STX $77 after the call */
    CHECK_EQ(g_ncalls, 2);
    check_call(0, ROM_SER_TX_PUT, 'A', 3, 0);
    check_call(1, ROM_SER_TX_HEX, 9, 0x43, 2);     /* LDA $9990,Y with the returned Y = 2: cmd_voicing ($13) */
    CHECK(!g_f.fault);
}

static void test_list_languages(void)
{
    setup();
    put_clause(" @L ");
    g_ram[CMD_NEXT] = 1;
    cmd_list_languages(&g_f, 2, g_ram[BOOT_CLAUSE + 2]);
    CHECK_EQ(g_ram[SER_REPLY_ID], 'L');
    CHECK_EQ(g_ram[SER_REPLY_COUNT], 7);
    CHECK_EQ(g_ram[SER_REPLY_SIZE], 14);
    CHECK_EQ(fw_rd16(&g_f, SER_REPLY_PTR), BANK_ID_TABLE);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_SER_TX_RECORDS, 'L', 2, 0);
    CHECK(!g_f.fault);
}

/* Every handler of this task runs as C through the dispatcher; bank_cmd_select still falls back to ROM. */
static void test_run_due_all_rows(void)
{
    static const uint16_t k_addrs[] = { ROM_CMD_SOUND, ROM_CMD_SET_VOLUME, ROM_CMD_SET_VOICING, ROM_CMD_MARK_INDEX,
                                        ROM_CMD_ECHO, ROM_CMD_QUERY, ROM_CMD_LIST_LANGUAGES, ROM_BANK_CMD_SELECT };
    for (size_t i = 0; i < sizeof k_addrs / sizeof k_addrs[0]; i++) {
        setup();
        put_clause(" @A5 ");
        g_ram[CMD_COUNT] = 1;                      /* one queued command at position 0, argument index 3 */
        g_ram[CMD_NEXT_POS] = 0;
        g_ram[CMD_POSITIONS + 1] = 0xFF;
        fw_wr16(&g_f, CMD_HANDLERS, k_addrs[i]);
        g_ram[CMD_ARG_INDEX] = 3;
        cmd_run_due(&g_f, 0);
        CHECK_EQ(g_ram[CMD_NEXT], 1);
        CHECK_EQ(g_ram[CMD_NEXT_POS], 0xFF);
        CHECK_EQ(count(ROM_CMD_RUN_NEXT), k_addrs[i] == ROM_BANK_CMD_SELECT ? 1u : 0u);
        CHECK(!g_f.fault);
    }
}
```

Then add these calls as the last statements of `suite_fw_cmd`:

```c
    test_sound();
    test_event_args();
    test_mark_index();
    test_event_index_wrap();
    test_echo();
    test_query_known();
    test_query_unknown();
    test_query_returned_regs();
    test_list_languages();
    test_run_due_all_rows();
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw_cmd"`
Expected: a compile error. `cmd_sound` (and the other six) are undeclared, so C4013 is reported and `/WX` turns it into an error (`error C2220`).

- [ ] **Step 3: Implement**

`src/firmware/cmd_handlers.h`: replace

```c
void cmd_set_voice(fw *f, uint8_t x, uint8_t a);          /* $AA74 @V: 0 (off) or presets 1-6 */

#endif
```

with

```c
void cmd_set_voice(fw *f, uint8_t x, uint8_t a);          /* $AA74 @V: 0 (off) or presets 1-6 */

/* Event producers (cmd_event.c): queue a command event at the phonetic position tn_phon_len */
void cmd_sound(fw *f, uint8_t x, uint8_t a);              /* $A8C0 @T */
void cmd_set_volume(fw *f, uint8_t x, uint8_t a);         /* $AB05 @A */
void cmd_set_voicing(fw *f, uint8_t x, uint8_t a);        /* $AB70 @B */
void cmd_mark_index(fw *f, uint8_t x, uint8_t a);         /* $AB2E @I+ (deferred part) */

/* Replies (cmd_reply.c): queued in the serial transmit ring by the ROM ser_tx_* routines */
void cmd_echo(fw *f, uint8_t x, uint8_t a);               /* $AB99 @E */
void cmd_query(fw *f, uint8_t x, uint8_t a);              /* $ABD3 @c? */
void cmd_list_languages(fw *f, uint8_t x, uint8_t a);     /* $ABBE @L */

#endif
```

`src/firmware/cmd_run.c`:
- In `k_handlers`, replace

```c
    { 0, NULL },
```

with

```c
    { ROM_CMD_SOUND,            cmd_sound },
    { ROM_CMD_SET_VOLUME,       cmd_set_volume },
    { ROM_CMD_SET_VOICING,      cmd_set_voicing },
    { ROM_CMD_MARK_INDEX,       cmd_mark_index },
    { ROM_CMD_ECHO,             cmd_echo },
    { ROM_CMD_QUERY,            cmd_query },
    { ROM_CMD_LIST_LANGUAGES,   cmd_list_languages },
    { 0, NULL },
```

- Replace the comment above `k_handlers`:

```c
/* The C handlers by ROM address: the bytes cmd_scan_clause copies into cmd_handlers. A command whose address is
   not here runs through the ROM cmd_run_next (spec 2.4). The NULL row ends the table. */
```

with

```c
/* The C handlers by ROM address: the bytes cmd_scan_clause copies into cmd_handlers. Every deferred handler is
   here except bank_cmd_select ($ABAF, P2 bank switching): it, and any unknown address (the latent NUL bug of the
   scan, spec 2.6), runs through the ROM cmd_run_next (spec 2.4). The NULL row ends the table. */
```

`src/firmware/cmd_event.c` (new):

```c
/* The deferred handlers that queue a command event instead of changing a setting (M4.2 spec 2.1): @T, @A, @B
   and the deferred part of @I+. An event is keyed by the phonetic position tn_phon_len and runs at playback
   (cmd_run_event $BB74, still ROM). Entered as the ROM handlers are: x = the argument's boot_clause index,
   a = boot_clause[x]. */
#include "cmd_handlers.h"
#include "cmd_addr.h"
#include "cmd_match.h"

/* The queue as the producers address it, n = cmd_ev_count:
   - cmd_ev_args[n] is STA $1406,X with X = n, unwrapped: n >= 128 writes on into cmd_count ($1486) and the
     deferred queue;
   - positions and handlers are STA $1206,X / $1306,X with X = ASL n, which wraps at n = 128; an offset in the
     instruction ($1208,X, $1308,X) adds to the 16-bit address and does not wrap, so ofs is X plus that offset;
   - only the low byte of a 16-bit position is written (pros_phrase_gen rewrites the whole word later).
   Within one producer the addresses never overlap, so the order of the writes does not matter. */
static void put_event(fw *f, uint16_t ofs, uint16_t handler)
{
    fw_wr8(f, (uint16_t)(CMD_EV_POSITIONS + ofs), fw_rd8(f, TN_PHON_LEN));
    fw_wr16(f, (uint16_t)(CMD_EV_HANDLERS + ofs), handler);
}

/* $A8C0 @T: effect 0-$10 (so 'G' is effect 16), or 'X' (hex value $21) for effect 16. Two events at the same
   position with the same argument: cmd_ev_sound_start, then cmd_ev_sound_end. */
void cmd_sound(fw *f, uint8_t x, uint8_t a)
{
    (void)x;
    uint8_t v = cmd_hex_value(f, a);
    if (v == 0x21)
        v = 0x10;
    if (v >= 0x11)
        return;
    uint8_t n = fw_rd8(f, CMD_EV_COUNT);
    fw_wr8(f, (uint16_t)(CMD_EV_ARGS + n), v);
    fw_wr8(f, (uint16_t)(CMD_EV_ARGS + 1 + n), v);              /* STA $1407,X */
    uint8_t i = (uint8_t)(n << 1);
    put_event(f, i, ROM_CMD_EV_SOUND_START);
    put_event(f, (uint16_t)(i + 2), ROM_CMD_EV_SOUND_END);      /* $1208,X / $1308,X: no wrap */
    fw_wr8(f, CMD_EV_COUNT, (uint8_t)(n + 2));                  /* INC twice */
}

/* $AB05 @A: volume 0-F, set at playback by cmd_ev_set_volume */
void cmd_set_volume(fw *f, uint8_t x, uint8_t a)
{
    (void)x;
    uint8_t v = cmd_hex_value(f, a);
    if (v >= 0x10)
        return;
    uint8_t n = fw_rd8(f, CMD_EV_COUNT);
    fw_wr8(f, (uint16_t)(CMD_EV_ARGS + n), v);
    put_event(f, (uint8_t)(n << 1), ROM_CMD_EV_SET_VOLUME);
    fw_wr8(f, CMD_EV_COUNT, (uint8_t)(n + 1));
}

/* $AB70 @B: voicing 0-9, set at playback by cmd_ev_set_voicing */
void cmd_set_voicing(fw *f, uint8_t x, uint8_t a)
{
    (void)x;
    uint8_t v = cmd_hex_value(f, a);
    if (v >= 0x0A)
        return;
    uint8_t n = fw_rd8(f, CMD_EV_COUNT);
    fw_wr8(f, (uint16_t)(CMD_EV_ARGS + n), v);
    put_event(f, (uint8_t)(n << 1), ROM_CMD_EV_SET_VOICING);
    fw_wr8(f, CMD_EV_COUNT, (uint8_t)(n + 1));
}

/* $AB2E, the deferred part of @I+: with the raw argument '+' (no cmd_hex_value), an index-mark event; no
   argument is written, so cmd_ev_args[n] stays stale */
void cmd_mark_index(fw *f, uint8_t x, uint8_t a)
{
    (void)x;
    if (a != '+')
        return;
    uint8_t n = fw_rd8(f, CMD_EV_COUNT);
    put_event(f, (uint8_t)(n << 1), ROM_CMD_EV_INDEX);
    fw_wr8(f, CMD_EV_COUNT, (uint8_t)(n + 1));
}
```

`src/firmware/cmd_reply.c` (new):

```c
/* The deferred handlers that reply on the serial line (M4.2 spec 2.2): @E, @c? and @L. The reply goes into the
   transmit ring through the ROM ser_tx_* routines (the serial subsystem, M4.6), called with A, X and Y as the
   ROM has them at each JSR. Entered as the ROM handlers are: x = the argument's boot_clause index,
   a = boot_clause[x]. */
#include "cmd_handlers.h"
#include "cmd_addr.h"
#include "data/fw_data.h"

/* A ser_tx_* routine with A, X, Y in; returns the registers it leaves (it pushes and pulls all three). */
static fw_regs ser_call(fw *f, uint16_t addr, uint8_t a, uint8_t x, uint8_t y)
{
    fw_regs r = { a, x, y, 0 };
    fw_rom_call(f, addr, &r, FW_ROM_CALL_BUDGET);
    return r;
}

/* Y at a handler's entry: cmd_run_due loaded cmd_next before cmd_run_next advanced it. The ser_tx_* routines
   only save and restore Y, so it reaches the dead stack alone; it is passed for exactness. */
static uint8_t handler_y(const fw *f) { return (uint8_t)(fw_rd8(f, CMD_NEXT) - 1); }

/* $AB99 @E: the argument character, raw, is the reply */
void cmd_echo(fw *f, uint8_t x, uint8_t a)
{
    ser_call(f, ROM_SER_TX_PUT, a, x, handler_y(f));
}

/* $C632: a = the setting letter, compared raw. If cmd_query_table lists it: reply the letter (ser_tx_put), then
   the setting's zero-page byte as two hex digits (ser_tx_hex). $75 = the letter; X is parked in $77
   (tn_resume_pos) around the zero-page read. The ROM continues with the X and Y that ser_tx_put returns. It
   returns A = $75, which cmd_query does not use. */
static void cmd_reply_setting(fw *f, uint8_t a, uint8_t x)
{
    fw_wr8(f, FW_TMP_75, a);
    uint8_t y = 0;
    for (;;) {                                                      /* $C636 */
        uint8_t letter = fw_rom8(f, (uint16_t)(CMD_QUERY_TABLE + y));
        if (letter == 0)
            return;                                                 /* not listed: no reply */
        if (letter == fw_rd8(f, FW_TMP_75))
            break;
        y = (uint8_t)(y + 2);
    }
    fw_regs r = ser_call(f, ROM_SER_TX_PUT, a, x, y);               /* A = the table letter = $75 */
    uint8_t zp = fw_rom8(f, (uint16_t)(CMD_QUERY_TABLE + 1 + r.y)); /* LDA $9990,Y */
    fw_wr8(f, TN_RESUME_POS, r.x);                                  /* STX $77 */
    uint8_t v = fw_mem8(f, zp);                                     /* TAX; LDA $00,X */
    ser_call(f, ROM_SER_TX_HEX, v, fw_rd8(f, TN_RESUME_POS), r.y);  /* LDX $77 */
}

/* $ABD3 @c?: x = the '?'; the letter before it is read as $1AFF,X (boot_clause[x - 1]; x = 0 reads
   cmd_text[255]) */
void cmd_query(fw *f, uint8_t x, uint8_t a)
{
    (void)a;
    cmd_reply_setting(f, fw_rd8(f, (uint16_t)(BOOT_CLAUSE - 1 + x)), x);
}

/* $ABBE @L: 'L', then the 7 bank ids of 14 characters from bank_id_table, then CR (ser_tx_records) */
void cmd_list_languages(fw *f, uint8_t x, uint8_t a)
{
    fw_wr8(f, SER_REPLY_ID, 'L');
    fw_wr8(f, SER_REPLY_COUNT, 7);
    fw_wr8(f, SER_REPLY_SIZE, 14);
    fw_wr16(f, SER_REPLY_PTR, BANK_ID_TABLE);
    ser_call(f, ROM_SER_TX_RECORDS, a, x, handler_y(f));   /* LDM leaves A = boot_clause[x] */
}
```

- [ ] **Step 4: Faithfulness check against the dumps**

Compare each routine with its dump (`build/ghidra740/dump/`, read-only), instruction by instruction:

- `rom_A8C0_cmd_sound.txt`:
  - `JSR cmd_hex_value` → `cmd_hex_value(f, a)`.
  - `CMP #0 / BCC` is dead.
  - `CMP #$21 / BNE / LDA #$10` → `v == 0x21`.
  - `CMP #$11 / BCS` → `v >= 0x11` returns.
  - `LDX cmd_ev_count`, `STA $1406,X`, `STA $1407,X` → the two argument writes, unwrapped.
  - `TXA / ASL / TAX` → `i`.
  - `LDA tn_phon_len`, `STA $1206,X`, `STA $1208,X` → the positions at `i` and `i + 2`, unwrapped offsets.
  - `$A7,$BB` to `$1306/$1307,X` and `$80,$BC` to `$1308/$1309,X` → `ROM_CMD_EV_SOUND_START` ($BBA7) and `ROM_CMD_EV_SOUND_END` ($BC80) little-endian.
  - `INC cmd_ev_count` ×2 → `n + 2`.
  - `JMP cmd_run_done` → return.
- `rom_AB05_cmd_set_volume.txt`:
  - `CMP #$10 / BCS` → `v >= 0x10`.
  - `STA $1406,X` with X = n.
  - `ASL`, then `STA $1206,X` (tn_phon_len), and `$ED,$BC` → `ROM_CMD_EV_SET_VOLUME` ($BCED).
  - One `INC`.
- `rom_AB70_cmd_set_voicing.txt`: as `@A`, with `CMP #$0A` and `$EA,$BC` → `ROM_CMD_EV_SET_VOICING` ($BCEA).
- `rom_AB2E_cmd_mark_index.txt`:
  - `CMP #$2B / BNE` on the entry A, with no `cmd_hex_value`.
  - No argument store.
  - `ASL`, `STA $1206,X`, and `$A0,$BB` → `ROM_CMD_EV_INDEX` ($BBA0).
  - One `INC`.
- `rom_AB99_cmd_echo.txt`: `JSR ser_tx_put` with the entry A and X (and Y = cmd_next − 1, from `rom_A880_cmd_run_due.txt` `LDY cmd_next` and `rom_A893_cmd_run_next.txt` `INC cmd_next`).
- `rom_ABD3_cmd_query.txt`: `LDA $1AFF,X`, then `JSR cmd_reply_setting` with X unchanged.
- `rom_C632_cmd_reply_setting.txt`:
  - `STA $75`, then `LDY #0`.
  - The loop: `LDA $998F,Y / BEQ` exit, `CMP $75 / BEQ` found, `INY ×2`.
  - `JSR ser_tx_put` (A = the table letter, X = entry X, Y = offset).
  - `LDA $9990,Y` with the returned Y, `STX $77` with the returned X, `TAX / LDA $00,X`, `LDX $77`.
  - `JSR ser_tx_hex` (Y = the returned Y).
  - `LDA $75 / RTS`: A is unused by `cmd_query`.
- `rom_ABBE_cmd_list_languages.txt`: the `LDM` values $4C ('L'), 7, $0E, $26, $20 → $70–$74, then `JSR ser_tx_records`.
- The callees' register use: `rom_C4DD_ser_tx_put.txt` (PHA/TXA/PHA/TYA/PHA … PLA/TAY/PLA/TAX/PLA; reads A from `$0103,X`), `rom_C571_ser_tx_hex.txt` with `rom_C53A_ser_hex_digits.txt` (A, X and Y preserved), and `rom_C5A4_ser_tx_records.txt` (A, X and Y preserved; reads only $70–$74 and the records).
- `cmd_query_table`: the 37 bytes at $998F (`firmware_data` compares them, Task 3).

- [ ] **Step 5: Run the tests**

Run, in order:
- `cmd //c ".\build.bat test fw_cmd"`
- `cmd //c ".\build.bat test diff_lts"`
- `cmd //c ".\build.bat test golden_hybrid"`
- `cmd //c ".\build.bat test"`

Expected:
- `suite_fw_cmd` ok, then `1 suite(s), 0 failure(s), 0 skip(s)`.
- `diff_lts`:
  - All 16 routines print `N calls checked` with N > 0: the 11 of M4.1 plus `cmd_match_table`, `cmd_hex_value`, `cmd_scan_clause`, `cmd_run_due` and `cmd_apply_speaker`.
  - 0 mismatches, and the flush line as before.
  - `cmd_run_due`'s checked calls now run the event producers as C (`volume`, `breathy`, `effects`, `index_marks`, `cmd_args`), and so do the replies (`queries`, `cmd_misc`). Their transmit-ring and `$75`/`$77` writes are compared through the RAM diff.
- `golden_hybrid`: `32 corpus files x 17 modes (all, then each routine alone)`, all identical, with the flush and unwind assertions holding.
- The full run: all 30 suites ok (the 29 of M4.1 plus `suite_fw_cmd`), with no warnings, and the firmware boundary check passing.

Then confirm spec criterion 5, that the CLI's hybrid output equals the rom engine's. `SP` is a scratch directory outside the repo:

```bash
for t in cmd_misc index_marks cmd_args cmd_unmatched bank_select; do
  ./build/apollo2.exe --engine rom --text-file tests/corpus/$t.txt -o "$SP/${t}_rom.wav" || echo "$t rom failed"
  ./build/apollo2.exe --engine hybrid --text-file tests/corpus/$t.txt -o "$SP/${t}_hybrid.wav" || echo "$t hybrid failed"
  cmp -s "$SP/${t}_rom.wav" "$SP/${t}_hybrid.wav" && echo "$t same" || echo "$t DIFFERS"
done
./build/apollo2.exe --engine rom -o "$SP/eq1_rom.wav" "@=1 Hello world." && ./build/apollo2.exe --engine hybrid -o "$SP/eq1_hybrid.wav" "@=1 Hello world." && cmp "$SP/eq1_rom.wav" "$SP/eq1_hybrid.wav" && echo "eq1 same"
```

Expected: five `<name> same` lines and `eq1 same`, with no `failed` or `DIFFERS` lines.

Then confirm the dispatcher table is complete:
- `grep -cE '^\s*\{\s*ROM_CMD_[A-Z_]+,\s*cmd_[a-z_]+\s*\},' src/firmware/cmd_run.c` must print `23`: 24 deferred handlers minus `bank_cmd_select`.
- `grep -c "ROM_BANK_CMD_SELECT,\s*cmd_" src/firmware/cmd_run.c` must print `0`.

- [ ] **Step 6: Commit**

```bash
git add src/firmware/cmd_event.c src/firmware/cmd_reply.c src/firmware/cmd_handlers.h src/firmware/cmd_run.c tests/test_fw_cmd.c
git commit -m "Event producers and replies in C: every ported handler runs as C, only bank_cmd_select and unknown addresses fall back to ROM (M4.2 2.2)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Parent spec rev 9, M4.2 status and README (criterion 8)

**Files:**
- Modify: `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`, `docs/superpowers/specs/2026-10-03-m4-2-command-parser-design.md`, `README.md`

**Interfaces:** documentation only.

- [ ] **Step 1: Parent spec, rev 9**

Make these edits in `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`. Mark each one *(rev 9)*. Find each place by its text, not by line number.

1. **Status line** (line 4). Insert after `**Status:** Approved. `:

   `*(rev 9)* Revision 9 records milestone 4.2: the command parser in C (matcher, clause scan, dispatcher and every deferred handler), the command tables converted, and the non-local exit for @= (design: docs/superpowers/specs/2026-10-03-m4-2-command-parser-design.md). `

2. **§6 step 3** (the paragraph that begins `*(rev 7)* Built in M4.0:`). Append at its end:

   `*(rev 9)* M4.2: a ROM call from C that resets the stack (bank_enter, @= with a bank present) is "unwound": machine_rom_call stops right after the reset, the C call chain is abandoned up to the hook that entered C, and the machine goes on where the ROM went.`

3. **§7.1**, the differential-test bullet (the one containing `*(rev 8)* A call that reaches boot_loop_play`). Append at its end:

   `*(rev 9)* The flush skip is legal only in tn_convert_clause. A call whose ROM copy resets the stack stops right after the reset in both copies and is compared like any other.`

4. **§8 milestone 4** (the paragraph containing `**M4.1 done**`). Append at its end:

   `*(rev 9)* **M4.2 done** (date of the final commit): the command parser runs as C (16 routines hooked, the handlers behind a C dispatcher, bank_cmd_select through the ROM cmd_run_next); the command tables are converted (35,878 bytes in all); five new corpus goldens (rom engine) reach every command; diff_lts and golden_hybrid (32 files x 17 modes) pass; the hybrid engine no longer faults on @=1. Next: M4.3, phoneme expansion and prosody.`

- [ ] **Step 2: M4.2 spec status**

In `docs/superpowers/specs/2026-10-03-m4-2-command-parser-design.md`, replace `**Status:** design, awaiting user review` with:

`**Status:** implemented (plan docs/superpowers/plans/2026-10-03-m4-2-command-parser.md)`

Then append a final section headed `7. As built` (a level-2 heading, like the spec's other sections) with these bullets, one fact each. They are the facts Tasks 3–9 found where the ROM is more precise than, or differs from, the spec. If the implementation found any of them different again, write what the ROM does and report it.

```markdown
- Thunk outputs as declared: `cmd_scan_clause` none; `cmd_match_table` A, X and Y (A = $FF at the end marker, else the entry X; X preserved); `cmd_hex_value` A, X, Y and Z (X and Y preserved); `cmd_run_due` X and Y (Y = `cmd_next`, as its last `LDY` leaves it); `cmd_apply_speaker` A, X and Y.
- `par_setup_voice` reads none of A, X and Y and returns X = 9 and Y = $0B; `cmd_apply_pitch` restores only A, so `boot_reset` sees X and Y changed after `cmd_apply_speaker` (its `bank_load_ramcode` saves them in `$76`/`$78`).
- The dispatcher's ROM fallback calls `cmd_run_next` with X = the position and Y = `cmd_next`, because `bank_cmd_select` → `bank_load_ramcode` saves Y in `$78`.
- The dispatcher finds a C handler by address in a table (`k_handlers` in `cmd_run.c`), not with a `switch`.
- The scan skips the case map on bit 0 of `cmd_phonetic` (`BBS0`), not on any non-zero value.
- The scan writes `$78` a second time for every case-mapped character (`$AD1B`), so it ends as the Y of the last mapped character or of the last `cmd_char`, whichever came later.
- The scan is a do-while: with `boot_clause_len` = 0 it still reads one character and sets the length to 1 (unreachable: `boot_get_clause` always puts a space at index 0).
- `cmd_hex_value` splits on bit 7 of c − 'A' (`BMI`), so case-mapped codes above 'Z' wrap as well ($80 gives $49, $E0 gives $B0).
- `cmd_sound` writes its second event at instruction offsets (`$1208,X`, `$1308,X`) that do not wrap with X: at `cmd_ev_count` = 127 it lands on `cmd_ev_handlers[0]`'s low byte and on `cmd_ev_args[0..1]`.
- The event producers' arguments (`STA $1406,X` with X = `cmd_ev_count`) do not wrap: a count of 128 or more writes into `cmd_count` and the deferred queue.
- `@T` accepts every value up to $10 plus 'X', so `@TG` is effect 16, like `@TX`.
- `cmd_mark_index` tests the raw '+' (no `cmd_hex_value`) and writes no argument, so `cmd_ev_args[n]` stays stale.
- A query compares the raw letter with the upper-case letters of `cmd_query_table`, so a lower-case query (`@a?`) gets no reply.
- `cmd_reply_setting` goes on with the X and Y that `ser_tx_put` returns; `cmd_query` reads the letter at `$1AFF,X`, so X = 0 reads `cmd_text[255]`.
- The speed-map base is named `CMD_SPEED_MAP_DATA` ($C898, not converted) instead of `ROM_PROS_SPEED_MAPS`. The voice-table addresses `PAR_VOICE_A` ($D46A) and `PAR_VOICE_B` ($E5E3) are immediates in `cmd_select_voice_a`/`_b`; the tables stay unconverted.
```

- [ ] **Step 3: README**

In `README.md`'s "Hybrid engine (milestone 4)" section, replace:

```
(src/firmware). Today these are the letter-to-sound routines (M4.0) and text normalisation, tn_convert_clause
and tn_append_phonetic (M4.1); the rest still runs on the emulated CPU.
```

with:

```
(src/firmware). Today these are the letter-to-sound routines (M4.0), text normalisation, tn_convert_clause
and tn_append_phonetic (M4.1), and the command parser: the @ command matcher, the clause scan, the dispatcher
and every deferred command handler (M4.2); the rest still runs on the emulated CPU.
```

- [ ] **Step 4: Run the full suite and commit**

Run: `cmd //c ".\build.bat test"`
Expected: all 30 suites ok.

```bash
git add docs/superpowers/specs/2026-09-30-software-apollo2-design.md docs/superpowers/specs/2026-10-03-m4-2-command-parser-design.md README.md
git commit -m "Spec rev 9: milestone 4.2 done (the command parser in C)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
