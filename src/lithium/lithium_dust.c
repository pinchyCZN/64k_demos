/* LITHIUM - the thousand particles that spell a word in ripples.
 *
 * ORIGIN img 0x6a7a is the mover inside Tick_Dust, img 0x6ad6 its
 * refill, img 0x0bb5 Dust_ResetFromPoint and img 0x0be1
 * Dust_SampleMask. docs/lithium/effects/dust.txt.
 *
 * A PARTICLE HAS NO VELOCITY. It carries three points - drawn, mid
 * and target - and chases: mid eases toward the target by an eighth
 * and drawn eases toward mid by a thirty-second. Only the target is
 * ever set from outside, so nothing here integrates and nothing
 * drifts. A particle needs about 200 ticks to arrive, and that is
 * what sets the scene's pace.
 *
 * THE COORDINATES ARE UNSIGNED 16-BIT, with seven fractional bits.
 * The stamp reads them with SHR at img 0x081a and a target may be up
 * to 0xa000, which a signed word could not hold; only the DIFFERENCE
 * in each ease is signed, img 0x6aa5.
 *
 * THE JITTER GOES ON THE TARGET, NOT THE POSITION, so it never
 * accumulates - a particle wanders around its destination instead of
 * drifting away from it.
 *
 * THE MOVER IS BYTE-EXACT against the emulator over all 12,000 bytes
 * of the pool, seeded from the generator's own state at img 0x6a82
 * of one tick [verified].
 */

#include <string.h>

#include "lithium.h"

/* OURS - the pool is the original's bytes, so the stamp and this
 * agree without a second layout. */
#define DUST_X(p)   ((s32)RD_U16(p))
#define DUST_W(p, v) ((p)[0] = (u8)((v) & 0xff), \
                      (p)[1] = (u8)(((v) >> 8) & 0xff))

/* OURS - a signed shift on a 16-bit field, which is what SAR does to
 * the word registers the mover keeps these in. */
static s32 dust_sar16(s32 v, s32 n)
{
	return (s32)((s16)(u16)v >> n);
}

/* ORIGIN img 0x6a7a to 0x6ad4 - the mover, once a tick.
 *
 * THE DRAWN POSITION EASES TOWARD THE NEW MID, not the old one: img
 * 0x6aaf stores mid before img 0x6aba reads it back for the second
 * stage, so the two eases are chained within a tick rather than
 * running off the same snapshot.
 *
 * THE SNAP WRITES THE TARGET AND SO TAKES EFFECT NEXT TICK. img
 * 0x6a9c lands after the jittered target has already been computed
 * from the old one at img 0x6a89.
 *
 * THE COMPARISON AT img 0x6a95 IS UNSIGNED - JBE, not JLE. */
void Lith_DustTick(u8 *pool, s32 jitter_shift, s32 snap_y)
{
	s32 i;
	s32 cl = jitter_shift & 7;

	for (i = 0; i < LITH_DUST_COUNT; i++) {
		u8 *p = pool + i * LITH_DUST_REC;
		s32 tx, ty, bp, di, bx, dx;
		u16 a, d;

		Lith_RndNextPair(&a, &d);               /* img 0x6a82 */
		tx = (dust_sar16(a, cl) + DUST_X(p + 8)) & 0xffff;
		ty = (dust_sar16(d, cl) + DUST_X(p + 10)) & 0xffff;

		bp = DUST_X(p + 4);
		di = DUST_X(p + 6);
		if ((u32)snap_y > (u32)di)              /* img 0x6a95 */
			DUST_W(p + 10, 0x6480);

		bp = (bp + dust_sar16(tx - bp, 3)) & 0xffff;
		di = (di + dust_sar16(ty - di, 3)) & 0xffff;
		DUST_W(p + 4, bp);
		DUST_W(p + 6, di);

		bx = DUST_X(p);
		dx = DUST_X(p + 2);
		bx = (bx + dust_sar16(bp - bx, 5)) & 0xffff;
		dx = (dx + dust_sar16(di - dx, 5)) & 0xffff;
		DUST_W(p, bx);
		DUST_W(p + 2, dx);
	}
}

/* ORIGIN img 0x0bb5 Dust_ResetFromPoint - the burst.
 *
 * EVERY PARTICLE STARTS AT THE SCREEN'S CENTRE: 0x5000 and 0x3200
 * shift down by seven to 160 and 100. Both the mid and the target
 * are random, so the cloud explodes outward and then gathers. */
void Lith_DustResetFromPoint(u8 *pool)
{
	s32 i;

	for (i = 0; i < LITH_DUST_COUNT; i++) {
		u8 *p = pool + i * LITH_DUST_REC;
		u16 a0, d0, a1, d1;

		Lith_RndNextPair(&a0, &d0);
		Lith_RndNextPair(&a1, &d1);
		DUST_W(p, 0x5000);
		DUST_W(p + 2, 0x3200);
		DUST_W(p + 4, a0);
		DUST_W(p + 6, d0);
		DUST_W(p + 8, a1);
		DUST_W(p + 10, d1);
	}
}

/* ORIGIN img 0x0be1 Dust_SampleMask.
 *
 * IT REJECTION-SAMPLES THE LIT PIXELS and writes only the TARGET, so
 * a re-sample MOVES the cloud instead of teleporting it.
 *
 * IT TESTS TWO ADJACENT BYTES, +0x6700 and +0x66ff, so a particle
 * needs its own pixel AND the one to its left - which keeps the
 * cloud off single-pixel edges.
 *
 * THE PHASE IS THE SAMPLE SHIFT AS WELL AS THE MODE, so as the
 * counter runs down the band of mask rows that can be hit widens and
 * the word arrives gradually. A phase too small accepts almost
 * nothing, which is why the retry count below is bounded - the
 * original spins instead, and the bound is OURS. */
void Lith_DustSampleMask(u8 *pool, const u8 *mask, s32 phase)
{
	s32 i, n;

	for (i = 0; i < LITH_DUST_COUNT; i++) {
		u8 *p = pool + i * LITH_DUST_REC;

		for (n = 0; n < LITH_DUST_RETRY; n++) {
			u16 a, d;
			s32 bp, di, bx;

			Lith_RndNextPair(&a, &d);
			d = (u16)(d >> 1);              /* img 0x0bef */
			bp = (s32)a;
			di = (s32)d;
			if (bp >= 0xa000 || di >= 0x6400) continue;
			di = (di - 0x3200) >> (phase & 0x1f);
			bp >>= 0xa;
			di += 0xa;
			bx = bp + di * PD_SCREEN_W;
			if (bx < 1 || bx >= 0x1900) continue;
			if (!mask[bx] || !mask[bx - 1]) continue;
			DUST_W(p + 8, a);
			DUST_W(p + 10, d);
			break;
		}
	}
}

/* ORIGIN img 0x6ad6 - the refill, once per music position change.
 *
 * ONE COUNTER IS BOTH THE MODE AND THE SHIFT. Phase 1 bursts from
 * the centre and everything else samples the mask, and the jitter
 * shift rides the same change so the cloud tightens and loosens on
 * its own.
 *
 * ONLY THE 0xfc RELOAD BELONGS HERE. The other one, 7 back to 1, is
 * at img 0x6b0f on the path BOTH branches take, so it runs every
 * tick rather than only on a beat - Lith_BumpTick has it. */
void Lith_DustModeStep(u8 *pool, const u8 *mask, s32 *phase,
                       s32 *jitter_shift)
{
	if (*phase == 1) Lith_DustResetFromPoint(pool);
	else Lith_DustSampleMask(pool, mask, *phase);

	*phase = (*phase - 1) & 0xff;
	*jitter_shift = (*jitter_shift + 1) & 7;
	if (*phase == 0xfc) *phase = 0xc;
}
