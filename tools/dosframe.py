"""Turn an emulator frame into the same P6 the PPM host writes.

    py -3.10 tools/dosframe.py FB.bin PAL.bin OUT.ppm [--fade N] [--dosbox]

DO NOT COMPARE PICTURES - COMPARE INDICES AND PALETTES SEPARATELY.
The emulator's window and our P6 disagree by construction: DOSBox
widens a 6-bit DAC component by BIT REPLICATION, (v << 2) | (v >> 4),
and src/main_ppm.c widens it by (v * 255) / 63. Measured on one frame,
33 of 52 live indices match the first and NONE match the second, so an
RGB diff reports a difference that is only the two formulas.

The comparison that means something needs no palette at all:

    our Demo_Pixels()   vs  dos_dump @a0000     both 8-bit indices
    our Demo_Palette()  vs  dos_dump <palette>  both 6-bit components

Each is byte-exact, and a mismatch lands in the raster or in the
palette rather than somewhere between them. This file is for LOOKING
at an emulator frame, and --dosbox makes that look match the window.

Get the inputs with the debugger's own tools - a mode 13h frame is
0xa0000 and the demo's DAC copy is wherever its palette lives:

    dos_dump addr=@a0000 length=64000 path=FB.bin
    dos_dump addr=@b10e  length=768   path=PAL.bin     LITHIUM

THE FADE IS NOT IN THE PALETTE BUFFER. Lithium's Pal_Upload biases
every component by g_PalFade >> 2 on the way to the DAC and clamps at
0x3f, so a dump of g_PalMain alone is the UNFADED palette - read
g_PalFade too and pass it, or a frame mid-fade comes out too bright.
docs/lithium/effects/rockintro.txt.
"""

import sys

W, H = 320, 200


def resolve(fb, pal, fade=0, dosbox=False):
    """Indices and a 6-bit DAC to RGB24.

    dosbox=True reproduces what the emulator's window shows; the
    default reproduces what src/main_ppm.c writes. See the header for
    why those are not the same picture.
    """
    bias = fade >> 2            # arithmetic, and fade is often negative
    lut = []
    for i in range(256):
        out = []
        for c in pal[i * 3:i * 3 + 3]:
            v = c + bias
            v = 0 if v < 0 else (0x3f if v > 0x3f else v)
            out.append(((v << 2) | (v >> 4)) if dosbox else (v * 255) // 63)
        lut.append(bytes(out))
    return b''.join(lut[b] for b in fb)


def main(argv):
    if len(argv) < 3:
        print(__doc__.strip())
        return 2
    fade = 0
    dosbox = '--dosbox' in argv
    if dosbox:
        argv.remove('--dosbox')
    if '--fade' in argv:
        i = argv.index('--fade')
        fade = int(argv[i + 1], 0)
        # a word read back as unsigned is still a signed fade
        if fade > 0x7fff:
            fade -= 0x10000
        del argv[i:i + 2]

    fb = open(argv[0], 'rb').read()
    pal = open(argv[1], 'rb').read()
    if len(fb) < W * H:
        print('fb is %d bytes, want %d' % (len(fb), W * H))
        return 1
    if len(pal) < 768:
        print('palette is %d bytes, want 768' % len(pal))
        return 1

    rgb = resolve(fb[:W * H], pal, fade, dosbox)
    with open(argv[2], 'wb') as f:
        f.write(b'P6\n%d %d\n255\n' % (W, H))
        f.write(rgb)
    print('wrote %s  %dx%d  fade=%d bias=%d  widen=%s'
          % (argv[2], W, H, fade, fade >> 2,
             'dosbox' if dosbox else 'main_ppm'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
