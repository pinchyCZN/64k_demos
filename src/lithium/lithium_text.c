/* LITHIUM - the font sheet and the credit roll.
 *
 * ORIGIN img 0x0c59 Font_Unpack, img 0x2d46 Text_DrawStringAt, img
 * 0x2d81 Text_DrawGlyphBlend. docs/lithium/effects/text.txt.
 *
 * THE GLYPH INDEX IS char - 0x41 AND NOTHING VALIDATES IT. The sheet
 * is alphabetical except that it has no Q and W sits in Q's vacant
 * slot, so the stored strings read as misspellings - text.txt 6 has
 * the table.
 *
 * ONE PEN, NO ARGUMENTS. The original passes the destination offset
 * through g_TextPenX and the string through BX, so the pen is a
 * global the draw advances; the port passes both and keeps the
 * global's role in one static.
 */

#include <string.h>

#include "lithium.h"

static u8 g_font[LITH_FONT_BYTES];
static s32 g_pen;                       /* g_TextPenX img 0x8f4b   */

/* ORIGIN img 0x0c59. Two passes over one buffer: an RLE expand, then
 * a dilate that puts the outline in.
 *
 * THE SOURCE LIES INSIDE THE DESTINATION in the original - the packed
 * bytes begin 0x1626 into the 0x1900-byte output and the write
 * pointer only reaches them once the read pointer has passed.
 * Reading from the image and writing to our own buffer is the same
 * result without the overlap. */
void Lith_FontUnpack(void)
{
	s32 di = 0, k;
	u32 src = LITH_FONT_RLE;

	while (di < LITH_FONT_BYTES) {
		s32 b = g_lith_img[src++];
		s32 n = (b & 0x7f) + 1;
		u8 v = (u8)((b & 0x80) ? 0x0f : 0);

		while (n-- && di < LITH_FONT_BYTES) g_font[di++] = v;
	}

	/* img 0x0c8f - the four-neighbourhood, one row in and one row
	 * short so it needs no bounds test. 0x1f is the next shade up
	 * and the blend SUBTRACTS it, text.txt 5.3. */
	for (k = 0; k < 0x1680; k++) {
		s32 i = LITH_FONT_W + k;

		if (g_font[i] == 0x0f) continue;
		if (g_font[i + 1] == 0x0f || g_font[i - 1] == 0x0f
		    || g_font[i + LITH_FONT_W] == 0x0f
		    || g_font[i - LITH_FONT_W] == 0x0f)
			g_font[i] = 0x1f;
	}
}

/* ORIGIN img 0x2d81 Text_DrawGlyphBlend.
 *
 * THE DESTINATION'S TOP TWO BITS ARE ITS PALETTE BAND and are put
 * back untouched; the arithmetic is on the 6-bit index inside it.
 *
 * 0x0f ADDS AND 0x1f SUBTRACTS - the ink lights what is under it and
 * the outline darkens it, which is the whole of the relief. Testing
 * the sheet byte for "is this lit" instead of running the blend
 * fills the outline too and the glyphs come out as solid blocks.
 *
 * ONE DESTINATION ROW PER SOURCE ROW. Text_DrawGlyph doubles; this
 * one does not, so a credit is 12 by 20 where a greeting is 12 by
 * 40. */
static void text_draw_glyph_blend(u8 *dst, s32 di, s32 glyph, s32 dl)
{
	s32 si = glyph * 12;
	s32 row, c;

	for (row = 0; row < 0x14; row++) {
		for (c = 0; c < 0xc; c++, si++, di++) {
			s32 al = g_font[si];
			s32 ah, band;

			if (!al) continue;
			ah = dst[di];
			band = ah & 0xc0;
			ah &= 0x3f;
			if (al > 0x0f) {
				al = (al - 0x10 - dl) & 0xff;
				ah -= al;
				if (ah < 0) ah = 0;
			} else {
				ah += (al - dl) & 0xff;
				if (ah > 0x3f) ah = 0x3f;
			}
			dst[di] = (u8)(ah + band);
		}
		si += 0x134;
		di += 0x134;
	}
}

/* ORIGIN img 0x2d46 Text_DrawStringAt.
 *
 * DL IS A SUBTRAHEND, NOT A BRIGHTNESS: the blend adds al - dl and
 * the ink is 0x0f, so 0 is full strength and 0xf is invisible - which
 * is why img 0x2d46 returns at once on 0xf.
 *
 * ANY BYTE BELOW 0x20 TERMINATES, so both 0 and the table's 0x1f
 * stop the draw. A space advances the pen and draws nothing.
 *
 * Ten pixels per character against glyphs twelve wide, so they
 * overlap by two. */
void Lith_TextDrawStringAt(u8 *dst, s32 pen, u32 str, s32 dl)
{
	if (dl == 0x0f) return;             /* img 0x2d46 */
	g_pen = pen;
	for (;;) {
		s32 ch = g_lith_img[str];

		if (ch < 0x20) return;
		if (ch != 0x20)
			text_draw_glyph_blend(dst, g_pen, ch - 0x41, dl);
		str++;
		g_pen += 0xa;
	}
}

/* g_CreditPtr img 0x8fde, g_CreditFade img 0x8fe0 and g_CreditTick
 * img 0x8fe2. THE SCENE NEVER SETS THE POINTER - only the fade, at
 * img 0x1a42 - so its first value is the one standing in the image.
 */
static u32 g_credit_ptr;
static s32 g_credit_fade, g_credit_tick;

/* ORIGIN img 0x2e1a Text_DrawGlyph - the greetings' blitter.
 *
 * IT DOUBLES VERTICALLY AND IT IS A STORE, NOT A BLEND. Each sheet
 * row is written to two destination rows, so a glyph is 12 by 40
 * where Text_DrawGlyphBlend's is 12 by 20, and the byte goes down
 * flat rather than being added into what is there.
 *
 * 0x0f IN THE SHEET IS THE INK and only 0x0f: img 0x2e2b swaps it
 * for the caller's level and copies every other value through, so
 * the outline the blend pass reads as 0x1f lands here as a literal
 * 0x1f. effects/text.txt section 3. */
static void text_draw_glyph(u8 *dst, s32 di, s32 glyph, s32 dl)
{
	s32 si = glyph * 12;
	s32 row, c;

	for (row = 0; row < 0x14; row++) {
		for (c = 0; c < 0xc; c++, si++, di++) {
			s32 al = g_font[si];

			if (al == 0x0f) al = dl;        /* img 0x2e2b */
			dst[di & 0xffff] = (u8)al;
			dst[(di + 0x140) & 0xffff] = (u8)al;
		}
		si += 0x134;
		di += 0x274;                    /* img 0x2e3f - two rows */
	}
}

/* ORIGIN img 0x2dd0 Text_DrawString - one item of the greetings.
 *
 * IT WALKS g_CreditPtr AND LEAVES IT PAST THE TERMINATOR, which is
 * how Tick_Greetings gets the next string without tracking a length:
 * img 0x2df7 advances the global before the terminator is tested, so
 * the pointer is already on the next entry when the draw returns.
 *
 * A SPACE IS DRAWN AS AN 'A' WITH ZERO INK, img 0x2e03 - the glyph
 * still runs, it just lays 0 instead of 0x7f, which is what makes a
 * space erase rather than skip. */
void Lith_TextDrawString(u8 *dst, s32 pen)
{
	g_pen = pen;
	for (;;) {
		s32 ch = g_lith_img[g_credit_ptr];
		s32 dl = 0x7f;

		g_credit_ptr++;                 /* img 0x2df7 */
		if (ch < 0x20) return;          /* img 0x2dfc */
		if (ch == 0x20) {               /* img 0x2e01 */
			dl = 0;
			ch = 0x41;
		}
		text_draw_glyph(dst, g_pen, ch - 0x41, dl);
		g_pen += 0xa;                   /* img 0x2e11 */
	}
}

/* OURS - Tick_Greetings needs the pen back to measure the item, and
 * the pointer to test the table's terminator. */
s32 Lith_TextPen(void) { return g_pen; }
u32 Lith_CreditPtr(void) { return g_credit_ptr; }
void Lith_CreditPtrSet(u32 p) { g_credit_ptr = p; }

/* OURS, standing in for the image's own initial value: the flyover
 * entry sets only the fade, so the pointer arrives already holding
 * the head of the table. */
void Lith_CreditsEnter(void)
{
	g_credit_ptr = LITH_CREDIT_TABLE;
	g_credit_fade = 0;                  /* img 0x1a42 */
	g_credit_tick = 0;
}

/* ORIGIN img 0x63fb - the tail of the flyover's tick handler.
 *
 * THE ROLL IS CUED BY THE MUSIC, not by a tick count: any change of
 * g_MusicPos advances a whole credit and restarts the fade at 0xf,
 * which then walks to 0 one step per eight ticks - 120 ticks, about
 * two seconds at 57 Hz.
 *
 * IT CONSUMES g_MusicPosLast AND Tick_Default TESTS THE SAME LATCH,
 * so this must run first and the beat strobe there does not fire
 * during this scene. subsystems/music.txt.
 *
 * THE LAST CREDIT STICKS. A record whose first byte is 0x1f is the
 * end of the table; the pointer backs up and the fade is forced to 0,
 * so the final pair stays at full strength for the rest of the
 * scene. */
void Lith_CreditsTick(void)
{
	g_credit_tick = (g_credit_tick + 1) & 0xff;
	if (!(g_credit_tick & 7) && g_credit_fade) g_credit_fade--;

	if (Lith_MusicPosLast() == Lith_MusicPos()) return;
	Lith_MusicPosLatch();
	g_credit_fade = 0xf;
	g_credit_ptr += 0x18;
	if (g_lith_img[g_credit_ptr] == 0x1f) {
		g_credit_ptr -= 0x18;
		g_credit_fade = 0;
	}
}

/* OURS - which credit is up, for the state line, and -1 before the
 * roll has a pointer at all. The scene never sets it, so it is zero
 * until Lith_CreditsEnter runs and an unguarded subtraction then
 * underflows into nonsense. */
s32 Lith_CreditIndex(void)
{
	if (g_credit_ptr < LITH_CREDIT_TABLE) return -1;
	return (s32)((g_credit_ptr - LITH_CREDIT_TABLE) / 0x18);
}

/* ORIGIN img 0x1b61 - four draws, two lines each, in the lower left.
 *
 * THE OUTGOING CREDIT IS DRAWN AT 0xf - fade, so the pair at -0x18
 * dims by exactly the step the incoming pair brightens and the two
 * cross-fade. Before the first advance that complement is 0xf and
 * the draw returns at once, which is also what keeps the record
 * ahead of the table from being read.
 *
 * img 0x1b66 and 0x1b78 are the two pen values and they are whole
 * buffer offsets, not columns: 0xbb90 is row 150 column 16 and
 * 0xd9a0 is row 174 column 32. */
void Lith_CreditsDraw(u8 *back)
{
	s32 f = g_credit_fade;

	Lith_TextDrawStringAt(back, 0xbb90, g_credit_ptr, f);
	Lith_TextDrawStringAt(back, 0xd9a0, g_credit_ptr + 0xc, f);
	Lith_TextDrawStringAt(back, 0xbb90, g_credit_ptr - 0x18, 0xf - f);
	Lith_TextDrawStringAt(back, 0xd9a0, g_credit_ptr - 0xc, 0xf - f);
}
