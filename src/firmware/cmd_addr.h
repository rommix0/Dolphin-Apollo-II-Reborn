#ifndef FW_CMD_ADDR_H
#define FW_CMD_ADDR_H
/* ROM code addresses of the command parser (M4.2 spec 2), checked against build/ghidra740/dump. They are code,
   not data: the pattern records hold the handler addresses, the scan copies them into cmd_handlers and the event
   producers write the event handlers into cmd_ev_handlers, byte for byte; the C dispatcher looks them
   up (k_handlers). */

/* Deferred command handlers, in pattern order: entered by cmd_run_next's JMP (cmd_vector) with X = the argument's
   boot_clause index and A = boot_clause[X]; each ends with JMP cmd_run_done ($A8BD) */
#define ROM_CMD_SAY_VERSION       0xA902   /* "?" */
#define ROM_CMD_QUERY             0xABD3   /* "*?" */
#define ROM_CMD_SET_VOLUME        0xAB05   /* "A*" */
#define ROM_CMD_SET_PHRASE_PAUSE  0xA91F   /* "D*" */
#define ROM_CMD_ECHO              0xAB99   /* "E*" */
#define ROM_CMD_SET_PITCH         0xAA24   /* "F*" */
#define ROM_CMD_SET_HYPER         0xAB4D   /* "H#" */
#define ROM_CMD_SET_MARK_SPACE    0xA99A   /* "M**" */
#define ROM_CMD_SET_PUNCT         0xAAF5   /* "P#" */
#define ROM_CMD_SET_WORD_PAUSE    0xA98A   /* "Q#" */
#define ROM_CMD_SET_PROSODY       0xA9DC   /* "R#", "R***" */
#define ROM_CMD_SET_SPELL         0xAA4F   /* "S#" */
#define ROM_CMD_SET_VOICE         0xAA74   /* "V#" */
#define ROM_CMD_SET_SPEED         0xA9C9   /* "W#" */
#define ROM_CMD_SET_PHONETIC      0xAB9F   /* "X#" */
#define ROM_CMD_SET_FORMANT       0xA92F   /* "U#*+", "U#*-", "U#**+", "U#**-" */
#define ROM_CMD_SET_SOURCE        0xAB5D   /* "$#" */
#define ROM_CMD_SET_VOICING       0xAB70   /* "B#" */
#define ROM_CMD_MARK_INDEX        0xAB2E   /* "I+" */
#define ROM_CMD_SOUND             0xA8C0   /* "T*" */
#define ROM_BANK_CMD_SELECT       0xABAF   /* "=#": bank switching (P2), stays in ROM */
#define ROM_CMD_LIST_LANGUAGES    0xABBE   /* "L" */
#define ROM_CMD_SET_SPEAKER       0xAA61   /* "K#" */
#define ROM_CMD_IGNORE            0xABDC   /* "G#" (listed twice in cmd_table), "I^", "+*" */

/* Immediate command handlers: cmd_imm_match's JMP (cmd_imm_vector) at interrupt level (stay in ROM) */
#define ROM_CMD_IMM_INDEX_ADD     0xC483   /* "I+" */
#define ROM_CMD_IMM_INDEX_QUERY   0xC495   /* "I?" */

/* Command event handlers: queued in cmd_ev_handlers by the event producers, run at playback by cmd_run_event
   (stay in ROM) */
#define ROM_CMD_EV_INDEX          0xBBA0   /* cmd_mark_index */
#define ROM_CMD_EV_SOUND_START    0xBBA7   /* cmd_sound, first event */
#define ROM_CMD_EV_SOUND_END      0xBC80   /* cmd_sound, second event */
#define ROM_CMD_EV_SET_VOICING    0xBCEA   /* cmd_set_voicing */
#define ROM_CMD_EV_SET_VOLUME     0xBCED   /* cmd_set_volume */

/* ROM routines the C calls through fw_rom_call (isr_par_ready: ROM_ISR_PAR_READY in tn_convert.h) */
#define ROM_CMD_RUN_NEXT          0xA893   /* dispatch cmd_handlers[cmd_next] (the dispatcher's fallback); X kept */
#define ROM_PAR_SETUP_VOICE       0xB3A4   /* rebuild the voice set-up and both DSP frames (M4.4/M4.5) */
#define ROM_SER_TX_PUT            0xC4DD   /* queue A as a reply byte; A, X, Y preserved */
#define ROM_SER_TX_HEX            0xC571   /* queue A as two ASCII hex digits, low nibble first; A preserved */
#define ROM_SER_TX_RECORDS        0xC5A4   /* queue the record reply ser_reply_id/_count/_size/_ptr describe */

#endif
