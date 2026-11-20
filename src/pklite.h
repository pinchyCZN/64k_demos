/* PCDEMOS 64k - PKLITE 1.15, decompressed in memory.
 *
 * UKKO.EXE is packed with PKLITE and the other three demos are not -
 * they share an LZEXE derivative. docs/layout.txt section 7.
 *
 * THE PORT DEPACKS AT RUNTIME AND SHIPS NO UNPACKED FILE. One
 * 52,836-byte original reproduces every asset.
 */

#ifndef PKLITE_H
#define PKLITE_H

#include "pd.h"

/* Is this file PKLITE-packed? Looks for the marker at 0x1d. */
int PkLite_Detect(const u8 *file, u32 len);

/* Decompress file[] into out[]. Returns 0 on success and writes the
 * produced length to *out_len; non-zero means the stream was
 * malformed or outcap was too small.
 *
 * THE RELOCATION PASS IS NOT APPLIED. PKLITE patches 32 bytes of
 * segment values into the image afterwards, and every one of them
 * lands in code - none touch the pose data, the backdrops, the logo
 * planes or the module. docs/ukko/formats/pklite.txt section 5.
 */
int PkLite_Depack(const u8 *file, u32 len, u8 *out, u32 outcap,
                  u32 *out_len);

#endif
