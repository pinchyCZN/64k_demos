/* LITHIUM - the surface flyover runner.
 *
 * ORIGIN img 0x18a3 maybe_Scene_SurfaceFlyover and its tick handler
 * img 0x639e. docs/lithium/effects/flyover.txt has the frame's call
 * order and the slot table; each component has its own note.
 *
 * FOUR SLOTS AND ONLY THREE ARE DRAWN. Slot 0 is the camera, slots 4
 * and 0xc are the same 34-vertex mesh under the sphere map - the two
 * ships - and slot 8 is the tower, which the tick handler never
 * touches because it does not move.
 *
 * THE TRAIL CALL AT img 0x1b5e DRAWS NOTHING HERE - g_TrailActive
 * gates it and no track in this scene fires. effects/trail.txt 1.1.
 */

#include <string.h>

#include "lithium.h"

static LithSlot *const g_slot = g_lith_slot;   /* 0, 4, 8 and 0xc */
static s32 g_cam_vel, g_cam_turn;       /* img 0x901f and 0x9023  */
static u8 g_back[PD_SCREEN_W * PD_SCREEN_H];

/* ORIGIN img 0x18a3, the setup. The positions are the two words each
 * slot gets at img 0x8959 and 0x8991, written to both the flight and
 * the home copies so nothing has to settle before the first frame. */
void Lith_FlyoverEnter(void)
{
	s32 i;

	for (i = 0; i < 4; i++) Lith_ObjReset(&g_slot[i]);
	Lith_SceneDoneClear();
	Lith_RollEaseShift(2);                  /* img 0x18a9 */

	g_slot[0].flight_x = 0x24000; g_slot[0].home_x = 0x24000;
	g_slot[0].flight_z = 0x0a000; g_slot[0].home_z = 0x0a000;
	g_slot[0].track = 0x7aca;
	g_cam_vel = 0xa000;                     /* img 0x18ed */
	g_cam_turn = 0x800;                     /* img 0x18f7 */

	g_slot[1].flight_x = 0x40000; g_slot[1].home_x = 0x40000;
	g_slot[1].flight_z = 0x0a000; g_slot[1].home_z = 0x0a000;
	g_slot[1].track = 0x74fd;

	g_slot[2].flight_x = 0x40000; g_slot[2].home_x = 0x40000;
	g_slot[2].flight_z = 0x6c000; g_slot[2].home_z = 0x6c000;
	g_slot[2].flight_y = 0x3800; g_slot[2].home_y = 0x3800;

	g_slot[3].flight_x = 0x40000; g_slot[3].home_x = 0x40000;
	g_slot[3].flight_z = 0x64c00; g_slot[3].home_z = 0x64c00;
	g_slot[3].flight_y = 0x5dc; g_slot[3].home_y = 0x5dc;
	g_slot[3].yaw_acc = -0x800;             /* img 0x19a6 */
	g_slot[3].yaw = -0x800;                 /* img 0x19ad and 0x19b4 */
	g_slot[3].a_y = -0x800;
	g_slot[3].track = 0x809a;

	Lith_CreditsEnter();
	Lith_MusicPosLatch();
	memset(g_back, 0x80, sizeof g_back);    /* img 0x19f6 */
	Lith_PolyReset();
}

/* OURS - so the state line can read the bank without reaching into
 * it. Slot 0 is the camera, 1 the lead craft - index as the runner
 * does, not as the demo numbers its slots. */
const LithSlot *Lith_FlyoverSlot(s32 i)
{
	return (i >= 0 && i < 4) ? &g_slot[i] : &g_slot[0];
}

/* ORIGIN img 0x639e, the tick handler.
 *
 * THE SHOT IS A CHASE: the camera's track spends a quarter of its
 * records on command 0x18, so slot 4 is the lead every Flight_Step
 * is handed - effects/flight.txt 6.2.
 *
 * THE CAMERA COASTS TO A STOP. Both of its rates decay to zero and
 * nothing restores them, so the opening push is all the scene gets
 * and everything after that comes out of Flight_Step. */
void Lith_FlyoverTick(void)
{
	static const s32 drive[3] = { 0, 1, 3 };    /* slots 0, 4, 0xc */
	s32 i;

	Lith_FlickerStep();                     /* img 0x639e */
	g_cam_vel -= 0x55;                      /* img 0x63a1 */
	if (g_cam_vel < 0) g_cam_vel = 0;
	g_cam_turn -= 4;                        /* img 0x63b4 */
	if (g_cam_turn < 0) g_cam_turn = 0;

	for (i = 0; i < 3; i++) {
		Lith_TrackPlay(&g_slot[drive[i]]);
		Lith_FlightStep(&g_slot[drive[i]], &g_slot[1]);
	}
	Lith_FlareEnergyStep();
	Lith_CreditsTick();
	Lith_TickDefault();
}

/* ORIGIN img 0x1a48, the frame.
 *
 * THE SKY RUNS AFTER THE GEOMETRY - img 0x1b53 - then the flare at
 * img 0x1b56 and the credit roll at img 0x1b61. effects/sky.txt 3,
 * effects/text.txt 7.
 *
 * TWO TEXTURES, NOT ONE: img 0x1a7d points the grid at segment
 * 0x5ad2 - the glow, effects/terrain.txt 6 - and img 0x1aaa points
 * the ships at 0x6ad2, the sphere map. nterrain is where the grid's
 * faces end, since Terrain_Build emits before the meshes.
 *
 * THE MODELS ARE PLACED CAMERA-RELATIVE - subsystems/mesh3d.txt 3.2.
 *
 * THE TOWER IS FACE ATTRIBUTE ZERO - img 0x1ad7 against the ships'
 * 0x6ad2 at img 0x1aaa and 0x1b04 - which is the same sphere map
 * with a band added, and the terrain carries 0x5ad2 from img
 * 0x1a7d. effects/flyover.txt section 4.
 *
 * THE CAMERA'S EXTRA PUSH IS NOT AN ACCUMULATOR. img 0x1a4d adds the
 * two decaying rates to the LIVE position and angle, which
 * Obj_LoadOrientA has just overwritten from the slot - so it is a
 * per-frame offset that shrinks to nothing, not a velocity. */
void Lith_FlyoverFrame(u8 *fb, const LithMesh *ship, const LithMesh *tower,
                       const u8 *tex, const u8 *glow, LithScene *sc,
                       LithProj *pv, LithSortEnt *sl)
{
	static const s32 order[3] = { 1, 2, 3 };
	s32 cp[3], ca[3], n, i, scroll_x, scroll_z, nterrain;
	LithObj o;

	Lith_CamLoad(&g_slot[0]);
	g_slot[0].pos_y += g_cam_vel;
	g_slot[0].ang_x += g_cam_turn;
	for (i = 0; i < 3; i++) Lith_ObjLoadOrientB(&g_slot[order[i]]);

	/* img 0x32f1 - before the draw, because the pass the fill loop
	 * runs below measures its u and v against it. */
	Lith_ShadowOrigin(&g_slot[0], &g_slot[1]);
	Lith_SceneReset(sc);                    /* img 0x1a73 */

	/* THE TERRAIN IS BUILT IN A LOCAL FRAME. Terrain_Height picks
	 * the map cell with the camera's position shifted down 0xc, so
	 * the grid comes out centred on the camera - and what the
	 * projection then adds is only the SUB-CELL remainder, img
	 * 0x1b0e, masked to 0xfff and shifted down 4. Handing it the
	 * absolute position instead throws the whole scene off screen. */
	Lith_TerrainBuild(sc, g_slot[0].ang_x, g_slot[0].ang_y,
	                  g_slot[0].pos_x >> 0xc, g_slot[0].pos_z >> 0xc,
	                  LITH_ATTR_GLOW);
	nterrain = sc->nface;
	scroll_x = (g_slot[0].pos_x & 0xfff) >> 4;
	scroll_z = ((-(g_slot[0].pos_z + 1)) & 0xfff) >> 4;
	g_slot[0].pos_y >>= 4;                  /* img 0x1b3b */

	for (i = 0; i < 3; i++) {
		const LithSlot *s = &g_slot[order[i]];

		Lith_ObjInit(&o);
		o.ang_x = s->ang_x; o.ang_y = s->ang_y; o.ang_z = s->ang_z;
		o.pos_x = s->pos_x; o.pos_y = s->pos_y; o.pos_z = s->pos_z;
		o.axis_swap = 1;                /* img 0x1a3c */
		o.rebase_x = g_slot[0].pos_x;
		o.rebase_z = g_slot[0].pos_z;
		o.scale_shift = (order[i] == 2) ? 0xa : 0x10;
		Lith_ObjFaceAttr((order[i] == 2) ? 0 : LITH_ATTR_SPHERE);
		Lith_MeshTransform(sc, (order[i] == 2) ? tower : ship, &o);
	}

	cp[0] = scroll_x; cp[1] = g_slot[0].pos_y; cp[2] = scroll_z;
	ca[0] = g_slot[0].ang_x; ca[1] = g_slot[0].ang_y;
	ca[2] = g_slot[0].ang_z;
	Lith_SceneProject(pv, sc, cp, ca);
	n = Lith_SceneCullSort(sl, pv, sc, LITH_FLYOVER_NEAR);

	memset(g_back, 0x80, sizeof g_back);
	for (i = 0; i < n; i++) {
		s32 f = sl[i].face;
		s32 idx[3];
		LithPoly p;
		s32 k;

		idx[0] = sc->f0[f]; idx[1] = sc->f1[f]; idx[2] = sc->f2[f];
		for (k = 0; k < 3; k++) {
			p.x[k] = pv[idx[k]].sx;
			p.y[k] = pv[idx[k]].sy;
			p.u[k] = pv[idx[k]].u;
			p.v[k] = pv[idx[k]].v;
		}
		Lith_PolyFill(g_back,
		              Lith_FaceSegment(sc->attr[f]) == LITH_ATTR_GLOW
		              ? glow : tex, &p, Lith_FaceBand(sc->attr[f]));

		/* img 0x342e - THE SAME FACE A SECOND TIME, for the craft's
		 * shadow. Terrain only, and the screen positions stand while
		 * only u and v are rebuilt. subsystems/raster.txt 3.3. */
		if (sc->attr[f] == LITH_ATTR_GLOW
		    && Lith_ShadowFaceUV(&p, pv, idx, g_slot[1].ang_y))
			Lith_PolyFillShadow(g_back, Lith_ShadowStencil(), &p);
	}

	Lith_SkySunAndStars(g_back, g_slot[0].ang_x, g_slot[0].ang_y,
	                    g_slot[0].ang_z);
	Lith_FlareRender(g_back);               /* img 0x1b56 */
	Lith_CreditsDraw(g_back);               /* img 0x1b61 */

	/* img 0x1bc8 Screen_PresentDirty, NOT Screen_CrossFade - the
	 * opening's smear is the opening's own. It copies the dirty box
	 * to the screen and refills the back buffer with the 0x80 staged
	 * at img 0x8ffd as it goes, which is why nothing clears the back
	 * at the top of the frame. We clear the whole thing instead of a
	 * box, which is the same result and no dirty box to keep. */
	memcpy(fb, g_back, (size_t)(PD_SCREEN_W * PD_SCREEN_H));
}
