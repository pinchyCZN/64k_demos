/* UKKO - the bicycle game. OURS entirely; see ukko_game.h.
 * The host's own keys still work: pause, single step, fast forward,
 * volume and the section skip all drive this through Demo_Frame
 * exactly as they drive the demo, because none of them know what is
 * being drawn.
 */

#include <math.h>
#include <string.h>

#include "ukko.h"
#include "ukko_game.h"

#define FP          256             /* 8.8 fixed point              */
#define SINE_N      256             /* entries in one turn          */
#define GROUND_Y    150             /* the flat road, in screen y   */
#define WHEEL_R     8
#define WHEELBASE   28
#define BIKE_SCREEN_X 96            /* where the bike sits          */

#define ROAD_MARGIN 8
#define WORLD_LEFT  (g_b.x - BIKE_SCREEN_X - ROAD_MARGIN)

/* The road, and the bumps that live on it. A bump is a triangle:
 * half-width w, height h, centred on x in world coordinates. */
#define BUMPS       8
#define BUMP_GAP    170             /* nominal world spacing        */

/* A BUMP MUST BE WIDER THAN THE WHEELBASE and shallower than 45
 * degrees; both wheels then touch by construction. Why, and what
 * breaks otherwise: docs/ukko/game.txt 2.1. */
#define BUMP_W_MIN  (WHEELBASE + 2)
#define BUMP_W_MAX  (WHEELBASE + 18)
#define BUMP_H_MIN  8
#define BUMP_H_MAX  18

/* A triangle, as the demo's is - the one closed loop in its
 * skeleton, at its own proportions - docs/ukko/game.txt 3.1. */
#define HEAD_BASE      4            /* across the crown             */
#define HEAD_LEN       5            /* crown down to the neck       */

/* Tweakables. Degrees are the amplitude EITHER SIDE of straight
 * ahead, and a period is one full sweep. Separate sines on purpose. */
#define HEAD_YAW_DEG   70           /* looking left and right       */
#define HEAD_PITCH_DEG 30           /* nodding, deliberately less   */
#define HEAD_YAW_MS    800
#define HEAD_PITCH_MS  2500

#define THICK_ROAD  5
#define THICK_WHEEL 3
#define THICK_FRAME 2
#define THICK_RIDER 1

/* Rider proportions, model space. Thigh plus shin must reach the
 * furthest pedal position or the leg detaches. */
#define L_THIGH     16
#define L_SHIN      16
#define L_UPPER     14
#define L_FORE      14
#define CRANK_X     14
#define CRANK_Y     (-4)
#define PEDAL_R     6

static s32 g_sin[SINE_N];           /* 8.8, one full turn           */
static u32 g_rng = 0x1234abcdUL;

static struct {
	s32 x;                          /* world x of the rear axle     */
	s32 y;                          /* screen y of the rear axle    */
	s32 y8;                         /* the same, 8.8 - the real one */
	s32 vy;                         /* 8.8                          */
	s32 speed;                      /* 8.8 world units per frame    */
	s32 ang;                        /* 8.8 TABLE STEPS, 256 = a turn */
	s32 avel;                       /* 8.8 steps per frame           */
	s32 wa;                         /* the wheelie's own lean, same  */
	s32 crank;                      /* pedal phase, 0..SINE_N        */
	s32 xacc;                       /* 64ths of a px not yet spent   */
	s32 sacc;                       /* the speed ramp's remainder    */
	s32 hyaw, hpitch;               /* head phases, 8.8 table steps  */
	s32 prev_ry, up_slope;          /* steepest nose-up this climb  */
	int air;
	s32 jump_hold;                  /* frames of lift still owed    */
	int jump_down, wheelie, fast;
} g_b;

/* 45 degrees is an eighth of a turn, which is 32 table steps. */
#define DEG45       (32 * FP)

/* Degrees to 8.8 table steps, and a period in milliseconds to the
 * phase step that walks one turn in that time. The host drives
 * Demo_Frame at the demo's own 70 Hz - subsystems/timeline.txt 3. */
#define GAME_HZ     70
#define DEG8(d)     ((s32)(d) * SINE_N * FP / 360)
#define PHASE_MS(m) (SINE_N * FP / ((s32)(m) * GAME_HZ / 1000))
#define PHASE_WRAP  (SINE_N * FP - 1)

/* THE THROTTLE RAMPS, AND THE PRESS IS STILL A STEP. Pressing it
 * jumps to SPEED_FAST exactly as it always did, and from there the
 * speed builds to twice that for as long as it is held; letting go
 * eases back to SPEED_IDLE rather than dropping.
 *
 * The two ramps are accumulated rather than divided per frame, so a
 * long period still moves - a plain step of range/frames truncates to
 * nothing past about four seconds and the throttle would silently
 * stop ramping. Speed is in 64ths of a pixel per frame. */
#define SPEED_IDLE     150
#define SPEED_FAST     220          /* the step, the moment it goes down */
#define SPEED_MAX      (SPEED_FAST * 2)
#define SPEED_UP_MS    3000         /* SPEED_FAST up to SPEED_MAX     */
#define SPEED_DOWN_MS  2000         /* and all the way back down      */
#define RAMP_UP_N      (SPEED_UP_MS * GAME_HZ / 1000)
#define RAMP_DOWN_N    (SPEED_DOWN_MS * GAME_HZ / 1000)
#define WHEELIE_RMP (DEG45 / 10)    /* reached in ten frames        */
#define GRAV        36
/* Slope is read across the WHEELBASE, not between frames, and the
 * gain is unphysical on purpose - docs/ukko/game.txt 4.2. */
#define LAUNCH_MIN  4
#define LAUNCH_GAIN 5               /* over two                     */

/* Variable height, platformer style: the hold cuts gravity rather
 * than adding thrust - docs/ukko/game.txt 4.3. */
#define JUMP_V0     576             /* 8.8, the press itself        */
#define GRAV_HELD   12              /* gravity while held and rising */
#define JUMP_FRAMES 14              /* the ceiling on holding it    */

static struct { s32 x, w, h; } g_bump[BUMPS];

/* THE BIKE. Model origin is the REAR AXLE, which is also what a
 * wheelie pivots about, so the transform needs no second centre.
 * Edit these and nothing else changes. */
static const Prim BIKE[] = {
	{ PRIM_CIRCLE,         0,   0,  0, WHEEL_R, THICK_WHEEL },
	{ PRIM_CIRCLE, WHEELBASE,   0,  0, WHEEL_R, THICK_WHEEL },
	{ PRIM_LINE,           0,   0, 14, -14,     THICK_FRAME },
	{ PRIM_LINE,          14, -14, WHEELBASE, 0, THICK_FRAME },
	{ PRIM_LINE,   WHEELBASE,   0,  0,   0,     THICK_FRAME },
	{ PRIM_LINE,          14, -14,  4, -22,     THICK_FRAME },
	{ PRIM_LINE,          -1, -22, 9, -22,      THICK_FRAME },
	{ PRIM_LINE,   WHEELBASE,   0, 26, -16,     THICK_FRAME },
	{ PRIM_LINE,          22, -16, 30, -16,     THICK_FRAME }
};
#define BIKE_N ((s32)(sizeof BIKE / sizeof BIKE[0]))

/* The rider, as joint indices. OUR skeleton, not the demo's - the
 * demo's limb mapping is still an open question and this does not
 * need to wait on it. */
#define J_HIP   0
#define J_SHO   1
#define J_HEAD  2
#define J_ELB   3
#define J_HAND  4
#define J_KNEEL 5
#define J_FOOTL 6
#define J_KNEER 7
#define J_FOOTR 8
#define J_N     9

static const u8 RIDER[][2] = {
	{ J_HIP, J_SHO }, { J_SHO, J_HEAD },
	{ J_SHO, J_ELB }, { J_ELB, J_HAND },
	{ J_HIP, J_KNEEL }, { J_KNEEL, J_FOOTL },
	{ J_HIP, J_KNEER }, { J_KNEER, J_FOOTR }
};
#define RIDER_N ((s32)(sizeof RIDER / sizeof RIDER[0]))

/* ------------------------------------------------------------ maths */

/* OURS - one turn of sine in 8.8. Built once with the library so the
 * table is exact; nothing else here touches a float. */
static void sine_build(void)
{
	s32 i;
	for (i = 0; i < SINE_N; i++)
		g_sin[i] = (s32)(sin((double)i * 6.283185307 / SINE_N) * FP);
}

static s32 sn(s32 a) { return g_sin[(a & (SINE_N - 1))]; }
static s32 cs(s32 a) { return g_sin[((a + SINE_N / 4) & (SINE_N - 1))]; }

/* OURS - integer square root, the shift-and-subtract one. */
static s32 isqrt(s32 v)
{
	s32 r = 0, b = 1L << 30, t;
	if (v <= 0) return 0;
	while (b > v) b >>= 2;
	while (b) {
		t = r + b;
		r >>= 1;
		if (v >= t) { v -= t; r += b; }
		b >>= 2;
	}
	return r;
}

/* OURS - the bumps have to be the same every run or a jump cannot be
 * practised. A plain linear congruential generator, seeded fixed. */
static s32 rnd(s32 lo, s32 hi)
{
	g_rng = g_rng * 1103515245UL + 12345UL;
	return lo + (s32)((g_rng >> 16) % (u32)(hi - lo + 1));
}

/* ---------------------------------------------------------- drawing */

/* OURS - a line of a given thickness, by drawing parallel copies.
 * The offset goes across whichever axis the line is shallower in,
 * which is what keeps a thick diagonal from looking like a staircase
 * of gaps. */
static void line_t(u8 *fb, s32 x0, s32 y0, s32 x1, s32 y1, s32 t)
{
	s32 i, dx = x1 - x0, dy = y1 - y0;
	if (t < 1) t = 1;
	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;
	for (i = 0; i < t; i++) {
		s32 o = i - t / 2;
		if (dx >= dy) Ukko_Line(fb, x0, y0 + o, x1, y1 + o, UKKO_PEN);
		else          Ukko_Line(fb, x0 + o, y0, x1 + o, y1, UKKO_PEN);
	}
}

/* OURS - a circle of any radius and thickness, as an annulus test
 * over the bounding box - docs/ukko/game.txt 1. */
static void circle_t(u8 *fb, s32 cx, s32 cy, s32 r, s32 t)
{
	s32 x, y, ro, ri, ro2, ri2;

	if (r < 1) return;
	if (t < 1) t = 1;
	ro = r + t / 2;
	ri = ro - t;
	if (ri < 0) ri = 0;
	ro2 = ro * ro;
	ri2 = ri * ri;

	for (y = cy - ro; y <= cy + ro; y++) {
		if (y < 0 || y >= PD_SCREEN_H) continue;
		for (x = cx - ro; x <= cx + ro; x++) {
			s32 dx, dy, d2;
			if (x < 0 || x >= PD_SCREEN_W) continue;
			dx = x - cx;
			dy = y - cy;
			d2 = dx * dx + dy * dy;
			if (d2 <= ro2 && d2 >= ri2)
				fb[y * PD_SCREEN_W + x] = UKKO_PEN;
		}
	}
}

/* OURS - model space to screen. The one place that knows the world
 * is flat; an isometric version replaces this and nothing else. */
static void xf_pt(const Xform *x, s32 mx, s32 my, s32 *ox, s32 *oy)
{
	*ox = x->tx + (mx * x->cs - my * x->sn) / FP;
	*oy = x->ty + (mx * x->sn + my * x->cs) / FP;
}

/* OURS */
static void prim_draw(u8 *fb, const Prim *p, const Xform *x)
{
	s32 ax, ay, bx, by;
	xf_pt(x, p->ax, p->ay, &ax, &ay);
	if (p->kind == PRIM_CIRCLE) {
		circle_t(fb, ax, ay, p->by, p->thick);
	} else {
		xf_pt(x, p->bx, p->by, &bx, &by);
		line_t(fb, ax, ay, bx, by, p->thick);
	}
}

/* ------------------------------------------------------------- road */

/* OURS - the surface at a world x: flat, plus any bump it falls in.
 * A bump is a triangle, so the height is linear in the distance from
 * its centre. */
static s32 ground_y(s32 wx)
{
	s32 i, y = GROUND_Y;
	for (i = 0; i < BUMPS; i++) {
		s32 d = wx - g_bump[i].x;
		if (d < 0) d = -d;
		if (d < g_bump[i].w)
			y -= g_bump[i].h * (g_bump[i].w - d) / g_bump[i].w;
	}
	return y;
}

/* OURS - put a bump back out ahead, but ONLY once its whole width
 * has left the screen - docs/ukko/game.txt 2.2. */
static void bumps_cycle(void)
{
	s32 i;
	for (i = 0; i < BUMPS; i++)
		if (g_bump[i].x + g_bump[i].w < WORLD_LEFT) {
			s32 far = 0, k;
			for (k = 0; k < BUMPS; k++)
				if (g_bump[k].x > far) far = g_bump[k].x;
			g_bump[i].x = far + rnd(BUMP_GAP - 50, BUMP_GAP + 90);
			g_bump[i].w = rnd(BUMP_W_MIN, BUMP_W_MAX);
			g_bump[i].h = rnd(BUMP_H_MIN, BUMP_H_MAX);
		}
}

/* OURS */
static void road_draw(u8 *fb)
{
	s32 sx, px = 0, py = 0;
	for (sx = -ROAD_MARGIN; sx <= PD_SCREEN_W + ROAD_MARGIN; sx += 4) {
		s32 wx = g_b.x + sx - BIKE_SCREEN_X;
		s32 y = ground_y(wx);
		if (sx > -ROAD_MARGIN) line_t(fb, px, py, sx, y, THICK_ROAD);
		px = sx;
		py = y;
	}
}

/* ------------------------------------------------------------ rider */

/* OURS - two-bone inverse kinematics, closed form. `flip` picks
 * which of the two solutions is a knee rather than a backwards
 * one - docs/ukko/game.txt 3. */
static void ik(s32 ax, s32 ay, s32 cx, s32 cy, s32 l1, s32 l2,
               s32 flip, s32 *bx, s32 *by)
{
	s32 dx = cx - ax, dy = cy - ay;
	s32 d = isqrt(dx * dx + dy * dy);
	s32 a, h2, h, px, py;

	if (d < 1) d = 1;
	if (d > l1 + l2 - 1) d = l1 + l2 - 1;      /* straighten, do not snap */
	a = (l1 * l1 - l2 * l2 + d * d) / (2 * d);
	h2 = l1 * l1 - a * a;
	h = isqrt(h2 > 0 ? h2 : 0);
	px = ax + a * dx / d;
	py = ay + a * dy / d;
	*bx = px + flip * h * (-dy) / d;
	*by = py + flip * h * dx / d;
}

/* OURS - the rider, in model space. The feet ARE the pedal circle;
 * everything else follows from where they are. No keyframes. */
static void rider_pose(s32 *jx, s32 *jy)
{
	s32 c = g_b.crank;

	jx[J_HIP] = 5;    jy[J_HIP] = -26;
	jx[J_SHO] = 13;   jy[J_SHO] = -38;
	jx[J_HEAD] = 16;  jy[J_HEAD] = -44;
	jx[J_HAND] = 26;  jy[J_HAND] = -16;

	jx[J_FOOTL] = CRANK_X + PEDAL_R * cs(c) / FP;
	jy[J_FOOTL] = CRANK_Y + PEDAL_R * sn(c) / FP;
	jx[J_FOOTR] = CRANK_X + PEDAL_R * cs(c + SINE_N / 2) / FP;
	jy[J_FOOTR] = CRANK_Y + PEDAL_R * sn(c + SINE_N / 2) / FP;

	/* KNEES FORWARD, elbows back - a wrong flip still reaches the
	 * pedals, it just hinges the leg the wrong way. */
	ik(jx[J_HIP], jy[J_HIP], jx[J_FOOTL], jy[J_FOOTL],
	   L_THIGH, L_SHIN, -1, &jx[J_KNEEL], &jy[J_KNEEL]);
	ik(jx[J_HIP], jy[J_HIP], jx[J_FOOTR], jy[J_FOOTR],
	   L_THIGH, L_SHIN, -1, &jx[J_KNEER], &jy[J_KNEER]);
	ik(jx[J_SHO], jy[J_SHO], jx[J_HAND], jy[J_HAND],
	   L_UPPER, L_FORE, 1, &jx[J_ELB], &jy[J_ELB]);
}

/* OURS - the head, a triangle seen EDGE ON: straight ahead its three
 * vertices share one screen x and it draws as a vertical line, and a
 * yaw shows only as width - docs/ukko/game.txt 3.1.
 *
 * STAYS 8.8 UNTIL THE LAST STEP. Rounding per vertex would open a
 * four-pixel base by one pixel and lose most of the sweep. */
static void head_draw(u8 *fb, const Xform *x, s32 hx, s32 hy)
{
	/* (side, up) in 8.8: apex at the neck, base across the crown. */
	static const s32 V[3][2] = {
		{                 0,            0 },
		{ -HEAD_BASE * FP / 2, HEAD_LEN * FP },
		{  HEAD_BASE * FP / 2, HEAD_LEN * FP }
	};
	s32 px[3], py[3];
	s32 yw, pt, sy, cp, sp, i;

	yw = DEG8(HEAD_YAW_DEG) * sn(g_b.hyaw / FP) / FP;
	pt = DEG8(HEAD_PITCH_DEG) * sn(g_b.hpitch / FP) / FP;
	sy = sn(yw / FP);
	cp = cs(pt / FP);
	sp = sn(pt / FP);

	for (i = 0; i < 3; i++) {
		s32 f, u, f2, u2;
		/* Yaw. The depth it produces is discarded - the camera
		 * looks down that axis. */
		f = -V[i][0] * sy / FP;
		u = V[i][1];
		f2 = (f * cp + u * sp) / FP;
		u2 = (u * cp - f * sp) / FP;
		/* Model space has up as NEGATIVE y. */
		xf_pt(x, hx + f2 / FP, hy - u2 / FP, &px[i], &py[i]);
	}
	for (i = 0; i < 3; i++)
		line_t(fb, px[i], py[i], px[(i + 1) % 3], py[(i + 1) % 3],
		       THICK_RIDER);
}

/* ---------------------------------------------------------- physics */

/* OURS - the table step whose sine is nearest s, by scanning the
 * half turn either side of level. A hundred and thirty compares a
 * frame is nothing, and it avoids a small-angle approximation that
 * would leave the front wheel short of the road on a steep bump. */
static s32 asin_steps(s32 s)
{
	s32 i, best = 0, bd = 0x7fffffffL;
	for (i = -64; i <= 64; i++) {
		s32 d = sn(i) - s;
		if (d < 0) d = -d;
		if (d < bd) { bd = d; best = i; }
	}
	return best * FP;
}

/* OURS - the lean the ground asks for, in 8.8 table steps.
 * THE SIGN MATTERS: a front wheel on a bump is HIGHER, which is a
 * SMALLER screen y, a NEGATIVE angle, and a nose pointing up. */
static s32 ground_lean(s32 ry, s32 fy)
{
	s32 a = asin_steps((fy - ry) * FP / WHEELBASE);
	if (a > DEG45) a = DEG45;
	if (a < -DEG45) a = -DEG45;
	return a;
}

/* OURS - the bike rides on its wheels and leaves the ground at a
 * crest. Two things that look optional and are not: a wheelie
 * stops the front wheel sampling the road at all, and the
 * steepest nose-up is LATCHED rather than compared against the
 * previous frame - docs/ukko/game.txt 4.1, 4.2. */
static void physics(void)
{
	s32 ry, fy, climb, want;

	if (g_b.fast) {
		if (g_b.speed < SPEED_FAST) {
			g_b.speed = SPEED_FAST;
		} else if (g_b.speed < SPEED_MAX) {
			g_b.sacc += SPEED_MAX - SPEED_FAST;
			g_b.speed += g_b.sacc / RAMP_UP_N;
			g_b.sacc %= RAMP_UP_N;
			if (g_b.speed > SPEED_MAX) g_b.speed = SPEED_MAX;
		}
	} else if (g_b.speed > SPEED_IDLE) {
		g_b.sacc += SPEED_MAX - SPEED_IDLE;
		g_b.speed -= g_b.sacc / RAMP_DOWN_N;
		g_b.sacc %= RAMP_DOWN_N;
		if (g_b.speed < SPEED_IDLE) g_b.speed = SPEED_IDLE;
	}

	/* The remainder is CARRIED. Truncating speed/64 every frame
	 * quantises the whole ramp to the five whole pixel rates between
	 * idle and flat out, which reads as five gears, not a ramp. */
	g_b.xacc += g_b.speed;
	g_b.x += g_b.xacc / 64;
	g_b.xacc %= 64;
	g_b.crank = (g_b.crank + g_b.speed / 24) & (SINE_N - 1);
	g_b.hyaw = (g_b.hyaw + PHASE_MS(HEAD_YAW_MS)) & PHASE_WRAP;
	g_b.hpitch = (g_b.hpitch + PHASE_MS(HEAD_PITCH_MS)) & PHASE_WRAP;

	/* The wheelie ramps up while held and falls back when let go. */
	if (g_b.wheelie) {
		g_b.wa -= WHEELIE_RMP;
		if (g_b.wa < -DEG45) g_b.wa = -DEG45;
	} else {
		g_b.wa += WHEELIE_RMP;
		if (g_b.wa > 0) g_b.wa = 0;
	}

	ry = ground_y(g_b.x) - WHEEL_R;
	fy = ground_y(g_b.x + WHEELBASE) - WHEEL_R;

	if (g_b.air) {
		/* Held, rising, and still within the budget: fall slower. */
		if (g_b.jump_down && g_b.vy < 0 && g_b.jump_hold > 0) {
			g_b.vy += GRAV_HELD;
			g_b.jump_hold--;
		} else {
			g_b.vy += GRAV;
			g_b.jump_hold = 0;
		}
		/* CARRIED IN 8.8, NOT TRUNCATED PER FRAME. Stepping an
		 * integer y by vy/256 throws away the fraction every frame,
		 * which at these speeds is most of the motion - it halved
		 * the height of a jump and made the arc visibly steppy. */
		g_b.y8 += g_b.vy;
		g_b.y = g_b.y8 / FP;
		g_b.ang += g_b.avel;
		if (g_b.y >= ry) {
			g_b.y = ry;
			g_b.y8 = ry * FP;
			g_b.vy = 0;
			g_b.avel = 0;
			g_b.air = 0;
			g_b.jump_hold = 0;
		}
	} else {
		climb = fy - ry;            /* the slope under the bike */
		if (climb < g_b.up_slope) g_b.up_slope = climb;
		/* The deliberate jump wins over the terrain. jump_hold is
		 * armed ONLY here, so a crest launch cannot be stretched by
		 * holding the key down through it. */
		if (g_b.jump_down && g_b.jump_hold == 0 && g_b.vy == 0) {
			g_b.y = ry;
			g_b.y8 = ry * FP;
			g_b.vy = -JUMP_V0;
			g_b.avel = 0;
			g_b.jump_hold = JUMP_FRAMES;
			g_b.air = 1;
		} else if (g_b.up_slope <= -LAUNCH_MIN && climb >= 0) {
			/* Nose was up, now it is not: over the top. The lift is
			 * the slope times how fast we met it. */
			s32 fwd = g_b.speed / 64;
			g_b.air = 1;
			g_b.y8 = g_b.y * FP;
			g_b.vy = g_b.up_slope * fwd * FP * LAUNCH_GAIN
			       / (WHEELBASE * 2);
			/* The ramp's spin CARRIES ON, nose up. */
			g_b.avel = g_b.wa ? 0 : g_b.up_slope * FP / 40;
			g_b.up_slope = 0;
		} else {
			g_b.y = ry;
			g_b.y8 = ry * FP;
			if (climb >= 0) g_b.up_slope = 0;
		}
	}
	g_b.prev_ry = ry;

	if (!g_b.air) {
		/* A lifted front wheel reads no road. */
		want = (g_b.wa < -DEG45 / 3) ? 0 : ground_lean(ry, fy);
		/* SNAPPED, NOT EASED. A rigid frame's lean is whatever its
		 * two contact points say it is; easing toward that leaves
		 * the front wheel under the road for the whole transient,
		 * which is most of a short bump. */
		g_b.ang = want;
	}
	if (g_b.ang > DEG45) g_b.ang = DEG45;
	if (g_b.ang < -DEG45) g_b.ang = -DEG45;
}

/* ------------------------------------------------------------- game */

/* OURS */
void Game_Init(void)
{
	s32 i;
	sine_build();
	memset(&g_b, 0, sizeof g_b);
	g_b.x = 0;
	g_b.y = GROUND_Y - WHEEL_R;
	g_b.y8 = g_b.y * FP;
	g_b.prev_ry = g_b.y;
	g_b.speed = SPEED_IDLE;
	g_rng = 0x1234abcdUL;
	for (i = 0; i < BUMPS; i++) {
		g_bump[i].x = 220 + i * BUMP_GAP;
		g_bump[i].w = rnd(BUMP_W_MIN, BUMP_W_MAX);
		g_bump[i].h = rnd(BUMP_H_MIN, BUMP_H_MAX);
	}
}

/* OURS */
void Game_Key(int ch, int down)
{
	if (ch == 'z') g_b.wheelie = down;      /* HELD, not toggled */
	else if (ch == 'c') g_b.jump_down = down;   /* HELD, see JUMP_V0 */
	else if (ch == 'x' && g_b.fast != down) {
		g_b.fast = down;
		g_b.sacc = 0;           /* the other ramp's remainder is not ours */
	}
}

/* OURS */
void Game_Frame(u8 *fb)
{
	s32 jx[J_N], jy[J_N], i;
	Xform x;

	bumps_cycle();
	physics();

	memset(fb, 0, PD_SCREEN_W * PD_SCREEN_H);
	road_draw(fb);

	/* Both leans are 8.8 table steps and simply add. */
	x.cs = cs((g_b.ang + g_b.wa) / FP);
	x.sn = sn((g_b.ang + g_b.wa) / FP);
	x.tx = BIKE_SCREEN_X;
	x.ty = g_b.y;

	for (i = 0; i < BIKE_N; i++)
		prim_draw(fb, &BIKE[i], &x);

	rider_pose(jx, jy);
	for (i = 0; i < RIDER_N; i++) {
		s32 ax, ay, bx, by;
		xf_pt(&x, jx[RIDER[i][0]], jy[RIDER[i][0]], &ax, &ay);
		xf_pt(&x, jx[RIDER[i][1]], jy[RIDER[i][1]], &bx, &by);
		line_t(fb, ax, ay, bx, by, THICK_RIDER);
	}
	head_draw(fb, &x, jx[J_HEAD], jy[J_HEAD]);
}

/* OURS - read-only probes, so a test can check the physics without
 * guessing at it from pixels. */
s32 Game_ProbeGround(s32 wx) { return ground_y(wx); }
/* OURS */
void Game_ProbeState(s32 *x, s32 *y, s32 *ang, s32 *wa, int *air)
{
	*x = g_b.x; *y = g_b.y; *ang = g_b.ang; *wa = g_b.wa; *air = g_b.air;
}
s32 Game_ProbeSin(s32 a8) { return sn(a8 / FP); }
/* OURS */
void Game_ProbeHead(s32 *yaw, s32 *pitch)
{
	*yaw = DEG8(HEAD_YAW_DEG) * sn(g_b.hyaw / FP) / FP;
	*pitch = DEG8(HEAD_PITCH_DEG) * sn(g_b.hpitch / FP) / FP;
}
