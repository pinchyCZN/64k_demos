# PCDEMOS/64k - DOS demoscene 64k intro reverse engineering

## Project context

Reverse engineering mid-1990s DOS 64k demo competition entries in Ghidra:
depacking them, naming them, typing them, and reconstructing their visual
effects as ANSI C.

**The mission is the EFFECTS, not the whole program.** Loaders, memory
managers, file I/O, keyboard handlers and music replay are means to an
end. Attribute them, bound them, and leave them. What we are after is the
inner loop of each visual effect and the maths behind it.

Five programs share one Ghidra project. **UKKO is a different group,
a different packer and shares nothing with the other three** - see
`docs/layout.txt` section 7:

| Program | Dated | Bytes | Status |
|---|---|---|---|
| `XCURSION_U.EXE` | derived | 108,432 | **current target.** The depacked image - `docs/xcursion/README.txt` |
| `XCURSION.EXE` | 1996-06-06 | 49,761 | the packed original. Stub + blob, 3 functions |
| `DRAIN.EXE` | 1997-06-06 | 65,536 | untouched |
| `LITHIUM_U.EXE` | derived | 175,424 | **the next target.** Depacked - `docs/lithium/README.txt` |
| `LITHIUM.EXE` | 1997-03-08 | 63,281 | the packed original |
| `UKKO.EXE` | 1995-08-11 | 52,836 | **PKLITE, not our packer.** Depacked - `docs/ukko/README.txt` |

**Each demo's own facts are in `docs/<demo>/README.txt`** - the header
fields, how its image divides, its depack status and what is open.
`docs/layout.txt` says which material is shared and which is per demo,
and records that **XCURSION, DRAIN and LITHIUM are packed with the same
packer, byte for byte**, so that depacker and `docs/formats/packer.txt`
are shared. **UKKO is PKLITE and shares none of it** - its packer notes
live in `docs/ukko/`, and its depacker is `tools/unpack/ukko_unpack.py`.

All of them are 16-bit real-mode DOS MZ images - `x86:LE:16:Real Mode`,
nothing like the flat 32-bit world. That shapes everything:

- **An address is a segment:offset pair and the same byte has many
  spellings.** `1be1:00cd` and `1000:bedd` are the same linear `0x1bedd`.
  Always write the segmented form the database shows, and when a document
  needs an unambiguous address, give the linear one as well.
- **There are no symbols, no imports, no library metadata, and no
  relocations.** The Xbox sister project got 421 names free from SDK
  metadata. Here we get zero. Every name in the database is one we chose.
- **The code is HAND-WRITTEN 386 ASSEMBLY, packed.** Measured, not
  assumed: XCURSION_U.EXE and LITHIUM_U.EXE contain ZERO
  `push bp / mov bp,sp` prologues and no `enter`, no Borland or Turbo
  runtime strings, and 15 to 19 per cent of their instructions take
  32-bit register operands. Data is reached through `CS:` - 2,520
  overrides against 54 `ds:` ones in Lithium's first 32 KB - so code
  and data share one segment, which no Borland memory model produces.
  Both use FS and GS, which are 386-only. DRAIN.EXE is unchecked.

  There is no import table to fence anything off with, so the only
  partition available is the one we build.

---

## Hard rules

### 1. Git is read-only

Enforced in `.claude/settings.json`. Read commands (`status`, `log`,
`diff`, `show`, `blame`, `ls-files`, `grep`, ...) are allowed. Everything
that mutates a repo, an index, a working tree, or a remote is denied -
including `add`, `commit`, `push`, `pull`, `fetch`, `checkout`, `switch`,
`restore`, `reset`, `rebase`, `merge`, `stash`, `clean`, `tag`, `remote`,
`config`, `init`.

**Commits, branches, and pushes are the user's job, never the agent's.**
When a change is ready to commit, say so and stop.

> This is a guardrail against accident, not a security sandbox. Do not
> route around it - no `sh -c`, no aliases, no helper script that shells
> out to git. If a git write is genuinely needed, ask for it.

**THIS DIRECTORY IS NOT A GIT REPOSITORY YET.** `git init` is denied, so
the agent cannot create one; ask. Until it exists there is no history of
any kind, so **every working-tree file is the only copy** - no restoring
content from a commit, because there are no commits. To undo something,
make targeted edits to the working copy.

### 2. Ghidra is the source of truth for the binary

Never guess at an address, offset, field, or signature. Read it out of the
database, or out of the generated maps that come from it (rule 6). If the
database is unreachable, stop and say so - do not substitute recollection
for a lookup.

The database is live and shared with the user's open Ghidra GUI. Renames,
retypes and comments are real edits to their project. Batch them
deliberately; do not churn.

```
project   64k_demos        n:\DEV\PCDEMOS\64k\reverse\ghidra\64k_demos
programs  XCURSION.EXE  DRAIN.EXE  LITHIUM.EXE
server    http://127.0.0.1:8090/
```

#### 2.1. THE PORT TRAP - check it before every session's first write

**There are two Ghidra servers on this machine and the MCP bridge
defaults to the WRONG ONE.**

```
8089    JSRF_decomp    the Xbox sister project. NOT OURS. Do not touch it
8090    64k_demos      ours
```

`GHIDRA_MCP_URL=http://127.0.0.1:8089/` is set in the user-scope MCP
config, and the bridge honours that variable **above everything else** -
above its own discovery, and above an explicit `connect_instance`. So
`connect_instance("64k_demos")` returns `{"connected": true}` and
connects to 8089 anyway. It has already done so once in this project.

A read against the wrong database is a wasted call. **A WRITE AGAINST THE
WRONG DATABASE SILENTLY CORRUPTS A SISTER PROJECT**, and it is the one
mistake here that cannot be fixed by looking something up again.

So, as the first MCP call of any session:

```
mcp__ghidra__list_instances        must report 64k_demos on 8090
```

and if it reports 8089 or `JSRF_decomp`, **the MCP is unusable for this
project in that session.** Do not work around it with `connect_instance`.
Use `tools/gq.py`, which addresses the server by port - a thing no
environment variable can redirect - and refuses to run against any
project but `64k_demos`.

```
py -3.10 tools/gq.py who                       prove which database
py -3.10 tools/gq.py find   list_              search the tool names
py -3.10 tools/gq.py sig    rename_data        what a tool takes
py -3.10 tools/gq.py call   get_metadata program=XCURSION.EXE
```

`.mcp.json` in this directory declares a project-scoped `ghidra` server
pinned to 8090, and a session started fresh here gets it: the
project-scoped server of a given name beats the user-scoped one, and
`list_instances` then reports `64k_demos` on 8090 [verified].

**`.mcp.json` IS NOT COMMITTED** - it names one machine's paths, and
two of its three servers live outside this repository. Copy
`.mcp.json.example` and fill them in; a clone without that step has no
`ghidra` server at all and falls through to the user-scope 8089.

**That does not remove the check.** The user-scope pin is still there, so
any session that does not pick up `.mcp.json` - a different working
directory, a harness that loads only user scope - silently gets 8089
again, and nothing in the tool output says so except the port.

#### 2.2. Strict program routing is on - name the program every time

`GHIDRA_MCP_REQUIRE_PROGRAM_SELECTORS=all`. Every call must carry
`program="XCURSION.EXE"`. A call that omits it is refused rather than
silently answered from whatever the server had open, and **that refusal
is the feature** - three programs are open in one project here, so an
unrouted call is a coin toss. Obey it; do not work around it.

### 3. The packed image and the unpacked image are different programs

`XCURSION.EXE` as imported is **the unpacker stub plus a compressed
blob.** Almost none of it is the demo. Ghidra split it on import:

```
HEADER   HEADER::0 - 7f          the MZ header, 8 paragraphs
CODE_0   1000:0000 - 1000:be0f   48,656 bytes - the packed payload
CODE_1   1be1:0000 - 1be1:03d0      977 bytes - the unpacker stub
DATA     1be1:03d1 - 9000:2ff0   the BSS the header asks DOS for
```

**An address only means something once you say which image it is in.**
The same demo function has one address in the packed file and a different
one after unpacking, and the two will not be relatable by a constant
offset because the unpacker relocates as it goes.

So:

- **Every address in a document or a comment carries its image.** Write
  `packed 1be1:00cd` or `unpacked 1234:0010`. An address with no image
  named is a defect, not a shorthand.
- **The unpacked image gets its own Ghidra program**, imported from the
  dump, not grafted into `XCURSION.EXE`'s space. Mixing them in one
  program makes every address ambiguous for the rest of the project, and
  the ambiguity is not visible in any individual listing.
- The exception is deliberate and narrow: a cross-reference comment
  naming the other image's address, which is how the two are stitched
  together. Say which image, every time.
- **Findings about the PACKER go in `docs/formats/`; findings about the
  DEMO go in `docs/<demo>/effects/` and `docs/<demo>/subsystems/`.**
  The packer is SHARED - all three demos use the same one, byte for
  byte - so its notes are `docs/formats/packer.txt`, not a demo's. It
  is not
  the demo and its notes should never crowd out the demo's.

#### 3.1. Depacking: static reimplementation first, emulation second

What is known is in `docs/formats/packer.txt`. The short form:

- **The decompressor is in the clear**, 0x248 bytes at image offset
  `0x54`, LZ77-class with a 16-bit LODSW bitstream and no entropy coder.
  It relocates itself to a high segment and far-returns into the copy, so
  its CS-relative addresses are image offset minus `0x54`.
- **The 977-byte stub at the end is an entry-point wrapper and nothing
  more.** It chains through `INT 1` and `INT 3` vectors it installs
  itself, XOR-patching its own code to assemble its exit jump a byte at a
  time. It does not touch the payload.
- `INT 3` here is **a one-byte call, not a breakpoint.** Stepping over it
  skips a stage. Nothing in the stub is a debugger check.
- It looks like **LZEXE 0.91 or a descendant**, but the `LZ91` marker has
  been stripped, so that is `[inferred]` and no stock unpacker will
  recognise the file.

So the wrapper is a dead end: read the decompressor and drive it
ourselves rather than fighting the chain.

Preference order, and the reason is reproducibility:

1. **Read the decompressor and reimplement it in Python** under
   `tools/unpack/`. The output is then a deterministic artifact anyone can
   regenerate from the original EXE, and the format is understood rather
   than merely defeated.
2. **Emulate the stub** and dump memory at the original entry point.
   Needs a 16-bit real-mode emulator with interrupt stubs.
3. **Run it under a DOS emulator's debugger and dump.** Ground truth, and
   the slowest to repeat.

A dump obtained by 2 or 3 is still a finding worth having. But record how
it was produced next to it, because **a dump nobody can regenerate is a
dead end the moment it is lost**, and it cannot be checked against the
original.

### 4. Write down what you discover

A finding that exists only in a chat transcript is lost. Anything learned
about a struct, an effect, a subsystem, or a file format goes into `docs/`
**in the same session it was found**. See `docs/README.txt`.

### 5. Separate fact from inference

Every claim in `docs/` carries a tag: `[verified]`, `[inferred]`,
`[observed]`, `[guess]`. `docs/README.txt` section 2 defines them and says
why `[observed]` outranks `[inferred]` for anything visual.

**Renaming a Ghidra symbol is an assertion. Do not assert what you have
not earned.** A name you are not sure of carries `maybe_` and a plate
comment saying what is unconfirmed.

**A type carries its tag in its Ghidra description**, whose first line is
fixed:

```
[verified] origin=1be100cd  the unpacker stub entry
```

The exporter reads that line, so the tag and the origin address reach
`struct_map.txt` without a second copy anywhere. A type with no tag shows
up as `src=untagged` and `structmap.py check` reports it. That report is
the most common way a session ends with the work done and the provenance
lost - clear it before stopping.

### 6. Ghidra is a deliverable, not a reference

**This is the central rule of the project.** Low-level research that turns
up something the database does not have goes back INTO the database, in
the session that found it. A name read off a string, a signature proved by
a disassembly, a field whose width you just established from a `mov` - all
of it lands in Ghidra. Leaving it in a chat reply means the next session
rediscovers it, and the database stays the weakest copy of what we know.

So every analysis task closes with three things done, not one:

| Done | Where |
|---|---|
| **renamed** | the function, its parameters, its locals, the globals it touches |
| **retyped** | a struct applied at every site, not just the one being read |
| **documented** | a plate comment on the function; the note in `docs/` |

A function you read and understood and left as `FUN_1be1_00cd` is work
thrown away. If the understanding is too thin to name it, say that in a
plate comment on it - that is still a deposit.

#### 6.1. Naming conventions

- **Functions:** `Subsystem_Verb` - `Unpack_Entry`, `Plasma_Render`,
  `Pal_Fade`, `Vga_SetMode`. One style, not two.
- **Effects get the effect's name**, and if we do not know what the demo
  called it, describe it: `Tunnel_Render`, `Blobs_Update`.
- **Unverified:** prefix `maybe_` and leave a plate comment stating what
  is unconfirmed. Dropping the prefix later is cheap; a confident wrong
  name costs a future session a wrong turn.
- **Globals:** `g_` prefix.
- **Structs, unions, enums: prefix per demo** - `Xcur_`, `Drain_`,
  `Lith_`. Data types are per-program in Ghidra, but the three share
  `struct_map.txt` and every document in `docs/`, and an unprefixed
  `Palette` in two of them is a fact with two conflicting layouts.
  Anything genuinely common to the era - a VGA register block, a mode-X
  page descriptor - takes `Dos_`.
- **Never overwrite an existing meaningful name without saying why.**

**The Ghidra server warns against all of the above and it is wrong for
this project.** It wants `UnpackMemcheck` over `Unpack_MemCheck`, a verb
from a fixed list as the first word, and `snake_case` labels over `g_`.
Those warnings are advisory, the write succeeds, and the conventions
above are the ones the sister project uses and the ones we keep. Ignore
the warning; do not rename to satisfy it, and do not re-litigate this per
session.

#### 6.2. Real-mode specifics that cost a session elsewhere

- **A 16-bit far call is a segment change, and Ghidra's auto-analysis
  guesses segments badly on self-modifying code.** When a listing looks
  like nonsense - `SALC`, `FDIV float ptr [BP+DI]`, long runs of
  `ADD AX, word ptr [BP+DI]` from `0303` filler - it is data being
  disassembled as code, or the wrong segment register value. Do not
  document what that listing says. Establish the segment first.
- **Code and data are freely mixed and the image is one rwx blob.** There
  is no executable-section test that means anything. What distinguishes
  code from data here is a reachable control-flow path, nothing else.
- **Self-modifying code is normal in this corpus, not a curiosity.** A
  `MOV CS:[...], ...` means the bytes Ghidra shows are not the bytes that
  execute. Note it on the function and say what the patched value is.

#### 6.3. Check the maps AND the comments before naming anything

Grep `reverse/exports/` and `docs/` for an address before assigning a
name, and search the plate comments too. A global's identity is often
already written in the comment of a function nobody has read yet, and on
a sister project that cost three renames of one symbol across three
sessions - once in the wrong direction - while the answer sat in a plate
comment the whole time.

Searching for an address is cheap and belongs *before* reasoning about
what something means, not after.

#### 6.4. Save the database when the work lands

**Edits are live but UNSAVED. Call `save_program` after any batch of real
changes** - a struct created or retyped, functions renamed, plate comments
written, a script applied. Not after every single call; after the batch
that constitutes the finding.

An unsaved database is one closed window away from losing a session's
work, and the loss is silent: Ghidra reopens looking exactly as it did
before, so there is nothing to notice. Nothing else in this project is a
copy of it - `reverse/exports/` is generated FROM the database, so an
export is not a backup.

**Save before re-exporting the maps**, so the text files can never
describe a state the database would not come back as.

The one caution: saving commits whatever the user's own GUI session has in
flight along with ours. That is normally what they want, but if a batch
turns out to be wrong, say so rather than saving over it.

### 7. Struct layout comes from the maps, never from arithmetic

`reverse/exports/<PROGRAM>/` holds three files generated from the database
by `tools/ghidra/demo_export_maps.java`, **one directory per program**:

| File | What it holds |
|---|---|
| `struct_map.txt` | every struct, union, enum and typedef we defined |
| `symbol_map.txt` | named data globals - address, type, count, size |
| `function_map.txt` | every function carrying a name somebody chose |

They exist because reading a decompiled function means answering "what is
at `+0x18` of this thing" and "what is `1be1:005a`" dozens of times, and a
flat text file answers both with one grep instead of one round trip
apiece.

```
py -3.10 tools/structmap/structmap.py -p XCURSION_U.EXE whatis 1000:3905
py -3.10 tools/structmap/structmap.py -p XCURSION_U.EXE off  Xcur_Effect.frame
py -3.10 tools/structmap/structmap.py -p XCURSION_U.EXE at   Xcur_Effect 0x18
py -3.10 tools/structmap/structmap.py -p XCURSION.EXE   sym  Unpack
py -3.10 tools/structmap/structmap.py -p XCURSION_U.EXE check
```

`-p` names the program, or `DEMO_PROGRAM` does. With more than one
exported and none named, `structmap.py` exits listing them instead of
reading whichever sorts first - the packed and depacked images share no
addresses, so a map read against the wrong one answers every question
confidently and wrongly. It accepts a segmented address or a linear one.

- **Consult the maps first.** Grep by hex offset, field name, symbol or
  address.
- **Do NOT calculate offsets by hand** when the type is in the map. `off`
  and `addr` print their working, so the arithmetic can be checked against
  the instruction in front of you.
- **Do not hand-edit the maps.** They are generated; the database is the
  source of truth and the next export discards the edit.
- **Re-export after any session that applied a type or a name**, and run
  `structmap.py check`. A map older than the database is the one way this
  system lies to you.

Run the exporter through its wrapper, which installs the script where
Ghidra can find it and does one program at a time:

```
py -3.10 tools/export_maps.py                   every open program
py -3.10 tools/export_maps.py XCURSION_U.EXE    just one
```

#### 7.1. THE PROGRAM SELECTOR DOES NOT REACH A GHIDRA SCRIPT

Rule 2.2 makes `program=` mandatory on every call, so the habit is to add
it and trust it. **`run_ghidra_script` accepts it and ignores it.** A
script's `currentProgram` is whatever the Ghidra *tool* has active, which
is normally whatever was imported or clicked last - so the selector names
one program while the export describes another.

It is silent. Both exports succeed, both print plausible counts, and the
only clue is the `program:` line the script itself prints. Two map sets
were written describing the same program this way.

So: `switch_program` first, then run the script, then check that printed
line against what you asked for. `tools/export_maps.py` does all three,
and **it stages the write** - the script is pointed at `build/` and
nothing reaches `reverse/exports/` until the check passes, because a
script writes the instant it runs and a check afterwards arrives too
late. Use it rather than calling `run_ghidra_script` directly, and
assume the same trap for any future script under `tools/ghidra/`.

**Every map now carries `# program: NAME` in its header**, so a map
that was filed under the wrong program says so on its own and
`structmap.py check` reports it. A map with no stamp predates this and
cannot be checked - re-export it.

**`switch_program` can report success and not take.** It returns
`{"success": true, "switched_to": ...}` while `get_current_program_info`
still names the old program, so the script still runs against the old
one. Two programs sharing a base name is one way to get there - the GUI's
active tab wins and the selector cannot break the tie. There is no
workaround from here: the Ghidra tool has to have the program active,
which is the user's window. `export_maps.py` catches it and writes
nothing, so trust its FAIL line over `switch_program`'s success.

#### 7.2. ONE ADDRESS OR OFFSET HAS EXACTLY ONE OWNING SCRIPT

Apply scripts under `tools/ghidra/` **EDIT, they never REBUILD.** Two ways
of getting this wrong have cost work on the sister project:

- **A script that constructs a fresh `StructureDataType` and adds it with
  `REPLACE_HANDLER` deletes every field the other scripts defined.**
  Re-running one such script silently destroyed 24 recovered fields. Look
  up the existing type and edit it; only construct when it does not exist.
- **Two scripts naming the same address fight.** Re-running an older one
  reverted a confirmed name back to its `maybe_` form. When a later script
  takes over an address or an offset, DELETE the entry from the earlier one
  and leave a comment saying who owns it now.

The symptom is silent: the script reports success and the count it prints
is its own, not the total. Print both - "14 from here, 38 named in total"
is what caught it.

**A missing struct is a stop, not a guess.** An honest `undefined2` beats a
confident wrong field. Map what the evidence supports, leave the rest
undefined, and say at the end of the task which types are missing.

### 8. Plain ASCII in everything we write

Every byte in a document, script, or comment we author must be `<= 0x7F`.
No em dashes, curly quotes, arrows, box-drawing, or ellipsis glyphs. Write
`->`, `-`, `...`, `x`, and `+`/`-`/`|` for diagrams.

```
py -3.10 tools/asciify.py --check    ASCII, width, table alignment
py -3.10 tools/asciify.py --fix      transliterate and rewrap
```

The demos' own strings are not covered by this - they are data, read out
of the binary, and get quoted as they are. Neither is anything under
`reverse/`.

### 9. Documents are RFC-style plain text, not markdown

Documents are `.txt`, all under `docs/`: project-level documents at the
top, and per-demo material under `docs/<demo>/`: effect reports in
`effects/`, subsystem reports in `subsystems/`, struct notes in
`structs/`, that demo's asset formats in `formats/`. Formats the demos
SHARE, like the packer, go in `docs/formats/`.

**A cross-reference inside a demo's docs is relative to that demo's
root** - `effects/voxel.txt`, `subsystems/palette.txt` - never to the
file's own directory. **A reference written in a `src/` comment obeys
the same convention**, with the demo taken from the source file's own
directory, so `subsystems/raster.txt` in `src/lithium/` is
`docs/lithium/subsystems/raster.txt`; a file directly in `src/` is
rooted at `docs/`. `tools/checkrefs.py` resolves both sides and is in
the closing checklist. This file is the one exception - Claude Code
requires the name `CLAUDE.md` and the harness renders it.

```
PCDEMOS 64k                                                Analysis Note


                          THE XCURSION PACKER


Abstract

   Body text indented three spaces, wrapped at 72 columns.

Table of Contents

   1. First Section
      1.1. A Subsection

1.  First Section

   +--------------+-------+
   | Column       | Count |
   +==============+=======+
   | aligned with | 2542  |
   +--------------+-------+
```

What follows from that:

- **No markdown syntax in a `.txt` file.** No `#` headings, no `**bold**`,
  no backticks, no `*emphasis*`, no `[text](link)`, no `|`-delimited
  markdown tables. If it needs a renderer to look right, it is wrong.
- **Tables are drawn with `+---+` borders and space-aligned columns**,
  `+===+` under the header row. Every `|` row must be exactly as wide as
  the border above it.
- **Numbered sections** (`1.`, `1.1.`) with a Table of Contents.
- **Body indented three spaces, wrapped at 72 columns.** Tables may run to
  120 including the indent, and so may a deeply indented listing.
- Blocks indented four or more spaces are preformatted - address maps,
  disassembly, diagrams - and are never rewrapped.
- Keep an **Open questions** section in every document. An honest gap is
  more useful than a confident guess.

`py -3.10 tools/rfcfmt.py --fix P` renders a markdown-ish draft as RFC
text. It is a one-way converter, not a round-trip formatter - it refuses a
file already in RFC form. Author new documents in RFC form directly, and
if you do use it, **read the output**: it strips inline markup, so a
passage about markdown comes out mangled.

The full conventions, the evidence tags and the templates are
`docs/README.txt`.

### 10. Record facts, not the journey - and record each one once

Documentation says **what the code does and why**. It does not narrate the
search that found it. Do not write "we first thought X, then found Y" -
write Y. No "this took three sessions", no restating what a previous
section established, no summary of the summary.

The one exception is a marked trap: if an approach looks obviously right
and is actually wrong, say so in a sentence. "The stub's `INT 3` is a jump,
not a breakpoint" is a fact about the problem. "I set a breakpoint there
first" is a diary entry.

**One fact, one home:**

| What | Where it lives |
|---|---|
| addresses, instruction-level evidence | the Ghidra plate comment |
| struct offsets, strides, sizes | the Ghidra type -> `struct_map.txt` |
| a type's evidence tag and origin address | the Ghidra type description |
| what an effect does, and its maths | `docs/<demo>/effects/` |
| a packer or asset format | `docs/formats/` |
| why the C is shaped this way; divergences | the `src/` comment |
| project rules | this file |

Everything else **points at** that home - `see effects/tunnel.txt 3.2` -
and does not restate it.

**Correct in place; never append.** When a fact changes, edit the sentence
that is now wrong. Leaving it standing with a correction beside it
produces two statements, one of them false, and the reader cannot tell
which.

**AN ANSWERED OPEN QUESTION IS DELETED, not annotated.** Move its fact
into the body and remove the bullet. `ANSWERED:` and `RESOLVED:`
prefixes in an Open questions list are the most common way this rule
gets broken here.

#### 10.1. The test, and the check

This rule is older than every document in `docs/` and has been broken in
every session anyway, so it gets a tool rather than another paragraph.

```
py -3.10 tools/journalcheck.py          every hit, with its category
py -3.10 tools/journalcheck.py --count  just the totals
```

**THE TEST A SENTENCE MUST PASS: does it describe the BINARY, or the
history of our work on the binary?** A reader who has never seen a
previous session should not be able to tell that there were any.

| Banned | Write instead |
|---|---|
| "this was named `Music_TimerIsr` for several sessions" | "`Music_TimerIsr` is the whole engine, not a replayer" |
| "ANSWERED: `g_ObjFaceAttr` is a texture segment" | delete the bullet; state it in the body |
| "the ten-runner survey missed them" | "a text scene never calls `Scene_ProjectVerts`" |
| "that cost four wrong names" | nothing - delete it |

Both columns carry the same fact. The left also carries us.

The narrow exception stays rule 10's marked trap: ONE sentence saying an
approach looks right and is wrong, phrased as a fact about the problem.
"The stub's `INT 3` is a one-byte CALL, not a breakpoint" is a fact. "I
set a breakpoint there first" is a diary entry.

**CLAUDE.md itself is exempt and nothing else is.** A rule needs its
rationale, and "this cost three renames on the sister project" is what
stops the next person undoing it. That licence does not extend to
`docs/`.

### 11. Running the demos

Unlike the sister Xbox project, **the agent MAY run these under a DOS
emulator** - they are 50 KB, they need no GUI emulator of their own, and
for a packed binary an emulator run is sometimes the only way to get the
unpacked image at all (rule 3.1).

What that permission does not extend to:

- **THE THREE EXE FILES ARE STOCK AND STAY STOCK.** They are the only
  copies and there is no git history to restore them from. Never write to
  `reverse/ghidra/*.EXE` - not to patch, not to unpack in place, not to
  "fix" the header. A depacker writes to `%TEMP%` or `reverse/unpacked/`,
  never over its input.
- **An emulator run produces `[observed]` evidence, not `[verified]`.** A
  screenshot says what it looked like; it does not say what the code does.
- A prediction that would need a run to confirm stays tagged as a
  prediction until the run happens. Do not quietly promote it.
- Say what you ran and how, so the result can be reproduced.

### 12. Never fake the look

**Match the original's CODE, not a capture of it.** When a reconstructed
effect looks wrong next to the real demo, the fix is to find the code that
produces the right result - never to add something that produces the right
pixels.

Banned, however tempting: hardcoding a constant, colour or timing that
"looks right" instead of reading it out of the binary; inventing a
palette ramp to stand in for one we have not located; tuning a magic
number until the output matches a screenshot.

This rule carries more weight here than anywhere else, because a 64k demo
effect IS its constants. A plasma whose sine table came from a video
instead of the binary is not a reconstruction of anything - it is a new
effect that resembles one. Implement what the code says, let it look
wrong, and write down the gap.

`[observed]` evidence is for deciding what to go and read, and for
catching a wrong colour. It is never a licence to implement the
observation directly.

### 13. Python is `py -3.10`

Run scripts through the launcher: `py -3.10`, which resolves to
`C:\Program Files\Python310\python.exe`. Do not assume `python` is on
PATH. `capstone` 5.0.7 and `unicorn` 2.1.4 are both installed; ask before
assuming any other pip install.

### 14. Scratch files go to the temp directory

Test exports, one-off scripts, and debug output go to the session scratch
directory or `%TEMP%`, resolved via the environment variable - never a
folder literally named `%TEMP%` inside the repo. Repo paths are for
persistent artifacts: `reverse/exports/*.txt`, `reverse/unpacked/*.bin`,
`docs/*.txt`, `src/`.

`structmap.py` honours `DEMO_EXPORTS` so a trial export can be checked in
a scratch directory before it replaces the real one.

---

## The reconstruction

**Python first, C later.** `sim/` is the live reconstruction: the
routines transcribed into Python, drawn into a window, one effect per
number key. An effect goes there before it goes into `src/`.

The reason is that an effect is proved by LOOKING at it, and the gap
between "the maths is transcribed" and "it renders" is where the
misreadings surface. Python closes that loop in minutes; C needs a
16-bit target or a port layer before it shows a single pixel. So an
effect goes into `sim/` first, and `src/` gets it once it is understood
well enough that the C is a translation rather than an experiment.

```
py -3.10 sim/run.py xcursion          number keys pick the effect
py -3.10 sim/run.py xcursion --aspect 640x480, the CRT shape
py -3.10 sim/run.py xcursion --list   every part, numbered
```

### 15. `sim/` is a transcription, not an impression

Rules 16 to 19 apply to `sim/` now and to `src/` when it lands. Rule 12
applies hardest of all here, because a Python window is exactly where
faking it is easiest and most tempting.

- **One function per Ghidra function, carrying the database's name**,
  lowercased to Python taste - `Points_Render` becomes `points_render`.
- **Every routine's docstring opens with `ORIGIN` and the address**, so
  any number in the file can be checked against the listing.
- **Transcribe the form the code uses, not the form the maths reduces
  to.** The terrain is a sum of two sine lookups and is separably
  `2 sin(y) cos(2x)`; `sim/` writes the sum, and `docs/` records the
  identity. The identity is a description, not a licence to substitute.
- **Keep the integer widths and the wrap.** A `& 0xffff`, a `>> 7` on a
  value stored as a byte, an `IDIV` that truncates toward zero where
  Python's `//` floors - all of it is load-bearing.
- **Where a value is not known, refuse.** No placeholder palette that
  happens to look like the capture. `sim/` shows the palette index as
  grey and says so in the window title, because the real palette has
  not been traced. A plausible brown-and-blue ramp would be a rule 12
  violation wearing a proof-of-concept hat.
- **Anything ours is labelled ours.** The field viewer and the camera
  sweep are not in the demo; both say so.
- The window is stdlib tkinter: a `PhotoImage` takes raw P6 PPM bytes,
  so a framebuffer blits in one `configure(data=...)`. No PIL.
- Mode 13h is 320x200 with non-square pixels. Default `--scale 2` is a
  pixel-exact 640x400; `--aspect` gives the 640x480 a CRT showed and is
  not pixel-exact. Pixel-exact is the default because counting pixels
  against the listing is what this is for.

### 16. ANSI C, and the target is readable C

**C89/ANSI C.** No `//` comments, no declarations after statements, no
`stdint.h`, no VLAs, no `inline`. Fixed-width types come from a project
header, so `u8`/`s16`/`u32` are ours and spelled once.

**Tabs for indentation, spaces for alignment**, at a tab width of 4:
tabs to the statement's nesting depth, then spaces out to the column
being lined up with. Python keeps spaces; `src/third_party/` is not ours.

The goal is **C that reads like the effect**, not C that reads like a
decompiler. The decompiler's output is the starting point and no more:

- Reconstruct the maths. `(x * 0x51eb851f) >> 37` is a divide by 100 and
  should be written as one, with the original's form noted if the
  rounding differs.
- Real-mode segment arithmetic becomes a flat pointer or an array index.
  `0xA000:0000` becomes a `u8 *vga` the port supplies. Say so once in a
  header comment; do not re-explain it per function.
- Keep the original's integer widths and overflow behaviour where the
  effect depends on them, which for fixed-point effects is usually. A
  `u8` that wraps is load-bearing in a plasma.
- Fixed-point stays fixed-point. Do not "clean it up" to float - that
  changes the output and makes a comparison against the original
  meaningless.

### 17. Import whole functions, not slices

**One C function per Ghidra function, carrying the database's name.**
Start from the decompiler's C and fix it up. Do not transcribe only the
part of a function the current task happens to need - a half-imported
function looks finished and is the hardest kind of gap to find later.

Where an effect is one giant asm routine with no call structure, split it
by what it does and say in a header comment that the split is ours.

### 18. Comments: `docs/` is the default, `src/` is the exception

**The failure mode this rule exists to stop: a one-line fix carrying five
lines of comment.** That is not documentation, it is a changelog written
into the code.

Comment **whole functions**, not individual edits. A function gets a
header comment saying what it does. A three-line fix inside it gets
**zero** comments unless it is one of these:

| Keep | Why |
|---|---|
| **`ORIGIN` + the address** | address first, name second - names drift, addresses do not. Say which image (rule 3) |
| **a landmine** | looks obviously right, is wrong. State the PROBLEM as a fact, never the story |
| **a deliberate divergence** | so nobody "fixes" it back |
| **`[open]` / not implemented** | what is missing and what it would take |
| **a magic constant's provenance** | the address it was read from, because rule 12 depends on being able to check it |
| **a non-obvious WHY** | something reading the code cannot give you |

Everything else goes to `docs/` with a one-line pointer, or nowhere. In
particular, delete on sight: the dev journal ("we first thought X", "fixed
the crash by"); restatement of the code; our own debug toggles; inline
disassembly listings, since the `ORIGIN` address IS the listing; and
anything stale, which is the highest priority of all because a stale
comment actively misleads where a missing one merely omits.

**`FUN_` and `DAT_` in a `src/` comment are a reference, not a stale
name.** Do not "fix" `FUN_1be1_00cd` to whatever the database now calls
that address. The name carries the address, and the address is the only
thing that survives - lose the `.rep` and a friendly name points at
nothing while `FUN_1be1_00cd` still points at the binary. Writing both is
fine; writing the friendly name *instead* is the loss.

The gate, because the criteria above are a judgement and judgement is
generous toward one's own fresh work:

```
py -3.10 tools/commentbudget.py            net comment <= net code
py -3.10 tools/commentbudget.py --inline   THE IN-BODY RULE
py -3.10 tools/commentbudget.py --reduce   a sweep: comment must DROP
py -3.10 tools/commentbudget.py --blocks   runs over 12 lines
py -3.10 tools/commentcheck.py             where a comment SITS
```

**THE IN-BODY RULE IS ABSOLUTE AND IS NOT A BUDGET.** A comment
indented inside the body of a function WE WROTE - one whose header
says `OURS` - is refused however short it is and however much code
sits beside it. The explanation goes in that function's own header,
or in `docs/`. Two things are exempt because they can live nowhere
else: a provenance marker, and the body of a TRANSCRIBED function -
one whose header carries `ORIGIN` - where a note about the original's
arithmetic is pinned to the line it describes.

So the transcription is allowed its line-by-line notes and our own
code is not. `commentcheck.py` polices the other half: a comment with
nothing attached under it, two stacked at file scope, an `img`
address that no export names any more, and a function whose header
says neither `ORIGIN` nor `OURS`.

Run the first before calling any change done. Going over it is the signal
to move the explanation to `docs/`, never to delete something true. See
`tools/README-commentbudget.txt` for what it is blind to.

> **The surrounding file is not the standard.** A dense neighbour reads as
> house style; it is evidence of the problem, not licence.

### 19. Discoveries flow BOTH ways

**The database and `src/` describe the same program. A fact learned on one
side is recorded on the other, in the same session.**

The failure this prevents is easy to walk into: work moves to the port, a
struct in `src/` turns out to be wrong, the agent goes back to Ghidra,
finds the missing field, fixes the C and not the database - and from then
on the two disagree with nothing saying which is right.

| Learned while... | Also required |
|---|---|
| fixing `src/` against the binary | apply the field, name or type in Ghidra |
| reading Ghidra for a port question | write it into `docs/` AND the C |
| a struct gains or loses a member | update the Ghidra struct to match |
| a function's real name is settled | rename it in the database too |

This does not mean `src/` must mirror the database's layouts. The
reconstruction may hold a different, simpler struct, as long as the
difference is deliberate and written down. What is banned is the two
drifting apart by accident.

---

## Repository layout

```
64k/
  CLAUDE.md              this file
  .mcp.json              project-scoped MCP servers  [not committed]
  .mcp.json.example      copy it and fill in the paths
  .claude/settings.json  permissions (read-only git)
  docs/                  everything we discover - RFC-style plain ASCII .txt
    README.txt             doc conventions, evidence tags, templates
    layout.txt             what is shared, what is per demo, and why
    formats/               formats the demos SHARE
      packer.txt             the packer all three are packed with
    <demo>/                one directory per demo: xcursion, lithium
      README.txt             THAT DEMO'S facts, status and open list
      effects/               one file per visual effect  <- the mission
      subsystems/           one file per subsystem
      structs/               one file per reconstructed type
      formats/               that demo's own asset formats
  reverse/
    ghidra/                Ghidra project and the binaries  [not committed]
      XCURSION.EXE  DRAIN.EXE  LITHIUM.EXE
      64k_demos.gpr / 64k_demos.rep/
    unpacked/              depacked images  [bytes not committed]
      xcursion.bin           the flat memory image
      XCURSION_U.EXE         the same bytes under a rebuilt MZ header
      xcursion.json          the recovered header fields  [committed]
    exports/               GENERATED from the database - see rule 7
      <PROGRAM>/             one directory per program
        struct_map.txt / symbol_map.txt / function_map.txt
  sim/                   the live reconstruction - rule 15
    harness.py             Screen, the blit, the frame loop, generic keys
    run.py                 the entry point: run.py <demo>
    <demo>/
      parts.py               the transcriptions
      demo.py                the part list and that demo's keys
  tools/
    gq.py                  talk to port 8090 directly - rule 2.1
    export_maps.py         the maps, per program - rule 7.1
    checkrefs.py           doc cross-references, in docs/ AND src/
    journalcheck.py        no dev-journal writing in docs  [rule 10]
    asciify.py             ASCII and width rule: --check / --fix
    rfcfmt.py              markdown draft -> RFC text
    commentbudget.py       no more comment than code  [rule 18]
    commentcheck.py        where a comment sits        [rule 18]
    README-commentbudget.txt
    unpack/
      xcur_unpack.py         THE DEPACKER for the other three. Runs the
                             binary's own decompressor under Unicorn
      ukko_unpack.py         UKKO's - a different packer, PKLITE
      xcur_stub.py           flow-follows the entry-point wrapper
    ghidra/
      demo_export_maps.java  the database -> the three maps
      demo_apply_*.java      the apply scripts - ONE owns each offset
    structmap/
      structmap.py           query and validate the maps
  src/                   the ANSI C reconstruction
    host.h               the host <-> demo contract
    main_sdl.c           native and web host; one of two files that loop
    main_ppm.c           the P6 dumper, and the depack acceptance test
    xcurpack.c           the packer XCURSION, LITHIUM and DRAIN share
    pklite.c             UKKO's, which is a different one
    <demo>/              one directory per demo
    demos.sln            <demo>.vcxproj and <demo>_ppm.vcxproj each
```

## Per-demo facts live with the demo

CLAUDE.md is rules only. Everything true of one demo in particular -
its verified header facts, how its image divides, its depack status,
whether it has a dispatcher, and where its work stands - is in that
demo's own README:

```
docs/xcursion/README.txt      EXCURSION, Vista Software, Abduction '96
docs/lithium/README.txt       LITHIUM
docs/layout.txt               why the split falls here, and the shared
                              packer all three demos use
```

**Read the demo's README before its effect notes.** It names the
working program, says which addresses mean what, and lists what is
open - and without it an address in an effect note is ambiguous
between the packed and unpacked images (rule 3).

## Working method

### Tools


Anything run more than twice becomes a script in `tools/`, with a header
comment saying what it does and how to run it. Ghidra scripts go in
`tools/ghidra/` and are run as described in rule 7.

### Closing a session

```
py -3.10 tools/gq.py who                        rule 2.1 - the right DB
py -3.10 tools/asciify.py --check
py -3.10 tools/checkrefs.py                     refs, docs/ and src/
py -3.10 tools/journalcheck.py                  rule 10 - facts, not us
save_all_programs                               rule 6.4 - FIRST
py -3.10 tools/export_maps.py                   every open program
py -3.10 tools/structmap/structmap.py -p <PROGRAM> check   each one
py -3.10 tools/commentbudget.py                 rule 18
py -3.10 tools/commentbudget.py --inline        rule 18, the hard half
py -3.10 tools/commentcheck.py                  anchors and provenance
```

The save comes before the export, so the maps can never describe a state
the database would not come back as.

Then say what was renamed, retyped and documented, and what is still open.
If `check` reports an untagged type, the session is not finished.

## Legal note

A personal interoperability and preservation effort on 30-year-old
demoscene releases. The original executables are not redistributed. Notes
and any reconstructed source are our own work product.
