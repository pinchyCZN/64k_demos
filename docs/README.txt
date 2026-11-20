PCDEMOS 64k                                                Analysis Note


                       DOCUMENTATION CONVENTIONS


Abstract

   The format and the evidence rules for everything under docs/.
   CLAUDE.md rules 4, 5, 7, 8 and 9 are the short form; this is the
   long form, with the templates.

   Read it before writing a new document. The checks in section 9 are
   not optional and they are cheap.

Table of Contents

   1. The format
   2. Evidence tagging
   3. Which image an address is in
   4. One fact, one home
   5. The effect note
   6. The struct note
   7. The subsystem and format notes
   8. Record facts, not the journey
   9. Before calling a document done

1.  The format

   Documents are .txt, plain ASCII, laid out like an old RFC. CLAUDE.md
   is the single exception, because Claude Code requires that name and
   the harness renders markdown.

   What that means concretely:

   o No markdown syntax in a .txt file. No hash headings, no asterisks
     around a word for emphasis, no backtick spans, no bracket-paren
     links, no pipe-delimited markdown tables. If it needs a renderer
     to look right, it is wrong.
   o Numbered sections, 1. and 1.1., with a Table of Contents.
   o Body text indented three spaces and wrapped at 72 columns.
   o Tables drawn with +---+ borders and space-aligned columns, +===+
     under the header row. Every | row must be exactly as wide as the
     border above it. Tables may run to 120 columns including the
     indent.
   o Blocks indented four or more spaces are preformatted - address
     maps, disassembly, diagrams - and are never rewrapped.
   o Every byte <= 0x7F. Write ->, -, ..., x, and +/-/| for diagrams.
     No em dashes, curly quotes, arrows or box-drawing glyphs.

   Two tools enforce it:

    py -3.10 tools/asciify.py --check    ASCII, width, table alignment
    py -3.10 tools/asciify.py --fix      transliterate and rewrap
    py -3.10 tools/rfcfmt.py  --fix P    render a markdown draft as RFC

   rfcfmt.py is a one-way converter, not a round-trip formatter. It
   refuses a file already in RFC form, because re-rendering its own
   output flattens the layout back into paragraphs. Author new
   documents in RFC form directly; use rfcfmt only to convert a draft,
   and read the result - it strips inline markup, so a passage ABOUT
   markdown comes out mangled.

   The table-alignment check exists because editing a cell without
   repadding the row is the easiest mistake in this format to make and
   the hardest to see in a diff. --fix does not repair those; widen or
   rewrap the cell by hand.

   The demos' own strings are not covered by the ASCII rule. They are
   data, read out of the binary, and get quoted exactly as they are -
   including the high-bit box characters a 1996 DOS intro will have put
   in its scroller.

2.  Evidence tagging

   Every claim carries a tag. Reverse engineering is mostly inference,
   and a document that does not separate what was read from what was
   reasoned is a document nobody can audit later.

   +------------+---------------------------------------------------------+
   | Tag        | What it means                                           |
   +============+=========================================================+
   | [verified] | read directly out of the binary or the Ghidra DB        |
   +------------+---------------------------------------------------------+
   | [inferred] | reasoned from verified evidence. State the evidence     |
   +------------+---------------------------------------------------------+
   | [observed] | seen running under an emulator, or in a capture         |
   +------------+---------------------------------------------------------+
   | [guess]    | a hypothesis worth testing. Never build on one silently |
   +------------+---------------------------------------------------------+

   [observed] is empirical but weaker than [verified]: a screenshot
   says what it looked like, not what the code does. It is stronger
   than [inferred] for anything visual, because no amount of static
   reading tells you which direction a tunnel scrolls.

   The thing [observed] is FOR is deciding what to go and read, and for
   catching a wrong colour. It is never a licence to implement the
   observation directly - see CLAUDE.md rule 12, which matters more
   here than on a sister project, because a 64k effect IS its
   constants.

   A rename in Ghidra is an assertion at [inferred] or better. Where it
   is not, the name carries maybe_ and the plate comment says what is
   unconfirmed.

3.  Which image an address is in

   This corpus is packed, so an address is meaningless until the image
   is named. CLAUDE.md rule 3 is the rule; this is what it looks like
   in a document.

   o Write the image, then the segmented address, as the database
     spells it:  packed 1be1:00cd,  unpacked 0a40:0112.
   o Where a document needs an address that cannot be misread - a table
     key, a cross-reference, the origin= line of a Ghidra type - give
     the linear form too:  packed 1be1:00cd (lin 0001bedd).
   o A bare address with no image named is a defect. Fix it in place;
     do not add a note saying which one it probably was.

   Remember that one linear address has many segmented spellings.
   1be1:00cd and 1000:bedd are the same byte, and Ghidra will show
   either depending on which block a listing came from. Quote the one
   you actually read, and give the linear address when it matters.

4.  One fact, one home

   The same fact in two files means one of them goes stale and only the
   other gets corrected.

   +------------------------------------------------------+---------------------------------------------+
   | What                                                 | Where it lives                              |
   +======================================================+=============================================+
   | addresses, instruction-level evidence, byte findings | the Ghidra plate comment                    |
   +------------------------------------------------------+---------------------------------------------+
   | struct offsets, sizes, strides                       | the Ghidra type, exported to struct_map.txt |
   +------------------------------------------------------+---------------------------------------------+
   | the evidence tag and origin address for a type       | the Ghidra type description                 |
   +------------------------------------------------------+---------------------------------------------+
   | what an effect does, and its maths                   | docs/<demo>/effects/                        |
   +------------------------------------------------------+---------------------------------------------+
   | what a subsystem does                                | docs/<demo>/subsystems/                     |
   +------------------------------------------------------+---------------------------------------------+
   | an asset format one demo uses                        | docs/<demo>/formats/                        |
   +------------------------------------------------------+---------------------------------------------+
   | a format the demos SHARE, like the packer            | docs/formats/                               |
   +------------------------------------------------------+---------------------------------------------+
   | why the C is shaped this way; divergences            | the src/ comment                            |
   +------------------------------------------------------+---------------------------------------------+
   | project rules                                        | CLAUDE.md                                   |
   +------------------------------------------------------+---------------------------------------------+

   Everything else points at that home - see xcursion/effects/
   tunnel.txt 3.2 -
   and does not restate it. A constant the code multiplies by keeps its
   number and its address inline; it does not also need a table in
   docs/ repeating it.

   Correct in place; never append. When a fact changes, edit the
   sentence that is now wrong. Leaving it standing with a correction
   beside it produces two statements, one of them false, and the reader
   cannot tell which.

5.  The effect note

   docs/<demo>/effects/<name>.txt, one per visual effect. This is the
   project's product and the only document type whose length is
   allowed to be generous, because the maths is the point.

   What it must answer, in this order: what you see, what the inner
   loop computes, where the constants came from, and what is still
   missing.

    PCDEMOS 64k                                                Analysis Note


                                 TUNNEL


    Abstract

       XCURSION, effect 3 of 7. Renderer at unpacked 0a40:0112
       (lin 0000a512), 214 bytes. Per-pixel table lookup into a
       precomputed angle/distance pair, no per-pixel divide.

       One paragraph on what the viewer sees.

    Table of Contents

       1. What it draws
       2. The inner loop
       3. Tables and constants
       4. Reconstruction notes
       5. Open questions

    2.  The inner loop

       One screen pixel per iteration, 320x200 in mode 13h.

           tex = angle[y][x] + frame * 3
           shd = dist[y][x]  - frame
           c   = (tex ^ shd) & 0x3f

       +-------+-------------+----------+--------------------------------+
       | Thing | Where       | Evidence | Notes                          |
       +=======+=============+==========+================================+
       | 3     | 0a40:0121   | verified | frame multiplier; the twist    |
       |       |             |          | rate. Decimal 3, not a shift   |
       +-------+-------------+----------+--------------------------------+
       | 0x3f  | 0a40:0135   | verified | palette is 64 entries, not 256 |
       +-------+-------------+----------+--------------------------------+

    3.  Tables and constants

       Every magic number gets a row and an address. A constant with no
       address is a rule 12 violation waiting to happen: it is the
       thing somebody will later tune to match a screenshot.

   The Evidence column is why this document exists rather than just the
   C file. Keep it doing that work.

6.  The struct note

   docs/<demo>/structs/<name>.txt, one per reconstructed type. The Ghidra type
   is the layout's home, so this document is for what the layout does
   not say: what the thing IS, who allocates it, how its fields relate.

    PCDEMOS 64k                                                Analysis Note


                                XCUR_EFFECT


    Abstract

       Size: 0x18 [verified] - from the table stride at
       unpacked 0a10:0040. Ghidra type applied: yes.

       One paragraph on what the thing is.

    1.  The layout

       +--------+------+-------+------------+-------------------------+
       | Offset | Size | Type  | Confidence | Notes                   |
       +========+======+=======+============+=========================+
       | +0x00  |   4  | void* | verified   | far pointer to the      |
       |        |      |       |            | render routine          |
       +--------+------+-------+------------+-------------------------+
       | +0x04  |   2  | u16   | inferred   | duration in frames;     |
       |        |      |       |            | only ever compared      |
       |        |      |       |            | against the frame count |
       +--------+------+-------+------------+-------------------------+

    2.  Open questions

       o +0x08..0x18 is untouched by the dispatcher.

   The layout table repeats what struct_map.txt already holds, and that
   is the one sanctioned duplication in this project: the offsets in
   the document carry the per-field EVIDENCE, which the type cannot.
   Keep the Notes column doing that work. A row that just restates the
   field name is a row to delete.

   On a 16-bit target, record whether a pointer field is near or far.
   A two-byte near pointer and a four-byte far pointer are different
   layouts and the decompiler will not always tell them apart.

7.  The subsystem and format notes

   docs/<demo>/subsystems/<name>.txt: what it does, the entry points
   with
   addresses, the types it owns, how it talks to the rest of the demo,
   and what is still not understood.

   docs/formats/<name>.txt - or docs/<demo>/formats/<name>.txt when
   only one demo uses it - is the same shape for a packer or an asset
   format, with the record layout and the size formulas.

   Proportion matters, and it cuts the other way from section 5. A
   subsystem note should be shorter than the code it describes. If a
   section is longer than the function it documents, it is too long.
   Prefer a table to a paragraph, a formula to a description of a
   formula, and silence to a restatement.

   The packer is the clearest case. It is not the demo, and a
   twenty-page study of a decompressor we only ever need to run once is
   twenty pages not spent on the effects.

8.  Record facts, not the journey

   Documentation says what the code does and why. It does not narrate
   the search that found it.

   Do not write "we first thought X, then found Y" - write Y. No "this
   took three sessions", no "an earlier note said", no restating what a
   previous section established, no summary of the summary, no mention
   of which tool was used.

   The one exception is a marked trap. If an approach looks obviously
   right and is actually wrong, say so in a sentence and say why. "The
   stub's INT 3 is an indirect jump through a vector it installed
   itself, not a breakpoint" is a fact about the problem. "I attached a
   debugger there first" is a diary entry.

   Keep an Open questions section in every document. An honest gap is
   more useful than a confident guess.

9.  Before calling a document done

    py -3.10 tools/asciify.py --check
    py -3.10 tools/structmap/structmap.py check

   The first is the format. The second is the maps, and it reports a
   type whose Ghidra description carries no evidence tag - which is the
   most common way a session ends with the work done and the provenance
   lost.

   Then two things only a reader can check:

   o Does every address name its image (section 3)?
   o Does every magic constant carry the address it was read from
     (section 5)?
