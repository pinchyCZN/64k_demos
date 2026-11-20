/* PCDEMOS 64k - the packer XCURSION, DRAIN and LITHIUM share.
 *
 * NOT PKLITE. UKKO is PKLITE and has its own decoder in pklite.c;
 * these three are packed with an LZEXE 0.91 derivative, byte for
 * byte the same packer - docs/layout.txt section 7. One decoder
 * serves all three, verified byte-exact against the committed dumps
 * of XCURSION and LITHIUM.
 *
 * THE PORT DEPACKS AT RUNTIME AND SHIPS NO UNPACKED FILE. One
 * 49,761-byte original reproduces every asset.
 */

#ifndef XCURPACK_H
#define XCURPACK_H

#include "pd.h"

#define XCURPACK_OK            0
#define XCURPACK_BAD_INPUT     1
#define XCURPACK_TOO_SMALL     2

/* Does this file have the shape of one of our three?
 *
 * A SHAPE CHECK, NOT A SIGNATURE. The LZ91 marker is stripped, so
 * there is nothing to match on and no stock unpacker recognises
 * these files - docs/formats/packer.txt. All this proves is an MZ
 * header of the right size.
 */
int XcurPack_Detect(const u8 *file, u32 len);

/* Decompress file[] into out[]. Returns XCURPACK_OK and writes the
 * produced length to *out_len, or one of the codes above.
 *
 * THE RELOCATIONS ARE APPLIED, unlike pklite.c's. This packer patches
 * them during the run rather than after, so the output is baked to
 * one load segment and cannot be rebased - docs/formats/packer.txt
 * section 6.
 */
int XcurPack_Depack(const u8 *file, u32 len, u8 *out, u32 outcap,
                    u32 *out_len);

#endif
