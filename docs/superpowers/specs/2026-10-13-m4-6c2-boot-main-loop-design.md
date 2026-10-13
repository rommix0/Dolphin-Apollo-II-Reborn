# M4.6c2 — Boot, the main loop and isr_idle in C (the C pulls its input) — Design

**Status:** design, awaiting user review
**Parent specs:**
- `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 17): §4 (the machine model, serial delivery at the idle WIT), §6 step 3–4, §8 milestones 4 and 5.
- `docs/superpowers/specs/2026-10-13-m4-6c1-banks-design.md`: the bank code, `fw_enter`/`enter`, the selected bank, `diff_lts` at the bank entry, `diff_serial`'s bank probes (§3, §4, §6 "As built").
- `docs/superpowers/specs/2026-10-09-m4-6b-speech-timing-design.md`: the wait seam with `at`, `FW_WAIT_INTERRUPT` and `machine_wait_interrupt`, polling loops that never spin in C, the branch probe.
- `docs/superpowers/specs/2026-10-08-m4-6a-serial-design.md`: SFR access through the host, the serial C (`ser_tx_service`, `ser_rx_put`, `ser_rx_next_page`), `diff_serial`.
- `docs/superpowers/specs/2026-10-07-m4-4b-frame-loop-design.md`: `boot_loop_play`, `waits` rows, `diff_play`, the bridge deadline.
- `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md`: hooks, thunks, `rom_call`, `diff_lts`, `golden_hybrid` (§8 "As built").

**User decisions:**
- (2026-10-13) M4.6c is two plans: M4.6c1 the banks and the command dispatcher (done), **M4.6c2** boot, the main loop and `isr_idle` (this spec).
- (2026-10-13) **The C pulls its input.** The engine registers an idle handler on the machine. When the machine has nothing left to do (the ROM engine's `machine_run` returning `MACHINE_IDLE`, or the C's input wait at `$C602` reaching the same condition), the handler feeds the next input or ends the run; ending abandons the C. One code path for both engines; it is what an M5 native host needs. (Rejected: running the C on its own stack as a coroutine; keeping the main loop in ROM.)
- (2026-10-13) **The stack page is not compared end to end.** With the main loop in C the machine stack no longer holds the ROM's return addresses, so interrupts push onto other bytes: the new end-to-end comparison excludes `$0100–$01FF` (`boot_stack`, the hardware stack page; nothing else lives there) and SP. The per-call comparisons are unchanged. (Rejected: the C mirroring the ROM's stack.)
- (2026-10-13) **The timer's per-frame counters are compared at frame events only.** `isr_tick_div` `$59`, `isr_word_ofs` `$5A` and `isr_words_left` `$5B` differ at input idles and at a run's end: after an utterance's last frame `isr_speech_stop` stops timer 3 a number of ticks later that depends on how long the main program takes, and the C takes no emulated cycles (true since M4.4b; `diff_play` already leaves them out at exit). `diff_main` compares them at every frame event (the same tick on both engines) and not at idles or a run's end. (Rejected: investigating a way for the C to pass emulated time there.)

## 1. Goal

Port boot, the main loop and `isr_idle` to C (21 hooked routines). After M4.6c2 every firmware instruction a hybrid run executes is C, except the ROM `cmd_run_next` for a NUL-bug handler address (M4.6c1 spec §2.5) and the host's wait loops (`ser_tx_wait` `$C500`, `isr_timer_start`'s `$B372` and `$B37F`); milestone 4 is complete.

Success: the goldens stay byte-identical with all 131 replacements and with each alone; `diff_lts`, `diff_play`, `diff_serial` and the new `diff_main` report 0 mismatches; `--engine hybrid` equals `--engine rom` on every corpus text.

## 2. The subsystem (all main ROM)

### 2.1 Ported to C

| Routine | Addr | Kind | What it does |
|---|---|---|---|
| `boot_reset` | `$A61C` | entry | `LDM` P3_DIR `$F9`, P23_FUNC 0, P3 `$78`; `SEI`; `LDX #$FF / TXS`; `CLT CLD CLC CLV`; A = X = Y = 0; ICR1, ICR2, ICR3 = 0; `$01–$C9` cleared (`STA 0,X`, X = `$C9` down to 1); `cmd_volume` = 0, `isr_set_volume`; `boot_read_dip`; `boot_init_ports`; ICR1 = 1; `boot_init_settings`; `cmd_apply_speaker`; `boot_selftest_sound`; in serial mode (DIP bit 0 clear) `boot_init_baud` with X = 0 and `boot_init_host_lines`; then `JMP boot_reentry_warm` |
| `boot_reentry_cold` | `$A665` | entry | `boot_read_dip`, `boot_init_settings`, then on into `boot_reentry_warm` |
| `boot_reentry_warm` | `$A66B` | entry | `ser_abort` = 0; `CLI`; `isr_par_hold`; `isr_par_ready`; `cmd_apply_speaker`; `bank_load_ramcode`, `bank_scan`, `bank_find_current`; then the main loop for ever: `boot_loop_text`, `pros_make_segments`, `boot_loop_play` |
| `bank_entry_cold` | `$FFD0` | entry | `SEI` (the bank signature `$78`) / `JMP boot_reentry_cold` |
| `bank_entry_warm` | `$FFD4` | entry | `JMP boot_reentry_warm` |
| `boot_loop_text` | `$A68D` | waits | `boot_get_clause`, `cmd_scan_clause`, `tn_convert_clause` |
| `boot_get_clause` | `$A697` | waits | waits for input and copies one clause into `boot_clause` (§2.5) |
| `isr_idle` | `$C5FE` | waits | `PHA`; the WIT at `$C602` unless DIP bit 0 (parallel mode); `PLA` |
| `boot_selftest_sound` | `$A79F` | waits | `cmd_text_ptr` = `boot_selftest_ok_cmds`, `boot_selftest`, `boot_selftest_fail_cmds` when it failed; `boot_queue_string`; `boot_loop_text`, `pros_make_segments`, `boot_loop_play` |
| `cmd_say_phonetic` | `$A876` | waits | `cmd_put_phonetic`, `pros_make_segments`, `boot_loop_play`; called only by `boot_init_baud` with X = 1, which no caller passes (unreachable) |
| `boot_read_dip` | `$ADE0` | helper | AD_CTRL = 0; `NOP`; `boot_dip` = P4 & `$0F` |
| `boot_init_ports` | `$AC22` | helper | P0 function, direction and latch, P1, P2 = `$70` and P2_DIR = `$FD` (the same in both DIP branches), P3_DIR, P23_FUNC, P3, by DIP bit 0 |
| `boot_init_settings` | `$ADEB` | helper | `boot_clear_input_state`, `boot_set_defaults`, `bank_ver_major`/`bank_ver_minor` from `$FFE0`/`$FFE1` |
| `boot_clear_input_state` | `$ABF0` | helper | the input and abort flags cleared; the ring pointers: input `$2100`, output `$3900` |
| `boot_set_defaults` | `$A742` | helper | the power-up @-settings, `cmd_char` = `ser_cmd_char` = `@`, `cmd_clear_formant`, `boot_load_prosody_defaults` |
| `boot_load_prosody_defaults` | `$A7F0` | helper | `boot_prosody_defaults` (26 bytes) to `pros_params` |
| `boot_init_baud` | `$AC7E` | helper | `tn_phon_len` = 0; UART_BAUD from P3.1/P3.2 (`$02`, `$BF`, `$2F`, `$05`); with X = 1 also speaks the rate through `cmd_say_phonetic` (unreachable) |
| `boot_init_host_lines` | `$AC68` | helper | UART_CTRL = `$D1`; P0_FUNC = `$4C`; by P0.2: `CLB5 P0` and `isr_host_line_high`, or `SEB5 P0` and `isr_host_line_low` |
| `boot_selftest` | `$C605` | helper | `boot_selftest_fail` = 1 when DIP bit 1 is set and P2.1 reads high, else 0 |
| `boot_queue_string` | `$C612` | helper | the zero-terminated string at `($46)` into the input ring through `ser_rx_put` (Y saved in `$78` around each call), between `isr_par_hold` and `isr_par_ready`, inside `PHP; SEI ... PLP`; A and Y kept |
| `isr_unused` | `$C3DF` | isr | the RTI of the INT3 (`$FFF6`) and INT0 (`$FFFC`) vectors; the board never raises those lines (unreachable) |

### 2.2 C calls C

- The entries call everything below them directly: `boot_reset` and the re-entries the boot helpers, `cmd_apply_speaker`, the self-test, the bank routines and the main loop (`boot_loop_text`, `pros_make_segments`, `boot_loop_play`); `bank_entry_cold`/`bank_entry_warm` call `boot_reentry_cold`/`boot_reentry_warm`.
- `boot_get_clause` calls the C `isr_idle`, `ser_tx_service`, `ser_rx_next_page`, `isr_par_ready` and `isr_par_hold`; `boot_queue_string` calls the C `ser_rx_put`; `boot_init_host_lines` the C `isr_host_line_high`/`low`.
- Every row also serves the `golden_hybrid` modes in which its callers stay ROM.

### 2.3 Hooks (21 new replacements; 131 in all, 132 `golden_hybrid` modes)

- **Entry rows** (`boot_reset`, `boot_reentry_cold`, `boot_reentry_warm`, `bank_entry_cold`, `bank_entry_warm`): they never return; their thunk runs under the engine's phase deadline (§3.2), and a bank entry from C (`fw_enter`) ends the thunk and lets the machine enter the next one at `$FFD0`/`$FFD4`. An entry whose C enters its own address again is run afresh by `run_frame` (otherwise the spin guard would fault, e.g. at `$FFD4`); `bank_entry_reset` `$FFD8` is never reached and is not hooked. *(Corrected 2026-10-13 from the plan's proof.)*
- **Waits rows** (`boot_loop_text`, `boot_get_clause`, `isr_idle`, `boot_selftest_sound`, `cmd_say_phonetic`), as M4.4b's frame-loop rows.
- **Helper rows** (the ten boot helpers), ordinary.
- **`isr_unused`**, an `isr` row (it declares no outputs; its thunk exits like RTI).
- Each row's outputs carry a comment naming what the ROM callers read after the call, checked in the dumps.

### 2.4 The data

Converted: `boot_selftest_ok_cmds` `$A7BE` (7 bytes) and `boot_selftest_fail_cmds` `$A7C5` (15) as strings; `boot_baud_text_300` `$ACCA` (14) and `boot_baud_text_1200` `$ACD8` (16) in the phonetic-text format of `boot_baud_text_19200` (all three baud texts were already converted, since M4.1: the new data is the self-test strings and `boot_prosody_defaults`, 48 bytes, `firmware_data` 49,092) *(Corrected 2026-10-13 from the plan's proof.)*; `boot_prosody_defaults` `$C65B` (26) as numbers with their meaning, one per `@R` letter. `firmware_data` checks them against the ROM.

### 2.5 `boot_get_clause` and its waits

- RAM writes as the ROM's: `boot_clause[0]` = space; the character carried over in `$1827` into `boot_clause[1]` (then 0); `ser_abort`, `tn_in_word`, `$6B` (`FW_UTT_CONTENT`), `boot_speaking` = 0; `SEB0 ser_rx_idle` when the ring is empty, before the loop and on each empty pass; the byte read at `(ser_rx_read)`, the pointer stepped and its page advanced through `ser_rx_next_page` at a page end; `SEB0 boot_speaking`.
- The clause ends as the ROM's: CR (a space stored instead), `.` `,` `:` `;` `!` `?`, X ≥ `$FA`, and from X ≥ `$DC` a space or `cmd_char` (which is carried in `$1827`, X stepped back); then a space is stored and `boot_clause_len` = X + 1. A set `ser_abort` restarts the routine.
- Interrupts: the unpaired `CLI` at `$A6D3` and `$A6FF` and `SEI` at `$A6E7` through `set_interrupts` (§3.3).
- **No spinning in C.** On an empty ring in serial mode the pass waits in `isr_idle` (`FW_WAIT_INPUT`, §3.1). A pass on which the ROM would loop with nothing changed waits through `FW_WAIT_INTERRUPT` at `$A6C2`: parallel mode (where `isr_idle` returns at once) and a status reply `ser_tx_service` could not send. A pass that changed state loops at once, as the ROM does. The plan's proof checks each case against the ROM (`diff_main`, §4.1).

### 2.6 Quirks kept

- Every RAM write, the scratch ones included: `$01–$C9` cleared by `boot_reset`, `$78` around `ser_rx_put`, `$1827`, `$6B`, the ring pointers, `pros_params`.
- Every SFR read and write in the ROM's order through the host, each `SEB`/`CLB` as a read then a write; `boot_read_dip`'s AD_CTRL write before the P4 read.
- The CPU state the ROM sets that later ROM code or the interrupts see (§3.3).
- The exit registers and flags the callers read.

## 3. Infrastructure

### 3.1 The input wait and the idle handler

- `machine_set_idle_handler(m, fn, user)` (machine API addition, no model change): `fn` is called when the machine has nothing left to do (the idle condition of `machine_run`: at the idle WIT, no queued input, no byte in the UART, timer 3 off, no interrupt line) and returns **feed** (it queued more input) or **stop**. The proof added `machine_idle` and `machine_wait_input` (the hybrid's `FW_WAIT_INPUT`, which parks the PC at `at`) and the engine's `phase_deadline`, all API additions; the host's wait returns `fw_wait_result` (`FW_WAIT_RETURNED`, `FW_WAIT_FAILED`, `FW_WAIT_STOPPED`). *(Corrected 2026-10-13 from the plan's proof.)*
- `rom_engine_run` becomes a loop: `machine_run`; on `MACHINE_IDLE` the handler; feed runs on, stop returns `MACHINE_IDLE`. The engine's own handler has two phases: at the first idle (the end of boot) it resets the counters, enables the sinks and queues the text and a CR, as `rom_engine_run` does today between its two runs; at the next idle it stops.
- `FW_WAIT_INPUT` (`at` = `$C602`): `isr_idle`'s WIT. The hybrid host parks the CPU in the WIT at `$C603` (the machine's idle point, where serial bytes are delivered) and runs until an interrupt has been handled. When the machine reaches the idle condition there, the host calls the handler: on feed it waits on and the byte is delivered as on the ROM; on stop the wait returns stopped, the firmware sets `f->stopped` and abandons the C like an unwound call (a longjmp to `f->trap`), and `run_frame` leaves the machine at the idle point, so the outer `machine_run` returns `MACHINE_IDLE`. Where the ROM `boot_get_clause` called the C `isr_idle`, that state is exactly the ROM's (A pushed, the WIT at `$C603`).
- **Delivery guard** (deferred from M4.6b): `machine_wait_interrupt` refuses the PCs `$C602`/`$C603`; serial bytes are delivered only at the input wait.

### 3.2 Phase budgets

The entry rows carry the run itself, so instead of the outermost thunk's `FW_ROM_CALL_SPEECH_BUDGET` they run under the engine's phase deadline: `ROM_ENGINE_BOOT_CYCLES` (10⁸) from the start, `ROM_ENGINE_SPEAK_CYCLES` (4·10⁸) from each feed, set by the idle handler. A wait that reaches it ends the run with `MACHINE_CYCLE_LIMIT`, as the ROM engine's `machine_run` does. Every other thunk keeps its 10⁹-cycle bound.

### 3.3 CPU state (two new host calls)

- `reset_cpu(ctx)`: `boot_reset`'s `$A625–$A62C` (`SEI`, `LDX #$FF / TXS`, `CLT`, `CLD`, `CLC`, `CLV`), so ROM code that runs later (the wait loops, the NUL fallback) and the interrupts see the ROM's flags and SP.
- `set_interrupts(ctx, enabled)`: the main loop's unpaired `CLI`/`SEI` (`$A66E`; `boot_get_clause` `$A6D3`, `$A6E7`, `$A6FF`), on which the next wait's interrupts depend. `PHP; SEI ... PLP` pairs stay invisible, as before.
- `fw_reset_cpu(f)` and `fw_set_interrupts(f, on, at)` call them; without a host, a fault at the ROM address of the instruction (`at` for `set_interrupts`). *(Corrected 2026-10-13 from the plan's proof.)*

### 3.4 Bank entries

`fw_enter(f, $FFD4 / $FFD0, a, x)` (M4.6c1) is unchanged: the host abandons the C chain up to the outermost thunk, sets SP `$FF` and the I flag and leaves the PC at the entry, which is now hooked: the C `bank_entry_warm` calls the C `boot_reentry_warm` (`bank_entry_cold` calls `boot_reentry_cold`). A warm `@=1` restarts the C main loop from a fresh thunk, with no `setjmp` in the firmware (the only pragma allowed for it is the one in `thunks.c`).

### 3.5 Files

- New: `src/firmware/boot.c/.h` (the entries, the boot helpers, the self-test, `cmd_say_phonetic`, `isr_unused`), `src/firmware/boot_input.c/.h` (`boot_loop_text`, `boot_get_clause`, `boot_queue_string`, `isr_idle`), `src/firmware/boot_addr.h`, `src/firmware/data/boot_data.c`, `tests/test_fw_boot.c`, `tests/test_diff_main.c`.
- Changed: `fw.h`/`fw.c` (`FW_WAIT_INPUT`, `f->stopped`, the two CPU helpers), `thunks.c`/`thunks.h` (the input wait and the idle handler, the CPU calls, entry rows and the phase deadline, the 21 rows), `machine.c`/`machine.h` (the idle handler, the delivery guard), `rom_engine.c`/`.h`, `hybrid_engine.c`, the data builder, the tests that stub these routines.

## 4. Verification

### 4.1 `diff_main` (new)

- The ROM engine and the all-C hybrid on every corpus text and on extra texts: `@=1` (warm re-entry); a cold re-entry injected into both engines (`bank_ver_minor` altered before `@=1`); DIP bit 1 with `@=5`; a query whose status reply waits on the host line; the self-test failing (DIP bit 1 and the machine's P2.1 input high), if the plan's proof shows it reachable that way.
- Compared at every input idle (the end of boot, the end of each utterance) and at every frame event: all RAM except the stack page `$0100–$01FF` (and, at idles and a run's end only, `$59–$5B`: the user decision above), the SFR state, the DSP outputs, the frame counts and the I flag; the numbers of idles and frame events must be equal.
- Parallel-mode boot (DIP bit 0): compared at the frame events of the boot announcement; neither engine idles in that mode, so the run ends at the boot budget and the comparison stops there.
- This is the comparison of the entry rows and the waits rows.

### 4.2 `diff_lts`

The ten helper rows join `k_every_call` (most run once per boot, so they are compared once per corpus text). The waits and entry rows are excluded, as M4.4b's frame-loop rows are; `diff_main` covers them. The 5-minute gate stays.

### 4.3 `diff_serial`

The probes deferred from M4.6c1's final review: the alternate table's no-match path in a DIP run, P2 bank bits `$78` under DIP bit 1, and `bank_safe_char` with A = `$20` compared ROM against C. Every existing probe compares the C.

### 4.4 Unreachable paths, pinned by unit tests

`boot_init_baud` with X = 1 (the spoken rate through `cmd_say_phonetic`, each of the three texts); `isr_unused`; the self-test's fail path (and `diff_main` compares it too if it is reachable, §4.1).

### 4.5 Unit tests (new suite `tests/test_fw_boot.c`, `suite_fw_boot`; with `diff_main`, 40 suites) *(Corrected 2026-10-13 from the plan's proof.)*

- Each ported routine against a stub host that logs SFR access, `reset_cpu`, `set_interrupts`, the waits and `enter`: RAM writes, SFR order, exit registers, every clause end of `boot_get_clause`.
- The input wait: feed, stop and the deadline, with `f->stopped` and the trap.
- `test_machine_hooks` and the engine tests: the idle handler on both engines, the delivery guard, stop leaving the machine at the idle point, the phase deadline ending with `MACHINE_CYCLE_LIMIT`.

### 4.6 `golden_hybrid`, `test_timing_invariance`, the CLI, the branch probe

- `golden_hybrid`: 48 files x 132 modes, the goldens byte-identical.
- `test_timing_invariance` passes on both engines.
- `--engine hybrid` equals `--engine rom` on every corpus text (always with `-o FILE`).
- The branch probe over every new routine (a throwaway tool): each conditional branch seen both ways on the corpus, under `diff_main`'s extra runs or a probe; a one-sided branch pinned by a named unit test. The same tool records every ROM PC a hybrid corpus run executes outside the C: only `$C500`, `$B372`, `$B37F` and the NUL fallback's path may appear.

### 4.7 Done criteria

1. The 21 routines are C, the 21 hooks installed; the C calls C as in §2.2.
2. `firmware_data` covers the new data; 0 bytes differ.
3. `diff_main`, `diff_lts`, `diff_play` and `diff_serial` report 0 mismatches; every probe and extra run reached.
4. The branch probe is recorded, one-sided branches pinned, and a hybrid corpus run executes no ROM firmware code but the host's wait loops.
5. `golden_hybrid` (48 x 132), `test_timing_invariance` on both engines and the CLI check pass.
6. The new and updated unit tests pass; the full suite is green (40 suites).
7. The parent spec rev 18 (M4.6c2 done, milestone 4 complete), this spec's As built section and the README are updated.

### 4.8 Task outline (for the plan)

1. Infrastructure: the idle handler and the engines' loop (both engines; the goldens unchanged with the ROM boot), `FW_WAIT_INPUT` and stop, the phase deadline, `reset_cpu`/`set_interrupts`, the delivery guard, `suite_fw_boot` wired in with real tests.
2. The data, `firmware_data` green.
3. `diff_main` and the deferred `diff_serial` probes, proved ROM against ROM.
4. The boot helpers in C, with hooks and unit tests.
5. The main loop's input side (`isr_idle`, `boot_get_clause`, `boot_loop_text`, `boot_queue_string`) and the self-test, with hooks.
6. The entries (`boot_reset`, the re-entries, the bank entries), `cmd_say_phonetic` and `isr_unused`; the branch probe; the CLI check.
7. Docs: the parent spec rev 18, As built, the README.

## 5. Out of scope

- M5: the native host (its input source, its frame sink, its behaviour for the NUL bug's unknown addresses), and removing the emulator from the build.
- Option-ROM images: every bank but the main ROM is an empty socket.
- `pros_pitch_code_dither` `$C131`: no call, jump or pointer reaches it.
