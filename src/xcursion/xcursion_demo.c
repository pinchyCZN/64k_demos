/* XCURSION - the Demo_* contract. host.h.
 *
 * This file is the seam and the scene table. The demo itself has no
 * dispatcher: Demo_Entry runs its parts as straight-line code, each
 * one a parameter block, a music-position target and a renderer that
 * loops until the music reaches it. The table here is a reading of
 * that straight line, which is what lets [ and ] land on a part.
 *
 * Ten parts are reconstructed, img 0x0000 to the moire at img 0x0457.
 * What follows the moire - img 0x045d, mode 8 - is not read, so the
 * run holds its last frame there rather than inventing an ending.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../host.h"
#include "../xcurpack.h"
#include "xcursion.h"
#include "xcursion_play.h"
#include "xcursion_look.h"

const u8 *g_img;
u32 g_image_len;

static u8 *g_image_own;
static u8 g_fb[XCUR_FB_BYTES];
static u8 g_pal[768];
static u8 g_texture[0x10000];           /* height AND the flame   */
static u8 g_colour[0x10000];
static u8 g_water[0x4000];
static XcurCam g_cam;
static XcurCamPhase g_phase;
static XcurModel g_ship;
static XcurSpan g_span;

/* OURS - a scene cursor, so [ and ] can land on one to look at
 *
 * THE BOUNDARY IS GENUINELY SOFT. The captions keep burning across
 * both of these - FireText_Render is called at sync 0x04 AND 0x07 -
 * and the craft appears partway through, so there is no frame where
 * one stops and the next starts. */
#define XCUR_SCENES 35
/* OURS - xcursion_play.c, and it is not on the music. */
#define XCUR_SCENE_PLAY 34
static s32 g_scene;
/* The four animation modes the opening runs through, and the order
 * each takes over on - read from a watch on img 0xa881.
 * effects/camera.txt section 4. */
#define XCUR_SYNC_APPROACH 5
#define XCUR_SYNC_LAUNCH   7
#define XCUR_SYNC_PLANET   0x0a
#define XCUR_SYNC_ROTO     0x0c
#define XCUR_SYNC_STARS    0x0d
#define XCUR_SYNC_ROTO2    0x0e
#define XCUR_SYNC_WARP     0x12
#define XCUR_SYNC_MOIRE    0x14
#define XCUR_SYNC_CHASE1   0x15
#define XCUR_SYNC_CHASE2   0x16
#define XCUR_SYNC_FIGHT1   0x19
#define XCUR_SYNC_FIGHT2   0x1b
#define XCUR_SYNC_FIGHT3   0x21
#define XCUR_SYNC_AFTER    0x23
#define XCUR_SYNC_WARPOUT  0x25
#define XCUR_SYNC_ROTOSLOW 0x26
#define XCUR_SYNC_BEAMS1   0x28
#define XCUR_SYNC_BEAMS2   0x29
#define XCUR_SYNC_TUNNEL   0x2a
#define XCUR_SYNC_TUNNEL2  0x2b
#define XCUR_SYNC_WATER    0x2d
#define XCUR_TUNNEL_FADE   0xa5
#define XCUR_SYNC_EXIT     0x32
#define XCUR_SYNC_SPACE    0x33
#define XCUR_SYNC_MOON     0x34
#define XCUR_SYNC_ROTOGND  0x35
#define XCUR_SYNC_FLIGHT   0x37
#define XCUR_SYNC_CAR      0x3a
#define XCUR_SYNC_CREDITS  0x4a
#define XCUR_SYNC_THEEND   0x4b
#define XCUR_SYNC_ENDPLASMA 0x4c
#define XCUR_SYNC_ENDWATER 0x52
static XcurObj g_obj;
static s32 g_tgt_x, g_tgt_y;            /* img 0x9368, img 0x936c */
static XcurMeshCam g_mc;                /* held, for the launch   */
static s32 g_fade, g_fade_prev;         /* img 0x97dd, img 0x97df */
static s32 g_launch_div, g_launch_step; /* img 0xa882, img 0x98f5 */
static u16 g_sphere[XCUR_SPHERE_DIM * XCUR_SPHERE_DIM];
static XcurPoint g_stars[XCUR_STARS];
static s32 g_pan, g_approach;           /* img 0x9877, img 0x9464 */
static s32 g_star_x, g_star_y, g_star_z; /* img 0x9919, 0x991b, 0x991d */
static s32 g_roto_zoom, g_roto_angle;   /* img 0x94bc, img 0x9961 */
static s32 g_roto_phase;                /* img 0xa87b             */
static s32 g_warp_ofs[XCUR_WARP_OFS];   /* img 0x98dd..0x98e8     */
static s32 g_warp_ph[XCUR_WARP_OFS];    /* img 0x98e9..0x98f4     */
static s32 g_fade_target;               /* img 0x991f             */
static u8 g_pal_master[768];            /* img 0xc7a4             */
static u8 g_bg[XCUR_FB_BYTES];
static XcurParticle g_part[XCUR_COUNTDOWN_MAX];
static s32 g_npart, g_window, g_burst, g_digits;
static s32 g_sky_bias;                  /* img 0x996d - 0 at start */
static s32 g_warp_x, g_warp_y;
static s32 g_moire_phase;               /* img 0x9921             */
static s32 g_anim_mode;                 /* img 0xa881             */
/* The battle. Each craft carries its own position and attitude, and
 * Battle_Draw* copies one into the shared object slot at a time. */
static s32 g_a_x, g_a_y, g_a_z;         /* img 0x94c0, 0x94c4, c8 */
static s32 g_a_pitch, g_a_yaw, g_a_roll;/* img 0x9955, 0x9957, 59 */
static s32 g_b_x, g_b_y, g_b_z;         /* img 0x94cc, 0x94d0, d4 */
static s32 g_b_pitch, g_b_yaw, g_b_roll;/* img 0x995b, 0x995d, 5f */
static s32 g_rot_pitch, g_rot_yaw, g_rot_roll;  /* img 0x99a9..ad  */
static XcurModel g_craft_b, g_bolt, g_winged;
static const XcurModel *g_obj_model;
static XcurShot g_shot[XCUR_SHOTS];
static XcurSpark g_spark[XCUR_SPARKS];
static u8 *g_glowmem;
static s32 g_glow_src, g_glow_dst;
static s32 g_shot_next;                 /* img 0x9923             */
static s32 g_spawn_count;               /* img 0x9925             */
static s32 g_spark_life, g_spark_shift; /* img 0xa59b, img 0xa59a */
static s32 g_zscale;                    /* img 0x9374             */
static s32 g_pal_phase[3];              /* img 0x9949, 4b, 4d     */
static s32 g_angle_rate;                /* img 0x9953             */
static s32 g_zoom_vel;                  /* img 0x9468             */
/* The tunnel. The depth table IS the colour map - segment 0x4a79 -
 * which is why the glow and the tunnel can never coexist; the other
 * two tables get arrays of their own because segment 0x6a79's other
 * tenants, the sphere table and craft B's vertices, are finished. */
static u8 g_depth8[XCUR_FB_BYTES];      /* segment 0x7a79         */
static u8 g_angle_tab[XCUR_FB_BYTES];   /* segment 0x6a79         */
static s32 g_tun_depth;                 /* img 0x946c, 8.8        */
static s32 g_tun_angle;                 /* img 0x9927             */
static s32 g_tun_parity;                /* img 0xa87a             */
static s32 g_tun_fade;                  /* img 0xa59c             */
/* The water. TWO BUFFERS BECAUSE THE SOLVER NEEDS THE OLD VALUE -
 * the demo puts them in the work buffer and the texture segment and
 * clears both, which is all either is doing by then. Padded, because
 * the solver runs a row past the grid. */
static s16 g_wat_a[XCUR_WATER_WORDS];   /* img 0x993f, 0x5a79     */
static s16 g_wat_b[XCUR_WATER_WORDS];   /* img 0x9941, 0x2a79     */
static s16 *g_wat_dst, *g_wat_src;
static s32 g_wat_damp;                  /* img 0xa59f             */
/* Segment 0x5a79 - a whole segment, effects/theend.txt section 4. */
static u8 g_comp[0x10000];
static s32 g_end_phase;                 /* img 0x9939             */
static s32 g_end_amp;                   /* img 0x9474, a DWORD    */
static s32 g_end_acc;                   /* OURS - scene_endwater  */
static s32 g_fade_div;                  /* img 0xa887             */
static s32 g_tick_target;               /* img 0x9917             */
static s32 g_roto_blend;                /* img 0xa59d             */
static XcurModel g_car, g_flagpost, g_letters;
static s32 g_car_x, g_car_y, g_car_z;   /* img 0x9478, 7c, 80     */
static s32 g_car_vy;                    /* img 0x9384             */
static s32 g_car_speed;                 /* img 0x9470             */
static s32 g_car_steer;                 /* img 0x98fd             */
static s32 g_car_pitch_t, g_car_roll_t; /* img 0x993b, 0x993d     */
static s32 g_cam_lead;                  /* img 0x98f7             */
static s32 g_cam_h2, g_cam_h3;          /* img 0x9873, 0x9875     */
static XcurScrollRow g_srow[0x80];
static s32 g_sprev[XCUR_STARS];
static u8 g_sfont[0x800];
static s32 g_scroll_base;               /* img 0x9380             */
static s32 g_scroll_pos;                /* img 0x937c             */
static s32 g_scroll_wob;                /* img 0x98d5             */
static s32 g_cam_yofs;                  /* img 0x9450             */
static s32 g_water_scroll;              /* img 0x997b             */
static s32 g_o2_pitch, g_o2_yaw, g_o2_roll; /* img 0x995b, 5d, 5f */
static s32 g_ship_x, g_ship_y, g_ship_z;    /* img 0x9484, 88, 8c */
static s32 g_ship_pitch, g_ship_yaw, g_ship_roll; /* 0x9943,45,47 */
static s32 g_tick_acc;                  /* OURS - see Demo_Frame  */
static s32 g_ticks;
static s32 g_frame;                     /* OURS - Demo_StateLine  */
static u32 g_track_ptr;                 /* img 0x98b9             */
static s32 g_throttle;                  /* img 0x9378             */
static s32 g_cam_wx_src, g_cam_wz_src;  /* img 0x9360, 0x9364     */
static s32 g_cam_h;                     /* img 0x9871             */

static void scene_enter(s32 which);
static void ticks_run(void (*fn)(void));
static void cam_follow(void);
static void demo_restart(void);

/* OURS. While this is set the scenes still run their state but draw
 * nothing, which is what makes a seek cheap enough to be instant. */
static s32 g_seeking;
static s32 g_cap_no;          /* the caption cursor, OURS */
static u8 g_mask[PD_SCREEN_W * PD_SCREEN_H];
static s32 g_cap_ofs, g_cap_rows;
static s32 g_thr, g_thr_dir, g_texbase;

static void caption(void);
#define THR_LO (-0x30)          /* img 0x987f's bounds, firetext 6 */
#define THR_HI 0x110
static char g_name[64] = "XCURSION";

const char *Demo_DefaultPath(void) { return "reverse/ghidra/XCURSION.EXE"; }
const char *Demo_Name(void) { return g_name; }

/* OURS - host.h, printed on pause and on a single step.
 *
 * t IS THE FRAME INDEX AND IT IS 0-BASED, which is what makes it
 * useful: `xcursion_ppm -skip t -n 1` dumps this exact frame, and
 * names the file after the same number. seek_scene restarts and
 * replays through Demo_Frame, so a seek leaves t truthful rather
 * than running ahead of the picture. */
const char *Demo_StateLine(void)
{
	static char line[96];
	s32 order = 0, row = 0;

	Xcur_SeqPos(&order, &row);
	sprintf(line, "t=%ld scene=%ld ord=%ld row=%ld",
	        (long)g_frame, (long)g_scene, (long)order, (long)row);
	return line;
}
const char *Demo_StateDetail(int n) { (void)n; return 0; }
void Demo_Wheel(s32 dy) { (void)dy; }

/* OURS - the host's old fixed rate, kept so nothing here changes
 * speed. [open] NOT READ OUT OF THE BINARY, unlike LITH_TICK_HZ. */
s32 Demo_TickHz(void) { return 70; }

/* OURS */
static u8 *slurp(const char *path, long *len_out)
{
	FILE *f = fopen(path, "rb");
	u8 *buf;
	long len;

	if (!f) return 0;
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len <= 0) { fclose(f); return 0; }
	buf = (u8 *)malloc((size_t)len);
	if (!buf) { fclose(f); return 0; }
	if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
		free(buf); fclose(f); return 0;
	}
	fclose(f);
	*len_out = len;
	return buf;
}

/* OURS - read the packed original and depack it in memory. There is
 * no unpacked file and none ships. */
static int load_image(const char *exe_path)
{
	long len = 0;
	u8 *packed;
	u32 got = 0;
	int rc;

	packed = slurp(exe_path, &len);
	if (!packed) return 1;

	g_image_own = (u8 *)calloc(XCUR_IMAGE_BYTES, 1);
	if (!g_image_own) { free(packed); return 1; }
	rc = XcurPack_Depack(packed, (u32)len, g_image_own,
	                     XCUR_IMAGE_BYTES, &got);
	free(packed);
	if (rc != XCURPACK_OK) {
		free(g_image_own);
		g_image_own = 0;
		return 1;
	}
	g_img = g_image_own;
	g_image_len = got;
	return 0;
}

/* OURS */
int Demo_Init(const char *exe_path)
{
	if (load_image(exe_path)) return 1;

	Xcur_PalBuild(g_pal_master);
	Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	Xcur_WaterFieldBuild(g_water);
	Xcur_SkyBuild();                    /* img 0x00bb */
	Xcur_MeshParse(&g_letters, XCUR_LETTERS);
	Xcur_MeshParse(&g_car, XCUR_CAR);
	Xcur_MeshParse(&g_flagpost, XCUR_FLAGPOST);
	Xcur_MeshParse(&g_ship, XCUR_SHIP);
	/* img 0x014c, before any part runs. The model's normals go in its
	 * own vertex segment, so each model is done once. */
	Xcur_MeshNormals(&g_ship, &g_mc, XCUR_MESH_ZSCALE);
	Xcur_SpanReset(&g_span);
	demo_restart();
	Xcur_MusicInit(XCUR_AUDIO_RATE);
	sprintf(g_name, "XCURSION");
	return 0;
}

/* OURS - everything a run starts from, so a seek can replay the demo
 * from tick zero. IT MUST RESTART THE RANDOM STREAM TOO: the scene
 * entries draw their fields from one shared stream in call order
 * (xcursion_plasma.c), so resuming it leaves every field after the
 * first a different cloud. */
static void demo_restart(void)
{
	g_frame = -1;           /* the first frame is 0 - Demo_StateLine */
	Xcur_RndSeed();
	/* SEGMENT 0x4a79 IS THE VOXEL'S COLOUR MAP and the glow's two
	 * buffers lie inside it - so the first glow frames blur whatever
	 * the landscape left there, and no part clears it before the
	 * sync-0x19 one reaches order 0x1a. That faint fading square is
	 * the demo's, not a fault here.
	 *
	 * img 0x98b1 and img 0x98b3 are statics with no writer but
	 * Glow_Render's own half-swap - xcursion_battle.c. */
	g_glowmem = g_colour + 0x1800;
	g_glow_dst = XCUR_GLOW_A;
	g_glow_src = XCUR_GLOW_B;
	g_anim_mode = 0;
	g_shot_next = 0;
	g_spawn_count = 0;                  /* img 0x9925's static */
	g_spark_life = XCUR_SPARK_LIFE;
	g_spark_shift = XCUR_SPARK_SHIFT;
	/* img 0x9374 and img 0x9370 are globals with statics, and the only
	 * writer of either before the warp part is none - so every object
	 * drawn through g_mc carries them. Leaving them zero collapses
	 * each model's z to nothing and drops its shading to the band's
	 * darkest entry. */
	g_zscale = XCUR_MESH_ZSCALE;
	g_mc.zscale = XCUR_MESH_ZSCALE;
	g_mc.shade_scale = XCUR_SHADE_SCALE;
	/* img 0x9883, 0x9885 and 0x990f are globals with statics too, and
	 * the parts OVERRIDE ONLY WHAT THEY WRITE - img 0x019a sets the
	 * pitch and roll and leaves the yaw alone. Starting them at zero
	 * lights the launch from somewhere the demo never does. */
	g_mc.light_pitch = XCUR_LIGHT_PITCH;
	g_mc.light_yaw = XCUR_LIGHT_YAW;
	g_mc.light_roll = XCUR_LIGHT_ROLL;
	memset(g_spark, 0, sizeof g_spark);
	memset(g_shot, 0, sizeof g_shot);
	/* img 0x00a4 - part 0's own field, and the flame texture is this
	 * call with no post-transform (effects/firetext.txt section 7).
	 * ONE BUFFER SERVES BOTH, segment 0x2a79 - voxel.txt 1.1. */
	Xcur_PlasmaGenerate(g_texture, 0x7fff, 0x4000, 3);
	Xcur_TerrainBuildShade(g_colour, g_texture, 0x1000, 0x53);

	/* img 0x01fd, 0x207, 0x287 and 0x210 - the statics. */
	g_cam.wx = -0xa000L;
	g_cam.wy = -0xa000L;
	g_cam.angle = 0x0100;
	g_cam.scale = 0x0b00;
	g_cam.hscale = 0x1000;
	g_cam.yofs = -3200;
	Xcur_SeqInit(XCUR_S3M_OFF);
	g_obj.pitch = 0;                    /* img 0x9909 */
	g_obj.yaw = XCUR_OBJ_YAW;           /* img 0x9881 */
	g_obj.roll = 0;                     /* img 0x990b */
	g_tgt_x = XCUR_TARGET_X;            /* img 0x9368 - mode 2 moves */
	g_tgt_y = XCUR_TARGET_Y;            /* img 0x936c   both of these */
	g_phase.a = 0;
	g_phase.b = 0;
	g_phase.c = 0;
	g_fade = XCUR_FADE_START;
	g_fade_prev = XCUR_FADE_START;
	g_cam.water_level = 0x21;
	g_cam.water_scroll = 0x22;
	/* img 0x0109 - THE ALTITUDE IS THE GROUND UNDER THE TARGET, not
	 * a static, and img 0x0119 is MOVZX. Seeding it later leaves the
	 * parked craft out of range for the whole opening -
	 * effects/camera.txt section 5. */
	{
		s32 kind = 0;
		s32 h = Xcur_TerrainHeightAt(g_texture, g_water, g_tgt_x,
		                             g_tgt_y, g_cam.water_level,
		                             g_cam.water_scroll, g_cam.hscale,
		                             &kind);
		g_cam.alt = (h & 0xffff) + 0xc8;        /* img 0x011d */
	}
	memset(g_fb, 0, sizeof g_fb);
	g_thr = THR_HI;             /* img 0x987f's static value */
	g_thr_dir = -1;
	g_texbase = 0;              /* img 0x9907's */
	g_pan = XCUR_PLANET_PAN;
	g_approach = XCUR_PLANET_NEAR;
	g_launch_div = 0;
	g_launch_step = 0;
	g_sky_bias = 0;
	g_digits = 0;
	g_warp_x = 0;
	g_warp_y = 0;
	g_cap_no = 0;
	g_scene = 0;
	caption();
}

/* OURS */
void Demo_Shutdown(void)
{
	Xcur_MusicShutdown();
	free(g_image_own);
	g_image_own = 0;
	g_img = 0;
	g_image_len = 0;
}

/* ORIGIN img 0x330f. Wraps, so the three captions cycle. */

static void caption(void)
{
	g_cap_rows = Xcur_TextBuildMask(g_mask,
	        (u32)(XCUR_FIRETEXT_RECS
	              + (g_cap_no % XCUR_FIRETEXT_N) * XCUR_REC_BYTES),
	        &g_cap_ofs);
	g_cap_no++;
}

/* ORIGIN img 0x0db4 and its tick work at img 0x0e45 - mode 0. A
 * TRIANGLE WAVE WHOSE TOP ADVANCES THE TEXT, which is the whole of
 * the opening's sequencing - effects/firetext.txt section 6. */
static void anim_tick0(void)
{
	g_thr += g_thr_dir;
	if (g_thr <= THR_LO) g_thr_dir = 1;
	if (g_thr >= THR_HI) { g_thr_dir = -1; caption(); }
	g_texbase = (g_texbase + 0x101) & 0xffff;   /* img 0x0e3e */
	if (g_fade != 0) {                  /* img 0x0db4 - the fade in */
		g_fade += 0x40;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	}
	/* img 0x0e70 IS MODE 0'S TAIL and nothing else reaches it: the
	 * handler ENDS in a jump to Cam_FollowTerrain, so the opening's
	 * horizon follows the land the way the car's does. Hanging it on
	 * mode 2 instead drags g_CamScale down every tick, and since
	 * Mesh_SetupTransform's y is -((alt - scale) << 5) that inverts
	 * the craft's height. The camera's own fields are the sources,
	 * so they go in and come back out around it. */
	g_cam_wx_src = g_cam.wx;
	g_cam_wz_src = g_cam.wy;
	g_cam_h = g_cam.scale & 0xffff;
	cam_follow();
	g_cam.scale = (s32)(s16)g_cam_h;
	g_cam.yofs = g_cam_yofs;
	g_cam.sky_bias = g_sky_bias;
	g_cam.water_scroll = g_water_scroll;
}

/* ORIGIN img 0x0e73 - mode 1's, which is four instructions and shares
 * nothing with mode 0's. Running both in one scene steps the warp
 * through the opening and the captions through the countdown. */
static void anim_tick1(void)
{
	g_warp_x = (g_warp_x - 1) & 0xffff;
	g_warp_y = (g_warp_y + 0x101) & 0xffff;
	if (g_burst > 0) g_burst--;         /* TICKS, not frames */
}

/* ORIGIN img 0x2373 and 0x237f - the craft, between the landscape
 * and the captions. THE DRAW IS GATED ON ITS OWN Z: img 0x2376
 * skips it once the object is further off than 0x28000. */
static void ship(u8 *fb)
{
	XcurMeshCam mc;
	s32 z;

	mc.pitch = g_obj.pitch;             /* img 0x9909 */
	mc.yaw = g_obj.yaw;                 /* img 0x9881 */
	mc.roll = g_obj.roll;               /* img 0x990b */
	mc.cam_yaw = g_cam.angle;           /* img 0x987d */
	mc.zscale = XCUR_MESH_ZSCALE;
	mc.sky_bias = g_sky_bias;           /* img 0x996d, img 0x49f9 */
	mc.light_pitch = XCUR_LIGHT_PITCH;
	mc.light_yaw = XCUR_LIGHT_YAW;
	mc.light_roll = XCUR_LIGHT_ROLL;
	mc.shade_scale = XCUR_SHADE_SCALE;

	z = Xcur_MeshSetup(&mc, g_cam.wx, g_cam.wy, g_cam.angle,
	                   g_cam.scale, g_tgt_x, g_tgt_y, g_cam.alt);
	g_mc = mc;
	/* img 0x237d IS `jae` - UNSIGNED. A negative z is therefore a
	 * huge unsigned one and the craft is skipped, which is also what
	 * behind-the-camera should do. A signed compare draws it. */
	if ((u32)z < (u32)XCUR_SHIP_FAR)
		Xcur_MeshDraw(fb, &g_ship, &mc, &g_span);
}

/* OURS. The landscape and the craft, which is all the countdown's
 * frozen background is. */
static void backdrop(u8 *dst)
{
	if (g_seeking) return;
	Xcur_LookPush(&g_cam, &g_sky_bias);
	memset(dst, 0, PD_SCREEN_W * PD_SCREEN_H);
	Xcur_VoxelRender(dst, g_texture, g_colour, g_water, &g_cam);
	ship(dst);
	Xcur_LookPop(&g_cam, &g_sky_bias);
}

/* ORIGIN img 0x2370 FireText_Render, the FIRST of Demo_Entry's two
 * calls to it at img 0x0160 - the landscape, the craft and the
 * burning title over them. */
static void scene_opening(void)
{
	/* THE MUSIC FLIES THE CAMERA. img 0x0dc5 takes its speed from two
	 * of the module's channel volumes and nothing else sets it, so
	 * the landscape coasts to a halt as they fade - effects/
	 * camera.txt. */
	s32 order = 0;

	{ s32 t_; for (t_ = 0; t_ < g_ticks; t_++)
	    Xcur_CamStep(&g_cam, &g_phase, Xcur_SeqCamSpeed()); }
	backdrop(g_fb);
	if (!g_seeking)
		Xcur_FireTextFill(g_fb, g_mask, g_texture, g_texbase, g_thr,
		                  g_cap_ofs, g_cap_rows);

	/* The sync values ARE order indices, so the module is the clock
	 * the demo itself uses. The countdown owns order 4 - read live,
	 * effects/camera.txt. */
	Xcur_SeqPos(&order, (s32 *)0);
	if (order >= XCUR_SYNC_COUNTDOWN)
		scene_enter(1);
}

/* ORIGIN img 0x3873 driven as sync 0x07 drives it - the digits over a
 * STILL landscape, the camera not stepping. */
static void scene_countdown(void)
{
	/* NO CAPTION FILL AND NO REDRAW - countdown.txt 3.1. */
	if (!g_seeking)
		Xcur_CountdownStep(g_fb, g_bg, g_part, g_npart, g_texture,
		                   XCUR_FIRE_HEAT, g_warp_x, g_warp_y);
	if (g_burst <= 0 && g_digits < XCUR_COUNTDOWN_WINDOWS - 1) {
		g_digits++;                     /* img 0x999f, 0x60 a digit */
		g_window = g_digits;
		g_npart = Xcur_CountdownSpawn(g_part, XCUR_COUNTDOWN_MAX,
		                              g_window);
		memcpy(g_fb, g_bg, sizeof g_fb);
		g_burst = XCUR_BURST_TICKS;
	}
	{
		s32 order = 0, cur = 0;
		Xcur_SeqPos(&order, &cur);
		if (order >= XCUR_SYNC_APPROACH) scene_enter(2);
	}
}

/* ORIGIN img 0x0e8e - mode 2. The craft flies, the camera chases. */
static void anim_tick2(void)
{
	Xcur_CamFlight(&g_cam, &g_obj, &g_tgt_x, &g_tgt_y);
	/* NOT mode 0's triangle wave: img 0x0f4b just decrements, so the
	 * flame climbs the banner steadily instead of pulsing, and img
	 * 0x0f44 pans the texture the other way. */
	g_thr -= 1;
	g_texbase = (g_texbase - 0x100) & 0xffff;
	/* img 0x0f32 - the pitch must pass -150 before this starts. */
	if ((s32)(s16)g_obj.pitch < -150) {
		g_fade += 0x46;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	}
}

/* ORIGIN img 0x0f77 - mode 3, the launch. camera.txt section 6.
 *
 * THE TRANSFORM IS INTEGRATED, NOT RECOMPUTED - Mesh_SetupTransform
 * is not called here. */
static void anim_tick3(void)
{
	s32 bx;

	g_cam.scale += 0x10;                        /* img 0x0f77 */
	g_mc.tz -= 0xfa;                            /* img 0x0f7d */
	g_mc.ty -= 0xfa;                            /* img 0x0f87 */
	g_obj.roll = (g_obj.roll + 4) & 0xffff;     /* img 0x0f91 */

	/* PLACED, not integrated - a small orbit of the origin. */
	bx = g_cam.angle;                           /* img 0x0f97 */
	g_cam.wx = -(Xcur_Sin(bx) << 4);
	g_cam.wy = -(Xcur_Cos(bx) << 4);
	g_cam.angle = (g_cam.angle - 4) & 0xffff;   /* img 0x0fc5 */
	g_obj.yaw = (g_obj.yaw + 4) & 0xffff;       /* img 0x0fcb */
	g_mc.yaw = g_obj.yaw;
	g_mc.roll = g_obj.roll;
	g_mc.cam_yaw = g_cam.angle;

	if (++g_launch_div > 5) {                   /* img 0x0fd1 */
		g_launch_div = 0;
		g_launch_step++;
	}
	/* ORIGIN img 0x3609, Voxel_LatchCamera. THE SKY BIAS IS THE
	 * GROUND RAMP, latched every frame - the same counter that
	 * closes the ground distance also walks the horizon, so the
	 * craft rises into frame as the ground is revealed. Pinning it
	 * to a constant leaves the craft below the screen for most of
	 * the part and then far too close when it arrives. */
	g_sky_bias = g_launch_step;
	g_mc.sky_bias = g_launch_step;
	/* img 0x0fef is `jl` - SIGNED, and the counter is negative. */
	if ((s32)(s16)g_launch_step >= 0xe) {       /* img 0x0fe9 */
		g_fade -= 0x100;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	} else if (g_fade != 0) {                   /* img 0x1008 */
		g_fade -= 0x46;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	}
}

/* ORIGIN img 0x2370 FireText_Render, the SECOND of Demo_Entry's two
 * calls to it at img 0x0190. No captions from mode 2 on - just
 * landscape and craft. */
static void scene_approach(void)
{
	s32 order = 0;

	{ s32 t_; for (t_ = 0; t_ < g_ticks; t_++) anim_tick2(); }
	backdrop(g_fb);
	if (!g_seeking)
		Xcur_FireTextFill(g_fb, g_mask, g_texture, g_texbase, g_thr,
		                  g_cap_ofs, g_cap_rows);
	Xcur_SeqPos(&order, (s32 *)0);
	if (order >= XCUR_SYNC_LAUNCH) scene_enter(3);
}

/* ORIGIN img 0x2621 Ground_Run - the ground, then the craft. THE
 * SHADE MAP IS THE HEIGHT HERE and the plasma is only the water
 * test, the opposite pair from Voxel_Render. */
static void scene_launch(void)
{
	s32 order = 0;

	{ s32 t_; for (t_ = 0; t_ < g_ticks; t_++) anim_tick3(); }
	if (!g_seeking) {
		XcurMeshCam mc;
		s32 look;

		mc = g_mc;
		look = Xcur_LookYaw();
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);
		Xcur_GroundRender(g_fb, g_colour, g_texture, g_cam.wx,
		                  g_cam.wy, g_cam.angle + look, g_cam.scale,
		                  g_launch_step);
		mc.cam_yaw += look;
		Xcur_MeshDraw(g_fb, &g_ship, &mc, &g_span);
	}
	Xcur_SeqPos(&order, (s32 *)0);
	if (order >= XCUR_SYNC_PLANET) scene_enter(4);
}

/* ORIGIN img 0x101e - mode 4. The surface turns a texel a tick and
 * the ball drifts BECAUSE THE CRAFT IS FLYING: img 0x105d adds twice
 * the rotated x of the craft's heading into the approach, so the two
 * are one motion seen two ways. */
static void anim_tick4(void)
{
	s32 x = 0, y = 0, z = -0x10;

	g_pan = (g_pan + 1) & 0xffff;               /* img 0x101e */
	g_obj.yaw = (g_obj.yaw + 2) & 0xffff;       /* img 0x1023 */
	g_obj.roll = (g_obj.roll - 2) & 0xffff;     /* img 0x1029 */
	Xcur_VecRotate3(&x, &y, &z, 0, g_obj.yaw, 0);
	g_mc.tz += z;                               /* img 0x1054 */
	x *= 2;                                     /* img 0x105a */
	g_approach += x;                            /* img 0x105d */
	/* THE STARFIELD HAS ITS OWN CAMERA and mode 4 flies it, which is
	 * the only thing that moves the stars - img 0x1072. */
	g_star_x = (s32)(s16)(g_star_x + (x >> 2));
	g_star_z = (s32)(s16)(g_star_z + ((z + 0x200) >> 2));
	g_mc.yaw = g_obj.yaw;
	g_mc.roll = g_obj.roll;
	if (g_fade != 0) {                          /* img 0x107c */
		g_fade += 0x100;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	}
}

/* ORIGIN img 0x2925 - Planet_Render. Stars, then the ball, then the
 * craft in front of it. */
static void scene_planet(void)
{
	{ s32 t_; for (t_ = 0; t_ < g_ticks; t_++) anim_tick4(); }
	if (!g_seeking) {
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);
		Xcur_PointsRender(g_fb, g_stars, XCUR_STARS,
		                  g_star_x, g_star_y, g_star_z);
		Xcur_PlanetRender(g_fb, g_sphere, g_texture, g_pan, g_approach);
		Xcur_MeshDraw(g_fb, &g_ship, &g_mc, &g_span);
	}
	{
		/* img 0x29ad AND img 0x29ba - the order and the row counter
		 * both. Ending on the order alone hands the roto part a row
		 * counter of 0, and mode 5 then rotates for its full 24 rows
		 * instead of the three the demo gives it. */
		s32 order = 0;
		Xcur_SeqPos(&order, (s32 *)0);
		if (order >= XCUR_SYNC_ROTO
		    && Xcur_SeqRowCount() >= g_tick_target)
		    scene_enter(5);
	}
}

/* ORIGIN img 0x1093 - mode 5. THE CUT IS THE MUSIC: it zooms and
 * turns for the first 0x18 rows of each pattern and fades to black
 * for the rest, and the row counter resets on every pattern - so the
 * word flashes up once a pattern with no timer of its own. */
static void anim_tick5(void)
{
	if (Xcur_SeqRowCount() <= XCUR_ROTO_ROWS) {
		g_roto_zoom -= 0x34;                    /* img 0x109b */
		g_roto_angle = (g_roto_angle + 0x18) & 0xffff;
	} else {
		g_fade -= 0x100;                        /* img 0x10ab */
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	}
}

/* ORIGIN img 0x25b5 Roto_Run - the rotozoomer and nothing else. */
static void scene_roto(void)
{
	s32 order = 0, k;

	Xcur_SeqPos(&order, (s32 *)0);
	/* THE SECOND PASS ENDS AT ITS OWN SYNC AND THE PART AFTER IT IS
	 * NOT PORTED - img 0x03ee, mode 6, Warp_RenderFull. Running on
	 * instead lets the pattern roll over, which resets the row
	 * counter and starts mode 5's full 24-row turn again from
	 * whatever zoom it had reached: a long smear that belongs to no
	 * part. Holding the last frame is the honest end of what is
	 * reconstructed. */
	if (g_scene == 7 && order >= XCUR_SYNC_ROTO2) {
		scene_enter(8);
		return;
	}

	/* ONE RENDER PER ANIMATION PASS - effects/roto.txt section 2. */
	for (k = 0; k < g_ticks; k++) {
		anim_tick5();
		if (g_seeking) continue;
		Xcur_RotoRender(g_fb, g_fb, g_texture, g_roto_zoom,
		                g_roto_angle, g_roto_phase, g_roto_blend);
		g_roto_phase++;                         /* img 0x3001 */
	}
	/* IT ENDS ON ITS OWN SYNC. Left running past it the zoom keeps
	 * falling, the field tiles finer every tick and the letters go
	 * with it - the scene is only legible inside its own pattern. */
	if (g_scene == 5 && order >= XCUR_SYNC_STARS) scene_enter(6);
}

/* ORIGIN img 0x0342 - a cut of the craft against stars, ball
 * suppressed. img 0x29ba ends it on the ROW COUNTER rather than the
 * order, so it is a fixed burst between the two roto passes. */
static void scene_stars(void)
{
	{ s32 t_; for (t_ = 0; t_ < g_ticks; t_++) anim_tick4(); }
	if (!g_seeking) {
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);
		Xcur_PointsRender(g_fb, g_stars, XCUR_STARS,
		                  g_star_x, g_star_y, g_star_z);
		Xcur_MeshDraw(g_fb, &g_ship, &g_mc, &g_span);
	}
	if (Xcur_SeqRowCount() >= g_tick_target) scene_enter(7);
}

/* ORIGIN img 0x10b8 - mode 6. Six offsets, each integrating its own
 * sine at its own rate, and the palette cycling under them.
 *
 * THE WHITE HITS ARE A NOTE. img 0x1170 reads the flag the sequencer
 * raises on instrument 0x10 and, when set, throws the fade level
 * straight to the target plus 0x2000 and halves four of the offsets -
 * so the picture snaps brighter and tighter on the beat and then
 * eases back at 0x100 a tick. */
static void anim_tick6(void)
{
	s32 k, target;

	for (k = 0; k < XCUR_WARP_OFS; k++)         /* img 0x10b8 */
		g_warp_ofs[k] = (s32)(s16)
		    ((g_warp_ofs[k] + (Xcur_Sin(g_warp_ph[k]) >> 3)) & 0xffff);
	for (k = 0; k < XCUR_WARP_OFS; k++)         /* img 0x1148, 2..7 */
		g_warp_ph[k] = (g_warp_ph[k] + 2 + k) & 0xffff;

	target = g_fade_target;
	if (Xcur_SeqTakeFlash()) {                  /* img 0x1170 */
		g_warp_ofs[0] = (s32)(s16)(((u32)g_warp_ofs[0] & 0xffff) >> 1);
		g_warp_ofs[1] = (s32)(s16)(((u32)g_warp_ofs[1] & 0xffff) >> 1);
		g_warp_ofs[3] = (s32)(s16)(((u32)g_warp_ofs[3] & 0xffff) >> 1);
		g_warp_ofs[4] = (s32)(s16)(((u32)g_warp_ofs[4] & 0xffff) >> 1);
		g_fade = target + XCUR_WARP_FLASH;      /* img 0x1192 */
	}
	Xcur_PalCycle(g_pal_master);                /* img 0x1199 */
	Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);

	/* img 0x11c3 - ease the level onto the target, never past it */
	if (g_fade == g_fade_target) return;
	if (g_fade > g_fade_target) g_fade -= XCUR_WARP_STEP;
	else                        g_fade += XCUR_WARP_STEP;
}

/* ORIGIN img 0x2639 Warp_RunFull - the warp and nothing else. */
static void scene_warp(void)
{
	s32 order = 0;

	Xcur_SeqPos(&order, (s32 *)0);
	if (order >= XCUR_SYNC_WARP) { scene_enter(9); return; }
	{ s32 t_; for (t_ = 0; t_ < g_ticks; t_++) anim_tick6(); }
	if (!g_seeking)
		Xcur_WarpRenderFull(g_fb, g_texture, g_warp_ofs);
}

/* ORIGIN img 0x11dd - mode 7, which only fades. THE LAST ORDER GOES
 * THE OTHER WAY: the climb stops dead at zero until order 0x13, which
 * carries it past into a positive bias, and that is Pal_Fade
 * brightening - effects/moire.txt section 1.1. */
static void anim_tick7(void)
{
	s32 order = 0;

	Xcur_SeqPos(&order, (s32 *)0);
	if (order == 0x13) {                        /* img 0x11dd */
		g_fade += 0x60;                         /* img 0x11fc */
	} else {
		if (g_fade == 0) return;                /* img 0x11e5 */
		g_fade += 0x100;                        /* img 0x11ef */
	}
	Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
}

/* ORIGIN img 0x2818 Moire_Render, which is its own runner. */
static void scene_moire(void)
{
	s32 order = 0;

	Xcur_SeqPos(&order, (s32 *)0);
	if (order >= XCUR_SYNC_MOIRE) { scene_enter(10); return; }
	{ s32 t_; for (t_ = 0; t_ < g_ticks; t_++) anim_tick7(); }
	if (!g_seeking)
		g_moire_phase = Xcur_MoireRender(g_fb, g_texture, g_moire_phase);
}

/* ORIGIN img 0x1ce8 - Battle_Pursue. Craft B chases A, and the star
 * camera drifts along A's heading.
 *
 * THE X AND Y TARGET IS THE NEGATED FORWARD VECTOR ALONE, with no
 * part of A's position in it; only z adds A's z, img 0x1d1f. One
 * sixteenth of the gap a tick - it never arrives and never
 * overshoots, and needs no velocity and no clamp.
 *
 * It hands the forward vector back because modes 8 and 0xb both
 * consume it rather than recomputing. */
static void battle_pursue(s32 *ofx, s32 *ofy, s32 *ofz)
{
	s32 x = 0, y = 0, z = -0x400;

	g_rot_pitch = g_a_pitch;                    /* img 0x1ce8 */
	g_rot_yaw = g_a_yaw;
	g_rot_roll = g_a_roll;
	Xcur_VecRotate3(&x, &y, &z, g_rot_pitch, g_rot_yaw, g_rot_roll);
	g_b_x += ((-x) - g_b_x) >> 4;               /* img 0x1d37 */
	g_b_y += ((-y) - g_b_y) >> 4;
	g_b_z += ((g_a_z - z) - g_b_z) >> 4;
	/* img 0x1d65 - WORD adds, so the star camera wraps */
	g_star_x = (s32)(s16)(g_star_x + (x >> 6));
	g_star_y = (s32)(s16)(g_star_y + (y >> 6));
	g_star_z = (s32)(s16)(g_star_z + (z >> 6));
	*ofx = x >> 6;
	*ofy = y >> 6;
	*ofz = z >> 6;
}

/* ORIGIN img 0x1dce and img 0x1e4b - one shot into a round robin of
 * eight, at the firing craft's own position and attitude. */
static void shot_spawn(s32 x, s32 y, s32 z, s32 p, s32 yw, s32 r,
                       s32 flash)
{
	s32 i = g_shot_next & 7;

	g_shot_next++;                              /* img 0x1dde */
	g_shot[i].x = x; g_shot[i].y = y; g_shot[i].z = z;
	g_shot[i].pitch = p; g_shot[i].yaw = yw; g_shot[i].roll = r;
	g_fade += flash;
}

/* ORIGIN img 0x1e9b and the tail at img 0x1f94 that follows it.
 *
 * THE FADE DECAY IS WHAT MAKES THE SCREEN PULSE. Every shot and every
 * hit throws the level UP - 0x600 or 0x300 - and this bleeds it back
 * down 0x60 a tick, so each shot is a bright flash that falls off
 * over about ten ticks. Without it the level only ever climbs, the
 * screen sits pale grey and the glow box reads as a square left on
 * screen rather than a muzzle flash.
 *
 * img 0x1f9a is JLE on a WORD, so it stops at zero from either side -
 * mode 8's own copy at img 0x124e is JZ and stops only on exactly
 * zero. */
static void shots_step(void)
{
	s32 hits = Xcur_ShotsStep(g_shot, g_anim_mode, g_b_x, g_b_y, g_b_z);

	while (hits-- > 0) {
		/* img 0x1f77 PUSHES CX AND SI, NOT THE RANDOM STATE, so this
		 * burst ADVANCES THE SHARED STREAM where the tail's parked one
		 * does not - effects/script1_landscape.txt section 6.1. */
		Xcur_GlowBurst(g_spark, g_spark_life, g_spark_shift);
		g_fade += 0x600;                        /* img 0x1f7e */
	}
	if ((s32)(s16)g_fade <= 0) return;          /* img 0x1f94 */
	g_fade -= 0x60;                             /* img 0x1f9e */
	Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
}

/* ORIGIN img 0x1d75 - the tail modes 9 and 0xa share, and where the
 * GUN lives: craft B fires on instrument 9.
 *
 * On row 0x18 of every pattern from order 0x1a the glow bursts and
 * the screen flashes. */
static void battle_tail_ab(void)
{
	s32 order = 0;

	Xcur_SeqPos(&order, (s32 *)0);
	if (order >= 0x1a) {                        /* img 0x1d75 */
		Xcur_GlowSpawnStep(g_glowmem, g_glow_src, g_spark);
		if (Xcur_SeqRowCount() == 0x18) {       /* img 0x1d80 */
			g_fade = 0x2400;
			Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
			Xcur_RndSave();
			Xcur_GlowBurst(g_spark, g_spark_life, g_spark_shift);
			Xcur_RndRestore();
		}
	}
	if (Xcur_SeqTakeFire())                     /* img 0x1dbe */
		shot_spawn(g_b_x, g_b_y, g_b_z,
		           g_b_pitch, g_b_yaw, g_b_roll, 0x600);
	shots_step();
}

/* ORIGIN img 0x1e22 - the tail mode 0xb uses. CRAFT A FIRES HERE, not
 * B, and its roll is forced to zero at img 0x1e70.
 *
 * THE TRIGGER CHANGES AT ORDER 0x1c, img 0x1e25. Below it the gun is
 * instrument 9; at or above it it is the flag Music_NextPattern
 * raises, so the shooting thins from one a note to one a pattern. */
static void battle_tail_b(void)
{
	s32 order = 0, fire;

	Xcur_GlowSpawnStep(g_glowmem, g_glow_src, g_spark);
	Xcur_SeqPos(&order, (s32 *)0);
	fire = (order >= 0x1c) ? Xcur_SeqTakeNewPattern()
	                       : Xcur_SeqTakeFire();
	if (fire) {
		/* img 0x1e3f clears BOTH, so a note left pending across the
		 * switchover cannot fire a second time. */
		(void)Xcur_SeqTakeFire();
		(void)Xcur_SeqTakeNewPattern();
		shot_spawn(g_a_x, g_a_y, g_a_z, g_a_pitch, g_a_yaw, 0, 0x300);
	}
	shots_step();
}

/* ORIGIN img 0x1208 - mode 8. */
static void anim_tick8(void)
{
	s32 fx, fy, fz;

	g_a_pitch = g_obj.pitch;                    /* img 0x1208 */
	g_a_yaw = g_obj.yaw;
	battle_pursue(&fx, &fy, &fz);
	g_obj.roll = (g_obj.roll + 8) & 0xffff;
	g_obj.yaw = (g_obj.yaw + 2) & 0xffff;
	/* img 0x1227 TRUNCATES x AND y TO 16 BITS before halving them,
	 * while img 0x1248 adds EDX for z - neither truncated nor halved.
	 * The ECX the instruction before prepares is never used. */
	g_mc.tx += (s32)(s16)fx >> 1;
	g_mc.ty += (s32)(s16)fy >> 1;
	g_mc.tz += fz;
	if (g_fade != 0) {                          /* img 0x124e */
		g_fade -= 0x60;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	}
}

/* ORIGIN img 0x1264 - mode 9, the approach. A flies in and CLAMPS,
 * and B copies A's pitch and yaw outright while rolling the other
 * way - so the two bank against each other with no attitude model of
 * B's own. */
static void anim_tick9(void)
{
	s32 fx, fy, fz;

	battle_pursue(&fx, &fy, &fz);
	if ((u32)g_a_z < 0x1e000UL) g_a_z += 0x60;  /* img 0x1267, jnc */
	g_a_pitch = (g_a_pitch + 3) & 0xffff;
	g_a_yaw = (g_a_yaw + 2) & 0xffff;
	g_a_roll = (g_a_roll + 4) & 0xffff;
	g_b_pitch = g_a_pitch;                      /* img 0x128c */
	g_b_yaw = g_a_yaw;
	g_b_roll = (g_b_roll - 2) & 0xffff;
	battle_tail_ab();
}

/* ORIGIN img 0x12a5 - mode 0xa, the pass. B flies a LISSAJOUS and
 * banks by reading its own position: yaw and pitch are the position
 * shifted right 7, which leads the curve correctly for free because
 * the derivative of a sine is a sine a quarter turn on.
 *
 * IT DOES NOT CALL Battle_Pursue - the only one of the four that does
 * not - so the rotate's roll latch still holds whatever the previous
 * tick's shot loop or Battle_Pursue left in it. */
static void anim_tick10(void)
{
	s32 x = 0, y = 0, z = -8;

	/* img 0x12ae - the ALIGNED dword sine, doubled */
	g_b_x = Xcur_Sin(g_plasma_c0) * 2;
	g_b_yaw = ((g_b_x >> 7) + 0x400) & 0xffff;
	g_b_y = Xcur_Sin(g_plasma_c1) * 2;
	g_b_pitch = (g_b_y >> 7) & 0xffff;
	g_plasma_c0 = (g_plasma_c0 + 4) & 0xffff;   /* img 0x12e6 */
	g_plasma_c1 = (g_plasma_c1 + 8) & 0xffff;
	g_a_roll = (g_a_roll + 3) & 0xffff;
	g_b_roll = (g_b_roll - 2) & 0xffff;
	g_star_z = (s32)(s16)(g_star_z + 0x400);    /* img 0x12fe */
	if ((s32)(s16)g_a_roll < 0x708) {           /* img 0x1305 */
		battle_tail_ab();
		return;
	}
	g_a_pitch = (g_a_pitch - 8) & 0xffff;
	g_rot_pitch = g_a_pitch;                    /* img 0x131a */
	g_rot_yaw = g_a_yaw;
	Xcur_VecRotate3(&x, &y, &z, g_rot_pitch, g_rot_yaw, g_rot_roll);
	g_a_x += x;
	g_a_y += y;
	g_a_z += z - 0x200;                         /* img 0x1335 */
	battle_tail_ab();
}

/* ORIGIN img 0x1351 - mode 0xb, the locked formation. B sits exactly
 * one forward vector AHEAD of A, which makes B the farther of the two
 * and is the reason Battle_Run flips its draw order here and nowhere
 * else. */
static void anim_tick11(void)
{
	s32 fx, fy, fz, ax;

	battle_pursue(&fx, &fy, &fz);
	/* img 0x1354 shifts BACK left 6, which does not recover the six
	 * bits Battle_Pursue's own shift dropped. */
	fx <<= 6; fy <<= 6; fz <<= 6;
	g_a_x = -(fx >> 2);                         /* img 0x1366 */
	g_a_y = -(fy >> 2);
	g_a_z = -(fz >> 2) + 0x8800;
	g_b_x = fx + g_a_x;                         /* img 0x1399 */
	g_b_y = fy + g_a_y;
	g_b_z = fz + g_a_z;
	g_a_roll = (g_a_roll - 4) & 0xffff;
	g_b_roll = (g_b_roll + 2) & 0xffff;
	ax = (0xff60 - g_a_pitch) & 0xffff;         /* img 0x13c8 */
	g_a_pitch = (g_a_pitch + ((s32)(s16)ax >> 6)) & 0xffff;
	battle_tail_b();
}

/* ORIGIN img 0x26a0 and img 0x26e8. Each craft copies its own state
 * into the shared object slot and draws.
 *
 * CRAFT B CARRIES THE ONLY TRUE CULL IN THE OBJECT PIPELINE: img
 * 0x2701 returns without drawing on a negative z, and it does so
 * AFTER x and y are already written. Everywhere else the transform
 * merely clamps to the near plane, so a craft behind the camera would
 * smear rather than vanish. */
static void draw_craft_a(void)
{
	g_mc.tx = g_a_x; g_mc.ty = g_a_y; g_mc.tz = g_a_z;
	g_mc.pitch = g_a_pitch; g_mc.yaw = g_a_yaw; g_mc.roll = g_a_roll;
	Xcur_LookPushMesh(&g_mc);
	Xcur_MeshDraw(g_fb, &g_ship, &g_mc, &g_span);
	Xcur_LookPopMesh(&g_mc);
}

/* ORIGIN img 0x26e8 Battle_DrawCraftB. */
static void draw_craft_b(void)
{
	g_mc.tx = g_b_x; g_mc.ty = g_b_y;
	if (g_b_z < 0) return;                      /* img 0x2701 */
	g_mc.tz = g_b_z;
	g_mc.pitch = g_b_pitch; g_mc.yaw = g_b_yaw; g_mc.roll = g_b_roll;
	Xcur_LookPushMesh(&g_mc);
	Xcur_MeshDraw(g_fb, &g_craft_b, &g_mc, &g_span);
	Xcur_LookPopMesh(&g_mc);
}

/* ORIGIN img 0x27b4. Shots_Draw owns tx/ty/tz inside its own loop,
 * so the swing reaches the shots through a copy of their positions
 * rather than through the mesh camera - docs/playground.txt 4.2. */
static void draw_shots(void)
{
	XcurShot sh[XCUR_SHOTS];
	s32 order = 0, i;

	memcpy(sh, g_shot, sizeof sh);
	for (i = 0; i < XCUR_SHOTS; i++)
		if (sh[i].x) Xcur_LookOrbit(&sh[i].x, &sh[i].y, &sh[i].z);
	Xcur_SeqPos(&order, (s32 *)0);
	Xcur_LookPushMesh(&g_mc);
	Xcur_ShotsDraw(g_fb, sh,
	               order >= 0x1c ? &g_winged : &g_bolt,
	               &g_mc, &g_span);
	Xcur_LookPopMesh(&g_mc);
}

/* ORIGIN img 0x2925 Planet_Render with the ball suppressed - one
 * craft against the stars while mode 8 flies the pursuit. */
static void scene_chase(void)
{
	s32 order = 0;

	{ s32 t_; for (t_ = 0; t_ < g_ticks; t_++) anim_tick8(); }
	if (!g_seeking) {
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);
		Xcur_PointsRender(g_fb, g_stars, XCUR_STARS,
		                  g_star_x, g_star_y, g_star_z);
		Xcur_PlanetRender(g_fb, g_sphere, g_texture, g_pan, g_approach);
		/* img 0x9909/0x9881/0x990b ARE the object's draw angles - the
		 * split between g_obj and g_mc is ours, so they are copied. */
		g_mc.pitch = g_obj.pitch;
		g_mc.yaw = g_obj.yaw;
		g_mc.roll = g_obj.roll;
		Xcur_LookPivot(g_mc.tx, g_mc.ty, g_mc.tz);
		Xcur_LookPushMesh(&g_mc);
		Xcur_MeshDraw(g_fb, g_obj_model, &g_mc, &g_span);
		Xcur_LookPopMesh(&g_mc);
	}
	Xcur_SeqPos(&order, (s32 *)0);
	/* img 0x29ba is a row condition too, but both of these parts set
	 * img 0x9917 to 0 at img 0x04c2, so it is vacuous here and the
	 * order alone ends them. */
	if (order >= (g_scene == 10 ? XCUR_SYNC_CHASE1 : XCUR_SYNC_CHASE2))
		scene_enter(g_scene + 1);
}

/* ORIGIN img 0x264b - Battle_Run. Stars, then the two craft with the
 * glow and the shots between them.
 *
 * THE DRAW ORDER FLIPS ON MODE 0xb AND THE GLOW'S BOX MOVES WITH IT,
 * img 0x265e - a painter's decision the flight model forces, not a
 * style choice.
 *
 * IT DOES NOT WAIT FOR A TICK: img 0x2691 is Input_Poll, not
 * Demo_WaitTick, so this part renders as fast as the machine allows
 * while the flight models stay on the timer. */
static void scene_battle(void)
{
	s32 order = 0;

	if (g_anim_mode == 0x09)      { anim_tick9();  anim_tick9();  }
	{
		s32 t_;
		for (t_ = 0; t_ < g_ticks; t_++) {
			if (g_anim_mode == 0x0a) anim_tick10();
			else                     anim_tick11();
		}
	}

	Xcur_SeqPos(&order, (s32 *)0);
	if (!g_seeking) {
		if (g_b_z >= 0)
			Xcur_LookPivot((g_a_x + g_b_x) >> 1, (g_a_y + g_b_y) >> 1,
			               (g_a_z + g_b_z) >> 1);
		else
			Xcur_LookPivot(g_a_x, g_a_y, g_a_z);
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);
		Xcur_PointsRender(g_fb, g_stars, XCUR_STARS,
		                  g_star_x, g_star_y, g_star_z);
		if (g_anim_mode == 0x0b) {
			draw_craft_b();
			g_glow_src = Xcur_GlowRender(g_fb, g_glowmem, g_glow_src,
			                             g_glow_dst, XCUR_GLOW_BOX_B);
			draw_shots();
			draw_craft_a();
		} else {
			draw_craft_a();
			if (order >= 0x1a)                  /* img 0x2669 */
				g_glow_src = Xcur_GlowRender(g_fb, g_glowmem,
				                             g_glow_src, g_glow_dst,
				                             XCUR_GLOW_BOX_A);
			draw_shots();
			draw_craft_b();
		}
	}
	/* Each part ends on ITS OWN target: img 0x053b, 0x0647, 0x06eb. */
	if (g_scene == 12 && order >= XCUR_SYNC_FIGHT1) scene_enter(13);
	else if (g_scene == 13 && order >= XCUR_SYNC_FIGHT2) scene_enter(14);
	else if (g_scene == 14 && order >= XCUR_SYNC_FIGHT3) scene_enter(15);
}

/* ORIGIN img 0x13db - mode 0xc, the aftermath. It TRICKLES particles
 * where Glow_Burst throws all 32 at once: one a tick, into the slot
 * the count's low five bits pick, so each slot is reused eight times
 * while the count runs to 0x100.
 *
 * THE LIFETIME SHORTENS AS THE COUNT RISES - 0x20 less a eighth of
 * the count - so the explosion thins out instead of stopping dead.
 * The count has no writer but this increment, so it runs 0 to 0x100
 * once and the trickle then stops for good.
 *
 * Its camera walk is mode 8's, landmine and all, but the fade falls
 * by 0x30 rather than 0x60 and it does not touch the yaw. */
static void anim_tick12(void)
{
	s32 fx, fy, fz;

	if (g_spawn_count < 0x100) {                /* img 0x13e4, jnc */
		s32 i = g_spawn_count & 0x1f;           /* the PRE-increment */

		g_spawn_count++;
		/* img 0x13fb reloads the INCREMENTED count for the life */
		g_spark[i].life = (0x20 - (g_spawn_count >> 3)) & 0xff;
		g_spark[i].x = 0x4000;
		g_spark[i].y = 0x4000;
		g_spark[i].dx = (s32)(s16)Xcur_RndNext() >> 6;
		g_spark[i].dy = (s32)(s16)Xcur_RndNext() >> 6;
	}
	Xcur_GlowSpawnStep(g_glowmem, g_glow_src, g_spark);     /* img 0x1425 */
	g_a_pitch = g_obj.pitch;                    /* img 0x1428 */
	g_a_yaw = g_obj.yaw;
	battle_pursue(&fx, &fy, &fz);
	g_obj.roll = (g_obj.roll + 8) & 0xffff;     /* img 0x143b */
	g_mc.tx += (s32)(s16)fx >> 1;               /* img 0x1441, as mode 8 */
	g_mc.ty += (s32)(s16)fy >> 1;
	g_mc.tz += fz >> 1;                         /* img 0x1453 SARs EDX */
	if (g_fade != 0) {                          /* img 0x1468 */
		g_fade -= 0x30;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	}
}

/* ORIGIN img 0x147e - mode 0xd, the warp out.
 *
 * THE STRETCH IS g_MeshZScale AND NOTHING ELSE. Mesh_Draw scales every
 * vertex's z by it before the rotate, so a growing scale draws the
 * craft longer and longer along its own axis - the warp streak is the
 * model itself, not a trail. 0x100 is the neutral value, matching the
 * << 3 that x and y get.
 *
 * THE SAME SCALE IS THE SPEED. img 0x1497 rotates (0, 0, -zscale<<4),
 * so the star camera and the craft both move faster as it grows, and
 * the stars streak for the same reason the ship stretches.
 *
 * It grows by 4 plus a sixteenth of itself a tick once the row
 * counter passes 0x20 - compounding, so the acceleration is
 * exponential - and decays by a thirty-second before that.
 *
 * THE WARP-OUT IS A PARKED SENTINEL, not a position. Past z 0x32000
 * img 0x1573 puts x and z at 0xfffd8000, flashes the fade to 0x4000
 * and bursts the glow with a longer life and a slower shift. img
 * 0x14ed tests that same value and skips the whole motion block, so
 * the craft stays gone and the burst is what is left on screen. */
static void anim_tick13(void)
{
	s32 x = 0, y = 0, z = -(g_zscale << 4), row = 0, order = 0;

	Xcur_GlowSpawnStep(g_glowmem, g_glow_src, g_spark);
	g_rot_pitch = g_obj.pitch;                  /* img 0x1481 */
	g_rot_yaw = g_obj.yaw;
	Xcur_VecRotate3(&x, &y, &z, g_rot_pitch, g_rot_yaw, g_rot_roll);
	x >>= 6; y >>= 6; z >>= 6;                  /* img 0x14a7 */
	g_star_x = (s32)(s16)(g_star_x + x);        /* img 0x14b3, words */
	g_star_y = (s32)(s16)(g_star_y + y);
	g_star_z = (s32)(s16)(g_star_z + z);

	Xcur_SeqPos(&order, &row);
	if (order >= XCUR_SYNC_AFTER + 1 && row >= 0x10) {  /* img 0x14c2 */
		g_fade -= 0x60;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	}
	if ((s32)(s16)g_fade > 0) {                 /* img 0x14db, jle */
		g_fade -= 0x80;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	}
	if (g_mc.tz == (s32)0xfffd8000L) return;    /* img 0x14ed */

	g_mc.tx += x >> 3;                          /* img 0x1507 */
	g_mc.ty += y >> 3;
	g_mc.tz += z >> 3;
	if ((u32)Xcur_SeqRowCount() > 0x20) {       /* img 0x1519, jbe */
		g_zscale += 4;                          /* img 0x1521 */
		g_zscale += g_zscale >> 4;
	} else {
		g_zscale -= g_zscale >> 5;              /* img 0x153e */
	}
	if (g_mc.tz <= 0x20000L) return;            /* img 0x1548 */
	g_zscale -= g_zscale >> 3;                  /* img 0x155b */
	if (g_mc.tz <= 0x32000L) return;            /* img 0x1565 */
	g_mc.tz = (s32)0xfffd8000L;                 /* img 0x1573 */
	g_mc.tx = (s32)0xfffd8000L;
	g_spark_life = XCUR_WARP_LIFE;              /* img 0x1587 */
	g_spark_shift = XCUR_WARP_SHIFT;
	g_fade = 0x4000;                            /* img 0x1593 */
	Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	Xcur_GlowBurst(g_spark, g_spark_life, g_spark_shift);
}

/* ORIGIN img 0x2efa - Battle_RunNoShots. Battle_Run with no shots and
 * one craft, and the glow drawn UNCONDITIONALLY rather than waiting
 * on the order. */
static void scene_aftermath(void)
{
	s32 order = 0;

	{
		s32 t_;
		for (t_ = 0; t_ < g_ticks; t_++) {
			if (g_anim_mode == 0x0c) anim_tick12();
			else                     anim_tick13();
		}
	}
	Xcur_SeqPos(&order, (s32 *)0);
	if (!g_seeking) {
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);
		Xcur_PointsRender(g_fb, g_stars, XCUR_STARS,
		                  g_star_x, g_star_y, g_star_z);
		g_glow_src = Xcur_GlowRender(g_fb, g_glowmem, g_glow_src,
		                             g_glow_dst, XCUR_GLOW_BOX_A);
		g_mc.pitch = g_obj.pitch;
		g_mc.yaw = g_obj.yaw;
		g_mc.roll = g_obj.roll;
		g_mc.zscale = g_zscale;                 /* img 0x9374 */
		Xcur_MeshDraw(g_fb, g_obj_model, &g_mc, &g_span);
	}
	if (g_scene == 15 && order >= XCUR_SYNC_AFTER) scene_enter(16);
	else if (g_scene == 16 && order >= XCUR_SYNC_WARPOUT) scene_enter(17);
}

/* ORIGIN img 0x15a0 - mode 0xe. Mode 5 at a fortieth of the speed -
 * effects/roto.txt section 4.2. */
static void anim_tick14(void)
{
	s32 order = 0;

	g_roto_zoom -= 2;                           /* img 0x15a0 */
	g_roto_angle = (g_roto_angle + 2) & 0xffff;
	Xcur_SeqPos(&order, (s32 *)0);
	if (order < XCUR_SYNC_ROTOSLOW) return;     /* img 0x15ad, jc */
	g_fade -= 0x100;
	Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
}

/* ORIGIN img 0x25b5 Roto_Run again - same renderer, same
 * one-render-per-pass rate, mode 0xe and a plasma for a texture. */
static void scene_rotoslow(void)
{
	s32 k;

	for (k = 0; k < g_ticks; k++) {
		anim_tick14();
		if (g_seeking) continue;
		Xcur_RotoRender(g_fb, g_fb, g_texture, g_roto_zoom,
		                g_roto_angle, g_roto_phase, g_roto_blend);
		g_roto_phase++;                         /* img 0x3001 */
	}
	{
		s32 order = 0;

		Xcur_SeqPos(&order, (s32 *)0);
		if (order >= XCUR_SYNC_ROTOSLOW) {
			/* img 0x0816 - the tunnel's tables, two parts early, with
			 * the screen already cleared. */
			Xcur_TunnelBuildDepth(g_colour, g_depth8);
			scene_enter(18);
		}
	}
}

/* ORIGIN img 0x15c4 - mode 0xf. It REBUILDS THE PALETTE EVERY TICK,
 * which is where the beams' colour drift comes from, and bounces the
 * zoom between 0x40 and 0x100 by flipping the velocity at each bound
 * - a triangle wave, so the stripe spacing breathes from 16 pixels to
 * 64 and back while they turn. */
static void anim_tick15(void)
{
	Xcur_PalBuildRgbRamps(g_pal_master, g_pal_phase);   /* img 0x15c4 */
	Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	g_roto_angle = (g_roto_angle + g_angle_rate) & 0xffff;
	g_roto_zoom += g_zoom_vel;                  /* img 0x15d8 */
	if (g_roto_zoom < 0x40) g_zoom_vel = 4;     /* img 0x15de */
	if (g_roto_zoom > 0x100) g_zoom_vel = -4;   /* img 0x15f1 */
}

/* ORIGIN img 0x260f Beams_Run - one render a tick, as Roto_Run. */
static void scene_beams(void)
{
	s32 order = 0;

	/* ONE RENDER A FRAME, not one an animation pass. Beams_Run waits
	 * a tick per render like Roto_Run, but a pass here is four times
	 * the work and does not keep up - effects/beams.txt section 7. */
	anim_tick15();
	anim_tick15();
	if (!g_seeking)
		Xcur_BeamsRender(g_fb, g_roto_zoom, g_roto_angle);
	Xcur_SeqPos(&order, (s32 *)0);
	if (g_scene == 18 && order >= XCUR_SYNC_BEAMS1) scene_enter(19);
	else if (g_scene == 19 && order >= XCUR_SYNC_BEAMS2) scene_enter(20);
}

/* ORIGIN img 0x160c - mode 0x10, and everything in it decreases. It
 * cycles the palette every tick, walks the craft toward the camera by
 * 0x200 a tick, and winds both tunnel TARGETS down - Tunnel_Run eases
 * the actual scrolls onto them. */
static void anim_tick16(void)
{
	Xcur_PalCycleBands(g_pal_master);           /* img 0x160c */
	Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	g_obj.roll = (g_obj.roll - 8) & 0xffff;     /* img 0x1612 */
	g_mc.tz -= 0x200;                           /* img 0x1618 */
	g_roto_angle = (g_roto_angle - 8) & 0xffff; /* img 0x1622 */
	g_roto_zoom -= 0x100;                       /* img 0x1628 */
	if (g_fade == 0) return;                    /* img 0x1632 */
	g_fade += 0x80;
}

/* ORIGIN img 0x1646 - mode 0x10 BACKWARDS, except the roll keeps its
 * direction - effects/tunnel.txt section 6. */
static void anim_tick17(void)
{
	Xcur_PalCycleBands(g_pal_master);           /* img 0x1646 */
	Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	g_obj.roll = (g_obj.roll - 8) & 0xffff;     /* img 0x164c */
	g_mc.tz += 0x200;                           /* img 0x1652 */
	g_roto_angle = (g_roto_angle + 8) & 0xffff; /* img 0x165c */
	g_roto_zoom += 0x100;                       /* img 0x1662 */
}

/* ORIGIN img 0x1702 - mode 0x14, the moon. It shares mode 0x13's
 * divider at img 0xa887 and turns the surface one texture row every
 * FOURTH tick, backwards.
 *
 * THE YAW AND PITCH STEPS COME OUT OF THE ROW COUNTER, so they grow
 * as the part runs - an angular acceleration, not a rate - and past
 * row 0x3c the fade goes negative and takes the part out. */
static void anim_tick20(void)
{
	s32 x = 0, y = 0, z = -8, rows;

	g_fade_div = (g_fade_div + 1) & 0xff;       /* img 0x1702 */
	if ((g_fade_div & 3) == 0)                  /* img 0x1707 */
		g_pan = (g_pan - 0x100) & 0xffff;
	g_obj.roll = (g_obj.roll - 4) & 0xffff;     /* img 0x1716 */
	rows = (s32)(s16)Xcur_SeqRowCount() >> 5;   /* img 0x1720, sar */
	g_obj.yaw = (g_obj.yaw + rows) & 0xffff;    /* img 0x1723 */
	g_obj.pitch = (g_obj.pitch + rows) & 0xffff;
	g_rot_pitch = g_obj.pitch;                  /* img 0x172d */
	g_rot_yaw = g_obj.yaw;
	g_rot_roll = 0;
	Xcur_VecRotate3(&x, &y, &z, g_rot_pitch, g_rot_yaw, g_rot_roll);
	g_mc.tx += x;                               /* img 0x1753 */
	g_mc.ty += y;
	g_mc.tz += z >> 1;                          /* img 0x175f */
	x >>= 3;                                    /* img 0x1768 */
	y >>= 3;
	g_star_x = (s32)(s16)(g_star_x + x);        /* img 0x1770, WORD */
	g_star_y = (s32)(s16)(g_star_y + y);
	if (Xcur_SeqRowCount() < 0x3c) return;      /* img 0x177a */
	g_fade -= 0x100;                            /* img 0x1784 */
	Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
}

/* ORIGIN img 0x2925 Planet_Render - the same runner scene_chase
 * uses, on the same mode 8. g_PlanetApproach holds the sphere out of
 * its own cutoff here, so only the stars and the craft draw. */
static void scene_space(void)
{
	{ s32 t_; for (t_ = 0; t_ < g_ticks; t_++) anim_tick8(); }
	if (!g_seeking) {
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);   /* img 0x292c */
		Xcur_PointsRender(g_fb, g_stars, XCUR_STARS,
		                  g_star_x, g_star_y, g_star_z);
		Xcur_PlanetRender(g_fb, g_sphere, g_texture, g_pan, g_approach);
		g_mc.pitch = g_obj.pitch;
		g_mc.yaw = g_obj.yaw;
		g_mc.roll = g_obj.roll;
		Xcur_MeshDraw(g_fb, g_obj_model, &g_mc, &g_span);  /* img 0x299b */
	}
	{
		s32 order = 0;
		Xcur_SeqPos(&order, (s32 *)0);
		if (order >= XCUR_SYNC_SPACE) scene_enter(25);
	}
}

/* ORIGIN img 0x2445 - set the object up at the current target,
 * gate on its z and draw. Car_Run does this three times with three
 * models; `ship` is the same thing pinned to the craft. */
static void ship_at(const XcurModel *m)
{
	XcurMeshCam mc;
	s32 z;

	mc = g_mc;
	mc.pitch = g_obj.pitch;
	mc.yaw = g_obj.yaw;
	mc.roll = g_obj.roll;
	mc.cam_yaw = g_cam.angle;
	mc.zscale = XCUR_MESH_ZSCALE;
	mc.sky_bias = g_sky_bias;
	z = Xcur_MeshSetup(&mc, g_cam.wx, g_cam.wy, g_cam.angle,
	                   g_cam.scale, g_tgt_x, g_tgt_y, g_cam.alt);
	g_mc = mc;
	if ((u32)z < (u32)XCUR_SHIP_FAR)
		Xcur_MeshDraw(g_fb, m, &mc, &g_span);
}

/* OURS - the demo reaches the sine table through a masked angle at
 * every one of the car's eight steps. */
static s32 car_sin(s32 a) { return Xcur_Sin(a & 0x7fc); }
static s32 car_cos(s32 a) { return Xcur_Cos(a & 0x7fc); }

/* OURS */
static s32 car_ground(s32 x, s32 z)
{
	return Xcur_TerrainHeightAt(g_texture, g_water, x, z,
	                            g_cam.water_level, g_cam.water_scroll,
	                            g_cam.hscale, (s32 *)0);
}

/* ORIGIN img 0x1c23 - mode 0x1a, the THE END letters.
 *
 * TWO INDEPENDENT ROTATIONS AT DIFFERENT RATES: the letters turn at
 * 8, 6 and 4 and THE LIGHT at 4, 8 and 0xc. This handler and the two
 * after it are the only place in the demo that animates the light -
 * everywhere else the script sets it once and leaves it. That is
 * what sweeps the highlight across the faces while they tumble.
 * effects/theend.txt section 1. */
static void anim_tick26(void)
{
	g_mc.light_pitch = (g_mc.light_pitch + 4) & 0xffff;   /* img 0x1c23 */
	g_mc.light_yaw = (g_mc.light_yaw + 8) & 0xffff;
	g_mc.light_roll = (g_mc.light_roll + 0xc) & 0xffff;
	g_obj.pitch = (g_obj.pitch + 8) & 0xffff;   /* img 0x1c35 */
	g_obj.yaw = (g_obj.yaw + 6) & 0xffff;
	g_obj.roll = (g_obj.roll + 4) & 0xffff;
	g_mc.tz -= 0x100;                           /* img 0x1c47 */
	if (g_fade == 0) return;                    /* img 0x1c51 */
	g_fade += 0x30;
	Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
}

/* ORIGIN img 0x25c7 TheEnd_Run - clear, draw, present, wait a tick.
 * The object's translation is the part's own, so there is no
 * Mesh_SetupTransform here. */
static void scene_theend(void)
{
	{ s32 t_; for (t_ = 0; t_ < g_ticks; t_++) anim_tick26(); }
	if (!g_seeking) {
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);  /* img 0x25ca */
		g_mc.pitch = g_obj.pitch;
		g_mc.yaw = g_obj.yaw;
		g_mc.roll = g_obj.roll;
		Xcur_MeshDraw(g_fb, &g_letters, &g_mc, &g_span);  /* img 0x25cd */
	}
	{
		s32 order = 0;
		Xcur_SeqPos(&order, (s32 *)0);
		if (order >= XCUR_SYNC_THEEND) scene_enter(32);
	}
}

/* ORIGIN img 0x1c67 - mode 0x1b. The light block and a fade down
 * from img 0x5314's white. effects/theend.txt section 2. */
static void anim_tick27(void)
{
	g_mc.light_pitch = (g_mc.light_pitch + 4) & 0xffff;   /* img 0x1c67 */
	g_mc.light_yaw = (g_mc.light_yaw + 8) & 0xffff;
	g_mc.light_roll = (g_mc.light_roll + 0xc) & 0xffff;
	if (g_fade == 0) return;                    /* img 0x1c79 */
	g_fade -= 0x40;                             /* img 0x1c83 */
	Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
}

/* ORIGIN img 0x1c8f - mode 0x1c, THE DROP: one word at one fixed
 * offset with an amplitude that climbs for ever. 0x3f20 is word
 * 8080 of the 160 by 100 grid, its exact centre.
 * IT DRIVES THE BUFFER THE SOLVER READS, img 0x1cb8 taking the
 * source and not the destination - the opposite of the dripping
 * scene's drops. effects/theend.txt section 3. */
static void anim_tick28(void)
{
	s32 order = 0, v;

	g_end_phase = (g_end_phase + 0x20) & 0xffff;   /* img 0x1c8f */
	g_end_amp++;                                   /* img 0x1c95 */
	v = (Xcur_Sin(g_end_phase & 0x7fc) * g_end_amp) >> 0xb;  /* img 0x1ca4 */
	g_wat_src[0x3f20 >> 1] = (s16)v;               /* img 0x1cbd */
	g_mc.light_pitch = (g_mc.light_pitch + 4) & 0xffff;   /* img 0x1cc0 */
	g_mc.light_yaw = (g_mc.light_yaw + 8) & 0xffff;
	g_mc.light_roll = (g_mc.light_roll + 0xc) & 0xffff;
	Xcur_SeqPos(&order, (s32 *)0);
	if (order < 0x50) return;                      /* img 0x1cd2 */
	g_fade -= 0x18;                                /* img 0x1cdc */
	Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
}

/* ORIGIN img 0x5367 - the plasma made into the picture the letters
 * stand on, read at 256 wide and written at 320.
 *
 * 204 ROWS, NOT 200: the overrun is slack, not spill. */
static void end_background(u8 *dst, const u8 *plasma)
{
	s32 si = 0, di = 0, row;                    /* img 0x536c */

	for (row = 0xcc; row > 0; row--) {          /* img 0x5370 */
		s32 col;

		for (col = 0x140; col > 0; col--) {     /* img 0x5373 */
			dst[di] = (u8)(((plasma[si] >> 3) + 0xe0) & 0xff);
			si++;
			di++;
		}
		si -= 0x40;                             /* img 0x5384 */
	}
}

/* ORIGIN img 0x25e2 - TheEnd_Run's second entry, WITHOUT THE CLEAR,
 * so the plasma survives and the letters redraw over themselves. */
static void scene_endplasma(void)
{
	s32 order = 0;

	{ s32 t_; for (t_ = 0; t_ < g_ticks; t_++) anim_tick27(); }
	if (!g_seeking) {
		g_mc.pitch = g_obj.pitch;
		g_mc.yaw = g_obj.yaw;
		g_mc.roll = g_obj.roll;
		Xcur_MeshDraw(g_comp, &g_letters, &g_mc, &g_span);  /* img 0x25e2 */
		memcpy(g_fb, g_comp, PD_SCREEN_W * PD_SCREEN_H);    /* img 0x25e5 */
	}
	Xcur_SeqPos(&order, (s32 *)0);
	if (order >= XCUR_SYNC_ENDPLASMA) scene_enter(33);
}

/* ORIGIN img 0x2358 - the last runner. NO Vga_Present: the screen
 * and the work buffer cannot be one buffer here. */
static void scene_endwater(void)
{
	s16 *t;

	{ s32 t_; for (t_ = 0; t_ < g_ticks; t_++) anim_tick28(); }
	/* img 0x53c4 jumps to Demo_Shutdown here. OURS goes to the
	 * playground instead - docs/playground.txt. */
	{
		s32 order = 0;
		Xcur_SeqPos(&order, (s32 *)0);
		if (order >= XCUR_SYNC_ENDWATER) {
			scene_enter(XCUR_SCENE_PLAY);
			return;
		}
	}
	/* OURS - THE MACHINE SLOWS DOWN HERE, and the ripple is made of
	 * that. effects/theend.txt section 3. */
	g_end_acc += XCUR_END_WATER_HZ;
	if (g_end_acc < XCUR_FRAME_HZ) return;
	g_end_acc -= XCUR_FRAME_HZ;

	if (!g_seeking) {
		g_mc.pitch = g_obj.pitch;
		g_mc.yaw = g_obj.yaw;
		g_mc.roll = g_obj.roll;
		Xcur_MeshDraw(g_comp, &g_letters, &g_mc, &g_span);  /* img 0x2358 */
	}
	Xcur_WaterStep(g_wat_dst, g_wat_src, g_wat_damp);       /* img 0x235b */
	t = g_wat_dst; g_wat_dst = g_wat_src; g_wat_src = t;

	if (!g_seeking)
		Xcur_WaterRefract(g_fb, g_comp, g_wat_src);         /* img 0x235e */
}

/* ORIGIN img 0x1bca - mode 0x19, the credits. THE TEXT SCROLLS
 * BACKWARDS FOR TWO FIFTHS OF THE TIME - the wobble outruns the
 * base - and the starfield rides the same position through WORD
 * stores that wrap. effects/scroller.txt section 5. */
static void anim_tick25(void)
{
	s32 order = 0, e;

	g_scroll_base += 0x40;                      /* img 0x1bca */
	e = Xcur_Sin(g_scroll_wob & 0x7fc) * 2;     /* img 0x1be5 */
	g_scroll_pos = g_scroll_base + e;           /* img 0x1beb */
	g_scroll_wob = (g_scroll_wob + 8) & 0xffff;
	g_star_y = (g_scroll_pos >> 2) & 0xffff;    /* img 0x1bfa, a WORD */
	g_star_z = (-(g_scroll_pos >> 2)) & 0xffff; /* img 0x1c01 */

	Xcur_SeqPos(&order, (s32 *)0);
	if (order < 0x49 && g_fade == 0) return;    /* img 0x1c05 */
	g_fade -= 0x30;                             /* img 0x1c17 */
	Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
}

/* ORIGIN img 0x3007 Scroller_Run - the stencil, then the stars over
 * it, then present. */
static void scene_credits(void)
{
	{ s32 t_; for (t_ = 0; t_ < g_ticks; t_++) anim_tick25(); }
	if (!g_seeking) {
		Xcur_ScrollerRender(g_fb, g_bg, g_sfont, g_srow,
		                    g_scroll_pos);      /* img 0x30c1 */
		Xcur_PointsRenderErase(g_fb, g_stars, g_sprev, XCUR_STARS,
		                       g_star_x, g_star_y, g_star_z);  /* img 0x3273 */
	}
	{
		s32 order = 0;
		Xcur_SeqPos(&order, (s32 *)0);
		if (order >= XCUR_SYNC_CREDITS) scene_enter(31);
	}
}

/* ORIGIN img 0x1faa - THE CAMERA'S TERRAIN FOLLOWER, the tail both
 * mode 2 and mode 0x18 leave through. TWO more probes: one AHEAD of
 * the camera that pitches the view, one UNDER it that is the floor.
 * Leave it out and the terrain rides up through a view that sits at
 * one level. effects/car.txt section 5.1. */
static void cam_follow(void)
{
	s32 si = g_cam.angle & 0x7fc;
	s32 x = g_cam_wx_src - (Xcur_Sin(si) << 3);   /* img 0x1fb3 */
	s32 z = g_cam_wz_src + (Xcur_Cos(si) << 3);
	s32 ax, kind = 0;

	ax = Xcur_TerrainHeightAt(g_texture, g_water, x, z,
	                          g_cam.water_level, g_water_scroll,
	                          g_cam.hscale, (s32 *)0);
	ax = (s32)(s16)((ax + 0x384 - g_cam_h - g_cam_lead) & 0xffff);
	ax >>= 3;                                   /* img 0x1fea */
	g_cam_lead = (s32)(s16)((g_cam_lead + ax) & 0xffff);
	g_sky_bias = (s32)(s16)g_cam_lead >> 4;     /* img 0x1ff6 */
	g_cam_yofs = g_sky_bias * 320;              /* img 0x2001 */

	g_fade_div = (g_fade_div + 1) & 0xff;       /* img 0x2015 */
	if (g_fade_div >= 3) {
		g_fade_div = 0;
		g_water_scroll = (g_water_scroll + 1) & 0xffff;
	}

	/* img 0x202d - a two-stage suspension. EVERY COMPARE HERE IS
	 * UNSIGNED and the final ease is signed. */
	if (g_cam_h3 < 0x1f4) g_cam_h3 = 0x1f4;     /* img 0x2031, jge */
	ax = g_cam_h3;
	if ((u32)ax < (u32)g_cam_h2) {              /* img 0x2045, jnc */
		g_cam_h2 = (g_cam_h2 - 0x80) & 0xffff;
		if ((u32)ax > (u32)g_cam_h2) g_cam_h2 = ax;
	} else if ((u32)ax > (u32)g_cam_h2) {       /* img 0x2060, jbe */
		g_cam_h2 = (g_cam_h2 + 0x80) & 0xffff;
		if ((u32)ax < (u32)g_cam_h2) g_cam_h2 = ax;
	}

	/* img 0x2074 - the probe UNDER the camera: the floor that stops
	 * the view sinking into a hill. Over water, kind 1, it does
	 * nothing. */
	ax = Xcur_TerrainHeightAt(g_texture, g_water, g_cam_wx_src,
	                          g_cam_wz_src, g_cam.water_level,
	                          g_water_scroll, g_cam.hscale, &kind);
	if (kind != 1) {                            /* img 0x2087 */
		ax = (ax + 0x1c2) & 0xffff;
		if ((u32)ax >= (u32)g_cam_h) g_cam_h = ax;
		ax = (ax + 0x1c2) & 0xffff;
		if ((u32)ax >= (u32)g_cam_h2) g_cam_h2 = ax;
	}
	ax = (s32)(s16)((g_cam_h2 - g_cam_h) & 0xffff) >> 4;  /* img 0x20ae */
	g_cam_h = (g_cam_h + ax) & 0xffff;
}

/* ORIGIN img 0x19a0 - mode 0x18, THE CAR, and the most elaborate
 * thing in the demo that is not a renderer. Four terrain probes a
 * tick. effects/car.txt has the whole model.
 *
 * THE TARGETS ARE COMPUTED AFTER THE EASE THAT READS THEM, so the
 * attitude trails the ground by one tick. That is the demo's, not a
 * transcription slip. */
static void anim_tick24(void)
{
	s32 dx, dz, d, g, order = 0;

	g_o2_yaw = (s32)(s16)(g_o2_yaw + (car_sin(g_car_steer) >> 0xb));
	g_car_steer = (g_car_steer + 2) & 0xffff;   /* img 0x19b8 */

	g_o2_pitch = (s32)(s16)(g_o2_pitch
	                        + ((g_car_pitch_t - g_o2_pitch) >> 4));
	g_o2_roll = (s32)(s16)(g_o2_roll
	                       + ((g_car_roll_t - g_o2_roll) >> 4));

	/* img 0x19e0 - NO CEILING, so it is fastest when the scene cuts. */
	g_car_speed++;
	g_car_x += (car_sin(g_o2_yaw) * g_car_speed) >> 0xb;
	g_car_z -= (car_cos(g_o2_yaw) * g_car_speed) >> 0xb;

	/* img 0x1a1d - THE PENETRATION DEPTH BECOMES UPWARD VELOCITY: a
	 * spring of stiffness one eighth, in two instructions, and it is
	 * why the car hops over bumps instead of sliding along them. */
	g_car_vy--;
	g = car_ground(g_car_x, g_car_z);
	if (g_car_y <= g) {
		g_car_vy += (g - g_car_y) >> 3;
		g_car_y = g;
	}
	g_car_y += g_car_vy >> 1;

	/* img 0x1a65 - pitch along the body, probes EIGHT cells apart. */
	dx = car_sin(g_o2_yaw) << 3;
	dz = car_cos(g_o2_yaw) << 3;
	d = (car_ground(g_car_x - dx, g_car_z + dz)
	     - car_ground(g_car_x + dx, g_car_z - dz)) >> 3;
	if (d < -XCUR_CAR_CLAMP) d = -XCUR_CAR_CLAMP;
	if (d >  XCUR_CAR_CLAMP) d =  XCUR_CAR_CLAMP;
	g_car_pitch_t = d;

	/* img 0x1ae3 - THE SIN AND COS ARE SWAPPED, and that swap is the
	 * ninety degrees that makes this the lateral axis. Four cells,
	 * not eight: a wheelbase twice the track. */
	dx = car_cos(g_o2_yaw) << 2;
	dz = car_sin(g_o2_yaw) << 2;
	d = (car_ground(g_car_x - dx, g_car_z - dz)
	     - car_ground(g_car_x + dx, g_car_z + dz)) >> 2;
	if (d < -XCUR_CAR_CLAMP) d = -XCUR_CAR_CLAMP;
	if (d >  XCUR_CAR_CLAMP) d =  XCUR_CAR_CLAMP;
	g_car_roll_t = d;

	g_cam.angle = (g_cam.angle + 2) & 0xffff;  /* img 0x1b61 */
	g_cam_wx_src = g_car_x - (car_sin(g_cam.angle) << 4);
	g_cam_wz_src = g_car_z - (car_cos(g_cam.angle) << 4);

	/* img 0x1ba0 - BOTH BRANCHES ADD. Below order 0x39 that finishes
	 * whatever fade is in progress; at 0x39 it keeps going past zero
	 * and takes the scene out to white. */
	Xcur_SeqPos(&order, (s32 *)0);
	if (order >= 0x39) {
		g_fade += 0x14;
	} else if (g_fade != 0) {
		g_fade += 0x78;
	} else {
		cam_follow();
		return;
	}
	Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	cam_follow();
}

/* ORIGIN img 0x23fe Car_Run - the landscape, then three objects of
 * which only the car moves. effects/car.txt section 6. */
static void scene_car(void)
{
	s32 order = 0;

	{ s32 t_; for (t_ = 0; t_ < g_ticks; t_++) anim_tick24(); }
	if (!g_seeking) {
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);
		g_cam.wx = g_cam_wx_src;                /* img 0x35d7 */
		g_cam.wy = g_cam_wz_src;
		g_cam.scale = (s32)(s16)g_cam_h;
		g_cam.yofs = g_cam_yofs;
		g_cam.sky_bias = g_sky_bias;            /* img 0x3605 */
		g_cam.water_scroll = g_water_scroll;
		Xcur_LookPush(&g_cam, &g_sky_bias);
		Xcur_VoxelRender(g_fb, g_texture, g_colour, g_water, &g_cam);

		g_tgt_x = g_ship_x;                     /* img 0x2401 */
		g_cam.alt = g_ship_y;
		g_tgt_y = g_ship_z;
		g_obj.pitch = g_ship_pitch;
		g_obj.yaw = g_ship_yaw;
		g_obj.roll = g_ship_roll;
		ship_at(&g_ship);

		g_obj.pitch = 0;                        /* img 0x2454 */
		g_obj.yaw = 0x200;
		g_obj.roll = 0;
		g_tgt_y -= 0xb800L;                     /* img 0x2469 */
		g_tgt_x -= 0x5200L;
		g_cam.alt = 0;
		ship_at(&g_flagpost);

		g_tgt_x = g_car_x;                      /* img 0x249d */
		g_cam.alt = g_car_y;
		g_tgt_y = g_car_z;
		g_obj.pitch = g_o2_pitch;
		g_obj.yaw = g_o2_yaw;
		g_obj.roll = g_o2_roll;
		ship_at(&g_car);
		Xcur_LookPop(&g_cam, &g_sky_bias);
	}
	Xcur_SeqPos(&order, (s32 *)0);
	if (order >= XCUR_SYNC_CAR) scene_enter(30);
}

/* ORIGIN img 0x1975 - mode 0x17, the flag's waving AND its fades.
 *
 * THE STEP IS 0xf0, NOT 0x100, and that is deliberate: the part sets
 * the level to 0x2d00, which is exactly 48 steps of 0xf0, so the
 * fade-in reaches zero exactly and the test at img 0x1989 stops it.
 * A step of 0x100 would overshoot into the negative and the scene
 * would darken instead of holding.
 *
 * PAST ROW 0x1c IT SUBTRACTS ANYWAY, img 0x1987 - the same
 * instruction then carries the level below zero and fades the part
 * out to black. One subtract, both transitions. */
static void anim_tick23(void)
{
	g_warp_x = (g_warp_x + 1) & 0xffff;         /* img 0x1975 */
	g_warp_y = (g_warp_y + 0x102) & 0xffff;     /* img 0x197a - TWO */
	if (Xcur_SeqRowCount() <= XCUR_FLAG_ROWS    /* img 0x1981, ja */
	    && g_fade == 0) return;                 /* img 0x1989 */
	g_fade -= XCUR_FLAG_STEP;                   /* img 0x1993 */
	Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
}

/* ORIGIN img 0x2504 Flag_Render, which is its own runner: it writes
 * straight to the screen with no present, and the work buffer behind
 * it never changes for the whole part. */
static void scene_flag(void)
{
	{ s32 t_; for (t_ = 0; t_ < g_ticks; t_++) anim_tick23(); }
	if (!g_seeking)
		Xcur_FlagRender(g_fb, g_bg, g_texture, g_water,
		                g_warp_x, g_warp_y);
	{
		s32 order = 0;
		Xcur_SeqPos(&order, (s32 *)0);
		if (order >= XCUR_SYNC_FLIGHT
		    && Xcur_SeqRowCount() >= g_tick_target) scene_enter(29);
	}
}

/* ORIGIN img 0x17e5 - mode 0x16, THE AIRCRAFT, and it replays a
 * recorded flight rather than computing one. effects/flight.txt has
 * the track, the control bits and why recording the INPUT is what
 * makes 422 bytes buy 42 seconds. */
static void anim_tick22(void)
{
	s32 x = 0, y = 0, z, g, ground, bx;
	s32 al;

	/* img 0x17e5 READS THROUGH THE OLD POINTER: the byte is fetched
	 * at img 0x1802 from the DI latched before img 0x17fd may have
	 * advanced it. */
	al = (s32)g_img[g_track_ptr];
	g_fade_div = (g_fade_div + 1) & 0xff;       /* img 0x17ea */
	if (g_fade_div >= 0xa) {                    /* img 0x17ef */
		g_fade_div = 0;
		g_track_ptr++;
	}

	if (al & 0x01) g_obj.pitch = (g_obj.pitch + 4) & 0xffff;
	if (al & 0x02) g_obj.pitch = (g_obj.pitch - 4) & 0xffff;
	if (al & 0x04) g_obj.roll  = (g_obj.roll  - 4) & 0xffff;
	if (al & 0x08) g_obj.roll  = (g_obj.roll  + 4) & 0xffff;
	if (al & 0x10) g_throttle--;                /* img 0x1831 */
	if (al & 0x20) g_throttle++;
	/* img 0x1841 - ROLL INDUCES YAW, which is why there is no yaw
	 * bit in the track. */
	g_obj.yaw = (g_obj.yaw + ((s32)(s16)g_obj.roll >> 6)) & 0xffff;

	g_rot_pitch = g_obj.pitch;                  /* img 0x184d */
	g_rot_yaw = g_obj.yaw;
	g_rot_roll = g_obj.roll;
	z = g_throttle;
	if (z > 0) z = 0;                           /* img 0x1871, jle */
	Xcur_VecRotate3(&x, &y, &z, g_rot_pitch, g_rot_yaw, g_rot_roll);
	y >>= 7;                                    /* img 0x1880 - SIX */
	x >>= 1;                                    /* places further   */
	z >>= 1;                                    /* than x and z     */
	g_tgt_x += x;                               /* img 0x188a */
	g_cam.alt -= y;
	g_tgt_y += z;

	g = (g_throttle >> 1) + 0x10;               /* img 0x189c */
	if (g > 0) g_cam.alt -= g;

	ground = Xcur_TerrainHeightAt(g_texture, g_water, g_tgt_x, g_tgt_y,
	                              g_cam.water_level, g_cam.water_scroll,
	                              g_cam.hscale, (s32 *)0) + 0x10;
	if (g_cam.alt < ground) {                   /* img 0x18c8 */
		g_cam.alt = ground;
		g_obj.pitch = (g_obj.pitch
		               + (((s32)(s16)(0x30 - g_obj.pitch)) >> 5)) & 0xffff;
		g_obj.roll = (g_obj.roll
		              - (((s32)(s16)g_obj.roll) >> 5)) & 0xffff;
	}
	g_cam_h = g_cam.alt & 0xffff;               /* img 0x18f1, a WORD */

	g_cam.angle = (g_cam.angle - 2) & 0xffff;  /* img 0x18fa */
	bx = g_cam.angle & 0x7fc;              /* img 0x1910 */
	g_cam_wx_src = g_tgt_x - (Xcur_Sin(bx) << 3);
	g_cam_wz_src = g_tgt_y - (Xcur_Cos(bx) << 3);

	/* img 0x1946 - A SECOND PROBE, at the camera rather than the
	 * craft, keeping the view above the landscape on its own. */
	ground = Xcur_TerrainHeightAt(g_texture, g_water, g_cam_wx_src,
	                              g_cam_wz_src, g_cam.water_level,
	                              g_cam.water_scroll, g_cam.hscale,
	                              (s32 *)0) + 0xc8;
	if ((s32)(s16)g_cam_h < ground) g_cam_h = ground & 0xffff;

	if (g_fade == 0) return;                    /* img 0x195e */
	g_fade += 0x100;
	Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
}

/* ORIGIN img 0x23da Landscape_Run - Voxel_Render with the craft over
 * it, and the craft skipped past 0x28000 of z. */
static void scene_landing(void)
{
	s32 order = 0;

	{ s32 t_; for (t_ = 0; t_ < g_ticks; t_++) anim_tick22(); }
	/* img 0x35d7 - the camera latch Voxel_Render opens with. */
	g_cam.wx = g_cam_wx_src;
	g_cam.wy = g_cam_wz_src;
	g_cam.scale = (s32)(s16)g_cam_h;
	backdrop(g_fb);
	Xcur_SeqPos(&order, (s32 *)0);
	if (order >= XCUR_SYNC_FLIGHT) scene_enter(28);
}

/* ORIGIN img 0x1791 - mode 0x15, and IT CARRIES BOTH HALVES OF THE
 * CUT: it climbs g_FadeLevel back to 0 from wherever mode 0x14's
 * fade-out left it, then past row 0x3c drives it down again.
 *
 * ONLY THE CLIMB REACHES THE SCREEN. img 0x17cb leaves for the ISR
 * tail without calling Pal_Fade; img 0x17df calls it. */
static void anim_tick21(void)
{
	g_fade_div = (g_fade_div + 1) & 0xff;       /* img 0x1791 */
	if (g_fade_div >= 2) {                      /* img 0x1796 */
		g_fade_div = 0;
		g_roto_zoom--;                          /* img 0x17a4 */
	}
	g_roto_angle = (g_roto_angle - 1) & 0xffff; /* img 0x17aa */
	g_obj.roll = (g_obj.roll - 3) & 0xffff;     /* img 0x17af */
	g_mc.tz += 0x20;                            /* img 0x17b5 */
	if (Xcur_SeqRowCount() >= 0x3c) {           /* img 0x17bc */
		g_fade -= 0x100;
		return;
	}
	if (g_fade == 0) return;                    /* img 0x17ce */
	g_fade += 0x100;
	Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
}

/* ORIGIN img 0x25f7 Roto_RunWithMesh - the rotozoom with the craft
 * over it. It waits on Demo_WaitTick, so it renders once per timer
 * tick, which is twice a frame. */
static void scene_rotognd(void)
{
	s32 k;

	for (k = 0; k < g_ticks; k++) {
		anim_tick21();
		if (g_seeking) continue;
		Xcur_RotoRender(g_fb, g_depth8, g_texture, g_roto_zoom,
		                g_roto_angle, g_roto_phase, g_roto_blend);
		g_roto_phase++;                         /* img 0x3001 */
		g_mc.pitch = g_obj.pitch;
		g_mc.yaw = g_obj.yaw;
		g_mc.roll = g_obj.roll;
		Xcur_MeshDraw(g_fb, g_obj_model, &g_mc, &g_span);
	}
	{
		s32 order = 0;
		Xcur_SeqPos(&order, (s32 *)0);
		if (order >= XCUR_SYNC_ROTOGND) scene_enter(27);
	}
}

/* ORIGIN img 0x2925 Planet_Render once more, and THE ONLY ONE OF ITS
 * SIX PARTS THAT LETS THE BALL DRAW besides the opening planet. */
static void scene_moon(void)
{
	{ s32 t_; for (t_ = 0; t_ < g_ticks; t_++) anim_tick20(); }
	if (!g_seeking) {
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);
		Xcur_PointsRender(g_fb, g_stars, XCUR_STARS,
		                  g_star_x, g_star_y, g_star_z);
		Xcur_PlanetRender(g_fb, g_sphere, g_texture, g_pan, g_approach);
		g_mc.pitch = g_obj.pitch;
		g_mc.yaw = g_obj.yaw;
		g_mc.roll = g_obj.roll;
		Xcur_MeshDraw(g_fb, g_obj_model, &g_mc, &g_span);
	}
	{
		s32 order = 0;
		Xcur_SeqPos(&order, (s32 *)0);
		if (order >= XCUR_SYNC_MOON) scene_enter(26);
	}
}

/* ORIGIN img 0x16a8 - mode 0x11 with a slower z, and the one thing
 * that makes the tunnel an exit: it counts g_TunnelFade down every
 * OTHER tick, which opens the hole. effects/tunnel.txt 5. */
static void anim_tick19(void)
{
	Xcur_PalCycleBands(g_pal_master);
	Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	g_obj.roll = (g_obj.roll - 8) & 0xffff;
	g_mc.tz += 0x80;                            /* img 0x16b4 */
	g_roto_angle = (g_roto_angle + 8) & 0xffff;
	g_roto_zoom += 0x100;
	g_fade_div = (g_fade_div + 1) & 0xff;       /* img 0x16ce */
	if (g_fade_div >= 2) {
		g_fade_div = 0;
		if (g_tun_fade != 0) g_tun_fade--;      /* img 0x16e9 */
	}
	if (g_fade == 0) return;
	g_fade += 0x80;                             /* img 0x16f8 */
}

/* ORIGIN img 0x2aef Tunnel_Run. It eases both scrolls onto their
 * targets by an eighth - the demo's usual shift-and-add - renders,
 * draws the craft and presents. Input_Poll rather than Demo_WaitTick,
 * so it runs flat out. */
static void scene_tunnel(void)
{
	s32 order = 0;

	s32 k;

	/* THE EASE IS PER RENDER, NOT PER TICK - it sits inside the
	 * runner's loop at img 0x2afa, so how far the tunnel lags its
	 * target is set by the render rate. effects/tunnel.txt 9. */
	for (k = 0; k < g_ticks; k++) {
		if (g_anim_mode == 0x10)      anim_tick16();
		else if (g_anim_mode == 0x11) anim_tick17();
		else                          anim_tick19();
		g_tun_depth += (g_roto_zoom - g_tun_depth) >> 3;  /* img 0x2afa */
		g_tun_angle = (s32)(s16)
		    (g_tun_angle
		     + (((s32)(s16)(g_roto_angle - g_tun_angle)) >> 3));
		if (g_seeking) continue;
		Xcur_SeqPos(&order, (s32 *)0);
		g_tun_parity = Xcur_TunnelRender(g_fb, g_texture, g_angle_tab,
		                                 g_colour, g_depth8,
		                                 g_tun_angle,
		                                 (g_tun_depth >> 8) & 0xff,
		                                 g_tun_parity, order,
		                                 g_tun_fade);
		g_mc.pitch = g_obj.pitch;
		g_mc.yaw = g_obj.yaw;
		g_mc.roll = g_obj.roll;
		Xcur_MeshDraw(g_fb, g_obj_model, &g_mc, &g_span);
	}
	Xcur_SeqPos(&order, (s32 *)0);
	if (g_scene == 20 && order >= XCUR_SYNC_TUNNEL) scene_enter(21);
	else if (g_scene == 21 && order >= XCUR_SYNC_TUNNEL2) scene_enter(22);
	else if (g_scene == 23 && order >= XCUR_SYNC_EXIT) scene_enter(24);
}

/* ORIGIN img 0x166f - mode 0x12, and THE MUSIC IS THE ONLY THING
 * DRIVING THE WATER.
 *
 * The eight slots it walks are the eight MUSIC CHANNELS, not a drop
 * table: img 0x0d26 arms a channel's counter to 0x10 on every note it
 * plays, and nothing else in the image reads that counter. So each
 * channel owns one drip row - 35, 43, 51 up to 91, eight rows apart
 * straight down the middle - and the column comes off that channel's
 * own SAMPLE STEP, so the note's pitch moves the splash sideways.
 *
 * 0xFC00 IS -1024 AS A SIGNED WORD: A DENT, NOT A BUMP. A falling
 * drop pushes the surface down, and the sign is the difference
 * between eight impacts and eight rising columns.
 *
 * It writes into the buffer the solver is about to WRITE, img 0x1677,
 * so a drop lands in the frame being computed rather than the one
 * being read. */
static void anim_tick18(void)
{
	s32 n;

	for (n = 0; n < 8; n++) {                   /* img 0x166f */
		s32 di, off;

		if (Xcur_SeqChanAge(n) == 0) continue;  /* img 0x167c */
		Xcur_SeqChanAgeDec(n);                  /* img 0x1684 */
		/* img 0x168f is an UNSIGNED 32-bit shift, and img 0x1693 keeps
		 * the low 16 bits - so a big step wraps the column. */
		di = (s32)(((u32)Xcur_SeqChanStep(n) >> 9) & 0xfffe);
		off = (0x2d00 + n * 0xa00 + di - 0xa0) & 0xffff;
		g_wat_dst[off >> 1] = (s16)-1024;       /* img 0x1696, 0xfc00 */
	}
}

/* ORIGIN img 0x29bf Water_Run - solve, draw, wait a tick, loop. */
static void scene_water(void)
{
	s16 *t;

	s32 order = 0;

	{ s32 t_; for (t_ = 0; t_ < g_ticks; t_++) anim_tick18(); }
	Xcur_WaterStep(g_wat_dst, g_wat_src, g_wat_damp);
	t = g_wat_dst; g_wat_dst = g_wat_src; g_wat_src = t;  /* img 0x2a10 */
	if (!g_seeking)
		Xcur_WaterRenderSpans(g_fb, g_wat_src);
	Xcur_SeqPos(&order, (s32 *)0);
	if (order >= XCUR_SYNC_WATER) scene_enter(23);
}

/* OURS - entering a scene from a key rather than from the script. */
static void scene_enter(s32 which)
{
	/* A PART'S ENTRY ALWAYS DRAWS, seek or not. Its one-off work -
	 * halving the field, blending a banner, building a background -
	 * is state, not a frame, and skipping it during a seek and then
	 * repeating it afterwards applies it twice. */
	s32 was_seeking = g_seeking;

	g_seeking = 0;
	g_scene = ((which % XCUR_SCENES) + XCUR_SCENES) % XCUR_SCENES;
	if (g_scene == 1) {
		/* THE POSITION IS FLOWN, NOT SEEDED. These two are the
		 * leftovers whose writer is still untraced - camera.txt
		 * section 5. The altitude is NOT one of them any more: it is
		 * probed from the ground at img 0x0109, in demo_restart. */
		g_cam.scale = XCUR_CD_SCALE;
		g_sky_bias = XCUR_CD_SKYBIAS;
		backdrop(g_bg);
		memcpy(g_fb, g_bg, sizeof g_fb);
		g_window = 0;
		g_npart = Xcur_CountdownSpawn(g_part, XCUR_COUNTDOWN_MAX, 0);
		g_burst = XCUR_BURST_TICKS;
		g_digits = 0;
	} else if (g_scene == 3) {
		/* The part's own block at img 0x01a1. THE FADE IS NOT IN IT
		 * - the white carries over from the approach.
		 *
		 * IT BUILDS ITS OWN TERRAIN. img 0x022d regenerates the
		 * plasma with a different mask, bias and roughness, and img
		 * 0x023e rebuilds the shade map from it, so the ground here
		 * is NOT the landscape the opening flew over. Reusing the
		 * opening's field gives the right shape and the wrong land. */
		Xcur_PlasmaGenerate(g_texture, 0xfff, 0x800, 2);
		Xcur_TerrainBuildShade(g_colour, g_texture, 0x1000, 0x53);
		g_launch_div = 0;               /* img 0xa882 */
		g_launch_step = XCUR_LAUNCH_STEP;
		g_mc.tx = 0;
		g_mc.ty = XCUR_LAUNCH_TY;
		g_mc.tz = XCUR_LAUNCH_TZ;
		g_obj.pitch = XCUR_LAUNCH_PITCH;
		g_obj.yaw = 0;
		g_obj.roll = XCUR_LAUNCH_ROLL;
		g_mc.pitch = g_obj.pitch;
		g_mc.yaw = g_obj.yaw;
		g_mc.roll = g_obj.roll;
		/* ITS YAW IS THE ODD ONE - 0x421, not the 0xff00 every other
		 * part uses. formats/mesh.txt section 5. */
		g_mc.light_yaw = XCUR_LAUNCH_LYAW;      /* img 0x0193 */
		g_mc.light_pitch = XCUR_LIGHT_PITCH;    /* img 0x019a */
		g_mc.light_roll = XCUR_LAUNCH_LROLL;    /* img 0x01a1 */
		g_cam.wx = 0;                   /* img 0x01f9 */
		g_cam.wy = 0;
		g_cam.angle = 0;                /* img 0x01f2 */
		g_cam.scale = XCUR_LAUNCH_SCALE;/* img 0x020d */
		g_mc.cam_yaw = 0;
		g_sky_bias = XCUR_LAUNCH_STEP;
		g_mc.sky_bias = XCUR_LAUNCH_STEP;
	} else if (g_scene == 4) {
		/* The part's own block at img 0x0253. The craft starts dead
		 * ahead and mode 4 walks it in at 1024 a tick. It builds the
		 * sphere table and the stars, img 0x02d2 and img 0x02d5, and
		 * the stars are 3000 draws off the shared stream - so they
		 * belong to this part and nowhere else.
		 *
		 * img 0x02cf IS A TERRAIN PASS, NOT A PALETTE BUILD. Nothing
		 * here draws the colour map, so the tint shows up only much
		 * later - it is what the battle's glow blurs on its first
		 * frames, because the glow buffers live inside that same
		 * colour map. Pal_BuildMain is called at img 0x0056 and not
		 * again until img 0x039d, so the palette is already right. */
		Xcur_TerrainTintWater(g_colour, g_texture);
		Xcur_PlanetBuildMap(g_sphere);
		Xcur_PointsMake(g_stars, XCUR_STARS);
		g_tick_target = XCUR_PART_ROWS;         /* img 0x02e2 */
		g_pan = XCUR_PLANET_PAN;
		g_approach = XCUR_PLANET_NEAR;
		g_star_x = 0; g_star_y = 0; g_star_z = 0;
		g_obj.pitch = 0; g_obj.yaw = 0; g_obj.roll = 0;
		g_cam.angle = 0;
		g_sky_bias = XCUR_PLANET_BIAS;
		g_mc.tx = 0; g_mc.ty = 0; g_mc.tz = XCUR_PLANET_TZ;
		g_mc.pitch = 0; g_mc.yaw = 0; g_mc.roll = 0; g_mc.cam_yaw = 0;
		g_mc.sky_bias = XCUR_PLANET_BIAS;
		g_mc.light_pitch = 0;
		g_mc.light_yaw = XCUR_PLANET_LYAW;
		g_mc.light_roll = 0;
	} else if (g_scene == 5) {
		/* The part's block at img 0x02ec. It HALVES THE FIELD and
		 * blends the letters into it, so the rotozoom turns them
		 * along with the texture - roto.txt. */
		s32 k;
		Xcur_PalBuildBlue(g_pal_master);        /* img 0x030d */
		g_fade = 0; g_fade_prev = 0;            /* img 0x0303 */
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		for (k = 0; k < 0x10000; k++)           /* img 0x0322 */
			g_texture[k] >>= 1;
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);
		Xcur_TextBuildBanner(g_texture, XCUR_HUI_REC, XCUR_HUI_BRIGHT);
		g_roto_zoom = XCUR_ROTO_ZOOM;
		g_roto_angle = 0;
		g_roto_phase = 0;
	} else if (g_scene == 7) {
		/* img 0x03b4 - the second pass. It clears the WORK buffer
		 * only and leaves the field alone, so the letters blended in
		 * for the first pass are still there. */
		Xcur_PalBuildBlue(g_pal_master);
		g_fade = 0; g_fade_prev = 0;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);
		g_roto_zoom = XCUR_ROTO_ZOOM;
		g_roto_angle = XCUR_ROTO2_ANGLE;
		g_roto_phase = 0;
	} else if (g_scene == 8) {
		/* img 0x03ee - its own block. It regenerates the field at a
		 * different roughness and opens at the fade's floor, so the
		 * plasma rises out of black. */
		s32 k;
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);
		Xcur_PalBuildBluePurple(g_pal_master);
		g_fade = XCUR_WARP_FADE;                /* img 0x03f4 */
		g_fade_prev = XCUR_WARP_FADE;
		g_fade_target = 0;                      /* img 0x991f's static */
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		Xcur_PlasmaGenerate(g_texture, 0x7ff, 0x400, 1);
		Xcur_BufClampMin1(g_texture);           /* img 0x0420 */
		for (k = 0; k < XCUR_WARP_OFS; k++) {
			g_warp_ofs[k] = 0;
			g_warp_ph[k] = 0;
		}
	} else if (g_scene == 9) {
		/* img 0x042d - its own block, and IT ZEROES THE ACCUMULATOR
		 * at img 0x0447. Entering on the field the warp left behind
		 * accumulates from a plasma instead of from flat and the
		 * bands never resolve - effects/moire.txt section 1.1. */
		Xcur_PalBuildBluePurple(g_pal_master);  /* img 0x042d */
		g_fade = XCUR_WARP_FADE;                /* img 0x0430 */
		g_fade_prev = XCUR_WARP_FADE;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		memset(g_texture, 0, sizeof g_texture);
		g_moire_phase = 0;                      /* img 0x9921's static */
	} else if (g_scene == 10) {
		/* img 0x045d - Pal_BuildMain, then the fade's CEILING: 0x3d80
		 * biases every component by 0x3d, so the part opens WHITE and
		 * mode 8 walks it back down. The moire ends washing out to
		 * white, so the two meet with no cut.
		 *
		 * It parses craft B here, img 0x0474, which is the only place
		 * either craft's model is read. */
		Xcur_PalBuild(g_pal_master);
		Xcur_MeshParse(&g_craft_b, XCUR_CRAFT_B);
		Xcur_MeshNormals(&g_craft_b, &g_mc, XCUR_MESH_ZSCALE); /* img 0x0474 */
		Xcur_MeshParse(&g_bolt, XCUR_SHOT_BOLT);
		Xcur_MeshParse(&g_winged, XCUR_SHOT_WINGED);
		/* THE SHOTS BORROW CRAFT A'S NORMALS, and that is the demo's
		 * doing: Shots_Draw sets the vertex segment to craft A's
		 * 0x3a79 at img 0x27ad, and the normal table lives inside
		 * that segment. Nothing ever precomputes a weapon's own, so
		 * each shot face is lit by craft A's face of the same index.
		 * Giving them their own would light them differently from the
		 * demo. */
		memcpy(g_bolt.fnx, g_ship.fnx, sizeof g_bolt.fnx);
		memcpy(g_bolt.fny, g_ship.fny, sizeof g_bolt.fny);
		memcpy(g_bolt.fnz, g_ship.fnz, sizeof g_bolt.fnz);
		memcpy(g_winged.fnx, g_ship.fnx, sizeof g_winged.fnx);
		memcpy(g_winged.fny, g_ship.fny, sizeof g_winged.fny);
		memcpy(g_winged.fnz, g_ship.fnz, sizeof g_winged.fnz);
		g_obj_model = &g_ship;                  /* img 0x047e */
		g_anim_mode = 8;                        /* img 0x0460 */
		g_mc.tx = 0;                            /* img 0x0485 */
		g_mc.ty = 0x1000;
		g_mc.tz = 0x2000;
		g_obj.pitch = 0xff80;                   /* img 0x04a3 */
		g_obj.yaw = 0x400;
		g_obj.roll = 0x400;
		g_mc.cam_yaw = 0;
		g_approach = XCUR_STARS_FAR;            /* img 0x04b8 */
		g_fade = XCUR_CHASE_FADE;               /* img 0x04c9 */
		g_fade_prev = XCUR_CHASE_FADE;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	} else if (g_scene == 11) {
		/* img 0x04e0 - the white ceiling again and a cut to craft B's
		 * model, placed far out to one side. */
		g_fade = XCUR_CHASE_FADE;               /* img 0x04e0 */
		g_fade_prev = XCUR_CHASE_FADE;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		g_obj_model = &g_craft_b;               /* img 0x04f1 */
		g_mc.tx = 0x16000L;                     /* img 0x04f8 */
		g_mc.ty = -0xc000L;
		g_mc.tz = 0x38000L;
		g_obj.pitch = 0x80;                     /* img 0x0516 */
		g_obj.yaw = 0xfe00;
		g_obj.roll = 0x400;
		g_anim_mode = 8;                        /* img 0x052b */
		g_approach = XCUR_STARS_FAR;            /* img 0x0531 */
	} else if (g_scene == 12) {
		/* img 0x0548 - the first Battle_Run part, and the only one of
		 * the three that clears neither the shots nor the glow. */
		g_fade = XCUR_FIGHT_FADE;               /* img 0x0548 */
		g_fade_prev = XCUR_FIGHT_FADE;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		g_a_x = 0; g_a_y = 0; g_a_z = 0x6000;   /* img 0x0552 */
		g_a_pitch = 0x1c4;                      /* img 0x0570 */
		g_a_yaw = 0x100;
		g_a_roll = 0x7b;
		g_anim_mode = 9;                        /* img 0x0585 */
		g_b_x = 0; g_b_y = 0;                   /* img 0x058b */
		g_b_z = -0x14000L;
		g_b_pitch = 0; g_b_yaw = 0; g_b_roll = 0;
	} else if (g_scene == 13) {
		/* img 0x05cb - the Lissajous pass. */
		g_fade = XCUR_FIGHT_FADE;               /* img 0x05cb */
		g_fade_prev = XCUR_FIGHT_FADE;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		Xcur_GlowClear(g_glowmem, g_spark);     /* img 0x05d5 */
		memset(g_shot, 0, sizeof g_shot);       /* img 0x05d8 */
		g_a_x = 0; g_a_y = 0; g_a_z = 0x10000L; /* img 0x05db */
		g_a_pitch = 0;                          /* img 0x05f9 */
		g_a_yaw = 0x400;
		g_a_roll = 0;
		g_anim_mode = 0x0a;                     /* img 0x060e */
		g_b_x = 0; g_b_y = 0; g_b_z = 0x6000;   /* img 0x0614 */
		g_b_pitch = 0;                          /* img 0x0632 */
		g_b_yaw = 0x400;
		g_b_roll = 0x400;
	} else if (g_scene == 14) {
		/* img 0x0654 - the locked formation, and the only part where
		 * craft A does the shooting. */
		memset(g_shot, 0, sizeof g_shot);       /* img 0x0654 */
		g_mc.light_pitch = 0x100;               /* img 0x0657 */
		g_mc.light_yaw = 0xff00;
		g_mc.light_roll = 0;
		g_fade = XCUR_FIGHT_FADE;               /* img 0x066c */
		g_fade_prev = XCUR_FIGHT_FADE;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		Xcur_GlowClear(g_glowmem, g_spark);     /* img 0x0676 */
		g_a_x = -0x9000L;                       /* img 0x0679 */
		g_a_y = 0x8000L;
		g_a_z = 0x10000L;
		g_a_pitch = 0x100;                      /* img 0x0697 */
		g_a_yaw = 0x300;
		g_a_roll = 0;
		(void)Xcur_SeqTakeNewPattern();         /* img 0x06ac */
		g_anim_mode = 0x0b;                     /* img 0x06b2 */
		g_b_x = 0;                              /* img 0x06b8 */
		g_b_y = 0x3000;
		g_b_z = 0xa000;
		g_b_pitch = 0xff60;                     /* img 0x06d6 */
		g_b_yaw = 0x300;
		g_b_roll = 0x400;
	} else if (g_scene == 15) {
		/* img 0x06f5 - the aftermath. It opens at the white ceiling
		 * again and mode 0xc eases it down at half the chase's rate.
		 *
		 * It does NOT reset the particle count, so the trickle picks
		 * up from wherever mode 0xc last left it. */
		g_fade = XCUR_CHASE_FADE;               /* img 0x06f5 */
		g_fade_prev = XCUR_CHASE_FADE;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		g_anim_mode = 0x0c;                     /* img 0x06ff */
		g_obj_model = &g_ship;                  /* img 0x070c */
		g_mc.tx = 0;                            /* img 0x0713 */
		g_mc.ty = 0;
		g_mc.tz = 0x30000L;
		g_obj.pitch = 0xffe0;                   /* img 0x0731 */
		g_obj.yaw = 0x40;
		g_obj.roll = 0x300;
	} else if (g_scene == 16) {
		/* img 0x0750 - the warp out, and it takes its OWN palette.
		 * img 0x3c1d is blue-dominated where Pal_BuildMain is a fire
		 * ramp, so the glow's 0x60..0xbf window reads blue here. */
		Xcur_PalBuildWarp(g_pal_master);        /* img 0x0750 */
		g_fade = 0;                             /* img 0x0753 */
		g_fade_prev = 0;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		Xcur_GlowClear(g_glowmem, g_spark);     /* img 0x075d */
		g_zscale = 0x100;                       /* img 0x0760 */
		g_anim_mode = 0x0d;                     /* img 0x076a */
		g_obj_model = &g_ship;
		g_mc.tx = -0x6000L;                     /* img 0x0770 */
		g_mc.ty = 0x4000L;
		g_mc.tz = 0x4000L;
		g_obj.pitch = 0xffe4;                   /* img 0x078e */
		g_obj.yaw = 0x3d0;
		g_obj.roll = 0;
	} else if (g_scene == 17) {
		/* img 0x07b0. THE HALVING AND THE HALF-PALETTE ARE ONE
		 * DECISION - effects/roto.txt section 4.2. */
		s32 i;

		g_roto_zoom = 0x30c;                    /* img 0x07b0 */
		g_roto_angle = 0;                       /* img 0x07ba */
		g_anim_mode = 0x0e;                     /* img 0x07c1 */
		g_fade = 0;                             /* img 0x07c7 */
		g_fade_prev = 0;
		Xcur_PalBuildBlue(g_pal_master);        /* img 0x07ce */
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		Xcur_PlasmaGenerate(g_texture, 0xfff, 0x800, 1);  /* img 0x07ed */
		for (i = 0; i < 0x10000; i++)           /* img 0x07fa */
			g_texture[i] = (u8)(g_texture[i] >> 1);
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);  /* img 0x0802 */
	} else if (g_scene == 18) {
		/* img 0x081c - the beams. img 0x084b zeroes the zoom, so the
		 * first stripes are infinitely wide and narrow in as the
		 * velocity ramps up. */
		g_pal_phase[0] = 0x400;                 /* img 0x081c */
		g_pal_phase[1] = 0x400;
		g_pal_phase[2] = 0x400;
		Xcur_PalBuildRgbRamps(g_pal_master, g_pal_phase);  /* img 0x0831 */
		g_fade = 0;                             /* img 0x0834 */
		g_fade_prev = 0;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		/* img 0x083e clears the WHOLE 64 KB work buffer, which is
		 * this effect's state as well as its picture. */
		memset(g_fb, 0, sizeof g_fb);
		g_roto_angle = 0;                       /* img 0x0844 */
		g_roto_zoom = 0;                        /* img 0x084b */
		g_angle_rate = 0x20;                    /* img 0x0855 */
		g_zoom_vel = 0;                         /* img 0x9468's static */
	g_tun_fade = XCUR_TUNNEL_FADE;          /* img 0xa59c's static */
	g_roto_blend = XCUR_ROTO_BLEND;         /* img 0xa59d's static */
	/* EVERY ONE OF THESE IS A STATIC IN THE IMAGE and a seek replays
	 * the whole demo, so anything left behind accumulates across
	 * replays. The bearing shows it first - mode 0x16 takes 2 off it
	 * a tick, so one un-reset landing moves it most of a turn. */
	g_cam_wx_src = -0xa000L;                /* img 0x9360, 0x9364  */
	g_cam_wz_src = -0xa000L;
	g_cam_h = 0x0b00;                       /* img 0x9871, 73, 75  */
	g_cam_h2 = 0x0b00;
	g_cam_h3 = 0x01f4;
	g_cam_lead = 0;                         /* img 0x98f7, 0x9450  */
	g_cam_yofs = 0;
	g_water_scroll = 0;                     /* img 0x997b          */
	g_o2_pitch = 0; g_o2_yaw = 0; g_o2_roll = 0;
	g_ship_x = 0; g_ship_y = 0; g_ship_z = 0;
	g_ship_pitch = 0; g_ship_yaw = 0; g_ship_roll = 0;
	g_car_x = 0; g_car_y = 0; g_car_z = 0;
	g_car_pitch_t = 0; g_car_roll_t = 0;
	g_car_steer = 0;
	g_tick_target = 0;
	g_scroll_base = XCUR_SCROLL0;           /* img 0x9380, 0x937c  */
	g_scroll_pos = XCUR_SCROLL0;
	g_scroll_wob = 0;                       /* img 0x98d5          */
	g_end_phase = 0;                        /* img 0x9939, 0x9474  */
	g_end_amp = 0;
	g_end_acc = 0;
	g_wat_damp = XCUR_WATER_DAMPING;        /* img 0xa59f          */
	g_tick_acc = 0;
	g_ticks = 0;
	g_car_speed = 0;                        /* img 0x9470's static */
	g_car_vy = 0;
	g_track_ptr = XCUR_FLIGHT_TRACK;        /* img 0x98b9's static */
	g_throttle = XCUR_THROTTLE0;            /* img 0x9378's static */
	g_fade_div = 0;
		g_anim_mode = 0x0f;                     /* img 0x085c */
	} else if (g_scene == 19) {
		/* img 0x086c - the same beams swept the other way. It changes
		 * four words and NOTHING CLEARS THE WORK BUFFER, so this part
		 * starts from the first one's trails. */
		g_pal_phase[0] = 0xff80;                /* img 0x086c */
		g_pal_phase[1] = 0xf980;
		g_pal_phase[2] = 0xf680;
		g_angle_rate = -0x20;                   /* img 0x0881 */
	} else if (g_scene == 20) {
		/* img 0x0892 - the tunnel. ITS TEXTURE IS THE ROTATING
		 * PLASMA'S FIELD, still halved into 0..127: no
		 * Plasma_Generate runs between img 0x07ed and here, and img
		 * 0x089f only lifts the zeros - effects/tunnel.txt. */
		g_zscale = XCUR_MESH_ZSCALE;            /* img 0x0892 */
		g_mc.zscale = XCUR_MESH_ZSCALE;
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);  /* img 0x089c */
		Xcur_BufClampMin1(g_texture);            /* img 0x089f */
		g_obj_model = &g_ship;
		g_mc.tx = 0x5000;                       /* img 0x08a2 */
		g_mc.ty = 0x2000;
		g_mc.tz = 0x30000L;
		g_obj.pitch = 0;                        /* img 0x08c0 */
		g_obj.yaw = 0;
		g_obj.roll = 0;
		g_tun_depth = 0;                        /* img 0x08d5 */
		g_tun_angle = 0;                        /* img 0x08df */
		g_roto_zoom = 0;                        /* img 0x08e6 */
		g_roto_angle = 0;                       /* img 0x08f0 */
		g_anim_mode = 0x10;                     /* img 0x08f7 */
		Xcur_PalBuild(g_pal_master);            /* img 0x08fe */
		Xcur_PalBuildBlueMagenta(g_pal_master);      /* img 0x0901 */
		g_fade = XCUR_WARP_FADE;                /* img 0x0905 */
		g_fade_prev = XCUR_WARP_FADE;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		Xcur_TunnelBuildAngle(g_angle_tab, &g_span);  /* img 0x090f */
	} else if (g_scene == 21) {
		/* img 0x091c - the same tunnel backwards. YAWED HALF A TURN
		 * so the craft faces away, and nothing is rebuilt -
		 * effects/tunnel.txt section 6. */
		g_mc.tx = -0x5000L;                     /* img 0x091c */
		g_mc.ty = -0x2000L;
		g_mc.tz = 0x4000L;
		g_obj.pitch = 0;                        /* img 0x093a */
		g_obj.yaw = 0x400;                      /* img 0x0941 */
		g_obj.roll = 0;                         /* img 0x0948 */
		g_anim_mode = 0x11;                     /* img 0x094f */
	} else if (g_scene == 22) {
		/* img 0x095f - the dripping water. It CLEARS BOTH FIELDS,
		 * img 0x0962 and img 0x0968, because the solver's old-value
		 * term reads one of them before anything has written it. */
		memset(g_wat_a, 0, sizeof g_wat_a);     /* img 0x0962 */
		memset(g_wat_b, 0, sizeof g_wat_b);     /* img 0x0968 */
		g_wat_dst = g_wat_a;                    /* img 0x096b */
		g_wat_src = g_wat_b;                    /* img 0x0972 */
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);  /* img 0x0979 */
		g_anim_mode = 0x12;                     /* img 0x097c */
		/* img 0x0982 goes STRAIGHT TO THE DAC, so no fade follows and
		 * none is wanted - xcursion_pal.c. */
		Xcur_PalSetRgbRamps(g_pal);
	} else if (g_scene == 23) {
		/* img 0x098f - the tunnel a third time, WITH ITS EXIT.
		 * effects/tunnel.txt 6.1. IT ARMS THE MODE BEFORE ITS OWN
		 * SETUP, img 0x0992 against img 0x09fb, so the fade is
		 * already down a few steps when the first frame shows. */
		s32 i;

		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);  /* img 0x098f */
		g_anim_mode = 0x13;                     /* img 0x0992 */
		Xcur_PlasmaGenerate(g_texture, 0xfff, 0x800, 1);  /* img 0x09b7 */
		for (i = 0; i < 0x10000; i++)           /* img 0x09ba */
			g_texture[i] = (u8)(g_texture[i] >> 1);
		Xcur_BufClampMin1(g_texture);
		g_mc.tx = -0x5000L;                     /* img 0x09cf */
		g_mc.ty = 0x2000L;
		g_mc.tz = 0x4000L;
		g_obj.pitch = 0;
		g_obj.yaw = 0x400;
		g_obj.roll = 0;                         /* img 0x09fb */
		g_tun_depth = 0;                        /* img 0x0a02 */
		g_tun_angle = 0;
		g_roto_zoom = 0;
		g_roto_angle = 0;
		Xcur_PalBuild(g_pal_master);            /* img 0x0a24 */
		Xcur_PalBuildBlueMagenta(g_pal_master);
		/* img 0x0a2a - no level set; it rebuilds g_pal after the
		 * water wrote the DAC straight. effects/water.txt 8. */
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	} else if (g_scene == 24) {
		/* img 0x4d6e - THE FIRST PART OF Demo_Timeline, which is a
		 * separate chain from Demo_Entry. effects/planet.txt.
		 *
		 * It is Planet_Render again with the planet pushed out of
		 * range, so only the stars and the craft draw. */
		Xcur_PalBuild(g_pal_master);            /* img 0x4d6e */
		g_anim_mode = 8;
		g_mc.tx = 0;                            /* img 0x4d77 */
		g_mc.ty = 0x1000L;
		g_mc.tz = 0x2000L;
		g_obj.pitch = (s32)0xff80;              /* img 0x4d95 */
		g_obj.yaw = 0x400;
		g_obj.roll = 0x400;
		g_approach = XCUR_PLANET_FAR;           /* img 0x4daa */
		g_tick_target = 0;                      /* img 0x4db4 */
		g_fade = XCUR_FADE_WHITE;               /* img 0x4dbb */
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		Xcur_PointsMake(g_stars, XCUR_STARS);   /* img 0x4dc5 */
	} else if (g_scene == 25) {
		/* img 0x4dd5 - THE GREY MOON. effects/planet.txt.
		 *
		 * Its surface is the plasma turned into a SLOPE and back: the
		 * shade map at img 0x4e61, then img 0x4e64's (v << 2) + 0x80,
		 * which Planet_Render's own (t >> 2) - 0x20 undoes - so the
		 * screen index is the shade, 1 to 0x1e, which is exactly the
		 * band Pal_BuildGreyLow greys. */
		s32 i;

		g_anim_mode = 0x14;                     /* img 0x4dd5 */
		g_mc.tx = -0x5000L;                     /* img 0x4ddb */
		g_mc.ty = 0x3000L;
		g_mc.tz = 0x2000L;
		g_obj.pitch = (s32)0xff80;              /* img 0x4df9 */
		g_obj.yaw = 0x380;
		g_obj.roll = 0;
		g_approach = XCUR_PLANET_NEAR;          /* img 0x4e0e */
		g_pan = XCUR_MOON_PAN;                  /* img 0x4e18 */
		g_mc.light_pitch = 0;                   /* img 0x4e1f */
		g_mc.light_yaw = XCUR_MOON_LYAW;
		g_mc.light_roll = 0;
		Xcur_PlanetBuildMap(g_sphere);          /* img 0x4e34 */
		Xcur_PlasmaGenerate(g_texture, 0x3ff, 0x200, 1);  /* img 0x4e50 */
		/* img 0x4e53 sets the field to the full 256x256 from its
		 * origin, which is the shape Xcur_TerrainBuildShade already
		 * has; the scale and divisor are not touched here and carry
		 * the opening's. effects/voxel.txt 1.2. */
		Xcur_TerrainBuildShade(g_colour, g_texture,
		                       XCUR_VOX_HSCALE, XCUR_SHADE_DIV);
		for (i = 0; i < 0x10000; i++)           /* img 0x4e70 */
			g_texture[i] = (u8)(((g_colour[i] << 2) + 0x80) & 0xff);
		Xcur_PalBuildGreyLow(g_pal_master);     /* img 0x4e7d */
		g_fade = 0;                             /* img 0x4e80 */
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		Xcur_PointsMake(g_stars, XCUR_STARS);   /* img 0x4e91 */
	} else if (g_scene == 26) {
		/* img 0x4e97 - the moon's surface from right behind, as a
		 * rotozoom. THE FEEDBACK BUFFER MOVES OFF THE SCREEN for
		 * this part alone, img 0x4e9a, because it is the only
		 * rotozoom with a craft over it. effects/roto.txt 5. */
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);  /* img 0x4e9e */
		g_anim_mode = 0x15;                     /* img 0x4ea1 */
		g_mc.tx = 0x5000L;                      /* img 0x4ea7 */
		g_mc.ty = -0x1000L;
		g_mc.tz = 0x7000L;
		g_obj.pitch = 0x40;                     /* img 0x4ec5 */
		g_obj.yaw = 0x480;
		g_obj.roll = 0;
		g_roto_zoom = 0x200;                    /* img 0x4eda */
		g_roto_angle = 0;
		Xcur_PlasmaGenerate(g_texture, 0x3ff, 0x200, 1);  /* img 0x4f04 */
		Xcur_TerrainBuildShade(g_colour, g_texture,
		                       XCUR_VOX_HSCALE, XCUR_SHADE_DIV);
		/* img 0x4f2e - a PLAIN copy. The moon's part transforms the
		 * same map on its way across; this one does not. */
		memcpy(g_texture, g_colour, 0x10000);
		/* img 0x4f2e clears all 64 KB of both segments; these two
		 * buffers are the screen's worth, which is every byte either
		 * the rotozoom or the blit can reach. */
		memset(g_depth8, 0, sizeof g_depth8);
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);
		g_roto_blend = 1;                       /* img 0x4f58 */
	} else if (g_scene == 27) {
		/* img 0x4f64 - the landing, flown from a recorded track.
		 * effects/flight.txt.
		 *
		 * IT RESEEDS THE SHARED RNG first, img 0x4f64 - the only
		 * reseed outside the opening - so this landscape's cloud is
		 * the same one the demo's first landscape got. */
		s32 i;

		Xcur_RndSeed();                         /* img 0x4f64 */
		g_cam.water_level = 0;                  /* img 0x4f72 */
		Xcur_PlasmaGenerate(g_texture, 0x3fff, 0x2000, 2);  /* img 0x4f91 */
		/* img 0x4f94 - a FLOOR of 0x10, not the usual 1. */
		for (i = 0; i < 0x10000; i++)
			if (g_texture[i] < 0x10) g_texture[i] = 0x10;
		Xcur_TerrainBuildShade(g_colour, g_texture,
		                       XCUR_VOX_HSCALE, XCUR_SHADE_DIV);
		g_anim_mode = 0x16;                     /* img 0x4fb5 */
		g_tgt_x = 0;                            /* img 0x4fbb */
		g_tgt_y = 0;
		g_cam.alt = 0x3000L;                    /* img 0x4fcf */
		g_obj.pitch = 0x180;                    /* img 0x4fd9 */
		g_obj.yaw = 0;
		g_obj.roll = 0;
		g_fade_div = 0;                         /* img 0x4fee */
		g_cam.hscale = XCUR_VOX_HSCALE;
	} else if (g_scene == 28) {
		/* img 0x4ffe - THE FLAG. effects/flag.txt.
		 *
		 * The landscape is drawn ONCE into the backdrop and frozen
		 * there, with the pole painted over it; Flag_Render then
		 * overlays the cloth every frame and restores the backdrop
		 * wherever the cloth is not. */
		s32 i;

		/* img 0x4ffe - the craft is PARKED here for the next two
		 * parts, and the car is placed relative to where it came
		 * down. effects/car.txt section 6. */
		g_ship_x = g_tgt_x;                     /* img 0x4ffe */
		g_car_x = g_tgt_x - 0x10000L;           /* img 0x5008 */
		g_ship_y = g_cam.alt;                   /* img 0x5013 */
		g_ship_z = g_tgt_y;
		g_car_z = g_tgt_y - 0x8000L;            /* img 0x5027 */
		g_ship_pitch = g_obj.pitch;             /* img 0x5032 */
		g_o2_pitch = 0;
		g_ship_yaw = g_obj.yaw;
		g_o2_yaw = (g_obj.yaw + 0x300) & 0xffff;  /* img 0x5049 */
		g_ship_roll = g_obj.roll;
		g_o2_roll = 0;
		Xcur_PalBuildBrownRamp(g_pal_master);   /* img 0x505f */
		g_fade = XCUR_FADE_FLAG;                /* img 0x5062 - WHITE */
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		g_warp_x = 0xdc00;                      /* img 0x506c */
		g_warp_y = 0xdc00;
		Xcur_SineFieldBias(g_water);            /* img 0x507a */
		g_cam.angle = (g_cam.angle + 0x200) & 0xffff;
		g_cam.angle = g_cam.angle;         /* img 0x50c2 */
		memset(g_bg, 0, PD_SCREEN_W * PD_SCREEN_H);
		Xcur_VoxelRender(g_bg, g_texture, g_colour, g_water, &g_cam);
		/* img 0x50d7 - THE FLAGPOLE, eight bytes of fixed gradient
		 * repeated down 176 rows. */
		for (i = 0; i < 0xb0; i++) {
			static const u8 pole[8] = {
				0x9f, 0x98, 0x90, 0x88, 0x80, 0x78, 0x70, 0x68
			};
			memcpy(g_bg + 0x1e48 + i * PD_SCREEN_W, pole, 8);
		}
		Xcur_PlasmaGenerate(g_texture, 0x3ff, 0x200, 1);  /* img 0x5105 */
		for (i = 0; i < 0x10000; i++)           /* img 0x510a */
			g_texture[i] = (u8)(((g_texture[i] >> 2) + 0x20) & 0xff);
		Xcur_TextBuildBanner(g_texture, XCUR_VISTA_REC,
		                     XCUR_FLAG_BRIGHT); /* img 0x5123 */
		memcpy(g_fb, g_bg, PD_SCREEN_W * PD_SCREEN_H);  /* img 0x5126 */
		g_anim_mode = 0x17;                     /* img 0x5130 */
		g_tick_target = XCUR_FLAG_END;          /* img 0x513d */
	} else if (g_scene == 29) {
		/* img 0x5147 - THE CAR. effects/car.txt.
		 *
		 * ITS TERRAIN IS THE LANDING'S AGAIN: the same reseed, the
		 * same generate parameters and the same floor, so the car
		 * drives the hills the craft came down on. */
		s32 i;

		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);  /* img 0x5147 */
		Xcur_RndSeed();                         /* img 0x514a */
		Xcur_PlasmaGenerate(g_texture, 0x3fff, 0x2000, 2);  /* img 0x5171 */
		for (i = 0; i < 0x10000; i++)           /* img 0x5174 */
			if (g_texture[i] < 0x10) g_texture[i] = 0x10;
		Xcur_TerrainBuildShade(g_colour, g_texture,
		                       XCUR_VOX_HSCALE, XCUR_SHADE_DIV);
		g_cam.angle = (g_cam.angle - 0x400) & 0xffff;
		Xcur_MeshNormals(&g_car, &g_mc, XCUR_MESH_ZSCALE);   /* img 0x51aa */
		/* img 0x51ba - the car is DROPPED ONTO the ground, and img
		 * 0x51d3 puts the camera's own probe into all three of the
		 * voxel camera's height words. */
		g_car_y = car_ground(g_car_x, g_car_z);
		/* img 0x51d6 - ALL THREE of the suspension's words take the
		 * same probe, and the tail pulls them apart from there. */
		g_cam_h = car_ground(g_cam_wx_src, g_cam_wz_src) & 0xffff;
		g_cam_h2 = g_cam_h;
		g_cam_h3 = g_cam_h;
		g_car_steer = XCUR_CAR_STEER0;          /* img 0x51e2 */
		g_anim_mode = 0x18;                     /* img 0x51e9 */
	} else if (g_scene == 30) {
		/* img 0x51f9 - THE END CREDITS. THE FONT IS NOT IN THE IMAGE,
		 * so Xcur_ScrollerFont's letterforms are OURS and will not
		 * match the demo. effects/scroller.txt. */
		Xcur_ScrollerBuildRows(g_srow);         /* img 0x51f9 */
		Xcur_ScrollerFont(g_sfont);
		/* img 0x51fc fills 0x4a79 with 4000 RNG words - the stream
		 * has to be spent whether or not anything reads them. */
		{
			s32 k;
			for (k = 0; k < 0xfa0; k++) (void)Xcur_RndNext();
		}
		Xcur_ScrollerSource(g_bg, g_texture, g_srow);   /* img 0x520c */
		{
			s32 k;
			for (k = 0; k < XCUR_STARS; k++) g_sprev[k] = -1;
		}
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);     /* img 0x5268 */
		Xcur_PalBuildCredits(g_pal_master);     /* img 0x5271 */
		g_anim_mode = 0x19;                     /* img 0x5274 */
		g_fade = XCUR_FADE_CREDITS;             /* img 0x527a - WHITE */
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
	} else if (g_scene == 31) {
		/* img 0x5290 - THE END. effects/theend.txt.
		 *
		 * ITS PALETTE IS BUILT IN THE PREVIOUS PART'S TAIL, img
		 * 0x5287, after Scroller_Run returns. */
		Xcur_PalBuildCyanCycle(g_pal_master);   /* img 0x5287 */
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		g_mc.tx = 0;                            /* img 0x5290 */
		g_mc.ty = 0;
		g_mc.tz = 0x20000L;
		g_obj.pitch = (s32)0xf400;              /* img 0x52ae */
		g_obj.yaw = (s32)0xf700;
		g_obj.roll = (s32)0xfa00;
		g_anim_mode = 0x1a;                     /* img 0x52c3 */
		Xcur_MeshNormals(&g_letters, &g_mc, XCUR_MESH_ZSCALE);  /* img 0x52de */
		g_cam.angle = 0;                        /* img 0x52e1 */
		g_sky_bias = XCUR_END_BIAS;             /* img 0x52ef */
		g_cam.sky_bias = XCUR_END_BIAS;
		g_cam.yofs = 0x2300;                    /* img 0x52fd */
		g_cam_yofs = 0x2300;
		g_mc.cam_yaw = 0;
		g_mc.sky_bias = XCUR_END_BIAS;
		g_mc.zscale = XCUR_MESH_ZSCALE;
		g_mc.shade_scale = XCUR_SHADE_SCALE;
	} else if (g_scene == 32) {
		/* img 0x5314 flashes white and img 0x532e SNAPS the letters
		 * square - no counter, no easing. effects/theend.txt 2. */
		g_fade = 0x4000;                        /* img 0x5314 - WHITE */
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		g_anim_mode = 0x1b;                     /* img 0x531e */
		g_mc.tz = 0x6000;                       /* img 0x5324 */
		g_obj.pitch = 0;                        /* img 0x532e */
		g_obj.yaw = 0;
		g_obj.roll = 0;
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);   /* img 0x5343 */
		Xcur_PlasmaGenerate(g_texture, 0x3fff, 0x2000, 1);  /* img 0x535f */
		end_background(g_comp, g_texture);      /* img 0x5367 */
	} else if (g_scene == 33) {
		/* img 0x5394 - the picture is what the last part left. */
		memset(g_wat_a, 0, sizeof g_wat_a);     /* img 0x5394 */
		memset(g_wat_b, 0, sizeof g_wat_b);     /* img 0x539a */
		g_wat_dst = g_wat_a;                    /* img 0x53a0 */
		g_wat_src = g_wat_b;                    /* img 0x53a7 */
		g_anim_mode = 0x1c;                     /* img 0x53ae */
		g_wat_damp = XCUR_END_DAMPING;          /* img 0x53bb */
	} else if (g_scene == XCUR_SCENE_PLAY) {
		Xcur_PlayEnter(g_pal, g_pal_master);
	} else if (g_scene == 6) {
		/* img 0x0342 - its own block, ball off.
		 *
		 * IT CLEARS THE SCREEN, img 0x039a. The palette it rebuilds
		 * is the full one, so a frame left holding the rotozoom's
		 * pixels would be shown through the land and water bands -
		 * the blue scene turning brown for a frame on its way out. */
		memset(g_fb, 0, PD_SCREEN_W * PD_SCREEN_H);
		Xcur_PalBuild(g_pal_master);
		g_fade = 0; g_fade_prev = 0;
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		g_pan = XCUR_PLANET_PAN;
		g_approach = XCUR_STARS_FAR;
		g_star_x = 0; g_star_y = 0; g_star_z = 0;
		g_obj.pitch = 0; g_obj.yaw = 0; g_obj.roll = 0;
		g_mc.tx = 0; g_mc.ty = 0; g_mc.tz = XCUR_STARS_TZ;
		g_mc.pitch = 0; g_mc.yaw = 0; g_mc.roll = 0; g_mc.cam_yaw = 0;
		/* IT DOES NOT TOUCH g_SkyBias, and that matters for every
		 * mesh from here to the end of Demo_Entry. The only writers
		 * are img 0x0292, the planet part, and Voxel_LatchCamera -
		 * which no part after the landscape calls. So the 0x1c the
		 * planet part set carries through, and Mesh_TransformVerts
		 * adds it to a y origin of 0x48: zeroing it here renders
		 * every craft 28 pixels high, which is also why a muzzle
		 * flash at the glow's fixed box then lands on nothing. */
		g_mc.light_pitch = 0;
		g_mc.light_yaw = XCUR_LIGHT_YAW;
		g_mc.light_roll = 0;
	} else if (g_scene == 2) {
		/* The fill writes the same 0x60..0x9f band as the opening -
		 * only the palette under it changes. img 0x3e8d. */
		Xcur_PalBuildGreen(g_pal_master);
		Xcur_PalFade(g_pal, g_pal_master, &g_fade, &g_fade_prev);
		g_cap_rows = Xcur_TextBuildMask(g_mask,
		        (u32)(XCUR_FIRETEXT_RECS
		              + XCUR_BANNER_REC * XCUR_REC_BYTES),
		        &g_cap_ofs);
		g_thr = THR_HI;
	} else {
		g_cap_no = 0;
		caption();
	}
	g_seeking = was_seeking;
}

/* OURS - a frame owes 10/7 of a tick; this runs a handler for as
 * many as this frame got. subsystems/animation.txt 1.1. */
static void ticks_run(void (*fn)(void))
{
	s32 t;

	for (t = 0; t < g_ticks; t++) fn();
}

/* OURS */
void Demo_Frame(void)
{
	if (!g_img) return;
	g_frame++;

	Xcur_LookTick();

	/* TICKS RUN AT 100 Hz WHATEVER THE FRAME RATE IS. The demo's loop
	 * waits only on the timer flag - never on a retrace - so a slow
	 * machine loses FRAMES and keeps its ticks, and every per-frame
	 * effect is machine speed. XCUR_FRAME_HZ is the machine we model.
	 *
	 * The accumulator spends the remainder rather than rounding it
	 * away. The sequencer halves the rate of its own accord. */
	g_tick_acc += XCUR_TICK_HZ;
	for (g_ticks = 0; g_tick_acc >= XCUR_FRAME_HZ; g_ticks++) {
		g_tick_acc -= XCUR_FRAME_HZ;
		Xcur_SeqTick();
	}
	switch (g_scene) {
	case 0:  ticks_run(anim_tick0); scene_opening();        break;
	case 1:  ticks_run(anim_tick1); scene_countdown();      break;
	case 2:  scene_approach();                              break;
	case 3:  scene_launch();                                break;
	case 4:  scene_planet();                                break;
	case 6:  scene_stars();                                 break;
	case 8:  scene_warp();                                  break;
	case 9:  scene_moire();                                 break;
	case 10:
	case 11: scene_chase();                                 break;
	case 12:
	case 13:
	case 14: scene_battle();                                break;
	case 15:
	case 16: scene_aftermath();                             break;
	case 17: scene_rotoslow();                              break;
	case 18:
	case 19: scene_beams();                                 break;
	case 20:
	case 21: scene_tunnel();                                break;
	case 22: scene_water();                                 break;
	case 23: scene_tunnel();                                break;
	case 24: scene_space();                                 break;
	case 25: scene_moon();                                  break;
	case 26: scene_rotognd();                               break;
	case 27: scene_landing();                               break;
	case 28: scene_flag();                                  break;
	case 29: scene_car();                                   break;
	case 30: scene_credits();                               break;
	case 31: scene_theend();                                break;
	case 32: scene_endplasma();                             break;
	case 33: scene_endwater();                              break;
	case XCUR_SCENE_PLAY: Xcur_PlayFrame(g_fb, g_ticks);    break;
	default: scene_roto();                                  break;
	}
}

/* OURS */
const u8 *Demo_Pixels(s32 *w, s32 *h)
{
	if (w) *w = PD_SCREEN_W;
	if (h) *h = PD_SCREEN_H;
	return g_fb;
}

const u8 *Demo_Palette(void) { return g_pal; }

/* OURS */
void Demo_Audio(s16 *out, s32 frames)
{
	Xcur_MusicRender(out, frames);
}

/* OURS */
const u8 *Demo_Image(u32 *len)
{
	if (len) *len = g_image_len;
	return g_img;
}

/* OURS */
void Demo_Mouse(s32 x, s32 y, s32 buttons)
{
	Xcur_LookMouse(x, y, buttons);
}

/* OURS, and the demo's own structure is what makes it honest. Every
 * part is a parameter block, a music-position target and one runner
 * that loops until the music reaches it - effects/script1_landscape
 * .txt section 1 - so a part IS its sync value, and seeking to one
 * means running the module to that order.
 *
 * REPLAYED FROM TICK ZERO, NOT JUMPED TO. The channel volumes that
 * fly the camera are the sum of the whole module so far, so landing
 * on a part with the right state means replaying into it. Drawing is
 * suppressed, which leaves only integer work and makes the replay
 * quick enough to be instant either way, forward or back. */
static void seek_scene(s32 which)
{
	static const s32 sync[XCUR_SCENES] = {
		0, XCUR_SYNC_COUNTDOWN, XCUR_SYNC_APPROACH,
		XCUR_SYNC_LAUNCH, XCUR_SYNC_PLANET, XCUR_SYNC_ROTO,
		XCUR_SYNC_STARS, XCUR_SYNC_STARS, XCUR_SYNC_ROTO2,
		XCUR_SYNC_WARP, XCUR_SYNC_MOIRE, XCUR_SYNC_CHASE1,
		XCUR_SYNC_CHASE2, XCUR_SYNC_FIGHT1, XCUR_SYNC_FIGHT2,
		XCUR_SYNC_FIGHT3, XCUR_SYNC_AFTER, XCUR_SYNC_WARPOUT,
		XCUR_SYNC_ROTOSLOW, XCUR_SYNC_BEAMS1, XCUR_SYNC_BEAMS2,
		XCUR_SYNC_TUNNEL, XCUR_SYNC_TUNNEL2, XCUR_SYNC_WATER,
		XCUR_SYNC_EXIT, XCUR_SYNC_SPACE, XCUR_SYNC_MOON,
		XCUR_SYNC_ROTOGND, XCUR_SYNC_FLIGHT, XCUR_SYNC_FLIGHT,
		XCUR_SYNC_CAR, XCUR_SYNC_CREDITS, XCUR_SYNC_THEEND,
		XCUR_SYNC_ENDPLASMA, XCUR_SYNC_ENDWATER
	};
	s32 target = ((which % XCUR_SCENES) + XCUR_SCENES) % XCUR_SCENES;
	s32 order = 0, guard = 0;

	demo_restart();
	if (target == 0) return;
	if (target == XCUR_SCENE_PLAY) { scene_enter(target); return; }

	g_seeking = 1;
	while (guard++ < 20000) {
		Demo_Frame();
		Xcur_SeqPos(&order, (s32 *)0);
		if (order >= sync[target] && g_scene >= target) break;
	}
	g_seeking = 0;
}

/* OURS - src/agent_host.h, and main_ppm.c -scene reaches it.
 *
 * IT STEPS RATHER THAN SEEKING STRAIGHT THERE. seek_scene restarts
 * and replays, but a restart is not a clean slate - demo_restart
 * leaves the voxel colour map and the glow buffers holding what the
 * last run put there, deliberately, because the original does. So
 * seek_scene(n) once and the n presses that used to reach it give
 * different frames for five scenes in eight. Stepping is what the
 * bench did before this function existed, so stepping is what keeps
 * its output comparable. */
void Dbg_GotoPart(s32 i)
{
	s32 guard = 0;

	while (g_scene != i && ++guard <= XCUR_SCENES)
		Demo_Key(']', 1);
}

/* OURS */
void Demo_Key(int ch, int down)
{
	if (g_scene == XCUR_SCENE_PLAY && Xcur_PlayKey(ch, down)) return;
	if (!down) return;
	if (ch == ']') seek_scene(g_scene + 1);
	else if (ch == '[') seek_scene(g_scene - 1);
}
