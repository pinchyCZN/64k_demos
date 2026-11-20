/* PCDEMOS 64k - the types and the readers, spelled once.
 *
 * ANSI C89. No // comments, no declarations after statements, no
 * stdint.h - CLAUDE.md rule 16.
 *
 * u32 AND s32 ARE long, NOT int. C89 guarantees long is at least 32
 * bits and guarantees nothing of the sort for int. The cost is that
 * every printf casts to (long) and wide literals carry an L.
 *
 * THESE DEMOS ARE LITTLE-ENDIAN. They are 16-bit real-mode x86, so
 * every multi-byte read of demo data goes through RD_U16 or RD_U32
 * rather than a wide-pointer cast - a cast would also be misaligned
 * about as often as not.
 */

#ifndef PD_H
#define PD_H

typedef unsigned char   u8;
typedef signed char     s8;
typedef unsigned short  u16;
typedef signed short    s16;
typedef unsigned long   u32;
typedef signed long     s32;

/* THE ONE DELIBERATE STEP OUTSIDE C89, and it is not a convenience:
 * every perspective divide in these demos has a 64-BIT DIVIDEND -
 * the 386 forms EDX:EAX and divides that - so a 32-bit intermediate
 * is a different calculation, not a simpler spelling of the same
 * one. docs/lithium/subsystems/raster.txt section 2. */
#if defined(_MSC_VER)
typedef __int64            s64;
typedef unsigned __int64   u64;
#elif defined(__GNUC__)     /* GCC, Clang, Emscripten */
typedef long long          s64;
typedef unsigned long long u64;
#else
#error "no 64-bit type for this compiler - see lith_idiv_shl8"
#endif

#define RD_U16(p)  ((u16)((u16)(p)[0] | ((u16)(p)[1] << 8)))
#define RD_S16(p)  ((s16)RD_U16(p))
#define RD_U32(p)  ((u32)(p)[0]         | ((u32)(p)[1] <<  8) \
                  | ((u32)(p)[2] << 16) | ((u32)(p)[3] << 24))
#define RD_S32(p)  ((s32)RD_U32(p))

/* Mode 13h, which is what all four of these demos draw in. */
#define PD_SCREEN_W  320
#define PD_SCREEN_H  200

#endif
