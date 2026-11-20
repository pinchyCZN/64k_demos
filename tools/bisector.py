"""Bisect the tick where the reconstruction stops matching the original.

NOT NAMED bisect.py. Python puts a script's own directory first on
sys.path, so a tools/bisect.py shadows the standard library's bisect
for every other script in here - and what breaks is not this file but
random, which imports it, so the traceback names neither.

OURS IN FULL. It keeps the bracket and tells the driver where to probe
next; it does not drive the emulator itself, because the debugger's
pipe takes ONE CLIENT and an MCP server already holds it - see
tools/savestate_tick.py.

    py -3.10 tools/bisector.py start --lo 2335 --hi 3750 --lo-slot 3
    py -3.10 tools/bisector.py next
    ... follow what it prints, then:
    py -3.10 tools/bisector.py tell build/p.bin --slot 9

A PROBE IS A SNAPSHOT, which is what makes this cheap. Running the
demo forward is one-way and slow - the transition alone is 700
emulator frames for 40 ticks - so every probe is saved to a scratch
slot and the next one resumes from the nearest checkpoint below it
instead of from the start.

EXACT MEANS EXACT. A probe passes only when every position field
agrees at the known offset, which is the state the scene really is in
for its first twelve hundred ticks - docs/lithium/README.txt section
11.2. Anything else is a fail, so the bracket closes on the FIRST
tick that stopped agreeing rather than on a threshold someone chose.

THE SCRATCH SLOTS ARE OVERWRITTEN, and the debugger only SAVES to
slots 0 to 6 even though it loads higher ones. Pass --pool to say
which to use; the default is the two the flyover work has superseded.
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import statecmp                                    # noqa: E402

STATE = os.path.join(HERE, '..', 'build', 'bisect.json')
DUMP_CMD = 'dos_dump @9d82 3256 build/p.bin'

# Measured in the flyover: the demo renders about three frames for
# every two ticks, so this converts a tick target into a frame count.
# It only has to be close - the probe is DATED when it lands, and the
# bracket narrows on where it actually fell.
FRAMES_PER_TICK = 1.4


def load():
    if not os.path.exists(STATE):
        raise SystemExit('tools/bisector.py: no bisection in progress - '
                         'run "start" first')
    with open(STATE) as fh:
        return json.load(fh)


def save(st):
    with open(STATE, 'w') as fh:
        json.dump(st, fh, indent=1, sort_keys=True)


def verdict(st, dump):
    """(tick, error) for a probe, through statecmp's own definition."""
    demo = statecmp.read_demo(dump)
    t = statecmp.demo_tick(dump)
    if t is None:
        raise SystemExit('tools/bisector.py: cannot date %s - run '
                         'savestate_tick.py on it to see why' % dump)
    port = statecmp.read_port(st['states'])
    mine = port.get(t + st['offset'])
    if not mine:
        raise SystemExit('tools/bisector.py: our table has no tick %d - '
                         'rerun -states with a bigger -n'
                         % (t + st['offset']))
    return t, statecmp.score(demo, mine)


def resume_from(st, target):
    """The checkpoint to start a probe from: the nearest at or below."""
    best = None
    for tick, slot in st['checkpoints'].items():
        tick = int(tick)
        if tick <= target and (best is None or tick > best[0]):
            best = (tick, slot)
    return best


def show_next(st):
    lo, hi = st['lo'], st['hi']
    if hi - lo <= 1:
        print('DONE. The last tick that agreed is %d and the first that '
              'did not is %d.' % (lo, hi))
        print('Compare them field by field:')
        print('  py -3.10 tools/statecmp.py build/p.bin %s --at %d'
              % (st['states'], hi + st['offset']))
        return
    mid = (lo + hi) // 2
    base = resume_from(st, mid)
    print('bracket %d (agrees) .. %d (does not), %d ticks apart'
          % (lo, hi, hi - lo))
    print()
    print('probe demo tick %d:' % mid)
    if base:
        print('  1. dos_snap_load %s          (checkpoint at tick %d)'
              % (base[1], base[0]))
        print('  2. dos_frames %d                 (about %d ticks)'
              % (int((mid - base[0]) * FRAMES_PER_TICK), mid - base[0]))
    else:
        print('  1. no checkpoint at or below %d - load the earliest '
              'state you have' % mid)
        print('  2. dos_frames enough to reach tick %d' % mid)
    # NEVER OVERWRITE THE SLOT WE WOULD RESUME FROM. If this probe
    # diverges, the next one starts at the CURRENT lo - so reusing its
    # slot for the probe loses the only checkpoint below the bracket
    # and the run falls back to replaying from the beginning.
    keep = base[1] if base else None
    free = [x for x in st['pool'] if x != keep]
    if not free:
        print('  (every slot in the pool holds the checkpoint we would')
        print('   resume from - pass a bigger --pool)')
        free = st['pool']
    nxt = free[len(st['log']) % len(free)]
    print('  3. dos_snap_save %s          (OVERWRITES it, so the next'
          % nxt)
    print('                               probe can resume from here)')
    print('  4. %s' % DUMP_CMD)
    print('  5. py -3.10 tools/bisector.py tell build/p.bin --slot %s' % nxt)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd')

    s = sub.add_parser('start')
    s.add_argument('--lo', type=int, required=True,
                   help='a demo tick that AGREES')
    s.add_argument('--hi', type=int, required=True,
                   help='a demo tick that does NOT')
    s.add_argument('--offset', type=int, default=17,
                   help='our tick minus the demo tick')
    s.add_argument('--states', default='build/states.txt')
    s.add_argument('--lo-slot', help='the slot holding the --lo tick')
    s.add_argument('--pool', default='5,4',
                   help='scratch slots to overwrite, comma separated;'
                        ' the debugger only saves to 0-6')

    sub.add_parser('next')
    sub.add_parser('status')

    t = sub.add_parser('tell')
    t.add_argument('dump')
    t.add_argument('--slot', help='the slot this probe was saved to')

    a = ap.parse_args()

    if a.cmd == 'start':
        st = {'lo': a.lo, 'hi': a.hi, 'offset': a.offset,
              'states': a.states, 'pool': a.pool.split(','),
              'checkpoints': {}, 'log': []}
        if a.lo_slot:
            st['checkpoints'][str(a.lo)] = a.lo_slot
        save(st)
        print('bracket %d .. %d at offset %+d'
              % (a.lo, a.hi, a.offset))
        print()
        show_next(st)
        return 0

    if a.cmd in (None, 'next'):
        show_next(load())
        return 0

    if a.cmd == 'status':
        st = load()
        print('bracket %d .. %d, offset %+d, %d probe(s)'
              % (st['lo'], st['hi'], st['offset'], len(st['log'])))
        for tick, slot in sorted(st['checkpoints'].items(),
                                 key=lambda kv: int(kv[0])):
            print('  checkpoint tick %-6s slot %s' % (tick, slot))
        for row in st['log']:
            print('  probe tick %-6d error %-10d %s'
                  % (row['tick'], row['error'], row['verdict']))
        return 0

    if a.cmd == 'tell':
        st = load()
        tick, err = verdict(st, a.dump)
        ok = err == 0
        st['log'].append({'tick': tick, 'error': err,
                          'verdict': 'agrees' if ok else 'diverged'})
        if a.slot:
            # A SLOT HOLDS ONE TICK. Saving over it has to drop the
            # tick that used to claim it, or resume_from will send the
            # next probe to a checkpoint that is no longer there - and
            # it would start from the wrong state without saying so.
            for k in [k for k, v in st['checkpoints'].items()
                      if v == a.slot]:
                del st['checkpoints'][k]
            st['checkpoints'][str(tick)] = a.slot
        print('probe landed on demo tick %d, position error %d - %s'
              % (tick, err, 'AGREES' if ok else 'DIVERGED'))
        if not st['lo'] < tick < st['hi']:
            print('It is outside the bracket %d..%d, so it narrows '
                  'nothing - the bracket stands.' % (st['lo'], st['hi']))
        elif ok:
            st['lo'] = tick
        else:
            st['hi'] = tick
        save(st)
        print()
        show_next(st)
        return 0

    ap.print_help()
    return 1


if __name__ == '__main__':
    sys.exit(main())
