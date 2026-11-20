/* UKKO - the bicycle game that follows the demo.
 *
 * OURS, ALL OF IT. Nothing here is a transcription; the demo ends at
 * keyframe 0x2b6 and this is what happens next. It is kept in its own
 * file so the reconstruction stays separable from the invention.
 *
 * THE GEOMETRY IS DATA, NOT CODE. The bike is a table of primitives
 * in MODEL SPACE, each with its own thickness, and a transform maps
 * them to the screen - which is also the path to an isometric
 * version, since a 3D transform replaces xf_pt and nothing that
 * describes the bike changes. game.txt.
 */

#ifndef UKKO_GAME_H
#define UKKO_GAME_H

#include "../pd.h"

/* What a primitive is. A circle ignores bx and treats by as its
 * radius, which keeps the table one shape rather than a union. */
#define PRIM_LINE    0
#define PRIM_CIRCLE  1

typedef struct {
	s32 kind;
	s32 ax, ay;     /* line start, or circle centre   */
	s32 bx, by;     /* line end, or (unused, radius)  */
	s32 thick;      /* in pixels, 1 is the stick man  */
} Prim;

/* Model space to screen. cs and sn are 8.8 fixed point. */
typedef struct {
	s32 cs, sn;
	s32 tx, ty;
} Xform;

void Game_Init(void);
void Game_Frame(u8 *fb);
void Game_Key(int ch, int down);

/* Read-only, for the test bench: physics that can be measured beats
 * physics judged from pixels. */
s32  Game_ProbeGround(s32 wx);
s32  Game_ProbeSin(s32 a8);
void Game_ProbeState(s32 *x, s32 *y, s32 *ang, s32 *wa, int *air);
void Game_ProbeHead(s32 *yaw, s32 *pitch);   /* 8.8 table steps */

#endif
