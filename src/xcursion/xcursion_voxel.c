/* XCURSION - the voxel landscape. effects/voxel.txt.
 *
 * A Y-BUFFER SPAN FILL, drawn far to near: each distance slice walks
 * 160 two-pixel columns, and a column only paints from wherever it
 * last stopped up to the new height. So nothing is ever overdrawn and
 * no depth test is needed - g_ColumnTop is the whole of it.
 */

#include <string.h>

#include "xcursion.h"

/* ORIGIN Demo_Entry img 0x005c. THE STEP ACCELERATES: one screen row
 * per slice while the distance is under 0x32, two under 0x5a, three
 * after, stopping at 0x96. Near slices are therefore thin and far
 * ones coarse, which is where a voxel renderer's time goes if it
 * does not do this. Verified against img 0xa890 in a running demo. */
#define SLICE_MAX   0x96
#define SLICES      128

static s32 g_dist[SLICES];
static s32 g_rows[SLICES];
static s32 g_nslice;

/* OURS */
static void slices_build(void)
{
	s32 cx = 0;

	g_nslice = 0;
	while (cx < SLICE_MAX && g_nslice < SLICES) {
		s32 n = 1;
		cx++;
		if (cx >= 0x32) { n++; cx++; }
		if (cx >= 0x5a) { n++; cx++; }
		g_dist[g_nslice] = cx;
		g_rows[g_nslice] = n;
		g_nslice++;
	}
}

/* ORIGIN img 0x39d8 - Terrain_BuildShade. The colour map is NOT a
 * generator run: it is the height map's slope - effects/voxel.txt
 * section 1.2, where the two deliberate oddities are written up. */
void Xcur_TerrainBuildShade(u8 *colour, const u8 *height,
                            s32 hscale, s32 divisor)
{
	s32 row, col;

	for (row = 0; row < 256; row++) {
		s32 base = row << 8;
		for (col = 0; col < 256; col++) {
			/* The neighbour steps by the LOW BYTE ONLY, so it wraps
			 * inside the row instead of crossing into the next. */
			s32 a = height[base | ((col - 1) & 0xff)];
			s32 b = height[base | ((col + 1) & 0xff)];
			s32 g = (a - b) * (hscale >> 6);
			s32 v;
			if (g < 0) g = -(-g / divisor);
			else g = g / divisor;
			v = g + 0x10;
			if (v <= 0) v = 1;
			if (v >= 0x1f) v = 0x1e;
			colour[base | col] = (u8)v;
		}
	}
}

/* ORIGIN img 0x3a63 - Terrain_TintWater. Every cell at or below the
 * water level has its COLOUR raised by 0x20, which moves it out of
 * the land ramp and into the water band.
 *
 * It runs over 0xfa00 cells, not the full 0x10000 - img 0x3a6d - so
 * the last 1536 of the colour map keep the land ramp. */
void Xcur_TerrainTintWater(u8 *colour, const u8 *height)
{
	s32 i;

	for (i = 0; i < 0xfa00; i++)
		if (height[i] <= 0x21) colour[i] = (u8)(colour[i] + 0x20);
}

/* ORIGIN img 0x3a7f - the ripple the water rides on. 128 by 128, and
 * SIGNED: img 0x3aba stores AL straight with no 0x80 bias, which is
 * why the renderer shifts it arithmetically and why the index mask
 * is 0x3fff. */
void Xcur_WaterFieldBuild(u8 *field)
{
	s32 cx, dx, di = 0;

	/* THE 256-ENTRY VIEW, NOT THE BYTE OFFSET. These builders index
	 * the table at a stride of two entries, so entry i is byte 8i -
	 * Xcur_Sin takes bytes. */
#define SIN256(i) Xcur_Sin((((i) & 0xff) * 8))
    for (cx = 0x80; cx > 0; cx--)
		for (dx = 0x80; dx > 0; dx--) {
			s32 a = SIN256(2 * (2 * dx + cx));
			s32 b = SIN256(2 * (cx - 2 * dx));
			field[di++] = (u8)(((a + b) >> 9) & 0xff);
		}
#undef SIN256
}

/* ORIGIN img 0x0dc5 - the camera, integrated inside Timer_Isr.
 * Two sine phases and a heading wobble, stepping 1, 2 and 2 -
 * effects/voxel.txt section 8.2.
 *
 * THE ANGLE TERM SHIFTS UNSIGNED and the positions do not: img 0x0e23
 * is `shr` against `sar` at 0x0ded and 0x0e0a. Alike looks right and
 * is wrong - a negative sine then turns the wrong way, hard.
 *
 * Speed is cs:[0x988d] + cs:[0x9899] - 0x800, zero until raised. */
void Xcur_CamStep(XcurCam *cam, XcurCamPhase *ph, s32 speed)
{
	s32 e;

	e = Xcur_Sin(ph->a) * speed;
	cam->wy += e >> 13;
	e = Xcur_Cos(ph->b) * speed;
	cam->wx += e >> 13;
	/* shr, not sar - see above. */
	e = (s32)((u32)Xcur_Sin(ph->c) >> 11);
	cam->angle = (cam->angle + e) & 0xffff;

	ph->a = (ph->a + 1) & 0xffff;
	ph->b = (ph->b + 2) & 0xffff;
	ph->c = (ph->c + 2) & 0xffff;
}

/* ORIGIN img 0x0ede - the steering tail of mode 2, which is where
 * the camera actually closes on the craft.
 *
 * AN EASE ONTO THE TARGET, not a flight along a heading, so it
 * CONVERGES and the view then stops moving on its own. Nothing
 * freezes it - effects/voxel.txt section 8.3. */
void Xcur_CamEase(XcurCam *cam, s32 tgt_x, s32 tgt_y)
{
	s32 x = tgt_x, y = tgt_y, dummy = 0;
	s32 sn = Xcur_Sin(cam->angle), cs = Xcur_Cos(cam->angle);

	(void)dummy;
	x -= sn << 3;                       /* img 0x0ef8 */
	y -= cs << 3;                       /* img 0x0f05 */
	x -= cam->wx;
	y -= cam->wy;
	cam->wx += x >> 4;                  /* img 0x0f18 */
	cam->wy += y >> 4;
	cam->angle = (cam->angle - 4) & 0xffff;   /* img 0x0f2c */
}

/* ORIGIN img 0x0e8e - mode 2's head, the landscape's flight model.
 *
 * THE CRAFT FLIES AND THE CAMERA CHASES IT: the step moves the
 * TARGET, the altitude and the scale - camera.txt section 5. */
void Xcur_CamFlight(XcurCam *cam, XcurObj *obj, s32 *tgt_x, s32 *tgt_y)
{
	s32 x = 0, y = 0, z = -4, bx, cx;

	obj->pitch = (obj->pitch - 1) & 0xffff;     /* img 0x0e8e */
	Xcur_VecRotate3(&x, &y, &z, obj->pitch, obj->yaw, obj->roll);
	y >>= 2;                                    /* img 0x0eba */
	*tgt_x += x;                                /* img 0x0ebe */
	cam->alt -= y;                              /* img 0x0ec4 */
	*tgt_y += z;                                /* img 0x0eca */
	cx = y >> 3;                 /* img 0x0ed3 - scale moves 7/8 */
	bx = (s32)(s16)((y - cx) & 0xffff);
	cam->scale = (s32)(s16)((cam->scale - bx) & 0xffff);

	Xcur_CamEase(cam, *tgt_x, *tgt_y);
}

/* ORIGIN img 0x00bb - THE SKY TABLE, built once in the opening's
 * init and never rebuilt. 400 entries, each ONE value split into a
 * dithered pair: the low byte is v >> 1 and the high byte (v + 1)
 * >> 1, so an odd v puts two adjacent palette entries in the word
 * and an even one puts the same twice. The renderer swaps the halves
 * on alternate frames, which is the whole of the dither.
 *
 * v is ((0x15e + i) >> 2) - 0x20 CLAMPED TO 0x40 .. 0x7e, which is
 * what gives the sky a flat top and bottom with the ramp between.
 * effects/voxel.txt. */
static u16 g_sky[0x190];
static s32 g_sky_phase;          /* img 0xa87e */

void Xcur_SkyBuild(void)
{
	s32 cx;

	for (cx = 0x190; cx > 0; cx--) {            /* img 0x00be */
		s32 v = (s32)(s16)((0x15e + cx) & 0xffff) >> 2;
		s32 lo, hi;

		v -= 0x20;                              /* img 0x00c9 */
		if (v < 0x40) v = 0x40;
		if (v >= 0x7f) v = 0x7e;
		hi = ((v + 1) & 0xff) >> 1;             /* img 0x00de */
		lo = (v & 0xff) >> 1;
		if (!(cx & 1)) { s32 t = lo; lo = hi; hi = t; }   /* img 0x00e4 */
		g_sky[0x190 - cx] = (u16)((hi << 8) | lo);
	}
}

/* ORIGIN img 0x3620 - Voxel_Render.
 *
 * The three maps are three segments and their roles are the code's
 * own - effects/voxel.txt section 1.1. Everything in cam is a global
 * the demo keeps, so none of it is invented here.
 */
void Xcur_VoxelRender(u8 *fb, const u8 *height, const u8 *colour,
                      const u8 *water, const XcurCam *cam)
{
	s32 scol;
	s32 top[160];
	s32 i, s;
	s32 sin_a, cos_a, dx0, dy0;

	if (!g_nslice) slices_build();

	/* img 0x3623 - row 199 of each two-pixel column, the bottom. */
	for (i = 0; i < 160; i++)
		top[i] = 0xf8c0 + 2 * i;

	sin_a = Xcur_Sin(cam->angle);
	cos_a = Xcur_Cos(cam->angle);
	/* img 0x36a9 - x + (x >> 2), ie. times 1.25. */
	dx0 = sin_a - cos_a;  dx0 += dx0 >> 2;
	dy0 = cos_a + sin_a;  dy0 += dy0 >> 2;

	for (s = 0; s < g_nslice; s++) {
		s32 dist = g_dist[s];
		s32 horizon = (dist >> 1) - 0x2c;
		s32 base, rx, ry, sx, sy, step, shade, edi, col;

		if (horizon < 0) horizon = 0;
		base = ((cam->scale / dist) + 0x48) * 320;
		rx = dx0 * dist + cam->wx;
		ry = dy0 * dist + cam->wy;
		sx = (cos_a * dist) >> 6;
		sy = (sin_a * dist) >> 6;
		step = cam->hscale / dist;      /* g_HeightToY's slope */
		shade = horizon & 0xff;
		edi = base;

		for (col = 0; col < 160; col++) {
			s32 si = (((rx >> 12) & 0xff) << 8) | ((ry >> 12) & 0xff);
			s32 h = height[si];
			s32 c = (colour[si] - shade) & 0xff;
			s32 y, at, lo, hi;

			if (c > 0x1f) c = 0;

			if (cam->water_level) {
				s32 wh = water[(si + cam->water_scroll) & 0x3fff];
				s32 bh = (wh + cam->water_level) & 0xff;
				if (bh > h) {
					h = bh;
					/* sar ah,2 - the sample is SIGNED, so this is an
					 * arithmetic shift of a negative half the time. */
					c = (c + (((s32)(s8)wh) >> 2) + 0x24) & 0xff;
					if (c >= 0x3f) c = 0x3e;
				}
			}

			y = -h * step + edi + cam->yofs;
			if (y < 0x140) y = 0x140;

			at = top[col];
			lo = c;
			hi = (c + 1) & 0xff;
			while (at >= y) {
				if (at + 1 < PD_SCREEN_W * PD_SCREEN_H) {
					fb[at] = (u8)lo;
					fb[at + 1] = (u8)hi;
				}
				/* img 0x37bb xchg - the pair swaps every row, which
				 * dithers the two-pixel column. */
				{ s32 t = lo; lo = hi; hi = t; }
				at -= 0x140;
			}
			top[col] = at;

			rx += sx;
			ry -= sy;
			edi += 2;
		}
	}

	/* img 0x37f5 - THE SKY, and it is the last thing the renderer
	 * does rather than the first. Each column fills from the top of
	 * the screen down to whatever the terrain left in the y-buffer,
	 * reading one table entry per row.
	 *
	 * WHERE IT STARTS IN THE TABLE IS THE HORIZON: 0x226 less the
	 * scale over 128 and TWICE the sky bias, img 0x3817. So the
	 * camera's height and the follower's pitch both slide the whole
	 * gradient up and down, which is what keeps the horizon attached
	 * to the land. effects/voxel.txt.
	 */
	g_sky_phase++;                              /* img 0x37f5 */
	for (scol = 0; scol < 0xa0; scol++) {       /* img 0x37fa */
		s32 di = scol * 2;
		s32 si = 0x226 - (((u32)cam->scale) >> 7);   /* img 0x380d */

		si -= cam->sky_bias;                    /* img 0x381e - TWICE */
		si -= cam->sky_bias;
		if (si < 0) si = 0;                     /* img 0x3828 */
		if (si >= 0x320) si = 0x31e;
		si &= 0xfffe;                           /* img 0x3838 */

		for (;;) {
			s32 w, lo, hi;

			w = (s32)g_sky[si >> 1];
			lo = w & 0xff;
			hi = (w >> 8) & 0xff;
			if (!(g_sky_phase & 1)) { s32 t = lo; lo = hi; hi = t; }
			if (di + 1 < PD_SCREEN_W * PD_SCREEN_H) {
				fb[di] = (u8)lo;
				fb[di + 1] = (u8)hi;
			}
			/* img 0x383e - the test is the TOP of the loop, and img
			 * 0x385f writes one row past it on the way out. */
			if ((u32)top[scol] <= (u32)di) break;
			si += 2;
			if (si > 0x31e) si = 0x31e;
			di += PD_SCREEN_W;
		}
	}
}

/* ORIGIN img 0x507a - the flag's sine field, and it is NOT the
 * water's. Same two terms and the same 128 by 128, but this one is
 * at SINGLE frequency where img 0x3a7f doubles it, shifts 7 where
 * that shifts 9, and adds a 0x80 bias where that stays signed. The
 * flag reads it unsigned, so the bias is what centres the ripple.
 */
void Xcur_SineFieldBias(u8 *field)
{
	s32 cx, dx, di = 0;

#define SIN256(i) Xcur_Sin((((i) & 0xff) * 8))
    for (cx = 0x80; cx > 0; cx--)
		for (dx = 0x80; dx > 0; dx--) {
			s32 a = SIN256(2 * dx + cx);        /* img 0x5096 */
			s32 b = SIN256(cx - 2 * dx);        /* img 0x50ab */

			field[di++] = (u8)((((a + b) >> 7) + 0x80) & 0xff);
		}
#undef SIN256
}

/* ORIGIN img 0x3d93 - Terrain_HeightAt, the only random access into
 * the height map. Everything else walks it.
 *
 * THE INDEX TAKES ITS TWO BYTES FROM DIFFERENT SHIFTS: x >> 4 masked
 * to the high byte, z >> 12 to the low. So x is a 12.4 fixed point
 * across the map and z a 20.12, and the two are not interchangeable.
 *
 * It answers in WORLD units, img 0x3deb: the height byte times
 * hscale / 320, which at the usual 0x1000 is twelve per step.
 *
 * `kind` comes back 2 when the water surface won rather than the
 * ground - img 0x3ddc - and with water_level 0 the whole branch is
 * skipped and it is always the ground.
 */
s32 Xcur_TerrainHeightAt(const u8 *height, const u8 *water, s32 x,
                         s32 z, s32 water_level, s32 water_scroll,
                         s32 hscale, s32 *kind)
{
	s32 si = (((x >> 4) & 0xff00) | ((z >> 12) & 0xff));
	s32 al = height[si];
	s32 dh = water_level & 0xff;
	s32 bx = 0;

	if (dh != 0) {                              /* img 0x3dcb */
		s32 dl = water[(si + water_scroll) & 0x3fff];

		dh = (dh + dl) & 0xff;                  /* img 0x3dcf */
		if (dh > al) { al = dh; bx = 2; }       /* img 0x3dd1, BP = 0 */
	}
	if (kind) *kind = bx;
	return (s32)(s16)(((hscale / 0x140) * al) & 0xffff);  /* img 0x3deb */
}
