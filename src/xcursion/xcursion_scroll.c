/* XCURSION - the end credits. effects/scroller.txt.
 *
 * THE FONT IS A STENCIL AND NOTHING ELSE. It decides which pixels
 * survive; a separate source image decides what they look like. So
 * the credits are textured, and the blitter never draws a glyph.
 */

#include <string.h>

#include "xcursion.h"

/* ORIGIN img 0x301f - Scroller_BuildRows. 128 records, one per screen
 * row from 72 down, out of a single divide each.
 *
 * THE STEP IS THE RECIPROCAL OF THE WIDTH, img 0x3063: the width is
 * 0x10000 / step, so step * width is 0x10000 exactly and every row
 * spans precisely one 32-character line however wide perspective
 * makes it. That is the whole trick - there is no per-row text
 * fitting anywhere.
 */
void Xcur_ScrollerBuildRows(XcurScrollRow *rows)
{
	s32 bp;

	for (bp = 0x80; bp > 0; bp--) {             /* img 0x303b */
		XcurScrollRow *r = &rows[0x80 - bp];
		s32 dist = 0x800000L / (0xc9 - bp);     /* img 0x3041 */
		s32 step = (dist >> 8) & 0xffff;        /* img 0x3059 */
		s32 wide = step ? ((0x10000L / step) & 0xffff) : 0;
		s32 start = 0;

		if (wide > 0x140)                       /* img 0x3075 */
			start = (step * ((wide - 0x140) >> 1)) & 0xffff;
		r->width = wide > 0x140 ? 0x140 : wide; /* img 0x308d */
		r->xoff = (0xa0 - (r->width >> 1)) & 0xffff;
		/* img 0x309d - the texture is centred on 0x8000 by backing
		 * off half the UNCLAMPED width's worth of steps. */
		r->start = (start + 0x8000 - step * (wide >> 1)) & 0xffff;
		r->dist = dist;
		r->step = step;
	}
}

/* OURS - A STAND-IN 8x8 FONT, and the letterforms are not the
 * demo's. img 0x301f asks the video BIOS for the ROM 8x8 set with
 * INT 10h AX=1130 BH=3, so THE DEMO CONTAINS NO FONT FOR THIS and
 * none can be transcribed out of the image. These glyphs exist only
 * so the perspective, the row table and the stencil can be seen
 * working - the credits' text is mixed case and the ROM font has
 * real lowercase, so this carries both.
 *
 * Bit 7 is the leftmost pixel, matching img 0x314f's (~column) & 7.
 * Authoring the other way round mirrors every glyph while leaving
 * the lines correct, which is exactly what that looks like.
 */
static const char *const g_glyph[] = {
"A183c66667e666600","B7c66667c66667c00","C3c66060606663c00",
"D786c6666666c7800","E7e06063e06067e00","F7e06063e06060600",
"G3c66063666663c00","H6666667e66666600","I3c18181818183c00",
"J7830303030331e00","K66361e1e3666e600","L0606060606067e00",
"Mc6eefed6c6c6c600","N66666e7e76666600","O3c66666666663c00",
"P7c66667c06060600","Q3c666666663c7000","R7c66667c3666e600",
"S3c66063c60663c00","T7e18181818181800","U6666666666663c00",
"V66666666663c1800","Wc6c6c6d6fed6c600","Xc6663c183c66c600",
"Y66666666183c1800","Z7e60300c06067e00","03c6666766e663c00",
"1181c181818181800","23c66060c30607e00","33c66603860663c00",
"43030367e30303000","57e06067e60663c00","63c66063e66663c00",
"77e6030180c0c0c00","83c66663c66663c00","93c66667c60663c00",
" 0000000000000000",".0000000000181800",",000000000018180c",
"!1818181818001800","?3c66603018001800",":0000181800181800",
";000018180018180c","-000000003c000000","(30180c0c0c183000",
")0c18303030180c00","'1818180000000000",
"a00003c607c663c00","b0606063e66663e00","c00003c0606063c00",
"d6060607e66667e00","e00003c667e063c00","f3818187e18181800",
"g00007c66667c603c","h0606063e66666600","i1800181818181800",
"j3000303030301e00","k060666361e366600","l1818181818181c00",
"m0000ecfed6c6c600","n00003e6666666600","o00003c6666663c00",
"p00003e66663e0606","q00007c66667c6060","r00003e6606060600",
"s00003c063c603c00","t18187e1818181800","u0000666666663c00",
"v00006666663c1800","w0000c6c6d6fe6c00","x000066663c666600",
"y00006666667c603c","z00007e301c067e00"
};

/* OURS, A STAND-IN for the 8x8 ROM font. [open] scroller.txt. */
void Xcur_ScrollerFont(u8 *font)
{
	static const char hx[] = "0123456789abcdef";
	s32 g, k, b;

	memset(font, 0, 0x800);
	for (g = 0; g < (s32)(sizeof g_glyph / sizeof g_glyph[0]); g++) {
		const char *p = g_glyph[g];
		s32 c = (s32)(u8)p[0];

		for (k = 0; k < 8; k++) {
			s32 v = 0, n;

			for (n = 0; n < 2; n++) {
				const char *q = hx;
				s32 d = 0;

				while (*q && *q != p[1 + k * 2 + n]) { q++; d++; }
				v = (v << 4) | (d & 0xf);
			}
			/* reverse the byte: the table reads LSB-left as hex and
			 * the blitter wants MSB-left. */
			b = 0;
			for (n = 0; n < 8; n++)
				if (v & (1 << n)) b |= 0x80 >> n;
			font[((c << 3) + k) & 0x7ff] = (u8)b;
		}
	}
}

/* ORIGIN img 0x30c1 - Scroller_Render. 128 rows from screen row 72,
 * which is the 0x5a00 at img 0x30db.
 *
 * THE CHARACTER CACHE AND THE BIT TEST SHARE ONE REGISTER: DH holds
 * the last character index and DL the font row, so img 0x312e decides
 * whether to re-read the font and img 0x3156 tests DL's bits. img
 * 0x3127 primes DH to 0xff, which a column >> 3 can never equal, so
 * the first pixel of every row always loads.
 *
 * `font_ofs` is img 0x302b's +7: it points at a glyph's LAST row and
 * the blitter walks UPWARD. Get that sign wrong and the text is
 * mirrored vertically.
 */
void Xcur_ScrollerRender(u8 *work, const u8 *source, const u8 *font,
                         const XcurScrollRow *rows, s32 scroll_pos)
{
	s32 si_base = 0x5a00, i;                    /* img 0x30db */

	for (i = 0; i < 0x80; i++) {                /* img 0x30d8 */
		const XcurScrollRow *r = &rows[i];
		s32 cx = r->width;

		if (cx) {                               /* img 0x30e5 - ZERO SKIPS */
			s32 si = si_base + r->xoff;         /* img 0x30eb */
			s32 ax = r->start;
			s32 step = r->step;
			s32 t = ((u32)(r->dist - scroll_pos)) >> 9;   /* img 0x30fb */
			s32 sub = t & 7;                    /* img 0x3108 */
			s32 bx = (s32)(s16)(t & 0xffff) >> 3;
			s32 base, dh = 0xff, dl = 0;        /* img 0x3127 */
			s32 n;

			if (bx > 0) bx = 0;                 /* img 0x310f */
			if (bx < -0x53) bx = -0x53;         /* img 0x3117 */
			base = (bx << 5) & 0xffff;          /* img 0x311f - 32 CHARS */

			for (n = 0; n < cx; n++) {
				s32 col = (ax >> 8) & 0xff;
				s32 bl = col >> 3;              /* img 0x312b */

				if (dh != bl) {                 /* img 0x312e */
					s32 ch;

					dh = bl;
					/* img 0x313b is a SIXTEEN-BIT address, so a
					 * negative line wraps INTO the text. */
					ch = (s32)g_img[(XCUR_SCROLL_TEXT
					                 + ((base + bl) & 0xffff)) & 0xffff];
					dl = (s32)font[((ch << 3) + 7 - sub) & 0x7ff];
				}
				if (dl & (1 << ((~col) & 7)))   /* img 0x314f */
					work[si] = source[si];      /* img 0x315b */
				else
					work[si] = 0;               /* img 0x3162 */
				ax = (ax + step) & 0xffff;
				si++;
			}
		}
		si_base += PD_SCREEN_W;                 /* img 0x3170 */
	}
}

/* ORIGIN img 0x520c - THE SOURCE THE STENCIL SHOWS THROUGH, built by
 * the part rather than by the renderer, 320 by 128 into 0x6a79.
 *
 * IT READS THE SCROLLER'S OWN ROW TABLE, not the sine field. img
 * 0x3038 puts that table at 0x8a79:0x4564 - over the top of the
 * field - and img 0x521b starts BX at exactly 0x4564, stepping it by
 * the 0xc record stride. The word at record+1 is bytes 1 and 2 of
 * the record's `dist` dword, which is (dist >> 8): the row's own
 * perspective STEP.
 *
 * So the shear IS distance. Far rows subtract more and come out
 * dark, near rows subtract less and come out bright, with the plasma
 * modulating both - the wash the credits are stencilled out of.
 * effects/scroller.txt section 7.1.
 */
void Xcur_ScrollerSource(u8 *dest, const u8 *plasma,
                         const XcurScrollRow *rows)
{
	s32 si = 0x4800, di = 0x5a00, dx;

	for (dx = 0; dx < 0x80; dx++) {             /* img 0x5224 */
		s32 cx;
		s32 bp = (s32)(s16)(rows[dx].step & 0xffff);   /* img 0x522a */
		s32 a;

		bp >>= 1;                               /* img 0x522e */
		a = bp >> 2;
		bp -= a;                                /* three quarters */
		for (cx = 0x140; cx > 0; cx--) {        /* img 0x5227 */
			s32 v = plasma[si & 0xffff] >> 1;   /* img 0x5237 */

			v = (v + 0xa0 - bp) & 0xffff;       /* img 0x523d */
			v = (s32)(s16)v;
			if (v < 0) v = 0;                   /* img 0x5242 */
			if (v > 0xbf) v = 0xbf;             /* img 0x5249 */
			dest[di & 0xffff] = (u8)v;          /* img 0x5251 */
			di++;
			si++;
		}
		si -= 0x40;                             /* img 0x525b */
	}
}
