"""Find dev-journal writing in the documents. CLAUDE.md rule 10.

    py -3.10 tools/journalcheck.py           report every hit
    py -3.10 tools/journalcheck.py --count   just the totals

Rule 10 says documents record WHAT THE CODE DOES, not the search that
found it. That rule has existed since the project started and has been
broken in every session anyway, which is why it needs a check rather
than another paragraph.

THE TEST A SENTENCE HAS TO PASS: does it describe the BINARY, or does
it describe the history of our work on the binary? A reader who has
never seen a previous session should not be able to tell that there
were any.

    banned      "this was named Music_TimerIsr for several sessions"
                "ANSWERED: g_ObjFaceAttr is a texture segment"
                "the ten-runner survey missed them"
                "that cost four wrong names"
    fine        "Music_TimerIsr is the whole engine, not a replayer"
                "g_ObjFaceAttr is a texture segment"
                "a text scene never calls Scene_ProjectVerts"

The two forms carry the same fact. The banned ones also carry us.

ANSWERED and RESOLVED get their own category because they are the most
common form here: an open question that has been answered should be
DELETED and its fact moved into the body, not annotated in place.
Rule 10, "correct in place; never append". The banned form is a PREFIX
on a surviving bullet, so that category requires the punctuation -
without it a wrapped prose line that merely opens with "resolved" is
reported, and a gate carrying a standing false positive stops getting
read.

WHAT IS STILL ALLOWED is rule 10's marked trap - one sentence saying
an approach looks right and is wrong, stated as a fact about the
problem. "The stub's INT 3 is a one-byte CALL, not a breakpoint" is a
fact. "I set a breakpoint there first" is a diary entry.

CLAUDE.md IS EXEMPT. A rule needs its rationale, and "this cost three
renames on the sister project" is what stops the next person undoing
it. That licence does not extend to docs/.
"""
import io
import os
import re
import sys

ROOTS = ['docs', 'src']

# THE EXEMPTION IS A PATH, NEVER A BASENAME. Only these three files
# quote the banned forms in order to ban them, and they are named in
# full for a reason: matching 'README.txt' by basename exempts every
# per-demo README as well - docs/lithium/README.txt among them, the
# longest document in the project - and nothing in the output says
# that a file went unchecked.
EXEMPT = (
    'CLAUDE.md',
    'docs/README.txt',
    'tools/README-commentbudget.txt',
)

PATTERNS = [
    ('answered-in-place', r'^\s*o?\s*(ANSWERED|RESOLVED)\s*[:-]'),
    ('our-sessions', r'(several|previous|earlier|another|this|last|one)\s+'
                     r'session|sessions\b'),
    ('naming-history', r'was (first )?(named|recorded|called|read)\b'
                       r'|were (first )?(named|recorded)\b'
                       r'|used to (say|carry|be)\b'
                       r'|stood here\b|no longer carr'),
    ('self-correction', r'\bA CORRECTION\b|was overstated\b'
                        r'|was wrong\b|got .{0,12}wrong\b'
                        r'|recorded backwards\b|recorded the other way\b'),
    ('what-it-cost', r'cost (a|one|two|three|four|five|several|the next)\b'
                     r'|threw away\b|wasted a\b'),
    ('first-person', r'\b(we|I) (first|then|now|had|have|were|was|found|'
                     r'checked|tried|missed|assumed|guessed)\b'),
    ('the-search', r'survey missed|was invisible|stayed invisible'
                   r'|turned out to be\b|it emerged\b|went looking\b'),
    ('how-we-found-it', r'what happened when|not a hypothetical'
                       r'|first transcribed|when this was'),
]
COMPILED = [(n, re.compile(p, re.I)) for n, p in PATTERNS]


def main(argv):
    count_only = '--count' in argv
    total, by_kind, files = 0, {}, 0
    for root in ROOTS:
        if not os.path.isdir(root):
            continue
        for dirpath, dirnames, names in os.walk(root):
            # third_party is vendored and not ours to lint.
            dirnames[:] = [d for d in dirnames
                           if d not in ('__pycache__', 'third_party')]
            for f in sorted(names):
                if not (f.endswith('.txt') or f.endswith('.c')
                        or f.endswith('.h')):
                    continue
                p = os.path.join(dirpath, f).replace(os.sep, '/')
                if p in EXEMPT:
                    continue
                hits = []
                for n, line in enumerate(io.open(p, encoding='ascii'), 1):
                    for kind, rx in COMPILED:
                        if rx.search(line):
                            hits.append((n, kind, line.rstrip()))
                            by_kind[kind] = by_kind.get(kind, 0) + 1
                            break
                if hits:
                    files += 1
                    total += len(hits)
                    if not count_only:
                        print('%s' % p)
                        for n, kind, line in hits:
                            print('  %4d  %-18s %s'
                                  % (n, kind, line.strip()[:70]))
    print('')
    print('%d journal-style line(s) in %d file(s)' % (total, files))
    for kind, n in sorted(by_kind.items(), key=lambda kv: -kv[1]):
        print('   %-18s %d' % (kind, n))
    if total:
        print('')
        print('Rewrite each as a fact about the binary, or delete it.')
        print('An ANSWERED open question should be removed and its fact')
        print('moved into the body - rule 10, correct in place.')
    return 1 if total else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
