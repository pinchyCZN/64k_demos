/* XCURSION - the second landscape renderer. effects/ground.txt.
 *
 * IT IS NOT A REUSE OF Voxel_Render. It shares that renderer's ray
 * setup exactly and reads the same 256x256 field, then inverts the
 * loop order: the outer loop counts DISTANCE down and rebuilds the
 * whole ray start each pass, where Voxel_Render takes one column and
 * marches outward.
 *
 * THE SOURCE PAIR IS THE OPPOSITE WAY ROUND. Here the height comes
 * from the SHADE MAP that Terrain_BuildShade writes, and the raw
 * plasma is only the water test. Voxel_Render takes its heightmap
 * from the plasma directly, so handing these two over in the other
 * order gives a plausible-looking and completely wrong landscape.
 */

#include "xcursion.h"

/* ORIGIN img 0x341c - Ground_Render.
 *
 * THE SCREEN WALK IS DIAGONAL: img 0x3531 adds 2 to the offset per
 * sample and another row every OTHER sample, so each distance pass
 * paints a 45-degree band and successive passes start one row
 * higher. That is the whole shape of the effect.
 *
 * ramp is g_GroundRamp, which the launch part seeds negative and
 * mode 3 bumps every six ticks - so the distance starts long and
 * closes to its floor, which is what reveals the ground.
 */
void Xcur_GroundRender(u8 *work, const u8 *shade, const u8 *water,
                       s32 cam_x, s32 cam_z, s32 cam_angle,
                       s32 cam_scale, s32 ramp)
{
	s32 dist = (0x80 - ramp) & 0xffff;          /* img 0x3431 */
	s32 di = 0xf8c0;                            /* img 0x342e */

	if (dist <= 0x50) dist = 0x50;              /* img 0x3439, JA */

	while (dist > 1) {                          /* img 0x3551 */
		s32 step = (s32)(((u32)cam_scale / (u32)dist) & 0xffff);
		s32 bias = (step >> 5) & 0xff;          /* img 0x3456 */
		s32 bx = cam_angle & 0x7fc;
		s32 sn = Xcur_Sin(bx), cs = Xcur_Cos(bx);
		s32 rx, rz, sx, sz, saved = di, bp;

		rx = ((sn - cs) * step) << 4;           /* img 0x346d */
		rx = rx + (rx >> 2) + cam_x;            /* img 0x3484, times 1.25 */
		rz = ((cs + sn) * step) << 4;           /* img 0x3496 */
		rz = rz + (rz >> 2) + cam_z;            /* img 0x34aa */
		sx = (cs * step) >> 2;                  /* img 0x34bf */
		sz = (sn * step) >> 2;                  /* img 0x34d2 */

		for (bp = 0xa0; bp; bp--) {             /* img 0x34e5 */
			u32 si = ((u32)rz >> 0x10) & 0xffff;
			s32 al;

			si = (si + (((u32)rx >> 8) & 0xff00)) & 0xffff;
			al = (shade[si] - bias) & 0xff;     /* img 0x3501 */
			/* The shade map is clamped to 1..0x1e, so 0x20 and over
			 * can only be reached by UNDERFLOW - that is the test. */
			if (al >= 0x20) al = 0;             /* img 0x3508 */
			if (water[si] < 0x21) al = (al + 0x20) & 0xff;  /* img 0x350e */
			if (di >= 0 && di + 1 < PD_SCREEN_W * PD_SCREEN_H) {
				work[di] = (u8)al;              /* img 0x3518, AH = AL */
				work[di + 1] = (u8)al;
			}
			rx += sx;                           /* img 0x351b */
			rz -= sz;                           /* img 0x3526 */
			di += 2;                            /* img 0x3531 */
			if (bp & 1) {
				di += PD_SCREEN_W;              /* img 0x3534 */
				/* img 0x353e leaves the INNER loop only. The distance
				 * loop keeps going, and because each pass starts a
				 * row higher it runs longer before falling off. */
				if (di >= 0xfa00) break;
			}
		}
		di = saved - PD_SCREEN_W;               /* img 0x3548 */
		dist -= 1;                              /* img 0x354c */
	}
}
