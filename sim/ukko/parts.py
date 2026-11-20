"""UKKO - StickMan's World transcriptions.

Every routine's docstring opens with ORIGIN and an address in the
DEPACKED image, which is Ghidra's UKKO_U.EXE. Its CODE_1 begins at
1020:0000, so a listing address 1020:XXXX is image offset
0x200 + XXXX. CLAUDE.md rule 15: this is a transcription.

GHIDRA PRINTS BRANCH TARGETS AGAINST A 1000: BASE while the listing
uses 1020:, so a printed target is 0x200 higher than the address it
names. Every ORIGIN here is a listing address.

EVERYTHING IN THIS DEMO IS THE DEMO'S OWN. The figure comes from its
keyframes through its skeleton, the scenery and the logos are its
bitmaps, and the palette is the ramp its own code programs. Nothing
is invented.

THE ONE THING THAT IS OURS IS THE FRAME RATE. Nothing on the demo's
per-frame path blocks - docs/ukko/subsystems/timeline.txt section 2 -
so its real rate is whatever the hardware cost, and the sim picks a
number. It says so in the window title.
"""

import os
import struct

SCREEN_W = 320
SCREEN_H = 200

# CODE_1 starts at 1020:0000, which is image offset 0x200. A CS-
# relative offset is therefore CS_BASE + offset.
CS_BASE = 0x200

# img 1020:08a5, the byte after Line_Draw's RETF. 705 keyframes of
# 0x30, of which 694 are played. docs/ukko/formats/pose.txt.
POSE_DATA = 0x8a5
POSE_STRIDE = 0x30
POSE_JOINTS = 12
POSE_FRAMES = 0x2b6                 # img 1020:efe7, the exit test

# The twelve bones, as the joint-offset pairs Demo_FrameLoop sets into
# g_JointA and g_JointB. Joint n is at byte offset n*4.
BONES = ((0, 1), (1, 2), (2, 3), (1, 4), (4, 5), (0, 6),
         (6, 7), (0, 8), (8, 9), (0, 10), (0, 11), (11, 10))

# The four backdrops, as the segments the scene handlers load into DS
# at img 1020:f0ca onward. Each is a full 0xfa00-byte mode 13h screen.
BACKDROP_SEGS = (0x1f4a, 0x2eeb, 0x3e8c, 0x4e2d)
BACKDROP_BYTES = 0xfa00

# The three mode 0Dh logo planes, as CS offsets. 0x1f40 bytes each,
# which is 320*200/8 - one EGA plane, so one bit per pixel.
LOGO_OFFS = (0x8795, 0xa6d6, 0xc617)
LOGO_BYTES = 0x1f40

# ORIGIN depacked 1020:e885 - the g_PartIndex dispatch inside
# Pose_Tween, as (prev weight, current weight, shift). EIGHT evenly
# spaced positions per keyframe: PartIndex 1 draws the keyframe
# itself through 1020:eee0 with no blend at all, and 3 to 9 give the
# seven eighths between. The weights are written as different shifts
# because 2/8 and 6/8 reduce. docs/ukko/formats/pose.txt section 5.
TWEEN = ((7, 1, 3), (3, 1, 2), (5, 3, 3), (1, 1, 1),
         (3, 5, 3), (1, 3, 2), (1, 7, 3))
SUBFRAMES = len(TWEEN) + 1

# img 1020:e695 and 1020:e667 - the title logo is held for 0x1a9
# retraces before the dance, and an interlude for 0xc8.
TITLE_HOLD = 0x1a9
LOGO_HOLD = 0xc8

# img 1020:e6a9 and 1020:eeeb. Every line and every backdrop pixel is
# this value; the artwork is two-valued, 0x00 and 0xff.
PEN = 0xff

# The scene switches and the logo interludes, both off g_FrameIndex.
# img 1020:f053, f067, f07b and f166, f171, f17f.
SCENE_AT = ((0x000, 0), (0x086, 1), (0x208, 0), (0x21e, 3))
LOGO_AT = ((0x06e, 1), (0x193, 2), (0x208, 2))

# OURS: the run divided at its interludes, for stepping about in it.
SECTION_KF = (0,) + tuple(at for at, _ in LOGO_AT)

_IMG = [None]


def _repo_root():
    return os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))


def image():
    """The depacked image. tools/unpack/ukko_unpack.py regenerates it."""
    if _IMG[0] is None:
        p = os.path.join(_repo_root(), 'reverse', 'unpacked', 'ukko.bin')
        with open(p, 'rb') as f:
            _IMG[0] = f.read()
    return _IMG[0]


def pal_build_main():
    """ORIGIN depacked 1020:e614 - the only palette the demo programs.

        dx = 0x3c8;  al = 0;  out dx, al
        inc dx;  cx = 0x100
      again:
        al = cl;  neg al;  shr al, 2
        out dx, al  x3                     R, G and B the same
        loop again

    A LINEAR GREYSCALE, index >> 2. CX runs 0x100 down to 1, so the
    DAC index that auto-increments from 0 gets (-cl) >> 2 - index 0 is
    black and index 255 is 0x3f, full white.

    IT IS PROGRAMMED ONCE AND THEN THROWN AWAY. The mode 0Dh set at
    1020:e64b resets the DAC, the mode 13h set at 1020:e690 resets it
    again, and nothing reprograms it - so on a conforming VGA BIOS the
    figure is drawn in index 255 against a default palette whose last
    eight entries are black. docs/ukko/subsystems/video.txt.

    THE SIM USES THE RAMP THIS CODE WRITES, not the default palette
    the demo actually ends up with. Rendering the bug faithfully is a
    black screen, which shows nothing.
    """
    pal = bytearray(768)
    for cx in range(0x100, 0, -1):
        i = 0x100 - cx
        v = ((-(cx & 0xff)) & 0xff) >> 2
        pal[i * 3] = pal[i * 3 + 1] = pal[i * 3 + 2] = v
    return bytes(pal)


def pal_to_rgb(pal):
    """6-bit DAC values to 8-bit RGB triples."""
    return [(pal[i * 3] * 255 // 63, pal[i * 3 + 1] * 255 // 63,
             pal[i * 3 + 2] * 255 // 63) for i in range(256)]


def pose(frame):
    """ORIGIN depacked 1020:f256 - Pose_FetchJointPair's source.

    A joint is FOUR BYTES, x then y, both unsigned words read with
    MOVZX. A keyframe is twelve of them, 0x30 bytes.
    """
    img = image()
    o = CS_BASE + POSE_DATA + frame * POSE_STRIDE
    return [struct.unpack('<HH', img[o + j * 4:o + j * 4 + 4])
            for j in range(POSE_JOINTS)]


def pose_tween(prev, cur, joint, sub):
    """ORIGIN depacked 1020:e823 - Pose_Tween, and the g_PartIndex
    dispatch at 1020:e885 that picks the weight.

    Reads the same joint from the PREVIOUS keyframe and the current
    one - SUBTRACTING 0x30 FROM THE JOINT OFFSET is how one index
    serves both, with no second cursor and no pose buffer - then
    blends them by the sub-frame's weight from TWEEN.

    sub 0 IS THE KEYFRAME ITSELF. The loop takes a different path
    for it, 1020:eee0, which calls Pose_FetchJointPair directly and
    never reaches Pose_Tween.

    THE TRUNCATION IS LOAD-BEARING. Each weight pair is written with
    the smallest shift that expresses it, so 2/8 is (3*prev+cur)>>2
    and not (6*prev+2*cur)>>3, and the two round differently.
    """
    px, py = prev[joint]
    cx, cy = cur[joint]
    if sub == 0:
        return (px, py)
    wp, wc, sh = TWEEN[sub - 1]
    return (((px * wp + cx * wc) >> sh), ((py * wp + cy * wc) >> sh))


def line_draw(fb, x0, y0, x1, y1, colour=PEN):
    """ORIGIN depacked 1020:0497 - Line_Draw.

    A DDA with no error term. The original holds the fraction in DL
    and lets the carry out of `add dl, g_LineFrac` be consumed by
    `sbb ebx, g_LineStepY`, so the slow axis steps exactly when the
    fraction wraps. g_LineFrac is (|dy| << 8) / |dx|, an 8.8 slope -
    img 1020:057b.

    CLIPPING REJECTS THE PLOT, NOT THE STEP: every bounds failure
    jumps past the store to the `add dl`, so the walk continues off
    screen and re-enters in the right place. Keeping that shape is
    why the bounds test is inside the loop here too.
    """
    dx, dy = x1 - x0, y1 - y0
    sx = 1 if dx > 0 else (-1 if dx < 0 else 0)
    sy = 1 if dy > 0 else (-1 if dy < 0 else 0)
    adx, ady = abs(dx), abs(dy)
    W, H = SCREEN_W, SCREEN_H
    if adx == 0 and ady == 0:
        if 0 <= x0 < W and 0 <= y0 < H:
            fb[y0 * W + x0] = colour
        return
    if ady <= adx:
        frac, acc, y = (ady << 8) // adx, 0, y0
        for i in range(adx + 1):
            x = x0 + sx * i
            if 0 <= x < W and 0 <= y < H:
                fb[y * W + x] = colour
            acc += frac
            if acc > 0xff:
                acc &= 0xff
                y += sy
    else:
        frac, acc, x = (adx << 8) // ady, 0, x0
        for i in range(ady + 1):
            y = y0 + sy * i
            if 0 <= x < W and 0 <= y < H:
                fb[y * W + x] = colour
            acc += frac
            if acc > 0xff:
                acc &= 0xff
                x += sx


def backdrop(which):
    """ORIGIN depacked 1020:f0ca onward - the scene handlers.

    Each copies 0x3e80 dwords from its own segment into the back
    buffer at 0x7098. The buffer is NEVER cleared; overwriting it with
    a backdrop is what erases the previous frame's figure.
    """
    img = image()
    o = (BACKDROP_SEGS[which] << 4) - 0x10000
    return img[o:o + BACKDROP_BYTES]


def logo_plane(which):
    """ORIGIN depacked 1020:e65f, f1b1, f1e3 - the mode 0Dh blits.

    0x1f40 is 8000, which is 320*200/8 - ONE plane of mode 0Dh, so the
    image is one bit per pixel with a 40-byte row stride. Expanded
    here to the sim's byte-per-pixel framebuffer.
    """
    img = image()
    o = CS_BASE + LOGO_OFFS[which]
    src = img[o:o + LOGO_BYTES]
    fb = bytearray(SCREEN_W * SCREEN_H)
    for y in range(SCREEN_H):
        row = y * 40
        for xb in range(40):
            b = src[row + xb]
            if not b:
                continue
            base = y * SCREEN_W + xb * 8
            for bit in range(8):
                if b & (0x80 >> bit):
                    fb[base + bit] = PEN
    return bytes(fb)


def scene_for(frame):
    """ORIGIN depacked 1020:f053, f067, f07b - the scene switches."""
    which = SCENE_AT[0][1]
    for at, w in SCENE_AT:
        if frame >= at:
            which = w
    return which
