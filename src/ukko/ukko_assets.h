/* UKKO - where everything sits in the depacked image.
 *
 * EVERY VALUE HERE IS AN IMAGE OFFSET, ready to add to the depacked
 * image and nothing else. Adding UKKO_CS_BASE to one is a double
 * count that still draws a plausible picture, out of wrong bytes.
 *
 * All verified - docs/ukko/README.txt section 3, formats/pose.txt,
 * subsystems/timeline.txt section 4.
 */

#ifndef UKKO_ASSETS_H
#define UKKO_ASSETS_H

#define UKKO_IMAGE_BYTES   486912UL   /* what the depacker produces  */
#define UKKO_CS_BASE       0x200UL    /* listing 1020:0000           */

/* img 1020:08a5, the byte after Line_Draw's RETF. 705 keyframes are
 * present; 694 are played before the exit test at 1020:efe7. */
#define UKKO_POSE_OFF      0x00aa5UL
#define UKKO_POSE_STRIDE   0x30       /* 12 joints of 4 bytes        */
#define UKKO_POSE_JOINTS   12
#define UKKO_POSE_FRAMES   0x2b6      /* 694                         */

/* Eight display frames per keyframe - g_PartIndex 1, then 3 to 9.
 * formats/pose.txt section 5. */
#define UKKO_SUBFRAMES     8

/* One EGA plane of mode 0Dh: 320*200/8. */
#define UKKO_LOGO_BYTES    0x1f40UL
#define UKKO_LOGO_TITLE    0x08995UL
#define UKKO_LOGO_MUZAC    0x0a8d6UL
#define UKKO_LOGO_PRODIGY  0x0c817UL
#define UKKO_SCREEN_SWEAT  0x4dce0UL

/* A full mode 13h screen each, copied whole by the scene handlers. */
#define UKKO_BACKDROP_BYTES 0xfa00UL
#define UKKO_BD_LOUNGE      0x0f4a0UL
#define UKKO_BD_FLOOR1      0x1eeb0UL
#define UKKO_BD_FLOOR2      0x2e8c0UL
#define UKKO_BD_KITCHEN     0x3e2d0UL

/* The Scream Tracker 3 module. formats/music.txt. */
#define UKKO_S3M_OFF       0x70380UL
#define UKKO_S3M_BYTES     22902UL

/* OURS - the demo's rate was whatever card was answered for. */
#define UKKO_AUDIO_RATE    44100L

/* OURS - PLAYBACK SPEED, as a percentage. 100 plays the module at
 * the tempo its own header asks for, which is speed 6 / tempo 125 =
 * 50 ticks a second. Raising it shortens every tick AND raises the
 * pitch, the way running a tape faster does - ibxm has no tempo
 * control separate from its sample rate. ukko_music.c. */
#define UKKO_MUSIC_SPEED   100L

/* OURS - ibxm treats this as a flag: 0 is nearest-neighbour, 1 is
 * linear. 0 is what a 1995 Sound Blaster mixer did, and it is what
 * this module was written to sound like. */
#define UKKO_MUSIC_INTERP  0

/* img 1020:e6a9 - every line and every backdrop pixel is this value.
 * The artwork is two-valued, 0x00 and 0xff. */
#define UKKO_PEN           0xff

/* The holds, in display frames. img 1020:e667 and 1020:f1b9. */
#define UKKO_TITLE_HOLD    0x1a9      /* 425 retraces                */
#define UKKO_LOGO_HOLD     0xc8       /* 200                         */
#define UKKO_SCREEN_HOLD   0x64       /* 100 - img 1020:f21d         */

#endif
