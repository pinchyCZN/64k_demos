"""Run a demo's sim.

    py -3.10 sim/run.py xcursion            number keys pick the part
    py -3.10 sim/run.py xcursion --aspect   640x480, the CRT shape
    py -3.10 sim/run.py xcursion --list     every part, numbered
    py -3.10 sim/run.py lithium             the models and the tables
    py -3.10 sim/run.py ukko                the stick figure and its sets

The harness is shared; the parts are not. See docs/layout.txt.
"""

import importlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import harness                                          # noqa: E402

TITLES = {'xcursion': 'EXCURSION parts', 'lithium': 'LITHIUM parts',
          'ukko': "StickMan's World"}


def main(argv):
    if not argv or argv[0].startswith('--'):
        print(__doc__)
        print('known demos: %s' % ', '.join(sorted(TITLES)))
        return 2
    name = argv[0]
    try:
        parts = importlib.import_module('%s.parts' % name)
        demo = importlib.import_module('%s.demo' % name)
    except ImportError as e:
        print('no sim for %r: %s' % (name, e))
        return 2
    harness.run(parts, demo.Demo, TITLES.get(name, name), argv[1:])
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
