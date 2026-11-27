// The arc of a world (r6): monument, worship day, summary, extinction, the next world, animals and fish.
//   monument   debug stages 0..5 stand on the map, 3 x 3 tiles, five types over the seeds, art differs per stage
//   worship    stage 5 -> the next day is the worship day (phase 1), everybody gathers; at the end of the day the summary (phase 3)
//   summary    stays through the evening and the night; the morning after (clock awake) a new world starts, very different from the last
//   extinction every villager dead -> summary (phase 4) -> a new world the next morning
//   new world  the last four worlds never repeat landform AND climate of the one before; a debug/web new world starts at once
//   animals    they die (old age) and herds regrow; fish stocks deplete with fishing and regrow
#include "gq_test.h"
#include "../lib/gamecore/src/gq_types.h"
#include "../lib/gamecore/src/world.h"
#include "../lib/gamecore/src/village.h"

enum { Q_POP = 0, Q_FOOD = 1, Q_WOOD = 2, Q_FAITH = 3, Q_BLD = 6, Q_BIRTHS = 12, Q_DEATHS = 11, Q_PEAK = 253, Q_BUILT_TOT = 254, Q_ANIMALS = 255, Q_FISH_STOCK = 256,
       Q_PEN_DEAD = 257, Q_FISH_CAUGHT = 258, Q_PEN_BORN = 260, Q_FISH_BODIES = 261 };
static int vq(int n) { return (int)gc_query(GQ_Q_VILLAGE + n); }
static int phase() { return (int)gc_query(GQ_Q_PHASE); }

static int g_wall;
static void minute() {
  g_wall = (g_wall + 1) % 1440;
  gc_set_clock(g_wall / 60, g_wall % 60, 1);
  for (int i = 0; i < 4; i++) gc_tick(2000);
}
static void run(int minutes) { for (int m = 0; m < minutes; m++) minute(); }
static void run_to(int hh, int mm) { do minute(); while (g_wall != hh * 60 + mm); }
static void start(uint32_t seed) {
  gc_init(GC_SCENE_VILLAGE, seed, 0);
  gc_debug(GQ_DBG_SET_SEED, (int32_t)seed);
  gc_set_live_weather(GQ_W_CLEAR, 180, 0);
  g_wall = 8 * 60 - 1;
  gc_set_clock(7, 59, 1);
}
static uint32_t frame() { return frame_hash(); }

// ---- monument
static void test_monument() {
  int types_seen = 0;
  uint32_t seen_mask = 0;
  for (uint32_t seed = 1; seed <= 40; seed++) {
    start(seed * 31 + 7);
    gc_debug(GQ_DBG_ADD_VILLAGERS, 6);
    run(30);
    CHECK((int)gc_query(GQ_Q_MON_STAGE) == 0);
    for (int st = 1; st <= 5; st++) {
      gc_debug(GQ_DBG_MONUMENT, st);
      CHECK((int)gc_query(GQ_Q_MON_STAGE) == st);
      gc_mark_all_dirty();
      uint32_t h = frame();                                  // renders without overrunning a strip (frame_hash guards every strip)
      (void)h;
    }
    int t = (int)gc_query(GQ_Q_MON_TYPE);
    CHECK(t >= 0 && t < 5);
    seen_mask |= 1u << t;
    gc_debug(GQ_DBG_MONUMENT, 3);                             // going back down is allowed (debug), the building stays 3 x 3
    CHECK((int)gc_query(GQ_Q_MON_STAGE) == 3);
    run(5);
  }
  for (int t = 0; t < 5; t++) types_seen += (seen_mask >> t) & 1;
  CHECK(types_seen == 5);                                     // all five monument types occur over the seeds
  std::printf("monument: 5 stages x 5 types over 40 seeds ok\n");
}

// ---- worship day, summary, the morning after
static void test_worship_and_summary() {
  start(4242);
  gc_debug(GQ_DBG_ADD_VILLAGERS, 12);
  run(60);
  gc_debug(GQ_DBG_MONUMENT, 5);
  CHECK(phase() == 0);
  int seed0 = (int)gc_query(GQ_Q_SEED);
  int land0 = (int)gc_query(GQ_Q_LANDFORM), clim0 = (int)gc_query(GQ_Q_CLIMATE);
  uint32_t play_frame = frame();
  run_to(0, 5);                                               // midnight: the monument is finished, the next day is the worship day
  CHECK(phase() == 1);
  run_to(12, 0);
  CHECK(phase() == 1);
  CHECK(vq(Q_POP) >= 2);
  uint32_t worship_frame = frame();
  CHECK(worship_frame != play_frame);
  run_to(21, 0);                                              // dusk: the worship is over, the summary shows
  CHECK(phase() == 3);
  gc_mark_all_dirty();
  uint32_t sum_frame = frame();
  CHECK(sum_frame != worship_frame && sum_frame != play_frame);
  CHECK(vq(Q_PEAK) >= 12);
  CHECK(vq(Q_BUILT_TOT) >= 1);
  run_to(23, 30);                                             // it stays the whole evening ...
  CHECK(phase() == 3 && (int)gc_query(GQ_Q_SEED) == seed0);
  run_to(3, 0);                                               // ... and the night (the sleep schedule darkens the screen)
  CHECK(phase() == 3 && (int)gc_query(GQ_Q_SEED) == seed0);
  run_to(5, 0);
  CHECK(phase() == 3 && (int)gc_query(GQ_Q_SEED) == seed0);
  run_to(8, 30);                                              // the morning after: a new world
  CHECK(phase() == 0);
  CHECK((int)gc_query(GQ_Q_SEED) != seed0);
  CHECK((int)gc_query(GQ_Q_LANDFORM) != land0 || (int)gc_query(GQ_Q_CLIMATE) != clim0);
  CHECK((int)gc_query(GQ_Q_DAY) == 0 && vq(Q_POP) >= 1 && (int)gc_query(GQ_Q_MON_STAGE) == 0);
  run(60 * 24);
  CHECK(phase() == 0 && vq(Q_POP) >= 1);
  std::printf("worship day and summary ok (world %d %d/%d -> %d %d/%d)\n", seed0, land0, clim0, (int)gc_query(GQ_Q_SEED), (int)gc_query(GQ_Q_LANDFORM), (int)gc_query(GQ_Q_CLIMATE));
}

// the summary that starts in the morning stays until the NEXT morning
static void test_summary_shown_by_day() {
  start(777);
  gc_debug(GQ_DBG_ADD_VILLAGERS, 6);
  run(60);
  int seed0 = (int)gc_query(GQ_Q_SEED);
  gc_debug(GQ_DBG_PHASE, 3);
  CHECK(phase() == 3);
  run_to(15, 0);
  CHECK(phase() == 3 && (int)gc_query(GQ_Q_SEED) == seed0);        // not the same day
  run_to(23, 0);
  CHECK(phase() == 3 && (int)gc_query(GQ_Q_SEED) == seed0);
  run_to(9, 0);
  CHECK(phase() == 0 && (int)gc_query(GQ_Q_SEED) != seed0);
  std::printf("summary stays until the next wake time ok\n");
}

// ---- extinction
static void test_extinction() {
  start(9001);
  gc_debug(GQ_DBG_ADD_VILLAGERS, 8);
  run(120);
  int seed0 = (int)gc_query(GQ_Q_SEED);
  gc_debug(GQ_DBG_EXTINCT, 0);
  run(6);
  CHECK(vq(Q_POP) == 0);
  CHECK(phase() == 4);                                        // the summary comes early
  gc_mark_all_dirty();
  CHECK(frame() != 0);
  run_to(22, 0);
  CHECK(phase() == 4 && (int)gc_query(GQ_Q_SEED) == seed0);
  run_to(8, 0);
  run(30);
  CHECK(phase() == 0 && (int)gc_query(GQ_Q_SEED) != seed0 && vq(Q_POP) >= 1);
  std::printf("extinction summary and new world ok\n");
}

// ---- new worlds
static void test_new_world() {
  start(5150);
  gc_debug(GQ_DBG_ADD_VILLAGERS, 3);
  run(10);
  int pl = (int)gc_query(GQ_Q_LANDFORM), pc = (int)gc_query(GQ_Q_CLIMATE);
  int seeds[40];
  for (int i = 0; i < 40; i++) {
    gc_new_world(0);                                          // what the web button does: at once, from a new seed
    seeds[i] = (int)gc_query(GQ_Q_SEED);
    int l = (int)gc_query(GQ_Q_LANDFORM), c = (int)gc_query(GQ_Q_CLIMATE);
    CHECK(l != pl || c != pc);                                // never the same landform AND climate as the world before
    CHECK((int)gc_query(GQ_Q_DAY) == 0 && phase() == 0 && vq(Q_POP) >= 1);
    for (int k = 0; k < i; k++) CHECK(seeds[k] != seeds[i]);
    pl = l; pc = c;
    run(3);
  }
  std::printf("40 new worlds in a row: never the same landform and climate twice\n");
  // the world itself is for a seed given by hand
  gc_new_world(123456);
  CHECK(gc_query(GQ_Q_SEED) == 123456);
}

// ---- animals and fish: 45 days of a world with a coast
static void test_animals_and_fish() {
  start(150462);
  int fish_prev = -1, fish_down = 0, fish_up = 0, seen_bodies = 0;
  int animals_peak = 0, pen_dead = 0, pen_born = 0;
  int caught = 0;
  bool over = false;
  for (int d = 0; d < 45 && !over; d++) {
    for (int h = 0; h < 24; h++) {
      run(60);
      if (phase() >= 3) { over = true; break; }                // the world ended: the summary shows, the numbers are still those of this world
      if (vq(Q_ANIMALS) > animals_peak) animals_peak = vq(Q_ANIMALS);
      if (vq(Q_PEN_DEAD) > pen_dead) pen_dead = vq(Q_PEN_DEAD);
      if (vq(Q_PEN_BORN) > pen_born) pen_born = vq(Q_PEN_BORN);
      if (vq(Q_FISH_CAUGHT) > caught) caught = vq(Q_FISH_CAUGHT);
      if (vq(Q_FISH_BODIES) > seen_bodies) seen_bodies = vq(Q_FISH_BODIES);
    }
    int fs = vq(Q_FISH_STOCK);
    if (fish_prev >= 0) { if (fs < fish_prev) fish_down++; else if (fs > fish_prev) fish_up++; }
    fish_prev = fs;
  }
  std::printf("animals: peak %d, died %d, born %d; fish: bodies %d, caught %d, stock fell %d days and regrew %d days\n", animals_peak, pen_dead, pen_born, seen_bodies, caught, fish_down, fish_up);
  CHECK(animals_peak >= 4);
  CHECK(pen_born >= 2);                                       // herds regrow when they are fed
  CHECK(pen_dead >= 1);                                       // animals die
  CHECK(seen_bodies >= 1 && caught >= 10);
  CHECK(fish_down >= 2 && fish_up >= 2);                      // fishing depletes a water body, and it regrows
}

int main() {
  test_monument();
  test_worship_and_summary();
  test_summary_shown_by_day();
  test_extinction();
  test_new_world();
  test_animals_and_fish();
  std::printf("test_arc: all passed\n");
  return 0;
}
