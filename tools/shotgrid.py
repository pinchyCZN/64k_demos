"""Turn a frame into a PNG with a framebuffer coordinate grid.

Takes either a dos_shot BMP or a raw 320x200 index dump of the back
buffer, which is the one tied to a known frame - the window surface
does not refresh per step while halted, so four dos_shots taken a
frame apart can come back byte-identical.

OURS IN FULL. dos_shot writes the real window image, which is the
320x200 framebuffer at whatever integer zoom the window happens to
be, so a position read off it is not a framebuffer position. This
labels the grid in FRAMEBUFFER coordinates - the ones a watch
address is computed from - so a pixel can be named without anyone
doing the division.

    py -3.10 tools/shotgrid.py build/sh1.bmp build/sh1.png
    py -3.10 tools/shotgrid.py build/bb1.bin build/bb1.png --zoom 3

A .bin source is 64,000 palette indices and is coloured through the
demo's own stored palette: dac = clamp(store + (fade >> 2), 0, 0x3f),
src/lithium/lithium_demo.c. Pass --fade when g_PalFade is not 0.

A pixel named (x, y) here is guest linear

    0x3c770 + y * 320 + x        the back buffer
    0x0a0000 + y * 320 + x       what the card shows

and the first of those is what Poly_Fill writes - see
docs/lithium/README.txt section 11.2.
"""

import argparse
import struct
import sys
import zlib

FB_W, FB_H = 320, 200
IMAGE = 'reverse/unpacked/lithium.bin'
PAL_1 = 0x099be         # what the flyover selects - lithium_assets.h

# A 3x5 digit, five rows of three bits, so a label needs no font file.
DIGITS = (
    0o75557, 0o22222, 0o71747, 0o71717, 0o55711,
    0o74717, 0o74757, 0o71111, 0o75757, 0o75711)
GLYPH = {}
for _d in range(10):
    _rows = []
    for _r in range(5):
        _v = (DIGITS[_d] >> (3 * (4 - _r))) & 7
        _rows.append(((_v >> 2) & 1, (_v >> 1) & 1, _v & 1))
    GLYPH[str(_d)] = _rows


def read_bmp(path):
    """(width, height, [row][col] -> (r,g,b)) for a 24 or 32bpp BMP."""
    d = open(path, 'rb').read()
    if d[:2] != b'BM':
        raise SystemExit('%s: not a BMP' % path)
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    if bpp not in (24, 32):
        raise SystemExit('%s: %d bits per pixel, want 24 or 32' % (path, bpp))
    step = bpp // 8
    flip = h > 0                  # a positive height is bottom-up
    h = abs(h)
    stride = ((w * step + 3) // 4) * 4
    rows = []
    for y in range(h):
        src = off + (h - 1 - y if flip else y) * stride
        row = []
        for x in range(w):
            p = src + x * step
            row.append((d[p + 2], d[p + 1], d[p]))
        rows.append(row)
    return w, h, rows


def read_raw(path, pal_off, fade, zoom):
    """A 320x200 index dump through the stored palette, nearest-zoomed."""
    d = open(path, 'rb').read()
    if len(d) < FB_W * FB_H:
        raise SystemExit('%s: %d bytes, want %d'
                         % (path, len(d), FB_W * FB_H))
    img = open(IMAGE, 'rb').read()
    bias = fade >> 2
    lut = []
    for i in range(256):
        rgb = []
        for k in range(3):
            v = img[pal_off + i * 3 + k] + bias
            v = 0 if v < 0 else 0x3f if v > 0x3f else v
            rgb.append(v * 255 // 0x3f)
        lut.append(tuple(rgb))
    rows = []
    for y in range(FB_H):
        row = []
        for x in range(FB_W):
            row.extend([lut[d[y * FB_W + x]]] * zoom)
        for _ in range(zoom):
            rows.append(list(row))
    return FB_W * zoom, FB_H * zoom, rows


def put(rows, x, y, rgb):
    if 0 <= y < len(rows) and 0 <= x < len(rows[0]):
        rows[y][x] = rgb


def text(rows, x, y, s, rgb, scale):
    """A 3x5 digit string, scaled, with one pixel between characters."""
    for ch in s:
        g = GLYPH.get(ch)
        if g:
            for gy in range(5):
                for gx in range(3):
                    if g[gy][gx]:
                        for sy in range(scale):
                            for sx in range(scale):
                                put(rows, x + gx * scale + sx,
                                    y + gy * scale + sy, rgb)
        x += (3 + 1) * scale


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--step', type=int, default=20,
                    help='grid spacing in framebuffer pixels')
    ap.add_argument('--major', type=int, default=100,
                    help='a brighter line every this many')
    ap.add_argument('--zoom', type=int, default=2,
                    help='nearest-neighbour zoom, a .bin source only')
    ap.add_argument('--fade', type=lambda v: int(v, 0), default=0,
                    help='g_PalFade, a .bin source only')
    ap.add_argument('--pal', type=lambda v: int(v, 0), default=PAL_1,
                    help='the stored palette, as an image offset')
    a = ap.parse_args()

    if a.src.lower().endswith('.bin'):
        w, h, rows = read_raw(a.src, a.pal, a.fade, a.zoom)
    else:
        w, h, rows = read_bmp(a.src)
    zx, zy = w // FB_W, h // FB_H
    if zx < 1 or zy < 1:
        raise SystemExit('%s is %dx%d, smaller than the framebuffer'
                         % (a.src, w, h))
    print('%s  %dx%d, zoom %dx%d -> grid every %d framebuffer px'
          % (a.src, w, h, zx, zy, a.step))

    THIN, THICK, LABEL = (0, 190, 190), (255, 90, 90), (255, 255, 60)
    for fx in range(0, FB_W + 1, a.step):
        major = fx % a.major == 0
        c = THICK if major else THIN
        x = min(fx * zx, w - 1)
        for y in range(h):
            if major or (y // max(zy, 1)) % 4 < 2:
                put(rows, x, y, c)
    for fy in range(0, FB_H + 1, a.step):
        major = fy % a.major == 0
        c = THICK if major else THIN
        y = min(fy * zy, h - 1)
        for x in range(w):
            if major or (x // max(zx, 1)) % 4 < 2:
                put(rows, x, y, c)

    # The labels go INSIDE the frame; a reading at the edge would
    # otherwise fall off the image.
    for fx in range(0, FB_W, a.step):
        text(rows, fx * zx + 2, 2, str(fx), LABEL, max(1, zy // 2))
    for fy in range(a.step, FB_H, a.step):
        text(rows, 2, fy * zy + 2, str(fy), LABEL, max(1, zy // 2))

    raw = bytearray()
    for row in rows:
        raw.append(0)
        for r, g, b in row:
            raw += bytes((r, g, b))

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data
                + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))

    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(bytes(raw), 6))
           + chunk(b'IEND', b''))
    open(a.dst, 'wb').write(png)
    print('  wrote %s' % a.dst)
    return 0


if __name__ == '__main__':
    sys.exit(main())
