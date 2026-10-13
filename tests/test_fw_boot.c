/* Unit checks of boot, the main loop and isr_idle (M4.6c2 spec 4.5), with a stub host that logs, in order, every SFR
   read and write, every wait (its ROM loop and kind), the CPU set-up (reset_cpu) and every CLI or SEI
   (set_interrupts), and fails every ROM call (the C makes none). Each expected value is worked out from the dump of the
   routine (build/ghidra740/dump), as the comment beside it says. The full proof is the live differential tests
   (diff_main, diff_lts) and the hybrid goldens (golden_hybrid). */
#include <string.h>
#include "test.h"
#include "engine/thunks.h"
#include "firmware/bank_addr.h"
#include "firmware/boot.h"
#include "firmware/boot_addr.h"
#include "firmware/boot_input.h"
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
static const char *g_feed[8];          /* a feed script: what each input wait puts into the input ring ... */
static unsigned g_nfeed, g_feed_pos;   /* ... in turn (none: no script) */
static unsigned g_mute_at;             /* the feed (1, 2, ...) that also sets ser_abort bit 0 (0: none) */

/* s into the input ring, as the receive interrupt's ser_rx_put puts each byte. */
static void feed(const char *s)
{
    for (; *s; s++) {
        fw_regs t = { (uint8_t)*s, 0, 0, 0 };
        ser_rx_put(&g_f, &t);
    }
}

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
   plays no segment: par_play goes from its first pass to its three rest frames. With a feed script, an input wait,
   and a wait for an interrupt at boot_get_clause's loop ($A6C2), takes the script's next string into the input ring
   (the g_mute_at-th sets ser_abort bit 0 as well); once the script is used up such a wait stops the run. */
static fw_wait_result stub_wait(void *ctx, fw_wait_kind kind, uint16_t at)
{
    (void)ctx;
    log_add(EV_WAIT, at, (uint8_t)kind);
    if (kind == FW_WAIT_INTERRUPT)
        g_ram[ISR_FRAME_FLAG] = 1;
    if (kind == FW_WAIT_TIMER2)
        g_ram[SER_ABORT] |= 1;
    if (g_nfeed && (kind == FW_WAIT_INPUT || (kind == FW_WAIT_INTERRUPT && at == BOOT_GET_CLAUSE_LOOP))) {
        if (g_feed_pos == g_nfeed)
            return FW_WAIT_STOPPED;
        feed(g_feed[g_feed_pos++]);
        if (g_feed_pos == g_mute_at)
            g_ram[SER_ABORT] |= 1;
    }
    return g_wait_result;
}

/* Every other bank an empty socket (bank_scan reads their headers): $FF. Not logged. */
static uint8_t stub_read_bank(void *ctx, uint16_t addr)
{
    (void)ctx;
    (void)addr;
    return 0xFF;
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
    g_host.read_bank = stub_read_bank;
    g_nlog = 0;
    g_ncalls = 0;
    g_wait_result = FW_WAIT_RETURNED;
    g_nfeed = g_feed_pos = g_mute_at = 0;
    fw_init(&g_f, g_ram, fw_image_get(), &g_host);
    g_ram[SER_RX_READ + 1] = g_ram[SER_RX_WRITE + 1] = 0x21;   /* the input ring empty at $2100, as at boot */
}

/* A feed script of n strings (setup clears it). */
static void script(const char *const *s, unsigned n)
{
    for (unsigned i = 0; i < n && i < 8; i++)
        g_feed[i] = s[i];
    g_nfeed = n;
    g_feed_pos = 0;
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

/* isr_idle $C5FE: in serial mode the WIT, one input wait ($C602); in parallel mode (boot_dip bit 0) nothing. A, X and Y
   kept. */
static void test_isr_idle(void)
{
    static const event want[] = { { EV_WAIT, FW_WAIT_INPUT_LOOP, FW_WAIT_INPUT } };
    setup();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_idle(&g_f, &r);
    check_log(want, 1);
    check_regs(&r, 0x11, 0x22, 0x33);
    setup();
    g_ram[BOOT_DIP] = DIP_PARALLEL;
    isr_idle(&g_f, &r);
    check_log(NULL, 0);
    check_regs(&r, 0x11, 0x22, 0x33);
    CHECK(!g_f.fault);
}

/* boot_clause is " " text " " with boot_clause_len its length. */
static void check_clause(const char *text)
{
    size_t n = strlen(text);
    CHECK_EQ(g_ram[BOOT_CLAUSE], ' ');
    CHECK(memcmp(g_ram + BOOT_CLAUSE + 1, text, n) == 0);
    CHECK_EQ(g_ram[BOOT_CLAUSE + 1 + n], ' ');
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], n + 2);
}

/* One boot_get_clause in serial mode on the feed script s (one string per input wait), cmd_char '@' (as
   boot_set_defaults leaves it); A = ' ' and X = boot_clause_len out, no fault, no stop. */
static void get_clause(const char *const *s, unsigned n)
{
    setup();
    script(s, n);
    g_ram[CMD_CHAR] = '@';
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    boot_get_clause(&g_f, &r);
    CHECK(!g_f.fault);
    CHECK(!g_f.stopped);
    CHECK_EQ(r.a, ' ');
    CHECK_EQ(r.x, g_ram[BOOT_CLAUSE_LEN]);
}

/* boot_get_clause $A697, serial mode, one byte per input wait as the machine delivers them: the entry's writes
   (boot_clause[0] = ' ', ser_abort, tn_in_word, $6B and boot_speaking 0, ser_rx_idle bit 0 set on the empty ring), then
   per byte a pass with the ring empty (CLI at $A6D3, isr_idle's input wait) and the read (SEI at $A6E7, CLI at $A6FF);
   isr_par_ready and isr_par_hold do nothing in serial mode. "Hi." ends at the '.', kept: " Hi. ", 5; boot_speaking
   bit 0 set by the read, ser_rx_idle's cleared by ser_rx_put. Y = 0. */
static void test_get_clause_waits(void)
{
    static const char *const hi[] = { "H", "i", "." };
    static const event want[] = {
        { EV_INT, 0, 1 }, { EV_WAIT, 0xC602, FW_WAIT_INPUT }, { EV_INT, 0, 0 }, { EV_INT, 0, 1 },
        { EV_INT, 0, 1 }, { EV_WAIT, 0xC602, FW_WAIT_INPUT }, { EV_INT, 0, 0 }, { EV_INT, 0, 1 },
        { EV_INT, 0, 1 }, { EV_WAIT, 0xC602, FW_WAIT_INPUT }, { EV_INT, 0, 0 }, { EV_INT, 0, 1 },
    };
    setup();
    script(hi, 3);
    g_ram[SER_ABORT] = g_ram[TN_IN_WORD] = g_ram[FW_UTT_CONTENT] = g_ram[BOOT_SPEAKING] = 0xFF;
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    boot_get_clause(&g_f, &r);
    check_log(want, 12);
    check_regs(&r, ' ', 5, 0);
    check_clause("Hi.");
    CHECK_EQ(g_ram[SER_ABORT], 0);
    CHECK_EQ(g_ram[TN_IN_WORD], 0);
    CHECK_EQ(g_ram[FW_UTT_CONTENT], 0);
    CHECK_EQ(g_ram[BOOT_SPEAKING], 1);
    CHECK_EQ(g_ram[SER_RX_IDLE], 0);
    CHECK_EQ(g_ram[SER_RX_READ], 3);
    CHECK(!g_f.fault);
    CHECK(!g_f.stopped);
}

/* Every clause end of boot_get_clause (M4.6c2 spec 2.5): a CR (not kept: a space instead); . , : ; ! ? (kept); X
   reaching $FA (249 characters); from X $DC on a space (kept, then the space after it), or cmd_char, carried over in
   $1827 with X stepped back over it, and put first in the next clause. A space before X $DC does not end it. */
static void test_get_clause_ends(void)
{
    static const char *const cr[] = { "ab\r" };
    get_clause(cr, 1);
    check_clause("ab");
    static const char *const ends = ".,:;!?";
    for (int i = 0; i < 6; i++) {
        char s[4] = { 'a', ends[i], 'b', 0 };
        const char *one[] = { s };
        get_clause(one, 1);
        char want[3] = { 'a', ends[i], 0 };
        check_clause(want);
        CHECK_EQ(g_ram[SER_RX_READ], 2);           /* the 'b' stays in the ring */
    }
    static char word[300], spaced[300], carried[300];
    memset(word, 'w', 260);
    word[260] = 0;
    const char *full[] = { word };
    get_clause(full, 1);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 0xFB);        /* 249 letters at $01-$F9, the space at $FA */
    CHECK_EQ(g_ram[BOOT_CLAUSE + 0xF9], 'w');
    CHECK_EQ(g_ram[BOOT_CLAUSE + 0xFA], ' ');
    CHECK_EQ(g_ram[SER_RX_READ], 249);

    for (int i = 0; i < 218; i++)                  /* words of four letters and a space up to $DA, then a space */
        spaced[i] = i % 5 == 4 ? ' ' : 'w';
    spaced[217] = 'w';
    strcpy(spaced + 218, " more.");
    const char *sp[] = { spaced };
    get_clause(sp, 1);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 0xDD);        /* the space at $DB kept, another at $DC */
    CHECK_EQ(g_ram[BOOT_CLAUSE + 0xDB], ' ');
    CHECK_EQ(g_ram[BOOT_CLAUSE + 0xDC], ' ');
    CHECK_EQ(g_ram[BOOT_CLAUSE + 0xDA], 'w');
    CHECK_EQ(g_ram[FW_CLAUSE_CARRY], 0);

    memset(carried, 'w', 218);
    strcpy(carried + 218, "@W5.");
    const char *ca[] = { carried };
    get_clause(ca, 1);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 0xDC);        /* the '@' at $DB overwritten by the space */
    CHECK_EQ(g_ram[BOOT_CLAUSE + 0xDB], ' ');
    CHECK_EQ(g_ram[FW_CLAUSE_CARRY], '@');
    fw_regs r = { 0, 0, 0, 0 };
    boot_get_clause(&g_f, &r);                     /* the next clause: the '@' first, then "W5." */
    check_clause("@W5.");
    CHECK_EQ(g_ram[FW_CLAUSE_CARRY], 0);
    CHECK(!g_f.fault);
}

/* boot_get_clause's other paths: a mute pending when a byte is there (ser_abort bit 0, $A6E8) starts again from the
   top, dropping what was read; the read pointer's page end ($21FF: ser_rx_next_page, Y = the new page $22; $38FF
   wraps to $21); a stopped input wait abandons it (f->stopped, boot_clause_len not written). Review Focus 5 (M4.6c2
   plan): input that arrives while speech plays reaches the ring in one wait, several clauses at once: each later call
   takes its clause from the ring with no wait. */
static void test_get_clause_paths(void)
{
    static const char *const muted[] = { "ab", "c." };
    setup();
    script(muted, 2);
    g_mute_at = 2;
    fw_regs r = { 0, 0, 0, 0 };
    boot_get_clause(&g_f, &r);
    check_clause("c.");
    CHECK_EQ(g_ram[SER_ABORT], 0);

    static const uint16_t page[] = { 0x21, 0x38 }, next[] = { 0x22, 0x21 };
    for (int i = 0; i < 2; i++) {
        setup();
        g_ram[SER_RX_READ] = g_ram[SER_RX_WRITE] = 0xFF;
        g_ram[SER_RX_READ + 1] = g_ram[SER_RX_WRITE + 1] = (uint8_t)page[i];
        static const char *const dot[] = { "." };
        script(dot, 1);
        r = (fw_regs){ 0, 0, 0, 0 };
        boot_get_clause(&g_f, &r);
        check_clause(".");
        CHECK_EQ(r.y, next[i]);
        CHECK_EQ(g_ram[SER_RX_READ], 0);
        CHECK_EQ(g_ram[SER_RX_READ + 1], next[i]);
    }

    setup();
    static const char *const part[] = { "ab" };
    script(part, 1);
    g_ram[BOOT_CLAUSE_LEN] = 0x77;
    r = (fw_regs){ 0, 0, 0, 0 };
    boot_get_clause(&g_f, &r);
    CHECK(g_f.stopped);
    CHECK(!g_f.fault);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 0x77);
    CHECK_EQ(g_feed_pos, 1);

    setup();
    static const char *const two[] = { "One. Two? " };
    script(two, 1);
    r = (fw_regs){ 0, 0, 0, 0 };
    boot_get_clause(&g_f, &r);
    check_clause("One.");
    unsigned waits = g_nlog;
    boot_get_clause(&g_f, &r);
    check_clause(" Two?");
    for (unsigned i = waits; i < g_nlog && i < MAX_LOG; i++)
        CHECK(g_log[i].kind != EV_WAIT);           /* the second clause read with no wait */
    CHECK_EQ(g_ram[SER_RX_IDLE], 0);               /* the ring was not empty at its start */
    CHECK(!g_f.fault);
}

/* boot_get_clause's waits for an interrupt (M4.6c2 spec 2.5), where the ROM would loop with nothing changed. Parallel
   mode: isr_par_ready (SEB3 P0) twice, isr_idle returning at once, then a wait at $A6C2. A status byte due with the
   host line low: ser_tx_service reads P0 and sends nothing, a wait at $A6C2. With the host line high it sends (a NAK,
   nothing pending), a pass that changed state: the next pass follows at once (ser_status_due now 0: isr_idle's input
   wait). */
static void test_get_clause_idle_waits(void)
{
    static const char *const dot[] = { "." };
    static const event par[] = {
        { EV_READ, SFR_P0, 0x00 }, { EV_WRITE, SFR_P0, 0x08 }, { EV_INT, 0, 1 }, { EV_READ, SFR_P0, 0x08 },
        { EV_WRITE, SFR_P0, 0x08 }, { EV_WAIT, 0xA6C2, FW_WAIT_INTERRUPT }, { EV_READ, SFR_P0, 0x08 },
        { EV_WRITE, SFR_P0, 0x00 }, { EV_READ, SFR_ICR1, 0x00 }, { EV_WRITE, SFR_ICR1, 0x00 }, { EV_INT, 0, 0 },
        { EV_READ, SFR_P0, 0x00 }, { EV_WRITE, SFR_P0, 0x08 }, { EV_INT, 0, 1 },
    };
    setup();
    g_ram[BOOT_DIP] = DIP_PARALLEL;
    script(dot, 1);
    fw_regs r = { 0, 0, 0, 0 };
    boot_get_clause(&g_f, &r);
    check_log(par, 14);
    check_clause(".");

    static const event low[] = {
        { EV_INT, 0, 1 }, { EV_READ, SFR_P0, 0x00 }, { EV_WAIT, 0xA6C2, FW_WAIT_INTERRUPT }, { EV_INT, 0, 0 },
        { EV_INT, 0, 1 },
    };
    setup();
    g_ram[SER_STATUS_DUE] = 1;
    script(dot, 1);
    boot_get_clause(&g_f, &r);
    check_log(low, 5);
    check_clause(".");
    CHECK_EQ(g_ram[SER_STATUS_DUE], 1);

    static const event high[] = {
        { EV_INT, 0, 1 }, { EV_READ, SFR_P0, 0x04 }, { EV_READ, SFR_P0, 0x04 }, { EV_WRITE, SFR_P0, 0x04 },
        { EV_WAIT, 0xC500, FW_WAIT_UART_TX }, { EV_WRITE, SFR_UART_TX, 0x15 }, { EV_WAIT, 0xC500, FW_WAIT_UART_TX },
        { EV_READ, SFR_P0, 0x04 }, { EV_READ, SFR_P0, 0x04 }, { EV_WRITE, SFR_P0, 0x24 }, { EV_INT, 0, 1 },
        { EV_WAIT, 0xC602, FW_WAIT_INPUT }, { EV_INT, 0, 0 }, { EV_INT, 0, 1 },
    };
    setup();
    g_ram[SER_STATUS_DUE] = 1;
    g_sfr[SFR_P0] = 0x04;
    script(dot, 1);
    boot_get_clause(&g_f, &r);
    check_log(high, 14);
    check_clause(".");
    CHECK_EQ(g_ram[SER_STATUS_DUE], 0);
    CHECK(!g_f.fault);
}

/* boot_loop_text $A68D: boot_get_clause, then cmd_scan_clause (boot_clause to cmd_text, upper case) and
   tn_convert_clause (cmd_text to tn_phonetic). A stopped wait in boot_get_clause ends it there: no scan. */
static void test_loop_text(void)
{
    static const char *const hi[] = { "hi." };
    setup();
    fw_regs r = { 0, 0, 0, 0 };
    boot_set_defaults(&g_f, &r);
    script(hi, 1);
    boot_loop_text(&g_f, &r);
    CHECK(!g_f.fault);
    CHECK(memcmp(g_ram + CMD_TEXT, " HI. ", 5) == 0);
    CHECK_EQ(g_ram[BOOT_CLAUSE_LEN], 5);
    CHECK(g_ram[TN_PHON_LEN] > 0);

    setup();
    g_wait_result = FW_WAIT_STOPPED;
    g_ram[CMD_TEXT] = 0x77;
    boot_loop_text(&g_f, &r);
    CHECK(g_f.stopped);
    CHECK_EQ(g_ram[CMD_TEXT], 0x77);
}

/* boot_queue_string $C612: the string at cmd_text_ptr (boot_selftest_ok_cmds) into the input ring up to its zero
   end, $78 = the index of its last byte; A, X and Y kept. Serial mode: no SFR; parallel mode: isr_par_hold (CLB3 P0,
   CLB1 ICR1) before and isr_par_ready (SEB3 P0) after. */
static void test_queue_string(void)
{
    static const event par[] = {
        { EV_READ, SFR_P0, 0x08 }, { EV_WRITE, SFR_P0, 0x00 }, { EV_READ, SFR_ICR1, 0x02 },
        { EV_WRITE, SFR_ICR1, 0x00 }, { EV_READ, SFR_P0, 0x00 }, { EV_WRITE, SFR_P0, 0x08 },
    };
    for (int mode = 0; mode <= 1; mode++) {
        setup();
        g_ram[BOOT_DIP] = (uint8_t)mode;
        g_sfr[SFR_P0] = 0x08;
        g_sfr[SFR_ICR1] = 0x02;
        g_ram[SER_RX_IDLE] = 1;
        fw_wr16(&g_f, CMD_TEXT_PTR, BOOT_SELFTEST_OK_CMDS);
        fw_regs r = { 0x11, 0x22, 0x33, 0 };
        boot_queue_string(&g_f, &r);
        check_log(mode ? par : NULL, mode ? 6 : 0);
        check_regs(&r, 0x11, 0x22, 0x33);
        CHECK(memcmp(g_ram + 0x2100, "\030@T5, ", 6) == 0);
        CHECK_EQ(g_ram[SER_RX_WRITE], 6);
        CHECK_EQ(g_ram[SER_RX_READ], 0);
        CHECK_EQ(g_ram[FW_TMP_78], 5);
        CHECK_EQ(g_ram[SER_RX_IDLE], 0);
        CHECK(!g_f.fault);
    }
}

/* boot_selftest $C605: boot_selftest_fail = 0, and 1 only with boot_dip bit 1 set and P2.1 high (one read of P2;
   none with bit 1 clear). A, X and Y kept. */
static void test_selftest(void)
{
    static const struct { uint8_t dip, p2, fail; unsigned reads; } k[] = {
        { 0x00, 0x72, 0, 0 }, { 0x02, 0x70, 0, 1 }, { 0x02, 0x72, 1, 1 },
    };
    for (int i = 0; i < 3; i++) {
        setup();
        g_ram[BOOT_DIP] = k[i].dip;
        g_ram[BOOT_SELFTEST_FAIL] = 0x55;
        g_sfr[SFR_P2] = k[i].p2;
        fw_regs r = { 0x11, 0x22, 0x33, 0 };
        boot_selftest(&g_f, &r);
        CHECK_EQ(g_ram[BOOT_SELFTEST_FAIL], k[i].fail);
        CHECK_EQ(g_nlog, k[i].reads);
        check_regs(&r, 0x11, 0x22, 0x33);
        CHECK(!g_f.fault);
    }
}

/* boot_selftest_sound $A79F: cmd_text_ptr = boot_selftest_ok_cmds, or boot_selftest_fail_cmds when the self-test
   failed (the fail path: M4.6c2 spec 4.4); the string queued into the input ring; its clause taken from the ring with
   no input wait (up to its ','; the space after it stays in the ring), scanned (its @T commands queued, cmd_count) and
   converted; then the speech (pros_make_segments, boot_loop_play: the DSP started, the stub's mute ending par_play at
   once). With the power-up settings, as boot_reset leaves them. */
static void test_selftest_sound(void)
{
    static const char *const text[] = { fw_boot_selftest_ok_cmds, fw_boot_selftest_fail_cmds };
    static const char *const clause[] = { " \030@T5, ", " \030@T7 @T7 @T7, " };
    for (int fail = 0; fail <= 1; fail++) {
        setup();
        fw_regs r = { 0, 0, 0, 0 };
        boot_set_defaults(&g_f, &r);
        r.a = cmd_apply_speaker(&g_f, &r.x, &r.y);
        g_ram[BOOT_DIP] = fail ? DIP_SELFTEST : 0;
        g_sfr[SFR_P2] = fail ? 0x72 : 0x70;
        g_nlog = 0;
        boot_selftest_sound(&g_f, &r);
        CHECK(!g_f.fault);
        CHECK(!g_f.stopped);
        CHECK_EQ(g_ncalls, 0);
        CHECK_EQ(g_ram[BOOT_SELFTEST_FAIL], fail);
        CHECK(memcmp(g_ram + SER_RX_RING, text[fail], strlen(text[fail])) == 0);
        CHECK_EQ(g_ram[SER_RX_WRITE], strlen(text[fail]));
        CHECK_EQ(g_ram[SER_RX_READ], strlen(text[fail]) - 1);
        CHECK(memcmp(g_ram + BOOT_CLAUSE, clause[fail], strlen(clause[fail])) == 0);
        CHECK_EQ(g_ram[CMD_COUNT], fail ? 3 : 1);   /* the @T commands of the clause */
        unsigned input = 0, timer2 = 0;
        for (unsigned k = 0; k < g_nlog && k < MAX_LOG; k++) {
            input += g_log[k].kind == EV_WAIT && g_log[k].value == FW_WAIT_INPUT;
            timer2 += g_log[k].kind == EV_WAIT && g_log[k].value == FW_WAIT_TIMER2;
        }
        CHECK_EQ(input, 0);
        CHECK_EQ(timer2, 1);
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

/* The count of logged events of kind with value (value 0xFF: any). */
static unsigned count_log(ev_kind kind, uint8_t value)
{
    unsigned n = 0;
    for (unsigned i = 0; i < g_nlog && i < MAX_LOG; i++)
        n += g_log[i].kind == kind && (value == 0xFF || g_log[i].value == value);
    return n;
}

/* boot_reset $A61C (M4.6c2 spec 2.1), serial mode: P3_DIR, P23_FUNC, P3; the CPU set-up; ICR1, ICR2, ICR3 = 0; then
   isr_set_volume's P3 (a read and a write: volume 0) and boot_read_dip's AD_CTRL write first. $C9 down to $01 cleared
   ($C9 is left 0; $CA, above, untouched). The whole boot runs: the settings, the self-test sound (spoken: the DSP
   started once), the baud rate and host lines (UART_BAUD $02, UART_CTRL $D1), boot_reentry_warm's CLI, the banks
   (bank_current 1), then the main loop: boot_get_clause takes the space the self-test string left in the ring and
   waits for input, where the script stops the run. Parallel mode (P4 bit 0): no UART_BAUD or UART_CTRL write. */
static void test_reset(void)
{
    static const char *const none[] = { "" };
    static const event want[] = {
        { EV_WRITE, SFR_P3_DIR, 0xF9 }, { EV_WRITE, SFR_P23_FUNC, 0x00 }, { EV_WRITE, SFR_P3, 0x78 },
        { EV_RESET, FW_RESET_CPU, 0 }, { EV_WRITE, SFR_ICR1, 0x00 }, { EV_WRITE, SFR_ICR2, 0x00 },
        { EV_WRITE, SFR_ICR3, 0x00 }, { EV_READ, SFR_P3, 0x78 }, { EV_WRITE, SFR_P3, 0x78 | 0x78 },
        { EV_WRITE, SFR_AD_CTRL, 0x00 },
    };
    for (int par = 0; par <= 1; par++) {
        setup();
        script(none, 1);
        memset(g_ram, 0xA5, 0x100);
        g_sfr[SFR_P4] = (uint8_t)par;
        boot_reset(&g_f);
        CHECK(g_f.stopped);
        CHECK(!g_f.fault);
        CHECK_EQ(g_ncalls, 0);
        CHECK(g_nlog >= 10);
        for (unsigned i = 0; i < 10 && i < g_nlog; i++) {
            CHECK_EQ(g_log[i].kind, want[i].kind);
            CHECK_EQ(g_log[i].addr, want[i].addr);
            CHECK_EQ(g_log[i].value, want[i].value);
        }
        CHECK_EQ(g_ram[0xC9], 0);
        CHECK_EQ(g_ram[0xCA], 0xA5);
        CHECK_EQ(g_ram[BOOT_DIP], par);
        CHECK_EQ(g_ram[BANK_VER_MAJOR], 2);
        CHECK_EQ(g_ram[BANK_CURRENT], 1);
        CHECK_EQ(g_ram[CMD_CHAR], '@');
        CHECK_EQ(count_log(EV_RESET, 0xFF), 1);
        CHECK_EQ(count_log(EV_WAIT, FW_WAIT_TIMER2), 1);              /* the self-test sound */
        CHECK_EQ(g_sfr[SFR_UART_BAUD], par ? 0x00 : 0x02);
        CHECK_EQ(g_sfr[SFR_UART_CTRL], par ? 0x00 : 0xD1);
        CHECK_EQ(g_ram[BOOT_CLAUSE + 1], ' ');                       /* the self-test string's last byte */
        CHECK_EQ(g_feed_pos, 1);
    }
}

/* The re-entries and the bank entries (M4.6c2 spec 2.1, 3.4). boot_reentry_warm: ser_abort = 0, CLI first, the banks
   (bank_load_ramcode's P2 read), then the main loop, which speaks each clause (the DSP started per clause: two) and
   stops at the input wait once the script is used up. bank_entry_warm is the same. boot_reentry_cold: boot_read_dip
   and boot_init_settings first (AD_CTRL, P4), the settings reset (the ring pointers, cmd_char), then the same.
   bank_entry_cold: SEI first, then boot_reentry_cold. */
static void test_reentries(void)
{
    static const char *const two[] = { "One. ", "Two." };
    for (int which = 0; which < 4; which++) {
        setup();
        fw_regs r = { 0, 0, 0, 0 };
        boot_set_defaults(&g_f, &r);
        g_ram[SER_ABORT] = 1;
        g_ram[CMD_CHAR] = 0x5E;                  /* the cold re-entry sets '@' again; the warm one keeps it */
        script(two, 2);
        g_nlog = 0;
        r = (fw_regs){ 0x02, 0xFF, 0x00, 0 };    /* as bank_enter leaves them at the warm entry */
        switch (which) {
        case 0: boot_reentry_warm(&g_f, &r); break;
        case 1: bank_entry_warm(&g_f, &r); break;
        case 2: boot_reentry_cold(&g_f, &r); break;
        default: bank_entry_cold(&g_f, &r); break;
        }
        CHECK(g_f.stopped);
        CHECK(!g_f.fault);
        CHECK_EQ(g_ncalls, 0);
        CHECK(g_nlog > 3);
        if (g_nlog <= 3)
            continue;
        bool cold = which >= 2;
        unsigned i = 0;
        if (which == 3) {
            CHECK_EQ(g_log[0].kind, EV_INT);                         /* $FFD0 SEI */
            CHECK_EQ(g_log[0].value, 0);
            i = 1;
        }
        if (cold) {
            CHECK_EQ(g_log[i].kind, EV_WRITE);                       /* boot_read_dip */
            CHECK_EQ(g_log[i].addr, SFR_AD_CTRL);
            i += 2;
        }
        CHECK_EQ(g_log[i].kind, EV_INT);                             /* $A66E CLI */
        CHECK_EQ(g_log[i].value, 1);
        CHECK_EQ(g_log[i + 1].kind, EV_READ);                        /* bank_load_ramcode's P2 */
        CHECK_EQ(g_log[i + 1].addr, SFR_P2);
        CHECK_EQ(g_ram[SER_ABORT], 0);
        CHECK_EQ(g_ram[CMD_CHAR], cold ? '@' : 0x5E);
        CHECK_EQ(g_ram[BANK_CURRENT], 1);
        CHECK_EQ(count_log(EV_WAIT, FW_WAIT_TIMER2), 2);             /* "One." and "Two." spoken */
        CHECK_EQ(g_feed_pos, 2);
    }
}

/* boot_get_clause with the read and write pointers' low bytes equal and their pages apart (256 bytes in the ring): not
   empty, at its entry ($A6BE BNE taken: ser_rx_idle left clear) and in its loop ($A6CC BNE taken: no wait). The
   corpus never buffers exactly 256 bytes: the branch probe found both branches one-sided, pinned here. */
static void test_get_clause_page_apart(void)
{
    setup();
    g_ram[SER_RX_WRITE + 1] = 0x22;               /* $2100 to $2200 */
    memset(g_ram + 0x2100, 'a', 256);
    g_ram[0x2100 + 3] = '.';
    fw_regs r = { 0, 0, 0, 0 };
    boot_get_clause(&g_f, &r);
    check_clause("aaa.");
    CHECK_EQ(g_ram[SER_RX_IDLE], 0);
    CHECK_EQ(count_log(EV_WAIT, 0xFF), 0);
    CHECK(!g_f.fault);
}

/* isr_unused $C3DF: RTI, nothing done: no host call, A, X and Y kept. */
static void test_isr_unused(void)
{
    setup();
    fw_regs r = { 0x11, 0x22, 0x33, 0 };
    isr_unused(&g_f, &r);
    check_log(NULL, 0);
    check_regs(&r, 0x11, 0x22, 0x33);
    CHECK(!g_f.fault);
}

/* The rows of the entries (no output: they never return) and isr_unused (an interrupt handler, no output). */
static void test_entry_rows(void)
{
    check_row("boot_reset", ROM_BOOT_RESET, 0, false, false, true);
    check_row("boot_reentry_cold", ROM_BOOT_REENTRY_COLD, 0, false, false, true);
    check_row("boot_reentry_warm", ROM_BOOT_REENTRY_WARM, 0, false, false, true);
    check_row("bank_entry_cold", BANK_ENTRY_COLD, 0, false, false, true);
    check_row("bank_entry_warm", BANK_ENTRY_WARM, 0, false, false, true);
    check_row("isr_unused", ROM_ISR_UNUSED, 0, false, true, false);
}

/* The rows of the input side and the self-test (M4.6c2 spec 2.3): the waits rows (boot_loop_text declares no output:
   the C tn_convert_clause sets no register) and two helpers. */
static void test_input_rows(void)
{
    check_row("boot_loop_text", ROM_BOOT_LOOP_TEXT, 0, true, false, false);
    check_row("boot_get_clause", ROM_BOOT_GET_CLAUSE, REGS, true, false, false);
    check_row("isr_idle", ROM_ISR_IDLE, REGS, true, false, false);
    check_row("boot_selftest_sound", ROM_BOOT_SELFTEST_SOUND, REGS, true, false, false);
    check_row("boot_selftest", ROM_BOOT_SELFTEST, REGS, false, false, false);
    check_row("boot_queue_string", ROM_BOOT_QUEUE_STRING, REGS, false, false, false);
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
    test_isr_idle();
    test_get_clause_waits();
    test_get_clause_ends();
    test_get_clause_paths();
    test_get_clause_idle_waits();
    test_loop_text();
    test_queue_string();
    test_selftest();
    test_selftest_sound();
    test_input_rows();
    test_reset();
    test_reentries();
    test_isr_unused();
    test_entry_rows();
    test_get_clause_page_apart();
}
