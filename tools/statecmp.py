"""Compare the reconstruction's state against the original's, by tick.

OURS IN FULL. Two pictures that differ may only be at different
moments, so a comparison has to be on STATE AT A TICK - the original
draws every second tick, so its frame N and ours are not the same
picture even when the state matches.

    dos_dump @9d82 3256 build/s6.bin                  on the demo side
    lithium_ppm -states -n 3600 > build/states.txt     on ours

    py -3.10 tools/statecmp.py build/s6.bin build/states.txt
    py -3.10 tools/statecmp.py build/s6.bin build/states.txt --drift
    py -3.10 tools/statecmp.py build/s6.bin build/states.txt --at 3565

--drift IS THE POINT. A flight path that is the right shape but the
wrong phase reads as a path bug and is not one, so this sweeps our
ticks either side and reports the offset that lines the two up. The
answer is a number, which a look at two screens is not.

A BEST FIT IS ONLY A MEASUREMENT WHILE THE STATES STILL MATCH.
Once they diverge every offset scores about the same and the
winner is noise, which reads as the phase having moved when it has
not - so --drift says whether the minimum is exact, sharp, or not
usable at all, and refuses to call a flat one an offset.

BOTH SIDES EMIT THE SAME KEYS, so the comparison is a dict diff and
nothing here knows what a field means. The port's half is
Demo_StateLine, src/lithium/lithium_state.c; this file reads the
demo's out of guest memory at the addresses below.
"""

import argparse
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GUEST = 0x1a50            # a Ghidra 1000:X is guest linear X + this

# img offsets, all inside the 0x8332..0x8fe2 span one dump covers.
# The slot arrays are indexed by the slot NUMBER as a byte offset, so
# slot 4 is +4 - docs/lithium/subsystems/objects.txt.
F_DWORD = (
    ('camx', 0x89c9), ('camy', 0x8d49), ('camz', 0x8a01),
    ('shipx', 0x89cd), ('shipy', 0x8d4d), ('shipz', 0x8a05),
    ('focus', 0x8ef6))
# THE ANGLES ARE THE MODEL'S, NOT THE LIVE ONES - the A set for the
# camera (g_SlotAngleAX/AY) and the B set for a flown craft
# (g_SlotPitch/g_SlotYaw). g_SlotAngleX/Y are rewritten twice a frame,
# by Obj_LoadOrientA and then by the decaying camera turn, so a
# snapshot taken mid-frame reads them from before that tick's
# Flight_Step and disagrees by one ease step with a model that
# matches exactly. src/lithium/lithium_state.c.
F_WORD = (
    ('campitch', 0x8af9), ('camyaw', 0x8b27),
    ('shippitch', 0x8b87), ('shipyaw', 0x8bb5),
    ('orgx', 0x8f5f), ('orgz', 0x8f61))
A_MUSIC_POS, A_CREDIT_PTR = 0x8332, 0x8fde
CREDIT_TABLE = 0x9089

# THE WHOLE FLIGHT PIPELINE for one slot, demo side only, for when a
# state line says a slot is wrong and the question is which stage of
# it. The order is the order the values flow in: the track fills the
# accumulators, the attitude chases them, the flown position follows
# the attitude and the drawn one trails that.
PIPE = (
    ('track', 0x8ddf, 4), ('ttick', 0x8e17, 1), ('cmd', 0x8d81, 1),
    ('thr', 0x8db0, 1), ('speed', 0x8e46, 1),
    ('pitch_acc', 0x8c0d, 2), ('yaw_acc', 0x8c3b, 2),
    ('pitch', 0x8b83, 2), ('yaw', 0x8bb1, 2), ('roll', 0x8bdf, 2),
    ('rollrest', 0x8a39, 2),
    ('a_x', 0x8af9, 2), ('a_y', 0x8b27, 2), ('a_z', 0x8b55, 2),
    ('ang_x', 0x8a6f, 2), ('ang_y', 0x8a9d, 2), ('ang_z', 0x8acb, 2),
    ('flight_x', 0x8959, 4), ('flight_y', 0x8c69, 4),
    ('flight_z', 0x8991, 4),
    ('home_x', 0x89c9, 4), ('home_y', 0x8d49, 4), ('home_z', 0x8a01, 4),
    ('pos_x', 0x88e9, 4), ('pos_y', 0x8cd9, 4), ('pos_z', 0x8921, 4))


def read_pipe(path, slot, base=None):
    """One slot's whole pipeline, out of a guest dump.

    THE SLOT NUMBER IS A BYTE OFFSET, not an index - the arrays are
    parallel and a four-byte field's slot 4 is +4, so the slots in
    use are 0, 4, 8 and 0xc and anything between straddles two
    entries. docs/lithium/subsystems/objects.txt.
    """
    d = open(path, 'rb').read()
    if base is None:
        base = A_MUSIC_POS + GUEST
    out = []
    for name, img, n in PIPE:
        i = img + GUEST - base + slot
        if i < 0 or i + n > len(d):
            out.append((name, None))
            continue
        if n == 4:
            v = struct.unpack_from('<i', d, i)[0]
        elif n == 2:
            v = s16(struct.unpack_from('<H', d, i)[0])
        else:
            v = d[i]
        out.append((name, v))
    return out

# What a difference in each field means, so the report can lead with
# the ones that matter. Position first: everything else follows it.
ORDER = ('t', 'ord', 'cr', 'camx', 'camy', 'camz', 'shipx', 'shipy',
         'shipz', 'campitch', 'camyaw', 'shippitch', 'shipyaw',
         'orgx', 'orgz', 'focus')
POSITION = ('camx', 'camy', 'camz', 'shipx', 'shipy', 'shipz')


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def read_demo(path, base=None):
    """The demo's fields out of a guest memory dump."""
    d = open(path, 'rb').read()
    if base is None:
        base = A_MUSIC_POS + GUEST
    out = {}

    def at(img, n):
        i = img + GUEST - base
        if i < 0 or i + n > len(d):
            raise SystemExit(
                'tools/statecmp.py: the dump does not reach img 0x%x. '
                'Take it as "dos_dump @%x 3256 FILE".'
                % (img, A_MUSIC_POS + GUEST))
        return d[i:i + n]

    for k, img in F_DWORD:
        out[k] = struct.unpack('<i', at(img, 4))[0]
    for k, img in F_WORD:
        out[k] = s16(struct.unpack('<H', at(img, 2))[0])
    out['ord'] = struct.unpack('<H', at(A_MUSIC_POS, 2))[0]
    ptr = struct.unpack('<H', at(A_CREDIT_PTR, 2))[0]
    out['cr'] = (ptr - CREDIT_TABLE) // 0x18 if ptr >= CREDIT_TABLE else -1
    return out


def demo_tick(path):
    """The tick, by asking the tool that already knows how."""
    r = subprocess.run([sys.executable, os.path.join(HERE,
                        'savestate_tick.py'), '--from-dump', path],
                       capture_output=True, text=True)
    for line in r.stdout.splitlines():
        if line.strip().startswith('tick ') and '->' in line:
            return int(line.split()[1])
    return None


def read_detail(path, slot):
    """{tick: fields} from our detail lines for one demo slot.

    A DETAIL LINE IS KEYED dt, NOT t, so read_port above skips them
    and this skips the state lines - src/host.h.
    """
    out = {}
    for line in open(path):
        if not line.startswith('dt='):
            continue
        f = {}
        for tok in line.split():
            k, _, v = tok.partition('=')
            try:
                f[k] = int(v)
            except ValueError:
                pass
        if f.get('slot') == slot and 'dt' in f:
            out[f['dt']] = f
    return out


def read_port(path):
    """{tick: fields} from one state line per tick."""
    out = {}
    for line in open(path):
        f = {}
        for tok in line.split():
            if '=' not in tok:
                continue
            k, _, v = tok.partition('=')
            try:
                f[k] = int(v)
            except ValueError:
                pass
        if 't' in f:
            out[f['t']] = f
    return out


def score(a, b):
    """How far apart two states are, on position alone.

    POSITION IS THE ONLY HONEST SCORE. An angle wraps and a counter is
    discrete, so either would swamp a sweep with false minima; the
    craft's place in the world does not.
    """
    n = 0
    for k in POSITION:
        if k in a and k in b:
            n += abs(a[k] - b[k])
    return n


def table(demo, port, t):
    print('%-10s %12s %12s %10s' % ('field', 'demo', 'port', 'delta'))
    for k in ORDER:
        if k not in demo and k not in port:
            continue
        dv = demo.get(k)
        pv = port.get(k)
        if dv is None or pv is None:
            print('%-10s %12s %12s %10s'
                  % (k, '-' if dv is None else dv,
                     '-' if pv is None else pv, '-'))
            continue
        d = pv - dv
        print('%-10s %12d %12d %+10d%s'
              % (k, dv, pv, d, '' if d == 0 else '  <-' if abs(d) > 200
                 else ''))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dump', help='a guest memory dump from @9d82')
    ap.add_argument('states', help='our own, from -states')
    ap.add_argument('--drift', action='store_true',
                    help='sweep our ticks and report the best offset')
    ap.add_argument('--span', type=int, default=60,
                    help='how far either side --drift looks')
    ap.add_argument('--at', type=int,
                    help='compare against this tick of ours instead')
    ap.add_argument('--base', type=lambda v: int(v, 0),
                    help='the guest address the dump starts at')
    ap.add_argument('--pipeline',
                    help='print the DEMO side whole flight pipeline for '
                         'these slots instead of comparing, as byte '
                         'offsets: "0,4". Use it when a state line '
                         'says a slot is wrong and the question is '
                         'which stage of it went wrong.')
    ap.add_argument('--pipecmp', type=lambda v: int(v, 0),
                    help='diff one slot pipeline against ours, by its '
                         'demo byte offset. Needs our side from '
                         '-detail rather than -states. THE FIRST '
                         'DIFFERING FIELD IS THE ONE THAT MATTERS: the '
                         'fields print in the order the values flow, so '
                         'everything below the first difference is its '
                         'consequence and only the top one names a '
                         'stage to go and read.')
    ap.add_argument('--demo-tick', type=int,
                    help='the demo tick, when dating cannot work - near '
                         'a scene start the credit roll has not begun, '
                         'so g_CreditTick plus the entry gives it instead')
    a = ap.parse_args()

    if a.pipeline:
        slots = [int(v, 0) for v in a.pipeline.split(',')]
        rows = [read_pipe(a.dump, n, a.base) for n in slots]
        print('%-10s %s' % ('field',
              ' '.join('%14s' % ('slot %d' % n) for n in slots)))
        for i, (name, _) in enumerate(rows[0]):
            print('%-10s %s' % (name, ' '.join(
                '%14s' % ('-' if r[i][1] is None else r[i][1])
                for r in rows)))
        return 0

    if a.pipecmp is not None:
        t = (a.demo_tick if a.demo_tick is not None
             else demo_tick(a.dump))
        if t is None:
            raise SystemExit('tools/statecmp.py: cannot date %s' % a.dump)
        at = a.at if a.at is not None else t
        mine = read_detail(a.states, a.pipecmp).get(at)
        if not mine:
            raise SystemExit('tools/statecmp.py: %s has no detail line '
                             'for tick %d slot %d - rerun with -detail'
                             % (a.states, at, a.pipecmp))
        print('%s is demo tick %d; ours at %d, slot %d'
              % (a.dump, t, at, a.pipecmp))
        print()
        print('%-10s %14s %14s %12s' % ('field', 'demo', 'port', 'delta'))
        first = None
        for name, dv in read_pipe(a.dump, a.pipecmp, a.base):
            pv = mine.get(name)
            if dv is None or pv is None:
                print('%-10s %14s %14s %12s'
                      % (name, '-' if dv is None else dv,
                         '-' if pv is None else pv, '-'))
                continue
            d = pv - dv
            if d and first is None:
                first = name
            print('%-10s %14d %14d %+12d%s'
                  % (name, dv, pv, d, '   <- FIRST' if name == first
                     else ('   <-' if d else '')))
        print()
        print('the first stage that differs is %s'
              % (first if first else 'nothing - the slot matches'))
        return 0

    demo = read_demo(a.dump, a.base)
    port = read_port(a.states)
    if not port:
        raise SystemExit('tools/statecmp.py: %s holds no state lines'
                         % a.states)
    t = a.demo_tick if a.demo_tick is not None else demo_tick(a.dump)
    if t is None:
        raise SystemExit('tools/statecmp.py: cannot date %s - run '
                         'savestate_tick.py on it to see why' % a.dump)
    demo['t'] = t
    print('%s is demo tick %d; %s covers %d..%d'
          % (a.dump, t, a.states, min(port), max(port)))

    if a.drift:
        best, rows = None, []
        for off in range(-a.span, a.span + 1):
            p = port.get(t + off)
            if not p:
                continue
            s = score(demo, p)
            rows.append((s, off))
            if best is None or s < best[0]:
                best = (s, off)
        if not rows:
            raise SystemExit('tools/statecmp.py: our table does not reach '
                             'tick %d - rerun -states with a bigger -n' % t)
        rows.sort()
        print()
        print('offset  position error')
        for s, off in rows[:6]:
            print('%+6d  %13d%s' % (off, s,
                  '   <- best' if off == best[1] else ''))

        near = [sc for sc, off in rows if abs(off - best[1]) == 1]
        edge = min(near) if near else 0
        print()
        if best[0] == 0:
            print('EXACT at offset %+d - every position field '
                  'agrees.' % best[1])
        elif edge > best[0] * 2 + 1:
            print('A SHARP minimum at %+d: %d against %d one tick '
                  'away.' % (best[1], best[0], edge))
        else:
            print('NO USABLE MINIMUM. The best is %d and one tick '
                  'away is %d, so no offset lines these up - the '
                  'states have diverged and the winning offset is '
                  'noise, not a phase.' % (best[0], edge))
        print('our tick %d against the demo %d is %+d out'
              % (t + best[1], t, -best[1]))
        print('at 0 offset the error is %d'
              % score(demo, port.get(t, {})))
        print()
        table(demo, port[t + best[1]], t + best[1])
        return 0

    at = a.at if a.at is not None else t
    if at not in port:
        raise SystemExit('tools/statecmp.py: no state line for tick %d' % at)
    print()
    table(demo, port[at], at)
    return 0


if __name__ == '__main__':
    sys.exit(main())
