/* LITHIUM - the sun and the starfield.
 *
 * ORIGIN img 0x425b Sky_SunAndStars. docs/lithium/effects/sky.txt.
 *
 * NEITHER IS TRANSLATED BY THE CAMERA, only rotated, so the sun sits
 * at infinity and the stars with it. That is the whole sky model.
 *
 * AND NEITHER NEEDS A DEPTH BUFFER. The back buffer still holds its
 * 0x80 fill wherever nothing has been drawn this frame, and the
 * terrain and the ships are already down by the time this runs - so
 * one byte compared against 0x80 answers both "is the sun hidden"
 * and "may this star be drawn".
 */

#include "lithium.h"

static s32 g_sun_x, g_sun_y;            /* img 0x8f42 and 0x8f44 */
static s32 g_sun_visible;               /* img 0x8ef2            */
static s32 g_flare_energy;              /* img 0x8eee            */

s32 Lith_SunScreenX(void) { return g_sun_x; }
s32 Lith_SunScreenY(void) { return g_sun_y; }
s32 Lith_SunVisible(void) { return g_sun_visible; }
void Lith_SunScreenSet(s32 x, s32 y) { g_sun_x = x; g_sun_y = y; }

/* g_FlareBrightness img 0x8eef IS BYTE ONE OF g_FlareEnergy, the
 * dword at img 0x8eee - not a global of its own. So the eased
 * energy's second byte IS the flare's threshold, and it ramps 0 to
 * 0x80 as the sun goes behind something. */
s32 Lith_FlareBrightness(void) { return (g_flare_energy >> 8) & 0xff; }
s32 Lith_FlareEnergy(void) { return g_flare_energy; }

/* ORIGIN img 0x63e3 - the flyover tick eases the energy a quarter of
 * the way to whatever the occlusion test last decided, so the glare
 * ramps over about a dozen ticks instead of switching. The flare
 * itself is [open] - img 0x4476 Flare_Render is not ported. */
void Lith_FlareEnergyStep(void)
{
	g_flare_energy += (g_sun_visible - g_flare_energy) >> 2;
}

/* ORIGIN img 0x427b - the same three rotations with a SAR 9 after
 * each and the same divide, for the sun and for every star. */
static int sky_project(s32 *x, s32 *y, s32 vx, s32 vy, s32 vz,
                       s32 ax, s32 ay, s32 az, s32 near_z)
{
	Lith_VecRotY(&vx, &vy, &vz, ay);
	vx >>= 9; vy >>= 9; vz >>= 9;
	Lith_VecRotX(&vx, &vy, &vz, ax);
	vx >>= 9; vy >>= 9; vz >>= 9;
	Lith_VecRotZ(&vx, &vy, &vz, az);
	vx >>= 9; vy >>= 9; vz >>= 9;

	if (vz < near_z) return 0;
	*x = lith_idiv_shl8(vx, vz) + 0xa0;     /* img 0x42cf */
	*y = lith_idiv_shl8(vy, vz) + 0x64;
	return 1;
}

/* ORIGIN img 0x425b Sky_SunAndStars.
 *
 * THE SUN IS A CONSTANT DIRECTION, img 0x4269. g_SunScreenX and Y are
 * set to 0xff80 first and that value is the mark Flare_Render tests
 * to decide the sun is off screen.
 *
 * THE STARS COST NOTHING TO STORE: the generator is reseeded with two
 * fixed constants, so the same 1,024 positions come out every frame
 * and the field holds still while the camera turns. The y axis is
 * biased by -0x5000 and shifted one further than the other two, so
 * the field is flattened and pushed below the camera.
 *
 * [open] THE DIRTY BOX IS NOT KEPT, as elsewhere - img 0x4435 grows
 * the same one Screen_PresentDirty reads. */
void Lith_SkySunAndStars(u8 *back, s32 ang_x, s32 ang_y, s32 ang_z)
{
	s32 i, sx, sy;

	g_sun_x = -0x80;                        /* img 0x425b, 0xff80 */
	g_sun_y = -0x80;
	g_sun_visible = 0;

	if (sky_project(&sx, &sy, -0x800, -0x12c, -0x400,
	                ang_x, ang_y, ang_z, 0x3e8)) {
		g_sun_x = (s32)(s16)sx;
		g_sun_y = (s32)(s16)sy;

		/* img 0x4301 - CLAMPED, not rejected: the occlusion is read
		 * at the nearest edge pixel when the sun is off screen. */
		if (sx > 0x13f) sx = 0x13f;
		if (sy > 0xc7) sy = 0xc7;
		if (sx < 0) sx = 0;
		if (sy < 0) sy = 0;
		if (back[sy * PD_SCREEN_W + sx] != 0x80)
			g_sun_visible = 0x8000;         /* img 0x433e */
	}

	Lith_RndSeed(0xcd57, 0xb6b5);           /* img 0x4348 */
	for (i = 0; i < 0x400; i++) {
		s32 shade = Lith_RndNext() & 0xff;
		s32 vz = (s32)(s16)Lith_RndNext();
		s32 vy = (s32)(s16)Lith_RndNext();
		s32 vx = (s32)(s16)Lith_RndNext();

		vy -= 0x5000;                       /* img 0x437f */
		vx >>= 4;
		vy >>= 5;
		vz >>= 4;
		if (!sky_project(&sx, &sy, vx, vy, vz, ang_x, ang_y, ang_z, 1))
			continue;
		if ((u32)sy >= 0xc8 || (u32)sx >= 0x140) continue;
		if (back[sy * PD_SCREEN_W + sx] != 0x80) continue;
		back[sy * PD_SCREEN_W + sx] = (u8)((shade >> 3) + 0x88);
	}
}
