/* LITHIUM - the polygon fill.
 *
 * ORIGIN img 0x38a3 Poly_Fill and img 0x3d64 Poly_ClearSpans.
 * docs/lithium/subsystems/raster.txt section 4.
 *
 * NOTHING HERE DRAWS TO VIDEO MEMORY. The original fills segment
 * 0x4ad2 and only Screen_Present copies that to 0xa000, so this takes
 * whatever buffer the caller hands it.
 */

#include <string.h>

#include "lithium.h"

/* A span record is 32 bytes in the original and only 20 are used;
 * the stride is an artifact of its addressing, so the fields are a
 * struct here. The original's offsets are kept in the names.
 *
 * base[] is Terrain_Build's stash: it passes a vertex-pool offset
 * from its vertex pass to its face pass through two words of the u
 * interpolant, which that call never writes - effects/terrain.txt
 * section 1.1. Named fields here rather than an alias. */
typedef struct {
	s32 x0, x1;         /* +0x00 min, +0x02 max - WORDS there     */
	s32 u0, u1;         /* +0x04, +0x08 - 16.16 at each edge      */
	s32 v0, v1;         /* +0x0c, +0x10                           */
	s32 base[2];
} LithSpan;

static LithSpan g_span[LITH_SPAN_ROWS];

/* g_SpanRowMin img 0x8edc and g_SpanRowMax img 0x8ede. THE IMAGE
 * CARRIES THE WHOLE TABLE, so the first clear covers every row;
 * starting at 0/0 leaves untouched rows filling from the left edge.
 * subsystems/raster.txt section 4.2. */
static s32 g_row_min = 0, g_row_max = LITH_SPAN_ROWS - 1;

/* ORIGIN img 0x3d64 Poly_ClearSpans. It resets only the rows the last
 * polygon touched, which is why the row bounds are kept.
 *
 * THE INITIAL VALUES ARE A DWORD, 0xffff0140: min x = 320, off the
 * right edge, and max x = -1, off the left. Every later compare then
 * works without a "has this row been touched" flag. */
static void poly_clear_spans(void)
{
	s32 y;

	for (y = g_row_min; y <= g_row_max; y++) {
		if (y < 0 || y >= LITH_SPAN_ROWS) continue;
		g_span[y].x0 = PD_SCREEN_W;
		g_span[y].x1 = -1;
	}
	g_row_min = LITH_SPAN_ROWS - 1;
	g_row_max = 0;
}

/* ORIGIN img 0x3952 and img 0x3a57 - the edge walk.
 *
 * THE ORIGINAL RUNS THIS TWICE, once carrying u and once v, into two
 * pairs of fields. One walk carrying both is the same result: the x
 * min and max it computes are identical on the second pass, so every
 * store lands on the same edge either way. Carrying the pair is ours;
 * the arithmetic and the branch structure are not.
 *
 * WHICH EDGE WINS IS DECIDED BY THE X COMPARE, and the interpolants
 * ride along with it - that is how u0 ends up belonging to the left
 * edge without either edge knowing which one it is.
 *
 * THE ROW RANGE IS TRACKED PER EDGE, not per row, the way img
 * 0x39a1 does it from the endpoints. Tracking it inside the row
 * loop loses any row that only ever updates its MINIMUM, because
 * that path skips the max store - and a lost top row tears. */
static void poly_edge(s32 x0, s32 y0, s32 u0, s32 v0,
                      s32 x1, s32 y1, s32 u1, s32 v1)
{
	s32 dy, xs, us, vs, ex, eu, ev, y;

	if (y0 > y1) {                      /* swap the ends */
		s32 t;
		t = x0; x0 = x1; x1 = t;
		t = y0; y0 = y1; y1 = t;
		t = u0; u0 = u1; u1 = t;
		t = v0; v0 = v1; v1 = t;
	}
	if (y0 >= LITH_SPAN_ROWS || y1 <= 0) return;
	dy = y1 - y0;
	if (!dy) return;

	/* The steps. x is 12-bit fixed, the interpolants 16.16, and each
	 * start carries HALF A STEP - img 0x3985 and 0x3999. */
	xs = lith_idiv((x1 - x0) << 0xc, dy);
	us = lith_idiv((u1 - u0) << 0x10, dy);
	vs = lith_idiv((v1 - v0) << 0x10, dy);
	ex = (x0 << 0xc) + (xs >> 1);
	eu = (u0 << 0x10) + (us >> 1);
	ev = (v0 << 0x10) + (vs >> 1);

	{
		s32 lo = y0 < 0 ? 0 : y0;
		s32 hi = y1 - 1;
		if (hi > LITH_SPAN_ROWS - 1) hi = LITH_SPAN_ROWS - 1;
		if (lo < g_row_min) g_row_min = lo;
		if (hi > g_row_max) g_row_max = hi;
	}

	for (y = y0; y < y1; y++) {
		s32 x;

		if (y >= LITH_SPAN_ROWS) break;
		if (y >= 0) {
			/* img 0x39bc - the x carries a -0.5 bias before the
			 * shift, which is what centres the edge on the pixel. */
			x = (ex - 0x800) >> 0xc;
			if (g_span[y].x0 >= x) {
				g_span[y].x0 = x;
				g_span[y].u0 = eu;
				g_span[y].v0 = ev;
				if (g_span[y].x1 > x) goto next;
			}
			g_span[y].x1 = x;
			g_span[y].u1 = eu;
			g_span[y].v1 = ev;
		}
	next:
		ex += xs;
		eu += us;
		ev += vs;
	}
}

/* ORIGIN img 0x3bd8 Poly_SpanTextured, and the band variants at img
 * 0x3c70 and 0x3c96 which are the same loop plus a constant.
 *
 * THE TEXTURE IS 256x256 AND FILLS A WHOLE SEGMENT, so u and v are
 * bytes and wrap for free - there is no mask anywhere in the loop.
 *
 * THE BAND IS HOW ONE MAP BECOMES MANY COLOURS: adding 0x40 or 0xc0
 * moves the texel into a different 64-entry stretch of the palette,
 * which is why the palette is four rising ramps. */
static void poly_span(u8 *dst, const u8 *tex, s32 n,
                      s32 u, s32 du, s32 v, s32 dv, s32 band)
{
	while (n-- > 0) {
		s32 ui = (u >> 16) & 0xff;
		s32 vi = (v >> 16) & 0xff;

		*dst++ = (u8)(tex[(vi << 8) | ui] + band);
		u += du;
		v += dv;
	}
}

/* g_SpanRoutine img 0x8f6a is a POINTER the scenes store, so one fill
 * serves five per-pixel loops. */
typedef void (*LithSpanFn)(u8 *, const u8 *, s32, s32, s32, s32, s32, s32);

/* ORIGIN img 0x3c46 Poly_SpanScreen - THE FIFTH SPAN ROUTINE, and
 * the only one whose texture is a SCREEN rather than a map, so its
 * stride is 320 and nothing masks u or v. The shatter samples the
 * picture that was on screen when the surface broke, which is why
 * its shards carry the image. subsystems/raster.txt section 4.1. */
static void poly_span_screen(u8 *dst, const u8 *tex, s32 n,
                             s32 u, s32 du, s32 v, s32 dv, s32 band)
{
	(void)band;
	while (n-- > 0) {
		s32 ui, vi;

		/* THE STEP COMES FIRST here and nowhere else - img 0x3c46
		 * is the loop top, the fetch is at img 0x3c65. */
		u += du;
		v += dv;
		ui = (s32)((u32)u >> 16);
		vi = (s32)((u32)v >> 16);
		*dst++ = tex[(u16)(ui + vi * PD_SCREEN_W)];
	}
}

/* ORIGIN img 0x3c18 Poly_SpanPlanar - THE CRAFT'S SHADOW, and the one
 * span routine that SUBTRACTS. subsystems/raster.txt section 3.3.
 *
 * THE INDEX IS A PLAIN 128 BY 128 LOOKUP. The original forms it in
 * one 16-bit register - (v >> 9) and 0xff80, then u's integer part
 * added into BL alone - which for the u and v the caller has already
 * clamped to 0..0x7f is exactly v * 128 + u.
 *
 * THE CLAMP KEEPS THE PIXEL IN ITS BAND, it does not floor at zero:
 * img 0x3c37 is a JS, so a result whose high bit SURVIVES is kept and
 * anything else becomes 0x81. Terrain sits in 0x81..0xbf, so a
 * subtraction that would drop it out of the top band is pinned to the
 * bottom of that band instead of wrapping into another colour.
 *
 * A ZERO STENCIL BYTE THEREFORE CHANGES NOTHING, which is what lets
 * the pass run over every terrain face and only darken the
 * silhouette. */
static void poly_span_planar(u8 *dst, const u8 *sten, s32 n,
                             s32 u, s32 du, s32 v, s32 dv, s32 band)
{
	(void)band;
	while (n-- > 0) {
		s32 bx = (v >> 9) & 0xff80;
		s32 d;

		bx = (bx & 0xff00) | (((bx & 0xff) + ((u >> 16) & 0xff)) & 0xff);
		d = (*dst - sten[bx]) & 0xff;
		*dst++ = (u8)((d & 0x80) ? d : 0x81);
		u += du;
		v += dv;
	}
}

/* ORIGIN img 0x38a3 Poly_Fill.
 *
 * AFFINE PER SCANLINE, AND THE GRADIENT IS RECOMPUTED EVERY ROW - one
 * divide per row per interpolant, never a polygon-wide pair. There is
 * no perspective correction anywhere in this demo.
 *
 * THE SPAN IS A POINTER IN THE ORIGINAL, g_SpanRoutine at img 0x8f6a,
 * so one fill serves five per-pixel loops. Passing it keeps that
 * shape rather than growing a flag per variant.
 *
 * [open] THE DIRTY BOX IS NOT KEPT. The original tracks one so
 * Screen_Present blits only what changed; nothing here presents
 * partially, so it would be bookkeeping with no reader. */
static void poly_fill_with(u8 *dst, const u8 *tex, const LithPoly *p,
                           s32 band, LithSpanFn span)
{
	s32 y;

	poly_clear_spans();
	/* Poly_Fill closes the polygon itself by copying vertex 0, so the
	 * edge walk needs no wrap test - img 0x38a3. */
	poly_edge(p->x[0], p->y[0], p->u[0], p->v[0],
	          p->x[1], p->y[1], p->u[1], p->v[1]);
	poly_edge(p->x[1], p->y[1], p->u[1], p->v[1],
	          p->x[2], p->y[2], p->u[2], p->v[2]);
	poly_edge(p->x[2], p->y[2], p->u[2], p->v[2],
	          p->x[0], p->y[0], p->u[0], p->v[0]);

	for (y = g_row_min; y <= g_row_max; y++) {
		s32 left, right, w, du, dv, u, v, skip;

		if (y < 0 || y >= LITH_SPAN_ROWS) continue;
		left = g_span[y].x0;
		right = g_span[y].x1;
		if (right < left) continue;

		/* img 0x3b7a - the width is INCLUSIVE before the divide. */
		w = right - left + 1;
		du = lith_idiv(g_span[y].u1 - g_span[y].u0, w);
		dv = lith_idiv(g_span[y].v1 - g_span[y].v0, w);
		u = g_span[y].u0;
		v = g_span[y].v0;

		/* img 0x3c0c and 0x3c12 - clamp to the screen and carry the
		 * interpolants over whatever the left edge lost. */
		skip = 0;
		if (left < 0) { skip = -left; left = 0; }
		if (right > PD_SCREEN_W - 1) right = PD_SCREEN_W - 1;
		if (right < left) continue;
		u += du * skip;
		v += dv * skip;

		span(dst + y * PD_SCREEN_W + left, tex,
		     right - left + 1, u, du, v, dv, band);
	}
}

/* OURS - the span the scenes select for everything but the shadow. */
void Lith_PolyFill(u8 *dst, const u8 *tex, const LithPoly *p, s32 band)
{
	poly_fill_with(dst, tex, p, band, poly_span);
}

/* OURS - and the one they select for it, img 0x34b9. */
void Lith_PolyFillShadow(u8 *dst, const u8 *sten, const LithPoly *p)
{
	poly_fill_with(dst, sten, p, 0, poly_span_planar);
}

/* ORIGIN img 0x2bf0 - the shatter's fill, which differs from the
 * others only in the span routine it points at. */
void Lith_PolyFillScreen(u8 *dst, const u8 *tex, const LithPoly *p)
{
	poly_fill_with(dst, tex, p, 0, poly_span_screen);
}

/* ORIGIN img 0x3cbc Poly_ScanEdges - and it is a 2D scan, nothing
 * more: g_PolyArgs at img 0xe1b2 is a list of (x, y) WORD pairs and
 * this walks the edges writing only the row range and each row's x
 * span. Terrain_Build is its only caller, which is why the terrain's
 * extent is the span row range - effects/terrain.txt section 1.
 *
 * THE ROW RANGE IS KEPT AS row << 5, the span record's own stride,
 * because the original indexes the table by byte offset. */
void Lith_PolyScanEdges(const s32 *xy, s32 n)
{
	s32 i;

	poly_clear_spans();
	for (i = 0; i < n; i++) {
		s32 x0 = xy[i * 2], y0 = xy[i * 2 + 1];
		s32 j = (i + 1 == n) ? 0 : i + 1;
		s32 x1 = xy[j * 2], y1 = xy[j * 2 + 1];
		s32 dy, step, acc, y;

		if (y0 == y1) continue;
		if (y0 > y1) {
			s32 t;
			t = x0; x0 = x1; x1 = t;
			t = y0; y0 = y1; y1 = t;
		}
		dy = y1 - y0;
		step = lith_idiv((x1 - x0) << 8, dy);   /* 8.8, img 0x3cf8 */
		acc = (x0 << 8) + (step >> 1);          /* img 0x3d1f      */
		for (y = y0; y < y1; y++) {
			s32 x;

			if (y < 0 || y >= LITH_SPAN_ROWS) { acc += step; continue; }
			if (y < g_row_min) g_row_min = y;
			if (y > g_row_max) g_row_max = y;
			x = acc >> 8;
			if (x < g_span[y].x0) g_span[y].x0 = x;
			if (x > g_span[y].x1) g_span[y].x1 = x;
			acc += step;
		}
	}
}

/* OURS - Terrain_Build reuses two words of each span record to pass
 * a vertex-pool offset from its vertex pass to its face pass; here
 * they are named fields rather than an alias of u0. */
void Lith_SpanSetBase(s32 row, s32 which, s32 v)
{
	if (row >= 0 && row < LITH_SPAN_ROWS) g_span[row].base[which] = v;
}

/* OURS */
s32 Lith_SpanBase(s32 row, s32 which)
{
	if (row < 0 || row >= LITH_SPAN_ROWS) return 0;
	return g_span[row].base[which];
}

/* ORIGIN img 0x5354 - Math_BuildSquareTable leaves the range at the
 * full screen so the next clear sweeps the whole table. */
void Lith_SpanResetFull(void)
{
	g_row_min = 0;
	g_row_max = LITH_SPAN_ROWS - 1;
}

s32 Lith_SpanRowMin(void) { return g_row_min; }
s32 Lith_SpanRowMax(void) { return g_row_max; }

/* OURS */
s32 Lith_SpanX0(s32 row)
{
	if (row < 0 || row >= LITH_SPAN_ROWS) return 0;
	return g_span[row].x0;
}

/* OURS */
s32 Lith_SpanX1(s32 row)
{
	if (row < 0 || row >= LITH_SPAN_ROWS) return -1;
	return g_span[row].x1;
}

/* OURS - the spans survive between calls, so a caller that drops a
 * frame has to start from a known state. */
void Lith_PolyReset(void)
{
	s32 y;

	for (y = 0; y < LITH_SPAN_ROWS; y++) {
		g_span[y].x0 = PD_SCREEN_W;
		g_span[y].x1 = -1;
	}
	g_row_min = LITH_SPAN_ROWS - 1;
	g_row_max = 0;
}
