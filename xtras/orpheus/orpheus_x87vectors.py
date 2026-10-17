# Generates tests/orpheus/x87.txt: x87 results (precision 64, round to nearest) computed by Unicorn, the oracle's own
# x87, for src/orpheus/orp_x87.c. Each line: OP A B RESULT DOUBLE, tbytes as 20 hex digits (sign/exponent, then the
# 64-bit significand); DOUBLE is RESULT stored with fstp qword. "trunc A - INT -" is fistp under chop.
# Usage, from the repository root: python xtras/orpheus/orpheus_x87vectors.py
import os
import random
import struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
CODE, DATA = 0x1000, 0x2000
CW, A, B, R, D, I = DATA, DATA + 0x10, DATA + 0x20, DATA + 0x30, DATA + 0x40, DATA + 0x50
OPS = {'add': b'\xde\xc1', 'sub': b'\xde\xe9', 'mul': b'\xde\xc9', 'div': b'\xde\xf9'}   # st1 = st1 OP st0

mu = Uc(UC_ARCH_X86, UC_MODE_32)
mu.mem_map(0, 0x10000)


def m32(op, addr):
    return op + struct.pack('<I', addr)


def run(code):
    mu.mem_write(CODE, code + b'\xf4')
    mu.emu_start(CODE, CODE + len(code))


def tbyte(neg, e, m):
    return struct.pack('<QH', m, (neg << 15) | (e + 16383 if m else 0))


def hx(t):
    m, se = struct.unpack('<QH', t)
    return '%04X%016X' % (se, m)


def binop(op, a, b):
    mu.mem_write(CW, struct.pack('<H', 0x037F))
    mu.mem_write(A, a)
    mu.mem_write(B, b)
    run(m32(b'\xd9\x2d', CW) + m32(b'\xdb\x2d', A) + m32(b'\xdb\x2d', B) + OPS[op] + b'\xd9\xc0' +
        m32(b'\xdb\x3d', R) + m32(b'\xdd\x1d', D))
    return mu.mem_read(R, 10), struct.unpack('<Q', mu.mem_read(D, 8))[0]


def cvt(a):
    mu.mem_write(CW, struct.pack('<H', 0x037F))
    mu.mem_write(A, a)
    run(m32(b'\xd9\x2d', CW) + m32(b'\xdb\x2d', A) + m32(b'\xdd\x1d', D))
    return struct.unpack('<Q', mu.mem_read(D, 8))[0]


def trunc(a):
    mu.mem_write(CW, struct.pack('<H', 0x0F7F))
    mu.mem_write(A, a)
    run(m32(b'\xd9\x2d', CW) + m32(b'\xdb\x2d', A) + m32(b'\xdb\x1d', I))
    return struct.unpack('<i', mu.mem_read(I, 4))[0]


def rand_m(rng):
    kind = rng.randrange(4)
    if kind == 0:
        return rng.getrandbits(63) | 1 << 63                   # any 64-bit significand
    if kind == 1:
        return (rng.getrandbits(52) | 1 << 52) << 11            # a double's
    if kind == 2:
        return (rng.getrandbits(8) | 1 << 8) << 55              # few bits: exact results and ties
    return (1 << 63) | rng.choice((0, 1, 1 << 10, (1 << 11) - 1, 1 << 11, (1 << 63) - 1))


def main():
    rng = random.Random(1987)
    lines = []
    for op in OPS:
        for k in range(600):
            ea = rng.randrange(-40, 40)
            eb = ea + rng.choice((0, 0, 1, -1, 2, rng.randrange(-70, 70), rng.randrange(-140, 140)))
            if op in ('mul', 'div'):
                eb = rng.randrange(-40, 40)
            ma = rand_m(rng)
            mb = ma if k % 25 == 0 else rand_m(rng)
            na, nb = rng.random() < 0.4, rng.random() < 0.4
            a, b = tbyte(na, ea, ma), tbyte(nb, eb, mb)
            if k % 50 == 7:
                a = tbyte(na, 0, 0)
            r, d = binop(op, a, b)
            lines.append('%s %s %s %s %016X' % (op, hx(a), hx(b), hx(bytes(r)), d))
    for k in range(400):   # fstp qword around and below the double range's bottom
        e = rng.choice((rng.randrange(-1100, -1000), rng.randrange(-60, 60)))
        a = tbyte(rng.random() < 0.5, e, rand_m(rng))
        lines.append('cvt %s - - %016X' % (hx(a), cvt(a)))
    for k in range(300):
        e = rng.randrange(-3, 31)
        a = tbyte(rng.random() < 0.5, e, rand_m(rng))
        lines.append('trunc %s - %d -' % (hx(a), trunc(a)))
    path = os.path.join(ROOT, 'tests', 'orpheus', 'x87.txt')
    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    print(path, len(lines), 'vectors')


if __name__ == '__main__':
    main()
