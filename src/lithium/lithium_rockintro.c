/* LITHIUM - the opening.
 *
 * ORIGIN img 0x150b maybe_Scene_RockIntro and img 0x6294
 * Tick_RockIntro. docs/lithium/effects/rockintro.txt.
 *
 * ONE RUNNER PRODUCES ALL THREE OF THE OPENING'S APPARENT SCENES -
 * the warping chrome surface, the rock that moves in, and the logo.
 * There is no dispatcher between them and no second runner.
 *
 * THE SURFACE AND THE ROCK ARE THE SAME MESH AT TWO SCALES. Both
 * slots load 0x41ea and THE SMALLER SHIFT IS THE LARGER OBJECT: at
 * 0xe the mesh is four times its size at 0x10, far enough past the
 * near plane that only a patch of its curvature is on screen.
 */

#include <string.h>

#include "lithium.h"

/* THE TWO SLOTS, 4 and 8. Six rates for two objects, none of them
 * shared, so the copies tumble on unrelated schedules and never look
 * like one thing twice - img 0x6294. */
static LithObj g_slot4, g_slot8;
static s32 g_env_angle;
static s32 g_logo_zoom;
static s32 g_phase2;            /* the handler img 0x6337 takes over */
static s32 g_frame_phase;       /* OURS - see Lith_RockIntroFrame   */
static s32 g_blit_phase;        /* g_BlitPhase, img 0x8f9f           */
static s32 g_blit_src_ofs;      /* g_BlitSrcOfs, img 0x8fd4          */

static void tick_phase2(void);

/* The title card, unpacked into what the listing calls segment
 * 0x5ad2 and then blurred TWICE - img 0x151c with CX = 2. */
static u8 g_title[LITH_TEX_BYTES];

/* THE BACK BUFFER AND THE BLEND BUFFER ARE DIFFERENT BUFFERS. The
 * geometry and the title go into this one - the listing's 0x4ad2 -
 * and Screen_CrossFade moves the visible one a quarter of the way
 * toward it. Drawing straight into the visible buffer loses the
 * smear the demo is built around. */
static u8 g_back[PD_SCREEN_W * PD_SCREEN_H];

/* ORIGIN img 0x150b, the setup before the loop.
 *
 * img 0x150e's blend clear is in Demo_Init. */
void Lith_RockIntroEnter(void)
{
	Lith_ObjInit(&g_slot4);
	Lith_ObjInit(&g_slot8);
	/* img 0x1606 - set ONCE in the head, so it is both slots'. Only
	 * the scale and the mirror change per slot inside the loop. */
	g_slot4.axis_swap = 1;
	g_slot8.axis_swap = 1;
	/* THE SMALLER SHIFT IS THE LARGER OBJECT. */
	g_slot4.scale_shift = 0xe;
	g_slot4.mirror = 1;
	g_slot8.scale_shift = 0x10;
	g_slot8.mirror = 0;
	g_slot4.pos_z = 0x1b58;         /* g_SlotPosZ[4] */
	g_slot8.pos_z = 0x6590;         /* g_SlotPosZ[8] */
	g_slot8.ang_z = 0xa00;          /* img 0x15b5 */
	Lith_ImageUnpackRle(g_title, LITH_TITLE_RLE);
	Lith_ImageBlur(g_title, 2);     /* img 0x151c, CX = 2 */
	memset(g_back, 0, sizeof g_back);
	g_env_angle = 0;
	g_blit_phase = 0;
	g_blit_src_ofs = LITH_BLIT_VISTA;
	g_logo_zoom = 0x2200;
	g_phase2 = 0;
	Lith_PalRampShift(1);           /* img 0x1520 */
	Lith_PolyReset();
	g_frame_phase = 0;
}

/* ORIGIN img 0x6294 Tick_RockIntro.
 *
 * THE SMALL ONE BOBS BECAUSE ITS HEIGHT IS A FUNCTION OF ITS DEPTH -
 * g_SlotPosY[8] is a sine lookup on its own Z. effects/rockintro.txt
 * section 3.
 *
 * IT HANDS OFF RATHER THAN ENDING. Once the zoom passes zero the
 * handler rewrites g_TickHandler to img 0x6337, so the opening is two
 * motion phases inside one runner. */
void Lith_RockIntroTick(void)
{
	const s32 *t = Lith_SinTable();
	s32 bx;

	if (g_phase2) { tick_phase2(); return; }

	g_slot4.ang_x += 0x10;
	g_slot4.ang_y += 0x0c;
	g_slot4.ang_z += 0x0a;

	g_slot8.pos_z -= 0x20;
	g_slot8.ang_x += 0x05;
	g_slot8.ang_y += 0x07;
	g_slot8.ang_z += 0x0d;

	bx = ((g_slot8.pos_z >> 1) + 0x200) & 0x1ffc;
	if (t) g_slot8.pos_y = (t[bx >> 2] >> 1) - 0x48;

	g_env_angle += 0x10;
	g_logo_zoom -= 8;
	if (g_logo_zoom < 0x1900) g_blit_phase += 0xa;

	Lith_PalAnimate(Lith_PalStore());       /* img 0x6304 */
	Lith_PalRefresh();                      /* img 0x6307 */

	/* img 0x630a - THE HANDOFF IS THE VISTA-TO-LITHIUM SWITCH. It
	 * moves the blit to the OTHER HALF of the one picture and
	 * restarts the zoom from behind the camera. */
	if (g_logo_zoom < 0) {
		g_blit_src_ofs = LITH_BLIT_LITH;        /* img 0x6315 */
		g_blit_phase = -0x100;
		g_logo_zoom = -0x200;
		g_phase2 = 1;           /* g_TickHandler = img 0x6337 */
	}
	Lith_TickDefault();                     /* img 0x6334 - JMP */
}

/* ORIGIN img 0x6337 Tick_RockIntro2 - the second motion phase.
 *
 * THE ROCKS DO NOT NOTICE THE HANDOFF: every rate below is phase
 * one's. Only the logo moves differently, and it EASES OUTWARD from
 * behind the camera where phase one walked inward -
 * effects/rockintro.txt section 4.1. */
static void tick_phase2(void)
{
	const s32 *t = Lith_SinTable();
	s32 bx;

	g_slot4.ang_x += 0x10;
	g_slot4.ang_y += 0x0c;
	g_slot4.ang_z += 0x0a;

	g_slot8.pos_z -= 0x20;
	g_slot8.ang_x += 0x05;
	g_slot8.ang_y += 0x07;
	g_slot8.ang_z += 0x0d;

	bx = ((g_slot8.pos_z >> 1) + 0x200) & 0x1ffc;
	if (t) g_slot8.pos_y = (t[bx >> 2] >> 1) - 0x48;

	g_env_angle += 0x10;
	g_logo_zoom += (0x800 - g_logo_zoom) >> 9;      /* img 0x6368 */
	g_blit_phase += 9;

	Lith_PalAnimate(Lith_PalStore());       /* img 0x6384 */
	Lith_PalRefresh();                      /* img 0x6387 */

	/* img 0x638a - arming this is what ends the part. */
	if (Lith_MusicPos() >= 6) Lith_PalFadeTo(-0x100);
	Lith_TickDefault();                     /* img 0x639b - JMP */
}

/* ORIGIN img 0x1635, the per-frame body.
 *
 * SLOT 8 IS DEPTH-GATED, NOT TIMED - img 0x166c skips it unless its
 * Z lies between -0x1f4 and 0x2cec.
 *
 * The title goes over the geometry last and additively - img 0x16b6,
 * and only Screen_CrossFade at img 0x1850 reaches the screen.
 *
 * THE BLEND RUNS EVERY LITH_FRAME_TICKS, NOT EVERY TICK -
 * effects/rockintro.txt 7.1.
 *
 * BOTH SLOTS CARRY FACE ATTRIBUTE 0x6ad2 - img 0x1662 and 0x16a6 -
 * so the band added to every texel is zero and the sphere map is
 * read as it stands. subsystems/raster.txt section 3.3. */
void Lith_RockIntroFrame(u8 *fb, const LithMesh *rock, const u8 *tex,
                         LithScene *sc, LithProj *pv, LithSortEnt *sl)
{
	s32 cp[3], ca[3], n, i;

	Lith_SceneReset(sc);            /* g_VertPoolCount = 0 */

	g_slot4.env_angle = g_env_angle;
	Lith_ObjFaceAttr(LITH_ATTR_SPHERE);
	Lith_MeshTransform(sc, rock, &g_slot4);

	if (g_slot8.pos_z > -0x1f4 && g_slot8.pos_z < 0x2cec) {
		g_slot8.env_angle = g_env_angle;
		Lith_ObjFaceAttr(LITH_ATTR_SPHERE);
		Lith_MeshTransform(sc, rock, &g_slot8);
	}

	cp[0] = cp[1] = cp[2] = 0;
	ca[0] = ca[1] = ca[2] = 0;
	Lith_SceneProject(pv, sc, cp, ca);
	n = Lith_SceneCullSort(sl, pv, sc, LITH_ROCKINTRO_DEPTH_NEAR);

	memset(g_back, 0, sizeof g_back);
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
		/* g_ObjFaceAttr = 0x6ad2 for both slots, which is the sphere
		 * map read with no band offset - raster.txt section 4. */
		Lith_PolyFill(g_back, tex, &p, Lith_FaceBand(sc->attr[f]));
	}

	{
		LithBlit b;

		b.zoom = g_logo_zoom;
		b.src_w = LITH_BLIT_SRC_W;
		b.src_h = LITH_BLIT_SRC_H;
		b.centre_x = 0xa0;
		b.centre_y = 0x64;
		b.src_ofs = g_blit_src_ofs;
		b.phase = g_blit_phase;
		Lith_ImageBlitScaled(g_back, g_title, &b);
	}

	if (++g_frame_phase >= LITH_FRAME_TICKS) {
		g_frame_phase = 0;
		Lith_ScreenCrossFade(fb, g_back);
	}
}

/* ORIGIN img 0x16bf and 0x16c9, the runner's loop test, negated.
 * BOTH HAVE TO HOLD, and why neither suffices is
 * effects/rockintro.txt section 6. */
s32 Lith_RockIntroDone(void)
{
	return Lith_MusicPos() >= 4 && Lith_PalFade() < -160;
}

/* OURS - what a caller needs to see without reaching into the slots. */
s32 Lith_RockIntroSlot8Z(void) { return g_slot8.pos_z; }
s32 Lith_RockIntroZoom(void) { return g_logo_zoom; }
