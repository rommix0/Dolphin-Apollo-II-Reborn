/* Dev CLI: run TEXT through the original firmware (emulated) and print the produced
   DSP frames, one per line, as 40 hex digits (spec §4 "produced frames"). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/rom_engine.h"
#include "engine/rom_id.h"

static machine g_machine;

static int usage(void)
{
    fprintf(stderr, "usage: apollo2_frames [--rom PATH] [--rom-any] [--boot] TEXT\n"
                    "  --rom PATH  ROM image (default: $APOLLO2_ROM or xtras/UK_V2.01_4.04_ROM.BIN)\n"
                    "  --rom-any   accept a ROM whose MD5 is not the UK V2.01 4.04 image (with a warning)\n"
                    "  --boot      also print frames produced during boot\n");
    return 1;
}

static bool load_rom(const char *path, uint8_t *rom)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    bool ok = fread(rom, 1, MACHINE_ROM_SIZE, f) == MACHINE_ROM_SIZE && fgetc(f) == EOF;
    fclose(f);
    return ok;
}

int main(int argc, char **argv)
{
    static uint8_t rom[MACHINE_ROM_SIZE];
    const char *rom_path = getenv("APOLLO2_ROM");
    const char *text = NULL;
    bool boot = false, rom_any = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rom") && i + 1 < argc)
            rom_path = argv[++i];
        else if (!strcmp(argv[i], "--rom-any"))
            rom_any = true;
        else if (!strcmp(argv[i], "--boot"))
            boot = true;
        else if (!text)
            text = argv[i];
        else
            return usage();
    }
    if (!text)
        return usage();
    if (!rom_path)
        rom_path = "xtras/UK_V2.01_4.04_ROM.BIN";
    if (!load_rom(rom_path, rom)) {
        fprintf(stderr, "cannot load a %d-byte ROM from %s\n", MACHINE_ROM_SIZE, rom_path);
        return 1;
    }
    if (!rom_id_accept(rom_path, rom, sizeof rom, rom_any, stderr))
        return 1;

    frame_list fl = { 0 };
    machine_status st = rom_engine_speak(&g_machine, rom, sizeof rom, text, boot, &fl);
    if (st == MACHINE_IDLE) {   /* never print a partial stream (spec section 4) */
        char *hex = frame_list_hex(&fl);
        fputs(hex, stdout);
        free(hex);
    }
    frame_list_free(&fl);
    if (st == MACHINE_IDLE && (g_machine.unmodelled_count || g_machine.unmodelled_dropped)) {
        fprintf(stderr, "the firmware made accesses the machine model does not cover:\n");
        machine_print_unmodelled(&g_machine, stderr);
        return 5;   /* spec section 4: never accept silently */
    }
    if (st == MACHINE_FAULT) {
        machine_dump(&g_machine, stderr);
        return 2;
    }
    if (st == MACHINE_CYCLE_LIMIT) {
        fprintf(stderr, "cycle limit reached before the firmware went idle\n");
        return 3;
    }
    return 0;
}
