/* PCDEMOS 64k - the SDL2 host, native and web from one body.
 *
 * ONE OF ONLY TWO FILES ALLOWED TO LOOP - host.h. The difference
 * between the two builds is who owns the loop and who runs teardown:
 * emscripten_set_main_loop never returns, so every cleanup call lives
 * in the #else and the web build simply does not have them.
 *
 *     ukko                            the packed EXE at its default path
 *     ukko -exe path\to\UKKO.EXE
 *     ukko -scale 3
 *
 * Esc or q quits, SPACE pauses, . steps one frame, f toggles a 10x
 * fast forward WHILE HELD, m mutes, + and - step the volume, [ and
 * ] step between sections, and every other key goes to the demo.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "host.h"

/* THE DEMO RUNS AT 70 Hz AND THE DISPLAY DOES NOT. Mode 13h retraces
 * at 70 and a modern monitor is 60, so a frame of demo per frame of
 * display runs the whole thing one sixth slow. The tick below is a
 * fixed timestep off the wall clock and the present rate is whatever
 * the host gives us.
 *
 * THE AUDIO QUEUE IS NOT PER FRAME EITHER. Queueing a fixed 630
 * pairs per rendered frame delivers 37,800 samples a second to a
 * device eating 44,100 - the queue drains, and the gaps sound like
 * grit and a dragging tune rather than like silence. It is topped up
 * to a DEPTH instead, so what is produced is what is consumed
 * whatever the frame rate. */
#define AUDIO_RATE   44100
#define DEMO_HZ_MAX  70                     /* sizes the mix buffer */
#define AUDIO_CHUNK  1024                   /* pairs per queue call */
#define AUDIO_TARGET ((AUDIO_RATE / 8) * 4) /* bytes: about 125 ms  */
#define CATCHUP_MAX  8                      /* a stall does not sprint */

/* THE RATE IS THE DEMO'S, NOT THE DISPLAY'S - Demo_TickHz. UKKO ticks
 * at 70 and LITHIUM at 57, and pacing one at the other's rate plays
 * it 23 per cent wrong: every position, angle and palette phase is
 * per tick, so the whole sequence arrives early or late together. */
static s32 g_demo_hz = DEMO_HZ_MAX;
static s32 g_frame_pairs = AUDIO_RATE / DEMO_HZ_MAX;
#define FF_STEPS     20                    /* ticks a pass while F held */

/* Five audible levels. Zero is not one of them - m owns silence, so
 * there are not two ways to be quiet and unmuting knows where to
 * go back to. */
#define VOL_MIN      20
#define VOL_MAX      100
#define VOL_STEP     20
#ifdef _DEBUG
#define VOL_START    0
#else
#define VOL_START    VOL_MAX
#endif
static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture  *g_tex;
static SDL_AudioDeviceID g_aud;
static int g_running = 1;
#ifdef __EMSCRIPTEN__
static int g_started;
#else
static int g_started = 1;
#endif
static int g_paused;
static int g_ff;
static int g_muted;
static s32 g_vol = VOL_START;
static u8 *g_rgb;
static s16 *g_mix;
static u32 g_last_ms;
static s32 g_tick_acc;      /* 1/1000 ms, carried not dropped */
static s32 g_tick_fx;       /* 16.16 demo ticks owed          */
static s32 g_ticks;         /* demo ticks run                 */

/* OURS - index to RGB24, the host's half of the palette.
 * The DAC is 6-bit, so 0x3f becomes 255 rather than 252. */
static void resolve(const u8 *px, s32 n, const u8 *pal, u8 *out)
{
	s32 i;
	for (i = 0; i < n; i++) {
		const u8 *c = pal + (u32)px[i] * 3;
		*out++ = (u8)((c[0] * 255) / 63);
		*out++ = (u8)((c[1] * 255) / 63);
		*out++ = (u8)((c[2] * 255) / 63);
	}
}

/* OURS - the window title is the only state readout there is. */
static void retitle(void)
{
	char t[96];
	const char *run = g_ff ? " - fast forward"
	                       : (g_paused ? " - paused" : "");
	if (g_muted)
		sprintf(t, "%s%s - muted", Demo_Name(), run);
	else if (g_vol != VOL_MAX)
		sprintf(t, "%s%s - vol %ld%%", Demo_Name(), run, (long)g_vol);
	else
		sprintf(t, "%s%s", Demo_Name(), run);
	SDL_SetWindowTitle(g_win, t);
}

/* OURS - scale and hand over. The demo renders at full level and the
 * host attenuates, because volume is a property of playing it back
 * and not of the demo - which is also why a -wav capture ignores it. */
static void queue_audio(s32 pairs)
{
	s32 v = g_muted ? 0 : g_vol;
	s32 i;

	if (!g_aud || !g_mix) return;
	if (v != VOL_MAX)
		for (i = 0; i < pairs * 2; i++)
			g_mix[i] = (s16)((s32)g_mix[i] * v / VOL_MAX);
	SDL_QueueAudio(g_aud, g_mix, (Uint32)(pairs * 2 * sizeof(s16)));
}

/* OURS - STOPPING THE CLOCK IS NOT ENOUGH. The demo clock and the
 * audio queue are independent by design, so halting one leaves the
 * module playing over a frozen picture.
 *
 * THE DEVICE IS LEFT RUNNING. Pausing it would hold whatever is
 * queued, and a single step's 630 samples would then never be heard.
 * Starving it instead goes quiet on its own, at the cost of about
 * 125 ms of tail after the key. */
static void set_paused(int on)
{
	g_paused = on;
	retitle();
}

/* OURS - the demo's own one-liner, at the two moments a person asks
 * for one: a single step, and stopping. NOT per frame; at speed it
 * would scroll everything else away and say nothing useful.
 *
 * THE DETAIL LINES ARE BEHIND i, because two long lines a step
 * is noise until one slot is actually under suspicion.
 *
 * The host prints it and does not read it - host.h. */
static int g_detail;

static void say_state(void)
{
	const char *s = Demo_StateLine();
	int k;

	if (s && *s) fprintf(stderr, "%s\n", s);
	if (!g_detail) return;
	for (k = 0; (s = Demo_StateDetail(k)) != 0; k++)
		fprintf(stderr, "%s\n", s);
}

/* OURS - ONE demo frame and the audio that belongs to it.
 *
 * `queue` IS WHAT MAKES FAST FORWARD BOUNDED. Demo_Audio is called
 * for every step so the replayer consumes all of them and ends up
 * where the picture is, but ten frames a pass into a device eating
 * one would grow the queue without limit - so only the last is
 * handed over and the rest are rendered and dropped. */
static void step_one(int queue)
{
	Demo_Frame();
	if (!g_mix) return;
	Demo_Audio(g_mix, g_frame_pairs);
	if (queue) queue_audio(g_frame_pairs);
}

/* OURS - window pixels to framebuffer pixels.
 *
 * SDL_RenderCopy STRETCHES TO THE WHOLE WINDOW, and the window is
 * scaled and resizable, so a raw event coordinate is three or more
 * times too large. */
static void on_mouse(int mx, int my, int down)
{
	int ww = 0, wh = 0;
	SDL_GetRendererOutputSize(g_ren, &ww, &wh);
	if (ww < 1 || wh < 1) return;
	Demo_Mouse((s32)mx * PD_SCREEN_W / ww,
	           (s32)my * PD_SCREEN_H / wh, (s32)down);
}

/* OURS */
static void events(void)
{
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		if (e.type == SDL_QUIT) {
			g_running = 0;
		} else if (e.type == SDL_KEYDOWN) {
			int k = e.key.keysym.sym;
			if (k == SDLK_ESCAPE || k == SDLK_q) g_running = 0;
			else if (k == SDLK_SPACE) {
				set_paused(!g_paused);
				if (g_paused) say_state();
			} else if (k == SDLK_PERIOD) {
				if (!g_paused) set_paused(1);
				step_one(1);
				say_state();
			} else if (k == SDLK_i) {
				g_detail = !g_detail;
				fprintf(stderr, "detail %s\n",
					g_detail ? "on" : "off");
			} else if (k == SDLK_f) {
				/* HELD, NOT TOGGLED. SDL repeats a held key, so
				 * act only on the transition. */
				if (!g_ff) { g_ff = 1; retitle(); }
			} else if (k == SDLK_m) {
				g_muted = !g_muted;
				retitle();
			} else if (k == SDLK_KP_PLUS || k == SDLK_EQUALS
			        || k == SDLK_PLUS) {
				if (g_vol < VOL_MAX) g_vol += VOL_STEP;
				g_muted = 0;
				retitle();
			} else if (k == SDLK_KP_MINUS || k == SDLK_MINUS) {
				if (g_vol > VOL_MIN) g_vol -= VOL_STEP;
				retitle();
			} else Demo_Key(k, 1);
		} else if (e.type == SDL_MOUSEMOTION) {
			on_mouse(e.motion.x, e.motion.y,
			         (e.motion.state & SDL_BUTTON_LMASK) ? 1 : 0);
		} else if (e.type == SDL_MOUSEWHEEL) {
			Demo_Wheel((s32)e.wheel.y);
		} else if (e.type == SDL_MOUSEBUTTONDOWN
		        || e.type == SDL_MOUSEBUTTONUP) {
			on_mouse(e.button.x, e.button.y,
			         (e.type == SDL_MOUSEBUTTONDOWN
			          && e.button.button == SDL_BUTTON_LEFT) ? 1 : 0);
		} else if (e.type == SDL_KEYUP) {
			if (e.key.keysym.sym == SDLK_f && g_ff) {
				g_ff = 0;
				retitle();
			} else {
				Demo_Key(e.key.keysym.sym, 0);
			}
		} else if (e.type == SDL_WINDOWEVENT
		        && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
			/* No KEYUP arrives for a key held when focus goes. */
			if (g_ff) { g_ff = 0; retitle(); }
		}
	}
}

/* OURS - top the device up to AUDIO_TARGET bytes, however many
 * frames have gone by. Bounded: each pass adds a whole chunk. */
static void pump_audio(void)
{
	int guard = 16;
	if (!g_aud || !g_mix) return;
	while (SDL_GetQueuedAudioSize(g_aud) < (Uint32)AUDIO_TARGET
	        && guard-- > 0) {
		Demo_Audio(g_mix, AUDIO_CHUNK);
		queue_audio(AUDIO_CHUNK);
	}
}

/* OURS - run the demo clock forward by `ms` of real time.
 *
 * CLAMP THE INPUT, NEVER THE LEFTOVER. A tick rate off a millisecond
 * clock is never a whole number - 57 Hz is 3735.55 per ms in 16.16 -
 * so the fraction is carried in g_tick_acc instead of being rounded
 * away, which would lose a tick a minute.
 */
static void advance(s32 ms)
{
	s32 want, n;

	if (ms < 0) ms = 0;
	if (ms > (1000 / g_demo_hz) * CATCHUP_MAX)
		ms = (1000 / g_demo_hz) * CATCHUP_MAX;

	g_tick_acc += ms * (g_demo_hz * 65536);
	g_tick_fx += g_tick_acc / 1000;
	g_tick_acc %= 1000;

	want = g_tick_fx >> 16;
	n = want - g_ticks;
	if (n < 0) n = 0;
	if (n > CATCHUP_MAX) {
		n = CATCHUP_MAX;
		g_tick_fx = (g_ticks + n) << 16;
	}
	while (n-- > 0) {
		Demo_Frame();
		g_ticks++;
	}
}

/* OURS - ONE pass: events, however many demo ticks real time owes,
 * an audio top-up, and a present. Nothing here waits for anything. */
static void tick(void)
{
	const u8 *px;
	s32 w, h;
	u32 now = (u32)SDL_GetTicks();

	events();
	if (g_started) {
		if (g_ff) {
			s32 i;
			for (i = 0; i < FF_STEPS; i++)
				step_one(i == FF_STEPS - 1);
		} else if (!g_paused) {
			advance((s32)(now - g_last_ms));
			pump_audio();
		}
	}
	g_last_ms = now;

	px = Demo_Pixels(&w, &h);
	if (!px) return;

	resolve(px, w * h, Demo_Palette(), g_rgb);
	SDL_UpdateTexture(g_tex, NULL, g_rgb, (int)w * 3);
	SDL_RenderClear(g_ren);
	SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
	SDL_RenderPresent(g_ren);
}

#ifdef __EMSCRIPTEN__
void Host_Start(void)
{
	g_started = 1;
	g_last_ms = (u32)SDL_GetTicks();
}
#endif

/* OURS */
int main(int argc, char **argv)
{
	const char *exe = 0;
	s32 scale = 3, i;
	SDL_AudioSpec want, got;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-exe") && i + 1 < argc) exe = argv[++i];
		else if (!strcmp(argv[i], "-scale") && i + 1 < argc)
			scale = (s32)atol(argv[++i]);
	}
	if (!exe) exe = Demo_DefaultPath();
	if (scale < 1) scale = 1;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	g_win = SDL_CreateWindow(Demo_Name(), SDL_WINDOWPOS_CENTERED,
	                         SDL_WINDOWPOS_CENTERED,
	                         (int)(PD_SCREEN_W * scale),
	                         (int)(PD_SCREEN_H * scale),
	                         SDL_WINDOW_RESIZABLE);
	if (!g_win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }

	g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED
	                                       | SDL_RENDERER_PRESENTVSYNC);
	if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, 0);
	if (!g_ren) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }

	g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGB24,
	                          SDL_TEXTUREACCESS_STREAMING,
	                          PD_SCREEN_W, PD_SCREEN_H);
	g_rgb = (u8 *)malloc(PD_SCREEN_W * PD_SCREEN_H * 3);
	if (!g_tex || !g_rgb) { fprintf(stderr, "out of memory\n"); return 1; }

	memset(&want, 0, sizeof want);
	want.freq = AUDIO_RATE;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 1024;
	want.callback = NULL;               /* queued, not called back */
	g_aud = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
	g_mix = (s16 *)malloc((size_t)AUDIO_CHUNK * 2 * sizeof(s16));
	if (g_aud && g_mix) SDL_PauseAudioDevice(g_aud, 0);

	if (Demo_Init(exe)) {
		fprintf(stderr, "%s: cannot init from %s\n", Demo_Name(), exe);
		return 1;
	}
	g_demo_hz = Demo_TickHz();
	if (g_demo_hz < 1) g_demo_hz = DEMO_HZ_MAX;
	g_frame_pairs = AUDIO_RATE / g_demo_hz;
	if (g_frame_pairs > AUDIO_CHUNK) g_frame_pairs = AUDIO_CHUNK;
	g_last_ms = (u32)SDL_GetTicks();

#ifdef __EMSCRIPTEN__
    Demo_Frame();
	g_ticks = 1;
	g_tick_fx = 1L << 16;
	emscripten_set_main_loop(tick, 0, 1);
#else
    while (g_running) tick();
	Demo_Shutdown();
	if (g_aud) SDL_CloseAudioDevice(g_aud);
	free(g_mix);
	free(g_rgb);
	SDL_DestroyTexture(g_tex);
	SDL_DestroyRenderer(g_ren);
	SDL_DestroyWindow(g_win);
	SDL_Quit();
#endif
    return 0;
}
