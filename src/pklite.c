/* PCDEMOS 64k - PKLITE 1.15, transcribed from the decompressor the
 * packed file carries at image offset 0x44.
 *
 * Addresses in the ORIGIN comments are image offsets into UKKO.EXE
 * as Ghidra shows them - segment 1000, offset = image offset. The
 * decompressor relocates itself to 0x86e0 before running, so an
 * address in the running copy is the image offset MINUS 0x44.
 *
 * Verified byte for byte: 483,083 bytes identical to what the
 * binary's own decompressor produces under emulation.
 */

#include "pklite.h"

/* Image offsets. The MZ header is 10 paragraphs, so a file offset is
 * 0xa0 + the image offset. */
#define HDR_PARAS     0xa0UL
#define T_LEN         0x24dUL    /* running 0x209 - lengths          */
#define T_LEN_EXT     0x258UL    /* running 0x214 - the second class */
#define T_LEN_EXT2    0x263UL    /* running 0x21f - the third        */
#define T_OFF         0x273UL    /* running 0x22f - offset high byte */

/* WHERE THE COMPRESSED STREAM STARTS, and it is a measured constant
 * rather than a derived one. The decompressor's code and tables run
 * to image 0x289 and the stream begins six bytes later. Deriving it
 * would mean emulating the relocating preamble, which computes the
 * source from the image size - img 0x44 to 0x7f. For this file the
 * value is verified; a general PKLITE reader would have to do that
 * arithmetic. */
#define STREAM_OFF    0x290UL

typedef struct {
	const u8 *s;
	u32 i;
	u32 end;
	u16 buf;
	int n;
	int bad;
} Bits;

/* ORIGIN img 0x0084 - the LODSW that primes the bitstream. */
static void bits_fill(Bits *b)
{
	if (b->i + 1 >= b->end) {
		b->bad = 1;
		b->buf = 0;
		b->n = 16;
		return;
	}
	b->buf = (u16)(b->s[b->i] | (b->s[b->i + 1] << 8));
	b->i += 2;
	b->n = 16;
}

/* ORIGIN img 0x00c3 - SHR BP,1 / DEC DX / JZ refill.
 *
 * EAGER REFILL, AND IT IS NOT A DETAIL. The original consumes the bit
 * and refills AT ONCE when the count reaches zero: every read site's
 * JZ goes to a stub that does LODSW and jumps back. LODSW and the
 * LODSB that fetches an offset byte share SI, so refilling lazily on
 * the next read swaps two stream bytes and lengths come out wrong a
 * few hundred bytes in. */
static int bits_bit(Bits *b)
{
	int v = b->buf & 1;
	b->buf = (u16)(b->buf >> 1);
	b->n--;
	if (b->n == 0)
		bits_fill(b);
	return v;
}

/* ORIGIN img 0x013c - LODSB. The literal and the bitstream share SI. */
static u8 bits_byte(Bits *b)
{
	if (b->i >= b->end) {
		b->bad = 1;
		return 0;
	}
	return b->s[b->i++];
}

/* OURS - the signature test. */
int PkLite_Detect(const u8 *file, u32 len)
{
	static const char sig[] = "PKLITE";
	u32 i;
	if (len < 0x30) return 0;
	for (i = 0; i < 6; i++)
		if (file[0x1e + i] != (u8)sig[i]) return 0;
	return 1;
}

/* ORIGIN image offset 0x44 - the decompressor the packed file
 * carries, cs:0000 of the relocated copy. */
int PkLite_Depack(const u8 *file, u32 len, u8 *out, u32 outcap,
                  u32 *out_len)
{
	const u8 *tl, *to;
	Bits b;
	u32 n = 0;

	if (!PkLite_Detect(file, len)) return 1;
	if (len < HDR_PARAS + STREAM_OFF) return 1;

	tl = file + HDR_PARAS + T_LEN;
	to = file + HDR_PARAS + T_OFF;

	b.s = file;
	b.i = HDR_PARAS + STREAM_OFF;
	b.end = len;
	b.n = 0;
	b.bad = 0;
	bits_fill(&b);

	for (;;) {
		u32 cx, off, p, k;

		if (b.bad) return 1;

		/* ORIGIN img 0x00c8 - a clear bit is a literal byte. */
		if (bits_bit(&b) == 0) {
			if (n >= outcap) return 1;
			out[n++] = bits_byte(&b);
			continue;
		}

		/* ---- length. ORIGIN img 0x00ca onward.
		 * A prefix code read a bit at a time: two bits name lengths 2
		 * and 3 outright, and longer codes index the table. */
		{
			u32 bx = (u32)bits_bit(&b);
			bx = (bx << 1) | (u32)bits_bit(&b);
			if (bx >= 2) {                        /* img 0x00dc */
				cx = bx;
			} else {
				bx = (bx << 1) | (u32)bits_bit(&b);
				if (bx == 0) {                    /* img 0x00e8 */
					cx = tl[bx];
				} else {
					bx = (bx << 1) | (u32)bits_bit(&b);
					if (bx < 5) {                 /* img 0x00f3 */
						cx = tl[bx];
					} else {
						bx = (bx << 1) | (u32)bits_bit(&b);
						if (bx <= 0xc) {          /* img 0x00ff */
							cx = tl[bx];
						} else {
							/* img 0x0181 - the second length class. */
							bx &= 3;
							bx = (bx << 1) | (u32)bits_bit(&b);
							if (bx >= 5) {        /* img 0x018f */
								bx = (bx << 1) | (u32)bits_bit(&b);
								if (bx > 0xc) {   /* img 0x019f */
									/* img 0x01e9 - the third class. */
									bx &= 3;
									bx = (bx << 1) | (u32)bits_bit(&b);
									if (bx >= 5)  /* img 0x01f7 */
										bx = (bx << 1)
										   | (u32)bits_bit(&b);
									cx = file[HDR_PARAS + T_LEN_EXT2
									          + bx];
									goto have_length;
								}
							}
							cx = file[HDR_PARAS + T_LEN_EXT + bx];
							if (cx == 0x19) {     /* img 0x01a9 */
								u8 al = bits_byte(&b);
								cx = 0x19 + (u32)al;
								/* img 0x01e2 - 0xff ENDS the stream;
								 * 0xfe only normalises ES:DI across a
								 * 64K boundary, which a flat buffer
								 * does not have. Neither emits a
								 * match. */
								if (al == 0xff) {
									if (out_len) *out_len = n;
									return 0;
								}
								if (al == 0xfe)
									continue;
							}
						}
					}
				}
			}
		}
have_length:

		/* ---- offset. ORIGIN img 0x010b onward. The low byte is
		 * always a literal LODSB; the high byte comes from the table
		 * or, for the widest codes, from the code itself. */
		off = 0;
		if (cx != 2) {                            /* img 0x010d */
			if (bits_bit(&b) == 0) {              /* img 0x0117 */
				u32 bx = (u32)bits_bit(&b);
				bx = (bx << 1) | (u32)bits_bit(&b);
				bx = (bx << 1) | (u32)bits_bit(&b);
				if (bx < 2) {                     /* img 0x0132 */
					off = (u32)to[bx] << 8;
				} else {
					bx = (bx << 1) | (u32)bits_bit(&b);
					if (bx < 8) {                 /* img 0x0159 */
						off = (u32)to[bx] << 8;
					} else {
						bx = (bx << 1) | (u32)bits_bit(&b);
						if (bx < 0x17) {          /* img 0x0169 */
							off = (u32)to[bx] << 8;
						} else {
							bx = (bx << 1) | (u32)bits_bit(&b);
							/* img 0x0179 - AND BX,0xdf clears bit 5,
							 * then XCHG BH,BL makes the code the high
							 * byte outright. */
							off = (bx & 0xdfUL) << 8;
						}
					}
				}
			}
		}
		off |= (u32)bits_byte(&b);                /* img 0x013c */

		if (off == 0 || off > n) return 1;
		if (n + cx > outcap) return 1;

		/* ORIGIN img 0x0147 - REP MOVSB, byte at a time so a short
		 * offset replicates its own output. */
		p = n - off;
		for (k = 0; k < cx; k++)
			out[n++] = out[p++];
	}
}
