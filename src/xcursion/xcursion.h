/* XCURSION - the modules behind Demo_*.
 *
 * NO DISPATCHER TO MIRROR. UKKO has one frame loop with a keyframe
 * cursor; XCURSION does not - Demo_Entry runs the effects one after
 * another as straight-line code, so the effect list IS the function
 * - docs/xcursion/README.txt section 5. The scene table in
 * xcursion_demo.c is therefore OURS, a reading of that straight
 * line, and not a structure the demo has.
 */

#ifndef XCURSION_H
#define XCURSION_H

#include "../pd.h"
#include "xcursion_assets.h"

/* The depacked image, shared by every module. Set by Demo_Init. */
extern const u8 *g_img;
extern u32 g_image_len;

/* g_SinTable as the code reads it: a BYTE offset, masked 0x7fc.
 * xcursion_tables.c */
s32 Xcur_Sin(s32 byte_off);
s32 Xcur_Cos(s32 byte_off);
/* The same table read one byte in - effects/moire.txt section 4. */
s32 Xcur_Sin5(s32 byte_off);

/* The palette the demo builds, not one read from the image -
 * xcursion_pal.c. */
void Xcur_PalBuild(u8 *pal768);
/* ORIGIN img 0x410a - the generator the plasma and the starfield
 * share. Its seeds are fixed in the image, so both are identical on
 * every run. */
void Xcur_RndSeed(void);
u16  Xcur_RndNext(void);
void Xcur_RndSave(void);
void Xcur_RndRestore(void);
extern s32 g_plasma_c0, g_plasma_c1;

/* xcursion_planet.c - effects/planet.txt. */
#define XCUR_STARS      1000
#define XCUR_SPHERE_DIM 0x80
#define XCUR_SPHERE_OUT 0xf380
typedef struct { s32 x, y, z; } XcurPoint;
void Xcur_PointsMake(XcurPoint *pts, s32 n);
void Xcur_PointsRenderErase(u8 *fb, const XcurPoint *pts, s32 *prev,
                            s32 n, s32 cx, s32 cy, s32 cz);
void Xcur_PointsRender(u8 *fb, const XcurPoint *pts, s32 n,
                       s32 cx, s32 cy, s32 cz);
void Xcur_PlanetBuildMap(u16 *table);
void Xcur_PlanetRender(u8 *fb, const u16 *table, const u8 *texture,
                       s32 pan, s32 approach);

/* xcursion_ground.c - the second landscape. effects/ground.txt. */
void Xcur_GroundRender(u8 *work, const u8 *shade, const u8 *water,
                       s32 cam_x, s32 cam_z, s32 cam_angle,
                       s32 cam_scale, s32 ramp);

void Xcur_PalBuildGreen(u8 *pal);
void Xcur_PalBuildBlue(u8 *pal);
void Xcur_PalBuildBluePurple(u8 *pal);
void Xcur_PalBuildWarp(u8 *pal);
void Xcur_PalBuildRgbRamps(u8 *pal, s32 *phase);
void Xcur_PalBuildBlueMagenta(u8 *pal);
void Xcur_PalCycleBands(u8 *pal);
void Xcur_PalBuildGreyLow(u8 *pal);
void Xcur_PalBuildBrownRamp(u8 *pal);
void Xcur_PalBuildCredits(u8 *pal);
void Xcur_PalBuildCyanCycle(u8 *pal);
void Xcur_PalSetRgbRamps(u8 *pal);
void Xcur_PalCycle(u8 *pal);

/* xcursion_warp.c - effects/warp.txt. Six animated offsets. */
#define XCUR_WARP_OFS 6
void Xcur_BufClampMin1(u8 *buf);
void Xcur_WarpRenderFull(u8 *fb, const u8 *field, const s32 *ofs);

/* xcursion_beams.c - effects/beams.txt. NOT a rotozoom: the screen is
 * its own state and there is no texture. */
void Xcur_BeamsRender(u8 *fb, s32 zoom, s32 angle);

/* xcursion_water.c - effects/water.txt. A 2D wave equation in eleven
 * instructions, drawn as column spans. The grid is 160 x 100 SIGNED
 * WORDS and both buffers are padded, because the solver runs a row
 * past the end and reads a row past that. */
#define XCUR_WATER_WORDS 16384
void Xcur_WaterStep(s16 *dst, const s16 *src, s32 damping);
void Xcur_WaterRenderSpans(u8 *fb, const s16 *field);
/* ORIGIN img 0x2a99 - `src` must be a whole 64 KB segment: the wrap
 * at img 0x2ac7 lets it read past 64000. effects/theend.txt 4. */
void Xcur_WaterRefract(u8 *fb, const u8 *src, const s16 *field);

/* xcursion_moire.c - effects/moire.txt. Its own runner in the binary,
 * so it returns the advanced phase rather than taking a state block. */
s32 Xcur_MoireRender(u8 *fb, u8 *field, s32 phase);

/* xcursion_roto.c - effects/roto.txt. */
void Xcur_TextBuildBanner(u8 *dest, u32 rec, s32 bright);
void Xcur_FlagRender(u8 *fb, const u8 *back, const u8 *text,
                     const u8 *field, s32 ofs_x, s32 ofs_y);
void Xcur_RotoRender(u8 *fb, u8 *accum, const u8 *texture,
                     s32 zoom, s32 angle, s32 phase, s32 shift);
void Xcur_PalFade(u8 *out, const u8 *master, s32 *level, s32 *prev);

/* Diamond-square, 0x10000 bytes out - xcursion_plasma.c. */
void Xcur_PlasmaGenerate(u8 *buf, s32 mask, s32 bias, s32 rough);

/* A caption's mask, out of the font and one 6-byte record. Returns
 * its row count and writes its start offset - xcursion_text.c. */
s32 Xcur_TextBuildMask(u8 *mask, u32 rec, s32 *ofs);
void Xcur_FireTextFill(u8 *fb, const u8 *mask, const u8 *texture,
                       s32 tex_row_base, s32 threshold,
                       s32 ofs, s32 rows);

/* Everything Voxel_Render reads, and every field of it is a global
 * the demo keeps - effects/voxel.txt section 8.1. */
typedef struct {
	s32 wx, wy;                 /* img 0x94d8, 0x94dc - 32-bit     */
	s32 angle;                  /* img 0x9973                      */
	s32 scale;                  /* img 0x9975                      */
	s32 hscale;                 /* img 0x986d g_VoxHeightScale     */
	s32 yofs;                   /* img 0x94f0                      */
	s32 water_level;            /* img 0xa598; zero disables it    */
	s32 water_scroll;           /* img 0x997d                      */
	s32 alt;                    /* img 0x9460 - the altitude term  */
	s32 sky_bias;               /* img 0x996d, from img 0x98f5 */
	s32 sky_phase;              /* img 0xa87e - dithers the sky */
} XcurCam;

/* The three phases the camera path runs off - img 0x98f9, 0x98fb,
 * 0x98fd, all zero at the start. */
typedef struct { s32 a, b, c; } XcurCamPhase;

/* xcursion_voxel.c - the landscape and the two fields it needs that
 * are not generator output. */
void Xcur_CamStep(XcurCam *cam, XcurCamPhase *ph, s32 speed);
/* The three object angles mode 2 and mode 3 animate - img 0x9909,
 * img 0x9881 and img 0x990b. */
typedef struct { s32 pitch, yaw, roll; } XcurObj;

void Xcur_CamEase(XcurCam *cam, s32 tgt_x, s32 tgt_y);
void Xcur_CamFlight(XcurCam *cam, XcurObj *obj, s32 *tgt_x, s32 *tgt_y);
void Xcur_TerrainBuildShade(u8 *colour, const u8 *height,
                            s32 hscale, s32 divisor);
void Xcur_WaterFieldBuild(u8 *field);
void Xcur_SkyBuild(void);

/* xcursion_scroll.c - the end credits. effects/scroller.txt. */
typedef struct { s32 dist, step, start, width, xoff; } XcurScrollRow;
void Xcur_ScrollerBuildRows(XcurScrollRow *rows);
void Xcur_ScrollerFont(u8 *font);
void Xcur_ScrollerRender(u8 *work, const u8 *source, const u8 *font,
                         const XcurScrollRow *rows, s32 scroll_pos);
void Xcur_ScrollerSource(u8 *dest, const u8 *plasma,
                         const XcurScrollRow *rows);
void Xcur_SineFieldBias(u8 *field);
void Xcur_TerrainTintWater(u8 *colour, const u8 *height);
s32 Xcur_TerrainHeightAt(const u8 *height, const u8 *water,
                         s32 x, s32 z, s32 water_level,
                         s32 water_scroll, s32 hscale,
                         s32 *kind);

void Xcur_VoxelRender(u8 *fb, const u8 *height, const u8 *colour,
                      const u8 *water, const XcurCam *cam);

/* The object renderer - formats/mesh.txt. The largest model is 120
 * vertices and 94 faces. */
#define XCUR_MESH_MAXV 128
#define XCUR_MESH_MAXF 128

typedef struct {
	s32 nvert, nface;
	s8  vx[XCUR_MESH_MAXV], vy[XCUR_MESH_MAXV], vz[XCUR_MESH_MAXV];
	u8  fcnt[XCUR_MESH_MAXF], fattr[XCUR_MESH_MAXF];
	s32 fidx[XCUR_MESH_MAXF][4];
	/* The face normals, computed ONCE in the reference pose - img
	 * 0x43d4. The demo keeps them in the vertex segment at 0x8000. */
	s32 fnx[XCUR_MESH_MAXF], fny[XCUR_MESH_MAXF], fnz[XCUR_MESH_MAXF];
} XcurModel;

/* g_SpanBuf and the min/max pair that bounds it - img 0x4b4e. Reused
 * across faces; only the touched rows are cleared. */
typedef struct {
	s32 left[PD_SCREEN_H], right[PD_SCREEN_H];
	s32 sl[PD_SCREEN_H], sr[PD_SCREEN_H];
	s32 lo, hi;
} XcurSpan;

typedef struct {
	s32 pitch, yaw, roll;       /* g_ObjPitch / Yaw / Roll          */
	s32 cam_yaw;                /* g_CamAngleNext, added to yaw     */
	s32 tx, ty, tz;             /* g_ObjTransX / Y / Z              */
	s32 zscale;                 /* g_MeshZScale                     */
	s32 sky_bias;               /* g_SkyBias                        */
	s32 light_pitch, light_yaw, light_roll;
	s32 shade_scale;            /* g_ShadeScale                     */
} XcurMeshCam;

void Xcur_VecRotate3(s32 *px, s32 *py, s32 *pz,
                     s32 pitch, s32 yaw, s32 roll);
int  Xcur_MeshParse(XcurModel *m, u32 base);
void Xcur_MeshNormals(XcurModel *m, XcurMeshCam *c, s32 zscale);
void Xcur_SpanReset(XcurSpan *b);
s32  Xcur_MeshSetup(XcurMeshCam *o, s32 cam_x, s32 cam_y, s32 cam_angle,
                    s32 scale, s32 tgt_x, s32 tgt_y, s32 y_term);
s32  Xcur_MeshDraw(u8 *fb, const XcurModel *m, const XcurMeshCam *c,
                   XcurSpan *b);

/* xcursion_battle.c - effects/battle.txt.
 *
 * XCUR_GLOWMEM stands for segment 0x4a79 from offset 0x1800, with the
 * two 128x128 buffers where the demo puts them. The routines write
 * past the buffer ends and the offsets are what decides where those
 * land, so the layout is load-bearing, not a convenience. */
#define XCUR_SHOTS       8
#define XCUR_SPARKS      0x20
#define XCUR_SPARK_LIFE  0x20          /* img 0xa59b's static */
#define XCUR_SPARK_SHIFT 6             /* img 0xa59a's static */
#define XCUR_WARP_LIFE   0x80          /* img 0x1587 */
#define XCUR_WARP_SHIFT  8             /* img 0x158d */
#define XCUR_GLOWCLR     0x82a0        /* img 0x27a3, 0x20a8 dwords */
#define XCUR_GLOW_A      0x0080        /* img 0x98b1, 0x1880 */
#define XCUR_GLOW_B      0x4100        /* img 0x98b3, 0x5900 */
#define XCUR_GLOW_BOX_A  0x2d60        /* img 0x2671, modes 9 and 0xa */
#define XCUR_GLOW_BOX_B  0x009c        /* img 0x2682, mode 0xb */

typedef struct { s32 x, y, dx, dy, life; } XcurSpark;
typedef struct { s32 x, y, z, pitch, yaw, roll; } XcurShot;

void Xcur_GlowSpawnStep(u8 *mem, s32 src, XcurSpark *sp);
void Xcur_GlowBurst(XcurSpark *sp, s32 life, s32 shift);
void Xcur_GlowClear(u8 *mem, XcurSpark *sp);
s32  Xcur_GlowRender(u8 *fb, u8 *mem, s32 src, s32 dst, s32 bx);
void Xcur_ShotsDraw(u8 *fb, const XcurShot *sh, const XcurModel *m,
                    XcurMeshCam *c, XcurSpan *b);
s32  Xcur_ShotsStep(XcurShot *sh, s32 mode, s32 bx, s32 by, s32 bz);

/* xcursion_tunnel.c - effects/tunnel.txt. Two full-screen tables feed
 * the two halves of one texture index. */
void Xcur_TunnelBuildDepth(u8 *depth, u8 *depth8);
void Xcur_PolyFillFlat(u8 *dest, const s32 *vx, const s32 *vy,
                       s32 colour, XcurSpan *b);
void Xcur_TunnelBuildAngle(u8 *angle, XcurSpan *b);
s32  Xcur_TunnelRender(u8 *fb, const u8 *texture, const u8 *angle,
                       const u8 *depth, const u8 *depth8, s32 scroll,
                       s32 depth_hi, s32 parity, s32 order, s32 fade);

/* One countdown particle - the demo's 11-byte record, img 0x3887.
 * 0x1000 of them, and the biggest glyph spawns 2321. */
#define XCUR_COUNTDOWN_MAX 0x1000

typedef struct {
	s32 life;                   /* 0xf8 on spawn, 0 is empty     */
	s32 x, y;                   /* 1/16 pixel                    */
	s32 vx, vy;
	s32 last;                   /* the offset last drawn, to undo */
} XcurParticle;

s32  Xcur_CountdownSpawn(XcurParticle *p, s32 max, s32 window);
void Xcur_CountdownStep(u8 *fb, const u8 *bg, XcurParticle *p, s32 n,
                        const u8 *field, s32 heat,
                        s32 ofs_x, s32 ofs_y);

/* img 0x3975 reaches 64317, past the 64000 a mode 13h screen shows.
 * The tail is the segment's - countdown.txt section 3. */
#define XCUR_FB_BYTES (PD_SCREEN_W * PD_SCREEN_H + 0x200)

/* xcursion_seq.c - the module sequencer's channel state. The demo
 * tracks eight slots; img 0x0d21 drops the rest. */
#define XCUR_SEQ_CHANNELS 8
void Xcur_SeqInit(u32 mod);
void Xcur_SeqTick(void);
s32  Xcur_SeqCamSpeed(void);
void Xcur_SeqPos(s32 *order, s32 *row);
s32  Xcur_SeqRowCount(void);
s32  Xcur_SeqTakeFlash(void);
s32  Xcur_SeqTakeFire(void);
s32  Xcur_SeqTakeNewPattern(void);
s32  Xcur_SeqChanValue(s32 ch);
/* img 0x9388 and img 0x93c8 - a channel's note age and its sample
 * step. Nothing but mode 0x12 reads either, so they exist to drive
 * the water's drips - effects/water.txt section 8. */
s32  Xcur_SeqChanAge(s32 ch);
void Xcur_SeqChanAgeDec(s32 ch);
s32  Xcur_SeqChanStep(s32 ch);

void Xcur_MusicInit(s32 rate);
void Xcur_MusicRender(s16 *out, s32 frames);
void Xcur_MusicShutdown(void);

#endif
