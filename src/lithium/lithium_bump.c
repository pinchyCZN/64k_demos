/* LITHIUM - the bump-mapped surface.
 *
 * ORIGIN img 0x0806 stamps the dust into the height field, img
 * 0x08af relaxes it and img 0x0ad1 Bump_Render turns it into pixels.
 * The first two are inline in maybe_Scene_BumpShatter; splitting
 * them into functions is ours. docs/lithium/effects/bump.txt.
 *
 * THERE IS NO NORMAL AND NO DOT PRODUCT. A byte of slope on each
 * axis is assembled into a 16-bit offset and the picture is READ AT
 * THAT DISPLACEMENT, so a flat pixel samples the picture where the
 * light sits and a slope drags the sample sideways. The displacement
 * IS the shading.
 *
 * THE BUFFERS ARE WHOLE 64 KB SEGMENTS, not 320x200. Every offset
 * here is a 16-bit quantity that wraps, the relaxation reads a row
 * below the last one it writes, and the stamp's brush overhangs the
 * bottom - so a 64,000-byte array would read and write out of
 * bounds. LITH_SEG_BYTES is the size the original's segment is.
 *
 * All three are byte-exact against the emulator, captured at the
 * stage boundaries img 0x0806, 0x08af, 0x08e8 and 0x08f0 of one
 * frame: the stamp and the relaxation over all 65,536 bytes of the
 * field and the render over all 63,360 it writes [verified].
 */

#include <string.h>

#include "lithium.h"

/* g_FieldToggle img 0x8fe7 - which half the interlaced paths take. */
static s32 g_field_toggle;

void Lith_BumpFieldToggleReset(void) { g_field_toggle = 0; }

s32 Lith_BumpFieldToggle(void) { return g_field_toggle; }

/* ORIGIN img 0x0832 to 0x083d - which brush the stamp uses.
 *
 * ZERO PICKS THE WEAK ONE. The JNZ at 0x083b keeps g_DustBrushA only
 * when g_BumpInterlace is SET, so the scene opens on g_DustBrushB,
 * which peaks at 4 against A's 8. */
const u8 *Lith_DustBrush(s32 interlace)
{
	if (!g_lith_img) return 0;
	return g_lith_img + (interlace ? LITH_DUST_BRUSH_A
	                               : LITH_DUST_BRUSH_B);
}

/* ORIGIN img 0x0806 - the dust plotter.
 *
 * IT DRAWS NO PIXELS. Each particle adds an 8x8 brush to the HEIGHT
 * FIELD, every byte saturating at 0xff, which is what makes a
 * thousand points read as a surface rather than as noise.
 *
 * SEVEN FRACTIONAL BITS, and the shift is unsigned - img 0x081a is
 * SHR, so a coordinate up to 0xa000 survives it.
 *
 * THE OFFSET IS 16-BIT AND THE SUM WRAPS BEFORE THE BOUND IS TESTED.
 * A particle below the screen does not vanish; its offset folds back
 * into the segment and it stamps somewhere else. Computing the sum
 * wider skips those particles instead and leaves the field empty.
 *
 * THE BOUND IS RE-TESTED FOR EVERY BRUSH ROW, img 0x0842 at the top
 * of the row loop, so a brush that reaches row 198 is ABANDONED
 * part-drawn rather than clipped or wrapped. Writing all eight rows
 * regardless puts height into rows the relaxation cannot reach. */
void Lith_DustStamp(u8 *field, const u8 *pool, const u8 *brush)
{
	s32 i, r, c;

	for (i = 0; i < LITH_DUST_COUNT; i++) {
		u32 bx;
		const u8 *p = pool + i * LITH_DUST_REC;
		u32 x = (u32)(p[0] | (p[1] << 8));
		u32 y = (u32)(p[2] | (p[3] << 8));

		bx = (((y >> 7) * PD_SCREEN_W) + (x >> 7)) & 0xffff;
		if (bx <= 0x140) continue;              /* img 0x082a */

		for (r = 0; r < 8; r++) {
			if (bx >= 0xf780) break;        /* img 0x0842 */
			for (c = 0; c < 8; c++) {
				s32 v = field[bx + c] + brush[r * 8 + c];

				field[bx + c] = (u8)(v > 0xff ? 0xff : v);
			}
			bx = (bx + PD_SCREEN_W) & 0xffff;
		}
	}
}

/* ORIGIN img 0x08af - the height field's relaxation.
 *
 * A FOUR-NEIGHBOUR AVERAGE WITH A DECAY OF ONE. The average spreads
 * a stamp outward a pixel a frame and the DEC at 0x08d3 drains it,
 * so a bump rises where particles are and dies where they have left
 * - no per-particle lifetime anywhere. The decay is skipped at zero,
 * so the field floors instead of wrapping.
 *
 * IT RUNS IN PLACE AND THAT IS LOAD-BEARING. The original stores to
 * [DI] and then steps DI, so the next pixel's [DI-1] and the next
 * row's [DI-0x140] read what this pass has ALREADY written. The
 * smoothing therefore propagates down and to the right inside one
 * pass, which a two-buffer version does not reproduce.
 *
 * THE INTERLACE HALVES THE WORK AND DOUBLES THE RELIEF. Clear - how
 * the scene opens - walks 0x63 rows skipping one each time, so half
 * the field keeps its full height every frame; the interlaced field
 * carries about twelve times the total height of one relaxed in
 * full. effects/bump.txt section 5.
 * THE PARITY IS g_FieldToggle, img 0x08a3 - the render's counter. */
void Lith_HeightRelax(u8 *field, s32 interlace, s32 parity)
{
	s32 rows = 0xc7, step = 0, n, k;
	u32 di = PD_SCREEN_W;

	if (!interlace) {
		rows = 0x63;                            /* img 0x08a0 */
		step = PD_SCREEN_W;
		if (parity & 1) di += PD_SCREEN_W;      /* img 0x08ab */
	}
	for (n = 0; n < rows; n++) {
		for (k = 0; k < PD_SCREEN_W; k++) {
			s32 v = (field[di - 1] + field[di + 1]
			         + field[di + PD_SCREEN_W]
			         + field[di - PD_SCREEN_W]) >> 2;

			field[di] = (u8)(v ? v - 1 : 0);
			di++;
		}
		di += step;                             /* img 0x08e1 */
	}
}

/* ORIGIN img 0x0ad1 Bump_Render.
 *
 * THE GRADIENT PAIR IS THE ADDRESS: bx is assembled as (dy, dx), so
 * a byte of slope on each axis becomes a 16-bit offset into the
 * picture at ES. effects/bump.txt section 4.
 *
 * THE HEIGHTS ARE READ FOUR BYTES AHEAD OF THE PIXELS THEY SHADE.
 * img 0x0b20 reads [DI+4] and img 0x0b74 writes [DI] after img
 * 0x0b48 has stepped DI down, so the relief sits four columns left
 * of the field that made it - and the first row written is 1, not 2.
 *
 * THE SUBTRACTIONS ARE 32-BIT AND THEIR BORROWS CROSS BYTES. Only
 * the DOUBLING's carry is masked away, by the 0xfefefefe at img
 * 0x0b3e clearing each byte's low bit. Doing the four bytes
 * independently disagrees with the original on about a quarter of
 * the pixels, by one or two levels each.
 *
 * THE HORIZONTAL NEIGHBOUR IS THE ONE TO THE RIGHT. img 0x0b2c
 * exchanges CL with BL and img 0x0b2e rotates, which carries the
 * previous group's first height in as this group's fourth
 * neighbour - so dx is height minus the byte ABOVE it in memory,
 * and CL threads that chain across groups and across rows.
 *
 * SI IS SWEPT, NOT HELD: down 2 a group and up 0xa0 a row, which
 * cancel, and down 0x100 a row - so the picture is MAPPED over the
 * surface and opposite corners of the screen sit near different
 * glows of the three in it. Within a group the left pair reads SI
 * and the right pair SI+1.
 *
 * ON THE FULL-RATE PATH SI DROPS EVERY SECOND ROW. img 0x0b89 bumps
 * the field toggle and img 0x0b94 goes straight back round while it
 * is odd, leaving SI alone - so the light's vertical sweep is one
 * texel per TWO screen rows there and one per row when interlaced. */
void Lith_BumpRender(const u8 *field, const u8 *pic, u8 *out,
                     s32 light_x, s32 light_y, s32 interlace)
{
	u32 si = (u32)(((light_x >> 8) + (light_y & 0xff00)) & 0xffff);
	u32 di = 0xf8bc;
	u32 cl = 0;
	s32 g, i;

	if (!interlace) {
		g_field_toggle = (g_field_toggle + 1) & 0xff;  /* img 0x0b03 */
		if (g_field_toggle & 1) {
			di -= 0x140;                    /* img 0x0b10 */
			si = (si - 0x100) & 0xffff;
		}
	} else {
		g_field_toggle = 0;                     /* img 0x0af6 */
	}

	for (;;) {
		for (g = 0; g < 0x50; g++) {
			u32 eax, edx, ebx, w;
			u8 px[4];

			eax = RD_U32(field + di + 4);
			edx = RD_U32(field + ((di - 0x13c) & 0xffff));
			ebx = eax;
			{
				u32 t = ebx & 0xff;     /* img 0x0b2c */

				ebx = (ebx & 0xffffff00UL) | cl;
				cl = t;
			}
			ebx = ((ebx >> 8) | (ebx << 24)) & 0xffffffffUL;
			edx = (edx - eax) & 0xffffffffUL;
			eax = (eax - ebx) & 0xffffffffUL;
			eax = (eax << 1) & 0xfefefefeUL;
			edx = (edx << 1) & 0xfefefefeUL;
			for (i = 0; i < 4; i++) {
				u32 dx = (eax >> (8 * i)) & 0xff;
				u32 dy = (edx >> (8 * i)) & 0xff;

				px[i] = pic[(((dy << 8) | dx) + si
				             + (i >= 2 ? 1 : 0)) & 0xffff];
			}
			w = di;                         /* img 0x0b48 then 0x0b74 */
			di -= 4;
			for (i = 0; i < 4; i++) out[w + i] = px[i];
			si = (si - 2) & 0xffff;         /* img 0x0b6d */
		}
		si = (si + 0xa0) & 0xffff;              /* img 0x0b7d */
		if (interlace) {
			g_field_toggle = (g_field_toggle + 1) & 0xff;
			if (g_field_toggle & 1) continue;  /* img 0x0b94 */
			si = (si - 0x100) & 0xffff;
			if (di >= 0x280) continue;      /* img 0x0b9a */
			break;
		}
		si = (si - 0x100) & 0xffff;             /* img 0x0ba4 */
		di -= 0x140;
		if (di < 0x280) break;                  /* img 0x0bac */
	}
}
