/* XCURSION - the module sequencer, channel state only.
 *
 * ORIGIN img 0x0c68. The demo's own S3M replayer, ported for what it
 * drives rather than for what it plays: no mixer, no samples, just
 * the eight channel slots at img 0x9428 and their values at img
 * 0x988d. effects/camera.txt says why that is the useful half.
 *
 * THE CAMERA FLIES ON THE MUSIC. Mode 0 reads two of those values and
 * nothing else sets its speed, so the sequencer has to run for the
 * landscape to move at all.
 */

#include <string.h>

#include "xcursion.h"

/* img 0x9428 opcode + parameter, img 0x988d the value. Eight slots:
 * img 0x0d21 drops any channel whose slot offset reaches 0x20. */
typedef struct { u8 op, par; s32 value; s32 age, step; } XcurChan;

static XcurChan g_ch[XCUR_SEQ_CHANNELS];
static u32 g_mod;                       /* img 0x0cda0, the module   */
static s32 g_order;                     /* img 0x9905               */
static s32 g_pat, g_len;                /* img 0x9901, img 0x9869   */
static s32 g_row;                       /* img 0x9903               */
static s32 g_tick, g_speed, g_half;     /* img 0xa87f, 0xa599, 0xa880 */
static s32 g_ordnum, g_patnum, g_ordofs, g_patofs;
static s32 g_insnum, g_insofs;
static s32 g_rowno;             /* img 0x9915 */
static s32 g_flash;             /* img 0xa88d */
static s32 g_fire;              /* img 0xa88c */
static s32 g_newpat;            /* img 0xa88e */
static s32 g_done;

static u32 rd16(u32 o) { return g_img[o] | ((u32)g_img[o + 1] << 8); }

/* ORIGIN img 0x2200. g_MusicPos INCs at 0x2205 and must never
 * wrap - music.txt 6. */
static void next_pattern(void)
{
	s32 e;
	u32 p;

	if (g_ordnum <= 0) return;
	e = (s32)g_img[(u32)(g_ordofs + g_order)];
	g_order++;
	if (e >= g_patnum) {
		g_done = 1;
		return;
	}
	p = g_mod + rd16((u32)(g_patofs + 2 * e)) * 16;
	g_len = (s32)rd16(p);
	g_pat = (s32)(p + 2 - g_mod);
	g_row = 0;
	g_rowno = 0;        /* img 0x222d */
	g_newpat = 8;       /* img 0x2234 */
}

/* OURS - the S3M header parse the replayer of this file's ORIGIN
 * needs. The routine the demo does it in is not identified.
 *
 * THE ORDER LIST IS AT +0x60 AND THE 32-BYTE CHANNEL TABLE AT +0x40,
 * BEFORE it, not between it and the header - effects/camera.txt 3. */
void Xcur_SeqInit(u32 mod)
{
	u32 o;
	s32 k;

	memset(g_ch, 0, sizeof g_ch);
	/* img 0x988d's own static value, every slot. The two the camera
	 * reads cancel against the 0x800 at img 0x0dd3, so the landscape
	 * starts STILL and the music is what sets it moving. */
	for (k = 0; k < XCUR_SEQ_CHANNELS; k++) g_ch[k].value = 0x0400;
	g_mod = mod;
	g_ordnum = (s32)rd16(mod + 0x20);
	g_patnum = (s32)rd16(mod + 0x24);
	g_speed = g_img[mod + 0x31];
	g_order = 0; g_tick = 0; g_half = 0; g_done = 0;
	o = mod + 0x60;
	g_ordofs = (s32)o;                       o += (u32)g_ordnum;
	g_insnum = (s32)rd16(mod + 0x22);
	g_insofs = (s32)o;                       o += 2 * (u32)g_insnum;
	g_patofs = (s32)o;
	next_pattern();
}

/* ORIGIN img 0x0cbc - the commands of one row, up to its 0 byte.
 *
 * S3M's packed row, and the three payload bits are read in this order
 * at img 0x0cf3, 0x0d38 and 0x0d63. */
static void read_row(void)
{
	s32 guard = 0;

	for (;;) {
		s32 ch, al;

		/* img 0x0cca FALLS THROUGH into the read: the new pattern's
		 * first row is taken in the SAME tick, not the next one.
		 * Returning here instead spends an extra tick per pattern,
		 * which the camera integrates into a path that drifts. */
		if (g_row >= g_len - 2) {
			if (++guard > 2) return;
			next_pattern();
		}
		al = g_img[(u32)(g_mod + g_pat + g_row)];
		g_row++;
		if (al == 0) return;
		ch = al & 0x1f;
		if (al & 0x20) {                     /* note and instrument  */
			/* ORIGIN img 0x226e. A NOTE RESETS THE VOLUME to the
			 * instrument's own, and that is where almost all of it
			 * comes from - the volume column is rare in this module,
			 * so without this every channel stays silent and the
			 * landscape never moves. */
			s32 inst = g_img[(u32)(g_mod + g_pat + g_row + 1)];

			/* img 0x0d03 - ONE INSTRUMENT DRIVES THE PLASMA'S FLASH.
			 * The decoder raises a flag when instrument 0x10 is
			 * triggered and mode 6 reads it, so the white hits are a
			 * note in the module rather than a timer. */
			if (inst == 0x10) g_flash = 8;
			if (inst == 0x09) g_fire = 8;       /* img 0x0d13 */

			if (ch < XCUR_SEQ_CHANNELS && inst > 0 && inst <= g_insnum) {
				u32 ih = g_mod
				       + rd16((u32)(g_insofs + 2 * (inst - 1))) * 16;
				if (g_img[ih] != 0) {   /* img 0x2267 - type 0 is none */
					s32 note, per, t, q;

					g_ch[ch].value =
					    ((s32)g_img[ih + 0x1c] << 8) + 0x400;
					/* img 0x0d26 - EVERY NOTE ARMS THIS CHANNEL'S
					 * DRIP for sixteen ticks, and img 0x228e works out
					 * the sample step the drip's column comes from.
					 * The music is the only thing driving the water. */
					g_ch[ch].age = 0x10;
					note = g_img[(u32)(g_mod + g_pat + g_row)];
					per = (s32)rd16((u32)(XCUR_PERIOD_TABLE
					                      + (note & 0xf) * 2));
					t = (s32)(((u32)per * 0x20ab0UL) >> (note >> 4));
					q = (s32)g_img[ih + 0x20]
					    | ((s32)g_img[ih + 0x21] << 8);
					if (q > 0) {
						q = (s32)((u32)t / (u32)q) & 0xffff;
						if (q > 0)
							g_ch[ch].step =
							    (s32)((0xda7600UL / (u32)q) << 2);
					}
				}
			}
			g_row += 2;
		}
		if (al & 0x40) {                     /* img 0x0d52           */
			if (ch < XCUR_SEQ_CHANNELS)
				g_ch[ch].value =
				    ((s32)g_img[(u32)(g_mod + g_pat + g_row)] << 8) + 0x400;
			g_row++;
		}
		if (al & 0x80) {                     /* img 0x0d78           */
			if (ch < XCUR_SEQ_CHANNELS) {
				g_ch[ch].op = g_img[(u32)(g_mod + g_pat + g_row)];
				if (g_img[(u32)(g_mod + g_pat + g_row + 1)])
					g_ch[ch].par = g_img[(u32)(g_mod + g_pat + g_row + 1)];
			}
			g_row += 2;
		} else if (ch < XCUR_SEQ_CHANNELS) {
			g_ch[ch].op = 0;                 /* img 0x0d9b           */
		}
	}
}

/* ORIGIN img 0x2130 - command D, the volume slide, and the only one
 * the camera needs.
 *
 * A NORMAL SLIDE SKIPS TICK 0 AND A FINE SLIDE ONLY RUNS THERE - img
 * 0x2161 returns when the row counter IS zero, img 0x2140 and 0x2153
 * when it is not. Inverting either costs the slide most of its ticks
 * and the volume never reaches the floor the music wrote it toward. */
static void slide(XcurChan *c, int tick0)
{
	s32 down = c->par & 0x0f, up = (c->par >> 4) & 0x0f;

	if (down == 0x0f) {                 /* img 0x213c - fine up      */
		if (!tick0) return;
		down = 0;
	} else if (up == 0x0f) {            /* img 0x214e - fine down    */
		if (!tick0) return;
		up = 0;
	} else if (tick0) {
		return;                         /* img 0x2161                */
	}

	if (down) up = 0;                   /* img 0x216f - down wins    */
	c->value += up << 8;
	if (c->value > 0x4400) c->value = 0x4400;
	c->value -= down << 8;
	if (c->value < 0x0400) c->value = 0x0400;
}

/* ORIGIN img 0x0c68 - one timer tick of the sequencer.
 *
 * IT RUNS ON EVERY OTHER TICK, not every one: img 0x0c6d tests bit 0
 * of its own counter and leaves through the animation dispatch on the
 * tick it skips. The effects run BEFORE the counter advances, so a
 * row's first tick is the one that reads it. */
void Xcur_SeqTick(void)
{
	s32 i, tick0;

	if (!g_img || !g_mod || g_done) return;
	g_half ^= 1;
	if (!g_half) return;

	tick0 = (g_tick == 0);
	for (i = 0; i < XCUR_SEQ_CHANNELS; i++) {
		switch (g_ch[i].op) {
		case 0x01: g_speed = g_ch[i].par; g_ch[i].op = 0; break;
		case 0x03: next_pattern(); g_ch[i].op = 0; break;
		case 0x04: slide(&g_ch[i], tick0); break;
		default: break;                 /* img 0x21fd - the no-ops   */
		}
	}

	if (++g_tick < g_speed) return;
	g_tick = 0;
	g_rowno++;                  /* img 0x0cb7 */
	read_row();
}

/* ORIGIN img 0x0dc5. Channel 0's value plus channel 3's, less the
 * 0x800 their two 0x400 bases contribute - so the landscape's speed
 * is just the two volumes, scaled by 256. */
s32 Xcur_SeqCamSpeed(void)
{
	return ((g_ch[0].value + g_ch[3].value) & 0xffff) - 0x800;
}

/* img 0x9915 - rows since this pattern started. The parts that cut
 * to the music watch this, not a frame count. */
s32 Xcur_SeqRowCount(void) { return g_rowno; }

/* OURS - read-and-clear of img 0xa88d, the way mode 6 does at
 * img 0x1178. */
s32 Xcur_SeqTakeFlash(void)
{
	s32 f = g_flash;
	g_flash = 0;
	return f;
}

/* OURS - read-and-clear of img 0xa88c, raised on INSTRUMENT 9, the
 * gun. The battle tails do it at img 0x1dbe and img 0x1e37. */
s32 Xcur_SeqTakeFire(void)
{
	s32 f = g_fire;
	g_fire = 0;
	return f;
}

/* OURS - read-and-clear of img 0xa88e, raised by Music_NextPattern
 * at img 0x2234 once per PATTERN, not per note. Mode 0xb's tail
 * switches to this once the order reaches 0x1c, which is what
 * thins the shooting out. */
s32 Xcur_SeqTakeNewPattern(void)
{
	s32 f = g_newpat;
	g_newpat = 0;
	return f;
}

/* OURS - reads img 0x9905 and img 0x9903, where in the module we
 * are. */
void Xcur_SeqPos(s32 *order, s32 *row)
{
	if (order) *order = g_order;
	if (row) *row = g_row;
}

/* OURS */
s32 Xcur_SeqChanAge(s32 ch)
{
	return (ch >= 0 && ch < XCUR_SEQ_CHANNELS) ? g_ch[ch].age : 0;
}

/* OURS */
void Xcur_SeqChanAgeDec(s32 ch)
{
	if (ch >= 0 && ch < XCUR_SEQ_CHANNELS && g_ch[ch].age > 0)
		g_ch[ch].age--;
}

/* OURS */
s32 Xcur_SeqChanStep(s32 ch)
{
	return (ch >= 0 && ch < XCUR_SEQ_CHANNELS) ? g_ch[ch].step : 0;
}

/* OURS */
s32 Xcur_SeqChanValue(s32 ch)
{
	return (ch >= 0 && ch < XCUR_SEQ_CHANNELS) ? g_ch[ch].value : 0;
}
