/* XCURSION - the mapped sphere, the starfield, and the space scene
 * they share. effects/planet.txt.
 *
 * SIX PARTS CALL THIS RENDERER and four of them suppress the ball by
 * setting the approach past its cutoff, so the same routine is both
 * the planet and the plain stars-and-a-craft scene.
 *
 * The sphere's texture is the landscape's own 256x256 height field,
 * read through the land and water bands - one planet for no extra
 * data.
 */

#include <string.h>

#include "xcursion.h"

/* ORIGIN img 0x42b5. CX counts 0xbb8 words, which is exactly 1000
 * xyz triples - the count and the record layout confirm each other. */
void Xcur_PointsMake(XcurPoint *pts, s32 n)
{
	s32 i;

	for (i = 0; i < n; i++) {
		pts[i].x = (s32)(s16)Xcur_RndNext();
		pts[i].y = (s32)(s16)Xcur_RndNext();
		pts[i].z = (s32)(s16)Xcur_RndNext();
	}
}

/* ORIGIN img 0x3207 - Points_Render.
 *
 * THE DIVISOR IS MASKED TO 9 BITS BEFORE THE +5, so z wraps instead
 * of clamping and a far point reappears near. That is behaviour, not
 * a fault - masking after, or clamping, loses the near stars. */
void Xcur_PointsRender(u8 *fb, const XcurPoint *pts, s32 n,
                       s32 cx, s32 cy, s32 cz)
{
	s32 i;

	for (i = 0; i < n; i++) {
		/* EVERY SUBTRACTION HERE IS 16-BIT and wraps - img 0x3222,
		 * 0x3227 and 0x322c. Doing it in 32 and shifting
		 * arithmetically sends whole regions of the field off
		 * screen instead of wrapping them. effects/planet.txt 8. */
		u32 dz = (u32)((pts[i].z - cz) & 0xffff);
		s32 d = (s32)(((dz >> 6) & 0x1ff) + 5);   /* shr, unsigned */
		s32 ax = (s32)(s16)((pts[i].x - cx) & 0xffff);
		s32 bx = (s32)(s16)((pts[i].y - cy) & 0xffff);
		s32 sx = (ax / d + 0xa0) & 0xffff;        /* idiv - truncates */
		s32 sy;

		if ((u32)sx >= 0x140) continue;           /* img 0x3241, jae */
		sy = (bx / d + 0x64) & 0xffff;
		if ((u32)sy >= 0xc7) continue;            /* img 0x324d, jae */
		fb[sy * PD_SCREEN_W + sx] = (u8)((0x100 - (d >> 3)) & 0xff);
	}
}

/* ORIGIN img 0x43b5 - Sqr_NearestRoot. A descending-error scan over
 * the square table that stops the moment the error grows.
 *
 * It returns the BYTE OFFSET ONE PAST the nearest square, so the
 * answer is si/2 - and the +2 is cancelled at img 0x4334 by indexing
 * from 0xaf7a, the table less one entry. Not an off-by-one. */
static s32 sqr_root(const s32 *sqr, s32 ax)
{
	s32 dx = 0x7fff, si = 0, k;

	for (k = 0; k < 0x5a; k++) {
		s32 v = sqr[si >> 1] - ax;
		s32 bx = v < 0 ? -v : v;
		if (bx > dx) break;
		dx = bx;
		si += 2;
	}
	return si;
}

/* ORIGIN img 0x42d8 - Planet_BuildMap. The 128x128 screen-to-texture
 * table, with a sentinel for "off the ball".
 *
 * A SPHERE OF RADIUS 64, CLIPPED AT z = 32. The clip comes out of img
 * 0x42ef as sqr[63] - sqr[31], which the root search rounds down, so
 * the disc is radius 55 inside the 128-wide box.
 *
 * The parameterisation is lateral/z - the tangent, not an arcsine -
 * and it still compresses toward the limb, which is what makes it
 * read as a ball rather than a disc.
 *
 * BOTH DIVIDES ARE IDIV at img 0x4370 and img 0x438c, so they
 * truncate toward zero and C's / is right. Flooring instead is wrong
 * over the half of the sphere where 0x40 - c goes negative, which is
 * where sim/xcursion/parts.py diverges.
 *
 * Both coordinates are negated, so the texture is mapped THROUGH THE
 * CENTRE - mirrored in both axes. */
void Xcur_PlanetBuildMap(u16 *table)
{
	s32 sqr[0x5a], k, cl, ch, i = 0;
	s32 r_sq = 0x1000, cut;                 /* img 0xaffa, 64 * 64 */

	for (k = 0; k < 0x5a; k++) sqr[k] = (k + 1) * (k + 1);
	cut = sqr[sqr_root(sqr, r_sq - 0x400) / 2 - 1];     /* img 0x42ef */

	for (cl = 0x80; cl > 0; cl--) {
		for (ch = 0x80; ch > 0; ch--) {
			s32 a = cl - 64, b = ch - 64, d, z, u, v;

			if (a < 0) a = -a;
			if (b < 0) b = -b;
			d = sqr[a] + sqr[b];
			if (cut <= d) {                 /* img 0x4339 */
				table[i++] = XCUR_SPHERE_OUT;
				continue;
			}
			z = sqr_root(sqr, r_sq - d) / 2;
			u = (((0x40 - ch) * 0x20 + z / 2) * 2) / z + 0x40;
			v = (((0x40 - cl) * 0x20 + z / 2) * 2) / z + 0x40;
			table[i++] = (u16)(((u & 0xff) | ((v & 0xff) << 8)) & 0xffff);
		}
	}
}

/* ORIGIN img 0x2925 - Planet_Render's ball, between the stars and the
 * object.
 *
 * THE CUTOFF COUNT AND THE SCREEN ORIGIN ARE THE SAME NUMBER, which
 * is why no write ever wraps across a row: the count drawn is
 * (0x38000 - approach) >> 10 and the origin moves with it, so the
 * ball slides in from the left and lands centred when approach is 0.
 *
 * An approach of 0xfa000 skips every column, and four of the six
 * parts set exactly that - the ball is simply not there. */
void Xcur_PlanetRender(u8 *fb, const u16 *table, const u8 *texture,
                       s32 pan, s32 approach)
{
	s32 di = 0, origin = 0x2de0 - (approach >> 10), row, col;

	for (row = 0; row < XCUR_SPHERE_DIM; row++) {
		s32 bx = origin, eax = approach;

		for (col = 0; col < XCUR_SPHERE_DIM; col++) {
			s32 si = (s32)table[di];

			if (eax < 0x38000 && si != XCUR_SPHERE_OUT) { /* img 0x2967 */
				s32 t = texture[(pan + si) & 0xffff];
				if (bx >= 0 && bx < PD_SCREEN_W * PD_SCREEN_H)
					fb[bx] = (u8)(((t >> 2) - 0x20) & 0x3f); /* img 0x297b */
			}
			di++;
			bx--;
			eax += 0x400;
		}
		origin += PD_SCREEN_W;              /* img 0x2993 */
	}
}

/* ORIGIN img 0x3273 - Points_RenderErase, the credits' starfield.
 *
 * IT ERASES ITS OWN PIXELS INSTEAD OF CLEARING THE SCREEN, which is
 * how the credits survive underneath: it blanks a star's remembered
 * offset only if that pixel is still 0xc0 or above, and refuses to
 * plot over anything that is neither empty nor another star. The
 * text never reaches 0xc0. effects/scroller.txt. */
void Xcur_PointsRenderErase(u8 *fb, const XcurPoint *pts, s32 *prev,
                            s32 n, s32 cx, s32 cy, s32 cz)
{
	s32 i;

	for (i = 0; i < n; i++) {
		s32 x, y, z, bx;

		if (prev[i] >= 0 && fb[prev[i]] >= 0xc0)    /* img 0x3289 */
			fb[prev[i]] = 0;

		x = (s32)(s16)((pts[i].x - cx) & 0xffff);   /* img 0x329b */
		y = (s32)(s16)((pts[i].y - cy) & 0xffff);
		z = (s32)(s16)((pts[i].z - cz) & 0xffff);
		z = (((u32)(z & 0xffff)) >> 6) & 0x1ff;     /* img 0x32aa, SHR */
		z += 5;
		x = (s32)(s16)(((x / z) + 0xa0) & 0xffff);  /* img 0x32b7 */
		if ((u32)x >= 0x140) continue;              /* img 0x32ba */
		y = (s32)(s16)(((y / z) + 0x64) & 0xffff);  /* img 0x32c3 */
		if ((u32)y >= 0xc7) continue;               /* img 0x32c6 */
		bx = x + y * PD_SCREEN_W;                   /* img 0x32cb */
		/* img 0x32d8 - empty or another star, nothing else. */
		if (fb[bx] != 0 && fb[bx] < 0xc0) continue;
		prev[i] = bx;
		fb[bx] = (u8)((0x100 - (z >> 3)) & 0xff);   /* img 0x32e3 */
	}
}
