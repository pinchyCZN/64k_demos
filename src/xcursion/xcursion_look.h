/* OURS - swing the view with the mouse while a scene plays on.
 * docs/playground.txt section 4.
 */

#ifndef XCURSION_LOOK_H
#define XCURSION_LOOK_H

#include "xcursion.h"

/* From the host, in framebuffer pixels. Bit 0 of buttons is held. */
void Xcur_LookMouse(s32 x, s32 y, s32 buttons);

/* Once a frame, to let a released view ease back. */
void Xcur_LookTick(void);

/* THE HOOK, two lines around a scene's drawing. Push applies the
 * offset and remembers what it displaced; pop puts it back, so the
 * simulation never sees the swung camera.
 *
 * THE CRAFT RIDES THE GLOBAL SKY BIAS, NOT THE CAMERA'S, so both are
 * lent - docs/playground.txt section 4.1. */
void Xcur_LookPush(XcurCam *c, s32 *sky_bias);
void Xcur_LookPop(XcurCam *c, s32 *sky_bias);

/* For a renderer whose bearing is a loose argument. Ground_Render has
 * no vertical term, so no pitch goes with it, and nothing is mutated
 * so there is nothing to restore. */
s32 Xcur_LookYaw(void);

/* For a scene whose objects are ALREADY IN CAMERA SPACE. The battle
 * and the chase write tx/ty/tz straight from the animation and never
 * call Mesh_Setup, so the swing is a rotation of each object's
 * position - cam_yaw alone only spins it where it stands.
 * docs/playground.txt section 4.2. */
void Xcur_LookPushMesh(XcurMeshCam *c);
void Xcur_LookPopMesh(XcurMeshCam *c);

/* One camera-space point, for an array the mesh camera does not own. */
void Xcur_LookOrbit(s32 *x, s32 *y, s32 *z);

/* THE POINT THE VIEW TURNS AROUND, in camera space, set once a frame
 * before the draws. Xcur_LookTick clears it, so no part can inherit
 * another part's - and a part that sets none turns about the camera,
 * which is a pan - docs/playground.txt section 4.3. */
void Xcur_LookPivot(s32 x, s32 y, s32 z);

#endif
