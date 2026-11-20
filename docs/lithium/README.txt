PCDEMOS 64k                                                Analysis Note


                        LITHIUM: THE DEMO


Abstract

   LITHIUM.EXE, 63,281 bytes, dated 1997-03-08. Depacked, imported and
   analysed: 107 functions, and the first routines named.

   IT IS BUILT THE SAME WAY XCURSION IS - a linear startup script that
   stores parameters into a block of globals and calls one runner per
   part, with no dispatcher table. That was the open question most
   worth not assuming, and it came out the same.

   What is different is the sound: Lithium has a Gravis Ultrasound
   path that mixes in hardware, where XCURSION only ever mixes in
   software.

Table of Contents

   1. The two programs
   2. What the header says, and the shared packer
   3. The depack, and an entry that points at nothing
   4. Startup, in order
      4.1. What the port replicates, and what it can safely skip
   5. The landmarks
   6. The music
   7. What the strings say
   8. The Ghidra program needs a cleanup in the GUI
   9. The sim
   10. The C port
   11. Under the emulator
       11.1. Where the image sits
       11.2. Comparing a frame against the port
   12. What is still open

1.  The two programs

   +-----------------+----------+--------------------------------------+
   | Program         | Bytes    | What it is                           |
   +=================+==========+======================================+
   | LITHIUM.EXE     | 63,281   | the packed original. For cross-      |
   |                 |          | referencing the depack and nothing   |
   |                 |          | else                                 |
   +-----------------+----------+--------------------------------------+
   | LITHIUM_U.EXE   | 175,424  | the depacked image under a rebuilt   |
   |                 |          | MZ header. THE WORKING PROGRAM       |
   +-----------------+----------+--------------------------------------+

2.  What the header says, and the shared packer

   Read out of the file itself [verified]:

   +--------------+-----------------------------------------------------+
   | format       | DOS MZ, 16-bit real mode, x86:LE:16:Real Mode       |
   | size         | 63,281 bytes, dated 1997-03-08                      |
   | header       | 8 paragraphs (0x80); load image is file 0x80 to EOF |
   | image        | 63,153 bytes                                        |
   | relocations  | NONE - e_crlc is 0                                  |
   | entry        | CS:IP = 0f30:0000                                   |
   | stack        | SS:SP = 0f38:0000                                   |
   +--------------+-----------------------------------------------------+

   THE DECOMPRESSOR IS BYTE-IDENTICAL TO XCURSION'S [verified]. At
   image offset 0x54 to 0x29c, sha256 begins 9071a0554cd2 in
   LITHIUM.EXE, XCURSION.EXE and DRAIN.EXE alike. Over the whole first
   0x29c bytes only FOUR differ, which are the relocate-and-enter
   preamble's target segment and the compressed size. The string
   "!Not enough memory$" sits at image offset 0 in all three.

   ../formats/packer.txt is the format and applies here unchanged. Its
   one trap is worth repeating: the stub's INT 3 is a one-byte CALL,
   not a breakpoint, and stepping over it skips a stage.

3.  The depack, and an entry that points at nothing

       py -3.10 tools/unpack/xcur_unpack.py
           --in  reverse/ghidra/LITHIUM.EXE
           -o    reverse/unpacked/lithium.bin
           --exe reverse/unpacked/LITHIUM_U.EXE

   No per-file work was needed - no new stop address, no new stream
   offset, nothing but naming the input [verified]. The image comes
   out at 175,392 bytes; the recovered SS:SP is 9ad2:0800 and the
   bitstream starts at img 0x2a0, the same offset as XCURSION's.

   THE IMAGE BEGINS WITH 0x100 ZERO BYTES AND THE ENTRY POINTS AT
   THEM. This reads exactly like a failed depack and is not one
   [verified]:

   o The packed stream really encodes those zeros - a single zero
     literal followed by an overlapping match, which is how LZ77
     spells a zero run.
   o The handoff at the decompressor's CS:0x209 pushes CS then IP off
     the stream, and the stream says 1000:0000. packer.txt section
     3.3 is the instruction-level reading of that handoff.
   o The decompression loop's own stores reach down to img 0x0000, so
     nothing was left unwritten.

   The real code starts at 1000:0100. Execution gets there by falling
   through 128 copies of ADD byte ptr [BX+SI],AL, which is what the
   bytes 00 00 decode to. It survives because the handoff zeroes BX
   and SI and leaves DS at 0ff0, so all 128 of them poke 0ff0:0000 -
   below the image, and harmless.

   The other thing this file cost: ../formats/packer.txt section 6.1.
   The depacker used to end the image at its last non-zero byte, and
   Lithium's image ENDS IN its music module, whose last sample ends in
   silence. That silently cut 20,688 bytes off a dump that otherwise
   looked perfect.

4.  Startup, in order

   Demo_Main at 1000:0100, 918 bytes [verified]. Its plate comment in
   the database is the per-instruction version; this is the shape:

   +----------------------+--------------------------------------------+
   | Snd_CheckDmaBuffer   | keep the 8 KB mix buffer off a 64 KB       |
   |                      | boundary, at 1000:6df8                     |
   | PIC port 0x21 bit 1  | mask IRQ1, so the BIOS never sees keys     |
   | INT 10h AX=1130 BH=3 | the ROM 8x8 font -> g_RomFont8x8{Off,Seg}  |
   | 1000:012b            | expand g_SinQuarterSrc into g_SinTable     |
   | 4ad2:0000            | 64 KB filled with 0x80, a centred buffer   |
   | INT 33h AX=0         | mouse reset                                |
   | port 0x60            | poll for a key RELEASE - bit 7 is the      |
   |                      | break code                                 |
   | INT 10h AX=0013      | VGA mode 13h, at 1000:0188                 |
   | Pal_Upload           | the first palette                          |
   | INT 21h 35/25 08h    | steal INT 08h -> Music_TimerIsr            |
   +----------------------+--------------------------------------------+

   The table build is worth a note: 512 source WORDS at
   g_SinQuarterSrc become 32-bit entries at g_SinTable, written three
   times each - as read, negated at +0x1000, and copied at +0x2000 -
   and then a second loop walks the source BACKWARDS for another 512.
   That is quadrant mirroring, so g_SinQuarterSrc holds a quarter
   period and g_SinTable holds a full one [inferred].

   After that the file is a linear script: a block of stores into the
   1000:8f00 globals, then one call. Two idioms recur -

       g_MeshSrcOff and g_MeshNrmOut, then CALL Mesh_Load
       MOV AX,0xa000, then CALL 1000:2aa1

   The first is now read: Mesh_Load at 1000:503a expands a byte-packed
   3D model and builds averaged per-vertex normals - formats/mesh.txt.
   THREE models load before the first scene, and the order matters
   because every load writes its expanded vertices from offset ZERO of
   segment 0x2ad2 - 1000:504f is XOR DI,DI - so the work buffer ends
   up holding whichever came last:

       g_MeshSrcOff   g_MeshNrmOut   at
       0x4000         0x8000         1000:01c6
       0x72f8         0x8bb8         1000:01da
       0x41ea         0x9770         1000:01ee   the opening's rock

   The second idiom is Mem_ClearSegment, 1000:2aa1: ES from AX, then
   STOSD 0x4000 times, so it zeroes a whole 64 KB segment. It is only
   ever called from a scene's entry, NEVER from inside a frame loop.

   Two more calls close the script before the first scene:
   Terrain_Generate at 1000:52dd and 1000:708f, which arms
   g_MixRateShift and spins until the module starts.

4.1.  What the port replicates, and what it can safely skip

   Checked against a cold start halted at 1000:150b, the first
   scene's own entry [verified]:

   +---------------------+----------------------------------------+
   | g_PalFade           | 0xff01 - one tick past its stored      |
   |                     | 0xff00, which is the opening fade      |
   | g_PalFadeTarget2    | 0                                      |
   | g_PalFadeShift      | 0x0a                                   |
   | g_EnvMapAngle       | 0 - nothing writes it before this      |
   | g_MusicPos          | 0                                      |
   | g_PalSource         | 0x96be                                 |
   | g_MeshSrcOff / Out  | 0x41ea / 0x9770                        |
   +---------------------+----------------------------------------+

   SKIPPED AND PROVED HARMLESS FOR THIS PART:

   o  The other two Mesh_Loads. Each writes from offset zero and the
      rock is last, so the work buffer is the same either way.

   o  Terrain_Generate. It reseeds g_RndState to 0xa5b5/0xcab6, but
      it runs AFTER Tex_BuildSphereMap, so the map still comes from
      the stored seed - get that order wrong and the chrome changes.
      It writes 0x1e5f:0000 upward and leaves the back buffer full of
      heightmap: mean 120.9 with not one zero byte at scene entry.
      The rock at 0x41ea and the title at img 0x1930f both sit past
      what it touches.

   o  The 0x80 fill of 0x4ad2, which Terrain_Generate overwrites.

   o  The ROM font pointer, the mouse reset, the key-release poll and
      Snd_CheckDmaBuffer - host concerns, and no text in this part.

   THE BACK BUFFER IS NEVER CLEARED PER FRAME. The original relies on
   the geometry covering the screen, which it does throughout this
   part; the port clears it anyway, which is the same result here and
   safe where a later scene draws sparsely.

   ONE REAL OMISSION CAME OUT OF THIS. 1000:018d calls Pal_Upload and
   NOT Pal_Animate - the animation first runs inside the tick handler
   at 1000:6304 - so a port that animates during its own setup runs
   every palette phase one step ahead for the whole part. It showed
   as 94 advances against the demo's 93 at tick 93 and is fixed.

5.  The landmarks

   What the era-generic landmark search found [verified]:

   +---------------------------+--------------------------------------+
   | INT 10h AX=0013           | ONE site, 1000:0188 in Demo_Main     |
   | MOV AX,0xa000             | 15 sites - the framebuffer writers   |
   | OUT 0x3c8                 | ONE site, inside Pal_Upload          |
   | MOV DX,0x3da              | NO site                              |
   +---------------------------+--------------------------------------+

   The last row is a search for the immediate form only, so it does
   not prove there is no retrace wait - DX can arrive any number of
   ways. What it does say is that there is no per-frame wait written
   the obvious way, which is also true of XCURSION, where each
   renderer owns its loop and ends on the music instead.

   The demo is 386 code running in real mode: Demo_Main and Mesh_Load
   use ESI, EDI, EAX and EBP freely for 32-bit ARITHMETIC.

   THAT IS NOT EVIDENCE OF UNREAL MODE, and it was briefly recorded
   here as though it were. Every memory reference found so far is
   ordinary 16-bit seg:off - Mesh_Load indexes its 32-byte records
   with a 16-bit BX and reads dwords out of them, which plain real
   mode does. Snd_CheckDmaBuffer's SHL EAX,4 carries a 20-bit address
   through one boundary test and nothing more. Nothing has been found
   that loads 4 GB segment limits, so there is no basis for reading a
   32-bit index in a later effect as a flat offset.

6.  The music

   THE MODULE IS IN THE IMAGE BUT ITS SAMPLES ARE NOT. The S3M header,
   the order list, the instrument headers and all 39 patterns are
   there and parse cleanly; the sample data they point at is not
   audio, so a slice of the image plays and sounds like static.

   +-------------------+--------------------------------------------+
   | at                | img 0x1ad20, SCRM magic at 0x1ad4c         |
   | title             | "paska - exact/vst&toy/vst"                |
   | tracker           | Impulse Tracker 2.10, from cwtv 0x3210     |
   | span              | img 0x1ad20..0x2a62e, 63,758 bytes         |
   | structure         | 58 orders, 12 instruments, 39 patterns     |
   | named samples     | ISOMI.SMP 2944, HEMU.SMP 2416, 1.IFF 878   |
   +-------------------+--------------------------------------------+

   Every structural test passes [verified]. All 12 instrument headers
   carry the SCRS magic, every one is type 1 with pack 0 and no 16-bit
   or stereo flag, every parapointer lands inside the module, and the
   derived end is 0x2a62e against an image that runs to 0x2ad20.

   What fails is the content [verified]:

   +-----------------+--------------------------------------------------+
   | instruments     | 8, 9, 10, 11, 12 - EVERY BYTE ZERO               |
   | instruments     | 1, 2, 4, 5, 6 - mean byte-to-byte difference 74  |
   |                 | to 88 with entropy 6.7 to 7.8, which is          |
   |                 | compressed or encoded data, not PCM              |
   | playable        | 1 instrument of 11                               |
   +-----------------+--------------------------------------------------+

   8-bit PCM is SMOOTH, because consecutive samples correlate. Two
   uniform random bytes average 85 apart, so a mean difference near
   that is not a waveform. Both tests are needed and neither alone is
   enough: XCURSION's instrument 15 has a mean difference of 114 and
   is perfectly good audio, because its entropy is 2.7 - it is a noisy
   percussion hit bouncing between a few levels, where compressed data
   is rough AND high-entropy.

   AND THE SAMPLES ARE NOWHERE ELSE IN THE IMAGE EITHER. A sweep of
   all 175,392 bytes for anything audio-shaped finds 1,536 bytes, at
   img 0x09a00 - which sits just below g_SinTable and is a table, not
   a sample [verified]. The same sweep on XCURSION finds 13,312 bytes
   exactly where its instruments say they are.

   So the sample data is BUILT AT RUN TIME or decoded from somewhere
   compact, and it is not stored in any simple encoding: raw PCM,
   4-bit packing and delta coding were all tested against instruments
   1, 2 and 4 and none of them yields a smooth waveform [verified].

   THE ONE-TIME SUSPECT HAS BEEN RULED OUT. The idiom at every scene
   boundary - CS:[0x8f70] and CS:[0x8f74] handed to 1000:503a - was
   recorded here as a loader and as the first place to look. It is
   Mesh_Load, a 3D model reader, and has nothing to do with sound
   (formats/mesh.txt). So was the volume-table builder at 1000:6eed,
   which only scales sample bytes by a volume.

   What the sound code does contain so far is Snd_CheckDmaBuffer, the
   PIT setup in Snd_SetMixRate, that volume table, and Snd_Isr at
   1000:73a0 - all mixer plumbing, no sample production.

   THE SAMPLES ARE 3-BIT ADPCM AND THE DEMO EXPANDS THEM AT STARTUP.
   Nothing about the module is damaged, nothing is generated and
   nothing is loaded from disk: what the image holds is the
   COMPRESSED form, and Snd_ExpandSamples at 1000:8007 turns it into
   the playable one before a single frame is drawn. It is reached
   from Snd_CheckDmaBuffer at 1000:6e31, which is Demo_Main's first
   call.

       ES = 0x3ad2;  DS = 0x2ad2;  REP MOVSD 0x4000   the 64 KB copy
       di = the first instrument's sample pointer
       si = di + [di] + 4                             the bitstream
       index = 0x14;  value = 0x80
       until di wraps to 0:
           3 bits - two of magnitude, one of sign
           value += sign * magnitude * g_AdpcmStep[index]
           index += g_AdpcmStepDelta[magnitude]
       [di] bytes from di + 4 are then copied RAW to [di + 2]

   g_AdpcmStep at 1000:8188 is sixteen words, 1 to 108, and
   g_AdpcmStepDelta at 1000:81a8 is four: -2, -2, 2, 2. The running
   value is clamped to a byte and the step index to 0..0x1e, and
   three bits a sample is about 2.7 to 1 on 8-bit PCM [verified].

   SO THE MODULE EXISTS TWICE. Segment 0x2ad2 is the image's own copy
   with the stream still packed; segment 0x3ad2 is the expansion, and
   every GS load in the replayer - 1000:7471, 1000:758a, 1000:78da -
   reads that one. Slicing the module straight out of the image
   therefore gives ONE usable instrument of eleven, and expanding
   first gives all eleven.

   THE SAMPLE AREA OF THE IMAGE'S COPY IS THEN DEAD, which is why
   Mesh_Load can take it as scratch - 1000:5180 writes vertex normals
   there with DS at the module's base segment - without the music
   noticing. The collision is real and harmless.
   formats/mesh.txt section 5.

   WHAT LOOKED LIKE MISSING DATA WAS THE PACKED FORM. The five
   instruments reading as all zeros are the part of the stream whose
   OUTPUT lands past the image's stored data, and the five reading as
   high-entropy noise are the packed stream itself. The one that
   tested as PCM is instrument 3, whose 18 bytes are copied raw and
   never compressed.

   tools/unpack/lith_adpcm.py reimplements it and writes a module
   whose every instrument carries playable PCM, verified byte for
   byte against 63,758 bytes dumped from segment 0x3ad2 of the
   running demo. tools/extract_s3m.py reads the image and so still
   refuses; it is looking at the compressed form.

   AND ALL TWELVE INSTRUMENTS ARE PLAYED - the packed pattern data
   was walked and every note counted, the most used being instrument
   9 with 967 triggers [verified].
   [verified].

   XCURSION's module needs none of this - 10 of its 10 instruments
   carry playable PCM and it extracts cleanly. See
   ../xcursion/subsystems/music.txt.

   The replayer is on INT 08h - IRQ0, the PIT - not INT 1Ch, so it
   owns the timer and must chain or EOI itself. Music_TimerIsr at
   1000:627a is the vector; nothing inside it is read yet. Lithium
   also has a GUS hardware-mixing path, which XCURSION does not, and
   that is a second reason its sample handling need not resemble
   XCURSION's at all.

   A separate string at 1000:80b0 names "kypros.s3", which is a module
   FILENAME in a demo that has its module embedded. Unexplained.

7.  What the strings say

   A soundcard setup menu runs before the intro [observed, from the
   strings alone]: "No sound", "Creative Labs Sound Blaster 1.0 or
   compatible", "Creative Labs Sound Blaster 2.0 or above", "Gravis
   Ultrasound (GF1 hardware mixing, 64k of DRAM used)", each with a
   base address and most with a sampling rate, then "Exit back to OS"
   and "Start the intro". The GUS line is the interesting one: GF1
   hardware mixing is a different output path from anything in
   XCURSION, and the demo says so itself - "BTW, Ultrasound gives you
   the best sound in this intro."

   A credits and greetings table runs from 1000:90a1, and it has TWO
   layouts, not one [verified]:

   +---------------------+----------------------------------------------+
   | 1000:90a1 .. 9148   | the credits. 12-byte records: 11 characters  |
   |                     | padded with SPACES, then a NUL. The last two |
   |                     | records are blank placeholders               |
   +---------------------+----------------------------------------------+
   | 1000:9149 .. 92db   | the greetings. A 0x1f byte, then packed      |
   |                     | NUL-terminated strings of varying length,    |
   |                     | no padding at all                            |
   +---------------------+----------------------------------------------+

   The credits read as alternating role and name: LITHIUM, VISTA,
   MUSIC / TOX, MUSIC / EWACT, FONTS / JANGLER, CODE / TEO,
   MUSICPLAXER / "SZ BX TEO". Then roughly thirty group names.

   THE TABLE'S TEXT IS NOT ASCII AND MUST NOT BE READ AS IT STANDS.
   Three letters are systematically substituted:

   +---------+---------+------------------------------------+
   | Stored  | Means   | Seen in                            |
   +=========+=========+====================================+
   | X       | Y       | MUSICPLAXER, DOOMSDAX, HALCXON     |
   | W       | X       | EWACT, COMPLEW, REFLEW             |
   | Q       | W       | QE APPRECIATE, QILD LIGHT          |
   | Y       | Z       | nothing - no string stores a Y     |
   +---------+---------+------------------------------------+

   THE FIRST TWO ROWS ARE [verified] FROM A SECOND PLACE IN THE SAME
   BINARY. The credit table spells the two musicians TOX and EWACT;
   the S3M title at img 0x1ad20 spells the same two names in plain
   ASCII as "paska - exact/vst&toy/vst". So TOX is TOY and EWACT is
   EXACT, read off two independent sources that were authored by
   different tools.

   The third row is [observed] - QE APPRECIATE and QILD LIGHT only
   make sense as WE APPRECIATE and WILD LIGHT. The fourth is
   [inferred] from the sheet's shape and the pattern of the other
   three; no string in the binary stores a Y to confirm it.

   THE SUBSTITUTION IS THE FONT'S OWN ORDERING, AND THE ORDERING HAS
   NOW BEEN READ. Text_DrawString at 1000:2dd0 computes the glyph
   index as the character MINUS 0x41 and nothing maps in between, so a
   stored letter picks a SLOT and gets whatever the author drew there.
   Font_Unpack at 1000:0c59 expands the sheet and it decodes cleanly -
   effects/text.txt sections 5 and 6:

       THE SHEET IS ALPHABETICAL EXCEPT THAT IT HAS NO Q, AND W SITS
       IN Q'S VACANT SLOT.

   Which is why it cannot be undone by arithmetic. Everything after V
   is stored one place early, so W, X and Y each move by one; W itself
   moves by six because it was parked in the one free slot rather than
   appended; and A to V are untouched, which is why PARAGON, STATIC,
   AMOUR and VISTA all read correctly.

   So every name in this section can now be trusted, except that
   "SZ BX TEO" decodes only to "S? BY TEO" - the stored Z selects cell
   25, a spare past the end of the alphabet whose shape is not a
   letter.

8.  The Ghidra program needs a cleanup in the GUI

   The project currently holds TWO Lithium depacked programs, and the
   usable one has the wrong name:

   +------------------+------------------------------------------------+
   | LITHIUM_U.EXE    | A RAW-LOADER IMPORT. One flat segment,         |
   |                  | 0000:0000 - 2000:5c6f, the MZ header included, |
   |                  | so every address in it is 0x20 past the image  |
   |                  | offset. USELESS - delete it                    |
   +------------------+------------------------------------------------+
   | LITHIUM_U.EXE.0  | The MZ-loader import, CODE_0 based at          |
   |                  | 1000:0000. THE ONE THE WORK IS IN              |
   +------------------+------------------------------------------------+

   PASSING language= TO import_file FORCES THE RAW LOADER, which gives
   one flat segment with the MZ header included. OMIT IT AND GHIDRA
   PICKS THE MZ LOADER, which is what gives 1000:0000 and the
   CODE_0/DATA split. Either way auto_analyze needs no GUI prompt and
   no analysis dialog.

   Both files are locked by the open GUI, so the agent can delete
   neither. What is needed, once, in Ghidra: delete LITHIUM_U.EXE,
   then rename LITHIUM_U.EXE.0 to LITHIUM_U.EXE. Renaming keeps the
   functions and everything already deposited.

   UNTIL THEN THE MAPS CANNOT BE EXPORTED. reverse/exports/ is written
   by a Ghidra script, and a script runs against whatever program the
   GUI has active - CLAUDE.md rule 7.1. With the name duplicated,
   switch_program and open_program both report success and the active
   program does not move, so tools/export_maps.py fails loudly rather
   than writing a map set describing XCURSION. There is no Lithium
   directory under reverse/exports/ yet for that reason, and the
   database is the only copy of Lithium's names.

   A re-import is also due, because the program was imported from the
   154,704-byte dump that section 3 says was short. The missing bytes
   are the music module's silent tail, so no code is affected and
   every address in these notes stays valid.

9.  The sim

   py -3.10 sim/run.py lithium            and --list for the parts

   IT SHOWS THE DATA, NOT THE DEMO, because no renderer is read yet.
   Eight parts: the five models, the quarter-sine table, the mixer
   volume table, and what is known about the palette.

   The transcriptions in sim/lithium/parts.py are Pal_Upload, the
   sine-table expansion, the volume table, Mesh_Load, the three
   rotations, Mesh_Transform, Scene_ProjectVerts, the cull out of
   Scene_DrawFaces, Rnd_Next, Tex_Plot, Tex_Stroke, Tex_Blur,
   Tex_BuildSphereMap and Pal_Animate.

   SO THE CHROME IN THE VIEWER IS LITHIUM'S PIXELS. A model reaches
   screen coordinates through the demo's own code, the texel comes out
   of the demo's own generated environment map, the band offset is the
   demo's, and the palette is either one the image stores or the one
   Pal_Animate builds. Nothing in the colour path is invented.

   What is still ours and says so on screen: the spin, the camera
   distance, the sort DIRECTION, and the per-pixel u,v interpolation -
   the real span routine steps them from gradients Poly_Fill sets up
   and that setup has not been read. CLAUDE.md rules 12 and 15.

   Keys worth knowing: w cycles textured / env-map u / wireframe /
   points, b cycles the band offset 0x00, 0x40, 0xc0, and p cycles the
   three palettes.

   THREE CHECKS FELL OUT OF BUILDING IT. g_ObjScaleShift = 16 makes
   the position chain the exact identity on all five models, which
   exercises every rotation and shift in it. The env-map coordinates
   land inside a 256-wide texture without clipping, which is what says
   the +0x80 bias and the SAR 0x11 are read right. And the backface
   cull rejects about half the faces of each closed model, which is
   what a correct winding test does.

   One thing the sim surfaced that reading alone had not: THE MODELS
   ARE AUTHORED AT VERY DIFFERENT SCALES. The ships reach 80 units and
   the tower only 56 across twenty times as many vertices, so the
   per-object g_ObjScaleShift is not a detail - a scene cannot place
   these models without setting it.

10. The C port

   THE SKELETON IS BUILT AND NO PART IS IN IT.

   +----------------------------+---------------------------------+
   | src/lithium/lithium.h      | the interface                   |
   | src/lithium/lithium_assets.h | every image offset the port   |
   |                            | reads                           |
   | src/lithium/lithium_demo.c | the host contract, and          |
   |                            | Pal_Upload                      |
   | src/lithium.vcxproj        | SDL2 and web                    |
   | src/lithium_ppm.vcxproj    | the no-dependency test bench    |
   +----------------------------+---------------------------------+

   IT DEPACKS THROUGH src/xcurpack.c, which is XCURSION's and needs
   no change - the packer is shared byte for byte, docs/layout.txt
   section 7. That is checked rather than assumed:

       lithium_ppm -verify reverse/unpacked/lithium.bin

   reports all 175,392 bytes identical, which makes the unpacker a
   pass-or-fail question rather than an open one.

   WHAT IT DRAWS IS THE STORED PALETTE and nothing else - four
   64-entry rising ramps, which is what subsystems/texture.txt says
   is there. It proves the depack, the palette path and the present
   path together, and it is REPLACED by the first part rather than
   extended.

   PAL_MAIN IS ZERO IN THE IMAGE. Reading it gets a black screen,
   because Pal_Animate rebuilds it every frame; the two stored
   palettes are the ones at a 0x300 and 0x600 stride past it.

   WHAT IS TRANSCRIBED SO FAR:

   +----------------------+------------+---------------------------+
   | Pal_Upload           | img 0x3dcc | lithium_demo.c            |
   | Sin_TableBuild       | img 0x012b | lithium_tables.c          |
   | Mesh_Load            | img 0x503a | lithium_mesh.c            |
   | Vec_RotZ / X / Y     | img 0x35e1 | lithium_scene.c           |
   |                      | 0x362a     |                           |
   |                      | 0x3674     |                           |
   | Mesh_Transform       | img 0x4d2c | lithium_scene.c           |
   | Scene_ProjectVerts   | img 0x2e48 | lithium_scene.c           |
   | Scene_DrawFaces      | img 0x31ca | lithium_scene.c - the     |
   | pass one             |            | cull and sort only        |
   | Poly_ClearSpans      | img 0x3d64 | lithium_raster.c          |
   | Poly_Fill            | img 0x38a3 | lithium_raster.c          |
   | Rnd_Next             | img 0x3dfc | lithium_tex.c             |
   | Tex_Plot / Stroke    | img 0x597f | lithium_tex.c             |
   |                      | 0x5938     |                           |
   | Tex_Blur             | img 0x4aee | lithium_tex.c             |
   | Tex_BuildSphereMap   | img 0x5899 | lithium_tex.c             |
   | Pal_Animate          | img 0x6d49 | lithium_demo.c            |
   | Scene_RockIntro      | img 0x150b | lithium_rockintro.c       |
   | Tick_RockIntro       | img 0x6294 | lithium_rockintro.c       |
   | Clip_CountOutside    | img 0x5fda | lithium_scene.c           |
   | Image_UnpackRle      | img 0x2d10 | lithium_logo.c            |
   | Image_BlitScaled     | img 0x16d5 | lithium_logo.c            |
   | Image_Blur           | img 0x4b15 | lithium_logo.c            |
   | Screen_CrossFade     | img 0x185a | lithium_logo.c            |
   | Tick_RockIntro2      | img 0x6337 | lithium_rockintro.c       |
   | Pal_Upload           | img 0x3dcc | lithium_demo.c            |
   | Tick_Default         | img 0x6c8e | lithium_music.c           |
   | Demo_Main's transition| img 0x0201 | lithium_demo.c - as a    |
   |                      |            | state machine             |
   | Sort_ByDepth         | img 0x3e30 | lithium_scene.c - the     |
   |                      |            | direction only            |
   +----------------------+------------+---------------------------+

   THE RASTER IS THE ONE PIECE WITH NO SIM TO DIFF AGAINST, because
   sim/lithium/parts.py marks its own fill as ours and never read
   Poly_Fill. What stands behind it instead is that every input to it
   is byte-verified, and that a wrong edge walk or span divide shows
   as tears and gaps rather than as a slightly wrong picture.

   EACH ONE IS CHECKED AGAINST sim/lithium/parts.py RATHER THAN BY
   EYE. The C runs under the agent, its arrays are dumped by symbol,
   and Python compares them with the sim's own transcription -
   ../agent.txt. The sine table matches all 2,560 entries and the
   rock matches on all 130 vertices, 256 faces and 130 averaged
   normals.

   The pipeline was checked the same way at known angles - the rock
   at g_ObjAngle (0x111, 0x222, 0x333), a camera at (0x55, 0x66,
   0x77) - and all 130 transformed vertices, all 256 faces, all 130
   projections and all 141 surviving sort entries matched.

   THE COLOUR CYCLES, and it is the palette that cycles rather than
   anything in the picture: every entry is a linear ramp up from
   black and Pal_Animate animates the three SLOPES. Measured over one
   run of the port the lit mean goes silver, gold, deep red, blue
   across about 460 frames, with the three phases starting close
   together - which is why the first seconds are nearly grey.

   THE ROCK IS ON SCREEN, environment-mapped and tumbling, which is
   the first picture the port has drawn that is the demo's own data
   all the way through: the model, its averaged normals, the sphere
   map built from the stored seed, and the palette out of the image.

   THE RUNNER IS THE DEMO'S OWN NOW. Both slots, their six unshared
   rates, the depth gate that lets the second copy in and drops it
   again, and the env-map angle all come from Tick_RockIntro. What is
   on screen for the first four hundred frames is the warping chrome
   surface - the same mesh at four times the size, too close to read
   as a rock - and the second copy arrives around tick 453 and leaves
   near 828, which is the gate arithmetic and not a timer.

   THE TITLE IS OVER IT NOW, unpacked, blurred twice and blitted
   scaled - and it is ONE PICTURE HOLDING BOTH WORDS, of which the
   blit takes half. effects/rockintro.txt section 5.

   THE OPENING IS COMPLETE AND IT ENDS WHERE THE DEMO ENDS IT, on
   tick 2,085 of 2,085. The two mesh copies, both motion phases, both
   words, the cross-fade that is the only thing reaching the screen,
   and now the fade out and the exit.

   WHAT MADE THE ENDING POSSIBLE WAS THE TICK CHAIN, not a replayer.
   Every scene's handler ends JMP Tick_Default rather than returning,
   and Tick_Default is where the palette fade lives, so a port that
   called only the scene's own handler could never end any part -
   the runners test g_PalFade. lithium_music.c keeps that tail and
   the module's ORDER POSITION, which is the only thing any scene
   reads from the replayer; Demo_Audio is still silent.

   ONE ORDER IS 64 ROWS x THE MODULE'S STORED SPEED OF 6 = 384 TICKS,
   whatever the tempo, so the order boundaries are fixed without
   knowing a frame rate. Trapped under the emulator they land on 1156
   and 1540, and the predicted exit of 2,084 came out at a measured
   2,085 - effects/rockintro.txt section 6 for the arithmetic and
   subsystems/music.txt section 5 for the clock.

   THE OPENING FADES UP OUT OF BLACK OVER 256 TICKS and that is the
   stored g_PalFade, not anything measured. Demo_TickIsr skips the
   entire tick chain - replayer, scene handler and fade alike - while
   g_MixRateShift is zero, and img 0x708f arms it three ticks before
   the runner, so Demo_Main's setup passes without one tick and the
   part begins at the image's own 0xff00. effects/rockintro.txt 7.

   WHILE THAT HAPPENS THE INDEX BUFFER IS FULLY DRAWN - 64,000
   non-zero bytes at tick 13 - and only the DAC is black. Comparing
   resolved pictures here would have hidden it; indices and palettes
   have to be compared separately.

   THE PART RUNS AT 57 Hz AND THE HOST USED TO PACE IT AT 70. Every
   position, angle and palette phase is per tick, so a 23 per cent
   fast clock moved the whole sequence together - the rock arriving
   early, the palette racing, the logo pulsing half again too often -
   without any one of them looking individually wrong. Demo_TickHz in
   ../host.h is now the demo's own, and LITH_TICK_HZ carries where 57
   came from.

   TWO MORE PARAMETERS WERE WRONG AND BOTH CHANGED WHAT THE OPENING
   LOOKS LIKE RATHER THAN WHERE ANYTHING IS:

   o  THE TITLE'S FADE IS A COSINE. img 0x17e2 reads the sine table
      at 0xa8b2 where its base is 0xa0b2, and that quarter turn is
      the difference between a word that starts INVISIBLE and rises
      to meet the rock at tick 453, and one that starts half lit and
      is gone before the rock arrives. effects/rockintro.txt 5.

   o  TEX_PLOT'S BOUNDS ARE JNC, SO >= AND NOT >. Admitting an
      origin at exactly 255 put extra ink on the map's far edges -
      10.4 per cent of it, every byte high by 1 to 9 - and a uniform
      +1 through every frame. subsystems/texture.txt 3.1. The port's
      sphere map is now byte-identical to the demo's across all
      65,536.

   TWO PARAMETERS WERE WRONG AND BOTH WERE IN THE RUNNER'S HEAD,
   which is the easy place to miss one: g_SlotAngZ[8] starts at 0xa00
   and g_ObjAxisSwap is set once for BOTH slots. The axis swap also
   had its sense inverted - effects/rockintro.txt section 6.1 - and
   an inverted swap is not visibly broken, it just moves the chrome
   highlight to the top of the screen.

   HOW CLOSE IT IS NOW, against the demo's own back buffer at 0x4ad2
   read out of DOSBox at a frame boundary - section 11.2 for why that
   qualification carries all the weight:

   +------+----------+----------+------------+-------------+
   | save | ISR tick | ours     | mean error | bytes exact |
   +======+==========+==========+============+=============+
   | 0    |       93 |       91 |     0.0000 |     100.0 % |
   | 2    |      426 |      424 |     0.0077 |      99.7 % |
   | 3    |      506 |      504 |     0.0253 |      99.3 % |
   +------+----------+----------+------------+-------------+

   SLOT 0 IS EVERY ONE OF ITS 64,000 BYTES. That is the BACK buffer -
   the frame as rendered. The VISIBLE buffer is a quarter-step blend
   of it and lands at 1.42, because the blend's time constant follows
   the original's frame rate and that varied; effects/rockintro.txt
   section 7 is the whole of it, and it is what the opening's fade-up
   out of black is made of. With the axis swap
   inverted the same measure is 15.3 and with it off 9.3, which is
   how that was found.

   WHAT THE REMAINING 0.3 AND 0.7 PER CENT ARE is the original's own
   frame disagreeing with itself. The runner transforms slot 4, then
   slot 8, then blits the title, and the interrupt advances the
   globals throughout - so one of its frames mixes two or three
   ticks, and the residual sits exactly where the later-sampled
   pieces are: the second rock and the title box, dead centre. Slot 0
   is byte-perfect because at tick 91 only slot 4 is drawn and the
   title contributes nothing. A tick-locked port cannot reproduce
   that skew without reproducing a particular CPU's speed.

   The port's palette store matches the demo's byte for byte, and so
   do all six slot angles, both slot depths, the logo zoom and the
   blit phase.

   THE OPENING NOW HANDS OVER instead of stopping. Demo_Main's
   script between the parts is four fades and one wait on the music,
   all of it g_PalFade - effects/transition.txt. The port runs it as
   a state machine because Demo_RunUntil blocks: black, then the
   generators' pause, then a ramp up past zero until the cleared
   screen is white, held until order 8, and then the flyover's own
   fade walking that white away. What is behind the white is the
   flyover, and that is not transcribed.

   THE PALETTE PATH HAD TO SPLIT TO GET THERE. Pal_Animate rebuilds a
   STORED palette - the original's is img 0x96be, which is why
   LITH_PAL_MAIN reads as zeros in the image - and only Pal_Upload
   reaches the DAC, applying g_PalFade as it goes. The port had the
   two collapsed into one write to the DAC, which left no place for
   a fade to act.

   THAT IS THE POINT OF HAVING BOTH. The sim is the reading of the
   disassembly and the C is a translation of the sim, so a
   disagreement is a translation bug and lands in one of three
   places: the truncating divide, a 16-bit accumulator that must
   wrap, or the byte sign of a vertex.

   The browser build is sh tools/build_web.sh lithium - ../web.txt -
   and is the only thing that enforces C89 on this code.

11. Under the emulator

11.1.  Where the image sits

   THE IMAGE LOADS AT SEGMENT 01a5, so a docs address img X is the
   live address 01a5:X and the linear address 0x1a50 + X [observed].
   Proved both ways: Pal_Upload, img 0x3dcc, is the only match for
   its first sixteen bytes in the whole first megabyte and sits at
   linear 0x581c, and 0x581c - 0x3dcc is 0x1a50, which is exactly
   what csbase reads while the demo runs.

   THAT SEGMENT IS NOT A CONSTANT OF THE DEMO. It is where DOS put
   the program, so a different emulator configuration or a resident
   driver moves it. Re-derive it rather than trusting it: search RAM
   for the first bytes of a routine whose image offset is known and
   subtract.

       Demo_Main    img 0x0100   e8f56ce4210c02e621b83011b703cd10
       Pal_Upload   img 0x3dcc   2e8b1e558fc1fb02bac80332c0ee42b9
       g_SinSource  img 0x1a535  00000c001900250032003e004b005700

   A DEMO SEGMENT IS NOT A GUEST SEGMENT. The immediates in the
   listing - 0x4ad2, 0x5ad2, 0x6ad2, 0x1e5f - are relative to
   Ghidra's base of 0x1000, so each one is image offset
   (S - 0x1000) * 16. Going live then adds the load base:

       image offset   =  (S - 0x1000) * 16
       guest linear   =  0x1a50 + image offset
       guest segment  =  S - 0xe5b

   +----------+-------------+------------+----------------------------+
   | Demo seg | img offset  | guest seg  | What                       |
   +==========+=============+============+============================+
   | 0x1e5f   | 0x0e5f0     | 0x1004     | the assets: heightmap at   |
   |          |             |            | +0, the title RLE at       |
   |          |             |            | +0xad1f                    |
   | 0x4ad2   | 0x3ad20     | 0x3c77     | the scaled blit's target   |
   | 0x5ad2   | 0x4ad20     | 0x4c77     | the title card, blurred    |
   | 0x6ad2   | 0x5ad20     | 0x5c77     | the sphere map             |
   | 0x7ad2   | 0x6ad20     | 0x6c77     | cleared by the runner      |
   +----------+-------------+------------+----------------------------+

   ADDING THE LOAD SEGMENT TO A DEMO SEGMENT LANDS ONE BUFFER LATE
   AND LOOKS RIGHT. S + 0x1a5 gives 0x5c77 for 0x5ad2, which is a
   real buffer holding smooth plausible data - the sphere map - so
   the error reads as success. The buffers are 0x1000 paragraphs
   apart, which is exactly the base Ghidra uses, and that is the
   coincidence. Subtract 0xe5b, not add 0x1a5.

   A SCREENSHOT AND A FRAMEBUFFER DUMP ARE DIFFERENT CAPTURES, and
   only one of them is restored by a snapshot.

   +-----------+------------------------+--------------------------+
   | dos_shot  | the SDL window surface | NOT in a slot. Stale     |
   |           | - scaled, already      | until the guest          |
   |           | through the DAC, RGB32 | repaints                 |
   | dos_dump  | VGA memory at 0xa0000, | IN the slot. Correct     |
   | @a0000    | 8-bit indices as the   | the instant the load     |
   |           | CPU sees them          | returns                  |
   +-----------+------------------------+--------------------------+

   SO A SNAPSHOT LOAD DOES NOT REPAINT THE WINDOW. A screenshot taken
   straight after loading a slot shows the PREVIOUS slot, and three
   different ones look identical; running a few frames first fixes
   it. Dumping 0xa0000 needs no frames at all - measured, two slots
   loaded back to back give different CRCs with nothing run between.

   NOTHING NEEDS ADDING TO THE SLOT. The pixels are already saved; it
   is the window that is behind, and the window is a consequence of
   them rather than a second copy worth keeping.

   THE PALETTE IS NOT IN THE FRAMEBUFFER, and reading them apart sees
   THROUGH A FADE. Slot 0 is near-black on screen because g_PalFade
   has pulled the DAC down, but its 64,000 bytes hold the whole chrome
   surface at indices 8 to 56. Render those against a flat ramp and
   the image is there in full. A capture of the window could never
   show that.

   The demo's own DAC copy is g_PalMain at img 0x96be, 768 bytes of
   6-bit components, and Pal_Upload biases it by g_PalFade >> 2 at
   img 0x8f55 on the way out - effects/rockintro.txt.

11.2.  Comparing a frame against the port

   STATE AT A TICK, NOT PIXELS AT A FRAME. The original draws every
   second tick, so its frame N and the port's are not the same picture
   even when the state matches - section 11.2's second artifact below.
   Three pieces make the comparison a number rather than a look:

       lithium_ppm -states -n 4400 > build/states.txt
       dos_dump @9d82 3256 build/s6.bin
       py -3.10 tools/statecmp.py build/s6.bin build/states.txt --drift

   The port's half is Demo_StateLine, src/lithium/lithium_state.c -
   one key=value line per tick, printed by the window on SPACE and on
   the single-step key, by the agent on STEP and PAUSE, and by -states
   for every tick at once. statecmp.py reads the SAME keys out of
   guest memory, so the comparison is a dict diff.

   --drift SWEEPS OUR TICKS EITHER SIDE and reports the offset that
   lines the two up, scored on position alone - an angle wraps and a
   counter is discrete, so either would fill the sweep with false
   minima. A path that is the right shape but the wrong phase reads as
   a path bug and is not one; this says which.

   A BEST FIT IS ONLY A MEASUREMENT WHILE THE STATES STILL MATCH.
   Once they diverge every offset scores about the same, and the
   winner is noise that reads as the phase having moved. The tool
   says which it has: EXACT, a SHARP minimum, or no usable one.

   WHAT IT FINDS IN THE SURFACE FLYOVER: nothing, at offset ZERO.
   At demo ticks 2599, 3635, 3660 and 3961 every position field,
   every model-owned angle, the order and the credit index agree to
   the bit with no offset at all [verified]. The scene begins on the
   same tick as the original's - effects/flyover.txt 4.1.

   A SLOT IS COMPARED ON WHAT THE FLIGHT MODEL OWNS, NOT ON THE LIVE
   COPIES. g_SlotAngle* and g_SlotPos* are rewritten twice a frame,
   so a snapshot of them is a point in a frame rather than a tick and
   reads one ease step - 30 to 50 units - out against a model that
   agrees exactly. Compare g_SlotHome*, g_SlotAngleA* for the camera
   and g_SlotPitch and g_SlotYaw for a flown craft;
   effects/flyover.txt section 4.1 and tools/statecmp.py say which
   addresses those are.

   WHAT THE BISECTION ACTUALLY FOUND was the first tick on which the
   camera executes command 0x18 - the chase cam's only appearance in
   the scene and therefore the last branch of Flight_Step to go
   untested. Its trail distance is 0x60 and the instruction spells
   it 0xffffffa0; effects/flight.txt 6.2.

   TWO WAYS A BISECTION ON THIS SCENE MISLEADS. Scoring on position
   alone names the tick a wrong POSITION appears, which is one tick
   after the branch that caused it ran; and the live angle copies
   above make the same tick look like an angle fault in the craft
   when the craft is exact and the camera is not.

   AND A SHARED WRONG CLOCK HIDES ITSELF. The dating tool and the
   port walked the order list the same mistaken way, so a dump dated
   by one and compared against the other lined up at a constant
   offset and read as a small transition overrun - the real error
   was 192 ticks in the meaning of g_MusicPos and it cancelled on
   both sides. An offset that is CONSTANT across a whole scene is
   evidence about the clock, not about the scene.

   g_FocusEased IS A SEPARATE DIVERGENCE AND THE ONE THAT REMAINS.
   It eases once per FRAME, inside Scene_DrawFaces at 1000:32fe,
   while the model above runs once a tick and the original's frame
   rate is set by how long the scene takes to draw - section 11.2. A
   value eased at a rate we cannot reproduce keeps a residual lag
   proportional to how fast the ground moves underneath, so it does
   not decay away. It shows as orgx and orgz 20 to 50 out while
   everything else is exact.



   TWO ARTIFACTS SWAMP THE REAL ERROR AND BOTH LOOK LIKE PORT BUGS.
   Measured against the same frames, correcting for them takes the
   mean error from 2.0 to 0.04.

   o  A HALT LANDS MID-RENDER. 1000:3de8, 1000:3bf6 and 1000:5ffc
      are all inside the polygon loop, so 0x4ad2 holds a part-drawn
      frame. Break at 1000:16b9 - the call to Screen_CrossFade - and
      the back buffer is complete and not yet consumed.

   o  THE GEOMETRY LAGS THE ISR BY TWO TICKS. The palette phases are
      the reliable tick index - they are 0xd, 0x10 and 0x15 times it
      exactly - but they are advanced by the interrupt while the
      frame is still drawing, so a frame finishing when they read
      N was built from the globals of N-2. The lag is 2 at every
      point measured.

   o  A BREAKPOINT ON A CALL THAT IS JUMPED OVER NEVER FIRES, and
      the debugger does not say so - cont returns at once and the
      guest runs free, so a dump taken after it is a race rather
      than a reading. 1000:1b50 is such a site: the call to
      maybe_Sky_DrawPoint is gated on 1000:8f5a and the scene never
      sets it. READ THE REGISTERS BEFORE EVERY DUMP; an eip that is
      not the breakpoint is the whole of the warning.

   o  A SNAPSHOT RESTORES THE RUN STATE AS WELL AS THE MEMORY. Load
      one that was saved running and it carries on running, so a
      breakpoint set afterwards catches a LATER frame - and a before
      and after pair taken that way comes from two different frames
      with the camera moved between them. Both halves of a
      comparison have to come from one run: break at the first
      point, dump, then break at the second and continue.

   THE SECOND ONE IS A FACT ABOUT THE ORIGINAL, not about measuring
   it: a frame takes LONGER THAN A TICK to draw, so motion is
   computed for ticks that are never seen. The port renders every
   tick, so it is SMOOTHER than the original on that machine, and
   matching it frame for frame would mean reproducing a particular
   CPU's speed.

   HOW MUCH LONGER IS A PROPERTY OF THE EMULATED CPU, so it is a
   setting and not a fact about the demo, and it is linear in the
   cycles: one frame a tick needs about 112,000 at the flyover's
   start and 142,000 where the draw costs most. At the 35,000 this
   project started with, the scene draws one frame every three or
   four ticks. subsystems/raster.txt section 3.3 has the table.

   MEASURE THE RATE, DO NOT READ IT. Neither dosbox.conf nor what
   the debugger reports follows a cycles changed from the GUI or
   with Ctrl+F12, and the GUI's own number is not the live value
   either: a reported 35,000 has measured 90,000. Watching
   g_FocusEased at 1000:32fe against g_CreditTick at 1000:63fb is
   the only honest source.

   NOTHING TICK-DRIVEN MOVES WITH IT. The flight model, the music
   and the credit roll are exact at every speed measured, so the
   setting changes the smoothness and one accumulator, and nothing
   else. A reading taken at one speed stays valid at another.

   The palette phases also give the tick without arithmetic on the
   zoom: read the three words at img 0x8ee8 and divide by 0xd.

12. What is still open

   o THE EFFECTS. Nothing visual is read at all. The 15 MOV AX,0xa000
     sites bound the framebuffer writers; which of them are distinct
     effects is unknown.

   o WHAT DRIVES A SCENE: g_ObjAngle*, g_CurPos*, g_CamPos*,
     g_SlotAngle*, g_ObjFaceAttr and g_PalSource. Every mechanism they
     feed is now read, so what is left is the animation - and the
     animation is what a scene actually IS. This is the next thing
     worth doing.

   o Whatever clears the two scene pools per frame. Scene_DrawFaces
     resets g_SortListPtr itself, but g_VertPoolCount and
     g_FacePoolCount only grow in everything read so far.

   o The other two texture segments. 0x7ad2 holds the 128x128 planar
     map and 0x5ad2 is the one whose presence triggers the planar
     second pass; nothing found yet writes either.
     subsystems/texture.txt section 8.

   o Palette entries 129 to 255 under g_PalMain, which Pal_Animate
     leaves black, and the fourth g_PalSource value 0x9e71, whose
     bytes run past the DAC range.

   o 1000:2aa1, taking a destination segment in AX. Its plate comment
     states the interface and nothing more.

   o Whatever relocates the S3M module off segment 0x2ad2 before the
     first Mesh_Load, which is the best lead on the missing sample
     data - section 6 and formats/mesh.txt section 5.

   o The font's glyph list, which decodes section 7's table. The route
     is now known: effects/text.txt section 5. The font is expanded
     from 0x1e5f:0xad1f by maybe_Asset_Expand at 1000:2d10, so reading
     that routine decodes it.

   o Music_TimerIsr's body and Snd_Isr's, and whether the graphics
     sync on a music order position the way XCURSION's do.

   o Structs, unions and enums take the Lith_ prefix. CLAUDE.md rule
     6.1. None exists yet - the 32-byte Mesh_Load work record and the
     model header are the two that are now understood well enough to
     define.
