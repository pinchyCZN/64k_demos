/* UKKO - the scenery, the logo planes and the scene schedule.
 *
 * docs/ukko/subsystems/timeline.txt.
 */

#include <string.h>

#include "ukko.h"

/* The four backdrops, as the scene handlers at img 1020:f0ca onward
 * load them, and the scene g_SceneId selects at each frame number.
 * img 1020:f053, f067 and f07b. */
static const u32 BACKDROP[4] = {
	UKKO_BD_LOUNGE, UKKO_BD_FLOOR1, UKKO_BD_FLOOR2, UKKO_BD_KITCHEN
};
static const u32 LOGO[3] = {
	UKKO_LOGO_TITLE, UKKO_LOGO_MUZAC, UKKO_LOGO_PRODIGY
};

/* ORIGIN depacked 1020:f0ca onward - the scene handlers.
 *
 * Each copies 0x3e80 dwords from its own segment into the back buffer
 * at 0x7098. THE BACK BUFFER IS NEVER CLEARED: overwriting it
 * wholesale with a backdrop every frame is what erases the previous
 * figure. There is no clear and no dirty rectangle in this demo.
 */
void Ukko_Backdrop(u8 *fb, s32 which)
{
	if (!g_img || which < 0 || which > 3) {
		memset(fb, 0, PD_SCREEN_W * PD_SCREEN_H);
		return;
	}
	memcpy(fb, g_img + BACKDROP[which], PD_SCREEN_W * PD_SCREEN_H);
}

/* ORIGIN depacked 1020:e65f, f1b1 and f1e3 - the mode 0Dh blits.
 *
 * 0x1f40 is 8000, which is 320*200/8 - ONE plane of mode 0Dh, so the
 * image is one bit per pixel with a 40-byte row stride. The demo
 * blits it straight to 0xa000 after an INT 10h mode set; expanded
 * here to the byte-per-pixel framebuffer the rest of the port uses.
 */
void Ukko_LogoPlane(u8 *fb, s32 which)
{
	const u8 *src;
	s32 y, xb, bit;

	memset(fb, 0, PD_SCREEN_W * PD_SCREEN_H);
	if (!g_img || which < 0 || which > 2) return;
	src = g_img + LOGO[which];

	for (y = 0; y < PD_SCREEN_H; y++) {
		const u8 *row = src + y * 40;
		u8 *out = fb + y * PD_SCREEN_W;
		for (xb = 0; xb < 40; xb++) {
			u8 b = row[xb];
			if (!b) continue;
			for (bit = 0; bit < 8; bit++)
				if (b & (0x80 >> bit))
					out[xb * 8 + bit] = UKKO_PEN;
		}
	}
}

/* ORIGIN depacked 1020:f1f4 - the third interlude.
 *
 * A FULL SCREEN, NOT A PLANE. It stays in mode 13h and copies 64,000
 * bytes where the logo interludes switch to mode 0Dh and blit 8,000,
 * so it needs no bit expansion - see docs/ukko/subsystems/
 * timeline.txt section 4.
 */
void Ukko_TextScreen(u8 *fb)
{
	if (!g_img) {
		memset(fb, 0, PD_SCREEN_W * PD_SCREEN_H);
		return;
	}
	memcpy(fb, g_img + UKKO_SCREEN_SWEAT, PD_SCREEN_W * PD_SCREEN_H);
}

/* ORIGIN depacked 1020:f053, f067 and f07b - the scene switches.
 *
 * Two independent schedules run off g_FrameIndex; this is the one
 * that picks the backdrop. The interludes are the other, and they
 * live in the frame loop.
 */
s32 Ukko_SceneFor(s32 kf)
{
	s32 which = 0;                  /* img 1020:e645 sets scene 1 */
	if (kf >= 0x086) which = 1;
	if (kf >= 0x208) which = 0;
	if (kf >= 0x21e) which = 3;
	return which;
}
