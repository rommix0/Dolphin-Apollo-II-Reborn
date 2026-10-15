# M4.1 — Text normalisation in C — Design

**Status:** implemented (plan docs/superpowers/plans/2026-10-03-m4-1-text-normalisation.md)
**Parent specs:**
- `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 7): §6 step 3, §8 milestone 4.
- `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md`: the hooks, thunks, `rom_call`, the data image and the tests that M4.1 builds on (§3–§5, §8 "As built").

**User decisions (2026-10-03):**
- M4.1 ports `tn_convert_clause` and `tn_append_phonetic` and converts the length-prefixed phonetic strings, as outlined in M4.0 §6.
- The corpus never reaches the mid-clause flush. A long-clause corpus text is added, and its golden is generated once by the rom engine (the oracle). The existing goldens stay unchanged.
- ROM calls from C get a cycle budget per call (approach 1). The short helpers keep the strict 10 M-cycle budget. The playback calls get a large speech budget.
- Testing is split. `diff_lts` covers every call that does not flush mid-clause. `golden_hybrid` covers the flush end to end.

## 1. Goal

When M4.1 is done, the clause-to-phonetic-text driver and the phonetic append run as C in the `hybrid` engine. They:
- reproduce every golden byte for byte, including a new golden that exercises the mid-clause flush;
- match their ROM originals at every call site the corpus reaches without a flush;
- read the phonetic strings only from committed, readable C.

## 2. The routines (all main ROM)

### 2.1 `tn_append_phonetic` ($B154)
- **Input:** A = the length m. `(tn_rule_ptr),Y` points at the length byte.
- **Effect:**
  - m = 0: nothing is written.
  - Otherwise `$78` = m.
  - When `cmd_voice` is not 0, the m bytes after the length byte are copied to `tn_phonetic` at `tn_phon_len`, and `tn_phon_len` advances by m. `$78` counts down to 0, Y ends on the last byte copied, and X = the new `tn_phon_len`.
  - When `cmd_voice` = 0 (voice off), `$78` keeps m, X = `tn_phon_len`, and `tn_phon_len` and Y are unchanged.
  - Returns A = `$FF`.
- **Callers:**
  - The C ports `tn_say_char` and `lts_match_list` now call the C version directly. `tn_append_phonetic_rom` and its `rom_call` are removed.
  - The ROM caller `cmd_put_phonetic` ($A868) reaches it through a hook. `cmd_put_phonetic` saves and restores A and Y around the call. Its own callers' use of X and the flags is checked in the plan, and the thunk declares exactly what they read.

### 2.2 `tn_convert_clause` ($B1E6)
- **Effect:** converts `cmd_text` (length `boot_clause_len`) to phonetic text in `tn_phonetic`. It follows the ROM structure exactly:
  1. `tn_resume_pos` = 0. Per pass: `cmd_ev_count` = 0, `tn_phon_len` = 0, and `tn_pos` = `tn_key_pos` = `tn_resume_pos`.
  2. Per character:
     - **ROM calls:** `isr_par_ready`, then the deferred commands due, through `cmd_run_due` for each `tn_key_pos` up to `tn_pos`.
     - **Mid-clause flush:** when `tn_phon_len` ≥ `$FA`, save X (= `tn_pos`) in `tn_resume_pos`, call `pros_make_segments` and `boot_loop_play` in ROM, and start a new pass.
     - **Phonetic mode** (`cmd_phonetic` set): copy the character verbatim.
     - **Letters** (`lts_char_class` bit 7): at the start of a word, with spell mode off, scan the word. If a non-letter comes before any vowel (class bit 6), set `cmd_spell` = 3. Then call `lts_convert_char` (C), clear P2.7, set `tn_in_word` bit 0, and continue from `tn_pos`.
     - **Any other character:** clear `tn_in_word` bit 0, and clear automatic spelling (`cmd_spell` bit 1 set → 0).
       - A space is copied, unless spell mode is on, `tn_pos` ≠ 0 and `tn_pos` + 1 ≠ `boot_clause_len`; then `tn_say_char` speaks it.
       - Digits `0`–`9` go to the rules like letters.
       - Other characters are copied, or spoken with `tn_say_char` when `cmd_punct` (@P1) is set. A copy saves X in `$75`.
  3. After the last character, `cmd_run_due` runs once more (except in phonetic mode, which returns at once, as in the ROM).
- **ROM calls from C:**
  - `isr_par_ready` ($C5D6) and `cmd_run_due` ($A880) use the strict budget.
  - `pros_make_segments` ($CA92) and `boot_loop_play` ($A7D4) use the speech budget (§3.1).
  - These calls pass registers exactly as the ROM sets them: X = `tn_key_pos` for `cmd_run_due`.
- **Caller:** `boot_loop_text` ($A68D) returns straight after the call. It declares no output registers.
- **Quirks kept:**
  - The `$FA` threshold is tested before each character, so `tn_phonetic` may grow past `$FA` within one character's output.
  - After `tn_say_char`, `tn_pos` = its X result + 1 (that X is the old `tn_pos`). After `lts_convert_char`, the ROM reloads X = `tn_pos` − 1 before the same `INX / STX tn_pos`, so `tn_pos` keeps the value the rules left.
  - The register flow through ROM calls is kept: the C uses the X that `isr_par_ready` and `cmd_run_due` return, as the ROM does (`STX tn_resume_pos` after `isr_par_ready`, `CPX tn_pos` after `cmd_run_due`).
  - Phonetic mode also passes through the `$FA` test and the `cmd_run_due` loop for every character.
  - `$75` and `$78` scratch writes are kept.

## 3. Infrastructure changes

### 3.1 ROM-call budget per call
- `fw_rom_call(fw *f, uint16_t addr, fw_regs *regs, uint64_t budget)` and the host's `rom_call` take the cycle budget, and the bridge passes it to `machine_rom_call`.
- Two named constants live in `src/firmware/fw.h`, because the firmware chooses the budget:
  - `FW_ROM_CALL_BUDGET` = 10,000,000 (a helper that does not return within it is a fault, as in M4.0);
  - `FW_ROM_CALL_SPEECH_BUDGET` = 1,000,000,000 (a call that plays speech; a real hang still faults).
- `FW_ROM_CALL_BUDGET` moves from `thunks.h` to `fw.h`. Once §2.1 lands, the only `fw_rom_call` users are the four calls of `tn_convert_clause`.

### 3.2 Thunks
- Two new replacements, `tn_append_phonetic` and `tn_convert_clause`, bring the table to 11 routines.
- C calls C directly. A hooked routine alone (`only`) still runs its C callees as C.
- No change to the hook semantics. The C part of a thunk takes 0 cycles and cannot be interrupted. A ROM routine it calls runs normally, with its cycles and any interrupt that falls due.

## 4. Converted data (R5)

The five length-prefixed phonetic strings are converted into a readable table in `src/firmware/data/`. Each string is written as its length, its text and the CR the ROM keeps, and is encoded at its original address:

| Address | Name | Size |
|---|---|---|
| `$9804` | `cmd_version_engine_text` | 32 |
| `$F75B` | `cmd_version_lang_text` | 32 |
| `$ACC7` | `boot_baud_text_19200` | 3 |
| `$ACCA` | `boot_baud_text_300` | 14 |
| `$ACD8` | `boot_baud_text_1200` | 16 |

- The baud texts are unreachable today, because `boot_reset` calls `boot_init_baud` only with X = 0. They are converted now because the later port of `boot_init_baud` (M4.6) needs them.
- `tools/extract_tables.py` gains the strings. `firmware_data` extends its coverage ranges, its byte count and the byte comparison with the ROM.
- The plan checks the exact sizes and framing against the ROM. If they differ from the annotations, the ROM wins and the difference is reported, not edited in `ghidra740/`.

## 5. Verification

### 5.1 `diff_lts` (extended)
- The two new routines are added to the observed set, with the same sampling as M4.0: all calls up to 1,000 per routine per file, then every 64th. Every routine must be checked at least once.
- **Calls that flush mid-clause are skipped and counted.** The ROM copy runs with interrupts held, so it could never return from `boot_loop_play`. Detection: the scratch machines get a stop hook at `boot_loop_play` ($A7D4). A scratch run that reaches it is abandoned and counted as "flush, skipped". The new long-clause file must produce at least one such skip.
- All other calls are compared exactly as in M4.0: full RAM except the dead stack and the SFRs, the exit PC and SP, the declared outputs, and the P2 latch.

### 5.2 `golden_hybrid` (extended)
- **Corpus and modes:** 27 corpus files (26 plus `long_clause`) and 12 modes (all routines, plus each of the 11 alone). Every golden must be reproduced byte for byte, every mode must run C, and the hybrid render must equal the rom engine's.
- **The flush path must run.** A counter in the hybrid run (C `tn_convert_clause` flushes) must be > 0 for `long_clause` in the all-routines mode and in the `tn_convert_clause`-alone mode.

### 5.3 The long-clause text
- `tests/corpus/long_clause.txt` holds one clause whose phonetic text passes `$FA`, for example a run of digits or spelled letters.
- Coverage (`apollo2_coverage`) must show `$B1FF–$B207` executed.
- Its golden `tests/golden/long_clause.frm` is generated once by the rom engine and committed. No other golden changes.

### 5.4 Done criteria
1. `diff_lts` checks all 11 routines more than 0 times each with 0 mismatches. It counts at least one skipped flush call.
2. `golden_hybrid` reproduces 27 files × 12 modes byte for byte. C runs in every mode, the flush counter is > 0 as in §5.2, and hybrid audio equals the rom engine's.
3. `firmware_data` passes with the five strings.
4. The new golden exists, its text reaches `$B1FF`, and every existing golden is unchanged.
5. `src/firmware/` still builds without `machine/` or `cpu740/` headers. No unexpected `fw_rom8` fault occurs.
6. `build.bat test` is green.
7. The docs are updated: the parent spec gets rev 8 (M4.1 done), this spec's status changes, and the README's hybrid section lists the new routines.

## 6. Out of scope
- `cmd_put_phonetic`, `cmd_say_phonetic` and `cmd_say_version`, which go to M4.2 with the command parser.
- `cmd_run_due` and the deferred-command machinery (M4.2).
- Prosody and playback (M4.3 and later).
- `isr_par_ready`, which stays in ROM: the firmware context has no SFR access by design.

## 7. As built

- `tn_convert_clause` itself writes only the `$75` scratch; the `$78` writes listed in §2.2's quirks belong to `tn_append_phonetic` (§2.1), and are kept there.
- The `tn_append_phonetic` thunk declares A, X and Y as outputs. No ROM caller reads them (cmd_put_phonetic restores A and Y; its callers and ROM tn_say_char reload X), so this is stricter than "exactly what they read"; it lets diff_lts compare all three.
- Flags: a thunk leaves P as it was on entry. ROM `$B154` exits with N=1, Z=0, C=1. On every ROM path into it (lts_match_list's BCC at `$B119` after `CMP #$1E`, tn_say_char's JSR at `$B0DB`, cmd_put_phonetic at `$A86F`) N and Z agree; C differs but every successor redefines it before reading it.
- Checked in the dumps (Task 4): `pros_make_segments` (`$CA92`) and `boot_loop_play` (`$A7D4`, including `boot_play_body` at `$B2B3`) write X and Y before reading them, so passing A = `tn_phon_len`, X = `tn_pos` and Y = 0 at the flush is exact.
- diff_lts installs the `boot_loop_play` stop hook for every observed routine; today only `tn_convert_clause` reaches it. The corpus run skips 1 flush call (long_clause); `tn_convert_clause` has 133 calls checked and `tn_append_phonetic` 460, with 0 mismatches. golden_hybrid runs 27 files × 12 modes.
- The fault test for a ROM call from C that does not return now uses `tn_convert_clause` → `isr_par_ready`, since `tn_append_phonetic` no longer calls ROM.
- The strings' framing differs from §4's "length, text and the CR": the two version texts have no CR; boot_baud_text_1200's length (15) counts its CR; only boot_baud_text_19200 and _300 keep a CR after the counted text. The table stores each as the counted text plus a tail (`fw_phonetic_text.text`, `.tail`).
