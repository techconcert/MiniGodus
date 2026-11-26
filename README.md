# MiniGodus

A self-playing god game for the GeekMagic 240x240 ESP8266 clock. A shaman, standing in for you,
spends the faith that villagers produce to reshape the land and to lay out homes, farms and
workshops. You never press anything: it is a clock with a little world in it. The full plan is in
[docs/DESIGN.md](docs/DESIGN.md).

**Status: release r5, weather, sickness and wood.** The real weather now plays in the game. Rain, storms, snow, heat, cold and
fog (the Open-Meteo reading the clock already fetches) change how the fields grow, how thirsty people are, how much firewood the
village burns and who falls sick; there is rain, snow, lightning, haze and drifting cloud shadows on the screen. Villagers get sick
(more often when hungry, thirsty, cold or wet), an infirmary with a healer cures them in a day, and untreated sickness can kill the
old and the weak. Lumberjacks fell trees, plant saplings on the stumps and the forest grows back. The shaman (r4) still decides what
to build; he now builds the infirmary for the first sick villager. The recovery layer, update page, rescue hotspot, WiFi, time
zone, sleep schedule and flash saving are unchanged.

**Download:** every build of the `main` branch is published on the
[Releases page](https://github.com/techconcert/MiniGodus/releases). Releases are named by number
(**MiniGodus r1**, r2, ...; the number lives in the `VERSION` file) and the newest is marked
"Latest". Each release has the two firmware files, `preview.html` (the game in your browser)
and `SHA256SUMS.txt`. The file names also carry the short commit, for example
`SDP_MiniGodus_quest_1-1a2b3c4.bin`.

## The world (r2)

Every world is made from one 32-bit seed (the clock's hardware random number generator picks it; a given seed always
gives the same world). The seed chooses one of six **landforms** and one of four **climates**, and a new world never
has the same landform and climate as the world before it (the last 4 worlds are kept in the save).

| Landform | Looks like |
|---|---|
| island | one island, terraces rising to the middle |
| coast | the sea on one side, land rising inland |
| river valley | a winding river with hills on both sides |
| lake basin | a lake in a bowl of hills |
| archipelago | 2 or 3 separate islands |
| mountain plateau | a terraced plateau with rock, lowland around it |

The map is 1 byte per tile: height 0 deep water, 1 shallow water, 2 beach, 3 to 6 land layers, 7 rock (unbuildable),
plus snow cover, soil and an object (tree, bush or fish, boulder). Climates are temperate, savanna, northern (snow
caps, pines) and tropical (palms); each has its own 16-colour palette, trees and resources.

Screens of the clock itself (the game core run on a PC, `tools/make_screenshots.py`), and the whole map of each landform:

| Island, temperate | Coast, tropical |
|---|---|
| ![Island](docs/screenshots/screen-island-temperate.png) | ![Coast](docs/screenshots/screen-coast-tropical.png) |
| ![Island map](docs/screenshots/map-island-temperate.png) | ![Coast map](docs/screenshots/map-coast-tropical.png) |

| River valley, savanna | Lake basin, northern |
|---|---|
| ![River valley](docs/screenshots/screen-river-valley-savanna.png) | ![Lake basin](docs/screenshots/screen-lake-basin-northern.png) |
| ![River valley map](docs/screenshots/map-river-valley-savanna.png) | ![Lake basin map](docs/screenshots/map-lake-basin-northern.png) |

| Archipelago, tropical | Mountain plateau, northern |
|---|---|
| ![Archipelago](docs/screenshots/screen-archipelago-tropical.png) | ![Plateau](docs/screenshots/screen-mountain-plateau-northern.png) |
| ![Archipelago map](docs/screenshots/map-archipelago-tropical.png) | ![Plateau map](docs/screenshots/map-mountain-plateau-northern.png) |

The four climates on the same island:

| Temperate | Savanna | Northern | Tropical |
|---|---|---|---|
| ![](docs/screenshots/climate-temperate.png) | ![](docs/screenshots/climate-savanna.png) | ![](docs/screenshots/climate-northern.png) | ![](docs/screenshots/climate-tropical.png) |

How it is drawn: every 16x16 tile is four 8x8 quarters. A quarter looks at its own tile and the three tiles that touch
its corner and picks a piece of art for the pair of terrains (water/sand, sand/grass, grass/cliff, cliff/rock,
grass/snow, deep/shallow water). That gives rounded coasts and curved cliff corners from about 70 small pieces
of art. A step up draws a cliff face on its south side. The pieces are chosen once when the map changes, not per
frame; the 13 map strips are then redrawn from those tables while the camera scrolls pixel by pixel.

## The village (r3)

![Village, temperate](docs/screenshots/village-island-temperate.png)

| Village, tropical river valley | Village, northern island |
|---|---|
| ![](docs/screenshots/village-river-valley-tropical.png) | ![](docs/screenshots/village-island-northern.png) |

**People.** Up to 48 villagers, 6 bytes each. The game starts with one couple in a tent. A child grows up in 5 days; an adult
lives 20 to 28 days (the last 5 are spent as an elder, who prays). Villagers pair up (the k-th healthy woman with the k-th healthy man); a couple has a child about every 3 days, when there is a free
bed and two days of food to spare. Hunger is counted in missed meals: weak after 4,
death after 6 (7 with a healer awake). Thirst is satisfied at a well or on the shore; a water carrier fills a stock that
everybody can drink from.

**Jobs, chosen each morning by need.** Farmer (hoe, a wheat field), fisher (rod, fishing hut or shore), lumberjack (axe,
fells a tree, trees regrow a little every day), builder (hammer, builds the current site), healer (herbs), water
carrier (bucket), child (plays), elder (prays). Herders need pens, which come in a later release. Each job has its own
tunic colour and tool animation.

**Buildings.** Tent (2 beds), hut (4 beds), wheat field (three growth stages), well, fishing hut on a beach next to the water.
The planner puts a new building near the village green when the village needs it (a bed for everybody plus one spare, one field
per 5 people, a well, a fishing hut) and the wood is there; the builder works on it minute by minute. Faith is produced by awake adults every
30 minutes, elders count double.

**Time.** Everything runs on the clock's real time, so a game day is a real day. A longer jump of the clock (WiFi time
sync, power cut) is not played back, so a clock that was off never starves its village.

**Memory.** The village uses about 500 bytes of RAM. Pathfinding and the search for building sites share one 1 KB distance
buffer; the quarter-tile table is a window around the camera instead of the whole map, which saved more than the village needs.
The saved game (still 192 bytes) holds the first 16 villagers and 18 buildings, food, wood, faith and water; the full multi-slot
save is release r6.

The land itself got a pass in r3: heights come from fractal noise with a domain warp before the landform shape is applied,
so coasts and cliffs are irregular instead of nested rings, and a smoothing pass removes everything narrower than 2 tiles. Islands and archipelagos cover about half the map, with open sea along the border.

| Before (r2) | After (r3) |
|---|---|
| ![](docs/screenshots/shape-before-island-northern.png) | ![](docs/screenshots/shape-after-island-northern.png) |
| ![](docs/screenshots/shape-before-island-temperate.png) | ![](docs/screenshots/shape-after-island-temperate.png) |
| ![](docs/screenshots/shape-before-archipelago-tropical.png) | ![](docs/screenshots/shape-after-archipelago-tropical.png) |

The top bar: ![](docs/screenshots/topbar-counters.png)

## The shaman (r4)

![The shaman digging a pond](docs/screenshots/shaman-terraforming.png)

| A pond dug on a world without a coast | The town on day 20 (island, temperate) |
|---|---|
| ![](docs/screenshots/pond.png) | ![](docs/screenshots/town-day20-island-temperate.png) |

| Town on day 20, river valley, tropical | Town on day 20, lake basin, savanna |
|---|---|
| ![](docs/screenshots/town-day20-river-valley-tropical.png) | ![](docs/screenshots/town-day20-lake-basin-savanna.png) |

| Town on day 20, mountain plateau, northern | |
|---|---|
| ![](docs/screenshots/town-day20-mountain-plateau-northern.png) | |

**Needs and projects.** He scores food (less than 2 days of food, or too few fields), housing (fewer beds than people),
water (a home with no water within 8 tiles), health (the first sick villager wants an infirmary, r5),
wood, growth (a new home tier unlocked: a flat 3x3 plot beside the homes first, then the home), faith (no shrine) and
monument (a hook for r6). A pond has its own need when there is no coast. A world's seed decides his personality: farmer
(green robe), herder (sand), fisher (blue), builder (red). It biases the scores and the choice of food building.

**Terraforming.** One layer per cast. A step between two layers costs the number of the higher one, an object (tree,
rock) costs 1, rock layer 7 is never touched (rock is only the highest peaks: at most about 4% of the land, 10% on a mountain plateau). Faith is paid when the staff comes down and never goes below zero. Near the
village neighbouring tiles may differ by one layer only, and walking needs that too, so after a project he cuts ramps
(a ramp sprite marks them) until there is no cliff left that blocks a path. Buildings stand on one layer between 3 and 5,
never on the lowest land beside water and never on the keep-clear shore of a fishing hut; the site search takes the
spot of lowest cost (faith to level and ramp, 2 per tile of walking from the green, reserved-tile penalties) and refuses
any spot that would cut the village in two or shut another building in. Every single cast is checked as well: a step
that would cut the village in two or shut a building in is left out. Faith (up to 9999) is not hoarded: when more than 40% of a
reserve is spare he opens wide flat land around the green for the next tiers, towards forests and good soil, and digs a
second pond for fish (LEVELLING LAND on the band). A building that trees have shut in gets its trees cleared.

| Building | Size | From | Beds / yield |
|---|---|---|---|
| Tent | 1x1 | start | 2 beds |
| Hut | 2x2 | 6 villagers | 4 beds |
| Cottage | 3x2 or 2x3 | 14 | 6 beds |
| Manor | 3x3 | 24 | 8 beds |
| Wheat field | 2x2 to 3x3 | start | 8 grain per harvest |
| Orchard | 3x2 or 2x3 | 8 | 8 fruit per harvest |
| Animal pen | 3x2 to 3x3 | 10 | up to 10 animals (sheep, pigs, chickens), they breed when fed; a herder gets food from them |
| Fishing hut | 2x1 or 1x2 | on a shore | fish |
| Well | 1x1 | start | water |
| Lumberjack hut | 2x2 | 4 | more wood per tree |
| Granary | 2x2 | 10 | food cap +100 |
| Shrine | 2x2 | 6 | +1 faith per half hour, praying elders count 3 |
| Infirmary | 2x2 or 3x2 | first sick villager | heals in 1 day with a healer (3 days without) |

A bigger home may replace tents and huts. Eating from three or more of the four food sources (grain, fruit, animal, fish)
in one day makes the village content: 25% more faith until midnight.

**Memory.** The shaman costs about 100 bytes of static RAM (project, reserved plots, last ramps) and the buildings table grew
from 32 to 40 entries; every search shares the one distance buffer. The saved game (still 192 bytes, version 4) now keeps each building's
size, but not the dug land: after a load the land is generated again and healed at once, free of charge.

## Weather, sickness and wood (r5)

The weather is the live Open-Meteo reading (WMO weather code and temperature, every 30 minutes). `wx_state()` in
`lib/gqsafe/src/wxparse.cpp` maps the code to the game's states: 0 to 3 clear or cloudy (windy above 29 km/h), 45 and 48 fog,
51 to 67 and 80 to 82 rain, 71 to 77, 85 and 86 snow, 95 to 99 thunderstorm. With no reading (no WiFi, a failed fetch) the game is
mild and dry: clear sky, 15 C, no rain, no shadows, no frost, no heat.

| Weather | On screen | In the game |
|---|---|---|
| Rain, drizzle | animated rain, puddles that gather on the low land and dry again | fields grow 50% faster, fishers catch 1 more, people working outside collect exposure |
| Thunderstorm | heavier slanted rain, a lightning bolt now and then that brightens one or two strips | like rain, and everybody goes home: nobody works outside while it lasts (they eat and drink at home) |
| Snow | falling snow; snow settles on the high layers in three steps (layer 6, then 5, then 4) and melts away again | fields stop, it counts as cold (firewood) |
| Above 28 C | | thirst comes twice as often, water carriers are hired in numbers (up to 3) |
| Below 8 C | | each occupied home burns 1 wood per night (chimney smoke until 09:00); lumberjacks come first; a home without wood is a cold home |
| Fog | haze, thicker at the top of the screen | none |
| Clear, cloudy, windy | cloud shadows drift over the land (2, 4 or 3 of them) | none |
| Night (the clock's sleep schedule) | the map is dimmed | everyone sleeps |

| Rain | Thunderstorm | Snow on the high layers |
|---|---|---|
| ![](docs/screenshots/weather-rain.png) | ![](docs/screenshots/weather-storm.png) | ![](docs/screenshots/weather-snow.png) |

| Heat, water carriers | Cold night, chimney smoke | Fog |
|---|---|---|
| ![](docs/screenshots/weather-heat.png) | ![](docs/screenshots/weather-cold-night.png) | ![](docs/screenshots/weather-fog.png) |

| Cloud shadows | An infirmary with a sick villager (green cross) | Lumberjack, stumps and saplings |
|---|---|---|
| ![](docs/screenshots/weather-clouds.png) | ![](docs/screenshots/infirmary-sick.png) | ![](docs/screenshots/lumberjack-saplings.png) |

**Sickness.** Once a day, at midnight, every villager rolls against a risk of 1% plus: 3% for each missed meal (up to 3), a quarter of
a percent per thirst point, 2% per half hour spent outside in the rain (up to 10 half hours, 20%), 2% per hour outside in the cold (counted together), 12% for a night in a
cold home without wood, and 3% for elders and the weak. A sick villager does not work, walks to the infirmary and wears a green tunic and
a green cross. With an infirmary and a healer the sickness is over at the next midnight; an infirmary with nobody to tend it takes 2 days,
no infirmary 3 days. Untreated, a sick elder or weak villager dies with a 25% chance at each midnight (6% in an infirmary with no healer);
healthy children and adults never die of it. A healer is hired as soon as somebody is sick and there is an infirmary. The shaman's health
need (the top of his list) builds the infirmary when the first villager falls sick, hunger alone does not call for one.

**Wood.** A lumberjack walks to a tree, fells it (3 wood, 4 with a lumberjack hut) and leaves a stump; his next swing plants a sapling on the
stump. A sapling becomes a tree after 4 to 5 days (never beside a building, on a reserved plot or on the green, never where the tree would shut a
path); a stump left alone rots away after 3 days. The forest also seeds saplings beside its own trees (up to 36 trees and saplings, more when the
forest is nearly gone). Lumberjacks leave the last 6 trees alone and never fell on a reserved plot or a project footprint.

**Debug commands** (`GQ_DBG_*` in `lib/gamecore/src/gq_types.h`, used by the tests and `tools/shots.cpp`; the browser preview's live weather menu does the
same through the normal reading): `GQ_DBG_WEATHER` forces a weather (and its top bar icon), `GQ_DBG_TEMP` forces the temperature in tenths of a degree,
`GQ_DBG_SICK` makes a villager sick, `GQ_DBG_AGE` makes one an elder or weak, `GQ_DBG_FLASH` strikes lightning now.

**Cost.** Rain and snow are 24 to 32 particles in one fixed array, moved every 100 ms and drawn into the strips that are being drawn anyway (the
map strips are redrawn about 10 times a second while it rains or snows; clear weather redraws only what changed, plus the slow cloud shadows).
Puddles, haze, shadows, the night dimming and the lightning are per strip, with no full-screen buffer.

**Memory and save.** The weather, sickness and forest add about 340 bytes of static RAM (sick state 48 bytes, snow map 128, saplings 32, particles 64,
shadows 8, counters). The saved game keeps 192 bytes, version 4: a villager's sick state is one more bit in the villager's record (old saves read
as healthy), so an r4 save loads. The weather itself, the snow, the stumps and the saplings are not saved (the weather comes from the live reading, and
the forest is regenerated from the seed on load), nor is the sickness of the villagers beyond the first 16.

**Known limits of r5:** the full multi-slot save is r6; the forest, felled trees and saplings come back as generated after a restart; a sick villager is
sick for the whole time (no partial recovery); the night dimming has no lit windows yet; weather does not stop the shaman casting; cloud shadows and the
night dimming are drawn over buildings and people as a flat tint.

## Hardware

- GeekMagic SD Pro / SmallTV-style clock: ESP8266 (ESP-12, 4 MB flash) with a 240x240 ST7789
  display.
- Display wiring (from the [GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware)
  teardown): hardware SPI mode 3 at 40 MHz, MOSI 13, SCLK 14, DC 0, RST 2, CS tied to ground,
  backlight on GPIO 5 (active LOW).
- No serial adapter is needed or assumed. Everything is installed and recovered over WiFi.

## Builds

| File | What it is |
|---|---|
| `SDP_MiniGodus_quest_<version>.bin` | The game. |
| `SDP_MiniGodus_test_<version>.bin` | Display test: grass field, walking figure, RGB colour bars, FPS. Flash this first if you are unsure about colours. |

The names start with `SDP` because the stock updater only accepts such names.

## How to flash (over WiFi, no cable)

**First time, from the stock firmware:** open the clock's own web page, go to its firmware update
page and choose `SDP_MiniGodus_test_....bin` (or the quest file). Wait for the clock to restart.

**Later, from MiniGodus:** open `http://<clock IP>/update` (the IP address is on the clock's
bottom band and on your router's list), choose the new `.bin` and press **Upload and install**.
The page checks the file before uploading and shows its CRC32; the clock verifies the image again
before switching over. A failed or interrupted upload leaves the running firmware untouched.

Read [docs/RECOVERY.md](docs/RECOVERY.md) once, before the first install.

## The clock's web pages

Open `http://<clock IP>/`. The status page shows firmware, WiFi, network health, memory and
uptime. **Update firmware** and **Clock settings** are buttons in the first tile, **WiFi setup** is
in the Network tile.

**Update firmware** (`/update`): choose a `.bin` by hand, or press **Check for a new version**: your
browser (not the clock) asks GitHub (`techconcert/MiniGodus`) for `latest.json` on the `builds`
branch, tells you if a newer build exists, and **Download it** loads the file into the page for you;
you then press **Upload and install**. The clock itself never talks to GitHub. The same checks run on
the file as for a manual upload.

**Clock settings** (`/config`):

- **Weather:** search a city (foreign places work), or type latitude and longitude. Celsius is the
  default, Fahrenheit is an option. The clock asks [Open-Meteo](https://open-meteo.com/) (free, no
  account, plain HTTP) every 30 minutes and shows the icon and temperature on the top bar.
- **Sleep:** a sleep time and a wake time. The shaman walks to his bed, the screen and game go
  dark, the CPU slows down, and the clock keeps the time. At the wake time everything comes back by
  itself. **Sleep now** and **Wake now** buttons are there too. WiFi, the web pages and the update
  page stay on while asleep.
- **Location and time zone:** the city search also fills the time zone rule (converted in your
  browser, no firmware cost); you can still type the rule text by hand.
- **Screen:** brightness, colour depth, the weekly maintenance restart and the display test.
- **Save settings** and **Reset these settings** on this page touch only this page's settings.

The status page has a **Game settings** section (info card interval, quiet at night) with its own
Apply and **Reset game settings** buttons, which touch only those two settings.

## Recovery and the rescue hotspot

Every build contains, and CI refuses to produce a `.bin` without (`tools/check_bin.py`):

- **The update page starts first.** WiFi and the web server start before the display or the game,
  so a game bug cannot hide the update page.
- **Rescue hotspot** `MINIGODUS-XXXX`, password `MINIGODUS`, at `http://192.168.4.1/`. It is on for
  the first 10 minutes after every power-on and whenever the clock cannot find your WiFi. The
  WiFi page (`/wifi`) lets you enter your network again.
- **Crash-loop safe mode.** After 3 crashes in a row (or 5 in total without 6 hours of uptime in
  between) the clock boots without the game and shows SAFE MODE; after 6 it also skips the
  display. The update page still works.
- **Automatic restarts:** after hours without WiFi, with very low memory, and once a week at night.
  They never happen while a firmware upload is running, are rate limited, and count towards safe
  mode like any restart.
- **Size limit.** The ESP8266 holds the running firmware while receiving the next one, both inside
  a 1 MB area. CI fails any build over 500 KB, so updates always fit, including going back to
  stock.
- **Stock files are never touched.** Same flash layout as the stock firmware; the code never writes
  flash by itself (only the update page's Updater does), and CI forbids EEPROM, LittleFS and SPIFFS
  code. Flashing `SDPro_V1.0.6_....bin` from the MiniGodus update page brings the original clock
  back.
- **One flash area.** The clock saves its settings and the world in sectors 0x80000-0x82FFF (three
  4 KB sectors, between the running image and a staged update). Nothing else is ever written: not
  0x3FA000 (stock data), not 0xFF000, no EEPROM, LittleFS or SPIFFS. CI enforces this
  (`tools/lint_safety.py` rules L2 and L14). See "Saving to flash".

## Saving to flash

`firmware/store.cpp` keeps a ring log of 320-byte records (settings, weather and sleep settings, game)
in 36 slots over the three sectors. Each record has a CRC and a sequence number; the newest valid
one is loaded at boot, so a power cut in the middle of a write can never produce a half-saved state
(tested with random power cuts, see `test/test_slotlog.cpp`). Writes are rare: settings 5 seconds
after a change, the game every 20 minutes and at each new game day, at most 6 writes in 10 minutes;
every write is read back and 3 failures switch saving off. Nothing is written in safe mode, while a
firmware update runs, or when the image is larger than 0x7D000 bytes. After 2 or more crashes the
saved game is kept aside, not loaded, so a game that crashes the clock cannot crash it again from
the save. Updating to a new MiniGodus build keeps everything; flashing the stock firmware does not
(not needed). The status page shows "Saved to flash ..." or "Loaded from flash ..." lines.

**GeekQuest shares this flash area.** The clock's WiFi, weather and sleep settings are common to
both firmwares, but the saved game has its own magic number in each: MiniGodus' is `MGOD`
(`GQ_SAVE_MAGIC` in `lib/gamecore/src/gq_types.h`), GeekQuest's is `GGQS`. A game saved by the other
firmware fails the magic check and is ignored, so switching the clock between GeekQuest and MiniGodus
never loads the other's game; the next save simply replaces it. `test/test_save.cpp` checks this.

Gates: `tools/lint_safety.py`, `tools/check_bin_selftest.py`, `tools/check_bin.py` on every `.bin`,
a size report, and `tools/recovery_guard.py` (a `RECOVERY-CHANGE:` commit line and a new
`GQ_RECOVERY_REV` whenever a recovery file changes).

## Build and test

Firmware (PlatformIO, ESP8266 Arduino core):

```
pio run -e quest        # the game  -> dist/SDP_MiniGodus_quest_<version>.bin
pio run -e test         # display test build (the only one with the test scene)
```

Host tests (no ESP8266 needed; run every `test/*.cpp`):

```
for t in test/*.cpp; do
  g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror \
    -o /tmp/$(basename $t .cpp) $t lib/gamecore/src/*.cpp lib/gqsafe/src/*.cpp && /tmp/$(basename $t .cpp) </dev/null
done
python3 tools/lint_safety.py && python3 tools/lint_safety.py --selftest
python3 tools/check_bin_selftest.py
```

Browser preview (clang with wasm32 and lld): `tools/build_preview.sh` writes `preview/dist/index.html`.
The preview is the same game core as the clock, plus a debug panel that is not in the firmware:
a seed picker (type a seed and press **Load seed** to see exactly that world), a **New world** button (random seed,
never the same landform and climate as before), menus to force a landform or climate, live weather, sleep mode,
WiFi state, jump to a day, and time scale.

Art: tiles and sprites are `assets/src/*.png` on a 16x16 grid with the 16-colour palette in
`tools/palette.py`. After editing run `python tools/png2tiles.py`. The r2 world art is made by
`python tools/make_world_art.py` (terrain quarters painted with roles, trees and boulders, the shaman, the top bar icons),
which you can also edit by hand afterwards; the climate palettes are in `tools/palette.py` too. The README pictures come
from `python tools/make_screenshots.py` (it builds `tools/shots.cpp`, a host renderer that uses the real game core).
Python tests: `python3 test/test_art_tools.py`. The r4 building sprites (`village.png`, ids in `village_art_ids.h`) are drawn by the same script.

## Repository layout

```
firmware/        ESP8266 entry (main.cpp), ST7789 driver, recovery layer (safety.cpp, netwatch.cpp), prefs.cpp,
                 weather (wxnet.cpp), sleep schedule (nightmode.cpp), settings page (config.cpp), flash save (store.cpp)
lib/gamecore/    portable game core (no heap, no libc), used by the firmware and the preview:
                 world.cpp (generator, autotiler), godus.cpp (scene, camera, shaman's walk and staff, top bar),
                 village.cpp (villagers, buildings; shaman.inc.h is the shaman's brain: needs, site search, terraforming)
lib/gqsafe/      pure decision code of the recovery layer (boot counters, WiFi ladder, upload checks)
assets/src/      editable PNG sheets (terrain quarters, objects, shaman, icons, and the r1 sheets)
tools/           asset converter, version stamp, .bin safety checks, source lint, CI guards
firmware/config_page.h  source of the /config page; the firmware serves the gzip copy in config_page_gz.h (run tools/gen_config_gz.py after editing it, CI verifies)
docs/            design (DESIGN.md), the owner's recovery guide, screenshots/
preview/         browser preview (WebAssembly)
```

The `GQ_` prefix on code names (settings, events, queries) is a leftover from the project this
firmware grew out of and is kept so the recovery layer stays byte-for-byte comparable.
