#!/usr/bin/env python3
"""Find a 16-bit constant in the code and say whether it is REAL.

    py -3.10 tools/portscan.py 0x3c8 0x3c9 0x3da 0x3d4
    py -3.10 tools/portscan.py --image reverse/unpacked/xcursion.bin 0xa000
    py -3.10 tools/portscan.py --end 0x569c 0x3c8

A naive byte search for a VGA port is wrong about half the time and
wrong SILENTLY, which is the whole reason this exists. Two claims
documented from one went into the database before being caught:

  o "port 0x3c9 at img 0x2f7d" - the bytes c9 03 there are the tail of
    one instruction and the head of the next.
  o "the only CRTC access, 0x3d4 at img 0x4114" - that is 86 d4
    (xchg ah,dl) followed by 03 c2 (add ax,dx). THERE IS NO CRTC
    ACCESS IN THIS DEMO, and a whole paragraph about tweaked modes was
    written on the strength of two straddling bytes.

So a hit counts only when an instruction decoded from a nearby
boundary actually renders the value as an operand. That still is not
proof - x86 has no instruction boundaries until you know where
execution enters - but it separates "an operand" from "two adjacent
bytes", which is the distinction CLAUDE.md rule 6.2 is about.

Report REAL hits. Treat STRADDLE hits as nothing at all.
"""

import sys

try:
    import capstone
except ImportError:
    sys.exit('needs capstone: py -3.10 -m pip install capstone')

DEFAULT_IMAGE = 'reverse/unpacked/xcursion.bin'
# The last function in XCURSION_U.EXE ends at img 0x569b. Past that is
# data, where a byte pair matching a port means nothing whatsoever.
DEFAULT_END = 0x569c

# How far back to try a decode boundary. An x86 instruction is at most
# 15 bytes, but a 2-byte operand sits within a few of its opcode.
LOOKBACK = 12


def classify(code, word, end, lookback=LOOKBACK):
    """[(offset, 'real'|'straddle', text)] for every byte-pair match."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    pat = word.to_bytes(2, 'little')
    want = '0x%x' % word
    out = []

    at = code.find(pat, 0, end)
    while at != -1:
        verdict, text = 'straddle', ''
        for back in range(1, lookback + 1):
            start = at - back
            if start < 0:
                break
            ins = next(md.disasm(code[start:start + 16], start), None)
            if ins is None:
                continue
            # The operand has to live inside this instruction, and the
            # instruction has to be the one that contains the bytes.
            if ins.address <= at and at + 2 <= ins.address + ins.size:
                if want in ins.op_str:
                    verdict = 'real'
                    text = '%s %s' % (ins.mnemonic, ins.op_str)
                    break
        out.append((at, verdict, text))
        at = code.find(pat, at + 1, end)
    return out


def main(argv):
    image, end, words = DEFAULT_IMAGE, DEFAULT_END, []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '--image':
            i += 1
            image = argv[i]
        elif a == '--end':
            i += 1
            end = int(argv[i], 0)
        else:
            words.append(int(a, 0))
        i += 1

    if not words:
        sys.exit(__doc__)

    with open(image, 'rb') as f:
        code = f.read()
    print('%s, searching img 0x0 .. 0x%x' % (image, end))

    for w in words:
        rows = classify(code, w, end)
        real = [r for r in rows if r[1] == 'real']
        strad = [r for r in rows if r[1] == 'straddle']
        print('')
        print('0x%04x   %d real, %d straddle' % (w, len(real), len(strad)))
        for at, _, text in real:
            print('  REAL      img %05x   %s' % (at, text))
        for at, _, _ in strad:
            print('  straddle  img %05x   - not an operand, ignore' % at)
        if not real:
            print('  NOTHING REAL. This constant is not used as an '
                  'operand anywhere in the code.')


if __name__ == '__main__':
    main(sys.argv[1:])
