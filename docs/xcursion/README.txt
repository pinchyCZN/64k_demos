PCDEMOS 64k                                                Analysis Note


                        XCURSION: THE DEMO


Abstract

   EXCURSION by Vista Software, for Abduction '96. The per-demo facts,
   the working method that applies to this demo only, and where it
   stands.

   CLAUDE.md holds the rules; this file holds everything true of
   XCURSION in particular, so CLAUDE.md does not grow by a section per
   demo. docs/layout.txt says why the split falls here.

A SEGMENT CONSTANT IN THE LISTING IS NOT A LIVE ADDRESS

   The unpacked image was dumped with the program based at segment
   0x1000, and its MZ relocations are already applied for that base.
   A running copy is based wherever DOS put it, so every segment
   literal in the listing - 0x2a79, 0x4a79, 0x5a79, 0x6a79, 0x7a79,
   0x8a79 - is that much too high at runtime [verified]:

       live_segment = listing_segment - 0x1000 + load_segment
       live_linear  = live_segment * 16

   The load segment is CS, which the debugger reports directly. At
   CS 0x1a5 the shift is 0xe5b paragraphs, so the texture the listing
   calls 0x2a79 is at linear 0x1c1e0 and NOT at 0x2a790.

   READING THE WRONG ONE DOES NOT LOOK WRONG. 0x2a790 is still inside
   the demo's memory and still holds plausible-looking field data, so
   a comparison against it yields a confident mismatch - a false "the
   generator diverges" against a generator that is byte-exact.

   Check it before any buffer comparison: read the segment registers
   and confirm the shift, rather than computing segment * 16.

Table of Contents

   1. The two programs
   2. Verified facts about XCURSION.EXE
   3. How the image divides
   4. The depack is done - work in XCURSION_U.EXE
   5. There is no dispatcher - Demo_Entry is the effect list
   6. Where it stands
      6.1. Comparing against the running demo
   7. What is still open

1.  The two programs

   +-----------------+----------+--------------------------------------+
   | Program         | Bytes    | What it is                           |
   +=================+==========+======================================+
   | XCURSION.EXE    | 49,761   | the packed original. Stub plus blob, |
   |                 |          | 3 functions. For cross-referencing   |
   |                 |          | the depack and nothing else          |
   +-----------------+----------+--------------------------------------+
   | XCURSION_U.EXE  | 108,432  | the depacked image. THE WORKING      |
   |                 |          | PROGRAM - name things here           |
   +-----------------+----------+--------------------------------------+

   XCURSION_U.EXE is not a file that shipped: it is XCURSION.EXE
   depacked by tools/unpack/, under a rebuilt MZ header.

   Ghidra split the packed file on import [verified]:

       HEADER   HEADER::0 - 7f          the MZ header, 8 paragraphs
       CODE_0   1000:0000 - 1000:be0f   48,656 bytes, the packed payload
       CODE_1   1be1:0000 - 1be1:03d0      977 bytes, the unpacker stub
       DATA     1be1:03d1 - 9000:2ff0   the BSS the header asks DOS for

   CLAUDE.md rule 3 is the rule that follows from this: an address
   means nothing until you say which image it is in.

2.  Verified facts about XCURSION.EXE

   All read out of the database or the file itself [verified].

   +--------------+-----------------------------------------------------+
   | format       | DOS MZ, 16-bit real mode, x86:LE:16:Real Mode       |
   | size         | 49,761 bytes, dated 1996-06-06                      |
   | sha256       | 611c304e54185cf09563253e06264a9cc813a1de75615056    |
   |              | 97f56b0d2ccaba6a                                    |
   | header       | 8 paragraphs (0x80); load image is file 0x80 to EOF |
   | relocations  | NONE - e_crlc is 0                                  |
   | entry        | CS:IP = 0be1:0000, which Ghidra bases at 1be1:0000  |
   | stack        | SS:SP = 0beb:0000                                   |
   | requested    | e_minalloc 0x76e1 paragraphs; e_maxalloc 0xffff     |
   | blocks       | 4, totalling 536,689 bytes with the BSS             |
   | functions    | 3 - the whole demo is inside the packed blob        |
   +--------------+-----------------------------------------------------+

   The packer itself is SHARED with LITHIUM.EXE and DRAIN.EXE, byte for
   byte - see docs/layout.txt section 2 and ../formats/packer.txt.

3.  How the image divides

   Three parts, and only the middle one is the demo:

       img 0000 - 003d    memory check, and its failure message
       img 003e - 0053    relocate-and-enter preamble
       img 0054 - 029b    the decompressor, 0x248 bytes, in the clear
       img 029c - be0f    compressed data, 48,372 bytes
       img be10 - c1e0    the entry-point wrapper - the MZ entry point

   `img X` is the load-image offset, file X - 0x80, and equals Ghidra's
   1000:X. The wrapper is Ghidra's 1be1:0000 onward. The entry chain:

       1be1:0000   JMP 1be1:00b0          the MZ entry
       1be1:00b0   JMP 1be1:00cd          thunk
       1be1:00cd   Unpack_WrapperEntry    the chain proper

   ../formats/packer.txt has the format, the wrapper's patch targets
   and how the depack works.

4.  The depack is done - work in XCURSION_U.EXE

   py -3.10 tools/unpack/xcur_unpack.py regenerates it, and the three
   checks that say it is correct are ../formats/packer.txt section 6.

   THE PACKER IS FINISHED BUSINESS. What is left on it is one open
   question worth having - a static reimplementation, CLAUDE.md rule
   3.1 option 1 - and no part of the mission depends on it.

5.  There is no dispatcher - Demo_Entry is the effect list

   The usual partition for a 64k intro is a dispatcher with an effect
   table. THIS DEMO DOES NOT HAVE ONE [verified]. XCURSION_U.EXE
   1000:0000 is a 2,618-byte linear script calling 38 routines
   directly, so that function IS the effect list, and bounding an
   effect means bounding its call from there.

   It tail-jumps to Demo_Timeline at 1000:4d6e, which runs the rest and
   jumps to Demo_Shutdown. Nothing returns. effects/timeline.txt is the
   whole part list.

   EVERY PART IS A BLOCK OF PARAMETERS, A g_AnimMode, A
   g_MusicPosTarget AND ONE RUNNER. The mode selects a per-tick handler
   out of g_AnimDispatch - subsystems/animation.txt - and the target
   says which music order position ends the part. There is no
   per-frame retrace wait anywhere; each renderer owns its loop and
   ends on the music.

   Landmarks worth searching for early in any demo of this era,
   because each one anchors a subsystem:

   +--------------------------------+---------------------------------+
   | Landmark                       | What it anchors                 |
   +================================+=================================+
   | INT 10h, AX=0x0013             | VGA mode 13h - the display      |
   | writes to 0xA000:0000          | the framebuffer, so every blit  |
   | OUT 0x3C8 / 0x3C9              | palette upload - fades, cycles  |
   | IN 0x3DA bit 3                 | retrace wait - the frame loop   |
   | INT 1Ch or 0x40:0x6C           | timing                          |
   | a 256- or 1024-entry table of  | a sine, atan or distance table  |
   | small words                    |                                 |
   +--------------------------------+---------------------------------+

   WHEN A CALL EDGE LEAVES THE EFFECT IN HAND, NOTE THE TARGET AND
   STOP. Following call edges depth-first turns a read that began on
   a tunnel effect into eight levels of memory allocator, every step
   locally sensible and the whole walk off-mission. Record the edge
   as an open question and return to the timeline.

6.  Where it stands

   Every renderer and all 29 animation handlers are read, and every
   scene in the demo has a mechanism - effects/scenes.txt maps the
   visible sequence onto the code.

       84 named functions, 24 still machine-named
       218 named globals
       25 parts in the sim

   The entry points, by subsystem:

       effects/timeline.txt      the part list, and which model is which
       effects/scenes.txt        the visible sequence against the code
       subsystems/animation.txt  g_AnimDispatch and all 29 handlers
       subsystems/palette.txt    seven palette builders and the fade
       subsystems/music.txt      the ISR, the S3M and the sync
       subsystems/unpacked_image.txt   the image map and the segments
       formats/mesh.txt          the model format and all six models

   THE C PORT HAS ITS SKELETON AND NONE OF ITS EFFECTS. src/ builds
   xcursion and xcursion_ppm from the same sources as the two UKKO
   targets, sharing main_sdl.c and main_ppm.c unchanged - the host
   contract is in src/host.h and the browser build in ../web.txt.
   Demo_Frame currently plots g_SinTable, which is a diagnostic and
   deliberately not an approximation of any effect.

   IT DEPACKS THE ORIGINAL AT RUN TIME and needs no file it does not
   ship. src/xcurpack.c is the static decoder for the packer all
   three of these demos share, verified byte-exact against this
   demo's dump and LITHIUM's - ../formats/packer.txt section 6.3.

   WHAT IS LEFT IS THE PALETTE AND THE EFFECTS. g_PaletteBase is zero
   in the image and the ramp is built at run time by img 0x3b65, so
   there is nothing to read - subsystems/palette.txt. Until that is
   transcribed the framebuffer is shown as grey, and Demo_Name says
   so, so it cannot be mistaken for finished.

6.1.  Comparing against the running demo

   A SNAPSHOT'S PALETTE DEPENDS ON WHAT CAME BEFORE IT. The colours
   live in the DAC, written by Pal_Fade a component at a time from a
   level that ramps across a part, and a state restored cold can come
   back carrying whichever palette the emulator last ran. The picture
   is right and the colours are not, which reads as a reconstruction
   fault when it is a capture artifact.

   So compare against a capture only after PLAYING INTO the scene
   from an earlier save, never after restoring straight onto it.
   Restoring an earlier slot and running forward to the target part
   gives the right palette where restoring the target's own slot
   does not [observed].

   The same applies to the screen itself: a snapshot restores RAM and
   registers but leaves the last drawn frame on the surface, so a
   capture taken before the part has drawn shows the previous scene.

7.  What is still open

   o maybe_Pal_Build2 at 1000:3bf4 and maybe_Pal_Build3 at 1000:3c1d
     are the last unread palette builders. maybe_Pal_Build2 is in
     force at sync 0x0c to 0x0e, which covers the first planet and the
     HUI rotozoom - so it decides those colours.

   o The helpers at img 0x21bb, 0x21e7 and 0x22cf fill g_DropPos for
     the water drips and have not been traced to a caller.

   o 24 functions are still machine-named. None is a renderer; they
     are the loaders, the memory manager and the music player's
     interior, which CLAUDE.md's mission statement puts out of scope.

   o A static reimplementation of the depacker - section 4.
