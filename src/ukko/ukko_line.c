/* UKKO - the stick figure's renderer, and the only thing that writes
 * pixels in the dance.
 *
 * docs/ukko/effects/stickman.txt.
 */

#include "ukko.h"

/* ORIGIN depacked 1020:0497 - Line_Draw.
 *
 * A DDA WITH NO ERROR TERM. The original holds the fraction in DL and
 * lets the carry out of `add dl, g_LineFrac` be consumed by the
 * following `sbb ebx, g_LineStepY`, so the slow axis steps exactly
 * when the fraction wraps - two instructions are the whole Bresenham
 * decision. g_LineFrac is (|dy| << 8) / |dx|, an 8.8 slope computed
 * at img 1020:057b.
 *
 * CLIPPING REJECTS THE PLOT, NOT THE STEP. Every bounds failure in
 * the original jumps past the store to the `add dl` that follows it,
 * so the walk continues off screen and re-enters in the right place.
 * A clip that skipped the step would bend the line, which is why the
 * bounds test is inside the loop here too rather than being hoisted
 * into a clipped endpoint pair.
 *
 * EBX IS Y AND INDEXES g_RowOffsets DIRECTLY in the original - 200
 * dwords of y*320 built by Demo_Main so a row costs one dword fetch
 * instead of a multiply. A flat framebuffer makes that moot, and the
 * table is not reproduced.
 *
 * The original is unrolled per direction with NOP pairs between the
 * bounds tests, one loop for each sign combination. The two
 * orientations here are the same decision.
 */
void Ukko_Line(u8 *fb, s32 x0, s32 y0, s32 x1, s32 y1, u8 colour)
{
	s32 dx = x1 - x0;
	s32 dy = y1 - y0;
	s32 sx = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
	s32 sy = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
	s32 adx = dx < 0 ? -dx : dx;
	s32 ady = dy < 0 ? -dy : dy;
	s32 frac, acc, i;

	if (adx == 0 && ady == 0) {
		if (x0 >= 0 && x0 < PD_SCREEN_W && y0 >= 0 && y0 < PD_SCREEN_H)
			fb[y0 * PD_SCREEN_W + x0] = colour;
		return;
	}

	if (ady <= adx) {
		s32 y = y0;
		frac = (ady << 8) / adx;
		acc = 0;
		for (i = 0; i <= adx; i++) {
			s32 x = x0 + sx * i;
			if (x >= 0 && x < PD_SCREEN_W && y >= 0 && y < PD_SCREEN_H)
				fb[y * PD_SCREEN_W + x] = colour;
			acc += frac;
			if (acc > 0xff) {
				acc &= 0xff;
				y += sy;
			}
		}
	} else {
		s32 x = x0;
		frac = (adx << 8) / ady;
		acc = 0;
		for (i = 0; i <= ady; i++) {
			s32 y = y0 + sy * i;
			if (x >= 0 && x < PD_SCREEN_W && y >= 0 && y < PD_SCREEN_H)
				fb[y * PD_SCREEN_W + x] = colour;
			acc += frac;
			if (acc > 0xff) {
				acc &= 0xff;
				x += sx;
			}
		}
	}
}
