# M4.6a — The serial code in C (receive, immediate commands, transmit, the serial ISRs) — Design

**Status:** implemented (plan docs/superpowers/plans/2026-10-08-m4-6a-serial.md)
**Parent specs:**
- `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 14): §1.3 (the peripherals), §4 (the machine model and its serial delivery rule), §6 step 3–4, §8 milestones 4 and 5.
- `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md`: hooks, thunks, `rom_call`, `diff_lts` (§3–§5, §8 "As built").
- `docs/superpowers/specs/2026-10-03-m4-2-command-parser-design.md`: `cmd_match_table`, the handlers behind a C dispatcher (the model for the immediate-command handlers), the reply handlers (`cmd_reply.c`).
- `docs/superpowers/specs/2026-10-07-m4-4b-frame-loop-design.md`: `read_sfr` (§6 "As built": $E7 only), `diff_play` and its injections, the bridge deadline.
- `docs/superpowers/specs/2026-10-08-m4-5-frame-builder-design.md`: the latest conventions and "As built".

**User decisions (2026-10-08):**
- M4.6 is three plans: **M4.6a** serial (this spec), **M4.6b** speech timing and DSP (the timer ISR, `isr_speech_*`, `isr_timer_start`, `isr_dsp_reset`, the frame waits, `isr_set_volume`, `isr_volume_half`, `isr_par_ready`/`isr_par_hold`), **M4.6c** boot, the main loop and the banks (`boot_*`, `isr_idle`, `cmd_run_next`, `bank_*` with the RAM-resident bank code, the remaining data).
- **Hardware waits are a semantic host seam.** C calls a host primitive per kind of wait (`fw_host.wait`): here `FW_WAIT_UART_TX`; M4.6b and M4.6c add the frame and input waits. The hybrid host implements each by running the original ROM loop on the emulated CPU (cycle-exact, interrupts live, no machine changes); the M5 native host implements them natively (the UART always ready, the frame flag as "emit a frame", WIT as "next input"). Those few ROM bytes are run as host code, not as firmware.
- **Paths no corpus text can reach are compared with the ROM through injected differential probes** (`diff_serial`, §4.2), with no change to the machine model: the reply transmission (host line P0.2 and INT4), the parallel-port mode (DIP bit 0), the `_INT1` latch path and the ring page wraps. Unit tests pin anything left.
- Hooks: every ported routine, as in M4.0–M4.5 (a mismatch then points at one routine).

## 1. Goal

When M4.6a is done, the firmware's serial code runs as C in the `hybrid` engine:
- the receive interrupt `isr_int1` takes each host byte (UART, `_INT1` latch or parallel port) to `ser_rx_byte`, which mutes, starts or continues an immediate command (`cmd_imm_match` and its two handlers), or queues the byte in the input ring;
- the host-line interrupt `isr_int4_host_line` follows the host's handshake line P0.2;
- the reply output (`ser_tx_put`, `ser_tx_hex`, `ser_tx_records`) fills the output ring, and `ser_tx_service` sends it when the host line is high, through `ser_tx_get` and `ser_tx_send`, waiting for the UART through the host;
- the C callers (`cmd_reply.c`, `par_play`) call the C directly;
- every routine matches its ROM original at every call `diff_lts` samples, and under every probe of `diff_serial`.

## 2. The subsystem (all main ROM)

### 2.1 Ported to C (18 hooked routines, 704 bytes; 2 handlers, 50 bytes)

| C file (new) | Routine | ROM | Bytes |
|---|---|---|---|
| `src/firmware/isr_serial.c` | `isr_int1` (INT1: serial UART path `ser_int1_serial`, `_INT1` latch path `ser_int1_latch`, parallel-port path) | $C2B4–$C37D | 202 |
| | `isr_int4_host_line` (INT4) | $C3E0–$C3EA | 11 |
| | `isr_host_line_high` | $C3EB–$C3F3 | 9 |
| | `isr_host_line_low` | $C3F4–$C3F9 | 6 |
| `src/firmware/ser.c` | `ser_rx_byte` | $C37E–$C3CB | 78 |
| | `ser_rx_put` | $C3CC–$C3DE | 19 |
| | `ser_rx_flush_cmd` | $C3FA–$C412 | 25 |
| | `ser_rx_next_page` | $C5EC–$C5F4 | 9 |
| | `ser_tx_get` | $C4B5–$C4DC | 40 |
| | `ser_tx_put` | $C4DD–$C4FF | 35 |
| | `ser_tx_send` | $C504–$C50C | 9 |
| | `ser_hex_digits` | $C53A–$C559 | 32 |
| | `ser_hex_digit` | $C55A–$C560 | 7 |
| | `ser_tx_hex` | $C571–$C580 | 16 |
| | `ser_tx_service` | $C581–$C5A3 | 35 |
| | `ser_tx_records` | $C5A4–$C5D5 | 50 |
| | `ser_tx_next_page` | $C5F5–$C5FD | 9 |
| `src/firmware/cmd_imm.c` | `cmd_imm_match` | $C413–$C482 | 112 |
| | `cmd_imm_index_add` (handler, not hooked) | $C483–$C494 | 18 |
| | `cmd_imm_index_query` (handler, not hooked) | $C495–$C4B4 | 32 |

- Each `.c` has a `.h`. Every routine is `void name(fw *f, fw_regs *r)`; one static helper per ROM block, commented with its address range; the quirks kept (§2.6). *(Corrected 2026-10-09 from the plan's proof.)* `ser_hex_digits` takes a third parameter, `uint8_t pair[2]`: the two bytes it reads and writes on its caller's stack frame ($0105 + (S − 2) and $0106 + (S − 2)).
- New ROM addresses go in a new `src/firmware/ser_addr.h` (the `ROM_SER_*` and `ROM_CMD_IMM_*` constants move there from `cmd_addr.h` and `par_addr.h`).
- **The immediate-command handlers** are entered only by `JMP (cmd_imm_vector)` at $C46A and leave by `JMP cmd_imm_done` ($C46C), not RTS. As with the M4.2 deferred handlers, `cmd_imm_match` dispatches them with a C `switch` on the 16-bit handler address read from the matched `cmd_imm_table` entry (`ROM_CMD_IMM_INDEX_ADD`, `ROM_CMD_IMM_INDEX_QUERY`); an unknown address is a fault (`fw_fault`). They are not hooked.
- **Not ported here:** `ser_tx_wait` $C500 (the wait seam, §3.3); `isr_par_ready` and `isr_par_hold` (M4.6b: `par_play`'s frame waits pass emulated time through ROM calls to `isr_par_ready`, so they move together with the frame-wait seam).

### 2.2 C calls C
- `isr_int1` → `ser_rx_byte`, `ser_tx_get`; `isr_int4_host_line` → `isr_host_line_high`/`_low`.
- `ser_rx_byte` → `cmd_imm_match`, `ser_rx_put`; `cmd_imm_match` → `cmd_match_table` (C since M4.2), `ser_rx_flush_cmd`, the two handlers; `ser_rx_flush_cmd` → `ser_rx_put` → `ser_rx_next_page`.
- `ser_tx_service` → `ser_tx_get`, `ser_tx_send` and the UART wait; `ser_tx_hex` → `ser_hex_digits` → `ser_hex_digit`, and `ser_tx_put`; `ser_tx_records` → `ser_tx_hex`, `ser_tx_put`; `ser_tx_get`/`ser_tx_put` → `ser_tx_next_page`.
- The C callers that use `fw_rom_call` today call the C directly: `cmd_reply.c` (`ser_tx_put`, `ser_tx_hex`, `ser_tx_records`, through its `ser_call` helper) and `par_play` (`ser_tx_service`).
- The ROM callers that remain are M4.6c code (`boot_get_clause`: `ser_tx_service`, `ser_rx_next_page`; `boot_queue_string`: `ser_rx_put`); they reach the C through the hooks.

### 2.3 The ISRs
- `isr_int1` and `isr_int4_host_line` are entered by the CPU through the vectors `vec_int1_serial` $FFFA and `vec_int4_timerx` $FFF4 (the interrupt has pushed PC and P and set I). They end with `RTI`.
- `isr_int1`'s serial path pushes A, X and Y, enables nested interrupts (`PHP`, `CLI`) around `ser_rx_byte`, then pulls them back; its early parallel paths (`RTI` at $C2BF, $C2D5, $C2E8) touch no register. Every path leaves A, X and Y as they came in.
- In the C the nested interrupt cannot happen (the C runs in no emulated time and cannot be interrupted); that is a difference of timing only, which the produced frames do not depend on (`test_timing_invariance`).

### 2.4 Hooks (18 new replacements; 89 in all, 90 `golden_hybrid` modes)
- Every routine of §2.1 except the two handlers gets a row in the replacement table. None is marked `waits`. `isr_int1` and `isr_int4_host_line` are marked `isr` (§3.2).
- Every thunk declares its outputs with a comment saying what the ROM callers read after the call, checked in the dumps; the two ISR thunks declare none (§3.2).

### 2.5 SFRs the C touches
P0 ($F6: bits 0, 2, 3, 4, 5), P1 ($F3), P1_DIR ($F4), P1_LATCH ($F2), ICR1 ($FF: bits 0, 1), ICR3 ($FD: bits 6, 7 and whole-byte writes), UART_CTRL ($E6: bit 4), UART_STATUS ($E7), UART_RX ($E5), UART_TX ($E4). Each is read and written through the host (§3.1) in the ROM's order, including the read-modify-write of every `SEB`/`CLB` and the `LDA UART_RX` / `STA UART_RX` pair at $C35D.

### 2.6 Quirks kept
- Every RAM write, scratch included: the ring pointers and `ser_rx_idle`/`ser_rx_idle_saved`, `ser_cmd_mode`, `ser_cmd_ptr`, `ser_cmd_table_ofs`, the five `ser_cmd_buf` bytes, `cmd_index_count`, `cmd_index_reset`, `isr_par_state`, `ser_status_due`, `ser_tx_pending`, `ser_tx_count`; `cmd_imm_match` saving and restoring zero page $44–$49 and $4C (on the stack in the ROM; the C restores the same values, and the stack bytes are dead stack).
- `ser_tx_put` reads its byte back from the stack (`TSX`, `LDA $0103,X`), which is A on entry; A, X and Y preserved.
- `ser_tx_get` returns NAK ($15) when nothing is pending; `ser_tx_count` is tested before its `DEY` (a count already 0 clears `ser_tx_pending` without the decrement).
- The ring page wraps: input $39 → $21 (`ser_rx_next_page`), output $3A → $39 (`ser_tx_next_page`, the one page $3900).
- `cmd_imm_index_query`: the reply `I`, `cmd_index_count` in hex, then `T` unless `ser_rx_idle_saved` is set and `boot_speaking` is clear (then `M`).
- The flag effects of `PHP`/`SEI`/`PLP` pairs: the C does not model P except where a caller reads it (none does, §2.4).

## 3. Infrastructure

### 3.1 SFR access from C
- `fw_host.read_sfr` covers every SFR ($DA–$FF); the hybrid host performs a bus read with a CPU read's side effects (it answers only UART_STATUS today, M4.4b). New `fw_host.write_sfr(ctx, addr, value)` performs a bus write.
- `fw.h` gains `fw_wr_sfr8`, `fw_sfr_seb(f, addr, bit)` and `fw_sfr_clb(f, addr, bit)` (a read, then a write of the changed value, as the CPU's `SEB`/`CLB` do: `cpu740.c`, `M_SEB`/`M_CLB`), and `fw_sfr_bit(f, addr, bit)` for `BBS`/`BBC`, next to the existing `fw_sfr8`. All fault (`fw_fault`) without a host or outside $DA–$FF.

### 3.2 Interrupt handlers as replacements
- `fw_replacement` gains a field `isr`: the thunk exits like `RTI` (pull P, then PC) instead of `RTS`. New `machine_return_interrupt(machine *m)` next to `machine_return`.
- An `isr` thunk declares no register outputs: the C leaves A, X and Y as entered, and P comes back from the stack.
- The hook fires at the ISR's first instruction, after the CPU has entered the interrupt (`machine_hook`'s conditions hold: the CPU is not waiting, and with I set no other interrupt is about to be taken).

### 3.3 The wait seam
- `fw_host.wait(ctx, kind)` with `enum fw_wait { FW_WAIT_UART_TX }` (M4.6b and M4.6c add their kinds); `fw_wait(f, kind)` in `fw.h` faults without a host.
- The hybrid host implements `FW_WAIT_UART_TX` by running `ser_tx_wait` $C500 (`BBC1 UART_STATUS, $C500` / `RTS`) through `machine_rom_call`, with interrupts live and the ROM's cycles. $C500 is never hooked: it is host code. The bridge deadline bounds it like any ROM call.
- The C `ser_tx_send` and `ser_tx_service` call `fw_wait(f, FW_WAIT_UART_TX)` where the ROM has `JSR ser_tx_wait`.

### 3.4 Machine API additions (no model change)
`machine_return_interrupt`, and an accessor (or a documented struct comparison) for the SFR model state that the differential tests compare (§4.1). The machine model of the parent spec §4 is unchanged.

## 4. Verification

### 4.1 `diff_lts`
- `DIFF_MAX` 96. It checks the 87 replacements not marked `waits` (69 + 18).
- **ISR replacements:** at an `isr` hook it clones the machine; the ROM copy runs to its `RTI` (`machine_finish_routine`: the SP rises above its value at the hook), the C copy runs the thunk.
- **The SFR model state is compared** in addition to RAM and the registers: the port latches and directions, ICR1–ICR3, the UART control, status, receive byte, transmit count and last byte (the plan names the exact fields of `m50734`). *(Corrected 2026-10-09 from the plan's proof.)* Of ICR2 only the enable bits ($15) are compared: timer 3 sets its request bit every 200 cycles while speaking and the C copy runs (almost) no cycles, so comparing the whole byte reports a timing difference, not a logic one (216,954 such mismatches on the existing replacements when tried).
- Routines the corpus never enters (§4.4) print 0 calls; each is covered by `diff_serial` instead. "Every routine exercised" holds across `diff_lts`, `diff_play` and `diff_serial` together. *(Corrected 2026-10-09 from the plan's proof.)* The six routines no corpus text enters (`ser_tx_get`, `ser_tx_send`, `ser_tx_service`, `ser_tx_next_page`, `isr_int4_host_line`, `isr_host_line_high`) are listed once, in `tests/diff_state.c` (`diff_probed`): `diff_lts` and `golden_hybrid`'s M4.0 check that C ran in each single-routine mode exempt exactly these six, and `diff_serial` checks that each is the routine of one of its probes.

### 4.2 `diff_serial` (new suite, `tests/test_diff_serial.c`)
- **Synthetic calls from real states.** It runs the corpus (and the boot run) on the ROM engine. At every `ser_tx_put` entry (the output ring holds a real reply) and every `isr_int1` entry, it clones the machine and makes each probe in two copies: copy A runs the ROM routine, copy B the C thunk. An ISR probe is entered as the CPU enters an interrupt (the request and enable bits set, the CPU takes it); a subroutine probe through `machine_rom_call`. Both copies run with interrupts held; the comparison is that of §4.1.
- **Probes** (each an injected change made to both copies, then a routine):
  - host line P0.2 high (`port_in`), then `ser_tx_service`: sends through `ser_tx_get`, `ser_tx_send` and the wait; with the ring holding one byte and several;
  - DIP bit 0 set (`boot_dip`), with P0 bits 0 and 4 and `isr_par_state` in each combination and P1_LATCH = 5, ≥ 7 and < 5, then `isr_int1`: every parallel path;
  - ICR1 bit 1 set, then `isr_int1` in serial mode: the `_INT1` latch path;
  - P0.2 high and low with INT4 requested, then `isr_int4_host_line`;
  - the input ring's write pointer at $38FF, then `ser_rx_put` (the $39 → $21 wrap); the output ring's pointers at $39FF, then `ser_tx_put` and `ser_tx_get` (the $3A → $39 wrap);
  - an immediate command whose `ser_cmd_ptr` sits at a page end, then `cmd_imm_match` with a partial match ($C458).
- An observer hook on copy A records whether each probe reached its target branch; every probe must have reached it at least once.
- **Self-check first:** before any C exists the suite runs with the ROM routine in both copies, which proves the probes and the comparison whatever the replacement table holds.
- Prints per probe the calls compared, each > 0, with 0 mismatches. *(Corrected 2026-10-09 from the plan's proof.)* Beyond the probes listed above, the plan adds `int1_mute_01`, `imm_query_text`/`_m`/`_t`, `host_line_high`, `tx_send` and `tx_next_page` (25 probes in all). The `isr_int4_host_line` probes are entered from a real `isr_int1` entry state with ICR3 bits 7 and 6 set and the PC taken from $FFF4 (the CPU pushes the same PC and P for every interrupt line; only the vector differs); the parallel-mode probes also set P0_DIR to $08, as `boot_init_ports` does in parallel mode.

### 4.3 `diff_play`, `golden_hybrid`, `test_timing_invariance`
- `diff_play` unchanged, 0 mismatches (its reply-due injection now runs the C `ser_tx_service`, still with P0.2 low).
- `golden_hybrid`: every file (48) × 90 modes byte for byte; every golden unchanged; no new corpus text.
- `test_timing_invariance` passes on both engines.

### 4.4 Coverage
Probed while designing with `apollo2_coverage` on the 47 corpus texts: 154 of the 423 instructions of these routines never run. Never entered: `ser_tx_get`, `ser_tx_send`, `ser_tx_wait`, `ser_tx_service`, `ser_tx_next_page`, `isr_int4_host_line`, `isr_host_line_high`. Never run inside entered routines: `isr_int1`'s `_INT1` latch and parallel paths (74 instructions), `ser_rx_next_page`'s wrap, `cmd_imm_match`'s page carry ($C458), `ser_tx_put`'s page step, `cmd_imm_index_query`'s `M` ($C4A7). The reason: the machine never raises the host line P0.2 (so no reply is transmitted), never runs in parallel mode and never raises the `_INT1` pin (parent spec §4). `diff_serial`'s probes reach each of them; the plan confirms with a branch probe that every conditional branch of the 20 routines is seen both ways on the corpus or under a probe, and pins any remainder with a named unit test. *(Corrected 2026-10-09 from the plan's proof.)* Three branches cannot be reached by any injection both copies share (the machine sets no UART error bits; P0.2 cannot change between `ser_tx_service`'s two tests; the output ring's page is always $39): $C350, $C59B taken and $C5F8 not taken are pinned by unit tests.

### 4.5 Unit tests (new suite `tests/test_fw_ser.c`, `suite_fw_ser`)
- One or more cases per routine; registers and RAM checked byte for byte against values worked out from the dumps. The stub host keeps an ordered log of SFR reads and writes (address, value) and of `wait` calls, so the order of every access is pinned.
- Cases: the mute bytes $01 and $18; the command character; `cmd_imm_match` with a full match (each handler), a partial match and no match; `@I+` after a mute (the reset) and not; `@I?` replying `T` and `M`; `ser_tx_get` empty (NAK) and with the count reaching 0; both ring wraps; `ser_tx_service` with P0.2 low and high, the ring emptying and not; every path of `isr_int1` (serial clean byte, serial with UART error bits, the `_INT1` latch, each parallel branch); `isr_int4_host_line` both ways; `fw_wr_sfr8`/`fw_sfr_seb`/`fw_sfr_clb` faulting without a host and outside $DA–$FF; `fw_wait` faulting without a host.
- `tests/test_fw_cmd.c` (the reply handlers) and `tests/test_fw_par.c` (`par_play`'s `ser_tx_service`): the stubs that record `ROM_SER_*` calls change to check that the C ran.

### 4.6 Converted data (`firmware_data` 48,496 → 48,512 bytes)

| ROM | Table | Form |
|---|---|---|
| $C561–$C570 | `ser_hex_chars` (16) | The string `"0123456789ABCDEF"` (without its terminating 0, which the ROM does not have), in a new `src/firmware/data/ser_data.c`. |

`tests/test_firmware_data.c`'s `k_rom` gains `{ 0xC561, 0xC571 }`.

### 4.7 Done criteria
1. `diff_lts`, `diff_play` and `diff_serial` pass with 0 mismatches; every new routine is compared more than 0 times by at least one of them; every `diff_serial` probe reaches its target branch.
2. `golden_hybrid` reproduces every file (48) × 90 modes byte for byte; every golden is unchanged.
3. Every conditional branch of the 20 routines is seen both ways on the corpus or under a `diff_serial` probe, or is pinned by a unit test the plan names.
4. `test_timing_invariance` passes on both engines.
5. `firmware_data` passes at 48,512 converted bytes.
6. `apollo2 --engine hybrid` equals `--engine rom` on every corpus text (`-o FILE` only).
7. No `ROM_SER_*` or `ROM_CMD_IMM_*` `fw_rom_call` remains in `src/firmware/`; `src/firmware/` still builds without `machine/` or `cpu740/` headers.
8. `build.bat test` is green (36 suites).
9. The docs are updated: the parent spec gets rev 15 (milestone 4.6a done; the SFR, ISR and wait host seams recorded in §6 step 3); this spec's status, with "As built" if anything differs; the README's hybrid section lists the serial code.

### 4.8 Task outline (for the plan)
1. Infrastructure: `read_sfr` for every SFR, `write_sfr`, the `fw_sfr*` helpers; `fw_host.wait` with the hybrid host running $C500; the `isr` field and `machine_return_interrupt`; `diff_lts` comparing the SFR state and handling `isr` rows, `DIFF_MAX` 96; `ser_hex_chars` converted; the suite `suite_fw_ser` wired in.
2. `diff_serial`, proved ROM against ROM with every probe before any C exists.
3. The transmit side: `ser_tx_put`, `ser_tx_get`, `ser_tx_send`, `ser_tx_service`, `ser_tx_hex`, `ser_hex_digits`, `ser_hex_digit`, `ser_tx_records`, `ser_tx_next_page`, with hooks and unit tests; `cmd_reply.c` and `par_play` call them directly.
4. The receive side: `ser_rx_byte`, `ser_rx_put`, `ser_rx_flush_cmd`, `ser_rx_next_page`, `cmd_imm_match` and its two handlers, with hooks and unit tests.
5. The ISRs: `isr_int1`, `isr_int4_host_line`, `isr_host_line_high`, `isr_host_line_low`, with hooks and unit tests; the branch probe recorded.
6. Parent spec rev 15, this spec's status, "As built" and the README.

## 5. Out of scope
- M4.6b (the timer ISR, speech start and stop, the DSP, the frame waits, `isr_set_volume`, `isr_volume_half`, `isr_par_ready`, `isr_par_hold`) and M4.6c (boot, the main loop, `isr_idle`, `cmd_run_next`, the banks).
- Any change to the machine model (parent spec §4): the host line, parallel mode and the `_INT1` pin stay unmodelled; `diff_serial` injects their effects instead.
- New corpus texts or goldens.

## 6. As built

Where the ROM is more precise than, or differs from, the sections above. The ROM wins; `ghidra740/` is not edited.

- **The SFR host calls** (§3.1): the hybrid `host_read_sfr` and `host_write_sfr` (`thunks.c`) are one bus read and one bus write, with the model's side effects (`m50734_read`: a read of UART_RX $E5 clears receive full; P1_LATCH $F2 reads `port_in[1]`; UART_STATUS $E7 is $02 or $03, its error bits 2–4 never set). A port reads its pins, `(port_in & ~dir) | (latch & dir)`, so `SEB`/`CLB` on P0 copies an input pin's level into the latch, as the CPU's do: `test_host_sfr` sets P0.5 with P0.2 an input at 1 and finds the latch $24. `fw_wr_sfr8`, `fw_sfr_seb` and `fw_sfr_clb` fault below $DA, without a host or without the host call they need (`write_sfr`; `SEB`/`CLB` also `read_sfr`), and then neither read nor write (`test_sfr_faults`); `fw_sfr_bit` is one `fw_sfr8`, whose faults it keeps.
- **The wait seam** (§3.3): the enum is `fw_wait_kind` (`FW_WAIT_UART_TX` = 0; its ROM loop `FW_WAIT_UART_TX_LOOP` $C500), and `fw_host.wait` returns whether the wait ended. `fw_wait` faults at $C500, whatever the kind, without a host, without `wait` or when the wait fails (`test_wait`). The hybrid `host_wait` fails for any other kind and runs $C500 through `host_rom_call` with the machine's own A, X and Y and `FW_ROM_CALL_BUDGET`. The machine's transmitter is always ready, so a wait takes 11 cycles (`BBC1` not taken 5, `RTS` 6) and leaves A, X, Y, S and the PC as they were; once the bridge deadline has passed it fails without running an instruction (`test_host_wait`).
- **The interrupt exit** (§3.2): `run_thunk` became `run_frame(m, b, regs, isr)`, which ends a thunk that did not fault or unwind with `machine_return_interrupt` for an `isr` row and `machine_return` otherwise (`run_thunk` passes false; `ISR_THUNK` makes the two interrupt hooks with true). A row is `{ name, addr, outputs, thunk, waits, isr }`. `machine_return_interrupt` pulls P (with B set), then the PC's low and high bytes, with no + 1, as `M_RTI` does (`test_return_interrupt` compares it with an executed `RTI`); unlike `M_RTI` it does not fault when the pulled P has T set.
- **The thunks** declare A, X and Y for the 16 subroutine rows and copy the C's registers back (`ser_regs`, or `ser_hex_digits_regs`); none declares a flag, and each has a comment on what its callers read next. The two `isr` rows declare none; their thunks still set A, X and Y from the C's `r`, which comes back as it went (the C makes no ROM call). `ser_hex_digits`' thunk reads its `pair` at $0105 + (S − 2) and $0106 + (S − 2), S being the SP at the hook (its caller's last two pushes: in `ser_tx_hex` the third and the second), and writes the digits back there.
- **The C layout**:
  - `ser.c`: the receive path, `ser_rx_byte` (`ser_rx_mute` $C391 and `ser_rx_cmd_start` $C3AC inline, each commented), `ser_rx_put`, `ser_rx_flush_cmd`, `ser_rx_next_page`; the reply output, `ser_tx_next_page`, `ser_tx_get`, `ser_tx_put`, `ser_tx_send`, `ser_hex_digit`, `ser_hex_digits`, `ser_tx_hex`, `ser_tx_service`, `ser_tx_records`. Its one static helper is `tx_step` ($C4C0–$C4C9, $C4EA–$C4F3: `INC ptr` / `BNE` / `LDY ptr+1` / `JSR ser_tx_next_page` / `STY ptr+1`), shared by `ser_tx_get` and `ser_tx_put`; the other blocks stay inline with their addresses in comments (§2.1 asked for a helper per block). `ser.h` names the SFRs (`SFR_UART_TX` … `SFR_ICR1`), `P0_HOST_LINE`, `P0_BUSY`, `SER_NAK`, `SER_CTRL_A` and `SER_CTRL_X`; `par_play.c` uses its `SFR_UART_STATUS` instead of its own;
  - `cmd_imm.c`: `cmd_imm_match`, with the zero page it keeps as `k_kept` in push order (`cmd_table_ofs` $4C, `cmd_table_ptr` $44–$45, `cmd_text_ptr` $46–$47, `cmd_pat_ptr` $48–$49), written back in reverse at the end of every path, as `cmd_imm_done` does; `cmd_imm_vector` ($4A–$4B), written on a full match, is not among them, in the ROM either. Then the two handlers; no helper;
  - `isr_serial.c`: `isr_int1` with the helpers `int1_serial` ($C33C–$C37D, the `_INT1` latch path $C367 inside it) and `int1_parallel` ($C2BA–$C33B), `isr_int4_host_line`, `isr_host_line_high`, `isr_host_line_low`, and its own names for the bits and codes (`ICR1_INT1_EN`/`_REQ`, `ICR3_INT4_EN`/`_REQ`, `ICR3_HOST_HIGH` $40, `ICR3_HOST_LOW` $48, `P0_ACK`, `P0_READY`, `P0_STROBE`, `PAR_REPLY_CODE` 5, `PAR_DATA_MIN` 7, `UART_ERRORS` $1C, `UART_RX_IRQ`);
  - `ser_addr.h`: the 20 entry addresses with their callers (the scan of every `JSR`/`JMP` operand and the vectors); `ROM_CMD_IMM_INDEX_ADD`/`_QUERY` and `ROM_SER_TX_PUT`/`_HEX`/`_RECORDS` moved there from `cmd_addr.h`, `ROM_SER_TX_SERVICE` from `par_addr.h`. `cmd_reply.c`'s `ser_call` takes the C routine instead of an address;
  - `data/ser_data.c`: `fw_ser_hex_chars[17]` = `"0123456789ABCDEF"`; `fw_data.c`'s step 11 puts the 16 characters (not C's 0) at `SER_HEX_CHARS` $C561 and fails the image build unless they end at `SER_HEX_CHARS_END` $C571;
  - `fw_ops.h`: `fw_seb`/`fw_clb`, `SEB`/`CLB` on a RAM byte;
  - `machine.h`/`machine.c`: `machine_return_interrupt` only. The SFR state is compared as fields of `machine.sfr` (`m50734`), listed in `tests/diff_state.h`, with no accessor (§3.4's documented struct comparison).
- **Quirks pinned** (sharper than §2.6):
  - `ser_tx_put` stores the A it is given: after its three pushes and `TSX`, `LDA $0103,X` reads $0100 + S, its first push, for any entry SP S of 3 or more (lower, $0103 + X would leave the stack page);
  - `ser_tx_get`'s `LDM #0,ser_tx_pending` ($C4D7) writes the whole byte, on the nothing-pending path too: $02 (bit 0 clear) becomes 0 (`test_tx_get`). `ser_tx_service` tests `ser_tx_pending` as a whole byte (`LDA` / `STA ser_status_due` / `BNE`);
  - `ser_tx_records`' second `LDX ser_reply_size` ($C5C5) is dead (X is reloaded at $C5BA or pulled back at $C5D2–$C5D3): the C leaves it out. `ser_reply_count` ends at 0.
- **`diff_lts`** (§4.1): the comparison moved to `diff_same` (`tests/diff_state.c`), shared with `diff_serial`: the exit PC and S, the declared outputs, the SFR fields `port_latch[0-3]`, `port_dir[0-3]`, `icr1`, `icr3`, `icr2 & $15`, `uart_ctrl`, `uart_rx`, `uart_rx_full` (UART_STATUS bit 0), `uart_tx_count` and `uart_tx_last` (before, the P2 latch alone), and all RAM except $DA–$FF and the dead stack. The other `m50734` fields are not compared: `port_in` (the board's input levels), `icr2`'s request bits, the timers, `p0_func`, `p23_func`, `uart_baud`, `ad_ctrl` and the store of unmodelled SFRs. For an `isr` row, which declares no outputs, A, X, Y and P are not compared, only the exit PC and S (the thunk sets A, X and Y back as they came); its unwound test counts a call whose exit SP is more than 3 above the hook's for an `isr` row (`RTI` pulls P too), 2 otherwise. It prints 87 lines; the 18 new ones: `isr_int1` 2,748, `ser_rx_byte` 2,748, `ser_rx_put` 3,038, `isr_host_line_low` 49 (called by `boot_init_host_lines`), `ser_rx_flush_cmd` 110, `cmd_imm_match` 115, `ser_tx_put` 124, `ser_hex_digits` 8, `ser_hex_digit` 16, `ser_tx_hex` 8, `ser_tx_records` 1, `ser_rx_next_page` 2 calls checked; the six of `diff_probed` 0; 0 mismatches.
- **`diff_serial`** (§4.2), as built:
  - **The points**: every entry of `isr_int1` ($C2B4) and of `ser_tx_put` ($C4DD) on the ROM engine. At each, for each of the point's probes, the machine is cloned twice, both copies with interrupts held, and the injection is made to each. Copy A runs the ROM routine; copy B gets a bridge with the probe's routine alone installed (`fw_install`), so its C calls the C of its callees directly. An `isr` probe runs to the `RTI` (`machine_finish_routine`), a subroutine probe through `machine_rom_call` with the copy's A, X, Y and P. `diff_same` compares A, X and Y for every probe, the `isr` ones included (unlike `diff_lts`).
  - **The probes** (25): at `isr_int1`, 10 of `isr_int1` (`int1_latch`: ICR1 bit 1 and the received byte on P1_LATCH, `port_in[1]`; `int1_mute_01`: `uart_rx` = $01; the eight `par_*`: `boot_dip` bit 0, P0_DIR $08, the host's P0.0 and P0.4 in `port_in[0]`, `isr_par_state` bit 0, `port_in[1]` = the received byte, 5 or 3, `ser_tx_pending` bit 0 for the two `par_data` probes), 2 of `isr_int4_host_line` (ICR3 bits 7 and 6, P0.2 in `port_in[0]`, the PC from $FFF4), `host_line_high`, `rx_wrap` (`ser_rx_write` $38FF, A = the received byte) and the four `imm_*` of `cmd_imm_match` (command mode, "I" in `ser_cmd_buf`, `ser_cmd_table_ofs` 0, `ser_cmd_ptr` $20FF with A = 'I', or $2021 with A = '?' and `ser_rx_idle_saved` bit 0 clear (`imm_query_text`), or set with `boot_speaking` bit 0 clear (`imm_query_m`) or set (`imm_query_t`)); at `ser_tx_put`, 7: `svc_high`, `svc_low`, `svc_dip` (P0.2 in `port_in[0]`, `boot_dip` bit 0), `tx_wrap` (`ser_tx_write` $39FF), `tx_get_wrap` (`ser_tx_read` $39FF, `ser_tx_pending` bit 0), `tx_send`, `tx_next_page` (Y = $39).
  - **The target**: a hook at the destination on copy A counts the call when the machine's last executed instruction (`history`) is the branch; for `host_line_high` and `tx_send`, when the routine is entered.
  - **Each run** first makes the self-check pass, the ROM in both copies, on `queries` only (23 calls of each `isr_int1` probe, 9 of each `ser_tx_put` probe), then the 47 texts and the boot run with the C in copy B: 2,649 calls of each `isr_int1` probe and 124 of each `ser_tx_put` probe, every target reached at every call, 0 mismatches. `report` requires each probe's routine to have a replacement (after the self-check), and the suite checks that every `diff_probed` routine is the routine of a probe.
- **The branch probe** (§4.4, §4.7 criterion 3): the plan's scratch tool (not committed: a machine hook on each of the 36 conditional branches of the 20 routines evaluates it before it runs, on the corpus machine and on every probe's copy A, ROM only) found **36 branches, 33 seen both ways, 3 one-sided**, each pinned by a unit test: $C350 `BNE` taken (`test_int1_uart_error`: UART_STATUS $07, no read of the byte, the same tail), $C59B `BBC2` taken (`test_tx_service`: P0.2 read high, high, then low, so no `SEB5`), $C5F8 `BEQ` not taken (`test_tx_next_page`: Y $20 → $21). The corpus alone sees 10 of the 36 both ways; the probes give the rest, `int1_mute_01` and the `imm_query_*` probes being there for $C380, $C4A1 and $C4A4, which the corpus takes one way only (a first run without `imm_query_text` left $C4A1 one-sided).
- **`tests/test_fw_ser.c`** (§4.5): its stub host logs every SFR read and write and every wait in order (`check_log`, `check_seq`), can make a level change between two reads (`g_script`), and fails and counts every ROM call. Beyond §4.5's cases: `test_sfr_helpers` (`SEB` of a bit already set still writes; `fw_sfr_bit` reads once per test), `test_data` (the 16 digits through `fw_rom8`; $C571 faults), `test_tx_send` (the wait before the `UART_TX` write), `test_hex` ("60" for 6, "5A" for $A5), `test_tx_records` (3 records of 2 bytes), `test_rx_put` ($21FF → $2200 and the wrap), `test_rx_flush_cmd`, `test_imm_partial_and_none` (with the page carry from $20FF to $2100) and `test_imm_unknown_handler` (a patched copy of the converted image whose "I+" handler is $1234: a fault at $1234 after `cmd_imm_vector` is written, zero page still restored). The `isr_int1` cases start from the SFRs as the boot leaves them in serial mode (`int1_setup`: ICR1 $01, ICR3 $48, UART_CTRL $D1; the parallel cases then set `boot_dip`, P0 and P1_LATCH) and check the accesses in order (`check_seq`; the parallel data case with a reply pending checks its P1 write, `isr_par_state` 3 and the queued byte instead).
- **The callers' unit tests** (§4.5):
  - `tests/test_fw_cmd.c`: the mock lost its `ROM_SER_TX_*` answers, so such a call fails; `setup` puts both output ring pointers at $3900 (as `boot_clear_input_state` does); `check_call` became `check_reply`: the reply's bytes from $3900, `ser_tx_write` after them, `ser_tx_read` $3900, `ser_tx_count` = their number, `ser_tx_pending` 1 and no ROM call. `test_echo` "!", `test_query_known` "A60", "$24" and "W30", `test_query_unknown` nothing queued, `test_list_languages` 104 bytes ("L70E0", 7 bank ids of 14 characters, CR) and `ser_reply_count` 0 (was 7). `test_query_returned_regs` is gone: with no `ser_tx_put` to mock, the C keeps X and Y, which `test_tx_put` and `diff_lts` check;
  - `tests/test_fw_par.c`: `ROM_SER_TX_SERVICE` left the mock (a call to it now fails) and `mock_read_sfr` counts the P0 reads, P0 reading 0 (the host line low): `test_play_segment` 4, one per pass (its checked order has 8 calls, was 12; `g_ncalls` 16, was 20), `test_play_abort` 0.
