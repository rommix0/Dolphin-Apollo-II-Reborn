/* The deferred handlers that reply on the serial line (M4.2 spec 2.2): @E, @c? and @L. The reply goes into the
   transmit ring through the ROM ser_tx_* routines (the serial subsystem, M4.6), called with A, X and Y as the
   ROM has them at each JSR. Entered as the ROM handlers are: x = the argument's boot_clause index,
   a = boot_clause[x]. */
#include "cmd_handlers.h"
#include "ser_addr.h"
#include "data/fw_data.h"

/* A ser_tx_* routine with A, X, Y in; returns the registers it leaves (it pushes and pulls all three). */
static fw_regs ser_call(fw *f, uint16_t addr, uint8_t a, uint8_t x, uint8_t y)
{
    fw_regs r = { a, x, y, 0 };
    fw_rom_call(f, addr, &r, FW_ROM_CALL_BUDGET);
    return r;
}

/* $AB99 @E: the argument character, raw, is the reply. Y is the handler's entry Y (cmd_handler_y); the ser_tx_*
   routines only save and restore it, so it reaches the dead stack alone and is passed for exactness. */
void cmd_echo(fw *f, uint8_t x, uint8_t a)
{
    ser_call(f, ROM_SER_TX_PUT, a, x, cmd_handler_y(f));
}

/* $C632: a = the setting letter, compared raw. If cmd_query_table lists it: reply the letter (ser_tx_put), then
   the setting's zero-page byte as two hex digits (ser_tx_hex). $75 = the letter; X is parked in $77
   (tn_resume_pos) around the zero-page read. The ROM continues with the X and Y that ser_tx_put returns. It
   returns A = $75, which cmd_query does not use. */
static void cmd_reply_setting(fw *f, uint8_t a, uint8_t x)
{
    fw_wr8(f, FW_TMP_75, a);
    uint8_t y = 0;
    for (;;) {                                                      /* $C636 */
        uint8_t letter = fw_rom8(f, (uint16_t)(CMD_QUERY_TABLE + y));
        if (letter == 0)
            return;                                                 /* not listed: no reply */
        if (letter == fw_rd8(f, FW_TMP_75))
            break;
        y = (uint8_t)(y + 2);
    }
    fw_regs r = ser_call(f, ROM_SER_TX_PUT, a, x, y);               /* A = the table letter = $75 */
    uint8_t zp = fw_rom8(f, (uint16_t)(CMD_QUERY_TABLE + 1 + r.y)); /* LDA $9990,Y */
    fw_wr8(f, TN_RESUME_POS, r.x);                                  /* STX $77 */
    uint8_t v = fw_mem8(f, zp);                                     /* TAX; LDA $00,X */
    ser_call(f, ROM_SER_TX_HEX, v, fw_rd8(f, TN_RESUME_POS), r.y);  /* LDX $77 */
}

/* $ABD3 @c?: x = the '?'; the letter before it is read as $1AFF,X (boot_clause[x - 1]; x = 0 reads
   cmd_text[255]) */
void cmd_query(fw *f, uint8_t x, uint8_t a)
{
    (void)a;
    cmd_reply_setting(f, fw_rd8(f, (uint16_t)(BOOT_CLAUSE - 1 + x)), x);
}

/* $ABBE @L: 'L', then the 7 bank ids of 14 characters from bank_id_table, then CR (ser_tx_records) */
void cmd_list_languages(fw *f, uint8_t x, uint8_t a)
{
    fw_wr8(f, SER_REPLY_ID, 'L');
    fw_wr8(f, SER_REPLY_COUNT, 7);
    fw_wr8(f, SER_REPLY_SIZE, 14);
    fw_wr16(f, SER_REPLY_PTR, BANK_ID_TABLE);
    ser_call(f, ROM_SER_TX_RECORDS, a, x, cmd_handler_y(f));   /* LDM leaves A = boot_clause[x] */
}
