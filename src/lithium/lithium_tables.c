/* LITHIUM - the sine table, built out of the image.
 *
 * ORIGIN img 0x012b - the first thing Demo_Main does.
 */

#include <string.h>

#include "lithium.h"

static s32 g_sin[LITH_SIN_ENTRIES];
static int g_built;

/* ORIGIN img 0x012b.
 *
 * ONE CONTIGUOUS TABLE OF 2560 DWORDS, not three tables. The image
 * stores only a quarter turn - 512 signed words rising 0 to 4096 at
 * img 0x1a535 - and the two build loops fill four quadrants from it:
 *
 *     0    .. 511    src[0..511]      0 to 90 degrees
 *     512  .. 1023   src[511..0]      90 to 180
 *     1024 .. 1535   -src[0..511]     180 to 270
 *     1536 .. 2047   -src[511..0]     270 to 360
 *     2048 .. 2559   src[0..511]      THE WRAP TAIL
 *
 * So 2048 entries are one period, which is why every rotation masks
 * its angle with 0x1ffc: a BYTE offset into dwords, 2048 units to the
 * circle, and the low two bits discarded.
 *
 * THE WRAP TAIL EXISTS FOR THE COSINE AND NOTHING ELSE. Cosine is
 * read as sine 512 entries further on, so the highest angle indexes
 * 2047 + 512 = 2559 - exactly the last entry. */
void Lith_SinTableBuild(void)
{
	s32 i;
	s32 src[LITH_SIN_QUARTER];

	if (!g_lith_img) return;
	for (i = 0; i < LITH_SIN_QUARTER; i++)
		src[i] = (s32)(s16)RD_U16(g_lith_img + LITH_SIN_SRC + i * 2);

	for (i = 0; i < LITH_SIN_QUARTER; i++) {
		s32 r = src[LITH_SIN_QUARTER - 1 - i];
		g_sin[i]        =  src[i];
		g_sin[512 + i]  =  r;
		g_sin[1024 + i] = -src[i];
		g_sin[1536 + i] = -r;
		g_sin[2048 + i] =  src[i];
	}
	g_built = 1;
}

/* OURS - the masked lookup every rotation opens with, so the mask
 * lives in one place rather than at each call. */
s32 Lith_Sin(s32 ang)
{
	return g_built ? g_sin[(ang & 0x1ffc) >> 2] : 0;
}

/* OURS */
s32 Lith_Cos(s32 ang)
{
	return g_built ? g_sin[((ang & 0x1ffc) >> 2) + LITH_SIN_QUARTER] : 0;
}

/* OURS - for a caller that wants the table itself. */
const s32 *Lith_SinTable(void)
{
	return g_built ? g_sin : (const s32 *)0;
}
