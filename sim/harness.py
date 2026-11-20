"""The sim harness: a 320x200 indexed framebuffer in a tkinter window,
and the frame loop around it. SHARED BY EVERY DEMO.

Nothing here knows about a particular demo. A demo supplies two
things:

    a parts module    with SCREEN_W, SCREEN_H, pal_build_main and
                      pal_to_rgb - the framebuffer conventions
    a Demo class      with parts, index, frame, paused, status,
                      select(n), advance(d), reseed(), step() and
                      key(k), and optionally HELP - a list of
                      (keys, what it does) the F1 screen appends to
                      the generic ones - and optionally FPS, the
                      tick rate, for a demo whose own rate cannot be
                      read off its listing

The generic keys live here - Escape, SPACE, F1, the number keys,
[ and ] and R. EVERYTHING ELSE GOES TO demo.key(k), which is the point: a key
handler written as a method has a real `self`, where one written
inside main() silently does not.

Mode 13h is 320x200 with non-square pixels. Default --scale 2 is a
pixel-exact 640x400; --aspect gives the 640x480 a 1996 monitor showed
and is not pixel-exact. Pixel-exact is the default, because counting
pixels against the listing is what this is for.
"""

import tkinter


# The keys this module handles itself. A demo's own go in its HELP.
HELP_COMMON = (
    ('F1', 'this list'),
    ('Esc', 'quit'),
    ('SPACE', 'pause'),
    ('1 - 9', 'jump to that part'),
    ('[  ]', 'previous / next part'),
    ('R', 'reseed'),
)


class Screen(object):
    """A 320x200 indexed framebuffer and a window to blit it into."""

    def __init__(self, parts, scale=2, aspect=False):
        self.P = parts
        self.w, self.h = parts.SCREEN_W, parts.SCREEN_H
        self.scale = scale
        self.out_w = self.w * scale
        self.out_h = int(self.h * scale * 1.2) if aspect else self.h * scale
        self.fb = bytearray(self.w * self.h)
        self.base_pal = self.P.pal_build_main()
        self.lut = None
        self.set_palette(self.base_pal)
        self.header = b'P6\n%d %d\n255\n' % (self.out_w, self.out_h)
        self.rows = [scale] * self.h
        if aspect:
            self._set_aspect_rows()

        self.root = tkinter.Tk()
        self.root.title('sim')      # run() retitles every frame
        self.root.resizable(False, False)
        self.img = tkinter.PhotoImage(width=self.out_w, height=self.out_h)
        tkinter.Label(self.root, image=self.img, bd=0).pack()

    def set_palette(self, pal):
        """Index -> an output pixel's RGB repeated `scale` times, so the
        horizontal zoom costs nothing at blit time.
        """
        self.lut = [bytes(c) * self.scale for c in self.P.pal_to_rgb(pal)]

    def _set_aspect_rows(self):
        """Spread out_h rows over h sources, so 200 -> 480 without PIL."""
        rows = []
        step = float(self.out_h) / self.h
        acc = 0.0
        for _ in range(self.h):
            acc += step
            rows.append(max(1, int(acc + 0.5) - sum(rows)))
        while sum(rows) > self.out_h:
            rows[-1] -= 1
        while sum(rows) < self.out_h:
            rows[-1] += 1
        self.rows = rows

    def clear(self, index=0):
        self.fb[:] = bytes([index]) * (self.w * self.h)

    def blit(self):
        """One PPM, one configure.

        configure(data=) replaces the existing image's pixels, where
        PhotoImage(data=) would allocate a new one every frame and leak
        it until the interpreter noticed.
        """
        lut, w, parts = self.lut, self.w, [self.header]
        fb = self.fb
        for y in range(self.h):
            row = b''.join([lut[b] for b in fb[y * w:(y + 1) * w]])
            parts.append(row * self.rows[y])
        self.img.configure(data=b''.join(parts))

def run(parts, demo_cls, title, argv):
    """Build the window and the demo, then loop. argv is the tail of
    sys.argv - --scale N, --aspect and --list are understood here."""
    scale = int(argv[argv.index('--scale') + 1]) if '--scale' in argv else 2
    screen = Screen(parts, scale=scale, aspect='--aspect' in argv)
    demo = demo_cls(screen)

    if '--list' in argv:
        for n, (kind, label, _) in enumerate(demo.parts):
            print('%2d  %-5s %s' % (n + 1, kind, label))
        screen.root.destroy()
        return

    help_win = [None]

    def toggle_help():
        """F1. A plain tkinter window, not a framebuffer overlay - the
        framebuffer is 320x200 indexed and has no font, and drawing one
        would put a fake glyph set next to the demo's real one.
        """
        if help_win[0] is not None:
            help_win[0].destroy()
            help_win[0] = None
            return
        rows = list(HELP_COMMON) + list(getattr(demo, 'HELP', ()))
        wide = max(len(k) for k, _ in rows)
        text = chr(10).join('  %-*s   %s' % (wide, k, d) for k, d in rows)
        w = tkinter.Toplevel(screen.root)
        w.title('keys')
        w.resizable(False, False)
        tkinter.Label(w, text=text, font=('Courier New', 10),
                      justify='left', anchor='w', padx=10, pady=8).pack()
        w.bind('<Key>', lambda e: toggle_help())
        help_win[0] = w

    def on_key(ev):
        k = ev.keysym
        if k == 'F1':
            toggle_help()
        elif k == 'Escape':
            screen.root.destroy()
        elif k == 'space':
            demo.paused = not demo.paused
        elif k in '123456789':
            demo.select(int(k) - 1)
        elif k == 'bracketright':
            demo.advance(1)
        elif k == 'bracketleft':
            demo.advance(-1)
        elif k in ('r', 'R'):
            demo.reseed()
        else:
            demo.key(k)

    screen.root.bind('<Key>', on_key)

    def tick():
        if not demo.paused:
            demo.step()
            screen.root.title('%s  [%d]  %s'
                              % (title, demo.frame, demo.status))
        screen.root.after(max(1, 1000 // getattr(demo, 'FPS', 60)), tick)

    tick()
    screen.root.mainloop()
