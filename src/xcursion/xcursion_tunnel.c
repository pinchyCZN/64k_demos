/* XCURSION - the tunnel. effects/tunnel.txt.
 *
 * TWO FULL-SCREEN TABLES SUPPLY THE TWO HALVES OF ONE 16-BIT TEXTURE
 * INDEX: the angle table is the low byte and the depth table the
 * high byte. So a tunnel costs one add per axis and one indexed read
 * per pixel - no multiply, no divide and no clamp in the inner loop.
 *
 * Both tables are built in an EARLIER part than the one that uses
 * them, which is why nothing in the tunnel's own part looks like it
 * sets a tunnel up.
 */

#include <string.h>

#include "xcursion.h"

/* ORIGIN img 0x2e9d - Sqr_NearestRootBisect. A bisecting search over
 * g_SqrTable's DWORDS, returning a BYTE OFFSET rather than an index.
 *
 * THE TABLE HAS TWO OWNERS AND TWO ELEMENT WIDTHS. Planet_BuildMap
 * fills cs:0xaf7c with 90 words and Tunnel_BuildDepth with 200
 * dwords, each rebuilding before use, and the two roots that read it
 * differ to match - Sqr_NearestRoot scans words, this one bisects
 * dwords. Reading either layout as the other is wrong.
 *
 * The step is kept a multiple of 4 by img 0x2eb3, so it always lands
 * on a dword boundary and the search never straddles an element. */
static s32 sqr_root_bisect(const s32 *sqr, s32 target)
{
	s32 si = 0x190, step = 0x190;
	s32 best = sqr[si >> 2] - target;

	if (best < 0) best = -best;
	while (step) {
		s32 hi, lo;

		step = (step >> 1) & 0xfffc;            /* img 0x2eb1 */
		hi = sqr[(si + step) >> 2] - target;
		if (hi < 0) hi = -hi;
		lo = sqr[(si - step) >> 2] - target;
		if (lo < 0) lo = -lo;
		/* img 0x2eda - UNSIGNED compares, but every term is already
		 * an absolute value. */
		if ((u32)hi < (u32)lo) {
			if ((u32)hi < (u32)best) { best = hi; si += step; }
		} else {
			if ((u32)lo < (u32)best) { si -= step; best = lo; }
		}
	}
	return si;
}

/* ORIGIN img 0x2bd4 - Tunnel_BuildDepth. Called ONCE, from the part
 * before the tunnel's.
 *
 * THE TRUNCATION TO A BYTE IS THE EFFECT. img 0x2c52 stores only AL,
 * and 0x4000/(4k+1) exceeds 255 for every radius under about 16, so
 * the depth WRAPS repeatedly near the middle. That accelerating
 * repeat toward the centre is what makes the tunnel look infinitely
 * deep - it is not an overflow to clamp away.
 *
 * SI COMES BACK AS A BYTE OFFSET, so si + 1 is 4k + 1 and the
 * effective constant is 0x1000 per unit radius, not 0x4000. Do not
 * "fix" the +1.
 */
void Xcur_TunnelBuildDepth(u8 *depth, u8 *depth8)
{
	s32 sqr[200], k, row, col, di = 0;

	for (k = 0; k < 200; k++)                   /* img 0x2bdd */
		sqr[k] = (k + 1) * (k + 1);

	for (row = 0xc8; row > 0; row--) {          /* img 0x2c00 */
		for (col = 0x140; col > 0; col--) {     /* img 0x2c07 */
			s32 a = col - 0xa0, b = row - 0x64, si, v;

			if (a < 0) a = -a;                  /* img 0x2c13 */
			if (b < 0) b = -b;
			si = sqr_root_bisect(sqr, sqr[a] + sqr[b]);
			v = 0x4000 / (si + 1);              /* img 0x2c4b */
			depth[di] = (u8)(v & 0xff);         /* img 0x2c52 */
			depth8[di] = (u8)((v & 0xffff) >> 3);
			di++;
		}
	}
}

/* ORIGIN img 0x2cef - Poly_FillFlat. Poly_Fill without the Gouraud:
 * one colour and a byte fill.
 *
 * ROW * 16 BECOMES BOTH OFFSETS. The span buffer is indexed by
 * row * 16, and img 0x2e40 turns that same value into row * 320 with
 * two shifts and an add - which is why the stride is 16 and why there
 * is no multiply anywhere in the routine.
 *
 * IT HARDCODES ITS DESTINATION at img 0x2d42, alone among the fills.
 * Everything else draws to g_WorkSeg; this one writes 0x6a79, and
 * that single instruction is the whole link between
 * Tunnel_BuildAngle and the angle table the renderer reads.
 */
void Xcur_PolyFillFlat(u8 *dest, const s32 *vx, const s32 *vy,
                       s32 colour, XcurSpan *b)
{
	s32 row, e, lo = PD_SCREEN_H, hi = -1;

	for (row = b->lo < 0 ? 0 : b->lo; row <= b->hi; row++) {
		b->left[row] = 0x140;
		b->right[row] = -1;
	}
	b->lo = PD_SCREEN_H;
	b->hi = -1;

	/* img 0x2d32 closes the polygon by copying vertex 0 into a fifth
	 * slot, so four edges walk v0-v1-v2-v3-v0. */
	for (e = 0; e < 4; e++) {
		s32 x0 = vx[e], y0 = vy[e];
		s32 x1 = vx[(e + 1) & 3], y1 = vy[(e + 1) & 3];
		s32 dy, gx, ax;

		if (y0 > y1) {                          /* img 0x2d5d */
			s32 t;
			t = x0; x0 = x1; x1 = t;
			t = y0; y0 = y1; y1 = t;
		}
		if (y0 >= PD_SCREEN_H || y1 <= 0 || y1 == y0) continue;
		dy = y1 - y0;
		gx = ((x1 - x0) << 12) / dy;            /* img 0x2d8d */
		ax = (x0 << 12) + (gx >> 1);            /* img 0x2d9d */
		for (row = y0; row < y1 && row < PD_SCREEN_H; row++) {
			if (row >= 0) {
				s32 px = (ax - 0x800) >> 12;    /* img 0x2dbc */

				if (b->left[row] > px) b->left[row] = px;
				if (b->right[row] < px) b->right[row] = px;
				if (row < b->lo) b->lo = row;
				if (row > b->hi) b->hi = row;
				if (row < lo) lo = row;
				if (row > hi) hi = row;
			}
			ax += gx;
		}
	}

	for (row = lo < 0 ? 0 : lo; row <= hi; row++) {
		s32 left = b->left[row], right = b->right[row];

		if (left > 0x13f || right < 0) continue; /* img 0x2e4f */
		if (left < 0) left = 0;
		if (right > 0x13f) right = 0x13f;
		if (right < left) continue;
		memset(dest + row * PD_SCREEN_W + left, (int)(colour & 0xff),
		       (size_t)(right - left + 1));
	}
}

/* ORIGIN img 0x2c6a - Tunnel_BuildAngle, and it DRAWS its table
 * rather than computing one. 256 flat triangles apexed at the screen
 * centre, each filled with its own index, leaves every pixel holding
 * its own angle in 1/256 turns: an atan2 table, rasterised.
 *
 * THE COLOUR IS THE LOW BYTE OF A DOWN-COUNTER, img 0x2c8c, so the
 * angles run backward - 0x00, 0xff, 0xfe down to 0x01.
 *
 * The wedges reach x = +/-255, well off a 320-wide screen, and the
 * fill's clamps cut them to the border - which is how every pixel
 * ends up with a value.
 */
void Xcur_TunnelBuildAngle(u8 *angle, XcurSpan *b)
{
	s32 vx[4], vy[4], cx;

	vx[0] = 0xa0; vy[0] = 0x64;                 /* img 0x2c6d */
	vx[3] = 0xa0; vy[3] = 0x64;
	for (cx = 0x100; cx > 0; cx--) {
		s32 bx = (cx << 3) & 0x7fc;

		vx[1] = (s32)(s16)(((Xcur_Cos(bx) >> 4) + 0xa0) & 0xffff);
		vy[1] = (s32)(s16)(((Xcur_Sin(bx) >> 4) + 0x64) & 0xffff);
		bx = (bx + 8) & 0x7fc;                  /* img 0x2cbe */
		vx[2] = (s32)(s16)(((Xcur_Cos(bx) >> 4) + 0xa0) & 0xffff);
		vy[2] = (s32)(s16)(((Xcur_Sin(bx) >> 4) + 0x64) & 0xffff);
		Xcur_PolyFillFlat(angle, vx, vy, cx & 0xff, b);
	}
}

/* ORIGIN img 0x2b2d - Tunnel_Render.
 *
 * The depth scroll is 8.8 and only its high byte reaches the index.
 *
 * IT DRAWS HALF THE PIXELS A FRAME: img 0x2b77 steps by two and the
 * parity flips once per row AND again per frame - a checkerboard
 * that inverts every frame, so two frames cover the screen and the
 * skipped pixels keep what was already there.
 *
 * ABOVE ORDER 0x2d img 0x2b8f TAKES BOTH THE COORDINATE AND A MASK
 * from the eighth-scale table and blacks any pixel whose mask exceeds
 * the fade - and because that mask is largest at the centre, the
 * tunnel goes black from the middle outward. One table serving twice.
 */
s32 Xcur_TunnelRender(u8 *fb, const u8 *texture, const u8 *angle,
                      const u8 *depth, const u8 *depth8, s32 scroll,
                      s32 depth_hi, s32 parity, s32 order, s32 fade)
{
	s32 row, col, di = 0;
	s32 dl = (scroll & 0xffff) >> 3;            /* img 0x2b46 */
	const u8 *dtab = (order >= 0x2d) ? depth8 : depth;

	for (row = 0; row < 200; row++) {
		s32 i = di + (parity & 1);              /* img 0x2b5f */

		for (col = 0; col < 0xa0; col++) {
			s32 bl = (angle[i] + dl) & 0xff;    /* img 0x2b68 */
			s32 bh = (dtab[i] + depth_hi) & 0xff;
			s32 v = texture[(bh << 8) | bl];

			if (order >= 0x2d && fade < dtab[i])
				v = 0;                          /* img 0x2ba8, jc */
			fb[i] = (u8)v;
			i += 2;                             /* img 0x2b77 */
		}
		di += PD_SCREEN_W;
		parity++;                               /* img 0x2b81 */
	}
	return parity + 1;                          /* img 0x2b89 */
}
