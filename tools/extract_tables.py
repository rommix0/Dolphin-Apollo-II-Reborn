#!/usr/bin/env python3
"""One-time converter (M4.0 spec 4.2): the ROM's character rules, letter-to-sound rules, affix lists and
character classes -> readable C in src/firmware/data/. The output is a first draft: review it, annotate it by
hand if useful, and commit it. Nothing runs this at build or test time; tests/test_firmware_data.c checks the
committed C against the ROM.
usage: python tools/extract_tables.py [ROM]   (default $APOLLO2_ROM, else UK_V2.01_4.04_ROM.BIN)"""
import os
import sys

OUT = os.path.join("src", "firmware", "data")
# (name, address, size), as annotated in ghidra740/annotations/text_norm.txt and lts.txt
CHAR_TABLES = [
    ("tn_rules_ctrl", 0x477C, 96), ("tn_rules_punct", 0x47DC, 166), ("tn_rules_digits", 0x4882, 255),
    ("tn_rules_punct2", 0x4981, 69), ("lts_rules_letters", 0x49C6, 1610), ("tn_rules_symbols3", 0x5010, 295),
    ("tn_rules_lower", 0x5137, 78), ("tn_rules_symbols4", 0x5185, 55), ("tn_rules_high", 0x51BC, 1129),
]
PAIR_TABLES = [
    ("a", 0x5625, 3062), ("b", 0x621B, 1346), ("c", 0x675D, 2807), ("d", 0x7254, 1163), ("e", 0x76DF, 2148),
    ("f", 0x7F43, 698), ("g", 0x81FD, 782), ("h", 0x850B, 862), ("i", 0x8869, 1834), ("j", 0x8F93, 97),
    ("k", 0x4010, 131), ("l", 0x4093, 876), ("m", 0x43FF, 1279), ("n", 0x48FE, 580), ("o", 0x4B42, 1707),
    ("p", 0x51ED, 2353), ("q", 0x5B1E, 201), ("r", 0x5BE7, 1268), ("s", 0x60DB, 2503), ("t", 0x6AA2, 1194),
    ("u", 0x6F4C, 628), ("v", 0x71C0, 614), ("w", 0x7426, 706), ("x", 0x76E8, 74), ("y", 0x7732, 165),
    ("z", 0x77D7, 91),
]
AFFIXES = [("lts_suffixes", 0x9825, 60), ("lts_sibilants", 0x9861, 35), ("lts_at_consonants", 0x9884, 24)]
CLASS_BITS = [(0x80, "CC_LETTER"), (0x40, "CC_VOWEL"), (0x20, "CC_FRONT_VOWEL"), (0x10, "CC_NASAL"),
              (0x08, "CC_VOICED"), (0x04, "CC_CONSONANT"), (0x02, "CC_HIGH"), (0x01, "CC_PUNCT")]
HEADER = ("/* Converted from the Apollo 2 ROM by tools/extract_tables.py (M4.0 spec section 4), then reviewed.\n"
          "   tests/test_firmware_data.c re-encodes it and compares it byte for byte with the ROM. */\n"
          "#include <stddef.h>\n#include \"fw_data.h\"\n\n")


def cstr(b):
    s = '"'
    for c in b:
        if c in (0x22, 0x5C, 0x3F):           # " \ ? (no trigraphs)
            s += "\\" + chr(c)
        elif 0x20 <= c < 0x7F:
            s += chr(c)
        else:
            s += "\\%03o" % c
    return s + '"'


def char_name(c):
    if 0x20 < c < 0x7F and chr(c) not in "'\\":
        return "'%s' ($%02X)" % (chr(c), c)
    return "$%02X" % c


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("APOLLO2_ROM", "UK_V2.01_4.04_ROM.BIN")
    rom = open(path, "rb").read()
    assert len(rom) == 0x10000, "need the 64 KB ROM"

    def rd(a, low):
        return rom[a - 0x4000] if low else rom[a]

    def word(a):
        return rom[a] | rom[a + 1] << 8

    def parse(addr, size, low):
        lists, p, end = [], addr, addr + size
        while p < end:
            start, rules = p, []
            while True:
                left = []
                while rd(p, low) >= 0x1E:
                    left.append(rd(p, low)); p += 1
                n = rd(p, low); p += 1
                right = []
                while rd(p, low) >= 0x1E:
                    right.append(rd(p, low)); p += 1
                assert n <= len(right), "rule at $%04X consumes more than its right context" % start
                m = rd(p, low); p += 1
                out = bytes(rd(p + i, low) for i in range(m)); p += m
                assert 0 not in out, "rule at $%04X has a $00 in its output" % start
                rules.append((bytes(reversed(left)), bytes(right[:n]), bytes(right[n:]), out))
                if rd(p, low) == 0xFF:
                    p += 1
                    break
            lists.append((start, rules))
        assert p == end, "table at $%04X: lists end at $%04X, the annotation says $%04X" % (addr, p, end)
        return lists

    def encode(rules):
        b = bytearray()
        for left, cons, right, out in rules:
            b += bytes(reversed(left)) + bytes([len(cons)]) + cons + right + bytes([len(out)]) + out
        return bytes(b) + b"\xff"

    def emit_list(lines, lid, title, rules):
        lines.append("/* %s */" % title)
        lines.append("static const fw_rule r_%s[] = {" % lid)
        for left, cons, right, out in rules:
            lines.append("    { %s, %s, %s, %s }," % (cstr(left), cstr(cons), cstr(right), cstr(out)))
        lines.append("};")
        lines.append("static const fw_rule_list l_%s = { r_%s, %d };" % (lid, lid, len(rules)))
        lines.append("")

    def emit_table(lines, name, addr, size, low, ids):
        lines.append("static const fw_rule_list *const k_%s[] = {" % name)
        for i in range(0, len(ids), 6):
            lines.append("    " + " ".join("&l_%s," % x for x in ids[i:i + 6]))
        lines.append("};")
        lines.append("const fw_rule_table %s = { \"%s\", 0x%04X, %d, %s, k_%s, %d };"
                     % (name, name, addr, size, "true" if low else "false", name, len(ids)))
        lines.append("")

    # Character rules ($477C-$5624): lists named by the first character that points at them.
    char_ptr = [rom[0x4000 + c] | rom[0x4100 + c] << 8 for c in range(256)]
    refs = {}
    for c, p in enumerate(char_ptr):
        refs.setdefault(p, []).append(c)
    lines = [HEADER]
    for name, addr, size in CHAR_TABLES:
        lists = parse(addr, size, False)
        for start, rules in lists:
            assert rom[start:start + len(encode(rules))] == encode(rules)
        ids = []
        for start, rules in lists:
            if start in refs:
                lid = "c%02x" % refs[start][0]
                title = "%s at $%04X, the list of %s" % (name, start, ", ".join(char_name(c) for c in refs[start]))
            else:
                lid = "unref_%04x" % start
                title = "%s at $%04X: no character points here" % (name, start)
            emit_list(lines, lid, title, rules)
            ids.append(lid)
        emit_table(lines, name, addr, size, False, ids)
    lines.append("const fw_rule_list *const tn_char_lists[256] = {")
    for c in range(256):
        lines.append("    [0x%02X] = &l_c%02x,   /* %s */" % (c, refs[char_ptr[c]][0], char_name(c)))
    lines.append("};")
    with open(os.path.join(OUT, "tn_rule_data.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")

    # Two-letter rules: lists named by the pairs that point at them.
    for k in range(26):
        assert word(0x4200 + 2 * k) == 0x4234 + 52 * k, "lts_pair_index is not the plain table sequence"
    pair_ptr = [[word(0x4234 + 52 * k + 2 * s) for s in range(26)] for k in range(26)]
    for group, keys in (("aj", PAIR_TABLES[:10]), ("kz", PAIR_TABLES[10:])):
        lines = [HEADER]
        for key, addr, size in keys:
            k = ord(key) - ord("a")
            low = k >= 10
            lists = parse(addr, size, low)
            prefs = {}
            for s, p in enumerate(pair_ptr[k]):
                if p:
                    prefs.setdefault(p, []).append(s)
            ids, by_addr = [], {}
            for start, rules in lists:
                if start in prefs:
                    lid = "_".join(key + chr(ord("a") + s) for s in prefs[start])
                    title = "lts_rules_%s at %s$%04X: %s" % (key, "LOW " if low else "", start,
                                                             ", ".join((key + chr(ord("a") + s)).upper()
                                                                       for s in prefs[start]))
                else:
                    lid = "%s_unref_%04x" % (key, start)
                    title = "lts_rules_%s at %s$%04X: no pair entry points here" % (key, "LOW " if low else "", start)
                emit_list(lines, lid, title, rules)
                ids.append(lid)
                by_addr[start] = lid
            emit_table(lines, "lts_rules_" + key, addr, size, low, ids)
            lines.append("const fw_rule_list *const lts_pairs_%s[26] = {" % key)
            for s, p in enumerate(pair_ptr[k]):
                if p:
                    assert p in by_addr, "pair %s%s points into a list" % (key, chr(ord("a") + s))
                    lines.append("    ['%s' - 'A'] = &l_%s," % (chr(ord("A") + s), by_addr[p]))
            lines.append("};")
            lines.append("")
        with open(os.path.join(OUT, "lts_rule_data_%s.c" % group), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines) + "\n")

    # Pair index, rule table list, affix lists, character classes.
    lines = [HEADER]
    lines.append("const fw_rule_list *const *const lts_pair_lists[26] = {")
    lines.append("    " + ", ".join("lts_pairs_%s" % chr(ord("a") + k) for k in range(26)) + ",")
    lines.append("};")
    lines.append("")
    lines.append("const fw_rule_table *const fw_rule_tables[] = {")
    for name, _, _ in CHAR_TABLES:
        lines.append("    &%s," % name)
    for key, _, _ in PAIR_TABLES:
        lines.append("    &lts_rules_%s," % key)
    lines.append("};")
    lines.append("const size_t fw_rule_table_count = sizeof fw_rule_tables / sizeof fw_rule_tables[0];")
    lines.append("")
    for name, addr, size in AFFIXES:
        body = rom[addr:addr + size]
        assert body[:3] == b"\x00\r\n" and body[-1:] == b"\x00", name
        entries = body[3:-1].split(b"\r\n")
        assert entries[-1] == b"", name
        entries = entries[:-1]
        lines.append("static const char *const k_%s[] = { %s };" % (name, ", ".join(cstr(e) for e in entries)))
    lines.append("const fw_affix_list fw_affix_lists[] = {")
    for name, addr, size in AFFIXES:
        n = len(rom[addr + 3:addr + size - 1].split(b"\r\n")) - 1
        lines.append("    { \"%s\", 0x%04X, k_%s, %d }," % (name, addr, name, n))
    lines.append("};")
    lines.append("const size_t fw_affix_list_count = sizeof fw_affix_lists / sizeof fw_affix_lists[0];")
    lines.append("")
    lines.append("const uint8_t lts_char_class[256] = {")
    for c in range(256):
        v = rom[0x9AB4 + c]
        if v:
            names = [n for bit, n in CLASS_BITS if v & bit]
            lines.append("    [0x%02X] = %s,   /* %s */" % (c, " | ".join(names), char_name(c)))
    lines.append("};")
    with open(os.path.join(OUT, "lts_table_data.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote tn_rule_data.c, lts_rule_data_aj.c, lts_rule_data_kz.c, lts_table_data.c in", OUT)


if __name__ == "__main__":
    main()
