# M4.6c2 Boot, the Main Loop and isr_idle in C — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port boot, the main loop and `isr_idle` to C (21 hooked routines), with the C pulling its input through an idle handler, and prove them against the ROM through `diff_main`, `diff_lts`, `diff_serial` and the goldens. That completes milestone 4.

**Architecture:**
- **Infrastructure first (Task 1).**
  - The machine gets an idle handler (feed or stop), and both engines run through it.
  - A new wait kind, `FW_WAIT_INPUT` at `$C602`, stops by abandoning the C and leaving the machine at the idle point.
  - Entry rows run under the engine's phase deadline.
  - Two new host calls: `reset_cpu` and `set_interrupts`.
  - `machine_wait_interrupt` gets the delivery guard.
  - The suite `suite_fw_boot` is added.
- **The data (Task 2).** The self-test command strings and `boot_prosody_defaults` are converted (the baud texts already are, since M4.1).
- **The tests before the C (Task 3).**
  - `diff_main`: the ROM engine against the hybrid at every input idle and frame event, with RAM compared except the stack page.
  - The deferred `diff_serial` DIP probes.
  - Both proved ROM against ROM.
- **Then the C.** The boot helpers and `cmd_say_phonetic` (Task 4), the main loop's input side and the self-test (Task 5), and the entries and `isr_unused` (Task 6).
- **C calls C.** The entries call boot and the main loop directly, and the main loop calls the serial, speech and bank C directly.
- **What stays ROM.** The ROM `cmd_run_next`, for a NUL-bug handler address only, and the host's wait loops (`$C500`, `$B372`, `$B37F`).
- **Hooks.** 21 routines, giving 131 replacements and 132 `golden_hybrid` modes.

**Tech Stack:** C11 with MSVC 2022 (`build.bat`, `/W4 /WX`).

**Spec:** `docs/superpowers/specs/2026-10-13-m4-6c2-boot-main-loop-design.md` (M4.6c2 spec). Its foundations are:
- the M4.6c1 spec, `docs/superpowers/specs/2026-10-13-m4-6c1-banks-design.md` (the bank code, `fw_enter`/`enter`, the selected bank; §6 "As built");
- the M4.6b spec, `docs/superpowers/specs/2026-10-09-m4-6b-speech-timing-design.md` (the wait seam, `FW_WAIT_INTERRUPT`, the branch probe; §6 "As built");
- the M4.6a spec, `docs/superpowers/specs/2026-10-08-m4-6a-serial-design.md` (SFR access, the serial C, `diff_serial`; §6 "As built");
- the M4.4b spec, `docs/superpowers/specs/2026-10-07-m4-4b-frame-loop-design.md` (`waits` rows, `diff_play`, the bridge deadline);
- the M4.0 spec, `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md` (§8 "As built").

The parent spec is `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 17; §4 the machine model and serial delivery at the idle WIT).

## Global Constraints

**Build and run**
- Build and test from the repo root with the Bash tool (Git Bash): `cmd //c ".\build.bat test [SUITE_FILTER]"`.
- In a git worktree, first run `export APOLLO2_ROM="C:/Users/abart/Desktop/Dolphin Apollo 2 ROM/UK_V2.01_4.04_ROM.BIN"`, because the ROM lives only in the main checkout.
- C11, `/W4 /WX`, `/wd4244` (8-bit truncation is intentional).
  - Fix warnings in code. Never add `/wd` flags or pragmas.
  - The only pragma allowed is the existing 4611 one in `thunks.c`. `setjmp` therefore stays out of the tests and the firmware.
- Every `.c` file under `src/` and `tests/` must have a **unique basename**, because `build.bat` puts all objects in `build\`.
- Never run `build\apollo2.exe` without `-o FILE`, because it plays audio on the user's speakers.
- Audio stays 10 kHz native, with no resampling.
- The full suite takes about 14–16 minutes. Use a suite filter while iterating and the full run at each task's end, in the background (a foreground Bash call is capped at 600000 ms).

**What may and may not be committed**
- Never commit the ROM, any `*.BIN`/`*.bin`, `*.mp3`, `apollo2_excerpt.wav`, `*.sla`, `build/` or `apollo_ghidra/`.
- Committed files contain no ROM bytes as hex blobs. Converted data is readable C: numbers with their meaning, named bits, strings, named addresses.
- Stage files by explicit path only. Never use `git add -A` or `git add .`.

**User-owned files and tools**
- Never stage, move or delete the user's untracked files: `docs/*` other than `docs/superpowers/`, `pics/`, `session_memory.json`, `apollo2_for_aksel.wav`, `apollo2_for_aksel.mp3`, `apollo2.png`.
- Never modify anything under `C:\ghidra_12.1.3_PUBLIC`, the user's Ghidra GUI project, or `ghidra740/` (including `ghidra740/coverage/`). Only read the annotations and the dumps.
- Disassembly dumps for checking the C against the ROM (git-ignored, read-only) are in the main checkout:
  - `C:\Users\abart\Desktop\Dolphin Apollo 2 ROM\build\ghidra740\dump\rom_<ADDR>_<name>.txt` and `ramcode_<ADDR>_<name>.txt`
  - `data.txt` and `functions.txt` in the same folder.

**Correctness rules**
- The goldens in `tests/golden/*.frm` stay byte-identical. No new golden. If anything changes a golden, stop and report.
- The machine model (parent spec §4) does not change. The only machine additions are API: `machine_set_idle_handler` with `machine_idle`, the input wait `machine_wait_input`, the phase deadline field `phase_deadline`, the delivery guard in `machine_wait_interrupt`, and access to state for the comparisons.
- Nothing under `src/firmware/` includes a `machine/` or `cpu740/` header. `build.bat` checks this.
- The ported C keeps the ROM's behaviour exactly, quirks included (M4.6c2 spec §2.5, §2.6):
  - every RAM write, the scratch ones included (`$01–$C9` cleared by `boot_reset`, `$78` around `ser_rx_put`, `$1827`, `$6B`, the ring pointers, `pros_params`);
  - every SFR read and write in the ROM's order, through the host, each `SEB`/`CLB` as a read then a write;
  - the CPU state the ROM sets that later ROM code or the interrupts see, through `reset_cpu` and `set_interrupts`;
  - the 8-bit wraps and loop bounds exactly as the ROM has them;
  - the exit registers and flags the callers read.
- Writes to the dead stack (below SP at exit) need not be reproduced. The end-to-end `diff_main` excludes the stack page `$0100–$01FF` and SP (user decision); every other comparison is unchanged.
- ROM data is read only through `fw_rom8` (or `fw_ind_y`/`fw_mem8`) at its ROM address. SFRs are accessed only through the `fw_sfr*` helpers.
- A polling loop on a flag an interrupt sets never spins in C: it waits through `fw_wait` between tests (`FW_WAIT_INPUT` at `isr_idle`'s WIT; `FW_WAIT_INTERRUPT` where the ROM would spin with nothing changed).
- Thunk outputs: each with a comment saying what the ROM callers read after the call, checked in the dumps.
- No new ROM call to firmware code. The only ROM firmware code a hybrid run may execute is the NUL fallback's ROM `cmd_run_next` and the host's wait loops.
- `diff_main`, `diff_lts`, `diff_play` and `diff_serial` failures are fixed by finding the root cause in the dump. Never loosen a comparison or an assertion.

**Commits**
- End every commit message with exactly `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. This is the user's rule for this session.

## Review Focus

The inputs and conditions most likely to bite a user that the spec implies but no other test exercises. Each line has a test in the task that owns the code.

1. **A text with no final punctuation and no CR inside it, longer than a clause** (240+ characters of words separated by spaces). `boot_get_clause` cuts it at X ≥ `$DC` on a space, or at X ≥ `$FA`, and carries a cut `cmd_char` in `$1827`. The hybrid must cut every clause where the ROM does. Test: `diff_main` extra texts (Task 5).
2. **Several sentences in one input** (`One. Two? Three!`): each punctuation ends a clause and the main loop speaks them in order without returning to the engine. Every clause boundary and the idle count must match. Test: `diff_main` extra text and the CLI check (Task 6).
3. **An empty input or a lone CR** (the text `""`, then CR). The engine feeds a CR only; the C must handle the short clause that CR ends (`"   "`, three characters, led by the space the self-test string leaves in the ring) and idle again, exactly as the ROM. Test: `diff_main` and a `golden_hybrid`-style rom-against-hybrid comparison (Task 5).
4. **A command-only input** (`@W5 @A3`) that speaks nothing. The main loop must not stall waiting for frames that never come, and must idle like the ROM. Test: `diff_main` extra text (Task 5).
5. **Input that arrives while speech is still playing** (two feeds without waiting for idle, if the engine API allows it; otherwise a long text whose second clause arrives in the ring during the first clause's playback). The C must take the next clause at the same point as the ROM. Test: `diff_main` (Task 5); if no engine path can produce it, pin it with a unit test of `boot_get_clause` against a ring filled mid-wait.

---

### Task 1: Infrastructure: the idle handler (`machine_set_idle_handler`, `machine_idle`) and both engines' loop through it (`engine_input`, `engine_watch`), the input wait (`FW_WAIT_INPUT`, `machine_wait_input`, `f->stopped`), the phase deadline of the entry rows, `reset_cpu` and `set_interrupts`, the delivery guard, the suite `suite_fw_boot` (spec §3, §4.5, §4.8 item 1)

**ROM facts checked / what was verified while proving the plan** (`rom_C5FE_isr_idle.txt`, `rom_A61C_boot_reset.txt`, `rom_A697_boot_get_clause.txt`, `rom_C612_boot_queue_string.txt`, `rom_C581_ser_tx_service.txt`, `rom_C5DE_isr_par_hold.txt`, `rom_B35D_isr_timer_start.txt`; `src/machine/machine.c`, `src/cpu740/cpu740.c`, `src/engine/rom_engine.c`, `tests/test_golden_hybrid.c`; everything below was run, nothing is copied from the spec):
- **The input wait** (`isr_idle` $C5FE–$C604: `PHA`, `BBS0 boot_dip,$C603`, `WIT` at $C602, `PLA`, `RTS`): $C602 is the firmware's only WIT, and the machine's idle point is the CPU waiting with the PC at $C603 (`at_idle`), where `maybe_deliver` hands over one serial byte. `machine_wait_input` puts the CPU there and tests `machine_run`'s idle condition (now the static `idle()` both share: no queued input, no byte in the UART, ICR2 bit 4 clear, no interrupt line) before every step, so the idle handler runs in the state in which the ROM engine's `machine_run` returns `MACHINE_IDLE`. The host pushes the machine's A first (isr_idle's `PHA`) and pulls it after; on a stop it stays pushed, the PC at $C603 and the CPU waiting, as the ROM's isr_idle is left.
- **The delivery guard**: no replacement starts at $C602 or $C603 (the 110 rows, and the 21 of this plan), so `machine_wait_interrupt` never ran there; it now refuses both PCs before anything runs.
- **The CPU set-up** (`boot_reset` $A625–$A62C: `SEI`, `LDX #$FF`, `TXS`, `CLT`, `CLD`, `CLC`, `CLV`): `cpu740_reset` leaves SP $FF and P = I|B, so at the reset entry it changes nothing; it matters for the ROM code a C main loop runs later (the wait loops, the NUL fallback) and for the interrupts. The unpaired `CLI`/`SEI` of the main loop are $A66E (`boot_reentry_warm`), $A6D3, $A6E7 and $A6FF (`boot_get_clause`); every other mask in boot is a `PHP` / `SEI` … `PLP` pair (`boot_queue_string` $C61A–$C62A, `ser_tx_service` $C599–$C5A0, `isr_par_hold`, `isr_timer_start`, `isr_dsp_reset`), invisible to the C.
- **The engines' two phases**: `rom_engine_run` booted to idle within `ROM_ENGINE_BOOT_CYCLES`, then reset the frame counters, kept the frames, queued the text and a CR and ran `ROM_ENGINE_SPEAK_CYCLES` to the next idle. The engine's idle handler does exactly that at the first idle and stops the run at the second; the loop runs `machine_run` up to `m->phase_deadline`, which the feed moves. With the main loop in ROM every result is as before: the goldens and every suite unchanged in the proof run's full suite. When a feed happens inside a hook (the C's input wait, from Task 5 on) `machine_run` returns `MACHINE_CYCLE_LIMIT` at its own, older budget; the loop then runs on to the moved deadline.
- **The M4.6c1 Review Focus raw inputs** (`test_review_focus`) booted with an empty text (a CR), then fed the raw bytes and ran on with `machine_run`. Past the end of boot only the engine's handler may feed (a C main loop is abandoned at a stop), so they are fed at the end of boot after a CR: without that CR's empty clause the ROM engine's `@A` NUL run stops at $8F04 (opcode $52) instead of $8F02 ($54), since the NUL bug's ROM jump returns through the stack bytes that clause leaves.
- **Parallel mode** (DIP bit 0, the P4 input's bit 0): isr_idle returns at once ($C5FF `BBS0`), so neither engine ever idles: the run ends at the boot phase's deadline (`MACHINE_CYCLE_LIMIT`, 10⁸ cycles) after the 41 frame events of the boot announcement, on both engines (`test_engine_loop`).
- **The phase deadline in the waits** (spec §3.2): under an outermost entry row a wait is budgeted to the phase deadline instead of `FW_ROM_CALL_BUDGET` (the ROM engine's loops spin until `machine_run`'s budget, not 10⁷ cycles), and a wait that reaches it stops the run (`FW_WAIT_STOPPED`) with the machine at the wait's ROM loop (`at`): `exec` faults a hook that returns with the PC at its own address (its zero-cycle spin guard), which is where a failed wait leaves it.
- **The host wait's result**: the hybrid host had a `bool` wait; a stop is a third outcome, so the host returns `fw_wait_result` (`FW_WAIT_FAILED` = 0, `FW_WAIT_RETURNED`, `FW_WAIT_STOPPED`). The four test stubs that implement `wait` (`test_fw_dsp.c`, `test_fw_par.c`, `test_fw_ser.c`, `test_fw_tn.c`) return it.
- **The spec gives `fw_set_interrupts(f, on)`**: a fault must name the ROM instruction, so it takes the address too: `fw_set_interrupts(f, on, at)` ($A66E, $A6D3, $A6E7, $A6FF, $FFD0). `fw_reset_cpu` has one instruction to name, $A625.

**Files:**
- Create: `tests/test_fw_boot.c`
- Modify: `src/machine/machine.h`, `src/machine/machine.c`, `src/firmware/fw.h`, `src/firmware/fw.c`, `src/engine/thunks.h`, `src/engine/thunks.c`, `src/engine/rom_engine.h`, `src/engine/rom_engine.c`, `src/engine/hybrid_engine.h`, `src/engine/hybrid_engine.c`, `tests/suites.h`, `tests/test_machine_hooks.c`, `tests/test_golden_hybrid.c`, `tests/test_fw_dsp.c`, `tests/test_fw_par.c`, `tests/test_fw_ser.c`, `tests/test_fw_tn.c`

**Interfaces:**
- Consumes: `machine_run`, `prepare`, `exec`, `at_idle` and `machine_wait_interrupt` (`machine.c`); `fault`, `fw_wait` and `fw_enter` (`fw.c`); `rom_call`, `host_wait`, `sync_p2`, `run_frame` and `fw_bridge_init` (`thunks.c`); `rom_engine_run`, `on_frame` and `frame_sink` (`rom_engine.c`); `hybrid_engine_speak` (`hybrid_engine.c`); `load_main`, `put`, `vector`, `run_to` (`tests/test_machine_hooks.c`); `run_raw`, `check_same` and `outcome` (`tests/test_golden_hybrid.c`).
- Produces (Tasks 3–6 use them):

```c
/* src/machine/machine.h */
typedef enum { MACHINE_STOP = 0, MACHINE_FEED } machine_idle_action;
typedef machine_idle_action (*machine_idle_fn)(struct machine *m, void *user);
typedef enum { MINPUT_FAILED = 0, MINPUT_HANDLED, MINPUT_STOPPED } machine_input_result;
machine: + machine_idle_fn idle_fn; void *idle_user; bool idle_stopped; uint64_t phase_deadline;
void machine_set_idle_handler(machine *m, machine_idle_fn fn, void *user);
machine_idle_action machine_idle(machine *m);                         /* MACHINE_STOP once stopped or with none */
machine_input_result machine_wait_input(machine *m, uint64_t max_cycles);
/* src/engine/rom_engine.h */
typedef struct { const uint8_t *bytes; size_t n; bool cr; } engine_input;
typedef struct { void (*on_frame)(void *user, machine *m); void (*on_idle)(void *user, machine *m); void *user; }
    engine_watch;
machine_status rom_engine_run_input(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                    engine_setup_fn setup, void *user, const engine_input *in, bool include_boot,
                                    frame_list *produced, frame_list *raw, const engine_watch *watch);
/* src/engine/hybrid_engine.h */
machine_status hybrid_engine_run_input(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                       const char *only, const engine_input *in, bool include_boot,
                                       frame_list *produced, frame_list *raw, hybrid_stats *stats,
                                       const engine_watch *watch);
/* src/firmware/fw.h */
fw_wait_kind: + FW_WAIT_INPUT;   #define FW_WAIT_INPUT_LOOP 0xC602, FW_RESET_CPU 0xA625
typedef enum { FW_WAIT_FAILED = 0, FW_WAIT_RETURNED, FW_WAIT_STOPPED } fw_wait_result;
fw_host: fw_wait_result (*wait)(...); + void (*reset_cpu)(void *ctx); + void (*set_interrupts)(void *ctx, bool enabled);
fw: + bool stopped;
void fw_reset_cpu(fw *f);                                 /* faults at $A625 */
void fw_set_interrupts(fw *f, bool enabled, uint16_t at); /* faults at at */
/* src/engine/thunks.h */
fw_bridge: + uint64_t stops; bool phase;   fw_replacement: + bool entry;
/* src/engine/thunks.c: frame_kind (FRAME_CALL, FRAME_ISR, FRAME_ENTRY) for run_frame; wait_ended(b, at) */
/* tests/test_fw_boot.c: the stub host (the ordered g_log of EV_READ / EV_WRITE / EV_WAIT / EV_RESET / EV_INT,
   g_wait_result), setup(), check_log(); suite_fw_boot, the 39th suite */
```

No name collides: `grep -rn "machine_set_idle_handler\|machine_wait_input\|phase_deadline\|MINPUT_\|MACHINE_FEED\|MACHINE_STOP\|FW_WAIT_INPUT\|fw_wait_result\|reset_cpu\|set_interrupts\|engine_input\|engine_watch\|run_input\|FRAME_ENTRY\|suite_fw_boot" src tests` prints nothing before this task.

- [ ] **Step 1: Write the failing tests**

`tests/test_fw_boot.c` starts the suite with the helpers' tests (`fw_wait` with `FW_WAIT_INPUT` and a stopped wait, `fw_reset_cpu`, `fw_set_interrupts`) on a stub host that logs every host call in order; Tasks 4–6 add the routines' tests to it. `test_machine_hooks.c` gains the input wait on the machine and through the bridge (feed, stop, the budget, the I flag), the delivery guard, the phase deadline in a wait and the CPU calls. `test_golden_hybrid.c` runs both engines through the idle handler (`test_engine_loop`) and feeds the M4.6c1 Review Focus inputs through it. The four wait stubs return `fw_wait_result`.

**Create `tests/test_fw_boot.c`:**

```c
/* Unit checks of boot, the main loop and isr_idle (M4.6c2 spec 4.5), with a stub host that logs, in order, every SFR
   read and write, every wait (its ROM loop and kind), the CPU set-up (reset_cpu) and every CLI or SEI
   (set_interrupts), and fails every ROM call (the C makes none). Each expected value is worked out from the dump of the
   routine (build/ghidra740/dump), as the comment beside it says. The full proof is the live differential tests
   (diff_main, diff_lts) and the hybrid goldens (golden_hybrid). */
#include <string.h>
#include "test.h"
#include "firmware/fw.h"
#include "firmware/data/fw_data.h"

#define MAX_LOG 512

typedef enum { EV_READ, EV_WRITE, EV_WAIT, EV_RESET, EV_INT } ev_kind;
/* One host call: EV_READ/EV_WRITE an SFR (addr) and its value; EV_WAIT the ROM loop (addr) and the kind (value);
   EV_RESET reset_cpu (addr FW_RESET_CPU); EV_INT set_interrupts, value 1 for CLI, 0 for SEI. */
typedef struct { ev_kind kind; uint16_t addr; uint8_t value; } event;

static uint8_t g_ram[0x10000];   /* the firmware's RAM is $0000-$3FFF; 64 KiB so no index can run off the array */
static fw g_f;
static fw_host g_host;
static uint8_t g_sfr[0x100];     /* the stub's SFRs: a read returns g_sfr[addr], a write sets it */
static event g_log[MAX_LOG];
static unsigned g_nlog, g_ncalls;
static fw_wait_result g_wait_result;   /* what the stub's wait returns */

static void log_add(ev_kind kind, uint16_t addr, uint8_t value)
{
    if (g_nlog < MAX_LOG)
        g_log[g_nlog] = (event){ kind, addr, value };
    g_nlog++;
}

static uint8_t stub_read_sfr(void *ctx, uint8_t addr)
{
    (void)ctx;
    log_add(EV_READ, addr, g_sfr[addr]);
    return g_sfr[addr];
}

static void stub_write_sfr(void *ctx, uint8_t addr, uint8_t value)
{
    (void)ctx;
    g_sfr[addr] = value;
    log_add(EV_WRITE, addr, value);
}

static fw_wait_result stub_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    (void)ctx;
    log_add(EV_WAIT, at, (uint8_t)kind);
    return g_wait_result;
}

static void stub_reset_cpu(void *ctx)
{
    (void)ctx;
    log_add(EV_RESET, FW_RESET_CPU, 0);
}

static void stub_set_interrupts(void *ctx, bool enabled)
{
    (void)ctx;
    log_add(EV_INT, 0, enabled);
}

/* Counts and fails every ROM call: the C makes none. */
static fw_call_result stub_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    (void)ctx;
    (void)addr;
    (void)r;
    (void)budget;
    g_ncalls++;
    return FW_CALL_FAILED;
}

/* Zeroed RAM and SFRs, the stub host, the converted image, nothing logged, every wait returning. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    memset(g_sfr, 0, sizeof g_sfr);
    memset(&g_host, 0, sizeof g_host);
    g_host.ctx = &g_f;
    g_host.rom_call = stub_rom_call;
    g_host.read_sfr = stub_read_sfr;
    g_host.write_sfr = stub_write_sfr;
    g_host.wait = stub_wait;
    g_host.reset_cpu = stub_reset_cpu;
    g_host.set_interrupts = stub_set_interrupts;
    g_nlog = 0;
    g_ncalls = 0;
    g_wait_result = FW_WAIT_RETURNED;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
}

/* The log is exactly the n events of want, and no ROM call was made. */
static void check_log(const event *want, unsigned n)
{
    CHECK_EQ(g_nlog, n);
    for (unsigned i = 0; i < n && i < g_nlog && i < MAX_LOG; i++) {
        CHECK_EQ(g_log[i].kind, want[i].kind);
        CHECK_EQ(g_log[i].addr, want[i].addr);
        CHECK_EQ(g_log[i].value, want[i].value);
    }
    CHECK_EQ(g_ncalls, 0);
}

/* fw_wait with FW_WAIT_INPUT (M4.6c2 spec 3.1): the host gets the kind and isr_idle's WIT $C602. A wait that returns
   leaves the firmware as it was; a stopped one (any kind: the idle handler, or an entry row's phase deadline) sets
   f->stopped and is not a fault; a failed one, a host without wait or no host is a fault at the loop. */
static void test_wait_input(void)
{
    static const event want[] = { { EV_WAIT, FW_WAIT_INPUT_LOOP, FW_WAIT_INPUT } };
    static const event want_irq[] = { { EV_WAIT, 0xA6C2, FW_WAIT_INTERRUPT } };
    setup();
    fw_wait(&g_f, FW_WAIT_INPUT, FW_WAIT_INPUT_LOOP);
    check_log(want, 1);
    CHECK(!g_f.stopped);
    CHECK(!g_f.fault);

    setup();
    g_wait_result = FW_WAIT_STOPPED;
    fw_wait(&g_f, FW_WAIT_INPUT, FW_WAIT_INPUT_LOOP);
    check_log(want, 1);
    CHECK(g_f.stopped);
    CHECK(!g_f.fault);
    CHECK(!g_f.unwound);

    setup();
    g_wait_result = FW_WAIT_STOPPED;
    fw_wait(&g_f, FW_WAIT_INTERRUPT, 0xA6C2);
    check_log(want_irq, 1);
    CHECK(g_f.stopped);
    CHECK(!g_f.fault);

    setup();
    g_wait_result = FW_WAIT_FAILED;
    fw_wait(&g_f, FW_WAIT_INPUT, FW_WAIT_INPUT_LOOP);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xC602);
    CHECK(!g_f.stopped);

    setup();
    g_host.wait = NULL;
    fw_wait(&g_f, FW_WAIT_INPUT, FW_WAIT_INPUT_LOOP);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xC602);
    CHECK_EQ(g_nlog, 0);

    fw f;
    fw_init(&f, g_ram, fw_image_get(), NULL);
    CHECK(!f.stopped);
    fw_wait(&f, FW_WAIT_INPUT, FW_WAIT_INPUT_LOOP);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xC602);
}

/* fw_reset_cpu and fw_set_interrupts (M4.6c2 spec 3.3): one host call each. Without the host call, or without a
   host, a fault: fw_reset_cpu at boot_reset's SEI $A625, fw_set_interrupts at the address given. */
static void test_cpu_calls(void)
{
    static const event want[] = { { EV_RESET, FW_RESET_CPU, 0 }, { EV_INT, 0, 1 }, { EV_INT, 0, 0 } };
    setup();
    fw_reset_cpu(&g_f);
    fw_set_interrupts(&g_f, true, 0xA66E);
    fw_set_interrupts(&g_f, false, 0xA6E7);
    check_log(want, 3);
    CHECK(!g_f.fault);

    setup();
    g_host.reset_cpu = NULL;
    fw_reset_cpu(&g_f);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xA625);
    CHECK_EQ(g_nlog, 0);

    setup();
    g_host.set_interrupts = NULL;
    fw_set_interrupts(&g_f, true, 0xA6D3);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xA6D3);
    CHECK_EQ(g_nlog, 0);

    fw f;
    fw_init(&f, g_ram, fw_image_get(), NULL);
    fw_reset_cpu(&f);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xA625);
    fw_init(&f, g_ram, fw_image_get(), NULL);
    fw_set_interrupts(&f, false, 0xA6E7);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xA6E7);
}

void suite_fw_boot(void)
{
    CHECK(fw_image_get() != NULL);
    test_wait_input();
    test_cpu_calls();
}
```

**`tests/suites.h`**: replace

```c
SUITE(suite_fw_dsp)
SUITE(suite_fw_bank)
```

with

```c
SUITE(suite_fw_dsp)
SUITE(suite_fw_bank)
SUITE(suite_fw_boot)
```

**`tests/test_machine_hooks.c`**: replace

```c
}

/* The bridge's DSP port and frame event (M4.6b spec 3.1): fw_dsp_write is a bus write of $8000 (counted, grouped
   into the raw frame), fw_frame_done the machine's frame capture. */
```

with

```c
}

#define SYN_RX_ISR  0xC680   /* the synthetic receive interrupt handler */

static unsigned g_idles;

/* An idle handler that feeds 'A' at its first call and stops the run at every later one. */
static machine_idle_action idle_feed_once(machine *m, void *user)
{
    static const uint8_t byte[] = { 'A' };
    (void)user;
    if (g_idles++ > 0)
        return MACHINE_STOP;
    CHECK(machine_feed_serial(m, byte, sizeof byte));
    return MACHINE_FEED;
}

/* $9000 as load_main, the UART's receive interrupt enabled (UART_CTRL bit 4) and its handler at $C680 (PHA /
   LDA UART_RX / STA $21 / INC $22 / PLA / RTI) on vec_int1_serial $FFFA, I clear. */
static void load_rx(void)
{
    static const uint8_t rx_isr[] = { 0x48, 0xA5, 0xE5, 0x85, 0x21, 0xE6, 0x22, 0x68, 0x40 };
    load_main(0xC100);
    put(SYN_RX_ISR, rx_isr, sizeof rx_isr);
    vector(0xFFFA, SYN_RX_ISR);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    g_m.bus.write(g_m.bus.ctx, M50734_UART_CTRL, 0x10);
    g_m.cpu.p = 0;
    g_idles = 0;
}

/* machine_wait_input (M4.6c2 spec 3.1): the CPU waits at the idle point $C603. With nothing left to do there the idle
   handler runs: it feeds a byte, which is delivered there, and the receive interrupt's handler runs; the wait returns
   with the PC and SP back. At the next idle the handler stops the run: the machine stays at the idle point (waiting),
   machine_run returns MACHINE_IDLE there, and the handler is not called again. A machine that never idles (timer 3
   enabled) and takes no interrupt fails after its budget; a line that wakes the CPU with I set fails at once. A clone
   has no idle handler. */
static void test_wait_input(void)
{
    static const uint8_t byte[] = { 'B' };
    load_rx();
    machine_set_idle_handler(&g_m, idle_feed_once, NULL);
    CHECK_EQ(machine_wait_input(&g_m, 10000), MINPUT_HANDLED);
    CHECK_EQ(g_idles, 1);
    CHECK_EQ(g_m.ram[0x21], 'A');
    CHECK_EQ(g_m.ram[0x22], 1);
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    CHECK(!g_m.cpu.waiting);
    machine_clone(&g_c, &g_m);
    CHECK(g_c.idle_fn == NULL);

    CHECK_EQ(machine_wait_input(&g_m, 10000), MINPUT_STOPPED);
    CHECK_EQ(g_idles, 2);
    CHECK_EQ(g_m.cpu.pc, 0xC603);
    CHECK(g_m.cpu.waiting);
    CHECK_EQ(machine_run(&g_m, 1000), MACHINE_IDLE);
    CHECK_EQ(machine_idle(&g_m), MACHINE_STOP);
    CHECK_EQ(g_idles, 2);

    load_rx();
    machine_set_idle_handler(&g_m, idle_feed_once, NULL);
    g_m.sfr.icr2 = 0x10;                         /* timer 3 enabled (not running): never idle */
    uint64_t c0 = g_m.cpu.cycles;
    CHECK_EQ(machine_wait_input(&g_m, 1000), MINPUT_FAILED);
    CHECK(g_m.cpu.cycles - c0 >= 1000);
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK(!g_m.cpu.waiting);
    CHECK_EQ(g_idles, 0);

    g_m.sfr.icr2 = 0;
    CHECK(machine_feed_serial(&g_m, byte, sizeof byte));   /* delivered at the idle point: the line wakes the CPU */
    g_m.cpu.p = CPU740_I;
    c0 = g_m.cpu.cycles;
    CHECK_EQ(machine_wait_input(&g_m, 1000), MINPUT_FAILED);
    CHECK_EQ(g_m.cpu.cycles, c0);
    CHECK(g_m.sfr.uart_rx_full);
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK(!g_m.cpu.waiting);
    CHECK_EQ(g_m.fault, MFAULT_NONE);
}

/* The delivery guard (M4.6c2 spec 3.1, the M4.6b deferred item): machine_wait_interrupt refuses the idle WIT $C602
   and the idle point $C603, running nothing, so a byte in the serial queue is never delivered by it. */
static void test_delivery_guard(void)
{
    static const uint8_t byte[] = { 'C' };
    load_rx();
    CHECK(machine_feed_serial(&g_m, byte, sizeof byte));
    for (uint16_t pc = 0xC602; pc <= 0xC603; pc++) {
        uint64_t c0 = g_m.cpu.cycles;
        g_m.cpu.pc = pc;
        CHECK(!machine_wait_interrupt(&g_m, 1000));
        CHECK_EQ(g_m.cpu.cycles, c0);
        CHECK_EQ(g_m.cpu.pc, pc);
        CHECK(!g_m.cpu.waiting);
        CHECK(!g_m.sfr.uart_rx_full);
    }
}

/* The bridge's input wait (M4.6c2 spec 3.1): isr_idle's PHA of the machine's A, the wait at the idle point, the PLA:
   SP and the PC back. Stopped: f->stopped, no fault, the machine at the idle point with A pushed, as the ROM's isr_idle
   leaves it. Under an outermost entry row (b->phase) a wait lasts until the engine's phase deadline, past
   FW_ROM_CALL_BUDGET, and one that reaches it stops the run (no fault; M4.6c2 spec 3.2), the machine left at the wait's
   loop (not waiting); so does one made after it has passed. Without phase, the bridge deadline is a fault as before.
   The CPU calls: reset_cpu sets I and SP $FF and clears T, D, C and V; set_interrupts clears or sets I. */
static void test_host_input(void)
{
    static fw_bridge b;
    load_rx();
    CHECK(fw_bridge_init(&b, &g_m));
    machine_set_idle_handler(&g_m, idle_feed_once, NULL);
    g_m.cpu.a = 0x5A;
    fw_wait(&b.f, FW_WAIT_INPUT, FW_WAIT_INPUT_LOOP);
    CHECK(!b.f.fault);
    CHECK(!b.f.stopped);
    CHECK_EQ(g_m.ram[0x21], 'A');
    CHECK_EQ(g_m.ram[0x1FF], 0x5A);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    fw_wait(&b.f, FW_WAIT_INPUT, FW_WAIT_INPUT_LOOP);
    CHECK(b.f.stopped);
    CHECK(!b.f.fault);
    CHECK_EQ(g_m.cpu.pc, 0xC603);
    CHECK(g_m.cpu.waiting);
    CHECK_EQ(g_m.cpu.s, 0xFE);
    CHECK_EQ(g_m.ram[0x1FF], 0x5A);

    load_rx();
    CHECK(fw_bridge_init(&b, &g_m));
    b.phase = true;
    g_m.phase_deadline = g_m.cpu.cycles + 2 * FW_ROM_CALL_BUDGET;   /* no interrupt comes: the deadline ends the wait */
    fw_wait(&b.f, FW_WAIT_INTERRUPT, 0xA6C2);
    CHECK(b.f.stopped);
    CHECK(!b.f.fault);
    CHECK(g_m.cpu.cycles >= g_m.phase_deadline);
    CHECK(g_m.cpu.cycles < g_m.phase_deadline + 8);
    CHECK_EQ(g_m.cpu.pc, 0xA6C2);
    CHECK(!g_m.cpu.waiting);
    b.f.stopped = false;
    g_m.cpu.pc = 0x9000;
    uint64_t c0 = g_m.cpu.cycles;
    fw_wait(&b.f, FW_WAIT_INTERRUPT, 0xA6C2);   /* passed: stopped without running */
    CHECK(b.f.stopped);
    CHECK_EQ(g_m.cpu.cycles, c0);
    CHECK_EQ(g_m.cpu.pc, 0xA6C2);
    g_m.cpu.pc = 0x9000;
    b.f.stopped = false;
    b.phase = false;
    b.deadline = g_m.cpu.cycles + 500;
    fw_wait(&b.f, FW_WAIT_INTERRUPT, 0xA6C2);
    CHECK(b.f.fault);
    CHECK_EQ(b.f.fault_addr, 0xA6C2);
    CHECK(!b.f.stopped);

    load_main(0xC100);
    CHECK(fw_bridge_init(&b, &g_m));
    g_m.cpu.p = 0xFF & ~CPU740_I;
    g_m.cpu.s = 0x80;
    fw_reset_cpu(&b.f);
    CHECK_EQ(g_m.cpu.p, CPU740_N | CPU740_B | CPU740_I | CPU740_Z);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    fw_set_interrupts(&b.f, true, 0xA66E);
    CHECK_EQ(g_m.cpu.p, CPU740_N | CPU740_B | CPU740_Z);
    fw_set_interrupts(&b.f, false, 0xA6E7);
    CHECK_EQ(g_m.cpu.p, CPU740_N | CPU740_B | CPU740_I | CPU740_Z);
    CHECK(!b.f.fault);
}

/* The bridge's DSP port and frame event (M4.6b spec 3.1): fw_dsp_write is a bus write of $8000 (counted, grouped
   into the raw frame), fw_frame_done the machine's frame capture. */
```

**`tests/test_machine_hooks.c`**: replace

```c
    test_host_bank();
    test_host_readback();
}
```

with

```c
    test_host_bank();
    test_host_readback();
    test_wait_input();
    test_delivery_guard();
    test_host_input();
}
```

**`tests/test_golden_hybrid.c`**: replace

```c
   spec 5.3, M4.6c1 spec 4.5). A routine no corpus text enters (diff_probed, M4.6a spec 4.4) runs no thunk alone;
   diff_serial compares it. Also the fault path of a thunk: a firmware fault stops the run instead of continuing on
   wrong data; the C entering a bank; and the M4.6c1 plan's Review Focus inputs on both engines (test_review_focus). */
#include <stdio.h>
#include <stdlib.h>
```

with

```c
   spec 5.3, M4.6c1 spec 4.5). A routine no corpus text enters (diff_probed, M4.6a spec 4.4) runs no thunk alone;
   diff_serial compares it. Also the fault path of a thunk: a firmware fault stops the run instead of continuing on
   wrong data; the C entering a bank; both engines' loop through the idle handler and the phase deadline (M4.6c2 spec
   3.1, 3.2; test_engine_loop); and the M4.6c1 plan's Review Focus inputs on both engines (test_review_focus). */
#include <stdio.h>
#include <stdlib.h>
```

**`tests/test_golden_hybrid.c`**: replace

```c
} outcome;

static fw_bridge g_raw_bridge;
static frame_list g_raw_frames;

static void install_all(machine *m, void *user)
{
    (void)user;
    CHECK(fw_bridge_init(&g_raw_bridge, m) && fw_install(m, &g_raw_bridge, NULL));
}

static void collect(void *user, const uint8_t f[APOLLO_FRAME_BYTES])
{
    frame_list *fl = user;
    if (fl->count == fl->cap) {
        fl->cap = fl->cap ? fl->cap * 2 : 256;
        fl->frames = realloc(fl->frames, fl->cap * sizeof fl->frames[0]);
        if (!fl->frames)
            abort();
    }
    memcpy(fl->frames[fl->count++], f, APOLLO_FRAME_BYTES);
}

/* One engine (hybrid: every replacement) booted to idle, then the n raw bytes of in fed to the serial input (a NUL
   included: no CLI text can hold one) and run to idle, a fault or the speaking budget. */
static void run_raw(const uint8_t *rom, size_t len, bool hybrid, const char *in, size_t n, outcome *o)
{
    frame_list boot = { 0 };
    memset(o, 0, sizeof *o);
    memset(&g_raw_bridge, 0, sizeof g_raw_bridge);
    o->st = rom_engine_run(&g_m, rom, len, NULL, hybrid ? install_all : NULL, NULL, "", false, &boot, NULL);
    frame_list_free(&boot);
    CHECK_EQ(o->st, MACHINE_IDLE);
    machine_set_outputs(&g_m, collect, &g_raw_frames);
    CHECK(machine_feed_serial(&g_m, (const uint8_t *)in, n));
    o->st = machine_run(&g_m, ROM_ENGINE_SPEAK_CYCLES);
    machine_set_outputs(&g_m, NULL, NULL);
    machine_clear_hooks(&g_m);
    o->fault = g_m.fault;
    o->fault_addr = g_m.fault_addr;
    o->cpu_fault = (uint8_t)g_m.cpu.fault;
    o->opcode = g_m.cpu.fault_opcode;
    o->cpu_fault_pc = g_m.cpu.fault_pc;
    o->frames = frame_list_hex(&g_raw_frames);
    frame_list_free(&g_raw_frames);
    uint16_t rd = (uint16_t)(g_m.ram[SER_TX_READ] | g_m.ram[SER_TX_READ + 1] << 8);
    uint16_t wr = (uint16_t)(g_m.ram[SER_TX_WRITE] | g_m.ram[SER_TX_WRITE + 1] << 8);
```

with

```c
} outcome;

/* One engine (hybrid: every replacement) fed a CR, then the n raw bytes of in (a NUL included: no CLI text can hold
   one), at the end of boot through the engine's own idle handler (M4.6c2 spec 3.1), and run to the next idle, a fault
   or the speaking budget; the frames from the end of boot on. The CR's empty clause first, as before M4.6c2 (a boot
   run fed it, then the raw bytes followed): the NUL bug's ROM jumps return through what it left on the stack. */
static void run_raw(const uint8_t *rom, size_t len, bool hybrid, const char *in, size_t n, outcome *o)
{
    frame_list fl = { 0 };
    hybrid_stats stats = { 0, 0, 0 };
    uint8_t bytes[256] = { '\r' };
    CHECK(n < sizeof bytes);
    if (n >= sizeof bytes)
        n = sizeof bytes - 1;
    memcpy(bytes + 1, in, n);
    engine_input input = { bytes, n + 1, false };
    memset(o, 0, sizeof *o);
    o->st = hybrid ? hybrid_engine_run_input(&g_m, rom, len, NULL, NULL, &input, false, &fl, NULL, &stats, NULL)
                   : rom_engine_run_input(&g_m, rom, len, NULL, NULL, NULL, &input, false, &fl, NULL, NULL);
    o->fault = g_m.fault;
    o->fault_addr = g_m.fault_addr;
    o->cpu_fault = (uint8_t)g_m.cpu.fault;
    o->opcode = g_m.cpu.fault_opcode;
    o->cpu_fault_pc = g_m.cpu.fault_pc;
    o->frames = frame_list_hex(&fl);
    frame_list_free(&fl);
    uint16_t rd = (uint16_t)(g_m.ram[SER_TX_READ] | g_m.ram[SER_TX_READ + 1] << 8);
    uint16_t wr = (uint16_t)(g_m.ram[SER_TX_WRITE] | g_m.ram[SER_TX_WRITE + 1] << 8);
```

**`tests/test_golden_hybrid.c`**: replace

```c
    o->bank_current = g_m.ram[BANK_CURRENT];
    memcpy(o->bank_ids, g_m.ram + BANK_ID_TABLE, sizeof o->bank_ids);
    o->unwinds = (unsigned)g_raw_bridge.unwinds;
}

```

with

```c
    o->bank_current = g_m.ram[BANK_CURRENT];
    memcpy(o->bank_ids, g_m.ram + BANK_ID_TABLE, sizeof o->bank_ids);
    o->unwinds = stats.unwinds;
}

```

**`tests/test_golden_hybrid.c`**: replace

```c
}

void suite_golden_hybrid(void)
{
```

with

```c
}

/* ---- the engines' loop (M4.6c2 spec 3.1, 3.2) ---- */

typedef struct { unsigned idles, frames, frames_at_idle[4]; } engine_count;

static void count_frame(void *user, machine *m)
{
    (void)m;
    ((engine_count *)user)->frames++;
}

static void count_idle(void *user, machine *m)
{
    engine_count *c = user;
    (void)m;
    if (c->idles < 4)
        c->frames_at_idle[c->idles] = c->frames;
    c->idles++;
}

/* One engine (hybrid: every replacement) on in with a watch counting its events. */
static machine_status run_counted(const uint8_t *rom, size_t len, const machine_config *cfg, bool hybrid,
                                  const char *in, engine_count *c, frame_list *fl)
{
    engine_watch w = { count_frame, count_idle, c };
    engine_input input = { (const uint8_t *)in, strlen(in), true };
    memset(c, 0, sizeof *c);
    return hybrid ? hybrid_engine_run_input(&g_m, rom, len, cfg, NULL, &input, false, fl, NULL, NULL, &w)
                  : rom_engine_run_input(&g_m, rom, len, cfg, NULL, NULL, &input, false, fl, NULL, &w);
}

/* Both engines run through the idle handler: on hello each idles twice (the end of boot, the end of the text) and
   runs on from the first, the boot's frame events before it (the self-test sound) and the text's after it, the
   frames kept from the first idle on. In parallel mode (DIP bit 0: isr_idle returns at once) neither idles: the run
   ends at the boot phase's deadline, MACHINE_CYCLE_LIMIT, after the boot's frames. */
static void test_engine_loop(const uint8_t *rom, size_t len)
{
    for (int hybrid = 0; hybrid <= 1; hybrid++) {
        engine_count c;
        frame_list fl = { 0 };
        CHECK_EQ(run_counted(rom, len, NULL, hybrid, "hello", &c, &fl), MACHINE_IDLE);
        CHECK_EQ(c.idles, 2);
        CHECK_EQ(c.frames_at_idle[0], 41);
        CHECK_EQ(c.frames_at_idle[1], c.frames);
        CHECK_EQ(fl.count, c.frames - 41);
        CHECK(fl.count > 0);
        CHECK_EQ(g_m.frames_produced, fl.count);
        frame_list_free(&fl);

        machine_config cfg;
        machine_default_config(&cfg);
        cfg.port_in[4] |= 1;   /* DIP bit 0: the parallel interface */
        CHECK_EQ(run_counted(rom, len, &cfg, hybrid, "hello", &c, &fl), MACHINE_CYCLE_LIMIT);
        CHECK_EQ(c.idles, 0);
        CHECK_EQ(c.frames, 41);
        CHECK_EQ(fl.count, 0);
        CHECK(g_m.cpu.cycles >= ROM_ENGINE_BOOT_CYCLES);
        CHECK_EQ(g_m.fault, MFAULT_NONE);
        frame_list_free(&fl);
    }
}

void suite_golden_hybrid(void)
{
```

**`tests/test_golden_hybrid.c`**: replace

```c
        SKIP("ROM not found (set APOLLO2_ROM)");
    test_thunk_faults(rom, len);
    test_review_focus(rom, len);
    check_corpus(rom, len, NULL);
```

with

```c
        SKIP("ROM not found (set APOLLO2_ROM)");
    test_thunk_faults(rom, len);
    test_engine_loop(rom, len);
    test_review_focus(rom, len);
    check_corpus(rom, len, NULL);
```

**`tests/test_fw_dsp.c`**: replace

```c
}

static bool stub_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    (void)ctx;
    log_add(EV_WAIT, at, (uint8_t)kind);
    if (kind == FW_WAIT_INTERRUPT && g_wait_ok && g_flag_every && ++g_iwaits % g_flag_every == 0)
        g_ram[ISR_FRAME_FLAG] |= 1;
    return g_wait_ok;
}

```

with

```c
}

static fw_wait_result stub_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    (void)ctx;
    log_add(EV_WAIT, at, (uint8_t)kind);
    if (kind == FW_WAIT_INTERRUPT && g_wait_ok && g_flag_every && ++g_iwaits % g_flag_every == 0)
        g_ram[ISR_FRAME_FLAG] |= 1;
    return g_wait_ok ? FW_WAIT_RETURNED : FW_WAIT_FAILED;
}

```

**`tests/test_fw_par.c`**: replace

```c
   the flag ($B306 LDM #1) at every g_flag_every-th, and the receive interrupt sets ser_abort bit 0 (a mute) at the
   g_abort_at-th. */
static bool mock_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    (void)ctx;
```

with

```c
   the flag ($B306 LDM #1) at every g_flag_every-th, and the receive interrupt sets ser_abort bit 0 (a mute) at the
   g_abort_at-th. */
static fw_wait_result mock_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    (void)ctx;
```

**`tests/test_fw_par.c`**: replace

```c
            g_ram[SER_ABORT] |= 1;
    }
    return true;
}

```

with

```c
            g_ram[SER_ABORT] |= 1;
    }
    return FW_WAIT_RETURNED;
}

```

**`tests/test_fw_ser.c`**: replace

```c
}

static bool stub_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    (void)ctx;
    log_add(LOG_WAIT, (uint8_t)kind, 0);
    g_wait_at = at;
    return g_wait_ok;
}

```

with

```c
}

static fw_wait_result stub_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    (void)ctx;
    log_add(LOG_WAIT, (uint8_t)kind, 0);
    g_wait_at = at;
    return g_wait_ok ? FW_WAIT_RETURNED : FW_WAIT_FAILED;
}

```

**`tests/test_fw_tn.c`**: replace

```c
   the DSP starts), so the C boot_loop_play of the mid-clause flush plays no segment: par_play goes from its first
   pass to the three rest frames. */
static bool mock_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    fw *f = ctx;
```

with

```c
   the DSP starts), so the C boot_loop_play of the mid-clause flush plays no segment: par_play goes from its first
   pass to the three rest frames. */
static fw_wait_result mock_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    fw *f = ctx;
```

**`tests/test_fw_tn.c`**: replace

```c
    if (kind == FW_WAIT_TIMER2)
        fw_wr8(f, SER_ABORT, 1);
    return true;
}

```

with

```c
    if (kind == FW_WAIT_TIMER2)
        fw_wr8(f, SER_ABORT, 1);
    return FW_WAIT_RETURNED;
}

```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw_boot"`
Expected: the build stops with compile errors (the proof run, first lines):

```
tests\test_fw_boot.c(24): error C2061: syntax error: identifier 'g_wait_result'
tests\test_fw_boot.c(24): error C2059: syntax error: ';'
tests\test_fw_boot.c(47): error C2061: syntax error: identifier 'stub_wait'
tests\test_fw_boot.c(47): error C2059: syntax error: ';'
```

- [ ] **Step 3: Implement**

The machine's idle handler and input wait, the delivery guard, the firmware's new wait kind and CPU calls, the bridge's input wait, phase deadline and CPU calls, and both engines' loop.

**`src/machine/machine.h`**: replace

```c
typedef struct { uint16_t addr; machine_hook_fn fn; void *user; bool ramcode; } machine_hook_entry;
typedef struct { uint8_t a, x, y, p; } machine_regs;

/* Where a CPU read lands under the current P2 (machine_map_read). */
```

with

```c
typedef struct { uint16_t addr; machine_hook_fn fn; void *user; bool ramcode; } machine_hook_entry;
typedef struct { uint8_t a, x, y, p; } machine_regs;
/* What the idle handler did (M4.6c2 spec 3.1): it queued more input, or the run ends. */
typedef enum { MACHINE_STOP = 0, MACHINE_FEED } machine_idle_action;
/* The idle handler: called when the machine has nothing left to do (machine_idle). */
typedef machine_idle_action (*machine_idle_fn)(struct machine *m, void *user);
/* How machine_wait_input ended. */
typedef enum { MINPUT_FAILED = 0, MINPUT_HANDLED, MINPUT_STOPPED } machine_input_result;

/* Where a CPU read lands under the current P2 (machine_map_read). */
```

**`src/machine/machine.h`**: replace

```c
    bool skip_hook;                        /* machine_rom_call_unhooked: the hook at skip_hook_pc does not fire ... */
    uint16_t skip_hook_pc;                 /* ... until the instruction there has run once */
    uint16_t fault_addr;                   /* MFAULT_FIRMWARE: the address the C code could not read or call */
} machine;
```

with

```c
    bool skip_hook;                        /* machine_rom_call_unhooked: the hook at skip_hook_pc does not fire ... */
    uint16_t skip_hook_pc;                 /* ... until the instruction there has run once */
    machine_idle_fn idle_fn;               /* the idle handler (machine_set_idle_handler); NULL: none */
    void *idle_user;
    bool idle_stopped;                     /* it returned MACHINE_STOP: it is not called again */
    uint64_t phase_deadline;               /* the engine's phase deadline (M4.6c2 spec 3.2): the cycle count by which
                                              the run's current phase must end (0: none); the entry rows' thunks run
                                              under it */
    uint16_t fault_addr;                   /* MFAULT_FIRMWARE: the address the C code could not read or call */
} machine;
```

**`src/machine/machine.h`**: replace

```c
void machine_set_raw_output(machine *m, machine_frame_fn on_raw, void *user);
bool machine_feed_serial(machine *m, const uint8_t *bytes, size_t n);
machine_status machine_run(machine *m, uint64_t max_cycles);
/* One instruction, interrupt entry or hooked routine, with serial delivery, IRQ sync and frame capture as in
   machine_run. Returns false on a fault (m->fault says which). */
```

with

```c
void machine_set_raw_output(machine *m, machine_frame_fn on_raw, void *user);
bool machine_feed_serial(machine *m, const uint8_t *bytes, size_t n);
/* Runs until the machine has nothing left to do (MACHINE_IDLE: the CPU waiting at the idle WIT $C602, no queued
   input, no byte in the UART, timer 3 off, no interrupt line), a fault, or max_cycles. */
machine_status machine_run(machine *m, uint64_t max_cycles);
/* Registers the idle handler (M4.6c2 spec 3.1; fn NULL: none) and forgets a stop it returned before. */
void machine_set_idle_handler(machine *m, machine_idle_fn fn, void *user);
/* The machine has nothing left to do: the idle handler queues more input (MACHINE_FEED) or ends the run
   (MACHINE_STOP). Once it has returned MACHINE_STOP, and without a handler, MACHINE_STOP without a call. */
machine_idle_action machine_idle(machine *m);
/* One instruction, interrupt entry or hooked routine, with serial delivery, IRQ sync and frame capture as in
   machine_run. Returns false on a fault (m->fault says which). */
```

**`src/machine/machine.h`**: replace

```c
   they were. The PC is a hook's, never the idle WIT's ($C602): no serial byte is delivered. False when max_cycles
   pass first, on a fault, or when a line wakes the CPU with I set (the interrupt could never be taken); the CPU is
   then no longer waiting, and the PC and SP may be inside the handler (the caller faults: run_frame puts them back). */
bool machine_wait_interrupt(machine *m, uint64_t max_cycles);
/* Runs the ROM loop at loop with I set (inside the caller's PHP/SEI ... PLP; M4.6b spec 3.3) until the PC reaches
   exit, then puts the PC and P back as they were. No stack frame. False when max_cycles pass first or on a fault
   (the PC and P put back all the same). */
bool machine_run_loop(machine *m, uint16_t loop, uint16_t exit, uint64_t max_cycles);
/* Copies src's whole state into dst for a differential run, without hooks (a pending skip of
   machine_rom_call_unhooked included), outputs, coverage or the hold. */
void machine_clone(machine *dst, const machine *src);
/* The shared address mapper (M4.0 spec section 2.2) behind bus reads, machine_peek and the hook check: where a
```

with

```c
   they were. The PC is a hook's, never the idle WIT's ($C602): no serial byte is delivered. False when max_cycles
   pass first, on a fault, or when a line wakes the CPU with I set (the interrupt could never be taken); the CPU is
   then no longer waiting, and the PC and SP may be inside the handler (the caller faults: run_frame puts them back).
   The delivery guard (M4.6c2 spec 3.1): a PC at the idle WIT $C602 or the idle point $C603 is refused (false, nothing
   run), so that serial bytes are delivered at the input wait only. */
bool machine_wait_interrupt(machine *m, uint64_t max_cycles);
/* The input wait (M4.6c2 spec 3.1), isr_idle's WIT at $C602 for the C: the CPU waits at the idle point $C603, where
   serial bytes are delivered as at the ROM's WIT, until an interrupt has been handled (its handler ROM or a hooked
   replacement): MINPUT_HANDLED, the PC back as it was. Whenever the machine has nothing left to do there (machine_run's
   MACHINE_IDLE), machine_idle: on MACHINE_FEED the wait goes on and the byte is delivered; on MACHINE_STOP,
   MINPUT_STOPPED with the machine left at the idle point (the PC $C603, the CPU waiting), where machine_run returns
   MACHINE_IDLE. MINPUT_FAILED when max_cycles pass first, on a fault, or when a line wakes the CPU with I set (the PC
   back, the CPU no longer waiting). */
machine_input_result machine_wait_input(machine *m, uint64_t max_cycles);
/* Runs the ROM loop at loop with I set (inside the caller's PHP/SEI ... PLP; M4.6b spec 3.3) until the PC reaches
   exit, then puts the PC and P back as they were. No stack frame. False when max_cycles pass first or on a fault
   (the PC and P put back all the same). */
bool machine_run_loop(machine *m, uint16_t loop, uint16_t exit, uint64_t max_cycles);
/* Copies src's whole state into dst for a differential run, without hooks (a pending skip of
   machine_rom_call_unhooked included), outputs, coverage, the hold or the idle handler. */
void machine_clone(machine *dst, const machine *src);
/* The shared address mapper (M4.0 spec section 2.2) behind bus reads, machine_peek and the hook check: where a
```

**`src/machine/machine.c`**: replace

```c
}

static const machine_hook_entry *find_hook(const machine *m)
{
```

with

```c
}

/* Nothing left to do (after prepare): at the idle WIT with no queued input, no byte in the UART, timer 3 off and no
   interrupt line. */
static bool idle(const machine *m)
{
    return at_idle(m) && m->q_head == m->q_tail && !m->sfr.uart_rx_full && !(m->sfr.icr2 & ICR2_T3_ENABLE) &&
           !m->cpu.irq_lines;
}

static const machine_hook_entry *find_hook(const machine *m)
{
```

**`src/machine/machine.c`**: replace

```c
    while (m->cpu.cycles < end) {
        prepare(m);
        if (at_idle(m) && m->q_head == m->q_tail && !m->sfr.uart_rx_full &&
            !(m->sfr.icr2 & ICR2_T3_ENABLE) && !m->cpu.irq_lines)
            return MACHINE_IDLE;
        if (!exec(m))
```

with

```c
    while (m->cpu.cycles < end) {
        prepare(m);
        if (idle(m))
            return MACHINE_IDLE;
        if (!exec(m))
```

**`src/machine/machine.c`**: replace

```c
    prepare(m);
    return exec(m);
}

```

with

```c
    prepare(m);
    return exec(m);
}

void machine_set_idle_handler(machine *m, machine_idle_fn fn, void *user)
{
    m->idle_fn = fn;
    m->idle_user = user;
    m->idle_stopped = false;
}

machine_idle_action machine_idle(machine *m)
{
    if (m->idle_stopped || !m->idle_fn)
        return MACHINE_STOP;
    machine_idle_action a = m->idle_fn(m, m->idle_user);
    m->idle_stopped = a == MACHINE_STOP;
    return a;
}

```

**`src/machine/machine.c`**: replace

```c
    uint8_t s = c->s;
    uint64_t end = c->cycles + max_cycles;
    c->waiting = true;   /* a WIT at pc: at_idle is false there, so maybe_deliver delivers nothing */
    while (c->cycles < end) {
```

with

```c
    uint8_t s = c->s;
    uint64_t end = c->cycles + max_cycles;
    if (pc == APOLLO_IDLE_WIT_PC || pc == APOLLO_IDLE_WIT_PC + 1)
        return false;    /* the delivery guard: serial bytes are delivered at the input wait only */
    c->waiting = true;   /* a WIT at pc: at_idle is false there, so maybe_deliver delivers nothing */
    while (c->cycles < end) {
```

**`src/machine/machine.c`**: replace

```c
    c->waiting = false;
    return false;
}

```

with

```c
    c->waiting = false;
    return false;
}

machine_input_result machine_wait_input(machine *m, uint64_t max_cycles)
{
    cpu740 *c = &m->cpu;
    uint16_t pc = c->pc;
    uint8_t s = c->s;
    uint64_t end = c->cycles + max_cycles;
    c->pc = APOLLO_IDLE_WIT_PC + 1;   /* the WIT at $C602 executed: the CPU waits at the idle point */
    c->waiting = true;
    while (c->cycles < end) {
        prepare(m);
        if (idle(m)) {
            if (machine_idle(m) == MACHINE_STOP)
                return MINPUT_STOPPED;    /* left at the idle point */
            continue;                     /* fed: the next prepare delivers a byte */
        }
        if (c->waiting && c->irq_lines && (c->p & CPU740_I))
            break;       /* the CPU would wake and run on at $C603 without taking the interrupt */
        if (!exec(m))
            break;
        if (!c->waiting && c->pc == APOLLO_IDLE_WIT_PC + 1 && c->s == s) {
            c->pc = pc;  /* the handler's RTI is back at the idle point */
            return MINPUT_HANDLED;
        }
    }
    c->waiting = false;
    c->pc = pc;
    return MINPUT_FAILED;
}

```

**`src/machine/machine.c`**: replace

```c
    dst->user = dst->raw_user = NULL;
    dst->irq_hold = false;
    machine_clear_hooks(dst);
}
```

with

```c
    dst->user = dst->raw_user = NULL;
    dst->irq_hold = false;
    machine_set_idle_handler(dst, NULL, NULL);
    machine_clear_hooks(dst);
}
```

**`src/firmware/fw.h`**: replace

```c
    FW_WAIT_TIMER2,        /* isr_timer_start's BBC3 ICR2 (inside SEI): until timer 2's request bit is set */
    FW_WAIT_TIMER3,        /* isr_timer_start's BBC5 ICR2 (inside SEI): until timer 3's request bit is set */
} fw_wait_kind;
#define FW_WAIT_UART_TX_LOOP  0xC500   /* ser_tx_wait: the ROM loop of FW_WAIT_UART_TX (BBC1 $E7,$C500 / RTS) */
#define FW_WAIT_TIMER2_LOOP   0xB372   /* isr_timer_start: BBC3 ICR2,$B372, the loop of FW_WAIT_TIMER2 ... */
#define FW_WAIT_TIMER2_EXIT   0xB375   /* ... and the instruction after it (LDM #$28,T3_RELOAD) */
#define FW_WAIT_TIMER3_LOOP   0xB37F   /* isr_timer_start: BBC5 ICR2,$B37F, the loop of FW_WAIT_TIMER3 ... */
#define FW_WAIT_TIMER3_EXIT   0xB382   /* ... and the instruction after it (LDM #$31,T3_RELOAD) */
#define FW_DSP_DATA           0x8000   /* DSP_DATA, the DSP port (write only): where fw_dsp_write faults */
#define FW_FRAME_EVENT        0xB306   /* isr_swap_buffers: the ROM's point of the frame event, where fw_frame_done
```

with

```c
    FW_WAIT_TIMER2,        /* isr_timer_start's BBC3 ICR2 (inside SEI): until timer 2's request bit is set */
    FW_WAIT_TIMER3,        /* isr_timer_start's BBC5 ICR2 (inside SEI): until timer 3's request bit is set */
    FW_WAIT_INPUT,         /* isr_idle's WIT (M4.6c2 spec 3.1): until an interrupt has been handled, the machine at the
                              idle point, where serial bytes are delivered; there the host's idle handler may stop the
                              run */
} fw_wait_kind;
/* How a wait ended: FW_WAIT_RETURNED when it did; FW_WAIT_FAILED when it did not (a fault); FW_WAIT_STOPPED when the
   run ends there (M4.6c2 spec 3.1, 3.2): the input wait's idle handler stopped it, or an entry row's phase deadline
   passed. */
typedef enum { FW_WAIT_FAILED = 0, FW_WAIT_RETURNED, FW_WAIT_STOPPED } fw_wait_result;
#define FW_WAIT_UART_TX_LOOP  0xC500   /* ser_tx_wait: the ROM loop of FW_WAIT_UART_TX (BBC1 $E7,$C500 / RTS) */
#define FW_WAIT_TIMER2_LOOP   0xB372   /* isr_timer_start: BBC3 ICR2,$B372, the loop of FW_WAIT_TIMER2 ... */
#define FW_WAIT_TIMER2_EXIT   0xB375   /* ... and the instruction after it (LDM #$28,T3_RELOAD) */
#define FW_WAIT_TIMER3_LOOP   0xB37F   /* isr_timer_start: BBC5 ICR2,$B37F, the loop of FW_WAIT_TIMER3 ... */
#define FW_WAIT_TIMER3_EXIT   0xB382   /* ... and the instruction after it (LDM #$31,T3_RELOAD) */
#define FW_WAIT_INPUT_LOOP    0xC602   /* isr_idle: the WIT of FW_WAIT_INPUT, the firmware's only one */
#define FW_RESET_CPU          0xA625   /* boot_reset: the SEI of the CPU set-up, where fw_reset_cpu faults */
#define FW_DSP_DATA           0x8000   /* DSP_DATA, the DSP port (write only): where fw_dsp_write faults */
#define FW_FRAME_EVENT        0xB306   /* isr_swap_buffers: the ROM's point of the frame event, where fw_frame_done
```

**`src/firmware/fw.h`**: replace

```c
    /* Writes an SFR ($DA-$FF) as the CPU's write would (M4.6a spec 3.1). */
    void (*write_sfr)(void *ctx, uint8_t addr, uint8_t value);
    /* Waits for the hardware (M4.6a spec 3.3, M4.6b spec 3.2); at is the ROM loop the wait stands for. False when
       the wait did not end (a fault). */
    bool (*wait)(void *ctx, fw_wait_kind kind, uint16_t at);
    /* Writes a byte to the DSP port DSP_DATA $8000, as isr_timer_dsp's STA DSP_DATA does (M4.6b spec 3.1). */
    void (*dsp_write)(void *ctx, uint8_t byte);
```

with

```c
    /* Writes an SFR ($DA-$FF) as the CPU's write would (M4.6a spec 3.1). */
    void (*write_sfr)(void *ctx, uint8_t addr, uint8_t value);
    /* Waits for the hardware (M4.6a spec 3.3, M4.6b spec 3.2, M4.6c2 spec 3.1); at is the ROM loop the wait stands
       for. */
    fw_wait_result (*wait)(void *ctx, fw_wait_kind kind, uint16_t at);
    /* Writes a byte to the DSP port DSP_DATA $8000, as isr_timer_dsp's STA DSP_DATA does (M4.6b spec 3.1). */
    void (*dsp_write)(void *ctx, uint8_t byte);
```

**`src/firmware/fw.h`**: replace

```c
       ROM cmd_run_next. */
    fw_call_result (*rom_call_unhooked)(void *ctx, uint16_t addr, fw_regs *regs, uint64_t budget);
} fw_host;

```

with

```c
       ROM cmd_run_next. */
    fw_call_result (*rom_call_unhooked)(void *ctx, uint16_t addr, fw_regs *regs, uint64_t budget);
    /* boot_reset's CPU set-up $A625-$A62C (M4.6c2 spec 3.3): SEI, SP = $FF (LDX #$FF / TXS), CLT, CLD, CLC, CLV, so
       that the ROM code run later and the interrupts see the ROM's flags and SP. */
    void (*reset_cpu)(void *ctx);
    /* CLI (enabled) or SEI: the main loop's unpaired ones (M4.6c2 spec 3.3), on which the next wait's interrupts
       depend. */
    void (*set_interrupts)(void *ctx, bool enabled);
} fw_host;

```

**`src/firmware/fw.h`**: replace

```c
    jmp_buf *trap;                 /* when set, a fault longjmps here and the routine is abandoned */
    bool unwound;                  /* a ROM call reset the stack: the C was abandoned (not a fault; M4.2 spec 3) */
    unsigned tn_flushes;           /* mid-clause flushes by tn_convert_clause (a statistic for the tests) */
} fw;
```

with

```c
    jmp_buf *trap;                 /* when set, a fault longjmps here and the routine is abandoned */
    bool unwound;                  /* a ROM call reset the stack: the C was abandoned (not a fault; M4.2 spec 3) */
    bool stopped;                  /* a wait stopped the run: the C was abandoned (not a fault; M4.6c2 spec 3.1) */
    unsigned tn_flushes;           /* mid-clause flushes by tn_convert_clause (a statistic for the tests) */
} fw;
```

**`src/firmware/fw.h`**: replace

```c
/* Waits for the hardware through the host: the polling loop or wait loop at the ROM address at (JSR ser_tx_wait
   $C500 for FW_WAIT_UART_TX; the polling loop's own address for FW_WAIT_INTERRUPT; $B372 and $B37F for the timer
   waits). Without a host or wait, or when the wait fails, a fault at at. */
void fw_wait(fw *f, fw_wait_kind kind, uint16_t at);
/* STA DSP_DATA: v written to the DSP port through the host. Without a host or dsp_write, a fault at FW_DSP_DATA. */
```

with

```c
/* Waits for the hardware through the host: the polling loop or wait loop at the ROM address at (JSR ser_tx_wait
   $C500 for FW_WAIT_UART_TX; the polling loop's own address for FW_WAIT_INTERRUPT; $B372 and $B37F for the timer
   waits; isr_idle's WIT $C602 for FW_WAIT_INPUT). Without a host or wait, or when the wait fails, a fault at at.
   When it stops the run, f->stopped is set and the routine is abandoned like an unwound ROM call (a longjmp to
   f->trap when set), but it is not a fault (M4.6c2 spec 3.1). */
void fw_wait(fw *f, fw_wait_kind kind, uint16_t at);
/* STA DSP_DATA: v written to the DSP port through the host. Without a host or dsp_write, a fault at FW_DSP_DATA. */
```

**`src/firmware/fw.h`**: replace

```c
   is not a fault). Without a host or enter, a fault at addr. */
void fw_enter(fw *f, uint16_t addr, uint8_t a, uint8_t x);

#endif
```

with

```c
   is not a fault). Without a host or enter, a fault at addr. */
void fw_enter(fw *f, uint16_t addr, uint8_t a, uint8_t x);
/* boot_reset's CPU set-up through the host's reset_cpu (M4.6c2 spec 3.3). Without a host or reset_cpu, a fault at
   FW_RESET_CPU $A625. */
void fw_reset_cpu(fw *f);
/* CLI (enabled) or SEI at the ROM address at through the host's set_interrupts (M4.6c2 spec 3.3). Without a host or
   set_interrupts, a fault at at. */
void fw_set_interrupts(fw *f, bool enabled, uint16_t at);

#endif
```

**`src/firmware/fw.c`**: replace

```c
    f->trap = NULL;
    f->unwound = false;
    f->tn_flushes = 0;
}
```

with

```c
    f->trap = NULL;
    f->unwound = false;
    f->stopped = false;
    f->tn_flushes = 0;
}
```

**`src/firmware/fw.c`**: replace

```c
void fw_wait(fw *f, fw_wait_kind kind, uint16_t at)
{
    if (f->host && f->host->wait && f->host->wait(f->host->ctx, kind, at))
        return;
    fault(f, at);
}
```

with

```c
void fw_wait(fw *f, fw_wait_kind kind, uint16_t at)
{
    fw_wait_result r = f->host && f->host->wait ? f->host->wait(f->host->ctx, kind, at) : FW_WAIT_FAILED;
    if (r == FW_WAIT_RETURNED)
        return;
    if (r == FW_WAIT_STOPPED) {
        f->stopped = true;   /* the run ends here: the C is abandoned, as at an unwound ROM call */
        if (f->trap)
            longjmp(*f->trap, 1);
        return;
    }
    fault(f, at);
}
```

**`src/firmware/fw.c`**: replace

```c
    f->unwound = true;   /* the C is abandoned as at an unwound ROM call (M4.2 spec 3) */
    if (f->trap)
        longjmp(*f->trap, 1);
}
```

with

```c
    f->unwound = true;   /* the C is abandoned as at an unwound ROM call (M4.2 spec 3) */
    if (f->trap)
        longjmp(*f->trap, 1);
}

void fw_reset_cpu(fw *f)
{
    if (f->host && f->host->reset_cpu) {
        f->host->reset_cpu(f->host->ctx);
        return;
    }
    fault(f, FW_RESET_CPU);
}

void fw_set_interrupts(fw *f, bool enabled, uint16_t at)
{
    if (f->host && f->host->set_interrupts) {
        f->host->set_interrupts(f->host->ctx, enabled);
        return;
    }
    fault(f, at);
}
```

**`src/engine/thunks.h`**: replace

```c
   thunks leave it alone. Each ROM call from C gets at most the cycles left before it and fails once it has passed,
   so a C wait that never ends (a whole utterance runs inside one hook call, where machine_run's budget is never
   checked) faults instead of hanging. */
typedef struct fw_bridge { machine *m; fw f; fw_host host; uint64_t thunk_calls, unwinds, deadline; } fw_bridge;

typedef struct {
```

with

```c
   thunks leave it alone. Each ROM call from C gets at most the cycles left before it and fails once it has passed,
   so a C wait that never ends (a whole utterance runs inside one hook call, where machine_run's budget is never
   checked) faults instead of hanging. An outermost entry row (phase set; M4.6c2 spec 3.2) runs instead under the
   engine's phase deadline, m->phase_deadline, which each feed moves: a wait that reaches it stops the run
   (FW_WAIT_STOPPED), so that machine_run returns MACHINE_CYCLE_LIMIT as the ROM engine's does. stops: C call chains
   abandoned at a stopped wait (M4.6c2 spec 3.1). */
typedef struct fw_bridge {
    machine *m;
    fw f;
    fw_host host;
    uint64_t thunk_calls, unwinds, stops, deadline;
    bool phase;
} fw_bridge;

typedef struct {
```

**`src/engine/thunks.h`**: replace

```c
    bool ramcode;             /* a routine of the RAM copy of bank_ramcode_image ($0200-$03FE): hooked with
                                 machine_hook_ramcode (M4.6c1 spec 3.3) */
} fw_replacement;

```

with

```c
    bool ramcode;             /* a routine of the RAM copy of bank_ramcode_image ($0200-$03FE): hooked with
                                 machine_hook_ramcode (M4.6c1 spec 3.3) */
    bool entry;               /* an entry (boot_reset, the re-entries, the bank entries; M4.6c2 spec 2.3): it never
                                 returns; an outermost one runs under the engine's phase deadline; checked by diff_main,
                                 not diff_lts */
} fw_replacement;

```

**`src/engine/thunks.c`**: replace

```c
}

/* A ROM call from C, hooked (machine_rom_call) or not (machine_rom_call_unhooked), within the bridge deadline; the
   window and the bank are read back after it. */
static fw_call_result rom_call(fw_bridge *b, uint16_t addr, fw_regs *r, uint64_t budget, bool unhooked)
{
    if (b->deadline) {   /* the outermost thunk's bound: at most the cycles left, none once it has passed */
        uint64_t now = b->m->cpu.cycles;
        if (now >= b->deadline)
            return FW_CALL_FAILED;
        if (budget > b->deadline - now)
            budget = b->deadline - now;
    }
    machine_regs mr = { r->a, r->x, r->y, b->m->cpu.p };   /* the routine starts with the machine's flags */
```

with

```c
}

/* The outermost thunk's bound (0: none): an entry row's is the engine's phase deadline, which each feed moves
   (M4.6c2 spec 3.2); any other's b->deadline. */
static uint64_t bridge_deadline(const fw_bridge *b) { return b->phase ? b->m->phase_deadline : b->deadline; }

/* A ROM call from C, hooked (machine_rom_call) or not (machine_rom_call_unhooked), within the bridge deadline; the
   window and the bank are read back after it. */
static fw_call_result rom_call(fw_bridge *b, uint16_t addr, fw_regs *r, uint64_t budget, bool unhooked)
{
    uint64_t deadline = bridge_deadline(b);
    if (deadline) {   /* the outermost thunk's bound: at most the cycles left, none once it has passed */
        uint64_t now = b->m->cpu.cycles;
        if (now >= deadline)
            return FW_CALL_FAILED;
        if (budget > deadline - now)
            budget = deadline - now;
    }
    machine_regs mr = { r->a, r->x, r->y, b->m->cpu.p };   /* the routine starts with the machine's flags */
```

**`src/engine/thunks.c`**: replace

```c
}

/* The firmware's hardware wait (M4.6a spec 3.3, M4.6b spec 3.2), on the emulated CPU, within the bridge deadline:
   - FW_WAIT_UART_TX: ser_tx_wait $C500 (BBC1 UART_STATUS,$C500 / RTS) runs as host code through host_rom_call, with
     the machine's own A, X and Y, so with interrupts live and the ROM's cycles. $C500 is never hooked.
   - FW_WAIT_INTERRUPT: the CPU idles as at a WIT at the hook's PC (never the idle WIT $C602, so no serial byte is
     delivered) until an interrupt has been handled, its handler ROM or C (machine_wait_interrupt).
   - FW_WAIT_TIMER2, FW_WAIT_TIMER3: isr_timer_start's loops $B372 and $B37F run with I set, as inside its PHP/SEI
     (machine_run_loop).
   at is the C's report of the loop; the hybrid host runs the kind's own. The window and the bank are read back after
   the wait, as after a ROM call: an interrupt handler may have run. */
static bool host_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    fw_bridge *b = ctx;
    machine *m = b->m;
    (void)at;
    if (kind == FW_WAIT_UART_TX) {
        fw_regs r = { m->cpu.a, m->cpu.x, m->cpu.y, 0 };
        return rom_call(b, FW_WAIT_UART_TX_LOOP, &r, FW_ROM_CALL_BUDGET, false) == FW_CALL_RETURNED;
    }
    uint64_t budget = FW_ROM_CALL_BUDGET;
    if (b->deadline) {   /* the outermost thunk's bound, as host_rom_call's */
        uint64_t now = m->cpu.cycles;
        if (now >= b->deadline)
            return false;
        if (budget > b->deadline - now)
            budget = b->deadline - now;
    }
    bool ok;
```

with

```c
}

/* A wait for the loop at that did not end: at an outermost entry row's phase deadline the run ends there
   (FW_WAIT_STOPPED; M4.6c2 spec 3.2: machine_run then returns MACHINE_CYCLE_LIMIT, as the ROM engine's does at its
   budget), the machine left at that loop, where the ROM engine would be found spinning (not at the hook, whose
   address would run the thunk again); otherwise a fault. */
static fw_wait_result wait_ended(fw_bridge *b, uint16_t at)
{
    machine *m = b->m;
    if (!b->phase || m->fault != MFAULT_NONE || m->cpu.cycles < m->phase_deadline)
        return FW_WAIT_FAILED;
    m->cpu.pc = at;
    m->cpu.waiting = false;
    return FW_WAIT_STOPPED;
}

/* The firmware's hardware wait (M4.6a spec 3.3, M4.6b spec 3.2, M4.6c2 spec 3.1), on the emulated CPU, within the
   bridge deadline:
   - FW_WAIT_UART_TX: ser_tx_wait $C500 (BBC1 UART_STATUS,$C500 / RTS) runs as host code through host_rom_call, with
     the machine's own A, X and Y, so with interrupts live and the ROM's cycles. $C500 is never hooked.
   - FW_WAIT_INTERRUPT: the CPU idles as at a WIT at the hook's PC (never the idle WIT $C602, so no serial byte is
     delivered) until an interrupt has been handled, its handler ROM or C (machine_wait_interrupt).
   - FW_WAIT_TIMER2, FW_WAIT_TIMER3: isr_timer_start's loops $B372 and $B37F run with I set, as inside its PHP/SEI
     (machine_run_loop).
   - FW_WAIT_INPUT: isr_idle's PHA (the machine's A: under isr_idle's own thunk the A of the ROM's JSR), then its WIT:
     the CPU waits at the idle point $C603 until an interrupt has been handled (machine_wait_input), and the PLA. When
     the idle handler stops the run there, the machine stays at the idle point, A pushed, as the ROM's isr_idle
     leaves it (FW_WAIT_STOPPED).
   at is the C's report of the loop; the hybrid host runs the kind's own, and leaves the machine there when the phase
   deadline ends the run. The window and the bank are read back after the wait, as after a ROM call: an interrupt
   handler may have run. */
static fw_wait_result host_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    fw_bridge *b = ctx;
    machine *m = b->m;
    if (kind == FW_WAIT_UART_TX) {
        fw_regs r = { m->cpu.a, m->cpu.x, m->cpu.y, 0 };
        return rom_call(b, FW_WAIT_UART_TX_LOOP, &r, FW_ROM_CALL_BUDGET, false) == FW_CALL_RETURNED
                   ? FW_WAIT_RETURNED
                   : FW_WAIT_FAILED;
    }
    /* Under an outermost entry row a wait may last until the phase deadline, as the ROM engine's loops do until
       machine_run's budget (M4.6c2 spec 3.2); any other at most FW_ROM_CALL_BUDGET. */
    uint64_t budget = b->phase ? UINT64_MAX : FW_ROM_CALL_BUDGET, deadline = bridge_deadline(b);
    if (deadline) {   /* the outermost thunk's bound, as host_rom_call's */
        uint64_t now = m->cpu.cycles;
        if (now >= deadline)
            return wait_ended(b, at);
        if (budget > deadline - now)
            budget = deadline - now;
    }
    bool ok;
```

**`src/engine/thunks.c`**: replace

```c
        ok = machine_run_loop(m, FW_WAIT_TIMER3_LOOP, FW_WAIT_TIMER3_EXIT, budget);
        break;
    default:
        return false;
    }
    sync_p2(b);
    return ok;
}

```

with

```c
        ok = machine_run_loop(m, FW_WAIT_TIMER3_LOOP, FW_WAIT_TIMER3_EXIT, budget);
        break;
    case FW_WAIT_INPUT: {
        m->ram[0x100 + m->cpu.s] = m->cpu.a;   /* PHA */
        m->cpu.s--;
        machine_input_result r = machine_wait_input(m, budget);
        sync_p2(b);
        if (r == MINPUT_STOPPED)
            return FW_WAIT_STOPPED;   /* at the idle point, A pushed */
        m->cpu.s++;                   /* PLA */
        return r == MINPUT_HANDLED ? FW_WAIT_RETURNED : wait_ended(b, at);
    }
    default:
        return FW_WAIT_FAILED;
    }
    sync_p2(b);
    return ok ? FW_WAIT_RETURNED : wait_ended(b, at);
}

/* boot_reset's CPU set-up $A625-$A62C (M4.6c2 spec 3.3) on the machine's CPU: SEI, LDX #$FF / TXS, CLT, CLD, CLC,
   CLV. */
static void host_reset_cpu(void *ctx)
{
    cpu740 *c = &((fw_bridge *)ctx)->m->cpu;
    c->p = (uint8_t)((c->p | CPU740_I) & ~(CPU740_T | CPU740_D | CPU740_C | CPU740_V));
    c->s = 0xFF;
}

/* CLI or SEI (M4.6c2 spec 3.3): the machine's I flag, on which the next wait's interrupts depend. */
static void host_set_interrupts(void *ctx, bool enabled)
{
    cpu740 *c = &((fw_bridge *)ctx)->m->cpu;
    c->p = enabled ? (uint8_t)(c->p & ~CPU740_I) : (uint8_t)(c->p | CPU740_I);
}

```

**`src/engine/thunks.c`**: replace

```c
    b->thunk_calls = 0;
    b->unwinds = 0;
    b->deadline = 0;
    b->host.ctx = b;
    b->host.rom_call = host_rom_call;
```

with

```c
    b->thunk_calls = 0;
    b->unwinds = 0;
    b->stops = 0;
    b->deadline = 0;
    b->phase = false;
    b->host.ctx = b;
    b->host.rom_call = host_rom_call;
```

**`src/engine/thunks.c`**: replace

```c
    b->host.enter = host_enter;
    b->host.rom_call_unhooked = host_rom_call_unhooked;
    fw_init(&b->f, m->ram, img, &b->host);
    return img != NULL;
```

with

```c
    b->host.enter = host_enter;
    b->host.rom_call_unhooked = host_rom_call_unhooked;
    b->host.reset_cpu = host_reset_cpu;
    b->host.set_interrupts = host_set_interrupts;
    fw_init(&b->f, m->ram, img, &b->host);
    return img != NULL;
```

**`src/engine/thunks.c`**: replace

```c
typedef void (*thunk_regs)(fw *f, cpu740 *c);

/* The frame every thunk shares: set the fault trap (saving an outer thunk's, as a ROM call from C can reach
   another thunk on the same bridge), sync the window and the bank, run the routine; then restore the trap and
   either leave the machine where an unwound ROM call or a bank entry (fw_enter) left it, report the fault, or
   emulate the RTS (an interrupt handler's RTI when
   isr is set, M4.6a spec 3.2). The outermost thunk (no deadline active) also bounds the whole call: b->deadline =
   its entry cycles + FW_ROM_CALL_SPEECH_BUDGET, cleared however it ends, so that a C wait that never ends
   (par_play's, a whole utterance inside one hook call) faults (thunks.h). */
static bool run_frame(machine *m, fw_bridge *b, thunk_regs regs, bool isr)
{
    if (m != b->m) {   /* registers come from m, RAM and host calls from b->m: they must be one machine */
```

with

```c
typedef void (*thunk_regs)(fw *f, cpu740 *c);

/* How a thunk exits: like RTS, like RTI (an interrupt handler, M4.6a spec 3.2), or never (an entry, M4.6c2 spec
   2.3). */
typedef enum { FRAME_CALL, FRAME_ISR, FRAME_ENTRY } frame_kind;

/* The frame every thunk shares: set the fault trap (saving an outer thunk's, as a ROM call from C can reach
   another thunk on the same bridge), sync the window and the bank, run the routine; then restore the trap and
   either leave the machine where a stopped wait (M4.6c2 spec 3.1), an unwound ROM call or a bank entry (fw_enter)
   left it, report the fault, or emulate the RTS (an interrupt handler's RTI, M4.6a spec 3.2). The outermost thunk
   (no deadline active) also bounds the whole call: b->deadline = its entry cycles + FW_ROM_CALL_SPEECH_BUDGET, or for
   an entry row the engine's phase deadline (b->phase, when the engine set one), cleared however it ends, so that a
   C wait that never ends (par_play's, a whole utterance inside one hook call) faults or, at the phase deadline, ends
   the run (thunks.h). */
static bool run_frame(machine *m, fw_bridge *b, thunk_regs regs, frame_kind kind)
{
    if (m != b->m) {   /* registers come from m, RAM and host calls from b->m: they must be one machine */
```

**`src/engine/thunks.c`**: replace

```c
    uint16_t entry = m->cpu.pc;
    uint8_t entry_sp = m->cpu.s;
    bool outermost = b->deadline == 0;
    if (outermost)
        b->deadline = m->cpu.cycles + FW_ROM_CALL_SPEECH_BUDGET;
    jmp_buf trap, *saved = b->f.trap;
    b->f.trap = &trap;
    sync_p2(b);
    if (setjmp(trap) == 0)
        regs(&b->f, &m->cpu);
    b->f.trap = saved;
    if (outermost)
        b->deadline = 0;
    if (b->f.unwound) {   /* a ROM call reset the stack or the C entered a bank: the C is abandoned, the machine
                             goes on where the ROM went or at the bank entry */
```

with

```c
    uint16_t entry = m->cpu.pc;
    uint8_t entry_sp = m->cpu.s;
    bool outermost = b->deadline == 0 && !b->phase;
    if (outermost) {
        if (kind == FRAME_ENTRY && m->phase_deadline)
            b->phase = true;
        else
            b->deadline = m->cpu.cycles + FW_ROM_CALL_SPEECH_BUDGET;
    }
    jmp_buf trap, *saved = b->f.trap;
    b->f.trap = &trap;
    sync_p2(b);
    if (setjmp(trap) == 0)
        regs(&b->f, &m->cpu);
    b->f.trap = saved;
    if (outermost) {
        b->deadline = 0;
        b->phase = false;
    }
    if (b->f.stopped) {   /* a wait stopped the run: the machine stays where the host left it (the idle point, or
                             the ROM loop of a wait that reached the phase deadline) */
        b->f.stopped = false;
        b->stops++;
        return true;
    }
    if (b->f.unwound) {   /* a ROM call reset the stack or the C entered a bank: the C is abandoned, the machine
                             goes on where the ROM went or at the bank entry */
```

**`src/engine/thunks.c`**: replace

```c
        return true;
    }
    if (isr)
        machine_return_interrupt(m);
    else
        machine_return(m);
    return true;
}

static bool run_thunk(machine *m, fw_bridge *b, thunk_regs regs) { return run_frame(m, b, regs, false); }

/* The registers in, a bank routine (bank.c, M4.6c1), the registers out: A, X and Y as the ROM leaves them. No caller
```

with

```c
        return true;
    }
    if (kind == FRAME_ISR)
        machine_return_interrupt(m);
    else
        machine_return(m);
    return true;
}

static bool run_thunk(machine *m, fw_bridge *b, thunk_regs regs) { return run_frame(m, b, regs, FRAME_CALL); }

/* The registers in, a bank routine (bank.c, M4.6c1), the registers out: A, X and Y as the ROM leaves them. No caller
```

**`src/engine/thunks.c`**: replace

```c
/* The hook of an interrupt handler: its frame exits like RTI. */
#define ISR_THUNK(name) \
    static bool t_##name(machine *m, void *user) { return run_frame(m, user, name##_regs, true); }
ISR_THUNK(isr_timer_dsp)
ISR_THUNK(isr_int1)
```

with

```c
/* The hook of an interrupt handler: its frame exits like RTI. */
#define ISR_THUNK(name) \
    static bool t_##name(machine *m, void *user) { return run_frame(m, user, name##_regs, FRAME_ISR); }
ISR_THUNK(isr_timer_dsp)
ISR_THUNK(isr_int1)
```

**`src/engine/rom_engine.h`**: replace

```c
typedef void (*engine_setup_fn)(machine *m, void *user);

/* The engine run: boot to idle, then feed text + CR and run to idle again. setup (may be NULL) runs after
   machine_init. produced gets the produced frames (the boot frames too if include_boot); raw, when not NULL,
   the raw DSP-port frames of the speaking phase. The rom_engine_speak* functions below are this with
   setup = NULL. */
machine_status rom_engine_run(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
```

with

```c
typedef void (*engine_setup_fn)(machine *m, void *user);

/* The input an engine run feeds at the end of boot: n bytes (raw serial input: a NUL may be among them), then a CR
   when cr is set. */
typedef struct { const uint8_t *bytes; size_t n; bool cr; } engine_input;

/* Watches an engine run (diff_main, M4.6c2 spec 4.1), each callback when not NULL: on_frame at every frame event (the
   machine's frame callback, whether the frames are kept or not), on_idle at every input idle, before the engine's
   idle handler feeds or stops. Both get the engine's machine, which on_idle may change (an injection). */
typedef struct {
    void (*on_frame)(void *user, machine *m);
    void (*on_idle)(void *user, machine *m);
    void *user;
} engine_watch;

/* The engine run (M4.6c2 spec 3.1): machine_init, setup (may be NULL: it installs hooks), then machine_run in a loop
   with the engine's idle handler on the machine. At the first idle (the end of boot) the handler resets the frame
   counters, keeps the frames from then on and feeds in; at the next it stops the run, and machine_run's MACHINE_IDLE
   is the result. The phase deadline (m->phase_deadline) is ROM_ENGINE_BOOT_CYCLES from the start and
   ROM_ENGINE_SPEAK_CYCLES from the feed; reaching it is MACHINE_CYCLE_LIMIT. Input longer than the serial queue is
   MFAULT_INPUT_OVERFLOW (MACHINE_FAULT). produced gets the produced frames (the boot frames too if include_boot);
   raw, when not NULL, the raw DSP-port frames of the speaking phase; watch (may be NULL) sees every event. */
machine_status rom_engine_run_input(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                    engine_setup_fn setup, void *user, const engine_input *in, bool include_boot,
                                    frame_list *produced, frame_list *raw, const engine_watch *watch);

/* rom_engine_run_input with the input text + CR and no watch. The rom_engine_speak* functions below are this with
   setup = NULL. */
machine_status rom_engine_run(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
```

**`src/engine/rom_engine.c`**: replace

```c
}

machine_status rom_engine_run(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                              engine_setup_fn setup, void *user, const char *text, bool include_boot,
                              frame_list *produced, frame_list *raw)
{
    frame_sink sink, raw_sink;
    machine_status st;
    sink.list = produced;
    sink.enabled = include_boot;
    raw_sink.list = raw;
    raw_sink.enabled = false;
    if (!machine_init(m, rom, len, cfg))
        return MACHINE_FAULT;
    if (setup)
        setup(m, user);
    if (m->fault != MFAULT_NONE)   /* the setup could not install its hooks */
        return MACHINE_FAULT;
    machine_set_outputs(m, on_frame, &sink);
    if (raw)
        machine_set_raw_output(m, on_frame, &raw_sink);
    st = machine_run(m, ROM_ENGINE_BOOT_CYCLES);
    if (st == MACHINE_IDLE) {
        m->frames_produced = 0;
        m->lower_window_reads = 0;
        m->raw_frames = 0;
        m->dsp_resets = 0;
        m->raw_discarded = 0;
        sink.enabled = true;
        raw_sink.enabled = true;
        if (machine_feed_serial(m, (const uint8_t *)text, strlen(text)) &&
            machine_feed_serial(m, (const uint8_t *)"\r", 1))
            st = machine_run(m, ROM_ENGINE_SPEAK_CYCLES);
        else {
            m->fault = MFAULT_INPUT_OVERFLOW;   /* text longer than the input queue */
            st = MACHINE_FAULT;
        }
    }
    machine_set_outputs(m, NULL, NULL);       /* the sinks live on this stack frame */
    machine_set_raw_output(m, NULL, NULL);
    return st;
}

```

with

```c
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

```

**`src/engine/hybrid_engine.h`**: replace

```c
                                   const char *only, const char *text, bool include_boot,
                                   frame_list *produced, frame_list *raw, hybrid_stats *stats);

#endif
```

with

```c
                                   const char *only, const char *text, bool include_boot,
                                   frame_list *produced, frame_list *raw, hybrid_stats *stats);
/* hybrid_engine_speak with the input and the watch of rom_engine_run_input. */
machine_status hybrid_engine_run_input(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                       const char *only, const engine_input *in, bool include_boot,
                                       frame_list *produced, frame_list *raw, hybrid_stats *stats,
                                       const engine_watch *watch);

#endif
```

**`src/engine/hybrid_engine.c`**: replace

```c
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
```

with

```c
                                   frame_list *produced, frame_list *raw, hybrid_stats *stats)
{
    engine_input in = { (const uint8_t *)text, strlen(text), true };
    return hybrid_engine_run_input(m, rom, len, cfg, only, &in, include_boot, produced, raw, stats, NULL);
}

machine_status hybrid_engine_run_input(machine *m, const uint8_t *rom, size_t len, const machine_config *cfg,
                                       const char *only, const engine_input *in, bool include_boot,
                                       frame_list *produced, frame_list *raw, hybrid_stats *stats,
                                       const engine_watch *watch)
{
    hybrid_setup h;
    frame_list scratch = { 0 };
    memset(&h, 0, sizeof h);
    h.only = only;
    machine_status st = rom_engine_run_input(m, rom, len, cfg, setup, &h, in, include_boot,
                                             produced ? produced : &scratch, raw, watch);
    machine_clear_hooks(m);   /* the bridge lives on this stack frame */
    frame_list_free(&scratch);
```

- [ ] **Step 4: Run the unit tests**

Run: `cmd //c ".\build.bat test fw_boot"`, then `build/apollo2_tests.exe machine_hooks` and `build/apollo2_tests.exe fw`.
Expected: the build passes with no warnings (the firmware syntax check included), and

```
suite_fw_boot                ok
1 suite(s), 0 failure(s), 0 skip(s)
suite_machine_hooks          ok
1 suite(s), 0 failure(s), 0 skip(s)
```

and the `fw` run: `suite_fw`, `suite_fw_lts`, `suite_fw_tn`, `suite_fw_cmd`, `suite_fw_pros`, `suite_fw_par`, `suite_fw_frm`, `suite_fw_ser`, `suite_fw_dsp`, `suite_fw_bank`, `suite_fw_boot` all `ok`, `11 suite(s), 0 failure(s), 0 skip(s)`.

- [ ] **Step 5: Run the full suite**

Run: `cmd //c ".\build.bat test"` (in the background; it takes about 15 minutes)
Expected (the engines' loop changes no result: every summary line as at a05f7d5):

- `39 suite(s), 0 failure(s), 0 skip(s)`, no warnings, the firmware syntax check passing (777 s in the proof run);
- `firmware_data`: `49044 converted bytes compared with the ROM, 0 differ`;
- `diff_lts`: the 105 lines `<name> N calls checked` exactly as at a05f7d5 (`bank_enter 9`, `bank_scan 58`, …, `cmd_run_due 5924`, `cmd_run_next 165`, `isr_set_volume 811`, …, `isr_timer_dsp 2697929`, …, `isr_par_ready 8293540`, …, `ph_lengthen_count 1325`), `1 mid-clause flush calls skipped (golden_hybrid covers them)`, `24 unwound calls compared`;
- `diff_play`: `self-check (ROM against ROM, hello): par_play 1 calls, boot_loop_play 2 calls, 2 injected; $B8A5 reached 2, $B89C reached 2`, `5 routines wait on the timer interrupt`, `boot_loop_play 179 calls checked, 48 with both injections`, `isr_speech_start 126`, `isr_wait_frames 126`, `isr_wait_frame 504`, `par_play 126 calls checked, 48 with both injections`, `$B8A5 (abort) reached in 96 calls, $B89C (reply due) in 96`, `7770113 timer interrupts run in C in copy B`;
- `diff_serial`: the 34 probe lines twice, as at a05f7d5 (corpus: `isr_int1` probes 2,649 calls, `ser_tx_put` probes 124, the six bank probes 2, 2, 2, 3, 50 and 686), every probe `(C)` on the corpus;
- `golden_hybrid`: `48 corpus files x 111 modes (all, then each routine alone)`, every golden unchanged (`git status --short tests/golden` prints nothing);
- `suite_golden`: `timer scale 1/4: 105 frames, 11789152 CPU cycles`, `timer scale 3/1: 105 frames, 1286784 CPU cycles`, `hybrid timer scale 1/4: 105 frames, 11684594 CPU cycles`, `hybrid timer scale 3/1: 105 frames, 979508 CPU cycles`;
- `suite_synth`: `corpus peak plain 20522 (-4.1 dBFS)`, `corpus peak b11 16038 (-6.2 dBFS)`.

- [ ] **Step 6: Commit**

```bash
git add src/machine/machine.h src/machine/machine.c src/firmware/fw.h src/firmware/fw.c src/engine/thunks.h src/engine/thunks.c src/engine/rom_engine.h src/engine/rom_engine.c src/engine/hybrid_engine.h src/engine/hybrid_engine.c tests/test_fw_boot.c tests/suites.h tests/test_machine_hooks.c tests/test_golden_hybrid.c tests/test_fw_dsp.c tests/test_fw_par.c tests/test_fw_ser.c tests/test_fw_tn.c
git commit -m "M4.6c2 infrastructure: the machine's idle handler (machine_set_idle_handler, machine_idle) and both engines' loop through it (engine_input, engine_watch), the input wait (FW_WAIT_INPUT, machine_wait_input, f->stopped), the entry rows' phase deadline, reset_cpu and set_interrupts, the delivery guard, the suite suite_fw_boot (M4.6c2 3, 4.5)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 2: The data: the self-test's command strings and `boot_prosody_defaults` in `boot_data.c`, `firmware_data` green (spec §2.4, §4.8 item 2)

**ROM facts checked / what was verified while proving the plan** (`rom_A79F_boot_selftest_sound.txt`, `rom_C612_boot_queue_string.txt`, `rom_A7F0_boot_load_prosody_defaults.txt`, `rom_AC7E_boot_init_baud.txt`, `rom_A7D4_boot_loop_play.txt`, `data.txt`, `ghidra740/annotations/boot.txt`, `unused.txt` and `ram.txt`; `src/firmware/pros_make.c`, `pros_phrase.c`, `pros_gen.c`; the ROM image):
- **The self-test strings** (`boot_selftest_sound` $A79F–$A7AE: `LDM #$BE / LDM #$A7`, then $A7C5 when the self-test failed; `boot_queue_string` copies up to the zero, $C61C `LDA ($46),Y / BEQ`): `boot_selftest_ok_cmds` $A7BE–$A7C4 is $18 (Ctrl-X, the cancel) "@T5, " and its zero (7 bytes); `boot_selftest_fail_cmds` $A7C5–$A7D3 is $18 "@T7 @T7 @T7, " and its zero (15); `boot_loop_play`'s code starts at $A7D4. They are C strings whose terminating 0 is the ROM's zero end, so the encoder writes each with it (step 13). `boot_queue_string` puts them into the input ring with `ser_rx_put`, not through `ser_rx_byte`, so the $18 is a character of the clause, not a mute.
- **`boot_prosody_defaults`** ($C65B, 26 bytes; `boot_load_prosody_defaults` $A7F2–$A7FB: `LDA $C65B,Y / STA $168B,Y / INY / CPY #$1A`): `B4 19 08 F6 EC`, `B4 19 08 00 05`, `C3 19 0A 0A 19`, `B4 19 08 0A 00`, `00 00 0F 00 01 00`, one per `@R` letter A–Z. `pros_make_segments` copies A–E to `pros_baseline`, `pros_baseline_end`, `pros_stress_param`, `pros_nucleus_param` and `pros_final_param` (indices 0, 1, 2, 3, 4), `pros_mark_phrases` the same five of the sets F, K and P for ',' ':', '?' and '!'; `pros_phrase_gen` reads U, V, W, Y and Z (`PROS_PARAM_U` … `_Z`), and X has no reader. A pitch target is `pros_baseline` / 8 plus the terms plus 105, each added with `ADC` mod 256 ($D2DF), so $F6 and $EC are the terms −10 and −20 (written `TERM(-10)`, `TERM(-20)`); the baselines are 180, 25 and 195. The 6 bytes after them, `unused_C675`, have no reader and stay unconverted.
- **The baud texts are already converted** (spec §2.4 lists them as new): `boot_baud_text_19200` $ACC7, `_300` $ACCA (14 bytes) and `_1200` $ACD8 (16) are three of M4.1's five `fw_phonetic_texts` (the range $ACC7–$ACE7 in `test_firmware_data`'s list since M4.1). So the new converted bytes are 7 + 15 + 26 = 48. Their addresses, which `boot_init_baud` puts in `tn_rule_ptr` (`LDA #$CA` / `#$D8` / `#$C7`, `LDA #$AC`), are named in Task 4, where the C first uses them.
- **The proof run**: `firmware_data` prints `49092 converted bytes compared with the ROM, 0 differ` (49,044 + 48), the first time the data was encoded.

**Files:**
- Create: `src/firmware/data/boot_data.c`
- Modify: `src/firmware/data/fw_data.h`, `src/firmware/data/fw_data.c`, `tests/test_firmware_data.c`

**Interfaces:**
- Consumes: `put`, `put_text` and `check_end` (`fw_data.c`).
- Produces (Tasks 4–5 read the image through `fw_rom8` and `fw_ind_y`):

```c
/* src/firmware/data/fw_data.h */
#define BOOT_SELFTEST_OK_CMDS 0xA7BE, BOOT_SELFTEST_FAIL_CMDS 0xA7C5, BOOT_SELFTEST_CMDS_END 0xA7D4
#define BOOT_PROSODY_DEFAULTS 0xC65B, BOOT_PROSODY_PARAMS 26
extern const char fw_boot_selftest_ok_cmds[7], fw_boot_selftest_fail_cmds[15];
extern const uint8_t fw_boot_prosody_defaults[BOOT_PROSODY_PARAMS];
/* the image: + $A7BE-$A7D3 and $C65B-$C674 */
```

No name collides: `grep -rn "BOOT_SELFTEST\|BOOT_PROSODY\|fw_boot_" src tests` prints nothing before this task.

- [ ] **Step 1: Write the failing tests**

`test_firmware_data.c` expects the two new converted ranges, the new byte count and the strings' and parameters' shape.

**`tests/test_firmware_data.c`**: replace

```c

typedef struct { uint32_t from, to; } range;   /* [from, to) in CPU addresses */
static const range k_rom[] = { { 0x4000, 0x8FF4 }, { 0x9800, 0xA397 }, { 0xACC7, 0xACE8 }, { 0xADFE, 0xAFFE },
                               { 0xB473, 0xB77D }, { 0xB783, 0xB7A1 }, { 0xB7A7, 0xB7B1 }, { 0xBAEC, 0xBAF2 },
                               { 0xC561, 0xC571 }, { 0xC67B, 0xC9B8 }, { 0xD44A, 0xF77B }, { 0xFFD0, 0xFFD4 },
                               { 0xFFE0, 0xFFE8 }, { 0xFFEA, 0xFFEE } };
static const range k_low[] = { { 0x4010, 0x7832 } };

```

with

```c

typedef struct { uint32_t from, to; } range;   /* [from, to) in CPU addresses */
static const range k_rom[] = { { 0x4000, 0x8FF4 }, { 0x9800, 0xA397 }, { 0xA7BE, 0xA7D4 }, { 0xACC7, 0xACE8 },
                               { 0xADFE, 0xAFFE }, { 0xB473, 0xB77D }, { 0xB783, 0xB7A1 }, { 0xB7A7, 0xB7B1 },
                               { 0xBAEC, 0xBAF2 }, { 0xC561, 0xC571 }, { 0xC65B, 0xC675 }, { 0xC67B, 0xC9B8 },
                               { 0xD44A, 0xF77B }, { 0xFFD0, 0xFFD4 }, { 0xFFE0, 0xFFE8 }, { 0xFFEA, 0xFFEE } };
static const range k_low[] = { { 0x4010, 0x7832 } };

```

**`tests/test_firmware_data.c`**: replace

```c
    CHECK_EQ(fw_bank_entry_code_count, 2);     /* SEI / JMP boot_reentry_cold */
    CHECK_EQ(fw_opcode_count, 57);
}

```

with

```c
    CHECK_EQ(fw_bank_entry_code_count, 2);     /* SEI / JMP boot_reentry_cold */
    CHECK_EQ(fw_opcode_count, 57);

    /* The boot data (M4.6c2 spec 2.4) */
    CHECK_EQ(strlen(fw_boot_selftest_ok_cmds), 6);      /* Ctrl-X, "@T5, " */
    CHECK_EQ(strlen(fw_boot_selftest_fail_cmds), 14);   /* Ctrl-X, "@T7 @T7 @T7, " */
    CHECK_EQ(fw_boot_selftest_ok_cmds[0], 0x18);
    CHECK_EQ(fw_boot_selftest_fail_cmds[0], 0x18);
    CHECK_EQ(fw_boot_prosody_defaults[0], fw_boot_prosody_defaults[5]);    /* '.' and ',' start on one baseline */
    CHECK(fw_boot_prosody_defaults[10] > fw_boot_prosody_defaults[0]);    /* '?' starts higher */
    CHECK_EQ(fw_boot_prosody_defaults[23], 0);                             /* X, which nothing reads */
}

```

**`tests/test_firmware_data.c`**: replace

```c
    }
    printf("  %u converted bytes compared with the ROM, %u differ\n", compared, bad);
    CHECK_EQ(compared, 49044);   /* M4.0's 35,214, M4.1's phonetic texts (97), M4.2's command data (567),
                                    M4.3a's prosody data (2,319), M4.4a's playback data (9,495), M4.4b's
                                    par_slot_map (10), M4.5's frame builder data (794), M4.6a's ser_hex_chars
                                    (16) and M4.6c1's bank code (532: $ADFE-$AFFD and 20 header bytes) */
    CHECK_EQ(bad, 0);
}
```

with

```c
    }
    printf("  %u converted bytes compared with the ROM, %u differ\n", compared, bad);
    CHECK_EQ(compared, 49092);   /* M4.0's 35,214, M4.1's phonetic texts (97), M4.2's command data (567),
                                    M4.3a's prosody data (2,319), M4.4a's playback data (9,495), M4.4b's
                                    par_slot_map (10), M4.5's frame builder data (794), M4.6a's ser_hex_chars
                                    (16), M4.6c1's bank code (532: $ADFE-$AFFD and 20 header bytes) and M4.6c2's
                                    boot data (48: the self-test strings, 22, and boot_prosody_defaults, 26) */
    CHECK_EQ(bad, 0);
}
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test firmware_data"`
Expected: the build stops with compile errors (the proof run, first lines):

```
tests\test_firmware_data.c(124): error C2065: 'fw_boot_selftest_ok_cmds': undeclared identifier
tests\test_firmware_data.c(125): error C2065: 'fw_boot_selftest_fail_cmds': undeclared identifier
tests\test_firmware_data.c(126): error C2065: 'fw_boot_selftest_ok_cmds': undeclared identifier
tests\test_firmware_data.c(126): error C2109: subscript requires array or pointer type
```

- [ ] **Step 3: Implement**

**Create `src/firmware/data/boot_data.c`:**

```c
/* Converted from the Apollo 2 ROM (M4.6c2 spec 2.4), then reviewed: the boot's command strings and the power-up
   prosody parameters. tests/test_firmware_data.c re-encodes them and compares them byte for byte with the ROM. */
#include "fw_data.h"

/* The self-test's command strings ($A7BE, $A7C5): boot_selftest_sound points cmd_text_ptr at one and
   boot_queue_string queues it into the input ring up to its zero end (C's terminating 0, which the encoder writes).
   Each starts with $18 (Ctrl-X, the cancel), then @T sound effects: one short buzz when the self-test passes, three
   long ones when it fails. */
const char fw_boot_selftest_ok_cmds[7] = "\030@T5, ";
const char fw_boot_selftest_fail_cmds[15] = "\030@T7 @T7 @T7, ";

/* A pitch term, added to each pitch target mod 256 (pros_target, $D2DF: ADC): a negative one is its two's
   complement. */
#define TERM(n) ((uint8_t)(n))

/* boot_prosody_defaults ($C65B): the power-up values of pros_params (boot_load_prosody_defaults), one per @R letter.
   A-T are five per phrase type: the start and end baselines (pros_baseline, pros_baseline_end; a pitch target adds
   pros_baseline / 8), then the stress, nucleus and final terms (pros_mark_phrases, pros_phrase_gen). U-Z are the word
   terms of pros_phrase_gen. */
const uint8_t fw_boot_prosody_defaults[26] = {
    180,         /* A: '.' and ';' (and the default): the start baseline */
    25,          /* B: the end baseline */
    TERM(8),     /* C: the stress term */
    TERM(-10),   /* D: the nucleus term */
    TERM(-20),   /* E: the final term */
    180,         /* F: ',' and ':': the start baseline */
    25,          /* G: the end baseline */
    TERM(8),     /* H: the stress term */
    TERM(0),     /* I: the nucleus term */
    TERM(5),     /* J: the final term */
    195,         /* K: '?': the start baseline */
    25,          /* L: the end baseline */
    TERM(10),    /* M: the stress term */
    TERM(10),    /* N: the nucleus term */
    TERM(25),    /* O: the final term */
    180,         /* P: '!': the start baseline */
    25,          /* Q: the end baseline */
    TERM(8),     /* R: the stress term */
    TERM(10),    /* S: the nucleus term */
    TERM(0),     /* T: the final term */
    TERM(0),     /* U: pros_term_stress of a word's first symbol */
    TERM(0),     /* V: added to pros_term_tail at a word's last symbol */
    TERM(15),    /* W: the accent */
    0,           /* X: no reader */
    TERM(1),     /* Y: a later stress of a word */
    TERM(0),     /* Z: pros_term_stress of a word's later symbols */
};
```

**`src/firmware/data/fw_data.h`**: replace

```c
#define SER_HEX_CHARS_END   0xC571

/* lts_char_class bits */
#define CC_LETTER       0x80   /* also the apostrophe and the underscore */
```

with

```c
#define SER_HEX_CHARS_END   0xC571

/* Pinned addresses of the boot data (ghidra740/annotations: boot.txt; M4.6c2 spec 2.4) */
#define BOOT_SELFTEST_OK_CMDS     0xA7BE   /* char[7]: queued when the self-test passes ... */
#define BOOT_SELFTEST_FAIL_CMDS   0xA7C5   /* char[15]: ... and when it fails; boot_loop_play's code follows at $A7D4 */
#define BOOT_SELFTEST_CMDS_END    0xA7D4
#define BOOT_PROSODY_DEFAULTS     0xC65B   /* byte[26]: pros_params at power-up; the unconverted unused_C675 follows */
#define BOOT_PROSODY_PARAMS       26

/* lts_char_class bits */
#define CC_LETTER       0x80   /* also the apostrophe and the underscore */
```

**`src/firmware/data/fw_data.h`**: replace

```c
extern const uint8_t fw_frm_template[FRAME_SIZE], fw_frm_source_codes[4], fw_frm_filter_codes[2];
extern const char fw_ser_hex_chars[17];   /* the 16 digits, then C's terminating 0 (not encoded) */
extern const uint8_t fw_cmd_sound_flags[CMD_SOUND_MAX];
extern const fw_cmd_sound_effect fw_cmd_sound_effects[CMD_SOUND_EFFECTS];
```

with

```c
extern const uint8_t fw_frm_template[FRAME_SIZE], fw_frm_source_codes[4], fw_frm_filter_codes[2];
extern const char fw_ser_hex_chars[17];   /* the 16 digits, then C's terminating 0 (not encoded) */
extern const char fw_boot_selftest_ok_cmds[7], fw_boot_selftest_fail_cmds[15];   /* each with its zero end */
extern const uint8_t fw_boot_prosody_defaults[BOOT_PROSODY_PARAMS];
extern const uint8_t fw_cmd_sound_flags[CMD_SOUND_MAX];
extern const fw_cmd_sound_effect fw_cmd_sound_effects[CMD_SOUND_EFFECTS];
```

**`src/firmware/data/fw_data.c`**: replace

```c
    }
    put_bank_code(&e);
    return e.ok;
}
```

with

```c
    }
    put_bank_code(&e);
    /* 13. The boot data (M4.6c2 spec 2.4): the self-test's two command strings back to back, each with its zero end,
       ending where boot_loop_play's code starts; boot_prosody_defaults. */
    {
        uint32_t at = put_text(&e, false, BOOT_SELFTEST_OK_CMDS, fw_boot_selftest_ok_cmds, false);
        put(&e, false, at++, 0);
        check_end(&e, "boot_selftest_ok_cmds", at, BOOT_SELFTEST_FAIL_CMDS);
        at = put_text(&e, false, at, fw_boot_selftest_fail_cmds, false);
        put(&e, false, at++, 0);
        check_end(&e, "boot_selftest_fail_cmds", at, BOOT_SELFTEST_CMDS_END);
        for (uint32_t i = 0; i < BOOT_PROSODY_PARAMS; i++)
            put(&e, false, BOOT_PROSODY_DEFAULTS + i, fw_boot_prosody_defaults[i]);
    }
    return e.ok;
}
```

- [ ] **Step 4: Run the data test**

Run: `cmd //c ".\build.bat test firmware_data"`
Expected: no warnings, and

```
  49092 converted bytes compared with the ROM, 0 differ
suite_firmware_data          ok
1 suite(s), 0 failure(s), 0 skip(s)
```

A byte that differs is a value or a string with a wrong character: the message names the address; fix the data from the ROM listing in `data.txt`, never the comparison.

- [ ] **Step 5: Run the full suite**

Run: `cmd //c ".\build.bat test"` (in the background)
Expected:

- `39 suite(s), 0 failure(s), 0 skip(s)`, no warnings (826 s in the proof run);
- `firmware_data`: `49092 converted bytes compared with the ROM, 0 differ`;
- `diff_lts`, `diff_play`, `diff_serial`, `golden_hybrid` (111 modes), `suite_golden` and `suite_synth` exactly as in Task 1 Step 5 (nothing reads the new data yet).

- [ ] **Step 6: Commit**

```bash
git add src/firmware/data/boot_data.c src/firmware/data/fw_data.h src/firmware/data/fw_data.c tests/test_firmware_data.c
git commit -m "M4.6c2 data: the self-test's command strings and boot_prosody_defaults (one value per @R letter, with its meaning) converted; firmware_data compares 49,092 bytes (M4.6c2 2.4)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 3: The tests before the C: `diff_main` (the ROM engine against the hybrid at every input idle and frame event) and the deferred `diff_serial` probes, proved with the main loop still ROM (spec §4.1, §4.3, §4.8 item 3)

**ROM facts checked / what was verified while proving the plan** (`rom_A697_boot_get_clause.txt`, `rom_C581_ser_tx_service.txt`, `rom_C3EB_isr_host_line_high.txt`, `rom_C2B4_isr_int1.txt`, `rom_B398_isr_speech_stop.txt`, `rom_B2D9_isr_timer_dsp.txt`, `rom_C605_boot_selftest.txt`, `ramcode_0210_bank_enter.txt`, `ramcode_025E_bank_select.txt`, `ramcode_02E9_bank_safe_char.txt`, `ramcode_02F0_bank_find_current.txt`; `machine.c` (`maybe_deliver`), `src/firmware/data/bank_data.c`):
- **How `diff_main` runs both engines and pairs their events.** The whole hybrid main program runs inside one hook call (Task 6: the reset entry's), so the two engines cannot run step by step side by side; they run one after the other on the same input and configuration. The ROM engine runs first (`rom_engine_run_input`) with a watch (`engine_watch`) that records every event in order: its kind (an input idle, at the engine's idle handler before it feeds or stops; a frame event, at the machine's frame callback) and a snapshot: all RAM but the SFR bytes and the stack page $0100–$01FF, the SFR model state (`diff_same_sfr`, the fields `diff_same` compares), the DSP outputs (port writes, raw position and bytes, reset pulses and level), `frames_produced`, `raw_frames` and the I flag. The hybrid engine with every replacement then runs (`hybrid_engine_run_input`), and its watch compares event *i* with the ROM's event *i*: the same kind and the same snapshot; the first difference ends that run's comparison. At the end of a run both engines must have the same status and the same number of events, and the final states are compared as one more event. Run time: 22 s for the 47 corpus texts and the seven extra runs in the proof run; each snapshot holds the 16 KB of RAM.
- **The timer interrupt's per-word counters at an idle** (spec §4.1 compares all RAM but the stack page): with the frame loop in C since M4.4b, the first comparison found every corpus text apart at the end of boot, `RAM $0059: ROM 09, C 0A`. `isr_tick_div` $59, `isr_word_ofs` $5A and `isr_words_left` $5B count the ticks within a frame; after the utterance's last frame event `isr_speech_stop` ($B398) stops timer 3 a number of ticks later that depends on how long the main program takes (the ROM's `isr_volume_half`, `isr_wait_frame` and `isr_set_volume` take cycles, the C none). `diff_play` leaves them out at exit for that reason (M4.4b). So at idles and at the end of a run these three bytes are not compared; at every frame event they are (the frame boundary is the same tick in both). The DSP outputs and everything else compare equal at idles.
- **The extra runs** (spec §4.1), each with the ROM address its ROM run must execute (a hook on the ROM engine alone; the count is printed):
  - `warm`: "@=1 One. Two.": bank 1 is the main ROM with the running version, so `bank_enter` enters $FFD4; the rest of the clause is dropped and " Two." spoken.
  - `cold`: the same with `bank_ver_minor` + 1 injected into both engines at the end of boot: `bank_enter`'s $0228 `BNE` to the cold entry $FFD0 (`boot_read_dip`, `boot_init_settings`: the input ring emptied).
  - `dip_bank`: the P4 input's DIP bit 1 and "@=5 Five.": `bank_select` reads `bank_p2_table_alt` at boot and at `@=5` (`bank_enter` $0210 reached).
  - `selftest_fail`: DIP bit 1 with the P2.1 input high: `boot_selftest` ($C60B `BBC1 P2` not taken) sets `boot_selftest_fail`, and `boot_selftest_sound` queues the fail string ($A7AB): three long buzzes (365 frame events in all). Reachable this way: `boot_init_ports` makes P2.1 an input (P2_DIR $FD) and the machine's pins give the configured level.
  - `status_waits`, "a query whose status reply waits on the host line": "@L One." with `ser_status_due` = 1 injected at the speech's second frame event (`isr_host_line_high` sets it, $C3F0) and the host line P0.2 low: `ser_tx_service` returns at once ($C585 `BBC2 P0`) in `par_play` and then in `boot_get_clause` ($A6D7), whose loop changes nothing from then on: the ROM spins in it until the speaking phase's deadline, `MACHINE_CYCLE_LIMIT` (4,531,687 passes through $A6D7). The machine delivers serial bytes only at the idle WIT, which that loop never reaches.
  - `status_sent`: the CR alone with `ser_status_due` = 1 and P0.2 high injected at the end of boot: after the CR's clause `boot_get_clause` calls `ser_tx_service` ($A6D7), which sends a NAK ($15: nothing pending) and clears it, a pass that changed state, then idles. A longer input cannot follow: `ser_tx_service` sets the busy line P0.5 after a send ($C59E), the machine delivers the next byte only at the busy level of the first idle (`maybe_deliver`), and only `isr_int1`'s receive path ($C363 `CLB5 P0`) clears it again, after a byte. With P0.2 high from boot the first idle's level is busy, and the first byte's interrupt clears it: the same lock the other way.
  - `parallel`: DIP bit 0: isr_idle returns at once ($C603 by its `BBS0`, 806,094 times): no idle; both engines end at the boot phase's deadline after the boot announcement's 41 frame events, compared there and at the end.
- **`diff_serial`'s deferred probes** (M4.6c1 final review): `bank_find_current` in the DIP bit 1 run with P2's bank bits in neither table (`dip_no_bank`: the alternate table's no-match path, $0309 → $030B), with P2's bank bits $78 (`dip_bank_78`: `bank_p2_table_alt[2]` = $78, so $0304 `BEQ` taken at X = 2, `bank_current` = 2, where `bank_p2_table` would give 5: the probe that tells the tables apart), and `bank_safe_char` with A = $20 (`safe_space`: $02EB `BCS` taken, the space kept). Each compared ROM against ROM in the self-check and ROM against C on the corpus.
- **With the main loop ROM**, every corpus text and extra run compares equal (Step 2): the comparison and the injections are right before any boot routine is C.

**Files:**
- Create: `tests/test_diff_main.c`
- Modify: `tests/suites.h`, `tests/diff_state.h`, `tests/diff_state.c`, `tests/test_diff_serial.c`

**Interfaces:**
- Consumes: Task 1's `rom_engine_run_input`, `hybrid_engine_run_input`, `engine_input`, `engine_watch`; `sfr_same` (`diff_state.c`), `machine_hook`, `machine_hook_ramcode`, `machine_default_config`; `CORPUS_NAMES`; the probe table and `parallel()` of `test_diff_serial.c`.
- Produces (Tasks 5–6 add runs to it):

```c
/* tests/diff_state.h */
bool diff_same_sfr(const m50734 *a, const m50734 *b, char *why, size_t n);   /* diff_same's SFR part (was sfr_same) */
/* tests/test_diff_main.c: suite_diff_main, the 40th suite; extra_run { name, text, p4, p2, p0, inject, at (AT_IDLE,
   AT_SPEECH), n, target, target_ramcode, want }, k_extra[] */
/* tests/test_diff_serial.c: the probes dip_no_bank, dip_bank_78, safe_space */
```

No name collides: `grep -rn "diff_main\|diff_same_sfr\|extra_run\|dip_no_bank\|dip_bank_78\|safe_space" src tests` prints nothing before this task.

- [ ] **Step 1: Write the tests**

**Create `tests/test_diff_main.c`:**

```c
/* The end-to-end differential test (M4.6c2 spec 4.1): the ROM engine and the hybrid engine with every replacement
   ("the all-C hybrid") on the same input, compared at every input idle and at every frame event. Each run goes through
   the ROM engine first, whose watch (engine_watch) records each event in order: its kind (an idle or a frame) and a
   snapshot of the machine: all RAM but the SFR bytes and the stack page $0100-$01FF (boot_stack: with the main loop
   in C the machine stack no longer holds the ROM's return addresses, so interrupts push onto other bytes; user
   decision), the SFR state (diff_same_sfr), the DSP outputs (the port writes, the raw frame position and bytes, the
   reset pulses and level), the frame counts (frames_produced, raw_frames) and the I flag. The same input then goes
   through the hybrid engine, whose watch compares each of its events with the ROM's event of the same index: the same
   kind and the same snapshot; the first difference ends the comparison of that run. At the end of a run both engines
   must have the same status (MACHINE_IDLE, or MACHINE_CYCLE_LIMIT where the ROM never idles again) and the same number
   of events, and their final states are compared as one more event.
   The runs: every corpus text, and extra runs whose configuration (the P4 DIP switches, the P2 and P0 input pins) or
   injection (made to both engines at the same event: an idle or a frame event, before the snapshot is taken) reaches
   what no corpus text does; each names a ROM address its ROM run must execute (a hook on the ROM engine only), so that
   every extra run is seen to reach its path. Parallel mode (DIP bit 0) never idles: the run ends at the boot phase's
   deadline in both engines, after the boot announcement's frame events. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "diff_state.h"
#include "engine/hybrid_engine.h"
#include "engine/thunks.h"
#include "firmware/bank_addr.h"

#define P4_DIP_PARALLEL  0x01   /* the P4 input's DIP bit 0: the parallel interface */
#define P4_DIP_ALT       0x02   /* DIP bit 1: the alternative bank table and the self-test input */
#define P2_SELFTEST      0x02   /* P2.1: the self-test input (boot_selftest $C60B BBC1 P2) */
#define P0_HOST_LINE     0x04   /* P0.2: the host's handshake line */

/* One event of a run. */
typedef struct {
    bool idle, frame;                 /* an input idle, a frame event, or neither: the end of the run */
    uint8_t ram[MACHINE_RAM_SIZE];
    m50734 sfr;
    uint64_t port_writes, dsp_resets, frames_produced, raw_frames;
    unsigned raw_pos;
    uint8_t raw_buf[APOLLO_FRAME_BYTES];
    bool dsp_reset_level, i;
} snapshot;

typedef enum { AT_IDLE, AT_SPEECH } inject_at;   /* the n-th idle; the n-th frame event after the first idle */

/* An extra run: its input, its configuration, an injection at the n-th idle or frame event (inject NULL: none), the
   ROM address its ROM run must execute and the status both engines must end with. */
typedef struct {
    const char *name, *text;
    uint8_t p4, p2, p0;               /* ORed into the default input pins of P4, P2, P0 */
    void (*inject)(machine *m);
    inject_at at;
    unsigned n;
    uint16_t target;
    bool target_ramcode;
    machine_status want;
} extra_run;

static machine g_m;
static snapshot *g_rom_events;        /* the ROM run's events: g_nrom of them, room for g_cap */
static size_t g_nrom, g_cap;
static snapshot g_cur;
static const extra_run *g_extra;      /* the run's injection, or NULL */
static unsigned g_idles, g_frames;    /* the events of the engine running */
static unsigned g_speech_frames;      /* its frame events after the first idle */
static bool g_rom_side;               /* recording (the ROM engine) or comparing (the hybrid) */
static size_t g_nevents;              /* the hybrid run's events so far */
static bool g_differs;
static char g_why[200];
static unsigned g_target_hits, g_mismatches, g_runs, g_total_idles, g_total_frames;

static void take(snapshot *s, const machine *m, bool idle, bool frame)
{
    s->idle = idle;
    s->frame = frame;
    memcpy(s->ram, m->ram, sizeof s->ram);
    s->sfr = m->sfr;
    s->port_writes = m->port_writes;
    s->dsp_resets = m->dsp_resets;
    s->frames_produced = m->frames_produced;
    s->raw_frames = m->raw_frames;
    s->raw_pos = m->raw_pos;
    memcpy(s->raw_buf, m->raw_buf, sizeof s->raw_buf);
    s->dsp_reset_level = m->dsp_reset_level;
    s->i = (m->cpu.p & CPU740_I) != 0;
}

/* The first difference between the ROM's snapshot a and the hybrid's b, or false. */
static bool differs(const snapshot *a, const snapshot *b, char *why, size_t n)
{
    if (a->idle != b->idle) {
        snprintf(why, n, "event kind: ROM %s, C %s", a->idle ? "idle" : "frame", b->idle ? "idle" : "frame");
        return true;
    }
    if (a->i != b->i) {
        snprintf(why, n, "I: ROM %d, C %d", a->i, b->i);
        return true;
    }
    if (!diff_same_sfr(&a->sfr, &b->sfr, why, n))
        return true;
    if (a->port_writes != b->port_writes || a->raw_pos != b->raw_pos ||
        memcmp(a->raw_buf, b->raw_buf, sizeof a->raw_buf) != 0 || a->dsp_resets != b->dsp_resets ||
        a->dsp_reset_level != b->dsp_reset_level) {
        snprintf(why, n, "DSP port writes/raw position/resets: ROM %llu/%u/%llu, C %llu/%u/%llu (or the raw bytes or "
                 "the reset level)", (unsigned long long)a->port_writes, a->raw_pos,
                 (unsigned long long)a->dsp_resets, (unsigned long long)b->port_writes, b->raw_pos,
                 (unsigned long long)b->dsp_resets);
        return true;
    }
    if (a->frames_produced != b->frames_produced || a->raw_frames != b->raw_frames) {
        snprintf(why, n, "frames produced/raw: ROM %llu/%llu, C %llu/%llu", (unsigned long long)a->frames_produced,
                 (unsigned long long)a->raw_frames, (unsigned long long)b->frames_produced,
                 (unsigned long long)b->raw_frames);
        return true;
    }
    for (uint32_t addr = 0; addr < MACHINE_RAM_SIZE; addr++) {
        if ((addr >= M50734_SFR_FIRST && addr <= 0xFF) || (addr >= 0x100 && addr <= 0x1FF))
            continue;
        if (!a->frame && addr >= ISR_TICK_DIV && addr <= ISR_WORDS_LEFT)
            continue;   /* the timer interrupt's per-word counters: how far it got before isr_speech_stop */
        if (a->ram[addr] != b->ram[addr]) {
            snprintf(why, n, "RAM $%04X: ROM %02X, C %02X", (unsigned)addr, a->ram[addr], b->ram[addr]);
            return true;
        }
    }
    return false;
}

/* One event of the engine running: the injection due at it, then the snapshot recorded (ROM) or compared (C). */
static void event(machine *m, bool idle)
{
    unsigned k = idle ? g_idles++ : g_frames++;
    unsigned at = idle ? k : g_idles ? g_speech_frames++ : ~0u;
    if (g_extra && g_extra->inject && (g_extra->at == AT_IDLE) == idle && g_extra->n == at)
        g_extra->inject(m);
    if (g_rom_side) {
        if (g_nrom == g_cap) {
            g_cap = g_cap ? g_cap * 2 : 1024;
            g_rom_events = realloc(g_rom_events, g_cap * sizeof *g_rom_events);
            if (!g_rom_events)
                abort();
        }
        take(&g_rom_events[g_nrom++], m, idle, !idle);
        return;
    }
    size_t i = g_nevents++;
    if (g_differs)
        return;
    take(&g_cur, m, idle, !idle);
    char why[160];
    if (i >= g_nrom) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "event %zu: the C has more events than the ROM (%zu)", i, g_nrom);
    } else if (differs(&g_rom_events[i], &g_cur, why, sizeof why)) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "event %zu (%s %u): %s", i, idle ? "idle" : "frame", k, why);
    }
}

static void on_frame(void *user, machine *m)
{
    (void)user;
    event(m, false);
}

static void on_idle(void *user, machine *m)
{
    (void)user;
    event(m, true);
}

static bool count_target(machine *m, void *user)
{
    (void)m;
    (void)user;
    g_target_hits++;
    return false;
}

/* The ROM run's hook at the extra run's target. */
static void install_target(machine *m, void *user)
{
    const extra_run *x = user;
    CHECK(x->target_ramcode ? machine_hook_ramcode(m, x->target, count_target, NULL)
                            : machine_hook(m, x->target, count_target, NULL));
}

/* One input on both engines; x: the extra run (NULL: a corpus text). */
static void run_pair(const uint8_t *rom, size_t len, const char *name, const char *text, const extra_run *x)
{
    machine_config cfg;
    machine_default_config(&cfg);
    if (x) {
        cfg.port_in[4] |= x->p4;
        cfg.port_in[2] |= x->p2;
        cfg.port_in[0] |= x->p0;
    }
    engine_input in = { (const uint8_t *)text, strlen(text), true };
    engine_watch w = { on_frame, on_idle, NULL };
    frame_list fl = { 0 };
    g_extra = x;

    g_rom_side = true;
    g_nrom = 0;
    g_idles = g_frames = g_speech_frames = 0;
    g_target_hits = 0;
    machine_status rom_st = rom_engine_run_input(&g_m, rom, len, &cfg, x ? install_target : NULL, (void *)x, &in,
                                                 false, &fl, NULL, &w);
    machine_clear_hooks(&g_m);
    take(&g_cur, &g_m, false, false);   /* the end of the run, one more event */
    snapshot rom_end = g_cur;
    unsigned rom_idles = g_idles, rom_frames = g_frames;
    frame_list_free(&fl);

    g_rom_side = false;
    g_nevents = 0;
    g_idles = g_frames = g_speech_frames = 0;
    g_differs = false;
    machine_status c_st = hybrid_engine_run_input(&g_m, rom, len, &cfg, NULL, &in, false, NULL, NULL, NULL, &w);
    char why[160];
    if (!g_differs && c_st != rom_st) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "status: ROM %d, C %d", (int)rom_st, (int)c_st);
    }
    if (!g_differs && g_nevents != g_nrom) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "events: ROM %zu, C %zu", g_nrom, g_nevents);
    }
    take(&g_cur, &g_m, false, false);
    if (!g_differs && differs(&rom_end, &g_cur, why, sizeof why)) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "the end: %s", why);
    }
    if (g_differs && g_mismatches++ < 10)
        fprintf(stderr, "  %s: %s\n", name, g_why);
    g_runs++;
    g_total_idles += rom_idles;
    g_total_frames += rom_frames;
    if (x) {
        printf("  %-14s %s, %u idles, %u frame events, $%04X reached %u\n", x->name,
               rom_st == MACHINE_IDLE ? "idle" : rom_st == MACHINE_CYCLE_LIMIT ? "cycle limit" : "fault", rom_idles,
               rom_frames, x->target, g_target_hits);
        CHECK_EQ(rom_st, x->want);
        CHECK(g_target_hits > 0);
    } else {
        CHECK_EQ(rom_st, MACHINE_IDLE);
    }
}

/* ---- the extra runs' injections ---- */

/* At the end of boot: the running engine's minor version altered, so @=1 finds the bank's header different: the
   cold entry (bank_enter $0228). */
static void inj_cold(machine *m) { m->ram[BANK_VER_MINOR]++; }
/* A status byte due (as isr_host_line_high leaves it, $C3F0) with the host line low, as at boot; or with the line
   raised too (the pin only: the machine raises no INT4). */
static void inj_status_due(machine *m) { m->ram[SER_STATUS_DUE] = 1; }
static void inj_status_line_high(machine *m)
{
    m->ram[SER_STATUS_DUE] = 1;
    m->sfr.port_in[0] |= P0_HOST_LINE;
}

static const extra_run k_extra[] = {
    /* @=1: the main bank, its version equal: the warm entry $FFD4, the rest of the clause dropped */
    { "warm", "@=1 One. Two.", 0, 0, 0, NULL, AT_IDLE, 0, BANK_ENTRY_WARM, false, MACHINE_IDLE },
    /* @=1 with the version altered at the end of boot: the cold entry $FFD0 (boot_read_dip, boot_init_settings) */
    { "cold", "@=1 One. Two.", 0, 0, 0, inj_cold, AT_IDLE, 0, BANK_ENTRY_COLD, false, MACHINE_IDLE },
    /* DIP bit 1: bank_select reads the alternative table at boot (bank_scan) and at @=5 (bank_enter) */
    { "dip_bank", "@=5 Five.", P4_DIP_ALT, 0, 0, NULL, AT_IDLE, 0, BANK_ENTER, true, MACHINE_IDLE },
    /* the self-test failing: DIP bit 1 and the P2.1 input high; the three long buzzes at boot ($A7AB) */
    { "selftest_fail", "Hello.", P4_DIP_ALT, P2_SELFTEST, 0, NULL, AT_IDLE, 0, 0xA7AB, false, MACHINE_IDLE },
    /* a status byte due and the host line high at the end of boot: after the empty clause of the CR, boot_get_clause
       sends it ($A6D7: a NAK, no reply pending), a pass that changed state, then idles. (A longer input cannot follow:
       the busy line P0.5 that ser_tx_service sets after the send is not the level the machine delivers bytes at.) */
    { "status_sent", "", 0, 0, 0, inj_status_line_high, AT_IDLE, 0, 0xA6D7, false, MACHINE_IDLE },
    /* a query (@L, its reply queued) with a status byte due from the speech's second frame event on and the host line
       low: the reply waits on the host line; par_play's ser_tx_service and then boot_get_clause's ($A6D7) send
       nothing, and boot_get_clause loops with nothing changed until the speaking phase's deadline */
    { "status_waits", "@L One.", 0, 0, 0, inj_status_due, AT_SPEECH, 1, 0xA6D7, false, MACHINE_CYCLE_LIMIT },
    /* parallel mode: isr_idle returns at once ($C603 by its BBS0), no idle; the boot announcement, then the deadline */
    { "parallel", "Hello.", P4_DIP_PARALLEL, 0, 0, NULL, AT_IDLE, 0, 0xC603, false, MACHINE_CYCLE_LIMIT },
};

static const char *const k_corpus[] = { CORPUS_NAMES };

static void strip_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

void suite_diff_main(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    g_mismatches = g_runs = g_total_idles = g_total_frames = 0;
    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char path[128];
        snprintf(path, sizeof path, "tests/corpus/%s.txt", k_corpus[c]);
        char *text = test_read_file(path);
        CHECK(text != NULL);
        if (!text)
            continue;
        strip_newline(text);
        run_pair(rom, len, k_corpus[c], text, NULL);
        free(text);
    }
    printf("  corpus: %u texts, %u idles, %u frame events compared\n", g_runs, g_total_idles, g_total_frames);
    for (size_t i = 0; i < sizeof k_extra / sizeof k_extra[0]; i++)
        run_pair(rom, len, k_extra[i].name, k_extra[i].text, &k_extra[i]);
    printf("  %u runs, %u idles, %u frame events compared in all\n", g_runs, g_total_idles, g_total_frames);
    CHECK_EQ(g_mismatches, 0);
    free(g_rom_events);
    g_rom_events = NULL;
    g_nrom = g_cap = 0;
}
```

**`tests/suites.h`**: replace

```c
SUITE(suite_diff_play)
SUITE(suite_diff_serial)
SUITE(suite_golden_hybrid)
SUITE(suite_fw_tn)
```

with

```c
SUITE(suite_diff_play)
SUITE(suite_diff_serial)
SUITE(suite_diff_main)
SUITE(suite_golden_hybrid)
SUITE(suite_fw_tn)
```

**`tests/diff_state.h`**: replace

```c
   Otherwise false, with the first difference in why. */
bool diff_same(const machine *a, const machine *b, uint8_t outputs, char *why, size_t n);

/* A routine whose copy reset the stack (bank_enter's LDX #$FF / TXS) is compared where bank_enter jumps, at the bank
```

with

```c
   Otherwise false, with the first difference in why. */
bool diff_same(const machine *a, const machine *b, uint8_t outputs, char *why, size_t n);
/* The SFR model state of diff_same alone (diff_main, M4.6c2 spec 4.1): true when a (the ROM's) and b (the C's) agree,
   else false with the first difference in why. */
bool diff_same_sfr(const m50734 *a, const m50734 *b, char *why, size_t n);

/* A routine whose copy reset the stack (bank_enter's LDX #$FF / TXS) is compared where bank_enter jumps, at the bank
```

**`tests/diff_state.c`**: replace

```c
}

static bool sfr_same(const m50734 *a, const m50734 *b, char *why, size_t n)
{
    for (int p = 0; p < 4; p++) {
```

with

```c
}

bool diff_same_sfr(const m50734 *a, const m50734 *b, char *why, size_t n)
{
    for (int p = 0; p < 4; p++) {
```

**`tests/diff_state.c`**: replace

```c
        return false;
    }
    if (!sfr_same(&a->sfr, &b->sfr, why, n) || !dsp_same(a, b, why, n))
        return false;
    for (uint32_t addr = 0; addr < MACHINE_RAM_SIZE; addr++) {
```

with

```c
        return false;
    }
    if (!diff_same_sfr(&a->sfr, &b->sfr, why, n) || !dsp_same(a, b, why, n))
        return false;
    for (uint32_t addr = 0; addr < MACHINE_RAM_SIZE; addr++) {
```

**`tests/test_diff_serial.c`**: replace

```c
   host-line interrupt, the ring page ends; the parallel handshake of isr_par_ready and isr_par_hold, a frame boundary
   of the timer interrupt with the frame flag still set; a bank's cold entry, DIP bit 1, an ID byte below $20 and P2
   bank bits in neither bank table) are compared with the ROM through synthetic calls from real states. Every corpus
   file and the boot run go through the ROM engine, and bank_select once more with the P4 input's DIP bit 1 set (P2.1
   low, so boot_selftest passes); at every entry of isr_int1 $C2B4 (a received byte: the CPU has just entered the
```

with

```c
   host-line interrupt, the ring page ends; the parallel handshake of isr_par_ready and isr_par_hold, a frame boundary
   of the timer interrupt with the frame flag still set; a bank's cold entry, DIP bit 1, an ID byte below $20 and P2
   bank bits in neither bank table; M4.6c2 spec 4.3: P2 bank bits in neither table and P2 bank bits $78 under DIP bit
   1, which the two tables tell apart, and a space, the first ID byte bank_safe_char keeps) are compared with the ROM
   through synthetic calls from real states. Every corpus
   file and the boot run go through the ROM engine, and bank_select once more with the P4 input's DIP bit 1 set (P2.1
   low, so boot_selftest passes); at every entry of isr_int1 $C2B4 (a received byte: the CPU has just entered the
```

**`tests/test_diff_serial.c`**: replace

```c
    m->sfr.port_latch[2] = (uint8_t)((m->sfr.port_latch[2] & ~P2_BANK_BITS) | P2_NO_BANK);
}
/* bank_safe_char with a control code */
static void inj_safe_char(machine *m) { m->cpu.a = 0x1F; }

static probe g_probes[] = {
```

with

```c
    m->sfr.port_latch[2] = (uint8_t)((m->sfr.port_latch[2] & ~P2_BANK_BITS) | P2_NO_BANK);
}
/* bank_safe_char with a control code; with a space, the first character it keeps */
static void inj_safe_char(machine *m) { m->cpu.a = 0x1F; }
static void inj_safe_space(machine *m) { m->cpu.a = 0x20; }
/* bank_find_current with P2's bank bits $78: bank 5 in bank_p2_table, bank 2 in bank_p2_table_alt (DIP bit 1) */
static void inj_bank_78(machine *m)
{
    m->sfr.port_latch[2] = (uint8_t)((m->sfr.port_latch[2] & ~P2_BANK_BITS) | P2_BANK_5);
}

static probe g_probes[] = {
```

**`tests/test_diff_serial.c`**: replace

```c
    { "no_bank",     AT_FIND_CURRENT, "bank_find_current", BANK_FIND_CURRENT, false, inj_no_bank,    0x0309, 0x030B },
    { "safe_char",   AT_SAFE_CHAR,    "bank_safe_char",    BANK_SAFE_CHAR,    false, inj_safe_char,  0x02EB, 0x02ED },
};
#define PROBE_COUNT (sizeof g_probes / sizeof g_probes[0])
```

with

```c
    { "no_bank",     AT_FIND_CURRENT, "bank_find_current", BANK_FIND_CURRENT, false, inj_no_bank,    0x0309, 0x030B },
    { "safe_char",   AT_SAFE_CHAR,    "bank_safe_char",    BANK_SAFE_CHAR,    false, inj_safe_char,  0x02EB, 0x02ED },
    { "dip_no_bank", AT_FIND_CURRENT, "bank_find_current", BANK_FIND_CURRENT, false, inj_no_bank,    0x0309, 0x030B, false,
      true },
    { "dip_bank_78", AT_FIND_CURRENT, "bank_find_current", BANK_FIND_CURRENT, false, inj_bank_78,    0x0304, 0x030D, false,
      true },
    { "safe_space",  AT_SAFE_CHAR,    "bank_safe_char",    BANK_SAFE_CHAR,    false, inj_safe_space, 0x02EB, 0x02EF },
};
#define PROBE_COUNT (sizeof g_probes / sizeof g_probes[0])
```

- [ ] **Step 2: Run them (the main loop still ROM)**

Run: `cmd //c ".\build.bat test diff_main"`, then `build/apollo2_tests.exe diff_serial`.
Expected: no warnings, and

```
  corpus: 47 texts, 94 idles, 25167 frame events compared
  warm           idle, 2 idles, 101 frame events, $FFD4 reached 1
  cold           idle, 2 idles, 101 frame events, $FFD0 reached 1
  dip_bank       idle, 2 idles, 41 frame events, $0210 reached 1
  selftest_fail  idle, 2 idles, 365 frame events, $A7AB reached 1
  status_sent    idle, 2 idles, 41 frame events, $A6D7 reached 1
  status_waits   cycle limit, 1 idles, 100 frame events, $A6D7 reached 4531687
  parallel       cycle limit, 0 idles, 41 frame events, $C603 reached 806094
  54 runs, 105 idles, 25957 frame events compared in all
suite_diff_main              ok
1 suite(s), 0 failure(s), 0 skip(s)
```

(22 s in the proof run), and `diff_serial` with its 37 probes: the 34 of Task 1 unchanged, and the three new ones in the self-check and on the corpus:

```
  self-check (ROM against ROM, queries, bank_select, bank_select with DIP bit 1):
...
  dip_no_bank      bank_find_current      3 calls compared (ROM), $0309 -> $030B in 3
  dip_bank_78      bank_find_current      3 calls compared (ROM), $0304 -> $030D in 3
  safe_space       bank_safe_char        42 calls compared (ROM), $02EB -> $02EF in 42
  corpus, boot and bank_select with DIP bit 1:
...
  dip_no_bank      bank_find_current      3 calls compared (C), $0309 -> $030B in 3
  dip_bank_78      bank_find_current      3 calls compared (C), $0304 -> $030D in 3
  safe_space       bank_safe_char       686 calls compared (C), $02EB -> $02EF in 686
suite_diff_serial            ok
1 suite(s), 0 failure(s), 0 skip(s)
```

A mismatch here is the comparison or an injection wrong (the boot routines are all ROM): fix the test, never loosen the comparison.

- [ ] **Step 3: Run the full suite**

Run: `cmd //c ".\build.bat test"` (in the background)
Expected:

- `40 suite(s), 0 failure(s), 0 skip(s)` (840 s in the proof run);
- `diff_main` and `diff_serial` as in Step 2;
- `firmware_data` (49,092), `diff_lts`, `diff_play`, `golden_hybrid` (111 modes), `suite_golden` and `suite_synth` exactly as in Task 2 Step 5.

- [ ] **Step 4: Commit**

```bash
git add tests/test_diff_main.c tests/suites.h tests/diff_state.h tests/diff_state.c tests/test_diff_serial.c
git commit -m "diff_main: the ROM engine against the hybrid at every input idle and frame event (RAM but the stack page, SFRs, DSP outputs, frame counts, the I flag), on the corpus and seven extra runs (warm and cold re-entry, DIP bit 1, the self-test failing, a status byte sent and one that waits, parallel mode); diff_serial's deferred DIP probes and a space for bank_safe_char; proved with the main loop ROM (M4.6c2 4.1, 4.3)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 4: The boot helpers in C: `boot_read_dip` $ADE0, `boot_init_ports` $AC22, `boot_init_settings` $ADEB, `boot_clear_input_state` $ABF0, `boot_set_defaults` $A742, `boot_load_prosody_defaults` $A7F0, `boot_init_baud` $AC7E, `boot_init_host_lines` $AC68, and `cmd_say_phonetic` $A876, which `boot_init_baud` calls; their hooks (spec §2.1–§2.3, §2.6, §4.2, §4.4, §4.8 item 4)

**ROM facts checked / what was verified while proving the plan** (`rom_ADE0_boot_read_dip.txt`, `rom_AC22_boot_init_ports.txt`, `rom_ADEB_boot_init_settings.txt`, `rom_ABF0_boot_clear_input_state.txt`, `rom_A742_boot_set_defaults.txt`, `rom_A7F0_boot_load_prosody_defaults.txt`, `rom_AC7E_boot_init_baud.txt`, `rom_AC68_boot_init_host_lines.txt`, `rom_A876_cmd_say_phonetic.txt`, `rom_A868_cmd_put_phonetic.txt`, `rom_A7DF_cmd_clear_formant.txt`, `rom_A78C_cmd_apply_speaker.txt`, `rom_A61C_boot_reset.txt`, `rom_C3EB_isr_host_line_high.txt`, `rom_C3F4_isr_host_line_low.txt`, `functions.txt`; a scan of the ROM image for the JSR/JMP operands and vectors of the 21 routines, whose result is `boot_addr.h`'s caller list):
- **The callers**: each helper has the callers `boot_addr.h` names and no other (the scan agrees with `functions.txt`): `boot_read_dip` $A645 and $A665 (the cold re-entry), `boot_init_settings` $A64E and $A668, the rest one each; `cmd_say_phonetic` only `boot_init_baud`'s three `JSR`s at $AC96, $ACAC and $ACBF, after a `CPX #1 / BNE`, and boot_reset calls `boot_init_baud` with X = 0 ($A65A `LDX #0`): unreachable. The C `boot_init_baud` calls it, so it is ported here (the spec's outline puts it in item 6); `cmd_put_phonetic` $A868, static in `cmd_set.c` since M4.2, becomes public for it (`cmd_handlers.h`).
- **SFR order**: `boot_read_dip` writes AD_CTRL ($ADE0 `LDM`), then reads P4 once (`AND #$0F`). `boot_init_ports` tests `boot_dip` bit 0 three times ($AC22, $AC3C, $AC4D, RAM): the P0 block (serial: P0_FUNC $48, P0_DIR $78, P0 $20; parallel: 0, 8, 0), the P1 block (serial: P1_LATCH 0, P1_DIR 0; parallel: P1 1, P1_DIR $FF), then the same `LDM #$70,P2` / `LDM #$FD,P2_DIR` in both branches ($AC50, $AC58) and P3_DIR $F9, P23_FUNC 0, P3 $78 (also `boot_reset`'s first three `LDM`s): ten writes, no read; A = 0 at the end in both modes ($AC25, $AC47). `boot_init_baud` reads P3 once per `BBS`: $AC81 bit 1, then $AC84 or $AC9A bit 2, and writes UART_BAUD once: $02 (both jumpers low, the machine's default: 19200 by elimination), $BF (P3.1), $2F (P3.2), $05 (both, no text). `boot_init_host_lines`: UART_CTRL $D1, then A = $4C to P0_FUNC, one read of P0 (`BBC2`), then `CLB5 P0` and `isr_host_line_high`, or `SEB5 P0` and `isr_host_line_low` (C since M4.6a).
- **RAM**: `boot_clear_input_state` stores A = 0 to ten bytes ($ABF2–$AC04: ser_abort, boot_speaking, ser_rx_idle, ser_rx_idle_saved, cmd_index_count, ser_tx_pending, $07, ser_tx_count, isr_par_state, ser_cmd_mode; $07 is unnamed), `cmd_index_reset` = 1, then the low bytes of the four ring pointers and their pages ($39, $39, $21, $21). `boot_set_defaults` writes the settings in the order of the dump, `cmd_char` = `ser_cmd_char` = '@', `$6B` = 4, ten bytes cleared ending with $1827 (now `FW_CLAUSE_CARRY`), then `cmd_clear_formant` ($2014–$201E; it keeps A and Y) and `boot_load_prosody_defaults` (26 bytes, Y = $1A, A = the last, 0). `boot_init_settings` reads the bank header's $FFE0 and $FFE1 through `fw_rom8` (converted in M4.6c1; the main bank is selected wherever it runs).
- **The exit registers** each row's comment names, checked at every caller: A, X and Y are defined at every exit and declared for all nine; no caller reads a flag. `cmd_say_phonetic` waits as `boot_loop_play` does: a waits row, which `diff_lts` leaves out; `diff_play` now leaves the main loop's waits rows to `diff_main` (`diff_is_main_row`), and `golden_hybrid` runs no thunk of it alone (`diff_is_unreached`: no corpus text, probe or extra run reaches it).
- **`diff_lts`**: the eight helpers join `k_every_call` (spec §4.2). Each runs once per boot: 52 calls each (the 47 corpus texts, the boot run and the 4 extra texts), all compared; `DIFF_MAX` 136 holds the 131 rows of Task 6.
- **The unreachable path pinned** (spec §4.4): `boot_init_baud` with X = 1 for each of the three texts, with the power-up settings: `tn_rule_ptr` = the text, `cmd_put_phonetic` appends it ("tRE HuNDRxD ", "TWeLV HuNDRxD \r", " "), and `pros_make_segments` and `boot_loop_play` speak it (the unit test's mute ends the playback at the timer start). With both jumpers, no text and nothing spoken.

**Files:**
- Create: `src/firmware/boot_addr.h`, `src/firmware/boot.h`, `src/firmware/boot.c`
- Modify: `src/firmware/fw.h`, `src/firmware/cmd_set.c`, `src/firmware/cmd_handlers.h`, `src/firmware/data/fw_data.h`, `src/engine/thunks.c`, `tests/test_fw_boot.c`, `tests/diff_state.h`, `tests/diff_state.c`, `tests/test_diff_lts.c`, `tests/test_diff_play.c`, `tests/test_fw_par.c`, `tests/test_golden_hybrid.c`

**Interfaces:**
- Consumes: Task 1's stub host in `test_fw_boot.c`; Task 2's `BOOT_PROSODY_DEFAULTS`; `cmd_apply_speaker` and `cmd_clear_formant` (`cmd_apply.h`), `isr_host_line_high`/`_low` (`isr_serial.h`), `pros_make_segments`, `boot_loop_play`, `tn_append_phonetic`; the SFR names of `ser.h` and `isr_dsp.h`; `BANK_HDR_VERSION` (`bank_addr.h`).
- Produces (Tasks 5–6 call them):

```c
/* src/firmware/boot.h (each void fn(fw *f, fw_regs *r)) */
boot_read_dip, boot_init_ports, boot_init_settings, boot_clear_input_state, boot_set_defaults,
boot_load_prosody_defaults, boot_init_baud, boot_init_host_lines, cmd_say_phonetic;
#define SFR_AD_CTRL 0xE9, SFR_P4 0xEB, SFR_P3_DIR 0xEF, SFR_P2_DIR 0xF1, SFR_P0_FUNC 0xF5, SFR_P0_DIR 0xF7,
        SFR_UART_BAUD 0xFB, DIP_PARALLEL 0x01, DIP_SELFTEST 0x02
/* src/firmware/boot_addr.h: ROM_BOOT_RESET ... ROM_ISR_UNUSED (the 16 routines not in bank_addr.h), BOOT_GET_CLAUSE_LOOP,
   BOOT_CLI_WARM, BOOT_CLI_EMPTY, BOOT_SEI_READ, BOOT_CLI_READ */
/* src/firmware/fw.h */  #define FW_CLAUSE_CARRY 0x1827
/* src/firmware/data/fw_data.h */  #define BOOT_BAUD_TEXT_19200 0xACC7, BOOT_BAUD_TEXT_300 0xACCA, BOOT_BAUD_TEXT_1200 0xACD8
/* src/firmware/cmd_handlers.h */  uint8_t cmd_put_phonetic(fw *f, uint8_t x);
/* src/engine/thunks.c: boot_regs(), the nine rows */
/* tests/diff_state.h */  bool diff_is_main_row(const char *name); bool diff_is_unreached(const char *name);
/* tests/test_fw_boot.c: check_row(), check_regs(), REGS */
```

No name collides: `grep -rnw "boot_read_dip\|boot_init_ports\|boot_init_settings\|boot_clear_input_state\|boot_set_defaults\|boot_load_prosody_defaults\|boot_init_baud\|boot_init_host_lines\|cmd_say_phonetic\|FW_CLAUSE_CARRY\|BOOT_BAUD_TEXT_300\|SFR_P4\|SFR_AD_CTRL\|SFR_UART_BAUD\|DIP_PARALLEL\|diff_is_main_row\|diff_is_unreached" src tests` prints only comments before this task (the ROM routines' names in thunk comments, the address headers, `boot_data.c` and test comments).

- [ ] **Step 1: Write the failing tests**

`test_fw_boot.c` gains the helpers' tests (RAM writes, SFR order, exit registers, the speech of the baud rate with X = 1, the rows); the stub gives P2 the main bank (as `test_fw_tn.c` and `test_fw_par.c`) and a frame flag and a mute at its waits (as `test_fw_tn.c`'s). `diff_lts` compares the helpers at every call; `diff_play` leaves the main loop's waits rows to `diff_main`; `golden_hybrid` exempts the rows no corpus text reaches; `test_fw_par.c` counts the five frame-loop rows among the waits rows.

**`tests/test_fw_boot.c`**: replace

```c
#include <string.h>
#include "test.h"
#include "firmware/fw.h"
#include "firmware/data/fw_data.h"

```

with

```c
#include <string.h>
#include "test.h"
#include "engine/thunks.h"
#include "firmware/boot.h"
#include "firmware/boot_addr.h"
#include "firmware/cmd_apply.h"
#include "firmware/isr_dsp.h"
#include "firmware/ser.h"
#include "firmware/data/fw_data.h"

```

**`tests/test_fw_boot.c`**: replace

```c
}

static fw_wait_result stub_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    (void)ctx;
    log_add(EV_WAIT, at, (uint8_t)kind);
    return g_wait_result;
}
```

with

```c
}

/* Logs the wait. A wait for an interrupt is one handled: the timer interrupt sets isr_frame_flag (a frame is due at
   every wait). isr_timer_start's first wait sets ser_abort bit 0 (a mute as the DSP starts), so a boot_loop_play
   plays no segment: par_play goes from its first pass to its three rest frames. */
static fw_wait_result stub_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    (void)ctx;
    log_add(EV_WAIT, at, (uint8_t)kind);
    if (kind == FW_WAIT_INTERRUPT)
        g_ram[ISR_FRAME_FLAG] = 1;
    if (kind == FW_WAIT_TIMER2)
        g_ram[SER_ABORT] |= 1;
    return g_wait_result;
}
```

**`tests/test_fw_boot.c`**: replace

```c
}

/* Zeroed RAM and SFRs, the stub host, the converted image, nothing logged, every wait returning. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    memset(g_sfr, 0, sizeof g_sfr);
    memset(&g_host, 0, sizeof g_host);
    g_host.ctx = &g_f;
```

with

```c
}

/* Zeroed RAM and SFRs but P2, the stub host, the converted image, nothing logged, every wait returning. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    memset(g_sfr, 0, sizeof g_sfr);
    g_sfr[SFR_P2] = P2_MAIN_BANK;   /* the main ROM selected, as on the board: SEB/CLB of P2 keep it */
    memset(&g_host, 0, sizeof g_host);
    g_host.ctx = &g_f;
```

**`tests/test_fw_boot.c`**: replace

```c
}

void suite_fw_boot(void)
{
    CHECK(fw_image_get() != NULL);
    test_wait_input();
    test_cpu_calls();
}
```

with

```c
}

/* The replacement row of name: its address, outputs and kind (waits, isr, entry). */
static void check_row(const char *name, uint16_t addr, uint8_t outputs, bool waits, bool isr, bool entry)
{
    const fw_replacement *r = fw_replacement_find(name);
    CHECK(r != NULL);
    if (!r)
        return;
    CHECK_EQ(r->addr, addr);
    CHECK_EQ(r->outputs, outputs);
    CHECK_EQ(r->waits, waits);
    CHECK_EQ(r->isr, isr);
    CHECK_EQ(r->entry, entry);
    CHECK(!r->ramcode);
}

/* r is a, x, y. */
static void check_regs(const fw_regs *r, uint8_t a, uint8_t x, uint8_t y)
{
    CHECK_EQ(r->a, a);
    CHECK_EQ(r->x, x);
    CHECK_EQ(r->y, y);
}

#define REGS (FW_OUT_A | FW_OUT_X | FW_OUT_Y)

/* boot_read_dip $ADE0: AD_CTRL = 0 (LDM, before the read), then P4: boot_dip = A = P4 & $0F. X and Y untouched. */
static void test_read_dip(void)
{
    static const event want[] = { { EV_WRITE, SFR_AD_CTRL, 0 }, { EV_READ, SFR_P4, 0xF5 } };
    setup();
    g_sfr[SFR_AD_CTRL] = 0x80;
    g_sfr[SFR_P4] = 0xF5;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    boot_read_dip(&g_f, &r);
    check_log(want, 2);
    check_regs(&r, 0x05, 0x22, 0x33);
    CHECK_EQ(g_ram[BOOT_DIP], 0x05);
    CHECK(!g_f.fault);
}

/* boot_init_ports $AC22, serial (boot_dip bit 0 clear) and parallel: every SFR write in the ROM's order; A = 0 in both
   (the serial path's LDA #$48 is followed by its LDA #0 at $AC47), X and Y untouched; P2's write selects the main
   bank. */
static void test_init_ports(void)
{
    static const event serial[] = {
        { EV_WRITE, SFR_P0_FUNC, 0x48 }, { EV_WRITE, SFR_P0_DIR, 0x78 }, { EV_WRITE, SFR_P0, 0x20 },
        { EV_WRITE, SFR_P1_LATCH, 0x00 }, { EV_WRITE, SFR_P1_DIR, 0x00 }, { EV_WRITE, SFR_P2, 0x70 },
        { EV_WRITE, SFR_P2_DIR, 0xFD }, { EV_WRITE, SFR_P3_DIR, 0xF9 }, { EV_WRITE, SFR_P23_FUNC, 0x00 },
        { EV_WRITE, SFR_P3, 0x78 },
    };
    static const event parallel[] = {
        { EV_WRITE, SFR_P0_FUNC, 0x00 }, { EV_WRITE, SFR_P0_DIR, 0x08 }, { EV_WRITE, SFR_P0, 0x00 },
        { EV_WRITE, SFR_P1, 0x01 }, { EV_WRITE, SFR_P1_DIR, 0xFF }, { EV_WRITE, SFR_P2, 0x70 },
        { EV_WRITE, SFR_P2_DIR, 0xFD }, { EV_WRITE, SFR_P3_DIR, 0xF9 }, { EV_WRITE, SFR_P23_FUNC, 0x00 },
        { EV_WRITE, SFR_P3, 0x78 },
    };
    for (int par = 0; par <= 1; par++) {
        setup();
        g_ram[BOOT_DIP] = (uint8_t)par;
        g_f.bank = 0;
        fw_regs r = { 0x11, 0x22, 0x33, 0 };
        boot_init_ports(&g_f, &r);
        check_log(par ? parallel : serial, 10);
        check_regs(&r, 0x00, 0x22, 0x33);
        CHECK_EQ(g_f.bank, P2_MAIN_BANK);
        CHECK(!g_f.low_window);
        CHECK(!g_f.fault);
    }
}

/* boot_clear_input_state $ABF0: the ten flags and counts 0, cmd_index_reset 1, the output ring pointers $3900 and the
   input ring's $2100; nothing else written, no SFR touched. A = 0, X and Y untouched. */
static void test_clear_input_state(void)
{
    static const uint16_t zeroed[] = {
        SER_ABORT, BOOT_SPEAKING, SER_RX_IDLE, SER_RX_IDLE_SAVED, CMD_INDEX_COUNT, SER_TX_PENDING, 0x0007,
        SER_TX_COUNT, ISR_PAR_STATE, SER_CMD_MODE,
    };
    setup();
    memset(g_ram, 0xA5, 0x4000);
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    boot_clear_input_state(&g_f, &r);
    check_log(NULL, 0);
    check_regs(&r, 0x00, 0x22, 0x33);
    unsigned written = 0;
    for (size_t i = 0; i < sizeof zeroed / sizeof zeroed[0]; i++)
        CHECK_EQ(g_ram[zeroed[i]], 0);
    CHECK_EQ(g_ram[CMD_INDEX_RESET], 1);
    CHECK_EQ(g_ram[SER_TX_READ], 0x00);
    CHECK_EQ(g_ram[SER_TX_READ + 1], 0x39);
    CHECK_EQ(g_ram[SER_TX_WRITE], 0x00);
    CHECK_EQ(g_ram[SER_TX_WRITE + 1], 0x39);
    CHECK_EQ(g_ram[SER_RX_READ], 0x00);
    CHECK_EQ(g_ram[SER_RX_READ + 1], 0x21);
    CHECK_EQ(g_ram[SER_RX_WRITE], 0x00);
    CHECK_EQ(g_ram[SER_RX_WRITE + 1], 0x21);
    for (uint32_t a = 0; a < 0x4000; a++)
        written += g_ram[a] != 0xA5;
    CHECK_EQ(written, 19);   /* the ten flags, cmd_index_reset and the eight bytes of the four pointers */
    CHECK(!g_f.fault);
}

/* boot_set_defaults $A742 and boot_load_prosody_defaults $A7F0 (through it): the power-up @-settings ($A742-$A769),
   cmd_char = ser_cmd_char = '@', the ten cleared ($A76C-$A782, $1827 among them), cmd_formant_ofs and par_ofs
   ($2014-$201E) cleared, pros_params = boot_prosody_defaults. A = its last byte (Z: 0), X untouched, Y = 26; no SFR. */
static void test_set_defaults(void)
{
    static const struct { uint16_t addr; uint8_t value; } want[] = {
        { CMD_PITCH, 8 }, { CMD_PROSODY, 3 }, { CMD_SPEED_LIMIT, 7 }, { CMD_SPEED, 3 }, { CMD_VOLUME, 6 },
        { CMD_WORD_PAUSE, 0 }, { CMD_CHAR, '@' }, { SER_CMD_CHAR, '@' }, { CMD_PITCH_STEP, 8 }, { FW_UTT_CONTENT, 4 },
        { CMD_PHRASE_PAUSE, 0x0B }, { CMD_VOICE, 1 }, { CMD_MARK_SPACE, 0x14 }, { CMD_VOICING, 8 },
        { CMD_SOURCE, 0 }, { CMD_PHONETIC, 0 }, { CMD_SPELL, 0 }, { CMD_PUNCT, 0 }, { CMD_HYPER, 0 },
        { CMD_EV_COUNT, 0 }, { CMD_COUNT, 0 }, { CMD_SOUND_ACTIVE, 0 }, { CMD_SPEAKER, 0 }, { FW_CLAUSE_CARRY, 0 },
    };
    setup();
    memset(g_ram, 0xA5, 0x4000);
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    boot_set_defaults(&g_f, &r);
    check_log(NULL, 0);
    check_regs(&r, 0x00, 0x22, 26);
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++)
        CHECK_EQ(g_ram[want[i].addr], want[i].value);
    for (uint16_t a = CMD_FORMANT_OFS; a <= PAR_OFS; a++)
        CHECK_EQ(g_ram[a], 0);
    CHECK_EQ(g_ram[PAR_OFS_WEIGHT], 0xA5);
    CHECK(memcmp(g_ram + PROS_PARAMS, fw_boot_prosody_defaults, BOOT_PROSODY_PARAMS) == 0);
    CHECK_EQ(g_ram[PROS_PARAMS + BOOT_PROSODY_PARAMS], 0xA5);
    CHECK(!g_f.fault);

    setup();
    r = (fw_regs){ 0x11, 0x22, 0x33, 0 };
    boot_load_prosody_defaults(&g_f, &r);
    check_regs(&r, 0x00, 0x22, 26);
    CHECK_EQ(g_ram[PROS_PARAMS], 180);
    CHECK_EQ(g_ram[PROS_PARAMS + 3], 0xF6);          /* D: -10 */
    CHECK_EQ(g_ram[PROS_PARAMS + 10], 195);          /* K */
    CHECK(!g_f.fault);
}

/* boot_init_settings $ADEB: boot_clear_input_state, boot_set_defaults, then bank_ver_major = $FFE0 (2) and
   bank_ver_minor = $FFE1 (1). A = 1, X untouched, Y = 26. */
static void test_init_settings(void)
{
    setup();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    boot_init_settings(&g_f, &r);
    check_log(NULL, 0);
    check_regs(&r, 0x01, 0x22, 26);
    CHECK_EQ(g_ram[BANK_VER_MAJOR], 2);
    CHECK_EQ(g_ram[BANK_VER_MINOR], 1);
    CHECK_EQ(g_ram[SER_RX_READ + 1], 0x21);
    CHECK_EQ(g_ram[CMD_CHAR], '@');
    CHECK(!g_f.fault);
}

/* boot_init_baud $AC7E with X = 0 (boot_reset's): tn_phon_len = 0, then the jumpers P3.1 and P3.2 (one read of P3 per
   BBS: $AC81 bit 1, then $AC84 or $AC9A bit 2) and UART_BAUD: both low $02, P3.1 $BF, P3.2 $2F, both $05. A, X and Y
   untouched; nothing spoken. */
static void test_init_baud(void)
{
    static const uint8_t p3[] = { 0x00, 0x02, 0x04, 0x06 }, baud[] = { 0x02, 0xBF, 0x2F, 0x05 };
    for (int i = 0; i < 4; i++) {
        event want[] = { { EV_READ, SFR_P3, p3[i] }, { EV_READ, SFR_P3, p3[i] }, { EV_WRITE, SFR_UART_BAUD, baud[i] } };
        setup();
        g_sfr[SFR_P3] = p3[i];
        g_ram[TN_PHON_LEN] = 9;
        fw_regs r = { 0x11, 0x00, 0x33, 0 };
        boot_init_baud(&g_f, &r);
        check_log(want, 3);
        check_regs(&r, 0x11, 0x00, 0x33);
        CHECK_EQ(g_ram[TN_PHON_LEN], 0);
        CHECK_EQ(g_ram[TN_RULE_PTR], 0);
        CHECK(!g_f.fault);
    }
}

/* boot_init_baud with X = 1, unreachable in the ROM (M4.6c2 spec 4.4): the rate spoken through cmd_say_phonetic, each
   of the three texts. With the power-up settings (boot_set_defaults, cmd_apply_speaker, as boot_reset leaves them):
   tn_rule_ptr = the text, which cmd_put_phonetic appends to tn_phonetic (tn_phon_len its length), then
   pros_make_segments and boot_loop_play (C calls C): the DSP started (the waits of isr_timer_start; the stub's mute
   ends par_play at once) and stopped (isr_speech_stop's ICR2 = 0, then the DSP reset's CLB0 of P2 last). Both jumpers:
   no text, nothing spoken. */
static void test_say_rate(void)
{
    static const uint8_t p3[] = { 0x00, 0x02, 0x04, 0x06 };
    static const uint16_t text[] = { BOOT_BAUD_TEXT_19200, BOOT_BAUD_TEXT_300, BOOT_BAUD_TEXT_1200, 0 };
    static const char *const phonetic[] = { " ", "tRE HuNDRxD ", "TWeLV HuNDRxD \r", "" };
    for (int i = 0; i < 4; i++) {
        setup();
        fw_regs r = { 0, 0, 0, 0 };
        boot_set_defaults(&g_f, &r);
        r.a = cmd_apply_speaker(&g_f, &r.x, &r.y);
        g_nlog = 0;
        g_sfr[SFR_P3] = p3[i];
        r.x = 1;
        boot_init_baud(&g_f, &r);
        CHECK(!g_f.fault);
        CHECK_EQ(g_ncalls, 0);
        CHECK_EQ(fw_rd16(&g_f, TN_RULE_PTR), text[i]);
        size_t n = strlen(phonetic[i]);
        CHECK_EQ(g_ram[TN_PHON_LEN], n);
        CHECK(memcmp(g_ram + TN_PHONETIC, phonetic[i], n) == 0);
        unsigned timer2 = 0;
        for (unsigned k = 0; k < g_nlog && k < MAX_LOG; k++)
            timer2 += g_log[k].kind == EV_WAIT && g_log[k].value == FW_WAIT_TIMER2;
        CHECK_EQ(timer2, text[i] ? 1 : 0);
        if (text[i] && g_nlog <= MAX_LOG) {
            CHECK_EQ(g_sfr[SFR_ICR2], 0);
            CHECK_EQ(g_log[g_nlog - 1].kind, EV_WRITE);
            CHECK_EQ(g_log[g_nlog - 1].addr, SFR_P2);
            CHECK_EQ(g_log[g_nlog - 1].value, P2_MAIN_BANK);
        }
    }
}

/* boot_init_host_lines $AC68: UART_CTRL = $D1, P0_FUNC = $4C, then one read of P0 (BBC2): the host line high: CLB5 P0
   (a read and a write), isr_host_line_high (ICR3 = $40, SEB5 P0, ser_status_due = 1); low: SEB5 P0,
   isr_host_line_low (ICR3 = $48, CLB5 P0). A = $4C, X and Y untouched. */
static void test_init_host_lines(void)
{
    static const event high[] = {
        { EV_WRITE, SFR_UART_CTRL, 0xD1 }, { EV_WRITE, SFR_P0_FUNC, 0x4C }, { EV_READ, SFR_P0, 0x24 },
        { EV_READ, SFR_P0, 0x24 }, { EV_WRITE, SFR_P0, 0x04 }, { EV_WRITE, SFR_ICR3, 0x40 }, { EV_READ, SFR_P0, 0x04 },
        { EV_WRITE, SFR_P0, 0x24 },
    };
    static const event low[] = {
        { EV_WRITE, SFR_UART_CTRL, 0xD1 }, { EV_WRITE, SFR_P0_FUNC, 0x4C }, { EV_READ, SFR_P0, 0x00 },
        { EV_READ, SFR_P0, 0x00 }, { EV_WRITE, SFR_P0, 0x20 }, { EV_WRITE, SFR_ICR3, 0x48 }, { EV_READ, SFR_P0, 0x20 },
        { EV_WRITE, SFR_P0, 0x00 },
    };
    for (int line = 0; line <= 1; line++) {
        setup();
        g_sfr[SFR_P0] = line ? 0x24 : 0x00;
        fw_regs r = { 0x11, 0x22, 0x33, 0 };
        boot_init_host_lines(&g_f, &r);
        check_log(line ? high : low, 8);
        check_regs(&r, 0x4C, 0x22, 0x33);
        CHECK_EQ(g_ram[SER_STATUS_DUE], line ? 1 : 0);
        CHECK(!g_f.fault);
    }
}

/* The rows of the boot helpers and cmd_say_phonetic (M4.6c2 spec 2.3): A, X and Y declared; cmd_say_phonetic waits. */
static void test_helper_rows(void)
{
    check_row("boot_read_dip", ROM_BOOT_READ_DIP, REGS, false, false, false);
    check_row("boot_init_ports", ROM_BOOT_INIT_PORTS, REGS, false, false, false);
    check_row("boot_init_settings", ROM_BOOT_INIT_SETTINGS, REGS, false, false, false);
    check_row("boot_clear_input_state", ROM_BOOT_CLEAR_INPUT_STATE, REGS, false, false, false);
    check_row("boot_set_defaults", ROM_BOOT_SET_DEFAULTS, REGS, false, false, false);
    check_row("boot_load_prosody_defaults", ROM_BOOT_LOAD_PROSODY_DEFAULTS, REGS, false, false, false);
    check_row("boot_init_baud", ROM_BOOT_INIT_BAUD, REGS, false, false, false);
    check_row("boot_init_host_lines", ROM_BOOT_INIT_HOST_LINES, REGS, false, false, false);
    check_row("cmd_say_phonetic", ROM_CMD_SAY_PHONETIC, REGS, true, false, false);
}

void suite_fw_boot(void)
{
    CHECK(fw_image_get() != NULL);
    test_wait_input();
    test_cpu_calls();
    test_read_dip();
    test_init_ports();
    test_clear_input_state();
    test_set_defaults();
    test_init_settings();
    test_init_baud();
    test_say_rate();
    test_init_host_lines();
    test_helper_rows();
}
```

**`tests/diff_state.h`**: replace

```c
bool diff_is_probed(const char *name);

#endif
```

with

```c
bool diff_is_probed(const char *name);

/* The main loop's rows that wait for input or never return (M4.6c2 spec 4.1): the entries and the waits rows of boot,
   which diff_main compares end to end, not diff_lts or diff_play. */
bool diff_is_main_row(const char *name);
/* The rows no corpus text enters and no probe compares (M4.6c2 spec 4.1, 4.4): the unreachable cmd_say_phonetic and
   isr_unused, pinned by unit tests, and the cold re-entry, which diff_main's injected run compares. golden_hybrid
   runs no thunk of them alone. */
bool diff_is_unreached(const char *name);

#endif
```

**`tests/diff_state.c`**: replace

```c
const size_t diff_probed_count = sizeof diff_probed / sizeof diff_probed[0];

bool diff_is_probed(const char *name)
{
    for (size_t i = 0; i < diff_probed_count; i++)
        if (!strcmp(diff_probed[i], name))
            return true;
    return false;
}

```

with

```c
const size_t diff_probed_count = sizeof diff_probed / sizeof diff_probed[0];

static bool listed(const char *const *list, size_t n, const char *name)
{
    for (size_t i = 0; i < n; i++)
        if (!strcmp(list[i], name))
            return true;
    return false;
}

bool diff_is_probed(const char *name) { return listed(diff_probed, diff_probed_count, name); }

bool diff_is_main_row(const char *name)
{
    static const char *const k_main[] = {
        "boot_reset", "boot_reentry_cold", "boot_reentry_warm", "bank_entry_cold", "bank_entry_warm",
        "boot_loop_text", "boot_get_clause", "isr_idle", "boot_selftest_sound", "cmd_say_phonetic",
    };
    return listed(k_main, sizeof k_main / sizeof k_main[0], name);
}

bool diff_is_unreached(const char *name)
{
    static const char *const k_unreached[] = {
        "cmd_say_phonetic", "isr_unused", "bank_entry_cold", "boot_reentry_cold",
    };
    return listed(k_unreached, sizeof k_unreached / sizeof k_unreached[0], name);
}

```

**`tests/test_diff_lts.c`**: replace

```c
#define DIFF_STRIDE     64        /* ... then every 64th (but the routines of k_every_call) */
#define DIFF_BUDGET     10000000ULL
#define DIFF_MAX        112

static const char *const k_corpus[] = { CORPUS_NAMES };
```

with

```c
#define DIFF_STRIDE     64        /* ... then every 64th (but the routines of k_every_call) */
#define DIFF_BUDGET     10000000ULL
#define DIFF_MAX        136

static const char *const k_corpus[] = { CORPUS_NAMES };
```

**`tests/test_diff_lts.c`**: replace

```c
};

/* The speech-timing routines that do not wait (M4.6b spec 4.1), the timer interrupt's too, and the bank code (M4.6c1
   spec 4.1): compared at every call. */
static const char *const k_every_call[] = {
    "isr_set_volume", "isr_volume_half", "isr_par_ready", "isr_par_hold", "isr_dsp_reset", "isr_speech_stop",
    "isr_timer_start", "isr_timer_dsp", "bank_load_ramcode", "bank_enter", "bank_scan", "bank_select", "bank_probe",
    "bank_read_id", "bank_put_id_char", "bank_safe_char", "bank_find_current", "cmd_run_next",
};

```

with

```c
};

/* The speech-timing routines that do not wait (M4.6b spec 4.1), the timer interrupt's too, the bank code (M4.6c1
   spec 4.1) and the boot helpers (M4.6c2 spec 4.2: most run once per boot): compared at every call. */
static const char *const k_every_call[] = {
    "isr_set_volume", "isr_volume_half", "isr_par_ready", "isr_par_hold", "isr_dsp_reset", "isr_speech_stop",
    "isr_timer_start", "isr_timer_dsp", "bank_load_ramcode", "bank_enter", "bank_scan", "bank_select", "bank_probe",
    "bank_read_id", "bank_put_id_char", "bank_safe_char", "bank_find_current", "cmd_run_next", "boot_read_dip",
    "boot_init_ports", "boot_init_settings", "boot_clear_input_state", "boot_set_defaults",
    "boot_load_prosody_defaults", "boot_init_baud", "boot_init_host_lines",
};

```

**`tests/test_diff_play.c`**: replace

```c
#include "firmware/par_addr.h"
#include "firmware/isr_addr.h"

#define PLAY_BUDGET        1000000000ULL   /* copy A: the longest call (seg_wrap_word, 1,774 frames) is 35.5 M cycles */
```

with

```c
#include "firmware/par_addr.h"
#include "firmware/isr_addr.h"
#include "diff_state.h"

#define PLAY_BUDGET        1000000000ULL   /* copy A: the longest call (seg_wrap_word, 1,774 frames) is 35.5 M cycles */
```

**`tests/test_diff_play.c`**: replace

```c
    for (size_t i = 0; i < fw_replacement_count; i++) {
        const fw_replacement *r = &fw_replacements[i];
        if (!r->waits)
            continue;
        CHECK(g_nwatch < WATCH_MAX && (r->addr == ROM_PAR_PLAY || r->addr == ROM_BOOT_LOOP_PLAY ||
                                       r->addr == ROM_ISR_SPEECH_START || r->addr == ROM_ISR_WAIT_FRAMES ||
```

with

```c
    for (size_t i = 0; i < fw_replacement_count; i++) {
        const fw_replacement *r = &fw_replacements[i];
        if (!r->waits || diff_is_main_row(r->name))
            continue;   /* the main loop's waits rows: diff_main's (M4.6c2 spec 4.1) */
        CHECK(g_nwatch < WATCH_MAX && (r->addr == ROM_PAR_PLAY || r->addr == ROM_BOOT_LOOP_PLAY ||
                                       r->addr == ROM_ISR_SPEECH_START || r->addr == ROM_ISR_WAIT_FRAMES ||
```

**`tests/test_fw_par.c`**: replace

```c
#include "test.h"
#include "engine/thunks.h"
#include "firmware/cmd_addr.h"
#include "firmware/cmd_play_event.h"
```

with

```c
#include "test.h"
#include "engine/thunks.h"
#include "firmware/boot_addr.h"
#include "firmware/cmd_addr.h"
#include "firmware/cmd_play_event.h"
```

**`tests/test_fw_par.c`**: replace

```c
        if (fw_replacements[i].waits) {
            uint16_t a = fw_replacements[i].addr;
            waits++;
            CHECK(a == ROM_BOOT_LOOP_PLAY || a == ROM_PAR_PLAY || a == ROM_ISR_SPEECH_START ||
                  a == ROM_ISR_WAIT_FRAMES || a == ROM_ISR_WAIT_FRAME);
        }
    CHECK_EQ(waits, 5);   /* the two loops and the three frame waits wait on the timer interrupt: diff_play checks
                             them, diff_lts not (M4.6b spec 2.4) */
}

```

with

```c
        if (fw_replacements[i].waits) {
            uint16_t a = fw_replacements[i].addr;
            bool frame = a == ROM_BOOT_LOOP_PLAY || a == ROM_PAR_PLAY || a == ROM_ISR_SPEECH_START ||
                         a == ROM_ISR_WAIT_FRAMES || a == ROM_ISR_WAIT_FRAME;
            waits += frame;
            CHECK(frame || a == ROM_BOOT_LOOP_TEXT || a == ROM_BOOT_GET_CLAUSE || a == ROM_ISR_IDLE ||
                  a == ROM_BOOT_SELFTEST_SOUND || a == ROM_CMD_SAY_PHONETIC);
        }
    CHECK_EQ(waits, 5);   /* the two loops and the three frame waits wait on the timer interrupt: diff_play checks
                             them, diff_lts not (M4.6b spec 2.4); the main loop's waits rows wait for input too:
                             diff_main checks them (M4.6c2 spec 4.1) */
}

```

**`tests/test_golden_hybrid.c`**: replace

```c
   On bank_select, @=1 enters a bank: where the C runs the dispatch or bank_enter, its call chain must unwind (M4.2
   spec 5.3, M4.6c1 spec 4.5). A routine no corpus text enters (diff_probed, M4.6a spec 4.4) runs no thunk alone;
   diff_serial compares it. Also the fault path of a thunk: a firmware fault stops the run instead of continuing on
   wrong data; the C entering a bank; both engines' loop through the idle handler and the phase deadline (M4.6c2 spec
   3.1, 3.2; test_engine_loop); and the M4.6c1 plan's Review Focus inputs on both engines (test_review_focus). */
#include <stdio.h>
#include <stdlib.h>
```

with

```c
   On bank_select, @=1 enters a bank: where the C runs the dispatch or bank_enter, its call chain must unwind (M4.2
   spec 5.3, M4.6c1 spec 4.5). A routine no corpus text enters (diff_probed, M4.6a spec 4.4) runs no thunk alone;
   diff_serial compares it; nor does a row no corpus text or probe reaches (diff_is_unreached, M4.6c2 spec 4.4).
   Also the fault path of a thunk: a firmware fault stops the run instead of continuing on wrong data; the C entering
   a bank; both engines' loop through the idle handler and the phase deadline (M4.6c2 spec 3.1, 3.2;
   test_engine_loop); and the M4.6c1 plan's Review Focus inputs on both engines (test_review_focus). */
#include <stdio.h>
#include <stdlib.h>
```

**`tests/test_golden_hybrid.c`**: replace

```c
    }
    check(rom, len, only, "boot", "", true);
    if (g_thunk_calls == 0 && !(only && diff_is_probed(only))) {
        fprintf(stderr, "  hybrid (%s): no thunk ran over the whole corpus\n", only ? only : "all");
        g_test_failures++;
```

with

```c
    }
    check(rom, len, only, "boot", "", true);
    if (g_thunk_calls == 0 && !(only && (diff_is_probed(only) || diff_is_unreached(only)))) {
        fprintf(stderr, "  hybrid (%s): no thunk ran over the whole corpus\n", only ? only : "all");
        g_test_failures++;
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw_boot"`
Expected: the build stops with compile errors (the proof run, first lines):

```
tests\test_fw_boot.c(9): fatal error C1083: Cannot open include file: 'firmware/boot.h': No such file or directory
tests\test_fw_par.c(10): fatal error C1083: Cannot open include file: 'firmware/boot_addr.h': No such file or directory
```

- [ ] **Step 3: Implement**

`boot_addr.h`, the helpers in `boot.c`, the names they need (`FW_CLAUSE_CARRY`, the baud texts' addresses, the public `cmd_put_phonetic`), and the nine rows.

**Create `src/firmware/boot_addr.h`:**

```c
#ifndef FW_BOOT_ADDR_H
#define FW_BOOT_ADDR_H
/* ROM code addresses of boot, the main loop and isr_idle (M4.6c2 spec 2.1), checked against build/ghidra740/dump and
   the JSR/JMP operands and vectors in the ROM image. The re-entries and the bank entries are in bank_addr.h:
   ROM_BOOT_REENTRY_COLD $A665 (bank_entry_cold's JMP at $FFD1), ROM_BOOT_REENTRY_WARM $A66B (boot_reset's JMP at
   $A662, bank_entry_warm's JMP at $FFD4, and boot_reentry_cold falls into it), BANK_ENTRY_COLD $FFD0 (bank_enter's
   JMP at $0240) and BANK_ENTRY_WARM $FFD4 (bank_enter's JMP at $0232). */

/* The entry */
#define ROM_BOOT_RESET                  0xA61C   /* vec_reset $FFFE (and bank_entry_reset's JMP at $FFD8, which no
                                                    code reaches) */

/* The main loop's routines that wait */
#define ROM_BOOT_LOOP_TEXT              0xA68D   /* called by boot_reset $A681, boot_selftest_sound $A7B4 */
#define ROM_BOOT_GET_CLAUSE             0xA697   /* called by boot_loop_text $A68D */
#define ROM_ISR_IDLE                    0xC5FE   /* called by boot_get_clause $A6DF */
#define ROM_BOOT_SELFTEST_SOUND         0xA79F   /* called by boot_reset $A654 */
#define ROM_CMD_SAY_PHONETIC            0xA876   /* called by boot_init_baud $AC96, $ACAC, $ACBF (with X = 1 only:
                                                    unreachable) */

/* The helpers */
#define ROM_BOOT_READ_DIP               0xADE0   /* called by boot_reset $A645, boot_reentry_cold $A665 */
#define ROM_BOOT_INIT_PORTS             0xAC22   /* called by boot_reset $A648 */
#define ROM_BOOT_INIT_SETTINGS          0xADEB   /* called by boot_reset $A64E, boot_reentry_cold $A668 */
#define ROM_BOOT_CLEAR_INPUT_STATE      0xABF0   /* called by boot_init_settings $ADEB */
#define ROM_BOOT_SET_DEFAULTS           0xA742   /* called by boot_init_settings $ADEE */
#define ROM_BOOT_LOAD_PROSODY_DEFAULTS  0xA7F0   /* called by boot_set_defaults $A788 */
#define ROM_BOOT_INIT_BAUD              0xAC7E   /* called by boot_reset $A65C (with X = 0) */
#define ROM_BOOT_INIT_HOST_LINES        0xAC68   /* called by boot_reset $A65F */
#define ROM_BOOT_SELFTEST               0xC605   /* called by boot_selftest_sound $A7A5 */
#define ROM_BOOT_QUEUE_STRING           0xC612   /* called by boot_selftest_sound $A7B1 */
#define ROM_ISR_UNUSED                  0xC3DF   /* vec_int3_step $FFF6, vec_int0 $FFFC (RTI) */

/* Instructions the C reports to the host */
#define BOOT_GET_CLAUSE_LOOP            0xA6C2   /* boot_get_clause's polling loop: its FW_WAIT_INTERRUPT */
#define BOOT_CLI_WARM                   0xA66E   /* boot_reentry_warm's CLI */
#define BOOT_CLI_EMPTY                  0xA6D3   /* boot_get_clause: the CLI of a pass with the input ring empty ... */
#define BOOT_SEI_READ                   0xA6E7   /* ... the SEI before a byte is read ... */
#define BOOT_CLI_READ                   0xA6FF   /* ... and the CLI after it */

#endif
```

**Create `src/firmware/boot.h`:**

```c
#ifndef FW_BOOT_H
#define FW_BOOT_H
/* Boot (ROM $A61C-$A68C, $A742-$A7D3, $A876-$A87F, $ABF0-$ACC6, $ADE0-$ADFD, $C3DF, $C605-$C611), decompiled
   (M4.6c2 spec 2.1). Each routine takes the registers at entry in r and leaves in r the A, X and Y the ROM leaves at
   its RTS; r->p is not used: no caller reads a flag. SFRs are read and written through the host in the ROM's order
   (fw_sfr*), each SEB/CLB as a read then a write. */
#include "fw.h"

/* The SFRs of boot (ghidra740/annotations/sfr.txt) that ser.h and isr_dsp.h do not name */
#define SFR_AD_CTRL      0xE9
#define SFR_P4           0xEB   /* bits 0-3: the DIP switches */
#define SFR_P3_DIR       0xEF
#define SFR_P2_DIR       0xF1
#define SFR_P0_FUNC      0xF5
#define SFR_P0_DIR       0xF7
#define SFR_UART_BAUD    0xFB

#define DIP_PARALLEL     0x01   /* boot_dip bit 0: the parallel interface */
#define DIP_SELFTEST     0x02   /* boot_dip bit 1: the self-test reads P2.1 (and the alternative bank table) */

/* $ADE0: AD_CTRL = 0, then boot_dip = A = P4 & $0F. X and Y untouched. */
void boot_read_dip(fw *f, fw_regs *r);
/* $AC22: the ports by boot_dip bit 0. Serial: P0_FUNC $48, P0_DIR $78, P0 $20, P1_LATCH 0, P1_DIR 0; parallel:
   P0_FUNC 0, P0_DIR 8, P0 0, P1 1, P1_DIR $FF. Then in both P2 $70 (the main bank), P2_DIR $FD, P3_DIR $F9,
   P23_FUNC 0, P3 $78. A = 0; X and Y untouched. */
void boot_init_ports(fw *f, fw_regs *r);
/* $ADEB: boot_clear_input_state, boot_set_defaults, then bank_ver_major and bank_ver_minor = the bank header's
   $FFE0 and $FFE1. A = the $FFE1 byte, X untouched, Y = $1A. */
void boot_init_settings(fw *f, fw_regs *r);
/* $ABF0: ser_abort, boot_speaking, ser_rx_idle, ser_rx_idle_saved, cmd_index_count, ser_tx_pending, $07,
   ser_tx_count, isr_par_state and ser_cmd_mode = 0; cmd_index_reset = 1; the output ring pointers ser_tx_read and
   ser_tx_write = $3900, the input ring's ser_rx_read and ser_rx_write = $2100. A = 0; X and Y untouched. */
void boot_clear_input_state(fw *f, fw_regs *r);
/* $A742: the power-up @-settings; cmd_char = ser_cmd_char = '@'; $6B = 4; the flags and counts cmd_source ...
   cmd_speaker and $1827 = 0; cmd_clear_formant; boot_load_prosody_defaults. A, Y as the last leaves them; X
   untouched. */
void boot_set_defaults(fw *f, fw_regs *r);
/* $A7F0: boot_prosody_defaults (26 bytes) copied to pros_params. A = the last byte, Y = 26; X untouched. */
void boot_load_prosody_defaults(fw *f, fw_regs *r);
/* $AC7E: tn_phon_len = 0; UART_BAUD by the P3.1 and P3.2 jumpers (one read of P3 per test): $02 (both low), $BF
   (P3.1 high), $2F (P3.2 high), $05 (both). With X = 1 the rate is also spoken: tn_rule_ptr = its phonetic text
   (boot_baud_text_19200, _300 or _1200; none for $05), cmd_say_phonetic. A, X and Y untouched, or as
   cmd_say_phonetic leaves them. */
void boot_init_baud(fw *f, fw_regs *r);
/* $AC68: UART_CTRL = $D1; A = $4C to P0_FUNC; the host line P0.2 high: CLB5 P0 and isr_host_line_high; low: SEB5
   P0 and isr_host_line_low. A = $4C; X and Y untouched. */
void boot_init_host_lines(fw *f, fw_regs *r);
/* $A876: cmd_put_phonetic (the length-prefixed text at tn_rule_ptr to tn_phonetic), pros_make_segments,
   boot_loop_play (C calls C); it waits as boot_loop_play does. A, X and Y as boot_loop_play leaves them. */
void cmd_say_phonetic(fw *f, fw_regs *r);

#endif
```

**Create `src/firmware/boot.c`:**

```c
#include "boot.h"
#include "bank_addr.h"
#include "cmd_apply.h"
#include "cmd_handlers.h"
#include "isr_dsp.h"
#include "isr_serial.h"
#include "par_play.h"
#include "pros_make.h"
#include "ser.h"
#include "data/fw_data.h"

#define DIP_SWITCHES     0x0F   /* boot_read_dip: P4 bits 0-3 */
#define P3_JUMPER_1      1      /* P3.1 and P3.2: the baud jumpers */
#define P3_JUMPER_2      2
#define BOOT_TX_PAGE     0x39   /* boot_clear_input_state: the output ring $3900 ... */
#define BOOT_RX_PAGE     0x21   /* ... and the input ring $2100 */
#define BOOT_TMP_07      0x0007 /* zero page, unnamed in the annotations: boot_clear_input_state clears it ($ABFE) */
#define BOOT_CMD_CHAR    '@'    /* boot_set_defaults: cmd_char and ser_cmd_char */
#define BOOT_SAY_RATE    1      /* boot_init_baud: X = 1 speaks the rate */

typedef struct { uint16_t addr; uint8_t value; } setting;

/* The power-up @-settings of boot_set_defaults ($A742-$A769, an LDM each), before and after the two command
   characters */
static const setting k_defaults[] = {
    { CMD_PITCH, 8 }, { CMD_PROSODY, 3 }, { CMD_SPEED_LIMIT, 7 }, { CMD_SPEED, 3 }, { CMD_VOLUME, 6 },
    { CMD_WORD_PAUSE, 0 },
};
static const setting k_defaults2[] = {
    { CMD_PITCH_STEP, 8 }, { FW_UTT_CONTENT, 4 }, { CMD_PHRASE_PAUSE, 0x0B }, { CMD_VOICE, 1 },
    { CMD_MARK_SPACE, 0x14 }, { CMD_VOICING, 8 },
};
/* ... and the ones it clears ($A76C-$A782, STA of A = 0) */
static const uint16_t k_cleared[] = {
    CMD_SOURCE, CMD_PHONETIC, CMD_SPELL, CMD_PUNCT, CMD_HYPER, CMD_EV_COUNT, CMD_COUNT, CMD_SOUND_ACTIVE, CMD_SPEAKER,
    FW_CLAUSE_CARRY,
};
/* ... and boot_clear_input_state's ($ABF2-$AC04, STA of A = 0) */
static const uint16_t k_input_cleared[] = {
    SER_ABORT, BOOT_SPEAKING, SER_RX_IDLE, SER_RX_IDLE_SAVED, CMD_INDEX_COUNT, SER_TX_PENDING, BOOT_TMP_07,
    SER_TX_COUNT, ISR_PAR_STATE, SER_CMD_MODE,
};

void boot_read_dip(fw *f, fw_regs *r)
{
    fw_wr_sfr8(f, SFR_AD_CTRL, 0);                               /* $ADE0 LDM #0,AD_CTRL; NOP */
    r->a = fw_sfr8(f, SFR_P4) & DIP_SWITCHES;                    /* LDA P4; AND #$0F */
    fw_wr8(f, BOOT_DIP, r->a);
}

/* $AC5E-$AC66 (and boot_reset's first three LDMs, $A61C-$A622): P3 and its function. */
static void init_p3(fw *f)
{
    fw_wr_sfr8(f, SFR_P3_DIR, 0xF9);
    fw_wr_sfr8(f, SFR_P23_FUNC, 0);
    fw_wr_sfr8(f, SFR_P3, 0x78);
}

void boot_init_ports(fw *f, fw_regs *r)
{
    if (fw_rd8(f, BOOT_DIP) & DIP_PARALLEL) {                    /* $AC22 BBC0 boot_dip */
        r->a = 0;
        fw_wr_sfr8(f, SFR_P0_FUNC, r->a);
        fw_wr_sfr8(f, SFR_P0_DIR, 0x08);
        fw_wr_sfr8(f, SFR_P0, 0x00);
    } else {
        r->a = 0x48;                                             /* $AC31 */
        fw_wr_sfr8(f, SFR_P0_FUNC, r->a);
        fw_wr_sfr8(f, SFR_P0_DIR, 0x78);
        fw_wr_sfr8(f, SFR_P0, 0x20);
    }
    if (fw_rd8(f, BOOT_DIP) & DIP_PARALLEL) {                    /* $AC3B NOP; $AC3C BBC0 boot_dip */
        fw_wr_sfr8(f, SFR_P1, 0x01);
        fw_wr_sfr8(f, SFR_P1_DIR, 0xFF);
    } else {
        r->a = 0;                                                /* $AC47 */
        fw_wr_sfr8(f, SFR_P1_LATCH, r->a);
        fw_wr_sfr8(f, SFR_P1_DIR, r->a);
    }
    fw_wr_sfr8(f, SFR_P2, P2_MAIN_BANK);                         /* $AC4D BBC0: $AC50 or $AC58, the same two LDMs */
    fw_wr_sfr8(f, SFR_P2_DIR, 0xFD);
    init_p3(f);
}

void boot_clear_input_state(fw *f, fw_regs *r)
{
    r->a = 0;                                                    /* $ABF0 LDA #0 */
    for (unsigned i = 0; i < sizeof k_input_cleared / sizeof k_input_cleared[0]; i++)
        fw_wr8(f, k_input_cleared[i], r->a);
    fw_wr8(f, CMD_INDEX_RESET, 1);                               /* $AC06 LDM #1 */
    fw_wr8(f, SER_TX_WRITE, 0);                                  /* $AC09-$AC1E: the low bytes, then the pages */
    fw_wr8(f, SER_TX_READ, 0);
    fw_wr8(f, SER_RX_READ, 0);
    fw_wr8(f, SER_RX_WRITE, 0);
    fw_wr8(f, SER_TX_WRITE + 1, BOOT_TX_PAGE);
    fw_wr8(f, SER_TX_READ + 1, BOOT_TX_PAGE);
    fw_wr8(f, SER_RX_READ + 1, BOOT_RX_PAGE);
    fw_wr8(f, SER_RX_WRITE + 1, BOOT_RX_PAGE);
}

void boot_load_prosody_defaults(fw *f, fw_regs *r)
{
    r->y = 0;                                                    /* $A7F0 LDY #0 */
    do {
        r->a = fw_rom8(f, (uint16_t)(BOOT_PROSODY_DEFAULTS + r->y));   /* LDA $C65B,Y; STA $168B,Y */
        fw_wr8(f, (uint16_t)(PROS_PARAMS + r->y), r->a);
        r->y++;
    } while (r->y != BOOT_PROSODY_PARAMS);                       /* INY; CPY #$1A; BNE */
}

void boot_set_defaults(fw *f, fw_regs *r)
{
    for (unsigned i = 0; i < sizeof k_defaults / sizeof k_defaults[0]; i++)
        fw_wr8(f, k_defaults[i].addr, k_defaults[i].value);
    r->a = BOOT_CMD_CHAR;                                        /* $A754 LDA #$40 */
    fw_wr8(f, CMD_CHAR, r->a);
    fw_wr8(f, SER_CMD_CHAR, r->a);
    for (unsigned i = 0; i < sizeof k_defaults2 / sizeof k_defaults2[0]; i++)
        fw_wr8(f, k_defaults2[i].addr, k_defaults2[i].value);
    r->a = 0;                                                    /* $A76C LDA #0 */
    for (unsigned i = 0; i < sizeof k_cleared / sizeof k_cleared[0]; i++)
        fw_wr8(f, k_cleared[i], r->a);
    cmd_clear_formant(f);                                        /* $A785: A and Y kept */
    boot_load_prosody_defaults(f, r);
}

void boot_init_settings(fw *f, fw_regs *r)
{
    boot_clear_input_state(f, r);
    boot_set_defaults(f, r);
    r->a = fw_rom8(f, BANK_HDR_VERSION);                         /* $ADF1 LDA $FFE0 */
    fw_wr8(f, BANK_VER_MAJOR, r->a);
    r->a = fw_rom8(f, BANK_HDR_VERSION + 1);                     /* $ADF7 LDA $FFE1 */
    fw_wr8(f, BANK_VER_MINOR, r->a);
}

void boot_init_baud(fw *f, fw_regs *r)
{
    uint8_t baud;
    uint16_t text;
    fw_wr8(f, TN_PHON_LEN, 0);                                   /* $AC7E LDM #0 */
    if (fw_sfr_bit(f, SFR_P3, P3_JUMPER_1)) {                   /* $AC81 BBS1 P3 */
        if (fw_sfr_bit(f, SFR_P3, P3_JUMPER_2)) {                /* $AC9A BBS2 P3 */
            fw_wr_sfr8(f, SFR_UART_BAUD, 0x05);                  /* $ACC3: both jumpers, no text */
            return;
        }
        baud = 0xBF;
        text = BOOT_BAUD_TEXT_300;
    } else if (fw_sfr_bit(f, SFR_P3, P3_JUMPER_2)) {             /* $AC84 BBS2 P3 */
        baud = 0x2F;
        text = BOOT_BAUD_TEXT_1200;
    } else {
        baud = 0x02;
        text = BOOT_BAUD_TEXT_19200;
    }
    fw_wr_sfr8(f, SFR_UART_BAUD, baud);
    if (r->x != BOOT_SAY_RATE)                                   /* CPX #1; BNE */
        return;
    r->a = (uint8_t)text;                                        /* LDA #lo; STA tn_rule_ptr; LDA #hi; STA */
    fw_wr8(f, TN_RULE_PTR, r->a);
    r->a = (uint8_t)(text >> 8);
    fw_wr8(f, TN_RULE_PTR + 1, r->a);
    cmd_say_phonetic(f, r);
}

void boot_init_host_lines(fw *f, fw_regs *r)
{
    fw_wr_sfr8(f, SFR_UART_CTRL, 0xD1);                          /* $AC68 LDM #$D1 */
    r->a = 0x4C;                                                 /* LDA #$4C; STA P0_FUNC */
    fw_wr_sfr8(f, SFR_P0_FUNC, r->a);
    if (fw_sfr_bit(f, SFR_P0, P0_HOST_LINE)) {                   /* $AC6F BBC2 P0 */
        fw_sfr_clb(f, SFR_P0, P0_BUSY);
        isr_host_line_high(f, r);
    } else {
        fw_sfr_seb(f, SFR_P0, P0_BUSY);                          /* $AC78 */
        isr_host_line_low(f, r);
    }
}

void cmd_say_phonetic(fw *f, fw_regs *r)
{
    r->x = cmd_put_phonetic(f, r->x);                            /* $A876: A and Y kept */
    pros_make_segments(f, r);
    boot_loop_play(f, r);
}
```

**`src/firmware/fw.h`**: replace

```c
#define FW_UTT_CONTENT         0x006B   /* unnamed in the annotations: "the utterance has content", $FF (LDM) at each
                                           word pause and each phoneme triple; boot_loop_play tests it */
#define FW_PROS_STRESS_TOGGLE  0x1C0D   /* unnamed in the annotations: pros_phrase_gen's "a stress or an accent seen
                                           in this word" (0 at each word start). pros_mark_stress: the syllable's
```

with

```c
#define FW_UTT_CONTENT         0x006B   /* unnamed in the annotations: "the utterance has content", $FF (LDM) at each
                                           word pause and each phoneme triple; boot_loop_play tests it */
#define FW_CLAUSE_CARRY        0x1827   /* unnamed in the annotations: a command character boot_get_clause cut off
                                           at a clause's end ($A739), put first in the next clause ($A6A3) and
                                           cleared; 0 when none. boot_set_defaults clears it ($A782) */
#define FW_PROS_STRESS_TOGGLE  0x1C0D   /* unnamed in the annotations: pros_phrase_gen's "a stress or an accent seen
                                           in this word" (0 at each word start). pros_mark_stress: the syllable's
```

**`src/firmware/data/fw_data.h`**: replace

```c
#define CMD_VERSION_ENGINE_TEXT 0x9804   /* cmd_version_engine_text */
#define CMD_VERSION_LANG_TEXT   0xF75B   /* cmd_version_lang_text */
#define CMD_SPEED_MAPS      0xC87B   /* ptr[10], indexed by speed - 1 */
#define CMD_SPEED_LIMITS    0xC88F   /* byte[9], indexed by speed - 1 */
```

with

```c
#define CMD_VERSION_ENGINE_TEXT 0x9804   /* cmd_version_engine_text */
#define CMD_VERSION_LANG_TEXT   0xF75B   /* cmd_version_lang_text */
/* The baud texts (phonetic texts converted in M4.1) that boot_init_baud passes by tn_rule_ptr with X = 1 */
#define BOOT_BAUD_TEXT_19200    0xACC7   /* boot_baud_text_19200: silent */
#define BOOT_BAUD_TEXT_300      0xACCA   /* boot_baud_text_300 */
#define BOOT_BAUD_TEXT_1200     0xACD8   /* boot_baud_text_1200 */
#define CMD_SPEED_MAPS      0xC87B   /* ptr[10], indexed by speed - 1 */
#define CMD_SPEED_LIMITS    0xC88F   /* byte[9], indexed by speed - 1 */
```

**`src/firmware/cmd_handlers.h`**: replace

```c
                                                             when it is absent */

/* The Y a ROM handler is entered with: cmd_next - 1 (cmd_run_due's LDY, before cmd_run_next's INC). */
static inline uint8_t cmd_handler_y(const fw *f) { return (uint8_t)(fw_rd8(f, CMD_NEXT) - 1); }
```

with

```c
                                                             when it is absent */

/* $A868, a helper of cmd_say_version (cmd_set.c) and cmd_say_phonetic (boot.c, M4.6c2): the length-prefixed phonetic
   text at tn_rule_ptr appended to tn_phonetic (tn_append_phonetic). A and Y kept; returns the X tn_append_phonetic
   leaves (x as it came for a length of 0). */
uint8_t cmd_put_phonetic(fw *f, uint8_t x);

/* The Y a ROM handler is entered with: cmd_next - 1 (cmd_run_due's LDY, before cmd_run_next's INC). */
static inline uint8_t cmd_handler_y(const fw *f) { return (uint8_t)(fw_rd8(f, CMD_NEXT) - 1); }
```

**`src/firmware/cmd_set.c`**: replace

```c
}

/* $A868: append the length-prefixed phonetic text at tn_rule_ptr to tn_phonetic. The ROM preserves A and Y and
   returns the X tn_append_phonetic leaves (cmd_run_done restores the handler's X at the end). */
static uint8_t cmd_put_phonetic(fw *f, uint8_t x)
{
    uint8_t y = 0;
```

with

```c
}

/* $A868 (cmd_handlers.h): the ROM preserves A and Y and returns the X tn_append_phonetic leaves (cmd_say_version's
   cmd_run_done restores the handler's X at the end). */
uint8_t cmd_put_phonetic(fw *f, uint8_t x)
{
    uint8_t y = 0;
```

**`src/engine/thunks.c`**: replace

```c
#include "firmware/isr_dsp.h"
#include "firmware/bank.h"
#include "firmware/data/fw_data.h"

```

with

```c
#include "firmware/isr_dsp.h"
#include "firmware/bank.h"
#include "firmware/boot.h"
#include "firmware/data/fw_data.h"

```

**`src/engine/thunks.c`**: replace

```c
}

/* The hook of each routine: its register mapping run in the shared thunk frame, user = the bound fw_bridge. */
#define THUNK(name) \
```

with

```c
}

/* The registers in, a boot routine (boot.c, M4.6c2), the registers out: A, X and Y as the ROM leaves them. No caller
   reads a flag. */
static void boot_regs(fw *f, cpu740 *c, void (*fn)(fw *, fw_regs *))
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    fn(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* boot_read_dip $ADE0: no register input (LDA P4). A = boot_dip, X and Y untouched, out. boot_reset ($A645) calls
   boot_init_ports next, which writes A first ($AC25 LDA #0, $AC31 LDA #$48) and reads neither X nor Y;
   boot_reentry_cold ($A665) calls boot_init_settings, whose boot_clear_input_state writes A first ($ABF0 LDA #0) and
   which reads no X, and Y only to push and pull it (cmd_clear_formant) before boot_load_prosody_defaults writes it
   ($A7F0 LDY #0). Declared: A, X, Y. */
static void boot_read_dip_regs(fw *f, cpu740 *c) { boot_regs(f, c, boot_read_dip); }

/* boot_init_ports $AC22: no register input. A = 0 (both modes end on an LDA #0: $AC25 parallel, $AC47 serial), X and Y
   untouched, out. Its caller, boot_reset ($A648), writes ICR1 ($A64B LDM) and calls boot_init_settings, which reads
   no register (see boot_read_dip). Declared: A, X, Y. */
static void boot_init_ports_regs(fw *f, cpu740 *c) { boot_regs(f, c, boot_init_ports); }

/* boot_init_settings $ADEB: no register read (see boot_read_dip). A = the bank header's $FFE1 byte ($ADF7 LDA), X
   untouched, Y = $1A (boot_load_prosody_defaults' count) out. boot_reset ($A64E) calls cmd_apply_speaker next, which
   loads A first ($A78C LDA cmd_speaker) and hands X and Y to par_setup_voice, which does not read them (its row);
   boot_reentry_cold ($A668) falls into boot_reentry_warm: LDM ser_abort, CLI, isr_par_hold and isr_par_ready (A, X
   and Y kept), cmd_apply_speaker. Declared: A, X, Y. */
static void boot_init_settings_regs(fw *f, cpu740 *c) { boot_regs(f, c, boot_init_settings); }

/* boot_clear_input_state $ABF0: no register input (LDA #0 first). A = 0, X and Y untouched, out. Its caller,
   boot_init_settings ($ADEB), calls boot_set_defaults next, which writes A ($A754 LDA #$40) and Y
   (boot_load_prosody_defaults) before reading them and reads no X. Declared: A, X, Y. */
static void boot_clear_input_state_regs(fw *f, cpu740 *c) { boot_regs(f, c, boot_clear_input_state); }

/* boot_set_defaults $A742: Y in, kept through cmd_clear_formant (PHA / TYA / PHA ... PLA / TAY / PLA), then written by
   boot_load_prosody_defaults. A = boot_prosody_defaults' last byte, X untouched, Y = $1A out, as
   boot_load_prosody_defaults leaves them. Its caller, boot_init_settings ($ADEE), loads A next ($ADF1 LDA $FFE0) and
   returns X and Y as its own (its row). Declared: A, X, Y. */
static void boot_set_defaults_regs(fw *f, cpu740 *c) { boot_regs(f, c, boot_set_defaults); }

/* boot_load_prosody_defaults $A7F0: no register input (LDY #0 first). A = the last byte copied, Y = $1A, X
   untouched, out. Its caller, boot_set_defaults ($A788), returns them ($A78B RTS; its row). Declared: A, X, Y. */
static void boot_load_prosody_defaults_regs(fw *f, cpu740 *c) { boot_regs(f, c, boot_load_prosody_defaults); }

/* boot_init_baud $AC7E: X in (1: speak the rate; boot_reset passes 0, $A65A LDX #0). A, X and Y untouched out (LDM,
   BBS, CPX), or with X = 1 as cmd_say_phonetic leaves them. Its caller, boot_reset ($A65C), calls
   boot_init_host_lines next, which loads A first ($AC6B LDA #$4C) and reads neither X nor Y. Declared: A, X, Y. */
static void boot_init_baud_regs(fw *f, cpu740 *c) { boot_regs(f, c, boot_init_baud); }

/* boot_init_host_lines $AC68: no register input. A = $4C ($AC6B LDA; isr_host_line_high and _low keep A, X and Y), X
   and Y untouched, out. Its caller, boot_reset ($A65F), falls into boot_reentry_warm ($A662 JMP): LDM, CLI,
   isr_par_hold and isr_par_ready (kept), cmd_apply_speaker (A loaded first; X and Y not read). Declared: A, X, Y. */
static void boot_init_host_lines_regs(fw *f, cpu740 *c) { boot_regs(f, c, boot_init_host_lines); }

/* cmd_say_phonetic $A876: X in, to cmd_put_phonetic, whose tn_append_phonetic writes it (X = tn_phon_len) before
   reading it unless the length is 0; A and Y are written first (LDY #0, LDA (tn_rule_ptr),Y). A, X and Y out as
   boot_loop_play leaves them. Its one caller, boot_init_baud ($AC96, $ACAC, $ACBF), returns at once ($AC99, $ACAF,
   $ACC2 RTS) to boot_reset, which calls boot_init_host_lines (see boot_init_baud). Unreachable: boot_init_baud is
   called with X = 0 only. It waits on the timer interrupt (boot_loop_play), so diff_main checks it (where reachable),
   not diff_lts. Declared: A, X, Y. */
static void cmd_say_phonetic_regs(fw *f, cpu740 *c) { boot_regs(f, c, cmd_say_phonetic); }

/* The hook of each routine: its register mapping run in the shared thunk frame, user = the bound fw_bridge. */
#define THUNK(name) \
```

**`src/engine/thunks.c`**: replace

```c
THUNK(bank_find_current)
THUNK(bank_load_ramcode)
THUNK(cmd_apply_speaker)
THUNK(boot_loop_play)
THUNK(cmd_run_due)
THUNK(cmd_run_next)
THUNK(isr_set_volume)
THUNK(cmd_scan_clause)
THUNK(cmd_match_table)
THUNK(lts_affix_next)
THUNK(lts_affix_prev)
```

with

```c
THUNK(bank_find_current)
THUNK(bank_load_ramcode)
THUNK(boot_set_defaults)
THUNK(cmd_apply_speaker)
THUNK(boot_loop_play)
THUNK(boot_load_prosody_defaults)
THUNK(cmd_say_phonetic)
THUNK(cmd_run_due)
THUNK(cmd_run_next)
THUNK(boot_clear_input_state)
THUNK(boot_init_ports)
THUNK(boot_init_host_lines)
THUNK(boot_init_baud)
THUNK(isr_set_volume)
THUNK(cmd_scan_clause)
THUNK(cmd_match_table)
THUNK(boot_read_dip)
THUNK(boot_init_settings)
THUNK(lts_affix_next)
THUNK(lts_affix_prev)
```

**`src/engine/thunks.c`**: replace

```c
    { "bank_find_current",  0x02F0, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_bank_find_current, false, false, true },
    { "bank_load_ramcode",  0xA600, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_bank_load_ramcode },
    { "cmd_apply_speaker",  0xA78C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_apply_speaker },
    { "boot_loop_play",     0xA7D4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_loop_play, true },
    { "cmd_run_due",        0xA880, FW_OUT_X | FW_OUT_Y, t_cmd_run_due },
    { "cmd_run_next",       0xA893, FW_OUT_A | FW_OUT_X, t_cmd_run_next },
    { "isr_set_volume",     0xACE8, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_set_volume },
    { "cmd_scan_clause",    0xACFA, 0,                   t_cmd_scan_clause },
    { "cmd_match_table",    0xADBF, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_match_table },
    { "lts_affix_next",     0xAF11, FW_OUT_A | FW_OUT_Y, t_lts_affix_next },
    { "lts_affix_prev",     0xAF1C, FW_OUT_A | FW_OUT_Y, t_lts_affix_prev },
```

with

```c
    { "bank_find_current",  0x02F0, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_bank_find_current, false, false, true },
    { "bank_load_ramcode",  0xA600, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_bank_load_ramcode },
    { "boot_set_defaults",  0xA742, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_set_defaults },
    { "cmd_apply_speaker",  0xA78C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_apply_speaker },
    { "boot_loop_play",     0xA7D4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_loop_play, true },
    { "boot_load_prosody_defaults", 0xA7F0, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_load_prosody_defaults },
    { "cmd_say_phonetic",   0xA876, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_say_phonetic, true },
    { "cmd_run_due",        0xA880, FW_OUT_X | FW_OUT_Y, t_cmd_run_due },
    { "cmd_run_next",       0xA893, FW_OUT_A | FW_OUT_X, t_cmd_run_next },
    { "boot_clear_input_state", 0xABF0, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_clear_input_state },
    { "boot_init_ports",    0xAC22, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_init_ports },
    { "boot_init_host_lines", 0xAC68, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_init_host_lines },
    { "boot_init_baud",     0xAC7E, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_init_baud },
    { "isr_set_volume",     0xACE8, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_set_volume },
    { "cmd_scan_clause",    0xACFA, 0,                   t_cmd_scan_clause },
    { "cmd_match_table",    0xADBF, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_match_table },
    { "boot_read_dip",      0xADE0, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_read_dip },
    { "boot_init_settings", 0xADEB, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_init_settings },
    { "lts_affix_next",     0xAF11, FW_OUT_A | FW_OUT_Y, t_lts_affix_next },
    { "lts_affix_prev",     0xAF1C, FW_OUT_A | FW_OUT_Y, t_lts_affix_prev },
```

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test fw"`, then `build/apollo2_tests.exe diff_lts`, `build/apollo2_tests.exe diff_play` and `build/apollo2_tests.exe diff_main`.
Expected:

- the build passes with no warnings; `fw`: `11 suite(s), 0 failure(s), 0 skip(s)`;
- `diff_lts`: 113 lines `<name> N calls checked`: the 105 of Task 1 Step 5, unchanged, and among them (in the table's order, so not together) these eight; then the summary:

```
  boot_set_defaults    52 calls checked
  boot_load_prosody_defaults 52 calls checked
  boot_clear_input_state 52 calls checked
  boot_init_ports      52 calls checked
  boot_init_host_lines 52 calls checked
  boot_init_baud       52 calls checked
  boot_read_dip        52 calls checked
  boot_init_settings   52 calls checked
  1 mid-clause flush calls skipped (golden_hybrid covers them)
  24 unwound calls compared
suite_diff_lts               ok
```

  0 mismatches (175 s in the proof run, under the 5 minutes the run-time gate allows);
- `diff_play` exactly as in Task 1 Step 5 (`5 routines wait on the timer interrupt`);
- `diff_main` exactly as in Task 3 Step 2 (the helpers run as C in its hybrid now), 0 mismatches.

- [ ] **Step 5: Run the full suite**

Run: `cmd //c ".\build.bat test"` (in the background)
Expected:

- `40 suite(s), 0 failure(s), 0 skip(s)`, no warnings, the firmware syntax check passing (867 s in the proof run);
- `firmware_data`: `49092 converted bytes compared with the ROM, 0 differ`;
- `golden_hybrid`: `48 corpus files x 120 modes (all, then each routine alone)`, every file identical, every golden unchanged (`git status --short tests/golden` prints nothing);
- `diff_lts` (113 lines, `24 unwound calls compared`), `diff_play`, `diff_main` and `diff_serial` as in Step 4 and Task 3, 0 mismatches;
- `suite_golden`: `timer scale 1/4: 105 frames, 11789152 CPU cycles`, `timer scale 3/1: 105 frames, 1286784 CPU cycles` (rom, unchanged), `hybrid timer scale 1/4: 105 frames, 11683706 CPU cycles`, `hybrid timer scale 3/1: 105 frames, 978616 CPU cycles` (fewer: the boot helpers take no emulated time);
- `suite_synth`: `corpus peak plain 20522 (-4.1 dBFS)`, `corpus peak b11 16038 (-6.2 dBFS)`.

- [ ] **Step 6: Commit**

```bash
git add src/firmware/boot_addr.h src/firmware/boot.h src/firmware/boot.c src/firmware/fw.h src/firmware/data/fw_data.h src/firmware/cmd_handlers.h src/firmware/cmd_set.c src/engine/thunks.c tests/test_fw_boot.c tests/diff_state.h tests/diff_state.c tests/test_diff_lts.c tests/test_diff_play.c tests/test_fw_par.c tests/test_golden_hybrid.c
git commit -m "The boot helpers in C: boot_read_dip, boot_init_ports, boot_init_settings, boot_clear_input_state, boot_set_defaults, boot_load_prosody_defaults, boot_init_baud, boot_init_host_lines and cmd_say_phonetic, with their hooks; diff_lts compares the helpers at every call (M4.6c2 2.1-2.3, 4.2, 4.4)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 5: The main loop's input side in C: `isr_idle` $C5FE, `boot_get_clause` $A697, `boot_loop_text` $A68D, `boot_queue_string` $C612, and the self-test, `boot_selftest` $C605 and `boot_selftest_sound` $A79F, with their hooks; the Review Focus tests 1, 3, 4 and 5 (spec §2.1–§2.6, §4.1, §4.2, §4.8 item 5)

**ROM facts checked / what was verified while proving the plan** (`rom_A697_boot_get_clause.txt`, `rom_A68D_boot_loop_text.txt`, `rom_C5FE_isr_idle.txt`, `rom_C612_boot_queue_string.txt`, `rom_C605_boot_selftest.txt`, `rom_A79F_boot_selftest_sound.txt`, `rom_C581_ser_tx_service.txt`, `rom_C3CC_ser_rx_put.txt`, `rom_C5EC_ser_rx_next_page.txt`, `rom_ACFA_cmd_scan_clause.txt`, `rom_B1E6_tn_convert_clause.txt`, `rom_CA92_pros_make_segments.txt`, `rom_CABD_pros_mark_stress.txt`; the corpus run of `diff_main` and the unit tests):
- **`boot_get_clause`'s entry** ($A697–$A6C0): X = 1, `boot_clause[0]` = ' '; $1827 (now `FW_CLAUSE_CARRY`), when not 0, into `boot_clause[1]` (X = 2) and cleared; then A = 0 into `ser_abort`, `tn_in_word`, $6B and `boot_speaking`; `SEB0 ser_rx_idle` when the ring is empty. "Empty" is two tests, the low bytes (`LDA ser_rx_write / CMP ser_rx_read / BNE`) and then the pages: a ring 256 bytes apart has equal low bytes and is not empty.
- **A pass with the ring empty** ($A6C2–$A6E2): `SEB0 ser_rx_idle`, `isr_par_ready`, `CLI` ($A6D3), then with `ser_status_due` bit 0 set `ser_tx_service` ($A6D7) and `BRA $A6C2`, else `isr_par_ready` ($A6DC), `isr_idle` and `BRA $A6C2`. The waits (spec §2.5): in serial mode `isr_idle`'s WIT is the input wait (`FW_WAIT_INPUT` at $C602); in parallel mode `isr_idle` returns at once and the pass changed nothing, so the C waits for an interrupt at $A6C2 (`FW_WAIT_INTERRUPT`); and `ser_tx_service` changes nothing when it returns at its `BBS0 boot_dip` ($C582) or `BBC2 P0` ($C585) — it then waits there too; when it sends, the pass changed the state (the byte sent, `ser_status_due` = `ser_tx_pending`) and the next one follows at once. So `ser_tx_service` now returns whether it sent (`par_play` ignores it). A pass of the ROM that comes back with nothing changed is the same pass again: waiting for the interrupt that can change it is the same machine state at every event. `diff_main`'s `status_waits` and `parallel` runs check both waits against the ROM once the main loop runs under the phase deadline (Task 6).
- **A byte** ($A6E4–$A6FF): `isr_par_hold`, `SEI` ($A6E7), `BBS0 ser_abort,boot_get_clause` (a mute: from the top, with I still set), `LDY #0 / LDA (ser_rx_read),Y`, `INC ser_rx_read` and at a page end `ser_rx_next_page` (Y = the page), `SEB0 boot_speaking`, `isr_par_ready`, `CLI` ($A6FF). The I flag set at $A6E7 matters only to a wait that follows, and none does before $A6FF; the C calls `set_interrupts` at all three so that the machine's I flag is the ROM's at every event (`diff_main` compares it).
- **The clause ends** ($A700–$A73D): a CR is not stored (a space instead); `.` `,` `:` `;` `!` `?` are stored and end it; then X ≥ $FA (`CPX #$FA / BCS`) ends it; below $DC (`BCC`) it goes on; from $DC a space ends it (stored), and `cmd_char` is stored, carried in $1827 and X stepped back over it (`DEX`), so the space overwrites it. Each end stores a space at X and `boot_clause_len` = X + 1 (`STX $54` after `INX`). The ROM tests in this order; the C keeps it.
- **The exit registers**: A = ' ', X = `boot_clause_len`, Y = 0 or the page of a page end; `boot_loop_text` declares none (the C `tn_convert_clause` sets none; its callers' `pros_make_segments` reads only Y, kept by `isr_par_ready`); each comment names the callers' reads.
- **The self-test** (`boot_selftest_sound` $A79F): `cmd_text_ptr` = $A7BE (`LDM` twice), `boot_selftest` ($C605: `boot_selftest_fail` = 0; with `boot_dip` bit 1 one read of P2, and 1 when P2.1 is high), $A7C5 when it failed, `boot_queue_string` (the string into the ring, $78 saved around each `ser_rx_put`, inside `isr_par_hold` … `isr_par_ready` and `PHP` / `SEI` … `PLP`), then `boot_loop_text`, `pros_make_segments`, `boot_loop_play`. The clause ends at the string's ',': its last character, a space, stays in the ring, and the main loop's first `boot_get_clause` reads it before the end of boot's idle; so the user's first clause starts with it (Review Focus 3: the CR alone gives the clause "   ", `boot_clause_len` 3).
- **The input that arrives while speech plays** (Review Focus 5): the machine delivers serial bytes only at the idle WIT ($C603, `maybe_deliver`), so no engine run feeds the ring during playback. `diff_main`'s `rf5_during` puts "Two." into the ring at the fifth frame event of "One." as `ser_rx_put` would (both engines): after the speech `boot_get_clause` finds the ring not empty and takes the clause with no wait, in C as in the ROM; the unit test `test_get_clause_paths` pins several clauses arriving in one wait.
- **Until boot_reset is C** (Task 6) the C main loop's waits run under the bridge's per-wait budget (10⁷ cycles) instead of the phase deadline, so the runs that never idle again (`diff_main`'s `status_waits` and `parallel`, `test_engine_loop`'s hybrid in parallel mode) would end in a firmware fault where the ROM engine reaches the deadline: they are skipped while `boot_get_clause` is C and `boot_reset` is not, and Task 6 removes the skips.
- **`diff_lts`**: `boot_selftest` and `boot_queue_string` join `k_every_call`, 52 calls each; the waits rows are not observed there.

**Files:**
- Create: `src/firmware/boot_input.h`, `src/firmware/boot_input.c`
- Modify: `src/firmware/boot.h`, `src/firmware/boot.c`, `src/firmware/ser.h`, `src/firmware/ser.c`, `src/firmware/fw.h`, `src/engine/thunks.c`, `tests/test_fw_boot.c`, `tests/test_fw_ser.c`, `tests/test_diff_lts.c`, `tests/test_diff_main.c`, `tests/test_golden_hybrid.c`, `tests/test_machine_hooks.c`

**Interfaces:**
- Consumes: Task 1's input wait, `fw_set_interrupts`, `f->stopped`; Task 4's boot helpers and rows; `ser_rx_put`, `ser_rx_next_page`, `ser_tx_service`, `isr_par_ready`, `isr_par_hold` (C since M4.6a/b), `cmd_scan_clause`, `tn_convert_clause`, `pros_make_segments`, `boot_loop_play`; `BOOT_SELFTEST_OK_CMDS`/`FAIL_CMDS` (Task 2).
- Produces (Task 6 calls them):

```c
/* src/firmware/boot_input.h (each void fn(fw *f, fw_regs *r)) */
isr_idle, boot_get_clause, boot_loop_text, boot_queue_string;
#define BOOT_CLAUSE_SPACE_END 0xDC, BOOT_CLAUSE_FULL 0xFA
/* src/firmware/boot.h */  boot_selftest, boot_selftest_sound;  #define P2_SELFTEST_PIN 1
/* src/firmware/ser.h */  bool ser_tx_service(fw *f, fw_regs *r);   /* true when it sent a byte */
/* tests/test_fw_boot.c: the feed script (script(), g_feed, g_mute_at), check_clause(), get_clause() */
```

No name collides: `grep -rnw "isr_idle\|boot_get_clause\|boot_loop_text\|boot_queue_string\|boot_selftest\|boot_selftest_sound\|BOOT_CLAUSE_FULL\|P2_SELFTEST_PIN" src tests` prints, before this task, only comments (the ROM routines' names in thunk comments, the address headers and test comments) and the row names of `diff_is_main_row` (`diff_state.c`, Task 4).

- [ ] **Step 1: Write the failing tests**

`test_fw_boot.c` gains the input side's tests (every clause end, the waits and the CLI/SEI order, a mute, a page end, a stop, several clauses in one wait, the self-test's ok and fail paths, the rows) on a feed script: each input wait puts the script's next string into the ring as the receive interrupt would. `test_fw_ser.c` checks `ser_tx_service`'s new result. `test_machine_hooks.c` runs the `isr_idle` thunk to a feed and to a stop. `diff_main` gains the Review Focus runs; `golden_hybrid` compares the CR alone on both engines; `diff_lts` compares the two helpers at every call.

**`tests/test_fw_boot.c`**: replace

```c
#include "firmware/boot.h"
#include "firmware/boot_addr.h"
#include "firmware/cmd_apply.h"
#include "firmware/isr_dsp.h"
```

with

```c
#include "firmware/boot.h"
#include "firmware/boot_addr.h"
#include "firmware/boot_input.h"
#include "firmware/cmd_apply.h"
#include "firmware/isr_dsp.h"
```

**`tests/test_fw_boot.c`**: replace

```c
static unsigned g_nlog, g_ncalls;
static fw_wait_result g_wait_result;   /* what the stub's wait returns */

static void log_add(ev_kind kind, uint16_t addr, uint8_t value)
```

with

```c
static unsigned g_nlog, g_ncalls;
static fw_wait_result g_wait_result;   /* what the stub's wait returns */
static const char *g_feed[8];          /* a feed script: what each input wait puts into the input ring ... */
static unsigned g_nfeed, g_feed_pos;   /* ... in turn (none: no script) */
static unsigned g_mute_at;             /* the feed (1, 2, ...) that also sets ser_abort bit 0 (0: none) */

/* s into the input ring, as the receive interrupt's ser_rx_put puts each byte. */
static void feed(const char *s)
{
    for (; *s; s++) {
        fw_regs t = { (uint8_t)*s, 0, 0, 0 };
        ser_rx_put(&g_f, &t);
    }
}

static void log_add(ev_kind kind, uint16_t addr, uint8_t value)
```

**`tests/test_fw_boot.c`**: replace

```c
/* Logs the wait. A wait for an interrupt is one handled: the timer interrupt sets isr_frame_flag (a frame is due at
   every wait). isr_timer_start's first wait sets ser_abort bit 0 (a mute as the DSP starts), so a boot_loop_play
   plays no segment: par_play goes from its first pass to its three rest frames. */
static fw_wait_result stub_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
```

with

```c
/* Logs the wait. A wait for an interrupt is one handled: the timer interrupt sets isr_frame_flag (a frame is due at
   every wait). isr_timer_start's first wait sets ser_abort bit 0 (a mute as the DSP starts), so a boot_loop_play
   plays no segment: par_play goes from its first pass to its three rest frames. With a feed script, an input wait,
   and a wait for an interrupt at boot_get_clause's loop ($A6C2), takes the script's next string into the input ring
   (the g_mute_at-th sets ser_abort bit 0 as well); once the script is used up such a wait stops the run. */
static fw_wait_result stub_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
```

**`tests/test_fw_boot.c`**: replace

```c
    if (kind == FW_WAIT_TIMER2)
        g_ram[SER_ABORT] |= 1;
    return g_wait_result;
}
```

with

```c
    if (kind == FW_WAIT_TIMER2)
        g_ram[SER_ABORT] |= 1;
    if (g_nfeed && (kind == FW_WAIT_INPUT || (kind == FW_WAIT_INTERRUPT && at == BOOT_GET_CLAUSE_LOOP))) {
        if (g_feed_pos == g_nfeed)
            return FW_WAIT_STOPPED;
        feed(g_feed[g_feed_pos++]);
        if (g_feed_pos == g_mute_at)
            g_ram[SER_ABORT] |= 1;
    }
    return g_wait_result;
}
```

**`tests/test_fw_boot.c`**: replace

```c
    g_ncalls = 0;
    g_wait_result = FW_WAIT_RETURNED;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
}

```

with

```c
    g_ncalls = 0;
    g_wait_result = FW_WAIT_RETURNED;
    g_nfeed = g_feed_pos = g_mute_at = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
    g_ram[SER_RX_READ + 1] = g_ram[SER_RX_WRITE + 1] = 0x21;   /* the input ring empty at $2100, as at boot */
}

/* A feed script of n strings (setup clears it). */
static void script(const char *const *s, unsigned n)
{
    for (unsigned i = 0; i < n && i < 8; i++)
        g_feed[i] = s[i];
    g_nfeed = n;
    g_feed_pos = 0;
}

```

**`tests/test_fw_boot.c`**: replace

```c
}

/* The rows of the boot helpers and cmd_say_phonetic (M4.6c2 spec 2.3): A, X and Y declared; cmd_say_phonetic waits. */
static void test_helper_rows(void)
```

with

```c
}

/* isr_idle $C5FE: in serial mode the WIT, one input wait ($C602); in parallel mode (boot_dip bit 0) nothing. A, X and Y
   kept. */
static void test_isr_idle(void)
{
    static const event want[] = { { EV_WAIT, FW_WAIT_INPUT_LOOP, FW_WAIT_INPUT } };
    setup();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_idle(&g_f, &r);
    check_log(want, 1);
    check_regs(&r, 0x11, 0x22, 0x33);
    setup();
    g_ram[BOOT_DIP] = DIP_PARALLEL;
    isr_idle(&g_f, &r);
    check_log(NULL, 0);
    check_regs(&r, 0x11, 0x22, 0x33);
    CHECK(!g_f.fault);
}

/* boot_clause is " " text " " with boot_clause_len its length. */
static void check_clause(const char *text)
{
    size_t n = strlen(text);
    CHECK_EQ(g_ram[BOOT_CLAUSE], ' ');
    CHECK(memcmp(g_ram + BOOT_CLAUSE + 1, text, n) == 0);
    CHECK_EQ(g_ram[BOOT_CLAUSE + 1 + n], ' ');
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], n + 2);
}

/* One boot_get_clause in serial mode on the feed script s (one string per input wait), cmd_char '@' (as
   boot_set_defaults leaves it); A = ' ' and X = boot_clause_len out, no fault, no stop. */
static void get_clause(const char *const *s, unsigned n)
{
    setup();
    script(s, n);
    g_ram[CMD_CHAR] = '@';
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    boot_get_clause(&g_f, &r);
    CHECK(!g_f.fault);
    CHECK(!g_f.stopped);
    CHECK_EQ(r.a, ' ');
    CHECK_EQ(r.x, g_ram[BOOT_CLAUSE_LEN]);
}

/* boot_get_clause $A697, serial mode, one byte per input wait as the machine delivers them: the entry's writes
   (boot_clause[0] = ' ', ser_abort, tn_in_word, $6B and boot_speaking 0, ser_rx_idle bit 0 set on the empty ring), then
   per byte a pass with the ring empty (CLI at $A6D3, isr_idle's input wait) and the read (SEI at $A6E7, CLI at $A6FF);
   isr_par_ready and isr_par_hold do nothing in serial mode. "Hi." ends at the '.', kept: " Hi. ", 5; boot_speaking
   bit 0 set by the read, ser_rx_idle's cleared by ser_rx_put. Y = 0. */
static void test_get_clause_waits(void)
{
    static const char *const hi[] = { "H", "i", "." };
    static const event want[] = {
        { EV_INT, 0, 1 }, { EV_WAIT, 0xC602, FW_WAIT_INPUT }, { EV_INT, 0, 0 }, { EV_INT, 0, 1 },
        { EV_INT, 0, 1 }, { EV_WAIT, 0xC602, FW_WAIT_INPUT }, { EV_INT, 0, 0 }, { EV_INT, 0, 1 },
        { EV_INT, 0, 1 }, { EV_WAIT, 0xC602, FW_WAIT_INPUT }, { EV_INT, 0, 0 }, { EV_INT, 0, 1 },
    };
    setup();
    script(hi, 3);
    g_ram[SER_ABORT] = g_ram[TN_IN_WORD] = g_ram[FW_UTT_CONTENT] = g_ram[BOOT_SPEAKING] = 0xFF;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    boot_get_clause(&g_f, &r);
    check_log(want, 12);
    check_regs(&r, ' ', 5, 0);
    check_clause("Hi.");
    CHECK_EQ(g_ram[SER_ABORT], 0);
    CHECK_EQ(g_ram[TN_IN_WORD], 0);
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0);
    CHECK_EQ(g_ram[BOOT_SPEAKING], 1);
    CHECK_EQ(g_ram[SER_RX_IDLE], 0);
    CHECK_EQ(g_ram[SER_RX_READ], 3);
    CHECK(!g_f.fault);
    CHECK(!g_f.stopped);
}

/* Every clause end of boot_get_clause (M4.6c2 spec 2.5): a CR (not kept: a space instead); . , : ; ! ? (kept); X
   reaching $FA (249 characters); from X $DC on a space (kept, then the space after it), or cmd_char, carried over in
   $1827 with X stepped back over it, and put first in the next clause. A space before X $DC does not end it. */
static void test_get_clause_ends(void)
{
    static const char *const cr[] = { "ab\r" };
    get_clause(cr, 1);
    check_clause("ab");
    static const char *const ends = ".,:;!?";
    for (int i = 0; i < 6; i++) {
        char s[4] = { 'a', ends[i], 'b', 0 };
        const char *one[] = { s };
        get_clause(one, 1);
        char want[3] = { 'a', ends[i], 0 };
        check_clause(want);
        CHECK_EQ(g_ram[SER_RX_READ], 2);           /* the 'b' stays in the ring */
    }
    static char word[300], spaced[300], carried[300];
    memset(word, 'w', 260);
    word[260] = 0;
    const char *full[] = { word };
    get_clause(full, 1);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 0xFB);        /* 249 letters at $01-$F9, the space at $FA */
    CHECK_EQ(g_ram[BOOT_CLAUSE + 0xF9], 'w');
    CHECK_EQ(g_ram[BOOT_CLAUSE + 0xFA], ' ');
    CHECK_EQ(g_ram[SER_RX_READ], 249);

    for (int i = 0; i < 218; i++)                  /* words of four letters and a space up to $DA, then a space */
        spaced[i] = i % 5 == 4 ? ' ' : 'w';
    spaced[217] = 'w';
    strcpy(spaced + 218, " more.");
    const char *sp[] = { spaced };
    get_clause(sp, 1);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 0xDD);        /* the space at $DB kept, another at $DC */
    CHECK_EQ(g_ram[BOOT_CLAUSE + 0xDB], ' ');
    CHECK_EQ(g_ram[BOOT_CLAUSE + 0xDC], ' ');
    CHECK_EQ(g_ram[BOOT_CLAUSE + 0xDA], 'w');
    CHECK_EQ(g_ram[FW_CLAUSE_CARRY], 0);

    memset(carried, 'w', 218);
    strcpy(carried + 218, "@W5.");
    const char *ca[] = { carried };
    get_clause(ca, 1);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 0xDC);        /* the '@' at $DB overwritten by the space */
    CHECK_EQ(g_ram[BOOT_CLAUSE + 0xDB], ' ');
    CHECK_EQ(g_ram[FW_CLAUSE_CARRY], '@');
    fw_regs r = { 0, 0, 0, 0 };
    boot_get_clause(&g_f, &r);                     /* the next clause: the '@' first, then "W5." */
    check_clause("@W5.");
    CHECK_EQ(g_ram[FW_CLAUSE_CARRY], 0);
    CHECK(!g_f.fault);
}

/* boot_get_clause's other paths: a mute pending when a byte is there (ser_abort bit 0, $A6E8) starts again from the
   top, dropping what was read; the read pointer's page end ($21FF: ser_rx_next_page, Y = the new page $22; $38FF
   wraps to $21); a stopped input wait abandons it (f->stopped, boot_clause_len not written). Review Focus 5 (M4.6c2
   plan): input that arrives while speech plays reaches the ring in one wait, several clauses at once: each later call
   takes its clause from the ring with no wait. */
static void test_get_clause_paths(void)
{
    static const char *const muted[] = { "ab", "c." };
    setup();
    script(muted, 2);
    g_mute_at = 2;
    fw_regs r = { 0, 0, 0, 0 };
    boot_get_clause(&g_f, &r);
    check_clause("c.");
    CHECK_EQ(g_ram[SER_ABORT], 0);

    static const uint16_t page[] = { 0x21, 0x38 }, next[] = { 0x22, 0x21 };
    for (int i = 0; i < 2; i++) {
        setup();
        g_ram[SER_RX_READ] = g_ram[SER_RX_WRITE] = 0xFF;
        g_ram[SER_RX_READ + 1] = g_ram[SER_RX_WRITE + 1] = (uint8_t)page[i];
        static const char *const dot[] = { "." };
        script(dot, 1);
        r = (fw_regs){ 0, 0, 0, 0 };
        boot_get_clause(&g_f, &r);
        check_clause(".");
        CHECK_EQ(r.y, next[i]);
        CHECK_EQ(g_ram[SER_RX_READ], 0);
        CHECK_EQ(g_ram[SER_RX_READ + 1], next[i]);
    }

    setup();
    static const char *const part[] = { "ab" };
    script(part, 1);
    g_ram[BOOT_CLAUSE_LEN] = 0x77;
    r = (fw_regs){ 0, 0, 0, 0 };
    boot_get_clause(&g_f, &r);
    CHECK(g_f.stopped);
    CHECK(!g_f.fault);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 0x77);
    CHECK_EQ(g_feed_pos, 1);

    setup();
    static const char *const two[] = { "One. Two? " };
    script(two, 1);
    r = (fw_regs){ 0, 0, 0, 0 };
    boot_get_clause(&g_f, &r);
    check_clause("One.");
    unsigned waits = g_nlog;
    boot_get_clause(&g_f, &r);
    check_clause(" Two?");
    for (unsigned i = waits; i < g_nlog && i < MAX_LOG; i++)
        CHECK(g_log[i].kind != EV_WAIT);           /* the second clause read with no wait */
    CHECK_EQ(g_ram[SER_RX_IDLE], 0);               /* the ring was not empty at its start */
    CHECK(!g_f.fault);
}

/* boot_get_clause's waits for an interrupt (M4.6c2 spec 2.5), where the ROM would loop with nothing changed. Parallel
   mode: isr_par_ready (SEB3 P0) twice, isr_idle returning at once, then a wait at $A6C2. A status byte due with the
   host line low: ser_tx_service reads P0 and sends nothing, a wait at $A6C2. With the host line high it sends (a NAK,
   nothing pending), a pass that changed state: the next pass follows at once (ser_status_due now 0: isr_idle's input
   wait). */
static void test_get_clause_idle_waits(void)
{
    static const char *const dot[] = { "." };
    static const event par[] = {
        { EV_READ, SFR_P0, 0x00 }, { EV_WRITE, SFR_P0, 0x08 }, { EV_INT, 0, 1 }, { EV_READ, SFR_P0, 0x08 },
        { EV_WRITE, SFR_P0, 0x08 }, { EV_WAIT, 0xA6C2, FW_WAIT_INTERRUPT }, { EV_READ, SFR_P0, 0x08 },
        { EV_WRITE, SFR_P0, 0x00 }, { EV_READ, SFR_ICR1, 0x00 }, { EV_WRITE, SFR_ICR1, 0x00 }, { EV_INT, 0, 0 },
        { EV_READ, SFR_P0, 0x00 }, { EV_WRITE, SFR_P0, 0x08 }, { EV_INT, 0, 1 },
    };
    setup();
    g_ram[BOOT_DIP] = DIP_PARALLEL;
    script(dot, 1);
    fw_regs r = { 0, 0, 0, 0 };
    boot_get_clause(&g_f, &r);
    check_log(par, 14);
    check_clause(".");

    static const event low[] = {
        { EV_INT, 0, 1 }, { EV_READ, SFR_P0, 0x00 }, { EV_WAIT, 0xA6C2, FW_WAIT_INTERRUPT }, { EV_INT, 0, 0 },
        { EV_INT, 0, 1 },
    };
    setup();
    g_ram[SER_STATUS_DUE] = 1;
    script(dot, 1);
    boot_get_clause(&g_f, &r);
    check_log(low, 5);
    check_clause(".");
    CHECK_EQ(g_ram[SER_STATUS_DUE], 1);

    static const event high[] = {
        { EV_INT, 0, 1 }, { EV_READ, SFR_P0, 0x04 }, { EV_READ, SFR_P0, 0x04 }, { EV_WRITE, SFR_P0, 0x04 },
        { EV_WAIT, 0xC500, FW_WAIT_UART_TX }, { EV_WRITE, SFR_UART_TX, 0x15 }, { EV_WAIT, 0xC500, FW_WAIT_UART_TX },
        { EV_READ, SFR_P0, 0x04 }, { EV_READ, SFR_P0, 0x04 }, { EV_WRITE, SFR_P0, 0x24 }, { EV_INT, 0, 1 },
        { EV_WAIT, 0xC602, FW_WAIT_INPUT }, { EV_INT, 0, 0 }, { EV_INT, 0, 1 },
    };
    setup();
    g_ram[SER_STATUS_DUE] = 1;
    g_sfr[SFR_P0] = 0x04;
    script(dot, 1);
    boot_get_clause(&g_f, &r);
    check_log(high, 14);
    check_clause(".");
    CHECK_EQ(g_ram[SER_STATUS_DUE], 0);
    CHECK(!g_f.fault);
}

/* boot_loop_text $A68D: boot_get_clause, then cmd_scan_clause (boot_clause to cmd_text, upper case) and
   tn_convert_clause (cmd_text to tn_phonetic). A stopped wait in boot_get_clause ends it there: no scan. */
static void test_loop_text(void)
{
    static const char *const hi[] = { "hi." };
    setup();
    fw_regs r = { 0, 0, 0, 0 };
    boot_set_defaults(&g_f, &r);
    script(hi, 1);
    boot_loop_text(&g_f, &r);
    CHECK(!g_f.fault);
    CHECK(memcmp(g_ram + CMD_TEXT, " HI. ", 5) == 0);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 5);
    CHECK(g_ram[TN_PHON_LEN] > 0);

    setup();
    g_wait_result = FW_WAIT_STOPPED;
    g_ram[CMD_TEXT] = 0x77;
    boot_loop_text(&g_f, &r);
    CHECK(g_f.stopped);
    CHECK_EQ(g_ram[CMD_TEXT], 0x77);
}

/* boot_queue_string $C612: the string at cmd_text_ptr (boot_selftest_ok_cmds) into the input ring up to its zero
   end, $78 = the index of its last byte; A, X and Y kept. Serial mode: no SFR; parallel mode: isr_par_hold (CLB3 P0,
   CLB1 ICR1) before and isr_par_ready (SEB3 P0) after. */
static void test_queue_string(void)
{
    static const event par[] = {
        { EV_READ, SFR_P0, 0x08 }, { EV_WRITE, SFR_P0, 0x00 }, { EV_READ, SFR_ICR1, 0x02 },
        { EV_WRITE, SFR_ICR1, 0x00 }, { EV_READ, SFR_P0, 0x00 }, { EV_WRITE, SFR_P0, 0x08 },
    };
    for (int mode = 0; mode <= 1; mode++) {
        setup();
        g_ram[BOOT_DIP] = (uint8_t)mode;
        g_sfr[SFR_P0] = 0x08;
        g_sfr[SFR_ICR1] = 0x02;
        g_ram[SER_RX_IDLE] = 1;
        fw_wr16(&g_f, CMD_TEXT_PTR, BOOT_SELFTEST_OK_CMDS);
        fw_regs r = { 0x11, 0x22, 0x33, 0 };
        boot_queue_string(&g_f, &r);
        check_log(mode ? par : NULL, mode ? 6 : 0);
        check_regs(&r, 0x11, 0x22, 0x33);
        CHECK(memcmp(g_ram + 0x2100, "\030@T5, ", 6) == 0);
        CHECK_EQ(g_ram[SER_RX_WRITE], 6);
        CHECK_EQ(g_ram[SER_RX_READ], 0);
        CHECK_EQ(g_ram[FW_TMP_78], 5);
        CHECK_EQ(g_ram[SER_RX_IDLE], 0);
        CHECK(!g_f.fault);
    }
}

/* boot_selftest $C605: boot_selftest_fail = 0, and 1 only with boot_dip bit 1 set and P2.1 high (one read of P2;
   none with bit 1 clear). A, X and Y kept. */
static void test_selftest(void)
{
    static const struct { uint8_t dip, p2, fail; unsigned reads; } k[] = {
        { 0x00, 0x72, 0, 0 }, { 0x02, 0x70, 0, 1 }, { 0x02, 0x72, 1, 1 },
    };
    for (int i = 0; i < 3; i++) {
        setup();
        g_ram[BOOT_DIP] = k[i].dip;
        g_ram[BOOT_SELFTEST_FAIL] = 0x55;
        g_sfr[SFR_P2] = k[i].p2;
        fw_regs r = { 0x11, 0x22, 0x33, 0 };
        boot_selftest(&g_f, &r);
        CHECK_EQ(g_ram[BOOT_SELFTEST_FAIL], k[i].fail);
        CHECK_EQ(g_nlog, k[i].reads);
        check_regs(&r, 0x11, 0x22, 0x33);
        CHECK(!g_f.fault);
    }
}

/* boot_selftest_sound $A79F: cmd_text_ptr = boot_selftest_ok_cmds, or boot_selftest_fail_cmds when the self-test
   failed (the fail path: M4.6c2 spec 4.4); the string queued into the input ring; its clause taken from the ring with
   no input wait (up to its ','; the space after it stays in the ring), scanned (its @T commands queued, cmd_count) and
   converted; then the speech (pros_make_segments, boot_loop_play: the DSP started, the stub's mute ending par_play at
   once). With the power-up settings, as boot_reset leaves them. */
static void test_selftest_sound(void)
{
    static const char *const text[] = { fw_boot_selftest_ok_cmds, fw_boot_selftest_fail_cmds };
    static const char *const clause[] = { " \030@T5, ", " \030@T7 @T7 @T7, " };
    for (int fail = 0; fail <= 1; fail++) {
        setup();
        fw_regs r = { 0, 0, 0, 0 };
        boot_set_defaults(&g_f, &r);
        r.a = cmd_apply_speaker(&g_f, &r.x, &r.y);
        g_ram[BOOT_DIP] = fail ? DIP_SELFTEST : 0;
        g_sfr[SFR_P2] = fail ? 0x72 : 0x70;
        g_nlog = 0;
        boot_selftest_sound(&g_f, &r);
        CHECK(!g_f.fault);
        CHECK(!g_f.stopped);
        CHECK_EQ(g_ncalls, 0);
        CHECK_EQ(g_ram[BOOT_SELFTEST_FAIL], fail);
        CHECK(memcmp(g_ram + SER_RX_RING, text[fail], strlen(text[fail])) == 0);
        CHECK_EQ(g_ram[SER_RX_WRITE], strlen(text[fail]));
        CHECK_EQ(g_ram[SER_RX_READ], strlen(text[fail]) - 1);
        CHECK(memcmp(g_ram + BOOT_CLAUSE, clause[fail], strlen(clause[fail])) == 0);
        CHECK_EQ(g_ram[CMD_COUNT], fail ? 3 : 1);   /* the @T commands of the clause */
        unsigned input = 0, timer2 = 0;
        for (unsigned k = 0; k < g_nlog && k < MAX_LOG; k++) {
            input += g_log[k].kind == EV_WAIT && g_log[k].value == FW_WAIT_INPUT;
            timer2 += g_log[k].kind == EV_WAIT && g_log[k].value == FW_WAIT_TIMER2;
        }
        CHECK_EQ(input, 0);
        CHECK_EQ(timer2, 1);
    }
}

/* The rows of the boot helpers and cmd_say_phonetic (M4.6c2 spec 2.3): A, X and Y declared; cmd_say_phonetic waits. */
static void test_helper_rows(void)
```

**`tests/test_fw_boot.c`**: replace

```c
    check_row("boot_init_host_lines", ROM_BOOT_INIT_HOST_LINES, REGS, false, false, false);
    check_row("cmd_say_phonetic", ROM_CMD_SAY_PHONETIC, REGS, true, false, false);
}

```

with

```c
    check_row("boot_init_host_lines", ROM_BOOT_INIT_HOST_LINES, REGS, false, false, false);
    check_row("cmd_say_phonetic", ROM_CMD_SAY_PHONETIC, REGS, true, false, false);
}

/* The rows of the input side and the self-test (M4.6c2 spec 2.3): the waits rows (boot_loop_text declares no output:
   the C tn_convert_clause sets no register) and two helpers. */
static void test_input_rows(void)
{
    check_row("boot_loop_text", ROM_BOOT_LOOP_TEXT, 0, true, false, false);
    check_row("boot_get_clause", ROM_BOOT_GET_CLAUSE, REGS, true, false, false);
    check_row("isr_idle", ROM_ISR_IDLE, REGS, true, false, false);
    check_row("boot_selftest_sound", ROM_BOOT_SELFTEST_SOUND, REGS, true, false, false);
    check_row("boot_selftest", ROM_BOOT_SELFTEST, REGS, false, false, false);
    check_row("boot_queue_string", ROM_BOOT_QUEUE_STRING, REGS, false, false, false);
}

```

**`tests/test_fw_boot.c`**: replace

```c
    test_init_host_lines();
    test_helper_rows();
}
```

with

```c
    test_init_host_lines();
    test_helper_rows();
    test_isr_idle();
    test_get_clause_waits();
    test_get_clause_ends();
    test_get_clause_paths();
    test_get_clause_idle_waits();
    test_loop_text();
    test_queue_string();
    test_selftest();
    test_selftest_sound();
    test_input_rows();
}
```

**`tests/test_fw_ser.c`**: replace

```c
   pinned here: neither the machine (no host line) nor diff_serial (interrupts held, no time passing between two
   reads of an input) can drop it, the ROM's own time between $C585 and $C59B could: no SEB5. A and X kept, Y as
   ser_tx_get leaves it. */
static void test_tx_service(void)
{
```

with

```c
   pinned here: neither the machine (no host line) nor diff_serial (interrupts held, no time passing between two
   reads of an input) can drop it, the ROM's own time between $C585 and $C59B could: no SEB5. A and X kept, Y as
   ser_tx_get leaves it. It returns whether it sent a byte (M4.6c2 spec 2.5). */
static void test_tx_service(void)
{
```

**`tests/test_fw_ser.c`**: replace

```c
    tx_ring(0x3900, 0x3901, 1, 1);
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    ser_tx_service(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 0x33);
    CHECK_EQ(g_nlog, 0);
```

with

```c
    tx_ring(0x3900, 0x3901, 1, 1);
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    CHECK(!ser_tx_service(&g_f, &r));
    check_regs(&r, 0x11, 0x22, 0x33);
    CHECK_EQ(g_nlog, 0);
```

**`tests/test_fw_ser.c`**: replace

```c
    tx_ring(0x3900, 0x3901, 1, 1);
    g_ram[SER_STATUS_DUE] = 1;
    ser_tx_service(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 0x33);
    CHECK_EQ(g_nlog, 1);
```

with

```c
    tx_ring(0x3900, 0x3901, 1, 1);
    g_ram[SER_STATUS_DUE] = 1;
    CHECK(!ser_tx_service(&g_f, &r));
    check_regs(&r, 0x11, 0x22, 0x33);
    CHECK_EQ(g_nlog, 1);
```

**`tests/test_fw_ser.c`**: replace

```c
    g_ram[0x3901] = 'b';
    tx_ring(0x3900, 0x3902, 2, 1);
    ser_tx_service(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 1);
    CHECK_EQ(g_nlog, 5);
```

with

```c
    g_ram[0x3901] = 'b';
    tx_ring(0x3900, 0x3902, 2, 1);
    CHECK(ser_tx_service(&g_f, &r));
    check_regs(&r, 0x11, 0x22, 1);
    CHECK_EQ(g_nlog, 5);
```

**`tests/test_fw_ser.c`**: replace

```c
    check_log(4, LOG_WRITE, SFR_UART_TX, 'a');
    CHECK_EQ(g_ram[SER_STATUS_DUE], 1);
    ser_tx_service(&g_f, &r);
    check_regs(&r, 0x11, 0x22, 0);
    CHECK_EQ(g_nlog, 14);
```

with

```c
    check_log(4, LOG_WRITE, SFR_UART_TX, 'a');
    CHECK_EQ(g_ram[SER_STATUS_DUE], 1);
    CHECK(ser_tx_service(&g_f, &r));
    check_regs(&r, 0x11, 0x22, 0);
    CHECK_EQ(g_nlog, 14);
```

**`tests/test_fw_ser.c`**: replace

```c
    g_ram[0x3900] = 'c';
    tx_ring(0x3900, 0x3901, 1, 1);
    ser_tx_service(&g_f, &r);
    CHECK_EQ(g_nlog, 7);
    check_log(5, LOG_WAIT, FW_WAIT_UART_TX, 0);
```

with

```c
    g_ram[0x3900] = 'c';
    tx_ring(0x3900, 0x3901, 1, 1);
    CHECK(ser_tx_service(&g_f, &r));
    CHECK_EQ(g_nlog, 7);
    check_log(5, LOG_WAIT, FW_WAIT_UART_TX, 0);
```

**`tests/test_machine_hooks.c`**: replace

```c
#include "engine/thunks.h"
#include "firmware/bank_addr.h"
#include "firmware/isr_addr.h"
#include "firmware/par_addr.h"
```

with

```c
#include "engine/thunks.h"
#include "firmware/bank_addr.h"
#include "firmware/boot_addr.h"
#include "firmware/isr_addr.h"
#include "firmware/par_addr.h"
```

**`tests/test_machine_hooks.c`**: replace

```c
}

/* The bridge's DSP port and frame event (M4.6b spec 3.1): fw_dsp_write is a bus write of $8000 (counted, grouped
   into the raw frame), fw_frame_done the machine's frame capture. */
```

with

```c
}

/* The isr_idle thunk (M4.6c2 spec 3.1) entered by $9002 JSR $C5FE on a synthetic machine (load_rx's receive
   interrupt, boot_dip $10 = 0: serial mode). The C waits for input at the idle point; fed, the byte's interrupt is
   handled and the thunk returns as the RTS would, A kept (the STA $10 after it stores 5). Stopped: the run ends there:
   the machine stays at the idle point $C603, waiting, the JSR's return address and A pushed (SP $FC), one stop
   counted, the bridge's trap and deadline clear; machine_run then returns MACHINE_IDLE. */
static void test_isr_idle_thunk(void)
{
    static fw_bridge b;
    static const uint8_t rx_isr[] = { 0x48, 0xA5, 0xE5, 0x85, 0x21, 0xE6, 0x22, 0x68, 0x40 };
    load_main(ROM_ISR_IDLE);
    put(SYN_RX_ISR, rx_isr, sizeof rx_isr);
    vector(0xFFFA, SYN_RX_ISR);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    g_m.bus.write(g_m.bus.ctx, M50734_UART_CTRL, 0x10);
    CHECK(fw_bridge_init(&b, &g_m));
    CHECK(fw_install(&g_m, &b, "isr_idle"));
    machine_set_idle_handler(&g_m, idle_feed_once, NULL);
    g_idles = 0;
    g_m.cpu.p = 0;
    run_to(&g_m, 0x9007);
    CHECK_EQ(g_idles, 1);
    CHECK_EQ(g_m.ram[0x21], 'A');
    CHECK_EQ(g_m.ram[0x10], 0x05);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    CHECK_EQ(b.stops, 0);

    g_m.ram[0x10] = 0;                           /* boot_dip: serial mode again */
    g_m.cpu.pc = 0x9000;
    run_to(&g_m, 0xC603);
    CHECK(g_m.cpu.waiting);
    CHECK_EQ(g_idles, 2);
    CHECK_EQ(g_m.cpu.s, 0xFC);
    CHECK_EQ(g_m.ram[0x1FD], 0x05);
    CHECK_EQ(b.stops, 1);
    CHECK(!b.f.stopped);
    CHECK(b.f.trap == NULL);
    CHECK_EQ(b.deadline, 0);
    CHECK_EQ(machine_run(&g_m, 1000), MACHINE_IDLE);
    CHECK_EQ(g_m.fault, MFAULT_NONE);
}

/* The bridge's DSP port and frame event (M4.6b spec 3.1): fw_dsp_write is a bus write of $8000 (counted, grouped
   into the raw frame), fw_frame_done the machine's frame capture. */
```

**`tests/test_machine_hooks.c`**: replace

```c
    test_delivery_guard();
    test_host_input();
}
```

with

```c
    test_delivery_guard();
    test_host_input();
    test_isr_idle_thunk();
}
```

**`tests/test_diff_main.c`**: replace

```c
    frame_list fl = { 0 };
    g_extra = x;

    g_rom_side = true;
```

with

```c
    frame_list fl = { 0 };
    g_extra = x;
    if (x && x->want == MACHINE_CYCLE_LIMIT && fw_replacement_find("boot_get_clause") &&
        !fw_replacement_find("boot_reset")) {
        printf("  %-14s skipped: until boot_reset is C, the C main loop's waits run under no phase deadline\n",
               x->name);
        return;
    }

    g_rom_side = true;
```

**`tests/test_diff_main.c`**: replace

```c
    m->sfr.port_in[0] |= P0_HOST_LINE;
}

static const extra_run k_extra[] = {
```

with

```c
    m->sfr.port_in[0] |= P0_HOST_LINE;
}
/* At a frame event of the speech: "Two." arrives in the input ring, each byte put as ser_rx_put puts it (the machine
   delivers serial bytes at the idle WIT only, never while speech plays). */
static void inj_ring_two(machine *m)
{
    for (const char *s = "Two."; *s; s++) {
        m->ram[m->ram[SER_RX_WRITE] | m->ram[SER_RX_WRITE + 1] << 8] = (uint8_t)*s;
        if (++m->ram[SER_RX_WRITE] == 0) {
            uint8_t page = (uint8_t)(m->ram[SER_RX_WRITE + 1] + 1);
            m->ram[SER_RX_WRITE + 1] = page == 0x39 ? 0x21 : page;
        }
    }
    m->ram[SER_RX_IDLE] &= (uint8_t)~1;
}

/* Words for the long clauses (44 characters) */
#define WORDS44 "the quick brown fox jumps over the lazy dog "

static const extra_run k_extra[] = {
```

**`tests/test_diff_main.c`**: replace

```c
    /* parallel mode: isr_idle returns at once ($C603 by its BBS0), no idle; the boot announcement, then the deadline */
    { "parallel", "Hello.", P4_DIP_PARALLEL, 0, 0, NULL, AT_IDLE, 0, 0xC603, false, MACHINE_CYCLE_LIMIT },
};

```

with

```c
    /* parallel mode: isr_idle returns at once ($C603 by its BBS0), no idle; the boot announcement, then the deadline */
    { "parallel", "Hello.", P4_DIP_PARALLEL, 0, 0, NULL, AT_IDLE, 0, 0xC603, false, MACHINE_CYCLE_LIMIT },
    /* the M4.6c2 plan's Review Focus 1: no punctuation and no CR inside, longer than a clause: cut at X >= $DC on a
       space ($A731 CMP #$20 reached); one word of 260 letters, cut at X >= $FA; and cmd_char at $DB (after 218
       characters), carried in $1827 into the next clause ($A739) */
    { "rf1_space", WORDS44 WORDS44 WORDS44 WORDS44 WORDS44 WORDS44, 0, 0, 0, NULL, AT_IDLE, 0, 0xA731, false,
      MACHINE_IDLE },
    { "rf1_full", "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij"
                  "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij"
                  "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij",
      0, 0, 0, NULL, AT_IDLE, 0, 0xA731, false, MACHINE_IDLE },
    { "rf1_carry", WORDS44 WORDS44 WORDS44 WORDS44 "the quick brown fox jumps over the lazy do@W5 now slower.", 0, 0, 0,
      NULL, AT_IDLE, 0, 0xA739, false, MACHINE_IDLE },
    /* Review Focus 3: an empty input, the CR alone: a one-character clause, then idle again */
    { "rf3_empty", "", 0, 0, 0, NULL, AT_IDLE, 0, 0xA704, false, MACHINE_IDLE },
    /* Review Focus 4: commands only (cmd_set_speed $A9C9 run), nothing spoken */
    { "rf4_commands", "@W5 @A3", 0, 0, 0, NULL, AT_IDLE, 0, 0xA9C9, false, MACHINE_IDLE },
    /* Review Focus 5: "Two." arrives in the ring during the speech of "One.": taken after it with no wait */
    { "rf5_during", "One.", 0, 0, 0, inj_ring_two, AT_SPEECH, 5, 0xA6E4, false, MACHINE_IDLE },
};

```

**`tests/test_golden_hybrid.c`**: replace

```c
    unsigned reply_len;
    uint8_t tx_pending, bank_current, bank_ids[98];
    unsigned unwinds;
} outcome;
```

with

```c
    unsigned reply_len;
    uint8_t tx_pending, bank_current, bank_ids[98];
    uint8_t clause_len, clause[8];   /* boot_clause_len and boot_clause's first bytes: the last clause read */
    unsigned unwinds;
} outcome;
```

**`tests/test_golden_hybrid.c`**: replace

```c
    o->bank_current = g_m.ram[BANK_CURRENT];
    memcpy(o->bank_ids, g_m.ram + BANK_ID_TABLE, sizeof o->bank_ids);
    o->unwinds = stats.unwinds;
}

/* Both engines on in: the same status, fault, frames, queued reply and bank state. Returns the hybrid's unwinds. */
static unsigned check_same(const uint8_t *rom, size_t len, const char *name, const char *in, size_t n, outcome *a)
{
```

with

```c
    o->bank_current = g_m.ram[BANK_CURRENT];
    memcpy(o->bank_ids, g_m.ram + BANK_ID_TABLE, sizeof o->bank_ids);
    o->clause_len = g_m.ram[BOOT_CLAUSE_LEN];
    memcpy(o->clause, g_m.ram + BOOT_CLAUSE, sizeof o->clause);
    o->unwinds = stats.unwinds;
}

/* Both engines on in: the same status, fault, frames, queued reply, bank state and last clause. Returns the hybrid's
   unwinds. */
static unsigned check_same(const uint8_t *rom, size_t len, const char *name, const char *in, size_t n, outcome *a)
{
```

**`tests/test_golden_hybrid.c`**: replace

```c
                !strcmp(a->frames, b.frames) && a->reply_len == b.reply_len &&
                !memcmp(a->reply, b.reply, a->reply_len) && a->tx_pending == b.tx_pending &&
                a->bank_current == b.bank_current && !memcmp(a->bank_ids, b.bank_ids, sizeof a->bank_ids);
    if (!same) {
        fprintf(stderr, "  review %s: rom status %d fault %d, hybrid status %d fault %d (or frames, reply, banks)\n",
                name, (int)a->st, (int)a->fault, (int)b.st, (int)b.fault);
        g_test_failures++;
```

with

```c
                !strcmp(a->frames, b.frames) && a->reply_len == b.reply_len &&
                !memcmp(a->reply, b.reply, a->reply_len) && a->tx_pending == b.tx_pending &&
                a->bank_current == b.bank_current && !memcmp(a->bank_ids, b.bank_ids, sizeof a->bank_ids) &&
                a->clause_len == b.clause_len && !memcmp(a->clause, b.clause, sizeof a->clause);
    if (!same) {
        fprintf(stderr, "  review %s: rom status %d fault %d, hybrid status %d fault %d (or frames, reply, banks, "
                "clause)\n",
                name, (int)a->st, (int)a->fault, (int)b.st, (int)b.fault);
        g_test_failures++;
```

**`tests/test_golden_hybrid.c`**: replace

```c
}

/* ---- the engines' loop (M4.6c2 spec 3.1, 3.2) ---- */

```

with

```c
}

/* The M4.6c2 plan's Review Focus 3: the CR alone (an empty input) on both engines, the C boot_get_clause taking it
   as the clause's last character: both idle again with no frame. The clause is three spaces (boot_clause_len 3): the
   one boot_clause[0] always holds, the one the self-test string left in the ring at boot (boot_get_clause read it
   before the end of boot's idle), and the one the CR becomes. */
static void test_review_focus_empty(const uint8_t *rom, size_t len)
{
    outcome a;
    check_same(rom, len, "rf3", "", 0, &a);
    CHECK_EQ(a.st, MACHINE_IDLE);
    CHECK_STR(a.frames, "");
    CHECK_EQ(a.clause_len, 3);
    CHECK(!memcmp(a.clause, "   ", 3));
    free(a.frames);
}

/* ---- the engines' loop (M4.6c2 spec 3.1, 3.2) ---- */

```

**`tests/test_golden_hybrid.c`**: replace

```c
        frame_list_free(&fl);

        machine_config cfg;
        machine_default_config(&cfg);
```

with

```c
        frame_list_free(&fl);

        if (hybrid && fw_replacement_find("boot_get_clause") && !fw_replacement_find("boot_reset"))
            continue;   /* until boot_reset is C, the C main loop's waits run under no phase deadline */
        machine_config cfg;
        machine_default_config(&cfg);
```

**`tests/test_golden_hybrid.c`**: replace

```c
    test_engine_loop(rom, len);
    test_review_focus(rom, len);
    check_corpus(rom, len, NULL);
    for (size_t i = 0; i < fw_replacement_count; i++)
```

with

```c
    test_engine_loop(rom, len);
    test_review_focus(rom, len);
    test_review_focus_empty(rom, len);
    check_corpus(rom, len, NULL);
    for (size_t i = 0; i < fw_replacement_count; i++)
```

**`tests/test_diff_lts.c`**: replace

```c
    "bank_read_id", "bank_put_id_char", "bank_safe_char", "bank_find_current", "cmd_run_next", "boot_read_dip",
    "boot_init_ports", "boot_init_settings", "boot_clear_input_state", "boot_set_defaults",
    "boot_load_prosody_defaults", "boot_init_baud", "boot_init_host_lines",
};

```

with

```c
    "bank_read_id", "bank_put_id_char", "bank_safe_char", "bank_find_current", "cmd_run_next", "boot_read_dip",
    "boot_init_ports", "boot_init_settings", "boot_clear_input_state", "boot_set_defaults",
    "boot_load_prosody_defaults", "boot_init_baud", "boot_init_host_lines", "boot_selftest", "boot_queue_string",
};

```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw_boot"`
Expected: the build stops with compile errors (the proof run, first lines):

```
tests\test_fw_boot.c(11): fatal error C1083: Cannot open include file: 'firmware/boot_input.h': No such file or directory
tests\test_fw_ser.c(395): error C2171: operator '!' cannot be applied to an operand of type 'void'
tests\test_fw_ser.c(395): error C2180: controlling expression has type 'void'
tests\test_fw_ser.c(404): error C2171: operator '!' cannot be applied to an operand of type 'void'
```

- [ ] **Step 3: Implement**

**Create `src/firmware/boot_input.h`:**

```c
#ifndef FW_BOOT_INPUT_H
#define FW_BOOT_INPUT_H
/* The main loop's input side (ROM $A68D-$A741, $C5FE-$C604, $C612-$C631), decompiled (M4.6c2 spec 2.1, 2.5). Each
   routine takes the registers at entry in r and leaves in r the A, X and Y the ROM leaves at its RTS; r->p is not
   used: no caller reads a flag. A polling loop never spins in C (M4.6c2 spec 2.5): where the ROM waits for input,
   the C waits through the host (FW_WAIT_INPUT); where it would loop with nothing changed, the C waits for an
   interrupt first (FW_WAIT_INTERRUPT). A wait that stops the run (f->stopped) abandons the C: under a thunk a longjmp,
   without a trap the routines return at once. */
#include "fw.h"

#define BOOT_CLAUSE_SPACE_END  0xDC   /* boot_get_clause: from this X on, a space or cmd_char ends the clause ... */
#define BOOT_CLAUSE_FULL       0xFA   /* ... and from this X on any character */

/* $C5FE: in serial mode (boot_dip bit 0 clear) the WIT at $C602: a wait for input (FW_WAIT_INPUT) until an interrupt
   has been handled; in parallel mode it returns at once. A, X and Y kept (PHA ... PLA). */
void isr_idle(fw *f, fw_regs *r);
/* $A697: one clause of input into boot_clause. boot_clause[0] = ' ', then the command character carried over in
   FW_CLAUSE_CARRY ($1827, cleared); ser_abort, tn_in_word, FW_UTT_CONTENT and boot_speaking = 0; ser_rx_idle bit 0
   set while the input ring is empty. A pass with the ring empty: ser_rx_idle bit 0, isr_par_ready, CLI, then with a
   status byte due (ser_status_due bit 0) ser_tx_service, else isr_par_ready and isr_idle; a pass on which the ROM
   would loop with nothing changed (ser_tx_service sent nothing; isr_idle in parallel mode) waits for an interrupt at
   $A6C2. With a byte in the ring: isr_par_hold, SEI, a mute pending (ser_abort bit 0) starts again from the top; else
   the byte is read (ser_rx_read steps on, ser_rx_next_page at a page end), boot_speaking bit 0 set, isr_par_ready,
   CLI. A CR ends the clause unkept; . , : ; ! ? end it kept; so does X reaching $FA, and from X $DC on a space or
   cmd_char (which is carried over, X stepped back over it). A space is then stored and boot_clause_len = X + 1. A =
   ' ', X = boot_clause_len, Y = 0 (or the page ser_rx_next_page gave the last byte). */
void boot_get_clause(fw *f, fw_regs *r);
/* $A68D: boot_get_clause, cmd_scan_clause, tn_convert_clause (C calls C). r as boot_get_clause leaves it: the C
   cmd_scan_clause and tn_convert_clause set no register. */
void boot_loop_text(fw *f, fw_regs *r);
/* $C612: the zero-terminated string at cmd_text_ptr into the input ring (ser_rx_put, Y saved in $78 around each),
   after isr_par_hold and before isr_par_ready. A, X and Y kept. */
void boot_queue_string(fw *f, fw_regs *r);

#endif
```

**Create `src/firmware/boot_input.c`:**

```c
#include "boot_input.h"
#include "boot.h"
#include "boot_addr.h"
#include "cmd_scan.h"
#include "fw_ops.h"
#include "isr_serial.h"
#include "ser.h"
#include "tn_convert.h"

#define BOOT_CR  0x0D   /* boot_get_clause: ends the clause, not kept */

/* $A6B4-$A6BE, $A6C2-$A6CC: the input ring is empty (ser_rx_write = ser_rx_read: the low bytes, then the pages). */
static bool ring_empty(const fw *f)
{
    return fw_rd8(f, SER_RX_WRITE) == fw_rd8(f, SER_RX_READ) &&
           fw_rd8(f, SER_RX_READ + 1) == fw_rd8(f, SER_RX_WRITE + 1);
}

/* $A6CE-$A6E2: a pass with the input ring empty. The ROM loops at once ($A6DA, $A6E2 BRA $A6C2); where that pass
   changed nothing (ser_tx_service sent nothing: the host line low, or parallel mode; isr_idle in parallel mode, which
   returns at once) the C waits for an interrupt first, which is what can change the state (M4.6c2 spec 2.5). */
static void empty_pass(fw *f, fw_regs *r)
{
    fw_seb(f, SER_RX_IDLE, 0);
    fw_par_ready(f, *r);                                         /* $A6D0 */
    fw_set_interrupts(f, true, BOOT_CLI_EMPTY);                  /* $A6D3 CLI */
    bool changed;
    if (fw_rd8(f, SER_STATUS_DUE) & 1) {                         /* $A6D4 BBC0 ser_status_due */
        changed = ser_tx_service(f, r);                          /* $A6D7 */
    } else {
        fw_par_ready(f, *r);                                     /* $A6DC */
        isr_idle(f, r);                                          /* $A6DF: in serial mode, the wait for input */
        changed = !(fw_rd8(f, BOOT_DIP) & DIP_PARALLEL);
    }
    if (!changed)
        fw_wait(f, FW_WAIT_INTERRUPT, BOOT_GET_CLAUSE_LOOP);
}

/* $A704: a space after the clause, boot_clause_len = X + 1. */
static void end_clause(fw *f, fw_regs *r)
{
    r->a = ' ';
    fw_wr8(f, (uint16_t)(BOOT_CLAUSE + r->x), r->a);
    r->x++;
    fw_wr8(f, BOOT_CLAUSE_LEN, r->x);
}

/* $A6EB-$A73F: the byte at ser_rx_read into boot_clause[X]; true when it ended the clause. */
static bool take_byte(fw *f, fw_regs *r)
{
    r->y = 0;
    r->a = fw_ind_y(f, (uint8_t)SER_RX_READ, r->y);              /* $A6EB LDY #0; LDA (ser_rx_read),Y */
    uint8_t lo = (uint8_t)(fw_rd8(f, SER_RX_READ) + 1);          /* INC ser_rx_read; BNE */
    fw_wr8(f, SER_RX_READ, lo);
    if (lo == 0) {
        r->y = fw_rd8(f, SER_RX_READ + 1);                       /* $A6F3 */
        ser_rx_next_page(f, r);
        fw_wr8(f, SER_RX_READ + 1, r->y);
    }
    fw_seb(f, BOOT_SPEAKING, 0);                                 /* $A6FA */
    fw_par_ready(f, *r);
    fw_set_interrupts(f, true, BOOT_CLI_READ);                   /* $A6FF CLI */
    if (r->a == BOOT_CR) {                                       /* CMP #$0D; BNE */
        end_clause(f, r);
        return true;
    }
    fw_wr8(f, (uint16_t)(BOOT_CLAUSE + r->x), r->a);             /* $A70D STA $1B00,X; INX */
    r->x++;
    bool ends = r->a == '.' || r->a == ',' || r->a == ':' || r->a == ';' || r->a == '!' || r->a == '?' ||
                r->x >= BOOT_CLAUSE_FULL;                        /* CPX #$FA; BCS */
    if (!ends && r->x >= BOOT_CLAUSE_SPACE_END) {                /* CPX #$DC; BCC */
        if (r->a == ' ') {
            ends = true;
        } else if (r->a == fw_rd8(f, CMD_CHAR)) {                /* CMP cmd_char */
            fw_wr8(f, FW_CLAUSE_CARRY, r->a);                    /* $A739: carried into the next clause */
            r->x--;
            ends = true;
        }
    }
    if (ends)
        end_clause(f, r);
    return ends;                                                 /* else $A73F JMP $A6C2 */
}

void isr_idle(fw *f, fw_regs *r)
{
    (void)r;                                                     /* PHA ... PLA: A kept */
    if (!(fw_rd8(f, BOOT_DIP) & DIP_PARALLEL))                   /* $C5FF BBS0 boot_dip,$C603 */
        fw_wait(f, FW_WAIT_INPUT, FW_WAIT_INPUT_LOOP);           /* $C602 WIT */
}

void boot_get_clause(fw *f, fw_regs *r)
{
    for (;;) {                                                   /* $A697, and again after a mute ($A6E8) */
        r->x = 1;
        r->a = ' ';
        fw_wr8(f, BOOT_CLAUSE, r->a);
        r->a = fw_rd8(f, FW_CLAUSE_CARRY);                       /* LDA $1827; BEQ */
        if (r->a) {
            fw_wr8(f, BOOT_CLAUSE + 1, r->a);
            r->x++;
            r->a = 0;
            fw_wr8(f, FW_CLAUSE_CARRY, r->a);
        }
        fw_wr8(f, SER_ABORT, r->a);                              /* $A6AC: A = 0 */
        fw_wr8(f, TN_IN_WORD, r->a);
        fw_wr8(f, FW_UTT_CONTENT, r->a);
        fw_wr8(f, BOOT_SPEAKING, r->a);
        if (ring_empty(f))                                       /* $A6B4-$A6C0 */
            fw_seb(f, SER_RX_IDLE, 0);
        for (;;) {                                               /* $A6C2 */
            while (ring_empty(f)) {
                empty_pass(f, r);
                if (f->fault || f->stopped)
                    return;                                      /* abandoned (without a trap: a unit test) */
            }
            isr_par_hold(f, r);                                  /* $A6E4 */
            fw_set_interrupts(f, false, BOOT_SEI_READ);          /* $A6E7 SEI */
            if (fw_rd8(f, SER_ABORT) & 1)                        /* $A6E8 BBS0 ser_abort,boot_get_clause */
                break;
            if (take_byte(f, r))
                return;
        }
    }
}

void boot_loop_text(fw *f, fw_regs *r)
{
    boot_get_clause(f, r);
    if (f->fault || f->stopped)
        return;
    cmd_scan_clause(f);
    tn_convert_clause(f);
}

void boot_queue_string(fw *f, fw_regs *r)
{
    fw_regs t = *r;                                              /* PHA / TYA / PHA ... PLA / TAY / PLA: A, Y kept */
    t.y = 0;
    isr_par_hold(f, &t);                                         /* $C617, then PHP / SEI ... PLP */
    for (;;) {
        t.a = fw_ind_y(f, (uint8_t)CMD_TEXT_PTR, t.y);           /* $C61C LDA ($46),Y; BEQ */
        if (t.a == 0)
            break;
        fw_wr8(f, FW_TMP_78, t.y);                               /* STY $78 */
        ser_rx_put(f, &t);
        t.y = fw_rd8(f, FW_TMP_78);                              /* LDY $78; INY */
        t.y++;
    }
    isr_par_ready(f, &t);                                        /* $C62B */
    r->x = t.x;
}
```

**`src/firmware/boot.h`**: replace

```c
#define DIP_PARALLEL     0x01   /* boot_dip bit 0: the parallel interface */
#define DIP_SELFTEST     0x02   /* boot_dip bit 1: the self-test reads P2.1 (and the alternative bank table) */

/* $ADE0: AD_CTRL = 0, then boot_dip = A = P4 & $0F. X and Y untouched. */
```

with

```c
#define DIP_PARALLEL     0x01   /* boot_dip bit 0: the parallel interface */
#define DIP_SELFTEST     0x02   /* boot_dip bit 1: the self-test reads P2.1 (and the alternative bank table) */
#define P2_SELFTEST_PIN  1      /* P2.1: the self-test input */

/* $ADE0: AD_CTRL = 0, then boot_dip = A = P4 & $0F. X and Y untouched. */
```

**`src/firmware/boot.h`**: replace

```c
   boot_loop_play (C calls C); it waits as boot_loop_play does. A, X and Y as boot_loop_play leaves them. */
void cmd_say_phonetic(fw *f, fw_regs *r);

#endif
```

with

```c
   boot_loop_play (C calls C); it waits as boot_loop_play does. A, X and Y as boot_loop_play leaves them. */
void cmd_say_phonetic(fw *f, fw_regs *r);
/* $C605: boot_selftest_fail = 1 when boot_dip bit 1 is set and the input P2.1 reads high (one read of P2), else 0.
   A, X and Y untouched. */
void boot_selftest(fw *f, fw_regs *r);
/* $A79F: cmd_text_ptr = boot_selftest_ok_cmds, boot_selftest, boot_selftest_fail_cmds when it failed;
   boot_queue_string; then the main loop's steps once: boot_loop_text, pros_make_segments, boot_loop_play (C calls
   C). A, X and Y as boot_loop_play leaves them. */
void boot_selftest_sound(fw *f, fw_regs *r);

#endif
```

**`src/firmware/boot.c`**: replace

```c
#include "boot.h"
#include "bank_addr.h"
#include "cmd_apply.h"
#include "cmd_handlers.h"
```

with

```c
#include "boot.h"
#include "bank_addr.h"
#include "boot_input.h"
#include "cmd_apply.h"
#include "cmd_handlers.h"
```

**`src/firmware/boot.c`**: replace

```c
    boot_loop_play(f, r);
}
```

with

```c
    boot_loop_play(f, r);
}

void boot_selftest(fw *f, fw_regs *r)
{
    (void)r;
    fw_wr8(f, BOOT_SELFTEST_FAIL, 0);                            /* $C605 LDM #0 */
    if (!(fw_rd8(f, BOOT_DIP) & DIP_SELFTEST))                   /* BBC1 boot_dip */
        return;
    if (!fw_sfr_bit(f, SFR_P2, P2_SELFTEST_PIN))                 /* BBC1 P2 */
        return;
    fw_wr8(f, BOOT_SELFTEST_FAIL, 1);
}

void boot_selftest_sound(fw *f, fw_regs *r)
{
    fw_wr16(f, CMD_TEXT_PTR, BOOT_SELFTEST_OK_CMDS);             /* $A79F LDM, LDM */
    boot_selftest(f, r);
    if (fw_rd8(f, BOOT_SELFTEST_FAIL) & 1)                       /* $A7A8 BBC0 boot_selftest_fail */
        fw_wr16(f, CMD_TEXT_PTR, BOOT_SELFTEST_FAIL_CMDS);
    boot_queue_string(f, r);
    boot_loop_text(f, r);
    if (f->fault || f->stopped)
        return;                                                  /* abandoned (without a trap: a unit test) */
    pros_make_segments(f, r);
    boot_loop_play(f, r);
}
```

**`src/firmware/ser.h`**: replace

```c
/* $C581: in serial mode (boot_dip bit 0 clear) with the host line P0.2 high: the busy line P0.5 cleared, one reply
   byte sent (ser_tx_get, ser_tx_send) and ser_status_due = ser_tx_pending; when that is 0, after the UART is ready
   again, P0.5 set if P0.2 is still high. A and X kept; Y as ser_tx_get leaves it when it ran. */
void ser_tx_service(fw *f, fw_regs *r);
/* $C5A4: a record reply: ser_reply_id, then ser_reply_count and ser_reply_size in hex, then ser_reply_count records
   of ser_reply_size bytes from ser_reply_ptr, then CR; ser_reply_count is counted down to 0. A, X and Y kept. */
```

with

```c
/* $C581: in serial mode (boot_dip bit 0 clear) with the host line P0.2 high: the busy line P0.5 cleared, one reply
   byte sent (ser_tx_get, ser_tx_send) and ser_status_due = ser_tx_pending; when that is 0, after the UART is ready
   again, P0.5 set if P0.2 is still high. A and X kept; Y as ser_tx_get leaves it when it ran. True when it sent a
   byte; false when it returned at once and changed nothing (parallel mode, or P0.2 low: boot_get_clause then waits
   for an interrupt, M4.6c2 spec 2.5). */
bool ser_tx_service(fw *f, fw_regs *r);
/* $C5A4: a record reply: ser_reply_id, then ser_reply_count and ser_reply_size in hex, then ser_reply_count records
   of ser_reply_size bytes from ser_reply_ptr, then CR; ser_reply_count is counted down to 0. A, X and Y kept. */
```

**`src/firmware/ser.c`**: replace

```c
}

void ser_tx_service(fw *f, fw_regs *r)
{
    if (fw_rd8(f, BOOT_DIP) & 1)                                 /* $C582 BBS0 boot_dip: parallel mode */
        return;
    if (!fw_sfr_bit(f, SFR_P0, P0_HOST_LINE))                    /* $C585 BBC2 P0: the host line low */
        return;
    fw_sfr_clb(f, SFR_P0, P0_BUSY);                              /* CLB5 P0 */
    fw_regs t = *r;
```

with

```c
}

bool ser_tx_service(fw *f, fw_regs *r)
{
    if (fw_rd8(f, BOOT_DIP) & 1)                                 /* $C582 BBS0 boot_dip: parallel mode */
        return false;
    if (!fw_sfr_bit(f, SFR_P0, P0_HOST_LINE))                    /* $C585 BBC2 P0: the host line low */
        return false;
    fw_sfr_clb(f, SFR_P0, P0_BUSY);                              /* CLB5 P0 */
    fw_regs t = *r;
```

**`src/firmware/ser.c`**: replace

```c
    }
    r->y = t.y;                                                  /* $C5A2 PLA: A as it came */
}

```

with

```c
    }
    r->y = t.y;                                                  /* $C5A2 PLA: A as it came */
    return true;
}

```

**`src/firmware/fw.h`**: replace

```c
#define FW_TMP_78    0x0078   /* zero-page scratch, unnamed in the annotations: a saved Y, byte counters;
                                 par_scale_duration's saved Y ($C2A0; not written for a duration of 0);
                                 bank_load_ramcode's saved Y, which bank_restore reloads ($A601, $0258) */
#define FW_TMP_75    0x0075   /* zero-page scratch, unnamed in the annotations: tn_convert_clause's saved X;
                                 bank_load_ramcode's saved P2, which bank_restore writes back ($A607, $0252);
```

with

```c
#define FW_TMP_78    0x0078   /* zero-page scratch, unnamed in the annotations: a saved Y, byte counters;
                                 par_scale_duration's saved Y ($C2A0; not written for a duration of 0);
                                 bank_load_ramcode's saved Y, which bank_restore reloads ($A601, $0258);
                                 boot_queue_string's Y around each ser_rx_put ($C620, $C625) */
#define FW_TMP_75    0x0075   /* zero-page scratch, unnamed in the annotations: tn_convert_clause's saved X;
                                 bank_load_ramcode's saved P2, which bank_restore writes back ($A607, $0252);
```

**`src/engine/thunks.c`**: replace

```c
#include "firmware/bank.h"
#include "firmware/boot.h"
#include "firmware/data/fw_data.h"

```

with

```c
#include "firmware/bank.h"
#include "firmware/boot.h"
#include "firmware/boot_input.h"
#include "firmware/data/fw_data.h"

```

**`src/engine/thunks.c`**: replace

```c
   (LDA ser_rx_write) and reads X, the clause length, at $A706 / $A70D (STA $1B00,X); Y is loaded first ($A6EB LDY
   #0). The C par_play calls the C. No caller reads a flag. Declared: A, X, Y. */
static void ser_tx_service_regs(fw *f, cpu740 *c) { ser_regs(f, c, ser_tx_service); }

/* ser_tx_records $C5A4: A, X and Y in and out (pushed at $C5A4-$C5A8, pulled at $C5D0-$C5D4). Its one caller,
```

with

```c
   (LDA ser_rx_write) and reads X, the clause length, at $A706 / $A70D (STA $1B00,X); Y is loaded first ($A6EB LDY
   #0). The C par_play calls the C. No caller reads a flag. Declared: A, X, Y. */
static void ser_tx_service_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    ser_tx_service(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* ser_tx_records $C5A4: A, X and Y in and out (pushed at $C5A4-$C5A8, pulled at $C5D0-$C5D4). Its one caller,
```

**`src/engine/thunks.c`**: replace

```c
   ser_tx_service after $A6D0 (its thunk); boot_reset ($A672) calls cmd_apply_speaker, which takes X and Y (its thunk);
   boot_queue_string ($C62B) pulls Y and A next ($C62E-$C630). No caller reads a flag (CLI, BBC0, JSR or PLA follow).
   The C callers (tn_convert_clause, cmd_scan_clause, par_play, the prosody passes) call the C, so in "all" mode the
   hook fires from boot_reset, boot_get_clause and boot_queue_string. Declared: A, X, Y. */
static void isr_par_ready_regs(fw *f, cpu740 *c) { ser_regs(f, c, isr_par_ready); }

/* isr_par_hold $C5DE: A, X and Y in and out as they came (PHA at $C5DE, PLA at $C5EA; X and Y untouched). boot_reset
   ($A66F) calls isr_par_ready (which keeps them) and cmd_apply_speaker, which takes X and Y (its thunk);
   boot_get_clause ($A6E4) reads X ($A706 / $A70D STA $1B00,X) after loading Y and A ($A6EB LDY #0, $A6ED LDA);
   boot_loop_play ($B2B3) calls par_play_start, which reads none (its thunk); boot_queue_string ($C617) reads the Y it
   set before ($C615 LDY #0; $C61C LDA ($46),Y). No caller reads a flag. The C boot_loop_play calls the C, so in
   "all" mode the hook fires from boot_reset, boot_get_clause and boot_queue_string. Declared: A, X, Y. */
static void isr_par_hold_regs(fw *f, cpu740 *c) { ser_regs(f, c, isr_par_hold); }

```

with

```c
   ser_tx_service after $A6D0 (its thunk); boot_reset ($A672) calls cmd_apply_speaker, which takes X and Y (its thunk);
   boot_queue_string ($C62B) pulls Y and A next ($C62E-$C630). No caller reads a flag (CLI, BBC0, JSR or PLA follow).
   The C callers (tn_convert_clause, cmd_scan_clause, par_play, the prosody passes, boot_get_clause and
   boot_queue_string) call the C, so in "all" mode the hook fires from boot_reset only. Declared: A, X, Y. */
static void isr_par_ready_regs(fw *f, cpu740 *c) { ser_regs(f, c, isr_par_ready); }

/* isr_par_hold $C5DE: A, X and Y in and out as they came (PHA at $C5DE, PLA at $C5EA; X and Y untouched). boot_reset
   ($A66F) calls isr_par_ready (which keeps them) and cmd_apply_speaker, which takes X and Y (its thunk);
   boot_get_clause ($A6E4) reads X ($A706 / $A70D STA $1B00,X) after loading Y and A ($A6EB LDY #0, $A6ED LDA);
   boot_loop_play ($B2B3) calls par_play_start, which reads none (its thunk); boot_queue_string ($C617) reads the Y it
   set before ($C615 LDY #0; $C61C LDA ($46),Y). No caller reads a flag. The C boot_loop_play, boot_get_clause and
   boot_queue_string call the C, so in "all" mode the hook fires from boot_reset only. Declared: A, X, Y. */
static void isr_par_hold_regs(fw *f, cpu740 *c) { ser_regs(f, c, isr_par_hold); }

```

**`src/engine/thunks.c`**: replace

```c
static void cmd_say_phonetic_regs(fw *f, cpu740 *c) { boot_regs(f, c, cmd_say_phonetic); }

/* The hook of each routine: its register mapping run in the shared thunk frame, user = the bound fw_bridge. */
#define THUNK(name) \
```

with

```c
static void cmd_say_phonetic_regs(fw *f, cpu740 *c) { boot_regs(f, c, cmd_say_phonetic); }

/* boot_selftest $C605: A, X and Y untouched (LDM, BBC). Its one caller, boot_selftest_sound ($A7A5), tests
   boot_selftest_fail, writes cmd_text_ptr (LDM) and calls boot_queue_string, which keeps A, X and Y (its row).
   Declared: A, X, Y. */
static void boot_selftest_regs(fw *f, cpu740 *c) { boot_regs(f, c, boot_selftest); }

/* boot_queue_string $C612: A, X and Y in and out as they came (A and Y pushed at $C612-$C614 and pulled at
   $C62E-$C630; X kept by isr_par_hold, ser_rx_put and isr_par_ready). Its one caller, boot_selftest_sound ($A7B1),
   calls boot_loop_text next, whose boot_get_clause writes X and A first ($A697 LDX #1, $A699 LDA #$20) and Y before
   reading it ($A6EB LDY #0). Declared: A, X, Y. */
static void boot_queue_string_regs(fw *f, cpu740 *c) { boot_regs(f, c, boot_queue_string); }

/* isr_idle $C5FE: A, X and Y in and out as they came (PHA ... PLA; X and Y untouched). Its one caller,
   boot_get_clause ($A6DF), loops back to $A6C2 ($A6E2 BRA), which loads A first (LDA ser_rx_write); it reads X, the
   clause index, at $A70D (STA $1B00,X) and writes Y first ($A6EB LDY #0). It waits for input (FW_WAIT_INPUT), so
   diff_main checks it, not diff_lts. Declared: A, X, Y. */
static void isr_idle_regs(fw *f, cpu740 *c) { boot_regs(f, c, isr_idle); }

/* boot_get_clause $A697: no register input ($A697 LDX #1, $A699 LDA #$20; Y written at $A6EB, or first by
   ser_tx_service's ser_tx_get). A = ' ', X = boot_clause_len and Y = 0, or the page of a page end ($A6F5
   ser_rx_next_page), out. Its one caller, boot_loop_text ($A68D), calls cmd_scan_clause next, which writes X, Y and A
   first ($ACFA LDX #0, $ACFC LDY #0, $AD04 LDA #$FF). It waits for input, so diff_main checks it, not diff_lts.
   Declared: A, X, Y. */
static void boot_get_clause_regs(fw *f, cpu740 *c) { boot_regs(f, c, boot_get_clause); }

/* boot_loop_text $A68D: no register input (boot_get_clause writes them first). No register output: the ROM leaves
   tn_convert_clause's, which the C tn_convert_clause does not set (its row); its callers, boot_reset ($A681) and
   boot_selftest_sound ($A7B4), call pros_make_segments next, whose only register read is Y, by isr_par_ready, which
   keeps it (its row). It waits for input and, at a mid-clause flush, on the timer interrupt, so diff_main checks it,
   not diff_lts. */
static void boot_loop_text_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    boot_loop_text(f, &r);
}

/* boot_selftest_sound $A79F: A, X and Y flow through boot_queue_string (kept) to boot_loop_text, which writes them
   first. A, X and Y out as boot_loop_play leaves them. Its one caller, boot_reset ($A654), tests boot_dip ($A657
   BBS0) and then loads X ($A65A LDX #0) for boot_init_baud, which reads no A or Y, or goes on at boot_reentry_warm
   ($A662 JMP: LDM, CLI, isr_par_hold and isr_par_ready, which keep them, then cmd_apply_speaker, which loads A first
   and does not read X or Y). It waits on the timer interrupt (boot_loop_play), so diff_main checks it, not diff_lts.
   Declared: A, X, Y. */
static void boot_selftest_sound_regs(fw *f, cpu740 *c) { boot_regs(f, c, boot_selftest_sound); }

/* The hook of each routine: its register mapping run in the shared thunk frame, user = the bound fw_bridge. */
#define THUNK(name) \
```

**`src/engine/thunks.c`**: replace

```c
THUNK(bank_find_current)
THUNK(bank_load_ramcode)
THUNK(boot_set_defaults)
THUNK(cmd_apply_speaker)
THUNK(boot_loop_play)
THUNK(boot_load_prosody_defaults)
```

with

```c
THUNK(bank_find_current)
THUNK(bank_load_ramcode)
THUNK(boot_loop_text)
THUNK(boot_get_clause)
THUNK(boot_set_defaults)
THUNK(cmd_apply_speaker)
THUNK(boot_selftest_sound)
THUNK(boot_loop_play)
THUNK(boot_load_prosody_defaults)
```

**`src/engine/thunks.c`**: replace

```c
THUNK(ser_rx_next_page)
THUNK(ser_tx_next_page)
THUNK(util_shl4)
THUNK(util_shl3)
```

with

```c
THUNK(ser_rx_next_page)
THUNK(ser_tx_next_page)
THUNK(isr_idle)
THUNK(boot_selftest)
THUNK(boot_queue_string)
THUNK(util_shl4)
THUNK(util_shl3)
```

**`src/engine/thunks.c`**: replace

```c
    { "bank_find_current",  0x02F0, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_bank_find_current, false, false, true },
    { "bank_load_ramcode",  0xA600, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_bank_load_ramcode },
    { "boot_set_defaults",  0xA742, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_set_defaults },
    { "cmd_apply_speaker",  0xA78C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_apply_speaker },
    { "boot_loop_play",     0xA7D4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_loop_play, true },
    { "boot_load_prosody_defaults", 0xA7F0, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_load_prosody_defaults },
```

with

```c
    { "bank_find_current",  0x02F0, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_bank_find_current, false, false, true },
    { "bank_load_ramcode",  0xA600, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_bank_load_ramcode },
    { "boot_loop_text",     0xA68D, 0,                   t_boot_loop_text, true },
    { "boot_get_clause",    0xA697, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_get_clause, true },
    { "boot_set_defaults",  0xA742, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_set_defaults },
    { "cmd_apply_speaker",  0xA78C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_apply_speaker },
    { "boot_selftest_sound", 0xA79F, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_selftest_sound, true },
    { "boot_loop_play",     0xA7D4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_loop_play, true },
    { "boot_load_prosody_defaults", 0xA7F0, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_load_prosody_defaults },
```

**`src/engine/thunks.c`**: replace

```c
    { "ser_rx_next_page",   0xC5EC, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_rx_next_page },
    { "ser_tx_next_page",   0xC5F5, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_tx_next_page },
    { "util_shl4",          0xC656, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_util_shl4 },
    { "util_shl3",          0xC657, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_util_shl3 },
```

with

```c
    { "ser_rx_next_page",   0xC5EC, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_rx_next_page },
    { "ser_tx_next_page",   0xC5F5, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_tx_next_page },
    { "isr_idle",           0xC5FE, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_idle, true },
    { "boot_selftest",      0xC605, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_selftest },
    { "boot_queue_string",  0xC612, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_queue_string },
    { "util_shl4",          0xC656, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_util_shl4 },
    { "util_shl3",          0xC657, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_util_shl3 },
```

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test fw"`, then `build/apollo2_tests.exe machine_hooks`, `build/apollo2_tests.exe diff_main` and `build/apollo2_tests.exe diff_lts`.
Expected:

- the build passes with no warnings; `fw`: `11 suite(s), 0 failure(s), 0 skip(s)`; `suite_machine_hooks ok`;
- `diff_main`:

```
  corpus: 47 texts, 94 idles, 25167 frame events compared
  warm           idle, 2 idles, 101 frame events, $FFD4 reached 1
  cold           idle, 2 idles, 101 frame events, $FFD0 reached 1
  dip_bank       idle, 2 idles, 41 frame events, $0210 reached 1
  selftest_fail  idle, 2 idles, 365 frame events, $A7AB reached 1
  status_sent    idle, 2 idles, 41 frame events, $A6D7 reached 1
  status_waits   skipped: until boot_reset is C, the C main loop's waits run under no phase deadline
  parallel       skipped: until boot_reset is C, the C main loop's waits run under no phase deadline
  rf1_space      idle, 2 idles, 1565 frame events, $A731 reached 3
  rf1_full       idle, 2 idles, 1675 frame events, $A731 reached 30
  rf1_carry      idle, 2 idles, 1370 frame events, $A739 reached 1
  rf3_empty      idle, 2 idles, 41 frame events, $A704 reached 2
  rf4_commands   idle, 2 idles, 52 frame events, $A9C9 reached 1
  rf5_during     idle, 2 idles, 161 frame events, $A6E4 reached 15
  58 runs, 116 idles, 30680 frame events compared in all
suite_diff_main              ok
1 suite(s), 0 failure(s), 0 skip(s)
```

  (10 s in the proof run): the corpus and every run as in Task 3 with the input side in C but `status_waits` and `parallel`, skipped until Task 6, and the Review Focus runs: `rf1_space` cut at X ≥ $DC on a space (3 times), `rf1_full` at X ≥ $FA, `rf1_carry` carrying `@` into the next clause, `rf3_empty`, `rf4_commands` and `rf5_during`, each idling twice and equal at every event;
- `diff_lts`: 115 lines: the 113 of Task 4 and `boot_selftest 52 calls checked`, `boot_queue_string 52 calls checked`; `1 mid-clause flush calls skipped (golden_hybrid covers them)`, `24 unwound calls compared`, 0 mismatches (176 s in the proof run).

- [ ] **Step 5: Run the full suite**

Run: `cmd //c ".\build.bat test"` (in the background)
Expected:

- `40 suite(s), 0 failure(s), 0 skip(s)`, no warnings (887 s in the proof run);
- `firmware_data`: `49092 converted bytes compared with the ROM, 0 differ`;
- `golden_hybrid`: `48 corpus files x 126 modes (all, then each routine alone)`, `suite_golden_hybrid ok` (the engines' loop, the Review Focus comparisons and every golden), every golden unchanged (`git status --short tests/golden` prints nothing);
- `diff_main`, `diff_lts` as in Step 4; `diff_play` as in Task 1 Step 5; `diff_serial` as in Task 3, 0 mismatches;
- `suite_golden`: `timer scale 1/4: 105 frames, 11789152 CPU cycles`, `timer scale 3/1: 105 frames, 1286784 CPU cycles` (rom, unchanged), `hybrid timer scale 1/4: 105 frames, 11680423 CPU cycles`, `hybrid timer scale 3/1: 105 frames, 975329 CPU cycles` (fewer: the self-test sound's text side takes no emulated time);
- `suite_synth`: `corpus peak plain 20522 (-4.1 dBFS)`, `corpus peak b11 16038 (-6.2 dBFS)`.

- [ ] **Step 6: Commit**

```bash
git add src/firmware/boot_input.h src/firmware/boot_input.c src/firmware/boot.h src/firmware/boot.c src/firmware/ser.h src/firmware/ser.c src/firmware/fw.h src/engine/thunks.c tests/test_fw_boot.c tests/test_fw_ser.c tests/test_machine_hooks.c tests/test_diff_main.c tests/test_golden_hybrid.c tests/test_diff_lts.c
git commit -m "The main loop's input side in C: isr_idle (the input wait), boot_get_clause (its waits where the ROM would loop with nothing changed), boot_loop_text, boot_queue_string, boot_selftest and boot_selftest_sound, with their hooks; the Review Focus runs in diff_main and golden_hybrid (M4.6c2 2.1-2.6, 4.1, 4.2)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 6: The entries in C: `boot_reset` $A61C, `boot_reentry_cold` $A665, `boot_reentry_warm` $A66B and the main loop, `bank_entry_cold` $FFD0, `bank_entry_warm` $FFD4, and `isr_unused` $C3DF, with their hooks; the Review Focus test 2; the branch probe; the CLI check (spec §2.1–§2.3, §3.2, §3.4, §4.1, §4.4–§4.7, §4.8 item 6)

**ROM facts checked / what was verified while proving the plan** (`rom_A61C_boot_reset.txt` with `boot_reentry_cold` $A665 and `boot_reentry_warm` $A66B, `rom_FFD0_bank_entry_cold.txt`, `rom_FFD4_bank_entry_warm.txt`, `rom_C3DF_isr_unused.txt`, `ramcode_0210_bank_enter.txt`, `rom_ACE8_isr_set_volume.txt`, `data.txt`, `ghidra740/annotations/bank.txt`; `machine.c` (`exec`), `m50734.c` (`m50734_irq_lines`); the vectors and the JSR/JMP operands in the ROM image; the throwaway branch probe and coverage tool of Step 5):
- **`boot_reset`** ($A61C–$A662): `LDM` P3_DIR $F9, P23_FUNC 0, P3 $78 (the same three as `boot_init_ports`' last); the CPU set-up ($A625–$A62C, `reset_cpu`); A = Y = X = 0 (`LDA #0 / TAY / TAX`); ICR1, ICR2, ICR3 = 0 (`STA`); `STA 0,X` from X = $C9 down to 1 (`DEX / BNE`: $01–$C9, not $00); `cmd_volume` = 0 and `isr_set_volume` with A = 0 (one read and one write of P3); `boot_read_dip`; `boot_init_ports`; ICR1 = 1 (`LDM`); `boot_init_settings`; `cmd_apply_speaker`; `boot_selftest_sound`; then `BBS0 boot_dip,$A662`: in serial mode `LDX #0`, `boot_init_baud` and `boot_init_host_lines`; `JMP boot_reentry_warm`.
- **The re-entries**: `boot_reentry_cold` ($A665) is `JSR boot_read_dip / JSR boot_init_settings` and falls into $A66B; `boot_reentry_warm`: `LDM #0,ser_abort`, `CLI` ($A66E), `isr_par_hold`, `isr_par_ready`, `cmd_apply_speaker`, `bank_load_ramcode`, `bank_scan`, `bank_find_current`, then $A681: `boot_loop_text`, `pros_make_segments`, `boot_loop_play`, `JMP $A681`. The bank header: `bank_entry_cold` $FFD0 `SEI` (the byte $78 `bank_probe` reads) / `JMP $A665`; `bank_entry_warm` $FFD4 `JMP $A66B`; `bank_entry_reset` $FFD8 `JMP $A61C` is not reached by any code (no operand names it). The only entries into them are `vec_reset` ($FFFE), `bank_enter`'s two `JMP`s ($0232, $0240 in RAM) and `boot_reset`'s `JMP $A66B`.
- **The entries never return**: the main loop's `JMP $A681` has no exit, so the five rows declare no output and their C returns only when abandoned without a trap (a unit test). Under the thunk the C leaves only through a stopped wait, a bank entry (`fw_enter`, a longjmp) or a fault; an outermost entry runs under the engine's phase deadline (Task 1).
- **An entry that enters itself again**: under `bank_entry_warm` the C main loop's `@=1` enters $FFD4 again (`fw_enter` leaves the PC there); a hook that returns with the PC at its own address is `exec`'s spin-guard fault (MFAULT_FIRMWARE): the M4.6c1 Review Focus input "@=1 One. @=5 Two. @=1 Three." faulted at $FFD4 that way. So `run_frame` runs an entry's C afresh while its C unwinds to the entry's own address (one more unwind and thunk call each), which is what the hook would do there; `diff_main`'s `warm_again` and `test_entry_thunk` pin it.
- **`isr_unused`** ($C3DF, `RTI`): the handler of `vec_int3_step` $FFF6 and `vec_int0` $FFFC. The machine raises those lines only with ICR1 bits 6/4 or 2/3 set (`m50734_irq_lines`), and boot writes ICR1 = 0, then 1 (the `_INT1` enable): unreachable on the board as modelled. An isr row with no outputs; `diff_lts` prints it with 0 calls (`diff_is_unreached`).
- **What the hybrid executes of the ROM**: a throwaway coverage run of the hybrid engine over the 47 corpus texts (`machine_config.coverage`) recorded exactly two ROM instructions executed: $B372 and $B37F, `isr_timer_start`'s loops (`FW_WAIT_TIMER2`, `FW_WAIT_TIMER3`); nothing in the lower window, the RAM copy or RAM (spec §4.6: `$C500` runs only when a reply is sent, the NUL fallback only on a NUL after a command).
- **The C callers**: no C calls the ROM for any of the 21 routines; with `boot_reset` C, the address headers' callers in parentheses are all C, and in "all" mode the hooks of `isr_set_volume`, `isr_par_ready` and `isr_par_hold` never fire (their comments said "from boot_reset").
- **The Task 5 skips** go: the C main loop now runs under the phase deadline, and `diff_main`'s `status_waits` and `parallel` runs and `test_engine_loop`'s hybrid in parallel mode end at it with `MACHINE_CYCLE_LIMIT`, compared to the end with the ROM engine's.

**Files:**
- Modify: `src/firmware/boot.h`, `src/firmware/boot.c`, `src/engine/thunks.c`, `src/firmware/isr_addr.h`, `src/firmware/ser_addr.h`, `src/firmware/bank_addr.h`, `src/firmware/par_addr.h`, `src/firmware/pros_addr.h`, `tests/test_fw_boot.c`, `tests/test_machine_hooks.c`, `tests/test_diff_main.c`, `tests/test_golden_hybrid.c`, `tests/test_diff_lts.c`

**Interfaces:**
- Consumes: Tasks 1–5: the phase deadline and `FRAME_ENTRY`, `fw_reset_cpu`, `fw_set_interrupts`, the boot helpers, the input side and the self-test; `isr_set_volume`, `cmd_apply_speaker`, `bank_load_ramcode`, `bank_scan`, `bank_find_current`, `pros_make_segments`, `boot_loop_play`.
- Produces:

```c
/* src/firmware/boot.h */
void boot_reset(fw *f);
void boot_reentry_cold(fw *f, fw_regs *r), boot_reentry_warm(fw *f, fw_regs *r);
void bank_entry_cold(fw *f, fw_regs *r), bank_entry_warm(fw *f, fw_regs *r), isr_unused(fw *f, fw_regs *r);
/* src/engine/thunks.c: ENTRY_THUNK, entry_regs(), the six rows (131 replacements); run_frame runs an entry's C afresh */
```

No name collides: `grep -rnw "boot_reset\|boot_reentry_cold\|boot_reentry_warm\|bank_entry_cold\|bank_entry_warm\|isr_unused\|ENTRY_THUNK" src tests` prints, before this task, only comments and `diff_state.c`'s row names (Task 4).

- [ ] **Step 1: Write the failing tests**

`test_fw_boot.c` gains the entries' tests (the reset's first writes and the whole boot to the main loop's first input wait, in both modes; each re-entry and bank entry to two spoken clauses; `isr_unused`; the rows) and pins the two branches the branch probe found one-sided in `boot_get_clause` (Step 5); the stub reads another bank as an empty socket. `test_machine_hooks.c` runs an entry row's thunk on the real ROM: the phase deadline, a fresh run at its own entry, the stop. `diff_main` gains the Review Focus run 2, `warm_again`, the baud jumpers and the host line high at boot, and loses Task 5's skip, as `test_engine_loop` does; `golden_hybrid` checks the unwind and the mid-clause flush in the modes whose C runs the main loop; `diff_lts` leaves the entry rows to `diff_main` and accepts 0 calls of `isr_unused`.

**`tests/test_fw_boot.c`**: replace

```c
#include "test.h"
#include "engine/thunks.h"
#include "firmware/boot.h"
#include "firmware/boot_addr.h"
```

with

```c
#include "test.h"
#include "engine/thunks.h"
#include "firmware/bank_addr.h"
#include "firmware/boot.h"
#include "firmware/boot_addr.h"
```

**`tests/test_fw_boot.c`**: replace

```c
}

static void stub_reset_cpu(void *ctx)
{
```

with

```c
}

/* Every other bank an empty socket (bank_scan reads their headers): $FF. Not logged. */
static uint8_t stub_read_bank(void *ctx, uint16_t addr)
{
    (void)ctx;
    (void)addr;
    return 0xFF;
}

static void stub_reset_cpu(void *ctx)
{
```

**`tests/test_fw_boot.c`**: replace

```c
    g_host.reset_cpu = stub_reset_cpu;
    g_host.set_interrupts = stub_set_interrupts;
    g_nlog = 0;
    g_ncalls = 0;
```

with

```c
    g_host.reset_cpu = stub_reset_cpu;
    g_host.set_interrupts = stub_set_interrupts;
    g_host.read_bank = stub_read_bank;
    g_nlog = 0;
    g_ncalls = 0;
```

**`tests/test_fw_boot.c`**: replace

```c
}

/* The rows of the input side and the self-test (M4.6c2 spec 2.3): the waits rows (boot_loop_text declares no output:
   the C tn_convert_clause sets no register) and two helpers. */
```

with

```c
}

/* The count of logged events of kind with value (value 0xFF: any). */
static unsigned count_log(ev_kind kind, uint8_t value)
{
    unsigned n = 0;
    for (unsigned i = 0; i < g_nlog && i < MAX_LOG; i++)
        n += g_log[i].kind == kind && (value == 0xFF || g_log[i].value == value);
    return n;
}

/* boot_reset $A61C (M4.6c2 spec 2.1), serial mode: P3_DIR, P23_FUNC, P3; the CPU set-up; ICR1, ICR2, ICR3 = 0; then
   isr_set_volume's P3 (a read and a write: volume 0) and boot_read_dip's AD_CTRL write first. $C9 down to $01 cleared
   ($C9 is left 0; $CA, above, untouched). The whole boot runs: the settings, the self-test sound (spoken: the DSP
   started once), the baud rate and host lines (UART_BAUD $02, UART_CTRL $D1), boot_reentry_warm's CLI, the banks
   (bank_current 1), then the main loop: boot_get_clause takes the space the self-test string left in the ring and
   waits for input, where the script stops the run. Parallel mode (P4 bit 0): no UART_BAUD or UART_CTRL write. */
static void test_reset(void)
{
    static const char *const none[] = { "" };
    static const event want[] = {
        { EV_WRITE, SFR_P3_DIR, 0xF9 }, { EV_WRITE, SFR_P23_FUNC, 0x00 }, { EV_WRITE, SFR_P3, 0x78 },
        { EV_RESET, FW_RESET_CPU, 0 }, { EV_WRITE, SFR_ICR1, 0x00 }, { EV_WRITE, SFR_ICR2, 0x00 },
        { EV_WRITE, SFR_ICR3, 0x00 }, { EV_READ, SFR_P3, 0x78 }, { EV_WRITE, SFR_P3, 0x78 | 0x78 },
        { EV_WRITE, SFR_AD_CTRL, 0x00 },
    };
    for (int par = 0; par <= 1; par++) {
        setup();
        script(none, 1);
        memset(g_ram, 0xA5, 0x100);
        g_sfr[SFR_P4] = (uint8_t)par;
        boot_reset(&g_f);
        CHECK(g_f.stopped);
        CHECK(!g_f.fault);
        CHECK_EQ(g_ncalls, 0);
        CHECK(g_nlog >= 10);
        for (unsigned i = 0; i < 10 && i < g_nlog; i++) {
            CHECK_EQ(g_log[i].kind, want[i].kind);
            CHECK_EQ(g_log[i].addr, want[i].addr);
            CHECK_EQ(g_log[i].value, want[i].value);
        }
        CHECK_EQ(g_ram[0xC9], 0);
        CHECK_EQ(g_ram[0xCA], 0xA5);
        CHECK_EQ(g_ram[BOOT_DIP], par);
        CHECK_EQ(g_ram[BANK_VER_MAJOR], 2);
        CHECK_EQ(g_ram[BANK_CURRENT], 1);
        CHECK_EQ(g_ram[CMD_CHAR], '@');
        CHECK_EQ(count_log(EV_RESET, 0xFF), 1);
        CHECK_EQ(count_log(EV_WAIT, FW_WAIT_TIMER2), 1);              /* the self-test sound */
        CHECK_EQ(g_sfr[SFR_UART_BAUD], par ? 0x00 : 0x02);
        CHECK_EQ(g_sfr[SFR_UART_CTRL], par ? 0x00 : 0xD1);
        CHECK_EQ(g_ram[BOOT_CLAUSE + 1], ' ');                       /* the self-test string's last byte */
        CHECK_EQ(g_feed_pos, 1);
    }
}

/* The re-entries and the bank entries (M4.6c2 spec 2.1, 3.4). boot_reentry_warm: ser_abort = 0, CLI first, the banks
   (bank_load_ramcode's P2 read), then the main loop, which speaks each clause (the DSP started per clause: two) and
   stops at the input wait once the script is used up. bank_entry_warm is the same. boot_reentry_cold: boot_read_dip
   and boot_init_settings first (AD_CTRL, P4), the settings reset (the ring pointers, cmd_char), then the same.
   bank_entry_cold: SEI first, then boot_reentry_cold. */
static void test_reentries(void)
{
    static const char *const two[] = { "One. ", "Two." };
    for (int which = 0; which < 4; which++) {
        setup();
        fw_regs r = { 0, 0, 0, 0 };
        boot_set_defaults(&g_f, &r);
        g_ram[SER_ABORT] = 1;
        g_ram[CMD_CHAR] = 0x5E;                  /* the cold re-entry sets '@' again; the warm one keeps it */
        script(two, 2);
        g_nlog = 0;
        r = (fw_regs){ 0x02, 0xFF, 0x00, 0 };    /* as bank_enter leaves them at the warm entry */
        switch (which) {
        case 0: boot_reentry_warm(&g_f, &r); break;
        case 1: bank_entry_warm(&g_f, &r); break;
        case 2: boot_reentry_cold(&g_f, &r); break;
        default: bank_entry_cold(&g_f, &r); break;
        }
        CHECK(g_f.stopped);
        CHECK(!g_f.fault);
        CHECK_EQ(g_ncalls, 0);
        CHECK(g_nlog > 3);
        if (g_nlog <= 3)
            continue;
        bool cold = which >= 2;
        unsigned i = 0;
        if (which == 3) {
            CHECK_EQ(g_log[0].kind, EV_INT);                         /* $FFD0 SEI */
            CHECK_EQ(g_log[0].value, 0);
            i = 1;
        }
        if (cold) {
            CHECK_EQ(g_log[i].kind, EV_WRITE);                       /* boot_read_dip */
            CHECK_EQ(g_log[i].addr, SFR_AD_CTRL);
            i += 2;
        }
        CHECK_EQ(g_log[i].kind, EV_INT);                             /* $A66E CLI */
        CHECK_EQ(g_log[i].value, 1);
        CHECK_EQ(g_log[i + 1].kind, EV_READ);                        /* bank_load_ramcode's P2 */
        CHECK_EQ(g_log[i + 1].addr, SFR_P2);
        CHECK_EQ(g_ram[SER_ABORT], 0);
        CHECK_EQ(g_ram[CMD_CHAR], cold ? '@' : 0x5E);
        CHECK_EQ(g_ram[BANK_CURRENT], 1);
        CHECK_EQ(count_log(EV_WAIT, FW_WAIT_TIMER2), 2);             /* "One." and "Two." spoken */
        CHECK_EQ(g_feed_pos, 2);
    }
}

/* boot_get_clause with the read and write pointers' low bytes equal and their pages apart (256 bytes in the ring): not
   empty, at its entry ($A6BE BNE taken: ser_rx_idle left clear) and in its loop ($A6CC BNE taken: no wait). The
   corpus never buffers exactly 256 bytes: the branch probe found both branches one-sided, pinned here. */
static void test_get_clause_page_apart(void)
{
    setup();
    g_ram[SER_RX_WRITE + 1] = 0x22;               /* $2100 to $2200 */
    memset(g_ram + 0x2100, 'a', 256);
    g_ram[0x2100 + 3] = '.';
    fw_regs r = { 0, 0, 0, 0 };
    boot_get_clause(&g_f, &r);
    check_clause("aaa.");
    CHECK_EQ(g_ram[SER_RX_IDLE], 0);
    CHECK_EQ(count_log(EV_WAIT, 0xFF), 0);
    CHECK(!g_f.fault);
}

/* isr_unused $C3DF: RTI, nothing done: no host call, A, X and Y kept. */
static void test_isr_unused(void)
{
    setup();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_unused(&g_f, &r);
    check_log(NULL, 0);
    check_regs(&r, 0x11, 0x22, 0x33);
    CHECK(!g_f.fault);
}

/* The rows of the entries (no output: they never return) and isr_unused (an interrupt handler, no output). */
static void test_entry_rows(void)
{
    check_row("boot_reset", ROM_BOOT_RESET, 0, false, false, true);
    check_row("boot_reentry_cold", ROM_BOOT_REENTRY_COLD, 0, false, false, true);
    check_row("boot_reentry_warm", ROM_BOOT_REENTRY_WARM, 0, false, false, true);
    check_row("bank_entry_cold", BANK_ENTRY_COLD, 0, false, false, true);
    check_row("bank_entry_warm", BANK_ENTRY_WARM, 0, false, false, true);
    check_row("isr_unused", ROM_ISR_UNUSED, 0, false, true, false);
}

/* The rows of the input side and the self-test (M4.6c2 spec 2.3): the waits rows (boot_loop_text declares no output:
   the C tn_convert_clause sets no register) and two helpers. */
```

**`tests/test_fw_boot.c`**: replace

```c
    test_selftest_sound();
    test_input_rows();
}
```

with

```c
    test_selftest_sound();
    test_input_rows();
    test_reset();
    test_reentries();
    test_isr_unused();
    test_entry_rows();
    test_get_clause_page_apart();
}
```

**`tests/test_machine_hooks.c`**: replace

```c
#include "rom_fixture.h"
#include "machine/machine.h"
#include "engine/thunks.h"
#include "firmware/bank_addr.h"
#include "firmware/boot_addr.h"
#include "firmware/isr_addr.h"
#include "firmware/par_addr.h"
#include "firmware/tn_convert.h"

```

with

```c
#include "rom_fixture.h"
#include "machine/machine.h"
#include "engine/rom_engine.h"
#include "engine/thunks.h"
#include "firmware/bank_addr.h"
#include "firmware/boot.h"
#include "firmware/boot_addr.h"
#include "firmware/isr_addr.h"
#include "firmware/par_addr.h"
#include "firmware/ser.h"
#include "firmware/tn_convert.h"

```

**`tests/test_machine_hooks.c`**: replace

```c
}

/* The bridge's DSP port and frame event (M4.6b spec 3.1): fw_dsp_write is a bus write of $8000 (counted, grouped
   into the raw frame), fw_frame_done the machine's frame capture. */
```

with

```c
}

static bool g_phase_seen;

/* An idle handler that records whether the bridge (user) ran under the phase deadline, then stops the run. */
static machine_idle_action idle_phase_stop(machine *m, void *user)
{
    (void)m;
    g_phase_seen = ((const fw_bridge *)user)->phase;
    g_idles++;
    return MACHINE_STOP;
}

/* An entry row's thunk (M4.6c2 spec 2.3, 3.2, 3.4) on the real ROM, hooked alone at bank_entry_warm $FFD4 and entered
   there as bank_enter leaves it (SP $FF, I set, A = 2, X = $FF), with the power-up settings and the bank version 2.01
   in RAM, and "@=1 x." in the input ring. Its C runs boot_reentry_warm's set-up and the main loop, under the engine's
   phase deadline (b->phase, seen by the idle handler), not a bridge deadline. The C dispatches @=1: the main bank,
   its version the running one: bank_enter enters $FFD4 again, so the thunk runs its C afresh (one unwind, two runs);
   the ring then empty, the input wait's idle handler stops the run: the machine at the idle point, b->phase and the
   trap clear, one stop. */
static void test_entry_thunk(void)
{
    static fw_bridge b;
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    CHECK(machine_init(&g_m, rom, len, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    CHECK(fw_install(&g_m, &b, "bank_entry_warm"));
    fw_regs r = { 0, 0, 0, 0 };
    boot_init_settings(&b.f, &r);
    for (const char *s = "@=1 x."; *s; s++) {
        fw_regs t = { (uint8_t)*s, 0, 0, 0 };
        ser_rx_put(&b.f, &t);
    }
    machine_set_idle_handler(&g_m, idle_phase_stop, &b);
    g_m.phase_deadline = g_m.cpu.cycles + ROM_ENGINE_SPEAK_CYCLES;
    g_idles = 0;
    g_phase_seen = false;
    g_m.cpu.pc = BANK_ENTRY_WARM;
    g_m.cpu.s = 0xFF;
    g_m.cpu.p = CPU740_I;
    g_m.cpu.a = 0x02;
    g_m.cpu.x = 0xFF;
    CHECK(machine_step(&g_m));
    CHECK_EQ(g_m.fault, MFAULT_NONE);
    CHECK_EQ(g_idles, 1);
    CHECK(g_phase_seen);
    CHECK_EQ(b.unwinds, 1);
    CHECK_EQ(b.thunk_calls, 2);
    CHECK_EQ(b.stops, 1);
    CHECK(!b.phase);
    CHECK_EQ(b.deadline, 0);
    CHECK(b.f.trap == NULL);
    CHECK_EQ(g_m.cpu.pc, 0xC603);
    CHECK(g_m.cpu.waiting);
    CHECK_EQ(g_m.ram[BANK_CURRENT], 1);
}

/* The bridge's DSP port and frame event (M4.6b spec 3.1): fw_dsp_write is a bus write of $8000 (counted, grouped
   into the raw frame), fw_frame_done the machine's frame capture. */
```

**`tests/test_machine_hooks.c`**: replace

```c
    test_host_input();
    test_isr_idle_thunk();
}
```

with

```c
    test_host_input();
    test_isr_idle_thunk();
    test_entry_thunk();
}
```

**`tests/test_diff_main.c`**: replace

```c
#define P2_SELFTEST      0x02   /* P2.1: the self-test input (boot_selftest $C60B BBC1 P2) */
#define P0_HOST_LINE     0x04   /* P0.2: the host's handshake line */

/* One event of a run. */
```

with

```c
#define P2_SELFTEST      0x02   /* P2.1: the self-test input (boot_selftest $C60B BBC1 P2) */
#define P0_HOST_LINE     0x04   /* P0.2: the host's handshake line */
#define P3_BAUD_300      0x02   /* P3.1: a baud jumper (boot_init_baud $AC81 BBS1 P3) */
#define P3_BAUD_1200     0x04   /* P3.2: the other ($AC84, $AC9A BBS2 P3) */

/* One event of a run. */
```

**`tests/test_diff_main.c`**: replace

```c
    bool target_ramcode;
    machine_status want;
} extra_run;

```

with

```c
    bool target_ramcode;
    machine_status want;
    uint8_t p3;                       /* ORed into the default input pins of P3 (the baud jumpers) */
} extra_run;

```

**`tests/test_diff_main.c`**: replace

```c
        cfg.port_in[2] |= x->p2;
        cfg.port_in[0] |= x->p0;
    }
    engine_input in = { (const uint8_t *)text, strlen(text), true };
    engine_watch w = { on_frame, on_idle, NULL };
    frame_list fl = { 0 };
    g_extra = x;
    if (x && x->want == MACHINE_CYCLE_LIMIT && fw_replacement_find("boot_get_clause") &&
        !fw_replacement_find("boot_reset")) {
        printf("  %-14s skipped: until boot_reset is C, the C main loop's waits run under no phase deadline\n",
               x->name);
        return;
    }

    g_rom_side = true;
```

with

```c
        cfg.port_in[2] |= x->p2;
        cfg.port_in[0] |= x->p0;
        cfg.port_in[3] |= x->p3;
    }
    engine_input in = { (const uint8_t *)text, strlen(text), true };
    engine_watch w = { on_frame, on_idle, NULL };
    frame_list fl = { 0 };
    g_extra = x;

    g_rom_side = true;
```

**`tests/test_diff_main.c`**: replace

```c
    /* @=1: the main bank, its version equal: the warm entry $FFD4, the rest of the clause dropped */
    { "warm", "@=1 One. Two.", 0, 0, 0, NULL, AT_IDLE, 0, BANK_ENTRY_WARM, false, MACHINE_IDLE },
    /* @=1 with the version altered at the end of boot: the cold entry $FFD0 (boot_read_dip, boot_init_settings) */
    { "cold", "@=1 One. Two.", 0, 0, 0, inj_cold, AT_IDLE, 0, BANK_ENTRY_COLD, false, MACHINE_IDLE },
```

with

```c
    /* @=1: the main bank, its version equal: the warm entry $FFD4, the rest of the clause dropped */
    { "warm", "@=1 One. Two.", 0, 0, 0, NULL, AT_IDLE, 0, BANK_ENTRY_WARM, false, MACHINE_IDLE },
    /* @=1 twice: the second from under the C bank_entry_warm, which enters $FFD4 again (its C run afresh) */
    { "warm_again", "@=1 One. @=1 Two. Three.", 0, 0, 0, NULL, AT_IDLE, 0, BANK_ENTRY_WARM, false, MACHINE_IDLE },
    /* @=1 with the version altered at the end of boot: the cold entry $FFD0 (boot_read_dip, boot_init_settings) */
    { "cold", "@=1 One. Two.", 0, 0, 0, inj_cold, AT_IDLE, 0, BANK_ENTRY_COLD, false, MACHINE_IDLE },
```

**`tests/test_diff_main.c`**: replace

```c
    { "rf1_carry", WORDS44 WORDS44 WORDS44 WORDS44 "the quick brown fox jumps over the lazy do@W5 now slower.", 0, 0, 0,
      NULL, AT_IDLE, 0, 0xA739, false, MACHINE_IDLE },
    /* Review Focus 3: an empty input, the CR alone: a one-character clause, then idle again */
    { "rf3_empty", "", 0, 0, 0, NULL, AT_IDLE, 0, 0xA704, false, MACHINE_IDLE },
    /* Review Focus 4: commands only (cmd_set_speed $A9C9 run), nothing spoken */
    { "rf4_commands", "@W5 @A3", 0, 0, 0, NULL, AT_IDLE, 0, 0xA9C9, false, MACHINE_IDLE },
    /* Review Focus 5: "Two." arrives in the ring during the speech of "One.": taken after it with no wait */
    { "rf5_during", "One.", 0, 0, 0, inj_ring_two, AT_SPEECH, 5, 0xA6E4, false, MACHINE_IDLE },
};

```

with

```c
    { "rf1_carry", WORDS44 WORDS44 WORDS44 WORDS44 "the quick brown fox jumps over the lazy do@W5 now slower.", 0, 0, 0,
      NULL, AT_IDLE, 0, 0xA739, false, MACHINE_IDLE },
    /* Review Focus 2: several sentences in one input, each punctuation the end of a clause ($A704), spoken in order
       with no idle between */
    { "rf2_sentences", "One. Two? Three!", 0, 0, 0, NULL, AT_IDLE, 0, 0xA704, false, MACHINE_IDLE },
    /* Review Focus 3: an empty input, the CR alone: a one-character clause, then idle again */
    { "rf3_empty", "", 0, 0, 0, NULL, AT_IDLE, 0, 0xA704, false, MACHINE_IDLE },
    /* Review Focus 4: commands only (cmd_set_speed $A9C9 run), nothing spoken */
    { "rf4_commands", "@W5 @A3", 0, 0, 0, NULL, AT_IDLE, 0, 0xA9C9, false, MACHINE_IDLE },
    /* Review Focus 5: "Two." arrives in the ring during the speech of "One.": taken after it with no wait */
    { "rf5_during", "One.", 0, 0, 0, inj_ring_two, AT_SPEECH, 5, 0xA6E4, false, MACHINE_IDLE },
    /* the baud jumpers (the branch probe): P3.1 ($AC9D: UART_BAUD $BF), P3.2 ($ACB0: $2F), both ($ACC3: $05) */
    { "baud_p31", "Hello.", 0, 0, 0, NULL, AT_IDLE, 0, 0xAC9D, false, MACHINE_IDLE, P3_BAUD_300 },
    { "baud_p32", "Hello.", 0, 0, 0, NULL, AT_IDLE, 0, 0xACB0, false, MACHINE_IDLE, P3_BAUD_1200 },
    { "baud_both", "Hello.", 0, 0, 0, NULL, AT_IDLE, 0, 0xACC3, false, MACHINE_IDLE, P3_BAUD_300 | P3_BAUD_1200 },
    /* the host line high at boot (the branch probe): boot_init_host_lines' CLB5 P0 path ($AC72), isr_host_line_high;
       the status byte that leaves due is sent before the end of boot. The CR alone after it: the busy line P0.5 that
       ser_tx_service sets is then the level the machine delivers bytes at, and the byte's interrupt clears it. */
    { "host_line_high", "", 0, 0, P0_HOST_LINE, NULL, AT_IDLE, 0, 0xAC72, false, MACHINE_IDLE },
};

```

**`tests/test_golden_hybrid.c`**: replace

```c
}

/* The modes in which the C reaches cmd_run_due's dispatch or bank_enter, so @= entering a bank resets the stack
   under C, or the C enters the bank, and the C call chain unwinds (M4.2 spec 5.3, M4.6c1 spec 4.5). */
static bool runs_c_dispatch(const char *only)
{
    return !only || !strcmp(only, "tn_convert_clause") || !strcmp(only, "cmd_run_due") ||
           !strcmp(only, "cmd_run_next") || !strcmp(only, "bank_enter");
}

```

with

```c
}

/* The modes in which the C runs the main loop's conversion: boot_reset, boot_reentry_warm and boot_loop_text call the
   C tn_convert_clause (M4.6c2 spec 2.2). */
static bool runs_c_main_loop(const char *only)
{
    return only && (!strcmp(only, "boot_reset") || !strcmp(only, "boot_reentry_warm") ||
                    !strcmp(only, "boot_loop_text"));
}

/* The modes in which the C reaches cmd_run_due's dispatch or bank_enter, so @= entering a bank resets the stack
   under C, or the C enters the bank, and the C call chain unwinds (M4.2 spec 5.3, M4.6c1 spec 4.5). */
static bool runs_c_dispatch(const char *only)
{
    return !only || !strcmp(only, "tn_convert_clause") || !strcmp(only, "cmd_run_due") ||
           !strcmp(only, "cmd_run_next") || !strcmp(only, "bank_enter") || runs_c_main_loop(only);
}

```

**`tests/test_golden_hybrid.c`**: replace

```c
            g_test_failures++;
        }
        if (!strcmp(k_corpus[c], "long_clause") && (!only || !strcmp(only, "tn_convert_clause")) &&
            st.tn_flushes == 0) {
            fprintf(stderr, "  hybrid (%s) long_clause: the C tn_convert_clause never flushed\n",
```

with

```c
            g_test_failures++;
        }
        if (!strcmp(k_corpus[c], "long_clause") && (!only || !strcmp(only, "tn_convert_clause") ||
                                                     runs_c_main_loop(only)) &&
            st.tn_flushes == 0) {
            fprintf(stderr, "  hybrid (%s) long_clause: the C tn_convert_clause never flushed\n",
```

**`tests/test_golden_hybrid.c`**: replace

```c
        frame_list_free(&fl);

        if (hybrid && fw_replacement_find("boot_get_clause") && !fw_replacement_find("boot_reset"))
            continue;   /* until boot_reset is C, the C main loop's waits run under no phase deadline */
        machine_config cfg;
        machine_default_config(&cfg);
```

with

```c
        frame_list_free(&fl);

        machine_config cfg;
        machine_default_config(&cfg);
```

**`tests/test_diff_lts.c`**: replace

```c
   diff_same_at_entry); it is counted: bank_select must produce some (M4.2 spec 5.5). The replacements
   marked waits (the frame loop, which waits on the timer interrupt) cannot run with interrupts held: they are not
   observed here but in diff_play (M4.4b spec 4.1, 4.2). */
#include <stdio.h>
#include <stdlib.h>
```

with

```c
   diff_same_at_entry); it is counted: bank_select must produce some (M4.2 spec 5.5). The replacements
   marked waits (the frame loop, which waits on the timer interrupt) cannot run with interrupts held: they are not
   observed here but in diff_play (M4.4b spec 4.1, 4.2), or, the main loop's, in diff_main (M4.6c2 spec 4.2); nor are
   the entries, which never return. isr_unused, which nothing enters (diff_is_unreached), prints 0 calls: unit tests
   pin it. */
#include <stdio.h>
#include <stdlib.h>
```

**`tests/test_diff_lts.c`**: replace

```c
    (void)user;
    for (size_t i = 0; i < fw_replacement_count; i++) {
        if (fw_replacements[i].waits)
            continue;   /* diff_play's */
        g_obs[i].r = &fw_replacements[i];
        g_obs[i].calls = 0;   /* the full-call budget is per corpus file */
```

with

```c
    (void)user;
    for (size_t i = 0; i < fw_replacement_count; i++) {
        if (fw_replacements[i].waits || fw_replacements[i].entry)
            continue;   /* diff_play's or diff_main's */
        g_obs[i].r = &fw_replacements[i];
        g_obs[i].calls = 0;   /* the full-call budget is per corpus file */
```

**`tests/test_diff_lts.c`**: replace

```c
        run(rom, len, "extra", k_extra[i], false);
    for (size_t i = 0; i < fw_replacement_count; i++) {
        if (fw_replacements[i].waits)
            continue;
        printf("  %-20s %u calls checked\n", fw_replacements[i].name, g_checked[i]);
        CHECK(g_checked[i] > 0 || diff_is_probed(fw_replacements[i].name));   /* every ported routine is exercised */
    }
    printf("  %u mid-clause flush calls skipped (golden_hybrid covers them)\n", g_flush_skips);
```

with

```c
        run(rom, len, "extra", k_extra[i], false);
    for (size_t i = 0; i < fw_replacement_count; i++) {
        if (fw_replacements[i].waits || fw_replacements[i].entry)
            continue;
        printf("  %-20s %u calls checked\n", fw_replacements[i].name, g_checked[i]);
        CHECK(g_checked[i] > 0 || diff_is_probed(fw_replacements[i].name) ||
              diff_is_unreached(fw_replacements[i].name));   /* every ported routine is exercised */
    }
    printf("  %u mid-clause flush calls skipped (golden_hybrid covers them)\n", g_flush_skips);
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw_boot"`
Expected: the build stops with compile errors (the proof run, first lines):

```
tests\test_fw_boot.c(882): error C2220: the following warning is treated as an error
tests\test_fw_boot.c(882): warning C4013: 'boot_reset' undefined; assuming extern returning int
tests\test_fw_boot.c(925): warning C4013: 'boot_reentry_warm' undefined; assuming extern returning int
tests\test_fw_boot.c(926): warning C4013: 'bank_entry_warm' undefined; assuming extern returning int
```

- [ ] **Step 3: Implement**

**`src/firmware/boot.h`**: replace

```c
void boot_selftest_sound(fw *f, fw_regs *r);

#endif
```

with

```c
void boot_selftest_sound(fw *f, fw_regs *r);

/* The entries (M4.6c2 spec 2.1, 3.4). None returns: the main loop runs for ever, and only a stopped wait, a bank entry
   (fw_enter) or a fault leaves it, each a longjmp to f->trap; without a trap (a unit test) the routine returns then. */
/* $A61C, the reset: P3_DIR $F9, P23_FUNC 0, P3 $78; the CPU set-up (fw_reset_cpu: SEI, SP $FF, CLT, CLD, CLC, CLV);
   A = X = Y = 0; ICR1, ICR2 and ICR3 = 0; $C9 down to $01 cleared; cmd_volume = 0 and isr_set_volume; boot_read_dip;
   boot_init_ports; ICR1 = 1; boot_init_settings; cmd_apply_speaker; boot_selftest_sound; in serial mode (boot_dip
   bit 0 clear) boot_init_baud with X = 0 and boot_init_host_lines; then boot_reentry_warm. */
void boot_reset(fw *f);
/* $A665: boot_read_dip, boot_init_settings, then boot_reentry_warm (the ROM falls into it). */
void boot_reentry_cold(fw *f, fw_regs *r);
/* $A66B: ser_abort = 0; CLI (fw_set_interrupts); isr_par_hold; isr_par_ready; cmd_apply_speaker; bank_load_ramcode,
   bank_scan, bank_find_current; then the main loop $A681 for ever: boot_loop_text, pros_make_segments,
   boot_loop_play (C calls C). */
void boot_reentry_warm(fw *f, fw_regs *r);
/* $FFD0, the main bank's cold entry: SEI (the signature byte $78 bank_probe reads), then boot_reentry_cold. */
void bank_entry_cold(fw *f, fw_regs *r);
/* $FFD4, its warm entry: boot_reentry_warm. */
void bank_entry_warm(fw *f, fw_regs *r);
/* $C3DF, the handler of vec_int3_step $FFF6 and vec_int0 $FFFC: RTI, nothing else. The board never raises those
   lines (M4.6c2 spec 2.1). */
void isr_unused(fw *f, fw_regs *r);

#endif
```

**`src/firmware/boot.c`**: replace

```c
#include "boot.h"
#include "bank_addr.h"
#include "boot_input.h"
#include "cmd_apply.h"
```

with

```c
#include "boot.h"
#include "bank.h"
#include "bank_addr.h"
#include "boot_addr.h"
#include "boot_input.h"
#include "cmd_apply.h"
```

**`src/firmware/boot.c`**: replace

```c
#define BOOT_CMD_CHAR    '@'    /* boot_set_defaults: cmd_char and ser_cmd_char */
#define BOOT_SAY_RATE    1      /* boot_init_baud: X = 1 speaks the rate */

typedef struct { uint16_t addr; uint8_t value; } setting;
```

with

```c
#define BOOT_CMD_CHAR    '@'    /* boot_set_defaults: cmd_char and ser_cmd_char */
#define BOOT_SAY_RATE    1      /* boot_init_baud: X = 1 speaks the rate */
#define BOOT_ZP_CLEAR    0xC9   /* boot_reset clears $C9 down to $01 */

typedef struct { uint16_t addr; uint8_t value; } setting;
```

**`src/firmware/boot.c`**: replace

```c
        return;                                                  /* abandoned (without a trap: a unit test) */
    pros_make_segments(f, r);
    boot_loop_play(f, r);
}
```

with

```c
        return;                                                  /* abandoned (without a trap: a unit test) */
    pros_make_segments(f, r);
    boot_loop_play(f, r);
}

/* $A681-$A68A: the main loop, for ever (JMP $A681). It returns only when abandoned without a trap (a unit test). */
static void main_loop(fw *f, fw_regs *r)
{
    for (;;) {
        boot_loop_text(f, r);
        if (f->fault || f->stopped || f->unwound)
            return;
        pros_make_segments(f, r);
        boot_loop_play(f, r);
        if (f->fault || f->stopped)
            return;
    }
}

void boot_reentry_warm(fw *f, fw_regs *r)
{
    fw_wr8(f, SER_ABORT, 0);                                     /* $A66B LDM #0 */
    fw_set_interrupts(f, true, BOOT_CLI_WARM);                   /* $A66E CLI */
    isr_par_hold(f, r);
    isr_par_ready(f, r);
    r->a = cmd_apply_speaker(f, &r->x, &r->y);                   /* $A675 */
    bank_load_ramcode(f, r);
    bank_scan(f, r);
    bank_find_current(f, r);
    main_loop(f, r);
}

void boot_reentry_cold(fw *f, fw_regs *r)
{
    boot_read_dip(f, r);                                         /* $A665 */
    boot_init_settings(f, r);
    boot_reentry_warm(f, r);                                     /* falls into $A66B */
}

void bank_entry_cold(fw *f, fw_regs *r)
{
    fw_set_interrupts(f, false, BANK_ENTRY_COLD);                /* $FFD0 SEI */
    boot_reentry_cold(f, r);                                     /* JMP boot_reentry_cold */
}

void bank_entry_warm(fw *f, fw_regs *r) { boot_reentry_warm(f, r); }   /* $FFD4 JMP boot_reentry_warm */

void boot_reset(fw *f)
{
    init_p3(f);                                                  /* $A61C-$A622 */
    fw_reset_cpu(f);                                             /* $A625-$A62C */
    fw_regs r = { 0, 0, 0, 0 };                                  /* LDA #0; TAY; TAX */
    fw_wr_sfr8(f, SFR_ICR1, r.a);
    fw_wr_sfr8(f, SFR_ICR2, r.a);
    fw_wr_sfr8(f, SFR_ICR3, r.a);
    for (r.x = BOOT_ZP_CLEAR; r.x != 0; r.x--)                   /* $A637 LDX #$C9; STA 0,X; DEX; BNE */
        fw_wr8(f, r.x, r.a);
    fw_wr8(f, CMD_VOLUME, r.a);                                  /* $A63E LDA #0; STA cmd_volume */
    isr_set_volume(f, &r);
    boot_read_dip(f, &r);
    boot_init_ports(f, &r);
    fw_wr_sfr8(f, SFR_ICR1, 1);                                  /* $A64B LDM #1,ICR1 */
    boot_init_settings(f, &r);
    r.a = cmd_apply_speaker(f, &r.x, &r.y);                      /* $A651 */
    boot_selftest_sound(f, &r);
    if (f->fault || f->stopped)
        return;
    if (!(fw_rd8(f, BOOT_DIP) & DIP_PARALLEL)) {                 /* $A657 BBS0 boot_dip */
        r.x = 0;
        boot_init_baud(f, &r);
        boot_init_host_lines(f, &r);
    }
    boot_reentry_warm(f, &r);                                    /* $A662 JMP boot_reentry_warm */
}

void isr_unused(fw *f, fw_regs *r)
{
    (void)f;
    (void)r;                                                     /* $C3DF RTI */
}
```

**`src/engine/thunks.c`**: replace

```c
   an entry row the engine's phase deadline (b->phase, when the engine set one), cleared however it ends, so that a
   C wait that never ends (par_play's, a whole utterance inside one hook call) faults or, at the phase deadline, ends
   the run (thunks.h). */
static bool run_frame(machine *m, fw_bridge *b, thunk_regs regs, frame_kind kind)
{
```

with

```c
   an entry row the engine's phase deadline (b->phase, when the engine set one), cleared however it ends, so that a
   C wait that never ends (par_play's, a whole utterance inside one hook call) faults or, at the phase deadline, ends
   the run (thunks.h). An entry whose C enters it again runs its C afresh (M4.6c2 spec 3.4). */
static bool run_frame(machine *m, fw_bridge *b, thunk_regs regs, frame_kind kind)
{
```

**`src/engine/thunks.c`**: replace

```c
    if (setjmp(trap) == 0)
        regs(&b->f, &m->cpu);
    b->f.trap = saved;
    if (outermost) {
```

with

```c
    if (setjmp(trap) == 0)
        regs(&b->f, &m->cpu);
    while (kind == FRAME_ENTRY && b->f.unwound && m->cpu.pc == entry) {
        /* its C entered this entry again (a warm @=1 under bank_entry_warm): a fresh run of its C, as the hook would
           fire there again; a hook that leaves the PC at its own address is the machine's spin-guard fault */
        b->f.unwound = false;
        b->unwinds++;
        b->thunk_calls++;
        sync_p2(b);
        if (setjmp(trap) == 0)
            regs(&b->f, &m->cpu);
    }
    b->f.trap = saved;
    if (outermost) {
```

**`src/engine/thunks.c`**: replace

```c
   isr_speech_stop after $B2D2, which keeps all three to boot_loop_play's exit (its thunk); isr_volume_half ($B46F)
   returns them to its callers (its row); cmd_ev_set_volume ($BCEF) returns them to par_play's $B892 JMP par_play
   (LDX first). No caller reads a flag. The C boot_loop_play, isr_volume_half and cmd_ev_set_volume call the C, so
   in "all" mode the hook fires from boot_reset only. Declared: A, X, Y. */
static void isr_set_volume_regs(fw *f, cpu740 *c)
{
```

with

```c
   isr_speech_stop after $B2D2, which keeps all three to boot_loop_play's exit (its thunk); isr_volume_half ($B46F)
   returns them to its callers (its row); cmd_ev_set_volume ($BCEF) returns them to par_play's $B892 JMP par_play
   (LDX first). No caller reads a flag. The C boot_reset, boot_loop_play, isr_volume_half and cmd_ev_set_volume call
   the C, so in "all" mode the hook never fires. Declared: A, X, Y. */
static void isr_set_volume_regs(fw *f, cpu740 *c)
{
```

**`src/engine/thunks.c`**: replace

```c
   ser_tx_service after $A6D0 (its thunk); boot_reset ($A672) calls cmd_apply_speaker, which takes X and Y (its thunk);
   boot_queue_string ($C62B) pulls Y and A next ($C62E-$C630). No caller reads a flag (CLI, BBC0, JSR or PLA follow).
   The C callers (tn_convert_clause, cmd_scan_clause, par_play, the prosody passes, boot_get_clause and
   boot_queue_string) call the C, so in "all" mode the hook fires from boot_reset only. Declared: A, X, Y. */
static void isr_par_ready_regs(fw *f, cpu740 *c) { ser_regs(f, c, isr_par_ready); }

/* isr_par_hold $C5DE: A, X and Y in and out as they came (PHA at $C5DE, PLA at $C5EA; X and Y untouched). boot_reset
   ($A66F) calls isr_par_ready (which keeps them) and cmd_apply_speaker, which takes X and Y (its thunk);
   boot_get_clause ($A6E4) reads X ($A706 / $A70D STA $1B00,X) after loading Y and A ($A6EB LDY #0, $A6ED LDA);
   boot_loop_play ($B2B3) calls par_play_start, which reads none (its thunk); boot_queue_string ($C617) reads the Y it
   set before ($C615 LDY #0; $C61C LDA ($46),Y). No caller reads a flag. The C boot_loop_play, boot_get_clause and
   boot_queue_string call the C, so in "all" mode the hook fires from boot_reset only. Declared: A, X, Y. */
static void isr_par_hold_regs(fw *f, cpu740 *c) { ser_regs(f, c, isr_par_hold); }

```

with

```c
   ser_tx_service after $A6D0 (its thunk); boot_reset ($A672) calls cmd_apply_speaker, which takes X and Y (its thunk);
   boot_queue_string ($C62B) pulls Y and A next ($C62E-$C630). No caller reads a flag (CLI, BBC0, JSR or PLA follow).
   The C callers (tn_convert_clause, cmd_scan_clause, par_play, the prosody passes, boot_reentry_warm,
   boot_get_clause and boot_queue_string) call the C, so in "all" mode the hook never fires. Declared: A, X, Y. */
static void isr_par_ready_regs(fw *f, cpu740 *c) { ser_regs(f, c, isr_par_ready); }

/* isr_par_hold $C5DE: A, X and Y in and out as they came (PHA at $C5DE, PLA at $C5EA; X and Y untouched). boot_reset
   ($A66F) calls isr_par_ready (which keeps them) and cmd_apply_speaker, which takes X and Y (its thunk);
   boot_get_clause ($A6E4) reads X ($A706 / $A70D STA $1B00,X) after loading Y and A ($A6EB LDY #0, $A6ED LDA);
   boot_loop_play ($B2B3) calls par_play_start, which reads none (its thunk); boot_queue_string ($C617) reads the Y it
   set before ($C615 LDY #0; $C61C LDA ($46),Y). No caller reads a flag. The C boot_loop_play, boot_reentry_warm,
   boot_get_clause and boot_queue_string call the C, so in "all" mode the hook never fires. Declared: A, X, Y. */
static void isr_par_hold_regs(fw *f, cpu740 *c) { ser_regs(f, c, isr_par_hold); }

```

**`src/engine/thunks.c`**: replace

```c
static void boot_selftest_sound_regs(fw *f, cpu740 *c) { boot_regs(f, c, boot_selftest_sound); }

/* The hook of each routine: its register mapping run in the shared thunk frame, user = the bound fw_bridge. */
#define THUNK(name) \
```

with

```c
static void boot_selftest_sound_regs(fw *f, cpu740 *c) { boot_regs(f, c, boot_selftest_sound); }

/* boot_reset $A61C, entered through vec_reset $FFFE: no register input (LDA #0 / TAY / TAX). It never returns: no
   output declared. An entry row: run_frame runs it under the engine's phase deadline (M4.6c2 spec 3.2). */
static void boot_reset_regs(fw *f, cpu740 *c)
{
    (void)c;
    boot_reset(f);
}

/* The other entries, which never return either (no output declared; entry rows): boot_reentry_cold $A665 and
   boot_reentry_warm $A66B, entered by the bank entries' JMPs (and boot_reset's $A662 JMP $A66B), and the main bank's
   bank_entry_cold $FFD0 and bank_entry_warm $FFD4, entered by bank_enter's JMPs with SP $FF, I set, A and X (A = the
   bank's $FFE0 and X = $FF, or 0 and 0). A, X and Y in, none read: boot_reentry_cold loads A first ($ADE4 LDA P4),
   boot_reentry_warm reads none before cmd_apply_speaker (A loaded first; X and Y handed to par_setup_voice, which
   does not read them); bank_load_ramcode then saves X and Y in $76 and $78 for bank_restore to reload, as its row
   says. */
static void entry_regs(fw *f, cpu740 *c, void (*fn)(fw *, fw_regs *))
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    fn(f, &r);
}
static void boot_reentry_cold_regs(fw *f, cpu740 *c) { entry_regs(f, c, boot_reentry_cold); }
static void boot_reentry_warm_regs(fw *f, cpu740 *c) { entry_regs(f, c, boot_reentry_warm); }
static void bank_entry_cold_regs(fw *f, cpu740 *c) { entry_regs(f, c, bank_entry_cold); }
static void bank_entry_warm_regs(fw *f, cpu740 *c) { entry_regs(f, c, bank_entry_warm); }

/* isr_unused $C3DF, an interrupt handler (isr): the RTI of vec_int3_step $FFF6 and vec_int0 $FFFC, which the board
   never raises (unreachable). A, X and Y as they came; no outputs declared (M4.6a spec 3.2). */
static void isr_unused_regs(fw *f, cpu740 *c) { boot_regs(f, c, isr_unused); }

/* The hook of each routine: its register mapping run in the shared thunk frame, user = the bound fw_bridge. */
#define THUNK(name) \
```

**`src/engine/thunks.c`**: replace

```c
ISR_THUNK(isr_timer_dsp)
ISR_THUNK(isr_int1)
ISR_THUNK(isr_int4_host_line)
#undef ISR_THUNK

const fw_replacement fw_replacements[] = {
```

with

```c
ISR_THUNK(isr_timer_dsp)
ISR_THUNK(isr_int1)
ISR_THUNK(isr_unused)
ISR_THUNK(isr_int4_host_line)
#undef ISR_THUNK
/* The hook of an entry: its frame never returns (M4.6c2 spec 2.3). */
#define ENTRY_THUNK(name) \
    static bool t_##name(machine *m, void *user) { return run_frame(m, user, name##_regs, FRAME_ENTRY); }
ENTRY_THUNK(boot_reset)
ENTRY_THUNK(boot_reentry_cold)
ENTRY_THUNK(boot_reentry_warm)
ENTRY_THUNK(bank_entry_cold)
ENTRY_THUNK(bank_entry_warm)
#undef ENTRY_THUNK

const fw_replacement fw_replacements[] = {
```

**`src/engine/thunks.c`**: replace

```c
    { "bank_find_current",  0x02F0, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_bank_find_current, false, false, true },
    { "bank_load_ramcode",  0xA600, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_bank_load_ramcode },
    { "boot_loop_text",     0xA68D, 0,                   t_boot_loop_text, true },
    { "boot_get_clause",    0xA697, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_get_clause, true },
```

with

```c
    { "bank_find_current",  0x02F0, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_bank_find_current, false, false, true },
    { "bank_load_ramcode",  0xA600, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_bank_load_ramcode },
    { "boot_reset",         0xA61C, 0,                   t_boot_reset, false, false, false, true },
    { "boot_reentry_cold",  0xA665, 0,                   t_boot_reentry_cold, false, false, false, true },
    { "boot_reentry_warm",  0xA66B, 0,                   t_boot_reentry_warm, false, false, false, true },
    { "boot_loop_text",     0xA68D, 0,                   t_boot_loop_text, true },
    { "boot_get_clause",    0xA697, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_get_clause, true },
```

**`src/engine/thunks.c`**: replace

```c
    { "ser_rx_byte",        0xC37E, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_rx_byte },
    { "ser_rx_put",         0xC3CC, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_rx_put },
    { "isr_int4_host_line", 0xC3E0, 0,                   t_isr_int4_host_line, false, true },
    { "isr_host_line_high", 0xC3EB, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_host_line_high },
```

with

```c
    { "ser_rx_byte",        0xC37E, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_rx_byte },
    { "ser_rx_put",         0xC3CC, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_rx_put },
    { "isr_unused",         0xC3DF, 0,                   t_isr_unused, false, true },
    { "isr_int4_host_line", 0xC3E0, 0,                   t_isr_int4_host_line, false, true },
    { "isr_host_line_high", 0xC3EB, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_host_line_high },
```

**`src/engine/thunks.c`**: replace

```c
    { "pros_declination_step", 0xD40D, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_declination_step },
    { "ph_lengthen_count",  0xD43E, FW_OUT_A | FW_OUT_Y, t_ph_lengthen_count },
};
const size_t fw_replacement_count = sizeof fw_replacements / sizeof fw_replacements[0];
```

with

```c
    { "pros_declination_step", 0xD40D, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_declination_step },
    { "ph_lengthen_count",  0xD43E, FW_OUT_A | FW_OUT_Y, t_ph_lengthen_count },
    { "bank_entry_cold",    0xFFD0, 0,                   t_bank_entry_cold, false, false, false, true },
    { "bank_entry_warm",    0xFFD4, 0,                   t_bank_entry_warm, false, false, false, true },
};
const size_t fw_replacement_count = sizeof fw_replacements / sizeof fw_replacements[0];
```

**`src/firmware/isr_addr.h`**: replace

```c
#define FW_ISR_ADDR_H
/* ROM code addresses of speech timing, the DSP, the volume and the parallel handshake (M4.6b spec 2.1), checked
   against build/ghidra740/dump and the JSR/JMP operands and vectors in the ROM image. A caller in parentheses stays
   ROM (M4.6c). */
#define ROM_ISR_SET_VOLUME     0xACE8   /* called by boot_loop_play $B2C4, $B2D2, isr_volume_half $B46F,
                                           cmd_ev_set_volume $BCEF (and boot_reset $A642) */
```

with

```c
#define FW_ISR_ADDR_H
/* ROM code addresses of speech timing, the DSP, the volume and the parallel handshake (M4.6b spec 2.1), checked
   against build/ghidra740/dump and the JSR/JMP operands and vectors in the ROM image. A caller in parentheses was ROM
   until M4.6c2: it is C now and calls the C. */
#define ROM_ISR_SET_VOLUME     0xACE8   /* called by boot_loop_play $B2C4, $B2D2, isr_volume_half $B46F,
                                           cmd_ev_set_volume $BCEF (and boot_reset $A642) */
```

**`src/firmware/ser_addr.h`**: replace

```c
#define FW_SER_ADDR_H
/* ROM code addresses of the serial code (M4.6a spec 2.1), checked against build/ghidra740/dump and the JSR/JMP
   operands and vectors in the ROM image. A caller in parentheses stays ROM (M4.6c). */
#define ROM_ISR_INT1             0xC2B4   /* INT1: entered through vec_int1_serial $FFFA */
#define ROM_SER_RX_BYTE          0xC37E   /* called by isr_int1 $C32E, $C356, $C36F */
```

with

```c
#define FW_SER_ADDR_H
/* ROM code addresses of the serial code (M4.6a spec 2.1), checked against build/ghidra740/dump and the JSR/JMP
   operands and vectors in the ROM image. A caller in parentheses was ROM until M4.6c2: it is C now and calls the C. */
#define ROM_ISR_INT1             0xC2B4   /* INT1: entered through vec_int1_serial $FFFA */
#define ROM_SER_RX_BYTE          0xC37E   /* called by isr_int1 $C32E, $C356, $C36F */
```

**`src/firmware/bank_addr.h`**: replace

```c
   in the ROM image. bank_load_ramcode copies bank_ramcode_image $ADFE-$AFFC to RAM $0200-$03FE: the two bank tables
   and the routines below run from there (RAM address + BANK_RAMCODE_OFFSET = ROM address). A caller in parentheses
   stays ROM (M4.6c2). */

/* The RAM copy */
```

with

```c
   in the ROM image. bank_load_ramcode copies bank_ramcode_image $ADFE-$AFFC to RAM $0200-$03FE: the two bank tables
   and the routines below run from there (RAM address + BANK_RAMCODE_OFFSET = ROM address). A caller in parentheses
   was ROM until M4.6c2: it is C now and calls the C. */

/* The RAM copy */
```

**`src/firmware/par_addr.h`**: replace

```c
/* ROM code addresses of the playback set-up (M4.4a spec 2.2), the frame loop (M4.4b spec 2.2) and the frame
   builder (M4.5 spec 2.2), checked against build/ghidra740/dump and the JSR/JMP operands in the ROM image (the
   interrupt routines they call are in isr_addr.h, M4.6b). A caller in parentheses stays ROM. */
#define ROM_BOOT_LOOP_PLAY         0xA7D4   /* called by tn_convert_clause $B204 (and boot_reset $A687,
                                               boot_selftest_sound $A7BA, cmd_say_phonetic $A87C) */
```

with

```c
/* ROM code addresses of the playback set-up (M4.4a spec 2.2), the frame loop (M4.4b spec 2.2) and the frame
   builder (M4.5 spec 2.2), checked against build/ghidra740/dump and the JSR/JMP operands in the ROM image (the
   interrupt routines they call are in isr_addr.h, M4.6b). A caller in parentheses was ROM until M4.6c2: it is C now
   and calls the C. */
#define ROM_BOOT_LOOP_PLAY         0xA7D4   /* called by tn_convert_clause $B204 (and boot_reset $A687,
                                               boot_selftest_sound $A7BA, cmd_say_phonetic $A87C) */
```

**`src/firmware/pros_addr.h`**: replace

```c
#define ROM_PROS_RESET_SEG_PTRS     0xCA71   /* also called at playback by par_play_start $BAF5 */
#define ROM_PROS_MAKE_SEGMENTS      0xCA92   /* called by boot_reset $A684, boot_selftest_sound $A7B7 and
                                                cmd_say_phonetic $A879; the C tn_convert_clause calls the C */
#define ROM_PROS_MARK_STRESS        0xCABD   /* called by pros_make_segments $CAB3 */
#define ROM_PROS_MARK_PHRASES_EXIT  0xCEEC   /* pros_mark_phrases' exit block, before its entry (BEQ from $CF26) */
```

with

```c
#define ROM_PROS_RESET_SEG_PTRS     0xCA71   /* also called at playback by par_play_start $BAF5 */
#define ROM_PROS_MAKE_SEGMENTS      0xCA92   /* called by boot_reset $A684, boot_selftest_sound $A7B7 and
                                                cmd_say_phonetic $A879, and tn_convert_clause: their C calls the C */
#define ROM_PROS_MARK_STRESS        0xCABD   /* called by pros_make_segments $CAB3 */
#define ROM_PROS_MARK_PHRASES_EXIT  0xCEEC   /* pros_mark_phrases' exit block, before its entry (BEQ from $CF26) */
```

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test fw"`, then `build/apollo2_tests.exe machine_hooks`, `build/apollo2_tests.exe diff_main` and `build/apollo2_tests.exe diff_lts`.
Expected:

- the build passes with no warnings; `fw`: `11 suite(s), 0 failure(s), 0 skip(s)`; `suite_machine_hooks ok`;
- `diff_main`, the all-C hybrid against the ROM engine:

```
  corpus: 47 texts, 94 idles, 25167 frame events compared
  warm           idle, 2 idles, 101 frame events, $FFD4 reached 1
  warm_again     idle, 2 idles, 106 frame events, $FFD4 reached 2
  cold           idle, 2 idles, 101 frame events, $FFD0 reached 1
  dip_bank       idle, 2 idles, 41 frame events, $0210 reached 1
  selftest_fail  idle, 2 idles, 365 frame events, $A7AB reached 1
  status_sent    idle, 2 idles, 41 frame events, $A6D7 reached 1
  status_waits   cycle limit, 1 idles, 100 frame events, $A6D7 reached 4531687
  parallel       cycle limit, 0 idles, 41 frame events, $C603 reached 806094
  rf1_space      idle, 2 idles, 1565 frame events, $A731 reached 3
  rf1_full       idle, 2 idles, 1675 frame events, $A731 reached 30
  rf1_carry      idle, 2 idles, 1370 frame events, $A739 reached 1
  rf2_sentences  idle, 2 idles, 220 frame events, $A704 reached 5
  rf3_empty      idle, 2 idles, 41 frame events, $A704 reached 2
  rf4_commands   idle, 2 idles, 52 frame events, $A9C9 reached 1
  rf5_during     idle, 2 idles, 161 frame events, $A6E4 reached 15
  baud_p31       idle, 2 idles, 115 frame events, $AC9D reached 1
  baud_p32       idle, 2 idles, 115 frame events, $ACB0 reached 1
  baud_both      idle, 2 idles, 115 frame events, $ACC3 reached 1
  host_line_high idle, 2 idles, 41 frame events, $AC72 reached 1
  66 runs, 129 idles, 31533 frame events compared in all
suite_diff_main              ok
1 suite(s), 0 failure(s), 0 skip(s)
```

  0 mismatches (18 s in the proof run, under the 5 minutes the run-time gate allows);
- `diff_lts`: 116 lines: the 115 of Task 5 and `isr_unused 0 calls checked`; `1 mid-clause flush calls skipped (golden_hybrid covers them)`, `24 unwound calls compared`, 0 mismatches (173 s in the proof run, under the 5 minutes).

- [ ] **Step 5: The branch probe (recorded; the tool is not part of the plan)**

A throwaway patch of `diff_main`'s ROM side (a hook at both destinations of each conditional branch of the 21 routines, which finds the branch as the last instruction executed) counted each branch taken and not taken over the corpus and the extra runs of Step 4:

| Routine | Branch | Corpus: taken | Not taken | The missing direction |
|---|---|---|---|---|
| `boot_reset` | $A63C `BNE` (the $C9–$01 clear) | 9,400 | 47 | |
| `boot_reset` | $A657 `BBS0 boot_dip` | 0 | 47 | `parallel`: taken once |
| `boot_get_clause` | $A6A1 `BEQ` ($1827 = 0) | 272 | 0 | `rf1_carry`: not taken once |
| `boot_get_clause` | $A6B8 `BNE` (low bytes differ at entry) | 95 | 177 | |
| `boot_get_clause` | $A6BE `BNE` (pages differ at entry) | 0 | 177 | none: the ring 256 bytes apart; pinned by `test_get_clause_page_apart` |
| `boot_get_clause` | $A6C6 `BNE` (low bytes differ) | 2,927 | 2,695 | |
| `boot_get_clause` | $A6CC `BNE` (pages differ) | 0 | 2,695 | none: pinned by `test_get_clause_page_apart` |
| `boot_get_clause` | $A6D4 `BBC0 ser_status_due` | 2,695 | 0 | `status_sent` and `host_line_high` once, `status_waits` 4,531,687 times |
| `boot_get_clause` | $A6E8 `BBS0 ser_abort` | 1 | 2,926 | |
| `boot_get_clause` | $A6F1 `BNE` (page end) | 2,925 | 1 | |
| `boot_get_clause` | $A702 `BNE` (not CR) | 2,879 | 47 | |
| `boot_get_clause` | $A713, $A717, $A71B, $A71F, $A723, $A727 `BEQ` (`.` `,` `:` `;` `!` `?`) | 92, 64, 4, 2, 5, 10 | 2,787 … 2,702 | |
| `boot_get_clause` | $A72B `BCS` (X ≥ $FA) | 0 | 2,702 | `rf1_full`: taken once |
| `boot_get_clause` | $A72F `BCC` (X < $DC) | 2,702 | 0 | `rf1_space`, `rf1_full`, `rf1_carry`: not taken 35 times |
| `boot_get_clause` | $A733 `BEQ` (a space) | 0 | 0 | `rf1_space`: taken once; not taken 34 times |
| `boot_get_clause` | $A737 `BNE` (not `cmd_char`) | 0 | 0 | taken 33 times; `rf1_carry`: not taken once |
| `isr_idle` | $C5FF `BBS0 boot_dip` | 0 | 2,695 | `parallel`: taken 806,094 times |
| `boot_selftest_sound` | $A7A8 `BBC0 boot_selftest_fail` | 47 | 0 | `selftest_fail`: not taken once |
| `boot_init_ports` | $AC22, $AC3C, $AC4D `BBC0 boot_dip` | 47 each | 0 | `parallel`: not taken once each |
| `boot_load_prosody_defaults` | $A7FB `BNE` | 1,175 | 47 | |
| `boot_init_baud` | $AC81 `BBS1 P3` | 0 | 47 | `baud_p31`, `baud_both`: taken |
| `boot_init_baud` | $AC84 `BBS2 P3` | 0 | 47 | `baud_p32`: taken |
| `boot_init_baud` | $AC9A `BBS2 P3` | 0 | 0 | `baud_both` taken, `baud_p31` not |
| `boot_init_baud` | $AC8C, $ACA2, $ACB5 `BNE` (X ≠ 1) | 47, 0, 0 | 0 | not taken: X = 1, unreachable: pinned by `test_say_rate`; $ACA2 and $ACB5 taken by `baud_p31` and `baud_p32` |
| `boot_init_host_lines` | $AC6F `BBC2 P0` | 47 | 0 | `host_line_high`: not taken once |
| `boot_selftest` | $C608 `BBC1 boot_dip` | 47 | 0 | `dip_bank`, `selftest_fail`: not taken |
| `boot_selftest` | $C60B `BBC1 P2` | 0 | 0 | `dip_bank` taken, `selftest_fail` not |
| `boot_queue_string` | $C61E `BEQ` | 47 | 282 | |

The other twelve routines have no conditional branch. Every branch is seen both ways on the corpus or in `diff_main`'s extra runs but the two ring tests and the three `X = 1` tests, which are pinned by named unit tests (spec §4.4, §4.6, §4.7 criterion 4). A throwaway coverage run of the hybrid over the 47 corpus texts recorded the ROM instructions it executes: $B372 and $B37F only.

- [ ] **Step 6: Run the full suite and the CLI check**

Run: `cmd //c ".\build.bat test"` (in the background)
Expected:

- `40 suite(s), 0 failure(s), 0 skip(s)`, no warnings, the firmware syntax check passing (924 s in the proof run);
- `firmware_data`: `49092 converted bytes compared with the ROM, 0 differ`;
- `golden_hybrid`: `48 corpus files x 132 modes (all, then each routine alone)`, `suite_golden_hybrid ok`, every golden unchanged (`git status --short tests/golden` prints nothing);
- `diff_main` and `diff_lts` as in Step 4; `diff_play` as in Task 1 Step 5; `diff_serial` as in Task 3, 0 mismatches;
- `suite_golden` (`test_timing_invariance` on both engines): `timer scale 1/4: 105 frames, 11789152 CPU cycles`, `timer scale 3/1: 105 frames, 1286784 CPU cycles` (rom, unchanged), `hybrid timer scale 1/4: 105 frames, 11678411 CPU cycles`, `hybrid timer scale 3/1: 105 frames, 973321 CPU cycles` (fewer still: boot and the main loop take no emulated time);
- `suite_synth`: `corpus peak plain 20522 (-4.1 dBFS)`, `corpus peak b11 16038 (-6.2 dBFS)`.

Then spec §4.6 on every corpus text and the Review Focus texts the CLI can take (each corpus text read from its file, the WAV files in a scratch folder outside the repo, always with `-o`, and `APOLLO2_ROM` exported as the Global Constraints say: the CLI reads the ROM from it; the CLI refuses an empty text, so Review Focus 3 is `diff_main`'s and `golden_hybrid`'s, and 5 is `diff_main`'s alone):

```bash
SP="${TMPDIR:-/tmp}/m46c2_cli"
mkdir -p "$SP"
for f in tests/corpus/*.txt; do
  t=$(basename "$f" .txt)
  ./build/apollo2.exe --engine rom --text-file "$f" -o "$SP/${t}_rom.wav" || echo "$t rom failed"
  ./build/apollo2.exe --engine hybrid --text-file "$f" -o "$SP/${t}_hybrid.wav" || echo "$t hybrid failed"
  cmp -s "$SP/${t}_rom.wav" "$SP/${t}_hybrid.wav" && echo "$t same" || echo "$t DIFFERS"
done | grep -vc " same$"
W="the quick brown fox jumps over the lazy dog "
i=0
while IFS= read -r t; do
  i=$((i+1))
  ./build/apollo2.exe --engine rom -o "$SP/rf${i}_rom.wav" "$t" || echo "rf$i rom failed"
  ./build/apollo2.exe --engine hybrid -o "$SP/rf${i}_hybrid.wav" "$t" || echo "rf$i hybrid failed"
  cmp -s "$SP/rf${i}_rom.wav" "$SP/rf${i}_hybrid.wav" && echo "rf$i same" || echo "rf$i DIFFERS"
done <<EOF
$W$W$W$W$W$W
abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij
$W$W$W${W}the quick brown fox jumps over the lazy do@W5 now slower.
One. Two? Three!
@W5 @A3
EOF
rm -rf "$SP"
```

Expected: `0` (all 47 texts `same`; no `failed` or `DIFFERS` line), then `rf1 same` … `rf5 same` (`rf1`–`rf3`: Review Focus 1's three long texts, cut on a space, cut at X ≥ $FA, and carrying the cut `@`; `rf4`: Review Focus 2; `rf5`: Review Focus 4). The proof run took 13 s.

Last, spec §4.7 criterion 1: `grep -rn "fw_rom_call(" src/firmware` prints, besides its declaration (`fw.h`) and definition (`fw.c`), only `cmd_play_event.c`'s call of an unknown event handler (M4.4b), and `grep -n "fw_rom_call" src/firmware/boot.c src/firmware/boot_input.c` prints nothing: no ROM call to firmware code.

- [ ] **Step 7: Commit**

```bash
git add src/firmware/boot.h src/firmware/boot.c src/engine/thunks.c src/firmware/isr_addr.h src/firmware/ser_addr.h src/firmware/bank_addr.h src/firmware/par_addr.h src/firmware/pros_addr.h tests/test_fw_boot.c tests/test_machine_hooks.c tests/test_diff_main.c tests/test_golden_hybrid.c tests/test_diff_lts.c
git commit -m "The entries in C: boot_reset, the cold and warm re-entries with the main loop, the bank entries and isr_unused, with their hooks (an entry runs under the engine's phase deadline, and afresh when its C enters it again); every firmware routine a hybrid run executes is C but the host's wait loops and the NUL-bug fallback (M4.6c2 2.1-2.3, 3.2, 3.4, 4.1, 4.4-4.7)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 7: Parent spec rev 18, M4.6c2 status and README (spec §4.7 criterion 7)

**Files:**
- Modify: `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`, `docs/superpowers/specs/2026-10-13-m4-6c2-boot-main-loop-design.md`, `README.md`

**Interfaces:** documentation only.

- [ ] **Step 1: Parent spec, rev 18**

Make these edits in `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`. Mark each one *(rev 18)*. Find each place by its text, not by line number.

1. **Status line** (line 4). Insert after `**Status:** Approved. `:

   `*(rev 18)* Revision 18 records milestone 4.6c2 and with it the end of milestone 4: boot, the main loop and isr_idle in C; the C pulls its input through an idle handler both engines share, and a hybrid run executes no ROM firmware code but the host's wait loops and the NUL-bug fallback (design: docs/superpowers/specs/2026-10-13-m4-6c2-boot-main-loop-design.md). `

2. **§8 milestone 4** (the paragraph containing `**M4.6c1 done**`). Replace its last sentence, `Next: M4.6c2, boot, the main loop and isr_idle.`, with:

   `Next: M4.6c2, boot, the main loop and isr_idle. *(rev 18)* **M4.6c2 done** <DATE>: boot, the main loop and isr_idle run as C (boot_reset and its warm and cold re-entries, the bank-entry stubs, the boot helpers, the self-test announcement, boot_loop_text, boot_get_clause, isr_idle, cmd_say_phonetic and isr_unused; 131 routines hooked in all); the engines feed input through an idle handler on the machine, which the C's input wait at $C602 calls too, and the entry rows run under the engine's phase budgets; the self-test strings and boot_prosody_defaults are converted (the baud texts already were, since M4.1); the new end-to-end test diff_main (the ROM engine against the all-C hybrid at every input idle and frame event, RAM compared but the stack page), diff_lts, diff_play, diff_serial (with the DIP-bit-1 bank probes), golden_hybrid (48 files x 132 modes) and test_timing_invariance on both engines pass. **Milestone 4 is complete**: a hybrid run executes no ROM firmware code but the host's wait loops ($C500, $B372, $B37F) and the ROM cmd_run_next for a NUL-bug handler address. Next: milestone 5, the native host.`

   Replace `<DATE>` with the date of your commit (`date +%F`).

3. **§6 step 3** (the paragraph containing `*(rev 17)* M4.6c1: the firmware tracks the selected bank`). Append at its end:

   ` *(rev 18)* M4.6c2: FW_WAIT_INPUT is isr_idle's WIT at $C602: the hybrid host parks the CPU at the idle point and, when the machine has nothing left to do, calls the idle handler the engine registered with machine_set_idle_handler, which feeds the next input or stops (the C is then abandoned and the machine left at the idle point); reset_cpu and set_interrupts carry boot's CPU set-up and the main loop's unpaired CLI/SEI; the entry rows run under the engine's phase budgets instead of the bridge's.`

- [ ] **Step 2: M4.6c2 spec status and As built**

In `docs/superpowers/specs/2026-10-13-m4-6c2-boot-main-loop-design.md`, replace `**Status:** design, awaiting user review` with:

`**Status:** implemented (plan docs/superpowers/plans/2026-10-13-m4-6c2-boot-main-loop.md)`

Then add a final section headed `## 6. As built`, after §5. Open it with the sentence `Where the ROM is more precise than, or differs from, the sections above. The ROM wins; ``ghidra740/`` is not edited.` (one pair of backquotes around `ghidra740/`). Then write one short factual bullet per difference between the spec and what was built. Gather them from:
- the "ROM facts checked" lists at the top of Tasks 1–6 of the plan, keeping only those that contradict or sharpen the spec;
- the code:
  - the thunk outputs as finally declared for the 21 new hooks (read the rows and comments in `src/engine/thunks.c`) and why;
  - the C layout (`boot.c`, `boot.h`, `boot_input.c`, `boot_input.h`, `boot_addr.h`, `data/boot_data.c`, the additions to `fw.h`/`fw.c`, `thunks.c`, `machine.h`/`machine.c`, `rom_engine.c`, `hybrid_engine.c`) and the static helpers;
  - the idle handler, the input wait and stop, the phase deadline, `reset_cpu` and `set_interrupts` as built;
  - each of `boot_get_clause`'s waits as built and where it sits;
  - `diff_main`'s runs, comparison points and counts, and its run time;
  - `diff_lts`'s and `diff_serial`'s additions and their counts;
  - the unit-test changes in the existing test files, and what `tests/test_fw_boot.c` covers;
  - the Review Focus tests and what each showed;
  - the branch probe's result and the list of ROM PCs a hybrid corpus run executes (any branch pinned by a unit test instead).

Every bullet must be true of the committed code and the dumps. A claim about a test must say only what that test checks. Follow the style of §6 of `docs/superpowers/specs/2026-10-13-m4-6c1-banks-design.md`. If nothing differs, add no section.

- [ ] **Step 3: README**

In `README.md`'s "Hybrid engine (milestone 4)" section, replace:

```
the banks and the command dispatcher: the RAM-resident bank code, @= and cmd_run_next (M4.6c1); the rest (boot,
the main loop and isr_idle) still runs on the emulated CPU.
```

with:

```
the banks and the command dispatcher: the RAM-resident bank code, @= and cmd_run_next (M4.6c1), and boot, the
main loop and isr_idle (M4.6c2). So every firmware routine now runs as C; the emulated CPU supplies the hardware
(timers, UART, DSP port, interrupts) and runs only three hardware wait loops, plus the ROM's own dispatcher for a
handler address that a NUL byte after a command produces.
```

Then replace:

```
compares the paths no corpus text reaches (reply transmission, parallel mode, the ring wraps, the timer
interrupt's rare paths, the banks' cold entry and DIP bit 1) through probes injected into both copies.
```

with:

```
compares the paths no corpus text reaches (reply transmission, parallel mode, the ring wraps, the timer
interrupt's rare paths, the banks' cold entry and DIP bit 1) through probes injected into both copies, and
`build.bat test diff_main` runs the ROM engine and the all-C hybrid side by side, comparing them at every input
idle and frame event.
```

- [ ] **Step 4: Run the full suite and commit**

Run: `cmd //c ".\build.bat test"` (in the background; it takes about 16 minutes)
Expected: `40 suite(s), 0 failure(s), 0 skip(s)`.

```bash
git add docs/superpowers/specs/2026-09-30-software-apollo2-design.md docs/superpowers/specs/2026-10-13-m4-6c2-boot-main-loop-design.md README.md
git commit -m "Spec rev 18: milestone 4.6c2 done (boot, the main loop and isr_idle in C); milestone 4 complete

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
