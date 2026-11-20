# PCDEMOS/64k

Reverse engineering three mid-1990s DOS 64k demos, and rebuilding their
visual effects from what the disassembly actually says.

The point of the repository is the **effects**: what each one computes,
the arithmetic it uses, and the constants it reads out of the binary.
Loaders, memory managers and file I/O are bounded and left alone. If you
have ever watched a 64k intro and wondered how a particular effect was
done, the answer is meant to be in here - written down in `docs/`, and
implemented twice so you can read it either way.

## The demos

| Demo | By | Released | Executable (bytes) | Status |
|---|---|---|---|---|
| [StickMan's World][smw] | Coma | Assembly 1995 | `UKKO.EXE`, 52,836 | C and Python ports |
| [EXCURSION][exc] | Vista | Abduction 1996 | `XCURSION.EXE`, 49,761 | 21 effects, C and Python ports |
| [LITHIUM][lit] | Vista | Juhla 1997 | `LITHIUM.EXE`, 63,281 | 19 effects, C and Python ports |

[smw]: https://www.pouet.net/prod.php?which=1139
[exc]: https://www.pouet.net/prod.php?which=1485
[lit]: https://www.pouet.net/prod.php?which=419

All three are 16-bit real-mode MZ images: hand-written 386 assembly,
320x200 mode 13h, with a tracker module for sound. `XCURSION.EXE` and
`LITHIUM.EXE` are packed with the same packer, byte for byte;
`UKKO.EXE` is PKLITE and shares none of it.

## The original executables are not included

They are not ours to redistribute, so `reverse/ghidra/` is empty in a
clone and **nothing here runs until you supply your own copies** of
`XCURSION.EXE`, `LITHIUM.EXE` or `UKKO.EXE`. Put them there and the
ports will find them.

Nothing is pre-extracted, either. The ports **depack the original in
memory at startup** and read the meshes, tables, palettes and music out
of the result, so what you see is driven by the 1990s file itself
rather than by assets checked in beside it. The depackers are
`src/xcurpack.c` and `src/pklite.c`, reimplemented from the binaries'
own decompressors and checked byte for byte against a known-good image.

## Where to start

- `docs/<demo>/README.txt` - that demo's facts, and what is still open.
  Read it before the effect notes; it says which addresses mean what.
- `docs/<demo>/effects/` - **one file per visual effect. This is the
  mission.** 41 of them across the three demos.
- `docs/<demo>/subsystems/` - the renderer, the meshes, the palette,
  the music replayer.
- `docs/formats/packer.txt` - the packer three of them share.

Every claim carries an evidence tag - `[verified]`, `[observed]`,
`[inferred]` or `[guess]` - so you can tell what was read out of the
disassembly from what was deduced or guessed. `docs/README.txt` defines
them.

## The reconstructions

There are two, deliberately. `sim/` is Python and exists to make an
effect visible in minutes; `src/` is the ANSI C port it graduates to.

```
py -3.10 sim/run.py xcursion          number keys pick the effect
py -3.10 sim/run.py xcursion --list   every part, numbered
```

The C builds with MSVC from `src/demos.sln` - an SDL2 target and a
dependency-free PPM dumper per demo - and with Emscripten for the
browser:

```
sh tools/build_web.sh lithium
```

`src/main_sdl.c` and `src/main_ppm.c` are the only files that contain a
loop.
Everything else renders into an 8-bit indexed framebuffer and knows
nothing about the host, which is what lets the same sources target
Windows and WebAssembly. The web build compiles with `-std=c89
-pedantic` and is the project's real C89 gate.

## How it is written

One C function per function in the disassembly, carrying the name the
Ghidra database uses, and a header comment on each one saying either
`ORIGIN <address>` - transcribed from the binary - or `OURS`. Fixed
point stays fixed point and integer widths keep the original's
overflow, because for these effects the wrap is usually load-bearing.

The rule that matters most: **an effect is implemented from the code,
never from a screenshot.** No constant, colour or timing is tuned until
the output matches a capture. Where a value has not been traced yet,
the reconstruction says so and looks wrong rather than guessing.

`CLAUDE.md` has the full working rules, and `tools/` holds the checks
that enforce them - ASCII and width, cross-references, comment
placement, and a check that the documents record what the binaries do
rather than the history of working on them.

## Layout

```
docs/     what we found, as RFC-style plain text
src/      the ANSI C reconstruction, plus the two hosts
sim/      the Python reconstruction
tools/    depackers, exporters and the checks
reverse/  the Ghidra project and the binaries  [not committed]
```

## Legal

A personal interoperability and preservation effort on 30-year-old
demoscene releases. The original executables are not redistributed and
no asset extracted from them is checked in. The notes, the tools and
the reconstructed source are our own work, under the MIT license in
`LICENSE`.
