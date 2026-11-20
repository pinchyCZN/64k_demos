"""LITHIUM transcriptions.

Every routine's docstring opens with ORIGIN and an address in the
UNPACKED image, which is Ghidra's LITHIUM_U.EXE. CLAUDE.md rule 15:
this is a transcription, not an impression. Where a value is not
known, refuse rather than invent one.

THE TEXTURE AND THE PALETTES ARE THE DEMO'S OWN. The environment map
is generated here exactly as Tex_BuildSphereMap generates it, and the
palettes are either read out of the image or animated by Pal_Animate.
So the chrome in the viewer is Lithium's pixels, not a stand-in.

WHAT IS STILL NOT KNOWN, and so is not here:

    palette 129-255  Pal_Animate fills only entries 0 to 128 of
                     g_PalMain. Nothing found yet writes the rest, so
                     those entries stay black.
    the music        the module's samples are not in the image.
                     docs/lithium/README.txt section 6.
"""

import os
import struct

SCREEN_W = 320
SCREEN_H = 200

# Segment 0x1e5f is the asset segment, so its image offset is
# 0x1e5f * 16 - 0x10000, the load base Ghidra uses. An offset stored in
# g_MeshSrcOff is relative to this.
ASSET_BASE = 0xe5f0

# img 0x1a535 - 512 signed words, a quarter sine rising 0 to 4096, so
# 4096 is 1.0 and the fixed point is 12-bit [verified]. Reached as
# ds:esi = 0x1e5f:0xbf45 at unpacked 1000:011f.
SIN_SRC = 0x1a535

# The three palettes Demo_Main selects by writing into g_PalSource.
# PAL_MAIN is zero in the image because Pal_Animate rebuilds it every
# frame; the other two are stored, and both are four 64-entry rising
# ramps - docs/lithium/subsystems/texture.txt.
PAL_MAIN = 0x96be
PAL_1 = 0x99be
PAL_2 = 0x9cbe

# img 0x9f31 - Tex_Plot's 16x16 brush, a disc of 1s.
BRUSH = 0x9f31

# g_MeshSrcOff values Demo_Main stores before each Mesh_Load call, in
# the order it stores them. The labels are [guess] from the vertex and
# face counts and from docs/lithium/effects/scenes.txt; nothing has
# been seen drawn.
# TEN MODELS, contiguous from img 0x125f0 to 0x15aed with no gaps.
# 0x4974 through 0x4afa are FOUR BALL SIZES, used at the same time by
# the figure's fifteen joints - same 16 faces over the same 10
# vertices at different radii. docs/lithium/effects/figure.txt.
MESHES = [
    (0x724a, 'bolt       4 verts,    4 faces  - battle'),
    (0x4974, 'ball 0    10 verts,   16 faces  - limbs'),
    (0x49f6, 'ball 1    10 verts,   16 faces  - hands, feet'),
    (0x4a78, 'ball 2    10 verts,   16 faces  - hips'),
    (0x4afa, 'ball 3    10 verts,   16 faces  - root'),
    (0x7272, 'small     11 verts,   16 faces  - tower falling'),
    (0x4000, 'ship A    34 verts,   64 faces  - battle, flyover'),
    (0x72f8, 'ship B    35 verts,   68 faces  - battle, tower'),
    (0x41ea, 'rock     130 verts,  256 faces  - rock scenes'),
    (0x4b7c, 'tower    664 verts, 1323 faces  - tower falling'),
]


def _repo_root():
    d = os.path.dirname(os.path.abspath(__file__))
    for _ in range(6):
        if os.path.exists(os.path.join(d, 'CLAUDE.md')):
            return d
        d = os.path.dirname(d)
    raise RuntimeError('cannot find the repo root above %s' % __file__)


_IMG = [None]


def image():
    """The depacked image. tools/unpack/xcur_unpack.py regenerates it."""
    if _IMG[0] is None:
        p = os.path.join(_repo_root(), 'reverse', 'unpacked', 'lithium.bin')
        with open(p, 'rb') as f:
            _IMG[0] = f.read()
    return _IMG[0]


def _idiv(a, b):
    """IDIV truncates toward zero; Python's // floors. The difference
    shows up on every negative normal component, so it is not
    cosmetic."""
    q = abs(a) // abs(b)
    return -q if (a < 0) != (b < 0) else q


def _s16(v):
    v &= 0xffff
    return v - 0x10000 if v & 0x8000 else v


def _s8(v):
    return v - 0x100 if v & 0x80 else v


# ------------------------------------------------------------- palette

def pal_stored(at):
    """One of the palettes stored in the image, as it is stored.

    g_Pal1 at PAL_1 and g_Pal2 at PAL_2 are both FOUR 64-ENTRY RISING
    RAMPS, which is the shape Poly_Fill needs: a texel is 0..0x7f and
    adding 0x40 or 0xc0 moves it into another band.
    """
    return bytearray(image()[at:at + 768])


def pal_build_main():
    """The harness asks for an initial palette; g_Pal2 is a real one.

    Demo_Main selects it at 1000:0353. Its first two bands are blue
    and cyan dominant, which is the surface colour the demo shows -
    docs/lithium/effects/scenes.txt scene 9.
    """
    return pal_stored(PAL_2)


def pal_to_rgb(pal):
    """768 DAC bytes, 0 to 0x3f, to 256 RGB triples."""
    out = []
    for i in range(256):
        r, g, b = pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]
        out.append((min(255, r * 255 // 63),
                    min(255, g * 255 // 63),
                    min(255, b * 255 // 63)))
    return out


def pal_upload(src, fade):
    """ORIGIN unpacked 1000:3dcc - Pal_Upload.

    768 DAC writes from g_PalSource, every byte biased by
    g_PalFade >> 2 and clamped into 0..0x3f. So g_PalFade is the fade
    in DAC units times four: -128 gives black, +256 gives white, 0
    gives the palette as stored.

    The original's clamp mixes an unsigned JBE with a signed JG, which
    is how one compare handles both the wrapped negatives and the
    overflow. Written out here rather than reduced to min/max.
    """
    bias = fade >> 2                    # SAR, so it stays negative
    out = bytearray(768)
    for i in range(768):
        v = src[i] + bias
        if v <= 0x3f:                   # JBE, unsigned
            if v < 0:                   # the signed JG fell through
                v = 0
        else:
            v = 0x3f
        out[i] = v
    return out


# --------------------------------------------------------------- tables

def sin_table_build():
    """ORIGIN unpacked 1000:012b - the first thing Demo_Main builds.

    ONE CONTIGUOUS TABLE OF 2560 DWORD ENTRIES, not three tables. The
    two build loops and the three destinations are a way of filling it
    in four quadrants at once:

        entries 0    .. 511    src[0..511]       0 to 90 degrees
        entries 512  .. 1023   src[511..0]       90 to 180
        entries 1024 .. 1535   -src[0..511]      180 to 270
        entries 1536 .. 2047   -src[511..0]      270 to 360
        entries 2048 .. 2559   src[0..511]       THE WRAP TAIL

    So 2048 entries are a full period, which is why Vec_RotZ masks the
    angle with 0x1ffc - a dword index, 2048 units to the circle.

    THE WRAP TAIL IS WHY THE FIRST LOOP WRITES +0x2000 AND THE SECOND
    DOES NOT. Cosine is read as sine 512 entries further on, so an
    angle near the top of the range indexes up to 2047 + 512 = 2559,
    and those last 512 entries exist for no other reason.
    """
    img = image()
    src = list(struct.unpack_from('<512h', img, SIN_SRC))
    rev = list(reversed(src))
    return (src + rev + [-v for v in src] + [-v for v in rev] + src)


def vol_table_build():
    """ORIGIN unpacked 1000:6eed - the mixer's volume table.

        for vol in 0 .. 0x40:
            for s in 0 .. 0xff:
                out = (((s - 0x80) * vol) >> 6) + 0x80

    0x41 volumes by 0x100 sample values, 16,640 bytes. The sample byte
    is unsigned with 0x80 as silence, so it is centred, scaled by the
    volume over 64, and re-centred. This is 8-bit software mixing and
    it is NOT sample synthesis - see docs/lithium/README.txt section 6.
    """
    tbl = []
    for vol in range(0x41):
        row = bytearray(0x100)
        for s in range(0x100):
            v = s - 0x80
            v = (v * vol) >> 6          # SAR, arithmetic
            row[s] = (v + 0x80) & 0xff
        tbl.append(row)
    return tbl


# ----------------------------------------------------------------- mesh

def mesh_load(src_off):
    """ORIGIN unpacked 1000:503a - Mesh_Load.

    Expands a byte-packed mesh and builds AVERAGED PER-VERTEX NORMALS,
    which is Gouraud setup. docs/lithium/formats/mesh.txt is the format.

    Returns (verts, faces, normals) where verts are the signed byte
    triples as stored, faces are index triples, and normals are the
    per-vertex result the routine actually leaves behind.

    THE FACE TABLE IS SCRATCH. The original writes face records at
    g_MeshNrmOut with a stride of 0xa, then restarts the same pointer
    and overwrites them with the per-vertex normals at a stride of 6.
    Only the normals survive, so only they are returned.
    """
    img = image()
    base = ASSET_BASE + src_off
    nv, nf = struct.unpack_from('<2H', img, base)

    si = base + 4
    vx, vy, vz = [], [], []
    ax, ay, az, cnt = [], [], [], []
    for _ in range(nv):
        vx.append(_s8(img[si]))
        vy.append(_s8(img[si + 1]))
        vz.append(_s8(img[si + 2]))
        si += 3
        ax.append(0)
        ay.append(0)
        az.append(0)
        cnt.append(0)

    faces = []
    for _ in range(nf):
        i0, i1, i2 = struct.unpack_from('<3H', img, si)
        si += 6
        faces.append((i0, i1, i2))

        # The centroid of the three vertices. IDIV, so truncating.
        cx = _idiv(vx[i0] + vx[i1] + vx[i2], 3)
        cy = _idiv(vy[i0] + vy[i1] + vy[i2], 3)
        cz = _idiv(vz[i0] + vz[i1] + vz[i2], 3)

        # Cross product of (v0 - centroid) x (v1 - centroid). ONLY v0
        # AND v1 TAKE PART; v2 reaches this only through the centroid.
        ux, uy, uz = vx[i0] - cx, vy[i0] - cy, vz[i0] - cz
        wx, wy, wz = vx[i1] - cx, vy[i1] - cy, vz[i1] - cz
        nx = _s16(uy * wz - uz * wy)    # stored as a WORD, so it wraps
        ny = _s16(uz * wx - ux * wz)
        nz = _s16(ux * wy - uy * wx)

        # The length, by BRUTE-FORCE SEARCH rather than a square root:
        # walk ECX up from 0 keeping the closest ECX^2 to the squared
        # length, and stop as soon as it gets worse.
        len2 = nx * nx + ny * ny + nz * nz
        best, c = 0xffffffff, 0
        while c < 0x4000:
            d = abs(c * c - len2)
            if d > best:
                break
            best = d
            c += 1
        c += 1                          # the trailing INC ECX

        # c is therefore the nearest integer root PLUS one or two, and
        # that is the point: it can never be zero, so a degenerate face
        # with a zero-length normal does not divide by zero.
        bx = _idiv(nx << 11, c)         # 11-bit fixed point
        by = _idiv(ny << 11, c)
        bz = _idiv(nz << 11, c)

        for i in (i0, i1, i2):
            ax[i] = (ax[i] + bx) & 0xffff   # 16-bit adds, so they wrap
            ay[i] = (ay[i] + by) & 0xffff
            az[i] = (az[i] + bz) & 0xffff
            cnt[i] = (cnt[i] + 1) & 0xffff

    normals = []
    for i in range(nv):
        if cnt[i]:
            normals.append((_idiv(_s16(ax[i]), cnt[i]),
                            _idiv(_s16(ay[i]), cnt[i]),
                            _idiv(_s16(az[i]), cnt[i])))
        else:
            # The original leaves the destination untouched here, so
            # whatever was in the buffer stays. Zero is our choice and
            # only reachable for a vertex no face references.
            normals.append((0, 0, 0))

    return list(zip(vx, vy, vz)), faces, normals


# ------------------------------------------------------------ rotation

_TBL = [None]


def sin_table():
    """The built table, once. 4096 is 1.0."""
    if _TBL[0] is None:
        _TBL[0] = sin_table_build()
    return _TBL[0]


def _s32(v):
    v &= 0xffffffff
    return v - 0x100000000 if v & 0x80000000 else v


def _sincos(ang):
    """ORIGIN the first three instructions of every rotation: the angle
    is masked to 0x1ffc, which both wraps it to the table and drops its
    low two bits, because the entries are dwords."""
    t = sin_table()
    i = (ang & 0x1ffc) >> 2
    return t[i], t[i + 512]


def vec_rot_z(x, y, z, ang):
    """ORIGIN unpacked 1000:35e1 - Vec_RotZ. x is EAX, y EBX, z ECX."""
    sn, cs = _sincos(ang)
    return (_s32(x * cs - y * sn), _s32(y * cs + x * sn), _s32(z << 12))


def vec_rot_x(x, y, z, ang):
    """ORIGIN unpacked 1000:362a - Vec_RotX."""
    sn, cs = _sincos(ang)
    return (_s32(x << 12), _s32(y * cs - z * sn), _s32(z * cs + y * sn))


def vec_rot_y(x, y, z, ang):
    """ORIGIN unpacked 1000:3674 - Vec_RotY."""
    sn, cs = _sincos(ang)
    return (_s32(x * cs - z * sn), _s32(y << 12), _s32(z * cs + x * sn))


def _sar(v, n):
    return v >> n                       # Python >> on an int is already SAR


# g_ObjScaleShift = 16 makes the whole position chain the IDENTITY:
# three rotations by zero multiply x by 4096 three times and shift it
# back down by 0xa twice, leaving x << 16, so a shift of 16 returns the
# authored byte coordinate exactly. Verified against all five models.
# It is a derived value, not one read out of the binary.
SCALE_IDENTITY = 16

# The z offset is (-(v + 1)) >> 0xc << 8, so the value that CANCELS is
# -1, not 0. Passing 0 moves every model 256 units CLOSER.
#
# IT IS NOT A PER-OBJECT PARAMETER AND IT IS RARELY -1. img 0x4e33 and
# 0x4e44 read a fixed address - element 0 of g_SlotPosX and
# g_SlotPosZ - so it is a scene term shared by every object, and the
# opening runs with it at 0. Defaulting a caller to the cancelling
# value draws the whole model 256 units too far away; read the global
# instead. docs/lithium/effects/rockintro.txt section 2.
FUDGE_Z_NEUTRAL = -1


def mesh_transform(src_off, nrm, ang, cur_pos, scale_shift=SCALE_IDENTITY,
                   env_angle=0, mirror=False, axis_swap=False,
                   fudge_x=0, fudge_z=FUDGE_Z_NEUTRAL):
    """ORIGIN unpacked 1000:4d2c - Mesh_Transform.

    One model into the scene pools. Returns (verts, faces) where a
    vert is (x, y, z, u, v) - the 16-byte record's five words - and a
    face is (i0, i1, i2) with the pool rebasing left out, because the
    sim transforms one model at a time.

    u and v ARE THE ENVIRONMENT-MAP COORDINATES and are the whole
    chrome effect: a view-space normal biased by +0x80 into a 256-wide
    texture. docs/lithium/subsystems/mesh3d.txt.

    ang is (x, y, z) from g_ObjAngle*, cur_pos is (x, y, z) from
    g_CurPos*. The distance cull the original does first is left to
    the caller, since the sim always wants the model.
    """
    img = image()
    base = ASSET_BASE + src_off
    nv, nf = struct.unpack_from('<2H', img, base)
    ax, ay, az = ang
    ox, oy, oz = cur_pos

    out = []
    si = base + 4
    for i in range(nv):
        x, y, z = _s8(img[si]), _s8(img[si + 1]), _s8(img[si + 2])
        if axis_swap:
            y, z = z, -y                # XCHG CL,BL then NEG BL
        # The POSITION: Z, X, Y, each followed by SAR 0xa.
        x, y, z = vec_rot_z(x, y, z, -(az * 2))
        x, y, z = _sar(x, 0xa), _sar(y, 0xa), _sar(z, 0xa)
        x, y, z = vec_rot_x(x, y, z, -ax)
        x, y, z = _sar(x, 0xa), _sar(y, 0xa), _sar(z, 0xa)
        x, y, z = vec_rot_y(x, y, z, -ay)
        x, y, z = (_sar(x, scale_shift), _sar(y, scale_shift),
                   _sar(z, scale_shift))
        x -= _sar(ox, 4)
        y -= _sar(oy, 4)
        z += _sar(oz, 4)
        x += _sar(fudge_x, 0xc) << 8
        z += _sar(-(fudge_z + 1), 0xc) << 8
        px, py, pz = x & 0xffff, y & 0xffff, z & 0xffff

        # The NORMAL, through the same three rotations but with SAR 0xc,
        # the Y angle offset by a quarter circle, and then a fourth
        # rotation about Z by the env-map angle.
        nx, ny, nz = nrm[i]
        if axis_swap:
            ny, nz = nz, -ny
        nx, ny, nz = vec_rot_z(nx, ny, nz, -(az * 2))
        nx, ny, nz = _sar(nx, 0xc), _sar(ny, 0xc), _sar(nz, 0xc)
        nx, ny, nz = vec_rot_x(nx, ny, nz, -ax)
        nx, ny, nz = _sar(nx, 0xc), _sar(ny, 0xc), _sar(nz, 0xc)
        nx, ny, nz = vec_rot_y(nx, ny, nz, -ay - 0x400)
        nx, ny = _sar(nx, 0xb), _sar(ny, 0xb)
        nx, ny, nz = vec_rot_z(nx, ny, nz, env_angle)
        if mirror:
            nx, ny = -nx, -ny
        u = (_sar(nx, 0x11) + 0x80) & 0xffff
        v = (_sar(ny, 0x11) + 0x80) & 0xffff

        out.append((_s16(px), _s16(py), _s16(pz), _s16(u), _s16(v)))
        si += 3

    faces = []
    for _ in range(nf):
        i0, i1, i2 = struct.unpack_from('<3H', img, si)
        si += 6
        if mirror:
            i0, i2 = i2, i0             # XCHG AX,CX reverses the winding
        faces.append((i0, i1, i2))
    return out, faces


# ------------------------------------------------------------- raster

NEAR_Z = 0x7d                   # the near-plane clamp
FOCAL = 8                       # (coord << 8) / z, so 256
CENTRE_X = 0xa0                 # 160, 100 - the centre of 320x200
CENTRE_Y = 0x64
GUARD = 0x800                   # the pre-centre clamp
FADE_BIAS = 0x28000             # where the env-map depth fade starts
FAR_Z = 0xd9a80                 # the baked far reject on summed z


def scene_project_verts(verts, cam_pos=(0, 0, 0), cam_ang=(0, 0, 0)):
    """ORIGIN unpacked 1000:2e48 - Scene_ProjectVerts.

    Takes what mesh_transform returned and gives back, per vertex,
    (screen_x, screen_y, z, u, v, world_x, world_z).

    THE CAMERA ROTATES Y THEN X THEN Z, where Mesh_Transform rotates
    Z then X then Y. The two are not the same sequence and swapping
    them silently tilts everything.

    docs/lithium/subsystems/raster.txt section 2.
    """
    out = []
    for (x, y, z, u, v) in verts:
        wx, wz = x, z                   # stashed at +0x0c and +0x0e
        x += cam_pos[0]
        y += cam_pos[1]
        z += cam_pos[2]
        x, y, z = vec_rot_y(x, y, z, cam_ang[1])
        x, y, z = _sar(x, 0xa), _sar(y, 0xa), _sar(z, 0xa)
        x, y, z = vec_rot_x(x, y, z, cam_ang[0])
        x, y, z = _sar(x, 0xa), _sar(y, 0xa), _sar(z, 0xa)
        x, y, z = vec_rot_z(x, y, z, cam_ang[2])
        x, y, z = _sar(x, 0xa), _sar(y, 0xa), _sar(z, 0xa)

        if z < NEAR_Z:                  # a CLAMP, not a reject
            z = NEAR_Z
        sx = _idiv(x << FOCAL, z)
        sy = _idiv(y << FOCAL, z)
        sx = max(-GUARD, min(GUARD, sx)) + CENTRE_X
        sy = max(-GUARD, min(GUARD, sy)) + CENTRE_Y

        # The depth fade, on the env-map coordinates themselves. The
        # divide is unsigned in the original and u, v are zero-extended.
        d = _sar(z - FADE_BIAS, 7)
        if d >= 0x100:
            u = ((u & 0xffff) << 0xf) // d >> 7
            v = ((v & 0xffff) << 0xf) // d >> 7
        out.append((sx, sy, z, u, v, wx, wz))
    return out


def scene_cull_sort(pv, faces, depth_near=0):
    """ORIGIN unpacked 1000:31ca pass one - Scene_DrawFaces.

    Returns [(key, face_index), ...] for the faces that survive, in
    the order they should be drawn.

    THE BACKFACE TEST IS A 2D CROSS PRODUCT ABOUT VERTEX 2, taken on
    the SCREEN coordinates after projection, not in view space:

        keep if (x0-x2)*(y1-y2) - (y0-y2)*(x1-x2) >= 0

    The clip-code stage the original runs first is left out, because
    maybe_Clip_Point at 1000:5fda is not read yet.

    FAR TO NEAR IS OURS. The key is the summed z and the original
    hands the list to maybe_Sort_ByDepth at 1000:3e30, which has not
    been read, so the DIRECTION is an assumption - painter order is
    the one that looks right for a solid model.
    """
    keep = []
    for n, (i0, i1, i2) in enumerate(faces):
        x0, y0 = pv[i0][0], pv[i0][1]
        x1, y1 = pv[i1][0], pv[i1][1]
        x2, y2 = pv[i2][0], pv[i2][1]
        if (x0 - x2) * (y1 - y2) - (y0 - y2) * (x1 - x2) < 0:
            continue
        ebp = pv[i0][2] + pv[i1][2] + pv[i2][2]
        if not (depth_near < ebp <= FAR_Z):
            continue
        keep.append((ebp >> 8, n))
    keep.sort(key=lambda e: -e[0])
    return keep


# ------------------------------------------------- texture and palette

# img 0x8f20 - g_RndState0 and g_RndState1, seeded IN THE IMAGE to
# 0xa5b5 and 0xcab6. A ZERO STATE IS A FIXED POINT of this generator,
# so the seed is load-bearing and cannot be invented: with (0, 0) it
# returns the same number for ever and the texture comes out blank.
RND_SEED = 0x8f20

_RND = [0, 0]


def rnd_reset():
    """Reload the seed the image ships.

    1000:52dd reseeds the generator later, but Demo_Main calls it at
    1000:01f1 - after Tex_BuildSphereMap at 1000:0168 - so the map is
    built from the stored seed. Nothing is ever seeded from a clock,
    which is what makes caching the result safe.
    """
    _RND[0], _RND[1] = struct.unpack_from('<2H', image(), RND_SEED)


def rnd_next():
    """ORIGIN unpacked 1000:3dfc - Rnd_Next.

        ax = state0; dx = state1
        xchg ah, dl
        ax += dx
        state0 = dx; state1 = ax
        return rol(ax, 2)

    The ROL affects only the returned value, not the state.
    """
    ax, dx = _RND[0], _RND[1]
    ah, al = (ax >> 8) & 0xff, ax & 0xff
    dh, dl = (dx >> 8) & 0xff, dx & 0xff
    ah, dl = dl, ah                     # XCHG AH,DL
    ax = (ah << 8) | al
    dx = (dh << 8) | dl
    ax = (ax + dx) & 0xffff
    _RND[0], _RND[1] = dx, ax
    return ((ax << 2) | (ax >> 14)) & 0xffff


def rnd_next_pair():
    """Rnd_Next's AX **and** DX. The routine returns a rotated AX, and
    leaves DX holding the value it just stored into g_RndState0 - and
    several callers use BOTH, Dust_SampleMask and Tick_Dust among
    them. Calling rnd_next() twice is NOT the same thing.
    """
    ax, dx = _RND[0], _RND[1]
    ah, al = (ax >> 8) & 0xff, ax & 0xff
    dh, dl = (dx >> 8) & 0xff, dx & 0xff
    ah, dl = dl, ah
    ax = (ah << 8) | al
    dx = (dh << 8) | dl
    ax = (ax + dx) & 0xffff
    _RND[0], _RND[1] = dx, ax
    return ((ax << 2) | (ax >> 14)) & 0xffff, dx


def tex_plot(buf, x, y):
    """ORIGIN unpacked 1000:597f - Tex_Plot. A 16x16 additive disc.

    THE DIAGONAL REJECT IS THE POINT. A plot is dropped unless
    x + y >= 0x46, which leaves the (0,0) corner of the map MUCH
    darker than the rest - and that corner is where
    Scene_ProjectVerts drags the map coordinates of a distant surface,
    so the dark corner IS the fog. raster.txt section 2.1.

    DARKER, NOT BLACK. Building it here measures the protected
    triangle at a mean of 3.1 against 23.4 over the whole map, with
    isolated values up to 49 along its hypotenuse: the eight blur
    passes bleed light back across the diagonal. So the fog fades
    toward a dark value rather than to zero.

    x and y are compared UNSIGNED against 0xff, so a negative
    coordinate arrives as a large word and is rejected.
    """
    if y > 0xff or x > 0xff:
        return
    if x + y < 0x46:
        return
    img = image()
    si = (y << 8) + x
    di = BRUSH
    for _ in range(0x10):
        for _ in range(0x10):
            v = buf[si] + img[di]
            buf[si] = 0x7f if v & 0x80 else v   # JNS, so it saturates
            si = (si + 1) & 0xffff      # SI is 16-bit, so it WRAPS
            di += 1
        si = (si + 0xf0) & 0xffff       # 0x100 - 0x10, the next row
    return


def tex_stroke(buf, sx, sy):
    """ORIGIN unpacked 1000:5938 - Tex_Stroke.

    Scatters ONE dot on a circle of random radius about (sx, sy). It
    does not step along a path - hundreds of calls make a soft blob.
    """
    t = sin_table()
    bx = (rnd_next() & 0x1ffc) >> 2
    r = (rnd_next() >> 3) + 0xa0
    x = (_s32(t[bx] * r) >> 0x12) & 0xffff
    y = (_s32(t[bx + 512] * r) >> 0x12) & 0xffff
    tex_plot(buf, (x + sx) & 0xffff, (y + sy) & 0xffff)


def tex_blur(buf, times):
    """ORIGIN unpacked 1000:4aee - Tex_Blur.

    The mean of the four neighbours at -1, +1, +0x100 and -0x100, so
    THE MAP IS 256 WIDE and DI wrapping at 16 bits makes it 256x256.

    IT BLURS IN PLACE and that is load-bearing: the left and upper
    neighbours have already been replaced this pass, which makes the
    smear directional. A two-buffer blur gives a different texture.
    """
    for _ in range(times):
        di = 0
        while True:
            v = (buf[(di - 1) & 0xffff] + buf[(di + 1) & 0xffff]
                 + buf[(di + 0x100) & 0xffff] + buf[(di - 0x100) & 0xffff])
            buf[di] = (v >> 2) & 0xff
            di = (di + 1) & 0xffff
            if di == 0:
                break
    return buf


def tex_build_sphere_map():
    """ORIGIN unpacked 1000:5899 - Tex_BuildSphereMap.

    The whole environment map, 256x256, in five steps and no data:
    clear, 35 random walks of 450 dots, 85 radial streaks of 250 dots,
    8 blurs, then halve every byte so the result is 0..0x7f.

    docs/lithium/subsystems/texture.txt.
    """
    rnd_reset()
    buf = bytearray(0x10000)
    t = sin_table()

    for _ in range(0x23):                       # 35 random walks
        sx = (~rnd_next()) & 0xff               # XOR AH,AH then NOT AL
        sy = (~rnd_next()) & 0xff
        for _ in range(0x1c2):                  # 450 dots
            tex_stroke(buf, sx, sy)

    for _ in range(0x55):                       # 85 radial streaks
        bx = (rnd_next() & 0x1ffc) >> 2
        r = rnd_next() >> 4
        sx = ((_s32(t[bx] * r) >> 0x12) + 0xde) & 0xffff
        sy = ((_s32(t[bx + 512] * r) >> 0x12) + 0xde) & 0xffff
        for _ in range(0xfa):                   # 250 dots
            tex_stroke(buf, sx, sy)

    tex_blur(buf, 8)
    for i in range(0x10000):                    # SHR byte ptr [DI],1
        buf[i] >>= 1
    return buf


_TEX = [None]


def sphere_map():
    """The built map, once per process, cached on disk between runs.

    CACHING IS SAFE BECAUSE IT IS DETERMINISTIC - rnd_next is never
    seeded from a clock, so the demo draws the same map every time it
    runs. The cache lives in the temp directory, never in the repo.
    """
    if _TEX[0] is not None:
        return _TEX[0]
    import hashlib
    import tempfile
    # The key covers the brush, the seed AND a transcription version,
    # so editing the generator here invalidates the cache. Without the
    # version a broken build gets served for ever.
    key = hashlib.sha256(image()[BRUSH:BRUSH + 256]
                         + image()[RND_SEED:RND_SEED + 4]
                         + b'tex1').hexdigest()[:16]
    path = os.path.join(tempfile.gettempdir(),
                        'lithium_spheremap_%s.bin' % key)
    if os.path.exists(path):
        with open(path, 'rb') as f:
            data = bytearray(f.read())
        if len(data) == 0x10000:
            _TEX[0] = data
            return data
    data = tex_build_sphere_map()
    try:
        with open(path, 'wb') as f:
            f.write(data)
    except IOError:
        pass
    _TEX[0] = data
    return data


_PAL_PHASE = [0, 0, 0]


def pal_phase_seed(a, b, c):
    """Pal_Animate's three accumulators are SEEDED by the scene, not
    started from zero. maybe_Scene_BumpShatter writes 0xf8a3, 0xf6f0
    and 0xf41b at img 0x0765 to 0x0773, so the cycling begins at a
    defined point in the ramp rather than wherever the last scene left
    it. docs/lithium/subsystems/texture.txt.
    """
    _PAL_PHASE[0], _PAL_PHASE[1], _PAL_PHASE[2] = a, b, c


def pal_animate(pal_ramp_shift=0):
    """ORIGIN unpacked 1000:6d49 - Pal_Animate. THE COLOUR CYCLING.

    Three phases advance by 0xd, 0x10 and 0x15 of 2048 per call, each
    indexes the sine table, and the sample becomes a per-channel SLOPE.
    A 16-bit accumulator is then stepped by its slope and ITS HIGH BYTE
    IS THE DAC VALUE, clamped at 0x3f.

    So each channel is a linear ramp up from black, and what cycles is
    the ramp's colour. It fills 128 ENTRIES and blacks out entry 128;
    129 to 255 are left alone by the original and stay black here.
    """
    t = sin_table()
    _PAL_PHASE[0] = (_PAL_PHASE[0] + 0xd) & 0xffff
    _PAL_PHASE[1] = (_PAL_PHASE[1] + 0x10) & 0xffff
    _PAL_PHASE[2] = (_PAL_PHASE[2] + 0x15) & 0xffff

    # g_PalRampShift at img 0x8fe1 is 0 as the image ships it, and four
    # scene routines write it - 1000:078e, 1000:09b7, 1000:13d5 and
    # 1000:1534 - so a scene can steepen every ramp at once. 0 is the
    # stored value, not a guess.
    shift = pal_ramp_shift
    step = []
    for ph in _PAL_PHASE:
        bx = (ph & 0x1ffc) >> 2
        v = ((t[bx] >> 6) + 0x40) & 0xff
        step.append((v << shift) & 0xffff)

    pal = bytearray(768)
    acc = [0, 0, 0]
    for i in range(128):
        for c in range(3):
            pal[i * 3 + c] = (acc[c] >> 8) & 0xff
            acc[c] = (acc[c] + step[c]) & 0xffff
            if (acc[c] >> 8) > 0x3f:
                acc[c] = 0x3f00
    return pal


# --------------------------------------------------------------- tunnel
#
# The tunnel has NO MESH. It is rebuilt from the sine table every frame:
# a 16-point circle swept along a path whose heading is three angles
# advancing at fixed rates. docs/lithium/effects/tunnel.txt.

TUNNEL_RING_VERTS = 16          # bx runs 0 to 0x2000 in steps of 0x200
TUNNEL_FIRST_RING = -4          # g_TunnelFirstRing, set at img 0x09c7
TUNNEL_LAST_RING = 11           # g_TunnelLastRing, set at img 0x09ce


def _s16(v):
    v &= 0xffff
    return v - 0x10000 if v & 0x8000 else v


def tunnel_step_path(path, ang):
    """ORIGIN unpacked 1000:0602 - Tunnel_StepPath.

    A FIXED STEP ROTATED BY A TURNING FRAME. The heading is not stored
    as a direction; it is three Euler angles that advance by constant
    amounts every ring, so the path curves continuously.

    The rates are 0x100, 0x80 and 0x40 - 4:2:1 - which is what makes
    the bore wind instead of closing into a circle.
    """
    x, y, z = 0, 0, 0x100
    x, y, z = vec_rot_z(x, y, z, ang[2])
    x, y, z = _sar(x, 8), _sar(y, 8), _sar(z, 8)
    x, y, z = vec_rot_x(x, y, z, ang[0])
    x, y, z = _sar(x, 0xc), _sar(y, 0xc), _sar(z, 0xc)
    x, y, z = vec_rot_y(x, y, z, ang[1])
    x, y, z = _sar(x, 0x10), _sar(y, 0x10), _sar(z, 0x10)
    path = (_s16(path[0] + x), _s16(path[1] + y), _s16(path[2] + z))
    ang = ((ang[0] + 0x100) & 0xffff,
           (ang[1] + 0x80) & 0xffff,
           (ang[2] + 0x40) & 0xffff)
    return path, ang


def tunnel_build(path, ang, rings, skip=0):
    """ORIGIN unpacked 1000:0496 - Tunnel_Build, the vertex half.

    Returns (verts, path, ang). A vertex is (x, y, z, u, v), the same
    shape mesh_transform produces, so scene_project_verts takes it
    unchanged.

    A RING IS A CIRCLE. The cosine and the sine are one table read
    0x800 BYTES apart - img 0x050a reads cs:[bx+0xa8b2] and img 0x0510
    cs:[bx+0xa0b2], and g_SinTable is 0xa0b2 - so a vertex starts as
    (cos t, sin t, 0) and is carried into the path's frame.

    THE THREE SHIFTS DIFFER - 0xa, 0xc, 0x11 - so the ring is scaled
    unequally on each axis and the bore is an ellipse that changes
    shape along the run.
    """
    tbl = sin_table()
    verts = []
    # img 0x04e3: the first g_TunnelFirstRing rings are WALKED AND
    # THROWN AWAY, which is how the geometry behind the camera is
    # skipped with no clip test - and how the window reaches ring
    # 1000 for the cost of 1000 steps and nothing stored.
    for _ in range(max(0, skip)):
        path, ang = tunnel_step_path(path, ang)
    for _ in range(rings):
        bx = 0
        while bx < 0x2000:
            i = bx >> 2
            x, y, z = tbl[i + 512], tbl[i], 0      # cosine, sine
            x, y, z = vec_rot_z(x, y, z, ang[2])
            x, y, z = _sar(x, 0xa), _sar(y, 0xa), _sar(z, 0xa)
            x, y, z = vec_rot_x(x, y, z, ang[0])
            x, y, z = _sar(x, 0xc), _sar(y, 0xc), _sar(z, 0xc)
            x, y, z = vec_rot_y(x, y, z, ang[1])
            x, y, z = _sar(x, 0x11), _sar(y, 0x11), _sar(z, 0x11)
            # img 0x0546: the three adds are 16-BIT, and the stores at
            # img 0x0555 are words, so the coordinate wraps in 16 bits.
            verts.append((_s16(x + path[0]),
                          _s16(y + path[1]),
                          _s16(z + path[2]),
                          (ang[0] >> 3) & 0xff,       # u, img 0x055f
                          (bx >> 3) & 0xff))          # v, img 0x0566
            bx += 0x200
        path, ang = tunnel_step_path(path, ang)
    return verts, path, ang


def tunnel_faces(rings):
    """ORIGIN unpacked 1000:05a0 - Tunnel_Build's face half.

    Two triangles per quad, 16 quads per ring, between consecutive
    rings. The ring index wraps with & 0xf, which is what closes the
    bore without a seam vertex.

    img 0x05d0 subtracts 0x11 and masks with 0xf, and the mask also
    kills the ring base in CX because that is always a multiple of
    0x10 - so the second triangle's two extra corners come out as
    cx+a+0x10 and cx+a.
    """
    faces = []
    cx = 0
    for _ in range(max(0, rings - 1)):
        for a in range(0x10):
            v0 = cx + a
            v1 = cx + ((a + 1) & 0xf)
            v2 = v1 + 0x10
            faces.append((v0, v1, v2))
            faces.append((v2, cx + a + 0x10, cx + a))
        cx += 0x10
    return faces


def tunnel_tick_init():
    """The state 1000:6b7f works on. first and last are the only two
    the scene before sets - maybe_Scene_BumpShatter, img 0x09c7 and
    0x09ce. The rest start at zero because Obj_Reset cleared them and
    nothing read sets them for this scene.
    """
    return {'first': TUNNEL_FIRST_RING, 'last': TUNNEL_LAST_RING,
            'flight': [0, 0, 0], 'home': [0, 0, 0], 'vel': [0, 0, 0],
            'pitch': 0, 'yaw': 0, 'roll': 0, 'rec': 0,
            # cs:[0x8af9], [0x8b27], [0x8b55] - tunnel.txt section 7
            'cam': [0, 0, 0]}


def tunnel_tick(st):
    """ORIGIN unpacked 1000:6b7f - the tunnel scene's tick handler.

    THIS IS WHAT MAKES THE TUNNEL ENDLESS, and it does it without
    storing any geometry at all.

    Every 16 ticks it slides the ring WINDOW forward by one:

        g_TunnelFirstRing++
        g_TunnelLastRing++

    Tunnel_Build pre-steps the path g_TunnelFirstRing times and throws
    those rings away, then emits last - first of them, so the count
    stays at 15 while the window walks the curve. A ring appears at
    the far end and one drops off the near end - and being a thousand
    rings along costs only the pre-step loop, because the path is
    regenerated from ring 0 every frame.

    In the same breath it recomputes the camera's velocity:

        eax, ebx, ecx = (0, 0, 0x100)
        Vec_RotZ2 by g_SlotRoll,  SAR 8
        Vec_RotX2 by g_SlotPitch, SAR 0xc
        Vec_RotY2 by g_SlotYaw,   SAR 0x10
        negate x and y
        g_SlotPitch += 0x100;  g_SlotYaw += 0x80

    THAT IS Tunnel_StepPath'S ARITHMETIC - the same step, the same
    three rotations, the same shifts, the same angle increments. The
    camera walks the identical curve the tunnel is built along, one
    ring per 16 ticks, in lockstep with the window.

    IT DOES NOT ADVANCE THE ROLL. Tunnel_StepPath adds 0x40 to its
    third angle every ring; this adds nothing to g_SlotRoll, so the
    camera does not twist with the rings.

    Per tick, between those: the flight position integrates the
    velocity and the home position eases after it by a 32nd - the
    house idiom, effects/flight.txt section 6.
    """
    for i in range(3):
        st['flight'][i] = _s32(st['flight'][i] + st['vel'][i])
        st['home'][i] = _s32(st['home'][i]
                             + _sar(st['flight'][i] - st['home'][i], 5))
    # img 0x6be5 - THE CAMERA'S ORIENTATION, reaching the renderer
    # through Obj_LoadOrientA. tunnel.txt section 7.
    st['cam'][0] = (st['cam'][0] - 0x10) & 0xffff
    st['cam'][1] = (st['cam'][1] - 0x08) & 0xffff
    st['cam'][2] = (st['cam'][2] - 0x0c) & 0xffff
    st['rec'] = (st['rec'] + 1) & 0xff          # g_RecordMode, img 0x6bf7
    if st['rec'] & 0xf:
        return
    st['first'] += 1
    st['last'] += 1
    x, y, z = 0, 0, 0x100
    x, y, z = vec_rot_z(x, y, z, st['roll'])
    x, y, z = _sar(x, 8), _sar(y, 8), _sar(z, 8)
    x, y, z = vec_rot_x(x, y, z, st['pitch'])
    x, y, z = _sar(x, 0xc), _sar(y, 0xc), _sar(z, 0xc)
    x, y, z = vec_rot_y(x, y, z, st['yaw'])
    x, y, z = _sar(x, 0x10), _sar(y, 0x10), _sar(z, 0x10)
    st['vel'] = [-x, -y, z]                     # img 0x6c67, x and y only
    st['pitch'] = (st['pitch'] + 0x100) & 0xffff
    st['yaw'] = (st['yaw'] + 0x80) & 0xffff


ROCK_MESH = 0x41ea              # 130 verts, 256 faces - the rock


def rock_tick_init(home_z=0):
    """Tick_Rock's state. home_z is where the previous scene left slot
    4; nothing in the runner writes it - rock.txt section 5.
    """
    return {'ang': [0, 0, 0], 'path': [0, 0, 0], 'home_z': home_z,
            'slide': 0}


def rock_tick(st):
    """ORIGIN unpacked 1000:6b35 - Tick_Rock, and 1000:6008 inside it.

    Returns (pos, ang): the position Obj_Select hands Mesh_Transform
    and the three angles it hands it too.

    SIX RATES, TWO JOBS. One set of three spins the mesh, the other
    walks a Lissajous, and nothing couples them.

    THE RETREAT IS AN EXPONENTIAL EASE with its fixed point at
    -0x4e20, so the rock creeps away and accelerates, and phase four
    ends when it passes -0xfa0.
    """
    tbl = sin_table()
    # Obj_WobblePos, img 0x6008: the position is SAMPLED, not integrated
    px = _sar(tbl[(st['path'][0] & 0x1ffc) >> 2], 2) + st['slide']
    py = _sar(tbl[(st['path'][1] & 0x1ffc) >> 2], 2)
    pz = _sar(tbl[(st['path'][2] & 0x1ffc) >> 2], 1) + 0x1f40 + st['home_z']
    for i, d in enumerate((0x24, 0x0f, 0x2b)):          # the tumble
        st['ang'][i] = (st['ang'][i] + d) & 0xffff
    for i, d in enumerate((0x45, 0x24, 0x36)):          # the Lissajous
        st['path'][i] = (st['path'][i] + d) & 0xffff
    st['home_z'] = _s32(st['home_z']
                        - _sar(st['home_z'] + 0x4e20, 8))
    st['slide'] -= 4
    return (px, py, pz), tuple(st['ang'])


def rock_gone(st):
    """img 0x0ac2 - phase four ends when the rock passes -0xfa0."""
    return st['home_z'] < -0xfa0


# ===================================================== the bump scene
#
# maybe_Scene_BumpShatter phases 1 and 2. docs/lithium/effects/bump.txt
# and effects/dust.txt.

SQ_TABLE = 0xc8b2               # g_SquareTable, 1600 entries
GLOW_CENTRES = ((0x40, 0x70), (0xb0, 0xc0), (0xc0, 0x40))
DUST_COUNT = 1000               # 0x3820 to 0x6700 at 0xc a record
MASK_OFF = 0x6700               # where the text goes, 320 x 20
BRUSH_A = 0xa031                # the two 8x8 splats, in the CODE segment
BRUSH_B = 0xa071


def square_table():
    """ORIGIN unpacked 1000:5334 - Math_BuildSquareTable.

    1600 entries, entry i holding (i+1) squared. The SAME 0x1900 bytes
    are the rasteriser's span buffer, which is why the script rebuilds
    this before every batch of texture work - raster.txt section 6.4.
    """
    if _SQ[0] is None:
        _SQ[0] = [(i + 1) * (i + 1) for i in range(0x640)]
    return _SQ[0]


_SQ = [None]


def math_sqrt(v):
    """ORIGIN unpacked 1000:4a91 - Math_Sqrt. Returns the INDEX, which
    the original leaves as a byte offset and every caller shifts by 2.

    A binary search over square_table, not a root: ten halvings of a
    dword-aligned index land on the nearest entry.
    """
    t = square_table()
    bx = si = 0x640 >> 1
    while bx:
        bx >>= 1
        for step in (si - bx, si + bx):
            if 0 <= step < len(t) and abs(t[step] - v) < abs(t[si] - v):
                si = step
        if bx == 0:
            break
    return si


def tex_build_three_glows():
    """ORIGIN unpacked 1000:5721 - Tex_BuildThreeGlows.

    The 256x256 picture the bump map refracts. The script builds it at
    img 0x029d, immediately before the bump scene.

    THE COUNTERS RUN DOWN WHILE DI RUNS UP, so a centre written as c
    lands at pixel 0x100 - c. Transcribed as the original has it.
    """
    buf = bytearray(0x10000)
    sq = square_table()
    di = 0
    ty = 0x100
    while ty:
        tx = 0x100
        while tx:
            for cx, cy in GLOW_CENTRES:          # Tex_AddGlow, img 0x5786
                bx = abs(tx - cx)
                if bx >= 0x3f:
                    continue
                v = sq[bx]
                bx = abs(ty - cy)
                if bx >= 0x3f:
                    continue
                v += sq[bx]
                d = math_sqrt(v)
                al = 0x3f - d
                if al > 0:
                    buf[di] = al
            di += 1
            tx -= 1
        ty -= 1
    return buf


_FONT = [None]


def font_sheet():
    """ORIGIN unpacked 1000:0c59 - Font_Unpack.

    320 x 20, 26 cells of 12 x 20. The sheet is RLE at asset 0xc345
    and expands IN PLACE over the title picture at 0xad1f: each source
    byte is one run, bit 7 the value - 0x0f if set - and bits 0 to 6
    the length minus one. Then an outline pass writes 0x1f into any
    byte with a 0x0f in its four-neighbourhood.

    effects/text.txt section 5.
    """
    if _FONT[0] is not None:
        return _FONT[0]
    img = image()
    n, buf, si, di = 0x1900, bytearray(0x1900), 0xc345, 0
    while di < n:
        cl = img[ASSET_BASE + si]
        si += 1
        al = 0x0f if (cl & 0x80) else 0
        for _ in range((cl & 0x7f) + 1):
            if di < n:
                buf[di] = al
                di += 1
    for k in range(0x140, 0x140 + 0x1680):      # the outline, img 0x0c88
        if buf[k] == 0x0f:
            continue
        for d in (1, -1, 0x140, -0x140):
            j = k + d
            if 0 <= j < n and buf[j] == 0x0f:
                buf[k] = 0x1f
                break
    _FONT[0] = buf
    return buf


def text_blend_pixel(dst, src, dl):
    """ORIGIN unpacked 1000:2d96 - the glyph blend, inside
    Text_DrawStringAt's inner loop.

    TWO SHEET VALUES, OPPOSITE SIGNS. The top two bits of the
    destination are its palette band and are put back untouched; the
    blend works on the 6-bit index inside that band.

        src == 0      transparent, the destination is left alone
        src <= 0x0f   ADD, saturating at 0x3f        the FILL
        src >  0x0f   SUB (src - 0x10), flooring 0   the OUTLINE

    SO THE OUTLINE DARKENS. It is a drop shadow, not a lit pixel, and
    against a zeroed buffer it floors to zero - which is what
    Dust_SampleMask rejects on. That is how a glyph's counters stay
    empty: they are written with 0x1f and come out as 0.

    docs/lithium/effects/text.txt section 5.
    """
    if src == 0:
        return dst
    band, ah = dst & 0xc0, dst & 0x3f
    if src <= 0x0f:
        ah += (src - dl) & 0xff
        if ah > 0x3f:
            ah = 0x3f
    else:
        al = (src - 0x10 - dl) & 0xff
        ah = ah - al if ah >= al else 0
    return (ah + band) & 0xff


def text_draw_into_mask(text, pen=MASK_OFF, dl=0):
    """ORIGIN unpacked 1000:2d46 - Text_DrawStringAt, into a 320-wide
    buffer at g_TextPenX.

    The glyph index is the character MINUS 0x41 and the sheet is
    alphabetical except that it has no Q and W sits in Q's slot -
    effects/text.txt section 6 - so the caller passes what the demo
    stores, not what it reads.

    THE PITCH IS 0xa AND THE CELL IS 0xc, so each glyph overlaps the
    previous by two columns - img 0x2d78 against the 0xc at 0x2d8c.
    """
    sheet = font_sheet()
    mask = bytearray(0x1900)
    for ch in text:
        if ch != ' ':
            g = ord(ch) - 0x41
            if 0 <= g < 26:
                for r in range(0x14):           # DH = 0x14 rows
                    for c in range(0xc):        # CX = 0xc columns
                        o = (pen - MASK_OFF) + r * 320 + c
                        if 0 <= o < len(mask):
                            mask[o] = text_blend_pixel(
                                mask[o], sheet[r * 320 + g * 12 + c], dl)
        pen += 0xa                              # img 0x2d78
    return mask


def dust_reset_from_point(pool):
    """ORIGIN unpacked 1000:0bb5 - Dust_ResetFromPoint."""
    for i in range(DUST_COUNT):
        a0, d0 = rnd_next_pair()
        a1, d1 = rnd_next_pair()
        pool[i] = [0x5000, 0x3200, a0, d0, a1, d1]


def dust_sample_mask(pool, mask, phase):
    """ORIGIN unpacked 1000:0be1 - Dust_SampleMask.

    Rejection-samples the lit pixels of the text and writes only the
    TARGET, so a re-sample moves the cloud rather than teleporting it.

    IT TESTS TWO ADJACENT BYTES, +0x6700 and +0x66ff, so a particle
    needs its own pixel AND the one to its left - which keeps the
    cloud off single-pixel edges.
    """
    # THE SHIFT IS THE WHOLE GEOMETRY. g_DustPhase runs 1 to 0xc and
    # only its high end brings di inside the 20-row mask - at 4 the
    # window lands past the end and almost nothing is accepted. The
    # cap below is OURS, so a phase that cannot hit stops instead of
    # spinning; the demo has no such guard.
    for i in range(DUST_COUNT):
        for _ in range(4000):
            ax, dx = rnd_next_pair()
            dx >>= 1
            bp, di = ax, dx
            if bp >= 0xa000 or di >= 0x6400:
                continue
            di = (di - 0x3200) >> (phase & 0x1f)
            bp >>= 0xa
            di += 0xa
            bx = bp + di * 320
            if not (0 <= bx < 0x1900) or bx - 1 < 0:
                continue
            if mask[bx] == 0 or mask[bx - 1] == 0:
                continue
            pool[i][4], pool[i][5] = ax, dx
            break


def dust_tick(pool, jitter_shift, snap_y=None):
    """ORIGIN unpacked 1000:6a23 - Tick_Dust's mover.

    A TWO-STAGE EASE, NOT A VELOCITY. Only the target is ever set from
    outside; drawn lags mid by a 32nd and mid lags target by an
    eighth. effects/dust.txt section 2.

    The jitter is applied to the TARGET, not the position, so it never
    accumulates - a particle wanders around its destination instead of
    drifting off it.
    """
    # THE COORDINATES ARE UNSIGNED 16-BIT. The stamp reads them with
    # SHR, not SAR - img 0x081a - and a target may be up to 0xa000,
    # which a signed word could not hold. Only the DIFFERENCE in each
    # ease is signed, img 0x6aa5.
    cl = jitter_shift & 7
    for p in pool:
        a, d = rnd_next_pair()
        ax = _sar(_s16(a), cl)
        dx = _sar(_s16(d), cl)
        tx, ty = (p[4] + ax) & 0xffff, (p[5] + dx) & 0xffff
        if snap_y is not None and snap_y > p[3]:
            p[5] = 0x6480
        p[2] = (p[2] + _sar(_s16(tx - p[2]), 3)) & 0xffff
        p[3] = (p[3] + _sar(_s16(ty - p[3]), 3)) & 0xffff
        p[0] = (p[0] + _sar(_s16(p[2] - p[0]), 5)) & 0xffff
        p[1] = (p[1] + _sar(_s16(p[3] - p[1]), 5)) & 0xffff


def dust_brush(interlace):
    """ORIGIN unpacked 1000:0832 - the brush select.

    g_BumpInterlace ZERO PICKS THE WEAK BRUSH. The JNZ at 0x083b
    keeps g_DustBrushA only when the flag is SET, so the scene's
    opening - flag clear - stamps with g_DustBrushB at 0xa071, which
    peaks at 4 against A's 8.
    """
    img = image()
    base = BRUSH_A if interlace else BRUSH_B
    return [img[base + r * 8:base + r * 8 + 8] for r in range(8)]


def dust_stamp(field, pool, brush):
    """ORIGIN unpacked 1000:0806 - the dust plotter, inline in
    maybe_Scene_BumpShatter.

    IT DRAWS NO PIXELS. Each particle adds an 8x8 brush to the HEIGHT
    FIELD, each byte SATURATING at 0xff - which is why a thousand
    points read as a surface and not as noise.

    SEVEN FRACTIONAL BITS: Dust_ResetFromPoint's 0x5000, 0x3200
    shifts down to 160, 100, the centre of the screen.

    BX IS 16-BIT AND THE SUM WRAPS BEFORE THE BOUND IS TESTED, img
    0x0823 against 0x082a. A particle below the screen does not
    vanish - its offset folds back into the segment and it stamps
    somewhere else. Computing the sum in wider arithmetic skips those
    particles instead, leaving the field empty but for the strip
    where the brush overhangs the bottom rows.

    THE UPPER BOUND IS TESTED PER BRUSH ROW, not per particle: img
    0x0842 is the top of the row loop, so a brush near the bottom is
    abandoned part-drawn rather than clipped or wrapped. Writing all
    eight rows regardless puts height into rows the relaxation cannot
    reach. BYTE-EXACT against the emulator over all 65,536 bytes of
    the field.
    """
    for p in pool:
        bx = (((p[1] >> 7) * 320) + (p[0] >> 7)) & 0xffff
        if bx <= 0x140:                             # img 0x082a
            continue
        for r in range(8):
            if bx >= 0xf780:                        # img 0x0842
                break
            row = brush[r]
            for c in range(8):
                v = field[bx + c] + row[c]
                field[bx + c] = 0xff if v > 0xff else v
            bx = (bx + 320) & 0xffff


def height_relax(field, interlace=0, parity=0):
    """ORIGIN unpacked 1000:08af - the height field's relaxation.

    A FOUR-NEIGHBOUR AVERAGE WITH A DECAY OF ONE. The average spreads
    a stamp outward a pixel a frame and the DEC AX at 0x08d3 drains
    it, so a bump rises where particles are and dies where they have
    left. The decay is skipped at zero, so the field floors rather
    than wrapping.

    IT RUNS IN PLACE AND THAT IS LOAD-BEARING. The original stores to
    [DI] and then steps DI, so the next pixel's [DI-1] and the next
    row's [DI-0x140] read values this pass has ALREADY written. The
    smoothing therefore propagates down and to the right within a
    single pass, which a two-buffer version does not reproduce.

    THE INTERLACE HALVES THE WORK AND DOUBLES THE RELIEF. With
    g_BumpInterlace zero - how the scene starts - only 0x63 rows are
    walked and DI skips one each time, so HALF THE FIELD IS LEFT
    ALONE every frame and the untouched rows keep their full height.
    g_BumpFieldParity picks which half. Relaxing every row instead
    drains the field twice as fast and the stamps never build into a
    surface. effects/bump.txt section 5.
    """
    W = SCREEN_W
    di, rows, step = W, 0xc7, 0
    if interlace == 0:
        rows, step = 0x63, W                    # img 0x08a0
        if parity & 1:
            di += W                             # img 0x08ab
    for _ in range(rows):
        for _ in range(W):
            v = (field[di - 1] + field[di + 1]
                 + field[di + W] + field[di - W]) >> 2
            field[di] = v - 1 if v else 0
            di += 1
        di += step                              # img 0x08e1


def bump_render(field, pic, light, out, interlace=0, parity=0):
    """ORIGIN unpacked 1000:0ad1 - Bump_Render.

    THE GRADIENT PAIR IS THE ADDRESS. A byte of slope on each axis
    becomes a 16-bit offset into the picture, which is read at that
    displacement instead of straight on - so a flat area samples
    itself and a slope pulls the sample sideways. There is no dot
    product and no normal. effects/bump.txt section 4.

    THE LIGHT OFFSET IS SWEPT ACROSS THE SCREEN, NOT FIXED. SI drops
    by 2 every four pixels at 0x0b6d and by 0x100 every row at
    0x0ba4, so the picture is MAPPED over the surface - half a texel
    per column, one texel row per screen row. Different parts of the
    screen therefore sit near different glows at the same instant,
    which is what puts more than one highlight on the map.

    IT COVERS ROWS 1 TO 198. The heights are read at [DI+4] and the
    pixels written at [DI], so the relief sits four columns left of
    the field that made it; rows 0 and 199 are never written. Row 199
    is also the one row the interlaced relaxation never reaches, so
    rendering it shows an unrelaxed saturating strip the demo simply
    does not draw.

    THE HORIZONTAL NEIGHBOUR IS AT THE HIGHER ADDRESS. The XCHG and
    ROR at img 0x0b2c carry the PREVIOUS group's first height in as
    this group's fourth neighbour, and CL threads that chain across
    groups and rows - it is never reset.

    THE SUBTRACTIONS ARE 32-BIT and their borrows cross bytes; only
    the doubling's carry is masked off, img 0x0b32 to 0x0b41. Doing
    the four slopes independently disagrees on about a quarter of the
    pixels by one or two levels.

    THE FULL-RATE PATH SWEEPS THE LIGHT EVERY SECOND ROW: img 0x0b94
    goes back round with SI untouched while the field toggle is odd,
    so there the light descends one texel row per TWO screen rows.

    BYTE-EXACT against the emulator over all 63,360 bytes it writes,
    captured at img 0x0ad1 and img 0x08f0 of one frame.

    IT WRITES THE SCREEN DIRECTLY, IN FIELDS. With g_BumpInterlace
    clear FS is 0xa000 and DI steps two rows, so a frame repaints
    half the lines and the rest persist - hence `out` is carried in
    rather than allocated here.

    The original does FOUR PIXELS AT A TIME in 32-bit registers and
    masks with 0xfefefefe after the doubling, which is what stops a
    carry crossing from one pixel's slope into the next.
    """
    M = 0xffffffff

    def dw(i):
        return (field[i] | (field[i + 1] << 8)
                | (field[i + 2] << 16) | (field[i + 3] << 24))

    si = ((light[0] >> 8) + (light[1] & 0xff00)) & 0xffff
    di, cl = 0xf8bc, 0
    toggle = parity & 0xff
    if not interlace:
        toggle = (toggle + 1) & 0xff            # img 0x0b03
        if toggle & 1:
            di -= 0x140                         # img 0x0b10
            si = (si - 0x100) & 0xffff
    else:
        toggle = 0                              # img 0x0af6
    while True:
        for _ in range(0x50):
            eax = dw(di + 4)                    # img 0x0b20
            edx = dw((di - 0x13c) & 0xffff)
            ebx = eax
            cl, ebx = ebx & 0xff, (ebx & 0xffffff00) | cl  # img 0x0b2c
            ebx = ((ebx >> 8) | (ebx << 24)) & M
            edx = (edx - eax) & M                   # img 0x0b32
            eax = (eax - ebx) & M
            eax = (eax << 1) & 0xfefefefe
            edx = (edx << 1) & 0xfefefefe
            px = [pic[((((edx >> (8 * i)) & 0xff) << 8)
                       | ((eax >> (8 * i)) & 0xff))
                      + si + (1 if i >= 2 else 0) & 0xffff]
                  for i in range(4)]
            w = di                              # img 0x0b48 then 0x0b74
            di -= 4
            for i in range(4):
                out[w + i] = px[i]
            si = (si - 2) & 0xffff              # img 0x0b6d
        si = (si + 0xa0) & 0xffff               # img 0x0b7d
        if interlace:
            toggle = (toggle + 1) & 0xff            # img 0x0b89
            if toggle & 1:
                continue
            si = (si - 0x100) & 0xffff
            if di >= 0x280:
                continue
            break
        si = (si - 0x100) & 0xffff              # img 0x0ba4
        di -= 0x140
        if di < 0x280:
            break
    return out


def bump_light_init():
    """Vec_Ease2D's four words plus the velocity pair, and
    Tick_Default's 0x18 countdown.
    """
    return {'vel': [0, 0], 'lead': [0, 0], 'pos': [0, 0], 'rec': 0x18}


def bump_light_tick(st, beat=False):
    """ORIGIN unpacked 1000:6244 - Vec_Ease2D, with the re-aim from
    1000:0c44 - Bump_RandomiseLight - and the countdown that gates it
    in Tick_Default at img 0x6c93.

    THE LIGHT IS WHAT LIGHTS THE FLAT GROUND. Bump_Render adds this
    pair to every gradient lookup, so a pixel whose slope is zero
    samples the picture AT THE LIGHT instead of at its origin.

    THE RE-AIM IS BEAT-GATED. The 0x18 countdown is what releases it,
    and a music position change RESETS that counter without re-aiming
    - img 0x6ccb - so beats arriving faster than 24 ticks hold the
    light still. It also needs cs:[0x8fe4] non-zero, which this scene
    sets to 0x80 at img 0x0794.

    ALL FOUR ACCUMULATORS ARE 16-BIT and wrap: Vec_Ease2D uses
    `add word ptr`, and Bump_Render reads the result with SHR, not
    SAR. So the light sweeps the picture cyclically rather than
    drifting away.
    """
    st['rec'] -= 1
    if st['rec'] <= 0:
        st['rec'] = 0x18
        st['vel'][0] = _sar(_s16(rnd_next_b()), 5)      # img 0x0c47
        st['vel'][1] = _sar(_s16(rnd_next_b()), 5)
    elif beat:
        st['rec'] = 0x18                                # img 0x6ccb
    for i in (0, 1):
        st['lead'][i] = (st['lead'][i] + st['vel'][i]) & 0xffff
        st['pos'][i] = (st['pos'][i]
                        + _sar(_s16(st['lead'][i] - st['pos'][i]), 3)) & 0xffff
    return (st['pos'][0], st['pos'][1])


_RNDB = [0, 0]


def rnd_next_b():
    """ORIGIN unpacked 1000:3e16 - Rnd_NextB, a SECOND generator on its
    own state. The separation is necessary: Sky_SunAndStars reseeds
    the main one every frame and Flicker_Step perturbs it, either of
    which would wreck anything that must not repeat.
    """
    ax, dx = _RNDB[0], _RNDB[1]
    ah, al = (ax >> 8) & 0xff, ax & 0xff
    dh, dl = (dx >> 8) & 0xff, dx & 0xff
    ah, dl = dl, ah
    ax = (ah << 8) | al
    dx = (dh << 8) | dl
    ax = (ax + dx) & 0xffff
    _RNDB[0], _RNDB[1] = dx, ax
    return ((ax << 2) | (ax >> 14)) & 0xffff


def rnd_b_reset(a=0xa5b5, b=0xcab6):
    """g_RndStateB0 and B1 at img 0x8fc7, and the IMAGE CARRIES THEM -
    the same pair it gives the main generator. Nothing in the demo
    but Rnd_NextB touches them, so the bump scene still finds the
    loaded value when it asks [verified against the emulator at the
    scene's entry].
    """
    _RNDB[0], _RNDB[1] = a, b


def dust_state_init(phase=0x0c):
    """Tick_Dust's two counters.

    THE SHIFT TRADES THE GLYPH AGAINST ITS SIZE. It is the SAR count
    applied to the mask row at img 0x0c0e, so a big shift stretches a
    few mask rows over the whole screen and a small one fits every row
    but shrinks the word. The ink occupies 14 of the cell's 20 rows:

        0xff..0xfd   2 of 14 rows at 99% height   bars
        0x0c         8 of 14        at 99%        big, cropped
        0x0b        13 of 14        at 97%        THE WORD, large
        0x0a        14 of 14        at 55%        THE WORD, half
        0x09        14 of 14        at 27%
        0x08        14 of 14        at 13%

    The default here is OURS: the demo's counter starts wherever the
    previous scene left it, and beginning at 0x0c walks down through
    the readable sizes before the burst instead of after it.
    docs/lithium/effects/dust.txt section 4.1.
    """
    return {'phase': phase, 'jitter': 0}


def dust_mode_step(pool, mask, st):
    """ORIGIN unpacked 1000:6ad6 - Tick_Dust's refill, once per music
    position change. docs/lithium/effects/dust.txt section 2.

    THE PHASE IS BOTH THE MODE AND THE SAMPLE SHIFT, which is why the
    word arrives gradually rather than at once: as the counter runs
    down the shift widens the band of mask rows that can be hit, so
    the cloud spreads from a thin slice to the whole glyph height.

        phase 1          Dust_ResetFromPoint - the burst
        phase 0          nothing is accepted; the shift is too small
        phase 0xff..0xfd rows 9 and 10 only
        phase 0xc..8     rows 7 to 13, the full glyph
    """
    if st['phase'] == 1:
        dust_reset_from_point(pool)
    else:
        dust_sample_mask(pool, mask, st['phase'])
    st['phase'] = (st['phase'] - 1) & 0xff
    st['jitter'] = (st['jitter'] + 1) & 7
    if st['phase'] == 0xfc:
        st['phase'] = 0xc
    if st['phase'] == 7:
        st['phase'] = 1
    return st['phase']


def shatter_build():
    """ORIGIN unpacked 1000:2803 - Shatter_Build, and 1000:2c78 -
    Shatter_InitShard. docs/lithium/effects/shatter.txt.

    140 shards: the screen tiled into 32x32 quads, each split in two.
    A shard is a dict with the same fields as Lith_Shard.
    """
    out = []
    dx = -0x54
    while dx < 0x74:
        cx = -0x90
        while cx < 0xb0:
            for corners in (((-16, -16), (16, -16), (-16, 16)),
                            ((-16, 16), (16, -16), (16, 16))):
                ax, bx = cx + 0x90, dx + 0x54
                if corners[0] == (-16, -16):
                    uv = ((ax, bx), (ax + 0x20, bx), (ax, bx + 0x20))
                else:
                    uv = ((ax, bx + 0x20), (ax + 0x20, bx),
                          (ax + 0x20, bx + 0x20))
                # Shatter_InitShard, img 0x2c78
                av = [_sar(_s16(rnd_next()), 7) for _ in range(3)]
                vx = _sar(_s16(rnd_next()), 0xc) + _sar(cx, 6)
                vy = (_sar(_s16(rnd_next()), 0xc) + _sar(dx, 6)
                      + _sar(abs(cx) - 0xa0, 3))
                # img 0x2ce1: vel_z has NO random term and is largest
                # at the centre, so the middle of the glass comes at
                # you fastest.
                vz = _sar(0x104 - (abs(cx) + abs(dx)), 4) + 5
                out.append({'corners': corners, 'uv': uv,
                            'pos': [cx, dx, 0x100], 'ang': [0, 0, 0],
                            'angvel': av, 'vel': [vx, vy, vz]})
            cx += 0x20
        dx += 0x20
    return out


def shatter_step(shards):
    """ORIGIN unpacked 1000:2c05 - Shatter_Step.

    0x100 IS WHERE A SHARD BELONGS: every one starts at pos_z 0x100,
    so a shard flying the other way reaches its home plane and stops
    dead there. Gravity is one unit a tick on vel_y.
    """
    live = 0
    for s in shards:
        p, v, a, av = s['pos'], s['vel'], s['ang'], s['angvel']
        for i in range(3):
            p[i] = _s16(p[i] + v[i])
            a[i] = _s16(a[i] + av[i])
        v[1] = _s16(v[1] + 1)                  # img 0x2c2a, gravity
        if p[2] <= 0x100:                      # img 0x2c46
            s['vel'] = [0, 0, 0]
            s['angvel'] = [0, 0, 0]
        else:
            live += 1
    return live


def shatter_project(shards):
    """ORIGIN unpacked 1000:2ac1 - Shatter_Draw's geometry half.

    Returns [(pts, uvs)] ready for a textured fill. NOTE THE SWAP at
    img 0x2ad8: pos+0x12 drives Y and +0x14 drives X.
    """
    out = []
    for s in shards:
        ax, ay, az = s['ang']
        px, py, pz = s['pos']
        pts, on = [], 0
        for (cxx, cyy) in s['corners']:
            x, y, z = cxx, cyy, 0
            x, y, z = vec_rot_y(x, y, z, ax)
            x, y, z = _sar(x, 9), _sar(y, 9), _sar(z, 9)
            x, y, z = vec_rot_x(x, y, z, ay)
            x, y, z = _sar(x, 9), _sar(y, 9), _sar(z, 9)
            x, y, z = vec_rot_z(x, y, z, az)
            x, y, z = _sar(x, 9), _sar(y, 9), _sar(z, 9)
            x += px << 9
            y += py << 9
            z += pz << 9
            if z < 0x3e8:                      # img 0x2b5c, the near clamp
                z = 0x3e8
            sx = _idiv(x << 8, z) + 0xa0
            sy = _idiv(y << 8, z) + 0x64
            if sx <= 0x140 and sy <= 0xc8:
                on += 1
            pts.append((sx, sy))
        if on:
            uvs = [(min(u, 0x13f), min(v, 0xc7)) for (u, v) in s['uv']]
            out.append((pts, uvs))
    return out
