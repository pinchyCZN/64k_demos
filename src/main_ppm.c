/* PCDEMOS 64k - the host that needs nothing but a C compiler.
 *
 * Renders frames to P6 PPM files. It is not a toy: it keeps the demo
 * core buildable and verifiable on a machine with no SDL, and it is
 * the bench that diffs src/ against sim/ frame for frame.
 *
 *     ukko_ppm                      10 frames to build/ppm/
 *     ukko_ppm -n 200 -o out        200 frames to out/
 *     ukko_ppm -skip 430 -n 4       start 430 frames in
 *     ukko_ppm -exe PATH            a different packed original
 *     ukko_ppm -verify PATH         the depacker's acceptance test
 *     ukko_ppm -wav OUT.WAV -sec 20  the music, as a WAV
 *     ukko_ppm -offsets           struct layouts, for the agent
 *     ukko_ppm -states -n 4000    one state line per tick, no PPMs
 *
 * ONE OF ONLY TWO FILES ALLOWED TO LOOP - host.h. -agent gives that
 * loop to the debug agent instead of the dumper - docs/agent.txt.
 *
 * -states IS THE CHEAP HALF OF THE BENCH, and it writes no pictures.
 * A comparison against the original is on STATE AT A TICK and not on
 * pixels at a frame, because the original draws every second tick -
 * so its frame N and ours are not the same picture even when the
 * state matches. tools/statecmp.py reads what this prints.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
/* THE AGENT IS OPT-IN PER PROJECT. main_ppm.c is shared by every
 * demo and only the ones that link src/agent.c define this, so a
 * demo without it builds exactly as it did - docs/agent.txt 5. */
#ifdef PD_AGENT
#include "agent.h"
#include "agent_host.h"
#endif

/* OURS - little-endian field writers for the WAV header. */
static void wr16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32 v)
{
	p[0] = (u8)v;         p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

/* The hosts open their device at this rate. */
#define UKKO_WAV_RATE 44100L

/* OURS */
static int write_ppm(const char *dir, s32 n, const u8 *px, s32 w, s32 h,
                     const u8 *pal)
{
	char path[512];
	FILE *f;
	s32 i, total = w * h;

	sprintf(path, "%s/f%05ld.ppm", dir, (long)n);
	f = fopen(path, "wb");
	if (!f) {
		fprintf(stderr, "cannot write %s\n", path);
		return 1;
	}
	fprintf(f, "P6\n%ld %ld\n255\n", (long)w, (long)h);
	for (i = 0; i < total; i++) {
		const u8 *c = pal + (u32)px[i] * 3;
		/* The DAC is 6-bit. 0x3f becomes 255, not 252. */
		fputc((c[0] * 255) / 63, f);
		fputc((c[1] * 255) / 63, f);
		fputc((c[2] * 255) / 63, f);
	}
	fclose(f);
	return 0;
}

/* OURS - render the music to a 16-bit stereo WAV, so it can be
 * listened to on a machine with no SDL. The demo is advanced in step
 * with it at 70 frames a second, which is what the audio would hear
 * in the real thing - the module is not simply played from the top
 * and left to run. */
static int to_wav(const char *path, s32 secs)
{
	FILE *f;
	s16 *chunk;
	s32 per = UKKO_WAV_RATE / 70;
	s32 frames = secs * 70;
	u32 bytes = (u32)frames * (u32)per * 4;
	s32 i;
	u8 h[44];

	f = fopen(path, "wb");
	if (!f) { printf("cannot write %s\n", path); return 1; }
	chunk = (s16 *)malloc((size_t)per * 2 * sizeof(s16));
	if (!chunk) { fclose(f); return 1; }

	memcpy(h, "RIFF", 4);
	wr32(h + 4, 36 + bytes);
	memcpy(h + 8, "WAVEfmt ", 8);
	wr32(h + 16, 16);
	wr16(h + 20, 1);
	wr16(h + 22, 2);
	wr32(h + 24, (u32)UKKO_WAV_RATE);
	wr32(h + 28, (u32)UKKO_WAV_RATE * 4);
	wr16(h + 32, 4);
	wr16(h + 34, 16);
	memcpy(h + 36, "data", 4);
	wr32(h + 40, bytes);
	fwrite(h, 1, 44, f);

	for (i = 0; i < frames; i++) {
		Demo_Frame();
		Demo_Audio(chunk, per);
		fwrite(chunk, 2, (size_t)per * 2, f);
	}
	fclose(f);
	free(chunk);
	printf("wrote %s - %ld s, %lu bytes\n",
	       path, (long)secs, (unsigned long)bytes);
	return 0;
}

/* OURS - the acceptance test for the depacker: what we produce
 * against what the binary's own decompressor produces under
 * emulation. docs/ukko/formats/pklite.txt section 5. */
static int verify(const char *path)
{
	const u8 *img;
	u32 len = 0, i;
	FILE *f;
	u8 *want;

	img = Demo_Image(&len);
	if (!img || !len) { printf("no image\n"); return 1; }
	f = fopen(path, "rb");
	if (!f) { printf("cannot read %s\n", path); return 1; }
	want = (u8 *)malloc(len);
	if (!want) { fclose(f); return 1; }
	if (fread(want, 1, len, f) != len) {
		printf("%s is shorter than %lu bytes\n",
		       path, (unsigned long)len);
		free(want); fclose(f); return 1;
	}
	fclose(f);
	for (i = 0; i < len; i++)
		if (img[i] != want[i]) {
			printf("MISMATCH at %lu: ours %02x want %02x\n",
			       (unsigned long)i, img[i], want[i]);
			free(want);
			return 1;
		}
	printf("depack IDENTICAL for all %lu bytes\n",
	       (unsigned long)len);
	free(want);
	return 0;
}

/* OURS */
int main(int argc, char **argv)
{
	const char *out = "build/ppm";
	const char *exe = 0;
	const char *chk = 0;
	const char *wav = 0;
	s32 count = 10, skip = 0, secs = 20, scene = 0, i;
	int agent = 0, offsets = 0, states = 0, detail = 0;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-n") && i + 1 < argc)
			count = (s32)atol(argv[++i]);
		else if (!strcmp(argv[i], "-o") && i + 1 < argc)
			out = argv[++i];
		else if (!strcmp(argv[i], "-skip") && i + 1 < argc)
			skip = (s32)atol(argv[++i]);
		else if (!strcmp(argv[i], "-exe") && i + 1 < argc)
			exe = argv[++i];
		else if (!strcmp(argv[i], "-verify") && i + 1 < argc)
			chk = argv[++i];
		else if (!strcmp(argv[i], "-wav") && i + 1 < argc)
			wav = argv[++i];
		else if (!strcmp(argv[i], "-sec") && i + 1 < argc)
			secs = (s32)atol(argv[++i]);
		else if (!strcmp(argv[i], "-scene") && i + 1 < argc)
			scene = (s32)atol(argv[++i]);
		else if (!strcmp(argv[i], "-agent"))
			agent = 1;
		else if (!strcmp(argv[i], "-offsets"))
			offsets = 1;
		else if (!strcmp(argv[i], "-states"))
			states = 1;
		else if (!strcmp(argv[i], "-detail"))
			states = detail = 1;
		else {
			fprintf(stderr, "usage: %s [-n N] [-o DIR] [-skip N]"
			                " [-exe PATH] [-verify PATH]"
			                " [-agent] [-offsets]"
			                " [-states] [-detail]\n", argv[0]);
			return 2;
		}
	}
	if (!exe) exe = Demo_DefaultPath();

	if (Demo_Init(exe)) {
		fprintf(stderr, "%s: cannot init from %s\n", Demo_Name(), exe);
		return 1;
	}
	if (chk) {
		int rc = verify(chk);
		Demo_Shutdown();
		return rc;
	}
	if (wav) {
		int rc = to_wav(wav, secs);
		Demo_Shutdown();
		return rc;
	}
#ifdef PD_AGENT
	/* After Demo_Init, so it describes this binary - agent.txt 5.1. */
	if (offsets) {
		Dbg_PrintLayout();
		Demo_Shutdown();
		return 0;
	}
	if (agent) {
		if (Agent_Start(out)) {
			fprintf(stderr, "%s: -agent needs the Debug Win32 build\n",
			        Demo_Name());
			Demo_Shutdown();
			return 1;
		}
		while (!Agent_Pump(AGENT_PUMP_BLOCK | AGENT_PUMP_CLOCK))
			;
		Agent_Stop();
		Demo_Shutdown();
		return 0;
	}
#else
	if (agent || offsets) {
		fprintf(stderr, "%s: this build has no agent - docs/agent.txt\n",
		        Demo_Name());
		Demo_Shutdown();
		return 1;
	}
#endif

	printf("%s: %ld frames to %s/ (skipping %ld)\n",
	       Demo_Name(), (long)count, out, (long)skip);

	Dbg_GotoPart((s32)scene);

	for (i = 0; i < skip; i++)
		Demo_Frame();

	if (states) {
		for (i = 0; i < count; i++) {
			int k;
			const char *d;

			Demo_Frame();
			printf("%s" "\n", Demo_StateLine());
			if (!detail) continue;
			for (k = 0; (d = Demo_StateDetail(k)) != 0; k++)
				printf("%s" "\n", d);
		}
		Demo_Shutdown();
		return 0;
	}

	for (i = 0; i < count; i++) {
		const u8 *px;
		s32 w, h;
		Demo_Frame();
		px = Demo_Pixels(&w, &h);
		if (!px) break;
		if (write_ppm(out, skip + i, px, w, h, Demo_Palette())) break;
	}
	Demo_Shutdown();
	return 0;
}
