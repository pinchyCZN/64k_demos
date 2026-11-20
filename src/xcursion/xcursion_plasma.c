/* XCURSION - the diamond-square generator.
 *
 * It makes more than one thing: the landscape's height field and its
 * colour field are two runs with different parameters, and the
 * caption fill samples a third as its flame texture. The parameter
 * block comes from Demo_Timeline - effects/script1_landscape.txt.
 */

#include "xcursion.h"

/* ORIGIN img 0x410a - Rand_Next, and THE WHOLE DEMO DRAWS FROM ONE
 * STREAM. No generate re-seeds it, so a field depends on every draw
 * made before it: seeding per call - the obvious reading - gets the
 * first field right and every later one wrong.
 * effects/script1_landscape.txt section 6.1 has the order and counts. */
static u16 g_rnd_a = 0x9e8e;            /* img 0x9879 */
static u16 g_rnd_b = 0x636d;            /* img 0x987b */

/* ORIGIN img 0x4f64 */
void Xcur_RndSeed(void)
{
	g_rnd_a = 0x9e8e;
	g_rnd_b = 0x636d;
}

/* ORIGIN img 0x1d92 and img 0x1f79 - the stream is parked in img
 * 0x9929 and img 0x992b across the glow burst, so 64 draws of spark
 * velocity cannot move the fields the plasma cuts from it. */
static u16 g_rnd_pa, g_rnd_pb;          /* img 0x9929, img 0x992b */

void Xcur_RndSave(void)
{
	g_rnd_pa = g_rnd_a;
	g_rnd_pb = g_rnd_b;
}

/* OURS */
void Xcur_RndRestore(void)
{
	g_rnd_a = g_rnd_pa;
	g_rnd_b = g_rnd_pb;
}

/* ORIGIN img 0x410a - Rand_Next. */
u16 Xcur_RndNext(void)
{
	u16 swapped = (u16)((g_rnd_a & 0x00ff) | ((g_rnd_b & 0x00ff) << 8));
	u16 b_hi = (u16)((g_rnd_b & 0xff00) | ((g_rnd_a >> 8) & 0xff));
	u16 nxt = (u16)(swapped + b_hi);

	g_rnd_a = b_hi;
	g_rnd_b = nxt;
	return (u16)((nxt << 2) | (nxt >> 14));
}

/* ORIGIN img 0x40e3. One octave's offset, then CLAMP TO A BYTE.
 *
 * The clamp is why the coarse octaves dominate: with mask 0x3fff and
 * bias 0x2000 the first offset is +/-8192 against a 0..255 range, so
 * the top octaves saturate and lay down flat continents that the
 * finer ones then texture. */
static u8 displace(s32 avg, s32 mask, s32 bias, s32 shift)
{
	s32 d = ((s32)(Xcur_RndNext() & (u16)mask) - bias) >> shift;
	s32 v = avg + d;
	if (v < 0) v = 0;
	else if (v >= 0xff) v = 0xff;
	return (u8)v;
}

/* img 0x998f and img 0x9991 - the generator's own corner variables.
 * Mode 0xa borrows them as Lissajous phase - effects/battle.txt 8.3. */
s32 g_plasma_c0, g_plasma_c1;

/* ORIGIN img 0x3fae. Diamond-square over a 256x256 TORUS.
 *
 * THE TORUS IS FREE IN ONE DIRECTION AND NOT THE OTHER. Offsets are
 * 16-bit into one 64 KB segment, so stepping down past the last row
 * wraps by itself; only the horizontal edge needs the special case at
 * img 0x401a, where `[si + 0xff00]` is si - 0x100 - column 0 of the
 * SAME row, where si + 2*step would have been column 0 of the next.
 *
 * BUF IS NOT CLEARED and needs no clearing: the walk never reads a
 * cell it has not already written. buf must be 0x10000 bytes.
 */
void Xcur_PlasmaGenerate(u8 *buf, s32 mask, s32 bias, s32 rough)
{
	s32 step_lin = 0x8000, step = 0x80, shift = 1;

	buf[0] = (u8)(Xcur_RndNext() & 0xff);   /* img 0x3fb1 */

	while (step) {
		s32 row_base = step_lin, row = 0;
		for (;;) {
			s32 di = (row_base + step) & 0xffff;
			s32 col = 0;
			for (;;) {
				s32 two = (step * 2) & 0xffff;
				s32 two_lin = (step_lin * 2) & 0xffff;
				s32 si = (di - step - step_lin) & 0xffff;
				s32 c0, c1, c2, c3;

				c0 = buf[si];
				si = (si + two) & 0xffff;
				if ((0x100 - two) != col) {
					c1 = buf[si];
					si = (si + two_lin) & 0xffff;
					c2 = buf[si];
				} else {
					c1 = buf[(si - 0x100) & 0xffff];
					si = (si + two_lin) & 0xffff;
					c2 = buf[(si - 0x100) & 0xffff];
				}
				si = (si - two) & 0xffff;
				c3 = buf[si];
				g_plasma_c0 = c0;       /* img 0x3fff */
				g_plasma_c1 = c1;       /* img 0x401e, 0x4032 */

				buf[di] = displace((c0 + c1 + c2 + c3) >> 2,
				                   mask, bias, shift);
				buf[(di - step_lin) & 0xffff] =
				    displace((c0 + c1) >> 1, mask, bias, shift);
				buf[(di - step) & 0xffff] =
				    displace((c3 + c0) >> 1, mask, bias, shift);

				di = (di + two) & 0xffff;
				col = (col + two) & 0xffff;
				if (col == 0x100 || col == 0) break;
			}
			row = (row + step * 2) & 0xffff;
			row_base = (row_base + step_lin * 2) & 0xffff;
			if (row == 0x100 || row == 0) break;
		}
		step_lin >>= 1;
		shift += rough;
		step >>= 1;
	}
}
