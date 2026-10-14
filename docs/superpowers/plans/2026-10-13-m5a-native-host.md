# M5a The Native Host and Engine — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the decompiled firmware with no ROM and no emulated CPU, through a CPU-less board and a native engine that are cycle-exact with the all-C hybrid. Prove it with `diff_native` (hybrid against native at every event of every `diff_main` run, no exclusions) and with every golden reproduced on native without the ROM.

**Architecture:**
- **The board (Task 1).**
  - `m50734` moves to `src/board/`, and `board.c` adds what the machine holds outside the CPU:
    - RAM, the I flag and the cycle clock with its scale;
    - SFR access with the unmodelled log and the DME check;
    - the DSP port with raw grouping and reset edges, and frame capture;
    - the serial queue with delivery at the idle point, and the idle test;
    - the step that mirrors `machine_step` (WIT 4 cycles, interrupt entry 7, the mirrored loop instructions);
    - dispatch through the converted vector table `$FFF4–$FFFF`.
- **The native host (Task 2).** The `fw_host` on a board.
  - The five waits run with the hybrid's own cycle charges.
  - `read_bank` returns `$FF`.
  - `enter` records a bank entry.
  - `rom_call` fails, since there is no ROM.
- **The native engine (Task 3).** It runs `boot_reset` from the reset vector under the ROM engine's phase budgets, with the same idle handler.
  - Bank entries run their C afresh.
  - A NUL-bug address ends the run as a fault at `$A893`.
  - `frame_list`, `engine_input` and `engine_status` become neutral types.
- **The oracle (Task 4).** `diff_native` runs every `diff_main` run through the hybrid and then native, and compares each event:
  - all RAM except the SFR bytes and the stack page;
  - the whole `m50734`, the cycles, the DSP outputs, the frame counts, the I flag and the queue.
  - Injections are shared with `diff_main` through a neutral view, and `diff_main`'s output does not change.
- **Native everywhere else (Task 5).** `golden_native`, timing invariance on native, and `render_text_native` with its sample comparison; then the no-ROM worktree run.
- **Unchanged.** `apollo2.exe`, the test-side tools, the machine model, the firmware C and every existing comparison.

**Tech Stack:** C11 with MSVC 2022 (`build.bat`, `/W4 /WX`).

**Spec:** `docs/superpowers/specs/2026-10-13-m5a-native-host-design.md` (M5a spec). Its foundations are:
- the M4.6c2 spec, `docs/superpowers/specs/2026-10-13-m4-6c2-boot-main-loop-design.md` (the input wait and the idle handler, phase budgets, entries run afresh, `diff_main`; §6 "As built");
- the M4.6c1 spec, `docs/superpowers/specs/2026-10-13-m4-6c1-banks-design.md` (`read_bank`, `enter`, the NUL bug; §6 "As built");
- the M4.6b spec, `docs/superpowers/specs/2026-10-09-m4-6b-speech-timing-design.md` (the wait kinds and their loops, `dsp_write`, `frame_done`; §6 "As built");
- the M4.6a spec, `docs/superpowers/specs/2026-10-08-m4-6a-serial-design.md` (SFR access through the host, `FW_WAIT_UART_TX`; §6 "As built").

The parent spec is `docs/superpowers/specs/2026-09-30-software-apollo2-design.md` (rev 18; §4 the machine model, §8 milestone 5).

## Global Constraints

**Build and run**
- Build and test from the repo root with the Bash tool (Git Bash): `cmd //c ".\build.bat test [SUITE_FILTER]"`.
- In a git worktree, first run `export APOLLO2_ROM="C:/Users/abart/Desktop/Dolphin Apollo 2 ROM/UK_V2.01_4.04_ROM.BIN"`, because the ROM lives only in the main checkout. Without it, a worktree has no ROM, and that is how the no-ROM run (Task 5) is made.
- C11, `/W4 /WX`, `/wd4244` (8-bit truncation is intentional).
  - Fix warnings in code. Never add `/wd` flags.
  - Pragmas: the existing 4611 one in `thunks.c`, and the same one in `native_engine.c`, which sets the firmware's fault trap with `setjmp`. No other pragma. `setjmp` stays out of the tests, the board and the firmware.
- Every `.c` file under `src/` and `tests/` must have a **unique basename**, because `build.bat` puts all objects in `build\`.
- Never run `build\apollo2.exe` without `-o FILE`, because it plays audio on the user's speakers.
- Audio stays 10 kHz native, with no resampling.
- The full suite takes about 16 minutes. Use a suite filter while iterating and the full run at each task's end, in the background (a foreground Bash call is capped at 600000 ms).

**What may and may not be committed**
- Never commit the ROM, any `*.BIN`/`*.bin`, `*.mp3`, `apollo2_excerpt.wav`, `*.sla`, `build/` or `apollo_ghidra/`.
- Committed files contain no ROM bytes as hex blobs. Converted data is readable C: numbers with their meaning, named bits, strings, named addresses.
- Stage files by explicit path only. Never use `git add -A` or `git add .`. A moved file is staged with `git mv`.

**User-owned files and tools**
- Never stage, move or delete the user's untracked files: `docs/*` other than `docs/superpowers/`, `pics/`, `session_memory.json`, `apollo2_for_aksel.wav`, `apollo2_for_aksel.mp3`, `apollo2.png`.
- Never modify anything under `C:\ghidra_12.1.3_PUBLIC`, the user's Ghidra GUI project, or `ghidra740/` (including `ghidra740/coverage/`). Only read the annotations and the dumps.
- Disassembly dumps for checking against the ROM (git-ignored, read-only) are in the main checkout:
  - `C:\Users\abart\Desktop\Dolphin Apollo 2 ROM\build\ghidra740\dump\rom_<ADDR>_<name>.txt` and `ramcode_<ADDR>_<name>.txt`
  - `data.txt` and `functions.txt` in the same folder.

**Correctness rules**
- The goldens in `tests/golden/*.frm` stay byte-identical. No new golden. If anything changes a golden, stop and report.
- The machine model (parent spec §4) does not change. `src/machine/` changes only in the include path of `m50734.h`, and `src/cpu740/` does not change.
- The firmware C under `src/firmware/` does not change, except the data that converts the vector table.
- `apollo2.exe`, `apollo2_frames`, `apollo2_coverage` and `cpu740_vectors` do not change in behaviour (M5b changes the CLI).
- Nothing under `src/firmware/`, `src/board/`, or the native files of `src/engine/` and `src/render/` includes a `machine/` or `cpu740/` header. `build.bat` checks this.
- The board mirrors `machine.c` and `cpu740.c` exactly where the all-C hybrid spends cycles or touches the board:
  - the WIT step (4 cycles), interrupt entry (7 cycles, the lowest line first, I saved and set, then restored as RTI pulls it);
  - the instructions and cycles of the loops at `$C500`, `$B372` and `$B37F`;
  - the order prepare (delivery, then the IRQ lines), act, tick, with the timer scale and its remainder;
  - delivery at the idle point and the idle test;
  - SFR side effects, the unmodelled log, the DME check, the DSP reset edge, raw grouping and frame capture.
- `diff_native` compares everything listed in M5a spec §4.3 with no exclusion beyond the SFR bytes and the stack page `$0100–$01FF` of RAM. A mismatch is fixed by finding its root cause in the board or the host, never by excluding the field or loosening an assertion.
- `diff_main`, `diff_lts`, `diff_play`, `diff_serial`, `golden_hybrid` and `test_timing_invariance` keep their output: `diff_main` 66 runs, 129 idles, 31,533 frame events.

**Commits**
- End every commit message with exactly `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. This is the user's rule for this session.

## Review Focus

These are the inputs and conditions most likely to bite a user that the spec implies but no other test exercises. Each line has a test in the task that owns the code.

1. **Several native runs in one process, one of them a fault.** A program that renders text after text (the M5b CLI, a future front end) must get from each run exactly what a fresh process gets. That includes a run right after a NUL-bug fault or an input overflow: no state may leak between runs, whether a static board, the data image or a sink. Test: `suite_native` (Task 3).
2. **The longest text the CLI accepts** (1500 characters, of sentences without a full stop for long clauses): 173 million cycles in all, 43% of the speaking budget. Native must end with the hybrid's status and frames, not reach the cycle limit at another step. Test: a `diff_native` extra run (Task 4).
3. **Input that just fits the serial queue, and one byte more.** `MACHINE_QUEUE_SIZE - 1` bytes, including the CR, must be accepted; one byte more must be the input-overflow fault, on native as on the ROM engine. Test: `suite_native` (Task 3), and a comparison with the ROM engine when the ROM is present.
4. **Bank commands with re-entries.** `@=1 One. @=5 Two. @=1 Three.` and `@=0 @=8 @=9 @=F @=G`: a warm `@=1` abandons the C and re-enters, and native must match the hybrid at every event. Test: `diff_native` extra runs (Task 4).
5. **The second voice and the prosody extremes through render.** `@K1` with `@R0` and with `@R7`, and a mute (Ctrl-X) in the middle of a text. These must give the same samples on native as on the hybrid. Test: `suite_render` (Task 5).

---

### Task 1: The board: `m50734` moved to `src/board`, `board.c` (RAM, the I flag, the cycle clock, SFR access, the DSP outputs, delivery and the idle test, the four waits, dispatch), the vector table converted, `suite_board`, the build check of `src/board` (M5a spec §3.1–§3.4, §4.1, §4.7 item 1)

**Facts checked / what was verified while proving the plan** (`src/machine/machine.c`, `src/cpu740/cpu740.c`, `src/cpu740/optable.h`, `src/machine/m50734.c`, `src/engine/thunks.c`; the dumps `rom_B35D_isr_timer_start.txt`, `rom_C500_ser_tx_wait.txt`; `ghidra740/annotations/boot.txt`; everything below was run, nothing is copied from the spec):
- **The cycles** (`cpu740_step`, `k_mode_cycles`): a waiting CPU with no line raised spends 4; any raised line ends the WIT, even with I set; a line with I clear is taken first (`take_irq`: the lowest line, its P pushed with I as it was, I set, the vector $FFFC − 2 × line read through the bus), 7. `BBC n,zp,rel` is `AM_BIT_ZP_REL`, 5 cycles, 2 more when taken (7); `RTS` 6. The loops: $B372 `77 FE FD` `BBC3 ICR2,$B372` (exit $B375), $B37F `B7 FE FD` `BBC5 ICR2,$B37F` (exit $B382), $C500 `37 E7 FD` `BBC1 UART_STATUS,$C500` then $C503 `60` `RTS`. `test_cycles_match_cpu` runs `cpu740_step` on those bytes and checks every board constant against it.
- **One step** (`machine_step` = `prepare` + `exec`): `prepare` delivers a serial byte at the idle point and then copies `m50734_irq_lines` to the CPU; `exec` first faults a runaway PC (`pc_escaped`: an SFR address, or $8000 and above while P2 selects another bank), then fires a hook (only when the CPU is not waiting and no line is raised with I clear: after an interrupt entry I is set, so the handler's hook fires at the next step), else runs `cpu740_step` and ticks the timers by its cycles through the scale (`acc = rem + cycles × num`, `rem = acc % den`, `m50734_tick(acc / den)`). A hook step takes 0 cycles and ticks nothing. In an all-C hybrid run every point of a wait is at $8000 or above (the entry hooks $A61C, $FFD0, $FFD4, the idle point $C603, the handlers, the loops), so the board checks the main bank at every step (`BOARD_FAULT_ESCAPED`), which is what `pc_escaped` does there.
- **The waits' bounds**: `machine_wait_interrupt` and `machine_wait_input` test their bound at the top of each iteration, then (the input wait only) the idle test, then the wake with I set, then `exec`; `machine_run_loop` tests it before each step and restores the PC and P after; `machine_rom_call` tests it before each step and returns at the sentinel after the `RTS`. So an interrupt entered as the bound passes leaves I set and its handler never runs (`test_wait_interrupt_bound`), and a loop's last pass may end past the bound and still return.
- **The SFRs** (`bus_read`, `bus_write`): a read of an unmodelled register or of write-only `UART_TX` $E4, and a write of an unmodelled one, are logged once per address and direction (then counted as dropped when the log is full); after every SFR write the DME check (`P0_FUNC` bit 5 with P0.5 an output) and the P2.0 edge (a rising edge counts a DSP reset and drops the partial raw frame). `m50734_read` of `UART_STATUS` always has bit 1 set: the $C500 loop never takes its branch on this model (5 + 6 cycles, plus any interrupt).
- **Frame capture** (`capture_frame`): the 20 bytes at the pointer at $2C read through `machine_peek` (an SFR reads 0, RAM, or ROM under the current P2); every frame buffer is in RAM.
- **The move**: `m50734.c` changes only in its include line, `m50734.h` not at all; `machine.h` and `test_m50734.c` include `board/m50734.h`. `git grep "machine/m50734"` finds those three lines and nothing else.
- **The vector table** ($FFF4–$FFFF, `boot.txt`): `vec_int4_timerx` $C3E0, `vec_int3_step` $C3DF, `vec_int2_timers` $B2D9, `vec_int1_serial` $C2B4, `vec_int0` $C3DF, `vec_reset` $A61C, each low byte first; the ROM has those 12 bytes there. `firmware_data` compares 49,104 bytes (49,092 + 12), and its coverage list gains $FFF4–$FFFF.
- **The build check**: the firmware check compiles without `/Isrc` and the firmware includes its headers by relative path (`"fw.h"`); the board includes `"board/m50734.h"` and the firmware's headers by `src`-relative path, so it is checked against `build\noemu`, a copy of `src\board` and `src\firmware` headers only: an include of `machine/` or `cpu740/` cannot be found there.
- **build.bat runs while cmd reads it**: editing `build.bat` while a build or a full suite runs from it makes cmd read the edited file at the old offset; edit it only when no run is in progress.

**Files:**
- Move: `src/machine/m50734.c` → `src/board/m50734.c`, `src/machine/m50734.h` → `src/board/m50734.h`
- Create: `src/board/board.h`, `src/board/board.c`, `tests/test_board.c`
- Modify: `src/board/m50734.c`, `src/machine/machine.h`, `tests/test_m50734.c`, `src/firmware/data/fw_data.h`, `src/firmware/data/fw_data.c`, `src/firmware/data/boot_data.c`, `tests/test_firmware_data.c`, `tests/suites.h`, `build.bat`

**Interfaces:**
- Consumes: `m50734_*` (`src/board/m50734.h`); `fw`, `fw_regs`, `fw_init` (`firmware/fw.h`); `isr_unused`, `boot_reset`, `bank_entry_cold`, `bank_entry_warm` (`firmware/boot.h`), `isr_timer_dsp` (`firmware/isr_dsp.h`), `isr_int1`, `isr_int4_host_line` (`firmware/isr_serial.h`); the address names `ROM_ISR_UNUSED`, `ROM_BOOT_RESET` (`boot_addr.h`), `ROM_ISR_TIMER_DSP` (`isr_addr.h`), `ROM_ISR_INT1`, `ROM_ISR_INT4_HOST_LINE` (`ser_addr.h`), `BANK_ENTRY_COLD`, `BANK_ENTRY_WARM` (`bank_addr.h`); `put_word`, `check_end` (`fw_data.c`).
- Produces (Tasks 2–5 use them):

```c
/* src/board/board.h */
#define BOARD_RAM_SIZE 0x4000, BOARD_QUEUE_SIZE 4096, BOARD_FRAME_BYTES 20, BOARD_PRODUCER_PTR 0x2C, BOARD_P0_BUSY 0x20 ...
#define BOARD_WIT_CYCLES 4, BOARD_IRQ_CYCLES 7, BOARD_BBC_EXIT 5, BOARD_BBC_PASS 7, BOARD_RTS_CYCLES 6
#define BOARD_VEC_FIRST 0xFFF4, BOARD_VEC_RESET 0xFFFE, BOARD_EMPTY_VECTOR 0xFFFF
typedef enum { BOARD_FAULT_NONE, BOARD_FAULT_DME, BOARD_FAULT_ESCAPED, BOARD_FAULT_VECTOR, BOARD_FAULT_INPUT_OVERFLOW,
               BOARD_FAULT_FIRMWARE } board_fault;
typedef enum { BOARD_STOP = 0, BOARD_FEED } board_idle_action;
typedef board_idle_action (*board_idle_fn)(struct board *b, void *user);
typedef enum { BOARD_INPUT_FAILED = 0, BOARD_INPUT_HANDLED, BOARD_INPUT_STOPPED } board_input_result;
typedef struct board { ram, sfr, i, waiting, idle_point, handler_due, handler_i, handler, irq_lines, cycles, cycle_num,
    cycle_den, cycle_rem, fault, fault_addr, queue, q_head, q_tail, busy_level_known, busy_idle_level, on_frame, user,
    on_raw, raw_user, raw_buf, raw_pos, dsp_reset_level, raw_frames, dsp_resets, raw_discarded, frames_produced,
    port_writes, unmodelled, unmodelled_count, unmodelled_dropped, rom, fw } board;
void board_init(board *b, const uint8_t port_in[5], uint32_t cycle_num, uint32_t cycle_den);
void board_set_outputs(board *b, board_frame_fn on_frame, void *user);
void board_set_raw_output(board *b, board_frame_fn on_raw, void *user);
bool board_feed_serial(board *b, const uint8_t *bytes, size_t n);
uint8_t board_read_sfr(board *b, uint8_t addr);
void board_write_sfr(board *b, uint8_t addr, uint8_t v);
void board_dsp_write(board *b, uint8_t v);
void board_capture_frame(board *b);
uint8_t board_peek(const board *b, uint16_t addr);
bool board_main_bank(const board *b);
void board_prepare(board *b);
bool board_idle(const board *b);
uint16_t board_vector(const board *b, uint16_t addr);
bool board_has_handler(uint16_t addr);
bool board_call(board *b, uint16_t addr, uint8_t a, uint8_t x);
bool board_wait_interrupt(board *b, uint64_t end);
board_input_result board_wait_input(board *b, uint64_t end, board_idle_fn idle, void *user);
bool board_wait_timer(board *b, unsigned bit, uint64_t end);
bool board_wait_uart_tx(board *b, uint64_t end);
/* src/firmware/data/fw_data.h */
#define FW_VECTORS 0xFFF4
extern const uint16_t fw_vec_int4_timerx, fw_vec_int3_step, fw_vec_int2_timers, fw_vec_int1_serial, fw_vec_int0,
    fw_vec_reset;
/* tests/test_board.c: suite_board, the 41st suite */
```

No name collides: `grep -rn "board_\|BOARD_\|fw_vec_\|FW_VECTORS\|suite_board" src tests` prints nothing before this task.

- [ ] **Step 1: Write the failing tests**

`tests/test_board.c` checks the board on hand-built states with no ROM, the handlers being the firmware's own C on a host that reads and writes the board's SFRs; `test_firmware_data.c` expects the vector table.

**Create `tests/test_board.c`:**

```c
/* The CPU-less board (M5a spec 4.1), on hand-built states and no ROM: each step's cycles against the emulated CPU's,
   interrupt entry and dispatch, the four waits, delivery and the idle test, the DSP outputs and frame capture, the
   timer scale, the unmodelled log and the faults. The handlers that run are the firmware's own C (isr_timer_dsp,
   whose CLB5 ICR2 drops timer 3's line; isr_unused), on a firmware context whose host reads and writes the board's
   SFRs; the vectors come from the converted image. */
#include <string.h>
#include "test.h"
#include "board/board.h"
#include "machine/machine.h"
#include "cpu740/cpu740.h"
#include "firmware/fw.h"
#include "firmware/boot_addr.h"
#include "firmware/isr_addr.h"
#include "firmware/ser_addr.h"
#include "firmware/bank_addr.h"
#include "firmware/cmd_addr.h"
#include "firmware/data/fw_data.h"

#define T2_PRE      0xDE
#define T2_RELOAD   0xDF
#define T3_PRE      0xE0
#define T3_RELOAD   0xE1
#define ICR1        0xFF
#define ICR2        0xFE
#define ICR2_T3     0x30   /* timer 3's enable (bit 4) and request (bit 5): line 2 */
#define P0          0xF6
#define P0_DIR      0xF7
#define P0_FUNC     0xF5
#define P2          0xF0
#define P2_DIR      0xF1
#define UART_RX     0xE5
#define UART_CTRL   0xE6

static const uint8_t k_pins[5] = { 0x00, 0x00, 0x70, 0x00, 0x00 };   /* machine_default_config's */
static board g_b;
static fw g_f;
static fw_host g_host;
static uint8_t g_frame[BOARD_FRAME_BYTES];
static unsigned g_frames, g_raws;

static uint8_t host_read_sfr(void *ctx, uint8_t addr) { return board_read_sfr(ctx, addr); }
static void host_write_sfr(void *ctx, uint8_t addr, uint8_t v) { board_write_sfr(ctx, addr, v); }

/* A fresh board with the converted image's vectors and a firmware whose host is the board's SFRs. */
static void setup(void)
{
    board_init(&g_b, k_pins, 1, 1);
    memset(&g_host, 0, sizeof g_host);
    g_host.ctx = &g_b;
    g_host.read_sfr = host_read_sfr;
    g_host.write_sfr = host_write_sfr;
    fw_init(&g_f, g_b.ram, fw_image_get(), &g_host);
    g_b.rom = fw_image_get()->rom;
    g_b.fw = &g_f;
    g_b.i = false;
}

static void on_frame(void *user, const uint8_t frame[BOARD_FRAME_BYTES])
{
    (void)user;
    memcpy(g_frame, frame, BOARD_FRAME_BYTES);
    g_frames++;
}

static void on_raw(void *user, const uint8_t frame[BOARD_FRAME_BYTES])
{
    (void)user;
    memcpy(g_frame, frame, BOARD_FRAME_BYTES);
    g_raws++;
}

/* The board's constants are the machine's and the firmware's (M5a spec 3.2). */
static void test_constants(void)
{
    CHECK_EQ(BOARD_RAM_SIZE, MACHINE_RAM_SIZE);
    CHECK_EQ(BOARD_QUEUE_SIZE, MACHINE_QUEUE_SIZE);
    CHECK_EQ(BOARD_MAX_UNMODELLED, MACHINE_MAX_UNMODELLED);
    CHECK_EQ(BOARD_FRAME_BYTES, APOLLO_FRAME_BYTES);
    CHECK_EQ(BOARD_PRODUCER_PTR, APOLLO_PRODUCER_PTR);
    CHECK_EQ(BOARD_P2_BANK_MASK, P2_BANK_BITS);
    CHECK_EQ(BOARD_P2_MAIN_BANK, P2_MAIN_BANK);
    CHECK_EQ(BOARD_ICR2, M50734_ICR2);
    CHECK_EQ(BOARD_UART_STATUS, M50734_UART_STATUS);
    CHECK_EQ(BOARD_VEC_FIRST, FW_VECTORS);
}

static uint8_t g_mem[0x10000];
static uint8_t mem_read(void *ctx, uint16_t a) { (void)ctx; return g_mem[a]; }
static void mem_write(void *ctx, uint16_t a, uint8_t v) { (void)ctx; g_mem[a] = v; }

/* The board's cycles are cpu740_step's on the instructions the waits mirror ($B372 BBC3 ICR2,$B372; $C500 BBC1
   UART_STATUS,$C500; $C503 RTS), a WIT step and an interrupt entry. */
static void test_cycles_match_cpu(void)
{
    cpu740 c;
    cpu740_bus bus = { NULL, mem_read, mem_write };
    memset(g_mem, 0, sizeof g_mem);
    g_mem[0xFFFE] = 0x72;
    g_mem[0xFFFF] = 0xB3;
    memcpy(&g_mem[FW_WAIT_TIMER2_LOOP], (const uint8_t[]){ 0x77, BOARD_ICR2, 0xFD }, 3);
    memcpy(&g_mem[FW_WAIT_UART_TX_LOOP], (const uint8_t[]){ 0x37, BOARD_UART_STATUS, 0xFD, 0x60 }, 4);
    cpu740_reset(&c, &bus);
    CHECK_EQ(c.pc, FW_WAIT_TIMER2_LOOP);
    CHECK_EQ(cpu740_step(&c, &bus), BOARD_BBC_PASS);   /* bit 3 clear: taken, back to itself */
    CHECK_EQ(c.pc, FW_WAIT_TIMER2_LOOP);
    g_mem[BOARD_ICR2] = 0x08;
    CHECK_EQ(cpu740_step(&c, &bus), BOARD_BBC_EXIT);   /* set: on to $B375 */
    CHECK_EQ(c.pc, FW_WAIT_TIMER2_EXIT);
    c.pc = FW_WAIT_UART_TX_LOOP;
    CHECK_EQ(cpu740_step(&c, &bus), BOARD_BBC_PASS);
    g_mem[BOARD_UART_STATUS] = 0x02;
    CHECK_EQ(cpu740_step(&c, &bus), BOARD_BBC_EXIT);
    CHECK_EQ(cpu740_step(&c, &bus), BOARD_RTS_CYCLES);
    c.waiting = true;
    CHECK_EQ(cpu740_step(&c, &bus), BOARD_WIT_CYCLES);
    cpu740_set_irq(&c, 2, true);
    c.p &= (uint8_t)~CPU740_I;
    CHECK_EQ(cpu740_step(&c, &bus), BOARD_IRQ_CYCLES);
}

/* board_init as machine_init: I set, 0 cycles, the scale, the pins, the P2.0 level. */
static void test_init(void)
{
    static const uint8_t pins[5] = { 0x00, 0x00, 0x71, 0x00, 0x00 };
    board_init(&g_b, pins, 0, 3);
    CHECK(g_b.i);
    CHECK_EQ(g_b.cycles, 0);
    CHECK_EQ(g_b.cycle_num, 1);
    CHECK_EQ(g_b.cycle_den, 3);
    CHECK_EQ(m50734_port_pins(&g_b.sfr, 2), 0x71);
    CHECK(g_b.dsp_reset_level);
    CHECK(g_b.rom == NULL && g_b.fw == NULL);
}

/* A WIT step is 4 cycles, an interrupt entry 7; the handler (isr_timer_dsp, its CLB5 ICR2) runs in 0 cycles and its RTI
   restores I. Timer 3 at prescaler 0 and reload 9 expires after 40 cycles: ten WIT steps. */
static void test_wait_interrupt(void)
{
    setup();
    board_write_sfr(&g_b, ICR2, 0x10);
    board_write_sfr(&g_b, T3_PRE, 0);
    board_write_sfr(&g_b, T3_RELOAD, 9);
    CHECK(board_wait_interrupt(&g_b, 1000));
    CHECK_EQ(g_b.cycles, 10 * BOARD_WIT_CYCLES + BOARD_IRQ_CYCLES);
    CHECK(!g_b.i);                         /* restored */
    CHECK(!g_b.waiting && !g_b.handler_due);
    CHECK_EQ(g_b.handler, ROM_ISR_TIMER_DSP);
    CHECK_EQ(g_b.sfr.icr2 & 0x20, 0);      /* the handler cleared the request */
}

/* The bound: at 40 the wait ends before the interrupt; at 41 the entry is made (7 cycles) but the bound has passed
   before its handler runs: I stays set, the handler never runs. */
static void test_wait_interrupt_bound(void)
{
    setup();
    board_write_sfr(&g_b, ICR2, 0x10);
    board_write_sfr(&g_b, T3_PRE, 0);
    board_write_sfr(&g_b, T3_RELOAD, 9);
    CHECK(!board_wait_interrupt(&g_b, 40));
    CHECK_EQ(g_b.cycles, 40);
    CHECK(!g_b.i && !g_b.waiting);
    setup();
    board_write_sfr(&g_b, ICR2, 0x10);
    board_write_sfr(&g_b, T3_PRE, 0);
    board_write_sfr(&g_b, T3_RELOAD, 9);
    CHECK(!board_wait_interrupt(&g_b, 41));
    CHECK_EQ(g_b.cycles, 47);
    CHECK(g_b.i);                          /* entered, never returned */
    CHECK(!g_b.handler_due && !g_b.waiting);
    CHECK_EQ(g_b.sfr.icr2 & 0x20, 0x20);   /* the handler did not run */
}

/* A line raised with I set wakes the CPU without the interrupt: the wait fails at once. */
static void test_wait_interrupt_i_set(void)
{
    setup();
    g_b.i = true;
    board_write_sfr(&g_b, ICR2, ICR2_T3);
    CHECK(!board_wait_interrupt(&g_b, 1000));
    CHECK_EQ(g_b.cycles, 0);
    CHECK(!g_b.waiting);
}

/* The lowest line first: lines 0 and 3 raised (ICR1 bits 2/3 and 4/5); a test image whose vectors are unmapped
   addresses shows which vector was read: line 0's ($FFFC). */
static void test_lowest_line_first(void)
{
    static uint8_t img[0x10000];
    setup();
    img[0xFFFC] = 0x34;
    img[0xFFFD] = 0x12;
    img[0xFFF6] = 0x78;
    img[0xFFF7] = 0x56;
    g_b.rom = img;
    board_write_sfr(&g_b, ICR1, 0x3C);
    CHECK(!board_wait_interrupt(&g_b, 1000));
    CHECK_EQ(g_b.cycles, BOARD_IRQ_CYCLES);
    CHECK_EQ(g_b.fault, BOARD_FAULT_VECTOR);
    CHECK_EQ(g_b.fault_addr, 0x1234);
}

/* Each timer loop: BBC3 ICR2 (timer 2) and BBC5 ICR2 (timer 3), with I set, 7 cycles a pass and 5 for the exit; I
   restored. Timer 2 at prescaler 0 and reload 4 expires after 20 cycles: three passes, then the exit. */
static void test_wait_timer(void)
{
    setup();
    board_write_sfr(&g_b, T2_PRE, 0);
    board_write_sfr(&g_b, T2_RELOAD, 4);
    board_write_sfr(&g_b, ICR2, ICR2_T3);   /* timer 3's line raised: not taken under the loop's SEI */
    CHECK(board_wait_timer(&g_b, 3, 1000));
    CHECK_EQ(g_b.cycles, 3 * BOARD_BBC_PASS + BOARD_BBC_EXIT);
    CHECK(!g_b.i);
    CHECK_EQ(g_b.sfr.icr2 & 0x08, 0x08);
    setup();
    board_write_sfr(&g_b, T3_PRE, 0);
    board_write_sfr(&g_b, T3_RELOAD, 1);    /* 8 cycles: two passes */
    CHECK(board_wait_timer(&g_b, 5, 1000));
    CHECK_EQ(g_b.cycles, 2 * BOARD_BBC_PASS + BOARD_BBC_EXIT);
    setup();
    CHECK(!board_wait_timer(&g_b, 5, 20));  /* no timer running: the bound ends it after three passes */
    CHECK_EQ(g_b.cycles, 3 * BOARD_BBC_PASS);
    CHECK(!g_b.i);
}

/* $C500 with an interrupt in the middle: the entry (7), the handler (0), BBC1 UART_STATUS not taken (the transmitter
   is always ready, 5), the RTS (6). */
static void test_wait_uart_tx(void)
{
    setup();
    board_write_sfr(&g_b, ICR2, ICR2_T3);
    CHECK(board_wait_uart_tx(&g_b, 1000));
    CHECK_EQ(g_b.cycles, BOARD_IRQ_CYCLES + BOARD_BBC_EXIT + BOARD_RTS_CYCLES);
    CHECK(!g_b.i);
    CHECK_EQ(g_b.sfr.icr2 & 0x20, 0);
    setup();
    CHECK(board_wait_uart_tx(&g_b, 1000));
    CHECK_EQ(g_b.cycles, BOARD_BBC_EXIT + BOARD_RTS_CYCLES);
    setup();
    CHECK(!board_wait_uart_tx(&g_b, 5));    /* the bound passed before the RTS */
    CHECK_EQ(g_b.cycles, BOARD_BBC_EXIT);
}

static unsigned g_idle_calls;
static board_idle_action g_idle_answer;
static board_idle_action idle_fn(board *b, void *user)
{
    (void)b;
    (void)user;
    g_idle_calls++;
    return g_idle_answer;
}

/* Delivery: at the idle point only, one byte while the UART is empty, at the first idle's busy level. */
static void test_delivery(void)
{
    setup();
    CHECK(board_feed_serial(&g_b, (const uint8_t *)"AB", 2));
    g_b.waiting = true;
    board_prepare(&g_b);                    /* waiting, but not at the idle point */
    CHECK(!g_b.sfr.uart_rx_full);
    CHECK(!g_b.busy_level_known);
    g_b.idle_point = true;
    board_prepare(&g_b);
    CHECK(g_b.sfr.uart_rx_full);
    CHECK_EQ(g_b.sfr.uart_rx, 'A');
    CHECK(g_b.busy_level_known);
    CHECK_EQ(g_b.busy_idle_level, 0);
    board_prepare(&g_b);                    /* the UART still full */
    CHECK_EQ(g_b.q_head, 1);
    CHECK_EQ(board_read_sfr(&g_b, UART_RX), 'A');
    g_b.sfr.port_in[0] |= BOARD_P0_BUSY;    /* busy: not the first idle's level */
    board_prepare(&g_b);
    CHECK(!g_b.sfr.uart_rx_full);
    g_b.sfr.port_in[0] &= (uint8_t)~BOARD_P0_BUSY;
    board_prepare(&g_b);
    CHECK_EQ(g_b.sfr.uart_rx, 'B');
    CHECK_EQ(g_b.q_head, g_b.q_tail);
    uint8_t big[BOARD_QUEUE_SIZE];
    memset(big, 'x', sizeof big);
    CHECK(board_feed_serial(&g_b, big, BOARD_QUEUE_SIZE - 1));   /* the ring holds one byte less than its size */
    CHECK(!board_feed_serial(&g_b, big, 1));
}

/* The idle test's five conditions: at the idle point, no queued byte, the UART empty, timer 3 off, no line. */
static void test_idle(void)
{
    setup();
    g_b.waiting = g_b.idle_point = true;
    board_prepare(&g_b);
    CHECK(board_idle(&g_b));
    g_b.idle_point = false;
    CHECK(!board_idle(&g_b));
    g_b.idle_point = true;
    g_b.q_tail = 1;
    CHECK(!board_idle(&g_b));
    g_b.q_tail = 0;
    g_b.sfr.uart_rx_full = true;
    CHECK(!board_idle(&g_b));
    g_b.sfr.uart_rx_full = false;
    board_write_sfr(&g_b, ICR2, 0x10);
    CHECK(!board_idle(&g_b));
    board_write_sfr(&g_b, ICR2, 0x00);
    board_write_sfr(&g_b, ICR1, 0x0C);
    board_prepare(&g_b);
    CHECK(!board_idle(&g_b));
    board_write_sfr(&g_b, ICR1, 0x00);
    board_prepare(&g_b);
    CHECK(board_idle(&g_b));
}

/* The input wait: the idle handler at every idle (a feed goes on and the byte is delivered; with the UART receive
   interrupt off it is never read, so the next idle never comes and the bound ends the wait); a stop leaves the board
   waiting at the idle point; an interrupt handled ends it. */
static void test_wait_input(void)
{
    setup();
    g_idle_calls = 0;
    g_idle_answer = BOARD_STOP;
    CHECK_EQ(board_wait_input(&g_b, 1000, idle_fn, NULL), BOARD_INPUT_STOPPED);
    CHECK_EQ(g_idle_calls, 1);
    CHECK_EQ(g_b.cycles, 0);
    CHECK(g_b.waiting && g_b.idle_point);
    setup();
    CHECK_EQ(board_wait_input(&g_b, 1000, NULL, NULL), BOARD_INPUT_STOPPED);   /* no handler: a stop */
    setup();
    g_idle_calls = 0;
    g_idle_answer = BOARD_FEED;
    CHECK(board_feed_serial(&g_b, (const uint8_t *)"A", 1));   /* a byte to deliver: no idle */
    CHECK_EQ(board_wait_input(&g_b, 40, idle_fn, NULL), BOARD_INPUT_FAILED);
    CHECK_EQ(g_idle_calls, 0);
    CHECK(g_b.sfr.uart_rx_full);
    CHECK_EQ(g_b.cycles, 40);
    CHECK(!g_b.waiting && !g_b.idle_point);
    setup();
    board_write_sfr(&g_b, ICR2, 0x10);
    board_write_sfr(&g_b, T3_PRE, 0);
    board_write_sfr(&g_b, T3_RELOAD, 9);
    CHECK_EQ(board_wait_input(&g_b, 1000, idle_fn, NULL), BOARD_INPUT_HANDLED);
    CHECK_EQ(g_b.cycles, 47);
    CHECK(!g_b.waiting && !g_b.idle_point && !g_b.i);
}

/* The DSP port: bytes grouped into raw frames of 20; a rising edge of P2.0 drops the partial frame. */
static void test_dsp(void)
{
    setup();
    board_set_raw_output(&g_b, on_raw, NULL);
    g_raws = 0;
    for (int k = 0; k < 27; k++)
        board_dsp_write(&g_b, (uint8_t)k);
    CHECK_EQ(g_b.port_writes, 27);
    CHECK_EQ(g_b.raw_frames, 1);
    CHECK_EQ(g_raws, 1);
    CHECK_EQ(g_frame[19], 19);
    CHECK_EQ(g_b.raw_pos, 7);
    board_write_sfr(&g_b, P2_DIR, 0x01);    /* P2.0 an output, latch 0: low */
    CHECK_EQ(g_b.dsp_resets, 0);
    board_write_sfr(&g_b, P2, 0x01);        /* rising edge */
    CHECK_EQ(g_b.dsp_resets, 1);
    CHECK_EQ(g_b.raw_discarded, 7);
    CHECK_EQ(g_b.raw_pos, 0);
    board_write_sfr(&g_b, P2, 0x01);        /* still high: no edge */
    CHECK_EQ(g_b.dsp_resets, 1);
}

/* The frame event: the 20 bytes at the producer pointer. */
static void test_capture(void)
{
    setup();
    board_set_outputs(&g_b, on_frame, NULL);
    g_frames = 0;
    g_b.ram[BOARD_PRODUCER_PTR] = 0x34;
    g_b.ram[BOARD_PRODUCER_PTR + 1] = 0x12;
    for (int k = 0; k < BOARD_FRAME_BYTES; k++)
        g_b.ram[0x1234 + k] = (uint8_t)(0xA0 + k);
    board_capture_frame(&g_b);
    CHECK_EQ(g_frames, 1);
    CHECK_EQ(g_b.frames_produced, 1);
    CHECK_EQ(g_frame[0], 0xA0);
    CHECK_EQ(g_frame[19], 0xB3);
}

/* The timer scale: ticks = cycles * num / den, the remainder carried. At 1/3 two WIT steps (8 cycles) tick 1 + 1. */
static void test_scale(void)
{
    setup();
    g_b.cycle_den = 3;
    board_write_sfr(&g_b, T2_PRE, 0);
    board_write_sfr(&g_b, T2_RELOAD, 99);   /* 400 cycles */
    CHECK(!board_wait_interrupt(&g_b, 8));
    CHECK_EQ(g_b.cycles, 8);
    CHECK_EQ(g_b.cycle_rem, 2);
    CHECK_EQ(g_b.sfr.timer_count[1], 400 - 2);
}

/* The unmodelled log: once per address and direction, a read of write-only UART_TX included; then the dropped
   count. */
static void test_unmodelled(void)
{
    setup();
    board_read_sfr(&g_b, 0xDA);
    board_read_sfr(&g_b, 0xDA);
    board_write_sfr(&g_b, 0xDA, 1);
    board_read_sfr(&g_b, M50734_UART_TX);
    board_read_sfr(&g_b, M50734_UART_STATUS);   /* modelled */
    CHECK_EQ(g_b.unmodelled_count, 3);
    CHECK(g_b.unmodelled[0].addr == 0xDA && !g_b.unmodelled[0].write);
    CHECK(g_b.unmodelled[1].addr == 0xDA && g_b.unmodelled[1].write);
    CHECK(g_b.unmodelled[2].addr == M50734_UART_TX && !g_b.unmodelled[2].write);
    g_b.unmodelled_count = BOARD_MAX_UNMODELLED;
    board_read_sfr(&g_b, 0xDB);
    CHECK_EQ(g_b.unmodelled_dropped, 1);
}

/* The DME strobe (P0_FUNC bit 5 with P0.5 an output), and a wait under another bank (code from a deselected bank). */
static void test_faults(void)
{
    setup();
    board_write_sfr(&g_b, P0_FUNC, 0x20);
    CHECK_EQ(g_b.fault, BOARD_FAULT_NONE);
    board_write_sfr(&g_b, P0_DIR, 0x20);
    CHECK_EQ(g_b.fault, BOARD_FAULT_DME);
    CHECK(!board_wait_interrupt(&g_b, 1000));   /* a step after the fault: the wait fails */
    CHECK_EQ(g_b.cycles, BOARD_WIT_CYCLES);
    setup();
    board_write_sfr(&g_b, P2_DIR, BOARD_P2_BANK_MASK);
    board_write_sfr(&g_b, P2, 0x00);
    CHECK(!board_main_bank(&g_b));
    CHECK(!board_wait_timer(&g_b, 3, 1000));
    CHECK_EQ(g_b.fault, BOARD_FAULT_ESCAPED);
    CHECK_EQ(g_b.cycles, 0);
}

/* The dispatch table: every vector of the converted image and every entry has its C; another address is an
   unmapped vector. With another bank selected a vector reads $FFFF. */
static void test_dispatch(void)
{
    setup();
    CHECK_EQ(board_vector(&g_b, BOARD_VEC_RESET), ROM_BOOT_RESET);
    CHECK_EQ(board_vector(&g_b, 0xFFFC - 2 * 1), ROM_ISR_INT1);
    CHECK_EQ(board_vector(&g_b, 0xFFFC - 2 * 2), ROM_ISR_TIMER_DSP);
    CHECK_EQ(board_vector(&g_b, 0xFFFC - 2 * 4), ROM_ISR_INT4_HOST_LINE);
    for (uint16_t v = BOARD_VEC_FIRST; v != 0; v = (uint16_t)(v + 2))
        CHECK(board_has_handler(board_vector(&g_b, v)));
    CHECK(board_has_handler(BANK_ENTRY_COLD) && board_has_handler(BANK_ENTRY_WARM));
    CHECK(!board_has_handler(ROM_CMD_RUN_NEXT));     /* the NUL bug's fallback has no C entry */
    CHECK(board_call(&g_b, ROM_ISR_UNUSED, 0, 0));   /* an RTI */
    CHECK_EQ(g_b.fault, BOARD_FAULT_NONE);
    CHECK(!board_call(&g_b, 0x1234, 0, 0));
    CHECK_EQ(g_b.fault, BOARD_FAULT_VECTOR);
    CHECK_EQ(g_b.fault_addr, 0x1234);
    setup();
    g_b.fw = NULL;
    CHECK(!board_call(&g_b, ROM_ISR_UNUSED, 0, 0));
    setup();
    board_write_sfr(&g_b, P2_DIR, BOARD_P2_BANK_MASK);
    board_write_sfr(&g_b, P2, 0x00);
    CHECK_EQ(board_vector(&g_b, BOARD_VEC_RESET), BOARD_EMPTY_VECTOR);
    g_b.rom = NULL;
    CHECK_EQ(board_peek(&g_b, 0x9000), 0xFF);
}

void suite_board(void)
{
    test_constants();
    test_cycles_match_cpu();
    test_init();
    test_wait_interrupt();
    test_wait_interrupt_bound();
    test_wait_interrupt_i_set();
    test_lowest_line_first();
    test_wait_timer();
    test_wait_uart_tx();
    test_delivery();
    test_idle();
    test_wait_input();
    test_dsp();
    test_capture();
    test_scale();
    test_unmodelled();
    test_faults();
    test_dispatch();
}
```

**`tests/suites.h`**: replace

```c
SUITE(suite_fw_boot)
```

with

```c
SUITE(suite_fw_boot)
SUITE(suite_board)
```

**`tests/test_firmware_data.c`**: replace

```c
                               { 0xD44A, 0xF77B }, { 0xFFD0, 0xFFD4 }, { 0xFFE0, 0xFFE8 }, { 0xFFEA, 0xFFEE } };
```

with

```c
                               { 0xD44A, 0xF77B }, { 0xFFD0, 0xFFD4 }, { 0xFFE0, 0xFFE8 }, { 0xFFEA, 0xFFEE },
                               { 0xFFF4, 0x10000 } };
```

**`tests/test_firmware_data.c`**: replace

```c
    CHECK_EQ(fw_boot_prosody_defaults[23], 0);                             /* X, which nothing reads */
```

with

```c
    CHECK_EQ(fw_boot_prosody_defaults[23], 0);                             /* X, which nothing reads */
    /* The vector table (M5a spec 3.4) */
    CHECK_EQ(fw_vec_int3_step, fw_vec_int0);    /* lines 3 and 0 share isr_unused, an RTI */
    CHECK_EQ(fw_vec_reset, 0xA61C);             /* boot_reset */
    CHECK(fw_vec_int4_timerx != fw_vec_int1_serial && fw_vec_int1_serial != fw_vec_int2_timers);
```

**`tests/test_firmware_data.c`**: replace

```c
    CHECK_EQ(compared, 49092);   /* M4.0's 35,214, M4.1's phonetic texts (97), M4.2's command data (567),
                                    M4.3a's prosody data (2,319), M4.4a's playback data (9,495), M4.4b's
                                    par_slot_map (10), M4.5's frame builder data (794), M4.6a's ser_hex_chars
                                    (16), M4.6c1's bank code (532: $ADFE-$AFFD and 20 header bytes) and M4.6c2's
                                    boot data (48: the self-test strings, 22, and boot_prosody_defaults, 26) */
```

with

```c
    CHECK_EQ(compared, 49104);   /* M4.0's 35,214, M4.1's phonetic texts (97), M4.2's command data (567),
                                    M4.3a's prosody data (2,319), M4.4a's playback data (9,495), M4.4b's
                                    par_slot_map (10), M4.5's frame builder data (794), M4.6a's ser_hex_chars
                                    (16), M4.6c1's bank code (532: $ADFE-$AFFD and 20 header bytes), M4.6c2's
                                    boot data (48: the self-test strings, 22, and boot_prosody_defaults, 26) and
                                    M5a's vector table (12) */
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test board"`
Expected: the build stops with compile errors (the proof run, first lines):

```
tests\test_board.c(8): fatal error C1083: Cannot open include file: 'board/board.h': No such file or directory
tests\test_firmware_data.c(133): error C2065: 'fw_vec_int3_step': undeclared identifier
tests\test_firmware_data.c(133): error C2065: 'fw_vec_int0': undeclared identifier
tests\test_firmware_data.c(134): error C2065: 'fw_vec_reset': undeclared identifier
```

- [ ] **Step 3: Implement**

The move, the board, the vector table and the build check.

```bash
mkdir -p src/board
git mv src/machine/m50734.c src/board/m50734.c
git mv src/machine/m50734.h src/board/m50734.h
```

**`src/board/m50734.c`**: replace

```c
#include "machine/m50734.h"
```

with

```c
#include "board/m50734.h"
```

**`src/machine/machine.h`**: replace

```c
#include "machine/m50734.h"
```

with

```c
#include "board/m50734.h"
```

**`tests/test_m50734.c`**: replace

```c
#include "machine/m50734.h"
```

with

```c
#include "board/m50734.h"
```

**Create `src/board/board.h`:**

```c
#ifndef BOARD_BOARD_H
#define BOARD_BOARD_H
/* The CPU-less board (M5a spec 3.2-3.4): what the machine (src/machine/machine.c) holds outside the CPU, with the
   machine's rules, so that the native host can give the C firmware everything the emulator gives it and the two can be
   compared field by field (diff_native). Time passes only in the waits, which charge exactly the cycles the all-C
   hybrid's emulated CPU spends in them (cpu740.c): a WIT step 4, an interrupt entry 7, the loops' instructions at their
   cycles. Each step mirrors machine_step: prepare (serial delivery at the idle point, then the IRQ lines), act (a WIT
   step, an interrupt entry, the handler of a taken interrupt in 0 cycles, or the instruction the wait mirrors), then
   the tick of the peripheral timers through the timer scale. Nothing here includes a machine/ or cpu740/ header. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "board/m50734.h"

#define BOARD_RAM_SIZE        0x4000   /* $0000-$3FFF; $00DA-$00FF are SFRs instead (MACHINE_RAM_SIZE) */
#define BOARD_QUEUE_SIZE      4096     /* the serial input ring (MACHINE_QUEUE_SIZE) */
#define BOARD_MAX_UNMODELLED  64       /* MACHINE_MAX_UNMODELLED */
#define BOARD_FRAME_BYTES     20       /* APOLLO_FRAME_BYTES */
#define BOARD_PRODUCER_PTR    0x2C     /* APOLLO_PRODUCER_PTR: the zero-page pointer to the frame the main loop filled */
#define BOARD_P2_BANK_MASK    0x7C     /* P2 bits 2-6: the selected ROM bank ... */
#define BOARD_P2_MAIN_BANK    0x70     /* ... the main ROM's */
#define BOARD_P2_DSP_RESET    0x01     /* P2.0: the DSP reset line (a rising edge restarts the DSP at word 0) */
#define BOARD_P0_BUSY         0x20     /* P0.5: the busy line to the host */
#define BOARD_ICR2_T3_ENABLE  0x10     /* ICR2 bit 4: timer 3's interrupt (the DSP tick) */
#define BOARD_ICR2            0xFE     /* the SFR the timer loops test */
#define BOARD_UART_STATUS     0xE7     /* the SFR the transmit loop tests */

/* The cycles of cpu740.c the waits charge (its k_mode_cycles and per-instruction adjustments) */
#define BOARD_WIT_CYCLES      4        /* a waiting CPU with no line raised */
#define BOARD_IRQ_CYCLES      7        /* an interrupt entry (take_irq) */
#define BOARD_BBC_EXIT        5        /* BBC n,zp,rel (AM_BIT_ZP_REL) with the bit set: not taken */
#define BOARD_BBC_PASS        7        /* ... with the bit clear: taken, 2 more */
#define BOARD_RTS_CYCLES      6

/* The ROM addresses the board stands for */
#define BOARD_VEC_FIRST       0xFFF4   /* the vector table: line n's at $FFFC - 2n ... */
#define BOARD_VEC_RESET       0xFFFE   /* ... and the reset vector */
#define BOARD_EMPTY_VECTOR    0xFFFF   /* a vector read with another bank selected (an empty socket reads $FF) */

struct board;
struct fw;   /* the firmware context the handlers run on (firmware/fw.h) */

typedef void (*board_frame_fn)(void *user, const uint8_t frame[BOARD_FRAME_BYTES]);
typedef struct { uint16_t addr; bool write; } board_access;

/* How the board stopped (machine_fault's board-side counterparts; M5a spec 3.6): the DME strobe (MFAULT_DME); code
   run from a deselected bank (MFAULT_ESCAPED_PC: a wait, a handler or an entry while P2 selects another bank than the
   main ROM); a handler address with no C (an unmapped vector, fault_addr = the address); input longer than the serial
   queue (MFAULT_INPUT_OVERFLOW); the firmware C (MFAULT_FIRMWARE: a read outside the converted data, a failed ROM call,
   fault_addr = where). */
typedef enum { BOARD_FAULT_NONE, BOARD_FAULT_DME, BOARD_FAULT_ESCAPED, BOARD_FAULT_VECTOR, BOARD_FAULT_INPUT_OVERFLOW,
               BOARD_FAULT_FIRMWARE } board_fault;

/* The idle handler of the input wait (machine_idle_fn): it queued more input, or the run ends there. */
typedef enum { BOARD_STOP = 0, BOARD_FEED } board_idle_action;
typedef board_idle_action (*board_idle_fn)(struct board *b, void *user);
/* How board_wait_input ended (machine_input_result). */
typedef enum { BOARD_INPUT_FAILED = 0, BOARD_INPUT_HANDLED, BOARD_INPUT_STOPPED } board_input_result;

typedef struct board {
    uint8_t ram[BOARD_RAM_SIZE];           /* the firmware's RAM (f->ram); the SFR bytes $DA-$FF are not data */
    m50734 sfr;
    bool i;                                /* the CPU's I flag */
    bool waiting;                          /* the CPU at a WIT ... */
    bool idle_point;                       /* ... the idle WIT $C602, waiting at $C603: serial bytes are delivered */
    bool handler_due;                      /* an interrupt was taken: its handler runs at the next step ... */
    bool handler_i;                        /* ... and its RTI restores this I flag (the P the entry pushed) */
    uint16_t handler;                      /* the handler's address, read from the vector */
    uint32_t irq_lines;                    /* the lines as the last prepare saw them (bit n = line n) */
    uint64_t cycles;                       /* the CPU cycles spent (cpu.cycles) */
    uint32_t cycle_num, cycle_den;         /* the peripheral timer scale (machine_config: TEST-ONLY), never 0 */
    uint64_t cycle_rem;                    /* remainder of the scaled ticks, < cycle_den */
    board_fault fault;
    uint16_t fault_addr;                   /* BOARD_FAULT_VECTOR, BOARD_FAULT_FIRMWARE: the address */
    uint8_t queue[BOARD_QUEUE_SIZE];       /* serial input ring buffer */
    size_t q_head, q_tail;
    bool busy_level_known;
    uint8_t busy_idle_level;               /* P0.5 level at the first idle = "not busy" */
    board_frame_fn on_frame;               /* the frame event (board_capture_frame) */
    void *user;
    board_frame_fn on_raw;                 /* raw DSP-port frames */
    void *raw_user;
    uint8_t raw_buf[BOARD_FRAME_BYTES];
    unsigned raw_pos;                      /* bytes of the current raw frame received so far */
    bool dsp_reset_level;                  /* last P2.0 pin level */
    uint64_t raw_frames;                   /* complete raw frames delivered */
    uint64_t dsp_resets;                   /* P2.0 rising edges */
    uint64_t raw_discarded;                /* partial-frame bytes dropped at a DSP reset */
    uint64_t frames_produced;
    uint64_t port_writes;                  /* bytes written to the DSP port */
    board_access unmodelled[BOARD_MAX_UNMODELLED];
    unsigned unmodelled_count;
    unsigned unmodelled_dropped;           /* accesses not logged because the log was full (as the machine counts) */
    const uint8_t *rom;                    /* the converted image by CPU address (fw_image.rom): the vectors; NULL: none */
    struct fw *fw;                         /* the firmware a handler runs on; NULL: a handler is an unmapped vector */
} board;

/* machine_init without the CPU and the ROM: everything zeroed, m50734_reset with the pins, the P2.0 level, I set,
   0 cycles; the timer scale num/den (0 means 1). rom and fw stay NULL. */
void board_init(board *b, const uint8_t port_in[5], uint32_t cycle_num, uint32_t cycle_den);
void board_set_outputs(board *b, board_frame_fn on_frame, void *user);
void board_set_raw_output(board *b, board_frame_fn on_raw, void *user);
/* Queues serial input (machine_feed_serial): false when it does not fit (the bytes before stay queued). */
bool board_feed_serial(board *b, const uint8_t *bytes, size_t n);
/* An SFR ($DA-$FF) read or write as the machine's bus does it: an unmodelled register (or a read of write-only
   UART_TX) logged; a write that enables the data-memory strobe is BOARD_FAULT_DME; a rising edge of P2.0 counts a DSP
   reset and drops the partial raw frame. */
uint8_t board_read_sfr(board *b, uint8_t addr);
void board_write_sfr(board *b, uint8_t addr, uint8_t v);
/* A byte to the DSP port $8000: counted, grouped into raw frames of 20. */
void board_dsp_write(board *b, uint8_t v);
/* The frame event: the 20 bytes at the BOARD_PRODUCER_PTR pointer to the frame callback, frames_produced counted. */
void board_capture_frame(board *b);
/* A byte as the CPU reads it (machine_peek): RAM, an SFR 0, else the converted image under the current P2 ($FF for
   another bank, or without an image). */
uint8_t board_peek(const board *b, uint16_t addr);
/* P2 selects the main ROM. */
bool board_main_bank(const board *b);
/* prepare: serial delivery at the idle point (one byte, the UART empty, P0.5 at the first idle's level), then the
   IRQ lines from m50734_irq_lines. */
void board_prepare(board *b);
/* After prepare, the machine has nothing left to do: at the idle point, no queued byte, the UART empty, timer 3 off and
   no line raised. */
bool board_idle(const board *b);
/* The vector at addr (BOARD_VEC_FIRST-BOARD_VEC_RESET) as the CPU reads it: from the image while the main ROM is
   selected, else BOARD_EMPTY_VECTOR. */
uint16_t board_vector(const board *b, uint16_t addr);
/* The dispatch table (M5a spec 3.4): true when the C has a function at addr. */
bool board_has_handler(uint16_t addr);
/* Runs the C function of addr on b->fw with A = a and X = x (the window and the bank synced from the P2 pins first,
   as a hybrid thunk does): an interrupt handler, the reset or a bank entry. False, with BOARD_FAULT_VECTOR at addr,
   when the table has none (or b->fw is NULL). */
bool board_call(board *b, uint16_t addr, uint8_t a, uint8_t x);
/* The waits (machine.c), each bounded by end (the cycle count by which it must be over), false on a fault or when end
   comes first:
   - board_wait_interrupt (machine_wait_interrupt): a WIT until one interrupt has been handled; a line that wakes the
     CPU with I set fails it; no byte is delivered.
   - board_wait_input (machine_wait_input): the same at the idle point, where bytes are delivered; whenever
     board_idle holds, idle (NULL: none, a stop) feeds (the wait goes on) or stops (BOARD_INPUT_STOPPED, the board
     left waiting at the idle point).
   - board_wait_timer (machine_run_loop of $B372 / $B37F): BBC bit,ICR2 to itself with I set, pass by pass, until
     the bit reads set; I restored.
   - board_wait_uart_tx (machine_rom_call of $C500): BBC1 UART_STATUS to itself, then its RTS, interrupts live. */
bool board_wait_interrupt(board *b, uint64_t end);
board_input_result board_wait_input(board *b, uint64_t end, board_idle_fn idle, void *user);
bool board_wait_timer(board *b, unsigned bit, uint64_t end);
bool board_wait_uart_tx(board *b, uint64_t end);

#endif
```

**Create `src/board/board.c`:**

```c
#include <string.h>
#include "board/board.h"
#include "firmware/fw.h"
#include "firmware/boot.h"
#include "firmware/boot_addr.h"
#include "firmware/bank_addr.h"
#include "firmware/isr_addr.h"
#include "firmware/isr_dsp.h"
#include "firmware/isr_serial.h"
#include "firmware/ser_addr.h"

#define P2_LOWER_HALF 0x80   /* P2 bit 7: $4000-$7FFF read the lower half of the image */

void board_init(board *b, const uint8_t port_in[5], uint32_t cycle_num, uint32_t cycle_den)
{
    memset(b, 0, sizeof *b);
    b->cycle_num = cycle_num ? cycle_num : 1;
    b->cycle_den = cycle_den ? cycle_den : 1;
    m50734_reset(&b->sfr, port_in);
    b->dsp_reset_level = (m50734_port_pins(&b->sfr, 2) & BOARD_P2_DSP_RESET) != 0;
    b->i = true;   /* cpu740_reset: P = I | B */
}

void board_set_outputs(board *b, board_frame_fn on_frame, void *user)
{
    b->on_frame = on_frame;
    b->user = user;
}

void board_set_raw_output(board *b, board_frame_fn on_raw, void *user)
{
    b->on_raw = on_raw;
    b->raw_user = user;
}

bool board_feed_serial(board *b, const uint8_t *bytes, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        size_t next = (b->q_tail + 1) % BOARD_QUEUE_SIZE;
        if (next == b->q_head)
            return false;
        b->queue[b->q_tail] = bytes[i];
        b->q_tail = next;
    }
    return true;
}

bool board_main_bank(const board *b)
{
    return (m50734_port_pins(&b->sfr, 2) & BOARD_P2_BANK_MASK) == BOARD_P2_MAIN_BANK;
}

static void note_unmodelled(board *b, uint16_t addr, bool write)
{
    for (unsigned i = 0; i < b->unmodelled_count; i++)
        if (b->unmodelled[i].addr == addr && b->unmodelled[i].write == write)
            return;
    if (b->unmodelled_count < BOARD_MAX_UNMODELLED) {
        b->unmodelled[b->unmodelled_count].addr = addr;
        b->unmodelled[b->unmodelled_count].write = write;
        b->unmodelled_count++;
    } else {
        b->unmodelled_dropped++;
    }
}

uint8_t board_read_sfr(board *b, uint8_t addr)
{
    if (!m50734_is_modelled(addr) || m50734_is_write_only(addr))
        note_unmodelled(b, addr, false);
    return m50734_read(&b->sfr, addr);
}

void board_write_sfr(board *b, uint8_t addr, uint8_t v)
{
    if (!m50734_is_modelled(addr))
        note_unmodelled(b, addr, true);
    m50734_write(&b->sfr, addr, v);
    if ((b->sfr.p0_func & 0x20) && (b->sfr.port_dir[0] & 0x20))
        b->fault = BOARD_FAULT_DME;   /* data-memory strobe: never used by the Apollo ROM */
    bool reset = (m50734_port_pins(&b->sfr, 2) & BOARD_P2_DSP_RESET) != 0;
    if (reset && !b->dsp_reset_level) {   /* rising edge: the DSP restarts at word 0 */
        b->dsp_resets++;
        b->raw_discarded += b->raw_pos;
        b->raw_pos = 0;
    }
    b->dsp_reset_level = reset;
}

void board_dsp_write(board *b, uint8_t v)
{
    b->port_writes++;
    b->raw_buf[b->raw_pos++] = v;
    if (b->raw_pos == BOARD_FRAME_BYTES) {
        b->raw_pos = 0;
        b->raw_frames++;
        if (b->on_raw)
            b->on_raw(b->raw_user, b->raw_buf);
    }
}

uint8_t board_peek(const board *b, uint16_t addr)
{
    if (addr >= M50734_SFR_FIRST && addr <= 0xFF)
        return 0;
    if (addr < BOARD_RAM_SIZE)
        return b->ram[addr];
    if (!b->rom || (addr >= 0x8000 && !board_main_bank(b)))
        return 0xFF;
    if (addr < 0x8000 && (m50734_port_pins(&b->sfr, 2) & P2_LOWER_HALF))
        return 0xFF;   /* the lower half: not in the image the board holds (the C never points a frame there) */
    return b->rom[addr];
}

void board_capture_frame(board *b)
{
    uint8_t frame[BOARD_FRAME_BYTES];
    uint16_t buf = (uint16_t)(b->ram[BOARD_PRODUCER_PTR] | b->ram[BOARD_PRODUCER_PTR + 1] << 8);
    for (int i = 0; i < BOARD_FRAME_BYTES; i++)
        frame[i] = board_peek(b, (uint16_t)(buf + i));
    b->frames_produced++;
    if (b->on_frame)
        b->on_frame(b->user, frame);
}

static bool at_idle(const board *b) { return b->waiting && b->idle_point; }

/* maybe_deliver: one byte per idle WIT, after the previous byte was read, while P0.5 is not busy. */
static void maybe_deliver(board *b)
{
    if (!at_idle(b))
        return;
    uint8_t busy = (uint8_t)(m50734_port_pins(&b->sfr, 0) & BOARD_P0_BUSY);
    if (!b->busy_level_known) {   /* the "not busy" level is the one seen at the first idle */
        b->busy_idle_level = busy;
        b->busy_level_known = true;
    }
    if (b->sfr.uart_rx_full || b->q_head == b->q_tail || busy != b->busy_idle_level)
        return;
    m50734_uart_receive(&b->sfr, b->queue[b->q_head]);
    b->q_head = (b->q_head + 1) % BOARD_QUEUE_SIZE;
}

void board_prepare(board *b)
{
    maybe_deliver(b);
    b->irq_lines = m50734_irq_lines(&b->sfr);
}

bool board_idle(const board *b)
{
    return at_idle(b) && b->q_head == b->q_tail && !b->sfr.uart_rx_full && !(b->sfr.icr2 & BOARD_ICR2_T3_ENABLE) &&
           !b->irq_lines;
}

uint16_t board_vector(const board *b, uint16_t addr)
{
    if (!b->rom || !board_main_bank(b))
        return BOARD_EMPTY_VECTOR;
    return (uint16_t)(b->rom[addr] | b->rom[addr + 1] << 8);
}

/* The C of the reset vector, which takes no registers. */
static void reset_handler(fw *f, fw_regs *r)
{
    (void)r;
    boot_reset(f);
}

/* The dispatch table (M5a spec 3.4): each handler address of the vector table and each entry, and its C. */
static const struct { uint16_t addr; void (*fn)(fw *f, fw_regs *r); } k_dispatch[] = {
    { ROM_ISR_UNUSED, isr_unused },                  /* lines 0 and 3 */
    { ROM_ISR_INT1, isr_int1 },                      /* line 1 */
    { ROM_ISR_TIMER_DSP, isr_timer_dsp },            /* line 2 */
    { ROM_ISR_INT4_HOST_LINE, isr_int4_host_line },  /* line 4 */
    { ROM_BOOT_RESET, reset_handler },               /* the reset vector */
    { BANK_ENTRY_COLD, bank_entry_cold },            /* the bank entries (fw_enter) */
    { BANK_ENTRY_WARM, bank_entry_warm },
};

bool board_has_handler(uint16_t addr)
{
    for (size_t i = 0; i < sizeof k_dispatch / sizeof k_dispatch[0]; i++)
        if (k_dispatch[i].addr == addr)
            return true;
    return false;
}

bool board_call(board *b, uint16_t addr, uint8_t a, uint8_t x)
{
    for (size_t i = 0; b->fw && i < sizeof k_dispatch / sizeof k_dispatch[0]; i++) {
        if (k_dispatch[i].addr != addr)
            continue;
        uint8_t p2 = m50734_port_pins(&b->sfr, 2);   /* sync_p2: the window and the bank from the pins */
        b->fw->low_window = (p2 & P2_LOWER_HALF) != 0;
        b->fw->bank = p2 & BOARD_P2_BANK_MASK;
        fw_regs r = { a, x, 0, 0 };
        k_dispatch[i].fn(b->fw, &r);
        return true;
    }
    b->fault = BOARD_FAULT_VECTOR;
    b->fault_addr = addr;
    return false;
}

/* The peripheral timers advance by the step's cycles through the scale (exec's tail). */
static void tick(board *b, int cycles)
{
    b->cycles += (uint64_t)cycles;
    uint64_t acc = b->cycle_rem + (uint64_t)cycles * b->cycle_num;
    b->cycle_rem = acc % b->cycle_den;
    m50734_tick(&b->sfr, (int)(acc / b->cycle_den));
}

/* The instruction a wait mirrors at its current point: its cycles, *done set when the wait's code is over. */
typedef int (*board_insn)(board *b, void *ctx, bool *done);

/* One step after prepare, as machine.c's exec: code outside the main ROM faults (pc_escaped: every point of a wait is
   at $8000 or above); the handler of a taken interrupt runs (the hook: 0 cycles, then its RTI restores I); else
   cpu740_step's start (a waiting CPU with no line spends a WIT step; any line ends the wait; a line with I clear is
   taken, the lowest first: I saved and set, the vector read) or the instruction, then the tick. */
static bool exec(board *b, board_insn insn, void *ctx, bool *done)
{
    if (!board_main_bank(b)) {
        b->fault = BOARD_FAULT_ESCAPED;
        return false;
    }
    if (b->handler_due) {
        b->handler_due = false;
        if (board_call(b, b->handler, 0, 0))
            b->i = b->handler_i;   /* its RTI */
        return b->fault == BOARD_FAULT_NONE;
    }
    int cycles;
    if (b->waiting && !b->irq_lines) {
        cycles = BOARD_WIT_CYCLES;
    } else {
        b->waiting = false;   /* any raised line ends WIT, even while I is set */
        if (b->irq_lines && !b->i) {
            int line = 0;
            while (!(b->irq_lines & (1u << line)))
                line++;
            b->handler_i = b->i;
            b->i = true;
            b->handler = board_vector(b, (uint16_t)(0xFFFC - 2 * line));
            b->handler_due = true;
            cycles = BOARD_IRQ_CYCLES;
        } else if (insn) {
            cycles = insn(b, ctx, done);
        } else {
            return false;   /* no instruction here: a wait for an interrupt never runs one */
        }
    }
    tick(b, cycles);
    return b->fault == BOARD_FAULT_NONE;
}

bool board_wait_interrupt(board *b, uint64_t end)
{
    b->waiting = true;   /* a WIT at the C's point: not the idle point, so no byte is delivered */
    b->idle_point = false;
    while (b->cycles < end) {
        board_prepare(b);
        if (b->waiting && b->irq_lines && b->i)
            break;   /* the CPU would wake and run on without taking the interrupt */
        if (!exec(b, NULL, NULL, NULL))
            break;
        if (!b->waiting && !b->handler_due)
            return true;   /* the handler's RTI is back */
    }
    b->waiting = false;
    b->handler_due = false;
    return false;
}

board_input_result board_wait_input(board *b, uint64_t end, board_idle_fn idle, void *user)
{
    b->waiting = true;   /* the WIT at $C602 executed: the CPU waits at the idle point */
    b->idle_point = true;
    while (b->cycles < end) {
        board_prepare(b);
        if (board_idle(b)) {
            if (!idle || idle(b, user) == BOARD_STOP)
                return BOARD_INPUT_STOPPED;   /* left at the idle point */
            continue;                         /* fed: the next prepare delivers a byte */
        }
        if (b->waiting && b->irq_lines && b->i)
            break;
        if (!exec(b, NULL, NULL, NULL))
            break;
        if (!b->waiting && !b->handler_due) {
            b->idle_point = false;
            return BOARD_INPUT_HANDLED;
        }
    }
    b->waiting = false;
    b->idle_point = false;
    b->handler_due = false;
    return BOARD_INPUT_FAILED;
}

/* BBC bit,ICR2 to itself ($B372 BBC3, $B37F BBC5): one read of ICR2; set, the loop is over. */
static int bbc_icr2(board *b, void *ctx, bool *done)
{
    unsigned bit = *(const unsigned *)ctx;
    *done = ((board_read_sfr(b, BOARD_ICR2) >> bit) & 1) != 0;
    return *done ? BOARD_BBC_EXIT : BOARD_BBC_PASS;
}

bool board_wait_timer(board *b, unsigned bit, uint64_t end)
{
    bool i = b->i, ok = true, done = false;
    b->i = true;   /* inside isr_timer_start's PHP / SEI */
    while (ok && !done) {
        ok = b->cycles < end;
        if (ok) {
            board_prepare(b);
            ok = exec(b, bbc_icr2, &bit, &done);
        }
    }
    b->i = i;
    return ok;
}

/* ser_tx_wait $C500: BBC1 UART_STATUS to itself (one read), then RTS ($C503). ctx: true once at the RTS. */
static int ser_tx_wait(board *b, void *ctx, bool *done)
{
    bool *at_rts = ctx;
    if (*at_rts) {
        *done = true;
        return BOARD_RTS_CYCLES;
    }
    *at_rts = ((board_read_sfr(b, BOARD_UART_STATUS) >> 1) & 1) != 0;
    return *at_rts ? BOARD_BBC_EXIT : BOARD_BBC_PASS;
}

bool board_wait_uart_tx(board *b, uint64_t end)
{
    bool at_rts = false, done = false;
    while (b->cycles < end) {
        board_prepare(b);
        if (!exec(b, ser_tx_wait, &at_rts, &done))
            break;
        if (done)
            return true;   /* the RTS */
    }
    b->handler_due = false;
    return false;
}
```

**`src/firmware/data/fw_data.h`**: replace

```c
#define BOOT_PROSODY_PARAMS       26
```

with

```c
#define BOOT_PROSODY_PARAMS       26

/* Pinned address of the vector table (ghidra740/annotations: boot.txt; M5a spec 3.4) */
#define FW_VECTORS                0xFFF4   /* vec_int4_timerx: the first of six handler addresses, to $FFFF */
```

**`src/firmware/data/fw_data.h`**: replace

```c
extern const uint8_t fw_boot_prosody_defaults[BOOT_PROSODY_PARAMS];
```

with

```c
extern const uint8_t fw_boot_prosody_defaults[BOOT_PROSODY_PARAMS];
extern const uint16_t fw_vec_int4_timerx, fw_vec_int3_step, fw_vec_int2_timers, fw_vec_int1_serial, fw_vec_int0,
    fw_vec_reset;
```

**`src/firmware/data/boot_data.c`**: replace

```c
#include "fw_data.h"
```

with

```c
#include "fw_data.h"
#include "../boot_addr.h"
#include "../isr_addr.h"
#include "../ser_addr.h"
```

**`src/firmware/data/boot_data.c`**: replace

```c
    TERM(0),     /* Z: pros_term_stress of a word's later symbols */
};
```

with

```c
    TERM(0),     /* Z: pros_term_stress of a word's later symbols */
};

/* The vector table ($FFF4-$FFFF, M5a spec 3.4): the handler the CPU enters for each interrupt line, line n's vector
   at $FFFC - 2n, and the reset's. The board dispatches each address to its C (src/board/board.c). */
const uint16_t fw_vec_int4_timerx = ROM_ISR_INT4_HOST_LINE;   /* $FFF4, line 4: Timer X / CNTR (INT4, the host line) */
const uint16_t fw_vec_int3_step = ROM_ISR_UNUSED;             /* $FFF6, line 3: the stepper counters */
const uint16_t fw_vec_int2_timers = ROM_ISR_TIMER_DSP;        /* $FFF8, line 2: timers 1-3 (timer 3, the DSP tick) */
const uint16_t fw_vec_int1_serial = ROM_ISR_INT1;             /* $FFFA, line 1: the _INT1 pin and the UART receive */
const uint16_t fw_vec_int0 = ROM_ISR_UNUSED;                  /* $FFFC, line 0: the _INT2 pin */
const uint16_t fw_vec_reset = ROM_BOOT_RESET;                 /* $FFFE: the reset */
```

**`src/firmware/data/fw_data.c`**: replace

```c
        for (uint32_t i = 0; i < BOOT_PROSODY_PARAMS; i++)
            put(&e, false, BOOT_PROSODY_DEFAULTS + i, fw_boot_prosody_defaults[i]);
    }
    return e.ok;
```

with

```c
        for (uint32_t i = 0; i < BOOT_PROSODY_PARAMS; i++)
            put(&e, false, BOOT_PROSODY_DEFAULTS + i, fw_boot_prosody_defaults[i]);
    }
    /* 14. The vector table (M5a spec 3.4): the six handler addresses, each low byte first, from FW_VECTORS to the top
       of the address space. */
    {
        const uint16_t vec[] = { fw_vec_int4_timerx, fw_vec_int3_step, fw_vec_int2_timers, fw_vec_int1_serial,
                                 fw_vec_int0, fw_vec_reset };
        uint32_t at = FW_VECTORS;
        for (size_t k = 0; k < sizeof vec / sizeof vec[0]; k++, at += 2)
            put_word(&e, at, vec[k]);
        check_end(&e, "the vector table", at, 0x10000);
    }
    return e.ok;
```

**`build.bat`** (its lines end in CRLF, `.gitattributes`; keep them so): replace

```bat
if defined FWSRC (
    cl /nologo /std:c11 /W4 /WX /wd4244 /Zs /D_CRT_SECURE_NO_WARNINGS !FWSRC! || exit /b 1
)
set LIBSRC=
for %%d in (src\cpu740 src\machine src\engine src\firmware src\firmware\data src\synth src\audio src\render) do (
```

with

```bat
if defined FWSRC (
    cl /nologo /std:c11 /W4 /WX /wd4244 /Zs /D_CRT_SECURE_NO_WARNINGS !FWSRC! || exit /b 1
)
rem M5a spec 3.1: src\board builds without the emulator too. Syntax-check it against an include root holding only the
rem headers it may use (src\board and src\firmware, copied to build\noemu), so an include of a machine\ or cpu740\
rem header cannot be found.
if exist build\noemu rmdir /s /q build\noemu
for %%d in (board firmware\data) do mkdir build\noemu\%%d
for %%d in (board firmware firmware\data) do copy /y src\%%d\*.h build\noemu\%%d >nul
cl /nologo /std:c11 /W4 /WX /wd4244 /Zs /D_CRT_SECURE_NO_WARNINGS /Ibuild\noemu src\board\*.c || exit /b 1
set LIBSRC=
for %%d in (src\cpu740 src\machine src\board src\engine src\firmware src\firmware\data src\synth src\audio src\render) do (
```

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test board"`, then `build/apollo2_tests.exe firmware_data` and `build/apollo2_tests.exe m50734`.
Expected: the build passes with no warnings (both syntax checks included), and

```
suite_board                  ok
1 suite(s), 0 failure(s), 0 skip(s)
  49104 converted bytes compared with the ROM, 0 differ
suite_firmware_data          ok
1 suite(s), 0 failure(s), 0 skip(s)
suite_m50734                 ok
1 suite(s), 0 failure(s), 0 skip(s)
```

- [ ] **Step 5: Run the full suite**

Run: `cmd //c ".\build.bat test"` (in the background; it takes about 16 minutes)
Expected:

- `41 suite(s), 0 failure(s), 0 skip(s)`, no warnings, both syntax checks passing (999 s in the proof run);
- `firmware_data`: `49104 converted bytes compared with the ROM, 0 differ`;
- every other line of the suites' output exactly as at 69c6038: `diff_main` `corpus: 47 texts, 94 idles, 25167 frame events compared` and `66 runs, 129 idles, 31533 frame events compared in all`; `golden_hybrid` `48 corpus files x 132 modes (all, then each routine alone)`; `suite_golden` `timer scale 1/4: 105 frames, 11789152 CPU cycles`, `timer scale 3/1: 105 frames, 1286784 CPU cycles`, `hybrid timer scale 1/4: 105 frames, 11678411 CPU cycles`, `hybrid timer scale 3/1: 105 frames, 973321 CPU cycles`; `suite_synth` `corpus peak plain 20522 (-4.1 dBFS)`, `corpus peak b11 16038 (-6.2 dBFS)`; `diff_lts`, `diff_play` and `diff_serial` line for line;
- every golden unchanged (`git status --short tests/golden` prints nothing).

- [ ] **Step 6: Commit**

```bash
git add src/board/board.h src/board/board.c src/board/m50734.c src/machine/machine.h tests/test_board.c tests/test_m50734.c tests/test_firmware_data.c tests/suites.h src/firmware/data/fw_data.h src/firmware/data/fw_data.c src/firmware/data/boot_data.c build.bat
git commit -m "The CPU-less board: m50734 moved to src/board, board.c (RAM, the I flag, the cycle clock with the timer scale, SFR access, the DSP port and frame capture, serial delivery and the idle test, the four waits at the hybrid's cycles, dispatch through the converted vector table), the vector table converted, suite_board, build.bat's check of src/board (M5a 3.1-3.4, 4.1)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files (the two `git mv` were staged when they were made).

---

### Task 2: The native host: the `fw_host` on a board, each wait with the hybrid's budgets and the phase deadline, `read_bank` $FF, `enter` recorded, the ROM calls failing; its tests in `suite_native` (M5a spec §3.5, §4.2, §4.7 item 2)

**Facts checked / what was verified while proving the plan** (`src/engine/thunks.c`: `host_wait`, `wait_ended`, `rom_call`, `phase_call`, `host_enter`, `host_reset_cpu`, `host_set_interrupts`, `host_set_low_window`, `host_read_bank`, `sync_p2`, `run_frame`, `bridge_deadline`; `src/firmware/fw.c`; `src/machine/machine.c`):
- **Every host call of an all-C hybrid run is under the phase deadline**: the run starts in `boot_reset`'s entry row (`run_frame`, `b->phase` set because the engine set `m->phase_deadline`), every other routine is C called from it, and a bank entry starts another outermost entry row; interrupt handlers run nested inside the C's waits. So the native host's waits always use the hybrid's `phase` branch: `FW_WAIT_UART_TX` at most `FW_ROM_CALL_BUDGET` and the cycles left before the deadline (`rom_call`), the others the cycles left (`UINT64_MAX` clamped); none once the deadline has passed. A wait that does not end is a stop at the deadline (`wait_ended`: the deadline reached and no machine fault), else a failure. Without a deadline (unit tests) every wait gets `FW_ROM_CALL_BUDGET` and its end is a failure, as the hybrid's non-phase branch.
- **The window and the bank after a wait** (`sync_p2`): read back from the P2 pins after every wait that started (`rom_call` returns before it when the deadline has passed), also after a stopped input wait.
- **The input wait's PHA and PLA** (`host_wait`, `FW_WAIT_INPUT`) touch only the stack page and SP: no board state.
- **`host_enter`** sets SP $FF, I, A, X and the PC; **`host_reset_cpu`** I, SP and T, D, C, V cleared; **`host_set_interrupts`** I. Of these the board has I alone; nothing in the C reads SP, A, X or the other flags of the machine (the thunks pass registers in `fw_regs`).
- **`host_read_bank`** is `machine_peek`, which reads $FF for $8000 and above while P2 selects another bank: the option sockets are empty. **`host_set_low_window`** is a read, then a write, of P2 through the bus.
- **The ROM calls**: in an all-C hybrid run the C calls the ROM only for the NUL bug's fallback (`fw_rom_call_unhooked` of `ROM_CMD_RUN_NEXT` $A893, `cmd_run.c`) and `cmd_play_event.c`'s unreachable call of an event vector; `fw_rom_call` faults at the address it called when the host's call fails.

**Files:**
- Create: `src/engine/native_host.h`, `src/engine/native_host.c`, `tests/test_native.c`
- Modify: `tests/suites.h`

**Interfaces:**
- Consumes: the board (Task 1); `fw_init`, `fw_host`, `fw_wait_kind`, `fw_wait_result`, `fw_call_result`, `FW_ROM_CALL_BUDGET`, `P2_BANK_BITS` (`firmware/fw.h`); `fw_image_get` (`firmware/data/fw_data.h`).
- Produces (Tasks 3–5 use them):

```c
/* src/engine/native_host.h */
typedef struct native_host {
    board *b; fw f; fw_host host;
    uint64_t deadline;                        /* the phase deadline; 0: none */
    board_idle_fn idle; void *idle_user;      /* the input wait's idle handler */
    bool entered; uint16_t entry; uint8_t entry_a, entry_x;   /* the last fw_enter */
} native_host;
bool native_host_init(native_host *h, board *b);   /* b->rom = the image, b->fw = &h->f */
/* tests/test_native.c: suite_native, the 42nd suite */
```

No name collides: `grep -rn "native_host\|suite_native" src tests` prints nothing before this task.

- [ ] **Step 1: Write the failing tests**

`tests/test_native.c` starts with the host's calls on a board; Task 3 adds the engine's runs.

**Create `tests/test_native.c`:**

```c
/* The native host (M5a spec 4.2), with no ROM: each host call on a board, read_bank's $FF, the failing ROM calls, the
   waits' bounds and ends. */
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "engine/native_host.h"
#include "firmware/cmd_addr.h"
#include "firmware/bank_addr.h"

#define T2_PRE      0xDE
#define T2_RELOAD   0xDF
#define ICR2        0xFE
#define P2          0xF0
#define P2_DIR      0xF1

static const uint8_t k_pins[5] = { 0x00, 0x00, 0x70, 0x00, 0x00 };
static board g_b;
static native_host g_h;

static void setup(void)
{
    board_init(&g_b, k_pins, 1, 1);
    CHECK(native_host_init(&g_h, &g_b));
    g_b.i = false;
}

/* The binding: the firmware's RAM is the board's, the board's vectors the converted image's, its handlers' firmware
   the host's. */
static void test_init(void)
{
    setup();
    CHECK(g_h.f.ram == g_b.ram);
    CHECK(g_h.f.host == &g_h.host);
    CHECK(g_b.fw == &g_h.f);
    CHECK(g_b.rom != NULL);
    CHECK_EQ(board_vector(&g_b, BOARD_VEC_RESET), 0xA61C);
    CHECK_EQ(g_h.deadline, 0);
    CHECK(!g_h.entered);
}

/* The SFRs, the window, the DSP port and the frame event through the firmware's own calls. */
static void test_ports(void)
{
    setup();
    fw_wr_sfr8(&g_h.f, P2_DIR, 0xFD);
    fw_wr_sfr8(&g_h.f, P2, 0x71);
    CHECK_EQ(m50734_port_pins(&g_b.sfr, 2), 0x71);
    CHECK_EQ(g_b.dsp_resets, 1);                 /* P2.0's rising edge */
    CHECK_EQ(fw_sfr8(&g_h.f, P2), 0x71);
    fw_set_low_window(&g_h.f, true);             /* SEB7 P2: a read, then a write */
    CHECK_EQ(m50734_port_pins(&g_b.sfr, 2), 0xF1);
    CHECK(g_h.f.low_window);
    fw_set_low_window(&g_h.f, false);
    CHECK_EQ(m50734_port_pins(&g_b.sfr, 2), 0x71);
    fw_dsp_write(&g_h.f, 0x12);
    CHECK_EQ(g_b.port_writes, 1);
    CHECK_EQ(g_b.raw_buf[0], 0x12);
    fw_frame_done(&g_h.f);
    CHECK_EQ(g_b.frames_produced, 1);
    CHECK(!g_h.f.fault);
}

/* Another bank's sockets are empty: $FF, no fault. A ROM call, hooked or not, fails: a fault at its address. */
static void test_bank_and_rom_calls(void)
{
    setup();
    g_h.f.bank = 0x6C;
    CHECK_EQ(fw_rom8(&g_h.f, 0xFFE0), 0xFF);
    CHECK_EQ(fw_rom8(&g_h.f, 0x8000), 0xFF);
    CHECK(!g_h.f.fault);
    g_h.f.bank = P2_MAIN_BANK;
    fw_regs r = { 1, 2, 3, 0 };
    CHECK(!fw_rom_call(&g_h.f, 0xC37E, &r, FW_ROM_CALL_BUDGET));
    CHECK(g_h.f.fault);
    CHECK_EQ(g_h.f.fault_addr, 0xC37E);
    setup();
    CHECK(!fw_rom_call_unhooked(&g_h.f, ROM_CMD_RUN_NEXT, &r, FW_ROM_CALL_BUDGET));
    CHECK_EQ(g_h.f.fault_addr, ROM_CMD_RUN_NEXT);
    CHECK_EQ(g_b.cycles, 0);
}

/* The CPU calls and the bank entry: the I flag; enter records the target, A and X, sets I, and the C is abandoned. */
static void test_cpu_calls(void)
{
    setup();
    fw_reset_cpu(&g_h.f);
    CHECK(g_b.i);
    fw_set_interrupts(&g_h.f, true, 0xA66E);
    CHECK(!g_b.i);
    fw_set_interrupts(&g_h.f, false, 0xA6E7);
    CHECK(g_b.i);
    g_b.i = false;
    fw_enter(&g_h.f, BANK_ENTRY_WARM, 0x31, 0xFF);
    CHECK(g_h.entered);
    CHECK_EQ(g_h.entry, BANK_ENTRY_WARM);
    CHECK_EQ(g_h.entry_a, 0x31);
    CHECK_EQ(g_h.entry_x, 0xFF);
    CHECK(g_b.i);
    CHECK(g_h.f.unwound);
    CHECK(!g_h.f.fault);
}

/* The waits through the host: each kind's cycles (as test_board's), the window and the bank read back after one;
   without a deadline FW_ROM_CALL_BUDGET bounds a wait and its end is a fault. */
static void test_waits(void)
{
    setup();
    fw_wr_sfr8(&g_h.f, T2_PRE, 0);
    fw_wr_sfr8(&g_h.f, T2_RELOAD, 4);
    g_h.f.bank = 0x6C;                           /* a stale view: the wait reads the pins back */
    g_h.f.low_window = true;
    fw_wait(&g_h.f, FW_WAIT_TIMER2, FW_WAIT_TIMER2_LOOP);
    CHECK(!g_h.f.fault);
    CHECK_EQ(g_b.cycles, 3 * BOARD_BBC_PASS + BOARD_BBC_EXIT);
    CHECK_EQ(g_h.f.bank, P2_MAIN_BANK);
    CHECK(!g_h.f.low_window);
    fw_wait(&g_h.f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);
    CHECK_EQ(g_b.cycles, 3 * BOARD_BBC_PASS + BOARD_BBC_EXIT + BOARD_BBC_EXIT + BOARD_RTS_CYCLES);
    CHECK(!g_h.f.fault);
    fw_wait(&g_h.f, FW_WAIT_INTERRUPT, 0xB465);  /* no line will rise: the budget ends it, a fault */
    CHECK(g_h.f.fault);
    CHECK_EQ(g_h.f.fault_addr, 0xB465);
    CHECK_EQ(g_b.cycles, 3 * BOARD_BBC_PASS + 2 * BOARD_BBC_EXIT + BOARD_RTS_CYCLES + FW_ROM_CALL_BUDGET);
    setup();
    fw_wait(&g_h.f, FW_WAIT_INPUT, FW_WAIT_INPUT_LOOP);   /* no idle handler: a stop at the first idle */
    CHECK(g_h.f.stopped);
    CHECK(!g_h.f.fault);
    CHECK(g_b.waiting && g_b.idle_point);
    setup();
    fw_wait(&g_h.f, (fw_wait_kind)99, 0x1234);
    CHECK_EQ(g_h.f.fault_addr, 0x1234);
}

/* The phase deadline: a wait that reaches it stops the run (f->stopped, not a fault); one called after it has passed
   stops at once, no cycle spent; the transmit wait is bounded by it too. */
static void test_deadline(void)
{
    setup();
    g_h.deadline = 100;
    fw_wait(&g_h.f, FW_WAIT_INTERRUPT, 0xB465);
    CHECK(g_h.f.stopped);
    CHECK(!g_h.f.fault);
    CHECK_EQ(g_b.cycles, 100);
    g_h.f.stopped = false;
    fw_wait(&g_h.f, FW_WAIT_TIMER2, FW_WAIT_TIMER2_LOOP);
    CHECK(g_h.f.stopped);
    CHECK_EQ(g_b.cycles, 100);
    setup();
    g_h.deadline = 3;
    fw_wait(&g_h.f, FW_WAIT_UART_TX, FW_WAIT_UART_TX_LOOP);
    CHECK(g_h.f.stopped);
    CHECK_EQ(g_b.cycles, BOARD_BBC_EXIT);
    setup();
    g_h.deadline = 1000;
    g_b.i = true;
    fw_wr_sfr8(&g_h.f, ICR2, 0x30);              /* a line with I set: the wait fails before the deadline */
    fw_wait(&g_h.f, FW_WAIT_INTERRUPT, 0xB465);
    CHECK(g_h.f.fault);
    CHECK(!g_h.f.stopped);
}

void suite_native(void)
{
    test_init();
    test_ports();
    test_bank_and_rom_calls();
    test_cpu_calls();
    test_waits();
    test_deadline();
}
```

**`tests/suites.h`**: replace

```c
SUITE(suite_board)
```

with

```c
SUITE(suite_board)
SUITE(suite_native)
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test suite_native"`
Expected: the build stops with compile errors (the proof run, first lines):

```
tests\test_native.c(6): fatal error C1083: Cannot open include file: 'engine/native_host.h': No such file or directory
```

- [ ] **Step 3: Implement**

**Create `src/engine/native_host.h`:**

```c
#ifndef ENGINE_NATIVE_HOST_H
#define ENGINE_NATIVE_HOST_H
/* The native host (M5a spec 3.5): the fw_host of the C firmware on a CPU-less board, with no ROM and no emulated CPU.
   Every call does what the hybrid host (src/engine/thunks.c) does on the machine, the cycles included: the SFRs, the
   DSP port and the frame event on the board; each wait on the board's clock, with the hybrid's budgets, the phase
   deadline and its end (a stop at the deadline, else a failure); read_bank $FF; enter records the bank entry; the CPU
   calls the I flag; a ROM call fails. Nothing here includes a machine/ or cpu740/ header. */
#include <stdbool.h>
#include <stdint.h>
#include "board/board.h"
#include "firmware/fw.h"

typedef struct native_host {
    board *b;
    fw f;                      /* the firmware, its RAM the board's */
    fw_host host;
    uint64_t deadline;         /* the engine's phase deadline (m->phase_deadline): a wait that reaches it stops the
                                  run; 0: none (each wait then gets FW_ROM_CALL_BUDGET and fails at its end) */
    board_idle_fn idle;        /* the input wait's idle handler (NULL: none, a stop) */
    void *idle_user;
    bool entered;              /* fw_enter left the C through a bank entry ... */
    uint16_t entry;            /* ... at this address, with A and X: */
    uint8_t entry_a, entry_x;
} native_host;

/* Binds h to b: fw_init on b's RAM with the converted image and h's host calls; b->rom = the image, b->fw = &h->f.
   False when the data image does not build. */
bool native_host_init(native_host *h, board *b);

#endif
```

**Create `src/engine/native_host.c`:**

```c
#include "engine/native_host.h"
#include "firmware/data/fw_data.h"

#define SFR_P2          0xF0
#define P2_LOWER_WINDOW 0x80

/* sync_p2: the firmware's window and bank read back from the board's pins after a wait (an interrupt handler may have
   written P2). */
static void sync_p2(native_host *h)
{
    uint8_t p2 = m50734_port_pins(&h->b->sfr, 2);
    h->f.low_window = (p2 & P2_LOWER_WINDOW) != 0;
    h->f.bank = p2 & P2_BANK_BITS;
}

/* The hybrid's rom_call, rom_call_unhooked: there is no ROM. The C faults at the address it called (the NUL fallback's
   $A893, cmd_play_event's unreachable call of an event vector). */
static fw_call_result host_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    (void)ctx;
    (void)addr;
    (void)r;
    (void)budget;
    return FW_CALL_FAILED;
}

/* SEB7/CLB7 $F0: a read-modify-write of P2 through the board, as the hybrid host's through the bus. */
static void host_set_low_window(void *ctx, bool on)
{
    board *b = ((native_host *)ctx)->b;
    uint8_t v = board_read_sfr(b, SFR_P2);
    board_write_sfr(b, SFR_P2, (uint8_t)(on ? v | P2_LOWER_WINDOW : v & ~P2_LOWER_WINDOW));
}

static uint8_t host_read_sfr(void *ctx, uint8_t addr) { return board_read_sfr(((native_host *)ctx)->b, addr); }

static void host_write_sfr(void *ctx, uint8_t addr, uint8_t value)
{
    board_write_sfr(((native_host *)ctx)->b, addr, value);
}

/* A wait that did not end (wait_ended): at the phase deadline the run stops there; otherwise a fault. */
static fw_wait_result wait_ended(native_host *h)
{
    if (!h->deadline || h->b->fault != BOARD_FAULT_NONE || h->b->cycles < h->deadline)
        return FW_WAIT_FAILED;
    h->b->waiting = false;
    return FW_WAIT_STOPPED;
}

/* The hybrid's host_wait, each kind on the board (board.h) within the same bound: FW_WAIT_UART_TX at most
   FW_ROM_CALL_BUDGET and the cycles left before the deadline (rom_call), the others the cycles left before the
   deadline (FW_ROM_CALL_BUDGET without one); none once it has passed. The window and the bank are read back after the
   wait, as the hybrid does (not when it never started). */
static fw_wait_result host_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    native_host *h = ctx;
    board *b = h->b;
    (void)at;
    uint64_t budget = kind == FW_WAIT_UART_TX || !h->deadline ? FW_ROM_CALL_BUDGET : UINT64_MAX;
    if (h->deadline) {
        if (b->cycles >= h->deadline)
            return wait_ended(h);
        if (budget > h->deadline - b->cycles)
            budget = h->deadline - b->cycles;
    }
    uint64_t end = b->cycles + budget;
    bool ok;
    switch (kind) {
    case FW_WAIT_UART_TX:
        ok = board_wait_uart_tx(b, end);
        break;
    case FW_WAIT_INTERRUPT:
        ok = board_wait_interrupt(b, end);
        break;
    case FW_WAIT_TIMER2:
        ok = board_wait_timer(b, 3, end);
        break;
    case FW_WAIT_TIMER3:
        ok = board_wait_timer(b, 5, end);
        break;
    case FW_WAIT_INPUT: {
        board_input_result r = board_wait_input(b, end, h->idle, h->idle_user);
        sync_p2(h);
        if (r == BOARD_INPUT_STOPPED)
            return FW_WAIT_STOPPED;   /* at the idle point */
        return r == BOARD_INPUT_HANDLED ? FW_WAIT_RETURNED : wait_ended(h);
    }
    default:
        return FW_WAIT_FAILED;
    }
    sync_p2(h);
    return ok ? FW_WAIT_RETURNED : wait_ended(h);
}

static void host_dsp_write(void *ctx, uint8_t byte) { board_dsp_write(((native_host *)ctx)->b, byte); }

static void host_frame_done(void *ctx) { board_capture_frame(((native_host *)ctx)->b); }

/* Another bank's ROM byte: the option sockets are empty (parent spec 9), every byte reads $FF. */
static uint8_t host_read_bank(void *ctx, uint16_t addr)
{
    (void)ctx;
    (void)addr;
    return 0xFF;
}

/* A bank entry: recorded for the engine, which runs the entry's C afresh; the I flag set (bank_enter's SEI). */
static void host_enter(void *ctx, uint16_t addr, uint8_t a, uint8_t x)
{
    native_host *h = ctx;
    h->entered = true;
    h->entry = addr;
    h->entry_a = a;
    h->entry_x = x;
    h->b->i = true;
}

/* boot_reset's CPU set-up: of SEI, SP $FF, CLT, CLD, CLC and CLV only the I flag has a counterpart. */
static void host_reset_cpu(void *ctx) { ((native_host *)ctx)->b->i = true; }

/* CLI or SEI. */
static void host_set_interrupts(void *ctx, bool enabled) { ((native_host *)ctx)->b->i = !enabled; }

bool native_host_init(native_host *h, board *b)
{
    const fw_image *img = fw_image_get();
    h->b = b;
    h->deadline = 0;
    h->idle = NULL;
    h->idle_user = NULL;
    h->entered = false;
    h->entry = 0;
    h->entry_a = h->entry_x = 0;
    h->host.ctx = h;
    h->host.rom_call = host_rom_call;
    h->host.set_low_window = host_set_low_window;
    h->host.read_sfr = host_read_sfr;
    h->host.write_sfr = host_write_sfr;
    h->host.wait = host_wait;
    h->host.dsp_write = host_dsp_write;
    h->host.frame_done = host_frame_done;
    h->host.read_bank = host_read_bank;
    h->host.enter = host_enter;
    h->host.rom_call_unhooked = host_rom_call;
    h->host.reset_cpu = host_reset_cpu;
    h->host.set_interrupts = host_set_interrupts;
    fw_init(&h->f, b->ram, img, &h->host);
    b->rom = img ? img->rom : NULL;
    b->fw = &h->f;
    return img != NULL;
}
```

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test suite_native"`
Expected: the build passes with no warnings, and

```
suite_native                 ok
1 suite(s), 0 failure(s), 0 skip(s)
```

- [ ] **Step 5: Run the full suite**

Run: `cmd //c ".\build.bat test"` (in the background)
Expected:

- `42 suite(s), 0 failure(s), 0 skip(s)`, no warnings (955 s in the proof run);
- every other line exactly as in Task 1 Step 5 (`firmware_data` 49104, `diff_main` 66 runs, 129 idles, 31533 frame events, `golden_hybrid` 132 modes, the four timer-scale lines, the synth peaks; `diff_lts`, `diff_play`, `diff_serial` line for line); every golden unchanged.

- [ ] **Step 6: Commit**

```bash
git add src/engine/native_host.h src/engine/native_host.c tests/test_native.c tests/suites.h
git commit -m "The native host: the fw_host on a board (the SFRs, the DSP port and the frame event; each wait with the hybrid's budgets and the phase deadline; read_bank \$FF; enter recorded; the CPU calls the I flag; the ROM calls fail), its tests in suite_native (M5a 3.5, 4.2)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 3: The native engine: `boot_reset` from the reset vector under the ROM engine's phase budgets and idle handler, bank entries run afresh, the NUL bug a fault at $A893; `frame_list.h` with `engine_input` and `engine_status`; the rest of `suite_native` with the Review Focus 1 and 3 tests; the build check of the native files (M5a spec §3.1, §3.6, §4.2, §4.7 item 3)

**Facts checked / what was verified while proving the plan** (`src/engine/rom_engine.c`, `src/engine/hybrid_engine.c`, `src/engine/thunks.c` (`run_frame`, `host_enter`), `src/machine/machine.c` (`machine_run`, `machine_idle`, `find_hook`), `src/firmware/fw.c`, `tests/test_golden_hybrid.c` (`test_review_focus`)):
- **The run** (`rom_engine_run_input`, the hybrid with every replacement): `machine_init` (the CPU at the reset vector $A61C with I set), the phase deadline `ROM_ENGINE_BOOT_CYCLES` from 0, then `machine_run`, whose first step fires `boot_reset`'s hook: the whole firmware runs inside that one C call. The idle handler (`on_idle`): the watch first; at the first idle the frame counters reset, the sinks on, the input and the CR queued (a queue too small is `MFAULT_INPUT_OVERFLOW`, and the run stops), the deadline moved to `ROM_ENGINE_SPEAK_CYCLES` from there; at the second idle the run stops. A stopped input wait leaves the machine at the idle point and `machine_run` returns `MACHINE_IDLE` after one more `prepare`, which changes no state there.
- **The end of a run**: a stop at the idle handler is `MACHINE_IDLE`; a wait that reached the phase deadline stops the run (`FW_WAIT_STOPPED`) and `machine_run` returns `MACHINE_CYCLE_LIMIT`; a fault (a machine fault set during the C, or `f->fault`, whose first address `fw.c`'s `fault` keeps) is `MACHINE_FAULT`, and it wins over a stop (`exec` returns `m->fault == MFAULT_NONE` after the hook). A C fault inside an interrupt handler is caught by the handler's nested thunk, the wait then fails and faults at its own address, which `fault` does not record over the first: the native engine's single trap gives the same fault and address.
- **The entries** (`run_frame`): `fw_enter` leaves the machine at the entry with I set; an entry that enters itself again (a warm `@=1` under `bank_entry_warm`) runs its C afresh at once, with no check; another entry runs when `machine_run` reaches its hook: not after a fault (`exec` returned false), not once the phase deadline has passed (`machine_run`'s bound, then the engine loop's retry up to `m->phase_deadline`), and only while P2 selects the main ROM (`pc_escaped` at $FFD0/$FFD4). A timer or transmit wait may return with its last pass past the deadline; a later wait then stops at once, and a bank entry after it ends the run as `MACHINE_CYCLE_LIMIT` before the entry runs.
- **The NUL bug on native**: `"@A"` NUL CR and `"@M5"` NUL CR end as `ENGINE_FAULT`, `BOARD_FAULT_FIRMWARE` at $A893, with no frame (`test_nul_bug`); the hybrid keeps its ROM fallback (`test_golden_hybrid`'s `test_review_focus`).
- **Review Focus 3** (`test_queue_fit`): the ring holds `MACHINE_QUEUE_SIZE − 1` = 4,095 bytes: 4,094 spaces and the CR are accepted (`ENGINE_IDLE`, no frame: spaces speak nothing), 4,095 spaces and the CR are the input-overflow fault, on the native engine and on the ROM engine alike (the same status and fault kind, the same frames).
- **Review Focus 1** (`test_runs_in_one_process`): "Hello world." on an engine used before by an overflow and both NUL faults gives the first run's 105 frames, cycle count and RAM, as does a second engine never used. The engine is the caller's storage, wiped at the start of each run; the data image is built once and only read (`const fw_image *`); the sinks and the idle handler are cleared at the end of each run.
- **`frame_list` moves out of `rom_engine.h`**: `rom_engine.h` includes `engine/frame_list.h` where the types were, so every user compiles unchanged; `ENGINE_FRAME_BYTES` is `APOLLO_FRAME_BYTES` (20), and the `frames` member keeps its type. `engine_status` lists its values in `machine_status`'s order.
- **The spec's signature** `native_engine_run_input(cfg, in, include_boot, produced, raw, watch)` has no place for the board the watch, the final state and the fault come from: the run takes the caller's `native_engine` first (the board, the host, the bank entries made), as `rom_engine_run_input` takes the machine.

**Files:**
- Create: `src/engine/frame_list.h`, `src/engine/frame_list.c`, `src/engine/native_engine.h`, `src/engine/native_engine.c`
- Modify: `src/engine/rom_engine.h`, `src/engine/rom_engine.c`, `tests/test_native.c`, `build.bat`

**Interfaces:**
- Consumes: the board (Task 1), the native host (Task 2); `boot_reset` through the board's dispatch.
- Produces (Tasks 4–5 use them):

```c
/* src/engine/frame_list.h (rom_engine.h includes it) */
#define ENGINE_FRAME_BYTES 20
typedef struct { uint8_t (*frames)[ENGINE_FRAME_BYTES]; size_t count, cap; } frame_list;
void frame_list_add(frame_list *fl, const uint8_t frame[ENGINE_FRAME_BYTES]);
void frame_list_free(frame_list *fl);
char *frame_list_hex(const frame_list *fl);
typedef struct { const uint8_t *bytes; size_t n; bool cr; } engine_input;
typedef enum { ENGINE_IDLE, ENGINE_FAULT, ENGINE_CYCLE_LIMIT } engine_status;
/* src/engine/native_engine.h */
#define NATIVE_BOOT_CYCLES 100000000ULL, NATIVE_SPEAK_CYCLES 400000000ULL
typedef struct { uint8_t port_in[5]; uint32_t cycle_num, cycle_den; } native_config;
typedef struct { void (*on_frame)(void *user, board *b); void (*on_idle)(void *user, board *b); void *user; }
    native_watch;
typedef struct native_engine { board b; native_host h; unsigned entries, unwinds; } native_engine;
void native_default_config(native_config *cfg);
engine_status native_engine_run_input(native_engine *e, const native_config *cfg, const engine_input *in,
                                      bool include_boot, frame_list *produced, frame_list *raw,
                                      const native_watch *watch);
engine_status native_engine_speak(native_engine *e, const native_config *cfg, const char *text, bool include_boot,
                                  frame_list *produced, frame_list *raw);
```

No name collides: `grep -rn "frame_list_add\|engine_status\|ENGINE_IDLE\|ENGINE_FRAME_BYTES\|native_engine\|native_config\|native_watch\|NATIVE_" src tests` prints nothing before this task.

- [ ] **Step 1: Write the failing tests**

`suite_native` gains whole runs of the engine: a boot and an utterance, a bank entry run afresh, the NUL bug, Review Focus 3 and 1, and the engine's constants.

**`tests/test_native.c`**: replace

```c
/* The native host (M5a spec 4.2), with no ROM: each host call on a board, read_bank's $FF, the failing ROM calls, the
   waits' bounds and ends. */
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "engine/native_host.h"
#include "firmware/cmd_addr.h"
```

with

```c
/* The native host and engine (M5a spec 4.2), with no ROM: each host call on a board, read_bank's $FF, the failing ROM
   calls, the waits' bounds and ends; then whole runs of the native engine: a boot to the first idle and a short
   utterance to the second, a bank entry run afresh, input overflow, the NUL bug ending as a fault at $A893, and the
   M5a plan's Review Focus 1 (several runs in one process, faults among them) and 3 (input that just fits the serial
   queue, and one byte more; compared with the ROM engine when the ROM is present). */
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "rom_fixture.h"
#include "engine/native_host.h"
#include "engine/native_engine.h"
#include "engine/rom_engine.h"
#include "firmware/cmd_addr.h"
```

**`tests/test_native.c`**: replace

```c
static native_host g_h;
```

with

```c
static native_host g_h;
static native_engine g_e, g_e2;
```

**`tests/test_native.c`**: replace

```c
void suite_native(void)
{
    test_init();
    test_ports();
    test_bank_and_rom_calls();
    test_cpu_calls();
    test_waits();
    test_deadline();
}
```

with

```c
/* ---- whole runs ---- */

static unsigned g_idles, g_frames, g_boot_frames;

static void count_frame(void *user, board *b)
{
    (void)user;
    (void)b;
    g_frames++;
}

static void count_idle(void *user, board *b)
{
    (void)user;
    (void)b;
    if (g_idles++ == 0)
        g_boot_frames = g_frames;
}

/* A boot to the first idle (the announcement's frame events), then "Hello world." to the second: the frames of
   tests/golden/hello.frm. */
static void test_boot_and_speak(void)
{
    native_watch w = { count_frame, count_idle, NULL };
    engine_input in = { (const uint8_t *)"Hello world.", 12, true };
    frame_list fl = { 0 };
    g_idles = g_frames = g_boot_frames = 0;
    CHECK_EQ(native_engine_run_input(&g_e, NULL, &in, false, &fl, NULL, &w), ENGINE_IDLE);
    CHECK_EQ(g_idles, 2);
    CHECK_EQ(g_boot_frames, 41);
    CHECK_EQ(g_frames, 41 + 105);
    CHECK_EQ(fl.count, 105);
    CHECK_EQ(g_e.b.frames_produced, 105);
    CHECK_EQ(g_e.b.fault, BOARD_FAULT_NONE);
    CHECK_EQ(g_e.entries, 1);
    CHECK_EQ(g_e.unwinds, 0);
    CHECK(g_e.b.waiting && g_e.b.idle_point);   /* left at the idle point */
    char *got = frame_list_hex(&fl);
    char *want = test_read_file("tests/golden/hello.frm");
    CHECK(want != NULL && !strcmp(got, want));
    free(got);
    free(want);
    frame_list_free(&fl);
    CHECK_EQ(native_engine_speak(&g_e, NULL, "Hello world.", true, &fl, NULL), ENGINE_IDLE);
    CHECK_EQ(fl.count, 41 + 105);                 /* with the boot frames */
    frame_list_free(&fl);
}

/* @=1 twice: the main bank's warm entry, the second from under its own C, run afresh. */
static void test_bank_entry(void)
{
    frame_list fl = { 0 };
    CHECK_EQ(native_engine_speak(&g_e, NULL, "@=1 One. @=1 Two. Three.", false, &fl, NULL), ENGINE_IDLE);
    CHECK_EQ(g_e.unwinds, 2);
    CHECK_EQ(g_e.entries, 3);
    CHECK(fl.count > 0);
    frame_list_free(&fl);
}

/* The NUL bug (M4.6c1 spec 2.5): a handler address with no C, the ROM cmd_run_next the C hands it to: a firmware fault
   at $A893, no frame. */
static void test_nul_bug(void)
{
    static const char *const k_in[] = { "@A", "@M5" };
    for (int i = 0; i < 2; i++) {
        uint8_t bytes[8];
        size_t n = strlen(k_in[i]);
        memcpy(bytes, k_in[i], n);
        bytes[n] = 0;
        engine_input in = { bytes, n + 1, true };
        frame_list fl = { 0 };
        CHECK_EQ(native_engine_run_input(&g_e, NULL, &in, false, &fl, NULL, NULL), ENGINE_FAULT);
        CHECK_EQ(g_e.b.fault, BOARD_FAULT_FIRMWARE);
        CHECK_EQ(g_e.b.fault_addr, ROM_CMD_RUN_NEXT);
        CHECK_EQ(fl.count, 0);
        frame_list_free(&fl);
    }
}

/* Review Focus 3: MACHINE_QUEUE_SIZE - 1 bytes with the CR are accepted (spaces: nothing spoken); one more is input
   overflow. With the ROM the ROM engine ends the same way, with the same frames. */
static void test_queue_fit(void)
{
    static uint8_t spaces[BOARD_QUEUE_SIZE];
    memset(spaces, ' ', sizeof spaces);
    size_t len;
    const uint8_t *rom = test_rom(&len);
    for (size_t n = BOARD_QUEUE_SIZE - 2; n <= BOARD_QUEUE_SIZE - 1; n++) {
        engine_input in = { spaces, n, true };
        frame_list fl = { 0 }, rl = { 0 };
        engine_status st = native_engine_run_input(&g_e, NULL, &in, false, &fl, NULL, NULL);
        bool fits = n + 1 < BOARD_QUEUE_SIZE;
        CHECK_EQ(st, fits ? ENGINE_IDLE : ENGINE_FAULT);
        CHECK_EQ(g_e.b.fault, fits ? BOARD_FAULT_NONE : BOARD_FAULT_INPUT_OVERFLOW);
        if (rom) {
            static machine m;
            machine_status ms = rom_engine_run_input(&m, rom, len, NULL, NULL, NULL, &in, false, &rl, NULL, NULL);
            CHECK_EQ(ms, (int)st);
            CHECK_EQ(m.fault, fits ? MFAULT_NONE : MFAULT_INPUT_OVERFLOW);
            CHECK(rl.count == fl.count && !memcmp(rl.frames, fl.frames, fl.count * ENGINE_FRAME_BYTES));
        }
        frame_list_free(&fl);
        frame_list_free(&rl);
    }
}

/* Review Focus 1: runs in one process, faults among them, each as from a fresh process: "Hello world." on a used
   engine after an overflow and both NUL faults gives the frames, cycles and RAM of the first, and of a run on an
   engine never used. */
static void test_runs_in_one_process(void)
{
    frame_list a = { 0 }, b = { 0 }, c = { 0 };
    static uint8_t ram[BOARD_RAM_SIZE];
    CHECK_EQ(native_engine_speak(&g_e, NULL, "Hello world.", false, &a, NULL), ENGINE_IDLE);
    uint64_t cycles = g_e.b.cycles;
    memcpy(ram, g_e.b.ram, sizeof ram);
    test_nul_bug();
    test_queue_fit();
    CHECK_EQ(native_engine_speak(&g_e, NULL, "Hello world.", false, &b, NULL), ENGINE_IDLE);
    CHECK_EQ(g_e.b.cycles, cycles);
    CHECK(!memcmp(g_e.b.ram, ram, sizeof ram));
    CHECK_EQ(native_engine_speak(&g_e2, NULL, "Hello world.", false, &c, NULL), ENGINE_IDLE);
    CHECK_EQ(g_e2.b.cycles, cycles);
    CHECK(!memcmp(g_e2.b.ram, ram, sizeof ram));
    CHECK(a.count == 105 && b.count == 105 && c.count == 105);
    CHECK(!memcmp(a.frames, b.frames, a.count * ENGINE_FRAME_BYTES));
    CHECK(!memcmp(a.frames, c.frames, a.count * ENGINE_FRAME_BYTES));
    frame_list_free(&a);
    frame_list_free(&b);
    frame_list_free(&c);
}

/* The engine's constants are the ROM engine's. */
static void test_constants(void)
{
    CHECK_EQ(NATIVE_BOOT_CYCLES, ROM_ENGINE_BOOT_CYCLES);
    CHECK_EQ(NATIVE_SPEAK_CYCLES, ROM_ENGINE_SPEAK_CYCLES);
    CHECK_EQ(ENGINE_FRAME_BYTES, APOLLO_FRAME_BYTES);
    CHECK_EQ(ENGINE_IDLE, MACHINE_IDLE);
    CHECK_EQ(ENGINE_FAULT, MACHINE_FAULT);
    CHECK_EQ(ENGINE_CYCLE_LIMIT, MACHINE_CYCLE_LIMIT);
    native_config cfg;
    machine_config mc;
    native_default_config(&cfg);
    machine_default_config(&mc);
    CHECK(!memcmp(cfg.port_in, mc.port_in, sizeof cfg.port_in));
    CHECK(cfg.cycle_num == mc.cycle_num && cfg.cycle_den == mc.cycle_den);
}

void suite_native(void)
{
    test_init();
    test_ports();
    test_bank_and_rom_calls();
    test_cpu_calls();
    test_waits();
    test_deadline();
    test_constants();
    test_boot_and_speak();
    test_bank_entry();
    test_nul_bug();
    test_queue_fit();
    test_runs_in_one_process();
}
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test suite_native"`
Expected: the build stops with compile errors (the proof run, first lines):

```
tests\test_native.c(11): fatal error C1083: Cannot open include file: 'engine/native_engine.h': No such file or directory
```

- [ ] **Step 3: Implement**

The neutral types first, then the engine and the build check.

**Create `src/engine/frame_list.h`:**

```c
#ifndef ENGINE_FRAME_LIST_H
#define ENGINE_FRAME_LIST_H
/* What every engine shares (M5a spec 3.1): the frame list, the input of a run and its status. No machine/ or cpu740/
   header: the ROM and hybrid engines (rom_engine.h includes this) and the native engine use the same types. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ENGINE_FRAME_BYTES 20   /* APOLLO_FRAME_BYTES, BOARD_FRAME_BYTES */

typedef struct {
    uint8_t (*frames)[ENGINE_FRAME_BYTES];
    size_t count, cap;
} frame_list;

/* Appends one frame (aborts when out of memory). */
void frame_list_add(frame_list *fl, const uint8_t frame[ENGINE_FRAME_BYTES]);
void frame_list_free(frame_list *fl);

/* One frame per line, 40 uppercase hex digits + '\n'. Caller frees. */
char *frame_list_hex(const frame_list *fl);

/* The input an engine run feeds at the end of boot: n bytes (raw serial input: a NUL may be among them), then a CR
   when cr is set. */
typedef struct { const uint8_t *bytes; size_t n; bool cr; } engine_input;

/* How an engine run ended, in machine_status's order (MACHINE_IDLE, MACHINE_FAULT, MACHINE_CYCLE_LIMIT): the second
   input idle, a fault, or the phase deadline. */
typedef enum { ENGINE_IDLE, ENGINE_FAULT, ENGINE_CYCLE_LIMIT } engine_status;

#endif
```

**Create `src/engine/frame_list.c`:**

```c
#include <stdlib.h>
#include <string.h>
#include "engine/frame_list.h"

void frame_list_add(frame_list *fl, const uint8_t frame[ENGINE_FRAME_BYTES])
{
    if (fl->count == fl->cap) {
        size_t cap = fl->cap ? fl->cap * 2 : 256;
        void *grown = realloc(fl->frames, cap * sizeof fl->frames[0]);
        if (!grown)
            abort();
        fl->frames = grown;
        fl->cap = cap;
    }
    memcpy(fl->frames[fl->count++], frame, ENGINE_FRAME_BYTES);
}

void frame_list_free(frame_list *fl)
{
    free(fl->frames);
    fl->frames = NULL;
    fl->count = fl->cap = 0;
}

char *frame_list_hex(const frame_list *fl)
{
    static const char k_hex[] = "0123456789ABCDEF";
    char *s = malloc(fl->count * (ENGINE_FRAME_BYTES * 2 + 1) + 1);
    char *p = s;
    if (!s)
        abort();
    for (size_t i = 0; i < fl->count; i++) {
        for (int b = 0; b < ENGINE_FRAME_BYTES; b++) {
            *p++ = k_hex[fl->frames[i][b] >> 4];
            *p++ = k_hex[fl->frames[i][b] & 0x0F];
        }
        *p++ = '\n';
    }
    *p = '\0';
    return s;
}
```

**`src/engine/rom_engine.h`**: replace

```c
#include "machine/machine.h"

#define ROM_ENGINE_BOOT_CYCLES  100000000ULL   /* 50 s of emulated time at 2 MHz */
#define ROM_ENGINE_SPEAK_CYCLES 400000000ULL   /* 200 s */

typedef struct {
    uint8_t (*frames)[APOLLO_FRAME_BYTES];
    size_t count, cap;
} frame_list;

void frame_list_free(frame_list *fl);

/* One frame per line, 40 uppercase hex digits + '\n'. Caller frees. */
char *frame_list_hex(const frame_list *fl);

/* Called once after machine_init, before boot: installs hooks (hybrid engine, differential tests). */
typedef void (*engine_setup_fn)(machine *m, void *user);

/* The input an engine run feeds at the end of boot: n bytes (raw serial input: a NUL may be among them), then a CR
   when cr is set. */
typedef struct { const uint8_t *bytes; size_t n; bool cr; } engine_input;
```

with

```c
#include "machine/machine.h"
#include "engine/frame_list.h"

#define ROM_ENGINE_BOOT_CYCLES  100000000ULL   /* 50 s of emulated time at 2 MHz */
#define ROM_ENGINE_SPEAK_CYCLES 400000000ULL   /* 200 s */

/* Called once after machine_init, before boot: installs hooks (hybrid engine, differential tests). */
typedef void (*engine_setup_fn)(machine *m, void *user);
```

**`src/engine/rom_engine.c`**: replace

```c
static void on_frame(void *user, const uint8_t frame[APOLLO_FRAME_BYTES])
{
    frame_sink *s = user;
    frame_list *fl = s->list;
    if (!s->enabled)
        return;
    if (fl->count == fl->cap) {
        size_t cap = fl->cap ? fl->cap * 2 : 256;
        void *grown = realloc(fl->frames, cap * sizeof fl->frames[0]);
        if (!grown)
            abort();
        fl->frames = grown;
        fl->cap = cap;
    }
    memcpy(fl->frames[fl->count++], frame, APOLLO_FRAME_BYTES);
}

void frame_list_free(frame_list *fl)
{
    free(fl->frames);
    fl->frames = NULL;
    fl->count = fl->cap = 0;
}

char *frame_list_hex(const frame_list *fl)
{
    static const char k_hex[] = "0123456789ABCDEF";
    char *s = malloc(fl->count * (APOLLO_FRAME_BYTES * 2 + 1) + 1);
    char *p = s;
    if (!s)
        abort();
    for (size_t i = 0; i < fl->count; i++) {
        for (int b = 0; b < APOLLO_FRAME_BYTES; b++) {
            *p++ = k_hex[fl->frames[i][b] >> 4];
            *p++ = k_hex[fl->frames[i][b] & 0x0F];
        }
        *p++ = '\n';
    }
    *p = '\0';
    return s;
}
```

with

```c
static void on_frame(void *user, const uint8_t frame[APOLLO_FRAME_BYTES])
{
    frame_sink *s = user;
    if (s->enabled)
        frame_list_add(s->list, frame);
}
```

**Create `src/engine/native_engine.h`:**

```c
#ifndef ENGINE_NATIVE_ENGINE_H
#define ENGINE_NATIVE_ENGINE_H
/* The native engine (M5a spec 3.6): the C firmware on the CPU-less board, with no ROM and no emulated CPU, run as the
   ROM engine runs the machine: the reset vector's C (boot_reset) under the boot phase's deadline, the ROM engine's
   idle handler (the input fed at the first input idle, the run stopped at the next), each bank entry's C run afresh,
   cycle for cycle as the all-C hybrid. Nothing here includes a machine/ or cpu740/ header. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "board/board.h"
#include "engine/frame_list.h"
#include "engine/native_host.h"

#define NATIVE_BOOT_CYCLES  100000000ULL   /* ROM_ENGINE_BOOT_CYCLES: the boot phase's budget */
#define NATIVE_SPEAK_CYCLES 400000000ULL   /* ROM_ENGINE_SPEAK_CYCLES: the speaking phase's, from the feed */

/* The board's input pins P0-P4 and the TEST-ONLY timer scale (machine_config without the coverage). */
typedef struct { uint8_t port_in[5]; uint32_t cycle_num, cycle_den; } native_config;

/* Watches a native run (engine_watch): on_frame at every frame event, on_idle at every input idle before the idle
   handler feeds or stops; both get the board, which on_idle may change (an injection). */
typedef struct {
    void (*on_frame)(void *user, board *b);
    void (*on_idle)(void *user, board *b);
    void *user;
} native_watch;

/* A native run's state, the caller's storage: the board (its final state when the run is over; for ENGINE_FAULT its
   fault and fault_addr say which and where), the host and the firmware on it, the bank entries made (unwinds: C call
   chains abandoned at fw_enter) and the entries run (entries: the reset's included). Each run starts it afresh. */
typedef struct native_engine {
    board b;
    native_host h;
    unsigned entries, unwinds;
} native_engine;

/* The machine's defaults (machine_default_config): the switches off (serial mode), P2 pulled to $70, scale 1/1. */
void native_default_config(native_config *cfg);

/* The run (rom_engine_run_input's on the board): the board reset with cfg's pins and scale (NULL: the defaults), the
   reset vector's C run with the firmware's trap set; at the first input idle the frame counters reset, the frames kept
   from then on (from the start too if include_boot), the input fed and the deadline moved to NATIVE_SPEAK_CYCLES from
   there; at the next idle the run stops: ENGINE_IDLE. The phase deadline reached: ENGINE_CYCLE_LIMIT. ENGINE_FAULT:
   input longer than the serial queue, a firmware fault (a read outside the converted data, a failed ROM call: the
   NUL bug's fallback at $A893), an unmapped vector, the DME strobe, code under another bank. produced gets the
   produced frames; raw, when not NULL, the raw DSP-port frames of the speaking phase; watch (may be NULL) sees every
   event. */
engine_status native_engine_run_input(native_engine *e, const native_config *cfg, const engine_input *in,
                                      bool include_boot, frame_list *produced, frame_list *raw,
                                      const native_watch *watch);
/* native_engine_run_input with the text and a CR, no watch. */
engine_status native_engine_speak(native_engine *e, const native_config *cfg, const char *text, bool include_boot,
                                  frame_list *produced, frame_list *raw);

#endif
```

**Create `src/engine/native_engine.c`:**

```c
#include <setjmp.h>
#include <string.h>
#include "engine/native_engine.h"

#ifdef _MSC_VER
#pragma warning(disable : 4611)   /* setjmp with no C++ objects to destroy: the trap of fw faults */
#endif

void native_default_config(native_config *cfg)
{
    /* Switches off (serial mode); P2 pulled to $70 so the main ROM is selected at reset. */
    static const uint8_t k_default[5] = { 0x00, 0x00, 0x70, 0x00, 0x00 };
    memcpy(cfg->port_in, k_default, sizeof k_default);
    cfg->cycle_num = cfg->cycle_den = 1;
}

/* One run: its engine, its frame sinks, its input and watch, the idle handler's count of idles and whether it ended
   the run. */
typedef struct {
    native_engine *e;
    frame_list *produced, *raw;
    bool produced_on, raw_on;
    const engine_input *in;
    const native_watch *watch;
    unsigned idles;
    bool stopped;
} native_run;

/* The board's frame callback: the watch, then the produced-frame sink. */
static void on_produced(void *user, const uint8_t frame[BOARD_FRAME_BYTES])
{
    native_run *r = user;
    if (r->watch && r->watch->on_frame)
        r->watch->on_frame(r->watch->user, &r->e->b);
    if (r->produced_on && r->produced)
        frame_list_add(r->produced, frame);
}

static void on_raw(void *user, const uint8_t frame[BOARD_FRAME_BYTES])
{
    native_run *r = user;
    if (r->raw_on)
        frame_list_add(r->raw, frame);
}

/* The ROM engine's idle handler (rom_engine.c on_idle): the watch first; at the first idle (the end of boot) the
   counters reset, the sinks on, the input fed and the speaking phase's deadline set; at the next the run stops. */
static board_idle_action on_idle(board *b, void *user)
{
    native_run *r = user;
    if (r->watch && r->watch->on_idle)
        r->watch->on_idle(r->watch->user, b);
    if (r->idles++ > 0) {
        r->stopped = true;
        return BOARD_STOP;
    }
    b->frames_produced = 0;
    b->raw_frames = 0;
    b->dsp_resets = 0;
    b->raw_discarded = 0;
    r->produced_on = true;
    r->raw_on = true;
    if (!board_feed_serial(b, r->in->bytes, r->in->n) ||
        (r->in->cr && !board_feed_serial(b, (const uint8_t *)"\r", 1))) {
        b->fault = BOARD_FAULT_INPUT_OVERFLOW;   /* input longer than the serial queue */
        return BOARD_STOP;
    }
    r->e->h.deadline = b->cycles + NATIVE_SPEAK_CYCLES;
    return BOARD_FEED;
}

/* The entries (the hybrid's entry rows and run_frame): the reset vector's C first. A stopped wait ends the run; a bank
   entry (fw_enter) runs the entry's C afresh with the recorded A and X, at once when it is the entry that is running
   (a warm @=1 under bank_entry_warm), else as machine_run reaches the hooked address: not after a fault, not once the
   phase deadline has passed, and only while P2 selects the main ROM. An entry that returns, or any other end, is a
   firmware fault (at the C's fault address, or the entry's). */
static void run_entries(native_engine *e)
{
    board *b = &e->b;
    fw *f = &e->h.f;
    jmp_buf trap;
    uint16_t entry = board_vector(b, BOARD_VEC_RESET);
    uint8_t a = 0, x = 0;
    f->trap = &trap;
    for (;;) {
        e->entries++;
        e->h.entered = false;
        if (setjmp(trap) == 0 && !board_call(b, entry, a, x))
            break;   /* no C at the reset vector: BOARD_FAULT_VECTOR */
        if (f->stopped)
            break;
        if (f->unwound && e->h.entered) {
            f->unwound = false;
            e->unwinds++;
            if (e->h.entry != entry) {
                if (b->fault != BOARD_FAULT_NONE || b->cycles >= e->h.deadline)
                    break;
                if (!board_main_bank(b)) {
                    b->fault = BOARD_FAULT_ESCAPED;
                    break;
                }
            }
            entry = e->h.entry;
            a = e->h.entry_a;
            x = e->h.entry_x;
            continue;
        }
        if (b->fault == BOARD_FAULT_NONE) {
            b->fault = BOARD_FAULT_FIRMWARE;
            b->fault_addr = f->fault ? f->fault_addr : entry;
        }
        break;
    }
    f->trap = NULL;
}

engine_status native_engine_run_input(native_engine *e, const native_config *cfg, const engine_input *in,
                                      bool include_boot, frame_list *produced, frame_list *raw,
                                      const native_watch *watch)
{
    native_config def;
    if (!cfg) {
        native_default_config(&def);
        cfg = &def;
    }
    memset(e, 0, sizeof *e);
    board_init(&e->b, cfg->port_in, cfg->cycle_num, cfg->cycle_den);
    if (!native_host_init(&e->h, &e->b)) {
        e->b.fault = BOARD_FAULT_FIRMWARE;   /* the data image does not build */
        return ENGINE_FAULT;
    }
    native_run r = { e, produced, raw, include_boot, false, in, watch, 0, false };
    board_set_outputs(&e->b, on_produced, &r);
    if (raw)
        board_set_raw_output(&e->b, on_raw, &r);
    e->h.idle = on_idle;
    e->h.idle_user = &r;
    e->h.deadline = e->b.cycles + NATIVE_BOOT_CYCLES;
    run_entries(e);
    board_set_outputs(&e->b, NULL, NULL);   /* the sinks and the handler live on this stack frame */
    board_set_raw_output(&e->b, NULL, NULL);
    e->h.idle = NULL;
    e->h.idle_user = NULL;
    if (e->b.fault != BOARD_FAULT_NONE)
        return ENGINE_FAULT;
    return r.stopped ? ENGINE_IDLE : ENGINE_CYCLE_LIMIT;
}

engine_status native_engine_speak(native_engine *e, const native_config *cfg, const char *text, bool include_boot,
                                  frame_list *produced, frame_list *raw)
{
    engine_input in = { (const uint8_t *)text, strlen(text), true };
    return native_engine_run_input(e, cfg, &in, include_boot, produced, raw, NULL);
}
```

**`build.bat`** (CRLF): replace

```bat
rem M5a spec 3.1: src\board builds without the emulator too. Syntax-check it against an include root holding only the
rem headers it may use (src\board and src\firmware, copied to build\noemu), so an include of a machine\ or cpu740\
rem header cannot be found.
if exist build\noemu rmdir /s /q build\noemu
for %%d in (board firmware\data) do mkdir build\noemu\%%d
for %%d in (board firmware firmware\data) do copy /y src\%%d\*.h build\noemu\%%d >nul
cl /nologo /std:c11 /W4 /WX /wd4244 /Zs /D_CRT_SECURE_NO_WARNINGS /Ibuild\noemu src\board\*.c || exit /b 1
```

with

```bat
rem M5a spec 3.1: src\board and the native files build without the emulator too. Syntax-check them against an include
rem root holding only the headers they may use (src\board, src\firmware and the native headers of src\engine, copied
rem to build\noemu), so an include of a machine\ or cpu740\ header cannot be found.
if exist build\noemu rmdir /s /q build\noemu
for %%d in (board firmware\data engine) do mkdir build\noemu\%%d
for %%d in (board firmware firmware\data) do copy /y src\%%d\*.h build\noemu\%%d >nul
for %%f in (frame_list native_host native_engine) do copy /y src\engine\%%f.h build\noemu\engine >nul
cl /nologo /std:c11 /W4 /WX /wd4244 /Zs /D_CRT_SECURE_NO_WARNINGS /Ibuild\noemu src\board\*.c src\engine\frame_list.c ^
    src\engine\native_host.c src\engine\native_engine.c || exit /b 1
```

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test suite_native"`, then `build/apollo2_tests.exe rom_speak` (the ROM engine through the moved frame list).
Expected: the build passes with no warnings (the three syntax checks included), and

```
suite_native                 ok
1 suite(s), 0 failure(s), 0 skip(s)
  zoo: 35 frames, 8 lower-window reads
suite_rom_speak              ok
1 suite(s), 0 failure(s), 0 skip(s)
```

- [ ] **Step 5: Run the full suite**

Run: `cmd //c ".\build.bat test"` (in the background)
Expected:

- `42 suite(s), 0 failure(s), 0 skip(s)`, no warnings, the three syntax checks passing (942 s in the proof run);
- every line of the suites' output exactly as in Task 2 Step 5: the ROM and hybrid engines, now with the frame list of `frame_list.c`, give the same results (`diff_main` 66 runs, 129 idles, 31533 frame events; `golden_hybrid` 132 modes; the four timer-scale lines); every golden unchanged.

- [ ] **Step 6: Commit**

```bash
git add src/engine/frame_list.h src/engine/frame_list.c src/engine/native_engine.h src/engine/native_engine.c src/engine/rom_engine.h src/engine/rom_engine.c tests/test_native.c build.bat
git commit -m "The native engine: boot_reset from the reset vector under the ROM engine's phase budgets and idle handler, bank entries run afresh, the NUL bug a firmware fault at \$A893; frame_list.h with engine_input and engine_status; suite_native's runs with the Review Focus 1 and 3 tests; build.bat's check of the native files (M5a 3.1, 3.6, 4.2)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 4: `diff_native`: the all-C hybrid against the native engine at every event of every `diff_main` run and the Review Focus 2 and 4 runs, nothing excluded but the SFR bytes and the stack page; the extra runs and their injections shared with `diff_main`, whose output does not change (M5a spec §4.3, §4.7 item 4)

**Facts checked / what was verified while proving the plan** (`tests/test_diff_main.c`, `tests/diff_state.c`, `src/engine/hybrid_engine.c`, `src/engine/thunks.c`, `src/machine/machine.c`; everything below was run):
- **The pairing**: both engines call their watch at the same points (a frame event, inside `fw_frame_done`; an input idle, before the idle handler feeds or stops), so the events are paired by their index in each run: the native engine's k-th event is compared with the hybrid's k-th, kind and snapshot, and an injection is made at the same k-th idle or n-th speech frame event on both before the snapshot is taken. At the end: the same status, the same number of events, the same final state (one more snapshot), and as many bank entries on native (`unwinds`) as C call chains the hybrid abandoned (`hybrid_stats.unwinds`).
- **The snapshot** compares everything M5a spec §4.3 lists: all RAM but $DA–$FF and $0100–$01FF; the whole `m50734` struct with `memcmp` (every field, the timers' counts and the request bits included; the first differing field is named); the cycle count; the port writes, the raw position and bytes, the reset count, the partial bytes dropped at resets and the reset level; `frames_produced` and `raw_frames`; I; the queue's bytes, head and tail, and the busy level. `diff_main`'s exclusion of `isr_tick_div` … `isr_words_left` at idles (ROM against C timing) has no counterpart here: hybrid and native agree on them too.
- **The proof run**: 0 mismatches on every run, cycle counts included, with no board change after Tasks 1–3: no cycle charge or board effect beyond those of spec §3.3 was needed. Two deliberate breaks in a throwaway copy showed the comparison live: an `RTS` of 7 cycles in `board_wait_uart_tx` made `status_sent` and `host_line_high` (the two runs that send a byte) differ at their cycle counts, and a RAM bit flipped at the 30th frame event made every run differ there.
- **The targets** (M5a spec §4.3: "every extra run reaches its target address (a hook on the hybrid side)"): the all-C hybrid runs none of those addresses as ROM (they are inside C routines, or entries `fw_install` has hooked already, where `machine_hook` refuses a second hook), so no hook can count them there. `diff_main`, which runs the same list, keeps checking them on its ROM side; `diff_native`'s own runs check what they reach through the bank entries made.
- **Review Focus 2** (`rf2_longest`): 1,500 characters (`WORDS44` 34 times, then "dogs"), no punctuation: clauses cut at their length. Both engines end at the second idle after 8,654 frame events and 173,088,979 cycles in all (about 172 million in the speaking phase, 43% of its budget of 4 × 10⁸), with the same frames and state at every event. Of the other 1,500-character texts tried natively (commas, semicolons, long words, a question), the slowest, a list of number words with commas, took 248 million cycles (62% of the budget).
- **Review Focus 4**: `rf4_reentries` (`@=1 One. @=5 Two. @=1 Three.`) makes 2 bank entries (both `@=1`, the second from under the C `bank_entry_warm`; `@=5` is an empty socket); `rf4_banks` (`@=0 @=8 @=9 @=F @=G`) makes 1: `@=0` enters the main bank warm and the re-entry abandons the rest of the clause (the M4.6c1 plan's Review Focus 1 separated them by full stops to make three).
- **No run reaches the NUL bug's fallback**: no corpus file holds a NUL and no extra run has one, so the spec's "compared up to it and its end pinned" has nothing to apply to; `suite_native` pins the native end (Task 3).
- **`diff_main` unchanged**: its injections and extra runs move to `tests/diff_runs.c` (the injections now take a `diff_view` of RAM, the `m50734` state and the queue; `want` an `engine_status`, numbered as `machine_status`), and its output stays `66 runs, 129 idles, 31533 frame events compared in all`, each extra run's line as before.
- **The run time**: `diff_native` alone takes about 11 s (the hybrid side dominates: its emulated waits step the CPU model; the native side runs the same steps without it).

**Files:**
- Create: `tests/diff_runs.h`, `tests/diff_runs.c`, `tests/test_diff_native.c`
- Modify: `tests/test_diff_main.c`, `tests/suites.h`

**Interfaces:**
- Consumes: `hybrid_engine_run_input`, `hybrid_stats`, `engine_watch` (`hybrid_engine.h`, `rom_engine.h`); `native_engine_run_input`, `native_watch`, `native_default_config` (Task 3); `machine_default_config`; `test_rom`, `test_read_file`; `CORPUS_NAMES`.
- Produces:

```c
/* tests/diff_runs.h */
typedef struct { uint8_t *ram; m50734 *sfr; uint8_t *queue; size_t *q_head, *q_tail; } diff_view;
typedef enum { AT_IDLE, AT_SPEECH } inject_at;
typedef struct { const char *name, *text; uint8_t p4, p2, p0; void (*inject)(const diff_view *v); inject_at at;
                 unsigned n; uint16_t target; bool target_ramcode; engine_status want; uint8_t p3; } extra_run;
extern const extra_run diff_extra_runs[];
extern const size_t diff_extra_run_count;
/* tests/test_diff_native.c: suite_diff_native, the 43rd suite */
```

No name collides: `grep -rn "diff_view\|diff_extra_run\|suite_diff_native" src tests` prints nothing before this task.

- [ ] **Step 1: Write the tests**

The extra runs move to a file both differential tests share; `diff_native` is new.

**Create `tests/diff_runs.h`:**

```c
#ifndef DIFF_RUNS_H
#define DIFF_RUNS_H
/* The extra runs of the end-to-end differential tests (M4.6c2 spec 4.1, M5a spec 4.3), shared by diff_main (the ROM
   engine against the all-C hybrid) and diff_native (the hybrid against the native engine): each one's input, its
   configuration, an injection at the n-th idle or frame event, the ROM address its ROM run executes and the status it
   ends with. An injection is written against a neutral view of the state, so that it applies to the machine or the
   board alike. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "board/m50734.h"
#include "engine/frame_list.h"

/* What an injection may change: RAM, the m50734 state (its input pins) and the serial queue. */
typedef struct {
    uint8_t *ram;
    m50734 *sfr;
    uint8_t *queue;
    size_t *q_head, *q_tail;
} diff_view;

typedef enum { AT_IDLE, AT_SPEECH } inject_at;   /* the n-th idle; the n-th frame event after the first idle */

/* An extra run: its input, its configuration, an injection at the n-th idle or frame event (inject NULL: none), the
   ROM address its ROM run must execute and the status every engine must end with. */
typedef struct {
    const char *name, *text;
    uint8_t p4, p2, p0;               /* ORed into the default input pins of P4, P2, P0 */
    void (*inject)(const diff_view *v);
    inject_at at;
    unsigned n;
    uint16_t target;
    bool target_ramcode;
    engine_status want;
    uint8_t p3;                       /* ORed into the default input pins of P3 (the baud jumpers) */
} extra_run;

extern const extra_run diff_extra_runs[];
extern const size_t diff_extra_run_count;

#endif
```

**Create `tests/diff_runs.c`:**

```c
/* The extra runs of diff_main and diff_native (diff_runs.h), moved from test_diff_main.c (M5a spec 4.3): each reaches
   what no corpus text does, through its configuration or an injection. */
#include "diff_runs.h"
#include "firmware/fw_ram.h"
#include "firmware/bank_addr.h"

#define P4_DIP_PARALLEL  0x01   /* the P4 input's DIP bit 0: the parallel interface */
#define P4_DIP_ALT       0x02   /* DIP bit 1: the alternative bank table and the self-test input */
#define P2_SELFTEST      0x02   /* P2.1: the self-test input (boot_selftest $C60B BBC1 P2) */
#define P0_HOST_LINE     0x04   /* P0.2: the host's handshake line */
#define P3_BAUD_300      0x02   /* P3.1: a baud jumper (boot_init_baud $AC81 BBS1 P3) */
#define P3_BAUD_1200     0x04   /* P3.2: the other ($AC84, $AC9A BBS2 P3) */

/* ---- the extra runs' injections ---- */

/* At the end of boot: the running engine's minor version altered, so @=1 finds the bank's header different: the
   cold entry (bank_enter $0228). */
static void inj_cold(const diff_view *v) { v->ram[BANK_VER_MINOR]++; }
/* A status byte due (as isr_host_line_high leaves it, $C3F0) with the host line low, as at boot; or with the line
   raised too (the pin only: the machine raises no INT4). */
static void inj_status_due(const diff_view *v) { v->ram[SER_STATUS_DUE] = 1; }
static void inj_status_line_high(const diff_view *v)
{
    v->ram[SER_STATUS_DUE] = 1;
    v->sfr->port_in[0] |= P0_HOST_LINE;
}
/* At a frame event of the speech: "Two." arrives in the input ring, each byte put as ser_rx_put puts it (the machine
   delivers serial bytes at the idle WIT only, never while speech plays). */
static void inj_ring_two(const diff_view *v)
{
    for (const char *s = "Two."; *s; s++) {
        v->ram[v->ram[SER_RX_WRITE] | v->ram[SER_RX_WRITE + 1] << 8] = (uint8_t)*s;
        if (++v->ram[SER_RX_WRITE] == 0) {
            uint8_t page = (uint8_t)(v->ram[SER_RX_WRITE + 1] + 1);
            v->ram[SER_RX_WRITE + 1] = page == 0x39 ? 0x21 : page;
        }
    }
    v->ram[SER_RX_IDLE] &= (uint8_t)~1;
}

/* Words for the long clauses (44 characters) */
#define WORDS44 "the quick brown fox jumps over the lazy dog "

const extra_run diff_extra_runs[] = {
    /* @=1: the main bank, its version equal: the warm entry $FFD4, the rest of the clause dropped */
    { "warm", "@=1 One. Two.", 0, 0, 0, NULL, AT_IDLE, 0, BANK_ENTRY_WARM, false, ENGINE_IDLE },
    /* @=1 twice: the second from under the C bank_entry_warm, which enters $FFD4 again (its C run afresh) */
    { "warm_again", "@=1 One. @=1 Two. Three.", 0, 0, 0, NULL, AT_IDLE, 0, BANK_ENTRY_WARM, false, ENGINE_IDLE },
    /* @=1 with the version altered at the end of boot: the cold entry $FFD0 (boot_read_dip, boot_init_settings) */
    { "cold", "@=1 One. Two.", 0, 0, 0, inj_cold, AT_IDLE, 0, BANK_ENTRY_COLD, false, ENGINE_IDLE },
    /* DIP bit 1: bank_select reads the alternative table at boot (bank_scan) and at @=5 (bank_enter) */
    { "dip_bank", "@=5 Five.", P4_DIP_ALT, 0, 0, NULL, AT_IDLE, 0, BANK_ENTER, true, ENGINE_IDLE },
    /* the self-test failing: DIP bit 1 and the P2.1 input high; the three long buzzes at boot ($A7AB) */
    { "selftest_fail", "Hello.", P4_DIP_ALT, P2_SELFTEST, 0, NULL, AT_IDLE, 0, 0xA7AB, false, ENGINE_IDLE },
    /* a status byte due and the host line high at the end of boot: after the empty clause of the CR, boot_get_clause
       sends it ($A6D7: a NAK, no reply pending), a pass that changed state, then idles. (A longer input cannot follow:
       the busy line P0.5 that ser_tx_service sets after the send is not the level the machine delivers bytes at.) */
    { "status_sent", "", 0, 0, 0, inj_status_line_high, AT_IDLE, 0, 0xA6D7, false, ENGINE_IDLE },
    /* a query (@L, its reply queued) with a status byte due from the speech's second frame event on and the host line
       low: the reply waits on the host line; par_play's ser_tx_service and then boot_get_clause's ($A6D7) send
       nothing, and boot_get_clause loops with nothing changed until the speaking phase's deadline */
    { "status_waits", "@L One.", 0, 0, 0, inj_status_due, AT_SPEECH, 1, 0xA6D7, false, ENGINE_CYCLE_LIMIT },
    /* parallel mode: isr_idle returns at once ($C603 by its BBS0), no idle; the boot announcement, then the deadline */
    { "parallel", "Hello.", P4_DIP_PARALLEL, 0, 0, NULL, AT_IDLE, 0, 0xC603, false, ENGINE_CYCLE_LIMIT },
    /* the M4.6c2 plan's Review Focus 1: no punctuation and no CR inside, longer than a clause: cut at X >= $DC on a
       space ($A731 CMP #$20 reached); one word of 260 letters, cut at X >= $FA; and cmd_char at $DC (after the
       self-test's space at boot_clause[1] and 218 characters), carried in $1827 into the next clause ($A739) */
    { "rf1_space", WORDS44 WORDS44 WORDS44 WORDS44 WORDS44 WORDS44, 0, 0, 0, NULL, AT_IDLE, 0, 0xA731, false,
      ENGINE_IDLE },
    { "rf1_full", "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij"
                  "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij"
                  "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij",
      0, 0, 0, NULL, AT_IDLE, 0, 0xA731, false, ENGINE_IDLE },
    { "rf1_carry", WORDS44 WORDS44 WORDS44 WORDS44 "the quick brown fox jumps over the lazy do@W5 now slower.", 0, 0, 0,
      NULL, AT_IDLE, 0, 0xA739, false, ENGINE_IDLE },
    /* Review Focus 2: several sentences in one input, each punctuation the end of a clause ($A704), spoken in order
       with no idle between */
    { "rf2_sentences", "One. Two? Three!", 0, 0, 0, NULL, AT_IDLE, 0, 0xA704, false, ENGINE_IDLE },
    /* Review Focus 3: an empty input, the CR alone: the clause "   " (boot_clause[0], the self-test's space,
       the CR's), then idle again */
    { "rf3_empty", "", 0, 0, 0, NULL, AT_IDLE, 0, 0xA704, false, ENGINE_IDLE },
    /* Review Focus 4: commands only (cmd_set_speed $A9C9 run), nothing spoken */
    { "rf4_commands", "@W5 @A3", 0, 0, 0, NULL, AT_IDLE, 0, 0xA9C9, false, ENGINE_IDLE },
    /* Review Focus 5: "Two." arrives in the ring during the speech of "One.": taken after it with no wait */
    { "rf5_during", "One.", 0, 0, 0, inj_ring_two, AT_SPEECH, 5, 0xA6E4, false, ENGINE_IDLE },
    /* the baud jumpers (the branch probe): P3.1 ($AC9D: UART_BAUD $BF), P3.2 ($ACB0: $2F), both ($ACC3: $05) */
    { "baud_p31", "Hello.", 0, 0, 0, NULL, AT_IDLE, 0, 0xAC9D, false, ENGINE_IDLE, P3_BAUD_300 },
    { "baud_p32", "Hello.", 0, 0, 0, NULL, AT_IDLE, 0, 0xACB0, false, ENGINE_IDLE, P3_BAUD_1200 },
    { "baud_both", "Hello.", 0, 0, 0, NULL, AT_IDLE, 0, 0xACC3, false, ENGINE_IDLE, P3_BAUD_300 | P3_BAUD_1200 },
    /* the host line high at boot (the branch probe): boot_init_host_lines' CLB5 P0 path ($AC72), isr_host_line_high;
       the status byte that leaves due is sent before the end of boot. The CR alone after it: the busy line P0.5 that
       ser_tx_service sets is then the level the machine delivers bytes at, and the byte's interrupt clears it. */
    { "host_line_high", "", 0, 0, P0_HOST_LINE, NULL, AT_IDLE, 0, 0xAC72, false, ENGINE_IDLE },
};
const size_t diff_extra_run_count = sizeof diff_extra_runs / sizeof diff_extra_runs[0];
```

**`tests/test_diff_main.c`**: replace

```c
   The runs: every corpus text, and extra runs whose configuration (the P4 DIP switches, the P2 and P0 input pins) or
   injection (made to both engines at the same event: an idle or a frame event, before the snapshot is taken) reaches
   what no corpus text does; each names a ROM address its ROM run must execute (a hook on the ROM engine only), so that
   every extra run is seen to reach its path. Parallel mode (DIP bit 0) never idles: the run ends at the boot phase's
   deadline in both engines, after the boot announcement's frame events. */
```

with

```c
   The runs: every corpus text, and the extra runs (diff_runs.c, shared with diff_native) whose configuration (the P4
   DIP switches, the P2 and P0 input pins) or injection (made to both engines at the same event: an idle or a frame
   event, before the snapshot is taken) reaches what no corpus text does; each names a ROM address its ROM run must
   execute (a hook on the ROM engine only), so that every extra run is seen to reach its path. Parallel mode (DIP bit
   0) never idles: the run ends at the boot phase's deadline in both engines, after the boot announcement's frame
   events. */
```

**`tests/test_diff_main.c`**: replace

```c
#include "diff_state.h"
#include "engine/hybrid_engine.h"
#include "engine/thunks.h"
#include "firmware/bank_addr.h"

#define P4_DIP_PARALLEL  0x01   /* the P4 input's DIP bit 0: the parallel interface */
#define P4_DIP_ALT       0x02   /* DIP bit 1: the alternative bank table and the self-test input */
#define P2_SELFTEST      0x02   /* P2.1: the self-test input (boot_selftest $C60B BBC1 P2) */
#define P0_HOST_LINE     0x04   /* P0.2: the host's handshake line */
#define P3_BAUD_300      0x02   /* P3.1: a baud jumper (boot_init_baud $AC81 BBS1 P3) */
#define P3_BAUD_1200     0x04   /* P3.2: the other ($AC84, $AC9A BBS2 P3) */
```

with

```c
#include "diff_state.h"
#include "diff_runs.h"
#include "engine/hybrid_engine.h"
#include "engine/thunks.h"
```

**`tests/test_diff_main.c`**: replace

```c
typedef enum { AT_IDLE, AT_SPEECH } inject_at;   /* the n-th idle; the n-th frame event after the first idle */

/* An extra run: its input, its configuration, an injection at the n-th idle or frame event (inject NULL: none), the
   ROM address its ROM run must execute and the status both engines must end with. */
typedef struct {
    const char *name, *text;
    uint8_t p4, p2, p0;               /* ORed into the default input pins of P4, P2, P0 */
    void (*inject)(machine *m);
    inject_at at;
    unsigned n;
    uint16_t target;
    bool target_ramcode;
    machine_status want;
    uint8_t p3;                       /* ORed into the default input pins of P3 (the baud jumpers) */
} extra_run;

static machine g_m;
```

with

```c
static machine g_m;
```

**`tests/test_diff_main.c`**: replace

```c
    if (g_extra && g_extra->inject && (g_extra->at == AT_IDLE) == idle && g_extra->n == at)
        g_extra->inject(m);
```

with

```c
    if (g_extra && g_extra->inject && (g_extra->at == AT_IDLE) == idle && g_extra->n == at) {
        diff_view v = { m->ram, &m->sfr, m->queue, &m->q_head, &m->q_tail };
        g_extra->inject(&v);
    }
```

**`tests/test_diff_main.c`**: replace (the injections and the extra runs, now in `diff_runs.c`)

```c
/* ---- the extra runs' injections ---- */

/* At the end of boot: the running engine's minor version altered, so @=1 finds the bank's header different: the
   cold entry (bank_enter $0228). */
static void inj_cold(machine *m) { m->ram[BANK_VER_MINOR]++; }
/* A status byte due (as isr_host_line_high leaves it, $C3F0) with the host line low, as at boot; or with the line
   raised too (the pin only: the machine raises no INT4). */
static void inj_status_due(machine *m) { m->ram[SER_STATUS_DUE] = 1; }
static void inj_status_line_high(machine *m)
{
    m->ram[SER_STATUS_DUE] = 1;
    m->sfr.port_in[0] |= P0_HOST_LINE;
}
/* At a frame event of the speech: "Two." arrives in the input ring, each byte put as ser_rx_put puts it (the machine
   delivers serial bytes at the idle WIT only, never while speech plays). */
static void inj_ring_two(machine *m)
{
    for (const char *s = "Two."; *s; s++) {
        m->ram[m->ram[SER_RX_WRITE] | m->ram[SER_RX_WRITE + 1] << 8] = (uint8_t)*s;
        if (++m->ram[SER_RX_WRITE] == 0) {
            uint8_t page = (uint8_t)(m->ram[SER_RX_WRITE + 1] + 1);
            m->ram[SER_RX_WRITE + 1] = page == 0x39 ? 0x21 : page;
        }
    }
    m->ram[SER_RX_IDLE] &= (uint8_t)~1;
}

/* Words for the long clauses (44 characters) */
#define WORDS44 "the quick brown fox jumps over the lazy dog "

static const extra_run k_extra[] = {
    /* @=1: the main bank, its version equal: the warm entry $FFD4, the rest of the clause dropped */
    { "warm", "@=1 One. Two.", 0, 0, 0, NULL, AT_IDLE, 0, BANK_ENTRY_WARM, false, MACHINE_IDLE },
    /* @=1 twice: the second from under the C bank_entry_warm, which enters $FFD4 again (its C run afresh) */
    { "warm_again", "@=1 One. @=1 Two. Three.", 0, 0, 0, NULL, AT_IDLE, 0, BANK_ENTRY_WARM, false, MACHINE_IDLE },
    /* @=1 with the version altered at the end of boot: the cold entry $FFD0 (boot_read_dip, boot_init_settings) */
    { "cold", "@=1 One. Two.", 0, 0, 0, inj_cold, AT_IDLE, 0, BANK_ENTRY_COLD, false, MACHINE_IDLE },
    /* DIP bit 1: bank_select reads the alternative table at boot (bank_scan) and at @=5 (bank_enter) */
    { "dip_bank", "@=5 Five.", P4_DIP_ALT, 0, 0, NULL, AT_IDLE, 0, BANK_ENTER, true, MACHINE_IDLE },
    /* the self-test failing: DIP bit 1 and the P2.1 input high; the three long buzzes at boot ($A7AB) */
    { "selftest_fail", "Hello.", P4_DIP_ALT, P2_SELFTEST, 0, NULL, AT_IDLE, 0, 0xA7AB, false, MACHINE_IDLE },
    /* a status byte due and the host line high at the end of boot: after the empty clause of the CR, boot_get_clause
       sends it ($A6D7: a NAK, no reply pending), a pass that changed state, then idles. (A longer input cannot follow:
       the busy line P0.5 that ser_tx_service sets after the send is not the level the machine delivers bytes at.) */
    { "status_sent", "", 0, 0, 0, inj_status_line_high, AT_IDLE, 0, 0xA6D7, false, MACHINE_IDLE },
    /* a query (@L, its reply queued) with a status byte due from the speech's second frame event on and the host line
       low: the reply waits on the host line; par_play's ser_tx_service and then boot_get_clause's ($A6D7) send
       nothing, and boot_get_clause loops with nothing changed until the speaking phase's deadline */
    { "status_waits", "@L One.", 0, 0, 0, inj_status_due, AT_SPEECH, 1, 0xA6D7, false, MACHINE_CYCLE_LIMIT },
    /* parallel mode: isr_idle returns at once ($C603 by its BBS0), no idle; the boot announcement, then the deadline */
    { "parallel", "Hello.", P4_DIP_PARALLEL, 0, 0, NULL, AT_IDLE, 0, 0xC603, false, MACHINE_CYCLE_LIMIT },
    /* the M4.6c2 plan's Review Focus 1: no punctuation and no CR inside, longer than a clause: cut at X >= $DC on a
       space ($A731 CMP #$20 reached); one word of 260 letters, cut at X >= $FA; and cmd_char at $DC (after the
       self-test's space at boot_clause[1] and 218 characters), carried in $1827 into the next clause ($A739) */
    { "rf1_space", WORDS44 WORDS44 WORDS44 WORDS44 WORDS44 WORDS44, 0, 0, 0, NULL, AT_IDLE, 0, 0xA731, false,
      MACHINE_IDLE },
    { "rf1_full", "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij"
                  "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij"
                  "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij",
      0, 0, 0, NULL, AT_IDLE, 0, 0xA731, false, MACHINE_IDLE },
    { "rf1_carry", WORDS44 WORDS44 WORDS44 WORDS44 "the quick brown fox jumps over the lazy do@W5 now slower.", 0, 0, 0,
      NULL, AT_IDLE, 0, 0xA739, false, MACHINE_IDLE },
    /* Review Focus 2: several sentences in one input, each punctuation the end of a clause ($A704), spoken in order
       with no idle between */
    { "rf2_sentences", "One. Two? Three!", 0, 0, 0, NULL, AT_IDLE, 0, 0xA704, false, MACHINE_IDLE },
    /* Review Focus 3: an empty input, the CR alone: the clause "   " (boot_clause[0], the self-test's space,
       the CR's), then idle again */
    { "rf3_empty", "", 0, 0, 0, NULL, AT_IDLE, 0, 0xA704, false, MACHINE_IDLE },
    /* Review Focus 4: commands only (cmd_set_speed $A9C9 run), nothing spoken */
    { "rf4_commands", "@W5 @A3", 0, 0, 0, NULL, AT_IDLE, 0, 0xA9C9, false, MACHINE_IDLE },
    /* Review Focus 5: "Two." arrives in the ring during the speech of "One.": taken after it with no wait */
    { "rf5_during", "One.", 0, 0, 0, inj_ring_two, AT_SPEECH, 5, 0xA6E4, false, MACHINE_IDLE },
    /* the baud jumpers (the branch probe): P3.1 ($AC9D: UART_BAUD $BF), P3.2 ($ACB0: $2F), both ($ACC3: $05) */
    { "baud_p31", "Hello.", 0, 0, 0, NULL, AT_IDLE, 0, 0xAC9D, false, MACHINE_IDLE, P3_BAUD_300 },
    { "baud_p32", "Hello.", 0, 0, 0, NULL, AT_IDLE, 0, 0xACB0, false, MACHINE_IDLE, P3_BAUD_1200 },
    { "baud_both", "Hello.", 0, 0, 0, NULL, AT_IDLE, 0, 0xACC3, false, MACHINE_IDLE, P3_BAUD_300 | P3_BAUD_1200 },
    /* the host line high at boot (the branch probe): boot_init_host_lines' CLB5 P0 path ($AC72), isr_host_line_high;
       the status byte that leaves due is sent before the end of boot. The CR alone after it: the busy line P0.5 that
       ser_tx_service sets is then the level the machine delivers bytes at, and the byte's interrupt clears it. */
    { "host_line_high", "", 0, 0, P0_HOST_LINE, NULL, AT_IDLE, 0, 0xAC72, false, MACHINE_IDLE },
};

static const char *const k_corpus[] = { CORPUS_NAMES };
```

with

```c
static const char *const k_corpus[] = { CORPUS_NAMES };
```

**`tests/test_diff_main.c`**: replace

```c
    for (size_t i = 0; i < sizeof k_extra / sizeof k_extra[0]; i++)
        run_pair(rom, len, k_extra[i].name, k_extra[i].text, &k_extra[i]);
```

with

```c
    for (size_t i = 0; i < diff_extra_run_count; i++)
        run_pair(rom, len, diff_extra_runs[i].name, diff_extra_runs[i].text, &diff_extra_runs[i]);
```

**Create `tests/test_diff_native.c`:**

```c
/* The native engine against the all-C hybrid (M5a spec 4.3), state for state and cycle for cycle. Each run goes
   through the hybrid engine first, whose watch records each event in order (an input idle or a frame event, the
   injection due at it made first) and a snapshot: its kind; all RAM but the SFR bytes and the stack page $0100-$01FF
   (the native board pushes no return frames: the C never reads them); the whole m50734 state (memcmp of the struct,
   the first differing field named); the cycle count; the DSP outputs (the port writes, the raw position and bytes,
   the reset pulses, the partial bytes they dropped and the reset level); frames_produced and raw_frames; the I flag;
   the serial queue (its bytes, head and tail) and the busy level (known, and its value). The same input then goes
   through the native engine, whose watch makes the same injection and compares its k-th event with the hybrid's k-th:
   the same kind and the same snapshot; the first difference ends the comparison of that run. At the end both have the
   same status, the same number of events and the same final snapshot (one more event), and the native engine made as
   many bank entries as the hybrid unwound C call chains.
   The runs: every corpus text, the extra runs diff_main makes (diff_runs.c: their configurations and injections; their
   ROM targets are diff_main's to check, as the all-C hybrid runs none of those addresses as ROM), and the Review
   Focus runs of the M5a plan: the longest text the CLI accepts (1500 characters, no full stop), and bank commands with
   re-entries. No run here reaches the NUL bug's fallback (no corpus file holds a NUL), where the two engines end
   differently by design (suite_native pins the native end). Needs the ROM for the hybrid. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "diff_runs.h"
#include "engine/hybrid_engine.h"
#include "engine/native_engine.h"

/* One event of a run. */
typedef struct {
    bool idle, frame;                 /* an input idle, a frame event, or neither: the end of the run */
    uint8_t ram[BOARD_RAM_SIZE];
    m50734 sfr;
    uint64_t cycles;
    uint64_t port_writes, dsp_resets, raw_discarded, frames_produced, raw_frames;
    unsigned raw_pos;
    uint8_t raw_buf[BOARD_FRAME_BYTES];
    bool dsp_reset_level, i;
    uint8_t queue[BOARD_QUEUE_SIZE];
    size_t q_head, q_tail;
    bool busy_level_known;
    uint8_t busy_idle_level;
} snapshot;

/* A run of this suite alone: its input, the status both engines end with and the bank entries the native engine
   makes. */
typedef struct {
    const char *name, *text;
    engine_status want;
    unsigned entries;
} focus_run;

static machine g_m;
static native_engine g_e;
static snapshot *g_hyb;               /* the hybrid run's events: g_nhyb of them, room for g_cap */
static size_t g_nhyb, g_cap;
static snapshot g_cur, g_end;         /* the native event being compared; the hybrid's end */
static const extra_run *g_extra;      /* the run's injection, or NULL */
static unsigned g_idles, g_frames, g_speech_frames;
static size_t g_nevents;
static bool g_differs;
static char g_why[240];
static unsigned g_mismatches, g_runs, g_total_idles, g_total_frames;

static void take_machine(snapshot *s, const machine *m, bool idle, bool frame)
{
    s->idle = idle;
    s->frame = frame;
    memcpy(s->ram, m->ram, sizeof s->ram);
    s->sfr = m->sfr;
    s->cycles = m->cpu.cycles;
    s->port_writes = m->port_writes;
    s->dsp_resets = m->dsp_resets;
    s->raw_discarded = m->raw_discarded;
    s->frames_produced = m->frames_produced;
    s->raw_frames = m->raw_frames;
    s->raw_pos = m->raw_pos;
    memcpy(s->raw_buf, m->raw_buf, sizeof s->raw_buf);
    s->dsp_reset_level = m->dsp_reset_level;
    s->i = (m->cpu.p & CPU740_I) != 0;
    memcpy(s->queue, m->queue, sizeof s->queue);
    s->q_head = m->q_head;
    s->q_tail = m->q_tail;
    s->busy_level_known = m->busy_level_known;
    s->busy_idle_level = m->busy_idle_level;
}

static void take_board(snapshot *s, const board *b, bool idle, bool frame)
{
    s->idle = idle;
    s->frame = frame;
    memcpy(s->ram, b->ram, sizeof s->ram);
    s->sfr = b->sfr;
    s->cycles = b->cycles;
    s->port_writes = b->port_writes;
    s->dsp_resets = b->dsp_resets;
    s->raw_discarded = b->raw_discarded;
    s->frames_produced = b->frames_produced;
    s->raw_frames = b->raw_frames;
    s->raw_pos = b->raw_pos;
    memcpy(s->raw_buf, b->raw_buf, sizeof s->raw_buf);
    s->dsp_reset_level = b->dsp_reset_level;
    s->i = b->i;
    memcpy(s->queue, b->queue, sizeof s->queue);
    s->q_head = b->q_head;
    s->q_tail = b->q_tail;
    s->busy_level_known = b->busy_level_known;
    s->busy_idle_level = b->busy_idle_level;
}

/* Every field of m50734, to name the first one that differs. */
#define SFR_FIELD(f) { #f, offsetof(m50734, f), sizeof ((m50734 *)0)->f }
static const struct { const char *name; size_t off, size; } k_sfr_fields[] = {
    SFR_FIELD(port_latch), SFR_FIELD(port_dir), SFR_FIELD(port_in), SFR_FIELD(p0_func), SFR_FIELD(p23_func),
    SFR_FIELD(uart_baud), SFR_FIELD(ad_ctrl), SFR_FIELD(icr1), SFR_FIELD(icr2), SFR_FIELD(icr3),
    SFR_FIELD(timer_pre), SFR_FIELD(timer_reload), SFR_FIELD(timer_count), SFR_FIELD(timer_running),
    SFR_FIELD(uart_ctrl), SFR_FIELD(uart_rx), SFR_FIELD(uart_tx_last), SFR_FIELD(uart_rx_full),
    SFR_FIELD(uart_tx_count), SFR_FIELD(other),
};
#undef SFR_FIELD

/* The first difference between the hybrid's snapshot a and the native one b, or false. */
static bool differs(const snapshot *a, const snapshot *b, char *why, size_t n)
{
    if (a->idle != b->idle || a->frame != b->frame) {
        snprintf(why, n, "event kind: hybrid %s, native %s", a->idle ? "idle" : a->frame ? "frame" : "end",
                 b->idle ? "idle" : b->frame ? "frame" : "end");
        return true;
    }
    if (a->cycles != b->cycles) {
        snprintf(why, n, "cycles: hybrid %llu, native %llu", (unsigned long long)a->cycles,
                 (unsigned long long)b->cycles);
        return true;
    }
    if (a->i != b->i) {
        snprintf(why, n, "I: hybrid %d, native %d", a->i, b->i);
        return true;
    }
    if (memcmp(&a->sfr, &b->sfr, sizeof a->sfr) != 0) {
        const char *field = "padding";
        for (size_t k = 0; k < sizeof k_sfr_fields / sizeof k_sfr_fields[0]; k++) {
            if (memcmp((const uint8_t *)&a->sfr + k_sfr_fields[k].off, (const uint8_t *)&b->sfr + k_sfr_fields[k].off,
                       k_sfr_fields[k].size) != 0) {
                field = k_sfr_fields[k].name;
                break;
            }
        }
        snprintf(why, n, "m50734 %s", field);
        return true;
    }
    if (a->port_writes != b->port_writes || a->raw_pos != b->raw_pos ||
        memcmp(a->raw_buf, b->raw_buf, sizeof a->raw_buf) != 0 || a->dsp_resets != b->dsp_resets ||
        a->raw_discarded != b->raw_discarded || a->dsp_reset_level != b->dsp_reset_level) {
        snprintf(why, n, "DSP port writes/raw position/resets: hybrid %llu/%u/%llu, native %llu/%u/%llu (or the raw "
                 "bytes, the dropped bytes or the reset level)", (unsigned long long)a->port_writes, a->raw_pos,
                 (unsigned long long)a->dsp_resets, (unsigned long long)b->port_writes, b->raw_pos,
                 (unsigned long long)b->dsp_resets);
        return true;
    }
    if (a->frames_produced != b->frames_produced || a->raw_frames != b->raw_frames) {
        snprintf(why, n, "frames produced/raw: hybrid %llu/%llu, native %llu/%llu",
                 (unsigned long long)a->frames_produced, (unsigned long long)a->raw_frames,
                 (unsigned long long)b->frames_produced, (unsigned long long)b->raw_frames);
        return true;
    }
    if (a->q_head != b->q_head || a->q_tail != b->q_tail || memcmp(a->queue, b->queue, sizeof a->queue) != 0 ||
        a->busy_level_known != b->busy_level_known || a->busy_idle_level != b->busy_idle_level) {
        snprintf(why, n, "serial queue head/tail: hybrid %zu/%zu, native %zu/%zu (or its bytes or the busy level)",
                 a->q_head, a->q_tail, b->q_head, b->q_tail);
        return true;
    }
    for (uint32_t addr = 0; addr < BOARD_RAM_SIZE; addr++) {
        if ((addr >= M50734_SFR_FIRST && addr <= 0xFF) || (addr >= 0x100 && addr <= 0x1FF))
            continue;
        if (a->ram[addr] != b->ram[addr]) {
            snprintf(why, n, "RAM $%04X: hybrid %02X, native %02X", (unsigned)addr, a->ram[addr], b->ram[addr]);
            return true;
        }
    }
    return false;
}

/* The injection due at this event (the k-th idle, or the n-th frame event after the first idle). */
static void inject(bool idle, unsigned k, uint8_t *ram, m50734 *sfr, uint8_t *queue, size_t *q_head, size_t *q_tail)
{
    unsigned at = idle ? k : g_idles ? g_speech_frames++ : ~0u;
    if (g_extra && g_extra->inject && (g_extra->at == AT_IDLE) == idle && g_extra->n == at) {
        diff_view v = { ram, sfr, queue, q_head, q_tail };
        g_extra->inject(&v);
    }
}

static void hybrid_event(machine *m, bool idle)
{
    unsigned k = idle ? g_idles++ : g_frames++;
    inject(idle, k, m->ram, &m->sfr, m->queue, &m->q_head, &m->q_tail);
    if (g_nhyb == g_cap) {
        g_cap = g_cap ? g_cap * 2 : 1024;
        g_hyb = realloc(g_hyb, g_cap * sizeof *g_hyb);
        if (!g_hyb)
            abort();
    }
    take_machine(&g_hyb[g_nhyb++], m, idle, !idle);
}

static void native_event(board *b, bool idle)
{
    unsigned k = idle ? g_idles++ : g_frames++;
    inject(idle, k, b->ram, &b->sfr, b->queue, &b->q_head, &b->q_tail);
    size_t i = g_nevents++;
    if (g_differs)
        return;
    take_board(&g_cur, b, idle, !idle);
    char why[200];
    if (i >= g_nhyb) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "event %zu: native has more events than the hybrid (%zu)", i, g_nhyb);
    } else if (differs(&g_hyb[i], &g_cur, why, sizeof why)) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "event %zu (%s %u): %s", i, idle ? "idle" : "frame", k, why);
    }
}

static void hybrid_frame(void *user, machine *m)
{
    (void)user;
    hybrid_event(m, false);
}

static void hybrid_idle(void *user, machine *m)
{
    (void)user;
    hybrid_event(m, true);
}

static void native_frame(void *user, board *b)
{
    (void)user;
    native_event(b, false);
}

static void native_idle(void *user, board *b)
{
    (void)user;
    native_event(b, true);
}

static const char *status_name(int st)
{
    return st == ENGINE_IDLE ? "idle" : st == ENGINE_CYCLE_LIMIT ? "cycle limit" : "fault";
}

/* One input on both engines; x: the extra run whose configuration and injection it has (NULL: none); label: the line
   to print for it (NULL: a corpus text, nothing printed). Returns the native status. */
static engine_status run_pair(const uint8_t *rom, size_t len, const char *name, const char *text, const extra_run *x,
                              const char *label)
{
    machine_config cfg;
    native_config ncfg;
    machine_default_config(&cfg);
    native_default_config(&ncfg);
    if (x) {
        const uint8_t or_in[5] = { x->p0, 0, x->p2, x->p3, x->p4 };
        for (int p = 0; p < 5; p++) {
            cfg.port_in[p] |= or_in[p];
            ncfg.port_in[p] |= or_in[p];
        }
    }
    engine_input in = { (const uint8_t *)text, strlen(text), true };
    engine_watch hw = { hybrid_frame, hybrid_idle, NULL };
    native_watch nw = { native_frame, native_idle, NULL };
    hybrid_stats stats;
    g_extra = x;

    g_nhyb = 0;
    g_idles = g_frames = g_speech_frames = 0;
    machine_status h_st = hybrid_engine_run_input(&g_m, rom, len, &cfg, NULL, &in, false, NULL, NULL, &stats, &hw);
    take_machine(&g_end, &g_m, false, false);   /* the end of the run, one more event */
    unsigned h_idles = g_idles, h_frames = g_frames;

    g_nevents = 0;
    g_idles = g_frames = g_speech_frames = 0;
    g_differs = false;
    engine_status n_st = native_engine_run_input(&g_e, &ncfg, &in, false, NULL, NULL, &nw);
    char why[200];
    if (!g_differs && (int)n_st != (int)h_st) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "status: hybrid %s, native %s (fault %d at $%04X)", status_name((int)h_st),
                 status_name((int)n_st), (int)g_e.b.fault, g_e.b.fault_addr);
    }
    if (!g_differs && g_nevents != g_nhyb) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "events: hybrid %zu, native %zu", g_nhyb, g_nevents);
    }
    take_board(&g_cur, &g_e.b, false, false);
    if (!g_differs && differs(&g_end, &g_cur, why, sizeof why)) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "the end: %s", why);
    }
    if (!g_differs && g_e.unwinds != stats.unwinds) {
        g_differs = true;
        snprintf(g_why, sizeof g_why, "bank entries: hybrid %u, native %u", stats.unwinds, g_e.unwinds);
    }
    if (g_differs && g_mismatches++ < 10)
        fprintf(stderr, "  %s: %s\n", name, g_why);
    g_runs++;
    g_total_idles += h_idles;
    g_total_frames += h_frames;
    if (label)
        printf("  %-16s %s, %u idles, %u frame events, %u bank entries, %llu cycles\n", label,
               status_name((int)n_st), h_idles, h_frames, g_e.unwinds, (unsigned long long)g_e.b.cycles);
    return n_st;
}

static const char *const k_corpus[] = { CORPUS_NAMES };

#define WORDS44 "the quick brown fox jumps over the lazy dog "
#define WORDS440 WORDS44 WORDS44 WORDS44 WORDS44 WORDS44 WORDS44 WORDS44 WORDS44 WORDS44 WORDS44

/* The M5a plan's Review Focus runs. */
static const focus_run k_focus_runs[] = {
    /* Review Focus 2: the longest text the CLI accepts, 1500 characters with no full stop (clauses cut at their
       length), close to the speaking phase's budget */
    { "rf2_longest", WORDS440 WORDS440 WORDS440 WORDS44 WORDS44 WORDS44 WORDS44 "dogs", ENGINE_IDLE, 0 },
    /* Review Focus 4: bank commands with re-entries: @=1 enters the main bank's warm entry (twice, the second from
       under the C bank_entry_warm, its C run afresh); @=5 is an empty socket; then every other bank number */
    { "rf4_reentries", "@=1 One. @=5 Two. @=1 Three.", ENGINE_IDLE, 2 },
    { "rf4_banks", "@=0 @=8 @=9 @=F @=G", ENGINE_IDLE, 1 },
};

static void strip_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

void suite_diff_native(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    g_mismatches = g_runs = g_total_idles = g_total_frames = 0;
    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char path[128];
        snprintf(path, sizeof path, "tests/corpus/%s.txt", k_corpus[c]);
        char *text = test_read_file(path);
        CHECK(text != NULL);
        if (!text)
            continue;
        strip_newline(text);
        CHECK_EQ(run_pair(rom, len, k_corpus[c], text, NULL, NULL), ENGINE_IDLE);
        free(text);
    }
    printf("  corpus: %u texts, %u idles, %u frame events compared\n", g_runs, g_total_idles, g_total_frames);
    for (size_t i = 0; i < diff_extra_run_count; i++) {
        const extra_run *x = &diff_extra_runs[i];
        CHECK_EQ(run_pair(rom, len, x->name, x->text, x, x->name), x->want);
    }
    for (size_t i = 0; i < sizeof k_focus_runs / sizeof k_focus_runs[0]; i++) {
        const focus_run *r = &k_focus_runs[i];
        CHECK_EQ(run_pair(rom, len, r->name, r->text, NULL, r->name), r->want);
        CHECK_EQ(g_e.unwinds, r->entries);
    }
    CHECK_EQ(strlen(k_focus_runs[0].text), 1500);
    printf("  %u runs, %u idles, %u frame events compared in all\n", g_runs, g_total_idles, g_total_frames);
    CHECK_EQ(g_mismatches, 0);
    free(g_hyb);
    g_hyb = NULL;
    g_nhyb = g_cap = 0;
}
```

**`tests/suites.h`**: replace

```c
SUITE(suite_native)
```

with

```c
SUITE(suite_native)
SUITE(suite_diff_native)
```

- [ ] **Step 2: Run them**

Run: `cmd //c ".\build.bat test diff_native"`, then `time build/apollo2_tests.exe diff_native` and `build/apollo2_tests.exe diff_main`.
Expected: the build passes with no warnings, and

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
suite_diff_native            ok
1 suite(s), 0 failure(s), 0 skip(s)
```

`time` prints the same lines, then `real` about 11 s (`0m10.597s` in the proof run). `diff_main` prints its lines exactly as at 69c6038:

```
  corpus: 47 texts, 94 idles, 25167 frame events compared
  warm           idle, 2 idles, 101 frame events, $FFD4 reached 1
  warm_again     idle, 2 idles, 106 frame events, $FFD4 reached 2
  cold           idle, 2 idles, 101 frame events, $FFD0 reached 1
  dip_bank       idle, 2 idles, 41 frame events, $0210 reached 1
  selftest_fail  idle, 2 idles, 365 frame events, $A7AB reached 1
  status_sent    idle, 2 idles, 41 frame events, $A6D7 reached 1
  status_waits   cycle limit, 1 idles, 100 frame events, $A6D7 reached 4531687
  parallel       cycle limit, 0 idles, 41 frame events, $C603 reached 806094
  rf1_space      idle, 2 idles, 1565 frame events, $A731 reached 3
  rf1_full       idle, 2 idles, 1675 frame events, $A731 reached 30
  rf1_carry      idle, 2 idles, 1370 frame events, $A739 reached 1
  rf2_sentences  idle, 2 idles, 220 frame events, $A704 reached 5
  rf3_empty      idle, 2 idles, 41 frame events, $A704 reached 2
  rf4_commands   idle, 2 idles, 52 frame events, $A9C9 reached 1
  rf5_during     idle, 2 idles, 161 frame events, $A6E4 reached 15
  baud_p31       idle, 2 idles, 115 frame events, $AC9D reached 1
  baud_p32       idle, 2 idles, 115 frame events, $ACB0 reached 1
  baud_both      idle, 2 idles, 115 frame events, $ACC3 reached 1
  host_line_high idle, 2 idles, 41 frame events, $AC72 reached 1
  66 runs, 129 idles, 31533 frame events compared in all
suite_diff_main              ok
1 suite(s), 0 failure(s), 0 skip(s)
```

A mismatch names the run, the event (its index, kind and count) and the first difference (the kind, the cycles, I, the `m50734` field, the DSP outputs, the frame counts, the queue or a RAM address). Find its root cause in the board or the host (Tasks 1–3) and mirror what the hybrid does there; never exclude the field or loosen the comparison.

- [ ] **Step 3: Run the full suite**

Run: `cmd //c ".\build.bat test"` (in the background)
Expected:

- `43 suite(s), 0 failure(s), 0 skip(s)`, no warnings (920 s in the proof run);
- `diff_native`: its lines as in Step 2, ending `69 runs, 135 idles, 40329 frame events compared in all`, `suite_diff_native ok`;
- every other line exactly as in Task 3 Step 5: `diff_main` `66 runs, 129 idles, 31533 frame events compared in all` with each extra run's line as before, `golden_hybrid` 132 modes, the four timer-scale lines, `firmware_data` 49104; every golden unchanged.

- [ ] **Step 4: Commit**

```bash
git add tests/diff_runs.h tests/diff_runs.c tests/test_diff_native.c tests/test_diff_main.c tests/suites.h
git commit -m "diff_native: the all-C hybrid against the native engine at every event of every diff_main run and the Review Focus 2 and 4 runs, nothing excluded but the SFR bytes and the stack page, the cycle count included; the extra runs and their injections shared with diff_main, whose output is unchanged (M5a 4.3)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 5: The goldens on native without the ROM (`suite_golden_native`), timing invariance on native, `render_text_native` with the part it shares with `render_text`, the render comparison and Review Focus 5; the no-ROM worktree run (M5a spec §3.7, §4.4–§4.6, §4.7 item 5)

**Facts checked / what was verified while proving the plan** (`tests/test_golden.c`, `tests/test_golden_hybrid.c`, `tests/test_render.c`, `src/render/render.c`, `src/apps/apollo2.c`, `tests/rom_fixture.c`; everything below was run):
- **The goldens** (`tests/golden/`, 48 files): the 47 corpus texts with no boot frames and `boot.frm` with them (its text empty), as `suite_golden` makes them. On native every one ends `ENGINE_IDLE` with no unmodelled access and byte-identical frames: 23,281 frames in all, in about a second, with no ROM file anywhere.
- **Timing invariance** (`test_timing_invariance`, inside `suite_golden`, which needs the ROM): at the scales 1/4 and 3/1 the native engine produces `hello.frm`'s 105 frames and spends exactly the hybrid's cycles at each scale (11,678,411 and 973,321), which the test checks.
- **The render path**: `render_text` takes its options, result, input prefix and synthesis from `render_native.h`, so the native path runs the same code: `render_input` builds the `@K1` / `@Rn` prefix, `render_synthesize` the median pitch centre and the samples (nothing for no frame), and both synthesize only at their engine's idle status. `render_options.engine` keeps its type and values; `render_text_native` ignores it. `apollo2.c` uses `render_default_options`, `render_text` and `render_result_free` and compiles unchanged.
- **The render comparison** (`suite_render`): all 47 corpus texts through `render_text` on the hybrid and through `render_text_native` give the same status, frame count, clipping and 2,328,100 samples; so do the raw stream (`RENDER_STREAM_RAW`, timing-dependent, of "Hello world.": native's cycles are the hybrid's) and Review Focus 5's inputs: voice b with `@R0` and with `@R7`, and a Ctrl-X (mute) in the middle of a text. "Hello world." renders natively with no ROM (105 frames, audible, no clipping) before the suite's ROM check.
- **The no-ROM run** (spec §4.6 criterion 2): `test_rom` reads `APOLLO2_ROM`, else `UK_V2.01_4.04_ROM.BIN` in the working directory; a worktree has neither once the variable is unset. Then `suite_board`, `suite_native` and `suite_golden_native` pass, `suite_render` passes its native part and skips, and every suite that needs the ROM skips (the counts in Step 6).

**Files:**
- Create: `tests/test_golden_native.c`, `src/render/render_native.h`, `src/render/render_native.c`
- Modify: `src/render/render.h`, `src/render/render.c`, `tests/test_golden.c`, `tests/test_render.c`, `tests/suites.h`, `build.bat`

**Interfaces:**
- Consumes: `native_engine_speak`, `native_default_config`, `native_config` (Task 3); `synth_*`, `params_f0_centre` (`synth/synth.h`); `hybrid_engine_speak`, `rom_engine_speak_streams`; `CORPUS_NAMES`.
- Produces:

```c
/* src/render/render_native.h (render.h includes it; no machine/ or cpu740/ header) */
typedef enum { RENDER_STREAM_PRODUCED, RENDER_STREAM_RAW } render_stream;
typedef enum { RENDER_ENGINE_ROM, RENDER_ENGINE_HYBRID } render_engine;
typedef struct { char voice; render_stream stream; render_engine engine; int prosody; synth_options synth; }
    render_options;
typedef struct { int16_t *samples; size_t count; size_t frames; uint64_t clipped; } render_result;
void render_default_options(render_options *opt);
char *render_input(const char *text, const render_options *opt);
void render_synthesize(const frame_list *fl, const render_options *opt, render_result *out);
engine_status render_text_native(const char *text, const render_options *opt, render_result *out);
void render_result_free(render_result *r);
/* tests/test_golden_native.c: suite_golden_native, the 44th suite */
```

No name collides: `grep -rn "render_native\|render_input\|render_synthesize\|render_text_native\|suite_golden_native" src tests` prints nothing before this task.

- [ ] **Step 1: Write the failing tests**

`suite_golden_native` is new; `test_timing_invariance` runs the native engine too; `suite_render` renders natively first (no ROM), then compares every corpus text and the Review Focus 5 inputs with the hybrid.

**Create `tests/test_golden_native.c`:**

```c
/* The native engine reproduces every golden with no ROM (M5a spec 4.4): each corpus text, and the boot golden with the
   boot frames, through native_engine_speak: ENGINE_IDLE, no unmodelled access and byte-identical frames. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "engine/native_engine.h"

static const char *const k_corpus[] = { CORPUS_NAMES };
static native_engine g_e;

static void strip_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

/* One golden: name's text (with the boot frames if boot) against tests/golden/<name>.frm. */
static void check(const char *name, const char *text, bool boot, size_t *frames)
{
    char path[128];
    snprintf(path, sizeof path, "tests/golden/%s.frm", name);
    char *want = test_read_file(path);
    CHECK(want != NULL);
    if (!want)
        return;
    frame_list fl = { 0 };
    engine_status st = native_engine_speak(&g_e, NULL, text, boot, &fl, NULL);
    if (st != ENGINE_IDLE)
        fprintf(stderr, "  native %s: status %d, fault %d at $%04X\n", name, (int)st, (int)g_e.b.fault,
                g_e.b.fault_addr);
    CHECK_EQ(st, ENGINE_IDLE);
    CHECK_EQ(g_e.b.unmodelled_count, 0);
    char *got = frame_list_hex(&fl);
    if (strcmp(got, want) != 0) {
        size_t line = 1;
        for (size_t i = 0; got[i] && got[i] == want[i]; i++)
            line += got[i] == '\n';
        fprintf(stderr, "  native %s: frames differ from the golden at frame %zu\n", name, line);
        g_test_failures++;
    }
    *frames += fl.count;
    free(got);
    free(want);
    frame_list_free(&fl);
}

void suite_golden_native(void)
{
    size_t frames = 0, files = 0;
    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char path[128];
        snprintf(path, sizeof path, "tests/corpus/%s.txt", k_corpus[c]);
        char *text = test_read_file(path);
        CHECK(text != NULL);
        if (!text)
            continue;
        strip_newline(text);
        check(k_corpus[c], text, false, &frames);
        files++;
        free(text);
    }
    check("boot", "", true, &frames);   /* the startup announcement */
    files++;
    printf("  %zu goldens, %zu frames reproduced on the native engine\n", files, frames);
}
```

**`tests/suites.h`**: replace

```c
SUITE(suite_diff_native)
```

with

```c
SUITE(suite_diff_native)
SUITE(suite_golden_native)
```

**`tests/test_golden.c`**: replace

```c
#include "engine/rom_engine.h"

static const char *const k_corpus[] = { CORPUS_NAMES };

static machine g_m;
```

with

```c
#include "engine/rom_engine.h"
#include "engine/native_engine.h"

static const char *const k_corpus[] = { CORPUS_NAMES };

static machine g_m;
static native_engine g_native;
```

**`tests/test_golden.c`**: replace

```c
   (M4.5), which take no emulated time, so it builds faster (only its isr_* and ser_* ROM calls take cycles); the
   produced frames must not change either way. */
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
            free(hex);
            frame_list_free(&fl);
        }
    }
    free(text);
    free(want);
}
```

with

```c
   (M4.5), which take no emulated time, so it builds faster (only its isr_* and ser_* ROM calls take cycles); the
   produced frames must not change either way. Then the native engine (M5a spec 4.5), which charges the hybrid's
   cycles: the same frames, and the hybrid's cycle count at each scale. */
static void test_timing_invariance(const uint8_t *rom, size_t len)
{
    static const uint32_t k_scale[][2] = { { 1, 4 }, { 3, 1 } };
    static const char *const k_engine[] = { "", "hybrid ", "native " };
    const size_t n = sizeof k_scale / sizeof k_scale[0];
    uint64_t hybrid_cycles[sizeof k_scale / sizeof k_scale[0]] = { 0 };
    char *text = test_read_file("tests/corpus/hello.txt");
    char *want = test_read_file("tests/golden/hello.frm");
    CHECK(text != NULL);
    CHECK(want != NULL);
    if (text && want) {
        strip_newline(text);
        for (size_t j = 0; j < 3 * n; j++) {
            size_t i = j % n, engine = j / n;
            frame_list fl = { 0 };
            uint64_t cycles;
            unsigned unmodelled;
            if (engine < 2) {
                machine_config cfg;
                machine_default_config(&cfg);
                cfg.cycle_num = k_scale[i][0];
                cfg.cycle_den = k_scale[i][1];
                machine_status st = engine ? hybrid_engine_speak(&g_m, rom, len, &cfg, NULL, text, false, &fl, NULL,
                                                                 NULL)
                                           : rom_engine_speak_cfg(&g_m, rom, len, &cfg, text, false, &fl);
                if (st != MACHINE_IDLE)
                    machine_dump(&g_m, stderr);
                CHECK_EQ(st, MACHINE_IDLE);
                print_unmodelled(&g_m);
                cycles = g_m.cpu.cycles;
                unmodelled = g_m.unmodelled_count;
                if (engine == 1)
                    hybrid_cycles[i] = cycles;
            } else {
                native_config cfg;
                native_default_config(&cfg);
                cfg.cycle_num = k_scale[i][0];
                cfg.cycle_den = k_scale[i][1];
                CHECK_EQ(native_engine_speak(&g_native, &cfg, text, false, &fl, NULL), ENGINE_IDLE);
                cycles = g_native.b.cycles;
                unmodelled = g_native.b.unmodelled_count;
                CHECK_EQ(cycles, hybrid_cycles[i]);
            }
            CHECK_EQ(unmodelled, 0);
            char *hex = frame_list_hex(&fl);
            if (strcmp(hex, want) != 0) {
                fprintf(stderr, "  %stimer scale %u/%u:", k_engine[engine], (unsigned)k_scale[i][0],
                        (unsigned)k_scale[i][1]);
                report_diff("hello", hex, want);
                g_test_failures++;
            }
            printf("  %stimer scale %u/%u: %zu frames, %llu CPU cycles\n", k_engine[engine], (unsigned)k_scale[i][0],
                   (unsigned)k_scale[i][1], fl.count, (unsigned long long)cycles);
            free(hex);
            frame_list_free(&fl);
        }
    }
    free(text);
    free(want);
}
```

**`tests/test_render.c`**: replace

```c
#include "test.h"
#include "rom_fixture.h"
#include "render/render.h"

static machine g_m;
```

with

```c
#include "test.h"
#include "corpus_list.h"
#include "rom_fixture.h"
#include "render/render.h"
#include "render/render_native.h"

static machine g_m;
static const char *const k_corpus[] = { CORPUS_NAMES };
```

**`tests/test_render.c`**: replace

```c
void suite_render(void)
{
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom)
        SKIP("ROM not found (set APOLLO2_ROM)");
    render_options opt;
    render_result a = { 0 }, b = { 0 }, voice_b = { 0 }, raw = { 0 }, r3 = { 0 }, plain = { 0 };
    render_default_options(&opt);
```

with

```c
static void strip_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

/* text through render_text with the hybrid engine and through render_text_native (M5a spec 4.5): the same status,
   samples and clipping. Returns the samples compared. */
static size_t same_on_native(const uint8_t *rom, size_t len, const char *name, const char *text,
                             const render_options *opt)
{
    render_options hopt = *opt;
    render_result h = { 0 }, n = { 0 };
    hopt.engine = RENDER_ENGINE_HYBRID;
    machine_status hs = render_text(&g_m, rom, len, text, &hopt, &h);
    engine_status ns = render_text_native(text, opt, &n);
    bool same = (int)hs == (int)ns && h.count == n.count && h.frames == n.frames && h.clipped == n.clipped &&
                (h.count == 0 || !memcmp(h.samples, n.samples, h.count * sizeof h.samples[0]));
    if (!same) {
        fprintf(stderr, "  render %s: hybrid %d, %zu samples; native %d, %zu samples (or they differ)\n", name, (int)hs,
                h.count, (int)ns, n.count);
        g_test_failures++;
    }
    size_t count = h.count;
    render_result_free(&h);
    render_result_free(&n);
    return count;
}

/* Every corpus text, the raw stream, and the M5a plan's Review Focus 5: the second voice with the prosody extremes
   @R0 and @R7, and a mute (Ctrl-X) in the middle of a text. */
static void test_native(const uint8_t *rom, size_t len)
{
    render_options opt;
    size_t texts = 0, samples = 0;
    render_default_options(&opt);
    for (size_t c = 0; c < sizeof k_corpus / sizeof k_corpus[0]; c++) {
        char path[128];
        snprintf(path, sizeof path, "tests/corpus/%s.txt", k_corpus[c]);
        char *text = test_read_file(path);
        CHECK(text != NULL);
        if (!text)
            continue;
        strip_newline(text);
        samples += same_on_native(rom, len, k_corpus[c], text, &opt);
        texts++;
        free(text);
    }
    printf("  native: %zu corpus texts, %zu samples the same as the hybrid's\n", texts, samples);
    opt.stream = RENDER_STREAM_RAW;
    CHECK(same_on_native(rom, len, "raw", "Hello world.", &opt) > 0);
    render_default_options(&opt);
    opt.voice = 'b';
    for (int r = 0; r <= 7; r += 7) {
        opt.prosody = r;
        CHECK(same_on_native(rom, len, r ? "b @R7" : "b @R0", "Is this the second voice? It is.", &opt) > 0);
    }
    render_default_options(&opt);
    CHECK(same_on_native(rom, len, "mute", "One two three four \030 five six seven.", &opt) > 0);
}

void suite_render(void)
{
    render_options opt;
    render_result nat = { 0 };
    render_default_options(&opt);
    CHECK_EQ(render_text_native("Hello world.", &opt, &nat), ENGINE_IDLE);   /* no ROM needed */
    CHECK_EQ(nat.frames, 105);                               /* tests/golden/hello.frm */
    CHECK_EQ(nat.count, 105 * SYNTH_FRAME_SAMPLES);
    CHECK_EQ(nat.clipped, 0);
    CHECK(peak_of(&nat) > 1000);
    size_t len;
    const uint8_t *rom = test_rom(&len);
    if (!rom) {
        render_result_free(&nat);
        SKIP("ROM not found (set APOLLO2_ROM)");
    }
    render_result a = { 0 }, b = { 0 }, voice_b = { 0 }, raw = { 0 }, r3 = { 0 }, plain = { 0 };
```

**`tests/test_render.c`**: replace

```c
    render_result_free(&a);
    render_result_free(&b);
    render_result_free(&voice_b);
```

with

```c
    CHECK(a.count == nat.count && !memcmp(a.samples, nat.samples, a.count * sizeof a.samples[0]));
    test_native(rom, len);
    render_result_free(&nat);
    render_result_free(&a);
    render_result_free(&b);
    render_result_free(&voice_b);
```

- [ ] **Step 2: Run them to see them fail**

Run: `cmd //c ".\build.bat test golden_native"`
Expected: the build stops with compile errors (the proof run, first lines):

```
tests\test_render.c(7): fatal error C1083: Cannot open include file: 'render/render_native.h': No such file or directory
```

- [ ] **Step 3: Implement**

**Create `src/render/render_native.h`:**

```c
#ifndef RENDER_RENDER_NATIVE_H
#define RENDER_RENDER_NATIVE_H
/* text -> native engine -> frame stream -> synthesizer -> 10 kHz samples (M5a spec 3.7), and the part render_text
   (render.h) shares: the options, the result, the input with its prefix and the synthesis. No machine/ or cpu740/
   header. */
#include <stddef.h>
#include <stdint.h>
#include "engine/frame_list.h"
#include "synth/synth.h"

typedef enum {
    RENDER_STREAM_PRODUCED,   /* the golden, timing-independent stream (default) */
    RENDER_STREAM_RAW,        /* what the DSP received: one frame late, CPU-timing dependent */
} render_stream;

typedef enum {
    RENDER_ENGINE_ROM,        /* the original firmware, emulated (default) */
    RENDER_ENGINE_HYBRID,     /* the same, with the ported routines running as C (M4) */
} render_engine;

typedef struct {
    char voice;               /* 'a' (default) or 'b': speaker table B, selected with @K1 */
    render_stream stream;
    render_engine engine;     /* render_text's choice; render_text_native ignores it */
    int prosody;              /* 0-7: sent as @Rn before the text (default 4, the firmware's unscaled
                                 contour; the device default is 3); -1 = send nothing */
    synth_options synth;
} render_options;

typedef struct {
    int16_t *samples;         /* malloc'ed: frames * SYNTH_FRAME_SAMPLES samples at SYNTH_RATE */
    size_t count;
    size_t frames;
    uint64_t clipped;
} render_result;

void render_default_options(render_options *opt);

/* The text an engine gets: prefixed with @K1 for voice b and @Rn for the prosody. Caller frees. */
char *render_input(const char *text, const render_options *opt);

/* Synthesizes fl into out (cleared first) with fl's median pitch as the pitch-range centre; nothing for no frame. */
void render_synthesize(const frame_list *fl, const render_options *opt, render_result *out);

/* render_text on the native engine: the same input, the chosen stream synthesized only at ENGINE_IDLE (never a
   partial stream). */
engine_status render_text_native(const char *text, const render_options *opt, render_result *out);

void render_result_free(render_result *r);

#endif
```

**Create `src/render/render_native.c`:**

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render/render_native.h"
#include "engine/native_engine.h"

void render_default_options(render_options *opt)
{
    memset(opt, 0, sizeof *opt);
    opt->voice = 'a';
    opt->stream = RENDER_STREAM_PRODUCED;
    opt->prosody = 4;
    synth_default_options(&opt->synth);
}

void render_result_free(render_result *r)
{
    free(r->samples);
    memset(r, 0, sizeof *r);
}

char *render_input(const char *text, const render_options *opt)
{
    char prefix[16];   /* at most "@K1 @R7 " */
    int plen = snprintf(prefix, sizeof prefix, "%s", opt->voice == 'b' ? "@K1 " : "");   /* manual §6 @Kb */
    if (opt->prosody >= 0 && opt->prosody <= 7)                                       /* manual §6 @Ro */
        plen += snprintf(prefix + plen, sizeof prefix - (size_t)plen, "@R%d ", opt->prosody);
    size_t n = strlen(text);
    char *input = malloc((size_t)plen + n + 1);
    if (!input)
        abort();
    memcpy(input, prefix, (size_t)plen);
    memcpy(input + plen, text, n + 1);
    return input;
}

void render_synthesize(const frame_list *fl, const render_options *opt, render_result *out)
{
    memset(out, 0, sizeof *out);
    if (fl->count == 0)
        return;
    synth s;
    synth_options so = opt->synth;
    so.f0_centre_hz = params_f0_centre(&fl->frames[0][0], fl->count);   /* pitch-range centre */
    synth_init(&s, &so);
    out->samples = malloc(fl->count * SYNTH_FRAME_SAMPLES * sizeof out->samples[0]);
    if (!out->samples)
        abort();
    for (size_t i = 0; i < fl->count; i++)
        synth_frame(&s, fl->frames[i], out->samples + i * SYNTH_FRAME_SAMPLES);
    out->frames = fl->count;
    out->count = fl->count * SYNTH_FRAME_SAMPLES;
    out->clipped = s.clipped;
}

engine_status render_text_native(const char *text, const render_options *opt, render_result *out)
{
    native_engine *e = malloc(sizeof *e);   /* each call its own engine: nothing carries over */
    if (!e)
        abort();
    char *input = render_input(text, opt);
    frame_list produced = { 0 }, raw = { 0 };
    bool want_raw = opt->stream == RENDER_STREAM_RAW;
    engine_status st = native_engine_speak(e, NULL, input, false, &produced, want_raw ? &raw : NULL);
    memset(out, 0, sizeof *out);
    if (st == ENGINE_IDLE)
        render_synthesize(want_raw ? &raw : &produced, opt, out);
    frame_list_free(&produced);
    frame_list_free(&raw);
    free(input);
    free(e);
    return st;
}
```

**`src/render/render.h`**: replace the whole file

```c
#ifndef RENDER_RENDER_H
#define RENDER_RENDER_H
/* text -> firmware engine -> frame stream -> synthesizer -> 10 kHz samples (spec §3). */
#include <stddef.h>
#include <stdint.h>
#include "engine/hybrid_engine.h"
#include "engine/rom_engine.h"
#include "synth/synth.h"

typedef enum {
    RENDER_STREAM_PRODUCED,   /* the golden, timing-independent stream (default) */
    RENDER_STREAM_RAW,        /* what the DSP received: one frame late, CPU-timing dependent */
} render_stream;

typedef enum {
    RENDER_ENGINE_ROM,        /* the original firmware, emulated (default) */
    RENDER_ENGINE_HYBRID,     /* the same, with the ported routines running as C (M4) */
} render_engine;

typedef struct {
    char voice;               /* 'a' (default) or 'b': speaker table B, selected with @K1 */
    render_stream stream;
    render_engine engine;
    int prosody;              /* 0-7: sent as @Rn before the text (default 4, the firmware's unscaled
                                 contour; the device default is 3); -1 = send nothing */
    synth_options synth;
} render_options;

typedef struct {
    int16_t *samples;         /* malloc'ed: frames * SYNTH_FRAME_SAMPLES samples at SYNTH_RATE */
    size_t count;
    size_t frames;
    uint64_t clipped;
} render_result;

void render_default_options(render_options *opt);

/* Runs text through the chosen engine (prefixed with @K1 for voice b and @Rn for the prosody) and
   synthesizes the chosen stream, with the stream's median pitch as the pitch-range centre. Samples
   are produced only when the engine reaches MACHINE_IDLE (spec §4: never a partial stream). */
machine_status render_text(machine *m, const uint8_t *rom, size_t len, const char *text,
                           const render_options *opt, render_result *out);

void render_result_free(render_result *r);

#endif
```

with

```c
#ifndef RENDER_RENDER_H
#define RENDER_RENDER_H
/* text -> firmware engine -> frame stream -> synthesizer -> 10 kHz samples (spec §3). The options, the result and the
   shared input and synthesis are render_native.h's. */
#include <stddef.h>
#include <stdint.h>
#include "engine/hybrid_engine.h"
#include "engine/rom_engine.h"
#include "render/render_native.h"

/* Runs text through the chosen engine (prefixed with @K1 for voice b and @Rn for the prosody) and
   synthesizes the chosen stream, with the stream's median pitch as the pitch-range centre. Samples
   are produced only when the engine reaches MACHINE_IDLE (spec §4: never a partial stream). */
machine_status render_text(machine *m, const uint8_t *rom, size_t len, const char *text,
                           const render_options *opt, render_result *out);

#endif
```

**`src/render/render.c`**: replace the whole file

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render/render.h"

void render_default_options(render_options *opt)
{
    memset(opt, 0, sizeof *opt);
    opt->voice = 'a';
    opt->stream = RENDER_STREAM_PRODUCED;
    opt->prosody = 4;
    synth_default_options(&opt->synth);
}

void render_result_free(render_result *r)
{
    free(r->samples);
    memset(r, 0, sizeof *r);
}

machine_status render_text(machine *m, const uint8_t *rom, size_t len, const char *text,
                           const render_options *opt, render_result *out)
{
    char prefix[16];   /* at most "@K1 @R7 " */
    int plen = snprintf(prefix, sizeof prefix, "%s", opt->voice == 'b' ? "@K1 " : "");   /* manual §6 @Kb */
    if (opt->prosody >= 0 && opt->prosody <= 7)                                       /* manual §6 @Ro */
        plen += snprintf(prefix + plen, sizeof prefix - (size_t)plen, "@R%d ", opt->prosody);
    frame_list produced = { 0 }, raw = { 0 };
    size_t n = strlen(text);
    char *input = malloc((size_t)plen + n + 1);
    if (!input)
        abort();
    memcpy(input, prefix, (size_t)plen);
    memcpy(input + plen, text, n + 1);
    memset(out, 0, sizeof *out);
    frame_list *raw_out = opt->stream == RENDER_STREAM_RAW ? &raw : NULL;
    machine_status st = opt->engine == RENDER_ENGINE_HYBRID
        ? hybrid_engine_speak(m, rom, len, NULL, NULL, input, false, &produced, raw_out, NULL)
        : rom_engine_speak_streams(m, rom, len, NULL, input, &produced, raw_out);
    const frame_list *chosen = opt->stream == RENDER_STREAM_RAW ? &raw : &produced;
    if (st == MACHINE_IDLE && chosen->count > 0) {
        synth s;
        synth_options so = opt->synth;
        so.f0_centre_hz = params_f0_centre(&chosen->frames[0][0], chosen->count);   /* pitch-range centre */
        synth_init(&s, &so);
        out->samples = malloc(chosen->count * SYNTH_FRAME_SAMPLES * sizeof out->samples[0]);
        if (!out->samples)
            abort();
        for (size_t i = 0; i < chosen->count; i++)
            synth_frame(&s, chosen->frames[i], out->samples + i * SYNTH_FRAME_SAMPLES);
        out->frames = chosen->count;
        out->count = chosen->count * SYNTH_FRAME_SAMPLES;
        out->clipped = s.clipped;
    }
    frame_list_free(&produced);
    frame_list_free(&raw);
    free(input);
    return st;
}
```

with

```c
#include <stdlib.h>
#include <string.h>
#include "render/render.h"

machine_status render_text(machine *m, const uint8_t *rom, size_t len, const char *text,
                           const render_options *opt, render_result *out)
{
    char *input = render_input(text, opt);
    frame_list produced = { 0 }, raw = { 0 };
    memset(out, 0, sizeof *out);
    frame_list *raw_out = opt->stream == RENDER_STREAM_RAW ? &raw : NULL;
    machine_status st = opt->engine == RENDER_ENGINE_HYBRID
        ? hybrid_engine_speak(m, rom, len, NULL, NULL, input, false, &produced, raw_out, NULL)
        : rom_engine_speak_streams(m, rom, len, NULL, input, &produced, raw_out);
    if (st == MACHINE_IDLE)
        render_synthesize(opt->stream == RENDER_STREAM_RAW ? &raw : &produced, opt, out);
    frame_list_free(&produced);
    frame_list_free(&raw);
    free(input);
    return st;
}
```

**`build.bat`** (CRLF): replace

```bat
rem M5a spec 3.1: src\board and the native files build without the emulator too. Syntax-check them against an include
rem root holding only the headers they may use (src\board, src\firmware and the native headers of src\engine, copied
rem to build\noemu), so an include of a machine\ or cpu740\ header cannot be found.
if exist build\noemu rmdir /s /q build\noemu
for %%d in (board firmware\data engine) do mkdir build\noemu\%%d
for %%d in (board firmware firmware\data) do copy /y src\%%d\*.h build\noemu\%%d >nul
for %%f in (frame_list native_host native_engine) do copy /y src\engine\%%f.h build\noemu\engine >nul
cl /nologo /std:c11 /W4 /WX /wd4244 /Zs /D_CRT_SECURE_NO_WARNINGS /Ibuild\noemu src\board\*.c src\engine\frame_list.c ^
    src\engine\native_host.c src\engine\native_engine.c || exit /b 1
```

with

```bat
rem M5a spec 3.1: src\board and the native files build without the emulator too. Syntax-check them against an include
rem root holding only the headers they may use (src\board, src\firmware, src\synth and the native headers of src\engine
rem and src\render, copied to build\noemu), so an include of a machine\ or cpu740\ header cannot be found.
if exist build\noemu rmdir /s /q build\noemu
for %%d in (board firmware\data engine render synth) do mkdir build\noemu\%%d
for %%d in (board firmware firmware\data synth) do copy /y src\%%d\*.h build\noemu\%%d >nul
for %%f in (frame_list native_host native_engine) do copy /y src\engine\%%f.h build\noemu\engine >nul
copy /y src\render\render_native.h build\noemu\render >nul
cl /nologo /std:c11 /W4 /WX /wd4244 /Zs /D_CRT_SECURE_NO_WARNINGS /Ibuild\noemu src\board\*.c src\engine\frame_list.c ^
    src\engine\native_host.c src\engine\native_engine.c src\render\render_native.c || exit /b 1
```

- [ ] **Step 4: Run the tests**

Run: `cmd //c ".\build.bat test golden_native"`, then `build/apollo2_tests.exe render`.
Expected: the build passes with no warnings (the three syntax checks included), and

```
  48 goldens, 23281 frames reproduced on the native engine
suite_golden_native          ok
1 suite(s), 0 failure(s), 0 skip(s)
  native: 47 corpus texts, 2328100 samples the same as the hybrid's
suite_render                 ok
1 suite(s), 0 failure(s), 0 skip(s)
```

- [ ] **Step 5: Run the full suite**

Run: `cmd //c ".\build.bat test"` (in the background)
Expected:

- `44 suite(s), 0 failure(s), 0 skip(s)`, no warnings, the three syntax checks passing (950 s in the proof run);
- `golden_native`: `48 goldens, 23281 frames reproduced on the native engine`;
- `suite_golden`: `timer scale 1/4: 105 frames, 11789152 CPU cycles`, `timer scale 3/1: 105 frames, 1286784 CPU cycles`, `hybrid timer scale 1/4: 105 frames, 11678411 CPU cycles`, `hybrid timer scale 3/1: 105 frames, 973321 CPU cycles`, `native timer scale 1/4: 105 frames, 11678411 CPU cycles`, `native timer scale 3/1: 105 frames, 973321 CPU cycles`;
- `suite_render`: `native: 47 corpus texts, 2328100 samples the same as the hybrid's`;
- `diff_native` as in Task 4 (`69 runs, 135 idles, 40329 frame events compared in all`); `diff_main` (`66 runs, 129 idles, 31533 frame events compared in all`), `golden_hybrid` (`48 corpus files x 132 modes (all, then each routine alone)`), `firmware_data` (`49104 converted bytes compared with the ROM, 0 differ`), `diff_lts`, `diff_play`, `diff_serial` and the synth peaks (`corpus peak plain 20522 (-4.1 dBFS)`, `corpus peak b11 16038 (-6.2 dBFS)`) as in Task 4; every golden unchanged (`git status --short tests/golden` prints nothing).

- [ ] **Step 6: The no-ROM run**

In the worktree, with the ROM variable unset (no ROM file is there):

Run: `env -u APOLLO2_ROM cmd //c ".\build.bat test"` (in the background)
Expected:

- `44 suite(s), 0 failure(s), 18 skip(s)`, no warnings, the three syntax checks passing (20 s in the proof run, the build included);
- 30 suites run in full: `suite_smoke`, `suite_optable`, `suite_cpu_base`, `suite_cpu_740`, `suite_disasm`, `suite_vec740`, `suite_m50734`, `suite_machine`, `suite_coverage`, `suite_resonator`, `suite_source`, `suite_params`, `suite_biquad`, `suite_synth`, `suite_audio`, `suite_cpu_harte`, `suite_fw`, `suite_fw_lts`, `suite_fw_tn`, `suite_fw_cmd`, `suite_fw_pros`, `suite_fw_par`, `suite_fw_frm`, `suite_fw_ser`, `suite_fw_dsp`, `suite_fw_bank`, `suite_fw_boot`, `suite_board`, `suite_native` and `suite_golden_native` (`48 goldens, 23281 frames reproduced on the native engine`);
- 14 suites skip what needs the ROM (`SKIP: ROM not found (set APOLLO2_ROM)`), 18 skips in all: `suite_rom_boot`, `suite_rom_speak` (3), `suite_golden`, `suite_raw_stream`, `suite_render` (after its native part, which passes), `suite_rom_id`, `suite_machine_hooks` (3; its other tests pass), `suite_firmware_data` (the comparison with the ROM only: `converted tables not compared`), `suite_diff_lts`, `suite_diff_play`, `suite_diff_serial`, `suite_diff_main`, `suite_golden_hybrid` and `suite_diff_native`; each still prints `ok`.

Then `export APOLLO2_ROM=...` again, as the Global Constraints say, for the next task.

- [ ] **Step 7: Commit**

```bash
git add tests/test_golden_native.c tests/test_golden.c tests/test_render.c tests/suites.h src/render/render_native.h src/render/render_native.c src/render/render.h src/render/render.c build.bat
git commit -m "The goldens on native without the ROM (suite_golden_native), timing invariance on native (the hybrid's cycles at each scale), render_text_native with the render part it shares with render_text, the render comparison over every corpus text and the Review Focus 5 inputs; build.bat's check of render_native.c (M5a 3.7, 4.4, 4.5)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: `git status --short` prints only the user's untracked files.

---

### Task 6: Parent spec rev 19, M5a status and README (M5a spec §4.6 criterion 5)

**Files:**
- Modify: `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`, `docs/superpowers/specs/2026-10-13-m5a-native-host-design.md`, `README.md`

**Interfaces:** documentation only.

- [ ] **Step 1: Parent spec, rev 19**

Make these edits in `docs/superpowers/specs/2026-09-30-software-apollo2-design.md`. Mark each one *(rev 19)*. Find each place by its text, not by line number.

1. **Status line** (line 4). Insert after `**Status:** Approved. `:

   `*(rev 19)* Revision 19 records milestone 5a: the native host and engine, which run the C firmware with no ROM and no emulated CPU on a CPU-less board that is cycle-exact with the hybrid engine (design: docs/superpowers/specs/2026-10-13-m5a-native-host-design.md). `

2. **§8 milestone 5** (the item that begins `5. **Native:** every routine and every table is in committed C.`). After its last bullet (the two lines that begin `   - The emulator stays in` and end `and that requires the ROM.`), add one more bullet at the same indentation (three spaces, then `- `), as one line:

   `- *(rev 19)* M5 is two plans: M5a the native host and engine, M5b the emulator moved under tests/ and the CLI made native-only (the shipped apollo2 then has no --engine, --rom or --rom-any option; user decision 2026-10-13). **M5a done** <DATE>: the CPU-less board (src/board: the m50734 model, moved from src/machine, and board.c with the RAM, the cycle clock, the interrupt dispatch through the converted vector table, serial delivery at the idle point and the DSP outputs), the native host and the native engine run the firmware with no ROM; the board charges exactly the cycles the all-C hybrid's emulated CPU spends (the WIT, interrupt entry and the three wait loops); a NUL-bug handler address ends a native run as a firmware fault at $A893; the vector table is converted (49,104 bytes in all); the new suites board, native, diff_native (the hybrid against native at every event of every diff_main run: all RAM but the SFR bytes and the stack page, the whole m50734 state, the cycle count) and golden_native (every golden on native, no ROM needed), test_timing_invariance and the render comparison on native pass, and with no ROM every native suite passes while the ROM suites skip. Next: M5b.`

   Replace `<DATE>` with the date of your commit (`date +%F`).

3. **§6 step 3** (the paragraph containing `*(rev 18)* M4.6c2: FW_WAIT_INPUT is isr_idle's WIT at $C602`). Append at its end:

   ` *(rev 19)* M5a: the native host performs every host call without the emulator: the SFRs, the DSP port and the frame event on the CPU-less board; each wait on the board's own clock, with the hybrid's cycle charges (a WIT step of 4 cycles, an interrupt entry of 7 with the handler's C called through the converted vector, the loops at $C500, $B372 and $B37F instruction by instruction); read_bank reads $FF (empty sockets); enter makes the native engine run the bank entry's C afresh; rom_call fails (there is no ROM).`

- [ ] **Step 2: M5a spec status and As built**

In `docs/superpowers/specs/2026-10-13-m5a-native-host-design.md`, replace `**Status:** design, awaiting user review` with:

`**Status:** implemented (plan docs/superpowers/plans/2026-10-13-m5a-native-host.md)`

Then add a final section headed `## 6. As built`, after §5. Open it with the sentence `Where the code is more precise than, or differs from, the sections above.` Then write one short factual bullet per difference between the spec and what was built. Gather them from:
- the "Facts checked" lists at the top of Tasks 1–5 of the plan, keeping only those that contradict or sharpen the spec;
- the code:
  - the board's layout and API as built (`src/board/board.h`), the fields it mirrors from the machine and its constants;
  - each wait as built, with its cycle charges per step and per pass;
  - the dispatch table and the converted vector table (its names and its file);
  - the native host and engine as built (`native_host.h`, `native_engine.h`, `frame_list.h`), the status and fault values, and where the 4611 pragma sits;
  - the render path as built (`render_native.h`) and what it shares with `render_text`;
  - `build.bat`'s check as built (which files, which include paths);
  - `diff_native`'s runs, events, counts and run time, the shared run list and its injections as built, `diff_main`'s counts unchanged;
  - the counts of `golden_native`, `test_timing_invariance` and the render comparison;
  - the no-ROM run's summary line (suites passed and skipped);
  - the unit tests in `tests/test_board.c` and `tests/test_native.c` and what they cover;
  - the Review Focus tests and what each showed.

Every bullet must be true of the committed code. A claim about a test must say only what that test checks. Follow the style of §6 of `docs/superpowers/specs/2026-10-13-m4-6c2-boot-main-loop-design.md`. If nothing differs, add no section.

- [ ] **Step 3: README**

In `README.md`, replace:

```
A ROM whose MD5 is not the UK V2.01 4.04 image is refused by the CLIs unless `--rom-any` is given.

## Ghidra project (milestone 3)
```

with:

```
A ROM whose MD5 is not the UK V2.01 4.04 image is refused by the CLIs unless `--rom-any` is given.

## Native engine (milestone 5)

The native engine (src/engine/native_engine.c) runs the same C firmware with no ROM and no emulated CPU: a
CPU-less board (src/board) supplies the M50734's timers, UART and ports, the interrupts and the DSP port, and
charges exactly the cycles the hybrid engine's emulated CPU spends waiting, so the two agree state for state.
`build.bat test golden_native` reproduces every golden frame stream on it without the ROM, and
`build.bat test diff_native` runs the hybrid and the native engine side by side over every diff_main run,
comparing RAM, the peripheral state and the cycle count at every input idle and frame event. Until milestone 5b
the CLI still runs the emulated engines; 5b makes it native-only, with no ROM needed.

## Ghidra project (milestone 3)
```

- [ ] **Step 4: Run the full suite and commit**

Run: `cmd //c ".\build.bat test"` (in the background; it takes about 16 minutes)
Expected: `44 suite(s), 0 failure(s), 0 skip(s)`.

```bash
git add docs/superpowers/specs/2026-09-30-software-apollo2-design.md docs/superpowers/specs/2026-10-13-m5a-native-host-design.md README.md
git commit -m "Spec rev 19: milestone 5a done (the native host and engine, cycle-exact with the hybrid)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
