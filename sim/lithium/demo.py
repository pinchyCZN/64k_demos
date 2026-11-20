"""LITHIUM's part list and its keys.

    py -3.10 sim/run.py lithium
    py -3.10 sim/run.py lithium --list

ALMOST ALL OF THIS IS LITHIUM'S ARITHMETIC. parts.py transcribes
Mesh_Load, the three rotations, Mesh_Transform, Scene_ProjectVerts and
the cull out of Scene_DrawFaces, so a model goes from packed bytes to
screen coordinates through the demo's own code, truncation included -
docs/lithium/subsystems/mesh3d.txt and raster.txt.

What is OURS, and says so on screen:

    the angles       the spin. Nothing read yet says what drives
                     g_ObjAngle* or g_SlotAngle* per scene
    the distance     the demo scales each object with a per-object
                     g_ObjScaleShift that nothing read yet sets, so
                     the sim leaves the transform at its identity
                     shift and frames the model with the camera
                     instead - see frame_dist
    the sort order   far to near. The key is the demo's; the
                     DIRECTION is an assumption, because
                     maybe_Sort_ByDepth is unread
    the fill         a flat scanline fill. The real one fetches a
                     texel per pixel
    the palette      g_PalMain is zero in the image, so the index is
                     shown as grey - CLAUDE.md rule 12
    nothing about    the tunnel is the demo's end to end. The ring
    the tunnel       window, the path, the velocity, the camera
                     position AND its orientation are all
                     transcribed - tunnel.txt sections 5 to 7. The
                     eye distance keys do nothing on this part,
                     because the demo has no such term and adding one
                     turns with the view instead of pushing back
    THE ROCK in      the demo's tunnel scene has NO MESH - it zeroes
    the tunnel       the vertex pool and builds only the bore. The
                     rock flies in the phase before. Putting them in
                     one picture is the sim's idea and nothing says
                     the demo ever did. Press t to turn it off

The u and v modes are NOT a texture lookup. The real span routine
fetches gs:[(v << 8) | u] from a 256x256 map, and that map is built at
run time in a segment above the image, so there is nothing to fetch
from. These modes draw the COORDINATE as a grey level instead, and the
banding they show belongs to the sphere map - that is the chrome.

Keys, on top of the harness ones - and F1 lists them all:

    w   textured / env-map u / wireframe / points
    b   cycle the span band offset: +0x00, +0x40, +0xc0
    p   cycle the palette: animated, g_Pal1, g_Pal2
    n   draw the per-vertex normals as spikes
    s   stop and start the model spin
    e   stop and start the env-map rotation on its own
    ,.  slower and faster
    +-  move the camera in and out
"""

from lithium import parts as P


_FALLOFF = (0x29, -0x30)      # see BUMP_LIGHT_SPOTS


class Demo(object):

    # The F1 screen. harness.HELP_COMMON has Esc, SPACE, 1-9, [ ] and R.
    HELP = (
        ('w', 'draw mode: textured / env-map u / wireframe / points'),
        ('b', 'span band offset: +0x00, +0x40, +0xc0'),
        ('p', 'palette: animated (Pal_Animate), g_Pal1, g_Pal2'),
        ('n', 'per-vertex normals as spikes'),
        ('s', 'spin on/off - also the tunnel fly-through'),
        ('e', 'env-map angle spin on/off'),
        (',  .', 'slower / faster'),
        ('-  +', 'camera further / nearer - not on the tunnel'),
        ('t', 'tunnel: show the rock (Tick_Rock)'),
        ('l', 'bump: let the light wander (it is held by default)'),
        ('g', 'bump: park the light on the next of the three glows'),
        ('i', 'bump: draw both fields a frame (OURS - kills the shimmer)'),
        ('k', 'bump: drain the off-screen rows (OURS - kills the band)'),
        ('p', 'bump: set the palette to "animated" for the cycling'),
    )

    # OURS. It has to exceed the ~200-tick settle - dust.txt 2.
    BUMP_REFILL = 260
    BUMP_LEN = 1100             # OURS - the demo breaks on the rock's
                                # drawn area, bump.txt section 5
    BUMP_FADE = 0               # NO bias at all - see draw_bump
    BUMP_SLIDE0 = 0x2ee0        # g_RockSlideX as loaded, 1000:8ff9
    BUMP_PHASE0 = 0x0b          # g_DustMaskShift as loaded, 1000:8ff7
    # OURS. The light is ONE offset into the picture and the three
    # bright spots are baked into it at fixed centres, so they slide
    # together - there is no way to hold one and move the others.
    # These park it on each glow's FALLOFF in turn; at a disc's centre
    # the relief inverts. bump.txt section 4.
    BUMP_LIGHT_SPOTS = tuple((((gx + _FALLOFF[0]) & 0xff) << 8,
                              ((gy + _FALLOFF[1]) & 0xff) << 8)
                             for gx, gy in P.GLOW_CENTRES)
    BUMP_LIGHT_SPOT = 1         # the one the scene opens on
    MODES = ('textured', 'env-map u', 'wireframe', 'points')
    BANDS = (0x00, 0x40, 0xc0)          # Poly_Fill's three span offsets
    PALS = ('animated', 'g_Pal1', 'g_Pal2')

    def __init__(self, screen):
        self.s = screen
        self.frame = 0
        self.paused = False
        self.index = 0
        self.mode = 0
        self.show_n = False
        self.spin = True
        self.env_spin = True
        self.rate = 1
        self.ax = 0
        self.ay = 0
        self.az = 0
        self.env = 0
        self.dist = 600                 # ours: far enough to clear
        self.cache = {}                 # the 0x7d near plane
        self.band = 0
        self.pal = 2
        self.set_pal()

        self.parts = [('mesh', lbl, off) for off, lbl in P.MESHES]
        self.parts.append(('sin', 'the sine table, 2048 entries and the '
                                  'wrap tail', None))
        self.parts.append(('vol', 'mixer volume table, 0x41 x 0x100', None))
        self.parts.append(('tex', 'Tex_BuildSphereMap, the generated '
                                  'environment map', None))
        self.parts.append(('pal', 'the three palettes', None))
        self.parts.append(('tunnel', 'Tunnel_Build, 15 rings of 16, swept '
                                     'along a 4:2:1 curve', None))
        self.parts.append(('bump', 'the bump scene: 1000 particles spell '
                                   'VOTE in a height field', None))
        self.bump = None
        self.tex_override = None
        self.tex_stride = 256
        self.bump_light_anim = False
        self.bump_spot = self.BUMP_LIGHT_SPOT
        self.bump_both = False      # OURS - see draw_bump
        self.bump_drain = False     # OURS - see draw_bump
        self.tun = P.tunnel_tick_init()  # 1000:6b7f's own state
        self.tun_t = 0                  # ours: drives the wrap, see
        self.tun_rock = True            # the rock, rock.txt
        self.rock = P.rock_tick_init()
        self.rock_pos = (0, 0, 0)
        self.rock_ang = (0, 0, 0)
        self.dist = self.dist           # draw_tunnel
        self.frame_dist()

    # ------------------------------------------------------- harness

    @property
    def status(self):
        kind, lbl, _ = self.parts[self.index]
        extra = ''
        if kind == 'mesh':
            extra = ('  %s  band +0x%02x  ang %d,%d,%d  env %d  dist %d'
                     % (self.MODES[self.mode], self.BANDS[self.band],
                        self.ax, self.ay, self.az, self.env, self.dist))
        elif kind == 'bump':
            b = self.bump
            extra = ('  frame %d  phase 0x%02x  light %s  %s'
                     % (b['n'] if b else 0, b['phase'] if b else 0,
                        'wandering' if self.bump_light_anim
                        else 'glow %d' % self.bump_spot,
                        'SHATTER' if b and b['shards'] else
                        ('BURST' if b and b['phase'] == 1 else
                         'letters' if b and 8 <= b['phase'] <= 0xb else
                         'bands')))
        elif kind == 'tunnel':
            extra = ('  %s  band +0x%02x  ring %d  ang %d,%d,%d%s'
                     % (self.MODES[self.mode], self.BANDS[self.band],
                        self.tun['first'],
                        self.tun['cam'][0] & 0x7ff,
                        self.tun['cam'][1] & 0x7ff,
                        self.tun['cam'][2] & 0x7ff,
                        ''))
        return ('%d/%d %s%s   pal %s'
                % (self.index + 1, len(self.parts), lbl, extra,
                   self.PALS[self.pal]))

    def select(self, i):
        if 0 <= i < len(self.parts):
            self.index = i
            self.frame_dist()

    def frame_dist(self):
        """Put the selected model on screen. OURS, and it has to be:
        the models are authored at wildly different scales - the ships
        reach 80 units and the tower only 56 across many more vertices -
        and the demo handles that with a PER-OBJECT g_ObjScaleShift that
        nothing read yet sets. So the sim leaves the transform at its
        identity shift and moves the camera instead, which is a
        parameter of nothing but this viewer."""
        kind, _, off = self.parts[self.index]
        if kind != 'mesh':
            return
        if off not in self.cache:
            self.cache[off] = P.mesh_load(off)
        raw = self.cache[off][0]
        ext = max(1, max(max(abs(c) for c in v) for v in raw))
        self.dist = max(P.NEAR_Z * 2, ext * (1 << P.FOCAL) // 80)

    def advance(self, d):
        self.index = (self.index + d) % len(self.parts)
        self.frame_dist()

    def reseed(self):
        self.ax = self.ay = self.az = self.env = 0

    def key(self, k):
        if k in ('w', 'W'):
            self.mode = (self.mode + 1) % len(self.MODES)
        elif k in ('n', 'N'):
            self.show_n = not self.show_n
        elif k in ('s', 'S'):
            self.spin = not self.spin
        elif k in ('e', 'E'):
            self.env_spin = not self.env_spin
        elif k == 'comma':
            self.rate = max(0, self.rate - 1)
        elif k == 'period':
            self.rate = min(8, self.rate + 1)
        elif k in ('minus', 'KP_Subtract'):
            self.dist = min(20000, int(self.dist * 1.25) + 1)
        elif k in ('plus', 'equal', 'KP_Add'):
            self.dist = max(P.NEAR_Z, int(self.dist / 1.25))
        elif k in ('b', 'B'):
            self.band = (self.band + 1) % len(self.BANDS)
        elif k in ('p', 'P'):
            self.pal = (self.pal + 1) % len(self.PALS)
            self.set_pal()
        elif k in ('t', 'T'):
            self.tun_rock = not self.tun_rock
        elif k in ('l', 'L'):
            self.bump_light_anim = not self.bump_light_anim
        elif k in ('g', 'G'):
            self.bump_spot = (self.bump_spot + 1) % len(self.BUMP_LIGHT_SPOTS)
        elif k in ('i', 'I'):
            self.bump_both = not self.bump_both
        elif k in ('k', 'K'):
            self.bump_drain = not self.bump_drain

    def set_pal(self):
        if self.pal == 0:
            self.s.set_palette(P.pal_animate())
        elif self.pal == 1:
            self.s.set_palette(P.pal_stored(P.PAL_1))
        else:
            self.s.set_palette(P.pal_stored(P.PAL_2))

    def step(self):
        self.frame += 1
        # 2048 angle units to the circle - parts.sin_table_build.
        if self.spin:
            self.ay = (self.ay + 3 * self.rate) & 0x7ff
            self.ax = (self.ax + 1 * self.rate) & 0x7ff
        if self.env_spin:
            self.env = (self.env + 2 * self.rate) & 0x7ff
        if self.spin and self.parts[self.index][0] == 'tunnel':
            # Tick_Tunnel runs once a tick; self.rate is ours.
            for _ in range(max(1, self.rate)):
                P.tunnel_tick(self.tun)
                self.rock_pos, self.rock_ang = P.rock_tick(self.rock)
                if P.rock_gone(self.rock):      # img 0x0ac2
                    self.rock = P.rock_tick_init()
                self.tun_t += 1
        if self.pal == 0:               # Pal_Animate runs per frame
            self.s.set_palette(P.pal_animate())
        kind, _, arg = self.parts[self.index]
        self.s.clear(0)
        if kind == 'mesh':
            self.draw_mesh(arg)
        elif kind == 'sin':
            self.draw_sin()
        elif kind == 'vol':
            self.draw_vol()
        elif kind == 'tex':
            self.draw_tex()
        elif kind == 'tunnel':
            self.draw_tunnel()
        elif kind == 'bump':
            self.draw_bump()
        else:
            self.draw_note()
        self.s.blit()

    # ---------------------------------------------------------- draw

    def _px(self, x, y, c):
        if 0 <= x < P.SCREEN_W and 0 <= y < P.SCREEN_H:
            self.s.fb[y * P.SCREEN_W + x] = c

    def _line(self, x0, y0, x1, y1, c):
        dx, dy = abs(x1 - x0), abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx - dy
        while True:
            self._px(x0, y0, c)
            if x0 == x1 and y0 == y1:
                return
            e2 = err * 2
            if e2 > -dy:
                err -= dy
                x0 += sx
            if e2 < dx:
                err += dx
                y0 += sy

    def _tri(self, p0, p1, p2, c):
        """Flat scanline fill. OURS - no rasteriser of Lithium's has
        been found, so this makes no claim to match one."""
        (x0, y0), (x1, y1), (x2, y2) = sorted((p0, p1, p2),
                                              key=lambda p: p[1])
        if y2 == y0:
            return
        for y in range(max(0, y0), min(P.SCREEN_H, y2 + 1)):
            xa = x0 + (x2 - x0) * (y - y0) // (y2 - y0)
            if y < y1:
                xb = x0 + (x1 - x0) * (y - y0) // (y1 - y0) if y1 != y0 else x0
            else:
                xb = x1 + (x2 - x1) * (y - y1) // (y2 - y1) if y2 != y1 else x1
            if xa > xb:
                xa, xb = xb, xa
            row = y * P.SCREEN_W
            for x in range(max(0, xa), min(P.SCREEN_W, xb + 1)):
                self.s.fb[row + x] = c

    def _tri_tex(self, p0, p1, p2, uv0, uv1, uv2, band):
        """A textured triangle.

        THE TEXEL FETCH AND THE BAND OFFSET ARE LITHIUM'S - the span
        routines do exactly gs:[(v << 8) | u] and then add 0x00, 0x40
        or 0xc0 - but INTERPOLATING u AND v ACROSS THE TRIANGLE IS
        OURS. The original steps them per pixel from gradients
        Poly_Fill sets up, and that setup has not been read, so the
        linear interpolation here makes no claim to match it.
        """
        tex = self.tex_override or P.sphere_map()
        pts = sorted(((p0, uv0), (p1, uv1), (p2, uv2)), key=lambda e: e[0][1])
        (x0, y0), (u0, v0) = pts[0]
        (x1, y1), (u1, v1) = pts[1]
        (x2, y2), (u2, v2) = pts[2]
        if y2 == y0:
            return
        fb = self.s.fb
        for y in range(max(0, y0), min(P.SCREEN_H, y2 + 1)):
            ta = float(y - y0) / (y2 - y0)
            xa = x0 + (x2 - x0) * ta
            ua = u0 + (u2 - u0) * ta
            va = v0 + (v2 - v0) * ta
            if y < y1:
                tb = float(y - y0) / (y1 - y0) if y1 != y0 else 0.0
                xb = x0 + (x1 - x0) * tb
                ub = u0 + (u1 - u0) * tb
                vb = v0 + (v1 - v0) * tb
            else:
                tb = float(y - y1) / (y2 - y1) if y2 != y1 else 0.0
                xb = x1 + (x2 - x1) * tb
                ub = u1 + (u2 - u1) * tb
                vb = v1 + (v2 - v1) * tb
            if xa > xb:
                xa, xb = xb, xa
                ua, ub = ub, ua
                va, vb = vb, va
            n = int(xb) - int(xa)
            if n < 0:
                continue
            du = (ub - ua) / n if n else 0.0
            dv = (vb - va) / n if n else 0.0
            row = y * P.SCREEN_W
            u, v = ua, va
            for x in range(int(xa), int(xb) + 1):
                if 0 <= x < P.SCREEN_W:
                    # The sphere map is 256 wide; the shatter's source
                    # is the 320-wide screen snapshot, which is why
                    # Shatter_Draw installs a DIFFERENT span routine
                    # (0x3c46) from Poly_SpanTextured.
                    if self.tex_stride == 256:
                        o = ((int(v) & 0xff) << 8) | (int(u) & 0xff)
                    else:
                        o = (int(v) * 320 + int(u)) & 0xffff
                    fb[row + x] = (tex[o] + band) & 0xff
                u += du
                v += dv

    def draw_mesh(self, off):
        if off not in self.cache:
            self.cache[off] = P.mesh_load(off)
        raw, _, normals = self.cache[off]

        # LITHIUM'S OWN PIPELINE, end to end: Mesh_Transform then
        # Scene_ProjectVerts then the cull. The angles and the camera
        # distance going in are ours; nothing the calls do with them is.
        verts, faces = P.mesh_transform(off, normals,
                                        (self.ax, self.ay, self.az),
                                        (0, 0, 0), env_angle=self.env)
        pv = P.scene_project_verts(verts, cam_pos=(0, 0, self.dist))
        order = P.scene_cull_sort(pv, faces)
        band = self.BANDS[self.band]

        if self.mode == 3:
            for v in pv:
                self._px(v[0], v[1], 200)
            return

        for _, f in order:
            i0, i1, i2 = faces[f]
            p0 = (pv[i0][0], pv[i0][1])
            p1 = (pv[i1][0], pv[i1][1])
            p2 = (pv[i2][0], pv[i2][1])
            if self.mode == 2:
                for a, b in ((p0, p1), (p1, p2), (p2, p0)):
                    self._line(a[0], a[1], b[0], b[1], 150)
            elif self.mode == 0:
                self._tri_tex(p0, p1, p2,
                              (pv[i0][3], pv[i0][4]),
                              (pv[i1][3], pv[i1][4]),
                              (pv[i2][3], pv[i2][4]), band)
            else:
                c = (pv[i0][3] + pv[i1][3] + pv[i2][3]) // 3
                self._tri(p0, p1, p2, max(0, min(255, c)))

        if self.show_n:
            for i in range(len(pv)):
                nx, ny, _ = normals[i]
                self._line(pv[i][0], pv[i][1],
                           int(pv[i][0] + nx / 2048.0 * 10),
                           int(pv[i][1] - ny / 2048.0 * 10), 255)

    def draw_tunnel(self):
        """Tunnel_Build and its faces, drawn through the demo's own
        projection. docs/lithium/effects/tunnel.txt.

        LITHIUM'S: the ring, the path, the three advancing angles, the
        u and v, the face winding, the near cull, and THE WRAP.
        maybe_Scene_Tunnel splits one position into a COARSE part that
        seeds the path - img 0x04a3, (pos >> 0xc) << 8 - and a FINE
        part that becomes the camera - img 0x0449, (-(pos+1) & 0xfff)
        >> 4. Together they are pos/16, so the geometry re-seeds every
        0x1000 while the camera offset wraps, and the numbers stay
        small however far you fly.

        OURS: what advances that position. In the demo it comes from a
        tick handler at 1000:6b7f that has not been read, so the sim
        drives it from the frame counter and says so on screen.
        """
        st = self.tun
        rings = st['last'] - st['first']
        hx, hy, hz = st['home']
        # img 0x04a3: the path starts at the position's COARSE part,
        # and the Z is NEGATED. Tunnel_Build then pre-steps
        # g_TunnelFirstRing rings and throws them away.
        path = ((hx >> 0xc) << 8, 0, -((hz >> 0xc) << 8))
        verts, _, _ = P.tunnel_build(path, (0, 0, 0), rings,
                                     skip=st['first'])
        # img 0x0435: the camera is the position's FINE part. Coarse
        # plus fine is pos/16, and THAT IS WHAT KEEPS THE EYE ON THE
        # WINDOW - tunnel.txt section 6.
        cam = ((hx & 0xfff) >> 4, hy >> 4, (-(hz + 1) & 0xfff) >> 4)
        faces = P.tunnel_faces(rings)
        # The angles come via Obj_LoadOrientA, and there is NO
        # distance term - tunnel.txt section 7.
        pv = P.scene_project_verts(verts, cam_pos=cam,
                                   cam_ang=tuple(st['cam']))
        # maybe_Scene_Tunnel's own near cull: cs:[0x8f80] = 0x36b,
        # img 0x040b, which Scene_DrawFaces compares the summed depth
        # against at img 0x32aa.
        order = P.scene_cull_sort(pv, faces, depth_near=0x36b)
        band = self.BANDS[self.band]

        if self.mode == 3:
            for v in pv:
                self._px(v[0], v[1], 200)
            return
        for _, n in order:
            i0, i1, i2 = faces[n]
            p0 = (pv[i0][0], pv[i0][1])
            p1 = (pv[i1][0], pv[i1][1])
            p2 = (pv[i2][0], pv[i2][1])
            if self.mode == 2:
                for a, b in ((p0, p1), (p1, p2), (p2, p0)):
                    self._line(a[0], a[1], b[0], b[1], 150)
            else:
                self._tri_tex(p0, p1, p2,
                              (pv[i0][3], pv[i0][4]),
                              (pv[i1][3], pv[i1][4]),
                              (pv[i2][3], pv[i2][4]), band)
        if self.tun_rock:
            self._tunnel_rock(band)

    def _tunnel_rock(self, band):
        """The rock, in the camera-less frame the demo draws it in.
        docs/lithium/effects/rock.txt.

        ALL OF IT IS LITHIUM'S. rock_tick is Tick_Rock and the
        Obj_WobblePos inside it; the camera is zeroed exactly as img
        0x0a2e to 0x0a6e does, so the rock sits in the middle of the
        view while the tunnel sweeps past behind it.

        The one unknown is where g_SlotHomeZ starts - nothing in the
        runner writes it - so the sim starts it at zero and the rock
        lasts 58 ticks, rock.txt section 5.
        """
        off = P.ROCK_MESH
        if off not in self.cache:
            self.cache[off] = P.mesh_load(off)
        _, _, normals = self.cache[off]
        pos, ang = self.rock_pos, self.rock_ang
        verts, faces = P.mesh_transform(off, normals, ang, pos,
                                        env_angle=self.env)
        # img 0x0a2e: the camera position AND all three angles are
        # zeroed before this draw.
        pv = P.scene_project_verts(verts, cam_pos=(0, 0, 0),
                                   cam_ang=(0, 0, 0))
        for _, n in P.scene_cull_sort(pv, faces):
            i0, i1, i2 = faces[n]
            p0 = (pv[i0][0], pv[i0][1])
            p1 = (pv[i1][0], pv[i1][1])
            p2 = (pv[i2][0], pv[i2][1])
            if self.mode == 2:
                for c, d in ((p0, p1), (p1, p2), (p2, p0)):
                    self._line(c[0], c[1], d[0], d[1], 150)
            elif self.mode != 3:
                self._tri_tex(p0, p1, p2,
                              (pv[i0][3], pv[i0][4]),
                              (pv[i1][3], pv[i1][4]),
                              (pv[i2][3], pv[i2][4]), band)

    def bump_reset(self):
        """Phase one of maybe_Scene_BumpShatter, img 0x0665."""
        P.rnd_reset()
        P.rnd_b_reset()
        # img 0x0765: the scene SEEDS the palette phases and steepens
        # every ramp. Both change what the cycling looks like.
        P.pal_phase_seed(0xf8a3, 0xf6f0, 0xf41b)
        pool = [[0] * 6 for _ in range(P.DUST_COUNT)]
        P.dust_reset_from_point(pool)                  # img 0x075c
        mask = P.text_draw_into_mask('VOTE')           # img 0x0698
        P.dust_sample_mask(pool, mask, self.BUMP_PHASE0)
        self.bump = {'pool': pool, 'field': bytearray(0x10000),
                     'pic': P.tex_build_three_glows(), 'n': 0,
                     'light': P.bump_light_init(),
                     # img 0x07cd sets g_BumpInterlace = 0, which
                     # selects the HALF-AMPLITUDE brush B.
                     'brush': P.dust_brush(0), 'il': 0,
                     'slide': self.BUMP_SLIDE0, 'mask': mask,
                     'dust': P.dust_state_init(self.BUMP_PHASE0),
                     'phase': self.BUMP_PHASE0,
                     'shards': None, 'snap': None}

    def draw_bump(self):
        """Phase two of maybe_Scene_BumpShatter, once a frame.
        docs/lithium/effects/bump.txt and effects/dust.txt.

        ALL OF IT IS LITHIUM'S except the seed for Rnd_NextB, which
        is zero in the image and which nothing read sets.

        THE LETTERS ARE MAGNIFIED BY THE SAMPLER, not drawn large.
        Dust_SampleMask reads the mask at ax >> 0xa, so one mask
        column covers EIGHT screen columns, and its row index spans
        only seven of the mask's twenty - so VOTE is drawn four
        glyphs wide and blown up to fill the screen.
        """
        if self.bump is None:
            self.bump_reset()
        b = self.bump
        b['n'] += 1
        # THE REFILL IS GATED ON THE MUSIC POSITION CHANGING - img
        # 0x6ad6. There is no music here, so the interval is OURS.
        if b['n'] % self.BUMP_REFILL == 0:
            b['phase'] = P.dust_mode_step(b['pool'], b['mask'], b['dust'])
        P.dust_tick(b['pool'], b['dust']['jitter'])    # img 0x6a7a
        if b['slide'] > 0:                             # img 0x6a4d
            b['slide'] -= 0x20
        il = 0 if b['slide'] >= 0xfa0 else 1           # img 0x07f4
        if il != b['il']:
            b['il'], b['brush'] = il, P.dust_brush(il)
        P.dust_stamp(b['field'], b['pool'], b['brush'])   # img 0x0806
        P.height_relax(b['field'], il, b['n'])
        beat = (b['n'] % self.BUMP_REFILL == 0)        # ours: the beat
        if self.bump_light_anim:
            light = P.bump_light_tick(b['light'], beat)   # img 0x6244
        else:
            light = self.BUMP_LIGHT_SPOTS[self.bump_spot]
        # BOTH OURS, AND BOTH DIVERGENCES. The demo repaints one field
        # a frame while the flag is clear, and nothing in it ever
        # drains the field's rows past 199 - bump.txt section 5. These
        # only exist so the surface can be judged without either.
        if self.bump_drain:
            for r in range(199, 205):
                b['field'][r * 320:(r + 1) * 320] = bytes(320)
        P.bump_render(b['field'], b['pic'], light, self.s.fb,
                      il, b['n'])
        if self.bump_both and not il:
            P.bump_render(b['field'], b['pic'], light, self.s.fb,
                          il, b['n'] + 1)
        if self.pal == 0:
            # img 0x078e: g_PalRampShift = 2 for this scene.
            # NO PALETTE BIAS, AND THAT IS A DELIBERATE DIVERGENCE.
            # The demo eases g_PalFade toward 0xff40 - a bias of -48,
            # nearly black - and Tick_Default snaps it back to
            # cs:[0x8fe4] = 0x80 on EVERY MUSIC POSITION CHANGE, img
            # 0x6cd8. A bias of +32 saturates all 64 entries to white,
            # so that oscillation IS the screen-wide flash on the
            # beat. Neither end is worth watching: this takes the raw
            # cycling colours and leaves the strobe out.
            self.s.set_palette(P.pal_upload(P.pal_animate(2),
                                            self.BUMP_FADE))

        # PHASE THREE, img 0x0942: snapshot the bump-mapped frame into
        # the texture buffer - Tex_SnapshotFrom - and break it.
        if b['shards'] is None and b['n'] >= self.BUMP_LEN:
            b['snap'] = bytes(self.s.fb) + bytes(0x10000 - len(self.s.fb))
            b['shards'] = P.shatter_build()
        if b['shards'] is not None:
            self._draw_shards(b)

    def _draw_shards(self, b):
        """Phase three and four's glass, textured from the snapshot.
        docs/lithium/effects/shatter.txt.
        """
        P.shatter_step(b['shards'])
        tris = P.shatter_project(b['shards'])
        self.s.clear(0)
        self.tex_override = b['snap']
        self.tex_stride = 320
        try:
            for pts, uvs in tris:
                self._tri_tex(pts[0], pts[1], pts[2],
                              uvs[0], uvs[1], uvs[2], 0)
        finally:
            self.tex_override = None
            self.tex_stride = 256
        if not tris:                                   # all gone, restart
            self.bump = None

    def draw_tex(self):
        """The generated environment map, with the band offset applied
        so the palette ramp in use is visible. 256x200 of the 256x256,
        centred on screen."""
        tex = P.sphere_map()
        band = self.BANDS[self.band]
        for y in range(P.SCREEN_H):
            row = y * P.SCREEN_W
            base = ((y + 28) & 0xff) << 8
            for x in range(256):
                self.s.fb[row + x + 32] = (tex[base | x] + band) & 0xff

    def draw_sin(self):
        """The table the rotations index. Entries 0 to 2047 are one full
        period; the tail from 2048 is the wrap that lets cosine be read
        as sine 512 entries on. The lighter trace IS that cosine."""
        t = P.sin_table()
        mid = P.SCREEN_H // 2
        for x in range(P.SCREEN_W):
            i = x * 2048 // P.SCREEN_W
            self._px(x, mid - t[i] * 80 // 4096, 220)
            self._px(x, mid - t[i + 512] * 80 // 4096, 120)
        for x in range(P.SCREEN_W):
            self._px(x, mid, 60)
        for q in range(1, 4):                   # the quadrant marks
            x = q * P.SCREEN_W // 4
            for y in range(mid - 84, mid + 84, 4):
                self._px(x, y, 80)

    def draw_vol(self):
        tbl = P.vol_table_build()
        for y in range(min(P.SCREEN_H, len(tbl) * 3)):
            row = tbl[min(len(tbl) - 1, y // 3)]
            for x in range(min(P.SCREEN_W, 0x100)):
                self._px(x + 32, y + 2, row[x])

    def draw_note(self):
        """What IS known about the palette, in two facts.

        g_PalMain at img 0x96be is 768 zero bytes, so the real ramp is
        built at run time and nothing that does has been found. What
        Pal_Upload does to a ramp is known exactly, so the three bars
        show the grey ramp under the three g_PalFade values Demo_Main
        stores: 0, +0x100 and -0x80.

        The palette itself stays the grey ramp, so a bar shows a faded
        DAC value v by picking the index that displays as grey v. That
        is a way of drawing the result, not a second palette.
        """
        src = pal = P.pal_build_main()
        for n, fade in enumerate((0, 0x100, -0x80)):
            out = P.pal_upload(src, fade)
            y0 = 24 + n * 54
            for x in range(256):
                c = min(255, out[x * 3] * 4)
                for y in range(y0, y0 + 40):
                    self._px(x + 32, y, c)
        self.s.set_palette(pal)
