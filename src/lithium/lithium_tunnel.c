/* LITHIUM - the tunnel the glass breaks into.
 *
 * ORIGIN img 0x0496 Tunnel_Build and img 0x0602 Tunnel_StepPath.
 * docs/lithium/effects/tunnel.txt.
 *
 * THE TUNNEL HAS NO MESH. A ring of sixteen points is swept along a
 * path, and the path is three Euler angles advancing at fixed rates
 * - so the whole shape comes out of the sine table every frame and
 * nothing about it is stored in the asset segment.
 *
 * THE RATES ARE 4:2:1, img 0x0650 to 0x065e. The three axes come
 * back into phase only after a long run, which is what makes the
 * path read as a tunnel winding rather than a circle.
 *
 * THE COORDINATES ARE 16-BIT AND WRAP. Every accumulate into the
 * path and every vertex store is a word, so a long enough run folds
 * back on itself rather than running away.
 */

#include "lithium.h"

/* g_TunnelPath{X,Y,Z} img 0x9007, and g_RotAngle{X,Y,Z} img 0x8f14,
 * which Tunnel_Build zeroes and the path step advances. */
static s32 g_path_x, g_path_y, g_path_z;
static s32 g_ang_x, g_ang_y, g_ang_z;

/* ORIGIN img 0x0602 Tunnel_StepPath - one ring along the path.
 *
 * THE HEADING IS NEVER STORED AS A DIRECTION. The step is always
 * (0, 0, 0x100) and it is the FRAME that turns, so the path curves
 * continuously and the rings stay perpendicular to it for free.
 *
 * THE THREE SHIFTS DIFFER - 8, 0xc and 0x10 - so the step is scaled
 * unequally between rotations and the advance is not a constant
 * length. */
static void tunnel_step_path(void)
{
	s32 x = 0, y = 0, z = 0x100;

	Lith_VecRotZ(&x, &y, &z, g_ang_z);
	x >>= 8; y >>= 8; z >>= 8;
	Lith_VecRotX(&x, &y, &z, g_ang_x);
	x >>= 0xc; y >>= 0xc; z >>= 0xc;
	Lith_VecRotY(&x, &y, &z, g_ang_y);
	x >>= 0x10; y >>= 0x10; z >>= 0x10;

	g_path_x = (s32)(s16)(g_path_x + x);
	g_path_y = (s32)(s16)(g_path_y + y);
	g_path_z = (s32)(s16)(g_path_z + z);

	g_ang_x = (s32)(s16)(g_ang_x + 0x100);
	g_ang_y = (s32)(s16)(g_ang_y + 0x80);
	g_ang_z = (s32)(s16)(g_ang_z + 0x40);
}

/* ORIGIN img 0x0496 Tunnel_Build - every vertex and face, per frame.
 *
 * A RING IS A CIRCLE READ STRAIGHT OUT OF THE TABLE. img 0x050a
 * takes the cosine and img 0x0510 the sine at the SAME byte offset
 * in the two tables, so a vertex starts as (cos t, sin t, 0) and is
 * then carried into the path's frame.
 *
 * THE FIRST RINGS ARE WALKED AND DISCARDED. first_ring steps the
 * path forward before any vertex is written, which is how geometry
 * behind the camera is skipped with no clip test. The shatter scene
 * passes -4, so nothing is skipped and the bore opens behind the
 * eye instead.
 *
 * THE RING SHIFTS ARE 0xa, 0xc AND 0x11, so the circle is scaled
 * unequally on each axis - the bore is an ellipse whose shape
 * changes along the run rather than a tube of fixed section.
 *
 * THE FACES ARE NOT REBASED on the vertex pool the way
 * Mesh_Transform's are: img 0x05b7 builds indices from the ring
 * counter alone, so the tunnel has to be the first thing in the
 * scene. The original's own scene resets the pool immediately
 * before calling this, img 0x09dd. */
void Lith_TunnelBuild(LithScene *sc, s32 cam_x, s32 cam_z,
                      s32 first_ring, s32 last_ring, s32 attr)
{
	s32 rings, r, b, a, cx;

	g_path_x = (s32)(s16)((cam_x >> 0xc) << 8);
	g_path_y = 0;
	g_path_z = (s32)(s16)(-(s32)(s16)((cam_z >> 0xc) << 8));
	g_ang_x = 0;
	g_ang_y = 0;
	g_ang_z = 0;

	for (r = 0; r < first_ring; r++)        /* img 0x04e7, JLE */
		tunnel_step_path();

	rings = last_ring - first_ring;
	for (r = 0; r < rings; r++) {
		for (b = 0; b < 0x2000; b += 0x200) {
			s32 x = Lith_Cos(b), y = Lith_Sin(b), z = 0;
			LithVert *v;

			Lith_VecRotZ(&x, &y, &z, g_ang_z);
			x >>= 0xa; y >>= 0xa; z >>= 0xa;
			Lith_VecRotX(&x, &y, &z, g_ang_x);
			x >>= 0xc; y >>= 0xc; z >>= 0xc;
			Lith_VecRotY(&x, &y, &z, g_ang_y);
			x >>= 0x11; y >>= 0x11; z >>= 0x11;

			if (sc->nvert >= LITH_POOL_MAXV) return;
			v = &sc->v[sc->nvert++];
			v->x = (s32)(s16)(x + g_path_x);
			v->y = (s32)(s16)(y + g_path_y);
			v->z = (s32)(s16)(z + g_path_z);
			/* img 0x055f - u walks the bore, v goes round it. */
			v->u = ((s32)(s16)g_ang_x >> 3) & 0xff;
			v->v = (b >> 3) & 0xff;
		}
		tunnel_step_path();
	}

	/* One fewer band of faces than there are rings, img 0x059b. */
	for (r = 0; r < rings - 1; r++) {
		cx = r * 0x10;
		for (a = 0; a < 0x10; a++) {
			s32 v0, v1, v2;

			if (sc->nface + 2 > LITH_POOL_MAXF) return;
			v0 = a + cx;
			v1 = ((a + 1) & 0xf) + cx;
			v2 = v1 + 0x10;
			sc->f0[sc->nface] = v0;
			sc->f1[sc->nface] = v1;
			sc->f2[sc->nface] = v2;
			sc->attr[sc->nface++] = attr;
			/* img 0x05d0 - the mask drops the ring base with the
			 * wrap, because cx is always a multiple of 0x10. */
			sc->f0[sc->nface] = v2;
			sc->f1[sc->nface] = a + 0x10 + cx;
			sc->f2[sc->nface] = a + cx;
			sc->attr[sc->nface++] = attr;
		}
	}
}

/* ------------------------------------------- the eye, and the scene */

/* Slot 0. The shatter phase flies it and maybe_Scene_Tunnel carries
 * straight on with it, which is why it lives here rather than with
 * either runner - the original has one slot bank and both index it.
 *
 * g_TunnelVel{X,Y,Z} img 0x9013, g_TunnelFirstRing img 0x900f and
 * g_TunnelLastRing img 0x9011, and the 16-tick counter they move on,
 * which is g_RecordMode img 0x8fb4 doing its second job. */
static LithSlot *const g_cam = &g_lith_slot[0];
static s32 g_vel_x, g_vel_y, g_vel_z;
static s32 g_first_ring, g_last_ring;
static s32 g_ring_tick;

/* OURS - the fade at img 0x0480 must arm once, not every frame. */
static s32 g_fade_armed;

/* ORIGIN img 0x6612 - g_RecordMode, one byte with three jobs.
 * effects/battle.txt section 1. */
void Lith_RecordModeClear(void) { g_ring_tick = 0; }

/* ORIGIN img 0x0964 to 0x09ce - slot 0's seeding, in the handover
 * that starts the shatter. Only z is given a flight value, so the
 * eye accelerates into the bore from rest. */
void Lith_TunnelCamInit(void)
{
	Lith_ObjReset(g_cam);                  /* img 0x0966 */
	g_cam->flight_x = 0;                     /* img 0x0969 */
	g_cam->flight_z = 0x1000;
	g_cam->home_x = 0;
	g_cam->home_z = 0;
	g_cam->home_y = 0;
	g_cam->a_x = 0x80;                       /* img 0x099b */
	g_cam->a_y = 0x40;
	g_cam->a_z = 0x20;

	g_first_ring = -4;                      /* img 0x09c7 */
	g_last_ring = 11;
	g_vel_x = 0;
	g_vel_y = 0;
	g_vel_z = 0;
	g_ring_tick = 0;
}

/* ORIGIN img 0x6b7f Tick_Tunnel - the handler maybe_Scene_Tunnel
 * installs, and the half of Tick_Rock that has no RET before it.
 *
 * THE EYE WALKS THE TUNNEL'S OWN CURVE. The velocity at img 0x6c28
 * is Tunnel_StepPath's arithmetic exactly - the same (0, 0, 0x100),
 * the same three rotations, the same shifts - on the second angle
 * set, recomputed once every sixteen ticks while the ring window
 * advances by one. Sixteen ticks of motion divided by sixteen is one
 * ring, and that is the whole of the lock. ./tunnel.txt section 6.1.
 *
 * IT DOES NOT CALL THE COMMON TAIL. Tick_Rock falls into this and
 * then into Tick_Default's body at img 0x6c8e, so the caller adds
 * that rather than this. */
void Lith_TunnelTick(void)
{
	Lith_PalAnimate(Lith_PalStore());       /* img 0x6b7f */
	Lith_PalRefresh();

	g_cam->flight_x += g_vel_x;              /* img 0x6b8a */
	g_cam->home_x += (g_cam->flight_x - g_cam->home_x) >> 5;
	g_cam->flight_y += g_vel_y;
	g_cam->home_y += (g_cam->flight_y - g_cam->home_y) >> 5;
	g_cam->flight_z += g_vel_z;
	g_cam->home_z += (g_cam->flight_z - g_cam->home_z) >> 5;

	/* img 0x6be5 - X and Y are the exact negation of the path's own
	 * 0x100 and 0x80 a ring, so the eye turns into the bend as fast
	 * as the bore turns away. Z IS NOT: the path takes 0x40 a ring
	 * and this takes 0xc0, so the view rolls 0x80 a ring relative to
	 * the tube. ./tunnel.txt section 7. */
	g_cam->a_x = (s32)(s16)(g_cam->a_x - 0x10);
	g_cam->a_y = (s32)(s16)(g_cam->a_y - 0x08);
	g_cam->a_z = (s32)(s16)(g_cam->a_z - 0x0c);

	g_ring_tick = (g_ring_tick + 1) & 0xff; /* img 0x6bf7 */
	if (!(g_ring_tick & 0xf)) {
		s32 x = 0, y = 0, z = 0x100;

		g_first_ring++;                 /* img 0x6c06 */
		g_last_ring++;
		Lith_VecRotZ(&x, &y, &z, g_cam->roll);
		x >>= 8; y >>= 8; z >>= 8;
		Lith_VecRotX(&x, &y, &z, g_cam->pitch);
		x >>= 0xc; y >>= 0xc; z >>= 0xc;
		Lith_VecRotY(&x, &y, &z, g_cam->yaw);
		x >>= 0x10; y >>= 0x10; z >>= 0x10;
		g_vel_x = -x;                   /* img 0x6c67 */
		g_vel_y = -y;
		g_vel_z = z;
		g_cam->pitch = (s32)(s16)(g_cam->pitch + 0x100);
		g_cam->yaw = (s32)(s16)(g_cam->yaw + 0x80);
	}
}

/* ORIGIN img 0x09da to 0x0a28, and img 0x041c to 0x046c - the two
 * runners build and draw the bore with the same seven steps.
 *
 * THE EYE'S POSITION REACHES THE GEOMETRY TWICE: whole rings through
 * g_TunnelFirstRing, which the tick advances, and the remainder
 * through the masked and shifted pair here. The mask keeps only what
 * is left inside one ring and the shift is the same sixteen the
 * window moves on. ./tunnel.txt section 6.1. */
void Lith_TunnelDraw(u8 *fb, const u8 *tex, LithScene *sc,
                     LithProj *pv, LithSortEnt *sl)
{
	s32 cp[3], ca[3];

	Lith_CamLoad(g_cam);            /* img 0x09da */
	Lith_SceneReset(sc);                    /* img 0x09dd */
	Lith_TunnelBuild(sc, g_cam->pos_x, g_cam->pos_z,
	                 g_first_ring, g_last_ring, LITH_ATTR_SPHERE);

	cp[0] = (g_cam->pos_x & 0xfff) >> 4;             /* img 0x09f1 */
	cp[2] = ((-(g_cam->pos_z + 1)) & 0xfff) >> 4;    /* img 0x0a05 */
	g_cam->pos_y >>= 4;                              /* img 0x0a1e */
	cp[1] = g_cam->pos_y;
	ca[0] = g_cam->ang_x;
	ca[1] = g_cam->ang_y;
	ca[2] = g_cam->ang_z;
	Lith_SceneDraw(fb, tex, sc, pv, sl, cp, ca,
	               LITH_TUNNEL_DEPTH_NEAR);
}

/* ORIGIN img 0x0a2e to 0x0a6e - the shatter phase zeroes the eye
 * before it draws the rock, position AND all three angles, so the
 * rock is rendered in a camera-less frame and sits in the middle of
 * the view instead of sweeping past with the walls. */
void Lith_TunnelCamClear(void)
{
	g_cam->pos_x = 0;
	g_cam->pos_y = 0;
	g_cam->pos_z = 0;
	g_cam->ang_x = 0;
	g_cam->ang_y = 0;
	g_cam->ang_z = 0;
}

/* ORIGIN img 0x040b maybe_Scene_Tunnel - the bore with nothing in
 * it, straight after the glass. It installs Tick_Tunnel ALONE, so
 * the rock stops being flown and the shards stop being stepped. */
void Lith_TunnelEnter(void)
{
	g_fade_armed = 0;
}

/* ORIGIN img 0x041c to 0x0475 - and the frame is the bore and
 * nothing else. There is no clear: the tunnel covers every pixel,
 * which is also why img 0x0472 blits the whole screen rather than a
 * dirty rectangle. */
void Lith_TunnelFrame(u8 *fb, const u8 *tex, LithScene *sc,
                      LithProj *pv, LithSortEnt *sl)
{
	Lith_TunnelDraw(fb, tex, sc, pv, sl);
}

/* ORIGIN img 0x0478 to 0x0493 - TWO music positions, not one. At
 * 0x1e it arms a fade to black over shift 8 and KEEPS RUNNING; only
 * 0x1f ends it, so the scene plays its own fade out rather than
 * handing a lit screen to whatever follows. */
s32 Lith_TunnelDone(void)
{
	if (!g_fade_armed && Lith_MusicPos() >= 0x1e) {
		g_fade_armed = 1;
		Lith_PalFadeTo(-0x100);         /* img 0x0480 */
		Lith_PalFadeShiftSet(8);        /* img 0x0487 */
	}
	return Lith_MusicPos() >= 0x1f;
}
