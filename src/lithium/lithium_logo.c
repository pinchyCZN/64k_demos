/* LITHIUM - the title, unpacked, blurred and blitted scaled.
 *
 * ORIGIN img 0x2d10 Image_UnpackRle and img 0x16d5 Image_BlitScaled.
 * docs/lithium/effects/rockintro.txt section 5.
 */

#include <string.h>

#include "lithium.h"

/* ORIGIN img 0x2d10 Image_UnpackRle.
 *
 * EACH SOURCE BYTE IS ONE RUN: bit 7 is the value, 0 or 0x7f, and
 * bits 0 to 6 are the length less one, so runs of 1 to 128. The
 * bound is 64,000, which is 320 x 200 exactly. */
void Lith_ImageUnpackRle(u8 *dst, u32 src)
{
	u32 di = 0;

	if (!g_lith_img) return;
	memset(dst, 0, LITH_TEX_BYTES);
	while (di < 64000) {
		s32 cl = g_lith_img[src++];
		u8 v = (u8)((cl & 0x80) ? 0x7f : 0);
		s32 n = (cl & 0x7f) + 1;
		while (n-- > 0 && di < LITH_TEX_BYTES) dst[di++] = v;
	}
}

/* ORIGIN img 0x4b15 Image_Blur. The mean of four neighbours, in
 * place, like Tex_Blur - BUT ITS STRIDE IS 320, NOT 256. The two sit
 * next to each other at img 0x4aee and 0x4b15 and differ only in
 * their vertical offsets, +-0x140 against +-0x100. Blurring a
 * 320-wide picture with the texture one smears it along a diagonal
 * and mangles every letterform. */
void Lith_ImageBlur(u8 *buf, s32 times)
{
	s32 n;

	for (n = 0; n < times; n++) {
		u32 di = 0;
		do {
			s32 v = buf[(u16)(di - 1)] + buf[(u16)(di + 1)]
			      + buf[(u16)(di + 0x140)] + buf[(u16)(di - 0x140)];
			buf[di] = (u8)(v >> 2);
			di = (u16)(di + 1);
		} while (di);
	}
}

/* ORIGIN img 0x185a Screen_CrossFade. A TEMPORAL blend, and the
 * reason the demo looks smeared rather than sharp:
 *
 *     blend[i] = blend[i] + ((back[i] - blend[i]) >> 2)
 *
 * so each frame moves a QUARTER of the way toward the one just
 * rendered, and that - not the back buffer - is what reaches the
 * screen. The accumulator is the segment the runner clears on entry.
 *
 * THE SHIFT IS SIGNED, so the approach is not symmetric and a value
 * can stall just short of its target climbing -
 * effects/rockintro.txt section 2.
 *
 * The original walks from both ends at once - DI down from 0xf9ff
 * and SI up from 0 - which is one loop for two pixels and nothing
 * more; a single sweep gives the same buffer. */
void Lith_ScreenCrossFade(u8 *blend, const u8 *back)
{
	s32 i;

	for (i = 0; i < PD_SCREEN_W * PD_SCREEN_H; i++) {
		s32 cur = (s8)blend[i];
		s32 nw = (s8)back[i];

		blend[i] = (u8)(cur + ((s32)(s8)(nw - cur) >> 2));
	}
}

/* ORIGIN img 0x16d5 Image_BlitScaled.
 *
 * A DIVIDE PER AXIS AND A CARRIED REMAINDER, not fixed point: the
 * quotient is the whole step and the remainder seeds the accumulator,
 * so the scale is a true ratio rather than a rounded one.
 *
 * THE TWO AXES SHIFT DIFFERENTLY AND THAT IS THE POINT. u comes out
 * at >> 0xb and v at >> 0xa, so with g_BlitSrcW at 320 << 10 the
 * source columns run 0 to 160 - HALF the picture - while the rows run
 * all 200. One 320-wide image therefore holds both words, and
 * src_ofs picks which half: 0 for "Vista", 160 for "Lithium".
 *
 * IT IS ADDITIVE AND IT SATURATES. img 0x1839 adds into the
 * destination and img 0x183e pins the result at 0x7f, which is why
 * the word shows THROUGH the chrome instead of over it.
 *
 * [open] THE DIRTY BOX IS NOT KEPT, as in Poly_Fill. */
void Lith_ImageBlitScaled(u8 *dst, const u8 *src, const LithBlit *b)
{
	s32 width, height, x0, y0, x1, y1, w, h, row, col;
	s32 ua, va, fade;
	const s32 *t = Lith_SinTable();

	if (b->zoom < 1) return;

	/* The x axis. The remainder seeds the accumulator unhalved. */
	width = (s32)((u32)b->src_w / (u32)b->zoom);
	ua = (s32)((u32)b->src_w - (u32)width * (u32)b->zoom);
	x0 = b->centre_x - (width >> 1);
	x1 = x0 + width;
	if (x0 < 0) {
		ua += -x0 * b->zoom;    /* carry the clipped columns */
		x0 = 0;
	}
	if (x1 > PD_SCREEN_W - 1) x1 = PD_SCREEN_W - 1;
	w = x1 - x0;
	if (w <= 0) return;

	/* The y axis, and ITS remainder IS halved - img 0x177f. */
	height = (s32)((u32)b->src_h / (u32)b->zoom);
	va = (s32)((u32)b->src_h - (u32)height * (u32)b->zoom) >> 1;
	y0 = b->centre_y - (height >> 1);
	y1 = y0 + height;
	if (y0 < 0) {
		va += -y0 * b->zoom;
		y0 = 0;
	}
	if (y1 > PD_SCREEN_H - 1) y1 = PD_SCREEN_H - 1;
	h = y1 - y0;
	if (h <= 0) return;

	/* img 0x17e2 reads 0xa8b2 against a base of 0xa0b2, so THIS IS
	 * THE COSINE - effects/rockintro.txt 5. */
	fade = 0x7f;
	if (t) {
		s32 bx = ((b->phase & 0x1ffc) >> 2) + LITH_SIN_QUARTER;

		fade = (((t[bx] - 0x1000) >> 7) + 0x7f);
		if (fade < 0) fade = 0;
	}

	for (row = 0; row < h; row++) {
		const u8 *sp = src + b->src_ofs + (va >> 0xa) * PD_SCREEN_W;
		u8 *dp = dst + (y0 + row) * PD_SCREEN_W + x0;
		s32 u = ua;

		for (col = 0; col < w; col++) {
			s32 c = sp[u >> 0xb];

			if (c) {
				c -= fade;
				if (c >= 0) {   /* JC skips a byte darker than the fade */
					s32 d = dp[col] + c;
					dp[col] = (u8)((d & 0x80) ? 0x7f : d);
				}
			}
			u += b->zoom;
		}
		va += b->zoom;
	}
}
