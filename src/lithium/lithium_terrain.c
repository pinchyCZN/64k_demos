/* LITHIUM - the planet surface.
 *
 * ORIGIN img 0x4b3c Terrain_Build and img 0x4067 Terrain_Height.
 * docs/lithium/effects/terrain.txt.
 *
 * IT IS A GRID MESH, NOT A VOXEL RENDERER, rebuilt every frame from a
 * 128x128 heightmap and pushed through the same pipeline as the ships
 * - which is worth saying because it looks like one and because
 * XCURSION's equivalent IS one.
 *
 * WHAT DECIDES ITS EXTENT IS THE RASTERISER. Terrain_Build builds a
 * camera-aligned quad, scans it with Poly_ScanEdges, and then walks
 * the span table the scan filled: the row range gives the strips and
 * each row's x span gives that strip's columns. So the footprint is
 * scan-converted rather than computed.
 */

#include <string.h>

#include "lithium.h"

/* The 128x128 map. The original keeps it at 1e5f:0000 - see the
 * header - and the 64 KB grid it is carved out of is the back
 * buffer, which nothing needs while the screen is black. */
static u8 g_hmap[LITH_HEIGHTMAP_BYTES];
static u8 g_grid[0x10000];
static s32 g_rough;

const u8 *Lith_Heightmap(void) { return g_hmap; }

/* ORIGIN img 0x4044 Terrain_Displace. Masking to 0x7fff and taking
 * 0x4000 off is how a signed offset comes out of an unsigned
 * generator with no sign test, and the shift by g_TerrainRough -
 * which rises one per level - is persistence 0.5 written as an
 * increment rather than a multiply. */
static s32 terrain_displace(s32 ax)
{
	ax += ((s32)(Lith_RndNext() & 0x7fff) - 0x4000) >> g_rough;
	if (ax < 0) ax = 0;
	if (ax >= 0xff) ax = 0xff;
	return ax & 0xff;
}

/* ORIGIN img 0x3ed6 Terrain_DiamondSquare. Midpoint displacement on
 * a 256x256 byte grid, where step is a ROW offset and halfstep a
 * COLUMN one and the two halve together.
 *
 * IT WRAPS, which is what lets the flight scenes run over it for
 * ever: on the last column the two right-hand samples and the right
 * midpoint are taken from si - 0x100 instead, one row back, so the
 * map is a torus. Every index is 16-bit and wraps on its own. */
static void terrain_diamond_square(void)
{
	s32 step = 0x8000, halfstep = 0x80;

	g_rough = 1;
	g_grid[0] = (u8)(Lith_RndNext() & 0xff);
	for (;;) {
		s32 cursor = step, rowctr = 0;

		for (;;) {
			s32 di = (cursor + halfstep) & 0xffff;
			s32 colctr = 0;

			for (;;) {
				s32 a2 = (halfstep * 2) & 0xffff;
				s32 d2 = (step * 2) & 0xffff;
				s32 bx = (0x100 - halfstep * 2) & 0xffff;
				s32 si = (di - halfstep - step) & 0xffff;
				s32 c1, c2, c3, c4, v, sv;
				int wrap;

				c1 = g_grid[si];
				si = (si + a2) & 0xffff;
				wrap = (bx != colctr);
				if (!wrap) {
					c2 = g_grid[(si - 0x100) & 0xffff];
					si = (si + d2) & 0xffff;
					c3 = g_grid[(si - 0x100) & 0xffff];
				} else {
					c2 = g_grid[si];
					si = (si + d2) & 0xffff;
					c3 = g_grid[si];
				}
				si = (si - a2) & 0xffff;
				c4 = g_grid[si];

				g_grid[di] = (u8)terrain_displace((c1 + c2 + c3 + c4) >> 2);
				g_grid[(di - step) & 0xffff] =
					(u8)terrain_displace((c1 + c2) >> 1);
				v = terrain_displace((c2 + c3) >> 1);
				sv = (di + halfstep) & 0xffff;
				if (wrap) sv = (sv - 0x100) & 0xffff;
				g_grid[sv] = (u8)v;
				g_grid[(di + step) & 0xffff] =
					(u8)terrain_displace((c3 + c4) >> 1);
				g_grid[(di - halfstep) & 0xffff] =
					(u8)terrain_displace((c4 + c1) >> 1);

				di = (di + a2) & 0xffff;
				colctr += a2;
				if (colctr == 0x100) break;
			}
			rowctr += (halfstep * 2) & 0xffff;
			cursor = (cursor + step * 2) & 0xffff;
			if (rowctr == 0x100) break;
		}
		step >>= 1;
		g_rough++;
		halfstep >>= 1;
		if (!halfstep) break;
	}
}

/* ORIGIN img 0x52dd Terrain_Generate.
 *
 * THE SEED IS FIXED, so the landscape is the same on every run - the
 * trick the starfield uses too. THE PLATEAU AT 0x7676 IS WHERE THE
 * TOWER STANDS: 20 rows of 20 bytes of 0xff is flat maximum ground
 * under it. Only the 128x128 window at 0x4014 is copied out, so the
 * diamond-square's ragged edges are discarded. */
void Lith_TerrainGenerate(void)
{
	s32 i, k, bp, si;

	Lith_RndSeed(0xa5b5, 0xcab6);           /* img 0x52dd */
	memset(g_grid, 0, sizeof g_grid);
	terrain_diamond_square();

	bp = 0x7676;                            /* img 0x52fe */
	for (i = 0; i < 0x14; i++) {
		for (k = 0; k < 20; k++) g_grid[(bp + k) & 0xffff] = 0xff;
		bp = (bp + 0x100) & 0xffff;
	}
	Lith_TexBlur(g_grid, 5);                /* img 0x5316 */

	si = 0x4014;                            /* img 0x531e */
	for (i = 0; i < 0x80; i++) {
		for (k = 0; k < 0x80; k++)
			g_hmap[i * 0x80 + k] = g_grid[(si + k) & 0xffff];
		si += 0x100;
	}
}

/* ORIGIN img 0x5334 Math_BuildSquareTable. Entry n is (n+1) squared.
 *
 * THE ORIGINAL OVERLAYS IT ON THE SPAN TABLE at img 0xc8b2 - 1,600
 * dwords is 6,400 bytes, which is exactly 200 rows of 32 - because
 * the squares are only wanted while the textures are generated and
 * no polygon has been drawn yet. Keeping it separate is ours; the
 * side effect at img 0x5354 is not, so that is reproduced. */
static s32 g_sqtab[LITH_SQTAB_ENTRIES];

/* ORIGIN img 0x5334 - see above. */
void Lith_MathBuildSquareTable(void)
{
	s32 i;

	for (i = 0; i < LITH_SQTAB_ENTRIES; i++)
		g_sqtab[i] = (i + 1) * (i + 1);
	Lith_SpanResetFull();
}

/* OURS - a reader for the table above. It holds (i+1) squared, so
 * asking for |d| is asking for (|d|+1) squared - img 0x538d. */
s32 Lith_Square(s32 d) { return g_sqtab[d < 0 ? -d : d]; }

/* ORIGIN img 0x4a91 Math_Sqrt - a BISECTION OVER THE SQUARE TABLE,
 * not an arithmetic root. It returns the byte offset of the closest
 * entry, which is why every caller shifts the result down.
 *
 * The step starts at 0xc80, is halved at the TOP of each pass and
 * masked back to dword alignment, so the 2 bit is what ends it. */
s32 Lith_MathSqrt(s32 eax)
{
	s32 bx = 0xc80, si = 0xc80, edx, ebp, ecx;

	edx = g_sqtab[si >> 2] - eax;
	if (edx < 0) edx = -edx;
	do {
		bx = (bx >> 1) & 0xfffc;
		si += bx;
		ebp = g_sqtab[(si >> 2) & 0x7ff] - eax;
		if (ebp < 0) ebp = -ebp;
		si -= bx + bx;
		ecx = g_sqtab[(si >> 2) & 0x7ff] - eax;
		if (ecx < 0) ecx = -ecx;
		si += bx;
		if ((u32)ebp < (u32)ecx) {
			if ((u32)ebp < (u32)edx) { edx = ebp; si += bx; }
		} else {
			if ((u32)ecx < (u32)edx) { si -= bx; edx = ecx; }
		}
	} while (bx);
	return si;
}

/* ORIGIN img 0x57e1 Tex_BuildGlowNoise - and this is the surface's
 * texture, the one segment 0x5ad2 holds when the flyover runs.
 *
 * A RADIAL FALLOFF FROM (0x30, 0x30) WITH NOISE. The distance comes
 * out of the square table and Math_Sqrt, the brightness is
 * 0x104 minus a sixteenth minus a sixty-fourth of it, and three or
 * four bits of Rnd_Next are added on top. IT REACHES 0xff, which no
 * sphere-map texel can - that is how the terrain was caught being
 * drawn with the wrong one. */
void Lith_TexBuildGlowNoise(u8 *buf)
{
	s32 x, y, di = 0;

	for (y = 0x100; y > 0; y--) {
		for (x = 0x100; x > 0; x--) {
			s32 ax, bx, d, si;

			if (x > 0xfc || y > 0xfc) {
				ax = 0;
			} else {
				bx = x - 0x30;
				if (bx < 0) bx = 0x30 - x;
				d = g_sqtab[bx & 0x7ff];
				bx = y - 0x30;
				if (bx < 0) bx = 0x30 - y;
				d += g_sqtab[bx & 0x7ff];
				si = Lith_MathSqrt(d << 2);
				si >>= 4;
				ax = 0x104 - si;
				si >>= 2;
				ax -= si;
				{
					s32 dx = (s32)(s16)Lith_RndNext() >> 0xd;

					ax += dx;
					dx >>= 1;
					ax += dx;
				}
				if (ax < 0) ax = 0;
			}
			if (ax > 0xff) ax = 0xff;
			buf[di++] = (u8)ax;
		}
	}
	Lith_TexBlur(buf, 1);                   /* img 0x5880, CX = 1 */

	/* img 0x5883 - THE CONTRAST IS HALVED AND THE FLOOR PINNED. A
	 * byte below 0x81 is raised to it, then every byte becomes
	 * 0x81 + (v - 0x81) / 2, so the whole texture lands in 0x81 to
	 * 0xc0. Without this pass it is about 17 too bright everywhere
	 * and the surface washes out. */
	for (di = 0; di < LITH_TEX_BYTES; di++) {
		s32 v = buf[di];

		if (v < 0x81) v = 0x81;
		buf[di] = (u8)(0x81 + (((v - 0x81) & 0xff) >> 1));
	}
}

/* ORIGIN img 0x4067 Terrain_Height.
 *
 * THE INDEX TRICK IS A 7-BIT STRIDE. The original builds BH and BL as
 * the map row and column, then does ADD BL,BL followed by SAR BX,1 -
 * which drops BL's top bit and halves the pair, so the result is
 * (row & 0x7f) << 7 | (col & 0x7f). A 128x128 map that wraps on both
 * axes, and the 0x3fff mask is what bounds it.
 *
 * U AND V ARE THE SLOPE, NOT A POSITION. The two differences are to
 * the next column and the next ROW - +1 and +0x80 - so each vertex's
 * sphere-map coordinate is the local gradient, and the surface is
 * shaded by its own relief through the chrome map. */
static void terrain_height(LithVert *v, s32 x, s32 z,
                           s32 scroll_x, s32 scroll_z)
{
	s32 row, col, idx, h, du, dv;
	const u8 *map;

	map = g_hmap;

	row = ((x >> 8) & 0xff) - scroll_x;
	col = ((z >> 8) & 0xff) + scroll_z;
	idx = (((row & 0x7f) << 7) | (col & 0x7f)) & LITH_HEIGHTMAP_MASK;

	h = map[idx];
	v->y = (s32)(s16)((s32)(s8)(u8)(h - 0x80) << 3);

	/* img 0x40a4 and 0x40ae - the two neighbours, then << 9 and a
	 * bias of 0x3f80, floored at 0 and capped at 0x7e81. */
	du = (h - map[(idx + 1) & LITH_HEIGHTMAP_MASK]) << 9;
	dv = (h - map[(idx + 0x80) & LITH_HEIGHTMAP_MASK]) << 9;
	du += 0x3f80;
	dv += 0x3f80;
	if (du < 0) du = 0;
	if (dv < 0) dv = 0;
	if (du > 0x7e81) du = 0x7e81;
	if (dv > 0x7e81) dv = 0x7e81;
	v->u = (du >> 7) & 0xffff;
	v->v = (dv >> 7) & 0xffff;
}

/* ORIGIN img 0x4b3c, the footprint.
 *
 * Four corners of a square about (0x80, 0x80) in MAP space, turned by
 * the camera's yaw and sized by its pitch: the pitch term is a cosine
 * plus 0x1140 shifted down 7, and each offset is a sine of that over
 * 1 << 0xe. The two halves at img 0x4b42 and 0x4bae differ only in
 * which of the two terms they negate. */
static void terrain_quad(s32 *xy, s32 ang_x, s32 ang_y)
{
	const s32 *t = Lith_SinTable();
	s32 bx, bp, r, cx, si, ax, dx;

	if (!t) return;
	bx = (-ang_y) & 0x1ffc;
	bp = ang_x & 0x1ffc;

	r = ((t[(bp >> 2) + LITH_SIN_QUARTER] + 0x1140) >> 7);
	cx = (-t[bx >> 2] * r) >> 0xe;
	si = (t[(bx >> 2) + LITH_SIN_QUARTER] * r) >> 0xe;

	ax = 0x80 + cx - si;  dx = 0x80 + si + cx;
	xy[2] = ax; xy[3] = dx;                 /* img 0x4b8e, corner 1 */
	ax += si + si;  dx -= cx + cx;
	xy[4] = ax; xy[5] = dx;                 /* img 0x4b9f, corner 2 */

	r = ((-t[(bp >> 2) + LITH_SIN_QUARTER] + 0x1140) >> 7);
	cx = (t[bx >> 2] * r) >> 0xe;
	si = (-t[(bx >> 2) + LITH_SIN_QUARTER] * r) >> 0xe;

	ax = 0x80 + cx - si;  dx = 0x80 + si + cx;
	xy[6] = ax; xy[7] = dx;                 /* img 0x4bf2, corner 3 */
	ax += si + si;  dx -= cx + cx;
	xy[0] = ax; xy[1] = dx;                 /* img 0x4c03, corner 0 */
}

/* ORIGIN img 0x4b3c Terrain_Build.
 *
 * TWO PASSES OVER THE SAME SPANS. The first emits a vertex row per
 * strip and the second joins consecutive rows into quads, and the
 * second needs to know where each row's vertices START. The original
 * passes that between them by STASHING THE VERTEX-POOL BYTE OFFSET
 * INTO THE SPAN RECORD - img 0x4c5c and 0x4c61 write it, img 0x4cdc
 * and 0x4ce1 read it back and shift right 4 to turn 16-byte records
 * into indices. Nothing else in the demo reuses a span that way.
 *
 * A STRIP SPANS THE UNION OF TWO ROWS, img 0x4c51 and 0x4c7b, so
 * neighbouring rows always have a vertex to meet at.
 * effects/terrain.txt section 1.3. */
void Lith_TerrainBuild(LithScene *sc, s32 cam_ang_x, s32 cam_ang_y,
                       s32 scroll_x, s32 scroll_z, s32 attr)
{
	s32 xy[8], near_row, far_row, strips, row, i, di, z;

	terrain_quad(xy, cam_ang_x, cam_ang_y);
	Lith_PolyScanEdges(xy, 4);

	near_row = Lith_SpanRowMin();
	far_row = Lith_SpanRowMax();
	if (far_row < near_row) return;
	strips = (far_row - near_row) + 1;

	/* The vertex pass. di is the pool offset the original carries in
	 * DI, kept in bytes so the stash matches. */
	di = sc->nvert * 0x10;
	z = ((near_row << 1) - 0x100) << 7;
	for (i = 0; i < strips; i++) {
		s32 r = near_row + i;
		s32 pa = Lith_SpanX0(r - 1), ca = Lith_SpanX1(r - 1);
		s32 pb = Lith_SpanX0(r), cb = Lith_SpanX1(r);
		s32 lo, hi, n, x, c;

		if (pb < pa) {
			Lith_SpanSetBase(r - 1, 1, di + (pa - pb) * 0x10);
			Lith_SpanSetBase(r, 0, di);
			lo = pb;
		} else {
			Lith_SpanSetBase(r, 0, di + (pb - pa) * 0x10);
			Lith_SpanSetBase(r - 1, 1, di);
			lo = pa;
		}
		hi = (cb > ca) ? cb : ca;
		n = hi - lo + 2;
		x = ((lo << 1) - 0x100) << 7;

		for (c = 0; c < n; c++) {
			if (sc->nvert >= LITH_POOL_MAXV) return;
			sc->v[sc->nvert].x = (s32)(s16)x;
			sc->v[sc->nvert].z = (s32)(s16)z;
			terrain_height(&sc->v[sc->nvert], x, z, scroll_x, scroll_z);
			sc->nvert++;
			di += 0x10;
			x += 0x100;
		}
		z += 0x100;
	}

	/* The face pass - one fewer strip, two triangles a column. */
	for (row = near_row; row < far_row; row++) {
		s32 n = Lith_SpanX1(row) - Lith_SpanX0(row) + 1;
		s32 a = Lith_SpanBase(row, 0) >> 4;
		s32 b = Lith_SpanBase(row, 1) >> 4;
		s32 c;

		for (c = 0; c < n; c++) {
			if (sc->nface + 2 > LITH_POOL_MAXF) return;
			sc->f0[sc->nface] = b + 1;
			sc->f1[sc->nface] = a + 1;
			sc->f2[sc->nface] = a;
			sc->attr[sc->nface] = attr;
			sc->nface++;
			sc->f0[sc->nface] = b;
			sc->f1[sc->nface] = b + 1;
			sc->f2[sc->nface] = a;
			sc->attr[sc->nface] = attr;
			sc->nface++;
			a++;
			b++;
		}
	}
}
