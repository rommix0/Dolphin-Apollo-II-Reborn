#include <math.h>
#include <string.h>
#include "engine/rom_id.h"

/* MD5 (RFC 1321). The sine table is computed: K[i] = floor(|sin(i + 1)| * 2^32). */
void rom_id_md5(const uint8_t *msg, size_t len, uint8_t out[16])
{
    static const uint8_t k_shift[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
    };
    uint32_t k[64];
    for (int i = 0; i < 64; i++)
        k[i] = (uint32_t)(fabs(sin(i + 1.0)) * 4294967296.0);
    uint32_t h[4] = { 0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u };
    uint64_t bits = (uint64_t)len * 8;
    size_t total = ((len + 8) / 64 + 1) * 64;   /* message, $80, zeros, 8-byte length */
    for (size_t off = 0; off < total; off += 64) {
        uint32_t w[16];
        for (int i = 0; i < 16; i++) {
            uint32_t word = 0;
            for (int j = 3; j >= 0; j--) {
                size_t p = off + (size_t)(4 * i + j);
                uint8_t b = p < len ? msg[p] : p == len ? 0x80
                          : p >= total - 8 ? (uint8_t)(bits >> (8 * (p - (total - 8)))) : 0;
                word = word << 8 | b;
            }
            w[i] = word;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; i++) {
            uint32_t f;
            int g;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }
            uint32_t x = a + f + k[i] + w[g];
            a = d;
            d = c;
            c = b;
            b = b + (x << k_shift[i] | x >> (32 - k_shift[i]));
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
    }
    for (int i = 0; i < 16; i++)
        out[i] = (uint8_t)(h[i / 4] >> (8 * (i % 4)));
}

void rom_id_md5_hex(const uint8_t *data, size_t len, char out[33])
{
    static const char k_hex[] = "0123456789abcdef";
    uint8_t d[16];
    rom_id_md5(data, len, d);
    for (int i = 0; i < 16; i++) {
        out[2 * i] = k_hex[d[i] >> 4];
        out[2 * i + 1] = k_hex[d[i] & 15];
    }
    out[32] = '\0';
}

bool rom_id_is_apollo2(const uint8_t *rom, size_t len)
{
    char hex[33];
    if (len != 0x10000)
        return false;
    rom_id_md5_hex(rom, len, hex);
    return strcmp(hex, ROM_ID_MD5) == 0;
}

bool rom_id_accept(const char *path, const uint8_t *rom, size_t len, bool any, FILE *err)
{
    char hex[33];
    if (rom_id_is_apollo2(rom, len))
        return true;
    rom_id_md5_hex(rom, len, hex);
    if (any) {
        fprintf(err, "warning: %s (MD5 %s) is not the UK V2.01 4.04 ROM; frames may differ from the goldens\n",
                path, hex);
        return true;
    }
    fprintf(err, "%s has MD5 %s, not the UK V2.01 4.04 ROM (%s); pass --rom-any to use it anyway\n",
            path, hex, ROM_ID_MD5);
    return false;
}
