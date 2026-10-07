# M4.4b The Frame Loop in C — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the 16 frame-loop routines to C and prove them against the ROM: the 14 that return without waiting through `diff_lts`, the two that wait on the timer interrupt (`par_play`, `boot_loop_play`) through a new live differential test, `diff_play`, and all of them end to end through the goldens.

**Architecture:**
- **Infrastructure and data first (Task 1).**
  - The firmware gains an SFR read through the host (`fw_sfr8`, for `UART_STATUS`), an indirect store (`fw_wr_ind_y`, for the frame bytes) and a fault helper (`fw_fault`).
  - `util_shl3`/`util_shl4` move to `fw_ops.h`.
  - The replacement table gains a `waits` flag.
  - `par_slot_map` is converted.
  - A new corpus text, `late_event`, reaches the one branch no text reached.
- **The leaves, then the loop.**
  - The track steppers (Task 2) and the command events (Task 3) are ordinary routines, checked by `diff_lts`.
  - `diff_play` comes next (Task 4), proved first ROM against ROM.
  - Then `par_play` and `boot_loop_play` in C (Task 5), which call the C routines directly and poll the timer interrupt's flag through ROM calls to `isr_par_ready`.
- **C calls C.**
  - `boot_loop_play` → `par_play_start`, `par_play`;
  - `par_play` → `cmd_run_event`, `par_next_segment`, `par_step_tracks`, `cmd_ev_sound_end`;
  - the C `tn_convert_clause` → the C `boot_loop_play`.
- **What stays ROM.** The frame builder (`frm_*`, M4.5) and the interrupt, serial and DSP routines (`isr_*`, `ser_*`, M4.6), called through `fw_rom_call`.
- **Hooks.** All 16 routines, giving 64 replacements and 65 `golden_hybrid` modes.

**Tech Stack:** C11 with MSVC 2022 (`build.bat`, `/W4 /WX`).

**Spec:** `docs/superpowers/specs/2026-10-07-m4-4b-frame-loop-design.md` (M4.4b spec, with the corrections of dcfc1e8 and c5ca8fa). Its foundations are:
- the M4.4a spec, `docs/superpowers/specs/2026-10-07-m4-4a-playback-setup-design.md` (§2.5, the output contract this loop drives; §6 "As built");
- the M4.2 spec, `docs/superpowers/specs/2026-10-03-m4-2-command-parser-design.md` (the event producers and the handler dispatch);
- the M4.0 spec, `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md` (§8 "As built").

The parent spec is `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 12).

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
- The full suite takes about 7–9 minutes (430 s at the end of Task 5 in the proof run) (`golden_hybrid` 46 files × 65 modes, `diff_play` about 40 s). Use a suite filter while iterating and the full run at each task's end.

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
- The existing goldens in `tests/golden/*.frm` stay byte-identical. The one new golden, `late_event.frm`, is generated once by the rom engine in Task 1. If anything changes another golden, stop and report.
- Nothing under `src/firmware/` includes a `machine/` or `cpu740/` header. `build.bat` checks this.
- The ported C keeps the ROM's behaviour exactly, quirks included (M4.4b spec §2.6):
  - every RAM write, the write-only ones included: `cmd_ev_vector`, `util_interp_quot`, the halved `util_interp_steps`, `par_start_track`/`par_end_track`, the full-byte `LDM #0` of `isr_frame_flag`;
  - the 8-bit wraps (`cmd_run_event`'s X, `par_end_dur` past 0) and the carry chains exactly as the ROM has them (no `CLC` where the ROM has none);
  - signed and unsigned tests exactly as the ROM's flags make them (`par_track_overlap`'s `CMP`/`BPL`/`BMI` is bit 7 of the 8-bit difference);
  - the order of every ROM call, with the registers the ROM has at each `JSR`.
- Writes to the dead stack (below SP at exit) need not be reproduced, because `diff_lts` and `diff_play` exclude it.
- ROM data is read only through `fw_rom8`, `fw_ind_y` or `fw_mem8` at its ROM address. The playback pointers point into ROM during a @T effect, so they are never read with `fw_rd8`. Frame bytes are written only through `fw_wr_ind_y` on `isr_fill_ptr`.
- Thunk outputs: A, X and Y for all 16, each with a comment saying what the ROM callers read after the call, checked in the dumps.
- `diff_lts` and `diff_play` failures are fixed by finding the root cause in the dump. Never loosen a comparison or an assertion.

**Commits**
- End every commit message with exactly `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. This is the user's standing rule and overrides any other attribution line the environment suggests.

---

### Task 1: The host's SFR read, `fw_wr_ind_y`, `fw_fault`, `util_shl3`/`util_shl4` in `fw_ops.h`, the frame loop's ROM addresses, the `waits` field and `DIFF_MAX` 64; `par_slot_map` converted; the `late_event` corpus text and its golden (spec §2.2, §2.4, §2.5, §2.6, §3, §4.2, §4.6)

**ROM facts checked / what was verified while proving the plan** (scratch worktree off main c5ca8fa, this task alone; everything below was run, nothing is copied from the spec or the survey):
- **`UART_STATUS` $E7 has no read side effect**: `m50734_read` (`src/machine/m50734.c`) returns `0x02 | (rx_full ? 1 : 0)`; it is in `m50734_is_modelled`'s ranges and is not write-only, so `bus_read` logs nothing. The only read side effect in the SFR model is $E5 (`uart_rx_full = false`), plus `bus_read`'s unmodelled-access log ($E4, and SFRs outside the modelled ranges). `machine_peek` returns 0 for every SFR, so it cannot be used.
- **`host_read_sfr` reads only $E7.** It reads `UART_STATUS` through `m->bus.read`, the CPU's own path (the one `host_set_low_window` already uses), and answers $FF for every other SFR without touching the bus: the firmware polls only $E7 (`par_play` $B895), and no other register's read (above all $E5's clear of receive full) can then happen behind the ROM's back. `test_host_read_sfr` (`tests/test_machine_hooks.c`) pins it through the real bridge: $E7 = $02, then $03 with a received byte, the read clears nothing, `UART_RX` $E5 reads $FF and leaves the byte received, and `unmodelled_count` stays 0. Without that test nothing would run `host_read_sfr` before Task 5.
- **The fault helper** is `fault(fw *, uint16_t)`, static in `fw.c`; `fw_sfr8` and `fw_wr_ind_y` use it, so both behave like `fw_rom8` (longjmp to `f->trap` when set, else $FF / no write, `fault_addr` set at the first fault only). Tasks 2 and 3 need it from outside `fw.c` (`util_interp_step`'s zero-step hang, `cmd_ev_sound_start`'s scan without an end), so this task exports it as `fw_fault(fw *f, uint16_t addr)`, a one-line wrapper; `test_fault` pins it without a trap (the tests use no `setjmp`).
- **`fw_wr_ind_y`** forms the address as `fw_ind_y` does: `(uint16_t)(fw_rd16(f, zp) + y)`, the carry into the high byte, the 16-bit wrap. Like `fw_ind_y` it reads the pointer's high byte from `zp + 1` (`$100` for `zp = $FF`); every M4.4b caller passes `ISR_FILL_PTR` $2C (`fw_ram.h`: `#define ISR_FILL_PTR 0x002C`), so that case never arises.
- **`fw_host` initialisers**: `tests/test_fw.c` builds hosts positionally with three members (`{ NULL, NULL, host_window }`, `{ NULL, host_call, host_window }`); the new fourth member `read_sfr` is then NULL. The five other test files assign members of a static `g_host`, so `read_sfr` is NULL there too. No existing initialiser changes.
- **`util_shl3`/`util_shl4`** (`rom_C656_util_shl4.txt`: `C656 ASL A`, then `rom_C657_util_shl3.txt`: `C657 ASL A / C658 ASL A / C659 ASL A / C65A RTS`): util_shl4 is one `ASL` falling into util_shl3. The old inline `util_shl4` in `cmd_match.h` (`(uint8_t)(a << 4)`) had five C users: `cmd_apply.c` (2, `cmd_apply_pitch`), `cmd_match.c` (1, `cmd_hex_byte`), `cmd_set.c` (2). **The users include `fw_ops.h`** (each `.c` that calls a ROM routine's inline includes the header that defines it); `cmd_match.h` keeps only the matcher. `thunks.c` and `tests/test_fw_cmd.c` include `cmd_match.h` but never use `util_shl4`. `fw_ops.h` includes `tn_convert.h`, which includes only `fw.h`: no include cycle. `diff_lts` and `golden_hybrid` (Step 11: every N identical) prove the move.
- **`ROM_BOOT_LOOP_PLAY` users**: `src/firmware/tn_convert.c:115` (the flush), `tests/test_fw_tn.c` (lines 45, 221, 230), `tests/test_diff_lts.c:98` (`stop_at_play`). `test_diff_lts.c` included `tn_convert.h` only for it (nothing else of `tn_` / `TN_` is used there), so its include becomes `par_addr.h`; `tn_convert.c` and `test_fw_tn.c` add `par_addr.h`. `ROM_ISR_PAR_READY` stays in `tn_convert.h`.
- **Every new `par_addr.h` address and caller** comes from the JSR/JMP scan of Step 10, with the containing routine of each site from the dumps (`grep -l "^  <ADDR> " rom_*.txt`). The scan has one false hit: `JMP $B917` "at $C0D7" is the operand of `C0D6 994C17 STA $174C,Y` in `rom_C0D2_par_plan_tracks.txt`. Other facts the comments record: `par_play` $B877 is entered by its three own `JMP`s ($B892, $B8AE, $B914); `isr_wait_frame` is also called by `isr_wait_frames` at its first instruction $B356; `isr_par_hold`'s callers are `boot_reset` $A66F, `boot_get_clause` $A6E4, `boot_play_body` $B2B3 and `boot_queue_string` $C617; `util_shl3` $C657 is also reached by falling through from `util_shl4`. All five `util_shl4` callers are already C. Two existing comments change: `ROM_FRM_COPY_REST_FRAME` and `ROM_FRM_CLAMP_MARK_SPACE` named `par_play` in parentheses (a ROM caller); `par_play` becomes C in Task 5, so it leaves the parentheses, and the header comment now says what the parentheses mean. The file is given whole in Step 6.
- **`par_slot_map`** ($B796–$B7A6 in the ROM: `ff 01 03 05 07 00 02 04 06 08 09 00 00 00 00 00 00`): $B797–$B7A0 = b1, b3, b5, b7, b0, b2, b4, b6, b8, b9 for P1…P10 (FN, F1, F2, F3, ALF, A1, A2, A3, AHF, V; `data.txt` line 461). The only operands `96 B7` in the ROM are at $C16A, $C178, $C19C, $C217, i.e. `LDY $B796,X` at $C169, $C177, $C19B (`rom_C14C_par_track_sequential.txt`) and $C216 (`rom_C1F2_par_track_overlap.txt`), each followed by `STA ($2C),Y`; no operand addresses $B797–$B7A0 directly. The pitch track writes b10 itself (`C12C LDY #$A / C12E STA ($2C),Y`, `rom_C0FF_par_step_tracks.txt`). The map never holds b10, so the encoder's check is `> FRAME_V` (sharper than `frm_snapshot_map`'s `> FRAME_PITCH`, which may hold b10). Tried while proving: `FRAME_PITCH` for P10 makes the image fail to build (`firmware data image: par_slot_map entry 10: frame byte 10`, then `CHECK failed: img != NULL` in `firmware_data`).
- **`firmware_data`**: 47,692 + 10 = **47,702**; `k_rom` gains `{ 0xB797, 0xB7A1 }` (between $B773–$B77D and $B7A7–$B7B1; $B796 and $B7A1–$B7A6 stay unconverted).
- **`waits` and `DIFF_MAX`**: the 48 rows keep their four initialisers (`waits` false). With no row marked, `diff_lts`' install and print loops skip nothing: the 48 `N calls checked` lines are identical, line for line, before and after this task (diffed in the proof run). `DIFF_MAX` 64 sizes `g_obs`/`g_checked` for Task 5's 64 rows.
- **`late_event`** = `"@Q9 " + w(56) + " @B5 oh."`: `seg_256`'s text (`"@Q9 " + w(54) + " oh."`) with the cycle's next two words `o at` and `@B5` before `oh`. Measured with the survey's branch probe (`brprobe44b`, a scratch tool built against the repo sources, not part of the plan, here given the text file as its argument; boot + the text), alongside three other candidates:

  | Text | Speak-phase event (`par_seg_index` = `cmd_ev_pos` at $B88F) | $B88D `BNE` |
  |---|---|---|
  | `seg_256` (no event) | none | taken 0, fall 2 (the boot run's two) |
  | `@Q9 w(54) oh @B5 oh.` | $00FC | taken 0, fall 3 |
  | `@Q9 w(54) oh a go @B5 i to it oh.` | $0103 | taken 1134, fall 3 |
  | **`@Q9 w(56) @B5 oh.`** (`late_event`) | **$0103** | **taken 1134, fall 3** |

  `seg_256`'s own words end at segment 252 ($FC), so a position ≥ 256 needs words beyond them; `w(56)` (the cycle exactly eight times) adds the fewest. The event at $0103: while `par_seg_index` is $0003 on page 0 the low bytes match and `CMP cmd_ev_pos+1` (1 against 0) takes the `BNE` at $B88D, 1,134 passes (every pass of that segment's frames); on page 1 it falls through to `cmd_run_event`, and the probe's handler count shows `cmd_ev_set_voicing` $BCEA run once (`HANDLER BCEA n 1`), with `cmd_ev_sound_start`/`cmd_ev_sound_end` once each from the boot run. No mid-clause flush (`BLPSEQ … boot:P@A7BA P@A687 P@A687`, no $B204), `MACHINE_IDLE`. Every other one-sided branch the survey lists stays one-sided on this text ($A7D6 here too, because the probe runs only one text: the boot's and both speak-phase calls play).
- **The coverage file cannot show $B88D taken**: its target $B895 is also the target of $B87D and $B885. `apollo2_coverage` before and after this task: the same summary `rom 4084, low 0, ramcode 116, ram 0 instruction addresses; 31 indirect edges`, and the two files are byte-identical (`late_event` reaches no new instruction). The probe is the proof; from Task 4 on, `diff_play` compares `par_play` on this very text.
- **Golden**: `late_event.frm` 1845 frames from the rom engine; the update run rewrote 46 goldens and `git status`/`git diff --stat tests/golden` showed only the new file. `golden_hybrid` prints `46 corpus files x 49 modes`.
- **Timing** (this machine): full `build.bat test` 272 s at Step 11; the RED golden run 263 s, the generation 287 s; the final full run (Step 18) 307 s.
- **`brprobe44b` on the committed text** (the consolidated proof): `BR B88D BNE $B895 taken 1134 fall 3`, `HANDLER BCEA n 1`, `BLPSEQ … boot:P@A7BA P@A687 P@A687`.

**Files:**
- Modify: `src/firmware/fw.h`, `src/firmware/fw.c`, `src/firmware/fw_ops.h`, `src/firmware/cmd_match.h`, `src/firmware/cmd_match.c`, `src/firmware/cmd_apply.c`, `src/firmware/cmd_set.c`, `src/firmware/par_addr.h`, `src/firmware/tn_convert.h`, `src/firmware/tn_convert.c`, `src/firmware/data/fw_data.h`, `src/firmware/data/fw_data.c`, `src/firmware/data/par_data.c`, `src/engine/thunks.h`, `src/engine/thunks.c`, `tests/test_fw.c`, `tests/test_machine_hooks.c`, `tests/test_firmware_data.c`, `tests/test_diff_lts.c`, `tests/test_fw_tn.c`, `tests/corpus_list.h`
- Create: `tests/corpus/late_event.txt`, `tests/golden/late_event.frm` (generated)

**Interfaces:**
- Consumes: `fw`, `fw_init`, `fw_rd16`, `fw_ind_y`, the static `fault` (`fw.c`); `ISR_FILL_PTR` (`fw_ram.h`); `fw_bridge`, `fw_bridge_init`, `fw_replacement` (`thunks.h`); `machine`, `m->bus` (`machine.h`), `M50734_UART_STATUS`, `m50734_uart_receive` (`m50734.h`); `FRAME_*`, `put`, `fail` (`fw_data.h`/`fw_data.c`); `CORPUS_NAMES` (`tests/corpus_list.h`).
- Produces (Tasks 2–5 use them; nothing here calls them yet):

```c
/* src/firmware/fw.h */
typedef struct fw_host { ... uint8_t (*read_sfr)(void *ctx, uint8_t addr); } fw_host;   /* last member */
uint8_t fw_sfr8(fw *f, uint8_t addr);                        /* Task 5: par_play's BBC1 UART_STATUS at $B895 */
void fw_wr_ind_y(fw *f, uint8_t zp, uint8_t y, uint8_t v);   /* Task 2: the frame bytes, fw_wr_ind_y(f, ISR_FILL_PTR, y, v) */
void fw_fault(fw *f, uint16_t addr);                         /* Task 2: util_interp_step's 0 steps; Task 3: the @T scan */
/* src/firmware/fw_ops.h */
static inline uint8_t util_shl3(uint8_t a);                  /* Task 3: cmd_ev_sound_start, the util_shl3 thunk */
static inline uint8_t util_shl4(uint8_t a);                  /* Task 3: the util_shl4 thunk (its C users already call it) */
/* src/engine/thunks.h */
bool waits;                                                  /* last member of fw_replacement; Task 5's two rows end with , true */
```

  and in `par_addr.h` every new ROM address of the conventions (`ROM_BOOT_LOOP_PLAY` moved there); `PAR_SLOT_MAP` 0xB797 and `fw_par_slot_map[10]` (`fw_data.h`; Task 2 reads the map only through `fw_rom8(f, PAR_SLOT_MAP - 1 + x)`); `DIFF_MAX` 64; the corpus name `"late_event"` (45 names; `golden_hybrid` 46 files with boot).

No name collides: `grep -rn "fw_sfr8\|fw_wr_ind_y\|fw_fault\|read_sfr\|util_shl3\|PAR_SLOT_MAP\|fw_par_slot_map\|late_event" src tests` prints nothing before this task; the new `par_addr.h` names other than `ROM_BOOT_LOOP_PLAY` are used nowhere yet (`ROM_CMD_EV_*` stay in `cmd_addr.h`, `ROM_ISR_PAR_READY` in `tn_convert.h`).

- [ ] **Step 1: Write the failing tests**

**`tests/test_fw.c`.** Replace

```c
#include "firmware/fw.h"
```

with

```c
#include "firmware/fw.h"
#include "firmware/fw_ops.h"
```

Replace

```c
    g_budget = budget;
    return g_call_result;
}

void suite_fw(void)
```

with

```c
    g_budget = budget;
    return g_call_result;
}

static void *g_sfr_ctx;
static uint8_t g_sfr_addr;
static int g_sfr_calls;

static uint8_t host_sfr(void *ctx, uint8_t addr)
{
    g_sfr_ctx = ctx;
    g_sfr_addr = addr;
    g_sfr_calls++;
    return (uint8_t)(addr ^ 0x5A);
}

/* fw_sfr8 (M4.4b spec 2.4): an SFR byte through the host's read_sfr; no host, no read_sfr or an address below
   $DA is a fault, reads $FF and does not reach the host. */
static void test_sfr8(void)
{
    fw f;
    int ctx;
    fw_init(&f, g_ram, &g_img, NULL);
    CHECK_EQ(fw_sfr8(&f, 0xE7), 0xFF);                     /* no host */
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0x00E7);

    fw_host no_sfr = { NULL, host_call, host_window };   /* a host without read_sfr */
    fw_init(&f, g_ram, &g_img, &no_sfr);
    CHECK_EQ(fw_sfr8(&f, 0xE7), 0xFF);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0x00E7);

    fw_host sfr = { &ctx, host_call, host_window, host_sfr };
    g_sfr_calls = 0;
    fw_init(&f, g_ram, &g_img, &sfr);
    CHECK_EQ(fw_sfr8(&f, 0xD9), 0xFF);                     /* RAM, not an SFR */
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0x00D9);
    CHECK_EQ(g_sfr_calls, 0);

    fw_init(&f, g_ram, &g_img, &sfr);
    CHECK_EQ(fw_sfr8(&f, 0xE7), 0xE7 ^ 0x5A);              /* UART_STATUS: the host's answer */
    CHECK_EQ(fw_sfr8(&f, 0xDA), 0xDA ^ 0x5A);              /* the first SFR */
    CHECK_EQ(fw_sfr8(&f, 0xFF), 0xFF ^ 0x5A);              /* the last */
    CHECK_EQ(g_sfr_calls, 3);
    CHECK_EQ(g_sfr_addr, 0xFF);
    CHECK(g_sfr_ctx == &ctx);
    CHECK(!f.fault);
}

/* fw_wr_ind_y (M4.4b spec 2.6, STA (zp),Y): v at the zero-page pointer plus y, the carry reaching the high byte;
   a target of $4000 or above, or an SFR, is a fault and writes nothing. */
static void test_wr_ind_y(void)
{
    static uint8_t before[FW_RAM_SIZE];
    fw f;
    memset(g_ram, 0, sizeof g_ram);
    fw_init(&f, g_ram, &g_img, NULL);
    fw_wr16(&f, ISR_FILL_PTR, 0x13FE);
    fw_wr_ind_y(&f, (uint8_t)ISR_FILL_PTR, 5, 0xA5);      /* $13FE + 5 = $1403 */
    CHECK_EQ(g_ram[0x1403], 0xA5);
    CHECK_EQ(g_ram[0x1303], 0x00);                         /* not $1303: the carry went into the high byte */
    CHECK_EQ(fw_ind_y(&f, (uint8_t)ISR_FILL_PTR, 5), 0xA5);
    CHECK(!f.fault);

    fw_wr16(&f, ISR_FILL_PTR, 0x3FFE);
    fw_wr_ind_y(&f, (uint8_t)ISR_FILL_PTR, 1, 0x11);      /* $3FFF: the last RAM byte */
    CHECK_EQ(g_ram[0x3FFF], 0x11);
    CHECK(!f.fault);
    memcpy(before, g_ram, sizeof before);
    fw_wr_ind_y(&f, (uint8_t)ISR_FILL_PTR, 2, 0x22);      /* $4000: ROM */
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0x4000);
    CHECK(memcmp(g_ram, before, sizeof before) == 0);

    fw_init(&f, g_ram, &g_img, NULL);
    fw_wr16(&f, ISR_FILL_PTR, 0x00E0);
    memcpy(before, g_ram, sizeof before);
    fw_wr_ind_y(&f, (uint8_t)ISR_FILL_PTR, 7, 0x33);      /* $E7: an SFR */
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0x00E7);
    CHECK(memcmp(g_ram, before, sizeof before) == 0);
}

/* util_shl3 $C657 (ASL A x 3) and util_shl4 $C656 (ASL A, then util_shl3): 8-bit, the high bits shifted out. */
static void test_shl(void)
{
    CHECK_EQ(util_shl3(0x01), 0x08);
    CHECK_EQ(util_shl3(0x10), 0x80);
    CHECK_EQ(util_shl3(0x21), 0x08);   /* $108: bit 8 lost */
    CHECK_EQ(util_shl4(0x01), 0x10);
    CHECK_EQ(util_shl4(0x0F), 0xF0);
    CHECK_EQ(util_shl4(0x11), 0x10);   /* $110: bit 8 lost */
    CHECK_EQ(util_shl4(0x80), 0x00);   /* the first ASL already loses it */
}

/* fw_fault: a fault at a ROM address where the ROM would never return (Tasks 2 and 3). Without a trap it returns
   with the fault set; the first fault's address is kept. */
static void test_fault(void)
{
    fw f;
    fw_init(&f, g_ram, &g_img, NULL);
    CHECK(!f.fault);
    fw_fault(&f, 0xC1A1);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xC1A1);
    fw_fault(&f, 0xBBA7);
    CHECK_EQ(f.fault_addr, 0xC1A1);
}

void suite_fw(void)
```

Replace the end of `suite_fw`

```c
    CHECK_EQ(LTS_MATCH_POS, 0x004F);
}
```

with

```c
    CHECK_EQ(LTS_MATCH_POS, 0x004F);

    test_sfr8();
    test_wr_ind_y();
    test_shl();
    test_fault();
}
```

(`test_wr_ind_y` clears `g_ram`; it runs after everything else of the suite. The four run last so the existing checks see the RAM they always saw.)

**`tests/test_machine_hooks.c`.** Replace

```c
#include "machine/machine.h"
```

with

```c
#include "machine/machine.h"
#include "engine/thunks.h"
```

Replace

```c
    CHECK_EQ(g_m.fault, MFAULT_NONE);
}

void suite_machine_hooks(void)
```

with

```c
    CHECK_EQ(g_m.fault, MFAULT_NONE);
}

/* The emulator answers the firmware's SFR reads (fw_sfr8, M4.4b spec 2.4) for UART_STATUS $E7 only, through the
   CPU's bus: transmitter ready (bit 1, always) and receive full (bit 0), and reading it changes nothing. Any other
   SFR reads $FF without reaching the bus: UART_RX $E5, whose read would clear receive full, keeps its byte. */
static void test_host_read_sfr(void)
{
    static fw_bridge b;
    load_main(0xC100);
    CHECK(fw_bridge_init(&b, &g_m));
    CHECK_EQ(fw_sfr8(&b.f, M50734_UART_STATUS), 0x02);
    CHECK(m50734_uart_receive(&g_m.sfr, 0x41));
    CHECK_EQ(fw_sfr8(&b.f, M50734_UART_STATUS), 0x03);
    CHECK_EQ(fw_sfr8(&b.f, M50734_UART_STATUS), 0x03);   /* still full: the read cleared nothing */
    CHECK(g_m.sfr.uart_rx_full);
    CHECK_EQ(fw_sfr8(&b.f, M50734_UART_RX), 0xFF);       /* not read: the received byte stays */
    CHECK(g_m.sfr.uart_rx_full);
    CHECK_EQ(fw_sfr8(&b.f, M50734_UART_STATUS), 0x03);
    CHECK_EQ(g_m.unmodelled_count, 0);
    CHECK(!b.f.fault);
}

void suite_machine_hooks(void)
```

(the replaced block is the end of `test_rom_call_unwound`, the only place `CHECK_EQ(g_m.fault, MFAULT_NONE);` is followed by `}` and the suite), and replace

```c
    test_rom_call_real_rom();
}
```

with

```c
    test_rom_call_real_rom();
    test_host_read_sfr();
}
```

**`tests/test_firmware_data.c`.** Replace

```c
static const range k_rom[] = { { 0x4000, 0x8FF4 }, { 0x9804, 0xA397 }, { 0xACC7, 0xACE8 }, { 0xB773, 0xB77D },
                               { 0xB7A7, 0xB7B1 }, { 0xC67B, 0xC9B8 }, { 0xD44A, 0xF77B } };
```

with

```c
static const range k_rom[] = { { 0x4000, 0x8FF4 }, { 0x9804, 0xA397 }, { 0xACC7, 0xACE8 }, { 0xB773, 0xB77D },
                               { 0xB797, 0xB7A1 }, { 0xB7A7, 0xB7B1 }, { 0xC67B, 0xC9B8 }, { 0xD44A, 0xF77B } };
```

Replace

```c
    CHECK_EQ(fw_frm_snapshot_map[0], FRAME_PITCH);
```

with

```c
    CHECK_EQ(fw_frm_snapshot_map[0], FRAME_PITCH);
    CHECK_EQ(fw_par_slot_map[0], FRAME_FN);                  /* P1 */
    CHECK_EQ(fw_par_slot_map[9], FRAME_V);                   /* P10 */
```

Replace

```c
    CHECK_EQ(compared, 47692);   /* M4.0's 35,214, M4.1's phonetic texts (97), M4.2's command data (567),
                                    M4.3a's prosody data (2,319) and M4.4a's playback data (9,495) */
```

with

```c
    CHECK_EQ(compared, 47702);   /* M4.0's 35,214, M4.1's phonetic texts (97), M4.2's command data (567),
                                    M4.3a's prosody data (2,319), M4.4a's playback data (9,495) and M4.4b's
                                    par_slot_map (10) */
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw"`
Expected: the build stops with compile errors (the proof run, in this order):

```
tests\test_firmware_data.c(94): error C2065: 'fw_par_slot_map': undeclared identifier
tests\test_fw.c(54): error C2220: the following warning is treated as an error
tests\test_fw.c(54): warning C4013: 'fw_sfr8' undefined; assuming extern returning int
tests\test_fw.c(64): error C2078: too many initializers
tests\test_fw.c(91): warning C4013: 'fw_wr_ind_y' undefined; assuming extern returning int
tests\test_fw.c(119): warning C4013: 'util_shl3' undefined; assuming extern returning int
tests\test_fw.c(122): warning C4013: 'util_shl4' undefined; assuming extern returning int
tests\test_fw.c(135): warning C4013: 'fw_fault' undefined; assuming extern returning int
tests\test_machine_hooks.c(391): warning C4013: 'fw_sfr8' undefined; assuming extern returning int
```

(plus `C2109` for each `fw_par_slot_map` subscript and a second `C2078`).

- [ ] **Step 3: `fw_sfr8`, `fw_wr_ind_y`, `fw_fault` and the host's `read_sfr` (`fw.h`, `fw.c`)**

`src/firmware/fw.h`: replace

```c
    /* P2 bit 7, the lower-ROM window, as SEB7/CLB7 $F0 set and clear it. */
    void (*set_low_window)(void *ctx, bool on);
} fw_host;
```

with

```c
    /* P2 bit 7, the lower-ROM window, as SEB7/CLB7 $F0 set and clear it. */
    void (*set_low_window)(void *ctx, bool on);
    /* Reads an SFR ($DA-$FF) as the CPU would, for the registers the firmware polls (UART_STATUS $E7). */
    uint8_t (*read_sfr)(void *ctx, uint8_t addr);
} fw_host;
```

and replace

```c
uint8_t fw_ind_y(fw *f, uint8_t zp, uint8_t y);
```

with

```c
uint8_t fw_ind_y(fw *f, uint8_t zp, uint8_t y);
/* STA (zp),Y: writes v at the little-endian pointer in zero page zp, plus y. A target that is not RAM ($4000 and
   above, or an SFR $DA-$FF) is a fault and nothing is written. */
void fw_wr_ind_y(fw *f, uint8_t zp, uint8_t y, uint8_t v);
/* An SFR byte, read through the host. Without a host or read_sfr, or for an address below $DA, a fault: it
   longjmps to f->trap when set, else reads $FF. */
uint8_t fw_sfr8(fw *f, uint8_t addr);
/* A fault at addr where the ROM would never return (a loop with no exit): it longjmps to f->trap when set, else
   returns with f->fault and f->fault_addr set, like a fault of fw_rom8. */
void fw_fault(fw *f, uint16_t addr);
```

`src/firmware/fw.c`: replace

```c
uint8_t fw_ind_y(fw *f, uint8_t zp, uint8_t y)
{
    return fw_mem8(f, (uint16_t)(fw_rd16(f, zp) + y));
}
```

with

```c
uint8_t fw_ind_y(fw *f, uint8_t zp, uint8_t y)
{
    return fw_mem8(f, (uint16_t)(fw_rd16(f, zp) + y));
}

void fw_wr_ind_y(fw *f, uint8_t zp, uint8_t y, uint8_t v)
{
    uint16_t addr = (uint16_t)(fw_rd16(f, zp) + y);
    if (addr >= FW_RAM_SIZE || (addr >= 0xDA && addr <= 0xFF)) {
        fault(f, addr);
        return;
    }
    f->ram[addr] = v;
}

uint8_t fw_sfr8(fw *f, uint8_t addr)
{
    if (addr >= 0xDA && f->host && f->host->read_sfr)
        return f->host->read_sfr(f->host->ctx, addr);
    fault(f, addr);
    return 0xFF;
}

void fw_fault(fw *f, uint16_t addr) { fault(f, addr); }
```

`src/engine/thunks.c`: replace

```c
bool fw_bridge_init(fw_bridge *b, machine *m)
```

with

```c
/* The firmware's SFR read. Only UART_STATUS $E7, the one register it polls (par_play $B895), is read, as the CPU's
   LDA reads it, through the bus; it has no side effect (m50734.c: bit 1, transmitter ready, always set; bit 0,
   receive full). Any other SFR reads $FF without reaching the bus, so that no read side effect (UART_RX $E5 clears
   receive full) can happen behind the ROM's back. */
static uint8_t host_read_sfr(void *ctx, uint8_t addr)
{
    machine *m = ((fw_bridge *)ctx)->m;
    return addr == M50734_UART_STATUS ? m->bus.read(m->bus.ctx, addr) : 0xFF;
}

bool fw_bridge_init(fw_bridge *b, machine *m)
```

and replace

```c
    b->host.set_low_window = host_set_low_window;
```

with

```c
    b->host.set_low_window = host_set_low_window;
    b->host.read_sfr = host_read_sfr;
```

- [ ] **Step 4: `util_shl3` and `util_shl4` in `fw_ops.h`**

`src/firmware/fw_ops.h`: replace

```c
/* Small steps the ported routines share (M4.4a spec 2.6). */
```

with

```c
/* Small steps the ported routines share (M4.4a spec 2.6), and the two shift routines (M4.4b spec 2.2). */
```

and replace

```c
    fw_wr8(f, (uint16_t)(arr + x), (uint8_t)(fw_rd8(f, (uint16_t)(arr + x)) + n));
}

#endif
```

with

```c
    fw_wr8(f, (uint16_t)(arr + x), (uint8_t)(fw_rd8(f, (uint16_t)(arr + x)) + n));
}

/* util_shl3 $C657: ASL A three times, A * 8 (8-bit). */
static inline uint8_t util_shl3(uint8_t a) { return (uint8_t)(a << 3); }
/* util_shl4 $C656: one ASL, then falls into util_shl3: A * 16 (8-bit). No caller reads the carry (M4.4b spec 2.6). */
static inline uint8_t util_shl4(uint8_t a) { return util_shl3((uint8_t)(a << 1)); }

#endif
```

`src/firmware/cmd_match.h`: replace

```c
#include "fw.h"

static inline uint8_t util_shl4(uint8_t a) { return (uint8_t)(a << 4); }   /* ROM util_shl4 $C656, inlined */

```

with

```c
#include "fw.h"

```

The three users include `fw_ops.h`. In `src/firmware/cmd_apply.c`, `src/firmware/cmd_match.c` and `src/firmware/cmd_set.c` (one occurrence in each), replace

```c
#include "cmd_match.h"
```

with

```c
#include "cmd_match.h"
#include "fw_ops.h"
```

Check: `grep -rn "util_shl4" src/firmware` prints seven lines: the comment and the inline in `fw_ops.h` (lines 31, 32) and the five calls (`cmd_apply.c:22`, `:23`, `cmd_match.c:19`, `cmd_set.c:59`, `:100`); nothing in `cmd_match.h`. (After Step 6 the two `par_addr.h` comments that name `util_shl4` are added to that list.)

- [ ] **Step 5: The `waits` field and `diff_lts`**

`src/engine/thunks.h`: replace

```c
    machine_hook_fn thunk;    /* user = the fw_bridge bound to that machine */
} fw_replacement;
```

with

```c
    machine_hook_fn thunk;    /* user = the fw_bridge bound to that machine */
    bool waits;               /* waits on the timer interrupt (the frame loop): checked by diff_play, not diff_lts */
} fw_replacement;
```

(The 48 rows keep their four initialisers: `waits` is false. Task 5's two rows end with `, true`.)

`tests/test_diff_lts.c`:
- replace

```c
   compared like any other (M4.2 spec 3) and counted: bank_select must produce some (M4.2 spec 5.5). */
```

  with

```c
   compared like any other (M4.2 spec 3) and counted: bank_select must produce some (M4.2 spec 5.5). The
   replacements marked waits (the frame loop, which waits on the timer interrupt) cannot run with interrupts held:
   they are not observed here but in diff_play (M4.4b spec 4.1, 4.2). */
```

- replace `#define DIFF_MAX        48` with `#define DIFF_MAX        64`;
- in `install`, replace

```c
    for (size_t i = 0; i < fw_replacement_count; i++) {
        g_obs[i].r = &fw_replacements[i];
```

  with

```c
    for (size_t i = 0; i < fw_replacement_count; i++) {
        if (fw_replacements[i].waits)
            continue;   /* diff_play's */
        g_obs[i].r = &fw_replacements[i];
```

- in `suite_diff_lts`, replace

```c
    for (size_t i = 0; i < fw_replacement_count; i++) {
        printf("  %-20s %u calls checked\n", fw_replacements[i].name, g_checked[i]);
```

  with

```c
    for (size_t i = 0; i < fw_replacement_count; i++) {
        if (fw_replacements[i].waits)
            continue;
        printf("  %-20s %u calls checked\n", fw_replacements[i].name, g_checked[i]);
```

  (the `CHECK(g_checked[i] > 0)` after the `printf` is skipped with it).

- [ ] **Step 6: The frame loop's ROM addresses in `par_addr.h`; `ROM_BOOT_LOOP_PLAY` moved there**

Replace the whole of `src/firmware/par_addr.h` with (the existing lines are unchanged except the header comment and the `ROM_FRM_COPY_REST_FRAME` / `ROM_FRM_CLAMP_MARK_SPACE` comments, where `par_play` leaves the parentheses because it becomes C in Task 5):

```c
#ifndef FW_PAR_ADDR_H
#define FW_PAR_ADDR_H
/* ROM code addresses of the playback set-up (M4.4a spec 2.2) and the frame loop (M4.4b spec 2.2), and of the
   frame-builder, interrupt and serial routines they call (M4.4a 2.4, M4.4b 2.4), checked against
   build/ghidra740/dump and the JSR/JMP operands in the ROM image. A caller in parentheses stays ROM. */
#define ROM_BOOT_LOOP_PLAY         0xA7D4   /* called by tn_convert_clause $B204 (and boot_reset $A687,
                                               boot_selftest_sound $A7BA, cmd_say_phonetic $A87C) */
#define ROM_ISR_SET_VOLUME         0xACE8   /* M4.6; called by boot_loop_play $B2C4, $B2D2, cmd_ev_set_volume $BCEF
                                               (and boot_reset $A642, isr_volume_half $B46F) */
#define ROM_ISR_SPEECH_START       0xB323   /* M4.6; called by boot_loop_play $B2B9 */
#define ROM_ISR_SPEECH_STOP        0xB398   /* M4.6; called by boot_loop_play $B2D5 */
#define ROM_PAR_SETUP_VOICE        0xB3A4   /* called by cmd_apply_pitch $A845; the C cmd_apply_pitch calls the C */
#define ROM_FRM_BUILD_REST_FRAME   0xB41D   /* M4.5; called by par_setup_voice $B404 */
#define ROM_FRM_COPY_REST_FRAME    0xB445   /* M4.5; called by par_setup_voice $B407, $B416, par_play $B8DA */
#define ROM_ISR_WAIT_FRAME         0xB465   /* M4.6; called by boot_loop_play $B2BF, $B2CD, par_play $B8D2 (and
                                               isr_wait_frames $B356) */
#define ROM_ISR_VOLUME_HALF        0xB46C   /* M4.6; called by boot_loop_play $B2BC, $B2CA */
#define ROM_PAR_PLAY               0xB877   /* called by boot_loop_play $B2C7; re-entered by its own JMPs $B892,
                                               $B8AE, $B914 */
#define ROM_FRM_SNAPSHOT           0xB917   /* M4.5; called by par_play $B8DD (and frm_finish_frame $B9BA) */
#define ROM_FRM_CLAMP_MARK_SPACE   0xB930   /* M4.5; called by par_setup_voice $B40A, $B419, par_play $B8E0 (and
                                               frm_finish_frame $B9BD) */
#define ROM_PROS_MICROPROSODY      0xB983   /* called by par_read_segment $BD67 */
#define ROM_FRM_FINISH_FRAME       0xB9B5   /* M4.5; called by par_play $B8C3, $B90E */
#define ROM_FRM_TRANSITION_FLAGS   0xBA3F   /* M4.5; called by par_play $B8E3 (and frm_finish_frame $BA36) */
#define ROM_PAR_PLAY_START         0xBAF2   /* called by boot_play_body $B2B6 */
#define ROM_PAR_RESET_TRACKS       0xBB35   /* called by par_play_start $BAF2 */
#define ROM_CMD_RUN_EVENT          0xBB74   /* called by par_play $B88F */
#define ROM_PAR_NEXT_SEGMENT       0xBCF3   /* called by par_play $B900 */
#define ROM_PAR_NEXT_ROW           0xBD07   /* called by par_setup_voice $B3BE, $B3C7; par_boundary_cur_dom $BF09,
                                               $BF14; par_boundary_next_dom $BF4E, $BF59; par_read_dominance $BF7B,
                                               $BF8D, $BFBF */
#define ROM_PAR_NEXT_PARAM         0xBD14   /* called by frm_build_rest_frame $B431, par_read_segment $BD9D,
                                               par_boundary_jump $BEAC */
#define ROM_PAR_CLAMP              0xBD24   /* called by frm_raise_formants $B970, $B97D; par_read_segment $BD8F;
                                               par_read_dominance $BF75; par_boundary_value $C0BF */
#define ROM_PAR_READ_SEGMENT       0xBD33   /* called by par_next_segment $BCFD */
#define ROM_PROS_SCALE_TARGET      0xBDD3   /* called by par_play_start $BAFE, par_read_segment $BD64 */
#define ROM_PAR_BOUNDARY_PASS      0xBE6A   /* called by par_next_segment $BD00 */
#define ROM_PAR_BOUNDARY_JUMP      0xBE94   /* called by par_boundary_pass $BE7F */
#define ROM_PAR_STORE_BOUNDARY     0xBEB5   /* called by par_boundary_jump $BEA9, par_boundary_value $C0AE */
#define ROM_PAR_BOUNDARY_CUR_DOM   0xBEE2   /* called by par_boundary_pass $BE90 */
#define ROM_PAR_BOUNDARY_NEXT_DOM  0xBF27   /* called by par_boundary_pass $BE8C */
#define ROM_PAR_READ_DOMINANCE     0xBF6C   /* called by par_boundary_cur_dom $BEFE, par_boundary_next_dom $BF43 */
#define ROM_PAR_SCALE_VOICING      0xBFC3   /* called by par_read_segment $BD96, par_read_dominance $BF87 */
#define ROM_PAR_BOUNDARY_VALUE     0xC039   /* called by par_boundary_cur_dom $BF19, par_boundary_next_dom $BF5E */
#define ROM_PAR_PLAN_TRACKS        0xC0D2   /* called by par_next_segment $BD03 */
#define ROM_PAR_STEP_TRACKS        0xC0FF   /* called by par_play $B8C0, $B90B */
#define ROM_PAR_TRACK_SEQUENTIAL   0xC14C   /* called by par_step_tracks $C10C */
#define ROM_UTIL_INTERP_STEP       0xC1A1   /* called by par_step_tracks $C124; par_track_sequential $C163, $C195;
                                               par_track_overlap $C210; par_track_start $C265; par_track_end $C28E */
#define ROM_PAR_TRACK_OVERLAP      0xC1F2   /* called by par_step_tracks $C106 */
#define ROM_PAR_TRACK_START        0xC24A   /* called by par_track_overlap $C1F2 */
#define ROM_PAR_TRACK_END          0xC26C   /* called by par_track_overlap $C1F8 */
#define ROM_PAR_SCALE_DURATION     0xC29C   /* called by par_boundary_cur_dom $BF03, $BF0E; par_boundary_next_dom
                                               $BF48, $BF53 */
#define ROM_SER_TX_SERVICE         0xC581   /* M4.6; called by par_play $B89C (and boot_get_clause $A6D7) */
#define ROM_ISR_PAR_HOLD           0xC5DE   /* M4.6; called by boot_loop_play $B2B3 (and boot_reset $A66F,
                                               boot_get_clause $A6E4, boot_queue_string $C617) */
#define ROM_UTIL_SHL4              0xC656   /* called by cmd_apply_pitch $A835, $A83F; cmd_set_formant $A952;
                                               cmd_set_mark_space $A9AD; cmd_hex_byte $B455 (all C: util_shl4 in
                                               fw_ops.h) */
#define ROM_UTIL_SHL3              0xC657   /* called by cmd_ev_sound_start $BBA7 (and isr_set_volume $ACEC); also
                                               entered from util_shl4 $C656 by falling through */

#endif
```

`src/firmware/tn_convert.h`: delete the line

```c
#define ROM_BOOT_LOOP_PLAY      0xA7D4   /* play the utterance */
```

`src/firmware/tn_convert.c`: replace

```c
#include "lts.h"
```

with

```c
#include "lts.h"
#include "par_addr.h"
```

`tests/test_fw_tn.c`: replace

```c
#include "firmware/cmd_addr.h"
```

with

```c
#include "firmware/cmd_addr.h"
#include "firmware/par_addr.h"
```

`tests/test_diff_lts.c` (it included `tn_convert.h` only for `ROM_BOOT_LOOP_PLAY`): replace

```c
#include "firmware/tn_convert.h"
```

with

```c
#include "firmware/par_addr.h"
```

Check: `grep -rn "ROM_BOOT_LOOP_PLAY" src tests` prints the definition in `par_addr.h` and the five uses (`tn_convert.c:116`, `test_diff_lts.c:100`, `test_fw_tn.c:46`, `:222`, `:231`), and nothing in `tn_convert.h`.

- [ ] **Step 7: `par_slot_map` as readable C**

`src/firmware/data/fw_data.h`: replace

```c
#define FRM_SNAPSHOT_MAP    0xB773   /* byte[10]: frame byte offset per snapshot entry */
```

with

```c
#define FRM_SNAPSHOT_MAP    0xB773   /* byte[10]: frame byte offset per snapshot entry */
#define PAR_SLOT_MAP        0xB797   /* byte[10]: frame byte per parameter P1..P10 (read as $B796,X, X = 1-10) */
```

replace

```c
/* The bytes of a DSP frame (frame20: ghidra740/annotations/types.txt; parent spec 1.4): the values of
   frm_snapshot_map */
```

with

```c
/* The bytes of a DSP frame (frame20: ghidra740/annotations/types.txt; parent spec 1.4): the values of
   frm_snapshot_map and par_slot_map */
```

and replace

```c
extern const uint8_t fw_par_target_bias[10], fw_frm_snapshot_map[10];
```

with

```c
extern const uint8_t fw_par_target_bias[10], fw_frm_snapshot_map[10];
extern const uint8_t fw_par_slot_map[10];
```

`src/firmware/data/par_data.c`: replace its last lines

```c
    FRAME_ALF, FRAME_FN, FRAME_F1, FRAME_F2, FRAME_F3,
};
```

with

```c
    FRAME_ALF, FRAME_FN, FRAME_F1, FRAME_F2, FRAME_F3,
};

/* par_slot_map ($B797): per parameter P1..P10 (read as $B796,X with X = 1-10: par_track_sequential $C169,
   $C177, $C19B, par_track_overlap $C216) the frame byte its track writes through isr_fill_ptr. The pitch track
   writes b10 itself (par_step_tracks, LDY #10). $B796 is not read, and the six zero bytes after the map
   (unused_B7A1) are not converted. */
const uint8_t fw_par_slot_map[10] = {
    FRAME_FN,    /* P1 */
    FRAME_F1,    /* P2 */
    FRAME_F2,    /* P3 */
    FRAME_F3,    /* P4 */
    FRAME_ALF,   /* P5 */
    FRAME_A1,    /* P6 */
    FRAME_A2,    /* P7 */
    FRAME_A3,    /* P8 */
    FRAME_AHF,   /* P9 */
    FRAME_V,     /* P10 voicing */
};
```

`src/firmware/data/fw_data.c`: replace

```c
    /* 9. The playback data (M4.4a spec 3): the two voice tables, each after its count byte (step 7) and filling the
       space up to the next table; par_target_bias and frm_snapshot_map; the @T sound effects end to end in
       cmd_sound_data, exactly filling it, and cmd_sound_table computed from where their arrays landed. */
```

with

```c
    /* 9. The playback data (M4.4a spec 3): the two voice tables, each after its count byte (step 7) and filling the
       space up to the next table; par_target_bias, frm_snapshot_map and (M4.4b spec 3) par_slot_map; the @T sound
       effects end to end in cmd_sound_data, exactly filling it, and cmd_sound_table computed from where their
       arrays landed. */
```

and replace

```c
            put(&e, false, FRM_SNAPSHOT_MAP + (uint32_t)k, fw_frm_snapshot_map[k]);
```

with

```c
            put(&e, false, FRM_SNAPSHOT_MAP + (uint32_t)k, fw_frm_snapshot_map[k]);
            if (fw_par_slot_map[k] > FRAME_V)   /* b10, the pitch, is the pitch track's */
                fail(&e, "par_slot_map entry %d: frame byte %u", k + 1, (unsigned)fw_par_slot_map[k]);
            put(&e, false, PAR_SLOT_MAP + (uint32_t)k, fw_par_slot_map[k]);
```

- [ ] **Step 8: Run the tests**

Run: `cmd //c ".\build.bat test fw"` (the filter matches the six `fw` suites), then `build/apollo2_tests.exe machine_hooks` and `build/apollo2_tests.exe firmware_data`.
Expected: the build passes with no warnings (the firmware syntax check included); then

```
suite_fw                     ok
suite_fw_lts                 ok
suite_fw_tn                  ok
suite_fw_cmd                 ok
suite_fw_pros                ok
suite_fw_par                 ok
6 suite(s), 0 failure(s), 0 skip(s)
```

```
suite_machine_hooks          ok
1 suite(s), 0 failure(s), 0 skip(s)
```

```
  47702 converted bytes compared with the ROM, 0 differ
suite_firmware_data          ok
1 suite(s), 0 failure(s), 0 skip(s)
```

- [ ] **Step 9: Faithfulness check**

No ROM routine is ported in this task (the routines come in Tasks 2, 3 and 5). Check instead:
- `util_shl3`/`util_shl4` against `rom_C657_util_shl3.txt` (`ASL A` ×3, `RTS`) and `rom_C656_util_shl4.txt` (one `ASL A`, then the code of $C657): `util_shl4` is `util_shl3` of `a << 1`, 8-bit at each step, so it equals the old `(uint8_t)(a << 4)` for every byte (the five C callers are unchanged, Step 11 proves it on the corpus). No caller reads the carry (survey table §1.3, checked one level after each site).
- `fw_par_slot_map` against `data.txt` (line 461, `par_slot_map`: FN, F1, F2, F3 = b1, b3, b5, b7; ALF, A1, A2, A3, AHF = b0, b2, b4, b6, b8; voicing b9) and the ROM bytes ($B797: `01 03 05 07 00 02 04 06 08 09`); `firmware_data` compares all ten bytes with the ROM.
- `fw_wr_ind_y` against the 740's `STA (zp),Y` as `fw_ind_y` already models it (the 16-bit pointer plus Y, the carry into the high byte); the four `STA ($2C),Y` sites the tracks use (`rom_C14C_par_track_sequential.txt` $C16C, $C17D, $C19E; `rom_C1F2_par_track_overlap.txt` $C223, $C22B, $C236; `rom_C0FF_par_step_tracks.txt` $C12E) all go through `isr_fill_ptr` $2C.
- `fw_sfr8`: `par_play`'s only SFR read is `B895 BBC1 UART_STATUS,$B89F` (`rom_B877_par_play.txt`, `37E707`: bit 1 of $E7).
- `par_addr.h`: every address and caller against Step 10's scan and the dump each site lies in.

- [ ] **Step 10: The caller JSR/JMP scan for the new `par_addr.h` addresses**

Run (from the repo root, with `APOLLO2_ROM` set):

```bash
python -c "import re,os;d=open(os.environ['APOLLO2_ROM'],'rb').read();[print(hex(a),hex(op),[hex(m.start()) for m in re.finditer(re.escape(bytes([op,a&255,a>>8])),d)]) for a in (0xA7D4,0xACE8,0xB323,0xB398,0xB465,0xB46C,0xB877,0xB917,0xB9B5,0xBA3F,0xBB74,0xC0FF,0xC14C,0xC1A1,0xC1F2,0xC24A,0xC26C,0xC581,0xC5DE,0xC656,0xC657) for op in (0x20,0x4C)]"
```

Expected:

```
0xa7d4 0x20 ['0xa687', '0xa7ba', '0xa87c', '0xb204']
0xa7d4 0x4c []
0xace8 0x20 ['0xa642', '0xb2c4', '0xb2d2', '0xb46f', '0xbcef']
0xace8 0x4c []
0xb323 0x20 ['0xb2b9']
0xb323 0x4c []
0xb398 0x20 ['0xb2d5']
0xb398 0x4c []
0xb465 0x20 ['0xb2bf', '0xb2cd', '0xb356', '0xb8d2']
0xb465 0x4c []
0xb46c 0x20 ['0xb2bc', '0xb2ca']
0xb46c 0x4c []
0xb877 0x20 ['0xb2c7']
0xb877 0x4c ['0xb892', '0xb8ae', '0xb914']
0xb917 0x20 ['0xb8dd', '0xb9ba']
0xb917 0x4c ['0xc0d7']
0xb9b5 0x20 ['0xb8c3', '0xb90e']
0xb9b5 0x4c []
0xba3f 0x20 ['0xb8e3', '0xba36']
0xba3f 0x4c []
0xbb74 0x20 ['0xb88f']
0xbb74 0x4c []
0xc0ff 0x20 ['0xb8c0', '0xb90b']
0xc0ff 0x4c []
0xc14c 0x20 ['0xc10c']
0xc14c 0x4c []
0xc1a1 0x20 ['0xc124', '0xc163', '0xc195', '0xc210', '0xc265', '0xc28e']
0xc1a1 0x4c []
0xc1f2 0x20 ['0xc106']
0xc1f2 0x4c []
0xc24a 0x20 ['0xc1f2']
0xc24a 0x4c []
0xc26c 0x20 ['0xc1f8']
0xc26c 0x4c []
0xc581 0x20 ['0xa6d7', '0xb89c']
0xc581 0x4c []
0xc5de 0x20 ['0xa66f', '0xa6e4', '0xb2b3', '0xc617']
0xc5de 0x4c []
0xc656 0x20 ['0xa835', '0xa83f', '0xa952', '0xa9ad', '0xb455']
0xc656 0x4c []
0xc657 0x20 ['0xacec', '0xbba7']
0xc657 0x4c []
```

`0xc0d7` is not a `JMP`: it is the operand byte $4C of `STA $174C,Y` at $C0D6 (`99 4C 17`, `rom_C0D2_par_plan_tracks.txt`). The routine around each site: `grep -l "^  <ADDR> " build/ghidra740/dump/rom_*.txt` (for example $B356 is the first instruction of `rom_B356_isr_wait_frames.txt`, $C617 lies in `rom_C612_boot_queue_string.txt`, $B2B3–$B2D5 in `rom_A7D4_boot_loop_play.txt`, whose body the dump labels `boot_play_body`).

- [ ] **Step 11: Run the full suite**

Run: `cmd //c ".\build.bat test"`
Expected (the proof run's output; 272 s, build included):
- `32 suite(s), 0 failure(s), 0 skip(s)`, no warnings, the firmware syntax check passing;
- `firmware_data`: `47702 converted bytes compared with the ROM, 0 differ`;
- `diff_lts`: 48 `N calls checked` lines, every one identical to the state before this task (`cmd_apply_speaker 101`, `cmd_run_due 5199`, `tn_convert_clause 224`, `par_setup_voice 113`, `par_play_start 173`, `par_next_segment 3391`, `par_scale_duration 25782`, `cmd_hex_value 144`, `pros_append_segment 2307`, `ph_lengthen_count 1139`, …), then `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`, 0 mismatches;
- `golden_hybrid`: `45 corpus files x 49 modes (all, then each routine alone)`;
- `suite_synth`: `corpus peak plain 20522 (-4.1 dBFS)`, `corpus peak b11 16038 (-6.2 dBFS)`.

If any `diff_lts` N or `golden_hybrid` result changes, the `util_shl4` move changed behaviour: compare with Step 9. Never loosen a comparison.

- [ ] **Step 12: Commit**

```bash
git add src/firmware/fw.h src/firmware/fw.c src/firmware/fw_ops.h src/firmware/cmd_match.h src/firmware/cmd_match.c src/firmware/cmd_apply.c src/firmware/cmd_set.c src/firmware/par_addr.h src/firmware/tn_convert.h src/firmware/tn_convert.c src/firmware/data/fw_data.h src/firmware/data/fw_data.c src/firmware/data/par_data.c src/engine/thunks.h src/engine/thunks.c tests/test_fw.c tests/test_machine_hooks.c tests/test_firmware_data.c tests/test_diff_lts.c tests/test_fw_tn.c
git commit -m "M4.4b infrastructure: the host's SFR read (fw_sfr8, read_sfr for UART_STATUS), fw_wr_ind_y, fw_fault, util_shl3/util_shl4 in fw_ops.h, the frame loop's ROM addresses in par_addr.h (ROM_BOOT_LOOP_PLAY moved there), the waits flag that diff_lts skips, DIFF_MAX 64; par_slot_map converted (M4.4b 2.2, 2.4, 2.5, 3)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 13: Write the corpus text**

One line ending with a single LF, written by script so the bytes are exact (the cycle is the seven words `a go i to it o at`, as in M4.3a's `seg_*` texts; `w(56)` is the cycle exactly eight times):

```bash
python - <<'EOF'
cyc = "a go i to it o at".split()
w = lambda n: " ".join(cyc[i % 7] for i in range(n))
open("tests/corpus/late_event.txt", "w", newline="\n").write("@Q9 " + w(56) + " @B5 oh.\n")
EOF
wc -c tests/corpus/late_event.txt
```

Expected `wc -c`: `156 tests/corpus/late_event.txt`. The line, for review:

```
@Q9 a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at a go i to it o at @B5 oh.
```

It is `seg_256` (`@Q9 ` + the first 54 words + ` oh.`) with the cycle's next two words `o at` and `@B5` before `oh`. The @B5 event lands at segment 259 ($0103): while `par_seg_index` is $0003 its low byte matches `cmd_ev_pos`, and the `BNE` at $B88D (high bytes 0 against 1) is taken.

- [ ] **Step 14: Add it to the corpus list**

In `tests/corpus_list.h`:
- replace

```c
   The last three (M4.4a) reach the playback set-up's rare paths: @R7 with the pitch capped at $FF under @FF and
```

  with

```c
   The next three (M4.4a) reach the playback set-up's rare paths: @R7 with the pitch capped at $FF under @FF and
```

- replace

```c
   duration 40 the speed map caps at 31. */
```

  with

```c
   duration 40 the speed map caps at 31.
   The last one (M4.4b) is seg_256 two words longer, with a @B5 before its last word: an event at segment 259
   ($0103), whose low byte par_play meets first on page 0 (the high-byte test at $B88D). */
```

- replace the last line of `CORPUS_NAMES`

```c
    "pitch_extremes", "voicing_levels", "fast_effect"
```

  with

```c
    "pitch_extremes", "voicing_levels", "fast_effect", \
    "late_event"
```

- [ ] **Step 15: Run the golden suites to see them fail**

Run: `cmd //c ".\build.bat test golden"`
Expected: FAIL, `2 suite(s), 50 failure(s), 0 skip(s)`:
- `suite_golden` prints `missing tests/golden/late_event.frm: run once with APOLLO2_UPDATE_GOLDEN=1 and review` (1 failure);
- `suite_golden_hybrid` prints `46 corpus files x 49 modes (all, then each routine alone)` and fails `tests\test_golden_hybrid.c:36: CHECK failed: want != NULL` 49 times.

- [ ] **Step 16: Generate the golden with the rom engine**

Run: `APOLLO2_UPDATE_GOLDEN=1 cmd //c ".\build.bat test golden"`
Expected: `2 suite(s), 0 failure(s), 0 skip(s)`, 46 `updated` lines (update mode rewrites every golden), among them

```
  updated tests/golden/late_event.frm (1845 frames)
```

Then run `git status --short tests/golden` and `git diff --stat tests/golden`. Expected: exactly `?? tests/golden/late_event.frm` from the first, nothing from the second. If any existing `.frm` shows as modified, STOP and report: nothing may be committed. If the frame count differs, STOP and check the text against Step 13 byte for byte.

- [ ] **Step 17: Confirm the coverage**

Run (from the repo root; the file goes under the git-ignored `build/`):

```bash
./build/apollo2_coverage.exe -o build/m44b.cov
for a in B88F B895 B89C B8A5 C271; do grep -q "^x rom $a$" build/m44b.cov && echo "$a ok" || echo "$a MISSING"; done
```

Expected: the summary `rom 4084, low 0, ramcode 116, ram 0 instruction addresses; 31 indirect edges`, unchanged from before this task, then `B88F ok`, `B895 ok`, `B89C MISSING`, `B8A5 MISSING`, `C271 MISSING` (spec §4.6: the reply service and the abort need serial input during playback, `diff_play` injects them in Task 4; $C271 is unreachable, Task 2's unit test pins it).

The coverage file has no branch directions, and the target of the `BNE` at $B88D, $B895, is also reached from $B87D and $B885, so `late_event` adds no instruction address (the file is byte-identical to the one before this task). The branch probe confirmed it while this plan was proved (ROM facts above: `BR B88D BNE $B895 taken 1134 fall 3` on `late_event`, `taken 0` on every other text); from Task 4 on, `diff_play` compares `par_play` on this very text.

- [ ] **Step 18: Run the full suite**

Run: `cmd //c ".\build.bat test"`
Expected:
- `32 suite(s), 0 failure(s), 0 skip(s)`;
- `golden_hybrid`: `46 corpus files x 49 modes (all, then each routine alone)`;
- `firmware_data`: `47702 converted bytes compared with the ROM, 0 differ`;
- `diff_lts`: 48 `N calls checked` lines, each N greater than 0 and 0 mismatches; `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`, unchanged. The counts grow with the new text (the proof run: before -> after, cmd_apply_speaker 101 -> 103, cmd_run_due 5199 -> 5501, cmd_scan_clause 225 -> 228, cmd_match_table 2895 -> 2957, lts_affix_next 4152 -> 4512, lts_sym_vowel 1209 -> 1241, lts_match_symbol 15488 -> 16161, lts_match_list 11671 -> 12144, tn_rule_skip_text 31490 -> 32498, tn_rule_skip_field 13841 -> 14361, tn_append_phonetic 893 -> 966, lts_convert_char 884 -> 957, tn_convert_clause 224 -> 227, par_setup_voice 113 -> 115, pros_microprosody 3391 -> 3666, par_play_start 173 -> 176, par_reset_tracks 173 -> 176, par_next_segment 3391 -> 3666, par_next_row 40696 -> 41827, par_next_param 30676 -> 31717, par_clamp 39664 -> 40771, par_read_segment 3391 -> 3666, pros_scale_target 3564 -> 3842, par_boundary_pass 3391 -> 3666, par_boundary_jump 1093 -> 1181, par_store_boundary 23447 -> 24474, par_boundary_cur_dom 833 -> 906, par_boundary_next_dom 1465 -> 1579, par_read_dominance 15982 -> 16996, par_scale_voicing 5689 -> 6151, par_boundary_value 15982 -> 16996, par_plan_tracks 3391 -> 3666, par_scale_duration 25782 -> 26825, cmd_hex_value 144 -> 147, ph_append_phrase_pause 360 -> 364, ph_append_pause 820 -> 921, pros_append_segment 2307 -> 2477, pros_reset_seg_ptrs 398 -> 404, pros_make_segments 225 -> 228, pros_mark_stress 225 -> 228, pros_mark_phrases 225 -> 228, pros_store_phrase_len 407 -> 412, pros_phrase_gen 225 -> 228, pros_declination 407 -> 412, pros_declination_step 407 -> 412, ph_lengthen_count 1139 -> 1253; only `lts_affix_prev` 1476 and `tn_say_char` 19 stay the same);
- `suite_synth`: `corpus peak plain 20522 (-4.1 dBFS)`, `corpus peak b11 16038 (-6.2 dBFS)` (unchanged). If it reports clipping, or a plain peak outside -12...-1 dBFS, STOP and report. Do not change any calibration.

- [ ] **Step 19: Commit**

```bash
git add tests/corpus/late_event.txt tests/golden/late_event.frm tests/corpus_list.h
git commit -m "Corpus: late_event, seg_256 two words longer with a @B5 before its last word, so a command event sits at segment 259 and par_play's high-byte test at \$B88D is taken; golden from the rom engine

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

(The `\$B88D` keeps Bash from expanding `$B88D` inside the double quotes; the message reads `$B88D`.)

---

### Task 2: The tracks in C: `util_interp_step` $C1A1, `par_track_start` $C24A, `par_track_end` $C26C, `par_track_sequential` $C14C, `par_track_overlap` $C1F2 and `par_step_tracks` $C0FF, with their hooks (spec §2.2, §2.3, §2.5, §2.6, §4.5)

**ROM facts checked** (`rom_C1A1_util_interp_step.txt`, `rom_C24A_par_track_start.txt`, `rom_C26C_par_track_end.txt`, `rom_C14C_par_track_sequential.txt`, `rom_C1F2_par_track_overlap.txt`, `rom_C0FF_par_step_tracks.txt`, each instruction by instruction; the callers `rom_B877_par_play.txt` ($B8C0, $B90B and what follows), `rom_B9B5_frm_finish_frame.txt`, `rom_B917_frm_snapshot.txt`, `rom_BB74_cmd_run_event.txt`, `rom_C581_ser_tx_service.txt`, `rom_C4B5_ser_tx_get.txt`, `rom_C504_ser_tx_send.txt`, `rom_C5D6_isr_par_ready.txt`; `rom_C0D2_par_plan_tracks.txt` for the ranges of `par_end_wait`; `data.txt` for the RAM names; the ROM image for $B796–$B7A1):
- **Sizes agree with spec §2.2** (81/43, 34/13, 48/19, 85/32, 88/36, 50/21 bytes/instructions). No second entry and no fall-through: the byte before each entry is an `RTS` ($60), except $C1F1 before `par_track_overlap`, which is the last byte of `JMP $C1CD` ($C1EF). The relative-branch scan's two hits ($C08F → $C0FF, $C133 → $C14C) are operand bytes, not instructions: $C08F is the $17 of `LDA $17DE` at $C08D (it matches the `BBC`/`BBS` pattern), $C133 the $F0 of `SBC $17F0` at $C132 (it matches `BEQ`).
- **`util_interp_step` reads `util_interp_steps` from RAM, not Y.** Y only picks the paths (1, 2, other). The ROM hangs exactly when `util_interp_steps` = 0 on the general path with a non-zero difference (`SBC #0` with the carry set never borrows, `ADC #0` with it clear never carries); every caller stores Y there first, so the C faults on `util_interp_steps` = 0 at that point, which is spec §2.6's "Y = 0 with A ≠ cur". Nothing is written before the hang, so the C writes nothing either and returns (with `f->trap` set it longjmps).
- **The halved count is not always written** (spec §2.6 says "then `LSR util_interp_steps`"): on the down path an exact quotient (the sum 0, `BEQ` at $C1E1) skips the `LSR`, so `util_interp_steps` keeps its value; the up path always halves it ($C1C3). `test_interp_step` and the sequential end-phase case pin both.
- **Exit Y of `util_interp_step`:** 0 for Y = 1 and Y = 2; Y − 2 for equal values (so $FE for Y = 0); else the quotient (`util_interp_quot`). The up quotient counts `INY` including the borrowing subtraction, then `DEY` when the remainder plus half the steps has bit 7 set (the `BPL` at $C1CA tests the 8-bit sum's N, not a carry); the down quotient counts `DEY` including the carrying addition, then `INY` when the remainder minus half the steps is above 0 with bit 7 clear (`BMI`, `BEQ`).
- **Every track step is the same 7-instruction pattern** (`STY util_interp_steps`, `DEC dur,X`, `LDA val,X`, `STA util_interp_cur`, `LDA target,X`, `STA util_interp_target`, `JSR util_interp_step`, `STA val,X`) at $C151, $C183, $C253, $C27C; the overlap line ($C201) and the pitch ($C115, absolute) have no `DEC`. The result is always stored back where the current value came from (`par_start_val`, `par_seg_target` for the sequential end phase, `par_end_val`, `par_line_val`, `pros_play_pitch`). The C has one helper, `track_step`, for the six copies.
- **`par_track_end`'s exit Y** is the wait count before the `DEC` while waiting ($C26C `LDY`, $C295 `DEC`), 0 after a negative count is zeroed ($C271), else `util_interp_step`'s. The zeroing path continues through the `BNE` at $C277 with the Z of the `TYA`, i.e. to the step (spec: confirmed).
- **`par_track_sequential`'s hold** loads Y ($C177 `LDY $B796,X`) before A ($C17A); both are reads without side effects, so the C's `slot_store` after `hold` is equivalent. Its end phase steps `par_seg_target[X]` itself (cur = `par_seg_target`, stored back at $C198) towards `par_boundary[X]`, with `par_end_dur` decremented unguarded (it wraps past 0; Y = 0 then takes the equal or the faulting path).
- **`par_track_overlap`'s median** (all six outcomes traced at $C219–$C249): with L the line, S `par_start_track`, E `par_end_track` and "a < b" meaning bit 7 of the 8-bit `a − b`: L ≥ S and L < E, or L < S and L ≥ E: L; L ≥ S, L ≥ E: E when S < E ($C245 `BMI`), else S; L < S, L < E: E when S ≥ E ($C234 `BPL`), else S. X is never changed, Y = `par_slot_map[X − 1]` from $C216 on every path.
- **`par_slot_map`**: the reads are `LDY $B796,X` at $C169, $C177, $C19B and $C216 with X = 1–10 (the C reads `PAR_SLOT_MAP − 1 + X` through `fw_rom8`, Task 1's conversion of $B797–$B7A0 = `01 03 05 07 00 02 04 06 08 09`). The pitch's frame byte is `LDY #$0A` (the C writes `FRAME_PITCH`, 10).
- **`par_step_tracks`**: `LDX #$0A`, `LDA par_sequential,X` / `BNE` (sequential when non-zero), `DEX` / `BNE`; exit A = `pros_play_pitch` >> 2, X = 0, Y = 10.
- **What the callers read after each call** (the thunk comments; no caller reads a flag, V is never tested):
  - `util_interp_step`: A at every site ($C166/$C198 `STA ...,X`, $C213, $C268, $C291), X at all but $C124 (pitch: `STA pros_play_pitch`, `LSR`, `LSR`, `LDY #$0A`, as facts.md §0 item 5 says); Y is loaded before it is read on every path.
  - `par_track_start` / `par_track_end`: A ($C1F5 / $C1FB `STA`) and X (the next `LDY $176C,X` at $C26C / `LDA $170C,X` at $C204).
  - `par_track_sequential` / `par_track_overlap`: X (`DEX` at $C10F); A and Y are loaded first ($C101, $C112, $C118).
  - `par_step_tracks`: nothing. `frm_finish_frame` pushes A, X, Y ($B9B5–$B9B9) and its first callee `frm_snapshot` loads Y and X first ($B917, $B922); it restores them ($BA39–$BA3D), and par_play then runs `LDM`, `isr_par_ready` (`PHA` … `PLA`), `isr_wait_frame`, `LDX cmd_ev_next` ($B877), or `ser_tx_service` / `ser_tx_get` / `ser_tx_send` (which load Y before reading it), or `cmd_run_event` (`LDA` / `TAX` first), none reading a register first.
- **Reach in `golden_hybrid`'s "all" mode in this task:** `par_step_tracks` fires (its caller `par_play` is ROM until Task 5); the other five are called C to C from it and fire only in their "alone" modes. After Task 5 `par_step_tracks` is called C to C too. `diff_lts` observes on the ROM machine, so it checks all six at every ROM call site (the counts below).
- **The zero-step fault** goes through Task 1's `fw_fault(f, ROM_UTIL_INTERP_STEP)`: with a trap (every thunk) it longjmps out, without one (the unit tests) `util_interp_step` returns with nothing written.
- **Proof run** (the consolidated plan applied literally in a scratch worktree off main c5ca8fa, after Task 1): RED `tests\test_fw_par.c(14): fatal error C1083: Cannot open include file: 'firmware/par_track.h': No such file or directory`; then `suite_fw_par ok`; both scans of Step 4 as printed there; the full run as Step 5 says. While drafting (scratch only, not part of the plan): each of the 32 non-faulting calls the new tests make was also run through the ROM routine in the emulator from the same RAM and registers (interrupts held), with RAM $0000–$3FFF (but the SFRs and the stack page) and the exit A, X and Y identical to the C in all 32; and each of these mutations failed `fw_par`: a true signed compare in the median (4 failures), `round_down` without the `BEQ` (6), `round_up` without storing the halved count (4), halving on an exact down quotient (2), no zeroing of a negative `par_end_wait` (8), the zeroing without its store (1).

**Files:**
- Create: `src/firmware/par_track.h`, `src/firmware/par_track.c`
- Modify: `src/engine/thunks.c` (an include, six thunks, `THUNK` lines and rows), `tests/test_fw_par.c` (an include, the track tests)

**Where the code goes.** `src/firmware/par_track.c`, one public function per routine plus `static` helpers, one per ROM block:

| Helper | ROM |
|---|---|
| `average` | $C1A8–$C1AD, Y = 2: `CLC; ADC util_interp_cur; ROR A` |
| `quotient_up` | $C1BA–$C1C1, the `INY; SEC; SBC` loop (faults on 0 steps) |
| `round_up` | $C1C3–$C1CC, `LSR util_interp_steps`, the `BPL` / `DEY` |
| `quotient_down` | $C1D8–$C1DF, the `DEY; CLC; ADC` loop (faults on 0 steps) |
| `round_down` | $C1E3–$C1EF, `LSR`, `SEC; SBC`, `BMI` / `BEQ` / `INY` |
| `add_quotient` | $C1CD–$C1D7, `util_interp_quot` and `cur + quot` |
| `track_step` | the six step copies $C151–$C166, $C183–$C198, $C201–$C213, $C253–$C268, $C27C–$C291, $C115–$C127 |
| `slot_store` | `LDY $B796,X; STA ($2C),Y` ($C169–$C16C, $C177/$C17D, $C19B–$C19E) |
| `zero_wait` | $C271–$C276, the negative `par_end_wait` zeroed |
| `end_wait` | $C295–$C29B, the wait counted down |
| `hold` | $C174–$C17A, `par_steady` counted down, `par_seg_target` held |
| `minus`, `median` | $C219–$C249 |
| `pitch_track` | $C112–$C130 |

The entries and dispatches ($C1A1–$C1B8, $C24A–$C252, $C26C–$C27B, $C14C–$C150 with $C16F–$C173 and $C180, $C1F2–$C200 with $C216, $C0FF–$C111) are in the public functions.

**Interfaces:**
- Consumes:
  - Task 1: `fw_wr_ind_y` and `fw_fault` (`fw.h`); in `src/firmware/par_addr.h` `ROM_PAR_STEP_TRACKS` 0xC0FF, `ROM_PAR_TRACK_SEQUENTIAL` 0xC14C, `ROM_UTIL_INTERP_STEP` 0xC1A1, `ROM_PAR_TRACK_OVERLAP` 0xC1F2, `ROM_PAR_TRACK_START` 0xC24A, `ROM_PAR_TRACK_END` 0xC26C; in `src/firmware/data/fw_data.h` `PAR_SLOT_MAP` 0xB797 and `extern const uint8_t fw_par_slot_map[10]`, converted into the image (the C reads it through `fw_rom8`, which faults on an unconverted byte; `firmware_data` at 47702); `DIFF_MAX` 64 (54 replacements need 54 observers); the `late_event` corpus text and golden (46 corpus files; the counts below assume it). The `waits` field is not used here (the six rows keep four initialisers).
  - M4.4a: the harness of `tests/test_fw_par.c` (`g_ram`, `g_f`, `g_ncalls`, `setup()`, `check_row()`, `snapshot()`, `check_written()`, `check_no_calls()`); `FRAME_*` (`fw_data.h`); the thunk style of `src/engine/thunks.c` with the `par_plan_tracks` and `par_scale_duration` rows.
  - Existing names (`fw_ram.h`): `ISR_FILL_PTR`, `ISR_FRAME_A`, `ISR_FRAME_B`, `PAR_BOUNDARY`, `PAR_START_VAL`, `PAR_LINE_VAL`, `PAR_START_DUR`, `PAR_END_DUR`, `PAR_SEQUENTIAL`, `PAR_END_VAL`, `PAR_END_WAIT`, `PAR_STEADY`, `PAR_SEG_TARGET`, `PAR_FRAMES_LEFT`, `UTIL_INTERP_QUOT`, `UTIL_INTERP_STEPS`, `UTIL_INTERP_CUR`, `UTIL_INTERP_TARGET`, `PAR_START_TRACK`, `PAR_END_TRACK`, `PROS_PLAY_TARGET`, `PROS_PLAY_PITCH`.
- Produces (Task 5's C `par_play` calls `par_step_tracks(f, r)` with its own `r`):

```c
/* src/firmware/par_track.h */
void util_interp_step(fw *f, fw_regs *r);      /* $C1A1: A, Y in; A = the value, X kept, Y = 0 / Y - 2 / quotient */
void par_track_start(fw *f, fw_regs *r);       /* $C24A: X in; A = the value, X kept */
void par_track_end(fw *f, fw_regs *r);         /* $C26C: X in; A = the value, X kept */
void par_track_sequential(fw *f, fw_regs *r);  /* $C14C: X in; A = the frame byte, X kept, Y = its index */
void par_track_overlap(fw *f, fw_regs *r);     /* $C1F2: X in; A = the frame byte, X kept, Y = its index */
void par_step_tracks(fw *f, fw_regs *r);       /* $C0FF: A = pitch >> 2, X = 0, Y = 10 */
```

  - The replacements `par_step_tracks` 0xC0FF, `par_track_sequential` 0xC14C, `util_interp_step` 0xC1A1, `par_track_overlap` 0xC1F2, `par_track_start` 0xC24A and `par_track_end` 0xC26C, all `FW_OUT_A | FW_OUT_X | FW_OUT_Y`, between the `par_plan_tracks` and `par_scale_duration` rows: **54 replacements, 55 `golden_hybrid` modes, 54 `diff_lts` lines**.
  - `tests/test_fw_par.c`: `test_step_tracks();` as the last line of `suite_fw_par` (Task 3's anchor), and `fill_frame_b()` and `check_interp_state()` for later tasks.

- [ ] **Step 1: Write the failing tests**

In `tests/test_fw_par.c`, replace

```c
#include "firmware/par_start.h"
```

with

```c
#include "firmware/par_start.h"
#include "firmware/par_track.h"
```

Replace

```c
void suite_fw_par(void)
{
```

with

```c
/* ---- the tracks (M4.4b Task 2) ---- */

static void test_track_rows(void)
{
    check_row("par_step_tracks", ROM_PAR_STEP_TRACKS, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_track_sequential", ROM_PAR_TRACK_SEQUENTIAL, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("util_interp_step", ROM_UTIL_INTERP_STEP, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_track_overlap", ROM_PAR_TRACK_OVERLAP, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_track_start", ROM_PAR_TRACK_START, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_track_end", ROM_PAR_TRACK_END, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

/* One util_interp_step call: A = a, Y = y, X = $5A, util_interp_cur = cur, util_interp_steps = steps,
   util_interp_target = $77 (so a return of it shows), util_interp_quot = $EE. Out: A, Y, and util_interp_steps and
   util_interp_quot as the ROM leaves them ($EE: not written); X untouched, nothing else written, no fault. */
static void interp_case(uint8_t a, uint8_t y, uint8_t cur, uint8_t steps, uint8_t want_a, uint8_t want_y,
                        uint8_t want_steps, uint8_t want_quot)
{
    static const uint16_t w[] = { UTIL_INTERP_STEPS, UTIL_INTERP_QUOT };
    setup();
    g_ram[UTIL_INTERP_CUR] = cur;
    g_ram[UTIL_INTERP_STEPS] = steps;
    g_ram[UTIL_INTERP_TARGET] = 0x77;
    g_ram[UTIL_INTERP_QUOT] = 0xEE;
    snapshot();
    fw_regs r = { a, 0x5A, y, 0 };
    util_interp_step(&g_f, &r);
    CHECK_EQ(r.a, want_a);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, want_y);
    CHECK_EQ(g_ram[UTIL_INTERP_STEPS], want_steps);
    CHECK_EQ(g_ram[UTIL_INTERP_QUOT], want_quot);
    check_written(w, 2);
    check_no_calls();
}

static void test_interp_step(void)
{
    /* Y = 1: DEY; BNE not taken; RTS: A unchanged, Y = 0, nothing written */
    interp_case(0x37, 1, 0x10, 1, 0x37, 0, 1, 0xEE);
    /* Y = 2: CLC; ADC cur; ROR A. $F0 + $90 = $180: the carry comes back as bit 7: $C0. $20 + $11 = $31: $18 */
    interp_case(0xF0, 2, 0x90, 2, 0xC0, 0, 2, 0xEE);
    interp_case(0x20, 2, 0x11, 2, 0x18, 0, 2, 0xEE);
    /* equal values: SEC; SBC gives 0 ($C1B2 BNE not taken): A = util_interp_target, Y = Y - 2 */
    interp_case(0x40, 5, 0x40, 5, 0x77, 3, 5, 0xEE);
    /* equal values with Y = 0 (and util_interp_steps 0): Y = $FE, no fault */
    interp_case(0x40, 0, 0x40, 0, 0x77, 0xFE, 0, 0xEE);
    /* up, d = 16, steps 5: SBC leaves 11, 6, 1, then $FC with the borrow, Y = 4. LSR: steps = 2 in RAM. $FC + 2 =
       $FE has bit 7 ($C1CA BPL not taken): DEY, q = 3 (16 / 5 = 3.2). A = $10 + 3 */
    interp_case(0x20, 5, 0x10, 5, 0x13, 3, 2, 3);
    /* up, d = 18: 13, 8, 3, $FE with the borrow, Y = 4; $FE + 2 = 0, bit 7 clear (BPL taken): q = 4 (3.6) */
    interp_case(0x22, 5, 0x10, 5, 0x14, 4, 2, 4);
    /* up, d = 6, steps 4 (even): 2, then $FE, Y = 2; half 2: $FE + 2 = 0: q = 2 (1.5 rounds up) */
    interp_case(0x16, 4, 0x10, 4, 0x12, 2, 2, 2);
    /* down, d = -16 ($F0), steps 5: ADC leaves $F5, $FA, $FF, then 4 with the carry, Y = $FC. Not 0 ($C1E1 BEQ not
       taken): LSR: steps 2 in RAM; 4 - 2 = 2 is above 0 ($C1EA BMI, $C1EC BEQ not taken): INY ($C1EE), q = $FD (-3,
       -3.2). A = $40 + $FD = $3D */
    interp_case(0x30, 5, 0x40, 5, 0x3D, 0xFD, 2, 0xFD);
    /* down, d = -15 ($F1): $F6, $FB, then 0 with the carry, Y = $FD: $C1E1 BEQ taken: q = $FD (-3 exactly), and
       util_interp_steps is NOT halved (it stays 5) */
    interp_case(0x31, 5, 0x40, 5, 0x3D, 0xFD, 5, 0xFD);
    /* down, d = -18 ($EE): $F3, $F8, $FD, then 2, Y = $FC; 2 - 2 = 0: $C1EC BEQ taken: q = $FC (-3.6 -> -4) */
    interp_case(0x2E, 5, 0x40, 5, 0x3C, 0xFC, 2, 0xFC);
    /* down, d = -19 ($ED): $F2, $F7, $FC, then 1, Y = $FC; 1 - 2 = $FF: $C1EA BMI taken: q = $FC (-3.8 -> -4) */
    interp_case(0x2D, 5, 0x40, 5, 0x3C, 0xFC, 2, 0xFC);
    /* a long way down, and the 8-bit add of the quotient wraps: cur $F0, A = $10: SBC gives $20 with the borrow (the
       down path, d = -224). 75 ADCs of 3 carry ($20 + 225 = $101): A = 1, Y = -75 = $B5. LSR: steps 1; 1 - 1 = 0
       ($C1EC BEQ taken): q = $B5 (-74.67 -> -75). A = $F0 + $B5 = $A5 (8 bits) */
    interp_case(0x10, 3, 0xF0, 3, 0xA5, 0xB5, 1, 0xB5);
}
/* util_interp_step with 0 steps and different values: the ROM loops forever ($C1BC SBC #0 with the carry set, or
   $C1DA ADC #0 with it clear); the C faults at ROM_UTIL_INTERP_STEP and writes nothing. */
static void test_interp_zero_steps(void)
{
    static const uint8_t a[2] = { 0x30, 0x10 };    /* up (no borrow) and down (a borrow) from cur $20 */
    for (unsigned i = 0; i < 2; i++) {
        setup();
        g_ram[UTIL_INTERP_CUR] = 0x20;
        g_ram[UTIL_INTERP_TARGET] = a[i];
        g_ram[UTIL_INTERP_QUOT] = 0xEE;
        snapshot();
        fw_regs r = { a[i], 0x5A, 0, 0 };
        util_interp_step(&g_f, &r);
        CHECK(g_f.fault);
        CHECK_EQ(g_f.fault_addr, ROM_UTIL_INTERP_STEP);
        CHECK_EQ(g_ncalls, 0);
        check_written(NULL, 0);
    }
}

/* The interpolation state a track step leaves: util_interp_steps, util_interp_cur, util_interp_target. */
static void check_interp_state(uint8_t steps, uint8_t cur, uint8_t target)
{
    CHECK_EQ(g_ram[UTIL_INTERP_STEPS], steps);
    CHECK_EQ(g_ram[UTIL_INTERP_CUR], cur);
    CHECK_EQ(g_ram[UTIL_INTERP_TARGET], target);
}

static void test_track_start(void)
{
    const uint8_t x = 4;
    static const uint16_t w[] = {
        UTIL_INTERP_STEPS, UTIL_INTERP_CUR, UTIL_INTERP_TARGET, UTIL_INTERP_QUOT, PAR_START_DUR + 4, PAR_START_VAL + 4,
    };
    /* par_start_dur[X] = 0 ($C24D BNE not taken): A = par_seg_target[X], Y = 0 (the LDY), nothing written */
    setup();
    g_ram[PAR_SEG_TARGET + x] = 0x2B;
    g_ram[PAR_START_VAL + x] = 0x10;
    snapshot();
    fw_regs r = { 0x11, x, 0x33, 0 };
    par_track_start(&g_f, &r);
    CHECK_EQ(r.a, 0x2B);
    CHECK_EQ(r.x, x);
    CHECK_EQ(r.y, 0);
    check_written(NULL, 0);
    check_no_calls();
    /* par_start_dur[X] = 3: util_interp_steps = 3, par_start_dur = 2, cur = par_start_val $10, target =
       par_seg_target $20; d = 16: 13, 10, 7, 4, 1, then $FE with the borrow (Y = 6); LSR: steps 1; $FE + 1 = $FF has
       bit 7: DEY, q = 5 (16 / 3 = 5.33). par_start_val = A = $15; Y = 5 */
    setup();
    g_ram[PAR_SEG_TARGET + x] = 0x20;
    g_ram[PAR_START_VAL + x] = 0x10;
    g_ram[PAR_START_DUR + x] = 3;
    snapshot();
    r = (fw_regs){ 0x11, x, 0x33, 0 };
    par_track_start(&g_f, &r);
    CHECK_EQ(r.a, 0x15);
    CHECK_EQ(r.x, x);
    CHECK_EQ(r.y, 5);
    CHECK_EQ(g_ram[PAR_START_VAL + x], 0x15);
    CHECK_EQ(g_ram[PAR_START_DUR + x], 2);
    CHECK_EQ(g_ram[UTIL_INTERP_QUOT], 5);
    check_interp_state(1, 0x10, 0x20);
    check_written(w, 6);
    check_no_calls();
}

/* One par_track_end run with X = 7: par_end_wait, par_end_dur, par_end_val and par_boundary in, par_seg_target $2B,
   util_interp_quot $EE. Out: A, Y, and the three track bytes; no case reaches the quotient. */
static void track_end_case(uint8_t wait, uint8_t dur, uint8_t val, uint8_t boundary, uint8_t want_a, uint8_t want_y,
                           uint8_t want_wait, uint8_t want_dur, uint8_t want_val)
{
    const uint8_t x = 7;
    static const uint16_t w[] = {
        UTIL_INTERP_STEPS, UTIL_INTERP_CUR, UTIL_INTERP_TARGET, PAR_END_WAIT + 7, PAR_END_DUR + 7, PAR_END_VAL + 7,
    };
    setup();
    g_ram[PAR_END_WAIT + x] = wait;
    g_ram[PAR_END_DUR + x] = dur;
    g_ram[PAR_END_VAL + x] = val;
    g_ram[PAR_BOUNDARY + x] = boundary;
    g_ram[PAR_SEG_TARGET + x] = 0x2B;
    g_ram[UTIL_INTERP_QUOT] = 0xEE;
    snapshot();
    fw_regs r = { 0x11, x, 0x33, 0 };
    par_track_end(&g_f, &r);
    CHECK_EQ(r.a, want_a);
    CHECK_EQ(r.x, x);
    CHECK_EQ(r.y, want_y);
    CHECK_EQ(g_ram[PAR_END_WAIT + x], want_wait);
    CHECK_EQ(g_ram[PAR_END_DUR + x], want_dur);
    CHECK_EQ(g_ram[PAR_END_VAL + x], want_val);
    check_written(w, 6);
    check_no_calls();
}

static void test_track_end(void)
{
    /* waiting: par_end_wait 2 ($C277 BNE taken): DEC to 1; A = par_seg_target, Y = 2 (the LDY); no step */
    track_end_case(2, 5, 0x10, 0x30, 0x2B, 2, 1, 5, 0x10);
    check_interp_state(0, 0, 0);
    /* the wait over (0): the step: steps = par_end_dur 2 (DEC to 1), cur $10, target $31: (CLC; ADC; ROR) $41 / 2 =
       $20 into par_end_val; Y = 0 */
    track_end_case(0, 2, 0x10, 0x31, 0x20, 0, 0, 1, 0x20);
    check_interp_state(2, 0x10, 0x31);
    /* a negative par_end_wait ($80; $C26F BPL not taken, unreachable in the ROM): LDY #0; TYA; STA zeroes it, and the
       Z of the TYA takes the step: par_end_dur 1 (DEC to 0): Y = 1 returns the target $30 */
    track_end_case(0x80, 1, 0x10, 0x30, 0x30, 0, 0, 0, 0x30);
    check_interp_state(1, 0x10, 0x30);
    /* par_end_dur 0 wraps (DEC to $FF); the values equal: Y = 0 - 2 = $FE, A = util_interp_target, no fault */
    track_end_case(0, 0, 0x30, 0x30, 0x30, 0xFE, 0, 0xFF, 0x30);
    check_interp_state(0, 0x30, 0x30);
}

/* The fill frame of the frame-byte tests: isr_fill_ptr = isr_frame_b. */
static void fill_frame_b(void) { fw_wr16(&g_f, ISR_FILL_PTR, ISR_FRAME_B); }

static void test_track_sequential(void)
{
    /* the start phase, X = 2 (F1: par_slot_map[1] = b3): par_start_dur 2 (DEC to 1): par_start_val $10 to
       par_seg_target $31: (CLC; ADC; ROR) $41 / 2 = $20 into par_start_val and b3; Y = 3 (LDY $B796,X) */
    {
        static const uint16_t w[] = {
            UTIL_INTERP_STEPS, UTIL_INTERP_CUR, UTIL_INTERP_TARGET, PAR_START_DUR + 2, PAR_START_VAL + 2,
            ISR_FRAME_B + FRAME_F1,
        };
        setup();
        fill_frame_b();
        g_ram[PAR_START_DUR + 2] = 2;
        g_ram[PAR_START_VAL + 2] = 0x10;
        g_ram[PAR_SEG_TARGET + 2] = 0x31;
        g_ram[PAR_STEADY + 2] = 9;                  /* not reached */
        snapshot();
        fw_regs r = { 0x11, 2, 0x33, 0 };
        par_track_sequential(&g_f, &r);
        CHECK_EQ(r.a, 0x20);
        CHECK_EQ(r.x, 2);
        CHECK_EQ(r.y, FRAME_F1);
        CHECK_EQ(g_ram[PAR_START_DUR + 2], 1);
        CHECK_EQ(g_ram[PAR_START_VAL + 2], 0x20);
        CHECK_EQ(g_ram[ISR_FRAME_B + FRAME_F1], 0x20);
        check_interp_state(2, 0x10, 0x31);
        check_written(w, 6);
        check_no_calls();
    }
    /* the hold, X = 5 (ALF: b0): par_start_dur 0, par_steady 4 ($C172 BEQ not taken): DEC to 3; b0 = par_seg_target;
       A = par_seg_target, Y = 0; util_interp_* untouched */
    {
        static const uint16_t w[] = { PAR_STEADY + 5, ISR_FRAME_B + FRAME_ALF };
        setup();
        fill_frame_b();
        g_ram[PAR_STEADY + 5] = 4;
        g_ram[PAR_SEG_TARGET + 5] = 0x27;
        g_ram[ISR_FRAME_B + FRAME_ALF] = 0xEE;
        snapshot();
        fw_regs r = { 0x11, 5, 0x33, 0 };
        par_track_sequential(&g_f, &r);
        CHECK_EQ(r.a, 0x27);
        CHECK_EQ(r.x, 5);
        CHECK_EQ(r.y, FRAME_ALF);
        CHECK_EQ(g_ram[PAR_STEADY + 5], 3);
        CHECK_EQ(g_ram[ISR_FRAME_B + FRAME_ALF], 0x27);
        check_written(w, 2);
        check_no_calls();
    }
    /* the end phase, X = 10 (voicing: b9): par_start_dur 0, par_steady 0: steps = par_end_dur 3 (DEC to 2), cur =
       par_seg_target $30 itself, target = par_boundary $24: d = -12 ($F4): $F7, $FA, $FD, then 0 with the carry
       (Y = $FC): $C1E1 BEQ: q = -4 exactly, util_interp_steps not halved (3). A = $2C into par_seg_target and b9 */
    {
        static const uint16_t w[] = {
            UTIL_INTERP_STEPS, UTIL_INTERP_CUR, UTIL_INTERP_TARGET, UTIL_INTERP_QUOT, PAR_END_DUR + 10,
            PAR_SEG_TARGET + 10, ISR_FRAME_B + FRAME_V,
        };
        setup();
        fill_frame_b();
        g_ram[PAR_END_DUR + 10] = 3;
        g_ram[PAR_SEG_TARGET + 10] = 0x30;
        g_ram[PAR_BOUNDARY + 10] = 0x24;
        snapshot();
        fw_regs r = { 0x11, 10, 0x33, 0 };
        par_track_sequential(&g_f, &r);
        CHECK_EQ(r.a, 0x2C);
        CHECK_EQ(r.x, 10);
        CHECK_EQ(r.y, FRAME_V);
        CHECK_EQ(g_ram[PAR_END_DUR + 10], 2);
        CHECK_EQ(g_ram[PAR_SEG_TARGET + 10], 0x2C);
        CHECK_EQ(g_ram[UTIL_INTERP_QUOT], 0xFC);
        CHECK_EQ(g_ram[ISR_FRAME_B + FRAME_V], 0x2C);
        check_interp_state(3, 0x30, 0x24);
        check_written(w, 7);
        check_no_calls();
    }
    /* the end phase with par_end_dur 0, X = 1 (FN: b1): the DEC wraps it to $FF; Y = 0 with par_seg_target =
       par_boundary ($22): the equal return (util_interp_target), no fault */
    {
        static const uint16_t w[] = {
            UTIL_INTERP_STEPS, UTIL_INTERP_CUR, UTIL_INTERP_TARGET, PAR_END_DUR + 1, PAR_SEG_TARGET + 1,
            ISR_FRAME_B + FRAME_FN,
        };
        setup();
        fill_frame_b();
        g_ram[PAR_SEG_TARGET + 1] = 0x22;
        g_ram[PAR_BOUNDARY + 1] = 0x22;
        g_ram[UTIL_INTERP_STEPS] = 0xEE;
        snapshot();
        fw_regs r = { 0x11, 1, 0x33, 0 };
        par_track_sequential(&g_f, &r);
        CHECK_EQ(r.a, 0x22);
        CHECK_EQ(r.x, 1);
        CHECK_EQ(r.y, FRAME_FN);
        CHECK_EQ(g_ram[PAR_END_DUR + 1], 0xFF);
        CHECK_EQ(g_ram[PAR_SEG_TARGET + 1], 0x22);
        CHECK_EQ(g_ram[ISR_FRAME_B + FRAME_FN], 0x22);
        check_interp_state(0, 0x22, 0x22);
        check_written(w, 6);
        check_no_calls();
    }
}

/* One par_track_overlap case, X = 3 (F2: b5), the fill frame isr_frame_b, par_end_dur 1 and par_end_val 1. The start
   track: par_seg_target t when sd = 0, else (sd = 2) the average of sv and t. The end track: par_boundary b when
   ew = 0 (par_end_dur 1: Y = 1 returns the target), t when ew = 1 (the wait). The line: lv to b over fl frames (1:
   b; 2: the average). s, e, l: the three values; want: the median the ROM writes. */
typedef struct {
    uint8_t sd, sv, t, ew, b, lv, fl;
    uint8_t s, e, l, want;
} overlap_row;

static void overlap_case(const overlap_row *k)
{
    const uint8_t x = 3;
    static const uint16_t w[] = {
        UTIL_INTERP_STEPS, UTIL_INTERP_CUR, UTIL_INTERP_TARGET, PAR_START_DUR + 3, PAR_START_VAL + 3, PAR_END_WAIT + 3,
        PAR_END_DUR + 3, PAR_END_VAL + 3, PAR_LINE_VAL + 3, PAR_START_TRACK, PAR_END_TRACK, ISR_FRAME_B + FRAME_F2,
    };
    setup();
    fill_frame_b();
    g_ram[PAR_START_DUR + x] = k->sd;
    g_ram[PAR_START_VAL + x] = k->sv;
    g_ram[PAR_SEG_TARGET + x] = k->t;
    g_ram[PAR_END_WAIT + x] = k->ew;
    g_ram[PAR_END_DUR + x] = 1;
    g_ram[PAR_END_VAL + x] = 1;
    g_ram[PAR_BOUNDARY + x] = k->b;
    g_ram[PAR_LINE_VAL + x] = k->lv;
    g_ram[PAR_FRAMES_LEFT] = k->fl;
    g_ram[UTIL_INTERP_QUOT] = 0xEE;
    snapshot();
    fw_regs r = { 0x11, x, 0x33, 0 };
    par_track_overlap(&g_f, &r);
    CHECK_EQ(g_ram[PAR_START_TRACK], k->s);
    CHECK_EQ(g_ram[PAR_END_TRACK], k->e);
    CHECK_EQ(g_ram[PAR_LINE_VAL + x], k->l);
    CHECK_EQ(g_ram[ISR_FRAME_B + FRAME_F2], k->want);
    CHECK_EQ(r.a, k->want);
    CHECK_EQ(r.x, x);
    CHECK_EQ(r.y, FRAME_F2);
    CHECK_EQ(g_ram[UTIL_INTERP_QUOT], 0xEE);
    check_interp_state(k->fl, k->lv, k->b);         /* the line's step comes last */
    check_written(w, 12);
    check_no_calls();
}

static void test_track_overlap(void)
{
    static const overlap_row k[] = {
        /* S = t, E = b, L = (lv + b) / 2 (fl = 2). The six outcomes, by CMP and BPL / BMI:
           L >= S, L < E ($C21C BPL taken, $C229 BPL not taken): the line */
        { 0, 0, 0x10, 0, 0x30, 0x10, 2, 0x10, 0x30, 0x20, 0x20 },
        /* L >= S, L >= E, S < E ($C229 BPL taken, $C245 BMI taken): the end */
        { 0, 0, 0x10, 0, 0x18, 0x28, 2, 0x10, 0x18, 0x20, 0x18 },
        /* L >= S, L >= E, S >= E ($C245 BMI not taken): the start */
        { 0, 0, 0x18, 0, 0x10, 0x30, 2, 0x18, 0x10, 0x20, 0x18 },
        /* L < S, L >= E ($C21C BPL not taken, $C221 BMI not taken): the line */
        { 0, 0, 0x20, 0, 0x10, 0x20, 2, 0x20, 0x10, 0x18, 0x18 },
        /* L < S, L < E, S >= E ($C221 BMI taken, $C234 BPL taken): the end */
        { 0, 0, 0x20, 0, 0x18, 0x08, 2, 0x20, 0x18, 0x10, 0x18 },
        /* L < S, L < E, S < E ($C234 BPL not taken): the start */
        { 0, 0, 0x18, 0, 0x20, 0x00, 2, 0x18, 0x20, 0x10, 0x18 },
        /* the sign of the 8-bit difference: L = $10, S = $90, E = $20. $10 - $90 = $80 is "minus" (L < S), $10 - $20 is
           minus, $90 - $20 = $70 is not: the end, $20. A true signed median is the line $10 (S = -112) */
        { 0, 0, 0x90, 0, 0x20, 0x00, 2, 0x90, 0x20, 0x10, 0x20 },
        /* L = $90 (fl = 1: b), S = $10 (sd = 2: (0 + $20) / 2), E = $20 (ew = 1: t): $90 - $10 = $80 is minus (L < S),
           $90 - $20 = $70 is not: the line, $90. A true signed median is S ($10; L = -112), an unsigned one E ($20) */
        { 2, 0, 0x20, 1, 0x90, 0x00, 1, 0x10, 0x20, 0x90, 0x90 },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        overlap_case(&k[i]);
}

/* par_step_tracks: P10..P1, odd X sequential (a hold of par_seg_target), even X overlapping (start = end =
   par_seg_target, the line par_boundary $3F over 1 frame, so the median is par_seg_target): each writes
   par_seg_target[X] = $10 + X into frame byte par_slot_map[X - 1]. The pitch: par_frames_left 1: pros_play_pitch =
   pros_play_target $A2, b10 = $A2 >> 2 = $28. The fill frame is isr_frame_a; its bytes b11-b19 stay $EE. */
static void test_step_tracks(void)
{
    uint16_t w[64];
    size_t n = 0;
    setup();
    fw_wr16(&g_f, ISR_FILL_PTR, ISR_FRAME_A);
    memset(g_ram + ISR_FRAME_A, 0xEE, 20);
    for (uint8_t x = 1; x <= 10; x++) {
        g_ram[PAR_SEG_TARGET + x] = (uint8_t)(0x10 + x);
        g_ram[PAR_SEQUENTIAL + x] = x & 1;
        if (x & 1) {
            g_ram[PAR_STEADY + x] = 1;
            w[n++] = (uint16_t)(PAR_STEADY + x);
        } else {
            g_ram[PAR_END_WAIT + x] = 1;
            g_ram[PAR_BOUNDARY + x] = 0x3F;
            w[n++] = (uint16_t)(PAR_END_WAIT + x);
            w[n++] = (uint16_t)(PAR_LINE_VAL + x);
        }
    }
    g_ram[PAR_FRAMES_LEFT] = 1;
    g_ram[PROS_PLAY_PITCH] = 0x80;
    g_ram[PROS_PLAY_TARGET] = 0xA2;
    for (uint16_t b = 0; b <= FRAME_PITCH; b++)
        w[n++] = (uint16_t)(ISR_FRAME_A + b);
    w[n++] = UTIL_INTERP_STEPS;
    w[n++] = UTIL_INTERP_CUR;
    w[n++] = UTIL_INTERP_TARGET;
    w[n++] = PAR_START_TRACK;
    w[n++] = PAR_END_TRACK;
    w[n++] = PROS_PLAY_PITCH;
    snapshot();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_step_tracks(&g_f, &r);
    for (uint8_t x = 1; x <= 10; x++) {
        CHECK_EQ(g_ram[ISR_FRAME_A + fw_par_slot_map[x - 1]], 0x10 + x);
        if (x & 1) {
            CHECK_EQ(g_ram[PAR_STEADY + x], 0);
        } else {
            CHECK_EQ(g_ram[PAR_END_WAIT + x], 0);
            CHECK_EQ(g_ram[PAR_LINE_VAL + x], 0x3F);
        }
    }
    /* the routing in full: b0 = P5 (ALF), b1 = P1 (FN), b2 = P6 (A1), b3 = P2 (F1), ..., b8 = P9 (AHF), b9 = P10 (V) */
    static const uint8_t frame[11] = { 0x15, 0x11, 0x16, 0x12, 0x17, 0x13, 0x18, 0x14, 0x19, 0x1A, 0x28 };
    for (unsigned b = 0; b < 11; b++)
        CHECK_EQ(g_ram[ISR_FRAME_A + b], frame[b]);
    for (unsigned b = 11; b < 20; b++)
        CHECK_EQ(g_ram[ISR_FRAME_A + b], 0xEE);
    CHECK_EQ(g_ram[PROS_PLAY_PITCH], 0xA2);
    CHECK_EQ(g_ram[PAR_START_TRACK], 0x12);         /* the last overlapping track is P2's */
    CHECK_EQ(g_ram[PAR_END_TRACK], 0x12);
    check_interp_state(1, 0x80, 0xA2);              /* the pitch's step comes last */
    CHECK_EQ(r.a, 0x28);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, FRAME_PITCH);
    check_written(w, n);
    check_no_calls();
}

void suite_fw_par(void)
{
```

and replace the end of `suite_fw_par`

```c
    test_setup_voice();
}
```

with

```c
    test_setup_voice();
    test_track_rows();
    test_interp_step();
    test_interp_zero_steps();
    test_track_start();
    test_track_end();
    test_track_sequential();
    test_track_overlap();
    test_step_tracks();
}
```

What the tests pin (spec §4.5, the part for this task):
- `test_track_rows`: the six rows (address, A/X/Y).
- `test_interp_step`: Y = 1; Y = 2 with and without a carry out of the add (the `ROR` brings it into bit 7); equal values with Y = 5 and with Y = 0 (Y out = Y − 2, A = `util_interp_target`, no fault); up quotients with the `BPL` at $C1CA taken and not (the `DEY`), an even count rounding half up; down quotients through $C1E1 `BEQ` (the count NOT halved), $C1EA `BMI`, $C1EC `BEQ` and $C1EE `INY`; the halved count in RAM; a long down quotient whose final add wraps in 8 bits.
- `test_interp_zero_steps`: 0 steps with different values, up and down: the fault at `ROM_UTIL_INTERP_STEP`, nothing written.
- `test_track_start`: no transition left (Y = 0, nothing written); one step (the count, the value, the interpolation state).
- `test_track_end`: waiting (Y = the count); the wait over; **a negative `par_end_wait` zeroed** ($C271) and stepped; `par_end_dur` wrapping past 0 with equal values.
- `test_track_sequential`: each phase (start, hold, end from `par_seg_target` itself) into its frame byte through `par_slot_map`, and `par_end_dur` wrapping past 0.
- `test_track_overlap`: each of the six median outcomes; **two where the 8-bit difference overflows** ($10 against $90, and $90 against $10), which a true signed comparison would order the other way.
- `test_step_tracks`: the slot routing of P1–P10 through `par_slot_map` (sequential and overlapping tracks mixed), the order (the last interpolation state is the pitch's, the last start/end track P2's), b10 = `pros_play_pitch` >> 2, b11–b19 untouched, the exit registers.

- [ ] **Step 2: Run it to see it fail**

Run: `cmd //c ".\build.bat test fw_par"`
Expected: the build stops with `tests\test_fw_par.c(14): fatal error C1083: Cannot open include file: 'firmware/par_track.h': No such file or directory`

- [ ] **Step 3: Implement**

Create `src/firmware/par_track.h`:

```c
#ifndef FW_PAR_TRACK_H
#define FW_PAR_TRACK_H
/* The tracks of the frame loop (ROM $C0FF-$C130 and $C14C-$C29B), decompiled (M4.4b spec 2.2, 2.6): once per frame
   par_play calls par_step_tracks, which steps the ten parameter tracks and the pitch track by util_interp_step and
   writes the frame's parameter bytes b0-b10 into the fill buffer (isr_fill_ptr) through par_slot_map. Each routine
   takes the registers at entry in r and leaves in r the A, X and Y that the ROM leaves at its RTS. r->p is not used:
   none of the six reads an entry flag, and no caller reads a flag after any of them. */
#include "fw.h"

/* $C1A1: one interpolation step. A = the target, Y = the steps left (the callers also store them in
   util_interp_steps, and the target in util_interp_target), util_interp_cur = the current value.
   - Y = 1: A unchanged (the target). Y = 2: (A + util_interp_cur) >> 1 with the carry as bit 7 (CLC; ADC; ROR).
   - Otherwise d = A - util_interp_cur (SEC; SBC). d = 0: A = util_interp_target.
   - d without a borrow: the quotient q by SBC util_interp_steps until one borrows; util_interp_steps is halved in
     RAM (LSR) and the remainder plus that half with bit 7 set takes one off q (BPL).
   - d with a borrow: q (negative) by ADC util_interp_steps until one carries; unless that sum is 0, util_interp_steps
     is halved in RAM and q gains one when the sum minus that half is above 0 (BMI, BEQ).
   - util_interp_quot = q, A = util_interp_cur + q (8 bits).
   util_interp_steps = 0 with d not 0 makes the ROM loop forever (SBC #0 with the carry set, ADC #0 with it clear):
   the C reports a fault at ROM_UTIL_INTERP_STEP instead (fw_fault) and returns with nothing written.
   r in: A, Y. r out: A = the value; X unchanged; Y = 0 (Y = 1 or 2), Y - 2 (d = 0), q (otherwise). */
void util_interp_step(fw *f, fw_regs *r);

/* $C24A: the start track of par_track_overlap, X = the parameter: par_seg_target[X] once par_start_dur[X] is 0;
   otherwise util_interp_steps = par_start_dur[X], which is decremented, and par_start_val[X] takes one step towards
   par_seg_target[X]. r in: X. r out: A = the track's value, X unchanged, Y = 0 (no step) or util_interp_step's Y. */
void par_track_start(fw *f, fw_regs *r);

/* $C26C: the end track of par_track_overlap, X = the parameter. A negative par_end_wait[X] is zeroed first ($C271,
   unreachable: par_plan_tracks stores 0-127). While par_end_wait[X] is not 0 it is decremented and A =
   par_seg_target[X] (Y = the count before the DEC); at 0, util_interp_steps = par_end_dur[X], which is decremented
   (wrapping past 0), and par_end_val[X] takes one step towards par_boundary[X] (Y = util_interp_step's).
   r in: X. r out: A = the track's value, X unchanged, Y as above. */
void par_track_end(fw *f, fw_regs *r);

/* $C14C: X = the parameter, its transitions apart (par_sequential[X] set): while par_start_dur[X] is not 0
   (decremented), par_start_val[X] steps towards par_seg_target[X]; then while par_steady[X] is not 0 (decremented),
   par_seg_target[X] holds; then par_seg_target[X] itself steps towards par_boundary[X] over par_end_dur[X]
   (decremented, wrapping past 0). The value goes to frame byte par_slot_map[X - 1] through (isr_fill_ptr),Y.
   r in: X. r out: A = the value written, X unchanged, Y = the frame byte's index. */
void par_track_sequential(fw *f, fw_regs *r);

/* $C1F2: X = the parameter, its transitions overlapping: par_start_track = par_track_start, par_end_track =
   par_track_end; par_line_val[X] steps towards par_boundary[X] over par_frames_left; frame byte par_slot_map[X - 1]
   = the median of the three, ordered by CMP and BPL / BMI: the sign of the 8-bit difference, not a true signed
   comparison. r in: X. r out: A = the value written, X unchanged, Y = the frame byte's index. */
void par_track_overlap(fw *f, fw_regs *r);

/* $C0FF: for X = 10 down to 1, par_track_sequential when par_sequential[X] is not 0, else par_track_overlap; then the
   pitch track: pros_play_pitch steps towards pros_play_target over par_frames_left, and frame byte b10 =
   pros_play_pitch >> 2. r in: not read (LDX #$0A first; A and Y are loaded before they are read). r out: A =
   pros_play_pitch >> 2, X = 0, Y = 10. */
void par_step_tracks(fw *f, fw_regs *r);

#endif
```

Create `src/firmware/par_track.c`:

```c
#include "par_track.h"
#include "par_addr.h"
#include "data/fw_data.h"

/* ---- util_interp_step $C1A1 ---- */

/* $C1A8-$C1AD, Y = 2: CLC; ADC util_interp_cur; ROR A: the 9-bit sum halved, its carry into bit 7. */
static void average(fw *f, fw_regs *r)
{
    r->a = (uint8_t)(((unsigned)r->a + fw_rd8(f, UTIL_INTERP_CUR)) >> 1);
}

/* $C1BA-$C1C1, d without a borrow (A = d): LDY #0, then INY; SEC; SBC util_interp_steps; BCS: Y = the number of
   subtractions up to and including the one that borrows (8 bits), A = what that one leaves. With util_interp_steps =
   0 nothing ever borrows and the ROM loops forever: the C faults and returns false. */
static bool quotient_up(fw *f, fw_regs *r)
{
    uint8_t steps = fw_rd8(f, UTIL_INTERP_STEPS);
    if (steps == 0) {
        fw_fault(f, ROM_UTIL_INTERP_STEP);
        return false;
    }
    bool no_borrow;
    r->y = 0;
    do {
        r->y++;
        no_borrow = r->a >= steps;
        r->a = (uint8_t)(r->a - steps);
    } while (no_borrow);
    return true;
}

/* $C1C3-$C1CC: LSR util_interp_steps (the halved count stays in RAM); CLC; ADC util_interp_steps; BPL: bit 7 of the
   8-bit sum set takes one off Y. */
static void round_up(fw *f, fw_regs *r)
{
    uint8_t half = fw_rd8(f, UTIL_INTERP_STEPS) >> 1;
    fw_wr8(f, UTIL_INTERP_STEPS, half);
    r->a = (uint8_t)(r->a + half);
    if (r->a & 0x80)
        r->y--;
}

/* $C1D8-$C1DF, d with a borrow (A = d): LDY #0, then DEY; CLC; ADC util_interp_steps; BCC: Y = minus the number of
   additions up to and including the one that carries (8 bits), A = the 8-bit sum it leaves. With
   util_interp_steps = 0 nothing ever carries and the ROM loops forever: the C faults and returns false. */
static bool quotient_down(fw *f, fw_regs *r)
{
    uint8_t steps = fw_rd8(f, UTIL_INTERP_STEPS);
    if (steps == 0) {
        fw_fault(f, ROM_UTIL_INTERP_STEP);
        return false;
    }
    unsigned sum;
    r->y = 0;
    do {
        r->y--;
        sum = (unsigned)r->a + steps;
        r->a = (uint8_t)sum;
    } while (sum < 0x100);
    return true;
}

/* $C1E3-$C1EF, after the BEQ at $C1E1 (a sum of 0 skips this): LSR util_interp_steps (kept in RAM); SEC; SBC
   util_interp_steps; BMI, BEQ: Y + 1 (INY) when the 8-bit difference is above 0 with bit 7 clear. */
static void round_down(fw *f, fw_regs *r)
{
    uint8_t half = fw_rd8(f, UTIL_INTERP_STEPS) >> 1;
    fw_wr8(f, UTIL_INTERP_STEPS, half);
    r->a = (uint8_t)(r->a - half);
    if (!(r->a & 0x80) && r->a != 0)
        r->y++;
}

/* $C1CD-$C1D7: util_interp_quot = Y; LDA util_interp_cur; CLC; ADC util_interp_quot. */
static void add_quotient(fw *f, fw_regs *r)
{
    fw_wr8(f, UTIL_INTERP_QUOT, r->y);
    r->a = (uint8_t)(fw_rd8(f, UTIL_INTERP_CUR) + fw_rd8(f, UTIL_INTERP_QUOT));
}

void util_interp_step(fw *f, fw_regs *r)
{
    r->y--;                                        /* $C1A1 DEY; BNE: Y = 1 returns the target */
    if (r->y == 0)
        return;
    r->y--;                                        /* $C1A5 DEY; BNE */
    if (r->y == 0) {
        average(f, r);
        return;
    }
    uint8_t cur = fw_rd8(f, UTIL_INTERP_CUR);      /* $C1AE SEC; SBC util_interp_cur */
    bool up = r->a >= cur;
    r->a = (uint8_t)(r->a - cur);
    if (r->a == 0) {                               /* $C1B2 BNE; LDA util_interp_target */
        r->a = fw_rd8(f, UTIL_INTERP_TARGET);
        return;
    }
    if (up) {                                      /* $C1B8 BCC $C1D8 */
        if (!quotient_up(f, r))
            return;
        round_up(f, r);
    } else {
        if (!quotient_down(f, r))
            return;
        if (r->a != 0)                             /* $C1E1 BEQ $C1CD */
            round_down(f, r);
    }
    add_quotient(f, r);
}

/* ---- the steps of every track ---- */

/* One step of a track value at val towards the value at target, with Y = the steps (loaded by the caller):
   STY util_interp_steps; DEC dur (when dur is not 0: the overlap line and the pitch have no count); LDA val; STA
   util_interp_cur; LDA target; STA util_interp_target; JSR util_interp_step; STA val. The six copies are $C151-$C166,
   $C183-$C198, $C201-$C213, $C253-$C268, $C27C-$C291 and $C115-$C127. */
static void track_step(fw *f, fw_regs *r, uint16_t dur, uint16_t val, uint16_t target)
{
    fw_wr8(f, UTIL_INTERP_STEPS, r->y);
    if (dur != 0)
        fw_wr8(f, dur, (uint8_t)(fw_rd8(f, dur) - 1));
    r->a = fw_rd8(f, val);
    fw_wr8(f, UTIL_INTERP_CUR, r->a);
    r->a = fw_rd8(f, target);
    fw_wr8(f, UTIL_INTERP_TARGET, r->a);
    util_interp_step(f, r);
    fw_wr8(f, val, r->a);
}

/* LDY $B796,X; STA ($2C),Y: A into the fill frame's byte par_slot_map[X - 1] ($C169-$C16C, $C177 with $C17D,
   $C19B-$C19E; par_track_overlap loads Y at $C216 and stores on each of its paths). */
static void slot_store(fw *f, fw_regs *r)
{
    r->y = fw_rom8(f, (uint16_t)(PAR_SLOT_MAP - 1 + r->x));
    fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);
}

/* ---- par_track_start $C24A, par_track_end $C26C ---- */

void par_track_start(fw *f, fw_regs *r)
{
    uint8_t x = r->x;
    r->y = fw_rd8(f, (uint16_t)(PAR_START_DUR + x));   /* $C24A LDY; BNE */
    if (r->y == 0) {
        r->a = fw_rd8(f, (uint16_t)(PAR_SEG_TARGET + x));   /* $C24F-$C252 */
        return;
    }
    track_step(f, r, (uint16_t)(PAR_START_DUR + x), (uint16_t)(PAR_START_VAL + x), (uint16_t)(PAR_SEG_TARGET + x));
}

/* $C271-$C276: LDY #0; TYA; STA par_end_wait,X: a negative count is zeroed, and the Z of the TYA takes the step
   path at $C277. */
static void zero_wait(fw *f, fw_regs *r)
{
    r->y = 0;
    r->a = r->y;
    fw_wr8(f, (uint16_t)(PAR_END_WAIT + r->x), r->a);
}

/* $C295-$C29B: DEC par_end_wait,X; LDA par_seg_target,X. */
static void end_wait(fw *f, fw_regs *r)
{
    uint16_t wait = (uint16_t)(PAR_END_WAIT + r->x);
    fw_wr8(f, wait, (uint8_t)(fw_rd8(f, wait) - 1));
    r->a = fw_rd8(f, (uint16_t)(PAR_SEG_TARGET + r->x));
}

void par_track_end(fw *f, fw_regs *r)
{
    uint8_t x = r->x;
    r->y = fw_rd8(f, (uint16_t)(PAR_END_WAIT + x));    /* $C26C LDY; BPL */
    if (r->y & 0x80)
        zero_wait(f, r);
    if (r->y != 0) {                                    /* $C277 BNE: the Z of the LDY, or of the TYA */
        end_wait(f, r);
        return;
    }
    r->y = fw_rd8(f, (uint16_t)(PAR_END_DUR + x));     /* $C279 */
    track_step(f, r, (uint16_t)(PAR_END_DUR + x), (uint16_t)(PAR_END_VAL + x), (uint16_t)(PAR_BOUNDARY + x));
}

/* ---- par_track_sequential $C14C ---- */

/* $C174-$C17F: DEC par_steady,X; then LDY $B796,X; LDA par_seg_target,X; STA ($2C),Y (slot_store; the LDY comes
   before the LDA in the ROM, and neither has a side effect). */
static void hold(fw *f, fw_regs *r)
{
    uint16_t steady = (uint16_t)(PAR_STEADY + r->x);
    fw_wr8(f, steady, (uint8_t)(fw_rd8(f, steady) - 1));
    r->a = fw_rd8(f, (uint16_t)(PAR_SEG_TARGET + r->x));
}

void par_track_sequential(fw *f, fw_regs *r)
{
    uint8_t x = r->x;
    r->y = fw_rd8(f, (uint16_t)(PAR_START_DUR + x));   /* $C14C LDY; BEQ $C16F */
    if (r->y != 0) {
        track_step(f, r, (uint16_t)(PAR_START_DUR + x), (uint16_t)(PAR_START_VAL + x),
                   (uint16_t)(PAR_SEG_TARGET + x));     /* $C151-$C166: the start transition */
    } else {
        r->a = fw_rd8(f, (uint16_t)(PAR_STEADY + x));   /* $C16F LDA; BEQ $C180 */
        if (r->a != 0) {
            hold(f, r);
        } else {
            r->y = fw_rd8(f, (uint16_t)(PAR_END_DUR + x));   /* $C180-$C198: the end transition, from the target */
            track_step(f, r, (uint16_t)(PAR_END_DUR + x), (uint16_t)(PAR_SEG_TARGET + x),
                       (uint16_t)(PAR_BOUNDARY + x));
        }
    }
    slot_store(f, r);
}

/* ---- par_track_overlap $C1F2 ---- */

/* CMP m, then BMI / BPL: bit 7 of the 8-bit difference a - m (the sign, overflow ignored). */
static bool minus(uint8_t a, uint8_t m) { return ((uint8_t)(a - m) & 0x80) != 0; }

/* $C219-$C249: A = the line value; the median of it, par_start_track and par_end_track by minus(). */
static void median(fw *f, fw_regs *r)
{
    uint8_t start = fw_rd8(f, PAR_START_TRACK), end = fw_rd8(f, PAR_END_TRACK);
    if (!minus(r->a, start)) {                     /* $C21C BPL $C226 */
        if (minus(r->a, end))                      /* $C226 CMP; BPL $C23F: else the line ($C22B) */
            return;
        r->a = start;                              /* $C23F LDA; CMP; BMI $C239 */
        if (minus(r->a, end))
            r->a = end;
    } else {
        if (!minus(r->a, end))                     /* $C21E CMP; BMI $C22E: else the line ($C223) */
            return;
        r->a = start;                              /* $C22E LDA; CMP; BPL $C239 */
        if (!minus(r->a, end))
            r->a = end;
    }
}

void par_track_overlap(fw *f, fw_regs *r)
{
    par_track_start(f, r);                          /* $C1F2 */
    fw_wr8(f, PAR_START_TRACK, r->a);
    par_track_end(f, r);                            /* $C1F8 */
    fw_wr8(f, PAR_END_TRACK, r->a);
    r->y = fw_rd8(f, PAR_FRAMES_LEFT);              /* $C1FE-$C213: the line, over the frames left */
    track_step(f, r, 0, (uint16_t)(PAR_LINE_VAL + r->x), (uint16_t)(PAR_BOUNDARY + r->x));
    r->y = fw_rom8(f, (uint16_t)(PAR_SLOT_MAP - 1 + r->x));   /* $C216 LDY $B796,X */
    median(f, r);
    fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);       /* STA ($2C),Y on each path */
}

/* ---- par_step_tracks $C0FF ---- */

/* $C112-$C130: the pitch track (no count): pros_play_pitch steps towards pros_play_target over par_frames_left; LSR
   A twice; LDY #$0A; STA ($2C),Y. */
static void pitch_track(fw *f, fw_regs *r)
{
    r->y = fw_rd8(f, PAR_FRAMES_LEFT);
    track_step(f, r, 0, PROS_PLAY_PITCH, PROS_PLAY_TARGET);
    r->a >>= 2;
    r->y = FRAME_PITCH;
    fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);
}

void par_step_tracks(fw *f, fw_regs *r)
{
    r->x = 10;                                      /* $C0FF LDX #$0A ... DEX; BNE $C101 */
    do {
        r->a = fw_rd8(f, (uint16_t)(PAR_SEQUENTIAL + r->x));
        if (r->a != 0)
            par_track_sequential(f, r);             /* $C10C */
        else
            par_track_overlap(f, r);                /* $C106 */
        r->x--;
    } while (r->x != 0);
    pitch_track(f, r);
}
```

In `src/engine/thunks.c`:
- Replace

```c
#include "firmware/par_start.h"
```

  with

```c
#include "firmware/par_start.h"
#include "firmware/par_track.h"
```

- Replace the end of `par_plan_tracks_regs`

```c
    par_plan_tracks(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

  with

```c
    par_plan_tracks(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_step_tracks $C0FF: nothing in (LDX #$0A first; A and Y are loaded before they are read). A = pros_play_pitch
   >> 2, X = 0, Y = 10 out. Its ROM caller par_play ($B8C0, $B90B) calls frm_finish_frame next, which pushes A, X and
   Y and restores them ($B9B5-$B9B9, $BA39-$BA3D; its frm_snapshot loads Y and X first, $B917, $B922); then par_play
   writes X ($B877 LDX cmd_ev_next) or calls isr_par_ready (PHA / PLA), isr_wait_frame or par_step_tracks, none of
   which reads them, before it writes A, X and Y. No caller reads a flag. Declared: A, X, Y. */
static void par_step_tracks_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_step_tracks(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_track_sequential $C14C: X (the parameter) in. A = the frame byte's value, X untouched, Y = its index
   (par_slot_map) out. par_step_tracks reads X ($C10F DEX; BNE) and loads A ($C101, $C118) and Y ($C112) before
   reading them. No caller reads a flag. Declared: A, X, Y. */
static void par_track_sequential_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_track_sequential(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* util_interp_step $C1A1: A (the target) and Y (the steps) in, util_interp_cur / _steps / _target in RAM. A = the
   value, X untouched, Y = 0, Y - 2 or the quotient out. The callers store A at once: par_track_sequential ($C166
   STA $16FC,X; $C198 STA $17BF,X), par_track_overlap ($C213 STA $170C,X), par_track_start ($C268 STA $16FC,X) and
   par_track_end ($C291 STA $175C,X) with X; par_step_tracks ($C127 STA pros_play_pitch, LSR, LSR) reads A only. Y is
   loaded before it is read on every path ($C169/$C19B/$C216 LDY $B796,X, $C12C LDY #$0A; after par_track_start and
   par_track_end's RTS, par_track_overlap's $C1F5 STA and par_track_end's $C26C LDY, or $C1FB STA and $C1FE LDY).
   No caller reads a flag. Declared: A, X, Y. */
static void util_interp_step_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    util_interp_step(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_track_overlap $C1F2: X (the parameter) in. A = the median written, X untouched, Y = the frame byte's index out.
   par_step_tracks reads X ($C109 JMP $C10F: DEX; BNE) and loads A and Y before reading them. No caller reads a flag.
   Declared: A, X, Y. */
static void par_track_overlap_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_track_overlap(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_track_start $C24A: X in. A = the start track's value, X untouched, Y = 0 or util_interp_step's out.
   par_track_overlap reads A ($C1F5 STA par_start_track) and X (par_track_end's $C26C LDY $176C,X), which loads Y
   first. No caller reads a flag. Declared: A, X, Y. */
static void par_track_start_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_track_start(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_track_end $C26C: X in. A = the end track's value, X untouched, Y = the wait count, or util_interp_step's out.
   par_track_overlap reads A ($C1FB STA par_end_track) and X ($C204 LDA $170C,X), and loads Y first ($C1FE LDY
   par_frames_left). No caller reads a flag. Declared: A, X, Y. */
static void par_track_end_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_track_end(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

- Replace

```c
THUNK(par_plan_tracks)
```

  with

```c
THUNK(par_plan_tracks)
THUNK(par_step_tracks)
THUNK(par_track_sequential)
THUNK(util_interp_step)
THUNK(par_track_overlap)
THUNK(par_track_start)
THUNK(par_track_end)
```

- Replace

```c
    { "par_plan_tracks",    0xC0D2, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_plan_tracks },
```

  with

```c
    { "par_plan_tracks",    0xC0D2, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_plan_tracks },
    { "par_step_tracks",    0xC0FF, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_step_tracks },
    { "par_track_sequential", 0xC14C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_track_sequential },
    { "util_interp_step",   0xC1A1, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_util_interp_step },
    { "par_track_overlap",  0xC1F2, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_track_overlap },
    { "par_track_start",    0xC24A, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_track_start },
    { "par_track_end",      0xC26C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_track_end },
```

The table stays in address order (54 rows: Task 1 adds none, these six follow `par_plan_tracks`); the `THUNK` lines follow it.

- [ ] **Step 4: Faithfulness check against the dumps**

Read each dump (`build/ghidra740/dump/`, read-only) side by side with its C, instruction by instruction:
- `rom_C1A1_util_interp_step.txt`: $C1A1/$C1A5 the two `DEY; BNE` → `r->y--` twice with the `== 0` tests; $C1A8–$C1AD → `average` (9-bit sum >> 1); $C1AE–$C1B7 (`SEC; SBC` → `up = a >= cur`, the 8-bit difference, `BNE`, `LDA util_interp_target`); $C1B8 `BCC` → `up`; $C1BA–$C1C1 → `quotient_up` (`LDY #0`, `INY`, carry = no borrow, `BCS`); $C1C3–$C1CC → `round_up` (`LSR` written, `CLC; ADC`, `BPL` = bit 7 clear, `DEY`); $C1CD–$C1D7 → `add_quotient` (`STY`, `LDA cur; CLC; ADC quot`); $C1D8–$C1DF → `quotient_down` (`DEY`, `CLC; ADC`, `BCC` = sum < $100); $C1E1 `BEQ` → `r->a != 0`; $C1E3–$C1EF → `round_down` (`LSR` written, `SEC; SBC`, `BMI`, `BEQ`, `INY`, `JMP $C1CD`).
- `rom_C24A_par_track_start.txt`: `LDY $172C,X; BNE` → `r->y`; $C24F `LDA $17BF,X; RTS`; $C253–$C268 → `track_step(PAR_START_DUR, PAR_START_VAL, PAR_SEG_TARGET)`.
- `rom_C26C_par_track_end.txt`: `LDY $176C,X; BPL` → `r->y & 0x80`; $C271–$C276 → `zero_wait` (`LDY #0; TYA; STA`); $C277 `BNE` → `r->y != 0`; $C295–$C29B → `end_wait`; $C279–$C291 → `LDY $173C,X` and `track_step(PAR_END_DUR, PAR_END_VAL, PAR_BOUNDARY)`.
- `rom_C14C_par_track_sequential.txt`: $C14C `LDY $172C,X; BEQ`; $C151–$C166 → `track_step(PAR_START_DUR, PAR_START_VAL, PAR_SEG_TARGET)`; $C16F `LDA $177C,X; BEQ`; $C174–$C17A → `hold`; $C180–$C198 → `LDY $173C,X` and `track_step(PAR_END_DUR, PAR_SEG_TARGET, PAR_BOUNDARY)`; each path's `LDY $B796,X; STA ($2C),Y` → `slot_store` (`fw_rom8(PAR_SLOT_MAP - 1 + X)`, `fw_wr_ind_y(ISR_FILL_PTR, ...)`).
- `rom_C1F2_par_track_overlap.txt`: the two `JSR`s and `STA par_start_track` / `STA par_end_track`; $C1FE–$C213 → `LDY par_frames_left` and `track_step(0, PAR_LINE_VAL, PAR_BOUNDARY)`; $C216 `LDY $B796,X`; $C219–$C249 → `median` (each `CMP` then `BPL`/`BMI` → `minus()`, bit 7 of the 8-bit difference), every `STA ($2C),Y` → the one `fw_wr_ind_y` after it.
- `rom_C0FF_par_step_tracks.txt`: `LDX #$0A`, `LDA $174C,X; BNE` → sequential, else overlap; `DEX; BNE`; $C112–$C130 → `pitch_track` (`LDY par_frames_left`, `track_step(0, PROS_PLAY_PITCH, PROS_PLAY_TARGET)`, `LSR A` twice, `LDY #$0A`, `STA ($2C),Y`).
- No RAM write in the C that the ROM does not make, none missing: the ROM's stores are exactly `util_interp_steps` (the callers' `STY`, the `LSR`s), `util_interp_cur`, `util_interp_target`, `util_interp_quot`, the `DEC`s, the value stores, `par_end_wait` (zeroing), `par_start_track`, `par_end_track`, `pros_play_pitch` and the frame bytes.

For the thunk outputs, re-read `rom_B877_par_play.txt` ($B8C0–$B8D2, $B8FB–$B914, $B877–$B8B0), `rom_B9B5_frm_finish_frame.txt` (the pushes and pops), `rom_B917_frm_snapshot.txt` (its first instructions), and the call sites listed in "ROM facts checked". Confirm the callers with:

```bash
python -c "import re,os;d=open(os.environ['APOLLO2_ROM'],'rb').read();[print(hex(a),hex(op),[hex(m.start()) for m in re.finditer(re.escape(bytes([op,a&255,a>>8])),d)]) for a in (0xC0FF,0xC14C,0xC1A1,0xC1F2,0xC24A,0xC26C) for op in (0x20,0x4C)]"
```

Expected:

```
0xc0ff 0x20 ['0xb8c0', '0xb90b']
0xc0ff 0x4c []
0xc14c 0x20 ['0xc10c']
0xc14c 0x4c []
0xc1a1 0x20 ['0xc124', '0xc163', '0xc195', '0xc210', '0xc265', '0xc28e']
0xc1a1 0x4c []
0xc1f2 0x20 ['0xc106']
0xc1f2 0x4c []
0xc24a 0x20 ['0xc1f2']
0xc24a 0x4c []
0xc26c 0x20 ['0xc1f8']
0xc26c 0x4c []
```

and for relative branches into the six entries (every byte position, 2-byte branches, `BBS`/`BBC` on A, and 3-byte `BBS`/`BBC` on zero page):

```bash
python -c "
import os
d=open(os.environ['APOLLO2_ROM'],'rb').read()
br={0x10,0x30,0x50,0x70,0x80,0x90,0xB0,0xD0,0xF0}
s=lambda b:b-256 if b>127 else b
for t in (0xC0FF,0xC14C,0xC1A1,0xC1F2,0xC24A,0xC26C):
  h=[hex(p) for p in range(0x8000,0xFFFE) if (d[p] in br or d[p]&0x1F in (3,0x13)) and p+2+s(d[p+1])==t]
  h+=[hex(p) for p in range(0x8000,0xFFFD) if d[p]&0x1F in (7,0x17) and p+3+s(d[p+2])==t]
  print(hex(t),h)
"
```

Expected:

```
0xc0ff ['0xc08f']
0xc14c ['0xc133']
0xc1a1 []
0xc1f2 []
0xc24a []
0xc26c []
```

The two hits are operand bytes, not branches: $C08F is the high byte $17 of `LDA $17DE` at $C08D (`rom_C039_par_boundary_value.txt`), $C133 the low byte $F0 of `SBC $17F0` at $C132 (`rom_C131_pros_pitch_code_dither.txt`); check both in the dumps.

- [ ] **Step 5: Run the tests**

Run, in order: `cmd //c ".\build.bat test fw_par"`, then the full suite `cmd //c ".\build.bat test"` (it runs `diff_lts` and `golden_hybrid` and prints their lines below)
Expected:
- `suite_fw_par                 ok`, then `1 suite(s), 0 failure(s), 0 skip(s)`.
- `diff_lts`: 54 lines `<name> N calls checked`, each N > 0, 0 mismatches. The proof run printed, from `par_plan_tracks` to `par_scale_duration` (the six of this task between them):

```
  par_plan_tracks      3666 calls checked
  par_step_tracks      18342 calls checked
  par_track_sequential 49029 calls checked
  util_interp_step     41887 calls checked
  par_track_overlap    9583 calls checked
  par_track_start      9583 calls checked
  par_track_end        9583 calls checked
  par_scale_duration   26825 calls checked
```

  `par_track_overlap`, `par_track_start` and `par_track_end` are equal (9583: one start and one end track per overlap call); the counts are sampled (`DIFF_FULL_CALLS` 1000 per routine and file, then every 64th), so `par_step_tracks` x 10 is not the sum of the two steppers. Every line outside these six is identical to Task 1's Step 18. Then `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`, unchanged.
- `golden_hybrid`: `46 corpus files x 55 modes (all, then each routine alone)`, all identical.
- The full run (331 s in the proof run): `32 suite(s), 0 failure(s), 0 skip(s)`, with no warnings, the firmware syntax check passing, and `firmware_data` at `47702 converted bytes compared with the ROM, 0 differ`.

If `diff_lts` reports a mismatch, find the root cause against the dump and fix the C faithfully. Never loosen the comparison or drop a declared output.

- [ ] **Step 6: Commit**

```bash
git add src/firmware/par_track.h src/firmware/par_track.c src/engine/thunks.c tests/test_fw_par.c
git commit -m "The tracks in C: util_interp_step, par_track_start, par_track_end, par_track_sequential, par_track_overlap and par_step_tracks, with hooks; the frame bytes through par_slot_map and fw_wr_ind_y, util_interp_step's zero-step hang a fault (M4.4b 2.2, 2.6)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: The command events in C: `cmd_run_event` $BB74, `cmd_ev_index` $BBA0, `cmd_ev_sound_start` $BBA7, `cmd_ev_sound_end` $BC80, `cmd_ev_set_voicing` $BCEA and `cmd_ev_set_volume` $BCED, with their hooks and the `util_shl4` $C656 / `util_shl3` $C657 hooks (spec §2.2, §2.3, §2.5, §2.6, §4.5)

**ROM facts checked** (`rom_BB74_cmd_run_event.txt`, `rom_BBA0_cmd_ev_index.txt`, `rom_BBA7_cmd_ev_sound_start.txt`, `rom_BC80_cmd_ev_sound_end.txt`, `rom_BCEA_cmd_ev_set_voicing.txt`, `rom_BCED_cmd_ev_set_volume.txt`, `rom_C656_util_shl4.txt`, `rom_C657_util_shl3.txt`, each instruction by instruction; the callee `rom_ACE8_isr_set_volume.txt`; the callers `rom_B877_par_play.txt`, `rom_A832_cmd_apply_pitch.txt`, `rom_A92F_cmd_set_formant.txt`, `rom_A99A_cmd_set_mark_space.txt`, `rom_B450_cmd_hex_byte.txt`, and after them `rom_C581_ser_tx_service.txt`, `rom_C4B5_ser_tx_get.txt`, `rom_C504_ser_tx_send.txt`, `rom_C500_ser_tx_wait.txt`, `rom_C5D6_isr_par_ready.txt`, `rom_B465_isr_wait_frame.txt`, `rom_B445_frm_copy_rest_frame.txt`, `rom_C0FF_par_step_tracks.txt`, `rom_B9B5_frm_finish_frame.txt`; the ROM image for `cmd_sound_table`):
- **Sizes agree with spec §2.2:** 44/18, 7/4, 217/88, 106/40, 3/2, 6/3 bytes/instructions, and 1 + 4 instructions for `util_shl4` + `util_shl3` (5 bytes).
- **The scan's fault** goes through Task 1's `fw_fault(f, ROM_CMD_EV_SOUND_START)`.
- **`cmd_run_event`'s three indexed reads** (spec §2.6 confirmed): `LDA cmd_ev_next / INC cmd_ev_next / ASL A / TAX` makes X = 2n in 8 bits (n = the old `cmd_ev_next`); `cmd_ev_vector` = the word at $1306 + X (`LDA $1306,X`, `$1307,X`); after `INX / INX`, which wrap at X = $FE, `cmd_ev_pos` = the word at $1206 + X; then `LDX cmd_ev_next / DEX` gives X = n (8 bits) and A = `cmd_ev_args[n]` (`LDA $1406,X`). Write order: `cmd_ev_next`, `cmd_ev_vector` low, high, `cmd_ev_pos` low, high. The C keeps the order and the wraps; n = 127 reads the handler at $1404 and the position at $1206 (`test_run_event`).
- **The handlers are entered only by the `JMP ($1204)` at $BB9D** (no `JSR`/`JMP` absolute to $BBA0, $BBA7, $BCEA or $BCED; $BC80 also by `JSR` at $B8F4). The byte before $BBA0 is $12, the operand of that `JMP`: no fall-through into `cmd_ev_index`. None of the five reads X, Y or a flag on entry: `cmd_ev_sound_start` writes X (`TAX` $BBAA) and Y (`LDY #0` $BC36) before reading them, the others read only A or RAM. So the C passes A = the argument, X = n and the entry Y, and the exit registers are the handler's.
- **The fallback for any other handler address** (never queued: the producers write only the five `ROM_CMD_EV_*`) calls `fw_rom_call` at that address with A, X and Y as the `JMP` hands them over, after the C has done the bookkeeping the ROM did before its `JMP`. The ROM routine's `RTS` then returns to `cmd_run_event`'s caller, as it would from the `JMP`. **One thing cannot be handed over: the flags of the `LDA $1406,X`** (spec §2.6). `fw_rom_call` takes no flags in (`host_rom_call` starts the routine with the machine's own P). No handler the producers queue reads a flag, and the fallback is never taken on the corpus; `test_run_event_rom` pins the registers and the address.
- **`cmd_ev_sound_start`** (spec §2.6 confirmed, write order as `facts.md` §1.4):
  - `JSR util_shl3 / TAX`: X = A << 3 in 8 bits. The pointers come from `LDA $9F74,X` … `$9F7B,X` (`CMD_SOUND_TABLE + i + X`, `fw_rom8`). For effects 0–16 X ≤ $80. Every effect's flag pointer is $9FFC (`CMD_SOUND_DATA`: `fw_cmd_sound_flags` lands first), effect 0's arrays are at $A00E/$A012/$A015 and effect 16's at $A183/$A185/$A186 (the ROM image; `test_sound_start_end` pins them).
  - The scan starts from Y = $FF (`LDY #$FF` at $BC53, before `STY pros_seg_count`), `INY` first, so the first read is at index 0 and Y leaves = the index of the $FF. All 17 effects have it (lengths 3 … 1).
  - **Exit registers:** A = 0 (`LDA #0` $BC75), X = `cmd_ev_next` << 1 (8 bits; `cmd_ev_next` already incremented by `cmd_run_event`, so this is the following event's slot, the @T end event that `cmd_sound` queued right after the start), Y = the length. The end event's position and `cmd_ev_pos` become the length (in segments from `par_seg_index` 0), high bytes 0: the start overrides the position `cmd_run_event` has just copied.
  - A beyond 16 (never queued) reads past the 17 entries into the @T arrays. With A = $15, X = $A8 and the phoneme pointer is $2805 (RAM). With zeroed RAM no $FF follows, so the ROM would loop forever. The C stops after 256 reads with `fw_fault(f, ROM_CMD_EV_SOUND_START)` before the end event (`test_sound_start_no_end`).
- **`cmd_ev_sound_end`**: `BBS0 cmd_sound_active,$BC84 / RTS`: with bit 0 clear nothing is written and A, X and Y are untouched. Otherwise `CLB0` first (bit 0 only; the test keeps bit 7 set to show it), then the exact reverse set. A leaves as `cmd_sound_state[6]` (the last `LDA`, `par_next_rank`'s saved value); X and Y are untouched.
- **`cmd_ev_set_volume`**: `STA cmd_volume / JSR isr_set_volume / RTS`. `isr_set_volume` ($ACE8) reads only A (`EOR #$F / AND #$F / JSR util_shl3`), writes $75 and P3, and leaves A = the new P3 value, X and Y untouched. The C passes the entry registers and returns what the ROM call leaves. The test mock models that with a P3 byte `g_p3` (an SFR, not in RAM); $75 is the ROM routine's own write and is not modelled.
- **What the callers read after each call** (the thunk comments; every site checked):
  - `cmd_run_event` ($B88F) and every handler (returning to $B892): `JMP par_play`, then `LDX cmd_ev_next` ($B877), then `LDA par_seg_index` ($B87F) or, past the event test, `isr_par_ready` (`PHA`/`PLA`) and `LDY pros_seg_count` ($B8B1), or `isr_wait_frame` and `LDA #3`. `ser_tx_service` (`PHA`/`PLA`; `ser_tx_get` loads A and Y before reading them), `par_next_segment` and `par_step_tracks` (`LDX #$0A` first) read nothing either. Nothing is read.
  - `cmd_ev_sound_end` at $B8F4: `isr_par_ready` (`PHA`/`PLA`), `RTS` to $B2CA `JSR isr_volume_half` (`LDA cmd_volume`). Nothing is read.
  - `util_shl3`: A at $ACEF (`STA $75`) and $BBAA (`TAX`); the fall-through from `util_shl4` ($C656 `ASL`; the byte before $C657 is that $0A) hands its callers' reads over.
  - `util_shl4`: A at $A838 (`ORA cmd_prosody`), $A842 (`STA pros_pitch_centre`), $A955 and $A9B0 (`STA $75`; X next at $A957/$A9B2 `INX`, which `util_shl4` leaves), $B458 (`STA $1809`).
  - No caller reads a flag (`JMP`, `LDX`, `STA`, `ORA`, `TAX` follow; the carry of the `ASL`s is never read).
- **Where each hook fires in the hybrid engine** (`golden_hybrid`):
  - In this task's state `par_play` is still ROM, so `cmd_run_event` ($B88F) and `cmd_ev_sound_end` ($B8F4) fire in "all" mode, and the C `cmd_run_event` calls the four other handlers C to C. Those four fire only in their alone modes, entered by the ROM `cmd_run_event`'s `JMP`. After Task 5 the C `par_play` calls the C `cmd_run_event` and `cmd_ev_sound_end`, so all six fire alone only (spec §2.5).
  - `util_shl3` fires in "all" mode from the ROM `isr_set_volume` (M4.6), which `boot_reset` $A642, `boot_loop_play` $B2C4/$B2D2, `isr_volume_half` $B46F and `cmd_ev_set_volume` $BCEF call; the C `cmd_ev_set_volume` reaches it through `rom_call`, a nested thunk.
  - `util_shl4` never fires in "all" mode. Its five `JSR`s are in `cmd_apply_pitch` (called by `cmd_apply_speaker` $A798, `cmd_set_prosody` $A9F1, `cmd_set_pitch` $AA49, `cmd_set_source` $AB6A), `cmd_set_formant`, `cmd_set_mark_space` (both entered by `JMP (cmd_vector)`) and `cmd_hex_byte` (`cmd_set_prosody` $AA19). All of them are C since M4.2/M4.4a and use the inline. In its alone mode the ROM callers run and it fires.
  - In `util_shl3`'s alone mode the ROM `util_shl4` falls into the hook at $C657 with its caller's return address on the stack, and the thunk's `RTS` returns there: the same result.
- **The test file.**
  - The mock gains one case, `ROM_ISR_SET_VOLUME`, which also serves Task 5's `boot_loop_play`.
  - Includes are added after `engine/thunks.h`: `firmware/cmd_addr.h` (the five `ROM_CMD_EV_*`), `firmware/cmd_play_event.h` and `firmware/fw_ops.h` (`util_shl3`, for the mock).
  - The new tests use only the existing harness and `g_p3`.
- **Proof run** (the consolidated plan applied literally in a scratch worktree off main c5ca8fa, after Tasks 1 and 2): RED `tests\test_fw_par.c(11): fatal error C1083: Cannot open include file: 'firmware/cmd_play_event.h': No such file or directory`; then `suite_fw_par ok`; both scans of Step 4 as printed there; the full run as Step 5 says. While drafting (scratch only), each of these mutations failed `fw_par` (failing checks in parentheses): X = `cmd_ev_next` without the `DEX` (14), the `DEC` at `cmd_index_count` = 0 (1), no `cmd_ev_pos` high byte in the end event (6), `cmd_sound_state[6]`/`[7]` swapped (22), one `INX` instead of two (5), the scan reading before its `INY` (18).

**Files:**
- Create: `src/firmware/cmd_play_event.h`, `src/firmware/cmd_play_event.c`
- Modify: `src/engine/thunks.c` (eight thunks, `THUNK` lines and rows, two includes), `tests/test_fw_par.c` (three includes, `g_p3`, the `ROM_ISR_SET_VOLUME` mock case, the tests)

**Where the code goes.** `cmd_play_event.c`, one static helper per ROM block:

| Routine | Helper | ROM |
|---|---|---|
| `cmd_run_event` | `event_vector` | $BB74–$BB87: `cmd_ev_next` + 1, X = 2n, `cmd_ev_vector` |
| | `next_position` | $BB88–$BB95: `INX`/`INX` (8 bits), `cmd_ev_pos` |
| | `dispatch` | $BB96–$BB9F: X = n, A = `cmd_ev_args[n]`, `JMP (cmd_ev_vector)` by address (`k_events`), else `fw_rom_call` |
| `cmd_ev_sound_start` | `save_pointers` | $BBAB–$BBD2 |
| | `effect_pointers` | $BBD3–$BBFA |
| | `save_state` | $BBFB–$BC35 |
| | `first_phoneme` | $BC36–$BC42 |
| | `fixed_state` | $BC43–$BC60 |
| | `find_end` | $BC61–$BC67 (false after 256 reads) |
| | `end_event` | $BC68–$BC7F |
| `cmd_ev_sound_end` | `restore_pointers` | $BC86–$BCAD |
| | `restore_state` | $BCAE–$BCE8 |

`cmd_ev_index` (4 instructions), `cmd_ev_set_voicing` (2) and `cmd_ev_set_volume` (3) have no helper. `util_shl3`/`util_shl4` are Task 1's inlines; this task adds only their thunks.

**Interfaces:**
- Consumes:
  - Task 1: `util_shl3`, `util_shl4` (`fw_ops.h`); `fw_fault` (`fw.h`); `ROM_CMD_RUN_EVENT`, `ROM_ISR_SET_VOLUME`, `ROM_UTIL_SHL4`, `ROM_UTIL_SHL3` (`par_addr.h`); `DIFF_MAX` 64 (62 replacements after this task); the `waits` field (the eight new rows keep four initialisers, `waits` = false).
  - Task 2: nothing in code (no track routine is called). Its edits to `thunks.c` (after `par_plan_tracks`) and to `tests/test_fw_par.c` are untouched by this task's anchors, except one: this task's calls go after Task 2's last call, `    test_step_tracks();`, at the end of `suite_fw_par`, and its test functions after Task 2's, before `void suite_fw_par(void)`.
  - M4.4a/M4.2: the harness of `tests/test_fw_par.c` (`setup`, `snapshot`, `check_written`, `check_call`, `check_row`, `check_no_calls`, `g_calls`, `g_ncalls`, `mock_rom_call`); `ROM_CMD_EV_INDEX`, `ROM_CMD_EV_SOUND_START`, `ROM_CMD_EV_SOUND_END`, `ROM_CMD_EV_SET_VOICING`, `ROM_CMD_EV_SET_VOLUME` (`cmd_addr.h`); `fw_inc8` (`fw_ops.h`); the `fw_ram.h` names `CMD_EV_*`, `CMD_SOUND_*`, `CMD_INDEX_COUNT`, `CMD_VOICING`, `CMD_VOLUME`, `PROS_*_PTR`, `PAR_RANK_PTR`, `PAR_SEG_INDEX`, `PROS_SEG_COUNT`, `PROS_PITCH_CENTRE`, `CMD_SOURCE_PROSODY`, `PROS_PLAY_TARGET`, `PAR_NEXT_PHONEME`, `PAR_NEXT_RANK`; `CMD_SOUND_TABLE`, `CMD_SOUND_DATA`, `CMD_SOUND_END`, `PAR_VOICE_A`, `fw_par_voice_a` (`data/fw_data.h`).
- Produces (Task 5 calls `cmd_run_event` and `cmd_ev_sound_end` C to C and uses the mock case):

```c
/* src/firmware/cmd_play_event.h */
#define CMD_SOUND_ACTIVE_ON       0x01
#define CMD_SOUND_PITCH_CENTRE    0x80
#define CMD_SOUND_SOURCE_PROSODY  3
#define CMD_SOUND_VOICING         8
void cmd_run_event(fw *f, fw_regs *r);       /* $BB74 */
void cmd_ev_index(fw *f, fw_regs *r);        /* $BBA0 */
void cmd_ev_sound_start(fw *f, fw_regs *r);  /* $BBA7 */
void cmd_ev_sound_end(fw *f, fw_regs *r);    /* $BC80 */
void cmd_ev_set_voicing(fw *f, fw_regs *r);  /* $BCEA */
void cmd_ev_set_volume(fw *f, fw_regs *r);   /* $BCED */
```

  - The replacements `"cmd_run_event"` `0xBB74`, `"cmd_ev_index"` `0xBBA0`, `"cmd_ev_sound_start"` `0xBBA7`, `"cmd_ev_sound_end"` `0xBC80`, `"cmd_ev_set_voicing"` `0xBCEA`, `"cmd_ev_set_volume"` `0xBCED` after `par_reset_tracks`, and `"util_shl4"` `0xC656`, `"util_shl3"` `0xC657` after `cmd_hex_value`, all `FW_OUT_A | FW_OUT_X | FW_OUT_Y`: 54 + 8 = 62 replacements, 63 `golden_hybrid` modes.
  - In `tests/test_fw_par.c`: `static uint8_t g_p3` (the mock's P3) and the mock case `ROM_ISR_SET_VOLUME` (A = (P3 & $87) | ((A ^ $F) & $F) << 3, P3 = A; X and Y untouched). `test_run_event_rom();` is the last line of `suite_fw_par` (Task 5's anchor).

- [ ] **Step 1: Write the failing tests**

In `tests/test_fw_par.c`, replace

```c
#include "engine/thunks.h"
#include "firmware/par_addr.h"
```

with

```c
#include "engine/thunks.h"
#include "firmware/cmd_addr.h"
#include "firmware/cmd_play_event.h"
#include "firmware/fw_ops.h"
#include "firmware/par_addr.h"
```

Replace

```c
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;
```

with

```c
static rom_call_record g_calls[MAX_CALLS];
static unsigned g_ncalls;
static uint8_t g_p3;             /* the port P3 the mock isr_set_volume reads and writes (an SFR: not in g_ram) */
```

Replace

```c
/* Records every ROM call. The three frame routines par_setup_voice calls (M4.5) return the registers the ROM ones
   leave; any other call fails (no other routine of the playback set-up makes one). */
```

with

```c
/* Records every ROM call. The three frame routines par_setup_voice calls (M4.5) and isr_set_volume (M4.6, called by
   cmd_ev_set_volume) return the registers the ROM ones leave; any other call fails. */
```

Replace

```c
    case ROM_FRM_CLAMP_MARK_SPACE:   /* $B930-$B95C: LDY #$0A, INY; LDA #$0B at $B954; X untouched */
        r->a = 0x0B;
        r->y = 0x0B;
        return FW_CALL_RETURNED;
```

with

```c
    case ROM_FRM_CLAMP_MARK_SPACE:   /* $B930-$B95C: LDY #$0A, INY; LDA #$0B at $B954; X untouched */
        r->a = 0x0B;
        r->y = 0x0B;
        return FW_CALL_RETURNED;
    case ROM_ISR_SET_VOLUME:         /* $ACE8-$ACF9: EOR #$F / AND #$F / JSR util_shl3 / STA $75 / LDA #$87 / AND P3 /
                                        ORA $75 / STA P3: A = the new P3; X and Y untouched ($75, the ROM's scratch,
                                        is not modelled) */
        g_p3 = (uint8_t)((g_p3 & 0x87) | util_shl3((uint8_t)((r->a ^ 0x0F) & 0x0F)));
        r->a = g_p3;
        return FW_CALL_RETURNED;
```

Replace

```c
void suite_fw_par(void)
{
```

with (the tests go after Task 2's)

```c
/* ---- the command events (M4.4b Task 3) ---- */

static void test_event_rows(void)
{
    check_row("cmd_run_event", ROM_CMD_RUN_EVENT, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("cmd_ev_index", ROM_CMD_EV_INDEX, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("cmd_ev_sound_start", ROM_CMD_EV_SOUND_START, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("cmd_ev_sound_end", ROM_CMD_EV_SOUND_END, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("cmd_ev_set_voicing", ROM_CMD_EV_SET_VOICING, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("cmd_ev_set_volume", ROM_CMD_EV_SET_VOLUME, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("util_shl4", ROM_UTIL_SHL4, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("util_shl3", ROM_UTIL_SHL3, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
}

/* cmd_ev_index with cmd_index_count = n: TST / BEQ $BBA6 (0: nothing written), else DEC. A, X and Y untouched. */
static void ev_index_case(uint8_t n, uint8_t want)
{
    static const uint16_t w[] = { CMD_INDEX_COUNT };
    setup();
    g_ram[CMD_INDEX_COUNT] = n;
    snapshot();
    fw_regs r = { 0x11, 0x5A, 0xA5, 0 };
    cmd_ev_index(&g_f, &r);
    CHECK_EQ(g_ram[CMD_INDEX_COUNT], want);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, 0xA5);
    check_written(w, 1);
    check_no_calls();
}

static void test_ev_index(void)
{
    ev_index_case(0, 0);         /* $BBA2 BEQ taken: only an 8-bit wrap of the count reaches it */
    ev_index_case(1, 0);
    ev_index_case(0xFF, 0xFE);
}

/* cmd_ev_set_voicing: STA cmd_voicing; RTS. */
static void test_set_voicing(void)
{
    static const uint16_t w[] = { CMD_VOICING };
    setup();
    g_ram[CMD_VOICING] = 2;
    snapshot();
    fw_regs r = { 9, 0x5A, 0xA5, 0 };
    cmd_ev_set_voicing(&g_f, &r);
    CHECK_EQ(g_ram[CMD_VOICING], 9);
    CHECK_EQ(r.a, 9);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, 0xA5);
    check_written(w, 1);
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
    snapshot();
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

static void test_set_volume(void)
{
    set_volume_case(0x0A, 0xFF, 0xAF);   /* $0A ^ $F = 5, << 3 = $28; $FF & $87 = $87 */
    set_volume_case(0x00, 0x00, 0x78);   /* silent: $F << 3 */
    set_volume_case(0x0F, 0x3C, 0x04);   /* full: 0; $3C & $87 = $04 */
}

/* The playback state cmd_ev_sound_start saves, set to distinct values: the pointers $22-$29 = $10-$17, par_seg_index
   = $1234, pros_seg_count = $0156, cmd_voicing 5, pros_pitch_centre $66, cmd_source_prosody $27, pros_play_target
   $55, par_next_phoneme $3A, par_next_rank $0C; par_rank_ptr = voice A; the save area, the end event's position and
   cmd_ev_pos $EE; cmd_sound_active $80 (bit 7 is not the flag). */
static void sound_state(void)
{
    for (uint8_t i = 0; i < 8; i++)
        g_ram[PROS_PHONEME_PTR + i] = (uint8_t)(0x10 + i);
    fw_wr16(&g_f, PAR_RANK_PTR, PAR_VOICE_A);
    fw_wr16(&g_f, PAR_SEG_INDEX, 0x1234);
    fw_wr16(&g_f, PROS_SEG_COUNT, 0x0156);
    g_ram[CMD_VOICING] = 5;
    g_ram[PROS_PITCH_CENTRE] = 0x66;
    g_ram[CMD_SOURCE_PROSODY] = 0x27;
    g_ram[PROS_PLAY_TARGET] = 0x55;
    g_ram[PAR_NEXT_PHONEME] = 0x3A;
    g_ram[PAR_NEXT_RANK] = 0x0C;
    memset(g_ram + CMD_SOUND_SAVE, 0xEE, 8);
    memset(g_ram + CMD_SOUND_SEG_INDEX, 0xEE, 2);
    memset(g_ram + CMD_SOUND_STATE, 0xEE, 8);
    memset(g_ram + CMD_EV_POSITIONS, 0xEE, 256);
    fw_wr16(&g_f, CMD_EV_POS, 0xEEEE);
    g_ram[CMD_SOUND_ACTIVE] = 0x80;
}

/* cmd_ev_sound_start for effect e with cmd_ev_next = next, on sound_state(): the effect's four pointers from
   cmd_sound_table (phonemes, durations, pitches; the flags are shared, $9FFC), its first phoneme 59 and that
   phoneme's rank, and the end at index len. Then cmd_ev_sound_end restores all but the save area, the end event
   and cmd_ev_pos. */
static void sound_case(uint8_t e, uint8_t next, uint16_t phonemes, uint16_t durations, uint16_t pitches, uint8_t len)
{
    uint16_t at = (uint16_t)(CMD_EV_POSITIONS + (uint8_t)(next << 1));   /* LDX / TXA / ASL / TAX: 8 bits */
    const uint16_t w[] = {
        PROS_PHONEME_PTR, PROS_PHONEME_PTR + 1, PROS_DURATION_PTR, PROS_DURATION_PTR + 1, PROS_PITCH_PTR,
        PROS_PITCH_PTR + 1, PROS_FLAGS_PTR, PROS_FLAGS_PTR + 1,
        CMD_SOUND_SAVE, CMD_SOUND_SAVE + 1, CMD_SOUND_SAVE + 2, CMD_SOUND_SAVE + 3, CMD_SOUND_SAVE + 4,
        CMD_SOUND_SAVE + 5, CMD_SOUND_SAVE + 6, CMD_SOUND_SAVE + 7,
        CMD_SOUND_SEG_INDEX, CMD_SOUND_SEG_INDEX + 1,
        CMD_SOUND_STATE, CMD_SOUND_STATE + 1, CMD_SOUND_STATE + 2, CMD_SOUND_STATE + 3, CMD_SOUND_STATE + 4,
        CMD_SOUND_STATE + 5, CMD_SOUND_STATE + 6, CMD_SOUND_STATE + 7,
        PAR_NEXT_PHONEME, PAR_NEXT_RANK, PROS_PITCH_CENTRE, CMD_SOURCE_PROSODY, CMD_VOICING,
        PAR_SEG_INDEX, PAR_SEG_INDEX + 1, PROS_SEG_COUNT, PROS_SEG_COUNT + 1,
        at, (uint16_t)(at + 1), CMD_EV_POS, CMD_EV_POS + 1, CMD_SOUND_ACTIVE,
    };
    setup();
    sound_state();
    g_ram[CMD_EV_NEXT] = next;
    snapshot();
    fw_regs r = { e, 0x5A, 0xA5, 0 };
    cmd_ev_sound_start(&g_f, &r);
    for (uint8_t i = 0; i < 8; i++)                                    /* the old pointers saved, low byte first */
        CHECK_EQ(g_ram[CMD_SOUND_SAVE + i], 0x10 + i);
    CHECK_EQ(fw_rd16(&g_f, PROS_PHONEME_PTR), phonemes);
    CHECK_EQ(fw_rd16(&g_f, PROS_DURATION_PTR), durations);
    CHECK_EQ(fw_rd16(&g_f, PROS_PITCH_PTR), pitches);
    CHECK_EQ(fw_rd16(&g_f, PROS_FLAGS_PTR), CMD_SOUND_DATA);       /* fw_cmd_sound_flags lands first, at $9FFC */
    CHECK_EQ(fw_rd16(&g_f, CMD_SOUND_SEG_INDEX), 0x1234);
    static const uint8_t state[8] = { 0x56, 0x01, 5, 0x66, 0x27, 0x55, 0x0C, 0x3A };
    for (unsigned i = 0; i < 8; i++)
        CHECK_EQ(g_ram[CMD_SOUND_STATE + i], state[i]);
    CHECK_EQ(g_ram[PAR_NEXT_PHONEME], 59);                         /* every effect starts with phoneme 59 */
    CHECK_EQ(g_ram[PAR_NEXT_RANK], fw_par_voice_a[59].rank);       /* (par_rank_ptr),59: voice A's rank row */
    CHECK_EQ(g_ram[PROS_PITCH_CENTRE], CMD_SOUND_PITCH_CENTRE);
    CHECK_EQ(g_ram[CMD_SOURCE_PROSODY], CMD_SOUND_SOURCE_PROSODY);
    CHECK_EQ(g_ram[CMD_VOICING], CMD_SOUND_VOICING);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 0);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0x00FF);               /* STY pros_seg_count with Y = $FF */
    CHECK_EQ(fw_rd16(&g_f, at), len);                              /* the end event's position: the length */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), len);
    CHECK_EQ(g_ram[CMD_SOUND_ACTIVE], 0x81);                       /* SEB0 */
    CHECK_EQ(r.a, 0);                                              /* LDA #0 at $BC75 */
    CHECK_EQ(r.x, (uint8_t)(next << 1));
    CHECK_EQ(r.y, len);                                            /* the scan's Y */
    check_written(w, sizeof w / sizeof w[0]);
    check_no_calls();

    fw_regs s = { 0x11, 0x22, 0x33, 0 };
    cmd_ev_sound_end(&g_f, &s);
    CHECK_EQ(s.a, 0x0C);                                           /* the last LDA, cmd_sound_state[6] */
    CHECK_EQ(s.x, 0x22);
    CHECK_EQ(s.y, 0x33);
    for (uint8_t i = 0; i < 8; i++) {                              /* what the end only reads stays as the start */
        g_before[CMD_SOUND_SAVE + i] = (uint8_t)(0x10 + i);       /* wrote it; the end event and cmd_ev_pos too */
        g_before[CMD_SOUND_STATE + i] = state[i];
    }
    g_before[CMD_SOUND_SEG_INDEX] = 0x34;
    g_before[CMD_SOUND_SEG_INDEX + 1] = 0x12;
    g_before[at] = len;
    g_before[at + 1] = 0;
    g_before[CMD_EV_POS] = len;
    g_before[CMD_EV_POS + 1] = 0;
    check_written(NULL, 0);                                        /* the rest restored, cmd_sound_active $80 (CLB0) */
    check_no_calls();
}

static void test_sound_start_end(void)
{
    sound_case(0, 3, 0xA00E, 0xA012, 0xA015, 3);    /* @T0: 59 77 59, $FF at index 3; the end event at $120C */
    sound_case(16, 3, 0xA183, 0xA185, 0xA186, 1);   /* @TG/@TX: 59, $FF at index 1 */
    sound_case(0, 0x80, 0xA00E, 0xA012, 0xA015, 3); /* cmd_ev_next = 128: ASL wraps, X = 0: positions[0] */
}

/* A = $15 (never queued: @T takes 0-16): util_shl3 gives X = $A8, past the 17 entries, and the bytes there (the @T
   arrays at $A01C) point the phonemes at $2805, RAM, all zero here. No end in 256 bytes: the ROM would loop
   forever; the C faults at ROM_CMD_EV_SOUND_START before the end event. */
static void test_sound_start_no_end(void)
{
    setup();
    sound_state();
    g_ram[CMD_EV_NEXT] = 3;
    fw_regs r = { 0x15, 0x5A, 0xA5, 0 };
    cmd_ev_sound_start(&g_f, &r);
    CHECK_EQ(fw_rd16(&g_f, PROS_PHONEME_PTR), 0x2805);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, ROM_CMD_EV_SOUND_START);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0xEEEE);                   /* the end event is not written */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POSITIONS + 6), 0xEEEE);
    CHECK_EQ(g_ram[CMD_SOUND_ACTIVE], 0x80);
    CHECK_EQ(g_ncalls, 0);
}

/* cmd_ev_sound_end with cmd_sound_active bit 0 clear: BBS0 not taken, RTS: nothing written, A, X, Y untouched. */
static void test_sound_end_inactive(void)
{
    setup();
    sound_state();
    g_ram[CMD_SOUND_ACTIVE] = 0xFE;
    snapshot();
    fw_regs r = { 0x11, 0x5A, 0xA5, 0 };
    cmd_ev_sound_end(&g_f, &r);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, 0xA5);
    check_written(NULL, 0);
    check_no_calls();
}

/* cmd_run_event with cmd_ev_next = next: the handler word at $1306 + (next << 1) and the next position at $1206 +
   (next << 1) + 2 (each 8 bits), the argument at $1406 + next; the entry A and X are not read, Y reaches the
   handler. Sets those and runs it; the caller checks cmd_ev_pos (a @T start rewrites it) and the handler's work. */
static void run_event(uint8_t next, uint16_t handler, uint16_t pos, uint8_t arg, fw_regs *r)
{
    uint8_t x = (uint8_t)(next << 1);
    fw_wr16(&g_f, (uint16_t)(CMD_EV_HANDLERS + x), handler);
    fw_wr16(&g_f, (uint16_t)(CMD_EV_POSITIONS + (uint8_t)(x + 2)), pos);
    g_ram[CMD_EV_ARGS + next] = arg;
    g_ram[CMD_EV_NEXT] = next;
    g_ram[CMD_EV_COUNT] = 0x7F;                                    /* not read by cmd_run_event */
    snapshot();
    cmd_run_event(&g_f, r);
    CHECK_EQ(g_ram[CMD_EV_NEXT], (uint8_t)(next + 1));
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_VECTOR), handler);
}

static void test_run_event(void)
{
    static const uint16_t w_voicing[] = { CMD_EV_NEXT, CMD_EV_VECTOR, CMD_EV_VECTOR + 1, CMD_EV_POS, CMD_EV_POS + 1,
                                          CMD_VOICING };
    setup();                                                       /* @B7 queued third: X = 4, then 6 */
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    run_event(2, ROM_CMD_EV_SET_VOICING, 0x0123, 7, &r);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0x0123);
    CHECK_EQ(g_ram[CMD_VOICING], 7);
    CHECK_EQ(r.a, 7);                                              /* the handler's A, X, Y: A = the argument, */
    CHECK_EQ(r.x, 2);                                              /* X = cmd_ev_next - 1, */
    CHECK_EQ(r.y, 0x33);                                           /* Y as par_play had it */
    check_written(w_voicing, sizeof w_voicing / sizeof w_voicing[0]);
    check_no_calls();

    static const uint16_t w_index[] = { CMD_EV_NEXT, CMD_EV_VECTOR, CMD_EV_VECTOR + 1, CMD_EV_POS, CMD_EV_POS + 1,
                                        CMD_INDEX_COUNT };
    setup();                                                       /* the last of 128: X = $FE, INX / INX wrap to 0 */
    g_ram[CMD_INDEX_COUNT] = 5;
    r = (fw_regs){ 0x11, 0x22, 0x33, 0 };
    run_event(127, ROM_CMD_EV_INDEX, 0x0456, 0x99, &r);           /* handler at $1404, position at $1206 */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0x0456);
    CHECK_EQ(g_ram[CMD_INDEX_COUNT], 4);
    CHECK_EQ(r.a, 0x99);                                           /* LDA $1406,X with X = 127: $1485 */
    CHECK_EQ(r.x, 127);
    CHECK_EQ(r.y, 0x33);
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
    CHECK(!g_f.fault);

    setup();                                                       /* @T5, start then end, as cmd_sound queues them */
    sound_state();
    r = (fw_regs){ 0x11, 0x22, 0x33, 0 };
    run_event(4, ROM_CMD_EV_SOUND_START, 0x0040, 5, &r);           /* the end event's position, cmd_sound's own */
    CHECK_EQ(fw_rd16(&g_f, PROS_PHONEME_PTR), 0xA040);
    CHECK_EQ(g_ram[CMD_SOUND_ACTIVE], 0x81);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POSITIONS + 10), 3);            /* the start rewrote the next event's position */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 3);                        /* (X = cmd_ev_next << 1 = 10) and cmd_ev_pos */
    CHECK_EQ(r.a, 0);
    CHECK_EQ(r.x, 10);
    CHECK_EQ(r.y, 3);
    fw_wr16(&g_f, CMD_EV_HANDLERS + 10, ROM_CMD_EV_SOUND_END);
    g_ram[CMD_EV_ARGS + 5] = 5;
    r = (fw_regs){ 0x11, 0x22, 0x33, 0 };
    cmd_run_event(&g_f, &r);
    CHECK_EQ(g_ram[CMD_EV_NEXT], 6);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0xEEEE);                   /* positions[6], still sound_state()'s */
    CHECK_EQ(g_ram[CMD_SOUND_ACTIVE], 0x80);
    CHECK_EQ(fw_rd16(&g_f, PROS_PHONEME_PTR), 0x1110);
    CHECK_EQ(r.a, 0x0C);
    CHECK_EQ(r.x, 5);
    CHECK_EQ(r.y, 0x33);
    check_no_calls();
}

/* A handler word that is none of the five (the producers queue none): the same bookkeeping, then fw_rom_call at
   that address with A = the argument, X = cmd_ev_next - 1 and the entry Y, as JMP (cmd_ev_vector) hands them over.
   The mock fails the call, so the C faults there. */
static void test_run_event_rom(void)
{
    static const uint16_t w[] = { CMD_EV_NEXT, CMD_EV_VECTOR, CMD_EV_VECTOR + 1, CMD_EV_POS, CMD_EV_POS + 1 };
    setup();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    run_event(1, 0xABCD, 0x0200, 0x42, &r);
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0x0200);
    CHECK_EQ(g_ncalls, 1);
    check_call(0, 0xABCD, 0x42, 1, 0x33);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xABCD);
    check_written(w, sizeof w / sizeof w[0]);
}

void suite_fw_par(void)
{
```

At the end of `suite_fw_par`, replace (Task 2's last call)

```c
    test_step_tracks();
}
```

with

```c
    test_step_tracks();
    test_event_rows();
    test_ev_index();
    test_set_voicing();
    test_set_volume();
    test_sound_start_end();
    test_sound_start_no_end();
    test_sound_end_inactive();
    test_run_event();
    test_run_event_rom();
}
```

What the tests pin (spec §4.5, the part for this task):
- `test_event_rows`: the eight rows, their addresses and declared outputs.
- `test_ev_index`: `cmd_index_count` = 0 ($BBA2 taken: nothing written), 1 and $FF; registers untouched.
- `test_set_voicing`: `cmd_voicing` = A, nothing else, registers untouched.
- `test_set_volume`: `cmd_volume` = A, one `isr_set_volume` call with the entry A, X and Y and the strict budget, the returned A (three volume/P3 pairs), X and Y.
- `test_sound_start_end`: effect 0 and effect 16, and `cmd_ev_next` = 128 (X = 0 after the `ASL`): every saved byte, the effect's four pointers, the first phoneme 59 and its voice A rank, the fixed values, `par_seg_index` 0 and `pros_seg_count` $00FF, the end event's position and `cmd_ev_pos` = the length, `SEB0` with bit 7 kept, exit A = 0, X = 2n, Y = the length, nothing else written; then `cmd_ev_sound_end` restores every byte except the save area, the end event and `cmd_ev_pos`, clears bit 0 only, and leaves A = `cmd_sound_state[6]`, X and Y untouched.
- `test_sound_start_no_end`: no $FF within 256 bytes: the fault at `ROM_CMD_EV_SOUND_START` before the end event.
- `test_sound_end_inactive`: bit 0 clear (all other bits set): nothing written, registers untouched.
- `test_run_event`: the bookkeeping (`cmd_ev_next` + 1, `cmd_ev_vector`, `cmd_ev_pos`), and dispatch to each of the five handlers with the handler's registers (A = the argument, X = n, the entry Y):
  - `cmd_ev_next` = 127: the handler at $1404, the position at $1206 (`cmd_ev_positions[0]`), the argument at $1485;
  - @A through the ROM `isr_set_volume`;
  - a @T start, then its end through `cmd_run_event` again (the start rewrites the next position).
- `test_run_event_rom`: an address that is none of the five goes to `fw_rom_call` there with A = the argument, X = n and the entry Y, after the bookkeeping.

- [ ] **Step 2: Run it to see it fail**

Run: `cmd //c ".\build.bat test fw_par"`
Expected: a compile error, `tests\test_fw_par.c(11): fatal error C1083: Cannot open include file: 'firmware/cmd_play_event.h': No such file or directory`.

- [ ] **Step 3: Implement**

Create `src/firmware/cmd_play_event.h`:

```c
#ifndef FW_CMD_PLAY_EVENT_H
#define FW_CMD_PLAY_EVENT_H
/* The command events at playback (ROM $BB74-$BB9F and $BBA0-$BCF2), decompiled (M4.4b spec 2.2, 2.6): the
   dispatcher the frame loop par_play calls when playback reaches an event's position, and the five handlers the
   producers queue (cmd_event.c: @I+, @T, @B, @A). Every routine takes the registers the ROM has at its entry in r
   (r->p is not read: no routine here reads an entry flag) and returns in r the A, X and Y the ROM leaves at its
   RTS; a register the routine does not touch is left as it came in. */
#include "fw.h"

#define CMD_SOUND_ACTIVE_ON       0x01   /* cmd_sound_active bit 0: an effect plays (SEB0 $BC7D, BBS0 $BC80, CLB0
                                            $BC84) */
#define CMD_SOUND_PITCH_CENTRE    0x80   /* $BC43: pros_pitch_centre while an effect plays */
#define CMD_SOUND_SOURCE_PROSODY  3      /* $BC48: cmd_source_prosody while an effect plays (the R that
                                            pros_scale_target reads, AND #7, is 3) */
#define CMD_SOUND_VOICING         8      /* $BC4D: cmd_voicing while an effect plays (@B8: par_scale_voicing passes
                                            P10 unscaled) */

/* $BB74: runs the event cmd_ev_next. A = cmd_ev_next, then cmd_ev_next + 1; X = A << 1 (8 bits).
   cmd_ev_vector = cmd_ev_handlers[X] (the word at $1306 + X); after INX / INX, which wrap X at 8 bits, cmd_ev_pos =
   the word at $1206 + X, the next event's position (cmd_ev_positions[0] when cmd_ev_next was 127). Then X =
   cmd_ev_next - 1 and A = cmd_ev_args[X] (the byte at $1406 + X), and JMP (cmd_ev_vector): the C handler of the
   five cmd_ev_* addresses with that A and X and the entry Y; any other address through fw_rom_call with the same
   registers. r in: Y (passed to the handler). r out: what the handler leaves. */
void cmd_run_event(fw *f, fw_regs *r);
/* $BBA0, the @I+ event: cmd_index_count -= 1 unless it is 0 (TST / BEQ). A, X and Y untouched. */
void cmd_ev_index(fw *f, fw_regs *r);
/* $BBA7, the first @T event, A = the effect 0-16: X = util_shl3(A). The four playback pointers ($22-$29) to
   cmd_sound_save; the effect's four from cmd_sound_table + X (fw_rom8). par_seg_index to cmd_sound_seg_index;
   pros_seg_count, cmd_voicing, pros_pitch_centre, cmd_source_prosody, pros_play_target to cmd_sound_state[0..5],
   par_next_phoneme to [7], then par_next_rank to [6]. par_next_phoneme = the effect's first phoneme,
   (pros_phoneme_ptr),0; par_next_rank = its rank, (par_rank_ptr),phoneme. pros_pitch_centre = $80,
   cmd_source_prosody = 3, cmd_voicing = 8, par_seg_index = 0, pros_seg_count = $00FF. Y = the index of the first
   CMD_SOUND_END in the effect's phonemes (INY first, from $FF); the word at $1206 + (cmd_ev_next << 1, 8 bits), the
   end event's position, and cmd_ev_pos = Y, high bytes 0; cmd_sound_active bit 0 set. No end within 256 bytes: the
   ROM loops forever, the C faults at ROM_CMD_EV_SOUND_START. r in: A. r out: A = 0, X = cmd_ev_next << 1 (8 bits),
   Y = the end's index. */
void cmd_ev_sound_start(fw *f, fw_regs *r);
/* $BC80, the second @T event (and par_play's last call): when cmd_sound_active bit 0 is set, clears it and
   restores what cmd_ev_sound_start saved, in the ROM's order (cmd_sound_state[7] to par_next_phoneme before [6] to
   par_next_rank). r out: A = cmd_sound_state[6] when it restored, else as it came in; X and Y untouched. */
void cmd_ev_sound_end(fw *f, fw_regs *r);
/* $BCEA, the @B event: cmd_voicing = A. A, X and Y untouched. */
void cmd_ev_set_voicing(fw *f, fw_regs *r);
/* $BCED, the @A event: cmd_volume = A, then the ROM isr_set_volume (M4.6) with the entry registers. r out: what
   isr_set_volume leaves (A = the new P3 value, X and Y untouched). */
void cmd_ev_set_volume(fw *f, fw_regs *r);

#endif
```

Create `src/firmware/cmd_play_event.c`:

```c
#include "cmd_play_event.h"
#include "cmd_addr.h"
#include "fw_ops.h"
#include "par_addr.h"
#include "data/fw_data.h"

/* ---- cmd_run_event $BB74 ---- */

/* The C handlers by ROM address: the handler words the producers queue in cmd_ev_handlers (cmd_event.c). Any other
   address runs in ROM (spec 2.3); the producers queue none. The NULL row ends the table. */
static const struct { uint16_t addr; void (*fn)(fw *f, fw_regs *r); } k_events[] = {
    { ROM_CMD_EV_INDEX,       cmd_ev_index },
    { ROM_CMD_EV_SOUND_START, cmd_ev_sound_start },
    { ROM_CMD_EV_SOUND_END,   cmd_ev_sound_end },
    { ROM_CMD_EV_SET_VOICING, cmd_ev_set_voicing },
    { ROM_CMD_EV_SET_VOLUME,  cmd_ev_set_volume },
    { 0, NULL },
};

/* $BB74-$BB87: A = cmd_ev_next; INC cmd_ev_next; ASL A / TAX (8 bits); cmd_ev_vector = the word at $1306 + X, low
   byte first. */
static void event_vector(fw *f, fw_regs *r)
{
    r->a = fw_rd8(f, CMD_EV_NEXT);
    fw_inc8(f, CMD_EV_NEXT);
    r->a = (uint8_t)(r->a << 1);
    r->x = r->a;
    r->a = fw_rd8(f, (uint16_t)(CMD_EV_HANDLERS + r->x));
    fw_wr8(f, CMD_EV_VECTOR, r->a);
    r->a = fw_rd8(f, (uint16_t)(CMD_EV_HANDLERS + 1 + r->x));
    fw_wr8(f, CMD_EV_VECTOR + 1, r->a);
}

/* $BB88-$BB95: INX / INX (8 bits: X = $FE gives 0); cmd_ev_pos = the word at $1206 + X, low byte first. */
static void next_position(fw *f, fw_regs *r)
{
    r->x = (uint8_t)(r->x + 2);
    r->a = fw_rd8(f, (uint16_t)(CMD_EV_POSITIONS + r->x));
    fw_wr8(f, CMD_EV_POS, r->a);
    r->a = fw_rd8(f, (uint16_t)(CMD_EV_POSITIONS + 1 + r->x));
    fw_wr8(f, CMD_EV_POS + 1, r->a);
}

/* $BB96-$BB9F: LDX cmd_ev_next / DEX (8 bits); LDA $1406,X; JMP (cmd_ev_vector). */
static void dispatch(fw *f, fw_regs *r)
{
    r->x = (uint8_t)(fw_rd8(f, CMD_EV_NEXT) - 1);
    r->a = fw_rd8(f, (uint16_t)(CMD_EV_ARGS + r->x));
    uint16_t vector = fw_rd16(f, CMD_EV_VECTOR);
    for (unsigned i = 0; k_events[i].fn; i++) {
        if (k_events[i].addr == vector) {
            k_events[i].fn(f, r);
            return;
        }
    }
    fw_rom_call(f, vector, r, FW_ROM_CALL_BUDGET);   /* the handler's RTS returns to cmd_run_event's caller */
}

void cmd_run_event(fw *f, fw_regs *r)
{
    event_vector(f, r);
    next_position(f, r);
    dispatch(f, r);
}

/* ---- cmd_ev_index $BBA0 ---- */

void cmd_ev_index(fw *f, fw_regs *r)
{
    (void)r;
    if (fw_rd8(f, CMD_INDEX_COUNT) != 0)                         /* TST / BEQ $BBA6 */
        fw_wr8(f, CMD_INDEX_COUNT, (uint8_t)(fw_rd8(f, CMD_INDEX_COUNT) - 1));
}

/* ---- cmd_ev_sound_start $BBA7 ---- */

/* $BBAB-$BBD2: the four playback pointers (pros_phoneme_ptr, pros_duration_ptr, pros_pitch_ptr, pros_flags_ptr,
   $22-$29) to cmd_sound_save[0..3], each low byte then high byte. */
static void save_pointers(fw *f, fw_regs *r)
{
    for (uint8_t i = 0; i < 8; i++) {
        r->a = fw_rd8(f, (uint16_t)(PROS_PHONEME_PTR + i));
        fw_wr8(f, (uint16_t)(CMD_SOUND_SAVE + i), r->a);
    }
}

/* $BBD3-$BBFA: the effect's four pointers, LDA $9F74,X ... $9F7B,X, into $22-$29. */
static void effect_pointers(fw *f, fw_regs *r)
{
    for (uint8_t i = 0; i < 8; i++) {
        r->a = fw_rom8(f, (uint16_t)(CMD_SOUND_TABLE + i + r->x));
        fw_wr8(f, (uint16_t)(PROS_PHONEME_PTR + i), r->a);
    }
}

/* $BBFB-$BC35: par_seg_index to cmd_sound_seg_index; pros_seg_count, cmd_voicing, pros_pitch_centre,
   cmd_source_prosody and pros_play_target to cmd_sound_state[0..5]; par_next_phoneme to [7], then par_next_rank to
   [6]. */
static void save_state(fw *f, fw_regs *r)
{
    static const struct { uint16_t from, to; } k_saves[] = {
        { PAR_SEG_INDEX,      CMD_SOUND_SEG_INDEX },
        { PAR_SEG_INDEX + 1,  CMD_SOUND_SEG_INDEX + 1 },
        { PROS_SEG_COUNT,     CMD_SOUND_STATE },
        { PROS_SEG_COUNT + 1, CMD_SOUND_STATE + 1 },
        { CMD_VOICING,        CMD_SOUND_STATE + 2 },
        { PROS_PITCH_CENTRE,  CMD_SOUND_STATE + 3 },
        { CMD_SOURCE_PROSODY, CMD_SOUND_STATE + 4 },
        { PROS_PLAY_TARGET,   CMD_SOUND_STATE + 5 },
        { PAR_NEXT_PHONEME,   CMD_SOUND_STATE + 7 },
        { PAR_NEXT_RANK,      CMD_SOUND_STATE + 6 },
    };
    for (unsigned i = 0; i < sizeof k_saves / sizeof k_saves[0]; i++) {
        r->a = fw_rd8(f, k_saves[i].from);
        fw_wr8(f, k_saves[i].to, r->a);
    }
}

/* $BC36-$BC42: LDY #0; par_next_phoneme = (pros_phoneme_ptr),Y; TAY; par_next_rank = (par_rank_ptr),Y. The pointer is
   now into ROM. */
static void first_phoneme(fw *f, fw_regs *r)
{
    r->y = 0;
    r->a = fw_ind_y(f, PROS_PHONEME_PTR, r->y);
    fw_wr8(f, PAR_NEXT_PHONEME, r->a);
    r->y = r->a;
    r->a = fw_ind_y(f, PAR_RANK_PTR, r->y);
    fw_wr8(f, PAR_NEXT_RANK, r->a);
}

/* $BC43-$BC60: the fixed pitch centre, prosody and voicing; LDA #0 / LDY #$FF: par_seg_index = 0, pros_seg_count+1
   = 0, pros_seg_count = $FF. */
static void fixed_state(fw *f, fw_regs *r)
{
    r->a = CMD_SOUND_PITCH_CENTRE;
    fw_wr8(f, PROS_PITCH_CENTRE, r->a);
    r->a = CMD_SOUND_SOURCE_PROSODY;
    fw_wr8(f, CMD_SOURCE_PROSODY, r->a);
    r->a = CMD_SOUND_VOICING;
    fw_wr8(f, CMD_VOICING, r->a);
    r->a = 0;
    r->y = 0xFF;
    fw_wr8(f, PAR_SEG_INDEX, r->a);
    fw_wr8(f, PAR_SEG_INDEX + 1, r->a);
    fw_wr8(f, PROS_SEG_COUNT + 1, r->a);
    fw_wr8(f, PROS_SEG_COUNT, r->y);
}

/* $BC61-$BC67: INY; LDA (pros_phoneme_ptr),Y; CMP #$FF; BNE: Y = the end's index (8 bits; from Y = $FF, the first
   read is at 0). The ROM loops forever without an end; the C stops after 256 reads and returns false. */
static bool find_end(fw *f, fw_regs *r)
{
    for (unsigned n = 0; n < 0x100; n++) {
        r->y = (uint8_t)(r->y + 1);
        r->a = fw_ind_y(f, PROS_PHONEME_PTR, r->y);
        if (r->a == CMD_SOUND_END)
            return true;
    }
    return false;
}

/* $BC68-$BC7F: LDX cmd_ev_next / TXA / ASL A / TAX (8 bits); TYA: the word at $1206 + X and cmd_ev_pos = Y, high
   bytes 0 (LDA #0); SEB0 cmd_sound_active. */
static void end_event(fw *f, fw_regs *r)
{
    r->x = fw_rd8(f, CMD_EV_NEXT);
    r->a = (uint8_t)(r->x << 1);
    r->x = r->a;
    r->a = r->y;
    fw_wr8(f, (uint16_t)(CMD_EV_POSITIONS + r->x), r->a);
    fw_wr8(f, CMD_EV_POS, r->a);
    r->a = 0;
    fw_wr8(f, (uint16_t)(CMD_EV_POSITIONS + 1 + r->x), r->a);
    fw_wr8(f, CMD_EV_POS + 1, r->a);
    fw_wr8(f, CMD_SOUND_ACTIVE, (uint8_t)(fw_rd8(f, CMD_SOUND_ACTIVE) | CMD_SOUND_ACTIVE_ON));
}

void cmd_ev_sound_start(fw *f, fw_regs *r)
{
    r->a = util_shl3(r->a);                                     /* $BBA7 JSR util_shl3 */
    r->x = r->a;                                                /* TAX */
    save_pointers(f, r);
    effect_pointers(f, r);
    save_state(f, r);
    first_phoneme(f, r);
    fixed_state(f, r);
    if (!find_end(f, r)) {
        fw_fault(f, ROM_CMD_EV_SOUND_START);                    /* the ROM never leaves the scan */
        return;
    }
    end_event(f, r);
}

/* ---- cmd_ev_sound_end $BC80 ---- */

/* $BC86-$BCAD: cmd_sound_save[0..3] back into the four playback pointers, each low byte then high byte. */
static void restore_pointers(fw *f, fw_regs *r)
{
    for (uint8_t i = 0; i < 8; i++) {
        r->a = fw_rd8(f, (uint16_t)(CMD_SOUND_SAVE + i));
        fw_wr8(f, (uint16_t)(PROS_PHONEME_PTR + i), r->a);
    }
}

/* $BCAE-$BCE8: the reverse of save_state, in the same order ([7] to par_next_phoneme before [6] to par_next_rank). */
static void restore_state(fw *f, fw_regs *r)
{
    static const struct { uint16_t from, to; } k_restores[] = {
        { CMD_SOUND_SEG_INDEX,     PAR_SEG_INDEX },
        { CMD_SOUND_SEG_INDEX + 1, PAR_SEG_INDEX + 1 },
        { CMD_SOUND_STATE,         PROS_SEG_COUNT },
        { CMD_SOUND_STATE + 1,     PROS_SEG_COUNT + 1 },
        { CMD_SOUND_STATE + 2,     CMD_VOICING },
        { CMD_SOUND_STATE + 3,     PROS_PITCH_CENTRE },
        { CMD_SOUND_STATE + 4,     CMD_SOURCE_PROSODY },
        { CMD_SOUND_STATE + 5,     PROS_PLAY_TARGET },
        { CMD_SOUND_STATE + 7,     PAR_NEXT_PHONEME },
        { CMD_SOUND_STATE + 6,     PAR_NEXT_RANK },
    };
    for (unsigned i = 0; i < sizeof k_restores / sizeof k_restores[0]; i++) {
        r->a = fw_rd8(f, k_restores[i].from);
        fw_wr8(f, k_restores[i].to, r->a);
    }
}

void cmd_ev_sound_end(fw *f, fw_regs *r)
{
    if (!(fw_rd8(f, CMD_SOUND_ACTIVE) & CMD_SOUND_ACTIVE_ON))   /* BBS0 cmd_sound_active, $BC84 / RTS */
        return;
    fw_wr8(f, CMD_SOUND_ACTIVE, (uint8_t)(fw_rd8(f, CMD_SOUND_ACTIVE) & ~CMD_SOUND_ACTIVE_ON));   /* CLB0 */
    restore_pointers(f, r);
    restore_state(f, r);
}

/* ---- cmd_ev_set_voicing $BCEA, cmd_ev_set_volume $BCED ---- */

void cmd_ev_set_voicing(fw *f, fw_regs *r) { fw_wr8(f, CMD_VOICING, r->a); }

void cmd_ev_set_volume(fw *f, fw_regs *r)
{
    fw_wr8(f, CMD_VOLUME, r->a);
    fw_rom_call(f, ROM_ISR_SET_VOLUME, r, FW_ROM_CALL_BUDGET);  /* $BCEF; its RTS is ours ($BCF2) */
}
```

In `src/engine/thunks.c`:

- Replace

```c
#include "firmware/cmd_apply.h"
#include "firmware/pros_seg.h"
```

with

```c
#include "firmware/cmd_apply.h"
#include "firmware/cmd_play_event.h"
#include "firmware/fw_ops.h"
#include "firmware/pros_seg.h"
```

- Replace the end of `par_reset_tracks_regs`

```c
    par_reset_tracks(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

  with

```c
    par_reset_tracks(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* cmd_run_event $BB74: Y in (handed to the handler, which does not read it); A and X are written first. A, X and Y
   out as the handler leaves them (the C handler, or the ROM one through rom_call). Its one ROM caller, par_play
   ($B88F), goes on at $B892 JMP par_play: LDX cmd_ev_next ($B877), then LDA par_seg_index ($B87F) or, past the
   event test, isr_par_ready (PHA / PLA) and LDY pros_seg_count ($B8B1), par_next_segment or par_step_tracks (each
   loads A, X and Y before reading them), or isr_wait_frame and LDA #3 ($B8D5). No caller reads a flag (JMP, LDX
   follow). From the ROM par_play only. Declared: A, X, Y. */
static void cmd_run_event_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    cmd_run_event(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* cmd_ev_index $BBA0: no register input; A, X and Y untouched (TST / DEC). Entered by cmd_run_event's JMP
   (cmd_ev_vector) ($BB9D); its RTS returns to par_play $B892 (JMP par_play: LDX, as cmd_run_event's row). No caller
   reads a flag. Declared: A, X, Y. */
static void cmd_ev_index_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    cmd_ev_index(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* cmd_ev_sound_start $BBA7: A in (the effect; X and Y are written before they are read: TAX at $BBAA, LDY #0 at
   $BC36). A = 0, X = cmd_ev_next << 1 and Y = the end's index out. Entered by cmd_run_event's JMP (cmd_ev_vector)
   ($BB9D); its RTS returns to par_play $B892 (JMP par_play: LDX, as cmd_run_event's row). No caller reads a flag.
   Declared: A, X, Y. */
static void cmd_ev_sound_start_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    cmd_ev_sound_start(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* cmd_ev_sound_end $BC80: no register input. A = cmd_sound_state[6] when it restores, else as it came in; X and Y
   untouched. Entered by cmd_run_event's JMP (cmd_ev_vector) ($BB9D), returning to par_play $B892 (JMP par_play:
   LDX, as cmd_run_event's row), and called by par_play $B8F4, which calls isr_par_ready next (PHA / PLA, A, X and Y
   preserved) and returns ($B8FA RTS) to boot_play_body $B2CA, JSR isr_volume_half (LDA cmd_volume first). No caller
   reads a flag. From the ROM par_play only. Declared: A, X, Y. */
static void cmd_ev_sound_end_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    cmd_ev_sound_end(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* cmd_ev_set_voicing $BCEA: A in; A, X and Y untouched (STA cmd_voicing). Entered by cmd_run_event's JMP
   (cmd_ev_vector) ($BB9D); its RTS returns to par_play $B892 (JMP par_play: LDX, as cmd_run_event's row). No caller
   reads a flag. Declared: A, X, Y. */
static void cmd_ev_set_voicing_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    cmd_ev_set_voicing(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* cmd_ev_set_volume $BCED: A in (X and Y reach isr_set_volume, which does not read them). A, X and Y out as
   isr_set_volume leaves them (A = the new P3 value, X and Y untouched). Entered by cmd_run_event's JMP
   (cmd_ev_vector) ($BB9D); its RTS returns to par_play $B892 (JMP par_play: LDX, as cmd_run_event's row). No caller
   reads a flag. Declared: A, X, Y. */
static void cmd_ev_set_volume_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    cmd_ev_set_volume(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

- Replace the end of `cmd_hex_value_regs`

```c
    uint8_t v = cmd_hex_value(f, c->a);
    c->a = v;
    c->p = (uint8_t)(v == 0 ? c->p | CPU740_Z : c->p & ~CPU740_Z);
}
```

  with

```c
    uint8_t v = cmd_hex_value(f, c->a);
    c->a = v;
    c->p = (uint8_t)(v == 0 ? c->p | CPU740_Z : c->p & ~CPU740_Z);
}

/* util_shl4 $C656: A in; A << 4 out (8 bits); X and Y untouched. Callers read A: cmd_apply_pitch ($A838 ORA
   cmd_prosody, $A842 STA pros_pitch_centre), cmd_set_formant ($A955 STA $75), cmd_set_mark_space ($A9B0 STA $75),
   cmd_hex_byte ($B458 STA $1809); the first two of these read X next ($A957, $A9B2 INX), which util_shl4 leaves. No
   caller reads a flag (ORA, STA follow; the carry is not read). All four callers are C and inline it: in "all"
   mode it never fires. Declared: A, X, Y. */
static void util_shl4_regs(fw *f, cpu740 *c)
{
    (void)f;
    c->a = util_shl4(c->a);
}

/* util_shl3 $C657: A in; A << 3 out (8 bits); X and Y untouched. Callers read A: isr_set_volume ($ACEF STA $75),
   cmd_ev_sound_start ($BBAA TAX); the ROM util_shl4 falls into it ($C656 ASL), so its callers' reads apply too (A,
   above). No caller reads a flag (STA, TAX follow). Fires in "all" mode from the ROM isr_set_volume, which stays
   ROM (M4.6; called by boot_reset $A642, boot_loop_play $B2C4/$B2D2, isr_volume_half $B46F and cmd_ev_set_volume
   $BCEF, the C ones through rom_call). Declared: A, X, Y. */
static void util_shl3_regs(fw *f, cpu740 *c)
{
    (void)f;
    c->a = util_shl3(c->a);
}
```

- Replace

```c
THUNK(par_reset_tracks)
THUNK(par_next_segment)
```

with

```c
THUNK(par_reset_tracks)
THUNK(cmd_run_event)
THUNK(cmd_ev_index)
THUNK(cmd_ev_sound_start)
THUNK(cmd_ev_sound_end)
THUNK(cmd_ev_set_voicing)
THUNK(cmd_ev_set_volume)
THUNK(par_next_segment)
```

- Replace

```c
THUNK(cmd_hex_value)
THUNK(ph_append_phrase_pause)
```

with

```c
THUNK(cmd_hex_value)
THUNK(util_shl4)
THUNK(util_shl3)
THUNK(ph_append_phrase_pause)
```

- Replace

```c
    { "par_reset_tracks",   0xBB35, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_reset_tracks },
```

with

```c
    { "par_reset_tracks",   0xBB35, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_reset_tracks },
    { "cmd_run_event",      0xBB74, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_run_event },
    { "cmd_ev_index",       0xBBA0, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_ev_index },
    { "cmd_ev_sound_start", 0xBBA7, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_ev_sound_start },
    { "cmd_ev_sound_end",   0xBC80, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_ev_sound_end },
    { "cmd_ev_set_voicing", 0xBCEA, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_ev_set_voicing },
    { "cmd_ev_set_volume",  0xBCED, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_ev_set_volume },
```

- Replace

```c
    { "cmd_hex_value",      0xC520, FW_OUT_A | FW_OUT_X | FW_OUT_Y | FW_OUT_Z, t_cmd_hex_value },
```

with

```c
    { "cmd_hex_value",      0xC520, FW_OUT_A | FW_OUT_X | FW_OUT_Y | FW_OUT_Z, t_cmd_hex_value },
    { "util_shl4",          0xC656, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_util_shl4 },
    { "util_shl3",          0xC657, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_util_shl3 },
```

The table stays in address order (62 rows with Task 2's six). The new file name clashes with no basename: `ls src/firmware src/firmware/data src/engine tests | grep -E "^cmd_play_event"` shows only `cmd_play_event.c` and `cmd_play_event.h` (`cmd_event.c` is a different name).

- [ ] **Step 4: Faithfulness check against the dumps**

Read each dump (`build/ghidra740/dump/`, read-only) side by side with its C, instruction by instruction:
- `rom_BB74_cmd_run_event.txt`:
  - `LDA cmd_ev_next / INC cmd_ev_next / ASL A / TAX` → `event_vector`'s `r->a`, `fw_inc8`, `<< 1`, `r->x`;
  - `LDA $1306,X / STA cmd_ev_vector / LDA $1307,X / STA cmd_ev_vector+1` → the two `CMD_EV_HANDLERS` reads and stores;
  - `INX / INX` → `(uint8_t)(r->x + 2)`; `LDA $1206,X / STA cmd_ev_pos / LDA $1207,X / STA cmd_ev_pos+1` → `next_position`;
  - `LDX cmd_ev_next / DEX / LDA $1406,X` → `dispatch`'s `r->x` and `r->a`;
  - `JMP ($1204)` → the `k_events` lookup on the word in `CMD_EV_VECTOR`, else `fw_rom_call` there with `r`.
- `rom_BBA0_cmd_ev_index.txt`: `TST cmd_index_count / BEQ $BBA6 / DEC` → the `!= 0` test and the decrement; `RTS` with no register written.
- `rom_BBA7_cmd_ev_sound_start.txt`:
  - `JSR util_shl3 / TAX` → `util_shl3(r->a)`, `r->x`;
  - $BBAB–$BBD2 → `save_pointers` ($22+i to $1800+i, i = 0…7, in address order);
  - $BBD3–$BBFA → `effect_pointers` (`$9F74+i,X` to $22+i);
  - $BBFB–$BC35 → `save_state`'s ten rows in the dump's order (`par_next_phoneme` → $17FF before `par_next_rank` → $17FE);
  - $BC36–$BC42 → `first_phoneme` (`LDY #0 / LDA (22),Y / STA / TAY / LDA (2E),Y / STA`);
  - $BC43–$BC60 → `fixed_state` (`LDA #$80`, `#3`, `#8`; `LDA #0 / LDY #$FF`; the three `STA`s, then `STY pros_seg_count`);
  - $BC61–$BC67 → `find_end` (`INY` before the read, `CMP #$FF / BNE`);
  - $BC68–$BC7F → `end_event` (`LDX cmd_ev_next / TXA / ASL A / TAX / TYA`, `STA $1206,X`, `STA cmd_ev_pos`, `LDA #0`, `STA $1207,X`, `STA cmd_ev_pos+1`, `SEB0 $5F`).
- `rom_BC80_cmd_ev_sound_end.txt`: `BBS0 $5F,$BC84 / RTS` → the early `return`; `CLB0 $5F` → the `& ~CMD_SOUND_ACTIVE_ON` store; $BC86–$BCAD → `restore_pointers`; $BCAE–$BCE8 → `restore_state` (the reverse rows, [7] before [6]).
- `rom_BCEA_cmd_ev_set_voicing.txt`: `STA cmd_voicing / RTS`. `rom_BCED_cmd_ev_set_volume.txt`: `STA cmd_volume / JSR isr_set_volume / RTS` → the store and `fw_rom_call(f, ROM_ISR_SET_VOLUME, r, …)`.
- `rom_C656_util_shl4.txt` / `rom_C657_util_shl3.txt`: one `ASL`, then the three `ASL`s and `RTS` → Task 1's inlines; the thunks set only A.
- For the thunk comments: every caller site named in them, in `rom_B877_par_play.txt`, `rom_ACE8_isr_set_volume.txt`, `rom_A832_cmd_apply_pitch.txt`, `rom_A92F_cmd_set_formant.txt`, `rom_A99A_cmd_set_mark_space.txt`, `rom_B450_cmd_hex_byte.txt`, `rom_A61C_boot_reset.txt`, `rom_A7D4_boot_loop_play.txt`, `rom_B46C_isr_volume_half.txt`.
- For the mock: `rom_ACE8_isr_set_volume.txt` (A out = the new P3, X and Y not written).

Confirm the callers with:

```bash
python -c "import re,os;d=open(os.environ['APOLLO2_ROM'],'rb').read();[print(hex(a),hex(op),[hex(m.start()) for m in re.finditer(re.escape(bytes([op,a&255,a>>8])),d)]) for a in (0xBB74,0xBBA0,0xBBA7,0xBC80,0xBCEA,0xBCED,0xC656,0xC657,0xACE8) for op in (0x20,0x4C)]"
```

Expected:

```
0xbb74 0x20 ['0xb88f']
0xbb74 0x4c []
0xbba0 0x20 []
0xbba0 0x4c []
0xbba7 0x20 []
0xbba7 0x4c []
0xbc80 0x20 ['0xb8f4']
0xbc80 0x4c []
0xbcea 0x20 []
0xbcea 0x4c []
0xbced 0x20 []
0xbced 0x4c []
0xc656 0x20 ['0xa835', '0xa83f', '0xa952', '0xa9ad', '0xb455']
0xc656 0x4c []
0xc657 0x20 ['0xacec', '0xbba7']
0xc657 0x4c []
0xace8 0x20 ['0xa642', '0xb2c4', '0xb2d2', '0xb46f', '0xbcef']
0xace8 0x4c []
```

The four handlers with no `JSR`/`JMP` are entered only by `JMP ($1204)` at $BB9D. Then the relative branches into the eight entries, and the byte before each:

```bash
python -c "
import os
d=open(os.environ['APOLLO2_ROM'],'rb').read()
br={0x10,0x30,0x50,0x70,0x80,0x90,0xB0,0xD0,0xF0}
s=lambda b:b-256 if b>127 else b
for t in (0xBB74,0xBBA0,0xBBA7,0xBC80,0xBCEA,0xBCED,0xC656,0xC657):
  h=[hex(p) for p in range(0x8000,0xFFFE) if (d[p] in br or d[p]&0x1F in (3,0x13)) and p+2+s(d[p+1])==t]
  h+=[hex(p) for p in range(0x8000,0xFFFD) if d[p]&0x1F in (7,0x17) and p+3+s(d[p+2])==t]
  print(hex(t),h,hex(d[t-1]))
"
```

Expected:

```
0xbb74 [] 0x60
0xbba0 [] 0x12
0xbba7 [] 0x60
0xbc80 [] 0x60
0xbcea [] 0x60
0xbced [] 0x60
0xc656 [] 0x60
0xc657 [] 0xa
```

No branch reaches an entry. Each entry follows an `RTS` ($60), except $BBA0, which follows the operand $12 of `JMP ($1204)` (an indirect jump: no fall-through), and $C657, which follows `util_shl4`'s `ASL A` ($0A): the fall-through of spec §2.5.

- [ ] **Step 5: Run the tests**

Run, in order: `cmd //c ".\build.bat test fw_par"`, then the full suite `cmd //c ".\build.bat test"` (it runs `diff_lts` and `golden_hybrid` and prints their lines; no separate filtered runs).
Expected:
- `suite_fw_par                 ok`, then `1 suite(s), 0 failure(s), 0 skip(s)`.
- `diff_lts`: 62 lines `<name> N calls checked`, each with N > 0; the eight new ones after `par_reset_tracks` and after `cmd_hex_value`. The proof run printed:

```
  par_reset_tracks     176 calls checked
  cmd_run_event        119 calls checked
  cmd_ev_index         2 calls checked
  cmd_ev_sound_start   53 calls checked
  cmd_ev_sound_end     229 calls checked
  cmd_ev_set_voicing   8 calls checked
  cmd_ev_set_volume    3 calls checked
  par_next_segment     3666 calls checked
```

```
  cmd_hex_value        147 calls checked
  util_shl4            244 calls checked
  util_shl3            1051 calls checked
  ph_append_phrase_pause 364 calls checked
```

  Every other count is unchanged from Task 2. Then `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`, unchanged, and 0 mismatches.
- `golden_hybrid`: `46 corpus files x 63 modes (all, then each routine alone)`, all identical.
- The full run (369 s in the proof run): `32 suite(s), 0 failure(s), 0 skip(s)`, with no warnings, the firmware syntax check passing, and `firmware_data` at its Task 1 count (`47702 converted bytes compared with the ROM, 0 differ`; this task does not change it).

If `diff_lts` reports a mismatch, find the root cause against the dump and fix the C faithfully. Never loosen the comparison or drop a declared output.

- [ ] **Step 6: Commit**

```bash
git add src/firmware/cmd_play_event.h src/firmware/cmd_play_event.c src/engine/thunks.c tests/test_fw_par.c
git commit -m "The command events in C (cmd_run_event, cmd_ev_index, cmd_ev_sound_start, cmd_ev_sound_end, cmd_ev_set_voicing, cmd_ev_set_volume), with hooks, and the util_shl4/util_shl3 hooks (M4.4b 2.2, 2.3, 2.5, 2.6, 4.5)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: `diff_play`, the live differential test of the routines that wait on the timer interrupt, with its ROM-against-ROM self-check (spec §4.1)

**ROM facts checked / what was verified while proving the plan** (dumps `rom_B877_par_play.txt`, `rom_A7D4_boot_loop_play.txt` (with `boot_play_body` $B2B3), `rom_B2D9_isr_timer_dsp.txt`, `rom_B9B5_frm_finish_frame.txt`, `rom_A79F_boot_selftest_sound.txt`, `rom_A61C_boot_reset.txt`, `rom_C581_ser_tx_service.txt`, `rom_C5D6_isr_par_ready.txt`, `data.txt`; `src/machine/machine.c` for the clone, the hooks and the frame capture):
- **Only a real call of `par_play` is observed.** `JSR $B877` is only at $B2C7; `JMP $B877` at $B892, $B8AE and $B914 (the ROM scan in Task 5 Step 4). A hook on $B2C7 arms the next $B877 entry; the hook at $B877 checks only an armed entry and disarms. Hooks fire once per execution of the instruction: `find_hook` skips the PC while an interrupt is about to be taken, and fires when the routine is entered again after the `RTI`. `boot_loop_play` is entered only by `JSR` ($A687, $A7BA, $A87C, $B204); the bytes before both entries are data (`boot_selftest_fail_cmds` $A7C5–$A7D3 ends with its 0; `unused_B7B1` runs up to $B876), so no fall-through reaches them.
- **The boot's own call.** `boot_selftest_sound`'s `JSR boot_loop_play` at $A7BA pushes $A7BC: the routine returns to $A7BD. `return_address()` reads the two bytes above SP at the entry. Every corpus run includes the boot (`rom_engine_run` boots to idle first); its call is identical in every file, so it is checked in the boot run only, with the `par_play` inside it (spec §4.1).
- **The copies.** `machine_clone` clears the hooks, the outputs and the interrupt hold, so both copies run with interrupts live, and each gets its own frame callback (`machine_set_outputs`) and hooks. Copy A: `machine_finish_routine` (SP above its entry value; the timer interrupt's pushes stay below), budget 1 G cycles. Copy B in the self-check: the same; with a replacement: the thunk on a bridge bound to the copy (`fw_bridge_init`), which emulates the `RTS`, so both stop at the return address with the same SP.
- **The swap snapshot.** `capture_frame` calls the frame callback at $B306 (`isr_swap_buffers`) before the instruction runs; the callback gets the produced frame and the copy. The snapshot is the whole 16 KiB of RAM (the comparison skips $DA–$FF and $0100–$01FF), the P2 latch and `uart_tx_count`/`uart_tx_last`. Copy A's snapshots are kept (`g_ref`, grown by doubling: 2,048 × 16.4 KB ≈ 34 MB for the longest call, freed at the end of the suite); copy B's are compared as they come.
- **The exit exclusion is exactly $59–$5B**, and it is needed: with it removed (Task 5 state, the C against the ROM; measured while drafting), 155 of the checked calls differ at exit, each first at $0059 (e.g. `ROM 09, C 0A`: the copies return at different points of a 10-tick word).
- **The injections and the reach hooks.** Reply: `ser_status_due` = 1 at the second swap; afterwards `ser_tx_service` runs at every pass and leaves at $C585 (P0.2 low in the machine: nothing sent, `$68` stays 1). Mute: at the first `frm_finish_frame` entry after the second swap (spec §4.1). Set at the second swap itself, the mute is seen one frame later by the ROM whenever the swap's interrupt lands between its abort test ($B8A2) and its flag test ($B8A8), never by the C, whose swaps fall inside its ROM calls: while drafting, 4 of 46 `par_play` abort injections differed that way (`phonetic`, `hypermode`, `phonetic_stress`, `fast_effect`), and a probe showed the ROM copy interrupted at $B8A8 in exactly those 4. At `frm_finish_frame` both copies have just built the same frame and the next test either makes is the abort test. The hooks on copy A at $B8A5 and $B89C set a flag per call; the counts are calls, not executions. In each run, each routine's first call whose ROM copy swaps the buffers at least twice gets both injected pairs (in `queries`, `cmd_misc`, `cmd_args`, `phrase_127` and `syllabic_stress` the first speak-phase `boot_loop_play` returns at once and a later call gets them).
- **The self-check needs no replacement:** both copies run the ROM routine, with both injections, on hello (2 `boot_loop_play` calls, of which 1 plays, and 1 `par_play` call); it proves the snapshot, the exit comparison and the injection machinery. In this task no replacement has `waits`, so the corpus loop runs nothing and prints `0 routines wait on the timer interrupt`.
- **The comparison bites** (Step 4): injecting the reply in copy A only gives `swap 1: RAM $0068: ROM 01, C 00` for both self-check calls.

**Files:**
- Create: `tests/test_diff_play.c`
- Modify: `tests/suites.h` (`suite_diff_play` after `suite_diff_lts`: 33 suites)

**Interfaces:**
- Consumes: Task 1's `waits` field, the `par_addr.h` names `ROM_BOOT_LOOP_PLAY`, `ROM_PAR_PLAY`, `ROM_FRM_FINISH_FRAME`, and the `late_event` corpus name (through `CORPUS_NAMES`); `machine_clone`, `machine_finish_routine`, `machine_set_outputs`, `machine_hook`, `machine_clear_hooks` (`machine.h`); `rom_engine_run`, `frame_list` (`rom_engine.h`); `fw_replacements`, `fw_replacement_count`, `fw_bridge_init` (`thunks.h`); `SER_ABORT`, `SER_STATUS_DUE`, `ISR_TICK_DIV`, `ISR_WORDS_LEFT` (`fw_ram.h`); `test_rom`, `test_read_file` (`rom_fixture.h`).
- Produces: `suite_diff_play` (33 suites). Task 5 adds one line to it (`CHECK_EQ(g_nwatch, 2)`), and its two `waits` rows make the corpus loop run.

- [ ] **Step 1: Write the test**

Create `tests/test_diff_play.c`:

```c
/* Live differential test of the routines that wait on the timer interrupt (M4.4b spec 4.1): par_play $B877 and
   boot_loop_play $A7D4, the replacements marked `waits`, which diff_lts skips (it holds interrupts). Every corpus
   file and the boot run go through the ROM engine; at every call (no sampling) the machine is cloned twice with
   interrupts live. Copy A runs the ROM routine to its return, copy B the C thunk. Both record, at every buffer swap
   ($B306, the machine's frame callback), the produced frame and a snapshot: all RAM but the SFRs ($DA-$FF) and the
   stack page, the P2 latch and the UART transmit state. At a swap both main programs wait for isr_frame_flag, so
   the snapshots must agree. They must also agree at exit: PC, SP, A, X, Y, the P2 latch, the UART transmit state
   and all RAM but the SFRs, the dead stack below the exit SP and the timer interrupt's per-word counters
   isr_tick_div $59, isr_word_ofs $5A and isr_words_left $5B (they depend on how far into a frame each copy
   returns; every swap has compared them already).
   par_play re-enters $B877 by its own JMP par_play ($B892, $B8AE, $B914) with the same stack, so only a real call
   counts: the hook on the JSR par_play at $B2C7 (boot_play_body) arms the next $B877 entry. boot_loop_play is
   entered only by JSR. In a corpus file the boot's own call (from boot_selftest_sound $A7BA) and the par_play inside
   it are the boot run's; they are checked there only.
   The receive interrupt's two effects that no corpus text reaches are injected into both copies, on the first call
   of each routine in each run whose ROM copy swapped the buffers at least twice. A reply due: ser_status_due = 1 in
   the frame callback of the second swap (ser_tx_service from $B89C at every pass after it). A mute: ser_abort bit 0
   set at the first frm_finish_frame ($B9B5) after the second swap, so that par_play's next abort test ($B8A2) ends
   it (the end path from $B8A5). Not at the swap itself: there the ROM sees the mute before or after the frame flag
   depending on the instruction the interrupt lands on (at $B8A8, between the two tests, it builds one more frame),
   which the C, taking no time between its tests, cannot reproduce. A hook on copy A counts the calls that executed
   $B8A5 and $B89C. No copy runs past the routine's return: after a ser_status_due injection the ROM never reaches
   idle again (boot_get_clause waits for the host at $A6D4).
   A self-check runs the ROM routine in copy B too, on hello, both injections included: it proves the comparison
   and the injection machinery whatever the replacement table holds. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "engine/rom_engine.h"
#include "engine/thunks.h"
#include "firmware/par_addr.h"

#define PLAY_BUDGET        1000000000ULL   /* copy A: the longest call (seg_wrap_word, 1,774 frames) is 35.5 M cycles */
#define PLAY_CALL_SITE     0xB2C7          /* boot_play_body: JSR par_play */
#define PLAY_REPLY         0xB89C          /* par_play: JSR ser_tx_service (a reply due) */
#define PLAY_ABORT         0xB8A5          /* par_play: JMP $B8D2 (ser_abort bit 0: the end) */
#define BOOT_SOUND_RETURN  0xA7BD          /* after boot_selftest_sound's JSR boot_loop_play at $A7BA */
#define INJECT_SWAP        1               /* the second swap of a call */
#define WATCH_MAX          2

static const char *const k_corpus[] = { CORPUS_NAMES };

typedef enum { INJECT_NONE, INJECT_ABORT, INJECT_REPLY } inject_kind;
static const char *const k_inject[] = { "", " (ser_abort injected)", " (ser_status_due injected)" };

/* The state of one copy at one buffer swap. */
typedef struct {
    uint8_t frame[APOLLO_FRAME_BYTES];
    uint8_t ram[MACHINE_RAM_SIZE];
    uint8_t p2;
    uint32_t tx_count;
    uint8_t tx_last;
} swap_state;

/* The frame callback's context: copy A records every swap, copy B compares each with copy A's. */
typedef struct {
    machine *m;
    inject_kind inject;
    bool ref;
    size_t swaps;
    bool abort_due;    /* the abort injection waits for the next frm_finish_frame */
    bool differs;      /* copy B: the first difference is in why */
    char why[160];
} recorder;

/* A routine watched at its ROM entry; copy B runs it through run_b. */
typedef struct watch {
    const char *name;
    uint16_t addr;
    bool (*run_b)(machine *m, const struct watch *w);
    const fw_replacement *r;   /* the replacement whose thunk copy B runs (NULL: the ROM routine) */
    unsigned calls, injected;
    bool done_injecting;       /* per run */
} watch;

static machine g_m, g_a, g_b;
static swap_state *g_ref;      /* copy A's swaps: g_ref_n of them, room for g_ref_cap */
static size_t g_ref_n, g_ref_cap;
static swap_state g_cur;
static watch g_watch[WATCH_MAX];
static size_t g_nwatch;
static const char *g_source;
static bool g_boot_run, g_blp_checked, g_play_armed;
static unsigned g_mismatches, g_reached_abort, g_reached_reply;
static bool g_hit_abort, g_hit_reply;

static void take(swap_state *s, const machine *m, const uint8_t frame[APOLLO_FRAME_BYTES])
{
    memcpy(s->frame, frame, APOLLO_FRAME_BYTES);
    memcpy(s->ram, m->ram, MACHINE_RAM_SIZE);
    s->p2 = m->sfr.port_latch[2];
    s->tx_count = m->sfr.uart_tx_count;
    s->tx_last = m->sfr.uart_tx_last;
}

/* The first difference between two swap states (the SFRs and the stack page excluded), or false. */
static bool swap_differs(const swap_state *a, const swap_state *b, char *why, size_t n)
{
    for (int i = 0; i < APOLLO_FRAME_BYTES; i++)
        if (a->frame[i] != b->frame[i]) {
            snprintf(why, n, "frame byte %d: ROM %02X, C %02X", i, a->frame[i], b->frame[i]);
            return true;
        }
    for (uint32_t addr = 0; addr < MACHINE_RAM_SIZE; addr++) {
        if ((addr >= M50734_SFR_FIRST && addr <= 0xFF) || (addr >= 0x100 && addr <= 0x1FF))
            continue;
        if (a->ram[addr] != b->ram[addr]) {
            snprintf(why, n, "RAM $%04X: ROM %02X, C %02X", (unsigned)addr, a->ram[addr], b->ram[addr]);
            return true;
        }
    }
    if (a->p2 != b->p2) {
        snprintf(why, n, "P2 latch: ROM %02X, C %02X", a->p2, b->p2);
        return true;
    }
    if (a->tx_count != b->tx_count || a->tx_last != b->tx_last) {
        snprintf(why, n, "UART TX: ROM %u/%02X, C %u/%02X", (unsigned)a->tx_count, a->tx_last, (unsigned)b->tx_count,
                 b->tx_last);
        return true;
    }
    return false;
}

/* At $B306 (isr_swap_buffers): at the second swap the reply injection is made and the abort injection made due,
   then the swap's state is recorded (copy A) or compared with copy A's (copy B). */
static void on_swap(void *user, const uint8_t frame[APOLLO_FRAME_BYTES])
{
    recorder *rec = user;
    machine *m = rec->m;
    size_t i = rec->swaps++;
    if (i == INJECT_SWAP && rec->inject == INJECT_ABORT)
        rec->abort_due = true;
    if (i == INJECT_SWAP && rec->inject == INJECT_REPLY)
        m->ram[SER_STATUS_DUE] = 1;
    if (rec->ref) {
        if (i == g_ref_cap) {
            g_ref_cap = g_ref_cap ? g_ref_cap * 2 : 256;
            g_ref = realloc(g_ref, g_ref_cap * sizeof *g_ref);
            if (!g_ref)
                abort();
        }
        take(&g_ref[i], m, frame);
        return;
    }
    if (rec->differs)
        return;
    take(&g_cur, m, frame);
    char why[140];
    if (i >= g_ref_n) {
        rec->differs = true;
        snprintf(rec->why, sizeof rec->why, "swap %zu: the C swapped more often than the ROM", i);
    } else if (swap_differs(&g_ref[i], &g_cur, why, sizeof why)) {
        rec->differs = true;
        snprintf(rec->why, sizeof rec->why, "swap %zu: %s", i, why);
    }
}

/* $B9B5, frm_finish_frame (ROM in both copies): the abort injection, at the first frame finished after the second
   swap. */
static bool inject_abort(machine *m, void *user)
{
    recorder *rec = user;
    if (rec->abort_due) {
        m->ram[SER_ABORT] |= 1;
        rec->abort_due = false;
    }
    return false;
}

/* The copy-A hooks: which of the two injected branches the ROM routine executed. */
static bool hit_abort(machine *m, void *user)
{
    (void)m;
    (void)user;
    g_hit_abort = true;
    return false;
}

static bool hit_reply(machine *m, void *user)
{
    (void)m;
    (void)user;
    g_hit_reply = true;
    return false;
}

/* Copy B, the ROM routine (the self-check). */
static bool run_rom(machine *m, const watch *w)
{
    (void)w;
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

/* The exit states: PC, SP, A, X, Y, the P2 latch, the UART transmit state and RAM (spec 4.1). */
static bool exit_differs(const machine *a, const machine *b, char *why, size_t n)
{
    if (a->cpu.pc != b->cpu.pc || a->cpu.s != b->cpu.s) {
        snprintf(why, n, "exit PC/S: ROM %04X/%02X, C %04X/%02X", a->cpu.pc, a->cpu.s, b->cpu.pc, b->cpu.s);
        return true;
    }
    if (a->cpu.a != b->cpu.a || a->cpu.x != b->cpu.x || a->cpu.y != b->cpu.y) {
        snprintf(why, n, "exit A/X/Y: ROM %02X/%02X/%02X, C %02X/%02X/%02X", a->cpu.a, a->cpu.x, a->cpu.y, b->cpu.a,
                 b->cpu.x, b->cpu.y);
        return true;
    }
    if (a->sfr.port_latch[2] != b->sfr.port_latch[2]) {
        snprintf(why, n, "exit P2 latch: ROM %02X, C %02X", a->sfr.port_latch[2], b->sfr.port_latch[2]);
        return true;
    }
    if (a->sfr.uart_tx_count != b->sfr.uart_tx_count || a->sfr.uart_tx_last != b->sfr.uart_tx_last) {
        snprintf(why, n, "exit UART TX: ROM %u/%02X, C %u/%02X", (unsigned)a->sfr.uart_tx_count,
                 a->sfr.uart_tx_last, (unsigned)b->sfr.uart_tx_count, b->sfr.uart_tx_last);
        return true;
    }
    for (uint32_t addr = 0; addr < MACHINE_RAM_SIZE; addr++) {
        if (addr >= M50734_SFR_FIRST && addr <= 0xFF)
            continue;
        if (addr >= 0x100 && addr <= 0x100u + a->cpu.s)
            continue;   /* the dead stack below the exit SP */
        if (addr >= ISR_TICK_DIV && addr <= ISR_WORDS_LEFT)
            continue;   /* the per-word counters: how far into a frame each copy returned */
        if (a->ram[addr] != b->ram[addr]) {
            snprintf(why, n, "exit RAM $%04X: ROM %02X, C %02X", (unsigned)addr, a->ram[addr], b->ram[addr]);
            return true;
        }
    }
    return false;
}

/* One call of w from m's state, with one injection kind. Returns the swaps copy A made. */
static size_t check_call(machine *m, watch *w, unsigned call, inject_kind inject)
{
    char why[200] = "";
    recorder ra = { &g_a, inject, true, 0, false, false, "" }, rb = { &g_b, inject, false, 0, false, false, "" };
    machine_clone(&g_a, m);
    g_hit_abort = g_hit_reply = false;
    CHECK(machine_hook(&g_a, PLAY_ABORT, hit_abort, NULL));
    CHECK(machine_hook(&g_a, PLAY_REPLY, hit_reply, NULL));
    CHECK(machine_hook(&g_a, ROM_FRM_FINISH_FRAME, inject_abort, &ra));
    machine_set_outputs(&g_a, on_swap, &ra);
    bool a_ok = machine_finish_routine(&g_a, PLAY_BUDGET);
    g_ref_n = ra.swaps;
    g_reached_abort += g_hit_abort;
    g_reached_reply += g_hit_reply;
    machine_clone(&g_b, m);
    CHECK(machine_hook(&g_b, ROM_FRM_FINISH_FRAME, inject_abort, &rb));
    machine_set_outputs(&g_b, on_swap, &rb);
    bool b_ok = w->run_b(&g_b, w);
    if (!a_ok)
        snprintf(why, sizeof why, "the ROM routine did not return (fault %d)", (int)g_a.fault);
    else if (!b_ok)
        snprintf(why, sizeof why, "copy B did not return (fault %d at $%04X)", (int)g_b.fault, g_b.fault_addr);
    else if (rb.differs)
        snprintf(why, sizeof why, "%s", rb.why);
    else if (ra.swaps != rb.swaps)
        snprintf(why, sizeof why, "swaps: ROM %zu, C %zu", ra.swaps, rb.swaps);
    else
        exit_differs(&g_a, &g_b, why, sizeof why);
    if (why[0] && g_mismatches++ < 10)
        fprintf(stderr, "  %s: %s call %u%s: %s\n", g_source, w->name, call, k_inject[inject], why);
    return ra.swaps;
}

/* Every call of w is checked; the first that swaps at least twice gets the two injected pairs too. */
static void check_all(machine *m, watch *w)
{
    unsigned call = w->calls++;
    if (check_call(m, w, call, INJECT_NONE) <= INJECT_SWAP || w->done_injecting)
        return;
    check_call(m, w, call, INJECT_ABORT);
    check_call(m, w, call, INJECT_REPLY);
    w->injected++;
    w->done_injecting = true;
}

static watch *watched(uint16_t addr)
{
    for (size_t i = 0; i < g_nwatch; i++)
        if (g_watch[i].addr == addr)
            return &g_watch[i];
    return NULL;
}

/* The address the routine just entered by JSR returns to (the pushed address + 1). */
static uint16_t return_address(const machine *m)
{
    uint8_t lo = m->ram[0x100 + (uint8_t)(m->cpu.s + 1)], hi = m->ram[0x100 + (uint8_t)(m->cpu.s + 2)];
    return (uint16_t)((lo | hi << 8) + 1);
}

/* $A7D4: a boot_loop_play call. The boot's own call is checked in the boot run only, with the par_play inside it. */
static bool on_loop_play(machine *m, void *user)
{
    (void)user;
    g_blp_checked = g_boot_run || return_address(m) != BOOT_SOUND_RETURN;
    watch *w = watched(ROM_BOOT_LOOP_PLAY);
    if (w && g_blp_checked)
        check_all(m, w);
    return false;   /* the ROM routine runs on in m */
}

/* $B2C7: boot_play_body's JSR par_play arms the next $B877 entry. */
static bool on_play_call(machine *m, void *user)
{
    (void)m;
    (void)user;
    g_play_armed = g_blp_checked;
    return false;
}

/* $B877: par_play, a real call only when armed (its own JMP par_play re-enters here with the same stack). */
static bool on_play(machine *m, void *user)
{
    (void)user;
    if (!g_play_armed)
        return false;
    g_play_armed = false;
    watch *w = watched(ROM_PAR_PLAY);
    if (w)
        check_all(m, w);
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
{
    frame_list fl = { 0 };
    g_source = source;
    g_boot_run = boot;
    g_blp_checked = g_play_armed = false;
    for (size_t i = 0; i < g_nwatch; i++)
        g_watch[i].done_injecting = false;
    machine_status st = rom_engine_run(&g_m, rom, len, NULL, install, NULL, text, boot, &fl, NULL);
    if (st != MACHINE_IDLE)
        machine_dump(&g_m, stderr);
    CHECK_EQ(st, MACHINE_IDLE);
    machine_clear_hooks(&g_m);
    frame_list_free(&fl);
}

static void strip_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

static void run_corpus(const uint8_t *rom, size_t len)
{
    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char path[128];
        snprintf(path, sizeof path, "tests/corpus/%s.txt", k_corpus[c]);
        char *text = test_read_file(path);
        CHECK(text != NULL);
        if (!text)
            continue;
        strip_newline(text);
        run(rom, len, k_corpus[c], text, false);
        free(text);
    }
    run(rom, len, "boot", "", true);
}

static void add_watch(const char *name, uint16_t addr, const fw_replacement *r)
{
    watch *w = &g_watch[g_nwatch++];
    memset(w, 0, sizeof *w);
    w->name = name;
    w->addr = addr;
    w->r = r;
    w->run_b = r ? run_thunk_b : run_rom;
}

/* ROM against ROM on hello: both routines, both injections; every pair must agree and both branches be reached. */
static void self_check(const uint8_t *rom, size_t len)
{
    char *text = test_read_file("tests/corpus/hello.txt");
    CHECK(text != NULL);
    if (!text)
        return;
    strip_newline(text);
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
    CHECK(g_reached_reply > 0);
}

void suite_diff_play(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    g_mismatches = 0;
    self_check(rom, len);
    g_nwatch = 0;
    for (size_t i = 0; i < fw_replacement_count; i++) {
        const fw_replacement *r = &fw_replacements[i];
        if (!r->waits)
            continue;
        CHECK(g_nwatch < WATCH_MAX && (r->addr == ROM_PAR_PLAY || r->addr == ROM_BOOT_LOOP_PLAY));
        if (g_nwatch < WATCH_MAX)
            add_watch(r->name, r->addr, r);
    }
    printf("  %zu routines wait on the timer interrupt\n", g_nwatch);
    if (g_nwatch > 0) {
        g_reached_abort = g_reached_reply = 0;
        run_corpus(rom, len);
        for (size_t i = 0; i < g_nwatch; i++) {
            printf("  %-20s %u calls checked, %u with both injections\n", g_watch[i].name, g_watch[i].calls,
                   g_watch[i].injected);
            CHECK(g_watch[i].calls > 0);
        }
        printf("  $B8A5 (abort) reached in %u calls, $B89C (reply due) in %u\n", g_reached_abort, g_reached_reply);
        CHECK(g_reached_abort > 0);
        CHECK(g_reached_reply > 0);
    }
    CHECK_EQ(g_mismatches, 0);
    free(g_ref);
    g_ref = NULL;
    g_ref_n = g_ref_cap = 0;
}
```

- [ ] **Step 2: Register the suite**

In `tests/suites.h`, replace

```c
SUITE(suite_diff_lts)
```

with

```c
SUITE(suite_diff_lts)
SUITE(suite_diff_play)
```

- [ ] **Step 3: Run it**

Run: `cmd //c ".\build.bat test diff_play"`
Expected:

```
  self-check (ROM against ROM, hello): par_play 1 calls, boot_loop_play 2 calls, 2 injected; $B8A5 reached 2, $B89C reached 2
  0 routines wait on the timer interrupt
suite_diff_play              ok
1 suite(s), 0 failure(s), 0 skip(s)
```

(No routine has `waits` before Task 5, so only the self-check runs: 14 s with the build in the proof run.)

- [ ] **Step 4: Check that the comparison bites (temporary, reverted)**

In `tests/test_diff_play.c` replace `    if (i == INJECT_SWAP && rec->inject == INJECT_REPLY)` with `    if (rec->ref && i == INJECT_SWAP && rec->inject == INJECT_REPLY)` (the reply injected in copy A only) and run `cmd //c ".\build.bat test diff_play"`.
Expected: `suite_diff_play              FAILED`, with

```
  self-check hello: boot_loop_play call 0 (ser_status_due injected): swap 1: RAM $0068: ROM 01, C 00
  self-check hello: par_play call 0 (ser_status_due injected): swap 1: RAM $0068: ROM 01, C 00
```

and `CHECK_EQ failed: g_mismatches == 0 (got 0x2, want 0x0)`. Then put the line back as Step 1 has it and run again: `suite_diff_play              ok`.

- [ ] **Step 5: Run the full suite**

Run: `cmd //c ".\build.bat test"`
Expected: `33 suite(s), 0 failure(s), 0 skip(s)`; every other suite's output as Task 3 left it (`suite_diff_lts` the same 62 lines with 0 mismatches, `1 mid-clause flush calls skipped (golden_hybrid covers them)`, `2 unwound calls compared`; `46 corpus files x 63 modes (all, then each routine alone)`; `47702 converted bytes compared with the ROM, 0 differ`); `suite_diff_play` prints the lines of Step 3 (its two lines and `suite_diff_play              ok`). The proof run took 378 s; `diff_play` alone takes about 14 s here.

- [ ] **Step 6: Commit**

```bash
git add tests/test_diff_play.c tests/suites.h
git commit -m "diff_play: the live differential test of the routines that wait on the timer interrupt, with interrupts running, the injected mute and reply, and its ROM-against-ROM self-check (M4.4b 4.1)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 5: `par_play` $B877 and `boot_loop_play` $A7D4 in C, with their hooks (`waits`); the C `tn_convert_clause` calls the C `boot_loop_play`; `test_timing_invariance` on the hybrid engine (spec §2.1–§2.6, §4.1, §4.4, §4.5)

**ROM facts checked** (`rom_B877_par_play.txt` and `rom_A7D4_boot_loop_play.txt` instruction by instruction against the C below; the callees `rom_C5D6_isr_par_ready.txt`, `rom_C5DE_isr_par_hold.txt`, `rom_C581_ser_tx_service.txt`, `rom_B465_isr_wait_frame.txt`, `rom_B46C_isr_volume_half.txt`, `rom_ACE8_isr_set_volume.txt`, `rom_B323_isr_speech_start.txt` with `rom_B356_isr_wait_frames.txt`, `rom_B38B_isr_dsp_reset.txt` and `rom_B35D_isr_timer_start.txt`, `rom_B398_isr_speech_stop.txt`, `rom_B9B5_frm_finish_frame.txt`, `rom_B445_frm_copy_rest_frame.txt`, `rom_B917_frm_snapshot.txt`, `rom_B930_frm_clamp_mark_space.txt`, `rom_BA3F_frm_transition_flags.txt`; the callers `rom_A61C_boot_reset.txt`, `rom_A79F_boot_selftest_sound.txt`, `rom_A876_cmd_say_phonetic.txt`, `rom_B1E6_tn_convert_clause.txt`, `rom_A68D_boot_loop_text.txt`, `rom_A697_boot_get_clause.txt`, `rom_AC7E_boot_init_baud.txt`, `rom_AC68_boot_init_host_lines.txt`):
- **No name or signature had to change.** `par_play` and `boot_loop_play` are `void name(fw *f, fw_regs *r)` in the new `src/firmware/par_play.h`/`.c`. `PAR_REST_FRAMES` (3, $B8D5) is in the header; `SFR_UART_STATUS` ($E7) and `UART_TX_READY` (bit 1) stay in the `.c`.
- **`par_play`'s blocks** (one static helper each): `run_due_event` $B877–$B892 (`CPX`/`BCS` unsigned, then the two `CMP`/`BNE`; X = `cmd_ev_next` and A = the last byte loaded flow into `cmd_run_event`, then `JMP par_play` tests again); `serve_reply` $B895–$B89C (`BBC1 UART_STATUS` through `fw_sfr8`, `TST ser_status_due` on the whole byte); the pass $B89F–$B8AE in the loop body; `segment_frame` $B8B1–$B907 (`LDY` low then high byte of `pros_seg_count`, `DEC`/`BNE` at $B8FB, `par_next_segment`, `LDA`/`BEQ $B8B1` back to the `LDY`); `segment_step` $B908–$B911; `last_frames` $B8BB–$B8CF; `rest_frames` $B8D2–$B8FA; `wait_frame_flag` the two `isr_par_ready`/`BBC0 isr_frame_flag` loops ($B8C9, $B8E6); `count_frame` the three `DEC par_frames_left` ($B8BB, $B8EF, $B8FB), mod 256. The order of the ROM calls is the ROM's in every path; the flag tests read bit 0, the three `LDM #0` write the whole byte.
- **The zero-frame wraps** (spec §2.6, unreachable): a `par_next_segment` that leaves 0 goes back to $B8B1 ($B906 `BEQ`), so the next `DEC` makes $FF — at $B8FB when segments are left (then a frame is built, $B908), at $B8BB when none are (then 255 frames of the tight loop). `test_play_zero_frames_segment`, `_main` and `_tail` pin all three.
- **Registers at each call, and what each ROM callee returns** (the mocks answer with these): `isr_par_ready` (PHA … PLA, serial mode) and `isr_par_hold` keep A, X, Y; `ser_tx_service` keeps them when the host line P0.2 is low (the machine's case: `BBC2 P0` at $C585 to `PLA`/`RTS`); `isr_wait_frame` keeps them and clears the flag; `frm_finish_frame` pushes and restores them ($B9B5–$B9B9, $BA39–$BA3E); `frm_copy_rest_frame` A = `frm_rest_frame[0]`, Y = $FF, X kept; `frm_snapshot` A = the fill frame's b10, X = 0, Y = 10 (the last `LDY $B772,X` at X = 1 reads `frm_snapshot_map[0]` = 10); `frm_clamp_mark_space` A = Y = $0B; `frm_transition_flags` A = `frm_flags`, Y = $0D ($BAE4–$BAE7), X not used; `isr_speech_start` A = 0 ($B344, kept by `isr_dsp_reset`, `isr_timer_start` and `isr_wait_frames`), X = 0 (`LDX #1`, `DEX`), Y kept; `isr_volume_half` A = `isr_set_volume`'s with A = `cmd_volume` >> 1; `isr_set_volume` A = the new P3, X and Y kept; `isr_speech_stop` keeps all three. In the C, every ROM call gets `r` and returns into it, so the registers flow as in the ROM; the unit tests pin them call by call.
- **None of these ROM routines reads a flag on entry** (`fw_rom_call` starts them with the machine's P): `isr_par_hold`, `isr_dsp_reset` and `isr_timer_start` save P with `PHP` and restore it with `PLP`, so the I flag is as it came; the others start with `PHA`, `LDA`, `LDX`, `LDY`, `LDM` or a `BBC`/`BBS`.
- **Exit registers.** `par_play`: as `cmd_ev_sound_end` leaves them, kept by the last `isr_par_ready` ($B8F7). `boot_loop_play`: A = $6B with X and Y untouched when it does not play ($A7D6 `BEQ`, $A7D8 `BBS0 ser_abort`); else `isr_speech_stop`'s.
- **What the callers read after a call** (the thunk comments): `boot_loop_play` — boot_reset $A687 `JMP $A681` → `boot_loop_text` → `boot_get_clause` (`LDX #1`, `LDA #$20` first); boot_selftest_sound $A7BA `RTS` → $A657 (`BBS0 boot_dip`, `LDX #0` or `JMP $A66B`: `LDM`, `CLI`, `isr_par_hold`/`isr_par_ready` keep the registers, `cmd_apply_speaker`'s `par_setup_voice` reads none); cmd_say_phonetic $A87C `RTS` (unreachable); tn_convert_clause $B207 `JMP $B1E9` (`LDX #0`). `par_play` — boot_play_body $B2CA `isr_volume_half` (`LDA cmd_volume` first, no X or Y). No caller reads a flag.
- **A failed ROM call ends `par_play`'s waits.** Its two wait loops and its pass test `f->fault`. Under a thunk a fault longjmps out before the test, so the test only ends what would otherwise be an endless loop in a unit test whose mock fails a call (the ROM routine would never return either). Nothing else in the C departs from the dump.
- **The unit-test harness grows here:** `MAX_CALLS` 64 → 1024 and a `frames` field (`par_frames_left` at each call) in `rom_call_record`, because the frame-loop tests are the first to need them (the longest, `test_play_zero_frames_tail`, makes 528 calls).
- **Timing** (spec §4.4): `cycle_num/cycle_den` = 1/4 makes the timers 4× slower (an 80,000-cycle frame), 3/1 3× faster (6,667 cycles). A probe hooked at $B300 (the swap test) on hello while drafting: rom engine, no late frame at 1/1 and 1/4, 30 of 146 frame ends late at 3/1; hybrid engine (every replacement), none late at any of the three, because its frame build is C except `frm_finish_frame`. So the hybrid run checks the C loop under a 3× faster and a 4× slower timer; the comment in `test_golden.c` says so.
- **The hooks.** `par_play`'s fires on its `JSR` only: once the thunk has run, the ROM loop and its three `JMP par_play` never run. In "all" mode `boot_loop_play` fires from the ROM main loop and the boot ($A687, $A7BA); the C `boot_loop_play` calls the C `par_play`, so `par_play`'s hook fires only in its "alone" mode, from the ROM `boot_play_body`. The C `tn_convert_clause` calls the C `boot_loop_play`. `golden_hybrid`: all 46 files × 65 modes identical.
- **`tn_convert_clause` no longer uses `FW_ROM_CALL_SPEECH_BUDGET` or `ROM_BOOT_LOOP_PLAY`:** after this task `grep -rn "ROM_BOOT_LOOP_PLAY" src` finds only `par_addr.h`, and `FW_ROM_CALL_SPEECH_BUDGET` is used only by `tests/test_fw.c` (spec §2.3 keeps it).
- **`tests/test_fw_tn.c`'s flush now runs the C `boot_loop_play`** (`FW_UTT_CONTENT` is $FF after the C `pros_make_segments`). Its mock answers the eleven ROM routines the C player calls, sets `isr_frame_flag` at every `isr_par_ready` and `ser_abort` at `isr_speech_start`, so `par_play` goes from its first pass to the rest frames. The first call of the flush is now `isr_par_hold` with A = $FF (`LDA $6B`), X = $FA and Y = $70 (`pros_phrase_gen`'s, unchanged); `isr_par_ready` is called 1,379 times (5 more: the pass, the three waits, the last); `cmd_ev_pos` = $FFFF shows `par_play_start` ran.
- **`diff_play` with the C** (Task 4's suite, now with the two `waits` rows): every call of both routines, both injections, 0 mismatches; $B8A5 and $B89C reached in 92 calls each (the ROM copies). The C copies wait in the ROM as long as the ROM ones, so the suite takes about 40 s.
- **Proof run** (the consolidated plan applied literally in a scratch worktree off main c5ca8fa, after Tasks 1–4): RED as Step 2 says; `fw_par` and `fw_tn` ok; the scans and the grep of Step 4 as printed there; the full run and the 45 CLI comparisons as Step 5 says, every `diff_lts` line identical to Task 4's.
- **Mutations** (while drafting; scratch, each reverted): `segment_frame` without the `BEQ $B906` loop-back fails `test_play_zero_frames_segment` (a fault on a 0-step interpolation, 6 failures); `serve_reply` without the UART test fails `test_play_abort` (23 failures); `boot_loop_play` without the `ser_abort` test fails `test_loop_play_idle` (22 failures; `test_loop_play_idle` sets the flag at every `isr_par_ready`, so a call played by mistake ends instead of waiting forever).

**Files:**
- Create: `src/firmware/par_play.h`, `src/firmware/par_play.c`
- Modify: `src/firmware/tn_convert.c` (the C call), `src/engine/thunks.c` (an include, two thunks, `THUNK` lines and rows with `waits`), `tests/test_fw_par.c` (the include, `MAX_CALLS`, the `frames` record, the mock, the tests), `tests/test_fw_tn.c` (the flush case), `tests/test_golden.c` (`test_timing_invariance` on the hybrid engine), `tests/test_diff_play.c` (exactly two `waits` routines)

**Interfaces:**
- Consumes: see the list at the top (Task 1: `fw_sfr8`, `waits`, the `par_addr.h` names; Task 2: `par_step_tracks`, `fill_frame_b`; Task 3: `cmd_run_event`, `cmd_ev_sound_end`, the `test_fw_par.c` state; M4.4a: `par_play_start`, `par_next_segment`, `fw_par_ready`, the harness); `hybrid_engine_speak` (`hybrid_engine.h`).
- Produces:

```c
/* src/firmware/par_play.h */
#define PAR_REST_FRAMES  3
void par_play(fw *f, fw_regs *r);         /* $B877 */
void boot_loop_play(fw *f, fw_regs *r);   /* $A7D4, body $B2B3-$B2D8 */
```

  - The replacements `boot_loop_play` 0xA7D4 (after `cmd_apply_speaker`) and `par_play` 0xB877 (after `par_setup_voice`), `FW_OUT_A | FW_OUT_X | FW_OUT_Y`, `waits` true: **64 replacements, 65 `golden_hybrid` modes; `diff_lts` still 62 lines; `diff_play` 2 routines.**
  - The C `tn_convert_clause` calls the C `boot_loop_play` (spec §4.7 criterion 7).

- [ ] **Step 1: Write the failing tests**

In `tests/test_fw_par.c` (as Task 3 leaves it):

Replace

```c
#include "firmware/par_boundary.h"
```

with

```c
#include "firmware/par_boundary.h"
#include "firmware/par_play.h"
```

Replace

```c
#define MAX_CALLS 64
```

with

```c
#define MAX_CALLS 1024
```

Replace

```c
    uint16_t play, fill;
    uint8_t phase;
} rom_call_record;
```

with

```c
    uint16_t play, fill;
    uint8_t phase;
    uint8_t frames;   /* par_frames_left at the call */
} rom_call_record;
```

Replace

```c
static uint8_t g_p3;             /* the port P3 the mock isr_set_volume reads and writes (an SFR: not in g_ram) */
```

with

```c
static uint8_t g_p3;             /* the port P3 the mock isr_set_volume reads and writes (an SFR: not in g_ram) */
static unsigned g_ready_calls;   /* isr_par_ready calls so far */
static unsigned g_flag_every;    /* the mock timer interrupt sets isr_frame_flag at every n-th of them (0: never) */
static unsigned g_abort_at;      /* the mock receive interrupt sets ser_abort bit 0 at this one (0: never) */
static uint8_t g_uart_status;    /* UART_STATUS as the mock reads it */
```

Replace

```c
/* Records every ROM call. The three frame routines par_setup_voice calls (M4.5) and isr_set_volume (M4.6, called by
   cmd_ev_set_volume) return the registers the ROM ones leave; any other call fails. */
```

with

```c
/* Records every ROM call. The three frame routines par_setup_voice calls (M4.5) and isr_set_volume (M4.6, called by
   cmd_ev_set_volume) return the registers the ROM ones leave; so do the interrupt, serial and frame routines that
   par_play and boot_loop_play call (M4.5, M4.6), with what the timer and receive interrupts would do between the
   frame loop's tests (g_flag_every, g_abort_at); any other call fails. */
```

Replace

```c
        c->phase = g_ram[ISR_SWAP_PHASE];
    }
```

with

```c
        c->phase = g_ram[ISR_SWAP_PHASE];
        c->frames = g_ram[PAR_FRAMES_LEFT];
    }
```

Replace

```c
    case ROM_ISR_SET_VOLUME:         /* $ACE8-$ACF9: EOR #$F / AND #$F / JSR util_shl3 / STA $75 / LDA #$87 / AND P3 /
```

with

```c
    case ROM_ISR_VOLUME_HALF:        /* $B46C-$B472: LDA cmd_volume / LSR / JSR isr_set_volume: on into its case */
        r->a = (uint8_t)(g_ram[CMD_VOLUME] >> 1);
        /* fall through */
    case ROM_ISR_SET_VOLUME:         /* $ACE8-$ACF9: EOR #$F / AND #$F / JSR util_shl3 / STA $75 / LDA #$87 / AND P3 /
```

Replace

```c
        r->a = g_p3;
        return FW_CALL_RETURNED;
    default:
        return FW_CALL_FAILED;
    }
}
```

with

```c
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
    case ROM_SER_TX_SERVICE:         /* $C581-$C585, $C5A2-$C5A3: PHA, BBS0 boot_dip, BBC2 P0 (the host line low, as
                                        in the machine), PLA: nothing sent, A, X, Y kept */
    case ROM_FRM_FINISH_FRAME:       /* $B9B5-$B9B9 PHA / TXA / PHA / TYA / PHA ... $BA39-$BA3E PLA / TAY / PLA / TAX /
                                        PLA: A, X, Y restored */
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
    case ROM_FRM_SNAPSHOT:           /* $B917-$B92F: LDY #$0A / DEY loop, then LDX #$0A: LDY $B772,X / LDA ($2C),Y /
                                        DEX: the last at X = 1, Y = frm_snapshot_map[0] = 10: A = the fill frame's b10,
                                        X = 0, Y = 10 */
        r->y = FRAME_PITCH;
        r->a = fw_ind_y(&g_f, ISR_FILL_PTR, r->y);
        r->x = 0;
        return FW_CALL_RETURNED;
    case ROM_FRM_TRANSITION_FLAGS:   /* $BAE4-$BAEB: LDA frm_flags / LDY #$0D / STA ($2C),Y / RTS; X not used. The
                                        flags themselves are not modelled: A = frm_flags as it is */
        r->a = g_ram[FRM_FLAGS];
        r->y = 0x0D;
        return FW_CALL_RETURNED;
    default:
        return FW_CALL_FAILED;
    }
}

/* UART_STATUS ($E7) as g_uart_status says; any other SFR reads 0. */
static uint8_t mock_read_sfr(void *ctx, uint8_t addr)
{
    (void)ctx;
    return addr == 0xE7 ? g_uart_status : 0;
}
```

Replace

```c
    g_host.set_low_window = NULL;   /* nothing in the playback set-up drives P2 */
    g_ncalls = 0;
```

with

```c
    g_host.set_low_window = NULL;   /* nothing in the playback set-up drives P2 */
    g_host.read_sfr = mock_read_sfr;
    g_ncalls = 0;
    g_ready_calls = 0;
    g_flag_every = 0;
    g_abort_at = 0;
    g_uart_status = 0x02;           /* the transmitter ready, nothing received (as the machine has it) */
```

Replace

```c
void suite_fw_par(void)
{
```

with (the tests go after Task 3's)

```c
/* ---- the frame loop (M4.4b Task 5) ---- */

static void test_play_rows(void)
{
    check_row("boot_loop_play", ROM_BOOT_LOOP_PLAY, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("par_play", ROM_PAR_PLAY, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    size_t waits = 0;
    for (size_t i = 0; i < fw_replacement_count; i++)
        if (fw_replacements[i].waits) {
            waits++;
            CHECK(fw_replacements[i].addr == ROM_BOOT_LOOP_PLAY || fw_replacements[i].addr == ROM_PAR_PLAY);
        }
    CHECK_EQ(waits, 2);   /* only the two loops wait on the timer interrupt: diff_play checks them, diff_lts not */
}

/* Record i is a call of addr with A, X and Y (the strict budget), made with par_frames_left = frames. */
static void check_play_call(unsigned i, uint16_t addr, uint8_t a, uint8_t x, uint8_t y, uint8_t frames)
{
    check_call(i, addr, a, x, y);
    if (i < g_ncalls && i < MAX_CALLS)
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
   then per rest frame frm_copy_rest_frame, frm_snapshot, frm_clamp_mark_space, frm_transition_flags and the wait,
   with par_frames_left 3, 2, 1 ($B8D5, then the DEC at $B8EF); then the last isr_par_ready ($B8F7;
   cmd_ev_sound_end is C and returns at once, cmd_sound_active being 0). Returns the number of calls. */
static unsigned check_rest_frames(unsigned i, unsigned waits)
{
    static const uint16_t k_frame[] = {
        ROM_FRM_COPY_REST_FRAME, ROM_FRM_SNAPSHOT, ROM_FRM_CLAMP_MARK_SPACE, ROM_FRM_TRANSITION_FLAGS,
    };
    uint16_t k[1 + 3 * (4 + 2) + 1];
    unsigned n = 0, per = 4 + waits;
    k[n++] = ROM_ISR_WAIT_FRAME;
    for (unsigned j = 0; j < 3; j++) {
        for (unsigned m = 0; m < 4; m++)
            k[n++] = k_frame[m];
        for (unsigned m = 0; m < waits; m++)
            k[n++] = ROM_ISR_PAR_READY;
    }
    k[n++] = ROM_ISR_PAR_READY;
    check_order(i, k, n);
    for (unsigned j = 0; j < 3; j++)
        for (unsigned m = 0; m < per; m++)
            if (i + 1 + per * j + m < MAX_CALLS)
                CHECK_EQ(g_calls[i + 1 + per * j + m].frames, 3 - j);
    return n;
}

/* No segment left (pros_seg_count = 0): two passes (the flag comes at the second isr_par_ready, every 2nd call),
   then the tight loop ($B8BB-$B8CF): par_frames_left 3 -> 2 -> 1 gives two frames, each par_step_tracks (C) and
   frm_finish_frame, the flag cleared and waited for again; 0 ends it. The rest frames, the flag every 2nd call. No
   event (cmd_ev_count 0) and no reply (ser_status_due 0). The registers flow: the entry A and Y and X =
   cmd_ev_next ($B877 LDX) reach the passes' isr_par_ready; Y = 0 from $B8B6 (pros_seg_count + 1); par_step_tracks
   leaves A = pros_play_pitch >> 2 = 0, X = 0, Y = 10; the rest frames' calls leave A = frm_flags, X = 0 and Y = $0D
   (frm_transition_flags' LDY #$0D, frm_snapshot's X = 0), which par_play returns. */
static void test_play_tail(void)
{
    setup();
    fill_frame_b();
    memset(g_ram + ISR_FRAME_B, 0xEE, 20);
    g_ram[PAR_FRAMES_LEFT] = 3;
    g_ram[FRM_REST_FRAME] = 0x77;
    g_ram[FRM_FLAGS] = 0x5A;
    g_flag_every = 2;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    static const uint16_t k[] = {
        ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,                       /* pass 1: $B89F, $B8AB (the flag comes) */
        ROM_ISR_PAR_READY,                                          /* pass 2: $B89F, then $B8B1-$B8B9 */
        ROM_FRM_FINISH_FRAME, ROM_ISR_PAR_READY,                    /* frame 1: $B8C3, $B8C9 */
        ROM_FRM_FINISH_FRAME, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, /* frame 2: $B8C3, $B8C9 twice */
    };
    check_order(0, k, 8);
    check_play_call(0, ROM_ISR_PAR_READY, 0x11, 0, 0x33, 3);
    check_play_call(2, ROM_ISR_PAR_READY, 0x11, 0, 0x33, 3);
    check_play_call(3, ROM_FRM_FINISH_FRAME, 0, 0, FRAME_PITCH, 2);   /* par_step_tracks' A, X, Y */
    check_play_call(5, ROM_FRM_FINISH_FRAME, 0, 0, FRAME_PITCH, 1);
    CHECK_EQ(g_calls[4].frames, 2);
    CHECK_EQ(check_rest_frames(8, 2), 20);                             /* the flag every 2nd call */
    check_play_call(8, ROM_ISR_WAIT_FRAME, 0, 0, FRAME_PITCH, 0);
    check_play_call(9, ROM_FRM_COPY_REST_FRAME, 3, 0, FRAME_PITCH, 3); /* $B8D5 LDA #3 */
    check_play_call(10, ROM_FRM_SNAPSHOT, 0x77, 0, 0xFF, 3);           /* frm_copy_rest_frame's A and Y */
    check_play_call(11, ROM_FRM_CLAMP_MARK_SPACE, 0, 0, FRAME_PITCH, 3);   /* frm_snapshot's: b10 (0), X = 0 */
    check_play_call(12, ROM_FRM_TRANSITION_FLAGS, 0x0B, 0, 0x0B, 3);
    check_play_call(13, ROM_ISR_PAR_READY, 0x5A, 0, 0x0D, 3);
    check_play_call(15, ROM_FRM_COPY_REST_FRAME, 0x5A, 0, 0x0D, 2);
    check_play_call(21, ROM_FRM_COPY_REST_FRAME, 0x5A, 0, 0x0D, 1);
    check_play_call(27, ROM_ISR_PAR_READY, 0x5A, 0, 0x0D, 0);           /* $B8F7 */
    CHECK_EQ(g_ncalls, 28);
    CHECK_EQ(count(ROM_ISR_PAR_READY), 13);
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 0);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0);                                  /* cleared after the last rest frame */
    for (int b = 0; b <= FRAME_PITCH; b++)
        CHECK_EQ(g_ram[ISR_FRAME_B + b], 0);                             /* par_step_tracks wrote b0-b10 */
    CHECK_EQ(g_ram[ISR_FRAME_B + 11], 0xEE);
    CHECK_EQ(r.a, 0x5A);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x0D);
    CHECK(!g_f.fault);
}

/* One segment's frames in the main loop, with an event and the reply service. read_setup's segment (index 3 to 4),
   whose duration byte (index 3) is 2, and whose next phoneme's rank $1E takes the boundary jump (test_next_segment);
   pros_seg_count 1, so it is the last. An @B6 is due at segment 3 (cmd_ev_pos = par_seg_index = 3, cmd_ev_count 1).
   A reply is due (ser_status_due 1) and the transmitter ready (UART_STATUS bit 1): ser_tx_service at every pass.
   The flag every 2nd isr_par_ready.
   Pass 1: the event (cmd_run_event with A = par_seg_index's high byte 0, X = cmd_ev_next 0): cmd_voicing = 6,
   cmd_ev_next = 1; the test again: X = 1, not below cmd_ev_count. ser_tx_service gets the handler's A = 6 and Y
   and X = 1; isr_par_ready twice (the flag comes). Pass 2: par_frames_left 1 -> 0 ($B8FB): par_next_segment (C):
   par_frames_left = 2, pros_seg_count 0, par_seg_index 4; A = 2 ($B903). $B908: isr_par_ready, par_step_tracks,
   frm_finish_frame. Pass 3: no flag, pass 4: the flag; no segment left: the tight loop, 2 -> 1: one frame; 1 -> 0:
   the end. */
static void test_play_segment(void)
{
    read_setup();
    fill_frame_b();
    fw_wr16(&g_f, PAR_RANK_PTR, T6_RANKS);
    g_ram[T6_RANKS + T6_NEXT] = 0x1E;
    g_ram[PROS_SEG_DURATION + 3] = 2;
    fw_wr16(&g_f, PROS_SEG_COUNT, 1);
    g_ram[PAR_FRAMES_LEFT] = 1;
    g_ram[CMD_EV_COUNT] = 1;
    fw_wr16(&g_f, CMD_EV_POS, 3);
    fw_wr16(&g_f, CMD_EV_HANDLERS, ROM_CMD_EV_SET_VOICING);
    g_ram[CMD_EV_ARGS] = 6;
    g_ram[SER_STATUS_DUE] = 1;
    g_ram[FRM_FLAGS] = 0x5A;
    g_flag_every = 2;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    static const uint16_t k[] = {
        ROM_SER_TX_SERVICE, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,                      /* pass 1 */
        ROM_SER_TX_SERVICE, ROM_ISR_PAR_READY,                                         /* pass 2 */
        ROM_ISR_PAR_READY, ROM_FRM_FINISH_FRAME,                                       /* $B908-$B90E */
        ROM_SER_TX_SERVICE, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,                      /* pass 3 */
        ROM_SER_TX_SERVICE, ROM_ISR_PAR_READY,                                         /* pass 4 */
        ROM_FRM_FINISH_FRAME, ROM_ISR_PAR_READY,                                       /* the last frame */
    };
    check_order(0, k, 14);
    check_play_call(0, ROM_SER_TX_SERVICE, 6, 1, 0x33, 1);       /* the handler's A, cmd_ev_next from $B877 */
    check_play_call(4, ROM_ISR_PAR_READY, 6, 1, 0x33, 1);
    CHECK_EQ(g_calls[5].frames, 2);                              /* par_next_segment's duration */
    check_play_call(5, ROM_ISR_PAR_READY, 2, 10, 0, 2);          /* A = par_frames_left ($B903), par_next_segment's
                                                                    X = 10 and Y = 0 */
    check_play_call(6, ROM_FRM_FINISH_FRAME, 0x18, 0, FRAME_PITCH, 2);   /* the pitch $55 to $70 over 2 frames:
                                                                       ($70 + $55) >> 1 = $62, b10 = $62 >> 2 */
    check_play_call(12, ROM_FRM_FINISH_FRAME, 0x1C, 0, FRAME_PITCH, 1);  /* then over 1: $70, b10 = $1C */
    CHECK_EQ(check_rest_frames(14, 2), 20);
    CHECK_EQ(g_ncalls, 34);
    CHECK_EQ(g_ram[CMD_VOICING], 6);
    CHECK_EQ(g_ram[CMD_EV_NEXT], 1);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 4);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 0);
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 0);
    CHECK_EQ(r.a, 0x5A);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x0D);
    CHECK(!g_f.fault);
}

/* A mute: ser_abort bit 0 arrives with the flag at the first isr_par_ready. The abort test ($B8A2) comes before the
   flag test, so par_play goes straight to the end: no frame is built (the fill frame is untouched, par_frames_left
   is only set to 3 and counted down), pros_seg_count is kept, and ser_abort stays set (boot_get_clause clears it).
   With UART_STATUS bit 1 clear (the transmitter busy, $B895 BBC1) the due reply is not served. */
static void test_play_abort(void)
{
    setup();
    fill_frame_b();
    memset(g_ram + ISR_FRAME_B, 0xEE, 20);
    fw_wr16(&g_f, PROS_SEG_COUNT, 5);
    g_ram[PAR_FRAMES_LEFT] = 4;
    g_ram[SER_STATUS_DUE] = 1;
    g_uart_status = 0;
    g_flag_every = 1;
    g_abort_at = 1;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    check_play_call(0, ROM_ISR_PAR_READY, 0x11, 0, 0x33, 4);
    CHECK_EQ(check_rest_frames(1, 1), 17);
    check_play_call(1, ROM_ISR_WAIT_FRAME, 0x11, 0, 0x33, 4);
    CHECK_EQ(g_ncalls, 18);
    CHECK_EQ(count(ROM_SER_TX_SERVICE), 0);
    CHECK_EQ(count(ROM_FRM_FINISH_FRAME), 0);
    for (int b = 0; b < 20; b++)
        CHECK_EQ(g_ram[ISR_FRAME_B + b], 0xEE);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 5);
    CHECK_EQ(g_ram[SER_ABORT], 1);
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 0);
    CHECK(!g_f.fault);
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
    static const uint16_t k[] = { ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_FRM_FINISH_FRAME, ROM_ISR_PAR_READY };
    check_order(0, k, 4);
    check_play_call(1, ROM_ISR_PAR_READY, 0x11, 0, 1, 0xFF);   /* $B908 after the DEC; Y = pros_seg_count ($B8B1) */
    CHECK_EQ(check_rest_frames(4, 1), 17);
    CHECK_EQ(g_ncalls, 21);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 1);   /* par_next_segment was not called */
    CHECK(!g_f.fault);
}

/* par_next_segment leaving par_frames_left = 0 ($B906 BEQ back to $B8B1): the T6 segment with the duration byte 0;
   its current and next phonemes have the same targets $11-$1A (@u offsets 0, @B8: P10 unscaled), so every track
   is at its boundary and the 0-step interpolations of par_step_tracks return their targets. pros_seg_count 2 -> 1:
   segments are left, so the DEC at $B8FB makes $FF and a frame is built; the mute at the 3rd isr_par_ready ends
   it. */
static void test_play_zero_frames_segment(void)
{
    read_setup();
    fill_frame_b();
    fw_wr16(&g_f, PAR_RANK_PTR, T6_RANKS);
    g_ram[T6_RANKS + T6_NEXT] = 0x1E;
    for (unsigned p = 0; p < 10; p++) {
        g_ram[T6_TABLE + T6_CUR + T6_STRIDE * p] = g_ram[T6_TABLE + T6_NEXT + T6_STRIDE * p] = (uint8_t)(0x11 + p);
        g_ram[CMD_FORMANT_OFS + p] = 0;
    }
    g_ram[CMD_VOICING] = 8;
    g_ram[PROS_SEG_DURATION + 3] = 0;
    fw_wr16(&g_f, PROS_SEG_COUNT, 2);
    g_ram[PAR_FRAMES_LEFT] = 1;
    g_flag_every = 1;
    g_abort_at = 3;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    static const uint16_t k[] = { ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_FRM_FINISH_FRAME, ROM_ISR_PAR_READY };
    check_order(0, k, 4);
    check_play_call(1, ROM_ISR_PAR_READY, 0, 10, 1, 0xFF);   /* A = 0 ($B903), X = 10 (par_next_segment), Y = 1:
                                                               pros_seg_count again at $B8B1 */
    CHECK_EQ(check_rest_frames(4, 1), 17);
    CHECK_EQ(g_ncalls, 21);
    CHECK_EQ(fw_rd16(&g_f, PROS_SEG_COUNT), 1);
    CHECK_EQ(fw_rd16(&g_f, PAR_SEG_INDEX), 4);
    CHECK(!g_f.fault);
}

/* No segment left and par_frames_left = 0: the DEC at $B8BB gives $FF, and the tight loop builds 255 frames
   ($FF ... 1), each frm_finish_frame and one isr_par_ready (the flag at every call), before the end. */
static void test_play_zero_frames_tail(void)
{
    setup();
    fill_frame_b();
    g_flag_every = 1;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    check_play_call(0, ROM_ISR_PAR_READY, 0x11, 0, 0x33, 0);
    check_play_call(1, ROM_FRM_FINISH_FRAME, 0, 0, FRAME_PITCH, 0xFF);
    check_play_call(509, ROM_FRM_FINISH_FRAME, 0, 0, FRAME_PITCH, 1);
    CHECK_EQ(count(ROM_FRM_FINISH_FRAME), 255);
    CHECK_EQ(check_rest_frames(511, 1), 17);
    CHECK_EQ(g_ncalls, 528);
    CHECK(!g_f.fault);
}

/* boot_loop_play that does not play: FW_UTT_CONTENT ($6B) 0 ($A7D6 BEQ), or the host's mute pending (ser_abort bit 0,
   $A7D8 BBS0): A = $6B, X and Y untouched, no call, nothing written. */
static void test_loop_play_idle(void)
{
    for (int abort = 0; abort <= 1; abort++) {
        setup();
        g_ram[FW_UTT_CONTENT] = abort ? 0xFF : 0;
        g_ram[SER_ABORT] = (uint8_t)abort;
        g_flag_every = 1;   /* a call that played by mistake would still end */
        snapshot();
        fw_regs r = { 0x11, 0x22, 0x33, 0 };
        boot_loop_play(&g_f, &r);
        CHECK_EQ(r.a, abort ? 0xFF : 0);
        CHECK_EQ(r.x, 0x22);
        CHECK_EQ(r.y, 0x33);
        check_written(NULL, 0);
        check_no_calls();
    }
}

/* boot_loop_play that plays (FW_UTT_CONTENT $FF): isr_par_hold with A = $FF and the entry X and Y; par_play_start
   (C, test_play_start's voice A set-up: Y = the pause phoneme $3B, A = 0); isr_speech_start (A = X = 0 out);
   isr_volume_half; isr_wait_frame; isr_set_volume with A = cmd_volume ($B2C2); par_play (C), here ended by a mute
   at its first isr_par_ready; isr_volume_half; isr_wait_frame; isr_set_volume with A = 0 ($B2D0);
   isr_speech_stop, whose registers it returns. cmd_volume 9: the half is 4, P3 = (P3 & $87) | (4 ^ $F) << 3. */
static void test_loop_play(void)
{
    setup();
    pitch_identity();
    fw_wr16(&g_f, PAR_RANK_PTR, PAR_VOICE_A);
    g_ram[PROS_SEG_PHONEME] = 0x3B;
    g_ram[PROS_SEG_PITCH] = 0x70;
    g_ram[FW_UTT_CONTENT] = 0xFF;
    g_ram[CMD_VOLUME] = 9;
    g_ram[FRM_FLAGS] = 0x5A;
    g_p3 = 0xFF;
    g_flag_every = 1;
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
    CHECK_EQ(check_rest_frames(6, 1), 17);
    check_call(23, ROM_ISR_VOLUME_HALF, 0x5A, 0, 0x0D);          /* par_play's exit registers */
    check_call(24, ROM_ISR_WAIT_FRAME, 0xDF, 0, 0x0D);
    check_call(25, ROM_ISR_SET_VOLUME, 0, 0, 0x0D);
    check_call(26, ROM_ISR_SPEECH_STOP, 0xFF, 0, 0x0D);          /* P3 $87 | $F << 3 */
    CHECK_EQ(g_ncalls, 27);
    CHECK_EQ(g_p3, 0xFF);
    CHECK_EQ(g_ram[PAR_CUR_PHONEME], 0x3B);                      /* par_play_start ran */
    CHECK_EQ(g_ram[PAR_CUR_RANK], 31);
    CHECK_EQ(r.a, 0xFF);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x0D);
    CHECK(!g_f.fault);
}

void suite_fw_par(void)
{
```

At the end of `suite_fw_par`, replace

```c
    test_run_event_rom();
}
```

with

```c
    test_run_event_rom();
    test_play_rows();
    test_play_tail();
    test_play_segment();
    test_play_abort();
    test_play_zero_frames_main();
    test_play_zero_frames_segment();
    test_play_zero_frames_tail();
    test_loop_play_idle();
    test_loop_play();
}
```

What the tests pin (spec §4.5, this task's part):
- `test_play_rows`: the two rows (address, A/X/Y) and that exactly these two have `waits`.
- `test_play_tail`: no segment left: the two passes, the tight loop's two frames, the three rest frames; the order of every call, the registers flowing through them (`par_step_tracks`' A = 0, X = 0, Y = 10; the rest frames' A = `frm_flags`, X = 0, Y = $0D, which `par_play` returns), `par_frames_left` at each call, b0–b10 written by `par_step_tracks`, the flag cleared at the end.
- `test_play_segment`: an @B event at the current segment run before the reply service (`cmd_voicing` 6, `cmd_ev_next` 1), `ser_tx_service` at every pass with the handler's A and X = `cmd_ev_next`, the segment's last frame starting the next segment (`par_next_segment`, C: `par_frames_left` 2, `par_seg_index` 4, `pros_seg_count` 0) and its registers (A = 2 from $B903, X = 10, Y = 0), the pitch track's b10 ($62 >> 2, then $70 >> 2), then the tight loop and the end.
- `test_play_abort`: a mute seen before the frame flag (no frame built, `pros_seg_count` kept, `ser_abort` still set); with UART_STATUS bit 1 clear no reply is served.
- `test_play_zero_frames_main`, `test_play_zero_frames_segment`, `test_play_zero_frames_tail`: the `DEC` from 0 at $B8FB (entered with 0), the `BEQ $B906` back to $B8B1 after a 0-frame segment, and the 255 frames after the `DEC` from 0 at $B8BB.
- `test_loop_play_idle`: not playing ($6B = 0; or $6B = $FF with the mute pending): A = $6B, X and Y untouched, nothing written, no call.
- `test_loop_play`: its eleven ROM calls in order around the C `par_play_start` and `par_play`, with the registers at each (A = $6B at `isr_par_hold`, `cmd_volume` and 0 at the two `isr_set_volume`), and `isr_speech_stop`'s registers out.

In `tests/test_fw_tn.c` (as Task 1 leaves it, with `#include "firmware/par_addr.h"`):

Replace

```c
   (cmd_next += 1, cmd_next_pos = cmd_positions[cmd_next]); the other helpers leave the registers unchanged. */
```

with

```c
   (cmd_next += 1, cmd_next_pos = cmd_positions[cmd_next]); the other helpers leave the registers unchanged.
   isr_par_ready also sets isr_frame_flag bit 0 (a frame is due at every wait of the C par_play), and
   isr_speech_start sets ser_abort bit 0 (a mute as the DSP starts), so the C boot_loop_play of the mid-clause
   flush plays no segment: par_play goes from its first pass to the three rest frames. */
```

Replace

```c
    if (addr == ROM_ISR_PAR_READY || addr == ROM_BOOT_LOOP_PLAY)
        return FW_CALL_RETURNED;
    return FW_CALL_FAILED;
}
```

with

```c
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
    case ROM_FRM_COPY_REST_FRAME:
    case ROM_FRM_SNAPSHOT:
    case ROM_FRM_CLAMP_MARK_SPACE:
    case ROM_FRM_TRANSITION_FLAGS:
        return FW_CALL_RETURNED;
    default:
        return FW_CALL_FAILED;
    }
}

/* UART_STATUS: the transmitter ready (bit 1), nothing received. */
static uint8_t mock_read_sfr(void *ctx, uint8_t addr)
{
    (void)ctx;
    return addr == 0xE7 ? 0x02 : 0x00;
}
```

Replace

```c
    g_host.set_low_window = mock_window;
```

with

```c
    g_host.set_low_window = mock_window;
    g_host.read_sfr = mock_read_sfr;
```

Replace

```c
/* The mid-clause flush ($B1FF): the C pros_make_segments (M4.3b) turns the 250 symbols into segments, with its
   isr_par_ready calls (the strict budget), and the ROM boot_loop_play gets the registers it leaves, as the ROM's
   would; then the conversion goes on from tn_resume_pos with an empty buffer. */
```

with

```c
/* The mid-clause flush ($B1FF): the C pros_make_segments (M4.3b) turns the 250 symbols into segments, with its
   isr_par_ready calls (the strict budget), and the C boot_loop_play (M4.4b) gets the registers it leaves, as the
   ROM's would: it plays (FW_UTT_CONTENT is $FF), so its first ROM call, isr_par_hold, has them; the C
   par_play_start sets up the first segment; the mock's mute at isr_speech_start ends the C par_play at its first
   pass. Then the conversion goes on from tn_resume_pos with an empty buffer. */
```

Replace

```c
    CHECK_EQ(count(ROM_BOOT_LOOP_PLAY), 1);
```

with

```c
    CHECK_EQ(count(ROM_BOOT_LOOP_PLAY), 0);               /* the C boot_loop_play, not the ROM's */
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0xFF);
```

Replace

```c
       segment but the two end pauses) */
    CHECK_EQ(count(ROM_ISR_PAR_READY), 1374);
    CHECK(g_ncalls <= MAX_CALLS);
    for (unsigned i = 0; i < g_ncalls && i < MAX_CALLS; i++) {
        if (g_calls[i].addr != ROM_BOOT_LOOP_PLAY)
            continue;
        CHECK_EQ(g_calls[i].a, 0x71);                     /* pros_phrase_gen's A: pros_seg_count's low byte before
                                                             its DEC, */
        CHECK_EQ(g_calls[i].x, TN_PHONETIC_FLUSH);        /* X = pros_pos (tn_phon_len), */
        CHECK_EQ(g_calls[i].y, 0x70);                     /* Y = the second end pause's: as the ROM leaves them */
        CHECK_EQ(g_calls[i].phon_len, TN_PHONETIC_FLUSH);
        CHECK_EQ(g_calls[i].budget, FW_ROM_CALL_SPEECH_BUDGET);
        CHECK_EQ(g_ncalls - i, 6);                        /* then isr_par_ready at text[250..254] */
    }
```

with

```c
       segment but the two end pauses), par_play 5 (its pass, one wait per rest frame, its last call) */
    CHECK_EQ(count(ROM_ISR_PAR_READY), 1379);
    CHECK(g_ncalls <= MAX_CALLS);
    /* boot_loop_play's ROM calls, then isr_par_ready at text[250..254] */
    static const uint16_t k_play[] = {
        ROM_ISR_PAR_HOLD, ROM_ISR_SPEECH_START, ROM_ISR_VOLUME_HALF, ROM_ISR_WAIT_FRAME, ROM_ISR_SET_VOLUME,
        ROM_ISR_PAR_READY, ROM_ISR_WAIT_FRAME,                                     /* par_play: the pass, the end */
        ROM_FRM_COPY_REST_FRAME, ROM_FRM_SNAPSHOT, ROM_FRM_CLAMP_MARK_SPACE, ROM_FRM_TRANSITION_FLAGS,
        ROM_ISR_PAR_READY,
        ROM_FRM_COPY_REST_FRAME, ROM_FRM_SNAPSHOT, ROM_FRM_CLAMP_MARK_SPACE, ROM_FRM_TRANSITION_FLAGS,
        ROM_ISR_PAR_READY,
        ROM_FRM_COPY_REST_FRAME, ROM_FRM_SNAPSHOT, ROM_FRM_CLAMP_MARK_SPACE, ROM_FRM_TRANSITION_FLAGS,
        ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,
        ROM_ISR_VOLUME_HALF, ROM_ISR_WAIT_FRAME, ROM_ISR_SET_VOLUME, ROM_ISR_SPEECH_STOP,
        ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,
    };
    unsigned n = sizeof k_play / sizeof k_play[0];
    CHECK(g_ncalls >= n && g_ncalls <= MAX_CALLS);
    if (g_ncalls < n || g_ncalls > MAX_CALLS)
        return;
    unsigned first = g_ncalls - n;
    for (unsigned i = 0; i < n; i++)
        CHECK_EQ(g_calls[first + i].addr, k_play[i]);
    CHECK_EQ(g_calls[first].a, 0xFF);                     /* boot_loop_play's LDA $6B ($A7D4); */
    CHECK_EQ(g_calls[first].x, TN_PHONETIC_FLUSH);        /* X = pros_pos (tn_phon_len) and */
    CHECK_EQ(g_calls[first].y, 0x70);                     /* Y = the second end pause's, as pros_phrase_gen leaves
                                                             them */
    CHECK_EQ(g_calls[first].phon_len, TN_PHONETIC_FLUSH);
    CHECK_EQ(g_calls[first + 4].a, g_ram[CMD_VOLUME]);   /* isr_set_volume(cmd_volume) ($B2C2) */
    CHECK_EQ(g_calls[first + 25].a, 0);                   /* isr_set_volume(0) ($B2D0) */
    CHECK_EQ(fw_rd16(&g_f, CMD_EV_POS), 0xFFFF);          /* par_play_start: no event queued */
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 0);                  /* the three rest frames counted down */
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 1);                   /* set by the last isr_par_ready calls */
    for (unsigned i = first; i < g_ncalls; i++)
        CHECK_EQ(g_calls[i].budget, FW_ROM_CALL_BUDGET);
```

In `tests/test_golden.c`, replace

```c
#include "engine/rom_engine.h"
```

with

```c
#include "engine/hybrid_engine.h"
#include "engine/rom_engine.h"
```

and replace

```c
/* Spec section 4: the produced-frame stream does not depend on CPU timing. Scale the cycles the
   peripheral timers see (1/4 = timers 4x slower relative to the CPU, 3/1 = 3x faster). */
static void test_timing_invariance(const uint8_t *rom, size_t len)
{
    static const uint32_t k_scale[][2] = { { 1, 4 }, { 3, 1 } };
    char *text = test_read_file("tests/corpus/hello.txt");
    char *want = test_read_file("tests/golden/hello.frm");
    CHECK(text != NULL);
    CHECK(want != NULL);
    if (text && want) {
        strip_newline(text);
        for (size_t i = 0; i < sizeof k_scale / sizeof k_scale[0]; i++) {
            machine_config cfg;
            frame_list fl = { 0 };
            machine_default_config(&cfg);
            cfg.cycle_num = k_scale[i][0];
            cfg.cycle_den = k_scale[i][1];
            machine_status st = rom_engine_speak_cfg(&g_m, rom, len, &cfg, text, false, &fl);
            if (st != MACHINE_IDLE)
                machine_dump(&g_m, stderr);
            CHECK_EQ(st, MACHINE_IDLE);
            print_unmodelled(&g_m);
            CHECK_EQ(g_m.unmodelled_count, 0);
            char *hex = frame_list_hex(&fl);
            if (strcmp(hex, want) != 0) {
                fprintf(stderr, "  timer scale %u/%u:", (unsigned)cfg.cycle_num, (unsigned)cfg.cycle_den);
                report_diff("hello", hex, want);
                g_test_failures++;
            }
            printf("  timer scale %u/%u: %zu frames, %llu CPU cycles\n", (unsigned)cfg.cycle_num,
                   (unsigned)cfg.cycle_den, fl.count, (unsigned long long)g_m.cpu.cycles);
```

with

```c
/* Spec section 4: the produced-frame stream does not depend on CPU timing. Scale the cycles the
   peripheral timers see (1/4 = timers 4x slower relative to the CPU, 3/1 = 3x faster: there the ROM finishes some
   frames after their period has ended, and the timer interrupt sends the previous frame again). First the rom
   engine, then the hybrid engine with every replacement (M4.4b spec 4.4): its frame loop is C, which takes no
   emulated time, so it is never late; the produced frames must not change either way. */
static void test_timing_invariance(const uint8_t *rom, size_t len)
{
    static const uint32_t k_scale[][2] = { { 1, 4 }, { 3, 1 } };
    const size_t n = sizeof k_scale / sizeof k_scale[0];
    char *text = test_read_file("tests/corpus/hello.txt");
    char *want = test_read_file("tests/golden/hello.frm");
    CHECK(text != NULL);
    CHECK(want != NULL);
    if (text && want) {
        strip_newline(text);
        for (size_t j = 0; j < 2 * n; j++) {
            size_t i = j % n;
            bool hybrid = j >= n;
            const char *engine = hybrid ? "hybrid " : "";
            machine_config cfg;
            frame_list fl = { 0 };
            machine_default_config(&cfg);
            cfg.cycle_num = k_scale[i][0];
            cfg.cycle_den = k_scale[i][1];
            machine_status st = hybrid ? hybrid_engine_speak(&g_m, rom, len, &cfg, NULL, text, false, &fl, NULL, NULL)
                                       : rom_engine_speak_cfg(&g_m, rom, len, &cfg, text, false, &fl);
            if (st != MACHINE_IDLE)
                machine_dump(&g_m, stderr);
            CHECK_EQ(st, MACHINE_IDLE);
            print_unmodelled(&g_m);
            CHECK_EQ(g_m.unmodelled_count, 0);
            char *hex = frame_list_hex(&fl);
            if (strcmp(hex, want) != 0) {
                fprintf(stderr, "  %stimer scale %u/%u:", engine, (unsigned)cfg.cycle_num, (unsigned)cfg.cycle_den);
                report_diff("hello", hex, want);
                g_test_failures++;
            }
            printf("  %stimer scale %u/%u: %zu frames, %llu CPU cycles\n", engine, (unsigned)cfg.cycle_num,
                   (unsigned)cfg.cycle_den, fl.count, (unsigned long long)g_m.cpu.cycles);
```

In `tests/test_diff_play.c`, replace

```c
    printf("  %zu routines wait on the timer interrupt\n", g_nwatch);
```

with

```c
    printf("  %zu routines wait on the timer interrupt\n", g_nwatch);
    CHECK_EQ(g_nwatch, 2);   /* par_play and boot_loop_play (M4.4b spec 2.5) */
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmd //c ".\build.bat test fw_par"`
Expected: the build stops with `tests\test_fw_par.c(15): fatal error C1083: Cannot open include file: 'firmware/par_play.h': No such file or directory`.

- [ ] **Step 3: Implement**

Create `src/firmware/par_play.h`:

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

/* $B877: the frame loop. While segments remain (pros_seg_count != 0), each pass runs every command event due at
   par_seg_index (cmd_run_event, one per pass of the test), serves the reply output (ser_tx_service, when UART_STATUS
   bit 1 is set and ser_status_due != 0), calls isr_par_ready, ends at a mute (ser_abort bit 0), and once per frame
   (isr_frame_flag bit 0) counts par_frames_left down, starts the next segment at 0 (par_next_segment), steps the
   tracks (par_step_tracks) and has the frame finished (frm_finish_frame), then clears the flag. When no segment is
   left: the remaining frames with no events, no reply service and no abort test. At the end, or at a mute: three
   rest frames (frm_copy_rest_frame, frm_snapshot, frm_clamp_mark_space, frm_transition_flags), then
   cmd_ev_sound_end and a last isr_par_ready. r in: A, X and Y flow into the first calls. r out: as cmd_ev_sound_end
   leaves them (isr_par_ready preserves them). */
void par_play(fw *f, fw_regs *r);

/* $A7D4: when the utterance has content (FW_UTT_CONTENT, $6B) and no mute is pending (ser_abort bit 0), play it:
   isr_par_hold, par_play_start (C), isr_speech_start, isr_volume_half, isr_wait_frame, isr_set_volume(cmd_volume),
   par_play (C), isr_volume_half, isr_wait_frame, isr_set_volume(0), isr_speech_stop. r in: A, X and Y flow into
   the first calls. r out: A = FW_UTT_CONTENT and X, Y untouched when it does not play; else the registers
   isr_speech_stop leaves. */
void boot_loop_play(fw *f, fw_regs *r);

#endif
```

Create `src/firmware/par_play.c`:

```c
#include "par_play.h"
#include "cmd_play_event.h"
#include "fw_ops.h"
#include "par_addr.h"
#include "par_start.h"
#include "par_track.h"

#define SFR_UART_STATUS  0xE7   /* UART_STATUS (an SFR): bit 1 = the transmitter is ready */
#define UART_TX_READY    0x02   /* $B895 BBC1 UART_STATUS */

/* ---- par_play $B877 ---- */

/* DEC par_frames_left ($B8BB, $B8EF, $B8FB): mod 256, so 0 becomes $FF. Returns the new count (its Z flag). */
static uint8_t count_frame(fw *f)
{
    uint8_t n = (uint8_t)(fw_rd8(f, PAR_FRAMES_LEFT) - 1);
    fw_wr8(f, PAR_FRAMES_LEFT, n);
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
   cmd_ev_pos, low byte, then high byte (CMP / BNE). cmd_run_event runs it with X = cmd_ev_next and A = the
   high byte of par_seg_index, and the JMP par_play ($B892) tests again. False when none is due: X = cmd_ev_next,
   and A = the last byte loaded, or as before when the BCS left first. */
static bool run_due_event(fw *f, fw_regs *r)
{
    r->x = fw_rd8(f, CMD_EV_NEXT);
    if (r->x >= fw_rd8(f, CMD_EV_COUNT))
        return false;
    r->a = fw_rd8(f, PAR_SEG_INDEX);
    if (r->a != fw_rd8(f, CMD_EV_POS))
        return false;
    r->a = fw_rd8(f, PAR_SEG_INDEX + 1);
    if (r->a != fw_rd8(f, CMD_EV_POS + 1))
        return false;
    cmd_run_event(f, r);
    return true;
}

/* $B895-$B89C: the reply output, when the transmitter is ready (UART_STATUS bit 1, BBC1) and a status byte is due
   (TST ser_status_due, the whole byte). */
static void serve_reply(fw *f, fw_regs *r)
{
    if ((fw_sfr8(f, SFR_UART_STATUS) & UART_TX_READY) && fw_rd8(f, SER_STATUS_DUE) != 0)
        fw_rom_call(f, ROM_SER_TX_SERVICE, r, FW_ROM_CALL_BUDGET);
}

/* $B8B1-$B907, with isr_frame_flag set: Y = pros_seg_count's low byte, or its high byte when that is 0 (LDY / BNE,
   twice). With no segment left, false ($B8BB). Otherwise DEC par_frames_left; when it reaches 0, par_next_segment
   and A = the new par_frames_left, and when that is 0 too, the test again from $B8B1 (BEQ $B906). True when a frame
   of the segment follows ($B908). */
static bool segment_frame(fw *f, fw_regs *r)
{
    for (;;) {
        r->y = fw_rd8(f, PROS_SEG_COUNT);
        if (r->y == 0) {
            r->y = fw_rd8(f, PROS_SEG_COUNT + 1);
            if (r->y == 0)
                return false;
        }
        if (count_frame(f) != 0)
            return true;
        par_next_segment(f, r);
        r->a = fw_rd8(f, PAR_FRAMES_LEFT);
        if (r->a != 0)
            return true;
    }
}

/* $B908-$B911: isr_par_ready, par_step_tracks, frm_finish_frame (it saves and restores A, X and Y), then LDM #0:
   the whole of isr_frame_flag cleared, the frame is ready for the next swap. */
static void segment_step(fw *f, fw_regs *r)
{
    fw_par_ready(f, *r);
    par_step_tracks(f, r);
    fw_rom_call(f, ROM_FRM_FINISH_FRAME, r, FW_ROM_CALL_BUDGET);
    fw_wr8(f, ISR_FRAME_FLAG, 0);
}

/* $B8BB-$B8CF, once no segment is left: DEC par_frames_left; until it reaches 0, par_step_tracks, frm_finish_frame,
   the flag cleared, and a wait for the next swap. No events, no reply service, no abort test. */
static void last_frames(fw *f, fw_regs *r)
{
    while (count_frame(f) != 0) {
        par_step_tracks(f, r);
        fw_rom_call(f, ROM_FRM_FINISH_FRAME, r, FW_ROM_CALL_BUDGET);
        fw_wr8(f, ISR_FRAME_FLAG, 0);
        wait_frame_flag(f, *r);
    }
}

/* $B8D2-$B8FA, the end: isr_wait_frame; par_frames_left = 3; per rest frame frm_copy_rest_frame, frm_snapshot,
   frm_clamp_mark_space and frm_transition_flags, a wait for the swap, the flag cleared (LDM #0) and DEC
   par_frames_left (BNE $B8DA). Then cmd_ev_sound_end (a @T effect cut short is undone) and isr_par_ready. */
static void rest_frames(fw *f, fw_regs *r)
{
    fw_rom_call(f, ROM_ISR_WAIT_FRAME, r, FW_ROM_CALL_BUDGET);
    r->a = PAR_REST_FRAMES;
    fw_wr8(f, PAR_FRAMES_LEFT, r->a);
    do {
        fw_rom_call(f, ROM_FRM_COPY_REST_FRAME, r, FW_ROM_CALL_BUDGET);
        fw_rom_call(f, ROM_FRM_SNAPSHOT, r, FW_ROM_CALL_BUDGET);
        fw_rom_call(f, ROM_FRM_CLAMP_MARK_SPACE, r, FW_ROM_CALL_BUDGET);
        fw_rom_call(f, ROM_FRM_TRANSITION_FLAGS, r, FW_ROM_CALL_BUDGET);
        wait_frame_flag(f, *r);
        fw_wr8(f, ISR_FRAME_FLAG, 0);
    } while (count_frame(f) != 0);
    cmd_ev_sound_end(f, r);
    fw_par_ready(f, *r);
}

void par_play(fw *f, fw_regs *r)
{
    for (;;) {                                 /* par_play $B877: one pass */
        if (f->fault)
            return;                            /* a failed ROM call without a trap: the ROM would not get past it */
        if (run_due_event(f, r))
            continue;                          /* $B892 JMP par_play */
        serve_reply(f, r);
        fw_par_ready(f, *r);                   /* $B89F */
        if (fw_rd8(f, SER_ABORT) & 1)          /* $B8A2 BBC0 ser_abort */
            break;                             /* $B8A5 JMP $B8D2: a mute */
        if (!(fw_rd8(f, ISR_FRAME_FLAG) & 1)) {   /* $B8A8 BBS0 isr_frame_flag */
            fw_par_ready(f, *r);               /* $B8AB, then $B8AE JMP par_play */
            continue;
        }
        if (!segment_frame(f, r)) {
            last_frames(f, r);
            break;
        }
        segment_step(f, r);                    /* then $B914 JMP par_play */
    }
    rest_frames(f, r);
}

/* ---- boot_loop_play $A7D4 ---- */

/* $B2B3-$B2D8, boot_play_body (JMP from $A7DB): hold the host, set up playback, start the DSP with a frame's wait,
   ramp the volume in (half, a frame, then cmd_volume), play, ramp it out (half, a frame, then 0) and stop the DSP.
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

void boot_loop_play(fw *f, fw_regs *r)
{
    r->a = fw_rd8(f, FW_UTT_CONTENT);          /* $A7D4 LDA $6B / BEQ $A7DE */
    if (r->a == 0 || (fw_rd8(f, SER_ABORT) & 1))   /* $A7D8 BBS0 ser_abort,$A7DE */
        return;                                /* $A7DE RTS */
    play_body(f, r);
}
```

In `src/firmware/tn_convert.c`, replace (Task 1 added the `par_addr.h` include for `ROM_BOOT_LOOP_PLAY`, whose one use goes here)

```c
#include "par_addr.h"
```

with

```c
#include "par_play.h"
```

and replace

```c
        /* $B1FF: speak tn_phonetic, then convert on from here with an empty buffer. The C pros_make_segments
           (M4.3b) leaves in r what the ROM's leaves (pros_phrase_gen's A, X and Y) for boot_loop_play. The entry Y
           here is 0, the ROM has the last character's: it reaches only isr_par_ready, which never reads it, and
           $CBB3 reloads Y, so no RAM differs (M4.3b spec 6). */
        fw_wr8(f, TN_RESUME_POS, x);
        fw_regs r = { fw_rd8(f, TN_PHON_LEN), x, 0, 0 };
        pros_make_segments(f, &r);
        fw_rom_call(f, ROM_BOOT_LOOP_PLAY, &r, FW_ROM_CALL_SPEECH_BUDGET);
```

with

```c
        /* $B1FF: speak tn_phonetic, then convert on from here with an empty buffer. The C pros_make_segments
           (M4.3b) leaves in r what the ROM's leaves (pros_phrase_gen's A, X and Y) for the C boot_loop_play
           (M4.4b). The entry Y here is 0, the ROM has the last character's: it reaches only isr_par_ready, which
           never reads it, and $CBB3 reloads Y, so no RAM differs (M4.3b spec 6). */
        fw_wr8(f, TN_RESUME_POS, x);
        fw_regs r = { fw_rd8(f, TN_PHON_LEN), x, 0, 0 };
        pros_make_segments(f, &r);
        boot_loop_play(f, &r);
```

In `src/engine/thunks.c`:
- Replace

```c
#include "firmware/par_track.h"
```

  with

```c
#include "firmware/par_track.h"
#include "firmware/par_play.h"
```

- Replace the end of `cmd_apply_speaker_regs`

```c
    c->a = cmd_apply_speaker(f, &x, &y);
    c->x = x;
    c->y = y;
}
```

  with

```c
    c->a = cmd_apply_speaker(f, &x, &y);
    c->x = x;
    c->y = y;
}

/* boot_loop_play $A7D4: A, X, Y in; none is read (A is loaded first; X and Y flow into isr_par_hold, which keeps
   them, and par_play_start, which writes Y and keeps X). Out: A = $6B and X, Y untouched when it does not play
   ($A7DE RTS); else the A, X and Y isr_speech_stop leaves (its RTS at $B3A3). The C's ROM calls overwrite the
   machine's registers, so all three are set from the C. Nothing reads them after a call: boot_reset ($A687) jumps
   to boot_loop_text, whose boot_get_clause writes X and A first ($A697 LDX #1, $A699 LDA #$20);
   boot_selftest_sound ($A7BA) returns to $A657 (BBS0 boot_dip, then LDX #0 or JMP $A66B: LDM, CLI, the isr_par_*
   calls that keep the registers, and cmd_apply_speaker, whose par_setup_voice reads none); cmd_say_phonetic
   ($A87C, unreachable) returns; the C tn_convert_clause (and the ROM's $B207 JMP $B1E9) writes X first (LDX #0).
   No caller reads a flag. It waits on the timer interrupt (par_play), so diff_play checks it, not diff_lts.
   Declared: A, X, Y. */
static void boot_loop_play_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    boot_loop_play(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

- Replace the end of `par_setup_voice_regs`

```c
    par_setup_voice(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

  with

```c
    par_setup_voice(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* par_play $B877: A, X, Y in; none is read before it is written (X at $B877 LDX cmd_ev_next; A and Y reach only
   ROM calls and C routines that write them first: isr_par_ready and ser_tx_service keep them, par_step_tracks
   starts with LDX #$0A, cmd_run_event with LDA cmd_ev_next). Out: as cmd_ev_sound_end leaves them, kept by the last
   isr_par_ready ($B8F7) to the RTS at $B8FA. The C's ROM calls overwrite the machine's registers, so all three are
   set from the C. Its one caller, boot_play_body ($B2C7), calls isr_volume_half next, which loads A first ($B46C
   LDA cmd_volume) and reads no X or Y; no caller reads a flag. The hook fires on the JSR only: once it has run,
   par_play's own JMP par_play ($B892, $B8AE, $B914) never runs. It waits on the timer interrupt, so diff_play checks
   it, not diff_lts. Declared: A, X, Y. */
static void par_play_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_play(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

- Replace

```c
THUNK(cmd_apply_speaker)
```

  with

```c
THUNK(cmd_apply_speaker)
THUNK(boot_loop_play)
```

- Replace

```c
THUNK(par_setup_voice)
```

  with

```c
THUNK(par_setup_voice)
THUNK(par_play)
```

- Replace

```c
    { "cmd_apply_speaker",  0xA78C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_apply_speaker },
```

  with

```c
    { "cmd_apply_speaker",  0xA78C, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_cmd_apply_speaker },
    { "boot_loop_play",     0xA7D4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_boot_loop_play, true },
```

- Replace

```c
    { "par_setup_voice",    0xB3A4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_setup_voice },
```

  with

```c
    { "par_setup_voice",    0xB3A4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_setup_voice },
    { "par_play",           0xB877, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_play, true },
```

The table then holds 64 rows in address order, `boot_loop_play` $A7D4 second and `par_play` $B877 after `par_setup_voice` $B3A4. `DIFF_MAX` (64 since Task 1) is the row count; `diff_lts` installs 62 observers (it skips the two `waits` rows). `par_play.c` is a new basename under `src/` and `tests/`.

- [ ] **Step 4: Faithfulness check against the dumps**

Read each dump side by side with the C, block by block (each static helper names its range):
- `rom_B877_par_play.txt` against `par_play.c`:
  - `run_due_event` ($B877–$B892): `LDX cmd_ev_next`, `CPX cmd_ev_count` / `BCS` (unsigned: no event when X ≥ count); `LDA par_seg_index` / `CMP cmd_ev_pos` / `BNE`; `LDA par_seg_index+1` / `CMP cmd_ev_pos+1` / `BNE`; `JSR cmd_run_event`; `JMP par_play` (the `continue`).
  - `serve_reply` ($B895–$B89C): `BBC1 UART_STATUS` (bit 1 of `fw_sfr8(f, $E7)`), `TST ser_status_due` / `BEQ` (the whole byte), `JSR ser_tx_service`.
  - the pass in `par_play` ($B89F–$B8AE): `JSR isr_par_ready`; `BBC0 ser_abort` (bit 0) / `JMP $B8D2`; `BBS0 isr_frame_flag` (bit 0); `JSR isr_par_ready`; `JMP par_play`.
  - `segment_frame` ($B8B1–$B907): `LDY pros_seg_count` / `BNE $B8FB`; `LDY pros_seg_count+1` / `BNE $B8FB`; else $B8BB; `DEC par_frames_left` / `BNE $B908`; `JSR par_next_segment`; `LDA par_frames_left` / `BEQ $B8B1`.
  - `segment_step` ($B908–$B914): `JSR isr_par_ready`, `JSR par_step_tracks`, `JSR frm_finish_frame`, `LDM #0,isr_frame_flag`, `JMP par_play`.
  - `last_frames` ($B8BB–$B8CF): `DEC` / `BEQ $B8D2`; `JSR par_step_tracks`, `JSR frm_finish_frame`, `LDM #0`; the wait `JSR isr_par_ready` / `BBC0 isr_frame_flag` ($B8C9–$B8CC); `JMP $B8BB`.
  - `rest_frames` ($B8D2–$B8FA): `JSR isr_wait_frame`; `LDA #3` / `STA par_frames_left`; the four `JSR`s $B8DA–$B8E3; the wait $B8E6–$B8E9; `LDM #0`; `DEC` / `BNE $B8DA`; `JSR cmd_ev_sound_end`; `JSR isr_par_ready`; `RTS`.
- `rom_A7D4_boot_loop_play.txt` against `boot_loop_play` ($A7D4 `LDA $6B` / `BEQ $A7DE`, $A7D8 `BBS0 ser_abort,$A7DE`, $A7DB `JMP boot_play_body`, $A7DE `RTS`) and `play_body` ($B2B3–$B2D8: the eleven `JSR`s in order, `LDA cmd_volume` at $B2C2 and `LDA #0` at $B2D0, `RTS`).
- For the registers each ROM callee leaves (the unit-test mocks), read the callee dumps listed in "ROM facts checked"; for the thunk comments, the caller dumps listed there.

Confirm the callers by scanning the operands:

```bash
python -c "import re,os;d=open(os.environ['APOLLO2_ROM'],'rb').read();[print(hex(a),hex(op),[hex(m.start()) for m in re.finditer(re.escape(bytes([op,a&255,a>>8])),d)]) for a in (0xB877,0xA7D4,0xB2B3) for op in (0x20,0x4C)]"
```

Expected:

```
0xb877 0x20 ['0xb2c7']
0xb877 0x4c ['0xb892', '0xb8ae', '0xb914']
0xa7d4 0x20 ['0xa687', '0xa7ba', '0xa87c', '0xb204']
0xa7d4 0x4c []
0xb2b3 0x20 []
0xb2b3 0x4c ['0xa7db']
```

The three `JMP $B877` are `par_play`'s own (the C loop); $B2B3 is reached only from $A7DB (the C `play_body`). Then the relative branches:

```bash
python -c "
import os
d=open(os.environ['APOLLO2_ROM'],'rb').read()
br={0x10,0x30,0x50,0x70,0x80,0x90,0xB0,0xD0,0xF0}
s=lambda b:b-256 if b>127 else b
for t in (0xB877,0xA7D4,0xB2B3):
  h=[hex(p) for p in range(0x8000,0xFFFE) if (d[p] in br or d[p]&0x1F in (3,0x13)) and p+2+s(d[p+1])==t]
  h+=[hex(p) for p in range(0x8000,0xFFFD) if d[p]&0x1F in (7,0x17) and p+3+s(d[p+2])==t]
  print(hex(t),h)
"
```

Expected: `0xb877 []`, `0xa7d4 []`, `0xb2b3 ['0xb2e9']`. The one hit is not an instruction: $B2E9 is the high byte ($80) of `STA $8000` at $B2E7 (`isr_timer_dsp`), which matches the `BRA` pattern.

Then confirm spec §4.7 criterion 7:

```bash
grep -rn "ROM_BOOT_LOOP_PLAY\|FW_ROM_CALL_SPEECH_BUDGET" src
```

Expected: `src/firmware/par_addr.h` (the `#define ROM_BOOT_LOOP_PLAY`) and `src/firmware/fw.h` (the budget's comment and `#define`), nothing in `tn_convert.c`.

- [ ] **Step 5: Run the tests**

Run, in order:
- `cmd //c ".\build.bat test fw_par"`, `cmd //c ".\build.bat test fw_tn"`: each `ok`, then `1 suite(s), 0 failure(s), 0 skip(s)`.
- `cmd //c ".\build.bat test"` (the full suite; it prints the lines below).

Expected in the full run:
- `33 suite(s), 0 failure(s), 0 skip(s)`, no warnings, the firmware boundary check passing (430 s in the proof run).
- `suite_golden`:

```
  timer scale 1/4: 105 frames, 11789152 CPU cycles
  timer scale 3/1: 105 frames, 1286784 CPU cycles
  hybrid timer scale 1/4: 105 frames, 11701952 CPU cycles
  hybrid timer scale 3/1: 105 frames, 999330 CPU cycles
```

- `suite_diff_lts`: 62 lines `<name> N calls checked`, every N > 0, 0 mismatches; `par_play` and `boot_loop_play` have no line (`waits`); every N as Task 3 left it; `1 mid-clause flush calls skipped (golden_hybrid covers them)`, `2 unwound calls compared`.
- `suite_diff_play`:

```
  self-check (ROM against ROM, hello): par_play 1 calls, boot_loop_play 2 calls, 2 injected; $B8A5 reached 2, $B89C reached 2
  2 routines wait on the timer interrupt
  boot_loop_play       175 calls checked, 46 with both injections
  par_play             124 calls checked, 46 with both injections
  $B8A5 (abort) reached in 92 calls, $B89C (reply due) in 92
```

  (175 and 124 count the boot run's calls once and every corpus file's speak phase; they depend on Task 1's `late_event` text, which here played 2 calls of each.)
- `suite_golden_hybrid`: `46 corpus files x 65 modes (all, then each routine alone)`, all identical.
- `suite_firmware_data`: `47702 converted bytes compared with the ROM, 0 differ`; `suite_synth`: `corpus peak plain 20522 (-4.1 dBFS)`, `corpus peak b11 16038 (-6.2 dBFS)`.

If `diff_play` reports a mismatch, find the root cause in the dump and fix the C faithfully; never loosen the comparison.

Then confirm spec criterion 6 on every corpus text (each read from its file; the outputs go to `build/`, always with `-o`):

```bash
SP=build/m44b_cli
mkdir -p "$SP"
for f in tests/corpus/*.txt; do
  t=$(basename "$f" .txt)
  ./build/apollo2.exe --engine rom --text-file "$f" -o "$SP/${t}_rom.wav" || echo "$t rom failed"
  ./build/apollo2.exe --engine hybrid --text-file "$f" -o "$SP/${t}_hybrid.wav" || echo "$t hybrid failed"
  cmp -s "$SP/${t}_rom.wav" "$SP/${t}_hybrid.wav" && echo "$t same" || echo "$t DIFFERS"
done | grep -vc " same$"
rm -rf "$SP"
```

Expected: `0` (all 45 texts `same`; no `failed` or `DIFFERS` line). The proof run took 14 s.

- [ ] **Step 6: Commit**

```bash
git add src/firmware/par_play.h src/firmware/par_play.c src/firmware/tn_convert.c src/engine/thunks.c tests/test_fw_par.c tests/test_fw_tn.c tests/test_golden.c tests/test_diff_play.c
git commit -m "par_play and boot_loop_play in C, with their hooks (waits: checked by diff_play); the C tn_convert_clause calls the C boot_loop_play; test_timing_invariance on the hybrid engine (M4.4b 2.1-2.6, 4.1, 4.4)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 6: Parent spec rev 13, M4.4b status and README (spec §4.7 criterion 9)

**Files:**
- Modify: `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`, `docs/superpowers/specs/2026-10-07-m4-4b-frame-loop-design.md`, `README.md`

**Interfaces:** documentation only.

- [ ] **Step 1: Parent spec, rev 13**

Make these edits in `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`. Mark each one *(rev 13)*. Find each place by its text, not by line number.

1. **Status line** (line 4). Insert after `**Status:** Approved. `:

   `*(rev 13)* Revision 13 records milestone 4.4b: the frame loop in C (par_play, the track steppers, the command events, boot_loop_play), so all of playback above the frame builder runs as C; the firmware reads an SFR through its host (design: docs/superpowers/specs/2026-10-07-m4-4b-frame-loop-design.md). `

2. **§8 milestone 4** (the paragraph containing `**M4.4a done**`). Replace its last sentence, `Next: M4.4b, the frame loop (par_play, the track steppers, the command events, boot_loop_play).`, with:

   `Next: M4.4b, the frame loop (par_play, the track steppers, the command events, boot_loop_play). *(rev 13)* **M4.4b done** <DATE>: the frame loop runs as C (par_play and boot_loop_play, which wait on the timer interrupt through ROM calls to isr_par_ready, the six track routines, cmd_run_event and its five handlers, util_shl3/util_shl4; 64 routines hooked in all), and the C tn_convert_clause calls the C boot_loop_play directly; the firmware reads UART_STATUS through a new host call (read_sfr); par_slot_map is converted (47,702 bytes in all); a new corpus golden (rom engine), late_event, reaches an event at segment 256 or later; diff_lts (62 routines), the new live differential test diff_play (par_play and boot_loop_play with interrupts running, compared at every frame swap, with an injected mute and reply), golden_hybrid (46 files x 65 modes) and test_timing_invariance on both engines pass. M4.4 is complete. Next: M4.5, the frame builder.`

   Replace `<DATE>` with the date of your commit (`date +%F`).

3. **The M4.0 foundation paragraph** (the paragraph containing `*(rev 9)* M4.2: a ROM call from C that resets the stack`). Append at its end:

   ` *(rev 13)* M4.4b: the host also reads an SFR for the firmware (read_sfr; fw_sfr8 faults without one), for UART_STATUS, which par_play polls. A routine that waits on the timer interrupt is marked in the replacement table (waits) and checked by diff_play instead of diff_lts: both copies run with interrupts live and are compared at every frame swap, where both main programs are waiting.`

- [ ] **Step 2: M4.4b spec status and As built**

In `docs/superpowers/specs/2026-10-07-m4-4b-frame-loop-design.md`, replace `**Status:** design, awaiting user review` with:

`**Status:** implemented (plan docs/superpowers/plans/2026-10-07-m4-4b-frame-loop.md)`

Then add a final section headed `## 6. As built`, after §5. Open it with the sentence `Where the ROM is more precise than, or differs from, the sections above. The ROM wins; ``ghidra740/`` is not edited.` (one pair of backquotes around `ghidra740/`). Then write one short factual bullet per difference between the spec and what was built. Gather them from:
- the "ROM facts checked" lists at the top of Tasks 1–5 of the plan, keeping only those that contradict or sharpen the spec;
- the code:
  - the thunk outputs as finally declared for the 16 new hooks (read the rows and comments in `src/engine/thunks.c`) and why;
  - the C file layout (`par_track.c`, `cmd_play_event.c`, `par_play.c`, the additions to `fw.h`/`fw.c`, `fw_ops.h`, `par_addr.h`) and the static helpers of the larger routines;
  - `host_read_sfr`'s scope as built;
  - where `fw_fault` is used;
  - `diff_play`'s structure as built (the arming hook at $B2C7, the injection points, the boot call checked in the boot run only, the exit exclusions) and its counts;
  - the unit-test harness changes in `tests/test_fw_par.c` (the call record, the mock's cases);
- the `late_event` text as committed.

Every bullet must be true of the committed code and the dumps. A claim about a test must say only what that test checks. Follow the style of §6 of `docs/superpowers/specs/2026-10-07-m4-4a-playback-setup-design.md`. If nothing differs, add no section.

- [ ] **Step 3: README**

In `README.md`'s "Hybrid engine (milestone 4)" section, replace:

```
(M4.3b), so all of prosody before playback, and the playback set-up: segment reading, pitch scaling, the
boundary and dominance pass, track planning and the voice set-up (M4.4a); the rest still runs on the emulated CPU.
```

with:

```
(M4.3b), so all of prosody before playback, the playback set-up: segment reading, pitch scaling, the
boundary and dominance pass, track planning and the voice set-up (M4.4a), and the frame loop: par_play, the track
steppers, the command events and boot_loop_play (M4.4b); the rest still runs on the emulated CPU.
```

Then replace:

```
The hybrid engine must reproduce every golden frame stream byte for byte (`build.bat test golden_hybrid`), and
`build.bat test diff_lts` compares each ported routine with the ROM at real call sites.
```

with:

```
The hybrid engine must reproduce every golden frame stream byte for byte (`build.bat test golden_hybrid`),
`build.bat test diff_lts` compares each ported routine with the ROM at real call sites, and
`build.bat test diff_play` compares the two that wait on the timer interrupt (par_play, boot_loop_play) with the
ROM at every frame swap, with interrupts running.
```

- [ ] **Step 4: Run the full suite and commit**

Run: `cmd //c ".\build.bat test"`
Expected: `33 suite(s), 0 failure(s), 0 skip(s)`.

```bash
git add docs/superpowers/specs/2026-09-30-software-apollo2-design.md docs/superpowers/specs/2026-10-07-m4-4b-frame-loop-design.md README.md
git commit -m "Spec rev 13: milestone 4.4b done (the frame loop in C)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
