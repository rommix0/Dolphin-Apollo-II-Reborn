# M4.6c1 — The banks and the command dispatcher in C (the RAM-resident bank code, cmd_run_next, @=) — Design

**Status:** design, awaiting user review
**Parent specs:**
- `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 16): §1.2 (the memory map, P2 bank switching), §4 (the machine model), §6 step 3–4, §8 milestones 4 and 5.
- `docs/superpowers/specs/2026-10-09-m4-6b-speech-timing-design.md`: the wait seam with `at`, P2 writes and the low window (§3.4), `k_every_call`, the branch probe (§6 "As built").
- `docs/superpowers/specs/2026-10-08-m4-6a-serial-design.md`: SFR access through the host, `diff_serial` and its probes (§3, §4, §6 "As built").
- `docs/superpowers/specs/2026-10-03-m4-2-command-parser-design.md`: the C dispatcher and its ROM fallback (§2.4), the latent NUL bug (§2.6), the non-local exit when the ROM resets the stack (§3).
- `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md`: hooks, thunks, `rom_call`, `diff_lts` (§8 "As built").

**User decisions:**
- (2026-10-08) M4.6 is three plans: M4.6a serial (done), M4.6b speech timing and the DSP (done), M4.6c boot, the main loop and the banks.
- (2026-10-13) **M4.6c is two plans:** **M4.6c1** the banks and the command dispatcher (this spec), with the ROM boot still in charge; **M4.6c2** boot, the main loop, `isr_idle`, the input the C pulls at its idle wait, the C warm re-entry and an end-to-end differential test.
- (2026-10-13) **The RAM-code copy is written exactly.** `bank_load_ramcode`'s 511 bytes at `$0200–$03FE` are converted ROM data: the two bank tables as named P2 values, the code as a readable instruction listing. RAM stays identical to the ROM run; no comparison changes.
- (2026-10-13) **Hooks in RAM code.** A machine API addition lets a hook fire at a RAM-code address; the eight RAM-resident bank routines become ordinary replacements, replaced from every caller, compared at every call, one `golden_hybrid` mode each.
- (2026-10-13) **The NUL bug: the hybrid follows the ROM.** For a handler address with no C handler the C leaves RAM untouched and runs the ROM `cmd_run_next` through a ROM call that skips its own hook. Only this path still runs ROM firmware code; M5's native host decides its own behaviour and records the divergence then.
- (2026-10-13) **Host seams for other banks (approach A).** The firmware tracks the selected bank from its P2 writes; a ROM read under another bank goes to a host call `read_bank`; leaving through a bank entry is a host call `enter`. Option-ROM images are out of scope: every bank but the main ROM is an empty socket.

## 1. Goal

Port the bank code and the command dispatcher to C: `bank_load_ramcode`, the `@=` handler `bank_cmd_select`, the eight routines that run from the RAM copy, and `cmd_run_next` (12 routines, 347 bytes; 10 hooked). After M4.6c1 the only ROM firmware code a hybrid run executes is boot, the main loop and `isr_idle` (M4.6c2), the two bank-entry stubs `$FFD0`/`$FFD4` (which jump into boot), the ROM `cmd_run_next` for an unknown handler address (the NUL bug), and the host's wait loops (`ser_tx_wait` `$C500`, the two timer-sync loops of `isr_timer_start`).

Success: the goldens stay byte-identical with all 110 replacements and with each alone; `diff_lts` and `diff_serial` report 0 mismatches; `--engine hybrid` equals `--engine rom` on every corpus text.

## 2. The subsystem

### 2.1 Ported to C

| Routine | Addr | Bytes | What it does |
|---|---|---|---|
| `bank_load_ramcode` | `$A600` | 28 | saves Y, X and P2 in `$78`, `$76`, `$75`; copies `bank_ramcode_image` `$ADFE–$AFFC` to `$0200–$03FE` (256 passes of two bytes: `$ADFE,X` → `$0200,X` and `$AEFD,X` → `$02FF,X`, so `$02FF` is written twice with the same byte); A kept |
| `bank_cmd_select` | `$ABAF` | 15 | the `@=` handler: `cmd_hex_value`, `bank_load_ramcode`, `bank_enter` (returns only when the bank is absent), `bank_find_current`, `JMP cmd_run_done` |
| `cmd_run_next` | `$A893` | 45 | with `cmd_run_done` `$A8BD`: X pushed; `cmd_vector` from `cmd_handlers[cmd_next]`; `cmd_next` += 1; `cmd_next_pos` from `cmd_positions`; X = `cmd_arg_index[cmd_next - 1]`, A = `boot_clause[X]`; `JMP (cmd_vector)`; every handler ends at `cmd_run_done` (X pulled, RTS) |
| `bank_enter` | `$0210` | 49 | A = bank 1–7: `PHA; PHP; SEI`; `bank_select`, `bank_probe`; absent → `bank_restore`; else `LDX #$FF / TXS`, then the warm entry `$FFD4` when the bank's `$FFE1`/`$FFE0` equal `bank_ver_minor`/`bank_ver_major`, else `$00` into `$01–$64` and the cold entry `$FFD0` |
| `bank_scan` | `$0241` | 29 | with `bank_restore` `$0252`: `PHA; PHP; SEI`; for A = 7 down to 1 (`DEC A`): `bank_select`, `bank_probe`, `bank_read_id`; then `bank_restore`: P2 from `$75`, X from `$76`, Y from `$78`, `PLP; NOP; PLA` |
| `bank_select` | `$025E` | 25 | A = bank: X = `bank_p2_table[A]` (`$0200,X`), or `bank_p2_table_alt[A]` (`$0208,X`) when DIP bit 1 is set; `$77` = P2 & `$83`; P2 = X \| `$77`; A kept, X = the table value |
| `bank_probe` | `$0277` | 14 | `SEB0 bank_absent`; `CLB0 bank_absent` when the selected bank reads `$78` at `$FFD0`; A kept |
| `bank_read_id` | `$0285` | 92 | A = bank: X = 14·(A − 1) by an add loop; absent → 14 spaces into `bank_id_table` (`$2026`); else 6 bytes from `$FFE2`, 4 from `$9800`, 4 from `$FFEA` of the selected bank through `bank_put_id_char`; A kept |
| `bank_put_id_char` | `$02E1` | 8 | `bank_safe_char`, then `$2026,X` = A, X += 1 |
| `bank_safe_char` | `$02E9` | 7 | A below `$20` becomes `$2A` (`*`) |
| `bank_find_current` | `$02F0` | 35 | `bank_current` = the first X in 1–7 whose table entry equals P2 & `$7C` (table chosen by DIP bit 1 on every pass); no match: `bank_current` unchanged; A and X kept |

### 2.2 Who calls them

- `bank_load_ramcode`: `boot_reset` `$A678` (ROM until M4.6c2) and `bank_cmd_select` `$ABB2`.
- `bank_scan`: `boot_reset` `$A67B`. `bank_find_current`: `boot_reset` `$A67E` and `bank_cmd_select` `$ABB8`. `bank_enter`: `bank_cmd_select` `$ABB5`.
- `bank_select`, `bank_probe`: `bank_enter` and `bank_scan`. `bank_read_id`: `bank_scan`. `bank_put_id_char`: `bank_read_id`. `bank_safe_char`: `bank_put_id_char`.
- `cmd_run_next`: `cmd_run_due` `$A88D` (C since M4.2; ROM in the `golden_hybrid` modes where `cmd_run_due` is not replaced).

### 2.3 C calls C

- The C `cmd_run_due` calls the C `cmd_run_next` (until now it called the ROM `cmd_run_next` for `bank_cmd_select` and for an unknown address).
- `bank_cmd_select` joins the C handler table (`cmd_run.c`, `k_handlers`) and calls the C `cmd_hex_value`, `bank_load_ramcode`, `bank_enter` and `bank_find_current` directly; `bank_enter` and `bank_scan` call the C `bank_select`, `bank_probe` and `bank_read_id`, and so on down.
- The remaining ROM callers (`boot_reset`; `cmd_run_due` in the modes where it is ROM) reach the C through the hooks.

### 2.4 Hooks (10 new replacements; 110 in all, 111 `golden_hybrid` modes)

- Main-ROM rows: `bank_load_ramcode` `$A600`, `cmd_run_next` `$A893` (its exit is `cmd_run_done`'s RTS; `cmd_run_due` reads X after it, `$A888 CPX cmd_next_pos`).
- RAM-code rows (new, §3.3): `bank_enter`, `bank_scan`, `bank_select`, `bank_probe`, `bank_read_id`, `bank_put_id_char`, `bank_safe_char`, `bank_find_current`.
- `bank_cmd_select` is not hooked: like the M4.2 handlers it is entered only through `cmd_run_next`'s computed jump and leaves by `JMP cmd_run_done`, not RTS.
- Each row's outputs carry a comment naming what the ROM callers read after the call, checked in the dumps (as in M4.6a/M4.6b).

### 2.5 The NUL bug (the unknown-address fallback)

A NUL byte in the text right after a command prefix is a partial match (M4.2 spec §2.6), and the scan queues handler bytes read from inside the pattern string: after `@` and any letter but `?` the query pattern `*?` matches partially and queues `$D300` (the middle of `pros_phrase_gen`); other prefixes queue zero-page addresses (`@M5` then NUL: `$002A`). The ROM jumps there; the C cannot follow. The C `cmd_run_next` leaves RAM untouched for every address with no C handler and calls the ROM `cmd_run_next` through `fw_rom_call_unhooked` (§3.2) with the registers of `$A88D` (X = the position, Y = `cmd_next`, A = X), so the ROM does its own bookkeeping and jumps where it jumps, exactly as today. CLI text cannot contain a NUL; no golden reaches this.

### 2.6 Quirks kept

- Every RAM write, the scratch ones included: `$75`/`$76`/`$78` (`bank_load_ramcode`), `$77` (`bank_select`), `bank_absent` bit 0 set then cleared (`bank_probe`), the 14 ID bytes per bank, `bank_current`, `$01–$64` on the cold path; `$02FF` written twice.
- The bank tables are read from the RAM copy (`$0200,X`, `$0208,X`), never from the ROM image.
- Every SFR access through the host in the ROM's order: `bank_select`'s P2 read and write, `bank_restore`'s P2 write, `bank_find_current`'s P2 read; P4 is not read (the DIP bits come from `boot_dip` in RAM).
- Every read of the selected bank (`$FFD0`, `$FFE0`/`$FFE1`, `$FFE2–$FFE7`, `$9800–$9803`, `$FFEA–$FFED`) through `fw_rom8`, so it reaches `read_bank` under another bank (§3.1).
- The 8-bit index wraps and loop bounds exactly as the ROM has them (`bank_read_id`'s add loop, `bank_find_current`'s `CPX #8`, `bank_scan`'s `DEC A / BNE`).
- `PHP; SEI ... PLP` pairs are invisible to the C (nothing else runs while the C runs); the I flag set by `bank_enter`'s `SEI` before a bank entry is not undone and is part of the exit state (§3.2).
- The exit registers and flags the callers read.

## 3. Infrastructure

### 3.1 The selected bank (`fw.h`/`fw.c`)

- `f->bank`: P2 bits 2–6 as the firmware last wrote them; the main ROM is `$70` (`boot_init_ports` `$AC50 LDM #$70,P2`; `bank_p2_table[1]`). Every P2 write through `fw_wr_sfr8`, `fw_sfr_seb` and `fw_sfr_clb` updates it, as it updates `low_window` (M4.6b spec §3.4). `fw_init` sets `$70`.
- `fw_rom8(f, addr)` with `addr >= $8000` while `f->bank != $70` reads through the new host call `read_bank(ctx, addr)`; without a host or `read_bank`, a fault at `addr`. Reads of `$4000–$7FFF` are unchanged (the bank bits do not select them on the board).
- After a ROM call and after a wait the hybrid host reads the bank back from the machine's P2 pins, with `low_window`.

### 3.2 Leaving the C (`fw.h`/`fw.c`)

- `fw_enter(f, addr)`: the bank entries `$FFD0`/`$FFD4`. It calls the new host call `enter(ctx, addr)`, then abandons the C as an unwound ROM call does (`f->unwound` set, a longjmp to `f->trap`); without a host or `enter`, a fault at `addr`. The state the ROM leaves at the jump is set before the call: RAM by the C, and through `enter` the CPU state the host must give the machine: SP `$FF` (`LDX #$FF / TXS`), the I flag set, A and X as `bank_enter` leaves them (warm: A = the bank's `$FFE0`, X = `$FF`; cold: A = 0, X = 0). In M4.6c2 `fw_enter` first looks for a C re-entry point that the C boot sets.
- `fw_rom_call_unhooked(f, addr, regs, budget)`: like `fw_rom_call`, through the new host call `rom_call_unhooked`; the hook at `addr` does not fire on this entry. Used only by the NUL fallback (§2.5).
- `fw_host` gains `read_bank`, `enter` and `rom_call_unhooked`.

### 3.3 Machine API additions (no model change)

- `machine_hook_ramcode(m, addr, fn, user)`: a hook at an address in `$0200–$03FE`. `find_hook` accepts it when the PC is there and the map says RAM; main-ROM hooks keep their current check (the main ROM, not the lower window or another bank). It fires only while the CPU executes there, which only happens after `bank_load_ramcode` has copied the code in.
- `machine_rom_call_unhooked(m, addr, regs, max_cycles)`: `machine_rom_call` with the hook at `addr` skipped on the first instruction only; every later entry of `addr` and every other hook fire as usual.

### 3.4 The hybrid host (`thunks.c`)

- `read_bank`: `machine_peek(m, addr)`, the bus as the CPU sees it: `$FF` for an empty socket, the main ROM when the main bank is selected.
- `enter`: sets the machine's SP, I flag, A and X; `run_frame`'s unwound branch then leaves the PC at `addr` instead of where a ROM call left it.
- `rom_call_unhooked`: `machine_rom_call_unhooked`, with the window and bank read back as after `rom_call`.
- The replacement table gains a `ramcode` flag; `fw_install` hooks those rows with `machine_hook_ramcode`.

### 3.5 Converted data (`src/firmware/data/bank_data.c`)

- `bank_ramcode_image` `$ADFE–$AFFC` (511 bytes):
  - `bank_p2_table` and `bank_p2_table_alt` (`$0200`/`$0208` in RAM) as named P2 values (`P2_MAIN_BANK` `$70` and the empty-socket selections);
  - the bank routines `$0210–$0312` as an instruction listing at their RAM addresses, placed at RAM address + `$ABFE`;
  - the letter-to-sound code copied along, `$AF11–$AFFC` (RAM `$0313–$03FE`, never executed in RAM), as an instruction listing at its ROM addresses.
- The main bank's header (every byte the bank routines read under the main bank; `bank_hdr_ffe8` `$FFE8–$FFE9` is not read): `$FFD0–$FFD3` (`SEI` / `JMP boot_reentry_cold`, a listing), `bank_hdr_version` `$FFE0–$FFE1`, `bank_hdr_lang_code` `$FFE2–$FFE7`, `bank_hdr_lang_ver_text` `$FFEA–$FFED`, `bank_hdr_engine_ver_text` `$9800–$9803`.
- The listing: one row per instruction (its address, mnemonic, addressing mode, operand), operands named (`JSR BANK_SELECT`, `LDA BANK_HDR_VERSION + 1`, `BBC0 BANK_ABSENT`, …), relative branches by target address. The data builder assembles the rows with a small table-driven encoder for the opcodes the listing uses, from the 740 opcode table the emulator uses (MAME's `dm740.lst`; `$1A` = `DEC A`, `$3A` = `INC A`). `firmware_data`'s existing check (every converted byte equal to the ROM) covers it. No hex blobs.

## 4. Verification

### 4.1 `diff_lts`

- The 10 new replacements join `k_every_call`: every call is compared (at boot `bank_scan` once and `bank_select`, `bank_probe`, `bank_read_id` 7 times each; at each `@=`).
- **Unwound calls are compared at the bank entry.** Today a ROM copy that resets the stack stops right after the reset. The C copy now stops at `$FFD4`/`$FFD0` (`fw_enter`), so the ROM copy runs on to the bank entry and both are compared there: PC, SP, P (the I flag), A, X, RAM (the dead stack excluded), the SFR state. Between the `TXS` and the entry the ROM only reads (warm) or clears zero page (cold), so this is stricter than before.
- `bank_select` (corpus: `@=5` absent, `@=1` the main ROM, warm) must still produce unwound calls (> 0).
- The run-time gate stays: if `diff_lts` alone takes more than 5 minutes, report before sampling anything.

### 4.2 `diff_serial` (probes injected into both copies; each proved ROM against ROM first, each reached > 0 times)

- **Cold entry:** `bank_ver_minor`, and in a second probe `bank_ver_major` (with the minor equal), altered before `@=1`: the version-mismatch paths; compared at `$FFD0` after zero page is cleared.
- **DIP bit 1:** the machine's P4 input with bit 1 set (and P2.1 low, so `boot_selftest` passes): `bank_select` and `bank_find_current` read `bank_p2_table_alt`; `@=5` then finds the main ROM and enters warm.
- **Synthetic calls from real states:** `bank_safe_char` with A below `$20` (the `*` branch; no real bank's ID has such a byte); `bank_find_current` with P2 bank bits found in neither table (no match, `bank_current` unchanged).

### 4.3 The NUL fallback

- A unit test (stub host): an unknown address leaves RAM untouched and calls `rom_call_unhooked($A893)` with X = the position, Y = `cmd_next`, A = X; a failed call is a fault.
- A machine test (`test_machine_hooks`): `machine_rom_call_unhooked` skips the hook at its entry once; the same hook fires on a later entry; other hooks fire.

### 4.4 The branch probe

As in M4.6b: every conditional branch of the 12 routines is seen both ways on the corpus or under a probe (a throwaway tool, not committed); a branch seen one way only is pinned by a named unit test and recorded in As built.

### 4.5 `golden_hybrid`, `test_timing_invariance`, the CLI

- `golden_hybrid`: 48 files x 111 modes, the goldens byte-identical. On `bank_select`, every mode in which C runs the dispatch or `bank_enter` must unwind at least once.
- `test_timing_invariance` passes on both engines.
- `--engine hybrid` equals `--engine rom` on every corpus text (always with `-o FILE`).

### 4.6 Unit tests (new suite `tests/test_fw_bank.c`, `suite_fw_bank`; 38 suites)

- Each ported routine against a stub host that logs P2 reads and writes, `read_bank` and `enter`: the RAM writes, the SFR order, the exit registers and flags, the present, absent and empty-table paths.
- `f->bank`: set by P2 writes through `fw_wr_sfr8`, `fw_sfr_seb`, `fw_sfr_clb`; read back after a ROM call and after a wait, together with `low_window` (the M4.6b deferred item: `host_wait`'s `low_window` read-back was untested).
- `fw_rom8` under another bank (through `read_bank`) and under the main bank (the image); `fw_enter`, `fw_rom_call_unhooked` and a read under another bank fault without a host.
- `test_machine_hooks`: RAM-code hooks fire at `$0200–$03FE` while the CPU executes there; main-ROM hooks are unchanged.
- The existing tests whose stubs record the ROM `cmd_run_next` (`test_fw_cmd`, `test_golden_hybrid`'s bridge tests) updated to the C dispatch and the unhooked fallback.

### 4.7 Done criteria

1. The 12 routines are C, the 10 hooks installed; the C `cmd_run_due` calls the C `cmd_run_next`, which calls the ROM only for an unknown address.
2. `firmware_data` covers the RAM-code image and the header; 0 bytes differ.
3. `diff_lts` (with the bank-entry comparison) and `diff_serial` (with the new probes) report 0 mismatches; every probe reached.
4. The branch probe is recorded; any one-sided branch is pinned by a unit test.
5. `golden_hybrid` (48 x 111), `test_timing_invariance` on both engines and the CLI check pass.
6. The new and updated unit tests pass; the full suite is green (38 suites).
7. The parent spec rev 17 (M4.6c1 done), this spec's As built section and the README are updated.

### 4.8 Task outline (for the plan)

1. Infrastructure: `f->bank` and `read_bank`, `fw_enter`/`enter`, `fw_rom_call_unhooked`; `machine_hook_ramcode`, `machine_rom_call_unhooked`; the `ramcode` row flag; the hybrid host's calls; `diff_lts` stopping unwound calls at the bank entry in both copies (the corpus still green with the ROM `cmd_run_next`); `suite_fw_bank` wired in with real tests.
2. The data: the listing encoder, `bank_data.c` (image and header), `firmware_data` green.
3. `diff_serial`'s new probes, proved ROM against ROM.
4. The RAM-code routines and `bank_load_ramcode` in C, with hooks and unit tests.
5. `cmd_run_next` and `bank_cmd_select` in C, the C `cmd_run_due` switched, the NUL fallback, the updated tests; the branch probe.
6. Docs: the parent spec rev 17, As built, the README.

## 5. Out of scope

- M4.6c2: boot (`boot_*`), the main loop, `isr_idle` (its WIT at `$C602` is the input idle, where serial bytes are delivered: a wait kind of its own, with the delivery guard deferred from M4.6b), the input the C pulls at its idle wait and the end of a run, the C warm and cold re-entry (`bank_entry_warm`/`bank_entry_cold` and `boot_reentry_*`), `cmd_say_phonetic`, `isr_unused`, the remaining boot data.
- Option-ROM images: every bank but the main ROM is an empty socket.
- `pros_pitch_code_dither` `$C131`: no call, jump or pointer reaches it.
- M5: the native host, including its behaviour for the NUL bug's unknown addresses.
