// The shaman (r4): 20 simulated days on 20 seeds under ASan/UBSan. He spends faith, terraforms, lays out buildings that
// builders finish; every building is reachable from the village green, none is bigger than 3x3, none stands on the lowest
// shore land (the fishing huts excepted), faith never goes negative (earned = spent + stock) and the population grows.
// A second run gives every world a crowd and plenty of wood so that every building type is reached in some seed; a third
// digs ponds on worlds without a coast; a fourth checks the saved village (the land is regenerated on load).
#include "gq_test.h"
#include "../lib/gamecore/src/gq_types.h"
#include "../lib/gamecore/src/world.h"
#include "../lib/gamecore/src/village.h"
#include <set>
#include <vector>

enum { Q_POP = 0, Q_FOOD = 1, Q_WOOD = 2, Q_FAITH = 3, Q_BUILT = 6, Q_SITES = 7, Q_CONTENT = 15, Q_EARNED = 16, Q_SPENT = 17, Q_CASTS = 18,
       Q_PROJ = 19, Q_UNREACH = 30, Q_PERS = 31, Q_RAMPS = 32, Q_PONDS = 33, Q_PLOTS = 34, Q_BLD = 40, Q_BINFO = 110 };
static int vq(int n) { return (int)gc_query(GQ_Q_VILLAGE + n); }

static void minute(int& wall) {
  wall = (wall + 1) % 1440;
  gc_set_clock(wall / 60, wall % 60, 1);
  for (int i = 0; i < 8; i++) gc_tick(500);
}

struct Bi { int x, y, w, h, tp, site; };
static std::vector<Bi> buildings() {
  std::vector<Bi> v;
  for (int b = 0; b < MAXB; b++) {
    int q = vq(Q_BINFO + b);
    if (q < 0) continue;
    v.push_back({q & 255, (q >> 8) & 255, (q >> 16) & 15, (q >> 20) & 15, (q >> 24) & 15, (q >> 28) & 1});
  }
  return v;
}
static int water(int x, int y) { return x >= 0 && y >= 0 && x < WM && y < WM && MT_H(g_map[y * WM + x]) <= 1; }

static int bad_slopes_near_village(void) {       // neighbouring tiles that differ by more than one layer within 5 tiles of a building
  int n = 0;
  std::vector<Bi> bs = buildings();
  for (int y = 0; y < WM - 1; y++)
    for (int x = 0; x < WM - 1; x++) {
      bool near = false;
      for (auto& b : bs) if (x >= b.x - 3 && x <= b.x + b.w + 2 && y >= b.y - 3 && y <= b.y + b.h + 2) near = true;
      if (!near) continue;
      int h = MT_H(g_map[y * WM + x]);
      if (std::abs(h - MT_H(g_map[y * WM + x + 1])) > 1) n++;
      if (std::abs(h - MT_H(g_map[(y + 1) * WM + x])) > 1) n++;
    }
  return n;
}

// Rules that hold at every moment.
static void check_rules(const char* what) {
  std::vector<Bi> bs = buildings();
  for (auto& b : bs) {
    CHECK(b.w >= 1 && b.w <= 3 && b.h >= 1 && b.h <= 3);                   // nothing over 3x3
    CHECK(b.x >= 1 && b.y >= 1 && b.x + b.w < WM && b.y + b.h < WM);
    for (int j = 0; j < b.h; j++)
      for (int i = 0; i < b.w; i++) {
        int x = b.x + i, y = b.y + j, h = MT_H(g_map[y * WM + x]);
        if (b.tp == B_FISH) continue;                                      // a fishing hut stands on the shore
        if (h < 3) { std::printf("%s: building type %d at %d,%d on layer %d\n", what, b.tp, x, y, h); CHECK(h >= 3); }
        CHECK(!(h == 2 && (water(x - 1, y) || water(x + 1, y) || water(x, y - 1) || water(x, y + 1))));
        CHECK(h <= 6);
      }
  }
  CHECK((int)gc_query(GQ_Q_VILLAGE + Q_EARNED) == vq(Q_FAITH) + vq(Q_SPENT));      // faith never goes negative
  CHECK(vq(Q_FAITH) >= 0 && vq(Q_FAITH) <= 999);
}

// ------------------------------------------------------------------------------------------------ 20 days, 20 seeds
static void test_twenty_days() {
  int seeds_with_casts = 0, grew = 0, total_casts = 0, total_built = 0, ramps = 0, plots = 0, ponds = 0, bad = 0;
  std::set<int> pers, types;
  for (uint32_t seed = 1; seed <= 20; seed++) {
    gc_init(GC_SCENE_VILLAGE, seed * 6007u, 0);
    gc_set_depth(40);
    gc_set_live_weather(GQ_W_CLEAR, 180, 0);
    pers.insert(vq(Q_PERS));
    int wall = 7 * 60 + 59, unreach_seen = 0;
    for (int d = 0; d < 20; d++)
      for (int m = 0; m < 1440; m++) {
        minute(wall);
        if (m % 113 == 0) {
          gc_debug(GQ_DBG_SET_CAM, (int)(seed * 37 + m) % 300 + 1024 * (((int)seed * 11 + m) % 300));
          for (int s = 0; s < GC_ROWS; s++) gqt::render_strip_guarded(s);
          check_rules("run");
        }
        if (m == 1439 && vq(Q_UNREACH) > 0) unreach_seen++;
      }
    check_rules("end");
    CHECK(vq(Q_UNREACH) == 0);
    int casts = vq(Q_CASTS);
    total_casts += casts; if (casts > 0) seeds_with_casts++;
    if (vq(Q_POP) > 2) grew++;
    total_built += vq(Q_BUILT);
    ramps += vq(Q_RAMPS); plots += vq(Q_PLOTS); ponds += vq(Q_PONDS);
    int slopes = bad_slopes_near_village();
    bad += slopes;
    for (auto& b : buildings()) types.insert(b.tp);
    std::printf("seed %2u land %d pers %d: pop %2d food %3d wood %3d faith %3d spent %4d casts %3d built %2d sites %d ramps %d plots %d ponds %d slopes %d unreach-at-midnight %d\n",
                seed, (int)gc_query(GQ_Q_LANDFORM), vq(Q_PERS), vq(Q_POP), vq(Q_FOOD), vq(Q_WOOD), vq(Q_FAITH), vq(Q_SPENT), casts, vq(Q_BUILT), vq(Q_SITES), vq(Q_RAMPS), vq(Q_PLOTS), vq(Q_PONDS), slopes, unreach_seen);
  }
  std::printf("20 days: casts %d, built %d, plots %d, ponds %d, bad slopes %d, building types seen %d\n", total_casts, total_built, plots, ponds, bad, (int)types.size());
  CHECK(seeds_with_casts >= 15);                  // he spends faith and terraforms in most worlds
  CHECK(grew >= 18);                              // the population grows
  CHECK(total_built >= 20 * 5);
  CHECK(pers.size() == 4);                        // every personality turns up
  CHECK(bad <= 20 * 6);                           // slopes are mended (a tile pulled both ways is left alone)
}

// ------------------------------------------------------------------------------------------------ every building type
static void test_all_buildings() {
  std::set<int> types;
  int shown_pens = 0;
  for (uint32_t seed = 1; seed <= 20; seed++) {
    gc_init(GC_SCENE_VILLAGE, seed * 4003u, 0);
    gc_set_depth(40);
    gc_debug(GQ_DBG_ADD_VILLAGERS, 30);
    int wall = 7 * 60 + 59;
    for (int d = 0; d < 8; d++)
      for (int m = 0; m < 1440; m++) {
        minute(wall);
        if (m % 90 == 0) {
          gc_debug(GQ_DBG_WOOD, (seed % 3 == 0 && d >= 1) ? 3 : 900);             // some worlds run out of wood
          gc_debug(GQ_DBG_FOOD, 400);
          gc_debug(GQ_DBG_FAITH, 600);
          if (seed % 3 == 1 && d == 2 && m == 0) gc_debug(GQ_DBG_THIRST, 30);         // somebody falls weak: an infirmary
        }
        if (m % 307 == 0) { for (int s = 0; s < GC_ROWS; s++) gqt::render_strip_guarded(s); check_rules("all"); }
      }
    check_rules("all end");
    CHECK(vq(Q_UNREACH) == 0);
    for (auto& b : buildings()) if (!b.site) { types.insert(b.tp); if (b.tp == B_PEN) shown_pens++; }
  }
  const char* names[B_COUNT] = {"tent", "hut", "cottage", "manor", "field", "orchard", "pen", "fishing hut", "well", "lumberjack hut", "granary", "shrine", "infirmary"};
  for (int t = 0; t < B_COUNT; t++) std::printf("%-15s %s\n", names[t], types.count(t) ? "built" : "NEVER");
  for (int t = 1; t < B_COUNT; t++) CHECK(types.count(t));      // (the tent is the start building and is replaced by bigger homes)
}

// ------------------------------------------------------------------------------------------------ ponds
static void test_pond() {
  int ponds = 0, tried = 0;
  for (uint32_t seed = 1; seed <= 12; seed++) {
    gc_init(GC_SCENE_VILLAGE, 1, 0);
    gc_debug(GQ_DBG_FORCE_LAND, 5);                     // mountain plateau: usually no coast
    gc_debug(GQ_DBG_SET_SEED, (int32_t)(seed * 977u));
    gc_set_depth(40);
    int sx = (int)gc_query(GQ_Q_START_X), sy = (int)gc_query(GQ_Q_START_Y);
    int water_before = 0;
    for (int y = 0; y < WM; y++) for (int x = 0; x < WM; x++) if (water(x, y) && std::abs(x - sx) + std::abs(y - sy) <= 12) water_before++;
    if (water_before > 0) continue;
    tried++;
    int wall = 7 * 60 + 59;
    for (int d = 0; d < 6; d++)
      for (int m = 0; m < 1440; m++) { minute(wall); if (m % 60 == 0) gc_debug(GQ_DBG_FAITH, 400); }
    int water_after = 0;
    for (int y = 0; y < WM; y++) for (int x = 0; x < WM; x++) if (water(x, y) && std::abs(x - sx) + std::abs(y - sy) <= 16) water_after++;
    std::printf("pond seed %u: ponds dug %d, water tiles near the village %d\n", seed, vq(Q_PONDS), water_after);
    if (vq(Q_PONDS) > 0) { ponds++; CHECK(water_after >= 6); }
    check_rules("pond");
    CHECK(vq(Q_UNREACH) == 0);
  }
  CHECK(tried >= 3 && ponds >= 2);
}

// ------------------------------------------------------------------------------------------------ rock and faith
static void test_rock_and_faith() {
  gc_init(GC_SCENE_VILLAGE, 321, 0);
  std::vector<uint8_t> rock;
  for (int i = 0; i < WM * WM; i++) rock.push_back(MT_H(g_map[i]) == 7 ? 1 : 0);
  gc_debug(GQ_DBG_ADD_VILLAGERS, 20);
  gc_debug(GQ_DBG_WOOD, 900);
  int wall = 7 * 60 + 59, last_spent = 0;
  for (int m = 0; m < 3 * 1440; m++) {
    minute(wall);
    if (m % 200 == 0) gc_debug(GQ_DBG_WOOD, 900);
    int sp = vq(Q_SPENT);
    CHECK(sp >= last_spent);
    last_spent = sp;
    CHECK((int)gc_query(GQ_Q_VILLAGE + Q_EARNED) == vq(Q_FAITH) + sp);
  }
  for (int i = 0; i < WM * WM; i++) if (rock[i]) CHECK(MT_H(g_map[i]) == 7);          // rock is untouchable
  CHECK(vq(Q_SPENT) > 0);
}

// ------------------------------------------------------------------------------------------------ save and load
static void test_save_load() {
  gc_init(GC_SCENE_VILLAGE, 8191, 0);
  gc_debug(GQ_DBG_ADD_VILLAGERS, 12);
  int wall = 7 * 60 + 59;
  for (int m = 0; m < 4 * 1440; m++) { minute(wall); if (m % 100 == 0) { gc_debug(GQ_DBG_WOOD, 500); gc_debug(GQ_DBG_FAITH, 300); } }
  uint8_t buf[GC_SAVE_BYTES];
  CHECK(gc_save_export(buf, sizeof buf) == GC_SAVE_BYTES);
  int built = vq(Q_BUILT) + vq(Q_SITES);
  gc_init(GC_SCENE_VILLAGE, 5, 0);
  CHECK(gc_save_import(buf, sizeof buf) == 1);
  int after = vq(Q_BUILT) + vq(Q_SITES);
  CHECK(after == (built < 18 ? built : 18));
  CHECK(vq(Q_UNREACH) == 0 || after > 0);
  check_rules("loaded");
  for (int m = 0; m < 1440; m++) minute(wall);
  check_rules("loaded + day");
}

int main() {
  test_pond();
  test_rock_and_faith();
  test_save_load();
  test_all_buildings();
  test_twenty_days();
  std::printf("test_shaman: all passed\n");
  return 0;
}
