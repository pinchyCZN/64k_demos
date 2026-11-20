"""Which tick of the demo a save state is sitting at.

Not when the snapshot was taken - WHERE IN THE DEMO it is. Our
dumper renders one tick per frame, so a snapshot's tick IS the
frame number to render for a like-for-like comparison, and without
it any difference between the two pictures could just as well be
the two being at different moments.

An emulator snapshot carries no tick counter. Three globals between
them pin one tick exactly - docs/lithium/subsystems/music.txt 5.5:

    g_MusicPos    img 0x8332  guest @9d82   the order
    g_CreditFade  img 0x8fe0  guest @aa30   0xf down to 0, one per 8
    g_CreditTick  img 0x8fe2  guest @aa32   free-running byte

The order gives a 192 or 384 tick window, the fade narrows it to
eight, and the byte picks the one tick that fits.

IT DOES NOT DEPEND ON ANY PARTICULAR SNAPSHOT. The three values are
read live, so re-saving a slot costs nothing - read them again and
ask again.

    py -3.10 tools/savestate_tick.py --order 10 --fade 4 --tick-byte 0xd8
    py -3.10 tools/savestate_tick.py --from-dump build/s6.bin
    py -3.10 tools/savestate_tick.py --timeline
    py -3.10 tools/savestate_tick.py --at 2636

--from-dump READS THE THREE OUT OF A MEMORY DUMP so they need not be
transcribed by hand. Take it with the debugger's own dump verb, from
g_MusicPos far enough to reach g_CreditTick:

    dos_dump @9d82 3256 build/s6.bin

The dump has to come through the debugger rather than this script:
the pipe is nMaxInstances=1, so while an MCP server holds it a second
client cannot open it at all.

The timeline is walked out of the module in the depacked image, the
same way src/lithium/lithium_music.c walks it, so the two cannot
disagree without one of them being wrong.
"""

import argparse
import os
import struct
import sys

IMAGE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     '..', 'reverse', 'unpacked', 'lithium.bin')
MOD_BASE = 0x1ad20
START_TICK = 4            # where the replayer's first row lands

# THE GUEST LOADS THE IMAGE AT SEGMENT 0x1a5, so a Ghidra 1000:X is
# guest linear X + 0x1a50 - docs/lithium/README.txt. These are the
# three --from-dump reads, as guest addresses.
GUEST_BASE = 0x1a50
A_MUSIC_POS = 0x8332 + GUEST_BASE
A_CREDIT_FADE = 0x8fe0 + GUEST_BASE
A_CREDIT_TICK = 0x8fe2 + GUEST_BASE
SCENE_ORDER = 8           # the surface flyover's first order
CREDITS = 8               # entries in g_CreditTable


def walk(path):
    """Order start ticks, honouring effect A and the pattern breaks.

    RETURNED INDEXED BY THE CURSOR, NOT THE ENTRY. g_MusicPos is one
    ahead of what is sounding - 1000:7875 reads the list entry at the
    cursor and then increments it - so the arrays are shifted by one
    and every caller can index straight off the word a dump gives it.
    Index 0 is the gap before the first row; entry N is at N + 1.
    docs/lithium/subsystems/music.txt section 5.2.
    """
    d = open(path, 'rb').read()
    b = MOD_BASE
    ordnum, insnum, patnum = struct.unpack_from('<3H', d, b + 0x20)
    speed = d[b + 0x31] or 6
    orders = list(d[b + 0x60: b + 0x60 + ordnum])
    ppara = b + 0x60 + ordnum + 2 * insnum
    pats = struct.unpack_from('<%dH' % patnum, d, ppara)

    starts, lens, used = [], [], []
    tick = START_TICK
    for o, pat in enumerate(orders):
        starts.append(tick)
        if pat >= patnum:
            lens.append(0)
            used.append((pat, 0, speed))
            break
        off = b + pats[pat] * 16
        end = off + 2 + struct.unpack_from('<H', d, off)[0]
        p, row = off + 2, 0
        setspeed = [0] * 64
        brk = 63
        while p < end and row < 64:
            w = d[p]
            p += 1
            if not w:
                row += 1
                continue
            if w & 32:
                p += 2
            if w & 64:
                p += 1
            if w & 128:
                c, v = d[p], d[p + 1]
                p += 2
                if c == 1 and v:
                    setspeed[row] = v
                if c in (2, 3) and row < brk:
                    brk = row
        n, s0 = 0, speed
        for r in range(brk + 1):
            if setspeed[r]:
                speed = setspeed[r]
            n += speed
        lens.append(n)
        used.append((pat, brk + 1, s0 if s0 == speed else speed))
        tick += n
    return [0] + starts, [0] + lens, [None] + used


def from_dump(path, base):
    """(order, fade, tick byte) out of a guest memory dump.

    base is where the dump STARTS, so a dump taken from somewhere
    other than g_MusicPos still works as long as it spans all three.
    """
    d = open(path, 'rb').read()
    out = []
    for addr, width in ((A_MUSIC_POS, 2), (A_CREDIT_FADE, 1),
                        (A_CREDIT_TICK, 1)):
        i = addr - base
        if i < 0 or i + width > len(d):
            raise SystemExit(
                'tools/savestate_tick.py: the dump does not reach '
                '@%x - it starts at @%x and is %d bytes. Take it as '
                '"dos_dump @%x %d FILE".'
                % (addr, base, len(d), A_MUSIC_POS,
                   A_CREDIT_TICK - A_MUSIC_POS + 1))
        out.append(d[i] | (d[i + 1] << 8) if width == 2 else d[i])
    return out


def order_at(starts, lens, tick):
    for o in range(len(starts) - 1, -1, -1):
        if lens[o] and tick >= starts[o]:
            return o
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--order', type=int, help='g_MusicPos')
    ap.add_argument('--fade', type=lambda v: int(v, 0), help='g_CreditFade')
    ap.add_argument('--tick-byte', type=lambda v: int(v, 0),
                    help='g_CreditTick')
    ap.add_argument('--from-dump', dest='dump',
                    help='read the three out of a guest memory dump')
    ap.add_argument('--base', type=lambda v: int(v, 0), default=A_MUSIC_POS,
                    help='the guest address the dump starts at')
    ap.add_argument('--at', type=int, help='go the other way: tick -> state')
    ap.add_argument('--timeline', action='store_true')
    ap.add_argument('--scene-order', type=int, default=SCENE_ORDER,
                    help='the order the scene holding g_CreditTick began on')
    ap.add_argument('--image', default=IMAGE)
    a = ap.parse_args()

    starts, lens, used = walk(a.image)

    if a.dump:
        a.order, a.fade, a.tick_byte = from_dump(a.dump, a.base)
        print('%s: g_MusicPos %d  g_CreditFade 0x%02x  g_CreditTick 0x%02x'
              % (a.dump, a.order, a.fade, a.tick_byte))

    if a.timeline:
        print('g_MusicPos reads  entry  pat  rows  speed  ticks   begins')
        for o in range(1, min(len(starts), 20)):
            pat, rows, sp = used[o]
            print('%12d  %8d  %3d  %4d  %5d  %5d  %7d'
                  % (o, o - 1, pat, rows, sp, lens[o], starts[o]))
        return 0

    if a.at is not None:
        o = order_at(starts, lens, a.at)
        into = a.at - starts[o]
        entry = starts[a.scene_order]
        print('tick %d is order %d, %d ticks in (order spans %d..%d)'
              % (a.at, o, into, starts[o], starts[o] + lens[o] - 1))
        if o >= a.scene_order:
            print('  credit index %d, g_CreditTick 0x%02x'
                  % (o - a.scene_order, (a.at - entry) & 0xff))
            if into < 120:
                print('  g_CreditFade about 0x%x' % (0xf - into // 8))
            else:
                print('  g_CreditFade 0')
        return 0

    if a.order is None:
        ap.error('give --order, or --at, or --timeline')
    if not (0 <= a.order < len(starts)) or not lens[a.order]:
        ap.error('order %d is not in the list' % a.order)

    # g_MusicPos LAGS THE BOUNDARY by a tick or two - the replayer
    # advances the order after the pattern's last row finishes - so the
    # window runs two ticks past the computed end.
    lo, hi = starts[a.order], starts[a.order] + lens[a.order] + 2
    print('order %d spans ticks %d..%d' % (a.order, lo, hi - 3))

    # THE ROLL STICKS once the table runs out, and the fade is then
    # forced to 0 for the rest of the scene - effects/text.txt 7.2 - so
    # a zero fade says nothing after that.
    stuck = a.order - a.scene_order >= CREDITS
    if a.fade is not None and not stuck:
        if a.fade:
            # 15 - fade steps have elapsed, one per eight ticks, and the
            # first lands on the next multiple of eight rather than
            # eight ticks in - so the window is nine wide, not eight.
            lo2 = lo + 8 * (15 - a.fade)
            hi2 = lo2 + 9
        else:
            lo2, hi2 = lo + 120, hi
        lo, hi = max(lo, lo2), min(hi, hi2)
        print('  fade 0x%x narrows it to %d..%d' % (a.fade, lo, hi - 1))
    elif stuck:
        print('  credit index %d is past the table, so the roll has stuck '
              'and the fade carries nothing' % (a.order - a.scene_order))

    cand = list(range(lo, hi))
    if a.tick_byte is not None:
        entry = starts[a.scene_order]
        cand = [t for t in cand if ((t - entry) & 0xff) == a.tick_byte]
        print('  g_CreditTick 0x%02x leaves %d candidate(s)'
              % (a.tick_byte, len(cand)))
    for t in cand:
        d = t - starts[a.order]
        print('  tick %d is %d ticks into the order, fade would be 0x%x'
              % (t, d, 0 if d >= 120 else 0xf - d // 8))

    if not cand:
        print('NOTHING FITS. The three values disagree - check which scene '
              'the state is in, since g_CreditTick only runs in the one '
              'that owns it (--scene-order).')
        return 1
    print()
    for t in cand[:4]:
        print('  tick %d   ->   build\\Win32\\Debug\\lithium_ppm.exe '
              '-skip %d -n 1 -o build/ppm' % (t, t))
    if len(cand) > 4:
        print('  ... and %d more' % (len(cand) - 4))
    return 0


if __name__ == '__main__':
    sys.exit(main())
