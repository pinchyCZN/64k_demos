#!/usr/bin/env python3
"""Disassemble XCURSION.EXE's unpacker stub by following its control flow.

    py -3.10 tools/unpack/xcur_stub.py              the whole trace
    py -3.10 tools/unpack/xcur_stub.py 0x137        start somewhere else
    py -3.10 tools/unpack/xcur_stub.py --hdr        just the MZ header
    py -3.10 tools/unpack/xcur_stub.py --raw 0x137 64    hex dump

A linear disassembly of this stub is worthless: it is 977 bytes of code
with constant pools and 0x03 filler between the routines, so Ghidra's
listing shows long runs of `ADD AX,[BP+DI]` and the occasional `SALC`
wherever it fell out of step. A recursive walk from the real entries
shows only bytes something actually reaches.

ADDRESSES ARE CS-RELATIVE, CS = the stub segment, and the stub is
exactly the last 977 bytes of the file. The file offset of a stub
address is STUB_AT + addr. Ghidra spells the same bytes 1be1:xxxx, and
also 1000:bxxx from the CODE_0 block, which is the same linear byte -
see CLAUDE.md rule 3 before quoting one of these anywhere.

What makes the walk inexact, and why its output is a lead rather than a
finding:

  o The stub installs its own INT 1 and INT 3 vectors and then uses
    those interrupts as one-byte calls. They are followed as calls to
    whatever address the installed vector held, which is only correct
    while that vector stands - the stub rewrites them.
  o It writes to its own code segment, so some bytes here are not the
    bytes that execute.

Both are reported rather than resolved. Confirm anything load-bearing
against Ghidra.
"""

import os
import sys

try:
    import capstone
except ImportError:
    sys.exit('needs capstone: py -3.10 -m pip install capstone')

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
EXE = os.path.join(ROOT, 'reverse', 'ghidra', 'XCURSION.EXE')

# From the MZ header: e_cparhdr 8 paragraphs, and e_cs 0x0be1. The stub
# segment therefore begins 0x0be1 paragraphs into the load image, which
# itself begins at file 0x80.
HDR_BYTES = 0x80
STUB_SEG = 0x0be1
STUB_AT = HDR_BYTES + STUB_SEG * 16
GHIDRA_SEG = 0x1be1        # what Ghidra bases the same block at

# MZ entry chain, then the two routines the stub reaches by interrupt.
# 0xcd is the real start; the two JMPs in front of it are the MZ entry.
SEEDS = (0x0000, 0x00b0, 0x00cd)

STOP = {'ret', 'retf', 'iret', 'iretw', 'jmp', 'hlt'}


def read():
    with open(EXE, 'rb') as f:
        return f.read()


def u16(b, off):
    return b[off] | (b[off + 1] << 8)


def header(b):
    """The MZ fields that decide where anything is."""
    f = [
        ('e_magic', b[0:2].decode('latin-1')),
        ('e_cblp   bytes in last page', u16(b, 0x02)),
        ('e_cp     pages', u16(b, 0x04)),
        ('e_crlc   relocations', u16(b, 0x06)),
        ('e_cparhdr header paragraphs', u16(b, 0x08)),
        ('e_minalloc', u16(b, 0x0a)),
        ('e_maxalloc', u16(b, 0x0c)),
        ('e_ss', u16(b, 0x0e)),
        ('e_sp', u16(b, 0x10)),
        ('e_csum', u16(b, 0x12)),
        ('e_ip', u16(b, 0x14)),
        ('e_cs', u16(b, 0x16)),
        ('e_lfarlc', u16(b, 0x18)),
        ('e_ovno', u16(b, 0x1a)),
    ]
    for k, v in f:
        print('  %-30s %s' % (k, v if isinstance(v, str) else '0x%04x' % v))

    pages, last = u16(b, 0x04), u16(b, 0x02)
    image = (pages - 1) * 512 + (last if last else 512)
    print('  %-30s %d (file is %d)' % ('load image bytes', image, len(b)))
    print('  %-30s file 0x%x' % ('load image starts at', HDR_BYTES))
    print('  %-30s file 0x%x, %d bytes'
          % ('stub starts at', STUB_AT, len(b) - STUB_AT))


def walk(code, seeds):
    """Recursive-descent disassembly. Returns {addr: insn} and the notes."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    md.detail = True

    seen = {}
    notes = []
    # Vectors the stub installs, as offset -> what it was set to. The
    # walk needs these to follow an `int 1` / `int 3`.
    vectors = {}
    todo = list(seeds)
    labels = {s: 'seed' for s in seeds}

    while todo:
        pc = todo.pop()
        while 0 <= pc < len(code):
            if pc in seen:
                break
            chunk = code[pc:pc + 16]
            ins = next(md.disasm(chunk, pc), None)
            if ins is None:
                notes.append('%04x  undecodable' % pc)
                break
            seen[pc] = ins
            m = ins.mnemonic

            # Catch the vector installs so `int n` can be followed.
            # Shape: mov word ptr es:[0x4], 0x23d   (offset)
            #        mov word ptr es:[0x6], cs      (segment)
            if (m == 'mov' and 'es:' in ins.op_str
                    and ins.op_str.count(',') == 1):
                lhs, rhs = [s.strip() for s in ins.op_str.split(',')]
                for v in (0x04, 0x0c):
                    if ('0x%x]' % v) in lhs and rhs.startswith('0x'):
                        vectors[v] = int(rhs, 16)
                        notes.append('%04x  installs INT %d -> %s'
                                     % (pc, v // 4, rhs))

            if m == 'int':
                n = int(ins.op_str, 16)
                tgt = vectors.get(n * 4)
                if tgt is not None and tgt not in labels:
                    labels[tgt] = 'int %d' % n
                    todo.append(tgt)
                    notes.append('%04x  int %d is a call to %04x'
                                 % (pc, n, tgt))
                elif tgt is None:
                    notes.append('%04x  int %d - vector not seen installed'
                                 % (pc, n))

            # Self-modifying writes. Worth a note wherever they are.
            if m == 'mov' and 'cs:' in ins.op_str.split(',')[0]:
                notes.append('%04x  WRITES ITS OWN CODE: %s %s'
                             % (pc, m, ins.op_str))

            if m in ('jmp', 'call') or m.startswith('j') or m == 'loop':
                op = ins.op_str
                if op.startswith('0x'):
                    tgt = int(op, 16)
                    if tgt not in labels:
                        labels[tgt] = m
                    if 0 <= tgt < len(code) and tgt not in seen:
                        todo.append(tgt)
                elif m in ('jmp', 'call'):
                    notes.append('%04x  indirect %s %s' % (pc, m, op))

            if m in STOP:
                break
            pc += ins.size

    return seen, labels, notes


def show(code, seen, labels):
    prev_end = None
    for pc in sorted(seen):
        if prev_end is not None and pc != prev_end:
            print('      ---- %d byte(s) not reached (file 0x%x) ----'
                  % (pc - prev_end, STUB_AT + prev_end))
        ins = seen[pc]
        mark = '  <- %s' % labels[pc] if pc in labels else ''
        print('%04x: %-22s %-28s %s%s'
              % (pc, ins.bytes.hex(), ins.mnemonic + ' ' + ins.op_str,
                 '', mark))
        prev_end = pc + ins.size


def main(argv):
    b = read()
    code = b[STUB_AT:]

    if '--hdr' in argv:
        print('MZ header, %s' % EXE)
        header(b)
        return

    if '--raw' in argv:
        i = argv.index('--raw')
        at = int(argv[i + 1], 0)
        n = int(argv[i + 2], 0) if len(argv) > i + 2 else 64
        for row in range(0, n, 16):
            chunk = code[at + row:at + row + 16]
            txt = ''.join(chr(c) if 32 <= c < 127 else '.' for c in chunk)
            print('%04x: %-47s  %s' % (at + row, chunk.hex(' '), txt))
        return

    seeds = [int(a, 0) for a in argv if not a.startswith('-')] or list(SEEDS)

    print('XCURSION.EXE unpacker stub')
    print('  file 0x%x .. 0x%x, %d bytes' % (STUB_AT, len(b), len(code)))
    print('  addresses are CS-relative; Ghidra spells CS as %04x'
          % GHIDRA_SEG)
    print('  seeds: %s' % ', '.join('0x%x' % s for s in seeds))
    print('')

    seen, labels, notes = walk(code, seeds)
    show(code, seen, labels)

    print('')
    print('reached %d of %d bytes in %d instructions'
          % (sum(i.size for i in seen.values()), len(code), len(seen)))
    if notes:
        print('')
        print('notes')
        for n in notes:
            print('  ' + n)


if __name__ == '__main__':
    main(sys.argv[1:])
