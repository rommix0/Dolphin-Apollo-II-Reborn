# M5a — The native host and engine (the firmware without the emulated CPU) — Design

**Status:** implemented (plan docs/superpowers/plans/2026-10-13-m5a-native-host.md)
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
- `build.bat`: the syntax check without `/Isrc` that guards `src/firmware` also covers `src/board` and the native files (`frame_list.c`, `native_host.c`, `native_engine.c`, `render_native.c`), each compiled with only the include paths it may use, so that none of them can include a `cpu740/` or `machine/` header. *(Corrected 2026-10-14 from the plan's proof.)* One include root serves them all: `build\noemu`, a copy of the `src\board` and `src\firmware` headers (and, from Task 3 on, the native engine's), made by `build.bat` before the check. The machine model does not change (parent spec §4); `machine.c` is not edited beyond its include path.

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

## 6. As built

Where the code is more precise than, or differs from, the sections above.

- **The files** (§3.1): `src/board/m50734.c` differs from the old `src/machine/m50734.c` only in its include line, and `machine.h` and `test_m50734.c` include `board/m50734.h`. `frame_list.h` also holds `ENGINE_FRAME_BYTES` (20) and a new `frame_list_add`, which `rom_engine.c`'s frame sink uses too; `rom_engine.h` includes it where the moved types were. `engine_status` lists `ENGINE_IDLE`, `ENGINE_FAULT` and `ENGINE_CYCLE_LIMIT` in `machine_status`'s order, which `suite_native` checks; `diff_main` compares a run's `machine_status` with the extra run's `want` (an `engine_status`) as numbers.
- **The build check** (§3.1), as built: the firmware's check without `/Isrc` is unchanged. A second `cl /Zs` compiles `src\board\*.c`, `frame_list.c`, `native_host.c`, `native_engine.c` and `render_native.c` in one call against the single include root `/Ibuild\noemu`, not one set of include paths per file. `build.bat` deletes and remakes `build\noemu` on every build from the headers of `src\board`, `src\firmware`, `src\firmware\data` and `src\synth`, the engine's `frame_list.h`, `native_host.h` and `native_engine.h`, and `render_native.h`. `src\board` is in the sources of the test binary and of every app.
- **The board's layout** (§3.2, `board.h`): the `board` struct mirrors the machine's fields under their names: `ram`, `sfr` (the `m50734`), `i`, `waiting`, `cycles`, `cycle_num`, `cycle_den`, `cycle_rem`, `fault`, `queue`, `q_head`, `q_tail`, `busy_level_known`, `busy_idle_level`, the sinks (`on_frame` and `user`, `on_raw` and `raw_user`), `raw_buf`, `raw_pos`, `dsp_reset_level`, `raw_frames`, `dsp_resets`, `raw_discarded`, `frames_produced`, `port_writes` and the unmodelled log (`unmodelled`, `unmodelled_count`, `unmodelled_dropped`). Its own fields are `idle_point`, `handler_due`, `handler_i` (the I flag the handler's RTI restores), `handler`, `irq_lines`, `fault_addr`, `rom` (the converted image by CPU address) and `fw` (the firmware the handlers run on). Its constants: `BOARD_RAM_SIZE` $4000, `BOARD_QUEUE_SIZE` 4096, `BOARD_MAX_UNMODELLED` 64, `BOARD_FRAME_BYTES` 20, `BOARD_PRODUCER_PTR` $2C, `BOARD_P2_BANK_MASK` $7C, `BOARD_P2_MAIN_BANK` $70, `BOARD_P2_DSP_RESET` $01, `BOARD_P0_BUSY` $20, `BOARD_ICR2_T3_ENABLE` $10, `BOARD_ICR2` $FE, `BOARD_UART_STATUS` $E7; the cycles `BOARD_WIT_CYCLES` 4, `BOARD_IRQ_CYCLES` 7, `BOARD_BBC_EXIT` 5, `BOARD_BBC_PASS` 7, `BOARD_RTS_CYCLES` 6; `BOARD_VEC_FIRST` $FFF4, `BOARD_VEC_RESET` $FFFE, `BOARD_EMPTY_VECTOR` $FFFF. `test_constants` checks the RAM size, the queue size, the log size, the frame size, the producer pointer, the bank mask and the main bank, ICR2, UART_STATUS and `BOARD_VEC_FIRST` against the machine's, the `m50734`'s and the firmware's names. `BOARD_P0_BUSY`, `BOARD_P2_DSP_RESET` and `BOARD_ICR2_T3_ENABLE` have the values of `machine.c`'s private `P0_BUSY`, `P2_DSP_RESET` and `ICR2_T3_ENABLE`, which no test can see, so no test compares them (§3.2 says one does). `test_cycles_match_cpu` checks the five cycle constants against `cpu740_step` on the loops' own bytes.
- **The board's faults and calls** (§3.2–§3.4): `board_fault` is `BOARD_FAULT_NONE`, `BOARD_FAULT_DME`, `BOARD_FAULT_ESCAPED` (a wait's step, or a bank entry, while P2 selects another bank than the main ROM: `pc_escaped`'s counterpart), `BOARD_FAULT_VECTOR` (an address with no C; `fault_addr` holds it), `BOARD_FAULT_INPUT_OVERFLOW` and `BOARD_FAULT_FIRMWARE` (`fault_addr` where). The calls are `board_init`, `board_set_outputs`, `board_set_raw_output`, `board_feed_serial`, `board_read_sfr`, `board_write_sfr`, `board_dsp_write`, `board_capture_frame`, `board_peek`, `board_main_bank`, `board_prepare`, `board_idle`, `board_vector`, `board_has_handler`, `board_call` and the four waits (below). `board_peek` reads $FF in $4000–$7FFF while P2.7 selects the lower window, where `machine_peek` reads the image's lower half. Only frame capture reads through it (its comment: the C never points a frame there), and no test covers that case.
- **A step** (§3.3), as built: each wait tests its bound (`cycles < end`) at the top of every iteration, then calls `board_prepare`, then (the interrupt and input waits) fails at once on a line that wakes the CPU with I set, then makes the step (`exec`). The step faults `BOARD_FAULT_ESCAPED` when P2 does not select the main ROM, since every point of a wait is at $8000 or above. A taken interrupt's handler runs at the step after its entry, through `board_call`, in 0 cycles and with no tick, and then I is set as its RTI pulls it back. Otherwise a waiting CPU with no line raised spends a WIT step (4); any raised line ends the WIT; a line with I clear is entered (7: the lowest line first, I saved and set, the handler read from $FFFC − 2 × line by `board_vector`); otherwise the wait's instruction runs. The tick follows: `cycles` += n, `acc = cycle_rem + n × cycle_num`, `cycle_rem = acc % cycle_den`, `m50734_tick(acc / cycle_den)`. A bound that passes between an entry and its handler therefore leaves I set and the handler unrun (`test_wait_interrupt_bound`: bound 41, 47 cycles spent, the request still pending).
- **The waits' charges** (§3.3), as built:
  - `board_wait_interrupt`: 4 per WIT step and 7 for the entry, the handler 0; it returns true once the handler's RTI is back, and fails with no cycle spent when a line is raised with I set.
  - `board_wait_input`: the same at the idle point, with delivery. At each idle the handler (`board_idle_fn`) is called before any step. A feed (`BOARD_FEED`) goes on with no step, and the next prepare delivers the byte. A stop (`BOARD_STOP`), or no handler, returns `BOARD_INPUT_STOPPED` with the board left waiting at the idle point. The other results are `BOARD_INPUT_HANDLED` and `BOARD_INPUT_FAILED`.
  - `board_wait_timer`: I is set for the loop and restored after it. Each pass reads ICR2 once through `board_read_sfr`: 7 cycles with the bit clear, 5 for the exit. The bound is tested before each pass, so the last pass may end past it and still succeed.
  - `board_wait_uart_tx`: one read of UART_STATUS (7 taken, 5 not), then the RTS (6), with interrupts live. `m50734_read` always returns UART_STATUS with bit 1 set, so the wait costs 11 cycles plus 7 for each interrupt entered on the way.
- **Dispatch and the vector table** (§3.4), as built: the six addresses are `fw_vec_int4_timerx`, `fw_vec_int3_step`, `fw_vec_int2_timers`, `fw_vec_int1_serial`, `fw_vec_int0` and `fw_vec_reset`. They are `const uint16_t` in `src/firmware/data/boot_data.c`, set from the address names `ROM_ISR_INT4_HOST_LINE`, `ROM_ISR_UNUSED`, `ROM_ISR_TIMER_DSP`, `ROM_ISR_INT1`, `ROM_ISR_UNUSED` and `ROM_BOOT_RESET`, and declared in `fw_data.h` with `FW_VECTORS` $FFF4. Step 14 of `fw_image_build` writes them low byte first up to $10000. `test_firmware_data` covers $FFF4–$FFFF (49,104 bytes compared with the ROM, 0 differ) and checks that lines 3 and 0 share a handler, that the reset is $A61C and that lines 4, 1 and 2 differ. The dispatch table is `board.c`'s static `k_dispatch`, seven rows as in §3.4's table; the reset goes through a static `reset_handler`, since `boot_reset` takes no registers. `board_call` first syncs the firmware's window and bank from the P2 pins, as a hybrid thunk does, then calls the C with the A and X it is given (Y 0). An interrupt's handler gets A = X = 0. With `b->fw` NULL every address is unmapped.
- **The native host** (§3.5), as built: `native_host` holds the board, the `fw`, the `fw_host`, the phase `deadline` (0: none), the input wait's idle handler and its user, and the last bank entry (`entered`, `entry`, `entry_a`, `entry_x`). `native_host_init` runs `fw_init` on the board's RAM with `fw_image_get()`'s image, sets `b->rom` and `b->fw`, and fails when the image does not build.
  - The waits' bounds: an engine run always has a deadline, as every host call of an all-C hybrid run is under the hybrid's phase deadline. So `FW_WAIT_UART_TX` gets at most `FW_ROM_CALL_BUDGET` and the cycles left, and the other waits the cycles left. A wait called once the deadline has passed stops at once, with no cycle spent.
  - A wait that does not end is a stop (`FW_WAIT_STOPPED`) only when the deadline is set and reached with no board fault (`wait_ended`); otherwise it is a failure. Without a deadline (the unit tests) every wait gets `FW_ROM_CALL_BUDGET`, and its end is a failure.
  - The window and the bank are read back from the P2 pins after every wait that started, a stopped input wait included. The wait's `at` is not used: each kind runs its own board wait.
  - `rom_call` and `rom_call_unhooked` are the same function. It returns `FW_CALL_FAILED` even after the phase deadline, where the hybrid's `phase_call` turns a call that fails at the deadline into a stop. No test makes a ROM call past the deadline.
- **The native engine** (§3.6), as built:
  - **Its state:** `native_engine` is the board, the host and two counts: `entries` (the entries run, the reset's included) and `unwinds` (the bank entries made). `native_engine_run_input` wipes it (`memset`), then calls `board_init` with `cfg`'s pins and scale. With `cfg` NULL it uses `native_default_config`: the pins $00 $00 $70 $00 $00 and scale 1/1, which `suite_native` checks against `machine_default_config`. A data image that does not build makes the run `ENGINE_FAULT` with `BOARD_FAULT_FIRMWARE`.
  - **The trap:** the reset vector is read through `board_vector($FFFE)`, and its C is called by `board_call` with the firmware's trap (`setjmp`) set. `setjmp` is called only there and in `thunks.c`. The 4611 pragma sits at the top of `native_engine.c`, under `#ifdef _MSC_VER`.
  - **After an entry's C ends:** a stop ends the run. An unwind with a recorded entry runs that entry's C afresh with its A and X: at once if it is the entry that was running; another entry only with no board fault, before the deadline and with the main ROM selected (else `BOARD_FAULT_ESCAPED`). Any other end, with no board fault already set, is `BOARD_FAULT_FIRMWARE`, at `f->fault_addr` if the C faulted, else at the entry's address. A C fault inside an interrupt handler longjmps to the same single trap.
  - **The status:** a board fault makes the run `ENGINE_FAULT`, even after a stop. Otherwise a stop by the idle handler is `ENGINE_IDLE`, and anything else `ENGINE_CYCLE_LIMIT`.
  - **The idle handler:** at the first idle it resets `frames_produced`, `raw_frames`, `dsp_resets` and `raw_discarded` and turns both sinks on (the produced one is already on with `include_boot`). It then queues the input and the CR (an overflow is `BOARD_FAULT_INPUT_OVERFLOW` and a stop) and sets the deadline `NATIVE_SPEAK_CYCLES` from there. The sinks and the handler are cleared when the run ends.
- **Render** (§3.7), as built: `render_native.h` holds `render_stream`, `render_engine`, `render_options`, `render_result`, `render_default_options`, `render_input` (the `@K1`/`@Rn` prefix), `render_synthesize` (the median pitch centre and the samples; nothing for no frame), `render_text_native` and `render_result_free`. `render.h` includes it and keeps only `render_text`, which builds its input and synthesizes through the same two functions. `render_text_native` allocates a fresh `native_engine` on the heap for each call and runs `native_engine_speak` with the default configuration and no boot frames, with the raw sink only for `RENDER_STREAM_RAW`. It synthesizes only at `ENGINE_IDLE`. `apollo2.c` is unchanged.
- **`diff_native`** (§4.3), as built: 69 runs: the 47 corpus texts, `diff_main`'s 19 extra runs and three Review Focus runs of its own (`rf2_longest`, `rf4_reentries`, `rf4_banks`).
  - **The comparison:** events are paired by their index in each run. An injection is made in both engines at the same idle or speech frame event, before the snapshot. The snapshot holds §4.3's list, plus the partial bytes dropped at DSP resets and whether the busy level is known yet. The `m50734` struct is compared with `memcmp`, and the first field that differs is named. `diff_main`'s exclusion of `isr_tick_div`…`isr_words_left` at idles has no counterpart here. At the end the two runs must have the same status, the same number of events, the same final snapshot, and as many bank entries on native (`unwinds`) as the hybrid's `hybrid_stats.unwinds`.
  - **The targets:** the extra runs' ROM targets are not checked here: the all-C hybrid runs none of those addresses as ROM, and `diff_main` keeps checking them. The three Review Focus runs check their number of bank entries (0, 2 and 1). No run reaches the NUL fallback, since every input is a C string, so §4.3's rule for one has nothing to apply to.
  - **The output** (about 11 s alone):

  ```
    corpus: 47 texts, 94 idles, 25167 frame events compared
    warm             idle, 2 idles, 101 frame events, 1 bank entries, 2019716 cycles
    warm_again       idle, 2 idles, 106 frame events, 2 bank entries, 2119793 cycles
    cold             idle, 2 idles, 101 frame events, 1 bank entries, 2019716 cycles
    dip_bank         idle, 2 idles, 41 frame events, 1 bank entries, 819879 cycles
    selftest_fail    idle, 2 idles, 365 frame events, 0 bank entries, 7299667 cycles
    status_sent      idle, 2 idles, 41 frame events, 0 bank entries, 819838 cycles
    status_waits     cycle limit, 1 idles, 100 frame events, 0 bank entries, 400819811 cycles
    parallel         cycle limit, 0 idles, 41 frame events, 0 bank entries, 100000001 cycles
    rf1_space        idle, 2 idles, 1565 frame events, 0 bank entries, 31301282 cycles
    rf1_full         idle, 2 idles, 1675 frame events, 0 bank entries, 33501254 cycles
    rf1_carry        idle, 2 idles, 1370 frame events, 0 bank entries, 27401065 cycles
    rf2_sentences    idle, 2 idles, 220 frame events, 0 bank entries, 4399355 cycles
    rf3_empty        idle, 2 idles, 41 frame events, 0 bank entries, 819816 cycles
    rf4_commands     idle, 2 idles, 52 frame events, 0 bank entries, 1039674 cycles
    rf5_during       idle, 2 idles, 161 frame events, 0 bank entries, 3219462 cycles
    baud_p31         idle, 2 idles, 115 frame events, 0 bank entries, 2299667 cycles
    baud_p32         idle, 2 idles, 115 frame events, 0 bank entries, 2299667 cycles
    baud_both        idle, 2 idles, 115 frame events, 0 bank entries, 2299667 cycles
    host_line_high   idle, 2 idles, 41 frame events, 0 bank entries, 819838 cycles
    rf2_longest      idle, 2 idles, 8654 frame events, 0 bank entries, 173088979 cycles
    rf4_reentries    idle, 2 idles, 101 frame events, 2 bank entries, 2019821 cycles
    rf4_banks        idle, 2 idles, 41 frame events, 1 bank entries, 819949 cycles
    69 runs, 135 idles, 40329 frame events compared in all
  ```

  0 mismatches. The printed bank entries and cycles are the native engine's.
- **The shared run list** (§4.3), as built: `tests/diff_runs.h` and `diff_runs.c` hold `diff_view` (RAM, the `m50734`, the queue with its head and tail), `inject_at` (`AT_IDLE`, `AT_SPEECH`), `extra_run` (its `want` an `engine_status`), `diff_extra_runs` and `diff_extra_run_count`, and the four injections (`inj_cold`, `inj_status_due`, `inj_status_line_high`, `inj_ring_two`) written against the view. `diff_main` builds the view from the machine, and its output is unchanged: `66 runs, 129 idles, 31533 frame events compared in all`.
- **`golden_native`** (§4.4): 48 goldens (the 47 corpus texts, and `boot` with the boot frames), 23,281 frames. Each is `ENGINE_IDLE` with no unmodelled access, and its frames are byte-identical to the golden. It reads no ROM and takes about a second.
- **`test_timing_invariance`** (§4.5) runs inside `suite_golden`, which needs the ROM. At the scales 1/4 and 3/1 the native engine gives `hello.frm`'s 105 frames and exactly the hybrid's cycles at each scale (11,678,411 and 973,321), which the test checks.
- **The render comparison** (§4.5): `suite_render` first renders "Hello world." natively with no ROM: 105 frames, 105 × `SYNTH_FRAME_SAMPLES` samples, no clipping and a peak above 1000. With the ROM it then checks:
  - those samples against `render_text`'s on the ROM engine;
  - every corpus text through `render_text` with the hybrid engine and through `render_text_native`: the same status, frames, clipping and samples, 2,328,100 samples over the 47 texts;
  - the same comparison for the raw stream of "Hello world." and for the Review Focus 5 inputs.
- **The no-ROM run** (§4.6 criterion 2): with `APOLLO2_ROM` unset and no ROM file in the worktree, the test binary prints `44 suite(s), 0 failure(s), 18 skip(s)` in about 2.4 s. `suite_board`, `suite_native` and `suite_golden_native` pass; `test_queue_fit` leaves out its ROM engine comparison; `suite_render` passes its native part before it skips.
- **`tests/test_board.c`** (§4.1), no ROM file (the converted image's vectors; the handlers run on a firmware whose host is the board's SFRs):
  - `test_constants` and `test_cycles_match_cpu` (above);
  - `test_init`: I set, 0 cycles, the scale (0 read as 1), the pins and the P2.0 level;
  - `test_wait_interrupt`: 10 WIT steps and an entry, 47 cycles; I restored; `isr_timer_dsp` cleared the request;
  - `test_wait_interrupt_bound`: bounds of 40 and 41;
  - `test_wait_interrupt_i_set`: a line with I set fails the wait with no cycle;
  - `test_lowest_line_first`: lines 0 and 3 raised, line 0's vector read; on a test image whose vectors are unmapped, a `BOARD_FAULT_VECTOR` at it;
  - `test_wait_timer`: both bits (three passes and the exit; two passes and the exit), the bound after three passes, I restored, timer 3's line not taken under the loop;
  - `test_wait_uart_tx`: with an interrupt (7 + 5 + 6), without one (5 + 6), and the bound before the RTS;
  - `test_delivery`: only at the idle point, none while the UART is full, the busy level of the first idle, and the ring holding 4,095 bytes;
  - `test_idle`: the five conditions;
  - `test_wait_input`: a stop, no handler, a byte delivered but never read until the bound, a handled interrupt in 47 cycles;
  - `test_dsp`: 27 bytes, one raw frame; a rising P2.0 edge dropping the 7 bytes; no edge while P2.0 stays high;
  - `test_capture`: the 20 bytes at the producer pointer;
  - `test_scale`: 1/3, 8 cycles, remainder 2;
  - `test_unmodelled`: once per address and direction, `UART_TX`'s read, the dropped count;
  - `test_faults`: the DME strobe, and a timer wait under another bank;
  - `test_dispatch`: every vector of the image has its C and so do both bank entries; `ROM_CMD_RUN_NEXT` has none; an unmapped address faults at itself; with `fw` NULL a call fails; under another bank the reset vector reads $FFFF, and with no image `board_peek` reads $FF.
- **`tests/test_native.c`** (§4.2), no ROM except the comparison in `test_queue_fit`:
  - `test_init`: the binding;
  - `test_ports`: the SFRs, a DSP reset edge, the window's read-modify-write of P2, a DSP byte and a frame event through the firmware's calls;
  - `test_bank_and_rom_calls`: another bank reading $FF; `fw_rom_call` ($C37E) and `fw_rom_call_unhooked` ($A893, with no cycle spent) each faulting at its address;
  - `test_cpu_calls`: `reset_cpu`, `set_interrupts` both ways, `fw_enter` recording $FFD4, A $31 and X $FF, I set, the C unwound;
  - `test_waits`: the timer and transmit waits' cycles; the window and bank read back; the interrupt wait's end at `FW_ROM_CALL_BUDGET`, a fault at its `at`; the input wait with no handler, a stop; an unknown kind, a fault;
  - `test_deadline`: a wait reaching the deadline is a stop at it; one called after it stops with no cycle; the transmit wait is cut by it; a line with I set is a fault before it;
  - `test_constants`: the budgets, the frame size, the statuses and the default configuration against the ROM engine's and the machine's;
  - `test_boot_and_speak`: "Hello world.": 41 boot frame events, then `hello.frm`'s 105 frames, two idles, one entry, the board left at the idle point; with `include_boot`, 146 frames;
  - `test_bank_entry`: "@=1 One. @=1 Two. Three.": 2 unwinds and 3 entries, and some frames;
  - `test_nul_bug`, `test_queue_fit` and `test_runs_in_one_process` (below).
- **The Review Focus** (the plan's header), as built and what each showed:
  1. `test_runs_in_one_process`: "Hello world." on an engine used before by both NUL faults and by `test_queue_fit`'s runs (one of them an overflow) gives the first run's 105 frames, cycle count and RAM, and so does an engine never used. It compares those three, not the `m50734` state or the queue.
  2. `rf2_longest` (1,500 characters: 34 times the 44-character words, then "dogs"; no punctuation): both engines end `ENGINE_IDLE` at the second idle, with 8,654 frame events and 173,088,979 cycles, the same at every event.
  3. `test_queue_fit`: 4,094 spaces and the CR give `ENGINE_IDLE`; 4,095 and the CR give `ENGINE_FAULT` with `BOARD_FAULT_INPUT_OVERFLOW`. With the ROM, the ROM engine ends with the same status and fault kind and the same frames.
  4. `rf4_reentries` ("@=1 One. @=5 Two. @=1 Three.") makes 2 bank entries and `rf4_banks` ("@=0 @=8 @=9 @=F @=G") makes 1, the same on both engines at every event.
  5. `suite_render`: voice b with `@R0` and with `@R7` ("Is this the second voice? It is."), and a Ctrl-X in the middle of "One two three four … five six seven.". Each gives the same status and samples on native as on the hybrid, and a nonzero sample count.

  The NUL bug (§4.2): `"@A"` NUL CR and `"@M5"` NUL CR end `ENGINE_FAULT`, `BOARD_FAULT_FIRMWARE` at $A893, with no frame (`test_nul_bug`).
