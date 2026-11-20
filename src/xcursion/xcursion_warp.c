/* XCURSION - the plasma warp. effects/warp.txt.
 *
 * Warp_RenderFull at img 0x287b, runner Warp_RunFull at img 0x2639.
 *
 * THE FIELD IS BOTH THE DISPLACEMENT AND THE PICTURE. Two samples of
 * the plasma give a source offset, and the pixels copied from that
 * offset are the plasma again - so it folds over itself rather than
 * warping some separate image.
 */

#include "xcursion.h"

/* ORIGIN img 0x0b24 - every ZERO byte in the field becomes 1, because
 * Pal_Cycle leaves entry 0 alone and a 0 pixel would sit still while
 * the veins around it cycled - effects/warp.txt section 1. The loop
 * runs until SI wraps, so it covers the whole 64 KB. */
void Xcur_BufClampMin1(u8 *buf)
{
	s32 i;

	for (i = 0; i < 0x10000; i++)
		if (buf[i] == 0) buf[i] = 1;        /* img 0x0b30 */
}

/* ORIGIN img 0x287b - Warp_RenderFull.
 *
 * It works in 2x2 BLOCKS: 160 by 100 of them, each one source word
 * copied to two screen rows, so the field is sampled at half
 * resolution in both axes.
 *
 * img 0x287b packs the six animated offsets into three words, HIGH
 * byte of each - two index the field and the third is added to the
 * result. effects/warp.txt section 4.
 *
 * THE TAIL CHECK IS NOT A CLIP. img 0x28dd takes the byte-at-a-time
 * path only when the source offset is within two bytes of the
 * segment end, so that reading a word cannot wrap the segment. Both
 * paths draw; neither skips a block.
 */
void Xcur_WarpRenderFull(u8 *fb, const u8 *field, const s32 *ofs)
{
	/* img 0x287b - low byte from one offset, high byte from another */
	u32 d7 = (u32)(((ofs[0] >> 8) & 0xff) | (((ofs[3] >> 8) & 0xff) << 8));
	u32 d9 = (u32)(((ofs[1] >> 8) & 0xff) | (((ofs[4] >> 8) & 0xff) << 8));
	u32 db = (u32)(((ofs[2] >> 8) & 0xff) | (((ofs[5] >> 8) & 0xff) << 8));
	u32 si = 0, di = 0;
	s32 row, col;

	for (row = 0; row < 100; row++) {           /* img 0x28b2 */
		for (col = 0; col < 160; col++) {       /* img 0x28b5 */
			u32 bx = (u32)(field[(d7 + si) & 0xffff] >> 3);   /* img 0x28bf */
			u32 ax = ((u32)(field[(d9 + si) & 0xffff] & 0xf8) << 5) & 0xffff;

			bx = (bx + ax + si + db) & 0xffff;  /* img 0x28d4 */
			if (di + PD_SCREEN_W + 1 < PD_SCREEN_W * PD_SCREEN_H) {
				fb[di]                      = field[bx];
				fb[di + 1]                  = field[(bx + 1) & 0xffff];
				fb[di + PD_SCREEN_W]        = field[(bx + 0x100) & 0xffff];
				fb[di + PD_SCREEN_W + 1]    = field[(bx + 0x101) & 0xffff];
			}
			di += 2;                            /* img 0x2911 */
			si += 2;
		}
		si = (si + 0xc0) & 0xffff;              /* img 0x2919, two field rows */
		di += PD_SCREEN_W;              /* img 0x291d, two screen rows */
	}
}
