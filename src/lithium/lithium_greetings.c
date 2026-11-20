/* LITHIUM - the greetings, and the rays they throw.
 *
 * ORIGIN img 0x1396 maybe_Scene_Greetings and img 0x6972
 * Tick_Greetings. docs/lithium/effects/text.txt sections 3 and 4.
 *
 * THE RAYS ARE A FEEDBACK LOOP, NOT A LIGHT MODEL. Three buffers and
 * three passes a frame: the eight text items are added into an
 * accumulator, the accumulator is blurred onto the screen, and the
 * blur is then magnified back into the accumulator at three quarters
 * brightness. Each frame's image therefore contains a slightly
 * larger, slightly dimmer copy of the frame before, which is what
 * draws the streaks out of the letters. Nothing traces a ray.
 *
 * THE MAGNIFICATION IS WHAT POINTS THEM. img 0x14d4 walks the source
 * x from 0x1400 by 0x60 a column and takes it >> 7, so 320 output
 * columns sample source 40 to 232 - and the rows likewise 25 to 175.
 * Sampling a smaller window into a bigger one pushes everything away
 * from the centre, so the streaks radiate.
 *
 * AN ITEM IS SIXTEEN BYTES AND EIGHT OF THEM LIVE AT ONCE, spawned
 * one every 32 ticks. The record is not the trail's - this scene
 * reuses the same segment with its own stride and its own fields.
 * ./text.txt section 4.
 *
 * THE TEXT BUFFER IS NEVER CLEARED and that is deliberate on the
 * original's part: the runner zeroes four segments at img 0x1396 to
 * 0x13c2 and 0x5ad2 is not among them, so the cells still hold the
 * tower fall's terrain. It does not show, because Text_DrawGlyph
 * STORES rather than blends and the glyph boxes cover the whole
 * rectangle the scaler reads. Clearing it would be a difference
 * nobody could see; inventing the clear is still wrong.
 */

#include <string.h>

#include "lithium.h"

/* The eight items at 0x2ad2:0, img 0x6a07 stepping 0x10.
 *
 *     +0x00  word   the phase, 0 at spawn and +0x20 a tick
 *     +0x02  word   x, and +0x04 y - both drift
 *     +0x06  word   the x drift, and +0x08 the y drift
 *     +0x0a  word   where in the text buffer this item's cell is
 *     +0x0c  dword  the string's width, scaled by 0x800
 */
typedef struct {
	s32 phase;
	s32 x, y;
	s32 dx, dy;
	s32 cell;
	s32 width;
} LithGreet;

#define LITH_GREET_ITEMS 8

static LithGreet g_item[LITH_GREET_ITEMS];
static s32 g_cursor;                    /* g_TrailWriteCursor 0x8fb5 */
static s32 g_spawn_tick;                /* g_TextSpawnTick    0x8fb7 */

/* 0x4ad2 the accumulator, 0x7ad2 the blur. Both are addressed with a
 * 16-bit wrap because the blur reads a row either side of its own and
 * the original lets those run off the ends of the segment. */
static u8 g_accum[LITH_SEG_BYTES];
static u8 g_blur[LITH_SEG_BYTES];

/* OURS - a signed shift on the word a drift is kept in. */
static s32 gr_sar16(s32 v, s32 n)
{
	return (s32)((s16)(u16)v >> n);
}

/* ORIGIN img 0x1396 to 0x140a - the setup.
 *
 * THE CELLS ARE A TWO BY FOUR GRID, img 0x13af laying out the eight
 * +0x0a fields as 0, 0xa0, 0x3200, 0x32a0 and so on: two columns 160
 * apart and four rows 0x3200 - forty lines - apart, which is exactly
 * the height Text_DrawGlyph doubles a glyph to. The grid is never
 * seen; it is only where each string is kept so the scaler has
 * something to magnify.
 *
 * THE ITEM BANK IS NOT CLEARED, only the +0x0a fields written - so
 * every item starts with x at 0, which is the runner's test for an
 * unused slot at img 0x141a. */
void Lith_GreetingsEnter(void)
{
	s32 i, ofs = 0;

	memset(g_item, 0, sizeof g_item);       /* img 0x13a2 */
	for (i = 0; i < LITH_GREET_ITEMS; i += 2) {      /* img 0x13af */
		g_item[i].cell = ofs;
		ofs += 0xa0;
		g_item[i + 1].cell = ofs;
		ofs += 0x3160;
	}

	memset(g_blur, 0, sizeof g_blur);       /* img 0x1396 */
	memset(g_accum, 0, sizeof g_accum);     /* img 0x13c2 */

	g_spawn_tick = 0;                       /* img 0x13c8 */
	g_cursor = 0;                           /* img 0x13ce */
	Lith_CreditPtrSet(LITH_GREET_TABLE);    /* img 0x13db */

	/* img 0x13e9 - IT OPENS PAST FULL AND FALLS BELOW BLACK: 0x100
	 * down to -0x80, so the greetings arrive lit and the scene dims
	 * under them for its whole length. */
	Lith_PalFadeSet(0x100, -0x80, 8, 8);
	Lith_PalFlashSet(1);                    /* img 0x1403 */
	Lith_PalRefresh();                      /* img 0x140a */
}

/* ORIGIN img 0x6972 Tick_Greetings.
 *
 * ONE SPAWN EVERY 32 TICKS AND EIGHT SLOTS, so an item lives 256
 * ticks before its slot is taken again.
 *
 * THE POSITION IS AIMED AT THE CENTRE, img 0x6998: x starts at
 * 0x1400 less the drift times 0x80 and gains the drift every tick,
 * so after 128 ticks - half its life - it is exactly at 0x1400,
 * which the runner shifts down 5 into 160. Every greeting therefore
 * crosses the middle of the screen at its midlife whatever drift it
 * drew, and the drift only decides from where.
 *
 * THE WIDTH IS MEASURED BY DRAWING, img 0x69c8. Text_DrawString
 * leaves the pen past the last glyph, so the difference against the
 * start is the string's width and no separate pass is needed. */
void Lith_GreetingsTick(void)
{
	s32 i;

	g_spawn_tick = (g_spawn_tick + 1) & 0xff;       /* img 0x6977 */
	if (!(g_spawn_tick & 0x1f)) {
		LithGreet *it = &g_item[g_cursor];
		s32 pen;

		it->phase = 0;                  /* img 0x698b */
		it->dx = gr_sar16((s32)Lith_RndNext(), 9);
		it->x = (s32)(s16)(0x1400 - (it->dx << 7));
		it->dy = gr_sar16((s32)Lith_RndNext(), 9);
		it->y = (s32)(s16)(0xc80 - (it->dy << 7));

		Lith_TextDrawString(Lith_PicBuffer(), it->cell);
		pen = Lith_TextPen() - it->cell;        /* img 0x69c8 */
		it->width = (pen & 0xffff) << 0xb;      /* img 0x69d7 */

		/* img 0x69df - 0x1f ENDS THE TABLE and the wrap target is
		 * the FIRST NAME, not the head, so the opening line is
		 * shown once and the names then cycle for ever. */
		if (g_lith_img[Lith_CreditPtr()] == 0x1f)
			Lith_CreditPtrSet(LITH_GREET_NAMES);

		g_cursor++;                     /* img 0x69f1 */
		if (g_cursor >= LITH_GREET_ITEMS) g_cursor = 0;
	}

	for (i = 0; i < LITH_GREET_ITEMS; i++) {        /* img 0x6a0c */
		g_item[i].phase = (g_item[i].phase + 0x20) & 0xffff;
		g_item[i].x = (s32)(s16)(g_item[i].x + g_item[i].dx);
		g_item[i].y = (s32)(s16)(g_item[i].y + g_item[i].dy);
	}
	Lith_TickDefault();                     /* img 0x6a20 */
}

/* ORIGIN img 0x140d to 0x1473 - the eight items, added.
 *
 * THE ZOOM COMES OUT OF THE COSINE, img 0x1458 reading the table at
 * 0xa8b2 against a base of 0xa0b2, and it is the DENOMINATOR the
 * scaler divides by - so the item swells as the term falls. The
 * 0x1320 floor is what stops it dividing by nothing.
 *
 * THE SOURCE HEIGHT IS ONE CELL, img 0x1438 - 40 << 10 against the
 * title card's 200 << 10 - because a greeting is one line and the
 * grid packs four of them per column. */
static void greet_items(u8 *dst)
{
	const s32 *t = Lith_SinTable();
	s32 i;

	for (i = 0; i < LITH_GREET_ITEMS; i++) {
		LithGreet *it = &g_item[i];
		LithBlit b;

		if (!it->x) continue;           /* img 0x141a */
		b.centre_x = gr_sar16(it->x, 5);        /* img 0x141e */
		b.centre_y = gr_sar16(it->y, 5);
		b.src_w = it->width;            /* img 0x142f */
		b.src_h = 0xa000;               /* img 0x1438 */
		b.src_ofs = it->cell;           /* img 0x1442 */
		b.phase = it->phase;            /* img 0x1449 */
		b.zoom = (t ? t[((it->phase & 0x1ffc) >> 2) + LITH_SIN_QUARTER]
		            : 0) + 0x1320;      /* img 0x1458 */
		Lith_ImageBlitScaled(dst, Lith_PicBuffer(), &b);
	}
}

/* ORIGIN img 0x1475 to 0x14ab - the blur, and it writes TWICE.
 *
 * FOUR NEIGHBOURS AND NO CENTRE. The pixel itself is not in the
 * average - left, right, above and below are summed and shifted down
 * two - so the pass is a blur that also fades, and that fade is half
 * of what keeps the feedback from saturating.
 *
 * THE RESULT GOES TO BOTH the intermediate and the screen, img
 * 0x14a4 and 0x14a7, so what is presented is the blurred
 * accumulation and never the sharp text. */
static void greet_blur(u8 *screen)
{
	s32 i;

	for (i = 0; i < PD_SCREEN_W * PD_SCREEN_H; i++) {
		s32 v = g_accum[(i - 1) & 0xffff]
		        + g_accum[(i + 1) & 0xffff]
		        + g_accum[(i - 0x140) & 0xffff]
		        + g_accum[(i + 0x140) & 0xffff];

		v >>= 2;                        /* img 0x14a1 */
		g_blur[i] = (u8)v;              /* img 0x14a4 */
		screen[i] = (u8)v;              /* img 0x14a7 */
	}
}

/* ORIGIN img 0x14ad to 0x14f4 - the magnify, back into the
 * accumulator at three quarters.
 *
 * THE STEP IS 0x60 IN A 0x80 UNIT, which is five sixths - so the
 * window shrinks by a sixth a frame and the image grows by the same,
 * and a streak takes about a second to cross the screen.
 *
 * THE DECAY IS al - (al >> 2), img 0x14df - three quarters exactly,
 * done with a shift and a subtract rather than a multiply. */
static void greet_magnify(void)
{
	s32 row, col, v = 0xc80;

	for (row = 0; row < PD_SCREEN_H; row++) {
		s32 src_row = (v >> 7) * PD_SCREEN_W;   /* img 0x14c1 */
		s32 u = 0x1400;

		for (col = 0; col < PD_SCREEN_W; col++) {
			s32 al = g_blur[(src_row + (u >> 7)) & 0xffff];

			al -= al >> 2;                  /* img 0x14df */
			g_accum[row * PD_SCREEN_W + col] = (u8)al;
			u += 0x60;                      /* img 0x14e7 */
		}
		v += 0x60;                              /* img 0x14ef */
	}
}

/* ORIGIN img 0x140d to 0x14f4 - the frame, in the order the three
 * buffers have to be touched. */
void Lith_GreetingsFrame(u8 *fb)
{
	greet_items(g_accum);
	greet_blur(fb);
	greet_magnify();
}

/* ORIGIN img 0x14f9 - the music, and nothing else. */
s32 Lith_GreetingsDone(void)
{
	return Lith_MusicPos() >= 0x39;
}

/* ORIGIN img 0x1503 - one instruction after the loop, and THE FINALE
 * IS BLACK WITHOUT IT: the common tail yanks the fade up to
 * g_PalFlash instead of easing it, and the DAC is only re-uploaded
 * while the fade moves. effects/figure.txt section 6. */
void Lith_GreetingsLeave(void)
{
	Lith_PalFlashSet(0);
}
