/* XCURSION - the moire. effects/moire.txt.
 *
 * Moire_Render at img 0x2818, and NOTHING IN IT PLOTS A LINE. Every
 * pixel adds a smooth increment to itself every frame into an 8-bit
 * accumulator, and the lines are where that sum crosses a multiple of
 * 256. The whole effect is one subtraction and one byte store.
 */

#include "xcursion.h"

/* ORIGIN img 0x2818 - Moire_Render.
 *
 * It is its own runner in the binary: img 0x286f loops the function
 * until the music reaches the target, so there is no Run beside it.
 *
 * THE -col TERM IS THE EFFECT. img 0x285d decrements DX once per
 * column, so neighbouring columns differ by exactly 1 in their
 * increment and by n after n frames - which bands the accumulator at
 * 256/n columns and makes the lines squeeze with nothing changing a
 * parameter.
 *
 * The row phase SI is seeded once before the row loop and accumulates
 * down the screen; the column phase BX is RE-SEEDED from the phase at
 * the top of every row, img 0x2837. That asymmetry is what tilts the
 * bands.
 *
 * Returns the advanced phase - img 0x2866 adds 2 a frame.
 */
s32 Xcur_MoireRender(u8 *fb, u8 *field, s32 phase)
{
	s32 bp = 0xc8, si = phase, di = 0, row, col;

	for (row = 0; row < 0xc8; row++) {          /* img 0x2834 */
		s32 bx = phase;                         /* img 0x2837, per row */
		s32 dx;

		si &= 0x7fc;                            /* img 0x283c */
		dx = (Xcur_Sin5(si) + bp) & 0xffff;     /* img 0x2840 */
		for (col = 0; col < 0x140; col++) {     /* img 0x2847 */
			s32 ax = (dx + Xcur_Sin5(bx)) & 0xffff;
			u8 al = (u8)((ax + field[di]) & 0xff);  /* img 0x2852, AL */

			field[di] = al;
			fb[di] = al;                        /* img 0x2856 */
			bx = (bx + 8) & 0xffff;             /* img 0x2859 */
			di++;
			dx = (dx - 1) & 0xffff;             /* img 0x285d */
		}
		si += 8;                                /* img 0x2860 */
		bp--;
	}
	return (phase + 2) & 0xffff;
}
