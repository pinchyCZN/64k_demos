"""Pull an embedded Scream Tracker 3 module out of a depacked image.

    py -3.10 tools/extract_s3m.py reverse/unpacked/xcursion.bin
    py -3.10 tools/extract_s3m.py <image> -o <out.s3m> [--at 0xcda0]
    py -3.10 tools/extract_s3m.py <image> -o <out.s3m> --force

--force overrides the sample-content refusal below. The refusal exists so
a broken slice is never mistaken for a good one; --force is for when the
brokenness is the thing being examined.

An S3M carries no total length, so the end has to be DERIVED from the
parapointers: the furthest of every pattern's end and every sample's
end. That is also the check that the parse is right - a wrong base or
a misread pointer puts the end past the image, and this refuses rather
than writing a truncated file.

Shared rather than per-demo: nothing here knows which demo it is
reading. It finds the SCRM magic itself and works from there.

The module is the demo's own music. It is derived from a binary we do
not redistribute, so the bytes stay local and uncommitted - the same
footing as the depacked image. See CLAUDE.md rule 11 and the legal
note.
"""
import math
import os
import struct
import sys

# cwtv high nibble. An S3M is not necessarily a Scream Tracker file -
# LITHIUM's was saved by Impulse Tracker - and saying so matters,
# because the tracker decides what the sample flags mean.
TRACKERS = {1: 'Scream Tracker', 2: 'Imago Orpheus', 3: 'Impulse Tracker',
            4: 'Schism Tracker', 5: 'OpenMPT', 6: 'BeRoTracker',
            7: 'CreamTracker'}

# Does a byte range look like 8-bit PCM? It takes BOTH tests, because
# either one alone has false positives:
#
#   mean absolute first difference - audio is smooth, because
#   consecutive samples correlate. Two uniform random bytes average 85
#   apart, so a high value suggests compressed or encoded data.
#
#   entropy - compressed data is high-entropy AS WELL AS rough. A noisy
#   percussion sample is rough but LOW entropy, because it bounces
#   between a few levels, and XCURSION has one: instrument 15, mean
#   difference 114 and entropy 2.7, which is perfectly good audio.
#
# This is the check that caught LITHIUM, where every structural test
# passed and the sample data was not sample data at all.
ROUGH_MIN = 60
ENTROPY_MIN = 6.0

# Below this share of usable instruments the module is not worth
# writing: it would play, and sound like static.
USABLE_MIN = 0.5

MAGIC = b'SCRM'
MAGIC_AT = 0x2c                                 # offset of SCRM in the header
HDR = 0x60                                      # fixed header, then the lists
INS_HDR = 0x50                                  # an instrument header
S3M_TYPE = 0x10


def find_modules(img):
    """Every plausible module base in the image, by the SCRM magic."""
    out, i = [], 0
    while True:
        i = img.find(MAGIC, i)
        if i < 0:
            return out
        base = i - MAGIC_AT
        if (base >= 0 and img[base + 0x1c] == 0x1a
                and img[base + 0x1d] == S3M_TYPE):
            out.append(base)
        i += len(MAGIC)


def parse(img, base):
    """Return a dict describing the module at base, including its end."""
    ordnum, insnum, patnum = struct.unpack_from('<3H', img, base + 0x20)
    o = base + HDR
    orders = img[o:o + ordnum]
    o += ordnum
    ins = struct.unpack_from('<%dH' % insnum, img, o)
    o += insnum * 2
    pat = struct.unpack_from('<%dH' % patnum, img, o)
    o += patnum * 2
    if img[base + 0x35] == 0xfc:                # the pan table is optional
        o += 32
    end = o
    samples = []
    for n, p in enumerate(ins):
        if not p:
            continue
        h = base + p * 16
        end = max(end, h + INS_HDR)
        typ = img[h]
        name = img[h + 1:h + 13].split(b'\x00')[0].decode('latin-1')
        # The sample pointer is stored hi:lo:lo across 0x0d..0x0f.
        seg = (img[h + 0x0e] | (img[h + 0x0f] << 8)) | (img[h + 0x0d] << 16)
        length = struct.unpack_from('<I', img, h + 0x10)[0]
        samples.append((n + 1, typ, name, seg, length))
        if typ == 1 and seg:
            end = max(end, base + seg * 16 + length)
    for p in pat:
        if not p:
            continue
        h = base + p * 16
        # A packed pattern's length word COUNTS ITSELF, so the end is
        # h + size, not h + 2 + size.
        end = max(end, h + struct.unpack_from('<H', img, h)[0])
    return {'base': base, 'end': end, 'orders': ordnum, 'ins': insnum,
            'pat': patnum, 'order_list': orders, 'samples': samples,
            'cwtv': struct.unpack_from('<H', img, base + 0x28)[0],
            'title': img[base:base + 28].split(b'\x00')[0].decode('latin-1')}


def audio_shape(d):
    """(mean absolute first difference, entropy, zero fraction)."""
    if len(d) < 64:
        return None
    n = float(len(d))
    dif = sum(abs(d[i + 1] - d[i]) for i in range(len(d) - 1)) / (n - 1)
    h = [0] * 256
    for b in d:
        h[b] += 1
    ent = -sum((c / n) * math.log(c / n, 2) for c in h if c)
    return dif, ent, h[0] / n


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    path = argv[0]
    out = argv[argv.index('-o') + 1] if '-o' in argv else None
    at = int(argv[argv.index('--at') + 1], 0) if '--at' in argv else None
    force = '--force' in argv
    img = open(path, 'rb').read()
    bases = [at] if at is not None else find_modules(img)
    if not bases:
        print('no S3M header found in %s' % path)
        return 1
    rc = 0
    for base in bases:
        m = parse(img, base)
        size = m['end'] - m['base']
        print('S3M at img 0x%04x, SCRM at 0x%04x' % (base, base + MAGIC_AT))
        print('  %d orders, %d instruments, %d patterns, %s %x.%02x'
              % (m['orders'], m['ins'], m['pat'],
                 TRACKERS.get(m['cwtv'] >> 12,
                              'tracker %x' % (m['cwtv'] >> 12)),
                 (m['cwtv'] >> 8) & 0xf, m['cwtv'] & 0xff))
        print('  spans img 0x%04x..0x%04x = %d bytes, %.0f%% of the image'
              % (m['base'], m['end'], size, 100.0 * size / len(img)))
        if m['end'] > len(img):
            print('  REFUSING: the derived end is past the image, so the base'
                  ' or a pointer is wrong')
            rc = 1
            continue
        named = [s for s in m['samples'] if s[2]]
        print('  %d named samples:' % len(named))
        for n, typ, name, seg, length in named:
            print('    %2d  type %d  %-14s %6d bytes' % (n, typ, name, length))

        rough, dead, tested = [], [], 0
        for n, typ, name, seg, length in m['samples']:
            if typ != 1 or not seg or not length:
                continue
            shape = audio_shape(img[m['base'] + seg * 16:][:length])
            if shape is None:
                continue
            dif, ent, zf = shape
            tested += 1
            if zf == 1.0:
                dead.append(n)
            elif dif > ROUGH_MIN and ent > ENTROPY_MIN:
                rough.append((n, dif, ent))
        if tested:
            usable = tested - len(rough) - len(dead)
            if rough or dead:
                print('  SAMPLE DATA THAT DOES NOT LOOK LIKE AUDIO:')
                if dead:
                    print('    all zero bytes:  instruments %s'
                          % ', '.join(str(n) for n in dead))
                for n, dif, ent in rough:
                    print('    instrument %-3d   mean difference %3.0f,'
                          ' entropy %.1f - compressed or encoded, not PCM'
                          % (n, dif, ent))
            print('  %d of %d instruments carry playable PCM'
                  % (usable, tested))
            if usable < USABLE_MIN * tested:
                if not force:
                    print('  REFUSING: most of the sample data is not audio,'
                          ' so this slice would play and sound like static.'
                          ' The samples are not in the image in this form -'
                          ' find what builds them at run time.')
                    print('  --force writes it anyway, for listening to.')
                    rc = 1
                    continue
                print('  --force: writing it anyway. THE PATTERNS AND THE'
                      ' ORDER LIST ARE INTACT and only the sample data is'
                      ' not, so the arrangement is worth hearing even where'
                      ' the voices are not.')
        if out:
            with open(out, 'wb') as f:
                f.write(img[m['base']:m['end']])
            print('  wrote %s (%d bytes)' % (out, size))
            print('  NOT FOR REDISTRIBUTION - see this file\'s docstring')
        else:
            print('  (no -o given, nothing written)')
    return rc


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
