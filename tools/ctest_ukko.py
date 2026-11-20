# -*- coding: ascii -*-
"""Diff the C port against the Python sim, frame for frame.

    py -3.10 tools/ctest_ukko.py             the default sample
    py -3.10 tools/ctest_ukko.py -n 40       more frames
    py -3.10 tools/ctest_ukko.py -from 430   start elsewhere

THE SIM IS THE REFERENCE because it was checked against the listing
first. A disagreement is a defect in one of them and the point of
this is to say which frame, not merely that they differ.

It finds the NEWEST ukko_ppm under build/ and warns when several
exist - Win32 and x64, Debug and Release all land in that tree and
running yesterday's is the easiest way to chase a fixed bug.
"""

import io
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRATCH = os.environ.get('TEMP', '.')


def find_exe():
    hits = []
    for base in (os.path.join(ROOT, 'build'),):
        for dirpath, _dirnames, names in os.walk(base):
            for f in names:
                if f.lower() == 'ukko_ppm.exe':
                    p = os.path.join(dirpath, f)
                    hits.append((os.path.getmtime(p), p))
    if not hits:
        sys.exit('no ukko_ppm.exe under build/ - build the solution first')
    hits.sort()
    if len(hits) > 1:
        print('note: %d copies of ukko_ppm.exe, using the newest'
              % len(hits))
    return hits[-1][1]


def sim_frames(first, count):
    """The sim's framebuffer for the same display frames."""
    sys.path.insert(0, os.path.join(ROOT, 'sim'))
    from ukko import demo as D

    class FS(object):
        def __init__(self):
            self.fb = bytearray(320 * 200)

        def blit(self):
            pass

        def clear(self, c):
            pass

        def set_palette(self, p):
            pass

    d = D.Demo(FS())
    out = []
    for n in range(first + count):
        d.step()
        if n >= first:
            out.append(bytes(d.s.fb))
    return out


def ppm_pixels(path):
    d = io.open(path, 'rb').read()
    i = d.index(b'255') + 4
    px = d[i:]
    # Back to indices: the host wrote pal[i]*255/63 three times, and
    # this demo only ever uses 0x00 and 0xff.
    return bytes(0xff if px[k * 3] else 0 for k in range(320 * 200))


def main(argv):
    count, first = 12, 0
    for i, a in enumerate(argv):
        if a == '-n' and i + 1 < len(argv):
            count = int(argv[i + 1])
        elif a == '-from' and i + 1 < len(argv):
            first = int(argv[i + 1])

    exe = find_exe()
    out = os.path.join(SCRATCH, 'ukko_ctest')
    if not os.path.isdir(out):
        os.makedirs(out)
    print('%s\n  %d frames from %d' % (exe, count, first))

    r = subprocess.call([exe, '-n', str(count), '-skip', str(first),
                         '-o', out], cwd=ROOT)
    if r != 0:
        sys.exit('ukko_ppm failed')

    want = sim_frames(first, count)
    bad = 0
    for k in range(count):
        p = os.path.join(out, 'f%05d.ppm' % (first + k))
        if not os.path.exists(p):
            print('  frame %5d  MISSING' % (first + k))
            bad += 1
            continue
        got = ppm_pixels(p)
        if got == want[k]:
            continue
        diff = sum(1 for i in range(len(got)) if got[i] != want[k][i])
        print('  frame %5d  DIFFERS in %d of 64000 pixels'
              % (first + k, diff))
        bad += 1
    if bad:
        print('%d of %d frames differ' % (bad, count))
        return 1
    print('all %d frames identical to the sim' % count)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
