/* XCURSION - the beams. effects/beams.txt.
 *
 * Beams_Render at img 0x317a, runner Beams_Run at img 0x260f.
 *
 * IT IS NOT A ROTOZOOM, however much its first four instructions look
 * like one. It opens exactly as Roto_Render does and drives off the
 * same two globals, but it loads no texture: the sine pair is a
 * ROTATED GRADIENT, and the screen is its own state.
 */

#include "xcursion.h"

/* ORIGIN img 0x317a - Beams_Render.
 *
 * One 16-bit accumulator carries a plane equation, zero at the screen
 * centre, and THE 16-BIT WRAP IS WHAT REPEATS IT - the period is
 * 0x10000 / step, so 64 pixels at zoom 0x40 down to 16 at 0x100.
 *
 * img 0x31da tests only the TOP TWO BITS, which are clear for exactly
 * a quarter of the range, so a quarter of every period is the bright
 * band and the duty cycle is not a parameter anywhere.
 *
 * Then feedback rather than drawing: bright converges, dark decays
 * linearly, which is what leaves a trail behind the sweep. The bright
 * rule's fixed point is 62, not 63 - (62 + 0x3f) >> 1 is 62 - so it
 * stops one short of the band top the way the demo's ramps do.
 *
 * Both destinations get every write in the binary, DS being 0xa000 and
 * ES the work buffer; the work buffer is the state and the screen a
 * copy, which is why Beams_Run has no Vga_Present. One framebuffer
 * serves as both.
 */
void Xcur_BeamsRender(u8 *fb, s32 zoom, s32 angle)
{
	/* img 0x3193 - SHR, but only the low word is kept and those bits
	 * are the same either way. */
	s32 du = (s32)(s16)(((Xcur_Sin(angle) * zoom) >> 8) & 0xffff);
	s32 dv = (s32)(s16)(((Xcur_Cos(angle) * zoom) >> 8) & 0xffff);
	s32 acc, di = 0, row, col;

	/* img 0x31b3 - -(160*du + 100*dv), assembled out of shifts */
	acc = (-(dv * 100) - du * 160) & 0xffff;
	for (row = 0; row < 200; row++) {
		s32 bx;

		acc = (acc + dv) & 0xffff;              /* img 0x31d3 */
		bx = acc;
		for (col = 0; col < PD_SCREEN_W; col++) {
			if ((bx & 0xc000) == 0)             /* img 0x31da */
				fb[di] = (u8)(((fb[di] + 0x3f) & 0xff) >> 1);
			else if (fb[di] >= 4)               /* img 0x31e3, jc */
				fb[di] = (u8)(fb[di] - 4);
			bx = (bx + du) & 0xffff;            /* img 0x31ee */
			di++;
		}
	}
}
