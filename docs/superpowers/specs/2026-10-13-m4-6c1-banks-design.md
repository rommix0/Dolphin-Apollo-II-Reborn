# M4.6c1 — The banks and the command dispatcher in C (the RAM-resident bank code, cmd_run_next, @=) — Design

**Status:** implemented (plan docs/superpowers/plans/2026-10-13-m4-6c1-banks.md)
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

A NUL byte in the text right after a command prefix is a partial match (M4.2 spec §2.6), and the scan queues handler bytes read from inside the pattern string: after `@` and any letter but `?` the query pattern `*?` matches partially and queues `$D300` (the middle of `pros_phrase_gen`); other prefixes queue zero-page addresses (`@M5` then NUL: `$002A`). The ROM jumps there; the C cannot follow. The C `cmd_run_next` leaves RAM untouched for every address with no C handler and calls the ROM `cmd_run_next` through `fw_rom_call_unhooked` (§3.2) with the registers of `$A88D` (X = the position, Y = `cmd_next`, A = X), so the ROM does its own bookkeeping and jumps where it jumps, exactly as today. CLI text cannot contain a NUL; no golden reaches this. *(Corrected 2026-10-13 from the plan's proof.)* The hybrid follows the ROM only while the target code does not return through the stack: `@M5` then NUL (`$002A`) ends with the same CPU fault on both engines (a BRK at `$2300`); `@A` then NUL (`$D300`, which returns through the stack) ends with no frame on both, but the ROM engine stops on an illegal opcode at `$8F02` and the hybrid at `$A893`, because the C dispatcher's callers have no return addresses on the machine stack. The hybrid on main (M4.2's fallback) ends the same way. User ruling: keep the fallback and pin both outcomes in a test; M5 decides.

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

- `fw_enter(f, addr)` *(built as `fw_enter(f, addr, a, x)` with `enter(ctx, addr, a, x)`: the C passes the A and X `bank_enter` leaves; corrected 2026-10-13 from the plan's proof)*: the bank entries `$FFD0`/`$FFD4`. It calls the new host call `enter(ctx, addr)`, then abandons the C as an unwound ROM call does (`f->unwound` set, a longjmp to `f->trap`); without a host or `enter`, a fault at `addr`. The state the ROM leaves at the jump is set before the call: RAM by the C, and through `enter` the CPU state the host must give the machine: SP `$FF` (`LDX #$FF / TXS`), the I flag set, A and X as `bank_enter` leaves them (warm: A = the bank's `$FFE0`, X = `$FF`; cold: A = 0, X = 0). In M4.6c2 `fw_enter` first looks for a C re-entry point that the C boot sets.
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

- `bank_ramcode_image` `$ADFE–$AFFC` (511 bytes; *(corrected 2026-10-13 from the plan's proof)* the converted range is `$ADFE–$AFFD`, 512 bytes, because the listing's last instruction, `LDA #1` at `$AFFC`, ends at `$AFFD`; with the 20 header bytes, 49,044 converted bytes in all):
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

## 6. As built

Where the ROM is more precise than, or differs from, the sections above. The ROM wins; `ghidra740/` is not edited.

- **The arguments and the callers** (sharper than §2.1 and §2.2): `bank_cmd_select`'s pattern `"=#"` takes one digit ('0'–'9', `cmd_match.c`), so `bank_enter` gets a bank 0–9, not only 1–7: 0 reads `bank_p2_table[0]` and 8–9 read `bank_p2_table_alt[0–1]` through `$0200,X`, all `P2_MAIN_BANK`, so `@=0`, `@=8` and `@=9` enter the main bank warm; `@=F` and `@=G` match no command. With `boot_dip` bit 1 set, `$0208,X` for 8–9 reads `$0210–$0211`, `bank_enter`'s own first bytes (no test reaches this). `bank_read_id`'s add loop gives X = 14 × (A − 1) in 8 bits (A = 0: 255 passes, X = 242). `bank_load_ramcode`, `bank_scan` and `bank_find_current` run at boot and at every warm re-entry: `boot_reentry_warm` $A66B runs into `boot_reset`'s $A678, $A67B and $A67E. The exits: `bank_load_ramcode` A and Y kept, X = 0; `bank_select` A and Y kept, X = the table value; `bank_probe` and `bank_find_current` A, X and Y kept; `bank_read_id` A kept, X after the last byte written, Y = 6 (present) or 14 (absent); `bank_put_id_char` X + 1; `bank_scan` and an absent `bank_enter` A kept, X = $76, Y = $78.
- **The thunks** (§2.4): the nine bank rows declare A, X and Y and no flag, each with a comment saying where the registers go next; no caller reads X or Y after `bank_load_ramcode`, `bank_scan`, an absent `bank_enter` or `bank_find_current` (the next code loads them before reading them, or pushes and pulls them unread), but they are declared all the same, so `diff_lts` compares all three. The eight RAM-code rows carry the `ramcode` flag (`test_rows`). The `cmd_run_next` row declares A and X (`cmd_run_done`'s `PLA / TAX`: both are the X it pushed), not Y, which its one ROM caller writes first ($A880 `LDY cmd_next`); its thunk runs the C and sets A = X = the X it came with. In "all" mode only `boot_reset`'s three calls enter the new hooks from ROM (`bank_load_ramcode`, `bank_scan`, `bank_find_current`); every caller of the other seven is C there, and they fire from ROM in their own `golden_hybrid` modes (from the ROM `bank_cmd_select`, `bank_enter`, `bank_scan`, `bank_read_id`, `bank_put_id_char` and `cmd_run_due`).
- **The C layout**:
  - `bank.c`/`bank.h`: the nine routines, and `bank_cmd_select` (declared in `cmd_handlers.h` with the other handlers); one static helper, `bank_restore` ($0252: `bank_scan`'s tail and an absent `bank_enter`'s exit); `SFR_P2` defined locally. A present `bank_enter` leaves through `fw_enter`; without a trap `fw_enter` returns, so `bank_cmd_select` tests `f->unwound` before `bank_find_current`;
  - `bank_addr.h`: the RAM copy's addresses with their callers, `BANK_RAMCODE_IMAGE` $ADFE, `BANK_RAMCODE_IMAGE_END` $AFFC, `BANK_RAMCODE_OFFSET` $ABFE, `ROM_BANK_LOAD_RAMCODE`, `ROM_BOOT_REENTRY_COLD` $A665 and `_WARM` $A66B, the header (`BANK_ENTRY_COLD`/`_WARM`, `BANK_SIGNATURE` $78, `BANK_HDR_VERSION`, `_LANG_CODE`, `_LANG_VER_TEXT`, `_ENGINE_VER_TEXT`), `BANK_ID_CHARS` 14, `BANK_LANG_CODE_CHARS` 6, `BANK_COUNT` 7, `BANK_STACK_TOP` $FF, `BANK_ZP_CLEAR_LAST` $64, `P2_BANK_2` … `P2_BANK_7`, `P2_KEEP` $83, `DIP_ALT_BANKS` $02;
  - `cmd_run.c`/`cmd_run.h`: the static `run_next` became the public `bool cmd_run_next(fw *f, uint8_t x)`, false when the C was abandoned (a failed or unwound ROM call, or a bank entry: `!f->unwound` after a C handler), and `cmd_run_due` stops there; `k_handlers` gains `ROM_BANK_CMD_SELECT`; `cmd_addr.h`'s comments say so;
  - `fw.h`/`fw.c`: `P2_BANK_BITS` $7C and `P2_MAIN_BANK` $70, `f->bank`, the three host calls, `fw_rom_call_unhooked` and `fw_enter`; `fw_rom_call` and `fw_rom_call_unhooked` share a static `call_rom`; `p2_written` sets `f->bank` beside `f->low_window`; the comments of `FW_TMP_75`/`76`/`78` name `bank_load_ramcode`'s saves;
  - `thunks.c`/`thunks.h`: the `ramcode` field (the rows' seventh), `fw_hook` (public: `fw_install` and `diff_lts` hook a row with it), a static `rom_call` behind `host_rom_call` and `host_rom_call_unhooked`, `sync_p2`, `host_read_bank`, `host_enter`, and `bank_regs` (A, X and Y in and out) for the nine bank rows; `machine.h`/`machine.c`: the two calls, the hook entry's `ramcode` and the machine's `skip_hook`/`skip_hook_pc`.
- **The host calls** (§3.1, §3.2, §3.4): `fw_rom8` sends $8000–$FFFF to `read_bank` before it looks at the image whenever `f->bank` is not $70 (`fw_mem8` reaches it too: `test_rom8_bank`). The hybrid `read_bank` is `machine_peek`; `enter` sets SP $FF, the I flag, A, X and the PC (Y and the other flags kept), and `run_frame`'s unwound branch leaves the machine there. A bank entry made by a thunk inside a `machine_rom_call` (`diff_serial`'s copy B calling the hooked `bank_enter`) ends that call `MCALL_UNWOUND` at once (SP above its start). `sync_p2` reads the window and the bank back from P2's pins after every ROM call (hooked or not), after every wait and also at each thunk's entry; `test_host_readback` checks the read-back after an interrupt wait and after a ROM call, and the read at a thunk's entry has no test of its own.
- **The machine calls** (§3.3): `machine_hook` and `machine_hook_ramcode` share one table and one address map, so a second hook at a hooked address is refused either way; `machine_hook_ramcode` refuses addresses outside $0200–$03FE. `find_hook` takes the entry at the PC and fires it only where the map agrees with its kind (RAM for a RAM-code entry, the main ROM file at the address itself for any other), so a `machine_hook` at $0250 never fires (`test_hook_ramcode`). `machine_rom_call_unhooked` sets `skip_hook` for `addr`; `exec` drops the hook it found there once and clears the flag (an interrupt taken at that point first finds no hook and keeps it), and the call clears it whatever happens; `machine_clear_hooks`, and so `machine_clone`, clear it too. `test_rom_call_unhooked` (machine): the hook skipped at the called routine, another hook in it fired, the same hook fired on the routine's second entry, and a plain `machine_rom_call` fires it.
- **The converted data** (§3.5): the rows are `fw_insn` { address, mnemonic, mode, operand, target, bit }: 130 for the bank routines at their RAM addresses (+ $ABFE in the ROM), 117 for the letter-to-sound code at its ROM addresses $AF11–$AFFD, 2 for `bank_entry_cold` (`SEI` / `JMP ROM_BOOT_REENTRY_COLD`); the two tables are `P2_MAIN_BANK` and `P2_BANK_2` … `P2_BANK_7` (`bank_p2_table_alt` maps banks 2–4 to the selections of 5–7 and banks 5–7 to the main bank); the texts are "000440", "0201", "0404", the version 2, 1. Three `JMP`s inside the letter-to-sound code give their loop label as an address ($AF59 → $AF35, $AF5F → $AF33, $AF7A → $AF6B), as the branches do (the three `BBC0 lts_match_dir` to `lts_step_back` $B004 too). The encoder (`put_listing`, step 12 of `fw_data.c`) knows 57 (mnemonic, mode) pairs of 42 mnemonics (`fw_opcodes`, a bit instruction's opcode for bit 0 + $20 × bit) and fails a row that does not land where the previous one ended, a pair it does not know, a branch out of reach, a bit above 7 or a bit on another instruction, or a byte operand above $FF. `test_listing_opcodes` checks each pair against `cpu740_optable` (all 8 bits of a bit instruction) and that none is listed twice; `test_counts` the counts 130, 117, 2 and 57. `firmware_data` compares 49,044 bytes, 532 more than before: $ADFE–$AFFD, $9800–$9803, $FFD0–$FFD3, $FFE0–$FFE7 and $FFEA–$FFED.
- **`diff_lts`** (§4.1): the ten rows join `k_every_call` and the Review Focus texts 1, 2 and 4 join `k_extra` (`DIFF_MAX` 112); `install` hooks each row with `fw_hook`. A call whose ROM copy reset the stack runs each copy on with `diff_run_to_bank_entry` (at most `DIFF_ENTRY_STEPS` = 1,000 instructions) and compares them with `diff_same_at_entry`: PC, SP, A, X, the I flag, the SFR and DSP state and RAM, not Y or the other flags (both re-entries write them before reading them). With SP $FF the dead stack it leaves out is the whole page $0100–$01FF. The run prints 105 lines: `bank_enter` 9, `bank_scan` 58, `bank_select` 415, `bank_probe` 415, `bank_read_id` 406, `bank_put_id_char` 812, `bank_safe_char` 812, `bank_find_current` 61, `bank_load_ramcode` 67, `cmd_run_next` 165 calls checked; `1 mid-clause flush calls skipped`, `24 unwound calls compared` (six bank entries, the corpus's `@=1` and the extra texts' `@=0`, `@=8`, `@=9` and two `@=1`, each under `tn_convert_clause`, `cmd_run_due`, `cmd_run_next` and `bank_enter`), 0 mismatches. `diff_lts` alone took 180 s on the committed code (179 s in the proof run); the full suite 860 s.
- **`diff_serial`** (§4.2), as built: the six new probes are at the entries of the RAM-code routines they test, hooked with `machine_hook_ramcode` (their target destinations in RAM too), not at the `isr_int1` point: `cold_minor` and `cold_major` at `bank_enter` (A = 1 and `bank_ver_minor` + 1, or `bank_ver_major` + 1; targets $0228 → $0235 and $0230 → $0235), `dip_enter` at `bank_enter` ($0263 → $0266), `dip_find` and `no_bank` at `bank_find_current` ($02F9 → $02FC; $0309 → $030B with P2's latch bits 2–6 set to $0C), `safe_char` at `bank_safe_char` (A = $1F; $02EB → $02ED). The three `bank_enter` probes (`enters`) need each copy's call to end `MCALL_UNWOUND` and reach the bank entry, and compare there with `diff_same_at_entry`. The DIP bit 1 run is the `bank_select` text once more with the P4 input's bit 1 set; the two `dip` probes are checked in it only, the others outside it only. Self-check (ROM against ROM; `queries`, `bank_select` and the DIP run): 2, 2, 2, 3, 4 and 42 calls; corpus, boot and the DIP run (C): 2, 2, 2, 3, 50 and 686 calls; every target reached at every call, 0 mismatches; 34 probes in all (the 28 earlier ones now see 60 and 9 calls in the self-check).
- **The NUL fallback** (§2.5, §4.3): `test_run_next_unknown` (`test_fw_bank.c`): $D300 queued, RAM byte for byte unchanged and one unhooked call of $A893 with A = X = 4 and Y = 1; $002A with a failed call: a fault at $A893; an unwound call stops `cmd_run_due` before the second queued command. `test_golden_hybrid`'s bridge test (2) queues $D300 with a hook stuck there: a firmware fault at $D300, and an observer hooked at $A893 counts 0 entries. `test_review_focus` pins both outcomes of §2.5's note.
- **`tests/test_fw_bank.c`** (§4.6): its stub host logs every SFR read and write, `read_bank` (the value returned), `enter` (A, X) and `rom_call_unhooked` (A, X, Y; it returns each + 1) in order (`check_log`); every other bank is empty ($FF); `setup_banks` loads the RAM copy and the version copies. Its tests: `test_bank_tracking`, `test_rom8_bank`, `test_enter` and `test_rom_call_unhooked` (the faults without a host or the call), `test_rows`, `test_load_ramcode` (one P2 read, $0200–$03FE equal to the image and $01FF and $03FF untouched, A and Y kept, X 0), `test_select` (`$77` = $81 from P2 $F1, the window and `f->bank` following; the alt table; banks 15, 10 and 0), `test_probe`, `test_put_id_char` ($1F, $20, $FF; X $FF wraps to 0), `test_read_id` (bank 1 from the image, an absent bank 7, another bank's control codes as '*' through 14 `read_bank`s, bank 0 at + 242), `test_find_current` (the alt table, no match in either table), `test_scan` (21 host calls, the ID table, `f->bank` back to $70), `test_enter_absent`, `test_enter_present` (banks 0 and 1 warm with zero page untouched; cold with the minor differing, $00 and $65 kept, and with the major differing, its log; another present bank's $FFE1 then $FFE0 through `read_bank`), `test_run_next_row`, `test_run_next_unknown` and `test_cmd_select` (`@=5` over a zeroed RAM copy: six host calls, $78 = 0, $76 = the argument index, the table reloaded, `bank_current` 1, the next command run; `@=1` warm: the next command not run, `cmd_next` 1).
- **The other unit tests** (§4.6):
  - `tests/test_machine_hooks.c`: `test_hook_ramcode`, `test_rom_call_unhooked` (above), `test_host_bank` (the bridge's `read_bank` under the main bank and an empty socket, `fw_rom8` of converted data under another bank $FF without a fault, `enter`'s machine state, `fw_rom_call_unhooked` past the hook and `fw_rom_call` through it) and `test_host_readback` (a timer interrupt handler in RAM that selects bank $78 with the window on, then a ROM call that selects the main bank with it off; the M4.6b deferred item);
  - `tests/test_firmware_data.c`: the ranges, counts and opcode test above;
  - `tests/test_fw_cmd.c`: the mock answers `rom_call_unhooked` (the ROM `cmd_run_next` of the queue tests, which now queue $D300 and $002A), counts every hooked `rom_call` and fails it (none happens), reads every other bank as $FF and keeps P2; `test_run_due_all_rows` runs `bank_cmd_select` as C (`" @A5 "`: bank 5, an empty socket) with no ROM `cmd_run_next`; `test_run_due_position` also checks A = X;
  - `tests/test_fw_tn.c`: queues $D300 instead of `bank_cmd_select`, its mock answering `rom_call_unhooked` too; `tests/test_fw_tn.c` and `tests/test_fw_par.c` start P2 at `P2_MAIN_BANK`, since `isr_dsp_reset`'s `SEB0`/`CLB0` now carry P2's bank bits into `f->bank`;
  - `tests/test_golden_hybrid.c`: the bridge tests (2) and (4) queue $D300 instead of `bank_cmd_select`; the new (5) has the C enter the main bank on a fresh machine through `cmd_run_due`: version copies 0 give the cold entry ($64 cleared), 2.01 the warm (A 2, X $FF), with SP $FF, I set, no fault, one unwind, the trap restored and `f->unwound` cleared.
- **The Review Focus** (the plan's header; `test_review_focus` boots both engines, feeds the raw bytes and compares the status, the fault, the frames, the queued reply, `ser_tx_pending`, `bank_current` and the 98 ID bytes): 1. `@=0 @=8 @=9 @=F @=G`: the same, idle, 3 unwinds in the hybrid; `diff_lts`'s `k_extra`, `test_select` and `test_enter_present` (bank 0) too. 2. `@=1 One. @=5 Two. @=1 Three.`: the same, idle, frames made, 2 unwinds; the bridge test (5) for the trap and `f->unwound`. 3. `@=5 @L`, `@=1 @L`, `@=1 One. @L @=5 Two. @L`: the same queued reply, 104, 0 and 208 bytes, starting "L70E0" and the main bank's ID; after a warm `@=1` the rest of its clause is abandoned on both engines. 4. a sentence, `@=5`, more speech: the same, idle, no unwind; `test_cmd_select` for the reload over a zeroed copy. 5. `"@M5" NUL CR` and `"@A" NUL CR`: no frame on either engine, the outcomes of §2.5's note. The CLI check of Task 5 gave the same WAV on both engines for the five texts and the 47 corpus texts.
- **`golden_hybrid` and `test_timing_invariance`** (§4.5): 48 files x 111 modes, every golden unchanged; on `bank_select`, the modes "all", `tn_convert_clause`, `cmd_run_due`, `cmd_run_next` and `bank_enter` must unwind at least once (`runs_c_dispatch`). `test_timing_invariance`: the ROM engine unchanged (11,789,152 and 1,286,784 cycles); the hybrid 11,684,594 and 979,508, 8,297 cycles fewer at both scales than with the ROM bank code (11,692,891 and 987,805 in Tasks 1–3): the boot-time bank code is now C (a thunk's C takes no emulated cycles; the timing text queues no command).
- **The branch probe** (§4.4, §4.7 criterion 4): the throwaway tool ran while the plan was proven (`rom_engine_run` over the corpus and the boot run, hooks at both destinations of each conditional branch, RAM-code destinations with `machine_hook_ramcode`); it was not re-run on the committed code, which the plan's tasks reproduce from the same text, and its table (the plan, Task 5 Step 5) stands. **16 branches** in eight routines (`bank_load_ramcode` 1, `bank_enter` 4, `bank_scan` 1, `bank_select` 1, `bank_probe` 1, `bank_read_id` 4, `bank_safe_char` 1, `bank_find_current` 3; `cmd_run_next`, `bank_cmd_select` and `bank_put_id_char` have none): 8 seen both ways on the corpus ($A618, $0219, $0250, $027F, $028A, $0294, $02A0, $02DD); 6 one way only, each completed by a `diff_serial` probe (the `BNE`s $0228 and $0230 never taken on the corpus: `cold_minor`, `cold_major`; $0263 `BBC1 boot_dip`, $02F9 `BBC1 boot_dip` and $0304 `BEQ` always taken: `dip_enter`, `dip_find`, `no_bank`; $02EB `BCS` always taken: `safe_char`); 2 never reached on the corpus, both ways under probes ($023C, the cold clear: `cold_minor` and `cold_major`; $0309: `no_bank`). None is pinned by a unit test instead; `diff_serial` checks at every run that each probe reaches its target at every call.
