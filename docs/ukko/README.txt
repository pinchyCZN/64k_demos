PCDEMOS 64k                                                Analysis Note


                   UKKO - "StickMan's World"


Abstract

   UKKO.EXE, dated 1995-08-11, 52,836 bytes. A different group and a
   different lineage from the other demos here: it is packed with
   PKLITE, not the LZEXE derivative Xcursion, Drain and Lithium
   share, and it is two years older than Lithium.

   Its own title screen calls it "StickMan's World". A white
   line-drawn stick figure dances through four scenes over 694
   frames, then the demo exits - it does not loop. No texture
   mapping, no palette work, no 3D engine.

   ONLY THE FIGURE IS VECTOR. The lounge, the floor and the kitchen
   are full-screen 1-bit bitmaps, and the three text screens are
   single EGA bitplanes - subsystems/timeline.txt section 4.

   EVERYTHING ABOUT THIS DEMO IS SEGREGATED from the other three -
   see ../layout.txt section 4. It shares no packer, no engine and
   no conventions with them, and the only thing in common is the
   era.

Table of Contents

   1. The file
   2. The packer, and depacking it
   3. What the image contains
   4. Why it may not run under an emulator
   5. Status
   6. Open questions

1.  The file

   +------------------+------------------------------------------+
   | Field            | Value                                    |
   +==================+==========================================+
   | dated            | 1995-08-11                               |
   | size             | 52,836 bytes                             |
   | header           | 10 paragraphs, 160 bytes                 |
   | relocations      | 1                                        |
   | entry            | fff0:0100, ie. image offset 0            |
   | minalloc         | 0x6a4e paragraphs, about 435 KB          |
   | load module      | 52,676 bytes                             |
   +------------------+------------------------------------------+

   ONE RELOCATION IS THE TELL. A normally linked DOS program of this
   size carries dozens; a packed one carries the handful its stub
   needs. All [verified] from the MZ header.

   Ghidra splits the packed file as CODE_0 1000:0000 - 1000:cdc3 and
   DATA to 8000:72a3.

2.  The packer, and depacking it

   PKLITE 1.15, named in the clear at file offset 0x1d:

       !PKLITE Copr. 1990-92 PKWARE Inc. All Rights Reserved

   The version word at 0x1c is 0x210f - minor 0x0f is 15, and the
   0x21 is both the flag byte and the '!' that precedes the string
   [verified]. What the flag bits mean is [inferred].

   The stub is the stock PKLITE one. From the entry it compares the
   required paragraph count against the PSP's top-of-memory word,
   prints "Not enough memory$" and exits if short, then:

       ss = required + load_seg - 0x20
       es = required + load_seg - 0x45
       move 0x123 words from cs:0x144 to es:0
       far jump into the copy

   With a load segment of 0x1000 that puts the decompressor at
   0x86e0 [verified by emulation].

   THERE ARE TWO DEPACKERS AND THEY AGREE. tools/unpack/
   ukko_unpack.py runs the binary's own decompressor under Unicorn;
   src/pklite.c is a static transcription of that same decompressor
   and needs no emulator, which is what lets the port depack at
   runtime in a browser. The format is formats/pklite.txt.

   The static one is held to the emulated one by

       ukko_ppm -verify reverse/unpacked/ukko_preloc.bin

   which reports 483,083 bytes identical [verified].

   THE EMULATOR RUN: It is a SECOND script
   rather than a flag on xcur_unpack.py because none of that one's
   constants apply to a different packer.

       py -3.10 tools/unpack/ukko_unpack.py
           -o reverse/unpacked/ukko.bin
           --exe reverse/unpacked/UKKO_U.EXE

   Result [verified]:

       depacked entry   0020:0000 image-relative
       stack            0000:01f4 image-relative
       image            486,912 bytes

   HOW THE END IS DETECTED is the only subtle part. A code hook is
   installed over the LOW region only, and armed on the first output
   write. The decompressor writes that region throughout but never
   executes there, so the hook cannot fire until the final transfer
   into the depacked program - which is the moment the original CS:IP
   is live.

   THE STUB'S ENTRY SHARES AN ADDRESS WITH THE OUTPUT. 0ff0:0100 is
   linear 0x10000, which is also the load base, so a hook that is not
   armed on a write fires on the first instruction of the run.

3.  What the image contains

   By 32 KB block, proportion of nonzero bytes [verified]:

       0x000000   51.6%      the program
       0x008000   26.9%
       0x010000    4.6%      sparse from here
       0x020000    0.6%
       0x040000    3.3%
       0x058000   18.2%
       0x068000    0.0%
       0x070000   88.1%      the module

   THE MUSIC IS A SCREAM TRACKER 3 MODULE. 'SCRM' sits at dump offset
   0x0703ac, which puts the S3M header at 0x070380 [verified]. Its
   first instrument carries the filename "Longjamp.995" and the name
   "Longjam productions:", and the sample names that follow are of
   the form xNNNNNNx.000.

   THE SOUNDCARD MENU IS AT 0x000205 [verified], as plain text:

       Soundcard setup :
       1] Gravis Ultrasound      2] AdLib
       3] Sound Blaster          4] Sound Blaster Pro
       5] Sound Blaster 16
       BasePort : 2x0h           Irq (in hex) :

   The filler pattern "PINO!" repeats from offset 0.

4.  Why the figure never appears

   THE DEMO DRAWS WHITE ON BLACK IN A PALETTE WHERE BOTH ARE BLACK.
   It programs a greyscale ramp once, in which index 255 is full
   white, and then resets the DAC three times with INT 10h mode sets
   without ever reprogramming it. Everything in mode 13h - the four
   backdrops and the figure alike - is drawn in index 255, which the
   default VGA palette leaves black.

   THAT MATCHES THE REPORTED SYMPTOM EXACTLY: the music plays, the
   mode 0Dh logos appear, and every mode 13h scene is blank. The
   logos survive because mode 0Dh resolves its sixteen attributes
   through the attribute controller rather than the upper DAC.

   SO IT IS A DEFECT IN THE DEMO, not in the emulator, and neither
   the soundcard prompt nor the video mode is to blame.
   subsystems/video.txt.

5.  Status

   Depacked and imported. Startup read as far as the first frame -
   subsystems/startup.txt. The module is extracted -
   formats/music.txt.

   A side-scrolling bicycle game follows the demo. It is OUR
   invention, not a reconstruction, and it is described separately -
   game.txt.

   THE DEPACKED PROGRAM IS NAMED UKKO_U.EXE IN THE PROJECT, and that
   is the name every call must carry.

   AN IMPORT THAT PASSES AN EXPLICIT LANGUAGE GETS THE RAW-BINARY
   LOADER and a flat ram:0000 block with no MZ structure. Leaving the
   loader to choose is what gives the segmented form:

       CODE_0   1000:0000 - 1000:01ff
       CODE_1   1020:0000 - 8000:6dff
       DATA     8000:6e00 - f000:12df
       HEADER   HEADER::00000000 - HEADER::0000001f

   +---------------+--------------------------------------------+
   | Program       | What it is                                 |
   +===============+============================================+
   | UKKO.EXE      | the packed original, PKLITE                |
   | UKKO_U.EXE    | THE DEPACKED IMAGE - use this one          |
   +---------------+--------------------------------------------+

   THE FIGURE IS DRAWN AS LINES, one pixel at a time, by Line_Draw
   at 1020:0497 - effects/stickman.txt. It is a DDA with no error
   term, unrolled per direction, and it indexes g_RowOffsets by y.

   THE C PORT RENDERS THE WHOLE DEMO. src/ builds an MSVC/SDL2
   binary and a no-dependency PPM one from the same sources; the
   PKLITE container is transcribed in src/pklite.c and the visuals in
   src/ukko/. Verified frame for frame against sim/ukko/ by
   tools/ctest_ukko.py, and the depacker against the emulated one by
   ukko_ppm -verify.

   THE MUSIC PLAYS. ibxm/ac is handed the module where it sits in the
   depacked image - formats/music.txt - so one packed original is
   still the only input and nothing is written to disk. The hosts
   pull a frame of audio at a time rather than taking a callback,
   which is the shape the original uses too.

   What is left is the web build, which needs emsdk.

   Named so far: Demo_Main, Line_Draw, maybe_Line_SmoothEndpoints,
   maybe_Sound_Setup, Music_Driver, and the line, smoothing and
   soundcard globals - see effects/stickman.txt section 3.

6.  Open questions

   o Where the rotoscoped keyframes live. The sparse middle of the
     image is the obvious place to look, but no structure has been
     identified in it.

   o What paces the frame loop. One retrace a frame would run the
     demo in ten seconds and it takes ninety - subsystems/
     timeline.txt section 2.

   o Whether any VGA BIOS of the period left the upper DAC entries
     alone on a mode set, which is what the demo needs to be visible
     at all - subsystems/video.txt.

   o What the second half of the 0x058000 block holds. Its first
     part is g_ScreenSweat, which runs to 0x5d6e0 -
     subsystems/timeline.txt section 4. What follows, up to the
     module at 0x70380, is about 10 per cent nonzero over 252
     distinct byte values, so it is neither a screen nor a plane.
