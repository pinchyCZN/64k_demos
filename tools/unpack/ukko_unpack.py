#!/usr/bin/env python3
"""Depack UKKO.EXE by running its own PKLITE decompressor under Unicorn.

    py -3.10 tools/unpack/ukko_unpack.py                   report only
    py -3.10 tools/unpack/ukko_unpack.py -o reverse/unpacked/ukko.bin
        --exe reverse/unpacked/UKKO_U.EXE

THIS IS A DIFFERENT PACKER FROM THE OTHER THREE. Xcursion, Drain and
Lithium share one LZEXE-derived packer - docs/formats/packer.txt - and
UKKO.EXE is PKLITE 1.15, marked in the clear at file offset 0x1d. None
of xcur_unpack.py's constants apply here, which is why this is a second
script rather than a flag on the first.

THE STUB RELOCATES ITSELF AND RUNS FROM THE COPY. From the MZ entry it
checks free memory, then moves 0x123 words to a segment it computes as
(required_paragraphs + load_seg - 0x45) and far-jumps into it. So the
decompressor's addresses are not the ones in the file.

HOW THE END IS DETECTED, and it is the whole trick: a UC_HOOK_CODE is
installed over the LOW region only. The decompressor writes there
throughout but never executes there, so the hook cannot fire until the
final transfer into the depacked program - which is exactly the moment
the original CS:IP is live. A hook over all of memory would fire on
every instruction and never finish.

DOS here is a stub. Only the memory check's PSP field is provided; an
interrupt that is not expected stops the run rather than returning a
plausible wrong answer.
"""

import os
import struct
import sys

try:
    import unicorn
    from unicorn import x86_const as X
except ImportError:
    sys.exit('needs unicorn: py -3.10 -m pip install unicorn')

EXE = 'reverse/ghidra/UKKO.EXE'
LOAD_SEG = 0x1000
MEM_SIZE = 0x110000
MAX_INSNS = 200_000_000


def lin(seg, off):
    return ((seg & 0xffff) << 4) + (off & 0xffff)


class Depacker(object):

    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            self.exe = f.read()
        self.hdr_paras = self.u16(0x08)
        self.nreloc, self.reltab = self.u16(0x06), self.u16(0x18)
        self.e_ss, self.e_sp = self.u16(0x0e), self.u16(0x10)
        self.e_ip, self.e_cs = self.u16(0x14), self.u16(0x16)
        pages, last = self.u16(0x04), self.u16(0x02)
        self.image_bytes = (pages - 1) * 512 + (last if last else 512)
        self.image = bytearray(self.exe[self.hdr_paras * 16:self.image_bytes])
        self.lo = self.hi = None
        self.stop = None
        self.insns = 0

    def u16(self, off, buf=None):
        b = self.exe if buf is None else buf
        return b[off] | (b[off + 1] << 8)

    def relocate(self):
        for i in range(self.nreloc):
            o = lin(self.u16(self.reltab + i * 4 + 2),
                    self.u16(self.reltab + i * 4))
            v = (self.u16(o, self.image) + LOAD_SEG) & 0xffff
            self.image[o], self.image[o + 1] = v & 0xff, v >> 8

    def build(self):
        uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_16)
        uc.mem_map(0, MEM_SIZE)
        uc.mem_write(lin(LOAD_SEG, 0), bytes(self.image))
        psp = LOAD_SEG - 0x10
        uc.mem_write(lin(psp, 2), struct.pack('<H', 0x9fff))   # top of memory
        for r, v in ((X.UC_X86_REG_CS, (self.e_cs + LOAD_SEG) & 0xffff),
                     (X.UC_X86_REG_IP, self.e_ip),
                     (X.UC_X86_REG_SS, (self.e_ss + LOAD_SEG) & 0xffff),
                     (X.UC_X86_REG_SP, self.e_sp),
                     (X.UC_X86_REG_DS, psp), (X.UC_X86_REG_ES, psp)):
            uc.reg_write(r, v)

        # The window stops just under the relocated stub so its own
        # stack pushes cannot be mistaken for decompressor output.
        base, top = lin(LOAD_SEG, 0), lin(0x86e0, 0) - 1

        def on_write(u, access, addr, size, value, data):
            o = addr - base
            if self.lo is None or o < self.lo:
                self.lo = o
            if self.hi is None or o + size > self.hi:
                self.hi = o + size

        def on_exec(u, addr, size, data):
            # THE STUB'S OWN ENTRY IS AT THIS ADDRESS TOO - 0ff0:0100 is
            # linear 0x10000, which is lin(LOAD_SEG, 0). Arming on the
            # first output write is what separates the two: the
            # decompressor writes the low region long before it ever
            # executes there.
            if self.lo is None:
                return
            self.stop = (u.reg_read(X.UC_X86_REG_CS),
                         u.reg_read(X.UC_X86_REG_IP),
                         u.reg_read(X.UC_X86_REG_SS),
                         u.reg_read(X.UC_X86_REG_SP))
            u.emu_stop()

        def on_intr(u, no, data):
            self.stop = ('INT', no, u.reg_read(X.UC_X86_REG_CS),
                         u.reg_read(X.UC_X86_REG_IP))
            u.emu_stop()

        uc.hook_add(unicorn.UC_HOOK_MEM_WRITE, on_write,
                    begin=lin(0x0800, 0), end=top)
        uc.hook_add(unicorn.UC_HOOK_CODE, on_exec, begin=base, end=top)
        uc.hook_add(unicorn.UC_HOOK_INTR, on_intr)
        self.uc = uc

    def run(self):
        self.relocate()
        self.build()
        try:
            self.uc.emu_start(lin((self.e_cs + LOAD_SEG) & 0xffff, self.e_ip),
                              MEM_SIZE, count=MAX_INSNS)
        except unicorn.UcError as e:
            print('emulation error: %s at %04x:%04x' %
                  (e, self.uc.reg_read(X.UC_X86_REG_CS),
                   self.uc.reg_read(X.UC_X86_REG_IP)))
        return self.stop

    def dump(self, path, end):
        data = bytes(self.uc.mem_read(lin(LOAD_SEG, 0), end))
        with open(path, 'wb') as f:
            f.write(data)
        return len(data)

    def write_exe(self, path, end, cs, ip, ss, sp):
        """A 32-byte header, no relocations, entry as discovered."""
        total = 32 + end
        pages = (total + 511) // 512
        hdr = struct.pack('<2sHHHHHHHHHHHH', b'MZ', total % 512, pages, 0, 2,
                          self.u16(0x0a), 0xffff,
                          (ss - LOAD_SEG) & 0xffff, sp, 0,
                          ip, (cs - LOAD_SEG) & 0xffff, 0x1c)
        with open(path, 'wb') as f:
            f.write(hdr + b'\x00' * (32 - len(hdr)))
            f.write(bytes(self.uc.mem_read(lin(LOAD_SEG, 0), end)))
        return total


def main(argv):
    out = exe_out = None
    src = EXE
    for i, a in enumerate(argv):
        if a == '-o':
            out = argv[i + 1]
        elif a == '--exe':
            exe_out = argv[i + 1]
        elif a == '--in':
            src = argv[i + 1]

    d = Depacker(src)
    print('%s  %d bytes, %d reloc(s), entry %04x:%04x'
          % (src, len(d.exe), d.nreloc, d.e_cs, d.e_ip))
    st = d.run()
    if st is None or st[0] == 'INT':
        print('did not reach the depacked entry: %r' % (st,))
        return 1
    cs, ip, ss, sp = st
    end = (d.hi + 15) & ~15
    print('depacked entry  %04x:%04x   (image-relative %04x:%04x)'
          % (cs, ip, (cs - LOAD_SEG) & 0xffff, ip))
    print('stack           %04x:%04x' % (ss, sp))
    print('writes spanned  0x%05x .. 0x%05x  -> %d bytes' % (d.lo, d.hi, end))
    if out:
        print('wrote %s  %d bytes' % (out, d.dump(out, end)))
    if exe_out:
        print('wrote %s  %d bytes'
              % (exe_out, d.write_exe(exe_out, end, cs, ip, ss, sp)))
    if out:
        import json
        meta = {'packer': 'PKLITE 1.15', 'dump_bytes': end,
                'load_seg': LOAD_SEG, 'stub_cs': 0x86e0,
                'orig_cs': cs, 'orig_ip': ip, 'orig_ss': ss, 'orig_sp': sp,
                'rel_cs': (cs - LOAD_SEG) & 0xffff,
                'rel_ss': (ss - LOAD_SEG) & 0xffff,
                'write_lo': d.lo, 'write_hi': d.hi}
        jp = os.path.splitext(out)[0] + '.json'
        with open(jp, 'w') as f:
            json.dump(meta, f, indent=2, sort_keys=True)
            f.write(chr(10))
        print('wrote %s' % jp)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
