"""Builds tools/shots.cpp with the real game core and writes the README screenshots to docs/screenshots/.

    python tools/make_screenshots.py            # needs g++ and Pillow (add --r6 or --r7 for the pictures of one release only)
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
# (landform, climate, seed, days) of the village pictures
# r4: the town on day 20 of an unaided world (landform, climate, seed)
TOWNS = [(0, 0, 11), (2, 3, 21), (3, 1, 7), (5, 2, 41)]
VILLAGES = [(0, 0, 11, 3), (2, 3, 21, 4), (0, 2, 11, 5), (3, 1, 7, 4)]


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
    shot("screen", os.path.join(OUT, "ramp-villagers.png"), 21, 5, 2, 0, 2, 300, "r", 200, "Fv")
    shot("screen", os.path.join(OUT, "ramp-shaman.png"), 11, 0, 0, 0, 2, 300, "R", 200, "Fv")
    shot("screen", os.path.join(OUT, "terraced-village.png"), 41, 5, 0, 0, 2, 19 * 1440 + 300, "town")


def main():
    os.makedirs(OUT, exist_ok=True)
    build()
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
    # r3: the village after a few days of play, at 11:00 (the clock is fed minute by minute from 06:00)
    for lf, cl, seed, days in VILLAGES:
        shot("screen", os.path.join(OUT, f"village-{LANDFORMS[lf]}-{CLIMATES[cl]}.png"), seed, lf, cl, 0, 2, days * 1440 + 300)
    # the land shapes of r3 (the "before" pictures are kept from r2) and the four top bar counters
    for lf, cl, seed, name in ((0, 2, 11, "island-northern"), (0, 0, 12, "island-temperate"), (4, 3, 33, "archipelago-tropical")):
        shot("map", os.path.join(OUT, f"shape-after-{name}.png"), seed, lf, cl)
    bars = []
    for k in range(4):
        shot("screen", os.path.join(OUT, "_bar.png"), 11, 0, 0, 3 * k + 1, 1, 2 * 1440 + 300)
        bars.append(Image.open(os.path.join(OUT, "_bar.png")).crop((0, 0, 240, 16)))
    os.remove(os.path.join(OUT, "_bar.png"))
    sheet = Image.new("RGB", (240, 16 * 4 + 6), (60, 60, 60))
    for k, b in enumerate(bars):
        sheet.paste(b, (0, k * 18))
    sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST).save(os.path.join(OUT, "topbar-counters.png"))
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
    os.remove(EXE)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
