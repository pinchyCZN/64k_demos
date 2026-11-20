/* LITHIUM - the runner, and the last thing the demo shows.
 *
 * ORIGIN img 0x10f7 maybe_Scene_Finale, img 0x11cf Figure_Draw, img
 * 0x6454 Tick_Finale and img 0x133e Vec_RotateByObj.
 * docs/lithium/effects/figure.txt.
 *
 * FIFTEEN BALLS ON A SKELETON AND NOT ONE KEYFRAME. The figure is a
 * hierarchy of fifteen joints, each drawing one of four ball meshes,
 * and the whole run cycle comes out of a single phase word stepped
 * 0x40 a tick. Nothing is stored per frame.
 *
 * THE CYCLE IS 128 TICKS, NOT 32. The mask is 0x1ffc and the index
 * is that shifted down two, so the phase spans 0x2000 for one turn
 * of a 2048-entry table - dividing the entry count by the step
 * instead is the easy error and it comes out four times too fast.
 *
 * THE FIGURE NEVER MOVES. g_FigureRoot stays at the origin for the
 * whole scene; it is THE CAMERA that closes, img 0x65a2 adding 4 to
 * its z every tick. So the man runs on the spot and the world comes
 * to him, which costs three words a tick instead of fifteen.
 *
 * THE LEFT SIDE IS THE RIGHT SIDE HALF A CYCLE LATER, img 0x64ef
 * adding 0x1000 to the phase - one cosine, read twice, animates all
 * twelve limb joints. effects/figure.txt section 3.
 *
 * THE BALLS ARE SPHERE-MAPPED, img 0x1329 forcing the texture to
 * 0x6ad2 for every joint, which is what makes them read as rock
 * rather than as spheres. subsystems/raster.txt section 4.
 */

#include <string.h>

#include "lithium.h"

/* The four ball meshes the SCRIPT loads at img 0x0363 to 0x03b0,
 * before the scene is entered. g_BallMeshTable img 0x9027 is filled
 * with these four offsets by Figure_Draw itself, every frame. */
#define LITH_FIGURE_BALLS 4
static const u32 g_ball_src[LITH_FIGURE_BALLS] = {
	0x4974UL, 0x49f6UL, 0x4a78UL, 0x4afaUL
};
static LithMesh g_ball[LITH_FIGURE_BALLS];

/* g_JointRecs img 0x92fe - fifteen records of 0x40 bytes, and the
 * fields this needs are at +0x00 the ball table entry's ADDRESS,
 * +0x04 the three angles, +0x0a the accumulated world position,
 * +0x16 the parent, +0x18 offset A and +0x24 offset B.
 * ./figure.txt section 1. */
#define LITH_JOINTS      15
#define LITH_JOINT_RECS  0x92feUL
#define LITH_JOINT_SIZE  0x40
#define LITH_BALL_TABLE  0x9027UL

typedef struct {
	s32 ball;                       /* +0x00, resolved to an index */
	s32 ang[3];                     /* +0x04 - Tick_Finale writes  */
	s32 acc[3];                     /* +0x0a - scratch per frame   */
	s32 parent;                     /* +0x16, -1 for the root      */
	s32 off_a[3];                   /* +0x18 */
	s32 off_b[3];                   /* +0x24 */
} LithJoint;

static LithJoint g_joint[LITH_JOINTS];

/* g_WalkPhase img 0x9001, and the camera pair img 0x8ca1 and
 * 0x8d11 - the scene writes those two directly where every other
 * scene derives them from a slot. */
static s32 g_phase, g_cam_x, g_cam_z;

/* OURS - what the playground drives. The step is the demo's
 * 0x40 and the smear is on, so a finale that never touches
 * them behaves exactly as the original does.
 * ../../docs/playground.txt section 5.4. */
static s32 g_step = 0x40;
static s32 g_smear = 1;
static s32 g_near = LITH_FINALE_DEPTH_NEAR;

/* 0x4ad2 the frame the figure is drawn into, cleared every pass at
 * img 0x11b1, and 0x7ad2 the accumulator the cross-fade eases -
 * cleared once at img 0x10fa. The accumulator is what is seen. */
static u8 g_work[PD_SCREEN_W * PD_SCREEN_H];
static u8 g_accum[PD_SCREEN_W * PD_SCREEN_H];

/* ORIGIN img 0x133e Vec_RotateByObj - the helper Figure_Draw calls
 * twice a joint.
 *
 * THE Z ANGLE IS DOUBLED AND THE OTHER TWO ARE NOT, img 0x1344, so
 * a joint's roll turns it twice as far as its pitch or yaw for the
 * same stored word.
 *
 * THE LAST STAGE SHIFTS BY THE SCALE, NOT BY NINE. Z and X come out
 * at >> 9 each but Y takes g_ObjScaleShift instead - 0x10 for every
 * joint here - which is where the offsets stop being mesh units and
 * become world ones. */
static void vec_rotate_by_obj(s32 *x, s32 *y, s32 *z,
                              const s32 ang[3], s32 scale_shift)
{
	Lith_VecRotZ(x, y, z, (s32)(s16)(ang[2] * 2));  /* img 0x1344 */
	*x >>= 9; *y >>= 9; *z >>= 9;
	Lith_VecRotX(x, y, z, ang[0]);                  /* img 0x135a */
	*x >>= 9; *y >>= 9; *z >>= 9;
	Lith_VecRotY(x, y, z, ang[1]);                  /* img 0x1373 */
	*x >>= scale_shift;                             /* img 0x1383 */
	*y >>= scale_shift;
	*z >>= scale_shift;
}

/* OURS - the records are read out of the image once. The original
 * works in place and writes its angles and its scratch back into the
 * image; we cannot, so the bank is copied into RAM at entry. */
static void figure_load(void)
{
	s32 j, k;

	for (j = 0; j < LITH_JOINTS; j++) {
		u32 o = LITH_JOINT_RECS + (u32)j * LITH_JOINT_SIZE;
		LithJoint *q = &g_joint[j];

		q->ball = (s32)((RD_U16(g_lith_img + o) - LITH_BALL_TABLE) / 4);
		for (k = 0; k < 3; k++)
			q->ang[k] = (s32)(s16)RD_U16(g_lith_img + o + 4 + k * 2);
		for (k = 0; k < 3; k++) q->acc[k] = 0;
		q->parent = (s32)(s16)RD_U16(g_lith_img + o + 0x16);
		for (k = 0; k < 3; k++)
			q->off_a[k] = (s32)RD_U32(g_lith_img + o + 0x18 + k * 4);
		for (k = 0; k < 3; k++)
			q->off_b[k] = (s32)RD_U32(g_lith_img + o + 0x24 + k * 4);
		if (q->ball < 0 || q->ball >= LITH_FIGURE_BALLS) q->ball = 0;
	}
}

/* ORIGIN img 0x11cf Figure_Draw - the forward kinematics, one pass.
 *
 * A JOINT'S WORLD POSITION IS THE PARENT'S, plus offset A turned by
 * the PARENT's orientation, plus offset B turned by its OWN. Two
 * offsets a joint rather than one is what lets a limb carry both a
 * socket and a length in one record.
 *
 * ONE PASS IS ENOUGH BECAUSE THE RECORDS ARE SORTED. Every parent
 * index is lower than its own, so a parent's accumulated position is
 * always already there - ./figure.txt section 1.
 *
 * THE X GOES IN NEGATED, img 0x12fa, because Mesh_Transform
 * subtracts the x it is given and adds the z - so negating is what
 * makes both axes come out the same way round.
 * subsystems/mesh3d.txt section 3.2. */
static void figure_draw(LithScene *sc)
{
	s32 j;

	Lith_SceneReset(sc);                    /* img 0x11f7 */
	for (j = 0; j < LITH_JOINTS; j++) {
		LithJoint *q = &g_joint[j];
		s32 x, y, z;
		LithObj o;

		if (q->parent < 0) {            /* img 0x1230 */
			/* img 0x1235 - and it is zero for the whole scene. */
			x = 0; y = 0; z = 0;
		} else {
			LithJoint *p = &g_joint[q->parent];
			s32 ax = q->off_a[0], ay = q->off_a[1];
			s32 az = q->off_a[2];

			vec_rotate_by_obj(&ax, &ay, &az, p->ang, 0x10);
			q->acc[0] = ax;         /* img 0x127f */
			q->acc[1] = ay;
			q->acc[2] = az;

			x = q->off_b[0]; y = q->off_b[1]; z = q->off_b[2];
			vec_rotate_by_obj(&x, &y, &z, q->ang, 0x10);
			x += q->acc[0] + p->acc[0];     /* img 0x12c4 */
			y += q->acc[1] + p->acc[1];
			z += q->acc[2] + p->acc[2];
		}
		q->acc[0] = x;                  /* img 0x12e8 */
		q->acc[1] = y;
		q->acc[2] = z;

		Lith_ObjInit(&o);
		o.pos_x = -x;                   /* img 0x12fa */
		o.pos_y = y;
		o.pos_z = z;
		o.ang_x = q->ang[0];            /* img 0x130e */
		o.ang_y = q->ang[1];
		o.ang_z = q->ang[2];
		o.scale_shift = 0x10;           /* img 0x121f */
		o.mirror = 0;
		o.axis_swap = 0;                /* img 0x11a4 */
		Lith_ObjFaceAttr(LITH_ATTR_SPHERE);     /* img 0x1329 */
		Lith_MeshTransform(sc, &g_ball[q->ball], &o);
	}
}

/* ORIGIN img 0x10f7 to 0x11aa - the setup.
 *
 * IT SETS THE CAMERA TWICE. img 0x113e zeroes all three angles and
 * img 0x1185 then writes 0x100, 0x1200 and 0x1000 over them; the
 * same goes for the position pair. The first write is dead and only
 * the second is seen.
 *
 * THE AXIS SWAP GOES OFF HERE, img 0x11a4, where the battle and the
 * tower fall both left it on - so the figure is built in the
 * untwisted frame. */
void Lith_FinaleEnter(void)
{
	s32 i;

	Lith_ObjReset(&g_lith_slot[LITH_SLOT(0)]);      /* img 0x10ff */
	g_lith_slot[LITH_SLOT(0)].flight_x = 0;         /* img 0x1102 */
	g_lith_slot[LITH_SLOT(0)].flight_z = -1;
	g_lith_slot[LITH_SLOT(0)].home_x = 0;
	g_lith_slot[LITH_SLOT(0)].home_z = -1;
	g_lith_slot[LITH_SLOT(0)].home_y = 0x5a;        /* img 0x112a */
	g_lith_slot[LITH_SLOT(0)].pos_x = 0;            /* img 0x1153 */
	g_lith_slot[LITH_SLOT(0)].pos_y = 0x5a;         /* img 0x1167 */
	g_lith_slot[LITH_SLOT(0)].pos_z = -1;
	g_lith_slot[LITH_SLOT(0)].ang_x = 0x100;        /* img 0x1185 */
	g_lith_slot[LITH_SLOT(0)].ang_y = 0x1200;
	g_lith_slot[LITH_SLOT(0)].ang_z = 0x1000;
	g_cam_x = -0x64;                                /* img 0x115d */
	g_cam_z = -0x800;                               /* img 0x117b */

	for (i = 0; i < LITH_FIGURE_BALLS; i++)         /* img 0x0363 */
		Lith_MeshLoad(&g_ball[i], g_ball_src[i]);
	figure_load();

	g_phase = 0;
	g_step = 0x40;
	g_smear = 1;
	g_near = LITH_FINALE_DEPTH_NEAR;
	memset(g_accum, 0, sizeof g_accum);             /* img 0x10fa */
	memset(g_work, 0, sizeof g_work);
}

/* OURS - the three the playground needs, and nothing else reaches
 * these statics. Lith_FinaleEnter puts all of them back, so the
 * demo's own finale cannot inherit a setting from the game.
 *
 * THE CAMERA IS SET AFTER Tick_Finale HAS RUN, which is what lets
 * the orbit override the approach at img 0x65a2 and the slow roll
 * after it without a flag to switch them off. */
void Lith_FinaleCamSet(s32 x, s32 z) { g_cam_x = x; g_cam_z = z; }
void Lith_FinaleStepSet(s32 n) { g_step = n; }
void Lith_FinaleSmearSet(s32 on) { g_smear = on; }

/* THE NEAR CULL IS WHAT BOUNDS A CLOSE-UP. Scene_CullSort drops a
 * face whose three z values sum to this or less, and at the demo's
 * 0x4e2 the figure stops growing around a quarter of the screen
 * however near the eye gets - the near half of him is simply thrown
 * away. IT HAS A FLOOR OF ITS OWN: the projection CLAMPS z to
 * LITH_NEAR_Z rather than rejecting it, so anything behind the eye
 * arrives summing to 0x177, and a cull set at or under that draws it
 * smeared across the screen instead of dropping it. */
void Lith_FinaleNearSet(s32 n) { g_near = n; }

/* ORIGIN img 0x6454 Tick_Finale - the walk, and it is all table.
 *
 * THE HALVING CHAIN IS THE SKELETON AS ARITHMETIC, img 0x6463: each
 * step halves and negates the one before, so four joints come out of
 * one cosine sample for a shift and a negate apiece - the next joint
 * always half the swing and the opposite phase.
 *
 * THE ADDITIVE TERMS ARE REST ANGLES - 0x244, -0x2bc, 0xd00, 0x1400
 * - so those joints swing about a fixed pose rather than about zero.
 *
 * THE BOUNCE AND THE BOB RUN AT DOUBLE RATE, img 0x6559 and 0x6583
 * doubling the phase before the lookup, because a stride has two
 * footfalls. */
void Lith_FinaleTick(void)
{
	const s32 *t = Lith_SinTable();
	s32 bx, a;

	if (!t) return;
	bx = (g_phase & 0x1ffc) >> 2;

	a = t[bx + LITH_SIN_QUARTER] >> 3;              /* img 0x645d */
	g_joint[0].ang[1] = (s32)(s16)a;
	a = -(a >> 1);
	g_joint[7].ang[1] = (s32)(s16)a;
	a = -(a >> 1);
	g_joint[14].ang[2] = (s32)(s16)a;               /* img 0x647b */
	g_joint[14].ang[1] = (s32)(s16)a;
	a = -(a >> 1);
	g_joint[0].ang[2] = (s32)(s16)a;

	/* img 0x648d - the right arm, then the right leg. */
	g_joint[1].ang[0] = (s32)(s16)((t[bx + LITH_SIN_QUARTER] >> 2) + 0x244);
	a = (t[bx] >> 2) - 0x2bc;                       /* img 0x64a1 */
	g_joint[2].ang[0] = (s32)(s16)a;
	g_joint[3].ang[0] = (s32)(s16)a;
	g_joint[11].ang[0] = (s32)(s16)((t[bx + LITH_SIN_QUARTER] >> 3) + 0xd00);
	g_joint[11].ang[2] = -0x100;            /* img 0x64cd, 0xff00 */
	a = (t[bx + LITH_SIN_QUARTER] >> 2) + 0x1400;   /* img 0x64d4 */
	g_joint[12].ang[0] = (s32)(s16)a;
	g_joint[13].ang[0] = (s32)(s16)(a + 0x800);

	/* img 0x64ef - HALF A CYCLE ON, and the same seven stores land
	 * on the opposite limbs. */
	bx = ((g_phase + 0x1000) & 0x1ffc) >> 2;
	g_joint[4].ang[0] = (s32)(s16)((t[bx + LITH_SIN_QUARTER] >> 2) + 0x244);
	a = (t[bx] >> 2) - 0x2bc;
	g_joint[5].ang[0] = (s32)(s16)a;
	g_joint[6].ang[0] = (s32)(s16)a;
	g_joint[8].ang[0] = (s32)(s16)((t[bx + LITH_SIN_QUARTER] >> 3) + 0xd00);
	g_joint[8].ang[2] = 0x100;                      /* img 0x6537 */
	a = (t[bx + LITH_SIN_QUARTER] >> 2) + 0x1400;
	g_joint[9].ang[0] = (s32)(s16)a;
	g_joint[10].ang[0] = (s32)(s16)(a + 0x800);

	/* img 0x6559 - the torso, off twice the phase plus 0x708. */
	bx = (((g_phase + 0x1000) * 2 + 0x708) & 0x1ffc) >> 2;
	a = t[bx] >> 6;
	g_joint[0].ang[0] = (s32)(s16)a;
	g_joint[7].ang[0] = (s32)(s16)(-a - 0x200);     /* img 0x6574 */

	/* img 0x6583 - and the camera's own bob, also double rate. */
	bx = ((g_phase * 2) & 0x1ffc) >> 2;
	g_lith_slot[LITH_SLOT(0)].pos_y =
		(t[bx + LITH_SIN_QUARTER] >> 0xa) + 0x5a;

	g_phase = (g_phase + g_step) & 0xffff;          /* img 0x659c */
	g_cam_z += 4;                                   /* img 0x65a2 */
	g_lith_slot[LITH_SLOT(0)].ang_z =
		(s32)(s16)(g_lith_slot[LITH_SLOT(0)].ang_z - 1);
	Lith_TickDefault();                             /* img 0x65ae */
}

/* ORIGIN img 0x11b1 to 0x11cc - the frame.
 *
 * THE FIGURE IS DRAWN SHARP AND SHOWN SMEARED. The pass clears its
 * own buffer and renders into it, then the cross-fade moves the
 * accumulator a quarter of the way there - so what reaches the
 * screen trails the run by several frames.
 *
 * WITH THE SMEAR OFF THE ACCUMULATOR IS LEFT STANDING, deliberately:
 * turning it back on blends the pose it was holding when it went
 * off, and that lingering ghost is wanted rather than tolerated. */
void Lith_FinaleFrame(u8 *fb, const u8 *tex, LithScene *sc,
                      LithProj *pv, LithSortEnt *sl)
{
	s32 cp[3], ca[3], n, i;

	memset(g_work, 0, sizeof g_work);       /* img 0x11b1 */

	figure_draw(sc);                        /* img 0x11b7 */

	cp[0] = g_cam_x;
	cp[1] = g_lith_slot[LITH_SLOT(0)].pos_y;
	cp[2] = g_cam_z;
	ca[0] = g_lith_slot[LITH_SLOT(0)].ang_x;
	ca[1] = g_lith_slot[LITH_SLOT(0)].ang_y;
	ca[2] = g_lith_slot[LITH_SLOT(0)].ang_z;

	Lith_SceneProject(pv, sc, cp, ca);      /* img 0x11ba */
	n = Lith_SceneCullSort(sl, pv, sc, g_near);
	for (i = 0; i < n; i++) {               /* img 0x11bd */
		s32 f = sl[i].face, idx[3], k;
		LithPoly p;

		idx[0] = sc->f0[f]; idx[1] = sc->f1[f]; idx[2] = sc->f2[f];
		for (k = 0; k < 3; k++) {
			p.x[k] = pv[idx[k]].sx;
			p.y[k] = pv[idx[k]].sy;
			p.u[k] = pv[idx[k]].u;
			p.v[k] = pv[idx[k]].v;
		}
		Lith_PolyFill(g_work, tex, &p, Lith_FaceBand(sc->attr[f]));
	}

	if (!g_smear) {
		memcpy(fb, g_work, sizeof g_work);
		return;
	}
	Lith_ScreenCrossFade(g_accum, g_work);  /* img 0x11c0 */
	memcpy(fb, g_accum, sizeof g_accum);    /* OURS - the present */
}

/* ORIGIN img 0x11c6 - and the test is the wrong way round from every
 * other scene's: it loops while the order is ABOVE 0xa, so the demo
 * ends when the module wraps back to its start rather than on
 * reaching a position. */
s32 Lith_FinaleDone(void)
{
	return Lith_MusicPos() <= 0xa;
}
