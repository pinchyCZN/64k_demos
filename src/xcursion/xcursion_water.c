/* XCURSION - the water. effects/water.txt.
 *
 * Water_Step at img 0x29d4 is A 2D WAVE EQUATION SOLVER - the real
 * thing, in eleven instructions - and it serves two scenes through
 * two renderers. This file has the sync 0x2d one, the dripping water:
 * Water_Run calls the solver then Water_RenderSpans.
 *
 * THE GRID IS 160 x 100 SIGNED WORDS, not bytes and not 320 wide.
 * Each cell drives a 2x2 screen block.
 */

#include "xcursion.h"

/* ORIGIN img 0x29d4 - Water_Step.
 *
 *     new = mean(four neighbours) * 2 - old,  then damped
 *
 * is the discrete wave equation in its standard two-timestep form.
 * SUBTRACTING THE OLD VALUE IS THE SECOND TIME DERIVATIVE, and it is
 * why two buffers are needed rather than one: the neighbours come
 * from one and the old value from the other, and img 0x2a10 swaps
 * their roles at the end of every step.
 *
 * THE DAMPING IS A SHIFT, NOT A MULTIPLY - ax -= ax >> 4 is ax times
 * 15/16 at the static damping of 4. Without it the wave rings for
 * ever. There is no velocity field and no explicit timestep.
 *
 * IT RUNS ONE ROW PAST THE GRID. img 0x29de starts at word 160 and
 * img 0x29e1 counts 16000 cells, so it ends on row 100 of a 0 to 99
 * grid, and the +0x140 neighbour read reaches a row beyond that
 * again. Both buffers are padded for it rather than the loop being
 * tidied.
 */
void Xcur_WaterStep(s16 *dst, const s16 *src, s32 damping)
{
	s32 i;

	for (i = 160; i < 160 + 16000; i++) {       /* img 0x29de */
		s32 ax = (s32)src[i - 1] + (s32)src[i + 1]
		       + (s32)src[i - 160] + (s32)src[i + 160];
		s32 dx;

		ax = (s32)(s16)(ax & 0xffff);           /* a WORD sum */
		ax >>= 1;                               /* img 0x29f7 */
		ax = (s32)(s16)((ax - dst[i]) & 0xffff);
		dx = ax >> damping;                     /* img 0x29fe */
		dst[i] = (s16)(ax - dx);
	}
}

/* ORIGIN img 0x2a1a - Water_RenderSpans. The field drawn as COLUMN
 * SPANS, the same y-buffer technique the voxel landscape uses - so
 * the water is seen from the side, as a landscape.
 *
 * The shade is the NEXT COLUMN'S HEIGHT MINUS THIS ONE, shifted down
 * 7, plus the row index and floored at 0: a horizontal gradient,
 * which is a lighting term, plus depth.
 *
 * img 0x2a5c PUTS TWO ADJACENT PALETTE ENTRIES IN ONE WORD - the
 * shade and the shade plus 3 - and img 0x2a83 swaps them every screen
 * row, so each column is dithered between two shades rather than
 * flat.
 *
 * THE FIRST COLUMN IS OFF THE RIGHT EDGE. The counter runs 160 down
 * to 1 and the offsets are built from it directly, so the first pass
 * writes two bytes just past 64000. The demo's screen segment
 * swallows it; the framebuffer here carries the same slack the
 * countdown needs.
 *
 * Each field row is TWO screen rows, img 0x2a8b, and the y-buffer
 * only ever decreases - so every column fills bottom-up and nothing
 * is overdrawn.
 */
void Xcur_WaterRenderSpans(u8 *fb, const s16 *field)
{
	s32 col;

	for (col = 0xa0; col > 0; col--) {          /* img 0x2a29 */
		s32 dx = col * 2;
		s32 di = 0x7bc0 + dx;                   /* img 0x2a33, bytes */
		s32 ebp = 0xf8c0 + dx;
		s32 esi = 0xfa00 + dx;
		s32 cx;

		for (cx = 0x65; cx > 0; cx--) {         /* img 0x2a4a */
			s32 h = field[di >> 1];
			s32 nx = field[(di >> 1) + 1];
			s32 bx = (s32)(s16)((nx - h) & 0xffff);
			s32 lo, hi, y;

			bx >>= 7;                           /* img 0x2a53 */
			bx += cx;
			if (bx < 0) bx = 0;                 /* img 0x2a58 */
			hi = bx & 0xff;                     /* img 0x2a5c, BH */
			lo = (bx + 3) & 0xff;               /* img 0x2a5e, BL */
			y = ebp - (h << 4);                 /* img 0x2a65 */
			while (y < esi) {                   /* img 0x2a6f */
				s32 t;

				esi -= PD_SCREEN_W;
				if (esi < 0) break;
				fb[esi] = (u8)lo;               /* img 0x2a81, a WORD */
				fb[esi + 1] = (u8)hi;
				t = lo; lo = hi; hi = t;        /* img 0x2a83 */
			}
			di -= PD_SCREEN_W;                  /* img 0x2a87, one row */
			ebp -= PD_SCREEN_W * 2;             /* img 0x2a8b, TWO */
		}
	}
}

/* ORIGIN img 0x2a99 - the grid used to DISPLACE a still picture,
 * v * 321 along the screen diagonal. effects/theend.txt section 4.
 *
 * SUBTRACTING 0x600 IS A MODULAR WRAP, NOT A CLAMP: 0x10000 less
 * 0xfa00 is 0x600, so a sample off the top returns at the bottom. */
void Xcur_WaterRefract(u8 *fb, const u8 *src, const s16 *field)
{
	s32 di = 0, si = 160, row;                  /* img 0x2aa8 */

	for (row = 0x64; row > 0; row--) {          /* img 0x2aad */
		s32 col;

		for (col = 0xa0; col > 0; col--) {      /* img 0x2ab0 */
			s32 v = (s32)field[si] >> 3;        /* img 0x2ab6, SAR */
			s32 bx = (di + v * 321) & 0xffff;   /* img 0x2ab9 */

			if ((u32)bx >= 0xfd00) bx -= 0x600; /* img 0x2ac7 */
			fb[di] = src[bx];                   /* img 0x2ad1, a WORD */
			fb[di + 1] = src[(bx + 1) & 0xffff];
			fb[di + 0x140] = src[(bx + 0x140) & 0xffff];   /* img 0x2ad6 */
			fb[di + 0x141] = src[(bx + 0x141) & 0xffff];
			si++;
			di += 2;
		}
		di += 0x140;                            /* img 0x2ae7 */
	}
}
