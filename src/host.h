/* PCDEMOS 64k - the boundary between a demo and whatever is showing it.
 *
 * THE DEMO IS ONE FUNCTION THAT RENDERS ONE FRAME AND RETURNS.
 * Nothing under src/ except main_*.c may loop waiting for anything,
 * open a window, read a key, or sleep.
 *
 *     native   while (running) { Demo_Frame(); present(); }
 *     web      emscripten_set_main_loop(tick, 0, 1);
 *
 * emscripten cannot run a while (1): the browser needs the stack
 * unwound between frames or nothing is ever painted.
 *
 * ONE DEMO PER EXECUTABLE. Each .vcxproj links the hosts plus one
 * demo's sources, so these symbols resolve to whichever demo was
 * built. That is what lets DRAIN and LITHIUM join this tree without
 * touching a host.
 */

#ifndef HOST_H
#define HOST_H

#include "pd.h"

/* exe_path is the ORIGINAL packed executable. The demo depacks it in
 * memory and reads its assets out of the image - there is no unpacked
 * file on disk and none ships. Returns 0 on success. */
int  Demo_Init(const char *exe_path);
void Demo_Shutdown(void);

/* The default path, so a host can run with no arguments and the web
 * build can embed at the identical virtual path. */
const char *Demo_DefaultPath(void);
const char *Demo_Name(void);

/* ONE FRAME. Advances the clock, renders, returns. Never blocks. */
void Demo_Frame(void);

/* HOW OFTEN Demo_Frame WANTS CALLING. Each demo drove the PIT at its
 * own rate and ALL ITS MOTION IS PER TICK, so this decides whether a
 * part plays at the right speed. main_ppm.c ignores it on purpose and
 * runs one tick per dumped frame. */
s32  Demo_TickHz(void);

/* The frame just rendered, 8 bits per pixel, w*h bytes.
 *
 * IT STAYS INDEXED BECAUSE MODE 13h IS. The host resolves it through
 * Demo_Palette once per frame; doing it here would throw away the one
 * thing that makes these demos' palette tricks visible. */
const u8 *Demo_Pixels(s32 *w, s32 *h);

/* 256 entries of R, G, B - 768 bytes, each 0..0x3f as the VGA DAC
 * takes them. The host scales to 8 bits. */
const u8 *Demo_Palette(void);

/* Stereo s16, `frames` sample-pairs. Never blocks.
 *
 * PULL PER FRAME, NOT A CALLBACK. The host renders what the frame
 * needs and queues it, which dodges callback threading entirely and
 * is what emscripten wants. It is also what the original does -
 * UKKO's Music_Poll tops up a DMA buffer once a frame. */
void Demo_Audio(s16 *out, s32 frames);

/* The depacked image, for a host that wants to check it. NULL
 * before Demo_Init, or if the demo has no packed form. */
const u8 *Demo_Image(u32 *len);

/* The pointer, in FRAMEBUFFER pixels - not window pixels. A host
 * that scales or stretches its window has to map back before
 * calling. buttons: bit 0 is the left one. */
void Demo_Mouse(s32 x, s32 y, s32 buttons);

/* Keys a host may pass through, press AND release - a demo that
 * wants a held key cannot work from the press alone. A demo ignores
 * what it does not use. */
void Demo_Key(int ch, int down);

/* The wheel, in notches: positive is away from the user. A host
 * that has no wheel never calls it, and a demo that does not use
 * one answers with nothing - but every demo must ANSWER, because
 * the hosts are shared and the linker does not know which. */
void Demo_Wheel(s32 dy);

/* WHERE THE DEMO IS, in one line, for correlating a frame of the
 * reconstruction against a moment of the original. A host prints it
 * and never reads it - the keys are the demo's business.
 *
 * KEY=VALUE, SPACE SEPARATED, AND THE ORDER IS STABLE, so the same
 * line serves a person reading it and a script diffing two of them.
 *
 * IT MUST ONLY READ. A host calls it while the demo is stopped, so
 * anything it advanced would change what the viewer is looking at.
 *
 * Never null; a demo with nothing to say returns "". */
const char *Demo_StateLine(void);

/* THE SAME LINE IN MORE DETAIL, n = 0 upward until it returns null,
 * so a host loops without knowing how many there are or what any of
 * them holds. A state line says WHICH of the demo's parts is wrong;
 * these say which stage inside it, and only the demo knows what its
 * stages are.
 *
 * THE KEY ON A DETAIL LINE IS NOT t. A tool that indexes state lines
 * by tick must not pick these up as well, so they carry their own
 * first key and the same reader can skip them.
 *
 * Reads only, like Demo_StateLine. Null when there are no more. */
const char *Demo_StateDetail(int n);

#endif
