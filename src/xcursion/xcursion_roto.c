/* XCURSION - the rotozoomer, and the banner that rides in its
 * texture. effects/roto.txt.
 *
 * THE LETTERS ARE NOT DRAWN OVER THE EFFECT. Text_BuildBanner blends
 * them INTO the 256x256 field before the part starts, so the
 * rotozoom turns them along with everything else - which is why the
 * word rotates rather than sitting on top.
 */

#include "xcursion.h"

/* ORIGIN img 0x33a0 - Text_BuildBanner. The 32x32 font blended into
 * a field, not blitted onto a screen.
 *
 * img 0x33ec HALVES WHAT IS ALREADY THERE and adds the brightness, so
 * a glyph shows as a lightened copy of the field beneath it and
 * carries the texture with it.
 *
 * It takes a record of the same shape the caption masks use - a text
 * pointer, a destination offset and a row count - through a pointer
 * at img 0x98bb. Eight glyphs to a line, 32 pixels apart, and the
 * field is 256 wide so a row strides 0x100. */
void Xcur_TextBuildBanner(u8 *dest, u32 rec, s32 bright)
{
	s32 str, di, lines, line, ch;

	if (!g_img) return;
	str = (s32)RD_U16(g_img + rec);
	di = (s32)RD_U16(g_img + rec + 2);
	lines = (s32)RD_U16(g_img + rec + 4) >> 5;

	for (line = 0; line < lines; line++) {
		for (ch = 0; ch < 8; ch++) {        /* img 0x33b6, BP = 8 */
			s32 c = g_img[str++], glyph, row, keep;

			if (c == 'a') { c = ' '; di += 0x10; }   /* img 0x33c1 */
			else if (c == 'b') { c = ' '; di -= 0x10; }
			glyph = (s32)XCUR_FONT32 + ((c - 0x20) << 7);
			keep = di;
			for (row = 0; row < 0x20; row++) {       /* img 0x33dc */
				s32 byte;
				for (byte = 0; byte < 4; byte++) {   /* img 0x33df */
					s32 bits = g_img[glyph++], bit;
					for (bit = 0; bit < 8; bit++) {
						if (bits & 1) {
							s32 p = di & 0xffff;
							dest[p] = (u8)((dest[p] >> 1) + bright);
						}
						bits >>= 1;
						di++;
					}
				}
				di = (di + 0xe0) & 0xffff;  /* img 0x3404, 0x100 a row */
			}
			di = (keep + 0x20) & 0xffff;    /* img 0x340d */
		}
		di = (di + 0x1f00) & 0xffff;        /* img 0x3413 */
	}
}

/* ORIGIN img 0x2f28 - Roto_Render.
 *
 * IT DRAWS A QUARTER OF THE PIXELS EACH FRAME. The walk is 160 by
 * 100 stepping two across and two down, and img 0x2fa0 offsets the
 * start by a pixel in x, a row in y, or both, from the low two bits
 * of its own frame counter. Four frames cover the screen, so the
 * image is always a frame or two behind itself at the edges and that
 * smear is the effect's look, not an artifact.
 *
 * AND IT BLENDS RATHER THAN WRITES: img 0x2fd5 takes the texel minus
 * what is already on screen, shifts that right by g_RotoBlend and
 * adds it back, so each pass moves a quarter of the way toward the
 * new texture. Writing the texel straight gives a hard, flickering
 * rotozoom instead.
 *
 * The step is the sine and cosine of the angle scaled by the zoom,
 * and the start is the texture coordinate at screen (-160, -100)
 * carried through the same rotation - assembled at img 0x2f62 out of
 * shifts and adds rather than a multiply.
 *
 * TWO DESTINATIONS, ONE READ BACK: `accum` is the feedback and `fb`
 * only receives a copy. effects/roto.txt section 5.
 */
void Xcur_RotoRender(u8 *fb, u8 *accum, const u8 *texture, s32 zoom,
                     s32 angle, s32 phase, s32 shift)
{
	s32 bx = angle & 0x7fc;
	s32 du = (Xcur_Sin(bx) * zoom) >> 12;   /* img 0x2f4c */
	s32 dv = (Xcur_Cos(bx) * zoom) >> 12;   /* img 0x2f5a */
	s32 u, v, di = 0, row, col;

	/* img 0x2f62 - (-160 * du + 100 * dv, -160 * dv - 100 * du) */
	u = (s32)(s16)((-160 * du + 100 * dv) & 0xffff);
	v = (s32)(s16)((-160 * dv - 100 * du) & 0xffff);

	if (phase & 1) { di += 1;             u += du; v += dv; }
	if (phase & 2) { di += PD_SCREEN_W;   u -= dv; v += du; }

	du += du;                               /* img 0x2fc4, two at a time */
	dv += dv;

	for (row = 0; row < 100; row++) {       /* img 0x2fbd, 0x64 rows */
		s32 ru = u, rv = v, rdi = di;

		for (col = 0; col < 160; col++) {   /* img 0x2fcd, 0xa0 columns */
			s32 t = texture[(((rv >> 8) & 0xff) << 8)
			                | ((ru >> 8) & 0xff)];
			s32 old = accum[rdi];               /* img 0x2fd8, DS */
			/* signed byte difference, then a signed shift */
			s32 d = (s32)(s8)((t - old) & 0xff);
			u8 nv = (u8)(((d >> shift) + old) & 0xff);

			accum[rdi] = nv;                    /* img 0x2fe0 */
			fb[rdi] = nv;                       /* img 0x2fe2, FS */
			ru += du;
			rv += dv;
			rdi += 2;
		}
		u -= dv;                            /* img 0x2ff6 */
		v += du;
		di += 2 * PD_SCREEN_W;              /* two rows at a time */
	}
}
