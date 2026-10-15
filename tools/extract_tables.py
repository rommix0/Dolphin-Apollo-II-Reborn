#!/usr/bin/env python3
"""One-time converter (M4.0 spec 4.2): the ROM's character rules, letter-to-sound rules, affix lists and
character classes, the phonetic texts (M4.1), the command tables (M4.2) and the prosody tables (M4.3a) -> readable
C in src/firmware/data/. The output is a first draft: review it, annotate it by hand if useful, and commit it.
Nothing runs this at build or test time; tests/test_firmware_data.c checks the committed C against the ROM.
usage: python tools/extract_tables.py [--phonetic-only | --command-only | --prosody-only] [ROM]
       (ROM: default $APOLLO2_ROM, else xtras/UK_V2.01_4.04_ROM.BIN)"""
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
# (name, address, size), as annotated in ghidra740/annotations/command.txt and boot.txt (M4.1 spec 4)
PHONETIC_TEXTS = [
    ("cmd_version_engine_text", 0x9804, 32), ("boot_baud_text_19200", 0xACC7, 3),
    ("boot_baud_text_300", 0xACCA, 14), ("boot_baud_text_1200", 0xACD8, 16),
    ("cmd_version_lang_text", 0xF75B, 32),
]
# The command data (M4.2 spec 4), as annotated in ghidra740/annotations/command.txt and params.txt
CMD_IMM_PATTERNS, CMD_IMM_TABLE, CMD_PATTERNS, CMD_TABLE = 0x989C, 0x98A7, 0x98AD, 0x994F
CMD_QUERY_TABLE, CMD_CASE_MAP = 0x998F, 0x99B4
CMD_SPEED_MAPS, CMD_SPEED_LIMITS, CMD_SPEED_MAP_DATA = 0xC87B, 0xC88F, 0xC898
PAR_VOICE_A_COUNT, PAR_VOICE_B_COUNT = 0xD469, 0xE5E2
# The handlers of the pattern records, named as in src/firmware/cmd_addr.h
CMD_HANDLERS = {
    0xC483: "ROM_CMD_IMM_INDEX_ADD", 0xC495: "ROM_CMD_IMM_INDEX_QUERY",
    0xA902: "ROM_CMD_SAY_VERSION", 0xABD3: "ROM_CMD_QUERY", 0xAB05: "ROM_CMD_SET_VOLUME",
    0xA91F: "ROM_CMD_SET_PHRASE_PAUSE", 0xAB99: "ROM_CMD_ECHO", 0xAA24: "ROM_CMD_SET_PITCH",
    0xAB4D: "ROM_CMD_SET_HYPER", 0xA99A: "ROM_CMD_SET_MARK_SPACE", 0xAAF5: "ROM_CMD_SET_PUNCT",
    0xA98A: "ROM_CMD_SET_WORD_PAUSE", 0xA9DC: "ROM_CMD_SET_PROSODY", 0xAA4F: "ROM_CMD_SET_SPELL",
    0xAA74: "ROM_CMD_SET_VOICE", 0xA9C9: "ROM_CMD_SET_SPEED", 0xAB9F: "ROM_CMD_SET_PHONETIC",
    0xA92F: "ROM_CMD_SET_FORMANT", 0xAB5D: "ROM_CMD_SET_SOURCE", 0xAB70: "ROM_CMD_SET_VOICING",
    0xAB2E: "ROM_CMD_MARK_INDEX", 0xA8C0: "ROM_CMD_SOUND", 0xABAF: "ROM_BANK_CMD_SELECT",
    0xABBE: "ROM_CMD_LIST_LANGUAGES", 0xAA61: "ROM_CMD_SET_SPEAKER", 0xABDC: "ROM_CMD_IGNORE",
}
# The prosody data (M4.3a spec 3), as annotated in ghidra740/annotations/phoneme.txt and command.txt
PH_SYMBOL_CLASS, PH_SYMBOL_FEATURES, PH_LIST_PTRS, PH_LISTS = 0x9BB4, 0x9CB4, 0x9D74, 0xA187
UNUSED_A397, PH_DUR_LENGTHEN, PH_DUR_SHORTEN, PH_LENGTHEN_STEPS = 0xA397, 0xC67B, 0xC77B, 0xD44A
PH_WORD_PAUSE, PH_APPEND_PHRASE_PAUSE = 0x5F, 0xC9B8
PH_CLASS_BITS = [(0x80, "PHC_PHONETIC"), (0x40, "PHC_VOWEL"), (0x08, "PHC_KIND_3"), (0x04, "PHC_KIND_2"),
                 (0x02, "PHC_KIND_1"), (0x01, "PHC_PUNCT")]
PH_FEATURE_BITS = [(0x80, "PHF_VOWEL"), (0x40, "PHF_CONSONANT"), (0x20, "PHF_STOP"), (0x10, "PHF_FRICATIVE"),
                   (0x08, "PHF_NASAL"), (0x04, "PHF_LIQUID"), (0x02, "PHF_LONG"), (0x01, "PHF_VOICED")]
# What phoneme.txt says about single lists (it numbers the phonemes, it names none)
PH_LIST_NOTES = {
    0x20: "the space: only the word-pause item, a pause of @Q length when @Q is not 0",
    0x83: "the only list with phoneme 7; only @X phonetic text reaches it",
    0x84: "the only list with phoneme 6; only @X phonetic text reaches it",
    0x87: "the only list with phoneme 22; only @X phonetic text reaches it",
    0x8C: "the only list with phoneme 40; only @X phonetic text reaches it",
    0x98: "the only list with phoneme 23; from letter-to-sound rules (MEGATON)",
    0xC0: "the only list with phoneme 41, a long syllabic consonant; from letter-to-sound rules (LEVEL)",
}
CLASS_BITS = [(0x80, "CC_LETTER"), (0x40, "CC_VOWEL"), (0x20, "CC_FRONT_VOWEL"), (0x10, "CC_NASAL"),
              (0x08, "CC_VOICED"), (0x04, "CC_CONSONANT"), (0x02, "CC_HIGH"), (0x01, "CC_PUNCT")]
HEADER = ("/* Converted from the Apollo 2 ROM by tools/extract_tables.py (M4.0 spec section 4), then reviewed.\n"
          "   tests/test_firmware_data.c re-encodes it and compares it byte for byte with the ROM. */\n"
          "#include <stddef.h>\n#include \"fw_data.h\"\n\n")
# The two-letter rule files' header: HEADER with what their three bytes above $7F (octal escapes) mean
LTS_HEADER = HEADER.replace(
    " with the ROM. */\n",
    " with the ROM.\n"
    "   Three bytes above $7F appear in the two-letter rules, as octal escapes. \\220 ($90), among the characters a\n"
    "   rule consumes (CAF\\220, under CA), is E acute in code page 437 (cmd_case_map folds the lower-case $82 to"
    " it):\n"
    "   lts_char_class has it a letter, so lts_match_symbol matches it against the text like any letter. The other\n"
    "   two are in outputs, phonetic symbols that tn_append_phonetic copies into tn_phonetic like any other (their\n"
    "   features and phoneme lists are in pros_data.c). \\230 ($98), under ME, is a voiced stop (the G of MEGATON):\n"
    "   its list, phonemes 21 and 23, is the only one with phoneme 23. \\300 ($C0) is a long syllabic consonant (the\n"
    "   final L of LEVEL, CANCEL): its list is phoneme 41 alone. */\n", 1)


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


def write_phonetic_texts(rom):
    """src/firmware/data/phonetic_text_data.c: each text as its counted text plus the bytes after it (tail)."""
    lines = [HEADER.replace("M4.0 spec section 4", "M4.1 spec section 4") +
             "/* name, address, annotated size, counted text, tail (bytes after the count) */",
             "const fw_phonetic_text fw_phonetic_texts[] = {"]
    for name, addr, size in PHONETIC_TEXTS:
        n = rom[addr]
        assert 0 < n and 1 + n <= size, "%s: length %d does not fit %d bytes" % (name, n, size)
        text, tail = rom[addr + 1:addr + 1 + n], rom[addr + 1 + n:addr + size]
        lines.append('    { "%s", 0x%04X, %d, %s, %s },' % (name, addr, size, cstr(text), cstr(tail)))
    lines += ["};", "const size_t fw_phonetic_text_count = sizeof fw_phonetic_texts / sizeof fw_phonetic_texts[0];"]
    with open(os.path.join(OUT, "phonetic_text_data.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote phonetic_text_data.c in", OUT)


def cchar(c):
    """A code as a C character constant if it is printable ASCII, else in hex."""
    if 0x20 <= c < 0x7F and chr(c) not in "'\\":
        return "'%s'" % chr(c)
    return "0x%02X" % c


def zero_page_names():
    """Address -> name of the zero-page variables, from src/firmware/fw_ram.h."""
    names = {}
    with open(os.path.join("src", "firmware", "fw_ram.h"), encoding="utf-8") as f:
        for line in f:
            p = line.split()
            if len(p) >= 3 and p[0] == "#define" and p[2].startswith("0x") and int(p[2], 16) < 0x100:
                names[int(p[2], 16)] = p[1]
    return names


def write_command_data(rom):
    """src/firmware/data/cmd_data.c: the command parser's tables (M4.2 spec 4). The pattern records are kept;
    the $FF ends, the pointer tables and the query table's 0 end are left to the encoder."""
    def word(a):
        return rom[a] | rom[a + 1] << 8

    def records(at):
        """The pattern records from at to the $FF end: [(address, pattern, handler)], and the end's address."""
        recs = []
        while rom[at] != 0xFF:
            n = rom.index(0, at) - at
            assert n > 0, "empty pattern at $%04X" % at
            recs.append((at, rom[at:at + n], word(at + n + 1)))
            at += n + 3
        return recs, at

    def pointers(at, recs, end):
        """The pointer table at at, up to the pointer to end: indices into recs, and the address after it."""
        starts, order = [a for a, _, _ in recs], []
        while word(at) != end:
            assert word(at) in starts, "pointer at $%04X is no pattern record" % at
            order.append(starts.index(word(at)))
            at += 2
        return order, at + 2

    def emit_records(lines, name, recs):
        lines.append("const fw_cmd_pattern %s[] = {" % name)
        for i, (a, pat, h) in enumerate(recs):
            assert h in CMD_HANDLERS, "pattern at $%04X: handler $%04X has no name" % (a, h)
            entry = "    { %s, %s }," % (cstr(pat), CMD_HANDLERS[h])
            lines.append(entry.ljust(48) + "/* %2d: $%04X */" % (i, a))
        lines.append("};")
        lines.append("const size_t %s_count = sizeof %s / sizeof %s[0];" % (name[:-1], name, name))
        lines.append("")

    head = HEADER.replace("M4.0 spec section 4", "M4.2 spec section 4")
    lines = [head.replace('#include "fw_data.h"\n', '#include "fw_data.h"\n#include "../cmd_addr.h"\n').rstrip("\n")
             + "\n"]
    imm, end = records(CMD_IMM_PATTERNS)
    order, at = pointers(CMD_IMM_TABLE, imm, end)
    assert end + 1 == CMD_IMM_TABLE and order == list(range(len(imm))) and at == CMD_PATTERNS, "cmd_imm_table"
    lines += ["/* cmd_imm_patterns ($%04X): the immediate commands, matched at interrupt level by cmd_imm_match."
              % CMD_IMM_PATTERNS,
              "   The encoder writes the $FF end, then cmd_imm_table ($%04X): each record's address, then the"
              % CMD_IMM_TABLE,
              "   end's. */"]
    emit_records(lines, "fw_cmd_imm_patterns", imm)

    recs, end = records(CMD_PATTERNS)
    order, at = pointers(CMD_TABLE, recs, end)
    assert end + 1 == CMD_TABLE and at == CMD_QUERY_TABLE, "cmd_table"
    lines += ["/* The deferred command patterns ($%04X), in ROM order: * matches any character, # a digit, both on"
              % CMD_PATTERNS,
              "   the case-mapped text. A record is the pattern, its 0 end and its handler; the encoder writes the",
              "   $FF end after the last. */"]
    emit_records(lines, "fw_cmd_patterns", recs)
    lines += ["/* cmd_table ($%04X): the patterns in match order (first match wins), as indices into" % CMD_TABLE,
              "   fw_cmd_patterns. The encoder writes each record's address, then the address of the $FF end. */",
              "const uint8_t fw_cmd_table[] = {"]
    seen = set()
    for i, k in enumerate(order):
        again = " again: the ROM lists this record twice" if k in seen else ""
        seen.add(k)
        lines.append(("    %d," % k).ljust(10) + "/* %2d: \"%s\"%s */" % (i, recs[k][1].decode("ascii"), again))
    lines += ["};", "const size_t fw_cmd_table_count = sizeof fw_cmd_table / sizeof fw_cmd_table[0];", ""]

    zp, at, query = zero_page_names(), CMD_QUERY_TABLE, []
    while rom[at]:
        assert rom[at + 1] in zp, "cmd_query_table at $%04X: no RAM name for $%02X" % (at, rom[at + 1])
        query.append((rom[at], zp[rom[at + 1]]))
        at += 2
    assert at + 1 == CMD_CASE_MAP, "cmd_query_table"
    lines += ["/* cmd_query_table ($%04X): the settings a @c? query reports, as the letter and the zero-page"
              % CMD_QUERY_TABLE,
              "   setting. The encoder writes the 0 end. */",
              "const fw_cmd_query fw_cmd_query_table[] = {"]
    lines += ["    { %s, %s }," % (cchar(letter), name) for letter, name in query]
    lines += ["};", "const size_t fw_cmd_query_count = sizeof fw_cmd_query_table / sizeof fw_cmd_query_table[0];", ""]

    cmap, folds, c = rom[CMD_CASE_MAP:CMD_CASE_MAP + 256], [], 0
    while c < 256:
        if cmap[c] == c:
            c += 1
            continue
        shift = c < 255 and cmap[c + 1] == cmap[c] + 1 and cmap[c + 1] != c + 1
        last = c
        while last < 255 and cmap[last + 1] != last + 1 and \
                cmap[last + 1] == (cmap[c] + last + 1 - c if shift else cmap[c]):
            last += 1
        folds.append((c, last, cmap[c], shift))
        c = last + 1
    lines += ["/* cmd_case_map ($%04X): the identity, except these folds { first, last, to, shift }: the codes"
              % CMD_CASE_MAP,
              "   first..last map to to, or with shift each code c to to + (c - first). The encoder writes all",
              "   256 codes. */",
              "const fw_case_fold fw_cmd_case_folds[] = {"]
    for first, last, to, shift in folds:
        span = "$%02X" % first if first == last else "$%02X-$%02X" % (first, last)
        dest = "$%02X-$%02X" % (to, to + last - first) if shift else "space" if to == 0x20 else "$%02X" % to
        entry = "    { %s, %s, %s, %s }," % (cchar(first), cchar(last), cchar(to), "true" if shift else "false")
        lines.append(entry.ljust(40) + "/* %s to %s */" % (span, dest))
    lines += ["};", "const size_t fw_cmd_case_fold_count = sizeof fw_cmd_case_folds / sizeof fw_cmd_case_folds[0];",
              ""]

    assert CMD_SPEED_MAPS + 20 == CMD_SPEED_LIMITS and CMD_SPEED_LIMITS + 9 == CMD_SPEED_MAP_DATA
    maps = [(word(CMD_SPEED_MAPS + 2 * i) - CMD_SPEED_MAP_DATA) // 32 for i in range(10)]
    assert all(word(CMD_SPEED_MAPS + 2 * i) == CMD_SPEED_MAP_DATA + 32 * k and 0 <= k < 9
               for i, k in enumerate(maps)), "cmd_speed_maps"
    lines += ["/* cmd_speed_maps ($%04X): per speed 1-9 (indexed by speed - 1) its 32-byte duration map in"
              % CMD_SPEED_MAPS,
              "   cmd_speed_map_data, which is prosody's (M4.3) and not converted here */",
              "const uint16_t fw_cmd_speed_maps[10] = {"]
    for i, k in enumerate(maps):
        note = "speed %d" % (i + 1) if k == i else "entry %d repeats entry %d" % (i + 1, k + 1)
        lines.append(("    CMD_SPEED_MAP_DATA + 32 * %d," % k).ljust(36) + "/* %s */" % note)
    lines += ["};",
              "/* cmd_speed_limits ($%04X): per speed 1-9 the value cmd_apply_speed puts in cmd_speed_limit */"
              % CMD_SPEED_LIMITS,
              "const uint8_t fw_cmd_speed_limits[9] = { %s };"
              % ", ".join(str(v) for v in rom[CMD_SPEED_LIMITS:CMD_SPEED_LIMITS + 9]),
              "",
              "/* par_voice_a_count ($%04X), par_voice_b_count ($%04X): the phoneme counts at the head of the"
              % (PAR_VOICE_A_COUNT, PAR_VOICE_B_COUNT),
              "   voice tables (M4.4), read by cmd_select_voice_a and cmd_select_voice_b */",
              "const uint8_t fw_par_voice_a_count = %d, fw_par_voice_b_count = %d;"
              % (rom[PAR_VOICE_A_COUNT], rom[PAR_VOICE_B_COUNT])]
    with open(os.path.join(OUT, "cmd_data.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote cmd_data.c in", OUT)


def write_prosody_data(rom):
    """src/firmware/data/pros_data.c: the tables of the phoneme expansion and the phrase generator (M4.3a spec 3).
    The list pointers and the lists' $FF ends are left to the encoder."""
    def word(a):
        return rom[a] | rom[a + 1] << 8

    def bits(v, names):
        assert v & ~sum(b for b, _ in names) == 0, "value $%02X has an unnamed bit" % v
        return " | ".join(n for b, n in names if v & b)

    def numbers(vals, per, what, first=0):
        """Rows of per decimal numbers, each with the index range it holds (the first value's index is first)."""
        out = []
        for i in range(0, len(vals), per):
            row = vals[i:i + per]
            out.append("    " + ", ".join("%3d" % v for v in row) + ",   /* %s %d-%d */"
                       % (what, first + i, first + i + len(row) - 1))
        return out

    lines = [HEADER.replace("M4.0 spec section 4", "M4.3a spec section 3").rstrip("\n") + "\n",
             "/* ph_symbol_class ($%04X): the class bits of each symbol code of tn_phonetic. Codes not listed have"
             % PH_SYMBOL_CLASS,
             "   none, among them the control codes, the space and the prosody markers ` { | } ~. */",
             "const uint8_t fw_ph_symbol_class[256] = {"]
    for s in range(256):
        v = rom[PH_SYMBOL_CLASS + s]
        assert s >= 0x20 or v == 0, "symbol $%02X below $20 has a class: the decoder would read its pointer" % s
        if v:
            lines.append("    [0x%02X] = %s,   /* %s */" % (s, bits(v, PH_CLASS_BITS), char_name(s)))
    lines += ["};", ""]

    assert PH_SYMBOL_FEATURES + 0xC0 == PH_LIST_PTRS, "ph_list_ptrs no longer starts inside ph_symbol_features"
    lines += ["/* ph_symbol_features ($%04X): the feature bits of each symbol code (codes not listed: none). Its last"
              % PH_SYMBOL_FEATURES,
              "   64 bytes, the features of $C0-$FF ($%04X-$%04X), are also the list-pointer slots of the symbols"
              % (PH_LIST_PTRS, PH_LIST_PTRS + 0x3F),
              "   $00-$1F, which have no class and are never decoded: the encoder writes them once, as features. */",
              "const uint8_t fw_ph_symbol_features[256] = {"]
    for s in range(256):
        v = rom[PH_SYMBOL_FEATURES + s]
        if v:
            alias = ("; also symbol $%02X's pointer slot, %s byte" % ((s - 0xC0) // 2, "high" if s & 1 else "low")
                     if s >= 0xC0 else "")
            lines.append("    [0x%02X] = %s,   /* %s%s */" % (s, bits(v, PH_FEATURE_BITS), char_name(s), alias))
    lines += ["};", ""]

    lists, at = {}, PH_LISTS
    for s in range(0x20, 0x100):
        assert word(PH_LIST_PTRS + 2 * s) == at, "the list pointer of symbol $%02X is not the next list" % s
        pause = rom[at] == PH_WORD_PAUSE
        at += pause
        items = []
        while rom[at] != 0xFF:
            assert not pause and rom[at] != PH_WORD_PAUSE, "a word-pause item with more at $%04X" % at
            phoneme, top, bottom = rom[at:at + 3]
            assert top >= bottom, "a triple at $%04X with max below min" % at
            items.append((phoneme, top, bottom))
            at += 3
        assert len(items) <= 3, "symbol $%02X has more than 3 triples" % s
        lists[s] = (pause, items)
        at += 1
    assert at == UNUSED_A397 and set(rom[UNUSED_A397:0xA600]) == {0xFF}, "the lists end at $%04X" % at
    lines += ["/* The phoneme lists ($%04X-$%04X), by symbol: the (phoneme, max, min) triples ph_decode_symbol turns"
              % (PH_LISTS, UNUSED_A397 - 1),
              "   into segments (the phoneme numbers a row of the voice tables; the duration is min plus the scaled",
              "   max - min). The encoder lays out the lists of $20-$FF end to end in code order, each with its $FF",
              "   end, and writes the list pointers (PH_LIST_PTRS + 2 * symbol, $%04X-$%04X) from where they land."
              % (PH_LIST_PTRS + 0x40, PH_LIST_PTRS + 0x1FF),
              "   Symbols not listed have an empty list (only the end) and make no segment. */",
              "const fw_ph_list fw_ph_lists[256] = {"]
    for s in range(0x20, 0x100):
        pause, items = lists[s]
        if not pause and not items:
            continue
        if s in PH_LIST_NOTES:
            lines.append("    /* %s */" % PH_LIST_NOTES[s])
        if pause:
            lines.append("    [0x%02X] = { .word_pause = true },   /* %s */" % (s, char_name(s)))
        else:
            body = ", ".join("{ %d, %d, %d }" % t for t in items)
            lines.append("    [0x%02X] = { %d, { %s } },   /* %s */" % (s, len(items), body, char_name(s)))
    lines += ["};", ""]

    lengthen = list(rom[PH_DUR_LENGTHEN:PH_DUR_LENGTHEN + 256])
    shorten = list(rom[PH_DUR_SHORTEN:PH_DUR_SHORTEN + 256])
    cap = lengthen.index(255)
    assert all(abs(lengthen[x] - 1.1 * x) < 1.5 for x in range(cap)) and set(lengthen[cap:]) == {255}
    assert all(abs(shorten[x] - 0.9 * x) < 1 for x in range(256))
    lines += ["/* ph_dur_lengthen ($%04X): one lengthening step, x to about 1.1 * x (less than 1.5 off), 255 from"
              % PH_DUR_LENGTHEN,
              "   x = %d on. ph_decode_symbol applies it ph_lengthen_count times to a duration range. */" % cap,
              "const uint8_t fw_ph_dur_lengthen[256] = {"]
    lines += numbers(lengthen, 16, "x =")
    lines += ["};",
              "/* ph_dur_shorten ($%04X): one shortening step, x to about 0.9 * x (less than 1 off)."
              % PH_DUR_SHORTEN,
              "   ph_decode_symbol applies it (shortening count + 1) / 2 times to a duration range. */",
              "const uint8_t fw_ph_dur_shorten[256] = {"]
    lines += numbers(shorten, 16, "x =")
    lines += ["};", ""]

    assert PH_DUR_LENGTHEN + 256 == PH_DUR_SHORTEN and PH_DUR_SHORTEN + 256 == CMD_SPEED_MAPS
    assert CMD_SPEED_MAP_DATA + 9 * 32 == PH_APPEND_PHRASE_PAUSE, "cmd_speed_map_data does not end at code"
    lines += ["/* cmd_speed_map_data ($%04X): per speed 1-9 (cmd_speed_maps) a map of 32 durations in frames:"
              % CMD_SPEED_MAP_DATA,
              "   ph_decode_symbol reads entry duration / 8 into ph_duration. It ends where ph_append_phrase_pause",
              "   begins. */",
              "const uint8_t fw_cmd_speed_map_data[9][32] = {"]
    for k in range(9):
        m = list(rom[CMD_SPEED_MAP_DATA + 32 * k:CMD_SPEED_MAP_DATA + 32 * k + 32])
        assert min(m) >= 1, "a speed map with a duration of 0"
        lines.append("    {   /* speed %d ($%04X) */" % (k + 1, CMD_SPEED_MAP_DATA + 32 * k))
        lines += ["    " + r for r in numbers(m, 16, "duration / 8 =")]
        lines.append("    },")
    lines += ["};", ""]

    assert rom[PH_LENGTHEN_STEPS - 4:PH_LENGTHEN_STEPS + 1] == b"\xb9\x4a\xd4\xa8\x60", \
        "ph_lengthen_count no longer ends with LDA $D44A,Y / TAY / RTS"
    assert PH_LENGTHEN_STEPS + 31 == PAR_VOICE_A_COUNT
    lines += ["/* ph_lengthen_steps ($%04X, the base ph_lengthen_count indexes with A = 0-30; annotated from $%04X):"
              % (PH_LENGTHEN_STEPS, PH_LENGTHEN_STEPS + 1),
              "   the number of ph_dur_lengthen steps for a half count A. Entry 0 is the RTS that ends",
              "   ph_lengthen_count (LDA $%04X,Y / TAY / RTS): A = 0, from a lengthening count of $FF, reads the"
              % PH_LENGTHEN_STEPS,
              "   opcode as %d steps. */" % rom[PH_LENGTHEN_STEPS],
              "const uint8_t fw_ph_lengthen_steps[31] = {",
              "    0x%02X,   /* A = 0: the RTS opcode at $%04X, read as data */" % (rom[PH_LENGTHEN_STEPS],
                                                                              PH_LENGTHEN_STEPS)]
    lines += numbers(list(rom[PH_LENGTHEN_STEPS + 1:PH_LENGTHEN_STEPS + 31]), 15, "A =", 1)
    lines.append("};")
    with open(os.path.join(OUT, "pros_data.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote pros_data.c in", OUT)


def main():
    args = [a for a in sys.argv[1:] if a not in ("--phonetic-only", "--command-only", "--prosody-only")]
    path = args[0] if args else os.environ.get("APOLLO2_ROM", "xtras/UK_V2.01_4.04_ROM.BIN")
    rom = open(path, "rb").read()
    assert len(rom) == 0x10000, "need the 64 KB ROM"
    if "--command-only" in sys.argv:   # M4.2: leave the reviewed M4.0 and M4.1 files alone
        write_command_data(rom)
        return
    if "--prosody-only" in sys.argv:   # M4.3a: leave the reviewed M4.0, M4.1 and M4.2 files alone
        write_prosody_data(rom)
        return
    write_phonetic_texts(rom)
    if "--phonetic-only" in sys.argv:   # M4.1: leave the reviewed M4.0 tables alone
        return
    write_command_data(rom)
    write_prosody_data(rom)

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
        lines = [LTS_HEADER]
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
