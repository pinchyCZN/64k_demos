/* OURS - swing the view with the mouse while a scene plays on.
 *
 * The scene is never told. Push writes an offset into the camera the
 * renderer is about to read, pop puts the camera back, and both happen
 * inside one frame with no simulation step between them - so every
 * integrator the demo runs on the camera, g_SkyBiasAccum in
 * Cam_FollowTerrain above all, still sees only its own values and the
 * view returns to the demo's exactly.
 *
 * docs/playground.txt section 4.
 */

#include "xcursion_look.h"

/* One screen width is about one full turn, one screen height about
 * half the horizon's useful travel. */
#define LOOK_YAW_NUM    16
#define LOOK_YAW_SHIFT  1
#define LOOK_PITCH_MAX  180
#define LOOK_EASE       2

static s32 g_held;
static s32 g_anchor_x, g_anchor_y;
static s32 g_yaw, g_pitch;

static s32 g_save_angle, g_save_bias, g_save_yofs, g_save_global;
static s32 g_pushed;

static s32 g_m_yaw, g_m_bias, g_m_tx, g_m_ty, g_m_tz;
static s32 g_m_pushed;

static s32 g_piv_x, g_piv_y, g_piv_z;

/* OURS. Press anchors, move swings, release lets go. */
void Xcur_LookMouse(s32 x, s32 y, s32 buttons)
{
	if (!(buttons & 1)) {
		g_held = 0;
		return;
	}

	if (!g_held) {
		g_held = 1;
		g_anchor_x = x;
		g_anchor_y = y;
		return;
	}

	g_yaw = ((g_anchor_x - x) * LOOK_YAW_NUM) >> LOOK_YAW_SHIFT;

	g_pitch = y - g_anchor_y;
	if (g_pitch >  LOOK_PITCH_MAX) g_pitch =  LOOK_PITCH_MAX;
	if (g_pitch < -LOOK_PITCH_MAX) g_pitch = -LOOK_PITCH_MAX;
}

/* OURS. One step toward zero, never stalling short of it. */
static s32 ease(s32 v)
{
	s32 step;

	if (!v) return 0;
	step = v >> LOOK_EASE;
	if (!step) step = (v > 0) - (v < 0);
	return v - step;
}

/* OURS */
s32 Xcur_LookYaw(void)
{
	return g_yaw;
}

/* OURS */
void Xcur_LookPivot(s32 x, s32 y, s32 z)
{
	g_piv_x = x;
	g_piv_y = y;
	g_piv_z = z;
}

/* OURS */
void Xcur_LookTick(void)
{
	g_piv_x = 0;
	g_piv_y = 0;
	g_piv_z = 0;
	if (g_held) return;
	g_yaw = ease(g_yaw);
	g_pitch = ease(g_pitch);
}

/* OURS. The land rides yofs and the sky and the craft ride sky_bias,
 * so the two move together by the relation Cam_FollowTerrain keeps at
 * img 0x2001 - docs/playground.txt section 4. */
void Xcur_LookPush(XcurCam *c, s32 *sky_bias)
{
	if (g_pushed || (!g_yaw && !g_pitch)) return;

	g_save_angle = c->angle;
	g_save_bias = c->sky_bias;
	g_save_yofs = c->yofs;
	g_save_global = *sky_bias;
	g_pushed = 1;

	c->angle = (c->angle + g_yaw) & 0x7ff;
	c->sky_bias += g_pitch;
	c->yofs += g_pitch * PD_SCREEN_W;
	*sky_bias += g_pitch;
}

/* OURS. Rotation composes, so turning a camera-space point by the
 * drag is exactly what Mesh_Setup would have produced from a camera
 * bearing that much further round.
 *
 * VEC_ROTATE3 RETURNS THE VECTOR TIMES 64, its sine table being
 * scaled to 4096 against a >> 6. Mesh_Setup pays that back with the
 * shift at img 0x4634 and so must this - without it the translation
 * dwarfs the vertices and every model collapses to a dot at 64 times
 * its true distance. */
void Xcur_LookOrbit(s32 *x, s32 *y, s32 *z)
{
	s32 dx, dy, dz;

	if (!g_yaw) return;
	dx = *x - g_piv_x;
	dy = *y - g_piv_y;
	dz = *z - g_piv_z;
	Xcur_VecRotate3(&dx, &dy, &dz, 0, g_yaw & 0xffff, 0);
	*x = (dx >> 6) + g_piv_x;
	*y = (dy >> 6) + g_piv_y;
	*z = (dz >> 6) + g_piv_z;
}

/* OURS. Five fields, because the position is part of the view here. */
void Xcur_LookPushMesh(XcurMeshCam *c)
{
	if (g_m_pushed || (!g_yaw && !g_pitch)) return;

	g_m_yaw = c->cam_yaw;
	g_m_bias = c->sky_bias;
	g_m_tx = c->tx;
	g_m_ty = c->ty;
	g_m_tz = c->tz;
	g_m_pushed = 1;

	Xcur_LookOrbit(&c->tx, &c->ty, &c->tz);
	c->cam_yaw = (c->cam_yaw + g_yaw) & 0xffff;
	c->sky_bias += g_pitch;
}

/* OURS */
void Xcur_LookPopMesh(XcurMeshCam *c)
{
	if (!g_m_pushed) return;

	c->cam_yaw = g_m_yaw;
	c->sky_bias = g_m_bias;
	c->tx = g_m_tx;
	c->ty = g_m_ty;
	c->tz = g_m_tz;
	g_m_pushed = 0;
}

/* OURS */
void Xcur_LookPop(XcurCam *c, s32 *sky_bias)
{
	if (!g_pushed) return;

	c->angle = g_save_angle;
	c->sky_bias = g_save_bias;
	c->yofs = g_save_yofs;
	*sky_bias = g_save_global;
	g_pushed = 0;
}
