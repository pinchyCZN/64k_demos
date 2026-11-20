/* PCDEMOS 64k - the shared packer. See xcurpack.h.
 *
 * Transcribed from the decompressor in the clear at img 0x54, whose
 * own CS-relative addresses are img minus 0x54 - and those `cs:` are
 * the addresses in the comments below. docs/formats/packer.txt has
 * the format; this file is the reading of it.
 */

#include "xcurpack.h"

#define HDR_PARAS   8           /* img X is file X + 0x80          */
#define HDR         0x80
#define IMG_TABLE   0x25e       /* the 62-byte table               */
#define IMG_STREAM  0x2a0       /* the bitstream, packer.txt 6      */
#define TAB_BYTES   62

/* The four bases the code indexes that one table through:
 * cs:[bx+0x20a], +0x215, +0x220 and +0x230. */
#define T1  0x00
#define T2  0x0b
#define T3  0x16
#define T4  0x26

/* EVERY SEGMENT IMMEDIATE IN THE OUTPUT IS BAKED TO THIS, because
 * the decompressor applies the original relocations during the run -
 * packer.txt section 6. 0x1000 is what the committed dumps were made
 * at and what Ghidra bases the depacked images on, so every address
 * in docs/ and in sim/ assumes it. Changing it rebases nothing; it
 * just produces an image whose constants no longer match the notes.
 */
#define LOAD_SEG    0x1000

typedef struct {
	const u8 *d;
	u32 p, len;
	u16 buf;
	s32 n;
	int bad;
} Bits;

/* OURS - the original reads its bitstream inline and the split into
 * these three is ours. */
static u16 bits_word(Bits *b)
{
	u16 v;
	if (b->p + 2 > b->len) { b->bad = 1; return 0; }
	v = RD_U16(b->d + b->p);
	b->p += 2;
	return v;
}

/* OURS */
static u8 bits_byte(Bits *b)
{
	if (b->p + 1 > b->len) { b->bad = 1; return 0; }
	return b->d[b->p++];
}

/* cs:007f - shr bp,1 / dec dx / je <refill>.
 *
 * OURS, split out of the inline bit handling.
 *
 * THE BIT IS TAKEN BEFORE THE COUNT IS CHECKED, so the refill is
 * EAGER: the word is pulled the moment the sixteenth bit leaves,
 * not when the seventeenth is wanted. Lazy refill decodes the first
 * few hundred bytes correctly and then diverges. */
static int bits_bit(Bits *b)
{
	int v = b->buf & 1;
	b->buf = (u16)(b->buf >> 1);
	if (--b->n == 0) {
		b->buf = bits_word(b);
		b->n = 16;
	}
	return v;
}

/* OURS */
int XcurPack_Detect(const u8 *file, u32 len)
{
	if (!file || len < HDR + IMG_STREAM) return 0;
	if (file[0] != 'M' || file[1] != 'Z') return 0;
	return RD_U16(file + 0x08) == HDR_PARAS;
}

/* ORIGIN cs:0086 - the length, gathered a bit at a time with an early exit
 * at every tier. `extra` records that the value came through the one
 * table that can yield the 0x19 escape. */
static s32 decode_len(Bits *b, const u8 *tab, int *extra)
{
	s32 bx;

	*extra = 0;
	bx = bits_bit(b);
	bx = (bx << 1) | bits_bit(b);
	if ((bx & 0xff) >= 2) return bx;            /* cs:0098, used raw */

	bx = (bx << 1) | bits_bit(b);
	if (bx == 0) return tab[T1 + bx];           /* cs:00a4 */

	bx = (bx << 1) | bits_bit(b);
	if ((bx & 0xff) < 5) return tab[T1 + bx];   /* cs:00af */

	bx = (bx << 1) | bits_bit(b);
	if ((bx & 0xff) <= 0xc) return tab[T1 + bx];  /* cs:00bb */

	bx &= 3;                                    /* cs:013d */
	bx = (bx << 1) | bits_bit(b);
	if ((bx & 0xff) < 5) { *extra = 1; return tab[T2 + bx]; }

	bx = (bx << 1) | bits_bit(b);
	if ((bx & 0xff) <= 0xc) { *extra = 1; return tab[T2 + bx]; }

	bx &= 3;                                    /* cs:01a5 */
	bx = (bx << 1) | bits_bit(b);
	if ((bx & 0xff) >= 5) bx = (bx << 1) | bits_bit(b);
	return tab[T3 + bx];
}

/* ORIGIN cs:00c7 - the distance. The high byte comes from the table or from
 * six raw bits; the low byte is always a literal byte off the
 * stream, so the window is 0x1fff. */
static s32 decode_dist(Bits *b, const u8 *tab, s32 cx)
{
	s32 bx = 0;

	if (cx != 2 && bits_bit(b) == 0) {          /* cs:00d3 */
		bx = bits_bit(b);
		bx = (bx << 1) | bits_bit(b);
		bx = (bx << 1) | bits_bit(b);
		if ((bx & 0xff) < 2) {                  /* cs:00ee */
			bx = (s32)tab[T4 + bx] << 8;
		} else {
			bx = (bx << 1) | bits_bit(b);
			if ((bx & 0xff) < 8) {              /* cs:0115 */
				bx = (s32)tab[T4 + bx] << 8;
			} else {
				bx = (bx << 1) | bits_bit(b);
				if ((bx & 0xff) < 0x17) {       /* cs:0125 */
					bx = (s32)tab[T4 + bx] << 8;
				} else {
					bx = (bx << 1) | bits_bit(b);
					bx &= 0xdf;                 /* cs:0135 */
					bx = (bx & 0xff) << 8;      /* xchg bh,bl */
				}
			}
		}
	}
	return bx | bits_byte(b);                   /* cs:00f8 */
}

/* ORIGIN img 0x54 - the decompressor in the clear, cs:0000. */
int XcurPack_Depack(const u8 *file, u32 len, u8 *out, u32 outcap,
                    u32 *out_len)
{
	const u8 *tab;
	Bits bs;
	u32 pos = 0, high = 0;
	s32 ss, sp, cs_, ip, i;

	if (out_len) *out_len = 0;
	if (!file || !out || !XcurPack_Detect(file, len))
		return XCURPACK_BAD_INPUT;
	if (len < HDR + IMG_TABLE + TAB_BYTES) return XCURPACK_BAD_INPUT;

	tab = file + HDR + IMG_TABLE;
	bs.d = file;
	bs.p = HDR + IMG_STREAM;
	bs.len = len;
	bs.bad = 0;
	bs.buf = bits_word(&bs);
	bs.n = 16;

	for (;;) {
		s32 cx, dist;
		int extra;

		if (bs.bad) return XCURPACK_BAD_INPUT;

		if (bits_bit(&bs) == 0) {               /* cs:007e movsb */
			if (pos >= outcap) return XCURPACK_TOO_SMALL;
			out[pos++] = bits_byte(&bs);
			if (pos > high) high = pos;
			continue;
		}

		cx = decode_len(&bs, tab, &extra);

		/* cs:0165 - only the one table can produce 0x19, and the byte
		 * that follows it is a control code as often as a length:
		 * 0xff ENDS THE STREAM and 0xfe only renormalises ES:DI,
		 * which is a no-op once the output is addressed flat. Both
		 * jump past the copy, so neither emits anything. */
		if (extra && cx == 0x19) {
			u8 al = bits_byte(&bs);
			cx = (cx + al) & 0xffff;
			if (al == 0xff) break;
			if (al == 0xfe) continue;
		}

		dist = decode_dist(&bs, tab, cx);
		if (dist == 0 || (u32)dist > pos) return XCURPACK_BAD_INPUT;
		if (pos + (u32)cx > outcap) return XCURPACK_TOO_SMALL;

		/* cs:0103 rep movsb - BYTE AT A TIME and the ranges overlap
		 * by design, which is how a run is encoded. */
		for (i = 0; i < cx; i++) {
			out[pos] = out[pos - (u32)dist];
			pos++;
		}
		if (pos > high) high = pos;
	}

	/* cs:01cb - the relocations: a count byte, a segment, that many
	 * offsets, repeating until a count of zero. Each adds the load
	 * segment to the word it names. */
	for (;;) {
		s32 n, seg;
		if (bs.bad) return XCURPACK_BAD_INPUT;
		n = bits_byte(&bs);
		if (n == 0) break;
		seg = bits_word(&bs);
		for (i = 0; i < n; i++) {
			u32 at = (u32)seg * 16 + bits_word(&bs);
			u16 v;
			if (at + 2 > outcap) return XCURPACK_BAD_INPUT;
			v = (u16)(RD_U16(out + at) + LOAD_SEG);
			out[at] = (u8)(v & 0xff);
			out[at + 1] = (u8)(v >> 8);
		}
	}

	/* cs:01e5 - the handoff reads SS, SP, CS and IP, all relative. */
	ss = bits_word(&bs);
	sp = bits_word(&bs);
	cs_ = bits_word(&bs);
	ip = bits_word(&bs);
	if (bs.bad) return XCURPACK_BAD_INPUT;

	/* THE IMAGE IS PADDED TO A PARAGRAPH: XCURSION's stores end at
	 * 108,427 and its image is 108,432. */
	high = (high + 15) & ~(u32)15;

	/* cs:01f4 and cs:01f6 PUSH THE ENTRY CS:IP, and a demo's own SS
	 * can point inside its own image - XCURSION's does, so both
	 * words land in the output and a decoder that skips them is a
	 * byte out at img 0xcd9f. LITHIUM's SS points above its image
	 * instead, where counting the pushes as output would claim
	 * 570,656 bytes for a 175,392-byte image. So they are applied
	 * only where they fall inside, and they never set the end -
	 * that is the stores' job. packer.txt section 6.1. */
	{
		u32 base = (u32)ss * 16;
		u32 a1 = base + (u32)((sp - 2) & 0xffff);
		u32 a2 = base + (u32)((sp - 4) & 0xffff);
		u16 v1 = (u16)(cs_ + LOAD_SEG);
		if (a1 + 2 <= high && a1 + 2 <= outcap) {
			out[a1] = (u8)(v1 & 0xff);
			out[a1 + 1] = (u8)(v1 >> 8);
		}
		if (a2 + 2 <= high && a2 + 2 <= outcap) {
			out[a2] = (u8)(ip & 0xff);
			out[a2 + 1] = (u8)((ip >> 8) & 0xff);
		}
	}

	if (out_len) *out_len = high;
	return XCURPACK_OK;
}
