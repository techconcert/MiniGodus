// Villagers (r3): 10 simulated days on 20 seeds under ASan/UBSan (positions, population, stocks stay valid, the strips
// render without overruns), meals at real clock times, hunger -> weak -> death, sleeping on the clock's schedule, births
// and a save round trip of the village.
#include "gq_test.h"
#include "../lib/gamecore/src/gq_types.h"
#include <vector>

enum { Q_POP = 0, Q_FOOD = 1, Q_WOOD = 2, Q_FAITH = 3, Q_WATER = 4, Q_WEAK = 5, Q_BUILT = 6, Q_SITES = 7, Q_CHILD = 8, Q_ADULTS = 9, Q_ELDERS = 10,
       Q_DEATHS = 11, Q_BIRTHS = 12, Q_BEDS = 13, Q_ASLEEP = 14, Q_ROLE = 20, Q_BLD = 40, Q_VIL = 60 };
static int vq(int n) { return (int)gc_query(GQ_Q_VILLAGE + n); }

// one wall minute: the clock moves, the game ticks for 4 simulated seconds in 8 steps
static void minute(int& wall) {
  wall = (wall + 1) % 1440;
  gc_set_clock(wall / 60, wall % 60, 1);
  for (int i = 0; i < 8; i++) gc_tick(500);
}

static void check_sane() {
  int pop = vq(Q_POP);
  CHECK(pop >= 0 && pop <= 48);
  CHECK(vq(Q_FOOD) <= 999 && vq(Q_WOOD) <= 999 && vq(Q_FAITH) <= 999);
  int seen = 0;
  for (int i = 0; i < 48; i++) {
    int v = vq(Q_VIL + i);
    if (v < 0) continue;
    seen++;
    int x = v & 255, y = (v >> 8) & 255;
    CHECK(x < 32 && y < 32);
  }
  CHECK(seen == pop);
  CHECK(vq(Q_ADULTS) + vq(Q_CHILD) + vq(Q_ELDERS) == pop);
}

static void test_ten_days_twenty_seeds() {
  int total_pop = 0, survivors = 0, built = 0;
  for (uint32_t seed = 1; seed <= 20; seed++) {
    gc_init(GC_SCENE_VILLAGE, seed * 7919u, 0);
    gc_set_depth(40);
    gc_set_live_weather(GQ_W_CLEAR, 180, 0);
    CHECK(vq(Q_POP) == 2 && vq(Q_ADULTS) == 2);                  // one couple to start with
    int wall = 7 * 60 + 59;
    for (int d = 0; d < 10; d++)
      for (int m = 0; m < 1440; m++) {
        minute(wall);
        if (m % 97 == 0) {
          gc_debug(GQ_DBG_SET_CAM, (int)(seed * 37 + m) % 300 + 1024 * (((int)seed * 11 + m) % 300));
          for (int s = 0; s < GC_ROWS; s++) gqt::render_strip_guarded(s);
          check_sane();
        }
      }
    check_sane();
    total_pop += vq(Q_POP);
    if (vq(Q_POP) > 0) survivors++;
    built += vq(Q_BUILT);
    std::printf("seed %2u: pop %2d (births %d deaths %d) food %3d wood %3d faith %3d buildings %d\n", seed, vq(Q_POP), vq(Q_BIRTHS), vq(Q_DEATHS), vq(Q_FOOD), vq(Q_WOOD), vq(Q_FAITH), vq(Q_BUILT));
  }
  CHECK(survivors >= 18);                                         // the economy carries a village through 10 days
  CHECK(built >= 20 * 3);                                         // and they build
}

static void test_meals_hunger_sleep() {
  gc_init(GC_SCENE_VILLAGE, 4242, 0);
  gc_debug(GQ_DBG_FOOD, 100);
  int wall = 7 * 60 + 29;
  gc_set_clock(7, 29, 1);
  for (int i = 0; i < 8; i++) gc_tick(500);
  int before = vq(Q_FOOD);
  minute(wall);                                                   // 07:30: breakfast, two people
  CHECK(wall == 450 && vq(Q_FOOD) == before - 2);
  while (wall != 12 * 60 + 29) minute(wall);
  int f = vq(Q_FOOD);
  minute(wall);                                                   // 12:29 -> 12:30
  CHECK(wall == 12 * 60 + 30);
  CHECK(vq(Q_FOOD) <= f - 2);                                     // lunch costs 2 more (fields may add some)
  // nobody eats while the world sleeps: jump to the night, meals at 19:00 happen, then the 22:00 sleep
  while (wall != 22 * 60 + 5) minute(wall);
  CHECK(vq(Q_ASLEEP) >= 1);
  // hunger: no food at all, 4 missed meals make them weak, 6 kill
  gc_init(GC_SCENE_VILLAGE, 4243, 0);
  gc_debug(GQ_DBG_FOOD, 0);
  wall = 6 * 60;
  gc_set_clock(6, 0, 1);
  int missed = 0, weak_seen = 0;
  for (int m = 0; m < 3 * 1440 && vq(Q_POP) > 0; m++) {
    gc_debug(GQ_DBG_FOOD, 0);
    minute(wall);
    if (wall == 7 * 60 + 30 || wall == 12 * 60 + 30 || wall == 19 * 60) missed++;
    if (vq(Q_WEAK) > 0 && !weak_seen) { weak_seen = missed; }
    if (vq(Q_POP) == 0) break;
  }
  std::printf("weak_seen %d missed %d pop %d deaths %d\n", weak_seen, missed, vq(Q_POP), vq(Q_DEATHS));
  CHECK(weak_seen == 4);                                          // weak after 4 missed meals
  CHECK(vq(Q_POP) == 0 && missed <= 7);                           // dead after 6 (7 with a healer, there is none)
  CHECK(vq(Q_DEATHS) == 2);
}

static void test_births_and_growth() {
  gc_init(GC_SCENE_VILLAGE, 777, 0);
  gc_debug(GQ_DBG_FOOD, 400);
  gc_debug(GQ_DBG_WOOD, 400);
  int wall = 5 * 60;
  gc_set_clock(5, 0, 1);
  int births = 0;
  for (int d = 0; d < 9; d++) {
    for (int m = 0; m < 1440; m++) { minute(wall); if (m % 200 == 0) { gc_debug(GQ_DBG_FOOD, 600); } }
    births = vq(Q_BIRTHS);
  }
  CHECK(births >= 1);
  CHECK(vq(Q_CHILD) + vq(Q_ADULTS) + vq(Q_ELDERS) == vq(Q_POP));
  CHECK(vq(Q_POP) <= 48);
}

static void test_save_round_trip() {
  gc_init(GC_SCENE_VILLAGE, 99, 0);
  gc_debug(GQ_DBG_ADD_VILLAGERS, 4);
  int wall = 8 * 60;
  gc_set_clock(8, 0, 1);
  for (int m = 0; m < 600; m++) minute(wall);
  uint8_t buf[GC_SAVE_BYTES];
  CHECK(gc_save_export(buf, sizeof buf) == GC_SAVE_BYTES);
  int pop = vq(Q_POP), food = vq(Q_FOOD), wood = vq(Q_WOOD), faith = vq(Q_FAITH), built = vq(Q_BUILT) + vq(Q_SITES);
  gc_init(GC_SCENE_VILLAGE, 5, 0);
  CHECK(gc_save_import(buf, sizeof buf) == 1);
  CHECK(vq(Q_FOOD) == food && vq(Q_WOOD) == wood && vq(Q_FAITH) == faith);
  CHECK(vq(Q_POP) == (pop < 16 ? pop : 16));
  CHECK(vq(Q_BUILT) + vq(Q_SITES) == (built < 18 ? built : 18));
  check_sane();
  for (int m = 0; m < 300; m++) minute(wall);
  check_sane();
  // garbage in the village bytes never breaks the game: flip bytes, fix the CRC, import
  for (int k = 0; k < 40; k++) {
    uint8_t b2[GC_SAVE_BYTES];
    for (int i = 0; i < GC_SAVE_BYTES; i++) b2[i] = buf[i];
    for (int i = 64; i < 192; i++) b2[i] = (uint8_t)(b2[i] * 31u + k * 17u + (i ^ k));
    b2[64] = k % 3 ? 0x56 : b2[64];
    b2[65] = (uint8_t)(k % 17);
    b2[66] = (uint8_t)(k % 19);
    uint32_t c = 0xFFFFFFFFu;
    for (int i = 12; i < 192; i++) { c ^= b2[i]; for (int j = 0; j < 8; j++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    c = ~c;
    std::memcpy(b2 + 8, &c, 4);
    if (gc_save_import(b2, sizeof b2) == 1) {
      for (int m = 0; m < 120; m++) minute(wall);
      check_sane();
    }
  }
}

int main() {
  test_meals_hunger_sleep();
  test_births_and_growth();
  test_save_round_trip();
  test_ten_days_twenty_seeds();
  std::printf("test_village: all passed\n");
  return 0;
}
