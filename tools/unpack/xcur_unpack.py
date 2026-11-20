#!/usr/bin/env python3
"""Depack one of these demos by running its own decompressor under
Unicorn. Defaults to XCURSION.EXE; --in picks another.

    py -3.10 tools/unpack/xcur_unpack.py                 report only
    py -3.10 tools/unpack/xcur_unpack.py --trace 60      step the preamble
    py -3.10 tools/unpack/xcur_unpack.py --wrapper --phasea 2000000
    py -3.10 tools/unpack/xcur_unpack.py -o reverse/unpacked/xcursion.bin
    py -3.10 tools/unpack/xcur_unpack.py --in reverse/ghidra/LITHIUM.EXE
        -o reverse/unpacked/lithium.bin
        --exe reverse/unpacked/LITHIUM_U.EXE

ALL THREE DEMOS SHARE THIS DECOMPRESSOR BYTE FOR BYTE, so CS_RETF and
CS_STREAM below are not XCURSION-specific. docs/layout.txt section 2.

Rule 3.1 prefers a static reimplementation to an emulator, and this is
the emulator. The reason for taking it first is that the static route is
blocked on one fact an emulator hands over for nothing: the decompressor
is entered with SI = 0 against a segment the preamble computes from the
image size, so where the bitstream starts is not readable off the
listing. Run it once, learn that, and the reimplementation becomes
something that can be CHECKED rather than guessed.

WHAT THIS DELIBERATELY DOES NOT RUN is the 977-byte wrapper at the MZ
entry point. Execution starts at the head of the load image instead -
img 0x0000, the memory check - which is where the wrapper's chain
eventually lands. That is sound because every address the wrapper patches
is inside its own 977 bytes (CS:0x5a through CS:0x203), so none of its
self-modification reaches the decompressor; see
docs/formats/xcursion_packer.txt section 4. Pass --wrapper to start at
the real MZ entry instead and make the emulator prove it.

THE RUN IS IN TWO PHASES AND THE REASON IS SPEED, NOT CORRECTNESS. A
Python UC_HOOK_CODE callback costs more than the instruction it watches,
and decompressing 48 KB is tens of millions of instructions - with the
hook installed throughout, this does not finish. So phase A steps the
preamble with the hook on, only far enough to learn which segment the
decompressor relocated itself to, and phase B runs the decompression with
no hooks at all, stopping at an exact address computed from that segment.

The stop address is the decompressor's final RETF, CS:0x209 of the
relocated copy. Unicorn stops BEFORE executing it, which is the useful
moment: the original CS and IP are on the stack, pushed three and five
instructions earlier, and SS:SP has already been set to the demo's own.

DOS here is a stub, not an emulation. Only what this binary asks for is
implemented, and anything else raises rather than returning a plausible
wrong answer.
"""

import json
import os
import sys

try:
    import unicorn
    from unicorn import x86_const as X
except ImportError:
    sys.exit('needs unicorn: py -3.10 -m pip install unicorn')

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
EXE = os.path.join(ROOT, 'reverse', 'ghidra', 'XCURSION.EXE')

# --in picks a different packed EXE. THE DECOMPRESSOR IS BYTE-IDENTICAL
# IN ALL THREE DEMOS - sha256 of img 0x54..0x29c begins 9071a0554cd2 in
# XCURSION, LITHIUM and DRAIN alike - so CS_RETF and CS_STREAM below
# carry over unchanged and only the input path is per-file. The four
# bytes that do differ are the relocate distance and the required
# paragraph count, and both are read out of the image at run time.
# See docs/layout.txt section 2.

# Where we pretend DOS loaded the program.
#
# LOAD_SEG IS 0x1000 ON PURPOSE: that is where Ghidra's MZ loader bases a
# DOS image, so every segment value the unpacker bakes into the output is
# already a Ghidra address. The original relocations are applied during
# the depack and cannot be un-applied, so the dump is load-segment
# specific either way - this just makes the one it is specific to the
# useful one. `mov dx, 0x1cd2` at the entry reads as the data segment it
# is instead of an arbitrary number.
#
# The memory check at img 0x0000 compares a required 0x82c0 paragraphs
# against PSP:0x0002, so the top of memory has to clear that - see
# docs/formats/xcursion_packer.txt section 2.
LOAD_SEG = 0x1000
PSP_SEG = LOAD_SEG - 0x10
MEM_TOP_SEG = 0x9fff

# 1 MB, plus 64 KB so a seg:off pair near the top still lands in mapped
# memory instead of faulting. Real mode wraps; Unicorn does not.
MEM_SIZE = 0x100000 + 0x10000

# Offsets inside the relocated decompressor. CS:0 of the running copy is
# img 0x54, so a CS offset is its img offset minus 0x54 - the shift that
# the plate comment on 1000:003e warns about.
IMG_BODY = 0x54
CS_RETF = 0x209             # img 0x25d, the handoff

# The two instructions that store decompressed output: the literal path
# and the match-copy path. Watching only these two gives the true end of
# the image, which trailing-zero trimming cannot - see image_out.
CS_STORES = (0x7e, 0x103)   # img 0xd2 and img 0x157
CS_STREAM = 0x40            # img 0x94, the first LODSW: DS:SI and ES:DI
                            # are both installed by here. img 0x8b looks
                            # like the right place and is three
                            # instructions too early - ES and DI still
                            # hold leftovers there.

PHASE_A_INSNS = 4000
PHASE_B_INSNS = 400_000_000


def lin(seg, off):
    return ((seg & 0xffff) << 4) + (off & 0xffff)


class Depacker(object):

    # path is REQUIRED rather than defaulted to EXE: a default argument
    # binds at def time, so a `global EXE` reassignment in main() would
    # never reach it. That silently depacked XCURSION under a Lithium
    # output name once.
    def __init__(self, path, start_at_wrapper=False, trace=0,
                 phase_a=PHASE_A_INSNS):
        self.path = path
        with open(path, 'rb') as f:
            self.exe = f.read()
        self.trace_left = trace
        self.start_at_wrapper = start_at_wrapper
        self.phase_a = phase_a

        self.hdr_paras = self.u16(0x08)
        self.e_ss, self.e_sp = self.u16(0x0e), self.u16(0x10)
        self.e_ip, self.e_cs = self.u16(0x14), self.u16(0x16)
        pages, last = self.u16(0x04), self.u16(0x02)
        self.image_bytes = (pages - 1) * 512 + (last if last else 512)
        self.image_at = self.hdr_paras * 16
        self.image = self.exe[self.image_at:self.image_bytes]

        self.load_seg = LOAD_SEG
        self.insns = 0
        self.seg_log = []
        self.dos_calls = []
        self.unpacker_cs = None
        self.out_end = None             # image offset; see image_out
        self.zero_end = None
        self.stream = None
        self.stopped = None
        self.result = {}

    def u16(self, off):
        return self.exe[off] | (self.exe[off + 1] << 8)

    # ------------------------------------------------------------ machine

    def build(self):
        uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_16)
        uc.mem_map(0, MEM_SIZE)
        self.uc = uc
        uc.mem_write(lin(self.load_seg, 0), self.image)

        base = lin(self.load_seg, 0)

        def out_watch(u, access, addr, size, value, data):
            if (self.unpacker_cs is not None
                    and u.reg_read(X.UC_X86_REG_CS) == self.unpacker_cs
                    and self.ip(u) in CS_STORES):
                off = addr + size - base
                if self.out_end is None or off > self.out_end:
                    self.out_end = off

        uc.hook_add(unicorn.UC_HOOK_MEM_WRITE, out_watch,
                    begin=base, end=base + 0x40000)

        # The PSP, in the two fields this binary reads. 0x0002 is the
        # segment one past the end of the allocation, which the memory
        # check tests and the relocation arithmetic is derived from.
        psp = lin(PSP_SEG, 0)
        uc.mem_write(psp + 0x00, b'\xcd\x20')
        uc.mem_write(psp + 0x02, MEM_TOP_SEG.to_bytes(2, 'little'))

        uc.reg_write(X.UC_X86_REG_DS, PSP_SEG)
        uc.reg_write(X.UC_X86_REG_ES, PSP_SEG)
        uc.reg_write(X.UC_X86_REG_SS, self.load_seg + self.e_ss)
        uc.reg_write(X.UC_X86_REG_SP, self.e_sp or 0xfffe)
        for r in (X.UC_X86_REG_AX, X.UC_X86_REG_BX,
                  X.UC_X86_REG_CX, X.UC_X86_REG_DX,
                  X.UC_X86_REG_SI, X.UC_X86_REG_DI, X.UC_X86_REG_BP):
            uc.reg_write(r, 0)

        if self.start_at_wrapper:
            cs, ip = self.load_seg + self.e_cs, self.e_ip
        else:
            cs, ip = self.load_seg, 0x0000
        uc.reg_write(X.UC_X86_REG_CS, cs)
        uc.reg_write(X.UC_X86_REG_IP, ip)
        self.start_cs, self.start_ip = cs, ip

        uc.hook_add(unicorn.UC_HOOK_INTR, self.on_intr)
        uc.hook_add(unicorn.UC_HOOK_INSN, self.on_out,
                    aux1=X.UC_X86_INS_OUT)
        uc.hook_add(unicorn.UC_HOOK_INSN, self.on_in,
                    aux1=X.UC_X86_INS_IN)
        self.code_hook = uc.hook_add(unicorn.UC_HOOK_CODE, self.on_code)
        return uc

    # -------------------------------------------------------------- hooks

    def on_code(self, uc, addr, size, _):
        self.insns += 1
        cs = uc.reg_read(X.UC_X86_REG_CS)
        ip = (addr - (cs << 4)) & 0xffff
        if not self.seg_log or self.seg_log[-1][0] != cs:
            self.seg_log.append((cs, ip, self.insns))
        # The relocated decompressor announces itself by being a segment
        # that is neither where the image was loaded nor the wrapper, and
        # by running at a CS offset inside its own 0x248 bytes.
        if (self.unpacker_cs is None and cs != self.start_cs
                and cs != self.load_seg and ip < 0x248):
            self.unpacker_cs = cs
        if self.trace_left > 0:
            self.trace_left -= 1
            print('%04x:%04x  ax=%04x bx=%04x cx=%04x dx=%04x si=%04x '
                  'di=%04x bp=%04x ds=%04x es=%04x ss=%04x sp=%04x'
                  % (cs, ip,
                     uc.reg_read(X.UC_X86_REG_AX),
                     uc.reg_read(X.UC_X86_REG_BX),
                     uc.reg_read(X.UC_X86_REG_CX),
                     uc.reg_read(X.UC_X86_REG_DX),
                     uc.reg_read(X.UC_X86_REG_SI),
                     uc.reg_read(X.UC_X86_REG_DI),
                     uc.reg_read(X.UC_X86_REG_BP),
                     uc.reg_read(X.UC_X86_REG_DS),
                     uc.reg_read(X.UC_X86_REG_ES),
                     uc.reg_read(X.UC_X86_REG_SS),
                     uc.reg_read(X.UC_X86_REG_SP)))
            sys.stdout.flush()

    def on_stream(self, uc, addr, size, _):
        """At img 0x94, the first LODSW: where the stream is read from.

        The byte offset in the ORIGINAL FILE is not derivable from these
        registers. The relocation loop moved the payload to the top of
        memory before this point, so the stream's segment says where the
        bytes are now, not where they came from.

        So match them instead of computing: read the first bytes of the
        stream and find them in the file. A measurement that can fail
        honestly beats arithmetic that cannot.
        """
        if self.stream is not None:
            return
        ds, si = uc.reg_read(X.UC_X86_REG_DS), uc.reg_read(X.UC_X86_REG_SI)
        head = bytes(uc.mem_read(lin(ds, si), 32))
        at = self.image.find(head)
        again = self.image.find(head, at + 1) if at >= 0 else -1
        self.stream = {
            'stream_seg': ds,
            'stream_off': si,
            'dest_seg': uc.reg_read(X.UC_X86_REG_ES),
            'dest_off': uc.reg_read(X.UC_X86_REG_DI),
            'stream_head': head.hex(),
            'stream_img_off': at,
            'stream_img_off_unique': at >= 0 and again < 0,
        }

    def on_out(self, uc, port, size, value, _):
        # The wrapper's OUT 0x64, 0xAD disables the keyboard interface.
        # Nothing reads it back, so swallowing it is enough.
        return True

    def on_in(self, uc, port, size, _):
        return 0xff

    def on_intr(self, uc, intno, _):
        if intno == 0x21:
            return self.int21(uc)
        if intno == 0x20:
            self.stopped = 'int 20h'
            uc.emu_stop()
            return
        if intno == 0x10:
            self.dos_calls.append('int 10h ax=%04x'
                                  % uc.reg_read(X.UC_X86_REG_AX))
            return
        # Everything else goes through the vector table, because the
        # wrapper installs INT 1 and INT 3 itself and uses them as
        # one-byte calls. Servicing those here instead of dispatching
        # them would silently skip a stage of its chain.
        self.dispatch_ivt(uc, intno)

    def dispatch_ivt(self, uc, intno):
        off = int.from_bytes(uc.mem_read(intno * 4, 2), 'little')
        seg = int.from_bytes(uc.mem_read(intno * 4 + 2, 2), 'little')
        if (seg, off) == (0, 0):
            raise RuntimeError('int %02xh with a null vector at %04x:%04x'
                               % (intno, uc.reg_read(X.UC_X86_REG_CS),
                                  self.ip(uc)))
        # Unicorn leaves IP on the INT instruction, not past it, so the
        # return address has to be advanced over it. INT 3 is one byte;
        # INT n is two.
        ret = self.ip(uc) + (1 if intno == 3 else 2)
        sp = uc.reg_read(X.UC_X86_REG_SP)
        ss = uc.reg_read(X.UC_X86_REG_SS)
        flags = uc.reg_read(X.UC_X86_REG_EFLAGS) & 0xffff
        for word in (flags, uc.reg_read(X.UC_X86_REG_CS), ret):
            sp = (sp - 2) & 0xffff
            uc.mem_write(lin(ss, sp), word.to_bytes(2, 'little'))
        uc.reg_write(X.UC_X86_REG_SP, sp)
        uc.reg_write(X.UC_X86_REG_CS, seg)
        uc.reg_write(X.UC_X86_REG_IP, off)

    def ip(self, uc):
        return uc.reg_read(X.UC_X86_REG_IP) & 0xffff

    def int21(self, uc):
        ah = (uc.reg_read(X.UC_X86_REG_AX) >> 8) & 0xff
        if ah == 0x4c:
            self.stopped = ('int 21h ah=4Ch, exit code %d'
                            % (uc.reg_read(X.UC_X86_REG_AX) & 0xff))
            uc.emu_stop()
            return
        if ah == 0x09:
            at = lin(uc.reg_read(X.UC_X86_REG_DS),
                     uc.reg_read(X.UC_X86_REG_DX))
            out = bytearray()
            for _ in range(256):
                c = uc.mem_read(at, 1)[0]
                if c == ord('$'):
                    break
                out.append(c)
                at += 1
            self.dos_calls.append('int 21h ah=09h print %r' % bytes(out))
            return
        if ah == 0x0b:
            uc.reg_write(X.UC_X86_REG_AX,
                         uc.reg_read(X.UC_X86_REG_AX) & 0xff00)
            self.dos_calls.append('int 21h ah=0Bh stdin check')
            return
        if ah == 0x30:
            uc.reg_write(X.UC_X86_REG_AX, 0x0005)
            self.dos_calls.append('int 21h ah=30h version')
            return
        if ah in (0x25, 0x35):
            self.dos_calls.append('int 21h ah=%02xh vector op' % ah)
            return
        raise RuntimeError('unimplemented INT 21h AH=%02xh at %04x:%04x'
                           % (ah, uc.reg_read(X.UC_X86_REG_CS), self.ip(uc)))

    # ---------------------------------------------------------------- run

    def cur(self):
        return lin(self.uc.reg_read(X.UC_X86_REG_CS), self.ip(self.uc))

    def run(self):
        uc = self.build()

        # Phase A: hooked, just far enough to see the relocation land.
        try:
            uc.emu_start(lin(self.start_cs, self.start_ip), MEM_SIZE,
                         count=self.phase_a)
        except unicorn.UcError as e:
            self.stopped = ('phase A UcError %s at %04x:%04x after %d'
                            % (e, uc.reg_read(X.UC_X86_REG_CS),
                               self.ip(uc), self.insns))
            return self
        if self.stopped:
            return self
        if self.unpacker_cs is None:
            self.stopped = ('phase A ended at %04x:%04x without relocating'
                            % (uc.reg_read(X.UC_X86_REG_CS), self.ip(uc)))
            return self

        # Phase B: unhooked, to the handoff RETF.
        #
        # One exception, and it is a single address rather than a range so
        # it costs one callback for the whole run: img 0x8b, the `mov
        # ds,bp` that installs the segment the bitstream is read from.
        # Where the stream starts is not readable off the listing - SI is
        # zeroed against a segment the relocation computed - and it is the
        # fact a static reimplementation needs most.
        uc.hook_del(self.code_hook)
        stream = lin(self.unpacker_cs, CS_STREAM)
        uc.hook_add(unicorn.UC_HOOK_CODE, self.on_stream,
                    begin=stream, end=stream)
        retf = lin(self.unpacker_cs, CS_RETF)
        try:
            uc.emu_start(self.cur(), retf, count=PHASE_B_INSNS)
        except unicorn.UcError as e:
            self.stopped = ('phase B UcError %s at %04x:%04x'
                            % (e, uc.reg_read(X.UC_X86_REG_CS),
                               self.ip(uc)))
            return self

        if self.cur() != retf:
            self.stopped = ('phase B stopped at %04x:%04x, not the RETF'
                            % (uc.reg_read(X.UC_X86_REG_CS), self.ip(uc)))
            return self

        self.stopped = 'reached the handoff RETF'
        self.harvest()
        return self

    def harvest(self):
        """At the RETF: the original entry is on the stack."""
        uc = self.uc
        ss, sp = uc.reg_read(X.UC_X86_REG_SS), uc.reg_read(X.UC_X86_REG_SP)
        orig_ip = int.from_bytes(uc.mem_read(lin(ss, sp), 2), 'little')
        orig_cs = int.from_bytes(uc.mem_read(lin(ss, sp + 2), 2), 'little')
        ds = uc.reg_read(X.UC_X86_REG_DS)
        self.result = {
            'load_seg': self.load_seg,
            'unpacker_cs': self.unpacker_cs,
            'orig_cs': orig_cs,
            'orig_ip': orig_ip,
            'orig_ss': ss,
            'orig_sp': sp + 4,
            'orig_ds': ds,
            # Relative to the load segment, which is what a different DOS
            # memory map would change. These are the portable numbers and
            # the ones that belong in an MZ header.
            'rel_cs': (orig_cs - self.load_seg) & 0xffff,
            'rel_ss': (ss - self.load_seg) & 0xffff,
            'insns': self.insns,
        }
        if self.stream:
            self.result.update(self.stream)

    def image_out(self, window_paras=0x4000):
        """The depacked image bytes, and how far the real content runs.

        THE END COMES FROM WATCHING THE DECOMPRESSION LOOP STORE, not
        from scanning for trailing zeros. Trimming zeros is wrong
        whenever the image ENDS in them, and LITHIUM.EXE does: its last
        S3M sample ends in silence, so trimming cut 20,688 bytes and
        the module no longer parsed. Nothing about that looked like a
        truncation - the dump was a clean image that simply stopped.

        The write cursor is no help: the decompressor zeroes DI two
        instructions before the handoff. Nor is the stack, because the
        demo's own SS sits INSIDE its image rather than above it.

        The zero scan is kept as a cross-check and reported beside the
        real end, since a large gap between them is worth seeing.
        """
        start = lin(self.load_seg, 0)
        end = min(start + window_paras * 16, MEM_SIZE)
        data = bytes(self.uc.mem_read(start, end - start))
        last = 0
        for i in range(len(data) - 1, -1, -1):
            if data[i]:
                last = i + 1
                break
        self.zero_end = (last + 15) & ~15
        if self.out_end is None:
            return data, self.zero_end
        return data, min((self.out_end + 15) & ~15, len(data))

    def dump(self, path):
        r = self.result
        if not r:
            raise RuntimeError('nothing to dump - the run did not finish')
        data, content = self.image_out()
        with open(path, 'wb') as f:
            f.write(data[:content])
        r['dump_bytes'] = content
        r['store_end'] = self.out_end
        r['zero_end'] = self.zero_end
        return content

    def write_exe(self, path):
        """Rebuild a loadable MZ around the depacked image.

        Worth doing rather than dumping flat bytes because Ghidra's MZ
        loader then derives the segmentation and the entry point itself,
        instead of us asserting a base and a processor at import time.

        e_crlc is 0 and that is not a shortcut: the original relocations
        were applied by the decompressor during the run, so the image is
        already fixed up for LOAD_SEG. Re-listing them would apply them
        twice. The consequence is that this EXE is only correct when
        loaded at LOAD_SEG, which is what makes LOAD_SEG = Ghidra's own
        MZ base the right choice - see the note on it above.
        """
        r = self.result
        if not r:
            raise RuntimeError('nothing to write - the run did not finish')
        data, content = self.image_out()
        body = data[:content]

        hdr = bytearray(0x20)
        pages = (len(body) + 0x20 + 511) // 512
        last = (len(body) + 0x20) % 512

        def put(off, val):
            hdr[off:off + 2] = int(val & 0xffff).to_bytes(2, 'little')

        put(0x00, 0x5a4d)                 # 'MZ'
        put(0x02, last)
        put(0x04, pages)
        put(0x06, 0)                      # e_crlc - see the docstring
        put(0x08, 2)                      # 2 paragraphs of header
        put(0x0a, self.u16(0x0a))         # keep the original's BSS ask
        put(0x0c, self.u16(0x0c))
        put(0x0e, r['rel_ss'])
        put(0x10, r['orig_sp'])
        put(0x12, 0)
        put(0x14, r['orig_ip'])
        put(0x16, r['rel_cs'])
        put(0x18, 0x20)                   # e_lfarlc past the header
        put(0x1a, 0)
        with open(path, 'wb') as f:
            f.write(bytes(hdr))
            f.write(body)
        r['exe_bytes'] = len(body) + 0x20
        return len(body) + 0x20

    # ------------------------------------------------------------- report

    def report(self):
        print('%s depack run' % os.path.basename(self.path))
        print('  image      file 0x%x, %d bytes, loaded at %04x:0000'
              % (self.image_at, len(self.image), self.load_seg))
        print('  entry      %s %04x:%04x'
              % ('MZ wrapper' if self.start_at_wrapper else 'image head',
                 self.start_cs, self.start_ip))
        print('  stopped    %s' % self.stopped)
        print('  insns      %d (phase A hooked)' % self.insns)
        if self.unpacker_cs is not None:
            print('  unpacker   relocated to %04x:0000 (%+d paragraphs '
                  'from the load segment)'
                  % (self.unpacker_cs, self.unpacker_cs - self.load_seg))
        if self.dos_calls:
            print('  dos')
            for c in self.dos_calls[:20]:
                print('    %s' % c)
        if self.seg_log:
            print('  cs changes in phase A (%d)' % len(self.seg_log))
            for cs, ip, n in self.seg_log[:20]:
                print('    %04x:%04x   after %d insns' % (cs, ip, n))
        r = self.result
        if r:
            print('  ORIGINAL ENTRY')
            print('    cs:ip    %04x:%04x   = load_seg + %04x : %04x'
                  % (r['orig_cs'], r['orig_ip'], r['rel_cs'], r['orig_ip']))
            print('    ss:sp    %04x:%04x   = load_seg + %04x : %04x'
                  % (r['orig_ss'], r['orig_sp'], r['rel_ss'], r['orig_sp']))
            print('    ds       %04x' % r['orig_ds'])
        if self.stream:
            t = self.stream
            print('  BITSTREAM')
            print('    read   from %04x:%04x' % (t['stream_seg'],
                                                 t['stream_off']))
            print('    write  to   %04x:%04x' % (t['dest_seg'],
                                                 t['dest_off']))
            print('    first bytes %s' % t['stream_head'][:32])
            if t['stream_img_off'] >= 0:
                print('    those bytes are at img 0x%x of the packed load '
                      'image%s'
                      % (t['stream_img_off'],
                         '' if t['stream_img_off_unique']
                         else '  (NOT UNIQUE - also found elsewhere)'))
            else:
                print('    those bytes are not in the packed image as '
                      'loaded - the relocation altered them')


def main(argv):
    src = argv[argv.index('--in') + 1] if '--in' in argv else EXE
    trace = int(argv[argv.index('--trace') + 1]) if '--trace' in argv else 0
    out = argv[argv.index('-o') + 1] if '-o' in argv else None
    exe = argv[argv.index('--exe') + 1] if '--exe' in argv else None

    phase_a = (int(argv[argv.index('--phasea') + 1])
               if '--phasea' in argv else PHASE_A_INSNS)
    d = Depacker(src, start_at_wrapper='--wrapper' in argv, trace=trace,
                 phase_a=phase_a)
    d.run()
    print('')
    d.report()

    if (out or exe) and not d.result:
        sys.exit('refusing to write from a run that did not finish')

    wrote = []
    if out:
        wrote.append((out, d.dump(out)))
    if exe:
        wrote.append((exe, d.write_exe(exe)))
    if wrote:
        side = os.path.splitext(wrote[0][0])[0] + '.json'
        with open(side, 'w') as f:
            json.dump(d.result, f, indent=2, sort_keys=True)
            f.write('\n')
        print('')
        for p, n in wrote:
            print('  wrote %-44s %d bytes' % (p, n))
        print('  wrote %s' % side)


if __name__ == '__main__':
    main(sys.argv[1:])
