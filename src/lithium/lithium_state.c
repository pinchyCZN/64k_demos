/* LITHIUM - where the demo is, in one line.
 *
 * OURS IN FULL. Nothing here is transcribed; it exists because a
 * frame of the reconstruction and a moment of the original are hard
 * to line up by eye, and an emulator snapshot carries no tick -
 * docs/lithium/subsystems/music.txt section 5.5.
 *
 * THE FORMAT IS THE INTERFACE. Key=value, space separated, stable
 * order, so the same line serves a person reading it and a script
 * diffing two of them. tools/savestate_tick.py names the same three
 * music fields from the other side.
 *
 * IT ONLY READS. The hosts call it while the demo is stopped, so
 * anything it advanced would change what is on screen - host.h.
 */

#include <stdio.h>

#include "../host.h"
#include "../agent_host.h"
#include "lithium.h"

/* One line of the fields below, and the hosts print it as it stands.
 * A DETAIL LINE GETS ITS OWN BUFFER: a host may hold the state line
 * while it walks the details, so sharing one would blank it. */
static char g_line[320];
static char g_detail[640];

/* OURS - src/host.h.
 *
 * THE SLOTS ARE THE POINT. A flight path that is the right shape but
 * the wrong phase looks like a path bug and is not one, so the camera
 * and the lead craft go out in full and the difference between two
 * lines says which. effects/flyover.txt.
 *
 * orgx and orgz ARE THE SHADOW'S, and they carry the craft's height
 * above ground inside them - subsystems/raster.txt 3.3 - so a shadow
 * that tracks wrongly can be told from a craft that flies wrongly.
 *
 * THE POSITIONS ARE THE HOME COPIES AND THE ANGLES ARE THE MODEL'S
 * OWN, for the same reason: both are what the flight model leaves
 * standing between ticks. The live pos_* and ang_* are per-FRAME
 * derivatives - Obj_LoadOrientA reloads both from the slot and the
 * runner then shifts pos_y and ang_x by the two decaying camera
 * rates - so a snapshot taken mid-frame holds the angle from BEFORE
 * that tick's Flight_Step while ours holds the one after, and the
 * two disagree by one ease step while the model itself agrees. The
 * camera reports its A set, which is what Mesh_Transform is handed;
 * a flown craft reports its B set, which is what Flight_Step
 * writes.
 *
 * THE SLOTS ONLY MEAN SOMETHING IN THE FLYOVER, part 3. Before it
 * they are whatever the entry left, which is why part goes out
 * first. */
const char *Demo_StateLine(void)
{
	const LithSlot *cam = Lith_FlyoverSlot(0);
	const LithSlot *ship = Lith_FlyoverSlot(1);
	s32 ox = 0, oz = 0;

	Lith_ShadowOrg(&ox, &oz);
	sprintf(g_line,
	        "t=%ld part=%ld ord=%ld cr=%ld"
	        " camx=%ld camy=%ld camz=%ld campitch=%ld camyaw=%ld"
	        " shipx=%ld shipy=%ld shipz=%ld shippitch=%ld shipyaw=%ld"
	        " orgx=%ld orgz=%ld focus=%ld",
	        (long)Dbg_TickCount(), (long)Lith_Part(),
	        (long)Lith_MusicPos(), (long)Lith_CreditIndex(),
	        (long)cam->home_x, (long)cam->home_y, (long)cam->home_z,
	        (long)cam->a_x, (long)cam->a_y,
	        (long)ship->home_x, (long)ship->home_y, (long)ship->home_z,
	        (long)ship->pitch, (long)ship->yaw,
	        (long)ox, (long)oz, (long)Lith_ShadowFocus());
	return g_line;
}

/* OURS - src/host.h, one line per slot the flyover flies.
 *
 * THE FIELDS ARE IN THE ORDER THE VALUES FLOW: the track fills the
 * two accumulators, the attitude chases those, the flown position
 * integrates from the attitude, the drawn position trails the flown
 * one and the live copies come off the drawn one. So the first field
 * that differs from the original's is the stage that broke, and
 * everything after it is consequence.
 *
 * THE SLOT NUMBER REPORTED IS THE DEMO'S, a byte offset into its
 * parallel arrays, not our index into g_slot - subsystems/objects.txt.
 * Ours 0 and 1 are its 0 and 4.
 *
 * tools/statecmp.py reads the same names off the other side. */
const char *Demo_StateDetail(int n)
{
	static const s32 shown[2] = { 0, 1 };
	const LithSlot *o;

	if (n < 0 || n >= (int)(sizeof shown / sizeof shown[0])) return 0;
	o = Lith_FlyoverSlot(shown[n]);
	sprintf(g_detail,
	        "dt=%ld slot=%ld track=%lu ttick=%ld cmd=%ld thr=%ld"
	        " speed=%ld pitch_acc=%ld yaw_acc=%ld"
	        " pitch=%ld yaw=%ld roll=%ld rollrest=%ld"
	        " a_x=%ld a_y=%ld a_z=%ld ang_x=%ld ang_y=%ld ang_z=%ld"
	        " flight_x=%ld flight_y=%ld flight_z=%ld"
	        " home_x=%ld home_y=%ld home_z=%ld"
	        " pos_x=%ld pos_y=%ld pos_z=%ld",
	        (long)Dbg_TickCount(), (long)(shown[n] * 4),
	        (unsigned long)o->track, (long)o->track_tick,
	        (long)o->command, (long)o->throttle, (long)o->speed,
	        (long)o->pitch_acc, (long)o->yaw_acc,
	        (long)o->pitch, (long)o->yaw, (long)o->roll,
	        (long)o->roll_rest,
	        (long)o->a_x, (long)o->a_y, (long)o->a_z,
	        (long)o->ang_x, (long)o->ang_y, (long)o->ang_z,
	        (long)o->flight_x, (long)o->flight_y, (long)o->flight_z,
	        (long)o->home_x, (long)o->home_y, (long)o->home_z,
	        (long)o->pos_x, (long)o->pos_y, (long)o->pos_z);
	return g_detail;
}
