/* Unit checks of boot, the main loop and isr_idle (M4.6c2 spec 4.5), with a stub host that logs, in order, every SFR
   read and write, every wait (its ROM loop and kind), the CPU set-up (reset_cpu) and every CLI or SEI
   (set_interrupts), and fails every ROM call (the C makes none). Each expected value is worked out from the dump of the
   routine (build/ghidra740/dump), as the comment beside it says. The full proof is the live differential tests
   (diff_main, diff_lts) and the hybrid goldens (golden_hybrid). */
#include <string.h>
#include "test.h"
#include "firmware/fw.h"
#include "firmware/data/fw_data.h"

#define MAX_LOG 512

typedef enum { EV_READ, EV_WRITE, EV_WAIT, EV_RESET, EV_INT } ev_kind;
/* One host call: EV_READ/EV_WRITE an SFR (addr) and its value; EV_WAIT the ROM loop (addr) and the kind (value);
   EV_RESET reset_cpu (addr FW_RESET_CPU); EV_INT set_interrupts, value 1 for CLI, 0 for SEI. */
typedef struct { ev_kind kind; uint16_t addr; uint8_t value; } event;

static uint8_t g_ram[0x10000];   /* the firmware's RAM is $0000-$3FFF; 64 KiB so no index can run off the array */
static fw g_f;
static fw_host g_host;
static uint8_t g_sfr[0x100];     /* the stub's SFRs: a read returns g_sfr[addr], a write sets it */
static event g_log[MAX_LOG];
static unsigned g_nlog, g_ncalls;
static fw_wait_result g_wait_result;   /* what the stub's wait returns */

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
    return g_wait_result;
}

static void stub_reset_cpu(void *ctx)
{
    (void)ctx;
    log_add(EV_RESET, FW_RESET_CPU, 0);
}

static void stub_set_interrupts(void *ctx, bool enabled)
{
    (void)ctx;
    log_add(EV_INT, 0, enabled);
}

/* Counts and fails every ROM call: the C makes none. */
static fw_call_result stub_rom_call(void *ctx, uint16_t addr, fw_regs *r, uint64_t budget)
{
    (void)ctx;
    (void)addr;
    (void)r;
    (void)budget;
    g_ncalls++;
    return FW_CALL_FAILED;
}

/* Zeroed RAM and SFRs, the stub host, the converted image, nothing logged, every wait returning. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    memset(g_sfr, 0, sizeof g_sfr);
    memset(&g_host, 0, sizeof g_host);
    g_host.ctx = &g_f;
    g_host.rom_call = stub_rom_call;
    g_host.read_sfr = stub_read_sfr;
    g_host.write_sfr = stub_write_sfr;
    g_host.wait = stub_wait;
    g_host.reset_cpu = stub_reset_cpu;
    g_host.set_interrupts = stub_set_interrupts;
    g_nlog = 0;
    g_ncalls = 0;
    g_wait_result = FW_WAIT_RETURNED;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
}

/* The log is exactly the n events of want, and no ROM call was made. */
static void check_log(const event *want, unsigned n)
{
    CHECK_EQ(g_nlog, n);
    for (unsigned i = 0; i < n && i < g_nlog && i < MAX_LOG; i++) {
        CHECK_EQ(g_log[i].kind, want[i].kind);
        CHECK_EQ(g_log[i].addr, want[i].addr);
        CHECK_EQ(g_log[i].value, want[i].value);
    }
    CHECK_EQ(g_ncalls, 0);
}

/* fw_wait with FW_WAIT_INPUT (M4.6c2 spec 3.1): the host gets the kind and isr_idle's WIT $C602. A wait that returns
   leaves the firmware as it was; a stopped one (any kind: the idle handler, or an entry row's phase deadline) sets
   f->stopped and is not a fault; a failed one, a host without wait or no host is a fault at the loop. */
static void test_wait_input(void)
{
    static const event want[] = { { EV_WAIT, FW_WAIT_INPUT_LOOP, FW_WAIT_INPUT } };
    static const event want_irq[] = { { EV_WAIT, 0xA6C2, FW_WAIT_INTERRUPT } };
    setup();
    fw_wait(&g_f, FW_WAIT_INPUT, FW_WAIT_INPUT_LOOP);
    check_log(want, 1);
    CHECK(!g_f.stopped);
    CHECK(!g_f.fault);

    setup();
    g_wait_result = FW_WAIT_STOPPED;
    fw_wait(&g_f, FW_WAIT_INPUT, FW_WAIT_INPUT_LOOP);
    check_log(want, 1);
    CHECK(g_f.stopped);
    CHECK(!g_f.fault);
    CHECK(!g_f.unwound);

    setup();
    g_wait_result = FW_WAIT_STOPPED;
    fw_wait(&g_f, FW_WAIT_INTERRUPT, 0xA6C2);
    check_log(want_irq, 1);
    CHECK(g_f.stopped);
    CHECK(!g_f.fault);

    setup();
    g_wait_result = FW_WAIT_FAILED;
    fw_wait(&g_f, FW_WAIT_INPUT, FW_WAIT_INPUT_LOOP);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xC602);
    CHECK(!g_f.stopped);

    setup();
    g_host.wait = NULL;
    fw_wait(&g_f, FW_WAIT_INPUT, FW_WAIT_INPUT_LOOP);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xC602);
    CHECK_EQ(g_nlog, 0);

    fw f;
    fw_init(&f, g_ram, fw_image_get(), NULL);
    CHECK(!f.stopped);
    fw_wait(&f, FW_WAIT_INPUT, FW_WAIT_INPUT_LOOP);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xC602);
}

/* fw_reset_cpu and fw_set_interrupts (M4.6c2 spec 3.3): one host call each. Without the host call, or without a
   host, a fault: fw_reset_cpu at boot_reset's SEI $A625, fw_set_interrupts at the address given. */
static void test_cpu_calls(void)
{
    static const event want[] = { { EV_RESET, FW_RESET_CPU, 0 }, { EV_INT, 0, 1 }, { EV_INT, 0, 0 } };
    setup();
    fw_reset_cpu(&g_f);
    fw_set_interrupts(&g_f, true, 0xA66E);
    fw_set_interrupts(&g_f, false, 0xA6E7);
    check_log(want, 3);
    CHECK(!g_f.fault);

    setup();
    g_host.reset_cpu = NULL;
    fw_reset_cpu(&g_f);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xA625);
    CHECK_EQ(g_nlog, 0);

    setup();
    g_host.set_interrupts = NULL;
    fw_set_interrupts(&g_f, true, 0xA6D3);
    CHECK(g_f.fault);
    CHECK_EQ(g_f.fault_addr, 0xA6D3);
    CHECK_EQ(g_nlog, 0);

    fw f;
    fw_init(&f, g_ram, fw_image_get(), NULL);
    fw_reset_cpu(&f);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xA625);
    fw_init(&f, g_ram, fw_image_get(), NULL);
    fw_set_interrupts(&f, false, 0xA6E7);
    CHECK(f.fault);
    CHECK_EQ(f.fault_addr, 0xA6E7);
}

void suite_fw_boot(void)
{
    CHECK(fw_image_get() != NULL);
    test_wait_input();
    test_cpu_calls();
}
