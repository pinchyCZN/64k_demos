/* XCURSION - the object renderer. formats/mesh.txt.
 *
 * DIVISION TRUNCATES TOWARD ZERO HERE, as x86 IDIV does. The Python
 * in sim/ uses // on several of these, which FLOORS, so the two can
 * differ by one on a negative coordinate - this is the correct one.
 */

#include <string.h>

#include "xcursion.h"

/* ORIGIN img 0x4a22 - Vec_Rotate3. RETURNS THE RESULT x64.
 *
 * THE SHIFT PATTERN IS NOT SYMMETRIC AND MUST NOT BE MADE SO. Blocks
 * one and two shift results that feed later blocks, which would
 * otherwise overflow 32 bits; all three outputs end one 6-bit shift
 * short of a unit rotation, so callers that want unit scale shift. */
void Xcur_VecRotate3(s32 *px, s32 *py, s32 *pz,
                     s32 pitch, s32 yaw, s32 roll)
{
	s32 x = *px, y = *py, z = *pz, s, c, t0, t1;

	s = Xcur_Sin(roll);  c = Xcur_Cos(roll);
	t0 = (x * s) >> 6;   t1 = (y * s) >> 6;
	x = (((x * c) >> 6) - t1) >> 6;
	y = (((y * c) >> 6) + t0) >> 6;

	s = Xcur_Sin(pitch); c = Xcur_Cos(pitch);
	t0 = (y * s) >> 6;   t1 = (z * s) >> 6;
	y = ((y * c) >> 6) - t1;
	z = (((z * c) >> 6) + t0) >> 6;

	s = Xcur_Sin(yaw);   c = Xcur_Cos(yaw);
	t0 = (x * s) >> 6;   t1 = (z * s) >> 6;
	*px = ((x * c) >> 6) - t1;
	*py = y;
	*pz = ((z * c) >> 6) + t0;
}

/* OURS - the demo reads the model inline and no parse routine is
 * named, so this is our reader of its format - formats/mesh.txt
 * section 1. Indices are ONE
 * BASED and a triangle repeats its third, which is what lets the
 * normal code average four unconditionally. */
int Xcur_MeshParse(XcurModel *m, u32 base)
{
	s32 i, fbase, f;

	if (!g_img) return 1;
	m->nvert = (s32)RD_U16(g_img + base);
	if (m->nvert < 1 || m->nvert > XCUR_MESH_MAXV) return 1;
	for (i = 0; i < m->nvert; i++) {
		const u8 *p = g_img + base + 2 + i * 3;
		m->vx[i] = (s8)p[0];
		m->vy[i] = (s8)p[1];
		m->vz[i] = (s8)p[2];
	}
	fbase = (s32)base + 2 + m->nvert * 3;
	m->nface = g_img[fbase];
	if (m->nface < 1 || m->nface > XCUR_MESH_MAXF) return 1;
	for (f = 0; f < m->nface; f++) {
		const u8 *o = g_img + fbase + 1 + f * 10;
		s32 k, cnt = o[0];
		if (cnt < 3 || cnt > 4) return 1;
		m->fcnt[f] = (u8)cnt;
		m->fattr[f] = o[1];
		for (k = 0; k < 4; k++) {
			s32 v = (s32)RD_U16(o + 2 + k * 2) - 1;
			if (v < 0 || v >= m->nvert) v = 0;
			m->fidx[f][k] = v;
		}
		/* A triangle's fourth slot repeats the third. */
		for (k = cnt; k < 4; k++) m->fidx[f][k] = m->fidx[f][cnt - 1];
	}
	return 0;
}

/* ORIGIN img 0x457f - the descending search from 0x4000.
 *
 * IT EXITS ON ITS FIRST COMPARISON once the length exceeds 0x4000, so
 * a long normal is divided by 0x4000 and not by its length. Zero
 * returns one, which is also what stops a divide by zero. */
static s32 isqrt_demo(s32 sq)
{
	s32 r, b;

	if (sq <= 0) return 1;
	if (sq >= 0x4000L * 0x4000L) return 0x4000;
	r = 0;
	for (b = 1L << 14; b; b >>= 1) {
		s32 t = r + b;
		if (t * t <= sq) r = t;
	}
	/* nearest, not floor */
	if ((r + 1) * (r + 1) - sq < sq - r * r) r++;
	return r ? r : 1;
}

/* ORIGIN img 0x4481 - the cross product, then that normalise. */
static void face_normal(const s32 *rx, const s32 *ry, const s32 *rz,
                        const s32 *ii, s32 cx, s32 cy, s32 cz,
                        s32 *onx, s32 *ony, s32 *onz)
{
	s32 ax = rx[ii[0]] - cx, ay = ry[ii[0]] - cy, az = rz[ii[0]] - cz;
	s32 bx = rx[ii[1]] - cx, by = ry[ii[1]] - cy, bz = rz[ii[1]] - cz;
	/* img 0x44c6, 0x450e and 0x4556 store each component as a WORD
	 * before the length is taken, so both stages truncate. */
	s32 nx = (s32)(s16)((ay * bz - az * by) >> 16);
	s32 ny = (s32)(s16)((az * bx - ax * bz) >> 16);
	s32 nz = (s32)(s16)((ax * by - ay * bx) >> 16);
	s32 c = isqrt_demo(nx * nx + ny * ny + nz * nz);

	*onx = (s32)(s16)((nx << 12) / c);
	*ony = (s32)(s16)((ny << 12) / c);
	*onz = (s32)(s16)((nz << 12) / c);
}

/* ORIGIN img 0x43d4 - and THE NORMALS ARE COMPUTED ONCE, NOT PER
 * FRAME.
 *
 * It puts the model in a FIXED REFERENCE POSE - zero angles and a
 * translation of (0, 0, 0x1e000), img 0x43d4 to 0x4400 - takes each
 * face's cross product there, normalises it, and stores it as three
 * words in the vertex segment at offset 0x8000, ten bytes a face.
 * Mesh_Draw reads that stored normal and rotates it by the object's
 * own angles ONCE, at img 0x47b5.
 *
 * TAKING THE NORMAL FROM THE ROTATED VERTICES AND THEN ROTATING IT IS
 * A DOUBLE ROTATION. The shading still moves with the object, so it
 * reads as lighting - it simply points somewhere else, and swings to
 * the wrong side as the craft turns.
 *
 * Mesh_Draw writes its sort key into the same record's first dword,
 * img 0x4772, which is why the table lives with the vertices. */
void Xcur_MeshNormals(XcurModel *m, XcurMeshCam *c, s32 zscale)
{
	s32 rx[XCUR_MESH_MAXV], ry[XCUR_MESH_MAXV], rz[XCUR_MESH_MAXV];
	s32 i, f;

	/* img 0x43d4 - IT OVERWRITES THE OBJECT TRANSLATION and leaves it
	 * overwritten. A part that sets its own z and then builds normals
	 * gets 0x1e000 back, not what it asked for. */
	c->tx = 0;
	c->ty = 0;
	c->tz = 0x1e000L;

	for (i = 0; i < m->nvert; i++) {
		s32 x = (s32)m->vx[i] << 3;
		s32 y = (s32)m->vy[i] << 3;
		s32 z = ((s32)m->vz[i] * zscale) >> 5;

		Xcur_VecRotate3(&x, &y, &z, 0, 0, 0);
		z += c->tz;                     /* img 0x43e8 */
		if (z < 0x2328) z = 0x2328;
		rx[i] = x; ry[i] = y; rz[i] = z;
	}
	for (f = 0; f < m->nface; f++) {
		const s32 *ii = m->fidx[f];
		s32 cx, cy, cz;

		/* img 0x441f - all four indices, and a triangle's fourth
		 * repeats its third. */
		cx = (rx[ii[0]] + rx[ii[1]] + rx[ii[2]] + rx[ii[3]]) >> 2;
		cy = (ry[ii[0]] + ry[ii[1]] + ry[ii[2]] + ry[ii[3]]) >> 2;
		cz = (rz[ii[0]] + rz[ii[1]] + rz[ii[2]] + rz[ii[3]]) >> 2;
		face_normal(rx, ry, rz, ii, cx, cy, cz,
		            &m->fnx[f], &m->fny[f], &m->fnz[f]);
	}
}

/* OURS. EVERY ROW, not just the bounds. poly_fill only clears the rows the
 * last face touched, so the whole buffer has to start empty or an
 * untouched row's left edge stays at zero and its span runs to the
 * screen edge. */
void Xcur_SpanReset(XcurSpan *b)
{
	s32 row;

	for (row = 0; row < PD_SCREEN_H; row++) {
		b->left[row] = 0x140;
		b->right[row] = -1;
	}
	b->lo = PD_SCREEN_H;
	b->hi = -1;
}

/* ORIGIN img 0x4b2c - Poly_Fill. Four vertices, convex, Gouraud.
 *
 * THE SPAN BUFFER IS REUSED and only the rows the last polygon
 * touched are cleared, bounded by its own min and max - img 0x4b4e.
 * Clearing all 200 rows a face costs several times the frame. */
static void poly_fill(u8 *fb, const s32 *vx, const s32 *vy,
                      const s32 *sh, s32 band, XcurSpan *b)
{
	s32 row, e, lo_y = PD_SCREEN_H, hi_y = -1;

	for (row = b->lo < 0 ? 0 : b->lo; row <= b->hi; row++) {
		b->left[row] = 0x140;
		b->right[row] = -1;
	}
	b->lo = PD_SCREEN_H;
	b->hi = -1;

	for (e = 0; e < 4; e++) {
		s32 x0 = vx[e], y0 = vy[e], s0 = sh[e];
		s32 x1 = vx[(e + 1) & 3], y1 = vy[(e + 1) & 3];
		s32 s1 = sh[(e + 1) & 3];
		s32 dy, gx, gs, ax, as;

		if (y0 > y1) {
			s32 t;
			t = x0; x0 = x1; x1 = t;
			t = y0; y0 = y1; y1 = t;
			t = s0; s0 = s1; s1 = t;
		}
		if (y0 >= PD_SCREEN_H || y1 <= 0 || y1 == y0) continue;
		dy = y1 - y0;
		gx = ((x1 - x0) << 12) / dy;
		gs = ((s1 - s0) << 16) / dy;
		ax = (x0 << 12) + (gx >> 1);
		as = (s0 << 16) + (gs >> 1);
		for (row = y0; row < y1 && row < PD_SCREEN_H; row++) {
			if (row >= 0) {
				s32 px = (ax - 0x800) >> 12;
				if (b->left[row] > px) {
					b->left[row] = px;
					b->sl[row] = as;
				}
				if (b->right[row] < px) {
					b->right[row] = px;
					b->sr[row] = as;
				}
				if (row < b->lo) b->lo = row;
				if (row > b->hi) b->hi = row;
				if (row < lo_y) lo_y = row;
				if (row > hi_y) hi_y = row;
			}
			ax += gx;
			as += gs;
		}
	}

	for (row = lo_y < 0 ? 0 : lo_y; row <= hi_y; row++) {
		s32 left = b->left[row], right = b->right[row];
		s32 sl = b->sl[row], sr = b->sr[row], n, step, x, acc, base;

		if (left > 0x13f || right < 0) continue;
		if (left < 0) left = 0;
		if (right > 0x13f) right = 0x13f;
		if (right < left) continue;
		n = right - left + 1;
		step = (sr - sl) / n;
		base = row * PD_SCREEN_W;
		acc = sl;
		for (x = left; x <= right; x++) {
			s32 v = acc >> 16;
			if (v < 0) v = 0;
			if (v > 0x3f) v = 0x3f;
			fb[base + x] = (u8)(v + band);
			acc += step;
		}
	}
}

/* ORIGIN img 0x45eb - Mesh_SetupTransform. The object goes AT THE
 * CAMERA'S TARGET, facing along its heading - formats/mesh.txt 3.0.
 *
 * Returns the object z, because img 0x2376 gates the draw on it:
 * the craft is skipped once it is further than 0x28000. */
s32 Xcur_MeshSetup(XcurMeshCam *o, s32 cam_x, s32 cam_y, s32 cam_angle,
                   s32 scale, s32 tgt_x, s32 tgt_y, s32 y_term)
{
	s32 x = tgt_x - cam_x;
	/* y_term is g_9460 and is NOT zero - mesh.txt 3.0.1. */
	s32 y = -((y_term - scale) << 5);
	s32 z = tgt_y - cam_y;

	Xcur_VecRotate3(&x, &y, &z, 0, cam_angle, 0);
	/* THE SHIFT Mesh_TransformVerts OMITS - img 0x4634. */
	x >>= 6; y >>= 6; z >>= 6;
	z -= z >> 3;                /* img 0x4643 - times 7/8 */
	o->tx = x; o->ty = y; o->tz = z;
	return z;
}

/* ORIGIN img 0x465c - Mesh_Draw. The whole object renderer.
 *
 * Cull, light, sort, fill - formats/mesh.txt section 5, which also
 * has why the light turns with the object and why that is the
 * binary's doing rather than a fault here.
 */
s32 Xcur_MeshDraw(u8 *fb, const XcurModel *m, const XcurMeshCam *c,
                  XcurSpan *b)
{
	s32 rx[XCUR_MESH_MAXV], ry[XCUR_MESH_MAXV], rz[XCUR_MESH_MAXV];
	s32 sx[XCUR_MESH_MAXV], sy[XCUR_MESH_MAXV];
	s32 accum[XCUR_MESH_MAXV], touch[XCUR_MESH_MAXV];
	s32 shade[XCUR_MESH_MAXV];
	s32 key[XCUR_MESH_MAXF];
	s32 i, f, drawn = 0, yaw_latch;

	/* img 0x4664 - an object's yaw is RELATIVE TO THE CAMERA. */
	yaw_latch = (c->yaw + c->cam_yaw) & 0xffff;

	for (i = 0; i < m->nvert; i++) {
		s32 x = (s32)m->vx[i] << 3;
		s32 y = (s32)m->vy[i] << 3;
		s32 z = ((s32)m->vz[i] * c->zscale) >> 5;
		s32 d;

		Xcur_VecRotate3(&x, &y, &z, c->pitch, yaw_latch, c->roll);
		/* NO >> 6 HERE - formats/mesh.txt section 4. Putting it in,
		 * as the symmetry invites, collapses every model to about
		 * four pixels across. */
		x += c->tx;
		y += c->ty;
		z += c->tz;
		if (z < 0x2328) z = 0x2328;
		rx[i] = x; ry[i] = y; rz[i] = z;
		d = z >> 3;
		sx[i] = (s32)(s16)(((x << 4) / d) + 0xa0);
		sy[i] = (s32)(s16)(((y << 4) / d) + 0x48 + c->sky_bias);
		accum[i] = 0;
		touch[i] = 0;
	}

	for (f = 0; f < m->nface; f++) {
		const s32 *ii = m->fidx[f];
		s32 cnt = m->fcnt[f];
		s32 ax, ay, bx, by, z3, lx, ly, lz, in;

		/* img 0x472a - a 2D screen-space winding test, not the 3D
		 * normal. IMUL keeps 16 bits, so each product truncates
		 * before the subtraction. */
		ax = (s32)(s16)(sx[ii[0]] - sx[ii[2]]);
		ay = (s32)(s16)(sy[ii[0]] - sy[ii[2]]);
		bx = (s32)(s16)(sx[ii[1]] - sx[ii[2]]);
		by = (s32)(s16)(sy[ii[1]] - sy[ii[2]]);
		/* 16-BIT THROUGHOUT, and THE CULL ONLY ZEROES THE KEY: a
		 * back-facing face still lights its vertices - mesh.txt 5. */
		if ((s32)(s16)((s32)(s16)(ax * by) - (s32)(s16)(ay * bx)) < 0) {
			key[f] = 0;
		} else {
			/* img 0x474c - the SUMMED z, never divided; a triangle is
			 * scaled by 4/3 so it compares against a quad. */
			z3 = rz[ii[0]] + rz[ii[1]] + rz[ii[2]];
			key[f] = (cnt == 3) ? (z3 * 4) / 3 : z3 + rz[ii[3]];
		}

		/* img 0x4778 - THE STORED normal, not one taken from the
		 * rotated vertices. Xcur_MeshNormals has why. */
		lx = m->fnx[f] << 4;
		ly = m->fny[f] << 4;
		lz = m->fnz[f] << 4;
		/* img 0x47a1 - rotated AGAIN, by the object's own yaw, on top
		 * of already being in camera space. */
		Xcur_VecRotate3(&lx, &ly, &lz, c->pitch, c->yaw, c->roll);
		lx >>= 6; ly >>= 6; lz >>= 6;
		Xcur_VecRotate3(&lx, &ly, &lz,
		                c->light_pitch, c->light_yaw, c->light_roll);
		in = (lx >> 12) + 0x12c;
		if (in < 0) in = 0;
		in = (in * in) >> 14;       /* squared - the falloff */
		for (i = 0; i < cnt; i++) {
			accum[ii[i]] += in;
			touch[ii[i]]++;
		}
	}

	/* img 0x4848 - average by the touch count, then scale. */
	for (i = 0; i < m->nvert; i++)
		shade[i] = touch[i] ? ((accum[i] / touch[i]) * c->shade_scale) >> 9
		                    : 0;

	/* img 0x4894 - a selection sort, largest key first. Culled faces
	 * are already zero so they fall out for free. */
	for (;;) {
		s32 best = -1, vxs[4], vys[4], shs[4];

		for (f = 0; f < m->nface; f++)
			if (key[f] > 0 && (best < 0 || key[f] > key[best])) best = f;
		if (best < 0) break;
		key[best] = 0;
		for (i = 0; i < 4; i++) {
			vxs[i] = sx[m->fidx[best][i]];
			vys[i] = sy[m->fidx[best][i]];
			shs[i] = shade[m->fidx[best][i]];
		}
		poly_fill(fb, vxs, vys, shs, m->fattr[best], b);
		drawn++;
	}
	return drawn;
}
