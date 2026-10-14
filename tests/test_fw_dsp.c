/* Unit checks of speech timing and the DSP (M4.6b spec 4.6): the host helpers it uses (fw_dsp_write, fw_frame_done,
   the wait kinds with their ROM loops, the P2 window rule), with a stub host that logs, in order, every SFR read and
   write, every wait (its kind and the ROM loop it stands for), every DSP port byte and every frame event. Each
   expected value is worked out from the dump of the routine (build/ghidra740/dump), as the comment beside it says. */
#include <string.h>
#include "test.h"
#include "engine/thunks.h"
#include "firmware/isr_dsp.h"
#include "firmware/isr_serial.h"
#include "firmware/data/fw_data.h"

#define MAX_LOG 64
#define SFR_P0  0xF6
#define SFR_ICR1 0xFF

typedef enum { EV_READ, EV_WRITE, EV_WAIT, EV_DSP, EV_FRAME } ev_kind;
/* One host call: EV_READ/EV_WRITE an SFR (addr) and its value; EV_WAIT the ROM loop (addr) and the kind (value);
   EV_DSP the byte (value) at FW_DSP_DATA; EV_FRAME at FW_FRAME_EVENT. */
typedef struct { ev_kind kind; uint16_t addr; uint8_t value; } event;

static uint8_t g_ram[0x10000];   /* the firmware's RAM is $0000-$3FFF; 64 KiB so no index can run off the array */
static fw g_f;
static fw_host g_host;
static uint8_t g_sfr[0x100];     /* the stub's SFRs: a read returns g_sfr[addr], a write sets it */
static event g_log[MAX_LOG];
static unsigned g_nlog;
static bool g_wait_ok;           /* what the stub's wait returns ... */
static unsigned g_waits_ok;      /* ... but with g_wait_ok false, the first g_waits_ok waits still return */
static unsigned g_iwaits;        /* FW_WAIT_INTERRUPT waits so far */
static uint8_t g_at_event[4];    /* isr_tick_div, isr_word_ofs, isr_words_left and isr_frame_flag at the frame event */
static unsigned g_flag_every;    /* the scripted timer interrupt sets isr_frame_flag bit 0 at every n-th (0: never) */

static void log_add(ev_kind kind, uint16_t addr, uint8_t value)
{
    if (g_nlog < MAX_LOG)
        g_log[g_nlog] = (event){ kind, addr, value };
    g_nlog++;
}

static uint8_t stub_read_sfr(void *ctx, uint8_t addr)
{
    (void)ctx;
    log_add(EV_READ, addr, g_sfr[addr]);
    return g_sfr[addr];
}

static void stub_write_sfr(void *ctx, uint8_t addr, uint8_t value)
{
    (void)ctx;
    g_sfr[addr] = value;
    log_add(EV_WRITE, addr, value);
}

static fw_wait_result stub_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    (void)ctx;
    log_add(EV_WAIT, at, (uint8_t)kind);
    bool ok = g_wait_ok;
    if (!ok && g_waits_ok) {
        g_waits_ok--;
        ok = true;
    }
    if (kind == FW_WAIT_INTERRUPT && ok && g_flag_every && ++g_iwaits % g_flag_every == 0)
        g_ram[ISR_FRAME_FLAG] |= 1;
    return ok ? FW_WAIT_RETURNED : FW_WAIT_FAILED;
}

static void stub_dsp_write(void *ctx, uint8_t byte)
{
    (void)ctx;
    log_add(EV_DSP, FW_DSP_DATA, byte);
}

static void stub_frame_done(void *ctx)
{
    (void)ctx;
    log_add(EV_FRAME, FW_FRAME_EVENT, 0);
    for (int i = 0; i < 4; i++)
        g_at_event[i] = g_ram[ISR_TICK_DIV + i];
}

/* Zeroed RAM and SFRs, the stub host (no ROM call, no window call: speech timing makes neither), nothing logged. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    memset(g_sfr, 0, sizeof g_sfr);
    memset(&g_host, 0, sizeof g_host);
    g_host.ctx = &g_f;
    g_host.read_sfr = stub_read_sfr;
    g_host.write_sfr = stub_write_sfr;
    g_host.wait = stub_wait;
    g_host.dsp_write = stub_dsp_write;
    g_host.frame_done = stub_frame_done;
    g_nlog = 0;
    g_wait_ok = true;
    g_waits_ok = 0;
    g_iwaits = 0;
    g_flag_every = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
}

/* The log is exactly the n events of want. */
static void check_log(const event *want, unsigned n)
{
    CHECK_EQ(g_nlog, n);
    for (unsigned i = 0; i < n && i < g_nlog && i < MAX_LOG; i++) {
        CHECK_EQ(g_log[i].kind, want[i].kind);
        CHECK_EQ(g_log[i].addr, want[i].addr);
        CHECK_EQ(g_log[i].value, want[i].value);
    }
}

/* fw_dsp_write: one host write of the byte; no host, or a host without dsp_write, is a fault at DSP_DATA $8000 and
   nothing is written. */
static void test_dsp_write(void)
{
    static const event want[] = { { EV_DSP, FW_DSP_DATA, 0x5A }, { EV_DSP, FW_DSP_DATA, 0xA5 } };
    setup();
    fw_dsp_write(&g_f, 0x5A);
    fw_dsp_write(&g_f, 0xA5);
    check_log(want, 2);
    CHECK(!g_f.fault);

    setup();
    g_host.dsp_write = NULL;
    fw_dsp_write(&g_f, 0x5A);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0x8000);
    CHECK_EQ(g_nlog, 0);

    fw f;
    fw_init(&f, g_ram, fw_image_get(), NULL);
    fw_dsp_write(&f, 0x5A);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0x8000);
}

/* fw_frame_done: one frame event; no host, or a host without frame_done, is a fault at $B306. */
static void test_frame_done(void)
{
    static const event want[] = { { EV_FRAME, FW_FRAME_EVENT, 0 } };
    setup();
    fw_frame_done(&g_f);
    check_log(want, 1);
    CHECK(!g_f.fault);

    setup();
    g_host.frame_done = NULL;
    fw_frame_done(&g_f);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xB306);
    CHECK_EQ(g_nlog, 0);

    fw f;
    fw_init(&f, g_ram, fw_image_get(), NULL);
    fw_frame_done(&f);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xB306);
}

/* fw_wait with the new kinds (M4.6b spec 3.2): the host gets the kind and the ROM loop; a wait that fails, a host
   without wait or no host is a fault at that loop, whatever the kind. */
static void test_wait_kinds(void)
{
    static const event want[] = {
        { EV_WAIT, 0xB465, FW_WAIT_INTERRUPT },
        { EV_WAIT, 0xB372, FW_WAIT_TIMER2 },
        { EV_WAIT, 0xB37F, FW_WAIT_TIMER3 },
    };
    setup();
    fw_wait(&g_f, FW_WAIT_INTERRUPT, 0xB465);
    fw_wait(&g_f, FW_WAIT_TIMER2, FW_WAIT_TIMER2_LOOP);
    fw_wait(&g_f, FW_WAIT_TIMER3, FW_WAIT_TIMER3_LOOP);
    check_log(want, 3);
    CHECK(!g_f.fault);

    setup();
    g_wait_ok = false;
    fw_wait(&g_f, FW_WAIT_INTERRUPT, 0xB877);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xB877);

    setup();
    g_wait_ok = false;
    fw_wait(&g_f, FW_WAIT_TIMER3, FW_WAIT_TIMER3_LOOP);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xB37F);

    setup();
    g_host.wait = NULL;
    fw_wait(&g_f, FW_WAIT_TIMER2, FW_WAIT_TIMER2_LOOP);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xB372);
    CHECK_EQ(g_nlog, 0);

    fw f;
    fw_init(&f, g_ram, fw_image_get(), NULL);
    fw_wait(&f, FW_WAIT_INTERRUPT, 0xB8C9);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xB8C9);
}

/* The P2 rule (M4.6b spec 3.4): every write of P2 through the SFR helpers sets the window cache from bit 7 of the
   value written (STA/LDM, and the write of SEB/CLB, whatever its bit); a write of another SFR, or a P2 write that
   faults, leaves it. */
static void test_p2_window(void)
{
    setup();
    fw_wr_sfr8(&g_f, SFR_P2, 0x80);
    CHECK(g_f.low_window);
    fw_wr_sfr8(&g_f, SFR_P2, 0x70);
    CHECK(!g_f.low_window);
    fw_sfr_seb(&g_f, SFR_P2, 7);                  /* reads $70, writes $F0 */
    CHECK(g_f.low_window);
    CHECK_EQ(g_sfr[SFR_P2], 0xF0);
    fw_sfr_seb(&g_f, SFR_P2, 0);                  /* $F1: bit 7 still set */
    CHECK(g_f.low_window);
    fw_sfr_clb(&g_f, SFR_P2, 0);                  /* $F0 */
    CHECK(g_f.low_window);
    fw_sfr_clb(&g_f, SFR_P2, 7);                  /* $70 */
    CHECK(!g_f.low_window);
    fw_wr_sfr8(&g_f, SFR_P3, 0xFF);               /* not P2 */
    fw_sfr_seb(&g_f, SFR_P3, 7);
    CHECK(!g_f.low_window);
    CHECK_EQ(g_nlog, 13);                         /* six P2 writes, four of them after a read; P3 three */
    CHECK(!g_f.fault);

    setup();
    g_f.low_window = true;
    g_host.write_sfr = NULL;                      /* the write faults: nothing written, the cache kept */
    fw_wr_sfr8(&g_f, SFR_P2, 0x00);
    CHECK(g_f.fault);
    CHECK(g_f.low_window);
}

/* The hook of name sits at addr, declares outputs, and is neither a waits nor an isr row (pinned here; diff_lts
   compares the outputs against the ROM). */
static void check_row(const char *name, uint16_t addr, uint8_t outputs)
{
    const fw_replacement *r = fw_replacement_find(name);
    CHECK(r != NULL);
    if (!r)
        return;
    CHECK_EQ(r->addr, addr);
    CHECK_EQ(r->outputs, outputs);
    CHECK(!r->waits);
    CHECK(!r->isr);
}

static void test_rows(void)
{
    const fw_replacement *t = fw_replacement_find("isr_timer_dsp");   /* an interrupt handler: no outputs */
    CHECK(t != NULL);
    if (t) {
        CHECK_EQ(t->addr, 0xB2D9);
        CHECK_EQ(t->outputs, 0);
        CHECK(!t->waits);
        CHECK(t->isr);
    }
    check_row("isr_set_volume", 0xACE8, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_volume_half", 0xB46C, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_par_ready", 0xC5D6, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_par_hold", 0xC5DE, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_timer_start", 0xB35D, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_dsp_reset", 0xB38B, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_speech_stop", 0xB398, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    static const char *const waits[] = { "isr_speech_start", "isr_wait_frames", "isr_wait_frame" };
    static const uint16_t addrs[] = { 0xB323, 0xB356, 0xB465 };
    for (unsigned i = 0; i < 3; i++) {          /* they wait on the timer interrupt: diff_play checks them */
        const fw_replacement *r = fw_replacement_find(waits[i]);
        CHECK(r != NULL);
        if (!r)
            continue;
        CHECK_EQ(r->addr, addrs[i]);
        CHECK_EQ(r->outputs, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
        CHECK(r->waits);
        CHECK(!r->isr);
    }
}

/* isr_set_volume $ACE8 at all 16 levels: $75 = ((A ^ $F) & $F) << 3 ($ACE8-$ACEF); P3 read once (AND P3) and written
   once with (P3 & $87) | $75 ($ACF1-$ACF7), which is A out; X and Y untouched. P3 = $A5: bits 7, 2 and 0 kept
   ($85), bits 3-6 replaced. A's high nibble does not count (AND #$F): $F3 is level 3. */
static void test_set_volume(void)
{
    for (unsigned v = 0; v < 17; v++) {
        uint8_t a = v < 16 ? (uint8_t)v : 0xF3;
        uint8_t level = (uint8_t)(((a ^ 0x0F) & 0x0F) << 3);
        uint8_t want = (uint8_t)(0x85 | level);
        const event log[] = { { EV_READ, SFR_P3, 0xA5 }, { EV_WRITE, SFR_P3, want } };
        setup();
        g_sfr[SFR_P3] = 0xA5;
        fw_regs r = { a, 0x11, 0x22, 0 };
        isr_set_volume(&g_f, &r);
        check_log(log, 2);
        CHECK_EQ(g_ram[0x75], level);
        CHECK_EQ(r.a, want);
        CHECK_EQ(r.x, 0x11);
        CHECK_EQ(r.y, 0x22);
        CHECK(!g_f.fault);
    }
    setup();
    g_sfr[SFR_P3] = 0xA5;
    fw_regs r = { 0, 0, 0, 0 };
    isr_set_volume(&g_f, &r);
    CHECK_EQ(r.a, 0xFD);                          /* silent: $85 | $78 */
    CHECK_EQ(g_sfr[SFR_P3], 0xFD);
}

/* isr_volume_half $B46C: isr_set_volume with A = cmd_volume >> 1 (LSR A): 11 gives 5, $FF gives $7F (level 0). */
static void test_volume_half(void)
{
    static const event log11[] = { { EV_READ, SFR_P3, 0x00 }, { EV_WRITE, SFR_P3, 0x50 } };   /* (5 ^ $F) << 3 */
    setup();
    g_ram[CMD_VOLUME] = 11;
    fw_regs r = { 0x99, 0x11, 0x22, 0 };
    isr_volume_half(&g_f, &r);
    check_log(log11, 2);
    CHECK_EQ(g_ram[0x75], 0x50);
    CHECK_EQ(r.a, 0x50);
    CHECK_EQ(r.x, 0x11);
    CHECK_EQ(r.y, 0x22);
    CHECK_EQ(g_ram[CMD_VOLUME], 11);

    static const event log_ff[] = { { EV_READ, SFR_P3, 0xFF }, { EV_WRITE, SFR_P3, 0x87 } };
    setup();
    g_ram[CMD_VOLUME] = 0xFF;
    g_sfr[SFR_P3] = 0xFF;
    r = (fw_regs){ 0, 0, 0, 0 };
    isr_volume_half(&g_f, &r);
    check_log(log_ff, 2);
    CHECK_EQ(r.a, 0x87);
    CHECK(!g_f.fault);
}

/* isr_par_ready $C5D6 and isr_par_hold $C5DE: in serial mode (boot_dip bit 0 clear; $FE has it clear) nothing but
   A, X and Y kept; in parallel mode isr_par_ready sets P0.3 (SEB3: P0 read, then written with bit 3) and
   isr_par_hold clears P0.3 and the _INT1 request ICR1 bit 1 (CLB3 P0, CLB1 ICR1: each a read then a write). */
static void test_par_handshake(void)
{
    static const uint8_t dips[2] = { 0x00, 0xFE };
    for (unsigned i = 0; i < 2; i++) {
        setup();
        g_ram[BOOT_DIP] = dips[i];
        g_sfr[SFR_P0] = 0x20;
        g_sfr[SFR_ICR1] = 0x03;
        fw_regs r = { 0x11, 0x22, 0x33, 0 };
        isr_par_ready(&g_f, &r);
        isr_par_hold(&g_f, &r);
        CHECK_EQ(g_nlog, 0);
        CHECK_EQ(r.a, 0x11);
        CHECK_EQ(r.x, 0x22);
        CHECK_EQ(r.y, 0x33);
    }

    static const event ready[] = { { EV_READ, SFR_P0, 0x20 }, { EV_WRITE, SFR_P0, 0x28 } };
    setup();
    g_ram[BOOT_DIP] = 1;
    g_sfr[SFR_P0] = 0x20;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_par_ready(&g_f, &r);
    check_log(ready, 2);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0x22);
    CHECK_EQ(r.y, 0x33);

    static const event hold[] = {
        { EV_READ, SFR_P0, 0x28 }, { EV_WRITE, SFR_P0, 0x20 }, { EV_READ, SFR_ICR1, 0x03 },
        { EV_WRITE, SFR_ICR1, 0x01 },
    };
    setup();
    g_ram[BOOT_DIP] = 1;
    g_sfr[SFR_P0] = 0x28;
    g_sfr[SFR_ICR1] = 0x03;
    isr_par_hold(&g_f, &r);
    check_log(hold, 4);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0x22);
    CHECK_EQ(r.y, 0x33);
    CHECK(!g_f.fault);
}

/* The SFR accesses of isr_dsp_reset $B38B from P2 = p2: CLB0, SEB0, CLB0 P2, each a read then a write. */
#define DSP_RESET_LOG(p2) \
    { EV_READ, SFR_P2, (p2) }, { EV_WRITE, SFR_P2, (uint8_t)((p2) & ~1) }, \
    { EV_READ, SFR_P2, (uint8_t)((p2) & ~1) }, { EV_WRITE, SFR_P2, (uint8_t)((p2) | 1) }, \
    { EV_READ, SFR_P2, (uint8_t)((p2) | 1) }, { EV_WRITE, SFR_P2, (uint8_t)((p2) & ~1) }

/* isr_dsp_reset $B38B: P2.0 low, high, low; the other P2 bits kept. P2 bit 7 (the lower-ROM window) is set and stays
   set, so the window cache stays on (M4.6b spec 3.4). A, X and Y untouched. */
static void test_dsp_reset(void)
{
    static const event log[] = { DSP_RESET_LOG(0xF2) };
    setup();
    g_sfr[SFR_P2] = 0xF2;
    g_f.low_window = true;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_dsp_reset(&g_f, &r);
    check_log(log, 6);
    CHECK(g_f.low_window);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0x22);
    CHECK_EQ(r.y, 0x33);
    CHECK(!g_f.fault);
}

/* The SFR accesses and waits of isr_timer_start $B35D ($B35F-$B387) from ICR2 = $3F and P3 = p3. */
#define TIMER_START_LOG(p3) \
    { EV_WRITE, SFR_T2_PRE, 0 }, { EV_WRITE, SFR_T3_PRE, 0 }, \
    { EV_READ, SFR_P3, (p3) }, { EV_WRITE, SFR_P3, (uint8_t)((p3) | 1) }, { EV_WRITE, SFR_T2_RELOAD, 4 }, \
    { EV_READ, SFR_ICR2, 0x3F }, { EV_WRITE, SFR_ICR2, 0x37 }, { EV_READ, SFR_ICR2, 0x37 }, \
    { EV_WRITE, SFR_ICR2, 0x33 }, { EV_WAIT, 0xB372, FW_WAIT_TIMER2 }, { EV_WRITE, SFR_T3_RELOAD, 0x28 }, \
    { EV_READ, SFR_ICR2, 0x33 }, { EV_WRITE, SFR_ICR2, 0x13 }, { EV_READ, SFR_ICR2, 0x13 }, \
    { EV_WRITE, SFR_ICR2, 0x03 }, { EV_WRITE, SFR_P23_FUNC, 1 }, { EV_WAIT, 0xB37F, FW_WAIT_TIMER3 }, \
    { EV_WRITE, SFR_T3_RELOAD, 0x31 }, { EV_WRITE, SFR_ICR2, 0x10 }

/* isr_timer_start $B35D: the whole SFR order around its two waits: the prescalers, isr_tick_div decremented twice
   (10 -> 8), P3.0 set, timer 2's reload 4 and its request and enable bits cleared (CLB3, CLB2), the wait for its
   request at $B372; timer 3's reload $28 and its request and enable bits cleared (CLB5, CLB4), P23_FUNC = 1, the
   wait at $B37F; then the reload $31 and ICR2 = $10. The stub keeps ICR2 as written: its request bits are the
   machine's (the host's loops end when they are set). A, X and Y untouched. A tick count of 1 wraps to $FF. */
static void test_timer_start(void)
{
    static const event log[] = { TIMER_START_LOG(0x80) };
    setup();
    g_sfr[SFR_ICR2] = 0x3F;
    g_sfr[SFR_P3] = 0x80;
    g_ram[ISR_TICK_DIV] = 10;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_timer_start(&g_f, &r);
    check_log(log, sizeof log / sizeof log[0]);
    CHECK_EQ(g_ram[ISR_TICK_DIV], 8);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0x22);
    CHECK_EQ(r.y, 0x33);
    CHECK(!g_f.fault);

    setup();
    g_ram[ISR_TICK_DIV] = 1;
    isr_timer_start(&g_f, &r);
    CHECK_EQ(g_ram[ISR_TICK_DIV], 0xFF);         /* 8-bit DEC */
}

/* isr_timer_start untrapped, a wait failing: a fault at that wait's loop and nothing after it, as at
   isr_wait_frame. Timer 2's wait failing: the log ends at the wait at $B372 (the first 10 events); timer 3's (the
   first wait returns): it ends at the wait at $B37F (the first 17). A, X and Y untouched. */
static void test_timer_start_fail(void)
{
    static const event log[] = { TIMER_START_LOG(0x80) };
    static const unsigned ends[2] = { 10, 17 };
    static const uint16_t loops[2] = { 0xB372, 0xB37F };
    for (unsigned i = 0; i < 2; i++) {
        setup();
        g_sfr[SFR_ICR2] = 0x3F;
        g_sfr[SFR_P3] = 0x80;
        g_wait_ok = false;
        g_waits_ok = i;
        fw_regs r = { 0x11, 0x22, 0x33, 0 };
        isr_timer_start(&g_f, &r);
        check_log(log, ends[i]);
        CHECK(g_f.fault);
        CHECK_EQ(g_f.fault_addr, loops[i]);
        CHECK_EQ(r.a, 0x11);
        CHECK_EQ(r.x, 0x22);
        CHECK_EQ(r.y, 0x33);
    }
}

/* isr_speech_stop $B398: P23_FUNC = 0, ICR2 = 0 (LDM: writes only), isr_dsp_reset, then boot_speaking bit 0
   cleared (the other bits kept). A, X and Y untouched. */
static void test_speech_stop(void)
{
    static const event log[] = { { EV_WRITE, SFR_P23_FUNC, 0 }, { EV_WRITE, SFR_ICR2, 0 }, DSP_RESET_LOG(0x71) };
    setup();
    g_sfr[SFR_P23_FUNC] = 1;
    g_sfr[SFR_ICR2] = 0x10;
    g_sfr[SFR_P2] = 0x71;
    g_ram[BOOT_SPEAKING] = 0xFF;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_speech_stop(&g_f, &r);
    check_log(log, sizeof log / sizeof log[0]);
    CHECK_EQ(g_ram[BOOT_SPEAKING], 0xFE);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0x22);
    CHECK_EQ(r.y, 0x33);
    CHECK(!g_f.fault);
}

/* isr_speech_start $B323: frm_source_index = cmd_source_prosody's bits 7-5 and frm_filter_index = its bit 4
   ($B5 = 101 1 0101: 5 and 1; the old values are cleared first); the ISR counters ($59 = $5B = 10, $5A = $5C = 0);
   isr_dsp_reset; isr_timer_start (isr_tick_div 10 -> 8); isr_wait_frames with X = 1: one wait at $B465, the flag
   coming with it, then cleared. A = 0 and X = 0 out, Y untouched. */
static void test_speech_start(void)
{
    static const event log[] = { DSP_RESET_LOG(0x70), TIMER_START_LOG(0x00), { EV_WAIT, 0xB465, FW_WAIT_INTERRUPT } };
    setup();
    g_sfr[SFR_ICR2] = 0x3F;
    g_sfr[SFR_P2] = 0x70;
    g_ram[CMD_SOURCE_PROSODY] = 0xB5;
    g_ram[FRM_SOURCE_INDEX] = 0xEE;
    g_ram[FRM_FILTER_INDEX] = 0xEE;
    g_ram[ISR_WORD_OFS] = 0x12;
    g_ram[ISR_FRAME_FLAG] = 0x81;               /* stale: cleared before the wait */
    g_flag_every = 1;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_speech_start(&g_f, &r);
    check_log(log, sizeof log / sizeof log[0]);
    CHECK_EQ(g_ram[FRM_SOURCE_INDEX], 5);
    CHECK_EQ(g_ram[FRM_FILTER_INDEX], 1);
    CHECK_EQ(g_ram[ISR_TICK_DIV], 8);
    CHECK_EQ(g_ram[ISR_WORDS_LEFT], 10);
    CHECK_EQ(g_ram[ISR_WORD_OFS], 0);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0);
    CHECK_EQ(r.a, 0);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x33);
    CHECK(!g_f.fault);
}

/* isr_wait_frame $B465 with a scripted timer interrupt: the flag already set (bit 0 of $03): no wait, the whole byte
   cleared; not set: a wait at $B465 per interrupt until the third brings it, then cleared (bit 1 alone, $02, does not
   count); a wait that fails: a fault at $B465 and no more waits. A, X and Y untouched. */
static void test_wait_frame(void)
{
    setup();
    g_ram[ISR_FRAME_FLAG] = 0x03;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_wait_frame(&g_f, &r);
    CHECK_EQ(g_nlog, 0);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0);

    static const event log[] = {
        { EV_WAIT, 0xB465, FW_WAIT_INTERRUPT }, { EV_WAIT, 0xB465, FW_WAIT_INTERRUPT },
        { EV_WAIT, 0xB465, FW_WAIT_INTERRUPT },
    };
    setup();
    g_ram[ISR_FRAME_FLAG] = 0x02;
    g_flag_every = 3;
    isr_wait_frame(&g_f, &r);
    check_log(log, 3);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0x22);
    CHECK_EQ(r.y, 0x33);
    CHECK(!g_f.fault);

    setup();
    g_wait_ok = false;
    isr_wait_frame(&g_f, &r);
    CHECK_EQ(g_nlog, 1);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xB465);
}

/* isr_wait_frames $B356 with X = 2: two frames, the DEX / BNE $B356 taken once, a branch no ROM caller takes (its
   one caller, isr_speech_start, passes X = 1), pinned here; X = 0 waits 256 frames (8-bit DEX). X = 0 out, A and Y
   untouched. */
static void test_wait_frames_two(void)
{
    setup();
    g_flag_every = 1;
    fw_regs r = { 0x11, 2, 0x33, 0 };
    isr_wait_frames(&g_f, &r);
    CHECK_EQ(g_iwaits, 2);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0x33);

    setup();
    g_flag_every = 1;
    r.x = 0;
    isr_wait_frames(&g_f, &r);
    CHECK_EQ(g_iwaits, 256);
    CHECK_EQ(r.x, 0);
    CHECK(!g_f.fault);
}

/* isr_timer_dsp's state: isr_tick_div, isr_word_ofs, isr_words_left, isr_frame_flag and isr_swap_phase; the
   ping-pong pointers at isr_frame_a (play) and isr_frame_b (fill); the 40 bytes of the two frames $16A5-$16CC =
   $40 + i; ICR2 with timer 3's request and enable bits ($30). */
static void timer_setup(uint8_t tick, uint8_t ofs, uint8_t words, uint8_t flag, uint8_t phase)
{
    setup();
    g_ram[ISR_TICK_DIV] = tick;
    g_ram[ISR_WORD_OFS] = ofs;
    g_ram[ISR_WORDS_LEFT] = words;
    g_ram[ISR_FRAME_FLAG] = flag;
    g_ram[ISR_SWAP_PHASE] = phase;
    fw_wr16(&g_f, ISR_PLAY_PTR, ISR_FRAME_A);
    fw_wr16(&g_f, ISR_FILL_PTR, ISR_FRAME_B);
    for (int i = 0; i < 40; i++)
        g_ram[ISR_FRAME_A + i] = (uint8_t)(0x40 + i);
    g_sfr[SFR_ICR2] = 0x30;
}

/* isr_timer_dsp $B2D9: A, X and Y as they came. */
static void run_timer(void)
{
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_timer_dsp(&g_f, &r);
    CHECK_EQ(r.a, 0x11);
    CHECK_EQ(r.x, 0x22);
    CHECK_EQ(r.y, 0x33);
    CHECK(!g_f.fault);
}

/* isr_timer_dsp: a tick not due (isr_tick_div 3 -> 2): CLB5 ICR2 (timer 3's request cleared), nothing else. A word
   due (isr_tick_div 1 -> 0, isr_words_left 5 -> 4): the word at isr_frame_a + 4 to the DSP port, even byte first,
   isr_word_ofs 4 -> 6, isr_tick_div = 10. isr_word_ofs $FE: Y wraps (INY, 8 bits): $FE, $FF, then 0. */
static void test_timer_word(void)
{
    static const event tick[] = { { EV_READ, SFR_ICR2, 0x30 }, { EV_WRITE, SFR_ICR2, 0x10 } };
    timer_setup(3, 4, 5, 0, 2);
    run_timer();
    check_log(tick, 2);
    CHECK_EQ(g_ram[ISR_TICK_DIV], 2);
    CHECK_EQ(g_ram[ISR_WORD_OFS], 4);
    CHECK_EQ(g_ram[ISR_WORDS_LEFT], 5);

    static const event word[] = {
        { EV_READ, SFR_ICR2, 0x30 }, { EV_WRITE, SFR_ICR2, 0x10 }, { EV_DSP, FW_DSP_DATA, 0x44 },
        { EV_DSP, FW_DSP_DATA, 0x45 },
    };
    timer_setup(1, 4, 5, 0, 2);
    run_timer();
    check_log(word, 4);
    CHECK_EQ(g_ram[ISR_TICK_DIV], 10);
    CHECK_EQ(g_ram[ISR_WORD_OFS], 6);
    CHECK_EQ(g_ram[ISR_WORDS_LEFT], 4);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0);

    timer_setup(1, 0xFE, 5, 0, 2);
    fw_wr16(&g_f, ISR_PLAY_PTR, 0x1600);
    g_ram[0x16FE] = 0x5A;
    g_ram[0x16FF] = 0xA5;
    run_timer();
    CHECK_EQ(g_log[2].value, 0x5A);
    CHECK_EQ(g_log[3].value, 0xA5);
    CHECK_EQ(g_ram[ISR_WORD_OFS], 0);

    static const event wrap[] = {
        { EV_READ, SFR_ICR2, 0x30 }, { EV_WRITE, SFR_ICR2, 0x10 }, { EV_DSP, FW_DSP_DATA, 0x5A },
        { EV_DSP, FW_DSP_DATA, 0xA5 },
    };
    timer_setup(1, 0xFF, 5, 0, 2);              /* isr_word_ofs $FF: the INY between the two reads wraps */
    fw_wr16(&g_f, ISR_PLAY_PTR, 0x1600);
    g_ram[0x16FF] = 0x5A;
    g_ram[0x1600] = 0xA5;                       /* the second byte from the pointer + 0, not + $100 */
    run_timer();
    check_log(wrap, 4);
    CHECK_EQ(g_ram[ISR_WORD_OFS], 1);
}

/* isr_timer_dsp at a frame boundary (the last word: isr_tick_div 1, isr_words_left 1, isr_word_ofs 18), the flag
   clear: the word, then the frame event ($B306) with the counters already reset (isr_tick_div and isr_words_left 10,
   isr_word_ofs 0) and the flag still clear, then isr_frame_flag = 1 and the swap. Phase 2 -> 1: isr_play_ptr's low
   byte $B9, isr_fill_ptr's $A5; phase 1 -> 0: $A5 and $B9, the phase 2 again. The high bytes are not written. */
static void test_timer_swap(void)
{
    static const event log[] = {
        { EV_READ, SFR_ICR2, 0x30 }, { EV_WRITE, SFR_ICR2, 0x10 }, { EV_DSP, FW_DSP_DATA, 0x52 },
        { EV_DSP, FW_DSP_DATA, 0x53 }, { EV_FRAME, FW_FRAME_EVENT, 0 },
    };
    static const uint8_t at_event[4] = { 10, 0, 10, 2 };
    timer_setup(1, 18, 1, 0x02, 2);             /* bit 0 clear, bit 1 set: LDM #1 writes the whole byte ($02 -> $01) */
    run_timer();
    check_log(log, 5);
    CHECK(memcmp(g_at_event, at_event, 4) == 0);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 1);
    CHECK_EQ(g_ram[ISR_SWAP_PHASE], 1);
    CHECK_EQ(fw_rd16(&g_f, ISR_PLAY_PTR), ISR_FRAME_B);
    CHECK_EQ(fw_rd16(&g_f, ISR_FILL_PTR), ISR_FRAME_A);

    timer_setup(1, 18, 1, 0, 1);
    g_ram[ISR_FILL_PTR + 1] = 0x55;             /* a high byte the LDMs leave */
    run_timer();
    check_log(log, 5);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 1);
    CHECK_EQ(g_ram[ISR_SWAP_PHASE], 2);
    CHECK_EQ(fw_rd16(&g_f, ISR_PLAY_PTR), ISR_FRAME_A);
    CHECK_EQ(g_ram[ISR_FILL_PTR], 0xB9);
    CHECK_EQ(g_ram[ISR_FILL_PTR + 1], 0x55);
}

/* isr_timer_dsp at a frame boundary with isr_frame_flag bit 0 still set ($03): the frame repeats: no frame event,
   the flag byte, the phase and the pointers unchanged; the counters reset as at any boundary. */
static void test_timer_repeat(void)
{
    static const event log[] = {
        { EV_READ, SFR_ICR2, 0x30 }, { EV_WRITE, SFR_ICR2, 0x10 }, { EV_DSP, FW_DSP_DATA, 0x52 },
        { EV_DSP, FW_DSP_DATA, 0x53 },
    };
    timer_setup(1, 18, 1, 0x03, 2);
    run_timer();
    check_log(log, 4);
    CHECK_EQ(g_ram[ISR_FRAME_FLAG], 0x03);
    CHECK_EQ(g_ram[ISR_SWAP_PHASE], 2);
    CHECK_EQ(fw_rd16(&g_f, ISR_PLAY_PTR), ISR_FRAME_A);
    CHECK_EQ(fw_rd16(&g_f, ISR_FILL_PTR), ISR_FRAME_B);
    CHECK_EQ(g_ram[ISR_TICK_DIV], 10);
    CHECK_EQ(g_ram[ISR_WORD_OFS], 0);
    CHECK_EQ(g_ram[ISR_WORDS_LEFT], 10);
}

void suite_fw_dsp(void)
{
    CHECK(fw_image_get() != NULL);
    test_dsp_write();
    test_frame_done();
    test_wait_kinds();
    test_p2_window();
    test_rows();
    test_set_volume();
    test_volume_half();
    test_par_handshake();
    test_dsp_reset();
    test_timer_start();
    test_timer_start_fail();
    test_speech_stop();
    test_speech_start();
    test_wait_frame();
    test_wait_frames_two();
    test_timer_word();
    test_timer_swap();
    test_timer_repeat();
}
