# M5a — The native host and engine (the firmware without the emulated CPU) — Design

**Status:** design, awaiting user review
**Parent specs:**
- `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 18): R1, R3, R5, §3, §4 (the machine model: serial delivery at the idle WIT, frame capture at `$B306`, the raw DSP stream), §6 step 3–4, §8 milestone 5.
- `docs/superpowers/specs/2026-10-13-m4-6c2-boot-main-loop-design.md`: the input wait and the idle handler, phase budgets, `reset_cpu`/`set_interrupts`, entries run afresh, `diff_main` (§3, §4.1, §6 "As built").
- `docs/superpowers/specs/2026-10-13-m4-6c1-banks-design.md`: `read_bank`, `enter`, the NUL bug and its fallback (§2.5, §3).
- `docs/superpowers/specs/2026-10-09-m4-6b-speech-timing-design.md`: the wait kinds and their ROM loops, `dsp_write`, `frame_done` (§3).
- `docs/superpowers/specs/2026-10-08-m4-6a-serial-design.md`: SFR access through the host, `FW_WAIT_UART_TX` (§3).

**User decisions:**
- (2026-10-13) **M5 is two plans.** **M5a** (this spec): the CPU-less board, the native host and engine, `diff_native`, the goldens on native; the emulator stays where it is. **M5b**: the emulator (`cpu740`, `machine`, the thunks, the `rom`/`hybrid` engines, `rom_id`) moves under `tests/` as the oracle, `apollo2` becomes native-only, `build.bat` checks that `src/` builds without the oracle, the formal no-ROM proof, the parent spec's last revision. M5b starts after M5a is merged.
- (2026-10-13) **The shipped CLI has no engine option.** After M5b `apollo2` always runs the native firmware: no `--engine`, `--rom` or `--rom-any`, and no ROM is loaded. The ROM serves the tests only. M5a therefore leaves `apollo2.exe` unchanged (no temporary `--engine native`); the native path is checked end to end through the render layer (§4.5). The test-side tools (`apollo2_frames`, `apollo2_coverage`, `cpu740_vectors`) keep their ROM options and their names in `build\` (`ghidra740/coverage.bat` and `validate.bat` call them; `ghidra740/` is not edited).
- (2026-10-13) **The native host is cycle-exact with the hybrid** (approach A). It reuses the `m50734` peripheral model and charges exactly the emulated cycles the all-C hybrid charges, so `diff_native` compares everything but the stack page, the cycle count included, with no exclusion. (Rejected: an event-level host with its own SFR model, time in timer ticks and the timing-dependent state excluded from the comparison.)
- (2026-10-13) **The NUL bug ends a native run as a firmware fault.** A handler address with no C handler (M4.6c1 spec §2.5: `@A` then NUL queues `$D300`, `@M5` then NUL `$002A`) ends the native run as `ENGINE_FAULT` with no frame, as the device crashes there too. The fault address is `$A893`, the ROM `cmd_run_next` the C dispatcher hands the unknown address to (the host sees only that call; reporting the queued address would need the shared C dispatcher to know its host). The hybrid keeps its ROM fallback. (Rejected: ignoring the command; fixing the scan bug.)

## 1. Goal

Run the decompiled firmware with no ROM and no emulated CPU: a native host that gives the C everything the emulator gives it today, and a native engine that reproduces every committed golden frame stream with no ROM file present. Prove it against the all-C hybrid, state for state and cycle for cycle, over every run `diff_main` makes.

## 2. What the hybrid still takes from the emulator

After M4.6c2 the C performs every firmware routine; the emulator supplies only the board and time:

| The hybrid uses | The native replacement |
|---|---|
| `m50734` behind the bus: SFR reads and writes, timers, UART, ports, ICR | the same `m50734`, behind the board (§3.2) |
| the CPU's interrupt entry, its WIT, the lines' priority, the I flag | the board's step (§3.3) and dispatch (§3.4) |
| the ROM loops `$C500`, `$B372`, `$B37F` run on the CPU | the board mirrors their instructions and cycles (§3.3) |
| serial delivery at the idle point, the idle test, the idle handler | the board's delivery and idle test, the engine's idle handler (§3.2, §3.6) |
| frame capture at the producer pointer, the raw DSP grouping | the board's (§3.2) |
| `machine_peek` for another bank: `$FF` (empty sockets) | `read_bank` returns `$FF` (§3.5) |
| a bank entry continuing at `$FFD0`/`$FFD4`, where the entry's hook fires | the engine calls the entry's C afresh (§3.6) |
| the ROM `cmd_run_next` for a NUL-bug address (`rom_call_unhooked`) | a failed call: a fault at `$A893` (§3.5) |
| the reset vector and the interrupt vectors in the ROM image | the vector table, converted (§3.4) |

## 3. Design

### 3.1 Files and the build check

- `src/board/m50734.c/.h`: moved from `src/machine/` (`git mv`), its content unchanged. The machine and `test_m50734` include it from there (their include line is the only change).
- `src/board/board.c/.h` (new): the CPU-less board (§3.2–§3.4).
- `src/engine/frame_list.c/.h` (new): `frame_list`, `frame_list_free`, `frame_list_hex` and `engine_input`, moved out of `rom_engine.h` (which includes the new header, so every user compiles unchanged), and `engine_status` (`ENGINE_IDLE`, `ENGINE_FAULT`, `ENGINE_CYCLE_LIMIT`).
- `src/engine/native_host.c/.h` (new): the `fw_host` on a board (§3.5).
- `src/engine/native_engine.c/.h` (new): the engine run (§3.6).
- `src/render/render_native.c/.h` (new): the native render entry (§3.7).
- `build.bat`: the syntax check without `/Isrc` that guards `src/firmware` also covers `src/board` and the native files (`frame_list.c`, `native_host.c`, `native_engine.c`, `render_native.c`), each compiled with only the include paths it may use, so that none of them can include a `cpu740/` or `machine/` header. *(Corrected 2026-10-14 from the plan's proof.)* One include root serves them all: `build
oemu`, a copy of the `srcoard` and `srcirmware` headers (and, from Task 3 on, the native engine's), made by `build.bat` before the check. The machine model does not change (parent spec §4); `machine.c` is not edited beyond its include path.

### 3.2 The board

A `board` holds what the machine holds outside the CPU, with the same rules (`machine.c`), so that the oracle and its native mirror can be compared field by field:
- RAM `$0000–$3FFF` (the firmware's `f->ram`; the SFR bytes `$DA–$FF` are not data), the `m50734` state, the I flag, the cycle count and the timer scale's remainder (`cycle_num`/`cycle_den`, test-only, as `machine_config`'s).
- SFR access: a read or write of `$DA–$FF` goes to `m50734_read`/`m50734_write`, an unmodelled register (or a read of write-only `UART_TX`) is logged as the bus logs it, a write that enables the data-memory strobe is the DME fault, a rising edge of P2.0 counts a DSP reset and drops the partial raw frame.
- The DSP port: each byte counted (`port_writes`) and grouped into raw frames of 20, delivered to the raw sink.
- Frame capture: the 20 bytes at the `APOLLO_PRODUCER_PTR` pointer (`$2C`), `frames_produced` counted, the frame sink called.
- The serial queue (`MACHINE_QUEUE_SIZE` bytes), the busy level of P0.5 recorded at the first idle, delivery of one byte at the idle point when the UART is empty and P0.5 is at that level (`maybe_deliver`).
- The idle test: at the idle point, no queued byte, the UART empty, timer 3 off (ICR2 bit 4) and no line raised (`idle`).

The board's names and constants are its own (`board.h` includes only `board/m50734.h` and the standard headers); where it mirrors a machine constant (`APOLLO_PRODUCER_PTR`, the queue size, the P0.5 and P2.0 masks) the value is the same and a test checks it.

### 3.3 Time

The all-C hybrid charges emulated cycles only in its waits; the C itself takes none. The board reproduces each charge with a step that mirrors `machine_step`:
1. **Prepare:** delivery (at the idle point only), then the IRQ lines from `m50734_irq_lines`.
2. **Act,** as `cpu740_step`: waiting with no line raised, 4 cycles; else, a line raised and I clear, the interrupt is taken (7 cycles, the lowest line first, I saved and set, the C handler run in 0 cycles, then I as RTI pulls it back); else the instruction the wait mirrors, at its cycles in `cpu740`'s table (taken branches included).
3. **Tick:** `m50734_tick` by the step's cycles through the scale and its remainder, as `machine_step` does.

The waits (`fw_wait_kind`), each the hybrid's own:
- `FW_WAIT_INTERRUPT`: a WIT at the C's point until one interrupt has been handled (`machine_wait_interrupt`); a line that wakes the CPU with I set fails it; no byte is delivered.
- `FW_WAIT_INPUT`: the same at the idle point (`machine_wait_input`), where bytes are delivered; whenever the idle test holds, the engine's idle handler feeds (the wait goes on) or stops (the run ends there).
- `FW_WAIT_TIMER2`, `FW_WAIT_TIMER3`: `BBC3 ICR2` / `BBC5 ICR2` with I set (`machine_run_loop`), pass by pass at the `BBC`'s cycles, until the bit reads set.
- `FW_WAIT_UART_TX`: `ser_tx_wait` `$C500` (`BBC1 UART_STATUS` with interrupts live, then its RTS, 6 cycles), as `host_rom_call` runs it.

Every step is bounded by the engine's phase deadline as the hybrid's waits are: a wait that reaches it stops the run (`FW_WAIT_STOPPED`; the engine returns `ENGINE_CYCLE_LIMIT`), at the same step as the hybrid. Interrupts are taken only inside waits, as in the hybrid, where the C never yields between them.

Not reproduced: the emulated stack. The return frames an interrupt pushes and the input wait's PHA land in `$0100–$01FF` on the machine; the C never reads that page, and `diff_native` does not compare it (as `diff_main`).

### 3.4 Dispatch from the converted vectors

The vector table `$FFF4–$FFFF` is converted into `src/firmware/data` as six named addresses (`vec_int4_timerx`, `vec_int3_step`, `vec_int2_timers`, `vec_int1_serial`, `vec_int0`, `vec_reset`), checked by `firmware_data` with the rest (49,092 → 49,104 bytes). The board reads a line's vector (`$FFFC - 2 × line`) from the image and calls the C function of that address:

| Address | C | Reached by |
|---|---|---|
| `$C3DF` | `isr_unused` | lines 0 and 3 |
| `$C2B4` | `isr_int1` | line 1 |
| `$B2D9` | `isr_timer_dsp` | line 2 |
| `$C3E0` | `isr_int4_host_line` | line 4 |
| `$A61C` | `boot_reset` | the reset vector |
| `$FFD0`, `$FFD4` | `bank_entry_cold`, `bank_entry_warm` | a bank entry (`fw_enter`) |

An address the table does not hold is a firmware fault at that address.

### 3.5 The native host

`native_host` binds an `fw` to a board:
- `read_sfr`, `write_sfr`: the board's SFR access (§3.2). `set_low_window`: the read-modify-write of P2 through it, as the hybrid host's.
- `dsp_write`, `frame_done`: the board's DSP port and frame capture.
- `wait`: §3.3.
- `read_bank`: `$FF` for every address (the option sockets are empty; option ROMs are out of scope, parent spec §9).
- `enter`: records the target, A and X, sets I; `fw_enter` then abandons the C (`f->unwound`).
- `reset_cpu`: sets I (SP, T, D, C and V have no native counterpart: nothing reads them). `set_interrupts`: the I flag.
- `rom_call`, `rom_call_unhooked`: `FW_CALL_FAILED`: there is no ROM. The C faults at the called address: `$A893` for the NUL fallback, the event vector for `cmd_play_event.c`'s unreachable call.

### 3.6 The native engine

`native_engine_run_input(const native_config *cfg, const engine_input *in, bool include_boot, frame_list *produced, frame_list *raw, const native_watch *watch)` returns an `engine_status` and, for `ENGINE_FAULT`, the fault and its address. *(Corrected 2026-10-14 from the plan's proof.)* The run takes the caller's `native_engine *e` (the board, the firmware context and the host) as its first argument, so nothing is static; the fault and its address are read from `e->b` after the run, and `e` is wiped at the start of each run. `native_config` holds the P0–P4 input pins and the timer scale (`native_default_config`: the machine's defaults). `native_engine_speak` is the same with the text and a CR.

1. The board is reset as `machine_init` resets the machine: RAM zeroed, `m50734_reset` with the pins, I set, 0 cycles. The phase deadline is `NATIVE_BOOT_CYCLES` (10⁸, the value of `ROM_ENGINE_BOOT_CYCLES`; `native_engine.h` defines its own budgets, as it includes no machine header, and a test checks them equal) from the start.
2. The reset vector's C, `boot_reset`, runs with the firmware's trap set; the whole firmware runs inside it.
3. The idle handler is the ROM engine's: the watch first; at the first idle the counters reset, the sinks on, the input and the CR queued, the deadline moved to `NATIVE_SPEAK_CYCLES` (4×10⁸, `ROM_ENGINE_SPEAK_CYCLES`'s value) from there; at the next idle the run stops. Input longer than the queue is the input-overflow fault.
4. A bank entry (`f->unwound` with a recorded target) makes the engine call the target's C afresh with the recorded A and X (§3.4), as the hybrid's entry hook fires at `$FFD0`/`$FFD4`; an entry that enters itself again runs afresh in turn (a warm `@=1`).
5. The run ends: a stopped input wait, `ENGINE_IDLE`; the phase deadline, `ENGINE_CYCLE_LIMIT`; a fault (a read outside the converted data, a failed ROM call, an unmapped vector, the DME strobe, input overflow), `ENGINE_FAULT`.

The watch (`on_frame`, `on_idle`) gets the board, which an injection may change (RAM, the input pins, the `m50734` state, the queue), as `engine_watch` gets the machine.

### 3.7 Render

`render_native.h` (no `machine/` or `cpu740/` header): `render_text_native(const char *text, const render_options *opt, render_result *out)` returns an `engine_status`. It runs the native engine and synthesizes as `render_text` does (the same `@K1`/`@Rn` prefix, the produced or raw stream, the median pitch, samples only at `ENGINE_IDLE`); the shared part is factored out so both paths run one code. `render_options.engine` is ignored there. `apollo2.exe` does not change in M5a.

## 4. Verification

### 4.1 `suite_board` (new, `tests/test_board.c`; no ROM)

Hand-built states: a WIT step of 4 cycles and an interrupt entry of 7; the lowest line first; I saved and restored around a handler; a line raised with I set failing `FW_WAIT_INTERRUPT`; each timer loop's cycles per pass and its exit; the `$C500` loop with an interrupt in the middle; delivery at the idle point only, after the previous byte was read, at the first idle's busy level; the idle test's five conditions; raw grouping and a DSP reset dropping a partial frame; frame capture at the producer pointer; the timer scale's remainder; the unmodelled log; the DME fault; the dispatch table, an unmapped vector faulting; the mirrored constants equal to the machine's.

### 4.2 `suite_native` (new, `tests/test_native.c`; no ROM)

Each host call; `read_bank` reading `$FF`; `rom_call` and `rom_call_unhooked` failing; a bank entry re-entering its C afresh; input overflow; `"@A" NUL CR` and `"@M5" NUL CR` ending as `ENGINE_FAULT` at `$A893` with no frame; a boot to the first idle and a short utterance to the second.

### 4.3 `suite_diff_native` (new, `tests/test_diff_native.c`; needs the ROM, skips without)

Every run `diff_main` makes (the 47 corpus texts and its extra runs with their configurations and injections), through the all-C hybrid first, whose watch records each event (an idle or a frame event) and a snapshot, then through the native engine, whose watch compares each of its events with the hybrid's of the same index. The snapshot: the event kind; all RAM but the SFR bytes and the stack page `$0100–$01FF`; the whole `m50734` state, no field excluded; the cycle count; the DSP outputs (port writes, the raw position and bytes, the reset count and level); `frames_produced` and `raw_frames`; the I flag; the serial queue (its bytes, head and tail) and the busy level. At a run's end both have the same status, the same number of events and the same final snapshot. Expected: 0 mismatches; every extra run reaches its target address (a hook on the hybrid side). *(Corrected 2026-10-14 from the plan's proof.)* No hook can see a target on the all-C hybrid: none of those addresses runs as ROM there (they lie inside C routines, or are entries `fw_install` already hooks). `diff_main`, which runs the same list, keeps checking the targets on its ROM side; `diff_native`'s own extra runs check what they reach through the number of bank entries made.

`diff_main`'s extra runs and their injections move to a file both tests share, the injections written against a neutral view (RAM, the `m50734` state, the queue) applied to the machine or the board. `diff_main`'s output does not change (66 runs, 129 idles, 31,533 frame events). A run that reaches the NUL fallback, if any, is compared up to it and its end pinned instead (the statuses differ by design).

### 4.4 `suite_golden_native` (new, `tests/test_golden_native.c`; no ROM)

Every golden in `tests/golden/` (48 files, the boot golden with `include_boot`) through the native engine: `ENGINE_IDLE` and byte-identical frames.

### 4.5 Changed suites

- `test_timing_invariance`: the native engine at the scaled timer configurations as well; the produced frames do not change.
- `suite_render`: every corpus text through `render_text_native` and through `render_text` with the hybrid engine: the same samples (needs the ROM for the hybrid side).
- `firmware_data`: the vector table (49,104 bytes).

### 4.6 Done criteria

1. The full suite passes with the ROM: 44 suites, 0 failures, 0 skips.
2. In a worktree with no ROM (no `APOLLO2_ROM`, no ROM file), `suite_board`, `suite_native` and `suite_golden_native` pass and every suite that needs the ROM skips (M5b makes this the formal proof).
3. `diff_native`: 0 mismatches over every `diff_main` run.
4. The goldens unchanged; `golden_hybrid` (48 × 132), `diff_main`, `diff_lts`, `diff_play` and `diff_serial` unchanged.
5. Parent spec rev 19 records M5a; this spec gets its As built section; the README describes the native engine.

### 4.7 Task outline (for the plan)

1. The board: `m50734` moved to `src/board`, `board.c` (§3.2–§3.4), the vector table converted, `suite_board`, the build check for `src/board`.
2. The native host (§3.5) and its tests in `suite_native`.
3. The native engine (§3.6), `frame_list.h` and `engine_status`, the NUL rule, the rest of `suite_native`, the build check for the native files.
4. `diff_native`: the shared run list and injections (`diff_main` unchanged), `suite_diff_native`.
5. `suite_golden_native`, `test_timing_invariance` on native, the render path and its comparison; the no-ROM worktree run.
6. Parent spec rev 19, this spec's As built, the README.

## 5. Out of scope

- The CLI (`apollo2` keeps its options until M5b), the move of the emulator under `tests/`, and the formal no-ROM proof (M5b).
- Option ROMs (the sockets read `$FF`), the parallel-port front end and serial replies to a real host (parent spec §9).
- Fixing the NUL bug or any other ROM quirk: the C stays the ROM's.
- Real-time streaming of the native output; the CLI renders, then plays.
