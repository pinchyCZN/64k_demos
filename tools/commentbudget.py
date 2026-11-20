#!/usr/bin/env python3
"""Comment budget for hand-written code. See README-commentbudget.txt.

    python tools/commentbudget.py            the working tree diff
    python tools/commentbudget.py --reduce   a REDUCTION pass: comment
                                             must go DOWN
    python tools/commentbudget.py --blocks   every oversized comment block
    python tools/commentbudget.py --inline   only the in-body rule

THE IN-BODY RULE IS ABSOLUTE AND IS NOT A BUDGET. A comment indented
inside the body of a function we WROTE - anything under tools/, and any
src/ or sim/ function whose header says OURS - is refused however short
it is and however much code sits beside it. A three-line fix carrying a
paragraph is the failure this exists to stop, and a ratio cannot catch
it: the paragraph is legal as soon as the fix is long enough.

Where the explanation goes instead is the function's OWN header comment,
or docs/. Two things are exempt, both because they cannot live anywhere
else: a provenance marker (ORIGIN, OURS), and the body of a
TRANSCRIBED function - one whose header carries ORIGIN - where a note
about the original's arithmetic is pinned to the line it describes.

THE DEFAULT GATE PERMITS GROWTH AND THAT IS DELIBERATE: net comment may
not exceed net code, so a change that adds code may explain it. It is
the wrong gate for a pass whose whole purpose is to shrink the comment -
under it a sweep can add code and spend the allowance it just earned.
--reduce requires the net to be NEGATIVE and ignores the code delta.
"""

import argparse
import os
import re
import subprocess
import sys

MAX_BLOCK = 12          # lines in one /* */ or // run
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)                       # repo root
CODE_EXT = ('.c', '.h', '.cpp', '.py')

# Hand-written code. Everything outside these roots is generated data,
# third-party, or build output, and a comment budget means nothing there.
#
# sim/ is the Python reconstruction and is under the budget for the same
# reason src/ is: it is a transcription of the binary, so a comment there
# is competing with docs/ for the same explanation. tools/ is NOT - a
# tool's docstring is its only documentation and has nowhere else to go.
SRC_ROOTS = ('src', 'sim')

# Not code we author: generated tables, vendored headers, build output.
SKIP = ('src/third_party/', 'src/generated/', 'sdk/', 'reverse/',
        'decomp/', 'tools/', 'build/')


def ours(path):
    p = path.replace('\\', '/')
    if not p.endswith(CODE_EXT):
        return False
    return not p.startswith(SKIP)


def is_comment(line):
    """A comment line in C or Python.

    A BARE LEADING STAR IS NOT ENOUGH to call a line a comment.
    "*ou = x;" is a dereference and "*p++ = c;" is a copy loop, while
    a block comment's continuation is "* " or a star alone. Counting
    the star on its own read every pointer store in a transcribed
    function as comment.

    A LINE WITH CODE ON IT IS CODE, whatever trails it. The house
    convention puts a provenance marker after the statement it
    belongs to - "x = 1;  /* img 0x1234 */" - and reading that as a
    comment line cost the statement its place in the code count as
    well, so one marker moved the balance by two. What comment such a
    line carries is comment_part's question, not this one.

    A Python docstring is not detected and cannot be: its lines are
    indistinguishable from a multi-line string by shape alone, and the
    budget counts lines rather than parsing. So the gate UNDER-counts
    Python comment, which --blocks partly covers by flagging long runs.
    """
    t = line.strip()
    if t.startswith('#'):
        return True
    if t.startswith(('/*', '//', '*/')):
        return True
    if '/*' in t:
        return False            # code first, then a comment
    return t.endswith('*/') or t == '*' or t.startswith('* ')


def comment_part(line):
    """The comment a line carries, or '' - what the in-body rule judges.

    It has to see what is_comment deliberately does not: a statement
    with a marker after it carries a comment even though the line
    counts as code. Without this the in-body rule would go blind to
    the one place an explanation is easiest to smuggle in.
    """
    t = line.strip()
    if t.startswith(('#', '//')):
        return t
    i = t.find('/*')
    if i > 0:
        return t[i:]
    return t if is_comment(line) else ''


def marks(t, mark):
    """Does this comment line OPEN with the given marker?

    IT HAS TO END ON A WORD BOUNDARY. "ORIGINAL" opens with "ORIGIN",
    and a prose header using the word - "not in the ORIGINAL" - read
    as a transcription marker and exempted that whole function's body
    from the in-body rule. A marker is followed by punctuation or a
    space, never by more of a word.
    """
    for pre in ('/* ', '* ', '# '):
        if t.startswith(pre + mark):
            rest = t[len(pre) + len(mark):]
            return not rest or not (rest[0].isalnum() or rest[0] == '_')
    return False


def is_provenance(t):
    """An ORIGIN or OURS line is METADATA, not commentary.

    Rule 18 REQUIRES one on every function definition in src/, and
    tools/commentcheck.py fails the build without it. A line the
    project mandates cannot also be penalised for existing, and it
    is the one comment that by definition cannot move to docs/ -
    its whole purpose is to sit against the code.

    Only the marker's OWN line is exempt. Prose continuing under it
    counts normally, so nothing can hide behind the marker.
    """
    return marks(t, 'ORIGIN') or marks(t, 'OURS')


# The in-body rule reaches further than the budget does: a tool's
# DOCSTRING has nowhere else to go and is exempt by being unindented,
# but a comment inside a tool's function is commentary like any other.
INLINE_SKIP = ('src/third_party/', 'src/generated/', 'sdk/', 'reverse/',
               'decomp/', 'build/',
               'tools/commentbudget.py')


def authored(path):
    p = path.replace('\\', '/')
    return p.endswith(CODE_EXT) and not p.startswith(INLINE_SKIP)


def added_with_lines():
    """[(path, line number in the new file, text)] for added lines."""
    out = subprocess.run(['git', 'diff', '-U0'], cwd=ROOT,
                         capture_output=True, text=True).stdout
    path, n, res = None, 0, []
    for line in out.splitlines():
        if line.startswith('+++ b/'):
            path = line[6:]
        elif line.startswith('@@'):
            m = re.search(r'\+(\d+)', line)
            n = int(m.group(1)) if m else 0
        elif line.startswith('+') and not line.startswith('+++'):
            if path and authored(path):
                res.append((path, n, line[1:]))
            n += 1
    return res


def py_marker(lines, idx):
    """The same answer for Python, where the marker sits BELOW the def.

    Rule 15 puts ORIGIN in the routine's DOCSTRING, so the C walk -
    which reads the comment block above the definition - never finds
    it, and every line of every transcription is reported instead.
    Here the walk goes up to the def and then forward into the
    docstring.
    """
    q1, q2 = '"' * 3, "'" * 3
    i = min(idx, len(lines) - 1)
    while i >= 0 and not lines[i].lstrip().startswith(('def ', 'class ')):
        i -= 1
    if i < 0:
        return None
    j, quote = i + 1, None
    while j < len(lines) and j < i + 4:
        t = lines[j].strip()
        if t.startswith((q1, q2)):
            quote = t[:3]
            break
        if t:
            return None                 # code before any docstring
        j += 1
    if quote is None:
        return None
    body = [lines[j].strip()[3:]]
    if not body[0].endswith(quote):
        j += 1
        while j < len(lines):
            body.append(lines[j])
            if quote in lines[j]:
                break
            j += 1
    # NOT marks(), WHICH WANTS A COMMENT PREFIX. A docstring has no
    # leading # or *, so that test never fires on one. Rule 15 says
    # the docstring OPENS with the marker, and that is the test.
    head = ''
    for t in body:
        if t.strip():
            head = t.strip()
            break
    for mark in ('ORIGIN', 'OURS'):
        if head.startswith(mark):
            rest = head[len(mark):]
            if not rest or not (rest[0].isalnum() or rest[0] == '_'):
                return mark
    return None


def enclosing_marker(lines, idx):
    """'ORIGIN' or 'OURS' for the function containing line idx, else None.

    IT HAS TO FIND THE DEFINITION FIRST. Rule 18 puts the marker on
    the function header, but a transcribed body may carry ORIGIN
    comments of its own against individual lines, so the nearest
    marker ABOVE a line is often one of those rather than the
    function's. Walking up to the definition - column zero, not a
    comment - and reading the block above THAT is what distinguishes
    a function we wrote from one we transcribed.

    The walk lands on the lone { that opens a C body in this house
    style, so the signature is one line further up again.

    A SIGNATURE THAT WRAPS IS THE CASE TO GET RIGHT. Only its FIRST
    line reaches column zero; the rest are indented to line up under
    the open bracket, so stepping off the { lands in the middle of
    the parameter list and the header above is never reached. Every
    comment in such a function's body then looks like one in a
    function we wrote, which is the wrong half of the rule.
    """
    i = min(idx, len(lines) - 1)
    while i >= 0:
        l = lines[i]
        if (l.strip() and not l[:1].isspace()
                and not l.lstrip().startswith(('/*', '*', '//', '#'))):
            break
        i -= 1
    while i >= 0 and lines[i].strip() == '{':
        i -= 1
    while (i > 0 and lines[i].strip() and lines[i][:1].isspace()
           and not is_comment(lines[i])):
        i -= 1
    j = i - 1
    while j >= 0 and not lines[j].strip():
        j -= 1
    marker = None
    while j >= 0 and is_comment(lines[j]):
        t = lines[j].strip()
        for mark in ('ORIGIN', 'OURS'):
            if marks(t, mark):
                marker = mark
        j -= 1
    return marker


def in_header_block(lines, idx):
    """Is this comment line part of a block that starts at column 0?

    A C header block opens at column 0 and every line after it begins
    " *", one space in, so indentation alone cannot tell a header's
    continuation from a comment inside a body. The run's FIRST line
    can: if it reaches column 0 the whole run is a header.
    """
    i = min(idx, len(lines) - 1)
    while i >= 0 and is_comment(lines[i]):
        if lines[i].strip() and not lines[i][:1].isspace():
            return True
        i -= 1
    return False


def all_with_lines():
    """Every line of every file we wrote, in added_with_lines' shape.

    added_with_lines reads `git diff`, so the in-body rule has only
    ever been applied to lines being ADDED. On a clean tree it yields
    nothing and the check passes without having looked at a single
    committed comment.
    """
    for rel, text in walk_ours():
        for n, line in enumerate(text.splitlines(), 1):
            yield rel, n, line


def report_inline(scan_all=False):
    bad = 0
    cache = {}
    for path, n, text in (all_with_lines() if scan_all
                          else added_with_lines()):
        if not text[:1].isspace():
            continue
        part = comment_part(text)
        if not part or is_provenance(part):
            continue
        if path not in cache:
            full = os.path.join(ROOT, path)
            try:
                with open(full, encoding='utf-8', errors='replace') as fh:
                    cache[path] = fh.read().splitlines()
            except OSError:
                cache[path] = []
        if in_header_block(cache[path], n - 1):
            continue
        find = py_marker if path.endswith('.py') else enclosing_marker
        if find(cache[path], n - 1) == 'ORIGIN':
            continue
        print('%s:%d  in-body comment: %s' % (path, n, text.strip()))
        bad += 1
    if bad:
        print('\n%d in-body comment line(s) in code we wrote. Put it in '
              'the function header or in docs/, or delete it.' % bad)
    return bad


def diff_lines():
    """{path: (added, removed)} for hand-written code in the working tree."""
    return parse_diff(subprocess.run(['git', 'diff', '-U0'], cwd=ROOT,
                                     capture_output=True,
                                     text=True).stdout)


def parse_diff(out):
    """{path: (added, removed)} from a unified diff. Split out so the
    selftest can feed it a diff git is not holding."""
    path, d = None, {}
    for line in out.splitlines():
        # EVERY '+++ ' LINE IS A FILE HEADER, including the
        # '+++ /dev/null' a DELETED file gets. Matching only '+++ b/'
        # leaves path on the previous file, and that file is then
        # charged with every line of the deleted one - which reads as
        # a huge code reduction and lets a real overspend through.
        if line.startswith('+++ '):
            name = line[6:] if line.startswith('+++ b/') else ''
            path = name if name and ours(name) else None
            if path:
                d.setdefault(path, ([], []))
        elif not path:
            continue
        elif line.startswith('+') and not line.startswith('+++'):
            d[path][0].append(line[1:])
        elif line.startswith('-') and not line.startswith('---'):
            d[path][1].append(line[1:])
    return d


def blocks(text):
    """Yield (start_line, length) for each run of comment lines."""
    run = 0
    start = 0
    for n, line in enumerate(text.splitlines(), 1):
        if is_comment(line):
            if not run:
                start = n
            run += 1
        else:
            if run:
                yield start, run
            run = 0
    if run:
        yield start, run


def counts(lines):
    """Comment and code line counts.

    A PROVENANCE MARKER IS NEITHER. Counting it as comment penalises
    a line rule 18 requires; counting it as code would let someone
    buy comment budget by adding markers. It is metadata and sits
    outside the ledger entirely.
    """
    body = [l for l in lines if not is_provenance(l.strip())]
    c = sum(1 for l in body if is_comment(l))
    k = sum(1 for l in body if l.strip() and not is_comment(l))
    return c, k


def report_diff(reduce_mode=False):
    bad = 0
    tc = tk = 0
    touched = 0            # files whose comments the diff actually moves
    for path, (add, rem) in sorted(diff_lines().items()):
        if not path.endswith(CODE_EXT):
            continue
        ac, ak = counts(add)
        rc, rk = counts(rem)
        # NET, so replacing a long comment with a short one counts as
        # the reduction it is rather than the addition it contains.
        c, k = ac - rc, ak - rk
        tc += c
        tk += k
        if not (ac or ak or rc or rk):
            continue
        if ac or rc:
            touched += 1
        if reduce_mode:
            # A file whose comments were not touched at all is neutral -
            # a reduction pass is allowed to carry an unrelated code fix.
            over = (ac or rc) and c >= 0
            flag = '  GREW' if over else ''
        else:
            over = c > max(k, 0)
            flag = '  OVER' if over else ''
        print('%-18s comment %+4d  code %+4d%s' % (path, c, k, flag))
        if over:
            # A HEADER IS MEASURED ON THE TOTAL, NOT ON ITSELF.
            #
            # In a .c the code is the thing and the comment explains
            # it, so comment > code means the explanation outgrew the
            # work. In a .h the code is a SIGNATURE and the comment is
            # the CONTRACT - what a caller must know to use the thing
            # correctly. One line of signature legitimately carries
            # several of contract, and holding a header to the .c
            # ratio asks it to under-document its own interface.
            #
            # The total check below still counts headers, so one
            # cannot become a dumping ground.
            if path.endswith('.h'):
                flag = '  over (header, advisory)'
                print('%-18s   ^ counted in the total only' % '')
            else:
                bad += 1
    if tc or tk:
        print('%-18s comment %+4d  code %+4d' % ('TOTAL', tc, tk))
    if reduce_mode:
        if touched and tc >= 0:
            print('\na reduction pass must REMOVE comment and this one is '
                  '%+d - shorten a block or move it to docs/' % tc)
            bad += 1
    elif tc > max(tk, 0):
        print('\nthis change adds more comment than code - move the '
              'explanation to docs/ and leave a one-line pointer')
        bad += 1
    return bad


def walk_ours():
    """Every hand-written source file under SRC_ROOTS, recursively.

    IT HAS TO RECURSE. Listing a root's top level alone reaches
    src/*.c and sim/*.py and NOTHING ELSE - so src/lithium/,
    src/xcursion/, src/ukko/ and every sim/<demo>/ go unvisited,
    which is the whole of the port. A walker that covers the hosts
    and none of the demos reports a clean tree in the one place a
    clean tree would be a surprise.
    """
    for sub in SRC_ROOTS:
        base = os.path.normpath(os.path.join(ROOT, sub))
        if not os.path.isdir(base):
            continue
        for dirpath, dirnames, names in os.walk(base):
            dirnames[:] = sorted(d for d in dirnames
                                 if d not in ('__pycache__', 'third_party'))
            for f in sorted(names):
                full = os.path.join(dirpath, f)
                rel = os.path.relpath(full, ROOT).replace(chr(92), '/')
                if not ours(rel):
                    continue
                try:
                    with open(full, encoding='utf-8',
                              errors='replace') as fh:
                        yield rel, fh.read()
                except OSError:
                    continue


def report_blocks():
    n = 0
    for rel, text in walk_ours():
        for start, length in blocks(text):
            if length > MAX_BLOCK:
                print('%s:%d  %d-line block' % (rel, start, length))
                n += 1
    if n:
        print('\n%d block(s) over %d lines' % (n, MAX_BLOCK))
    return 0            # advisory: the tree is full of them already


# THE FIXTURE'S COMMENT MARKERS ARE ASSEMBLED, NOT WRITTEN. Spelt out,
# its four comment-shaped lines are indistinguishable from real ones in
# a diff and the rule flags this tool's own test data.
_C, _E = '/' + '*', '*' + '/'
# A DIFF WITH A DELETED FILE AFTER A TOUCHED ONE, which is the
# shape that charged one file with the whole length of another.
TEST_DIFF = '\n'.join((
    'diff --git a/src/a.c b/src/a.c',
    '--- a/src/a.c',
    '+++ b/src/a.c',
    '@@ -1,0 +2 @@',
    '+    x = 1;',
    'diff --git a/src/gone.c b/src/gone.c',
    'deleted file mode 100644',
    '--- a/src/gone.c',
    '+++ /dev/null',
    '@@ -1,2 +0,0 @@',
    '-    y = 2;',
    '-    z = 3;',
))


TEST_C = '\n'.join((
    '%s OURS. A thing we wrote. %s' % (_C, _E),
    'static void ours_fn(void)',
    '{',
    '    %s an explanation that should not be here %s' % (_C, _E),
    '    x = 1;',
    '    x = 2;  %s and neither should this %s' % (_C, _E),
    '}',
    '',
    '%s ORIGIN 0x00123456 Some_DemoRoutine - what the original does,' % _C,
    ' * at length, because this is a transcription. %s' % _E,
    'static void transcribed_fn(void)',
    '{',
    '    %s the original shifts first, which matters %s' % (_C, _E),
    '    y = 2;',
    '}',
    '',
    '%s ORIGIN 0x00234567 Wrapped_DemoRoutine - and its signature' % _C,
    ' * wraps, which used to hide this header from the walk. %s' % _E,
    'static void wrapped_fn(int a_long_parameter_name,',
    '                       int another_one,',
    '                       int a_third)',
    '{',
    '    %s the original rounds toward zero here %s' % (_C, _E),
    '    z = 3;',
    '    z = 4;  %s img 0x00234599 %s' % (_C, _E),
    '}',
    ''))


def audit():
    """Comment against code, per file, over the whole tree.

    THE BUDGET IS A DELTA AND THAT IS CORRECT - it asks whether a
    CHANGE carries more comment than code. What it cannot answer is
    where the tree already stands, which is the question a trim
    starts from, so this counts the committed state instead and
    sorts by what a trim would recover.
    """
    rows = []
    for rel, text in walk_ours():
        lines = text.splitlines()
        com, code = counts(lines)
        if com + code:
            rows.append((com, code, rel))
    rows.sort(reverse=True)
    tc = sum(r[0] for r in rows)
    tk = sum(r[1] for r in rows)
    print('  comment   code   pct  file')
    for com, code, rel in rows:
        print('  %7d %6d  %3d%%  %s'
              % (com, code, com * 100 // max(1, com + code), rel))
    print('\n  %d file(s), comment %d, code %d - %d%% of the tree is comment'
          % (len(rows), tc, tk, tc * 100 // max(1, tc + tk)))
    return 0            # a measurement, never a gate


def selftest():
    """Prove each arm of the in-body rule fires, and each exemption.

    The rule turns on two judgements that are easy to get backwards -
    where a comment run STARTS, and which function encloses it - so
    both are asserted here rather than trusted.

    A header line has no enclosing marker and is never asked for one:
    it sits above its function, so the walk finds the previous one or
    nothing, and in_header_block settles it first.
    """
    lines = TEST_C.split('\n')
    bad = 0
    cases = (
        (3, 'OURS', False, 'body of a function we wrote'),
        (11, 'ORIGIN', False, 'body of a transcription'),
        (0, None, True, 'the OURS header itself'),
        (8, None, True, 'continuation of an ORIGIN header'),
        (22, 'ORIGIN', False, 'body under a WRAPPED signature'),
        (17, None, True, 'continuation of the wrapped header'),
    )
    for idx, want_mark, want_hdr, what in cases:
        mark = enclosing_marker(lines, idx)
        hdr = in_header_block(lines, idx)
        ok = (mark == want_mark and hdr == want_hdr)
        print('%-36s marker=%-6s header=%-5s %s'
              % (what, mark, hdr, 'ok' if ok else 'FAILED'))
        if not ok:
            bad += 1
    words = (('%s ORIGIN 0x1234 Thing %s' % (_C, _E), True),
             ('%s ORIGINAL behaviour is kept here %s' % (_C, _E), False),
             (' * ORIGINAL: and not in a continuation either', False),
             ('%s OURS. With a full stop. %s' % (_C, _E), True))
    for t, want in words:
        got = is_provenance(t.strip())
        ok = got == want
        print('%-36s %s'
              % ('"%s" marker=%s' % (t.strip()[:22], got),
                 'ok' if ok else 'FAILED'))
        if not ok:
            bad += 1
    trailing = '    x = 1;  %s a note %s' % (_C, _E)
    checks = (
        (is_comment(trailing), False, 'a marked statement counts as code'),
        (comment_part(trailing) == '%s a note %s' % (_C, _E), True,
         'its comment is still read'),
        (comment_part('    x = 1;') == '', True,
         'a bare statement carries none'))
    for got, want, what in checks:
        ok = got == want
        print('%-36s %s' % (what, 'ok' if ok else 'FAILED'))
        if not ok:
            bad += 1
    deref = ['    *ou = (sn * py + c * px) >> 10;', '    *p++ = c;']
    for d in deref:
        if is_comment(d):
            print('%-36s %s' % ('a dereference read as comment', 'FAILED'))
            bad += 1
            break
    else:
        print('%-36s %s' % ('a dereference is not a comment', 'ok'))
    flagged = [i for i in range(len(lines))
               if lines[i][:1].isspace() and comment_part(lines[i])
               and not in_header_block(lines, i)
               and enclosing_marker(lines, i) != 'ORIGIN'
               and not is_provenance(comment_part(lines[i]))]
    if flagged != [3, 5]:
        print('rule flagged %s, expected [3, 5]  FAILED' % flagged)
        bad += 1
    else:
        print('%-36s %s'
              % ('it flags the two in OURS and no more', 'ok'))
    # A DELETED FILE IS CHARGED TO NOBODY. Its '+++ /dev/null' has to
    # clear the current path, or the file before it in the diff is
    # charged with every line of it.
    got = parse_diff(TEST_DIFF)
    got = dict((k, (len(v[0]), len(v[1]))) for k, v in got.items())
    if got != {'src/a.c': (1, 0)}:
        print('a deleted file was charged to %s  FAILED' % got)
        bad += 1
    else:
        print('%-36s %s'
              % ('a deleted file is charged to nobody', 'ok'))

    print('\n%s' % ('selftest FAILED' if bad else 'selftest ok'))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--blocks', action='store_true')
    ap.add_argument('--reduce', action='store_true',
                    help='net comment must be NEGATIVE')
    ap.add_argument('--inline', action='store_true',
                    help='only the in-body rule')
    ap.add_argument('--selftest', action='store_true',
                    help='prove the in-body rule fires')
    ap.add_argument('--all', action='store_true',
                    help='apply the in-body rule to the whole tree, '
                         'not only to what the diff adds')
    ap.add_argument('--audit', action='store_true',
                    help='comment against code per file, committed state')
    a = ap.parse_args()
    if a.selftest:
        sys.exit(selftest())
    if a.audit:
        sys.exit(audit())
    if a.blocks:
        sys.exit(report_blocks())
    if a.all:
        # A WORKLIST, NOT A GATE, for the reason report_blocks gives:
        # the committed tree is already full of these, so a non-zero
        # exit here could never be adopted and would only teach the
        # run to be ignored. --inline keeps the gate, on the diff.
        report_inline(True)
        sys.exit(0)
    if a.inline:
        sys.exit(report_inline())
    sys.exit(report_diff(a.reduce) + report_inline())


if __name__ == '__main__':
    main()
