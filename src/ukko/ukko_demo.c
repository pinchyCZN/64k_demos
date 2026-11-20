/* UKKO - the demo as one frame at a time.
 *
 * PHASE 1. The palette is real and the contract is real; the picture
 * is a standing-in test pattern until ukko_scene.c and ukko_pose.c
 * land. Nothing here loops.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../host.h"
#include "../pklite.h"
#include "ukko.h"
#include "ukko_game.h"

u8 g_fb[PD_SCREEN_W * PD_SCREEN_H];
const u8 *g_img;

static u8 g_pal[768];
static u8 *g_image_own;
static u32 g_image_len;
static s32 g_frame_no;
static s32 g_kf, g_sub, g_hold, g_held, g_done;
static s32 g_shown[3];

/* OURS - the mouse grab. g_disp is a per-joint offset in 8.8 fixed
 * point; g_hop is the skeleton distance between every pair of joints,
 * which is what decides how far a pull carries.
 *
 * NOTHING HERE CAN DAMAGE THE KEYFRAMES. Ukko_Pose rebuilds its
 * output from the image every frame into a caller's buffer, so the
 * displacement is applied to a stack local that is thrown away 70
 * times a second. The data is read-only by construction, not by
 * discipline. */
#define HOP_FAR  8
static s32 g_disp[UKKO_POSE_JOINTS][2];
static s32 g_hop[UKKO_POSE_JOINTS][UKKO_POSE_JOINTS];
static s16 g_last_xy[UKKO_POSE_JOINTS * 2];
static s32 g_grab = -1;
static s32 g_mx, g_my, g_mdown;

static void build_hops(void);
static void apply_mouse(s16 *xy);

/* The twelve bones, as the joint-offset pairs Demo_FrameLoop
 * sets into g_JointA and g_JointB. Joint n is at byte offset
 * n*4; these are the indices. formats/pose.txt section 3. */
static const u8 BONES[12][2] = {
	{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 1, 4 }, { 4, 5 }, { 0, 6 },
	{ 6, 7 }, { 0, 8 }, { 8, 9 }, { 0, 10 }, { 0, 11 }, { 11, 10 }
};

/* img 1020:f166, f171 and f17f - the interlude frames, and
 * which plane each shows. */
static const s32 LOGO_AT[3] = { 0x06e, 0x193, 0x208 };
/* THE THIRD ONE IS NOT A PLANE, so it is not an index into LOGO[] -
 * it is a full screen with its own blit and its own hold. */
#define HELD_TEXT 3
static const s32 LOGO_WHICH[3] = { 1, 2, HELD_TEXT };

/* OURS - how long each held screen stays up. */
static s32 hold_for(s32 which)
{
	if (which == 0) return UKKO_TITLE_HOLD;
	if (which == HELD_TEXT) return UKKO_SCREEN_HOLD;
	return UKKO_LOGO_HOLD;
}

/* OURS - where [ and ] land: every transition the demo makes.
 * A HOLD AND THE DANCING SHARE A KEYFRAME, so a list of keyframes
 * cannot name both - hence the pair. logo >= 0 is that plane held,
 * -1 is the dancing. Scene switches: img 1020:f053, f067, f07b. */
typedef struct { s32 kf; s32 logo; } Section;
#define SECTIONS 11
static const Section SECTION[SECTIONS] = {
	{ 0x000,  0 },      /* the title, held              */
	{ 0x000, -1 },      /* the lounge                   */
	{ 0x06e,  1 },      /* interlude: good muzac        */
	{ 0x06e, -1 },      /* the lounge again             */
	{ 0x086, -1 },      /* scene 2 - the floor          */
	{ 0x193,  2 },      /* interlude: THE PRODIGY       */
	{ 0x193, -1 },      /* the floor again              */
	{ 0x208, HELD_TEXT },  /* interlude, and scene 1 back */
	{ 0x208, -1 },      /* the lounge                   */
	{ 0x21e, -1 },      /* scene 3 - the kitchen        */
	{    -1, -1 }       /* past the end: the game       */
};

/* ORIGIN depacked 1020:e614 - the only palette the demo programs.
 *
 * A LINEAR GREYSCALE, index >> 2. The DAC index auto-increments from
 * 0 and each entry gets (-cl) >> 2, so index 0 is black and index 255
 * is 0x3f, full white.
 *
 * THE DEMO THROWS THIS AWAY AND WE DO NOT. It is programmed once and
 * then three later INT 10h mode sets reload the DAC without it ever
 * being reprogrammed, so on conforming hardware the whole mode 13h
 * half of the demo is index 255 against a default palette whose top
 * eight entries are black - black on black. Reproducing that bug
 * faithfully renders nothing at all.
 * docs/ukko/subsystems/video.txt.
 */
static void pal_build(void)
{
	s32 cx;
	for (cx = 0x100; cx > 0; cx--) {
		s32 i = 0x100 - cx;
		u8 v = (u8)(((u8)(-(s32)(cx & 0xff)) & 0xff) >> 2);
		g_pal[i * 3 + 0] = v;
		g_pal[i * 3 + 1] = v;
		g_pal[i * 3 + 2] = v;
	}
}

/* OURS */
const char *Demo_Name(void) { return "UKKO"; }

/* OURS - host.h. Nothing to say yet: a state line is only worth
 * the fields a comparison needs, and nothing here is being
 * compared against the original tick by tick. */
const char *Demo_StateLine(void) { return ""; }
const char *Demo_StateDetail(int n) { (void)n; return 0; }
void Demo_Wheel(s32 dy) { (void)dy; }

/* OURS - 70 Hz, which a full run corroborates: 6,577 ticks
 * against the 1 min 32 s capture. */
s32 Demo_TickHz(void) { return 70; }

/* OURS - the packed original, at the path both hosts use. The web
 * build embeds it at this same virtual path, so no code here knows
 * which host it is under. */
const char *Demo_DefaultPath(void) { return "reverse/ghidra/UKKO.EXE"; }

/* OURS - read the packed original and depack it in memory. There is
 * no unpacked file and none ships. */
int Demo_Init(const char *exe_path)
{
	FILE *f;
	u8 *packed;
	long len;
	u32 got = 0;

	f = fopen(exe_path, "rb");
	if (!f) return 1;
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len <= 0) { fclose(f); return 1; }
	packed = (u8 *)malloc((size_t)len);
	if (!packed) { fclose(f); return 1; }
	if (fread(packed, 1, (size_t)len, f) != (size_t)len) {
		free(packed); fclose(f); return 1;
	}
	fclose(f);

	g_image_own = (u8 *)calloc(UKKO_IMAGE_BYTES, 1);
	if (!g_image_own) { free(packed); return 1; }
	if (PkLite_Depack(packed, (u32)len, g_image_own,
	                  UKKO_IMAGE_BYTES, &got)) {
		free(packed);
		free(g_image_own);
		g_image_own = 0;
		return 1;
	}
	free(packed);
	g_img = g_image_own;
	g_image_len = got;

	pal_build();
	memset(g_fb, 0, sizeof g_fb);
	g_frame_no = 0;
	g_kf = 0;
	g_sub = 0;
	g_done = 0;
	g_shown[0] = g_shown[1] = g_shown[2] = 0;
	/* img 1020:e667 - the title plane is held for 0x1a9
	 * retraces before the dance starts. */
	g_held = 0;
	g_hold = UKKO_TITLE_HOLD;
	/* img 1020:e5f5 - the driver is started before the first
	 * frame. A failure here is not fatal: the demo is watchable
	 * without sound and saying so beats refusing to run. */
	Ukko_MusicInit(UKKO_AUDIO_RATE);
	build_hops();
	g_grab = -1;
	memset(g_disp, 0, sizeof g_disp);
	return 0;
}

/* OURS */
void Demo_Shutdown(void)
{
	Ukko_MusicShutdown();
	if (g_image_own) free(g_image_own);
	g_image_own = 0;
	g_img = 0;
}

/* ORIGIN depacked 1020:e6b6 - Demo_FrameLoop, one pass.
 *
 * The backdrop overwrites the back buffer, the twelve bones are drawn
 * over it, and the whole 64,000 bytes go to the screen. There is no
 * clear anywhere - the backdrop IS the clear.
 *
 * g_PartIndex counts 1, then 3 to 9, and only the PartIndex 1 pass
 * reaches the advance at 1020:efda; the tween path jumps over it at
 * 1020:e820. So the cursor moves once every EIGHT display frames and
 * the figure runs at 8.75 keyframes a second while being drawn at
 * seventy. docs/ukko/subsystems/timeline.txt section 2.
 */
static void frame_tick(void)
{
	s32 i;

	if (g_hold > 0) {               /* a title or interlude hold */
		g_hold--;
		return;
	}
	g_held = -1;
	g_sub++;
	if (g_sub < UKKO_SUBFRAMES) return;

	g_sub = 0;
	g_kf++;                         /* img 1020:efda */
	if (g_kf >= UKKO_POSE_FRAMES) { /* img 1020:efe7, then the exit */
		g_done = 1;
		Game_Init();
		return;
	}
	for (i = 0; i < 3; i++)         /* img 1020:f166, f171, f17f */
		if (LOGO_AT[i] == g_kf && !g_shown[i]) {
			g_shown[i] = 1;
			g_held = LOGO_WHICH[i];
			g_hold = hold_for(g_held);
		}
}

/* ORIGIN depacked 1020:e6b6 - one frame, drawn. */
void Demo_Frame(void)
{
	s16 xy[UKKO_POSE_JOINTS * 2];
	s32 i;

	if (!g_img) return;

	if (g_done) { Game_Frame(g_fb); return; }   /* ukko_game.c */

	if (g_held == HELD_TEXT) {
		Ukko_TextScreen(g_fb);
	} else if (g_held >= 0) {
		Ukko_LogoPlane(g_fb, g_held);
	} else {
		Ukko_Backdrop(g_fb, Ukko_SceneFor(g_kf));
		Ukko_Pose(g_kf, g_sub, xy);
		apply_mouse(xy);
		for (i = 0; i < UKKO_POSE_JOINTS * 2; i++)
			g_last_xy[i] = xy[i];
		for (i = 0; i < 12; i++) {      /* the twelve bones */
			s32 a = BONES[i][0], b = BONES[i][1];
			Ukko_Line(g_fb, xy[a * 2], xy[a * 2 + 1],
			          xy[b * 2], xy[b * 2 + 1], UKKO_PEN);
		}
	}
	frame_tick();
	g_frame_no++;
}

/* OURS */
const u8 *Demo_Pixels(s32 *w, s32 *h)
{
	if (w) *w = PD_SCREEN_W;
	if (h) *h = PD_SCREEN_H;
	return g_fb;
}

/* OURS */
const u8 *Demo_Palette(void) { return g_pal; }

/* OURS */
const u8 *Demo_Image(u32 *len)
{
	if (len) *len = g_image_len;
	return g_img;
}

/* ORIGIN depacked 6d6f:1ce3 - Music_Poll's place in the frame. */
void Demo_Audio(s16 *out, s32 frames)
{
	if (out && frames > 0)
		Ukko_MusicRender(out, frames);
}

/* OURS - how many bones apart every pair of joints is.
 *
 * Floyd-Warshall rather than a breadth-first walk: twelve nodes makes
 * it 1,728 trivial iterations and about a third of the code. Joint 0
 * is a hub with five bones on it, so most of the figure is within
 * three hops of anywhere. */
static void build_hops(void)
{
	s32 a, b, k;

	for (a = 0; a < UKKO_POSE_JOINTS; a++)
		for (b = 0; b < UKKO_POSE_JOINTS; b++)
			g_hop[a][b] = (a == b) ? 0 : HOP_FAR;
	for (k = 0; k < 12; k++) {
		g_hop[BONES[k][0]][BONES[k][1]] = 1;
		g_hop[BONES[k][1]][BONES[k][0]] = 1;
	}
	for (k = 0; k < UKKO_POSE_JOINTS; k++)
		for (a = 0; a < UKKO_POSE_JOINTS; a++)
			for (b = 0; b < UKKO_POSE_JOINTS; b++)
				if (g_hop[a][k] + g_hop[k][b] < g_hop[a][b])
					g_hop[a][b] = g_hop[a][k] + g_hop[k][b];
}

/* OURS - drag the figure about, and let go of it.
 *
 * HELD: the grabbed joint follows the pointer exactly and the rest
 * follow by halves, one halving per bone of separation - so pulling a
 * hand swings the forearm, nudges the shoulder and leaves the far
 * foot where it was.
 *
 * RELEASED: the offset is multiplied by 7/8 a frame. That is the
 * demo's own easing shape - Pose_Tween is a weighted blend and a
 * shift - and it reaches zero on its own in about twenty frames
 * without a velocity term or a spring to tune.
 *
 * The arithmetic is division rather than a shift because C89 leaves
 * a right shift of a negative value implementation-defined, and half
 * of these are negative.
 */
static void apply_mouse(s16 *xy)
{
	s32 j;

	if (g_grab >= 0) {
		s32 dx = g_mx - (s32)xy[g_grab * 2];
		s32 dy = g_my - (s32)xy[g_grab * 2 + 1];
		for (j = 0; j < UKKO_POSE_JOINTS; j++) {
			s32 h = g_hop[g_grab][j];
			s32 w = (h >= HOP_FAR) ? 0 : (256 >> h);
			g_disp[j][0] = dx * w;
			g_disp[j][1] = dy * w;
		}
	} else {
		for (j = 0; j < UKKO_POSE_JOINTS; j++) {
			g_disp[j][0] = g_disp[j][0] * 7 / 8;
			g_disp[j][1] = g_disp[j][1] * 7 / 8;
		}
	}
	for (j = 0; j < UKKO_POSE_JOINTS; j++) {
		xy[j * 2] = (s16)((s32)xy[j * 2] + g_disp[j][0] / 256);
		xy[j * 2 + 1] = (s16)((s32)xy[j * 2 + 1] + g_disp[j][1] / 256);
	}
}

/* OURS - whichever joint the pointer is nearest, by squared distance
 * off the frame just drawn. */
static s32 nearest_joint(s32 x, s32 y)
{
	s32 j, best = 0, bd = 0x7fffffffL;

	for (j = 0; j < UKKO_POSE_JOINTS; j++) {
		s32 dx = x - (s32)g_last_xy[j * 2];
		s32 dy = y - (s32)g_last_xy[j * 2 + 1];
		s32 d = dx * dx + dy * dy;
		if (d < bd) { bd = d; best = j; }
	}
	return best;
}

/* OURS */
void Demo_Mouse(s32 x, s32 y, s32 buttons)
{
	g_mx = x;
	g_my = y;
	if (buttons & 1) {
		if (!g_mdown) {
			g_mdown = 1;
			g_grab = nearest_joint(x, y);
		}
	} else {
		g_mdown = 0;
		g_grab = -1;
	}
}

static void enter_section(Section n);

/* OURS - step a section, landing on the dancing not on a hold.
 *
 * THE MUSIC IS NOT MOVED. Demo_Audio pulls from the replayer and
 * knows nothing about g_kf, so picture and music stop being in the
 * relationship the real demo had. That is the point of it. */
static void jump_section(s32 d)
{
	s32 i, at = 0;

	/* Two entries share a keyframe, so a hold currently being shown
	 * is what tells them apart. */
	if (g_done) {
		at = SECTIONS - 1;          /* the game is the last section */
	} else {
		for (i = 0; i < SECTIONS; i++)
			if (SECTION[i].kf >= 0 && SECTION[i].kf <= g_kf
			        && (SECTION[i].logo < 0) == (g_held < 0))
			    at = i;
	}
	enter_section(SECTION[(at + d + SECTIONS) % SECTIONS]);
}

/* OURS - put the demo in a section, wherever it was.
 *
 * A NEGATIVE KEYFRAME IS THE GAME, which lives past the end of the
 * pose data and so has no keyframe of its own. */
static void enter_section(Section n)
{
	s32 i;

	if (n.kf < 0) {
		g_done = 1;
		g_held = -1;
		g_hold = 0;
		Game_Init();
		return;
	}
	g_kf = n.kf;
	g_sub = 0;
	g_done = 0;
	g_held = n.logo;
	if (n.logo < 0)
		g_hold = 0;
	else
		g_hold = hold_for(n.logo);

	/* Interludes ahead still fire, ones jumped over do not, and
	 * landing ON one counts as shown or it fires again on leaving. */
	for (i = 0; i < 3; i++)
		g_shown[i] = (n.logo >= 0) ? (LOGO_AT[i] <= n.kf)
		                           : (LOGO_AT[i] < n.kf);
}

/* OURS - src/agent_host.h, and main_ppm.c -scene reaches it. */
void Dbg_GotoPart(s32 i)
{
	if (i < 0) i = 0;
	if (i >= SECTIONS) i = SECTIONS - 1;
	enter_section(SECTION[i]);
}

/* OURS */
void Demo_Key(int ch, int down)
{
	if (down && ch == ']') jump_section(1);
	else if (down && ch == '[') jump_section(-1);
	else if (g_done) Game_Key(ch, down);
}
