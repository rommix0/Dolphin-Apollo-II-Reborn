/* Unit checks of boot, the main loop and isr_idle (M4.6c2 spec 4.5), with a stub host that logs, in order, every SFR
   read and write, every wait (its ROM loop and kind), the CPU set-up (reset_cpu) and every CLI or SEI
   (set_interrupts), and fails every ROM call (the C makes none). Each expected value is worked out from the dump of the
   routine (build/ghidra740/dump), as the comment beside it says. The full proof is the live differential tests
   (diff_main, diff_lts) and the hybrid goldens (golden_hybrid). */
#include <string.h>
#include "test.h"
#include "engine/thunks.h"
#include "firmware/boot.h"
#include "firmware/boot_addr.h"
#include "firmware/cmd_apply.h"
#include "firmware/isr_dsp.h"
#include "firmware/ser.h"
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

/* Logs the wait. A wait for an interrupt is one handled: the timer interrupt sets isr_frame_flag (a frame is due at
   every wait). isr_timer_start's first wait sets ser_abort bit 0 (a mute as the DSP starts), so a boot_loop_play
   plays no segment: par_play goes from its first pass to its three rest frames. */
static fw_wait_result stub_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    (void)ctx;
    log_add(EV_WAIT, at, (uint8_t)kind);
    if (kind == FW_WAIT_INTERRUPT)
        g_ram[ISR_FRAME_FLAG] = 1;
    if (kind == FW_WAIT_TIMER2)
        g_ram[SER_ABORT] |= 1;
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

/* Zeroed RAM and SFRs but P2, the stub host, the converted image, nothing logged, every wait returning. */
static void setup(void)
{
    memset(g_ram, 0, sizeof g_ram);
    memset(g_sfr, 0, sizeof g_sfr);
    g_sfr[SFR_P2] = P2_MAIN_BANK;   /* the main ROM selected, as on the board: SEB/CLB of P2 keep it */
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

/* The replacement row of name: its address, outputs and kind (waits, isr, entry). */
static void check_row(const char *name, uint16_t addr, uint8_t outputs, bool waits, bool isr, bool entry)
{
    const fw_replacement *r = fw_replacement_find(name);
    CHECK(r != NULL);
    if (!r)
        return;
    CHECK_EQ(r->addr, addr);
    CHECK_EQ(r->outputs, outputs);
    CHECK_EQ(r->waits, waits);
    CHECK_EQ(r->isr, isr);
    CHECK_EQ(r->entry, entry);
    CHECK(!r->ramcode);
}

/* r is a, x, y. */
static void check_regs(const fw_regs *r, uint8_t a, uint8_t x, uint8_t y)
{
    CHECK_EQ(r->a, a);
    CHECK_EQ(r->x, x);
    CHECK_EQ(r->y, y);
}

#define REGS (FW_OUT_A | FW_OUT_X | FW_OUT_Y)

/* boot_read_dip $ADE0: AD_CTRL = 0 (LDM, before the read), then P4: boot_dip = A = P4 & $0F. X and Y untouched. */
static void test_read_dip(void)
{
    static const event want[] = { { EV_WRITE, SFR_AD_CTRL, 0 }, { EV_READ, SFR_P4, 0xF5 } };
    setup();
    g_sfr[SFR_AD_CTRL] = 0x80;
    g_sfr[SFR_P4] = 0xF5;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    boot_read_dip(&g_f, &r);
    check_log(want, 2);
    check_regs(&r, 0x05, 0x22, 0x33);
    CHECK_EQ(g_ram[BOOT_DIP], 0x05);
    CHECK(!g_f.fault);
}

/* boot_init_ports $AC22, serial (boot_dip bit 0 clear) and parallel: every SFR write in the ROM's order; A = 0 in both
   (the serial path's LDA #$48 is followed by its LDA #0 at $AC47), X and Y untouched; P2's write selects the main
   bank. */
static void test_init_ports(void)
{
    static const event serial[] = {
        { EV_WRITE, SFR_P0_FUNC, 0x48 }, { EV_WRITE, SFR_P0_DIR, 0x78 }, { EV_WRITE, SFR_P0, 0x20 },
        { EV_WRITE, SFR_P1_LATCH, 0x00 }, { EV_WRITE, SFR_P1_DIR, 0x00 }, { EV_WRITE, SFR_P2, 0x70 },
        { EV_WRITE, SFR_P2_DIR, 0xFD }, { EV_WRITE, SFR_P3_DIR, 0xF9 }, { EV_WRITE, SFR_P23_FUNC, 0x00 },
        { EV_WRITE, SFR_P3, 0x78 },
    };
    static const event parallel[] = {
        { EV_WRITE, SFR_P0_FUNC, 0x00 }, { EV_WRITE, SFR_P0_DIR, 0x08 }, { EV_WRITE, SFR_P0, 0x00 },
        { EV_WRITE, SFR_P1, 0x01 }, { EV_WRITE, SFR_P1_DIR, 0xFF }, { EV_WRITE, SFR_P2, 0x70 },
        { EV_WRITE, SFR_P2_DIR, 0xFD }, { EV_WRITE, SFR_P3_DIR, 0xF9 }, { EV_WRITE, SFR_P23_FUNC, 0x00 },
        { EV_WRITE, SFR_P3, 0x78 },
    };
    for (int par = 0; par <= 1; par++) {
        setup();
        g_ram[BOOT_DIP] = (uint8_t)par;
        g_f.bank = 0;
        fw_regs r = { 0x11, 0x22, 0x33, 0 };
        boot_init_ports(&g_f, &r);
        check_log(par ? parallel : serial, 10);
        check_regs(&r, 0x00, 0x22, 0x33);
        CHECK_EQ(g_f.bank, P2_MAIN_BANK);
        CHECK(!g_f.low_window);
        CHECK(!g_f.fault);
    }
}

/* boot_clear_input_state $ABF0: the ten flags and counts 0, cmd_index_reset 1, the output ring pointers $3900 and the
   input ring's $2100; nothing else written, no SFR touched. A = 0, X and Y untouched. */
static void test_clear_input_state(void)
{
    static const uint16_t zeroed[] = {
        SER_ABORT, BOOT_SPEAKING, SER_RX_IDLE, SER_RX_IDLE_SAVED, CMD_INDEX_COUNT, SER_TX_PENDING, 0x0007,
        SER_TX_COUNT, ISR_PAR_STATE, SER_CMD_MODE,
    };
    setup();
    memset(g_ram, 0xA5, 0x4000);
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    boot_clear_input_state(&g_f, &r);
    check_log(NULL, 0);
    check_regs(&r, 0x00, 0x22, 0x33);
    unsigned written = 0;
    for (size_t i = 0; i < sizeof zeroed / sizeof zeroed[0]; i++)
        CHECK_EQ(g_ram[zeroed[i]], 0);
    CHECK_EQ(g_ram[CMD_INDEX_RESET], 1);
    CHECK_EQ(g_ram[SER_TX_READ], 0x00);
    CHECK_EQ(g_ram[SER_TX_READ + 1], 0x39);
    CHECK_EQ(g_ram[SER_TX_WRITE], 0x00);
    CHECK_EQ(g_ram[SER_TX_WRITE + 1], 0x39);
    CHECK_EQ(g_ram[SER_RX_READ], 0x00);
    CHECK_EQ(g_ram[SER_RX_READ + 1], 0x21);
    CHECK_EQ(g_ram[SER_RX_WRITE], 0x00);
    CHECK_EQ(g_ram[SER_RX_WRITE + 1], 0x21);
    for (uint32_t a = 0; a < 0x4000; a++)
        written += g_ram[a] != 0xA5;
    CHECK_EQ(written, 19);   /* the ten flags, cmd_index_reset and the eight bytes of the four pointers */
    CHECK(!g_f.fault);
}

/* boot_set_defaults $A742 and boot_load_prosody_defaults $A7F0 (through it): the power-up @-settings ($A742-$A769),
   cmd_char = ser_cmd_char = '@', the ten cleared ($A76C-$A782, $1827 among them), cmd_formant_ofs and par_ofs
   ($2014-$201E) cleared, pros_params = boot_prosody_defaults. A = its last byte (Z: 0), X untouched, Y = 26; no SFR. */
static void test_set_defaults(void)
{
    static const struct { uint16_t addr; uint8_t value; } want[] = {
        { CMD_PITCH, 8 }, { CMD_PROSODY, 3 }, { CMD_SPEED_LIMIT, 7 }, { CMD_SPEED, 3 }, { CMD_VOLUME, 6 },
        { CMD_WORD_PAUSE, 0 }, { CMD_CHAR, '@' }, { SER_CMD_CHAR, '@' }, { CMD_PITCH_STEP, 8 }, { FW_UTT_CONTENT, 4 },
        { CMD_PHRASE_PAUSE, 0x0B }, { CMD_VOICE, 1 }, { CMD_MARK_SPACE, 0x14 }, { CMD_VOICING, 8 },
        { CMD_SOURCE, 0 }, { CMD_PHONETIC, 0 }, { CMD_SPELL, 0 }, { CMD_PUNCT, 0 }, { CMD_HYPER, 0 },
        { CMD_EV_COUNT, 0 }, { CMD_COUNT, 0 }, { CMD_SOUND_ACTIVE, 0 }, { CMD_SPEAKER, 0 }, { FW_CLAUSE_CARRY, 0 },
    };
    setup();
    memset(g_ram, 0xA5, 0x4000);
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    boot_set_defaults(&g_f, &r);
    check_log(NULL, 0);
    check_regs(&r, 0x00, 0x22, 26);
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++)
        CHECK_EQ(g_ram[want[i].addr], want[i].value);
    for (uint16_t a = CMD_FORMANT_OFS; a <= PAR_OFS; a++)
        CHECK_EQ(g_ram[a], 0);
    CHECK_EQ(g_ram[PAR_OFS_WEIGHT], 0xA5);
    CHECK(memcmp(g_ram + PROS_PARAMS, fw_boot_prosody_defaults, BOOT_PROSODY_PARAMS) == 0);
    CHECK_EQ(g_ram[PROS_PARAMS + BOOT_PROSODY_PARAMS], 0xA5);
    CHECK(!g_f.fault);

    setup();
    r = (fw_regs){ 0x11, 0x22, 0x33, 0 };
    boot_load_prosody_defaults(&g_f, &r);
    check_regs(&r, 0x00, 0x22, 26);
    CHECK_EQ(g_ram[PROS_PARAMS], 180);
    CHECK_EQ(g_ram[PROS_PARAMS + 3], 0xF6);          /* D: -10 */
    CHECK_EQ(g_ram[PROS_PARAMS + 10], 195);          /* K */
    CHECK(!g_f.fault);
}

/* boot_init_settings $ADEB: boot_clear_input_state, boot_set_defaults, then bank_ver_major = $FFE0 (2) and
   bank_ver_minor = $FFE1 (1). A = 1, X untouched, Y = 26. */
static void test_init_settings(void)
{
    setup();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    boot_init_settings(&g_f, &r);
    check_log(NULL, 0);
    check_regs(&r, 0x01, 0x22, 26);
    CHECK_EQ(g_ram[BANK_VER_MAJOR], 2);
    CHECK_EQ(g_ram[BANK_VER_MINOR], 1);
    CHECK_EQ(g_ram[SER_RX_READ + 1], 0x21);
    CHECK_EQ(g_ram[CMD_CHAR], '@');
    CHECK(!g_f.fault);
}

/* boot_init_baud $AC7E with X = 0 (boot_reset's): tn_phon_len = 0, then the jumpers P3.1 and P3.2 (one read of P3 per
   BBS: $AC81 bit 1, then $AC84 or $AC9A bit 2) and UART_BAUD: both low $02, P3.1 $BF, P3.2 $2F, both $05. A, X and Y
   untouched; nothing spoken. */
static void test_init_baud(void)
{
    static const uint8_t p3[] = { 0x00, 0x02, 0x04, 0x06 }, baud[] = { 0x02, 0xBF, 0x2F, 0x05 };
    for (int i = 0; i < 4; i++) {
        event want[] = { { EV_READ, SFR_P3, p3[i] }, { EV_READ, SFR_P3, p3[i] }, { EV_WRITE, SFR_UART_BAUD, baud[i] } };
        setup();
        g_sfr[SFR_P3] = p3[i];
        g_ram[TN_PHON_LEN] = 9;
        fw_regs r = { 0x11, 0x00, 0x33, 0 };
        boot_init_baud(&g_f, &r);
        check_log(want, 3);
        check_regs(&r, 0x11, 0x00, 0x33);
        CHECK_EQ(g_ram[TN_PHON_LEN], 0);
        CHECK_EQ(g_ram[TN_RULE_PTR], 0);
        CHECK(!g_f.fault);
    }
}

/* boot_init_baud with X = 1, unreachable in the ROM (M4.6c2 spec 4.4): the rate spoken through cmd_say_phonetic, each
   of the three texts. With the power-up settings (boot_set_defaults, cmd_apply_speaker, as boot_reset leaves them):
   tn_rule_ptr = the text, which cmd_put_phonetic appends to tn_phonetic (tn_phon_len its length), then
   pros_make_segments and boot_loop_play (C calls C): the DSP started (the waits of isr_timer_start; the stub's mute
   ends par_play at once) and stopped (isr_speech_stop's ICR2 = 0, then the DSP reset's CLB0 of P2 last). Both jumpers:
   no text, nothing spoken. */
static void test_say_rate(void)
{
    static const uint8_t p3[] = { 0x00, 0x02, 0x04, 0x06 };
    static const uint16_t text[] = { BOOT_BAUD_TEXT_19200, BOOT_BAUD_TEXT_300, BOOT_BAUD_TEXT_1200, 0 };
    static const char *const phonetic[] = { " ", "tRE HuNDRxD ", "TWeLV HuNDRxD \r", "" };
    for (int i = 0; i < 4; i++) {
        setup();
        fw_regs r = { 0, 0, 0, 0 };
        boot_set_defaults(&g_f, &r);
        r.a = cmd_apply_speaker(&g_f, &r.x, &r.y);
        g_nlog = 0;
        g_sfr[SFR_P3] = p3[i];
        r.x = 1;
        boot_init_baud(&g_f, &r);
        CHECK(!g_f.fault);
        CHECK_EQ(g_ncalls, 0);
        CHECK_EQ(fw_rd16(&g_f, TN_RULE_PTR), text[i]);
        size_t n = strlen(phonetic[i]);
        CHECK_EQ(g_ram[TN_PHON_LEN], n);
        CHECK(memcmp(g_ram + TN_PHONETIC, phonetic[i], n) == 0);
        unsigned timer2 = 0;
        for (unsigned k = 0; k < g_nlog && k < MAX_LOG; k++)
            timer2 += g_log[k].kind == EV_WAIT && g_log[k].value == FW_WAIT_TIMER2;
        CHECK_EQ(timer2, text[i] ? 1 : 0);
        if (text[i] && g_nlog <= MAX_LOG) {
            CHECK_EQ(g_sfr[SFR_ICR2], 0);
            CHECK_EQ(g_log[g_nlog - 1].kind, EV_WRITE);
            CHECK_EQ(g_log[g_nlog - 1].addr, SFR_P2);
            CHECK_EQ(g_log[g_nlog - 1].value, P2_MAIN_BANK);
        }
    }
}

/* boot_init_host_lines $AC68: UART_CTRL = $D1, P0_FUNC = $4C, then one read of P0 (BBC2): the host line high: CLB5 P0
   (a read and a write), isr_host_line_high (ICR3 = $40, SEB5 P0, ser_status_due = 1); low: SEB5 P0,
   isr_host_line_low (ICR3 = $48, CLB5 P0). A = $4C, X and Y untouched. */
static void test_init_host_lines(void)
{
    static const event high[] = {
        { EV_WRITE, SFR_UART_CTRL, 0xD1 }, { EV_WRITE, SFR_P0_FUNC, 0x4C }, { EV_READ, SFR_P0, 0x24 },
        { EV_READ, SFR_P0, 0x24 }, { EV_WRITE, SFR_P0, 0x04 }, { EV_WRITE, SFR_ICR3, 0x40 }, { EV_READ, SFR_P0, 0x04 },
        { EV_WRITE, SFR_P0, 0x24 },
    };
    static const event low[] = {
        { EV_WRITE, SFR_UART_CTRL, 0xD1 }, { EV_WRITE, SFR_P0_FUNC, 0x4C }, { EV_READ, SFR_P0, 0x00 },
        { EV_READ, SFR_P0, 0x00 }, { EV_WRITE, SFR_P0, 0x20 }, { EV_WRITE, SFR_ICR3, 0x48 }, { EV_READ, SFR_P0, 0x20 },
        { EV_WRITE, SFR_P0, 0x00 },
    };
    for (int line = 0; line <= 1; line++) {
        setup();
        g_sfr[SFR_P0] = line ? 0x24 : 0x00;
        fw_regs r = { 0x11, 0x22, 0x33, 0 };
        boot_init_host_lines(&g_f, &r);
        check_log(line ? high : low, 8);
        check_regs(&r, 0x4C, 0x22, 0x33);
        CHECK_EQ(g_ram[SER_STATUS_DUE], line ? 1 : 0);
        CHECK(!g_f.fault);
    }
}

/* The rows of the boot helpers and cmd_say_phonetic (M4.6c2 spec 2.3): A, X and Y declared; cmd_say_phonetic waits. */
static void test_helper_rows(void)
{
    check_row("boot_read_dip", ROM_BOOT_READ_DIP, REGS, false, false, false);
    check_row("boot_init_ports", ROM_BOOT_INIT_PORTS, REGS, false, false, false);
    check_row("boot_init_settings", ROM_BOOT_INIT_SETTINGS, REGS, false, false, false);
    check_row("boot_clear_input_state", ROM_BOOT_CLEAR_INPUT_STATE, REGS, false, false, false);
    check_row("boot_set_defaults", ROM_BOOT_SET_DEFAULTS, REGS, false, false, false);
    check_row("boot_load_prosody_defaults", ROM_BOOT_LOAD_PROSODY_DEFAULTS, REGS, false, false, false);
    check_row("boot_init_baud", ROM_BOOT_INIT_BAUD, REGS, false, false, false);
    check_row("boot_init_host_lines", ROM_BOOT_INIT_HOST_LINES, REGS, false, false, false);
    check_row("cmd_say_phonetic", ROM_CMD_SAY_PHONETIC, REGS, true, false, false);
}

void suite_fw_boot(void)
{
    CHECK(fw_image_get() != NULL);
    test_wait_input();
    test_cpu_calls();
    test_read_dip();
    test_init_ports();
    test_clear_input_state();
    test_set_defaults();
    test_init_settings();
    test_init_baud();
    test_say_rate();
    test_init_host_lines();
    test_helper_rows();
}
