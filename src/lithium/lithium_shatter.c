/* LITHIUM - the surface breaking like glass.
 *
 * ORIGIN img 0x2803 Shatter_Build, img 0x2c78 Shatter_InitShard and
 * img 0x2c05 Shatter_Step. docs/lithium/effects/shatter.txt.
 *
 * A HUNDRED AND FORTY TRIANGLES CARRYING THE PICTURE. The grid is
 * seven rows of ten cells, each cell a 32 by 32 square cut along its
 * diagonal into two shards, and every shard remembers the u,v it was
 * cut from - so the sheet that bursts is the frame that was on
 * screen a moment earlier.
 *
 * THE BUILD, THE STEP AND THE DRAW ARE BYTE-EXACT against the
 * emulator - the whole 6,720-byte array, and all 64,000 bytes of the
 * frame the draw composes [verified].
 *
 * THE ORIGINAL'S DRAW RACES ITS OWN TICK. Tick_Shatter steps the
 * shards off the 57 Hz handler and a 140-shard pass is longer than a
 * tick, so the original draws the tail of a frame from positions one
 * step ahead of the head. A frame of it is not internally consistent
 * and two captures of one pass differ; the acceptance test is
 * therefore the 106-shard prefix the original completes before the
 * first tick. ./shatter.txt section 5.1.
 */

#include <string.h>

#include "lithium.h"

static LithShard g_shard[LITH_SHARD_MAX];
static s32 g_shard_count;
static s32 g_on_screen;

LithShard *Lith_Shards(void) { return g_shard; }
s32 Lith_ShardCount(void) { return g_shard_count; }
s32 Lith_ShardsOnScreen(void) { return g_on_screen; }

/* OURS - a signed shift on the word these are kept in. */
static s32 shard_sar16(s32 v, s32 n)
{
	return (s32)((s16)(u16)v >> n);
}

/* ORIGIN img 0x2c78 Shatter_InitShard.
 *
 * EVERY VELOCITY CARRIES A TERM IN THE SHARD'S OWN GRID POSITION, so
 * the sheet opens outward and the random part only breaks up the
 * regularity. Z IS A CONE AND HAS NO RANDOM AT ALL, img 0x2ce1 to
 * 0x2cfd, while the tumble is pure noise - no positional term
 * anywhere in the angular velocity. effects/shatter.txt section 3. */
static void shatter_init_shard(LithShard *s, s32 cx, s32 dx)
{
	s32 k;

	s->pos[0] = (s16)cx;
	s->pos[1] = (s16)dx;
	s->pos[2] = (s16)0x100;
	for (k = 0; k < 3; k++) s->rot[k] = 0;

	for (k = 0; k < 3; k++)
		s->angvel[k] = (s16)shard_sar16((s32)Lith_RndNext(), 7);

	s->vel[0] = (s16)(shard_sar16((s32)Lith_RndNext(), 0xc)
	                  + (cx >> 6));
	s->vel[1] = (s16)(shard_sar16((s32)Lith_RndNext(), 0xc)
	                  + (dx >> 6)
	                  + (((cx < 0 ? -cx : cx) - 0xa0) >> 3));
	s->vel[2] = (s16)((((0x104 - ((cx < 0 ? -cx : cx)
	                              + (dx < 0 ? -dx : dx))) >> 4)) + 5);
}

/* g_ShatterAssemble img 0x8f85. Demo_Main sets it at img 0x0305 for
 * the transition into the battle and maybe_Scene_BumpShatter clears
 * it at img 0x0948 for the forward break; nothing else writes it. */
static s32 g_assemble;

void Lith_ShatterAssembleSet(s32 on) { g_assemble = on; }

/* ORIGIN img 0x28c7 to 0x2917 - the whole effect played out, then
 * turned round.
 *
 * FIVE STEPS PER TEST, img 0x28cf. Only the draw refreshes
 * g_ShardsOnScreen, so the fast-forward samples it once per five
 * ticks of simulation; the live effect steps once a tick.
 *
 * THE INC AT img 0x2903 IS THE GRAVITY CORRECTION, not a fudge -
 * negating the velocity alone leaves every shard a pixel a tick
 * adrift and the sheet never closes. effects/shatter.txt section 4.
 *
 * THE DRAW TARGET IS DISCARDED. img 0x290c and 0x2915 clear the back
 * buffer and the screen once the run is done, so none of the
 * fast-forward is ever seen - which is why ours needs no buffer to
 * draw into and passes a scratch one. */
static void shatter_reverse(void)
{
	static u8 scratch[PD_SCREEN_W * PD_SCREEN_H];
	s32 i, k, guard;

	for (guard = 0; guard < 4096; guard++) {
		for (k = 0; k < 5; k++) Lith_ShatterStep();
		Lith_ShatterDraw(scratch, scratch);
		if (!g_on_screen) break;
	}
	for (i = 0; i < g_shard_count; i++) {
		LithShard *s = &g_shard[i];

		for (k = 0; k < 3; k++) {
			s->vel[k] = (s16)(-s->vel[k]);
			s->angvel[k] = (s16)(-s->angvel[k]);
		}
		s->vel[1] = (s16)(s->vel[1] + 1);
	}
	memset(scratch, 0, sizeof scratch);
}

/* ORIGIN img 0x2803 Shatter_Build.
 *
 * THE ORIGINS 0x90 AND 0x54 ARE THE CENTRING: 144 and 84 is a 320 by
 * 200 screen less the sixteen-pixel half cell, so the grid sits in
 * the middle and the source u,v fall straight out of the loop
 * counters.
 *
 * THE REVERSAL IS THE SAME EFFECT RUN TO ITS END AND NEGATED - the
 * transition into the battle costs no second piece of code.
 * ./shatter.txt section 3.2. */
void Lith_ShatterBuild(void)
{
	static const s16 corner[2][6] = {
		{ -16, -16,  16, -16, -16,  16 },
		{ -16,  16,  16, -16,  16,  16 }
	};
	s32 cx, dx, half, k;

	g_shard_count = 0;
	for (dx = -0x54; dx <= 0x74; dx += 0x20) {
		for (cx = -0x90; cx < 0xb0; cx += 0x20) {
			for (half = 0; half < 2; half++) {
				LithShard *s = &g_shard[g_shard_count];
				const s16 *c = corner[half];

				for (k = 0; k < 6; k++) s->c[k] = c[k];
				shatter_init_shard(s, cx, dx);
				for (k = 0; k < 3; k++) {
					s->uv[k * 2] =
						(s16)(cx + 0x90 + c[k * 2] + 16);
					s->uv[k * 2 + 1] =
						(s16)(dx + 0x54 + c[k * 2 + 1] + 16);
				}
				g_shard_count++;
			}
		}
	}
	if (g_assemble) shatter_reverse();      /* img 0x28c7 */
}

/* ORIGIN img 0x2c05 Shatter_Step - one tick, every shard.
 *
 * 0x100 IS WHERE A SHARD BELONGS. InitShard starts every one at that
 * z, so in the ASSEMBLING direction a shard flies in, reaches its
 * home plane and stops dead. That single test is the whole arrival
 * mechanism; nothing else asks whether a shard has got home. */
void Lith_ShatterStep(void)
{
	s32 i, k;

	for (i = 0; i < g_shard_count; i++) {
		LithShard *s = &g_shard[i];

		for (k = 0; k < 3; k++)
			s->pos[k] = (s16)(s->pos[k] + s->vel[k]);
		s->vel[1] = (s16)(s->vel[1] + 1);       /* gravity */
		for (k = 0; k < 3; k++)
			s->rot[k] = (s16)(s->rot[k] + s->angvel[k]);
		if (s->pos[2] <= 0x100) {
			for (k = 0; k < 3; k++) {
				s->vel[k] = 0;
				s->angvel[k] = 0;
			}
		}
	}
}

/* ORIGIN img 0x2ac1 Shatter_Draw.
 *
 * THE X AND Y ANGLES ARE SWAPPED ON THE WAY IN. img 0x2adc stores
 * the shard's rot x into g_RotAngleY and img 0x2ae5 its rot y into
 * g_RotAngleX, so the tumble a shard shows is not the one its own
 * fields name. Loading them straight through turns every shard about
 * the wrong pair of axes.
 *
 * NINE SHIFTS, NOT THREE. Each of Vec_RotY, Vec_RotX and Vec_RotZ is
 * followed by a SAR 9 on ALL THREE components, so the corner is
 * brought back down between rotations rather than once at the end -
 * and the position is added at the same 9-bit scale.
 *
 * g_ShardsOnScreen IS THE TERMINATION TEST, counted here and read by
 * the caller: the effect runs until no projected corner lands inside
 * the screen. The two bounds are NOT tested alike - img 0x2b94 is a
 * JA, unsigned, so a negative x fails it, while img 0x2b99 is a JG,
 * signed, so a shard above the top edge still counts.
 *
 * THE NEAR CLAMP AT 0x3e8 is what stops a shard that has passed the
 * camera dividing by something near zero.
 *
 * THE FILL'S SPAN SAMPLES A SCREEN AT STRIDE 320, not a 256 by 256
 * map, and steps u and v BEFORE it fetches - subsystems/raster.txt
 * section 4.1. */
void Lith_ShatterDraw(u8 *dst, const u8 *snap)
{
	s32 i, k;

	g_on_screen = 0;
	for (i = 0; i < g_shard_count; i++) {
		LithShard *s = &g_shard[i];
		LithPoly p;

		for (k = 0; k < 3; k++) {
			s32 x = s->c[k * 2], y = s->c[k * 2 + 1], z = 0;
			s32 sx, sy;

			Lith_VecRotY(&x, &y, &z, s->rot[0]);    /* the swap */
			x >>= 9; y >>= 9; z >>= 9;
			Lith_VecRotX(&x, &y, &z, s->rot[1]);
			x >>= 9; y >>= 9; z >>= 9;
			Lith_VecRotZ(&x, &y, &z, s->rot[2]);
			x >>= 9; y >>= 9; z >>= 9;

			x += (s32)s->pos[0] << 9;
			y += (s32)s->pos[1] << 9;
			z += (s32)s->pos[2] << 9;
			if (z < 0x3e8) z = 0x3e8;               /* img 0x2b65 */

			sx = lith_idiv_shl8(x, z) + 0xa0;   /* img 0x2b6b */
			sy = lith_idiv_shl8(y, z) + 0x64;
			if ((u32)(u16)sx <= 0x140 && (s16)sy <= 0xc8)
				g_on_screen++;

			p.x[k] = (s16)sx;
			p.y[k] = (s16)sy;
			p.u[k] = s->uv[k * 2] >= 0x140 ? 0x13f : s->uv[k * 2];
			p.v[k] = s->uv[k * 2 + 1] >= 0xc8 ? 0xc7
			                                  : s->uv[k * 2 + 1];
		}
		Lith_PolyFillScreen(dst, snap, &p);
	}
}

/* ORIGIN img 0x2919 Scene_Shatter - the runner, and the reverse
 * glass transition into the battle.
 *
 * THE SAME CODE RUNS BOTH WAYS. Everything that makes this the
 * assemble rather than the break happened in Shatter_Build, which
 * played the explosion out and negated it; from here it is the
 * ordinary effect stepped forward.
 *
 * THE TWO DIRECTIONS END ON DIFFERENT TESTS, img 0x2939. The break
 * ends when nothing is left on screen; the assemble ends when the
 * FIRST shard reaches its home plane, read straight out of shard 0.
 *
 * IT ENDS BY SHOWING THE PICTURE WHOLE, img 0x295b. The assembled
 * shards are 140 triangles with seams between them; the scene blits
 * the original instead, so the seams never show. */
void Lith_ShatterSceneEnter(void)
{
	/* fade, upload, THEN source - ./shatter.txt 6.1 */
	Lith_PalFadeSet(-0x40, -0x40, 6, 6);    /* img 0x02ed */
	Lith_PalRefresh();                      /* img 0x02fb */
	Lith_PalSelect(LITH_PAL_1);             /* img 0x02fe */
	Lith_ShatterAssembleSet(1);             /* img 0x0305 */
	Lith_ShatterBuild();                    /* img 0x2919 */
}

/* ORIGIN img 0x644e Tick_Shatter - one step, then the common tail. */
void Lith_ShatterSceneTick(void)
{
	Lith_ShatterStep();
	Lith_TickDefault();
}

/* ORIGIN img 0x2923 to 0x2933 - clear to the fill colour and draw.
 *
 * THE GROUND IS g_PresentFill, img 0x2929, which this scene sets to
 * 0x80808080 - the base of the band the picture is drawn in, so the
 * shards land on grey rather than on black. */
void Lith_ShatterSceneFrame(u8 *fb, const u8 *pic)
{
	memset(fb, 0x80, PD_SCREEN_W * PD_SCREEN_H);
	Lith_ShatterDraw(fb, pic);              /* img 0x2926 */
}

/* ORIGIN img 0x2950 - shard 0's depth, read at 0x2ad2:0x4010. */
s32 Lith_ShatterSceneDone(void)
{
	return g_shard[0].pos[2] <= 0x100;
}
