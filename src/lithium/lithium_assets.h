/* LITHIUM - every address the port reads out of the image.
 *
 * EVERY VALUE HERE IS AN IMAGE OFFSET - what docs/lithium/ calls
 * `img X`, which is Ghidra's 1000:X. The depacked image is based at
 * segment 0x1000, so a Ghidra address 1000:X is image offset X and
 * nothing has to be added. Adding a base to one of these is a double
 * count.
 *
 * sim/lithium/parts.py reads the same offsets from the same dump, so
 * a number here that disagrees with one there is a bug in one of
 * them.
 */

#ifndef LITHIUM_ASSETS_H
#define LITHIUM_ASSETS_H

/* reverse/unpacked/lithium.bin, and what the depacker must produce.
 * docs/lithium/README.txt section 1. THE PACKER IS XCURSION'S,
 * BYTE FOR BYTE - docs/layout.txt section 7 - so this demo depacks
 * through src/xcurpack.c and not a second implementation.
 *
 * AN IMAGE OFFSET PAST 0xffff HAS NO 1000:X SPELLING, this image
 * being 175 KB. Those carry their seg:off form as well. */
#define LITH_IMAGE_BYTES   175392UL

/* The three 768-byte palettes Demo_Main selects between by writing
 * g_PalSource at img 0x8f8c. 6-bit DAC.
 *
 * PAL_MAIN IS ZERO IN THE IMAGE and reading it gets black:
 * Pal_Animate rebuilds it every frame. The other two are stored,
 * and both are four 64-entry rising ramps -
 * subsystems/texture.txt. */
#define LITH_PAL_MAIN      0x096beUL
#define LITH_PAL_1         0x099beUL
#define LITH_PAL_2         0x09cbeUL
#define LITH_PAL_BYTES     768

/* g_SinSource, img 0x1a535 = 2a53:0005 - 512 signed words, a quarter
 * sine rising 0 to 4096, so 4096 is 1.0 and the fixed point is
 * 12-bit. Demo_Main reads it at img 0x012e and 0x0154. */
#define LITH_SIN_SRC       0x1a535UL

/* "VOTE" - the word the thousand particles spell. img 0x0692 hands
 * it to the text renderer. effects/bump.txt section 7. */
#define LITH_STR_VOTE      0x92ebUL

/* NOT WRITTEN BY THE SCENE - the image's loaded values are what the
 * demo runs on. g_RockSlideX's 0x2ee0 drained 0x20 a tick is the 250
 * ticks before the interlace crosses over. effects/bump.txt 5. */
#define LITH_BUMP_SLIDE_SEED  0x8ff9UL
#define LITH_BUMP_PHASE_SEED  0x8ff7UL
#define LITH_BUMP_JITTER_SEED 0x8feaUL

/* The two 8x8 height brushes the dust stamps with - B peaks at 4
 * against A's 8, and g_BumpInterlace picks which. bump.txt 2. */
#define LITH_DUST_BRUSH_A  0xa031UL
#define LITH_DUST_BRUSH_B  0xa071UL

/* img 0xe5f0 - where the parts' own data begins. */
#define LITH_ASSET_BASE    0x0e5f0UL

/* THE S3M MODULE, img 0x1ad20 = segment 2ad2 - 58 orders of 39
 * patterns. THE DEPACKED IMAGE ENDS AT 0x2ad20, so a reader that
 * takes the module's segment for an image offset runs off the end.
 * subsystems/music.txt section 5. */
#define LITH_MOD_BASE      0x1ad20UL
#define LITH_MOD_TAG       0x2c      /* "SCRM"                     */
#define LITH_MOD_ORDNUM    0x20      /* word - entries in the list */
#define LITH_MOD_INSNUM    0x22      /* word - instruments         */
#define LITH_MOD_PATNUM    0x24      /* word - patterns            */
#define LITH_MOD_SPEED     0x31      /* byte - ticks per row       */
#define LITH_MOD_TEMPO     0x32      /* byte - BPM; ticks/s = x2/5 */
#define LITH_MOD_ORDERS    0x60      /* the order list itself      */

/* 64 rows is the FORMAT's. AN ORDER IS NOT A FIXED NUMBER OF TICKS:
 * effect A resets the speed and effects B and C end the pattern
 * early, so the length has to be walked per order -
 * subsystems/music.txt section 5.2. */
#define LITH_MOD_ROWS      64
#define LITH_MOD_MAX_ORDERS 256

#define LITH_AUDIO_RATE    44100L

#endif
