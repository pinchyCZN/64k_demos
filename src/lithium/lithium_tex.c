/* LITHIUM - the environment map, generated with no stored art.
 *
 * ORIGIN img 0x5899 Tex_BuildSphereMap.
 * docs/lithium/subsystems/texture.txt.
 */

#include <string.h>

#include "lithium.h"

static u16 g_rnd0, g_rnd1;

/* ORIGIN img 0x8f20 - g_RndState0 and g_RndState1, seeded IN THE
 * IMAGE. A ZERO STATE IS A FIXED POINT of this generator, so the seed
 * is load-bearing and cannot be invented: from (0, 0) it returns the
 * same number for ever and the map comes out blank. */
void Lith_RndReset(void)
{
	if (!g_lith_img) return;
	g_rnd0 = RD_U16(g_lith_img + LITH_RND_SEED);
	g_rnd1 = RD_U16(g_lith_img + LITH_RND_SEED + 2);
}

/* OURS - Terrain_Generate seeds the pair directly at img 0x52dd
 * rather than from the image, so it needs a way in. */
void Lith_RndSeed(u16 a, u16 b)
{
	g_rnd0 = a;
	g_rnd1 = b;
}

/* OURS - Flicker_Step keeps the pair across its walk and writes it
 * back at img 0x60bf, so it needs a way out. */
void Lith_RndGet(u16 *a, u16 *b) { *a = g_rnd0; *b = g_rnd1; }

/* ORIGIN img 0x3dfc Rnd_Next. THE ROL AFFECTS THE RETURNED VALUE
 * ONLY, not the state that is carried forward. */
u16 Lith_RndNext(void)
{
	u16 ax = g_rnd0, dx = g_rnd1;
	u16 ah = (u16)((ax >> 8) & 0xff), al = (u16)(ax & 0xff);
	u16 dh = (u16)((dx >> 8) & 0xff), dl = (u16)(dx & 0xff);
	u16 t;

	t = ah; ah = dl; dl = t;            /* XCHG AH,DL */
	ax = (u16)((ah << 8) | al);
	dx = (u16)((dh << 8) | dl);
	ax = (u16)(ax + dx);
	g_rnd0 = dx;
	g_rnd1 = ax;
	return (u16)((ax << 2) | (ax >> 14));
}

/* ORIGIN img 0x5721 Tex_BuildThreeGlows - the picture the bump map
 * refracts, built at img 0x029d immediately before that scene.
 *
 * THE COUNTERS RUN DOWN WHILE DI RUNS UP, so a centre written as c
 * lands at pixel 0x100 - c. Transcribed as the original has it
 * rather than with the centres pre-flipped.
 *
 * NOTHING IN THE BUMP CODE KNOWS THERE ARE THREE. The count is a
 * property of this picture, and it is why more than one highlight
 * appears on the surface. docs/lithium/effects/bump.txt section 4.
 *
 * EACH DISC IS CLIPPED TO A SQUARE FIRST - the 0x3f tests reject on
 * each axis before the distance is taken, so the sweep is cheap and
 * the discs are hard-edged at 0x3f. */
void Lith_TexBuildThreeGlows(u8 *buf)
{
	static const s32 gx[3] = { 0x40, 0xb0, 0xc0 };
	static const s32 gy[3] = { 0x70, 0xc0, 0x40 };
	s32 di = 0, tx, ty, g;

	memset(buf, 0, LITH_SEG_BYTES);
	for (ty = 0x100; ty; ty--) {
		for (tx = 0x100; tx; tx--) {
			for (g = 0; g < 3; g++) {
				s32 bx = tx - gx[g], v, al;

				if (bx < 0) bx = -bx;
				if (bx >= 0x3f) continue;
				v = Lith_Square(bx);
				bx = ty - gy[g];
				if (bx < 0) bx = -bx;
				if (bx >= 0x3f) continue;
				v += Lith_Square(bx);
				/* MATH_SQRT RETURNS A BYTE OFFSET - img
				 * 0x4a91 leaves the table index times
				 * four, so this shift is not optional. */
				al = 0x3f - (Lith_MathSqrt(v) >> 2);
				if (al > 0) buf[di] = (u8)al;
			}
			di++;
		}
	}
}

/* ORIGIN img 0x3dfc Rnd_Next, TAKING BOTH REGISTERS. The routine
 * returns a rotated AX and leaves DX holding what it just stored as
 * the first state word, and several callers use both - Tick_Dust's
 * mover and Dust_SampleMask among them. Calling Lith_RndNext twice
 * is NOT the same thing. */
void Lith_RndNextPair(u16 *rot, u16 *dx)
{
	*rot = Lith_RndNext();
	*dx = g_rnd0;
}

/* ORIGIN img 0x3e16 Rnd_NextB - A SECOND GENERATOR ON ITS OWN STATE,
 * g_RndStateB0 and B1 at img 0x8fc7. The separation is necessary:
 * Sky_SunAndStars reseeds the main pair every frame and Flicker_Step
 * perturbs it, either of which would wreck anything that must not
 * repeat - Bump_RandomiseLight, the launcher and the two bursts.
 *
 * BOTH WORDS ARE ZERO IN THE IMAGE and nothing read seeds them, so
 * the seed is OURS - any non-zero pair will do, a zero state being
 * this generator's fixed point. */
static u16 g_rndb0, g_rndb1;

/* OURS - the image's own loaded pair, which is what the demo still
 * has when the bump scene asks: it is the first caller. */
void Lith_RndBReset(void)
{
	if (!g_lith_img) return;
	g_rndb0 = RD_U16(g_lith_img + LITH_RNDB_SEED);
	g_rndb1 = RD_U16(g_lith_img + LITH_RNDB_SEED + 2);
}

void Lith_RndBSeed(u16 a, u16 b) { g_rndb0 = a; g_rndb1 = b; }

/* ORIGIN img 0x3e16 Rnd_NextB - see Lith_RndBSeed above. */
u16 Lith_RndNextB(void)
{
	u16 ax = g_rndb0, dx = g_rndb1;
	u16 ah = (u16)((ax >> 8) & 0xff), al = (u16)(ax & 0xff);
	u16 dh = (u16)((dx >> 8) & 0xff), dl = (u16)(dx & 0xff);
	u16 t;

	t = ah; ah = dl; dl = t;
	ax = (u16)((ah << 8) | al);
	dx = (u16)((dh << 8) | dl);
	ax = (u16)(ax + dx);
	g_rndb0 = dx;
	g_rndb1 = ax;
	return (u16)((ax << 2) | (ax >> 14));
}

/* ORIGIN img 0x597f Tex_Plot. A 16x16 additive disc.
 *
 * THE DIAGONAL REJECT IS THE POINT. A plot is dropped unless
 * x + y >= 0x46, which leaves the (0,0) corner much darker than the
 * rest - and that corner is where Scene_ProjectVerts drags a distant
 * surface's map coordinates, so THE DARK CORNER IS THE FOG.
 *
 * x and y are compared UNSIGNED, so a negative coordinate arrives as
 * a large word and is rejected. */
static void tex_plot(u8 *buf, u32 x, u32 y)
{
	u16 si;
	u32 di;
	s32 r, c;

	/* JNC, so >= - subsystems/texture.txt 3.1. */
	if (y >= 0xff || x >= 0xff) return;
	if (x + y < 0x46) return;
	si = (u16)((y << 8) + x);
	di = LITH_BRUSH;
	for (r = 0; r < 0x10; r++) {
		for (c = 0; c < 0x10; c++) {
			s32 v = buf[si] + g_lith_img[di];
			buf[si] = (u8)((v & 0x80) ? 0x7f : v);  /* JNS saturates */
			si = (u16)(si + 1);     /* SI is 16-bit and WRAPS */
			di++;
		}
		si = (u16)(si + 0xf0);      /* 0x100 - 0x10, the next row */
	}
}

/* ORIGIN img 0x5938 Tex_Stroke. ONE dot on a circle of random radius
 * about (sx, sy) - it does not walk a path, and hundreds of calls are
 * what make a soft blob. */
static void tex_stroke(u8 *buf, u16 sx, u16 sy)
{
	s32 bx = (Lith_RndNext() & 0x1ffc) >> 2;
	s32 r = (Lith_RndNext() >> 3) + 0xa0;
	const s32 *t = Lith_SinTable();
	u16 x, y;

	if (!t) return;
	x = (u16)((t[bx] * r) >> 0x12);
	y = (u16)((t[bx + LITH_SIN_QUARTER] * r) >> 0x12);
	tex_plot(buf, (u16)(x + sx), (u16)(y + sy));
}

/* ORIGIN img 0x4aee Tex_Blur. The mean of the four neighbours at -1,
 * +1, +0x100 and -0x100, so THE MAP IS 256 WIDE and DI wrapping at 16
 * bits makes it 256x256.
 *
 * IT BLURS IN PLACE AND THAT IS LOAD-BEARING: the left and upper
 * neighbours have already been replaced this pass, which makes the
 * smear directional. A two-buffer blur gives a different texture. */
void Lith_TexBlur(u8 *buf, s32 times)
{
	s32 n;

	for (n = 0; n < times; n++) {
		u32 di = 0;
		do {
			s32 v = buf[(u16)(di - 1)] + buf[(u16)(di + 1)]
			      + buf[(u16)(di + 0x100)] + buf[(u16)(di - 0x100)];
			buf[di] = (u8)(v >> 2);
			di = (u16)(di + 1);
		} while (di);
	}
}

/* ORIGIN img 0x5899 Tex_BuildSphereMap. The whole environment map in
 * five steps and NO STORED ART: clear, 35 random walks of 450 dots,
 * 85 radial streaks of 250, eight blurs, then halve every byte so the
 * result is 0 to 0x7f - which is the range one palette ramp holds. */
void Lith_TexBuildSphereMap(u8 *buf)
{
	s32 i;
	const s32 *t = Lith_SinTable();

	if (!g_lith_img || !t) return;
	Lith_RndReset();
	memset(buf, 0, LITH_TEX_BYTES);

	for (i = 0; i < 0x23; i++) {
		/* XOR AH,AH then NOT AL - so the seed is a complemented byte. */
		u16 sx = (u16)((~Lith_RndNext()) & 0xff);
		u16 sy = (u16)((~Lith_RndNext()) & 0xff);
		s32 k;
		for (k = 0; k < 0x1c2; k++) tex_stroke(buf, sx, sy);
	}

	for (i = 0; i < 0x55; i++) {
		s32 bx = (Lith_RndNext() & 0x1ffc) >> 2;
		s32 r = Lith_RndNext() >> 4;
		u16 sx = (u16)(((t[bx] * r) >> 0x12) + 0xde);
		u16 sy = (u16)(((t[bx + LITH_SIN_QUARTER] * r) >> 0x12) + 0xde);
		s32 k;
		for (k = 0; k < 0xfa; k++) tex_stroke(buf, sx, sy);
	}

	Lith_TexBlur(buf, 8);
	for (i = 0; i < LITH_TEX_BYTES; i++)
		buf[i] >>= 1;               /* SHR byte ptr [DI],1 */
}
