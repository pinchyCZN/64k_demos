/* UKKO - the music, played out of the depacked image.
 *
 * The demo's own driver is a linked-in third-party blob and is
 * bounded, not reconstructed - CLAUDE.md's mission rule, and
 * subsystems/music.txt. What is reproduced here is WHICH bytes
 * get played and WHEN, not how a Scream Tracker module is mixed.
 *
 * ibxm/ac does the mixing. docs/ukko/formats/music.txt.
 */

#include <stdlib.h>
#include <string.h>

#include "ukko.h"
#include "../third_party/ibxm/ibxm.h"

static struct module *g_mod;
static struct replay *g_replay;
static char *g_copy;

/* ibxm renders a TICK at a time, not a count we choose, so its output
 * lands here and Demo_Audio drains whatever the frame asked for. */
static int *g_mix;
static s32 g_mix_cap;       /* in stereo pairs                     */
static s32 g_mix_have;      /* pairs produced and not yet drained  */
static s32 g_mix_pos;       /* pairs already drained               */

/* OURS - hand ibxm the module where it already sits.
 *
 * THE MODULE IS NOT A FILE. It is 22,902 bytes at image offset
 * 0x70380, which Demo_Main stages to 0x9000 before calling the
 * driver's function 7 - subsystems/startup.txt section 5. Nothing
 * is read from disk and nothing is written to it.
 *
 * The bytes are copied because module_load takes a non-const
 * pointer; 22 KB is not worth reasoning about whether it writes.
 */
int Ukko_MusicInit(s32 rate)
{
	struct data d;
	char message[64];

	if (!g_img) return 1;

	g_copy = (char *)malloc(UKKO_S3M_BYTES);
	if (!g_copy) return 1;
	memcpy(g_copy, g_img + UKKO_S3M_OFF, UKKO_S3M_BYTES);

	d.buffer = g_copy;
	d.length = (int)UKKO_S3M_BYTES;
	message[0] = 0;
	g_mod = module_load(&d, message);
	if (!g_mod) {
		free(g_copy);
		g_copy = 0;
		return 1;
	}

	/* THE SPEED KNOB IS THE REPLAY'S SAMPLE RATE. ibxm derives its
	 * tick length from the rate it was built with - (rate * 5) /
	 * (tempo * 2) - so building it at a LOWER rate than the device
	 * actually runs makes every tick shorter and the whole thing
	 * play faster and higher, exactly like a tape. There is no way
	 * to move tempo without pitch. ../ukko_assets.h. */
	g_replay = new_replay(g_mod,
	                      (int)(rate * 100L / UKKO_MUSIC_SPEED),
	                      UKKO_MUSIC_INTERP);
	if (!g_replay) {
		dispose_module(g_mod);
		g_mod = 0;
		free(g_copy);
		g_copy = 0;
		return 1;
	}

	g_mix_cap = (s32)calculate_mix_buf_len(
	                (int)(rate * 100L / UKKO_MUSIC_SPEED));
	g_mix = (int *)malloc((size_t)g_mix_cap * sizeof(int));
	if (!g_mix) {
		Ukko_MusicShutdown();
		return 1;
	}
	g_mix_have = 0;
	g_mix_pos = 0;
	return 0;
}

/* OURS */
void Ukko_MusicShutdown(void)
{
	if (g_replay) dispose_replay(g_replay);
	if (g_mod) dispose_module(g_mod);
	free(g_mix);
	free(g_copy);
	g_replay = 0;
	g_mod = 0;
	g_mix = 0;
	g_copy = 0;
}

/* OURS - fill `frames` stereo pairs. Never blocks.
 *
 * PULL PER FRAME, WHICH IS WHAT THE ORIGINAL DOES TOO. Music_Poll at
 * 6d6f:1ce3 tops up a DMA buffer once a frame and returns at once if
 * the hardware has not moved - subsystems/music.txt section 3. The
 * shape here is the same: produce only what was asked for, keep the
 * remainder, return.
 */
void Ukko_MusicRender(s16 *out, s32 frames)
{
	s32 n = 0;

	if (!g_replay || !g_mix) {
		memset(out, 0, (size_t)frames * 2 * sizeof(s16));
		return;
	}

	while (n < frames) {
		s32 avail, take, i;

		if (g_mix_pos >= g_mix_have) {
			g_mix_have = (s32)replay_get_audio(g_replay, g_mix, 0);
			g_mix_pos = 0;
			if (g_mix_have <= 0) {          /* the song ran out */
				memset(out + n * 2, 0,
				       (size_t)(frames - n) * 2 * sizeof(s16));
				return;
			}
		}
		avail = g_mix_have - g_mix_pos;
		take = frames - n;
		if (take > avail) take = avail;

		for (i = 0; i < take * 2; i++) {
			/* ibxm hands back room-to-spare ints; clamp to s16. */
			int a = g_mix[g_mix_pos * 2 + i];
			if (a > 32767) a = 32767;
			if (a < -32768) a = -32768;
			out[(n * 2) + i] = (s16)a;
		}
		g_mix_pos += take;
		n += take;
	}
}
