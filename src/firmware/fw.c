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

void fw_set_low_window(fw *f, bool on)
{
    f->low_window = on;
    if (f->host && f->host->set_low_window)
        f->host->set_low_window(f->host->ctx, on);
}

bool fw_rom_call(fw *f, uint16_t addr, fw_regs *regs, uint64_t budget)
{
    if (!f->host || !f->host->rom_call || !f->host->rom_call(f->host->ctx, addr, regs, budget)) {
        fault(f, addr);
        return false;
    }
    return true;
}
