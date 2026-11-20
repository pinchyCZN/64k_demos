/* UKKO - "StickMan's World", Longjam 1995. The demo's own interface.
 *
 * Addresses are in the DEPACKED image. Ghidra calls that UKKO_U.EXE
 * and its CODE_1 begins at 1020:0000, so a listing address 1020:XXXX
 * is image offset 0x200 + XXXX.
 *
 * GHIDRA PRINTS BRANCH TARGETS AGAINST A 1000: BASE while the listing
 * uses 1020:, so a printed target is 0x200 higher than the address it
 * names. A printed CALL 0x1000:0697 is 1020:0497. Every ORIGIN in
 * this port is a listing address.
 *
 * docs/ukko/README.txt and the notes it points at.
 */

#ifndef UKKO_H
#define UKKO_H

#include "../pd.h"

/* The depacked image, and where its assets sit in it. One place. */
#include "ukko_assets.h"

/* ukko_demo.c owns these; the other modules draw into them. */
extern u8 g_fb[PD_SCREEN_W * PD_SCREEN_H];
extern const u8 *g_img;     /* the depacked image, UKKO_IMAGE_BYTES */

/* ukko_pose.c */
void Ukko_Pose(s32 kf, s32 sub, s16 *out_xy);

/* ukko_line.c */
void Ukko_Line(u8 *fb, s32 x0, s32 y0, s32 x1, s32 y1, u8 colour);

/* ukko_music.c - ibxm plays the module out of the image. */
int  Ukko_MusicInit(s32 rate);
void Ukko_MusicShutdown(void);
void Ukko_MusicRender(s16 *out, s32 frames);

/* ukko_scene.c */
void Ukko_Backdrop(u8 *fb, s32 which);
void Ukko_LogoPlane(u8 *fb, s32 which);
void Ukko_TextScreen(u8 *fb);
s32  Ukko_SceneFor(s32 kf);

#endif
