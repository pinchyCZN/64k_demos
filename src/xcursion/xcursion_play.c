/* OURS - a car and a ship over the demo's terrain.
 * docs/playground.txt.
 *
 * A FORK OF THE CAR SCENE, deliberately. The demo's own version is
 * effects/car.txt and stays untouched: its steering is a sine sweep
 * and its speed has no ceiling, neither of which a player can hold.
 * What is kept is the SHAPE of that model - a spring suspension, two
 * probe pairs for pitch and roll - because it is what makes the car
 * hop over a ridge instead of sliding up it.
 *
 * THE TWO VEHICLES ARE ONE INDEX APART. A body, a model and a step
 * function per vehicle, so switching is a single assignment and the
 * one left behind keeps its place and goes on being drawn.
 *
 * Every constant here is ours and tuned by feel. None of it is read
 * out of the image, and nothing in this file may be cited as if it
 * were.
 */

#include <string.h>

#include "xcursion.h"
#include "xcursion_play.h"

#define PLAY_ACCEL       30     /* gas                          */
#define PLAY_BRAKE       28
#define PLAY_DRAG_SHIFT  8      /* higher = less drag           */
#define PLAY_ROLL_FRIC   1
#define PLAY_SPEED_MAX   0xA00
#define PLAY_GRAVITY     1
#define PLAY_SLOPE_SHIFT 9      /* how hard to climb slope      */
#define PLAY_SLOPE_PROBE 4
#define PLAY_AIR         0x40
#define PLAY_FLOOR       0x10
#define PLAY_WATER       0x21
#define PLAY_SPRING      3
#define PLAY_BOUNCE      1
#define PLAY_STEER       0x18
#define PLAY_CAM_DIST    0x1000
#define PLAY_CAM_NEAR    0x0600
#define PLAY_CAM_FAR     0x3000
#define PLAY_CAM_STEP    0x0200
#define PLAY_CAM_SLOT    0x200
#define PLAY_CAM_SLOTS   4
#define PLAY_MODEL_YAW   0x400
#define PLAY_CAM_EASE    4
#define PLAY_CAM_RISE    0x1c2
#define PLAY_PROBE_LONG  3
#define PLAY_PROBE_WIDE  2
#define PLAY_TILT_MAX    0x1e0

#define SHIP_ACCEL       18
#define SHIP_DRAG_SHIFT  8
#define SHIP_SPEED_MAX   0xC00
#define SHIP_STEER       0x10
#define SHIP_PITCH_RATE  4      /* trim per tick                */
#define SHIP_PITCH_MAX   0x100  /* an eighth of a turn          */
#define SHIP_CLIMB_SHIFT 17     /* higher = gentler climb       */
#define SHIP_CEILING     0x2800
#define SHIP_CLEAR       0x100  /* lowest it may skim           */
#define SHIP_RISE        0x600  /* where it starts above ground */
#define SHIP_AWAY        0x4000 /* where it parks from the car  */
#define SHIP_MODEL_YAW   0x400

#define PLAY_KEY_GAS    1
#define PLAY_KEY_BRAKE  2
#define PLAY_KEY_LEFT   4
#define PLAY_KEY_RIGHT  8
#define PLAY_KEY_UP     16
#define PLAY_KEY_DOWN   32

#define PLAY_CAR        0
#define PLAY_SHIP       1
#define PLAY_VEHICLES   2

/* One vehicle. The last three are the car's alone - the ship has no
 * spring and takes its tilt from the stick, not from the ground. */
typedef struct {
	s32 x, y, z;
	s32 speed;
	s32 yaw, pitch, roll;
	s32 vy;
	s32 pitch_t, roll_t;
} PlayBody;

static u8 g_pl_height[0x10000];
static u8 g_pl_colour[0x10000];
static u8 g_pl_water[0x4000];

static PlayBody g_pl_body[PLAY_VEHICLES];
static XcurModel g_pl_model[PLAY_VEHICLES];
static s32 g_pl_mode;

static XcurCam g_pl_cam;
static XcurSpan g_pl_span;
static XcurMeshCam g_pl_mc;

static s32 g_pl_cam_angle;
static s32 g_pl_cam_slot;
static s32 g_pl_cam_dist;
static s32 g_pl_cam_track;
static u8 *g_pl_pal;
static u8 *g_pl_pal_master;
static u8 g_pl_ground_pal[0x20 * 3];
static s32 g_pl_grey;
static s32 g_pl_keys;
static s32 g_pl_ready;

/* OURS. The ground's own 32 palette entries, swapped under the rest
 * of the palette. Xcur_PalBuildGreyLow writes 0 to 0x1f and nothing
 * else, which is the band Voxel_Render clamps the terrain to - so
 * this reaches the land and leaves the craft and the sky alone.
 *
 * THE WATER GOES WITH IT, because the moon has none: img 0x4f72
 * turns it off for the whole landing-flag-car run that this terrain
 * comes from. Zero disables it in the renderer and in the height
 * probe both, so the car then drives on the sea bed. */
static void play_ground_pal(void)
{
	u8 grey[0x20 * 3];
	const u8 *src = g_pl_ground_pal;

	if (g_pl_grey) {
		memset(grey, 0, sizeof grey);
		Xcur_PalBuildGreyLow(grey);
		src = grey;
	}
	if (g_pl_pal) memcpy(g_pl_pal, src, sizeof grey);
	if (g_pl_pal_master) memcpy(g_pl_pal_master, src, sizeof grey);
	g_pl_cam.water_level = g_pl_grey ? 0 : PLAY_WATER;
}

/* OURS. The ground under a world point. */
static s32 play_ground(s32 x, s32 z)
{
	return Xcur_TerrainHeightAt(g_pl_height, g_pl_water, x, z,
	                            g_pl_cam.water_level,
	                            g_pl_cam.water_scroll,
	                            g_pl_cam.hscale, (s32 *)0);
}

/* OURS. The tilt the car takes from the ground under it: one probe
 * pair along the body and one across, the difference clamped. The
 * across pair swaps sin and cos, which is the ninety degrees. */
static void play_tilt(PlayBody *b)
{
	s32 dx, dz, d;

	dx = Xcur_Sin(b->yaw) << PLAY_PROBE_LONG;
	dz = Xcur_Cos(b->yaw) << PLAY_PROBE_LONG;
	d = (play_ground(b->x - dx, b->z - dz)
	     - play_ground(b->x + dx, b->z + dz)) >> 3;
	if (d < -PLAY_TILT_MAX) d = -PLAY_TILT_MAX;
	if (d > PLAY_TILT_MAX) d = PLAY_TILT_MAX;
	b->pitch_t = d;

	dx = Xcur_Cos(b->yaw) << PLAY_PROBE_WIDE;
	dz = Xcur_Sin(b->yaw) << PLAY_PROBE_WIDE;
	d = (play_ground(b->x + dx, b->z - dz)
	     - play_ground(b->x - dx, b->z + dz)) >> 2;
	if (d < -PLAY_TILT_MAX) d = -PLAY_TILT_MAX;
	if (d > PLAY_TILT_MAX) d = PLAY_TILT_MAX;
	b->roll_t = d;
}

/* OURS. One of four slots a quarter turn apart around the vehicle,
 * eased on. A TURN IS 0x800 HERE, not 0x10000, so a difference taken
 * across the word wrap sends the camera the long way round.
 *
 * FLYING, IT IGNORES THE TERRAIN ENTIRELY and rides a fixed lift
 * above the ship. Driving, it clears the higher of the ground under
 * itself and whichever of the car or the car's ground the height
 * switch picks. docs/playground.txt section 2.
 */
static void play_camera(const PlayBody *b)
{
	s32 want = b->yaw + g_pl_cam_slot * PLAY_CAM_SLOT;
	s32 d = (want - g_pl_cam_angle) & 0x7ff;
	s32 step, gc, gv;

	if (d >= 0x400) d -= 0x800;
	step = d >> PLAY_CAM_EASE;
	if (!step) step = (d > 0) - (d < 0);
	g_pl_cam_angle = (g_pl_cam_angle + step) & 0xffff;

	g_pl_cam.angle = g_pl_cam_angle;
	g_pl_cam.wx = b->x
	              - ((Xcur_Sin(g_pl_cam_angle) * g_pl_cam_dist) >> 8);
	g_pl_cam.wy = b->z
	              - ((Xcur_Cos(g_pl_cam_angle) * g_pl_cam_dist) >> 8);
	if (g_pl_mode == PLAY_SHIP) {
		g_pl_cam.scale = b->y + PLAY_CAM_RISE;
		return;
	}
	gc = play_ground(g_pl_cam.wx, g_pl_cam.wy);
	gv = g_pl_cam_track ? b->y : play_ground(b->x, b->z);
	g_pl_cam.scale = (gc > gv ? gc : gv) + PLAY_CAM_RISE;
}

/* OURS. Drive, slope, two drags, then the spring.
 * docs/playground.txt section 2 has the force table, the slope
 * scaling, and the heading convention this shares with the camera.
 */
static void play_step_car(PlayBody *b)
{
	s32 g, dx, dz, slope, a = 0, grounded;

	if (g_pl_keys & PLAY_KEY_LEFT)
		b->yaw = (b->yaw - PLAY_STEER) & 0xffff;
	if (g_pl_keys & PLAY_KEY_RIGHT)
		b->yaw = (b->yaw + PLAY_STEER) & 0xffff;

	g = play_ground(b->x, b->z);
	grounded = b->y <= g + PLAY_AIR;

	dx = Xcur_Sin(b->yaw) << PLAY_SLOPE_PROBE;
	dz = Xcur_Cos(b->yaw) << PLAY_SLOPE_PROBE;
	slope = play_ground(b->x - dx, b->z - dz)
	        - play_ground(b->x + dx, b->z + dz);

	if (grounded) {
		if (g_pl_keys & PLAY_KEY_GAS) a += PLAY_ACCEL;
		if (g_pl_keys & PLAY_KEY_BRAKE)
			a += (b->speed > 0) ? -PLAY_BRAKE : PLAY_BRAKE;
		a += slope >> PLAY_SLOPE_SHIFT;
	}

	b->speed += a;
	b->speed -= b->speed >> PLAY_DRAG_SHIFT;
	if (b->speed > 0) b->speed -= PLAY_ROLL_FRIC;
	else if (b->speed < 0) b->speed += PLAY_ROLL_FRIC;
	if (b->speed > PLAY_SPEED_MAX) b->speed = PLAY_SPEED_MAX;
	if (b->speed < -PLAY_SPEED_MAX) b->speed = -PLAY_SPEED_MAX;

	b->x += (Xcur_Sin(b->yaw) * b->speed) >> 0xb;
	b->z += (Xcur_Cos(b->yaw) * b->speed) >> 0xb;

	b->vy -= PLAY_GRAVITY;
	g = play_ground(b->x, b->z);
	if (b->y <= g) {
		b->vy += (g - b->y) >> PLAY_SPRING;
		b->y = g;
	}
	b->y += b->vy >> PLAY_BOUNCE;

	play_tilt(b);
	b->pitch = (s32)(s16)(b->pitch + ((b->pitch_t - b->pitch) >> 4));
	b->roll = (s32)(s16)(b->roll + ((b->roll_t - b->roll) >> 4));
}

/* OURS. No gravity and no spring: the ship holds whatever the pitch
 * gives it.
 *
 * THE PITCH IS THE CLIMB AND IT IS TRIM, NOT A STICK. Up and down
 * move it and nothing returns it, so level flight has to be flown
 * back to level. The rate is the pitch's sine times the speed, so a
 * slow ship climbs slowly and a stopped one not at all.
 *
 * THE TERRAIN AND THE CEILING ARE PLAIN CLAMPS and nothing else is
 * touched - a ship flown into a hill rides up it and goes on
 * descending the far side, still nose down, exactly as it was.
 * The brake only slows - there is no reverse to oppose. */
static void play_step_ship(PlayBody *b)
{
	s32 g;

	if (g_pl_keys & PLAY_KEY_LEFT)
		b->yaw = (b->yaw - SHIP_STEER) & 0xffff;
	if (g_pl_keys & PLAY_KEY_RIGHT)
		b->yaw = (b->yaw + SHIP_STEER) & 0xffff;
	if (g_pl_keys & PLAY_KEY_UP) b->pitch += SHIP_PITCH_RATE;
	if (g_pl_keys & PLAY_KEY_DOWN) b->pitch -= SHIP_PITCH_RATE;
	if (b->pitch > SHIP_PITCH_MAX) b->pitch = SHIP_PITCH_MAX;
	if (b->pitch < -SHIP_PITCH_MAX) b->pitch = -SHIP_PITCH_MAX;

	if (g_pl_keys & PLAY_KEY_GAS) b->speed += SHIP_ACCEL;
	if (g_pl_keys & PLAY_KEY_BRAKE) b->speed -= PLAY_BRAKE;
	b->speed -= b->speed >> SHIP_DRAG_SHIFT;
	if (b->speed < 0) b->speed = 0;
	if (b->speed > SHIP_SPEED_MAX) b->speed = SHIP_SPEED_MAX;

	b->x += (Xcur_Sin(b->yaw) * b->speed) >> 0xb;
	b->z += (Xcur_Cos(b->yaw) * b->speed) >> 0xb;
	b->y += (Xcur_Sin(b->pitch & 0xffff) * b->speed) >> SHIP_CLIMB_SHIFT;

	g = play_ground(b->x, b->z) + SHIP_CLEAR;
	if (b->y < g) b->y = g;
	if (b->y > SHIP_CEILING) b->y = SHIP_CEILING;
}

/* OURS. One tick of whichever vehicle is being flown or driven. */
static void play_step(void)
{
	PlayBody *b = &g_pl_body[g_pl_mode];

	if (g_pl_mode == PLAY_SHIP) play_step_ship(b);
	else play_step_car(b);
	play_camera(b);
}

/* OURS. One vehicle, parked or not. The z cull is the demo's own
 * gate at img 0x2376, which keeps a model behind the camera from
 * being drawn inside out.
 *
 * THE PITCH COMES IN RATHER THAN OFF THE BODY because the two
 * models do not agree on which way their nose points, the same way
 * they do not agree on their yaw. The ship's is inverted: left as
 * the body has it, it tilts nose-down to climb. */
static void play_draw(u8 *fb, const PlayBody *b, const XcurModel *m,
                      s32 model_yaw, s32 model_pitch)
{
	s32 z;

	g_pl_mc.cam_yaw = g_pl_cam.angle;
	g_pl_mc.sky_bias = g_pl_cam.sky_bias;
	g_pl_mc.pitch = model_pitch;
	g_pl_mc.yaw = (model_yaw - b->yaw) & 0xffff;
	g_pl_mc.roll = b->roll;
	z = Xcur_MeshSetup(&g_pl_mc, g_pl_cam.wx, g_pl_cam.wy,
	                   g_pl_cam.angle, g_pl_cam.scale, b->x, b->z, b->y);
	if ((u32)z < (u32)XCUR_SHIP_FAR)
		Xcur_MeshDraw(fb, m, &g_pl_mc, &g_pl_span);
}

/* OURS. Its own world, built exactly as the demo's car part builds
 * its own: the same generator call, and the same FLOOR OF 0x10 at
 * img 0x5174 - which is what gives that part its wide flat plains,
 * every basin below the floor coming out dead level.
 * docs/playground.txt section 2. */
void Xcur_PlayEnter(u8 *pal, u8 *pal_master)
{
	s32 i;
	PlayBody *car = &g_pl_body[PLAY_CAR];
	PlayBody *ship = &g_pl_body[PLAY_SHIP];

	Xcur_PlasmaGenerate(g_pl_height, 0x3fff, 0x2000, 2);
	for (i = 0; i < 0x10000; i++)
		if (g_pl_height[i] < PLAY_FLOOR) g_pl_height[i] = PLAY_FLOOR;
	Xcur_WaterFieldBuild(g_pl_water);
	Xcur_TerrainBuildShade(g_pl_colour, g_pl_height,
	                       XCUR_VOX_HSCALE, XCUR_SHADE_DIV);
	Xcur_SkyBuild();
	Xcur_MeshParse(&g_pl_model[PLAY_CAR], XCUR_CAR);
	Xcur_MeshParse(&g_pl_model[PLAY_SHIP], XCUR_SHIP);
	Xcur_SpanReset(&g_pl_span);

	memset(&g_pl_cam, 0, sizeof g_pl_cam);
	g_pl_cam.hscale = XCUR_VOX_HSCALE;
	g_pl_cam.water_level = PLAY_WATER;
	g_pl_cam.water_scroll = 0x22;
	g_pl_cam.yofs = 0;
	g_pl_cam.sky_bias = 0;

	memset(&g_pl_mc, 0, sizeof g_pl_mc);
	g_pl_mc.zscale = XCUR_MESH_ZSCALE;
	g_pl_mc.shade_scale = XCUR_SHADE_SCALE;
	g_pl_mc.light_pitch = XCUR_LIGHT_PITCH;
	g_pl_mc.light_yaw = XCUR_LIGHT_YAW;
	g_pl_mc.light_roll = XCUR_LIGHT_ROLL;
	Xcur_MeshNormals(&g_pl_model[PLAY_CAR], &g_pl_mc, XCUR_MESH_ZSCALE);
	Xcur_MeshNormals(&g_pl_model[PLAY_SHIP], &g_pl_mc, XCUR_MESH_ZSCALE);

	memset(g_pl_body, 0, sizeof g_pl_body);
	car->y = play_ground(0, 0);
	ship->x = SHIP_AWAY;
	ship->y = play_ground(ship->x, 0) + SHIP_RISE;

	g_pl_mode = PLAY_CAR;
	g_pl_cam_angle = 0;
	g_pl_cam_slot = 0;
	g_pl_cam_dist = PLAY_CAM_DIST;
	g_pl_cam_track = 1;
	g_pl_keys = 0;
	g_pl_ready = 1;

	Xcur_PalBuild(pal_master);
	memcpy(pal, pal_master, 768);
	g_pl_pal = pal;
	g_pl_pal_master = pal_master;
	memcpy(g_pl_ground_pal, pal_master, sizeof g_pl_ground_pal);
	g_pl_grey = 1;
	play_ground_pal();
	play_camera(&g_pl_body[g_pl_mode]);
}

/* OURS. Held keys as a bitmask: the host sends press AND
 * release, and may send a release we never saw pressed. */
int Xcur_PlayKey(int ch, int down)
{
	s32 bit = 0;

	if (ch == XCUR_PLAY_SWITCH) {
		if (down) g_pl_mode = (g_pl_mode + 1) % PLAY_VEHICLES;
		return 1;
	}
	if (ch == XCUR_PLAY_CAMERA) {
		if (down)
			g_pl_cam_slot = (g_pl_cam_slot + 1) % PLAY_CAM_SLOTS;
		return 1;
	}
	if (ch == XCUR_PLAY_PALETTE) {
		if (down) {
			g_pl_grey = !g_pl_grey;
			play_ground_pal();
		}
		return 1;
	}
	if (ch == XCUR_PLAY_HEIGHT) {
		if (down) g_pl_cam_track = !g_pl_cam_track;
		return 1;
	}
	if (ch == XCUR_PLAY_NEAR || ch == XCUR_PLAY_FAR) {
		if (down) {
			g_pl_cam_dist += (ch == XCUR_PLAY_FAR) ? PLAY_CAM_STEP
			                                       : -PLAY_CAM_STEP;
			if (g_pl_cam_dist < PLAY_CAM_NEAR)
				g_pl_cam_dist = PLAY_CAM_NEAR;
			if (g_pl_cam_dist > PLAY_CAM_FAR)
				g_pl_cam_dist = PLAY_CAM_FAR;
		}
		return 1;
	}
	if (ch == XCUR_PLAY_GAS) bit = PLAY_KEY_GAS;
	else if (ch == XCUR_PLAY_BRAKE) bit = PLAY_KEY_BRAKE;
	else if (ch == (int)XCUR_PLAY_LEFT) bit = PLAY_KEY_LEFT;
	else if (ch == (int)XCUR_PLAY_RIGHT) bit = PLAY_KEY_RIGHT;
	else if (ch == (int)XCUR_PLAY_UP) bit = PLAY_KEY_UP;
	else if (ch == (int)XCUR_PLAY_DOWN) bit = PLAY_KEY_DOWN;
	if (!bit) return 0;

	if (down) g_pl_keys |= bit;
	else g_pl_keys &= ~bit;
	return 1;
}

/* OURS. Physics on the tick and the picture on the frame, which is
 * the demo's own split - subsystems/animation.txt 1.1. */
void Xcur_PlayFrame(u8 *fb, s32 ticks)
{
	s32 t;

	if (!g_pl_ready) return;
	for (t = 0; t < ticks; t++) play_step();

	memset(fb, 0, PD_SCREEN_W * PD_SCREEN_H);
	Xcur_VoxelRender(fb, g_pl_height, g_pl_colour, g_pl_water,
	                 &g_pl_cam);
	play_draw(fb, &g_pl_body[PLAY_CAR], &g_pl_model[PLAY_CAR],
	          PLAY_MODEL_YAW, g_pl_body[PLAY_CAR].pitch);
	play_draw(fb, &g_pl_body[PLAY_SHIP], &g_pl_model[PLAY_SHIP],
	          SHIP_MODEL_YAW, -g_pl_body[PLAY_SHIP].pitch);
}
