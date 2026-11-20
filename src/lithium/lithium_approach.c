/* LITHIUM - the three ships that cross the white.
 *
 * ORIGIN img 0x25a0 maybe_Scene_Approach and img 0x6965
 * Tick_Approach. docs/lithium/effects/battle.txt.
 *
 * IT IS ONE SHIP DRAWN THREE TIMES. There is no formation and no
 * second slot: Obj_Select loads slot 4 once and the runner nudges
 * g_CurPosX and g_CurPosY between three Mesh_Transform calls, so the
 * three craft share an attitude and a depth and differ only by a
 * fixed offset.
 *
 * THE BACKGROUND IS THE ASSEMBLED PICTURE, copied from 0x5ad2 EVERY
 * FRAME at img 0x25c1 - so the shot the glass just put together
 * stays under the ships rather than being drawn again.
 *
 * IT OPENS WHITE. img 0x25a7 sets the fade to +0x80, past neutral,
 * and aims it at -0x100: the scene is the white-out peaking as the
 * ships pass and then falling all the way to black before the
 * battle.
 */

#include <string.h>

#include "lithium.h"

/* The ship, img 0x25ea - the same mesh the battle's near row flies. */
static LithMesh g_ship;

/* ORIGIN img 0x25a0 to 0x25bb - the setup. */
void Lith_ApproachEnter(void)
{
	Lith_MeshLoad(&g_ship, 0x72f8);
	Lith_PalFadeSet(0x80, -0x100, 7, 7);    /* img 0x25a0 */
}

/* ORIGIN img 0x6965 Tick_Approach, and it is two instructions: the
 * ships' depth and then the common tail. Nothing steers them. */
void Lith_ApproachTick(void)
{
	Lith_BattleSlots()[LITH_SLOT(4)].home_z += 0x100;
	Lith_TickDefault();
}

/* ORIGIN img 0x25c1 to 0x2693 - the frame.
 *
 * THE THREE OFFSETS ARE APPLIED TO THE SELECTED OBJECT, not to the
 * slot: img 0x2614 adds 0x100 to g_CurPosY, img 0x2621 takes 0x200
 * back off it and img 0x262b and 0x2638 swing g_CurPosX by -0x1000
 * and then +0x2000. So the first ship rides high and the other two
 * sit low and wide. */
void Lith_ApproachFrame(u8 *fb, const u8 *pic, LithScene *sc,
                        LithProj *pv, LithSortEnt *sl)
{
	LithSlot *slot = Lith_BattleSlots();
	s32 cp[3], ca[3];
	LithObj o;

	memcpy(fb, pic, PD_SCREEN_W * PD_SCREEN_H);     /* img 0x25c1 */

	Lith_CamLoad(&slot[LITH_SLOT(0)]);       /* img 0x25d5 */
	Lith_ObjLoadOrientB(&slot[LITH_SLOT(4)]);
	Lith_SceneReset(sc);                            /* img 0x25e0 */

	Lith_ObjInit(&o);                               /* img 0x2607 */
	o.ang_x = slot[LITH_SLOT(4)].ang_x;
	o.ang_y = slot[LITH_SLOT(4)].ang_y;
	o.ang_z = slot[LITH_SLOT(4)].ang_z;
	o.pos_x = slot[LITH_SLOT(4)].pos_x;
	o.pos_y = slot[LITH_SLOT(4)].pos_y;
	o.pos_z = slot[LITH_SLOT(4)].pos_z;
	o.scale_shift = 0x10;                           /* img 0x25fb */
	o.mirror = 0;
	o.axis_swap = 1;                                /* img 0x25bb */
	o.rebase_x = slot[LITH_SLOT(0)].pos_x;
	o.rebase_z = slot[LITH_SLOT(0)].pos_z;
	Lith_ObjFaceAttr(0);                            /* img 0x260d */

	o.pos_y += 0x100;                               /* img 0x2614 */
	Lith_MeshTransform(sc, &g_ship, &o);
	o.pos_y -= 0x200;                               /* img 0x2621 */
	o.pos_x -= 0x1000;
	Lith_MeshTransform(sc, &g_ship, &o);
	o.pos_x += 0x2000;                              /* img 0x2638 */
	Lith_MeshTransform(sc, &g_ship, &o);

	cp[0] = (slot[0].pos_x & 0xfff) >> 4;           /* img 0x2645 */
	cp[2] = ((-(slot[0].pos_z + 1)) & 0xfff) >> 4;
	slot[0].pos_y >>= 4;
	cp[1] = slot[0].pos_y;
	ca[0] = slot[0].ang_x;
	ca[1] = slot[0].ang_y;
	ca[2] = slot[0].ang_z;
	Lith_SceneDraw(fb, g_lith_tex, sc, pv, sl, cp, ca,
	               LITH_BATTLE_DEPTH_NEAR);
}

/* ORIGIN img 0x2696 - it runs until the music reaches the order the
 * script parked in g_ApproachEndOrder at img 0x02e0. */
s32 Lith_ApproachDone(void)
{
	return Lith_MusicPos() >= LITH_APPROACH_END_ORDER;
}
