/* LITHIUM - the battle over the surface.
 *
 * ORIGIN img 0x1c4f maybe_Scene_Battle, img 0x660f Tick_Battle, img
 * 0x61de Flight_StepRoll and img 0x60eb Slot_SwapState.
 * docs/lithium/effects/battle.txt.
 *
 * FOURTEEN SLOTS, EIGHT OF THEM FLYING A RECORDING. The tracks are
 * laid end to end in the image, 300 entries of three bytes each, and
 * ONE OF THEM IS THE SCENE'S CLOCK - only track 0 carries a stop
 * command, three bytes past where the other seven end.
 *
 * THE OTHER SIX SLOTS ARE THE BOLTS and they fly themselves: no
 * track, no recorded position, just a heading and a constant roll.
 *
 * TWO SLOTS ARE NEVER DRAWN. 0 and 0x1c carry tracks and are stepped
 * like everything else, but no mesh points at them - they are camera
 * rigs, which is why exchanging them on the music reads as a cut.
 */

#include <string.h>

#include "lithium.h"

/* The fourteen, at the byte offsets the original indexes them by -
 * so g_slot[LITH_SLOT(0x14)] is what SI = 0x14 reaches. */
static LithSlot *const g_slot = g_lith_slot;

/* g_ProjTick img 0x8fc6 and g_ProjNextSlot img 0x8fc4. */
static s32 g_proj_tick, g_proj_next;

/* The near row, the far row and the bolt - img 0x20b1, 0x20f0 and
 * img 0x2123. The far row is the flyover's own ship. */
static LithMesh g_mesh_near, g_mesh_far, g_mesh_bolt;

LithSlot *Lith_BattleSlots(void) { return g_slot; }

/* ORIGIN img 0x61de Flight_StepRoll - the bolts.
 *
 * IT IS Flight_Step WITH THE MODEL TAKEN OUT. There is no throttle,
 * no attitude easing and no track: the speed is rotated by the pitch
 * and then by the yaw, added to the position, and the roll is
 * advanced a fixed 0x40. A bolt cannot turn, and nothing it hits
 * knows about it.
 *
 * THE TWO ROTATIONS ARE SEPARATE CALLS, img 0x6201 and 0x621c, each
 * with the other two angles zeroed - not one call with all three.
 * Vec_Rotate3_2 returns the vector times 64 and these take the shift
 * of 0xb once at the end, so the pair is a single rotation at a
 * different scale rather than two chained ones. */
void Lith_FlightStepRoll(LithSlot *o)
{
	s32 x = 0, y = 0, z = o->speed;

	Lith_VecRotate3_2(&x, &y, &z, o->pitch, 0, 0);
	Lith_VecRotate3_2(&x, &y, &z, 0, o->yaw, 0);
	x >>= 0xb;
	y >>= 0xb;
	z >>= 0xb;
	o->home_x += x;                         /* img 0x622b */
	o->home_y += y;
	o->home_z += z;
	o->roll = (s32)(s16)(o->roll + 0x40);   /* img 0x623d */
}

/* ORIGIN img 0x60eb Slot_SwapState - an exchange, not a copy.
 *
 * IT SWAPS THE TRACK AND THE TRACK CURSOR TOO, img 0x6151 and
 * 0x615d, so the two rigs trade recordings as well as positions and
 * carry on from where the other was. That is what makes the cut land
 * somewhere plausible instead of jumping.
 *
 * THE DRAWN POSITION IS NOT SWAPPED. g_SlotPos* is recomputed from
 * home by Obj_LoadOrient every frame, so exchanging it would be
 * undone before anything read it. Nor is g_SlotFiring. */
void Lith_SlotSwapState(LithSlot *a, LithSlot *b)
{
#define SWAP(f) do { s32 t_ = a->f; a->f = b->f; b->f = t_; } while (0)
	SWAP(flight_x); SWAP(flight_y); SWAP(flight_z);
	SWAP(home_x);   SWAP(home_y);   SWAP(home_z);
	SWAP(pitch);    SWAP(yaw);      SWAP(roll);
	SWAP(a_x);      SWAP(a_y);      SWAP(a_z);
	SWAP(roll_rest);
	SWAP(pitch_acc); SWAP(yaw_acc);
	SWAP(command);  SWAP(throttle); SWAP(speed);
	SWAP(track_tick);
#undef SWAP
	{ u32 t = a->track; a->track = b->track; b->track = t; }
}

/* ORIGIN img 0x660f Tick_Battle.
 *
 * THE LAUNCHER IS A ROUND ROBIN OVER A RANDOM SOURCE, img 0x66aa.
 * The destination cycles through the six bolt slots; the source is a
 * random one of six craft. There is no ownership and no aiming - a
 * bolt starts wherever a craft happens to be, pointing where it
 * points, and nothing tracks whose it was.
 *
 * TWO IN EIGHT LAUNCHES ARE THROWN AWAY. Rnd_NextB & 7 gives 0 to 7
 * and 6 and 7 are skipped, so a quarter of the opportunities do
 * nothing - masking to 8 and rejecting two is cheaper than a modulo.
 *
 * THE FIRING TEST KEEPS THE TWO WEAPONS APART, img 0x66d7. A craft
 * whose recorded track has fired - which is what fills the plasma
 * ring, effects/trail.txt section 5 - does not also launch a mesh
 * bolt, so the two systems never run off one craft at once.
 *
 * g_RecordMode IS CLEARED EVERY TICK HERE, img 0x6612, where the
 * tunnel's handler uses the same byte as its sixteen-tick counter. */
void Lith_BattleTick(void)
{
	static const s32 flown[8] = { 0, 4, 8, 0xc, 0x10, 0x14, 0x18, 0x1c };
	s32 i;

	Lith_FlickerStep();                     /* img 0x660f */
	Lith_RecordModeClear();                 /* img 0x6612 */
	for (i = 0; i < 8; i++) {
		LithSlot *o = &g_slot[LITH_SLOT(flown[i])];

		Lith_TrackPlay(o);                      /* img 0x661a */
		Lith_FlightStep(o, &g_slot[LITH_SLOT(4)]);
	}
	for (i = 0x20; i <= 0x34; i += 4)       /* img 0x665f */
		Lith_FlightStepRoll(&g_slot[LITH_SLOT(i)]);

	Lith_FlareEnergyStep();                 /* img 0x6683 */
	Lith_TrailStep();                       /* img 0x6698 */

	g_proj_tick = (g_proj_tick + 1) & 0xff; /* img 0x669b */
	if (!(g_proj_tick & 3)) {
		s32 di = g_proj_next, si;

		g_proj_next += 4;                       /* img 0x66af */
		if (g_proj_next == 0x38) g_proj_next = 0x20;
		si = Lith_RndNextB() & 7;               /* img 0x66c4 */
		if (si < 6) {
			si = si * 4 + 4;                /* img 0x66d1 */
			if (g_slot[LITH_SLOT(si)].firing != 1) {
				LithSlot *s = &g_slot[LITH_SLOT(si)];
				LithSlot *d = &g_slot[LITH_SLOT(di)];

				d->home_x = s->home_x;  /* img 0x66df */
				d->home_z = s->home_z;
				d->home_y = s->home_y;
				d->pitch = s->pitch;    /* img 0x6703 */
				d->yaw = s->yaw;
				d->roll = s->roll;
				d->speed = 0x1e0;       /* img 0x6721 */
			}
		}
	}

	if (Lith_MusicPosChanged()) {           /* img 0x672b */
		Lith_PalFadeShiftReloadSet(3);  /* img 0x6738 */
		Lith_PalFadeShiftSet(3);
		Lith_PalFlashSet(0x80);
		Lith_SlotSwapState(&g_slot[LITH_SLOT(0)],
		                   &g_slot[LITH_SLOT(0x1c)]);
	}
	Lith_TickDefault();                     /* img 0x6753 */
}

/* ORIGIN img 0x1c4f to 0x1fe3 - the slot bank, and nothing else yet.
 *
 * THREE FORMATION ROWS AND TWO SINGLETONS - effects/battle.txt
 * section 2. The two singletons are the camera rigs and the only
 * slots never drawn.
 *
 * THE INITIAL YAW IS WHAT TURNS THE SECOND ROW TO FACE THE FIRST.
 * The far row and both rigs get 0x1000 where slot 0 gets 0x800, so
 * the formations meet head on rather than flying parallel.
 *
 * THE TRAIL RING IS CLEARED HERE, img 0x1fdb, 0x580 bytes of it -
 * which is the 64 records effects/trail.txt describes, and the only
 * place in the scene that touches them before a shot lands.
 *
 * [open] THE SECOND PASS. img 0x1fed branches on g_BattleSecondPass
 * and only the first entry is here; the second skips the fade and
 * installs the handler directly. */
void Lith_BattleEnter(void)
{
	static const s32 track[8] = {
		0x8667, 0x89ee, 0x8d72, 0x90f6,
		0x947a, 0x97fe, 0x9b82, 0x9f06
	};
	static const s32 posx[8] = {
		0x46000, 0x3c000, 0x40000, 0x44000,
		0x3c000, 0x40000, 0x44000, 0x40000
	};
	static const s32 posz[8] = {
		0x42000, 0x40000, 0x40000, 0x40000,
		0x50000, 0x50000, 0x50000, 0x59000
	};
	s32 i;

	for (i = 0; i < LITH_BATTLE_SLOTS; i++)
		Lith_ObjReset(&g_slot[i]);

	for (i = 0; i < 8; i++) {
		LithSlot *o = &g_slot[i];

		o->track = (u32)track[i];       /* img 0x1c61 */
		o->flight_x = posx[i];
		o->flight_z = posz[i];
		o->home_x = posx[i];
		o->home_z = posz[i];
		if (i >= 4) {                   /* img 0x1d9c */
			o->yaw_acc = 0x1000;
			o->yaw = 0x1000;
		}
	}
	g_slot[LITH_SLOT(0)].yaw_acc = 0x800;   /* img 0x1c93 */
	g_slot[LITH_SLOT(0)].yaw = 0x800;
	g_slot[LITH_SLOT(0)].a_y = 0x800;
	g_slot[LITH_SLOT(0x1c)].a_y = 0x1000;   /* img 0x1e7c */
	g_slot[LITH_SLOT(0x1c)].flight_y = 0x1000;
	g_slot[LITH_SLOT(0x1c)].home_y = 0x1000;

	Lith_MeshLoad(&g_mesh_near, 0x72f8);    /* img 0x20b1 */
	Lith_MeshLoad(&g_mesh_far, 0x4000);     /* img 0x20f0 */
	Lith_MeshLoad(&g_mesh_bolt, 0x724a);    /* img 0x2123 */
	Lith_TrailReset();                      /* img 0x1fdb */
	g_proj_tick = 0;
	g_proj_next = 0x20;

	/* img 0x1fe9 - PRIME THE CUT LATCH. ./battle.txt section 5. */
	Lith_MusicPosLatch();

	/* img 0x1fab - THE BATTLE USES A STORED PALETTE where the bump
	 * scene generated its ramps. Pal_Animate never runs here, so the
	 * colour comes entirely from g_Pal1 and the fade over it.
	 * g_EnvMapAngle, img 0x1fbc, is 0 - which Lith_ObjInit already
	 * gives every object this scene builds. */
	Lith_PalSelect(LITH_PAL_1);

	/* img 0x201d - it opens DARK and comes up: -0xa0 toward 0 at a
	 * shift of 6, with 3 armed for whatever fades next. */
	Lith_PalFadeSet(-0xa0, 0, 6, 3);
	Lith_PalFlashCountdownSet(0xff);        /* img 0x204a */
	Lith_SceneDoneClear();                  /* img 0x2044 */
}

/* ORIGIN img 0x4ffd Obj_Select - the three live angles and the
 * position out of the slot bank into what Mesh_Transform reads. The
 * attitude that FLIES a craft, set B, does not come this way: it
 * reaches the live angles through Obj_LoadOrientB first. */
static void obj_select(LithObj *o, const LithSlot *s)
{
	Lith_ObjInit(o);
	o->ang_x = s->ang_x;
	o->ang_y = s->ang_y;
	o->ang_z = s->ang_z;
	o->pos_x = s->pos_x;
	o->pos_z = s->pos_z;
	o->pos_y = s->pos_y;
}

/* ORIGIN img 0x2050 to 0x21e6 - the frame.
 *
 * TWELVE RENDERS, NOT NINE, in three groups of one mesh each: the
 * near row at 0x72f8, the far row at 0x4000 - the flyover's own
 * ship - and six bolts at 0x724a, which is FOUR VERTICES AND FOUR
 * FACES. That tetrahedron is the entire model for every shot in the
 * scene.
 *
 * THE THREE GROUPS DIFFER BY ATTRIBUTE, not by texture: the near row
 * takes 0, the far row 0x6ad2 and the bolts 1 - so one sphere map
 * becomes two ship colours and a bolt colour through the band alone.
 * subsystems/raster.txt section 4.
 *
 * SLOT 0x1c IS NOT EVEN ORIENTED HERE. It carries a track and is
 * flown every tick, but nothing loads it and nothing draws it - it
 * is the spare camera rig the music cut swaps in.
 *
 * THE CAMERA IS RELOADED BEFORE THE TRAIL, img 0x21c8, because the
 * twelve Obj_Selects above have overwritten the live angles with
 * each craft's in turn. Drawing the blobs off the last bolt's
 * attitude puts them somewhere else entirely. */
void Lith_BattleFrame(u8 *fb, const u8 *tex, const u8 *pic,
                      LithScene *sc, LithProj *pv, LithSortEnt *sl)
{
	static const s32 drawn[12] = {
		4, 8, 0xc, 0x10, 0x14, 0x18,
		0x20, 0x24, 0x28, 0x2c, 0x30, 0x34
	};
	s32 cp[3], ca[3], n, i;
	LithObj o;

	/* OURS, standing in for img 0x21da. Screen_PresentDirty blits
	 * the dirty box and then erases that box in the back buffer to
	 * g_PresentFill, which this scene sets to 0x80808080 - so every
	 * frame starts from mid grey. We present whole frames and keep
	 * no box, so the erase is a full fill. IT IS NOT COSMETIC: 0x80
	 * is the base of the palette band everything here is drawn in,
	 * and starting from 0 puts the whole scene in the wrong band.
	 * effects/battle.txt section 1. */
	memset(fb, 0x80, PD_SCREEN_W * PD_SCREEN_H);

	Lith_CamLoad(&g_slot[LITH_SLOT(0)]);     /* img 0x2050 */
	for (i = 0; i < 12; i++)
		Lith_ObjLoadOrientB(&g_slot[LITH_SLOT(drawn[i])]);

	Lith_SceneReset(sc);                            /* img 0x209d */
	Lith_TerrainBuild(sc, g_slot[0].ang_x, g_slot[0].ang_y,
	                  g_slot[0].pos_x >> 0xc, g_slot[0].pos_z >> 0xc,
	                  LITH_BATTLE_TERRAIN_ATTR);    /* img 0x20a7 */

	for (i = 0; i < 12; i++) {
		const LithMesh *m;
		s32 attr;

		if (i < 3)       { m = &g_mesh_near; attr = 0; }
		else if (i < 6)  { m = &g_mesh_far;  attr = LITH_ATTR_SPHERE; }
		else             { m = &g_mesh_bolt; attr = 1; }
		obj_select(&o, &g_slot[LITH_SLOT(drawn[i])]);
		o.scale_shift = (i < 6) ? 0x10 : 0xe;   /* img 0x20c2 */
		o.mirror = 0;
		/* img 0x4e33 and 0x4e44 - Mesh_Transform rebases against
		 * SLOT 0's position, read unindexed, so every craft is
		 * placed relative to the camera rather than absolutely.
		 * Left at zero they land a whole world away. */
		o.rebase_x = g_slot[LITH_SLOT(0)].pos_x;
		o.rebase_z = g_slot[LITH_SLOT(0)].pos_z;
		o.axis_swap = 1;                /* g_ObjAxisSwap, img 0x203e */
		Lith_ObjFaceAttr(attr);
		Lith_MeshTransform(sc, m, &o);
	}

	cp[0] = (g_slot[0].pos_x & 0xfff) >> 4;         /* img 0x217d */
	cp[2] = ((-(g_slot[0].pos_z + 1)) & 0xfff) >> 4;
	g_slot[0].pos_y >>= 4;
	cp[1] = g_slot[0].pos_y;
	ca[0] = g_slot[0].ang_x;
	ca[1] = g_slot[0].ang_y;
	ca[2] = g_slot[0].ang_z;

	Lith_SceneProject(pv, sc, cp, ca);              /* img 0x21b1 */
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
		Lith_PolyFill(fb,
		              sc->attr[f] == LITH_BATTLE_TERRAIN_ATTR
		              ? pic : tex, &p, Lith_FaceBand(sc->attr[f]));
	}

	Lith_SkySunAndStars(fb, ca[0], ca[1], ca[2]);   /* img 0x21b7 */
	Lith_FlareRender(fb);                           /* img 0x21ba */
	/* img 0x21c5 maybe_Sky_DrawPoint IS DEAD CODE: its gate
	 * g_SkyPointsOn img 0x8f5a is never written. effects/sky.txt. */

	Lith_CamLoad(&g_slot[LITH_SLOT(0)]);     /* img 0x21c8 */
	{
		s32 tp[3], ta[3];

		tp[0] = g_slot[0].pos_x;
		tp[1] = g_slot[0].pos_y;
		tp[2] = g_slot[0].pos_z;
		ta[0] = g_slot[0].ang_x;
		ta[1] = g_slot[0].ang_y;
		ta[2] = g_slot[0].ang_z;
		Lith_TrailDraw(fb, tp, ta);             /* img 0x21cd */
	}
}

/* ORIGIN img 0x21e0 - and it is the TRACK that ends the scene, not
 * anything on screen. Only track 0 carries the stop command.
 * ./battle.txt section 3. */
s32 Lith_BattleDone(void)
{
	return Lith_SceneDone();
}

/* ORIGIN img 0x2496 Tex_SnapshotFrame - one terrain frame, kept.
 *
 * THE PICTURE THE REVERSE SHATTER ASSEMBLES IS PRE-RENDERED and was
 * never on screen. It is also what the battle's own terrain is
 * textured from, img 0x20a7, so this one frame is both the thing the
 * glass puts back together and the surface detail of the scene that
 * follows.
 *
 * IT IS TERRAIN, SKY AND FLARE ONLY - no craft, no bolts. The slots
 * it places are the two the terrain needs: 0 for the camera and 4
 * for the shadow origin, both parked at 0x46000.
 *
 * THE TEXTURE IT USES IS WHATEVER 0x5ad2 ALREADY HELD, img 0x2541,
 * which at this point in the demo is the picture the glass broke out
 * of. So the chain feeds itself: the bump scene's surface becomes
 * this terrain's texture, and this terrain becomes the battle's. */
void Lith_TexSnapshotFrame(u8 *pic, const u8 *tex, LithScene *sc,
                           LithProj *pv, LithSortEnt *sl)
{
	static u8 back[PD_SCREEN_W * PD_SCREEN_H];
	s32 cp[3], ca[3], n, i;

	Lith_ObjReset(&g_slot[LITH_SLOT(0)]);   /* img 0x249f */
	Lith_ObjReset(&g_slot[LITH_SLOT(4)]);
	for (i = 0; i <= 1; i++) {
		LithSlot *o = &g_slot[i];

		o->flight_x = 0x46000;          /* img 0x24a2 */
		o->flight_z = 0x46000;
		o->home_x = 0x46000;
		o->home_z = 0x46000;
	}
	Lith_PalSelect(LITH_PAL_1);             /* img 0x24f8 */
	memset(back, 0x80, sizeof back);        /* img 0x2515 */
	Lith_TrailReset();                      /* img 0x2528 */

	Lith_CamLoad(&g_slot[LITH_SLOT(0)]);     /* img 0x2534 */
	Lith_SceneReset(sc);
	Lith_TerrainBuild(sc, g_slot[0].ang_x, g_slot[0].ang_y,
	                  g_slot[0].pos_x >> 0xc, g_slot[0].pos_z >> 0xc,
	                  LITH_BATTLE_TERRAIN_ATTR);    /* img 0x2541 */

	cp[0] = (g_slot[0].pos_x & 0xfff) >> 4;         /* img 0x254b */
	cp[2] = ((-(g_slot[0].pos_z + 1)) & 0xfff) >> 4;
	g_slot[0].pos_y >>= 4;
	cp[1] = g_slot[0].pos_y;
	ca[0] = g_slot[0].ang_x;
	ca[1] = g_slot[0].ang_y;
	ca[2] = g_slot[0].ang_z;

	Lith_SceneProject(pv, sc, cp, ca);              /* img 0x257f */
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
		/* EVERY FACE HERE IS TERRAIN, and its attribute is the
		 * destination segment - so the texture is what `pic`
		 * still holds, which the copy below has not yet
		 * replaced. `tex` is unused and says so. */
		Lith_PolyFill(back, pic, &p, Lith_FaceBand(sc->attr[f]));
	}
	(void)tex;
	Lith_SkySunAndStars(back, ca[0], ca[1], ca[2]); /* img 0x2585 */
	Lith_FlareRender(back);                         /* img 0x2588 */

	memcpy(pic, back, PD_SCREEN_W * PD_SCREEN_H);   /* img 0x2590 */
}
