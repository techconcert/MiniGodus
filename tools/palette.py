"""Master 16-colour palette shared by every tile and sprite.

Index 0 is transparent (stored as magenta #FF00FF in the PNGs).
Edit colours here, then re-run `python tools/png2tiles.py`.
`python tools/palette.py` writes assets/palette.gpl for Aseprite/LibreSprite.
"""
import os

# (char used in ASCII art, name, RGB)
PALETTE = [
    (".", "transparent", (255, 0, 255)),
    ("k", "outline",     (26, 20, 36)),
    ("g", "dark green",  (46, 92, 40)),
    ("G", "green",       (82, 150, 58)),
    ("l", "light green", (140, 200, 75)),
    ("b", "dark brown",  (100, 64, 38)),
    ("B", "brown",       (160, 108, 60)),
    ("s", "sand",        (216, 180, 112)),
    ("w", "dark blue",   (40, 72, 150)),
    ("W", "blue",        (70, 135, 215)),
    ("f", "foam",        (160, 210, 240)),
    ("p", "skin",        (240, 200, 160)),
    ("r", "red",         (196, 58, 42)),
    ("o", "white",       (240, 240, 232)),
    ("a", "grey",        (128, 128, 144)),
    ("Y", "yellow",      (240, 192, 64)),
]

CHAR_TO_INDEX = {c: i for i, (c, _, _) in enumerate(PALETTE)}
RGB_TO_INDEX = {rgb: i for i, (_, _, rgb) in enumerate(PALETTE)}


def rgb565(rgb):
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def write_gpl(path):
    with open(path, "w") as f:
        f.write("GIMP Palette\nName: MiniGodus\nColumns: 16\n#\n")
        for _, name, (r, g, b) in PALETTE:
            f.write(f"{r:3d} {g:3d} {b:3d}\t{name}\n")


if __name__ == "__main__":
    root = os.path.join(os.path.dirname(__file__), "..")
    write_gpl(os.path.join(root, "assets", "palette.gpl"))
    print("wrote assets/palette.gpl")


# ---------------------------------------------------------------------------------------------------------------
# World palettes (r2). The terrain and the world objects use their own 16 slots, one swatch per climate.
# Slot meaning is the same in every climate; only the RGB changes. Slot 0 is transparent.
WORLD_SLOTS = ["transparent", "outline", "deep water", "shallow water", "pale", "white", "sand", "sand dark",
               "green 0 (dark)", "green 1 (layer 3)", "green 2 (layer 4)", "green 3 (layer 5)", "green 4 (layer 6)",
               "earth", "rock", "shadow"]
CLIMATES = ["temperate", "savanna", "northern", "tropical"]
WORLD_PALETTES = {
    "temperate": [(255, 0, 255), (32, 26, 44), (36, 66, 140), (62, 126, 206), (168, 214, 240), (244, 244, 236),
                  (226, 196, 128), (178, 140, 86), (44, 98, 52), (66, 134, 60), (94, 164, 66), (128, 188, 76),
                  (162, 206, 98), (140, 92, 56), (140, 142, 156), (70, 54, 62)],
    "savanna": [(255, 0, 255), (40, 24, 24), (40, 90, 120), (84, 160, 170), (190, 220, 214), (250, 244, 226),
                (236, 196, 120), (196, 142, 76), (96, 92, 40), (146, 134, 56), (180, 164, 70), (206, 184, 92),
                (226, 204, 120), (156, 84, 48), (150, 130, 116), (84, 52, 44)],
    "northern": [(255, 0, 255), (22, 24, 40), (28, 52, 110), (56, 108, 170), (190, 224, 244), (246, 250, 255),
                 (170, 164, 150), (120, 114, 108), (24, 64, 60), (44, 94, 80), (70, 122, 98), (100, 146, 114),
                 (132, 170, 134), (104, 84, 76), (120, 128, 148), (52, 52, 70)],
    "tropical": [(255, 0, 255), (24, 28, 44), (20, 70, 150), (40, 190, 200), (170, 240, 236), (255, 252, 240),
                 (250, 230, 170), (214, 176, 110), (16, 96, 56), (30, 140, 70), (60, 176, 76), (100, 206, 88),
                 (150, 224, 110), (156, 100, 60), (128, 128, 140), (60, 50, 60)],
}
WORLD_REF = WORLD_PALETTES["temperate"]          # the PNG sheets of the world objects are painted with this one
WORLD_RGB_TO_INDEX = {rgb: i for i, rgb in enumerate(WORLD_REF)}

# Role palette of terrain.png: the quarter tiles are painted with roles, not colours. The game maps the roles to
# palette slots per tile kind (own tile A, neighbour B, rim, cliff face); see lib/gamecore/src/world.cpp.
ROLES = [("transparent", (255, 0, 255)), ("A dark", (40, 90, 40)), ("A", (70, 150, 60)), ("A light", (150, 210, 90)),
         ("B dark", (110, 80, 40)), ("B", (190, 150, 90)), ("B light", (230, 200, 140)), ("rim dark", (20, 20, 30)),
         ("rim light", (255, 255, 200)), ("face dark", (90, 50, 40)), ("face", (150, 90, 60)),
         ("face light", (200, 140, 100)), ("sparkle", (255, 255, 255)), ("shadow", (60, 50, 90)),
         ("spare 1", (0, 160, 160)), ("spare 2", (200, 0, 120))]
ROLE_RGB_TO_INDEX = {rgb: i for i, (_, rgb) in enumerate(ROLES)}
R_AD, R_A, R_AL, R_BD, R_B, R_BL, R_RD, R_RL, R_FD, R_F, R_FL, R_SP, R_SH = range(1, 14)
