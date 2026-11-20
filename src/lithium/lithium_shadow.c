/* LITHIUM - the craft's shadow.
 *
 * ORIGIN img 0x5526 Tex_RenderObjectTile paints the stencil, img
 * 0x32f1 and 0x3320 place it, img 0x3448 maps it onto each terrain
 * face and img 0x3c18 Poly_SpanPlanar subtracts it.
 * docs/lithium/subsystems/raster.txt section 3.3.
 *
 * IT IS A STENCIL, NOT A MESH. A 128 by 128 map holds the ship seen
 * from straight above, flat-filled with one texel, and the pass
 * re-draws the TERRAIN'S OWN FACES a second time with world-space u
 * and v - so the silhouette lies on the surface and bends with it,
 * and nothing is added to the vertex pool.
 *
 * THE PASS RUNS OVER EVERY TERRAIN FACE and only the silhouette
 * darkens anything, because a zero stencil byte subtracts nothing.
 */

#include <string.h>

#include "lithium.h"

/* 0x7ad2:0x8c00 - the third tenant of the flare's segment. */
static u8 g_stencil[LITH_SHADOW_W * LITH_SHADOW_W];

/* g_PlanarOrgX img 0x8f5f, g_PlanarOrgZ img 0x8f61, g_FocusEased img
 * 0x8ef6. The origin is WORDS in the original and the wrap matters:
 * the subtraction below is 16-bit too. */
static s32 g_org_x, g_org_z;
static s32 g_focus_eased;

const u8 *Lith_ShadowStencil(void) { return g_stencil; }

/* OURS - for the state line. */
void Lith_ShadowOrg(s32 *x, s32 *z) { *x = g_org_x; *z = g_org_z; }
s32 Lith_ShadowFocus(void) { return g_focus_eased; }

/* ORIGIN img 0x5526 Tex_RenderObjectTile.
 *
 * THE SILHOUETTE IS THE SHIP ITSELF, TIPPED TO LOOK DOWN - img
 * 0x5540 sets the X angle to 0x800, a quarter of the 0x2000 turn.
 *
 * THE FILL IS FLAT, NOT SHADED. img 0x5681 and 0x568b hand every
 * vertex u = v = 0x80, so each face samples the one sphere-map texel
 * there and the whole silhouette comes out 0x14 - which is the
 * amount a shadowed pixel loses.
 *
 * IT DRAWS EVERY FACE: no cull, no depth sort and no backface test,
 * since a silhouette wants both sides.
 *
 * img 0x570a then copies a 128 by 128 window from (96,36), which is
 * byte 0x2d60 of a 320-wide buffer. */
void Lith_ShadowBuildStencil(const LithMesh *ship, const u8 *tex,
                             LithScene *sc, LithProj *pv)
{
	static u8 tile[LITH_FB_BYTES];
	s32 cp[3], ca[3], n, k;
	LithObj o;

	memset(tile, 0, sizeof tile);
	memset(g_stencil, 0, sizeof g_stencil);
	Lith_SceneReset(sc);

	Lith_ObjInit(&o);
	o.ang_x = 0x800;                /* img 0x5540 */
	o.pos_z = 0x6000;               /* img 0x5589, through the home copy */
	o.scale_shift = LITH_SCALE_IDENTITY;
	o.rebase_z = 0x6000;            /* img 0x55c5 - slot 0 sits with it */

	/* IT INHERITS THE SWAP RATHER THAN SETTING IT. Nothing in
	 * Tex_RenderObjectTile touches g_ObjAxisSwap, so it still holds
	 * the 1 the opening left - and without it the silhouette comes
	 * out the craft's 120 by 60 PLAN instead of its 120 by 124
	 * outline, a third of the pixels and the wrong shape. */
	o.axis_swap = 1;
	Lith_ObjFaceAttr(LITH_ATTR_SPHERE);
	Lith_MeshTransform(sc, ship, &o);

	cp[0] = 0; cp[1] = 0; cp[2] = 0x7d0;    /* img 0x561b to 0x562f */
	ca[0] = 0; ca[1] = 0; ca[2] = 0;        /* img 0x5639 */
	Lith_SceneProject(pv, sc, cp, ca);

	for (n = 0; n < sc->nface; n++) {
		s32 idx[3];
		LithPoly p;

		idx[0] = sc->f0[n]; idx[1] = sc->f1[n]; idx[2] = sc->f2[n];
		for (k = 0; k < 3; k++) {
			p.x[k] = pv[idx[k]].sx;
			p.y[k] = pv[idx[k]].sy;
			p.u[k] = 0x80;
			p.v[k] = 0x80;
		}
		Lith_PolyFill(tile, tex, &p, LITH_BAND_NONE);
	}

	for (n = 0; n < LITH_SHADOW_W; n++)
		memcpy(g_stencil + n * LITH_SHADOW_W,
		       tile + 0x2d60 + n * PD_SCREEN_W, LITH_SHADOW_W);
}

/* ORIGIN img 0x32f1 to 0x335e - where the stencil lands, once a frame.
 *
 * THE ALTITUDE IS THE LIGHT. g_FocusEased follows the ground under
 * the craft by the usual shift-and-add, so h is the craft's height
 * ABOVE GROUND, and h shifts the origin by (h, h/2) - a direction
 * baked as a slope rather than held as a vector. So the shadow slides
 * away from the craft as it climbs, which is what a directional light
 * does, and it is the whole of the light model.
 *
 * THE CAMERA'S CELL COMES IN TOO, quantised exactly as
 * Mesh_Transform's rebase quantises it - subsystems/mesh3d.txt 3.2 -
 * because the u and v this anchors are measured against vertices that
 * were placed the same way. The two have to agree.
 *
 * THE Z TERM'S NEUTRAL VALUE IS -1 for the same reason it is there.
 *
 * ONCE PER FRAME, NO DIVIDER - a host frame is one tick here, and so
 * is the original's on a quick enough machine. raster.txt 3.3. */
void Lith_ShadowOrigin(const LithSlot *cam, const LithSlot *ship)
{
	s32 h, ax, cx;

	g_focus_eased += (Lith_TerrainHeightAtSlot(ship)
	                  - g_focus_eased) >> 3;
	h = (ship->home_y - g_focus_eased) >> 4;

	ax = h - (ship->pos_x >> 4);
	cx = (ship->pos_z >> 4) + (h >> 1);
	ax += (cam->pos_x >> 0xc) << 8;
	cx += ((-(cam->pos_z + 1)) >> 0xc) << 8;
	g_org_x = (s32)(s16)(u16)ax;
	g_org_z = (s32)(s16)(u16)cx;
}

/* ORIGIN img 0x3448 - one terrain face's u and v, or zero to drop it.
 *
 * u AND v COME FROM THE STASHED PRE-CAMERA x AND z, not from the
 * screen, which is the whole reason the silhouette conforms to the
 * ground instead of sitting flat on it.
 *
 * THE ARITHMETIC IS 16-BIT UNTIL THE ROTATION. The subtraction, the
 * DEC and the SAR 3 are all on words and only then sign-extended, so
 * a vertex far from the origin wraps rather than running away.
 *
 * THE ANGLE IS THE SHIP'S OWN YAW. img 0x346f reads img 0x8aa1,
 * which is g_SlotAngleY at slot 4 - so the stencil turns with the
 * craft. Reading that address as a planar angle of its own is the
 * trap; it is the ship's heading.
 *
 * ONE VERTEX OUTSIDE 0..0x7f DROPS THE WHOLE FACE - img 0x348e and
 * 0x349b test unsigned, so a negative fails the same test. */
int Lith_ShadowFaceUV(LithPoly *p, const LithProj *pv, const s32 *idx,
                      s32 yaw)
{
	s32 k;

	for (k = 0; k < 3; k++) {
		s32 x = (s32)(s16)(u16)(pv[idx[k]].wx - g_org_x);
		s32 z = (s32)(s16)(u16)(pv[idx[k]].wz - g_org_z);
		s32 y = 0;

		x = (s32)((s16)(u16)(x - 1) >> 3);
		z = (s32)((s16)(u16)(z - 1) >> 3);
		Lith_VecRotY(&x, &y, &z, yaw);
		x = (x >> 0xc) + 0x40;
		z = (z >> 0xc) + 0x40;
		if ((u32)x >= 0x80 || (u32)z >= 0x80) return 0;
		p->u[k] = x;
		p->v[k] = z;
	}
	return 1;
}
