"""Art tool tests: the generators are deterministic, every pixel is a valid palette entry, the committed PNG sheets and
lib/gamecore/src/assets_gen.h / world_art_ids.h are what the tools produce today, and the palettes are sane.

    python3 test/test_art_tools.py
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

from PIL import Image  # noqa: E402

import make_world_art as art  # noqa: E402
import palette  # noqa: E402


def check(cond, msg):
    if not cond:
        print("FAIL:", msg)
        sys.exit(1)


def sheet_pixels(path, cw, ch, table):
    img = Image.open(path).convert("RGB")
    cols = img.width // cw
    cells = []
    for i in range((img.width // cw) * (img.height // ch)):
        ox, oy = (i % cols) * cw, (i // cols) * ch
        cells.append([[table[img.getpixel((ox + x, oy + y))] for x in range(cw)] for y in range(ch)])
    return cells


def test_palettes():
    check(palette.CLIMATES == ["temperate", "savanna", "northern", "tropical"], "climate list")
    for name in palette.CLIMATES:
        p = palette.WORLD_PALETTES[name]
        check(len(p) == 16 and p[0] == (255, 0, 255), f"{name}: 16 entries, slot 0 transparent")
        check(len(set(p)) == 16, f"{name}: palette entries must be distinct")
        for rgb in p:
            check(all(0 <= v <= 255 for v in rgb), f"{name}: rgb range")
    check(len(set(rgb for _, rgb in palette.ROLES)) == 16, "role palette entries distinct")
    check(len(palette.PALETTE) == 16 and len(set(rgb for _, _, rgb in palette.PALETTE)) == 16, "master palette distinct")


def test_generators():
    a1, an1 = art.build_terrain()
    a2, an2 = art.build_terrain()
    check(an1 == an2 and [g for _, g in a1] == [g for _, g in a2], "terrain generator is deterministic")
    check(len(set(n for n, _ in a1)) == len(a1), "terrain art names are unique")
    for name, g in a1:
        check(len(g) == 8 and all(len(r) == 8 for r in g), f"{name}: 8x8")
        check(all(1 <= v <= 13 for r in g for v in r), f"{name}: only roles 1..13 are painted")
    check(art.build_objects() == art.build_objects(), "objects deterministic")
    check(art.build_shaman() == art.build_shaman(), "shaman deterministic")
    for cell in art.build_objects():
        check(all(0 <= v < 16 for r in cell for v in r), "object palette index")
    check(len(art.build_shaman()) == 15 and len(art.build_hud()) == 4, "shaman 15 frames (9 walking, 6 casting), 4 hud icons")


def test_committed_sheets_match():
    src = os.path.join(ROOT, "assets", "src")
    arts, _ = art.build_terrain()
    check(sheet_pixels(os.path.join(src, "terrain.png"), 8, 8, palette.ROLE_RGB_TO_INDEX)[:len(arts)] == [g for _, g in arts],
          "assets/src/terrain.png is stale: run python tools/make_world_art.py")
    objs = art.build_objects()
    check(sheet_pixels(os.path.join(src, "objects.png"), 16, 16, palette.WORLD_RGB_TO_INDEX)[:len(objs)] == objs,
          "assets/src/objects.png is stale")
    shaman = art.build_shaman()
    check(sheet_pixels(os.path.join(src, "shaman.png"), 16, 16, palette.RGB_TO_INDEX)[:len(shaman)] == shaman,
          "assets/src/shaman.png is stale")
    hud = art.build_hud()
    check(sheet_pixels(os.path.join(src, "hud.png"), 8, 8, palette.RGB_TO_INDEX)[:len(hud)] == hud, "assets/src/hud.png is stale")
    vcells, vids = art.build_village()
    check(sheet_pixels(os.path.join(src, "village.png"), 16, 16, palette.WORLD_RGB_TO_INDEX)[:len(vcells)] == vcells,
          "assets/src/village.png is stale: run python tools/make_world_art.py")
    ids = open(os.path.join(ROOT, "lib", "gamecore", "src", "village_art_ids.h")).read()
    for k, v in vids.items():
        check(f"VA_{k} = {v}," in ids, f"village_art_ids.h is stale ({k})")


def test_ids_header():
    text = open(os.path.join(ROOT, "lib", "gamecore", "src", "world_art_ids.h")).read()
    arts, an = art.build_terrain()
    ids = dict((k, int(v)) for k, v in re.findall(r"(ART_\w+) = (\d+)", text))
    check(ids["ART_COUNT"] == len(arts) and ids["ART_AN_COUNT"] == an, "world_art_ids.h is stale")
    names = [n for n, _ in arts]
    for key, first in (("ART_FILL_WATER", "FILL_WATER_0_0"), ("ART_SHORE_LOW", "SHORE_LOW_EH_0"), ("ART_UPN", "UPN_EH"),
                       ("ART_LON", "LON_FACE"), ("ART_LOS", "LOS_SHADH")):
        check(ids[key] == names.index(first), f"{key} points at {first}")
    check(all(i % 2 == 0 for i in (ids["ART_WATER_UP"], ids["ART_WATER_LOW"], ids["ART_SHORE_UP"], ids["ART_SHORE_LOW"])),
          "animated groups start on even ids (frame = id | 1)")


def test_png2tiles():
    with tempfile.TemporaryDirectory() as d:
        outs = []
        for i in range(2):
            out = os.path.join(d, f"a{i}.h")
            subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "png2tiles.py")],
                                  env=dict(os.environ, PNG2TILES_OUT=out), stdout=subprocess.DEVNULL)
            outs.append(open(out).read())
        check(outs[0] == outs[1], "png2tiles is deterministic")
        committed = open(os.path.join(ROOT, "lib", "gamecore", "src", "assets_gen.h")).read()
        check(outs[0] == committed, "assets_gen.h is stale: run python tools/png2tiles.py")
        check(outs[0].count("GC_WPAL") >= 2 and "GC_QUARTS_CELLS" in outs[0], "world tables are generated")
        # art budget: r1 carried 37.7 KB; r2 added about 10 KB; r3 the village, villager and tool sheets (about 4 KB); r4 the buildings
        # up to 48x48, pen, orchard, construction and cast frames (about 6 KB); r6 the monument, 5 types x 5 stages of 3x3 cells (105 distinct cells
        # and 674 new rows: about 9 KB). The sum below counts declarations and definitions, so it is twice the bytes in flash.
        size = 0
        for m in re.finditer(r"const (?:uint8_t|uint16_t) (GC_\w+)\[([\d\]\[]+)\]", outs[0]):
            dims = [int(x) for x in re.findall(r"\d+", m.group(2))]
            n = 1
            for x in dims:
                n *= x
            size += n * (2 if "uint16_t" in m.group(0) else 1)
        check(size < 84 * 1024, f"art tables are {size} bytes (declarations and definitions), over the budget (about 41 KB in flash)")


if __name__ == "__main__":
    test_palettes()
    test_generators()
    test_committed_sheets_match()
    test_ids_header()
    test_png2tiles()
    print("test_art_tools: all passed")
