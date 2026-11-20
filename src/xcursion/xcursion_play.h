/* OURS - the interactive parts. Nothing here is in the demo.
 *
 * It is a FORK of the car scene, not a hook into it: the physics,
 * the camera and the world are this file's own, so nothing a player
 * part does can reach the reconstruction. docs/playground.txt.
 */

#ifndef XCURSION_PLAY_H
#define XCURSION_PLAY_H

#include "../pd.h"

/* SDL keysyms, because the host passes them through untranslated. */
#define XCUR_PLAY_GAS     'z'
#define XCUR_PLAY_BRAKE   'x'
#define XCUR_PLAY_LEFT    0x40000050L
#define XCUR_PLAY_RIGHT   0x4000004FL
#define XCUR_PLAY_CAMERA  'c'      /* four slots, 90 apart */
#define XCUR_PLAY_NEAR    'v'      /* camera closer        */
#define XCUR_PLAY_FAR     'b'      /* camera further       */
#define XCUR_PLAY_HEIGHT  'a'      /* hug ground / track car */
#define XCUR_PLAY_PALETTE 's'      /* ground colour toggle */
#define XCUR_PLAY_UP      0x40000052L
#define XCUR_PLAY_DOWN    0x40000051L
#define XCUR_PLAY_SWITCH  '1'      /* car <-> ship         */

/* The caller owns the palette and the framebuffer and lends them,
 * which is what keeps this file clear of the demo's statics. */
void Xcur_PlayEnter(u8 *pal, u8 *pal_master);
void Xcur_PlayFrame(u8 *fb, s32 ticks);

/* Non-zero when the key was ours, so the caller can stop looking. */
int Xcur_PlayKey(int ch, int down);

#endif
