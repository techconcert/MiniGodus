What is new in MiniGodus r2:

- The world: a generated 32x32 map from a 32-bit seed (the clock's hardware random number generator). Six landforms (island, coast, river valley, lake basin, archipelago of 2 or 3 islands, mountain plateau) in four climates (temperate, savanna, northern with snow caps, tropical), each with its own 16-colour palette, trees and resources. A new world is never the same landform and climate as the previous one; the save keeps the last 4 worlds.
- Stepped height layers with a cliff face on the south side of every step, a different ground colour per layer, and smooth coasts and curved cliff corners from corner-based 8x8 quarter-tile autotiling (water/sand, sand/grass, grass/cliff, cliff/rock, grass/snow). Two-frame water shimmer.
- A camera that follows the shaman, now a robed figure with a staff, with smooth pixel scrolling.
- Top bar: time, weather icon and temperature on the right; faith, food, wood and population counters on the left (placeholder values; the pairs alternate every 4 seconds). The bottom band also shows "DAY n OF <world name>".
- Preview: a seed picker, a New world button, and menus to force a landform or climate. README has screenshots of every landform and climate.
- Saved game format version 2 (adds the world history); an r1 save is ignored and a new world starts. Recovery layer, update page, hotspot, safe mode, the flash area 0x80000-0x82FFF and the 500 KB limit are unchanged.
- New tests: world generator (determinism, all landforms and climates, no repeated pair, walkable start, every strip rendered under ASan), art tools (test/test_art_tools.py).
- GeekQuest's game-specific tests and tools (test_boss, test_calendar, test_delve*, test_econ, test_fmt, test_presence, test_quest, test_r3_api, test_soak, test_talk, test_ui, test_world, gq_sanitize.h, gen_quest_data.py, gen_talk.py) were left out of this repository on purpose; they test GeekQuest's game, which MiniGodus does not contain. (The world generator test here is test/test_worldgen.cpp.)

What was new in MiniGodus r1:

- First release: the foundation. MiniGodus is a self-playing god game for the GeekMagic 240x240 ESP8266 clock (see docs/DESIGN.md). It carries over the whole recovery layer, update page, rescue hotspot, WiFi, weather, time zone and sleep schedule, and flash saving from GeekQuest, renamed.
- The hotspot is MINIGODUS-XXXX (password MINIGODUS). The update page checks github.com/techconcert/MiniGodus for new versions.
- MiniGodus saves in the same flash area as GeekQuest but with its own magic number, so switching between the two firmwares never loads the other's game (the WiFi and clock settings are shared).
