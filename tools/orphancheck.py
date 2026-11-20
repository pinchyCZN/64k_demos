"""Find ORPHANED BLOCK COMMENTS in the reconstruction's own sources.

A comment that documents a function has to sit immediately above it.
Inserting a new function by anchoring on an existing declaration puts
the new code between that declaration and ITS comment, and the comment
is then attached to the wrong thing - or to nothing. It reads as
correct, which is why it needs a check rather than care.

The signal is two column-0 block comments in a row with no declaration
between them. In our own files that is always a mistake; in
third_party it is ordinary style, so those are skipped, and so is a
one-line SECTION BANNER - a rule of dashes with a label - which is
meant to sit above a comment.

    py -3.10 tools/orphancheck.py          every hit, with its head
    py -3.10 tools/orphancheck.py --count  just the total
"""

import os
import sys

ROOTS = (os.path.join('src', 'xcursion'), 'src',
         os.path.join('sim', 'xcursion'))
SKIP = ('third_party',)
EXTS = ('.c', '.h')


def is_banner(line):
    """A one-line rule of dashes with a label, not a documenting
    comment - `/* ------ maths */`."""
    if '*/' not in line:
        return False
    body = line[2:line.rindex('*/')]
    return body.count('-') + body.count('=') >= 8


def scan(path):
    """Return [(line_number, head)] for each orphan in one file."""
    try:
        text = open(path, encoding='ascii').read()
    except (UnicodeDecodeError, OSError):
        return []
    lines = text.split('\n')
    out = []
    i = 0
    while i < len(lines):
        if not lines[i].startswith('/*'):
            i += 1
            continue
        end = i
        while end < len(lines) and '*/' not in lines[end]:
            end += 1
        nxt = end + 1
        while nxt < len(lines) and lines[nxt].strip() == '':
            nxt += 1
        if (nxt < len(lines) and lines[nxt].startswith('/*')
                and not is_banner(lines[i])):
            out.append((i + 1, lines[i][:58]))
        i = end + 1
    return out


def files():
    seen = {}
    for root in ROOTS:
        if not os.path.isdir(root):
            continue
        for dirpath, dirnames, names in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in SKIP]
            for n in sorted(names):
                if n.endswith(EXTS):
                    p = os.path.join(dirpath, n)
                    seen[os.path.normpath(p)] = True
    return sorted(seen)


def main(argv):
    quiet = '--count' in argv
    total = 0
    for p in files():
        for line, head in scan(p):
            total += 1
            if not quiet:
                print('%s:%d' % (p.replace(os.sep, '/'), line))
                print('    %s' % head)
    if total:
        print('%d orphaned comment(s). Move each one back above the '
              'thing it describes,' % total)
        print('or delete it if what it described is gone.')
        return 1
    print('0 orphaned comment(s).')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
