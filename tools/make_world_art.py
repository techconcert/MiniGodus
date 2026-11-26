"""Generates the r2 world art as editable PNG sheets in assets/src/ and the id header for the game core.

    python tools/make_world_art.py          # writes terrain.png, objects.png, shaman.png, hud.png and
                                            # lib/gamecore/src/world_art_ids.h
    python tools/png2tiles.py               # then converts every sheet into lib/gamecore/src/assets_gen.h

terrain.png   8x8 quarter tiles painted with ROLES (tools/palette.py), not colours. Corner-based autotiling: every 16x16
              map tile is drawn as four quarters. The game picks the quarter from the tile and its three neighbours at
              that corner (horizontal, vertical, diagonal) and maps the roles to palette slots (world.cpp).
              All quarters are painted for the north-west corner of a tile; the game mirrors them for the east side.
objects.png   16x16 trees, bushes, boulders and fish painted with the temperate world palette.
shaman.png    16x16 robed figure with a staff (master palette): down 0-2, up 3-5, left 6-8, right = left mirrored.
hud.png       8x8 icons of the top bar (master palette): flame, wheat, log, person.
village.png   16x16 buildings of the village (world palette): tent, building site, well, hut (2x2 cells), wheat field
              in 3 growth stages, fishing hut (2x1 cells).
villagers.png 16x16 villagers (master palette): adult down 0-2, up 3-5, left 6-8, child 9-11. The tunic (colour 'W') and
              the hair ('b') are recoloured by the game per role and age.
tools.png     8x8 tools held by the villagers (master palette): hoe, crook, rod, axe, hammer, herb, bucket.
Running this overwrites the PNGs; once they exist you may also edit them by hand.
"""
import math
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
from palette import (PALETTE, CHAR_TO_INDEX, WORLD_REF, ROLES, R_AD, R_A, R_AL, R_BD, R_B, R_BL, R_RD, R_RL,
                     R_FD, R_F, R_FL, R_SP, R_SH)

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = os.path.join(ROOT, "assets", "src")
IDS = os.path.join(ROOT, "lib", "gamecore", "src", "world_art_ids.h")

# ------------------------------------------------------------------------------------------------ terrain quarters
N = 8


def blank(v=R_A):
    return [[v] * N for _ in range(N)]


def hash2(x, y, s):
    h = (x * 73856093) ^ (y * 19349663) ^ (s * 83492791)
    h = (h ^ (h >> 13)) * 1274126177
    return (h ^ (h >> 16)) & 0xFFFF


def speckles(g, seed, dark, light, nd, nl):
    for k, (role, n) in enumerate(((R_AD, nd), (R_AL, nl))):
        for i in range(n):
            x, y = hash2(i, k, seed) % N, hash2(k, i, seed + 7) % N
            g[y][x] = role


def fill_ground(v):
    g = blank()
    speckles(g, v, 0, 0, 2 + v % 2, 2)
    if v == 3:      # tuft
        for (x, y, r) in ((2, 5, R_AD), (4, 5, R_AD), (3, 4, R_AD), (3, 3, R_AL), (2, 4, R_AL)):
            g[y][x] = r
    elif v == 4:    # taller tufts
        for (x, y, r) in ((5, 6, R_AD), (6, 5, R_AD), (4, 5, R_AD), (5, 4, R_AL), (4, 4, R_AL), (1, 2, R_AD), (2, 1, R_AL), (0, 2, R_AD)):
            g[y][x] = r
    elif v == 5:    # flowers
        for (x, y) in ((2, 2), (6, 5), (4, 7)):
            g[y][x] = R_SP
        g[2][3] = R_AL
        g[5][5] = R_AL
    return g


def fill_sand(v):
    g = blank()
    for i in range(3):
        x, y = hash2(i, v, 3) % 6, hash2(v, i, 5) % N
        g[y][x] = R_AD
        g[y][x + 1] = R_AD if i == 0 else g[y][x + 1]
    g[hash2(v, 1, 9) % N][hash2(v, 2, 9) % N] = R_AL
    return g


def fill_rock(v):
    g = blank()
    # cracks and glints
    x, y = hash2(v, 1, 11) % 5 + 1, hash2(v, 2, 11) % 4
    for k in range(4):
        g[min(7, y + k)][max(0, min(7, x + (k // 2) * (1 if v % 2 else -1)))] = R_AD
    g[hash2(v, 3, 11) % N][hash2(v, 4, 11) % N] = R_AL
    g[hash2(v, 5, 11) % N][hash2(v, 6, 11) % N] = R_AL
    g[hash2(v, 7, 11) % N][hash2(v, 8, 11) % N] = R_AD
    return g


def fill_snow(v):
    g = blank()
    for i in range(3):
        g[hash2(i, v, 21) % N][hash2(v, i, 22) % N] = R_AD
    g[hash2(v, 5, 23) % N][hash2(v, 6, 23) % N] = R_SP
    return g


def fill_water(v, f):
    g = blank()
    # two short ripples; frame 1 moves them and adds a sparkle
    for i in range(2):
        x0, y0 = hash2(i, v, 31) % 5, (hash2(v, i, 32) % 3) * 2 + i
        if f:
            x0 = (x0 + 3) % 6
            y0 = (y0 + 1) % N
        for k in range(2 + (i == 0)):
            g[y0][min(7, x0 + k)] = R_AL
    if f:
        g[hash2(v, 9, 33) % N][hash2(v, 8, 33) % N] = R_SP
    else:
        g[hash2(v, 7, 34) % N][hash2(v, 6, 34) % N] = R_AD
    return g


def circle_outside(R=8):
    """True for the pixels cut off the north-west corner of a quarter (rounded corner of radius R)."""
    out = [[False] * N for _ in range(N)]
    for y in range(N):
        for x in range(N):
            if x + .5 < R and y + .5 < R and math.hypot(x + .5 - R, y + .5 - R) > R:
                out[y][x] = True
    return out


def dist_field(shape, outside):
    """d[y][x]: distance of an inside pixel to the boundary (0 = outermost row of A pixels).
    bd[y][x]: for an outside pixel, 1 when it touches an inside pixel, else 2+."""
    d = [[99] * N for _ in range(N)]
    for y in range(N):
        for x in range(N):
            if outside and outside[y][x]:
                continue
            v = 99
            if shape in ("EH", "OUT"):
                v = min(v, x)
            if shape in ("EV", "OUT"):
                v = min(v, y)
            if outside:
                for yy in range(N):
                    for xx in range(N):
                        if outside[yy][xx]:
                            v = min(v, abs(xx - x) + abs(yy - y) - 1)
            d[y][x] = v
    bd = [[0] * N for _ in range(N)]
    if outside:
        for y in range(N):
            for x in range(N):
                if outside[y][x]:
                    best = 99
                    for yy in range(N):
                        for xx in range(N):
                            if not outside[yy][xx]:
                                best = min(best, abs(xx - x) + abs(yy - y))
                    bd[y][x] = best
    return d, bd


def edge(shape, style, f=0):
    """One quarter of an edge family. style is a function (d, bd, x, y, f, is_outside) -> role."""
    outside = circle_outside() if shape == "OUT" else None
    d, bd = dist_field(shape, outside)
    g = blank()
    for y in range(N):
        for x in range(N):
            if outside and outside[y][x]:
                g[y][x] = style(None, bd[y][x], x, y, f, True)
            else:
                g[y][x] = style(d[y][x], None, x, y, f, False)
    return g


def st_dither(d, bd, x, y, f, out):
    if out:
        return R_B
    if d == 0:
        return R_B if (x + y + f) % 2 == 0 else R_A
    if d == 1:
        return R_AL if (x + y * 2 + f) % 3 == 0 else R_A
    return R_A


def st_shore_up(d, bd, x, y, f, out):         # sand beside water: wet sand, foam in the water
    if out:
        return R_RL if bd == 1 and (x + y + f) % 2 == 0 else R_B
    if d == 0:
        return R_AD
    if d == 1:
        return R_AD if (x + y) % 3 == 0 else R_A
    return R_A


def st_shore_low(d, bd, x, y, f, out):        # shallow water beside sand: surf
    if out:
        return R_BD if bd == 1 else R_B
    if d == 0:
        return R_RL if (x + y * 2 + f * 2) % 4 < 3 else R_AL
    if d == 1:
        return R_AL if (x + f * 2) % 3 == 0 else R_A
    return R_A


def st_beach_up(d, bd, x, y, f, out):         # grass with a ragged edge over sand
    if out:
        return R_BD if bd == 1 else R_B
    if d == 0:
        return R_B if (x * 3 + y * 5) % 4 == 0 else R_AD
    if d == 1:
        return R_AL if (x + y) % 3 == 0 else R_A
    return R_A


def st_beach_low(d, bd, x, y, f, out):        # sand beside grass
    if out:
        return R_BD if bd == 1 else R_B
    if d == 0:
        return R_AD if (x + y) % 2 == 0 else R_A
    if d == 1:
        return R_AD if (x * 2 + y) % 5 == 0 else R_A
    return R_A


def st_snow_up(d, bd, x, y, f, out):          # snow drifting onto grass
    if out:
        return R_A if (bd == 1 and (x + y) % 2 == 0) else R_B
    if d == 0:
        return R_B if (x * 5 + y * 3) % 2 == 0 else R_AD
    if d == 1:
        return R_B if (x * 5 + y * 3) % 6 == 0 else R_A
    return R_A


def st_snow_low(d, bd, x, y, f, out):         # grass beside snow
    if out:
        return R_AD if (bd == 1 and (x + y) % 2 == 0) else R_B
    if d == 0:
        return R_B if (x * 3 + y * 5) % 2 == 0 else R_A
    if d == 1:
        return R_B if (x * 3 + y * 5) % 6 == 0 else R_A
    return R_A


def st_terr_up(d, bd, x, y, f, out):          # upper ground of a cliff: dark lip line and a light inner line
    if out:
        return R_B if bd > 1 else R_BD
    if d == 0:
        return R_RD
    if d == 1:
        return R_AL
    return R_A


def terr_ups(shape):
    """Upper tile, southern half: the left wall (if the west is lower) and the cliff-top line at the bottom."""
    g = blank()
    for y in range(N):
        for x in range(N):
            v = R_A
            if shape in ("EH", "OUT"):
                if x == 0:
                    v = R_RD
                elif x == 1:
                    v = R_AL
            if shape in ("EV", "OUT"):
                if y == 7:
                    v = R_RD
                elif y == 6 and v == R_A:
                    v = R_AL
            g[y][x] = v
    if shape == "OUT":
        g[7][0] = R_RD
    return g


def face(kind):
    """Cliff face (northern half of the lower tile): strata, vertical erosion streaks, base shadow."""
    g = blank(R_F)
    for x in range(N):
        g[0][x] = R_FL
        g[3][x] = R_FD if x % 2 == 0 else R_F
        g[6][x] = R_FD
        g[7][x] = R_SH
    for (x, y) in ((2, 1), (5, 2), (1, 4), (6, 4), (3, 5), (4, 1)):
        g[y][x] = R_FL if y < 3 else R_FD
    for (x, y) in ((2, 4), (2, 5), (5, 4), (5, 5), (5, 3)):
        g[y][x] = R_FD
    if kind == "END":
        R = 5
        for y in range(N):
            for x in range(N):
                if x + .5 < R and (N - y - .5) < R and math.hypot(x + .5 - R, (N - y - .5) - (N - R)) > R - 0.0 and True:
                    pass
        # rounded convex end at the bottom-left, ground (A) shows outside
        for y in range(N):
            for x in range(N):
                cx, cy = R, N - R
                if x + .5 < R and y + .5 > cy and math.hypot(x + .5 - cx, y + .5 - cy) > R:
                    g[y][x] = R_A
        for y in range(N):
            for x in range(N):
                if g[y][x] != R_A and (x == 0 or any(0 <= x + dx < N and 0 <= y + dy < N and g[y + dy][x + dx] == R_A
                                                      for dx, dy in ((-1, 0), (0, 1)))):
                    if y < 7:
                        g[y][x] = R_RD
        for x in range(N):
            if g[7][x] == R_A and g[7][min(7, x + 1)] != R_A:
                g[7][x] = R_SH
    elif kind == "INNER":
        for y in range(N):
            g[y][0] = R_RD
            g[y][1] = R_FD
            if y > 2:
                g[y][2] = R_FD if y % 2 else R_F
    return g


def shad_h():
    g = blank()
    for y in range(N):
        g[y][0] = R_SH
        g[y][1] = R_AD if y % 2 == 0 else R_A
        g[y][2] = R_AD if y % 4 == 1 else R_A
    return g


def build_terrain():
    arts = []      # (name, grid)
    names = {}

    def add(name, grid):
        names.setdefault(name.split("+")[0], len(arts))
        arts.append((name, grid))

    # animated region first: frame f of art i is at index 2*i+f
    for v in range(4):
        for f in range(2):
            add(f"FILL_WATER_{v}_{f}", fill_water(v, f))
    for fam, up, low in (("WATER", st_dither, st_dither), ("SHORE", st_shore_up, st_shore_low)):
        for role, st in (("UP", up), ("LOW", low)):
            for shape in ("EH", "EV", "OUT"):
                for f in range(2):
                    add(f"{fam}_{role}_{shape}_{f}", edge(shape, st, f))
    an_count = len(arts)
    for v in range(6):
        add(f"FILL_GROUND_{v}", fill_ground(v))
    for v in range(4):
        add(f"FILL_SAND_{v}", fill_sand(v))
    for v in range(4):
        add(f"FILL_ROCK_{v}", fill_rock(v))
    for v in range(3):
        add(f"FILL_SNOW_{v}", fill_snow(v))
    for fam, up, low in (("BEACH", st_beach_up, st_beach_low), ("SNOW", st_snow_up, st_snow_low)):
        for role, st in (("UP", up), ("LOW", low)):
            for shape in ("EH", "EV", "OUT"):
                add(f"{fam}_{role}_{shape}", edge(shape, st))
    for shape in ("EH", "EV", "OUT"):
        add(f"UPN_{shape}", edge(shape, st_terr_up))
    for shape in ("EH", "EV", "OUT"):
        add(f"UPS_{shape}", terr_ups(shape))
    add("LON_FACE", face("FACE"))
    add("LON_END", face("END"))
    add("LON_INNER", face("INNER"))
    add("LON_SHADH", shad_h())
    add("LOS_SHADH", shad_h())
    return arts, an_count


def write_ids(arts, an_count):
    first = {}
    for i, (name, _) in enumerate(arts):
        key = name
        for suf in ("_0", "_1", "_2", "_3", "_4", "_5"):
            pass
        first.setdefault(name, i)
    bases = [("FILL_WATER", "FILL_WATER_0_0"), ("WATER_UP", "WATER_UP_EH_0"), ("WATER_LOW", "WATER_LOW_EH_0"),
             ("SHORE_UP", "SHORE_UP_EH_0"), ("SHORE_LOW", "SHORE_LOW_EH_0"), ("FILL_GROUND", "FILL_GROUND_0"),
             ("FILL_SAND", "FILL_SAND_0"), ("FILL_ROCK", "FILL_ROCK_0"), ("FILL_SNOW", "FILL_SNOW_0"),
             ("BEACH_UP", "BEACH_UP_EH"), ("BEACH_LOW", "BEACH_LOW_EH"), ("SNOW_UP", "SNOW_UP_EH"),
             ("SNOW_LOW", "SNOW_LOW_EH"), ("UPN", "UPN_EH"), ("UPS", "UPS_EH"), ("LON", "LON_FACE"),
             ("LOS", "LOS_SHADH")]
    lines = ["// GENERATED by tools/make_world_art.py - do not edit by hand.",
             "// Ids of the quarter tiles in assets/src/terrain.png (cell number = id).",
             "// Animated arts (id < ART_AN_COUNT): frame f is at id | f, ids are even. Shapes of an edge group: EH, EV, OUT",
             "// (animated groups step by 2: id = base + shape * 2 + frame). LON group: FACE, END, INNER, SHADH.",
             "#pragma once", "enum {", f"  ART_AN_COUNT = {an_count},"]
    for k, nm in bases:
        lines.append(f"  ART_{k} = {first[nm]},")
    lines.append(f"  ART_COUNT = {len(arts)}")
    lines.append("};")
    with open(IDS, "w") as f:
        f.write("\n".join(lines) + "\n")


def save_sheet(path, cells, cw, ch, cols, palette_rgb):
    rows = (len(cells) + cols - 1) // cols
    img = Image.new("RGB", (cols * cw, rows * ch), (255, 0, 255))
    for i, cell in enumerate(cells):
        ox, oy = (i % cols) * cw, (i // cols) * ch
        for y in range(ch):
            for x in range(cw):
                img.putpixel((ox + x, oy + y), palette_rgb[cell[y][x]])
    img.save(path)


# ------------------------------------------------------------------------------------------------ objects (world palette)
class Canvas:
    def __init__(self):
        self.p = [[0] * 16 for _ in range(16)]

    def set(self, x, y, c):
        if 0 <= x < 16 and 0 <= y < 16:
            self.p[y][x] = c

    def get(self, x, y):
        return self.p[y][x] if 0 <= x < 16 and 0 <= y < 16 else 0

    def ell(self, cx, cy, rx, ry, dark, mid, light, hi=None, outline=1):
        pix = []
        for y in range(16):
            for x in range(16):
                dx, dy = (x + .5 - cx) / rx, (y + .5 - cy) / ry
                if dx * dx + dy * dy <= 1:
                    pix.append((x, y, dx, dy))
        for x, y, dx, dy in pix:
            t = -0.6 * dx - 0.8 * dy
            c = mid
            if t > 0.55 and hi is not None:
                c = hi
            elif t > 0.2:
                c = light
            elif t < -0.35:
                c = dark
            self.set(x, y, c)
        if outline:
            for x, y, dx, dy in pix:
                for ax, ay in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if self.get(x + ax, y + ay) == 0:
                        self.set(x + ax, y + ay, outline)

    def line(self, x0, y0, x1, y1, c):
        n = max(abs(x1 - x0), abs(y1 - y0), 1)
        for i in range(n + 1):
            self.set(round(x0 + (x1 - x0) * i / n), round(y0 + (y1 - y0) * i / n), c)

    def shadow(self, y, x0, x1):
        self.shadows = getattr(self, "shadows", []) + [(x, y) for x in range(x0, x1) if (x + y) % 2 == 0]

    def finish(self):
        """outline the art, then add the ground shadow (a dither of the shadow slot, not outlined)"""
        self.outline_pass(1)
        for x, y in getattr(self, "shadows", []):
            if self.get(x, y) == 0:
                self.set(x, y, 15)
        return self.p

    def outline_pass(self, c=1):
        add = []
        for y in range(16):
            for x in range(16):
                if self.p[y][x] == 0:
                    if any(self.get(x + ax, y + ay) not in (0, c) for ax, ay in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                        add.append((x, y))
        for x, y in add:
            self.p[y][x] = c


def tree_oak(v):
    c = Canvas()
    if v == 0:
        c.shadow(15, 3, 13); c.shadow(14, 4, 12)
        for y in range(10, 15):
            c.set(7, y, 13); c.set(8, y, 15)
        c.ell(8, 6, 6, 5.5, 8, 9, 10, 11)
    elif v == 1:
        c.shadow(15, 3, 13); c.shadow(14, 4, 12)
        for y in range(9, 15):
            c.set(7, y, 13); c.set(8, y, 13); c.set(9, y, 15)
        c.ell(6, 6, 4.5, 4.5, 8, 9, 10, 11)
        c.ell(10, 5, 4.5, 4.5, 8, 9, 10, 11)
        c.ell(8, 8, 5, 3.5, 8, 9, 10, None, outline=0)
    else:
        c.shadow(15, 4, 12)
        for y in range(11, 15):
            c.set(7, y, 13); c.set(8, y, 15)
        c.ell(8, 8, 4.5, 4, 8, 9, 10, 11)
    return c.finish()


def tree_acacia(v):
    c = Canvas()
    c.shadow(15, 3, 13)
    if v == 2:   # baobab
        for y in range(6, 15):
            for x in range(6, 10):
                c.set(x, y, 13 if x < 8 else 15)
        c.ell(8, 5, 5, 2.5, 9, 10, 11, 12)
        c.line(5, 7, 4, 4, 13); c.line(11, 7, 12, 4, 13)
    else:
        w = 7 if v == 0 else 5.5
        for y in range(7, 15):
            c.set(8, y, 13); c.set(7 if y > 11 else 9, y, 15)
        c.line(8, 8, 5, 6, 13); c.line(8, 8, 11, 6, 13)
        c.ell(8, 5, w, 2.8, 9, 10, 11, 12)
        c.ell(5, 6, 3, 1.6, 9, 10, 10, None, outline=0) if v == 0 else None
    return c.finish()


def tree_pine(v):
    c = Canvas()
    c.shadow(15, 4, 12)
    for y in range(12, 15):
        c.set(7, y, 13); c.set(8, y, 15)
    snow = v == 2
    tiers = ((2, 7, 3), (5, 9, 4), (8, 11, 5)) if v != 1 else ((1, 5, 2), (3, 7, 3), (6, 9, 4), (9, 12, 5))
    for top, bot, hw in tiers:
        for y in range(top, bot + 1):
            half = max(0, round((y - top) / max(1, bot - top) * hw))
            for x in range(8 - half, 8 + half):
                t = (x - (8 - half)) / max(1, 2 * half)
                col = 9 if t < 0.35 else (8 if t > 0.7 else 9)
                if y == bot:
                    col = 8
                if t < 0.2:
                    col = 10
                if snow and (y - top) < 2 + (hw > 3) and t < 0.9:
                    col = 5 if (x + y) % 3 else 4
                if snow and y == bot and x % 2 == 0:
                    col = 4
                c.set(x, y, col)
    return c.finish()


def frond(c, tx, ty, sgn, L, R, D):
    """a palm frond: a curve rising by R and drooping by D over L pixels, three pixels thick"""
    for i in range(L * 2 + 1):
        t = i / (L * 2)
        x = round(tx + sgn * L * t)
        y = round(ty - R * t + D * t * t)
        c.set(x, y - 1, 11)
        c.set(x, y, 10)
        c.set(x, y + 1, 9)


def tree_palm(v):
    c = Canvas()
    c.shadow(15, 4, 12)
    if v == 2:    # broadleaf
        for y in range(10, 15):
            c.set(7, y, 13); c.set(8, y, 15)
        c.ell(8, 6, 6.5, 5.5, 8, 9, 10, 11)
        c.ell(11, 4, 3, 2.5, 8, 10, 11, 12, outline=0)
        c.ell(5, 8, 3, 2.5, 8, 9, 9, None, outline=0)
        return c.finish()
    s = 1 if v == 0 else -1
    pts = [(8, 15), (8, 13), (8 + s, 11), (8 + s, 9), (8 + 2 * s, 7), (8 + 2 * s, 5)]
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        c.line(x0, y0, x1, y1, 13)
        c.set(x1 + 1, y1, 15)
    tx, ty = 8 + 2 * s, 4
    for sg, L, R, D in ((-1, 7, 3, 8), (1, 7, 3, 8), (-1, 5, 6, 7), (1, 5, 6, 7), (-1, 3, 5, 3), (1, 3, 5, 3)):
        frond(c, tx, ty, sg, L, R, D)
    c.set(tx, ty + 1, 15); c.set(tx + 1, ty + 1, 15); c.set(tx - 1, ty + 1, 15)
    return c.finish()


def bush(v):
    c = Canvas()
    c.shadow(14, 3, 13)
    c.ell(8, 10.5, 5.5, 3.6, 8, 9, 10, 11)
    for (x, y) in ((5, 9), (8, 8), (10, 10), (7, 11), (11, 9)):
        c.set(x, y, 5 if v != 1 else 7)
    return c.finish()


def boulder(ore):
    c = Canvas()
    c.shadow(14, 3, 13)
    c.ell(8, 10, 6, 4, 15, 14, 4, 5)
    c.ell(11.5, 11.5, 3, 2.2, 15, 14, 14, None, outline=0)
    if ore:
        for (x, y) in ((6, 9), (9, 11), (8, 8)):
            c.set(x, y, 6); c.set(x + 1, y, 5)
    return c.finish()


def fish(f):
    c = Canvas()
    for (x, y) in ((4, 5), (10, 8), (6, 11)):
        x += f * 2 - 1
        c.set(x, y, 4); c.set(x + 1, y, 5); c.set(x + 2, y, 4)
        c.set(x - 1, y - 1 + f * 2, 4)
    return c.p


def build_objects():
    cells = []
    cells += [tree_oak(i) for i in range(3)]
    cells += [tree_acacia(i) for i in range(3)]
    cells += [tree_pine(i) for i in range(3)]
    cells += [tree_palm(i) for i in range(3)]
    cells += [bush(i) for i in range(4)]
    cells += [boulder(0), boulder(1), fish(0), fish(1)]
    while len(cells) % 8:
        cells.append(Canvas().p)
    return cells


# ------------------------------------------------------------------------------------------------ shaman (master palette)
class Pix:
    """16x16 canvas of master palette letters"""
    def __init__(self):
        self.p = [["."] * 16 for _ in range(16)]

    def span(self, y, x0, x1, ch):
        for x in range(x0, x1 + 1):
            self.put(x, y, ch)

    def put(self, x, y, ch):
        if 0 <= x < 16 and 0 <= y < 16:
            self.p[y][x] = ch

    def outline(self, ch="k"):
        add = []
        for y in range(16):
            for x in range(16):
                if self.p[y][x] == "." and any(0 <= x + dx < 16 and 0 <= y + dy < 16 and self.p[y + dy][x + dx] not in (".", ch)
                                               for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    add.append((x, y))
        for x, y in add:
            self.p[y][x] = ch

    def rows(self):
        return ["".join(r) for r in self.p]


def shaman_frame(facing, frame):
    """A robed figure with a staff. frame 0 stands, 1 and 2 step (a bob and one boot planted)."""
    c = Pix()
    bob = -1 if frame else 0
    side = facing == "left"
    dx = 2 if side else 0            # the body leans to the right when seen from the side
    sx = 3 if side else 13          # the staff is held in front: left of a left-facing figure
    # staff and orb
    for y in range(3 + bob, 15):
        c.put(sx, y, "B")
    c.span(1 + bob, sx - 1, sx + 1, "Y"); c.span(2 + bob, sx - 1, sx + 1, "Y"); c.put(sx - 1, 1 + bob, "o")
    # hood
    for y, (x0, x1) in zip(range(3, 10), ((5, 8), (4, 9), (3, 10), (3, 10), (3, 10), (3, 10), (4, 9))):
        c.span(y + bob, x0 + dx, x1 + dx, "r")
    # robe
    for y, (x0, x1) in zip(range(10, 15), ((3, 10), (2, 11), (2, 11), (1, 11), (1, 11))):
        c.span(y + bob, x0 + dx, x1 + dx, "r")
    c.span(12 + bob, 3 + dx, 10 + dx, "B")                       # belt
    for (x, y) in ((4, 13), (7, 14), (9, 13)):
        c.put(x + dx, y + bob, "b")                              # folds
    if facing == "down":
        c.span(6 + bob, 5, 8, "p"); c.span(7 + bob, 5, 8, "p"); c.span(8 + bob, 5, 8, "p")
        c.put(5, 7 + bob, "k"); c.put(8, 7 + bob, "k")
        c.span(9 + bob, 5, 8, "o"); c.span(10 + bob, 6, 7, "o")
        c.put(12, 11 + bob, "p"); c.put(12, 12 + bob, "p")
    elif facing == "up":
        c.put(12, 11 + bob, "p"); c.put(12, 12 + bob, "p")
        c.span(5 + bob, 5, 8, "r")
        for x in (5, 8):
            c.put(x, 8 + bob, "b")
    else:
        c.span(6 + bob, 5, 6, "p"); c.span(7 + bob, 5, 6, "p"); c.span(8 + bob, 5, 6, "p")
        c.put(5, 7 + bob, "k")
        c.span(9 + bob, 5, 6, "o"); c.put(5, 10 + bob, "o")
        c.put(4, 11 + bob, "p"); c.put(4, 12 + bob, "p")
    c.outline()
    # boots
    if frame == 0:
        for x in (4, 5, 8, 9):
            c.put(x + dx, 15, "b")
    else:
        for x in ((4, 5) if frame == 1 else (8, 9)):
            c.put(x + dx, 15, "b")
    return c.rows()


def px(rows):
    assert len(rows) == 16 and all(len(r) == 16 for r in rows), [len(r) for r in rows]
    return [[CHAR_TO_INDEX[ch] for ch in r] for r in rows]


def build_shaman():
    return [px(shaman_frame(f, fr)) for f in ("down", "up", "left") for fr in range(3)]


# ------------------------------------------------------------------------------------------------ hud icons (master palette)
HUD = [
    ["....r...", "...rr...", "..rrYr..", "..rYYr..", ".rrYYrr.", ".rYoYYr.", ".rYYYYr.", "..rrrr.."],   # faith: flame
    ["...Y.Y..", "..YYYYY.", "...YBY..", ".Y.YBY..", "YYYYBYY.", ".Y.lB...", "...lB...", "...bB..."],   # food: wheat
    ["........", "..bbbbb.", ".bBBBBBb", "bsoBBBBb", "bsBsBBBb", ".bbBBBBb", "..bbbbb.", "........"],   # wood: log
    ["..kkkk..", ".kppppk.", ".kppppk.", "..kppk..", ".kwwwwk.", "kwwwwwwk", ".kwwwwk.", ".kb..bk."],   # population: person
]


def build_hud():
    out = []
    for rows in HUD:
        assert len(rows) == 8 and all(len(r) == 8 for r in rows)
        out.append([[CHAR_TO_INDEX[ch] for ch in r] for r in rows])
    return out


# ------------------------------------------------------------------------------------------------ village (world palette)
class Big:
    """Pixel canvas of any size in world palette indices; 0 = transparent."""
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.p = [[0] * w for _ in range(h)]

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.p[y][x] = c

    def get(self, x, y):
        return self.p[y][x] if 0 <= x < self.w and 0 <= y < self.h else 0

    def rect(self, x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, c)

    def line(self, x0, y0, x1, y1, c):
        n = max(abs(x1 - x0), abs(y1 - y0), 1)
        for i in range(n + 1):
            self.set(round(x0 + (x1 - x0) * i / n), round(y0 + (y1 - y0) * i / n), c)

    def outline(self, c=1):
        add = []
        for y in range(self.h):
            for x in range(self.w):
                if self.p[y][x] == 0 and any(self.get(x + dx, y + dy) not in (0, c) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    add.append((x, y))
        for x, y in add:
            self.p[y][x] = c

    def cell(self, cx, cy):
        return [row[cx * 16:(cx + 1) * 16] for row in self.p[cy * 16:(cy + 1) * 16]]


def b_tent(site=False):
    c = Big(16, 16)
    if site:   # building site: four stakes, a rope and some planks on the ground
        for (x, y) in ((2, 5), (13, 5), (2, 13), (13, 13)):
            c.rect(x, y - 3, x, y + 1, 13)
            c.set(x, y - 3, 7)
        c.line(2, 5, 13, 5, 7); c.line(2, 13, 13, 13, 7); c.line(2, 5, 2, 13, 7); c.line(13, 5, 13, 13, 7)
        c.rect(5, 9, 10, 9, 13); c.rect(6, 11, 11, 11, 7)
        c.outline(1)
        return c.p
    for y in range(3, 14):                       # a hide tent: a triangle of cloth with a dark opening
        half = (y - 3) * 6 // 10 + 1
        for x in range(8 - half, 8 + half):
            c.set(x, y, 6 if x < 8 else 7)
    c.rect(7, 9, 8, 13, 15)
    c.set(7, 8, 15); c.set(8, 8, 15)
    c.line(8, 2, 8, 3, 13)
    for y in range(4, 13, 3):                    # seams
        c.set(8 - (y - 3) * 6 // 10, y, 7)
        c.set(7 + (y - 3) * 6 // 10, y, 15)
    c.outline(1)
    for x in range(3, 13):
        if (x) % 2 == 0 and c.get(x, 15) == 0:
            c.set(x, 15, 15)
    return c.p


def b_well():
    c = Big(16, 16)
    c.rect(2, 8, 13, 13, 14)                     # stone ring
    c.rect(2, 8, 13, 9, 4)
    c.rect(3, 9, 12, 10, 3)                      # water
    for x in range(2, 14):
        c.set(x, 12, 15); c.set(x, 13, 15)
    for x in (2, 5, 8, 11):
        c.set(x, 11, 15)
    c.rect(3, 2, 3, 8, 13); c.rect(12, 2, 12, 8, 13)     # posts and roof
    c.rect(2, 1, 13, 2, 7); c.rect(2, 1, 13, 1, 6)
    c.line(8, 2, 8, 5, 15); c.rect(7, 5, 9, 6, 13)       # rope and bucket
    c.outline(1)
    return c.p


def b_hut():
    c = Big(32, 32)
    c.rect(5, 17, 26, 28, 13)                    # clay walls
    for x in range(5, 27):
        c.set(x, 28, 15)
    for y in range(17, 29):
        c.set(26, y, 15); c.set(25, y, 15)
    for y in range(17, 29, 3):
        for x in range(6, 25, 5):
            c.set(x + (y // 3) % 2 * 2, y, 15)   # daub pattern
    c.rect(13, 20, 18, 28, 15)                   # door
    c.rect(14, 21, 17, 28, 1)
    c.rect(7, 20, 10, 23, 15); c.rect(8, 21, 9, 22, 4)   # window
    for y in range(3, 18):                       # thatched roof, wider than the walls
        half = 5 + (y - 3) * 11 // 14
        for x in range(16 - half, 16 + half + 1):
            col = 6 if x < 16 else 7
            if (x + y) % 3 == 0:
                col = 7 if x < 16 else 15
            c.set(x, y, col)
    for x in range(1, 31):
        if c.get(x, 17) != 0:
            c.set(x, 17, 15)
    c.rect(15, 1, 16, 3, 13)                     # ridge pole
    c.outline(1)
    for x in range(5, 28):
        if (x % 2 == 0) and c.get(x, 31) == 0:
            c.set(x, 31, 15)
    return c


def b_field(stage):
    c = Big(16, 16)
    c.rect(0, 0, 15, 15, 13)
    for y in (1, 5, 9, 13):
        c.rect(0, y, 15, y, 15)                  # furrows
        c.rect(0, y + 1, 15, y + 1, 7 if stage == 0 else 13)
    if stage == 0:
        for (x, y) in ((3, 3), (9, 7), (12, 11), (5, 14), (1, 8)):
            c.set(x, y, 7)
    elif stage == 1:
        for y in (2, 6, 10, 14):
            for x in range(1, 16, 3):
                c.set(x, y - 1, 10); c.set(x, y, 11)
                c.set(x + 1, y, 9)
    else:
        for y in (2, 6, 10, 14):
            for x in range(1, 16, 3):
                c.rect(x, y - 3, x, y, 6)
                c.set(x, y - 3, 5); c.set(x + 1, y - 2, 7); c.set(x - 1, y - 1, 7); c.set(x, y - 4 if y > 3 else y - 3, 6)
    return c.p


def b_fish():
    c = Big(32, 16)
    c.rect(2, 7, 12, 13, 13)                     # hut
    c.rect(2, 7, 12, 7, 15)
    for y in range(2, 8):
        half = 2 + (7 - y) * 0 + (y - 2) * 5 // 5
        for x in range(7 - half - 2, 7 + half + 3):
            c.set(x, y, 6 if x < 7 else 7)
    c.rect(6, 9, 8, 13, 15)
    c.rect(13, 12, 31, 13, 13)                   # pier
    for x in range(13, 32, 4):
        c.rect(x, 13, x, 15, 15)
    for x in range(14, 31, 4):
        c.set(x, 12, 7)
    c.line(24, 12, 28, 6, 15)                    # rod and a drying net
    c.line(28, 6, 28, 10, 4)
    c.rect(14, 8, 19, 11, 7)
    for x in range(14, 20, 2):
        c.set(x, 8, 5); c.set(x + 1, 10, 5)
    c.outline(1)
    return c


def build_village():
    cells = [b_tent(), b_tent(True), b_well()]
    hut = b_hut()
    cells += [hut.cell(0, 0), hut.cell(1, 0), hut.cell(0, 1), hut.cell(1, 1)]
    cells += [b_field(0), b_field(1), b_field(2)]
    fish = b_fish()
    cells += [fish.cell(0, 0), fish.cell(1, 0)]
    while len(cells) % 8:
        cells.append(Big(16, 16).p)
    return cells


# ------------------------------------------------------------------------------------------------ villagers (master palette)
def villager_frame(facing, frame, child=False):
    """facing down / up / left. 'W' is the tunic and 'b' the hair: the game recolours them."""
    c = Pix()
    top = 6 if child else 2
    side = facing == "left"
    dx = 1 if side else 0
    hh = 5
    # head
    c.span(top, 5 + dx, 9 + dx, "b"); c.span(top + 1, 4 + dx, 10 + dx, "b")
    for y in range(top + 2, top + 5):
        c.span(y, 5 + dx, 9 + dx, "p")
    if facing == "down":
        c.put(6, top + 3, "k"); c.put(8, top + 3, "k")
    elif facing == "up":
        c.span(top + 2, 4, 10, "b"); c.span(top + 3, 5, 9, "b")
    else:
        c.put(5, top + 3, "k"); c.span(top + 2, 8 + dx, 10 + dx, "b")
    # body
    by = top + 5
    bh = 3 if child else 5
    for y in range(by, by + bh):
        c.span(y, 4 + dx, 10 + dx, "W")
    c.span(by + bh - 2, 4 + dx, 10 + dx, "B")
    c.put(3 + dx, by + 1, "p"); c.put(11 + dx, by + 1, "p")       # hands
    # legs
    ly = by + bh
    legs = 15 - ly + 1
    lf = ((0, 0), (1, 0), (0, 1))[frame]
    for x0, off in ((5 + dx, lf[0]), (8 + dx, lf[1])):
        for y in range(ly, 16 - (0 if not off else 1)):
            c.span(y, x0, x0 + 1, "b")
    c.outline()
    return c.rows()


def build_villagers():
    cells = [px(villager_frame(f, fr)) for f in ("down", "up", "left") for fr in range(3)]
    cells += [px(villager_frame("down", fr, True)) for fr in range(3)]
    return cells


TOOLS = [
    ["......kk", ".....kBk", "....kB.k", "...kb...", "..kb....", ".kb.....", "kb......", "b......."],     # hoe
    ["..kkk...", ".kBBBk..", ".kB.kBk.", "...kBBk.", "...kB...", "...kB...", "...kB...", "...kB..."],     # crook
    ["........", "......kb", ".....kb.", "....kb.f", "...kb..f", "..kb...f", ".kb....W", "kb......"],        # fishing rod
    ["..kkk...", ".kaaak..", ".kaaBk..", "..kkBk..", "....Bk..", "...Bk...", "..Bk....", ".Bk....."],       # axe
    [".kkkkk..", ".kaaak..", ".kaaak..", ".kkBkk..", "...Bk...", "...Bk...", "...Bk...", "...kk..."],       # hammer
    ["........", "..l.l...", ".lGlGl..", "..GGG...", "...G....", "..lGl...", "...G....", "...b...."],        # herb bundle
    ["...kk...", "..k..k..", "..k..k..", ".kWWWWk.", ".kWWWWk.", ".kWfWWk.", "..kWWk..", "...kk..."],       # bucket
    ["........"] * 8,
]


def build_tools():
    out = []
    for rows in TOOLS:
        assert len(rows) == 8 and all(len(r) == 8 for r in rows)
        out.append([[CHAR_TO_INDEX[ch] if ch != "." else 0 for ch in r] for r in rows])
    return out


def main():
    os.makedirs(SRC, exist_ok=True)
    arts, an_count = build_terrain()
    save_sheet(os.path.join(SRC, "terrain.png"), [g for _, g in arts], 8, 8, 16, [rgb for _, rgb in ROLES])
    write_ids(arts, an_count)
    save_sheet(os.path.join(SRC, "objects.png"), build_objects(), 16, 16, 8, WORLD_REF)
    save_sheet(os.path.join(SRC, "shaman.png"), build_shaman(), 16, 16, 8, [rgb for _, _, rgb in PALETTE])
    save_sheet(os.path.join(SRC, "hud.png"), build_hud(), 8, 8, 4, [rgb for _, _, rgb in PALETTE])
    save_sheet(os.path.join(SRC, "village.png"), build_village(), 16, 16, 8, WORLD_REF)
    save_sheet(os.path.join(SRC, "villagers.png"), build_villagers(), 16, 16, 8, [rgb for _, _, rgb in PALETTE])
    save_sheet(os.path.join(SRC, "tools.png"), build_tools(), 8, 8, 8, [rgb for _, _, rgb in PALETTE])
    print(f"wrote terrain.png ({len(arts)} quarters, {an_count} animated), objects, shaman, hud, village, villagers, tools, world_art_ids.h")


if __name__ == "__main__":
    main()
