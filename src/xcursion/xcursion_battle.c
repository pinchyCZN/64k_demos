/* XCURSION - the battle: the glow field, its particles, and the
 * shots. effects/battle.txt.
 *
 * `mem` IS SEGMENT 0x4a79 FROM OFFSET 0x1800, the voxel's colour map,
 * which the glow's two 128x128 buffers lie inside. One array, because
 * the routines here read and write past the buffer ends and land in
 * each other's memory when they do.
 */

#include <string.h>

#include "xcursion.h"

/* ORIGIN img 0x2320 - Glow_SpawnStep. One step of 32 particles.
 *
 * THE ADDRESS IS ASSEMBLED BYTE-WISE AND DOES NOT CARRY. img 0x2339
 * puts the y high byte in BH and shifts BX right one, giving y * 128;
 * img 0x233e then adds the x high byte with `add bl`, an 8-BIT add,
 * so a particle past x = 255 wraps inside its own half-row rather
 * than stepping down. And y is not masked to the field either: a
 * particle below row 127 stamps into the memory after the buffer. */
void Xcur_GlowSpawnStep(u8 *mem, s32 src, XcurSpark *sp)
{
	s32 i;

	for (i = 0; i < XCUR_SPARKS; i++) {
		s32 bx, lo;

		if (sp[i].life == 0) continue;          /* img 0x2333 */
		bx = (((sp[i].y >> 8) & 0xff) << 7) & 0xffff;
		lo = (bx + ((sp[i].x >> 8) & 0xff)) & 0xff;
		bx = (bx & 0xff00) | lo;
		mem[src + bx] = 0xff;                   /* img 0x2341 */
		sp[i].x = (sp[i].x + sp[i].dx) & 0xffff;
		sp[i].y = (sp[i].y + sp[i].dy) & 0xffff;
		sp[i].life--;                           /* img 0x234f */
	}
}

/* ORIGIN img 0x35a4 - the burst. All 32 particles at once, from the
 * CENTRE OF THE FIELD and not from either craft: x and y are the
 * literal 0x4000, whose high byte is 64 of 128 either way. The
 * explosion therefore appears wherever Glow_Render's box is, which is
 * a fixed screen offset.
 *
 * LIFE AND SHIFT ARE NOT CONSTANTS. img 0xa59b and img 0xa59a are
 * 0x20 and 6 in the image, and mode 0xd's warp-out rewrites them to
 * 0x80 and 8 at img 0x1587 - a longer-lived, slower burst that stays
 * on screen. Nothing ever puts them back.
 *
 * It takes 64 draws off the shared random stream, and the callers in
 * the battle tails SAVE AND RESTORE the stream around it so the
 * plasma fields stay reproducible. Mode 0xd's call does not. */
void Xcur_GlowBurst(XcurSpark *sp, s32 life, s32 shift)
{
	s32 i;

	for (i = 0; i < XCUR_SPARKS; i++) {
		sp[i].life = life;
		sp[i].x = 0x4000;
		sp[i].y = 0x4000;
		sp[i].dx = (s32)(s16)Xcur_RndNext() >> shift;
		sp[i].dy = (s32)(s16)Xcur_RndNext() >> shift;
	}
}

/* ORIGIN img 0x279b - 0x20a8 dwords from 0x1800, which covers both
 * buffers and ends on the particle table's last byte. */
void Xcur_GlowClear(u8 *mem, XcurSpark *sp)
{
	memset(mem, 0, XCUR_GLOWCLR);
	memset(sp, 0, sizeof(XcurSpark) * XCUR_SPARKS);
}

/* ORIGIN img 0x2736 - Glow_Render. A box blur that decays, drawn into
 * the fire ramp at 96.
 *
 * IT IS SINGLE-BUFFERED AFTER THE FIRST FRAME. img 0x2791 reads
 * g_GlowDst and XCHGs it into g_GlowSrc, and the XCHG's other half is
 * dropped in AX; g_GlowDst has no writer anywhere in the image. So
 * from the second call on both point at one buffer and the blur reads
 * its own output - the taps behind the cursor are this frame's, the
 * ones ahead last frame's, which drags the glow along the scan.
 * Giving it two buffers gives a symmetric blob and the wrong effect.
 *
 * THE DECAY IS THE LIFETIME. A box blur conserves its total, so the
 * DEC at img 0x2771 is the only reason the glow dies.
 *
 * Returns the new source offset. bx is a flat offset into the
 * 320-wide buffer, not a coordinate pair. */
s32 Xcur_GlowRender(u8 *fb, u8 *mem, s32 src, s32 dst, s32 bx)
{
	s32 row, col, si = src, di = dst;

	for (row = 0; row < 0x80; row++) {          /* img 0x274c */
		for (col = 0; col < 0x80; col++) {      /* img 0x274f */
			/* img 0x2753 - ADC into AH keeps the sum to 10 bits */
			s32 ax = (s32)mem[si - 1] + (s32)mem[si - 0x80]
			       + (s32)mem[si + 1] + (s32)mem[si + 0x80];
			s32 dh;

			ax >>= 2;                           /* img 0x2768 */
			dh = ax & 0xff;
			if (ax != 0) {
				s32 v = (ax >> 1) + 0x60;       /* img 0x2775 */

				dh = (dh - 1) & 0xff;           /* img 0x2771 */
				if (v >= 0xc0) v = 0xbf;        /* img 0x277d */
				fb[bx] = (u8)v;
			}
			mem[di] = (u8)dh;                   /* img 0x2782 */
			si++;
			di++;
			bx++;
		}
		bx += 0xc0;                             /* img 0x2789, 320 wide */
	}
	return dst;                                 /* img 0x2795 */
}

/* ORIGIN img 0x27ad - Shots_Draw. Eight slots, each a full 3D object.
 *
 * A SLOT WITH X == 0 IS INACTIVE, tested as a dword at img 0x27d3, so
 * the free marker is a position: a shot can never sit on the world x
 * origin. */
void Xcur_ShotsDraw(u8 *fb, const XcurShot *sh, const XcurModel *m,
                    XcurMeshCam *c, XcurSpan *b)
{
	s32 i;

	for (i = 0; i < XCUR_SHOTS; i++) {
		if (sh[i].x == 0) continue;
		c->tx = sh[i].x;
		c->ty = sh[i].y;
		c->tz = sh[i].z;
		c->pitch = sh[i].pitch;
		c->yaw = sh[i].yaw;
		c->roll = sh[i].roll;
		Xcur_MeshDraw(fb, m, c, b);             /* img 0x280b */
	}
}

/* ORIGIN img 0x1e9b - the shot step, shared by both battle tails.
 *
 * Each live shot flies along its OWN heading and spins its roll by
 * 0xb, then dies on any of five bounds. THE Z BOUND IS UNSIGNED - img
 * 0x1f12 is `ja` - so a shot whose z goes negative is killed by that
 * same test instead of flying on behind the camera.
 *
 * In mode 0xb only, a survivor is tested against a box 0x1000 on a
 * side centred on craft B. The box is axis-aligned in WORLD space and
 * ignores B's attitude. Returns the number of hits so the caller can
 * burst the glow and flash. */
s32 Xcur_ShotsStep(XcurShot *sh, s32 mode, s32 bx, s32 by, s32 bz)
{
	s32 i, hits = 0;

	for (i = 0; i < XCUR_SHOTS; i++) {
		s32 x = 0, y = 0, z = -0x20, lo_x, lo_y, lo_z;

		if (sh[i].x == 0) continue;             /* img 0x1ea1 */
		/* Vec_Rotate3 returns x64, and img 0x1ece adds it unshifted */
		Xcur_VecRotate3(&x, &y, &z,
		                sh[i].pitch, sh[i].yaw, sh[i].roll);
		sh[i].x += x;                           /* img 0x1ece */
		sh[i].y += y;
		sh[i].z += z;
		sh[i].roll = (sh[i].roll + 0xb) & 0xffff;   /* img 0x1ed9 */

		if (sh[i].x > 0xa0000L || sh[i].x < -0xa0000L
		 || sh[i].y > 0xa0000L || sh[i].y < -0xa0000L
		 || (u32)sh[i].z > 0x2e000UL) {         /* img 0x1f12, ja */
			sh[i].x = 0;
			continue;
		}
		if (mode != 0x0b) continue;             /* img 0x1f14 */

		lo_x = bx - 0x800;
		lo_y = by - 0x800;
		lo_z = bz - 0x800;
		if (sh[i].x < lo_x || sh[i].y < lo_y || sh[i].z < lo_z)
			continue;
		if (sh[i].x > lo_x + 0x1000L || sh[i].y > lo_y + 0x1000L
		 || sh[i].z > lo_z + 0x1000L)
		    continue;
		hits++;
		sh[i].x = 0;                            /* img 0x1f85 */
	}
	return hits;
}
