#!/usr/bin/env python3
"""One-time converter: Tom Harte's 6502 SingleStepTests (MIT) -> tests/data/cpu6502_vectors.dat.

usage: python tools/harte_subset.py [--fetch] [--per-op N]
  --fetch     download the documented-opcode files (about 640 MB) into build/harte/ first
  --per-op N  vectors kept per opcode (default 300)

Filters (M4.0 spec section 2.1), each a documented 740 difference:
  - BRK ($00) and SED ($F8) are excluded: cpu740 faults on them by design (spec section 3), the ROM never uses them.
  - ADC/SBC vectors whose initial P has D set are skipped: cpu740 faults there by design, the ROM only clears D.
  - PLP/RTI vectors that pull a status byte with bit 5 set are skipped: bit 5 is the 740's T flag, and
    cpu740 faults when PLP/RTI restore T = 1 (by design).
Record format (little-endian): magic b"HARTE740", u32 count, then per vector:
  u8 opcode, state initial, state final;  state = u16 pc, u8 s, a, x, y, p, u8 n, n * (u16 addr, u8 value)
"""
import json
import os
import struct
import sys
import urllib.request

URL = "https://raw.githubusercontent.com/SingleStepTests/65x02/main/6502/v1/{:02x}.json"
SRC = os.path.join("build", "harte")
OUT = os.path.join("tests", "data", "cpu6502_vectors.dat")

# The 151 documented NMOS 6502 opcodes.
DOCUMENTED = sorted({
    0x69, 0x65, 0x75, 0x6D, 0x7D, 0x79, 0x61, 0x71,              # ADC
    0x29, 0x25, 0x35, 0x2D, 0x3D, 0x39, 0x21, 0x31,              # AND
    0x0A, 0x06, 0x16, 0x0E, 0x1E,                                # ASL
    0x90, 0xB0, 0xF0, 0x30, 0xD0, 0x10, 0x50, 0x70,              # branches
    0x24, 0x2C, 0x00,                                            # BIT, BRK
    0x18, 0xD8, 0x58, 0xB8,                                      # CLC CLD CLI CLV
    0xC9, 0xC5, 0xD5, 0xCD, 0xDD, 0xD9, 0xC1, 0xD1,              # CMP
    0xE0, 0xE4, 0xEC, 0xC0, 0xC4, 0xCC,                          # CPX CPY
    0xC6, 0xD6, 0xCE, 0xDE, 0xCA, 0x88,                          # DEC DEX DEY
    0x49, 0x45, 0x55, 0x4D, 0x5D, 0x59, 0x41, 0x51,              # EOR
    0xE6, 0xF6, 0xEE, 0xFE, 0xE8, 0xC8,                          # INC INX INY
    0x4C, 0x6C, 0x20,                                            # JMP JSR
    0xA9, 0xA5, 0xB5, 0xAD, 0xBD, 0xB9, 0xA1, 0xB1,              # LDA
    0xA2, 0xA6, 0xB6, 0xAE, 0xBE, 0xA0, 0xA4, 0xB4, 0xAC, 0xBC,  # LDX LDY
    0x4A, 0x46, 0x56, 0x4E, 0x5E, 0xEA,                          # LSR NOP
    0x09, 0x05, 0x15, 0x0D, 0x1D, 0x19, 0x01, 0x11,              # ORA
    0x48, 0x08, 0x68, 0x28,                                      # PHA PHP PLA PLP
    0x2A, 0x26, 0x36, 0x2E, 0x3E, 0x6A, 0x66, 0x76, 0x6E, 0x7E,  # ROL ROR
    0x40, 0x60,                                                  # RTI RTS
    0xE9, 0xE5, 0xF5, 0xED, 0xFD, 0xF9, 0xE1, 0xF1,              # SBC
    0x38, 0xF8, 0x78,                                            # SEC SED SEI
    0x85, 0x95, 0x8D, 0x9D, 0x99, 0x81, 0x91,                    # STA
    0x86, 0x96, 0x8E, 0x84, 0x94, 0x8C,                          # STX STY
    0xAA, 0xA8, 0xBA, 0x8A, 0x9A, 0x98,                          # TAX TAY TSX TXA TXS TYA
})
assert len(DOCUMENTED) == 151
EXCLUDED = {0x00: "BRK", 0xF8: "SED"}
ADC_SBC = {0x69, 0x65, 0x75, 0x6D, 0x7D, 0x79, 0x61, 0x71, 0xE9, 0xE5, 0xF5, 0xED, 0xFD, 0xF9, 0xE1, 0xF1}


def keep(op, vec):
    ini = vec["initial"]
    if op in ADC_SBC and ini["p"] & 0x08:
        return False
    if op in (0x28, 0x40):   # PLP, RTI: the status byte comes from $0100 + S + 1
        ram = {a: v for a, v in ini["ram"]}
        if ram.get(0x100 + ((ini["s"] + 1) & 0xFF), 0) & 0x20:
            return False
    return True


def state(s):
    ram = s["ram"]
    if len(ram) > 255:
        raise ValueError("more than 255 RAM entries")
    out = struct.pack("<HBBBBBB", s["pc"], s["s"], s["a"], s["x"], s["y"], s["p"], len(ram))
    for addr, val in ram:
        out += struct.pack("<HB", addr, val)
    return out


def main():
    fetch = "--fetch" in sys.argv
    per_op = int(sys.argv[sys.argv.index("--per-op") + 1]) if "--per-op" in sys.argv else 300
    ops = [op for op in DOCUMENTED if op not in EXCLUDED]
    os.makedirs(SRC, exist_ok=True)
    records = []
    for op in ops:
        path = os.path.join(SRC, "{:02x}.json".format(op))
        if not os.path.exists(path):
            if not fetch:
                sys.exit("missing %s: run with --fetch" % path)
            print("fetching", path)
            urllib.request.urlretrieve(URL.format(op), path)
        with open(path, encoding="utf-8") as f:
            vectors = json.load(f)
        n = 0
        for vec in vectors:
            if keep(op, vec):
                records.append(struct.pack("B", op) + state(vec["initial"]) + state(vec["final"]))
                n += 1
                if n == per_op:
                    break
        if n < per_op:
            sys.exit("opcode %02X: only %d vectors pass the filters" % (op, n))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(b"HARTE740" + struct.pack("<I", len(records)) + b"".join(records))
    print("wrote %s: %d vectors, %d opcodes" % (OUT, len(records), len(ops)))


if __name__ == "__main__":
    main()
