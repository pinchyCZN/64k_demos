/* LITHIUM - the shot and the tower.
 *
 * ORIGIN img 0x21eb maybe_Scene_TowerFall and img 0x6756
 * Tick_TowerFall. docs/lithium/effects/towerfall.txt.
 *
 * SLOT 0x10 IS THE ROUND, NOT THE TOWER. The runner parks it at the
 * origin and it stays there until a recorded keystroke - command
 * 0x1a in Flight_Step - copies the firing craft's drawn position and
 * attitude into it and arms g_TowerFalling. From then the tick flies
 * it with Flight_StepRoll while winding its speed up, so it
 * accelerates away from the craft that launched it and rolls 0x40 a
 * tick on the way, trailing one weightless particle every eighth.
 *
 * THE TOWER IS SLOT 8 and it does not move until the THIRD hit.
 * g_TowerImpacts counts the bursts; at three the slot stops being a
 * model and becomes a debris emitter, swung around a fixed base by
 * its own rising pitch.
 *
 * THE IMPACT TEST IS A GRID CELL, img 0x6810 - the round's two
 * horizontal coordinates biased by 0x4000 and shifted down 15 must
 * equal 8 and 0xd exactly. Nothing measures a height and nothing
 * consults the tower, so the round bursts where the author put the
 * target. ./towerfall.txt section 4.
 */

#include <string.h>

#include "lithium.h"

/* The three meshes the loop draws: the craft at 0x72f8, the tower at
 * 0x4b7c and the round at 0x7272, which the script unpacks at img
 * 0x0339 before the scene is entered. */
static LithMesh g_mesh_craft, g_mesh_tower, g_mesh_shot;

/* g_FadeStepTick img 0x8fe6 and g_TowerImpacts img 0x8fcd. */
static s32 g_tick, g_impacts;

static LithSlot *const g_slot = g_lith_slot;

/* OURS - a signed shift on the word the shake is drawn from. */
static s32 tf_sar16(s32 v, s32 n)
{
	return (s32)((s16)(u16)v >> n);
}

/* ORIGIN img 0x21eb to 0x237b - the setup.
 *
 * THE DRAWN POSITION STARTS BEHIND THE FLOWN ONE. Slots 0, 4 and 0xc
 * get a flight_z well past their home_z - 0x55000 against 0x4e000 -
 * and the flight model eases home a thirty-second a tick, so the
 * scene opens dollying forward rather than cutting in square.
 *
 * THE PLANAR SHADOW PASS IS ON HERE, img 0x21eb setting 0x9025 to 1
 * where the battle cleared it, so the craft lays a shadow on the
 * terrain. subsystems/raster.txt 3.3. */
void Lith_TowerFallEnter(void)
{
	static const s32 track[3] = { 0xa28a, 0xa611, 0xa998 };
	static const s32 off[3] = { 0, 4, 0xc };
	static const s32 fx[3] = { 0x41000, 0x40000, 0x40000 };
	static const s32 fz[3] = { 0x55000, 0x55000, 0x54800 };
	static const s32 hz[3] = { 0x4e000, 0x4d000, 0x4e800 };
	s32 i;

	for (i = 0; i < 3; i++) {               /* img 0x21fe */
		LithSlot *o = &g_slot[LITH_SLOT(off[i])];

		Lith_ObjReset(o);
		o->flight_x = fx[i];
		o->flight_z = fz[i];
		o->home_x = fx[i];
		o->home_z = hz[i];
		o->track = (u32)track[i];
	}

	Lith_ObjReset(&g_slot[LITH_SLOT(8)]);   /* img 0x226d */
	g_slot[LITH_SLOT(8)].flight_x = 0x40000;
	g_slot[LITH_SLOT(8)].flight_z = 0x6c000;
	g_slot[LITH_SLOT(8)].home_x = 0x40000;
	g_slot[LITH_SLOT(8)].home_z = 0x6c000;
	g_slot[LITH_SLOT(8)].flight_y = 0x3800;
	g_slot[LITH_SLOT(8)].home_y = 0x3800;

	Lith_ObjReset(&g_slot[LITH_SLOT(0x10)]);        /* img 0x22e7 */
	g_slot[LITH_SLOT(0x10)].flight_x = 0;
	g_slot[LITH_SLOT(0x10)].flight_z = 0;
	g_slot[LITH_SLOT(0x10)].home_x = 0;
	g_slot[LITH_SLOT(0x10)].home_z = 0;

	Lith_MeshLoad(&g_mesh_craft, 0x72f8);   /* img 0x23ac */
	Lith_MeshLoad(&g_mesh_tower, LITH_MESH_TOWER);
	Lith_MeshLoad(&g_mesh_shot, LITH_MESH_SHOT);

	Lith_PalSelect(LITH_PAL_1);             /* img 0x2315 */
	Lith_TrailReset();                      /* img 0x2340 */
	Lith_MusicPosLatch();                   /* img 0x2353 */
	Lith_PalFadeSet(0, 0, 3, 3);            /* img 0x2357 */
	Lith_PalRefresh();                      /* img 0x2365 */
	Lith_SceneDoneClear();                  /* img 0x21f1 */

	g_tick = 0;
	g_impacts = 0;
	g_lith_tower_falling = 0;
}

/* ORIGIN img 0x6756 Tick_TowerFall.
 *
 * THE FADE IS CUED OFF A TRACK CURSOR, img 0x6770 - how far slot 4
 * has flown through its own recording, not a tick count - so the
 * scene starts going white at the same point in the flight however
 * fast the machine is. */
void Lith_TowerFallTick(void)
{
	static const s32 flown[3] = { 0, 4, 0xc };
	LithSlot *shot = &g_slot[LITH_SLOT(0x10)];
	LithSlot *tower = &g_slot[LITH_SLOT(8)];
	s32 i;

	g_tick = (g_tick + 1) & 0xff;           /* img 0x6756 */
	if (!(g_tick & 7) && Lith_PalFlash())
		Lith_PalFlashSet(Lith_PalFlash() - 1);

	if ((u32)(g_slot[LITH_SLOT(4)].track - 0xa611) >= 0x2f8) {
		Lith_PalFadeTo(0x100);          /* img 0x6783 */
		Lith_PalFadeShiftSet(0xa);
	}

	Lith_FlickerStep();                     /* img 0x6790 */
	Lith_RecordModeClear();                 /* img 0x6793 */
	for (i = 0; i < 3; i++) {               /* img 0x6799 */
		LithSlot *o = &g_slot[LITH_SLOT(flown[i])];

		Lith_TrackPlay(o);
		Lith_FlightStep(o, &g_slot[LITH_SLOT(4)]);
	}

	if (g_lith_tower_falling) {              /* img 0x67b3 */
		Lith_FlightStepRoll(shot);      /* img 0x67c0 */
		shot->speed++;                  /* img 0x67c3 */
		/* img 0x67c9 - A BYTE INCREMENT ON THE TRACK CURSOR. The
		 * round plays no recording; the low byte is reused as the
		 * eight-tick counter for its exhaust. */
		shot->track = (shot->track & ~0xffUL)
		              | ((shot->track + 1) & 0xff);
		if (!(shot->track & 7)) {
			LithTrail *t = Lith_TrailSpawn(shot);

			t->life = 0x32000L;     /* img 0x67d9 */
			t->vel[0] = 0;
			t->vel[1] = 0x80;
			t->vel[2] = 0;
			if (((shot->home_x + 0x4000) >> 15) == 8
			    && ((shot->home_z + 0x4000) >> 15) == 0xd) {
				Lith_TrailSpawnBurstBig(shot, 0x32);
				shot->home_z = 0;       /* img 0x6825 */
				g_lith_tower_falling = 0;
				g_impacts++;            /* img 0x683f */
			}
		}
	}

	Lith_FlareEnergyStep();                 /* img 0x6844 */
	Lith_TrailStep();                       /* img 0x6859 */

	if (g_impacts >= 3) {                   /* img 0x685c */
		LithTrail *t;
		s32 x = 0, y = 0x7480, z = 0;

		/* img 0x6866 - THE SPAWN POINT IS JITTERED AND THEN PUT
		 * BACK. The three random nudges move the tower slot only
		 * far enough for the particle taken from it to land
		 * somewhere new; everything below overwrites them. */
		tower->home_x += tf_sar16((s32)Lith_RndNextB(), 1);
		tower->home_z += tf_sar16((s32)Lith_RndNextB(), 1);
		tower->home_y += tf_sar16((s32)Lith_RndNextB(), 1);

		t = Lith_TrailSpawnBurstBig(tower, 1);  /* img 0x6893 */
		t->vel[1] = (s16)(t->vel[1] + 0x200);   /* img 0x689c */

		tower->pitch = (s32)(s16)(tower->pitch + 3);    /* 0x68a1 */
		tower->flight_x = 0x40000;              /* img 0x68a7 */
		tower->flight_z = 0x6c000;
		tower->home_x = 0x40000;
		tower->home_z = 0x6c000;
		tower->flight_y = Lith_TerrainHeightAtSlot(tower);
		tower->home_y = tower->flight_y;        /* img 0x68da */

		/* img 0x68df - AND SWUNG ROUND THE BASE. A vertical
		 * 0x7480 turned by the slot's own pitch and TWICE its
		 * roll, so the rising pitch above walks the emitter around
		 * the wreck instead of leaving it in one place. */
		Lith_VecRotate3_2(&x, &y, &z, tower->ang_x, 0,
		                  (s32)(s16)(tower->ang_z * 2));
		x >>= 6; y >>= 6; z >>= 6;
		x = -x + 0x40000;                       /* img 0x6917 */
		z = z + 0x6c000;
		tower->flight_x = x;
		tower->flight_z = z;
		tower->home_x = x;
		tower->home_z = z;
		tower->flight_y += y;                   /* img 0x693d */
		tower->home_y += y;
	}

	/* img 0x6949 - THE CUT, and it trades slot 0 with slot 0xc
	 * where the battle traded with 0x1c. This handler keeps the
	 * latch itself rather than leaving it to the common tail. */
	if (Lith_MusicPosChanged()) {
		Lith_MusicPosLatch();           /* img 0x6956 */
		Lith_SlotSwapState(&g_slot[LITH_SLOT(0)],
		                   &g_slot[LITH_SLOT(0xc)]);
	}
	Lith_TickDefault();                     /* img 0x6962 */
}

/* ORIGIN img 0x2381 to 0x2491 - the frame.
 *
 * THREE OBJECTS AND THEY DIFFER BY SCALE AS WELL AS MESH: the craft
 * at 0x10, the tower at 0xa and the round at 0x10 again, the last
 * the only one textured - 0x6ad2, the sphere map. */
void Lith_TowerFallFrame(u8 *fb, const u8 *tex, const u8 *pic,
                         LithScene *sc, LithProj *pv, LithSortEnt *sl)
{
	static const s32 drawn[3] = { 4, 8, 0x10 };
	s32 cp[3], ca[3], n, i;
	LithObj o;

	/* OURS, standing in for img 0x232d and the present-fill at img
	 * 0x247b, both of which leave 0x4ad2 at mid grey - the base of
	 * the band this scene draws in. effects/battle.txt section 1. */
	memset(fb, 0x80, PD_SCREEN_W * PD_SCREEN_H);

	Lith_CamLoad(&g_slot[LITH_SLOT(0)]);     /* img 0x2381 */
	for (i = 0; i < 3; i++)
		Lith_ObjLoadOrientB(&g_slot[LITH_SLOT(drawn[i])]);

	Lith_SceneReset(sc);                            /* img 0x2398 */
	Lith_TerrainBuild(sc, g_slot[0].ang_x, g_slot[0].ang_y,
	                  g_slot[0].pos_x >> 0xc, g_slot[0].pos_z >> 0xc,
	                  LITH_ATTR_GLOW);              /* img 0x23a2 */

	for (i = 0; i < 3; i++) {
		const LithMesh *m;

		if (i == 0)      m = &g_mesh_craft;
		else if (i == 1) m = &g_mesh_tower;
		else             m = &g_mesh_shot;
		Lith_ObjInit(&o);                       /* img 0x23cc */
		o.ang_x = g_slot[LITH_SLOT(drawn[i])].ang_x;
		o.ang_y = g_slot[LITH_SLOT(drawn[i])].ang_y;
		o.ang_z = g_slot[LITH_SLOT(drawn[i])].ang_z;
		o.pos_x = g_slot[LITH_SLOT(drawn[i])].pos_x;
		o.pos_y = g_slot[LITH_SLOT(drawn[i])].pos_y;
		o.pos_z = g_slot[LITH_SLOT(drawn[i])].pos_z;
		o.scale_shift = (i == 1) ? 0xa : 0x10;  /* img 0x23ea */
		o.mirror = 0;
		o.axis_swap = 1;
		o.rebase_x = g_slot[LITH_SLOT(0)].pos_x;
		o.rebase_z = g_slot[LITH_SLOT(0)].pos_z;
		Lith_ObjFaceAttr(i == 2 ? LITH_ATTR_SPHERE : 0);
		Lith_MeshTransform(sc, m, &o);
	}

	cp[0] = (g_slot[0].pos_x & 0xfff) >> 4;         /* img 0x2433 */
	cp[2] = ((-(g_slot[0].pos_z + 1)) & 0xfff) >> 4;
	g_slot[0].pos_y >>= 4;                          /* img 0x2460 */
	cp[1] = g_slot[0].pos_y;
	ca[0] = g_slot[0].ang_x;
	ca[1] = g_slot[0].ang_y;
	ca[2] = g_slot[0].ang_z;

	Lith_ShadowOrigin(&g_slot[LITH_SLOT(0)],
	                  &g_slot[LITH_SLOT(4)]);       /* img 0x335a */
	Lith_SceneProject(pv, sc, cp, ca);              /* img 0x2467 */
	n = Lith_SceneCullSort(sl, pv, sc, LITH_BATTLE_DEPTH_NEAR);
	for (i = 0; i < n; i++) {
		s32 f = sl[i].face, idx[3], k;
		LithPoly p;

		idx[0] = sc->f0[f]; idx[1] = sc->f1[f]; idx[2] = sc->f2[f];
		for (k = 0; k < 3; k++) {
			p.x[k] = pv[idx[k]].sx;
			p.y[k] = pv[idx[k]].sy;
			p.u[k] = pv[idx[k]].u;
			p.v[k] = pv[idx[k]].v;
		}
		Lith_PolyFill(fb, sc->attr[f] == LITH_ATTR_GLOW ? pic : tex,
		              &p, Lith_FaceBand(sc->attr[f]));

		/* img 0x342e - the craft's shadow, terrain faces only and
		 * the screen positions left standing. */
		if (sc->attr[f] == LITH_ATTR_GLOW
		    && Lith_ShadowFaceUV(&p, pv, idx,
		                         g_slot[LITH_SLOT(4)].ang_y))
			Lith_PolyFillShadow(fb, Lith_ShadowStencil(), &p);
	}

	Lith_SkySunAndStars(fb, ca[0], ca[1], ca[2]);   /* img 0x246d */
	Lith_FlareRender(fb);                           /* img 0x2470 */

	Lith_CamLoad(&g_slot[LITH_SLOT(0)]);     /* img 0x2473 */
	{
		s32 tp[3], ta[3];

		tp[0] = g_slot[0].pos_x;
		tp[1] = g_slot[0].pos_y;
		tp[2] = g_slot[0].pos_z;
		ta[0] = g_slot[0].ang_x;
		ta[1] = g_slot[0].ang_y;
		ta[2] = g_slot[0].ang_z;
		Lith_TrailDraw(fb, tp, ta);             /* img 0x2478 */
	}
}

/* ORIGIN img 0x248b - and nothing in the loop sets the flag. The
 * scene ends when a track issues the stop command, which reaches
 * g_SceneDone through Flight_Step. */
s32 Lith_TowerFallDone(void)
{
	return Lith_SceneDone();
}
