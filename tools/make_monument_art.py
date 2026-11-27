"""Paints the r6 monuments: five types (pyramid, stone circle, great tree, obelisk, temple) in five build stages, 3x3 tiles each
(48x48 pixels, world palette slots, so every climate tints them).

    python tools/make_monument_art.py       # writes assets/src/monument.png and lib/gamecore/src/monument_art_ids.h
    python tools/png2tiles.py               # then converts the sheet into lib/gamecore/src/assets_gen.h
    python tools/make_monument_art.py --preview out.png     # contact sheet, 4x

Stage 1 (the foundation: levelled ground, corner stones, stakes and rope) is the same for every type, so the sheet holds
9 + 5 types x 4 stages x 9 = 189 cells of 16x16. Stage 0 (pegged ground) is the building site art of village.png.
Cell of tile (i, j) of the monument of type t at stage s (2..5): MA_BASE + (t * 4 + s - 2) * 9 + j * 3 + i.
"""
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
from palette import WORLD_REF
from make_world_art import Big, save_sheet

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = os.path.join(ROOT, "assets", "src")
IDS = os.path.join(ROOT, "lib", "gamecore", "src", "monument_art_ids.h")

# world palette slots
OUT, PALE, WHITE, SAND, SAND_D, G0, G1, G2, G3, G4, EARTH, ROCK, SHADOW = 1, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
W = 48
BASE = 46          # the ground line


def hash2(x, y, s=0):
    h = (x * 73856093) ^ (y * 19349663) ^ (s * 83492791)
    h = (h ^ (h >> 13)) * 1274126177
    return (h ^ (h >> 16)) & 0xFFFF


def ground(c, hw=22, h=6):
    """a patch of trampled earth under the monument"""
    for y in range(BASE - h, BASE + 1):
        t = (y - (BASE - h)) / max(h, 1)
        half = int(hw * (0.78 + 0.22 * (1 - abs(t - 0.5) * 2)) + 0.5)
        for x in range(24 - half, 24 + half):
            c.set(x, y, EARTH if hash2(x, y) % 5 else SAND_D)
    for x in range(24 - hw + 3, 24 + hw - 2):
        if hash2(x, 7) % 3 == 0:
            c.set(x, BASE, SHADOW)


def stone_block(c, x0, y0, x1, y1, light=PALE, mid=ROCK, dark=SHADOW):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            col = mid
            if x == x0 or y == y0:
                col = light
            elif x == x1 or y == y1:
                col = dark
            c.set(x, y, col)


def blob(c, cx, cy, rx, ry, dark, mid, light, hi=None):
    for y in range(int(cy - ry - 1), int(cy + ry + 2)):
        for x in range(int(cx - rx - 1), int(cx + rx + 2)):
            dx, dy = (x + .5 - cx) / rx, (y + .5 - cy) / ry
            if dx * dx + dy * dy <= 1:
                t = -0.6 * dx - 0.8 * dy
                col = mid
                if t > 0.6 and hi is not None:
                    col = hi
                elif t > 0.25:
                    col = light
                elif t < -0.35:
                    col = dark
                if hash2(x, y, 3) % 11 == 0:
                    col = dark if col != dark else mid
                c.set(x, y, col)


# ------------------------------------------------------------------------------------------------ stage 1, the same for all
def foundation():
    c = Big(W, W)
    for y in range(22, BASE + 1):
        t = (y - 22) / (BASE - 22)
        half = 20 + int(3 * t)
        for x in range(24 - half, 24 + half):
            c.set(x, y, EARTH if hash2(x, y, 1) % 4 else SAND_D)
    for x in range(4, 44):                               # a rim of lighter, packed earth and the shadow at the front
        c.set(x, 23, SAND)
        if hash2(x, 1) % 2 == 0:
            c.set(x, BASE, SHADOW)
    for (x, y) in ((5, 24), (40, 24), (3, 42), (42, 42)):   # corner stones
        stone_block(c, x, y, x + 3, y + 3)
    for (x, y) in ((8, 30), (36, 33), (17, 40)):         # stakes with a rope between them
        c.rect(x, y - 5, x, y, 13)
        c.set(x, y - 5, SAND)
    c.line(8, 25, 36, 28, SAND); c.line(36, 28, 17, 35, SAND)
    stone_block(c, 20, 32, 26, 36)                       # a first block and a heap of stones
    stone_block(c, 28, 36, 32, 39)
    c.outline(OUT)
    return c


# ------------------------------------------------------------------------------------------------ pyramid
TIERS = [(21, 8), (17, 8), (13, 8), (9, 7), (5, 6)]      # half width, height; the bottom tier first


def pyramid(stage):
    c = Big(W, W)
    ground(c, 23, 5)
    y = BASE - 3
    n = stage - 1                                         # stage 2: one tier ... stage 5: four tiers + the capstone
    top = None
    for t in range(min(n + (1 if stage == 5 else 0), len(TIERS))):
        hw, h = TIERS[t]
        for yy in range(y - h + 1, y + 1):
            for x in range(24 - hw, 24 + hw):
                col = SAND if x < 24 + 1 else SAND_D
                if yy == y - h + 1:
                    col = PALE if x < 25 else SAND
                elif yy == y:
                    col = SAND_D if x < 25 else SHADOW
                elif (x + yy * 2) % 9 == 0 and hw > 6:
                    col = SAND_D if x < 25 else SHADOW   # mortar joints
                c.set(x, yy, col)
        top = y - h + 1
        y -= h
    if stage == 5:
        hw, h = TIERS[4]
        c.rect(23, top - 1, 24, top - 1, WHITE)          # capstone
        c.rect(22, top, 25, top, PALE)
        for yy in range(top + 1, BASE - 6, 1):           # the stair up the front
            for x in range(22, 26):
                c.set(x, yy, PALE if yy % 2 == 0 else SAND_D)
        c.rect(21, BASE - 6, 26, BASE - 3, SHADOW)       # the door at the foot
        c.rect(22, BASE - 5, 25, BASE - 3, OUT)
    if stage == 4:                                        # scaffold and a ramp of earth on one side
        c.line(38, BASE - 4, 28, y + 2, EARTH)
        c.line(38, BASE - 3, 29, y + 3, SAND_D)
        c.rect(10, y - 6, 10, y + 2, EARTH); c.rect(14, y - 4, 14, y + 2, EARTH); c.line(10, y - 6, 14, y - 4, SAND)
    c.outline(OUT)
    return c


# ------------------------------------------------------------------------------------------------ stone circle
ANGLES = [(0, -1), (7, 1), (-14, 4), (14, 4), (-19, 12), (19, 12), (-9, 15), (9, 15)]     # dx, dy from the middle of the ring


def standing(c, cx, base, w, h, cap=False):
    for y in range(base - h + 1, base + 1):
        for x in range(cx - w // 2, cx - w // 2 + w):
            col = ROCK
            if x == cx - w // 2:
                col = PALE
            elif x == cx - w // 2 + w - 1:
                col = SHADOW
            if y == base - h + 1:
                col = WHITE if x < cx else PALE
            elif y == base:
                col = SHADOW
            c.set(x, y, col)
    c.set(cx - 1, base - h // 2, SHADOW)
    c.set(cx, base - h // 2 - 3, SHADOW)


def circle(stage):
    c = Big(W, W)
    ground(c, 23, 11)
    order = [0, 5, 3, 6, 1, 4, 2, 7]
    count = {2: 3, 3: 5, 4: 7, 5: 8}[stage]
    present = set(order[:count])
    stones = sorted(((24 + dx, 33 + dy), k) for k, (dx, dy) in enumerate(ANGLES))
    stones.sort(key=lambda s: s[0][1])
    for (x, y), k in stones:
        if k in present:
            h = 11 + (y - 32) // 3
            standing(c, x, y, 6, h)
    if stage == 5:
        # lintels between the two front pairs and a glowing altar in the middle
        for (a, b) in ((2, 0), (3, 1)):
            pass
        stone_block(c, 13, 17, 28, 19, PALE, ROCK, SHADOW)         # a lintel across the back pair
        c.rect(21, 30, 26, 35, ROCK); c.rect(21, 30, 26, 30, WHITE); c.rect(21, 35, 26, 35, SHADOW)
        c.rect(23, 28, 24, 29, WHITE)
        c.set(22, 27, PALE); c.set(25, 27, PALE); c.set(23, 26, WHITE); c.set(24, 26, WHITE)
    c.outline(OUT)
    return c


# ------------------------------------------------------------------------------------------------ great tree
def tree(stage):
    c = Big(W, W)
    ground(c, 20, 5)
    tw, th, rx, ry, cy = {2: (2, 9, 5, 4, 30), 3: (4, 15, 9, 7, 25), 4: (6, 21, 15, 11, 18), 5: (8, 25, 21, 14, 14)}[stage]
    x0 = 24 - tw // 2
    for y in range(BASE - 2 - th, BASE - 1):             # the trunk
        flare = max(0, (y - (BASE - 8)) // 2) if stage >= 4 else 0
        for x in range(x0 - flare, x0 + tw + flare):
            col = EARTH if x < x0 + tw // 2 + 1 else SHADOW
            if x == x0 - flare:
                col = SAND_D
            c.set(x, y, col)
    if stage >= 4:                                       # roots
        c.line(x0 - 3, BASE - 2, x0 - 7, BASE, EARTH); c.line(x0 + tw + 2, BASE - 2, x0 + tw + 6, BASE, EARTH)
    if stage == 5:
        c.line(x0 + 1, BASE - 12, x0 - 7, BASE - 20, EARTH); c.line(x0 + tw - 2, BASE - 12, x0 + tw + 6, BASE - 20, EARTH)
    # the crown: overlapping blobs, lit from the upper left
    blob(c, 24 - rx // 2, cy + ry // 3, rx * .7, ry * .8, G0, G1, G2)
    blob(c, 24 + rx // 2, cy + ry // 3, rx * .7, ry * .8, G0, G1, G2)
    blob(c, 24, cy, rx, ry, G0, G2, G3, G4)
    if stage == 5:
        blob(c, 24 - 9, cy - 4, 8, 6, G1, G2, G3, G4)
        blob(c, 24 + 8, cy - 5, 8, 6, G1, G2, G3, G4)
        blob(c, 24, cy - 8, 8, 5, G2, G3, G4, G4)
        for k in range(9):                               # white blossoms
            x = 6 + hash2(k, 5) % 36
            y = 3 + hash2(k, 6) % 22
            if c.get(x, y) in (G1, G2, G3, G4):
                c.set(x, y, WHITE); c.set(x + 1, y, PALE)
    c.outline(OUT)
    return c


# ------------------------------------------------------------------------------------------------ obelisk
def obelisk(stage):
    c = Big(W, W)
    ground(c, 18, 5)
    stone_block(c, 10, BASE - 5, 37, BASE - 1, PALE, ROCK, SHADOW)          # the plinth, two steps
    stone_block(c, 14, BASE - 9, 33, BASE - 5, PALE, ROCK, SHADOW)
    shaft = {2: 5, 3: 17, 4: 27, 5: 33}[stage]
    y1 = BASE - 9
    for k in range(shaft):
        y = y1 - k
        half = 5 - (k * 2) // max(shaft, 12) if stage < 5 else 5 - (k * 2) // 36
        for x in range(24 - half, 24 + half):
            col = ROCK
            if x == 24 - half:
                col = PALE
            elif x >= 24 + half - 2:
                col = SHADOW
            elif x < 24 - half + 2:
                col = PALE if k % 2 == 0 else ROCK
            c.set(x, y, col)
        if stage >= 4 and k % 7 == 3:
            c.rect(24 - half + 2, y, 24 + half - 3, y, SHADOW)               # carved bands
        if stage >= 4 and k % 7 == 5:
            c.set(23, y, SHADOW); c.set(25, y, SAND_D)
    if stage == 5:
        top = y1 - shaft
        for k in range(5):                               # the pyramidion
            half = 4 - k if k < 4 else 0
            for x in range(24 - half, 24 + half):
                c.set(x, top - k, WHITE if x < 24 else PALE)
        c.set(23, top - 5, WHITE); c.set(24, top - 5, PALE)
        for (x, y) in ((20, top - 3), (28, top - 4), (24, top - 8)):
            c.set(x, y, WHITE)
    elif stage >= 3:
        top = y1 - shaft
        c.rect(22, top - 1, 25, top, ROCK)                # a rough top: it is still being cut
    if stage in (3, 4):
        c.rect(36, BASE - 14, 36, BASE - 10, EARTH); c.line(36, BASE - 14, 30, y1 - shaft // 2, SAND_D)    # a lifting pole
    c.outline(OUT)
    return c


# ------------------------------------------------------------------------------------------------ temple
def temple(stage):
    c = Big(W, W)
    ground(c, 23, 4)
    stone_block(c, 3, BASE - 3, 44, BASE - 1, PALE, PALE, ROCK)          # three steps
    stone_block(c, 5, BASE - 5, 42, BASE - 3, WHITE, PALE, ROCK)
    stone_block(c, 7, BASE - 7, 40, BASE - 5, WHITE, PALE, ROCK)
    floor = BASE - 7
    col_h = 14
    if stage >= 3:
        xs = [10, 17, 26, 33] if stage < 5 else [9, 15, 21, 27, 33, 38]
        if stage == 3:
            xs = [10, 33]
        for x in xs:
            for y in range(floor - col_h, floor):
                for k in range(4):
                    col = PALE if k == 0 else (SHADOW if k == 3 else WHITE if k == 1 else PALE)
                    if stage == 3 and y < floor - 9:
                        continue                                      # the columns are still going up
                    c.set(x + k, y, col)
            if stage > 3:
                c.rect(x - 1, floor - col_h, x + 4, floor - col_h, PALE)     # capital
                c.rect(x - 1, floor - 1, x + 4, floor - 1, PALE)             # base
    if stage >= 4:
        stone_block(c, 7, floor - col_h - 4, 40, floor - col_h - 1, WHITE, PALE, ROCK)     # entablature
        c.rect(23, floor - col_h, 25, floor - 1, SHADOW)                                   # the doorway
        c.rect(22, floor - 6, 26, floor - 1, SHADOW)
    if stage == 5:
        top = floor - col_h - 4
        for k in range(10):                                           # the pediment: a roof of terracotta tiles
            half = 21 - k * 2
            for x in range(24 - half, 24 + half):
                col = EARTH if k % 2 == 0 else SAND_D
                if x == 24 - half:
                    col = SAND
                c.set(x, top - 1 - k, col)
        c.rect(22, top - 12, 25, top - 11, WHITE)                     # the finial
        c.set(23, top - 13, PALE); c.set(24, top - 13, WHITE)
        for x in range(12, 36, 4):
            c.set(x, top - 3, SHADOW)
    c.outline(OUT)
    return c


TYPES = [pyramid, circle, tree, obelisk, temple]


def build():
    cells = []
    f = foundation()
    cells += [f.cell(i, j) for j in range(3) for i in range(3)]
    for t in TYPES:
        for stage in range(2, 6):
            img = t(stage)
            cells += [img.cell(i, j) for j in range(3) for i in range(3)]
    while len(cells) % 8:
        cells.append(Big(16, 16).p)
    return cells


def main():
    cells = build()
    save_sheet(os.path.join(SRC, "monument.png"), cells, 16, 16, 8, WORLD_REF)
    with open(IDS, "w") as fh:
        fh.write("// GENERATED by tools/make_monument_art.py - do not edit by hand.\n"
                 "// assets/src/monument.png (GC_MONUMENT(id)): stage 1 is MA_STAGE1 + j * 3 + i; stage s (2..5) of type t is\n"
                 "// MA_BASE + (t * 4 + s - 2) * 9 + j * 3 + i.\n#pragma once\nenum { MA_STAGE1 = 0, MA_BASE = 9, MA_COUNT = %d };\n" % len(cells))
    print("wrote monument.png,", len(cells), "cells")


def preview(path):
    img = Image.new("RGB", (W * 5 * 2 + 40, W * 5 + 20), (96, 150, 70))
    sheets = [foundation()] + [t(s) for t in TYPES for s in (2, 3, 4, 5)]
    # rows: foundation, then one row per type with 4 stages
    big = Image.new("RGB", (W * 5, W * 6), (96, 150, 70))
    rows = [[foundation()] + [pyramid(s) for s in (2, 3, 4, 5)]]
    for t in TYPES[1:]:
        rows.append([foundation()] + [t(s) for s in (2, 3, 4, 5)])
    rows[0][0] = foundation()
    rows = [[foundation()] + [t(s) for s in (2, 3, 4, 5)] for t in TYPES]
    big = Image.new("RGB", (W * 5, W * 5), (96, 150, 70))
    for r, row in enumerate(rows):
        for k, c in enumerate(row):
            for y in range(W):
                for x in range(W):
                    v = c.p[y][x]
                    if v:
                        big.putpixel((k * W + x, r * W + y), WORLD_REF[v])
    big = big.resize((W * 5 * 3, W * 5 * 3), Image.NEAREST)
    big.save(path)


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--preview":
        preview(sys.argv[2])
    else:
        main()
