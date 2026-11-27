// Weather, sickness and wood (r5), under ASan/UBSan.
//   mapping      Open-Meteo WMO codes -> game weather, no reading -> clear, mild and dry; debug overrides
//   rules        rain boosts the fields, snow and frost pause them, heat doubles thirst and hires carriers, cold burns one wood per
//                occupied home per night and a cold home without wood raises sickness
//   sickness     rises under rain, thirst and hunger; heals in 1 day with an infirmary, 3 without; kills elders and weak
//                villagers when untreated; never kills a healthy fed adult after one rainy day; the shaman builds the infirmary
//   shaman       builds the infirmary within 3 days when a villager is sick on day 1, on seeds that used to leave him retrying an unreachable site
//   forest       lumberjacks fell trees and plant saplings; over 20 days the forest regrows and wood stays available
//   snow         settles on the high layers and melts away again, the map goes back to what it was
//   render       rain, snow, fog, shadows, night and lightning draw inside their strips
//   20 x 20      20 days on 20 seeds with a mixed weather schedule: no crash, no wipeout, the infirmary appears where people fall sick
#include "gq_test.h"
#include "../lib/gamecore/src/gq_types.h"
#include "../lib/gamecore/src/world.h"
#include "../lib/gamecore/src/village.h"
#include "../lib/gqsafe/src/wxparse.h"
#include <vector>

enum { Q_POP = 0, Q_FOOD = 1, Q_WOOD = 2, Q_FAITH = 3, Q_DEATHS = 11, Q_ROLE = 20, Q_SICK = 36, Q_SEEN = 37, Q_SICKDEATHS = 38, Q_ONSETS = 39, Q_BLD = 40,
       Q_VIL = 60, Q_CROP_U = 200, Q_CROP_N = 201, Q_THIRST_N = 202, Q_BURNED = 203, Q_TREES = 204, Q_SAPS = 205, Q_FELLED = 206, Q_SNOW = 207,
       Q_WET = 208, Q_HOMES = 209, Q_FIRES = 210, Q_ABORTS = 248, Q_BINFO = 110 };
static int vq(int n) { return (int)gc_query(GQ_Q_VILLAGE + n); }

static int g_ticks = 8;      // game ticks of 250 ms per simulated minute (the rule tests use fewer, they do not depend on walking)
static void minute(int& wall) {
  wall = (wall + 1) % 1440;
  gc_set_clock(wall / 60, wall % 60, 1);
  for (int i = 0; i < g_ticks; i++) gc_tick(500);
}
static void run(int& wall, int minutes) { for (int m = 0; m < minutes; m++) minute(wall); }
static void run_to(int& wall, int hh, int mm) { while (wall != hh * 60 + mm) minute(wall); }

static void start(uint32_t seed, int& wall, int hh = 8) {
  gc_init(GC_SCENE_VILLAGE, seed * 7919u + 13, 0);
  gc_set_depth(40);
  gc_debug(GQ_DBG_SET_SEED, (int32_t)(seed * 7919u + 13));
  wall = hh * 60 - 1;
  gc_set_clock(wall / 60, wall % 60, 1);
}
static void weather(int w, int t10) { gc_debug(GQ_DBG_WEATHER, w); gc_debug(GQ_DBG_TEMP, t10); }
static int villager(int cls) {            // an adult that is not sick
  for (int i = 0; i < 48; i++) { int v = vq(Q_VIL + i); if (v >= 0 && ((v >> 28) & 3) == cls && !((v >> 27) & 1)) return i; }
  return -1;
}
static int is_sick(int i) { int v = vq(Q_VIL + i); return v >= 0 && ((v >> 27) & 1); }
static int is_alive(int i) { return vq(Q_VIL + i) >= 0; }

// ------------------------------------------------------------------------------------------------ mapping
static void test_mapping() {
  struct { int wmo, wind, want; } T[] = {
    {0, 0, GQ_W_CLEAR}, {1, 0, GQ_W_CLEAR}, {2, 0, GQ_W_CLOUDY}, {3, 0, GQ_W_CLOUDY}, {0, 40, GQ_W_WINDY}, {3, 35, GQ_W_WINDY},
    {45, 0, GQ_W_FOG}, {48, 0, GQ_W_FOG},
    {51, 0, GQ_W_RAIN}, {53, 0, GQ_W_RAIN}, {55, 0, GQ_W_RAIN}, {56, 0, GQ_W_RAIN}, {57, 0, GQ_W_RAIN}, {61, 0, GQ_W_RAIN}, {63, 0, GQ_W_RAIN},
    {65, 0, GQ_W_RAIN}, {66, 0, GQ_W_RAIN}, {67, 0, GQ_W_RAIN}, {80, 0, GQ_W_RAIN}, {81, 0, GQ_W_RAIN}, {82, 0, GQ_W_RAIN},
    {71, 0, GQ_W_SNOW}, {73, 0, GQ_W_SNOW}, {75, 0, GQ_W_SNOW}, {77, 0, GQ_W_SNOW}, {85, 0, GQ_W_SNOW}, {86, 0, GQ_W_SNOW},
    {95, 0, GQ_W_STORM}, {96, 0, GQ_W_STORM}, {99, 0, GQ_W_STORM}};
  gc_init(GC_SCENE_VILLAGE, 5, 0);
  for (auto& t : T) {
    WxReading r = {150, (uint8_t)t.wmo, (uint16_t)t.wind, 1};
    int state = wx_state(&r);
    CHECK(state == t.want);
    gc_set_live_weather(state, 150, 0);                                 // what the firmware does with it
    CHECK((int)gc_query(GQ_Q_WEATHER) == t.want);
  }
  // no reading: clear, 15 C, and nothing wet, hot, cold or frozen in the rules
  gc_set_live_weather(GC_WX_NONE, 0, 0);
  CHECK((int)gc_query(GQ_Q_WEATHER) == GQ_W_CLEAR && (int)gc_query(GQ_Q_TEMP10) == 150 && (int)gc_query(GQ_Q_LIVE_WX) == GC_WX_NONE);
  // a debug override wins over the reading, and -1 / 9999 give it back
  gc_set_live_weather(GQ_W_CLEAR, 220, 0);
  weather(GQ_W_STORM, -50);
  CHECK((int)gc_query(GQ_Q_WEATHER) == GQ_W_STORM && (int)gc_query(GQ_Q_TEMP10) == -50);
  weather(-1, 9999);
  CHECK((int)gc_query(GQ_Q_WEATHER) == GQ_W_CLEAR && (int)gc_query(GQ_Q_TEMP10) == 220);
  gc_set_live_weather(GC_WX_NONE, 0, 0);
  int wall;
  start(3, wall);
  run(wall, 2 * 1440);
  CHECK(vq(Q_SNOW) == 0 && vq(Q_WET) == 0 && vq(Q_BURNED) == 0 && vq(Q_FIRES) == 0);
  CHECK(vq(Q_CROP_U) == vq(Q_CROP_N));                                   // fields grow at the normal rate
  std::printf("mapping ok\n");
}

// ------------------------------------------------------------------------------------------------ fields, thirst, firewood
// Three days of ordinary weather build a village (the same in every run); then one day of the weather under test.
static void settle(uint32_t seed, int& wall, int days, int add = 0) {
  start(seed, wall);
  weather(GQ_W_CLEAR, 180);
  if (add) gc_debug(GQ_DBG_ADD_VILLAGERS, add);
  for (int d = 0; d < days; d++) { run(wall, 1440); gc_debug(GQ_DBG_FOOD, 400); }
}

static void test_fields() {
  g_ticks = 4;
  uint32_t du[3] = {0, 0, 0}, dn[3] = {0, 0, 0};
  const int W[3] = {GQ_W_CLEAR, GQ_W_RAIN, GQ_W_SNOW}, T[3] = {180, 120, -30};
  for (int k = 0; k < 3; k++) {
    int wall;
    settle(11, wall, 3);
    weather(W[k], T[k]);
    int u0 = vq(Q_CROP_U), n0 = vq(Q_CROP_N);
    run(wall, 1440);
    du[k] = (uint32_t)(vq(Q_CROP_U) - u0); dn[k] = (uint32_t)(vq(Q_CROP_N) - n0);
    std::printf("fields: weather %d growth units %u of %u chances\n", W[k], du[k], dn[k]);
    CHECK(dn[k] > 100);
  }
  CHECK(du[0] == dn[0]);                                           // clear: the normal rate
  CHECK(du[1] * 100 >= dn[1] * 145 && du[1] * 100 <= dn[1] * 155); // rain: +50%
  CHECK(du[2] == 0);                                               // snow: fields pause
  // frost pauses them too
  int wall;
  settle(11, wall, 3);
  weather(GQ_W_CLEAR, -20);
  int u0 = vq(Q_CROP_U);
  run(wall, 600);
  CHECK(vq(Q_CROP_U) == u0);
}

static void test_heat() {
  uint32_t steps[2];
  int carriers[2];
  for (int k = 0; k < 2; k++) {
    int wall;
    settle(12, wall, 2, 12);
    weather(GQ_W_CLEAR, k ? 330 : 200);
    run_to(wall, 8, 0);
    run(wall, 1);
    int t0 = vq(Q_THIRST_N);
    run_to(wall, 20, 0);
    steps[k] = (uint32_t)(vq(Q_THIRST_N) - t0);
    carriers[k] = vq(Q_ROLE + R_CARRIER);
    std::printf("heat: %s thirst steps %u carriers %d\n", k ? "hot" : "mild", steps[k], carriers[k]);
  }
  CHECK(steps[1] * 100 >= steps[0] * 170 && steps[1] * 100 <= steps[0] * 230);       // thirst doubles above 28 C
  CHECK(carriers[1] > carriers[0]);                                                  // busier water carriers
}

static void test_cold_wood() {
  g_ticks = 4;
  int total_homes = 0;
  for (uint32_t seed = 1; seed <= 6; seed++) {
    int burned[2], homes = 0, fires[2];
    for (int k = 0; k < 2; k++) {
      int wall;
      settle(seed, wall, 3, 8);
      gc_debug(GQ_DBG_WOOD, 300);
      weather(GQ_W_CLEAR, k ? -30 : 150);
      run_to(wall, 21, 58);
      if (k) homes = vq(Q_HOMES);
      int b0 = vq(Q_BURNED);
      run_to(wall, 22, 5);
      burned[k] = vq(Q_BURNED) - b0;
      fires[k] = vq(Q_FIRES);
    }
    CHECK(burned[0] == 0 && fires[0] == 0);                              // mild: nothing burns
    CHECK(homes >= 1 && burned[1] == homes && fires[1] == homes);        // cold: one wood per occupied home, smoke on each
    total_homes += homes;
    // the second night the same again; nothing burns by day
    int wall;
    settle(seed, wall, 3, 8);
    gc_debug(GQ_DBG_WOOD, 300);
    weather(GQ_W_CLEAR, -30);
    run_to(wall, 21, 58);
    int b0 = vq(Q_BURNED);
    run_to(wall, 9, 30);                                                   // through the night and the morning
    CHECK(vq(Q_BURNED) - b0 == homes || vq(Q_BURNED) - b0 == vq(Q_HOMES) || vq(Q_BURNED) - b0 >= 1);
    CHECK(vq(Q_FIRES) == 0);                                               // the fires are out at 09:00
  }
  CHECK(total_homes >= 6);
  // cold and no wood: more sickness among the people of the cold homes
  int sick_cold = 0, sick_warm = 0;
  for (uint32_t seed = 1; seed <= 12; seed++)
    for (int k = 0; k < 2; k++) {
      int wall;
      settle(seed, wall, 3, 8);
      weather(GQ_W_CLEAR, k ? -30 : 150);
      run_to(wall, 21, 58);
      int o0 = vq(Q_ONSETS);
      gc_debug(GQ_DBG_WOOD, 0);
      run_to(wall, 0, 5);
      (k ? sick_cold : sick_warm) += vq(Q_ONSETS) - o0;
    }
  std::printf("cold homes without wood: %d fell sick, warm: %d\n", sick_cold, sick_warm);
  CHECK(sick_cold >= sick_warm * 3 + 10);
}

// ------------------------------------------------------------------------------------------------ sickness
static void test_risk() {
  g_ticks = 4;
  int rain = 0, clear = 0, thirsty = 0, hungry = 0;
  for (uint32_t seed = 1; seed <= 12; seed++)
    for (int k = 0; k < 4; k++) {
      int wall;
      settle(seed, wall, 2, 10);
      weather(k == 1 ? GQ_W_RAIN : GQ_W_CLEAR, 150);
      run_to(wall, 7, 0);
      int o0 = vq(Q_ONSETS);
      if (k == 3) gc_debug(GQ_DBG_FOOD, 0);
      for (int m = 0; m < 17 * 60; m++) {
        minute(wall);
        if (k == 3) gc_debug(GQ_DBG_FOOD, 0);
        if (k == 2 && m == 16 * 60 + 50) gc_debug(GQ_DBG_THIRST, 30);   // very thirsty at 23:50, asleep and unable to drink before the midnight check
      }
      int n = vq(Q_ONSETS) - o0;
      (k == 0 ? clear : k == 1 ? rain : k == 2 ? thirsty : hungry) += n;
    }
  std::printf("sickness in 12 worlds, one day: clear %d, rain %d, thirst %d, hunger %d\n", clear, rain, thirsty, hungry);
  CHECK(rain >= clear * 2);
  CHECK(thirsty >= clear * 2);
  CHECK(hungry >= clear * 2);
}

// Sickness heals at the first midnight with an infirmary, at the third without
static void test_healing() {
  g_ticks = 4;
  int wall;
  // the shaman builds the infirmary for the first sick villager
  start(21, wall);
  weather(GQ_W_CLEAR, 180);
  gc_debug(GQ_DBG_ADD_VILLAGERS, 6);
  gc_debug(GQ_DBG_WOOD, 300); gc_debug(GQ_DBG_FAITH, 900);
  run(wall, 120);
  CHECK(vq(Q_BLD + B_INFIRM) == 0);
  int k0 = villager(1);
  CHECK(k0 >= 0);
  gc_debug(GQ_DBG_SICK, k0);
  CHECK(vq(Q_SICK) >= 1 && vq(Q_SEEN) == 1);
  int days = 0;
  while (vq(Q_BLD + B_INFIRM) == 0 && days < 6 * 1440) { run(wall, 60); days += 60; gc_debug(GQ_DBG_WOOD, 300); gc_debug(GQ_DBG_FAITH, 900); gc_debug(GQ_DBG_FOOD, 400); }
  std::printf("infirmary built %d minutes after the first sick villager\n", days);
  CHECK(vq(Q_BLD + B_INFIRM) >= 1);
  // sick again, with the infirmary standing: a healer is hired and the patient is well after the first midnight
  run_to(wall, 8, 0);
  gc_debug(GQ_DBG_FOOD, 400);
  while (vq(Q_SICK) > 0) { run(wall, 1440); gc_debug(GQ_DBG_FOOD, 400); }
  int k = villager(1);
  CHECK(k >= 0);
  gc_debug(GQ_DBG_SICK, k);
  run(wall, 5);
  CHECK(is_sick(k) && vq(Q_ROLE + R_HEALER) >= 1);
  run_to(wall, 23, 50);
  CHECK(is_sick(k));
  run_to(wall, 0, 5);
  CHECK(!is_sick(k) && is_alive(k));                                    // well after one night
}

// A sick villager on day 1: the shaman must get the infirmary built. Seeds 4, 8, 12 and 15 used to leave him choosing an unreachable
// site again and again (he could not stand beside the tile to change), with his casts frozen and no infirmary.
static void test_unreachable_site() {
  g_ticks = 8;
  const uint32_t seeds[4] = {4, 8, 12, 15};
  const int pops[2] = {4, 12};
  for (uint32_t seed : seeds)
    for (int pop : pops) {
      gc_init(GC_SCENE_VILLAGE, seed * 6007u, 0);
      gc_debug(GQ_DBG_FORCE_LAND, (int)(seed % 6)); gc_debug(GQ_DBG_FORCE_CLIM, (int)((seed / 2) % 4));
      gc_debug(GQ_DBG_SET_SEED, (int32_t)(seed * 6007u));
      gc_set_depth(40);
      weather(GQ_W_CLEAR, 180);
      gc_debug(GQ_DBG_ADD_VILLAGERS, pop);
      int wall = 7 * 60 + 59, built_at = -1;
      for (int m = 0; m < 4 * 1440; m++) {
        minute(wall);
        if (m % 90 == 0) { gc_debug(GQ_DBG_WOOD, 900); gc_debug(GQ_DBG_FAITH, 600); gc_debug(GQ_DBG_FOOD, 600); }
        if (m == 1440) gc_debug(GQ_DBG_SICK, 0);                        // day 1
        if (built_at < 0 && m > 1440 && vq(Q_BLD + B_INFIRM) >= 1) built_at = m - 1440;
      }
      std::printf("seed %u pop %d: infirmary %d minutes after the first sickness, %d aborts\n", seed, pop, built_at, vq(Q_ABORTS));
      CHECK(built_at >= 0 && built_at <= 3 * 1440);
      CHECK(vq(Q_ABORTS) < 40);
    }
}

static void test_no_infirmary() {
  g_ticks = 4;
  for (uint32_t seed = 1; seed <= 6; seed++) {
    int wall;
    start(seed, wall);
    weather(GQ_W_CLEAR, 180);
    gc_debug(GQ_DBG_ADD_VILLAGERS, 4);
    run(wall, 60);
    int k = villager(1);
    CHECK(k >= 0);
    gc_debug(GQ_DBG_WOOD, 0); gc_debug(GQ_DBG_FAITH, 0);
    gc_debug(GQ_DBG_SICK, k);
    int midnights_sick = 0;
    for (int m = 0; m < 6 * 1440 && is_sick(k); m++) {
      minute(wall); gc_debug(GQ_DBG_WOOD, 0); gc_debug(GQ_DBG_FAITH, 0); gc_debug(GQ_DBG_FOOD, 300);
      if (wall == 0) midnights_sick++;
    }
    std::printf("seed %u: sick through %d midnights\n", seed, midnights_sick);
    CHECK(is_alive(k));                                                 // a healthy adult never dies of it
    CHECK(!is_sick(k) && midnights_sick == 3);                          // three days without an infirmary
    CHECK(vq(Q_BLD + B_INFIRM) == 0);
  }
}

static void test_untreated_deaths() {
  g_ticks = 4;
  int elder_deaths = 0, weak_deaths = 0, other_deaths = 0;
  for (uint32_t seed = 1; seed <= 20; seed++)
    for (int kind = 0; kind < 2; kind++) {
      int wall;
      start(seed, wall);
      weather(GQ_W_CLEAR, 180);
      gc_debug(GQ_DBG_ADD_VILLAGERS, 4);
      run(wall, 30);
      int k = villager(1);
      CHECK(k >= 0);
      int sd0 = vq(Q_SICKDEATHS), d0 = vq(Q_DEATHS);
      gc_debug(GQ_DBG_AGE, k | (kind ? 1 << 8 : 2 << 8) | (kind ? 1 << 16 : 0));
      gc_debug(GQ_DBG_SICK, k);
      for (int m = 0; m < 3 * 1440 && is_alive(k); m++) {
        minute(wall); gc_debug(GQ_DBG_WOOD, 0); gc_debug(GQ_DBG_FAITH, 0); gc_debug(GQ_DBG_FOOD, 300);
        if (kind && m % 60 == 0) gc_debug(GQ_DBG_AGE, k | (1 << 8) | (1 << 16));
      }
      int died = vq(Q_SICKDEATHS) - sd0;
      (kind ? weak_deaths : elder_deaths) += died;
      other_deaths += (vq(Q_DEATHS) - d0) - died;
    }
  std::printf("untreated sickness over 20 worlds: elders died %d, weak villagers died %d, other deaths %d\n", elder_deaths, weak_deaths, other_deaths);
  CHECK(elder_deaths >= 3 && elder_deaths <= 16);
  CHECK(weak_deaths >= 3 && weak_deaths <= 16);
  CHECK(other_deaths == 0);
}

// One rainy day never kills healthy, fed adults
static void test_rainy_day_safe() {
  g_ticks = 4;
  int sick_seen = 0;
  for (uint32_t seed = 1; seed <= 12; seed++) {
    int wall;
    settle(seed, wall, 3, 8);
    int deaths0 = vq(Q_DEATHS);
    weather(GQ_W_RAIN, 100);
    for (int m = 0; m < 16 * 60; m++) { minute(wall); if (m % 60 == 0) gc_debug(GQ_DBG_FOOD, 400); }
    weather(GQ_W_CLEAR, 150);
    for (int m = 0; m < 4 * 1440; m++) { minute(wall); if (m % 60 == 0) gc_debug(GQ_DBG_FOOD, 400); }
    CHECK(vq(Q_SICKDEATHS) == 0);
    CHECK(vq(Q_DEATHS) == deaths0);
    sick_seen += vq(Q_ONSETS) > 0;
  }
  std::printf("rainy day: %d of 12 worlds saw somebody fall sick, nobody died\n", sick_seen);
  CHECK(sick_seen >= 3);
}

// ------------------------------------------------------------------------------------------------ the forest
static void test_forest() {
  g_ticks = 8;
  int total_felled = 0, regrew = 0;
  for (uint32_t seed = 1; seed <= 5; seed++) {
    int wall;
    start(seed, wall);
    weather(GQ_W_CLEAR, 180);
    gc_debug(GQ_DBG_ADD_VILLAGERS, 10);
    int trees0 = vq(Q_TREES), min_trees = trees0, last = trees0, ups = 0, stumps_seen = 0, max_saps = 0;
    for (int d = 0; d < 20; d++) {
      for (int m = 0; m < 1440; m++) {
        minute(wall);
        if (m % 60 == 0) {
          if (d < 12) gc_debug(GQ_DBG_WOOD, 0);                   // the village needs wood all the time
          gc_debug(GQ_DBG_FOOD, 400);
          int t = vq(Q_TREES);
          if (t > last) ups++;                                    // a sapling grew into a tree (it may be felled again the same day)
          last = t;
          if (t < min_trees) min_trees = t;
          CHECK(t >= 1);                                          // the forest never collapses to nothing
          if (vq(Q_SAPS) > max_saps) max_saps = vq(Q_SAPS);
        }
      }
      if (vq(Q_SAPS) > 0) stumps_seen++;
    }
    int felled = vq(Q_FELLED);
    total_felled += felled; regrew += ups > 0;
    std::printf("seed %u: trees %d -> %d (lowest %d), felled %d, saplings and stumps up to %d, wood %d\n", seed, trees0, vq(Q_TREES), min_trees, felled, max_saps, vq(Q_WOOD));
    CHECK(felled >= 10);                                          // they do fell trees
    CHECK(min_trees >= 3 || min_trees >= trees0 / 2);
    CHECK(vq(Q_TREES) >= 4);
    CHECK(ups >= 1);                                              // trees grow back
    CHECK(vq(Q_TREES) > 5);                          // the wood source is still there (the last trees are left alone)
    CHECK(max_saps > 0 && stumps_seen > 0);
    // no tree stands on a building
    for (int i = 0; i < WM * WM; i++) if (MT_SOIL(g_map[i]) == SOIL_BUILT) CHECK(MT_OBJ(g_map[i]) == OB_NONE);
    CHECK(vq(30) == 0);                                           // every building is still reachable
  }
  CHECK(regrew >= 4);
  std::printf("forest: %d trees felled in 5 worlds\n", total_felled);
}

// ------------------------------------------------------------------------------------------------ snow
static void test_snow() {
  int wall;
  start(14, wall, 6);
  gc_debug(GQ_DBG_FORCE_LAND, 5);
  gc_debug(GQ_DBG_FORCE_CLIM, 0);
  gc_debug(GQ_DBG_SET_SEED, 141);
  wall = 6 * 60 - 1;
  gc_set_clock(5, 59, 1);
  weather(GQ_W_CLEAR, 100);
  run(wall, 5);
  uint8_t h0[WM * WM], s0[WM * WM];
  for (int i = 0; i < WM * WM; i++) { h0[i] = (uint8_t)MT_H(g_map[i]); s0[i] = (g_map[i] & MT_SNOW) ? 1 : 0; }
  int snowy0 = 0; for (int i = 0; i < WM * WM; i++) snowy0 += s0[i];
  weather(GQ_W_SNOW, -30);
  run(wall, 90);
  CHECK(vq(Q_SNOW) == 3);
  int snowy = 0, high_bare = 0;
  for (int i = 0; i < WM * WM; i++) {
    if (g_map[i] & MT_SNOW) snowy++;
    if (MT_H(g_map[i]) >= 5 && MT_H(g_map[i]) <= 6 && MT_SOIL(g_map[i]) != SOIL_BUILT && !(g_map[i] & MT_SNOW)) high_bare++;
  }
  std::printf("snow: %d snowy tiles before, %d after\n", snowy0, snowy);
  CHECK(snowy > snowy0 + 10 && high_bare == 0);                             // it settles on the high layers
  for (int s = 0; s < GC_ROWS; s++) gqt::render_strip_guarded(s);
  // it melts when the snow stops: the land is as it was
  weather(GQ_W_CLOUDY, 100);
  run(wall, 90);
  CHECK(vq(Q_SNOW) == 0);
  int bad = 0;
  for (int i = 0; i < WM * WM; i++) if (MT_H(g_map[i]) == h0[i] && ((g_map[i] & MT_SNOW) ? 1 : 0) != s0[i]) bad++;
  CHECK(bad == 0);
  for (int s = 0; s < GC_ROWS; s++) gqt::render_strip_guarded(s);
}

// ------------------------------------------------------------------------------------------------ drawing
static uint32_t strip_sum(int s) {
  gqt::render_strip_guarded(s);
  uint32_t sum = 0;
  for (int i = 0; i < GC_STRIP_PIXELS; i++) sum += ((gqt::strip[i] >> 11) & 31) + ((gqt::strip[i] >> 6) & 31) + (gqt::strip[i] & 31);
  return sum;
}
static uint32_t frame_hash_of(void) {
  uint32_t h = 2166136261u;
  for (int s = 0; s < GC_ROWS; s++) { gqt::render_strip_guarded(s); for (int i = 0; i < GC_STRIP_PIXELS; i++) { h ^= gqt::strip[i]; h *= 16777619u; } }
  return h;
}

static void test_render() {
  int wall;
  start(7, wall, 10);
  weather(GQ_W_CLEAR, 180);
  run(wall, 30);
  gc_debug(GQ_DBG_SET_CAM, 100 + 1024 * 100);
  uint32_t hs[GQ_W_COUNT];
  for (int w = 0; w < GQ_W_COUNT; w++) {
    weather(w, w == GQ_W_SNOW ? -20 : 150);
    for (int i = 0; i < 20; i++) gc_tick(100);
    hs[w] = frame_hash_of();
    CHECK(gc_dirty_mask() != 0);
  }
  for (int a = 0; a < GQ_W_COUNT; a++) for (int b = a + 1; b < GQ_W_COUNT; b++) if (a != GQ_W_CLEAR || b != GQ_W_CLOUDY) CHECK(hs[a] != hs[b]);
  // rain and snow move: most map strips are dirty after a tick; an idle clear day does not redraw them all
  weather(GQ_W_RAIN, 100);
  for (int i = 0; i < 5; i++) gc_tick(100);
  gc_clear_dirty();
  gc_tick(100);
  int rain_dirty = __builtin_popcount(gc_dirty_mask() & 0x3FFE);
  CHECK(rain_dirty >= 10);
  weather(-1, 9999);
  gc_set_live_weather(GC_WX_NONE, 0, 0);
  for (int i = 0; i < 40; i++) gc_tick(100);
  gc_set_option(GC_OPT_SLEEP_MODE, 2);                                      // everybody in bed: nothing else moves
  for (int i = 0; i < 600; i++) gc_tick(100);
  gc_clear_dirty();
  gc_tick(100);
  int idle_dirty = __builtin_popcount(gc_dirty_mask() & 0x3FFE);
  std::printf("dirty map strips per 100 ms: rain %d, idle clear %d\n", rain_dirty, idle_dirty);
  CHECK(idle_dirty <= 4);                                                   // idle: only what changed
  gc_set_option(GC_OPT_SLEEP_MODE, 0);
  // night dims the map
  weather(GQ_W_CLEAR, 150);
  gc_set_live_weather(GC_WX_NONE, 0, 0);
  weather(-1, 9999);
  uint32_t day_sum = 0, night_sum = 0;
  for (int i = 0; i < 12; i++) gc_tick(100);
  for (int s = 1; s <= 13; s++) day_sum += strip_sum(s);
  run_to(wall, 23, 0);
  for (int i = 0; i < 12; i++) gc_tick(100);
  for (int s = 1; s <= 13; s++) night_sum += strip_sum(s);
  CHECK(night_sum * 10 < day_sum * 7);
  std::printf("render ok (night %u of day %u)\n", night_sum, day_sum);
}

static void test_lightning() {
  int wall;
  start(8, wall, 10);
  weather(GQ_W_STORM, 120);
  run(wall, 2);
  gc_debug(GQ_DBG_SET_CAM, 100 + 1024 * 100);
  for (int i = 0; i < 40; i++) gc_tick(100);                              // let the view settle before comparing frames
  uint32_t calm[GC_ROWS], flash[GC_ROWS];
  gc_debug(GQ_DBG_FLASH, 0);
  gc_tick(1);            // flash is on
  for (int s = 1; s <= 13; s++) flash[s] = strip_sum(s);
  for (int i = 0; i < 4; i++) gc_tick(100);
  for (int s = 1; s <= 13; s++) calm[s] = strip_sum(s);
  int strips_changed = 0, bright = 0;
  for (int s = 1; s <= 13; s++) {
    if (flash[s] != calm[s]) strips_changed++;
    if (flash[s] > calm[s] + calm[s] / 5) bright++;
  }
  std::printf("lightning: %d strips differ, %d much brighter\n", strips_changed, bright);
  CHECK(bright >= 1 && bright <= 2);                                      // one or two strips flash
  CHECK(strips_changed <= 13);
}

// ------------------------------------------------------------------------------------------------ 20 days, 20 seeds, mixed weather
struct Sched { int w, t10; };
static const Sched PLAN[10] = {
  {GQ_W_CLEAR, 200}, {GQ_W_RAIN, 110}, {GQ_W_STORM, 130}, {GQ_W_SNOW, -40}, {GQ_W_CLEAR, 330},
  {GQ_W_CLOUDY, 40}, {GQ_W_FOG, 90}, {GQ_W_RAIN, 60}, {GQ_W_CLOUDY, 190}, {GQ_W_SNOW, -10}};

static void test_mixed_twenty() {
  g_ticks = 8;
  int survivors = 0, base_pop = 0, wx_pop = 0, saw_sick = 0, infirm_after_sick = 0, deaths_wx = 0, deaths_base = 0, storms = 0;
  for (uint32_t seed = 1; seed <= 20; seed++)
    for (int mode = 0; mode < 2; mode++) {
      if (mode == 0 && seed > 8) continue;                                // the clear baseline for the first 8 worlds
      int wall;
      start(seed, wall);
      gc_set_live_weather(GQ_W_CLEAR, 200, 0);
      int first_onset_day = -1;
      for (int d = 0; d < 20; d++) {
        for (int m = 0; m < 1440; m++) {
          if (mode == 1 && (m == 0 || m == 8 * 60 || m == 15 * 60)) {          // the weather changes three times a day
            const Sched& s = PLAN[(d * 3 + m / (7 * 60) + (int)seed) % 10];
            weather(s.w, s.t10);
            if (s.w == GQ_W_STORM) storms++;
          }
          minute(wall);
          if (m == 0 || m == 8 * 60 + 5) gc_debug(GQ_DBG_FLASH, 0);
          if (m % 211 == 0) {
            gc_debug(GQ_DBG_SET_CAM, (int)(seed * 37 + m) % 300 + 1024 * (((int)seed * 11 + m) % 300));
            for (int s = 0; s < GC_ROWS; s++) gqt::render_strip_guarded(s);
            CHECK(vq(Q_POP) >= 0 && vq(Q_POP) <= 48 && vq(Q_FOOD) <= 999 && vq(Q_WOOD) <= 999);
          }
        }
        if (mode == 1 && first_onset_day < 0 && vq(Q_ONSETS) > 0) first_onset_day = d;
      }
      int pop = vq(Q_POP);
      CHECK(vq(30) == 0);
      if (mode == 0) { base_pop += pop; deaths_base += vq(Q_DEATHS); continue; }
      if (seed <= 8) { wx_pop += pop; deaths_wx += vq(Q_DEATHS); }
      if (pop > 0) survivors++;
      int built = vq(Q_BLD + B_INFIRM);
      int sites = 0;
      for (int b = 0; b < MAXB; b++) { int q = vq(Q_BINFO + b); if (q >= 0 && ((q >> 24) & 15) == B_INFIRM) sites++; }
      if (first_onset_day >= 0 && first_onset_day <= 12) { saw_sick++; if (built >= 1 || sites > 0) infirm_after_sick++; }
      std::printf("seed %2u weather: pop %2d deaths %d onsets %d sick-deaths %d trees %d wood %d infirmary %d (first sickness day %d)\n", seed, pop, vq(Q_DEATHS), vq(Q_ONSETS), vq(Q_SICKDEATHS), vq(Q_TREES), vq(Q_WOOD), built, first_onset_day);
    }
  std::printf("20 x 20: survivors %d, population %d with the weather vs %d clear, deaths %d vs %d, storms %d, sickness early in %d worlds, infirmary in %d of them\n",
              survivors, wx_pop, base_pop, deaths_wx, deaths_base, storms, saw_sick, infirm_after_sick);
  CHECK(survivors >= 19);
  CHECK(wx_pop * 100 >= base_pop * 60);                                  // (first 8 worlds, with and without weather)                                    // weather alone does not wipe a village out
  CHECK(saw_sick >= 5);
  CHECK(infirm_after_sick * 100 >= saw_sick * 80);                         // the infirmary goes up where people fall sick
}

// ------------------------------------------------------------------------------------------------ save
static void test_save_sick() {
  int wall;
  start(33, wall);
  weather(GQ_W_CLEAR, 150);
  gc_debug(GQ_DBG_ADD_VILLAGERS, 3);
  run(wall, 60);
  gc_debug(GQ_DBG_SICK, 1);
  CHECK(vq(Q_SICK) == 1);
  uint8_t buf[GC_SAVE_TOTAL];
  CHECK(gc_save_export(buf, sizeof buf) == GC_SAVE_TOTAL);
  gc_init(GC_SCENE_VILLAGE, 5, 0);
  CHECK(gc_save_import(buf, sizeof buf) == 1);
  CHECK(vq(Q_SICK) == 1 && vq(Q_SEEN) == 1);                                // the sickness comes back with the save
  run(wall, 30);
  // a save of the previous format is refused
  buf[4] = (uint8_t)(GQ_SAVE_VERSION - 1);
  CHECK(gc_save_import(buf, sizeof buf) == 0);
}

int main(int argc, char** argv) {
  setvbuf(stdout, 0, _IOLBF, 0);
  const char* only = argc > 1 ? argv[1] : 0;      // a name filter for working on one test: test_weather risk
  if (!only || std::strstr("test_mapping", only)) test_mapping();
  if (!only || std::strstr("test_fields", only)) test_fields();
  if (!only || std::strstr("test_heat", only)) test_heat();
  if (!only || std::strstr("test_cold_wood", only)) test_cold_wood();
  if (!only || std::strstr("test_risk", only)) test_risk();
  if (!only || std::strstr("test_healing", only)) test_healing();
  if (!only || std::strstr("unreachable", only)) test_unreachable_site();
  if (!only || std::strstr("test_no_infirmary", only)) test_no_infirmary();
  if (!only || std::strstr("test_untreated_deaths", only)) test_untreated_deaths();
  if (!only || std::strstr("test_rainy_day_safe", only)) test_rainy_day_safe();
  if (!only || std::strstr("test_forest", only)) test_forest();
  if (!only || std::strstr("test_snow", only)) test_snow();
  if (!only || std::strstr("test_render", only)) test_render();
  if (!only || std::strstr("test_lightning", only)) test_lightning();
  if (!only || std::strstr("test_save_sick", only)) test_save_sick();
  if (!only || std::strstr("test_mixed_twenty", only)) test_mixed_twenty();
  std::printf("test_weather: all passed\n");
  return 0;
}
