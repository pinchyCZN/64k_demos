/* XCURSION - the 4-3-2-1 countdown. effects/countdown.txt.
 *
 * A PARTICLE SYSTEM THAT RESTORES ITS BACKGROUND. Each particle puts
 * back the pixel it last wrote, from a clean copy, so the digits
 * leave no trail - which is why the camera freezes for this scene:
 * a moving background could not be restored from.
 */

#include <string.h>

#include "xcursion.h"

/* ORIGIN img 0x3897. The mask is 320 WIDE, not four 8-byte glyphs -
 * the loop walks a 64-wide window down its left edge at a 40-byte
 * row stride. Reading it as 8-byte glyphs decodes into noise.
 *
 * SI IS PRE-INCREMENTED, so the first byte read is the base plus one. */
static int mask_bit(u32 window, s32 col, s32 row)
{
	u32 base = XCUR_COUNTDOWN_MASK + window * 8 + 1;
	return (g_img[base + (u32)row * XCUR_MASK_STRIDE + (col >> 3)]
	        >> (col & 7)) & 1;
}

/* ORIGIN img 0x3887 - one particle per set bit.
 *
 * THE VELOCITY IS THE OFFSET FROM CENTRE: ax and dx run -512 to +496
 * across the window, and vx/vy are those >> 4. So the glyph explodes
 * outward, fastest at its edges, with no randomness anywhere. */
s32 Xcur_CountdownSpawn(XcurParticle *p, s32 max, s32 window)
{
	s32 row, col, n = 0, ax, dx = -0x200;

	if (!g_img) return 0;
	for (row = 0; row < 64; row++) {
		ax = -0x200;
		for (col = 0; col < 64; col++) {
			if (mask_bit((u32)window, col, row) && n < max) {
				p[n].life = 0xf8;
				p[n].x = ax + 0x0a00;
				p[n].y = dx + 0x0640;
				p[n].vx = ax >> 4;
				p[n].vy = dx >> 4;
				p[n].last = 0;
				n++;
			}
			ax += 0x10;
		}
		dx += 0x10;
	}
	return n;
}

/* ORIGIN img 0x3925 - one frame of the system. countdown.txt 3.
 *
 * BOTH CLIPS ARE `jae`, SO BOTH ARE UNSIGNED, on 16-bit registers.
 * Sign-extending them culls every row from 128 down.
 *
 * The second warp sample is CUMULATIVE, and the heat carries DH
 * from the field as well as g_FireHeat. */
void Xcur_CountdownStep(u8 *fb, const u8 *bg, XcurParticle *p, s32 n,
                        const u8 *field, s32 heat, s32 ofs_x, s32 ofs_y)
{
	s32 i;

	for (i = 0; i < n; i++) {
		u32 si, ax, bx, dx;
		s32 dh, v;

		if (p[i].life == 0) continue;
		if (p[i].last) fb[p[i].last] = bg[p[i].last];

		si = ((u32)p[i].x & 0xffff) >> 4;           /* img 0x3934 */
		/* THE SAME value is reused as the screen y. */
		ax = (((u32)p[i].y & 0xfff0) << 4) & 0xffff;
		bx = (si + ax + (u32)ofs_x) & 0xffff;
		si = (si + (field[bx] >> 3) - 0x10) & 0xffff;
		if (si >= 0x140) { p[i].life = 0; continue; }   /* img 0x3953 */

		bx = (bx + (u32)ofs_y) & 0xffff;
		dx = ((u32)(field[bx] & 0xf8) << 5) & 0xffff;
		/* DH IS NOT ZERO HERE - img 0x3966 put the field byte in it. */
		dh = (s32)(dx >> 8);
		ax = (ax + dx - 0x1000) & 0xffff;
		if (ax >= 0xc800) { p[i].life = 0; continue; } /* img 0x396e */

		/* ax is the row in 256ths, so ax + ax/4 is row * 320. */
		si = (si + ax + (ax >> 2)) & 0xffff;

		v = fb[si];
		if (v >= 0xc0 || v < 0x60) v = 0x60;
		dh = ((dh + ((heat & 0xff) >> 1)) & 0xff) >> 2;
		fb[si] = (u8)((v + dh) & 0xff);             /* img 0x3998 */
		p[i].last = (s32)si;

		p[i].life -= 4;                             /* img 0x399e */
		if (p[i].life <= 0) { p[i].life = 0; continue; }
		p[i].x += p[i].vx;
		p[i].y += p[i].vy;
	}
}
