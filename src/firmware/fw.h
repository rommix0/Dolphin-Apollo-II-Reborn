#ifndef FW_H
#define FW_H
/* Decompiled Apollo 2 firmware on the firmware's own memory image (M4.0 spec section 3).
   Nothing under src/firmware/ includes machine/ or cpu740/ headers: build.bat syntax-checks this directory
   without the src include path. */
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include "fw_ram.h"

#define FW_RAM_SIZE  0x4000
#define FW_TMP_78    0x0078   /* zero-page scratch, unnamed in the annotations: a saved Y, byte counters */
#define FW_TMP_75    0x0075   /* zero-page scratch, unnamed in the annotations: tn_convert_clause's saved X */
#define FW_TMP_76    0x0076   /* zero-page scratch, unnamed in the annotations: cmd_scan_clause's resume index,
                                 cmd_set_prosody's parameter index */
#define FW_TMP_34    0x0034   /* zero-page scratch, unnamed in the annotations: cmd_hex_byte's high digit */
#define FW_TMP_35    0x0035   /* zero-page scratch, unnamed in the annotations: cmd_hex_byte's low digit */
#define FW_TMP_1809  0x1809   /* scratch, unnamed in the annotations: cmd_hex_byte's high nibble */

/* Cycle budgets of a ROM call from C (M4.1 spec 3.1): a helper that does not return within FW_ROM_CALL_BUDGET
   is a fault; a call that plays speech (prosody and playback) gets FW_ROM_CALL_SPEECH_BUDGET. */
#define FW_ROM_CALL_BUDGET         10000000ULL
#define FW_ROM_CALL_SPEECH_BUDGET  1000000000ULL

typedef struct { uint8_t a, x, y, p; } fw_regs;

/* How a ROM call from C ended: returned; failed (a fault, or out of budget); or unwound: the ROM reset the
   stack past the call's own frame (bank_enter, M4.2 spec 3), so it can never return. */
typedef enum { FW_CALL_FAILED = 0, FW_CALL_RETURNED, FW_CALL_UNWOUND } fw_call_result;

/* The machine or native host. In hybrid runs it is the emulator (src/engine/thunks.c). */
typedef struct fw_host {
    void *ctx;
    /* Runs the routine at addr, still in ROM, to its RTS, within budget cycles. A, X and Y in and out; the
       routine starts with the machine's current flags, and p returns its exit flags. */
    fw_call_result (*rom_call)(void *ctx, uint16_t addr, fw_regs *regs, uint64_t budget);
    /* P2 bit 7, the lower-ROM window, as SEB7/CLB7 $F0 set and clear it. */
    void (*set_low_window)(void *ctx, bool on);
} fw_host;

/* Converted ROM data at its original addresses (built by src/firmware/data/fw_data.c). */
typedef struct fw_image {
    uint8_t rom[0x10000];          /* $4000-$FFFF with the window off; index = CPU address */
    uint8_t rom_ok[0x10000 / 8];   /* bit set = converted byte */
    uint8_t low[0x4000];           /* $4000-$7FFF with the window on; index = CPU address - $4000 */
    uint8_t low_ok[0x4000 / 8];
} fw_image;

typedef struct fw {
    uint8_t *ram;                  /* the firmware's RAM $0000-$3FFF (the SFR bytes $DA-$FF are not data) */
    const fw_image *img;
    const fw_host *host;           /* NULL: no ROM to call, no port to drive */
    bool low_window;               /* P2 bit 7: $4000-$7FFF read the LOW overlay */
    bool fault;                    /* a read outside the converted data, or a failed ROM call */
    uint16_t fault_addr;
    jmp_buf *trap;                 /* when set, a fault longjmps here and the routine is abandoned */
    bool unwound;                  /* a ROM call reset the stack: the C was abandoned (not a fault; M4.2 spec 3) */
    unsigned tn_flushes;           /* mid-clause flushes by tn_convert_clause (a statistic for the tests) */
} fw;

/* RAM by name; a < FW_RAM_SIZE. */
static inline uint8_t fw_rd8(const fw *f, uint16_t a) { return f->ram[a]; }
static inline void fw_wr8(fw *f, uint16_t a, uint8_t v) { f->ram[a] = v; }
static inline uint16_t fw_rd16(const fw *f, uint16_t a) { return (uint16_t)(f->ram[a] | f->ram[a + 1] << 8); }
static inline void fw_wr16(fw *f, uint16_t a, uint16_t v)
{
    f->ram[a] = (uint8_t)v;
    f->ram[a + 1] = (uint8_t)(v >> 8);
}

void fw_init(fw *f, uint8_t *ram, const fw_image *img, const fw_host *host);
/* A ROM data byte as the CPU reads it at addr (>= $4000) under the current window. A byte outside the
   converted data is a fault: it longjmps to f->trap when set, else reads $FF. */
uint8_t fw_rom8(fw *f, uint16_t addr);
/* A data byte anywhere: RAM below $4000 (an SFR address is a fault), else fw_rom8. */
uint8_t fw_mem8(fw *f, uint16_t addr);
/* (zp),Y: the byte at the little-endian pointer in zero page zp, plus y. */
uint8_t fw_ind_y(fw *f, uint8_t zp, uint8_t y);
/* SEB7/CLB7 P2: sets the window and tells the host. */
void fw_set_low_window(fw *f, bool on);
/* Calls a routine still in ROM through the host, within budget cycles. True when it returned. Without a host, or
   when the call fails, a fault. When the ROM reset the stack (unwound), f->unwound is set and the routine is
   abandoned like a fault (a longjmp to f->trap), but it is not a fault. */
bool fw_rom_call(fw *f, uint16_t addr, fw_regs *regs, uint64_t budget);

#endif
