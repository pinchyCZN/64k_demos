"""XCURSION's part list and the state its parts draw from.

Every part is one function, named for what it shows, and each one says
in its docstring which of its inputs are the demo's and which are
ours. See CLAUDE.md rules 12 and 15 - the window title carries the
same warning at run time.

    py -3.10 sim/run.py xcursion
"""

from xcursion import parts as P


class Demo(object):

    # The F1 screen. harness.HELP_COMMON has Esc, SPACE, 1-9, [ ] and R.
    HELP = (
        ('g', 'grey low palette on/off'),
        ('f', 'step the fade'),
        (',  .', 'voxel height scale down / up'),
        ('backslash', 'voxel water level'),
        ('k', 'voxel ripple on/off'),
        ('m', 'next model'),
        ('l', 'light: demo / view'),
        ('o  p', 'ground ramp down / up'),
        ('j', 'reset the moire'),
        ('z', 'reset the finale'),
        ('x', 'tunnel exit on/off'),
        ('v', 'reverse the beams'),
        ('n', 'reset the car'),
        ('b', 'battle mode 9 / 0xa / 0xb'),
        ('w', 'shot early on/off'),
        ('a  s', 'planet approach nearer / further'),
        ('c', 'camera yaw on/off'),
    )
    """The part list, and the state the parts draw from."""

    def __init__(self, screen):
        self.s = screen
        self.frame = 0
        self.paused = False
        self.index = 0
        self.status = ''
        self.grey = False
        self.fade = 0

        self.field_a = P.build_height_field_a()
        self.field_b = P.build_height_field_b()
        self.points = P.make_points()
        self.blob = P.countdown_blob()
        self.cd_window = 0
        self.cd_parts = []
        self.cd_bg = bytearray(P.SCREEN_W * P.SCREEN_H)
        self.text_mask = P.countdown_mask_as_bitmap(self.blob, 0)
        # Height and colour maps are Plasma_Generate output. The colour
        # map is shifted into 0..0x1f because Voxel_Render's own clamp
        # at img 0x3758 discards anything above that - so the range is
        # derived from the code, not chosen to look right. Which plasma
        # call feeds which buffer is an open question.
        self.vox_h = P.plasma_generate(0x3ff, 0x200, 1)
        self.vox_c = bytearray(v >> 3 for v in
                               P.plasma_generate(0x3fff, 0x2000, 2))
        # Static initial values, img 0x986d and img 0xa598.
        self.vox_hscale = 0x1000
        self.vox_water = 0x21
        # g_ScratchField is BSS: zero, so a level sea, until
        # Demo_Timeline builds the two-sine field into it.
        self.vox_flatsea = bytearray(0x4000)
        self.vox_ripple = False
        self.model = 0
        self.sphere = P.planet_build_map()
        # g_GlowSrc and g_GlowDst statics, img 0x98b3 and 0x98b1,
        # rebased to 0 since the sim holds the field on its own.
        self.glow = bytearray(0x10000)
        self.glow_src, self.glow_dst = 0x5900 - 0x1800, 0x1880 - 0x1800
        self.planet_approach = 0
        self.span = P.new_span_buf()
        self.light_mode = 'demo'
        self.shot_early = True
        self.battle_mode = 9
        self.reset_battle()
        self.reset_car()
        # img 0x5324 sets g_ObjTransZ for the part; the light starts
        # where Demo_Timeline img 0x4e1f left it.
        self.end = {'ang': (0, 0, 0), 'light': (0, 0xfe00, 0),
                    'z': 0x6000}
        # img 0x084b zeroes g_RotoZoom before sync 0x28 and img 0x0855
        # sets the sweep rate; mode 0xf supplies the zoom velocity.
        self.beams = {'work': bytearray(320 * 200), 'zoom': 0,
                      'vel': 0, 'angle': 0, 'rate': 0x20,
                      'pal': bytearray(768), 'ph': (0, 0, 0)}
        self.tunnel = None
        self.tunnel_exit = False
        self.reset_finale()
        self.reset_moire()
        n = P.WATER_W * (P.WATER_H + 2)
        self.wsp = {'a': [0] * n, 'b': [0] * n, 'phase': 0, 'amp': 0,
                    'drops': None}
        self.ground_ramp = 0
        self.ground_shade = None
        self.scr = None
        self.flag = None
        self.cam_yaw = False
        self.reseed()

        self.parts = [
            ('DEBUG', 'terrain A, img 0x507a, two-sine field, stretched',
             self.p_field_a),
            ('DEBUG', 'terrain B, img 0x3a7f, double frequency, SIGNED',
             self.p_field_b),
            ('SCENE', 'points, 1000:3207, 1000 pts, depth-shaded',
             self.p_points),
            ('DEBUG', 'plasma, 1000:3fae, mask 0x3ff bias 0x200 rough 1',
             self.p_plasma_a),
            ('DEBUG', 'plasma, 1000:3fae, mask 0x3fff bias 0x2000 rough 2',
             self.p_plasma_b),
            ('DEBUG', 'the palette, 1000:3b65, 96-191 is the fire ramp',
             self.p_palette),
            ('SCENE', 'plasma -> blue band, (v>>2)+0x20, img 0x5108',
             self.p_blue_band),
            ('DEBUG', 'plasma -> fire band, OURS not the demo',
             self.p_fire_band),
            ('SCENE', 'countdown, 1000:3873, 4-3-2-1 fire particles',
             self.p_countdown),
            ('SCENE', 'fire text, 1000:2370, mask filled with hot plasma',
             self.p_firetext),
            ('SCENE', 'VOXEL LANDSCAPE, 1000:3620, y-buffer span fill',
             self.p_voxel),
            ('SCENE', 'the 3D models, 1000:465c Mesh_Draw, Gouraud',
             self.p_models),
            ('SCENE', 'THE PLANET, 1000:2925, mapped sphere over stars',
             self.p_planet),
            ('SCENE', 'THE MOON, the same ball under Pal_BuildGreyLow',
             self.p_moon),
            ('SCENE', 'THE BATTLE, 1000:264b, craft + shots + glow',
             self.p_battle),
            ('SCENE', 'THE CAR, 1000:23fe over img 0x19a0 vehicle model',
             self.p_car),
            ('SCENE', 'THE END, 1000:25c7, tumbling light img 0x1c23',
             self.p_theend),
            ('SCENE', 'THE BEAMS, 1000:317a + Pal_BuildRgbRamps',
             self.p_beams),
            ('SCENE', 'THE TUNNEL, 1000:2b2d, all of it transcribed',
             self.p_tunnel),
            ('SCENE', 'THE FINALE, 1000:2358 - letters + water drop',
             self.p_finale),
            ('SCENE', 'THE MOIRE, 1000:2818 + Pal_BuildBluePurple',
             self.p_moire),
            ('SCENE', 'THE DRIPPING WATER, 1000:2a1a + mode 0x12 drops',
             self.p_water_spans),
            ('SCENE', 'GROUND, 1000:341c - a second landscape renderer',
             self.p_ground),
            ('SCENE', 'THE SCROLLER, 1000:30c1 - font and texture STAND-INS',
             self.p_scroller),
            ('SCENE', 'THE VISTA FLAG, 1000:2504 - all of it the demo',
             self.p_flag),
        ]

    def reset_battle(self):
        """The initial craft state Demo_Entry writes for sync 0x19,
        img 0x0552-0x05b7. g_94c0.. and g_9955.. for craft A, g_94cc..
        and g_995b.. for craft B."""
        self.bst = {
            'a_pos': [0, 0, 0x6000], 'a_ang': [0x1c4, 0x100, 0x7b],
            'b_pos': [0, 0, -0x14000], 'b_ang': [0, 0, 0],
            'cam': (0, 0, 0), 'c0': 0, 'c1': 0,
        }
        for k in ('a_pos', 'a_ang', 'b_pos', 'b_ang'):
            self.bst[k] = tuple(self.bst[k])
        self.glow_parts = []

    def reset_car(self):
        """Demo_Timeline img 0x5003 sets g_ShipX/Y/Z and img 0x51ba
        asks Terrain_HeightAt for the car's starting ground height -
        so the car starts ON the terrain, which the spring at
        img 0x1a1d depends on."""
        x, z = 0x8000, 0x8000
        g, _ = P.terrain_height_at(self.vox_h, self.vox_flatsea, x, z,
                                   self.vox_water)
        self.car = {
            'x': x, 'y': g, 'z': z, 'vy': 0, 'speed': 0, 'steer': 0,
            'yaw': 0, 'pitch': 0, 'roll': 0, 'pitch_target': 0,
            'roll_target': 0, 'cam_angle': 0, 'cam': (x, z),
        }

    def reset_moire(self):
        self.moire = {'field': bytearray(0x10000), 'phase': 0, 'frame': 0}

    def reset_finale(self):
        """img 0x53a0 and 0x53a7 name 0x6a79 and 0x7a79 as the water
        pair for the last part, which leaves g_WorkSeg free to hold
        the letters."""
        # TWO ROWS OF PAD. Both Water_Step and Water_Refract process
        # field rows 1..100 of a 0..99 grid - img 0x29de with 0x3e80
        # cells, and img 0x2aaa walking 100 rows without resetting SI -
        # and Water_Step's +0x140 neighbour read reaches a row beyond
        # that again. In real mode those are the next bytes in the
        # segment; here they would run off the list.
        n = P.WATER_W * (P.WATER_H + 2)
        self.fin = {'a': [0] * n, 'b': [0] * n, 'phase': 0, 'amp': 0,
                    'work': bytearray(320 * 200),
                    'ang': (0, 0, 0), 'light': (0, 0xfe00, 0),
                    'z': 0x6000}

    def reseed(self):
        # Parameters from Demo_Timeline img 0x4ef0 and img 0x4f7d.
        self.plasma_a = P.plasma_generate(0x3ff, 0x200, 1)
        self.plasma_b = P.plasma_generate(0x3fff, 0x2000, 2)
        self.cd_parts = []
        self.frame = 0

    def select(self, n):
        self.index = max(0, min(len(self.parts) - 1, n))
        self.frame = 0

    def advance(self, d):
        self.select((self.index + d) % len(self.parts))

    # ------------------------------------------------------------- parts

    def p_field_a(self):
        P.field_to_screen(self.s.fb, self.field_a)

    def p_field_b(self):
        P.field_to_screen(self.s.fb, self.field_b)

    def p_points(self):
        # The demo drives the camera from its parameter block; this
        # sweeps z so the perspective and the depth colour show. The
        # sweep is OURS - the real camera path is unread.
        P.points_render(self.s.fb, self.points,
                        (0, 0, (self.frame * 0x200) & 0xffff))

    def p_plasma_a(self):
        P.plasma_to_screen(self.s.fb, self.plasma_a)

    def p_plasma_b(self):
        P.plasma_to_screen(self.s.fb, self.plasma_b)

    def p_palette(self):
        """OURS. 256 vertical bars so the bands can be counted."""
        for x in range(P.SCREEN_W):
            idx = x * 256 // P.SCREEN_W
            for y in range(40, 160):
                self.s.fb[y * P.SCREEN_W + x] = idx

    def p_blue_band(self):
        P.plasma_to_screen(self.s.fb, self.plasma_a)
        fb = self.s.fb
        for n in range(len(fb)):
            fb[n] = (fb[n] >> 2) + 0x20

    def p_fire_band(self):
        # OURS. The demo reaches the fire band by clamping or by a
        # threshold, never by this mapping - kept only to show the ramp.
        P.plasma_to_screen(self.s.fb, self.plasma_a)
        fb = self.s.fb
        for n in range(len(fb)):
            fb[n] = 0x60 + (fb[n] * 0x60 >> 8)

    def p_countdown(self):
        # g_BurstTicks is 0x60 and Timer_IsrFrameUpdate decrements it
        # once per tick, so a digit lasts 96 frames.
        if self.frame % 96 == 0:
            self.cd_window = (self.frame // 96) % 4
            self.cd_parts = P.countdown_spawn(self.blob, self.cd_window)
        self.s.fb[:] = self.cd_bg
        # img 0x3941: the mode 1 handler moves g_WarpOfsX by -1 and
        # g_WarpOfsY by +0x101 a tick, so the dots wave as they fly.
        P.countdown_step(self.s.fb, self.cd_parts, self.cd_bg,
                         field=self.plasma_a,
                         ofs_x=(-self.frame) & 0xffff,
                         ofs_y=(self.frame * 0x101) & 0xffff)
        self.status += '  glyph %d, %d alive' % (
            4 - self.cd_window, sum(1 for p in self.cd_parts if p[0]))

    def p_firetext(self):
        # g_987f is the threshold and the demo animates it; sweeping it
        # here is OURS, and so is the mask - see
        # countdown_mask_as_bitmap.
        thr = 0x20 + (self.frame * 2) % 0xa0
        P.firetext_fill(self.s.fb, self.text_mask, self.plasma_a,
                        (-self.frame * 0x40) & 0xffff, thr)
        self.status += '  threshold 0x%02x' % thr

    def p_voxel(self):
        # g_CamScale 0x400 is img 0x0210; the 0 y-offset is img 0x01e5
        # (img 0x02ae sets 0x2300 instead, which drops the horizon 28
        # rows). The camera PATH is ours - the demo's comes from the
        # script - and so are the two parameters on the keys.
        # g_CamScale 0x0b00, the camera at -0xa000 and the angle 0x100
        # are the static initial values. The PATH is ours: it flies
        # along the heading, where the demo integrates it in Timer_Isr.
        f = self.frame
        angle = (0x100 + f) & 0x7fc
        dist = f * 0x180
        cx = -0xa000 + (P.cos_at(angle) * dist >> 12)
        cy = -0xa000 + (P.sin_at(angle) * dist >> 12)
        cam = (cx, cy, angle, 0xb00, self.vox_hscale, 0,
               self.vox_water, (f * 0x20) & 0x3fff)
        P.voxel_render(self.s.fb, self.vox_h, self.vox_c,
                       self.vox_ripple and self.field_a or self.vox_flatsea,
                       cam)
        self.status += ('  hscale 0x%x water 0x%02x sea %s'
                        % (self.vox_hscale, self.vox_water,
                           'ripple' if self.vox_ripple else 'flat'))

    def p_models(self):
        # FULLY TRANSCRIBED - Mesh_Draw, Mesh_TransformVerts,
        # Mesh_FaceNormals, Vec_Rotate3 and Poly_Fill. The cull, the
        # depth key, the Gouraud shading, the sort and each face's
        # palette band are all the demo's. Only the rotation RATE below
        # is ours, standing in for the animation handler. M cycles the
        # models.
        base = P.MODEL_BASES[self.model]
        verts, faces = P.parse_model(self.blob, base)
        f = self.frame
        n = P.mesh_draw(self.s.fb, verts, faces,
                        (f * 4, f * 7, f * 3), (0, 0, 0x6000),
                        cam_yaw=self.cam_yaw and f * 2 or 0,
                        buf=self.span, light_mode=self.light_mode)
        self.status += ('  img 0x%04x %dv %df, %d front, light %s%s'
                        % (base, len(verts), len(faces), n,
                           self.light_mode,
                           ' [OURS]' if self.light_mode == 'view' else ''))

    def _planet(self, pan, grey):
        """Planet_Render's three stages in order: Points_Render, the
        ball, then Mesh_Draw. The approach value and the pan are the
        demo's own - 0 and 0x8000 at sync 0x0c, 0 and 0xc0 at 0x34."""
        pal = P.pal_build_main()
        if grey:
            P.pal_build_grey_low(pal)
        self.s.set_palette(pal)
        P.points_render(self.s.fb, self.points, (0, 0, 0))
        # img 0x101e: the mode 4 handler does inc g_PlanetTexPan every
        # tick, so the surface turns. The demo's own, not ours.
        P.planet_render(self.s.fb, self.sphere, self.plasma_a,
                        pan=(pan + self.frame) & 0xffff,
                        approach=self.planet_approach)
        base = P.MODEL_BASES[self.model]
        verts, faces = P.parse_model(self.blob, base)
        f = self.frame
        P.mesh_draw(self.s.fb, verts, faces, (f * 4, f * 7, f * 3),
                    (0, 0, 0x6000), buf=self.span,
                    light_mode=self.light_mode)
        cols = max(0, min(128, (0x38000 - self.planet_approach) >> 10))
        self.status += ('  approach 0x%x -> %d of 128 columns, pan 0x%04x'
                        % (self.planet_approach, cols, pan))

    def p_battle(self):
        """Battle_Run, 1000:264b. The flight model is the demo's now -
        the mode 9, 0xa and 0xb handlers - so the only thing of ours
        left here is the glow particles, which are labelled. B cycles
        the mode and with it the draw order."""
        self.s.set_palette(P.pal_build_main())
        st = self.bst
        {9: P.battle_mode9, 0xa: P.battle_mode_a,
         0xb: P.battle_mode_b}[self.battle_mode](st)
        P.points_render(self.s.fb, self.points, (0, 0, 0))

        def craft(base, pos, ang):
            v, f = P.parse_model(self.blob, base)
            P.mesh_draw(self.s.fb, v, f, ang, pos, buf=self.span,
                        light_mode=self.light_mode)

        def glow():
            # img 0x2669: the glow only draws once g_MusicPos reaches
            # 0x1a, and img 0x2682 moves its box in mode 0xb.
            P.glow_spawn_step(self.glow, self.glow_src, self.glow_parts)
            P.glow_render(self.s.fb, self.glow, self.glow_src,
                          self.glow_dst, 0x9c if self.battle_mode == 0xb
                          else 0x2d60)
            self.glow_src, self.glow_dst = self.glow_dst, self.glow_src

        def shots():
            base = 0x5b81 if self.shot_early else 0x5a8a
            v, f = P.parse_model(self.blob, base)
            for n in range(8):
                z = st['b_pos'][2] + n * 0x4000
                P.mesh_draw(self.s.fb, v, f,
                            st['b_ang'], (st['b_pos'][0], st['b_pos'][1], z),
                            buf=self.span, light_mode=self.light_mode)

        a = (0x585f, st['a_pos'], st['a_ang'])
        b = (0x572f, st['b_pos'], st['b_ang'])
        # img 0x265e: mode 0xb puts B farther, so B goes down first.
        order = (b, a) if self.battle_mode == 0xb else (a, b)
        craft(*order[0])
        glow()
        shots()
        craft(*order[1])
        self.status += ('  mode 0x%x, shot 0x%04x, A.z %d  [glow particles '
                        'OURS]' % (self.battle_mode,
                                   0x5b81 if self.shot_early else 0x5a8a,
                                   st['a_pos'][2]))

    def p_car(self):
        """Car_Run, 1000:23fe: Voxel_Render, then the parked craft, the
        flag and the car - each skipped when its camera-space z passes
        0x28000. The vehicle model is img 0x19a0, all of it the
        demo's."""
        c = self.car
        P.car_tick(c, self.vox_h, self.vox_flatsea, self.vox_water)
        cam = (c['cam'][0], c['cam'][1], c['cam_angle'] & 0x7fc, 0xb00,
               self.vox_hscale, 0, self.vox_water, 0)
        P.voxel_render(self.s.fb, self.vox_h, self.vox_c,
                       self.vox_flatsea, cam)
        # img 0x2401: the craft is parked, and img 0x2469 puts the flag
        # at a fixed offset from it. Both relative to the car here, so
        # they stay in frame - THAT PLACEMENT IS OURS.
        for base, dx, dy, dz, ang in (
                (0x585f, 0x6000, -0x8000, 0x8000, (0, 0x200, 0)),
                (0x5be7, 0x6000 - 0x5200, 0, 0x8000 - 0xb800, (0, 0x200, 0)),
                (0x94f4, 0, 0, 0, (c['pitch'], c['yaw'], c['roll']))):
            v, f = P.parse_model(self.blob, base)
            P.mesh_draw(self.s.fb, v, f, ang, (dx, dy + 0x2000, 0x18000 + dz),
                        buf=self.span, light_mode=self.light_mode)
        g, _ = P.terrain_height_at(self.vox_h, self.vox_flatsea, c['x'],
                                   c['z'], self.vox_water)
        self.status += ('  speed %d  y %d ground %d  pitch %d roll %d'
                        '  [object placement OURS]'
                        % (c['speed'], c['y'], g, c['pitch'], c['roll']))

    def p_theend(self):
        """TheEnd_Run, 1000:25c7: clear, Mesh_Draw, present. Model
        0x5c47, 120 verts and 94 faces - the largest in the image. The
        whole motion is the mode 0x1a handler's, light included. The
        near clamp below is OURS, so the letters do not pass through
        the camera while the part is looked at."""
        self.s.set_palette(P.pal_build_main())
        P.theend_tick(self.end)
        e = self.end
        v, f = P.parse_model(self.blob, 0x5c47)
        n = P.mesh_draw(self.s.fb, v, f, e['ang'], (0, 0, max(0x2400, e['z'])),
                        buf=self.span, light=e['light'],
                        light_mode=self.light_mode)
        self.status += ('  z %d  obj %s  light %s  %d front  [near clamp OURS]'
                        % (e['z'], tuple(a & 0x7fc for a in e['ang']),
                           tuple(a & 0x7fc for a in e['light']), n or 0))

    def p_beams(self):
        """Beams_Render, 1000:317a, with mode 0xf driving it: the zoom
        bounces between 0x40 and 0x100 by flipping its velocity, and
        the angle advances by g_RotoAngleRate - +0x20 at sync 0x28,
        -0x20 at sync 0x29. V reverses the sweep.

        The palette is the demo's own - mode 0xf calls
        Pal_BuildRgbRamps every tick, so the beams drift through
        colour as three cosine-slope ramps.
        """
        b = self.beams
        b['ph'] = P.pal_build_rgb_ramps(b['pal'], *b['ph'])
        self.s.set_palette(b['pal'])
        b['angle'] = (b['angle'] + b['rate']) & 0xffff     # img 15ce
        b['zoom'] += b['vel']                              # img 15d8
        if b['zoom'] < 0x40:                               # img 15de
            b['vel'] = 4
        elif b['zoom'] > 0x100:                            # img 15f1
            b['vel'] = -4
        P.beams_render(self.s.fb, b['work'], b['zoom'], b['angle'])
        bp = ((P.sin_at(b['angle']) * b['zoom']) >> 8) & 0xffff
        step = max(1, min(bp, 0x10000 - bp))
        self.status += ('  zoom 0x%x vel %+d angle 0x%03x  period %.0f px'
                        '  rgb slope %d/%d/%d'
                        % (b['zoom'], b['vel'], b['angle'] & 0x7fc,
                           65536.0 / step,
                           (P.cos_at(b['ph'][0]) + 0x1000) >> 5,
                           (P.cos_at(b['ph'][1]) + 0x1000) >> 5,
                           (P.cos_at(b['ph'][2]) + 0x1000) >> 5))

    def p_tunnel(self):
        """Tunnel_Render, 1000:2b2d. ALL OF IT IS THE DEMO'S NOW -
        the depth table from Tunnel_BuildDepth, the angle table drawn
        by Tunnel_BuildAngle through Poly_FillFlat, and the sampling.
        Only the scroll RATES are ours, standing in for Tunnel_Run's
        easing targets. X toggles the exit variant, which blacks out
        pixels where g_TunnelFade falls below the /8 mask.
        """
        if self.tunnel is None:
            d, m = P.tunnel_build_depth()
            self.tunnel = {'depth': d, 'mask': m,
                           'angle_tab': P.tunnel_build_angle(),
                           'ang': 0, 'dep': 0, 'parity': 0, 'fade': 0xff}
        t = self.tunnel
        # img 0x08fe then 0x0901: Pal_BuildMain, then
        # Pal_BuildBlueMagenta over its first 192 entries.
        self.s.set_palette(P.pal_build_blue_magenta(P.pal_build_main()))
        # Tunnel_Run eases both by >> 3; the rates here are ours.
        t['ang'] = (t['ang'] + 0x60) & 0xffff
        t['dep'] = (t['dep'] + 0x300) & 0xffffff
        if self.tunnel_exit:
            t['fade'] = max(0, t['fade'] - 1)
            t['parity'] = P.tunnel_render(
                self.s.fb, t['angle_tab'], t['mask'], self.plasma_a,
                t['ang'], t['dep'], t['parity'], fade=t['fade'])
        else:
            t['fade'] = 0xff
            t['parity'] = P.tunnel_render(
                self.s.fb, t['angle_tab'], t['depth'], self.plasma_a,
                t['ang'], t['dep'], t['parity'])
        self.status += ('  angle 0x%03x depth hi 0x%02x parity %d%s'
                        '  [scroll rates OURS]'
                        % ((t['ang'] >> 3) & 0xff, (t['dep'] >> 8) & 0xff,
                           t['parity'] & 1,
                           '  EXIT fade %d' % t['fade']
                           if self.tunnel_exit else ''))

    def p_finale(self):
        """TheEnd_RunWater, 1000:2358, called at img 0x53c1: Mesh_Draw
        the letters into the work buffer, Water_Step the wave, then
        Water_Refract the work buffer onto the screen through it.

        The drive is mode 0x1c's: one word at the centre cell with a
        ramping amplitude, so the drop is invisible for the first
        second and then grows. Z resets it.
        """
        f = self.fin
        self.s.set_palette(P.pal_build_main())
        # Mesh_Draw into the work buffer - the letters, mode 0x1a's
        # motion, which runs through the finale too.
        for i in range(len(f['work'])):
            f['work'][i] = 0
        lp, ly, lr = f['light']
        f['light'] = (P.to_s16(lp + 4), P.to_s16(ly + 8), P.to_s16(lr + 0xc))
        p, y, r = f['ang']
        f['ang'] = (P.to_s16(p + 8), P.to_s16(y + 6), P.to_s16(r + 4))
        v, fc = P.parse_model(self.blob, 0x5c47)
        P.mesh_draw(f['work'], v, fc, f['ang'], (0, 0, 0x6000),
                    buf=self.span, light=f['light'],
                    light_mode=self.light_mode)
        # img 0x1c8f: the centre cell, word 8080 of a 160x100 grid.
        f['phase'] = (f['phase'] + 0x20) & 0xffff
        f['amp'] += 1
        f['b'][8080] = P.to_s16(
            ((P.sin_at(f['phase']) * f['amp']) >> 0xb) & 0xffff)
        P.water_step(f['a'], f['b'])
        f['a'], f['b'] = f['b'], f['a']
        P.water_refract(self.s.fb, f['work'], f['b'])
        live = sum(1 for q in f['b'] if q)
        self.status += ('  amp %d  drive %d  %d live cells of %d'
                        % (f['amp'], f['b'][8080], live,
                           P.WATER_W * P.WATER_H))

    def p_moire(self):
        """Moire_Render, 1000:2818. NOTHING HERE PLOTS A LINE: each
        pixel accumulates a smooth increment into an 8-bit field, and
        the lines are where that sum wraps past 256.

        The -col term makes neighbouring columns diverge by 1 a frame,
        so the band period is 256/n and the pattern tightens; the
        advancing phases then turn it round again. J resets it.
        """
        # img 0x042d: Pal_BuildBluePurple, and Pal_BuildMain does not
        # run again until img 0x045d - AFTER Moire_Render's call site
        # at img 0x0457. So this part is NOT under the fire palette.
        self.s.set_palette(P.pal_build_blue_purple())
        m = self.moire
        m['phase'] = P.interfere_render(self.s.fb, m['field'], m['phase'])
        m['frame'] += 1
        row = 100 * 320
        w = sum(1 for x in range(1, 320)
                if self.s.fb[row + x] > self.s.fb[row + x - 1] + 64)
        self.status += ('  frame %d  phase 0x%03x  %d bands = %s'
                        % (m['frame'], m['phase'] & 0x7fc, w,
                           '%.1f px' % (320.0 / w) if w else 'none'))

    def p_water_spans(self):
        """Water_RenderSpans, 1000:2a1a - the sync 0x2d scene. The wave
        field as column spans, a y-buffer like Voxel_Render's.

        THE PALETTE AND THE DRIPS ARE BOTH THE DEMO'S NOW.
        Pal_SetRgbRamps at 1000:3e31 gives the cold black-blue-cyan-
        white ramp, and the mode 0x12 handler at img 0x166f is the
        drip source: eight slots at rows 35, 43, 51, 59, 67, 75, 83
        and 91, each poking -1024 - a DENT, not a bump.

        The slot POSITIONS and the lifetime refresh are ours; the demo
        fills them from cs:0x93c8 through helpers at img 0x21bb,
        0x21e7 and 0x22cf that have not been traced to a caller.
        """
        self.s.set_palette(P.pal_set_rgb_ramps())
        w = self.wsp
        if w['drops'] is None or self.frame % 90 == 0:
            w['drops'] = [[0x10, (n * 37 % 150 - 75) * 0x200]
                          for n in range(8)]
        P.water_drop_step(w['b'], w['drops'])           # img 0x166f
        P.water_step(w['a'], w['b'])
        w['a'], w['b'] = w['b'], w['a']
        P.water_render_spans(self.s.fb, w['b'])
        alive = sum(1 for d in w['drops'] if d[0])
        self.status += ('  %d of 8 drops alive  [positions OURS]' % alive)

    def p_ground(self):
        """Ground_Render, 1000:341c, sync 0x0a. Voxel_Render's ray
        setup with the loop order inverted - distance outside, 160
        samples in - and a DIAGONAL screen walk. O and P step
        g_GroundRamp so the horizon can be moved by hand; the demo
        bumps it every six ticks from mode 3.
        """
        self.s.set_palette(P.pal_build_main())
        f = self.frame
        if self.ground_shade is None:
            # img 0x023e: Terrain_BuildShade fills 0x4a79 from the
            # plasma at 0x2a79 just before this part. Both fields are
            # the demo's now; only the CAMERA below is ours.
            self.ground_shade = P.terrain_build_shade(self.plasma_a)
        P.ground_render(self.s.fb, self.ground_shade, self.plasma_a,
                        -0xa000 + f * 0x40, -0xa000, (0x100 + f) & 0x7fc,
                        0x0b00, self.ground_ramp)
        dist = max(0x50, (0x80 - self.ground_ramp) & 0xffff)
        self.status += ('  ramp 0x%02x -> dist %d, horizon row %d'
                        '  shade 1..30  [camera OURS]'
                        % (self.ground_ramp, dist, 200 - dist))

    def p_scroller(self):
        """Scroller_Render, 1000:30c1, over Scroller_BuildRows'
        perspective table, driven by the mode 0x19 handler's rock.

        THE FONT AND THE SOURCE TEXTURE ARE BOTH STAND-INS and the
        title says so. img 0x301f fetches the font from the VIDEO BIOS
        with INT 10h AX=1130 BH=3, so the image contains none; and
        what is in segment 0x6a79 when this part runs is
        effects/scroller.txt open question 1.

        Everything else is the demo's: the row table, the stencil, the
        32-characters-per-line stepping, the glyph cache, and the text
        itself out of g_ScrollerText at img 0xa435.
        """
        if self.scr is None:
            src = bytearray(320 * 200)
            for i in range(len(src)):                   # a texture, OURS
                src[i] = 0x60 + (((i // 320) + (i % 320)) >> 2 & 0x1f)
            self.scr = {'rows': P.scroller_build_rows(),
                        'font': P.font_standin(), 'src': src,
                        'base': 0, 'phase': 0, 'pos': 0,
                        'cam_y': 0, 'cam_z': 0}
        t = self.scr
        P.scroller_tick(t)                              # img 0x1bca
        P.points_render(self.s.fb, self.points, (0, 0, 0))
        P.scroller_render(self.s.fb, t['src'], self.blob, t['font'],
                          t['rows'], t['pos'])
        lit = sum(1 for q in self.s.fb if q)
        self.status += ('  pos %d  %d lit  [FONT + TEXTURE STAND-INS]'
                        % (t['pos'], lit))

    def p_flag(self):
        """Flag_Render, 1000:2504 - the waving VISTA flag, sync 0x37.

        The whole pipeline is the demo's: img 0x5105 generates a fresh
        plasma, img 0x5108 maps it to (v >> 2) + 0x20 which is the BLUE
        band, img 0x5116 points g_BannerTextRec at the "VISTA   !"
        record, img 0x5123 blends it in with Text_BuildBanner, and the
        mode 0x17 handler scrolls g_WarpOfsX by +1 and g_WarpOfsY by
        +0x102 a tick - which is the waving.

        The sine field is the two-sine field at 0x8a79:0x4564, built
        at img 0x507a in the part just before - the sim's field_a.

        The out-of-range fallback reads the work buffer, so whatever
        is in it shows around the panel. Here that is the voxel
        landscape, which is what the part has behind it.
        """
        if self.flag is None:
            src = P.plasma_generate(0x3ff, 0x200, 1)    # img 5105
            for i in range(len(src)):                   # img 5108
                src[i] = ((src[i] >> 2) + 0x20) & 0xff
            P.text_build_banner(src, self.blob, 'VISTA', 0x3000)
            work = bytearray(320 * 200)
            cam = (0, 0, 0x100, 0xb00, self.vox_hscale, 0,
                   self.vox_water, 0)
            P.voxel_render(work, self.vox_h, self.vox_c,
                           self.vox_flatsea, cam)
            self.flag = {'src': src, 'work': work, 'ofs_x': 0, 'ofs_y': 0}
        f = self.flag
        f['ofs_x'] = (f['ofs_x'] + 1) & 0xffff          # img 1975
        f['ofs_y'] = (f['ofs_y'] + 0x102) & 0xffff      # img 197a
        P.flag_render(self.s.fb, f['src'], f['work'], self.field_a,
                      f['ofs_x'], f['ofs_y'])
        self.status += ('  warp x 0x%04x y 0x%04x  panel 186x152 at 70,24'
                        % (f['ofs_x'], f['ofs_y']))

    def p_planet(self):
        # sync 0x0c: g_PlanetApproach is statically 0 and g_PlanetTexPan
        # statically 0x8000, so the ball lands centred under
        # Pal_BuildMain's land and water bands.
        self._planet(0x8000, False)

    def p_moon(self):
        # sync 0x34: Demo_Timeline sets approach 0 and pan 0xc0 at
        # img 0x4e0e, then Pal_BuildGreyLow before the runner.
        self._planet(0x00c0, True)

    # -------------------------------------------------------------- loop

    def step(self):
        kind, label, fn = self.parts[self.index]
        self.s.clear()
        # EVERY PART STARTS FROM Pal_BuildMain. Without this a part
        # that installs a cold or grey palette leaks it into the next
        # one - which is what made the plasma parts lose their reds.
        self.s.set_palette(self.s.base_pal)
        self.status = '%d/%d %s  %s' % (self.index + 1, len(self.parts),
                                        kind, label)
        fn()
        self.s.blit()
        self.frame += 1

    def key(self, k):
        """The keys that belong to THIS demo. The harness handles
        Escape, SPACE, the digits, the brackets and R, and sends
        everything else here.

        THESE USED TO LIVE IN main() AND ELEVEN OF THEM WERE BROKEN -
        O, P, J, Z, X, V, N, B, W, A and S all said `self.` where no
        self existed, so each raised NameError when pressed and
        Tkinter swallowed it. As a method `self` is correct by
        construction, which is the structural reason this moved.
        """
        if k in ('g', 'G'):
            self.grey = not self.grey
            self.s.set_palette(
                P.pal_build_grey_low(bytearray(self.s.base_pal))
                if self.grey else self.s.base_pal)
        elif k in ('f', 'F'):
            self.fade = -0x4000 if self.fade >= 0x4000 else self.fade + 0x1000
            self.s.set_palette(P.pal_fade(self.s.base_pal, self.fade))
        elif k == 'comma':
            self.vox_hscale = max(0x40, self.vox_hscale // 2)
        elif k == 'period':
            self.vox_hscale = min(0x1000, self.vox_hscale * 2)
        elif k == 'backslash':
            self.vox_water = 0 if self.vox_water >= 0x30 else                 self.vox_water + 8
        elif k in ('k', 'K'):
            self.vox_ripple = not self.vox_ripple
        elif k in ('m', 'M'):
            self.model = (self.model + 1) % len(P.MODEL_BASES)
        elif k in ('l', 'L'):
            self.light_mode = ('view' if self.light_mode == 'demo'
                               else 'demo')
        elif k in ('o', 'O'):
            self.ground_ramp = max(0, self.ground_ramp - 4)
        elif k in ('p', 'P'):
            self.ground_ramp = min(0x30, self.ground_ramp + 4)
        elif k in ('j', 'J'):
            self.reset_moire()
        elif k in ('z', 'Z'):
            self.reset_finale()
        elif k in ('x', 'X'):
            self.tunnel_exit = not self.tunnel_exit
        elif k in ('v', 'V'):
            self.beams['rate'] = (-self.beams['rate']) & 0xffff
        elif k in ('n', 'N'):
            self.reset_car()
        elif k in ('b', 'B'):
            self.battle_mode = {9: 0xa, 0xa: 0xb, 0xb: 9}[self.battle_mode]
            self.reset_battle()
        elif k in ('w', 'W'):
            self.shot_early = not self.shot_early
        elif k in ('a', 'A'):
            self.planet_approach = max(0, self.planet_approach - 0x2000)
        elif k in ('s', 'S'):
            self.planet_approach = min(0xfa000,
                                       self.planet_approach + 0x2000)
        elif k in ('c', 'C'):
            self.cam_yaw = not self.cam_yaw
