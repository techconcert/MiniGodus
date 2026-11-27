# MiniGodus design

Living version: https://claude.ai/artifact/XpaeeNcNcvMuVA5Pqtagsd (26 Nov 2026). Releases are tracked as issues #1 to #6.

MiniGodus is a self-playing god game for the GeekMagic 240x240 ESP8266 clock. A shaman, standing in for the player, spends the faith villagers produce to reshape land and to lay out homes, farms and workshops. The village must feed itself 3 times a day, grow, and eventually finish a monument. Then a very different world starts.

## Fixed decisions
- One game day = one real day. Meals at real breakfast/lunch/dinner (about 07:30, 12:30, 19:00 local). Villagers sleep on the clock's sleep schedule. The world keeps simulating while the screen sleeps.
- Map 32x32 tiles, camera follows the shaman. Spend art budget on edge tiles: height transitions and land-to-water.
- Villagers starve, fall sick, age and die. Couples breed; a child grows up after 5 days.
- Buildings are at most 3x3; 3x2 and 2x3 allowed.
- Real weather and temperature drive rain, thirst and firewood.
- Save in the GeekQuest R5 flash area 0x80000-0x82FFF only.
- No hard end date: the world runs until the monument is done. Balance so no world ends before about day 30; typical 1 to 1.5 months.

## Carried over from GeekQuest (copy, rename only)
Recovery (update page first, safe mode, 500 KB limit, check_bin.py, lint_safety.py), rescue hotspot renamed `MINIGODUS-XXXX` / `MINIGODUS`, web pages (status, /update, /config, /wifi), latest.json on `builds` branch for "Check for a new version", Releases + CI, Open-Meteo weather (wxnet), time zone + sleep schedule, store.cpp/slotlog flash saving, ST7789 strip renderer, WASM preview, host smoke tests. Status page gains world day, population and a New world button.

## World
- 32x32 tiles (512x512 px). 1 byte per tile: height 0-7 (0 deep water, 1 shallow, 2 beach ... 6 land, 7 rock unbuildable) plus ground type/flags.
- Top-down with slight tilt: each step up draws a cliff face on its south side; each layer has its own ground colour (Godus strata look).
- Edge tiles: 8x8 quarter tiles picked from the 4 neighbours of each corner (autotile). Pairs: water/sand, sand/grass, grass/cliff, cliff/rock, grass/snow. ~5 quarter shapes per pair. 2-frame water shimmer.
- Randomness per seed (hardware RNG): landform (island, coast, river valley, lake basin, archipelago 2-3 islands, mountain plateau); climate look (temperate, savanna, northern pine with snow, tropical palms) with its own palette; resource placement (forest, fertile soil, fish, herds); start spot; shaman personality (Farmer, Herder, Fisher, Builder); monument (pyramid, stone circle, great tree, obelisk, temple). Keep last 4 seeds; reject a world with the same landform AND climate as the previous one.

## Villagers
- Start with 1 couple; cap 48. ~6 bytes each: age, sex, role, home, hunger, thirst, health, partner, sick.
- Food: 3 meals/day. Miss 4 in a row -> weak (no work); 6 -> dies.
- Water from well/lake/river; trips double above 28 C.
- Warmth: below 8 C each home burns 1 wood/night; cold homes raise sickness.
- Child grows up in 5 days; adults live 20-28 more days; elders pray (faith) and do no heavy work.
- Couple with a free bed and >= 2 days food surplus: a child about every 3 days.
- Sickness chance rises with hunger, thirst, cold, rain exposure. Infirmary heals in 1 day (3 days without). Untreated can kill elders/weak.
- Roles each morning by need, each with its own animation: farmer, herder, fisher, lumberjack, builder, healer, water carrier, child, elder.

## Economy and buildings
Faith (≈1/hour per awake adult, more if fed, housed, near shrine; elders add more; content villagers +25%) is spent by the shaman on terraforming and laying out buildings. Food is one store with 4 sources (grain, fruit, animal, fish); eating from 3+ sources in a day = content. Wood from lumberjacks for buildings and firewood; saplings regrow forests. Water is a place.

All buildings on flat land (single height). Unlocks are first guesses to tune by simulation.

| Building | Size | Unlocks at | Role |
|---|---|---|---|
| Tent | 1x1 | start | home for 2 |
| Hut | 2x2 | 6 villagers | home for 4 |
| Cottage | 3x2/2x3 | 14 | home for 6 |
| Manor | 3x3 | 24 | home for 8 |
| Wheat field | 2x2-3x3 | start | grain every 3 days, faster after rain |
| Orchard | 3x2/2x3 | 8 | fruit |
| Animal pen | 3x2-3x3 | 10 | sheep/pigs/chickens, breed when fed |
| Fishing hut | 2x1/1x2 on shore | start if water | fish; each water body has a regrowing stock |
| Well | 1x1 | start | water |
| Lumberjack hut | 2x2 | 4 | wood |
| Granary | 2x2 | 10 | raises food cap |
| Shrine | 2x2 | 6 | faith bonus, elders pray |
| Infirmary | 2x2/3x2 | first sick | heals |
| Monument | 3x3, 5 stages | 28 (and day 21) | ends the world |

## The shaman
Robed figure with staff, always on screen, one project at a time, tile by tile. Every 10 game minutes scores needs and picks the most pressing affordable: food (<2 days or a source low), housing, water (none within 8 tiles of a home), health, wood, growth (next home tier), faith (shrine), monument (spare faith). Site search minimises levelling cost + walking distance + reserved-tile penalties. Rules: no coast -> dig a pond of >= 6 tiles near the village and stock it; keep shore tiles by fishing huts clear; pre-flatten 3x3 plots when a tier unlocks; near the village neighbouring tiles differ by at most 1 level (cut ramps); never build on the lowest land next to water; terraform cost rises with layer; rock (7) untouchable until the monument stage. Personality biases choices.

## Weather and time
Rain/drizzle: rain on screen, puddles on low tiles, fields +50%, sickness risk outside, better fishing. Thunderstorm: heavier rain, lightning, shelter. Snow: settles on high layers, fields pause, firewood. >28 C thirst. <8 C firewood, chimney smoke, lumberjack priority. Clear/cloudy: cloud shadows. Fog: haze. Night (sleep schedule): go home, windows light, screen sleeps. No reading: mild dry.

## Arc
1. Landing (≈days 1-3): couple, tent, field, fishing spot or well.
2. Village (≈4-12): first children grown, huts, lumberjack, shrine, ~10 villagers.
3. Town (≈13-24): cottages, pens, orchard, granary, infirmary, ponds, 3x3 plots, 20-30 villagers.
4. Monument (≈25+): manors, monument in 5 stages funded with spare faith; first generation dies of age.
5. Final day: all gather and worship dawn to dusk. Summary screen (world name, days, peak population, births, deaths, buildings) stays until the next morning's wake time; the sleep schedule still darkens the screen.
6. New world from a new seed. Village died out -> summary early, new world the next morning. The New world button (with a confirmation) is on the Clock settings page, which the status page links to: the status page belongs to the recovery layer and is left untouched (r6 divergence).

## Screen
15 rows of 16 px: top bar, 13 map rows (15x13 tiles visible), bottom band.
- Top bar: time, weather icon, temperature on the right; left: faith (flame), food (wheat), wood (log), population (person) as 8x8 icon + 5x7 number (~140 px). If cramped, alternate faith+pop / food+wood every 4 s like GeekQuestAI.
- Camera follows the shaman, leads in the walk direction, cuts briefly to births, deaths, finished buildings, monument stages.
- Bottom band cycles every 4 s: shaman activity (DIGGING POND), DAY 12 OF <WORLD NAME>, village event (BIRTH: TOMA), IP, date.

## Technical budget
- Firmware < 500 KB (CI). Base ≈300 KB -> ≈150 KB for game + art.
- Art ≈40 KB: 4-bit 16x16 tiles (128 B), 8x8 edge pieces (32 B), 16-colour palette per climate.
- RAM ≈4-6 KB static game state; no heap, no libc in gamecore.
- Save 1.8 KB as one multi-slot record (6 x 320 B slots, 300 B of payload each: settings 48, config 64, game 1688) in 0x80000-0x82FFF; valid only if all slots share sequence + CRC; torn-write test extended. Save every 20 min and at each new day.
- 15-30 fps; smooth camera scroll at >= 15 fps; idle scenes redraw only changed strips.

## Releases
r1 repo setup + placeholder scene; r2 world gen/edges/camera/HUD; r3 villagers/needs/roles/homes/food; r4 shaman/terraform/all buildings; r5 weather/sickness/wood; r6 arc/monument/summary/new world/multi-slot save/100-seed balancing.
