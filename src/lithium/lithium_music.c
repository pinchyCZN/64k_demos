/* LITHIUM - the tick chain's tail: the music clock and the fade.
 *
 * ORIGIN img 0x6c8e Tick_Default. docs/lithium/subsystems/music.txt
 * section 6.
 *
 * EVERY SCENE'S TICK HANDLER ENDS BY JUMPING HERE. img 0x628f
 * Demo_TickIsr is the whole interrupt - it saves the registers and
 * jumps through g_TickHandler at img 0x8f64 - and each Tick_* routine
 * ends `JMP 0x6c8e` rather than returning, so the palette fade and
 * the music clock run on every tick whichever scene is up. A port
 * that calls only the scene's own handler has no fade, and with no
 * fade NO PART CAN END: the runners test g_PalFade, not a frame
 * count.
 *
 * THE SAME PIT DIVISOR IS ALSO A 16.16 STEP into img 0x88b3, whose
 * carry far-calls g_OldTimerIsrOff at 18.2065 Hz - the saved BIOS
 * handler, kept fed so the DOS clock survives the fast timer.
 *
 * THE MUSIC IS A CLOCK HERE AND NOTHING ELSE. g_MusicPos is the only
 * thing any scene reads from the replayer, so the stub maintains that
 * one word and Demo_Audio stays silent.
 *
 * [open] NOT THE REPLAYER. img 0x7465, in Snd_ReplayTick's Sound
 * Blaster branch, is what writes g_MusicPos in the original. Nothing
 * of the replayer is transcribed.
 */

#include "lithium.h"

/* g_PalFade img 0x8f55, g_PalFadeTarget2 img 0x8f57, g_PalFadeShift
 * img 0x8f78 and its reload img 0x8f84. */
static s32 g_pal_fade;
static s32 g_pal_fade_target;
static s32 g_pal_fade_shift;
static s32 g_pal_fade_shift_reload = 0xa;

/* g_MusicPos img 0x8332 and g_MusicPosLast img 0x8fc2. */
static s32 g_music_pos;
static s32 g_music_pos_last;
static s32 g_music_tick;

/* OURS - where each order begins, in ticks. An order is NOT a fixed
 * length: effect A resets the speed mid-song and effect C ends a
 * pattern early, so the boundaries are walked once at startup.
 * subsystems/music.txt section 5.2. */
static s32 g_order_start[LITH_MOD_MAX_ORDERS + 1];
static s32 g_order_count;

/* g_FlashCountdown img 0x8fe3 and g_PalFlash img 0x8fe4. */
static s32 g_flash_countdown;
static s32 g_pal_flash;

/* OURS - 1000:7882 writes the cursor 1 at tick 4, so every boundary
 * carries this offset. subsystems/music.txt section 5.2. */
#define LITH_MUSIC_START_TICK 4


/* OURS - walk the order list and total each pattern's ticks.
 *
 * TEMPO SETS THE WALL CLOCK AND SPEED THE TICK COUNT, so this fixes
 * the boundaries without reference to any frame rate. Effect A
 * (1000:76c1) carries forward into later orders, which is why the
 * speed is a running value and not re-read per pattern; effects B
 * and C (1000:76d0 and 0x76d3) end the pattern on the row they sit
 * on.
 *
 * THE OPENING'S ORDERS ARE 384 TICKS AND THE REST ARE NOT. Entry 6
 * is pattern 3, which carries A3 at row 0 and the module's only
 * pattern break at row 15, so that entry is 48 ticks and every one
 * after it is 192 - half the opening's. Assuming the stored speed
 * throughout puts the flyover 908 ticks late and halves the rate of
 * its credit roll.
 *
 * EVERY ENTRY IS WALKED, ORDER 0 INCLUDED - the four ticks that look
 * like its length are the start offset above.
 *
 * A pattern number past the last pattern - 0xff - ends the list. */
static void music_build_timeline(void)
{
	const u8 *m = g_lith_img + LITH_MOD_BASE;
	s32 ordnum, insnum, patnum, speed, o, tick;
	u32 ppara;

	g_order_count = 0;
	g_order_start[0] = LITH_MUSIC_START_TICK;
	if (!g_lith_img) return;

	ordnum = m[LITH_MOD_ORDNUM] | (m[LITH_MOD_ORDNUM + 1] << 8);
	insnum = m[LITH_MOD_INSNUM] | (m[LITH_MOD_INSNUM + 1] << 8);
	patnum = m[LITH_MOD_PATNUM] | (m[LITH_MOD_PATNUM + 1] << 8);
	speed = m[LITH_MOD_SPEED];
	if (!speed) speed = 6;
	if (ordnum > LITH_MOD_MAX_ORDERS) ordnum = LITH_MOD_MAX_ORDERS;
	ppara = (u32)LITH_MOD_ORDERS + (u32)ordnum + 2u * (u32)insnum;

	tick = LITH_MUSIC_START_TICK;
	for (o = 0; o < ordnum; o++) {
		s32 pat = m[LITH_MOD_ORDERS + o];
		u32 off;
		s32 end, row, brk, n, i;
		s32 setspeed[LITH_MOD_ROWS], brkrow[LITH_MOD_ROWS];

		g_order_start[o] = tick;
		g_order_count = o + 1;
		if (pat >= patnum) break;

		off = 16u * (u32)(m[ppara + 2 * pat]
		                  | (m[ppara + 2 * pat + 1] << 8));
		end = (s32)(off + 2
		            + (u32)(m[off] | (m[off + 1] << 8)));
		for (i = 0; i < LITH_MOD_ROWS; i++) {
			setspeed[i] = 0; brkrow[i] = 0;
		}
		row = 0;
		i = (s32)off + 2;
		while (i < end && row < LITH_MOD_ROWS) {
			s32 w = m[i++];

			if (!w) { row++; continue; }
			if (w & 32) i += 2;
			if (w & 64) i += 1;
			if (w & 128) {
				s32 c = m[i], v = m[i + 1];

				i += 2;
				if (c == 1 && v) setspeed[row] = v;
				if (c == 2 || c == 3) brkrow[row] = 1;
			}
		}
		brk = LITH_MOD_ROWS - 1;
		for (i = 0; i < LITH_MOD_ROWS; i++)
			if (brkrow[i]) { brk = i; break; }
		n = 0;
		for (i = 0; i <= brk; i++) {
			if (setspeed[i]) speed = setspeed[i];
			n += speed;
		}
		if (o == LITH_ORDER_STRAY_BREAK) n = LITH_ORDER_STRAY_TICKS;
		tick += n;
	}
	g_order_start[g_order_count] = tick;
}

/* OURS - g_MusicPos at a tick, and IT IS THE CURSOR: 1000:7875 reads
 * the list entry then increments, so img 0x8332 is one AHEAD of what
 * sounds. Reading it as the entry playing is the trap, and it hides
 * itself - every boundary then looks one entry early.
 * subsystems/music.txt section 5.2. */
static s32 music_pos_at(s32 tick)
{
	s32 o;

	for (o = g_order_count - 1; o >= 0; o--)
		if (tick >= g_order_start[o]) return o + 1;
	return 0;
}

/* THE FADE STARTS AT ITS STORED img 0x8f55 OF -256, which is the
 * opening's fade-up out of black - effects/rockintro.txt 7.
 *
 * READING THE HEADER LATE IS SAFE HERE AND WOULD NOT BE IN THE
 * ORIGINAL: Mesh_Load's vertex records are written over segment
 * 0x2ad2, which is the module's own base - formats/mesh.txt section
 * 5. Lith_MeshLoad builds a LithMesh instead and never writes to the
 * image, so these bytes survive. */
void Lith_MusicStart(void)
{
	music_build_timeline();
	Lith_MusicSeek(0);
}

/* OURS - put the clock at a tick, for the part jump in
 * lithium_demo.c. The LATCH IS THE POINT: leaving g_MusicPosLast
 * behind makes the first tick after the jump look like a beat, and a
 * scene that cuts its camera on one - the battle at img 0x1fe9 -
 * fires it immediately.
 *
 * It takes the fade machinery back to its start too, because
 * g_PalFlash and the shift reload outlive a scene and a jump that
 * carried the greetings' strobe into the opening would be a bug
 * nobody could place. */
void Lith_MusicSeek(s32 tick)
{
	g_music_tick = tick;
	g_music_pos = music_pos_at(g_music_tick);
	g_music_pos_last = g_music_pos;

	g_pal_fade = -0x100;
	g_pal_fade_target = 0;
	g_pal_fade_shift = 0xa;         /* img 0x8f78 */
	g_pal_fade_shift_reload = 0xa;
	g_pal_flash = 0;                /* img 0x8fe4 */
	g_flash_countdown = 0;

	Lith_MusicSeekAudio(tick);
}

/* OURS - put the REPLAYER at a tick, leaving the clock alone.
 *
 * IT WALKS FROM THE START, and the cost buys exactness. Landing on
 * the order with replay_set_sequence_pos and stepping the remainder
 * gets within a row, but no nearer: ibxm carries its own phase
 * between the row counter and the tick counter, and the arithmetic
 * that would cancel it is a guess at another program's convention.
 * Rendering from tick zero is not a guess - it is the same sequence
 * of calls the true run made, so the replayer arrives in the state
 * the true run had, tempo and speed changes included.
 *
 * THE STRAY BREAK HAS TO BE TAKEN ON THE WAY. ibxm plays order 50 in
 * full and the demo does not - lithium.h LITH_ORDER_STRAY_BREAK - so
 * a walk that ignored it would arrive 383 ticks late for anything
 * past the greetings.
 *
 * IT IS ALSO THE SECOND HALF OF A SEEK THAT REPLAYS. jump_part runs
 * Demo_Frame to reach a part that cannot be entered cold, and
 * Demo_Frame moves the clock without moving the replayer, so the
 * audio has to be placed again once the picture has arrived. */
void Lith_MusicSeekAudio(s32 tick)
{
	s32 k, brk;

	if (g_order_count < 1) return;
	brk = (LITH_ORDER_STRAY_BREAK < g_order_count)
	      ? g_order_start[LITH_ORDER_STRAY_BREAK] : -1;

	Lith_MusicSeekOrder(0);
	for (k = 0; k < tick; k++) {
		if (brk >= 0 && k > brk
		        && Lith_MusicSeqPos() == LITH_ORDER_STRAY_BREAK)
			Lith_MusicSeekOrder(LITH_ORDER_STRAY_BREAK + 1);
		Lith_MusicAdvanceTicks(1);
	}
}

/* OURS - the tick the order list runs out on, which
 * music_build_timeline already parks past the last entry. Zero until
 * the timeline is built, so a caller can tell. */
s32 Lith_MusicEndTick(void)
{
	return g_order_count > 0 ? g_order_start[g_order_count] : 0;
}

s32 Lith_MusicPos(void) { return g_music_pos; }

/* g_MusicPosLast img 0x8fc2 IS SHARED - the credit roll takes the
 * change at img 0x6419 before Tick_Default tests the same word at
 * img 0x6cbc. effects/text.txt 7.2. */
s32 Lith_MusicPosLast(void) { return g_music_pos_last; }
void Lith_MusicPosLatch(void) { g_music_pos_last = g_music_pos; }

/* OURS - img 0x0794 sets g_PalFlash and img 0x09b0 clears it, both
 * from the bump scene, so it needs a way in. */
void Lith_PalFlashSet(s32 v) { g_pal_flash = v; }
s32 Lith_PalFlash(void) { return g_pal_flash; }

/* OURS - img 0x6ad6 compares the two directly; a scene wants the
 * answer rather than both words. */
s32 Lith_MusicPosChanged(void)
{
	return g_music_pos != g_music_pos_last;
}
s32 Lith_PalFade(void) { return g_pal_fade; }

/* OURS - img 0x638a and its like write the target directly. */
void Lith_PalFadeTo(s32 target) { g_pal_fade_target = target; }

/* g_PalFadeShift img 0x8f78 ALONE - the tunnel's fade out sets
 * the shift and the target and nothing else, img 0x0480, so the
 * reload still holds whatever armed the current fade. */
void Lith_PalFadeShiftSet(s32 n) { g_pal_fade_shift = n; }

/* g_PalFadeShiftReload img 0x8f84 - set with the shift at 0x6738. */
void Lith_PalFadeShiftReloadSet(s32 n) { g_pal_fade_shift_reload = n; }

/* g_FlashCountdown img 0x8fe3 - the battle parks it at 0xff so the
 * beat strobe cannot fire for 255 ticks, img 0x204a. */
void Lith_PalFlashCountdownSet(s32 n) { g_flash_countdown = n; }

/* OURS - the transitions set all four at once, and the RELOAD is not
 * the shift: img 0x1a29 arms 3 for whatever the scene fades next
 * while img 0x1a2f runs the current fade at 8. */
void Lith_PalFadeSet(s32 fade, s32 target, s32 shift, s32 reload)
{
	g_pal_fade = fade;
	g_pal_fade_target = target;
	g_pal_fade_shift = shift;
	g_pal_fade_shift_reload = reload;
}

/* OURS - the stub's replayer. One tick per call, which is the
 * original's ratio: order 4 began at tick 1156 and order 5 at 1540,
 * exactly 64 x 6 apart, so one interrupt is one music tick. */
static void music_tick(void)
{
	if (g_order_count < 1) return;
	g_music_tick++;
	g_music_pos = music_pos_at(g_music_tick);
}

/* ORIGIN img 0x6c8e Tick_Default.
 *
 * THE FADE IS WRITTEN AS EXPONENTIAL AND COMES OUT LINEAR. The step
 * is (target - fade) >> 10, rounded away from zero only when the
 * difference is positive, so a target of -256 from 0 gives -1 every
 * tick for the whole 256 - the shift never bites because the gap
 * never reaches 1024. That is why a part ends a round 161 ticks after
 * its fade is armed instead of easing out.
 *
 * g_PalFlash IS A BEAT STROBE, not part of the fade: on a 24-tick
 * countdown and on every change of g_MusicPos it drags g_PalFade back
 * up to g_PalFlash and re-randomises the bump light. Both paths are
 * gated on g_PalFlash being non-zero and the opening leaves it zero.
 *
 * [open] img 0x0c44 Bump_RandomiseLight, the other half of that
 * strobe, is not transcribed - nothing reaches it until a scene sets
 * g_PalFlash. */
void Lith_TickDefault(void)
{
	s32 diff, step;

	g_flash_countdown = (g_flash_countdown - 1) & 0xff;
	if (!g_flash_countdown && g_pal_flash) {
		g_flash_countdown = 0x18;
		Lith_BumpRandomiseLight();      /* img 0x0c44 */
		if (g_pal_fade < g_pal_flash) g_pal_fade = g_pal_flash;
	}
	if (g_music_pos_last != g_music_pos) {
		g_music_pos_last = g_music_pos;
		if (g_pal_flash) {
			g_flash_countdown = 0x18;
			if (g_pal_fade < g_pal_flash) g_pal_fade = g_pal_flash;
		}
	}

	/* img 0x6ce6 - and the arithmetic is on WORDS. */
	diff = (s32)(s16)(g_pal_fade_target - g_pal_fade);
	step = diff >> g_pal_fade_shift;
	if (diff > 0) step++;
	g_pal_fade = (s32)(s16)(g_pal_fade + step);

	/* img 0x6d08 - the DAC is refreshed only while the fade moves;
	 * img 0x6d0d reloads the shift from img 0x8f84 once it has
	 * arrived. */
	if (g_pal_fade != g_pal_fade_target) Lith_PalRefresh();
	else g_pal_fade_shift = g_pal_fade_shift_reload;

	music_tick();
}
