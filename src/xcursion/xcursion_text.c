/* XCURSION - the caption masks, and the fill that sets them alight. */

#include <string.h>

#include "xcursion.h"

/* ORIGIN img 0x330f - Text_BuildMask.
 *
 * A 32x32 BITMAP FONT EXPANDED TO ONE BYTE PER PIXEL. g_Font32 holds
 * 128 bytes per glyph - four bytes across by 32 rows - indexed by
 * (character - 0x20), and each bit becomes a mask byte of 0 or 1.
 *
 * STRINGS ARE FIXED WIDTH AND UNTERMINATED: exactly ten characters
 * per line, and ten glyphs of 32 pixels is exactly the 320-pixel
 * screen. The row count divided by 32 is the number of lines, so the
 * record's `rows` is what says how long the string is.
 *
 * 'a' AND 'b' ARE NOT LETTERS, they are half-character kerning. Both
 * draw a space; 'a' then shifts the cursor 16 pixels right and 'b'
 * 16 left, which is how an eleven-character word is centred in ten
 * slots - the banner record "bEXCURSION" is exactly that.
 *
 * mask must be 320*200 bytes. Returns the record's row count, and
 * writes the record's start offset through `ofs`.
 */
s32 Xcur_TextBuildMask(u8 *mask, u32 rec, s32 *ofs)
{
	s32 str, di, rows, lines, line;

	memset(mask, 0, PD_SCREEN_W * PD_SCREEN_H);
	if (!g_img) return 0;

	str = (s32)RD_U16(g_img + rec);
	di = (s32)RD_U16(g_img + rec + 2);
	rows = (s32)RD_U16(g_img + rec + 4);
	lines = rows >> 5;
	if (ofs) *ofs = di;

	for (line = 0; line < lines; line++) {
		s32 ch;
		for (ch = 0; ch < XCUR_TEXT_COLS; ch++) {
			s32 c = g_img[str++];
			s32 glyph, row, keep;

			if (c == 'a') { c = ' '; di += 0x10; }
			else if (c == 'b') { c = ' '; di -= 0x10; }

			glyph = (s32)XCUR_FONT32 + ((c - 0x20) << 7);
			keep = di;
			for (row = 0; row < 32; row++) {
				s32 byte;
				for (byte = 0; byte < 4; byte++) {
					s32 bits = g_img[glyph++], bit;
					for (bit = 0; bit < 8; bit++) {
						if (di >= 0 && di < PD_SCREEN_W * PD_SCREEN_H)
							mask[di] = (u8)(bits & 1);
						bits >>= 1;
						di++;
					}
				}
				di += PD_SCREEN_W - 32;
			}
			di = keep + 0x20;
		}
		/* The ten glyphs advanced the cursor by one screen row's
		 * worth, so the rest of the 32-row band remains. */
		di += 32 * PD_SCREEN_W - PD_SCREEN_W;
	}
	return rows;
}

/* ORIGIN img 0x23a3 - the fill inside FireText_Render.
 *
 * Only where the mask is set, and only where the texture clears the
 * threshold:
 *
 *     v = texture[bx + di] - threshold
 *     if (v >= 0) fb[di] = (v >> 2) + 0x60
 *
 * So it lands in 0x60..0x9f, the lower two thirds of the fire ramp,
 * and THRESHOLD IS THE FLAME HEIGHT: raising it rejects more of the
 * texture and the fill retreats down the glyphs.
 *
 * The texture row stride is NEGATIVE - bx steps by -0x40 a row - so
 * the flame crawls upward as the rows advance.
 */
void Xcur_FireTextFill(u8 *fb, const u8 *mask, const u8 *texture,
                       s32 tex_row_base, s32 threshold,
                       s32 ofs, s32 rows)
{
	s32 bx = tex_row_base, di = ofs, row, col;

	for (row = 0; row < rows; row++) {
		for (col = 0; col < PD_SCREEN_W; col++) {
			if (di >= 0 && di < PD_SCREEN_W * PD_SCREEN_H && mask[di]) {
				s32 v = texture[(bx + di) & 0xffff] - threshold;
				if (v >= 0)
					fb[di] = (u8)(((v >> 2) + 0x60) & 0xff);
			}
			di++;
		}
		bx = (bx - 0x40) & 0xffff;
	}
}

/* ORIGIN img 0x2504 - Flag_Render. effects/flag.txt.
 *
 * The cloth is the word VISTA, drawn into the texture by
 * Text_BuildBanner, displaced by two scrolling samples of the sine
 * field and blitted as 2x2 blocks.
 *
 * THE OUT-OF-RANGE FALLBACK IS THE WHOLE SCENE. img 0x2574 copies
 * from the work buffer instead of the cloth, so the frozen landscape
 * shows around and through the flag rather than a black panel - and
 * it is also what erases the previous frame's cloth, which is why
 * `back` has to be a separate buffer from `fb`.
 *
 * The y sample is shifted left 6 because the SOURCE STRIDE IS 0x100:
 * it is pre-multiplied into a row offset rather than multiplied per
 * sample. The bounds test at img 0x255a checks the two halves of BX
 * separately, BL < 0xa0 and BH < 0x80.
 */
void Xcur_FlagRender(u8 *fb, const u8 *back, const u8 *text,
                     const u8 *field, s32 ofs_x, s32 ofs_y)
{
	s32 si = 0x1846, di = 0x1e46, row, col;

	for (row = 0; row < 0x4c; row++) {          /* img 0x251e */
		for (col = 0; col < 0x5d; col++) {      /* img 0x2521 */
			s32 bp, bl, ax, bx;

			bp = (ofs_x + si) & 0x3fff;         /* img 0x252b */
			bl = ((field[bp] >> 2) - 0x70) & 0xff;
			bp = (ofs_y + si) & 0x3fff;         /* img 0x2543 */
			ax = field[bp] & 0xfc;              /* img 0x254e */
			ax = (ax << 6) & 0xffff;            /* img 0x2550 */
			ax = (ax - 0x4200) & 0xffff;
			bx = (bl + ax + si) & 0xffff;       /* img 0x2556 */

			if ((bx & 0xff) < 0xa0 && ((bx >> 8) & 0xff) < 0x80) {
				fb[di]     = text[bx];          /* img 0x2564, a WORD */
				fb[di + 1] = text[(bx + 1) & 0xffff];
				fb[di + PD_SCREEN_W]     = text[(bx + 0x100) & 0xffff];
				fb[di + PD_SCREEN_W + 1] = text[(bx + 0x101) & 0xffff];
			} else {
				fb[di]     = back[di];          /* img 0x2574 */
				fb[di + 1] = back[di + 1];
				fb[di + PD_SCREEN_W]     = back[di + PD_SCREEN_W];
				fb[di + PD_SCREEN_W + 1] = back[di + PD_SCREEN_W + 1];
			}
			di += 2;
			si += 2;
		}
		si += 0x146;                            /* img 0x258c */
		di += 0x1c6;                            /* img 0x2590 */
	}
}
