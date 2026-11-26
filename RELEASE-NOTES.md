What is new in MiniGodus r3:

- Villagers: up to 48, 6 bytes each, starting with one couple; a couple has a child about every 3 days. Three meals a day at 07:30, 12:30 and 19:00 local time, thirst and water trips, weak after 4 missed meals and death after 6, children grow up in 5 days, adults live 20 to 28 days, elders pray, a child needs a free bed and two days of food to spare. They sleep on the clock's sleep schedule.
- Daily roles chosen by need each morning, each with its own tunic and tool animation: farmer, fisher, lumberjack, builder, healer, water carrier, child, elder (herder once pens exist).
- Buildings built by the village itself: tents, huts, wheat fields (three growth stages), fishing huts and wells. Faith is produced by awake adults and praying elders. The shaman now wanders near the village.
- Top bar: one big counter (16 px icon, clock-sized digits) cycling faith, population, food, wood every 3 s, with real values.
- Polish: snow terraces now have visible cliff faces and shading; cliff and rock tops have a warm rim instead of a water-like cyan; heights come from fractal noise with a domain warp, so islands, coasts and cliffs are irregular, and islands and archipelagos cover about half the map (before and after pictures in the README).
- Memory: the quarter table is a window around the camera and every pathfinding or site search shares one distance buffer, so r3 uses less static RAM than r2 even with the village.
- Saved game format version 3 (village in the reserved bytes: the first 16 villagers and 18 buildings, the stocks; the complete save is release r6). An r2 save is ignored and a new world starts. Recovery layer, update page, hotspot, safe mode, the flash area 0x80000-0x82FFF and the 500 KB limit are unchanged.
- Known limits of r3: the herder role exists but nobody is assigned to it until animal pens arrive in r4; the saved game holds only the first 16 villagers and 18 buildings (the complete save is r6).
- Land: a smoothing pass removes ribbons, spikes and pillars narrower than 2 tiles; islands and archipelagos keep open sea along the border. A forward jump of the clock over midnight counts as a new day, a jump back (time zone, NTP) does not. Weakness from thirst ends with the drink.
- New tests: test/test_village.cpp (10 simulated days on 20 seeds under ASan, meals at real times, hunger, births, save round trip with garbage bytes), a land-shape irregularity test in test/test_worldgen.cpp.

What was new in MiniGodus r2:

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
