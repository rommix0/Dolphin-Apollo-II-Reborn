# M4.2 — The command parser in C — Design

**Status:** implemented (plan docs/superpowers/plans/2026-10-03-m4-2-command-parser.md)
**Parent specs:**
- `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 8): §6 step 3, §8 milestone 4.
- `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md`: hooks, thunks, `rom_call`, the data image and the tests (§3–§5, §8 "As built").
- `docs/superpowers/specs/2026-10-03-m4-1-text-normalisation-design.md`: the C `tn_convert_clause`, the per-call budget, the flush skip (§2–§5, §7 "As built").

**User decisions (2026-10-03):**
- Scope: the whole main-level command path in C, which covers the matcher, the clause scan, the dispatcher and every deferred handler, including the event producers, the "apply" helpers and the reply handlers. Hardware and later-milestone code stays in ROM behind `fw_rom_call`.
- Approach 1: the C dispatcher owns the handlers. Only routines that ROM code still calls are hooked. Handlers are C functions behind a `switch` on the handler address in RAM.
- The M4.1 regression on `@=1` is fixed in M4.2 with a non-local exit (§3).
- Coverage gaps are closed with new corpus texts. Their goldens are generated once by the rom engine, and the existing goldens stay unchanged.

## 1. Goal

When M4.2 is done, the command parser runs as C in the `hybrid` engine. It:
- reproduces every golden byte for byte, including new goldens that reach every command the corpus missed;
- matches its ROM originals at every call site the corpus reaches;
- reads the command tables only from committed, readable C;
- behaves like the ROM when `@=` enters another bank, so the hybrid engine no longer faults on `@=1`.

## 2. The subsystem (all main ROM)

### 2.1 How a clause's commands run
- **Scan** (`cmd_scan_clause` $ACFA, called by `boot_loop_text` before conversion):
  - It copies `boot_clause` ($1B00) to `cmd_text` ($1A00). Codes ≥ $21 are mapped through `cmd_case_map` unless `cmd_phonetic` is set.
  - At each `cmd_char` it matches the text after it against the 31 patterns of `cmd_table` (first match wins).
  - A match is cut out of the text and queued:
    - `cmd_positions[n]` = the `cmd_text` position;
    - `cmd_handlers[n]` = the handler address copied from the ROM pattern record;
    - `cmd_arg_index[n]` = the `boot_clause` index of the last matched character.
  - An unmatched `cmd_char` stays in the text. No handler runs during the scan.
  - `isr_par_ready` is called once per iteration.
- **Run** (`cmd_run_due` $A880, `cmd_run_next` $A893):
  - For each `cmd_text` position, `tn_convert_clause` calls `cmd_run_due` with X = the position.
  - `cmd_run_due` runs every queued command due there, in queue order.
  - `cmd_run_next` loads `cmd_vector` from `cmd_handlers[cmd_next]`, advances `cmd_next` and `cmd_next_pos`, and sets X = `cmd_arg_index` and A = `boot_clause[X]`. Then it does `JMP ($1489)`.
  - Handlers end with `JMP $A8BD` (`PLA / TAX / RTS`).
- **Events.** `@T`, `@A`, `@B` and `@I+` queue command events in `$1200–$1485`, keyed by the low byte of `tn_phon_len`. These events run at playback (`cmd_run_event`), which is not part of M4.2.
- **Immediate path.** Every host `@` first passes through the interrupt-level `cmd_imm_match` ($C413). It matches `I+` and `I?` with the shared `cmd_match_table`.

### 2.2 Ported to C
| Group | Routines |
|---|---|
| Matcher | `cmd_match_pattern` $AD90, `cmd_match_table` $ADBF, `cmd_upcase` $C50D, `cmd_hex_value` $C520, `cmd_hex_byte` $B450, `cmd_find_back` $ABDF |
| Scan | `cmd_scan_clause` $ACFA |
| Dispatcher | `cmd_run_due` $A880, `cmd_run_next` $A893 (with `cmd_run_done` $A8BD) |
| Setters | `cmd_say_version` $A902 (with `cmd_put_phonetic` $A868), `cmd_set_phrase_pause`, `cmd_set_word_pause`, `cmd_set_mark_space`, `cmd_set_spell`, `cmd_set_punct`, `cmd_set_hyper`, `cmd_set_phonetic`, `cmd_set_formant`, `cmd_set_pitch`, `cmd_set_speed`, `cmd_set_prosody`, `cmd_set_source`, `cmd_set_speaker`, `cmd_set_voice`, `cmd_ignore` |
| Event producers | `cmd_sound` $A8C0, `cmd_set_volume` $AB05, `cmd_set_voicing` $AB70, `cmd_mark_index` $AB2E |
| Apply helpers | `cmd_apply_speaker` $A78C, `cmd_select_voice_a` $A7FE, `cmd_select_voice_b` $A814, `cmd_apply_pitch` $A832, `cmd_apply_speed` $A84A, `cmd_clear_formant` $A7DF |
| Replies | `cmd_echo` $AB99, `cmd_query` $ABD3 (with `cmd_reply_setting` $C632), `cmd_list_languages` $ABBE |

That is about 620 instructions.

### 2.3 Hooks (5 new replacements; 16 in all)
Only entries that ROM code still calls are hooked:

| Entry | ROM caller | Outputs |
|---|---|---|
| `cmd_scan_clause` $ACFA | `boot_loop_text` | none (`tn_convert_clause` follows) |
| `cmd_match_table` $ADBF | `cmd_imm_match` (interrupt level) | Y, plus whatever its caller reads, checked in the plan |
| `cmd_hex_value` $C520 | `bank_cmd_select` (stays in ROM) | A and the flags `bank_cmd_select` reads, checked in the plan (`FW_OUT_Z` if needed) |
| `cmd_run_due` $A880 | the ROM `tn_convert_clause`, in the modes where it runs in ROM | X kept, as the ROM keeps it |
| `cmd_apply_speaker` $A78C | `boot_reset` | none beyond what `boot_reset` reads, checked in the plan |

- The C `tn_convert_clause` calls the C `cmd_run_due` directly. Its `fw_rom_call` of `$A880` is removed (C calls C, M4.1 §3.2).
- A hooked routine alone (`only`) still runs its C callees as C.

### 2.4 The dispatcher
- `cmd_run_due` and `cmd_run_next` form one C unit. The computed jump becomes a `switch` on the 16-bit address in `cmd_handlers[cmd_next]`.
- RAM stays byte-identical. The scan writes the ROM handler addresses into `cmd_handlers`. The event producers write the ROM event-handler addresses ($BBA0, $BBA7, $BC80, $BCEA, $BCED) into `cmd_ev_handlers`, with the 8-bit index wrap of the queue.
- **Fallback.** If the address is `bank_cmd_select` ($ABAF), or no ported handler (for example the garbage address that the latent NUL bug in §2.6 produces), the C leaves RAM untouched. It then calls the ROM `cmd_run_next` ($A893, not hooked) through `fw_rom_call` with X. The ROM does its own bookkeeping and dispatch, exactly as before.
- Handlers are not hooked. They are entered only through the computed jump, and their exit is `JMP $A8BD`, not RTS.

### 2.5 ROM calls from C (strict budget, `FW_ROM_CALL_BUDGET`)
| ROM routine | Called from | Why it stays in ROM |
|---|---|---|
| `isr_par_ready` $C5D6 | the scan, once per iteration (as `tn_convert_clause` does) | SFR access |
| `par_setup_voice` $B3A4 | `cmd_apply_pitch` | M4.4/M4.5 code |
| `ser_tx_put` $C4DD, `ser_tx_hex` $C571, `ser_tx_records` $C5A4 | the replies | the serial subsystem (M4.6) |
| `cmd_run_next` $A893 | the dispatcher's fallback (§2.4) | `bank_cmd_select` touches P2 |

The registers are passed exactly as the ROM has them at each call. The plan checks in the dumps what each callee reads.

### 2.6 Quirks kept
- The scratch writes: `$75`, `$76`, `$78`, `$34`/`$35`, `$1809`, and `$77` (`tn_resume_pos`, which `cmd_reply_setting` uses as scratch).
- The matcher has no length bound. It reads past the clause end into stale RAM, as the ROM does.
- `cmd_hex_value`'s mapping: '0'–'9' → 0–9, 'A'–'Z' → 10–35, ':'–'@' → 35, and others wrap. The callers' dead `CMP #0 / BCC` checks stay.
- `cmd_find_back` re-anchors on the command letter, so `@MM5` and `@RRxx` re-anchor too.
- `cmd_set_voice` branches on `cmd_hex_value`'s Z. That stays inside C.
- `cmd_positions[cmd_count]` is not reset after the last command (stale).
- The `$AD4D` high-byte increment in the scan is dead code.
- **The latent NUL bug.** A NUL byte right after a command letter is a partial match, which the scan takes as a match. It then reads a handler address from inside the pattern string. The C writes the same bytes, and its dispatcher falls back to ROM `cmd_run_next` (§2.4), which jumps where the ROM jumps.

## 3. Non-local exit: abandoning C when the ROM resets the stack

`@=n` with a present bank (`bank_cmd_select` → `bank_enter`) resets the stack and re-enters the firmware at the bank's warm entry. It never returns.

Since M4.1, `apollo2 --engine hybrid -o x.wav "@=1 Hello world."` faults (exit 2): the C `tn_convert_clause` calls `cmd_run_due` in ROM, and that call runs out of its budget. `--engine rom` renders the same text with exit 0.

- **Detection** (`machine_rom_call`):
  - Today a call ends either by returning to its sentinel at SP = s0, or by failing.
  - It gains a third outcome, **unwound**: after any instruction, if SP is above s0 and the CPU is not back at the sentinel, the routine has consumed the sentinel's return address and can never return.
  - The call stops right after that instruction, and PC and SP stay where the ROM put them.
- **Propagation:**
  - The host's `rom_call` reports the unwound outcome.
  - `fw_rom_call` marks the context unwound (not a fault) and long-jumps out of the C, as a fault does.
  - `run_thunk` then neither faults nor emulates the RTS. It leaves PC and SP as the ROM left them and counts the unwind (`hybrid_stats.unwinds`).
  - Nested C needs nothing extra. If the thunk was itself entered inside a `rom_call`, that enclosing call's own SP check sees the same reset and unwinds in turn, up to the hook that first entered C.
  - The machine then carries on from the warm re-entry, as the ROM's own call chain does.
- **`diff_lts`:**
  - `machine_finish_routine` already stops when SP rises above the entry, so an unwound call is **compared**, not skipped. Both machines must stop at the same instruction with the same RAM, PC and SP.
  - The flush skip (the stop hook at `boot_loop_play`) is legal only for `tn_convert_clause`. A flush in any other observed routine is a mismatch. This closes the M4.1 follow-up.

## 4. Converted data (R5)
All of this data goes into readable C in `src/firmware/data/`, encoded at its ROM address and compared byte for byte by `firmware_data`:

| ROM | Name | Bytes | Readable form |
|---|---|---|---|
| `$989C–$98AC` | `cmd_imm_patterns`, `cmd_imm_table` | 17 | `{ "I+", ROM_CMD_IMM_INDEX_ADD }`, `{ "I?", ROM_CMD_IMM_INDEX_QUERY }`; the encoder writes the `$FF` end and the pointer table |
| `$98AD–$994E` | the deferred patterns | 162 | one record per pattern: the pattern text and a named handler address; then the `$FF` end |
| `$994F–$998E` | `cmd_table` | 64 | 31 pointers in match order plus the end pointer, generated by the encoder; entries 27 and 28 both point at `G#`, as an explicit, commented duplicate |
| `$998F–$99B3` | `cmd_query_table` | 37 | 18 × (letter, zero-page setting by its `fw_ram.h` name), then 0 |
| `$99B4–$9AB3` | `cmd_case_map` | 256 | identity plus a commented exception list |
| `$C87B–$C897` | `cmd_speed_maps`, `cmd_speed_limits` | 29 | 10 pointers `ROM_PROS_SPEED_MAPS + 32·i` (the 10th repeats the 9th, commented), then 9 limits |
| `$D469`, `$E5E2` | `par_voice_a_count`, `par_voice_b_count` | 2 | numbers (the head bytes of the M4.4 voice tables) |

- `firmware_data`'s converted-byte count goes from 35,311 to 35,878 (+567).
- `$9804–$9BB3` becomes one contiguous converted block.
- The plan checks every size and layout against the ROM. Where the ROM differs from the annotations, the ROM wins and the difference is reported. `ghidra740/` is never edited.

## 5. Verification

### 5.1 New corpus texts
Each new text gets a golden generated once by the rom engine. Existing goldens stay unchanged. The plan confirms each text's coverage with `apollo2_coverage` and fixes the exact wording:

| Text | Reaches |
|---|---|
| `cmd_misc` | `@E`, `@L` (with `ser_tx_records`), `@G`, `@I^`, `@+` |
| `index_marks` | `@I+`, `@I?`: the immediate path's partial and full match, `cmd_mark_index`, `cmd_ev_index` |
| `cmd_args` | `@R` with a letter (`cmd_hex_byte`), `@V0`, `@A:`-style arguments (`$C52B`), unknown queries |
| `cmd_unmatched` | a `cmd_char` that matches no command (`$AD58–$AD60`) |
| `bank_select` | `@=5` (empty socket; returns) and `@=1` (warm re-entry; unwinds) |

Replies (`@E`, `@L`, queries) never appear in the frames. `diff_lts` checks them through the RAM comparison, which covers the transmit ring.

### 5.2 `diff_lts`
- The 5 new entries are observed with the M4.0 sampling. `cmd_run_due` covers every handler's execution.
- Every one of the 16 routines must be checked more than 0 times, with 0 mismatches.
- Unwound calls are compared (§3). Flush skips are legal only for `tn_convert_clause`.

### 5.3 `golden_hybrid`
- All corpus files (about 32) × 17 modes (all routines, plus each of the 16 alone) must reproduce the goldens byte for byte.
- Every mode must run C.
- The `long_clause` flush assertion of M4.1 stays.
- `bank_select` must unwind at least once in each mode where the C dispatcher runs: all routines, `tn_convert_clause` alone (its C calls the C `cmd_run_due`), and `cmd_run_due` alone.
- Hybrid audio equals the rom engine's.

### 5.4 Unit tests (`tests/test_fw_cmd.c`, a mock host that records every ROM call)
- **Matcher:** `*` and `#`, a partial match on a NUL, first match wins, the `$FF` end, the case map.
- **Scan:**
  - a command is cut out and queued;
  - an unmatched `cmd_char` stays;
  - phonetic mode skips the case map;
  - `isr_par_ready` is called once per iteration.
- **Dispatcher:** queue order, `cmd_run_due`'s position test, and the ROM fallback for `bank_cmd_select` and for an unknown address.
- **Handlers:**
  - `cmd_hex_value`'s mapping and Z;
  - `cmd_find_back`'s re-anchoring;
  - the event queue's index wrap;
  - `cmd_apply_pitch` calls `par_setup_voice`;
  - the replies call the right `ser_tx_*` routines.
- **Unwinding (`machine_hooks`):**
  - a `rom_call` into a routine that resets the stack reports unwound, with PC and SP where it left them;
  - a nested thunk unwinds through its enclosing `rom_call`.

### 5.5 Done criteria
1. `diff_lts` checks all 16 routines more than 0 times each with 0 mismatches. Flush skips occur only in `tn_convert_clause`, and the `bank_select` text produces compared unwound calls.
2. `golden_hybrid` reproduces every file × 17 modes byte for byte. C runs in every mode, and the flush and unwind assertions of §5.3 hold.
3. `firmware_data` passes with 35,878 converted bytes.
4. The new goldens exist and reach what §5.1 lists. Every existing golden is unchanged.
5. `apollo2 --engine hybrid` equals `--engine rom` for `@=1` and for every new text.
6. `src/firmware/` still builds without `machine/` or `cpu740/` headers. No unexpected `fw_rom8` fault occurs.
7. `build.bat test` is green.
8. The docs are updated: the parent spec gets rev 9, this spec's status changes (with "As built" if anything differs), and the README's hybrid section lists the command parser.

## 6. Out of scope
- `cmd_run_event` and the five event handlers (playback, M4.3–M4.5; `cmd_ev_set_volume` writes SFR P3 through `isr_set_volume`).
- `par_setup_voice` and the voice tables (M4.4/M4.5).
- The interrupt-level immediate path (`ser_rx_byte`, `cmd_imm_match`, `cmd_imm_index_add`/`_query`, `ser_rx_flush_cmd`), `boot_get_clause`, `boot_set_defaults` and the `ser_tx_*` routines (M4.6).
- `bank_cmd_select` (P2 bank switching) and `cmd_say_phonetic` (an unreachable caller; it plays speech).

## 7. As built

Where the ROM is more precise than, or differs from, the sections above. The ROM wins; `ghidra740/` is not edited.

- Thunk outputs as finally declared (`src/engine/thunks.c`). They are wider than §2.3 says, because each is compared by `diff_lts` and the ROM leaves them defined:
  - `cmd_scan_clause`: none;
  - `cmd_match_table`: A, X and Y (A = $FF at the end marker, else the entry X; X preserved). §2.3 said Y. The ROM `cmd_scan_clause` reads Y and X; A reaches only the dead stack;
  - `cmd_hex_value`: A, X, Y and Z (X and Y preserved). §2.3 said A and the flags. `bank_cmd_select` passes A on to `bank_enter`, and its `bank_load_ramcode` saves X and Y in `$76`/`$78`; `cmd_set_voice` branches on Z. N and C are read by no caller;
  - `cmd_run_due`: X and Y (Y = `cmd_next`, as its last `LDY` leaves it). §2.3 said X. Only `tn_convert_clause` calls it, and it reads X;
  - `cmd_apply_speaker`: A, X and Y. §2.3 said none beyond what `boot_reset` reads, and `boot_reset` reads all three (next bullet).
- `par_setup_voice` reads none of A, X and Y and returns X = 9 and Y = $0B. `cmd_apply_pitch` restores only A, so `boot_reset` sees X and Y changed after `cmd_apply_speaker`: its `bank_load_ramcode` saves them in `$76`/`$78`. The `cmd_apply_speaker` hook therefore hands all three back.
- The dispatcher finds a C handler by address in a table (`k_handlers` in `cmd_run.c`, 23 rows), not with the `switch` of §2.4. The behaviour is identical.
- The dispatcher's ROM fallback calls `cmd_run_next` with X = the position and Y = `cmd_next`, because `bank_cmd_select` → `bank_load_ramcode` saves Y in `$78` (`$A601 STY $78`).
- `diff_lts` also counts the compared calls whose ROM copy unwound and fails if `bank_select` is in the corpus and the count is 0. It prints `N unwound calls compared`; on the current corpus N = 2: the `bank_select` `@=1` under `tn_convert_clause` and under `cmd_run_due`. The unwound call is detected by SP in the reference copy: its SP ends above the return address.
- `cmd_handler_y` (the Y every handler enters with, `cmd_next` − 1) is one inline helper in `cmd_handlers.h`, shared by the setters, the apply helpers and the replies.
- The version-text data addresses are named `CMD_VERSION_ENGINE_TEXT` ($9804) and `CMD_VERSION_LANG_TEXT` ($F75B) in `data/fw_data.h`, next to the other `CMD_*` data names.
- The speed-map base is named `CMD_SPEED_MAP_DATA` ($C898, not converted) instead of `ROM_PROS_SPEED_MAPS`. The voice-table addresses `PAR_VOICE_A` ($D46A) and `PAR_VOICE_B` ($E5E3) are immediates in `cmd_select_voice_a`/`_b`; the tables stay unconverted.
- The scan skips the case map on bit 0 of `cmd_phonetic` (`BBS0`), not on any non-zero value, and only for codes ≥ $21.
- The scan writes `$78` a second time for every case-mapped character (`$AD1B`), so it ends as the Y of the last mapped character or of the last `cmd_char`, whichever came later.
- The scan is a do-while: with `boot_clause_len` = 0 it still reads one character and sets the length to 1 (unreachable: `boot_get_clause` always puts a space at index 0).
- `cmd_hex_value` splits on bit 7 of c − 'A' (`BMI`), so case-mapped codes above 'Z' wrap as well ($80 gives $49, $E0 gives $B0).
- `cmd_sound` writes its second event at instruction offsets (`$1208,X`, `$1308,X`) that do not wrap with X: at `cmd_ev_count` = 127 it lands on `cmd_ev_handlers[0]`'s low byte and on `cmd_ev_args[0..1]`.
- The event producers' arguments (`STA $1406,X` with X = `cmd_ev_count`) do not wrap: a count of 128 or more writes into `cmd_count` and the deferred queue.
- `@T` accepts every value up to $10 plus 'X', so `@TG` is effect 16, like `@TX`.
- `cmd_mark_index` tests the raw '+' (no `cmd_hex_value`) and writes no argument, so `cmd_ev_args[n]` stays stale.
- A query compares the raw letter with the upper-case letters of `cmd_query_table`, so a lower-case query (`@a?`) gets no reply.
- `cmd_reply_setting` goes on with the X and Y that `ser_tx_put` returns; `cmd_query` reads the letter at `$1AFF,X`, so X = 0 reads `cmd_text[255]`.
