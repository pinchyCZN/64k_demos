/* LITHIUM - the lens flare, its sprite, and the glare wash.
 *
 * ORIGIN img 0x5363 Flare_BuildSprite, img 0x6065 Flicker_Step, img
 * 0x46f3 Sun_GlareFade, img 0x4476 Flare_Render, img 0x4754
 * Flare_DrawSun, img 0x47f5 Flare_DrawElement, img 0x49df
 * Flare_ClipSetup. docs/lithium/effects/sky.txt sections 5 and 6.
 *
 * ONE 160 x 160 CONE SERVES EVERYTHING: it is the sun's sprite, it is
 * every element's sprite at a different crop, and Sun_GlareFade reads
 * it as a falloff curve - screen centre (160,100) lands exactly on
 * its centre, so the cone measures how near the middle of the screen
 * the sun is.
 *
 * THE SECOND PLANE IS AN ANGLE MAP, not more brightness. It sits
 * 0x4600 past the cone, which puts the sprite's centre byte exactly
 * on the sweep's centre byte, and g_FlickerTable is indexed through
 * it - so the glare shimmers in angular sectors rather than per
 * pixel. Flicker_Step closes its table into a ring for that reason.
 */

#include <string.h>

#include "lithium.h"

/* 0x0000 the cone and 0x6400 the angle plane.
 *
 * THE SEGMENT HAS A THIRD TENANT: 0x8c00 is the craft's shadow
 * stencil - subsystems/raster.txt 3.3 - so a read past the plane
 * does NOT land in zeros. The port's buffer is a whole segment
 * because the original's is: the plane read is the sprite offset
 * plus 0x4600, and a low element pushes that past the plane into
 * whatever the neighbour holds. */
static u8 g_sprite[LITH_SPRITE_BYTES];
static s32 g_flare_ramp[0x100];         /* g_FlareRamp img 0xe3e6  */
static s32 g_flicker[0x100];            /* g_FlickerTable 0xe1e6   */
static s32 g_flare_size;                /* img 0x8f46              */
static s32 g_flare_colour;              /* img 0x8f48              */
static s32 g_sun_saved_x, g_sun_saved_y;    /* img 0x8f5b, 0x8f5d  */

/* OURS. The original sweeps the wedges into the back buffer and
 * copies the window out of it; the window is wholly covered by
 * wedges, so what was underneath never shows and a buffer of our own
 * is the same result without disturbing the scene's. */
static u8 g_sweep[LITH_FB_BYTES];

/* ORIGIN img 0x4a67 - the threshold both blitters work against, and
 * what sets an element's size. */
static s32 flare_threshold(void)
{
	s32 ah = g_flare_colour + 2 * Lith_FlareBrightness();

	return ah > 0xff ? 0xff : ah;
}

/* ORIGIN img 0x6065 Flicker_Step - a 256-entry smoothed random walk,
 * rebuilt every tick. effects/flicker.txt.
 *
 * IT LEAVES THE GENERATOR EXACTLY AS IT FOUND IT, img 0x60bf writing
 * back the pair loaded at img 0x6065. Letting it advance the
 * generator instead moves the dust, the shatter and everything else
 * random in the demo.
 *
 * THE LEFT NEIGHBOUR IS ALREADY UPDATED: the running value stays in
 * AX across the loop, so an entry is the mean of the NEW entry below
 * it and the OLD one above. The last entry is the first one back,
 * which closes the table into a ring. */
void Lith_FlickerStep(void)
{
	u16 s0, s1;
	s32 first, ax, i;

	Lith_RndGet(&s0, &s1);
	first = g_flicker[0];
	ax = (first * 2 + 0x602d) & 0xffff;
	Lith_RndSeed((u16)(s0 + ax),
	             (u16)(s1 + ((ax * 2 + 0x602d) & 0xffff)));

	ax = first;
	for (i = 0; i < 0xff; i++) {
		ax = (ax + g_flicker[i + 1]) >> 1;
		ax += (s32)(s16)(u16)(Lith_RndNext() + 0x800) >> 0xc;
		if (ax < 0) ax = 0;
		if (ax > 0x40) ax = 0x40;
		g_flicker[i] = ax;
	}
	g_flicker[0xff] = first;            /* img 0x60bc */
	Lith_RndSeed(s0, s1);               /* img 0x60bf */
}

/* ORIGIN img 0x5363 Flare_BuildSprite - the cone, g_FlareRamp, the
 * angle plane, then the flicker table's starting walk. */
void Lith_FlareBuildSprite(void)
{
	s32 di = 0, cx, cy, si, ax, bp, k, dx;
	u8 wedge[1];
	LithPoly p;

	/* img 0x5378 - a linear cone, 251 at the centre falling to 0 at
	 * the rim. Math_Sqrt returns a BYTE offset, which is why the four
	 * shifts start at 2 rather than 0. */
	for (cy = 0xa0; cy > 0; cy--) {
		for (cx = 0xa0; cx > 0; cx--) {
			si = Lith_MathSqrt((Lith_Square(cx - 0x50)
			                    + Lith_Square(cy - 0x50)) << 2);
			ax = 0xff;
			si >>= 2; ax -= si;
			si >>= 1; ax -= si;
			si >>= 3; ax -= si;
			si >>= 3; ax -= si;
			g_sprite[di++] = (u8)(ax > 0 ? ax : 0);
		}
	}

	/* ORIGIN img 0x53e3 - a reciprocal ramp with a small bump around
	 * entry 0x50.
	 *
	 * THE CLAMP WRITES AL ALONE, so an entry whose quotient passed
	 * 0xff keeps its high byte and stays far above 0xff. That is the
	 * original's, and it is invisible because Flare_DrawSun clamps
	 * the whole word - but clamping here would not be the same
	 * table. */
	for (k = 0; k < 0x100; k++) {
		bp = 0x100 - k;
		ax = 0x1194 / (k + 1) - 0x11;
		si = bp - 0xb0;
		if (si < 0) si = 0xb0 - bp;
		si -= 4;
		if (si < 0) ax -= si << 3;
		if (ax > 0xff) ax = (ax & 0xff00) | 0xff;
		g_flare_ramp[k] = ax;
	}

	/* ORIGIN img 0x541f - 512 wedges from (160,100) out to radius
	 * 256, each a 512th of a turn, filled through Poly_Fill with a
	 * one-byte texture so the fill comes out flat.
	 *
	 * THE VALUE IS THE COUNTER'S LOW BYTE AGAINST 512 WEDGES, so the
	 * ramp goes round TWICE and the shimmer has two-fold symmetry.
	 *
	 * img 0x54df copies 160 x 64 from (80,68), which is what centres
	 * the sweep under the 0x4600 offset. */
	memset(g_sprite + 0x6400, 0, LITH_SPRITE_BYTES - 0x6400);
	memset(g_sweep, 0, sizeof g_sweep);
	for (cx = 0x200; cx > 0; cx--) {
		s32 bx = (cx << 4) & 0x1ffc;

		wedge[0] = (u8)(cx & 0xff);
		p.x[0] = 0xa0; p.y[0] = 0x64;
		p.x[1] = (Lith_Cos(bx) >> 4) + 0xa0;
		p.y[1] = (Lith_Sin(bx) >> 4) + 0x64;
		bx = (bx + 0x10) & 0x1ffc;
		p.x[2] = (Lith_Cos(bx) >> 4) + 0xa0;
		p.y[2] = (Lith_Sin(bx) >> 4) + 0x64;
		p.u[0] = p.u[1] = p.u[2] = 0;
		p.v[0] = p.v[1] = p.v[2] = 0;
		Lith_PolyFill(g_sweep, wedge, &p, LITH_BAND_NONE);
	}
	for (k = 0; k < 0x40; k++)
		memcpy(g_sprite + 0x6400 + k * 0xa0,
		       g_sweep + 0x5550 + k * 0x140, 0xa0);

	/* img 0x54f5 - the flicker table's starting walk. This one DOES
	 * advance the generator; only Flicker_Step restores it. */
	dx = (Lith_RndNext() >> 0xa) + 0x20;
	for (k = 0; k < 0x100; k++) {
		dx += (s32)(s16)Lith_RndNext() >> 0xb;
		if (dx < 0) dx = 0;
		if (dx > 0x40) dx = 0x40;
		g_flicker[k] = dx;
	}
}

/* What Flare_ClipSetup hands back, in the registers it leaves them. */
typedef struct {
	s32 si, bx, left, top, bottom, thresh;
} LithFlareClip;

/* ORIGIN img 0x49df Flare_ClipSetup.
 *
 * THERE IS NO SCALER ANYWHERE. The source starts at (0x50-size,
 * 0x50-size) of the 160-wide cone - bp*161 is a row and a column -
 * so a tighter crop IS a smaller, brighter disc, correctly shaped.
 * sky.txt 6.1.
 *
 * The caller's row count is bottom - top, so a fully clipped sprite
 * falls out before any row is touched. */
static void flare_clip_setup(LithFlareClip *c)
{
	s32 bp = 0x50 - g_flare_size;
	s32 si = bp + (bp << 5) + (bp << 7);
	s32 di;

	bp = Lith_SunScreenX() - g_flare_size;
	di = Lith_SunScreenY() - g_flare_size;
	if (bp < 0) { si -= bp; bp = 0; }
	if (di < 0) { si += (-di << 5) + (-di << 7); di = 0; }
	if (di >= 0xc8) di = 0xc7;
	c->si = si;
	c->left = bp;
	c->top = di;
	c->bx = bp + (di << 6) + (di << 8);
	di = Lith_SunScreenY() + g_flare_size;
	if (di >= 0xc8) di = 0xc7;
	c->bottom = di;
	c->thresh = flare_threshold();
}

/* ORIGIN img 0x4754 Flare_DrawSun - THE DEMO'S ONLY ADDITIVE WRITE,
 * and not a span routine, which is why it is absent from the five
 * g_SpanRoutine values of subsystems/raster.txt.
 *
 * THE TOP TWO BITS ARE THE PALETTE BAND and are put back untouched,
 * so the flare brightens whatever it covers without changing that
 * pixel's colour; saturating at 0x3f is what stops it carrying into
 * the next band and flipping the colour outright.
 *
 * g_FlareRamp IS NOT INDEXED BY THE FLICKERED VALUE - img 0x47b0
 * exchanges the two registers, so the ramp is read at the sprite's
 * own height above the threshold. effects/sky.txt section 6. */
static void flare_draw_sun(u8 *back)
{
	LithFlareClip c;
	s32 rows, row;

	flare_clip_setup(&c);
	rows = c.bottom - c.top;    /* img 0x4757 */
	for (row = 0; row < rows; row++) {
		s32 cx = Lith_SunScreenX() + 0x50;
		s32 si = c.si, bx = c.bx, i;

		if (cx > 0x13f) cx = 0x13f;
		cx -= c.left;
		if (cx <= 0) return;
		for (i = 0; i < cx; i++, si++, bx++) {
			s32 al = g_sprite[si] - g_flare_colour;
			s32 ax, di, ah, dh;

			if (al <= 0) continue;      /* img 0x478f - CF or ZF */
			ax = (-al) & 0xff;
			di = g_flicker[g_sprite[(si + 0x4600) & 0xffff]] - ax + 0x40;
			if (di < 0) di = 0;
			ax = di + g_flare_ramp[ax];
			if (ax > 0xff) ax = 0xff;
			ax >>= 2;
			ah = back[bx];
			dh = (ah & 0x3f) + ax;
			if (dh >= 0x40) dh = 0x3f;
			back[bx] = (u8)(dh | (ah & 0xc0));
		}
		c.bx += 0x140;
		c.si += 0xa0;
	}
}

/* ORIGIN img 0x47f5 Flare_DrawElement.
 *
 * AN ELEMENT IS A RING, NOT A DISC. It draws only where the sprite
 * lies within 0x20 above the threshold, and the strength is
 * ((0x20 - that) & 0x1f) >> 2 - brightest one step above it, gone
 * 0x1f above. The cone falls with radius, so that band is an
 * annulus, which is why two elements at one position in different
 * colours read as a ring inside a disc.
 *
 * IT OVERWRITES g_SunScreenX AND Y with its own position, so those
 * globals no longer hold the sun's after the first element - and a
 * measurement taken at the end of the frame reads an element. */
static void flare_draw_element(u8 *back, s32 ex, s32 ey, s32 colour)
{
	LithFlareClip c;
	s32 ax, bx, rows, row;

	g_flare_colour = colour;
	ax = flare_threshold();

	/* img 0x480b - the size falls as the threshold rises, so each
	 * element picks its own crop of the one cone. */
	bx = 0x50;
	ax >>= 2; bx -= ax;
	ax >>= 2; bx -= ax;
	ax >>= 4; bx -= ax;
	g_flare_size = bx;

	Lith_SunScreenSet(ex + g_sun_saved_x, ey + g_sun_saved_y);
	flare_clip_setup(&c);
	rows = c.bottom - c.top;    /* img 0x4839 */
	for (row = 0; row < rows; row++) {
		s32 cx = Lith_SunScreenX() + g_flare_size;
		s32 si = c.si, bx2 = c.bx, i;

		if (cx > 0x13f) cx = 0x13f;
		cx -= c.left;
		if (cx <= 0) return;
		for (i = 0; i < cx; i++, si++, bx2++) {
			s32 al = (g_sprite[si] - c.thresh) & 0xff;
			s32 dl, dh;

			if (al >= 0x20) continue;       /* img 0x486b */
			al = ((-al) & 0x1f) >> 2;
			dl = back[bx2];
			dh = (dl & 0x3f) + al;
			if (dh >= 0x40) dh = 0x3f;
			back[bx2] = (u8)(dh | (dl & 0xc0));
		}
		c.bx += 0x140;
		c.si += 0xa0;
	}
}

/* ORIGIN img 0x48a3 Blob_DrawKeepBand and img 0x4943 Blob_DrawBurn.
 * The two are one instruction apart, so the body is transcribed
 * once here and the pair below is what the original names; the
 * merge is ours and the `burn` flag is the instruction.
 *
 * IT BORROWS THE FLARE'S PLUMBING and nothing else: the same cone,
 * the same clip setup, the same size chain off the colour - but it
 * SUBTRACTS the colour from the sprite and takes what is left,
 * where an element keeps only a 0x20-wide band. So a blob is a
 * disc that grows as the colour falls, not a ring.
 *
 * THE ORIGINAL TRACKS THE DIRTY BOX HERE TOO, img 0x48f5. Nothing
 * downstream of ours reads one. */
static void blob_draw(u8 *back, s32 ex, s32 ey, int burn)
{
	LithFlareClip c;
	s32 ax, bx, rows, row;

	ax = g_flare_colour & 0xff;             /* img 0x48a3 */
	bx = 0x50;
	ax >>= 2; bx -= ax;
	ax >>= 2; bx -= ax;
	ax >>= 4; bx -= ax;
	g_flare_size = bx;

	Lith_SunScreenSet(ex + g_sun_saved_x, ey + g_sun_saved_y);
	flare_clip_setup(&c);
	rows = c.bottom - c.top;                /* img 0x48d7 */
	for (row = 0; row < rows; row++) {
		s32 cx = Lith_SunScreenX() + g_flare_size;
		s32 si = c.si, bx2 = c.bx, i;

		if (cx > 0x13f) cx = 0x13f;         /* img 0x48ec */
		cx -= c.left;
		if (cx <= 0) return;
		for (i = 0; i < cx; i++, si++, bx2++) {
			s32 al = g_sprite[si] - (g_flare_colour & 0xff);
			s32 dl, dh;

			if (al < 0) continue;           /* img 0x490f, JC */
			al >>= 3;
			dl = back[bx2];
			dh = (dl & 0x3f) + al;
			if (dh >= 0x40) dh = 0x3f;
			back[bx2] = (u8)(burn ? (dh | 0xc0)
			                      : (dh | (dl & 0xc0)));
		}
		c.bx += 0x140;
		c.si += 0xa0;
	}
}

/* ORIGIN img 0x48a3 - it keeps whatever band the pixel was in, so
 * the blob only brightens what is under it. */
void Lith_BlobDrawKeepBand(u8 *back, s32 ex, s32 ey)
{
	blob_draw(back, ex, ey, 0);
}

/* ORIGIN img 0x4943 - it FORCES band 3, so a bolt reads as its own
 * colour rather than as a brightening of the terrain under it. */
void Lith_BlobDrawBurn(u8 *back, s32 ex, s32 ey)
{
	blob_draw(back, ex, ey, 1);
}

/* OURS - the blobs place themselves at centre plus projection by
 * setting the pair the blitters add. img 0x27b1. */
void Lith_FlareSavedSet(s32 x, s32 y)
{
	g_sun_saved_x = x;
	g_sun_saved_y = y;
}

/* OURS - Trail_Draw sets the colour directly, img 0x27ab. */
void Lith_FlareColourSet(s32 c) { g_flare_colour = c; }

/* ORIGIN img 0x46f3 Sun_GlareFade - the full-screen wash.
 *
 * IT READS THE CONE AS A FALLOFF, at half resolution: screen (160,
 * 100) maps to the cone's centre, so the nearer the sun is to the
 * middle of the screen the larger the stored value, the smaller the
 * divisor, and the more the whole palette bleaches. Tick_Default
 * eases g_PalFade toward it, so nothing in the fade path knows the
 * sun exists. */
void Lith_SunGlareFade(void)
{
	s32 bx, ecx;

	if ((u32)Lith_SunScreenY() >= 0xc8
	    || (u32)Lith_SunScreenX() >= 0x140) {
		Lith_PalFadeTo(0);
		return;
	}
	bx = ((0x104 - Lith_SunScreenY()) >> 1) * 0xa0
	     + (Lith_SunScreenX() >> 1);
	ecx = 0x104 - g_sprite[bx];
	Lith_PalFadeTo((s32)(((u32)(0x8200 - Lith_FlareEnergy()) >> 3)
	                     / (u32)ecx));
}

/* ORIGIN img 0x4476 Flare_Render.
 *
 * (a, b) IS THE VECTOR FROM THE SUN TO THE CENTRE OF THE SCREEN and
 * every element sits at a multiple of it, open-coded in shifts and
 * adds - which is why this is straight line rather than a loop.
 *
 * THE SHIFTS COMPOUND WITHIN A BLOCK: a block's second shift sees
 * the first one's result, so they are not independent fractions of
 * the vector. The multiples, the colours and the rounding are
 * effects/sky.txt section 5.
 *
 * The elements are skipped unless the sun itself is on screen - img
 * 0x44d4 - while the sun's own disc is drawn clipped. */
void Lith_FlareRender(u8 *back)
{
	s32 a, b, cx, dx, ax, bx;

	if (Lith_SunScreenX() == -0x20 && Lith_SunScreenY() == -0x20) return;
	Lith_SunGlareFade();
	if (Lith_FlareEnergy() >= 0x7f00) return;

	a = 0xa0 - Lith_SunScreenX();
	b = 0x64 - Lith_SunScreenY();
	g_flare_colour = Lith_FlareBrightness();
	g_flare_size = 0x50;
	flare_draw_sun(back);
	g_sun_saved_x = Lith_SunScreenX();
	g_sun_saved_y = Lith_SunScreenY();
	if ((u32)g_sun_saved_x >= 0x140 || (u32)g_sun_saved_y >= 0xc8) return;

	/* 1 - img 0x44ec, 2 + 1/4 + 1/8 */
	ax = a; bx = b; cx = 0; dx = 0;
	ax += ax; bx += bx; cx += ax; dx += bx;
	ax >>= 3; bx >>= 3; cx += ax; dx += bx;
	ax >>= 1; bx >>= 1; cx += ax; dx += bx;
	flare_draw_element(back, cx, dx, 0x01);

	/* 2 - img 0x451e, 2 */
	flare_draw_element(back, a + a, b + b, 0x80);

	/* 3 - img 0x453e, -1/2 + 1/8 + 1/32 */
	ax = a; bx = b; cx = 0; dx = 0;
	ax >>= 1; bx >>= 1; cx -= ax; dx -= bx;
	ax >>= 2; bx >>= 2; cx += ax; dx += bx;
	ax >>= 2; bx >>= 2; cx += ax; dx += bx;
	flare_draw_element(back, cx, dx, 0xc0);

	/* 4 - img 0x4572, 1/2 */
	flare_draw_element(back, a >> 1, b >> 1, 0xac);

	/* 5 AND 6 SHARE A POSITION - img 0x45ac pushes it back */
	ax = a; bx = b; cx = 0; dx = 0;
	cx += ax; dx += bx;
	ax >>= 1; bx >>= 1; cx += ax; dx += bx;
	ax >>= 2; bx >>= 2; cx -= ax; dx -= bx;
	flare_draw_element(back, cx, dx, 0x9c);
	flare_draw_element(back, cx, dx, 0xe4);

	/* 7 - img 0x45d4, 1/2 + 1/16 */
	ax = a; bx = b; cx = 0; dx = 0;
	ax >>= 1; bx >>= 1; cx += ax; dx += bx;
	ax >>= 3; bx >>= 3; cx += ax; dx += bx;
	flare_draw_element(back, cx, dx, 0xd6);

	/* 8 - img 0x45fe, 1 + 1/2 - 1/8 - 1/16 */
	ax = a; bx = b; cx = 0; dx = 0;
	cx += ax; dx += bx;
	ax >>= 1; bx >>= 1; cx += ax; dx += bx;
	ax >>= 2; bx >>= 2; cx -= ax; dx -= bx;
	ax >>= 1; bx >>= 1; cx -= ax; dx -= bx;
	flare_draw_element(back, cx, dx, 0xce);

	/* 9 - img 0x4634, 1/2 - 1/16 */
	ax = a; bx = b; cx = 0; dx = 0;
	ax >>= 1; bx >>= 1; cx += ax; dx += bx;
	ax >>= 3; bx >>= 3; cx -= ax; dx -= bx;
	flare_draw_element(back, cx, dx, 0xe4);

	/* 10 - img 0x465e, 1/2 + 1/4 */
	ax = a; bx = b; cx = 0; dx = 0;
	ax >>= 1; bx >>= 1; cx += ax; dx += bx;
	ax >>= 1; bx >>= 1; cx += ax; dx += bx;
	flare_draw_element(back, cx, dx, 0xe4);

	/* 11 - img 0x4686, 1 */
	flare_draw_element(back, a, b, 0xf6);

	/* 12 - img 0x469e, 1 + 1/8 + 1/32 */
	ax = a; bx = b; cx = ax; dx = bx;
	ax >>= 3; bx >>= 3; cx += ax; dx += bx;
	ax >>= 2; bx >>= 2; cx += ax; dx += bx;
	flare_draw_element(back, cx, dx, 0xf0);

	/* 13 - img 0x46ca, 1 + 1/2 + 1/8 */
	ax = a; bx = b; cx = ax; dx = bx;
	ax >>= 1; bx >>= 1; cx += ax; dx += bx;
	ax >>= 2; bx >>= 2; cx += ax; dx += bx;
	flare_draw_element(back, cx, dx, 0xdc);
}
