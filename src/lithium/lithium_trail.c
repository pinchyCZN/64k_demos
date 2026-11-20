/* LITHIUM - the plasma the battle fires.
 *
 * ORIGIN img 0x6164 Trail_Step and img 0x26a4 Trail_Draw.
 * docs/lithium/effects/trail.txt.
 *
 * SIXTY-FOUR PARTICLES AND ONE BIT DECIDES WHAT EACH ONE IS. Bit 0
 * of the life gates gravity in the step and picks the blitter in the
 * draw, so the ring holds two kinds at once: a heavy bolt that falls
 * and burns its own colour, and a weightless spark that drifts and
 * only brightens. Nothing else distinguishes them.
 *
 * THE LIFE IS ALSO THE BRIGHTNESS. The draw divides it by the depth,
 * caps at 0xfe and COMPLEMENTS the result into the flare's colour -
 * and the blitters subtract that from the sprite, so a closer or
 * younger particle leaves more of the cone above the cutoff and
 * comes out bigger and brighter. One counter, three jobs.
 *
 * g_TrailActive IS A LATCH, NOT A SETTING. The draw clears it and
 * sets it again only if something was alive, so a frame with nothing
 * in the ring costs one compare and makes the next step return at
 * once - neither routine ever counts the living.
 */

#include <string.h>

#include "lithium.h"

/* The ring at 0x8ad2:0x7a80, and g_TrailActive img 0x8fbc. */
static LithTrail g_ring[LITH_TRAIL_MAX];
static s32 g_active;

/* g_TrailWriteCursor img 0x8fb5. It ships as the ring base and NO
 * SCENE RESETS IT - the clears at img 0x1fdb and 0x2528 zero the 64
 * records and leave the cursor where it stood. */
static s32 g_write;

LithTrail *Lith_TrailRing(void) { return g_ring; }
s32 Lith_TrailActive(void) { return g_active; }

/* ORIGIN img 0x5ebf Trail_Spawn's clear half - the scenes that use
 * the ring zero it first, img 0x2523. */
void Lith_TrailReset(void)
{
	memset(g_ring, 0, sizeof g_ring);
	g_active = 0;
}

/* OURS - a signed shift on the word a velocity is kept in. */
static s32 trail_sar16(s32 v, s32 n)
{
	return (s32)((s16)(u16)v >> n);
}

/* ORIGIN img 0x5eba Trail_Spawn - take the next record, fill in the
 * POSITION only, and leave it to the caller.
 *
 * A WRAPPING CURSOR IS THE WHOLE ALLOCATOR: the 65th spawn overwrites
 * the first whether it is alive or not. ./trail.txt 5.1.
 *
 * THE POSITION IS THE DRAWN ONE, img 0x5eca - g_SlotHome rather than
 * g_SlotFlight, so a particle is laid where the model is seen to be
 * and not where the flight model has got to. */
LithTrail *Lith_TrailSpawn(const LithSlot *o)
{
	LithTrail *t = &g_ring[g_write];

	g_active = 1;                           /* img 0x5ebf */
	t->pos[0] = o->home_x;                  /* img 0x5eca */
	t->pos[1] = o->home_y;
	t->pos[2] = o->home_z;
	if (++g_write >= LITH_TRAIL_MAX) g_write = 0;    /* img 0x5ee7 */
	return t;
}

/* ORIGIN img 0x5e4d Trail_SpawnBurst - n records, the explosion.
 *
 * MASKING THE RANDOM EVEN AND ADDING AN ODD CONSTANT MAKES THE LIFE
 * ODD EVERY TIME, which is what buys gravity and the burning blitter
 * - ./trail.txt 5.2.
 *
 * THE VERTICAL IS AN UNSIGNED SHIFT where the other two are signed,
 * img 0x5e70, so a burst always rises. */
LithTrail *Lith_TrailSpawnBurst(const LithSlot *o, s32 n)
{
	LithTrail *t = (LithTrail *)0;

	while (n-- > 0) {
		t = Lith_TrailSpawn(o);

		t->life = (s32)(u32)(Lith_RndNextB() & 0xfffe) + 0x38401L;
		t->vel[0] = (s16)trail_sar16((s32)Lith_RndNextB(), 9);
		t->vel[1] = (s16)(Lith_RndNextB() >> 8);
		t->vel[2] = (s16)trail_sar16((s32)Lith_RndNextB(), 9);
	}
	return t;
}

/* ORIGIN img 0x5e82 Trail_SpawnBurstBig - the tower's, and the same
 * routine with a longer life, a wider spread and a lift of 0x80. */
LithTrail *Lith_TrailSpawnBurstBig(const LithSlot *o, s32 n)
{
	LithTrail *t = (LithTrail *)0;

	while (n-- > 0) {
		t = Lith_TrailSpawn(o);

		t->life = (s32)(u32)(Lith_RndNextB() & 0xfffe) + 0x80001L;
		t->vel[0] = (s16)trail_sar16((s32)Lith_RndNextB(), 7);
		t->vel[1] = (s16)(trail_sar16((s32)Lith_RndNextB(), 7) + 0x80);
		t->vel[2] = (s16)trail_sar16((s32)Lith_RndNextB(), 7);
	}
	return t;
}

/* ORIGIN img 0x6164 Trail_Step - once a tick, from all three track
 * handlers.
 *
 * THE RESPAWN VALUE IS EVEN, so a recycled record always comes back
 * as the weightless kind however it started. */
void Lith_TrailStep(void)
{
	s32 i;

	if (!g_active) return;                  /* img 0x6164 */
	for (i = 0; i < LITH_TRAIL_MAX; i++) {
		LithTrail *t = &g_ring[i];

		t->pos[0] += t->vel[0];
		t->pos[1] += t->vel[1];
		t->pos[2] += t->vel[2];
		if (t->life & 1) {
			t->vel[1] = (s16)(t->vel[1] - 1);   /* gravity */
			if (t->life < 0xaa00L) {
				t->life = 0x28000L;
				t->vel[0] = 0;
				t->vel[1] = 0x80;
				t->vel[2] = 0;
			}
		}
		t->life -= 0x7d0;
		if (t->life < 0) t->life = 0;
	}
}

/* ORIGIN img 0x26a4 Trail_Draw - once a frame.
 *
 * IT IS DRAWN IN THE CAMERA'S FRAME BY HAND rather than through
 * Scene_ProjectVerts, because a particle is a point and carries no
 * vertex record - effects/trail.txt section 3.1.
 *
 * THE NEAR REJECT IS A REJECT HERE, img 0x275e, where every other
 * projection in the demo clamps. */
void Lith_TrailDraw(u8 *back, const s32 cam_pos[3], const s32 cam_ang[3])
{
	s32 i;

	if (!g_active) return;                  /* img 0x26b4 */
	g_active = 0;                           /* img 0x26bd */
	for (i = 0; i < LITH_TRAIL_MAX; i++) {
		LithTrail *t = &g_ring[i];
		s32 x, y, z, sx, sy, d;

		if (t->life <= 0) continue;         /* img 0x26c7 */
		g_active = 1;                       /* img 0x26d1 */

		x = -(t->pos[0] - cam_pos[0]) >> 4; /* img 0x26ef */
		y = -(t->pos[1] - cam_pos[1]) >> 4;
		z = (t->pos[2] - cam_pos[2]) >> 4;

		Lith_VecRotY(&x, &y, &z, cam_ang[1]);
		x >>= 0xc; y >>= 0xc; z >>= 0xc;
		Lith_VecRotX(&x, &y, &z, cam_ang[0]);
		x >>= 0xc; y >>= 0xc; z >>= 0xc;
		Lith_VecRotZ(&x, &y, &z, cam_ang[2]);
		x >>= 0xc; y >>= 0xc; z >>= 0xc;

		if (z < 0xa) continue;              /* img 0x275e */
		sx = lith_idiv_shl8(x, z);          /* img 0x276a, 64-bit */
		sy = lith_idiv_shl8(y, z);
		d = t->life / z;                    /* img 0x2792 */
		if (d > 0xfe) d = 0xfe;
		Lith_FlareColourSet((~d) & 0xff);   /* img 0x27a9, NOT DL */

		Lith_FlareSavedSet(0xa0, 0x64);     /* img 0x27b1 */
		if (t->life & 1) Lith_BlobDrawBurn(back, sx, sy);
		else Lith_BlobDrawKeepBand(back, sx, sy);
	}
}
