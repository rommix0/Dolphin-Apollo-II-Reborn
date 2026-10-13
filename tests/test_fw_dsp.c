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
static bool g_wait_ok;           /* what the stub's wait returns */

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

static bool stub_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    (void)ctx;
    log_add(EV_WAIT, at, (uint8_t)kind);
    return g_wait_ok;
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
    check_row("isr_set_volume", 0xACE8, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_volume_half", 0xB46C, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_par_ready", 0xC5D6, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
    check_row("isr_par_hold", 0xC5DE, FW_OUT_A | FW_OUT_X | FW_OUT_Y);
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
}
