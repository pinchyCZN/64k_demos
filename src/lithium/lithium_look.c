/* LITHIUM - swing the view with the mouse while a scene plays on.
 *
 * OURS IN FULL. Nothing here is transcribed. It exists so the demo
 * can be looked around rather than only watched, the way
 * xcursion_look.c does for XCURSION - docs/playground.txt.
 *
 * THE SIMULATION IS NEVER TOLD, AND THAT COSTS NOTHING TO ARRANGE.
 * Obj_LoadOrientA rewrites the camera's live ang_* and pos_* out of
 * the accumulators a_* and home_* every frame, and Flight_Step
 * integrates the accumulators alone - img 0x5bac never reads the
 * live pair back. So an offset written after the load is gone by the
 * next one: the view returns to the demo's exactly, with no saved
 * state and nothing to put back. XCURSION needs a push AND a pop
 * because its camera IS its integrator state; this one does not.
 *
 * IT TURNS ON THE SPOT. Scene_Project translates by the camera's
 * position and THEN rotates - lithium_scene.c - so the whole vertex
 * pool swings about the eye: terrain, craft, tower and the tunnel's
 * rings all together. Nothing orbits and there is no pivot.
 *
 * LOOKING BACKWARDS IS CLEAN RATHER THAN EMPTY-HANDED. The near
 * plane is a CLAMP, not a reject, so geometry behind the eye lands
 * on z = LITH_NEAR_Z and sums to 0x177 across a triangle - under
 * every depth_near the scenes pass Scene_CullSort, so it is dropped
 * instead of smeared. In the tunnel that leaves the four rings
 * Tunnel_Build carries behind the eye and then black.
 */

#include "lithium.h"

/* One screen width is one full turn. THE CIRCLE IS 0x2000: Lith_Sin
 * reads its table through a 0x1ffc mask, a byte offset into dwords,
 * so 2048 entries cover a revolution and the angle counts four to
 * the entry. */
#define LOOK_CIRCLE     0x2000
#define LOOK_PITCH_MAX  0x0500
#define LOOK_EASE       2

static s32 g_held;
static s32 g_anchor_x, g_anchor_y;
static s32 g_yaw, g_pitch;

/* OURS - press anchors, move swings, release lets go. The host hands
 * over framebuffer pixels, so the anchor is what makes a drag out of
 * a position - src/host.h Demo_Mouse. */
void Lith_LookMouse(s32 x, s32 y, s32 buttons)
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
	g_yaw = ((x - g_anchor_x) * LOOK_CIRCLE) / PD_SCREEN_W;
	g_pitch = ((y - g_anchor_y) * LOOK_CIRCLE) / PD_SCREEN_W;
	if (g_pitch >  LOOK_PITCH_MAX) g_pitch =  LOOK_PITCH_MAX;
	if (g_pitch < -LOOK_PITCH_MAX) g_pitch = -LOOK_PITCH_MAX;
}

/* OURS - one step toward zero, never stalling short of it. */
static s32 ease(s32 v)
{
	s32 step;

	if (!v) return 0;
	step = v >> LOOK_EASE;
	if (!step) step = (v > 0) - (v < 0);
	return v - step;
}

/* OURS - once a frame, so a released view eases back. */
void Lith_LookTick(void)
{
	if (g_held) return;
	g_yaw = ease(g_yaw);
	g_pitch = ease(g_pitch);
}

/* OURS - what every scene that has a camera calls in place of
 * Obj_LoadOrientA. The load is the original's and is left alone; the
 * two adds are not, and they are the whole of the feature.
 *
 * THE ANGLES WRAP ON SIXTEEN BITS, as the slot bank's do. */
void Lith_CamLoad(LithSlot *cam)
{
	Lith_ObjLoadOrientA(cam);
	if (!g_yaw && !g_pitch) return;
	cam->ang_y = (s32)(s16)(cam->ang_y + g_yaw);
	cam->ang_x = (s32)(s16)(cam->ang_x + g_pitch);
}
