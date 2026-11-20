/* LITHIUM - one model into screen coordinates.
 *
 * Vec_RotZ/X/Y, Mesh_Transform, Scene_ProjectVerts and the cull and
 * sort out of Scene_DrawFaces. The pipeline is
 * docs/lithium/subsystems/mesh3d.txt and subsystems/raster.txt.
 *
 * EVERY SHIFT HERE IS LOAD-BEARING. The three rotations are followed
 * by different shifts depending on whether a position or a normal is
 * going through them, and the two chains also differ in their ORDER
 * of axes - see Mesh_Transform and Scene_ProjectVerts.
 */

#include <string.h>

#include "lithium.h"

/* ORIGIN img 0x35e1 Vec_RotZ. x is EAX, y EBX, z ECX. */
void Lith_VecRotZ(s32 *x, s32 *y, s32 *z, s32 ang)
{
	s32 sn = Lith_Sin(ang), cs = Lith_Cos(ang);
	s32 a = *x, b = *y;

	*x = a * cs - b * sn;
	*y = b * cs + a * sn;
	*z = *z << 12;
}

/* ORIGIN img 0x362a Vec_RotX. */
void Lith_VecRotX(s32 *x, s32 *y, s32 *z, s32 ang)
{
	s32 sn = Lith_Sin(ang), cs = Lith_Cos(ang);
	s32 b = *y, c = *z;

	*x = *x << 12;
	*y = b * cs - c * sn;
	*z = c * cs + b * sn;
}

/* ORIGIN img 0x3674 Vec_RotY. */
void Lith_VecRotY(s32 *x, s32 *y, s32 *z, s32 ang)
{
	s32 sn = Lith_Sin(ang), cs = Lith_Cos(ang);
	s32 a = *x, c = *z;

	*x = a * cs - c * sn;
	*y = *y << 12;
	*z = c * cs + a * sn;
}

/* OURS - the object defaults, so a caller sets only what it means.
 *
 * THE FUDGE PAIR IS NOT PER OBJECT, however much carrying it here
 * suggests otherwise. img 0x4e33 and 0x4e44 read a FIXED address -
 * element 0 of g_SlotPosX and g_SlotPosZ - so both copies of a mesh
 * get the same offset whatever slot they came from. It is a scene
 * term; the struct is only where we keep it.
 *
 * SO ITS DEFAULT IS 0, NOT THE VALUE THAT CANCELS. The offset is
 * (-(v + 1)) >> 0xc << 8, so 0 subtracts 256 from z and -1 is what
 * subtracts nothing - and the opening runs with 0. Starting from the
 * cancelling value puts every model 256 units too far away. */
void Lith_ObjInit(LithObj *o)
{
	memset(o, 0, sizeof *o);
	o->scale_shift = LITH_SCALE_IDENTITY;
}

/* g_ObjFaceAttr img 0x8fb2 - set per OBJECT, written into every face
 * record Mesh_Transform emits, at +0x06. subsystems/mesh3d.txt. */
static s32 g_obj_face_attr = LITH_ATTR_SPHERE;

/* OURS - the original writes that global directly. */
void Lith_ObjFaceAttr(s32 attr) { g_obj_face_attr = attr; }

/* ORIGIN img 0x338d - which texture the attribute selects.
 *
 * ATTRIBUTES 0 AND 1 ARE NOT SEGMENTS. Both load the sphere map and
 * differ only in the band added to the texel, which is how one map
 * becomes the gold ships, the cyan surface and the purple tower -
 * subsystems/raster.txt section 4. */
s32 Lith_FaceSegment(s32 attr)
{
	return attr <= 1 ? LITH_ATTR_SPHERE : attr;
}

/* ORIGIN img 0x3398 and 0x33a9 - the band those two add.
 *
 * IT DOES NOT CARRY FROM ONE FACE TO THE NEXT. img 0x3366 stores the
 * plain span routine and IS the loop's own back edge, so every face
 * starts from plain and only 0 and 1 override. */
s32 Lith_FaceBand(s32 attr)
{
	if (attr == 0) return LITH_BAND_40;
	if (attr == 1) return LITH_BAND_C0;
	return LITH_BAND_NONE;
}

/* OURS - img 0x2e48 Scene_ProjectVerts and img 0x31ca
 * Scene_DrawFaces, which the runner calls as a pair three times a
 * frame. Naming the pair once keeps the three call sites readable;
 * the arithmetic is theirs. */
void Lith_SceneDraw(u8 *fb, const u8 *tex, LithScene *sc,
                    LithProj *pv, LithSortEnt *sl,
                    const s32 cp[3], const s32 ca[3], s32 depth_near)
{
	s32 n, i;

	Lith_SceneProject(pv, sc, cp, ca);
	n = Lith_SceneCullSort(sl, pv, sc, depth_near);
	for (i = 0; i < n; i++) {
		s32 f = sl[i].face, idx[3], k;
		LithPoly p;

		idx[0] = sc->f0[f];
		idx[1] = sc->f1[f];
		idx[2] = sc->f2[f];
		for (k = 0; k < 3; k++) {
			p.x[k] = pv[idx[k]].sx;
			p.y[k] = pv[idx[k]].sy;
			p.u[k] = pv[idx[k]].u;
			p.v[k] = pv[idx[k]].v;
		}
		Lith_PolyFill(fb, tex, &p, Lith_FaceBand(sc->attr[f]));
	}
}

/* ORIGIN img 0x4d2c Mesh_Transform. One model into the scene pool.
 *
 * U AND V ARE THE ENVIRONMENT MAP AND THEY ARE THE CHROME: a
 * view-space normal biased by +0x80 into a 256-wide texture. Nothing
 * else makes the surface look metallic.
 *
 * THE TWO CHAINS ARE NOT THE SAME. A position rotates Z, X, Y with a
 * SAR 0xa between each; a normal takes the same three with SAR 0xc,
 * its Y angle offset by a quarter circle, and then a FOURTH rotation
 * about Z by the env-map angle - which is what turns the reflection
 * independently of the geometry, effects/rockintro.txt section 4.
 *
 * The vertices come from the loaded copy rather than being re-read
 * out of the image; Mesh_Load stores the authored bytes unchanged,
 * so it is the same data by a shorter route.
 *
 * EVERY FACE IT EMITS CARRIES g_ObjFaceAttr, img 0x8fb2, which is set
 * per OBJECT and not per face - subsystems/mesh3d.txt. */
void Lith_MeshTransform(LithScene *sc, const LithMesh *m, const LithObj *o)
{
	s32 i, f;
	s32 vbase = sc->nvert, fbase = sc->nface;

	/* IT APPENDS. The runner zeroes g_VertPoolCount once a frame and
	 * then calls this per object, so two copies of one mesh share a
	 * pool and the faces are rebased onto it - img 0x1635. */
	if (vbase + m->nvert > LITH_POOL_MAXV) return;
	if (fbase + m->nface > LITH_POOL_MAXF) return;
	sc->nvert = vbase + m->nvert;
	sc->nface = fbase + m->nface;
	for (i = 0; i < m->nvert; i++) {
		s32 x = m->vx[i], y = m->vy[i], z = m->vz[i];
		s32 nx = m->nxv[i], ny = m->nyv[i], nz = m->nzv[i];
		s32 t;

		/* img 0x4d9f, 8-bit - effects/rockintro.txt 6.1. */
		if (o->axis_swap) {
			t = y; y = (s32)(s8)(-z); z = t;
		}
		Lith_VecRotZ(&x, &y, &z, -(o->ang_z * 2));
		x >>= 0xa; y >>= 0xa; z >>= 0xa;
		Lith_VecRotX(&x, &y, &z, -o->ang_x);
		x >>= 0xa; y >>= 0xa; z >>= 0xa;
		Lith_VecRotY(&x, &y, &z, -o->ang_y);
		x >>= o->scale_shift; y >>= o->scale_shift; z >>= o->scale_shift;
		x -= o->pos_x >> 4;
		y -= o->pos_y >> 4;
		z += o->pos_z >> 4;
		x += (o->rebase_x >> 0xc) << 8;
		z += ((-(o->rebase_z + 1)) >> 0xc) << 8;

		/* img 0x4e82 - the same swap, on WORDS. */
		if (o->axis_swap) {
			t = ny; ny = (s32)(s16)(-nz); nz = t;
		}
		Lith_VecRotZ(&nx, &ny, &nz, -(o->ang_z * 2));
		nx >>= 0xc; ny >>= 0xc; nz >>= 0xc;
		Lith_VecRotX(&nx, &ny, &nz, -o->ang_x);
		nx >>= 0xc; ny >>= 0xc; nz >>= 0xc;
		Lith_VecRotY(&nx, &ny, &nz, -o->ang_y - 0x400);
		nx >>= 0xb; ny >>= 0xb;
		Lith_VecRotZ(&nx, &ny, &nz, o->env_angle);
		if (o->mirror) { nx = -nx; ny = -ny; }

		/* The record's five words, so each is kept to 16 bits. */
		sc->v[vbase + i].x = (s32)(s16)(u16)x;
		sc->v[vbase + i].y = (s32)(s16)(u16)y;
		sc->v[vbase + i].z = (s32)(s16)(u16)z;
		sc->v[vbase + i].u = (s32)(s16)(u16)((nx >> 0x11) + 0x80);
		sc->v[vbase + i].v = (s32)(s16)(u16)((ny >> 0x11) + 0x80);
	}

	for (f = 0; f < m->nface; f++) {
		s32 i0 = m->f0[f], i1 = m->f1[f], i2 = m->f2[f];

		if (o->mirror) {                /* XCHG AX,CX reverses it */
			s32 t = i0; i0 = i2; i2 = t;
		}
		sc->f0[fbase + f] = vbase + i0;
		sc->f1[fbase + f] = vbase + i1;
		sc->f2[fbase + f] = vbase + i2;
		sc->attr[fbase + f] = g_obj_face_attr;
	}
}

/* OURS - img 0x1635 clears g_VertPoolCount and g_FacePoolCount
 * together in one dword store; a frame starts from here. */
void Lith_SceneReset(LithScene *sc)
{
	sc->nvert = 0;
	sc->nface = 0;
}

/* ORIGIN img 0x2e48 Scene_ProjectVerts.
 *
 * THE CAMERA ROTATES Y THEN X THEN Z, where Mesh_Transform rotates Z
 * then X then Y. They are not the same sequence and swapping them
 * tilts the whole scene without obviously breaking it. */
void Lith_SceneProject(LithProj *out, const LithScene *sc,
                       const s32 cam_pos[3], const s32 cam_ang[3])
{
	s32 i;

	for (i = 0; i < sc->nvert; i++) {
		s32 x = sc->v[i].x, y = sc->v[i].y, z = sc->v[i].z;
		s32 u = sc->v[i].u, v = sc->v[i].v;
		s32 sx, sy, d;

		out[i].wx = x;                  /* stashed at +0x0c and +0x0e */
		out[i].wz = z;
		x += cam_pos[0]; y += cam_pos[1]; z += cam_pos[2];
		Lith_VecRotY(&x, &y, &z, cam_ang[1]);
		x >>= 0xa; y >>= 0xa; z >>= 0xa;
		Lith_VecRotX(&x, &y, &z, cam_ang[0]);
		x >>= 0xa; y >>= 0xa; z >>= 0xa;
		Lith_VecRotZ(&x, &y, &z, cam_ang[2]);
		x >>= 0xa; y >>= 0xa; z >>= 0xa;

		if (z < LITH_NEAR_Z) z = LITH_NEAR_Z;   /* a CLAMP, not a reject */
		sx = lith_idiv_shl8(x, z);      /* img 0x2ef1, 64-bit */
		sy = lith_idiv_shl8(y, z);
		if (sx >  LITH_GUARD) sx =  LITH_GUARD;
		if (sx < -LITH_GUARD) sx = -LITH_GUARD;
		if (sy >  LITH_GUARD) sy =  LITH_GUARD;
		if (sy < -LITH_GUARD) sy = -LITH_GUARD;

		/* THE DEPTH FADE IS ON THE ENV COORDINATES THEMSELVES, and
		 * the divide is UNSIGNED with u and v zero-extended. */
		d = (z - LITH_FADE_BIAS) >> 7;
		if (d >= 0x100) {
			u = (s32)((((u32)(u & 0xffff)) << 0xf) / (u32)d) >> 7;
			v = (s32)((((u32)(v & 0xffff)) << 0xf) / (u32)d) >> 7;
		}
		out[i].sx = sx + LITH_CENTRE_X;
		out[i].sy = sy + LITH_CENTRE_Y;
		out[i].z = z;
		out[i].u = u;
		out[i].v = v;
	}
}

/* OURS - insertion sort, far to near. See the direction note below. */
static void sort_desc(LithSortEnt *e, s32 n)
{
	s32 i, j;

	for (i = 1; i < n; i++) {
		LithSortEnt t = e[i];
		for (j = i; j > 0 && e[j - 1].key < t.key; j--)
			e[j] = e[j - 1];
		e[j] = t;
	}
}

/* ORIGIN img 0x5fda Clip_CountOutside. IT DOES NOT CLIP - it never
 * touches a coordinate. It TALLIES, one counter per screen edge, and
 * img 0x31fb seeds all four at once with 0x01010101.
 *
 * A counter reaches 4 only when ALL THREE vertices were outside that
 * same edge, so img 0x325a tests the dword against 0xfcfcfcfc - the
 * low two bits masked off - and rejects the face in one compare
 * instead of three. It is a trivial reject, not a clip, so it only
 * ever REMOVES faces. */
static int clip_reject(const LithProj *a, const LithProj *b,
                       const LithProj *c)
{
	u32 codes = 0x01010101UL;
	const LithProj *v[3];
	s32 k;

	v[0] = a; v[1] = b; v[2] = c;
	for (k = 0; k < 3; k++) {
		if (v[k]->sx < 0)                   codes += 0x00000001UL;
		else if (v[k]->sx >= PD_SCREEN_W)   codes += 0x00000100UL;
		if (v[k]->sy < 0)                   codes += 0x00010000UL;
		else if (v[k]->sy >= PD_SCREEN_H)   codes += 0x01000000UL;
	}
	return (codes & 0xfcfcfcfcUL) != 0;
}

/* ORIGIN img 0x31ca, pass one of Scene_DrawFaces.
 *
 * THE BACKFACE TEST IS A 2D CROSS PRODUCT ABOUT VERTEX 2, taken on
 * the SCREEN coordinates after projection rather than in view space.
 *
 * FAR TO NEAR IS READ, NOT GUESSED - Sort_ByDepth at img 0x3e30
 * sorts descending on the summed z, so the list is painter order.
 *
 * THE TRIVIAL REJECT COMES FIRST, before the cross product - img
 * 0x325a tests the codes and only then computes the winding. */
s32 Lith_SceneCullSort(LithSortEnt *out, const LithProj *pv,
                       const LithScene *sc, s32 depth_near)
{
	s32 n, kept = 0;

	for (n = 0; n < sc->nface; n++) {
		s32 i0 = sc->f0[n], i1 = sc->f1[n], i2 = sc->f2[n];
		s32 x0 = pv[i0].sx, y0 = pv[i0].sy;
		s32 x1 = pv[i1].sx, y1 = pv[i1].sy;
		s32 x2 = pv[i2].sx, y2 = pv[i2].sy;
		s32 ebp;

		if (clip_reject(&pv[i0], &pv[i1], &pv[i2])) continue;
		if ((x0 - x2) * (y1 - y2) - (y0 - y2) * (x1 - x2) < 0) continue;
		ebp = pv[i0].z + pv[i1].z + pv[i2].z;
		if (ebp <= depth_near || ebp > LITH_FAR_Z) continue;
		out[kept].key = ebp >> 8;
		out[kept].face = n;
		kept++;
	}
	sort_desc(out, kept);
	return kept;
}
