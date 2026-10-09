# M4.6a — The serial code in C (receive, immediate commands, transmit, the serial ISRs) — Design

**Status:** design, awaiting user review
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
