/* XCURSION - the palette the demo builds.
 *
 * g_PaletteBase is all zeroes in the image: this is not data to be
 * read, it is code to be run - subsystems/palette.txt.
 */

#include <string.h>

#include "xcursion.h"

/* ORIGIN img 0x3b65 - Pal_BuildMain. Seven bands: six of 32 and one
 * of 64.
 *
 * BL STEPS BY TWO ACROSS A 32-BAND, so a component spans 0 to 62 of
 * the DAC's 0 to 63 and no band quite reaches full. That off-by-one
 * is the demo's, not a transcription slip - do not round it up.
 *
 * Indices 96 to 191 are the fire ramp, which is what the caption
 * fill writes into: black to red, red to yellow, yellow to white.
 */
void Xcur_PalBuild(u8 *pal)
{
	s32 n, i = 0;

#define PUT(r, g, b) do {                 \
        pal[i * 3 + 0] = (u8)((r) & 0x3f); \
        pal[i * 3 + 1] = (u8)((g) & 0x3f); \
        pal[i * 3 + 2] = (u8)((b) & 0x3f); \
        i++;                               \
	} while (0)

	for (n = 0; n < 0x20; n++) {            /* 0..31    land  */
		s32 bl = n * 2;
		PUT(bl, bl >> 1, bl >> 2);
	}
	for (n = 0; n < 0x20; n++) {            /* 32..63   water */
		s32 bl = n * 2;
		PUT(bl >> 2, bl >> 1, bl);
	}
	for (n = 0; n < 0x20; n++) {            /* 64..95   pale to white */
		s32 bl = n * 2;
		PUT((bl >> 2) + 0x10, (bl >> 1) + 0x20, 0x3f);
	}
	for (n = 0; n < 0x20; n++)              /* 96..127  black to red */
		PUT(n * 2, 0, 0);
	for (n = 0; n < 0x20; n++)              /* 128..159 red to yellow */
		PUT(0x3f, n * 2, 0);
	for (n = 0; n < 0x20; n++)              /* 160..191 yellow to white */
		PUT(0x3f, 0x3f, n * 2);
	for (n = 0; n < 0x40; n++)              /* 192..255 grey */
		PUT(n, n, n);
#undef PUT
}

/* ORIGIN img 0x3b17 - Pal_Fade. One bias on every DAC component,
 * clamped to 0..0x3f: negative is black, positive white. OUT OF
 * RANGE REVERTS rather than clamping - img 0x3b22. */
void Xcur_PalFade(u8 *out, const u8 *master, s32 *level, s32 *prev)
{
	s32 bx = (s32)(s16)*level, bias, i;

	if (bx < -0x4000) bx = (s32)(s16)*prev;     /* img 0x3b1c */
	if (bx >  0x4000) bx = (s32)(s16)*prev;     /* img 0x3b27 */
	*prev = bx;
	*level = bx;

	bias = bx >> 8;                             /* img 0x3b43, sar */
	for (i = 0; i < 768; i++) {
		s32 v = (s32)master[i] + bias;          /* img 0x3b51 */
		if (v > 0x3f) v = 0x3f;
		else if (v < 0) v = 0;
		out[i] = (u8)v;
	}
}

/* ORIGIN img 0x3e8d - Pal_BuildGreenRamp. Rewrites the fire band in
 * place, so Pal_BuildMain runs first and this replaces indices 96 to
 * 191. The land, water and grey bands are left alone. */
void Xcur_PalBuildGreen(u8 *pal)
{
	s32 n, i = 96;

	for (n = 0; n < 0x40; n++) {        /* 96..159  black to green */
		pal[i * 3 + 0] = 0;
		pal[i * 3 + 1] = (u8)n;
		pal[i * 3 + 2] = 0;
		i++;
	}
	for (n = 0; n < 0x20; n++) {        /* 160..191 green to white */
		pal[i * 3 + 0] = (u8)(n * 2);
		pal[i * 3 + 1] = 0x3f;
		pal[i * 3 + 2] = (u8)(n * 2);
		i++;
	}
}

/* ORIGIN img 0x3bf4 - Pal_BuildBlue. Replaces indices 0 to 127 only,
 * so whatever the previous palette left above them survives.
 *
 * THE HALF-BUILD IS DELIBERATE, not unfinished. All three callers -
 * img 0x030d, img 0x03ce and img 0x07ce - index it with a field that
 * has been halved into 0..127, by img 0x0322 for the first and img
 * 0x07fa for the last, and the second inherits the first's. Filling
 * the upper half, or leaving a field full-range, each break the
 * other. */
void Xcur_PalBuildBlue(u8 *pal)
{
	s32 n, i = 0;

	for (n = 0; n < 0x40; n++) {        /* 0..63    black to blue */
		pal[i * 3 + 0] = 0;
		pal[i * 3 + 1] = 0;
		pal[i * 3 + 2] = (u8)n;
		i++;
	}
	for (n = 0; n < 0x40; n++) {        /* 64..127  blue to cyan */
		pal[i * 3 + 0] = 0;
		pal[i * 3 + 1] = (u8)n;
		pal[i * 3 + 2] = 0x3f;
		i++;
	}
}

/* ORIGIN img 0x3c1d - the warp part's palette, and the reason its
 * burst reads BLUE. Seven bands; the glow's 0x60..0xbf window lands
 * on bands 4 to 6, which run black to blue to cyan to white, where
 * Pal_BuildMain puts a fire ramp.
 *
 * BL steps by 2 across each 32-entry band, so every band covers
 * 0..0x3e rather than 0..0x1f. */
void Xcur_PalBuildWarp(u8 *pal)
{
	s32 n, i = 0;

	for (n = 0; n < 0x40; n += 2) {         /* img 0x3c29, warm    */
		pal[i*3] = (u8)n; pal[i*3+1] = (u8)(n >> 1);
		pal[i*3+2] = (u8)(n >> 2); i++;
	}
	for (n = 0; n < 0x40; n += 2) {         /* img 0x3c3c, blue    */
		pal[i*3] = (u8)(n >> 2); pal[i*3+1] = (u8)(n >> 1);
		pal[i*3+2] = (u8)n; i++;
	}
	for (n = 0; n < 0x40; n += 2) {         /* img 0x3c54          */
		pal[i*3] = (u8)((n >> 2) + 0x10);
		pal[i*3+1] = (u8)((n >> 1) + 0x20);
		pal[i*3+2] = 0x3f; i++;
	}
	for (n = 0; n < 0x40; n += 2) {         /* img 0x3c70          */
		pal[i*3] = 0; pal[i*3+1] = 0; pal[i*3+2] = (u8)n; i++;
	}
	for (n = 0; n < 0x40; n += 2) {         /* img 0x3c80          */
		pal[i*3] = 0; pal[i*3+1] = (u8)n; pal[i*3+2] = 0x3f; i++;
	}
	for (n = 0; n < 0x40; n += 2) {         /* img 0x3c93          */
		pal[i*3] = (u8)n; pal[i*3+1] = 0x3f; pal[i*3+2] = 0x3f; i++;
	}
	for (n = 0; n < 0x40; n++) {            /* img 0x3ca4, grey    */
		pal[i*3] = (u8)n; pal[i*3+1] = (u8)n; pal[i*3+2] = (u8)n; i++;
	}
}

/* ORIGIN img 0x3cac - Pal_BuildBluePurple. Six bands that return to
 * where they started, which is what lets Pal_Cycle rotate it without
 * a seam: black to blue, out through magenta to white, and back. */
void Xcur_PalBuildBluePurple(u8 *pal)
{
	s32 n, i = 0;

	for (n = 0; n < 0x40; n++) {            /* black to blue      */
		pal[i*3] = 0; pal[i*3+1] = 0; pal[i*3+2] = (u8)n; i++;
	}
	for (n = 0; n < 0x20; n++) {            /* blue to magenta    */
		pal[i*3] = (u8)(n*2); pal[i*3+1] = 0; pal[i*3+2] = 0x3f; i++;
	}
	for (n = 0; n < 0x20; n++) {            /* magenta to white   */
		pal[i*3] = 0x3f; pal[i*3+1] = (u8)(n*2); pal[i*3+2] = 0x3f; i++;
	}
	for (n = 0; n < 0x20; n++) {            /* white back         */
		pal[i*3] = 0x3f; pal[i*3+1] = (u8)(0x3f-n*2); pal[i*3+2] = 0x3f; i++;
	}
	for (n = 0; n < 0x20; n++) {            /* magenta back       */
		pal[i*3] = (u8)(0x3f-n*2); pal[i*3+1] = 0; pal[i*3+2] = 0x3f; i++;
	}
	for (n = 0; n < 0x40; n++) {            /* blue to black      */
		pal[i*3] = 0; pal[i*3+1] = 0; pal[i*3+2] = (u8)(0x3f-n); i++;
	}
}

/* ORIGIN img 0x3d1e - Pal_BuildRgbRamps, and mode 0xf calls it EVERY
 * TICK. Only the 64 entries the beams paint, each channel a linear
 * ramp whose slope is (cos(phase) + 0x1000) >> 5 and whose phase
 * advances at 3, 7 or 9 - so the hue combination never repeats
 * quickly. ONLY THE HIGH BYTE of each accumulator reaches the
 * palette, img 0x3d78. effects/beams.txt section 8. */
void Xcur_PalBuildRgbRamps(u8 *pal, s32 *phase)
{
	s32 sl[3], c, i;

	for (c = 0; c < 3; c++) {
		/* a WORD read of the dword table, so 0x1000 +/- 4095 */
		sl[c] = (((Xcur_Cos(phase[c]) & 0xffff) + 0x1000) & 0xffff) >> 5;
	}
	phase[0] = (phase[0] + 3) & 0xffff;         /* img 0x3d60 */
	phase[1] = (phase[1] + 7) & 0xffff;
	phase[2] = (phase[2] + 9) & 0xffff;

	for (c = 0; c < 3; c++) {
		s32 acc = 0;

		for (i = 0; i < 0x40; i++) {            /* img 0x3d78 */
			pal[i * 3 + c] = (u8)((acc >> 8) & 0xff);
			acc = (acc + sl[c]) & 0xffff;
		}
	}
}

/* ORIGIN img 0x3df6 - the tunnel's palette: 192 entries, black to
 * blue to magenta and back to black. It runs AFTER Pal_BuildMain in
 * its part, so it overwrites the first three bands and leaves the
 * rest. The tunnel's texture is the halved plasma, 0..127, so only
 * the first two bands are ever indexed. */
void Xcur_PalBuildBlueMagenta(u8 *pal)
{
	s32 n, i = 0;

	for (n = 0; n < 0x40; n++) {            /* img 0x3e02, to blue */
		pal[i*3] = 0; pal[i*3+1] = 0; pal[i*3+2] = (u8)n; i++;
	}
	for (n = 0; n < 0x40; n++) {            /* img 0x3e11, to magenta */
		pal[i*3] = (u8)n; pal[i*3+1] = 0; pal[i*3+2] = 0x3f; i++;
	}
	for (n = 0x3f; n >= 0; n--) {           /* img 0x3e23, to black */
		pal[i*3] = (u8)n; pal[i*3+1] = 0; pal[i*3+2] = (u8)n; i++;
	}
}

/* ORIGIN img 0x3e31 - Pal_SetRgbRamps, and it writes FOUR BANDS OF 64
 * STRAIGHT TO THE DAC. It bypasses g_PaletteBase entirely, so no fade
 * can touch it and nothing needs to - which is why the water's part
 * never calls Pal_Fade.
 *
 * BL counts 0 to 0x3f, so a component reaches the DAC's full 63;
 * Pal_BuildMain's ramps step by 2 and stop one short. The spans land
 * around 1..104, so the water reads black through blue into cyan with
 * no fire band anywhere. */
void Xcur_PalSetRgbRamps(u8 *pal)
{
	s32 n, i = 0;

	for (n = 0; n < 0x40; n++) {            /* img 0x3e3d, to blue */
		pal[i*3] = 0; pal[i*3+1] = 0; pal[i*3+2] = (u8)n; i++;
	}
	for (n = 0; n < 0x40; n++) {            /* img 0x3e4e, to cyan */
		pal[i*3] = 0; pal[i*3+1] = (u8)n; pal[i*3+2] = 0x3f; i++;
	}
	for (n = 0; n < 0x40; n++) {            /* img 0x3e61, to white */
		pal[i*3] = (u8)n; pal[i*3+1] = 0x3f; pal[i*3+2] = 0x3f; i++;
	}
	for (n = 0; n < 0x40; n++) {            /* img 0x3e72, flat white */
		pal[i*3] = 0x3f; pal[i*3+1] = 0x3f; pal[i*3+2] = 0x3f; i++;
	}
}

/* ORIGIN img 0x3aee - the tunnel's cycle, and NOT Pal_Cycle at img
 * 0x3ac5. This one moves 0x23a bytes, so it rotates entries 1 to 191
 * and leaves 0 and everything above 191 alone - the span the tunnel's
 * own palette covers. */
void Xcur_PalCycleBands(u8 *pal)
{
	u8 keep[3];

	keep[0] = pal[3]; keep[1] = pal[4]; keep[2] = pal[5];
	memmove(pal + 3, pal + 6, 0x23a);           /* img 0x3b0a */
	pal[191*3] = keep[0];                       /* img 0x3b0c */
	pal[191*3+1] = keep[1];
	pal[191*3+2] = keep[2];
}

/* ORIGIN img 0x3ac5 - Pal_Cycle. Rotates entries 1..255 down by one
 * and wraps entry 1 round to entry 255.
 *
 * ENTRY 0 IS LEFT ALONE - img 0x3ad8 starts the move at entry 1, so
 * the background stays put while everything above it cycles. */
void Xcur_PalCycle(u8 *pal)
{
	u8 keep[3];
	s32 i;

	keep[0] = pal[3]; keep[1] = pal[4]; keep[2] = pal[5];
	for (i = 3; i < 765; i++) pal[i] = pal[i + 3];
	pal[765] = keep[0]; pal[766] = keep[1]; pal[767] = keep[2];
}

/* ORIGIN img 0x3e79 - Pal_BuildGreyLow. THIRTY-TWO ENTRIES, not 256:
 * it writes g_PaletteBase itself and stops, leaving everything above
 * whatever the last build left there.
 *
 * That narrow band is the whole moon. Planet_Render's own
 * (texel >> 2) - 0x20 undoes the (shade << 2) + 0x80 the part applies
 * at img 0x4e64, so the index reaching the screen IS the shade, and
 * Terrain_BuildShade clamps that to 1 .. 0x1e. effects/planet.txt. */
void Xcur_PalBuildGreyLow(u8 *pal)
{
	s32 i, v = 0;

	for (i = 0; i < 0x20; i++) {
		pal[i * 3 + 0] = (u8)v;
		pal[i * 3 + 1] = (u8)v;
		pal[i * 3 + 2] = (u8)v;
		v = (v + 2) & 0xff;                     /* img 0x3e88 */
	}
}

/* ORIGIN img 0x3eba - Pal_BuildBrownRamp. Sixty-four entries from 96,
 * and the three components are ONE value shifted twice: r = n,
 * g = n >> 1, b = n >> 2. A brown that brightens to orange, built
 * without a table. */
void Xcur_PalBuildBrownRamp(u8 *pal)
{
	s32 i;

	for (i = 0; i < 0x40; i++) {
		s32 v = i & 0xff;

		pal[(96 + i) * 3 + 0] = (u8)v;
		v >>= 1;                                /* img 0x3ec9 */
		pal[(96 + i) * 3 + 1] = (u8)v;
		v >>= 1;
		pal[(96 + i) * 3 + 2] = (u8)v;
	}
}

/* ORIGIN img 0x3ed4 - the credits' palette. Four bands of 64, and
 * the fourth is why the stars and the text can share one buffer:
 *
 *     0.. 63   0, 0, n        black -> blue
 *    64..127   0, n, 0x3f     blue  -> cyan
 *   128..191   n, 0x3f, 0x3f  cyan  -> white
 *   192..255   n, n, n        a GREY RAMP
 *
 * The text's source stops at 0xbf and the stars start at 0xc0, which
 * is how one buffer carries both. effects/scroller.txt. */
void Xcur_PalBuildCredits(u8 *pal)
{
	s32 n;

	for (n = 0; n < 0x40; n++) {                /* img 0x3ede */
		pal[n * 3 + 0] = 0;
		pal[n * 3 + 1] = 0;
		pal[n * 3 + 2] = (u8)n;
	}
	for (n = 0; n < 0x40; n++) {                /* img 0x3eef */
		pal[(0x40 + n) * 3 + 0] = 0;
		pal[(0x40 + n) * 3 + 1] = (u8)n;
		pal[(0x40 + n) * 3 + 2] = 0x3f;
	}
	for (n = 0; n < 0x40; n++) {                /* img 0x3f06 */
		pal[(0x80 + n) * 3 + 0] = (u8)n;
		pal[(0x80 + n) * 3 + 1] = 0x3f;
		pal[(0x80 + n) * 3 + 2] = 0x3f;
	}
	for (n = 0; n < 0x40; n++) {                /* img 0x3f16 */
		pal[(0xc0 + n) * 3 + 0] = (u8)n;
		pal[(0xc0 + n) * 3 + 1] = (u8)n;
		pal[(0xc0 + n) * 3 + 2] = (u8)n;
	}
}

/* ORIGIN img 0x3f1e - THE END's palette, built in the credits part's
 * tail at img 0x5287 rather than in the part that uses it.
 *
 * IT IS CYCLIC WITH A PERIOD OF SIXTEEN. The loops write each entry
 * FOUR TIMES - at DI and at DI+0x30, +0x60 and +0x90, which is 16
 * entries apart - so 1 .. 64 are four copies of one ramp:
 *
 *     1 ..  8   (0, g, 0x3f)  g rising  0 to 49 by 7   blue -> cyan
 *     9 .. 16   (0, g, 0x3f)  g falling 56 to 7 by 7   cyan -> blue
 *
 * The letters' shades land in 8 .. 50, so they cross the cycle three
 * times and band blue, cyan, blue - which is the effect. A single
 * 64-entry ramp would give one smooth wash and no banding.
 *
 * img 0x3f9b then puts a dark RED ramp at 224, and 65 .. 223 keep
 * whatever the previous palette left. effects/theend.txt. */
void Xcur_PalBuildCyanCycle(u8 *pal)
{
	s32 i, bl = 0, e = 1;

	pal[0] = 0; pal[1] = 0; pal[2] = 0;         /* img 0x3f25 */
	for (i = 0; i < 8; i++, e++) {              /* img 0x3f2e */
		pal[e * 3 + 0] = 0;
		pal[e * 3 + 1] = (u8)bl;
		pal[e * 3 + 2] = 0x3f;
		bl = (bl + 7) & 0xff;                   /* img 0x3f5e */
	}
	for (i = 0; i < 8; i++, e++) {              /* img 0x3f66 */
		pal[e * 3 + 0] = 0;
		pal[e * 3 + 1] = (u8)bl;
		pal[e * 3 + 2] = 0x3f;
		bl = (bl - 7) & 0xff;                   /* img 0x3f96 */
	}
	for (i = 1; i <= 16; i++) {                 /* img 0x3f30's +0x30 */
		s32 k;
		for (k = 0; k < 3; k++) {
			pal[(i + 0x10) * 3 + k] = pal[i * 3 + k];
			pal[(i + 0x20) * 3 + k] = pal[i * 3 + k];
			pal[(i + 0x30) * 3 + k] = pal[i * 3 + k];
		}
	}
	for (i = 0; i < 0x20; i++) {                /* img 0x3f9e */
		pal[(0xe0 + i) * 3 + 0] = (u8)i;
		pal[(0xe0 + i) * 3 + 1] = 0;
		pal[(0xe0 + i) * 3 + 2] = 0;
	}
}
