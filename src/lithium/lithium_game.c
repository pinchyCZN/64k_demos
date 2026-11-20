/* LITHIUM - the playground: fly the ship yourself.
 *
 * OURS IN FULL. NOTHING HERE IS IN THE DEMO. It is kept to one file
 * and reached as a part past the finale, so no document under
 * docs/lithium/ has to say which of its numbers came from a binary.
 * docs/playground.txt.
 *
 * THERE IS NO FLIGHT MODEL HERE BECAUSE THE DEMO ALREADY IS ONE.
 * Track_Play - img 0x5efe, lithium_flight.c - is the whole bridge
 * between the author's recorded mouse and Flight_Step: two signed
 * deltas into the accumulators the attitude chases, a six-bit
 * command and a two-bit throttle. Writing those four fields from the
 * keyboard instead of from the asset segment is the entire feature,
 * and Flight_Step then eases the attitude, banks and levels the
 * roll, drives the throttle, clamps to the ground and fires the
 * guns exactly as it does for the demo's own craft.
 *
 * THE SCENE IS THE FLYOVER'S, assembled from the same public calls
 * in the same order - terrain, meshes, project, cull, fill, sky.
 * Nothing was added to any other file to make this possible; every
 * routine it needs was already declared in lithium.h.
 *
 * ONE SLOT BANK IS SHARED WITH THE DEMO. g_lith_slot is the
 * original's single parameter bank and this writes into it, so
 * leaving the game and seeking back into the demo relies on each
 * scene's own Enter to re-seed what it uses - which they all do.
 */

#include <string.h>

#include "lithium.h"

/* Slot 0 is the eye and slot 4 - index 1 - is the craft, which is
 * how the flyover arranges them. */
#define GAME_CAM   LITH_SLOT(0)
#define GAME_SHIP  LITH_SLOT(4)

/* OURS, chosen by feel. The original spends a track entry every six
 * ticks and scales each delta by 32; a held key is smoother applied
 * every tick, so the rate is that much smaller. */
#define GAME_TURN_RATE   0x60
#define GAME_PITCH_RATE  0x50

/* THE KEYS, in one place so they are one edit to change. The arrows
 * are SDL keysyms passed through untranslated, which is the same
 * thing xcursion_play.h does and why neither host needs to know what
 * a demo does with a key. */
#define GAME_KEY_LEFT    0x40000050L
#define GAME_KEY_RIGHT   0x4000004FL
#define GAME_KEY_UP      0x40000052L
#define GAME_KEY_DOWN    0x40000051L
#define GAME_KEY_FIRE    'z'
#define GAME_KEY_SLOWER  'x'
#define GAME_KEY_FASTER  'c'

/* The demo's fourteen speeds are commands 0x0a to 0x17, img 0x5c4f;
 * the index is ours. The bolt's speed is a slot speed, rotated and
 * shifted down seven as the throttle at img 0x5aa6 is, so the two
 * are comparable. The lid is ours - the floor is already the
 * terrain, img 0x5d40. ../../docs/playground.txt section 5.3. */
#define GAME_SPEED_STEPS 14
#define GAME_SPEED_START 8
#define GAME_FIRE_EVERY  7
#define GAME_CEILING     0xa000
#define GAME_FLOOR       0x0400

/* THE SHOT IS A MESH, NOT A PUFF. img 0x2123 loads a four-vertex
 * tetrahedron at 0x724a and the battle draws six of them as slots
 * 0x20 to 0x34; the plasma ring is a SECOND weapon that only fills
 * in around them - img 0x66d7 keeps a craft from running both at
 * once. 0x1e0 is the demo's own bolt speed, img 0x6721. */
#define GAME_BOLT_MESH   0x724aUL
#define GAME_BOLT_SPEED  0x1e0
#define GAME_BOLTS       6
#define GAME_BOLT0       LITH_SLOT(0x20)
#define GAME_BOLT_TICKS  90

/* OURS, and BIGGER than the battle's 0xe - a shorter shift is a
 * larger mesh. A bolt fired along the view axis from a chase camera
 * recedes to the vanishing point instead of crossing the screen, so
 * four vertices at the battle's scale are sub-pixel within a tick.
 * The battle gets away with 0xe because its bolts are fired by other
 * craft and cross the view. The muzzle offset is a knob and zero
 * today; it only pushes the bolt further away. */
#define GAME_BOLT_SCALE  0xb
#define GAME_BOLT_AHEAD  0x0

/* OURS - the chase. A bigger shift is a looser camera; the distance
 * is the demo's own 0x60 from command 0x18. */
#define GAME_CAM_EASE    2
#define GAME_CAM_DIST    0x60

/* The sub-parts. A number key picks one, the way sim/run.py does. */
enum { GAME_FLY, GAME_TUNNEL, GAME_RUN, GAME_PAINT, GAME_PARTS };
#define GAME_KEY_SUB0    '1'
#define GAME_KEY_SMEAR   's'

/* OURS - the paint. THE DEMO'S BUMP SCENE IS A PAINT PROGRAM WITH
 * THE BRUSH TAKEN OUT OF ITS HANDS, and that is the whole of this
 * sub-part. Dust_Stamp adds an eight by eight brush into a height
 * field with saturation, Height_Relax diffuses it, and Bump_Render
 * lights the result. The demo drives the stamp from a thousand
 * drifting particles; the mouse does it here instead, and nothing
 * else changes.
 *
 * THE LIGHT CIRCLES BECAUSE A FIXED ONE HIDES THE RELIEF. Bump
 * render takes the light as arguments rather than reading a global,
 * so the game is free to put it where it likes without an accessor
 * - which is also why the bump scene's own randomised light is not
 * borrowed. z wipes the sheet; it is the fire key doing another
 * job, as the sub-parts' keys all do.
 *
 * THE LIGHT IS NOT IN PIXELS. Bump_Render reads the pair SHR 8 as a
 * coordinate in the 256 by 256 picture, so a screen coordinate
 * handed in unscaled parks it. The orbit's three constants are the
 * glow table's centroid and radius - docs/playground.txt 5.4. */
#define GAME_PAINT_LIGHT_CX (0x70 << 8)
#define GAME_PAINT_LIGHT_CY (0x85 << 8)
#define GAME_PAINT_LIGHT_R  (0x3f << 8)

/* OURS - the light's step and the bump scene's own colour kick, img
 * 0x6b1d, which runs with it a tick at a time the way that scene
 * does. s turns the pair off together. */
#define GAME_PAINT_LIGHT_K  0x18
#define GAME_PAINT_PAL_R    0x73
#define GAME_PAINT_PAL_G    0x50
#define GAME_PAINT_PAL_B    0x0b

/* OURS - HOW HIGH THE PEN RAISES THE SURFACE: how many times over a
 * dab lays the brush down. docs/playground.txt 5.4. */
#define GAME_PAINT_WEIGHT   40

/* OURS - the dissolve's dial, x down and c up while held. It meters
 * relax PASSES, the only rate the diffusion has. UNIT is the
 * accumulator's denominator and not a tick count, so the travel is
 * the span that differs rather than mostly rates that look alike -
 * docs/playground.txt 5.4. */
#define GAME_PAINT_DISUNIT  64
#define GAME_PAINT_DISDEF   8
#define GAME_PAINT_DISMAX   (GAME_PAINT_DISUNIT * 2)
#define GAME_PAINT_DISRATE  2

/* OURS - the orbit, and it is built around the FIGURE rather than
 * around the scene's origin.
 *
 * THE DEMO'S EYE IS NOT AIMED AT HIM. It frames him low and to the
 * right, off a bearing of 0x1200 with a pitch of 0x100 - a
 * composition, not a target. Turning that arrangement about the
 * origin therefore orbits a point he is nowhere near, which shows
 * up as a camera swinging around empty space while he drifts.
 *
 * SO THE CENTRE IS MEASURED, NOT ASSUMED: game_run_centre averages
 * the figure's own vertices out of the scene pool after each frame,
 * and the eye is placed at that point plus a turned offset. With
 * the offset built the same way round the projection unbuilds it -
 * Y then X, which is the order Scene_Project rotates in - the two
 * cancel exactly and he lands dead centre at every bearing and
 * every distance. Two Vec_Rotate3_2 calls scale by 0x1000 between
 * them, so the shift after the pair is what hands the vector back
 * at its own size. */
#define GAME_RUN_ROLL0   0x1000
#define GAME_RUN_RDEF    0x800

/* OURS. THE ZOOM IS GEOMETRIC, not a fixed step: a step that reads
 * well far out is a jump up close, and the useful range here spans
 * six doublings. The numerator and denominator are the per-tick
 * ratio. The near limit is deliberately inside the point where the
 * figure starts to come apart - section 5.4 - because filling the
 * screen with a leg is the thing being asked for, and a face that
 * crosses the near plane dropping out is what being inside a model
 * looks like. */
#define GAME_RUN_RMIN    0x30
#define GAME_RUN_RMAX    0x8000
#define GAME_RUN_ZNUM    31
#define GAME_RUN_ZDEN    32
#define GAME_RUN_WHEEL   6

/* Above the 0x177 that behind-the-eye geometry clamps to, so a
 * close-up keeps the near half of him without drawing junk. */
#define GAME_RUN_NEAR    0x180

/* OURS - the walk's slider. Z winds it forward and X back, both
 * while held, and it stops at a turn and a half a tick either way.
 * The demo's own pace is 0x40 and sits a quarter of the way up. */
#define GAME_RUN_STEPMAX 0x300
#define GAME_RUN_STEPRATE 0x8
#define GAME_RUN_ORBIT_K 0x40
#define GAME_RUN_ELEV_K  0x10
#define GAME_RUN_ELEV_MAX 0x600

static LithSlot *const g_slot = g_lith_slot;
static LithMesh g_ship, g_bolt;
static s32 g_bolt_life[GAME_BOLTS];
static s32 g_bolt_next;
static u8 g_back[PD_SCREEN_W * PD_SCREEN_H];
static s32 g_sub;

/* Held keys, one flag each, so steering is a rate and not a nudge.
 * The speed keys are edges, not holds, so one press is one step. */
static s32 g_k_left, g_k_right, g_k_up, g_k_down, g_k_fire;
static s32 g_k_slower, g_k_faster;
static s32 g_speed_idx, g_fire_tick;

/* OURS - the two sub-parts that fly one. The runner and the paint
 * sheet have no craft and no throttle. */
static s32 game_has_craft(void)
{
	return g_sub == GAME_FLY || g_sub == GAME_TUNNEL;
}

/* The runner's orbit. IT HOLDS WHERE IT IS PUT - a drag commits and
 * nothing eases back, which is the opposite of lithium_look.c and
 * the point of having its own state. */
static s32 g_orbit, g_elev, g_radius;
static s32 g_cen_x, g_cen_y, g_cen_z;

/* The paint's field and its light. The field is a whole segment
 * because Height_Relax reads a row past the last it writes and
 * the stamp overhangs - effects/bump.txt. */
static u8 g_paint[LITH_SEG_BYTES];
static s32 g_paint_ang, g_paint_relax, g_paint_lx, g_paint_ly;
static s32 g_paint_x, g_paint_y, g_paint_down;
static s32 g_paint_dis, g_paint_anim;
static s32 g_run_held, g_run_ax, g_run_ay, g_run_o0, g_run_e0;
static s32 g_run_step, g_smear_on;

static void game_sub_enter(s32 n);

/* OURS - src/host.h gives press AND release, which is what a held
 * key needs. Demo_Key routes everything here while the game part is
 * running and nothing here is seen by the demo's own parts.
 *
 * x and c step the craft's speed where there is a craft and hold a
 * slider where there is not: the runner and the paint sheet read
 * the held flags per tick instead, so the same pair of keys is a
 * notch in two sub-parts and a dial in the other two. */
void Lith_GameKey(int ch, int down)
{
	switch (ch) {
	case GAME_KEY_LEFT:  g_k_left = down;  break;
	case GAME_KEY_RIGHT: g_k_right = down; break;
	case GAME_KEY_UP:    g_k_up = down;    break;
	case GAME_KEY_DOWN:  g_k_down = down;  break;
	case GAME_KEY_FIRE:
		if (g_sub == GAME_PAINT) {
			if (down) memset(g_paint, 0, sizeof g_paint);
		} else {
			g_k_fire = down;
		}
		break;
	case GAME_KEY_SMEAR:
		if (!down) break;
		if (g_sub == GAME_RUN) {
			g_smear_on = !g_smear_on;
			Lith_FinaleSmearSet(g_smear_on);
		} else if (g_sub == GAME_PAINT) {
			g_paint_anim = !g_paint_anim;
		}
		break;
	case GAME_KEY_SLOWER:
		g_k_slower = down;
		if (down && game_has_craft() && g_speed_idx > 0)
			g_speed_idx--;
		break;
	case GAME_KEY_FASTER:
		g_k_faster = down;
		if (down && game_has_craft()
		        && g_speed_idx < GAME_SPEED_STEPS - 1)
			g_speed_idx++;
		break;
	default:
		if (down && ch >= GAME_KEY_SUB0
		        && ch < GAME_KEY_SUB0 + GAME_PARTS)
			game_sub_enter(ch - GAME_KEY_SUB0);
		break;
	}
}

static void game_run_zoom(s32 in);
static void game_paint_mouse(s32 x, s32 y, s32 buttons);

/* OURS - one notch of zoom, either way, geometric. */
void Lith_GameWheel(s32 dy)
{
	s32 i;

	if (g_sub != GAME_RUN) return;
	while (dy > 0) {
		for (i = 0; i < GAME_RUN_WHEEL; i++) game_run_zoom(1);
		dy--;
	}
	while (dy < 0) {
		for (i = 0; i < GAME_RUN_WHEEL; i++) game_run_zoom(0);
		dy++;
	}
}

/* OURS - the runner's keys: the cursors orbit and zoom, z and x
 * wind the walk's slider either way. */
static void game_run_keys(void)
{
	if (g_k_up)    game_run_zoom(1);
	if (g_k_down)  game_run_zoom(0);
	if (g_k_left)  g_orbit = (s32)(s16)(g_orbit - GAME_RUN_ORBIT_K);
	if (g_k_right) g_orbit = (s32)(s16)(g_orbit + GAME_RUN_ORBIT_K);

	if (g_k_fire && g_run_step < GAME_RUN_STEPMAX)
		g_run_step += GAME_RUN_STEPRATE;
	if (g_k_slower && g_run_step > -GAME_RUN_STEPMAX)
		g_run_step -= GAME_RUN_STEPRATE;
	Lith_FinaleStepSet(g_run_step);
}

/* OURS - Track_Play's four fields, written from the keyboard.
 *
 * THE SIGNS ARE THE TRACK'S. img 0x5f2a SUBTRACTS the vertical delta
 * and ADDS the horizontal one, so pressing up has to lower pitch_acc
 * the way a mouse pushed forward did. */
static void game_input(LithSlot *o)
{
	if (g_k_left)  o->yaw_acc = (s32)(s16)(o->yaw_acc - GAME_TURN_RATE);
	if (g_k_right) o->yaw_acc = (s32)(s16)(o->yaw_acc + GAME_TURN_RATE);
	if (g_k_up)    o->pitch_acc = (s32)(s16)(o->pitch_acc
	                                         - GAME_PITCH_RATE);
	if (g_k_down)  o->pitch_acc = (s32)(s16)(o->pitch_acc
	                                         + GAME_PITCH_RATE);
	o->command = 0xa + g_speed_idx;
	o->throttle = 1;
}

/* OURS - the gun, launched and flown the way the battle's are.
 *
 * COMMAND 0x19 IS THE OTHER WEAPON AND IS A TRAP HERE. It drives the
 * plasma ring, whose tail zeroes the firing craft's x and z at img
 * 0x5e1e and parks the ship at the origin - so a trigger wired to it
 * teleports the player, and what it draws is a puff rather than a
 * shot. What the battle launches instead is a MESH bolt: the firing
 * craft's position at img 0x66df, its attitude at img 0x6703 and
 * the speed at img 0x6721 go into one of six slots, and
 * Flight_StepRoll flies it with no throttle, no easing and no
 * turning.
 *
 * THE LIFE IS OURS. The demo never expires a bolt; it recycles the
 * six round robin and lets the rest fly on for ever. */
static void game_fire(const LithSlot *o)
{
	LithSlot *b;
	s32 i;

	for (i = 0; i < GAME_BOLTS; i++) {
		if (!g_bolt_life[i]) continue;
		Lith_FlightStepRoll(&g_slot[GAME_BOLT0 + i]);
		g_bolt_life[i]--;
	}
	if (!g_k_fire) return;
	if (++g_fire_tick & GAME_FIRE_EVERY) return;

	{
		s32 ax = 0, ay = 0, az = GAME_BOLT_AHEAD;

		Lith_VecRotate3_2(&ax, &ay, &az, o->pitch, 0, 0);
		Lith_VecRotate3_2(&ax, &ay, &az, 0, o->yaw, 0);
		b = &g_slot[GAME_BOLT0 + g_bolt_next];
		b->home_x = o->home_x + (ax >> 6);
		b->home_y = o->home_y + (ay >> 6);
		b->home_z = o->home_z + (az >> 6);
	}
	b->pitch = o->pitch;
	b->yaw = o->yaw;
	b->roll = o->roll;
	b->speed = GAME_BOLT_SPEED;
	g_bolt_life[g_bolt_next] = GAME_BOLT_TICKS;
	if (++g_bolt_next >= GAME_BOLTS) g_bolt_next = 0;
}

/* OURS - the eye, behind the craft.
 *
 * IT CHASES THE DRAWN SHIP, NOT THE FLOWN ONE, and that distinction
 * is the whole of it. The demo's own eye carried two lags that
 * cancelled - docs/playground.txt 5.3 - so tightening it makes them
 * stop cancelling and the eye ends up IN FRONT of the ship. Taking
 * the offset from home_* and the bearing from a_*, both the drawn
 * values, leaves one lag instead of two. */
static void game_chase(LithSlot *cam, const LithSlot *ship)
{
	s32 vx = 0, vy = 0, vz = -GAME_CAM_DIST;

	Lith_VecRotate3_2(&vx, &vy, &vz, ship->a_x, 0, 0);
	Lith_VecRotate3_2(&vx, &vy, &vz, 0, ship->a_y, 0);

	cam->home_x += ((ship->home_x + (vx >> 6)) - cam->home_x)
	               >> GAME_CAM_EASE;
	cam->home_y += ((ship->home_y + (vy >> 6)) - cam->home_y)
	               >> GAME_CAM_EASE;
	cam->home_z += ((ship->home_z + (vz >> 6)) - cam->home_z)
	               >> GAME_CAM_EASE;
	cam->flight_x = cam->home_x;
	cam->flight_y = cam->home_y;
	cam->flight_z = cam->home_z;
	cam->a_x = ship->a_x;
	cam->a_y = ship->a_y;
	cam->a_z = ship->a_z;
}

/* OURS - keep the craft between the ground and a lid above it. */
static void game_altitude(LithSlot *o)
{
	s32 g = Lith_TerrainHeightAtSlot(o);

	if (o->flight_y < g + GAME_FLOOR) o->flight_y = g + GAME_FLOOR;
	if (o->flight_y > g + GAME_CEILING) o->flight_y = g + GAME_CEILING;
}

/* OURS - the craft and the eye, seeded the way the flyover seeds
 * its own: both the flight and the home copies, so nothing has to
 * settle before the first frame is drawn. */
static void game_fly_enter(void)
{
	s32 i;

	for (i = 0; i < 4; i++) Lith_ObjReset(&g_slot[i]);
	for (i = 0; i < GAME_BOLTS; i++) {
		Lith_ObjReset(&g_slot[GAME_BOLT0 + i]);
		g_bolt_life[i] = 0;
	}
	g_bolt_next = 0;
	Lith_MeshLoad(&g_ship, LITH_MESH_SHIP);
	Lith_MeshLoad(&g_bolt, GAME_BOLT_MESH);
	Lith_SceneDoneClear();
	Lith_RollEaseShift(2);

	g_slot[GAME_SHIP].flight_x = 0x40000;
	g_slot[GAME_SHIP].home_x = 0x40000;
	g_slot[GAME_SHIP].flight_z = 0x0a000;
	g_slot[GAME_SHIP].home_z = 0x0a000;
	g_slot[GAME_SHIP].flight_y = 0x3800;
	g_slot[GAME_SHIP].home_y = 0x3800;
	g_slot[GAME_SHIP].speed = 0x1b;

	g_slot[GAME_CAM].flight_x = 0x40000;
	g_slot[GAME_CAM].home_x = 0x40000;
	g_slot[GAME_CAM].flight_z = 0x0a000;
	g_slot[GAME_CAM].home_z = 0x0a000;

	g_k_left = g_k_right = g_k_up = g_k_down = 0;
	g_k_fire = g_k_slower = 0;
	g_speed_idx = GAME_SPEED_START;
	g_fire_tick = 0;
	Lith_TrailReset();

	Lith_PalSelect(LITH_PAL_1);
	Lith_PalFadeSet(0x80, 0, 8, 8);
	Lith_PalRefresh();
	memset(g_back, 0x80, sizeof g_back);
	Lith_PolyReset();
}

/* OURS - the bore, and it does not end.
 *
 * THE DEMO'S OWN TUNNEL IS ALREADY ENDLESS; what stops it is
 * Lith_TunnelDone asking the music, and nothing here asks. The eye,
 * the path and the sixteen-tick ring window are Tick_Tunnel's at
 * img 0x6b7f, untouched.
 *
 * THE CAMERA SEEDING LIVES IN THE SHATTER'S HANDOVER, not in
 * Lith_TunnelEnter - img 0x0964, 0x09b0 and 0x09b7, which is why
 * this repeats them. docs/playground.txt 5.4.
 *
 * NO STORED PALETTE IS SELECTED. Pal_Animate generates the ramps
 * from the three accumulators and Tick_Tunnel drives it every tick,
 * so the phases are seeded and the colours run from there. */
static void game_tunnel_enter(void)
{
	Lith_TunnelCamInit();
	Lith_TunnelEnter();
	Lith_PalFlashSet(0);
	Lith_PalRampShift(2);
	Lith_PalPhaseSeed(0xf8a3, 0xf6f0, 0xf41b);
	Lith_PalAnimate(Lith_PalStore());
	Lith_PalFadeSet(0, 0, 8, 8);
	Lith_PalRefresh();
}

/* OURS - a notch of zoom. In multiplies the radius down, out
 * multiplies it up, and the clamps are the ends of the range. */
static void game_run_zoom(s32 in)
{
	if (in) {
		g_radius = g_radius * GAME_RUN_ZNUM / GAME_RUN_ZDEN;
		if (g_radius < GAME_RUN_RMIN) g_radius = GAME_RUN_RMIN;
	} else {
		g_radius = g_radius * GAME_RUN_ZDEN / GAME_RUN_ZNUM + 1;
		if (g_radius > GAME_RUN_RMAX) g_radius = GAME_RUN_RMAX;
	}
}

/* OURS - place the eye on its circle.
 *
 * Lith_Sin and Lith_Cos come back scaled to 0x1000, so the rotated
 * pair shifts down twelve. The bearing turns with the offset, which
 * is what keeps the figure in the middle of the frame.
 *
 * THE HEIGHT SCALES WITH THE RADIUS AND HAS TO. Tick_Finale writes
 * the eye's y at img 0x6583 - the bob, about 0x5a - and leaving it
 * there while x and z shrink lifts the camera over the figure's head
 * as it closes, so the zoom runs out long before he fills the
 * frame. Scaling the written value keeps the whole arrangement
 * proportional and keeps the bob. */
static void game_run_centre(void)
{
	const LithScene *sc = &g_lith_scene;
	s32 i, n = sc->nvert, sx = 0, sy = 0, sz = 0;

	if (n < 1) return;
	for (i = 0; i < n; i++) {
		sx += sc->v[i].x;
		sy += sc->v[i].y;
		sz += sc->v[i].z;
	}
	g_cen_x = sx / n;
	g_cen_y = sy / n;
	g_cen_z = sz / n;
}

/* OURS - the orbit. The figure is centred first, so the camera turns
 * about where the runner IS rather than about the scene's origin. */
static void game_run_camera(void)
{
	LithSlot *cam = &g_slot[GAME_CAM];
	s32 ox = 0, oy = 0, oz = g_radius;

	Lith_VecRotate3_2(&ox, &oy, &oz, g_elev, 0, 0);
	Lith_VecRotate3_2(&ox, &oy, &oz, 0, g_orbit, 0);
	ox >>= 0xc;
	oy >>= 0xc;
	oz >>= 0xc;

	Lith_FinaleCamSet(ox - g_cen_x, oz - g_cen_z);
	cam->pos_y = oy - g_cen_y;
	cam->ang_y = (s32)(s16)(-g_orbit);
	cam->ang_x = (s32)(s16)(-g_elev);
	cam->ang_z = GAME_RUN_ROLL0;
}

/* OURS - the runner. The smear is the demo's and goes off here; the
 * accumulator is left holding whatever it had, so switching it back
 * on brings that pose with it. */
static void game_run_enter(void)
{
	Lith_PalSelect(LITH_PAL_FINALE);
	Lith_PalFadeSet(-0xa0, 0, 4, 4);
	Lith_PalRefresh();
	Lith_FinaleEnter();

	g_orbit = 0;
	g_elev = 0;
	g_radius = GAME_RUN_RDEF;
	g_run_held = 0;
	g_run_step = 0x40;
	g_smear_on = 0;
	Lith_FinaleStepSet(g_run_step);
	Lith_FinaleSmearSet(g_smear_on);
	Lith_FinaleNearSet(GAME_RUN_NEAR);
	game_run_camera();
}

/* OURS - a drag that commits. The anchor remembers where the orbit
 * was when the button went down, so releasing leaves it there. */
static void game_run_mouse(s32 x, s32 y, s32 buttons)
{
	if (!(buttons & 1)) {
		g_run_held = 0;
		return;
	}
	if (!g_run_held) {
		g_run_held = 1;
		g_run_ax = x;
		g_run_ay = y;
		g_run_o0 = g_orbit;
		g_run_e0 = g_elev;
		return;
	}
	g_orbit = (s32)(s16)(g_run_o0 + (x - g_run_ax) * GAME_RUN_ORBIT_K);
	g_elev = g_run_e0 + (y - g_run_ay) * GAME_RUN_ELEV_K;
	if (g_elev >  GAME_RUN_ELEV_MAX) g_elev =  GAME_RUN_ELEV_MAX;
	if (g_elev < -GAME_RUN_ELEV_MAX) g_elev = -GAME_RUN_ELEV_MAX;
}

/* OURS - src/host.h. The runner has its own camera and holds it;
 * everything else keeps the easing look of lithium_look.c. */
void Lith_GameMouse(s32 x, s32 y, s32 buttons)
{
	if (g_sub == GAME_RUN) game_run_mouse(x, y, buttons);
	else if (g_sub == GAME_PAINT) game_paint_mouse(x, y, buttons);
	else Lith_LookMouse(x, y, buttons);
}

/* OURS - one dab, the way Dust_Stamp lays one down: an eight by
 * eight add that saturates rather than wraps. */
static void game_paint_dab(s32 x, s32 y)
{
	const u8 *brush = Lith_DustBrush(1);
	u32 bx = (u32)(((y * PD_SCREEN_W) + x) & 0xffff);
	s32 r, c;

	if (x < 0 || x >= PD_SCREEN_W - 8) return;
	if (y < 0 || y >= PD_SCREEN_H - 8) return;
	for (r = 0; r < 8; r++) {
		for (c = 0; c < 8; c++) {
			s32 v = g_paint[bx + c]
			        + brush[r * 8 + c] * GAME_PAINT_WEIGHT;

			g_paint[bx + c] = (u8)(v > 0xff ? 0xff : v);
		}
		bx = (bx + PD_SCREEN_W) & 0xffff;
	}
}

/* OURS - the sheet, the picture and the palette the light reads
 * through.
 *
 * THE PALETTE SEQUENCE IS THE BUMP SCENE'S OWN ORDER, img 0x0765 to
 * 0x078e: seed the three phases, animate once, and steepen the ramp
 * only THEN, so the shift bites from the next tick rather than this
 * one. effects/bump.txt section 7. */
static void game_paint_enter(void)
{
	memset(g_paint, 0, sizeof g_paint);
	Lith_MathBuildSquareTable();
	Lith_TexBuildThreeGlows(Lith_PicBuffer());
	g_paint_ang = 0;
	g_paint_relax = 0;
	g_paint_down = 0;
	g_paint_dis = GAME_PAINT_DISDEF;
	g_paint_anim = 1;
	Lith_PalPhaseSeed(0xf8a3, 0xf6f0, 0xf41b);
	Lith_PalAnimate(Lith_PalStore());
	Lith_PalRampShift(2);
	Lith_PalFadeSet(0, 0, 8, 8);
	Lith_PalRefresh();
}

/* OURS - the mouse is the brush. A DRAG IS JOINED POINT TO POINT,
 * because a mouse is sampled per event and a quick sweep would
 * otherwise come out as a row of dots. */
static void game_paint_mouse(s32 x, s32 y, s32 buttons)
{
	if (!(buttons & 1)) {
		g_paint_down = 0;
		return;
	}
	if (g_paint_down) {
		s32 dx = x - g_paint_x, dy = y - g_paint_y;
		s32 n = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy), i;

		if (n > 64) n = 64;
		for (i = 1; i <= n; i++)
			game_paint_dab(g_paint_x + dx * i / n,
			               g_paint_y + dy * i / n);
	} else {
		game_paint_dab(x, y);
	}
	g_paint_down = 1;
	g_paint_x = x;
	g_paint_y = y;
}

/* OURS - leave one sub-part and arrive in another. */
static void game_sub_enter(s32 n)
{
	g_sub = n;
	switch (n) {
	case GAME_TUNNEL: game_tunnel_enter(); break;
	case GAME_RUN:    game_run_enter();    break;
	case GAME_PAINT:  game_paint_enter();  break;
	default:          game_fly_enter();    break;
	}
}

/* OURS - src/host.h. */
void Lith_GameEnter(void)
{
	game_sub_enter(GAME_FLY);
}

/* OURS - one tick: fly the craft, then chase it.
 *
 * PARKING THE EYE ON THE CRAFT LOOKS LIKE A RENDERER BUG AND IS NOT
 * ONE: the ship is then at distance zero, the near plane clamps it
 * rather than rejecting it - lithium_scene.c - and it smears across
 * the view as a long sliver. game_chase is what keeps it back. */
void Lith_GameTick(void)
{
	LithSlot *cam = &g_slot[GAME_CAM];
	LithSlot *ship = &g_slot[GAME_SHIP];

	if (g_sub == GAME_TUNNEL) {
		Lith_TunnelTick();
		Lith_TickDefault();
		return;
	}
	if (g_sub == GAME_PAINT) {
		if (g_k_slower && g_paint_dis > 0)
			g_paint_dis -= GAME_PAINT_DISRATE;
		if (g_k_faster && g_paint_dis < GAME_PAINT_DISMAX)
			g_paint_dis += GAME_PAINT_DISRATE;
		g_paint_relax += g_paint_dis;
		while (g_paint_relax >= GAME_PAINT_DISUNIT) {
			g_paint_relax -= GAME_PAINT_DISUNIT;
			Lith_HeightRelax(g_paint, 1, 0);
		}
		if (g_paint_anim) {
			g_paint_ang = (s32)(s16)(g_paint_ang
			                         + GAME_PAINT_LIGHT_K);
			Lith_PalPhaseAdd(GAME_PAINT_PAL_R, GAME_PAINT_PAL_G,
			                 GAME_PAINT_PAL_B);
			Lith_PalAnimate(Lith_PalStore());
			Lith_PalRefresh();
		}
		g_paint_lx = (GAME_PAINT_LIGHT_CX + ((Lith_Cos(
		              g_paint_ang) * GAME_PAINT_LIGHT_R) >> 12))
		             & 0xffff;
		g_paint_ly = (GAME_PAINT_LIGHT_CY + ((Lith_Sin(
		              g_paint_ang) * GAME_PAINT_LIGHT_R) >> 12))
		             & 0xffff;
		Lith_TickDefault();
		return;
	}
	if (g_sub == GAME_RUN) {
		game_run_keys();
		Lith_FinaleTick();
		game_run_centre();
		game_run_camera();
		return;
	}

	Lith_FlickerStep();
	game_input(ship);
	Lith_FlightStep(ship, ship);
	game_altitude(ship);
	game_fire(ship);

	game_chase(cam, ship);

	Lith_TrailStep();
	Lith_FlareEnergyStep();
	Lith_TickDefault();
}

/* OURS - the flyover's frame, with one craft instead of three and
 * no tower. The order is the original's because the parts depend on
 * it: the terrain first so the shadow pass has faces to stencil, the
 * sky after the geometry because it reads the 0x80 fill as its only
 * visibility test. effects/flyover.txt.
 *
 * THE TRAIL TAKES THE CAMERA'S WHOLE POSITION, NOT THE SCROLL. The
 * geometry is projected against the sub-cell remainder because
 * Terrain_Build has already centred the grid, but Trail_Draw works
 * in world coordinates - img 0x26ef subtracts the camera outright -
 * so it is handed the position saved before the frame shifts it
 * down four. Passing the scroll puts every bolt off screen. */
static void game_fly_frame(u8 *fb, const u8 *tex, const u8 *glow,
                           LithScene *sc, LithProj *pv, LithSortEnt *sl)
{
	s32 cp[3], ca[3], tp[3], n, i, scroll_x, scroll_z;
	LithObj o;

	Lith_CamLoad(&g_slot[GAME_CAM]);
	Lith_ObjLoadOrientB(&g_slot[GAME_SHIP]);

	tp[0] = g_slot[GAME_CAM].pos_x;
	tp[1] = g_slot[GAME_CAM].pos_y;
	tp[2] = g_slot[GAME_CAM].pos_z;

	Lith_ShadowOrigin(&g_slot[GAME_CAM], &g_slot[GAME_SHIP]);
	Lith_SceneReset(sc);
	Lith_TerrainBuild(sc, g_slot[GAME_CAM].ang_x, g_slot[GAME_CAM].ang_y,
	                  g_slot[GAME_CAM].pos_x >> 0xc,
	                  g_slot[GAME_CAM].pos_z >> 0xc, LITH_ATTR_GLOW);
	scroll_x = (g_slot[GAME_CAM].pos_x & 0xfff) >> 4;
	scroll_z = ((-(g_slot[GAME_CAM].pos_z + 1)) & 0xfff) >> 4;
	g_slot[GAME_CAM].pos_y >>= 4;

	Lith_ObjInit(&o);
	o.ang_x = g_slot[GAME_SHIP].ang_x;
	o.ang_y = g_slot[GAME_SHIP].ang_y;
	o.ang_z = g_slot[GAME_SHIP].ang_z;
	o.pos_x = g_slot[GAME_SHIP].pos_x;
	o.pos_y = g_slot[GAME_SHIP].pos_y;
	o.pos_z = g_slot[GAME_SHIP].pos_z;
	o.axis_swap = 1;
	o.rebase_x = g_slot[GAME_CAM].pos_x;
	o.rebase_z = g_slot[GAME_CAM].pos_z;
	o.scale_shift = 0x10;
	Lith_ObjFaceAttr(LITH_ATTR_SPHERE);
	Lith_MeshTransform(sc, &g_ship, &o);

	for (i = 0; i < GAME_BOLTS; i++) {
		LithSlot *b = &g_slot[GAME_BOLT0 + i];

		if (!g_bolt_life[i]) continue;
		Lith_ObjLoadOrientB(b);
		Lith_ObjInit(&o);
		o.ang_x = b->ang_x; o.ang_y = b->ang_y; o.ang_z = b->ang_z;
		o.pos_x = b->pos_x; o.pos_y = b->pos_y; o.pos_z = b->pos_z;
		o.axis_swap = 1;
		o.rebase_x = g_slot[GAME_CAM].pos_x;
		o.rebase_z = g_slot[GAME_CAM].pos_z;
		o.scale_shift = GAME_BOLT_SCALE;
		Lith_ObjFaceAttr(1);
		Lith_MeshTransform(sc, &g_bolt, &o);
	}

	cp[0] = scroll_x; cp[1] = g_slot[GAME_CAM].pos_y; cp[2] = scroll_z;
	ca[0] = g_slot[GAME_CAM].ang_x;
	ca[1] = g_slot[GAME_CAM].ang_y;
	ca[2] = g_slot[GAME_CAM].ang_z;
	Lith_SceneProject(pv, sc, cp, ca);
	n = Lith_SceneCullSort(sl, pv, sc, LITH_FLYOVER_NEAR);

	memset(g_back, 0x80, sizeof g_back);
	for (i = 0; i < n; i++) {
		s32 f = sl[i].face, idx[3], k;
		LithPoly p;

		idx[0] = sc->f0[f]; idx[1] = sc->f1[f]; idx[2] = sc->f2[f];
		for (k = 0; k < 3; k++) {
			p.x[k] = pv[idx[k]].sx;
			p.y[k] = pv[idx[k]].sy;
			p.u[k] = pv[idx[k]].u;
			p.v[k] = pv[idx[k]].v;
		}
		Lith_PolyFill(g_back,
		              Lith_FaceSegment(sc->attr[f]) == LITH_ATTR_GLOW
		              ? glow : tex, &p, Lith_FaceBand(sc->attr[f]));
		if (sc->attr[f] == LITH_ATTR_GLOW
		    && Lith_ShadowFaceUV(&p, pv, idx, g_slot[GAME_SHIP].ang_y))
			Lith_PolyFillShadow(g_back, Lith_ShadowStencil(), &p);
	}

	Lith_SkySunAndStars(g_back, g_slot[GAME_CAM].ang_x,
	                    g_slot[GAME_CAM].ang_y, g_slot[GAME_CAM].ang_z);
	Lith_FlareRender(g_back);
	Lith_TrailDraw(g_back, tp, ca);
	memcpy(fb, g_back, (size_t)(PD_SCREEN_W * PD_SCREEN_H));
}

/* OURS - the sub-part dispatch.
 *
 * THE TUNNEL DRAWS STRAIGHT TO THE SCREEN and needs no clear: the
 * bore covers every pixel, which is why img 0x0472 blits the whole
 * frame rather than a dirty box. */
void Lith_GameFrame(u8 *fb, const u8 *tex, const u8 *glow,
                    LithScene *sc, LithProj *pv, LithSortEnt *sl)
{
	switch (g_sub) {
	case GAME_TUNNEL:
		Lith_TunnelFrame(fb, tex, sc, pv, sl);
		break;
	case GAME_RUN:
		Lith_FinaleFrame(fb, tex, sc, pv, sl);
		break;
	case GAME_PAINT:
		Lith_BumpRender(g_paint, Lith_PicBuffer(), fb,
		                g_paint_lx, g_paint_ly, 1);
		break;
	default:
		game_fly_frame(fb, tex, glow, sc, pv, sl);
		break;
	}
}
