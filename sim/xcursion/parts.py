#!/usr/bin/env python3
"""EXCURSION's routines, transcribed. One function per Ghidra function.

ADDRESSES ARE THE DEPACKED IMAGE, XCURSION_U.EXE, based at 0x1000; the
packed original's are unrelated (rule 3). What each routine does and the
maths behind it: docs/effects/script1_landscape.txt.

The sine table is READ FROM THE DUMP, never generated. It is near
4095*sin(2*pi*k/512) but not equal to it, so a formula here would be a
tuned constant standing in for a real one (rule 12).

IT IS A FLAT 640-DWORD TABLE AT A 4-BYTE STRIDE: 512 steps plus a
128-entry tail, which exists so the COSINE is the same table read
0x200 bytes on and never runs off the end. Reading it as 256 entries of
8 bytes also "works" - every other dword is a 256-step sine, which is
what the height fields use - but the voxel camera indexes it with
`& 0x7fc` at the full 512 steps, and the pair reading gets that wrong
by a factor of two.
"""

import math
import os
import struct

def _repo_root():
    """Walk up until CLAUDE.md is found, rather than assuming how deep
    this module sits. It has moved once already - sim/ to
    sim/xcursion/ - and a hardcoded '..' broke silently when it did."""
    d = os.path.dirname(os.path.abspath(__file__))
    for _ in range(6):
        if os.path.exists(os.path.join(d, 'CLAUDE.md')):
            return d
        d = os.path.dirname(d)
    raise RuntimeError('cannot find the repo root above %s' % __file__)


ROOT = _repo_root()
DUMP = os.path.join(ROOT, 'reverse', 'unpacked', 'xcursion.bin')

SCREEN_W, SCREEN_H = 320, 200      # mode 13h, set at img 0x0050
FIELD_W = FIELD_H = 128            # the loop bounds at img 0x5081

IMG_SIN_TABLE = 0x7f5e             # g_SinTable, 640 dwords
COS_SKEW = 0x200 // 4              # the cosine is +0x200 bytes on


def load_sin_table(path=DUMP):
    """ORIGIN img 0x7f5e. 640 dwords at a 4-byte stride."""
    if not os.path.exists(path):
        raise SystemExit(
            'the depacked image is not there:\n  %s\n'
            'it is derived from a binary we do not redistribute, so it is\n'
            'not committed. Regenerate it with:\n'
            '  py -3.10 tools/unpack/xcur_unpack.py -o %s'
            % (path, os.path.relpath(path, ROOT)))
    with open(path, 'rb') as f:
        blob = f.read()
    return [struct.unpack_from('<i', blob, IMG_SIN_TABLE + k * 4)[0]
            for k in range(640)]


SIN_FLAT = load_sin_table()

# The height field generators index with an 8-byte stride, which is
# every other dword - a 256-step sine. Naming the two resolutions
# apart is what stops them being confused again.
SIN = SIN_FLAT[::2][:256]


def sin_at(byte_off):
    """The table as the code reads it: a BYTE offset, masked 0x7fc."""
    return SIN_FLAT[(byte_off & 0x7fc) >> 2]


def cos_at(byte_off):
    """The same table 0x200 bytes on, which is the cosine."""
    return SIN_FLAT[((byte_off & 0x7fc) >> 2) + COS_SKEW]


def rand_state(a=0x9e8e, b=0x636d):
    """ORIGIN 1000:410a; seeds from img 0x4f64 and 0x4f6b.

    The seed is fixed in the binary, so the "random" cloud is identical
    on every run of the real demo.
    """
    while True:
        # xchg ah, dl  then  ax += dx
        swapped = ((a & 0x00ff) | ((b & 0x00ff) << 8)) & 0xffff
        b_hi = (b & 0xff00) | ((a >> 8) & 0xff)
        nxt = (swapped + b_hi) & 0xffff
        a, b = b_hi, nxt
        yield ((nxt << 2) | (nxt >> 14)) & 0xffff


_RAND = rand_state()


def rand_shared():
    """ORIGIN img 0x9879 - THE WHOLE DEMO DRAWS FROM ONE STREAM.

    Nothing re-seeds it except img 0x4f64 and img 0x514a, so a field's
    bytes depend on how many draws came before it: 0x10000 per
    plasma_generate, 3000 per make_points. See
    effects/script1_landscape.txt section 6.1.

    SO A PART VIEWED ON ITS OWN HAS THE STREAM AT THE WRONG POSITION,
    unless its generate is one of the two that directly follow a
    re-seed - img 0x4f7d and img 0x515d. For every other part the field
    here is a cloud of the right character and the wrong values; only a
    replay from the demo's start gives the demo's own. src/ does that
    replay; this does not.
    """
    return _RAND


def rand_restart():
    """ORIGIN img 0x4f64 - put the stream back to the image's statics."""
    global _RAND
    _RAND = rand_state()
    return _RAND


def build_height_field_a():
    """ORIGIN 1000:507a - 1000:50c0. See effects/script1_landscape 5.

    x and y count DOWN from 0x80 while the destination index counts up,
    so the field is mirrored on both axes against the obvious reading.
    """
    field = bytearray(FIELD_W * FIELD_H)
    di = 0
    for cx in range(0x80, 0, -1):
        for dx in range(0x80, 0, -1):
            a = SIN[(2 * dx + cx) & 0xff]
            b = SIN[(cx - 2 * dx) & 0xff]
            field[di] = ((a + b) >> 7) + 0x80 & 0xff
            di += 1
    return field


def build_height_field_b():
    """ORIGIN 1000:3a7f - 1000:3ac4. Terrain A at double frequency.

    NO +0x80 BIAS: img 0x3aba stores AL straight, so unlike A this field
    is signed and spans 0xf1..0x0f rather than 0x41..0xbf.
    """
    field = bytearray(FIELD_W * FIELD_H)
    di = 0
    for cx in range(0x80, 0, -1):
        for dx in range(0x80, 0, -1):
            a = SIN[(2 * (2 * dx + cx)) & 0xff]
            b = SIN[(2 * (cx - 2 * dx)) & 0xff]
            field[di] = ((a + b) >> 9) & 0xff
            di += 1
    return field


def make_points(count=1000):
    """ORIGIN 1000:42b5. CX = 0xbb8 words is exactly 1000 xyz triples,
    which is how the count and the record layout confirm each other.

    3000 draws off the shared stream - see rand_shared.
    """
    gen = rand_shared()
    pts = []
    for _ in range(count):
        x, y, z = next(gen), next(gen), next(gen)
        pts.append((x - 0x10000 if x & 0x8000 else x,
                    y - 0x10000 if y & 0x8000 else y,
                    z - 0x10000 if z & 0x8000 else z))
    return pts


def points_render(fb, pts, cam):
    """ORIGIN 1000:3207.

    The divisor is masked to 9 bits BEFORE the +5, so z wraps instead of
    clamping and a far point reappears near. Behaviour, not a bug.
    """
    cx, cy, cz = cam
    for px, py, pz in pts:
        d = (((pz - cz) >> 6) & 0x1ff) + 5
        sx = _idiv(px - cx, d) + 160
        if not 0 <= sx < 0x140:
            continue
        sy = _idiv(py - cy, d) + 100
        if not 0 <= sy < 0xc7:
            continue
        fb[sy * SCREEN_W + sx] = (0x100 - (d >> 3)) & 0xff


def _idiv(n, d):
    """x86 IDIV truncates toward zero; Python's // floors."""
    q = abs(n) // abs(d)
    return -q if (n < 0) != (d < 0) else q


def field_to_screen(fb, field, stretch=True):
    """OURS, not a transcription - the demo never draws a field directly.

    STRETCHED BY DEFAULT, and that is the honest choice for a debug
    view rather than a cheat: terrain A only spans 0x40..0xbf, so drawn
    raw against a grey palette it is a flat grey square, and terrain B
    is a SIGNED field so drawn raw it is near-black against near-white.
    Both looked broken and were correct. Pass stretch=False for the
    literal bytes.
    """
    lo, hi = (min(field), max(field)) if stretch else (0, 255)
    span = max(1, hi - lo)
    for y in range(FIELD_H):
        row = y * FIELD_W
        out = (y + 36) * SCREEN_W + 96
        if stretch:
            fb[out:out + FIELD_W] = bytes(
                (field[row + x] - lo) * 255 // span for x in range(FIELD_W))
        else:
            fb[out:out + FIELD_W] = field[row:row + FIELD_W]


def plasma_to_screen(fb, buf):
    """OURS. A 320x200 window into the 256x256 torus, wrapped.

    The demo never draws the plasma either - it is the source a warp
    samples and the colour data a renderer indexes. This is what the
    texture looks like before anything uses it.
    """
    for y in range(SCREEN_H):
        row = (y & 0xff) * 0x100
        out = y * SCREEN_W
        fb[out:out + SCREEN_W] = bytes(
            buf[row + (x & 0xff)] for x in range(SCREEN_W))


def plasma_displace(avg, rnd, mask, bias, shift):
    """ORIGIN 1000:40e3. One octave's random offset, then clamp to a byte.

    The clamp is why the coarse octaves matter so much: with mask 0x3fff
    and bias 0x2000 the first offset is +/-8192 against a 0..255 range,
    so the top octaves saturate to 0 or 0xff and lay down flat
    continents that the finer ones then texture.
    """
    d = ((next(rnd) & mask) - bias) >> shift
    v = avg + d
    if v < 0:
        v = 0
    elif v >= 0xff:
        v = 0xff
    return v


def plasma_generate(mask=0x3ff, bias=0x200, rough=1, rnd=None):
    """ORIGIN 1000:3fae. Diamond-square over a 256x256 TORUS.

    The torus is the part worth stating: offsets are 16-bit into one
    64 KB segment, so stepping down past the last row wraps for free,
    and only the horizontal edge needs the special case at img 0x401a -
    `[si + 0xff00]` is si - 0x100, which is column 0 of the SAME row
    where si + 2*step would have been column 0 of the next one.

    The parameter block comes from Demo_Timeline; see
    effects/script1_landscape.txt for which part uses which.

    A call is exactly 0x10000 draws off the shared stream, so the field
    depends on every draw before it - rand_shared.
    """
    if rnd is None:
        rnd = rand_shared()
    buf = bytearray(0x10000)
    buf[0] = next(rnd) & 0xff
    step_lin, step, shift = 0x8000, 0x80, 1

    while step:
        row_base, row = step_lin, 0
        while True:
            di = (row_base + step) & 0xffff
            col = 0
            while True:
                two, two_lin = (step * 2) & 0xffff, (step_lin * 2) & 0xffff
                si = (di - step - step_lin) & 0xffff
                c0 = buf[si]
                si = (si + two) & 0xffff
                if (0x100 - two) != col:
                    c1 = buf[si]
                    si = (si + two_lin) & 0xffff
                    c2 = buf[si]
                else:
                    c1 = buf[(si - 0x100) & 0xffff]
                    si = (si + two_lin) & 0xffff
                    c2 = buf[(si - 0x100) & 0xffff]
                si = (si - two) & 0xffff
                c3 = buf[si]

                buf[di] = plasma_displace((c0 + c1 + c2 + c3) >> 2,
                                          rnd, mask, bias, shift)
                buf[(di - step_lin) & 0xffff] = plasma_displace(
                    (c0 + c1) >> 1, rnd, mask, bias, shift)
                buf[(di - step) & 0xffff] = plasma_displace(
                    (c3 + c0) >> 1, rnd, mask, bias, shift)

                di = (di + two) & 0xffff
                col = (col + two) & 0xffff
                if col == 0x100 or col == 0:
                    break
            row = (row + (step * 2)) & 0xffff
            row_base = (row_base + (step_lin * 2)) & 0xffff
            if row == 0x100 or row == 0:
                break
        step_lin >>= 1
        shift += rough
        step >>= 1
    return buf


def pal_build_main():
    """ORIGIN 1000:3b65. The demo's main palette, seven bands.

    Six bands of 32 then one of 64. BL steps by 2 across a 32-band, so
    a component spans 0..62 of the DAC's 0..63 and the band never quite
    reaches full - that off-by-one is the code's, not a transcription
    slip.
    """
    pal = bytearray(768)
    i = 0

    def put(r, g, b):
        pal[i * 3:i * 3 + 3] = bytes((r & 0x3f, g & 0x3f, b & 0x3f))

    for n in range(0x20):                       # 0..31  brown / land
        bl = n * 2
        put(bl, bl >> 1, bl >> 2)
        i += 1
    for n in range(0x20):                       # 32..63  blue / water
        bl = n * 2
        put(bl >> 2, bl >> 1, bl)
        i += 1
    for n in range(0x20):                       # 64..95  pale blue to white
        bl = n * 2
        put((bl >> 2) + 0x10, (bl >> 1) + 0x20, 0x3f)
        i += 1
    for n in range(0x20):                       # 96..127  black to red
        put(n * 2, 0, 0)
        i += 1
    for n in range(0x20):                       # 128..159  red to yellow
        put(0x3f, n * 2, 0)
        i += 1
    for n in range(0x20):                       # 160..191  yellow to white
        put(0x3f, 0x3f, n * 2)
        i += 1
    for n in range(0x40):                       # 192..255  greyscale
        put(n, n, n)
        i += 1
    return pal


def pal_build_grey_low(pal):
    """ORIGIN 1000:3e79. Overwrites indices 0..31 with grey.

    Builders COMPOSE: this one rewrites a single band of whatever
    pal_build_main left, which is why the palette has to be modelled as
    a buffer rather than as one function's return value.
    """
    for n in range(0x20):
        v = n * 2
        pal[n * 3:n * 3 + 3] = bytes((v, v, v))
    return pal


def pal_fade(pal, level):
    """ORIGIN 1000:3b17. A signed brightness offset, saturating.

    level is g_FadeLevel; the hardware sees (level >> 8) added to every
    component. Valid range is -0x4000..+0x4000, clamped by the two
    tests at img 0x3b1c and 0x3b27, so the offset spans -64..+64 and
    the ends are full black and full white.
    """
    d = level >> 8 if level >= 0 else -((-level) >> 8)
    out = bytearray(768)
    for n in range(768):
        v = pal[n] + d
        out[n] = 0 if v < 0 else (0x3f if v > 0x3f else v)
    return out


def pal_to_rgb(pal):
    """DAC 6-bit -> 8-bit for a modern display. OURS, not the demo's."""
    return [((pal[n * 3] << 2) | (pal[n * 3] >> 4),
             (pal[n * 3 + 1] << 2) | (pal[n * 3 + 1] >> 4),
             (pal[n * 3 + 2] << 2) | (pal[n * 3 + 2] >> 4))
            for n in range(256)]


IMG_COUNTDOWN_MASK = 0x895d        # g_CountdownMask, 320x64 1-bit
MASK_STRIDE = 40                   # 320 bits; see effects/countdown.txt


def countdown_mask_bit(blob, window, col, row):
    """One bit of g_CountdownMask. ORIGIN 1000:3897-0x389f.

    SI is pre-incremented - CX starts at 0 so img 0x389e fires before
    the first BT - so the first byte read is the base plus one.
    """
    base = IMG_COUNTDOWN_MASK + window * 8 + 1
    return blob[base + row * MASK_STRIDE + (col >> 3)] >> (col & 7) & 1


def countdown_spawn(blob, window):
    """ORIGIN 1000:3887-0x38e5. One particle per set bit of one window.

    ax and dx run -512..+496 by 0x10, so vx and vy are ax>>4 and dx>>4
    and the glyph explodes outward from its centre.
    """
    parts = []
    dx = -0x200
    for row in range(64):
        ax = -0x200
        for col in range(64):
            if countdown_mask_bit(blob, window, col, row):
                parts.append([0xf8, ax + 0xa00, dx + 0x640,
                              ax >> 4, dx >> 4, 0])
            ax += 0x10
        dx += 0x10
    return parts


def countdown_step(fb, parts, bg, heat=0xff, field=None,
                   ofs_x=0, ofs_y=0):
    """ORIGIN 1000:3925-0x39b1. One frame of the particle system.

    THE PARTICLES ARE WARPED. img 0x3941 and 0x395b sample the field
    at segment 0x2a79 - the plasma, which img 0x00a4 generates before
    the countdown - and displace the particle by it, with g_WarpOfsX
    and g_WarpOfsY added CUMULATIVELY. The mode 1 handler advances
    both every tick, so the dots WAVE as well as expand, exactly as
    Flag_Render's panel does.

    Pass field to get that; without it the particles fly straight,
    which is how this was first transcribed and is wrong.
    """
    for p in parts:
        if p[0] == 0:
            continue
        if p[5]:
            fb[p[5]] = bg[p[5]]
        if field is not None:                           # img 3941
            sx, sy = countdown_warp(field, p[1], p[2], ofs_x, ofs_y)
            sx = to_s16(sx)
            sy = to_s16(sy)
        else:
            sx = ((p[1] & 0xffff) >> 4) - 0x10
            sy = ((p[2] & 0xfff0) << 4) - 0x1000
        if False:
            sx -= 0x10
        if not 0 <= sx < 0x140:
            p[0] = 0
            continue
        if not 0 <= sy < 0xc800:
            p[0] = 0
            continue
        off = sx + sy + (sy >> 2)
        if off >= len(fb):
            p[0] = 0
            continue
        v = fb[off]
        if v >= 0xc0 or v < 0x60:
            v = 0x60
        fb[off] = min(0xff, v + ((heat >> 1) >> 2))
        p[5] = off
        p[0] -= 4
        if p[0] <= 0:
            p[0] = 0
            continue
        p[1] += p[3]
        p[2] += p[4]


def countdown_blob():
    """The dump, for the mask. Shares load_sin_table's refusal."""
    with open(DUMP, 'rb') as f:
        return f.read()


def firetext_fill(fb, mask, texture, tex_row_base, threshold, rows=200):
    """ORIGIN 1000:23a3-0x23c6. Fill a mask with fire-coloured plasma.

    Per pixel, only where the mask byte is non-zero:

        v = texture[bx + di] - threshold
        if v >= 0:  fb[di] = (v >> 2) + 0x60

    So the output lands in 0x60..0x9f, the lower two thirds of the fire
    band, and `threshold` is the flame's height control: raising it
    rejects more of the texture and the fill retreats.

    bx steps by -0x40 per row, so the texture is sampled with a
    negative row stride and the flame crawls upward as rows advance.
    """
    bx = tex_row_base
    di = 0
    for _ in range(rows):
        for _ in range(SCREEN_W):
            if di >= len(fb):
                return
            if mask[di]:
                v = texture[(bx + di) & 0xffff] - threshold
                if v >= 0:
                    fb[di] = ((v >> 2) + 0x60) & 0xff
            di += 1
        bx = (bx - 0x40) & 0xffff


def countdown_mask_as_bitmap(blob, window=0, x=128, y=68):
    """OURS, a stand-in. The demo's own text mask producer is unlocated.

    firetext_fill needs a 320x200 mask and nothing found so far builds
    one, so this borrows a countdown glyph - real data out of this
    image rather than a shape invented to look right. The fill maths
    and the palette are the demo's; the shape is not.
    """
    mask = bytearray(SCREEN_W * SCREEN_H)
    for row in range(64):
        for col in range(64):
            if countdown_mask_bit(blob, window, col, row):
                px, py = x + col, y + row
                if 0 <= px < SCREEN_W and 0 <= py < SCREEN_H:
                    mask[py * SCREEN_W + px] = 1
    return mask





def voxel_slice_table():
    """ORIGIN Demo_Entry img 0x005c-0x0085. Distances and rows per slice.

    The step accelerates: +1 while the distance is under 0x32, +2 under
    0x5a, +3 after, stopping at 0x96. So near slices are one screen row
    each and far ones three, which is where a voxel renderer's time
    goes if it does not do this.

    Returns (distances, rows) and the length of either is g_SliceCount.
    """
    dists, rows, cx = [], [], 0
    while cx < 0x96:
        n = 1
        cx += 1
        if cx >= 0x32:
            n += 1
            cx += 1
        if cx >= 0x5a:
            n += 1
            cx += 1
        dists.append(cx)
        rows.append(n)
    return dists, rows


SLICE_DIST, SLICE_ROWS = voxel_slice_table()


def voxel_render(fb, heightmap, colourmap, water, cam, slices=None):
    """ORIGIN 1000:3620. See effects/voxel.txt for the whole algorithm.

    cam is (world_x, world_y, angle, scale, height_scale, y_offset,
    water_level, water_scroll) - Voxel_LatchCamera's six plus the two
    water parameters.

    THE SKY IS NOT DRAWN. g_SkyGradient at cs:0xb5dc has no located
    producer, so the sky stays index 0. Everything below the horizon is
    transcribed. [open]
    """
    wx, wy, angle, scale, hscale, yofs, water_level, water_scroll = cam
    dists = SLICE_DIST if slices is None else slices

    # g_ColumnTop, img 0x3623: 160 entries seeded 0xf8c0 + 2i, which is
    # row 199 - the bottom - at each two-pixel column.
    top = [0xf8c0 + 2 * i for i in range(160)]

    sin_a, cos_a = sin_at(angle), cos_at(angle)
    # (sin - cos) * 1.25 and (cos + sin) * 1.25; the 1.25 is x + (x>>2).
    dx0 = (sin_a - cos_a)
    dx0 += dx0 >> 2
    dy0 = (cos_a + sin_a)
    dy0 += dy0 >> 2

    for dist in dists:
        horizon = max(0, (dist >> 1) - 0x2c)
        base = ((scale // dist) + 0x48) * 320
        rx = dx0 * dist + wx
        ry = dy0 * dist + wy
        sx = (cos_a * dist) >> 6
        sy = (sin_a * dist) >> 6

        # g_HeightToY, img 0x371c: 256 dwords descending by
        # g_VoxHeightScale / dist, rebuilt for every slice.
        step = hscale // dist
        shade = horizon & 0xff
        edi = base

        for col in range(160):
            si = (((rx >> 12) & 0xff) << 8) | ((ry >> 12) & 0xff)
            h = heightmap[si]
            c = (colourmap[si] - shade) & 0xff
            if c > 0x1f:
                c = 0
            if water_level:
                si2 = (si + water_scroll) & 0x3fff
                wh = water[si2]
                bh = (wh + water_level) & 0xff
                if bh > h:
                    h = bh
                    c = (c + (wh >> 2) + 0x24) & 0xff
                    if c >= 0x3f:
                        c = 0x3e
            y = -h * step + edi + yofs
            if y < 0x140:
                y = 0x140
            si3 = top[col]
            lo, hi = c, (c + 1) & 0xff
            while si3 >= y:
                fb[si3] = lo
                fb[si3 + 1] = hi
                lo, hi = hi, lo
                si3 -= 0x140
            top[col] = si3
            rx += sx
            ry -= sy
            edi += 2


def vec_rotate3(x, y, z, pitch, yaw, roll):
    """ORIGIN 1000:4a22. Three Euler angles. RETURNS THE RESULT x64.

    The shift pattern is not symmetric and that is deliberate: blocks 1
    and 2 shift results that feed later blocks, which would otherwise
    overflow 32 bits. Counting shifts per output shows all three leave
    one 6-bit shift short of a unit rotation, so callers do >> 6.
    """
    s, c = sin_at(roll), cos_at(roll)                   # x,y
    t0, t1 = (x * s) >> 6, (y * s) >> 6
    x, y = (((x * c) >> 6) - t1) >> 6, (((y * c) >> 6) + t0) >> 6

    s, c = sin_at(pitch), cos_at(pitch)                 # y,z
    t0, t1 = (y * s) >> 6, (z * s) >> 6
    y, z = ((y * c) >> 6) - t1, (((z * c) >> 6) + t0) >> 6

    s, c = sin_at(yaw), cos_at(yaw)                     # x,z
    t0, t1 = (x * s) >> 6, (z * s) >> 6
    return ((x * c) >> 6) - t1, y, ((z * c) >> 6) + t0


def parse_model(blob, base):
    """The model format, verified by its own extent. See formats/mesh.txt.

    A face's +0 byte is ITS OWN vertex count, 3 or 4, and a triangle
    stores i3 == i2 - which is why maybe_Mesh_Render can average four
    indices unconditionally. Indices are 1-BASED: the code does
    `shl bx,5` then reads [bx-0x20].
    """
    n = struct.unpack_from('<H', blob, base)[0]
    verts = [struct.unpack_from('<3b', blob, base + 2 + i * 3)
             for i in range(n)]
    fbase = base + 2 + n * 3
    faces = []
    for f in range(blob[fbase]):
        o = fbase + 1 + f * 10
        cnt = blob[o]
        attr = blob[o + 1]
        idx = struct.unpack_from('<4H', blob, o + 2)
        faces.append((cnt, attr, [i - 1 for i in idx[:cnt]]))
    return verts, faces


def mesh_transform(verts, angles, trans, zscale=0x100, sky_bias=0):
    """ORIGIN 1000:4960. Rotate, translate, near-clamp, project.

    Focal length 128, screen centre x 0xa0, and screen y is biased by
    0x48 - THE SAME HORIZON CONSTANT Voxel_Render uses - so a model and
    the landscape share a horizon.
    """
    pitch, yaw, roll = angles
    tx, ty, tz = trans
    out = []
    for vx, vy, vz in verts:
        x, y, z = vec_rotate3(vx << 3, vy << 3, (vz * zscale) >> 5,
                              pitch, yaw, roll)
        # NO >> 6 HERE. Mesh_SetupTransform shifts Vec_Rotate3's result
        # and this does not, so a vertex stays at x64 while it is added
        # to a translation that was brought down to the same scale from
        # much larger world values. Adding the shift collapses every
        # model to a few pixels, which is how the omission was found.
        x += tx
        y += ty
        z += tz
        if z < 0x2328:
            z = 0x2328
        d = z >> 3
        out.append((((x << 4) // d) + 0xa0,
                    ((y << 4) // d) + 0x48 + sky_bias, z))
    return out


def draw_line(fb, x0, y0, x1, y1, colour):
    """OURS. The demo FILLS its faces; the rasteriser is unread, so a
    wireframe is a stand-in for showing the geometry, not the effect.
    """
    dx, dy = abs(x1 - x0), abs(y1 - y0)
    sx = 1 if x1 > x0 else -1
    sy = 1 if y1 > y0 else -1
    err = dx - dy
    while True:
        if 0 <= x0 < SCREEN_W and 0 <= y0 < SCREEN_H:
            fb[y0 * SCREEN_W + x0] = colour
        if x0 == x1 and y0 == y1:
            return
        e2 = err * 2
        if e2 > -dy:
            err -= dy
            x0 += sx
        if e2 < dx:
            err += dx
            y0 += sy


# The four g_MeshFaces values Demo_Entry and Demo_Timeline store, at
# img 0x0466, 0x04ea, 0x51a3 and 0x52c9. Each model's faces end exactly
# where the next one's header begins, which is what verifies the format.
# Every model the image holds, found by scanning the stores to
# g_MeshFaces at cs:0x98ad. Names from what each part draws them as -
# see effects/timeline.txt section 3 and effects/battle.txt.
MODEL_BASES = (0x572f, 0x585f, 0x94f4, 0x5c47, 0x5b81, 0x5a8a, 0x5be7)
MODEL_NAMES = {
    0x572f: 'craft B, the battle',
    0x585f: 'craft A, recurs',
    0x94f4: 'the car',
    0x5c47: 'THE END letters',
    0x5b81: 'laser bolt, music < 0x1c',
    0x5a8a: 'the later weapon',
    0x5be7: 'THE FLAG - pole plus banner',
}


def face_normal(pts3, idx, centre):
    """ORIGIN 1000:4481-0x45da. Cross product, then the odd normalise.

    THE SQUARE ROOT IS A DESCENDING SEARCH FROM 0x4000 and exits on its
    first comparison once the length exceeds that, so a long normal is
    divided by 0x4000 rather than by its length. Transcribed, because
    the shading depends on it.
    """
    (x0, y0, z0), (x1, y1, z1) = pts3[idx[0]], pts3[idx[1]]
    cx, cy, cz = centre
    ax, ay, az = x0 - cx, y0 - cy, z0 - cz
    bx, by, bz = x1 - cx, y1 - cy, z1 - cz
    # img 44c6, 450e and 4556 store each component as a WORD before the
    # length is taken, so both stages truncate.
    nx = to_s16((ay * bz - az * by) >> 16)
    ny = to_s16((az * bx - ax * bz) >> 16)
    nz = to_s16((ax * by - ay * bx) >> 16)

    c = isqrt_as_demo(nx * nx + ny * ny + nz * nz)
    # IDIV TRUNCATES TOWARD ZERO and Python's // floors, which is one
    # low on every negative component - img 45b1.
    return (to_s16(int((nx << 12) / c)), to_s16(int((ny << 12) / c)),
            to_s16(int((nz << 12) / c)))


def mesh_normals(verts, faces, zscale=0x100):
    """ORIGIN 1000:43d4 Mesh_FaceNormals, and THE NORMALS ARE COMPUTED
    ONCE PER MODEL, NOT PER FRAME.

    It puts the model in a FIXED REFERENCE POSE - zero angles and a
    translation of (0, 0, 0x1e000), img 43d4 to 4400 - takes each
    face's cross product there and stores the result in the vertex
    segment at 0x8000. Mesh_Draw reads it and rotates it by the
    object's angles once.

    TAKING THE NORMAL FROM THE ROTATED VERTICES AND THEN ROTATING IT IS
    A DOUBLE ROTATION. The shading still moves with the object so it
    reads as lighting; it simply points somewhere else and swings to
    the wrong side as the craft turns.
    """
    ref = []
    for vx, vy, vz in verts:
        x, y, z = vec_rotate3(vx << 3, vy << 3, (vz * zscale) >> 5, 0, 0, 0)
        z += 0x1e000
        if z < 0x2328:
            z = 0x2328
        ref.append((x, y, z))
    out = []
    for cnt, _attr, idx in faces:
        ii = list(idx) + [idx[-1]] * (4 - cnt)
        out.append(face_normal(ref, ii, (
            sum(ref[i][0] for i in ii) >> 2,
            sum(ref[i][1] for i in ii) >> 2,
            sum(ref[i][2] for i in ii) >> 2)))
    return out


def isqrt_as_demo(sq):
    """What img 0x457f-0x45a2's descending search returns, computed.

    The demo counts a candidate DOWN from 0x4000 keeping whichever
    square is nearest sq, and breaks when the error grows. That is the
    nearest integer root, with two edges worth having exactly:

      sq = 0            -> 1, because the loop runs to c = 0 and then
                           increments, which is also what stops a
                           divide by zero
      sq >= 0x4000^2    -> 0x4000, because the search exits on its
                           first comparison and never descends

    Checked against the loop over 0..5000, the boundaries, and 400
    random values below 0x4000^2: one difference, sq = 0, handled here.
    Transcribing the loop instead costs about 12000 Python iterations
    per face, which is the whole frame.
    """
    if sq == 0:
        return 1
    if sq >= 0x4000 * 0x4000:
        return 0x4000
    r = math.isqrt(sq)
    return r if abs(r * r - sq) <= abs((r + 1) ** 2 - sq) else r + 1


def new_span_buf():
    """g_SpanBuf plus the g_SpanMinY/g_SpanMaxY pair that bounds it."""
    return ([[0x140, -1, 0, 0] for _ in range(200)], [200, -1])


def poly_fill(fb, verts, shades, band_base, buf):
    """ORIGIN 1000:4b2c. A four-vertex convex polygon, Gouraud shaded.

    buf comes from new_span_buf and is REUSED: only the rows the last
    polygon touched are reset, bounded by its own min and max, exactly
    as img 0x4b4e does. Allocating a fresh 200-row buffer per face
    instead costs about three times the frame.
    """
    span, bounds = buf
    for row in range(max(0, bounds[0]), bounds[1] + 1):
        sp = span[row]
        sp[0], sp[1] = 0x140, -1
    bounds[0], bounds[1] = 200, -1
    vs = list(verts) + [verts[0]]
    ss = list(shades) + [shades[0]]
    lo_y, hi_y = 200, -1

    for e in range(4):
        (x0, y0), (x1, y1) = vs[e], vs[e + 1]
        s0, s1 = ss[e], ss[e + 1]
        if y0 > y1:
            x0, y0, x1, y1, s0, s1 = x1, y1, x0, y0, s1, s0
        if y0 >= 200 or y1 <= 0 or y1 == y0:
            continue
        dy = y1 - y0
        gx = ((x1 - x0) << 12) // dy
        gs = ((s1 - s0) << 16) // dy
        ax = (x0 << 12) + (gx >> 1)
        asd = (s0 << 16) + (gs >> 1)
        for row in range(y0, min(y1, 200)):
            if row >= 0:
                px = (ax - 0x800) >> 12
                sp = span[row]
                if sp[0] > px:
                    sp[0], sp[2] = px, asd
                if sp[1] < px:
                    sp[1], sp[3] = px, asd
                if row < bounds[0]:
                    bounds[0] = row
                if row > bounds[1]:
                    bounds[1] = row
                lo_y = min(lo_y, row)
                hi_y = max(hi_y, row)
            ax += gx
            asd += gs

    for row in range(max(0, lo_y), hi_y + 1):
        left, right, sl, sr = span[row]
        if left > 0x13f or right < 0:
            continue
        left = max(0, left)
        right = min(0x13f, right)
        if right < left:
            continue
        n = right - left + 1
        step = (sr - sl) // n
        base = row * SCREEN_W
        if step == 0:
            # Flat shading is the common case here, and a slice write is
            # the same pixels for a fraction of the time.
            v = min(0x3f, max(0, sl >> 16)) + band_base
            fb[base + left:base + right + 1] = bytes([v]) * n
            continue
        acc = sl
        for x in range(left, right + 1):
            fb[base + x] = min(0x3f, max(0, acc >> 16)) + band_base
            acc += step


# Static initial values from the variable block: img 0x9883, 0x9885,
# 0x990f, 0x9370.
LIGHT_ANGLES = (0x100, 0xff00, 0x0000)
SHADE_SCALE = 0x190


def mesh_draw(fb, verts, faces, angles, trans, cam_yaw=0,
              zscale=0x100, buf=None, light=LIGHT_ANGLES,
              shade_scale=SHADE_SCALE, light_mode='demo'):
    """ORIGIN 1000:465c. The whole object renderer, transcribed.

    The cull is the demo's 2D winding test, the depth key its summed z
    with the 4/3 triangle scale, the shading its per-face Lambert
    accumulated and averaged per vertex, the sort its selection sort,
    and the band each face's own attribute byte.

    light_mode 'demo' is faithful: the stored model-space normal is
    rotated into world space by g_ObjPitch/Yaw/Roll at img 0x47b5 and
    then into light space by g_LightPitch/Yaw/Roll. 'view' skips the
    first rotation, which pins the light near the viewer - OURS, kept
    only so the two can be compared. The two rotations do use
    different yaws: the vertices take g_ObjYaw + g_CamAngleNext at img
    0x4668 and the normal g_ObjYaw alone at img 0x47a1, which is
    camera space. So the light sweeps across the model at roughly twice
    its rotation rate and a face can go dark facing the viewer.

    light_mode 'view' is OURS: it skips that second rotation, leaving
    the normal in camera space so the light stays put. It looks better
    and it is not what the demo does - CLAUDE.md rule 12. Kept only so
    the two can be compared.
    """
    if buf is None:
        buf = new_span_buf()
    pitch, yaw, roll = angles
    tx, ty, tz = trans

    # img 4664: an object's yaw is RELATIVE TO THE CAMERA's heading.
    latch = (pitch, (yaw + cam_yaw) & 0xffff, roll)
    raw, screen = [], []
    for vx, vy, vz in verts:
        x, y, z = vec_rotate3(vx << 3, vy << 3, (vz * zscale) >> 5,
                              *latch)
        x += tx
        y += ty
        z += tz
        if z < 0x2328:
            z = 0x2328
        raw.append((x, y, z))
        d = z >> 3
        screen.append((to_s16(((x << 4) // d) + 0xa0),
                       to_s16(((y << 4) // d) + 0x48)))

    # img 43d4 - from the reference pose, not from `raw`. The demo
    # does this once per model; recomputing it here is only slower,
    # never different.
    fnorm = mesh_normals(verts, faces, zscale)

    accum = [0] * len(verts)
    touch = [0] * len(verts)
    keys = []

    for cnt, attr, idx in faces:
        ii = list(idx) + [idx[-1]] * (4 - cnt)
        # img 472a: the 2D cross product of two screen-space edges from
        # vertex 2. Negative is back-facing.
        (x0, y0), (x1, y1), (x2, y2) = (screen[ii[0]], screen[ii[1]],
                                        screen[ii[2]])
        ax, ay, bx, by = (to_s16(x0 - x2), to_s16(y0 - y2),
                          to_s16(x1 - x2), to_s16(y1 - y2))
        # 16-BIT AT EVERY STEP, and THE CULL ONLY ZEROES THE KEY: a
        # back-facing face still lights its vertices - mesh.txt 5.
        if to_s16(to_s16(ax * by) - to_s16(ay * bx)) < 0:
            keys.append(0)
        else:
            # img 474c: the summed z, a triangle scaled by 4/3 so it
            # compares against a quad.
            z3 = raw[ii[0]][2] + raw[ii[1]][2] + raw[ii[2]][2]
            keys.append((z3 * 4) // 3 if cnt == 3 else z3 + raw[ii[3]][2])

        nx, ny, nz = fnorm[len(keys) - 1]
        if light_mode == 'demo':
            wx, wy, wz = vec_rotate3(nx << 4, ny << 4, nz << 4,
                                     pitch, yaw, roll)
            wx, wy, wz = wx >> 6, wy >> 6, wz >> 6
        else:
            wx, wy, wz = nx << 4, ny << 4, nz << 4
        lx, _, _ = vec_rotate3(wx, wy, wz, *light)
        i = (lx >> 0xc) + 0x12c
        if i < 0:
            i = 0
        i = (i * i) >> 0xe
        for k in range(cnt):
            accum[ii[k]] += i
            touch[ii[k]] += 1

    # img 4848: the averaged vertex intensity.
    shade = [((accum[v] // touch[v]) * shade_scale) >> 9 if touch[v] else 0
             for v in range(len(verts))]

    # img 4894: a selection sort, largest key first, until none is left.
    drawn = 0
    order = list(range(len(faces)))
    while True:
        best = -1
        for f in order:
            if keys[f] > 0 and (best < 0 or keys[f] > keys[best]):
                best = f
        if best < 0:
            break
        keys[best] = 0
        cnt, attr, idx = faces[best]
        ii = list(idx) + [idx[-1]] * (4 - cnt)
        poly_fill(fb, [screen[i] for i in ii], [shade[i] for i in ii],
                  attr, buf)
        drawn += 1
    return drawn


def to_s16(v):
    """IMUL BX,DX keeps 16 bits, so the winding test wraps. The demo's
    coordinates are small enough that it does not, but the test has to
    be the 16-bit one to be the same test."""
    v &= 0xffff
    return v - 0x10000 if v & 0x8000 else v


def planet_build_map():
    """ORIGIN 1000:42d8 Planet_BuildMap. The 128x128 screen-to-texture
    table for the mapped sphere, with a sentinel for "off the ball".

    A SPHERE OF RADIUS 64, CLIPPED AT z = 32. The clip comes out of
    img 0x42ef as g_SqrTable[63] - g_SqrTable[31] = 64*64 - 32*32 =
    3072, which the root search rounds to 3025, so the disc is radius
    55 inside the 128-wide box. The visible cap subtends 60 degrees.

    The parameterisation is lateral/z, not an arcsine - the tangent
    rather than the true spherical form. It still compresses toward the
    limb, 17 texels per 16 pixels at the centre against 26 at the rim,
    which is what makes it read as a ball.

    Both coordinates are negated, so THE TEXTURE IS MAPPED THROUGH THE
    CENTRE - mirrored in both axes.
    """
    sqr = [(k + 1) ** 2 for k in range(0x5a)]

    def root(ax):
        """img 0x43b5. A descending-error scan over the square table.
        Returns the BYTE OFFSET ONE PAST the nearest square, so the
        answer is si // 2 - and the +2 is cancelled at img 0x4334 by
        indexing from 0xaf7a, the table less one entry. Not an
        off-by-one."""
        dx, si = 0x7fff, 0
        for _ in range(0x5a):
            bx = abs(sqr[si // 2] - ax)
            if bx > dx:
                break
            dx, si = bx, si + 2
        return si

    r_sq = 0x1000                                       # cs:0xaffa, 64*64
    cut = sqr[root(r_sq - 0x400) // 2 - 1]              # img 42ef, 42fb, 4334
    table = []
    for cl in range(0x80, 0, -1):
        for ch in range(0x80, 0, -1):
            d = sqr[abs(cl - 64)] + sqr[abs(ch - 64)]   # (|c-64|+1)^2 each
            if cut <= d:
                table.append(SPHERE_OUTSIDE)
                continue
            z = root(r_sq - d) // 2
            u = (((0x40 - ch) * 0x20 + z // 2) * 2) // z + 0x40
            v = (((0x40 - cl) * 0x20 + z // 2) * 2) // z + 0x40
            table.append(((u & 0xff) | ((v & 0xff) << 8)) & 0xffff)
    return table


SPHERE_OUTSIDE = 0xf380                                 # img 0x43a1, 0x2972


def planet_render(fb, table, texture, pan=0x8000, approach=0):
    """ORIGIN 1000:2925 Planet_Render. A starfield, then the mapped
    sphere, then a 3D object - the demo's whole space scene in one
    runner, and the only renderer that six parts share.

    THE BALL IS OFTEN ABSENT AND THAT IS THE POINT. approach is
    g_PlanetApproach, cs:0x9464, and every column whose running value
    reaches 0x38000 is skipped. Four of the six parts set it to
    0xfa000 first, which skips every column, leaving stars and the
    object alone. See effects/planet.txt section 4.

    The column cutoff and the screen origin move together: the count
    drawn is (0x38000 - approach) >> 10 and the origin x is the same
    number, so the ball's right edge sits at x = count - 11 and its
    left is clipped by x = 1. It slides in from the left and lands
    centred at x = 160 when approach is 0.

    texture is the 256x256 field at SEGMENT 0x2a79 - a segment value,
    not an image offset - the same diamond-square data the landscape
    uses for its heightmap, which is why one planet carries the
    terrain colours.
    """
    di = 0
    origin = 0x2de0 - (approach >> 10)
    for _ in range(0x80):                               # img 295e, rows
        bx, eax = origin, approach
        for _ in range(0x80):                           # img 2967, columns
            if eax >= 0x38000:                          # img 2967
                di += 2
                bx -= 1
                eax += 0x400
                continue
            si = table[di // 2]
            if si != SPHERE_OUTSIDE:
                dh = texture[(pan + si) & 0xffff]
                dh = ((dh >> 2) - 0x20) & 0x3f          # img 297b-2981
                if 0 <= bx < 320 * 200:
                    fb[bx] = dh
            di += 2
            bx -= 1
            eax += 0x400
        origin += 0x140                                 # img 2993


def glow_render(fb, field, src, dst, dest_ofs):
    """ORIGIN 1000:2736 Glow_Render. The battle's energy glow: a
    128x128 box blur with a decay, double-buffered, drawn into the
    96..191 fire ramp.

    src and dst are g_GlowSrc and g_GlowDst, byte offsets into the
    same 0x4a79 segment; the caller swaps them afterwards exactly as
    img 0x2791 does with an XCHG. dest_ofs is BX, a flat offset into
    the 320-wide screen - 0x2d60 is x=96 y=36, 0x9c is x=156 y=0.

    THE DECAY IS WHAT MAKES IT DIE. A box blur alone conserves its
    total and would settle into a steady glow; the DEC at img 0x2771
    takes one off every non-zero cell per frame.
    """
    bx = dest_ofs
    si, di = src, dst
    for _ in range(0x80):
        row_bx = bx
        for _ in range(0x80):
            ax = (field[(si - 1) & 0xffff] + field[(si - 0x80) & 0xffff]
                  + field[(si + 1) & 0xffff] + field[(si + 0x80) & 0xffff])
            ax >>= 2                                    # img 2768
            dh = ax & 0xff
            if ax:
                dh = (dh - 1) & 0xff                    # img 2771, the decay
                al = (ax >> 1) + 0x60                   # img 2773, the band
                if al >= 0xc0:
                    al = 0xbf                           # img 277d
                if 0 <= row_bx < 320 * 200:
                    fb[row_bx] = al
            field[di & 0xffff] = dh
            si += 1
            di += 1
            row_bx += 1
        bx += 0x140                                     # img 2789 (0x80+0xc0)


def battle_pursue(a_pos, a_ang, b_pos, cam):
    """ORIGIN 1000:1ce8 Battle_Pursue. Craft B closes one sixteenth of
    the gap to a point on craft A's heading, per tick.

    Returns (b_pos, cam, forward_shifted) - and that last value is not
    cosmetic: the mode 0xb handler shifts it back left by 6 instead of
    recomputing the rotation, so the >> 6 has to happen here.
    """
    ax, ay, az = vec_rotate3(0, 0, -0x400, a_ang[0], a_ang[1], a_ang[2])
    fwd = (ax, ay, az)
    # img 1d16: negate, then A's z joins the z component.
    tx, ty, tz = -ax, -ay, -az + a_pos[2]
    b = [to_s32(b_pos[i] + (((t - b_pos[i]) >> 4)))
         for i, t in enumerate((tx, ty, tz))]
    cam = tuple(to_s16(cam[i] + ((fwd[i] >> 6) & 0xffff)) for i in range(3))
    return tuple(b), cam, tuple(f >> 6 for f in fwd)


def to_s32(v):
    v &= 0xffffffff
    return v - 0x100000000 if v & 0x80000000 else v


def battle_mode9(st):
    """ORIGIN img 0x1264. The approach: A flies in and CLAMPS."""
    st['b_pos'], st['cam'], _ = battle_pursue(st['a_pos'], st['a_ang'],
                                              st['b_pos'], st['cam'])
    if st['a_pos'][2] < 0x1e000:                        # img 1267, a clamp
        st['a_pos'] = (st['a_pos'][0], st['a_pos'][1], st['a_pos'][2] + 0x60)
    p, y, r = st['a_ang']
    st['a_ang'] = (to_s16(p + 3), to_s16(y + 2), to_s16(r + 4))
    bp, by, br = st['b_ang']
    st['b_ang'] = (st['a_ang'][0], st['a_ang'][1], to_s16(br - 2))


def battle_mode_a(st):
    """ORIGIN img 0x12a5. The pass: B on a Lissajous, banking by
    reading its own position."""
    bx = 2 * sin_at(st['c0'])
    by = 2 * sin_at(st['c1'])
    st['b_pos'] = (to_s32(bx), to_s32(by), st['b_pos'][2])
    st['b_ang'] = (to_s16(by >> 7), to_s16((bx >> 7) + 0x400),
                   to_s16(st['b_ang'][2] - 2))
    st['c0'] = (st['c0'] + 4) & 0xffff                  # img 12e6, 4 and 8
    st['c1'] = (st['c1'] + 8) & 0xffff                  # - a 1:2 ratio
    p, y, r = st['a_ang']
    st['a_ang'] = (p, y, to_s16(r + 3))
    if to_s16(st['a_ang'][2]) >= 0x708:                 # img 1305
        st['a_ang'] = (to_s16(st['a_ang'][0] - 8), y, st['a_ang'][2])
        dx, dy, dz = vec_rotate3(0, 0, -8, st['a_ang'][0], st['a_ang'][1], 0)
        st['a_pos'] = (to_s32(st['a_pos'][0] + dx),
                       to_s32(st['a_pos'][1] + dy),
                       to_s32(st['a_pos'][2] + dz - 0x200))


def battle_mode_b(st):
    """ORIGIN img 0x1351. Locked formation - B one forward vector
    ahead of A, which is why Battle_Run draws B first in this mode."""
    st['b_pos'], st['cam'], fwd = battle_pursue(
        st['a_pos'], st['a_ang'], st['b_pos'], st['cam'])
    f = [to_s32(v << 6) for v in fwd]                   # img 1354
    st['a_pos'] = (to_s32(-(f[0] >> 2)), to_s32(-(f[1] >> 2)),
                   to_s32(-(f[2] >> 2) + 0x8800))
    st['b_pos'] = tuple(to_s32(f[i] + st['a_pos'][i]) for i in range(3))
    st['a_ang'] = (st['a_ang'][0], st['a_ang'][1], to_s16(st['a_ang'][2] - 4))
    st['b_ang'] = (st['b_ang'][0], st['b_ang'][1], to_s16(st['b_ang'][2] + 2))
    p = to_s16(st['a_ang'][0])
    st['a_ang'] = (to_s16(p + ((to_s16(0xff60) - p) >> 6)),
                   st['a_ang'][1], st['a_ang'][2])


def glow_spawn_step(field, dst, parts):
    """ORIGIN 1000:2320 Glow_SpawnStep. 32 particles stamping 0xff into
    the glow field, one step per tick.

    THE FIELD OFFSET USES THE HIGH BYTES ONLY, img 0x2339: bh is y's
    high byte, bx >>= 1 makes that y * 0x80 - the 128-wide row stride -
    and bl takes x's high byte. So the 8.8 fixed point costs nothing to
    index with.
    """
    for p in parts:
        if not p[4]:                                    # img 2333, lifetime
            continue
        bx = ((p[1] & 0xff00) >> 1) | ((p[0] >> 8) & 0xff)
        field[(bx + dst) & 0xffff] = 0xff               # img 2341
        p[0] = (p[0] + p[2]) & 0xffff                   # img 2344
        p[1] = (p[1] + p[3]) & 0xffff
        p[4] -= 1


def terrain_height_at(heightmap, water_field, x, z, water_level=0x21,
                      water_scroll=0, height_scale=0x1000, bp=0):
    """ORIGIN 1000:3d93 Terrain_HeightAt. How high is the ground at a
    world position, water included.

    THE RETURN IS SCALED, img 0x3de2: (g_VoxHeightScale / 320) * the
    height byte, so with the shipped 0x1000 that is 12 per unit and a
    range of 0..3060. The divide is by the SCREEN WIDTH, which is how
    the scale ends up in the same units the renderer projects with.

    bp = 0 lets the water REPLACE the land height; bp != 0 only sets
    the returned flag, img 0x3dd5. The car passes 0, so it floats on
    the water surface rather than sinking to the bed.

    Returns (height, flag) where flag is 2 when the sample is water.
    """
    si = ((z >> 12) & 0xff) | ((x >> 4) & 0xff00)       # img 3d93-3da7
    al = heightmap[si & 0xffff]                         # img 3db0
    si = (si + water_scroll) & 0x3fff                   # img 3db2
    dl = water_field[si]                                # img 3dc0
    flag = 0
    if water_level:                                     # img 3dcb
        dh = (water_level + dl) & 0xff                  # img 3dcf, BYTE
        if dh > al:
            if bp == 0:
                al = dh
            flag = 2
    return (height_scale // 0x140) * al, flag           # img 3de2-3ded


def car_tick(st, heightmap, water_field, water_level=0x21):
    """ORIGIN img 0x19a0, the mode 0x18 handler. THE CAR.

    Eight steps in the demo's own order. Note that the pitch and roll
    TARGETS are computed after the easing that reads them, so the
    attitude trails the ground by one tick - the demo's behaviour, not
    a transcription slip.

    st carries x, y, z, vy, speed, steer, yaw, pitch, roll,
    pitch_target, roll_target and cam_angle. See effects/car.txt.
    """
    def ground(x, z, bp=0):
        return terrain_height_at(heightmap, water_field, x, z,
                                 water_level, bp=bp)[0]

    # img 19a0: the steering is one sine and nothing else.
    st['yaw'] = to_s16(st['yaw'] + (sin_at(st['steer']) >> 0xb))
    st['steer'] = (st['steer'] + 2) & 0xffff
    # img 19be: ease toward LAST tick's targets.
    st['pitch'] = to_s16(st['pitch']
                         + ((st['pitch_target'] - st['pitch']) >> 4))
    st['roll'] = to_s16(st['roll'] + ((st['roll_target'] - st['roll']) >> 4))
    # img 19e0: accelerate, with no ceiling, and drive along the heading.
    st['speed'] += 1
    st['x'] = to_s32(st['x'] + ((sin_at(st['yaw']) * st['speed']) >> 0xb))
    st['z'] = to_s32(st['z'] - ((cos_at(st['yaw']) * st['speed']) >> 0xb))
    # img 1a1d: gravity, and the penetration becomes upward velocity.
    st['vy'] -= 1
    g = ground(st['x'], st['z'])
    if st['y'] <= g:
        st['vy'] += (g - st['y']) >> 3
        st['y'] = g
    st['y'] = to_s32(st['y'] + (st['vy'] >> 1))
    # img 1a65: pitch from the slope along the body, probes 8 apart.
    dx, dz = sin_at(st['yaw']) << 3, cos_at(st['yaw']) << 3
    d = (ground(st['x'] - dx, st['z'] + dz)
         - ground(st['x'] + dx, st['z'] - dz)) >> 3
    st['pitch_target'] = max(-0x1e0, min(0x1e0, d))
    # img 1ae3: roll from the cross-slope. THE SIN AND COS ARE SWAPPED -
    # that is the 90 degree turn - and the reach is 4, not 8.
    dx, dz = cos_at(st['yaw']) << 2, sin_at(st['yaw']) << 2
    d = (ground(st['x'] - dx, st['z'] - dz)
         - ground(st['x'] + dx, st['z'] + dz)) >> 2
    st['roll_target'] = max(-0x1e0, min(0x1e0, d))
    # img 1b61: the camera circles the car at radius 16.
    st['cam_angle'] = (st['cam_angle'] + 2) & 0xffff
    st['cam'] = (to_s32(st['x'] - (sin_at(st['cam_angle']) << 4)),
                 to_s32(st['z'] - (cos_at(st['cam_angle']) << 4)))


def scroller_tick(st):
    """ORIGIN img 0x1bca, the mode 0x19 handler. What advances the
    credits scroll.

    THE WOBBLE OUTRUNS THE BASE. The base advances a steady 0x40 a
    tick, but the sine term's amplitude is 2 * 0xfff over a period of
    only 0x800 / 8 = 256 ticks, which differentiates to a peak of
    about 201 a tick - so the net velocity goes NEGATIVE and the text
    retreats for roughly 40 per cent of each cycle. Anyone "fixing"
    that has changed the effect; see effects/scroller.txt section 5.

    st carries base, phase, pos, cam_y and cam_z.
    """
    st['base'] = to_s32(st['base'] + 0x40)              # img 1bca
    st['pos'] = to_s32(st['base'] + 2 * sin_at(st['phase']))
    st['phase'] = (st['phase'] + 8) & 0xffff            # img 1bf0
    # img 1bfa and 1c01: WORD stores from a 32-bit value, so they wrap.
    st['cam_y'] = to_s16((st['pos'] >> 2) & 0xffff)
    st['cam_z'] = to_s16((-(st['pos'] >> 2)) & 0xffff)


def theend_tick(st):
    """ORIGIN img 0x1c23, the mode 0x1a handler. The THE END letters.

    TWO INDEPENDENT ROTATIONS: the object turns 8/6/4 and the light
    4/8/0xc, so the highlight sweeps across the letters. This is the
    ONLY handler family in the demo that animates the light - see
    effects/theend.txt section 4 before changing any lighting.
    """
    lp, ly, lr = st['light']
    st['light'] = (to_s16(lp + 4), to_s16(ly + 8), to_s16(lr + 0xc))
    p, y, r = st['ang']
    st['ang'] = (to_s16(p + 8), to_s16(y + 6), to_s16(r + 4))
    st['z'] = to_s32(st['z'] - 0x100)                   # img 1c47


def end_osc_tick(st):
    """ORIGIN img 0x1c8f, the mode 0x1c handler - the last part.

    One word at one fixed offset, with an amplitude that ramps for as
    long as the part runs. 0x3f20 is 320*50 + 160, dead centre of row
    50. WHAT READS IT IS NOT KNOWN; see effects/theend.txt section 6.
    """
    st['phase'] = (st['phase'] + 0x20) & 0xffff
    st['amp'] += 1
    st['value'] = to_s16(((sin_at(st['phase']) * st['amp']) >> 0xb) & 0xffff)
    lp, ly, lr = st['light']
    st['light'] = (to_s16(lp + 4), to_s16(ly + 8), to_s16(lr + 0xc))


FLIGHT_TRACK = 0xcc1a                                   # img 0xcc1a


def flight_tick(st, blob, heightmap, water_field, water_level=0x21):
    """ORIGIN img 0x17e5, the mode 0x16 handler. THE AIRCRAFT.

    It replays a recorded track rather than computing a path: one byte
    of six control bits per TEN ticks out of g_FlightTrack. There is no
    yaw bit because img 0x1841 derives yaw from roll - a banked turn.

    Nothing clamps or decays the stick, so a held bit RAMPS the
    attitude; that is what makes ten samples a second enough. See
    effects/flight.txt.
    """
    st['div'] += 1
    if st['div'] >= 0xa:                                # img 17ef
        st['div'] = 0
        st['ptr'] += 1
    al = blob[FLIGHT_TRACK + st['ptr']]
    if al & 0x01:
        st['pitch'] = to_s16(st['pitch'] + 4)
    if al & 0x02:
        st['pitch'] = to_s16(st['pitch'] - 4)
    if al & 0x04:
        st['roll'] = to_s16(st['roll'] - 4)
    if al & 0x08:
        st['roll'] = to_s16(st['roll'] + 4)
    if al & 0x10:
        st['throttle'] -= 1
    if al & 0x20:
        st['throttle'] += 1
    # img 1841: ROLL INDUCES YAW. The whole reason the track has no
    # yaw bit.
    st['yaw'] = to_s16(st['yaw'] + (st['roll'] >> 6))
    ecx = min(0, st['throttle'])                        # img 1871, clamp
    ax, bx, cx = vec_rotate3(0, 0, ecx, st['pitch'], st['yaw'], st['roll'])
    # img 1880: y is shifted SIX FURTHER than x and z.
    st['x'] = to_s32(st['x'] + (ax >> 1))
    st['y'] = to_s32(st['y'] - (bx >> 7))
    st['z'] = to_s32(st['z'] + (cx >> 1))
    lift = (st['throttle'] >> 1) + 0x10                 # img 189c
    if lift > 0:
        st['y'] = to_s32(st['y'] - lift)
    g, _ = terrain_height_at(heightmap, water_field, st['x'], st['z'],
                             water_level)
    g += 0x10                                           # img 18c4
    if st['y'] < g:                                     # img 18c8
        st['y'] = g
        st['pitch'] = to_s16(st['pitch'] + ((0x30 - st['pitch']) >> 5))
        st['roll'] = to_s16(st['roll'] - (st['roll'] >> 5))
    st['horizon'] = st['y'] & 0xffff                    # img 18f6
    st['cam_angle'] = (st['cam_angle'] - 2) & 0xffff    # img 18fa
    st['cam'] = (to_s32(st['x'] - (sin_at(st['cam_angle']) << 3)),
                 to_s32(st['z'] - (cos_at(st['cam_angle']) << 3)))
    cg, _ = terrain_height_at(heightmap, water_field, st['cam'][0],
                              st['cam'][1], water_level)
    if st['horizon'] < ((cg + 0xc8) & 0xffff):          # img 1953
        st['horizon'] = (cg + 0xc8) & 0xffff


def flight_track_decode(blob, count=422):
    """The recorded track as (flags, label) pairs. Reading, not
    simulating - for effects/flight.txt section 1."""
    FLAG = ((1, 'pitch+'), (2, 'pitch-'), (4, 'roll-'), (8, 'roll+'),
            (0x10, 'thr-'), (0x20, 'thr+'))
    out = []
    for i in range(count):
        v = blob[FLIGHT_TRACK + i]
        out.append((v, '+'.join(n for m, n in FLAG if v & m) or '-'))
    return out


def beams_render(fb, work, zoom, angle):
    """ORIGIN 1000:317a Beams_Render. ROTATING STRIPES OF LIGHT, drawn
    by feedback rather than by compositing.

    NOT A ROTOZOOM, despite opening with the same sin*zoom pair. There
    is no texture and no source image: a 16-bit accumulator steps by
    bp per column and si per row - a plane equation in screen space -
    and img 0x31da tests only its TOP TWO BITS. Those are clear for
    exactly one quarter of the accumulator's range, so the screen is
    cut into parallel stripes with one quarter of each period bright.

        bx = (x - 160) * bp + (y - 100) * si     mod 0x10000
        bx < 0x4000   ->  pixel = (pixel + 0x3f) >> 1   toward 0x3f
        otherwise     ->  pixel -= 4, floored by leaving < 4 alone

    So the bright band converges on 0x3f exponentially and everything
    behind it ramps down by 4 a frame. The state lives in the work
    buffer and the screen gets a copy, which is why the runner at
    1000:260f never calls Vga_Present.

    AH IS THE ROW COUNTER WHILE AL IS THE PIXEL. img 0x31d1 sets
    AH = 0xc8 and the loop body only ever touches AL, so `shr al, 1`
    rather than `shr ax, 1` at img 0x31fe is load-bearing.

    The offset puts bx = 0 at the screen centre, img 0x31b3-0x31cd:
    -(160 * bp + 100 * si), assembled from shifts.
    """
    bp = ((sin_at(angle) * zoom) >> 8) & 0xffff         # img 3193
    si = ((cos_at(angle) * zoom) >> 8) & 0xffff         # img 31a3
    dx = (-(100 * si + 160 * bp)) & 0xffff              # img 31b3-31cd
    di = 0
    for _ in range(0xc8):                               # img 31d1, AH
        dx = (dx + si) & 0xffff                         # img 31d3
        bx = dx
        for _ in range(0x140):
            al = work[di]
            if bx & 0xc000:                             # img 31da
                if al >= 4:                             # img 31e3
                    al -= 4
                    work[di] = al
                    fb[di] = al
            else:
                al = (al + 0x3f) >> 1                   # img 31fc
                work[di] = al
                fb[di] = al
            bx = (bx + bp) & 0xffff                     # img 31ee
            di += 1


def pal_build_rgb_ramps(pal, ph_r, ph_g, ph_b):
    """ORIGIN 1000:3d1e Pal_BuildRgbRamps. The beams' palette: indices
    0 to 63 as three linear ramps whose slopes are cosines.

    Writes 64 entries from g_PaletteBase at cs:0xc7a4 through 0xc864 -
    exactly the range Beams_Render paints.

    THE STORED BYTE IS THE HIGH BYTE of a 16-bit accumulator, img
    0x3d78, so the slope is 8.8 fixed point for free. The + 0x1000
    before the shift is what keeps a slope non-negative: the cosine is
    +/-0xfff, so the sum is 1..0x1fff and the shift lands it in 0..0xff.

    Returns the three advanced phases - 3, 7 and 9 a tick, coprime, so
    the hue combination does not repeat quickly.
    """
    sr = (cos_at(ph_r) + 0x1000) >> 5                   # img 3d2a
    sg = (cos_at(ph_g) + 0x1000) >> 5
    sb = (cos_at(ph_b) + 0x1000) >> 5
    ax = bx = dx = 0
    for i in range(64):                                 # img 3d78
        pal[i * 3] = (ax >> 8) & 0xff
        pal[i * 3 + 1] = (bx >> 8) & 0xff
        pal[i * 3 + 2] = (dx >> 8) & 0xff
        ax = (ax + sr) & 0xffff
        bx = (bx + sg) & 0xffff
        dx = (dx + sb) & 0xffff
    return ((ph_r + 3) & 0xffff, (ph_g + 7) & 0xffff,
            (ph_b + 9) & 0xffff)                        # img 3d60


def sqr_nearest_root_bisect(sq, table):
    """ORIGIN 1000:2e9d. Integer sqrt by BINARY SEARCH over the dword
    square table. Returns a BYTE offset, so the root is si/4 + 1.

    img 0x2eb1 halves the step and then ANDs 0xfffc, which keeps every
    probe dword-aligned as it shrinks. Distinct from Sqr_NearestRoot
    at 1000:43b5, which scans a 90-entry WORD table linearly and
    returns one past the answer.
    """
    bx, si = 0x190, 0x190
    dx = abs(table[si // 4] - sq)
    while True:
        bx = (bx >> 1) & 0xfffc                         # img 2eb1
        if bx == 0:
            return si
        ebp = abs(table[(si + bx) // 4] - sq)
        ecx = abs(table[(si - bx) // 4] - sq)
        if ebp < ecx:
            if ebp < dx:
                dx = ebp
                si += bx
        elif ecx < dx:
            si -= bx
            dx = ecx


def tunnel_build_depth():
    """ORIGIN 1000:2bd4 Tunnel_BuildDepth. The depth table and its /8
    companion, 320x200 bytes each.

    THE TRUNCATION TO A BYTE IS THE EFFECT, not an overflow: 0x4000/cx
    exceeds 255 for every small radius, so the depth WRAPS repeatedly
    near the centre and the texture repeat accelerates inward. That is
    what makes the tunnel look infinitely deep.

    SI from the bisection is a BYTE offset, so the divisor is 4k+1 and
    not k+1 - the constant is effectively 0x1000 per unit radius.
    """
    sqr = [(k + 1) ** 2 for k in range(0xc8)]           # img 2bd4, DWORDS
    depth = bytearray(320 * 200)
    mask = bytearray(320 * 200)
    di = 0
    for row in range(0xc8, 0, -1):
        dy = sqr[abs(row - 0x64)]                       # img 2c2a
        for col in range(0x140, 0, -1):
            r2 = sqr[abs(col - 0xa0)] + dy              # img 2c0e
            si = sqr_nearest_root_bisect(r2, sqr)
            v = 0x4000 // (si + 1)                      # img 2c4b
            depth[di] = v & 0xff                        # img 2c52
            mask[di] = (v >> 3) & 0xff                  # img 2c54
            di += 1
    return depth, mask


def tunnel_render(fb, angle_tab, depth_tab, texture, angle, depth,
                  parity, fade=None):
    """ORIGIN 1000:2b2d Tunnel_Render. The two tables supply the two
    HALVES of one 16-bit texture index, so the angle is the texture's
    x and the depth its y, and both scroll by a byte add.

    IT DRAWS HALF THE PIXELS PER FRAME - img 0x2b77 steps by 2 and the
    parity flips per row and again per frame, so it is a checkerboard
    that inverts each frame. The skipped pixels keep what was there.

    Pass fade (g_TunnelFade) to get the exit variant at img 0x2b8f:
    depth_tab must then be the /8 mask, and any pixel whose mask value
    exceeds fade goes black. The tunnel dissolves from the middle out.
    """
    dl = (angle >> 3) & 0xff                            # img 2b41
    dh = (depth >> 8) & 0xff                            # img 2b49, HIGH byte
    di = 0
    for _ in range(0xc8):
        start = di + (1 if parity & 1 else 0)           # img 2b5f
        p = start
        for _ in range(0xa0):
            if fade is not None and fade < depth_tab[p]:
                fb[p] = 0                               # img 2bba
            else:
                bl = (angle_tab[p] + dl) & 0xff         # img 2b68
                bh = (depth_tab[p] + dh) & 0xff         # img 2b6d
                fb[p] = texture[(bh << 8) | bl]         # img 2b72
            p += 2                                      # img 2b77
        di += 0x140
        parity = (parity + 1) & 0xff                    # img 2b81
    return (parity + 1) & 0xff                          # img 2b89


def tunnel_build_angle_OURS():
    """THE 256-WEDGE ANGLE TABLE, COMPUTED - NOT TRANSCRIBED.

    The demo DRAWS it: Tunnel_BuildAngle at 1000:2c6a fills 256
    triangles from the screen centre through Poly_FillFlat, each
    flat-filled with its own index, into segment 0x6a79. Neither that
    routine nor Poly_FillFlat is transcribed yet, so this computes the
    same quantity directly.

    That makes the table OURS and the name says so. It is the right
    SHAPE - a pixel's value is its angle in 1/256 turns about
    (160, 100) - but the demo's version carries whatever rasterisation
    edges 256 drawn triangles leave, and this does not. See
    effects/tunnel.txt section 5.
    """
    import math
    tab = bytearray(320 * 200)
    for y in range(200):
        for x in range(320):
            a = math.atan2(y - 100, x - 160) / (2.0 * math.pi)
            tab[y * 320 + x] = int(a * 256.0) & 0xff
    return tab


WATER_W, WATER_H = 160, 100                             # img 29de, 29e1


def water_step(buf_a, buf_b, damping=4):
    """ORIGIN 1000:29d4 Water_Step. The discrete 2D wave equation.

        new = (sum of 4 neighbours) / 2 - old,  then damped

    Subtracting the OLD value is the second time derivative, and it is
    why two buffers are needed rather than one. The caller swaps them
    after, as img 0x2a10 does with an XCHG.

    THE GRID IS 160 x 100 SIGNED WORDS. DI steps 2 and the vertical
    neighbours are +/-0x140 BYTES, which is 160 words; the loop count
    0x3e80 is 160 * 100.

    THE DAMPING IS A SHIFT: ax -= ax >> 4 is ax * 15/16.
    """
    w = WATER_W
    # img 0x29de starts at word 160 and img 0x29e1 counts 0x3e80 =
    # 16000 cells, so it processes words 160..16159 - ROWS 1 TO 100 OF
    # A 0..99 GRID. It over-runs a row exactly as Water_Refract does,
    # and the +w neighbour read reaches word 16319, so a caller needs
    # TWO rows of pad. Reproduced rather than tidied.
    for i in range(w, w + w * WATER_H):                 # img 29e9
        ax = (buf_b[i - 1] + buf_b[i + 1]
              + buf_b[i - w] + buf_b[i + w])
        ax >>= 1                                        # img 29f7
        ax -= buf_a[i]                                  # img 29f9
        ax -= ax >> damping                             # img 29fe-2a00
        buf_a[i] = to_s16(ax & 0xffff)                  # img 2a02


def water_refract(fb, source, field):
    """ORIGIN 1000:2a99 Water_Refract. THE FINAL EFFECT: the work
    buffer displaced onto the screen through the height field.

    321 IS 320 + 1, so the displacement runs along the screen DIAGONAL
    - down and right together, proportional to height. Refraction in
    three shifts and three adds, no trigonometry.

    One field cell drives a 2x2 screen block: img 0x2ad1 and 0x2ada
    write words on two rows, img 0x2ae2 steps DI by 2 per cell, and
    img 0x2ae7 adds another 0x140 per row so the screen advances two
    rows for every field row.

    IT READS ONE FIELD ROW PAST THE GRID. SI starts at word 160 - row
    1 - and walks 100 rows without resetting, so it finishes on row
    100 of a grid that has rows 0 to 99, the same over-run Water_Step
    has. In real mode that reads whatever follows the field in the
    segment; callers must allow TWO rows of pad, so the behaviour is
    reproduced rather than crashing.
    """
    di = 0
    si = WATER_W                                        # img 2aaa, word 160
    for _ in range(0x64):                               # img 2aad, 100 rows
        for _ in range(0xa0):                           # img 2ab0, 160 cols
            ax = field[si] >> 3                         # img 2ab6
            bx = (di + ax + (ax << 6) + (ax << 8)) & 0xffff
            if bx >= 0xfd00:                            # img 2ac7
                bx -= 0x600
            for off in (0, 0x140):                      # img 2ad1, 2ada
                for k in (0, 1):                        # the word = 2 px
                    fb[(di + off + k) & 0xffff] = source[(bx + off + k)
                                                         & 0xffff]
            si += 1                                     # img 2adf, += 2 BYTES
            di += 2                                     # img 2ae2
        di += 0x140                                     # img 2ae7


def sin5_at(byte_off):
    """The sine table read ONE BYTE IN, as img 0x2840 and 0x284d do:
    `word ptr cs:[si + 0x7f5f]` against the table's 0x7f5e. Byte 1 and
    byte 2 of a little-endian dword are bits 8..23, so the word at +1
    IS the dword >> 8 - a free shift, giving a 5-bit sine of +/-15.
    """
    v = SIN_FLAT[(byte_off & 0x7fc) >> 2] >> 8
    return v


def interfere_render(fb, field, phase):
    """ORIGIN 1000:2818 maybe_Interfere_Render. THE MOIRE.

    Each pixel ACCUMULATES a smooth increment into the field every
    frame, and the field is 8-bit, so the mod-256 wrap of an
    accumulating ramp is what draws the lines. Nothing here plots a
    line at all.

        increment = sin5(rowPhase) + (200 - row) - col + sin5(colPhase)
        field[di] = (field[di] + increment) & 0xff

    THE -col TERM IS WHY THE LINES SQUEEZE. Neighbouring columns differ
    by 1 in their increment, so after n frames they differ by n, and
    the banding period is 256/n columns. The pattern starts coarse and
    tightens frame by frame without anything changing a parameter.

    The two sines are only +/-15 and bend the bands rather than
    forming them. Writes to both the field (DS = 0x2a79, which it
    clobbers) and 0xA000.

    Returns the advanced phase - img 0x2866 adds 2 per frame.
    """
    bp = 0xc8
    si = phase
    di = 0
    for _ in range(0xc8):                               # img 2834
        bx = phase                                      # RESET per row
        si &= 0x7fc                                     # img 283c
        dx = (sin5_at(si) + bp) & 0xffff                # img 2840-2845
        for _ in range(0x140):
            ax = (dx + sin5_at(bx)) & 0xffff            # img 2847-284d
            al = (ax + field[di]) & 0xff                # img 2852, AL ONLY
            field[di] = al
            fb[di] = al                                 # img 2856
            bx = (bx + 8) & 0xffff                      # img 2859
            di += 1
            dx = (dx - 1) & 0xffff                      # img 285d
        si += 8                                         # img 2860
        bp -= 1
    return (phase + 2) & 0xffff                         # img 2866


def new_flat_span_state():
    """g_SpanBuf's left/right pair per row, plus g_SpanMinY and
    g_SpanMaxY - which hold ROW * 16, not a row index."""
    return {'span': [[0x140, -1] for _ in range(200)],
            'lo': 0, 'hi': 0}


def poly_fill_flat(dest, verts, colour, st):
    """ORIGIN 1000:2cef Poly_FillFlat. POLY_FILL MINUS THE GOURAUD: the
    same span buffer, the same stride, the same convex min/max scheme
    and the same dirty box, but one flat colour and a byte fill.

    THE ROW IS CARRIED AS ROW * 16 THROUGHOUT, which is why the span
    stride is 16. img 0x2e40 turns that one value into the screen
    offset with two shifts and an add - row*64 + row*256 = row*320 -
    and reuses it unshifted as the span-buffer offset. No multiply
    anywhere.

    verts is five (x, y) pairs: img 0x2d32 copies vertex 0 into the
    fifth slot so the four-edge walk can read "the next vertex"
    without wrapping, exactly as Poly_Fill does.

    Writes to 0x6a79, hardcoded at img 0x2d42 - NOT g_WorkSeg and not
    the caller's ES. That is the only link between Tunnel_BuildAngle
    and Tunnel_Render; see effects/tunnel.txt section 3.
    """
    # img 2cef: reset only the rows the LAST call touched, to the
    # inverted left > right so an untouched row fills nothing.
    hi = min(st['hi'], 0xc70)
    lo = max(st['lo'], 0)
    if hi - lo >= 0:
        for k in range(((hi - lo) >> 4) + 1):
            r = (lo >> 4) + k
            if 0 <= r < 200:
                st['span'][r] = [0x140, -1]
    v = list(verts) + [verts[0]]                        # img 2d32
    st['lo'], st['hi'] = 0x7fff, -0x8000
    for e in range(4):                                  # img 2d49, di 0..0xc
        x0, y0 = v[e]
        x1, y1 = v[e + 1]
        if y0 > y1:                                     # img 2d5d
            x0, y0, x1, y1 = x1, y1, x0, y0
        if y0 >= 0xc8 or y1 <= 0:                       # img 2d65, 2d6d
            continue
        dy = y1 - y0
        if dy == 0:
            continue
        si = y0 << 4                                    # img 2d7e
        grad = ((x1 - x0) << 0xc) // dy if dy else 0    # img 2d85, 20.12
        if (x1 - x0) < 0 and ((x1 - x0) << 0xc) % dy:
            grad += 1                                   # IDIV truncates
        acc = (x0 << 0xc) + (grad >> 1)                 # img 2d94-2d9d
        st['lo'] = min(st['lo'], si)                    # img 2da2
        for _ in range(dy):
            if si >= 0xc80:                             # img 2dae
                break
            if si >= 0:                                 # img 2db4
                xx = (acc - 0x800) >> 0xc                # img 2db9
                r = si >> 4
                if st['span'][r][0] > xx:               # img 2dc7
                    st['span'][r][0] = xx
                if st['span'][r][1] < xx:               # img 2dd3
                    st['span'][r][1] = xx
            si += 0x10
            acc += grad
        st['hi'] = max(st['hi'], si - 0x10)             # img 2de7
    # img 2e00: the fill. One flat colour, one byte at a time.
    hi = min(st['hi'], 0xc70)
    lo = max(st['lo'], 0)
    if hi - lo < 0:
        return
    rows = ((hi - lo) >> 4) + 1
    di = (lo << 2) + (lo << 4)                          # img 2e40: row*320
    si = lo
    for _ in range(rows):
        r = si >> 4
        left = st['span'][r][0]
        if left <= 0x13f:
            if left < 0:
                left = 0
            right = st['span'][r][1]
            if right >= 0:
                if right > 0x13f:
                    right = 0x13f
                for x in range(left, right + 1):        # img 2e8c
                    dest[di + x] = colour
        di += 0x140
        si += 0x10


def tunnel_build_angle(st=None):
    """ORIGIN 1000:2c6a Tunnel_BuildAngle. 256 wedges from the screen
    centre, each flat-filled with its own index, so every pixel ends
    up holding its own angle. An atan2 table DRAWN rather than
    computed.

    Replaces tunnel_build_angle_OURS now that Poly_FillFlat is read.

    img 0x2c6d pins vertices 0 and 3 to (0xa0, 0x64) - the screen
    centre - and the other two to cos/sin >> 4 plus the centre, which
    is 255 units out: well off a 320x200 screen, so each wedge is
    clipped to the edge and the whole screen gets a value.

    THE COLOUR IS THE LOW BYTE OF THE DOWN-COUNTER, img 0x2c8c, so the
    angle values run backward: 0x00, 0xff, 0xfe ... 0x01.
    """
    if st is None:
        st = new_flat_span_state()
    tab = bytearray(320 * 200)
    for cx in range(0x100, 0, -1):                      # img 2c89
        colour = cx & 0xff                              # img 2c8c
        bx = (cx << 3) & 0x7fc                          # img 2c91-2c97
        x1 = to_s16((cos_at(bx) >> 4) + 0xa0)           # img 2c9b
        y1 = to_s16((sin_at(bx) >> 4) + 0x64)           # img 2ca8
        bx2 = (bx + 8) & 0x7fc                          # img 2cbe
        x2 = to_s16((cos_at(bx2) >> 4) + 0xa0)
        y2 = to_s16((sin_at(bx2) >> 4) + 0x64)
        poly_fill_flat(tab, [(0xa0, 0x64), (x1, y1), (x2, y2),
                             (0xa0, 0x64)], colour, st)
    return tab


def water_render_spans(fb, field):
    """ORIGIN 1000:2a1a Water_RenderSpans. The wave field drawn as
    COLUMN SPANS - a y-buffer, the same technique as Voxel_Render, so
    the water is seen from the side as a landscape.

    THE SCREEN POINTER ESI IS THE Y-BUFFER. It is set once per column
    at img 0x2a41 and only ever DECREASES, so each column fills
    bottom-up and nothing is ever overdrawn. img 0x2a6f fills from
    wherever it stands up to this cell's height and leaves it there.

    The shade is the horizontal gradient to the next column plus the
    row counter - a lighting term plus depth, img 0x2a4a. img 0x2a5c
    makes BH = BL and BL += 3, so the word store lays two ADJACENT
    palette entries, and img 0x2a83 XCHGs them after every row: a
    vertical checkerboard dither between shade and shade+3.

    One field row drives TWO screen rows, img 0x2a8b.
    """
    for col in range(0xa0, 0, -1):                      # img 2a26
        edx = (0xa0 - col) * 2                          # img 2a2a, col*2
        di = 0x7bc0 + edx                               # img 2a33, field row 99
        ebp = 0xf8c0 + edx                              # img 2a38, screen row 199
        esi = 0xfa00 + edx                              # img 2a41, one past the end
        cx = 0x65                                       # img 2a30, 101
        while cx:
            fi = di >> 1                                # bytes -> word index
            ax = field[fi]                              # img 2a4a
            bx = field[fi + 1] - ax                     # img 2a4d, next column
            bx = (bx >> 7) + cx                         # img 2a53-2a56
            if bx < 0:                                  # img 2a58
                bx = 0
            bl, bh = (bx + 3) & 0xff, bx & 0xff         # img 2a5c-2a5e
            edx_t = ebp - (to_s16(ax) << 4)             # img 2a61-2a6c
            while edx_t < esi:                          # img 2a6f
                esi -= 0x140                            # img 2a74
                if esi < 0:                             # img 2a7b
                    break
                if esi + 1 < 320 * 200:
                    fb[esi] = bl                        # img 2a81, a WORD
                    fb[esi + 1] = bh
                bl, bh = bh, bl                         # img 2a83, XCHG
            di -= 0x140                                 # img 2a87, up a field row
            ebp -= 0x280                                # img 2a8b, TWO screen rows
            cx -= 1


def ground_render(work, height, water, cam_x, cam_z, cam_angle,
                  cam_scale, ramp):
    """ORIGIN 1000:341c Ground_Render. The demo's SECOND landscape
    renderer - Voxel_Render's ray setup with the loop order inverted.

    THE OUTER LOOP IS DISTANCE, not column: it counts g_GroundDist down
    and recomputes the whole ray start each time, where Voxel_Render
    takes one column and marches outward.

    AND THE SCREEN WALK IS DIAGONAL - img 0x3531 adds 2 to DI per
    sample plus another 0x140 every OTHER sample - so each distance
    pass paints a 45-degree band, and successive passes start one
    screen row higher.

    height is segment 0x4a79, which FUN_39d8 fills from the plasma at
    0x2a79 just before this part, img 0x023e; water is the raw 0x2a79.
    NOTE THAT IS THE OPPOSITE PAIR FROM Voxel_Render, which takes its
    heightmap from 0x2a79 directly.

    The second pass is not here: it paints black from an unwritten
    table. See effects/ground.txt section 4.
    """
    dist = (0x80 - ramp) & 0xffff                       # img 3431
    if dist <= 0x50:                                    # img 3439, JA
        dist = 0x50
    di = 0xf8c0                                         # img 342e
    while dist > 1:                                     # img 3551
        step = (cam_scale // dist) & 0xffff             # img 3451
        bias = (step >> 5) & 0xff                       # img 3456, g_GroundBias
        bx = cam_angle & 0x7fc
        sn, cs_ = sin_at(bx), cos_at(bx)
        rx = to_s32(((sn - cs_) * step) << 4)           # img 346d-347d
        rx = to_s32(rx + (rx >> 2) + cam_x)             # img 3484-348b, *1.25
        rz = to_s32(((cs_ + sn) * step) << 4)           # img 3496-34a6
        rz = to_s32(rz + (rz >> 2) + cam_z)             # img 34aa-34b4
        sx = to_s32((cs_ * step) >> 2)                  # img 34bf-34c9
        sz = to_s32((sn * step) >> 2)                   # img 34d2-34dc
        saved = di
        bp = 0xa0                                       # img 34e5
        while bp:
            # img 34ed: SHR of the 32-bit, then SI = AX.
            # img 34f8: SAR 8 of the other, AND 0xff00, added in.
            si = (((rz & 0xffffffff) >> 0x10) & 0xffff)
            si = (si + ((rx >> 8) & 0xff00)) & 0xffff
            al = (height[si] - bias) & 0xff             # img 3501
            if al >= 0x20:                              # img 3508
                al = 0
            if water[si] < 0x21:                        # img 350e
                al = (al + 0x20) & 0xff                 # THE WATER BIAS
            if 0 <= di and di + 1 < 320 * 200:
                work[di] = al                           # img 3518, AH = AL
                work[di + 1] = al
            rx = to_s32(rx + sx)                        # img 351b
            rz = to_s32(rz - sz)                        # img 3526
            di += 2                                     # img 3531
            if bp & 1:                                  # img 3534
                di += 0x140
                if di >= 0xfa00:                        # img 353e
                    # JNC 0x3547 leaves the INNER loop only. The
                    # distance loop keeps going, and because each pass
                    # starts a row higher it gets progressively longer
                    # before it runs off the bottom.
                    break
            bp -= 1
        di = saved - 0x140                              # img 3548
        dist -= 1                                       # img 354c


def terrain_build_shade(heightmap, height_scale=0x1000, divisor=0x53):
    """ORIGIN 1000:39d8 Terrain_BuildShade. A lighting value per
    terrain cell, from the heightmap's HORIZONTAL GRADIENT, into a
    32-entry band. This is what fills segment 0x4a79 for
    Ground_Render.

    ONLY BL IS TOUCHED FOR THE NEIGHBOURS, img 0x39f5 and 0x39f9, so
    they wrap WITHIN THE 256-BYTE ROW - the x axis is a torus and no
    edge case is needed. The row index is BH.

    The output is clamped to 1..0x1e, which is why Ground_Render can
    subtract a bias and treat anything 0x20 or over as below
    threshold: the value can only reach 0x20 by UNDERFLOWING.

    g_ShadeSize is 0x100 and g_ShadeStart 0 at all six call sites, so
    this always does the full 256x256 from offset 0.
    """
    out = bytearray(0x10000)
    k = (height_scale & 0xffff) >> 6                    # img 3a12
    for row in range(0x100):
        base = row << 8
        for col in range(0x100):
            left = heightmap[base | ((col - 1) & 0xff)]  # img 39f5
            right = heightmap[base | ((col + 1) & 0xff)]  # img 39f9
            g = (left - right) * k                      # img 3a06-3a16
            # img 3a1a-3a3e: abs, unsigned divide, sign restored.
            v = (abs(g) // divisor) * (1 if g >= 0 else -1)
            v += 0x10                                   # img 3a42
            if v <= 0:                                  # img 3a45
                v = 1
            elif v >= 0x1f:                             # img 3a4d
                v = 0x1e
            out[base | col] = v                         # img 3a55
    return out


SCROLLER_TEXT = 0xa435                                  # img 0x313b


def scroller_build_rows():
    """ORIGIN 1000:301f Scroller_BuildRows. 128 records, one per screen
    row from 72 down, built from one divide.

    THE STEP IS THE RECIPROCAL OF THE WIDTH, img 0x3063: width is
    0x10000 / step, so STEP * WIDTH IS 0x10000 EXACTLY and every row
    spans precisely 256.0 texture units - one full 32-character line -
    however wide perspective makes it. That is the whole trick; there
    is no per-row text fitting anywhere.

    The font is NOT built here and NOT in the image: img 0x301f gets it
    from the video BIOS with INT 10h AX=1130 BH=3. img 0x302b adds 7,
    so g_FontOfs points at a glyph's LAST row and the blitter walks
    upward - get that sign wrong and the text is mirrored vertically.
    """
    rows = []
    for bp in range(0x80, 0, -1):                       # img 303b
        dist = 0x800000 // (0xc9 - bp)                  # img 3041-3053
        step = (dist >> 8) & 0xffff                     # img 3059
        wide = (0x10000 // step) & 0xffff if step else 0  # img 3063
        start = 0
        if wide > 0x140:                                # img 3075
            start = (step * ((wide - 0x140) >> 1)) & 0xffff
        width = min(wide, 0x140)                        # img 308d
        xoff = (0xa0 - (width >> 1)) & 0xffff           # img 3093
        # img 309d-30b7: the texture is centred on 0x8000 by backing
        # off half the UNCLAMPED width's worth of steps.
        start = (start + 0x8000 - step * (wide >> 1)) & 0xffff
        rows.append({'dist': dist, 'step': step, 'start': start,
                     'width': width, 'xoff': xoff})
    return rows


def scroller_render(work, source, blob, font, rows, scroll_pos,
                    font_ofs=7):
    """ORIGIN 1000:30c1 Scroller_Render. THE FONT IS A STENCIL AND
    NOTHING ELSE: it decides which pixels survive, and segment 0x6a79
    decides what they look like. So the credits are textured, and
    changing what is in the source changes their appearance without
    touching this routine.

    128 rows from screen row 72 - img 0x30db starts the cursor at
    0x5a00, which is 320*72.

    font is a flat 2048 bytes, 256 glyphs by 8 rows TOP TO BOTTOM, and
    font_ofs is the img 0x302b +7, so row `sub` is indexed as
    char*8 + font_ofs - sub and the glyph walks UPWARD.

    THE CHARACTER CACHE AND THE BIT TEST SHARE ONE REGISTER. DH holds
    the last character index and DL the font row byte, so img 0x312e
    compares DH to decide whether to re-read the font and img 0x3156
    does BT DX, BX on DL's bits. img 0x3127 primes DH to 0xff, which
    `ah >> 3` can never equal, so the first pixel of every row always
    loads.
    """
    si_base = 0x5a00                                    # img 30db
    for r in rows:                                      # img 30d8
        cx = r['width']
        if cx:                                          # img 30e5, ZERO = SKIP
            si = si_base + r['xoff']                    # img 30eb
            ax = r['start']                             # img 30ef
            step = r['step']                            # img 30f3
            t = ((r['dist'] - scroll_pos) & 0xffffffff) >> 9   # img 30fb
            sub = t & 7                                 # img 3108
            bx = to_s16(t & 0xffff) >> 3                # img 310b
            if bx > 0:                                  # img 310f
                bx = 0
            if bx < -0x53:                              # img 3117
                bx = -0x53
            line_base = (bx << 5) & 0xffff              # img 311f, 32 CHARS
            dh, dl = 0xff, 0                            # img 3127, the cache
            for _ in range(cx):
                col = (ax >> 8) & 0xff
                bl = col >> 3                           # img 312b
                if dh != bl:                            # img 312e
                    dh = bl
                    ch = blob[(SCROLLER_TEXT
                               + ((line_base + bl) & 0xffff)) & 0xffff]
                    dl = font[((ch << 3) + font_ofs - sub) & 0x7ff]
                if dl & (1 << ((~col) & 7)):            # img 314f-3159
                    work[si] = source[si]               # img 315b
                else:
                    work[si] = 0                        # img 3162
                ax = (ax + step) & 0xffff               # img 3165
                si += 1
        si_base += 0x140                                # img 3170


def font_standin():
    """A STAND-IN 8x8 FONT. THE DEMO CONTAINS NO FONT - img 0x301f
    asks the video BIOS for the ROM 8x8 set with INT 10h AX=1130 BH=3
    - so none can be transcribed out of the image.

    These glyphs are OURS. They exist only so the perspective, the row
    table and the stencil can be looked at; the LETTERFORMS are not
    the demo's and will not match it. Lowercase aliases uppercase,
    which the real ROM font does not do.

    Bit 7 is the leftmost pixel, matching img 0x314f: the blitter uses
    (~column) & 7 as the bit number.
    """
    G = {
        'A': '183c66667e666600', 'B': '7c66667c66667c00',
        'C': '3c66060606663c00', 'D': '786c6666666c7800',
        'E': '7e06063e06067e00', 'F': '7e06063e06060600',
        'G': '3c66063666663c00', 'H': '6666667e66666600',
        'I': '3c18181818183c00', 'J': '7830303030331e00',
        'K': '66361e1e3666e600', 'L': '0606060606067e00',
        'M': 'c6eefed6c6c6c600', 'N': '66666e7e76666600',
        'O': '3c66666666663c00', 'P': '7c66667c06060600',
        'Q': '3c666666663c7000', 'R': '7c66667c3666e600',
        'S': '3c66063c60663c00', 'T': '7e18181818181800',
        'U': '6666666666663c00', 'V': '66666666663c1800',
        'W': 'c6c6c6d6fed6c600', 'X': 'c6663c183c66c600',
        'Y': '66666666183c1800', 'Z': '7e60300c06067e00',
        '0': '3c666e7666663c00', '1': '18181818181c1800',
        '2': '7e60300c06663c00', '3': '3c66603860663c00',
        '4': '3030307e36303000', '5': '3c66607e06067e00',
        '6': '3c66663e06663c00', '7': '0c0c0c1830607e00',
        '8': '3c66663c66663c00', '9': '3c66607c66663c00',
        ' ': '0000000000000000', '.': '0018180000000000',
        ',': '0030181800000000', '!': '0018181818181800',
        '?': '001818603c663c00', ':': '0018180018180000',
        ';': '0030181800181800', '-': '000000003c000000',
        '(': '0c18303030180c00', ')': '3018181818183000',
    }
    G[chr(39)] = '0000000018181800'                     # the apostrophe
    # THE TABLE ABOVE IS WRITTEN LSB-LEFT because that is how it reads
    # as hex, and the blitter wants MSB-LEFT: img 0x314f tests bit
    # (~column) & 7, so column 0 is bit 7. Reversing each byte here is
    # what keeps the letters the right way round - without it every
    # glyph comes out mirrored left to right while the LINES stay
    # correct, which is exactly what that looks like on screen.
    rev = bytes(int('{:08b}'.format(v)[::-1], 2) for v in range(256))
    f = bytearray(0x800)
    for ch, hx in G.items():
        glyph = bytes(rev[v] for v in bytes.fromhex(hx))
        for c in (ch, ch.lower()):                      # the alias is OURS
            o = ord(c) << 3
            if o + 8 <= 0x800:
                f[o:o + 8] = glyph
    return f


FONT32 = 0x615e                                         # img 0x615e


def font32_glyph(blob, ch):
    """ORIGIN img 0x615e, read as Text_BuildBanner reads it. THE DEMO'S
    OWN FONT: 32x32 pixels, 0x80 bytes a glyph, first glyph at
    character 0x20.

    FOUR BYTES A ROW, LSB FIRST - img 0x33e7 does TEST AH, 1 and then
    shifts right, so bit 0 is the LEFTMOST pixel of each byte. That is
    the opposite order from the 8x8 BIOS font the scroller uses, where
    img 0x314f takes (~column) & 7 and bit 7 is leftmost. Two fonts,
    two bit orders, in the same program.

    Returns 32 strings of 32 characters.
    """
    o = FONT32 + (ord(ch) - 0x20) * 0x80
    out = []
    for row in range(32):
        bits = []
        for byte in range(4):
            v = blob[o + row * 4 + byte]
            for k in range(8):                          # img 33f9, SHR AH,1
                bits.append('#' if v & (1 << k) else '.')
        out.append(''.join(bits))
    return out


def text_build_banner(dest, blob, text, dst_ofs, bright=0x30):
    """ORIGIN 1000:33a0 Text_BuildBanner. Blits g_Font32 into a field,
    BLENDING rather than drawing.

    img 0x33ec: al = dest[di] >> 1 + g_BannerBright. So a glyph shows
    as a halved-and-brightened copy of whatever was already there,
    which is why the HUI letters carry the rotozoom's texture and the
    VISTA flag carries the plasma's.

    IT WRITES TO THE CALLER'S SEGMENT. Both call sites leave that as
    0x2a79 - img 0x0316 sets it for HUI, and at img 0x5123 it is what
    Voxel_Render left at img 0x364b - so the banners go INTO THE
    PLASMA FIELD, not onto the screen.

    'a' (0x61) and 'b' (0x62) are not letters: img 0x33c1 and 0x33ca
    shift the destination by +/-0x10, so they are line breaks.
    """
    di = dst_ofs
    for ch in text:
        c = ord(ch)
        if c == 0x61:                                   # img 33c1
            c, di = 0x20, di + 0x10
        elif c == 0x62:                                 # img 33ca
            c, di = 0x20, di - 0x10
        si = FONT32 + (c - 0x20) * 0x80                 # img 33d4-33da
        row_di = di
        for _ in range(0x20):                           # img 33dc, 32 rows
            p = row_di
            for byte in range(4):                       # img 33df, DH = 4
                ah = blob[si]
                si += 1
                for _ in range(8):                      # img 33e1, CL = 8
                    if ah & 1:                          # img 33e7
                        v = (dest[p & 0xffff] >> 1) + bright
                        dest[p & 0xffff] = v & 0xff     # img 33f6
                    ah >>= 1                            # img 33f9
                    p += 1
            row_di = (row_di + 0x100) & 0xffff          # img 3404, +0xe0+0x20
        di = (di + 0x20) & 0xffff                       # img 340d


def pal_set_rgb_ramps():
    """ORIGIN 1000:3e31 Pal_SetRgbRamps. THE COLD PALETTE: four bands
    of 64, written STRAIGHT TO THE DAC with OUT 0x3c8 then 0x3c9 and
    bypassing g_PaletteBase entirely - which is why no fade can touch
    it. See subsystems/palette.txt.

        0.. 63   R 0,    G 0,    B n      black  -> blue
       64..127   R 0,    G n,    B 0x3f   blue   -> cyan
      128..191   R n,    G 0x3f, B 0x3f   cyan   -> white
      192..255   R 0x3f, G 0x3f, B 0x3f   flat white

    Each band counts BL from 0 to 0x3f, so a component reaches the
    DAC's full 63 here - unlike Pal_BuildMain's ramps, which step by 2
    and stop one short.

    Water_RenderSpans' shades land around 0..104, so the water reads
    black through blue into cyan.
    """
    pal = bytearray(768)
    for n in range(0x40):                               # img 3e3d
        pal[n * 3 + 0] = 0
        pal[n * 3 + 1] = 0
        pal[n * 3 + 2] = n
    for n in range(0x40):                               # img 3e4e
        i = 0x40 + n
        pal[i * 3 + 0] = 0
        pal[i * 3 + 1] = n
        pal[i * 3 + 2] = 0x3f
    for n in range(0x40):                               # img 3e61
        i = 0x80 + n
        pal[i * 3 + 0] = n
        pal[i * 3 + 1] = 0x3f
        pal[i * 3 + 2] = 0x3f
    for n in range(0x40):                               # img 3e70
        i = 0xc0 + n
        pal[i * 3 + 0] = 0x3f
        pal[i * 3 + 1] = 0x3f
        pal[i * 3 + 2] = 0x3f
    return pal


WATER_DROP_ROWS = (35, 43, 51, 59, 67, 75, 83, 91)      # img 0x1674, 0x169f


def water_drop_step(field, slots):
    """ORIGIN img 0x166f, the mode 0x12 handler. THE DRIP SOURCE.

    Eight slots, each a drop at a FIXED ROW - 35, 43, 51, 59, 67, 75,
    83 and 91 of the 100-row field, evenly eight apart - and at the
    CENTRE COLUMN 80 plus its own horizontal offset.

    img 0x1696 writes 0xfc00, which as a signed word is -1024: A
    NEGATIVE IMPULSE, a dent in the surface, which is what a falling
    drop makes. A positive one would be a bump.

    slots is a list of [lifetime, position]. img 0x167c skips a slot
    whose lifetime is zero and img 0x1684 decrements it, so each drop
    pokes for `lifetime` ticks and then stops; img 0x0d26 sets them to
    0x10.

    The field is g_WaterBufA - DS at img 0x1677 - so the drop lands in
    the buffer Water_Step is about to write, not the one it reads.
    """
    for n, slot in enumerate(slots):
        if not slot[0]:                                 # img 167c
            continue
        slot[0] -= 1                                    # img 1684
        # img 1689: MOV EDI, dword - img 168f: SHR EDI, 9 (UNSIGNED
        # 32-bit) - img 1693: AND DI, 0xfffe. So a negative position
        # shifts as a large unsigned and then takes its low 16 bits,
        # which WRAPS the column. Reproduced, not corrected.
        di = (((slot[1] & 0xffffffff) >> 9) & 0xfffe)   # img 1689-1693
        ofs = (0x2d00 + n * 0xa00 + di - 0xa0) & 0xffff  # img 1696
        field[(ofs >> 1) & 0xffff] = to_s16(0xfc00)


def pal_build_blue_magenta(pal):
    """ORIGIN 1000:3df6 Pal_BuildBlueMagenta. THE TUNNEL'S PALETTE.
    Three bands of 64 into g_PaletteBase, and IT STOPS AT 192 - so
    192..255 keep whatever Pal_BuildMain left.

        0.. 63   R 0, G 0, B n        black -> blue
       64..127   R n, G 0, B 0x3f     blue  -> magenta
      128..191   R n, G 0, B n, n COUNTING DOWN from 0x3f
                                      magenta -> black

    Band 3 counting down is what closes the ramp back to black, which
    is what makes the tunnel's depth wrap read as repeating rings.

    Call it on top of pal_build_main, which is what img 0x08fe and
    0x0901 do in that order.
    """
    for n in range(0x40):                               # img 3e02
        pal[n * 3 + 0] = 0
        pal[n * 3 + 1] = 0
        pal[n * 3 + 2] = n
    for n in range(0x40):                               # img 3e11
        i = 0x40 + n
        pal[i * 3 + 0] = n
        pal[i * 3 + 1] = 0
        pal[i * 3 + 2] = 0x3f
    for n in range(0x40):                               # img 3e23
        i = 0x80 + n
        v = 0x3f - n                                    # img 3e2c, DEC BL
        pal[i * 3 + 0] = v
        pal[i * 3 + 1] = 0
        pal[i * 3 + 2] = v
    return pal


def countdown_warp(field, x, y, ofs_x, ofs_y):
    """ORIGIN img 0x3941 and 0x395b. THE COUNTDOWN PARTICLES ARE
    WARPED, and this is the piece that was missing - without it they
    fly straight instead of waving.

    bx indexes the field at segment 0x2a79 from the particle's own
    position, then g_WarpOfsX is added, and g_WarpOfsY is added to the
    ALREADY-OFFSET bx - so the second sample is at base + ofsX + ofsY,
    cumulatively, not independently.

        x += (field[bx + ofsX] >> 3)       then - 0x10, so +/-16 px
        y += (field[bx + ofsX + ofsY] & 0xf8) << 5

    The mode 1 handler advances both offsets every tick, so the dots
    wave exactly as Flag_Render's panel does.

    Returns the displaced (si, ax) in the demo's units.
    """
    si = (x & 0xffff) >> 4                              # img 3934
    # img 3937 then 393c: AND 0xfff0 then SHL 4, and THE SAME SHIFTED
    # AX is reused for the screen y at img 0x3969. Shifting it only
    # inside the bx computation puts the y out by a factor of 16.
    ax = ((y & 0xfff0) << 4) & 0xffff
    bx = (si + ax) & 0xffff                             # img 393a-393f
    bx = (bx + ofs_x) & 0xffff                          # img 3941
    dx = field[bx] >> 3                                 # img 3948-394b
    si = (si + dx - 0x10) & 0xffff                      # img 394e-3950
    bx = (bx + ofs_y) & 0xffff                          # img 395b, CUMULATIVE
    dx = ((field[bx] & 0xf8) << 5) & 0xffff             # img 3960-3966
    ax = (ax + dx - 0x1000) & 0xffff                    # img 3969-396b
    return si, ax


def flag_render(fb, source, work, sine_field, ofs_x, ofs_y):
    """ORIGIN 1000:2504 Flag_Render. THE WAVING VISTA FLAG: a 186x152
    panel of the source field displaced by two scrolling samples of
    the sine field, in 2x2 blocks straight to 0xA000.

    source is segment 0x2a79, holding the plasma with the word VISTA
    blended into it by Text_BuildBanner; work is g_WorkSeg, which the
    OUT-OF-RANGE FALLBACK reads - and that fallback is why the
    landscape shows around and through the flag instead of a black
    box.

    sine_field is 0x8a79 at +0x4564, wrapped & 0x3fff.

    THE Y DISPLACEMENT IS PRE-MULTIPLIED BY THE SOURCE STRIDE: img
    0x2550 shifts it left 6 after masking 0xfc, which makes it a row
    offset in a 0x100-wide field rather than a value to multiply.

    THE BOUNDS TEST IS ON THE TWO HALVES OF BX SEPARATELY, img 0x255a
    and 0x255f: BL < 0xa0 and BH < 0x80.
    """
    si = 0x1846                                         # img 2518
    di = 0x1e46                                         # img 251b
    for _ in range(0x4c):                               # img 251e, 76 rows
        for _ in range(0x5d):                           # img 2521, 93 cols
            bp = (ofs_x + si) & 0x3fff                  # img 2526-252d
            bl = (sine_field[bp] >> 2) - 0x70           # img 2531-2539
            bp = (ofs_y + si) & 0x3fff                  # img 253e-2545
            al = sine_field[bp] & 0xfc                  # img 2549-254e
            ax = ((al << 6) - 0x4200) & 0xffff          # img 2550-2553
            bx = ((bl & 0xff) + ax + si) & 0xffff       # img 2556-2558
            if (bx & 0xff) < 0xa0 and ((bx >> 8) & 0xff) < 0x80:
                for k in (0, 1):                        # img 2564, a WORD
                    fb[di + k] = source[(bx + k) & 0xffff]
                    fb[di + 0x140 + k] = source[(bx + 0x100 + k) & 0xffff]
            else:                                       # img 2574, THE FALLBACK
                for k in (0, 1):
                    fb[di + k] = work[di + k]
                    fb[di + 0x140 + k] = work[di + 0x140 + k]
            di += 2                                     # img 2584
            si += 2                                     # img 2587
        si += 0x146                                     # img 258c, two rows
        di += 0x1c6                                     # img 2590, two rows


def pal_build_blue_purple():
    """ORIGIN 1000:3cac Pal_BuildBluePurple. THE MOIRE'S PALETTE, and
    the sync 0x0e part's. Six bands, and IT IS CYCLIC - index 0 and
    index 255 are both black.

        0.. 63  64  R 0,    G 0,      B n        black  -> blue
       64.. 95  32  R n,    G 0,      B 0x3f     blue   -> purple
       96..127  32  R 0x3f, G n,      B 0x3f     purple -> white
      128..159  32  R 0x3f, G n down, B 0x3f     white  -> purple
      160..191  32  R n down, G 0,    B 0x3f     purple -> blue
      192..255  64  R 0,    G 0,      B n down   blue   -> black

    THERE IS NO RED OR ORANGE IN IT. R only ever rises while B is
    already full, so it reads purple and never red.

    THE SYMMETRY IS THE POINT. Moire_Render's content is a mod-256
    accumulator, so a palette whose ends did not meet would put a hard
    edge at every wrap. This one runs out to white and back.

    The 32-entry bands step by 2 and the 64-entry bands by 1, so all
    of them cover 0..62 or 0..63.
    """
    pal = bytearray(768)
    i = 0

    def put(n, r, g, b):
        nonlocal i
        pal[i * 3 + 0] = r
        pal[i * 3 + 1] = g
        pal[i * 3 + 2] = b
        i += 1

    for n in range(0x40):                               # img 3cb8
        put(n, 0, 0, n)
    for n in range(0x20):                               # img 3cc7
        put(n, n * 2, 0, 0x3f)
    for n in range(0x20):                               # img 3cda
        put(n, 0x3f, n * 2, 0x3f)
    for n in range(0x20):                               # img 3ced
        put(n, 0x3f, (0x3f - n * 2) & 0x3f, 0x3f)
    for n in range(0x20):                               # img 3d00
        put(n, (0x3f - n * 2) & 0x3f, 0, 0x3f)
    for n in range(0x40):                               # img 3d13
        put(n, 0, 0, 0x3f - n)
    return pal
