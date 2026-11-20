/* LITHIUM - where this build puts the members of the structs we read.
 *
 * OURS IN FULL. Nothing here is transcribed from the demo; it exists
 * because the map file carries symbol ADDRESSES and says nothing
 * about layout, so the only way to know where a member sits is to
 * ask the compiler that built the binary the agent is attached to.
 *
 *     build\Win32\Debug\lithium_ppm.exe -offsets
 *
 * WORKING AN OFFSET OUT BY HAND IS THE THING THIS REPLACES. One such
 * sum put a dump of LithScene.attr 64 bytes low, and because the
 * bytes either side of a member are themselves plausible the result
 * read as real data - sixteen faces with an attribute of zero that
 * were not there.
 *
 * THE HEX COLUMN IS THE POINT. The agent's dump takes its offset in
 * hex, which is where that sum went wrong, so it is printed rather
 * than converted.
 *
 * A WORK IN PROGRESS BY DESIGN: it lists what we actually read
 * through the agent and nothing else. docs/agent.txt.
 */

#include <stddef.h>
#include <stdio.h>

#include "lithium.h"
#include "../agent_host.h"

/* OURS. elem is the whole size for a scalar, so one row serves both
 * and an array still says how far it runs - reading past a member
 * into the next is the other half of the mistake above. */
static void row(const char *name, size_t off, size_t total, size_t elem)
{
	unsigned long n = (unsigned long)(elem ? total / elem : 0);

	printf("  %-12s +0x%06lX %9lu  %5lu x %-6lu = %lu\n",
	       name, (unsigned long)off, (unsigned long)off,
	       (unsigned long)elem, n, (unsigned long)total);
}

#define LAY_ONE(S, M) \
	row(#M, offsetof(S, M), sizeof(((S *)0)->M), sizeof(((S *)0)->M))
#define LAY_ARR(S, M) \
	row(#M, offsetof(S, M), sizeof(((S *)0)->M), sizeof(((S *)0)->M[0]))

/* OURS */
static void head(const char *name, size_t total)
{
	printf("\n%s  sizeof %lu (0x%lX)\n", name, (unsigned long)total,
	       (unsigned long)total);
	printf("  %-12s %8s %9s  %s\n", "member", "offset", "dec",
	       "elem x count = bytes");
}

/* OURS - src/agent_host.h. */
void Dbg_PrintLayout(void)
{
	printf("lithium, this build. Offsets are from the symbol's own "
	       "address.\n");

	/* The pool every runner fills and the agent reads face by face. */
	head("LithScene", sizeof(LithScene));
	LAY_ONE(LithScene, nvert);
	LAY_ONE(LithScene, nface);
	LAY_ARR(LithScene, v);
	LAY_ARR(LithScene, f0);
	LAY_ARR(LithScene, f1);
	LAY_ARR(LithScene, f2);
	LAY_ARR(LithScene, attr);

	/* One pool vertex, projected - read by stride out of
	 * g_lith_proj. */
	head("LithProj", sizeof(LithProj));
	LAY_ONE(LithProj, sx);
	LAY_ONE(LithProj, sy);
	LAY_ONE(LithProj, z);
	LAY_ONE(LithProj, u);
	LAY_ONE(LithProj, v);
	LAY_ONE(LithProj, wx);
	LAY_ONE(LithProj, wz);

	/* The flight model's slot, compared field by field against the
	 * demo's own bank - subsystems/objects.txt. */
	head("LithSlot", sizeof(LithSlot));
	LAY_ONE(LithSlot, pos_x);
	LAY_ONE(LithSlot, pos_y);
	LAY_ONE(LithSlot, pos_z);
	LAY_ONE(LithSlot, flight_x);
	LAY_ONE(LithSlot, flight_y);
	LAY_ONE(LithSlot, flight_z);
	LAY_ONE(LithSlot, home_x);
	LAY_ONE(LithSlot, home_y);
	LAY_ONE(LithSlot, home_z);
	LAY_ONE(LithSlot, ang_x);
	LAY_ONE(LithSlot, ang_y);
	LAY_ONE(LithSlot, ang_z);
	LAY_ONE(LithSlot, a_x);
	LAY_ONE(LithSlot, a_y);
	LAY_ONE(LithSlot, a_z);
	LAY_ONE(LithSlot, pitch);
	LAY_ONE(LithSlot, yaw);
	LAY_ONE(LithSlot, roll);
	LAY_ONE(LithSlot, command);
	LAY_ONE(LithSlot, track);
	LAY_ONE(LithSlot, track_tick);
}
