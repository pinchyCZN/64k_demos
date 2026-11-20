/* XCURSION - the music, played out of the depacked image.
 *
 * THE DEMO'S OWN REPLAYER IS ALREADY IN THE PORT and is not this
 * file. xcursion_seq.c is img 0x0c68 transcribed, and it stays,
 * because the camera is flown by channel volumes - the sequencer is
 * an input to the visuals, not an output. What this file adds is
 * the one thing that transcription does not do: mixing samples.
 *
 * ibxm/ac does that. The two run side by side off the same bytes,
 * and subsystems/music.txt section 6 is why they stay in step.
 */

#include <stdlib.h>
#include <string.h>

#include "xcursion.h"
#include "../third_party/ibxm/ibxm.h"

static struct module *g_mod;
static struct replay *g_replay;
static char *g_copy;

/* ibxm renders a TICK at a time, not a count we choose, so its
 * output lands here and the render drains what the frame asked. */
static int *g_mix;
static s32 g_mix_cap;       /* in stereo pairs                     */
static s32 g_mix_have;      /* pairs produced and not yet drained  */
static s32 g_mix_pos;       /* pairs already drained               */

/* OURS - hand ibxm the module where it already sits, 55,787 bytes at
 * image offset 0x0cda0. Nothing is read from disk.
 *
 * NO SPEED CORRECTION HERE, unlike ukko's. The demo ignores the
 * module's tempo byte entirely - its sequencer runs off the 100 Hz
 * timer halved, a fixed 50 ticks a second - and this module's tempo
 * is 125, which is 50 ticks a second exactly. The two clocks agree
 * by arithmetic rather than by luck, so ibxm is built at the
 * device's own rate. subsystems/music.txt section 6.
 *
 * The bytes are copied because module_load takes a non-const
 * pointer and the depacked image is shared with everything else.
 */
void Xcur_MusicInit(s32 rate)
{
	struct data d;
	char message[64];

	if (!g_img) return;

	g_copy = (char *)malloc(XCUR_S3M_BYTES);
	if (!g_copy) return;
	memcpy(g_copy, g_img + XCUR_S3M_OFF, XCUR_S3M_BYTES);

	d.buffer = g_copy;
	d.length = (int)XCUR_S3M_BYTES;
	message[0] = 0;
	g_mod = module_load(&d, message);
	if (!g_mod) {
		free(g_copy);
		g_copy = 0;
		return;
	}

	g_replay = new_replay(g_mod, (int)rate, XCUR_MUSIC_INTERP);
	if (!g_replay) {
		dispose_module(g_mod);
		g_mod = 0;
		free(g_copy);
		g_copy = 0;
		return;
	}

	g_mix_cap = (s32)calculate_mix_buf_len((int)rate);
	g_mix = (int *)malloc((size_t)g_mix_cap * sizeof(int));
	if (!g_mix) {
		Xcur_MusicShutdown();
		return;
	}
	g_mix_have = 0;
	g_mix_pos = 0;
}

/* OURS */
void Xcur_MusicShutdown(void)
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

/* OURS - fill `frames` stereo pairs. Never blocks. Silence until the
 * module loads, so a missing image is quiet rather than fatal. */
void Xcur_MusicRender(s16 *out, s32 frames)
{
	s32 n = 0;

	if (!out || frames <= 0) return;
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
