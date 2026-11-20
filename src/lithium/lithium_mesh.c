/* LITHIUM - the model loader.
 *
 * ORIGIN img 0x503a Mesh_Load. The format is
 * docs/lithium/formats/mesh.txt; this is the reading of it.
 */

#include <string.h>

#include "lithium.h"

/* ORIGIN img 0x503a. Expands a byte-packed mesh and builds AVERAGED
 * PER-VERTEX NORMALS, which is Gouraud setup.
 *
 * THE FACE TABLE IS SCRATCH IN THE ORIGINAL. It writes face records
 * at g_MeshNrmOut with a stride of 0xa, then restarts the same
 * pointer and overwrites them with the normals at a stride of 6. Only
 * the normals survive it; we keep the faces because the renderer
 * needs them and nothing else would have to re-read the file. */
int Lith_MeshLoad(LithMesh *m, u32 src_off)
{
	u32 base = LITH_ASSET_BASE + src_off;
	u32 si;
	s32 i, f;
	/* The accumulators are 16-BIT AND THEY WRAP - see the adds below. */
	u16 ax[LITH_MESH_MAXV], ay[LITH_MESH_MAXV], az[LITH_MESH_MAXV];
	u16 cnt[LITH_MESH_MAXV];

	if (!g_lith_img) return 1;
	memset(m, 0, sizeof *m);
	m->nvert = (s32)RD_U16(g_lith_img + base);
	m->nface = (s32)RD_U16(g_lith_img + base + 2);
	if (m->nvert < 1 || m->nvert > LITH_MESH_MAXV) return 1;
	if (m->nface < 1 || m->nface > LITH_MESH_MAXF) return 1;

	si = base + 4;
	for (i = 0; i < m->nvert; i++) {
		m->vx[i] = (s32)(s8)g_lith_img[si];
		m->vy[i] = (s32)(s8)g_lith_img[si + 1];
		m->vz[i] = (s32)(s8)g_lith_img[si + 2];
		si += 3;
		ax[i] = ay[i] = az[i] = 0;
		cnt[i] = 0;
	}

	for (f = 0; f < m->nface; f++) {
		s32 i0, i1, i2, cx, cy, cz, ux, uy, uz, wx, wy, wz;
		s32 nx, ny, nz, bx, by, bz, k;
		u32 len2, best, c;

		i0 = (s32)RD_U16(g_lith_img + si);
		i1 = (s32)RD_U16(g_lith_img + si + 2);
		i2 = (s32)RD_U16(g_lith_img + si + 4);
		si += 6;
		if (i0 >= m->nvert || i1 >= m->nvert || i2 >= m->nvert) return 1;
		m->f0[f] = i0; m->f1[f] = i1; m->f2[f] = i2;

		/* The centroid. IDIV, so it truncates toward zero. */
		cx = lith_idiv(m->vx[i0] + m->vx[i1] + m->vx[i2], 3);
		cy = lith_idiv(m->vy[i0] + m->vy[i1] + m->vy[i2], 3);
		cz = lith_idiv(m->vz[i0] + m->vz[i1] + m->vz[i2], 3);

		/* ONLY v0 AND v1 TAKE PART in the cross product; v2 reaches
		 * it through the centroid alone. */
		ux = m->vx[i0] - cx; uy = m->vy[i0] - cy; uz = m->vz[i0] - cz;
		wx = m->vx[i1] - cx; wy = m->vy[i1] - cy; wz = m->vz[i1] - cz;
		nx = (s32)(s16)(u16)(uy * wz - uz * wy);   /* kept as a WORD */
		ny = (s32)(s16)(u16)(uz * wx - ux * wz);
		nz = (s32)(s16)(u16)(ux * wy - uy * wx);

		/* THE LENGTH IS A BRUTE-FORCE SEARCH, not a square root: walk
		 * c up keeping the closest c*c to the squared length and stop
		 * as soon as it gets worse. */
		len2 = (u32)(nx * nx + ny * ny + nz * nz);
		best = 0xffffffffUL;
		for (c = 0; c < 0x4000; c++) {
			u32 sq = c * c;
			u32 d = sq > len2 ? sq - len2 : len2 - sq;
			if (d > best) break;
			best = d;
		}
		c++;        /* the trailing INC ECX */

		/* So c is the nearest root PLUS one or two, and that is the
		 * point: it can never be zero, so a degenerate face with a
		 * zero-length normal cannot divide by zero. */
		bx = lith_idiv(nx << 11, (s32)c);          /* 11-bit fixed */
		by = lith_idiv(ny << 11, (s32)c);
		bz = lith_idiv(nz << 11, (s32)c);

		for (k = 0; k < 3; k++) {
			s32 v = k == 0 ? i0 : (k == 1 ? i1 : i2);
			ax[v] = (u16)(ax[v] + (u16)bx);     /* 16-bit, wraps */
			ay[v] = (u16)(ay[v] + (u16)by);
			az[v] = (u16)(az[v] + (u16)bz);
			cnt[v] = (u16)(cnt[v] + 1);
		}
	}

	for (i = 0; i < m->nvert; i++) {
		if (cnt[i]) {
			m->nxv[i] = lith_idiv((s32)(s16)ax[i], (s32)cnt[i]);
			m->nyv[i] = lith_idiv((s32)(s16)ay[i], (s32)cnt[i]);
			m->nzv[i] = lith_idiv((s32)(s16)az[i], (s32)cnt[i]);
		}
		/* The original leaves the destination untouched for a vertex
		 * no face references; the memset above is our choice. */
	}
	return 0;
}
