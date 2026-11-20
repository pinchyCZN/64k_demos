/* XCURSION - every address the port reads out of the image.
 *
 * EVERY VALUE HERE IS AN IMAGE OFFSET - what docs/xcursion/ calls
 * `img X`, which is file offset X + 0x80 and Ghidra's 1000:X. The
 * depacked image is based at segment 0x1000, so a Ghidra address
 * 1000:X is image offset X and nothing has to be added. Adding a
 * base to one of these is a double count.
 *
 * sim/xcursion/parts.py reads the same offsets from the same dump,
 * so a number here that disagrees with one there is a bug in one of
 * them.
 */

#ifndef XCURSION_ASSETS_H
#define XCURSION_ASSETS_H

/* reverse/unpacked/xcursion.bin, and what the depacker must produce.
 * docs/xcursion/README.txt section 1. */
#define XCUR_IMAGE_BYTES   108432UL

/* g_SinTable - 640 dwords at a 4-byte stride, img 0x7f5e. THE COSINE
 * IS THE SAME TABLE 0x200 BYTES ON, which is why it has 640 entries
 * and not 512: the read runs off the end of one turn and into the
 * next by design. The code indexes it as a BYTE offset masked 0x7fc.
 * sim/xcursion/parts.py section at the top of the file. */
#define XCUR_SIN_TABLE     0x07f5eUL
#define XCUR_SIN_ENTRIES   640

/* img 0x97e1 - the canonical S3M note periods, 1712 down to 907. */
#define XCUR_PERIOD_TABLE  0x097e1UL
#define XCUR_WATER_DAMPING 4           /* img 0xa59f, a static */
#define XCUR_END_DAMPING   6           /* img 0x53bb, the last part */
/* OURS - the frame rate THIS PART is run at, because no measured run
 * of it sustains the timer's 100. effects/theend.txt section 3. */
#define XCUR_END_WATER_HZ  48
#define XCUR_COS_SKEW      0x200UL

/* IT IS 12-BIT, NOT 16. The table is round(4095 * sin(2*pi*k/512)),
 * so a turn is 512 entries and ONE IS 4096 - a product of two
 * entries carries 12 bits of fraction, not 16.
 * effects/script1_landscape.txt section 5. */
#define XCUR_SIN_ONE       4096
#define XCUR_SIN_TURN      512

/* g_PaletteBase - img 0xc7a4, 768 bytes, 256 x RGB at 6 bits.
 *
 * ZERO IN THE DEPACKED IMAGE. It is not data to be read: the demo
 * builds it at run time, and the ramp builder is img 0x3b65 - six
 * bands of 32 then one of 64. subsystems/palette.txt sections 3
 * and 4. Reading these 768 bytes gets black. */
#define XCUR_PALETTE_BASE  0x0c7a4UL
#define XCUR_PAL_BUILDER   0x03b65UL

/* g_Font32 - 32x32 glyphs, 128 bytes each, indexed by char - 0x20.
 * Ten glyphs is exactly the 320-pixel screen width. */
#define XCUR_FONT32        0x0615eUL
#define XCUR_TEXT_COLS     10

/* The fire-text caption records: 6 bytes each, {string, offset,
 * rows}. g_FireTextRec at img 0x97b7 is a CURSOR holding 0x97b9 and
 * stepping by 6, and the list ends where g_BannerRecs begins at
 * 0x97cb - which is exactly three records on. */
#define XCUR_FIRETEXT_RECS 0x097b9UL
#define XCUR_FIRETEXT_N    3
/* Record 3 is "bEXCURSION", one line - the title mode 2 burns. */
#define XCUR_BANNER_REC    3

/* The HUI part - effects/roto.txt. The banner record is a separate
 * one at img 0x97d1, reached through the pointer at img 0x98bb. */
#define XCUR_HUI_REC       0x097d1UL
#define XCUR_HUI_BRIGHT    0x3f        /* img 0xa59e              */
#define XCUR_ROTO_BLEND    2           /* img 0xa59d              */
#define XCUR_ROTO_ZOOM     0x400L      /* img 0x02ec              */
#define XCUR_ROTO_ROWS     0x18        /* img 0x1093              */
#define XCUR_ROTO2_ANGLE   0x100       /* img 0x03c4              */
/* img 0x9917. Planet_Render loops on TWO conditions, img 0x29ad and
 * img 0x29ba - the order AND the row counter - so a part using it
 * ends on a ROW boundary, not just an order one. Both the planet and
 * the star cut set 0x16, which is why the roto part that follows
 * starts with the row counter already at 22 rather than 0. */
#define XCUR_PART_ROWS     0x16        /* img 0x02e2, img 0x03aa  */
#define XCUR_STARS_FAR     0xfa000L    /* img 0x0375, ball off    */
#define XCUR_STARS_TZ      0x28000L    /* img 0x0356              */

/* The plasma part, img 0x03ee - effects/warp.txt. It opens at the
 * fade's floor, so the scene rises out of black. */
#define XCUR_WARP_FADE     (-16384)    /* img 0x03f4 and 0x0430   */
/* The battle's two fade levels. 0x3d80 is a bias of 0x3d on every
 * DAC component, so the chase parts open white; 0x1800 is 0x18. */
#define XCUR_CHASE_FADE    0x3d80L     /* img 0x04c9, 0x04e0      */
#define XCUR_FIGHT_FADE    0x1800L     /* img 0x0548, 0x05cb, 066c */
#define XCUR_WARP_FLASH    0x2000      /* img 0x1192              */
#define XCUR_WARP_STEP     0x100       /* img 0x11c9              */
#define XCUR_REC_BYTES     6

/* Sync values are order indices into the module - camera.txt. */
#define XCUR_SYNC_COUNTDOWN 4
/* The planet part's whole parameter block, img 0x0253 to 0x02e2.
 * Nothing here is inherited and nothing is seeded - planet.txt. */
#define XCUR_PLANET_PAN    0x8000
#define XCUR_PLANET_NEAR   0L
#define XCUR_PLANET_TZ     0x2e000L  /* img 0x0265              */
#define XCUR_PLANET_BIAS   0x1c      /* img 0x0292              */
#define XCUR_PLANET_LYAW   (-512)    /* img 0x02bb              */

/* Demo_Timeline's first part - img 0x4daa and img 0x4dbb. The ball is
 * held outside Planet_Render's own cutoff, and THE FADE IS FROM
 * WHITE: a positive level. effects/planet.txt. */
#define XCUR_PLANET_FAR    0xfa000L
#define XCUR_FADE_WHITE    0x3d80

/* The grey moon, img 0x4e18 and img 0x4e26. The last two are the
 * shade scale and divisor at img 0x986d and img 0x986f, which that
 * part does NOT set - they carry the opening's. voxel.txt 1.2. */
#define XCUR_MOON_PAN      0xc0
#define XCUR_MOON_LYAW     (-512)
#define XCUR_VOX_HSCALE    0x1000
#define XCUR_SHADE_DIV     0x53

/* The recorded flight track, one byte of control flags per ten
 * ticks. effects/flight.txt. */
#define XCUR_FLIGHT_TRACK  0xcc1aUL  /* img 0x98b9's static  */
#define XCUR_THROTTLE0     (-64)     /* img 0x9378's static  */

/* The flag, img 0x5116 to img 0x513d. 0x2d00 is exactly 48 steps
 * of 0xf0, so mode 0x17's fade lands on zero and stops dead.
 * effects/flag.txt. */
#define XCUR_VISTA_REC     0x097d7UL
#define XCUR_FLAG_BRIGHT   0x30
#define XCUR_FADE_FLAG     0x2d00
#define XCUR_FLAG_STEP     0xf0
#define XCUR_FLAG_ROWS     0x1c
#define XCUR_FLAG_END      0x20

/* A displayed frame is 10/7 of a tick, not two - measured, see
 * subsystems/animation.txt section 1.1. */
#define XCUR_TICK_HZ       100     /* img 0x9867's static     */
/* THE MACHINE WE MODEL - subsystems/animation.txt section 1.1. */
#define XCUR_FRAME_HZ      70

/* The car part, img 0x5147. The car model and the flagpost it
 * drives past. effects/car.txt. */
#define XCUR_CAR           0x094f4UL  /* img 0x51a3           */
#define XCUR_FLAGPOST      0x05be7UL  /* img 0x2487           */
#define XCUR_CAR_STEER0    0x200      /* img 0x51e2           */
#define XCUR_CAR_CLAMP     0x1e0      /* img 0x1acc, 0x1b4a   */

/* The credits. 84 lines of 32 characters, and they read -
 * effects/scroller.txt section 4 quotes them. */
#define XCUR_SCROLL_TEXT   0x0a435UL  /* img 0x313b           */
#define XCUR_FADE_CREDITS  0x3c00     /* img 0x527a           */
#define XCUR_SCROLL0      0xa000L    /* img 0x937c, 0x9380   */

/* THE END - 120 verts and 94 faces, the largest model in the
 * image. effects/theend.txt. */
#define XCUR_LETTERS      0x05c47UL  /* img 0x52c9           */
#define XCUR_END_BIAS     0x1c       /* img 0x52ef           */

/* img 0x97dd's static value - the demo opens from black. */
#define XCUR_FADE_START (-12288)

/* The launch part's whole block, img 0x01a1 to 0x0247 - its own
 * parameters, not state carried in from the approach. The scale is
 * the one that matters: 0x400 against the 14392 the approach leaves,
 * and a small scale is what puts the horizon high and looks DOWN at
 * the ground. effects/ground.txt. */
#define XCUR_LAUNCH_TY      0x1c000L   /* img 0x01b8              */
#define XCUR_LAUNCH_TZ      0x26000L   /* img 0x01c2              */
#define XCUR_LAUNCH_PITCH   (-256)     /* img 0x01cc              */
#define XCUR_LAUNCH_ROLL    0x100      /* img 0x01da              */
#define XCUR_LAUNCH_LROLL   0x200      /* img 0x01a1              */
#define XCUR_LAUNCH_LYAW    0x421      /* img 0x0193              */
#define XCUR_LAUNCH_SCALE   0x400      /* img 0x020d              */
#define XCUR_LAUNCH_STEP    (-72)      /* img 0x01eb, g_GroundRamp */

/* The craft the opening flies - formats/mesh.txt section 2 has all
 * six models. */
#define XCUR_SHIP          0x0585fUL   /* img 0x98ad names it     */

/* The battle's other three models - effects/battle.txt section 5. */
#define XCUR_CRAFT_B       0x0572fUL   /* img 0x046d, 17v 25f     */
#define XCUR_SHOT_BOLT     0x05b81UL   /* img 0x27c1, order <0x1c */
#define XCUR_SHOT_WINGED   0x05a8aUL   /* img 0x27bc, order >=    */

/* GLOBALS the draw reads, not constants - mesh.txt 3.0.1. */
#define XCUR_MESH_ZSCALE   0x0100      /* img 0x9374, a dword     */
#define XCUR_OBJ_YAW       (-382)      /* img 0x9881, 0xfe82      */
#define XCUR_LIGHT_PITCH   0x0100      /* img 0x9883              */
#define XCUR_LIGHT_YAW     (-256)      /* img 0x9885, 0xff00      */
#define XCUR_LIGHT_ROLL    0x0000      /* img 0x990f              */
#define XCUR_SHADE_SCALE   0x0190      /* img 0x9370, a dword     */
#define XCUR_TARGET_X      (-401408L)  /* img 0x9368              */
#define XCUR_TARGET_Y      (-200704L)  /* img 0x936c              */
#define XCUR_SHIP_FAR      0x28000L    /* img 0x2376 gates on it  */
#define XCUR_CD_SCALE      0x0b88
#define XCUR_CD_SKYBIAS    (-39)       /* img 0x996d              */

/* g_CountdownMask - 320 x 64 ONE BIT, static data in the image, with
 * the four glyphs as 64-wide windows down its left edge at a 40-byte
 * row stride. effects/countdown.txt section 1. */
#define XCUR_COUNTDOWN_MASK 0x0895dUL
#define XCUR_MASK_STRIDE    40
#define XCUR_COUNTDOWN_WINDOWS 4
#define XCUR_BURST_TICKS    0x60    /* img 0x999f, ticks per digit */
#define XCUR_FIRE_HEAT      0xff

/* The Scream Tracker module - subsystems/music.txt. */
#define XCUR_S3M_OFF       0x0cda0UL
#define XCUR_S3M_BYTES     55787UL     /* tools/extract_s3m.py    */
/* ibxm's nearest-sample mixer, which is what a 1996 SB card did. */
#define XCUR_MUSIC_INTERP  0

#define XCUR_AUDIO_RATE    44100L

#endif
