"""Builds tools/shots.cpp with the real game core and writes the README screenshots to docs/screenshots/.

    python tools/make_screenshots.py            # needs g++ and Pillow (add --r6, --r7 or --r8 for the pictures of one release only)
Each landform gets an overview of the whole 512x512 map (the camera moved over it) and the 240x240 screen of the clock.
"""
import os
import subprocess
import sys
from PIL import Image

ROOT = os.path.join(os.path.dirname(__file__), "..")
OUT = os.path.join(ROOT, "docs", "screenshots")
EXE = os.path.join(os.environ.get("TMPDIR", "/tmp"), "mg_shots.bin")
LANDFORMS = ["island", "coast", "river-valley", "lake-basin", "archipelago", "mountain-plateau"]
CLIMATES = ["temperate", "savanna", "northern", "tropical"]
# (landform, climate, seed) of the README gallery: every landform once, every climate at least once
GALLERY = [(0, 0, 11), (1, 3, 5), (2, 1, 21), (3, 2, 7), (4, 3, 33), (5, 2, 41)]
# r4: the town on day 20 of an unaided world (landform, climate, seed)
TOWNS = [(0, 0, 11), (2, 3, 21), (3, 1, 7), (5, 2, 41)]


# r5: (name, seed, landform, climate, minutes, mode, boost, tokens)
R5_SHOTS = [
    ("weather-rain", 11, 0, 0, 5 * 1440 + 300, "", 0, "w2,t120,Fv"),
    ("weather-storm", 11, 0, 0, 5 * 1440 + 300, "", 0, "w3,t130,Fv,flash,lead=120"),
    ("weather-snow", 41, 5, 0, 5 * 1440 + 300, "", 0, "w5,t-30,Fh,lead=240"),
    ("weather-heat", 21, 2, 1, 4 * 1440 + 480, "", 0, "w0,t330,pop=14,Fc,lead=300"),
    ("weather-cold-night", 11, 0, 2, 4 * 1440 + 17 * 60, "", 0, "w0,t-40,pop=8,Fb,lead=1200"),
    ("weather-fog", 11, 0, 0, 5 * 1440 + 300, "", 0, "w4,t90,Fv"),
    ("weather-clouds", 11, 0, 0, 5 * 1440 + 420, "", 0, "w1,t190,Fv,shadow"),
    ("infirmary-sick", 21, 2, 3, 2 * 1440 + 300, "i", 50, "Fi"),
    ("lumberjack-saplings", 11, 0, 0, 2 * 1440 + 300, "l", 0, "Fl,wood0,pop=8"),
]


MON_TYPES = ["pyramid", "stone-circle", "great-tree", "obelisk", "temple"]


def mon_type(seed):
    """Same hash as village_mon_type() in village.cpp: the monument type of a world."""
    h = (seed * 0x9E3779B1) & 0xFFFFFFFF
    h ^= h >> 15
    h = (h * 0x85EBCA6B) & 0xFFFFFFFF
    h ^= h >> 13
    return (h >> 4) % 5


def seed_for_type(t, first=1):
    seed = first
    while mon_type(seed) != t:
        seed += 1
    return seed


def build():
    srcs = [os.path.join(ROOT, "lib", "gamecore", "src", f) for f in sorted(os.listdir(os.path.join(ROOT, "lib", "gamecore", "src"))) if f.endswith(".cpp")]
    subprocess.check_call(["g++", "-std=c++17", "-O2", "-o", EXE, os.path.join(ROOT, "tools", "shots.cpp")] + srcs)


def shot(mode, path, seed, lf, cl, ticks=0, scale=1, minutes=0, extra="", boost=0, tokens=""):
    ppm = path + ".ppm"
    subprocess.check_call([EXE, mode, ppm, str(seed), str(lf), str(cl), str(ticks), str(minutes), extra or "-", str(boost), tokens],
                          stderr=subprocess.DEVNULL)
    img = Image.open(ppm)
    if scale > 1:
        img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    img.save(path)
    os.remove(ppm)


def r6_shots():
    # r6: every monument type at its last stage, a monument half way, the worship day, the two summaries and a town on day 35.
    # (-1 -1: landform and climate come from the seed.) Tokens are explained at the top of tools/shots.cpp.
    for t in range(5):
        shot("screen", os.path.join(OUT, f"monument-{MON_TYPES[t]}.png"), seed_for_type(t, 3), -1, -1, 0, 2, 1440 + 360, "", 0, "pop=10,mon=5,Fm,settle=5")
    shot("screen", os.path.join(OUT, "monument-midway.png"), seed_for_type(1, 3), -1, -1, 0, 2, 1440 + 360, "", 0, "pop=10,mon=3,Fm,settle=5")
    shot("screen", os.path.join(OUT, "worship-day.png"), seed_for_type(4, 3), -1, -1, 0, 2, 1440 + 360, "", 0, "pop=14,mon=5,phase=1,Fm,settle=90")
    shot("screen", os.path.join(OUT, "summary.png"), seed_for_type(2, 3), -1, -1, 0, 2, 3 * 1440 + 360, "", 0, "mon=5,phase=3,settle=1")
    shot("screen", os.path.join(OUT, "summary-extinct.png"), seed_for_type(0, 3), -1, -1, 0, 2, 1440 + 360, "", 0, "extinct,settle=8")
    shot("screen", os.path.join(OUT, "town-day35.png"), 11, 0, 0, 0, 2, 34 * 1440 + 300, "town")


def r7_shots():
    # r7: the two top bar pairs (faith + population, then food + wood; the pair changes every 3.5 s: 4 s after the start it is the first pair,
    # 1 s after the start the second), villagers walking a ramp (mode r: run on until somebody is on one), the shaman cutting a ramp
    # (mode R) and a terraced village (the town on day 20 of a mountain plateau).
    for name, ticks in (("a", 4), ("b", 1)):
        _bar = os.path.join(OUT, "_bar.png")
        shot("screen", _bar, 11, 0, 0, ticks, 1, 2 * 1440 + 300, "", 400, "pop=20")
        Image.open(_bar).crop((0, 0, 240, 16)).resize((720, 48), Image.NEAREST).save(os.path.join(OUT, f"topbar-pair-{name}.png"))
        os.remove(_bar)
    # the worst case for the clock: 4 digits of faith, 2 of population, -10 C (the unit is left out of three characters)
    _bar = os.path.join(OUT, "_bar.png")
    shot("screen", _bar, 11, 0, 0, 4, 1, 2 * 1440 + 300, "", 9999, "pop=46,w0,t-100,lead=60")
    Image.open(_bar).crop((0, 0, 240, 16)).resize((720, 48), Image.NEAREST).save(os.path.join(OUT, "topbar-worst-case.png"))
    os.remove(_bar)
    shot("screen", os.path.join(OUT, "ramp-villagers.png"), 21, 5, 2, 0, 2, 300, "r", 200, "Fv")
    shot("screen", os.path.join(OUT, "ramp-shaman.png"), 11, 0, 0, 0, 2, 300, "R", 200, "Fv")
    shot("screen", os.path.join(OUT, "terraced-village.png"), 41, 5, 0, 0, 2, 19 * 1440 + 300, "town")


def r8_shots():
    # r8: the top bar (resources | weather icon + temperature | clock), the worst cases, the band labels and the robe colours.
    def bar(name, ticks, tokens, boost=400, seed=11):
        _bar = os.path.join(OUT, "_bar.png")
        shot("screen", _bar, seed, 0, 0, ticks, 1, 2 * 1440 + 300, "", boost, tokens)
        Image.open(_bar).crop((0, 0, 240, 16)).resize((720, 48), Image.NEAREST).save(os.path.join(OUT, name))
        os.remove(_bar)
    bar("topbar-pair-a.png", 4, "pop=20")
    bar("topbar-pair-b.png", 1, "pop=20")
    bar("topbar-worst-case.png", 4, "pop=46,w0,t-100,lead=60,faith=9999", 9999)             # 9999 faith, 48 villagers, -10 C
    bar("topbar-worst-case-b.png", 1, "pop=46,w0,t-100,lead=60,food=999,wood=999", 9999)    # 999 food, 999 wood, -10 C
    bar("topbar-999-48-21.png", 4, "pop=46,w0,t210,lead=60,faith=999", 0)                  # 999 faith, 48 villagers, 21 C
    # the band: what the shaman is doing (mode L runs on until the band reads the label)
    for name, seed, lab in (("band-praying", 21, "PRAYING"), ("band-resting", 11, "RESTING"), ("band-walking-to-site", 21, "WALKING_TO_SITE"),
                            ("band-blessing-fields", 11, "BLESSING_FIELDS"), ("band-levelling", 11, "LEVELLING_LAND")):
        shot("screen", os.path.join(OUT, name + ".png"), seed, -1, -1, 0, 2, 4 * 1440 + 300, "L", 150, "lab=" + lab)
    # the robe colours: seeds that give six different ones, the shaman in the middle of the picture
    tiles = []
    for seed in (1, 25, 7, 8, 3, 11):
        _r = os.path.join(OUT, "_robe.png")
        shot("screen", _r, seed, -1, -1, 0, 1, 1)
        tiles.append(Image.open(_r).crop((84, 72, 156, 144)).resize((216, 216), Image.NEAREST))
        os.remove(_r)
    sheet = Image.new("RGB", (216 * 3 + 8, 216 * 2 + 4), (30, 30, 30))
    for k, t in enumerate(tiles):
        sheet.paste(t, ((k % 3) * 220, (k // 3) * 220))
    sheet.save(os.path.join(OUT, "robes.png"))


def main():
    os.makedirs(OUT, exist_ok=True)
    build()
    if "--r8" in sys.argv:             # only the r8 pictures
        r8_shots()
        os.remove(EXE)
        print("wrote the r8 pictures to", OUT)
        return
    if "--r7" in sys.argv:             # only the r7 pictures
        r7_shots()
        os.remove(EXE)
        print("wrote the r7 pictures to", OUT)
        return
    if "--r6" in sys.argv:             # only the r6 pictures
        r6_shots()
        os.remove(EXE)
        print("wrote the r6 pictures to", OUT)
        return
    for lf, cl, seed in GALLERY:
        name = f"{LANDFORMS[lf]}-{CLIMATES[cl]}"
        shot("map", os.path.join(OUT, f"map-{name}.png"), seed, lf, cl)
        shot("screen", os.path.join(OUT, f"screen-{name}.png"), seed, lf, cl, 6, 2)
    for cl in range(4):
        shot("screen", os.path.join(OUT, f"climate-{CLIMATES[cl]}.png"), 11, 0, cl, 6, 2)
    # r4: the shaman terraforming (band: his project), a pond dug on a world without a coast, and the town on day 20
    shot("screen", os.path.join(OUT, "shaman-terraforming.png"), 1, 0, 0, 0, 2, 1, "cast", 200)
    shot("screen", os.path.join(OUT, "pond.png"), 1, 5, 0, 0, 2, 1, "pond", 200)
    for lf, cl, seed in TOWNS:
        shot("screen", os.path.join(OUT, f"town-day20-{LANDFORMS[lf]}-{CLIMATES[cl]}.png"), seed, lf, cl, 0, 2, 19 * 1440 + 300, "town")
    # r5: weather (the tokens are explained at the top of tools/shots.cpp), sickness and wood. minutes = day * 1440 + minutes since 06:00
    for name, seed, lf, cl, minutes, mode, boost, tokens in R5_SHOTS:
        shot("screen", os.path.join(OUT, name + ".png"), seed, lf, cl, 0, 2, minutes, mode, boost, tokens)
    r6_shots()
    r7_shots()
    r8_shots()
    os.remove(EXE)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
