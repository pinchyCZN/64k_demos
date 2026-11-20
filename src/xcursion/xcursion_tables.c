/* XCURSION - the sine table, read out of the image. */

#include "xcursion.h"

/* ORIGIN img 0x7f5e - g_SinTable.
 *
 * THE INDEX IS A BYTE OFFSET, not an entry number, and the mask is
 * 0x7fc rather than 0x7ff - so the low two bits are discarded and
 * the table is addressed at its natural 4-byte stride. Masking to
 * 0x7fc wraps at 512 entries; the table is 640 long because the
 * cosine read is the same table 0x200 bytes on and runs past the
 * end of one turn into the next.
 */
static s32 tab(s32 entry)
{
	const u8 *p;
	if (!g_img || entry < 0 || entry >= XCUR_SIN_ENTRIES) return 0;
	p = g_img + XCUR_SIN_TABLE + (u32)entry * 4;
	return (s32)RD_S32(p);
}

/* OURS */
s32 Xcur_Sin(s32 byte_off)
{
	return tab((byte_off & 0x7fc) >> 2);
}

/* OURS */
s32 Xcur_Cos(s32 byte_off)
{
	return tab(((byte_off & 0x7fc) >> 2) + (s32)(XCUR_COS_SKEW / 4));
}

/* ORIGIN img 0x2840 and img 0x284d - THE TABLE READ ONE BYTE IN, which
 * takes bits 8..23 of the dword and so is a free >> 8 giving a 5-bit
 * sine. Not a misaligned read: aligning it to 0x7f5e multiplies the
 * term by 256 - effects/moire.txt section 4. */
s32 Xcur_Sin5(s32 byte_off)
{
	s32 entry = (byte_off & 0x7fc) >> 2;

	if (!g_img || entry < 0 || entry >= XCUR_SIN_ENTRIES) return 0;
	return (s32)(s16)RD_U16(g_img + XCUR_SIN_TABLE + (u32)entry * 4 + 1);
}
