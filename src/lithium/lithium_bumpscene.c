/* LITHIUM - the bump scene's runner.
 *
 * ORIGIN img 0x0665 maybe_Scene_BumpShatter's setup, img 0x07f4 the
 * top of its frame loop and img 0x6a23 Tick_Dust. The effect itself
 * is lithium_bump.c and the particles lithium_dust.c;
 * docs/lithium/effects/bump.txt and effects/dust.txt.
 *
 * THE SCENE IS A THOUSAND PARTICLES SPELLING "VOTE". They stamp a
 * height field, the field is relaxed, and the field's slope is used
 * to displace where a picture of three glows is sampled - which is
 * the whole of the lighting.
 *
 * THE RUNNER HAS TWO PHASES AND ONE BODY, which is why they share a
 * file: img 0x08f8 breaks the surface on the rock's depth and the
 * same routine carries straight on into the tunnel, the flying glass
 * and the rock together. The rock's slot survives the crossing, so
 * the second phase picks up the spin and the approach the first was
 * driving. effects/shatter.txt and effects/tunnel.txt.
 */

#include <string.h>

#include "lithium.h"

/* 0x7ad2 the field, 0x5ad2 the picture, 0x2ad2:0x6700 the mask and
 * 0x2ad2:0x3820 the pool. The two segment-sized ones are whole 64 KB
 * for the reason lithium_bump.c gives. */
static u8 g_field[LITH_SEG_BYTES];
static u8 g_pic[LITH_SEG_BYTES];
static u8 g_mask[LITH_DUST_MASK_BYTES];
static u8 g_pool[LITH_DUST_COUNT * LITH_DUST_REC];

/* g_RockSlideX img 0x8ff9, g_DustPhase img 0x8ff7, g_BumpInterlace
 * img 0x8ff8 and g_DustJitterShift img 0x8fea. */
static s32 g_slide, g_phase, g_interlace, g_jitter;

/* Vec_Ease2D's four words at img 0x8fef and 0x8ff3, the velocity
 * pair at img 0x8feb and the countdown at img 0x8fe3. */
static s32 g_light_vx, g_light_vy;
static s32 g_light_lx, g_light_ly;
static s32 g_light_x, g_light_y;

/* Slot 4 - the chrome rock, the same 0x41ea mesh the opening flies.
 * The setup points g_MeshSrcOff at it at img 0x07a9.
 *
 * TWO PIECES, AS THE ORIGINAL HAS THEM. Obj_Select at img 0x4ffd
 * copies only the three ANG accumulators and the position out of the
 * slot bank into the object Mesh_Transform reads; the pitch, yaw and
 * roll that drive the wobble never reach it. */
static LithObj g_rock;
static s32 g_rk_pitch, g_rk_yaw, g_rk_roll;
static s32 g_rk_home_z, g_rk_flight_z;

/* OURS - so the host can see which half of the scene is running. */
s32 Lith_BumpInterlace(void) { return g_interlace; }

/* Segment 0x5ad2, which outlives this scene: the shatter samples
 * it and the battle's terrain is textured from it.
 * Tex_SnapshotFrame overwrites it between the two, img 0x2496. */
u8 *Lith_PicBuffer(void) { return g_pic; }

/* ORIGIN img 0x6008 Obj_WobblePos - the rock's position, which is
 * not integrated but LOOKED UP.
 *
 * THREE SINE READS OFF ITS OWN ATTITUDE. The pitch, yaw and roll
 * accumulators Tick_Dust steps every tick index the sine table, so
 * the rock traces a Lissajous figure rather than flying a path -
 * and because the table is read with the same 0x1ffc mask as every
 * rotation, the three axes drift against one another.
 *
 * X CARRIES g_RockSlideX, img 0x601b, AND THAT IS THE ENTRANCE. The
 * counter starts at the image's 0x2ee0 and Tick_Dust drains 0x20 a
 * tick, so the rock begins far off the right and slides in over the
 * 250 ticks that also decide the interlace crossover - one counter,
 * both jobs. Z is the only axis that accumulates, through home_z. */
static void bump_wobble_pos(void)
{
	g_rock.pos_x = (Lith_Sin(g_rk_pitch) >> 2) + g_slide;
	g_rock.pos_y = (Lith_Sin(g_rk_yaw) >> 2);
	g_rock.pos_z = (Lith_Sin(g_rk_roll) >> 1) + 0x1f40 + g_rk_home_z;
}

/* OURS - a signed shift on a 16-bit field, which is what SAR does to
 * the word registers these accumulators live in. */
static s32 bump_sar16(s32 v, s32 n)
{
	return (s32)((s16)(u16)v >> n);
}

/* ORIGIN img 0x0c44 Bump_RandomiseLight.
 *
 * TICK_DEFAULT OWNS THE COUNTDOWN, NOT THIS. One counter does both
 * jobs - the white flash and this re-aim - and that couples them: a
 * beat resets it WITHOUT re-aiming, img 0x6ccb, so beats arriving
 * faster than 24 ticks hold the light still while the screen keeps
 * flashing. Keeping a second counter here would uncouple the two.
 * effects/bump.txt section 6. */
void Lith_BumpRandomiseLight(void)
{
	g_light_vx = bump_sar16((s32)Lith_RndNextB(), 5);
	g_light_vy = bump_sar16((s32)Lith_RndNextB(), 5);
}

/* ORIGIN img 0x6244 Vec_Ease2D - what actually moves the light.
 *
 * THE LIGHT IS WHAT LIGHTS THE FLAT GROUND. Bump_Render adds this
 * pair to every lookup, so a pixel whose slope is zero samples the
 * picture AT THE LIGHT rather than at its own origin - and since the
 * picture is only about half lit, a light off the glows leaves the
 * surface black, which is what the wander does much of the time.
 *
 * ALL FOUR ACCUMULATORS ARE 16-BIT AND WRAP - it adds with `add word
 * ptr` and Bump_Render reads the result with SHR, not SAR, so the
 * light sweeps the picture cyclically instead of drifting away. */
static void bump_light_tick(void)
{
	g_light_lx = (g_light_lx + g_light_vx) & 0xffff;
	g_light_ly = (g_light_ly + g_light_vy) & 0xffff;
	g_light_x = (g_light_x
	             + bump_sar16(g_light_lx - g_light_x, 3)) & 0xffff;
	g_light_y = (g_light_y
	             + bump_sar16(g_light_ly - g_light_y, 3)) & 0xffff;
}

/* ORIGIN img 0x0665 to 0x07ed - the setup.
 *
 * THE MASK IS DRAWN, NOT STORED. img 0x0686 clears 6,400 bytes and
 * img 0x0698 calls the CREDIT ROLL'S text routine to write "VOTE"
 * into them at full strength. effects/bump.txt section 7.
 *
 * THREE VALUES ARE NOT SET HERE, and that is deliberate: nothing in
 * the demo initialises g_RockSlideX, g_DustPhase or
 * g_DustJitterShift, so each holds what the image carries - 0x2ee0,
 * 0x0b and 1. The slide's drain turns the first into the 250 ticks
 * before the interlace crosses over. Seeding any of them from a
 * literal would be inventing a number the image already supplies,
 * and a jitter of 0 rather than 1 doubles the cloud's wander.
 *
 * THE SETUP SPLITS ACROSS A MUSIC GATE. img 0x0762 is
 * Demo_RunUntil(0x18): the buffers, the dust and the rock are
 * seeded here, the palette and the fade in Lith_BumpBegin, and
 * Tick_Dust waits 90 ticks - subsystems/timeline.txt 2.1. */
void Lith_BumpEnter(void)
{
	memset(g_field, 0, sizeof g_field);     /* img 0x066c */
	memset(g_mask, 0, sizeof g_mask);       /* img 0x0686 */
	Lith_TextDrawStringAt(g_mask, 0, LITH_STR_VOTE, 0);

	Lith_MathBuildSquareTable();
	Lith_TexBuildThreeGlows(g_pic);         /* img 0x029d */

	g_slide = (s32)RD_U32(g_lith_img + LITH_BUMP_SLIDE_SEED);
	g_phase = g_lith_img[LITH_BUMP_PHASE_SEED];
	g_jitter = g_lith_img[LITH_BUMP_JITTER_SEED];
	Lith_BumpFieldToggleReset();

	Lith_RndBReset();
	g_light_vx = g_light_vy = 0;
	g_light_lx = g_light_ly = 0;
	g_light_x = g_light_y = 0;

	Lith_DustResetFromPoint(g_pool);

	/* img 0x0704 to 0x0748 - slot 4's seeding. Only z is given
	 * anything: the rock starts 0x1b58 out and approaches at a
	 * flight z that begins NEGATIVE and is added to itself every
	 * tick, so the approach accelerates. */
	Lith_ObjInit(&g_rock);
	g_rock.pos_z = 0x1b58;                      /* img 0x072a */
	g_rk_pitch = g_rk_yaw = g_rk_roll = 0;
	g_rk_home_z = 0;
	g_rk_flight_z = -1800;                      /* img 0x0734 */
}

/* ORIGIN img 0x0765 to 0x07ed - the half of the setup that waits on
 * the music, and where Tick_Dust takes over.
 *
 * NO STORED PALETTE IS SELECTED: Pal_Animate GENERATES the ramps
 * from the three accumulators, and the shift that steepens them is
 * set after the first animate. The latch at img 0x07a1 is after the
 * gate, so the refill does not fire on the first dust tick. */
void Lith_BumpBegin(void)
{
	Lith_PalPhaseSeed(0xf8a3, 0xf6f0, 0xf41b);
	Lith_PalAnimate(Lith_PalStore());            /* img 0x0788 */
	Lith_PalRampShift(2);                        /* img 0x078e */
	Lith_PalFlashSet(0x80);                      /* img 0x0794 */
	Lith_MusicPosLatch();                        /* img 0x07a1 */
	g_rock.scale_shift = LITH_SCALE_IDENTITY;    /* img 0x07ba */
	g_rock.mirror = 0;                           /* img 0x07c0 */
	g_interlace = 0;                             /* img 0x07cd */
	Lith_PalFadeSet(0x80, -0xc0, 6, 6);          /* img 0x07d3 */
}

/* ORIGIN img 0x6a23 Tick_Dust. */
void Lith_BumpTick(void)
{
	s32 beat;

	bump_wobble_pos();                      /* img 0x6a26 */
	g_rock.ang_x += 0x24;                   /* img 0x6a29 */
	g_rock.ang_y += 0x0f;
	g_rock.ang_z += 0x2b;
	g_rk_pitch += 0x45;
	g_rk_yaw += 0x24;
	g_rk_roll += 0x36;

	if (g_slide > 0) {                      /* img 0x6a4d */
		g_slide -= 0x20;
		if (g_slide < 0) g_slide = 0;
	}
	/* img 0x6a64 is a JLE PAST THE FOLD, so the rock does not
	 * approach at all while its flight z is negative - 450 ticks of
	 * nothing, and then an acceleration. Folding unconditionally
	 * drives it through the surface long before the shatter. */
	g_rk_flight_z += 4;                     /* img 0x6a5d */
	if (g_rk_flight_z > 0)
		g_rk_home_z += g_rk_flight_z;

	beat = Lith_MusicPosChanged();
	bump_light_tick();                      /* img 0x6a72 */
	Lith_DustTick(g_pool, g_jitter, 0);     /* img 0x6a7a */
	if (beat)                               /* img 0x6ad6 */
		Lith_DustModeStep(g_pool, g_mask, &g_phase, &g_jitter);
	if (g_phase == 7) g_phase = 1;          /* img 0x6b0f, EVERY tick */

	/* THE SCENE CYCLES ITS OWN PALETTE. Tick_Default does not call
	 * Pal_Animate - only a scene's handler does - and this one
	 * drives the three accumulators hard before each call, img
	 * 0x6b1d, which is what makes the surface's colours run. */
	Lith_PalPhaseAdd(0x73, 0x50, 0x0b);
	Lith_PalAnimate(Lith_PalStore());       /* img 0x6b2f */

	/* THE COMMON TAIL OF EVERY HANDLER, img 0x6b32: it eases the
	 * palette, strobes the flash, re-aims the light and latches the
	 * music position - so nothing above may latch it. */
	Lith_TickDefault();
}

/* ORIGIN img 0x07f4 to 0x08ed - the frame.
 *
 * THE CROSSOVER IS TESTED AT THE TOP, img 0x07f4, so the frame that
 * crosses already stamps with the strong brush and relaxes every
 * row. After it the render writes a back buffer in full; before it,
 * the screen in alternating fields - which is the shimmer the scene
 * opens with. effects/bump.txt section 5. */
void Lith_BumpFrame(u8 *fb, const LithMesh *rock, const u8 *tex,
                    LithScene *sc, LithProj *pv, LithSortEnt *sl)
{
	if (g_slide < 0xfa0) g_interlace = 1;   /* img 0x07f4 */

	Lith_DustStamp(g_field, g_pool, Lith_DustBrush(g_interlace));
	Lith_HeightRelax(g_field, g_interlace, Lith_BumpFieldToggle());
	Lith_BumpRender(g_field, g_pic, fb,
	                g_light_x, g_light_y, g_interlace);

	/* img 0x08f0 - the rock only exists on the full-rate path, and
	 * it CANNOT reach the height field: that is segment 0x7ad2 and
	 * this draws into 0x4ad2, which the render has already resolved
	 * - so the rock lands ON the surface and nothing it draws feeds
	 * back into the relaxation. */
	if (!g_interlace) return;

	Lith_SceneReset(sc);                    /* img 0x0904 */
	Lith_ObjFaceAttr(LITH_ATTR_SPHERE);     /* img 0x07c6 */
	Lith_MeshTransform(sc, rock, &g_rock);  /* img 0x0914 */
	{
		s32 cp[3], ca[3];

		cp[0] = cp[1] = cp[2] = 0;
		ca[0] = ca[1] = ca[2] = 0;
		Lith_SceneDraw(fb, tex, sc, pv, sl, cp, ca, 0);
	}

	/* img 0x0922 and 0x092d - the two rows Bump_Render never writes
	 * are cleared every frame, so nothing the previous scene left
	 * shows along the top and bottom edges. */
	memset(fb, 0, PD_SCREEN_W);
	memset(fb + 199 * PD_SCREEN_W, 0, PD_SCREEN_W);
}

/* ------------------------------------------- the shatter phase */

/* ORIGIN img 0x08f8 - the depth the surface breaks at.
 *
 * IT IS ONLY ASKED ON THE FULL-RATE PATH. img 0x08f6 jumps past this
 * while the scene is still interlaced, so the shatter cannot happen
 * before the crossover however deep the rock is. */
s32 Lith_BumpDone(void)
{
	return g_interlace && g_rock.pos_z >= 0x88b8;
}

/* ORIGIN img 0x27e3 Tex_SnapshotFrom - the copy into 0x5ad2 that
 * gives the shards their picture.
 *
 * THERE IS NO SEPARATE SNAPSHOT BUFFER. 0x5ad2 IS g_pic, the
 * three-glow picture the scene opened with, and this copies the
 * back buffer over its first 64,000 bytes - so the glow picture's
 * last 1,536 survive in the tail, where a wrapped texture index
 * lands.
 *
 * IT ALSO AIMS THE FADE, img 0x27ea, and that is the whole of the
 * next phase's lighting - a copy routine carrying a palette write
 * reads as incidental and is not. effects/bump.txt section 5.1. */
static void tex_snapshot_from(u8 *dst, const u8 *src)
{
	Lith_PalFadeTo(0);                      /* img 0x27ea */
	memcpy(dst, src, PD_SCREEN_W * PD_SCREEN_H);
}

/* ORIGIN img 0x0942 to 0x09ce - the handover.
 *
 * THE SHARDS CARRY THE FRAME THAT WAS ON SCREEN. Tex_SnapshotFrom at
 * img 0x0945 copies the back buffer into the segment the shatter's
 * span routine samples, so what breaks is the bump surface with the
 * rock already on it - subsystems/raster.txt section 4.1.
 *
 * THE ROCK IS NOT RESET. Only slot 0 is, img 0x0964, so the rock
 * keeps the spin and the depth the first phase left it at and simply
 * starts being flown by a different handler.
 */
void Lith_ShatterEnter(const u8 *fb)
{
	tex_snapshot_from(g_pic, fb);           /* img 0x0945 */
	Lith_ShatterAssembleSet(0);             /* img 0x0948 */
	Lith_ShatterBuild();                    /* img 0x094e */
	g_interlace = 1;                        /* img 0x0951 */

	Lith_TunnelCamInit();                   /* img 0x0964 to 0x09ce */
	Lith_PalFlashSet(0);                    /* img 0x09b0 */
	Lith_PalRampShift(2);                   /* img 0x09b7 */
}

/* ORIGIN img 0x6b35 Tick_Rock, WHICH FALLS THROUGH into img 0x6b7f
 * Tick_Tunnel - there is no RET between them, so this phase's
 * handler is both bodies. effects/tunnel.txt section 5.
 *
 * THE ROCK NO LONGER ACCELERATES, IT EASES. The first phase added a
 * growing flight z to the depth every tick; this one drains the
 * depth toward -0x4e20 by a 256th, img 0x6b5f, so the rock slows as
 * it leaves rather than tearing away.
 *
 * THE SLIDE REVERSES AT A SIXTEENTH OF THE RATE, img 0x6b75 - four a
 * tick against the first phase's 0x20 - so the rock drifts back out
 * to the right over the whole of the tunnel.
 *
 * THE CAMERA WALKS THE TUNNEL'S OWN CURVE. The velocity at img
 * 0x6c28 is Tunnel_StepPath's arithmetic exactly - the same
 * (0, 0, 0x100), the same three rotations, the same shifts - on the
 * second angle set, recomputed once every sixteen ticks while the
 * ring window advances by one. Sixteen ticks of motion divided by
 * sixteen is one ring, and that is what keeps the eye in the bore. */
void Lith_ShatterTick(void)
{
	bump_wobble_pos();                      /* img 0x6b38 */
	g_rock.ang_x += 0x24;                   /* img 0x6b3b */
	g_rock.ang_y += 0x0f;
	g_rock.ang_z += 0x2b;
	g_rk_pitch += 0x45;
	g_rk_yaw += 0x24;
	g_rk_roll += 0x36;
	g_rk_home_z -= (g_rk_home_z + 0x4e20) >> 8;
	g_slide -= 4;
	Lith_ShatterStep();                     /* img 0x6b7c */

	Lith_TunnelTick();                      /* img 0x6b7f */

	Lith_TickDefault();                     /* img 0x6c8e */
}

/* ORIGIN img 0x09d5 to 0x0ac2 - the frame, which is three draws into
 * one buffer with no clear between them.
 *
 * THE POOL IS RESET TWICE, img 0x09dd and img 0x0a9f, so the tunnel
 * and the rock are separate scenes that happen to share a frame.
 * Tunnel_Build's face indices are not rebased, so it has to go
 * first - see Lith_TunnelBuild.
 *
 * THE CAMERA SPLITS COARSE FROM FINE. The eye's position reaches the
 * tunnel TWICE: whole rings through g_TunnelFirstRing, which the
 * tick advances, and the remainder through the masked and shifted
 * pair here. The mask keeps only what is left inside one ring and
 * the shift is the same sixteen the window moves on, which is the
 * whole of the lock between them - effects/tunnel.txt section 6.1.
 *
 * THE SHARDS GO ON TOP OF THE TUNNEL AND UNDER THE ROCK. */
void Lith_ShatterFrame(u8 *fb, const LithMesh *rock, const u8 *tex,
                       LithScene *sc, LithProj *pv, LithSortEnt *sl)
{
	s32 cp[3], ca[3];

	Lith_TunnelDraw(fb, tex, sc, pv, sl);   /* img 0x09da to 0x0a28 */
	Lith_ShatterDraw(fb, g_pic);            /* img 0x0a2b */
	Lith_TunnelCamClear();                  /* img 0x0a2e to 0x0a6e */

	Lith_SceneReset(sc);                    /* img 0x0a9f */
	g_rock.scale_shift = 0x10;              /* img 0x0a8c */
	g_rock.mirror = 0;                      /* img 0x0a92 */
	Lith_ObjFaceAttr(LITH_ATTR_SPHERE);     /* img 0x0a98 */
	Lith_MeshTransform(sc, rock, &g_rock);  /* img 0x0aa9 */
	cp[0] = 0;
	cp[1] = 0;
	cp[2] = 0;
	ca[0] = 0;
	ca[1] = 0;
	ca[2] = 0;
	Lith_SceneDraw(fb, tex, sc, pv, sl, cp, ca,
	               LITH_TUNNEL_DEPTH_NEAR);
}

/* ORIGIN img 0x0ac2 - and it is the ROCK that ends the phase, not
 * the shards. g_ShardsOnScreen is what ends the other use of the
 * effect, img 0x2926; here the loop runs until the rock has eased
 * past -0xfa0 on its way out, so the glass is long gone before the
 * tunnel is. */
s32 Lith_ShatterDone(void)
{
	return g_rk_home_z < -0xfa0;
}
