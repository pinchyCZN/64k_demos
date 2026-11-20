/* LITHIUM - the flight model, and the slot bank it drives.
 *
 * ORIGIN img 0x59b7 Flight_Step, img 0x5efe Track_Play, img 0x1bdf
 * Obj_Reset, img 0x4fba and 0x4f77 Obj_LoadOrientA and B, img 0x3799
 * Vec_Rotate3_2. docs/lithium/effects/flight.txt and
 * effects/tracks.txt.
 *
 * THE ANIMATION IS RECORDED MOUSE INPUT. Track_Play replays three
 * bytes every six ticks into two accumulators, the attitude chases
 * those, and the position integrates from the attitude - so a
 * hand-flown path comes out smooth without any of it being stored as
 * positions.
 */

#include <string.h>

#include "lithium.h"

/* img 0x8f63 - command 1 raises it and the flyover's loop at img
 * 0x1bce leaves when it is set, so THE SCENE'S END IS IN ITS OWN
 * TRACK rather than on a clock. */
static s32 g_scene_done;
static s32 g_roll_ease;        /* img 0x9026 */

void Lith_RollEaseShift(s32 n) { g_roll_ease = n; }

s32 Lith_SceneDone(void) { return g_scene_done; }
void Lith_SceneDoneClear(void) { g_scene_done = 0; }

/* ORIGIN img 0x3799 Vec_Rotate3_2 - Z, then X, then Y, and IT
 * RETURNS THE VECTOR TIMES 64 like the single-axis helpers; only the
 * stages feeding a later stage take the second shift of 6.
 * effects/flight.txt section 4. Taking the angles as parameters is
 * ours - the original reads g_RotAngle2* at img 0x8f1a. */
void Lith_VecRotate3_2(s32 *x, s32 *y, s32 *z, s32 ax, s32 ay, s32 az)
{
	const s32 *t = Lith_SinTable();
	s32 vx = *x, vy = *y, vz = *z, s, c, t0, t1, i;

	if (!t) return;

	i = (az & 0x1ffc) >> 2;                 /* img 0x379d, Z */
	s = t[i];
	c = t[i + LITH_SIN_QUARTER];
	t0 = (vx * s) >> 6;
	t1 = (vy * s) >> 6;
	vx = (((vx * c) >> 6) - t1) >> 6;
	vy = (((vy * c) >> 6) + t0) >> 6;

	i = (ax & 0x1ffc) >> 2;                 /* img 0x37f9, X */
	s = t[i];
	c = t[i + LITH_SIN_QUARTER];
	t0 = (vy * s) >> 6;
	t1 = (vz * s) >> 6;
	vy = ((vy * c) >> 6) - t1;              /* NOT shifted again */
	vz = (((vz * c) >> 6) + t0) >> 6;

	i = (ay & 0x1ffc) >> 2;                 /* img 0x3852, Y */
	s = t[i];
	c = t[i + LITH_SIN_QUARTER];
	t0 = (vx * s) >> 6;
	t1 = (vz * s) >> 6;
	vx = ((vx * c) >> 6) - t1;
	vz = ((vz * c) >> 6) + t0;

	*x = vx; *y = vy; *z = vz;
}

/* g_TowerFalling img 0x8fcc - Flight_Step arms it on command 0x1a
 * and Tick_TowerFall flies and clears it. effects/towerfall.txt. */
s32 g_lith_tower_falling;

/* ONE BANK, SHARED BY EVERY SCENE. The original's per-field arrays
 * at img 0x8959 and after are globals addressed by a byte offset in
 * SI, so a slot is the same storage whichever scene is running and
 * WHAT ONE SCENE LEAVES THE NEXT INHERITS. Obj_Reset below clears
 * only part of a slot precisely because of that.
 * subsystems/objects.txt. */
LithSlot g_lith_slot[LITH_BATTLE_SLOTS];

/* ORIGIN img 0x1bdf Obj_Reset.
 *
 * IT IS A LIST, NOT A WIPE, and the omissions are load-bearing: the
 * throttle above all carries over between scenes, and the battle's
 * near row depends on it. effects/battle.txt section 2. */
void Lith_ObjReset(LithSlot *o)
{
	o->pitch = 0;                   /* img 0x1bdf */
	o->a_x = 0;
	o->pitch_acc = 0;
	o->yaw_acc = 0;
	o->yaw = 0;
	o->roll = 0;
	o->a_y = 0;
	o->a_z = 0;
	o->roll_rest = 2;               /* img 0x1c17 */
	o->flight_y = 0;                /* img 0x1c1e */
	o->home_y = 0;
	o->speed = 0xa;                 /* img 0x1c32 */
	o->track_tick = 0;              /* img 0x1c3c */
	o->command = 0;
	o->firing = 0;                  /* img 0x1c48 */
}

/* ORIGIN img 0x4fba and img 0x4f77. The two are the same routine but
 * for which angle triple reaches the live one - A for the camera, B
 * for anything the flight model flies. The position is the same in
 * both, and it is HOME rather than FLIGHT: the drawn craft trails its
 * own flight position, see Lith_FlightStep. */
void Lith_ObjLoadOrientA(LithSlot *o)
{
	o->ang_x = o->a_x;
	o->ang_y = o->a_y;
	o->ang_z = o->a_z;
	o->pos_x = o->home_x;
	o->pos_z = o->home_z;
	o->pos_y = o->home_y;
}

/* ORIGIN img 0x4f77 - see Lith_ObjLoadOrientA. */
void Lith_ObjLoadOrientB(LithSlot *o)
{
	o->ang_x = o->pitch;
	o->ang_y = o->yaw;
	o->ang_z = o->roll;
	o->pos_x = o->home_x;
	o->pos_z = o->home_z;
	o->pos_y = o->home_y;
}

/* ORIGIN img 0x5efe Track_Play - effects/tracks.txt.
 *
 * THREE BYTES EVERY SIX TICKS out of the asset segment, and what they
 * hold is RATES, not positions: two signed deltas scaled by 32 into
 * the accumulators the attitude chases, and one packed byte whose low
 * six bits are a command and whose high two are the throttle. */
void Lith_TrackPlay(LithSlot *o)
{
	s32 b0, b1, b2;

	if (++o->track_tick <= 5) return;       /* img 0x5f03 */
	o->track_tick = 0;
	if (!g_lith_img) return;

	b0 = (s32)(s8)g_lith_img[LITH_ASSET_BASE + o->track];
	b1 = (s32)(s8)g_lith_img[LITH_ASSET_BASE + o->track + 1];
	b2 = g_lith_img[LITH_ASSET_BASE + o->track + 2];

	o->pitch_acc = (s32)(s16)(o->pitch_acc - (b1 << 5));
	o->yaw_acc = (s32)(s16)(o->yaw_acc + (b0 << 5));
	o->command = b2 & 0x3f;
	o->throttle = (b2 >> 6) & 3;
	o->track += 3;
}

/* ORIGIN img 0x4107 Terrain_HeightAtSlot. The same 7-bit stride as
 * Terrain_Height, but it NEGATES the byte and shifts left 7 with a
 * 0x40 bias - effects/flight.txt section 6.1.
 *
 * IT READS THE GENERATED MAP, not the image. The asset segment holds
 * zeros there, and a zero byte pins the clamp at -16320 instead of
 * wherever the ground is, so the camera sinks straight through it. */
s32 Lith_TerrainHeightAtSlot(const LithSlot *o)
{
	s32 bh, bl, idx, h;
	const u8 *map;

	map = Lith_Heightmap();

	bh = (-(((o->flight_x - 0x400) >> 0xc) & 0xff)) & 0xff;
	bl = (((o->flight_z - 0x400) >> 0xc) & 0xff) & 0xff;
	idx = (((bh & 0x7f) << 7) | (bl & 0x7f)) & LITH_HEIGHTMAP_MASK;

	h = (s32)(s8)(u8)(-(s32)(u8)(map[idx] - 0x80));
	return (h << 7) + 0x40;
}

/* ORIGIN img 0x59b7 Flight_Step - effects/flight.txt.
 *
 * ROLL IS NEVER RECORDED. It is four times the eased yaw rate -
 * subtracted twice, then twice more after bx doubles - pulled back
 * toward roll_rest by an eighth, so a turn banks and levels itself
 * with no input at all.
 *
 * THE ATTITUDE CHASES THE TRACK rather than taking it: the error
 * against each accumulator is eased by a thirty-second, which is what
 * turns a hand-flown mouse path into something smooth.
 *
 * COMMAND 0x18 IS A CHASE CAM and it OVERWRITES the position with
 * slot 4's plus a rotated offset, which is what keeps the camera on
 * its recorded path. [open] Ten of the thirteen commands are not
 * here - effects/flight.txt 6.2 lists which track uses what.
 *
 * THE CRASH IS THE TAIL, img 0x5dd8 - effects/trail.txt section 5.
 * g_RecordMode is always clear in playback, so the ground clamp
 * below is unconditional here. */
void Lith_FlightStep(LithSlot *o, const LithSlot *lead)
{
	s32 ax, bx, vx, vy, vz;

	ax = (s32)(s16)(o->pitch_acc - o->pitch) >> 5;
	bx = (s32)(s16)(o->yaw_acc - o->yaw) >> 5;

	o->roll = (s32)(s16)(o->roll - bx);
	o->roll = (s32)(s16)(o->roll - bx);
	o->yaw = (s32)(s16)(o->yaw + bx);
	o->pitch = (s32)(s16)(o->pitch + ax);
	bx += bx;
	o->roll = (s32)(s16)(o->roll - bx);
	o->roll = (s32)(s16)(o->roll + (((s32)(s16)(o->roll_rest - o->roll)) >> 3));

	if (o->throttle == 1 || o->throttle == 2) {
		s32 sp = (o->throttle == 1) ? o->speed : -o->speed;

		vx = 0; vy = 0; vz = sp;
		Lith_VecRotate3_2(&vx, &vy, &vz, o->pitch, 0, 0);
		Lith_VecRotate3_2(&vx, &vy, &vz, 0, o->yaw, 0);
		o->flight_x += vx >> 7;
		o->flight_y += vy >> 7;
		o->flight_z += vz >> 7;
	}

	/* img 0x5acf onward - effects/flight.txt 6.2. */
	if (o->command == 2 || o->command == 3) {
		/* A MIRRORED PAIR: img 0x5ad7 rolls one way and strafes with
		 * +speed, img 0x5b44 the other way with -speed. */
		s32 sp = (o->command == 2) ? o->speed : -o->speed;

		o->roll = (s32)(s16)(o->roll + ((o->command == 2) ? 4 : -4));
		vx = sp; vy = 0; vz = 0;
		Lith_VecRotate3_2(&vx, &vy, &vz, o->pitch, 0, 0);
		Lith_VecRotate3_2(&vx, &vy, &vz, 0, o->yaw, 0);
		o->flight_x += vx >> 7;
		o->flight_y += vy >> 7;
		o->flight_z += vz >> 7;
	}
	if (o->command == 0x19) o->firing = 1;          /* img 0x5bb4 */
	if (o->command == 1) g_scene_done = 1;          /* img 0x5bc2 */
	if (o->command == 4) o->flight_y += o->speed << 4;
	if (o->command == 5) o->flight_y -= o->speed << 4;
	if (o->command == 6)
		o->roll_rest = (s32)(s16)(o->roll_rest - 0x20);
	if (o->command == 7)
		o->roll_rest = (s32)(s16)(o->roll_rest + 0x20);
	if (o->command == 8) {
		/* img 0x5c1c - LEVEL THE ROLL TO THE NEAREST 0x2000, eased a
		 * sixteenth at a time, and SNAP once the step rounds to
		 * nothing so it cannot creep for ever. */
		s32 dx = (s32)(s16)((o->roll_rest + 0x1000) & 0xe000);
		s32 step = (s32)(s16)(dx - o->roll_rest) >> 4;

		o->roll_rest = (s32)(s16)(o->roll_rest + step);
		if (!step) o->roll_rest = dx;
	}
	if (o->command >= 0xa && o->command <= 0x17) {
		/* img 0x5c4f - THE SPEED STEPS, 1 to 27 in twos. */
		o->speed = (s32)(s8)(u8)((o->command - 9) & 0xff) * 2 - 1;
	}
	if (o->command == 0x18 && lead) {
		/* img 0x5c89 is MOV ECX, 0xffffffa0  */
		vx = 0; vy = 0; vz = -0x60;
		Lith_VecRotate3_2(&vx, &vy, &vz, o->pitch, 0, 0);
		Lith_VecRotate3_2(&vx, &vy, &vz, 0, o->yaw, 0);
		o->flight_x = (vx >> 6) + lead->flight_x;
		o->flight_y = (vy >> 6) + lead->flight_y;
		o->flight_z = (vz >> 6) + lead->flight_z;
	}

	/* img 0x5cdd - COMMAND 0x1a IS THE SHOT. It copies this craft's
	 * DRAWN position and attitude into slot 0x10, drops it 0x2bc,
	 * zeroes its speed and arms it. Nothing names a target: the
	 * round is launched from whichever slot executed the command,
	 * and the tower-fall tick flies it from there.
	 * effects/towerfall.txt section 3. */
	if (o->command == 0x1a) {
		LithSlot *shot = &g_lith_slot[LITH_SLOT(0x10)];

		shot->home_x = o->home_x;               /* img 0x5ce8 */
		shot->home_z = o->home_z;
		shot->home_y = o->home_y - 0x2bc;       /* img 0x5d06 */
		shot->pitch = o->pitch;                 /* img 0x5d12 */
		shot->yaw = o->yaw;
		shot->roll = o->roll;
		shot->speed = 0;                        /* img 0x5d30 */
		g_lith_tower_falling = 1;               /* img 0x5d3a */
	}

	/* img 0x5d40 - THE GROUND CLAMP, and it is SKIPPED WHILE
	 * RECORDING so the author could fly through terrain laying a
	 * track down and the replay would still ride the surface. */
	{
		s32 g = Lith_TerrainHeightAtSlot(o);

		if (o->flight_y < g) o->flight_y = g;
	}

	/* img 0x5d59 - THE DRAWN POSITION TRAILS THE FLOWN ONE, and the
	 * lag is deliberate: a hard turn makes the model swing wide. */
	o->home_y += (o->flight_y - o->home_y) >> 5;
	o->home_x += (o->flight_x - o->home_x) >> 5;
	o->home_z += (o->flight_z - o->home_z) >> 5;

	/* img 0x5d9b - THE DRAWN ORIENTATION TRAILS THE FLOWN ONE a
	 * quarter a tick, and THIS IS WHAT TURNS THE CAMERA: set B is
	 * what the model writes, set A is what Mesh_Transform gets.
	 * effects/flight.txt section 6. */
	o->a_x = (s32)(s16)(o->a_x + ((s32)(s16)(o->pitch - o->a_x) >> 2));
	o->a_y = (s32)(s16)(o->a_y + ((s32)(s16)(o->yaw - o->a_y) >> 2));
	o->a_z = (s32)(s16)(o->a_z
	                    + (((s32)(s16)(o->roll - o->a_z) >> 2)
	                       >> g_roll_ease));

	/* img 0x5dd8 - a craft the track has marked as hit trails one
	 * weightless particle every eighth tick, and bursts fifty
	 * odd-lifed ones once it comes within 0x280 of the terrain.
	 * effects/trail.txt section 5. */
	if (!o->firing) return;
	o->fire_tick = (o->fire_tick + 1) & 0xff;       /* img 0x5de0 */
	if (o->fire_tick & 7) return;
	{
		LithTrail *t = Lith_TrailSpawn(o);      /* img 0x5ded */

		t->life = 0x32000L;                     /* img 0x5df0 */
		t->vel[0] = 0;
		t->vel[1] = 0x80;
		t->vel[2] = 0;
	}
	if (o->home_y > Lith_TerrainHeightAtSlot(o) + 0x280)
		return;                                 /* img 0x5e07 */
	Lith_TrailSpawnBurst(o, 0x32);                  /* img 0x5e1b */
	/* img 0x5e1e - THE WRECK GOES BACK TO THE ORIGIN. Only x and z
	 * are zeroed, flown and drawn both; y and the attitude are left,
	 * so the craft carries on from the middle of the arena. */
	o->flight_x = 0;
	o->home_x = 0;
	o->flight_z = 0;
	o->home_z = 0;
	o->firing = 0;                                  /* img 0x5e46 */
}
