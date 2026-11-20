#!/usr/bin/env python3
"""Write the evidence tag into each data type's description (rule 5).

    py -3.10 tools/tag_types.py                  every known program
    py -3.10 tools/tag_types.py LITHIUM_U.EXE    just one

Wraps tools/ghidra/demo_tag_types.java, whose TAGS table holds the text.
A type with no tag exports as src=untagged and structmap.py check
reports it, so this is what clears that report.

THE PROGRAM SELECTOR DOES NOT REACH A GHIDRA SCRIPT (rule 7.1).
run_ghidra_script accepts program= and ignores it: the script's
currentProgram is whatever the Ghidra TOOL has active. The script checks
its own currentProgram against each row's program column and skips the
rest, so a wrong-program run tags nothing rather than tagging the wrong
database - but it still has to be SENT to the right one to do any work.
So: switch_program first, then run, then check the printed `program:`
line against what was asked for, exactly as export_maps.py does.

switch_program can report success and not take, so the printed line is
the authority and a mismatch here is a FAIL, not a warning.
"""

import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
GQ = [sys.executable, os.path.join(ROOT, 'tools', 'gq.py')]
SCRIPT = 'demo_tag_types.java'

PROGRAMS = ['XCURSION.EXE', 'XCURSION_U.EXE', 'DRAIN.EXE', 'LITHIUM.EXE',
            'LITHIUM_U.EXE']


def gq(*args):
    r = subprocess.run(GQ + list(args), capture_output=True, text=True)
    return (r.stdout or r.stderr).strip()


def install_script():
    """Ghidra resolves scripts from its own directories, not from ours.

    WITHOUT THIS THE RUN USES WHATEVER WAS INSTALLED LAST, so an edit to
    the TAGS table appears to do nothing and the type stays untagged.
    """
    home = os.environ.get('USERPROFILE') or os.path.expanduser('~')
    dest = os.path.join(home, 'ghidra_scripts')
    if not os.path.isdir(dest):
        os.makedirs(dest)
    with open(os.path.join(ROOT, 'tools', 'ghidra', SCRIPT), 'rb') as f:
        data = f.read()
    with open(os.path.join(dest, SCRIPT), 'wb') as f:
        f.write(data)


def tag(program):
    gq('call', 'switch_program', 'program=%s' % program)
    raw = gq('call', 'run_ghidra_script',
             'script_name=%s' % SCRIPT, 'program=%s' % program)
    try:
        d = json.loads(raw)
    except ValueError:
        print('%-16s FAIL  %s' % (program, raw[:160]))
        return False
    if not d.get('success'):
        print('%-16s FAIL  %s' % (program, raw[:160]))
        return False

    body = d.get('console_output', '')
    if '--- SCRIPT OUTPUT ---' in body:
        body = body.split('--- SCRIPT OUTPUT ---')[1].split('===')[0]
    lines = [l.strip() for l in body.strip().splitlines() if l.strip()]

    said = ''
    for l in lines:
        if l.startswith('program:'):
            said = l.split(':', 1)[1].strip()
    if said != program:
        print('%-16s FAIL  ran against %r, not %r - switch_program did '
              'not take' % (program, said, program))
        return False
    body = '  '.join(l for l in lines if not l.startswith('program:'))
    print('%-16s ok    %s' % (program, body[:220]))
    return True


def main():
    wanted = [a for a in sys.argv[1:] if not a.startswith('-')] or PROGRAMS
    install_script()
    bad = [p for p in wanted if not tag(p)]
    if bad:
        print('\n%d program(s) FAILED - see rule 7.1' % len(bad))
        return 1
    print('\ndone. run structmap.py check next.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
