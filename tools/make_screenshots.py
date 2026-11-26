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


def build():
    srcs = [os.path.join(ROOT, "lib", "gamecore", "src", f) for f in sorted(os.listdir(os.path.join(ROOT, "lib", "gamecore", "src"))) if f.endswith(".cpp")]
    subprocess.check_call(["g++", "-std=c++17", "-O2", "-o", EXE, os.path.join(ROOT, "tools", "shots.cpp")] + srcs)


def shot(mode, path, seed, lf, cl, ticks=0, scale=1):
    ppm = path + ".ppm"
    subprocess.check_call([EXE, mode, ppm, str(seed), str(lf), str(cl), str(ticks)])
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
    os.remove(EXE)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
