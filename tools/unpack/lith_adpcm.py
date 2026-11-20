"""Expand LITHIUM's music samples out of the depacked image.

    py -3.10 tools/unpack/lith_adpcm.py reverse/unpacked/lithium.bin \
        -o reverse/unpacked/lithium_kypros.s3m

LITHIUM's embedded module looks broken when it is sliced straight out
of the image: five instruments read as all zeros, five more as noise,
and only one as PCM. None of that is damage. THE SAMPLES ARE STORED
AS A 3-BIT ADPCM STREAM and the demo expands them at startup, so what
the image holds is the compressed form.

This is img 0x8007, reimplemented. Demo_Main's first call reaches it
before anything is drawn:

    ES = 0x3ad2;  DS = 0x2ad2;  REP MOVSD 0x4000      the 64 KB copy
    di = first instrument's sample pointer
    si = di + [di] + 4                                the bitstream
    step index = 0x14, value = 0x80
    until di wraps to 0:
        3 bits: two of magnitude, one of sign
        value += sign * magnitude * step[index]       clamped 0..0xff
        index += delta[magnitude]                     clamped 0..0x1e
    then [di] bytes from di + 4 are copied raw to [di + 2]

TWO SEGMENTS, AND THAT IS THE TRAP. The stream is read through DS and
the output written through ES, so the read cursor walks the ORIGINAL
while the write cursor fills the copy. Decoding in one buffer works
for 36 bytes - until the write cursor catches the read cursor - and
then quietly produces noise.

Verified byte for byte against 63,758 bytes dumped from segment
0x3ad2 of the running demo.
"""
import struct
import sys

MODULE_AT = 0x1ad20         # segment 0x2ad2, where the module sits
SEG_BYTES = 0x10000
STEP_AT = 0x8188            # img, 16 words
DELTA_AT = 0x81a8           # img, 4 words
VALUE_0 = 0x80              # the running value starts centred
INDEX_0 = 0x14
INDEX_MAX = 0x1e


def expand(img, module_at=MODULE_AT):
    """The depacked image in, the module with its samples expanded out."""
    src = img[module_at:module_at + SEG_BYTES]
    if len(src) < SEG_BYTES:
        src = src + b'\x00' * (SEG_BYTES - len(src))
    out = bytearray(src)
    step = struct.unpack_from('<16h', img, STEP_AT)
    delta = struct.unpack_from('<4h', img, DELTA_AT)

    orders = struct.unpack_from('<H', src, 0x20)[0]
    first = struct.unpack_from('<H', src, 0x60 + orders)[0]
    di = base = struct.unpack_from('<H', src, first * 16 + 0x0e)[0] << 4
    si = (base + struct.unpack_from('<H', src, base)[0] + 4) & 0xffff

    index, acc, bits, value = INDEX_0, 0, 0, VALUE_0
    while True:
        if bits < 3:
            word = struct.unpack_from('<H', src, si)[0]
            si = (si + 1) & 0xffff
            acc = (acc | ((word << bits) & 0xffff)) & 0xffff
            bits += 8
        mag = acc & 3
        step_d = mag * step[index >> 1]
        value += -step_d if acc & 4 else step_d
        value = 0 if value < 0 else (0xff if value > 0xff else value)
        out[di] = value
        if index == 0 and value == 0:
            index = INDEX_0
        index += delta[mag]
        index = 0 if index < 0 else (INDEX_MAX if index > INDEX_MAX else index)
        acc >>= 3
        bits -= 3
        di = (di + 1) & 0xffff
        if di == 0:
            break

    count = struct.unpack_from('<H', src, base)[0]
    dst = struct.unpack_from('<H', src, base + 2)[0]
    out[dst:dst + count] = src[base + 4:base + 4 + count]
    return bytes(out)


def module_end(mod):
    """The furthest byte any pattern or sample reaches - an S3M carries
    no total length, so the end has to be derived."""
    orders = struct.unpack_from('<H', mod, 0x20)[0]
    ins = struct.unpack_from('<H', mod, 0x22)[0]
    pat = struct.unpack_from('<H', mod, 0x24)[0]
    ins_at = 0x60 + orders
    pat_at = ins_at + ins * 2
    end = pat_at + pat * 2
    for i in range(ins):
        para = struct.unpack_from('<H', mod, ins_at + i * 2)[0]
        if not para:
            continue
        hdr = para * 16
        length = struct.unpack_from('<I', mod, hdr + 0x10)[0]
        lo = mod[hdr + 0x0d]
        seg = struct.unpack_from('<H', mod, hdr + 0x0e)[0]
        end = max(end, ((seg | (lo << 16)) * 16) + length)
    for i in range(pat):
        para = struct.unpack_from('<H', mod, pat_at + i * 2)[0]
        if not para:
            continue
        at = para * 16
        end = max(end, at + 2 + struct.unpack_from('<H', mod, at)[0])
    return end


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    path = argv[0]
    out = argv[argv.index('-o') + 1] if '-o' in argv else None
    img = open(path, 'rb').read()
    mod = expand(img)

    if mod[0x2c:0x30] != b'SCRM':
        print('no SCRM at the expected offset - wrong image?')
        return 1
    end = module_end(mod)
    print('module at img 0x%05x, %d bytes once expanded' % (MODULE_AT, end))

    orders = struct.unpack_from('<H', mod, 0x20)[0]
    ins = struct.unpack_from('<H', mod, 0x22)[0]
    ins_at = 0x60 + orders
    good = 0
    for i in range(ins):
        para = struct.unpack_from('<H', mod, ins_at + i * 2)[0]
        if not para:
            continue
        hdr = para * 16
        length = struct.unpack_from('<I', mod, hdr + 0x10)[0]
        lo = mod[hdr + 0x0d]
        seg = struct.unpack_from('<H', mod, hdr + 0x0e)[0]
        at = (seg | (lo << 16)) * 16
        if length and any(mod[at:at + length]):
            good += 1
    print('  %d of %d instruments carry sample data' % (good, ins))

    if out:
        with open(out, 'wb') as f:
            f.write(mod[:end])
        print('  wrote %s (%d bytes)' % (out, end))
        print('  NOT FOR REDISTRIBUTION - see this file\'s docstring')
    else:
        print('  (no -o given, nothing written)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
