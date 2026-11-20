/* PCDEMOS 64k - what a demo must add to be debuggable.
 *
 * OURS. src/host.h is the contract every demo already answers; this
 * is the small extra one the agent needs on top of it, and only a
 * demo that wants the agent has to implement it.
 *
 * THE AGENT IS SHARED, THE PARTS ARE NOT. Nothing in src/agent.c
 * knows what a part is beyond an index and a label, which is what
 * lets one agent serve three demos whose part structures have
 * nothing in common.
 *
 * docs/agent.txt.
 */

#ifndef PD_AGENT_HOST_H
#define PD_AGENT_HOST_H

#include "pd.h"

/* Ticks since the demo started. The agent reports this and the tick
 * the pixels actually show, which differ by one because Demo_Frame
 * renders and then advances. */
s32 Dbg_TickCount(void);

/* Where the demo is, and how many places there are to be. */
s32 Dbg_PartIndex(void);
s32 Dbg_PartCount(void);

/* A label for one part, or for the current one when i is negative.
 * Never null - a demo with nothing to say returns a constant. */
const char *Dbg_PartLabel(s32 i);

/* THESE MAY REPLAY RATHER THAN SEEK. Both demos that have parts
 * reach one by running the module to it, so a jump is a replay and
 * takes as long as it takes. */
void Dbg_GotoPart(s32 i);
void Dbg_StepPart(s32 delta);

/* WHERE A STRUCT'S MEMBERS SIT - the map file does not know, so the
 * demo prints its own. docs/agent.txt section 5.1. */
void Dbg_PrintLayout(void);

#endif
