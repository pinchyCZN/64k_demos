"""UKKO's part list and its keys.

    py -3.10 sim/run.py ukko
    py -3.10 sim/run.py ukko --list

THE WHOLE DEMO IS HERE AND IT IS ALL THE DEMO'S. The figure is its
keyframes through its skeleton at its eight-step interpolation, the
scenery and the logos are its bitmaps, the schedule is its frame
numbers and its hold counts, and the palette is the ramp its own code
writes.

ONE THING IS OURS AND IT SAYS SO: the palette. The demo programs a
greyscale ramp once and then resets the DAC three times without
reprogramming it, so on a conforming VGA the mode 13h part is black
on black - docs/ukko/subsystems/video.txt. The sim keeps the ramp the
code writes, because rendering the bug faithfully shows nothing.

THE RATE IS THE DEMO'S. It waits one vertical retrace a frame, which
is 70 Hz in mode 13h, and advances a keyframe every EIGHTH frame -
docs/ukko/subsystems/timeline.txt section 2. That is 8.75 keyframes a
second, and 694 of them plus the holds is about ninety-four seconds.
"""

import ukko.parts as P


class Demo(object):

    # img 1020:f044 - one vertical retrace a frame, 70 Hz in mode 13h.
    FPS = 70

    HELP = (
        ('.  ,', 'step one frame forward / back (while paused)'),
        ('f', 'cycle the tick rate: 70, 35, 18, 140'),
        ('b', 'show the backdrop only, without the figure'),
        ('j', 'mark the twelve joints'),
        ('t', 'turn the tween off - keyframes only, as it was blocky'),
        ('s   [  ]', 'next / previous section of the run (OURS)'),
    )

    def __init__(self, screen):
        self.s = screen
        self.parts = [('demo', "StickMan's World, the whole run", None),
                      ('logo', 'logo: StickMan\'s World', 0),
                      ('logo', 'logo: good muzac', 1),
                      ('logo', 'logo: THE PRODIGY', 2)]
        self.index = 0
        self.frame = 0
        self.paused = False
        self.status = ''
        self.show_backdrop = True
        self.show_joints = False
        self.tween = True
        self._rates = (70, 35, 18, 140)
        self._rate_i = 0
        self._bd = {}
        self._logo = {}
        self.reseed()

    # ------------------------------------------------------------ keys

    def select(self, n):
        if 0 <= n < len(self.parts):
            self.index = n
            self.reseed()

    def advance(self, d):
        """OURS. Step between the run's sections - the stretch of
        dancing after each interlude - rather than between the parts
        list, and land on the dancing rather than on a hold. The
        logos are still on the number keys.
        """
        self.index = 0
        n = len(P.SECTION_KF)
        i = max(j for j in range(n) if P.SECTION_KF[j] <= self.kf)
        kf = P.SECTION_KF[(i + d) % n]
        self.kf, self.sub = kf, 0
        self.hold, self.held = 0, None
        self.shown = set(at for at, _ in P.LOGO_AT if at < kf)

    def reseed(self):
        self.frame = 0
        self.kf = 0
        self.sub = 0
        self.hold = P.TITLE_HOLD
        self.held = 0           # which logo is on screen, or None
        self.shown = set()

    def key(self, k):
        if k == 'period':
            self.paused = True
            self._tick()
        elif k == 'comma':
            self.paused = True
            self.sub = (self.sub - 1) % P.SUBFRAMES
            if self.sub == P.SUBFRAMES - 1:
                self.kf = max(0, self.kf - 1)
        elif k in ('f', 'F'):
            self._rate_i = (self._rate_i + 1) % len(self._rates)
            self.FPS = self._rates[self._rate_i]
        elif k in ('b', 'B'):
            self.show_backdrop = not self.show_backdrop
        elif k in ('j', 'J'):
            self.show_joints = not self.show_joints
        elif k in ('t', 'T'):
            self.tween = not self.tween
        elif k in ('s', 'S'):
            self.advance(1)

    # ------------------------------------------------------------ frame

    def step(self):
        if self.parts[self.index][0] == 'logo':
            self.draw_logo(self.parts[self.index][2])
        else:
            self.draw_run()
        self.s.blit()
        if not self.paused:
            self.frame += 1
            self._tick()

    def _tick(self):
        """ORIGIN depacked 1020:e6b6 - one pass of Demo_FrameLoop.

        g_PartIndex counts 1, then 3 to 9, and only the PartIndex 1
        pass reaches the advance at 1020:efda - the tween path jumps
        over it at 1020:e820. So the cursor moves once every EIGHT
        display frames.
        """
        if self.hold > 0:
            self.hold -= 1
            return
        self.held = None
        self.sub += 1
        if self.sub >= P.SUBFRAMES:
            self.sub = 0
            self.kf += 1
            if self.kf >= P.POSE_FRAMES:
                self.reseed()
                return
            for at, which in P.LOGO_AT:
                if at == self.kf and at not in self.shown:
                    self.shown.add(at)
                    self.held = which
                    self.hold = P.LOGO_HOLD

    def _cached(self, store, which, build):
        if which not in store:
            store[which] = build(which)
        return store[which]

    def draw_logo(self, which):
        """ORIGIN depacked 1020:e64b and 1020:f19d - mode 0Dh, one
        plane straight to 0xa000, held, then mode 13h again."""
        self.s.fb[:] = self._cached(self._logo, which, P.logo_plane)
        self.status = 'mode 0Dh, one bitplane  [%d fps]' % self.FPS

    def draw_run(self):
        """ORIGIN depacked 1020:e6b6 - the whole run, holds included.

        The backdrop overwrites the back buffer, the twelve bones are
        drawn over it, and the whole 64,000 bytes go to the screen.
        There is no clear anywhere - the backdrop IS the clear.
        """
        if self.held is not None:
            self.draw_logo(self.held)
            self.status = ('%s  hold %3d  [%d fps]'
                           % ('title' if self.held == 0 else 'interlude',
                              self.hold, self.FPS))
            return

        scene = P.scene_for(self.kf)
        fb = self.s.fb
        if self.show_backdrop:
            fb[:] = self._cached(self._bd, scene, P.backdrop)
        else:
            fb[:] = bytes(len(fb))

        cur = P.pose(self.kf)
        prev = P.pose(self.kf - 1) if self.kf else cur
        sub = self.sub if self.tween else 0
        js = [P.pose_tween(prev, cur, j, sub) for j in range(P.POSE_JOINTS)]
        for a, b in P.BONES:
            P.line_draw(fb, js[a][0], js[a][1], js[b][0], js[b][1])
        if self.show_joints:
            for x, y in js:
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        px, py = x + dx, y + dy
                        if 0 <= px < P.SCREEN_W and 0 <= py < P.SCREEN_H:
                            fb[py * P.SCREEN_W + px] = P.PEN

        self.status = ('kf %3d/%d  %d/8  scene %d%s  [%d fps]'
                       % (self.kf, P.POSE_FRAMES - 1, sub, scene,
                          '' if self.tween else '  NO TWEEN', self.FPS))
