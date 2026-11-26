"""Builds tools/shots.cpp with the real game core and writes the README screenshots to docs/screenshots/.

    python tools/make_screenshots.py            # needs g++ and Pillow
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
TOWNS = [(0, 0, 11), (2, 3, 21), (3, 1, 7)]
VILLAGES = [(0, 0, 11, 3), (2, 3, 21, 4), (0, 2, 11, 5), (3, 1, 7, 4)]


def build():
    srcs = [os.path.join(ROOT, "lib", "gamecore", "src", f) for f in sorted(os.listdir(os.path.join(ROOT, "lib", "gamecore", "src"))) if f.endswith(".cpp")]
    subprocess.check_call(["g++", "-std=c++17", "-O2", "-o", EXE, os.path.join(ROOT, "tools", "shots.cpp")] + srcs)


def shot(mode, path, seed, lf, cl, ticks=0, scale=1, minutes=0, extra="", boost=0):
    ppm = path + ".ppm"
    subprocess.check_call([EXE, mode, ppm, str(seed), str(lf), str(cl), str(ticks), str(minutes), extra or "-", str(boost)],
                          stderr=subprocess.DEVNULL)
    img = Image.open(ppm)
    if scale > 1:
        img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    img.save(path)
    os.remove(ppm)


def main():
    os.makedirs(OUT, exist_ok=True)
    build()
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
    os.remove(EXE)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
