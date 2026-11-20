/* LITHIUM - the reconstruction's own interface.
 *
 * One C function per function in the disassembly, carrying the name
 * reverse/exports/LITHIUM_U.EXE/function_map.txt uses. Nothing here
 * is a host concern: the framebuffer and the palette are 8-bit
 * indexed exactly as mode 13h leaves them, and src/main_*.c resolves
 * them - src/host.h.
 *
 * What is known about this demo is docs/lithium/, and the Python
 * reconstruction the C follows is sim/lithium/.
 */

#ifndef LITHIUM_H
#define LITHIUM_H

#include "../pd.h"
#include "lithium_assets.h"

/* Mode 13h, and the one INT 10h AX=0013 at img 0x0188 in Demo_Main
 * is the only place it is ever set - README.txt section 5. */
#define LITH_FB_BYTES (PD_SCREEN_W * PD_SCREEN_H)

/* A WHOLE REAL-MODE SEGMENT. The bump field's offsets are 16-bit and
 * wrap, the relaxation reads a row past the last it writes and the
 * stamp's brush overhangs - so these buffers are 64 KB, not 320x200.
 * effects/bump.txt. */
#define LITH_SEG_BYTES   0x10000

/* The dust pool: 1000 records of 12 bytes at 0x2ad2:0x3820 - three
 * point pairs, drawn then mid then target. effects/dust.txt 1. */
#define LITH_DUST_COUNT  1000
#define LITH_DUST_REC    12

/* THE MODULE ASKS FOR THIS - every pattern carries T144 on row 0 and
 * the tempo formula is param * 2 / 5. Measured three ways, all in
 * subsystems/music.txt section 5.1. */
#define LITH_TICK_HZ  57

/* TICKS PER RENDERED FRAME - the crossfade steps a quarter per FRAME.
 * Measured - effects/rockintro.txt 7.1. */
#define LITH_FRAME_TICKS 3

/* The depacked image, for anything that reads its own data out of
 * it. Valid between Demo_Init and Demo_Shutdown. */
extern const u8 *g_lith_img;

/* ORIGIN img 0x3dcc Pal_Upload - 256 DAC entries from a stored
 * palette, biased by the fade. Here the DAC is the 768 bytes
 * src/main_*.c reads. The fade is in DAC units TIMES FOUR:
 * -128 gives black, +256 white, 0 the palette as stored. */
void Lith_PalUpload(u8 *dac, const u8 *store, s32 fade);

/* THE CYCLING. Rebuilds entries 0 to 128 as three linear ramps whose
 * slopes are animated. IT WRITES THE STORE, NOT THE DAC - the
 * original's destination is img 0x96be, which is why LITH_PAL_MAIN
 * reads as zeros in the image - so only Lith_PalUpload reaches the
 * DAC and only it applies the fade. Lith_PalSelect stands in for the
 * pointer the original keeps at img 0x8f8c. */
void Lith_PalAnimate(u8 *store);
u8  *Lith_PalStore(void);
void Lith_PalSelect(u32 src);
void Lith_PalRefresh(void);
void Lith_PalPhaseSeed(u16 r, u16 g, u16 b);
void Lith_PalPhaseAdd(u16 r, u16 g, u16 b);
void Lith_PalRampShift(s32 n);

/* X86 IDIV TRUNCATES TOWARD ZERO and C89's / is implementation
 * defined for negatives, so the rounding is spelled out. It is not
 * cosmetic: every negative normal component goes through it. */
s32 lith_idiv(s32 a, s32 b);
/* ((s64)a << 8) / b - the projections divide a 64-BIT dividend. */
s32 lith_idiv_shl8(s32 a, s32 b);

/* ------------------------------------------------------------ tables */

/* The image stores a QUARTER turn - 512 signed words rising 0 to
 * 4096, so 4096 is 1.0 - and the built table is four quadrants plus a
 * 512-entry wrap tail the cosine reads into. lithium_tables.c. */
#define LITH_SIN_QUARTER   512
#define LITH_SIN_ENTRIES   2560
#define LITH_SIN_PERIOD    2048

void Lith_SinTableBuild(void);

/* The angle is masked 0x1ffc: a BYTE offset into dwords, so 2048
 * units to the circle and the low two bits are discarded. */
s32 Lith_Sin(s32 ang);
s32 Lith_Cos(s32 ang);
const s32 *Lith_SinTable(void);

/* -------------------------------------------------------------- mesh */

/* The tower is the largest at 664 vertices and 1,323 faces -
 * formats/mesh.txt section 4. */
#define LITH_MESH_MAXV     1024
#define LITH_MESH_MAXF     1536

/* THE SCENE POOL IS NOT ONE MESH. Terrain_Build emits into the same
 * pool as the models and its count follows the camera's footprint,
 * so the pool has to hold the worst case of both together.
 *
 * The footprint is a square about (0x80, 0x80) whose half-diagonal
 * is bounded by the arithmetic at img 0x4bae: the pitch term is at
 * most (0x1000 + 0x1140) >> 7 = 66 and each offset is a sine of that
 * over 1 << 0xe, so 16 either way - a 64-row, 66-column grid at
 * worst, which is 4,290 vertices and 8,320 faces. Measured 336 to
 * 391 and 600 to 684 at the flyover's camera pitch, so the bound
 * is loose and the pool is sized for it anyway.
 * effects/terrain.txt section 1. */
#define LITH_POOL_MAXV     5120
#define LITH_POOL_MAXF     10240

/* ONE MESH, AS Mesh_Load LEAVES IT. The original keeps the faces only
 * as scratch and overwrites them with the normals; we keep both,
 * because the renderer wants the faces and nothing is gained by
 * reading the file twice. */
typedef struct {
	s32 nvert, nface;
	s32 vx[LITH_MESH_MAXV], vy[LITH_MESH_MAXV], vz[LITH_MESH_MAXV];
	s32 nxv[LITH_MESH_MAXV], nyv[LITH_MESH_MAXV], nzv[LITH_MESH_MAXV];
	s32 f0[LITH_MESH_MAXF], f1[LITH_MESH_MAXF], f2[LITH_MESH_MAXF];
} LithMesh;

/* src_off is the g_MeshSrcOff the demo stores, relative to the asset
 * base. Non-zero on a bad header or an out-of-range index. */
int Lith_MeshLoad(LithMesh *m, u32 src_off);

/* g_MeshSrcOff for the opening's model: 130 vertices, 256 faces. */
#define LITH_MESH_ROCK     0x41eaUL

extern LithMesh g_lith_rock;

/* ------------------------------------------------------------- scene */

/* g_ObjScaleShift = 16 IS THE IDENTITY and it is derived, not read:
 * three rotations by zero multiply a coordinate by 4096 three times
 * and shift it back by 0xa twice, so a shift of 16 returns the
 * authored byte exactly. Verified against all five models. */
#define LITH_SCALE_IDENTITY  16

/* WHAT img 0x4e33 AND 0x4e44 ADD BACK IS THE CAMERA'S POSITION, and
 * it is what makes the placement relative - subsystems/mesh3d.txt
 * 3.2. The Z form's neutral value is -1, not 0. */
#define LITH_REBASE_Z_NEUTRAL (-1)

#define LITH_NEAR_Z     0x7d        /* the near-plane CLAMP          */
#define LITH_CENTRE_X   0xa0
#define LITH_CENTRE_Y   0x64
#define LITH_GUARD      0x800       /* clamped BEFORE the centring   */
#define LITH_FADE_BIAS  0x28000L    /* where the env fade starts     */
#define LITH_FAR_Z      0xd9a80L    /* the baked reject on summed z  */

/* What Mesh_Transform writes: the 16-byte record's five words. */
typedef struct { s32 x, y, z, u, v; } LithVert;

/* What Scene_ProjectVerts gives back. wx and wz are the pre-camera
 * x and z the original stashes at +0x0c and +0x0e. */
typedef struct { s32 sx, sy, z, u, v, wx, wz; } LithProj;

typedef struct { s32 key, face; } LithSortEnt;

/* ONE MODEL AT A TIME. The original appends into a shared pool and
 * rebases the face indices; nothing here needs that yet, so a scene
 * holds a single transformed model. */
typedef struct {
	s32 nvert, nface;
	LithVert v[LITH_POOL_MAXV];
	s32 f0[LITH_POOL_MAXF], f1[LITH_POOL_MAXF], f2[LITH_POOL_MAXF];
	s32 attr[LITH_POOL_MAXF];
} LithScene;

/* g_ObjAngle*, g_CurPos* and the flags one call needs. */
typedef struct {
	s32 ang_x, ang_y, ang_z;
	s32 pos_x, pos_y, pos_z;
	s32 scale_shift;
	s32 env_angle;
	int mirror, axis_swap;
	s32 rebase_x, rebase_z;
} LithObj;

void Lith_VecRotZ(s32 *x, s32 *y, s32 *z, s32 ang);
void Lith_VecRotX(s32 *x, s32 *y, s32 *z, s32 ang);
void Lith_VecRotY(s32 *x, s32 *y, s32 *z, s32 ang);

void Lith_ObjInit(LithObj *o);
void Lith_SceneReset(LithScene *sc);
void Lith_MeshTransform(LithScene *sc, const LithMesh *m, const LithObj *o);
void Lith_ObjFaceAttr(s32 attr);
s32  Lith_FaceSegment(s32 attr);
s32  Lith_FaceBand(s32 attr);
void Lith_SceneProject(LithProj *out, const LithScene *sc,
                       const s32 cam_pos[3], const s32 cam_ang[3]);
s32  Lith_SceneCullSort(LithSortEnt *out, const LithProj *pv,
                        const LithScene *sc, s32 depth_near);
void Lith_SceneDraw(u8 *fb, const u8 *tex, LithScene *sc,
                    LithProj *pv, LithSortEnt *sl,
                    const s32 cp[3], const s32 ca[3], s32 depth_near);

extern LithScene g_lith_scene;
/* THE POOL, NOT ONE MESH. The flyover appends the terrain and three
 * models into one pool - img 0x1a73 - so anything downstream of
 * Mesh_Transform has to be sized for the sum and not for the
 * largest model. Sized for a mesh, the tower overflows the bound in
 * Mesh_Transform and is dropped without a trace. */
extern LithProj g_lith_proj[LITH_POOL_MAXV];
extern LithSortEnt g_lith_sort[LITH_POOL_MAXF];
extern s32 g_lith_nsort;

/* ------------------------------------------------------------ raster */

#define LITH_SPAN_ROWS  200

/* g_ObjFaceAttr is a TEXTURE SEGMENT unless it is 0 or 1, which both
 * mean the sphere map with a band added - img 0x338d. */
#define LITH_ATTR_SPHERE 0x6ad2
#define LITH_ATTR_GLOW   0x5ad2

/* The band offsets the span routines add. Attributes 0 and 1 read the
 * SAME map and land in different 64-entry stretches of the palette,
 * which is why the palette is four rising ramps - raster.txt 4. */
#define LITH_BAND_NONE  0x00
#define LITH_BAND_40    0x40
#define LITH_BAND_C0    0xc0

/* g_PolyArgs. The fourth slot the original fills itself by copying
 * vertex 0 is left out: a triangle is all the mesh path ever sends. */
typedef struct {
	s32 x[3], y[3];
	s32 u[3], v[3];
} LithPoly;

void Lith_PolyReset(void);
void Lith_PolyFill(u8 *dst, const u8 *tex, const LithPoly *p, s32 band);
void Lith_PolyFillShadow(u8 *dst, const u8 *sten, const LithPoly *p);

/* ----------------------------------------------------------- texture */

/* img 0x8f20 - the generator's seed, STORED IN THE IMAGE. */
#define LITH_RND_SEED   0x08f20UL

/* g_RndStateB0 and B1. The image carries the SAME pair here as it
 * does for the main generator, and nothing in the demo but Rnd_NextB
 * ever touches these - so this is a loaded value, not a seed anyone
 * chose. effects/bump.txt section 6. */
#define LITH_RNDB_SEED  0x08fc7UL
/* img 0x9f31 - Tex_Plot's 16x16 brush, a disc of 1s. */
#define LITH_BRUSH      0x09f31UL
/* 256 x 256, a whole real-mode segment, so u and v wrap for free. */
#define LITH_TEX_BYTES  0x10000

void Lith_RndReset(void);
void Lith_RndSeed(u16 a, u16 b);
u16  Lith_RndNext(void);
void Lith_RndGet(u16 *a, u16 *b);
void Lith_RndNextPair(u16 *rot, u16 *dx);
void Lith_RndBReset(void);
void Lith_RndBSeed(u16 a, u16 b);
u16  Lith_RndNextB(void);
void Lith_TexBlur(u8 *buf, s32 times);
void Lith_TexBuildSphereMap(u8 *buf);

extern u8 g_lith_tex[LITH_TEX_BYTES];

/* --------------------------------------------------------- rockintro */

/* g_DepthNear, set at img 0x1532 before the loop. */
#define LITH_ROCKINTRO_DEPTH_NEAR  0x186aL

void Lith_RockIntroEnter(void);
void Lith_RockIntroTick(void);
void Lith_RockIntroFrame(u8 *fb, const LithMesh *rock, const u8 *tex,
                         LithScene *sc, LithProj *pv, LithSortEnt *sl);
s32  Lith_RockIntroSlot8Z(void);
s32  Lith_RockIntroZoom(void);

/* img 0x16bf and 0x16c9 - THE RUNNER NEEDS BOTH. It loops while the
 * module is short of order 4 OR the fade has not yet passed -160, so
 * a part that only counted orders would never leave. */
s32  Lith_RockIntroDone(void);

/* ------------------------------------------------------- the clock */

/* lithium_music.c - a clock, not a replayer. */
void Lith_MusicStart(void);
void Lith_MusicSeek(s32 tick);
void Lith_MusicSeekOrder(s32 order);
void Lith_MusicSeekAudio(s32 tick);
void Lith_MusicAdvanceTicks(s32 n);
s32  Lith_MusicEndTick(void);
s32  Lith_MusicPos(void);
s32  Lith_MusicTicks(void);
s32  Lith_MusicSeqPos(void);
s32  Lith_MusicRow(void);
s32  Lith_MusicPosLast(void);
void Lith_MusicPosLatch(void);
s32  Lith_MusicPosChanged(void);
/* img 0x8007 - the image holds the samples as 3-bit ADPCM. */
#define LITH_MODULE_AT     0x1ad20UL
#define LITH_ADPCM_STEP    0x8188UL
#define LITH_ADPCM_DELTA   0x81a8UL
/* OURS - ibxm's interpolation flag; 0 is what a 1997 mixer did. */
#define LITH_MUSIC_INTERP  0

#define LITH_AUDIO_RATE    44100L

/* ORDER 50 RUNS ONE TICK, NOT THE 384 ITS PATTERN ASKS FOR. The
 * replayer's effect pass reads past its command table at img 0x8270
 * into the period table at img 0x8290 and takes a period's low byte
 * for a command; here that byte is 3, which is C, so 1000:76d3
 * breaks the pattern. No pattern data predicts it, so the exception
 * is carried rather than derived - subsystems/music.txt section 6. */
#define LITH_ORDER_STRAY_BREAK  50
#define LITH_ORDER_STRAY_TICKS  1

/* THE DEMO PLAYS ITS MODULE ONE PER CENT SLOW: the tick rate is
 * (tempo * 2) / 5 in INTEGER arithmetic, so T144 gives 57 and not
 * 57.6 - subsystems/music.txt section 5.1. ibxm does not truncate,
 * so it is handed the rate at which ITS tick length comes out as
 * the demo's PIT period. The cost is about one per cent flat in
 * pitch, the deliberate trade for lining the cues up. */
#define LITH_MUSIC_TEMPO   144
#define LITH_IBXM_RATE     ((LITH_AUDIO_RATE * LITH_MUSIC_TEMPO * 2) \
                            / (5 * LITH_TICK_HZ))

int  Lith_MusicInit(s32 rate);
void Lith_MusicShutdown(void);
void Lith_MusicRender(s16 *out, s32 frames);
const u8 *Lith_MusicModule(s32 *len);

void Lith_PalFlashSet(s32 v);
s32  Lith_PalFlash(void);

/* img 0x3dab Pal_BuildRamp - the script runs it between the battle
 * and the tower fall, so g_Pal1 is a blue ramp for that scene only.
 * subsystems/texture.txt. */
void Lith_PalBuildRamp(void);

/* img 0x6c8e - the tail every scene's tick handler jumps into. */
void Lith_TickDefault(void);
s32  Lith_PalFade(void);

/* ------------------------------------------------------- the slots */

/* ONE SLOT OF THE PARAMETER BANK - subsystems/objects.txt. The
 * original holds these as PARALLEL ARRAYS indexed by slot times four,
 * one array per field; a struct per slot is the same data and the
 * reason the original's names read as scalars is that every routine
 * but four addresses slot 0 with no index at all.
 *
 * ANGLE SET B IS PITCH, YAW AND ROLL. img 0x8b83, 0x8bb1 and 0x8bdf
 * carry both names, and that is not a coincidence: the flight model
 * writes attitude into set B and Obj_LoadOrientB is what hands it to
 * Mesh_Transform, so flying and being drawn are the same three
 * words. The camera takes set A instead and is flown by its own
 * decaying push - effects/flyover.txt section 1.
 *
 * Every array's base address is in subsystems/objects.txt, and the
 * fields below are in that order. */
typedef struct {
	s32 pos_x, pos_y, pos_z;
	s32 flight_x, flight_y, flight_z;
	s32 home_x, home_y, home_z;
	s32 ang_x, ang_y, ang_z;
	s32 a_x, a_y, a_z;
	s32 pitch, yaw, roll;
	s32 pitch_acc, yaw_acc;
	s32 roll_rest;
	s32 throttle, speed;
	s32 command, firing, fire_tick;
	u32 track;
	s32 track_tick;
} LithSlot;

/* THE ORIGINAL INDEXES SLOTS BY BYTE OFFSET - SI is 0, 4, 8 - so a
 * documented slot number is four times the array index.
 * effects/battle.txt section 2. */
#define LITH_BATTLE_SLOTS 14
#define LITH_SLOT(n)      ((n) >> 2)

extern LithSlot g_lith_slot[LITH_BATTLE_SLOTS];
extern s32 g_lith_tower_falling;

void Lith_ObjReset(LithSlot *o);
void Lith_ObjLoadOrientA(LithSlot *o);

/* OURS - the mouse look, lithium_look.c. Lith_CamLoad is what the
 * scenes call in place of Lith_ObjLoadOrientA: the load, then the
 * drag applied to the live angles the renderer is about to read. */
void Lith_LookMouse(s32 x, s32 y, s32 buttons);
void Lith_LookTick(void);
void Lith_CamLoad(LithSlot *cam);
void Lith_ObjLoadOrientB(LithSlot *o);
void Lith_TrackPlay(LithSlot *o);
/* `lead` is slot 4, which command 0x18 chases - img 0x895d. */
void Lith_FlightStep(LithSlot *o, const LithSlot *lead);
s32  Lith_TerrainHeightAtSlot(const LithSlot *o);

/* ------------------------------------------------------- the sky */

/* img 0x425b Sky_SunAndStars - effects/sky.txt. It reads the back
 * buffer's 0x80 fill as its only visibility test and plots the
 * stars into it, so it must run AFTER the geometry. */
void Lith_SkySunAndStars(u8 *back, s32 ang_x, s32 ang_y, s32 ang_z);
s32  Lith_SunScreenX(void);
s32  Lith_SunScreenY(void);
s32  Lith_SunVisible(void);
void Lith_SunScreenSet(s32 x, s32 y);
s32  Lith_FlareBrightness(void);
s32  Lith_FlareEnergy(void);
void Lith_FlareEnergyStep(void);

/* ------------------------------------------------------- the flare */

/* A WHOLE REAL-MODE SEGMENT, as the original has - see lithium_flare.c. */
#define LITH_SPRITE_BYTES 0x10000

void Lith_FlickerStep(void);
void Lith_FlareBuildSprite(void);
void Lith_SunGlareFade(void);
void Lith_FlareRender(u8 *back);
void Lith_BlobDrawKeepBand(u8 *back, s32 ex, s32 ey);
void Lith_BlobDrawBurn(u8 *back, s32 ex, s32 ey);
void Lith_FlareSavedSet(s32 x, s32 y);
void Lith_FlareColourSet(s32 c);

/* -------------------------------------------------- the trail */

/* 64 records of 0x16 bytes at 0x8ad2:0x7a80 - effects/trail.txt.
 * Tick_Greetings reads the same ring a different way; 22 bytes is
 * all the two readings share. */
typedef struct { s32 pos[3]; s16 vel[3]; s32 life; } LithTrail;
#define LITH_TRAIL_MAX 64

LithTrail *Lith_TrailRing(void);
s32  Lith_TrailActive(void);
void Lith_TrailReset(void);
/* The caller writes the life and the velocity - effects/trail.txt
 * 5.1 - so the spawn returns the record it took. */
LithTrail *Lith_TrailSpawn(const LithSlot *o);
/* Both return the LAST record they filled - the original leaves DI on
 * it and Tick_TowerFall adds to that particle's rise, img 0x689c. */
LithTrail *Lith_TrailSpawnBurst(const LithSlot *o, s32 n);
LithTrail *Lith_TrailSpawnBurstBig(const LithSlot *o, s32 n);
void Lith_TrailStep(void);
void Lith_TrailDraw(u8 *back, const s32 cam_pos[3],
                    const s32 cam_ang[3]);

/* ------------------------------------------------- the battle */

LithSlot *Lith_BattleSlots(void);
void Lith_FlightStepRoll(LithSlot *o);
void Lith_SlotSwapState(LithSlot *a, LithSlot *b);
/* g_DepthNear img 0x8f80 at img 0x1fb3, and g_BuildFaceAttr img
 * 0x8fa6 at img 0x20a7 - the terrain samples the SNAPSHOT segment,
 * not the sphere map the craft use. */
#define LITH_BATTLE_DEPTH_NEAR   0x1fbd
#define LITH_BATTLE_TERRAIN_ATTR 0x5ad2

void Lith_BattleEnter(void);
void Lith_BattleFrame(u8 *fb, const u8 *tex, const u8 *pic,
                      LithScene *sc, LithProj *pv, LithSortEnt *sl);
s32  Lith_BattleDone(void);

/* g_ApproachEndOrder img 0x8fce, parked by the script at img
 * 0x02e0 - the order the three-ship flyby runs until. */
#define LITH_APPROACH_END_ORDER 0x21

void Lith_ApproachEnter(void);
void Lith_ApproachTick(void);
void Lith_ApproachFrame(u8 *fb, const u8 *pic, LithScene *sc,
                        LithProj *pv, LithSortEnt *sl);
s32  Lith_ApproachDone(void);
void Lith_TexSnapshotFrame(u8 *pic, const u8 *tex, LithScene *sc,
                           LithProj *pv, LithSortEnt *sl);
void Lith_BattleTick(void);
void Lith_RecordModeClear(void);

/* ------------------------------------------------------ the shadow */

/* The stencil at 0x7ad2:0x8c00 is 128 square, and the pass rejects
 * any face whose u or v leaves 0..0x7f - subsystems/raster.txt 3.3. */
#define LITH_SHADOW_W 128

const u8 *Lith_ShadowStencil(void);
void Lith_ShadowBuildStencil(const LithMesh *ship, const u8 *tex,
                             LithScene *sc, LithProj *pv);
void Lith_ShadowOrigin(const LithSlot *cam, const LithSlot *ship);
void Lith_ShadowOrg(s32 *x, s32 *z);
/* ------------------------------------------------------- the bump */

/* OURS - Dust_SampleMask spins until it lands on the mask and a
 * phase too small never will, so the retry is bounded here. The
 * original has no such guard. */
#define LITH_DUST_RETRY  4000

void Lith_DustTick(u8 *pool, s32 jitter_shift, s32 snap_y);
void Lith_DustResetFromPoint(u8 *pool);
void Lith_DustSampleMask(u8 *pool, const u8 *mask, s32 phase);
void Lith_DustModeStep(u8 *pool, const u8 *mask, s32 *phase,
                       s32 *jitter_shift);

/* The dust mask is 320 by 20 at 0x2ad2:0x6700 - effects/bump.txt 7. */
#define LITH_DUST_MASK_BYTES  6400

/* ------------------------------------------------- the shatter */

/* 0x30 bytes at 0x2ad2:0x4000 + 0x30n - three local corners all
 * +/- 16, the position, the rotation, the angular velocity, the
 * velocity, and the three source u,v the shard was cut from.
 * effects/shatter.txt section 2. */
typedef struct {
	s16 c[6];
	s16 pos[3];
	s16 rot[3];
	s16 angvel[3];
	s16 vel[3];
	s16 uv[6];
} LithShard;

/* Seven rows of ten cells, two shards each. */
#define LITH_SHARD_MAX  140

void Lith_PolyFillScreen(u8 *dst, const u8 *tex, const LithPoly *p);

void Lith_ShatterAssembleSet(s32 on);
void Lith_ShatterBuild(void);
void Lith_ShatterDraw(u8 *dst, const u8 *snap);
s32  Lith_ShardsOnScreen(void);

/* The reverse glass, img 0x2919 - the transition into the battle. */
void Lith_ShatterSceneEnter(void);
void Lith_ShatterSceneTick(void);
void Lith_ShatterSceneFrame(u8 *fb, const u8 *pic);
s32  Lith_ShatterSceneDone(void);
void Lith_ShatterStep(void);
LithShard *Lith_Shards(void);
s32  Lith_ShardCount(void);

void Lith_BumpRandomiseLight(void);
void Lith_BumpEnter(void);
void Lith_BumpBegin(void);
void Lith_BumpTick(void);
void Lith_BumpFrame(u8 *fb, const LithMesh *rock, const u8 *tex,
                    LithScene *sc, LithProj *pv, LithSortEnt *sl);
s32  Lith_BumpInterlace(void);
u8  *Lith_PicBuffer(void);
s32  Lith_BumpDone(void);

/* The second half of the same runner - the tunnel, the flying
 * glass and the rock, all into one buffer. effects/shatter.txt
 * and effects/tunnel.txt. */
void Lith_ShatterEnter(const u8 *fb);
void Lith_ShatterTick(void);
void Lith_ShatterFrame(u8 *fb, const LithMesh *rock, const u8 *tex,
                       LithScene *sc, LithProj *pv, LithSortEnt *sl);
s32  Lith_ShatterDone(void);

/* The bore, rebuilt from the sine table every frame - there is no
 * mesh. `attr` is g_TunnelFaceAttr img 0x8fa6. */
void Lith_TunnelBuild(LithScene *sc, s32 cam_x, s32 cam_z,
                      s32 first_ring, s32 last_ring, s32 attr);

/* g_DepthNear img 0x8f80, set to this by both runners that show
 * the bore - img 0x040b and img 0x09bd - and never changed. */
#define LITH_TUNNEL_DEPTH_NEAR 0x36b

void Lith_TunnelCamInit(void);
void Lith_TunnelCamClear(void);
void Lith_TunnelTick(void);
void Lith_TunnelDraw(u8 *fb, const u8 *tex, LithScene *sc,
                     LithProj *pv, LithSortEnt *sl);
void Lith_TunnelEnter(void);
void Lith_TunnelFrame(u8 *fb, const u8 *tex, LithScene *sc,
                      LithProj *pv, LithSortEnt *sl);
s32  Lith_TunnelDone(void);

void Lith_BumpFieldToggleReset(void);
s32  Lith_BumpFieldToggle(void);
const u8 *Lith_DustBrush(s32 interlace);
void Lith_DustStamp(u8 *field, const u8 *pool, const u8 *brush);
void Lith_HeightRelax(u8 *field, s32 interlace, s32 parity);
void Lith_BumpRender(const u8 *field, const u8 *pic, u8 *out,
                     s32 light_x, s32 light_y, s32 interlace);

s32  Lith_ShadowFocus(void);
int  Lith_ShadowFaceUV(LithPoly *p, const LithProj *pv, const s32 *idx,
                       s32 yaw);

/* img 0x9026 - the extra shift on the roll's follow. */
void Lith_RollEaseShift(s32 n);

/* The round and the tower, img 0x21eb and img 0x6756 -
 * effects/towerfall.txt. */
void Lith_TowerFallEnter(void);
void Lith_TowerFallTick(void);
void Lith_TowerFallFrame(u8 *fb, const u8 *tex, const u8 *pic,
                         LithScene *sc, LithProj *pv, LithSortEnt *sl);
s32  Lith_TowerFallDone(void);

/* img 0x8f63 - command 1 ends the scene. */
s32  Lith_SceneDone(void);
void Lith_SceneDoneClear(void);

/* img 0x3799 Vec_Rotate3_2 - Z then X then Y, and it returns the
 * vector TIMES 64 like the single-axis helpers do. */
void Lith_VecRotate3_2(s32 *x, s32 *y, s32 *z, s32 ax, s32 ay, s32 az);

/* ----------------------------------------------------- the terrain */

/* img 0x4067 Terrain_Height and img 0x4b3c Terrain_Build -
 * effects/terrain.txt. The heightmap is 128x128 at LITH_ASSET_BASE
 * and both wrap, so the surface is endless. */
#define LITH_HEIGHTMAP_MASK 0x3fff
#define LITH_HEIGHTMAP_BYTES 0x4000

/* ORIGIN img 0x52dd Terrain_Generate - diamond-square from a FIXED
 * seed, so the landscape is identical every run. Call it once.
 *
 * THE ORIGINAL GENERATES INTO THE ASSET SEGMENT, over packed data it
 * no longer needs, and Terrain_Height then indexes it there. Our
 * image is read-only, so the map lives in its own buffer and the two
 * readers take it from here - effects/terrain.txt section 5. */
void Lith_TerrainGenerate(void);
const u8 *Lith_Heightmap(void);

/* img 0x5334 Math_BuildSquareTable, img 0x4a91 Math_Sqrt and img
 * 0x57e1 Tex_BuildGlowNoise - the transition builds all three, and
 * the last of them is the TERRAIN'S TEXTURE, segment 0x5ad2.
 * effects/terrain.txt section 6. */
#define LITH_SQTAB_ENTRIES 0x640
void Lith_MathBuildSquareTable(void);
s32  Lith_MathSqrt(s32 v);
s32  Lith_Square(s32 d);
void Lith_TexBuildGlowNoise(u8 *buf);
void Lith_TexBuildThreeGlows(u8 *buf);

/* img 0x5354 sets the span row range to the whole screen, so that
 * the first Poly_ClearSpans covers all of it. */
void Lith_SpanResetFull(void);

void Lith_TerrainBuild(LithScene *sc, s32 cam_ang_x, s32 cam_ang_y,
                       s32 scroll_x, s32 scroll_z, s32 attr);

/* ------------------------------------------------- the flyover */

/* img 0x18a3 maybe_Scene_SurfaceFlyover - effects/flyover.txt. */
const LithSlot *Lith_FlyoverSlot(s32 i);
void Lith_FlyoverEnter(void);
void Lith_FlyoverTick(void);
void Lith_FlyoverFrame(u8 *fb, const LithMesh *ship, const LithMesh *tower,
                       const u8 *tex, const u8 *glow, LithScene *sc,
                       LithProj *pv, LithSortEnt *sl);

#define LITH_MESH_SHIP     0x4000UL
#define LITH_MESH_TOWER    0x4b7cUL
/* The shot slot 0x10 flies, loaded by the script at img 0x0339. */
#define LITH_MESH_SHOT     0x7272UL
#define LITH_FLYOVER_NEAR  0x1fbdL

/* img 0x3cbc Poly_ScanEdges - a 2D scan of g_PolyArgs into the span
 * table, which is what decides the terrain's extent. */
void Lith_PolyScanEdges(const s32 *xy, s32 n);
s32  Lith_SpanRowMin(void);
s32  Lith_SpanRowMax(void);
s32  Lith_SpanX0(s32 row);
s32  Lith_SpanX1(s32 row);
void Lith_SpanSetBase(s32 row, s32 which, s32 v);
s32  Lith_SpanBase(s32 row, s32 which);
void Lith_PalFadeTo(s32 target);
void Lith_PalFadeShiftSet(s32 n);
void Lith_PalFadeShiftReloadSet(s32 n);
void Lith_PalFlashCountdownSet(s32 n);
void Lith_PalFadeSet(s32 fade, s32 target, s32 shift, s32 reload);

/* ---------------------------------------------------------- the logo */

/* img 0x1930f - the title card, RLE, 4,646 bytes expanding to exactly
 * 64,000. "Vista Lithium" white on black - rockintro.txt section 5. */
#define LITH_TITLE_RLE   0x1930fUL

/* g_FontRleSrc img 0x1a935 -> a 320 x 20 sheet of 26 cells, and
 * g_CreditTable img 0x9089. effects/text.txt sections 5 and 7.1. */
#define LITH_FONT_RLE    0x1a935UL
#define LITH_FONT_BYTES  0x1900
#define LITH_FONT_W      0x140
#define LITH_CREDIT_TABLE 0x9089UL

/* The greetings' own table, img 0x13db, and the name the terminator
 * wraps back to at img 0x69ea - the SECOND entry, so the opening line
 * is shown once and the names cycle. effects/text.txt section 4. */
#define LITH_GREET_TABLE  0x914aUL
#define LITH_GREET_NAMES  0x915aUL

/* img 0x2dd0 Text_DrawString - the greetings' doubled blitter, which
 * leaves the pen and the pointer where it finished. */
void Lith_TextDrawString(u8 *dst, s32 pen);
s32  Lith_TextPen(void);
u32  Lith_CreditPtr(void);
void Lith_CreditPtrSet(u32 p);

/* g_DepthNear img 0x8f80 at img 0x119a, and the palette the script
 * selects for the scene at img 0x03b3. */
#define LITH_FINALE_DEPTH_NEAR 0x4e2
#define LITH_PAL_FINALE        0x9e71UL

/* The runner, img 0x10f7 and img 0x6454 - effects/figure.txt. */
void Lith_FinaleEnter(void);
void Lith_FinaleTick(void);
void Lith_FinaleFrame(u8 *fb, const u8 *tex, LithScene *sc,
                      LithProj *pv, LithSortEnt *sl);
s32  Lith_FinaleDone(void);

/* OURS - what the playground drives: the eye's x and z, the walk's
 * per-tick phase step - 0 freezes mid-stride, negative runs it
 * backwards - and the cross-fade. docs/playground.txt section 5.4. */
void Lith_FinaleCamSet(s32 x, s32 z);
void Lith_FinaleStepSet(s32 n);
void Lith_FinaleSmearSet(s32 on);
void Lith_FinaleNearSet(s32 n);

/* OURS - the playground, lithium_game.c. A part past the finale and
 * nothing the demo reaches on its own. docs/playground.txt. */
void Lith_GameEnter(void);
void Lith_GameTick(void);
void Lith_GameFrame(u8 *fb, const u8 *tex, const u8 *glow,
                    LithScene *sc, LithProj *pv, LithSortEnt *sl);
void Lith_GameKey(int ch, int down);
void Lith_GameMouse(s32 x, s32 y, s32 buttons);
void Lith_GameWheel(s32 dy);

/* The greetings and their rays, img 0x1396 and img 0x6972 -
 * effects/text.txt sections 3 and 4. */
void Lith_GreetingsEnter(void);
void Lith_GreetingsTick(void);
void Lith_GreetingsFrame(u8 *fb);
s32  Lith_GreetingsDone(void);
void Lith_GreetingsLeave(void);

void Lith_FontUnpack(void);

/* OURS - the bump scene draws its dust mask with the SAME routine
 * the credit roll uses, img 0x2d46 at img 0x0698, so it is no longer
 * private to the credits. effects/bump.txt section 7. */
void Lith_TextDrawStringAt(u8 *dst, s32 pen, u32 str, s32 dl);
void Lith_CreditsEnter(void);
void Lith_CreditsTick(void);
void Lith_CreditsDraw(u8 *back);
s32  Lith_CreditIndex(void);
s32  Lith_Part(void);

/* g_BlitSrcW and g_BlitSrcH, img 0x8fd6 and 0x8fda: 320 << 10 and
 * 200 << 10. THE WIDTH COVERS HALF THE PICTURE because u shifts by
 * 0xb where v shifts by 0xa. */
#define LITH_BLIT_SRC_W  0x50000L
#define LITH_BLIT_SRC_H  0x32000L
#define LITH_BLIT_VISTA  0        /* src_ofs for the left half   */
#define LITH_BLIT_LITH   160      /* and for the right           */

/* What Image_BlitScaled reads, each from its own global: zoom is the
 * denominator at img 0x8f8e, src_w and src_h are img 0x8fd6 and
 * 0x8fda, the centre pair img 0x8fd0 and 0x8fd2, src_ofs img 0x8fd4
 * for which half of the picture, and phase img 0x8f9f for the fade. */
typedef struct {
	s32 zoom;
	s32 src_w, src_h;
	s32 centre_x, centre_y;
	s32 src_ofs;
	s32 phase;
} LithBlit;

void Lith_ImageUnpackRle(u8 *dst, u32 src);
void Lith_ImageBlur(u8 *buf, s32 times);
void Lith_ScreenCrossFade(u8 *blend, const u8 *back);
void Lith_ImageBlitScaled(u8 *dst, const u8 *src, const LithBlit *b);

#endif
