/* UKKO - the rotoscope data and the interpolation between keyframes.
 *
 * docs/ukko/formats/pose.txt.
 */

#include "ukko.h"

/* ORIGIN depacked 1020:e885 - the g_PartIndex dispatch inside
 * Pose_Tween, as (previous weight, current weight, shift).
 *
 * EIGHT EVENLY SPACED POSITIONS PER KEYFRAME. g_PartIndex 1 draws the
 * keyframe itself through 1020:eee0 with no blend at all, and 3 to 9
 * give the seven eighths between. The traced poses advance at 8.75 a
 * second while the loop draws at the 70 Hz retrace, so seven
 * interpolated frames sit between each pair - that is what makes the
 * dance smooth.
 *
 * EACH WEIGHT USES THE SMALLEST SHIFT THAT EXPRESSES IT, so 2/8 is
 * (3*prev + cur) >> 2 and not (6*prev + 2*cur) >> 3. The two truncate
 * differently, so the shift is part of the value and not a tidying.
 *
 * The original builds these weights by repeated addition - 7*prev is
 * the accumulator added to itself six times. There is no multiply
 * anywhere in it.
 */
static const u8 TWEEN[UKKO_SUBFRAMES - 1][3] = {
	{ 7, 1, 3 },    /* g_PartIndex 3   1/8 */
	{ 3, 1, 2 },    /*              4   2/8 */
	{ 5, 3, 3 },    /*              5   3/8 */
	{ 1, 1, 1 },    /*              6   4/8 */
	{ 3, 5, 3 },    /*              7   5/8 */
	{ 1, 3, 2 },    /*              8   6/8 */
	{ 1, 7, 3 }     /*              9   7/8 */
};

/* ORIGIN depacked 1020:f256 - Pose_FetchJointPair's source.
 *
 * A joint is FOUR BYTES, x then y, both unsigned words read with
 * MOVZX. A keyframe is twelve of them, 0x30 bytes. The stream sits in
 * the code segment at 1020:08a5, the byte after Line_Draw's RETF.
 */
static const u8 *pose_at(s32 kf)
{
	return g_img + UKKO_POSE_OFF + (u32)kf * UKKO_POSE_STRIDE;
}

/* ORIGIN depacked 1020:e823 - Pose_Tween.
 *
 * Reads the same joint from the PREVIOUS keyframe and the current one
 * and blends by the sub-frame's weight. SUBTRACTING 0x30 FROM THE
 * JOINT OFFSET is how one index serves both keyframes - there is no
 * second cursor and no pose buffer anywhere; the fetch writes
 * straight into Line_Draw's endpoint variables.
 *
 * ONLY THE SUB-FRAME 0 PASS ADVANCES THE CURSOR. The tween path jumps
 * over the advance at 1020:e820, which is why a keyframe lasts eight
 * display frames. subsystems/timeline.txt section 2.
 */
void Ukko_Pose(s32 kf, s32 sub, s16 *out_xy)
{
	const u8 *cur, *prev;
	s32 j;

	if (!g_img) return;
	if (kf < 0) kf = 0;
	cur = pose_at(kf);
	prev = kf > 0 ? pose_at(kf - 1) : cur;

	for (j = 0; j < UKKO_POSE_JOINTS; j++) {
		u32 cx = RD_U16(cur + j * 4);
		u32 cy = RD_U16(cur + j * 4 + 2);
		u32 px = RD_U16(prev + j * 4);
		u32 py = RD_U16(prev + j * 4 + 2);
		if (sub == 0) {
			/* SUB 0 IS THE PREVIOUS KEYFRAME, NOT THE CURRENT ONE.
			 * The demo draws the pose the cursor points at and only
			 * THEN advances - img 1020:eee0 runs before the add at
			 * 1020:efda - so by the time the counter reads kf, the
			 * frame just drawn was kf-1. Returning the current
			 * keyframe here makes the figure jump forward an eighth
			 * and snap back on the next sub. */
			out_xy[j * 2] = (s16)px;
			out_xy[j * 2 + 1] = (s16)py;
		} else {
			u32 wp = TWEEN[sub - 1][0];
			u32 wc = TWEEN[sub - 1][1];
			u32 sh = TWEEN[sub - 1][2];
			out_xy[j * 2] = (s16)((px * wp + cx * wc) >> sh);
			out_xy[j * 2 + 1] = (s16)((py * wp + cy * wc) >> sh);
		}
	}
}
