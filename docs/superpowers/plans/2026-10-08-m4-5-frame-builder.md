# M4.5 The Frame Builder in C — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the seven frame-builder routines (`frm_*`) to C and prove them against the ROM through `diff_lts`, `diff_play` (inside the C `par_play`) and the goldens, so that playback calls the ROM only for `isr_*` and `ser_*`.

**Architecture:**
- **Data, corpus and test plumbing first (Task 1).**
  - The frame-byte names b11–b17, the three small tables in a new `frm_data.c`, and the three formula tables expanded by the encoder.
  - Two new corpus texts (`formant_extremes`, `mark_space_cap`) and their rom-engine goldens.
  - `DIFF_MAX` 72, and `diff_play`'s abort injection moved from the `frm_finish_frame` hook to `isr_par_ready`, proved while the frame builder is still ROM.
  - The new unit-test suite `suite_fw_frm`.
- **The leaves, then the finish.**
  - The five leaves (Task 2): `frm_copy_rest_frame`, `frm_build_rest_frame`, `frm_snapshot`, `frm_clamp_mark_space`, `frm_raise_formants`. `par_setup_voice` calls them directly.
  - The finish (Task 3): `frm_transition_flags` and `frm_finish_frame`. `par_play` calls the frame builder directly.
- **C calls C.** `par_setup_voice` and `par_play` → the frame builder; `frm_finish_frame` → `frm_snapshot`, `frm_clamp_mark_space`, `frm_raise_formants`, `frm_transition_flags`; `frm_raise_formants` → `par_clamp`; `frm_build_rest_frame` → `par_next_param`.
- **Hooks.** All seven routines, giving 71 replacements and 72 `golden_hybrid` modes.

**Tech Stack:** C11 with MSVC 2022 (`build.bat`, `/W4 /WX`).

**Spec:** `docs/superpowers/specs/2026-10-08-m4-5-frame-builder-design.md` (M4.5 spec). Its foundations are:
- the M4.4b spec, `docs/superpowers/specs/2026-10-07-m4-4b-frame-loop-design.md` (`par_play`, `diff_play` §4.1, §6 "As built");
- the M4.4a spec, `docs/superpowers/specs/2026-10-07-m4-4a-playback-setup-design.md` (`par_setup_voice`, the conventions §2.3–§2.7, §6 "As built");
- the M4.0 spec, `docs/superpowers/specs/2026-10-03-m4-0-replacement-foundation-design.md` (§8 "As built").

The parent spec is `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 13; §1.4 describes the frame bytes).

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
- The full suite takes about 8 minutes (`golden_hybrid` 48 files × 72 modes, `diff_play` about 40 s). Use a suite filter while iterating and the full run at each task's end.

**What may and may not be committed**
- Never commit the ROM, any `*.BIN`/`*.bin`, `*.mp3`, `apollo2_excerpt.wav`, `*.sla`, `build/` or `apollo_ghidra/`.
- Committed files contain no ROM bytes as hex blobs. Converted data is readable C: numbers with their meaning, named bits, strings, named addresses, or a formula the encoder expands.
- Stage files by explicit path only. Never use `git add -A` or `git add .`.

**User-owned files and tools**
- Never stage, move or delete the user's untracked files: `docs/*` other than `docs/superpowers/`, `pics/`, `session_memory.json`, `apollo2_for_aksel.wav`, `apollo2_for_aksel.mp3`, `apollo2.png`.
- Never modify anything under `C:\ghidra_12.1.3_PUBLIC`, the user's Ghidra GUI project, or `ghidra740/` (including `ghidra740/coverage/`). Only read the annotations and the dumps.
- Disassembly dumps for checking the C against the ROM (git-ignored, read-only) are in the main checkout:
  - `C:\Users\abart\Desktop\Dolphin Apollo 2 ROM\build\ghidra740\dump\rom_<ADDR>_<name>.txt`
  - `data.txt` and `functions.txt` in the same folder.

**Correctness rules**
- The existing goldens in `tests/golden/*.frm` stay byte-identical. The two new goldens, `formant_extremes.frm` and `mark_space_cap.frm`, are generated once by the rom engine in Task 1. If anything changes another golden, stop and report.
- Nothing under `src/firmware/` includes a `machine/` or `cpu740/` header. `build.bat` checks this.
- The ported C keeps the ROM's behaviour exactly, quirks included (M4.5 spec §2.6):
  - every RAM write, the scratch ones included: `$75` in `frm_clamp_mark_space`, `frm_raise_formants` and `frm_transition_flags`; `frm_level_diff`/`frm_f1_excess` shifted to 0 by the multiply; `frm_b14`; `frm_flags` built by read-modify-writes; `par_row_ptr` advanced and reset; b11 written with @M and then by each clamp in turn;
  - the 8-bit wraps and carries exactly as the ROM has them (the multiply's lost carries);
  - signed and unsigned tests exactly as the ROM's flags make them (`BMI`/`BPL` test bit 7 of the 8-bit result, not the true signed order);
  - the exit registers of M4.5 spec §2.5.
- Writes to the dead stack (below SP at exit) need not be reproduced, because `diff_lts` and `diff_play` exclude it (`frm_finish_frame`'s `PHA`s).
- ROM data is read only through `fw_rom8` at its ROM address. The producer frame is read with `fw_ind_y` and written only with `fw_wr_ind_y`, both on `isr_fill_ptr`.
- Thunk outputs: A, X and Y for all seven, each with a comment saying what the ROM callers read after the call, checked in the dumps.
- `diff_lts` and `diff_play` failures are fixed by finding the root cause in the dump. Never loosen a comparison or an assertion.

**Commits**
- End every commit message with exactly `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. This is the user's standing rule and overrides any other attribution line the environment suggests.

---

### Task 1: The frame-byte names b11–b17, `frm_data.c` and the encoder formulas, `firmware_data` at 48,496 bytes, `DIFF_MAX` 72, `diff_play`'s abort injection at `isr_par_ready`, the suite `suite_fw_frm`; the corpus texts `formant_extremes` and `mark_space_cap` and their goldens (spec §2.6, §3, §4.1, §4.2, §4.5, §4.6)

**ROM facts checked / what was verified while proving the plan** (the ROM image; `data.txt`; `ghidra740/annotations/frame.txt`, `types.txt`, `unused.txt`; `rom_C5D6_isr_par_ready.txt`, `rom_B877_par_play.txt`, `rom_B465_isr_wait_frame.txt`, `rom_B3A4_par_setup_voice.txt`, `rom_A7D4_boot_loop_play.txt`; everything below was run, nothing is copied from the spec):
- **The tables, byte for byte** (Step 7's script): `frm_product_limit` $B473 is 255 / e (truncated) for every e = 1..255 and 72 at e = 0; `frm_b14_15_lo`/`_hi` $B573/$B673 are the low and high bytes of 82 × i for all 256 i; `frm_template` $B783–$B796 is `01 20 01 20 01 20 01 20 01 3f 20 20 17 20 19 24 82 4a ff ff` (ALF, A1, A2, A3, AHF = 1; FN, F1, F2, F3 = 32; V = 63; b10 = b11 = 32; b12 $17; b13 $20; b14–b15 $19 $24; b16 $82; b17 $4A; $FF $FF), so its b16 and b17 are `frm_source_codes[0]` and `frm_filter_codes[0]`; `frm_source_codes` $BAEC = `82 58 9b ba`, `frm_filter_codes` $BAF0 = `4a 54`. `unused_B77D` ($B77D–$B782, `20 d0 41 a0 0a 84`) stays unconverted.
- **The frame-byte names** follow `types.txt`'s `frame20` (b11 `mark_space`, b12 `const17`, b13 `transitions`, b14–b15 the word `b14_15`, b16 `source`, b17 `filter`, b18–b19 `end_marker`). `FRAME_FLAGS` (13, a frame byte) and `fw_ram.h`'s `FRM_FLAGS` ($181F, RAM) are different names for different things; the C writes b13 from `FRM_FLAGS`.
- **`firmware_data`**: 47,702 + 256 × 3 + 20 + 4 + 2 = **48,496**. `k_rom` is spec §3's list: $B473–$B77C joins `frm_snapshot_map` (the formula tables end where it starts) and $B783–$B7A0 joins `par_slot_map` (the template ends where it starts); $B7A1–$B7A6 and $B77D–$B782 stay out.
- **`diff_play`'s abort injection** ($C5D6, spec §4.2). `isr_par_ready` is `PHA`, `BBC0 boot_dip`, …, `PLA`, `RTS` (A, X, Y kept); `par_play` calls it at $B89F (just before the abort test `BBC0 ser_abort` at $B8A2), $B8AB, $B8C9, $B8E6, $B8F7 and $B908. The timer interrupt sets `isr_frame_flag` bit 0 at the swap; it is cleared by `par_play`'s `LDM #0` at $B8C6, $B8EC and $B911 and by `isr_wait_frame` ($B468). After the second swap the flag is set until the frame built after it is finished and `LDM #0` clears it, so the first `isr_par_ready` entry with the flag clear is $B89F in the segment path, so the abort is seen by the same `BBC0 ser_abort` at $B8A2 as with the old trigger (which set it at the `frm_finish_frame` call $B90E, before that frame's `LDM #0`); in the tail it is $B8C9 (the old one fired at $B8C3) and in the rest frames $B8E6 (the old one never fired there), where no abort test follows in either case and both copies return with `ser_abort` bit 0 set. For a `boot_loop_play` call whose second swap happens inside `boot_play_body`'s `isr_wait_frame`, which clears the flag, the first entry is `par_play`'s first $B89F. A machine hook runs before the instruction and after a pending interrupt has been taken, so the condition reads the flag as the instruction would. The point is the same program point in both copies (in copy B a ROM call from the C `fw_par_ready`, as the old one was a ROM call to `frm_finish_frame`). Proved while the frame builder is still ROM: on the 45 texts (before Step 11) `diff_play` prints exactly the lines it printed before this task (`boot_loop_play 175 calls checked, 46 with both injections`, `par_play 124 …`, `$B8A5 (abort) reached in 92 calls, $B89C (reply due) in 92`), 0 mismatches, the self-check unchanged.
- **The corpus texts** (spec §4.5), measured with a branch probe (a scratch tool built against the repo sources, not part of the plan: a machine hook on each of the 32 conditional branches of the seven routines evaluates the branch from the flags before it runs; rom engine, boot + the given text) and with `apollo2 -o FILE` (plain and `--hyp b11`) for clipping:

  | Text | $B9F4 (e = 0) | $B9F6 `BMI` (e < 0) | $BA02 `BMI` (limit) | $BA0B `BCC` (multiplier bit) | peak plain / b11 |
  |---|---|---|---|---|---|
  | first choice `@u120+ @u410+ @u510- Hello world.` | taken 0, fall 69 | taken 0, fall 69 | taken 53, fall 16 | taken 96, fall 32 | 32767 / 32768: 106 / 427 samples clipped |
  | `@u120+ @u510- Hello world.` | 0, 72 | 0, 72 | 9, 63 | 367, 137 | 11046 / 10519 |
  | **`@u110+ @u510- Hello world.`** (`formant_extremes`) | **3, 69** | **39, 30** | **14, 16** | **104, 24** | **12277 / 10858** |
  | `@M3F @R7 @FF Take a cake.` (`mark_space_cap`) | — | — | — | — | $B943 `BCS`: taken 92, fall 115 (the cap written at $B945) |

  **The spec's first `formant_extremes` text could not be used** (the spec now records the replacement, a user decision): its golden makes `suite_synth` fail (`plain formant_extremes: 61 samples clipped`, `b11 formant_extremes: 403 samples clipped`, `corpus peak plain 32767 (0.0 dBFS)`, above the −1 dBFS limit; ALF + 16 drives the synthesizer into clipping), and the calibration must not change. `@u110+ @u510-` (F1 + 16, A1 − 16) reaches everything the spec's text reaches (the multiply, the saturated index $FF with $BA02 taken) and more ($B9F4, F1 = 33 exactly, and $B9F6 both ways), without clipping (the corpus peaks stay `20522 (-4.1 dBFS)` and `16038 (-6.2 dBFS)`). The name stays `formant_extremes`.
- **Goldens**: `formant_extremes.frm` 105 frames and `mark_space_cap.frm` 125 frames from the rom engine; the update run rewrote 48 goldens and `git status`/`git diff --stat tests/golden` showed only the two new files.
- **Coverage**: `apollo2_coverage` on boot + the 47 texts executes all 230 instructions of the seven routines ($B945 and $B9F8–$BA26 included, which the 45 texts miss).

**Files:**
- Create: `src/firmware/data/frm_data.c`, `tests/test_fw_frm.c`, `tests/corpus/formant_extremes.txt`, `tests/corpus/mark_space_cap.txt`, `tests/golden/formant_extremes.frm` and `tests/golden/mark_space_cap.frm` (generated)
- Modify: `src/firmware/data/fw_data.h`, `src/firmware/data/fw_data.c`, `tests/test_firmware_data.c`, `tests/suites.h`, `tests/test_diff_lts.c`, `tests/test_diff_play.c`, `tests/corpus_list.h`

**Interfaces:**
- Consumes: `put`, `fail`, `check_end` and the step layout of `fw_image_build` (`src/firmware/data/fw_data.c`); `FRAME_*`, `FRM_SNAPSHOT_MAP`, `PAR_SLOT_MAP`, `PAR_TARGET_BIAS` (`fw_data.h`); `fw_rom8`, `fw_init`, `fw_host` (`fw.h`); `ROM_ISR_PAR_READY` (`src/firmware/tn_convert.h`); `ISR_FRAME_FLAG`, `SER_ABORT` (`fw_ram.h`); `machine_hook` (`machine.h`); `CORPUS_NAMES` (`tests/corpus_list.h`).
- Produces (Tasks 2 and 3 use them):

```c
/* src/firmware/data/fw_data.h */
#define FRM_PRODUCT_LIMIT   0xB473   /* byte[256], 255 / e */
#define FRM_B14_15_LO       0xB573   /* byte[256], low byte of 82 * i */
#define FRM_B14_15_HI       0xB673   /* byte[256], high byte */
#define FRM_TEMPLATE        0xB783   /* byte[FRAME_SIZE] */
#define FRM_SOURCE_CODES    0xBAEC   /* byte[4] */
#define FRM_FILTER_CODES    0xBAF0   /* byte[2] */
#define FRM_PRODUCT_LIMIT_0 72
#define FRM_B14_15_STEP     82
#define FRAME_MARK_SPACE 11 ... FRAME_FILTER 17, FRAME_END_MARKER 18, FRAME_SIZE 20
extern const uint8_t fw_frm_template[FRAME_SIZE], fw_frm_source_codes[4], fw_frm_filter_codes[2];
/* tests/test_fw_frm.c */
void suite_fw_frm(void);   /* 34 suites; setup(), g_ram, g_f, g_ncalls for Tasks 2 and 3 */
```

  and `DIFF_MAX` 72 (71 replacements after Task 3; `diff_lts` observes the 69 not marked `waits`); the corpus names `"formant_extremes"` and `"mark_space_cap"` (47 names; `golden_hybrid` 48 files with boot).

No name collides: `grep -rn "FRM_PRODUCT_LIMIT\|FRM_B14_15\|FRM_TEMPLATE\|FRM_SOURCE_CODES\|FRM_FILTER_CODES\|FRAME_MARK_SPACE\|FRAME_SIZE\|FRAME_END_MARKER\|FRAME_SOURCE\|FRAME_FILTER\|fw_frm_template\|suite_fw_frm\|formant_extremes\|mark_space_cap" src tests` prints nothing before this task. (`FRAME_FLAGS` 13, the frame byte, is new; `FRM_FLAGS` $181F, the RAM byte of `fw_ram.h`, is another name.)

- [ ] **Step 1: Write the failing tests**

**`tests/test_firmware_data.c`.** Replace

```c
static const range k_rom[] = { { 0x4000, 0x8FF4 }, { 0x9804, 0xA397 }, { 0xACC7, 0xACE8 }, { 0xB773, 0xB77D },
                               { 0xB797, 0xB7A1 }, { 0xB7A7, 0xB7B1 }, { 0xC67B, 0xC9B8 }, { 0xD44A, 0xF77B } };
```

with

```c
static const range k_rom[] = { { 0x4000, 0x8FF4 }, { 0x9804, 0xA397 }, { 0xACC7, 0xACE8 }, { 0xB473, 0xB77D },
                               { 0xB783, 0xB7A1 }, { 0xB7A7, 0xB7B1 }, { 0xBAEC, 0xBAF2 }, { 0xC67B, 0xC9B8 },
                               { 0xD44A, 0xF77B } };
```

Replace

```c
    CHECK_EQ(fw_par_slot_map[9], FRAME_V);                   /* P10 */
```

with

```c
    CHECK_EQ(fw_par_slot_map[9], FRAME_V);                   /* P10 */
    CHECK_EQ(fw_frm_template[FRAME_SOURCE], fw_frm_source_codes[0]);   /* the rest frame's b16: the default source */
    CHECK_EQ(fw_frm_template[FRAME_FILTER], fw_frm_filter_codes[0]);   /* and b17: the male filter */
    CHECK_EQ(fw_frm_template[FRAME_END_MARKER], 0xFF);
    CHECK_EQ(fw_frm_template[FRAME_END_MARKER + 1], 0xFF);
```

Replace

```c
    CHECK_EQ(compared, 47702);   /* M4.0's 35,214, M4.1's phonetic texts (97), M4.2's command data (567),
                                    M4.3a's prosody data (2,319), M4.4a's playback data (9,495) and M4.4b's
                                    par_slot_map (10) */
```

with

```c
    CHECK_EQ(compared, 48496);   /* M4.0's 35,214, M4.1's phonetic texts (97), M4.2's command data (567),
                                    M4.3a's prosody data (2,319), M4.4a's playback data (9,495), M4.4b's
                                    par_slot_map (10) and M4.5's frame builder data (794) */
```

**Create `tests/test_fw_frm.c`** (the suite Tasks 2 and 3 extend; its mock fails every ROM call, since the frame builder makes none):

```c
/* Unit checks of the frame builder (M4.5 spec 4.6) on the converted data, with a mock host that fails every ROM
   call: the frame builder makes none. Each expected value is worked out from the dump of the routine
   (build/ghidra740/dump), as the comment beside it says. The full proof is the live differential tests (diff_lts,
   diff_play) and the hybrid goldens (golden_hybrid), which compare the thunks' register outputs against the ROM. */
#include <string.h>
#include "test.h"
#include "firmware/fw.h"
#include "firmware/data/fw_data.h"

static uint8_t g_ram[0x10000];   /* the firmware's RAM is $0000-$3FFF; 64 KiB so no index can run off the array */
static fw g_f;
static fw_host g_host;
static unsigned g_ncalls;

/* Counts and fails every ROM call. */
static fw_call_result mock_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    (void)ctx;
    (void)addr;
    (void)r;
    (void)budget;
    g_ncalls++;
    return FW_CALL_FAILED;
}

/* Zeroed RAM, the mock host, the converted image, no calls made. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    g_host.ctx = &g_f;
    g_host.rom_call = mock_rom_call;
    g_host.set_low_window = NULL;   /* the frame builder does not drive P2 */
    g_host.read_sfr = NULL;         /* nor read an SFR */
    g_ncalls = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
}

/* The frame builder's tables as the C reads them, through fw_rom8 at their ROM addresses (M4.5 spec 3): the
   template by byte name, both code tables, and the two formula tables at their edges. */
static void test_data(void)
{
    setup();
    static const uint8_t k_template[FRAME_SIZE] = {
        1, 32, 1, 32, 1, 32, 1, 32, 1, 63,   /* ALF FN A1 F1 A2 F2 A3 F3 AHF V */
        32, 32, 0x17, 0x20, 0x19, 0x24,      /* pitch code, b11, b12, b13, b14, b15 */
        0x82, 0x4A, 0xFF, 0xFF,              /* b16 source, b17 filter, the end marker */
    };
    for (unsigned b = 0; b < FRAME_SIZE; b++)
        CHECK_EQ(fw_rom8(&g_f, (uint16_t)(FRM_TEMPLATE + b)), k_template[b]);
    CHECK_EQ(fw_rom8(&g_f, FRM_SOURCE_CODES), 0x82);       /* default */
    CHECK_EQ(fw_rom8(&g_f, FRM_SOURCE_CODES + 3), 0xBA);   /* reduced HF */
    CHECK_EQ(fw_rom8(&g_f, FRM_FILTER_CODES), 0x4A);       /* male */
    CHECK_EQ(fw_rom8(&g_f, FRM_FILTER_CODES + 1), 0x54);   /* female */
    CHECK_EQ(fw_rom8(&g_f, FRM_PRODUCT_LIMIT), FRM_PRODUCT_LIMIT_0);
    CHECK_EQ(fw_rom8(&g_f, FRM_PRODUCT_LIMIT + 1), 255);   /* e = 1: 255, negative as a signed byte */
    CHECK_EQ(fw_rom8(&g_f, FRM_PRODUCT_LIMIT + 2), 127);
    CHECK_EQ(fw_rom8(&g_f, FRM_PRODUCT_LIMIT + 30), 8);    /* the largest e the frame can give (F1 63 - 33) */
    CHECK_EQ(fw_rom8(&g_f, FRM_PRODUCT_LIMIT + 255), 1);
    CHECK_EQ(fw_rom8(&g_f, FRM_B14_15_LO + 1), 82);        /* 82 * 1 */
    CHECK_EQ(fw_rom8(&g_f, FRM_B14_15_HI + 1), 0);
    CHECK_EQ(fw_rom8(&g_f, FRM_B14_15_LO + 4), 0x48);      /* 82 * 4 = 328 = $0148 */
    CHECK_EQ(fw_rom8(&g_f, FRM_B14_15_HI + 4), 0x01);
    CHECK_EQ(fw_rom8(&g_f, FRM_B14_15_LO + 255), 0xAE);    /* 82 * 255 = 20910 = $51AE */
    CHECK_EQ(fw_rom8(&g_f, FRM_B14_15_HI + 255), 0x51);
    CHECK(!g_f.fault);
    CHECK_EQ(g_ncalls, 0);
}

void suite_fw_frm(void)
{
    CHECK(fw_image_get() != NULL);
    test_data();
}
```

**`tests/suites.h`.** Replace the last line

```c
SUITE(suite_fw_par)
```

with

```c
SUITE(suite_fw_par)
SUITE(suite_fw_frm)
```

(`build.bat` compiles `tests\*.c` and `src\firmware\data\*.c` by wildcard: it needs no change.)

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw_frm"`
Expected: the build stops with compile errors (the proof run, first lines):

```
tests\test_firmware_data.c(97): error C2065: 'fw_frm_template': undeclared identifier
tests\test_firmware_data.c(97): error C2065: 'FRAME_SOURCE': undeclared identifier
tests\test_firmware_data.c(97): error C2109: subscript requires array or pointer type
tests\test_firmware_data.c(97): error C2065: 'fw_frm_source_codes': undeclared identifier
...
tests\test_fw_frm.c(43): error C2065: 'FRAME_SIZE': undeclared identifier
tests\test_fw_frm.c(43): error C2057: expected constant expression
...
tests\test_fw_frm.c(64): error C2065: 'FRM_B14_15_HI': undeclared identifier
```

- [ ] **Step 3: The frame-byte names and the pinned addresses (`fw_data.h`)**

`src/firmware/data/fw_data.h`: replace

```c
#define PAR_TARGET_BIAS     0xB7A7   /* byte[10]: per parameter P1..P10 */
```

with

```c
#define PAR_TARGET_BIAS     0xB7A7   /* byte[10]: per parameter P1..P10 */

/* Pinned addresses of the frame builder's data (ghidra740/annotations: frame.txt; M4.5 spec 3) */
#define FRM_PRODUCT_LIMIT   0xB473   /* byte[256]: per F1 excess e, 255 / e (a formula the encoder expands) */
#define FRM_B14_15_LO       0xB573   /* byte[256]: per product index i, the low byte of FRM_B14_15_STEP * i */
#define FRM_B14_15_HI       0xB673   /* byte[256]: its high byte */
#define FRM_TEMPLATE        0xB783   /* byte[FRAME_SIZE]: the rest-frame template (read as $B782,Y, Y = 1-20) */
#define FRM_SOURCE_CODES    0xBAEC   /* byte[4]: b16 per frm_source_index */
#define FRM_FILTER_CODES    0xBAF0   /* byte[2]: b17 per frm_filter_index */
#define FRM_PRODUCT_LIMIT_0 72       /* frm_product_limit[0], which nothing reads (e >= 1 where it is indexed) */
#define FRM_B14_15_STEP     82       /* the b14-b15 word per product index (parent spec 1.4, rev 6) */
```

replace

```c
/* The bytes of a DSP frame (frame20: ghidra740/annotations/types.txt; parent spec 1.4): the values of
   frm_snapshot_map and par_slot_map */
```

with

```c
/* The bytes of a DSP frame (frame20: ghidra740/annotations/types.txt; parent spec 1.4): the values of
   frm_snapshot_map and par_slot_map, and the bytes the frame builder writes (M4.5) */
```

replace

```c
#define FRAME_PITCH     10   /* b10 pitch code */
```

with

```c
#define FRAME_PITCH     10   /* b10 pitch code */
#define FRAME_MARK_SPACE 11  /* b11 mark-space ratio (@M) */
#define FRAME_B12       12   /* b12, always $17 */
#define FRAME_FLAGS     13   /* b13 transition flags */
#define FRAME_B14       14   /* b14-b15: a little-endian word (frm_b14_15_lo/hi) */
#define FRAME_B15       15
#define FRAME_SOURCE    16   /* b16 voicing source (@$) */
#define FRAME_FILTER    17   /* b17 voice filter (@$) */
#define FRAME_END_MARKER 18  /* b18-b19, always $FF $FF */
#define FRAME_SIZE      20
```

and replace

```c
extern const uint8_t fw_par_slot_map[10];
```

with

```c
extern const uint8_t fw_par_slot_map[10];
extern const uint8_t fw_frm_template[FRAME_SIZE], fw_frm_source_codes[4], fw_frm_filter_codes[2];
```

- [ ] **Step 4: The three small tables (`frm_data.c`)**

Create `src/firmware/data/frm_data.c`:

```c
/* Converted from the Apollo 2 ROM (M4.5 spec section 3), then reviewed: the frame builder's three small tables.
   Its two formula tables (frm_product_limit, frm_b14_15_lo/hi) are expanded by the encoder (fw_data.c, step 10).
   tests/test_firmware_data.c re-encodes it and compares it byte for byte with the ROM. */
#include <stddef.h>
#include "fw_data.h"

/* frm_template ($B783, read as $B782,Y with Y = 20 down to 1 by frm_build_rest_frame): the rest frame before the
   pause phoneme's FN, F1, F2, F3 are put over it. */
const uint8_t fw_frm_template[FRAME_SIZE] = {
    [FRAME_ALF] = 1, [FRAME_A1] = 1, [FRAME_A2] = 1, [FRAME_A3] = 1, [FRAME_AHF] = 1,   /* the silent value */
    [FRAME_FN] = 32, [FRAME_F1] = 32, [FRAME_F2] = 32, [FRAME_F3] = 32,                  /* mid-range */
    [FRAME_V] = 63,                         /* full voicing */
    [FRAME_PITCH] = 32,
    [FRAME_MARK_SPACE] = 32,
    [FRAME_B12] = 0x17,                     /* the constant b12 */
    [FRAME_FLAGS] = 0x20,                   /* bit 5, which the firmware never computes (parent spec 1.4) */
    [FRAME_B14] = 0x19, [FRAME_B15] = 0x24,
    [FRAME_SOURCE] = 0x82,                  /* the default source, frm_source_codes[0] */
    [FRAME_FILTER] = 0x4A,                  /* the male filter, frm_filter_codes[0] */
    [FRAME_END_MARKER] = 0xFF, [FRAME_END_MARKER + 1] = 0xFF,
};

/* frm_source_codes ($BAEC): b16 per frm_source_index (@$ bits 1-2; frm_finish_frame). */
const uint8_t fw_frm_source_codes[4] = {
    0x82,   /* 0: the default source */
    0x58,   /* 1: spike */
    0x9B,   /* 2: cut-down */
    0xBA,   /* 3: reduced HF */
};

/* frm_filter_codes ($BAF0): b17 per frm_filter_index (@$ bit 0; frm_finish_frame). */
const uint8_t fw_frm_filter_codes[2] = {
    0x4A,   /* 0: male */
    0x54,   /* 1: female */
};
```

- [ ] **Step 5: The encoder (`fw_data.c`)**

`src/firmware/data/fw_data.c`: replace the end of `fw_image_build`

```c
        check_end(&e, "cmd_sound_table", CMD_SOUND_TABLE + 8 * CMD_SOUND_EFFECTS, CMD_SOUND_DATA);
    }
    return e.ok;
}
```

with

```c
        check_end(&e, "cmd_sound_table", CMD_SOUND_TABLE + 8 * CMD_SOUND_EFFECTS, CMD_SOUND_DATA);
    }
    /* 10. The frame builder's data (M4.5 spec 3): two tables expanded from their formulas, frm_product_limit[e] =
       255 / e (truncated), the largest level difference d whose product d * e fits a byte (entry 0, which nothing
       reads, is FRM_PRODUCT_LIMIT_0), and frm_b14_15_lo/hi[i] = the low and the high byte of the b14-b15 word
       FRM_B14_15_STEP * i, ending where frm_snapshot_map starts; frm_template, ending where par_slot_map starts;
       frm_source_codes and frm_filter_codes, back to back. */
    {
        for (uint32_t i = 0; i < 256; i++) {
            put(&e, false, FRM_PRODUCT_LIMIT + i, (uint8_t)(i ? 255 / i : FRM_PRODUCT_LIMIT_0));
            uint16_t w = (uint16_t)(FRM_B14_15_STEP * i);
            put(&e, false, FRM_B14_15_LO + i, (uint8_t)w);
            put(&e, false, FRM_B14_15_HI + i, (uint8_t)(w >> 8));
        }
        check_end(&e, "frm_b14_15_hi", FRM_B14_15_HI + 256, FRM_SNAPSHOT_MAP);
        for (uint32_t b = 0; b < FRAME_SIZE; b++)
            put(&e, false, FRM_TEMPLATE + b, fw_frm_template[b]);
        check_end(&e, "frm_template", FRM_TEMPLATE + FRAME_SIZE, PAR_SLOT_MAP);
        for (uint32_t k = 0; k < 4; k++)
            put(&e, false, FRM_SOURCE_CODES + k, fw_frm_source_codes[k]);
        check_end(&e, "frm_source_codes", FRM_SOURCE_CODES + 4, FRM_FILTER_CODES);
        for (uint32_t k = 0; k < 2; k++)
            put(&e, false, FRM_FILTER_CODES + k, fw_frm_filter_codes[k]);
    }
    return e.ok;
}
```

The `check_end` calls pin the layout: `frm_b14_15_hi` ends at `frm_snapshot_map` ($B773), `frm_template` at `par_slot_map` ($B797), `frm_source_codes` at `frm_filter_codes` ($BAF0). No byte is written twice (`put` fails on that): $B773–$B77C and $B797–$B7A0 were converted before and stay outside these ranges.

- [ ] **Step 6: Run the tests**

Run: `cmd //c ".\build.bat test fw_frm"`, then `build/apollo2_tests.exe firmware_data`.
Expected: the build passes with no warnings (the firmware syntax check included); then

```
suite_fw_frm                 ok
1 suite(s), 0 failure(s), 0 skip(s)
```

```
  48496 converted bytes compared with the ROM, 0 differ
suite_firmware_data          ok
1 suite(s), 0 failure(s), 0 skip(s)
```

- [ ] **Step 7: Faithfulness check of the data**

Run (from the repo root, with `APOLLO2_ROM` set):

```bash
python -c "
import os
d=open(os.environ['APOLLO2_ROM'],'rb').read()
print('limit', d[0xB473], all(d[0xB473+e]==255//e for e in range(1,256)))
print('word', all(d[0xB573+i]==(82*i)&255 and d[0xB673+i]==(82*i)>>8 for i in range(256)))
print('template', d[0xB783:0xB797].hex(' '))
print('codes', d[0xBAEC:0xBAF2].hex(' '))
"
```

Expected:

```
limit 72 True
word True
template 01 20 01 20 01 20 01 20 01 3f 20 20 17 20 19 24 82 4a ff ff
codes 82 58 9b ba 4a 54
```

Compare with `frm_data.c` (b0–b19 by name) and the encoder's formulas; `firmware_data` compares all 794 new bytes with the ROM (Step 6).

- [ ] **Step 8: `DIFF_MAX` 72 and `diff_play`'s abort injection at `isr_par_ready`**

`tests/test_diff_lts.c`: replace

```c
#define DIFF_MAX        64
```

with

```c
#define DIFF_MAX        72
```

`tests/test_diff_play.c`: replace

```c
   set at the first frm_finish_frame ($B9B5) after the second swap, so that par_play's next abort test ($B8A2) ends
   it (the end path from $B8A5). Not at the swap itself: there the ROM sees the mute before or after the frame flag
   depending on the instruction the interrupt lands on (at $B8A8, between the two tests, it builds one more frame),
   which the C, taking no time between its tests, cannot reproduce. A hook on copy A counts the calls that executed
```

with

```c
   set at the first isr_par_ready entry ($C5D6, ROM in both copies) after the second swap at which isr_frame_flag
   bit 0 is clear: the frame built after that swap has been finished and the flag cleared, so that par_play's next
   abort test ($B8A2, right after its JSR isr_par_ready at $B89F) ends it (the end path from $B8A5). Not at the swap
   itself: there the ROM sees the mute before or after the frame flag depending on the instruction the interrupt
   lands on (at $B8A8, between the two tests, it builds one more frame), which the C, taking no time between its
   tests, cannot reproduce. Nor at frm_finish_frame ($B9B5), the point used until M4.5: once par_play calls the C
   frame builder (M4.5), its C copy never executes $B9B5. A hook on copy A counts the calls that executed
```

replace

```c
#include "firmware/par_addr.h"
```

with (`ROM_ISR_PAR_READY` is defined there)

```c
#include "firmware/par_addr.h"
#include "firmware/tn_convert.h"
```

replace

```c
    bool abort_due;    /* the abort injection waits for the next frm_finish_frame */
```

with

```c
    bool abort_due;    /* the abort injection waits for the next isr_par_ready with isr_frame_flag bit 0 clear */
```

replace

```c
/* $B9B5, frm_finish_frame (ROM in both copies): the abort injection, at the first frame finished after the second
   swap. */
static bool inject_abort(machine *m, void *user)
{
    recorder *rec = user;
    if (rec->abort_due) {
```

with

```c
/* $C5D6, isr_par_ready (ROM in both copies): the abort injection, at its first entry after the second swap with
   isr_frame_flag bit 0 clear (the frame built after the swap is finished). */
static bool inject_abort(machine *m, void *user)
{
    recorder *rec = user;
    if (rec->abort_due && !(m->ram[ISR_FRAME_FLAG] & 1)) {
```

and in `check_call` replace

```c
    CHECK(machine_hook(&g_a, ROM_FRM_FINISH_FRAME, inject_abort, &ra));
```

with

```c
    CHECK(machine_hook(&g_a, ROM_ISR_PAR_READY, inject_abort, &ra));
```

and

```c
    CHECK(machine_hook(&g_b, ROM_FRM_FINISH_FRAME, inject_abort, &rb));
```

with

```c
    CHECK(machine_hook(&g_b, ROM_ISR_PAR_READY, inject_abort, &rb));
```

- [ ] **Step 9: Run the differential tests**

Run: `cmd //c ".\build.bat test diff"` (the filter matches `diff_lts` and `diff_play`)
Expected:

- `suite_diff_lts`: the 62 lines `<name> N calls checked`, each identical to the state before this task (the proof run diffed them: `cmd_apply_speaker 103`, `cmd_run_due 5501`, `par_setup_voice 115`, `par_step_tracks 18342`, `par_scale_duration 26825`, …), then `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`, 0 mismatches;
- `suite_diff_play`, also exactly as before this task:

```
  self-check (ROM against ROM, hello): par_play 1 calls, boot_loop_play 2 calls, 2 injected; $B8A5 reached 2, $B89C reached 2
  2 routines wait on the timer interrupt
  boot_loop_play       175 calls checked, 46 with both injections
  par_play             124 calls checked, 46 with both injections
  $B8A5 (abort) reached in 92 calls, $B89C (reply due) in 92
```

- `2 suite(s), 0 failure(s), 0 skip(s)` (68 s in the proof run).

The moved injection must give exactly these counts, 92 above all: every injected run still ends at $B8A5 ("ROM facts checked"). If a count differs, find the cause in the dump and the trace (which call, which swap, where the flag was); never loosen the comparison.

- [ ] **Step 10: Commit**

```bash
git add src/firmware/data/fw_data.h src/firmware/data/fw_data.c src/firmware/data/frm_data.c tests/test_firmware_data.c tests/test_fw_frm.c tests/suites.h tests/test_diff_lts.c tests/test_diff_play.c
git commit -m "M4.5 data and plumbing: the frame-byte names b11-b17, frm_template, frm_source_codes and frm_filter_codes in frm_data.c, frm_product_limit and frm_b14_15_lo/hi as formulas the encoder expands (48,496 bytes), DIFF_MAX 72, diff_play's abort injection at isr_par_ready, the suite suite_fw_frm (M4.5 2.6, 3, 4.1, 4.2, 4.6)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 11: Write the corpus texts**

One line each, ending with a single LF, written by `printf` so the bytes are exact:

```bash
printf '@u110+ @u510- Hello world.\n' > tests/corpus/formant_extremes.txt
printf '@M3F @R7 @FF Take a cake.\n' > tests/corpus/mark_space_cap.txt
wc -c tests/corpus/formant_extremes.txt tests/corpus/mark_space_cap.txt
```

Expected `wc -c`: `27 tests/corpus/formant_extremes.txt`, `26 tests/corpus/mark_space_cap.txt`, `53 total`. `formant_extremes` is @u1 +$10 (F1 + 16) and @u5 −$10 (A1 − 16), not the spec's first choice, `@u120+ @u410+ @u510-`: see "ROM facts checked" (the spec's text clips the synthesizer; this one reaches every path the spec's does, and F1 = 33 too).

- [ ] **Step 12: Add them to the corpus list**

In `tests/corpus_list.h`, replace

```c
   The last one (M4.4b) is seg_256 two words longer, with a @B5 before its last word: an event at segment 259
   ($0103), whose low byte par_play meets first on page 0 (the high-byte test at $B88D). */
```

with

```c
   The next one (M4.4b) is seg_256 two words longer, with a @B5 before its last word: an event at segment 259
   ($0103), whose low byte par_play meets first on page 0 (the high-byte test at $B88D).
   The last two (M4.5) reach the frame builder's rare paths: @u offsets that raise F1 by 16 and lower A1 by 16, so
   that F1 >= 33 and ALF - A1 > 4 in the same frame (the b14-b15 product, its saturated index $FF, and F1 = 33);
   and @M3F under @R7 @FF, a mark-space ratio above the cap 160 - 2 x b10 at the highest pitch codes. */
```

and replace the last line of `CORPUS_NAMES`

```c
    "late_event"
```

with

```c
    "late_event", \
    "formant_extremes", "mark_space_cap"
```

- [ ] **Step 13: Run the golden suites to see them fail**

Run: `cmd //c ".\build.bat test golden"`
Expected: FAIL, `2 suite(s), 132 failure(s), 0 skip(s)`:
- `suite_golden` prints `missing tests/golden/formant_extremes.frm: run once with APOLLO2_UPDATE_GOLDEN=1 and review` and the same for `mark_space_cap.frm` (2 failures);
- `suite_golden_hybrid` prints `48 corpus files x 65 modes (all, then each routine alone)` and fails `tests\test_golden_hybrid.c:36: CHECK failed: want != NULL` 130 times (2 files × 65 modes).

- [ ] **Step 14: Generate the goldens with the rom engine**

Run: `APOLLO2_UPDATE_GOLDEN=1 cmd //c ".\build.bat test golden"`
Expected: `2 suite(s), 0 failure(s), 0 skip(s)`, 48 `updated` lines (update mode rewrites every golden), among them

```
  updated tests/golden/formant_extremes.frm (105 frames)
  updated tests/golden/mark_space_cap.frm (125 frames)
```

Then run `git status --short tests/golden` and `git diff --stat tests/golden`. Expected: exactly `?? tests/golden/formant_extremes.frm` and `?? tests/golden/mark_space_cap.frm` from the first, nothing from the second. If any existing `.frm` shows as modified, STOP and report: nothing may be committed. If a frame count differs, STOP and check the text against Step 11 byte for byte.

- [ ] **Step 15: Confirm the coverage**

Run (from the repo root; the file goes under the git-ignored `build/`):

```bash
./build/apollo2_coverage.exe -o build/m45.cov
D="C:/Users/abart/Desktop/Dolphin Apollo 2 ROM/build/ghidra740/dump"
for r in B41D B445 B917 B930 B95D B9B5 BA3F; do
  for a in $(sed -n '1,/decompiled/p' "$D"/rom_${r}_*.txt | grep -oE "^  [0-9A-F]{4} " | tr -d ' '); do
    grep -q "^x rom $a$" build/m45.cov || echo "$a MISSING"
  done
done | wc -l
```

Expected: the summary `rom 4106, low 0, ramcode 116, ram 0 instruction addresses; 31 indirect edges` (M4.4b recorded 4,084 on the 45 texts: the two texts add 22, among them $B945 and $B9F8–$BA26), then `0`: no instruction of the seven routines (230, read from their dumps) is missing. The coverage file has no branch directions; Task 3's branch probe checks the edges.

- [ ] **Step 16: Run the full suite**

Run: `cmd //c ".\build.bat test"`
Expected:

- `34 suite(s), 0 failure(s), 0 skip(s)`, no warnings, the firmware syntax check passing (436 s in the proof run);
- `firmware_data`: `48496 converted bytes compared with the ROM, 0 differ`;
- `golden_hybrid`: `48 corpus files x 65 modes (all, then each routine alone)`;
- `suite_golden`: `hybrid timer scale 1/4: 105 frames, 11701952 CPU cycles`, `hybrid timer scale 3/1: 105 frames, 999330 CPU cycles` (unchanged: the frame builder is still ROM);
- `diff_lts`: 62 `N calls checked` lines, each N > 0 and 0 mismatches; `1 mid-clause flush calls skipped (golden_hybrid covers them)` and `2 unwound calls compared`. The counts grow with the two texts (the proof run, before -> after: cmd_apply_speaker 103 -> 107, cmd_run_due 5501 -> 5595, cmd_scan_clause 228 -> 234, cmd_match_table 2957 -> 3084, lts_affix_next 4512 -> 4557, lts_sym_vowel 1241 -> 1275, lts_match_symbol 16161 -> 16402, lts_match_list 12144 -> 12302, tn_rule_skip_text 32498 -> 33176, tn_rule_skip_field 14361 -> 14561, tn_append_phonetic 966 -> 978, lts_convert_char 957 -> 969, tn_convert_clause 227 -> 233, par_setup_voice 115 -> 121, pros_microprosody 3666 -> 3710, par_play_start 176 -> 180, par_reset_tracks 176 -> 180, cmd_run_event 119 -> 123, cmd_ev_sound_start 53 -> 55, cmd_ev_sound_end 229 -> 235, par_next_segment 3666 -> 3710, par_next_row 41827 -> 42889, par_next_param 31717 -> 32411, par_clamp 40771 -> 41888, par_read_segment 3666 -> 3710, pros_scale_target 3842 -> 3890, par_boundary_pass 3666 -> 3710, par_boundary_jump 1181 -> 1204, par_store_boundary 24474 -> 24914, par_boundary_cur_dom 906 -> 912, par_boundary_next_dom 1579 -> 1594, par_read_dominance 16996 -> 17206, par_scale_voicing 6151 -> 6216, par_boundary_value 16996 -> 17206, par_plan_tracks 3666 -> 3710, par_step_tracks 18342 -> 18626, par_track_sequential 49029 -> 51042, util_interp_step 41887 -> 43004, par_track_overlap 9583 -> 9666, par_track_start 9583 -> 9666, par_track_end 9583 -> 9666, par_scale_duration 26825 -> 27245, cmd_hex_value 147 -> 158, util_shl4 244 -> 259, util_shl3 1051 -> 1086, ph_append_phrase_pause 364 -> 372, ph_append_pause 921 -> 933, pros_append_segment 2477 -> 2503, pros_reset_seg_ptrs 404 -> 414, pros_make_segments 228 -> 234, pros_mark_stress 228 -> 234, pros_mark_phrases 228 -> 234, pros_store_phrase_len 412 -> 422, pros_phrase_gen 228 -> 234, pros_declination 412 -> 422, pros_declination_step 412 -> 422, ph_lengthen_count 1253 -> 1269; `lts_affix_prev`, `tn_say_char`, `cmd_ev_index`, `cmd_ev_set_voicing` and `cmd_ev_set_volume` stay the same);
- `diff_play`: `boot_loop_play 179 calls checked, 48 with both injections`, `par_play 126 calls checked, 48 with both injections`, `$B8A5 (abort) reached in 96 calls, $B89C (reply due) in 96` (92 + 4: the two new texts give one injected run of each routine each, and each reaches $B8A5), 0 mismatches;
- `suite_synth`: `corpus peak plain 20522 (-4.1 dBFS)`, `corpus peak b11 16038 (-6.2 dBFS)` (unchanged). If it reports clipping, or a plain peak outside -12...-1 dBFS, STOP and report. Do not change any calibration.

- [ ] **Step 17: Commit**

```bash
git add tests/corpus/formant_extremes.txt tests/corpus/mark_space_cap.txt tests/golden/formant_extremes.frm tests/golden/mark_space_cap.frm tests/corpus_list.h
git commit -m "Corpus: formant_extremes (@u F1 + 16, A1 - 16: the b14-b15 product, its saturated index and F1 = 33) and mark_space_cap (@M3F @R7 @FF: the upper mark-space clamp); goldens from the rom engine

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: The leaves in C: `frm_build_rest_frame` $B41D, `frm_copy_rest_frame` $B445, `frm_snapshot` $B917, `frm_clamp_mark_space` $B930 and `frm_raise_formants` $B95D, with their hooks; `par_setup_voice` calls them directly (spec §2.2–§2.6, §4.6)

**ROM facts checked** (`rom_B41D_frm_build_rest_frame.txt`, `rom_B445_frm_copy_rest_frame.txt`, `rom_B917_frm_snapshot.txt`, `rom_B930_frm_clamp_mark_space.txt`, `rom_B95D_frm_raise_formants.txt`, each instruction by instruction; the callees `rom_BD14_par_next_param.txt`, `rom_BD24_par_clamp.txt`; the callers `rom_B3A4_par_setup_voice.txt` ($B3F5–$B41C), `rom_B877_par_play.txt` ($B8D2–$B8FA), `rom_B9B5_frm_finish_frame.txt` ($B9B5–$B9C8); the ROM image for the operand scans):
- **Sizes agree with spec §2.2**: 40/19, 11/6, 25/12, 45/25 and 38/22 bytes/instructions. The byte before each entry is an `RTS` ($60), except $B916 before `frm_snapshot`, the last byte of `JMP par_play` ($B914): no fall-through into any of them. Callers (Step 4's scans): `frm_build_rest_frame` only `JSR` $B404; `frm_copy_rest_frame` $B407, $B416, $B8DA; `frm_snapshot` $B8DD, $B9BA; `frm_clamp_mark_space` $B40A, $B419, $B8E0, $B9BD; `frm_raise_formants` $B9C0. No relative branch reaches any entry; the one `JMP $B917` "at $C0D7" is the operand byte $4C of `STA $174C,Y` at $C0D6 (M4.4b's scan found it too).
- **Exit registers (spec §2.5 confirmed):**
  - `frm_build_rest_frame`: the copy loop `LDA $B782,Y` / `STA $1FFF,Y` for Y = 20..1 leaves Y = 0; then `LDY #$3B`, `LDX #1`, and four passes of `LDA ($32),Y` / `STA $2000,X` / `JSR par_next_param` / `INX` / `INX` / `CPX #8` / `BCC`: X = 9, Y = $3B; `LDA par_params_ptr` / `STA par_row_ptr` / `LDA par_params_ptr+1` / `STA par_row_ptr+1`: A = the high byte. `par_next_param` keeps X and Y (A = `par_row_ptr`'s new high byte). The entry registers are not read.
  - `frm_copy_rest_frame`: `LDY #$13`, `LDA $2000,Y` / `STA ($2C),Y` / `DEY` / `BPL`: Y = $FF, A = `frm_rest_frame[0]`, X untouched.
  - `frm_snapshot`: `LDY #$0A`, `LDA $16CC,Y` / `STA $16DC,Y` / `DEY` / `BNE` (`frm_snap_prev[i] = frm_snap[i]` for i = 9..0); `LDX #$0A`, `LDY $B772,X` / `LDA ($2C),Y` / `STA $16CC,X` / `DEX` / `BNE`: X = 0, Y = `frm_snapshot_map[0]` = 10, A = the frame's b10. M4.4a converted `frm_snapshot_map` ($B773: `0a 08 06 04 02 00 01 03 05 07`), read through `fw_rom8` at `FRM_SNAPSHOT_MAP - 1 + X`.
  - `frm_clamp_mark_space`: `LDY #$0A`, `LDA ($2C),Y`, `INY`, `ASL A`, `STA $75` (2c, 8 bits: bit 7 of c is lost), `LDA cmd_mark_space` / `STA ($2C),Y`; `LDA #$A0` / `SEC` / `SBC $75` / `CMP ($2C),Y` / `BCS` / `STA` (written when 160 − 2c, 8 bits, is below b11, unsigned); `LDA #$2F` / `SEC` / `SBC $75` / `BMI` / `CMP` / `BCC` / `STA` (skipped when 47 − 2c has bit 7 set; written when it is not below b11); `LDA #$0B` / `CMP` / `BCC` / `STA` (written when b11 is not above 11). Exit A = $0B, Y = $0B, X untouched.
  - `frm_raise_formants`: `LDY #$0A`, `LDA ($2C),Y`, `SEC`, `SBC #$20`, `BPL`, else `RTS` (A = b10 − 32 with bit 7 set, Y = 10); `LSR`, `LSR`, `STA $75`; `LDY #3`, `CLC`, `ADC ($2C),Y`, `JSR par_clamp`, `STA ($2C),Y`; `LDA $75`, `LSR`, `LDY #5`, `CLC`, `ADC ($2C),Y`, `JSR par_clamp`, `STA ($2C),Y`, `RTS`: A = the clamped F2, Y = 5; X untouched (`par_clamp` touches only A).
- **Every RAM write** (spec §2.6): `frm_rest_frame` (20 bytes) and `par_row_ptr` (moved by `par_next_param` three times, then set to `par_params_ptr`); the producer frame's 20 bytes; `frm_snap_prev` and `frm_snap`; b11 up to four times and `$75`; F1, F2 and `$75`. Nothing else.
- **What the callers read after each call** (the thunk comments): see each thunk's comment in Step 3; `par_setup_voice`'s X (9, from `frm_build_rest_frame`) passes through every later call to its callers, and its A and Y ($0B) are `frm_clamp_mark_space`'s. No caller reads a flag.
- **`ROM_FRM_RAISE_FORMANTS` did not exist** (spec §2.2 says all `ROM_FRM_*` addresses exist in `par_addr.h`; `frm_raise_formants` had no C caller before): it is added here, its caller in parentheses until Task 3.
- **Reach in `golden_hybrid`'s "all" mode in this task:** `frm_build_rest_frame` and `frm_copy_rest_frame` (from `par_setup_voice`) are called C to C, as `frm_copy_rest_frame` from the rest frames is not yet (the C `par_play` calls it through `fw_rom_call` until Task 3, and its hook fires there); `frm_snapshot`, `frm_clamp_mark_space` and `frm_raise_formants` fire from the ROM `frm_finish_frame`, which the C `par_play` calls through `fw_rom_call`. `par_next_param` and `par_clamp`, whose thunk comments said they were reached from ROM in every mode through the frame builder, now have only C callers; their comments say so.
- **The unit tests** (`tests/test_fw_frm.c`): each case's RAM is checked byte for byte (`check_written`), the registers in and out per spec §2.5. `tests/test_fw_par.c` and `tests/test_fw_cmd.c` had mocks for the three `frm_*` routines `par_setup_voice` called through the ROM; their `par_setup_voice` cases now check that no ROM call was made and what the C wrote (the rest frame and both frame buffers). `check_buffers` (`test_fw_par.c`), which read the frame pointers at each ROM call, has no call left to read and goes.

**Files:**
- Create: `src/firmware/frm_build.h`, `src/firmware/frm_build.c`
- Modify: `src/firmware/par_start.c`, `src/firmware/par_start.h`, `src/firmware/par_addr.h`, `src/engine/thunks.c`, `tests/test_fw_frm.c`, `tests/test_fw_par.c`, `tests/test_fw_cmd.c`

**Where the code goes.** `src/firmware/frm_build.c`, one public function per routine plus `static` helpers, one per ROM block:

| Helper | ROM |
|---|---|
| `copy_template` | $B41D–$B426, `frm_template` into `frm_rest_frame` |
| `pause_formants` | $B428–$B438, the pause phoneme's FN, F1, F2, F3 |
| `save_snapshot` | $B917–$B920, `frm_snap_prev` = `frm_snap` |
| `take_snapshot` | $B922–$B92D, `frm_snap` through `frm_snapshot_map` |
| `mark_space` | $B930–$B93A, $75 = 2c, b11 = @M |
| `upper_cap`, `lower_bound`, `floor_11` | $B93C–$B945, $B947–$B952, $B954–$B95A |
| `raise` | $B96B–$B973 (F1) and $B978–$B980 (F2) |

`frm_copy_rest_frame` ($B445–$B44F) has no helper; the entries and the reset of `par_row_ptr` ($B43A–$B444) and the test at $B95D–$B966 are in the public functions.

**Interfaces:**
- Consumes: Task 1's `FRAME_*` b11–b19 names, `FRAME_SIZE`, `FRM_TEMPLATE`, `fw_frm_template` and `test_fw_frm.c` (`setup`, `g_ram`, `g_f`, `g_ncalls`); `fw_rom8`, `fw_ind_y`, `fw_wr_ind_y`, `fw_mem8` (`fw.h`); `par_next_param`, `par_clamp` (`par_segment.h`); `FRM_SNAPSHOT_MAP` (`fw_data.h`); `ISR_FILL_PTR`, `PAR_ROW_PTR`, `PAR_PARAMS_PTR`, `FRM_REST_FRAME`, `FRM_SNAP`, `FRM_SNAP_PREV`, `CMD_MARK_SPACE` (`fw_ram.h`), `FW_TMP_75` (`fw.h`); the thunk style of `src/engine/thunks.c`.
- Produces (Task 3 adds `frm_transition_flags` and `frm_finish_frame` to the same files):

```c
/* src/firmware/frm_build.h */
#define FRM_PAUSE_PHONEME 0x3B, FRM_MARK_SPACE_CAP 0xA0, FRM_MARK_SPACE_BASE 0x2F, FRM_MARK_SPACE_FLOOR 0x0B,
        FRM_RAISE_PITCH 0x20
void frm_build_rest_frame(fw *f, fw_regs *r);   /* $B41D: A = par_params_ptr high, X = 9, Y = $3B */
void frm_copy_rest_frame(fw *f, fw_regs *r);    /* $B445: A = frm_rest_frame[0], X kept, Y = $FF */
void frm_snapshot(fw *f, fw_regs *r);           /* $B917: A = b10, X = 0, Y = 10 */
void frm_clamp_mark_space(fw *f, fw_regs *r);   /* $B930: A = $0B, X kept, Y = 11 */
void frm_raise_formants(fw *f, fw_regs *r);     /* $B95D: A = b10 - 32, Y = 10 (early), else A = F2, Y = 5; X kept */
/* src/firmware/par_addr.h */
#define ROM_FRM_RAISE_FORMANTS     0xB95D
```

  - The replacements `frm_build_rest_frame` 0xB41D and `frm_copy_rest_frame` 0xB445 (after `par_setup_voice`), `frm_snapshot` 0xB917, `frm_clamp_mark_space` 0xB930 and `frm_raise_formants` 0xB95D (after `par_play`), all `FW_OUT_A | FW_OUT_X | FW_OUT_Y`, not `waits`: **69 replacements, 70 `golden_hybrid` modes, 67 `diff_lts` lines**.
  - `tests/test_fw_frm.c`: `check_row`, `snapshot`, `check_written`, `check_no_calls`, `fill_frame_b` for Task 3; `test_raise_formants();` as the last line of `suite_fw_frm` (Task 3's anchor).

- [ ] **Step 1: Write the failing tests**

**`tests/test_fw_frm.c`.** Replace

```c
#include "test.h"
#include "firmware/fw.h"
#include "firmware/data/fw_data.h"
```

with

```c
#include "test.h"
#include "engine/thunks.h"
#include "firmware/frm_build.h"
#include "firmware/par_addr.h"
#include "firmware/data/fw_data.h"
```

Replace

```c
static fw g_f;
```

with

```c
static uint8_t g_before[FW_RAM_SIZE];
static fw g_f;
```

Replace `suite_fw_frm`

```c
void suite_fw_frm(void)
{
    CHECK(fw_image_get() != NULL);
    test_data();
}
```

with the harness, the leaf tests and the suite

```c
/* The hook of name sits at addr, declares A, X and Y and does not wait (pinned here; diff_lts compares the outputs
   against the ROM). */
static void check_row(const char *name, uint16_t addr)
{
    const fw_replacement *r = fw_replacement_find(name);
    CHECK(r != NULL);
    if (!r)
        return;
    CHECK_EQ(r->addr, addr);
    CHECK_EQ(r->outputs, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    CHECK(!r->waits);
}

/* The RAM as it is now, for check_written. */
static void snapshot(void) { memcpy(g_before, g_ram, sizeof g_before); }

/* Every RAM byte except the n listed is as snapshot() saw it (the listed ones are checked by the caller). */
static void check_written(const uint16_t *addrs, size_t n)
{
    for (uint16_t a = 0; a < FW_RAM_SIZE; a++) {
        bool listed = false;
        for (size_t i = 0; i < n; i++)
            listed |= addrs[i] == a;
        if (!listed && g_ram[a] != g_before[a]) {
            fprintf(stderr, "  RAM $%04X written: %02X -> %02X\n", a, g_before[a], g_ram[a]);
            CHECK(g_ram[a] == g_before[a]);
        }
    }
}

/* No ROM call was made and nothing faulted. */
static void check_no_calls(void)
{
    CHECK_EQ(g_ncalls, 0);
    CHECK(!g_f.fault);
}

/* The producer frame (isr_fill_ptr) is isr_frame_b. */
static void fill_frame_b(void) { fw_wr16(&g_f, ISR_FILL_PTR, ISR_FRAME_B); }

/* ---- the leaves (M4.5 Task 2) ---- */

static void test_leaf_rows(void)
{
    check_row("frm_build_rest_frame", ROM_FRM_BUILD_REST_FRAME);
    check_row("frm_copy_rest_frame", ROM_FRM_COPY_REST_FRAME);
    check_row("frm_snapshot", ROM_FRM_SNAPSHOT);
    check_row("frm_clamp_mark_space", ROM_FRM_CLAMP_MARK_SPACE);
    check_row("frm_raise_formants", ROM_FRM_RAISE_FORMANTS);
}

/* frm_build_rest_frame after setup() and the caller's set-up: frm_rest_frame ($EE before) = frm_template with FN,
   F1, F2, F3 = want[0..3]; par_row_ptr = par_params_ptr; A = its high byte, X = 9 ($B436 CPX #8 after X = 1, 3, 5,
   7), Y = $3B; only frm_rest_frame and par_row_ptr written, no call. The entry registers are not read. */
static void check_build(uint16_t params, const uint8_t want[4])
{
    static const uint16_t w[] = {
        FRM_REST_FRAME + 0, FRM_REST_FRAME + 1, FRM_REST_FRAME + 2, FRM_REST_FRAME + 3, FRM_REST_FRAME + 4,
        FRM_REST_FRAME + 5, FRM_REST_FRAME + 6, FRM_REST_FRAME + 7, FRM_REST_FRAME + 8, FRM_REST_FRAME + 9,
        FRM_REST_FRAME + 10, FRM_REST_FRAME + 11, FRM_REST_FRAME + 12, FRM_REST_FRAME + 13, FRM_REST_FRAME + 14,
        FRM_REST_FRAME + 15, FRM_REST_FRAME + 16, FRM_REST_FRAME + 17, FRM_REST_FRAME + 18, FRM_REST_FRAME + 19,
        PAR_ROW_PTR, PAR_ROW_PTR + 1,
    };
    memset(g_ram + FRM_REST_FRAME, 0xEE, FRAME_SIZE);
    snapshot();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    frm_build_rest_frame(&g_f, &r);
    for (unsigned b = 0; b < FRAME_SIZE; b++) {
        uint8_t v = fw_frm_template[b];
        if (b == FRAME_FN || b == FRAME_F1 || b == FRAME_F2 || b == FRAME_F3)
            v = want[b >> 1];
        CHECK_EQ(g_ram[FRM_REST_FRAME + b], v);
    }
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), params);
    CHECK_EQ(r.a, params >> 8);
    CHECK_EQ(r.x, 9);
    CHECK_EQ(r.y, FRM_PAUSE_PHONEME);
    check_written(w, sizeof w / sizeof w[0]);
    check_no_calls();
}

/* Voice A as par_setup_voice leaves it (par_row_ptr = par_params_ptr = the table + 2N, P1's target row; stride
   5N = 430): the pause phoneme's FN 32, F1 32, F2 23, F3 31 from the ROM table. Then a RAM table whose par_row_ptr
   ($1000) is not par_params_ptr ($3010), stride $0100: the four rows' bytes at $103B, $113B, $123B, $133B, and
   par_row_ptr set to par_params_ptr, not back to where it started. */
static void test_build_rest_frame(void)
{
    static const uint8_t voice_a[4] = { 32, 32, 23, 31 };
    static const uint8_t ram[4] = { 0x11, 0x22, 0x33, 0x44 };
    const uint16_t params = PAR_VOICE_A + 2 * PAR_PHONEMES;
    setup();
    fw_wr16(&g_f, PAR_ROW_PTR, params);
    fw_wr16(&g_f, PAR_PARAMS_PTR, params);
    fw_wr16(&g_f, PAR_PARAM_STRIDE, 5 * PAR_PHONEMES);
    for (unsigned k = 0; k < 4; k++)
        CHECK_EQ(fw_par_voice_a[FRM_PAUSE_PHONEME].p[k].target, voice_a[k]);
    check_build(params, voice_a);

    setup();
    fw_wr16(&g_f, PAR_ROW_PTR, 0x1000);
    fw_wr16(&g_f, PAR_PARAMS_PTR, 0x3010);
    fw_wr16(&g_f, PAR_PARAM_STRIDE, 0x0100);
    for (unsigned k = 0; k < 4; k++)
        g_ram[0x103B + 0x100 * k] = ram[k];
    check_build(0x3010, ram);
}

/* frm_copy_rest_frame: the 20 bytes of frm_rest_frame ($40 + b) into the producer frame (isr_frame_b, $EE before),
   Y = $13 down to 0: A = frm_rest_frame[0], X untouched, Y = $FF; only the frame written. */
static void test_copy_rest_frame(void)
{
    uint16_t w[FRAME_SIZE];
    setup();
    fill_frame_b();
    for (unsigned b = 0; b < FRAME_SIZE; b++) {
        g_ram[FRM_REST_FRAME + b] = (uint8_t)(0x40 + b);
        g_ram[ISR_FRAME_B + b] = 0xEE;
        w[b] = (uint16_t)(ISR_FRAME_B + b);
    }
    snapshot();
    fw_regs r = { 0x11, 0x5A, 0x33, 0 };
    frm_copy_rest_frame(&g_f, &r);
    for (unsigned b = 0; b < FRAME_SIZE; b++)
        CHECK_EQ(g_ram[ISR_FRAME_B + b], 0x40 + b);
    CHECK_EQ(r.a, 0x40);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, 0xFF);
    check_written(w, FRAME_SIZE);
    check_no_calls();
}

/* frm_snapshot: frm_snap ($30 + i) moves to frm_snap_prev ($EE before); frm_snap[i] = the producer frame's byte
   frm_snapshot_map[i] (frame byte b = $50 + b; the map is 10 8 6 4 2 0 1 3 5 7). A = b10 ($5A), X = 0, Y = 10; the
   bytes around frm_snap_prev untouched. */
static void test_snapshot(void)
{
    static const uint8_t map[10] = { 10, 8, 6, 4, 2, 0, 1, 3, 5, 7 };
    uint16_t w[20];
    setup();
    fill_frame_b();
    for (unsigned b = 0; b < FRAME_SIZE; b++)
        g_ram[ISR_FRAME_B + b] = (uint8_t)(0x50 + b);
    memset(g_ram + FRM_SNAP_PREV - 1, 0xEE, 12);
    for (unsigned i = 0; i < 10; i++) {
        g_ram[FRM_SNAP + i] = (uint8_t)(0x30 + i);
        w[i] = (uint16_t)(FRM_SNAP + i);
        w[10 + i] = (uint16_t)(FRM_SNAP_PREV + i);
    }
    snapshot();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    frm_snapshot(&g_f, &r);
    for (unsigned i = 0; i < 10; i++) {
        CHECK_EQ(fw_frm_snapshot_map[i], map[i]);
        CHECK_EQ(g_ram[FRM_SNAP_PREV + i], 0x30 + i);
        CHECK_EQ(g_ram[FRM_SNAP + i], 0x50 + map[i]);
    }
    CHECK_EQ(g_ram[FRM_SNAP_PREV - 1], 0xEE);
    CHECK_EQ(g_ram[FRM_SNAP_PREV + 10], 0xEE);
    CHECK_EQ(r.a, 0x5A);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, FRAME_PITCH);
    check_written(w, 20);
    check_no_calls();
}

/* One frm_clamp_mark_space call: b10 = c and @M = m, the producer frame's b11 $EE before. Out: b11 and FW_TMP_75
   (2c, 8 bits) as the ROM leaves them; A = $0B, X untouched, Y = 11; nothing else written. */
static void clamp_case(uint8_t c, uint8_t m, uint8_t want_b11, uint8_t want_75)
{
    static const uint16_t w[] = { ISR_FRAME_B + FRAME_MARK_SPACE, FW_TMP_75 };
    setup();
    fill_frame_b();
    g_ram[ISR_FRAME_B + FRAME_PITCH] = c;
    g_ram[ISR_FRAME_B + FRAME_MARK_SPACE] = 0xEE;
    g_ram[CMD_MARK_SPACE] = m;
    snapshot();
    fw_regs r = { 0x11, 0x5A, 0x33, 0 };
    frm_clamp_mark_space(&g_f, &r);
    CHECK_EQ(g_ram[ISR_FRAME_B + FRAME_MARK_SPACE], want_b11);
    CHECK_EQ(g_ram[FW_TMP_75], want_75);
    CHECK_EQ(r.a, FRM_MARK_SPACE_FLOOR);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, FRAME_MARK_SPACE);
    check_written(w, 2);
    check_no_calls();
}

static void test_clamp_mark_space(void)
{
    /* the upper cap: c = 63, 2c = 126: 160 - 126 = 34 is below @M $3F ($B943 BCS not taken): b11 = 34; 47 - 126 =
       $B1 has bit 7 set ($B94C BMI taken); 11 is below 34 */
    clamp_case(63, 0x3F, 34, 126);
    /* @M at the cap is kept: c = 32, 2c = 64: 96 is not below @M 96 (BCS taken) */
    clamp_case(32, 96, 96, 64);
    /* 47 - 2c positive and raising b11: c = 10: 47 - 20 = 27 is not below @M 5 ($B950 BCC not taken): b11 = 27 */
    clamp_case(10, 5, 27, 20);
    /* 47 - 2c positive, below @M 30 (BCC taken): b11 stays 30 */
    clamp_case(10, 30, 30, 20);
    /* 47 - 2c negative (skipped) and the floor: c = 32: 47 - 64 = $EF; @M 3 is not above 11 ($B958 BCC not taken):
       b11 = 11 */
    clamp_case(32, 3, 11, 64);
    /* above the floor (BCC taken): @M 12 stays */
    clamp_case(32, 12, 12, 64);
    /* the 8-bit wraps: c = 90, 2c = 180 ($B4): 160 - 180 = $EC is not below @M 63 (BCS taken: no cap), and
       47 - 180 = $7B has bit 7 clear and is not below 63: b11 = 123 */
    clamp_case(90, 63, 0x7B, 0xB4);
    /* the ASL loses bit 7: c = $81, 2c = 2: 158 is not below @M 63; 47 - 2 = 45 is below 63: b11 = 63 */
    clamp_case(0x81, 63, 63, 2);
}

/* One frm_raise_formants call: b10 = c, F1 = f1, F2 = f2 in the producer frame, FW_TMP_75 = $EE before. Out: F1, F2
   and FW_TMP_75 as the ROM leaves them; A and Y as given, X untouched; nothing else written. */
static void raise_case(uint8_t c, uint8_t f1, uint8_t f2, uint8_t want_f1, uint8_t want_f2, uint8_t want_75,
                       uint8_t want_a, uint8_t want_y)
{
    static const uint16_t w[] = { ISR_FRAME_B + FRAME_F1, ISR_FRAME_B + FRAME_F2, FW_TMP_75 };
    setup();
    fill_frame_b();
    g_ram[ISR_FRAME_B + FRAME_PITCH] = c;
    g_ram[ISR_FRAME_B + FRAME_F1] = f1;
    g_ram[ISR_FRAME_B + FRAME_F2] = f2;
    g_ram[FW_TMP_75] = 0xEE;
    snapshot();
    fw_regs r = { 0x11, 0x5A, 0x33, 0 };
    frm_raise_formants(&g_f, &r);
    CHECK_EQ(g_ram[ISR_FRAME_B + FRAME_F1], want_f1);
    CHECK_EQ(g_ram[ISR_FRAME_B + FRAME_F2], want_f2);
    CHECK_EQ(g_ram[FW_TMP_75], want_75);
    CHECK_EQ(r.a, want_a);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, want_y);
    check_written(w, 3);
    check_no_calls();
}

static void test_raise_formants(void)
{
    /* b10 = 31: 31 - 32 = $FF, bit 7 set: the RTS at $B966, A = $FF, Y = 10, nothing written */
    raise_case(31, 20, 40, 20, 40, 0xEE, 0xFF, FRAME_PITCH);
    /* b10 = $A0: $A0 - 32 = $80 has bit 7 set too (BPL is bit 7 of the 8-bit difference): it returns at once */
    raise_case(0xA0, 20, 40, 20, 40, 0xEE, 0x80, FRAME_PITCH);
    /* b10 = 32: d = 0: FW_TMP_75 = 0, F1 and F2 written back unchanged through par_clamp; A = F2, Y = 5 */
    raise_case(32, 20, 40, 20, 40, 0, 40, FRAME_F2);
    /* b10 = 63: (63 - 32) >> 2 = 7: F1 20 + 7 = 27, F2 40 + (7 >> 1) = 43 */
    raise_case(63, 20, 40, 27, 43, 7, 43, FRAME_F2);
    /* at the clamp: F1 60 + 7 = 67 -> 63, F2 62 + 3 = 65 -> 63 */
    raise_case(63, 60, 62, 63, 63, 7, 63, FRAME_F2);
    /* b10 = $9F: $7F >> 2 = $1F: F1 0 + 31 = 31, F2 0 + 15 = 15 */
    raise_case(0x9F, 0, 0, 31, 15, 0x1F, 15, FRAME_F2);
}

void suite_fw_frm(void)
{
    CHECK(fw_image_get() != NULL);
    test_data();
    test_leaf_rows();
    test_build_rest_frame();
    test_copy_rest_frame();
    test_snapshot();
    test_clamp_mark_space();
    test_raise_formants();
}
```

What the new tests pin:
- `test_leaf_rows`: the five rows (address, A/X/Y declared, not `waits`).
- `test_build_rest_frame`: voice A from the ROM table (FN 32, F1 32, F2 23, F3 31, the pause row of `par_voice_a`), and a RAM table whose `par_row_ptr` differs from `par_params_ptr` (the reset goes to `par_params_ptr`); the template's other 16 bytes; A, X = 9, Y = $3B.
- `test_copy_rest_frame`: the 20 bytes, A = `frm_rest_frame[0]`, X kept, Y = $FF.
- `test_snapshot`: `frm_snap_prev` = the old `frm_snap`; the new one through the map; the bytes around `frm_snap_prev` untouched.
- `test_clamp_mark_space`: the upper cap (written, and at equality not); 47 − 2c positive raising b11 and below it; 47 − 2c negative (skipped) with the floor of 11 written and not; **the 8-bit wraps**: c = 90 (160 − 180 = $EC is not below @M: no cap; 47 − 180 = $7B has bit 7 clear: b11 = 123) and c = $81 (the `ASL` loses bit 7: 2c = 2); `$75` = 2c each time.
- `test_raise_formants`: b10 < 32 (the early return, Y = 10, nothing written); **b10 = $A0, whose b10 − 32 = $80 has bit 7 set** (returns at once too); b10 = 32 (d = 0, written back unchanged); b10 = 63 away from the clamp and at it (63); b10 = $9F (d >> 2 = $1F).

**`tests/test_fw_par.c`** (`par_setup_voice` now calls the C leaves: no ROM call, the frames written by the C). Replace

```c
#include "firmware/fw_ops.h"
#include "firmware/par_addr.h"
```

with

```c
#include "firmware/frm_build.h"
#include "firmware/fw_ops.h"
#include "firmware/par_addr.h"
```

replace

```c
/* Records every ROM call. The three frame routines par_setup_voice calls (M4.5) and isr_set_volume (M4.6, called by
   cmd_ev_set_volume) return the registers the ROM ones leave; so do the interrupt, serial and frame routines that
   par_play and boot_loop_play call (M4.5, M4.6), with what the timer and receive interrupts would do between the
   frame loop's tests (g_flag_every, g_abort_at); any other call fails. */
```

with

```c
/* Records every ROM call. isr_set_volume (M4.6, called by cmd_ev_set_volume) returns the registers the ROM one
   leaves; so do the interrupt, serial and frame routines that par_play and boot_loop_play call (M4.5, M4.6), with
   what the timer and receive interrupts would do between the frame loop's tests (g_flag_every, g_abort_at); any
   other call fails (par_setup_voice calls the C frame builder since M4.5). */
```

delete

```c
    case ROM_FRM_BUILD_REST_FRAME:   /* $B428-$B444: LDY #$3B; LDX #1, INX / INX until X >= 8 (9); LDA par_params_ptr+1 */
        r->a = g_ram[PAR_PARAMS_PTR + 1];
        r->x = 9;
        r->y = 0x3B;
        return FW_CALL_RETURNED;
```

replace

```c
/* Record i's frame buffer pointers: isr_play_ptr, isr_fill_ptr and isr_swap_phase as the C set them (LDM) before
   the call. */
static void check_buffers(unsigned i, uint16_t play, uint16_t fill, uint8_t phase)
{
    CHECK(i < g_ncalls && i < MAX_CALLS);
    if (i >= g_ncalls || i >= MAX_CALLS)
        return;
    CHECK_EQ(g_calls[i].play, play);
    CHECK_EQ(g_calls[i].fill, fill);
    CHECK_EQ(g_calls[i].phase, phase);
}

/* par_setup_voice on a table at `table` with n phonemes: the pointers and the stride, then the five ROM calls in
   order with the registers the ROM has at each JSR and the buffers set before each; out, the last call's A and Y and
   frm_build_rest_frame's X. The entry A and X are not read; the entry Y reaches frm_build_rest_frame. */
static void setup_voice_case(uint16_t table, uint8_t n)
{
    setup();
    g_ram[PAR_COUNT] = n;
    fw_wr16(&g_f, PAR_TABLE_PTR, table);
    fw_wr16(&g_f, PAR_PARAM_STRIDE, 0x5A5A);
    g_ram[FRM_REST_FRAME] = 0x77;                     /* frm_rest_frame[0]: frm_copy_rest_frame's exit A */
    fw_regs r = { 0x90, 0x31, 0x42, 0 };
    par_setup_voice(&g_f, &r);
    uint16_t params = (uint16_t)(table + 2 * n), stride = (uint16_t)(5 * n);
    CHECK_EQ(g_ram[PAR_ROW_LEN], n);
    CHECK_EQ(fw_rd16(&g_f, PAR_TABLE_COPY), table);
    CHECK_EQ(fw_rd16(&g_f, PAR_RANK_PTR), table);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW2_PTR), table + n);
    CHECK_EQ(fw_rd16(&g_f, PAR_PARAMS_PTR), params);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), params);     /* left on P1's target row, as frm_build_rest_frame needs */
    CHECK_EQ(fw_rd16(&g_f, PAR_PARAM_STRIDE), stride);
    CHECK_EQ(g_ncalls, 5);
    check_call(0, ROM_FRM_BUILD_REST_FRAME, (uint8_t)(stride >> 8), 0, 0x42);   /* A = the stride's high byte
                                                                                   ($B3F2), X = 0 (the shift loop) */
    check_buffers(0, ISR_FRAME_A, ISR_FRAME_B, 2);
    check_call(1, ROM_FRM_COPY_REST_FRAME, (uint8_t)(params >> 8), 9, 0x3B);    /* frm_build_rest_frame's exit */
    check_buffers(1, ISR_FRAME_A, ISR_FRAME_B, 2);
    check_call(2, ROM_FRM_CLAMP_MARK_SPACE, 0x77, 9, 0xFF);                     /* frm_copy_rest_frame's exit */
    check_buffers(2, ISR_FRAME_A, ISR_FRAME_B, 2);
    check_call(3, ROM_FRM_COPY_REST_FRAME, 0x0B, 9, 0x0B);                      /* frm_clamp_mark_space's exit */
    check_buffers(3, ISR_FRAME_B, ISR_FRAME_A, 1);
    check_call(4, ROM_FRM_CLAMP_MARK_SPACE, 0x77, 9, 0xFF);
    check_buffers(4, ISR_FRAME_B, ISR_FRAME_A, 1);
    CHECK_EQ(r.a, 0x0B);
    CHECK_EQ(r.x, 9);
    CHECK_EQ(r.y, 0x0B);
    CHECK(!g_f.fault);
}
```

with

```c
/* par_setup_voice on a table at `table` with n phonemes: the pointers and the stride; then the C frame builder (M4.5),
   which makes no ROM call: frm_rest_frame = frm_template with b1, b3, b5, b7 = row $3B of the four target rows from
   par_params_ptr (5N apart, read as the C reads them); both frame buffers = the rest frame with b11 = @M (0) raised
   to the floor 11 (b10 is the template's 32: the cap 96 and the bound 47 - 64, which is skipped); isr_play_ptr =
   isr_frame_b, isr_fill_ptr = isr_frame_a and isr_swap_phase = 1 at the end; FW_TMP_75 = 2 x 32. Out, the last
   call's A and Y and frm_build_rest_frame's X. The entry A and X are not read, nor Y (LDY #$14 first). */
static void setup_voice_case(uint16_t table, uint8_t n)
{
    setup();
    g_ram[PAR_COUNT] = n;
    fw_wr16(&g_f, PAR_TABLE_PTR, table);
    fw_wr16(&g_f, PAR_PARAM_STRIDE, 0x5A5A);
    fw_regs r = { 0x90, 0x31, 0x42, 0 };
    par_setup_voice(&g_f, &r);
    uint16_t params = (uint16_t)(table + 2 * n), stride = (uint16_t)(5 * n);
    CHECK_EQ(g_ram[PAR_ROW_LEN], n);
    CHECK_EQ(fw_rd16(&g_f, PAR_TABLE_COPY), table);
    CHECK_EQ(fw_rd16(&g_f, PAR_RANK_PTR), table);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW2_PTR), table + n);
    CHECK_EQ(fw_rd16(&g_f, PAR_PARAMS_PTR), params);
    CHECK_EQ(fw_rd16(&g_f, PAR_ROW_PTR), params);     /* moved by frm_build_rest_frame, then set back to P1's row */
    CHECK_EQ(fw_rd16(&g_f, PAR_PARAM_STRIDE), stride);
    CHECK_EQ(g_ncalls, 0);
    for (unsigned b = 0; b < FRAME_SIZE; b++) {
        uint8_t v = fw_frm_template[b];
        if (b == FRAME_FN || b == FRAME_F1 || b == FRAME_F2 || b == FRAME_F3)
            v = fw_mem8(&g_f, (uint16_t)(params + (b >> 1) * stride + FRM_PAUSE_PHONEME));
        CHECK_EQ(g_ram[FRM_REST_FRAME + b], v);
        if (b == FRAME_MARK_SPACE)
            v = FRM_MARK_SPACE_FLOOR;
        CHECK_EQ(g_ram[ISR_FRAME_A + b], v);
        CHECK_EQ(g_ram[ISR_FRAME_B + b], v);
    }
    CHECK_EQ(fw_rd16(&g_f, ISR_PLAY_PTR), ISR_FRAME_B);
    CHECK_EQ(fw_rd16(&g_f, ISR_FILL_PTR), ISR_FRAME_A);
    CHECK_EQ(g_ram[ISR_SWAP_PHASE], 1);
    CHECK_EQ(g_ram[FW_TMP_75], 64);
    CHECK_EQ(r.a, 0x0B);
    CHECK_EQ(r.x, 9);
    CHECK_EQ(r.y, 0x0B);
    CHECK(!g_f.fault);
}
```

replace

```c
/* Voice A (N = 86: stride 430 = $01AE, 4N = 344 sets the high byte through ROL) and a RAM table of 255 phonemes
   ($10F0: both par_next_row calls carry into the high byte; 4N = $3FC, + N carries: stride $04FB). */
```

with

```c
/* Voice A (N = 86: stride 430 = $01AE, 4N = 344 sets the high byte through ROL; the pause row gives FN 32, F1 32,
   F2 23, F3 31) and a RAM table of 255 phonemes ($10F0: both par_next_row calls carry into the high byte; 4N = $3FC,
   + N carries: stride $04FB; the pause row's four bytes are RAM at $1329, $1824, $1D1F, $221A). */
```

**`tests/test_fw_cmd.c`** (the C `par_setup_voice` reached from the command handlers: no ROM call, its frames checked). Replace

```c
#include "firmware/cmd_run.h"
```

with

```c
#include "firmware/cmd_run.h"
#include "firmware/frm_build.h"
```

delete

```c
    /* the frame routines the C par_setup_voice calls (M4.5) return the registers the ROM ones leave */
    if (addr == ROM_FRM_BUILD_REST_FRAME) {     /* $B428-$B444: Y = $3B, X = 9 (its INX loop), A = par_params_ptr+1 */
        r->a = g_ram[PAR_PARAMS_PTR + 1];
        r->x = 9;
        r->y = 0x3B;
        return FW_CALL_RETURNED;
    }
    if (addr == ROM_FRM_COPY_REST_FRAME) {      /* $B445-$B44F: Y = $FF, A = frm_rest_frame[0]; X untouched */
        r->a = g_ram[FRM_REST_FRAME];
        r->y = 0xFF;
        return FW_CALL_RETURNED;
    }
    if (addr == ROM_FRM_CLAMP_MARK_SPACE) {     /* $B930-$B95C: A = Y = $0B; X untouched */
        r->a = 0x0B;
        r->y = 0x0B;
        return FW_CALL_RETURNED;
    }
```

replace

```c
/* Calls i..i+4 are the C par_setup_voice's (M4.4a): frm_build_rest_frame with A = par_param_stride's high byte, X = 0
   and Y = y (the Y handed to par_setup_voice), then frm_copy_rest_frame, frm_clamp_mark_space, frm_copy_rest_frame
   and frm_clamp_mark_space, each with the registers the one before left; and the voice pointers and the stride it
   set from par_count (N) and par_table_ptr. */
static void check_setup_voice(unsigned i, uint8_t y)
{
    uint8_t n = g_ram[PAR_COUNT];
    uint16_t table = fw_rd16(&g_f, PAR_TABLE_PTR), params = (uint16_t)(table + 2 * n), stride = (uint16_t)(5 * n);
    uint8_t rest0 = g_ram[FRM_REST_FRAME];
    check_call(i, ROM_FRM_BUILD_REST_FRAME, (uint8_t)(stride >> 8), 0, y);
    check_call(i + 1, ROM_FRM_COPY_REST_FRAME, (uint8_t)(params >> 8), 9, 0x3B);
    check_call(i + 2, ROM_FRM_CLAMP_MARK_SPACE, rest0, 9, 0xFF);
    check_call(i + 3, ROM_FRM_COPY_REST_FRAME, 0x0B, 9, 0x0B);
    check_call(i + 4, ROM_FRM_CLAMP_MARK_SPACE, rest0, 9, 0xFF);
    CHECK_EQ(g_ram[PAR_ROW_LEN], n);
    CHECK_EQ(fw_rd16(&g_f, PAR_RANK_PTR), table);
    CHECK_EQ(fw_rd16(&g_f, PAR_PARAMS_PTR), params);
    CHECK_EQ(fw_rd16(&g_f, PAR_PARAM_STRIDE), stride);
}
```

with

```c
/* The C par_setup_voice ran (M4.4a; with the C frame builder since M4.5, so no ROM call): the voice pointers and the
   stride it set from par_count (N) and par_table_ptr; frm_rest_frame = frm_template with b1, b3, b5, b7 = row $3B of
   the four target rows from par_params_ptr (5N apart); both frame buffers = the rest frame with b11 = @M clamped at
   the template's pitch code 32 (at most 160 - 64 = 96, at least 11; 47 - 64 is skipped). */
static void check_setup_voice(void)
{
    uint8_t n = g_ram[PAR_COUNT], m = g_ram[CMD_MARK_SPACE];
    uint16_t table = fw_rd16(&g_f, PAR_TABLE_PTR), params = (uint16_t)(table + 2 * n), stride = (uint16_t)(5 * n);
    CHECK_EQ(g_ram[PAR_ROW_LEN], n);
    CHECK_EQ(fw_rd16(&g_f, PAR_RANK_PTR), table);
    CHECK_EQ(fw_rd16(&g_f, PAR_PARAMS_PTR), params);
    CHECK_EQ(fw_rd16(&g_f, PAR_PARAM_STRIDE), stride);
    for (unsigned b = 0; b < FRAME_SIZE; b++) {
        uint8_t v = fw_frm_template[b];
        if (b == FRAME_FN || b == FRAME_F1 || b == FRAME_F2 || b == FRAME_F3)
            v = fw_mem8(&g_f, (uint16_t)(params + (b >> 1) * stride + FRM_PAUSE_PHONEME));
        CHECK_EQ(g_ram[FRM_REST_FRAME + b], v);
        if (b == FRAME_MARK_SPACE)
            v = m > 96 ? 96 : m < FRM_MARK_SPACE_FLOOR ? FRM_MARK_SPACE_FLOOR : m;
        CHECK_EQ(g_ram[ISR_FRAME_A + b], v);
        CHECK_EQ(g_ram[ISR_FRAME_B + b], v);
    }
}
```

replace

```c
    CHECK_EQ(g_ram[PROS_PITCH_CENTRE], 0x90);
    CHECK_EQ(g_ncalls, 5);
    check_setup_voice(0, 0x42);                 /* the C par_setup_voice: A (pros_pitch_centre) and X are not read */
```

with

```c
    CHECK_EQ(g_ram[PROS_PITCH_CENTRE], 0x90);
    CHECK_EQ(g_ncalls, 0);
    check_setup_voice();                        /* the C par_setup_voice: A (pros_pitch_centre), X and Y not read */
```

replace

```c
    CHECK_EQ(count(ROM_FRM_BUILD_REST_FRAME), 1);
    check_setup_voice(0, 2);                    /* voice A: N = 86, stride 430 */
```

with

```c
    CHECK_EQ(g_ncalls, 0);
    check_setup_voice();                        /* voice A: N = 86, stride 430 */
```

replace

```c
    CHECK_EQ(g_ram[CMD_PITCH_STEP], 8);
    CHECK_EQ(g_ncalls, 5);
    CHECK_EQ(g_ram[PROS_PITCH_CENTRE], 0xB0);   /* the A that cmd_apply_pitch hands to par_setup_voice */
    check_setup_voice(0, 2);
```

with

```c
    CHECK_EQ(g_ram[CMD_PITCH_STEP], 8);
    CHECK_EQ(g_ncalls, 0);
    CHECK_EQ(g_ram[PROS_PITCH_CENTRE], 0xB0);   /* the A that cmd_apply_pitch hands to par_setup_voice */
    check_setup_voice();
```

replace

```c
    CHECK_EQ(g_ram[CMD_SOURCE_PROSODY], 0x73);
    CHECK_EQ(g_ncalls, 5);
    check_setup_voice(0, 0);
```

with

```c
    CHECK_EQ(g_ram[CMD_SOURCE_PROSODY], 0x73);
    CHECK_EQ(g_ncalls, 0);
    check_setup_voice();
```

replace

```c
    CHECK_EQ(fw_rd16(&g_f, PAR_TABLE_PTR), 0xE5E3);
    CHECK_EQ(g_ncalls, 5);
    check_setup_voice(0, 0);
```

with

```c
    CHECK_EQ(fw_rd16(&g_f, PAR_TABLE_PTR), 0xE5E3);
    CHECK_EQ(g_ncalls, 0);
    check_setup_voice();
```

replace

```c
        CHECK_EQ(g_ncalls, 5);
        CHECK_EQ(g_ram[PROS_PITCH_CENTRE], (uint8_t)(k_want[v - 1].pitch << 4));
        check_setup_voice(0, 0xFF);
```

with

```c
        CHECK_EQ(g_ncalls, 0);
        CHECK_EQ(g_ram[PROS_PITCH_CENTRE], (uint8_t)(k_want[v - 1].pitch << 4));
        check_setup_voice();
```

replace

```c
    CHECK_EQ(g_ram[CMD_PROSODY], 3);
    CHECK_EQ(g_ncalls, 5);
    check_setup_voice(0, 0);
```

with

```c
    CHECK_EQ(g_ram[CMD_PROSODY], 3);
    CHECK_EQ(g_ncalls, 0);
    check_setup_voice();
```

replace

```c
    CHECK_EQ(g_ram[CMD_PROSODY], 1);
    CHECK_EQ(g_ncalls, 5);
```

with

```c
    CHECK_EQ(g_ram[CMD_PROSODY], 1);
    CHECK_EQ(g_ncalls, 0);
    check_setup_voice();
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw"`
Expected: the build stops with (the proof run):

```
tests\test_fw_cmd.c(11): fatal error C1083: Cannot open include file: 'firmware/frm_build.h': No such file or directory
tests\test_fw_frm.c(8): fatal error C1083: Cannot open include file: 'firmware/frm_build.h': No such file or directory
tests\test_fw_par.c(12): fatal error C1083: Cannot open include file: 'firmware/frm_build.h': No such file or directory
```

- [ ] **Step 3: Implement**

Create `src/firmware/frm_build.h`:

```c
#ifndef FW_FRM_BUILD_H
#define FW_FRM_BUILD_H
/* The frame builder (ROM $B41D-$B44F and $B917-$B982), decompiled (M4.5 spec 2.2, 2.6): the rest frame that
   par_setup_voice builds and copies into both frame buffers, and the steps that finish a producer frame (the frame
   at isr_fill_ptr) after the tracks: the snapshot of its parameter bytes, the mark-space clamp of b11 and the formant
   raise at high pitch codes. Each routine takes the registers at entry in r and leaves in r the A, X and Y that the
   ROM leaves at its RTS. r->p is not used: none reads an entry flag, and no caller reads a flag after any of them.
   The producer frame is read with fw_ind_y and written with fw_wr_ind_y on isr_fill_ptr; ROM tables are read with
   fw_rom8 at their ROM addresses. */
#include "fw.h"

#define FRM_PAUSE_PHONEME     0x3B   /* $B428 LDY #$3B: the pause phoneme, whose FN, F1, F2, F3 the rest frame takes */
#define FRM_MARK_SPACE_CAP    0xA0   /* $B93C: b11 is at most 160 - 2 x b10 (unsigned) ... */
#define FRM_MARK_SPACE_BASE   0x2F   /* $B947: ... at least 47 - 2 x b10 unless that has bit 7 set ... */
#define FRM_MARK_SPACE_FLOOR  0x0B   /* $B954: ... and at least 11 */
#define FRM_RAISE_PITCH       0x20   /* $B962: pitch codes b10 from 32 up raise F1 and F2 */

/* $B41D: frm_rest_frame ($2000) = frm_template, copied 1-based for Y = 20 down to 1; then its FN, F1, F2, F3 (b1, b3,
   b5, b7, X = 1, 3, 5, 7) = row FRM_PAUSE_PHONEME of the target rows at par_row_ptr, moved on one parameter block
   by par_next_param after each; then par_row_ptr = par_params_ptr. par_setup_voice leaves par_row_ptr at
   par_params_ptr (P1's target row) before the call. r in: not read (Y and X are loaded first). r out: A =
   par_params_ptr's high byte, X = 9 (the INX / INX loop ends at CPX #8), Y = FRM_PAUSE_PHONEME. */
void frm_build_rest_frame(fw *f, fw_regs *r);

/* $B445: the 20 bytes of frm_rest_frame into the producer frame, Y = 19 down to 0 (DEY / BPL). r in: X passes
   through. r out: A = frm_rest_frame[0], X unchanged, Y = $FF. */
void frm_copy_rest_frame(fw *f, fw_regs *r);

/* $B917: frm_snap_prev = frm_snap (1-based, Y = 10 down to 1); then frm_snap[X - 1] = the producer frame's byte
   frm_snapshot_map[X - 1], X = 10 down to 1 (b10, AHF, A3, A2, A1, ALF, FN, F1, F2, F3 in frm_snap[0..9]).
   r in: not read. r out: A = the frame byte of the last entry read (frm_snapshot_map[0], the pitch code b10), X = 0,
   Y = that map entry. */
void frm_snapshot(fw *f, fw_regs *r);

/* $B930: with c = b10 (the pitch code), FW_TMP_75 = 2c (8 bits, ASL). b11 = @M (cmd_mark_space); then b11 =
   160 - 2c when b11 is above it (CMP / BCS, unsigned; 160 - 2c wraps in 8 bits for c > 80); then b11 = 47 - 2c
   when that 8-bit difference has bit 7 clear (BMI) and is not below b11 (CMP / BCC); then b11 = 11 when b11 is not
   above 11. Each write goes to the frame (up to four). r in: X passes through. r out: A = $0B, X unchanged,
   Y = FRAME_MARK_SPACE (11). */
void frm_clamp_mark_space(fw *f, fw_regs *r);

/* $B95D: d = b10 - 32 (8 bits). With d's bit 7 set (BPL not taken) it returns at once: A = d, Y = FRAME_PITCH (10).
   Otherwise FW_TMP_75 = d >> 2; F1 (b3) = par_clamp(F1 + (d >> 2)), then F2 (b5) = par_clamp(F2 + (d >> 3)), each
   sum 8 bits (CLC / ADC). r in: X passes through (par_clamp keeps it). r out: A = d and Y = 10 (the early return),
   else A = the new F2 and Y = FRAME_F2 (5); X unchanged. */
void frm_raise_formants(fw *f, fw_regs *r);

#endif
```

Create `src/firmware/frm_build.c`:

```c
#include "frm_build.h"
#include "par_segment.h"
#include "data/fw_data.h"

/* ---- frm_build_rest_frame $B41D ---- */

/* $B41D-$B426: LDA $B782,Y / STA $1FFF,Y for Y = 20 down to 1 (DEY / BNE): frm_rest_frame = frm_template.
   Leaves A = frm_template[0] and Y = 0. */
static void copy_template(fw *f, fw_regs *r)
{
    r->y = FRAME_SIZE;
    do {
        r->a = fw_rom8(f, (uint16_t)(FRM_TEMPLATE - 1 + r->y));
        fw_wr8(f, (uint16_t)(FRM_REST_FRAME - 1 + r->y), r->a);
        r->y--;
    } while (r->y != 0);
}

/* $B428-$B438: Y = the pause phoneme; for X = 1, 3, 5, 7 (INX / INX, CPX #8 / BCC): frm_rest_frame[X] =
   (par_row_ptr),Y, then par_next_param (the same row of the next parameter). Leaves X = 9. */
static void pause_formants(fw *f, fw_regs *r)
{
    r->y = FRM_PAUSE_PHONEME;
    r->x = FRAME_FN;
    do {
        r->a = fw_ind_y(f, PAR_ROW_PTR, r->y);
        fw_wr8(f, (uint16_t)(FRM_REST_FRAME + r->x), r->a);
        par_next_param(f, r);
        r->x += 2;
    } while (r->x < FRAME_F3 + 1);
}

void frm_build_rest_frame(fw *f, fw_regs *r)
{
    copy_template(f, r);
    pause_formants(f, r);
    r->a = fw_rd8(f, PAR_PARAMS_PTR);          /* $B43A-$B444: par_row_ptr = par_params_ptr */
    fw_wr8(f, PAR_ROW_PTR, r->a);
    r->a = fw_rd8(f, PAR_PARAMS_PTR + 1);
    fw_wr8(f, PAR_ROW_PTR + 1, r->a);
}

/* ---- frm_copy_rest_frame $B445 ---- */

void frm_copy_rest_frame(fw *f, fw_regs *r)
{
    r->y = FRAME_SIZE - 1;                     /* LDY #$13 */
    do {
        r->a = fw_rd8(f, (uint16_t)(FRM_REST_FRAME + r->y));
        fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);
        r->y--;
    } while (!(r->y & 0x80));                  /* DEY / BPL */
}

/* ---- frm_snapshot $B917 ---- */

/* $B917-$B920: LDA $16CC,Y / STA $16DC,Y for Y = 10 down to 1: frm_snap_prev = frm_snap. */
static void save_snapshot(fw *f, fw_regs *r)
{
    r->y = 10;
    do {
        r->a = fw_rd8(f, (uint16_t)(FRM_SNAP - 1 + r->y));
        fw_wr8(f, (uint16_t)(FRM_SNAP_PREV - 1 + r->y), r->a);
        r->y--;
    } while (r->y != 0);
}

/* $B922-$B92D: for X = 10 down to 1: Y = frm_snapshot_map[X - 1] ($B772,X), frm_snap[X - 1] ($16CC,X) = the
   producer frame's byte Y. */
static void take_snapshot(fw *f, fw_regs *r)
{
    r->x = 10;
    do {
        r->y = fw_rom8(f, (uint16_t)(FRM_SNAPSHOT_MAP - 1 + r->x));
        r->a = fw_ind_y(f, ISR_FILL_PTR, r->y);
        fw_wr8(f, (uint16_t)(FRM_SNAP - 1 + r->x), r->a);
        r->x--;
    } while (r->x != 0);
}

void frm_snapshot(fw *f, fw_regs *r)
{
    save_snapshot(f, r);
    take_snapshot(f, r);
}

/* ---- frm_clamp_mark_space $B930 ---- */

/* $B930-$B93A: Y = 10, A = b10; INY; ASL A: FW_TMP_75 = 2c; b11 = @M. */
static void mark_space(fw *f, fw_regs *r)
{
    r->y = FRAME_PITCH;
    r->a = fw_ind_y(f, ISR_FILL_PTR, r->y);
    r->y++;
    r->a = (uint8_t)(r->a << 1);
    fw_wr8(f, FW_TMP_75, r->a);
    r->a = fw_rd8(f, CMD_MARK_SPACE);
    fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);
}

/* $B93C-$B945: A = 160 - 2c (SEC / SBC, 8 bits); CMP b11 / BCS: written when A < b11 (unsigned). */
static void upper_cap(fw *f, fw_regs *r)
{
    r->a = (uint8_t)(FRM_MARK_SPACE_CAP - fw_rd8(f, FW_TMP_75));
    if (r->a < fw_ind_y(f, ISR_FILL_PTR, r->y))
        fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);
}

/* $B947-$B952: A = 47 - 2c (SEC / SBC, 8 bits); BMI: skipped when bit 7 is set; CMP b11 / BCC: written when
   A >= b11 (unsigned). */
static void lower_bound(fw *f, fw_regs *r)
{
    r->a = (uint8_t)(FRM_MARK_SPACE_BASE - fw_rd8(f, FW_TMP_75));
    if (!(r->a & 0x80) && r->a >= fw_ind_y(f, ISR_FILL_PTR, r->y))
        fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);
}

/* $B954-$B95A: A = 11; CMP b11 / BCC: written when 11 >= b11. */
static void floor_11(fw *f, fw_regs *r)
{
    r->a = FRM_MARK_SPACE_FLOOR;
    if (r->a >= fw_ind_y(f, ISR_FILL_PTR, r->y))
        fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);
}

void frm_clamp_mark_space(fw *f, fw_regs *r)
{
    mark_space(f, r);
    upper_cap(f, r);
    lower_bound(f, r);
    floor_11(f, r);
}

/* ---- frm_raise_formants $B95D ---- */

/* LDY #n; CLC; ADC (fill),Y; JSR par_clamp; STA (fill),Y: frame byte n += A, clamped to 1-63 ($B96B-$B973 for F1,
   $B978-$B980 for F2). */
static void raise(fw *f, fw_regs *r, uint8_t n)
{
    r->y = n;
    r->a = (uint8_t)(r->a + fw_ind_y(f, ISR_FILL_PTR, r->y));
    par_clamp(f, r);
    fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);
}

void frm_raise_formants(fw *f, fw_regs *r)
{
    r->y = FRAME_PITCH;                        /* $B95D-$B966: SEC; SBC #$20; BPL, else RTS */
    r->a = (uint8_t)(fw_ind_y(f, ISR_FILL_PTR, r->y) - FRM_RAISE_PITCH);
    if (r->a & 0x80)
        return;
    r->a >>= 2;                                /* $B967-$B969: LSR, LSR, STA $75 */
    fw_wr8(f, FW_TMP_75, r->a);
    raise(f, r, FRAME_F1);
    r->a = (uint8_t)(fw_rd8(f, FW_TMP_75) >> 1);   /* $B975-$B977: LDA $75; LSR */
    raise(f, r, FRAME_F2);
}
```

`src/firmware/par_start.c`: replace

```c
#include "par_start.h"
#include "fw_ops.h"
#include "par_addr.h"
```

with

```c
#include "par_start.h"
#include "frm_build.h"
#include "fw_ops.h"
```

replace

```c
/* $B3F5-$B40C: LDM isr_play_ptr = isr_frame_a (high byte first), isr_fill_ptr = isr_frame_b, isr_swap_phase = 2.
   Then the rest frame is built at frm_rest_frame, copied to the fill frame, and its b11 clamped. */
```

with

```c
/* $B3F5-$B40C: LDM isr_play_ptr = isr_frame_a (high byte first), isr_fill_ptr = isr_frame_b, isr_swap_phase = 2.
   Then the rest frame is built at frm_rest_frame, copied to the fill frame, and its b11 clamped (the C frame
   builder, M4.5). */
```

replace

```c
    fw_rom_call(f, ROM_FRM_BUILD_REST_FRAME, r, FW_ROM_CALL_BUDGET);    /* returns X = 9 */
    fw_rom_call(f, ROM_FRM_COPY_REST_FRAME, r, FW_ROM_CALL_BUDGET);
    fw_rom_call(f, ROM_FRM_CLAMP_MARK_SPACE, r, FW_ROM_CALL_BUDGET);
```

with

```c
    frm_build_rest_frame(f, r);                /* returns X = 9 */
    frm_copy_rest_frame(f, r);
    frm_clamp_mark_space(f, r);
```

replace

```c
    fw_rom_call(f, ROM_FRM_COPY_REST_FRAME, r, FW_ROM_CALL_BUDGET);
    fw_rom_call(f, ROM_FRM_CLAMP_MARK_SPACE, r, FW_ROM_CALL_BUDGET);    /* leaves A = Y = $0B */
```

with

```c
    frm_copy_rest_frame(f, r);
    frm_clamp_mark_space(f, r);                /* leaves A = Y = $0B */
```

`src/firmware/par_start.h`: replace

```c
   (never read) = table + N and par_params_ptr = table + 2N. par_param_stride = 5N in 16 bits. Then the rest frames:
   with isr_play_ptr = isr_frame_a, isr_fill_ptr = isr_frame_b and isr_swap_phase = 2, the ROM frm_build_rest_frame,
   frm_copy_rest_frame and frm_clamp_mark_space; with the two buffers swapped and isr_swap_phase = 1,
   frm_copy_rest_frame and frm_clamp_mark_space again. These M4.5 routines are called through fw_rom_call with the
   registers the ROM has at each JSR.
   r in: Y passes through to frm_build_rest_frame, which does not read it (A and X are written first). r out: the
   registers the last ROM call leaves: A = Y = $0B (frm_clamp_mark_space) and X = 9 (frm_build_rest_frame's). */
```

with

```c
   (never read) = table + N and par_params_ptr = table + 2N. par_param_stride = 5N in 16 bits. Then the rest frames:
   with isr_play_ptr = isr_frame_a, isr_fill_ptr = isr_frame_b and isr_swap_phase = 2, frm_build_rest_frame,
   frm_copy_rest_frame and frm_clamp_mark_space; with the two buffers swapped and isr_swap_phase = 1,
   frm_copy_rest_frame and frm_clamp_mark_space again. These are the C frame builder's (M4.5), called directly with
   the registers flowing from one to the next as the ROM's do.
   r in: Y passes through to frm_build_rest_frame, which does not read it (LDY #$14 first). r out: the
   registers the last call leaves: A = Y = $0B (frm_clamp_mark_space) and X = 9 (frm_build_rest_frame's). */
```

`src/firmware/par_addr.h`: replace

```c
/* ROM code addresses of the playback set-up (M4.4a spec 2.2) and the frame loop (M4.4b spec 2.2), and of the
   frame-builder, interrupt and serial routines they call (M4.4a 2.4, M4.4b 2.4), checked against
   build/ghidra740/dump and the JSR/JMP operands in the ROM image. A caller in parentheses stays ROM. */
```

with

```c
/* ROM code addresses of the playback set-up (M4.4a spec 2.2), the frame loop (M4.4b spec 2.2) and the frame
   builder (M4.5 spec 2.2), and of the interrupt and serial routines they call (M4.4a 2.4, M4.4b 2.4), checked
   against build/ghidra740/dump and the JSR/JMP operands in the ROM image. A caller in parentheses stays ROM. */
```

replace

```c
#define ROM_FRM_BUILD_REST_FRAME   0xB41D   /* M4.5; called by par_setup_voice $B404 */
#define ROM_FRM_COPY_REST_FRAME    0xB445   /* M4.5; called by par_setup_voice $B407, $B416, par_play $B8DA */
```

with

```c
#define ROM_FRM_BUILD_REST_FRAME   0xB41D   /* called by par_setup_voice $B404 */
#define ROM_FRM_COPY_REST_FRAME    0xB445   /* called by par_setup_voice $B407, $B416, par_play $B8DA */
```

replace

```c
#define ROM_FRM_SNAPSHOT           0xB917   /* M4.5; called by par_play $B8DD (and frm_finish_frame $B9BA) */
#define ROM_FRM_CLAMP_MARK_SPACE   0xB930   /* M4.5; called by par_setup_voice $B40A, $B419, par_play $B8E0 (and
                                               frm_finish_frame $B9BD) */
```

with

```c
#define ROM_FRM_SNAPSHOT           0xB917   /* called by par_play $B8DD (and frm_finish_frame $B9BA) */
#define ROM_FRM_CLAMP_MARK_SPACE   0xB930   /* called by par_setup_voice $B40A, $B419, par_play $B8E0 (and
                                               frm_finish_frame $B9BD) */
#define ROM_FRM_RAISE_FORMANTS     0xB95D   /* called by (frm_finish_frame $B9C0) */
```

(`par_start.c` no longer includes `par_addr.h`: its five `ROM_FRM_*` calls were the only uses.)

`src/engine/thunks.c`: replace

```c
#include "firmware/par_play.h"
```

with

```c
#include "firmware/par_play.h"
#include "firmware/frm_build.h"
```

replace

```c
   at X = 9 ($B436 CPX #8), frm_clamp_mark_space ends with LDA #$0B and Y = $0A + 1 ($B930-$B954). The C's ROM calls
   overwrite the machine's registers, so all three are set from the C. Its one ROM caller, cmd_apply_pitch ($A845),
```

with

```c
   at X = 9 ($B436 CPX #8), frm_clamp_mark_space ends with LDA #$0B and Y = $0A + 1 ($B930-$B954); the C calls the C
   frame builder (M4.5), and all three are set from the C. Its one ROM caller, cmd_apply_pitch ($A845),
```

replace

```c
static void par_setup_voice_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_setup_voice(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

with

```c
static void par_setup_voice_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_setup_voice(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* frm_build_rest_frame $B41D: nothing in (LDY #$14 first; X and A are loaded before they are read). A =
   par_params_ptr's high byte, X = 9 and Y = $3B out (the RTS at $B444). Its one ROM caller, par_setup_voice ($B404),
   calls frm_copy_rest_frame next, which loads Y and A first ($B445 LDY #$13, $B447 LDA) and leaves X; so do the
   frm_clamp_mark_space and frm_copy_rest_frame calls after it ($B40A, $B416, $B419), so X reaches par_setup_voice's
   callers (its thunk). No caller reads a flag. The C par_setup_voice calls the C (M4.5), so the hook fires only
   while the ROM par_setup_voice runs. Declared: A, X, Y. */
static void frm_build_rest_frame_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_build_rest_frame(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* frm_copy_rest_frame $B445: X passes through (Y and A are loaded first). A = frm_rest_frame[0], X untouched and Y =
   $FF out. par_setup_voice ($B407, $B416) calls frm_clamp_mark_space next, which loads Y and A first ($B930 LDY
   #$0A, $B932 LDA) and leaves X, so X reaches par_setup_voice's callers (its thunk); par_play ($B8DA) calls
   frm_snapshot next, which loads Y, A and X first ($B917 LDY, $B919 LDA, $B922 LDX). No caller reads a flag.
   Declared: A, X, Y. */
static void frm_copy_rest_frame_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_copy_rest_frame(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

replace

```c
static void par_play_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_play(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

with

```c
static void par_play_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    par_play(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* frm_snapshot $B917: nothing in (LDY #$0A first, then LDA; LDX #$0A at $B922). A = the producer frame's b10, X = 0
   and Y = 10 (frm_snapshot_map[0], the last entry read) out. par_play ($B8DD) and frm_finish_frame ($B9BA) call
   frm_clamp_mark_space next, which loads Y and A first and leaves X: in frm_finish_frame X is loaded again at $B9C5
   (LDX frm_source_index) after frm_raise_formants, which keeps it; in par_play it passes frm_transition_flags,
   the waits (isr_par_ready, PHA / PLA) and the next frm_copy_rest_frame, which keep it, or cmd_ev_sound_end and
   the last isr_par_ready, which return it as par_play's exit X (its thunk). No caller reads a flag. Declared: A,
   X, Y. */
static void frm_snapshot_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_snapshot(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* frm_clamp_mark_space $B930: X passes through (Y and A are loaded first). A = $0B, X untouched and Y = 11 out.
   par_setup_voice calls frm_copy_rest_frame after $B40A (LDY and LDA first, X kept) and returns after $B419, so A,
   X and Y reach its callers (its thunk); par_play ($B8E0) calls frm_transition_flags next, which loads A and Y first
   ($BA3F LDA #0, $BA44 LDY #9) and keeps X; frm_finish_frame ($B9BD) calls frm_raise_formants next, which loads Y and
   A first and keeps X, then loads X ($B9C5). No caller reads a flag. Declared: A, X, Y. */
static void frm_clamp_mark_space_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_clamp_mark_space(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* frm_raise_formants $B95D: X passes through (Y and A are loaded first; par_clamp keeps X). A = b10 - 32 and Y = 10
   when that has bit 7 set (the RTS at $B966), else A = the new F2 and Y = 5; X untouched. Its one caller,
   frm_finish_frame ($B9C0), loads Y, X and A next ($B9C3 LDY #$10, $B9C5 LDX, $B9C8 LDA): none is read. No caller
   reads a flag. Declared: A, X, Y. */
static void frm_raise_formants_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_raise_formants(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

replace

```c
   it first). No caller reads a flag (INX, CPX, DEC follow). Reached from ROM in every mode through
   frm_build_rest_frame (M4.5). Declared: A, X, Y. */
```

with

```c
   it first). No caller reads a flag (INX, CPX, DEC follow). Every caller is C since M4.5 (frm_build_rest_frame), so
   the hook fires only while a caller runs as ROM (the "alone" modes). Declared: A, X, Y. */
```

replace

```c
   caller reads a flag (STA, CPX or JMP follow). Reached from ROM in every mode through frm_raise_formants (M4.5).
   Declared: A, X, Y. */
```

with

```c
   caller reads a flag (STA, CPX or JMP follow). Every caller is C since M4.5 (frm_raise_formants), so the hook
   fires only while a caller runs as ROM (the "alone" modes). Declared: A, X, Y. */
```

replace

```c
THUNK(par_setup_voice)
THUNK(par_play)
THUNK(pros_microprosody)
```

with

```c
THUNK(par_setup_voice)
THUNK(frm_build_rest_frame)
THUNK(frm_copy_rest_frame)
THUNK(par_play)
THUNK(frm_snapshot)
THUNK(frm_clamp_mark_space)
THUNK(frm_raise_formants)
THUNK(pros_microprosody)
```

replace

```c
    { "par_setup_voice",    0xB3A4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_setup_voice },
    { "par_play",           0xB877, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_play, true },
```

with

```c
    { "par_setup_voice",    0xB3A4, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_setup_voice },
    { "frm_build_rest_frame", 0xB41D, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_build_rest_frame },
    { "frm_copy_rest_frame", 0xB445, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_copy_rest_frame },
    { "par_play",           0xB877, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_par_play, true },
    { "frm_snapshot",       0xB917, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_snapshot },
    { "frm_clamp_mark_space", 0xB930, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_clamp_mark_space },
    { "frm_raise_formants", 0xB95D, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_raise_formants },
```

The table stays in address order (69 rows: `frm_build_rest_frame` and `frm_copy_rest_frame` between `par_setup_voice` $B3A4 and `par_play` $B877, the other three between `par_play` and `pros_microprosody` $B983); the `THUNK` lines follow it. `frm_build.c` is a new basename under `src/` and `tests/`.

- [ ] **Step 4: Faithfulness check against the dumps**

Read each dump (`build/ghidra740/dump/`, read-only) side by side with its C, instruction by instruction:
- `rom_B41D_frm_build_rest_frame.txt`: $B41D–$B426 → `copy_template` (`LDY #$14`, `LDA $B782,Y` = `fw_rom8(FRM_TEMPLATE - 1 + Y)`, `STA $1FFF,Y` = `FRM_REST_FRAME - 1 + Y`, `DEY` / `BNE`); $B428–$B438 → `pause_formants` (`LDY #$3B`, `LDX #1`, `LDA ($32),Y` = `fw_ind_y(PAR_ROW_PTR)`, `STA $2000,X`, `JSR par_next_param` (C), `INX` / `INX`, `CPX #8` / `BCC`: while X < 8); $B43A–$B444 in `frm_build_rest_frame`.
- `rom_B445_frm_copy_rest_frame.txt`: `LDY #$13`, `LDA $2000,Y` / `STA ($2C),Y` (`fw_wr_ind_y(ISR_FILL_PTR)`), `DEY` / `BPL` (bit 7 of Y).
- `rom_B917_frm_snapshot.txt`: $B917–$B920 → `save_snapshot` ($16CC,Y → $16DC,Y = `FRM_SNAP - 1 + Y` → `FRM_SNAP_PREV - 1 + Y`); $B922–$B92D → `take_snapshot` (`LDY $B772,X` = `fw_rom8(FRM_SNAPSHOT_MAP - 1 + X)`, `LDA ($2C),Y`, `STA $16CC,X`).
- `rom_B930_frm_clamp_mark_space.txt`: $B930–$B93A → `mark_space` (`LDY #$0A`, `LDA`, `INY`, `ASL A` 8 bits, `STA $75` = `FW_TMP_75`, `LDA cmd_mark_space`, `STA ($2C),Y`); $B93C–$B945 → `upper_cap` (`SEC; SBC $75` 8 bits, `CMP` / `BCS`: write when A < b11); $B947–$B952 → `lower_bound` (`BMI` on bit 7 of the 8-bit difference, `CMP` / `BCC`: write when A ≥ b11); $B954–$B95A → `floor_11` (`CMP` / `BCC`: write when 11 ≥ b11).
- `rom_B95D_frm_raise_formants.txt`: $B95D–$B966 (`SEC; SBC #$20`, `BPL`, `RTS`) in `frm_raise_formants`; `LSR`, `LSR`, `STA $75`; $B96B–$B973 and $B978–$B980 → `raise` (`LDY #n`, `CLC; ADC ($2C),Y` 8 bits, `JSR par_clamp` (C), `STA ($2C),Y`); $B975 `LDA $75; LSR` between them.
- `rom_B3A4_par_setup_voice.txt` $B3F5–$B41C against `first_rest_frame` and `second_rest_frame`: the five `JSR`s are now the five direct calls, in the same order, with the registers flowing.
- No RAM write in the C that the ROM does not make, none missing (the list in "ROM facts checked").

Confirm the callers with:

```bash
python -c "import re,os;d=open(os.environ['APOLLO2_ROM'],'rb').read();[print(hex(a),hex(op),[hex(m.start()) for m in re.finditer(re.escape(bytes([op,a&255,a>>8])),d)]) for a in (0xB41D,0xB445,0xB917,0xB930,0xB95D) for op in (0x20,0x4C)]"
```

Expected:

```
0xb41d 0x20 ['0xb404']
0xb41d 0x4c []
0xb445 0x20 ['0xb407', '0xb416', '0xb8da']
0xb445 0x4c []
0xb917 0x20 ['0xb8dd', '0xb9ba']
0xb917 0x4c ['0xc0d7']
0xb930 0x20 ['0xb40a', '0xb419', '0xb8e0', '0xb9bd']
0xb930 0x4c []
0xb95d 0x20 ['0xb9c0']
0xb95d 0x4c []
```

`0xc0d7` is not a `JMP`: it is the operand byte $4C of `STA $174C,Y` at $C0D6 (`rom_C0D2_par_plan_tracks.txt`). Then the relative branches into the five entries and the byte before each:

```bash
python -c "
import os
d=open(os.environ['APOLLO2_ROM'],'rb').read()
br={0x10,0x30,0x50,0x70,0x80,0x90,0xB0,0xD0,0xF0}
s=lambda b:b-256 if b>127 else b
for t in (0xB41D,0xB445,0xB917,0xB930,0xB95D):
  h=[hex(p) for p in range(0x8000,0xFFFE) if (d[p] in br or d[p]&0x1F in (3,0x13)) and p+2+s(d[p+1])==t]
  h+=[hex(p) for p in range(0x8000,0xFFFD) if d[p]&0x1F in (7,0x17) and p+3+s(d[p+2])==t]
  print(hex(t),h,hex(d[t-1]))
"
```

Expected:

```
0xb41d [] 0x60
0xb445 [] 0x60
0xb917 [] 0xb8
0xb930 [] 0x60
0xb95d [] 0x60
```

$B916 ($B8) is the last byte of `JMP par_play` at $B914; every other entry follows an `RTS`.

- [ ] **Step 5: Run the tests**

Run, in order: `cmd //c ".\build.bat test fw"`, then the full suite `cmd //c ".\build.bat test"`.
Expected:
- the `fw` run: the build passes with no warnings, then

```
suite_fw                     ok
suite_fw_lts                 ok
suite_fw_tn                  ok
suite_fw_cmd                 ok
suite_fw_pros                ok
suite_fw_par                 ok
suite_fw_frm                 ok
7 suite(s), 0 failure(s), 0 skip(s)
```

- the full run (448 s in the proof run): `34 suite(s), 0 failure(s), 0 skip(s)`, no warnings, the firmware syntax check passing; `firmware_data` `48496 converted bytes compared with the ROM, 0 differ`.
- `diff_lts`: 67 lines `<name> N calls checked`, each N > 0, 0 mismatches. The five new lines (the proof run), in table order:

```
  frm_build_rest_frame 121 calls checked
  frm_copy_rest_frame  782 calls checked
  frm_snapshot         19113 calls checked
  frm_clamp_mark_space 19331 calls checked
  frm_raise_formants   18626 calls checked
```

  `frm_build_rest_frame` equals `par_setup_voice` (121: one call per voice set-up). Every other line is identical to Task 1's Step 16; `1 mid-clause flush calls skipped (golden_hybrid covers them)`, `2 unwound calls compared`.
- `diff_play`: as Task 1's Step 16 (`boot_loop_play 179 calls checked, 48 with both injections`, `par_play 126 calls checked, 48 with both injections`, `$B8A5 (abort) reached in 96 calls, $B89C (reply due) in 96`), 0 mismatches.
- `golden_hybrid`: `48 corpus files x 70 modes (all, then each routine alone)`, all identical.
- `suite_golden`: `hybrid timer scale 1/4: 105 frames, 11699789 CPU cycles`, `hybrid timer scale 3/1: 105 frames, 997725 CPU cycles` (the voice set-up's three frame calls take no emulated time now; the rom-engine lines are unchanged).
- `suite_synth`: `corpus peak plain 20522 (-4.1 dBFS)`, `corpus peak b11 16038 (-6.2 dBFS)`.

If `diff_lts` reports a mismatch, find the root cause against the dump and fix the C faithfully. Never loosen the comparison or drop a declared output.

- [ ] **Step 6: Commit**

```bash
git add src/firmware/frm_build.h src/firmware/frm_build.c src/firmware/par_start.c src/firmware/par_start.h src/firmware/par_addr.h src/engine/thunks.c tests/test_fw_frm.c tests/test_fw_par.c tests/test_fw_cmd.c
git commit -m "The frame builder's leaves in C: frm_build_rest_frame, frm_copy_rest_frame, frm_snapshot, frm_clamp_mark_space and frm_raise_formants, with hooks; the C par_setup_voice calls them directly (M4.5 2.2-2.6, 4.6)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: The finish in C: `frm_transition_flags` $BA3F and `frm_finish_frame` $B9B5, with their hooks; `par_play` calls the frame builder directly; the branch probe (spec §2.2–§2.6, §4.5–§4.7)

**ROM facts checked** (`rom_B9B5_frm_finish_frame.txt` and `rom_BA3F_frm_transition_flags.txt` instruction by instruction; the callers `rom_B877_par_play.txt` ($B8BB–$B8FA, $B908–$B914) and the callees of Task 2; `rom_BC80_cmd_ev_sound_end.txt` and `rom_C5D6_isr_par_ready.txt` for what follows the rest frames; the ROM image for the scans):
- **Sizes agree with spec §2.2**: 138/68 and 173/78 bytes/instructions (the seven routines: 470 bytes, 230 instructions). Both entries follow an `RTS`; `frm_finish_frame` is called only at $B8C3 and $B90E, `frm_transition_flags` at $B8E3 and $BA36 (Step 4).
- **`frm_finish_frame`** ($B9B5–$BA3E; spec §2.5, §2.6 confirmed): `PHA`, `TXA`, `PHA`, `TYA`, `PHA`; the three `JSR`s; `LDY #$10`, `LDX frm_source_index`, `LDA $BAEC,X`, `STA ($2C),Y` (b16), `INY`, `LDX frm_filter_index`, `LDA $BAF0,X`, `STA ($2C),Y` (b17), `DEY`, `DEY` (Y = 15); `LDX #5`, `LDA $16CD,X`, `DEX`, `SEC`, `SBC $16CD,X`, `SEC`, `SBC #4`: d = `frm_snap[5]` − `frm_snap[4]` − 4 = ALF − A1 − 4 (8 bits, the snapshot taken before the clamp and the raise); `BEQ` / `BMI` to $BA29; `STA frm_level_diff`; `LDX #7`, `LDA $16CD,X`, `SEC`, `SBC #$21`: e = F1 − 33; `BEQ` / `BMI` to $BA29; `STA frm_f1_excess`, `TAX`, `LDA $B473,X`, `CMP frm_level_diff`, `BMI` to $BA1A (`LDA #$FF`); else `LDA #0`, `LDX #8`, eight times `LSR frm_f1_excess` / `BCC` / `CLC` / `ADC frm_level_diff` / `ASL frm_level_diff` / `DEX` / `BNE` (each `ADC`'s carry is overwritten by the next shift: lost); `TAX`, `LDA $B573,X`, `STA frm_b14`, `LDA $B673,X`, `JMP $BA2E`; $BA29 `LDA #0`, `STA frm_b14`; $BA2E `STA ($2C),Y` (b15 = the high byte or 0), `DEY`, `LDA frm_b14`, `STA ($2C),Y` (b14); `JSR frm_transition_flags`; `PLA`, `TAY`, `PLA`, `TAX`, `PLA`, `RTS`. Exit A, X, Y = entry; the five pushes and the `JSR`s' return addresses are dead stack at the `RTS`, which `diff_lts` and `diff_play` exclude.
- **The product never overflows on the ROM's paths**: d and e are 1..127 when the multiply runs, `frm_product_limit[e]` = 255 / e, and `CMP` / `BMI` lets the multiply run only when limit − d has bit 7 clear, which for these ranges means d ≤ limit, so d × e ≤ 255; e = 1 (limit 255) always gives the index $FF (255 − d is $80..$FE). The C still adds in 8 bits, as `ADC` does.
- **`frm_transition_flags`** ($BA3F–$BAEB): `LDA #0` / `STA frm_flags`; `LDY #9`, the loop $BA46 (`LDA $16DD,Y`, `SEC`, `SBC $16CD,Y`, `CMP #$10` / `BPL`, `CMP #$F1` / `BMI`, `DEY`, `CPY #5` / `BNE`); the loop $BA5A with Y = 5 (`CMP #$0C` / `BPL`, `CMP #$F5` / `BMI`, `DEY` / `BNE`), then `JMP $BA77`; $BA6F `ORA #$80`. $BA77: `LDY #5`, `LDA $16CD,Y`, `CMP #1` / `BEQ $BA8E`; the difference `prev − snap` then `CMP #$1C` / `BPL` (or `CMP #$13` / `BPL` at the silent value 1), `DEY` / `BNE`; $BA9F `ORA #$40`. $BAA7 `LDM #0,$75`; Y = 5..1: `LDA $16CD,Y`, `SEC`, `SBC $16DD,Y` (snap − prev), `CMP #$0C` / `BMI`, `INC $75`; `LDA $75`, `CMP #2` / `BMI`; `ORA #$10`. $BACA: `frm_snap_prev[0]` − `frm_snap[0]`, `CMP #7` / `BPL`, `CMP #$FA` / `BMI`; `ORA #8`. $BAE4 `LDA frm_flags`, `LDY #$0D`, `STA ($2C),Y`, `RTS`: A = `frm_flags`, Y = $0D, X untouched (spec §2.5). The windows' second constants are $F1, $F5 and $FA, i.e. −15, −11, −6 (spec §2.6 writes them as "−k"): the flag is set for d ≥ 16 or d ≤ −16 (frequencies), d ≥ 12 or d ≤ −12 (amplitudes), d ≥ 7 or d ≤ −7 (the pitch), each as bit 7 of the 8-bit `CMP` result.
- **What the callers read** (the thunk comments, Step 3): `frm_finish_frame`'s callers as `par_step_tracks`' (A, X and Y come back unchanged); `frm_transition_flags`' A and Y become `par_play`'s exit registers after the last rest frame (`cmd_ev_sound_end` keeps X and Y, and A unless it restores; `isr_par_ready` keeps all three). No caller reads a flag.
- **`par_play`** ($B8C3, $B90E, $B8DA–$B8E3): the six `fw_rom_call`s become direct calls, in the ROM's order, with `r` flowing; after this task `grep -rn "ROM_FRM_" src/firmware` finds only `par_addr.h` (spec §4.7 criterion 8). The callers' comments in `par_addr.h` lose their parentheses (no `frm_*` caller stays ROM); `FW_TMP_75`'s comment (`fw.h`) gains `frm_transition_flags`' rise count.
- **The unit tests of the frame loop** (`tests/test_fw_par.c`, spec §4.6): the mock had cases for `frm_finish_frame`, `frm_copy_rest_frame`, `frm_snapshot`, `frm_clamp_mark_space` and `frm_transition_flags`; they go (any call to them now fails). The C frame builder now writes the fill frame, so the tests check it: `test_play_tail` (the fill frame ends as the rest frame with b11 = 47 and b13 = 0; the first rest frame's `frm_flags` $C0 reaches `isr_par_ready` in A), `test_play_abort` (no frame built: the rest frame, b16 = 0 where `frm_finish_frame` would write $82), `test_play_segment` (the last rest frame's b16 and `frm_flags` 0), the zero-frame tests (the `isr_par_ready` waits stand where the `frm_finish_frame` calls were), `test_loop_play` (the fill pointer set to `isr_frame_b`, as `par_setup_voice` leaves it, and `par_play`'s exit A = 0). `check_rest_frames` counts one wait per rest frame and no `frm_*` call. **`tests/test_fw_tn.c`'s flush** (not in spec §4.6's list) runs the C `par_play` too: its mock loses the four rest-frame cases, the expected tail is 20 calls (was 32) and `isr_set_volume(0)` is call 13 of them (was 25); its `isr_fill_ptr` is set to `isr_frame_b`, because with the zeroed RAM's fill pointer $0000 the C rest frames would write the zero page ($0000–$0013: `ser_tx_*`, `cmd_spell`, `cmd_phonetic`, …), as the ROM would with that pointer; the counts it checks (1,379 `isr_par_ready` calls, `tn_resume_pos` 250) are unchanged.
- **`test_timing_invariance`** passes on both engines; its comment said `frm_finish_frame` was still ROM and now says the frame builder is C; the hybrid cycle counts drop (Step 6).

**Files:**
- Modify: `src/firmware/frm_build.h`, `src/firmware/frm_build.c`, `src/firmware/par_play.c`, `src/firmware/par_addr.h`, `src/firmware/fw.h` (the `FW_TMP_75` comment), `src/engine/thunks.c`, `tests/test_fw_frm.c`, `tests/test_fw_par.c`, `tests/test_fw_tn.c`, `tests/test_golden.c` (a comment)

**Where the code goes.** `src/firmware/frm_build.c`, after Task 2's routines:

| Helper | ROM |
|---|---|
| `minus` | every `CMP` / `BPL` / `BMI` of the two: bit 7 of the 8-bit difference |
| `moved` | `LDA $16DD,Y; SEC; SBC $16CD,Y` |
| `set_flag` | `LDA frm_flags; ORA #bit; STA frm_flags` ($BA6F, $BA9F, $BAC2, $BADC) |
| `jump_flag` | $BA44–$BA74 |
| `fall_flag` | $BA77–$BAA4 |
| `rise_flag` | $BAA7–$BAC7 |
| `pitch_flag` | $BACA–$BAE1 |
| `source_filter` | $B9C3–$B9D7 |
| `level_diff` | $B9D8–$B9E9 |
| `f1_excess` | $B9EC–$B9FB |
| `multiply` | $BA04–$BA17 |
| `product_word` | $B9FC–$BA26 |
| `b14_15` | $B9D8–$BA34, with $BA29–$BA34 |

`frm_transition_flags` holds $BA3F–$BA41 and $BAE4–$BAEB; `frm_finish_frame` the saves and restores ($B9B5–$B9B9, $BA39–$BA3E) and the four calls.

**Interfaces:**
- Consumes: Task 1 (`FRM_PRODUCT_LIMIT`, `FRM_B14_15_LO`/`_HI`, `FRM_SOURCE_CODES`, `FRM_FILTER_CODES`, `FRAME_*`); Task 2 (`frm_snapshot`, `frm_clamp_mark_space`, `frm_raise_formants`, `frm_copy_rest_frame`, the `test_fw_frm.c` harness); `fw_inc8` (`fw_ops.h`); `FRM_SOURCE_INDEX`, `FRM_FILTER_INDEX`, `FRM_LEVEL_DIFF`, `FRM_F1_EXCESS`, `FRM_B14`, `FRM_FLAGS` (`fw_ram.h`); M4.4b's `par_play.c` and the `test_fw_par.c` frame-loop tests.
- Produces:

```c
/* src/firmware/frm_build.h */
#define FRM_LEVEL_MARGIN 4, FRM_F1_BASE 0x21, FRM_PRODUCT_SATURATED 0xFF
#define FRM_FLAG_JUMP 0x80, FRM_FLAG_FALL 0x40, FRM_FLAG_RISES 0x10, FRM_FLAG_PITCH 0x08
void frm_transition_flags(fw *f, fw_regs *r);   /* $BA3F: A = frm_flags, X kept, Y = 13 */
void frm_finish_frame(fw *f, fw_regs *r);       /* $B9B5: A, X, Y kept */
```

  - The replacements `frm_finish_frame` 0xB9B5 and `frm_transition_flags` 0xBA3F (after `pros_microprosody`), `FW_OUT_A | FW_OUT_X | FW_OUT_Y`, not `waits`: **71 replacements, 72 `golden_hybrid` modes, 69 `diff_lts` lines**.
  - No `ROM_FRM_*` `fw_rom_call` in `src/firmware/` (spec §4.7 criterion 8).

- [ ] **Step 1: Write the failing tests**

**`tests/test_fw_frm.c`.** Replace

```c
void suite_fw_frm(void)
{
```

with the finish tests and the suite's head

```c
/* ---- the finish (M4.5 Task 3) ---- */

static void test_finish_rows(void)
{
    check_row("frm_finish_frame", ROM_FRM_FINISH_FRAME);
    check_row("frm_transition_flags", ROM_FRM_TRANSITION_FLAGS);
}

/* frm_snap and frm_snap_prev in the flag cases: the pitch code 20, the amplitudes AHF, A3, A2, A1, ALF 10 and the
   frequencies FN, F1, F2, F3 30 (i = 0..9, in frm_snapshot_map's order), in both arrays. */
static const uint8_t k_snap[10] = { 20, 10, 10, 10, 10, 10, 30, 30, 30, 30 };

/* A change from k_snap: frm_snap_prev[i] = prev and frm_snap[i] = snap (i = 10: none). */
typedef struct { uint8_t i, prev, snap; } snap_change;

/* One frm_transition_flags call with k_snap changed by a and b; b13 and frm_flags $EE before. Out: frm_flags = b13 =
   A = want, FW_TMP_75 = want_75 (the rises counted), X untouched, Y = 13; nothing else written. */
static void flags_case(snap_change a, snap_change b, uint8_t want, uint8_t want_75)
{
    static const uint16_t w[] = { FRM_FLAGS, FW_TMP_75, ISR_FRAME_B + FRAME_FLAGS };
    setup();
    fill_frame_b();
    memcpy(g_ram + FRM_SNAP, k_snap, 10);
    memcpy(g_ram + FRM_SNAP_PREV, k_snap, 10);
    if (a.i < 10) {
        g_ram[FRM_SNAP_PREV + a.i] = a.prev;
        g_ram[FRM_SNAP + a.i] = a.snap;
    }
    if (b.i < 10) {
        g_ram[FRM_SNAP_PREV + b.i] = b.prev;
        g_ram[FRM_SNAP + b.i] = b.snap;
    }
    g_ram[FRM_FLAGS] = 0xEE;
    g_ram[ISR_FRAME_B + FRAME_FLAGS] = 0xEE;
    g_ram[FW_TMP_75] = 0xEE;
    snapshot();
    fw_regs r = { 0x11, 0x5A, 0x33, 0 };
    frm_transition_flags(&g_f, &r);
    CHECK_EQ(g_ram[FRM_FLAGS], want);
    CHECK_EQ(g_ram[ISR_FRAME_B + FRAME_FLAGS], want);
    CHECK_EQ(g_ram[FW_TMP_75], want_75);
    CHECK_EQ(r.a, want);
    CHECK_EQ(r.x, 0x5A);
    CHECK_EQ(r.y, FRAME_FLAGS);
    check_written(w, 3);
    check_no_calls();
}

/* Each flag at its threshold and one short of it, with d = frm_snap_prev[i] - frm_snap[i] (i: 0 the pitch code, 1-5
   AHF, A3, A2, A1, ALF, 6-9 FN, F1, F2, F3), and two 8-bit wraps. */
static void test_transition_flags(void)
{
#define NONE { 10, 0, 0 }
    static const struct { snap_change a, b; uint8_t want, want_75; } k[] = {
        { NONE, NONE, 0, 0 },                                       /* nothing moved */
        /* F1: d = 16 ($BA4F BPL taken), 15; d = -16 ($F0 - $F1 has bit 7: $BA53 BMI taken), -15 ($F1 - $F1 = 0) */
        { { 7, 46, 30 }, NONE, FRM_FLAG_JUMP, 0 },
        { { 7, 45, 30 }, NONE, 0, 0 },
        { { 7, 14, 30 }, NONE, FRM_FLAG_JUMP, 0 },
        { { 7, 15, 30 }, NONE, 0, 0 },
        /* A1: d = 12 ($BA63 BPL), 11; d = -12 ($F4 - $F5: $BA67 BMI; a rise of 12, counted), -11 (not counted) */
        { { 4, 22, 10 }, NONE, FRM_FLAG_JUMP, 0 },
        { { 4, 21, 10 }, NONE, 0, 0 },
        { { 4, 10, 22 }, NONE, FRM_FLAG_JUMP, 1 },
        { { 4, 10, 21 }, NONE, 0, 0 },
        /* a fall of A2: d = 28 ($BA89 BPL taken), 27 (the jump only) */
        { { 3, 38, 10 }, NONE, FRM_FLAG_JUMP | FRM_FLAG_FALL, 0 },
        { { 3, 37, 10 }, NONE, FRM_FLAG_JUMP, 0 },
        /* a fall of A3 to the silent value 1 ($BA7E BEQ taken): d = 19 ($BA97 BPL taken), 18; and d = 19 to 10 */
        { { 2, 20, 1 }, NONE, FRM_FLAG_JUMP | FRM_FLAG_FALL, 0 },
        { { 2, 19, 1 }, NONE, FRM_FLAG_JUMP, 0 },
        { { 2, 29, 10 }, NONE, FRM_FLAG_JUMP, 0 },
        /* A1 and AHF rise by 12: $75 = 2, not below 2 ($BAC0 BMI not taken) */
        { { 4, 10, 22 }, { 1, 10, 22 }, FRM_FLAG_JUMP | FRM_FLAG_RISES, 2 },
        /* the pitch code: d = 7 ($BAD3 BPL), 6; d = -7 ($F9 - $FA: $BAD7 BMI), -6 */
        { { 0, 27, 20 }, NONE, FRM_FLAG_PITCH, 0 },
        { { 0, 26, 20 }, NONE, 0, 0 },
        { { 0, 13, 20 }, NONE, FRM_FLAG_PITCH, 0 },
        { { 0, 14, 20 }, NONE, 0, 0 },
        /* FN from 250 to 0: d = $FA, -6 in 8 bits: no jump. ALF from 0 to $77: d = $89, and $89 - 12 and $89 - 28 have
           bit 7 clear: a rise of 119 sets the jump and the fall flags (and counts as one rise) */
        { { 6, 250, 0 }, NONE, 0, 0 },
        { { 5, 0, 0x77 }, NONE, FRM_FLAG_JUMP | FRM_FLAG_FALL, 1 },
    };
#undef NONE
    for (size_t n = 0; n < sizeof k / sizeof k[0]; n++)
        flags_case(k[n].a, k[n].b, k[n].want, k[n].want_75);
}

/* One frm_finish_frame case: the producer frame's ALF, A1, F1 and pitch code b10 (FN 30, A2 10, F2 40, A3 10, F3 50,
   AHF 10, V 63; b11-b17 $EE), @M 20, frm_source_index src and frm_filter_index flt; frm_snap already as the frame
   gives it (so no transition flag); frm_level_diff, frm_f1_excess, frm_b14 and frm_flags $EE before. Out: b14, b15
   and frm_level_diff, frm_f1_excess ($EE: not written) as the ROM leaves them; F1 and F2 after the raise. */
typedef struct {
    uint8_t alf, a1, f1, pitch, src, flt;
    uint8_t b14, b15, level, excess, f1_out, f2_out;
} finish_row;

static void finish_case(const finish_row *k)
{
    static const uint8_t codes_src[4] = { 0x82, 0x58, 0x9B, 0xBA }, codes_flt[2] = { 0x4A, 0x54 };
    uint16_t w[40];
    size_t n = 0;
    setup();
    fill_frame_b();
    uint8_t *frame = g_ram + ISR_FRAME_B;
    static const uint8_t base[11] = { 0, 30, 0, 0, 10, 40, 10, 50, 10, 63, 0 };
    memcpy(frame, base, 11);
    memset(frame + 11, 0xEE, 9);
    frame[FRAME_ALF] = k->alf;
    frame[FRAME_A1] = k->a1;
    frame[FRAME_F1] = k->f1;
    frame[FRAME_PITCH] = k->pitch;
    for (unsigned i = 0; i < 10; i++)
        g_ram[FRM_SNAP + i] = frame[fw_frm_snapshot_map[i]];
    g_ram[CMD_MARK_SPACE] = 20;
    g_ram[FRM_SOURCE_INDEX] = k->src;
    g_ram[FRM_FILTER_INDEX] = k->flt;
    g_ram[FRM_LEVEL_DIFF] = g_ram[FRM_F1_EXCESS] = g_ram[FRM_B14] = g_ram[FRM_FLAGS] = 0xEE;
    for (unsigned i = 0; i < 10; i++) {
        w[n++] = (uint16_t)(FRM_SNAP + i);
        w[n++] = (uint16_t)(FRM_SNAP_PREV + i);
    }
    static const uint16_t more[] = {
        FW_TMP_75, ISR_FRAME_B + FRAME_F1, ISR_FRAME_B + FRAME_F2, ISR_FRAME_B + FRAME_MARK_SPACE,
        ISR_FRAME_B + FRAME_FLAGS, ISR_FRAME_B + FRAME_B14, ISR_FRAME_B + FRAME_B15, ISR_FRAME_B + FRAME_SOURCE,
        ISR_FRAME_B + FRAME_FILTER, FRM_FLAGS, FRM_B14, FRM_LEVEL_DIFF, FRM_F1_EXCESS,
    };
    for (size_t i = 0; i < sizeof more / sizeof more[0]; i++)
        w[n++] = more[i];
    snapshot();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    frm_finish_frame(&g_f, &r);
    CHECK_EQ(frame[FRAME_B14], k->b14);
    CHECK_EQ(frame[FRAME_B15], k->b15);
    CHECK_EQ(g_ram[FRM_B14], k->b14);
    CHECK_EQ(g_ram[FRM_LEVEL_DIFF], k->level);
    CHECK_EQ(g_ram[FRM_F1_EXCESS], k->excess);
    CHECK_EQ(frame[FRAME_F1], k->f1_out);
    CHECK_EQ(frame[FRAME_F2], k->f2_out);
    CHECK_EQ(frame[FRAME_SOURCE], codes_src[k->src]);
    CHECK_EQ(frame[FRAME_FILTER], codes_flt[k->flt]);
    CHECK_EQ(frame[FRAME_MARK_SPACE], 20);   /* @M 20 within 47 - 2c and 160 - 2c (c = 20 or 40) */
    CHECK_EQ(frame[FRAME_FLAGS], 0);         /* frm_snap_prev = frm_snap: no flag */
    CHECK_EQ(g_ram[FRM_FLAGS], 0);
    CHECK_EQ(g_ram[FW_TMP_75], 0);           /* the clamp's 2c, then the raise's, then the rise count 0 */
    CHECK_EQ(frame[FRAME_END_MARKER], 0xEE);
    CHECK_EQ(r.a, 0x11);                     /* A, X and Y as they came in */
    CHECK_EQ(r.x, 0x22);
    CHECK_EQ(r.y, 0x33);
    check_written(w, n);
    check_no_calls();
}

/* d = ALF - A1 - 4 and e = F1 - 33 from the snapshot (taken before the raise); the word is 82 x the index. */
static void test_finish_frame(void)
{
    static const finish_row k[] = {
        /* d = -4 ($FC, $B9E7 BMI taken): 0 and 0; nothing else written. Both codes at index 0 */
        { 10, 10, 30, 20, 0, 0, 0, 0, 0xEE, 0xEE, 30, 40 },
        /* d = 0 ($B9E5 BEQ taken). source 1 (spike $58), filter 1 (female $54) */
        { 14, 10, 40, 20, 1, 1, 0, 0, 0xEE, 0xEE, 40, 40 },
        /* d = 6, e = 0 (F1 33: $B9F4 BEQ taken, which no corpus text takes): frm_level_diff = 6 is written; 0 */
        { 20, 10, 33, 20, 2, 0, 0, 0, 6, 0xEE, 33, 40 },
        /* d = 6, e = 20 - 33 = $F3 ($B9F6 BMI taken): 0. source 3 (reduced HF $BA) */
        { 20, 10, 20, 20, 3, 1, 0, 0, 6, 0xEE, 20, 40 },
        /* e = 1 (F1 34): the limit 255 - 6 = $F9 has bit 7 set ($BA02 BMI taken): the index $FF, 82 x 255 = $51AE;
           frm_f1_excess = 1 stays (no multiply) */
        { 20, 10, 34, 20, 0, 0, 0xAE, 0x51, 6, 1, 34, 40 },
        /* d = 60 above the limit 51 of e = 5 (F1 38): $F7: the index $FF */
        { 74, 10, 38, 20, 0, 0, 0xAE, 0x51, 60, 5, 38, 40 },
        /* d = 6, e = 5: 51 - 6 = 45: the multiply, 30 (82 x 30 = 2460 = $099C); both bytes shifted out to 0 */
        { 20, 10, 38, 20, 0, 0, 0x9C, 0x09, 0, 0, 38, 40 },
        /* d = 85 at the limit of e = 3 (F1 36): 85 - 85 = 0: the multiply, 255 */
        { 99, 10, 36, 20, 0, 0, 0xAE, 0x51, 0, 0, 36, 40 },
        /* b10 = 40: the raise ((40 - 32) >> 2 = 2: F1 34 -> 36, F2 40 -> 41) comes after the snapshot, so e = 1 */
        { 20, 10, 34, 40, 0, 0, 0xAE, 0x51, 6, 1, 36, 41 },
    };
    for (size_t n = 0; n < sizeof k / sizeof k[0]; n++)
        finish_case(&k[n]);
}

void suite_fw_frm(void)
{
```

and replace the end of `suite_fw_frm`

```c
    test_raise_formants();
}
```

with

```c
    test_raise_formants();
    test_finish_rows();
    test_transition_flags();
    test_finish_frame();
}
```

What the new tests pin:
- `test_finish_rows`: the two rows.
- `test_transition_flags` (each case's RAM byte for byte, A = b13 = `frm_flags`, Y = 13, X kept, `$75` = the rise count): no change; a frequency (F1) at ±16 and ±15; an amplitude (A1) at ±12 and ±11 (a rise of 12 counted in `$75`); a fall of 28 and 27; a fall to the silent value 1 of 19 and 18, and a fall of 19 to 10 (no fall flag); two rises of 12 (`FRM_FLAG_RISES`, `$75` = 2); the pitch code at ±7 and ±6; **two 8-bit wraps**: FN from 250 to 0 (d = $FA: no flag) and ALF from 0 to $77 (d = $89: the jump and the fall flags for a rise).
- `test_finish_frame` (each case's RAM byte for byte; A, X, Y preserved; b11 = @M 20; b13 = 0; b16 and b17 per index): d = −4 (`BMI`) and d = 0 (`BEQ`): b14 = b15 = 0, `frm_level_diff` not written; d = 6 with F1 = 33, **e = 0 (`BEQ` at $B9F4)**, and with e negative (`BMI` at $B9F6): 0, `frm_level_diff` = 6; e = 1: always the index $FF ($51AE); d above the limit (60 > 51 for e = 5): $FF; an exact product (6 × 5 = 30: $099C) and one at the limit (85 × 3 = 255), `frm_level_diff` and `frm_f1_excess` shifted to 0; a raise (b10 = 40: F1 34 → 36, F2 40 → 41) after the snapshot, so e = 1 from the unraised F1; the four source codes and both filter codes.

**`tests/test_fw_par.c`** (the frame-loop tests: the frame builder is C, so the mock loses its five `frm_*` cases and the tests check what the C wrote). Replace

```c
/* Records every ROM call. isr_set_volume (M4.6, called by cmd_ev_set_volume) returns the registers the ROM one
   leaves; so do the interrupt, serial and frame routines that par_play and boot_loop_play call (M4.5, M4.6), with
   what the timer and receive interrupts would do between the frame loop's tests (g_flag_every, g_abort_at); any
   other call fails (par_setup_voice calls the C frame builder since M4.5). */
```

with

```c
/* Records every ROM call. isr_set_volume (M4.6, called by cmd_ev_set_volume) returns the registers the ROM one
   leaves; so do the interrupt and serial routines that par_play and boot_loop_play call (M4.6), with what the timer
   and receive interrupts would do between the frame loop's tests (g_flag_every, g_abort_at); any other call fails
   (par_setup_voice and par_play call the C frame builder since M4.5). */
```

delete

```c
    case ROM_FRM_COPY_REST_FRAME:    /* $B445-$B44F: LDA $2000,Y for Y = $13..0, DEY / BPL: Y = $FF; X untouched */
        r->a = g_ram[FRM_REST_FRAME];
        r->y = 0xFF;
        return FW_CALL_RETURNED;
    case ROM_FRM_CLAMP_MARK_SPACE:   /* $B930-$B95C: LDY #$0A, INY; LDA #$0B at $B954; X untouched */
        r->a = 0x0B;
        r->y = 0x0B;
        return FW_CALL_RETURNED;
```

delete

```c
    case ROM_FRM_FINISH_FRAME:       /* $B9B5-$B9B9 PHA / TXA / PHA / TYA / PHA ... $BA39-$BA3E PLA / TAY / PLA / TAX /
                                        PLA: A, X, Y restored */
```

delete

```c
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
```

replace

```c
/* The end ($B8D2-$B8FA) from call i on, with `waits` isr_par_ready calls per wait for the flag: isr_wait_frame,
   then per rest frame frm_copy_rest_frame, frm_snapshot, frm_clamp_mark_space, frm_transition_flags and the wait,
   with par_frames_left 3, 2, 1 ($B8D5, then the DEC at $B8EF); then the last isr_par_ready ($B8F7;
   cmd_ev_sound_end is C and returns at once, cmd_sound_active being 0). Returns the number of calls. */
static unsigned check_rest_frames(unsigned i, unsigned waits)
{
    static const uint16_t k_frame[] = {
        ROM_FRM_COPY_REST_FRAME, ROM_FRM_SNAPSHOT, ROM_FRM_CLAMP_MARK_SPACE, ROM_FRM_TRANSITION_FLAGS,
    };
    uint16_t k[1 + 3 * (4 + 2) + 1];   /* enough for waits <= 2 */
    CHECK(waits <= 2);
    if (waits > 2)
        return 0;
    unsigned n = 0, per = 4 + waits;
    k[n++] = ROM_ISR_WAIT_FRAME;
    for (unsigned j = 0; j < 3; j++) {
        for (unsigned m = 0; m < 4; m++)
            k[n++] = k_frame[m];
        for (unsigned m = 0; m < waits; m++)
            k[n++] = ROM_ISR_PAR_READY;
    }
```

with

```c
/* The end ($B8D2-$B8FA) from call i on, with `waits` isr_par_ready calls per wait for the flag: isr_wait_frame,
   then per rest frame (frm_copy_rest_frame, frm_snapshot, frm_clamp_mark_space and frm_transition_flags, all C)
   the wait, with par_frames_left 3, 2, 1 ($B8D5, then the DEC at $B8EF); then the last isr_par_ready ($B8F7;
   cmd_ev_sound_end is C and returns at once, cmd_sound_active being 0). Returns the number of calls. */
static unsigned check_rest_frames(unsigned i, unsigned waits)
{
    uint16_t k[1 + 3 * 2 + 1];   /* enough for waits <= 2 */
    CHECK(waits <= 2);
    if (waits > 2)
        return 0;
    unsigned n = 0, per = waits;
    k[n++] = ROM_ISR_WAIT_FRAME;
    for (unsigned j = 0; j < 3; j++)
        for (unsigned m = 0; m < waits; m++)
            k[n++] = ROM_ISR_PAR_READY;
```

Replace `test_play_tail`, with its comment,

```c
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

```

with

```c
/* No segment left (pros_seg_count = 0): two passes (the flag comes at the second isr_par_ready, every 2nd call),
   then the tight loop ($B8BB-$B8CF): par_frames_left 3 -> 2 -> 1 gives two frames, each par_step_tracks and
   frm_finish_frame (both C), the flag cleared and waited for again; 0 ends it. The rest frames, the flag every 2nd
   call. No event (cmd_ev_count 0) and no reply (ser_status_due 0). The registers flow: the entry A and Y and X =
   cmd_ev_next ($B877 LDX) reach the passes' isr_par_ready; Y = 0 from $B8B6 (pros_seg_count + 1); par_step_tracks
   leaves A = pros_play_pitch >> 2 = 0, X = 0, Y = 10, which frm_finish_frame keeps; each rest frame leaves A =
   frm_flags, X = 0 (frm_snapshot's) and Y = $0D (frm_transition_flags'), which par_play returns. frm_rest_frame is
   $77 and zeros: the first rest frame's ALF goes from 0 (the frames before) to $77, an 8-bit difference of $89, so
   frm_flags = $C0 (bit 7: $89 - 12 has bit 7 clear; bit 6: $89 - 28 too); the next two change nothing (0). The
   fill frame ends as the rest frame with b11 = @M 0 raised to 47 - 2 x 0 and b13 = 0. */
static void test_play_tail(void)
{
    setup();
    fill_frame_b();
    memset(g_ram + ISR_FRAME_B, 0xEE, 20);
    g_ram[PAR_FRAMES_LEFT] = 3;
    g_ram[FRM_REST_FRAME] = 0x77;
    g_flag_every = 2;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    par_play(&g_f, &r);
    static const uint16_t k[] = {
        ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,   /* pass 1: $B89F, $B8AB (the flag comes) */
        ROM_ISR_PAR_READY,                      /* pass 2: $B89F, then $B8B1-$B8B9 */
        ROM_ISR_PAR_READY,                      /* frame 1: $B8C9 */
        ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,   /* frame 2: $B8C9 twice */
    };
    check_order(0, k, 6);
    check_play_call(0, ROM_ISR_PAR_READY, 0x11, 0, 0x33, 3);
    check_play_call(2, ROM_ISR_PAR_READY, 0x11, 0, 0x33, 3);
    check_play_call(3, ROM_ISR_PAR_READY, 0, 0, FRAME_PITCH, 2);   /* par_step_tracks' A, X, Y */
    check_play_call(4, ROM_ISR_PAR_READY, 0, 0, FRAME_PITCH, 1);
    CHECK_EQ(check_rest_frames(6, 2), 8);                           /* the flag every 2nd call */
    check_play_call(6, ROM_ISR_WAIT_FRAME, 0, 0, FRAME_PITCH, 0);
    check_play_call(7, ROM_ISR_PAR_READY, 0xC0, 0, 0x0D, 3);         /* the first rest frame's frm_flags */
    check_play_call(9, ROM_ISR_PAR_READY, 0, 0, 0x0D, 2);
    check_play_call(11, ROM_ISR_PAR_READY, 0, 0, 0x0D, 1);
    check_play_call(13, ROM_ISR_PAR_READY, 0, 0, 0x0D, 0);           /* $B8F7 */
    CHECK_EQ(g_ncalls, 14);
    CHECK_EQ(count(ROM_ISR_PAR_READY), 13);
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 0);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0);                                /* cleared after the last rest frame */
    for (int b = 0; b < FRAME_SIZE; b++)
        CHECK_EQ(g_ram[ISR_FRAME_B + b], b == FRAME_ALF ? 0x77 : b == FRAME_MARK_SPACE ? 47 : 0);
    CHECK_EQ(g_ram[FRM_FLAGS], 0);
    CHECK_EQ(r.a, 0);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x0D);
    CHECK(!g_f.fault);
}

```

In `test_play_segment`, replace

```c
    g_ram[SER_STATUS_DUE] = 1;
    g_ram[FRM_FLAGS] = 0x5A;
    g_flag_every = 2;
```

with

```c
    g_ram[SER_STATUS_DUE] = 1;
    g_flag_every = 2;
```

replace

```c
        ROM_ISR_PAR_READY, ROM_FRM_FINISH_FRAME,                                       /* $B908-$B90E */
```

with

```c
        ROM_ISR_PAR_READY,                                                             /* $B908 */
```

replace

```c
        ROM_FRM_FINISH_FRAME, ROM_ISR_PAR_READY,                                       /* the last frame */
    };
    check_order(0, k, 14);
```

with

```c
        ROM_ISR_PAR_READY,                                                             /* the last frame: $B8C9 */
    };
    check_order(0, k, 12);
```

replace

```c
    check_play_call(6, ROM_FRM_FINISH_FRAME, 0x18, 0, FRAME_PITCH, 2);   /* the pitch $55 to $70 over 2 frames:
                                                                       ($70 + $55) >> 1 = $62, b10 = $62 >> 2 */
    check_play_call(12, ROM_FRM_FINISH_FRAME, 0x1C, 0, FRAME_PITCH, 1);  /* then over 1: $70, b10 = $1C */
    CHECK_EQ(check_rest_frames(14, 2), 20);
    CHECK_EQ(g_ncalls, 34);
```

with

```c
    check_play_call(6, ROM_SER_TX_SERVICE, 0x18, 1, FRAME_PITCH, 2);    /* the pitch $55 to $70 over 2 frames:
                                                                       ($70 + $55) >> 1 = $62, b10 = $62 >> 2: A of
                                                                       par_step_tracks, kept by frm_finish_frame;
                                                                       X = cmd_ev_next ($B877) */
    check_play_call(11, ROM_ISR_PAR_READY, 0x1C, 0, FRAME_PITCH, 1);    /* then over 1: $70, b10 = $1C */
    CHECK_EQ(check_rest_frames(12, 2), 8);
    CHECK_EQ(g_ncalls, 20);
```

and replace

```c
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 0);
    CHECK_EQ(r.a, 0x5A);
    CHECK_EQ(r.x, 0);
```

with

```c
    CHECK_EQ(g_ram[PAR_FRAMES_LEFT], 0);
    CHECK_EQ(g_ram[ISR_FRAME_B + FRAME_SOURCE], 0);   /* the last rest frame: frm_rest_frame's (zeros here) */
    CHECK_EQ(r.a, 0);                                 /* its frm_flags: nothing moved from the rest frame before */
    CHECK_EQ(r.x, 0);
```

In `test_play_abort`, replace

```c
/* A mute: ser_abort bit 0 arrives with the flag at the first isr_par_ready. The abort test ($B8A2) comes before the
   flag test, so par_play goes straight to the end: no frame is built (the fill frame is untouched, par_frames_left
   is only set to 3 and counted down), pros_seg_count is kept, and ser_abort stays set (boot_get_clause clears it).
```

with

```c
/* A mute: ser_abort bit 0 arrives with the flag at the first isr_par_ready. The abort test ($B8A2) comes before the
   flag test, so par_play goes straight to the end: no frame is built (par_frames_left is only set to 3 and counted
   down; the fill frame becomes the rest frame, zeros here, with b11 = 47 - 2 x 0, and frm_finish_frame would have
   written b16 = $82), pros_seg_count is kept, and ser_abort stays set (boot_get_clause clears it).
```

replace

```c
    CHECK_EQ(check_rest_frames(1, 1), 17);
    check_play_call(1, ROM_ISR_WAIT_FRAME, 0x11, 0, 0x33, 4);
    CHECK_EQ(g_ncalls, 18);
    CHECK_EQ(count(ROM_SER_TX_SERVICE), 0);
    CHECK_EQ(count(ROM_FRM_FINISH_FRAME), 0);
    for (int b = 0; b < 20; b++)
        CHECK_EQ(g_ram[ISR_FRAME_B + b], 0xEE);
```

with

```c
    CHECK_EQ(check_rest_frames(1, 1), 5);
    check_play_call(1, ROM_ISR_WAIT_FRAME, 0x11, 0, 0x33, 4);
    CHECK_EQ(g_ncalls, 6);
    CHECK_EQ(count(ROM_SER_TX_SERVICE), 0);
    for (int b = 0; b < FRAME_SIZE; b++)
        CHECK_EQ(g_ram[ISR_FRAME_B + b], b == FRAME_MARK_SPACE ? 47 : 0);
```

In `test_play_zero_frames_main`, replace

```c
    static const uint16_t k[] = { ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_FRM_FINISH_FRAME, ROM_ISR_PAR_READY };
    check_order(0, k, 4);
    check_play_call(1, ROM_ISR_PAR_READY, 0x11, 0, 1, 0xFF);   /* $B908 after the DEC; Y = pros_seg_count ($B8B1) */
    CHECK_EQ(check_rest_frames(4, 1), 17);
    CHECK_EQ(g_ncalls, 21);
```

with

```c
    static const uint16_t k[] = { ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY };
    check_order(0, k, 3);
    check_play_call(1, ROM_ISR_PAR_READY, 0x11, 0, 1, 0xFF);   /* $B908 after the DEC; Y = pros_seg_count ($B8B1) */
    CHECK_EQ(check_rest_frames(3, 1), 5);
    CHECK_EQ(g_ncalls, 8);
```

In `test_play_zero_frames_segment`, replace

```c
    static const uint16_t k[] = { ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_FRM_FINISH_FRAME, ROM_ISR_PAR_READY };
    check_order(0, k, 4);
    check_play_call(1, ROM_ISR_PAR_READY, 0, 10, 1, 0xFF);   /* A = 0 ($B903), X = 10 (par_next_segment), Y = 1:
                                                               pros_seg_count again at $B8B1 */
    CHECK_EQ(check_rest_frames(4, 1), 17);
    CHECK_EQ(g_ncalls, 21);
```

with

```c
    static const uint16_t k[] = { ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY };
    check_order(0, k, 3);
    check_play_call(1, ROM_ISR_PAR_READY, 0, 10, 1, 0xFF);   /* A = 0 ($B903), X = 10 (par_next_segment), Y = 1:
                                                               pros_seg_count again at $B8B1 */
    CHECK_EQ(check_rest_frames(3, 1), 5);
    CHECK_EQ(g_ncalls, 8);
```

In `test_play_zero_frames_tail`, replace

```c
/* No segment left and par_frames_left = 0: the DEC at $B8BB gives $FF, and the tight loop builds 255 frames
   ($FF ... 1), each frm_finish_frame and one isr_par_ready (the flag at every call), before the end. */
```

with

```c
/* No segment left and par_frames_left = 0: the DEC at $B8BB gives $FF, and the tight loop builds 255 frames
   ($FF ... 1), each par_step_tracks and frm_finish_frame (C) and one isr_par_ready (the flag at every call), before
   the end. */
```

replace

```c
    check_play_call(1, ROM_FRM_FINISH_FRAME, 0, 0, FRAME_PITCH, 0xFF);
    check_play_call(509, ROM_FRM_FINISH_FRAME, 0, 0, FRAME_PITCH, 1);
    CHECK_EQ(count(ROM_FRM_FINISH_FRAME), 255);
    CHECK_EQ(check_rest_frames(511, 1), 17);
    CHECK_EQ(g_ncalls, 528);
```

with

```c
    check_play_call(1, ROM_ISR_PAR_READY, 0, 0, FRAME_PITCH, 0xFF);     /* frame $FF's wait */
    check_play_call(255, ROM_ISR_PAR_READY, 0, 0, FRAME_PITCH, 1);      /* frame 1's */
    CHECK_EQ(count(ROM_ISR_PAR_READY), 1 + 255 + 3 + 1);
    CHECK_EQ(check_rest_frames(256, 1), 5);
    CHECK_EQ(g_ncalls, 261);
```

In `test_loop_play`, replace

```c
   isr_speech_stop, whose registers it returns. cmd_volume 9: the half is 4, P3 = (P3 & $87) | (4 ^ $F) << 3. */
static void test_loop_play(void)
{
    setup();
    pitch_identity();
```

with

```c
   isr_speech_stop, whose registers it returns. cmd_volume 9: the half is 4, P3 = (P3 & $87) | (4 ^ $F) << 3. The
   rest frames go to isr_frame_b (isr_fill_ptr as par_setup_voice leaves it); par_play returns the last one's
   frm_flags, 0. */
static void test_loop_play(void)
{
    setup();
    fill_frame_b();
    pitch_identity();
```

replace

```c
    g_ram[CMD_VOLUME] = 9;
    g_ram[FRM_FLAGS] = 0x5A;
    g_p3 = 0xFF;
```

with

```c
    g_ram[CMD_VOLUME] = 9;
    g_p3 = 0xFF;
```

and replace

```c
    CHECK_EQ(check_rest_frames(6, 1), 17);
    check_call(23, ROM_ISR_VOLUME_HALF, 0x5A, 0, 0x0D);          /* par_play's exit registers */
    check_call(24, ROM_ISR_WAIT_FRAME, 0xDF, 0, 0x0D);
    check_call(25, ROM_ISR_SET_VOLUME, 0, 0, 0x0D);
    check_call(26, ROM_ISR_SPEECH_STOP, 0xFF, 0, 0x0D);          /* P3 $87 | $F << 3 */
    CHECK_EQ(g_ncalls, 27);
```

with

```c
    CHECK_EQ(check_rest_frames(6, 1), 5);
    check_call(11, ROM_ISR_VOLUME_HALF, 0, 0, 0x0D);             /* par_play's exit registers */
    check_call(12, ROM_ISR_WAIT_FRAME, 0xDF, 0, 0x0D);
    check_call(13, ROM_ISR_SET_VOLUME, 0, 0, 0x0D);
    check_call(14, ROM_ISR_SPEECH_STOP, 0xFF, 0, 0x0D);          /* P3 $87 | $F << 3 */
    CHECK_EQ(g_ncalls, 15);
```

**`tests/test_fw_tn.c`** (the flush's C player now runs the C rest frames; they go to `isr_frame_b`, where `par_setup_voice` leaves `isr_fill_ptr`, not to the zero page). Replace

```c
    case ROM_ISR_SPEECH_STOP:
    case ROM_FRM_COPY_REST_FRAME:
    case ROM_FRM_SNAPSHOT:
    case ROM_FRM_CLAMP_MARK_SPACE:
    case ROM_FRM_TRANSITION_FLAGS:
        return FW_CALL_RETURNED;
```

with

```c
    case ROM_ISR_SPEECH_STOP:
        return FW_CALL_RETURNED;
```

replace

```c
    setup_n(text, sizeof text);
    g_ram[CMD_PHONETIC] = 1;                    /* verbatim: one phonetic byte per character */
    tn_convert_clause(&g_f);
```

with

```c
    setup_n(text, sizeof text);
    g_ram[CMD_PHONETIC] = 1;                    /* verbatim: one phonetic byte per character */
    fw_wr16(&g_f, ISR_FILL_PTR, ISR_FRAME_B);   /* where par_setup_voice leaves it: the C rest frames go there */
    tn_convert_clause(&g_f);
```

replace

```c
        ROM_ISR_PAR_READY, ROM_ISR_WAIT_FRAME,                                     /* par_play: the pass, the end */
        ROM_FRM_COPY_REST_FRAME, ROM_FRM_SNAPSHOT, ROM_FRM_CLAMP_MARK_SPACE, ROM_FRM_TRANSITION_FLAGS,
        ROM_ISR_PAR_READY,
        ROM_FRM_COPY_REST_FRAME, ROM_FRM_SNAPSHOT, ROM_FRM_CLAMP_MARK_SPACE, ROM_FRM_TRANSITION_FLAGS,
        ROM_ISR_PAR_READY,
        ROM_FRM_COPY_REST_FRAME, ROM_FRM_SNAPSHOT, ROM_FRM_CLAMP_MARK_SPACE, ROM_FRM_TRANSITION_FLAGS,
        ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,
```

with

```c
        ROM_ISR_PAR_READY, ROM_ISR_WAIT_FRAME,                                     /* par_play: the pass, the end */
        ROM_ISR_PAR_READY, ROM_ISR_PAR_READY, ROM_ISR_PAR_READY,                   /* the rest frames (C) */
        ROM_ISR_PAR_READY,
```

replace

```c
    CHECK_EQ(g_calls[first + 25].a, 0);                   /* isr_set_volume(0) ($B2D0) */
```

with

```c
    CHECK_EQ(g_calls[first + 13].a, 0);                   /* isr_set_volume(0) ($B2D0) */
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test fw"`
Expected: the build stops with compile errors (the proof run, in this order, then more `C2065` for the other `FRM_FLAG_*` uses):

```
tests\test_fw_frm.c(360): error C2220: the following warning is treated as an error
tests\test_fw_frm.c(360): warning C4013: 'frm_transition_flags' undefined; assuming extern returning int
tests\test_fw_frm.c(379): error C2065: 'FRM_FLAG_JUMP': undeclared identifier
...
tests\test_fw_frm.c(376): error C2099: initializer is not a constant
tests\test_fw_frm.c(455): warning C4013: 'frm_finish_frame' undefined; assuming extern returning int
```

- [ ] **Step 3: Implement**

`src/firmware/frm_build.h`: replace the first lines of the header comment

```c
/* The frame builder (ROM $B41D-$B44F and $B917-$B982), decompiled (M4.5 spec 2.2, 2.6): the rest frame that
   par_setup_voice builds and copies into both frame buffers, and the steps that finish a producer frame (the frame
   at isr_fill_ptr) after the tracks: the snapshot of its parameter bytes, the mark-space clamp of b11 and the formant
   raise at high pitch codes. Each routine takes the registers at entry in r and leaves in r the A, X and Y that the
```

with

```c
/* The frame builder (ROM $B41D-$B44F, $B917-$B982 and $B9B5-$BAEB), decompiled (M4.5 spec 2.2, 2.6): the rest
   frame that par_setup_voice builds and copies into both frame buffers, and frm_finish_frame, which finishes each
   producer frame (the frame at isr_fill_ptr) after the tracks: the snapshot of its parameter bytes, the mark-space
   clamp of b11, the formant raise at high pitch codes, the source and filter codes (b16, b17), the b14-b15 word and
   the transition flags (b13). Each routine takes the registers at entry in r and leaves in r the A, X and Y that the
```

replace

```c
#define FRM_RAISE_PITCH       0x20   /* $B962: pitch codes b10 from 32 up raise F1 and F2 */
```

with

```c
#define FRM_RAISE_PITCH       0x20   /* $B962: pitch codes b10 from 32 up raise F1 and F2 */
#define FRM_LEVEL_MARGIN      4      /* $B9E3 SBC #4: d = ALF - A1 - 4 */
#define FRM_F1_BASE           0x21   /* $B9F2 SBC #$21: e = F1 - 33 */
#define FRM_PRODUCT_SATURATED 0xFF   /* $BA1A LDA #$FF: the product index when frm_product_limit[e] - d is negative */
/* The bits of frm_flags and b13 (frm_transition_flags), from d = frm_snap_prev - frm_snap per byte */
#define FRM_FLAG_JUMP         0x80   /* a frequency moved by 16 or more, or an amplitude by 12 or more */
#define FRM_FLAG_FALL         0x40   /* an amplitude fell by 28 or more, or by 19 or more to the silent value 1 */
#define FRM_FLAG_RISES        0x10   /* two or more amplitudes rose by 12 or more */
#define FRM_FLAG_PITCH        0x08   /* the pitch code b10 moved by 7 or more */
```

and replace

```c
void frm_raise_formants(fw *f, fw_regs *r);
```

with

```c
void frm_raise_formants(fw *f, fw_regs *r);

/* $BA3F: b13 = frm_flags. With d = frm_snap_prev[i] - frm_snap[i] (SEC / SBC, 8 bits), each test is the N flag of
   an 8-bit CMP (bit 7 of d - k, not a true signed order). frm_flags = 0, then each bit OR-ed in by its own
   read-modify-write:
   - FRM_FLAG_JUMP when, for F3, F2, F1, FN (i = 9 down to 6), d - 16 has bit 7 clear or d - $F1 has it set; else
     when, for ALF, A1, A2, A3, AHF (i = 5 down to 1), d - 12 has it clear or d - $F5 has it set;
   - FRM_FLAG_FALL when, for i = 5 down to 1, d - 28 has bit 7 clear, or d - 19 when frm_snap[i] is 1 (the silent
     value);
   - FRM_FLAG_RISES when FW_TMP_75, cleared (LDM) and then counting the i = 5 down to 1 whose frm_snap[i] -
     frm_snap_prev[i] - 12 has bit 7 clear, minus 2 has bit 7 clear (two or more);
   - FRM_FLAG_PITCH when, for the pitch code (i = 0), d - 7 has bit 7 clear or d - $FA has it set.
   The scans for FRM_FLAG_JUMP and FRM_FLAG_FALL stop at their first hit. r in: X passes through. r out: A =
   frm_flags, X unchanged, Y = FRAME_FLAGS (13). */
void frm_transition_flags(fw *f, fw_regs *r);

/* $B9B5: finishes the producer frame after par_step_tracks: frm_snapshot, frm_clamp_mark_space and
   frm_raise_formants; b16 = frm_source_codes[frm_source_index] and b17 = frm_filter_codes[frm_filter_index]; the
   b14-b15 word from frm_b14_15_lo/hi at a product index, or 0 (frm_b14 = 0) unless d = ALF - A1 - 4 and e = F1 - 33
   (frm_snap[5] - frm_snap[4] - 4 and frm_snap[7] - 33, 8 bits) are both not 0 with bit 7 clear (BEQ, BMI); the index
   is FRM_PRODUCT_SATURATED when frm_product_limit[e] - d has bit 7 set (so always for e = 1), else d * e by an 8-bit
   shift-and-add multiply that loses its carries and leaves frm_level_diff (d) and frm_f1_excess (e) at 0; b15 is
   written before b14, which goes through frm_b14; then frm_transition_flags. r: A, X and Y are preserved (PHA ...
   PLA; the pushes go to the dead stack, which is not reproduced). */
void frm_finish_frame(fw *f, fw_regs *r);
```

`src/firmware/frm_build.c`: replace

```c
#include "frm_build.h"
```

with (`fw_inc8`)

```c
#include "frm_build.h"
#include "fw_ops.h"
```

and replace its end, `frm_raise_formants`,

```c
void frm_raise_formants(fw *f, fw_regs *r)
{
    r->y = FRAME_PITCH;                        /* $B95D-$B966: SEC; SBC #$20; BPL, else RTS */
    r->a = (uint8_t)(fw_ind_y(f, ISR_FILL_PTR, r->y) - FRM_RAISE_PITCH);
    if (r->a & 0x80)
        return;
    r->a >>= 2;                                /* $B967-$B969: LSR, LSR, STA $75 */
    fw_wr8(f, FW_TMP_75, r->a);
    raise(f, r, FRAME_F1);
    r->a = (uint8_t)(fw_rd8(f, FW_TMP_75) >> 1);   /* $B975-$B977: LDA $75; LSR */
    raise(f, r, FRAME_F2);
}
```

with

```c
void frm_raise_formants(fw *f, fw_regs *r)
{
    r->y = FRAME_PITCH;                        /* $B95D-$B966: SEC; SBC #$20; BPL, else RTS */
    r->a = (uint8_t)(fw_ind_y(f, ISR_FILL_PTR, r->y) - FRM_RAISE_PITCH);
    if (r->a & 0x80)
        return;
    r->a >>= 2;                                /* $B967-$B969: LSR, LSR, STA $75 */
    fw_wr8(f, FW_TMP_75, r->a);
    raise(f, r, FRAME_F1);
    r->a = (uint8_t)(fw_rd8(f, FW_TMP_75) >> 1);   /* $B975-$B977: LDA $75; LSR */
    raise(f, r, FRAME_F2);
}

/* ---- frm_transition_flags $BA3F ---- */

/* CMP #k: the N flag, bit 7 of the 8-bit d - k (BPL / BMI). */
static bool minus(uint8_t d, uint8_t k) { return ((uint8_t)(d - k) & 0x80) != 0; }

/* LDA $16DD,Y; SEC; SBC $16CD,Y: frm_snap_prev[i] - frm_snap[i], 8 bits. */
static uint8_t moved(fw *f, uint8_t i)
{
    return (uint8_t)(fw_rd8(f, (uint16_t)(FRM_SNAP_PREV + i)) - fw_rd8(f, (uint16_t)(FRM_SNAP + i)));
}

/* LDA frm_flags; ORA #bit; STA frm_flags. */
static void set_flag(fw *f, uint8_t bit) { fw_wr8(f, FRM_FLAGS, (uint8_t)(fw_rd8(f, FRM_FLAGS) | bit)); }

/* $BA44-$BA74: the frequencies (Y = 9 down to 6, CMP #$10 / BPL, CMP #$F1 / BMI; DEY / CPY #5), then the
   amplitudes (Y = 5 down to 1, CMP #$0C / BPL, CMP #$F5 / BMI; DEY / BNE); the first hit sets FRM_FLAG_JUMP. */
static void jump_flag(fw *f)
{
    uint8_t i = 9;
    for (; i != 5; i--)
        if (!minus(moved(f, i), 0x10) || minus(moved(f, i), 0xF1)) {
            set_flag(f, FRM_FLAG_JUMP);
            return;
        }
    for (; i != 0; i--)
        if (!minus(moved(f, i), 0x0C) || minus(moved(f, i), 0xF5)) {
            set_flag(f, FRM_FLAG_JUMP);
            return;
        }
}

/* $BA77-$BAA4: for Y = 5 down to 1: CMP #1 / BEQ on frm_snap[i] picks the limit, 19 ($13) at the silent value 1,
   else 28 ($1C); CMP / BPL: the first hit sets FRM_FLAG_FALL. */
static void fall_flag(fw *f)
{
    for (uint8_t i = 5; i != 0; i--) {
        uint8_t k = fw_rd8(f, (uint16_t)(FRM_SNAP + i)) == 1 ? 0x13 : 0x1C;
        if (!minus(moved(f, i), k)) {
            set_flag(f, FRM_FLAG_FALL);
            return;
        }
    }
}

/* $BAA7-$BAC7: LDM #0,$75; for Y = 5 down to 1: frm_snap[i] - frm_snap_prev[i] (SEC; SBC); CMP #$0C / BMI, else
   INC $75. Then LDA $75; CMP #2 / BMI, else FRM_FLAG_RISES. */
static void rise_flag(fw *f)
{
    fw_wr8(f, FW_TMP_75, 0);
    for (uint8_t i = 5; i != 0; i--) {
        uint8_t rise = (uint8_t)(fw_rd8(f, (uint16_t)(FRM_SNAP + i)) - fw_rd8(f, (uint16_t)(FRM_SNAP_PREV + i)));
        if (!minus(rise, 0x0C))
            fw_inc8(f, FW_TMP_75);
    }
    if (!minus(fw_rd8(f, FW_TMP_75), 2))
        set_flag(f, FRM_FLAG_RISES);
}

/* $BACA-$BAE1: the pitch code (frm_snap_prev[0] - frm_snap[0]): CMP #7 / BPL, CMP #$FA / BMI: FRM_FLAG_PITCH. */
static void pitch_flag(fw *f)
{
    if (!minus(moved(f, 0), 0x07) || minus(moved(f, 0), 0xFA))
        set_flag(f, FRM_FLAG_PITCH);
}

void frm_transition_flags(fw *f, fw_regs *r)
{
    r->a = 0;                                  /* $BA3F-$BA41 */
    fw_wr8(f, FRM_FLAGS, r->a);
    jump_flag(f);
    fall_flag(f);
    rise_flag(f);
    pitch_flag(f);
    r->a = fw_rd8(f, FRM_FLAGS);               /* $BAE4-$BAE9: b13 = frm_flags */
    r->y = FRAME_FLAGS;
    fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);
}

/* ---- frm_finish_frame $B9B5 ---- */

/* $B9C3-$B9D7: Y = 16, X = frm_source_index: b16 = frm_source_codes,X; INY, X = frm_filter_index: b17 =
   frm_filter_codes,X; DEY / DEY: Y = 15. */
static void source_filter(fw *f, fw_regs *r)
{
    r->y = FRAME_SOURCE;
    r->x = fw_rd8(f, FRM_SOURCE_INDEX);
    r->a = fw_rom8(f, (uint16_t)(FRM_SOURCE_CODES + r->x));
    fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);
    r->y++;
    r->x = fw_rd8(f, FRM_FILTER_INDEX);
    r->a = fw_rom8(f, (uint16_t)(FRM_FILTER_CODES + r->x));
    fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);
    r->y -= 2;
}

/* $B9D8-$B9E9: X = 5, LDA frm_snap,X; DEX; SEC; SBC frm_snap,X; SEC; SBC #4: d = ALF - A1 - 4 (8 bits). BEQ / BMI:
   false when d is 0 or has bit 7 set; else frm_level_diff = d. */
static bool level_diff(fw *f, fw_regs *r)
{
    r->x = 5;
    r->a = fw_rd8(f, (uint16_t)(FRM_SNAP + r->x));
    r->x--;
    r->a = (uint8_t)(r->a - fw_rd8(f, (uint16_t)(FRM_SNAP + r->x)));
    r->a = (uint8_t)(r->a - FRM_LEVEL_MARGIN);
    if (r->a == 0 || (r->a & 0x80))
        return false;
    fw_wr8(f, FRM_LEVEL_DIFF, r->a);
    return true;
}

/* $B9EC-$B9FB: X = 7, LDA frm_snap,X; SEC; SBC #$21: e = F1 - 33 (8 bits). BEQ / BMI: false when e is 0 or has
   bit 7 set; else frm_f1_excess = e and X = e (TAX). */
static bool f1_excess(fw *f, fw_regs *r)
{
    r->x = 7;
    r->a = (uint8_t)(fw_rd8(f, (uint16_t)(FRM_SNAP + r->x)) - FRM_F1_BASE);
    if (r->a == 0 || (r->a & 0x80))
        return false;
    fw_wr8(f, FRM_F1_EXCESS, r->a);
    r->x = r->a;
    return true;
}

/* $BA04-$BA17: A = 0; for X = 8 down to 1: LSR frm_f1_excess, BCC, else CLC / ADC frm_level_diff (8 bits, the
   carry out lost); ASL frm_level_diff. A = d * e (8 bits); both bytes end at 0. */
static void multiply(fw *f, fw_regs *r)
{
    r->a = 0;
    for (r->x = 8; r->x != 0; r->x--) {
        uint8_t e = fw_rd8(f, FRM_F1_EXCESS);
        fw_wr8(f, FRM_F1_EXCESS, (uint8_t)(e >> 1));
        if (e & 1)
            r->a = (uint8_t)(r->a + fw_rd8(f, FRM_LEVEL_DIFF));
        fw_wr8(f, FRM_LEVEL_DIFF, (uint8_t)(fw_rd8(f, FRM_LEVEL_DIFF) << 1));
    }
}

/* $B9FC-$BA26: LDA frm_product_limit,X (X = e); CMP frm_level_diff / BMI: the index FRM_PRODUCT_SATURATED when the
   8-bit difference has bit 7 set, else the product. TAX: frm_b14 = frm_b14_15_lo,X; A = frm_b14_15_hi,X. */
static void product_word(fw *f, fw_regs *r)
{
    r->a = fw_rom8(f, (uint16_t)(FRM_PRODUCT_LIMIT + r->x));
    if (minus(r->a, fw_rd8(f, FRM_LEVEL_DIFF)))
        r->a = FRM_PRODUCT_SATURATED;
    else
        multiply(f, r);
    r->x = r->a;
    r->a = fw_rom8(f, (uint16_t)(FRM_B14_15_LO + r->x));
    fw_wr8(f, FRM_B14, r->a);
    r->a = fw_rom8(f, (uint16_t)(FRM_B14_15_HI + r->x));
}

/* $B9D8-$BA34: the b14-b15 word, or 0 ($BA29-$BA2B: LDA #0; STA frm_b14); STA (fill),Y: b15 (Y = 15); DEY;
   LDA frm_b14; STA (fill),Y: b14. */
static void b14_15(fw *f, fw_regs *r)
{
    if (level_diff(f, r) && f1_excess(f, r)) {
        product_word(f, r);
    } else {
        r->a = 0;
        fw_wr8(f, FRM_B14, r->a);
    }
    fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);
    r->y--;
    r->a = fw_rd8(f, FRM_B14);
    fw_wr_ind_y(f, ISR_FILL_PTR, r->y, r->a);
}

void frm_finish_frame(fw *f, fw_regs *r)
{
    fw_regs s = *r;                            /* $B9B5-$B9B9: PHA / TXA / PHA / TYA / PHA */
    frm_snapshot(f, &s);
    frm_clamp_mark_space(f, &s);
    frm_raise_formants(f, &s);
    source_filter(f, &s);
    b14_15(f, &s);
    frm_transition_flags(f, &s);               /* $BA36, then PLA / TAY / PLA / TAX / PLA: r as it came in */
}
```

`src/firmware/par_play.c`: replace

```c
#include "par_play.h"
#include "cmd_play_event.h"
#include "fw_ops.h"
```

with

```c
#include "par_play.h"
#include "cmd_play_event.h"
#include "frm_build.h"
#include "fw_ops.h"
```

replace

```c
/* $B908-$B911: isr_par_ready, par_step_tracks, frm_finish_frame (it saves and restores A, X and Y), then LDM #0:
   the whole of isr_frame_flag cleared, the frame is ready for the next swap. */
static void segment_step(fw *f, fw_regs *r)
{
    fw_par_ready(f, *r);
    par_step_tracks(f, r);
    fw_rom_call(f, ROM_FRM_FINISH_FRAME, r, FW_ROM_CALL_BUDGET);
    fw_wr8(f, ISR_FRAME_FLAG, 0);
}
```

with

```c
/* $B908-$B911: isr_par_ready, par_step_tracks, frm_finish_frame (C; it keeps A, X and Y), then LDM #0: the whole of
   isr_frame_flag cleared, the frame is ready for the next swap. */
static void segment_step(fw *f, fw_regs *r)
{
    fw_par_ready(f, *r);
    par_step_tracks(f, r);
    frm_finish_frame(f, r);
    fw_wr8(f, ISR_FRAME_FLAG, 0);
}
```

replace

```c
        par_step_tracks(f, r);
        fw_rom_call(f, ROM_FRM_FINISH_FRAME, r, FW_ROM_CALL_BUDGET);
        fw_wr8(f, ISR_FRAME_FLAG, 0);
        wait_frame_flag(f, *r);
```

with

```c
        par_step_tracks(f, r);
        frm_finish_frame(f, r);
        fw_wr8(f, ISR_FRAME_FLAG, 0);
        wait_frame_flag(f, *r);
```

replace

```c
/* $B8D2-$B8FA, the end: isr_wait_frame; par_frames_left = 3; per rest frame frm_copy_rest_frame, frm_snapshot,
   frm_clamp_mark_space and frm_transition_flags, a wait for the swap, the flag cleared (LDM #0) and DEC
   par_frames_left (BNE $B8DA). Then cmd_ev_sound_end (a @T effect cut short is undone) and isr_par_ready. */
```

with

```c
/* $B8D2-$B8FA, the end: isr_wait_frame; par_frames_left = 3; per rest frame frm_copy_rest_frame, frm_snapshot,
   frm_clamp_mark_space and frm_transition_flags (C, the registers flowing from one to the next), a wait for the
   swap, the flag cleared (LDM #0) and DEC par_frames_left (BNE $B8DA). Then cmd_ev_sound_end (a @T effect cut short
   is undone) and isr_par_ready. */
```

replace

```c
        fw_rom_call(f, ROM_FRM_COPY_REST_FRAME, r, FW_ROM_CALL_BUDGET);
        fw_rom_call(f, ROM_FRM_SNAPSHOT, r, FW_ROM_CALL_BUDGET);
        fw_rom_call(f, ROM_FRM_CLAMP_MARK_SPACE, r, FW_ROM_CALL_BUDGET);
        fw_rom_call(f, ROM_FRM_TRANSITION_FLAGS, r, FW_ROM_CALL_BUDGET);
```

with

```c
        frm_copy_rest_frame(f, r);
        frm_snapshot(f, r);
        frm_clamp_mark_space(f, r);
        frm_transition_flags(f, r);
```

`src/firmware/par_addr.h`: replace

```c
#define ROM_FRM_SNAPSHOT           0xB917   /* called by par_play $B8DD (and frm_finish_frame $B9BA) */
#define ROM_FRM_CLAMP_MARK_SPACE   0xB930   /* called by par_setup_voice $B40A, $B419, par_play $B8E0 (and
                                               frm_finish_frame $B9BD) */
#define ROM_FRM_RAISE_FORMANTS     0xB95D   /* called by (frm_finish_frame $B9C0) */
```

with

```c
#define ROM_FRM_SNAPSHOT           0xB917   /* called by par_play $B8DD, frm_finish_frame $B9BA */
#define ROM_FRM_CLAMP_MARK_SPACE   0xB930   /* called by par_setup_voice $B40A, $B419, par_play $B8E0,
                                               frm_finish_frame $B9BD */
#define ROM_FRM_RAISE_FORMANTS     0xB95D   /* called by frm_finish_frame $B9C0 */
```

replace

```c
#define ROM_FRM_FINISH_FRAME       0xB9B5   /* M4.5; called by par_play $B8C3, $B90E */
#define ROM_FRM_TRANSITION_FLAGS   0xBA3F   /* M4.5; called by par_play $B8E3 (and frm_finish_frame $BA36) */
```

with

```c
#define ROM_FRM_FINISH_FRAME       0xB9B5   /* called by par_play $B8C3, $B90E */
#define ROM_FRM_TRANSITION_FLAGS   0xBA3F   /* called by par_play $B8E3, frm_finish_frame $BA36 */
```

`src/firmware/fw.h`: replace

```c
#define FW_TMP_75    0x0075   /* zero-page scratch, unnamed in the annotations: tn_convert_clause's saved X;
                                 frm_clamp_mark_space's 2 x b10 ($B936; called by par_setup_voice at $B40A and
                                 $B419) and frm_raise_formants' (b10 - 32) / 4 ($B969) */
```

with

```c
#define FW_TMP_75    0x0075   /* zero-page scratch, unnamed in the annotations: tn_convert_clause's saved X;
                                 frm_clamp_mark_space's 2 x b10 ($B936), frm_raise_formants' (b10 - 32) / 4 ($B969)
                                 and frm_transition_flags' count of the rising amplitudes ($BAA7-$BAB7) */
```

`src/engine/thunks.c`: replace

```c
static void frm_raise_formants_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_raise_formants(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

with

```c
static void frm_raise_formants_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_raise_formants(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* frm_finish_frame $B9B5: A, X and Y in, the same out (PHA / TXA / PHA / TYA / PHA at $B9B5-$B9B9, PLA / TAY / PLA /
   TAX / PLA at $BA39-$BA3D; the pushes are dead stack at the RTS). par_play ($B8C3, $B90E) clears isr_frame_flag
   (LDM) and then writes X ($B877 LDX cmd_ev_next) or calls isr_par_ready (PHA / PLA), isr_wait_frame or
   par_step_tracks, none of which reads them, before it writes A, X and Y (par_step_tracks' thunk). No caller reads
   a flag. The C par_play calls the C (M4.5), so the hook fires only while the ROM par_play runs. Declared: A, X,
   Y. */
static void frm_finish_frame_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_finish_frame(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}

/* frm_transition_flags $BA3F: X passes through (A and Y are loaded first). A = frm_flags, X untouched and Y = $0D
   out (the RTS at $BAEB). frm_finish_frame ($BA36) restores A, X and Y next (PLA / TAY / PLA / TAX / PLA); par_play
   ($B8E3) waits (isr_par_ready, PHA / PLA), then calls frm_copy_rest_frame (LDY and LDA first, X kept) or, after
   the last rest frame, cmd_ev_sound_end (A as it came in unless it restores; X and Y kept) and isr_par_ready, which
   return them as par_play's exit registers (its thunk). No caller reads a flag. Declared: A, X, Y. */
static void frm_transition_flags_regs(fw *f, cpu740 *c)
{
    fw_regs r = { c->a, c->x, c->y, 0 };
    frm_transition_flags(f, &r);
    c->a = r.a;
    c->x = r.x;
    c->y = r.y;
}
```

replace

```c
THUNK(pros_microprosody)
THUNK(par_play_start)
```

with

```c
THUNK(pros_microprosody)
THUNK(frm_finish_frame)
THUNK(frm_transition_flags)
THUNK(par_play_start)
```

replace

```c
    { "pros_microprosody",  0xB983, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_microprosody },
```

with

```c
    { "pros_microprosody",  0xB983, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_pros_microprosody },
    { "frm_finish_frame",   0xB9B5, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_finish_frame },
    { "frm_transition_flags", 0xBA3F, FW_OUT_A | FW_OUT_X | FW_OUT_Y, t_frm_transition_flags },
```

`tests/test_golden.c`: replace the end of the comment above `test_timing_invariance`

```c
   engine, then the hybrid engine with every replacement (M4.4b spec 4.4): its frame loop is C, which takes no
   emulated time, so it builds faster (frm_finish_frame is still ROM and takes cycles); the produced frames must not
   change either way. */
```

with

```c
   engine, then the hybrid engine with every replacement (M4.4b spec 4.4): its frame loop and frame builder are C
   (M4.5), which take no emulated time, so it builds faster (only its isr_* and ser_* ROM calls take cycles); the
   produced frames must not change either way. */
```

- [ ] **Step 4: Faithfulness check against the dumps**

Read each dump side by side with the C, block by block (each static helper names its range):
- `rom_BA3F_frm_transition_flags.txt`: $BA3F–$BA41 and $BAE4–$BAEB in `frm_transition_flags`; `jump_flag` (the two loops, `CMP #$10`/`#$F1`, `CMP #$0C`/`#$F5`, each `BPL` → `!minus(d, k)`, each `BMI` → `minus(d, m)`), `fall_flag` (`CMP #1` / `BEQ` picks `#$13` or `#$1C`), `rise_flag` (`LDM #0`, snap − prev, `CMP #$0C` / `BMI`, `INC $75` → `fw_inc8`, `CMP #2` / `BMI`), `pitch_flag` (`CMP #7`, `CMP #$FA`); each `ORA` a read-modify-write of `frm_flags` (`set_flag`).
- `rom_B9B5_frm_finish_frame.txt`: the saves → `fw_regs s = *r` (r untouched); the three `JSR`s with `s`; `source_filter` ($B9C3–$B9D7, `fw_rom8(FRM_SOURCE_CODES + X)`, `FRM_FILTER_CODES + X`); `level_diff` ($B9D8–$B9E9) and `f1_excess` ($B9EC–$B9FB), each `BEQ` / `BMI` → `== 0 || bit 7`; `product_word` ($B9FC–$BA26: `fw_rom8(FRM_PRODUCT_LIMIT + e)`, `CMP` / `BMI` → `minus`, `LDA #$FF` → `FRM_PRODUCT_SATURATED`, `TAX`, the lo/hi reads); `multiply` ($BA04–$BA17); `b14_15` ($BA29–$BA34: b15 written first, then `frm_b14` read back into b14); `JSR frm_transition_flags` with `s`.
- `rom_B877_par_play.txt`: `segment_step` ($B908–$B911) and `last_frames` ($B8BB–$B8CF) call `frm_finish_frame` with `r`; `rest_frames` ($B8DA–$B8E3) calls the four in order with `r`.

Confirm the callers and that nothing branches into the two entries:

```bash
python -c "import re,os;d=open(os.environ['APOLLO2_ROM'],'rb').read();[print(hex(a),hex(op),[hex(m.start()) for m in re.finditer(re.escape(bytes([op,a&255,a>>8])),d)]) for a in (0xB9B5,0xBA3F) for op in (0x20,0x4C)]"
python -c "
import os
d=open(os.environ['APOLLO2_ROM'],'rb').read()
br={0x10,0x30,0x50,0x70,0x80,0x90,0xB0,0xD0,0xF0}
s=lambda b:b-256 if b>127 else b
for t in (0xB9B5,0xBA3F):
  h=[hex(p) for p in range(0x8000,0xFFFE) if (d[p] in br or d[p]&0x1F in (3,0x13)) and p+2+s(d[p+1])==t]
  h+=[hex(p) for p in range(0x8000,0xFFFD) if d[p]&0x1F in (7,0x17) and p+3+s(d[p+2])==t]
  print(hex(t),h)
"
grep -rn "ROM_FRM_" src/firmware | cut -d: -f1 | sort | uniq -c
```

Expected:

```
0xb9b5 0x20 ['0xb8c3', '0xb90e']
0xb9b5 0x4c []
0xba3f 0x20 ['0xb8e3', '0xba36']
0xba3f 0x4c []
0xb9b5 []
0xba3f []
      7 src/firmware/par_addr.h
```

(the seven `#define`s and nothing else: no `ROM_FRM_*` `fw_rom_call` remains in `src/firmware/`).

- [ ] **Step 5: The branch probe (recorded; the tool is not part of the plan)**

Spec §4.5 and §4.7 criterion 4: every conditional branch of the seven routines seen both ways on the corpus. `apollo2_coverage` records instructions, not edges (a branch whose target is also reached another way looks covered), so the plan was proved with a branch probe: a scratch tool (not committed, not part of the plan) built against the repo sources, which runs boot and the 47 corpus texts on the rom engine with a machine hook on each of the 32 conditional branches of the seven routines; the hook fires before the branch executes and evaluates it from the flags (`BPL`/`BMI` N, `BCC`/`BCS` C, `BNE`/`BEQ` Z). Result on the committed corpus (the proof run; it runs only the ROM, so it is the same after any task):

| Branch | Routine | Taken | Fell through |
|---|---|---|---|
| $B426 `BNE` | `frm_build_rest_frame` | 2261 | 119 |
| $B438 `BCC` | `frm_build_rest_frame` | 357 | 119 |
| $B44D `BPL` | `frm_copy_rest_frame` | 14383 | 757 |
| $B920 `BNE`, $B92D `BNE` | `frm_snapshot` | 220644 each | 24516 each |
| $B943 `BCS` | `frm_clamp_mark_space` | 24639 | 115 |
| $B94C `BMI` | `frm_clamp_mark_space` | 24543 | 211 |
| $B950 `BCC` | `frm_clamp_mark_space` | 41 | 170 |
| $B958 `BCC` | `frm_clamp_mark_space` | 24547 | 207 |
| $B964 `BPL` | `frm_raise_formants` | 16600 | 7397 |
| $B9E5 `BEQ` | `frm_finish_frame` | 355 | 23642 |
| $B9E7 `BMI` | `frm_finish_frame` | 21299 | 2343 |
| $B9F4 `BEQ` | `frm_finish_frame` | 3 | 2340 |
| $B9F6 `BMI` | `frm_finish_frame` | 2310 | 30 |
| $BA02 `BMI` | `frm_finish_frame` | 14 | 16 |
| $BA0B `BCC` | `frm_finish_frame` | 104 | 24 |
| $BA15 `BNE` | `frm_finish_frame` | 112 | 16 |
| $BA4F `BPL` | `frm_transition_flags` | 234 | 97064 |
| $BA53 `BMI` | `frm_transition_flags` | 166 | 96898 |
| $BA58 `BNE` | `frm_transition_flags` | 72782 | 24116 |
| $BA63 `BPL` | `frm_transition_flags` | 929 | 112441 |
| $BA67 `BMI` | `frm_transition_flags` | 1302 | 111139 |
| $BA6A `BNE` | `frm_transition_flags` | 89254 | 21885 |
| $BA7E `BEQ` | `frm_transition_flags` | 57715 | 62273 |
| $BA89 `BPL` | `frm_transition_flags` | 9 | 62264 |
| $BA97 `BPL` | `frm_transition_flags` | 804 | 56911 |
| $BA9A `BNE` | `frm_transition_flags` | 95472 | 23703 |
| $BAB5 `BMI` | `frm_transition_flags` | 118337 | 4243 |
| $BABA `BNE` | `frm_transition_flags` | 98064 | 24516 |
| $BAC0 `BMI` | `frm_transition_flags` | 23174 | 1342 |
| $BAD3 `BPL` | `frm_transition_flags` | 10 | 24506 |
| $BAD7 `BMI` | `frm_transition_flags` | 9 | 24497 |

**No branch is one-sided** (`32 branches, 0 one-sided`). On the 45 texts before Task 1 six were: $B943 (fall 0), $B9F4 (taken 0), $B9F6 (fall 0), $BA02, $BA0B and $BA15 (never reached); `mark_space_cap` gives $B943's fall-through (the cap written at $B945) and `formant_extremes` the other five. The unit tests pin the rarest edges as well: $B9F4 taken (`test_finish_frame`, F1 = 33), $BA02 both ways and $BA0B both ways (the saturated and the multiplied cases), $B943 (`test_clamp_mark_space`), $BA89, $BA97, $BAD3 and $BAD7 (`test_transition_flags`).

- [ ] **Step 6: Run the tests**

Run, in order: `cmd //c ".\build.bat test fw"`, then the full suite `cmd //c ".\build.bat test"`.
Expected:
- the `fw` run: the build passes with no warnings, the seven `fw` suites `ok`, `7 suite(s), 0 failure(s), 0 skip(s)`.
- the full run (480 s in the proof run): `34 suite(s), 0 failure(s), 0 skip(s)`, no warnings, the firmware syntax check passing; `firmware_data` `48496 converted bytes compared with the ROM, 0 differ`.
- `diff_lts`: 69 lines `<name> N calls checked`, each N > 0, 0 mismatches; the two new ones (the proof run)

```
  frm_finish_frame     18626 calls checked
  frm_transition_flags 19113 calls checked
```

  (`frm_finish_frame` equals `frm_raise_formants`, its only caller's callee); every other line identical to Task 2's; `1 mid-clause flush calls skipped (golden_hybrid covers them)`, `2 unwound calls compared`.
- `diff_play` (now comparing the C frame builder inside the C `par_play` at every swap):

```
  self-check (ROM against ROM, hello): par_play 1 calls, boot_loop_play 2 calls, 2 injected; $B8A5 reached 2, $B89C reached 2
  2 routines wait on the timer interrupt
  boot_loop_play       179 calls checked, 48 with both injections
  par_play             126 calls checked, 48 with both injections
  $B8A5 (abort) reached in 96 calls, $B89C (reply due) in 96
```

  0 mismatches.
- `golden_hybrid`: `48 corpus files x 72 modes (all, then each routine alone)`, all identical.
- `suite_golden`: `hybrid timer scale 1/4: 105 frames, 11699792 CPU cycles`, `hybrid timer scale 3/1: 105 frames, 997035 CPU cycles` (the rom-engine lines unchanged).
- `suite_synth`: `corpus peak plain 20522 (-4.1 dBFS)`, `corpus peak b11 16038 (-6.2 dBFS)`.

If `diff_lts` or `diff_play` reports a mismatch, find the root cause in the dump and fix the C faithfully; never loosen the comparison.

Then confirm spec criterion 7 on every corpus text (each read from its file; the outputs go to `build/`, always with `-o`):

```bash
SP=build/m45_cli
mkdir -p "$SP"
for f in tests/corpus/*.txt; do
  t=$(basename "$f" .txt)
  ./build/apollo2.exe --engine rom --text-file "$f" -o "$SP/${t}_rom.wav" || echo "$t rom failed"
  ./build/apollo2.exe --engine hybrid --text-file "$f" -o "$SP/${t}_hybrid.wav" || echo "$t hybrid failed"
  cmp -s "$SP/${t}_rom.wav" "$SP/${t}_hybrid.wav" && echo "$t same" || echo "$t DIFFERS"
done | grep -vc " same$"
rm -rf "$SP"
```

Expected: `0` (all 47 texts `same`; no `failed` or `DIFFERS` line). The proof run took 15 s.

- [ ] **Step 7: Commit**

```bash
git add src/firmware/frm_build.h src/firmware/frm_build.c src/firmware/par_play.c src/firmware/par_addr.h src/firmware/fw.h src/engine/thunks.c tests/test_fw_frm.c tests/test_fw_par.c tests/test_fw_tn.c tests/test_golden.c
git commit -m "frm_transition_flags and frm_finish_frame in C, with hooks; the C par_play calls the C frame builder, so no frm_* ROM call remains (M4.5 2.2-2.6, 4.5-4.7)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 4: Parent spec rev 14, M4.5 status and README (spec §4.7 criterion 10)

**Files:**
- Modify: `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`, `docs/superpowers/specs/2026-10-08-m4-5-frame-builder-design.md`, `README.md`

**Interfaces:** documentation only.

- [ ] **Step 1: Parent spec, rev 14**

Make these edits in `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`. Mark each one *(rev 14)*. Find each place by its text, not by line number.

1. **Status line** (line 4). Insert after `**Status:** Approved. `:

   `*(rev 14)* Revision 14 records milestone 4.5: the frame builder in C (the rest frame, the snapshot, the mark-space clamp, the formant raise, b13-b17), so playback calls the ROM only for the interrupt and serial routines; the frame builder's tables converted, three of them as formulas (design: docs/superpowers/specs/2026-10-08-m4-5-frame-builder-design.md). `

2. **§8 milestone 4** (the paragraph containing `**M4.4b done**`). Replace its last sentence, `Next: M4.5, the frame builder.`, with:

   `Next: M4.5, the frame builder. *(rev 14)* **M4.5 done** <DATE>: the frame builder runs as C (frm_build_rest_frame, frm_copy_rest_frame, frm_snapshot, frm_clamp_mark_space, frm_raise_formants, frm_finish_frame, frm_transition_flags; 71 routines hooked in all), and the C par_setup_voice and par_play call it directly; frm_template, frm_source_codes and frm_filter_codes are converted, and frm_product_limit (255 / e) and frm_b14_15_lo/hi (82 x i) as formulas the encoder expands (48,496 bytes in all); two new corpus goldens (rom engine), formant_extremes and mark_space_cap, reach the b14-b15 product and the upper mark-space clamp; diff_lts (69 routines), diff_play (its abort injection now at isr_par_ready), golden_hybrid (48 files x 72 modes) and test_timing_invariance on both engines pass. Next: M4.6, the boot, serial, bank and interrupt code.`

   Replace `<DATE>` with the date of your commit (`date +%F`).

- [ ] **Step 2: M4.5 spec status and As built**

In `docs/superpowers/specs/2026-10-08-m4-5-frame-builder-design.md`, replace `**Status:** design, awaiting user review` with:

`**Status:** implemented (plan docs/superpowers/plans/2026-10-08-m4-5-frame-builder.md)`

Then add a final section headed `## 6. As built`, after §5. Open it with the sentence `Where the ROM is more precise than, or differs from, the sections above. The ROM wins; ``ghidra740/`` is not edited.` (one pair of backquotes around `ghidra740/`). Then write one short factual bullet per difference between the spec and what was built. Gather them from:
- the "ROM facts checked" lists at the top of Tasks 1–3 of the plan, keeping only those that contradict or sharpen the spec;
- the code:
  - the thunk outputs as finally declared for the 7 new hooks (read the rows and comments in `src/engine/thunks.c`) and why;
  - the C layout of `frm_build.c` and its static helpers; `frm_data.c` and the encoder's formulas in `fw_data.c`;
  - `diff_play`'s abort injection as built (the hook, its condition) and its counts;
  - the unit-test changes in `tests/test_fw_par.c` and `tests/test_fw_cmd.c`, and what `tests/test_fw_frm.c` covers;
  - the branch probe's result (any branch pinned by a unit test instead of the corpus).

Every bullet must be true of the committed code and the dumps. A claim about a test must say only what that test checks. Follow the style of §6 of `docs/superpowers/specs/2026-10-07-m4-4b-frame-loop-design.md`. If nothing differs, add no section.

- [ ] **Step 3: README**

In `README.md`'s "Hybrid engine (milestone 4)" section, replace:

```
boundary and dominance pass, track planning and the voice set-up (M4.4a), and the frame loop: par_play, the track
steppers, the command events and boot_loop_play (M4.4b); the rest still runs on the emulated CPU.
```

with:

```
boundary and dominance pass, track planning and the voice set-up (M4.4a), the frame loop: par_play, the track
steppers, the command events and boot_loop_play (M4.4b), and the frame builder: the rest frame, the snapshot, the
mark-space clamp, the formant raise and the b13-b17 bytes (M4.5); the rest (the boot, serial, bank and interrupt
code) still runs on the emulated CPU.
```

- [ ] **Step 4: Run the full suite and commit**

Run: `cmd //c ".\build.bat test"`
Expected: `34 suite(s), 0 failure(s), 0 skip(s)`.

```bash
git add docs/superpowers/specs/2026-09-30-software-apollo2-design.md docs/superpowers/specs/2026-10-08-m4-5-frame-builder-design.md README.md
git commit -m "Spec rev 14: milestone 4.5 done (the frame builder in C)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
