/* LITHIUM - the host contract, and what little of the demo is here.
 *
 * Demo_Init depacks the original and Demo_Frame runs the opening.
 * The order the parts go in is docs/lithium/effects/scenes.txt, and
 * the Python they are being followed from is sim/lithium/.
 *
 * [open] Demo_Audio is silent and lithium_music.c keeps the module's
 * ORDER POSITION only, which is all any scene reads from the
 * replayer. Lithium has a Gravis Ultrasound path
 * that mixes in HARDWARE where XCURSION only ever mixes in software,
 * so its replayer is not XCURSION's - docs/lithium/subsystems/
 * music.txt.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../host.h"
#include "../xcurpack.h"
#include "lithium.h"
#include "../agent_host.h"

const u8 *g_lith_img;

static u8 *g_image_own;
static u32 g_image_len;
static u8 g_fb[LITH_FB_BYTES];
static u8 g_pal[LITH_PAL_BYTES];

/* THE STORE, standing in for img 0x96be. Pal_Animate rebuilds it
 * and Pal_Upload is the only thing that reaches g_pal. */
static u8 g_pal_store[LITH_PAL_BYTES];
static s32 g_tick;

/* The rock of the opening - one mesh at two scales is the whole of
 * scenes 1 and 2, effects/rockintro.txt section 2. */
LithMesh g_lith_rock;
LithMesh g_lith_ship;
LithMesh g_lith_tower;

/* [open] A BENCH, NOT THE SCENE. One transform of the rock at known
 * angles so the pipeline can be diffed against sim/lithium/parts.py
 * through the agent - docs/agent.txt. The runner replaces it. */
LithScene g_lith_scene;
LithProj g_lith_proj[LITH_POOL_MAXV];
LithSortEnt g_lith_sort[LITH_POOL_MAXF];
s32 g_lith_nsort;
u8 g_lith_tex[LITH_TEX_BYTES];
u8 g_lith_glow[LITH_TEX_BYTES];

const char *Demo_DefaultPath(void) { return "reverse/ghidra/LITHIUM.EXE"; }
const char *Demo_Name(void) { return "LITHIUM"; }

/* OURS - LITH_TICK_HZ carries where 57 came from. */
s32 Demo_TickHz(void) { return LITH_TICK_HZ; }

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
 * no unpacked file and none ships. The packer is XCURSION's, byte
 * for byte, so this is xcurpack.c and not a second implementation. */
static int load_image(const char *exe_path)
{
	long len = 0;
	u8 *packed;
	u32 got = 0;
	int rc;

	packed = slurp(exe_path, &len);
	if (!packed) return 1;

	g_image_own = (u8 *)calloc(LITH_IMAGE_BYTES, 1);
	if (!g_image_own) { free(packed); return 1; }
	rc = XcurPack_Depack(packed, (u32)len, g_image_own,
	                     LITH_IMAGE_BYTES, &got);
	free(packed);
	if (rc != XCURPACK_OK) {
		free(g_image_own);
		g_image_own = 0;
		return 1;
	}
	g_lith_img = g_image_own;
	g_image_len = got;
	return 0;
}

/* ORIGIN img 0x8fe1 g_PalRampShift, and the three accumulators at
 * img 0x8f22. THE PHASES ARE SEEDED BY A SCENE, NOT STARTED AT ZERO:
 * maybe_Scene_BumpShatter writes 0xf8a3, 0xf6f0 and 0xf41b at img
 * 0x0765 to 0x0773, so the cycling begins at a defined point in the
 * ramp rather than wherever the last part left it. */
static u16 g_pal_phase[3];
static s32 g_pal_ramp_shift;

/* OURS */
void Lith_PalPhaseSeed(u16 r, u16 g, u16 b)
{
	g_pal_phase[0] = r;
	g_pal_phase[1] = g;
	g_pal_phase[2] = b;
}

/* OURS - img 0x8fe1 is 0 as the image ships it, and four scene
 * routines write it, so a part can steepen every ramp at once. */
void Lith_PalRampShift(s32 n)
{
	g_pal_ramp_shift = n;
}

/* OURS - Tick_Dust adds to the three accumulators before every
 * animate, img 0x6b1d, so the bump scene's ramps drift faster than
 * the ones a scene leaves alone. The seeder sets; this adds. */
void Lith_PalPhaseAdd(u16 r, u16 g, u16 b)
{
	g_pal_phase[0] = (u16)(g_pal_phase[0] + r);
	g_pal_phase[1] = (u16)(g_pal_phase[1] + g);
	g_pal_phase[2] = (u16)(g_pal_phase[2] + b);
}

/* ORIGIN img 0x6d49 Pal_Animate - THE COLOUR CYCLING.
 *
 * Three phases advance by 0xd, 0x10 and 0x15 of 2048 per call -
 * coprime enough that the hue wanders without repeating soon. Each
 * indexes the sine table and the sample becomes a per-channel SLOPE;
 * a 16-bit accumulator is stepped by it and ITS HIGH BYTE IS THE DAC
 * VALUE, clamped at 0x3f.
 *
 * SO EVERY ENTRY IS A LINEAR RAMP UP FROM BLACK and what cycles is
 * the ramp's colour, not the individual entries. It fills 128 ENTRIES
 * and blacks out entry 128; 129 to 255 the original never touches.
 * docs/lithium/subsystems/texture.txt. */
void Lith_PalAnimate(u8 *pal)
{
	s32 step[3], i, c;
	u16 acc[3];
	const s32 *t = Lith_SinTable();

	if (!t) return;
	g_pal_phase[0] = (u16)(g_pal_phase[0] + 0x0d);
	g_pal_phase[1] = (u16)(g_pal_phase[1] + 0x10);
	g_pal_phase[2] = (u16)(g_pal_phase[2] + 0x15);

	for (c = 0; c < 3; c++) {
		s32 bx = (g_pal_phase[c] & 0x1ffc) >> 2;
		s32 v = ((t[bx] >> 6) + 0x40) & 0xff;
		step[c] = (v << g_pal_ramp_shift) & 0xffff;
		acc[c] = 0;
	}

	memset(pal, 0, LITH_PAL_BYTES);
	for (i = 0; i < 128; i++) {
		for (c = 0; c < 3; c++) {
			pal[i * 3 + c] = (u8)((acc[c] >> 8) & 0xff);
			acc[c] = (u16)(acc[c] + step[c]);
			if ((acc[c] >> 8) > 0x3f) acc[c] = 0x3f00;
		}
	}
}

/* ORIGIN img 0x3dcc Pal_Upload. The clamp mixes an unsigned test
 * with a signed one on purpose: the unsigned one catches the wrapped
 * negatives and the signed one separates them from the overflow. */
void Lith_PalUpload(u8 *dac, const u8 *store, s32 fade)
{
	s32 bias = fade >> 2;
	s32 i;

	for (i = 0; i < LITH_PAL_BYTES; i++) {
		s32 v = (s32)store[i] + bias;
		if (v < 0) v = 0;
		if (v > 0x3f) v = 0x3f;
		dac[i] = (u8)v;
	}
}

/* OURS */
u8 *Lith_PalStore(void) { return g_pal_store; }

/* OURS - g_Pal1 once Pal_BuildRamp has written it, see below. */
static u8 g_pal1[LITH_PAL_BYTES];
static s32 g_pal1_built;

/* OURS - the original keeps a POINTER at img 0x8f8c and Pal_Upload
 * follows it, so selecting a palette is a two-byte write there and
 * costs no copy. The store is RAM here because Pal_Animate writes
 * into it, so selecting one copies. */
void Lith_PalSelect(u32 src)
{
	s32 i;

	if (!g_lith_img) return;
	if (src == LITH_PAL_1 && g_pal1_built) {
		for (i = 0; i < LITH_PAL_BYTES; i++)
			g_pal_store[i] = g_pal1[i];
		return;
	}
	for (i = 0; i < LITH_PAL_BYTES; i++)
		g_pal_store[i] = g_lith_img[src + i];
}

/* ORIGIN img 0x3dab Pal_BuildRamp - 63 entries of (n, n+2, n+6) from
 * n = 1 up, entry 0 untouched. subsystems/texture.txt.
 *
 * OURS IS A WRITABLE COPY. The original's palette stores live in the
 * image and this routine writes one, so a store cannot be re-read
 * from the image once it has been built - our g_Pal1 has to outlive
 * the call. */
void Lith_PalBuildRamp(void)
{
	s32 i;
	u8 al = 3;

	if (!g_lith_img) return;
	for (i = 0; i < LITH_PAL_BYTES; i++)
		g_pal1[i] = g_lith_img[LITH_PAL_1 + i];
	for (i = 0; i < 0x3f; i++) {
		al = (u8)(al - 2);
		g_pal1[3 + i * 3 + 0] = al;
		al = (u8)(al + 2);
		g_pal1[3 + i * 3 + 1] = al;
		al = (u8)(al + 4);
		g_pal1[3 + i * 3 + 2] = al;
		al = (u8)(al - 3);
	}
	g_pal1_built = 1;
}

/* OURS - img 0x6d08 and img 0x6307 both land here. */
void Lith_PalRefresh(void)
{
	Lith_PalUpload(g_pal, g_pal_store, Lith_PalFade());
}

enum { LITH_PART_ROCKINTRO, LITH_PART_TOBLACK, LITH_PART_TOWHITE,
       LITH_PART_FLYOVER, LITH_PART_BUMPWAIT, LITH_PART_BUMP,
       LITH_PART_SHATTER,
       LITH_PART_TUNNEL, LITH_PART_ASSEMBLE, LITH_PART_FLASH,
       LITH_PART_APPROACH, LITH_PART_BATTLE, LITH_PART_TOWERFALL,
       LITH_PART_GREETINGS, LITH_PART_FINALE, LITH_PART_GAME,
       LITH_PARTS };
static s32 g_part;

/* OURS - x86 IDIV truncates toward zero; C89 leaves it open. */
s32 lith_idiv(s32 a, s32 b)
{
	s32 q;

	if (!b) return 0;
	q = (a < 0 ? -a : a) / (b < 0 ? -b : b);
	return ((a < 0) != (b < 0)) ? -q : q;
}

/* OURS - ((s64)a << 8) / b, truncating toward zero.
 *
 * EVERY PROJECTION DIVIDES A 64-BIT DIVIDEND. img 0x2ef1, img 0x2b6b
 * and img 0x42cf all build EDX:EAX with SHL EAX, 8 and SAR EDX, 0x18
 * before the IDIV, so the shift cannot lose the top eight bits.
 * Shifting a 32-bit value instead silently overflows once the
 * coordinate passes 2^23 - and a sign flip there puts a shard that
 * has fallen far below the screen back ON it, as a black sliver the
 * full height of the frame.
 *
 * s64 IS WHY pd.h STEPS OUTSIDE C89. Division truncates toward zero
 * in C as it does in IDIV, so the shift and the divide are the
 * original's arithmetic directly. */
s32 lith_idiv_shl8(s32 a, s32 b)
{
	if (!b) return 0;
	return (s32)(((s64)a << 8) / (s64)b);
}

/* OURS - the generator where a replay starts, not the image seed. */
static u16 g_rnd_start[2];

/* OURS - everything the first part needs, so a seek can replay from
 * here rather than tracking what each part would have left behind.
 *
 * IT RUNS THE WHOLE FRAME, drawing included: scene state is settled
 * inside the frame functions, as effects/bump.txt 5 describes, so a
 * seek that ticked without drawing never arrives. */
static void demo_restart(void)
{
	Lith_RndSeed(g_rnd_start[0], g_rnd_start[1]);
	Lith_MusicStart();
	memset(g_fb, 0, sizeof g_fb);
	Lith_RockIntroEnter();
	Lith_PalRefresh();
	g_part = LITH_PART_ROCKINTRO;
	g_tick = 0;
}

/* OURS. g_PalRampShift is set to 1 the way the opening runner does
 * at img 0x1520, and the blend buffer is cleared the way img 0x150e
 * does - the opening fades up out of it. effects/rockintro.txt 1.
 *
 * IT UPLOADS BUT DOES NOT ANIMATE. img 0x018d calls Pal_Upload only;
 * Pal_Animate first runs inside the tick handler, so animating here
 * too puts every palette phase a step ahead for the whole part. */
int Demo_Init(const char *exe_path)
{
	if (load_image(exe_path)) return 1;
	Lith_SinTableBuild();
	Lith_MeshLoad(&g_lith_rock, LITH_MESH_ROCK);
	Lith_TexBuildSphereMap(g_lith_tex);
	Lith_MeshLoad(&g_lith_ship, LITH_MESH_SHIP);
	Lith_MeshLoad(&g_lith_tower, LITH_MESH_TOWER);
	Lith_TerrainGenerate();
	Lith_MathBuildSquareTable();
	Lith_TexBuildGlowNoise(g_lith_glow);
	Lith_FontUnpack();
	Lith_FlareBuildSprite();
	Lith_ShadowBuildStencil(&g_lith_ship, g_lith_tex, &g_lith_scene,
	                        g_lith_proj);
	Lith_MusicInit(LITH_AUDIO_RATE);
	Lith_RndGet(&g_rnd_start[0], &g_rnd_start[1]);
	demo_restart();
	return 0;
}

/* OURS */
void Demo_Shutdown(void)
{
	Lith_MusicShutdown();
	free(g_image_own);
	g_image_own = 0;
	g_lith_img = 0;
	g_image_len = 0;
}

/* OURS - the script Demo_Main runs between the parts, img 0x0201 to
 * img 0x0262, as a state machine. The original is straight-line code
 * that blocks in Demo_RunUntil; a host that cannot block needs the
 * same steps driven one tick at a time.
 *
 * ONE HOST FRAME IS ONE TICK HERE AND IN THE ORIGINAL IT IS NOT. The
 * original's motion runs on the timer ISR while the loop body renders
 * as fast as the machine allows, so a slow machine drops frames and
 * the opening still lasts the same 2,085 ticks. Tying the two gives a
 * host-rate-independent part length instead, which is what the
 * frame-for-frame diff against the emulator needs.
 *
 * THE STORED PALETTE FREEZES ACROSS THE TRANSITION. Tick_Default does
 * not call Pal_Animate - only a scene's own handler does - so the
 * ramps stop animating the moment the opening returns and the only
 * thing still moving is g_PalFade. effects/transition.txt.
 * The flyover's stored palette is img 0x19d9 - effects/flyover.txt 3.
 *
 * THE SPHERE MAP IS BUILT BEFORE THE TERRAIN because Terrain_Generate
 * reseeds the generator and the map wants the image's own seed -
 * Demo_Main has them in that order too, img 0x0168 then 0x01f1. The
 * square table and the glow follow at img 0x0232 and 0x0235.
 *
 * THE SNAPSHOT IS OF THE FRAME JUST DRAWN, img 0x0945, so the bump
 * part hands over AFTER its draw and not before it - the shards
 * carry that picture.
 *
 * LEAVING THE SHATTER ON TIME IS NOT COSMETIC. The shards' y is a
 * word and wraps about 252 ticks in, which puts them back on screen
 * as flecks; the original's phase ends at 293, so it shows that for
 * forty ticks and then leaves. A part that runs on shows it for as
 * long as it runs. effects/shatter.txt section 5.2.
 *
 * THE NOISE TEXTURE IS BUILT TWICE AND BOTH MATTER. img 0x02c3 is
 * what the SNAPSHOT'S terrain samples - and the snapshot is what the
 * glass assembles and what the ships fly over - while img 0x032d is
 * what the BATTLE'S terrain samples. Skip either and that scene
 * draws with whatever tenant of 0x5ad2 came before it, 0x80 too low
 * and in the wrong quarter of the palette.
 * subsystems/texture.txt section 6.4.
 *
 * THE SQUARE TABLE IS REBUILT BEFORE THE NOISE, img 0x032a, because
 * it shares memory with the rasteriser's span records and the
 * approach has just drawn over them - subsystems/raster.txt 6.
 *
 * THE WHITE-OUT IS A PART OF ITS OWN, img 0x030e to 0x031b. Once
 * the glass is together the assembled picture STANDS while the fade
 * runs past neutral to +0x80 at a shift of 0xf, and only order 0x20
 * starts the battle. The handler through it is still Tick_Shatter -
 * nothing reinstalls one before img 0x2008 - so the shards go on
 * being stepped against their stops.
 *
 * THE ASSEMBLE ENDS BY SHOWING THE PICTURE WHOLE, img 0x295b: the
 * 140 assembled shards have seams between them, so the scene blits
 * the original over them rather than letting them stand.
 *
 * THE BATTLE HANDS OVER ON TRACK 0'S STOP COMMAND, which is what
 * Lith_BattleDone() reads - img 0x0336 returning into the script.
 *
 * A HANDOVER THAT CHANGES THE PALETTE MUST ALSO REDRAW. The original
 * presents inside each scene's own loop and then leaves it, so the
 * new scene's fade is set with nothing on screen to show it - the
 * tunnel's 24 ticks of setup at img 0x02c0 present nothing at all.
 * Here one Demo_Frame draws and then hands over, and the host
 * presents after, so a scene whose Enter moves the fade would show
 * ONE frame of the previous scene's pixels under the new palette.
 * The tunnel is where it shows: it leaves at a fade of -191 and the
 * assemble sets -0x40, which lit the dead tunnel back up for a
 * frame. ../../docs/lithium/subsystems/timeline.txt section 2.1.
 *
 * THE FINALE HANDS OVER TO THE PLAYGROUND, AND THAT IS OURS.
 * Lith_FinaleDone cannot fire - img 0x11c6 waits for a module wrap
 * this clock does not do - so the handover watches the tick reach
 * the end of the order list, which is the same moment by a route
 * the clock can answer. ../../docs/playground.txt section 5.2. */
void Demo_Frame(void)
{
	if (!g_lith_img) return;
	g_tick++;
	Lith_LookTick();

	switch (g_part) {
	case LITH_PART_ROCKINTRO:
		Lith_RockIntroTick();
		Lith_RockIntroFrame(g_fb, &g_lith_rock, g_lith_tex,
		                    &g_lith_scene, g_lith_proj,
		                    g_lith_sort);
		if (Lith_RockIntroDone()) {
			Lith_PalFadeSet(Lith_PalFade(), -0x100, 0xe, 0xe);
			g_part = LITH_PART_TOBLACK;
		}
		break;

	case LITH_PART_TOBLACK:
		Lith_TickDefault();
		if (Lith_PalFade() <= -0x100) {
			memset(g_fb, 0, sizeof g_fb);
			Lith_PalFadeSet(-0x80, 0xc0, 0xe, 0xe);
			g_part = LITH_PART_TOWHITE;
		}
		break;

	case LITH_PART_TOWHITE:
		Lith_TickDefault();
		if (Lith_MusicPos() >= 8) {
			Lith_PalSelect(LITH_PAL_1);
			memset(g_fb, 0x80, sizeof g_fb);
			Lith_PalFadeSet(0x120, 0, 8, 3);
			Lith_FlyoverEnter();
			g_part = LITH_PART_FLYOVER;
		}
		break;

	case LITH_PART_FLYOVER:
		Lith_FlyoverTick();
		Lith_FlyoverFrame(g_fb, &g_lith_ship, &g_lith_tower,
		                  g_lith_tex, g_lith_glow,
		                  &g_lith_scene, g_lith_proj,
		                  g_lith_sort);
		if (Lith_SceneDone()) {
			Lith_SceneDoneClear();
			Lith_BumpEnter();
			g_part = LITH_PART_BUMPWAIT;
		}
		break;

	case LITH_PART_BUMPWAIT:
		Lith_TickDefault();
		if (Lith_MusicPos() >= 0x18) {
			Lith_BumpBegin();
			g_part = LITH_PART_BUMP;
		}
		break;

	case LITH_PART_BUMP:
		Lith_BumpTick();
		Lith_BumpFrame(g_fb, &g_lith_rock, g_lith_tex,
		               &g_lith_scene, g_lith_proj,
		               g_lith_sort);
		if (Lith_BumpDone()) {
			Lith_ShatterEnter(g_fb);
			g_part = LITH_PART_SHATTER;
		}
		break;

	case LITH_PART_SHATTER:
		Lith_ShatterTick();
		Lith_ShatterFrame(g_fb, &g_lith_rock, g_lith_tex,
		                  &g_lith_scene, g_lith_proj,
		                  g_lith_sort);
		if (Lith_ShatterDone()) {
			Lith_TunnelEnter();
			g_part = LITH_PART_TUNNEL;
		}
		break;

	case LITH_PART_TUNNEL:
		Lith_TunnelTick();
		Lith_TickDefault();
		Lith_TunnelFrame(g_fb, g_lith_tex, &g_lith_scene,
		                 g_lith_proj, g_lith_sort);
		if (Lith_TunnelDone()) {
			Lith_MathBuildSquareTable();    /* img 0x02c0 */
			Lith_TexBuildGlowNoise(Lith_PicBuffer());
			Lith_FlareBuildSprite();        /* img 0x02c6 */
			Lith_TexSnapshotFrame(Lith_PicBuffer(), g_lith_tex,
			                      &g_lith_scene, g_lith_proj,
			                      g_lith_sort);
			Lith_ShatterSceneEnter();
			g_part = LITH_PART_ASSEMBLE;
			Lith_ShatterSceneFrame(g_fb, Lith_PicBuffer());
		}
		break;

	case LITH_PART_ASSEMBLE:
		Lith_ShatterSceneTick();
		Lith_ShatterSceneFrame(g_fb, Lith_PicBuffer());
		if (Lith_ShatterSceneDone()) {
			memcpy(g_fb, Lith_PicBuffer(), sizeof g_fb);
			Lith_PalFadeTo(0x80);
			Lith_PalFadeShiftSet(0xf);
			g_part = LITH_PART_FLASH;
		}
		break;

	case LITH_PART_FLASH:
		Lith_ShatterSceneTick();
		if (Lith_MusicPos() >= 0x20) {
			Lith_ApproachEnter();
			g_part = LITH_PART_APPROACH;
		}
		break;

	case LITH_PART_APPROACH:
		Lith_ApproachTick();
		Lith_ApproachFrame(g_fb, Lith_PicBuffer(),
		                   &g_lith_scene, g_lith_proj,
		                   g_lith_sort);
		if (Lith_ApproachDone()) {
			Lith_MathBuildSquareTable();
			Lith_TexBuildGlowNoise(Lith_PicBuffer());
			Lith_BattleEnter();
			g_part = LITH_PART_BATTLE;
		}
		break;

	case LITH_PART_BATTLE:
		Lith_BattleTick();
		Lith_BattleFrame(g_fb, g_lith_tex, Lith_PicBuffer(),
		                 &g_lith_scene, g_lith_proj,
		                 g_lith_sort);
		if (Lith_BattleDone()) {
			Lith_PalBuildRamp();
			Lith_TowerFallEnter();
			g_part = LITH_PART_TOWERFALL;
		}
		break;

	case LITH_PART_TOWERFALL:
		Lith_TowerFallTick();
		Lith_TowerFallFrame(g_fb, g_lith_tex, Lith_PicBuffer(),
		                    &g_lith_scene, g_lith_proj,
		                    g_lith_sort);
		if (Lith_TowerFallDone()) {
			Lith_PalSelect(LITH_PAL_2);
			Lith_GreetingsEnter();
			g_part = LITH_PART_GREETINGS;
		}
		break;

	case LITH_PART_GREETINGS:
		Lith_GreetingsTick();
		Lith_GreetingsFrame(g_fb);
		if (Lith_GreetingsDone()) {
			Lith_GreetingsLeave();
			Lith_PalSelect(LITH_PAL_FINALE);
			Lith_PalFadeSet(-0xa0, 0, 4, 4);
			Lith_PalRefresh();
			Lith_FinaleEnter();
			g_part = LITH_PART_FINALE;
		}
		break;

	case LITH_PART_FINALE:
		Lith_FinaleTick();
		Lith_FinaleFrame(g_fb, g_lith_tex, &g_lith_scene,
		                 g_lith_proj, g_lith_sort);
		if (Lith_MusicEndTick() && g_tick >= Lith_MusicEndTick()) {
			Lith_GameEnter();
			g_part = LITH_PART_GAME;
		}
		break;

	default:
		Lith_GameTick();
		Lith_GameFrame(g_fb, g_lith_tex, g_lith_glow,
		               &g_lith_scene, g_lith_proj, g_lith_sort);
		break;
	}
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
void Demo_Audio(s16 *out, s32 frames)
{
	if (out && frames > 0) Lith_MusicRender(out, frames);
}

/* OURS */
const u8 *Demo_Image(u32 *len)
{
	if (len) *len = g_image_len;
	return g_lith_img;
}

/* OURS - the look, lithium_look.c. src/host.h hands over
 * framebuffer pixels and bit 0 of buttons. */
void Demo_Mouse(s32 x, s32 y, s32 buttons)
{
	if (g_part == LITH_PART_GAME) Lith_GameMouse(x, y, buttons);
	else Lith_LookMouse(x, y, buttons);
}

/* OURS - the wheel is the playground's zoom and nothing else uses
 * it. src/host.h. */
void Demo_Wheel(s32 dy)
{
	if (g_part == LITH_PART_GAME) Lith_GameWheel(dy);
}

/* OURS - what the agent needs on top of host.h, src/agent_host.h. */
s32 Dbg_TickCount(void) { return g_tick; }

/* OURS */
s32 Lith_Part(void) { return g_part; }

/* OURS - the names ../../docs/lithium/subsystems/timeline.txt 2.1
 * uses, so a part the agent reports and a row of that table are the
 * same thing said twice rather than two namings to reconcile. */
static const char *const g_part_name[LITH_PARTS] = {
	"rock intro",           "to black",
	"to white",             "surface flyover",
	"bump, waiting",        "bump, dust cloud",
	"shatter, forward",     "tunnel",
	"shatter, assemble",    "flash",
	"approach",             "battle",
	"tower fall",           "greetings",
	"finale",               "playground"
};

/* OURS */
s32 Dbg_PartIndex(void) { return g_part; }

/* OURS */
s32 Dbg_PartCount(void) { return LITH_PARTS; }

/* OURS - a negative index asks for the part running now. */
const char *Dbg_PartLabel(s32 i)
{
	if (i < 0) i = g_part;
	if (i < 0 || i >= LITH_PARTS) return "?";
	return g_part_name[i];
}

/* OURS - where each part begins, and what it has to be entered
 * through to look right.
 *
 * THE TICK IS THE WHOLE MUSIC STATE. g_MusicPos is music_pos_at of
 * the tick and nothing else, so placing the clock places the order,
 * the credit roll and every gate that reads them - a second column
 * of music offsets would only be a copy that could disagree.
 *
 * SEVEN PARTS CANNOT BE ENTERED COLD and carry the part to land on
 * instead; the jump runs forward from there. Two want the picture
 * before them - the shatter textures its shards from the frame the
 * bump left, and the assemble, the flash and the approach live off
 * Tex_SnapshotFrame's copy of the tunnel's last frame at img 0x27e3,
 * with no second buffer to take it from. The rest want STATE a
 * handover gives them: the tunnel's camera and ramp shift come from
 * Lith_ShatterEnter, and the two fades show the frame before them
 * going out. Assembling any of it from literals would be inventing
 * state, so they replay the short way instead.
 *
 * THE COST IS THE SCENE, NOT THE DEMO: 760 ms at worst, the flyover
 * into the bump wait, against the 3.1 s a replay from the opening
 * takes to reach the finale. Eight of the fifteen are instant.
 *
 * A base of -1 is a part that enters cold. The ticks are measured,
 * not derived; they move when the timing does.
 * ../../docs/lithium/subsystems/timeline.txt section 2.1. */
typedef struct {
	s32 tick;
	s32 base;
} LithPartSeek;

static const LithPartSeek g_seek[LITH_PARTS] = {
	{     0, -1 },
	{  2085, LITH_PART_ROCKINTRO },
	{  2180, -1 },
	{  2356, -1 },
	{  5332, LITH_PART_FLYOVER },
	{  5428, -1 },
	{  6000, LITH_PART_BUMP },
	{  6293, LITH_PART_BUMP },
	{  6772, LITH_PART_BUMP },
	{  6842, LITH_PART_BUMP },
	{  6964, LITH_PART_BUMP },
	{  7156, -1 },
	{  8962, -1 },
	{ 10768, -1 },
	{ 12797, -1 },
	{ 13749, -1 }
};

/* OURS - the handover into a part, lifted from the arm of Demo_Frame
 * that performs it. The battle and the tower fall rebuild the glow
 * noise because the approach's handover does, and landing on either
 * without it leaves them drawing over whatever picture was last in
 * the buffer. */
static void part_enter(s32 which)
{
	switch (which) {
	case LITH_PART_ROCKINTRO:
		Lith_RockIntroEnter();
		break;
	case LITH_PART_TOBLACK:
		Lith_PalFadeSet(Lith_PalFade(), -0x100, 0xe, 0xe);
		break;
	case LITH_PART_TOWHITE:
		memset(g_fb, 0, sizeof g_fb);
		Lith_PalFadeSet(-0x80, 0xc0, 0xe, 0xe);
		break;
	case LITH_PART_FLYOVER:
		Lith_PalSelect(LITH_PAL_1);
		memset(g_fb, 0x80, sizeof g_fb);
		Lith_PalFadeSet(0x120, 0, 8, 3);
		Lith_FlyoverEnter();
		break;
	case LITH_PART_BUMPWAIT:
		Lith_BumpEnter();
		break;
	case LITH_PART_BUMP:
		Lith_BumpEnter();
		Lith_BumpBegin();
		break;
	case LITH_PART_TUNNEL:
		Lith_TunnelEnter();
		break;
	case LITH_PART_BATTLE:
		Lith_MathBuildSquareTable();
		Lith_TexBuildGlowNoise(Lith_PicBuffer());
		Lith_BattleEnter();
		break;
	case LITH_PART_TOWERFALL:
		Lith_MathBuildSquareTable();
		Lith_TexBuildGlowNoise(Lith_PicBuffer());
		Lith_PalBuildRamp();
		Lith_TowerFallEnter();
		break;
	case LITH_PART_GREETINGS:
		Lith_PalSelect(LITH_PAL_2);
		Lith_GreetingsEnter();
		break;
	case LITH_PART_GAME:
		Lith_GameEnter();
		break;
	default:
		Lith_GreetingsLeave();
		Lith_PalSelect(LITH_PAL_FINALE);
		Lith_PalFadeSet(-0xa0, 0, 4, 4);
		Lith_FinaleEnter();
		break;
	}
}

/* OURS - land on a part without replaying the demo into it.
 *
 * THE RNG IS RESEEDED so a jump is repeatable, and that is all it
 * is: the stream a part would really have reached by here is gone,
 * so the dust, the lights and the shard velocities are this jump's
 * and not the timeline's. A frame taken after a jump cannot be
 * compared against the emulator - Dbg_GotoPart is what that needs.
 *
 * THE REFRESH IS NOT OPTIONAL. Tick_Default only re-uploads the DAC
 * while the fade is moving, so a part entered onto a fade that is
 * already at its target would come up on whatever the last scene
 * left in the hardware. */
static void jump_part(s32 which)
{
	s32 base, guard = 0;

	if (which < 0) which = 0;
	if (which >= LITH_PARTS) which = LITH_PARTS - 1;
	base = g_seek[which].base < 0 ? which : g_seek[which].base;

	Lith_RndSeed(g_rnd_start[0], g_rnd_start[1]);
	memset(g_fb, 0, sizeof g_fb);
	Lith_MusicSeek(g_seek[base].tick);
	g_tick = g_seek[base].tick;
	g_part = base;
	part_enter(base);
	Lith_PalRefresh();

	while (g_part != which && ++guard < 6000) Demo_Frame();
	if (base != which) Lith_MusicSeekAudio(g_seek[which].tick);
}

/* OURS - src/agent_host.h, and the accurate half of the seek.
 *
 * IT REPLAYS, AND THAT IS WHAT IT IS FOR. Every part's state
 * accumulates and the music clock decides the handovers, so running
 * the demo into a part is the only way to reach the state a real
 * playthrough would have there. main_ppm.c -scene takes this one so
 * the bench stays a true run; the window takes jump_part. Holding F
 * is the same advance at the display's rate.
 *
 * Forwards needs no replay - running on from here IS that timeline.
 * The frames are whole frames, for the reason demo_restart gives. */
void Dbg_GotoPart(s32 which)
{
	s32 guard = 0;

	if (which < 0) which = 0;
	if (which >= LITH_PARTS) which = LITH_PARTS - 1;
	if (which <= g_part) {
		demo_restart();
		if (which == LITH_PART_ROCKINTRO) return;
	}
	while (g_part != which && ++guard < 100000) Demo_Frame();
}

/* OURS */
void Dbg_StepPart(s32 delta) { Dbg_GotoPart(g_part + delta); }

/* OURS - [ and ] step the parts, as they do in XCURSION, and g
 * jumps to the playground.
 *
 * THE RELEASE IS NOT DISCARDED ANY MORE. Steering is a held key, so
 * the game needs both edges - src/host.h promises them and the hosts
 * send them; only this routine was throwing them away. */
void Demo_Key(int ch, int down)
{
	if (g_part == LITH_PART_GAME) Lith_GameKey(ch, down);
	if (!down) return;
	if (ch == ']') jump_part(g_part + 1);
	else if (ch == '[') jump_part(g_part - 1);
	else if (ch == 'g') jump_part(LITH_PART_GAME);
}
