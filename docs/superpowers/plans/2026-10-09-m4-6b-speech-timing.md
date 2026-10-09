# M4.6b Speech Timing and the DSP in C — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the firmware's speech timing to C (the 10 kHz timer interrupt that feeds the DSP, the start and stop of DSP output, the frame waits, the volume and the parallel-port handshake; 11 hooked routines) and prove it against the ROM through `diff_lts`, `diff_play`, `diff_serial` and the goldens.

**Architecture:**
- **Infrastructure first (Task 1).**
  - Two new host calls: `dsp_write` (the DSP port at `$8000`) and `frame_done` (the frame event at the point of `$B306`).
  - The wait seam gains `at` (the ROM loop's address) and three kinds: `FW_WAIT_INTERRUPT`, `FW_WAIT_TIMER2`, `FW_WAIT_TIMER3`.
  - Three machine API additions: `machine_capture_frame`, `machine_wait_interrupt`, `machine_run_loop`.
  - P2 writes keep the low-window cache in step.
  - `diff_lts` compares the timer, `P23_FUNC` and DSP-output state; the suite `suite_fw_dsp` is added.
- **The tests before the C (Task 2).** `diff_play`'s injections move off `isr_par_ready`: the reply to the frame event, the mute to the timer interrupt two ticks after it; `diff_serial` gains probes for parallel mode in `isr_par_ready`/`isr_par_hold` and for the timer ISR. Both are proved ROM against ROM.
- **Then the C.** Volume and the parallel handshake, with the polling loops on `FW_WAIT_INTERRUPT` (Task 3); DSP control and the frame waits (Task 4); the timer ISR (Task 5).
- **C calls C.** `boot_loop_play`'s body, `par_play`, `cmd_ev_set_volume` and `fw_par_ready` call the C routines directly.
- **What stays ROM.** `ser_tx_wait` `$C500` and the two timer-sync loops of `isr_timer_start` (`$B372`, `$B37F`; host code behind the wait seam); boot, the main loop, `isr_idle`, the banks and `cmd_run_next` (M4.6c).
- **Hooks.** 11 routines, giving 100 replacements and 101 `golden_hybrid` modes.

**Tech Stack:** C11 with MSVC 2022 (`build.bat`, `/W4 /WX`).

**Spec:** `docs/superpowers/specs/2026-10-09-m4-6b-speech-timing-design.md` (M4.6b spec). Its foundations are:
- the M4.6a spec, `docs/superpowers/specs/2026-10-08-m4-6a-serial-design.md` (SFR access, the wait seam, `isr` rows, `diff_serial`; §6 "As built");
- the M4.4b spec, `docs/superpowers/specs/2026-10-07-m4-4b-frame-loop-design.md` (`par_play`, `boot_loop_play`, `waits` rows, `diff_play`, the bridge deadline; §6 "As built");
- the M4.0 spec, `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md` (§8 "As built").

The parent spec is `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 15; §4 the machine model, frame capture at `$B306`, serial delivery at the idle WIT).

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
- The full suite takes over 10 minutes. Use a suite filter while iterating and the full run at each task's end, in the background (a foreground Bash call is capped at 600000 ms).

**What may and may not be committed**
- Never commit the ROM, any `*.BIN`/`*.bin`, `*.mp3`, `apollo2_excerpt.wav`, `*.sla`, `build/` or `apollo_ghidra/`.
- Committed files contain no ROM bytes as hex blobs. Converted data is readable C: numbers with their meaning, named bits, strings, named addresses.
- Stage files by explicit path only. Never use `git add -A` or `git add .`.

**User-owned files and tools**
- Never stage, move or delete the user's untracked files: `docs/*` other than `docs/superpowers/`, `pics/`, `session_memory.json`, `apollo2_for_aksel.wav`, `apollo2_for_aksel.mp3`, `apollo2.png`.
- Never modify anything under `C:\ghidra_12.1.3_PUBLIC`, the user's Ghidra GUI project, or `ghidra740/` (including `ghidra740/coverage/`). Only read the annotations and the dumps.
- Disassembly dumps for checking the C against the ROM (git-ignored, read-only) are in the main checkout:
  - `C:\Users\abart\Desktop\Dolphin Apollo 2 ROM\build\ghidra740\dump\rom_<ADDR>_<name>.txt`
  - `data.txt` and `functions.txt` in the same folder.

**Correctness rules**
- The goldens in `tests/golden/*.frm` stay byte-identical. No new golden. If anything changes a golden, stop and report.
- The machine model (parent spec §4) does not change. The only machine additions are API: `machine_capture_frame`, `machine_wait_interrupt`, `machine_run_loop`, and access to state for the comparisons.
- Nothing under `src/firmware/` includes a `machine/` or `cpu740/` header. `build.bat` checks this.
- The ported C keeps the ROM's behaviour exactly, quirks included (M4.6b spec §2.6):
  - every RAM write, the scratch ones included (`$75`, the ISR counters, `isr_frame_flag` as a whole byte, `frm_source_index`/`frm_filter_index`);
  - every SFR read and write in the ROM's order, through the host, each `SEB`/`CLB` as a read then a write;
  - every DSP-port byte and the frame event at the ROM's point, through the host;
  - the 8-bit wraps exactly as the ROM has them;
  - the exit registers the callers read.
- Writes to the dead stack (below SP at exit) need not be reproduced, because `diff_lts`, `diff_play` and `diff_serial` exclude it.
- ROM data is read only through `fw_rom8` (or `fw_ind_y`/`fw_mem8`) at its ROM address. SFRs are accessed only through the `fw_sfr*` helpers; the DSP port and the frame event only through `fw_dsp_write` and `fw_frame_done`.
- Thunk outputs: each with a comment saying what the ROM callers read after the call, checked in the dumps; the `isr` thunk declares none.
- A polling loop on a flag an interrupt sets never spins in C: it waits through `fw_wait(f, FW_WAIT_INTERRUPT, at)` between tests.
- `diff_lts`, `diff_play` and `diff_serial` failures are fixed by finding the root cause in the dump. Never loosen a comparison or an assertion.

**Commits**
- End every commit message with exactly `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. This is the user's rule for this session.

---

### Task 1: Infrastructure: the DSP port and the frame event through the host, the wait seam with `at` and three new kinds, `machine_capture_frame`, `machine_wait_interrupt`, `machine_run_loop`, the P2 rule, `diff_lts` comparing the timer and DSP-output state, `isr_addr.h`, the suite `suite_fw_dsp` (spec §3, §4.1, §4.6, §4.8 item 1)

**ROM facts checked / what was verified while proving the plan** (`src/cpu740/cpu740.c`, `src/machine/m50734.c`, `src/machine/machine.c`; `rom_B35D_isr_timer_start.txt`, `rom_B2D9_isr_timer_dsp.txt`, `rom_AC22_boot_init_ports.txt`; the ROM image for the operand scans; everything below was run, nothing is copied from the spec):
- **The frame capture** (`machine.c`, `exec`): at `APOLLO_SWAP_PC` ($B306) with the CPU not waiting, `capture_frame` copies the 20 bytes at the `$2C/$2D` pointer (`APOLLO_PRODUCER_PTR`, `isr_fill_ptr`), counts `frames_produced` and calls the frame callback, before the hook check and before the instruction. `machine_capture_frame` is that function made public; `test_capture_frame` compares it with a step at $B306 (same bytes, same count). Nothing else in the ROM jumps to $B306 (operand scan: no `JSR`/`JMP` to it; it is reached only from `BBC0 isr_frame_flag` at $B300), so a C timer interrupt that never executes $B306 and calls `frame_done` instead captures each frame once.
- **The DSP port** (`bus_write`): a write of $8000 counts `port_writes`, stores the byte at `raw_buf[raw_pos++]` and delivers a raw frame every 20 bytes; a rising P2.0 edge counts `dsp_resets` and restarts the grouping. The hybrid host's `dsp_write` is that bus write, so the counters and the raw stream are kept as before (`test_host_dsp`).
- **`WIT` and the interrupt entry** (`cpu740_step`, `take_irq`): a waiting CPU with no IRQ line spends 4 cycles per step; any asserted line ends the wait, even with I set, and then the instruction at the PC runs unless an unmasked interrupt is taken (7 cycles: PC and P pushed, I set, the vector loaded). So `machine_wait_interrupt`, which waits at a hook's PC, must never let a woken CPU run on with I set (it would execute the hooked routine's ROM code): it returns false at once in that case, without a step. `maybe_deliver` delivers a serial byte only when the CPU waits with the PC at $C603 (after the idle `WIT` at $C602): at a hook's PC no byte is delivered (`test_wait_interrupt`, a byte queued and not delivered). The handler's `RTI` returns to the hook's PC with the SP as before, which ends the call; a hooked handler (a C replacement, exit `machine_return_interrupt`) is taken the same way (the hook fires at the vector target: the CPU is not waiting and I is set).
- **The timers** (`m50734.c`): the period is `4 x (prescaler + 1) x (reload + 1)` CPU cycles; any write of a prescaler or reload restarts that timer with a full period; `m50734_tick` sets the request bits 1, 3, 5 (timers 1–3) as cycles pass; an ICR2 write keeps bits 0–5. Timer 3's tick after `isr_timer_start` (prescaler 0, reload $31) is 200 cycles, 10 kHz at 2 MHz.
- **`isr_timer_start`'s two loops**: $B372 `77 FE FD` (`BBC3 ICR2,$B372`), exit $B375; $B37F `B7 FE FD` (`BBC5 ICR2,$B37F`), exit $B382; both between `PHP; SEI` ($B35D–$B35E) and `PLP` ($B388). No `JSR`/`JMP` operand in the ROM names $B372, $B375, $B37F or $B382, and neither address is hooked, so the hybrid host runs them as host code with `machine_run_loop` (I set as the `SEI` left it, the PC and P put back after). A `BBC zp,rel` costs 5 cycles, 7 when taken (`k_mode_cycles`, then `cycles += 2` in `M_BBC`), so a 400-cycle timer ends the synthetic loop of `test_run_loop` within 412 cycles.
- **P2 and the window** (`machine_map_read`): the lower-ROM window is P2 pin 7; `boot_init_ports` sets `P2_DIR` = $FD ($AC53, $AC5B), so bit 7 is an output and its pin is the latch bit written. Hence the spec's rule: a P2 write through the SFR helpers sets `f->low_window` from bit 7 of the value written (`test_p2_window`).
- **The state `diff_same` adds** (`machine.h`, `m50734.h`): `sfr.p23_func`; `sfr.timer_pre[0-2]`, `sfr.timer_reload[0-2]`, `sfr.timer_running[0-2]`; `port_writes`, `raw_pos`, `raw_buf[20]`, `dsp_resets`, `dsp_reset_level`, `frames_produced`. Not `timer_count` (it moves as cycles pass, and the C takes none), nor `raw_frames` and `raw_discarded` (they follow from the others). With them, `diff_lts` prints exactly the 87 lines `<name> N calls checked`, `1 mid-clause flush calls skipped` and `2 unwound calls compared` it printed at 06d42c5 (Step 5): no existing routine touches them differently.
- **The callers of the 11 routines** (the scan of every `JSR`/`JMP` operand and of the vectors $FFE0–$FFFF; recorded in `isr_addr.h`): `isr_set_volume` $A642, $B2C4, $B2D2, $B46F, $BCEF; `isr_timer_dsp` only `vec_int2_timers` $FFF8; `isr_speech_start` $B2B9; `isr_wait_frames` $B352; `isr_timer_start` $B34D; `isr_dsp_reset` $B34A, $B39E; `isr_speech_stop` $B2D5; `isr_wait_frame` $B2BF, $B2CD, $B356, $B8D2; `isr_volume_half` $B2BC, $B2CA; `isr_par_ready` $A672, $A6D0, $A6DC, $A6FC, $AD09, $B1F6, $B89F, $B8AB, $B8C9, $B8E6, $B8F7, $B908, $C62B, $CA3B, $CB1F, $CF28, $D155; `isr_par_hold` $A66F, $A6E4, $B2B3, $C617. The byte before each entry is an `RTS` ($60), an `RTI` ($40, before $B323), the `BRA` offset of `isr_par_ready` ($0C before $C5DE) or data (text before $ACE8): no routine is entered by falling through (`util_shl3` $C657, entered from `util_shl4`, is not one of them).
- **`fw_dsp_write` and `fw_frame_done` without a host** (spec §3.1 says "as the SFR helpers do", not where): this plan faults at `FW_DSP_DATA` ($8000) and at `FW_FRAME_EVENT` ($B306). The host's `wait` gains the `at` argument too, so that a stub host can log it (spec §4.6); the hybrid host runs each kind's own loop and ignores it.

**Files:**
- Create: `src/firmware/isr_addr.h`, `tests/test_fw_dsp.c`
- Modify: `src/firmware/fw.h`, `src/firmware/fw.c`, `src/firmware/ser.c`, `src/firmware/fw_ops.h`, `src/firmware/par_addr.h`, `src/firmware/tn_convert.h`, `src/firmware/tn_convert.c`, `src/firmware/par_play.c`, `src/firmware/cmd_play_event.c`, `src/firmware/cmd_scan.c`, `src/firmware/pros_seg.c`, `src/firmware/cmd_addr.h`, `src/firmware/pros_addr.h`, `src/machine/machine.h`, `src/machine/machine.c`, `src/engine/thunks.c`, `tests/diff_state.h`, `tests/diff_state.c`, `tests/suites.h`, `tests/test_diff_lts.c`, `tests/test_diff_play.c`, `tests/test_fw_cmd.c`, `tests/test_fw_par.c`, `tests/test_fw_pros.c`, `tests/test_fw_ser.c`, `tests/test_fw_tn.c`, `tests/test_golden_hybrid.c`, `tests/test_machine_hooks.c`

**Interfaces:**
- Consumes: `fw_host`, `fault` (`fw.c`), `host_rom_call`, `window_on` and `fw_bridge_init` (`thunks.c`), `capture_frame`, `prepare`, `exec` and `machine_step` (`machine.c`), `m->bus.write`, `diff_same` and `sfr_same` (`tests/diff_state.c`).
- Produces (Tasks 2–5 use them):

```c
/* src/firmware/fw.h */
typedef enum fw_wait_kind { FW_WAIT_UART_TX = 0, FW_WAIT_INTERRUPT, FW_WAIT_TIMER2, FW_WAIT_TIMER3 } fw_wait_kind;
#define FW_WAIT_TIMER2_LOOP 0xB372, FW_WAIT_TIMER2_EXIT 0xB375, FW_WAIT_TIMER3_LOOP 0xB37F, FW_WAIT_TIMER3_EXIT 0xB382
#define FW_DSP_DATA 0x8000, FW_FRAME_EVENT 0xB306
fw_host: bool (*wait)(void *ctx, fw_wait_kind kind, uint16_t at); + void (*dsp_write)(void *ctx, uint8_t byte);
         + void (*frame_done)(void *ctx);
void fw_wait(fw *f, fw_wait_kind kind, uint16_t at);   /* faults at at */
void fw_dsp_write(fw *f, uint8_t v);                   /* faults at FW_DSP_DATA */
void fw_frame_done(fw *f);                             /* faults at FW_FRAME_EVENT */
/* src/machine/machine.h */
void machine_capture_frame(machine *m);
bool machine_wait_interrupt(machine *m, uint64_t max_cycles);
bool machine_run_loop(machine *m, uint16_t loop, uint16_t exit, uint64_t max_cycles);
/* src/firmware/isr_addr.h: ROM_ISR_SET_VOLUME ... ROM_ISR_PAR_HOLD (the 11 routines) and ROM_ISR_SWAP_BUFFERS;
   ROM_ISR_PAR_READY moves here from tn_convert.h, the others from par_addr.h */
/* tests/test_fw_dsp.c: the stub host (g_sfr, the ordered g_log of EV_READ / EV_WRITE / EV_WAIT / EV_DSP / EV_FRAME,
   g_wait_ok), setup(), check_log(); suite_fw_dsp, the last suite: 37 */
/* tests/test_diff_lts.c: k_every_call (the eight routines of spec 4.1, compared at every call); DIFF_MAX 104 */
```

No name collides: `grep -rn "fw_dsp_write\|fw_frame_done\|FW_WAIT_INTERRUPT\|FW_WAIT_TIMER\|FW_DSP_DATA\|FW_FRAME_EVENT\|machine_capture_frame\|machine_wait_interrupt\|machine_run_loop\|isr_addr\|suite_fw_dsp\|k_every_call" src tests` prints nothing before this task.

- [ ] **Step 1: Write the failing tests**

`tests/test_fw_dsp.c` starts the suite with the helpers' tests (the faults, the new wait kinds, the P2 rule) on a stub host that logs every host call in order; Tasks 3–5 add the routines' tests to it. `test_machine_hooks.c` gains the machine calls' tests and the bridge's DSP port, frame event and new waits. `diff_state.c` compares the added state; `test_diff_lts.c` compares the eight routines of spec §4.1 at every call (none of them is a replacement yet, so nothing changes) and grows `DIFF_MAX`. `test_fw_ser.c` passes `at`. The tests that use a `ROM_ISR_*` constant include `isr_addr.h`.

**`tests/diff_state.c`**: replace

```c
                 (unsigned)b->uart_tx_count, b->uart_tx_last);
        return false;
    }
    return true;
}
```

with

```c
                 (unsigned)b->uart_tx_count, b->uart_tx_last);
        return false;
    }
    if (a->p23_func != b->p23_func) {
        snprintf(why, n, "P23_FUNC: ROM %02X, C %02X", a->p23_func, b->p23_func);
        return false;
    }
    for (int t = 0; t < 3; t++) {
        if (a->timer_pre[t] != b->timer_pre[t] || a->timer_reload[t] != b->timer_reload[t] ||
            a->timer_running[t] != b->timer_running[t]) {
            snprintf(why, n, "timer %d prescaler/reload/running: ROM %02X/%02X/%d, C %02X/%02X/%d", t + 1,
                     a->timer_pre[t], a->timer_reload[t], a->timer_running[t], b->timer_pre[t], b->timer_reload[t],
                     b->timer_running[t]);
            return false;
        }
    }
    return true;
}

/* The DSP outputs (M4.6b spec 4.1): the port bytes, the reset pulses and the raw frame grouping, the frames
   produced. */
static bool dsp_same(const machine *a, const machine *b, char *why, size_t n)
{
    if (a->port_writes != b->port_writes || a->raw_pos != b->raw_pos ||
        memcmp(a->raw_buf, b->raw_buf, sizeof a->raw_buf) != 0) {
        snprintf(why, n, "DSP port writes/raw position: ROM %llu/%u, C %llu/%u (or the raw frame bytes)",
                 (unsigned long long)a->port_writes, a->raw_pos, (unsigned long long)b->port_writes, b->raw_pos);
        return false;
    }
    if (a->dsp_resets != b->dsp_resets || a->dsp_reset_level != b->dsp_reset_level) {
        snprintf(why, n, "DSP resets/reset level: ROM %llu/%d, C %llu/%d", (unsigned long long)a->dsp_resets,
                 a->dsp_reset_level, (unsigned long long)b->dsp_resets, b->dsp_reset_level);
        return false;
    }
    if (a->frames_produced != b->frames_produced) {
        snprintf(why, n, "frames produced: ROM %llu, C %llu", (unsigned long long)a->frames_produced,
                 (unsigned long long)b->frames_produced);
        return false;
    }
    return true;
}
```

**`tests/diff_state.c`**: replace

```c
        snprintf(why, n, "Z: ROM %d, C %d", (a->cpu.p & CPU740_Z) != 0, (b->cpu.p & CPU740_Z) != 0);
        return false;
    }
    if (!sfr_same(&a->sfr, &b->sfr, why, n))
        return false;
    for (uint32_t addr = 0; addr < MACHINE_RAM_SIZE; addr++) {
        if (addr >= M50734_SFR_FIRST && addr <= 0xFF)
```

with

```c
        snprintf(why, n, "Z: ROM %d, C %d", (a->cpu.p & CPU740_Z) != 0, (b->cpu.p & CPU740_Z) != 0);
        return false;
    }
    if (!sfr_same(&a->sfr, &b->sfr, why, n) || !dsp_same(a, b, why, n))
        return false;
    for (uint32_t addr = 0; addr < MACHINE_RAM_SIZE; addr++) {
        if (addr >= M50734_SFR_FIRST && addr <= 0xFF)
```

**`tests/diff_state.h`**: replace

```c
/* True when a (the ROM copy) and b (the C copy) agree on the exit PC and SP, the outputs declared (FW_OUT_* of
   engine/thunks.h), the SFR model state and all RAM except the SFR bytes and the dead stack below a's exit SP. The
   SFR state is these m50734 fields: port_latch[0-3] and port_dir[0-3] (P0-P3), icr1, icr3 and the enable bits
   (0, 2, 4) of icr2, uart_ctrl, uart_rx and uart_rx_full (UART_STATUS bit 0), uart_tx_count and uart_tx_last. icr2's
   request bits (1, 3, 5) and the timer counts are left out: the timers set them as cycles pass (m50734_tick), and the
   C takes none. Otherwise false, with the first difference in why. */
bool diff_same(const machine *a, const machine *b, uint8_t outputs, char *why, size_t n);

/* The serial routines no corpus text enters (M4.6a spec 4.4: the machine never raises the host line P0.2, so no
```

with

```c
/* True when a (the ROM copy) and b (the C copy) agree on the exit PC and SP, the outputs declared (FW_OUT_* of
   engine/thunks.h), the SFR model state and all RAM except the SFR bytes and the dead stack below a's exit SP. The
   SFR state is these m50734 fields: port_latch[0-3] and port_dir[0-3] (P0-P3), icr1, icr3 and the enable bits
   (0, 2, 4) of icr2, uart_ctrl, uart_rx and uart_rx_full (UART_STATUS bit 0), uart_tx_count and uart_tx_last,
   p23_func, and timer_pre, timer_reload and timer_running of the three timers (M4.6b spec 4.1). icr2's request bits
   (1, 3, 5) and timer_count are left out: the timers set and move them as cycles pass (m50734_tick), and the C takes
   none. The DSP outputs too: port_writes, raw_pos and raw_buf, dsp_resets and dsp_reset_level, frames_produced.
   Otherwise false, with the first difference in why. */
bool diff_same(const machine *a, const machine *b, uint8_t outputs, char *why, size_t n);

/* The serial routines no corpus text enters (M4.6a spec 4.4: the machine never raises the host line P0.2, so no
```

**`tests/suites.h`**: replace

```c
SUITE(suite_fw_par)
SUITE(suite_fw_frm)
SUITE(suite_fw_ser)
```

with

```c
SUITE(suite_fw_par)
SUITE(suite_fw_frm)
SUITE(suite_fw_ser)
SUITE(suite_fw_dsp)
```

**`tests/test_diff_lts.c`**: replace

```c
/* Live differential test of the ported routines (M4.0 spec section 5.1). Every corpus file runs on the ROM
   engine; at each entry of a ported routine (sampled) scratch A runs the ROM routine with interrupts held. If it
   returns, scratch B is cloned and runs the C thunk, and the two must agree on the exit PC and SP, the routine's
   declared outputs, the SFR model state and all RAM except the SFRs and the dead stack below the exit SP
   (diff_same, diff_state.h; M4.6a spec 4.1). An interrupt handler (a replacement marked isr) is compared the same
   way: entered through its vector, its ROM copy runs to its RTI (SP rises 3 above the hook's) and its thunk exits
   like RTI, and A, X and Y are compared too (the interrupted code reads all three; the row declares none). The
   routines no corpus text enters (diff_probed, M4.6a spec 4.4) print 0 calls: diff_serial's probes compare them.
```

with

```c
/* Live differential test of the ported routines (M4.0 spec section 5.1). Every corpus file runs on the ROM
   engine; at each entry of a ported routine (sampled, but every call of the speech-timing routines, k_every_call)
   scratch A runs the ROM routine with interrupts held. If it returns, scratch B is cloned and runs the C thunk, and
   the two must agree on the exit PC and SP, the routine's declared outputs, the SFR model state, the DSP outputs
   and all RAM except the SFRs and the dead stack below the exit SP (diff_same, diff_state.h; M4.6a spec 4.1, M4.6b
   spec 4.1). An interrupt handler (a replacement marked isr) is compared the same
   way: entered through its vector, its ROM copy runs to its RTI (SP rises 3 above the hook's) and its thunk exits
   like RTI, and A, X and Y are compared too (the interrupted code reads all three; the row declares none). The
   routines no corpus text enters (diff_probed, M4.6a spec 4.4) print 0 calls: diff_serial's probes compare them.
```

**`tests/test_diff_lts.c`**: replace

```c
#include "diff_state.h"

#define DIFF_FULL_CALLS 1000      /* every call up to this many per routine and corpus file ... */
#define DIFF_STRIDE     64        /* ... then every 64th */
#define DIFF_BUDGET     10000000ULL
#define DIFF_MAX        96

static const char *const k_corpus[] = { CORPUS_NAMES };
/* Extra text, for this test only (not goldens): words whose rules use the % & @ context symbols. */
```

with

```c
#include "diff_state.h"

#define DIFF_FULL_CALLS 1000      /* every call up to this many per routine and corpus file ... */
#define DIFF_STRIDE     64        /* ... then every 64th (but the routines of k_every_call) */
#define DIFF_BUDGET     10000000ULL
#define DIFF_MAX        104

static const char *const k_corpus[] = { CORPUS_NAMES };
/* Extra text, for this test only (not goldens): words whose rules use the % & @ context symbols. */
```

**`tests/test_diff_lts.c`**: replace

```c
    "Judges ruled the churches; loose rules, sugar, the nation's fixes, washing, rejoicing and lunches.",
};

typedef struct { const fw_replacement *r; unsigned calls; } observer;

static machine g_m, g_ref, g_c;
static observer g_obs[DIFF_MAX];
```

with

```c
    "Judges ruled the churches; loose rules, sugar, the nation's fixes, washing, rejoicing and lunches.",
};

/* The speech-timing routines that do not wait (M4.6b spec 4.1): compared at every call, the timer interrupt's too. */
static const char *const k_every_call[] = {
    "isr_set_volume", "isr_volume_half", "isr_par_ready", "isr_par_hold", "isr_dsp_reset", "isr_speech_stop",
    "isr_timer_start", "isr_timer_dsp",
};

typedef struct { const fw_replacement *r; unsigned calls; bool every; } observer;

static machine g_m, g_ref, g_c;
static observer g_obs[DIFF_MAX];
```

**`tests/test_diff_lts.c`**: replace

```c
{
    observer *o = user;
    unsigned call = o->calls++;
    if (call >= DIFF_FULL_CALLS && call % DIFF_STRIDE != 0)
        return false;
    machine_clone(&g_ref, m);
    machine_hold_irqs(&g_ref, true);
```

with

```c
{
    observer *o = user;
    unsigned call = o->calls++;
    if (!o->every && call >= DIFF_FULL_CALLS && call % DIFF_STRIDE != 0)
        return false;
    machine_clone(&g_ref, m);
    machine_hold_irqs(&g_ref, true);
```

**`tests/test_diff_lts.c`**: replace

```c
            continue;   /* diff_play's */
        g_obs[i].r = &fw_replacements[i];
        g_obs[i].calls = 0;   /* the full-call budget is per corpus file */
        CHECK(machine_hook(m, fw_replacements[i].addr, observe, &g_obs[i]));
    }
}
```

with

```c
            continue;   /* diff_play's */
        g_obs[i].r = &fw_replacements[i];
        g_obs[i].calls = 0;   /* the full-call budget is per corpus file */
        g_obs[i].every = false;
        for (size_t k = 0; k < sizeof k_every_call / sizeof k_every_call[0]; k++)
            g_obs[i].every |= !strcmp(fw_replacements[i].name, k_every_call[k]);
        CHECK(machine_hook(m, fw_replacements[i].addr, observe, &g_obs[i]));
    }
}
```

**`tests/test_diff_play.c`**: replace

```c
#include "engine/rom_engine.h"
#include "engine/thunks.h"
#include "firmware/par_addr.h"
#include "firmware/tn_convert.h"

#define PLAY_BUDGET        1000000000ULL   /* copy A: the longest call (seg_wrap_word, 1,774 frames) is 35.5 M cycles */
```

with

```c
#include "engine/rom_engine.h"
#include "engine/thunks.h"
#include "firmware/par_addr.h"
#include "firmware/isr_addr.h"
#include "firmware/tn_convert.h"

#define PLAY_BUDGET        1000000000ULL   /* copy A: the longest call (seg_wrap_word, 1,774 frames) is 35.5 M cycles */
```

**`tests/test_fw_cmd.c`**: replace

```c
#include "firmware/cmd_handlers.h"
#include "firmware/cmd_run.h"
#include "firmware/frm_build.h"
#include "firmware/par_addr.h"
#include "firmware/tn_convert.h"
#include "firmware/data/fw_data.h"
```

with

```c
#include "firmware/cmd_handlers.h"
#include "firmware/cmd_run.h"
#include "firmware/frm_build.h"
#include "firmware/isr_addr.h"
#include "firmware/par_addr.h"
#include "firmware/tn_convert.h"
#include "firmware/data/fw_data.h"
```

**Create `tests/test_fw_dsp.c`:**

```c
/* Unit checks of speech timing and the DSP (M4.6b spec 4.6): the host helpers it uses (fw_dsp_write, fw_frame_done,
   the wait kinds with their ROM loops, the P2 window rule), with a stub host that logs, in order, every SFR read and
   write, every wait (its kind and the ROM loop it stands for), every DSP port byte and every frame event. Each
   expected value is worked out from the dump of the routine (build/ghidra740/dump), as the comment beside it says. */
#include <string.h>
#include "test.h"
#include "firmware/fw.h"
#include "firmware/data/fw_data.h"

#define MAX_LOG 64
#define SFR_P2  0xF0
#define SFR_P3  0xEE

typedef enum { EV_READ, EV_WRITE, EV_WAIT, EV_DSP, EV_FRAME } ev_kind;
/* One host call: EV_READ/EV_WRITE an SFR (addr) and its value; EV_WAIT the ROM loop (addr) and the kind (value);
   EV_DSP the byte (value) at FW_DSP_DATA; EV_FRAME at FW_FRAME_EVENT. */
typedef struct { ev_kind kind; uint16_t addr; uint8_t value; } event;

static uint8_t g_ram[0x10000];   /* the firmware's RAM is $0000-$3FFF; 64 KiB so no index can run off the array */
static fw g_f;
static fw_host g_host;
static uint8_t g_sfr[0x100];     /* the stub's SFRs: a read returns g_sfr[addr], a write sets it */
static event g_log[MAX_LOG];
static unsigned g_nlog;
static bool g_wait_ok;           /* what the stub's wait returns */

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

static bool stub_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    (void)ctx;
    log_add(EV_WAIT, at, (uint8_t)kind);
    return g_wait_ok;
}

static void stub_dsp_write(void *ctx, uint8_t byte)
{
    (void)ctx;
    log_add(EV_DSP, FW_DSP_DATA, byte);
}

static void stub_frame_done(void *ctx)
{
    (void)ctx;
    log_add(EV_FRAME, FW_FRAME_EVENT, 0);
}

/* Zeroed RAM and SFRs, the stub host (no ROM call, no window call: speech timing makes neither), nothing logged. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    memset(g_sfr, 0, sizeof g_sfr);
    memset(&g_host, 0, sizeof g_host);
    g_host.ctx = &g_f;
    g_host.read_sfr = stub_read_sfr;
    g_host.write_sfr = stub_write_sfr;
    g_host.wait = stub_wait;
    g_host.dsp_write = stub_dsp_write;
    g_host.frame_done = stub_frame_done;
    g_nlog = 0;
    g_wait_ok = true;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
}

/* The log is exactly the n events of want. */
static void check_log(const event *want, unsigned n)
{
    CHECK_EQ(g_nlog, n);
    for (unsigned i = 0; i < n && i < g_nlog && i < MAX_LOG; i++) {
        CHECK_EQ(g_log[i].kind, want[i].kind);
        CHECK_EQ(g_log[i].addr, want[i].addr);
        CHECK_EQ(g_log[i].value, want[i].value);
    }
}

/* fw_dsp_write: one host write of the byte; no host, or a host without dsp_write, is a fault at DSP_DATA $8000 and
   nothing is written. */
static void test_dsp_write(void)
{
    static const event want[] = { { EV_DSP, FW_DSP_DATA, 0x5A }, { EV_DSP, FW_DSP_DATA, 0xA5 } };
    setup();
    fw_dsp_write(&g_f, 0x5A);
    fw_dsp_write(&g_f, 0xA5);
    check_log(want, 2);
    CHECK(!g_f.fault);

    setup();
    g_host.dsp_write = NULL;
    fw_dsp_write(&g_f, 0x5A);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0x8000);
    CHECK_EQ(g_nlog, 0);

    fw f;
    fw_init(&f, g_ram, fw_image_get(), NULL);
    fw_dsp_write(&f, 0x5A);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0x8000);
}

/* fw_frame_done: one frame event; no host, or a host without frame_done, is a fault at $B306. */
static void test_frame_done(void)
{
    static const event want[] = { { EV_FRAME, FW_FRAME_EVENT, 0 } };
    setup();
    fw_frame_done(&g_f);
    check_log(want, 1);
    CHECK(!g_f.fault);

    setup();
    g_host.frame_done = NULL;
    fw_frame_done(&g_f);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xB306);
    CHECK_EQ(g_nlog, 0);

    fw f;
    fw_init(&f, g_ram, fw_image_get(), NULL);
    fw_frame_done(&f);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xB306);
}

/* fw_wait with the new kinds (M4.6b spec 3.2): the host gets the kind and the ROM loop; a wait that fails, a host
   without wait or no host is a fault at that loop, whatever the kind. */
static void test_wait_kinds(void)
{
    static const event want[] = {
        { EV_WAIT, 0xB465, FW_WAIT_INTERRUPT },
        { EV_WAIT, 0xB372, FW_WAIT_TIMER2 },
        { EV_WAIT, 0xB37F, FW_WAIT_TIMER3 },
    };
    setup();
    fw_wait(&g_f, FW_WAIT_INTERRUPT, 0xB465);
    fw_wait(&g_f, FW_WAIT_TIMER2, FW_WAIT_TIMER2_LOOP);
    fw_wait(&g_f, FW_WAIT_TIMER3, FW_WAIT_TIMER3_LOOP);
    check_log(want, 3);
    CHECK(!g_f.fault);

    setup();
    g_wait_ok = false;
    fw_wait(&g_f, FW_WAIT_INTERRUPT, 0xB877);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xB877);

    setup();
    g_wait_ok = false;
    fw_wait(&g_f, FW_WAIT_TIMER3, FW_WAIT_TIMER3_LOOP);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xB37F);

    setup();
    g_host.wait = NULL;
    fw_wait(&g_f, FW_WAIT_TIMER2, FW_WAIT_TIMER2_LOOP);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xB372);
    CHECK_EQ(g_nlog, 0);

    fw f;
    fw_init(&f, g_ram, fw_image_get(), NULL);
    fw_wait(&f, FW_WAIT_INTERRUPT, 0xB8C9);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xB8C9);
}

/* The P2 rule (M4.6b spec 3.4): every write of P2 through the SFR helpers sets the window cache from bit 7 of the
   value written (STA/LDM, and the write of SEB/CLB, whatever its bit); a write of another SFR, or a P2 write that
   faults, leaves it. */
static void test_p2_window(void)
{
    setup();
    fw_wr_sfr8(&g_f, SFR_P2, 0x80);
    CHECK(g_f.low_window);
    fw_wr_sfr8(&g_f, SFR_P2, 0x70);
    CHECK(!g_f.low_window);
    fw_sfr_seb(&g_f, SFR_P2, 7);                  /* reads $70, writes $F0 */
    CHECK(g_f.low_window);
    CHECK_EQ(g_sfr[SFR_P2], 0xF0);
    fw_sfr_seb(&g_f, SFR_P2, 0);                  /* $F1: bit 7 still set */
    CHECK(g_f.low_window);
    fw_sfr_clb(&g_f, SFR_P2, 0);                  /* $F0 */
    CHECK(g_f.low_window);
    fw_sfr_clb(&g_f, SFR_P2, 7);                  /* $70 */
    CHECK(!g_f.low_window);
    fw_wr_sfr8(&g_f, SFR_P3, 0xFF);               /* not P2 */
    fw_sfr_seb(&g_f, SFR_P3, 7);
    CHECK(!g_f.low_window);
    CHECK_EQ(g_nlog, 13);                         /* six P2 writes, four of them after a read; P3 three */
    CHECK(!g_f.fault);

    setup();
    g_f.low_window = true;
    g_host.write_sfr = NULL;                      /* the write faults: nothing written, the cache kept */
    fw_wr_sfr8(&g_f, SFR_P2, 0x00);
    CHECK(g_f.fault);
    CHECK(g_f.low_window);
}

void suite_fw_dsp(void)
{
    CHECK(fw_image_get() != NULL);
    test_dsp_write();
    test_frame_done();
    test_wait_kinds();
    test_p2_window();
}
```

**`tests/test_fw_par.c`**: replace

```c
#include "firmware/cmd_play_event.h"
#include "firmware/frm_build.h"
#include "firmware/fw_ops.h"
#include "firmware/par_addr.h"
#include "firmware/par_boundary.h"
#include "firmware/par_play.h"
```

with

```c
#include "firmware/cmd_play_event.h"
#include "firmware/frm_build.h"
#include "firmware/fw_ops.h"
#include "firmware/isr_addr.h"
#include "firmware/par_addr.h"
#include "firmware/par_boundary.h"
#include "firmware/par_play.h"
```

**`tests/test_fw_pros.c`**: replace

```c
#include <string.h>
#include "test.h"
#include "engine/thunks.h"
#include "firmware/ph_expand.h"
#include "firmware/pros_addr.h"
#include "firmware/pros_decl.h"
```

with

```c
#include <string.h>
#include "test.h"
#include "engine/thunks.h"
#include "firmware/isr_addr.h"
#include "firmware/ph_expand.h"
#include "firmware/pros_addr.h"
#include "firmware/pros_decl.h"
```

**`tests/test_fw_ser.c`**: replace

```c
static log_entry g_log[MAX_LOG];
static unsigned g_nlog, g_ncalls;
static bool g_wait_ok;           /* what the stub's wait returns */
static uint8_t g_script_addr;    /* reads of this SFR take the next g_script value while any is left (a level that */
static uint8_t g_script[8];      /* changes between two reads), storing it in g_sfr; 0 = no script */
static unsigned g_script_n, g_script_pos;
```

with

```c
static log_entry g_log[MAX_LOG];
static unsigned g_nlog, g_ncalls;
static bool g_wait_ok;           /* what the stub's wait returns */
static uint16_t g_wait_at;       /* the ROM loop the last wait stood for */
static uint8_t g_script_addr;    /* reads of this SFR take the next g_script value while any is left (a level that */
static uint8_t g_script[8];      /* changes between two reads), storing it in g_sfr; 0 = no script */
static unsigned g_script_n, g_script_pos;
```

**`tests/test_fw_ser.c`**: replace

```c
    log_add(LOG_WRITE, addr, value);
}

static bool stub_wait(void *ctx, fw_wait_kind kind)
{
    (void)ctx;
    log_add(LOG_WAIT, (uint8_t)kind, 0);
    return g_wait_ok;
}
```

with

```c
    log_add(LOG_WRITE, addr, value);
}

static bool stub_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    (void)ctx;
    log_add(LOG_WAIT, (uint8_t)kind, 0);
    g_wait_at = at;
    return g_wait_ok;
}
```

**`tests/test_fw_ser.c`**: replace

```c
    g_nlog = 0;
    g_ncalls = 0;
    g_wait_ok = true;
    g_script_addr = 0;
    g_script_n = g_script_pos = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
```

with

```c
    g_nlog = 0;
    g_ncalls = 0;
    g_wait_ok = true;
    g_wait_at = 0;
    g_script_addr = 0;
    g_script_n = g_script_pos = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
```

**`tests/test_fw_ser.c`**: replace

```c
    CHECK_EQ(g_nlog, 0);                          /* not even the write */
}

/* fw_wait (M4.6a spec 3.3): one host wait of the kind; no host, a host without wait, or a wait that fails is a fault
   at the kind's ROM loop, ser_tx_wait $C500. */
static void test_wait(void)
{
    setup();
    fw_wait(&g_f, FW_WAIT_UART_TX);
    CHECK_EQ(g_nlog, 1);
    check_log(0, LOG_WAIT, FW_WAIT_UART_TX, 0);
    CHECK(!g_f.fault);

    setup();
    g_wait_ok = false;
    fw_wait(&g_f, FW_WAIT_UART_TX);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, FW_WAIT_UART_TX_LOOP);

    setup();
    g_host.wait = NULL;
    fw_wait(&g_f, FW_WAIT_UART_TX);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xC500);
    CHECK_EQ(g_nlog, 0);

    fw f;
    fw_init(&f, g_ram, fw_image_get(), NULL);
    fw_wait(&f, FW_WAIT_UART_TX);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xC500);
}
```

with

```c
    CHECK_EQ(g_nlog, 0);                          /* not even the write */
}

/* fw_wait (M4.6a spec 3.3, M4.6b spec 3.2): one host wait of the kind, with the ROM loop it stands for; no host, a
   host without wait, or a wait that fails is a fault at that loop, here ser_tx_wait $C500. */
static void test_wait(void)
{
    setup();
    fw_wait(&g_f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);
    CHECK_EQ(g_nlog, 1);
    check_log(0, LOG_WAIT, FW_WAIT_UART_TX, 0);
    CHECK_EQ(g_wait_at, 0xC500);
    CHECK(!g_f.fault);

    setup();
    g_wait_ok = false;
    fw_wait(&g_f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, FW_WAIT_UART_TX_LOOP);

    setup();
    g_host.wait = NULL;
    fw_wait(&g_f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xC500);
    CHECK_EQ(g_nlog, 0);

    fw f;
    fw_init(&f, g_ram, fw_image_get(), NULL);
    fw_wait(&f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xC500);
}
```

**`tests/test_fw_tn.c`**: replace

```c
#include <string.h>
#include "test.h"
#include "firmware/cmd_addr.h"
#include "firmware/par_addr.h"
#include "firmware/pros_addr.h"
#include "firmware/tn_convert.h"
```

with

```c
#include <string.h>
#include "test.h"
#include "firmware/cmd_addr.h"
#include "firmware/isr_addr.h"
#include "firmware/par_addr.h"
#include "firmware/pros_addr.h"
#include "firmware/tn_convert.h"
```

**`tests/test_golden_hybrid.c`**: replace

```c
#include "diff_state.h"
#include "engine/hybrid_engine.h"
#include "engine/thunks.h"
#include "firmware/tn_convert.h"
#include "render/render.h"
```

with

```c
#include "diff_state.h"
#include "engine/hybrid_engine.h"
#include "engine/thunks.h"
#include "firmware/isr_addr.h"
#include "firmware/tn_convert.h"
#include "render/render.h"
```

**`tests/test_machine_hooks.c`**: replace

```c
#include "rom_fixture.h"
#include "machine/machine.h"
#include "engine/thunks.h"
#include "firmware/par_addr.h"
#include "firmware/tn_convert.h"
```

with

```c
#include "rom_fixture.h"
#include "machine/machine.h"
#include "engine/thunks.h"
#include "firmware/isr_addr.h"
#include "firmware/par_addr.h"
#include "firmware/tn_convert.h"
```

**`tests/test_machine_hooks.c`**: replace

```c
    CHECK(!b.f.fault);
}

/* The firmware's hardware wait (M4.6a spec 3.3): FW_WAIT_UART_TX runs ser_tx_wait at $C500 (BBC1 UART_STATUS,$C500 /
   RTS, here in the synthetic ROM) on the emulated CPU through a ROM call: A, X, Y, SP and the PC as they were, the
   loop's cycles spent (BBC1 not taken, the transmitter being ready, then RTS). Once the bridge deadline has passed
   it fails, and fw_wait faults at $C500. */
static void test_host_wait(void)
{
    static fw_bridge b;
```

with

```c
    CHECK(!b.f.fault);
}

#define SYN_ISR   0xC600   /* the synthetic timer interrupt handler */
#define SYN_LOOP  0xC700   /* a synthetic BBC3 ICR2 loop */

/* Timer 2 running with a 400-cycle period (prescaler 0, reload 99: 4 x 1 x 100), its interrupt enabled (ICR2 bit 2)
   or not, written through the bus as the CPU's LDM writes them; its request bit 3 clear. */
static void timer2_on(machine *m, bool irq)
{
    m->bus.write(m->bus.ctx, 0xDE, 0);
    m->bus.write(m->bus.ctx, 0xDF, 99);
    m->bus.write(m->bus.ctx, M50734_ICR2, irq ? 0x04 : 0x00);
}

/* The firmware's hardware wait (M4.6a spec 3.3, M4.6b spec 3.2): FW_WAIT_UART_TX runs ser_tx_wait at $C500 (BBC1
   UART_STATUS,$C500 / RTS, here in the synthetic ROM) on the emulated CPU through a ROM call: A, X, Y, SP and the PC
   as they were, the loop's cycles spent (BBC1 not taken, the transmitter being ready, then RTS). Once the bridge
   deadline has passed it fails, and fw_wait faults at $C500. FW_WAIT_TIMER2 and FW_WAIT_TIMER3 run the synthetic
   loops at $B372 (BBC3 ICR2) and $B37F (BBC5 ICR2) until the timer's request bit; FW_WAIT_INTERRUPT returns once
   the timer interrupt's handler has run; each with the PC and SP as they were. A deadline that passes in the wait
   ends it with a fault at the loop given. */
static void test_host_wait(void)
{
    static fw_bridge b;
```

**`tests/test_machine_hooks.c`**: replace

```c
    g_m.cpu.x = 0x22;
    g_m.cpu.y = 0x33;
    uint64_t c0 = g_m.cpu.cycles;
    fw_wait(&b.f, FW_WAIT_UART_TX);
    CHECK(!b.f.fault);
    CHECK_EQ(g_m.cpu.a, 0x11);
    CHECK_EQ(g_m.cpu.x, 0x22);
```

with

```c
    g_m.cpu.x = 0x22;
    g_m.cpu.y = 0x33;
    uint64_t c0 = g_m.cpu.cycles;
    fw_wait(&b.f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);
    CHECK(!b.f.fault);
    CHECK_EQ(g_m.cpu.a, 0x11);
    CHECK_EQ(g_m.cpu.x, 0x22);
```

**`tests/test_machine_hooks.c`**: replace

```c
    CHECK_EQ(g_m.cpu.cycles - c0, 11);   /* BBC1 zp not taken (5), RTS (6) */

    b.deadline = g_m.cpu.cycles;         /* passed: the wait fails without running an instruction */
    fw_wait(&b.f, FW_WAIT_UART_TX);
    CHECK(b.f.fault);
    CHECK_EQ(b.f.fault_addr, FW_WAIT_UART_TX_LOOP);
    CHECK_EQ(g_m.cpu.cycles - c0, 11);
}

/* machine_return_interrupt is the CPU's RTI (M4.6a spec 3.2): from the same interrupt frame (the PC $9007, then P with
```

with

```c
    CHECK_EQ(g_m.cpu.cycles - c0, 11);   /* BBC1 zp not taken (5), RTS (6) */

    b.deadline = g_m.cpu.cycles;         /* passed: the wait fails without running an instruction */
    fw_wait(&b.f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);
    CHECK(b.f.fault);
    CHECK_EQ(b.f.fault_addr, FW_WAIT_UART_TX_LOOP);
    CHECK_EQ(g_m.cpu.cycles - c0, 11);

    static const uint8_t t2_loop[] = { 0x77, 0xFE, 0xFD };   /* $B372: BBC3 ICR2,$B372 */
    static const uint8_t t3_loop[] = { 0xB7, 0xFE, 0xFD };   /* $B37F: BBC5 ICR2,$B37F */
    static const uint8_t isr[] = { 0x7F, 0xFE, 0xE6, 0x20, 0x40 };
    put(FW_WAIT_TIMER2_LOOP, t2_loop, sizeof t2_loop);
    put(FW_WAIT_TIMER3_LOOP, t3_loop, sizeof t3_loop);
    put(SYN_ISR, isr, sizeof isr);
    vector(0xFFF8, SYN_ISR);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    timer2_on(&g_m, false);
    fw_wait(&b.f, FW_WAIT_TIMER2, FW_WAIT_TIMER2_LOOP);
    CHECK(!b.f.fault);
    CHECK(g_m.sfr.icr2 & 0x08);
    g_m.bus.write(g_m.bus.ctx, 0xE0, 0);         /* timer 3: prescaler 0, reload $28 (164 cycles) */
    g_m.bus.write(g_m.bus.ctx, 0xE1, 0x28);
    fw_wait(&b.f, FW_WAIT_TIMER3, FW_WAIT_TIMER3_LOOP);
    CHECK(!b.f.fault);
    CHECK(g_m.sfr.icr2 & 0x20);
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    g_m.bus.write(g_m.bus.ctx, M50734_ICR2, 0x04);   /* timer 2's interrupt alone */
    g_m.cpu.p = 0;
    fw_wait(&b.f, FW_WAIT_INTERRUPT, 0xB877);
    CHECK(!b.f.fault);
    CHECK_EQ(g_m.ram[0x20], 1);
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK_EQ(g_m.cpu.s, 0xFF);

    g_m.sfr.timer_running[1] = false;            /* no interrupt comes: the deadline ends the wait */
    b.deadline = g_m.cpu.cycles + 500;
    fw_wait(&b.f, FW_WAIT_INTERRUPT, 0xB877);
    CHECK(b.f.fault);
    CHECK_EQ(b.f.fault_addr, 0xB877);
    CHECK(g_m.cpu.cycles >= b.deadline);
    CHECK(g_m.cpu.cycles < b.deadline + 8);
}

/* machine_return_interrupt is the CPU's RTI (M4.6a spec 3.2): from the same interrupt frame (the PC $9007, then P with
```

**`tests/test_machine_hooks.c`**: replace

```c
    CHECK_EQ(g_c.cpu.s, 0xFF);
    CHECK_EQ(g_m.cpu.p, CPU740_N | CPU740_Z | CPU740_C | CPU740_B);
    CHECK_EQ(g_c.cpu.p, g_m.cpu.p);
}

/* The bridge's cycle deadline (thunks.h): a ROM call from C gets at most the cycles left before b->deadline, and
```

with

```c
    CHECK_EQ(g_c.cpu.s, 0xFF);
    CHECK_EQ(g_m.cpu.p, CPU740_N | CPU740_Z | CPU740_C | CPU740_B);
    CHECK_EQ(g_c.cpu.p, g_m.cpu.p);
}

static unsigned g_frames;
static uint8_t g_frame[APOLLO_FRAME_BYTES];

static void on_frame(void *user, const uint8_t frame[APOLLO_FRAME_BYTES])
{
    (void)user;
    g_frames++;
    memcpy(g_frame, frame, APOLLO_FRAME_BYTES);
}

/* machine_capture_frame (M4.6b spec 3.3) is the capture the step loop makes at $B306: the 20 bytes at the $2C/$2D
   pointer to the frame callback, frames_produced counted. */
static void test_capture_frame(void)
{
    static const uint8_t nop[] = { 0xEA };
    load_main(0xC100);
    put(APOLLO_SWAP_PC, nop, sizeof nop);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    for (int i = 0; i < APOLLO_FRAME_BYTES; i++)
        g_m.ram[0x16B9 + i] = (uint8_t)(0x40 + i);
    g_m.ram[APOLLO_PRODUCER_PTR] = 0xB9;
    g_m.ram[APOLLO_PRODUCER_PTR + 1] = 0x16;
    machine_clone(&g_c, &g_m);
    machine_set_outputs(&g_m, on_frame, NULL);
    machine_set_outputs(&g_c, on_frame, NULL);
    g_frames = 0;
    g_m.cpu.pc = APOLLO_SWAP_PC;
    CHECK(machine_step(&g_m));                   /* the step at $B306 captures, then runs the NOP */
    CHECK_EQ(g_frames, 1);
    CHECK_EQ(g_m.frames_produced, 1);
    uint8_t first[APOLLO_FRAME_BYTES];
    memcpy(first, g_frame, sizeof first);
    machine_capture_frame(&g_c);
    CHECK_EQ(g_frames, 2);
    CHECK_EQ(g_c.frames_produced, 1);
    CHECK(memcmp(first, g_frame, sizeof first) == 0);
    CHECK_EQ(first[0], 0x40);
    CHECK_EQ(first[19], 0x53);
    CHECK_EQ(g_c.cpu.pc, 0x9000);                /* nothing executed */
}

/* machine_run_loop (M4.6b spec 3.3): BBC3 ICR2 at $C700 runs with I set until timer 2 sets its request bit, so the
   timer's interrupt, enabled with I clear before, is not taken (SP unchanged); then the PC and P are as they were.
   A loop that cannot end fails after its budget, the PC and P put back all the same. */
static void test_run_loop(void)
{
    static const uint8_t loop[] = { 0x77, 0xFE, 0xFD, 0xEA };   /* $C700: BBC3 ICR2,$C700 / NOP */
    load_main(0xC100);
    put(SYN_LOOP, loop, sizeof loop);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    timer2_on(&g_m, true);
    g_m.cpu.p = CPU740_C;
    uint64_t c0 = g_m.cpu.cycles;
    CHECK(machine_run_loop(&g_m, SYN_LOOP, SYN_LOOP + 3, 10000));
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK_EQ(g_m.cpu.p, CPU740_C);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    CHECK(g_m.sfr.icr2 & 0x08);
    CHECK(g_m.cpu.cycles - c0 >= 400);
    CHECK(g_m.cpu.cycles - c0 < 413);            /* BBC3 taken 7 cycles a pass, not taken 5 */

    g_m.sfr.timer_running[1] = false;            /* nothing sets the bit any more */
    g_m.sfr.icr2 = 0;
    c0 = g_m.cpu.cycles;
    CHECK(!machine_run_loop(&g_m, SYN_LOOP, SYN_LOOP + 3, 1000));
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK_EQ(g_m.cpu.p, CPU740_C);
    CHECK(g_m.cpu.cycles - c0 >= 1000);
    CHECK_EQ(g_m.fault, MFAULT_NONE);
}

/* A replacement of the synthetic handler: counts, clears the request and exits like RTI. */
static bool hook_isr_c(machine *m, void *user)
{
    (void)user;
    g_calls++;
    m->sfr.icr2 &= (uint8_t)~0x08;
    machine_return_interrupt(m);
    return true;
}

/* machine_wait_interrupt (M4.6b spec 3.3): as a WIT at $9000, the CPU idles until timer 2's interrupt; its handler
   ($C600: CLB3 ICR2 / INC $20 / RTI) runs and the call returns with the PC and SP back, the CPU no longer waiting.
   A byte waiting in the serial queue is not delivered (only the idle WIT $C602 delivers). With the handler replaced
   by a hook the same, the ROM handler not run. Without an interrupt it fails after its budget; with I set it fails
   at once (the interrupt could never be taken). */
static void test_wait_interrupt(void)
{
    static const uint8_t isr[] = { 0x7F, 0xFE, 0xE6, 0x20, 0x40 };   /* CLB3 ICR2 / INC $20 / RTI */
    static const uint8_t byte[] = { 0x41 };
    load_main(0xC100);
    put(SYN_ISR, isr, sizeof isr);
    vector(0xFFF8, SYN_ISR);
    CHECK(machine_init(&g_m, g_rom, sizeof g_rom, NULL));
    CHECK(machine_feed_serial(&g_m, byte, sizeof byte));
    timer2_on(&g_m, true);
    g_m.cpu.p = 0;
    uint64_t c0 = g_m.cpu.cycles;
    CHECK(machine_wait_interrupt(&g_m, 10000));
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK_EQ(g_m.cpu.s, 0xFF);
    CHECK(!g_m.cpu.waiting);
    CHECK_EQ(g_m.ram[0x20], 1);
    CHECK(g_m.cpu.cycles - c0 >= 400);
    CHECK(!g_m.sfr.uart_rx_full);                /* no byte delivered */
    CHECK(g_m.q_head != g_m.q_tail);

    g_calls = 0;
    CHECK(machine_hook(&g_m, SYN_ISR, hook_isr_c, NULL));
    CHECK(machine_wait_interrupt(&g_m, 10000));
    CHECK_EQ(g_calls, 1);
    CHECK_EQ(g_m.ram[0x20], 1);                  /* the ROM handler did not run */
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK_EQ(g_m.cpu.s, 0xFF);

    g_m.sfr.timer_running[1] = false;            /* no interrupt comes */
    g_m.sfr.icr2 = 0x04;
    c0 = g_m.cpu.cycles;
    CHECK(!machine_wait_interrupt(&g_m, 1000));
    CHECK(!g_m.cpu.waiting);
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK(g_m.cpu.cycles - c0 >= 1000);
    CHECK(g_m.cpu.cycles - c0 < 1008);

    g_m.sfr.icr2 = 0x0C;                         /* requested and enabled, but masked */
    g_m.cpu.p = CPU740_I;
    c0 = g_m.cpu.cycles;
    CHECK(!machine_wait_interrupt(&g_m, 1000));
    CHECK_EQ(g_m.cpu.cycles, c0);
    CHECK(!g_m.cpu.waiting);
    CHECK_EQ(g_m.cpu.pc, 0x9000);
    CHECK_EQ(g_calls, 1);
    CHECK_EQ(g_m.fault, MFAULT_NONE);
}

/* The bridge's DSP port and frame event (M4.6b spec 3.1): fw_dsp_write is a bus write of $8000 (counted, grouped
   into the raw frame), fw_frame_done the machine's frame capture. */
static void test_host_dsp(void)
{
    static fw_bridge b;
    load_main(0xC100);
    CHECK(fw_bridge_init(&b, &g_m));
    machine_set_outputs(&g_m, on_frame, NULL);
    g_frames = 0;
    fw_dsp_write(&b.f, 0x12);
    fw_dsp_write(&b.f, 0x34);
    CHECK_EQ(g_m.port_writes, 2);
    CHECK_EQ(g_m.raw_pos, 2);
    CHECK_EQ(g_m.raw_buf[0], 0x12);
    CHECK_EQ(g_m.raw_buf[1], 0x34);
    g_m.ram[0x1700] = 0x77;
    g_m.ram[APOLLO_PRODUCER_PTR] = 0x00;
    g_m.ram[APOLLO_PRODUCER_PTR + 1] = 0x17;
    fw_frame_done(&b.f);
    CHECK_EQ(g_frames, 1);
    CHECK_EQ(g_m.frames_produced, 1);
    CHECK_EQ(g_frame[0], 0x77);
    CHECK(!b.f.fault);
    CHECK_EQ(g_m.unmodelled_count, 0);
}

/* The bridge's cycle deadline (thunks.h): a ROM call from C gets at most the cycles left before b->deadline, and
```

**`tests/test_machine_hooks.c`**: replace

```c
    test_host_sfr();
    test_host_wait();
    test_return_interrupt();
    test_bridge_deadline();
    test_waits_thunk_deadline();
}
```

with

```c
    test_host_sfr();
    test_host_wait();
    test_return_interrupt();
    test_capture_frame();
    test_run_loop();
    test_wait_interrupt();
    test_host_dsp();
    test_bridge_deadline();
    test_waits_thunk_deadline();
}
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw_dsp"`
Expected: the build stops with compile errors (the proof run, first lines):

```
tests\test_diff_play.c(38): fatal error C1083: Cannot open include file: 'firmware/isr_addr.h': No such file or directory
tests\test_fw_cmd.c(12): fatal error C1083: Cannot open include file: 'firmware/isr_addr.h': No such file or directory
tests\test_fw_dsp.c(58): error C2065: 'FW_DSP_DATA': undeclared identifier
tests\test_fw_dsp.c(64): error C2065: 'FW_FRAME_EVENT': undeclared identifier
...
tests\test_fw_dsp.c(153): error C2197: 'void fw_wait(fw *,fw_wait_kind)': too many arguments for call
...
tests\test_fw_par.c(14): fatal error C1083: Cannot open include file: 'firmware/isr_addr.h': No such file or directory
tests\test_fw_pros.c(8): fatal error C1083: Cannot open include file: 'firmware/isr_addr.h': No such file or directory
tests\test_fw_ser.c(84): error C2220: the following warning is treated as an error
...
```

(and the same missing `isr_addr.h` in `test_fw_tn.c`, `test_golden_hybrid.c` and `test_machine_hooks.c`).

- [ ] **Step 3: Implement**

The host interface and its helpers (`fw.h`, `fw.c`), the `at` of the two UART waits (`ser.c`), `isr_addr.h` with the constants moved into it and the includes that follow them, the three machine calls, and the hybrid host's `wait`, `dsp_write` and `frame_done`.

**`src/engine/thunks.c`**: replace

```c
    m->bus.write(m->bus.ctx, addr, value);
}

/* The firmware's hardware wait (M4.6a spec 3.3): the kind's ROM loop runs on the emulated CPU as host code (for
   FW_WAIT_UART_TX, ser_tx_wait $C500: BBC1 UART_STATUS,$C500 / RTS), through host_rom_call with the machine's own
   A, X and Y, so with interrupts live, the ROM's cycles and the bridge deadline. $C500 is never hooked. */
static bool host_wait(void *ctx, fw_wait_kind kind)
{
    fw_bridge *b = ctx;
    if (kind != FW_WAIT_UART_TX)
        return false;
    fw_regs r = { b->m->cpu.a, b->m->cpu.x, b->m->cpu.y, 0 };
    return host_rom_call(b, FW_WAIT_UART_TX_LOOP, &r, FW_ROM_CALL_BUDGET) == FW_CALL_RETURNED;
}

bool fw_bridge_init(fw_bridge *b, machine *m)
```

with

```c
    m->bus.write(m->bus.ctx, addr, value);
}

/* The firmware's hardware wait (M4.6a spec 3.3, M4.6b spec 3.2), on the emulated CPU, within the bridge deadline:
   - FW_WAIT_UART_TX: ser_tx_wait $C500 (BBC1 UART_STATUS,$C500 / RTS) runs as host code through host_rom_call, with
     the machine's own A, X and Y, so with interrupts live and the ROM's cycles. $C500 is never hooked.
   - FW_WAIT_INTERRUPT: the CPU idles as at a WIT at the hook's PC (never the idle WIT $C602, so no serial byte is
     delivered) until an interrupt has been handled, its handler ROM or C (machine_wait_interrupt).
   - FW_WAIT_TIMER2, FW_WAIT_TIMER3: isr_timer_start's loops $B372 and $B37F run with I set, as inside its PHP/SEI
     (machine_run_loop).
   at is the C's report of the loop; the hybrid host runs the kind's own. The window is read back after the wait,
   as after a ROM call: an interrupt handler ran. */
static bool host_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    fw_bridge *b = ctx;
    machine *m = b->m;
    (void)at;
    if (kind == FW_WAIT_UART_TX) {
        fw_regs r = { m->cpu.a, m->cpu.x, m->cpu.y, 0 };
        return host_rom_call(b, FW_WAIT_UART_TX_LOOP, &r, FW_ROM_CALL_BUDGET) == FW_CALL_RETURNED;
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
    switch (kind) {
    case FW_WAIT_INTERRUPT:
        ok = machine_wait_interrupt(m, budget);
        break;
    case FW_WAIT_TIMER2:
        ok = machine_run_loop(m, FW_WAIT_TIMER2_LOOP, FW_WAIT_TIMER2_EXIT, budget);
        break;
    case FW_WAIT_TIMER3:
        ok = machine_run_loop(m, FW_WAIT_TIMER3_LOOP, FW_WAIT_TIMER3_EXIT, budget);
        break;
    default:
        return false;
    }
    b->f.low_window = window_on(m);
    return ok;
}

/* STA DSP_DATA (M4.6b spec 3.1): the byte written to $8000 through the bus, as the CPU's STA writes it, so the
   machine counts it (port_writes) and groups it into the raw frame stream as before. */
static void host_dsp_write(void *ctx, uint8_t byte)
{
    machine *m = ((fw_bridge *)ctx)->m;
    m->bus.write(m->bus.ctx, APOLLO_DSP_PORT, byte);
}

/* The frame event (M4.6b spec 3.1): the capture the machine makes when the ROM reaches $B306. The C timer interrupt
   never executes $B306, so no frame is captured twice. */
static void host_frame_done(void *ctx)
{
    machine_capture_frame(((fw_bridge *)ctx)->m);
}

bool fw_bridge_init(fw_bridge *b, machine *m)
```

**`src/engine/thunks.c`**: replace

```c
    b->host.read_sfr = host_read_sfr;
    b->host.write_sfr = host_write_sfr;
    b->host.wait = host_wait;
    fw_init(&b->f, m->ram, img, &b->host);
    return img != NULL;
}
```

with

```c
    b->host.read_sfr = host_read_sfr;
    b->host.write_sfr = host_write_sfr;
    b->host.wait = host_wait;
    b->host.dsp_write = host_dsp_write;
    b->host.frame_done = host_frame_done;
    fw_init(&b->f, m->ram, img, &b->host);
    return img != NULL;
}
```

**`src/firmware/cmd_addr.h`**: replace

```c
#define ROM_CMD_EV_SET_VOICING    0xBCEA   /* cmd_set_voicing */
#define ROM_CMD_EV_SET_VOLUME     0xBCED   /* cmd_set_volume */

/* ROM routines the C calls through fw_rom_call (isr_par_ready: ROM_ISR_PAR_READY in tn_convert.h). The serial
   routines and the immediate handlers are C since M4.6a; their ROM addresses are in ser_addr.h. */
#define ROM_CMD_RUN_NEXT          0xA893   /* dispatch cmd_handlers[cmd_next] (the dispatcher's fallback); X kept */
```

with

```c
#define ROM_CMD_EV_SET_VOICING    0xBCEA   /* cmd_set_voicing */
#define ROM_CMD_EV_SET_VOLUME     0xBCED   /* cmd_set_volume */

/* ROM routines the C calls through fw_rom_call (isr_par_ready: ROM_ISR_PAR_READY in isr_addr.h). The serial
   routines and the immediate handlers are C since M4.6a; their ROM addresses are in ser_addr.h. */
#define ROM_CMD_RUN_NEXT          0xA893   /* dispatch cmd_handlers[cmd_next] (the dispatcher's fallback); X kept */
```

**`src/firmware/cmd_play_event.c`**: replace

```c
#include "cmd_play_event.h"
#include "cmd_addr.h"
#include "fw_ops.h"
#include "par_addr.h"
#include "data/fw_data.h"
```

with

```c
#include "cmd_play_event.h"
#include "cmd_addr.h"
#include "fw_ops.h"
#include "isr_addr.h"
#include "par_addr.h"
#include "data/fw_data.h"
```

**`src/firmware/cmd_scan.c`**: replace

```c
#include "cmd_scan.h"
#include "cmd_match.h"
#include "tn_convert.h"
#include "data/fw_data.h"

void cmd_scan_clause(fw *f)
```

with

```c
#include "cmd_scan.h"
#include "cmd_match.h"
#include "isr_addr.h"
#include "data/fw_data.h"

void cmd_scan_clause(fw *f)
```

**`src/firmware/fw.c`**: replace

```c
#include "fw.h"

void fw_init(fw *f, uint8_t *ram, const fw_image *img, const fw_host *host)
{
```

with

```c
#include "fw.h"

#define SFR_P2         0xF0
#define P2_LOW_WINDOW  0x80   /* P2 bit 7: the lower-ROM window */

void fw_init(fw *f, uint8_t *ram, const fw_image *img, const fw_host *host)
{
```

**`src/firmware/fw.c`**: replace

```c
    return 0xFF;
}

void fw_wr_sfr8(fw *f, uint8_t addr, uint8_t v)
{
    if (addr >= 0xDA && f->host && f->host->write_sfr) {
        f->host->write_sfr(f->host->ctx, addr, v);
        return;
    }
    fault(f, addr);
```

with

```c
    return 0xFF;
}

/* A value written to P2: the window cache follows its bit 7 (M4.6b spec 3.4). */
static void p2_written(fw *f, uint8_t addr, uint8_t v)
{
    if (addr == SFR_P2)
        f->low_window = (v & P2_LOW_WINDOW) != 0;
}

void fw_wr_sfr8(fw *f, uint8_t addr, uint8_t v)
{
    if (addr >= 0xDA && f->host && f->host->write_sfr) {
        f->host->write_sfr(f->host->ctx, addr, v);
        p2_written(f, addr, v);
        return;
    }
    fault(f, addr);
```

**`src/firmware/fw.c`**: replace

```c
static void sfr_rmw(fw *f, uint8_t addr, uint8_t set, uint8_t clear)
{
    if (addr >= 0xDA && f->host && f->host->read_sfr && f->host->write_sfr) {
        uint8_t v = f->host->read_sfr(f->host->ctx, addr);
        f->host->write_sfr(f->host->ctx, addr, (uint8_t)((v | set) & ~clear));
        return;
    }
    fault(f, addr);
```

with

```c
static void sfr_rmw(fw *f, uint8_t addr, uint8_t set, uint8_t clear)
{
    if (addr >= 0xDA && f->host && f->host->read_sfr && f->host->write_sfr) {
        uint8_t v = (uint8_t)((f->host->read_sfr(f->host->ctx, addr) | set) & ~clear);
        f->host->write_sfr(f->host->ctx, addr, v);
        p2_written(f, addr, v);
        return;
    }
    fault(f, addr);
```

**`src/firmware/fw.c`**: replace

```c
void fw_sfr_clb(fw *f, uint8_t addr, unsigned bit) { sfr_rmw(f, addr, 0, (uint8_t)(1u << bit)); }
bool fw_sfr_bit(fw *f, uint8_t addr, unsigned bit) { return ((fw_sfr8(f, addr) >> bit) & 1) != 0; }

void fw_wait(fw *f, fw_wait_kind kind)
{
    if (f->host && f->host->wait && f->host->wait(f->host->ctx, kind))
        return;
    fault(f, FW_WAIT_UART_TX_LOOP);   /* the only kind (M4.6b and M4.6c add theirs) */
}

void fw_fault(fw *f, uint16_t addr) { fault(f, addr); }
```

with

```c
void fw_sfr_clb(fw *f, uint8_t addr, unsigned bit) { sfr_rmw(f, addr, 0, (uint8_t)(1u << bit)); }
bool fw_sfr_bit(fw *f, uint8_t addr, unsigned bit) { return ((fw_sfr8(f, addr) >> bit) & 1) != 0; }

void fw_wait(fw *f, fw_wait_kind kind, uint16_t at)
{
    if (f->host && f->host->wait && f->host->wait(f->host->ctx, kind, at))
        return;
    fault(f, at);
}

void fw_dsp_write(fw *f, uint8_t v)
{
    if (f->host && f->host->dsp_write) {
        f->host->dsp_write(f->host->ctx, v);
        return;
    }
    fault(f, FW_DSP_DATA);
}

void fw_frame_done(fw *f)
{
    if (f->host && f->host->frame_done) {
        f->host->frame_done(f->host->ctx);
        return;
    }
    fault(f, FW_FRAME_EVENT);
}

void fw_fault(fw *f, uint16_t addr) { fault(f, addr); }
```

**`src/firmware/fw.h`**: replace

```c
   stack past the call's own frame (bank_enter, M4.2 spec 3), so it can never return. */
typedef enum { FW_CALL_FAILED = 0, FW_CALL_RETURNED, FW_CALL_UNWOUND } fw_call_result;

/* The hardware waits (M4.6a spec 3.3): one kind per ROM wait loop. The hybrid host runs the loop on the emulated
   CPU; a native host waits natively. */
typedef enum fw_wait_kind {
    FW_WAIT_UART_TX = 0,   /* until the UART can transmit: ser_tx_wait, BBC1 UART_STATUS until bit 1 is set */
} fw_wait_kind;
#define FW_WAIT_UART_TX_LOOP  0xC500   /* ser_tx_wait: the ROM loop of FW_WAIT_UART_TX (BBC1 $E7,$C500 / RTS) */

/* The machine or native host. In hybrid runs it is the emulator (src/engine/thunks.c). */
typedef struct fw_host {
```

with

```c
   stack past the call's own frame (bank_enter, M4.2 spec 3), so it can never return. */
typedef enum { FW_CALL_FAILED = 0, FW_CALL_RETURNED, FW_CALL_UNWOUND } fw_call_result;

/* The hardware waits (M4.6a spec 3.3, M4.6b spec 3.2): one kind per ROM wait loop. The hybrid host runs the loop on
   the emulated CPU, or idles it until an interrupt has been handled; a native host waits natively. */
typedef enum fw_wait_kind {
    FW_WAIT_UART_TX = 0,   /* until the UART can transmit: ser_tx_wait, BBC1 UART_STATUS until bit 1 is set */
    FW_WAIT_INTERRUPT,     /* a polling loop on a flag an interrupt sets: until an interrupt has been handled */
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
                                          faults */

/* The machine or native host. In hybrid runs it is the emulator (src/engine/thunks.c). */
typedef struct fw_host {
```

**`src/firmware/fw.h`**: replace

```c
    uint8_t (*read_sfr)(void *ctx, uint8_t addr);
    /* Writes an SFR ($DA-$FF) as the CPU's write would (M4.6a spec 3.1). */
    void (*write_sfr)(void *ctx, uint8_t addr, uint8_t value);
    /* Waits for the hardware (M4.6a spec 3.3). False when the wait did not end (a fault). */
    bool (*wait)(void *ctx, fw_wait_kind kind);
} fw_host;

/* Converted ROM data at its original addresses (built by src/firmware/data/fw_data.c). */
```

with

```c
    uint8_t (*read_sfr)(void *ctx, uint8_t addr);
    /* Writes an SFR ($DA-$FF) as the CPU's write would (M4.6a spec 3.1). */
    void (*write_sfr)(void *ctx, uint8_t addr, uint8_t value);
    /* Waits for the hardware (M4.6a spec 3.3, M4.6b spec 3.2); at is the ROM loop the wait stands for. False when
       the wait did not end (a fault). */
    bool (*wait)(void *ctx, fw_wait_kind kind, uint16_t at);
    /* Writes a byte to the DSP port DSP_DATA $8000, as isr_timer_dsp's STA DSP_DATA does (M4.6b spec 3.1). */
    void (*dsp_write)(void *ctx, uint8_t byte);
    /* The frame event: the C timer interrupt is where the ROM's reaches $B306 (isr_swap_buffers), the frame at
       isr_fill_ptr complete (M4.6b spec 3.1). */
    void (*frame_done)(void *ctx);
} fw_host;

/* Converted ROM data at its original addresses (built by src/firmware/data/fw_data.c). */
```

**`src/firmware/fw.h`**: replace

```c
   $DA, a fault: it longjmps to f->trap when set, else reads $FF. */
uint8_t fw_sfr8(fw *f, uint8_t addr);
/* STA/LDM to an SFR: v written through the host. Without a host or write_sfr, or below $DA, a fault (nothing
   written). */
void fw_wr_sfr8(fw *f, uint8_t addr, uint8_t v);
/* SEB bit,addr / CLB bit,addr on an SFR: a read, then a write of the value with the bit set or cleared, as the CPU
   does them (cpu740.c, M_SEB/M_CLB): a port reads its pins, so an input pin's level reaches the latch. Without a
   host, read_sfr or write_sfr, or below $DA, a fault (nothing read or written). */
void fw_sfr_seb(fw *f, uint8_t addr, unsigned bit);
void fw_sfr_clb(fw *f, uint8_t addr, unsigned bit);
/* BBS bit,addr / BBC bit,addr on an SFR: the bit of one read (fw_sfr8, its faults included). */
bool fw_sfr_bit(fw *f, uint8_t addr, unsigned bit);
/* Waits for the hardware through the host (JSR ser_tx_wait for FW_WAIT_UART_TX). Without a host or wait, or when the
   wait fails, a fault at the kind's ROM loop (FW_WAIT_UART_TX_LOOP). */
void fw_wait(fw *f, fw_wait_kind kind);
/* A fault at addr where the ROM would never return (a loop with no exit), like a fault of fw_rom8: it sets f->fault
   (keeping the first fault_addr), then longjmps to f->trap when set, else returns. run_thunk relies on the flag being
   set after the longjmp. */
```

with

```c
   $DA, a fault: it longjmps to f->trap when set, else reads $FF. */
uint8_t fw_sfr8(fw *f, uint8_t addr);
/* STA/LDM to an SFR: v written through the host. Without a host or write_sfr, or below $DA, a fault (nothing
   written). A write of P2 ($F0) sets f->low_window from bit 7 of v (M4.6b spec 3.4). */
void fw_wr_sfr8(fw *f, uint8_t addr, uint8_t v);
/* SEB bit,addr / CLB bit,addr on an SFR: a read, then a write of the value with the bit set or cleared, as the CPU
   does them (cpu740.c, M_SEB/M_CLB): a port reads its pins, so an input pin's level reaches the latch. Without a
   host, read_sfr or write_sfr, or below $DA, a fault (nothing read or written). On P2 ($F0) f->low_window follows
   bit 7 of the value written (M4.6b spec 3.4). */
void fw_sfr_seb(fw *f, uint8_t addr, unsigned bit);
void fw_sfr_clb(fw *f, uint8_t addr, unsigned bit);
/* BBS bit,addr / BBC bit,addr on an SFR: the bit of one read (fw_sfr8, its faults included). */
bool fw_sfr_bit(fw *f, uint8_t addr, unsigned bit);
/* Waits for the hardware through the host: the polling loop or wait loop at the ROM address at (JSR ser_tx_wait
   $C500 for FW_WAIT_UART_TX; the polling loop's own address for FW_WAIT_INTERRUPT; $B372 and $B37F for the timer
   waits). Without a host or wait, or when the wait fails, a fault at at. */
void fw_wait(fw *f, fw_wait_kind kind, uint16_t at);
/* STA DSP_DATA: v written to the DSP port through the host. Without a host or dsp_write, a fault at FW_DSP_DATA. */
void fw_dsp_write(fw *f, uint8_t v);
/* The frame event through the host (M4.6b spec 3.1). Without a host or frame_done, a fault at FW_FRAME_EVENT. */
void fw_frame_done(fw *f);
/* A fault at addr where the ROM would never return (a loop with no exit), like a fault of fw_rom8: it sets f->fault
   (keeping the first fault_addr), then longjmps to f->trap when set, else returns. run_thunk relies on the flag being
   set after the longjmp. */
```

**`src/firmware/fw_ops.h`**: replace

```c
/* Small steps the ported routines share (M4.4a spec 2.6), and the two shift routines (M4.4b spec 2.2). */

#include "fw.h"
#include "tn_convert.h"   /* ROM_ISR_PAR_READY */

/* JSR isr_par_ready with the registers the ROM has at the JSR (it preserves A, X and Y). */
static inline void fw_par_ready(fw *f, fw_regs r) { fw_rom_call(f, ROM_ISR_PAR_READY, &r, FW_ROM_CALL_BUDGET); }
```

with

```c
/* Small steps the ported routines share (M4.4a spec 2.6), and the two shift routines (M4.4b spec 2.2). */

#include "fw.h"
#include "isr_addr.h"

/* JSR isr_par_ready with the registers the ROM has at the JSR (it preserves A, X and Y). */
static inline void fw_par_ready(fw *f, fw_regs r) { fw_rom_call(f, ROM_ISR_PAR_READY, &r, FW_ROM_CALL_BUDGET); }
```

**Create `src/firmware/isr_addr.h`:**

```c
#ifndef FW_ISR_ADDR_H
#define FW_ISR_ADDR_H
/* ROM code addresses of speech timing, the DSP, the volume and the parallel handshake (M4.6b spec 2.1), checked
   against build/ghidra740/dump and the JSR/JMP operands and vectors in the ROM image. A caller in parentheses stays
   ROM (M4.6c). */
#define ROM_ISR_SET_VOLUME     0xACE8   /* called by boot_loop_play $B2C4, $B2D2, isr_volume_half $B46F,
                                           cmd_ev_set_volume $BCEF (and boot_reset $A642) */
#define ROM_ISR_TIMER_DSP      0xB2D9   /* INT2: entered through vec_int2_timers $FFF8 (timer 3, the DSP tick) */
#define ROM_ISR_SWAP_BUFFERS   0xB306   /* isr_timer_dsp's frame boundary: the frame event (FW_FRAME_EVENT) */
#define ROM_ISR_SPEECH_START   0xB323   /* called by boot_loop_play $B2B9 */
#define ROM_ISR_WAIT_FRAMES    0xB356   /* called by isr_speech_start $B352 */
#define ROM_ISR_TIMER_START    0xB35D   /* called by isr_speech_start $B34D */
#define ROM_ISR_DSP_RESET      0xB38B   /* called by isr_speech_start $B34A, isr_speech_stop $B39E */
#define ROM_ISR_SPEECH_STOP    0xB398   /* called by boot_loop_play $B2D5 */
#define ROM_ISR_WAIT_FRAME     0xB465   /* called by boot_loop_play $B2BF, $B2CD, isr_wait_frames $B356, par_play
                                           $B8D2 */
#define ROM_ISR_VOLUME_HALF    0xB46C   /* called by boot_loop_play $B2BC, $B2CA */
#define ROM_ISR_PAR_READY      0xC5D6   /* parallel mode: ready for the next host byte; A, X, Y preserved. Called by
                                           tn_convert_clause $B1F6, cmd_scan_clause $AD09, par_play $B89F, $B8AB,
                                           $B8C9, $B8E6, $B8F7, $B908, pros_append_segment $CA3B, pros_mark_stress
                                           $CB1F, pros_mark_phrases $CF28, pros_phrase_gen $D155 (and boot_reset
                                           $A672, boot_get_clause $A6D0, $A6DC, $A6FC, boot_queue_string $C62B) */
#define ROM_ISR_PAR_HOLD       0xC5DE   /* called by boot_loop_play $B2B3 (and boot_reset $A66F, boot_get_clause
                                           $A6E4, boot_queue_string $C617) */

#endif
```

**`src/firmware/par_addr.h`**: replace

```c
#ifndef FW_PAR_ADDR_H
#define FW_PAR_ADDR_H
/* ROM code addresses of the playback set-up (M4.4a spec 2.2), the frame loop (M4.4b spec 2.2) and the frame
   builder (M4.5 spec 2.2), and of the interrupt and serial routines they call (M4.4a 2.4, M4.4b 2.4), checked
   against build/ghidra740/dump and the JSR/JMP operands in the ROM image. A caller in parentheses stays ROM. */
#define ROM_BOOT_LOOP_PLAY         0xA7D4   /* called by tn_convert_clause $B204 (and boot_reset $A687,
                                               boot_selftest_sound $A7BA, cmd_say_phonetic $A87C) */
#define ROM_ISR_SET_VOLUME         0xACE8   /* M4.6; called by boot_loop_play $B2C4, $B2D2, cmd_ev_set_volume $BCEF
                                               (and boot_reset $A642, isr_volume_half $B46F) */
#define ROM_ISR_SPEECH_START       0xB323   /* M4.6; called by boot_loop_play $B2B9 */
#define ROM_ISR_SPEECH_STOP        0xB398   /* M4.6; called by boot_loop_play $B2D5 */
#define ROM_PAR_SETUP_VOICE        0xB3A4   /* called by cmd_apply_pitch $A845; the C cmd_apply_pitch calls the C */
#define ROM_FRM_BUILD_REST_FRAME   0xB41D   /* called by par_setup_voice $B404 */
#define ROM_FRM_COPY_REST_FRAME    0xB445   /* called by par_setup_voice $B407, $B416, par_play $B8DA */
#define ROM_ISR_WAIT_FRAME         0xB465   /* M4.6; called by boot_loop_play $B2BF, $B2CD, par_play $B8D2 (and
                                               isr_wait_frames $B356) */
#define ROM_ISR_VOLUME_HALF        0xB46C   /* M4.6; called by boot_loop_play $B2BC, $B2CA */
#define ROM_PAR_PLAY               0xB877   /* called by boot_loop_play $B2C7; re-entered by its own JMPs $B892,
                                               $B8AE, $B914 */
#define ROM_FRM_SNAPSHOT           0xB917   /* called by par_play $B8DD, frm_finish_frame $B9BA */
```

with

```c
#ifndef FW_PAR_ADDR_H
#define FW_PAR_ADDR_H
/* ROM code addresses of the playback set-up (M4.4a spec 2.2), the frame loop (M4.4b spec 2.2) and the frame
   builder (M4.5 spec 2.2), checked against build/ghidra740/dump and the JSR/JMP operands in the ROM image (the
   interrupt routines they call are in isr_addr.h, M4.6b). A caller in parentheses stays ROM. */
#define ROM_BOOT_LOOP_PLAY         0xA7D4   /* called by tn_convert_clause $B204 (and boot_reset $A687,
                                               boot_selftest_sound $A7BA, cmd_say_phonetic $A87C) */
#define ROM_PAR_SETUP_VOICE        0xB3A4   /* called by cmd_apply_pitch $A845; the C cmd_apply_pitch calls the C */
#define ROM_FRM_BUILD_REST_FRAME   0xB41D   /* called by par_setup_voice $B404 */
#define ROM_FRM_COPY_REST_FRAME    0xB445   /* called by par_setup_voice $B407, $B416, par_play $B8DA */
#define ROM_PAR_PLAY               0xB877   /* called by boot_loop_play $B2C7; re-entered by its own JMPs $B892,
                                               $B8AE, $B914 */
#define ROM_FRM_SNAPSHOT           0xB917   /* called by par_play $B8DD, frm_finish_frame $B9BA */
```

**`src/firmware/par_addr.h`**: replace

```c
#define ROM_PAR_TRACK_END          0xC26C   /* called by par_track_overlap $C1F8 */
#define ROM_PAR_SCALE_DURATION     0xC29C   /* called by par_boundary_cur_dom $BF03, $BF0E; par_boundary_next_dom
                                               $BF48, $BF53 */
#define ROM_ISR_PAR_HOLD           0xC5DE   /* M4.6; called by boot_loop_play $B2B3 (and boot_reset $A66F,
                                               boot_get_clause $A6E4, boot_queue_string $C617) */
#define ROM_UTIL_SHL4              0xC656   /* called by cmd_apply_pitch $A835, $A83F; cmd_set_formant $A952;
                                               cmd_set_mark_space $A9AD; cmd_hex_byte $B455 (all C: util_shl4 in
                                               fw_ops.h) */
```

with

```c
#define ROM_PAR_TRACK_END          0xC26C   /* called by par_track_overlap $C1F8 */
#define ROM_PAR_SCALE_DURATION     0xC29C   /* called by par_boundary_cur_dom $BF03, $BF0E; par_boundary_next_dom
                                               $BF48, $BF53 */
#define ROM_UTIL_SHL4              0xC656   /* called by cmd_apply_pitch $A835, $A83F; cmd_set_formant $A952;
                                               cmd_set_mark_space $A9AD; cmd_hex_byte $B455 (all C: util_shl4 in
                                               fw_ops.h) */
```

**`src/firmware/par_play.c`**: replace

```c
#include "cmd_play_event.h"
#include "frm_build.h"
#include "fw_ops.h"
#include "par_addr.h"
#include "par_start.h"
#include "par_track.h"
```

with

```c
#include "cmd_play_event.h"
#include "frm_build.h"
#include "fw_ops.h"
#include "isr_addr.h"
#include "par_addr.h"
#include "par_start.h"
#include "par_track.h"
```

**`src/firmware/pros_addr.h`**: replace

```c
#define FW_PROS_ADDR_H
/* ROM code addresses of the segment generator (M4.3a spec 2.2) and of the stress and phrase passes before it
   (M4.3b spec 2.2), checked against build/ghidra740/dump and the JSR/JMP operands in the ROM image.
   isr_par_ready, the one ROM routine they call, is ROM_ISR_PAR_READY in tn_convert.h. */
#define ROM_PH_APPEND_PHRASE_PAUSE  0xC9B8   /* exits with JMP ROM_PROS_SEG_TAIL */
#define ROM_PH_APPEND_PAUSE         0xCA15
#define ROM_PROS_APPEND_SEGMENT     0xCA3B
```

with

```c
#define FW_PROS_ADDR_H
/* ROM code addresses of the segment generator (M4.3a spec 2.2) and of the stress and phrase passes before it
   (M4.3b spec 2.2), checked against build/ghidra740/dump and the JSR/JMP operands in the ROM image.
   isr_par_ready, the one ROM routine they call, is ROM_ISR_PAR_READY in isr_addr.h. */
#define ROM_PH_APPEND_PHRASE_PAUSE  0xC9B8   /* exits with JMP ROM_PROS_SEG_TAIL */
#define ROM_PH_APPEND_PAUSE         0xCA15
#define ROM_PROS_APPEND_SEGMENT     0xCA3B
```

**`src/firmware/pros_seg.c`**: replace

```c
#include "pros_seg.h"
#include "fw_ops.h"
#include "tn_convert.h"

/* STA (ptr),Y: the 16-bit pointer plus y. The pointers start at the arrays ($0200, $0600, $0A00, $0E00) and only
   move up one page per 256 segments; a clause makes far fewer than 1,280 segments (at most 3 per phonetic symbol
```

with

```c
#include "pros_seg.h"
#include "fw_ops.h"
#include "isr_addr.h"

/* STA (ptr),Y: the 16-bit pointer plus y. The pointers start at the arrays ($0200, $0600, $0A00, $0E00) and only
   move up one page per 256 segments; a clause makes far fewer than 1,280 segments (at most 3 per phonetic symbol
```

**`src/firmware/ser.c`**: replace

```c
void ser_tx_send(fw *f, fw_regs *r)
{
    fw_wait(f, FW_WAIT_UART_TX);                                 /* $C505 JSR ser_tx_wait (inside PHP ... PLP) */
    fw_wr_sfr8(f, SFR_UART_TX, r->a);                            /* $C508 STA UART_TX */
}
```

with

```c
void ser_tx_send(fw *f, fw_regs *r)
{
    fw_wait(f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);           /* $C505 JSR ser_tx_wait (inside PHP ... PLP) */
    fw_wr_sfr8(f, SFR_UART_TX, r->a);                            /* $C508 STA UART_TX */
}
```

**`src/firmware/ser.c`**: replace

```c
    uint8_t pending = fw_rd8(f, SER_TX_PENDING);                 /* LDA ser_tx_pending */
    fw_wr8(f, SER_STATUS_DUE, pending);
    if (pending == 0) {                                          /* BNE: the whole byte */
        fw_wait(f, FW_WAIT_UART_TX);                             /* $C596 JSR ser_tx_wait */
        if (fw_sfr_bit(f, SFR_P0, P0_HOST_LINE))                 /* PHP, SEI, BBC2 P0 */
            fw_sfr_seb(f, SFR_P0, P0_BUSY);                      /* SEB5 P0 */
    }
```

with

```c
    uint8_t pending = fw_rd8(f, SER_TX_PENDING);                 /* LDA ser_tx_pending */
    fw_wr8(f, SER_STATUS_DUE, pending);
    if (pending == 0) {                                          /* BNE: the whole byte */
        fw_wait(f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);       /* $C596 JSR ser_tx_wait */
        if (fw_sfr_bit(f, SFR_P0, P0_HOST_LINE))                 /* PHP, SEI, BBC2 P0 */
            fw_sfr_seb(f, SFR_P0, P0_BUSY);                      /* SEB5 P0 */
    }
```

**`src/firmware/tn_convert.c`**: replace

```c
#include "tn_convert.h"
#include "cmd_run.h"
#include "lts.h"
#include "par_play.h"
#include "pros_make.h"
```

with

```c
#include "tn_convert.h"
#include "cmd_run.h"
#include "isr_addr.h"
#include "lts.h"
#include "par_play.h"
#include "pros_make.h"
```

**`src/firmware/tn_convert.h`**: replace

```c
/* The clause-to-phonetic-text driver of text normalisation (ROM $B1E6), decompiled (M4.1 spec section 2.2). */
#include "fw.h"

/* Routines still in ROM that tn_convert_clause calls */
#define ROM_ISR_PAR_READY       0xC5D6   /* parallel mode: ready for the next host byte; A, X, Y preserved */
#define TN_PHONETIC_FLUSH       0xFA     /* tn_phon_len at which tn_phonetic is spoken mid-clause */

/* $B1E6: convert cmd_text (boot_clause_len characters) to phonetic text in tn_phonetic: per character the
```

with

```c
/* The clause-to-phonetic-text driver of text normalisation (ROM $B1E6), decompiled (M4.1 spec section 2.2). */
#include "fw.h"

#define TN_PHONETIC_FLUSH       0xFA     /* tn_phon_len at which tn_phonetic is spoken mid-clause */

/* $B1E6: convert cmd_text (boot_clause_len characters) to phonetic text in tn_phonetic: per character the
```

**`src/machine/machine.c`**: replace

```c
    return true;
}

void machine_clone(machine *dst, const machine *src)
{
    memcpy(dst, src, sizeof *dst);
```

with

```c
    return true;
}

void machine_capture_frame(machine *m)
{
    capture_frame(m);
}

bool machine_wait_interrupt(machine *m, uint64_t max_cycles)
{
    cpu740 *c = &m->cpu;
    uint16_t pc = c->pc;
    uint8_t s = c->s;
    uint64_t end = c->cycles + max_cycles;
    c->waiting = true;   /* a WIT at pc: at_idle is false there, so maybe_deliver delivers nothing */
    while (c->cycles < end) {
        prepare(m);
        if (c->waiting && c->irq_lines && (c->p & CPU740_I))
            break;       /* the CPU would wake and run on at pc without taking the interrupt */
        if (!exec(m))
            break;
        if (!c->waiting && c->pc == pc && c->s == s)
            return true;   /* the handler's RTI is back at pc */
    }
    c->waiting = false;
    return false;
}

bool machine_run_loop(machine *m, uint16_t loop, uint16_t exit, uint64_t max_cycles)
{
    cpu740 *c = &m->cpu;
    uint16_t pc = c->pc;
    uint8_t p = c->p;
    uint64_t end = c->cycles + max_cycles;
    bool ok = true;
    c->p |= CPU740_I;
    c->pc = loop;
    while (ok && c->pc != exit)
        ok = c->cycles < end && machine_step(m);
    c->pc = pc;
    c->p = p;
    return ok;
}

void machine_clone(machine *dst, const machine *src)
{
    memcpy(dst, src, sizeof *dst);
```

**`src/machine/machine.h`**: replace

```c
/* Runs the routine the PC has just entered (by JSR) until the stack pointer rises above its value now, i.e.
   the routine returned, also two levels up. False on a fault or when max_cycles pass first. */
bool machine_finish_routine(machine *m, uint64_t max_cycles);
/* Copies src's whole state into dst for a differential run, without hooks, outputs, coverage or the hold. */
void machine_clone(machine *dst, const machine *src);
/* The shared address mapper (M4.0 spec section 2.2) behind bus reads, machine_peek and the hook check: where a
```

with

```c
/* Runs the routine the PC has just entered (by JSR) until the stack pointer rises above its value now, i.e.
   the routine returned, also two levels up. False on a fault or when max_cycles pass first. */
bool machine_finish_routine(machine *m, uint64_t max_cycles);
/* The frame capture the step loop makes at APOLLO_SWAP_PC ($B306): the 20 bytes at the APOLLO_PRODUCER_PTR
   pointer, frames_produced counted, the frame callback called (M4.6b spec 3.3: the hybrid host's frame event). */
void machine_capture_frame(machine *m);
/* Waits as a WIT at the current PC would (M4.6b spec 3.3): the CPU idles until an IRQ line wakes it, takes the
   interrupt (its handler ROM or a hooked replacement) and returns from it. True once the PC and SP are back where
   they were. The PC is a hook's, never the idle WIT's ($C602): no serial byte is delivered. False when max_cycles
   pass first, on a fault, or when a line wakes the CPU with I set (the interrupt could never be taken); the CPU is
   then no longer waiting. */
bool machine_wait_interrupt(machine *m, uint64_t max_cycles);
/* Runs the ROM loop at loop with I set (inside the caller's PHP/SEI ... PLP; M4.6b spec 3.3) until the PC reaches
   exit, then puts the PC and P back as they were. No stack frame. False when max_cycles pass first or on a fault
   (the PC and P put back all the same). */
bool machine_run_loop(machine *m, uint16_t loop, uint16_t exit, uint64_t max_cycles);
/* Copies src's whole state into dst for a differential run, without hooks, outputs, coverage or the hold. */
void machine_clone(machine *dst, const machine *src);
/* The shared address mapper (M4.0 spec section 2.2) behind bus reads, machine_peek and the hook check: where a
```

- [ ] **Step 4: Run the unit tests**

Run: `cmd //c ".\build.bat test fw_dsp"`, then `build/apollo2_tests.exe machine_hooks` and `build/apollo2_tests.exe fw`.
Expected: the build passes with no warnings (the firmware syntax check included), and

```
suite_fw_dsp                 ok
1 suite(s), 0 failure(s), 0 skip(s)
suite_machine_hooks          ok
1 suite(s), 0 failure(s), 0 skip(s)
```

and the `fw` run: `suite_fw`, `suite_fw_lts`, `suite_fw_tn`, `suite_fw_cmd`, `suite_fw_pros`, `suite_fw_par`, `suite_fw_frm`, `suite_fw_ser`, `suite_fw_dsp` all `ok`, `9 suite(s), 0 failure(s), 0 skip(s)`.

- [ ] **Step 5: Run `diff_lts` (the comparison change alone)**

Run: `build/apollo2_tests.exe diff_lts`
Expected: the 87 lines `<name> N calls checked` exactly as at 06d42c5 (the proof run diffed them: `cmd_apply_speaker 107`, `cmd_run_due 5595`, `cmd_scan_clause 234`, …, `pros_declination_step 422`, `ph_lengthen_count 1269`), `1 mid-clause flush calls skipped (golden_hybrid covers them)`, `2 unwound calls compared`, `suite_diff_lts ok`, 0 mismatches (17 s in the proof run). A mismatch here is the comparison of the timer or DSP state finding something an existing routine does differently: find it in the dump; never drop a field from the comparison.

- [ ] **Step 6: Run the full suite**

Run: `cmd //c ".\build.bat test"` (in the background; it takes about 10 minutes)
Expected:

- `37 suite(s), 0 failure(s), 0 skip(s)`, no warnings, the firmware syntax check passing (556 s in the proof run);
- `firmware_data`: `48512 converted bytes compared with the ROM, 0 differ`;
- `diff_lts`: the 87 lines of Step 5, `1 mid-clause flush calls skipped (golden_hybrid covers them)`, `2 unwound calls compared`;
- `diff_play` unchanged: `self-check (ROM against ROM, hello): par_play 1 calls, boot_loop_play 2 calls, 2 injected; $B8A5 reached 2, $B89C reached 2`, `2 routines wait on the timer interrupt`, `boot_loop_play 179 calls checked, 48 with both injections`, `par_play 126 calls checked, 48 with both injections`, `$B8A5 (abort) reached in 96 calls, $B89C (reply due) in 96`;
- `diff_serial` unchanged: the 25 probe lines twice, every target reached at every call (`queries`: 23 or 9 of each; corpus and boot: 2,649 or 124 of each);
- `golden_hybrid`: `48 corpus files x 90 modes (all, then each routine alone)` (no new replacement yet), every golden unchanged;
- `suite_golden`: `timer scale 1/4: 105 frames, 11789152 CPU cycles`, `timer scale 3/1: 105 frames, 1286784 CPU cycles`, `hybrid timer scale 1/4: 105 frames, 11697300 CPU cycles`, `hybrid timer scale 3/1: 105 frames, 994543 CPU cycles` (as before: no speech-timing routine is C yet);
- `suite_synth`: `corpus peak plain 20522 (-4.1 dBFS)`, `corpus peak b11 16038 (-6.2 dBFS)`.

- [ ] **Step 7: Commit**

```bash
git add src/firmware/isr_addr.h src/firmware/fw.h src/firmware/fw.c src/firmware/ser.c src/firmware/fw_ops.h src/firmware/par_addr.h src/firmware/tn_convert.h src/firmware/tn_convert.c src/firmware/par_play.c src/firmware/cmd_play_event.c src/firmware/cmd_scan.c src/firmware/pros_seg.c src/firmware/cmd_addr.h src/firmware/pros_addr.h src/machine/machine.h src/machine/machine.c src/engine/thunks.c tests/test_fw_dsp.c tests/diff_state.h tests/diff_state.c tests/suites.h tests/test_diff_lts.c tests/test_diff_play.c tests/test_fw_cmd.c tests/test_fw_par.c tests/test_fw_pros.c tests/test_fw_ser.c tests/test_fw_tn.c tests/test_golden_hybrid.c tests/test_machine_hooks.c
git commit -m "M4.6b infrastructure: the DSP port and the frame event through the host (fw_dsp_write, fw_frame_done), fw_wait with the ROM loop's address and the kinds FW_WAIT_INTERRUPT/TIMER2/TIMER3, machine_capture_frame, machine_wait_interrupt and machine_run_loop, the P2 window rule, diff_lts comparing the timer and DSP-output state (DIFF_MAX 104), isr_addr.h, the suite suite_fw_dsp (M4.6b 3, 4.1, 4.6)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 2: The tests before the C: `diff_play`'s injections at the frame event and the timer interrupt, `diff_serial`'s probes of the parallel handshake and the repeated frame, both proved ROM against ROM (spec §4.2, §4.3, §4.8 item 2)

**ROM facts checked / what was verified while proving the plan** (`rom_B2D9_isr_timer_dsp.txt`, `rom_B877_par_play.txt`, `rom_C5D6_isr_par_ready.txt`, `rom_C5DE_isr_par_hold.txt`, `rom_AC22_boot_init_ports.txt`; `cpu740.c`, `m50734.c`, `machine.c`; a throwaway branch probe, Task 5 Step 5):
- **How `diff_play` sees the frame event in both copies.** It stays the machine's frame callback (`machine_set_outputs`), which the ROM timer interrupt reaches at $B306 (the step loop's capture) and the C one (Task 5) through `frame_done`, which the bridge turns into `machine_capture_frame`: the same callback, the same snapshot. Copy B's thunk runs on a bridge bound to copy B (`g_bridge`), and a hook on copy B at `isr_timer_dsp` $B2D9 runs the C handler on that same bridge once the table has it (`fw_replacement_find("isr_timer_dsp")`; Task 5), so the C timer interrupt is nested in the watched routine's thunk exactly as the hybrid engine nests it. Until then the hook lets the ROM handler run.
- **The abort injection cannot be made at the frame event itself.** Set at the frame event (`ser_abort` bit 0 in the frame callback of the second swap), the ROM sees it before or after the frame flag depending on where the interrupt returns: returning to $B8A8 (`BBS0 isr_frame_flag`, right after the abort test at $B8A2) it builds one more frame first. The C waits for the interrupt only at its back edge ($B877), so it always sees the mute first. Tried while proving the plan, on this task's copy B (C `par_play` polling through ROM `isr_par_ready` calls) and again on Task 3's (polling through `FW_WAIT_INTERRUPT`): **4 mismatches** each time (`phonetic`, `hypermode`, `phonetic_stress`: `par_play ... (ser_abort injected): swap 2: frame byte 1: ROM 16, C 20`; `fast_effect`: frame byte 9). So the mute is made due at the second frame event and set on entering the timer interrupt `ABORT_TICKS` = 2 ticks later (400 cycles: the tick is 200): by then the ROM has passed its flag test and is building the frame (`par_step_tracks` and `frm_finish_frame` take thousands of cycles), and the C, which builds it in no time, is waiting at its back edge; both end at the next abort test with that one frame built. `ABORT_TICKS` = 1 also gave 0 mismatches (on the corpus the ROM is always past its flag test one tick after the event); 2 keeps a margin. A frame event during `boot_loop_play`'s ramp (`isr_wait_frame`, the second swap of a `boot_loop_play` call) makes `par_play` end at its first abort test in both copies.
- **`isr_par_ready` and `isr_par_hold`** test `BBC0 boot_dip` ($C5D7, $C5DF): parallel mode is bit 0. In it `isr_par_ready` does `SEB3 P0` ($C5DA) and `isr_par_hold` `CLB3 P0`, `CLB1 ICR1` ($C5E4, $C5E6), both keeping A (`PHA` ... `PLA` at $C5EA). The probes call them by `machine_rom_call` at the `isr_int1` point (2,649 entries on the corpus and the boot run, 23 on `queries`) with `boot_dip` bit 0 set and P0's parallel directions ($08, `boot_init_ports` $AC29); for `isr_par_hold` an `_INT1` request (ICR1 bit 1) is pending, so the `CLB1` is seen. Targets: $C5D7 → $C5DA and $C5DF → $C5E2 (the branch not taken).
- **The timer interrupt's repeated frame** ($B300 `BBC0 isr_frame_flag` not taken, $B303 `JMP $B31F`) is never taken on the corpus (the branch probe: taken 25,208 times, not taken 0): the main program always clears the flag within a frame. The probe enters `isr_timer_dsp` from the `isr_int1` point as M4.6a's INT4 probes do: the PC set to the word at `vec_int2_timers` $FFF8 ($B2D9) and timer 3's request and enable bits (ICR2 bits 5 and 4) set, which is what `take_irq` would have done had it taken INT2 there (the pushed PC and P and I are the same for every line); then the state of a frame boundary, `isr_tick_div` 1, `isr_words_left` 1, `isr_word_ofs` 18, with `isr_frame_flag` bit 0 set. At the idle point `isr_play_ptr` still points into `isr_frame_a`/`isr_frame_b` ($16A5/$16B9, the boot plays the self-test sound first), so the word read is RAM. Each copy runs to the `RTI` (`machine_finish_routine`), interrupts held.
- **The other branches of the 11 routines** are seen both ways on the corpus, except `isr_wait_frames`' `BNE` $B35A taken (its one caller passes X = 1): no probe can reach it, since the copies hold interrupts and a frame never comes; Task 4 pins it with `test_wait_frames_two`.
- **The `isr_timer_dsp` probe's routine and the two handshake probes' routines are not C yet**: `report()`'s check that every probe compares the C (M4.6a) exempts exactly them, listed in `k_not_c_yet`; Task 3 removes `isr_par_ready` and `isr_par_hold` from the list, Task 5 the list. The self-checks prove the probes and the injections ROM against ROM: every probe and injection compared at every point, every target reached at every call, 0 mismatches (Step 2).

**Files:**
- Modify: `tests/test_diff_play.c`, `tests/test_diff_serial.c`

**Interfaces:**
- Consumes: Task 1's `ROM_ISR_TIMER_DSP`, `ROM_ISR_PAR_READY`, `ROM_ISR_PAR_HOLD` (`isr_addr.h`), `fw_replacement_find`, `fw_bridge_init` (`thunks.h`), `machine_set_outputs`, `machine_hook`, `machine_clone`, `machine_finish_routine`, `machine_rom_call`.
- Produces: `diff_play`'s `on_timer` hook and `ABORT_TICKS`, the recorder's `isr` and `bridge` (Task 5 makes copy B's timer interrupt C through them); `diff_serial`'s probes `timer_repeat`, `par_ready`, `par_hold` and `k_not_c_yet` (Tasks 3 and 5 shrink it).

- [ ] **Step 1: Write the tests**

**`tests/test_diff_play.c`**: replace

```c
   counts: the hook on the JSR par_play at $B2C7 (boot_play_body) arms the next $B877 entry. boot_loop_play is
   entered only by JSR. In a corpus file the boot's own call (from boot_selftest_sound $A7BA) and the par_play inside
   it are the boot run's; they are checked there only.
   The receive interrupt's two effects that no corpus text reaches are injected into both copies, on the first call
   of each routine in each run whose ROM copy swapped the buffers at least twice. A reply due: ser_status_due = 1 in
   the frame callback of the second swap (ser_tx_service from $B89C at every pass after it). A mute: ser_abort bit 0
   set at the first isr_par_ready entry ($C5D6, ROM in both copies) after the second swap at which isr_frame_flag
   bit 0 is clear: the frame built after that swap has been finished and the flag cleared, so that par_play's next
   abort test ($B8A2, right after its JSR isr_par_ready at $B89F) ends it (the end path from $B8A5). Not at the swap
   itself: there the ROM sees the mute before or after the frame flag depending on the instruction the interrupt
   lands on (at $B8A8, between the two tests, it builds one more frame), which the C, taking no time between its
   tests, cannot reproduce. Nor at frm_finish_frame ($B9B5), the point used until M4.5: once par_play calls the C
   frame builder (M4.5), its C copy never executes $B9B5. A hook on copy A counts the calls that executed
   $B8A5 and $B89C. No copy runs past the routine's return: after a ser_status_due injection the ROM never reaches
   idle again (boot_get_clause waits for the host at $A6D4).
   A self-check runs the ROM routine in copy B too, on hello, both injections included: it proves the comparison
```

with

```c
   counts: the hook on the JSR par_play at $B2C7 (boot_play_body) arms the next $B877 entry. boot_loop_play is
   entered only by JSR. In a corpus file the boot's own call (from boot_selftest_sound $A7BA) and the par_play inside
   it are the boot run's; they are checked there only.
   The receive interrupt's two effects that no corpus text reaches are injected into both copies at the frame event
   (M4.6b spec 4.2), on the first call of each routine in each run whose ROM copy swapped the buffers at least
   twice. The frame event is the machine's frame callback in both copies: the ROM timer interrupt reaches $B306, the
   C one (isr_timer_dsp, hooked in copy B once it is a replacement) calls frame_done, which the bridge turns into
   machine_capture_frame. A reply due: ser_status_due = 1 at the second frame event (ser_tx_service from $B89C at
   every pass after it). A mute: due at the second frame event, ser_abort bit 0 is set on entering the timer
   interrupt ABORT_TICKS times after it ($B2D9, as the CPU enters it: a hook in both copies, which in copy B runs the
   C handler after the injection once it is a replacement). By then the ROM has seen the frame flag and is building
   the frame (thousands of cycles) and the C, which builds it in no time, is waiting at its back edge, so both end
   at par_play's next abort test ($B8A2) with that one frame built (the end path from $B8A5). Not at the frame event
   itself: there the ROM sees the mute before or after the frame flag depending on the instruction the interrupt
   lands on (at $B8A8, between the two tests, it builds one more frame), which the C, taking no time between its
   tests, cannot reproduce. Nor at an isr_par_ready entry ($C5D6), the point used until M4.6b: the C par_play calls
   the C isr_par_ready (M4.6b), so its copy never executes $C5D6. A hook on copy A counts the calls that executed
   $B8A5 and $B89C. No copy runs past the routine's return: after a ser_status_due injection the ROM never reaches
   idle again (boot_get_clause waits for the host at $A6D4).
   A self-check runs the ROM routine in copy B too, on hello, both injections included: it proves the comparison
```

**`tests/test_diff_play.c`**: replace

```c
#include "engine/thunks.h"
#include "firmware/par_addr.h"
#include "firmware/isr_addr.h"
#include "firmware/tn_convert.h"

#define PLAY_BUDGET        1000000000ULL   /* copy A: the longest call (seg_wrap_word, 1,774 frames) is 35.5 M cycles */
#define PLAY_CALL_SITE     0xB2C7          /* boot_play_body: JSR par_play */
```

with

```c
#include "engine/thunks.h"
#include "firmware/par_addr.h"
#include "firmware/isr_addr.h"

#define PLAY_BUDGET        1000000000ULL   /* copy A: the longest call (seg_wrap_word, 1,774 frames) is 35.5 M cycles */
#define PLAY_CALL_SITE     0xB2C7          /* boot_play_body: JSR par_play */
```

**`tests/test_diff_play.c`**: replace

```c
#define PLAY_ABORT         0xB8A5          /* par_play: JMP $B8D2 (ser_abort bit 0: the end) */
#define BOOT_SOUND_RETURN  0xA7BD          /* after boot_selftest_sound's JSR boot_loop_play at $A7BA */
#define INJECT_SWAP        1               /* the second swap of a call */
#define WATCH_MAX          2

static const char *const k_corpus[] = { CORPUS_NAMES };
```

with

```c
#define PLAY_ABORT         0xB8A5          /* par_play: JMP $B8D2 (ser_abort bit 0: the end) */
#define BOOT_SOUND_RETURN  0xA7BD          /* after boot_selftest_sound's JSR boot_loop_play at $A7BA */
#define INJECT_SWAP        1               /* the second swap of a call */
#define ABORT_TICKS        2               /* the mute: at the second timer interrupt after that swap */
#define WATCH_MAX          2

static const char *const k_corpus[] = { CORPUS_NAMES };
```

**`tests/test_diff_play.c`**: replace

```c
    inject_kind inject;
    bool ref;
    size_t swaps;
    bool abort_due;    /* the abort injection waits for the next isr_par_ready with isr_frame_flag bit 0 clear */
    bool differs;      /* copy B: the first difference is in why */
    char why[160];
} recorder;

/* A routine watched at its ROM entry; copy B runs it through run_b. */
```

with

```c
    inject_kind inject;
    bool ref;
    size_t swaps;
    unsigned abort_in; /* the abort injection: at the abort_in-th timer interrupt entry from now (0: none due) */
    bool differs;      /* copy B: the first difference is in why */
    char why[160];
    const fw_replacement *isr;   /* copy B: the timer interrupt's replacement, run by the $B2D9 hook (NULL: ROM) */
    fw_bridge *bridge;           /* ... on this bridge, the one the watched routine's thunk runs on */
} recorder;

/* A routine watched at its ROM entry; copy B runs it through run_b. */
```

**`tests/test_diff_play.c`**: replace

```c
} watch;

static machine g_m, g_a, g_b;
static swap_state *g_ref;      /* copy A's swaps: g_ref_n of them, room for g_ref_cap */
static size_t g_ref_n, g_ref_cap;
static swap_state g_cur;
```

with

```c
} watch;

static machine g_m, g_a, g_b;
static fw_bridge g_bridge;     /* copy B's */
static swap_state *g_ref;      /* copy A's swaps: g_ref_n of them, room for g_ref_cap */
static size_t g_ref_n, g_ref_cap;
static swap_state g_cur;
```

**`tests/test_diff_play.c`**: replace

```c
    machine *m = rec->m;
    size_t i = rec->swaps++;
    if (i == INJECT_SWAP && rec->inject == INJECT_ABORT)
        rec->abort_due = true;
    if (i == INJECT_SWAP && rec->inject == INJECT_REPLY)
        m->ram[SER_STATUS_DUE] = 1;
    if (rec->ref) {
```

with

```c
    machine *m = rec->m;
    size_t i = rec->swaps++;
    if (i == INJECT_SWAP && rec->inject == INJECT_ABORT)
        rec->abort_in = ABORT_TICKS;
    if (i == INJECT_SWAP && rec->inject == INJECT_REPLY)
        m->ram[SER_STATUS_DUE] = 1;
    if (rec->ref) {
```

**`tests/test_diff_play.c`**: replace

```c
    }
}

/* $C5D6, isr_par_ready (ROM in both copies): the abort injection, at its first entry after the second swap with
   isr_frame_flag bit 0 clear (the frame built after the swap is finished). */
static bool inject_abort(machine *m, void *user)
{
    recorder *rec = user;
    if (rec->abort_due && !(m->ram[ISR_FRAME_FLAG] & 1)) {
        m->ram[SER_ABORT] |= 1;
        rec->abort_due = false;
    }
    return false;
}

/* The copy-A hooks: which of the two injected branches the ROM routine executed. */
```

with

```c
    }
}

/* $B2D9, isr_timer_dsp entered by the CPU (the hook fires after the interrupt entry): the abort injection, at the
   ABORT_TICKS-th entry after the second swap; then the handler, ROM (false) or the C replacement on copy B's
   bridge. */
static bool on_timer(machine *m, void *user)
{
    recorder *rec = user;
    if (rec->abort_in && --rec->abort_in == 0)
        m->ram[SER_ABORT] |= 1;
    return rec->isr ? rec->isr->thunk(m, rec->bridge) : false;
}

/* The copy-A hooks: which of the two injected branches the ROM routine executed. */
```

**`tests/test_diff_play.c`**: replace

```c
    return machine_finish_routine(m, PLAY_BUDGET);
}

/* Copy B, the C thunk of the replacement, on a bridge bound to this copy. */
static bool run_thunk_b(machine *m, const watch *w)
{
    fw_bridge b;
    if (!fw_bridge_init(&b, m))
        return false;
    w->r->thunk(m, &b);
    return m->fault == MFAULT_NONE;
}
```

with

```c
    return machine_finish_routine(m, PLAY_BUDGET);
}

/* Copy B, the C thunk of the replacement, on the bridge bound to this copy (g_bridge, check_call). */
static bool run_thunk_b(machine *m, const watch *w)
{
    w->r->thunk(m, &g_bridge);
    return m->fault == MFAULT_NONE;
}
```

**`tests/test_diff_play.c`**: replace

```c
static size_t check_call(machine *m, watch *w, unsigned call, inject_kind inject)
{
    char why[200] = "";
    recorder ra = { &g_a, inject, true, 0, false, false, "" }, rb = { &g_b, inject, false, 0, false, false, "" };
    machine_clone(&g_a, m);
    g_hit_abort = g_hit_reply = false;
    CHECK(machine_hook(&g_a, PLAY_ABORT, hit_abort, NULL));
    CHECK(machine_hook(&g_a, PLAY_REPLY, hit_reply, NULL));
    CHECK(machine_hook(&g_a, ROM_ISR_PAR_READY, inject_abort, &ra));
    machine_set_outputs(&g_a, on_swap, &ra);
    bool a_ok = machine_finish_routine(&g_a, PLAY_BUDGET);
    g_ref_n = ra.swaps;
    g_reached_abort += g_hit_abort;
    g_reached_reply += g_hit_reply;
    machine_clone(&g_b, m);
    CHECK(machine_hook(&g_b, ROM_ISR_PAR_READY, inject_abort, &rb));
    machine_set_outputs(&g_b, on_swap, &rb);
    bool b_ok = w->run_b(&g_b, w);
    if (!a_ok)
```

with

```c
static size_t check_call(machine *m, watch *w, unsigned call, inject_kind inject)
{
    char why[200] = "";
    recorder ra = { &g_a, inject, true, 0, 0, false, "", NULL, NULL };
    recorder rb = { &g_b, inject, false, 0, 0, false, "", NULL, &g_bridge };
    machine_clone(&g_a, m);
    g_hit_abort = g_hit_reply = false;
    CHECK(machine_hook(&g_a, PLAY_ABORT, hit_abort, NULL));
    CHECK(machine_hook(&g_a, PLAY_REPLY, hit_reply, NULL));
    CHECK(machine_hook(&g_a, ROM_ISR_TIMER_DSP, on_timer, &ra));
    machine_set_outputs(&g_a, on_swap, &ra);
    bool a_ok = machine_finish_routine(&g_a, PLAY_BUDGET);
    g_ref_n = ra.swaps;
    g_reached_abort += g_hit_abort;
    g_reached_reply += g_hit_reply;
    machine_clone(&g_b, m);
    CHECK(fw_bridge_init(&g_bridge, &g_b));
    if (w->r)
        rb.isr = fw_replacement_find("isr_timer_dsp");   /* the C handler, once it is a replacement (M4.6b) */
    CHECK(machine_hook(&g_b, ROM_ISR_TIMER_DSP, on_timer, &rb));
    machine_set_outputs(&g_b, on_swap, &rb);
    bool b_ok = w->run_b(&g_b, w);
    if (!a_ok)
```

**`tests/test_diff_serial.c`**: replace

```c
/* Probe-based differential test of the serial code (M4.6a spec 4.2): the paths no corpus text reaches (the reply
   transmission, parallel mode, the _INT1 latch, the host-line interrupt, the ring page ends) are compared with the
   ROM through synthetic calls from real states. Every corpus file and the boot run go through the ROM engine; at
   every entry of isr_int1 $C2B4 (a received byte: the CPU has just entered the interrupt, PC and P pushed, I set) and
   of ser_tx_put $C4DD (a reply being queued: the output ring holds the reply's bytes before it), the machine is
   cloned twice for each probe of that point, both copies with interrupts held. The probe's injection is made to both
   copies; then copy A runs the ROM routine and copy B the C (its replacement hooked in copy B alone; the ROM routine
   while there is none), and the two must agree on the exit state with A, X and Y (diff_same, diff_state.h).
   An interrupt probe starts at the handler's first instruction with the frame the CPU pushed on entering it:
   isr_int1's own entry; for isr_int4_host_line the same point with the PC set to vec_int4_timerx ($FFF4) and INT4
   requested and enabled (ICR3 bits 7 and 6), as if the CPU had taken INT4 there instead (take_irq, cpu740.c, pushes
   the same PC and P and sets I for every line; only the vector differs). Each copy runs to the handler's RTI
   (machine_finish_routine). A subroutine probe is a machine_rom_call of the routine with the copy's registers.
   A hook on copy A records whether the probe's target branch went the probe's way (the hook at the destination fires
   with the branch as the last instruction executed, the machine's history), or for a routine without a branch
   whether it was entered; every probe must reach its target at every call. Every routine diff_lts never sees
```

with

```c
/* Probe-based differential test of the serial code (M4.6a spec 4.2) and of speech timing (M4.6b spec 4.3): the paths no
   corpus text reaches (the reply transmission, parallel mode, the _INT1 latch, the host-line interrupt, the ring page
   ends; the parallel handshake of isr_par_ready and isr_par_hold, a frame boundary of the timer interrupt with the
   frame flag still set) are compared with the ROM through synthetic calls from real states. Every corpus file and the
   boot run go through the ROM engine; at every entry of isr_int1 $C2B4 (a received byte: the CPU has just entered the
   interrupt, PC and P pushed, I set) and of ser_tx_put $C4DD (a reply being queued: the output ring holds the reply's
   bytes before it), the machine is cloned twice for each probe of that point, both copies with interrupts held. The
   probe's injection is made to both copies; then copy A runs the ROM routine and copy B the C (its replacement hooked
   in copy B alone; the ROM routine while there is none), and the two must agree on the exit state with A, X and Y
   (diff_same, diff_state.h).
   An interrupt probe starts at the handler's first instruction with the frame the CPU pushed on entering it: isr_int1's
   own entry; for isr_int4_host_line the same point with the PC set to vec_int4_timerx ($FFF4) and INT4 requested and
   enabled (ICR3 bits 7 and 6), as if the CPU had taken INT4 there instead (take_irq, cpu740.c, pushes the same PC and P
   and sets I for every line; only the vector differs); for isr_timer_dsp likewise with vec_int2_timers ($FFF8) and
   timer 3's request and enable bits (ICR2 bits 5 and 4). Each copy runs to the handler's RTI (machine_finish_routine).
   A subroutine probe is a machine_rom_call of the routine with the copy's registers.
   A hook on copy A records whether the probe's target branch went the probe's way (the hook at the destination fires
   with the branch as the last instruction executed, the machine's history), or for a routine without a branch
   whether it was entered; every probe must reach its target at every call. Every routine diff_lts never sees
```

**`tests/test_diff_serial.c`**: replace

```c
#include "diff_state.h"
#include "engine/rom_engine.h"
#include "engine/thunks.h"
#include "firmware/ser_addr.h"

#define PROBE_BUDGET  1000000ULL
#define VEC_INT4      0xFFF4   /* vec_int4_timerx */
#define P0_IN_PAR     0x08     /* P0_DIR in parallel mode (boot_init_ports $AC29): P0.0 and P0.4 inputs */
#define P0_HOST_LINE  0x04     /* P0.2, the host's handshake line (an input in both modes) */
#define ICR1_INT1_REQ 0x02
#define ICR3_INT4     0xC0     /* bit 7 request, bit 6 enable */
#define REGS          (FW_OUT_A | FW_OUT_X | FW_OUT_Y)

static const char *const k_corpus[] = { CORPUS_NAMES };
```

with

```c
#include "diff_state.h"
#include "engine/rom_engine.h"
#include "engine/thunks.h"
#include "firmware/isr_addr.h"
#include "firmware/ser_addr.h"

#define PROBE_BUDGET  1000000ULL
#define VEC_INT4      0xFFF4   /* vec_int4_timerx */
#define VEC_INT2      0xFFF8   /* vec_int2_timers (timer 3: the DSP tick) */
#define P0_IN_PAR     0x08     /* P0_DIR in parallel mode (boot_init_ports $AC29): P0.0 and P0.4 inputs */
#define P0_HOST_LINE  0x04     /* P0.2, the host's handshake line (an input in both modes) */
#define ICR1_INT1_REQ 0x02
#define ICR3_INT4     0xC0     /* bit 7 request, bit 6 enable */
#define ICR2_T3       0x30     /* timer 3: bit 5 request, bit 4 enable */
#define REGS          (FW_OUT_A | FW_OUT_X | FW_OUT_Y)

static const char *const k_corpus[] = { CORPUS_NAMES };
```

**`tests/test_diff_serial.c`**: replace

```c
}
static void inj_int4_high(machine *m) { int4(m, true); }
static void inj_int4_low(machine *m) { int4(m, false); }

/* ser_rx_put of the received byte with the input ring's write pointer at $38FF, its last byte */
static void inj_rx_wrap(machine *m)
```

with

```c
}
static void inj_int4_high(machine *m) { int4(m, true); }
static void inj_int4_low(machine *m) { int4(m, false); }

/* isr_timer_dsp: timer 3's interrupt taken instead, at a frame boundary (the 10th tick of the frame's last word:
   isr_tick_div and isr_words_left 1, isr_word_ofs 18) with isr_frame_flag bit 0 still set, so the frame repeats */
static void inj_timer_repeat(machine *m)
{
    m->sfr.icr2 |= ICR2_T3;
    m->ram[ISR_TICK_DIV] = 1;
    m->ram[ISR_WORDS_LEFT] = 1;
    m->ram[ISR_WORD_OFS] = 18;
    m->ram[ISR_FRAME_FLAG] |= 1;
    m->cpu.pc = (uint16_t)(machine_peek(m, VEC_INT2) | machine_peek(m, VEC_INT2 + 1) << 8);
}

/* isr_par_ready and isr_par_hold in parallel mode (boot_dip bit 0, P0's parallel directions); for isr_par_hold an
   _INT1 request pending too (ICR1 bit 1), which it clears */
static void inj_par_ready(machine *m)
{
    m->ram[BOOT_DIP] |= 1;
    m->sfr.port_dir[0] = P0_IN_PAR;
}
static void inj_par_hold(machine *m)
{
    inj_par_ready(m);
    m->sfr.icr1 |= ICR1_INT1_REQ;
}

/* ser_rx_put of the received byte with the input ring's write pointer at $38FF, its last byte */
static void inj_rx_wrap(machine *m)
```

**`tests/test_diff_serial.c`**: replace

```c
    { "int4_high",        AT_INT1,   "isr_int4_host_line", ROM_ISR_INT4_HOST_LINE, true,  inj_int4_high,   0xC3E0, 0xC3E7 },
    { "int4_low",         AT_INT1,   "isr_int4_host_line", ROM_ISR_INT4_HOST_LINE, true,  inj_int4_low,    0xC3E0, 0xC3E3 },
    { "host_line_high",   AT_INT1,   "isr_host_line_high", ROM_ISR_HOST_LINE_HIGH, false, inj_none,        0, ROM_ISR_HOST_LINE_HIGH },
    { "rx_wrap",          AT_INT1,   "ser_rx_put",         ROM_SER_RX_PUT,         false, inj_rx_wrap,     0xC5EF, 0xC5F2 },
    { "imm_page",         AT_INT1,   "cmd_imm_match",      ROM_CMD_IMM_MATCH,      false, inj_imm_page,    0xC456, 0xC458 },
    { "imm_query_text",   AT_INT1,   "cmd_imm_match",      ROM_CMD_IMM_MATCH,      false, inj_imm_query_text, 0xC4A1, 0xC4A9 },
```

with

```c
    { "int4_high",        AT_INT1,   "isr_int4_host_line", ROM_ISR_INT4_HOST_LINE, true,  inj_int4_high,   0xC3E0, 0xC3E7 },
    { "int4_low",         AT_INT1,   "isr_int4_host_line", ROM_ISR_INT4_HOST_LINE, true,  inj_int4_low,    0xC3E0, 0xC3E3 },
    { "host_line_high",   AT_INT1,   "isr_host_line_high", ROM_ISR_HOST_LINE_HIGH, false, inj_none,        0, ROM_ISR_HOST_LINE_HIGH },
    { "timer_repeat",     AT_INT1,   "isr_timer_dsp",      ROM_ISR_TIMER_DSP,      true,  inj_timer_repeat, 0xB300, 0xB303 },
    { "par_ready",        AT_INT1,   "isr_par_ready",      ROM_ISR_PAR_READY,      false, inj_par_ready,   0xC5D7, 0xC5DA },
    { "par_hold",         AT_INT1,   "isr_par_hold",       ROM_ISR_PAR_HOLD,       false, inj_par_hold,    0xC5DF, 0xC5E2 },
    { "rx_wrap",          AT_INT1,   "ser_rx_put",         ROM_SER_RX_PUT,         false, inj_rx_wrap,     0xC5EF, 0xC5F2 },
    { "imm_page",         AT_INT1,   "cmd_imm_match",      ROM_CMD_IMM_MATCH,      false, inj_imm_page,    0xC456, 0xC458 },
    { "imm_query_text",   AT_INT1,   "cmd_imm_match",      ROM_CMD_IMM_MATCH,      false, inj_imm_query_text, 0xC4A1, 0xC4A9 },
```

**`tests/test_diff_serial.c`**: replace

```c
    free(text);
}

/* Prints each probe's counts; every probe compared and its target reached at every call. */
static void report(void)
{
```

with

```c
    free(text);
}

/* The probes' routines that are not C yet (M4.6b): their copy B runs the ROM until Tasks 3 and 5 of the M4.6b plan
   make them C. */
static const char *const k_not_c_yet[] = { "isr_par_ready", "isr_par_hold", "isr_timer_dsp" };

static bool not_c_yet(const char *routine)
{
    for (size_t i = 0; i < sizeof k_not_c_yet / sizeof k_not_c_yet[0]; i++)
        if (!strcmp(k_not_c_yet[i], routine))
            return true;
    return false;
}

/* Prints each probe's counts; every probe compared and its target reached at every call. */
static void report(void)
{
```

**`tests/test_diff_serial.c`**: replace

```c
                   c ? "C" : "ROM", p->reached);
        CHECK(p->calls > 0);
        CHECK_EQ(p->reached, p->calls);
        CHECK(c || g_self_check);   /* every probe's routine is C (M4.6a) */
    }
}
```

with

```c
                   c ? "C" : "ROM", p->reached);
        CHECK(p->calls > 0);
        CHECK_EQ(p->reached, p->calls);
        CHECK(c || g_self_check || not_c_yet(p->routine));   /* every probe's routine is C (M4.6a, M4.6b) */
    }
}
```

- [ ] **Step 2: Run them (ROM against ROM)**

Run: `cmd //c ".\build.bat test diff_play"`, then `build/apollo2_tests.exe diff_serial`.
Expected: no warnings, and

```
  self-check (ROM against ROM, hello): par_play 1 calls, boot_loop_play 2 calls, 2 injected; $B8A5 reached 2, $B89C reached 2
  2 routines wait on the timer interrupt
  boot_loop_play       179 calls checked, 48 with both injections
  par_play             126 calls checked, 48 with both injections
  $B8A5 (abort) reached in 96 calls, $B89C (reply due) in 96
suite_diff_play              ok
1 suite(s), 0 failure(s), 0 skip(s)
```

(the same counts as before the change: the mute is now set at the second timer interrupt after the second frame event, and it still ends `par_play` at $B8A5 in both copies, 0 mismatches; 50 s in the proof run), then `diff_serial` with its 28 probes, the 25 of M4.6a as before, and the three new ones, in the self-check and on the corpus (6 s):

```
  timer_repeat     isr_timer_dsp         23 calls compared (ROM), $B300 -> $B303 in 23
  par_ready        isr_par_ready         23 calls compared (ROM), $C5D7 -> $C5DA in 23
  par_hold         isr_par_hold          23 calls compared (ROM), $C5DF -> $C5E2 in 23
...
  timer_repeat     isr_timer_dsp       2649 calls compared (ROM), $B300 -> $B303 in 2649
  par_ready        isr_par_ready       2649 calls compared (ROM), $C5D7 -> $C5DA in 2649
  par_hold         isr_par_hold        2649 calls compared (ROM), $C5DF -> $C5E2 in 2649
...
suite_diff_serial            ok
1 suite(s), 0 failure(s), 0 skip(s)
```

The `(ROM)` of the three new probes on the corpus is `k_not_c_yet`: their routines have no replacement yet. A mismatch here is the probe machinery comparing a copy with itself wrongly: fix the test, never the comparison.

- [ ] **Step 3: Run the full suite**

Run: `cmd //c ".\build.bat test"` (in the background)
Expected:

- `37 suite(s), 0 failure(s), 0 skip(s)` (527 s in the proof run);
- `diff_lts`, `golden_hybrid` (90 modes), `suite_golden` and `suite_synth` exactly as in Task 1 Step 6;
- `diff_play` and `diff_serial` as in Step 2.

- [ ] **Step 4: Commit**

```bash
git add tests/test_diff_play.c tests/test_diff_serial.c
git commit -m "diff_play injects the mute at the timer interrupt two ticks after the second frame event and hooks the timer interrupt in copy B; diff_serial probes isr_par_ready and isr_par_hold in parallel mode and the timer interrupt's repeated frame, proved ROM against ROM (M4.6b 4.2, 4.3)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 3: Volume and the parallel handshake in C: `isr_set_volume` $ACE8, `isr_volume_half` $B46C, `isr_par_ready` $C5D6, `isr_par_hold` $C5DE, with their hooks; their C callers call them directly, and the polling loops wait for an interrupt (spec §2.1–§2.6, §3.2, §4.5, §4.8 item 3)

**ROM facts checked / what was verified while proving the plan** (`rom_ACE8_isr_set_volume.txt`, `rom_B46C_isr_volume_half.txt`, `rom_C5D6_isr_par_ready.txt`, `rom_C5DE_isr_par_hold.txt`, `rom_C657_util_shl3.txt`, `rom_B877_par_play.txt`, `rom_A7D4_boot_loop_play.txt`, `rom_BCED_cmd_ev_set_volume.txt`, `rom_A61C_boot_reset.txt`, `rom_A697_boot_get_clause.txt`, `rom_C612_boot_queue_string.txt`, `rom_ADE0_boot_read_dip.txt`):
- **`isr_set_volume`** $ACE8–$ACF9: `EOR #$F; AND #$F; JSR util_shl3; STA $75; LDA #$87; AND P3; ORA $75; STA P3; RTS`: one read of P3, one write; $75 written; A out = the value written; X and Y untouched; N and Z from the `ORA`, which no caller reads. `isr_volume_half` $B46C–$B472: `LDA cmd_volume; LSR A; JSR isr_set_volume; RTS`. The C calls the C `util_shl3` (`fw_ops.h`); since `cmd_ev_sound_start` is C too (M4.4b), the `util_shl3` hook no longer fires in "all" mode (its comment is updated).
- **`isr_par_ready`** $C5D6–$C5DD, $C5EA–$C5EB: `PHA; BBC0 boot_dip,$C5EA; SEB3 P0; BRA $C5EA; PLA; RTS`. **`isr_par_hold`** $C5DE–$C5EB: `PHA; BBC0 boot_dip,$C5EA; PHP; SEI; CLB3 P0; CLB1 ICR1; PLP; NOP; PLA; RTS`. A, X and Y out as they came; the `PLA` sets N and Z from A, which no caller reads (`CLI`, `BBC0`, `JSR`, `PLA` follow every call).
- **The callers' register reads** (the thunk comments): after `isr_set_volume`, `boot_reset` ($A642) calls `boot_read_dip`, which loads A first ($ADE4 `LDA P4`) and reads no X or Y; `boot_loop_play` calls `par_play` after $B2C4 and `isr_speech_stop` after $B2D2, which keeps A, X and Y to its exit; `cmd_ev_set_volume` ($BCEF) returns to `par_play`'s $B892 `JMP par_play` (`LDX` first). After `isr_volume_half`, `boot_play_body` calls `isr_wait_frame` ($B2BF, $B2CD: `BBC0`/`LDM`, none read) and loads A ($B2C2, $B2D0). After `isr_par_ready`, `boot_get_clause` reads A and X ($A700 `CMP #$0D`, $A706/$A70D `STA $1B00,X`) and hands all three to `ser_tx_service` after $A6D0; `boot_reset` ($A672) calls `cmd_apply_speaker`, which takes X and Y; `boot_queue_string` ($C62B) pulls Y and A. After `isr_par_hold`, `boot_get_clause` ($A6E4) reads X after loading Y and A; `boot_reset` ($A66F) calls `isr_par_ready` and `cmd_apply_speaker`; `boot_queue_string` ($C617) reads the Y it set at $C615. All four rows declare A, X and Y.
- **The polling loops** (spec §2.3): `par_play`'s top loop, $B877–$B8AE: the events, the reply ($B895–$B89C), `isr_par_ready` ($B89F), the abort test ($B8A2), the frame-flag test ($B8A8), `isr_par_ready` ($B8AB), `JMP par_play`: the C waits at the back edge, after the second `isr_par_ready`, with `at` = $B877. The frame-flag waits: $B8C9–$B8CC (the last frames) and $B8E6–$B8E9 (the rest frames) are each `JSR isr_par_ready; BBC0 isr_frame_flag` back to the `JSR`: the C calls `isr_par_ready`, tests, and waits between tests with `at` = the loop's own address ($B8C9 or $B8E6; spec §2.3 names only $B8C9, the helper serving both). `isr_wait_frame` stays ROM until Task 4.
- **Where the C's `isr_par_ready` calls were seen**: until now each was a ROM call, whose registers the unit tests' mocks recorded. In C (serial mode) it does nothing observable, and its registers reach nothing, so `test_fw_pros.c`, `test_fw_cmd.c` and `test_fw_tn.c` run in parallel mode (`boot_dip` bit 0) and record each call by its `SEB3 P0` write: their order and count checks stay, the register values at those calls are no longer checked (diff_lts compares every caller's exit registers against the ROM). `test_fw_par.c`'s mock now takes the waits for an interrupt as the moments the timer and receive interrupts act (`g_flag_every`, `g_abort_at` count waits, no longer `isr_par_ready` calls), so the frame loop's tests count waits per pass; P3 is an SFR the mock keeps.
- **The fault tests that used `isr_par_ready`**: `test_golden_hybrid.c`'s "a ROM call from the C that does not return" and "that resets the stack" hooked `isr_par_ready`, `tn_convert_clause`'s first ROM call; now the C makes none there, so they use `cmd_run_due` with one command queued whose handler stays in ROM (`bank_cmd_select` $ABAF), its call of `cmd_run_next` $A893 hooked. `test_machine_hooks.c`'s deadline test saw the deadline at an `isr_par_ready` entry; it now sees it at an event handler run in ROM ($C657, an event due at segment 0), and `par_play`'s wait at its back edge ends at the given deadline with a fault at $B877. With a mute pending, `isr_wait_frame` (ROM now, C from Task 4, a fault at $B465 either way) waits out its budget, `FW_ROM_CALL_BUDGET`, before the deadline.
- **`diff_lts` compares every call of the eight routines of spec §4.1** (Task 1's `k_every_call`): from this task `isr_par_ready` is one of them, with **7,959,385** calls on the corpus, the boot run and the extra text (`par_play`'s polling loop calls it at every pass); `isr_set_volume` 772, `isr_volume_half` 360, `isr_par_hold` 3,318.

**Files:**
- Create: `src/firmware/isr_dsp.h`, `src/firmware/isr_dsp.c`
- Modify: `src/firmware/isr_serial.h`, `src/firmware/isr_serial.c`, `src/firmware/fw_ops.h`, `src/firmware/fw.h`, `src/firmware/par_play.h`, `src/firmware/par_play.c`, `src/firmware/cmd_play_event.c`, `src/firmware/cmd_play_event.h`, `src/firmware/cmd_scan.c`, `src/firmware/cmd_scan.h`, `src/firmware/pros_seg.c`, `src/firmware/pros_seg.h`, `src/firmware/tn_convert.c`, `src/firmware/par_addr.h`, `src/firmware/cmd_addr.h`, `src/firmware/pros_addr.h`, `src/engine/thunks.c`, `tests/test_fw_dsp.c`, `tests/test_fw_par.c`, `tests/test_fw_cmd.c`, `tests/test_fw_pros.c`, `tests/test_fw_tn.c`, `tests/test_golden_hybrid.c`, `tests/test_machine_hooks.c`, `tests/test_diff_serial.c`

**Interfaces:**
- Consumes: Task 1's `fw_wait(f, FW_WAIT_INTERRUPT, at)`, the SFR helpers, the stub host of `test_fw_dsp.c`; `util_shl3` (`fw_ops.h`), `P0_READY`, `ICR1_INT1_REQ`, `SFR_P0`, `SFR_ICR1` (`isr_serial.c`, `ser.h`), `ser_regs` (`thunks.c`).
- Produces:

```c
/* src/firmware/isr_dsp.h (Tasks 4 and 5 add the rest of speech timing) */
#define SFR_T2_PRE 0xDE, SFR_T2_RELOAD 0xDF, SFR_T3_PRE 0xE0, SFR_T3_RELOAD 0xE1, SFR_P23_FUNC 0xED, SFR_P3 0xEE,
        SFR_P2 0xF0, SFR_ICR2 0xFE
void isr_set_volume(fw *f, fw_regs *r);
void isr_volume_half(fw *f, fw_regs *r);
/* src/firmware/isr_serial.h */
void isr_par_ready(fw *f, fw_regs *r);
void isr_par_hold(fw *f, fw_regs *r);
/* src/firmware/fw_ops.h: fw_par_ready(f, r) calls the C isr_par_ready */
/* thunks.c: 4 rows (93 replacements) */
```

- [ ] **Step 1: Write the failing tests**

`test_fw_dsp.c` gains the four routines' tests and their rows; the tests whose mocks served the ROM calls of `isr_par_ready`, `isr_par_hold`, `isr_set_volume` and `isr_volume_half` record the C's SFR writes and the waits instead (the facts above); `diff_serial` requires the C for the handshake probes.

**`tests/test_diff_serial.c`**: replace

```c
    free(text);
}

/* The probes' routines that are not C yet (M4.6b): their copy B runs the ROM until Tasks 3 and 5 of the M4.6b plan
   make them C. */
static const char *const k_not_c_yet[] = { "isr_par_ready", "isr_par_hold", "isr_timer_dsp" };

static bool not_c_yet(const char *routine)
{
```

with

```c
    free(text);
}

/* The probes' routines that are not C yet (M4.6b): their copy B runs the ROM until Task 5 of the M4.6b plan makes it
   C. */
static const char *const k_not_c_yet[] = { "isr_timer_dsp" };

static bool not_c_yet(const char *routine)
{
```

**`tests/test_fw_cmd.c`**: replace

```c
static unsigned g_ncalls;
static uint8_t g_before[FW_RAM_SIZE];   /* RAM as a test left it: the C must not touch it before a ROM cmd_run_next */
static unsigned g_touched;              /* ROM cmd_run_next calls that found RAM changed since g_before */

/* Records every ROM call and answers the routines the C calls as the ROM would; any other call fails. The reply
   handlers call the C ser_tx_* routines (M4.6a), which make no ROM call. */
```

with

```c
static unsigned g_ncalls;
static uint8_t g_before[FW_RAM_SIZE];   /* RAM as a test left it: the C must not touch it before a ROM cmd_run_next */
static unsigned g_touched;              /* ROM cmd_run_next calls that found RAM changed since g_before */
static uint8_t g_p0;                    /* the port P0 the C isr_par_ready reads and writes in parallel mode */

/* Records every ROM call and answers the routines the C calls as the ROM would; any other call fails. The reply
   handlers call the C ser_tx_* routines (M4.6a), which make no ROM call. */
```

**`tests/test_fw_cmd.c`**: replace

```c
        memcpy(g_before, g_ram, sizeof g_ram);
        return FW_CALL_RETURNED;   /* X back as it was (cmd_run_done) */
    }
    /* isr_par_ready leaves A, X and Y as they were: the registers come back unchanged */
    return addr == ROM_ISR_PAR_READY ? FW_CALL_RETURNED : FW_CALL_FAILED;
}

/* Zeroed RAM with the output ring empty at $3900 (as boot_clear_input_state leaves it, $AC09-$AC18), the mock
```

with

```c
        memcpy(g_before, g_ram, sizeof g_ram);
        return FW_CALL_RETURNED;   /* X back as it was (cmd_run_done) */
    }
    return FW_CALL_FAILED;   /* isr_par_ready is C since M4.6b */
}

/* The C isr_par_ready (M4.6b) in parallel mode (scan_setup sets boot_dip bit 0): SEB3 P0, one read and one write of
   P0 ($F6), recorded as a call of ROM_ISR_PAR_READY without registers (the C passes them to nothing). Any other SFR
   access fails the test. */
static uint8_t mock_read_sfr(void *ctx, uint8_t addr)
{
    (void)ctx;
    CHECK_EQ(addr, 0xF6);
    return g_p0;
}

static void mock_write_sfr(void *ctx, uint8_t addr, uint8_t value)
{
    (void)ctx;
    CHECK_EQ(addr, 0xF6);
    CHECK(value & 0x08);
    g_p0 = value;
    if (g_ncalls < MAX_CALLS)
        g_calls[g_ncalls] = (rom_call_record){ ROM_ISR_PAR_READY, 0, 0, 0, 0, 0, 0, 0 };
    g_ncalls++;
}

/* Zeroed RAM with the output ring empty at $3900 (as boot_clear_input_state leaves it, $AC09-$AC18), the mock
```

**`tests/test_fw_cmd.c`**: replace

```c
    g_host.ctx = &g_f;
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = NULL;   /* no command routine in C drives P2 (bank_cmd_select stays in ROM) */
    g_ncalls = 0;
    g_touched = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
    fw_wr16(&g_f, SER_TX_READ, SER_TX_RING);
    fw_wr16(&g_f, SER_TX_WRITE, SER_TX_RING);
```

with

```c
    g_host.ctx = &g_f;
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = NULL;   /* no command routine in C drives P2 (bank_cmd_select stays in ROM) */
    g_host.read_sfr = mock_read_sfr;
    g_host.write_sfr = mock_write_sfr;
    g_ncalls = 0;
    g_touched = 0;
    g_p0 = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
    fw_wr16(&g_f, SER_TX_READ, SER_TX_RING);
    fw_wr16(&g_f, SER_TX_WRITE, SER_TX_RING);
```

**`tests/test_fw_cmd.c`**: replace

```c
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
```

with

```c
    return n;
}

/* Every call recorded is isr_par_ready (its P0 write): no ROM call. */
static void check_par_ready_calls(void)
{
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++)
        CHECK_EQ(g_calls[i].addr, ROM_ISR_PAR_READY);
}

/* boot_clause = s (the bytes after it unchanged). */
static void put_clause(const char *s) { memcpy(g_ram + BOOT_CLAUSE, s, strlen(s)); }

/* setup(), then boot_clause = clause (then zeros), boot_clause_len = its length, cmd_char = '@', parallel mode
   (boot_dip bit 0: each isr_par_ready is recorded). */
static void scan_setup(const char *clause)
{
    setup();
    g_ram[BOOT_DIP] = 1;
    put_clause(clause);
    g_ram[BOOT_CLAUSE_LEN] = (uint8_t)strlen(clause);
    g_ram[CMD_CHAR] = '@';
```

**`tests/test_fw_cmd.c`**: replace

```c
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
```

with

```c
    CHECK_EQ(fw_rd16(&g_f, CMD_TABLE_PTR), CMD_TABLE);
    CHECK_EQ(g_ram[CMD_TABLE_OFS], 2 * P_SPEED);
    CHECK_EQ(fw_rd16(&g_f, CMD_PAT_PTR), entry(CMD_TABLE, P_SPEED));
    CHECK_EQ(g_ncalls, 4);                      /* isr_par_ready once per character read (X = 0, 1, 4, 5) */
    check_par_ready_calls();
    CHECK(!g_f.fault);
}
```

**`tests/test_fw_dsp.c`**: replace

```c
   expected value is worked out from the dump of the routine (build/ghidra740/dump), as the comment beside it says. */
#include <string.h>
#include "test.h"
#include "firmware/fw.h"
#include "firmware/data/fw_data.h"

#define MAX_LOG 64
#define SFR_P2  0xF0
#define SFR_P3  0xEE

typedef enum { EV_READ, EV_WRITE, EV_WAIT, EV_DSP, EV_FRAME } ev_kind;
/* One host call: EV_READ/EV_WRITE an SFR (addr) and its value; EV_WAIT the ROM loop (addr) and the kind (value);
```

with

```c
   expected value is worked out from the dump of the routine (build/ghidra740/dump), as the comment beside it says. */
#include <string.h>
#include "test.h"
#include "engine/thunks.h"
#include "firmware/isr_dsp.h"
#include "firmware/isr_serial.h"
#include "firmware/data/fw_data.h"

#define MAX_LOG 64
#define SFR_P0  0xF6
#define SFR_ICR1 0xFF

typedef enum { EV_READ, EV_WRITE, EV_WAIT, EV_DSP, EV_FRAME } ev_kind;
/* One host call: EV_READ/EV_WRITE an SFR (addr) and its value; EV_WAIT the ROM loop (addr) and the kind (value);
```

**`tests/test_fw_dsp.c`**: replace

```c
    CHECK(g_f.low_window);
}

void suite_fw_dsp(void)
{
    CHECK(fw_image_get() != NULL);
```

with

```c
    CHECK(g_f.low_window);
}

/* The hook of name sits at addr, declares outputs, and is neither a waits nor an isr row (pinned here; diff_lts
   compares the outputs against the ROM). */
static void check_row(const char *name, uint16_t addr, uint8_t outputs)
{
    const fw_replacement *r = fw_replacement_find(name);
    CHECK(r != NULL);
    if (!r)
        return;
    CHECK_EQ(r->addr, addr);
    CHECK_EQ(r->outputs, outputs);
    CHECK(!r->waits);
    CHECK(!r->isr);
}

static void test_rows(void)
{
    check_row("isr_set_volume", 0xACE8, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_volume_half", 0xB46C, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_par_ready", 0xC5D6, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_par_hold", 0xC5DE, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

/* isr_set_volume $ACE8 at all 16 levels: $75 = ((A ^ $F) & $F) << 3 ($ACE8-$ACEF); P3 read once (AND P3) and written
   once with (P3 & $87) | $75 ($ACF1-$ACF7), which is A out; X and Y untouched. P3 = $A5: bits 7, 2 and 0 kept
   ($85), bits 3-6 replaced. A's high nibble does not count (AND #$F): $F3 is level 3. */
static void test_set_volume(void)
{
    for (unsigned v = 0; v < 17; v++) {
        uint8_t a = v < 16 ? (uint8_t)v : 0xF3;
        uint8_t level = (uint8_t)(((a ^ 0x0F) & 0x0F) << 3);
        uint8_t want = (uint8_t)(0x85 | level);
        const event log[] = { { EV_READ, SFR_P3, 0xA5 }, { EV_WRITE, SFR_P3, want } };
        setup();
        g_sfr[SFR_P3] = 0xA5;
        fw_regs r = { a, 0x11, 0x22, 0 };
        isr_set_volume(&g_f, &r);
        check_log(log, 2);
        CHECK_EQ(g_ram[0x75], level);
        CHECK_EQ(r.a, want);
        CHECK_EQ(r.x, 0x11);
        CHECK_EQ(r.y, 0x22);
        CHECK(!g_f.fault);
    }
    setup();
    g_sfr[SFR_P3] = 0xA5;
    fw_regs r = { 0, 0, 0, 0 };
    isr_set_volume(&g_f, &r);
    CHECK_EQ(r.a, 0xFD);                          /* silent: $85 | $78 */
    CHECK_EQ(g_sfr[SFR_P3], 0xFD);
}

/* isr_volume_half $B46C: isr_set_volume with A = cmd_volume >> 1 (LSR A): 11 gives 5, $FF gives $7F (level 0). */
static void test_volume_half(void)
{
    static const event log11[] = { { EV_READ, SFR_P3, 0x00 }, { EV_WRITE, SFR_P3, 0x50 } };   /* (5 ^ $F) << 3 */
    setup();
    g_ram[CMD_VOLUME] = 11;
    fw_regs r = { 0x99, 0x11, 0x22, 0 };
    isr_volume_half(&g_f, &r);
    check_log(log11, 2);
    CHECK_EQ(g_ram[0x75], 0x50);
    CHECK_EQ(r.a, 0x50);
    CHECK_EQ(r.x, 0x11);
    CHECK_EQ(r.y, 0x22);
    CHECK_EQ(g_ram[CMD_VOLUME], 11);

    static const event log_ff[] = { { EV_READ, SFR_P3, 0xFF }, { EV_WRITE, SFR_P3, 0x87 } };
    setup();
    g_ram[CMD_VOLUME] = 0xFF;
    g_sfr[SFR_P3] = 0xFF;
    r = (fw_regs){ 0, 0, 0, 0 };
    isr_volume_half(&g_f, &r);
    check_log(log_ff, 2);
    CHECK_EQ(r.a, 0x87);
    CHECK(!g_f.fault);
}

/* isr_par_ready $C5D6 and isr_par_hold $C5DE: in serial mode (boot_dip bit 0 clear; $FE has it clear) nothing but
   A, X and Y kept; in parallel mode isr_par_ready sets P0.3 (SEB3: P0 read, then written with bit 3) and
   isr_par_hold clears P0.3 and the _INT1 request ICR1 bit 1 (CLB3 P0, CLB1 ICR1: each a read then a write). */
static void test_par_handshake(void)
{
    static const uint8_t dips[2] = { 0x00, 0xFE };
    for (unsigned i = 0; i < 2; i++) {
        setup();
        g_ram[BOOT_DIP] = dips[i];
        g_sfr[SFR_P0] = 0x20;
        g_sfr[SFR_ICR1] = 0x03;
        fw_regs r = { 0x11, 0x22, 0x33, 0 };
        isr_par_ready(&g_f, &r);
        isr_par_hold(&g_f, &r);
        CHECK_EQ(g_nlog, 0);
        CHECK_EQ(r.a, 0x11);
        CHECK_EQ(r.x, 0x22);
        CHECK_EQ(r.y, 0x33);
    }

    static const event ready[] = { { EV_READ, SFR_P0, 0x20 }, { EV_WRITE, SFR_P0, 0x28 } };
    setup();
    g_ram[BOOT_DIP] = 1;
    g_sfr[SFR_P0] = 0x20;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_par_ready(&g_f, &r);
    check_log(ready, 2);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0x22);
    CHECK_EQ(r.y, 0x33);

    static const event hold[] = {
        { EV_READ, SFR_P0, 0x28 }, { EV_WRITE, SFR_P0, 0x20 }, { EV_READ, SFR_ICR1, 0x03 },
        { EV_WRITE, SFR_ICR1, 0x01 },
    };
    setup();
    g_ram[BOOT_DIP] = 1;
    g_sfr[SFR_P0] = 0x28;
    g_sfr[SFR_ICR1] = 0x03;
    isr_par_hold(&g_f, &r);
    check_log(hold, 4);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0x22);
    CHECK_EQ(r.y, 0x33);
    CHECK(!g_f.fault);
}

void suite_fw_dsp(void)
{
    CHECK(fw_image_get() != NULL);
```

**`tests/test_fw_dsp.c`**: replace

```c
    test_frame_done();
    test_wait_kinds();
    test_p2_window();
}
```

with

```c
    test_frame_done();
    test_wait_kinds();
    test_p2_window();
    test_rows();
    test_set_volume();
    test_volume_half();
    test_par_handshake();
}
```

**`tests/test_fw_par.c`**: replace

```c
#include "firmware/data/fw_data.h"

#define MAX_CALLS 1024

/* One ROM call: its address, registers and budget, and par_frames_left at the call. */
typedef struct {
    uint16_t addr;
    uint8_t a, x, y;
    uint64_t budget;
    uint8_t frames;   /* par_frames_left at the call */
} rom_call_record;

static uint8_t g_ram[0x10000];   /* the firmware's RAM is $0000-$3FFF; 64 KiB so no index can run off the array */
```

with

```c
#include "firmware/data/fw_data.h"

#define MAX_CALLS 1024
#define PAR_WAIT_TOP   0xB877   /* par_play's polling loop: the wait at its back edge */
#define PAR_WAIT_LAST  0xB8C9   /* the frame-flag wait of the last frames */
#define PAR_WAIT_REST  0xB8E6   /* the frame-flag wait of the rest frames */

/* One ROM call (its address, registers and budget) or one wait (wait set: addr = the ROM loop it stands for, no
   registers), and par_frames_left at the call. */
typedef struct {
    uint16_t addr;
    uint8_t a, x, y;
    uint64_t budget;
    uint8_t frames;   /* par_frames_left at the call */
    bool wait;
} rom_call_record;

static uint8_t g_ram[0x10000];   /* the firmware's RAM is $0000-$3FFF; 64 KiB so no index can run off the array */
```

**`tests/test_fw_par.c`**: replace

```c
static fw_host g_host;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;
static uint8_t g_p3;             /* the port P3 the mock isr_set_volume reads and writes (an SFR: not in g_ram) */
static unsigned g_ready_calls;   /* isr_par_ready calls so far */
static unsigned g_flag_every;    /* the mock timer interrupt sets isr_frame_flag at every n-th of them (0: never) */
static unsigned g_abort_at;      /* the mock receive interrupt sets ser_abort bit 0 at this one (0: never) */
static uint8_t g_uart_status;    /* UART_STATUS as the mock reads it */
static unsigned g_p0_reads;      /* reads of P0: the C ser_tx_service's test of the host line */

/* Records every ROM call. isr_set_volume (M4.6, called by cmd_ev_set_volume) returns the registers the ROM one
   leaves; so do the interrupt routines that par_play and boot_loop_play call (M4.6), with what the timer and receive
   interrupts would do between the frame loop's tests (g_flag_every, g_abort_at); any other call fails (par_setup_voice
   and par_play call the C frame builder since M4.5, and the C ser_tx_service since M4.6a). */
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
        c->frames = g_ram[PAR_FRAMES_LEFT];
    }
    g_ncalls++;
    switch (addr) {
    case ROM_ISR_VOLUME_HALF:        /* $B46C-$B472: LDA cmd_volume / LSR / JSR isr_set_volume: on into its case */
        r->a = (uint8_t)(g_ram[CMD_VOLUME] >> 1);
        /* fall through */
    case ROM_ISR_SET_VOLUME:         /* $ACE8-$ACF9: EOR #$F / AND #$F / JSR util_shl3 / STA $75 / LDA #$87 / AND P3 /
                                        ORA $75 / STA P3: A = the new P3; X and Y untouched ($75, the ROM's scratch,
                                        is not modelled) */
        g_p3 = (uint8_t)((g_p3 & 0x87) | util_shl3((uint8_t)((r->a ^ 0x0F) & 0x0F)));
        r->a = g_p3;
        return FW_CALL_RETURNED;
    case ROM_ISR_PAR_READY:          /* $C5D6-$C5D9, $C5EA-$C5EB: PHA, BBC0 boot_dip (serial mode), PLA: A, X, Y kept.
                                        Meanwhile the timer interrupt swaps the buffers and sets the flag ($B306
                                        LDM #1), and the receive interrupt sets ser_abort bit 0 (a mute) */
        g_ready_calls++;
        if (g_flag_every && g_ready_calls % g_flag_every == 0)
            g_ram[ISR_FRAME_FLAG] = 1;
        if (g_ready_calls == g_abort_at)
            g_ram[SER_ABORT] |= 1;
        return FW_CALL_RETURNED;
    case ROM_ISR_PAR_HOLD:           /* $C5DE-$C5DF, $C5EA-$C5EB: PHA, BBC0 boot_dip, PLA: A, X, Y kept */
    case ROM_ISR_SPEECH_STOP:        /* $B398-$B3A3: LDM, LDM, isr_dsp_reset (PHP ... PLP), CLB0: A, X, Y kept */
        return FW_CALL_RETURNED;
    case ROM_ISR_WAIT_FRAME:         /* $B465-$B46B: BBC0 isr_frame_flag (the frame is due), LDM #0: A, X, Y kept */
```

with

```c
static fw_host g_host;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;
static uint8_t g_p3;             /* the port P3 the C isr_set_volume reads and writes (an SFR: not in g_ram) */
static uint8_t g_p3_log[8];      /* the values written to P3, in order */
static unsigned g_np3;
static unsigned g_waits;         /* FW_WAIT_INTERRUPT waits so far */
static unsigned g_flag_every;    /* the mock timer interrupt sets isr_frame_flag at every n-th of them (0: never) */
static unsigned g_abort_at;      /* the mock receive interrupt sets ser_abort bit 0 at this one (0: never) */
static uint8_t g_uart_status;    /* UART_STATUS as the mock reads it */
static unsigned g_p0_reads;      /* reads of P0: the C ser_tx_service's test of the host line */

static void record(uint16_t addr, const fw_regs *r, uint64_t budget, bool wait)
{
    if (g_ncalls < MAX_CALLS) {
        rom_call_record *c = &g_calls[g_ncalls];
        c->addr = addr;
        c->a = r ? r->a : 0;
        c->x = r ? r->x : 0;
        c->y = r ? r->y : 0;
        c->budget = budget;
        c->frames = g_ram[PAR_FRAMES_LEFT];
        c->wait = wait;
    }
    g_ncalls++;
}

/* Records every ROM call. The interrupt routines that boot_loop_play and par_play still call in ROM (M4.6b Task 4
   makes them C) return the registers the ROM ones leave; any other call fails (par_setup_voice and par_play call
   the C frame builder since M4.5, the C ser_tx_service since M4.6a, the C volume and parallel handshake since
   M4.6b). */
static fw_call_result mock_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    (void)ctx;
    record(addr, r, budget, false);
    switch (addr) {
    case ROM_ISR_SPEECH_STOP:        /* $B398-$B3A3: LDM, LDM, isr_dsp_reset (PHP ... PLP), CLB0: A, X, Y kept */
        return FW_CALL_RETURNED;
    case ROM_ISR_WAIT_FRAME:         /* $B465-$B46B: BBC0 isr_frame_flag (the frame is due), LDM #0: A, X, Y kept */
```

**`tests/test_fw_par.c`**: replace

```c
    }
}

/* UART_STATUS ($E7) as g_uart_status says; any other SFR reads 0: P0 ($F6, counted) with the host line P0.2 low, as
   in the machine, so the C ser_tx_service returns at its BBC2 P0 ($C585) and sends nothing. */
static uint8_t mock_read_sfr(void *ctx, uint8_t addr)
{
    (void)ctx;
    g_p0_reads += addr == 0xF6;
    return addr == 0xE7 ? g_uart_status : 0;
}

/* Zeroed RAM, the mock host, the converted image, no calls recorded. */
```

with

```c
    }
}

/* Records every wait. Each FW_WAIT_INTERRUPT is an interrupt handled: the timer interrupt swaps the buffers and sets
   the flag ($B306 LDM #1) at every g_flag_every-th, and the receive interrupt sets ser_abort bit 0 (a mute) at the
   g_abort_at-th. */
static bool mock_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    (void)ctx;
    record(at, NULL, 0, true);
    if (kind == FW_WAIT_INTERRUPT) {
        g_waits++;
        if (g_flag_every && g_waits % g_flag_every == 0)
            g_ram[ISR_FRAME_FLAG] = 1;
        if (g_waits == g_abort_at)
            g_ram[SER_ABORT] |= 1;
    }
    return true;
}

/* UART_STATUS ($E7) as g_uart_status says, P3 ($EE) as g_p3; any other SFR reads 0: P0 ($F6, counted) with the host
   line P0.2 low, as in the machine, so the C ser_tx_service returns at its BBC2 P0 ($C585) and sends nothing. */
static uint8_t mock_read_sfr(void *ctx, uint8_t addr)
{
    (void)ctx;
    g_p0_reads += addr == 0xF6;
    return addr == 0xE7 ? g_uart_status : addr == 0xEE ? g_p3 : 0;
}

/* P3 ($EE) writes go to g_p3 and its log; any other SFR write fails the test (nothing else is written here). */
static void mock_write_sfr(void *ctx, uint8_t addr, uint8_t value)
{
    (void)ctx;
    CHECK_EQ(addr, 0xEE);
    g_p3 = value;
    if (g_np3 < sizeof g_p3_log)
        g_p3_log[g_np3] = value;
    g_np3++;
}

/* Zeroed RAM, the mock host, the converted image, no calls recorded. */
```

**`tests/test_fw_par.c`**: replace

```c
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = NULL;   /* nothing in the playback set-up drives P2 */
    g_host.read_sfr = mock_read_sfr;
    g_ncalls = 0;
    g_ready_calls = 0;
    g_flag_every = 0;
    g_abort_at = 0;
    g_uart_status = 0x02;           /* the transmitter ready, nothing received (as the machine has it) */
```

with

```c
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = NULL;   /* nothing in the playback set-up drives P2 */
    g_host.read_sfr = mock_read_sfr;
    g_host.write_sfr = mock_write_sfr;
    g_host.wait = mock_wait;
    g_ncalls = 0;
    g_np3 = 0;
    g_waits = 0;
    g_flag_every = 0;
    g_abort_at = 0;
    g_uart_status = 0x02;           /* the transmitter ready, nothing received (as the machine has it) */
```

**`tests/test_fw_par.c`**: replace

```c
    CHECK(i < g_ncalls && i < MAX_CALLS);
    if (i >= g_ncalls || i >= MAX_CALLS)
        return;
    CHECK_EQ(g_calls[i].addr, addr);
    CHECK_EQ(g_calls[i].a, a);
    CHECK_EQ(g_calls[i].x, x);
```

with

```c
    CHECK(i < g_ncalls && i < MAX_CALLS);
    if (i >= g_ncalls || i >= MAX_CALLS)
        return;
    CHECK(!g_calls[i].wait);
    CHECK_EQ(g_calls[i].addr, addr);
    CHECK_EQ(g_calls[i].a, a);
    CHECK_EQ(g_calls[i].x, x);
```

**`tests/test_fw_par.c`**: replace

```c
    check_no_calls();
}

/* cmd_ev_set_volume with volume v and the P3 latch p3: STA cmd_volume; JSR isr_set_volume with the entry A, X and Y;
   out what it leaves: A = (p3 & $87) | ((v ^ $F) & $F) << 3 (the mock's model of $ACE8), X and Y untouched. */
static void set_volume_case(uint8_t v, uint8_t p3, uint8_t want_a)
{
    static const uint16_t w[] = { CMD_VOLUME };
    setup();
    g_p3 = p3;
    g_ram[CMD_VOLUME] = 0xEE;
```

with

```c
    check_no_calls();
}

/* cmd_ev_set_volume with volume v and the P3 latch p3: STA cmd_volume; the C isr_set_volume ($ACE8, M4.6b) with the
   entry A, X and Y: $75 = ((v ^ $F) & $F) << 3, P3 = A = (p3 & $87) | $75 (one write), X and Y untouched. No ROM
   call. */
static void set_volume_case(uint8_t v, uint8_t p3, uint8_t want_a)
{
    static const uint16_t w[] = { CMD_VOLUME, FW_TMP_75 };
    setup();
    g_p3 = p3;
    g_ram[CMD_VOLUME] = 0xEE;
```

**`tests/test_fw_par.c`**: replace

```c
    fw_regs r = { v, 0x5A, 0xA5, 0 };
    cmd_ev_set_volume(&g_f, &r);
    CHECK_EQ(g_ram[CMD_VOLUME], v);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_ISR_SET_VOLUME, v, 0x5A, 0xA5);
    CHECK_EQ(r.a, want_a);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, 0xA5);
    check_written(w, 1);
    CHECK(!g_f.fault);
}
```

with

```c
    fw_regs r = { v, 0x5A, 0xA5, 0 };
    cmd_ev_set_volume(&g_f, &r);
    CHECK_EQ(g_ram[CMD_VOLUME], v);
    CHECK_EQ(g_ram[FW_TMP_75], (uint8_t)(want_a & 0x78));
    CHECK_EQ(g_ncalls, 0);
    CHECK_EQ(g_np3, 1);
    CHECK_EQ(g_p3_log[0], want_a);
    CHECK_EQ(r.a, want_a);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, 0xA5);
    check_written(w, 2);
    CHECK(!g_f.fault);
}
```

**`tests/test_fw_par.c`**: replace

```c
    check_written(w_index, sizeof w_index / sizeof w_index[0]);
    check_no_calls();

    setup();                                                       /* @A3, queued first: isr_set_volume in ROM */
    g_p3 = 0;
    r = (fw_regs){ 0x11, 0x22, 0x33, 0 };
    run_event(0, ROM_CMD_EV_SET_VOLUME, 0x0010, 3, &r);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0x0010);
    CHECK_EQ(g_ram[CMD_VOLUME], 3);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_ISR_SET_VOLUME, 3, 0, 0x33);
    CHECK_EQ(r.a, 0x60);                                           /* $C << 3 */
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x33);
```

with

```c
    check_written(w_index, sizeof w_index / sizeof w_index[0]);
    check_no_calls();

    setup();                                                       /* @A3, queued first: the C isr_set_volume */
    g_p3 = 0;
    r = (fw_regs){ 0x11, 0x22, 0x33, 0 };
    run_event(0, ROM_CMD_EV_SET_VOLUME, 0x0010, 3, &r);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0x0010);
    CHECK_EQ(g_ram[CMD_VOLUME], 3);
    CHECK_EQ(g_ncalls, 0);
    CHECK_EQ(g_np3, 1);
    CHECK_EQ(g_p3, 0x60);
    CHECK_EQ(r.a, 0x60);                                           /* $C << 3 */
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x33);
```

**`tests/test_fw_par.c`**: replace

```c
        CHECK_EQ(g_calls[i].frames, frames);
}

/* The calls i, i + 1, ... are exactly the n addresses given. */
static void check_order(unsigned i, const uint16_t *addrs, unsigned n)
{
    CHECK(i + n <= g_ncalls && i + n <= MAX_CALLS);
    for (unsigned k = 0; k < n && i + k < g_ncalls && i + k < MAX_CALLS; k++)
        CHECK_EQ(g_calls[i + k].addr, addrs[k]);
}

/* The end ($B8D2-$B8FA) from call i on, with `waits` isr_par_ready calls per wait for the flag: isr_wait_frame,
   then per rest frame (frm_copy_rest_frame, frm_snapshot, frm_clamp_mark_space and frm_transition_flags, all C)
   the wait, with par_frames_left 3, 2, 1 ($B8D5, then the DEC at $B8EF); then the last isr_par_ready ($B8F7;
   cmd_ev_sound_end is C and returns at once, cmd_sound_active being 0). Returns the number of calls. */
static unsigned check_rest_frames(unsigned i, unsigned waits)
{
    uint16_t k[1 + 3 * 2 + 1];   /* enough for waits <= 2 */
    CHECK(waits <= 2);
    if (waits > 2)
        return 0;
    unsigned n = 0, per = waits;
    k[n++] = ROM_ISR_WAIT_FRAME;
    for (unsigned j = 0; j < 3; j++)
        for (unsigned m = 0; m < waits; m++)
            k[n++] = ROM_ISR_PAR_READY;
    k[n++] = ROM_ISR_PAR_READY;
    check_order(i, k, n);
    for (unsigned j = 0; j < 3; j++)
        for (unsigned m = 0; m < per; m++)
            if (i + 1 + per * j + m < MAX_CALLS)
                CHECK_EQ(g_calls[i + 1 + per * j + m].frames, 3 - j);
    return n;
}

/* No segment left (pros_seg_count = 0): two passes (the flag comes at the second isr_par_ready, every 2nd call),
   then the tight loop ($B8BB-$B8CF): par_frames_left 3 -> 2 -> 1 gives two frames, each par_step_tracks and
   frm_finish_frame (both C), the flag cleared and waited for again; 0 ends it. The rest frames, the flag every 2nd
   call. No event (cmd_ev_count 0) and no reply (ser_status_due 0). The registers flow: the entry A and Y and X =
   cmd_ev_next ($B877 LDX) reach the passes' isr_par_ready; Y = 0 from $B8B6 (pros_seg_count + 1); par_step_tracks
   leaves A = pros_play_pitch >> 2 = 0, X = 0, Y = 10, which frm_finish_frame keeps; each rest frame leaves A =
   frm_flags, X = 0 (frm_snapshot's) and Y = $0D (frm_transition_flags'), which par_play returns. frm_rest_frame is
   $77 and zeros: the first rest frame's ALF goes from 0 (the frames before) to $77, an 8-bit difference of $89, so
   frm_flags = $C0 (bit 7: $89 - 12 has bit 7 clear; bit 6: $89 - 28 too); the next two change nothing (0). The
   fill frame ends as the rest frame with b11 = @M 0 raised to 47 - 2 x 0 and b13 = 0. */
static void test_play_tail(void)
{
    setup();
```

with

```c
        CHECK_EQ(g_calls[i].frames, frames);
}

/* Record i is a wait for an interrupt at the polling loop at, made with par_frames_left = frames. */
static void check_wait(unsigned i, uint16_t at, uint8_t frames)
{
    CHECK(i < g_ncalls && i < MAX_CALLS);
    if (i >= g_ncalls || i >= MAX_CALLS)
        return;
    CHECK(g_calls[i].wait);
    CHECK_EQ(g_calls[i].addr, at);
    CHECK_EQ(g_calls[i].frames, frames);
}

/* The end ($B8D2-$B8FA) from record i on, with `waits` waits for an interrupt per rest frame: isr_wait_frame, then
   per rest frame (frm_copy_rest_frame, frm_snapshot, frm_clamp_mark_space and frm_transition_flags, all C) the
   waits at $B8E6, with par_frames_left 3, 2, 1 ($B8D5, then the DEC at $B8EF); then cmd_ev_sound_end (C, returning
   at once, cmd_sound_active being 0) and the last isr_par_ready ($B8F7, C). Returns the number of records. */
static unsigned check_rest_frames(unsigned i, unsigned waits)
{
    CHECK(i < g_ncalls && i < MAX_CALLS);
    if (i >= g_ncalls || i >= MAX_CALLS)
        return 0;
    CHECK(!g_calls[i].wait);
    CHECK_EQ(g_calls[i].addr, ROM_ISR_WAIT_FRAME);
    for (unsigned j = 0; j < 3; j++)
        for (unsigned m = 0; m < waits; m++)
            check_wait(i + 1 + waits * j + m, PAR_WAIT_REST, (uint8_t)(3 - j));
    return 1 + 3 * waits;
}

/* No segment left (pros_seg_count = 0): two passes, each ending in a wait for an interrupt at the back edge (the flag
   comes with the second), then a third that sees the flag and enters the tight loop ($B8BB-$B8CF): par_frames_left
   3 -> 2 -> 1 gives two frames, each par_step_tracks and frm_finish_frame (both C), the flag cleared and waited for
   again at $B8C9 (two waits each); 0 ends it. The rest frames, the flag every 2nd wait. No event (cmd_ev_count 0) and
   no reply (ser_status_due 0). The registers flow: par_step_tracks leaves A = pros_play_pitch >> 2 = 0, X = 0,
   Y = 10, which frm_finish_frame keeps, into isr_wait_frame; each rest frame leaves A = frm_flags, X = 0
   (frm_snapshot's) and Y = $0D (frm_transition_flags'), which par_play returns. frm_rest_frame is $77 and zeros: the
   first rest frame's ALF goes from 0 (the frames before) to $77, an 8-bit difference of $89, so frm_flags = $C0
   (bit 7: $89 - 12 has bit 7 clear; bit 6: $89 - 28 too); the next two change nothing (0). The fill frame ends as
   the rest frame with b11 = @M 0 raised to 47 - 2 x 0 and b13 = 0. */
static void test_play_tail(void)
{
    setup();
```

**`tests/test_fw_par.c`**: replace

```c
    g_flag_every = 2;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    static const uint16_t k[] = {
        ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,   /* pass 1: $B89F, $B8AB (the flag comes) */
        ROM_ISR_PAR_READY,                      /* pass 2: $B89F, then $B8B1-$B8B9 */
        ROM_ISR_PAR_READY,                      /* frame 1: $B8C9 */
        ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,   /* frame 2: $B8C9 twice */
    };
    check_order(0, k, 6);
    check_play_call(0, ROM_ISR_PAR_READY, 0x11, 0, 0x33, 3);
    check_play_call(2, ROM_ISR_PAR_READY, 0x11, 0, 0x33, 3);
    check_play_call(3, ROM_ISR_PAR_READY, 0, 0, FRAME_PITCH, 2);   /* par_step_tracks' A, X, Y */
    check_play_call(4, ROM_ISR_PAR_READY, 0, 0, FRAME_PITCH, 1);
    CHECK_EQ(check_rest_frames(6, 2), 8);                           /* the flag every 2nd call */
    check_play_call(6, ROM_ISR_WAIT_FRAME, 0, 0, FRAME_PITCH, 0);
    check_play_call(7, ROM_ISR_PAR_READY, 0xC0, 0, 0x0D, 3);         /* the first rest frame's frm_flags */
    check_play_call(9, ROM_ISR_PAR_READY, 0, 0, 0x0D, 2);
    check_play_call(11, ROM_ISR_PAR_READY, 0, 0, 0x0D, 1);
    check_play_call(13, ROM_ISR_PAR_READY, 0, 0, 0x0D, 0);           /* $B8F7 */
    CHECK_EQ(g_ncalls, 14);
    CHECK_EQ(count(ROM_ISR_PAR_READY), 13);
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 0);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0);                                /* cleared after the last rest frame */
    for (int b = 0; b < FRAME_SIZE; b++)
```

with

```c
    g_flag_every = 2;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    check_wait(0, PAR_WAIT_TOP, 3);                                  /* pass 1 */
    check_wait(1, PAR_WAIT_TOP, 3);                                  /* pass 2: the flag comes */
    check_wait(2, PAR_WAIT_LAST, 2);                                 /* frame 1 */
    check_wait(3, PAR_WAIT_LAST, 2);
    check_wait(4, PAR_WAIT_LAST, 1);                                 /* frame 2 */
    check_wait(5, PAR_WAIT_LAST, 1);
    CHECK_EQ(check_rest_frames(6, 2), 7);                            /* the flag every 2nd wait */
    check_play_call(6, ROM_ISR_WAIT_FRAME, 0, 0, FRAME_PITCH, 0);    /* par_step_tracks' A, X, Y */
    CHECK_EQ(g_ncalls, 13);
    CHECK_EQ(g_waits, 12);
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 0);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0);                                /* cleared after the last rest frame */
    for (int b = 0; b < FRAME_SIZE; b++)
```

**`tests/test_fw_par.c`**: replace

```c
   pros_seg_count 1, so it is the last. An @B6 is due at segment 3 (cmd_ev_pos = par_seg_index = 3, cmd_ev_count 1).
   A reply is due (ser_status_due 1) and the transmitter ready (UART_STATUS bit 1): the C ser_tx_service at every
   pass, which reads P0 (the host line low) and keeps A, X and Y.
   The flag every 2nd isr_par_ready.
   Pass 1: the event (cmd_run_event with A = par_seg_index's high byte 0, X = cmd_ev_next 0): cmd_voicing = 6,
   cmd_ev_next = 1; the test again: X = 1, not below cmd_ev_count. ser_tx_service and isr_par_ready get the
   handler's A = 6 and Y and X = 1; isr_par_ready twice (the flag comes). Pass 2: par_frames_left 1 -> 0 ($B8FB):
   par_next_segment (C): par_frames_left = 2, pros_seg_count 0, par_seg_index 4; A = 2 ($B903). $B908: isr_par_ready,
   par_step_tracks, frm_finish_frame. Pass 3: no flag, pass 4: the flag; no segment left: the tight loop, 2 -> 1: one
   frame; 1 -> 0: the end. */
static void test_play_segment(void)
{
    read_setup();
```

with

```c
   pros_seg_count 1, so it is the last. An @B6 is due at segment 3 (cmd_ev_pos = par_seg_index = 3, cmd_ev_count 1).
   A reply is due (ser_status_due 1) and the transmitter ready (UART_STATUS bit 1): the C ser_tx_service at every
   pass, which reads P0 (the host line low) and keeps A, X and Y.
   The flag every 2nd wait.
   Pass 1: the event (cmd_run_event with A = par_seg_index's high byte 0, X = cmd_ev_next 0): cmd_voicing = 6,
   cmd_ev_next = 1; the test again: X = 1, not below cmd_ev_count; no flag, a wait. Pass 2: a wait (the flag comes).
   Pass 3: par_frames_left 1 -> 0 ($B8FB): par_next_segment (C): par_frames_left = 2, pros_seg_count 0,
   par_seg_index 4; A = 2 ($B903). $B908: isr_par_ready, par_step_tracks, frm_finish_frame. Passes 4 and 5: a wait
   each (the flag comes with the second). Pass 6: no segment left: the tight loop, 2 -> 1: one frame (two waits at
   $B8C9); 1 -> 0: the end. */
static void test_play_segment(void)
{
    read_setup();
```

**`tests/test_fw_par.c`**: replace

```c
    g_flag_every = 2;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    static const uint16_t k[] = {
        ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,                                          /* pass 1 */
        ROM_ISR_PAR_READY,                                                             /* pass 2 */
        ROM_ISR_PAR_READY,                                                             /* $B908 */
        ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,                                          /* pass 3 */
        ROM_ISR_PAR_READY,                                                             /* pass 4 */
        ROM_ISR_PAR_READY,                                                             /* the last frame: $B8C9 */
    };
    check_order(0, k, 8);
    CHECK_EQ(g_p0_reads, 4);                                     /* ser_tx_service at each of the four passes */
    check_play_call(0, ROM_ISR_PAR_READY, 6, 1, 0x33, 1);        /* the handler's A, cmd_ev_next from $B877 */
    check_play_call(2, ROM_ISR_PAR_READY, 6, 1, 0x33, 1);
    CHECK_EQ(g_calls[3].frames, 2);                              /* par_next_segment's duration */
    check_play_call(3, ROM_ISR_PAR_READY, 2, 10, 0, 2);          /* A = par_frames_left ($B903), par_next_segment's
                                                                    X = 10 and Y = 0 */
    check_play_call(4, ROM_ISR_PAR_READY, 0x18, 1, FRAME_PITCH, 2);     /* the pitch $55 to $70 over 2 frames:
                                                                       ($70 + $55) >> 1 = $62, b10 = $62 >> 2: A of
                                                                       par_step_tracks, kept by frm_finish_frame;
                                                                       X = cmd_ev_next ($B877) */
    check_play_call(7, ROM_ISR_PAR_READY, 0x1C, 0, FRAME_PITCH, 1);     /* then over 1: $70, b10 = $1C */
    CHECK_EQ(check_rest_frames(8, 2), 8);
    CHECK_EQ(g_ncalls, 16);
    CHECK_EQ(g_ram[CMD_VOICING], 6);
    CHECK_EQ(g_ram[CMD_EV_NEXT], 1);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 4);
```

with

```c
    g_flag_every = 2;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    check_wait(0, PAR_WAIT_TOP, 1);                              /* pass 1 */
    check_wait(1, PAR_WAIT_TOP, 1);                              /* pass 2: the flag comes */
    check_wait(2, PAR_WAIT_TOP, 2);                              /* pass 4: par_next_segment's duration */
    check_wait(3, PAR_WAIT_TOP, 2);                              /* pass 5: the flag comes */
    check_wait(4, PAR_WAIT_LAST, 1);                             /* the last frame */
    check_wait(5, PAR_WAIT_LAST, 1);
    CHECK_EQ(g_p0_reads, 6);                                     /* ser_tx_service at each of the six passes */
    CHECK_EQ(check_rest_frames(6, 2), 7);
    check_play_call(6, ROM_ISR_WAIT_FRAME, 0x1C, 0, FRAME_PITCH, 0);   /* the pitch $55 to $70 over 2 frames, then
                                                                    $70: b10 = $1C, A of par_step_tracks, kept by
                                                                    frm_finish_frame */
    CHECK_EQ(g_ncalls, 13);
    CHECK_EQ(g_ram[CMD_VOICING], 6);
    CHECK_EQ(g_ram[CMD_EV_NEXT], 1);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 4);
```

**`tests/test_fw_par.c`**: replace

```c
    CHECK(!g_f.fault);
}

/* A mute: ser_abort bit 0 arrives with the flag at the first isr_par_ready. The abort test ($B8A2) comes before the
   flag test, so par_play goes straight to the end: no frame is built (par_frames_left is only set to 3 and counted
   down; the fill frame becomes the rest frame, zeros here, with b11 = 47 - 2 x 0, and frm_finish_frame would have
   written b16 = $82), pros_seg_count is kept, and ser_abort stays set (boot_get_clause clears it).
   With UART_STATUS bit 1 clear (the transmitter busy, $B895 BBC1) the due reply is not served. */
static void test_play_abort(void)
{
```

with

```c
    CHECK(!g_f.fault);
}

/* A mute: ser_abort bit 0 arrives with the flag at the first wait. The abort test ($B8A2) comes before the flag test,
   so par_play goes straight to the end: no frame is built (par_frames_left is only set to 3 and counted down; the
   fill frame becomes the rest frame, zeros here, with b11 = 47 - 2 x 0, and frm_finish_frame would have written b16
   = $82), pros_seg_count is kept, and ser_abort stays set (boot_get_clause clears it).
   With UART_STATUS bit 1 clear (the transmitter busy, $B895 BBC1) the due reply is not served. */
static void test_play_abort(void)
{
```

**`tests/test_fw_par.c`**: replace

```c
    g_abort_at = 1;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    check_play_call(0, ROM_ISR_PAR_READY, 0x11, 0, 0x33, 4);
    CHECK_EQ(check_rest_frames(1, 1), 5);
    check_play_call(1, ROM_ISR_WAIT_FRAME, 0x11, 0, 0x33, 4);
    CHECK_EQ(g_ncalls, 6);
    CHECK_EQ(g_p0_reads, 0);                                     /* no ser_tx_service: it would read P0 */
    for (int b = 0; b < FRAME_SIZE; b++)
        CHECK_EQ(g_ram[ISR_FRAME_B + b], b == FRAME_MARK_SPACE ? 47 : 0);
```

with

```c
    g_abort_at = 1;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    check_wait(0, PAR_WAIT_TOP, 4);
    CHECK_EQ(check_rest_frames(1, 1), 4);
    check_play_call(1, ROM_ISR_WAIT_FRAME, 0x11, 0, 0x33, 4);   /* the entry A and Y, X = cmd_ev_next ($B877) */
    CHECK_EQ(g_ncalls, 5);
    CHECK_EQ(g_p0_reads, 0);                                     /* no ser_tx_service: it would read P0 */
    for (int b = 0; b < FRAME_SIZE; b++)
        CHECK_EQ(g_ram[ISR_FRAME_B + b], b == FRAME_MARK_SPACE ? 47 : 0);
```

**`tests/test_fw_par.c`**: replace

```c
}

/* A segment of 0 frames (spec 2.6; unreachable, every duration is at least 1). With segments left, the DEC at $B8FB
   from 0 gives $FF and a frame is built ($B908); then the mute ends it. Entered with par_frames_left = 0 and
   pros_seg_count 1. */
static void test_play_zero_frames_main(void)
{
    setup();
    fill_frame_b();
    fw_wr16(&g_f, PROS_SEG_COUNT, 1);
    g_flag_every = 1;
    g_abort_at = 3;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    static const uint16_t k[] = { ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY };
    check_order(0, k, 3);
    check_play_call(1, ROM_ISR_PAR_READY, 0x11, 0, 1, 0xFF);   /* $B908 after the DEC; Y = pros_seg_count ($B8B1) */
    CHECK_EQ(check_rest_frames(3, 1), 5);
    CHECK_EQ(g_ncalls, 8);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 1);   /* par_next_segment was not called */
    CHECK(!g_f.fault);
}
```

with

```c
}

/* A segment of 0 frames (spec 2.6; unreachable, every duration is at least 1). With segments left, the DEC at $B8FB
   from 0 gives $FF and a frame is built ($B908); then the mute, with the second wait's flag, ends it. Entered with
   par_frames_left = 0 and pros_seg_count 1. */
static void test_play_zero_frames_main(void)
{
    setup();
    fill_frame_b();
    fw_wr16(&g_f, PROS_SEG_COUNT, 1);
    g_flag_every = 1;
    g_abort_at = 2;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    check_wait(0, PAR_WAIT_TOP, 0);
    check_wait(1, PAR_WAIT_TOP, 0xFF);                         /* after the frame built at $B908 */
    CHECK_EQ(check_rest_frames(2, 1), 4);
    CHECK_EQ(g_ncalls, 6);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 1);   /* par_next_segment was not called */
    CHECK(!g_f.fault);
}
```

**`tests/test_fw_par.c`**: replace

```c
/* par_next_segment leaving par_frames_left = 0 ($B906 BEQ back to $B8B1): the T6 segment with the duration byte 0;
   its current and next phonemes have the same targets $11-$1A (@u offsets 0, @B8: P10 unscaled), so every track
   is at its boundary and the 0-step interpolations of par_step_tracks return their targets. pros_seg_count 2 -> 1:
   segments are left, so the DEC at $B8FB makes $FF and a frame is built; the mute at the 3rd isr_par_ready ends
   it. */
static void test_play_zero_frames_segment(void)
{
    read_setup();
```

with

```c
/* par_next_segment leaving par_frames_left = 0 ($B906 BEQ back to $B8B1): the T6 segment with the duration byte 0;
   its current and next phonemes have the same targets $11-$1A (@u offsets 0, @B8: P10 unscaled), so every track
   is at its boundary and the 0-step interpolations of par_step_tracks return their targets. pros_seg_count 2 -> 1:
   segments are left, so the DEC at $B8FB makes $FF and a frame is built; the mute at the 2nd wait ends it. */
static void test_play_zero_frames_segment(void)
{
    read_setup();
```

**`tests/test_fw_par.c`**: replace

```c
    fw_wr16(&g_f, PROS_SEG_COUNT, 2);
    g_ram[PAR_FRAMES_LEFT] = 1;
    g_flag_every = 1;
    g_abort_at = 3;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    static const uint16_t k[] = { ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY };
    check_order(0, k, 3);
    check_play_call(1, ROM_ISR_PAR_READY, 0, 10, 1, 0xFF);   /* A = 0 ($B903), X = 10 (par_next_segment), Y = 1:
                                                               pros_seg_count again at $B8B1 */
    CHECK_EQ(check_rest_frames(3, 1), 5);
    CHECK_EQ(g_ncalls, 8);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 1);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 4);
    CHECK(!g_f.fault);
}

/* No segment left and par_frames_left = 0: the DEC at $B8BB gives $FF, and the tight loop builds 255 frames
   ($FF ... 1), each par_step_tracks and frm_finish_frame (C) and one isr_par_ready (the flag at every call), before
   the end. */
static void test_play_zero_frames_tail(void)
{
```

with

```c
    fw_wr16(&g_f, PROS_SEG_COUNT, 2);
    g_ram[PAR_FRAMES_LEFT] = 1;
    g_flag_every = 1;
    g_abort_at = 2;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    check_wait(0, PAR_WAIT_TOP, 1);
    check_wait(1, PAR_WAIT_TOP, 0xFF);                       /* after the frame built at $B908 */
    CHECK_EQ(check_rest_frames(2, 1), 4);
    CHECK_EQ(g_ncalls, 6);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 1);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 4);
    CHECK(!g_f.fault);
}

/* No segment left and par_frames_left = 0: the DEC at $B8BB gives $FF, and the tight loop builds 255 frames
   ($FF ... 1), each par_step_tracks and frm_finish_frame (C) and one wait at $B8C9 (the flag at every wait), before
   the end. */
static void test_play_zero_frames_tail(void)
{
```

**`tests/test_fw_par.c`**: replace

```c
    g_flag_every = 1;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    check_play_call(0, ROM_ISR_PAR_READY, 0x11, 0, 0x33, 0);
    check_play_call(1, ROM_ISR_PAR_READY, 0, 0, FRAME_PITCH, 0xFF);     /* frame $FF's wait */
    check_play_call(255, ROM_ISR_PAR_READY, 0, 0, FRAME_PITCH, 1);      /* frame 1's */
    CHECK_EQ(count(ROM_ISR_PAR_READY), 1 + 255 + 3 + 1);
    CHECK_EQ(check_rest_frames(256, 1), 5);
    CHECK_EQ(g_ncalls, 261);
    CHECK(!g_f.fault);
}
```

with

```c
    g_flag_every = 1;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    check_wait(0, PAR_WAIT_TOP, 0);
    check_wait(1, PAR_WAIT_LAST, 0xFF);                                 /* frame $FF's wait */
    check_wait(255, PAR_WAIT_LAST, 1);                                  /* frame 1's */
    CHECK_EQ(count(PAR_WAIT_LAST), 255);
    CHECK_EQ(check_rest_frames(256, 1), 4);
    CHECK_EQ(g_ncalls, 260);
    CHECK(!g_f.fault);
}
```

**`tests/test_fw_par.c`**: replace

```c
    }
}

/* boot_loop_play that plays (FW_UTT_CONTENT $FF): isr_par_hold with A = $FF and the entry X and Y; par_play_start
   (C, test_play_start's voice A set-up: Y = the pause phoneme $3B, A = 0); isr_speech_start (A = X = 0 out);
   isr_volume_half; isr_wait_frame; isr_set_volume with A = cmd_volume ($B2C2); par_play (C), here ended by a mute
   at its first isr_par_ready; isr_volume_half; isr_wait_frame; isr_set_volume with A = 0 ($B2D0);
   isr_speech_stop, whose registers it returns. cmd_volume 9: the half is 4, P3 = (P3 & $87) | (4 ^ $F) << 3. The
   rest frames go to isr_frame_b (isr_fill_ptr as par_setup_voice leaves it); par_play returns the last one's
   frm_flags, 0. */
static void test_loop_play(void)
{
```

with

```c
    }
}

/* boot_loop_play that plays (FW_UTT_CONTENT $FF): isr_par_hold (C: nothing in serial mode); par_play_start (C,
   test_play_start's voice A set-up: Y = the pause phoneme $3B, A = 0); isr_speech_start (ROM: A = X = 0 out);
   isr_volume_half (C); isr_wait_frame (ROM); isr_set_volume (C) with A = cmd_volume ($B2C2); par_play (C), here
   ended by a mute at its first wait; isr_volume_half; isr_wait_frame; isr_set_volume with A = 0 ($B2D0);
   isr_speech_stop (ROM), whose registers it returns. cmd_volume 9: the half is 4, P3 = (P3 & $87) | (4 ^ $F) << 3.
   The rest frames go to isr_frame_b (isr_fill_ptr as par_setup_voice leaves it); par_play returns the last one's
   frm_flags, 0. */
static void test_loop_play(void)
{
```

**`tests/test_fw_par.c`**: replace

```c
    g_abort_at = 1;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    boot_loop_play(&g_f, &r);
    check_call(0, ROM_ISR_PAR_HOLD, 0xFF, 0x22, 0x33);
    check_call(1, ROM_ISR_SPEECH_START, 0, 0x22, 0x3B);          /* par_play_start's A and Y */
    check_call(2, ROM_ISR_VOLUME_HALF, 0, 0, 0x3B);              /* isr_speech_start's A and X */
    check_call(3, ROM_ISR_WAIT_FRAME, 0xDF, 0, 0x3B);            /* P3: $87 | (4 ^ $F) << 3 = $87 | $58 */
    check_call(4, ROM_ISR_SET_VOLUME, 9, 0, 0x3B);               /* cmd_volume */
    check_call(5, ROM_ISR_PAR_READY, 0xB7, 0, 0x3B);             /* par_play: P3 $87 | (9 ^ $F) << 3 = $87 | $30;
                                                                    X = cmd_ev_next 0 */
    CHECK_EQ(check_rest_frames(6, 1), 5);
    check_call(11, ROM_ISR_VOLUME_HALF, 0, 0, 0x0D);             /* par_play's exit registers */
    check_call(12, ROM_ISR_WAIT_FRAME, 0xDF, 0, 0x0D);
    check_call(13, ROM_ISR_SET_VOLUME, 0, 0, 0x0D);
    check_call(14, ROM_ISR_SPEECH_STOP, 0xFF, 0, 0x0D);          /* P3 $87 | $F << 3 */
    CHECK_EQ(g_ncalls, 15);
    CHECK_EQ(g_p3, 0xFF);
    CHECK_EQ(g_ram[PAR_CUR_PHONEME], 0x3B);                      /* par_play_start ran */
    CHECK_EQ(g_ram[PAR_CUR_RANK], 31);
```

with

```c
    g_abort_at = 1;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    boot_loop_play(&g_f, &r);
    check_call(0, ROM_ISR_SPEECH_START, 0, 0x22, 0x3B);          /* par_play_start's A and Y */
    check_call(1, ROM_ISR_WAIT_FRAME, 0xDF, 0, 0x3B);            /* P3: $87 | (4 ^ $F) << 3 = $87 | $58 */
    check_wait(2, PAR_WAIT_TOP, 1);                              /* par_play (par_play_start's 1 frame): the mute */
    CHECK_EQ(check_rest_frames(3, 1), 4);
    check_call(3, ROM_ISR_WAIT_FRAME, 0xB7, 0, 0x3B);            /* P3 $87 | (9 ^ $F) << 3 = $87 | $30; X =
                                                                    cmd_ev_next 0 */
    check_call(7, ROM_ISR_WAIT_FRAME, 0xDF, 0, 0x0D);            /* par_play's X and Y */
    check_call(8, ROM_ISR_SPEECH_STOP, 0xFF, 0, 0x0D);           /* P3 $87 | $F << 3 */
    CHECK_EQ(g_ncalls, 9);
    static const uint8_t p3[] = { 0xDF, 0xB7, 0xDF, 0xFF };      /* half, cmd_volume, half, 0 */
    CHECK_EQ(g_np3, 4);
    for (unsigned i = 0; i < 4; i++)
        CHECK_EQ(g_p3_log[i], p3[i]);
    CHECK_EQ(g_p3, 0xFF);
    CHECK_EQ(g_ram[PAR_CUR_PHONEME], 0x3B);                      /* par_play_start ran */
    CHECK_EQ(g_ram[PAR_CUR_RANK], 31);
```

**`tests/test_fw_pros.c`**: replace

```c
static fw_host g_host;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;

/* Records every ROM call; isr_par_ready returns the registers unchanged (it preserves A, X and Y), any other call
   fails. */
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
    return addr == ROM_ISR_PAR_READY ? FW_CALL_RETURNED : FW_CALL_FAILED;
}

/* Zeroed RAM, the mock host, the converted image, no calls recorded. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    g_host.ctx = &g_f;
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = NULL;   /* nothing in the segment generator drives P2 */
    g_ncalls = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
}

static unsigned count(uint16_t addr)
```

with

```c
static fw_host g_host;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;
static uint8_t g_p0;   /* the port P0 the C isr_par_ready reads and writes in parallel mode */

static void record(uint16_t addr, const fw_regs *r, uint64_t budget)
{
    if (g_ncalls < MAX_CALLS) {
        rom_call_record *c = &g_calls[g_ncalls];
        c->addr = addr;
        c->a = r ? r->a : 0;
        c->x = r ? r->x : 0;
        c->y = r ? r->y : 0;
        c->budget = budget;
    }
    g_ncalls++;
}

/* Records every ROM call and fails it: the segment generator and the passes before it make none (isr_par_ready is C
   since M4.6b). */
static fw_call_result mock_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    (void)ctx;
    record(addr, r, budget);
    return FW_CALL_FAILED;
}

/* The C isr_par_ready in parallel mode (setup sets boot_dip bit 0): SEB3 P0, one read and one write of P0 ($F6),
   recorded as a call of ROM_ISR_PAR_READY without registers (the C passes them to nothing). Any other SFR access
   fails the test. */
static uint8_t mock_read_sfr(void *ctx, uint8_t addr)
{
    (void)ctx;
    CHECK_EQ(addr, 0xF6);
    return g_p0;
}

static void mock_write_sfr(void *ctx, uint8_t addr, uint8_t value)
{
    (void)ctx;
    CHECK_EQ(addr, 0xF6);
    CHECK(value & 0x08);
    g_p0 = value;
    record(ROM_ISR_PAR_READY, NULL, 0);
}

/* Zeroed RAM but boot_dip = 1 (parallel mode, so that each isr_par_ready is recorded), the mock host, the converted
   image, no calls recorded. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    g_host.ctx = &g_f;
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = NULL;   /* nothing in the segment generator drives P2 */
    g_host.read_sfr = mock_read_sfr;
    g_host.write_sfr = mock_write_sfr;
    g_ncalls = 0;
    g_p0 = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
    g_ram[BOOT_DIP] = 1;
}

static unsigned count(uint16_t addr)
```

**`tests/test_fw_pros.c`**: replace

```c
    return n;
}

/* Record i is a call of addr with A, X, Y and the strict budget. */
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

/* The hook of name sits at addr and declares outputs (pinned here; diff_lts compares them against the ROM). */
```

with

```c
    return n;
}

/* Record i is an isr_par_ready (the C's: its registers reach nothing, so none is recorded). */
static void check_ready(unsigned i)
{
    CHECK(i < g_ncalls && i < MAX_CALLS);
    if (i >= g_ncalls || i >= MAX_CALLS)
        return;
    CHECK_EQ(g_calls[i].addr, ROM_ISR_PAR_READY);
}

/* The hook of name sits at addr and declares outputs (pinned here; diff_lts compares them against the ROM). */
```

**`tests/test_fw_pros.c`**: replace

```c
    g_ram[CMD_PITCH_STEP] = 0x08;
    pros_append_segment(&g_f, 0xA1, 0xB2, 0xC3);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_ISR_PAR_READY, 0xA1, 0xB2, 0xC3);   /* first, with the entry registers */
    check_segment(0x105, 0x12, 0x09, 0x78);
    CHECK_EQ(g_ram[PROS_LAST_TARGET], 0x78);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0106);
```

with

```c
    g_ram[CMD_PITCH_STEP] = 0x08;
    pros_append_segment(&g_f, 0xA1, 0xB2, 0xC3);
    CHECK_EQ(g_ncalls, 1);
    check_ready(0);                                     /* first */
    check_segment(0x105, 0x12, 0x09, 0x78);
    CHECK_EQ(g_ram[PROS_LAST_TARGET], 0x78);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0106);
```

**`tests/test_fw_pros.c`**: replace

```c
    g_ram[PH_DURATION] = 0x05;
    g_ram[PROS_TARGET] = 0x60;
    pros_append_segment(&g_f, 0x11, 0x22, 0x33);
    check_call(0, ROM_ISR_PAR_READY, 0x11, 0x22, 0x33);
    check_segment(0x1FF, 0x21, 0x05, 0x60);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0200);
    check_pages(2);
```

with

```c
    g_ram[PH_DURATION] = 0x05;
    g_ram[PROS_TARGET] = 0x60;
    pros_append_segment(&g_f, 0x11, 0x22, 0x33);
    check_ready(0);
    check_segment(0x1FF, 0x21, 0x05, 0x60);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0200);
    check_pages(2);
```

**`tests/test_fw_pros.c`**: replace

```c
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0101);
    check_pages(1);
    CHECK_EQ(count(ROM_ISR_PAR_READY), 1);
    check_call(0, ROM_ISR_PAR_READY, 0x80, 1, 2);
    CHECK(!g_f.fault);
}
```

with

```c
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x0101);
    check_pages(1);
    CHECK_EQ(count(ROM_ISR_PAR_READY), 1);
    check_ready(0);
    CHECK(!g_f.fault);
}
```

**`tests/test_fw_pros.c`**: replace

```c
    g_ram[CMD_PHRASE_PAUSE] = 8;              /* @D8: two pauses of 8 after '.' */
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ncalls, 7);
    check_call(0, ROM_ISR_PAR_READY, 0xFF, 0, 4);       /* position 0; X and Y as pros_declination leaves them */
    check_call(1, ROM_ISR_PAR_READY, 0, 0, 0);          /* position 1: the space's list ended at its item (@Q0) */
    check_call(2, ROM_ISR_PAR_READY, 9, 1, 9);          /* M's segment: (60 + 18) / 8 */
    check_call(3, ROM_ISR_PAR_READY, 0xFF, 1, 3);       /* position 2: M's list end */
    check_call(4, ROM_ISR_PAR_READY, 20, 100, 20);      /* E's segment: X = the last lengthening index */
    check_call(5, ROM_ISR_PAR_READY, 0xFF, 100, 3);
    check_call(6, ROM_ISR_PAR_READY, 0, 3, '.');        /* position 4 (the end), after the punctuation */
    /* M: $80 / 8 + U 3 + 105 = $7C. E: ($7C >> 3 = 15, carry 1) + W 10 + Z 1 + final 6 + 105 = $8A */
    check_segment(0, 0x2A, 9, 0x7C);
    check_segment(1, 0x10, 20, 0x8A);
```

with

```c
    g_ram[CMD_PHRASE_PAUSE] = 8;              /* @D8: two pauses of 8 after '.' */
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ncalls, 7);
    check_ready(0);                                     /* position 0 */
    check_ready(1);                                     /* position 1: the space's list ended at its item (@Q0) */
    check_ready(2);                                     /* M's segment */
    check_ready(3);                                     /* position 2: M's list end */
    check_ready(4);                                     /* E's segment */
    check_ready(5);
    check_ready(6);                                     /* position 4 (the end), after the punctuation */
    /* M: $80 / 8 + U 3 + 105 = $7C. E: ($7C >> 3 = 15, carry 1) + W 10 + Z 1 + final 6 + 105 = $8A */
    check_segment(0, 0x2A, 9, 0x7C);
    check_segment(1, 0x10, 20, 0x8A);
```

**`tests/test_fw_pros.c`**: replace

```c
    g_ram[PROS_SYM_SHORTEN + 0] = 0xFF;                 /* (255 + 1) / 2 = 0 in 8 bits: DEY/BNE runs 256 steps */
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[PH_DUR_RANGE], 5);                   /* x0.9 down to its fixed point (0 steps would leave 45) */
    check_call(1, ROM_ISR_PAR_READY, 10, 0, 10);        /* (80 + 5) / 8; X = pros_pos (no lengthening) */
    CHECK_EQ(g_ram[PROS_SEG_DURATION + 0], 10);

    gen_setup("S");
    g_ram[PROS_SYM_LENGTHEN + 0] = 0xFF;                /* half-count 0: ph_lengthen_count(0) = $60, 96 steps */
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[PH_DUR_RANGE], 255);                 /* x1.1 up to 255 */
    check_call(1, ROM_ISR_PAR_READY, 31, 255, 31);      /* 80 + 255 capped at 255, / 8; X = the last index */
    CHECK_EQ(g_ram[PROS_SEG_DURATION + 0], 31);
    CHECK(!g_f.fault);
}
```

with

```c
    g_ram[PROS_SYM_SHORTEN + 0] = 0xFF;                 /* (255 + 1) / 2 = 0 in 8 bits: DEY/BNE runs 256 steps */
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[PH_DUR_RANGE], 5);                   /* x0.9 down to its fixed point (0 steps would leave 45) */
    check_ready(1);                                     /* the segment's */
    CHECK_EQ(g_ram[PROS_SEG_DURATION + 0], 10);

    gen_setup("S");
    g_ram[PROS_SYM_LENGTHEN + 0] = 0xFF;                /* half-count 0: ph_lengthen_count(0) = $60, 96 steps */
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[PH_DUR_RANGE], 255);                 /* x1.1 up to 255 */
    check_ready(1);                                     /* the segment's */
    CHECK_EQ(g_ram[PROS_SEG_DURATION + 0], 31);
    CHECK(!g_f.fault);
}
```

**`tests/test_fw_pros.c`**: replace

```c
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 2);         /* M and the two end pauses */
    check_segment(1, 0x3B, 4, g_ram[PROS_SEG_PITCH + 0]);
    check_segment(2, 0x3B, 0x80, g_ram[PROS_SEG_PITCH + 0]);
    check_call(3, ROM_ISR_PAR_READY, 0, 1, '.');
    CHECK(!g_f.fault);
}
```

with

```c
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 2);         /* M and the two end pauses */
    check_segment(1, 0x3B, 4, g_ram[PROS_SEG_PITCH + 0]);
    check_segment(2, 0x3B, 0x80, g_ram[PROS_SEG_PITCH + 0]);
    check_ready(3);
    CHECK(!g_f.fault);
}
```

**`tests/test_fw_pros.c`**: replace

```c
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0xFF);
    check_segment(0, 0x3B, 5, 0x80);                    /* pitch: pros_last_target as initialised */
    CHECK_EQ(g_ram[FW_TMP_78], 0);
    check_call(1, ROM_ISR_PAR_READY, 0x80, 0, 0);       /* A as ph_append_pause leaves it, X = pros_pos, Y = $78 */
    gen_setup("X");
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0xFF);
```

with

```c
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0xFF);
    check_segment(0, 0x3B, 5, 0x80);                    /* pitch: pros_last_target as initialised */
    CHECK_EQ(g_ram[FW_TMP_78], 0);
    check_ready(1);                                     /* after the word pause */
    gen_setup("X");
    gen_run(&a, &x, &y);
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0xFF);
```

**`tests/test_fw_pros.c`**: replace

```c
    gen_run(&a, &x, &y);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 2);
    CHECK_EQ(g_ram[PROS_SEG_PHONEME + 0], 0x40);
    check_call(1, ROM_ISR_PAR_READY, 10, 0, 10);        /* (40 + 40) / 8 */
    check_call(2, ROM_ISR_PAR_READY, 0x10, 0, 10);      /* A = pros_sym_flag at the JMP */

    gen_setup("~M");                                    /* @H1: a function word makes no segment */
    g_ram[CMD_HYPER] = 1;
```

with

```c
    gen_run(&a, &x, &y);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 2);
    CHECK_EQ(g_ram[PROS_SEG_PHONEME + 0], 0x40);
    check_ready(1);                                     /* the closure's segment */
    check_ready(2);                                     /* the JMP $D152 */

    gen_setup("~M");                                    /* @H1: a function word makes no segment */
    g_ram[CMD_HYPER] = 1;
```

**`tests/test_fw_pros.c`**: replace

```c
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 1);         /* the two end pauses only */
    CHECK_EQ(g_ram[PROS_FUNC_WORD], 1);
    CHECK_EQ(g_ncalls, 3);
    check_call(1, ROM_ISR_PAR_READY, 0, 0, 0);          /* after '~' (class 0): A = Y = pros_class */
    check_call(2, ROM_ISR_PAR_READY, 1, 1, 0x80);       /* after M: A = pros_func_word, Y = pros_class */
    CHECK(!g_f.fault);
}
```

with

```c
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 1);         /* the two end pauses only */
    CHECK_EQ(g_ram[PROS_FUNC_WORD], 1);
    CHECK_EQ(g_ncalls, 3);
    check_ready(1);                                     /* after '~' (class 0) */
    check_ready(2);                                     /* after M, no segment */
    CHECK(!g_f.fault);
}
```

**`tests/test_fw_pros.c`**: replace

```c
    CHECK(!g_f.fault);
}

/* " ME, ~TO{." walked from position 9 down to 1, then the exit block at 0: the registers at isr_par_ready ($CF28) on
   each path, the counts, the accent, the nucleus marks and the state left */
static void test_phrases_clause(void)
{
    fw_regs r;
```

with

```c
    CHECK(!g_f.fault);
}

/* " ME, ~TO{." walked from position 9 down to 1, then the exit block at 0: isr_par_ready ($CF28) on each path, the
   counts, the accent, the nucleus marks and the state left */
static void test_phrases_clause(void)
{
    fw_regs r;
```

**`tests/test_fw_pros.c`**: replace

```c
    g_ram[PROS_FEATURES] = 0x5A;
    phrase_run(0x33, &r);
    CHECK_EQ(g_ncalls, 9);
    check_call(0, ROM_ISR_PAR_READY, 0, 9, 0x33);       /* the first: A = pros_features (0 at entry), the entry Y */
    check_call(1, ROM_ISR_PAR_READY, 0, 8, 16);         /* after '.': Y = pros_declination's (falling: the step) */
    check_call(2, ROM_ISR_PAR_READY, 0, 7, '{');        /* after '{': Y = the symbol */
    check_call(3, ROM_ISR_PAR_READY, 0x83, 6, 0x83);    /* after O: Y = its features (LDY at $D068) */
    check_call(4, ROM_ISR_PAR_READY, 0x60, 5, 0x60);    /* after T */
    check_call(5, ROM_ISR_PAR_READY, 0, 4, '~');        /* after '~', a marker: Y = the symbol */
    check_call(6, ROM_ISR_PAR_READY, 0, 3, ' ');        /* after the space */
    check_call(7, ROM_ISR_PAR_READY, 0, 2, 4);          /* after ',': rising, Y = the step before its negation */
    check_call(8, ROM_ISR_PAR_READY, 0x83, 1, 0x83);    /* after E */
    CHECK_EQ(r.a, 2);                                   /* the exit block: A = pros_word_pos, X = pros_pos */
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x49);                                /* M's features */
```

with

```c
    g_ram[PROS_FEATURES] = 0x5A;
    phrase_run(0x33, &r);
    CHECK_EQ(g_ncalls, 9);
    check_ready(0);                                     /* the first */
    check_ready(1);                                     /* after '.' */
    check_ready(2);                                     /* after '{' */
    check_ready(3);                                     /* after O */
    check_ready(4);                                     /* after T */
    check_ready(5);                                     /* after '~', a marker */
    check_ready(6);                                     /* after the space */
    check_ready(7);                                     /* after ',' */
    check_ready(8);                                     /* after E */
    CHECK_EQ(r.a, 2);                                   /* the exit block: A = pros_word_pos, X = pros_pos */
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x49);                                /* M's features */
```

**`tests/test_fw_pros.c`**: replace

```c
        CHECK_EQ(g_ram[PROS_FINAL_PARAM], 0xC0 - s - 4);
        CHECK_EQ(g_ram[PROS_DECL_STEP], 1);             /* 1 over the count 1 */
        CHECK_EQ(g_ncalls, 1);
        check_call(0, ROM_ISR_PAR_READY, 0, 1, 0);
        CHECK(!g_f.fault);
    }
    phrase_setup(" -");                                 /* '-' (class 1) keeps the current set */
```

with

```c
        CHECK_EQ(g_ram[PROS_FINAL_PARAM], 0xC0 - s - 4);
        CHECK_EQ(g_ram[PROS_DECL_STEP], 1);             /* 1 over the count 1 */
        CHECK_EQ(g_ncalls, 1);
        check_ready(0);
        CHECK(!g_f.fault);
    }
    phrase_setup(" -");                                 /* '-' (class 1) keeps the current set */
```

**`tests/test_fw_pros.c`**: replace

```c
    CHECK_EQ(g_ram[FW_PROS_ACCENT_PENDING], 0);
    CHECK_EQ(g_ram[FW_PROS_FINAL], 0);
    CHECK_EQ(g_ncalls, 3);
    check_call(1, ROM_ISR_PAR_READY, 0x83, 2, 0x83);
    check_call(2, ROM_ISR_PAR_READY, 0x49, 1, 0x49);

    phrase_setup(" 7");                                 /* a syllabic consonant (features $4B: bit 1, not bit 7) */
    g_ram[PROS_SYM_FLAGS + 1] = PSF_STRESS;
```

with

```c
    CHECK_EQ(g_ram[FW_PROS_ACCENT_PENDING], 0);
    CHECK_EQ(g_ram[FW_PROS_FINAL], 0);
    CHECK_EQ(g_ncalls, 3);
    check_ready(1);
    check_ready(2);

    phrase_setup(" 7");                                 /* a syllabic consonant (features $4B: bit 1, not bit 7) */
    g_ram[PROS_SYM_FLAGS + 1] = PSF_STRESS;
```

**`tests/test_fw_pros.c`**: replace

```c
    g_ram[PROS_SYM_FLAGS + 254] = PSF_STRESS;
    phrase_run(0x5A, &r);
    CHECK_EQ(g_ncalls, 255);
    check_call(0, ROM_ISR_PAR_READY, 0, 255, 0x5A);
    check_call(1, ROM_ISR_PAR_READY, 0, 254, 0);        /* after '.': a level baseline (0 to 0), step 0 */
    check_call(2, ROM_ISR_PAR_READY, 0x83, 253, 0x83);
    check_call(3, ROM_ISR_PAR_READY, 0, 252, 0);        /* after symbol 0 */
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 254], 0x67);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 254], 16);       /* the accent, and the lengthening before '.' */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 8);          /* still before '.': symbol 0 has no features */
```

with

```c
    g_ram[PROS_SYM_FLAGS + 254] = PSF_STRESS;
    phrase_run(0x5A, &r);
    CHECK_EQ(g_ncalls, 255);
    check_ready(0);
    check_ready(1);                                     /* after '.' */
    check_ready(2);
    check_ready(3);                                     /* after symbol 0 */
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 254], 0x67);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 254], 16);       /* the accent, and the lengthening before '.' */
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 8);          /* still before '.': symbol 0 has no features */
```

**`tests/test_fw_pros.c`**: replace

```c
    fw_wr16(&g_f, CMD_EV_POSITIONS, 0x0304);            /* read only when cmd_ev_count is not 0 */
    fw_regs r = stress_run(0x11, 0x22, 0x33);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, ROM_ISR_PAR_READY, 0, 0, 0x33);       /* A = cmd_ev_count (BEQ), X = 0 (TAX), Y = the caller's */
    CHECK_EQ(r.a, 0);
    CHECK_EQ(r.x, 0);                                   /* pros_pos: $FF + 1 */
    CHECK_EQ(r.y, 0x33);
```

with

```c
    fw_wr16(&g_f, CMD_EV_POSITIONS, 0x0304);            /* read only when cmd_ev_count is not 0 */
    fw_regs r = stress_run(0x11, 0x22, 0x33);
    CHECK_EQ(g_ncalls, 1);
    check_ready(0);                                     /* the only one */
    CHECK_EQ(r.a, 0);
    CHECK_EQ(r.x, 0);                                   /* pros_pos: $FF + 1 */
    CHECK_EQ(r.y, 0x33);
```

**`tests/test_fw_pros.c`**: replace

```c
    fw_wr16(&g_f, CMD_EV_POS, 0x7766);
    fw_wr16(&g_f, CMD_EV_POSITIONS, 0x0304);
    r = stress_run(0x11, 0x22, 0x33);
    check_call(0, ROM_ISR_PAR_READY, 0x03, 0, 0x33);    /* A = the high byte, the last one read */
    CHECK_EQ(r.a, 0x03);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0x0304);        /* both bytes of the first position */
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0xFF);              /* LDM #$FF */
```

with

```c
    fw_wr16(&g_f, CMD_EV_POS, 0x7766);
    fw_wr16(&g_f, CMD_EV_POSITIONS, 0x0304);
    r = stress_run(0x11, 0x22, 0x33);
    check_ready(0);                                     /* the only one */
    CHECK_EQ(r.a, 0x03);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0x0304);        /* both bytes of the first position */
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0xFF);              /* LDM #$FF */
```

**`tests/test_fw_pros.c`**: replace

```c
    CHECK(!g_f.fault);
}

/* Every way back to isr_par_ready at $CB1F, with its registers, and the three tn_phonetic rewrites */
static void test_stress_paths(void)
{
    static const uint8_t flags[12] = { 0, PSF_FUNC_WORD, 0, 0, 0, 0, 0, 0, 0, 0, PSF_STRESS, 0 };
```

with

```c
    CHECK(!g_f.fault);
}

/* Every way back to isr_par_ready at $CB1F, and the three tn_phonetic rewrites */
static void test_stress_paths(void)
{
    static const uint8_t flags[12] = { 0, PSF_FUNC_WORD, 0, 0, 0, 0, 0, 0, 0, 0, PSF_STRESS, 0 };
```

**`tests/test_fw_pros.c`**: replace

```c
    stress_setup(" ~}{`|MMA|E?");
    fw_regs r = stress_run(0x11, 0x22, 0x33);
    CHECK_EQ(g_ncalls, 13);
    check_call(0, ROM_ISR_PAR_READY, 0x00, 0, 0x33);    /* the start */
    check_call(1, ROM_ISR_PAR_READY, 0xFF, 0, 0x00);    /* ' ': the word end, A = $FF, Y = features 0 */
    check_call(2, ROM_ISR_PAR_READY, PSF_FUNC_WORD, 1, 0x00);   /* '~' */
    check_call(3, ROM_ISR_PAR_READY, 0xFF, 2, 0x00);    /* '}' */
    check_call(4, ROM_ISR_PAR_READY, 0x00, 3, 0x00);    /* '{' */
    check_call(5, ROM_ISR_PAR_READY, 0x01, 4, 0x00);    /* '`' */
    check_call(6, ROM_ISR_PAR_READY, 0x49, 5, 0x00);    /* '|' before M: A = M's features ($1C09) */
    check_call(7, ROM_ISR_PAR_READY, '|', 6, 0x49);     /* M before M: becomes | */
    check_call(8, ROM_ISR_PAR_READY, 0x49, 7, 0x49);    /* the consonant tail: A = Y = its features */
    check_call(9, ROM_ISR_PAR_READY, 0x83, 8, 0x83);    /* the vowel tail: Y = the next symbol's features */
    check_call(10, ROM_ISR_PAR_READY, 0x45, 9, 0x45);   /* '|' before E: R, a consonant */
    check_call(11, ROM_ISR_PAR_READY, 0x83, 10, 0x00);  /* E before the end: Y = 0 */
    check_call(12, ROM_ISR_PAR_READY, 0xFF, 11, 0x00);  /* '?': the word end */
    CHECK_EQ(r.a, 0xFF);
    CHECK_EQ(r.x, 12);
    CHECK_EQ(r.y, 0x00);
```

with

```c
    stress_setup(" ~}{`|MMA|E?");
    fw_regs r = stress_run(0x11, 0x22, 0x33);
    CHECK_EQ(g_ncalls, 13);
    check_ready(0);                                     /* the start */
    check_ready(1);                                     /* ' ': the word end */
    check_ready(2);                                     /* '~' */
    check_ready(3);                                     /* '}' */
    check_ready(4);                                     /* '{' */
    check_ready(5);                                     /* '`' */
    check_ready(6);                                     /* '|' before M */
    check_ready(7);                                     /* M before M: becomes | */
    check_ready(8);                                     /* the consonant tail */
    check_ready(9);                                     /* the vowel tail */
    check_ready(10);                                    /* '|' before E: R, a consonant */
    check_ready(11);                                    /* E before the end */
    check_ready(12);                                    /* '?': the word end */
    CHECK_EQ(r.a, 0xFF);
    CHECK_EQ(r.x, 12);
    CHECK_EQ(r.y, 0x00);
```

**`tests/test_fw_pros.c`**: replace

```c
    stress_setup("A|A");
    fw_regs r = stress_run(0, 0, 0);
    CHECK_EQ(g_ncalls, 4);
    check_call(1, ROM_ISR_PAR_READY, 0x83, 0, 0x83);
    check_call(2, ROM_ISR_PAR_READY, 0x45, 1, 0x45);
    check_call(3, ROM_ISR_PAR_READY, 0x83, 2, 0x00);
    CHECK_EQ(r.x, 3);
    CHECK(memcmp(g_ram + TN_PHONETIC, "ARA", 3) == 0);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 0], PSF_STRESS);
```

with

```c
    stress_setup("A|A");
    fw_regs r = stress_run(0, 0, 0);
    CHECK_EQ(g_ncalls, 4);
    check_ready(1);
    check_ready(2);
    check_ready(3);
    CHECK_EQ(r.x, 3);
    CHECK(memcmp(g_ram + TN_PHONETIC, "ARA", 3) == 0);
    CHECK_EQ(g_ram[PROS_SYM_FLAGS + 0], PSF_STRESS);
```

**`tests/test_fw_pros.c`**: replace

```c
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 0], 4);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 4);
    CHECK_EQ(g_ram[FW_PROS_NEXT_PHON], 0x0D);           /* the look-ahead at L overwrote $CDBF's 'E' */
    check_call(1, ROM_ISR_PAR_READY, 0x83, 0, 0x45);    /* Y = L's features ($1C07) */
    CHECK_EQ(r.y, 0x45);
    CHECK(!g_f.fault);
```

with

```c
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 0], 4);
    CHECK_EQ(g_ram[PROS_SYM_LENGTHEN + 1], 4);
    CHECK_EQ(g_ram[FW_PROS_NEXT_PHON], 0x0D);           /* the look-ahead at L overwrote $CDBF's 'E' */
    check_ready(1);                                     /* after the vowel */
    CHECK_EQ(r.y, 0x45);
    CHECK(!g_f.fault);
```

**`tests/test_fw_pros.c`**: replace

```c
}

/* The parameter copies, then pros_reset_seg_ptrs, pros_mark_stress, pros_mark_phrases and pros_phrase_gen in that
   order, each pass's first isr_par_ready with the Y the step before left; the registers out are pros_phrase_gen's */
static void test_make_segments(void)
{
    make_setup();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    pros_make_segments(&g_f, &r);
    CHECK_EQ(g_ncalls, 12);
    check_call(0, ROM_ISR_PAR_READY, 0, 0, 0x33);       /* pros_mark_stress, the start: Y = the entry Y */
    check_call(1, ROM_ISR_PAR_READY, 0xFF, 0, 0);       /* after the space */
    check_call(2, ROM_ISR_PAR_READY, 0x83, 1, 0x49);    /* after E: Y = the next symbol's features */
    check_call(3, ROM_ISR_PAR_READY, 0x49, 2, 0x49);    /* after M; it leaves A = Y = $49, X = 3 */
    check_call(4, ROM_ISR_PAR_READY, 0, 2, 0x49);       /* pros_mark_phrases, the first: Y = pros_mark_stress's */
    check_call(5, ROM_ISR_PAR_READY, 0x49, 1, 0x49);    /* after M; it leaves A = 2, X = 0, Y = E's features */
    check_call(6, ROM_ISR_PAR_READY, 0xFF, 0, 5);       /* pros_phrase_gen, the first: X and Y as its
                                                           pros_declination leaves them ($10 over the count 3) */
    check_call(7, ROM_ISR_PAR_READY, 0, 0, 0);
    check_call(8, ROM_ISR_PAR_READY, 0x13, 0x61, 0x13); /* E's segment */
    check_call(9, ROM_ISR_PAR_READY, 0xFF, 0x61, 3);
    check_call(10, ROM_ISR_PAR_READY, 0x0A, 0x12, 0x0A);   /* M's segment */
    check_call(11, ROM_ISR_PAR_READY, 0xFF, 0x12, 3);   /* the end */
    CHECK_EQ(r.a, 4);                                   /* pros_phrase_gen's A, X and Y */
    CHECK_EQ(r.x, 3);
    CHECK_EQ(r.y, 3);
```

with

```c
}

/* The parameter copies, then pros_reset_seg_ptrs, pros_mark_stress, pros_mark_phrases and pros_phrase_gen in that
   order, each pass with its isr_par_ready calls; the registers out are pros_phrase_gen's */
static void test_make_segments(void)
{
    make_setup();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    pros_make_segments(&g_f, &r);
    CHECK_EQ(g_ncalls, 12);
    check_ready(0);                                     /* pros_mark_stress, the start */
    check_ready(1);                                     /* after the space */
    check_ready(2);                                     /* after E */
    check_ready(3);                                     /* after M */
    check_ready(4);                                     /* pros_mark_phrases, the first */
    check_ready(5);                                     /* after M */
    check_ready(6);                                     /* pros_phrase_gen, the first */
    check_ready(7);
    check_ready(8);                                     /* E's segment */
    check_ready(9);
    check_ready(10);                                    /* M's segment */
    check_ready(11);                                    /* the end */
    CHECK_EQ(r.a, 4);                                   /* pros_phrase_gen's A, X and Y */
    CHECK_EQ(r.x, 3);
    CHECK_EQ(r.y, 3);
```

**`tests/test_fw_tn.c`**: replace

```c
/* Unit checks of tn_convert_clause and tn_append_phonetic on the converted data, with a mock host that records
   every ROM call. tn_convert_clause runs the deferred commands through the C cmd_run_due; the tests queue
   commands whose handler stays in ROM (bank_cmd_select), so the mock sees each one as a cmd_run_next call. The
   full proof is the live differential test (diff_lts) and the hybrid goldens (golden_hybrid). */
#include <string.h>
#include "test.h"
#include "firmware/cmd_addr.h"
```

with

```c
/* Unit checks of tn_convert_clause and tn_append_phonetic on the converted data, with a mock host that records
   every ROM call, every wait and, in parallel mode (boot_dip bit 0, set by setup_n), every isr_par_ready and
   isr_par_hold (C since M4.6b) by its write of P0. tn_convert_clause runs the deferred commands through the C
   cmd_run_due; the tests queue commands whose handler stays in ROM (bank_cmd_select), so the mock sees each one as
   a cmd_run_next call. The full proof is the live differential test (diff_lts) and the hybrid goldens
   (golden_hybrid). */
#include <string.h>
#include "test.h"
#include "firmware/cmd_addr.h"
```

**`tests/test_fw_tn.c`**: replace

```c
static bool g_window;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;

/* Records every call with tn_phon_len at that moment. cmd_run_next ($A893) plays the ROM's bookkeeping
   (cmd_next += 1, cmd_next_pos = cmd_positions[cmd_next]); the other helpers leave the registers unchanged.
   isr_par_ready also sets isr_frame_flag bit 0 (a frame is due at every wait of the C par_play), and
   isr_speech_start sets ser_abort bit 0 (a mute as the DSP starts), so the C boot_loop_play of the mid-clause
   flush plays no segment: par_play goes from its first pass to the three rest frames. */
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
```

with

```c
static bool g_window;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;
static uint8_t g_p0, g_p3, g_icr1;   /* the SFRs the C isr_par_ready, isr_par_hold and isr_set_volume touch */

static void record(fw *f, uint16_t addr, const fw_regs *r, uint64_t budget)
{
    if (g_ncalls < MAX_CALLS) {
        rom_call_record *c = &g_calls[g_ncalls];
        c->addr = addr;
        c->a = r ? r->a : 0;
        c->x = r ? r->x : 0;
        c->y = r ? r->y : 0;
        c->phon_len = fw_rd8(f, TN_PHON_LEN);
        c->budget = budget;
    }
    g_ncalls++;
}

/* Records every call with tn_phon_len at that moment. cmd_run_next ($A893) plays the ROM's bookkeeping
   (cmd_next += 1, cmd_next_pos = cmd_positions[cmd_next]); the other helpers leave the registers unchanged.
   isr_speech_start sets ser_abort bit 0 (a mute as the DSP starts), so the C boot_loop_play of the mid-clause flush
   plays no segment: par_play goes from its first pass to the three rest frames. */
static fw_call_result mock_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    fw *f = ctx;
    record(f, addr, r, budget);
    if (addr == ROM_CMD_RUN_NEXT) {
        uint8_t n = (uint8_t)(fw_rd8(f, CMD_NEXT) + 1);
        fw_wr8(f, CMD_NEXT, n);
```

**`tests/test_fw_tn.c`**: replace

```c
        return FW_CALL_RETURNED;
    }
    switch (addr) {
    case ROM_ISR_PAR_READY:
        fw_wr8(f, ISR_FRAME_FLAG, 1);
        return FW_CALL_RETURNED;
    case ROM_ISR_SPEECH_START:
        fw_wr8(f, SER_ABORT, 1);
        return FW_CALL_RETURNED;
    case ROM_ISR_PAR_HOLD:
    case ROM_ISR_VOLUME_HALF:
    case ROM_ISR_WAIT_FRAME:
    case ROM_ISR_SET_VOLUME:
    case ROM_ISR_SPEECH_STOP:
        return FW_CALL_RETURNED;
    default:
```

with

```c
        return FW_CALL_RETURNED;
    }
    switch (addr) {
    case ROM_ISR_SPEECH_START:
        fw_wr8(f, SER_ABORT, 1);
        return FW_CALL_RETURNED;
    case ROM_ISR_WAIT_FRAME:
    case ROM_ISR_SPEECH_STOP:
        return FW_CALL_RETURNED;
    default:
```

**`tests/test_fw_tn.c`**: replace

```c
    }
}

/* UART_STATUS: the transmitter ready (bit 1), nothing received. */
static uint8_t mock_read_sfr(void *ctx, uint8_t addr)
{
    (void)ctx;
    return addr == 0xE7 ? 0x02 : 0x00;
}

static void mock_window(void *ctx, bool on)
```

with

```c
    }
}

/* Records every wait (addr = its ROM loop). Each is an interrupt handled: the timer interrupt sets isr_frame_flag
   bit 0 (a frame is due at every wait of the C par_play). */
static bool mock_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    fw *f = ctx;
    record(f, at, NULL, 0);
    if (kind == FW_WAIT_INTERRUPT)
        fw_wr8(f, ISR_FRAME_FLAG, 1);
    return true;
}

/* UART_STATUS: the transmitter ready (bit 1), nothing received; P0, P3 and ICR1 as last written. */
static uint8_t mock_read_sfr(void *ctx, uint8_t addr)
{
    (void)ctx;
    switch (addr) {
    case 0xE7: return 0x02;
    case 0xF6: return g_p0;
    case 0xEE: return g_p3;
    case 0xFF: return g_icr1;
    default: return 0x00;
    }
}

/* P0 ($F6): isr_par_ready sets bit 3 (recorded as a call of ROM_ISR_PAR_READY), isr_par_hold clears it (recorded as
   ROM_ISR_PAR_HOLD), neither with registers (the C passes them to nothing); P3 and ICR1 are kept. */
static void mock_write_sfr(void *ctx, uint8_t addr, uint8_t value)
{
    fw *f = ctx;
    switch (addr) {
    case 0xF6:
        g_p0 = value;
        record(f, (value & 0x08) ? ROM_ISR_PAR_READY : ROM_ISR_PAR_HOLD, NULL, 0);
        break;
    case 0xEE: g_p3 = value; break;
    case 0xFF: g_icr1 = value; break;
    default: CHECK(false); break;
    }
}

static void mock_window(void *ctx, bool on)
```

**`tests/test_fw_tn.c`**: replace

```c
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = mock_window;
    g_host.read_sfr = mock_read_sfr;
    g_window = false;
    g_ncalls = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
    memcpy(g_ram + CMD_TEXT, text, n);
    g_ram[BOOT_CLAUSE_LEN] = (uint8_t)n;
    g_ram[CMD_VOICE] = 1;
```

with

```c
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = mock_window;
    g_host.read_sfr = mock_read_sfr;
    g_host.write_sfr = mock_write_sfr;
    g_host.wait = mock_wait;
    g_window = false;
    g_ncalls = 0;
    g_p0 = g_p3 = g_icr1 = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
    g_ram[BOOT_DIP] = 1;                        /* parallel mode: isr_par_ready and isr_par_hold are recorded */
    memcpy(g_ram + CMD_TEXT, text, n);
    g_ram[BOOT_CLAUSE_LEN] = (uint8_t)n;
    g_ram[CMD_VOICE] = 1;
```

**`tests/test_fw_tn.c`**: replace

```c
    return n;
}

/* Every helper call has the strict budget. */
static void check_helper_budgets(void)
{
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++)
        if (g_calls[i].addr == ROM_ISR_PAR_READY || g_calls[i].addr == ROM_CMD_RUN_NEXT)
            CHECK_EQ(g_calls[i].budget, FW_ROM_CALL_BUDGET);
}
```

with

```c
    return n;
}

/* Every cmd_run_next call has the strict budget. */
static void check_helper_budgets(void)
{
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++)
        if (g_calls[i].addr == ROM_CMD_RUN_NEXT)
            CHECK_EQ(g_calls[i].budget, FW_ROM_CALL_BUDGET);
}
```

**`tests/test_fw_tn.c`**: replace

```c
}

/* The mid-clause flush ($B1FF): the C pros_make_segments (M4.3b) turns the 250 symbols into segments, with its
   isr_par_ready calls (the strict budget), and the C boot_loop_play (M4.4b) gets the registers it leaves, as the
   ROM's would: it plays (FW_UTT_CONTENT is $FF), so its first ROM call, isr_par_hold, has them; the C
   par_play_start sets up the first segment; the mock's mute at isr_speech_start ends the C par_play at its first
   pass. Then the conversion goes on from tn_resume_pos with an empty buffer. */
static void test_flush(void)
{
    char text[255];
```

with

```c
}

/* The mid-clause flush ($B1FF): the C pros_make_segments (M4.3b) turns the 250 symbols into segments, with its
   isr_par_ready calls, and the C boot_loop_play (M4.4b) gets the registers it leaves, as the ROM's would: it plays
   (FW_UTT_CONTENT is $FF), so isr_par_hold and par_play_start pass X on to its first ROM call, isr_speech_start;
   the C par_play_start sets up the first segment; the mock's mute at isr_speech_start ends the C par_play at its
   first pass. Then the conversion goes on from tn_resume_pos with an empty buffer. */
static void test_flush(void)
{
    char text[255];
```

**`tests/test_fw_tn.c`**: replace

```c
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 368);         /* ... into 369 segments (the index of the last) */
    /* the conversion 251 + 5 (one per character, the flush's included), pros_mark_stress 251 (the start and one per
       position), pros_mark_phrases 249 (positions 249 to 1), pros_phrase_gen 251 (positions 0 to 250) + 367 (one per
       segment but the two end pauses), par_play 5 (its pass, one wait per rest frame, its last call) */
    CHECK_EQ(count(ROM_ISR_PAR_READY), 1379);
    CHECK(g_ncalls <= MAX_CALLS);
    /* boot_loop_play's ROM calls, then isr_par_ready at text[250..254] */
    static const uint16_t k_play[] = {
        ROM_ISR_PAR_HOLD, ROM_ISR_SPEECH_START, ROM_ISR_VOLUME_HALF, ROM_ISR_WAIT_FRAME, ROM_ISR_SET_VOLUME,
        ROM_ISR_PAR_READY, ROM_ISR_WAIT_FRAME,                                     /* par_play: the pass, the end */
        ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,                   /* the rest frames (C) */
        ROM_ISR_PAR_READY,
        ROM_ISR_VOLUME_HALF, ROM_ISR_WAIT_FRAME, ROM_ISR_SET_VOLUME, ROM_ISR_SPEECH_STOP,
        ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,
    };
    unsigned n = sizeof k_play / sizeof k_play[0];
```

with

```c
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 368);         /* ... into 369 segments (the index of the last) */
    /* the conversion 251 + 5 (one per character, the flush's included), pros_mark_stress 251 (the start and one per
       position), pros_mark_phrases 249 (positions 249 to 1), pros_phrase_gen 251 (positions 0 to 250) + 367 (one per
       segment but the two end pauses), par_play 8 (its pass, two per rest frame around its wait, its last call) */
    CHECK_EQ(count(ROM_ISR_PAR_READY), 1382);
    CHECK(g_ncalls <= MAX_CALLS);
    /* boot_loop_play's ROM calls and waits (isr_set_volume and isr_volume_half are C and write P3 only), then
       isr_par_ready at text[250..254] */
    static const uint16_t k_play[] = {
        ROM_ISR_PAR_HOLD, ROM_ISR_SPEECH_START, ROM_ISR_WAIT_FRAME,
        ROM_ISR_PAR_READY, ROM_ISR_WAIT_FRAME,                                     /* par_play: the pass, the end */
        ROM_ISR_PAR_READY, 0xB8E6, ROM_ISR_PAR_READY,                              /* the rest frames (C) */
        ROM_ISR_PAR_READY, 0xB8E6, ROM_ISR_PAR_READY,
        ROM_ISR_PAR_READY, 0xB8E6, ROM_ISR_PAR_READY,
        ROM_ISR_PAR_READY,                                                         /* $B8F7 */
        ROM_ISR_WAIT_FRAME, ROM_ISR_SPEECH_STOP,
        ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,
    };
    unsigned n = sizeof k_play / sizeof k_play[0];
```

**`tests/test_fw_tn.c`**: replace

```c
    unsigned first = g_ncalls - n;
    for (unsigned i = 0; i < n; i++)
        CHECK_EQ(g_calls[first + i].addr, k_play[i]);
    CHECK_EQ(g_calls[first].a, 0xFF);                     /* boot_loop_play's LDA $6B ($A7D4); */
    CHECK_EQ(g_calls[first].x, TN_PHONETIC_FLUSH);        /* X = pros_pos (tn_phon_len) and */
    CHECK_EQ(g_calls[first].y, 0x70);                     /* Y = the second end pause's, as pros_phrase_gen leaves
                                                             them */
    CHECK_EQ(g_calls[first].phon_len, TN_PHONETIC_FLUSH);
    CHECK_EQ(g_calls[first + 4].a, g_ram[CMD_VOLUME]);   /* isr_set_volume(cmd_volume) ($B2C2) */
    CHECK_EQ(g_calls[first + 13].a, 0);                   /* isr_set_volume(0) ($B2D0) */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0xFFFF);          /* par_play_start: no event queued */
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 0);                  /* the three rest frames counted down */
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 1);                   /* set by the last isr_par_ready calls */
    for (unsigned i = first; i < g_ncalls; i++)
        CHECK_EQ(g_calls[i].budget, FW_ROM_CALL_BUDGET);
    check_helper_budgets();
    CHECK(!g_f.fault);
}
```

with

```c
    unsigned first = g_ncalls - n;
    for (unsigned i = 0; i < n; i++)
        CHECK_EQ(g_calls[first + i].addr, k_play[i]);
    CHECK_EQ(g_calls[first].phon_len, TN_PHONETIC_FLUSH);
    CHECK_EQ(g_calls[first + 1].a, 0);                    /* isr_speech_start: par_play_start's A = 0 ($BB2C) and */
    CHECK_EQ(g_calls[first + 1].x, TN_PHONETIC_FLUSH);    /* X = pros_pos (tn_phon_len), as pros_phrase_gen leaves
                                                             it, through isr_par_hold and par_play_start */
    CHECK_EQ(g_p3, 0x78);                                 /* isr_set_volume(0) ($B2D0): $F << 3 */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0xFFFF);          /* par_play_start: no event queued */
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 0);                  /* the three rest frames counted down */
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0);                   /* cleared after the last rest frame ($B8EC) */
    check_helper_budgets();
    CHECK(!g_f.fault);
}
```

**`tests/test_golden_hybrid.c`**: replace

```c
#include "diff_state.h"
#include "engine/hybrid_engine.h"
#include "engine/thunks.h"
#include "firmware/isr_addr.h"
#include "firmware/tn_convert.h"
#include "render/render.h"

static const char *const k_corpus[] = { CORPUS_NAMES };
```

with

```c
#include "diff_state.h"
#include "engine/hybrid_engine.h"
#include "engine/thunks.h"
#include "firmware/cmd_addr.h"
#include "render/render.h"

static const char *const k_corpus[] = { CORPUS_NAMES };
```

**`tests/test_golden_hybrid.c`**: replace

```c
#define BOOT_RESET_ADDR 0xA61C   /* any ROM address: where reset_stack_hook "jumps" */

/* Stands for bank_enter's stack reset: SP = $FF and a jump elsewhere. */
static bool reset_stack_hook(machine *m, void *user)
{
```

with

```c
#define BOOT_RESET_ADDR 0xA61C   /* any ROM address: where reset_stack_hook "jumps" */

/* One command queued, due at position 0, whose handler stays in ROM (bank_cmd_select): the C cmd_run_due with X = 0
   hands it to the ROM cmd_run_next, its one ROM call. */
static void queue_rom_command(machine *m)
{
    m->ram[CMD_COUNT] = 1;
    m->ram[CMD_NEXT] = 0;
    m->ram[CMD_NEXT_POS] = 0;
    m->ram[CMD_HANDLERS] = ROM_BANK_CMD_SELECT & 0xFF;
    m->ram[CMD_HANDLERS + 1] = ROM_BANK_CMD_SELECT >> 8;
    m->cpu.x = 0;
}

/* Stands for bank_enter's stack reset: SP = $FF and a jump elsewhere. */
static bool reset_stack_hook(machine *m, void *user)
{
```

**`tests/test_golden_hybrid.c`**: replace

```c
    CHECK(r->thunk(&g_m, &b));
    check_stopped(&g_m, &b, r->addr, 0x00E1, sp);

    /* (2) a ROM call from the C that does not return: isr_par_ready, tn_convert_clause's first call */
    CHECK(machine_init(&g_m, rom, len, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    CHECK(machine_hook(&g_m, ROM_ISR_PAR_READY, stuck_hook, NULL));
    r = fw_replacement_find("tn_convert_clause");
    CHECK(r != NULL);
    if (!r)
        return;
    g_m.cpu.pc = r->addr;
    sp = g_m.cpu.s;
    CHECK(r->thunk(&g_m, &b));
    check_stopped(&g_m, &b, r->addr, ROM_ISR_PAR_READY, sp);

    /* (3) a thunk run on another machine than its bridge's: refused */
    CHECK(machine_init(&g_m, rom, len, NULL));
```

with

```c
    CHECK(r->thunk(&g_m, &b));
    check_stopped(&g_m, &b, r->addr, 0x00E1, sp);

    /* (2) a ROM call from the C that does not return: cmd_run_next, cmd_run_due's call for a ROM handler */
    CHECK(machine_init(&g_m, rom, len, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    CHECK(machine_hook(&g_m, ROM_CMD_RUN_NEXT, stuck_hook, NULL));
    r = fw_replacement_find("cmd_run_due");
    CHECK(r != NULL);
    if (!r)
        return;
    queue_rom_command(&g_m);
    g_m.cpu.pc = r->addr;
    sp = g_m.cpu.s;
    CHECK(r->thunk(&g_m, &b));
    check_stopped(&g_m, &b, r->addr, ROM_CMD_RUN_NEXT, sp);

    /* (3) a thunk run on another machine than its bridge's: refused */
    CHECK(machine_init(&g_m, rom, len, NULL));
```

**`tests/test_golden_hybrid.c`**: replace

```c
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
```

with

```c
       fault; the machine goes on where the ROM went (M4.2 spec 3) */
    CHECK(machine_init(&g_m, rom, len, NULL));
    CHECK(fw_bridge_init(&b, &g_m));
    CHECK(machine_hook(&g_m, ROM_CMD_RUN_NEXT, reset_stack_hook, NULL));
    r = fw_replacement_find("cmd_run_due");
    CHECK(r != NULL);
    if (!r)
        return;
    queue_rom_command(&g_m);
    g_m.cpu.pc = r->addr;
    g_m.cpu.s = 0xF0;                            /* a caller's frame below the stack reset */
    CHECK(r->thunk(&g_m, &b));
```

**`tests/test_machine_hooks.c`**: replace

```c
static uint64_t g_seen_deadline;

/* An observer at isr_par_ready $C5D6: records the bridge's deadline at its first call and sets ser_abort bit 0. */
static bool hook_par_ready_abort(machine *m, void *user)
{
    const fw_bridge *b = user;
    if (!g_seen_deadline)
        g_seen_deadline = b->deadline;
    m->ram[SER_ABORT] |= 1;
    return false;
}

/* A fresh real-ROM machine (the timer off: isr_frame_flag is never set) with the par_play thunk hooked, entered at
   $B877 as a JSR from $9000 would enter it. */
static void enter_par_play(const uint8_t *rom, size_t len, fw_bridge *b)
{
    CHECK(machine_init(&g_m, rom, len, NULL));
```

with

```c
static uint64_t g_seen_deadline;

/* An observer at util_shl3 $C657, here an event handler in ROM: records the bridge's deadline at its first call. */
static bool hook_record_deadline(machine *m, void *user)
{
    const fw_bridge *b = user;
    (void)m;
    if (!g_seen_deadline)
        g_seen_deadline = b->deadline;
    return false;
}

/* A fresh real-ROM machine (the timer off: no interrupt comes and isr_frame_flag is never set) with the par_play thunk
   hooked, entered at $B877 as a JSR from $9000 would enter it. */
static void enter_par_play(const uint8_t *rom, size_t len, fw_bridge *b)
{
    CHECK(machine_init(&g_m, rom, len, NULL));
```

**`tests/test_machine_hooks.c`**: replace

```c
}

/* A waits thunk whose flag never comes faults instead of hanging. The outermost thunk sets the deadline (entry
   cycles + FW_ROM_CALL_SPEECH_BUDGET) and clears it when it ends: here par_play aborts at its first isr_par_ready and
   isr_wait_frame $B465 waits out its own budget. A deadline already set (as by an outer thunk) is left alone, and
   par_play's isr_par_ready loop, every call of which returns, ends at it with a fault at isr_par_ready. */
static void test_waits_thunk_deadline(void)
{
    static fw_bridge b;
```

with

```c
}

/* A waits thunk whose flag never comes faults instead of hanging. The outermost thunk sets the deadline (entry
   cycles + FW_ROM_CALL_SPEECH_BUDGET), seen by an event handler run in ROM ($C657, an event due at segment 0), and
   clears it when it ends: here par_play, with a mute pending, then goes straight to its end, and isr_wait_frame
   $B465 waits out its budget (FW_ROM_CALL_BUDGET, before the deadline). A deadline already set (as by an outer thunk)
   is left alone, and par_play's wait for an interrupt at its back edge (M4.6b) ends at it with a fault at $B877. */
static void test_waits_thunk_deadline(void)
{
    static fw_bridge b;
```

**`tests/test_machine_hooks.c`**: replace

```c
        SKIP("ROM not found (set APOLLO2_ROM)");

    enter_par_play(rom, len, &b);
    CHECK(machine_hook(&g_m, ROM_ISR_PAR_READY, hook_par_ready_abort, &b));
    g_seen_deadline = 0;
    uint64_t c0 = g_m.cpu.cycles;
    CHECK(!machine_step(&g_m));
    CHECK_EQ(g_seen_deadline, c0 + FW_ROM_CALL_SPEECH_BUDGET);
    CHECK_EQ(b.deadline, 0);   /* cleared by the outermost thunk */
    CHECK_EQ(g_m.fault, MFAULT_FIRMWARE);
    CHECK_EQ(g_m.fault_addr, ROM_ISR_WAIT_FRAME);
```

with

```c
        SKIP("ROM not found (set APOLLO2_ROM)");

    enter_par_play(rom, len, &b);
    CHECK(machine_hook(&g_m, 0xC657, hook_record_deadline, &b));
    g_m.ram[CMD_EV_COUNT] = 1;                    /* an event due at par_seg_index 0, its handler $C657 */
    g_m.ram[CMD_EV_HANDLERS] = 0x57;
    g_m.ram[CMD_EV_HANDLERS + 1] = 0xC6;
    g_m.ram[SER_ABORT] |= 1;
    g_seen_deadline = 0;
    uint64_t c0 = g_m.cpu.cycles;
    CHECK(!machine_step(&g_m));
    CHECK_EQ(g_seen_deadline, c0 + FW_ROM_CALL_SPEECH_BUDGET);
    CHECK(g_m.cpu.cycles >= c0 + FW_ROM_CALL_BUDGET);
    CHECK(g_m.cpu.cycles < c0 + FW_ROM_CALL_BUDGET + 100);
    CHECK_EQ(b.deadline, 0);   /* cleared by the outermost thunk */
    CHECK_EQ(g_m.fault, MFAULT_FIRMWARE);
    CHECK_EQ(g_m.fault_addr, ROM_ISR_WAIT_FRAME);
```

**`tests/test_machine_hooks.c`**: replace

```c
    CHECK(!machine_step(&g_m));
    CHECK_EQ(b.deadline, deadline);   /* not the outermost: left alone */
    CHECK_EQ(g_m.fault, MFAULT_FIRMWARE);
    CHECK_EQ(g_m.fault_addr, ROM_ISR_PAR_READY);
    CHECK(g_m.cpu.cycles >= deadline);
    CHECK(g_m.cpu.cycles < deadline + 100);
    CHECK_EQ(g_m.cpu.pc, ROM_PAR_PLAY);
```

with

```c
    CHECK(!machine_step(&g_m));
    CHECK_EQ(b.deadline, deadline);   /* not the outermost: left alone */
    CHECK_EQ(g_m.fault, MFAULT_FIRMWARE);
    CHECK_EQ(g_m.fault_addr, ROM_PAR_PLAY);
    CHECK(g_m.cpu.cycles >= deadline);
    CHECK(g_m.cpu.cycles < deadline + 100);
    CHECK_EQ(g_m.cpu.pc, ROM_PAR_PLAY);
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw_dsp"`
Expected: the build stops with compile errors (the proof run, first lines):

```
tests\test_fw_dsp.c(8): fatal error C1083: Cannot open include file: 'firmware/isr_dsp.h': No such file or directory
```

(the only error: the other tests compile, and the changed mocks fail only once the C is in).

- [ ] **Step 3: Implement**

The C routines (`isr_dsp.c`, `isr_serial.c`), their rows in `thunks.c` with the callers' reads, the callers switched to the C (`fw_par_ready`, `cmd_scan_clause`, `pros_append_segment`, `tn_convert_clause`, `cmd_ev_set_volume`, `boot_loop_play`'s body), and `par_play`'s polling loops on `FW_WAIT_INTERRUPT`.

**`src/engine/thunks.c`**: replace

```c
#include "firmware/ser.h"
#include "firmware/cmd_imm.h"
#include "firmware/isr_serial.h"
#include "firmware/data/fw_data.h"

#ifdef _MSC_VER
```

with

```c
#include "firmware/ser.h"
#include "firmware/cmd_imm.h"
#include "firmware/isr_serial.h"
#include "firmware/isr_dsp.h"
#include "firmware/data/fw_data.h"

#ifdef _MSC_VER
```

**`src/engine/thunks.c`**: replace

```c
    c->y = fw_rd8(f, CMD_NEXT);
}

/* cmd_scan_clause $ACFA: no register input or output (boot_loop_text calls tn_convert_clause next, which starts
   with LDM/LDX #0 and reads no register) */
static void cmd_scan_clause_regs(fw *f, cpu740 *c)
```

with

```c
    c->y = fw_rd8(f, CMD_NEXT);
}

/* isr_set_volume $ACE8: A in (the volume). A = the value written to P3 ($ACF7 STA P3, after AND P3 / ORA $75), X and
   Y untouched, out. boot_reset ($A642) calls boot_read_dip next, which loads A first ($ADE4 LDA P4) and reads no X
   or Y; boot_loop_play calls par_play after $B2C4, which writes A, X and Y before reading them (its thunk), and
   isr_speech_stop after $B2D2, which keeps all three to boot_loop_play's exit (its thunk); isr_volume_half ($B46F)
   returns them to its callers (its row); cmd_ev_set_volume ($BCEF) returns them to par_play's $B892 JMP par_play
   (LDX first). No caller reads a flag. The C boot_loop_play, isr_volume_half and cmd_ev_set_volume call the C, so
   in "all" mode the hook fires from boot_reset only. Declared: A, X, Y. */
static void isr_set_volume_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_set_volume(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* cmd_scan_clause $ACFA: no register input or output (boot_loop_text calls tn_convert_clause next, which starts
   with LDM/LDX #0 and reads no register) */
static void cmd_scan_clause_regs(fw *f, cpu740 *c)
```

**`src/engine/thunks.c`**: replace

```c
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_copy_rest_frame(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
```

with

```c
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_copy_rest_frame(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* isr_volume_half $B46C: no register input (LDA cmd_volume first). A = the value written to P3, X and Y untouched, out
   (isr_set_volume). Its callers, boot_play_body $B2BC and $B2CA, call isr_wait_frame next (BBC0 / LDM: none read),
   then load A ($B2C2 LDA cmd_volume, $B2D0 LDA #0) and hand X and Y to isr_set_volume, par_play and isr_speech_stop
   (their rows). No caller reads a flag. The C boot_loop_play calls the C, so it fires only while the ROM
   boot_loop_play runs. Declared: A, X, Y. */
static void isr_volume_half_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_volume_half(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
```

**`src/engine/thunks.c`**: replace

```c
   cmd_run_due, which reloads Y at $A880): none read. Declared: A, X, Y. */
static void ser_tx_records_regs(fw *f, cpu740 *c) { ser_regs(f, c, ser_tx_records); }

/* ser_rx_next_page $C5EC: Y in; Y = the next page out, A and X untouched. ser_rx_put ($C3DC) stores Y and returns;
   boot_get_clause ($A6F8) stores Y, then reads A (the byte it loaded, $A700 CMP #$0D) and X ($A706 / $A70D STA
   $1B00,X). No caller reads a flag. Declared: A, X, Y. */
```

with

```c
   cmd_run_due, which reloads Y at $A880): none read. Declared: A, X, Y. */
static void ser_tx_records_regs(fw *f, cpu740 *c) { ser_regs(f, c, ser_tx_records); }

/* isr_par_ready $C5D6: A, X and Y in and out as they came (PHA at $C5D6, PLA at $C5EA; X and Y untouched).
   boot_get_clause reads A and X after $A6FC ($A700 CMP #$0D, $A706 / $A70D STA $1B00,X) and hands all three to
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

/* ser_rx_next_page $C5EC: Y in; Y = the next page out, A and X untouched. ser_rx_put ($C3DC) stores Y and returns;
   boot_get_clause ($A6F8) stores Y, then reads A (the byte it loaded, $A700 CMP #$0D) and X ($A706 / $A70D STA
   $1B00,X). No caller reads a flag. Declared: A, X, Y. */
```

**`src/engine/thunks.c`**: replace

```c
/* util_shl3 $C657: A in; A << 3 out (8 bits); X and Y untouched. Callers read A: isr_set_volume ($ACEF STA $75),
   cmd_ev_sound_start ($BBAA TAX); the ROM util_shl4 falls into it ($C656 ASL), so its callers' reads apply too (A,
   above). No caller reads a flag (STA, TAX follow). Fires in "all" mode from the ROM isr_set_volume, which stays
   ROM (M4.6; called by boot_reset $A642, boot_loop_play $B2C4/$B2D2, isr_volume_half $B46F and cmd_ev_set_volume
   $BCEF, the C ones through rom_call). Declared: A, X, Y. */
static void util_shl3_regs(fw *f, cpu740 *c)
{
    (void)f;
```

with

```c
/* util_shl3 $C657: A in; A << 3 out (8 bits); X and Y untouched. Callers read A: isr_set_volume ($ACEF STA $75),
   cmd_ev_sound_start ($BBAA TAX); the ROM util_shl4 falls into it ($C656 ASL), so its callers' reads apply too (A,
   above). No caller reads a flag (STA, TAX follow). Both callers are C (cmd_ev_sound_start since M4.4b,
   isr_set_volume since M4.6b) and inline it: in "all" mode it never fires. Declared: A, X, Y. */
static void util_shl3_regs(fw *f, cpu740 *c)
{
    (void)f;
```

**`src/engine/thunks.c`**: replace

```c
THUNK(cmd_apply_speaker)
THUNK(boot_loop_play)
THUNK(cmd_run_due)
THUNK(cmd_scan_clause)
THUNK(cmd_match_table)
THUNK(lts_affix_next)
```

with

```c
THUNK(cmd_apply_speaker)
THUNK(boot_loop_play)
THUNK(cmd_run_due)
THUNK(isr_set_volume)
THUNK(cmd_scan_clause)
THUNK(cmd_match_table)
THUNK(lts_affix_next)
```

**`src/engine/thunks.c`**: replace

```c
THUNK(par_setup_voice)
THUNK(frm_build_rest_frame)
THUNK(frm_copy_rest_frame)
THUNK(par_play)
THUNK(frm_snapshot)
THUNK(frm_clamp_mark_space)
```

with

```c
THUNK(par_setup_voice)
THUNK(frm_build_rest_frame)
THUNK(frm_copy_rest_frame)
THUNK(isr_volume_half)
THUNK(par_play)
THUNK(frm_snapshot)
THUNK(frm_clamp_mark_space)
```

**`src/engine/thunks.c`**: replace

```c
THUNK(ser_tx_hex)
THUNK(ser_tx_service)
THUNK(ser_tx_records)
THUNK(ser_rx_next_page)
THUNK(ser_tx_next_page)
THUNK(util_shl4)
```

with

```c
THUNK(ser_tx_hex)
THUNK(ser_tx_service)
THUNK(ser_tx_records)
THUNK(isr_par_ready)
THUNK(isr_par_hold)
THUNK(ser_rx_next_page)
THUNK(ser_tx_next_page)
THUNK(util_shl4)
```

**`src/engine/thunks.c`**: replace

```c
    { "cmd_apply_speaker",  0xA78C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_apply_speaker },
    { "boot_loop_play",     0xA7D4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_loop_play, true },
    { "cmd_run_due",        0xA880, FW_OUT_X | FW_OUT_Y, t_cmd_run_due },
    { "cmd_scan_clause",    0xACFA, 0,                   t_cmd_scan_clause },
    { "cmd_match_table",    0xADBF, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_match_table },
    { "lts_affix_next",     0xAF11, FW_OUT_A | FW_OUT_Y, t_lts_affix_next },
```

with

```c
    { "cmd_apply_speaker",  0xA78C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_apply_speaker },
    { "boot_loop_play",     0xA7D4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_loop_play, true },
    { "cmd_run_due",        0xA880, FW_OUT_X | FW_OUT_Y, t_cmd_run_due },
    { "isr_set_volume",     0xACE8, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_set_volume },
    { "cmd_scan_clause",    0xACFA, 0,                   t_cmd_scan_clause },
    { "cmd_match_table",    0xADBF, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_match_table },
    { "lts_affix_next",     0xAF11, FW_OUT_A | FW_OUT_Y, t_lts_affix_next },
```

**`src/engine/thunks.c`**: replace

```c
    { "par_setup_voice",    0xB3A4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_setup_voice },
    { "frm_build_rest_frame", 0xB41D, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_build_rest_frame },
    { "frm_copy_rest_frame", 0xB445, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_copy_rest_frame },
    { "par_play",           0xB877, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_play, true },
    { "frm_snapshot",       0xB917, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_snapshot },
    { "frm_clamp_mark_space", 0xB930, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_clamp_mark_space },
```

with

```c
    { "par_setup_voice",    0xB3A4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_setup_voice },
    { "frm_build_rest_frame", 0xB41D, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_build_rest_frame },
    { "frm_copy_rest_frame", 0xB445, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_copy_rest_frame },
    { "isr_volume_half",    0xB46C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_volume_half },
    { "par_play",           0xB877, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_play, true },
    { "frm_snapshot",       0xB917, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_snapshot },
    { "frm_clamp_mark_space", 0xB930, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_clamp_mark_space },
```

**`src/engine/thunks.c`**: replace

```c
    { "ser_tx_hex",         0xC571, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_tx_hex },
    { "ser_tx_service",     0xC581, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_tx_service },
    { "ser_tx_records",     0xC5A4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_tx_records },
    { "ser_rx_next_page",   0xC5EC, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_rx_next_page },
    { "ser_tx_next_page",   0xC5F5, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_tx_next_page },
    { "util_shl4",          0xC656, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_util_shl4 },
```

with

```c
    { "ser_tx_hex",         0xC571, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_tx_hex },
    { "ser_tx_service",     0xC581, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_tx_service },
    { "ser_tx_records",     0xC5A4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_tx_records },
    { "isr_par_ready",      0xC5D6, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_par_ready },
    { "isr_par_hold",       0xC5DE, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_par_hold },
    { "ser_rx_next_page",   0xC5EC, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_rx_next_page },
    { "ser_tx_next_page",   0xC5F5, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_ser_tx_next_page },
    { "util_shl4",          0xC656, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_util_shl4 },
```

**`src/firmware/cmd_addr.h`**: replace

```c
#define ROM_CMD_EV_SET_VOICING    0xBCEA   /* cmd_set_voicing */
#define ROM_CMD_EV_SET_VOLUME     0xBCED   /* cmd_set_volume */

/* ROM routines the C calls through fw_rom_call (isr_par_ready: ROM_ISR_PAR_READY in isr_addr.h). The serial
   routines and the immediate handlers are C since M4.6a; their ROM addresses are in ser_addr.h. */
#define ROM_CMD_RUN_NEXT          0xA893   /* dispatch cmd_handlers[cmd_next] (the dispatcher's fallback); X kept */

#endif
```

with

```c
#define ROM_CMD_EV_SET_VOICING    0xBCEA   /* cmd_set_voicing */
#define ROM_CMD_EV_SET_VOLUME     0xBCED   /* cmd_set_volume */

/* ROM routines the C calls through fw_rom_call. The serial routines and the immediate handlers are C since M4.6a
   (their ROM addresses are in ser_addr.h), isr_par_ready since M4.6b (isr_addr.h). */
#define ROM_CMD_RUN_NEXT          0xA893   /* dispatch cmd_handlers[cmd_next] (the dispatcher's fallback); X kept */

#endif
```

**`src/firmware/cmd_play_event.c`**: replace

```c
#include "cmd_play_event.h"
#include "cmd_addr.h"
#include "fw_ops.h"
#include "isr_addr.h"
#include "par_addr.h"
#include "data/fw_data.h"
```

with

```c
#include "cmd_play_event.h"
#include "cmd_addr.h"
#include "fw_ops.h"
#include "isr_dsp.h"
#include "par_addr.h"
#include "data/fw_data.h"
```

**`src/firmware/cmd_play_event.c`**: replace

```c
void cmd_ev_set_volume(fw *f, fw_regs *r)
{
    fw_wr8(f, CMD_VOLUME, r->a);
    fw_rom_call(f, ROM_ISR_SET_VOLUME, r, FW_ROM_CALL_BUDGET);  /* $BCEF; its RTS is ours ($BCF2) */
}
```

with

```c
void cmd_ev_set_volume(fw *f, fw_regs *r)
{
    fw_wr8(f, CMD_VOLUME, r->a);
    isr_set_volume(f, r);                                        /* $BCEF; its RTS is ours ($BCF2) */
}
```

**`src/firmware/cmd_play_event.h`**: replace

```c
void cmd_ev_sound_end(fw *f, fw_regs *r);
/* $BCEA, the @B event: cmd_voicing = A. A, X and Y untouched. */
void cmd_ev_set_voicing(fw *f, fw_regs *r);
/* $BCED, the @A event: cmd_volume = A, then the ROM isr_set_volume (M4.6) with the entry registers. r out: what
   isr_set_volume leaves (A = the new P3 value, X and Y untouched). */
void cmd_ev_set_volume(fw *f, fw_regs *r);
```

with

```c
void cmd_ev_sound_end(fw *f, fw_regs *r);
/* $BCEA, the @B event: cmd_voicing = A. A, X and Y untouched. */
void cmd_ev_set_voicing(fw *f, fw_regs *r);
/* $BCED, the @A event: cmd_volume = A, then isr_set_volume (C since M4.6b) with the entry registers. r out: what
   isr_set_volume leaves (A = the new P3 value, X and Y untouched). */
void cmd_ev_set_volume(fw *f, fw_regs *r);
```

**`src/firmware/cmd_scan.c`**: replace

```c
#include "cmd_scan.h"
#include "cmd_match.h"
#include "isr_addr.h"
#include "data/fw_data.h"

void cmd_scan_clause(fw *f)
```

with

```c
#include "cmd_scan.h"
#include "cmd_match.h"
#include "isr_serial.h"
#include "data/fw_data.h"

void cmd_scan_clause(fw *f)
```

**`src/firmware/cmd_scan.c`**: replace

```c
    fw_wr8(f, (uint16_t)(CMD_POSITIONS + x), a);    /* cmd_positions[0] = $FF: no command yet */
    do {                                            /* $AD09: one boot_clause character */
        fw_regs r = { a, x, y, 0 };
        fw_rom_call(f, ROM_ISR_PAR_READY, &r, FW_ROM_CALL_BUDGET);   /* parallel mode: ready for the next byte */
        x = r.x;
        y = r.y;
        a = fw_rd8(f, (uint16_t)(BOOT_CLAUSE + x++));
```

with

```c
    fw_wr8(f, (uint16_t)(CMD_POSITIONS + x), a);    /* cmd_positions[0] = $FF: no command yet */
    do {                                            /* $AD09: one boot_clause character */
        fw_regs r = { a, x, y, 0 };
        isr_par_ready(f, &r);                                    /* parallel mode: ready for the next byte */
        x = r.x;
        y = r.y;
        a = fw_rd8(f, (uint16_t)(BOOT_CLAUSE + x++));
```

**`src/firmware/cmd_scan.h`**: replace

```c
   command n = cmd_count++ (cmd_positions[n] = its cmd_text position, cmd_handlers[n] = the two bytes after the
   zero that follows the matched part (Y+1, Y+2; on a partial match they are pattern bytes Y+1, Y+2),
   cmd_arg_index[n] = the boot_clause index of its last character); an unmatched cmd_char stays as text, raw. Then boot_clause_len = the cmd_text length and cmd_next_pos = cmd_positions[0]
   ($FF set at the start). isr_par_ready (ROM) runs before each character is read. Scratch: $76, $78. */
void cmd_scan_clause(fw *f);

#endif
```

with

```c
   command n = cmd_count++ (cmd_positions[n] = its cmd_text position, cmd_handlers[n] = the two bytes after the
   zero that follows the matched part (Y+1, Y+2; on a partial match they are pattern bytes Y+1, Y+2),
   cmd_arg_index[n] = the boot_clause index of its last character); an unmatched cmd_char stays as text, raw. Then boot_clause_len = the cmd_text length and cmd_next_pos = cmd_positions[0]
   ($FF set at the start). isr_par_ready (C, M4.6b) runs before each character is read. Scratch: $76, $78. */
void cmd_scan_clause(fw *f);

#endif
```

**`src/firmware/fw.h`**: replace

```c
#define FW_TMP_78    0x0078   /* zero-page scratch, unnamed in the annotations: a saved Y, byte counters;
                                 par_scale_duration's saved Y ($C2A0; not written for a duration of 0) */
#define FW_TMP_75    0x0075   /* zero-page scratch, unnamed in the annotations: tn_convert_clause's saved X;
                                 frm_clamp_mark_space's 2 x b10 ($B936), frm_raise_formants' (b10 - 32) / 4 ($B969)
                                 and frm_transition_flags' count of the rising amplitudes ($BAA7-$BAB7) */
#define FW_TMP_76    0x0076   /* zero-page scratch, unnamed in the annotations: cmd_scan_clause's resume index,
```

with

```c
#define FW_TMP_78    0x0078   /* zero-page scratch, unnamed in the annotations: a saved Y, byte counters;
                                 par_scale_duration's saved Y ($C2A0; not written for a duration of 0) */
#define FW_TMP_75    0x0075   /* zero-page scratch, unnamed in the annotations: tn_convert_clause's saved X;
                                 isr_set_volume's inverted level << 3 ($ACEF);
                                 frm_clamp_mark_space's 2 x b10 ($B936), frm_raise_formants' (b10 - 32) / 4 ($B969)
                                 and frm_transition_flags' count of the rising amplitudes ($BAA7-$BAB7) */
#define FW_TMP_76    0x0076   /* zero-page scratch, unnamed in the annotations: cmd_scan_clause's resume index,
```

**`src/firmware/fw_ops.h`**: replace

```c
/* Small steps the ported routines share (M4.4a spec 2.6), and the two shift routines (M4.4b spec 2.2). */

#include "fw.h"
#include "isr_addr.h"

/* JSR isr_par_ready with the registers the ROM has at the JSR (it preserves A, X and Y). */
static inline void fw_par_ready(fw *f, fw_regs r) { fw_rom_call(f, ROM_ISR_PAR_READY, &r, FW_ROM_CALL_BUDGET); }

/* ADC: a + m + the carry in *c; the carry out back in *c. */
static inline uint8_t fw_adc(uint8_t a, uint8_t m, uint8_t *c)
```

with

```c
/* Small steps the ported routines share (M4.4a spec 2.6), and the two shift routines (M4.4b spec 2.2). */

#include "fw.h"
#include "isr_serial.h"

/* JSR isr_par_ready with the registers the ROM has at the JSR (it preserves A, X and Y): the C (M4.6b). */
static inline void fw_par_ready(fw *f, fw_regs r) { isr_par_ready(f, &r); }

/* ADC: a + m + the carry in *c; the carry out back in *c. */
static inline uint8_t fw_adc(uint8_t a, uint8_t m, uint8_t *c)
```

**Create `src/firmware/isr_dsp.c`:**

```c
#include "isr_dsp.h"
#include "fw_ops.h"

#define P3_KEPT  0x87   /* isr_set_volume $ACF1 LDA #$87 / AND P3: the bits the volume leaves */

/* ---- isr_set_volume $ACE8, isr_volume_half $B46C ---- */

void isr_set_volume(fw *f, fw_regs *r)
{
    fw_wr8(f, FW_TMP_75, util_shl3((uint8_t)((r->a ^ 0x0F) & 0x0F)));   /* EOR #$F; AND #$F; JSR util_shl3; STA $75 */
    r->a = (uint8_t)((fw_sfr8(f, SFR_P3) & P3_KEPT) | fw_rd8(f, FW_TMP_75));   /* LDA #$87; AND P3; ORA $75 */
    fw_wr_sfr8(f, SFR_P3, r->a);                                          /* STA P3 */
}

void isr_volume_half(fw *f, fw_regs *r)
{
    r->a = (uint8_t)(fw_rd8(f, CMD_VOLUME) >> 1);                         /* LDA cmd_volume; LSR A */
    isr_set_volume(f, r);
}
```

**Create `src/firmware/isr_dsp.h`:**

```c
#ifndef FW_ISR_DSP_H
#define FW_ISR_DSP_H
/* Speech timing and the DSP (ROM $ACE8-$ACF9, $B2D9-$B397, $B465-$B472), decompiled (M4.6b spec 2.1, 2.6). Each
   routine takes the registers at entry in r and leaves in r the A, X and Y the ROM leaves at its RTS (or RTI); r->p
   is not used: no caller reads a flag, and the PHP/SEI ... PLP pairs only hold interrupts off, which the C, taking
   no emulated time, needs no more (M4.6b spec 3.5). SFRs are read and written through the host in the ROM's order
   (fw_sfr*), each SEB/CLB as a read then a write. */
#include "fw.h"

/* The SFRs of speech timing (M4.6b spec 2.5; ghidra740/annotations/sfr.txt) */
#define SFR_T2_PRE      0xDE
#define SFR_T2_RELOAD   0xDF
#define SFR_T3_PRE      0xE0
#define SFR_T3_RELOAD   0xE1
#define SFR_P23_FUNC    0xED
#define SFR_P3          0xEE   /* bits 3-6: the inverted volume level; bit 0 set by isr_timer_start */
#define SFR_P2          0xF0   /* bit 0: the DSP reset line */
#define SFR_ICR2        0xFE   /* timers 1-3: enable bits 0, 2, 4, request bits 1, 3, 5 */

/* $ACE8: A = the volume 0-15 (0 silent): P3 bits 3-6 = its inverted level, (A ^ $F) & $F shifted left 3
   (util_shl3), kept in the scratch byte $75; the other P3 bits kept (AND #$87). A = the value written to P3; X and Y
   untouched. */
void isr_set_volume(fw *f, fw_regs *r);
/* $B46C: isr_set_volume with A = cmd_volume >> 1. */
void isr_volume_half(fw *f, fw_regs *r);

#endif
```

**`src/firmware/isr_serial.c`**: replace

```c
    fw_sfr_clb(f, SFR_P0, P0_BUSY);
}
```

with

```c
    fw_sfr_clb(f, SFR_P0, P0_BUSY);
}

void isr_par_ready(fw *f, fw_regs *r)
{
    (void)r;                                                     /* PHA ... PLA ($C5EA): A kept */
    if (fw_rd8(f, BOOT_DIP) & 1)                                 /* BBC0 boot_dip,$C5EA */
        fw_sfr_seb(f, SFR_P0, P0_READY);                         /* SEB3 P0; BRA $C5EA */
}

void isr_par_hold(fw *f, fw_regs *r)
{
    (void)r;                                                     /* PHA ... PLA ($C5EA): A kept */
    if (!(fw_rd8(f, BOOT_DIP) & 1))                              /* BBC0 boot_dip,$C5EA */
        return;
    fw_sfr_clb(f, SFR_P0, P0_READY);                             /* PHP; SEI; CLB3 P0 */
    fw_sfr_clb(f, SFR_ICR1, ICR1_INT1_REQ);                      /* CLB1 ICR1; PLP; NOP */
}
```

**`src/firmware/isr_serial.h`**: replace

```c
/* $C3F4: ICR3 = $48, the busy line P0.5 cleared. */
void isr_host_line_low(fw *f, fw_regs *r);

#endif
```

with

```c
/* $C3F4: ICR3 = $48, the busy line P0.5 cleared. */
void isr_host_line_low(fw *f, fw_regs *r);

/* The parallel host handshake (ROM $C5D6-$C5EB, M4.6b spec 2.1): subroutines that keep A (PHA ... PLA at $C5EA), X
   and Y, in serial mode (boot_dip bit 0 clear) doing nothing else. */
/* $C5D6: parallel mode: P0.3 set, ready for the next host byte. */
void isr_par_ready(fw *f, fw_regs *r);
/* $C5DE: parallel mode: P0.3 cleared and a pending _INT1 request (ICR1 bit 1) cleared, inside PHP/SEI ... PLP. */
void isr_par_hold(fw *f, fw_regs *r);

#endif
```

**`src/firmware/par_addr.h`**: replace

```c
#define ROM_UTIL_SHL4              0xC656   /* called by cmd_apply_pitch $A835, $A83F; cmd_set_formant $A952;
                                               cmd_set_mark_space $A9AD; cmd_hex_byte $B455 (all C: util_shl4 in
                                               fw_ops.h) */
#define ROM_UTIL_SHL3              0xC657   /* called by cmd_ev_sound_start $BBA7 (and isr_set_volume $ACEC); also
                                               entered from util_shl4 $C656 by falling through */

#endif
```

with

```c
#define ROM_UTIL_SHL4              0xC656   /* called by cmd_apply_pitch $A835, $A83F; cmd_set_formant $A952;
                                               cmd_set_mark_space $A9AD; cmd_hex_byte $B455 (all C: util_shl4 in
                                               fw_ops.h) */
#define ROM_UTIL_SHL3              0xC657   /* called by cmd_ev_sound_start $BBA7, isr_set_volume $ACEC (both C:
                                               util_shl3 in fw_ops.h); also entered from util_shl4 $C656 by falling
                                               through */

#endif
```

**`src/firmware/par_play.c`**: replace

```c
#include "frm_build.h"
#include "fw_ops.h"
#include "isr_addr.h"
#include "par_addr.h"
#include "par_start.h"
#include "par_track.h"
#include "ser.h"

#define UART_TX_READY    0x02   /* $B895 BBC1 UART_STATUS: the transmitter is ready */

/* ---- par_play $B877 ---- */
```

with

```c
#include "frm_build.h"
#include "fw_ops.h"
#include "isr_addr.h"
#include "isr_dsp.h"
#include "isr_serial.h"
#include "par_addr.h"
#include "par_start.h"
#include "par_track.h"
#include "ser.h"

#define UART_TX_READY    0x02   /* $B895 BBC1 UART_STATUS: the transmitter is ready */
#define PAR_WAIT_LAST    0xB8C9   /* $B8C9-$B8CC: the frame-flag wait of the last frames */
#define PAR_WAIT_REST    0xB8E6   /* $B8E6-$B8E9: the frame-flag wait of the rest frames */

/* ---- par_play $B877 ---- */
```

**`src/firmware/par_play.c`**: replace

```c
    return n;
}

/* $B8C9-$B8CC, $B8E6-$B8E9: isr_par_ready until the timer interrupt has swapped the buffers and set isr_frame_flag
   bit 0. A failed ROM call ends the wait: under a thunk the fault has longjmped out already, and without a trap (a
   unit test) the flag would never come. */
static void wait_frame_flag(fw *f, fw_regs r)
{
    do
        fw_par_ready(f, r);
    while (!(fw_rd8(f, ISR_FRAME_FLAG) & 1) && !f->fault);
}

/* $B877-$B892: an event is due when cmd_ev_next < cmd_ev_count (CPX / BCS, unsigned) and par_seg_index =
```

with

```c
    return n;
}

/* $B8C9-$B8CC, $B8E6-$B8E9 (at): isr_par_ready, then until the timer interrupt has swapped the buffers and set
   isr_frame_flag bit 0, a wait for the next interrupt (M4.6b spec 2.3) and isr_par_ready again. A failed wait ends
   it: under a thunk the fault has longjmped out already, and without a trap (a unit test) the flag would never
   come. */
static void wait_frame_flag(fw *f, fw_regs r, uint16_t at)
{
    for (;;) {
        fw_par_ready(f, r);
        if ((fw_rd8(f, ISR_FRAME_FLAG) & 1) || f->fault)       /* BBC0 isr_frame_flag */
            return;
        fw_wait(f, FW_WAIT_INTERRUPT, at);
    }
}

/* $B877-$B892: an event is due when cmd_ev_next < cmd_ev_count (CPX / BCS, unsigned) and par_seg_index =
```

**`src/firmware/par_play.c`**: replace

```c
        par_step_tracks(f, r);
        frm_finish_frame(f, r);
        fw_wr8(f, ISR_FRAME_FLAG, 0);
        wait_frame_flag(f, *r);
    }
}
```

with

```c
        par_step_tracks(f, r);
        frm_finish_frame(f, r);
        fw_wr8(f, ISR_FRAME_FLAG, 0);
        wait_frame_flag(f, *r, PAR_WAIT_LAST);
    }
}
```

**`src/firmware/par_play.c`**: replace

```c
        frm_snapshot(f, r);
        frm_clamp_mark_space(f, r);
        frm_transition_flags(f, r);
        wait_frame_flag(f, *r);
        fw_wr8(f, ISR_FRAME_FLAG, 0);
    } while (count_frame(f) != 0);
    cmd_ev_sound_end(f, r);
```

with

```c
        frm_snapshot(f, r);
        frm_clamp_mark_space(f, r);
        frm_transition_flags(f, r);
        wait_frame_flag(f, *r, PAR_WAIT_REST);
        fw_wr8(f, ISR_FRAME_FLAG, 0);
    } while (count_frame(f) != 0);
    cmd_ev_sound_end(f, r);
```

**`src/firmware/par_play.c`**: replace

```c
{
    for (;;) {                                 /* par_play $B877: one pass */
        if (f->fault)
            return;                            /* a failed ROM call without a trap: the ROM would not get past it */
        if (run_due_event(f, r))
            continue;                          /* $B892 JMP par_play */
        serve_reply(f, r);
```

with

```c
{
    for (;;) {                                 /* par_play $B877: one pass */
        if (f->fault)
            return;                            /* a failed call or wait without a trap: the ROM would not get past it */
        if (run_due_event(f, r))
            continue;                          /* $B892 JMP par_play */
        serve_reply(f, r);
```

**`src/firmware/par_play.c`**: replace

```c
            break;                             /* $B8A5 JMP $B8D2: a mute */
        if (!(fw_rd8(f, ISR_FRAME_FLAG) & 1)) {   /* $B8A8 BBS0 isr_frame_flag */
            fw_par_ready(f, *r);               /* $B8AB, then $B8AE JMP par_play */
            continue;
        }
        if (!segment_frame(f, r)) {
```

with

```c
            break;                             /* $B8A5 JMP $B8D2: a mute */
        if (!(fw_rd8(f, ISR_FRAME_FLAG) & 1)) {   /* $B8A8 BBS0 isr_frame_flag */
            fw_par_ready(f, *r);               /* $B8AB, then $B8AE JMP par_play */
            fw_wait(f, FW_WAIT_INTERRUPT, ROM_PAR_PLAY);   /* the next pass after an interrupt (M4.6b spec 2.3) */
            continue;
        }
        if (!segment_frame(f, r)) {
```

**`src/firmware/par_play.c`**: replace

```c
   The registers flow from call to call as the ROM's do; A is loaded only for the two isr_set_volume calls. */
static void play_body(fw *f, fw_regs *r)
{
    fw_rom_call(f, ROM_ISR_PAR_HOLD, r, FW_ROM_CALL_BUDGET);
    par_play_start(f, r);
    fw_rom_call(f, ROM_ISR_SPEECH_START, r, FW_ROM_CALL_BUDGET);
    fw_rom_call(f, ROM_ISR_VOLUME_HALF, r, FW_ROM_CALL_BUDGET);
    fw_rom_call(f, ROM_ISR_WAIT_FRAME, r, FW_ROM_CALL_BUDGET);
    r->a = fw_rd8(f, CMD_VOLUME);
    fw_rom_call(f, ROM_ISR_SET_VOLUME, r, FW_ROM_CALL_BUDGET);
    par_play(f, r);
    fw_rom_call(f, ROM_ISR_VOLUME_HALF, r, FW_ROM_CALL_BUDGET);
    fw_rom_call(f, ROM_ISR_WAIT_FRAME, r, FW_ROM_CALL_BUDGET);
    r->a = 0;
    fw_rom_call(f, ROM_ISR_SET_VOLUME, r, FW_ROM_CALL_BUDGET);
    fw_rom_call(f, ROM_ISR_SPEECH_STOP, r, FW_ROM_CALL_BUDGET);
}
```

with

```c
   The registers flow from call to call as the ROM's do; A is loaded only for the two isr_set_volume calls. */
static void play_body(fw *f, fw_regs *r)
{
    isr_par_hold(f, r);
    par_play_start(f, r);
    fw_rom_call(f, ROM_ISR_SPEECH_START, r, FW_ROM_CALL_BUDGET);
    isr_volume_half(f, r);
    fw_rom_call(f, ROM_ISR_WAIT_FRAME, r, FW_ROM_CALL_BUDGET);
    r->a = fw_rd8(f, CMD_VOLUME);
    isr_set_volume(f, r);
    par_play(f, r);
    isr_volume_half(f, r);
    fw_rom_call(f, ROM_ISR_WAIT_FRAME, r, FW_ROM_CALL_BUDGET);
    r->a = 0;
    isr_set_volume(f, r);
    fw_rom_call(f, ROM_ISR_SPEECH_STOP, r, FW_ROM_CALL_BUDGET);
}
```

**`src/firmware/par_play.h`**: replace

```c
#ifndef FW_PAR_PLAY_H
#define FW_PAR_PLAY_H
/* The frame loop of one utterance (ROM $B877-$B916) and the player around it ($A7D4-$A7DE and its body $B2B3-$B2D8),
   decompiled (M4.4b spec 2.1, 2.3, 2.6). Both wait on the timer interrupt: each wait is a ROM call to isr_par_ready
   (or isr_wait_frame), then a test of isr_frame_flag in RAM; the C itself takes no emulated time, so the interrupt
   is serviced inside the ROM calls. Each routine takes the registers at entry in r and leaves in r the A, X and Y
   that the ROM leaves at its RTS. r->p is not used: no caller reads a flag. */
#include "fw.h"

#define PAR_REST_FRAMES  3   /* par_play $B8D5 LDA #3: the rest frames sent at the end */
```

with

```c
#ifndef FW_PAR_PLAY_H
#define FW_PAR_PLAY_H
/* The frame loop of one utterance (ROM $B877-$B916) and the player around it ($A7D4-$A7DE and its body $B2B3-$B2D8),
   decompiled (M4.4b spec 2.1, 2.3, 2.6). Both wait on the timer interrupt: each polling loop tests isr_frame_flag in
   RAM (and par_play's main loop ser_abort first) and, while it is not yet set, waits for the next interrupt through
   the host (fw_wait FW_WAIT_INTERRUPT, M4.6b spec 2.3) before testing again; the C itself takes no emulated time, so
   the interrupt is serviced inside the wait. isr_par_ready is called on every pass where the ROM calls it. Each
   routine takes the registers at entry in r and leaves in r the A, X and Y that the ROM leaves at its RTS. r->p is
   not used: no caller reads a flag. */
#include "fw.h"

#define PAR_REST_FRAMES  3   /* par_play $B8D5 LDA #3: the rest frames sent at the end */
```

**`src/firmware/pros_addr.h`**: replace

```c
#define FW_PROS_ADDR_H
/* ROM code addresses of the segment generator (M4.3a spec 2.2) and of the stress and phrase passes before it
   (M4.3b spec 2.2), checked against build/ghidra740/dump and the JSR/JMP operands in the ROM image.
   isr_par_ready, the one ROM routine they call, is ROM_ISR_PAR_READY in isr_addr.h. */
#define ROM_PH_APPEND_PHRASE_PAUSE  0xC9B8   /* exits with JMP ROM_PROS_SEG_TAIL */
#define ROM_PH_APPEND_PAUSE         0xCA15
#define ROM_PROS_APPEND_SEGMENT     0xCA3B
```

with

```c
#define FW_PROS_ADDR_H
/* ROM code addresses of the segment generator (M4.3a spec 2.2) and of the stress and phrase passes before it
   (M4.3b spec 2.2), checked against build/ghidra740/dump and the JSR/JMP operands in the ROM image.
   isr_par_ready, the one interrupt routine they call, is C since M4.6b (ROM_ISR_PAR_READY in isr_addr.h). */
#define ROM_PH_APPEND_PHRASE_PAUSE  0xC9B8   /* exits with JMP ROM_PROS_SEG_TAIL */
#define ROM_PH_APPEND_PAUSE         0xCA15
#define ROM_PROS_APPEND_SEGMENT     0xCA3B
```

**`src/firmware/pros_seg.c`**: replace

```c
#include "pros_seg.h"
#include "fw_ops.h"
#include "isr_addr.h"

/* STA (ptr),Y: the 16-bit pointer plus y. The pointers start at the arrays ($0200, $0600, $0A00, $0E00) and only
   move up one page per 256 segments; a clause makes far fewer than 1,280 segments (at most 3 per phonetic symbol
```

with

```c
#include "pros_seg.h"
#include "fw_ops.h"

/* STA (ptr),Y: the 16-bit pointer plus y. The pointers start at the arrays ($0200, $0600, $0A00, $0E00) and only
   move up one page per 256 segments; a clause makes far fewer than 1,280 segments (at most 3 per phonetic symbol
```

**`src/firmware/pros_seg.c`**: replace

```c
void pros_append_segment(fw *f, uint8_t a, uint8_t x, uint8_t y)
{
    fw_regs r = { a, x, y, 0 };
    fw_rom_call(f, ROM_ISR_PAR_READY, &r, FW_ROM_CALL_BUDGET);
    uint8_t i = fw_rd8(f, PROS_SEG_COUNT);
    seg_store(f, PROS_PHONEME_PTR, i, fw_rd8(f, PH_PHONEME));
    seg_store(f, PROS_DURATION_PTR, i, fw_rd8(f, PH_DURATION));
```

with

```c
void pros_append_segment(fw *f, uint8_t a, uint8_t x, uint8_t y)
{
    fw_par_ready(f, (fw_regs){ a, x, y, 0 });
    uint8_t i = fw_rd8(f, PROS_SEG_COUNT);
    seg_store(f, PROS_PHONEME_PTR, i, fw_rd8(f, PH_PHONEME));
    seg_store(f, PROS_DURATION_PTR, i, fw_rd8(f, PH_DURATION));
```

**`src/firmware/pros_seg.h`**: replace

```c
/* $CA71: pros_phoneme_ptr = pros_seg_phoneme ($0200), pros_duration_ptr = pros_seg_duration ($0600),
   pros_pitch_ptr = pros_seg_pitch ($0A00), pros_flags_ptr = pros_seg_flags ($0E00). The ROM leaves A = $0E. */
void pros_reset_seg_ptrs(fw *f);
/* $CA3B: the ROM isr_par_ready with A = a, X = x, Y = y first, then one segment: ph_phoneme, ph_duration, and
   pros_last_target = pros_target + cmd_pitch_step (8 bits) as its pitch; flags $80. A, X and Y are preserved
   (isr_par_ready preserves them; the ROM pushes A and Y after it and pulls them at $CA6D). */
void pros_append_segment(fw *f, uint8_t a, uint8_t x, uint8_t y);
```

with

```c
/* $CA71: pros_phoneme_ptr = pros_seg_phoneme ($0200), pros_duration_ptr = pros_seg_duration ($0600),
   pros_pitch_ptr = pros_seg_pitch ($0A00), pros_flags_ptr = pros_seg_flags ($0E00). The ROM leaves A = $0E. */
void pros_reset_seg_ptrs(fw *f);
/* $CA3B: isr_par_ready (C since M4.6b) with A = a, X = x, Y = y first, then one segment: ph_phoneme, ph_duration, and
   pros_last_target = pros_target + cmd_pitch_step (8 bits) as its pitch; flags $80. A, X and Y are preserved
   (isr_par_ready preserves them; the ROM pushes A and Y after it and pulls them at $CA6D). */
void pros_append_segment(fw *f, uint8_t a, uint8_t x, uint8_t y);
```

**`src/firmware/tn_convert.c`**: replace

```c
#include "tn_convert.h"
#include "cmd_run.h"
#include "isr_addr.h"
#include "lts.h"
#include "par_play.h"
#include "pros_make.h"
```

with

```c
#include "tn_convert.h"
#include "cmd_run.h"
#include "isr_serial.h"
#include "lts.h"
#include "par_play.h"
#include "pros_make.h"
```

**`src/firmware/tn_convert.c`**: replace

```c
static uint8_t char_class(fw *f, uint8_t c) { return fw_rom8(f, (uint16_t)(LTS_CHAR_CLASS + c)); }
static uint8_t text_at(const fw *f, uint8_t x) { return fw_rd8(f, (uint16_t)(CMD_TEXT + x)); }

/* A ROM helper that reads only X (isr_par_ready); returns the X it leaves. */
static uint8_t rom_call_x(fw *f, uint16_t addr, uint8_t x)
{
    fw_regs r = { 0, x, 0, 0 };
    fw_rom_call(f, addr, &r, FW_ROM_CALL_BUDGET);
    return r.x;
}

/* tn_phonetic[tn_phon_len++] = c */
static void put_phonetic(fw *f, uint8_t c)
```

with

```c
static uint8_t char_class(fw *f, uint8_t c) { return fw_rom8(f, (uint16_t)(LTS_CHAR_CLASS + c)); }
static uint8_t text_at(const fw *f, uint8_t x) { return fw_rd8(f, (uint16_t)(CMD_TEXT + x)); }

/* tn_phonetic[tn_phon_len++] = c */
static void put_phonetic(fw *f, uint8_t c)
```

**`src/firmware/tn_convert.c`**: replace

```c
        fw_wr8(f, TN_POS, x);
        fw_wr8(f, TN_KEY_POS, x);
        for (;;) {                                              /* per character: $B1F6 */
            x = rom_call_x(f, ROM_ISR_PAR_READY, x);
            if (fw_rd8(f, TN_PHON_LEN) >= TN_PHONETIC_FLUSH)
                break;                                          /* nearly full: speak it now */
            for (;;) {                                          /* $B20A: the deferred commands up to tn_pos */
```

with

```c
        fw_wr8(f, TN_POS, x);
        fw_wr8(f, TN_KEY_POS, x);
        for (;;) {                                              /* per character: $B1F6 */
            fw_regs pr = { 0, x, 0, 0 };
            isr_par_ready(f, &pr);                              /* X kept */
            if (fw_rd8(f, TN_PHON_LEN) >= TN_PHONETIC_FLUSH)
                break;                                          /* nearly full: speak it now */
            for (;;) {                                          /* $B20A: the deferred commands up to tn_pos */
```

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test fw"`, then `build/apollo2_tests.exe machine_hooks`, `build/apollo2_tests.exe diff_lts`, `build/apollo2_tests.exe diff_play` and `build/apollo2_tests.exe diff_serial`.
Expected:

- the build passes with no warnings (the firmware syntax check included); `fw`: the nine suites `ok`, `9 suite(s), 0 failure(s), 0 skip(s)`; `suite_machine_hooks ok`;
- `diff_lts`: 91 lines `<name> N calls checked`, the 87 of Task 1 unchanged and `isr_set_volume 772 calls checked`, `isr_volume_half 360 calls checked`, `isr_par_ready 7959385 calls checked`, `isr_par_hold 3318 calls checked` (every call: `k_every_call`), `1 mid-clause flush calls skipped (golden_hybrid covers them)`, `2 unwound calls compared`, 0 mismatches (137 s in the proof run, against 17 s in Task 1: `isr_par_ready`'s every call, two machine clones each, makes the difference);
- `diff_play`: the lines of Task 2 Step 2 unchanged (`boot_loop_play 179 calls checked, 48 with both injections`, `par_play 126 calls checked, 48 with both injections`, `$B8A5 (abort) reached in 96 calls, $B89C (reply due) in 96`), 0 mismatches: the C `par_play` now waits for each interrupt at its back edge (34 s);
- `diff_serial`: as in Task 2 Step 2 but `par_ready isr_par_ready 2649 calls compared (C), $C5D7 -> $C5DA in 2649` and `par_hold isr_par_hold 2649 calls compared (C), $C5DF -> $C5E2 in 2649` on the corpus.

A mismatch is the C differing from the ROM: find it in the dump; never loosen a comparison.

- [ ] **Step 5: Run the full suite**

Run: `cmd //c ".\build.bat test"` (in the background)
Expected:

- `37 suite(s), 0 failure(s), 0 skip(s)`, no warnings, the firmware syntax check passing (695 s in the proof run);
- `firmware_data`: `48512 converted bytes compared with the ROM, 0 differ`;
- `diff_lts`, `diff_play` and `diff_serial` as in Step 4, 0 mismatches;
- `golden_hybrid`: `48 corpus files x 94 modes (all, then each routine alone)`, every file identical, every golden unchanged (`git status --short tests/golden` prints nothing);
- `suite_golden` (`test_timing_invariance` on both engines): `timer scale 1/4: 105 frames, 11789152 CPU cycles` and `timer scale 3/1: 105 frames, 1286784 CPU cycles` (rom, unchanged), `hybrid timer scale 1/4: 105 frames, 11693547 CPU cycles`, `hybrid timer scale 3/1: 105 frames, 989919 CPU cycles` (fewer: the polls wait for the interrupt instead of calling the ROM `isr_par_ready`);
- `suite_synth`: `corpus peak plain 20522 (-4.1 dBFS)`, `corpus peak b11 16038 (-6.2 dBFS)`.

- [ ] **Step 6: Commit**

```bash
git add src/firmware/isr_dsp.h src/firmware/isr_dsp.c src/firmware/isr_serial.h src/firmware/isr_serial.c src/firmware/fw_ops.h src/firmware/fw.h src/firmware/par_play.h src/firmware/par_play.c src/firmware/cmd_play_event.c src/firmware/cmd_play_event.h src/firmware/cmd_scan.c src/firmware/cmd_scan.h src/firmware/pros_seg.c src/firmware/pros_seg.h src/firmware/tn_convert.c src/firmware/par_addr.h src/firmware/cmd_addr.h src/firmware/pros_addr.h src/engine/thunks.c tests/test_fw_dsp.c tests/test_fw_par.c tests/test_fw_cmd.c tests/test_fw_pros.c tests/test_fw_tn.c tests/test_golden_hybrid.c tests/test_machine_hooks.c tests/test_diff_serial.c
git commit -m "isr_set_volume, isr_volume_half, isr_par_ready and isr_par_hold in C, with hooks; the C callers call them directly, and par_play's polling loops wait for an interrupt through the host (M4.6b 2.1-2.6, 3.2, 4.5)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 4: DSP control and the frame waits in C: `isr_dsp_reset` $B38B, `isr_timer_start` $B35D, `isr_speech_stop` $B398, `isr_speech_start` $B323, `isr_wait_frames` $B356, `isr_wait_frame` $B465, with their hooks; `boot_loop_play` and `par_play` call them; `diff_play` watches the three frame waits (spec §2.1–§2.6, §3.2, §3.5, §4.2, §4.8 item 4)

**ROM facts checked / what was verified while proving the plan** (`rom_B38B_isr_dsp_reset.txt`, `rom_B35D_isr_timer_start.txt`, `rom_B398_isr_speech_stop.txt`, `rom_B323_isr_speech_start.txt`, `rom_B356_isr_wait_frames.txt`, `rom_B465_isr_wait_frame.txt`, `rom_A7D4_boot_loop_play.txt`, `rom_B877_par_play.txt`):
- **`isr_dsp_reset`** $B38B: `PHP; SEI; CLB0 P2; NOP; SEB0 P2; NOP; CLB0 P2; PLP; NOP; RTS`: three read-modify-writes of P2, bit 0 only, so the window (bit 7) does not change (`test_dsp_reset`). **`isr_speech_stop`** $B398: `LDM #0,P23_FUNC; LDM #0,ICR2; JSR isr_dsp_reset; CLB0 boot_speaking; RTS` (`LDM` writes without reading).
- **`isr_timer_start`** $B35D: `PHP; SEI; LDM #0,T2_PRE; LDM #0,T3_PRE; DEC isr_tick_div; DEC isr_tick_div; SEB0 P3; LDM #4,T2_RELOAD; CLB3 ICR2; CLB2 ICR2; BBC3 ICR2,$B372; LDM #$28,T3_RELOAD; CLB5 ICR2; CLB4 ICR2; LDM #1,P23_FUNC; BBC5 ICR2,$B37F; LDM #$31,T3_RELOAD; LDM #$10,ICR2; PLP; NOP; RTS`. The C makes the SFR accesses in that order (`test_timer_start` logs all 19 accesses and waits) and the two waits through the host (`FW_WAIT_TIMER2` at $B372, `FW_WAIT_TIMER3` at $B37F), which the hybrid host runs masked. `isr_tick_div` is decremented twice, 10 → 8, both RAM writes kept. Its `PHP`/`SEI`/`PLP` are comments in the C (spec §3.5).
- **`isr_speech_start`** $B323: `LDX #0; STX frm_source_index; STX frm_filter_index; LDA cmd_source_prosody`, then `ASL A; ROL frm_source_index` three times and `ASL A; ROL frm_filter_index` once (the source index = bits 7–5, the filter index = bit 4), `LDM #$0A,isr_tick_div; LDM #$0A,isr_words_left; LDA #0; STA isr_word_ofs; STA isr_frame_flag; JSR isr_dsp_reset; JSR isr_timer_start; LDX #1; JSR isr_wait_frames; RTS`: A = 0 and X = 0 out (`isr_wait_frames`' `DEX`), Y untouched. **`isr_wait_frames`** $B356: `JSR isr_wait_frame; DEX; BNE $B356; RTS`. **`isr_wait_frame`** $B465: `BBC0 isr_frame_flag,$B465; LDM #0,isr_frame_flag; RTS`: the C waits for an interrupt between tests (`at` = $B465) and clears the whole byte; with the flag already set it does not wait at all (so at `par_play`'s end, $B8D2, where the frame loop has just seen the flag, the C makes no wait; `test_fw_par.c`'s `check_rest_frames` has no record for it any more).
- **The callers' reads** (the thunk comments): `isr_speech_start`'s one caller, `boot_play_body` $B2B9, calls `isr_volume_half` (`LDA cmd_volume` first). `isr_wait_frames`' one caller, `isr_speech_start` $B352, returns its X as its own. `isr_timer_start` ($B34D) is followed by `LDX #1`; `isr_dsp_reset` ($B34A, $B39E) by `isr_timer_start` or `CLB0`/`RTS`; `isr_speech_stop` ($B2D5) by `boot_play_body`'s `RTS`. `isr_wait_frame` is followed by `LDA` ($B2C2, $B2D0, $B8D5) or `DEX` ($B359: X read). No caller reads a flag. All six rows declare A, X and Y; the three that wait are `waits` rows, the other three are compared by `diff_lts` at every call (`k_every_call`).
- **`diff_play` watches the three frame waits** (spec §2.4: `diff_play` and the goldens compare them): `isr_wait_frames` and `isr_wait_frame` re-enter their own entries (`BNE $B356`, `BBC0 $B465`), so, as for `par_play`, only a real call counts: the hooks on the `JSR`s arm the next entry ($B352 for `isr_wait_frames`; $B2BF, $B2CD, $B8D2 and $B356, `isr_wait_frames`' own first instruction, for `isr_wait_frame`); `isr_speech_start` is entered only by `JSR` ($B2B9). Each waits one frame per call, so none gets an injection. `WATCH_MAX` is 5; the self-check runs all five ROM against ROM.
- **The mocks of the playback tests**: `test_fw_par.c`'s `test_loop_play` and `test_fw_tn.c`'s flush now see `boot_loop_play`'s speech timing as SFR writes and waits: the two timer waits, then a frame waited for at $B465 (`isr_speech_start`), a frame after the half volume, `par_play`'s end and the rest frames, a frame after the half volume again; `test_fw_tn.c`'s mute comes with the first timer wait (it came with the ROM `isr_speech_start` call).

**Files:**
- Modify: `src/firmware/isr_dsp.h`, `src/firmware/isr_dsp.c`, `src/firmware/par_play.h`, `src/firmware/par_play.c`, `src/engine/thunks.h`, `src/engine/thunks.c`, `tests/test_fw_dsp.c`, `tests/test_fw_par.c`, `tests/test_fw_tn.c`, `tests/test_diff_play.c`

**Interfaces:**
- Consumes: Tasks 1–3 (`fw_wait` with `FW_WAIT_TIMER2`/`TIMER3`/`INTERRUPT`, `FW_WAIT_TIMER2_LOOP`/`TIMER3_LOOP`, `ROM_ISR_WAIT_FRAME` as the `at` of `isr_wait_frame`, the SFR names of `isr_dsp.h`, `fw_clb` of `fw_ops.h`), `diff_play`'s `check_all` and `watched`.
- Produces:

```c
/* src/firmware/isr_dsp.h */
void isr_dsp_reset(fw *f, fw_regs *r);
void isr_timer_start(fw *f, fw_regs *r);
void isr_speech_stop(fw *f, fw_regs *r);
void isr_speech_start(fw *f, fw_regs *r);
void isr_wait_frames(fw *f, fw_regs *r);
void isr_wait_frame(fw *f, fw_regs *r);
/* thunks.c: 6 rows (99 replacements), isr_speech_start, isr_wait_frames and isr_wait_frame marked waits */
/* tests/test_diff_play.c: WATCH_MAX 5, FRAMES_CALL_SITE, k_frame_call_sites, the five watched routines */
```

- [ ] **Step 1: Write the failing tests**

**`tests/test_diff_play.c`**: replace

```c
/* Live differential test of the routines that wait on the timer interrupt (M4.4b spec 4.1): par_play $B877 and
   boot_loop_play $A7D4, the replacements marked `waits`, which diff_lts skips (it holds interrupts). Every corpus
   file and the boot run go through the ROM engine; at every call (no sampling) the machine is cloned twice with
   interrupts live. Copy A runs the ROM routine to its return, copy B the C thunk. Both record, at every buffer swap
   ($B306, the machine's frame callback), the produced frame and a snapshot: all RAM but the SFRs ($DA-$FF) and the
```

with

```c
/* Live differential test of the routines that wait on the timer interrupt (M4.4b spec 4.1): par_play $B877,
   boot_loop_play $A7D4 and the frame waits isr_speech_start $B323, isr_wait_frames $B356 and isr_wait_frame $B465
   (M4.6b spec 2.4), the replacements marked `waits`, which diff_lts skips (it holds interrupts). Every corpus
   file and the boot run go through the ROM engine; at every call (no sampling) the machine is cloned twice with
   interrupts live. Copy A runs the ROM routine to its return, copy B the C thunk. Both record, at every buffer swap
   ($B306, the machine's frame callback), the produced frame and a snapshot: all RAM but the SFRs ($DA-$FF) and the
```

**`tests/test_diff_play.c`**: replace

```c
   isr_tick_div $59, isr_word_ofs $5A and isr_words_left $5B (they depend on how far into a frame each copy
   returns; every swap has compared them already).
   par_play re-enters $B877 by its own JMP par_play ($B892, $B8AE, $B914) with the same stack, so only a real call
   counts: the hook on the JSR par_play at $B2C7 (boot_play_body) arms the next $B877 entry. boot_loop_play is
   entered only by JSR. In a corpus file the boot's own call (from boot_selftest_sound $A7BA) and the par_play inside
   it are the boot run's; they are checked there only.
   The receive interrupt's two effects that no corpus text reaches are injected into both copies at the frame event
   (M4.6b spec 4.2), on the first call of each routine in each run whose ROM copy swapped the buffers at least
   twice. The frame event is the machine's frame callback in both copies: the ROM timer interrupt reaches $B306, the
```

with

```c
   isr_tick_div $59, isr_word_ofs $5A and isr_words_left $5B (they depend on how far into a frame each copy
   returns; every swap has compared them already).
   par_play re-enters $B877 by its own JMP par_play ($B892, $B8AE, $B914) with the same stack, so only a real call
   counts: the hook on the JSR par_play at $B2C7 (boot_play_body) arms the next $B877 entry. So do the JSRs of
   isr_wait_frames ($B352) and isr_wait_frame ($B2BF, $B2CD, $B8D2, and $B356, the first instruction of
   isr_wait_frames), whose own loops re-enter them (BNE $B356, BBC0 $B465). boot_loop_play and isr_speech_start are
   entered only by JSR. In a corpus file the boot's own call (from boot_selftest_sound $A7BA) and the calls inside it
   are the boot run's; they are checked there only.
   The receive interrupt's two effects that no corpus text reaches are injected into both copies at the frame event
   (M4.6b spec 4.2), on the first call of each routine in each run whose ROM copy swapped the buffers at least
   twice. The frame event is the machine's frame callback in both copies: the ROM timer interrupt reaches $B306, the
```

**`tests/test_diff_play.c`**: replace

```c
#define PLAY_REPLY         0xB89C          /* par_play: JSR ser_tx_service (a reply due) */
#define PLAY_ABORT         0xB8A5          /* par_play: JMP $B8D2 (ser_abort bit 0: the end) */
#define BOOT_SOUND_RETURN  0xA7BD          /* after boot_selftest_sound's JSR boot_loop_play at $A7BA */
#define INJECT_SWAP        1               /* the second swap of a call */
#define ABORT_TICKS        2               /* the mute: at the second timer interrupt after that swap */
#define WATCH_MAX          2

static const char *const k_corpus[] = { CORPUS_NAMES };
```

with

```c
#define PLAY_REPLY         0xB89C          /* par_play: JSR ser_tx_service (a reply due) */
#define PLAY_ABORT         0xB8A5          /* par_play: JMP $B8D2 (ser_abort bit 0: the end) */
#define BOOT_SOUND_RETURN  0xA7BD          /* after boot_selftest_sound's JSR boot_loop_play at $A7BA */
#define FRAMES_CALL_SITE   0xB352          /* isr_speech_start: JSR isr_wait_frames */
#define INJECT_SWAP        1               /* the second swap of a call */
#define ABORT_TICKS        2               /* the mute: at the second timer interrupt after that swap */
#define WATCH_MAX          5

/* The JSR isr_wait_frame sites but the one at isr_wait_frames' entry: boot_play_body's two and par_play's */
static const uint16_t k_frame_call_sites[] = { 0xB2BF, 0xB2CD, 0xB8D2 };

static const char *const k_corpus[] = { CORPUS_NAMES };
```

**`tests/test_diff_play.c`**: replace

```c
static watch g_watch[WATCH_MAX];
static size_t g_nwatch;
static const char *g_source;
static bool g_boot_run, g_blp_checked, g_play_armed;
static unsigned g_mismatches, g_reached_abort, g_reached_reply;
static bool g_hit_abort, g_hit_reply;
```

with

```c
static watch g_watch[WATCH_MAX];
static size_t g_nwatch;
static const char *g_source;
static bool g_boot_run, g_blp_checked, g_play_armed, g_frames_armed, g_frame_armed;
static unsigned g_mismatches, g_reached_abort, g_reached_reply;
static bool g_hit_abort, g_hit_reply;
```

**`tests/test_diff_play.c`**: replace

```c
    return false;
}

static void install(machine *m, void *user)
{
    (void)user;
    CHECK(machine_hook(m, ROM_BOOT_LOOP_PLAY, on_loop_play, NULL));
    CHECK(machine_hook(m, PLAY_CALL_SITE, on_play_call, NULL));
    CHECK(machine_hook(m, ROM_PAR_PLAY, on_play, NULL));
}

static void run(const uint8_t *rom, size_t len, const char *source, const char *text, bool boot)
```

with

```c
    return false;
}

/* The watch of the routine at addr checks this call, when it is watched. */
static void check_watched(machine *m, uint16_t addr)
{
    watch *w = watched(addr);
    if (w)
        check_all(m, w);
}

/* $B323: isr_speech_start, inside a boot_loop_play call checked here. */
static bool on_speech_start(machine *m, void *user)
{
    (void)user;
    if (g_blp_checked)
        check_watched(m, ROM_ISR_SPEECH_START);
    return false;
}

/* $B352: isr_speech_start's JSR isr_wait_frames arms the next $B356 entry. */
static bool on_frames_call(machine *m, void *user)
{
    (void)m;
    (void)user;
    g_frames_armed = g_blp_checked;
    return false;
}

/* $B356: isr_wait_frames, a real call only when armed (its BNE $B356 re-enters here); its own JSR isr_wait_frame
   arms the next $B465 entry, at every pass. */
static bool on_frames(machine *m, void *user)
{
    (void)user;
    if (g_frames_armed) {
        g_frames_armed = false;
        check_watched(m, ROM_ISR_WAIT_FRAMES);
    }
    g_frame_armed = g_blp_checked;
    return false;
}

/* $B2BF, $B2CD, $B8D2: a JSR isr_wait_frame arms the next $B465 entry. */
static bool on_frame_call(machine *m, void *user)
{
    (void)m;
    (void)user;
    g_frame_armed = g_blp_checked;
    return false;
}

/* $B465: isr_wait_frame, a real call only when armed (its BBC0 $B465 re-enters here). */
static bool on_frame(machine *m, void *user)
{
    (void)user;
    if (g_frame_armed) {
        g_frame_armed = false;
        check_watched(m, ROM_ISR_WAIT_FRAME);
    }
    return false;
}

static void install(machine *m, void *user)
{
    (void)user;
    CHECK(machine_hook(m, ROM_BOOT_LOOP_PLAY, on_loop_play, NULL));
    CHECK(machine_hook(m, PLAY_CALL_SITE, on_play_call, NULL));
    CHECK(machine_hook(m, ROM_PAR_PLAY, on_play, NULL));
    CHECK(machine_hook(m, ROM_ISR_SPEECH_START, on_speech_start, NULL));
    CHECK(machine_hook(m, FRAMES_CALL_SITE, on_frames_call, NULL));
    CHECK(machine_hook(m, ROM_ISR_WAIT_FRAMES, on_frames, NULL));
    for (size_t i = 0; i < sizeof k_frame_call_sites / sizeof k_frame_call_sites[0]; i++)
        CHECK(machine_hook(m, k_frame_call_sites[i], on_frame_call, NULL));
    CHECK(machine_hook(m, ROM_ISR_WAIT_FRAME, on_frame, NULL));
}

static void run(const uint8_t *rom, size_t len, const char *source, const char *text, bool boot)
```

**`tests/test_diff_play.c`**: replace

```c
    frame_list fl = { 0 };
    g_source = source;
    g_boot_run = boot;
    g_blp_checked = g_play_armed = false;
    for (size_t i = 0; i < g_nwatch; i++)
        g_watch[i].done_injecting = false;
    machine_status st = rom_engine_run(&g_m, rom, len, NULL, install, NULL, text, boot, &fl, NULL);
```

with

```c
    frame_list fl = { 0 };
    g_source = source;
    g_boot_run = boot;
    g_blp_checked = g_play_armed = g_frames_armed = g_frame_armed = false;
    for (size_t i = 0; i < g_nwatch; i++)
        g_watch[i].done_injecting = false;
    machine_status st = rom_engine_run(&g_m, rom, len, NULL, install, NULL, text, boot, &fl, NULL);
```

**`tests/test_diff_play.c`**: replace

```c
    w->run_b = r ? run_thunk_b : run_rom;
}

/* ROM against ROM on hello: both routines, both injections; every pair must agree and both branches be reached. */
static void self_check(const uint8_t *rom, size_t len)
{
    char *text = test_read_file("tests/corpus/hello.txt");
```

with

```c
    w->run_b = r ? run_thunk_b : run_rom;
}

/* ROM against ROM on hello: the five routines, both injections; every pair must agree and both branches be
   reached. */
static void self_check(const uint8_t *rom, size_t len)
{
    char *text = test_read_file("tests/corpus/hello.txt");
```

**`tests/test_diff_play.c`**: replace

```c
    g_nwatch = 0;
    add_watch("par_play", ROM_PAR_PLAY, NULL);
    add_watch("boot_loop_play", ROM_BOOT_LOOP_PLAY, NULL);
    g_reached_abort = g_reached_reply = 0;
    run(rom, len, "self-check hello", text, false);
    free(text);
    printf("  self-check (ROM against ROM, hello): par_play %u calls, boot_loop_play %u calls, %u injected; $B8A5 "
           "reached %u, $B89C reached %u\n", g_watch[0].calls, g_watch[1].calls, g_watch[0].injected +
           g_watch[1].injected, g_reached_abort, g_reached_reply);
    CHECK(g_watch[0].calls > 0 && g_watch[1].calls > 0);
    CHECK_EQ(g_watch[0].injected, 1);
    CHECK_EQ(g_watch[1].injected, 1);
    CHECK(g_reached_abort > 0);
```

with

```c
    g_nwatch = 0;
    add_watch("par_play", ROM_PAR_PLAY, NULL);
    add_watch("boot_loop_play", ROM_BOOT_LOOP_PLAY, NULL);
    add_watch("isr_speech_start", ROM_ISR_SPEECH_START, NULL);
    add_watch("isr_wait_frames", ROM_ISR_WAIT_FRAMES, NULL);
    add_watch("isr_wait_frame", ROM_ISR_WAIT_FRAME, NULL);
    g_reached_abort = g_reached_reply = 0;
    run(rom, len, "self-check hello", text, false);
    free(text);
    printf("  self-check (ROM against ROM, hello): par_play %u calls, boot_loop_play %u calls, %u injected; $B8A5 "
           "reached %u, $B89C reached %u\n", g_watch[0].calls, g_watch[1].calls, g_watch[0].injected +
           g_watch[1].injected, g_reached_abort, g_reached_reply);
    printf("  self-check (ROM against ROM, hello): isr_speech_start %u calls, isr_wait_frames %u, isr_wait_frame %u\n",
           g_watch[2].calls, g_watch[3].calls, g_watch[4].calls);
    for (size_t i = 0; i < g_nwatch; i++)
        CHECK(g_watch[i].calls > 0);
    CHECK_EQ(g_watch[0].injected, 1);
    CHECK_EQ(g_watch[1].injected, 1);
    CHECK(g_reached_abort > 0);
```

**`tests/test_diff_play.c`**: replace

```c
        const fw_replacement *r = &fw_replacements[i];
        if (!r->waits)
            continue;
        CHECK(g_nwatch < WATCH_MAX && (r->addr == ROM_PAR_PLAY || r->addr == ROM_BOOT_LOOP_PLAY));
        if (g_nwatch < WATCH_MAX)
            add_watch(r->name, r->addr, r);
    }
    printf("  %zu routines wait on the timer interrupt\n", g_nwatch);
    CHECK_EQ(g_nwatch, 2);   /* par_play and boot_loop_play (M4.4b spec 2.5) */
    if (g_nwatch > 0) {
        g_reached_abort = g_reached_reply = 0;
        run_corpus(rom, len);
```

with

```c
        const fw_replacement *r = &fw_replacements[i];
        if (!r->waits)
            continue;
        CHECK(g_nwatch < WATCH_MAX && (r->addr == ROM_PAR_PLAY || r->addr == ROM_BOOT_LOOP_PLAY ||
                                       r->addr == ROM_ISR_SPEECH_START || r->addr == ROM_ISR_WAIT_FRAMES ||
                                       r->addr == ROM_ISR_WAIT_FRAME));
        if (g_nwatch < WATCH_MAX)
            add_watch(r->name, r->addr, r);
    }
    printf("  %zu routines wait on the timer interrupt\n", g_nwatch);
    CHECK_EQ(g_nwatch, 5);   /* par_play and boot_loop_play (M4.4b spec 2.5), the frame waits (M4.6b spec 2.4) */
    if (g_nwatch > 0) {
        g_reached_abort = g_reached_reply = 0;
        run_corpus(rom, len);
```

**`tests/test_fw_dsp.c`**: replace

```c
static event g_log[MAX_LOG];
static unsigned g_nlog;
static bool g_wait_ok;           /* what the stub's wait returns */

static void log_add(ev_kind kind, uint16_t addr, uint8_t value)
{
```

with

```c
static event g_log[MAX_LOG];
static unsigned g_nlog;
static bool g_wait_ok;           /* what the stub's wait returns */
static unsigned g_iwaits;        /* FW_WAIT_INTERRUPT waits so far */
static unsigned g_flag_every;    /* the scripted timer interrupt sets isr_frame_flag bit 0 at every n-th (0: never) */

static void log_add(ev_kind kind, uint16_t addr, uint8_t value)
{
```

**`tests/test_fw_dsp.c`**: replace

```c
{
    (void)ctx;
    log_add(EV_WAIT, at, (uint8_t)kind);
    return g_wait_ok;
}
```

with

```c
{
    (void)ctx;
    log_add(EV_WAIT, at, (uint8_t)kind);
    if (kind == FW_WAIT_INTERRUPT && g_wait_ok && g_flag_every && ++g_iwaits % g_flag_every == 0)
        g_ram[ISR_FRAME_FLAG] |= 1;
    return g_wait_ok;
}
```

**`tests/test_fw_dsp.c`**: replace

```c
    g_host.frame_done = stub_frame_done;
    g_nlog = 0;
    g_wait_ok = true;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
}
```

with

```c
    g_host.frame_done = stub_frame_done;
    g_nlog = 0;
    g_wait_ok = true;
    g_iwaits = 0;
    g_flag_every = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
}
```

**`tests/test_fw_dsp.c`**: replace

```c
    check_row("isr_volume_half", 0xB46C, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_par_ready", 0xC5D6, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_par_hold", 0xC5DE, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

/* isr_set_volume $ACE8 at all 16 levels: $75 = ((A ^ $F) & $F) << 3 ($ACE8-$ACEF); P3 read once (AND P3) and written
```

with

```c
    check_row("isr_volume_half", 0xB46C, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_par_ready", 0xC5D6, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_par_hold", 0xC5DE, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_timer_start", 0xB35D, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_dsp_reset", 0xB38B, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_speech_stop", 0xB398, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    static const char *const waits[] = { "isr_speech_start", "isr_wait_frames", "isr_wait_frame" };
    static const uint16_t addrs[] = { 0xB323, 0xB356, 0xB465 };
    for (unsigned i = 0; i < 3; i++) {          /* they wait on the timer interrupt: diff_play checks them */
        const fw_replacement *r = fw_replacement_find(waits[i]);
        CHECK(r != NULL);
        if (!r)
            continue;
        CHECK_EQ(r->addr, addrs[i]);
        CHECK_EQ(r->outputs, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
        CHECK(r->waits);
        CHECK(!r->isr);
    }
}

/* isr_set_volume $ACE8 at all 16 levels: $75 = ((A ^ $F) & $F) << 3 ($ACE8-$ACEF); P3 read once (AND P3) and written
```

**`tests/test_fw_dsp.c`**: replace

```c
    CHECK(!g_f.fault);
}

void suite_fw_dsp(void)
{
    CHECK(fw_image_get() != NULL);
```

with

```c
    CHECK(!g_f.fault);
}

/* The SFR accesses of isr_dsp_reset $B38B from P2 = p2: CLB0, SEB0, CLB0 P2, each a read then a write. */
#define DSP_RESET_LOG(p2) \
    { EV_READ, SFR_P2, (p2) }, { EV_WRITE, SFR_P2, (uint8_t)((p2) & ~1) }, \
    { EV_READ, SFR_P2, (uint8_t)((p2) & ~1) }, { EV_WRITE, SFR_P2, (uint8_t)((p2) | 1) }, \
    { EV_READ, SFR_P2, (uint8_t)((p2) | 1) }, { EV_WRITE, SFR_P2, (uint8_t)((p2) & ~1) }

/* isr_dsp_reset $B38B: P2.0 low, high, low; the other P2 bits kept. P2 bit 7 (the lower-ROM window) is set and stays
   set, so the window cache stays on (M4.6b spec 3.4). A, X and Y untouched. */
static void test_dsp_reset(void)
{
    static const event log[] = { DSP_RESET_LOG(0xF2) };
    setup();
    g_sfr[SFR_P2] = 0xF2;
    g_f.low_window = true;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_dsp_reset(&g_f, &r);
    check_log(log, 6);
    CHECK(g_f.low_window);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0x22);
    CHECK_EQ(r.y, 0x33);
    CHECK(!g_f.fault);
}

/* The SFR accesses and waits of isr_timer_start $B35D ($B35F-$B387) from ICR2 = $3F and P3 = p3. */
#define TIMER_START_LOG(p3) \
    { EV_WRITE, SFR_T2_PRE, 0 }, { EV_WRITE, SFR_T3_PRE, 0 }, \
    { EV_READ, SFR_P3, (p3) }, { EV_WRITE, SFR_P3, (uint8_t)((p3) | 1) }, { EV_WRITE, SFR_T2_RELOAD, 4 }, \
    { EV_READ, SFR_ICR2, 0x3F }, { EV_WRITE, SFR_ICR2, 0x37 }, { EV_READ, SFR_ICR2, 0x37 }, \
    { EV_WRITE, SFR_ICR2, 0x33 }, { EV_WAIT, 0xB372, FW_WAIT_TIMER2 }, { EV_WRITE, SFR_T3_RELOAD, 0x28 }, \
    { EV_READ, SFR_ICR2, 0x33 }, { EV_WRITE, SFR_ICR2, 0x13 }, { EV_READ, SFR_ICR2, 0x13 }, \
    { EV_WRITE, SFR_ICR2, 0x03 }, { EV_WRITE, SFR_P23_FUNC, 1 }, { EV_WAIT, 0xB37F, FW_WAIT_TIMER3 }, \
    { EV_WRITE, SFR_T3_RELOAD, 0x31 }, { EV_WRITE, SFR_ICR2, 0x10 }

/* isr_timer_start $B35D: the whole SFR order around its two waits: the prescalers, isr_tick_div decremented twice
   (10 -> 8), P3.0 set, timer 2's reload 4 and its request and enable bits cleared (CLB3, CLB2), the wait for its
   request at $B372; timer 3's reload $28 and its request and enable bits cleared (CLB5, CLB4), P23_FUNC = 1, the
   wait at $B37F; then the reload $31 and ICR2 = $10. The stub keeps ICR2 as written: its request bits are the
   machine's (the host's loops end when they are set). A, X and Y untouched. A tick count of 1 wraps to $FF. */
static void test_timer_start(void)
{
    static const event log[] = { TIMER_START_LOG(0x80) };
    setup();
    g_sfr[SFR_ICR2] = 0x3F;
    g_sfr[SFR_P3] = 0x80;
    g_ram[ISR_TICK_DIV] = 10;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_timer_start(&g_f, &r);
    check_log(log, sizeof log / sizeof log[0]);
    CHECK_EQ(g_ram[ISR_TICK_DIV], 8);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0x22);
    CHECK_EQ(r.y, 0x33);
    CHECK(!g_f.fault);

    setup();
    g_ram[ISR_TICK_DIV] = 1;
    isr_timer_start(&g_f, &r);
    CHECK_EQ(g_ram[ISR_TICK_DIV], 0xFF);         /* 8-bit DEC */
}

/* isr_speech_stop $B398: P23_FUNC = 0, ICR2 = 0 (LDM: writes only), isr_dsp_reset, then boot_speaking bit 0
   cleared (the other bits kept). A, X and Y untouched. */
static void test_speech_stop(void)
{
    static const event log[] = { { EV_WRITE, SFR_P23_FUNC, 0 }, { EV_WRITE, SFR_ICR2, 0 }, DSP_RESET_LOG(0x71) };
    setup();
    g_sfr[SFR_P23_FUNC] = 1;
    g_sfr[SFR_ICR2] = 0x10;
    g_sfr[SFR_P2] = 0x71;
    g_ram[BOOT_SPEAKING] = 0xFF;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_speech_stop(&g_f, &r);
    check_log(log, sizeof log / sizeof log[0]);
    CHECK_EQ(g_ram[BOOT_SPEAKING], 0xFE);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0x22);
    CHECK_EQ(r.y, 0x33);
    CHECK(!g_f.fault);
}

/* isr_speech_start $B323: frm_source_index = cmd_source_prosody's bits 7-5 and frm_filter_index = its bit 4
   ($B5 = 101 1 0101: 5 and 1; the old values are cleared first); the ISR counters ($59 = $5B = 10, $5A = $5C = 0);
   isr_dsp_reset; isr_timer_start (isr_tick_div 10 -> 8); isr_wait_frames with X = 1: one wait at $B465, the flag
   coming with it, then cleared. A = 0 and X = 0 out, Y untouched. */
static void test_speech_start(void)
{
    static const event log[] = { DSP_RESET_LOG(0x70), TIMER_START_LOG(0x00), { EV_WAIT, 0xB465, FW_WAIT_INTERRUPT } };
    setup();
    g_sfr[SFR_ICR2] = 0x3F;
    g_sfr[SFR_P2] = 0x70;
    g_ram[CMD_SOURCE_PROSODY] = 0xB5;
    g_ram[FRM_SOURCE_INDEX] = 0xEE;
    g_ram[FRM_FILTER_INDEX] = 0xEE;
    g_ram[ISR_WORD_OFS] = 0x12;
    g_ram[ISR_FRAME_FLAG] = 0x81;               /* stale: cleared before the wait */
    g_flag_every = 1;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_speech_start(&g_f, &r);
    check_log(log, sizeof log / sizeof log[0]);
    CHECK_EQ(g_ram[FRM_SOURCE_INDEX], 5);
    CHECK_EQ(g_ram[FRM_FILTER_INDEX], 1);
    CHECK_EQ(g_ram[ISR_TICK_DIV], 8);
    CHECK_EQ(g_ram[ISR_WORDS_LEFT], 10);
    CHECK_EQ(g_ram[ISR_WORD_OFS], 0);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0);
    CHECK_EQ(r.a, 0);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x33);
    CHECK(!g_f.fault);
}

/* isr_wait_frame $B465 with a scripted timer interrupt: the flag already set (bit 0 of $03): no wait, the whole byte
   cleared; not set: a wait at $B465 per interrupt until the third brings it, then cleared (bit 1 alone, $02, does not
   count); a wait that fails: a fault at $B465 and no more waits. A, X and Y untouched. */
static void test_wait_frame(void)
{
    setup();
    g_ram[ISR_FRAME_FLAG] = 0x03;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_wait_frame(&g_f, &r);
    CHECK_EQ(g_nlog, 0);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0);

    static const event log[] = {
        { EV_WAIT, 0xB465, FW_WAIT_INTERRUPT }, { EV_WAIT, 0xB465, FW_WAIT_INTERRUPT },
        { EV_WAIT, 0xB465, FW_WAIT_INTERRUPT },
    };
    setup();
    g_ram[ISR_FRAME_FLAG] = 0x02;
    g_flag_every = 3;
    isr_wait_frame(&g_f, &r);
    check_log(log, 3);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0x22);
    CHECK_EQ(r.y, 0x33);
    CHECK(!g_f.fault);

    setup();
    g_wait_ok = false;
    isr_wait_frame(&g_f, &r);
    CHECK_EQ(g_nlog, 1);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xB465);
}

/* isr_wait_frames $B356 with X = 2: two frames, the DEX / BNE $B356 taken once, a branch no ROM caller takes (its
   one caller, isr_speech_start, passes X = 1), pinned here; X = 0 waits 256 frames (8-bit DEX). X = 0 out, A and Y
   untouched. */
static void test_wait_frames_two(void)
{
    setup();
    g_flag_every = 1;
    fw_regs r = { 0x11, 2, 0x33, 0 };
    isr_wait_frames(&g_f, &r);
    CHECK_EQ(g_iwaits, 2);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x33);

    setup();
    g_flag_every = 1;
    r.x = 0;
    isr_wait_frames(&g_f, &r);
    CHECK_EQ(g_iwaits, 256);
    CHECK_EQ(r.x, 0);
    CHECK(!g_f.fault);
}

void suite_fw_dsp(void)
{
    CHECK(fw_image_get() != NULL);
```

**`tests/test_fw_dsp.c`**: replace

```c
    test_set_volume();
    test_volume_half();
    test_par_handshake();
}
```

with

```c
    test_set_volume();
    test_volume_half();
    test_par_handshake();
    test_dsp_reset();
    test_timer_start();
    test_speech_stop();
    test_speech_start();
    test_wait_frame();
    test_wait_frames_two();
}
```

**`tests/test_fw_par.c`**: replace

```c
#include "firmware/frm_build.h"
#include "firmware/fw_ops.h"
#include "firmware/isr_addr.h"
#include "firmware/par_addr.h"
#include "firmware/par_boundary.h"
#include "firmware/par_play.h"
```

with

```c
#include "firmware/frm_build.h"
#include "firmware/fw_ops.h"
#include "firmware/isr_addr.h"
#include "firmware/isr_dsp.h"
#include "firmware/par_addr.h"
#include "firmware/par_boundary.h"
#include "firmware/par_play.h"
```

**`tests/test_fw_par.c`**: replace

```c
static uint8_t g_p3;             /* the port P3 the C isr_set_volume reads and writes (an SFR: not in g_ram) */
static uint8_t g_p3_log[8];      /* the values written to P3, in order */
static unsigned g_np3;
static unsigned g_waits;         /* FW_WAIT_INTERRUPT waits so far */
static unsigned g_flag_every;    /* the mock timer interrupt sets isr_frame_flag at every n-th of them (0: never) */
static unsigned g_abort_at;      /* the mock receive interrupt sets ser_abort bit 0 at this one (0: never) */
```

with

```c
static uint8_t g_p3;             /* the port P3 the C isr_set_volume reads and writes (an SFR: not in g_ram) */
static uint8_t g_p3_log[8];      /* the values written to P3, in order */
static unsigned g_np3;
static uint8_t g_sfr[0x100];     /* the other SFRs the C speech timing reads and writes (P2, ICR2, the timers) */
static unsigned g_nsfr_writes;   /* SFR writes but P3's */
static unsigned g_waits;         /* FW_WAIT_INTERRUPT waits so far */
static unsigned g_flag_every;    /* the mock timer interrupt sets isr_frame_flag at every n-th of them (0: never) */
static unsigned g_abort_at;      /* the mock receive interrupt sets ser_abort bit 0 at this one (0: never) */
```

**`tests/test_fw_par.c`**: replace

```c
    g_ncalls++;
}

/* Records every ROM call. The interrupt routines that boot_loop_play and par_play still call in ROM (M4.6b Task 4
   makes them C) return the registers the ROM ones leave; any other call fails (par_setup_voice and par_play call
   the C frame builder since M4.5, the C ser_tx_service since M4.6a, the C volume and parallel handshake since
   M4.6b). */
static fw_call_result mock_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    (void)ctx;
    record(addr, r, budget, false);
    switch (addr) {
    case ROM_ISR_SPEECH_STOP:        /* $B398-$B3A3: LDM, LDM, isr_dsp_reset (PHP ... PLP), CLB0: A, X, Y kept */
        return FW_CALL_RETURNED;
    case ROM_ISR_WAIT_FRAME:         /* $B465-$B46B: BBC0 isr_frame_flag (the frame is due), LDM #0: A, X, Y kept */
        g_ram[ISR_FRAME_FLAG] = 0;
        return FW_CALL_RETURNED;
    case ROM_ISR_SPEECH_START:       /* $B323-$B355: LDA #0 at $B344 (isr_dsp_reset, isr_timer_start and
                                        isr_wait_frames keep A), LDX #1 / isr_wait_frames' DEX: A = X = 0, Y kept; its
                                        isr_wait_frame clears the flag */
        r->a = 0;
        r->x = 0;
        g_ram[ISR_FRAME_FLAG] = 0;
        return FW_CALL_RETURNED;
    default:
        return FW_CALL_FAILED;
    }
}

/* Records every wait. Each FW_WAIT_INTERRUPT is an interrupt handled: the timer interrupt swaps the buffers and sets
```

with

```c
    g_ncalls++;
}

/* Records every ROM call and fails it: par_setup_voice and par_play call the C frame builder since M4.5, the C
   ser_tx_service since M4.6a, and boot_loop_play and par_play the C speech timing since M4.6b. Only an event
   handler that is none of the five (test_run_event_rom) makes one. */
static fw_call_result mock_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    (void)ctx;
    record(addr, r, budget, false);
    return FW_CALL_FAILED;
}

/* Records every wait. Each FW_WAIT_INTERRUPT is an interrupt handled: the timer interrupt swaps the buffers and sets
```

**`tests/test_fw_par.c`**: replace

```c
    return true;
}

/* UART_STATUS ($E7) as g_uart_status says, P3 ($EE) as g_p3; any other SFR reads 0: P0 ($F6, counted) with the host
   line P0.2 low, as in the machine, so the C ser_tx_service returns at its BBC2 P0 ($C585) and sends nothing. */
static uint8_t mock_read_sfr(void *ctx, uint8_t addr)
{
    (void)ctx;
    g_p0_reads += addr == 0xF6;
    return addr == 0xE7 ? g_uart_status : addr == 0xEE ? g_p3 : 0;
}

/* P3 ($EE) writes go to g_p3 and its log; any other SFR write fails the test (nothing else is written here). */
static void mock_write_sfr(void *ctx, uint8_t addr, uint8_t value)
{
    (void)ctx;
    CHECK_EQ(addr, 0xEE);
    g_p3 = value;
    if (g_np3 < sizeof g_p3_log)
        g_p3_log[g_np3] = value;
```

with

```c
    return true;
}

/* UART_STATUS ($E7) as g_uart_status says, P3 ($EE) as g_p3, P0 ($F6, counted) 0: the host line P0.2 low, as in the
   machine, so the C ser_tx_service returns at its BBC2 P0 ($C585) and sends nothing; any other SFR as last
   written (g_sfr, 0 at first). */
static uint8_t mock_read_sfr(void *ctx, uint8_t addr)
{
    (void)ctx;
    g_p0_reads += addr == 0xF6;
    switch (addr) {
    case 0xE7: return g_uart_status;
    case 0xEE: return g_p3;
    case 0xF6: return 0;
    default: return g_sfr[addr];
    }
}

/* P3 ($EE) writes go to g_p3 and its log; the others to g_sfr, counted. */
static void mock_write_sfr(void *ctx, uint8_t addr, uint8_t value)
{
    (void)ctx;
    if (addr != 0xEE) {
        g_sfr[addr] = value;
        g_nsfr_writes++;
        return;
    }
    g_p3 = value;
    if (g_np3 < sizeof g_p3_log)
        g_p3_log[g_np3] = value;
```

**`tests/test_fw_par.c`**: replace

```c
    g_host.wait = mock_wait;
    g_ncalls = 0;
    g_np3 = 0;
    g_waits = 0;
    g_flag_every = 0;
    g_abort_at = 0;
```

with

```c
    g_host.wait = mock_wait;
    g_ncalls = 0;
    g_np3 = 0;
    memset(g_sfr, 0, sizeof g_sfr);
    g_nsfr_writes = 0;
    g_waits = 0;
    g_flag_every = 0;
    g_abort_at = 0;
```

**`tests/test_fw_par.c`**: replace

```c
    size_t waits = 0;
    for (size_t i = 0; i < fw_replacement_count; i++)
        if (fw_replacements[i].waits) {
            waits++;
            CHECK(fw_replacements[i].addr == ROM_BOOT_LOOP_PLAY || fw_replacements[i].addr == ROM_PAR_PLAY);
        }
    CHECK_EQ(waits, 2);   /* only the two loops wait on the timer interrupt: diff_play checks them, diff_lts not */
}

/* Record i is a call of addr with A, X and Y (the strict budget), made with par_frames_left = frames. */
```

with

```c
    size_t waits = 0;
    for (size_t i = 0; i < fw_replacement_count; i++)
        if (fw_replacements[i].waits) {
            uint16_t a = fw_replacements[i].addr;
            waits++;
            CHECK(a == ROM_BOOT_LOOP_PLAY || a == ROM_PAR_PLAY || a == ROM_ISR_SPEECH_START ||
                  a == ROM_ISR_WAIT_FRAMES || a == ROM_ISR_WAIT_FRAME);
        }
    CHECK_EQ(waits, 5);   /* the two loops and the three frame waits wait on the timer interrupt: diff_play checks
                             them, diff_lts not (M4.6b spec 2.4) */
}

/* Record i is a call of addr with A, X and Y (the strict budget), made with par_frames_left = frames. */
```

**`tests/test_fw_par.c`**: replace

```c
    CHECK_EQ(g_calls[i].frames, frames);
}

/* The end ($B8D2-$B8FA) from record i on, with `waits` waits for an interrupt per rest frame: isr_wait_frame, then
   per rest frame (frm_copy_rest_frame, frm_snapshot, frm_clamp_mark_space and frm_transition_flags, all C) the
   waits at $B8E6, with par_frames_left 3, 2, 1 ($B8D5, then the DEC at $B8EF); then cmd_ev_sound_end (C, returning
   at once, cmd_sound_active being 0) and the last isr_par_ready ($B8F7, C). Returns the number of records. */
static unsigned check_rest_frames(unsigned i, unsigned waits)
{
    CHECK(i < g_ncalls && i < MAX_CALLS);
    if (i >= g_ncalls || i >= MAX_CALLS)
        return 0;
    CHECK(!g_calls[i].wait);
    CHECK_EQ(g_calls[i].addr, ROM_ISR_WAIT_FRAME);
    for (unsigned j = 0; j < 3; j++)
        for (unsigned m = 0; m < waits; m++)
            check_wait(i + 1 + waits * j + m, PAR_WAIT_REST, (uint8_t)(3 - j));
    return 1 + 3 * waits;
}

/* No segment left (pros_seg_count = 0): two passes, each ending in a wait for an interrupt at the back edge (the flag
```

with

```c
    CHECK_EQ(g_calls[i].frames, frames);
}

/* The end ($B8D2-$B8FA) from record i on, with `waits` waits for an interrupt per rest frame: isr_wait_frame (C,
   M4.6b; the flag is set when the frame loop ends, so it clears it without a wait), then per rest frame
   (frm_copy_rest_frame, frm_snapshot, frm_clamp_mark_space and frm_transition_flags, all C) the waits at $B8E6,
   with par_frames_left 3, 2, 1 ($B8D5, then the DEC at $B8EF); then cmd_ev_sound_end (C, returning at once,
   cmd_sound_active being 0) and the last isr_par_ready ($B8F7, C). Returns the number of records. */
static unsigned check_rest_frames(unsigned i, unsigned waits)
{
    for (unsigned j = 0; j < 3; j++)
        for (unsigned m = 0; m < waits; m++)
            check_wait(i + waits * j + m, PAR_WAIT_REST, (uint8_t)(3 - j));
    return 3 * waits;
}

/* No segment left (pros_seg_count = 0): two passes, each ending in a wait for an interrupt at the back edge (the flag
```

**`tests/test_fw_par.c`**: replace

```c
    check_wait(3, PAR_WAIT_LAST, 2);
    check_wait(4, PAR_WAIT_LAST, 1);                                 /* frame 2 */
    check_wait(5, PAR_WAIT_LAST, 1);
    CHECK_EQ(check_rest_frames(6, 2), 7);                            /* the flag every 2nd wait */
    check_play_call(6, ROM_ISR_WAIT_FRAME, 0, 0, FRAME_PITCH, 0);    /* par_step_tracks' A, X, Y */
    CHECK_EQ(g_ncalls, 13);
    CHECK_EQ(g_waits, 12);
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 0);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0);                                /* cleared after the last rest frame */
```

with

```c
    check_wait(3, PAR_WAIT_LAST, 2);
    check_wait(4, PAR_WAIT_LAST, 1);                                 /* frame 2 */
    check_wait(5, PAR_WAIT_LAST, 1);
    CHECK_EQ(check_rest_frames(6, 2), 6);                            /* the flag every 2nd wait */
    CHECK_EQ(g_ncalls, 12);
    CHECK_EQ(g_waits, 12);
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 0);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0);                                /* cleared after the last rest frame */
```

**`tests/test_fw_par.c`**: replace

```c
    check_wait(4, PAR_WAIT_LAST, 1);                             /* the last frame */
    check_wait(5, PAR_WAIT_LAST, 1);
    CHECK_EQ(g_p0_reads, 6);                                     /* ser_tx_service at each of the six passes */
    CHECK_EQ(check_rest_frames(6, 2), 7);
    check_play_call(6, ROM_ISR_WAIT_FRAME, 0x1C, 0, FRAME_PITCH, 0);   /* the pitch $55 to $70 over 2 frames, then
                                                                    $70: b10 = $1C, A of par_step_tracks, kept by
                                                                    frm_finish_frame */
    CHECK_EQ(g_ncalls, 13);
    CHECK_EQ(g_ram[CMD_VOICING], 6);
    CHECK_EQ(g_ram[CMD_EV_NEXT], 1);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 4);
```

with

```c
    check_wait(4, PAR_WAIT_LAST, 1);                             /* the last frame */
    check_wait(5, PAR_WAIT_LAST, 1);
    CHECK_EQ(g_p0_reads, 6);                                     /* ser_tx_service at each of the six passes */
    CHECK_EQ(check_rest_frames(6, 2), 6);
    CHECK_EQ(g_ncalls, 12);
    CHECK_EQ(g_ram[CMD_VOICING], 6);
    CHECK_EQ(g_ram[CMD_EV_NEXT], 1);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 4);
```

**`tests/test_fw_par.c`**: replace

```c
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    check_wait(0, PAR_WAIT_TOP, 4);
    CHECK_EQ(check_rest_frames(1, 1), 4);
    check_play_call(1, ROM_ISR_WAIT_FRAME, 0x11, 0, 0x33, 4);   /* the entry A and Y, X = cmd_ev_next ($B877) */
    CHECK_EQ(g_ncalls, 5);
    CHECK_EQ(g_p0_reads, 0);                                     /* no ser_tx_service: it would read P0 */
    for (int b = 0; b < FRAME_SIZE; b++)
        CHECK_EQ(g_ram[ISR_FRAME_B + b], b == FRAME_MARK_SPACE ? 47 : 0);
```

with

```c
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    check_wait(0, PAR_WAIT_TOP, 4);
    CHECK_EQ(check_rest_frames(1, 1), 3);
    CHECK_EQ(g_ncalls, 4);
    CHECK_EQ(g_p0_reads, 0);                                     /* no ser_tx_service: it would read P0 */
    for (int b = 0; b < FRAME_SIZE; b++)
        CHECK_EQ(g_ram[ISR_FRAME_B + b], b == FRAME_MARK_SPACE ? 47 : 0);
```

**`tests/test_fw_par.c`**: replace

```c
    par_play(&g_f, &r);
    check_wait(0, PAR_WAIT_TOP, 0);
    check_wait(1, PAR_WAIT_TOP, 0xFF);                         /* after the frame built at $B908 */
    CHECK_EQ(check_rest_frames(2, 1), 4);
    CHECK_EQ(g_ncalls, 6);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 1);   /* par_next_segment was not called */
    CHECK(!g_f.fault);
}
```

with

```c
    par_play(&g_f, &r);
    check_wait(0, PAR_WAIT_TOP, 0);
    check_wait(1, PAR_WAIT_TOP, 0xFF);                         /* after the frame built at $B908 */
    CHECK_EQ(check_rest_frames(2, 1), 3);
    CHECK_EQ(g_ncalls, 5);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 1);   /* par_next_segment was not called */
    CHECK(!g_f.fault);
}
```

**`tests/test_fw_par.c`**: replace

```c
    par_play(&g_f, &r);
    check_wait(0, PAR_WAIT_TOP, 1);
    check_wait(1, PAR_WAIT_TOP, 0xFF);                       /* after the frame built at $B908 */
    CHECK_EQ(check_rest_frames(2, 1), 4);
    CHECK_EQ(g_ncalls, 6);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 1);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 4);
    CHECK(!g_f.fault);
```

with

```c
    par_play(&g_f, &r);
    check_wait(0, PAR_WAIT_TOP, 1);
    check_wait(1, PAR_WAIT_TOP, 0xFF);                       /* after the frame built at $B908 */
    CHECK_EQ(check_rest_frames(2, 1), 3);
    CHECK_EQ(g_ncalls, 5);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 1);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 4);
    CHECK(!g_f.fault);
```

**`tests/test_fw_par.c`**: replace

```c
    check_wait(1, PAR_WAIT_LAST, 0xFF);                                 /* frame $FF's wait */
    check_wait(255, PAR_WAIT_LAST, 1);                                  /* frame 1's */
    CHECK_EQ(count(PAR_WAIT_LAST), 255);
    CHECK_EQ(check_rest_frames(256, 1), 4);
    CHECK_EQ(g_ncalls, 260);
    CHECK(!g_f.fault);
}
```

with

```c
    check_wait(1, PAR_WAIT_LAST, 0xFF);                                 /* frame $FF's wait */
    check_wait(255, PAR_WAIT_LAST, 1);                                  /* frame 1's */
    CHECK_EQ(count(PAR_WAIT_LAST), 255);
    CHECK_EQ(check_rest_frames(256, 1), 3);
    CHECK_EQ(g_ncalls, 259);
    CHECK(!g_f.fault);
}
```

**`tests/test_fw_par.c`**: replace

```c
    }
}

/* boot_loop_play that plays (FW_UTT_CONTENT $FF): isr_par_hold (C: nothing in serial mode); par_play_start (C,
   test_play_start's voice A set-up: Y = the pause phoneme $3B, A = 0); isr_speech_start (ROM: A = X = 0 out);
   isr_volume_half (C); isr_wait_frame (ROM); isr_set_volume (C) with A = cmd_volume ($B2C2); par_play (C), here
   ended by a mute at its first wait; isr_volume_half; isr_wait_frame; isr_set_volume with A = 0 ($B2D0);
   isr_speech_stop (ROM), whose registers it returns. cmd_volume 9: the half is 4, P3 = (P3 & $87) | (4 ^ $F) << 3.
   The rest frames go to isr_frame_b (isr_fill_ptr as par_setup_voice leaves it); par_play returns the last one's
   frm_flags, 0. */
static void test_loop_play(void)
{
    setup();
```

with

```c
    }
}

/* boot_loop_play that plays (FW_UTT_CONTENT $FF), all C (M4.6b): isr_par_hold (nothing in serial mode);
   par_play_start (test_play_start's voice A set-up: Y = the pause phoneme $3B, A = 0, par_frames_left 1);
   isr_speech_start (the DSP reset, the timer start with its two waits, P3.0 set, then a frame waited for at $B465,
   whose interrupt also brings the mute); isr_volume_half; isr_wait_frame; isr_set_volume with A = cmd_volume
   ($B2C2); par_play, which sees the mute at once: its end waits a frame at $B465, then one per rest frame;
   isr_volume_half; isr_wait_frame; isr_set_volume with A = 0 ($B2D0); isr_speech_stop, whose registers it returns.
   cmd_volume 9: the half is 4, P3 = (P3 & $87) | (4 ^ $F) << 3. The rest frames go to isr_frame_b (isr_fill_ptr as
   par_setup_voice leaves it); par_play returns the last one's frm_flags, 0. */
static void test_loop_play(void)
{
    setup();
```

**`tests/test_fw_par.c`**: replace

```c
    g_abort_at = 1;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    boot_loop_play(&g_f, &r);
    check_call(0, ROM_ISR_SPEECH_START, 0, 0x22, 0x3B);          /* par_play_start's A and Y */
    check_call(1, ROM_ISR_WAIT_FRAME, 0xDF, 0, 0x3B);            /* P3: $87 | (4 ^ $F) << 3 = $87 | $58 */
    check_wait(2, PAR_WAIT_TOP, 1);                              /* par_play (par_play_start's 1 frame): the mute */
    CHECK_EQ(check_rest_frames(3, 1), 4);
    check_call(3, ROM_ISR_WAIT_FRAME, 0xB7, 0, 0x3B);            /* P3 $87 | (9 ^ $F) << 3 = $87 | $30; X =
                                                                    cmd_ev_next 0 */
    check_call(7, ROM_ISR_WAIT_FRAME, 0xDF, 0, 0x0D);            /* par_play's X and Y */
    check_call(8, ROM_ISR_SPEECH_STOP, 0xFF, 0, 0x0D);           /* P3 $87 | $F << 3 */
    CHECK_EQ(g_ncalls, 9);
    static const uint8_t p3[] = { 0xDF, 0xB7, 0xDF, 0xFF };      /* half, cmd_volume, half, 0 */
    CHECK_EQ(g_np3, 4);
    for (unsigned i = 0; i < 4; i++)
        CHECK_EQ(g_p3_log[i], p3[i]);
    CHECK_EQ(g_p3, 0xFF);
    CHECK_EQ(g_ram[PAR_CUR_PHONEME], 0x3B);                      /* par_play_start ran */
    CHECK_EQ(g_ram[PAR_CUR_RANK], 31);
    CHECK_EQ(r.a, 0xFF);
```

with

```c
    g_abort_at = 1;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    boot_loop_play(&g_f, &r);
    check_wait(0, FW_WAIT_TIMER2_LOOP, 1);                       /* isr_timer_start */
    check_wait(1, FW_WAIT_TIMER3_LOOP, 1);
    check_wait(2, ROM_ISR_WAIT_FRAME, 1);                        /* isr_wait_frames: the mute comes */
    check_wait(3, ROM_ISR_WAIT_FRAME, 1);                        /* after the half volume */
    check_wait(4, ROM_ISR_WAIT_FRAME, 1);                        /* par_play's end ($B8D2) */
    CHECK_EQ(check_rest_frames(5, 1), 3);
    check_wait(8, ROM_ISR_WAIT_FRAME, 0);                        /* after the half volume */
    CHECK_EQ(g_ncalls, 9);
    static const uint8_t p3[] = { 0xFF, 0xDF, 0xB7, 0xDF, 0xFF };   /* SEB0 P3, half, cmd_volume, half, 0 */
    CHECK_EQ(g_np3, 5);
    for (unsigned i = 0; i < 5; i++)
        CHECK_EQ(g_p3_log[i], p3[i]);
    CHECK_EQ(g_p3, 0xFF);                                        /* P3 $87 | $F << 3 */
    CHECK_EQ(g_nsfr_writes, 19);                                 /* isr_speech_start 14, isr_speech_stop 5 */
    CHECK_EQ(g_sfr[SFR_P23_FUNC], 0);                            /* isr_speech_stop */
    CHECK_EQ(g_sfr[SFR_ICR2], 0);
    CHECK_EQ(g_sfr[SFR_T3_RELOAD], 0x31);                        /* isr_timer_start */
    CHECK_EQ(g_ram[ISR_TICK_DIV], 8);                            /* 10, then decremented twice */
    CHECK_EQ(g_ram[PAR_CUR_PHONEME], 0x3B);                      /* par_play_start ran */
    CHECK_EQ(g_ram[PAR_CUR_RANK], 31);
    CHECK_EQ(r.a, 0xFF);
```

**`tests/test_fw_tn.c`**: replace

```c
static bool g_window;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;
static uint8_t g_p0, g_p3, g_icr1;   /* the SFRs the C isr_par_ready, isr_par_hold and isr_set_volume touch */

static void record(fw *f, uint16_t addr, const fw_regs *r, uint64_t budget)
{
```

with

```c
static bool g_window;
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;
static uint8_t g_sfr[0x100];         /* the SFRs the C speech timing reads and writes, as last written */

static void record(fw *f, uint16_t addr, const fw_regs *r, uint64_t budget)
{
```

**`tests/test_fw_tn.c`**: replace

```c
}

/* Records every call with tn_phon_len at that moment. cmd_run_next ($A893) plays the ROM's bookkeeping
   (cmd_next += 1, cmd_next_pos = cmd_positions[cmd_next]); the other helpers leave the registers unchanged.
   isr_speech_start sets ser_abort bit 0 (a mute as the DSP starts), so the C boot_loop_play of the mid-clause flush
   plays no segment: par_play goes from its first pass to the three rest frames. */
static fw_call_result mock_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    fw *f = ctx;
```

with

```c
}

/* Records every call with tn_phon_len at that moment. cmd_run_next ($A893) plays the ROM's bookkeeping
   (cmd_next += 1, cmd_next_pos = cmd_positions[cmd_next]); any other call fails (speech timing is C since
   M4.6b). */
static fw_call_result mock_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    fw *f = ctx;
```

**`tests/test_fw_tn.c`**: replace

```c
        fw_wr8(f, CMD_NEXT_POS, fw_rd8(f, (uint16_t)(CMD_POSITIONS + n)));
        return FW_CALL_RETURNED;
    }
    switch (addr) {
    case ROM_ISR_SPEECH_START:
        fw_wr8(f, SER_ABORT, 1);
        return FW_CALL_RETURNED;
    case ROM_ISR_WAIT_FRAME:
    case ROM_ISR_SPEECH_STOP:
        return FW_CALL_RETURNED;
    default:
        return FW_CALL_FAILED;
    }
}

/* Records every wait (addr = its ROM loop). Each is an interrupt handled: the timer interrupt sets isr_frame_flag
   bit 0 (a frame is due at every wait of the C par_play). */
static bool mock_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    fw *f = ctx;
    record(f, at, NULL, 0);
    if (kind == FW_WAIT_INTERRUPT)
        fw_wr8(f, ISR_FRAME_FLAG, 1);
    return true;
}

/* UART_STATUS: the transmitter ready (bit 1), nothing received; P0, P3 and ICR1 as last written. */
static uint8_t mock_read_sfr(void *ctx, uint8_t addr)
{
    (void)ctx;
    switch (addr) {
    case 0xE7: return 0x02;
    case 0xF6: return g_p0;
    case 0xEE: return g_p3;
    case 0xFF: return g_icr1;
    default: return 0x00;
    }
}

/* P0 ($F6): isr_par_ready sets bit 3 (recorded as a call of ROM_ISR_PAR_READY), isr_par_hold clears it (recorded as
   ROM_ISR_PAR_HOLD), neither with registers (the C passes them to nothing); P3 and ICR1 are kept. */
static void mock_write_sfr(void *ctx, uint8_t addr, uint8_t value)
{
    fw *f = ctx;
    switch (addr) {
    case 0xF6:
        g_p0 = value;
        record(f, (value & 0x08) ? ROM_ISR_PAR_READY : ROM_ISR_PAR_HOLD, NULL, 0);
        break;
    case 0xEE: g_p3 = value; break;
    case 0xFF: g_icr1 = value; break;
    default: CHECK(false); break;
    }
}

static void mock_window(void *ctx, bool on)
```

with

```c
        fw_wr8(f, CMD_NEXT_POS, fw_rd8(f, (uint16_t)(CMD_POSITIONS + n)));
        return FW_CALL_RETURNED;
    }
    return FW_CALL_FAILED;
}

/* Records every wait (addr = its ROM loop). Each wait for an interrupt is one handled: the timer interrupt sets
   isr_frame_flag bit 0 (a frame is due at every wait). isr_timer_start's first wait sets ser_abort bit 0 (a mute as
   the DSP starts), so the C boot_loop_play of the mid-clause flush plays no segment: par_play goes from its first
   pass to the three rest frames. */
static bool mock_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    fw *f = ctx;
    record(f, at, NULL, 0);
    if (kind == FW_WAIT_INTERRUPT)
        fw_wr8(f, ISR_FRAME_FLAG, 1);
    if (kind == FW_WAIT_TIMER2)
        fw_wr8(f, SER_ABORT, 1);
    return true;
}

/* UART_STATUS: the transmitter ready (bit 1), nothing received; any other SFR as last written. */
static uint8_t mock_read_sfr(void *ctx, uint8_t addr)
{
    (void)ctx;
    return addr == 0xE7 ? 0x02 : g_sfr[addr];
}

/* Every SFR write is kept; a write of P0 ($F6) is also recorded: isr_par_ready sets bit 3 (a call of
   ROM_ISR_PAR_READY), isr_par_hold clears it (ROM_ISR_PAR_HOLD), neither with registers (the C passes them to
   nothing). */
static void mock_write_sfr(void *ctx, uint8_t addr, uint8_t value)
{
    fw *f = ctx;
    g_sfr[addr] = value;
    if (addr == 0xF6)
        record(f, (value & 0x08) ? ROM_ISR_PAR_READY : ROM_ISR_PAR_HOLD, NULL, 0);
}

static void mock_window(void *ctx, bool on)
```

**`tests/test_fw_tn.c`**: replace

```c
    g_host.wait = mock_wait;
    g_window = false;
    g_ncalls = 0;
    g_p0 = g_p3 = g_icr1 = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
    g_ram[BOOT_DIP] = 1;                        /* parallel mode: isr_par_ready and isr_par_hold are recorded */
    memcpy(g_ram + CMD_TEXT, text, n);
```

with

```c
    g_host.wait = mock_wait;
    g_window = false;
    g_ncalls = 0;
    memset(g_sfr, 0, sizeof g_sfr);
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
    g_ram[BOOT_DIP] = 1;                        /* parallel mode: isr_par_ready and isr_par_hold are recorded */
    memcpy(g_ram + CMD_TEXT, text, n);
```

**`tests/test_fw_tn.c`**: replace

```c
}

/* The mid-clause flush ($B1FF): the C pros_make_segments (M4.3b) turns the 250 symbols into segments, with its
   isr_par_ready calls, and the C boot_loop_play (M4.4b) gets the registers it leaves, as the ROM's would: it plays
   (FW_UTT_CONTENT is $FF), so isr_par_hold and par_play_start pass X on to its first ROM call, isr_speech_start;
   the C par_play_start sets up the first segment; the mock's mute at isr_speech_start ends the C par_play at its
   first pass. Then the conversion goes on from tn_resume_pos with an empty buffer. */
static void test_flush(void)
{
    char text[255];
```

with

```c
}

/* The mid-clause flush ($B1FF): the C pros_make_segments (M4.3b) turns the 250 symbols into segments, with its
   isr_par_ready calls, and the C boot_loop_play (M4.4b) plays them (FW_UTT_CONTENT is $FF): the C par_play_start
   sets up the first segment; the mock's mute at the timer start's first wait ends the C par_play at its first
   pass. Then the conversion goes on from tn_resume_pos with an empty buffer. */
static void test_flush(void)
{
    char text[255];
```

**`tests/test_fw_tn.c`**: replace

```c
       segment but the two end pauses), par_play 8 (its pass, two per rest frame around its wait, its last call) */
    CHECK_EQ(count(ROM_ISR_PAR_READY), 1382);
    CHECK(g_ncalls <= MAX_CALLS);
    /* boot_loop_play's ROM calls and waits (isr_set_volume and isr_volume_half are C and write P3 only), then
       isr_par_ready at text[250..254] */
    static const uint16_t k_play[] = {
        ROM_ISR_PAR_HOLD, ROM_ISR_SPEECH_START, ROM_ISR_WAIT_FRAME,
        ROM_ISR_PAR_READY, ROM_ISR_WAIT_FRAME,                                     /* par_play: the pass, the end */
        ROM_ISR_PAR_READY, 0xB8E6, ROM_ISR_PAR_READY,                              /* the rest frames (C) */
        ROM_ISR_PAR_READY, 0xB8E6, ROM_ISR_PAR_READY,
        ROM_ISR_PAR_READY, 0xB8E6, ROM_ISR_PAR_READY,
        ROM_ISR_PAR_READY,                                                         /* $B8F7 */
        ROM_ISR_WAIT_FRAME, ROM_ISR_SPEECH_STOP,
        ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,
    };
    unsigned n = sizeof k_play / sizeof k_play[0];
```

with

```c
       segment but the two end pauses), par_play 8 (its pass, two per rest frame around its wait, its last call) */
    CHECK_EQ(count(ROM_ISR_PAR_READY), 1382);
    CHECK(g_ncalls <= MAX_CALLS);
    /* boot_loop_play, all C (M4.6b): isr_par_hold, the waits of isr_speech_start (the timer start's two, a frame),
       the frame after the half volume, par_play's pass and end, the frame after the half volume again; then
       isr_par_ready at text[250..254] */
    static const uint16_t k_play[] = {
        ROM_ISR_PAR_HOLD, FW_WAIT_TIMER2_LOOP, FW_WAIT_TIMER3_LOOP, ROM_ISR_WAIT_FRAME, ROM_ISR_WAIT_FRAME,
        ROM_ISR_PAR_READY, ROM_ISR_WAIT_FRAME,                                     /* par_play: the pass, the end */
        ROM_ISR_PAR_READY, 0xB8E6, ROM_ISR_PAR_READY,                              /* the rest frames */
        ROM_ISR_PAR_READY, 0xB8E6, ROM_ISR_PAR_READY,
        ROM_ISR_PAR_READY, 0xB8E6, ROM_ISR_PAR_READY,
        ROM_ISR_PAR_READY,                                                         /* $B8F7 */
        ROM_ISR_WAIT_FRAME,
        ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,
    };
    unsigned n = sizeof k_play / sizeof k_play[0];
```

**`tests/test_fw_tn.c`**: replace

```c
    for (unsigned i = 0; i < n; i++)
        CHECK_EQ(g_calls[first + i].addr, k_play[i]);
    CHECK_EQ(g_calls[first].phon_len, TN_PHONETIC_FLUSH);
    CHECK_EQ(g_calls[first + 1].a, 0);                    /* isr_speech_start: par_play_start's A = 0 ($BB2C) and */
    CHECK_EQ(g_calls[first + 1].x, TN_PHONETIC_FLUSH);    /* X = pros_pos (tn_phon_len), as pros_phrase_gen leaves
                                                             it, through isr_par_hold and par_play_start */
    CHECK_EQ(g_p3, 0x78);                                 /* isr_set_volume(0) ($B2D0): $F << 3 */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0xFFFF);          /* par_play_start: no event queued */
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 0);                  /* the three rest frames counted down */
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0);                   /* cleared after the last rest frame ($B8EC) */
```

with

```c
    for (unsigned i = 0; i < n; i++)
        CHECK_EQ(g_calls[first + i].addr, k_play[i]);
    CHECK_EQ(g_calls[first].phon_len, TN_PHONETIC_FLUSH);
    CHECK_EQ(g_sfr[0xEE], 0x79);                          /* P3: isr_set_volume(0) ($B2D0): $F << 3, and bit 0 set
                                                             by isr_timer_start */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0xFFFF);          /* par_play_start: no event queued */
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 0);                  /* the three rest frames counted down */
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0);                   /* cleared after the last rest frame ($B8EC) */
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw_dsp"`
Expected: the build stops with compile errors (the proof run, first lines):

```
tests\test_fw_dsp.c(380): error C2220: the following warning is treated as an error
tests\test_fw_dsp.c(380): warning C4013: 'isr_dsp_reset' undefined; assuming extern returning int
tests\test_fw_dsp.c(412): warning C4013: 'isr_timer_start' undefined; assuming extern returning int
tests\test_fw_dsp.c(437): warning C4013: 'isr_speech_stop' undefined; assuming extern returning int
tests\test_fw_dsp.c(463): warning C4013: 'isr_speech_start' undefined; assuming extern returning int
tests\test_fw_dsp.c(485): warning C4013: 'isr_wait_frame' undefined; assuming extern returning int
tests\test_fw_dsp.c(520): warning C4013: 'isr_wait_frames' undefined; assuming extern returning int
```

- [ ] **Step 3: Implement**

**`src/engine/thunks.c`**: replace

```c
    tn_convert_clause(f);
}

/* par_setup_voice $B3A4: A, X, Y in; none is read (Y reaches frm_build_rest_frame, whose first instruction is
   LDY #$14 at $B41D). A = $0B, X = 9 and Y = $0B out, as the frame builder leaves them: frm_build_rest_frame's loop ends
   at X = 9 ($B436 CPX #8), frm_clamp_mark_space ends with LDA #$0B and Y = $0A + 1 ($B930-$B954); the C calls the C
```

with

```c
    tn_convert_clause(f);
}

/* isr_speech_start $B323: no register input (X and A are written first: $B323 LDX #0, $B32B LDA). A = 0 ($B344 LDA
   #0) and X = 0 (isr_wait_frames' DEX) out, Y untouched (isr_dsp_reset, isr_timer_start and isr_wait_frames keep A
   and Y). Its one caller, boot_play_body ($B2B9), calls isr_volume_half next, which loads A first ($B46C LDA
   cmd_volume) and hands X and Y on to isr_wait_frame, isr_set_volume and par_play (their rows). No caller reads a
   flag. It waits on the timer interrupt (isr_wait_frames), so diff_play checks it, not diff_lts. The C
   boot_loop_play calls the C, so it fires only while the ROM boot_loop_play runs. Declared: A, X, Y. */
static void isr_speech_start_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_speech_start(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* isr_wait_frames $B356: X in (the count). X = 0 out, A and Y untouched. Its one caller, isr_speech_start ($B352),
   returns ($B355 RTS), so X and A (0, $B344) and Y are its exit registers (its row). No caller reads a flag. It waits
   on the timer interrupt, so diff_play checks it, not diff_lts. The C isr_speech_start calls the C, so it fires only
   while the ROM isr_speech_start runs. Declared: A, X, Y. */
static void isr_wait_frames_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_wait_frames(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* isr_timer_start $B35D: A, X and Y in and out untouched (LDM, DEC, SEB, CLB and the two BBC loops; PHP / PLP keep
   the flags). Its one caller, isr_speech_start ($B34D), loads X next ($B350 LDX #1) and returns A and Y as its exit
   registers (its row). No caller reads a flag. The C isr_speech_start calls the C, so it fires only while the ROM
   isr_speech_start runs. Declared: A, X, Y. */
static void isr_timer_start_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_timer_start(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* isr_dsp_reset $B38B: A, X and Y in and out untouched (CLB, SEB, CLB; PHP / PLP keep the flags). isr_speech_start
   ($B34A) calls isr_timer_start next, which keeps them (its row); isr_speech_stop ($B39E) clears boot_speaking bit 0
   and returns them as its exit registers (its row). No caller reads a flag. The C isr_speech_start and
   isr_speech_stop call the C, so it fires only while one of them runs as ROM. Declared: A, X, Y. */
static void isr_dsp_reset_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_dsp_reset(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* isr_speech_stop $B398: A, X and Y in and out untouched (LDM, LDM, isr_dsp_reset, CLB). Its one caller,
   boot_play_body ($B2D5), returns ($B2D8 RTS), so they are boot_loop_play's exit registers (its thunk), which no
   caller reads. No caller reads a flag. The C boot_loop_play calls the C, so it fires only while the ROM
   boot_loop_play runs. Declared: A, X, Y. */
static void isr_speech_stop_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_speech_stop(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_setup_voice $B3A4: A, X, Y in; none is read (Y reaches frm_build_rest_frame, whose first instruction is
   LDY #$14 at $B41D). A = $0B, X = 9 and Y = $0B out, as the frame builder leaves them: frm_build_rest_frame's loop ends
   at X = 9 ($B436 CPX #8), frm_clamp_mark_space ends with LDA #$0B and Y = $0A + 1 ($B930-$B954); the C calls the C
```

**`src/engine/thunks.c`**: replace

```c
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_copy_rest_frame(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
```

with

```c
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_copy_rest_frame(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* isr_wait_frame $B465: A, X and Y in and out untouched (BBC0, LDM). boot_play_body loads A next ($B2C2 LDA cmd_volume,
   $B2D0 LDA #0) and hands X and Y on to isr_set_volume and par_play or isr_speech_stop (their rows); isr_wait_frames
   ($B359 DEX) reads X; par_play ($B8D2) loads A next ($B8D5 LDA #3) and hands X and Y to frm_copy_rest_frame, which
   loads Y first and keeps X (its row). No caller reads a flag. It waits on the timer interrupt, so diff_play checks
   it, not diff_lts. The C boot_loop_play, isr_wait_frames and par_play call the C, so it fires only while one of
   them runs as ROM. Declared: A, X, Y. */
static void isr_wait_frame_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_wait_frame(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
```

**`src/engine/thunks.c`**: replace

```c
THUNK(tn_append_phonetic)
THUNK(lts_convert_char)
THUNK(tn_convert_clause)
THUNK(par_setup_voice)
THUNK(frm_build_rest_frame)
THUNK(frm_copy_rest_frame)
THUNK(isr_volume_half)
THUNK(par_play)
THUNK(frm_snapshot)
```

with

```c
THUNK(tn_append_phonetic)
THUNK(lts_convert_char)
THUNK(tn_convert_clause)
THUNK(isr_speech_start)
THUNK(isr_wait_frames)
THUNK(isr_timer_start)
THUNK(isr_dsp_reset)
THUNK(isr_speech_stop)
THUNK(par_setup_voice)
THUNK(frm_build_rest_frame)
THUNK(frm_copy_rest_frame)
THUNK(isr_wait_frame)
THUNK(isr_volume_half)
THUNK(par_play)
THUNK(frm_snapshot)
```

**`src/engine/thunks.c`**: replace

```c
    { "tn_append_phonetic", 0xB154, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_tn_append_phonetic },
    { "lts_convert_char",   0xB172, 0,                   t_lts_convert_char },
    { "tn_convert_clause",  0xB1E6, 0,                   t_tn_convert_clause },
    { "par_setup_voice",    0xB3A4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_setup_voice },
    { "frm_build_rest_frame", 0xB41D, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_build_rest_frame },
    { "frm_copy_rest_frame", 0xB445, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_copy_rest_frame },
    { "isr_volume_half",    0xB46C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_volume_half },
    { "par_play",           0xB877, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_play, true },
    { "frm_snapshot",       0xB917, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_snapshot },
```

with

```c
    { "tn_append_phonetic", 0xB154, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_tn_append_phonetic },
    { "lts_convert_char",   0xB172, 0,                   t_lts_convert_char },
    { "tn_convert_clause",  0xB1E6, 0,                   t_tn_convert_clause },
    { "isr_speech_start",   0xB323, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_speech_start, true },
    { "isr_wait_frames",    0xB356, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_wait_frames, true },
    { "isr_timer_start",    0xB35D, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_timer_start },
    { "isr_dsp_reset",      0xB38B, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_dsp_reset },
    { "isr_speech_stop",    0xB398, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_speech_stop },
    { "par_setup_voice",    0xB3A4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_setup_voice },
    { "frm_build_rest_frame", 0xB41D, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_build_rest_frame },
    { "frm_copy_rest_frame", 0xB445, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_copy_rest_frame },
    { "isr_wait_frame",     0xB465, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_wait_frame, true },
    { "isr_volume_half",    0xB46C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_volume_half },
    { "par_play",           0xB877, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_play, true },
    { "frm_snapshot",       0xB917, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_snapshot },
```

**`src/engine/thunks.h`**: replace

```c
    uint16_t addr;
    uint8_t outputs;          /* FW_OUT_* */
    machine_hook_fn thunk;    /* user = the fw_bridge bound to that machine */
    bool waits;               /* waits on the timer interrupt (the frame loop): checked by diff_play, not diff_lts */
    bool isr;                 /* an interrupt handler, entered through its vector: the thunk exits like RTI
                                 (machine_return_interrupt) and declares no outputs (M4.6a spec 3.2) */
} fw_replacement;
```

with

```c
    uint16_t addr;
    uint8_t outputs;          /* FW_OUT_* */
    machine_hook_fn thunk;    /* user = the fw_bridge bound to that machine */
    bool waits;               /* waits on the timer interrupt (the frame loop, the frame waits): checked by diff_play,
                                 not diff_lts */
    bool isr;                 /* an interrupt handler, entered through its vector: the thunk exits like RTI
                                 (machine_return_interrupt) and declares no outputs (M4.6a spec 3.2) */
} fw_replacement;
```

**`src/firmware/isr_dsp.c`**: replace

```c
#include "isr_dsp.h"
#include "fw_ops.h"

#define P3_KEPT  0x87   /* isr_set_volume $ACF1 LDA #$87 / AND P3: the bits the volume leaves */

/* ---- isr_set_volume $ACE8, isr_volume_half $B46C ---- */
```

with

```c
#include "isr_dsp.h"
#include "fw_ops.h"
#include "isr_addr.h"

#define P3_KEPT         0x87   /* isr_set_volume $ACF1 LDA #$87 / AND P3: the bits the volume leaves */
#define P3_TICK         0      /* isr_timer_start $B369 SEB0 P3 */
#define P2_DSP_RESET    0      /* P2.0: the DSP reset line */
#define ICR2_T2_EN      2      /* ICR2: timer 2's enable and request bits ... */
#define ICR2_T2_REQ     3
#define ICR2_T3_EN      4      /* ... and timer 3's */
#define ICR2_T3_REQ     5
#define ICR2_T3_ONLY    0x10   /* $B385: timer 3's interrupt enabled, nothing else */
#define T2_RELOAD_SYNC  4      /* $B36B */
#define T3_RELOAD_SYNC  0x28   /* $B375 */
#define T3_RELOAD_TICK  0x31   /* $B382: 4 x 50 CPU cycles, the 10 kHz tick */
#define ISR_TICKS       10     /* isr_tick_div's start: ticks per DSP word */
#define ISR_WORDS       10     /* isr_words_left's start: words per frame */

/* ---- isr_set_volume $ACE8, isr_volume_half $B46C ---- */
```

**`src/firmware/isr_dsp.c`**: replace

```c
    isr_set_volume(f, r);
}
```

with

```c
    isr_set_volume(f, r);
}

/* ---- isr_dsp_reset $B38B, isr_timer_start $B35D, isr_speech_stop $B398 ---- */

void isr_dsp_reset(fw *f, fw_regs *r)
{
    (void)r;                                         /* PHP; SEI ... PLP; NOP: registers and flags kept */
    fw_sfr_clb(f, SFR_P2, P2_DSP_RESET);             /* CLB0 P2; NOP */
    fw_sfr_seb(f, SFR_P2, P2_DSP_RESET);             /* SEB0 P2; NOP */
    fw_sfr_clb(f, SFR_P2, P2_DSP_RESET);             /* CLB0 P2 */
}

void isr_timer_start(fw *f, fw_regs *r)
{
    (void)r;                                         /* PHP; SEI ... PLP; NOP: registers and flags kept */
    fw_wr_sfr8(f, SFR_T2_PRE, 0);                    /* LDM #0,T2_PRE */
    fw_wr_sfr8(f, SFR_T3_PRE, 0);                    /* LDM #0,T3_PRE */
    fw_wr8(f, ISR_TICK_DIV, (uint8_t)(fw_rd8(f, ISR_TICK_DIV) - 1));   /* DEC isr_tick_div, twice */
    fw_wr8(f, ISR_TICK_DIV, (uint8_t)(fw_rd8(f, ISR_TICK_DIV) - 1));
    fw_sfr_seb(f, SFR_P3, P3_TICK);                  /* SEB0 P3 */
    fw_wr_sfr8(f, SFR_T2_RELOAD, T2_RELOAD_SYNC);    /* LDM #4,T2_RELOAD */
    fw_sfr_clb(f, SFR_ICR2, ICR2_T2_REQ);            /* CLB3 ICR2 */
    fw_sfr_clb(f, SFR_ICR2, ICR2_T2_EN);             /* CLB2 ICR2 */
    fw_wait(f, FW_WAIT_TIMER2, FW_WAIT_TIMER2_LOOP); /* $B372 BBC3 ICR2,$B372 */
    fw_wr_sfr8(f, SFR_T3_RELOAD, T3_RELOAD_SYNC);    /* LDM #$28,T3_RELOAD */
    fw_sfr_clb(f, SFR_ICR2, ICR2_T3_REQ);            /* CLB5 ICR2 */
    fw_sfr_clb(f, SFR_ICR2, ICR2_T3_EN);             /* CLB4 ICR2 */
    fw_wr_sfr8(f, SFR_P23_FUNC, 1);                  /* LDM #1,P23_FUNC */
    fw_wait(f, FW_WAIT_TIMER3, FW_WAIT_TIMER3_LOOP); /* $B37F BBC5 ICR2,$B37F */
    fw_wr_sfr8(f, SFR_T3_RELOAD, T3_RELOAD_TICK);    /* LDM #$31,T3_RELOAD */
    fw_wr_sfr8(f, SFR_ICR2, ICR2_T3_ONLY);           /* LDM #$10,ICR2 */
}

void isr_speech_stop(fw *f, fw_regs *r)
{
    fw_wr_sfr8(f, SFR_P23_FUNC, 0);                  /* LDM #0,P23_FUNC */
    fw_wr_sfr8(f, SFR_ICR2, 0);                      /* LDM #0,ICR2 */
    isr_dsp_reset(f, r);
    fw_clb(f, BOOT_SPEAKING, 0);                     /* CLB0 boot_speaking */
}

/* ---- isr_speech_start $B323, isr_wait_frames $B356, isr_wait_frame $B465 ---- */

/* ASL A; ROL addr: the bit A shifts out goes into bit 0 of the RAM byte; the A it leaves. */
static uint8_t asl_rol(fw *f, uint16_t addr, uint8_t a)
{
    fw_wr8(f, addr, (uint8_t)(fw_rd8(f, addr) << 1 | a >> 7));
    return (uint8_t)(a << 1);
}

void isr_speech_start(fw *f, fw_regs *r)
{
    r->x = 0;                                        /* LDX #0; STX frm_source_index; STX frm_filter_index */
    fw_wr8(f, FRM_SOURCE_INDEX, r->x);
    fw_wr8(f, FRM_FILTER_INDEX, r->x);
    r->a = fw_rd8(f, CMD_SOURCE_PROSODY);
    r->a = asl_rol(f, FRM_SOURCE_INDEX, r->a);       /* bit 7 */
    r->a = asl_rol(f, FRM_SOURCE_INDEX, r->a);       /* bit 6 */
    r->a = asl_rol(f, FRM_SOURCE_INDEX, r->a);       /* bit 5 */
    r->a = asl_rol(f, FRM_FILTER_INDEX, r->a);       /* bit 4 */
    fw_wr8(f, ISR_TICK_DIV, ISR_TICKS);              /* LDM #$0A */
    fw_wr8(f, ISR_WORDS_LEFT, ISR_WORDS);            /* LDM #$0A */
    r->a = 0;                                        /* LDA #0; STA isr_word_ofs; STA isr_frame_flag */
    fw_wr8(f, ISR_WORD_OFS, r->a);
    fw_wr8(f, ISR_FRAME_FLAG, r->a);
    isr_dsp_reset(f, r);
    isr_timer_start(f, r);
    r->x = 1;                                        /* LDX #1 */
    isr_wait_frames(f, r);
}

void isr_wait_frames(fw *f, fw_regs *r)
{
    do {
        isr_wait_frame(f, r);
        r->x = (uint8_t)(r->x - 1);                  /* DEX; BNE isr_wait_frames */
    } while (r->x != 0);
}

void isr_wait_frame(fw *f, fw_regs *r)
{
    (void)r;
    while (!(fw_rd8(f, ISR_FRAME_FLAG) & 1)) {       /* BBC0 isr_frame_flag,isr_wait_frame */
        fw_wait(f, FW_WAIT_INTERRUPT, ROM_ISR_WAIT_FRAME);
        if (f->fault)
            return;                                  /* the wait failed (without a trap): the flag would never come */
    }
    fw_wr8(f, ISR_FRAME_FLAG, 0);                    /* LDM #0,isr_frame_flag */
}
```

**`src/firmware/isr_dsp.h`**: replace

```c
/* $B46C: isr_set_volume with A = cmd_volume >> 1. */
void isr_volume_half(fw *f, fw_regs *r);

#endif
```

with

```c
/* $B46C: isr_set_volume with A = cmd_volume >> 1. */
void isr_volume_half(fw *f, fw_regs *r);

/* $B38B: the DSP reset line P2.0 low, high, low (CLB0, SEB0, CLB0 P2), inside PHP/SEI ... PLP. A, X and Y
   untouched. */
void isr_dsp_reset(fw *f, fw_regs *r);
/* $B35D: starts the 10 kHz DSP tick, inside PHP/SEI ... PLP: T2_PRE = T3_PRE = 0; isr_tick_div decremented twice;
   P3.0 set; T2_RELOAD = 4; ICR2 bits 3 and 2 cleared; a wait for timer 2's request bit (FW_WAIT_TIMER2, $B372);
   T3_RELOAD = $28; ICR2 bits 5 and 4 cleared; P23_FUNC = 1; a wait for timer 3's request bit (FW_WAIT_TIMER3,
   $B37F); T3_RELOAD = $31; ICR2 = $10 (timer 3's interrupt alone). A, X and Y untouched. */
void isr_timer_start(fw *f, fw_regs *r);
/* $B398: P23_FUNC = 0, ICR2 = 0, isr_dsp_reset, boot_speaking bit 0 cleared. A, X and Y untouched. */
void isr_speech_stop(fw *f, fw_regs *r);
/* $B323: frm_source_index = cmd_source_prosody's bits 7-5 and frm_filter_index = its bit 4 (both 0 first, then a ROL
   per bit); isr_tick_div = isr_words_left = 10, isr_word_ofs = isr_frame_flag = 0; isr_dsp_reset; isr_timer_start;
   isr_wait_frames with X = 1. A = 0 and X = 0 out, Y untouched. */
void isr_speech_start(fw *f, fw_regs *r);
/* $B356: X frame boundaries: isr_wait_frame, then DEX until X is 0 (X = 0 waits 256). X = 0 out, A and Y untouched. */
void isr_wait_frames(fw *f, fw_regs *r);
/* $B465: until the timer interrupt has set isr_frame_flag bit 0 (a frame boundary), a wait for the next interrupt
   (FW_WAIT_INTERRUPT at $B465, M4.6b spec 2.3); then the whole byte = 0 (LDM). A, X and Y untouched. A failed wait
   ends it (a fault). */
void isr_wait_frame(fw *f, fw_regs *r);

#endif
```

**`src/firmware/par_play.c`**: replace

```c
#include "cmd_play_event.h"
#include "frm_build.h"
#include "fw_ops.h"
#include "isr_addr.h"
#include "isr_dsp.h"
#include "isr_serial.h"
#include "par_addr.h"
```

with

```c
#include "cmd_play_event.h"
#include "frm_build.h"
#include "fw_ops.h"
#include "isr_dsp.h"
#include "isr_serial.h"
#include "par_addr.h"
```

**`src/firmware/par_play.c`**: replace

```c
    }
}

/* $B8D2-$B8FA, the end: isr_wait_frame; par_frames_left = 3; per rest frame frm_copy_rest_frame, frm_snapshot,
   frm_clamp_mark_space and frm_transition_flags (C, the registers flowing from one to the next), a wait for the
   swap, the flag cleared (LDM #0) and DEC par_frames_left (BNE $B8DA). Then cmd_ev_sound_end (a @T effect cut short
   is undone) and isr_par_ready. */
static void rest_frames(fw *f, fw_regs *r)
{
    fw_rom_call(f, ROM_ISR_WAIT_FRAME, r, FW_ROM_CALL_BUDGET);
    r->a = PAR_REST_FRAMES;
    fw_wr8(f, PAR_FRAMES_LEFT, r->a);
    do {
```

with

```c
    }
}

/* $B8D2-$B8FA, the end: isr_wait_frame (C); par_frames_left = 3; per rest frame frm_copy_rest_frame, frm_snapshot,
   frm_clamp_mark_space and frm_transition_flags (C, the registers flowing from one to the next), a wait for the
   swap, the flag cleared (LDM #0) and DEC par_frames_left (BNE $B8DA). Then cmd_ev_sound_end (a @T effect cut short
   is undone) and isr_par_ready. */
static void rest_frames(fw *f, fw_regs *r)
{
    isr_wait_frame(f, r);
    r->a = PAR_REST_FRAMES;
    fw_wr8(f, PAR_FRAMES_LEFT, r->a);
    do {
```

**`src/firmware/par_play.c`**: replace

```c
{
    isr_par_hold(f, r);
    par_play_start(f, r);
    fw_rom_call(f, ROM_ISR_SPEECH_START, r, FW_ROM_CALL_BUDGET);
    isr_volume_half(f, r);
    fw_rom_call(f, ROM_ISR_WAIT_FRAME, r, FW_ROM_CALL_BUDGET);
    r->a = fw_rd8(f, CMD_VOLUME);
    isr_set_volume(f, r);
    par_play(f, r);
    isr_volume_half(f, r);
    fw_rom_call(f, ROM_ISR_WAIT_FRAME, r, FW_ROM_CALL_BUDGET);
    r->a = 0;
    isr_set_volume(f, r);
    fw_rom_call(f, ROM_ISR_SPEECH_STOP, r, FW_ROM_CALL_BUDGET);
}

void boot_loop_play(fw *f, fw_regs *r)
```

with

```c
{
    isr_par_hold(f, r);
    par_play_start(f, r);
    isr_speech_start(f, r);
    isr_volume_half(f, r);
    isr_wait_frame(f, r);
    r->a = fw_rd8(f, CMD_VOLUME);
    isr_set_volume(f, r);
    par_play(f, r);
    isr_volume_half(f, r);
    isr_wait_frame(f, r);
    r->a = 0;
    isr_set_volume(f, r);
    isr_speech_stop(f, r);
}

void boot_loop_play(fw *f, fw_regs *r)
```

**`src/firmware/par_play.h`**: replace

```c
void par_play(fw *f, fw_regs *r);

/* $A7D4: when the utterance has content (FW_UTT_CONTENT, $6B) and no mute is pending (ser_abort bit 0), play it:
   isr_par_hold, par_play_start (C), isr_speech_start, isr_volume_half, isr_wait_frame, isr_set_volume(cmd_volume),
   par_play (C), isr_volume_half, isr_wait_frame, isr_set_volume(0), isr_speech_stop. r in: A, X and Y flow into
   the first calls. r out: A = FW_UTT_CONTENT and X, Y untouched when it does not play; else the registers
   isr_speech_stop leaves. */
void boot_loop_play(fw *f, fw_regs *r);

#endif
```

with

```c
void par_play(fw *f, fw_regs *r);

/* $A7D4: when the utterance has content (FW_UTT_CONTENT, $6B) and no mute is pending (ser_abort bit 0), play it:
   isr_par_hold, par_play_start, isr_speech_start, isr_volume_half, isr_wait_frame, isr_set_volume(cmd_volume),
   par_play, isr_volume_half, isr_wait_frame, isr_set_volume(0), isr_speech_stop (all C since M4.6b). r in: A, X and
   Y flow into the first calls. r out: A = FW_UTT_CONTENT and X, Y untouched when it does not play; else the
   registers isr_speech_stop leaves. */
void boot_loop_play(fw *f, fw_regs *r);

#endif
```

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test fw"`, then `build/apollo2_tests.exe machine_hooks`, `build/apollo2_tests.exe diff_lts`, `build/apollo2_tests.exe diff_play` and `build/apollo2_tests.exe diff_serial`.
Expected:

- the build passes with no warnings; `fw`: `9 suite(s), 0 failure(s), 0 skip(s)`; `suite_machine_hooks ok`;
- `diff_lts`: 94 lines `<name> N calls checked`: those of Task 3 Step 4 and `isr_timer_start 180 calls checked`, `isr_dsp_reset 360 calls checked`, `isr_speech_stop 180 calls checked`; `1 mid-clause flush calls skipped (golden_hybrid covers them)`, `2 unwound calls compared`, 0 mismatches (133 s in the proof run);
- `diff_play` (32 s):

```
  self-check (ROM against ROM, hello): par_play 1 calls, boot_loop_play 2 calls, 2 injected; $B8A5 reached 2, $B89C reached 2
  self-check (ROM against ROM, hello): isr_speech_start 1 calls, isr_wait_frames 1, isr_wait_frame 4
  5 routines wait on the timer interrupt
  boot_loop_play       179 calls checked, 48 with both injections
  isr_speech_start     126 calls checked, 0 with both injections
  isr_wait_frames      126 calls checked, 0 with both injections
  isr_wait_frame       504 calls checked, 0 with both injections
  par_play             126 calls checked, 48 with both injections
  $B8A5 (abort) reached in 96 calls, $B89C (reply due) in 96
suite_diff_play              ok
1 suite(s), 0 failure(s), 0 skip(s)
```

  (`isr_wait_frame`: four calls per checked `boot_loop_play` that plays: two in its ramps, one in `isr_wait_frames`, one at `par_play`'s end);
- `diff_serial` as in Task 3 Step 4.

- [ ] **Step 5: Run the full suite**

Run: `cmd //c ".\build.bat test"` (in the background)
Expected:

- `37 suite(s), 0 failure(s), 0 skip(s)`, no warnings, the firmware syntax check passing (682 s in the proof run);
- `firmware_data`: `48512 converted bytes compared with the ROM, 0 differ`;
- `diff_lts`, `diff_play` and `diff_serial` as in Step 4, 0 mismatches;
- `golden_hybrid`: `48 corpus files x 100 modes (all, then each routine alone)`, every file identical, every golden unchanged (`git status --short tests/golden` prints nothing);
- `suite_golden`: `timer scale 1/4: 105 frames, 11789152 CPU cycles` and `timer scale 3/1: 105 frames, 1286784 CPU cycles` (rom, unchanged), `hybrid timer scale 1/4: 105 frames, 11693127 CPU cycles`, `hybrid timer scale 3/1: 105 frames, 989241 CPU cycles` (fewer again);
- `suite_synth`: `corpus peak plain 20522 (-4.1 dBFS)`, `corpus peak b11 16038 (-6.2 dBFS)`.

- [ ] **Step 6: Commit**

```bash
git add src/firmware/isr_dsp.h src/firmware/isr_dsp.c src/firmware/par_play.h src/firmware/par_play.c src/engine/thunks.h src/engine/thunks.c tests/test_fw_dsp.c tests/test_fw_par.c tests/test_fw_tn.c tests/test_diff_play.c
git commit -m "isr_dsp_reset, isr_timer_start, isr_speech_stop, isr_speech_start, isr_wait_frames and isr_wait_frame in C, with hooks (the three frame waits as waits rows, watched by diff_play); boot_loop_play and par_play call them (M4.6b 2.1-2.6, 3.2, 3.5, 4.2)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 5: The timer interrupt in C: `isr_timer_dsp` $B2D9 as an `isr` replacement; `diff_play`'s copy B and `diff_serial` run the C; the branch probe; the CLI check (spec §2.1, §2.4, §2.6, §3.1, §4.1, §4.5, §4.7, §4.8 item 5)

**ROM facts checked / what was verified while proving the plan** (`rom_B2D9_isr_timer_dsp.txt`; `cpu740.c`, `machine.c`):
- **`isr_timer_dsp`** $B2D9: `CLB5 ICR2; DEC isr_tick_div; BEQ $B2E0; RTI`; then `PHA; TYA; PHA; LDY isr_word_ofs; LDA (isr_play_ptr),Y; STA DSP_DATA; INY; LDA (isr_play_ptr),Y; STA DSP_DATA; INY; STY isr_word_ofs; LDM #$0A,isr_tick_div; DEC isr_words_left; BNE $B31F; LDM #$0A,isr_words_left; LDM #0,isr_word_ofs; BBC0 isr_frame_flag,$B306; JMP $B31F`; at $B306 `LDM #1,isr_frame_flag; DEC isr_swap_phase; BNE $B319; LDM #$A5,isr_play_ptr; LDM #$B9,isr_fill_ptr; LDM #2,isr_swap_phase; JMP $B31F`; $B319 `LDM #$B9,isr_play_ptr; LDM #$A5,isr_fill_ptr`; $B31F `PLA; TAY; PLA; RTI`. The `INY`s are 8-bit (`test_timer_word`: Y $FE, $FF, then 0); the `LDM`s write the pointers' low bytes only; the frame event comes before the `LDM #1` and after the counters' reset (`test_timer_swap` reads them at the event: 10, 0, 10 and the flag still 0), which is what the machine's capture at $B306 sees. A, X and Y are as they came at every exit (the pushes are pulled), so the row declares no outputs; `diff_lts` compares A, X and Y for it all the same (M4.6a). The C makes no ROM call: the DSP bytes and the frame event go through the host.
- **`diff_lts` compares every call of `isr_timer_dsp`**: **2,589,942** on the corpus, the boot run and the extra text (spec §4.1 estimated about 700,000; the branch probe's $B2DD counts 252,080 ticks with a word due and 2,268,473 without, on the corpus and the boot run). Together with `isr_par_ready`'s 7,959,385 (Task 3) they make most of `diff_lts`'s time: 179 s in the proof run (17 s in Task 1; `isr_par_ready`'s every call added about 120 s in Task 3, `isr_timer_dsp`'s about 45 s here), under the 5 minutes of the run-time gate.
- **`diff_play`'s copy B** runs the C timer interrupt from this task on: its $B2D9 hook (Task 2) finds the replacement and runs it on the copy's bridge, nested in the watched routine's thunk; the run counts the interrupts copy B ran in C (`7,770,113` in the proof run) and requires some. The frame event reaches the same frame callback through `frame_done` and `machine_capture_frame`.
- **`diff_serial`** requires the C for every probe again: `k_not_c_yet` goes; `timer_repeat` compares the C handler.
- **The branch probe** (Step 5) and the CLI check (Step 6) are recorded below.

**Files:**
- Modify: `src/firmware/isr_dsp.h`, `src/firmware/isr_dsp.c`, `src/engine/thunks.c`, `tests/test_fw_dsp.c`, `tests/test_diff_play.c`, `tests/test_diff_serial.c`

**Interfaces:**
- Consumes: Tasks 1–4 (`fw_dsp_write`, `fw_frame_done`, `fw_ind_y`, the SFR names and counters of `isr_dsp.c`, `run_frame` with `isr` set through `ISR_THUNK`).
- Produces:

```c
/* src/firmware/isr_dsp.h */
void isr_timer_dsp(fw *f, fw_regs *r);
/* thunks.c: the isr row { "isr_timer_dsp", 0xB2D9, 0, t_isr_timer_dsp, false, true } (100 replacements) */
```

- [ ] **Step 1: Write the failing tests**

**`tests/test_diff_play.c`**: replace

```c
static const char *g_source;
static bool g_boot_run, g_blp_checked, g_play_armed, g_frames_armed, g_frame_armed;
static unsigned g_mismatches, g_reached_abort, g_reached_reply;
static bool g_hit_abort, g_hit_reply;

static void take(swap_state *s, const machine *m, const uint8_t frame[APOLLO_FRAME_BYTES])
```

with

```c
static const char *g_source;
static bool g_boot_run, g_blp_checked, g_play_armed, g_frames_armed, g_frame_armed;
static unsigned g_mismatches, g_reached_abort, g_reached_reply;
static uint64_t g_isr_c;       /* timer interrupts copy B ran in C */
static bool g_hit_abort, g_hit_reply;

static void take(swap_state *s, const machine *m, const uint8_t frame[APOLLO_FRAME_BYTES])
```

**`tests/test_diff_play.c`**: replace

```c
    recorder *rec = user;
    if (rec->abort_in && --rec->abort_in == 0)
        m->ram[SER_ABORT] |= 1;
    return rec->isr ? rec->isr->thunk(m, rec->bridge) : false;
}

/* The copy-A hooks: which of the two injected branches the ROM routine executed. */
```

with

```c
    recorder *rec = user;
    if (rec->abort_in && --rec->abort_in == 0)
        m->ram[SER_ABORT] |= 1;
    if (!rec->isr)
        return false;
    g_isr_c++;
    return rec->isr->thunk(m, rec->bridge);
}

/* The copy-A hooks: which of the two injected branches the ROM routine executed. */
```

**`tests/test_diff_play.c`**: replace

```c
    CHECK_EQ(g_nwatch, 5);   /* par_play and boot_loop_play (M4.4b spec 2.5), the frame waits (M4.6b spec 2.4) */
    if (g_nwatch > 0) {
        g_reached_abort = g_reached_reply = 0;
        run_corpus(rom, len);
        for (size_t i = 0; i < g_nwatch; i++) {
            printf("  %-20s %u calls checked, %u with both injections\n", g_watch[i].name, g_watch[i].calls,
```

with

```c
    CHECK_EQ(g_nwatch, 5);   /* par_play and boot_loop_play (M4.4b spec 2.5), the frame waits (M4.6b spec 2.4) */
    if (g_nwatch > 0) {
        g_reached_abort = g_reached_reply = 0;
        g_isr_c = 0;
        run_corpus(rom, len);
        for (size_t i = 0; i < g_nwatch; i++) {
            printf("  %-20s %u calls checked, %u with both injections\n", g_watch[i].name, g_watch[i].calls,
```

**`tests/test_diff_play.c`**: replace

```c
        printf("  $B8A5 (abort) reached in %u calls, $B89C (reply due) in %u\n", g_reached_abort, g_reached_reply);
        CHECK(g_reached_abort > 0);
        CHECK(g_reached_reply > 0);
    }
    CHECK_EQ(g_mismatches, 0);
    free(g_ref);
```

with

```c
        printf("  $B8A5 (abort) reached in %u calls, $B89C (reply due) in %u\n", g_reached_abort, g_reached_reply);
        CHECK(g_reached_abort > 0);
        CHECK(g_reached_reply > 0);
        printf("  %llu timer interrupts run in C in copy B\n", (unsigned long long)g_isr_c);
        CHECK(g_isr_c > 0);   /* isr_timer_dsp is a replacement (M4.6b): copy B's interrupts are C */
    }
    CHECK_EQ(g_mismatches, 0);
    free(g_ref);
```

**`tests/test_diff_serial.c`**: replace

```c
    free(text);
}

/* The probes' routines that are not C yet (M4.6b): their copy B runs the ROM until Task 5 of the M4.6b plan makes it
   C. */
static const char *const k_not_c_yet[] = { "isr_timer_dsp" };

static bool not_c_yet(const char *routine)
{
    for (size_t i = 0; i < sizeof k_not_c_yet / sizeof k_not_c_yet[0]; i++)
        if (!strcmp(k_not_c_yet[i], routine))
            return true;
    return false;
}

/* Prints each probe's counts; every probe compared and its target reached at every call. */
static void report(void)
{
```

with

```c
    free(text);
}

/* Prints each probe's counts; every probe compared and its target reached at every call. */
static void report(void)
{
```

**`tests/test_diff_serial.c`**: replace

```c
                   c ? "C" : "ROM", p->reached);
        CHECK(p->calls > 0);
        CHECK_EQ(p->reached, p->calls);
        CHECK(c || g_self_check || not_c_yet(p->routine));   /* every probe's routine is C (M4.6a, M4.6b) */
    }
}
```

with

```c
                   c ? "C" : "ROM", p->reached);
        CHECK(p->calls > 0);
        CHECK_EQ(p->reached, p->calls);
        CHECK(c || g_self_check);   /* every probe's routine is C (M4.6a, M4.6b) */
    }
}
```

**`tests/test_fw_dsp.c`**: replace

```c
static unsigned g_nlog;
static bool g_wait_ok;           /* what the stub's wait returns */
static unsigned g_iwaits;        /* FW_WAIT_INTERRUPT waits so far */
static unsigned g_flag_every;    /* the scripted timer interrupt sets isr_frame_flag bit 0 at every n-th (0: never) */

static void log_add(ev_kind kind, uint16_t addr, uint8_t value)
```

with

```c
static unsigned g_nlog;
static bool g_wait_ok;           /* what the stub's wait returns */
static unsigned g_iwaits;        /* FW_WAIT_INTERRUPT waits so far */
static uint8_t g_at_event[4];    /* isr_tick_div, isr_word_ofs, isr_words_left and isr_frame_flag at the frame event */
static unsigned g_flag_every;    /* the scripted timer interrupt sets isr_frame_flag bit 0 at every n-th (0: never) */

static void log_add(ev_kind kind, uint16_t addr, uint8_t value)
```

**`tests/test_fw_dsp.c`**: replace

```c
{
    (void)ctx;
    log_add(EV_FRAME, FW_FRAME_EVENT, 0);
}

/* Zeroed RAM and SFRs, the stub host (no ROM call, no window call: speech timing makes neither), nothing logged. */
```

with

```c
{
    (void)ctx;
    log_add(EV_FRAME, FW_FRAME_EVENT, 0);
    for (int i = 0; i < 4; i++)
        g_at_event[i] = g_ram[ISR_TICK_DIV + i];
}

/* Zeroed RAM and SFRs, the stub host (no ROM call, no window call: speech timing makes neither), nothing logged. */
```

**`tests/test_fw_dsp.c`**: replace

```c
static void test_rows(void)
{
    check_row("isr_set_volume", 0xACE8, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_volume_half", 0xB46C, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_par_ready", 0xC5D6, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
```

with

```c
static void test_rows(void)
{
    const fw_replacement *t = fw_replacement_find("isr_timer_dsp");   /* an interrupt handler: no outputs */
    CHECK(t != NULL);
    if (t) {
        CHECK_EQ(t->addr, 0xB2D9);
        CHECK_EQ(t->outputs, 0);
        CHECK(!t->waits);
        CHECK(t->isr);
    }
    check_row("isr_set_volume", 0xACE8, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_volume_half", 0xB46C, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_par_ready", 0xC5D6, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
```

**`tests/test_fw_dsp.c`**: replace

```c
    CHECK(!g_f.fault);
}

void suite_fw_dsp(void)
{
    CHECK(fw_image_get() != NULL);
```

with

```c
    CHECK(!g_f.fault);
}

/* isr_timer_dsp's state: isr_tick_div, isr_word_ofs, isr_words_left, isr_frame_flag and isr_swap_phase; the
   ping-pong pointers at isr_frame_a (play) and isr_frame_b (fill); the 40 bytes of the two frames $16A5-$16CC =
   $40 + i; ICR2 with timer 3's request and enable bits ($30). */
static void timer_setup(uint8_t tick, uint8_t ofs, uint8_t words, uint8_t flag, uint8_t phase)
{
    setup();
    g_ram[ISR_TICK_DIV] = tick;
    g_ram[ISR_WORD_OFS] = ofs;
    g_ram[ISR_WORDS_LEFT] = words;
    g_ram[ISR_FRAME_FLAG] = flag;
    g_ram[ISR_SWAP_PHASE] = phase;
    fw_wr16(&g_f, ISR_PLAY_PTR, ISR_FRAME_A);
    fw_wr16(&g_f, ISR_FILL_PTR, ISR_FRAME_B);
    for (int i = 0; i < 40; i++)
        g_ram[ISR_FRAME_A + i] = (uint8_t)(0x40 + i);
    g_sfr[SFR_ICR2] = 0x30;
}

/* isr_timer_dsp $B2D9: A, X and Y as they came. */
static void run_timer(void)
{
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_timer_dsp(&g_f, &r);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0x22);
    CHECK_EQ(r.y, 0x33);
    CHECK(!g_f.fault);
}

/* isr_timer_dsp: a tick not due (isr_tick_div 3 -> 2): CLB5 ICR2 (timer 3's request cleared), nothing else. A word
   due (isr_tick_div 1 -> 0, isr_words_left 5 -> 4): the word at isr_frame_a + 4 to the DSP port, even byte first,
   isr_word_ofs 4 -> 6, isr_tick_div = 10. isr_word_ofs $FE: Y wraps (INY, 8 bits): $FE, $FF, then 0. */
static void test_timer_word(void)
{
    static const event tick[] = { { EV_READ, SFR_ICR2, 0x30 }, { EV_WRITE, SFR_ICR2, 0x10 } };
    timer_setup(3, 4, 5, 0, 2);
    run_timer();
    check_log(tick, 2);
    CHECK_EQ(g_ram[ISR_TICK_DIV], 2);
    CHECK_EQ(g_ram[ISR_WORD_OFS], 4);
    CHECK_EQ(g_ram[ISR_WORDS_LEFT], 5);

    static const event word[] = {
        { EV_READ, SFR_ICR2, 0x30 }, { EV_WRITE, SFR_ICR2, 0x10 }, { EV_DSP, FW_DSP_DATA, 0x44 },
        { EV_DSP, FW_DSP_DATA, 0x45 },
    };
    timer_setup(1, 4, 5, 0, 2);
    run_timer();
    check_log(word, 4);
    CHECK_EQ(g_ram[ISR_TICK_DIV], 10);
    CHECK_EQ(g_ram[ISR_WORD_OFS], 6);
    CHECK_EQ(g_ram[ISR_WORDS_LEFT], 4);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0);

    timer_setup(1, 0xFE, 5, 0, 2);
    fw_wr16(&g_f, ISR_PLAY_PTR, 0x1600);
    g_ram[0x16FE] = 0x5A;
    g_ram[0x16FF] = 0xA5;
    run_timer();
    CHECK_EQ(g_log[2].value, 0x5A);
    CHECK_EQ(g_log[3].value, 0xA5);
    CHECK_EQ(g_ram[ISR_WORD_OFS], 0);
}

/* isr_timer_dsp at a frame boundary (the last word: isr_tick_div 1, isr_words_left 1, isr_word_ofs 18), the flag
   clear: the word, then the frame event ($B306) with the counters already reset (isr_tick_div and isr_words_left 10,
   isr_word_ofs 0) and the flag still clear, then isr_frame_flag = 1 and the swap. Phase 2 -> 1: isr_play_ptr's low
   byte $B9, isr_fill_ptr's $A5; phase 1 -> 0: $A5 and $B9, the phase 2 again. The high bytes are not written. */
static void test_timer_swap(void)
{
    static const event log[] = {
        { EV_READ, SFR_ICR2, 0x30 }, { EV_WRITE, SFR_ICR2, 0x10 }, { EV_DSP, FW_DSP_DATA, 0x52 },
        { EV_DSP, FW_DSP_DATA, 0x53 }, { EV_FRAME, FW_FRAME_EVENT, 0 },
    };
    static const uint8_t at_event[4] = { 10, 0, 10, 0 };
    timer_setup(1, 18, 1, 0, 2);
    run_timer();
    check_log(log, 5);
    CHECK(memcmp(g_at_event, at_event, 4) == 0);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 1);
    CHECK_EQ(g_ram[ISR_SWAP_PHASE], 1);
    CHECK_EQ(fw_rd16(&g_f, ISR_PLAY_PTR), ISR_FRAME_B);
    CHECK_EQ(fw_rd16(&g_f, ISR_FILL_PTR), ISR_FRAME_A);

    timer_setup(1, 18, 1, 0, 1);
    g_ram[ISR_FILL_PTR + 1] = 0x55;             /* a high byte the LDMs leave */
    run_timer();
    check_log(log, 5);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 1);
    CHECK_EQ(g_ram[ISR_SWAP_PHASE], 2);
    CHECK_EQ(fw_rd16(&g_f, ISR_PLAY_PTR), ISR_FRAME_A);
    CHECK_EQ(g_ram[ISR_FILL_PTR], 0xB9);
    CHECK_EQ(g_ram[ISR_FILL_PTR + 1], 0x55);
}

/* isr_timer_dsp at a frame boundary with isr_frame_flag bit 0 still set ($03): the frame repeats: no frame event,
   the flag byte, the phase and the pointers unchanged; the counters reset as at any boundary. */
static void test_timer_repeat(void)
{
    static const event log[] = {
        { EV_READ, SFR_ICR2, 0x30 }, { EV_WRITE, SFR_ICR2, 0x10 }, { EV_DSP, FW_DSP_DATA, 0x52 },
        { EV_DSP, FW_DSP_DATA, 0x53 },
    };
    timer_setup(1, 18, 1, 0x03, 2);
    run_timer();
    check_log(log, 4);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0x03);
    CHECK_EQ(g_ram[ISR_SWAP_PHASE], 2);
    CHECK_EQ(fw_rd16(&g_f, ISR_PLAY_PTR), ISR_FRAME_A);
    CHECK_EQ(fw_rd16(&g_f, ISR_FILL_PTR), ISR_FRAME_B);
    CHECK_EQ(g_ram[ISR_TICK_DIV], 10);
    CHECK_EQ(g_ram[ISR_WORD_OFS], 0);
    CHECK_EQ(g_ram[ISR_WORDS_LEFT], 10);
}

void suite_fw_dsp(void)
{
    CHECK(fw_image_get() != NULL);
```

**`tests/test_fw_dsp.c`**: replace

```c
    test_speech_start();
    test_wait_frame();
    test_wait_frames_two();
}
```

with

```c
    test_speech_start();
    test_wait_frame();
    test_wait_frames_two();
    test_timer_word();
    test_timer_swap();
    test_timer_repeat();
}
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw_dsp"`
Expected: the build stops with compile errors (the proof run, first lines):

```
tests\test_fw_dsp.c(568): error C2220: the following warning is treated as an error
tests\test_fw_dsp.c(568): warning C4013: 'isr_timer_dsp' undefined; assuming extern returning int
```

- [ ] **Step 3: Implement**

**`src/engine/thunks.c`**: replace

```c
{
    (void)c;
    tn_convert_clause(f);
}

/* isr_speech_start $B323: no register input (X and A are written first: $B323 LDX #0, $B32B LDA). A = 0 ($B344 LDA
```

with

```c
{
    (void)c;
    tn_convert_clause(f);
}

/* isr_timer_dsp $B2D9, an interrupt handler (isr): entered by the CPU through vec_int2_timers $FFF8, with the PC and P
   pushed and I set. A, X and Y in; every path leaves them as they came (the RTI at $B2DF touches none; the word path
   pushes A and Y at $B2E0-$B2E2 and pulls them at $B31F-$B321), and the RTI pulls P and the PC
   (machine_return_interrupt): the interrupted code goes on with its own registers. The C makes no ROM call (the DSP
   port and the frame event go through the host), so the machine's registers stay; they are set back from the entry
   values all the same. No outputs declared (M4.6a spec 3.2). */
static void isr_timer_dsp_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    isr_timer_dsp(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* isr_speech_start $B323: no register input (X and A are written first: $B323 LDX #0, $B32B LDA). A = 0 ($B344 LDA
```

**`src/engine/thunks.c`**: replace

```c
/* The hook of an interrupt handler: its frame exits like RTI. */
#define ISR_THUNK(name) \
    static bool t_##name(machine *m, void *user) { return run_frame(m, user, name##_regs, true); }
ISR_THUNK(isr_int1)
ISR_THUNK(isr_int4_host_line)
#undef ISR_THUNK
```

with

```c
/* The hook of an interrupt handler: its frame exits like RTI. */
#define ISR_THUNK(name) \
    static bool t_##name(machine *m, void *user) { return run_frame(m, user, name##_regs, true); }
ISR_THUNK(isr_timer_dsp)
ISR_THUNK(isr_int1)
ISR_THUNK(isr_int4_host_line)
#undef ISR_THUNK
```

**`src/engine/thunks.c`**: replace

```c
    { "tn_append_phonetic", 0xB154, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_tn_append_phonetic },
    { "lts_convert_char",   0xB172, 0,                   t_lts_convert_char },
    { "tn_convert_clause",  0xB1E6, 0,                   t_tn_convert_clause },
    { "isr_speech_start",   0xB323, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_speech_start, true },
    { "isr_wait_frames",    0xB356, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_wait_frames, true },
    { "isr_timer_start",    0xB35D, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_timer_start },
```

with

```c
    { "tn_append_phonetic", 0xB154, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_tn_append_phonetic },
    { "lts_convert_char",   0xB172, 0,                   t_lts_convert_char },
    { "tn_convert_clause",  0xB1E6, 0,                   t_tn_convert_clause },
    { "isr_timer_dsp",      0xB2D9, 0,                   t_isr_timer_dsp, false, true },
    { "isr_speech_start",   0xB323, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_speech_start, true },
    { "isr_wait_frames",    0xB356, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_wait_frames, true },
    { "isr_timer_start",    0xB35D, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_isr_timer_start },
```

**`src/firmware/isr_dsp.c`**: replace

```c
#define T3_RELOAD_TICK  0x31   /* $B382: 4 x 50 CPU cycles, the 10 kHz tick */
#define ISR_TICKS       10     /* isr_tick_div's start: ticks per DSP word */
#define ISR_WORDS       10     /* isr_words_left's start: words per frame */

/* ---- isr_set_volume $ACE8, isr_volume_half $B46C ---- */
```

with

```c
#define T3_RELOAD_TICK  0x31   /* $B382: 4 x 50 CPU cycles, the 10 kHz tick */
#define ISR_TICKS       10     /* isr_tick_div's start: ticks per DSP word */
#define ISR_WORDS       10     /* isr_words_left's start: words per frame */
#define ISR_FRAME_A_LO  0xA5   /* the low bytes of the ping-pong buffers isr_frame_a $16A5 ... */
#define ISR_FRAME_B_LO  0xB9   /* ... and isr_frame_b $16B9 */
#define ISR_SWAP_PHASES 2      /* $B313: isr_swap_phase counts 2 -> 1 -> 2 */

/* ---- isr_timer_dsp $B2D9 ---- */

void isr_timer_dsp(fw *f, fw_regs *r)
{
    (void)r;                                         /* PHA / TYA / PHA ... PLA / TAY / PLA; RTI: A, X, Y kept */
    fw_sfr_clb(f, SFR_ICR2, ICR2_T3_REQ);            /* CLB5 ICR2 */
    uint8_t t = (uint8_t)(fw_rd8(f, ISR_TICK_DIV) - 1);   /* DEC isr_tick_div; BEQ */
    fw_wr8(f, ISR_TICK_DIV, t);
    if (t != 0)
        return;                                      /* $B2DF RTI */
    uint8_t y = fw_rd8(f, ISR_WORD_OFS);             /* LDY isr_word_ofs */
    fw_dsp_write(f, fw_ind_y(f, ISR_PLAY_PTR, y));   /* LDA (isr_play_ptr),Y; STA DSP_DATA */
    y = (uint8_t)(y + 1);                            /* INY */
    fw_dsp_write(f, fw_ind_y(f, ISR_PLAY_PTR, y));
    y = (uint8_t)(y + 1);
    fw_wr8(f, ISR_WORD_OFS, y);                      /* STY isr_word_ofs */
    fw_wr8(f, ISR_TICK_DIV, ISR_TICKS);              /* LDM #$0A */
    uint8_t w = (uint8_t)(fw_rd8(f, ISR_WORDS_LEFT) - 1);   /* DEC isr_words_left; BNE */
    fw_wr8(f, ISR_WORDS_LEFT, w);
    if (w != 0)
        return;
    fw_wr8(f, ISR_WORDS_LEFT, ISR_WORDS);            /* LDM #$0A */
    fw_wr8(f, ISR_WORD_OFS, 0);                      /* LDM #0 */
    if (fw_rd8(f, ISR_FRAME_FLAG) & 1)               /* BBC0 isr_frame_flag: still set, the frame repeats */
        return;
    fw_frame_done(f);                                /* $B306 isr_swap_buffers: the frame event */
    fw_wr8(f, ISR_FRAME_FLAG, 1);                    /* LDM #1 */
    uint8_t phase = (uint8_t)(fw_rd8(f, ISR_SWAP_PHASE) - 1);   /* DEC isr_swap_phase; BNE */
    fw_wr8(f, ISR_SWAP_PHASE, phase);
    if (phase == 0) {
        fw_wr8(f, ISR_PLAY_PTR, ISR_FRAME_A_LO);     /* LDM: the low bytes only */
        fw_wr8(f, ISR_FILL_PTR, ISR_FRAME_B_LO);
        fw_wr8(f, ISR_SWAP_PHASE, ISR_SWAP_PHASES);
    } else {
        fw_wr8(f, ISR_PLAY_PTR, ISR_FRAME_B_LO);     /* $B319 */
        fw_wr8(f, ISR_FILL_PTR, ISR_FRAME_A_LO);
    }
}

/* ---- isr_set_volume $ACE8, isr_volume_half $B46C ---- */
```

**`src/firmware/isr_dsp.h`**: replace

```c
#define SFR_P3          0xEE   /* bits 3-6: the inverted volume level; bit 0 set by isr_timer_start */
#define SFR_P2          0xF0   /* bit 0: the DSP reset line */
#define SFR_ICR2        0xFE   /* timers 1-3: enable bits 0, 2, 4, request bits 1, 3, 5 */

/* $ACE8: A = the volume 0-15 (0 silent): P3 bits 3-6 = its inverted level, (A ^ $F) & $F shifted left 3
   (util_shl3), kept in the scratch byte $75; the other P3 bits kept (AND #$87). A = the value written to P3; X and Y
```

with

```c
#define SFR_P3          0xEE   /* bits 3-6: the inverted volume level; bit 0 set by isr_timer_start */
#define SFR_P2          0xF0   /* bit 0: the DSP reset line */
#define SFR_ICR2        0xFE   /* timers 1-3: enable bits 0, 2, 4, request bits 1, 3, 5 */

/* $B2D9, an interrupt handler, entered through vec_int2_timers $FFF8 (timer 3, the 10 kHz DSP tick): CLB5 ICR2;
   isr_tick_div counted down; at 0 the word at (isr_play_ptr),Y with Y = isr_word_ofs goes to the DSP port, its even
   byte first (fw_dsp_write twice), isr_word_ofs + 2 (8 bits), isr_tick_div = 10 and isr_words_left counted down; at
   0 isr_words_left = 10 and isr_word_ofs = 0, and with isr_frame_flag bit 0 clear the frame event (fw_frame_done,
   where the ROM reaches $B306), isr_frame_flag = 1 (LDM) and the buffers swapped by isr_swap_phase counted down (to
   1: isr_play_ptr's low byte $B9, isr_fill_ptr's $A5; to 0: $A5, $B9 and the phase 2 again); with the flag still set
   the frame repeats. A, X and Y as they came (PHA / TYA / PHA ... PLA / TAY / PLA; RTI). */
void isr_timer_dsp(fw *f, fw_regs *r);

/* $ACE8: A = the volume 0-15 (0 silent): P3 bits 3-6 = its inverted level, (A ^ $F) & $F shifted left 3
   (util_shl3), kept in the scratch byte $75; the other P3 bits kept (AND #$87). A = the value written to P3; X and Y
```

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test fw"`, then `build/apollo2_tests.exe diff_lts`, `build/apollo2_tests.exe diff_play` and `build/apollo2_tests.exe diff_serial`.
Expected:

- the build passes with no warnings; `fw`: `9 suite(s), 0 failure(s), 0 skip(s)`;
- `diff_lts`: 95 lines `<name> N calls checked`: those of Task 4 Step 4 and `isr_timer_dsp 2589942 calls checked`; `1 mid-clause flush calls skipped (golden_hybrid covers them)`, `2 unwound calls compared`, 0 mismatches (179 s in the proof run, under the 5 minutes the run-time gate allows);
- `diff_play`: the lines of Task 4 Step 4, then `7770113 timer interrupts run in C in copy B`, 0 mismatches (29 s);
- `diff_serial`: every probe `(C)` on the corpus, among them `timer_repeat isr_timer_dsp 2649 calls compared (C), $B300 -> $B303 in 2649` (the self-check's 23 stay `(ROM)`), 0 mismatches (6 s).

- [ ] **Step 5: The branch probe (recorded; the tool is not part of the plan)**

A throwaway program run while proving the plan (`rom_engine_run` over the 47 texts and the boot run, a hook at both destinations of each conditional branch of the 11 routines that finds the branch as the last instruction executed) counted each branch taken and not taken on the corpus:

| Routine | Branch | Taken | Not taken | The missing direction |
|---|---|---|---|---|
| `isr_timer_dsp` | $B2DD `BEQ` | 252,080 | 2,268,473 | |
| `isr_timer_dsp` | $B2F8 `BNE` | 226,872 | 25,208 | |
| `isr_timer_dsp` | $B300 `BBC0 isr_frame_flag` | 25,208 | 0 | the probe `timer_repeat` (Task 2), 2,649 calls |
| `isr_timer_dsp` | $B30B `BNE` | 12,565 | 12,643 | |
| `isr_timer_start` | $B372 `BBC3 ICR2` | 173 | 173 | |
| `isr_timer_start` | $B37F `BBC5 ICR2` | 3,633 | 173 | |
| `isr_wait_frames` | $B35A `BNE` | 0 | 173 | pinned by `test_wait_frames_two` (Task 4): no probe reaches it (its one caller passes X = 1, and a probe's copies hold interrupts, so no frame comes) |
| `isr_wait_frame` | $B465 `BBC0 isr_frame_flag` | 1,187,206 | 692 | |
| `isr_par_ready` | $C5D7 `BBC0 boot_dip` | 7,425,753 | 0 | the probe `par_ready` (Task 2) |
| `isr_par_hold` | $C5DF `BBC0 boot_dip` | 3,204 | 0 | the probe `par_hold` (Task 2) |

`isr_set_volume`, `isr_volume_half`, `isr_speech_start`, `isr_dsp_reset` and `isr_speech_stop` have no conditional branch. Every branch is seen both ways on the corpus, under a probe, or (one) in a named unit test: spec §4.7 criterion 3.

- [ ] **Step 6: Run the full suite and the CLI check**

Run: `cmd //c ".\build.bat test"` (in the background)
Expected:

- `37 suite(s), 0 failure(s), 0 skip(s)`, no warnings, the firmware syntax check passing (727 s in the proof run);
- `firmware_data`: `48512 converted bytes compared with the ROM, 0 differ`;
- `golden_hybrid`: `48 corpus files x 101 modes (all, then each routine alone)`, every file identical, every golden unchanged (`git status --short tests/golden` prints nothing);
- `diff_lts` (95 lines), `diff_play` and `diff_serial` as in Step 4, 0 mismatches;
- `suite_golden` (`test_timing_invariance` on both engines): `timer scale 1/4: 105 frames, 11789152 CPU cycles` and `timer scale 3/1: 105 frames, 1286784 CPU cycles` (rom, unchanged), `hybrid timer scale 1/4: 105 frames, 11692891 CPU cycles`, `hybrid timer scale 3/1: 105 frames, 987805 CPU cycles` (fewer again: the timer interrupt takes no emulated time);
- `suite_synth`: `corpus peak plain 20522 (-4.1 dBFS)`, `corpus peak b11 16038 (-6.2 dBFS)`.

Then spec §4.7 criterion 5 on every corpus text (each read from its file, the WAV files in a scratch folder outside the repo, always with `-o`):

```bash
SP="${TMPDIR:-/tmp}/m46b_cli"
mkdir -p "$SP"
for f in tests/corpus/*.txt; do
  t=$(basename "$f" .txt)
  ./build/apollo2.exe --engine rom --text-file "$f" -o "$SP/${t}_rom.wav" || echo "$t rom failed"
  ./build/apollo2.exe --engine hybrid --text-file "$f" -o "$SP/${t}_hybrid.wav" || echo "$t hybrid failed"
  cmp -s "$SP/${t}_rom.wav" "$SP/${t}_hybrid.wav" && echo "$t same" || echo "$t DIFFERS"
done | grep -vc " same$"
rm -rf "$SP"
```

Expected: `0` (all 47 texts `same`; no `failed` or `DIFFERS` line). The proof run took 13 s.

Last, spec §4.7 criterion 6: `grep -rn "fw_rom_call" src/firmware | grep "ROM_ISR_"` prints nothing, and `grep -rn "ROM_ISR_" src/firmware | grep -v "isr_addr.h\|ser_addr.h"` prints only `isr_dsp.c`'s `fw_wait(f, FW_WAIT_INTERRUPT, ROM_ISR_WAIT_FRAME)` (the wait's `at`, not a call).

- [ ] **Step 7: Commit**

```bash
git add src/firmware/isr_dsp.h src/firmware/isr_dsp.c src/engine/thunks.c tests/test_fw_dsp.c tests/test_diff_play.c tests/test_diff_serial.c
git commit -m "isr_timer_dsp in C as an interrupt replacement (the DSP port and the frame event through the host), with its hook; diff_play's copy B and diff_serial compare the C timer interrupt (M4.6b 2.1, 2.4, 3.1, 4.1-4.3, 4.5)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 6: Parent spec rev 16, M4.6b status and README (spec §4.7 criterion 7)

**Files:**
- Modify: `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`, `docs/superpowers/specs/2026-10-09-m4-6b-speech-timing-design.md`, `README.md`

**Interfaces:** documentation only.

- [ ] **Step 1: Parent spec, rev 16**

Make these edits in `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`. Mark each one *(rev 16)*. Find each place by its text, not by line number.

1. **Status line** (line 4). Insert after `**Status:** Approved. `:

   `*(rev 16)* Revision 16 records milestone 4.6b: speech timing and the DSP in C (the timer interrupt, the start and stop of DSP output, the frame waits, the volume, the parallel handshake); the DSP port and the frame event go through the host, and polling loops wait for the next interrupt through the host (design: docs/superpowers/specs/2026-10-09-m4-6b-speech-timing-design.md). `

2. **§8 milestone 4** (the paragraph containing `**M4.6a done**`). Replace its last sentence, `Next: M4.6b, speech timing and the DSP.`, with:

   `Next: M4.6b, speech timing and the DSP. *(rev 16)* **M4.6b done** <DATE>: speech timing runs as C (isr_timer_dsp as an interrupt replacement, isr_speech_start, isr_timer_start, isr_dsp_reset, isr_speech_stop, the frame waits, isr_set_volume, isr_volume_half, isr_par_ready and isr_par_hold; 100 routines hooked in all), and the C boot_loop_play, par_play, cmd_ev_set_volume and fw_par_ready call it directly; the polling loops wait for the next interrupt through the host, and frames leave the C timer interrupt through a host frame event; diff_lts (now comparing the timer and DSP-output state), diff_play (now also the three frame waits; the reply injected at the frame event, the mute two timer interrupts after it), diff_serial (new probes for parallel mode and the timer interrupt), golden_hybrid (48 files x 101 modes) and test_timing_invariance on both engines pass. Next: M4.6c, boot, the main loop and the banks.`

   Replace `<DATE>` with the date of your commit (`date +%F`).

3. **§6 step 3** (the paragraph containing `*(rev 15)* M4.6a: the host reads and writes every SFR`). Append at its end:

   ` *(rev 16)* M4.6b: the DSP port and the frame event are host calls too (dsp_write; frame_done at the point of $B306, which the hybrid host turns into machine_capture_frame). fw_wait takes the ROM address of the loop it stands for; FW_WAIT_INTERRUPT (a polling loop on a flag an interrupt sets: the hybrid host idles the emulated CPU as at a WIT, never the idle WIT at $C602, until an interrupt has been handled) and FW_WAIT_TIMER2/FW_WAIT_TIMER3 (isr_timer_start's two synchronisation loops, which the hybrid host runs masked with machine_run_loop) join FW_WAIT_UART_TX.`

- [ ] **Step 2: M4.6b spec status and As built**

In `docs/superpowers/specs/2026-10-09-m4-6b-speech-timing-design.md`, replace `**Status:** design, awaiting user review` with:

`**Status:** implemented (plan docs/superpowers/plans/2026-10-09-m4-6b-speech-timing.md)`

Then add a final section headed `## 6. As built`, after §5. Open it with the sentence `Where the ROM is more precise than, or differs from, the sections above. The ROM wins; ``ghidra740/`` is not edited.` (one pair of backquotes around `ghidra740/`). Then write one short factual bullet per difference between the spec and what was built. Gather them from:
- the "ROM facts checked" lists at the top of Tasks 1–5 of the plan, keeping only those that contradict or sharpen the spec;
- the code:
  - the thunk outputs as finally declared for the 11 new hooks (read the rows and comments in `src/engine/thunks.c`) and why;
  - the C layout (`isr_dsp.c`, `isr_serial.c`, `isr_addr.h`, the additions to `fw.h`/`fw.c`, `thunks.c`, `machine.h`/`machine.c`) and the static helpers;
  - the host calls (`dsp_write`, `frame_done`), the wait kinds and their `at` addresses, and the three machine calls as built;
  - each polling loop as built and where its wait sits;
  - `diff_lts`'s added comparisons and its `isr_timer_dsp` call count and run time;
  - `diff_play`'s injections as built and their counts;
  - `diff_serial`'s new probes as built and their counts;
  - the unit-test changes in the existing test files, and what `tests/test_fw_dsp.c` covers;
  - the branch probe's result (any branch pinned by a unit test instead).

Every bullet must be true of the committed code and the dumps. A claim about a test must say only what that test checks. Follow the style of §6 of `docs/superpowers/specs/2026-10-08-m4-6a-serial-design.md`. If nothing differs, add no section.

- [ ] **Step 3: README**

In `README.md`'s "Hybrid engine (milestone 4)" section, replace:

```
interrupts, the immediate commands and the reply output (M4.6a); the rest (speech timing, the DSP, boot and the
banks) still runs on the emulated CPU.
```

with:

```
interrupts, the immediate commands and the reply output (M4.6a), and speech timing: the timer interrupt that feeds
the DSP, the start and stop of DSP output, the frame waits, the volume and the parallel handshake (M4.6b); the
rest (boot, the main loop and the banks) still runs on the emulated CPU.
```

Then replace:

```
`build.bat test diff_play` compares the two that wait on the timer interrupt (par_play, boot_loop_play) with the
ROM at every frame swap, with interrupts running, and `build.bat test diff_serial` compares the serial paths no
corpus text reaches (reply transmission, parallel mode, the ring wraps) through probes injected into both copies.
```

with:

```
`build.bat test diff_play` compares the routines that wait on the timer interrupt (par_play, boot_loop_play and
the frame waits) with the ROM at every frame event, with interrupts running, and `build.bat test diff_serial`
compares the paths no corpus text reaches (reply transmission, parallel mode, the ring wraps, the timer
interrupt's rare paths) through probes injected into both copies.
```

- [ ] **Step 4: Run the full suite and commit**

Run: `cmd //c ".\build.bat test"` (in the background; it takes over 10 minutes)
Expected: `37 suite(s), 0 failure(s), 0 skip(s)`.

```bash
git add docs/superpowers/specs/2026-09-30-software-apollo2-design.md docs/superpowers/specs/2026-10-09-m4-6b-speech-timing-design.md README.md
git commit -m "Spec rev 16: milestone 4.6b done (speech timing and the DSP in C)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
