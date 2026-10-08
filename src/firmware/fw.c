#include "fw.h"

void fw_init(fw *f, uint8_t *ram, const fw_image *img, const fw_host *host)
{
    f->ram = ram;
    f->img = img;
    f->host = host;
    f->low_window = false;
    f->fault = false;
    f->fault_addr = 0;
    f->trap = NULL;
    f->unwound = false;
    f->tn_flushes = 0;
}

static void fault(fw *f, uint16_t addr)
{
    if (!f->fault) {
        f->fault = true;
        f->fault_addr = addr;
    }
    if (f->trap)
        longjmp(*f->trap, 1);
}

static bool converted(const uint8_t *ok, uint32_t i) { return (ok[i >> 3] >> (i & 7)) & 1; }

uint8_t fw_rom8(fw *f, uint16_t addr)
{
    const fw_image *img = f->img;
    if (img && addr >= 0x4000) {
        if (f->low_window && addr < 0x8000) {
            if (converted(img->low_ok, addr - 0x4000u))
                return img->low[addr - 0x4000u];
        } else if (converted(img->rom_ok, addr)) {
            return img->rom[addr];
        }
    }
    fault(f, addr);
    return 0xFF;
}

uint8_t fw_mem8(fw *f, uint16_t addr)
{
    if (addr >= 0x4000)
        return fw_rom8(f, addr);
    if (addr >= 0xDA && addr <= 0xFF) {
        fault(f, addr);
        return 0xFF;
    }
    return f->ram[addr];
}

uint8_t fw_ind_y(fw *f, uint8_t zp, uint8_t y)
{
    return fw_mem8(f, (uint16_t)(fw_rd16(f, zp) + y));
}

void fw_wr_ind_y(fw *f, uint8_t zp, uint8_t y, uint8_t v)
{
    uint16_t addr = (uint16_t)(fw_rd16(f, zp) + y);
    if (addr >= FW_RAM_SIZE || (addr >= 0xDA && addr <= 0xFF)) {
        fault(f, addr);
        return;
    }
    f->ram[addr] = v;
}

uint8_t fw_sfr8(fw *f, uint8_t addr)
{
    if (addr >= 0xDA && f->host && f->host->read_sfr)
        return f->host->read_sfr(f->host->ctx, addr);
    fault(f, addr);
    return 0xFF;
}

void fw_fault(fw *f, uint16_t addr) { fault(f, addr); }

void fw_set_low_window(fw *f, bool on)
{
    f->low_window = on;
    if (f->host && f->host->set_low_window)
        f->host->set_low_window(f->host->ctx, on);
}

bool fw_rom_call(fw *f, uint16_t addr, fw_regs *regs, uint64_t budget)
{
    fw_call_result r = FW_CALL_FAILED;
    if (f->host && f->host->rom_call)
        r = f->host->rom_call(f->host->ctx, addr, regs, budget);
    if (r == FW_CALL_RETURNED)
        return true;
    if (r == FW_CALL_UNWOUND) {
        f->unwound = true;   /* the ROM abandoned this call chain, and so does the C */
        if (f->trap)
            longjmp(*f->trap, 1);
        return false;
    }
    fault(f, addr);
    return false;
}
