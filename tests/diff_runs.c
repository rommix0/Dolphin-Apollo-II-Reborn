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
