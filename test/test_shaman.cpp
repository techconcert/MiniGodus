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
#include <string>
#include <cstring>
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
  CHECK(vq(Q_FAITH) >= 0 && vq(Q_FAITH) <= 9999);
}

// ------------------------------------------------------------------------------------------------ 20 days, 20 seeds
static void test_twenty_days() {
  long total_earned = 0, total_spent = 0;
  int seeds_with_casts = 0, grew = 0, total_casts = 0, total_built = 0, ramps = 0, plots = 0, ponds = 0, bad = 0;
  std::set<int> pers, types;
  for (uint32_t seed = 1; seed <= 20; seed++) {
    gc_init(GC_SCENE_VILLAGE, seed * 6007u, 0);
    gc_debug(GQ_DBG_FORCE_LAND, (int)(seed % 6)); gc_debug(GQ_DBG_FORCE_CLIM, (int)((seed / 2) % 4));          // every landform and climate
    gc_debug(GQ_DBG_SET_SEED, (int32_t)(seed * 6007u));
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
        if (m % 13 == 0) for (auto& b : buildings()) if (!b.site) types.insert(b.tp);
        if (m == 1439 && vq(Q_UNREACH) > 0) unreach_seen++;
      }
    check_rules("end");
    CHECK(vq(Q_UNREACH) == 0);
    int casts = vq(Q_CASTS);
    total_casts += casts; if (casts > 0) seeds_with_casts++;
    total_earned += vq(Q_EARNED); total_spent += vq(Q_SPENT);
    CHECK(casts >= 36);                           // a few dozen tiles are shaped in every world
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
  std::printf("faith earned %ld, spent %ld (%ld%%)\n", total_earned, total_spent, total_earned ? total_spent * 100 / total_earned : 0);
  CHECK(total_spent * 100 >= total_earned * 15);   // r8: 15% (he gives up fewer projects, so fewer wasted casts); r6: the towns are bigger (more faith than the land can take, the cap is 9999) and the rest is banked for the monument; r5 spent 60%
  CHECK(seeds_with_casts == 20);
  CHECK(grew >= 18);                              // the population grows
  CHECK(total_built >= 20 * 5);
  CHECK(pers.size() == 4);                        // every personality turns up
  // r7: a cliff of two layers or more beside the village is a terrace now (people go round it over ramps, test_move checks that nobody crosses one);
  // the shaman still mends the steps he works near; the terrace rule allows a few of them per world, not a wall of cliffs
  CHECK(bad <= 20 * 6);
  CHECK(world_audit_get(2) == 0);                 // no recorded step of any villager or the shaman crossed a cliff or entered a tile nobody can stand on
}

// ------------------------------------------------------------------------------------------------ every building type
static void test_all_buildings() {
  std::set<int> types;
  int shown_pens = 0;
  for (uint32_t seed = 1; seed <= 20; seed++) {
    gc_init(GC_SCENE_VILLAGE, seed * 4003u, 0);
    gc_set_depth(40);
    gc_debug(GQ_DBG_ADD_VILLAGERS, seed % 5 == 0 ? 4 : (seed % 5 == 1 ? 12 : 30));        // some start with a hut's worth, some a cottage's, most a manor's
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
        if (m % 13 == 0) for (auto& b : buildings()) if (!b.site) types.insert(b.tp);       // smaller homes are replaced later: note them while they stand
        if (m % 307 == 0) { for (int s = 0; s < GC_ROWS; s++) gqt::render_strip_guarded(s); check_rules("all"); }
      }
    check_rules("all end");
    if (vq(Q_UNREACH) != 0) std::printf("unreachable buildings: %d (seed %u)\n", vq(Q_UNREACH), seed);
    if (vq(Q_UNREACH) != 0) {
      world_flow((int)gc_query(GQ_Q_START_X), (int)gc_query(GQ_Q_START_Y));
      for (auto& b : buildings()) {
        int ok = 0;
        for (int j = -1; j <= b.h; j++) for (int i = -1; i <= b.w; i++) {
          if (((i == -1 || i == b.w) && (j == -1 || j == b.h)) || (i >= 0 && i < b.w && j >= 0 && j < b.h)) continue;
          int x = b.x + i, y = b.y + j;
          if (x >= 0 && y >= 0 && x < WM && y < WM && g_dist[y * WM + x] != 255) ok = 1;
        }
        std::printf("b x%d y%d w%d h%d tp%d site%d %s\n", b.x, b.y, b.w, b.h, b.tp, b.site, ok ? "" : "UNREACHABLE");
      }
      for (int y = 0; y < WM; y++) { for (int x = 0; x < WM; x++) { uint8_t t = g_map[y * WM + x]; std::putchar(MT_SOIL(t) == SOIL_BUILT ? '#' : (MT_OBJ(t) == OB_TREE ? 't' : '0' + MT_H(t))); } std::putchar('\n'); }
    }
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
    for (int y = 0; y < WM; y++) for (int x = 0; x < WM; x++) if (water(x, y) && std::abs(x - sx) + std::abs(y - sy) <= 16) water_before++;
    if (water_before > 0) continue;
    tried++;
    int wall = 7 * 60 + 59;
    for (int d = 0; d < 6; d++)
      for (int m = 0; m < 1440; m++) { minute(wall); if (m % 60 == 0) gc_debug(GQ_DBG_FAITH, 400); }
    int water_after = 0;
    for (int y = 0; y < WM; y++) for (int x = 0; x < WM; x++) if (water(x, y) && std::abs(x - sx) + std::abs(y - sy) <= 18) water_after++;
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
  uint8_t buf[GC_SAVE_TOTAL];
  CHECK(gc_save_export(buf, sizeof buf) == GC_SAVE_TOTAL);
  int built = vq(Q_BUILT) + vq(Q_SITES);
  gc_init(GC_SCENE_VILLAGE, 5, 0);
  CHECK(gc_save_import(buf, sizeof buf) == 1);
  int after = vq(Q_BUILT) + vq(Q_SITES);
  CHECK(after == built);                                     // every building comes back
  CHECK(vq(Q_UNREACH) == 0 || after > 0);
  check_rules("loaded");
  for (int m = 0; m < 1440; m++) minute(wall);
  check_rules("loaded + day");
}

// ------------------------------------------------------------------------------------------------ purposeful shaman (r8)
// 20 days on 6 seeds: the shaman never wanders aimlessly for long (when he has nothing to build he walks to the shrine, the monument, the
// fields, a home or the green and stands there), the band label is never empty and follows the project, and projects given up stay bounded.
static void test_purposeful() {
  long awake = 0, wander = 0, spots = 0, stands = 0, aborts = 0, turned = 0;
  int longest = 0;
  std::set<std::string> labels;
  for (uint32_t seed = 1; seed <= 6; seed++) {
    gc_init(GC_SCENE_VILLAGE, 1, 0);
    gc_debug(GQ_DBG_SET_SEED, (int32_t)(seed * 7919u + 13));
    gc_set_live_weather(GQ_W_CLEAR, 180, 0);
    int wall = 8 * 60, run = 0;
    for (int d = 0; d < 20; d++)
      for (int m = 0; m < 1440; m++) {
        wall = (wall + 1) % 1440;
        gc_set_clock(wall / 60, wall % 60, 1);
        for (int i = 0; i < 4; i++) gc_tick(2000);
        const char* l = world_state_label();
        CHECK(l && l[0]);                                           // never empty
        labels.insert(l);
        int md = (int)gc_query(GQ_Q_SHAMAN_MODE), pk = vq(Q_PROJ);
        if (md == GQ_SM_SLEEP || md == GQ_SM_BED) { CHECK(!std::strcmp(l, "SLEEPING") || !std::strcmp(l, "GOING TO BED")); continue; }
        awake++;
        if (md == GQ_SM_WANDER) { wander++; if (++run > longest) longest = run; } else run = 0;
        if (md == GQ_SM_TO_SPOT) { spots++; CHECK(!std::strncmp(l, "WALKING", 7)); }
        if (md == GQ_SM_IDLE) { stands++; CHECK(pk == 0); }
        if (pk != 0 && md == GQ_SM_WORK) CHECK(std::strcmp(l, "RESTING") != 0 && std::strncmp(l, "WALKING TO S", 12) != 0 && std::strcmp(l, "PRAYING") != 0);   // working: the project, not an idle label
        if (pk != 0 && md == GQ_SM_TO_SITE) CHECK(std::strstr(l, "ING") != nullptr);
      }
    aborts += vq(248); turned += vq(247);
  }
  std::printf("shaman, 6 worlds x 20 days: awake %ld min, aimless wander %.1f%% (longest run %d min), walking to a place %.1f%%, standing at one %.1f%%, projects given up %ld, sites turned down at planning %ld\n",
              awake, 100.0 * wander / awake, longest, 100.0 * spots / awake, 100.0 * stands / awake, aborts, turned);
  std::printf("labels seen: %d\n", (int)labels.size());
  CHECK(wander * 100 <= awake * 15);
  CHECK(longest <= 60);
  CHECK(aborts <= 6 * 9);                                        // r7: 7.6 per world over 60 worlds, r8: 4.4; six worlds vary from 3 to 7 per world
  CHECK(labels.size() >= 8);
}

int main() {
  test_pond();
  test_rock_and_faith();
  test_save_load();
  test_all_buildings();
  test_twenty_days();
  test_purposeful();
  std::printf("test_shaman: all passed\n");
  return 0;
}
