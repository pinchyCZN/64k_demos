/* LITHIUM - the module, expanded and played.
 *
 * ORIGIN img 0x8007 Snd_ExpandSamples. The replayer itself is a
 * linked-in third-party blob and is bounded rather than
 * reconstructed - CLAUDE.md's mission rule, and
 * subsystems/music.txt. What is reproduced here is HOW THE SAMPLES
 * ARE STORED, which is the demo's own work; ibxm does the mixing.
 *
 * THE SAMPLES ARE 3-BIT ADPCM AND THE IMAGE HOLDS THE PACKED FORM.
 * Sliced straight out, the module reads as five silent instruments,
 * five of noise and one of PCM - none of which is damage. The demo
 * expands the lot before it draws a frame, in Demo_Main's very first
 * call, and only the expansion is ever played. ../README.txt 6.
 *
 * ONE STREAM COVERS EVERY INSTRUMENT. There is a single four-byte
 * header, at the FIRST instrument's sample pointer, and the decode
 * runs from there to the end of the 64 KB with the instrument
 * boundaries never consulted - 12,030 bytes of stream becoming
 * 32,080 of sample, which is three bits each.
 *
 * TWO BUFFERS ARE REQUIRED, NOT PREFERRED. The original reads the
 * stream through DS and writes through ES, and the stream lives
 * INSIDE the area the output fills: the write cursor runs 2.67 times
 * faster than the read cursor and passes it after 36 bytes. Decoding
 * in place is correct until exactly there and then yields noise that
 * still looks like a decode.
 */

#include <stdlib.h>
#include <string.h>

#include "lithium.h"
#include "../third_party/ibxm/ibxm.h"

static struct module *g_mod;
static struct replay *g_replay;
static char *g_module;

/* ibxm renders a TICK at a time rather than a count we choose, so its
 * output lands here and Demo_Audio drains what the frame asked for. */
static int *g_mix;
static s32 g_mix_cap;           /* stereo pairs                    */
static s32 g_mix_have;          /* produced and not yet drained    */
static s32 g_mix_pos;           /* already drained                 */
static s32 g_mix_ticks;         /* ibxm ticks rendered so far      */
static s32 g_stray_taken;       /* the order-50 break, taken once  */

/* ORIGIN img 0x8007 Snd_ExpandSamples - the 64 KB copy, the decode,
 * and the raw tail.
 *
 * THE LOOP ENDS ON A SEGMENT WRAP, not on a length: img 0x80a2 runs
 * until the 16-bit write cursor returns to zero, so the end of the
 * segment is the end of the data.
 *
 * THE RAW TAIL IS ONE INSTRUMENT. img 0x80ad copies [base] bytes
 * from base + 4 over the decoded output at [base + 2] - eighteen
 * bytes that were never compressed. */
static void snd_expand_samples(u8 *dst, const u8 *src)
{
	s32 index, acc, bits, value;
	u32 di, si, base, count, raw_at;
	s32 steps[16], delta[4];
	s32 i;

	for (i = 0; i < 16; i++)
		steps[i] = (s32)(s16)RD_U16(g_lith_img + LITH_ADPCM_STEP + i * 2);
	for (i = 0; i < 4; i++)
		delta[i] = (s32)(s16)RD_U16(g_lith_img + LITH_ADPCM_DELTA + i * 2);

	memcpy(dst, src, LITH_SEG_BYTES);       /* img 0x8018 */

	/* img 0x801b - the first instrument's sample pointer, reached
	 * through the parapointer table rather than assumed. */
	base = (u32)RD_U16(src + 0x60 + RD_U16(src + 0x20));
	base = (u32)RD_U16(src + base * 16 + 0x0e) << 4;
	di = base;
	si = (base + RD_U16(src + base) + 4) & 0xffff;  /* img 0x8030 */

	index = 0x14;                           /* img 0x8035 */
	acc = 0;
	bits = 0;
	value = 0x80;
	for (;;) {
		s32 mag, d;

		if (bits < 3) {                 /* img 0x803e */
			s32 word = (s32)RD_U16(src + si);

			si = (si + 1) & 0xffff;
			acc = (acc | ((word << bits) & 0xffff)) & 0xffff;
			bits += 8;
		}
		mag = acc & 3;
		d = mag * steps[index >> 1];    /* img 0x8052 */
		value += (acc & 4) ? -d : d;    /* img 0x805c */
		if (value < 0) value = 0;
		if (value > 0xff) value = 0xff; /* img 0x806a */
		dst[di] = (u8)value;            /* img 0x8075 */
		if (!index && !value) index = 0x14;      /* img 0x8080 */
		index += delta[mag];            /* img 0x808a */
		if (index < 0) index = 0;
		if (index > 0x1e) index = 0x1e;
		acc >>= 3;
		bits -= 3;
		di = (di + 1) & 0xffff;
		if (!di) break;                 /* img 0x80a2 */
	}

	count = (u32)RD_U16(src + base);        /* img 0x80a5 */
	raw_at = (u32)RD_U16(src + base + 2);
	memcpy(dst + raw_at, src + base + 4, count);    /* img 0x80ad */
}

/* OURS - the furthest byte any pattern or sample reaches. An S3M
 * carries no total length, and ibxm wants one. */
static s32 module_end(const u8 *m)
{
	s32 orders = (s32)RD_U16(m + 0x20);
	s32 ins = (s32)RD_U16(m + 0x22);
	s32 pat = (s32)RD_U16(m + 0x24);
	s32 ins_at = 0x60 + orders;
	s32 pat_at = ins_at + ins * 2;
	s32 end = pat_at + pat * 2;
	s32 i;

	for (i = 0; i < ins; i++) {
		s32 para = (s32)RD_U16(m + ins_at + i * 2);
		s32 hdr, at;

		if (!para) continue;
		hdr = para * 16;
		at = (s32)(((u32)RD_U16(m + hdr + 0x0e)
		            | ((u32)m[hdr + 0x0d] << 16)) * 16);
		at += (s32)RD_U32(m + hdr + 0x10);
		if (at > end) end = at;
	}
	for (i = 0; i < pat; i++) {
		s32 para = (s32)RD_U16(m + pat_at + i * 2);
		s32 at;

		if (!para) continue;
		at = para * 16;
		at += 2 + (s32)RD_U16(m + at);
		if (at > end) end = at;
	}
	return end;
}

/* OURS - expand the module out of the image and hand it to ibxm.
 *
 * THE MODULE IS NOT A FILE and nothing is read from disk: the demo
 * has no file I/O at all. module_load takes a non-const pointer, and
 * the buffer has to outlive the call anyway because the expansion
 * writes into it. */
int Lith_MusicInit(s32 rate)
{
	struct data d;
	char message[64];
	s32 len;

	if (!g_lith_img) return 1;

	g_module = (char *)malloc(LITH_SEG_BYTES);
	if (!g_module) return 1;
	snd_expand_samples((u8 *)g_module, g_lith_img + LITH_MODULE_AT);
	len = module_end((const u8 *)g_module);

	d.buffer = g_module;
	d.length = (int)len;
	message[0] = 0;
	g_mod = module_load(&d, message);
	if (!g_mod) {
		Lith_MusicShutdown();
		return 1;
	}
	g_replay = new_replay(g_mod, LITH_IBXM_RATE, LITH_MUSIC_INTERP);
	if (!g_replay) {
		Lith_MusicShutdown();
		return 1;
	}
	g_mix_cap = (s32)calculate_mix_buf_len((int)rate);
	g_mix = (int *)malloc((size_t)g_mix_cap * sizeof(int));
	if (!g_mix) {
		Lith_MusicShutdown();
		return 1;
	}
	g_mix_have = 0;
	g_mix_pos = 0;
	g_mix_ticks = 0;
	g_stray_taken = 0;
	return 0;
}

/* OURS - the expanded module, for dumping it or checking it against
 * what the demo builds in memory. */
const u8 *Lith_MusicModule(s32 *len)
{
	if (len) *len = g_module ? module_end((const u8 *)g_module) : 0;
	return (const u8 *)g_module;
}

/* OURS */
void Lith_MusicShutdown(void)
{
	if (g_replay) dispose_replay(g_replay);
	if (g_mod) dispose_module(g_mod);
	free(g_mix);
	free(g_module);
	g_replay = 0;
	g_mod = 0;
	g_mix = 0;
	g_module = 0;
}

/* ORIGIN img 0x76d3, reached through the overrun that
 * LITH_ORDER_STRAY_BREAK names. ibxm carries no such defect and
 * plays order 50 in full, so without this the audio falls 383 ticks
 * - six and a half seconds - behind the picture from the greetings
 * on, and the finale's music cue lands long after the figure. The
 * tempo reset replay_set_sequence_pos performs costs nothing here
 * because the order it lands on carries T144 on row 0. */
static void snd_take_stray_break(void)
{
	if (g_stray_taken || !g_replay) return;
	if (replay_get_sequence_pos(g_replay) != LITH_ORDER_STRAY_BREAK)
		return;
	if (Lith_MusicPos() - 1 <= LITH_ORDER_STRAY_BREAK) return;
	g_stray_taken = 1;
	replay_set_sequence_pos(g_replay, LITH_ORDER_STRAY_BREAK + 1);
}

/* OURS - fill `frames` stereo pairs. Never blocks.
 *
 * PULL PER FRAME, WHICH IS WHAT THE ORIGINAL DOES TOO: Snd_ReplayTick
 * tops up a DMA buffer from the timer interrupt and returns at once
 * if the hardware has not moved - subsystems/music.txt section 1. */
void Lith_MusicRender(s16 *out, s32 frames)
{
	s32 n = 0;

	if (!g_replay || !g_mix) {
		memset(out, 0, (size_t)frames * 2 * sizeof(s16));
		return;
	}
	snd_take_stray_break();
	while (n < frames) {
		s32 avail, take, i;

		if (g_mix_pos >= g_mix_have) {
			g_mix_have = (s32)replay_get_audio(g_replay, g_mix, 0);
				g_mix_ticks++;
			g_mix_pos = 0;
			if (g_mix_have <= 0) {
				memset(out + n * 2, 0,
				       (size_t)(frames - n) * 2 * sizeof(s16));
				return;
			}
		}
		avail = g_mix_have - g_mix_pos;
		take = frames - n;
		if (take > avail) take = avail;
		for (i = 0; i < take * 2; i++) {
			int a = g_mix[g_mix_pos * 2 + i];

			if (a > 32767) a = 32767;
			if (a < -32768) a = -32768;
			out[n * 2 + i] = (s16)a;
		}
		g_mix_pos += take;
		n += take;
	}
}

/* OURS - move the replayer to an order, for the part jump.
 *
 * THE BUFFERED TICK HAS TO GO WITH IT or the first thing heard after
 * the jump is the tail of the tick the seek left behind. The stray
 * break is re-armed or retired by the order landed on, so a jump
 * back past order 50 takes it again. */
void Lith_MusicSeekOrder(s32 order)
{
	if (!g_replay) return;
	if (order < 0) order = 0;
	replay_set_sequence_pos(g_replay, (int)order);
	g_mix_have = 0;
	g_mix_pos = 0;
	g_stray_taken = (order > LITH_ORDER_STRAY_BREAK);
}

/* OURS - walk the replayer n module ticks on, discarding what it
 * mixes. replay_get_audio renders a tick and THEN advances, so n of
 * these leave the replayer exactly where n of the calls Demo_Audio
 * makes in play would - which is what lets a seek be exact rather
 * than near. The mask mutes every channel: ibxm shifts it down one
 * channel at a time, so a lone 1 would mute only the first. */
void Lith_MusicAdvanceTicks(s32 n)
{
	if (!g_replay || !g_mix) return;
	while (n-- > 0) {
		replay_get_audio(g_replay, g_mix, 0xffff);
		g_mix_ticks++;
	}
	g_mix_have = 0;
	g_mix_pos = 0;
}

/* OURS - how many module ticks the replayer has actually rendered.
 * One demo tick is one module tick in the original, where a single
 * PIT interrupt drives both, so this should track Dbg_TickCount
 * exactly; the gap is the audio's drift against the picture. */
s32 Lith_MusicTicks(void)
{
	return g_mix_ticks;
}

/* OURS - where the replayer actually is, for checking the audio
 * against the scene clock. Diagnostic only. */
s32 Lith_MusicSeqPos(void)
{
	return g_replay ? (s32)replay_get_sequence_pos(g_replay) : 0;
}

/* OURS */
s32 Lith_MusicRow(void)
{
	return g_replay ? (s32)replay_get_row(g_replay) : 0;
}
