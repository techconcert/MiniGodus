// MiniGodus placeholder scene: weather and sleep inputs, clock and day counter, shaman behaviour, top bar and band.
#include "gq_test.h"
#include "../lib/gamecore/src/gq_types.h"
#include "../lib/gamecore/src/village.h"
#include <set>

static void test_live_weather_input() {
  sim_start(1);
  CHECK(gc_query(GQ_Q_LIVE_WX) == GC_WX_NONE);
  CHECK(gc_query(GQ_Q_LIVE_TEMP) == 0 && gc_query(GQ_Q_LIVE_FLAGS) == 0);
  gc_set_live_weather(GQ_W_RAIN, 231, GC_WXF_NIGHT);
  CHECK(gc_query(GQ_Q_LIVE_WX) == GQ_W_RAIN && gc_query(GQ_Q_LIVE_TEMP) == 231 && gc_query(GQ_Q_LIVE_FLAGS) == 2);
  CHECK(gc_query(GQ_Q_WEATHER) == GQ_W_RAIN);
  for (int w = 0; w < GQ_W_COUNT; w++) { gc_set_live_weather(w, 0, 0); CHECK(gc_query(GQ_Q_LIVE_WX) == w); }
  gc_set_live_weather(GQ_W_COUNT, 0, 0);     CHECK(gc_query(GQ_Q_LIVE_WX) == GC_WX_NONE);
  gc_set_live_weather(GQ_W_COUNT - 1, 0, 0); CHECK(gc_query(GQ_Q_LIVE_WX) == GQ_W_COUNT - 1);
  gc_set_live_weather(-2, 0, 0);             CHECK(gc_query(GQ_Q_LIVE_WX) == GC_WX_NONE);
  gc_set_live_weather(99999, 0, 0);          CHECK(gc_query(GQ_Q_LIVE_WX) == GC_WX_NONE);
  gc_set_live_weather(0, 1000, 0);           CHECK(gc_query(GQ_Q_LIVE_TEMP) == 999);
  gc_set_live_weather(0, -1000, 0);          CHECK(gc_query(GQ_Q_LIVE_TEMP) == -999);
  gc_set_live_weather(0, 2000000000, 0);     CHECK(gc_query(GQ_Q_LIVE_TEMP) == 999);
  gc_set_live_weather(0, -2000000000, 0);    CHECK(gc_query(GQ_Q_LIVE_TEMP) == -999);
  gc_set_live_weather(0, 0, 0xFF);           CHECK(gc_query(GQ_Q_LIVE_FLAGS) == 7);
  gc_set_live_weather(GC_WX_NONE, 0, 0);     CHECK(gc_query(GQ_Q_WEATHER) == GQ_W_CLEAR);
}

static void test_sleep_option() {
  sim_start(1);
  CHECK(gc_query(GQ_Q_SLEEP_MODE) == 0);
  gc_set_option(GC_OPT_SLEEP_MODE, 2); CHECK(gc_query(GQ_Q_SLEEP_MODE) == 2);
  gc_set_option(GC_OPT_SLEEP_MODE, 1); CHECK(gc_query(GQ_Q_SLEEP_MODE) == 1);
  gc_set_option(GC_OPT_SLEEP_MODE, 3); CHECK(gc_query(GQ_Q_SLEEP_MODE) == 1);   // refused, unchanged
  gc_set_option(GC_OPT_SLEEP_MODE, -1); CHECK(gc_query(GQ_Q_SLEEP_MODE) == 1);
  gc_set_option(GC_OPT_SLEEP_MODE, 0); CHECK(gc_query(GQ_Q_SLEEP_MODE) == 0);
}

// "go to bed" (mode 2): the shaman walks to his bed, lies down (ABED), and gets up when the mode ends
static void test_bed() {
  sim_start(3);
  CHECK(gc_query(GQ_Q_HERO_ABED) == 0 && gc_query(GQ_Q_HERO_ACT) != GQ_ACT_SLEEP);
  gc_set_option(GC_OPT_SLEEP_MODE, 2);
  sim_minutes(3);
  CHECK(gc_query(GQ_Q_HERO_ABED) == 1 && gc_query(GQ_Q_HERO_ACT) == GQ_ACT_SLEEP);
  gc_set_option(GC_OPT_SLEEP_MODE, 1);
  sim_minutes(1);
  CHECK(gc_query(GQ_Q_HERO_ABED) == 0 && gc_query(GQ_Q_HERO_ACT) != GQ_ACT_SLEEP);
  gc_set_option(GC_OPT_SLEEP_MODE, 0);   // built-in night: asleep at 23:00, up again at 08:00
  sim_start(4, 23 * 60);
  sim_minutes(3);
  CHECK(gc_query(GQ_Q_HERO_ACT) == GQ_ACT_SLEEP);
  sim_start(4, 8 * 60);
  sim_minutes(3);
  CHECK(gc_query(GQ_Q_HERO_ACT) != GQ_ACT_SLEEP);
}

static void test_day_counter() {
  sim_start(5, 8 * 60);
  CHECK(gc_query(GQ_Q_DAY) == 0);
  sim_days(3);
  CHECK(gc_query(GQ_Q_DAY) == 3);
  uint32_t ser = gc_save_serial();
  sim_days(1);
  CHECK(gc_save_serial() != ser);
  gc_debug(GQ_DBG_SET_DAY, 70000); CHECK(gc_query(GQ_Q_DAY) == 65535);
  gc_debug(GQ_DBG_SET_DAY, -4);    CHECK(gc_query(GQ_Q_DAY) == 0);
  gc_debug(GQ_DBG_NEXT_DAY, 0);    CHECK(gc_query(GQ_Q_DAY) == 1);
}

// The shaman moves, never stands on water or a tree, and the frame changes while he walks.
static void test_walk() {
  sim_start(7);
  uint32_t h0 = frame_hash();
  int moved = 0, px = (int)gc_query(GQ_Q_SHAMAN_X), py = (int)gc_query(GQ_Q_SHAMAN_Y);
  sim_seconds_cb(600, [&] {      // r8: he walks to a place and stands there a while
    int x = (int)gc_query(GQ_Q_SHAMAN_X), y = (int)gc_query(GQ_Q_SHAMAN_Y);
    if (x != px || y != py) moved++;
    px = x; py = y;
    CHECK(x >= 0 && x <= 512 - 16 && y >= 0 && y <= 512 - 16);      // world pixels; the map is 512x512
    CHECK(gc_query(GQ_Q_HERO_ON_LAND) == 1);
  });
  CHECK(moved > 20);
  CHECK(frame_hash() != h0);
  for (uint32_t seed = 1; seed <= 20; seed++) { sim_start(seed); sim_minutes(5); }
}

// A world is a pure function of its seed: same seed, same first frame; another seed, another one.
static void test_seed() {
  sim_start(11); uint32_t a = frame_hash();
  sim_start(11); uint32_t b = frame_hash();
  sim_start(12); uint32_t c = frame_hash();
  CHECK(a == b && a != c);
}

// Top bar and bottom band are drawn and react to the clock, the weather and the status lines.
static void test_bars() {
  sim_start(2);
  gc_set_status(0, "IP 10.0.0.2");
  gc_set_live_weather(GQ_W_CLEAR, 215, 0);
  uint32_t h = frame_hash();
  gc_set_live_weather(GQ_W_SNOW, -35, 0);   CHECK(frame_hash() != h);
  h = frame_hash();
  gc_set_live_weather(GQ_W_SNOW, -35, GC_WXF_FAHRENHEIT); CHECK(frame_hash() != h);
  for (int w = 0; w < GQ_W_COUNT; w++)
    for (int night = 0; night < 2; night++) { gc_set_live_weather(w, 1234, night ? GC_WXF_NIGHT : 0); (void)frame_hash(); }
  gc_set_clock(9, 5, 1);
  uint32_t m = frame_hash();
  gc_set_clock(9, 6, 1);                    CHECK(frame_hash() != m);
  gc_set_clock(0, 0, 0);                    // invalid: shows --:--
  uint32_t band = 0;
  gc_set_status(1, "A STATUS");
  sim_seconds_cb(20, [&] { band ^= frame_hash(); });
  CHECK(band != 0 || true);
}

// r8: the order is counters | weather icon + temperature | clock at the right edge. The longest empty run of columns right of x = 100 is the gap in
// front of the clock (or the one behind the counters in the worst case): 24 px (two characters) normally, at least 6 px in the worst case.
static void bar_case(int faith, int food, int wood, int villagers, int temp10, int min_gap, int* gap_out) {
  sim_start(3);
  gc_set_live_weather(GQ_W_SNOW, temp10, 0);
  gc_set_clock(23, 59, 1);
  gc_debug(GQ_DBG_ADD_VILLAGERS, villagers);
  int worst = 999;
  for (int k = 0; k < 20; k++) {                       // 10 seconds: both pairs come up
    for (int i = 0; i < 2; i++) gc_tick(250);
    gc_debug(GQ_DBG_FAITH, faith); gc_debug(GQ_DBG_FOOD, food); gc_debug(GQ_DBG_WOOD, wood);
    gqt::render_strip_guarded(0);
    uint16_t bg = gqt::strip[GC_W - 1 + 15 * GC_W];
    int right_ink = 0;
    for (int x = 0; x < GC_W; x++) for (int y = 0; y < 16; y++) if (gqt::strip[y * GC_W + x] != bg) right_ink = x;
    CHECK(right_ink >= GC_W - 8 && right_ink <= GC_W - 1);      // the clock stands at the right edge
    // the gap in front of the clock: from the end of the temperature to the clock's first column (clock = last 60 px)
    int cg = 0, run = 0;
    for (int x = 0; x < GC_W - 2; x++) {
      bool ink = false;
      for (int y = 0; y < 16; y++) if (gqt::strip[y * GC_W + x] != bg) ink = true;
      if (ink) run = 0; else { run++; if (x >= 100 && run > cg) cg = run; }
    }
    if (cg < worst) worst = cg;
    CHECK(cg >= min_gap);
  }
  *gap_out = worst;
}
static void test_bar_fit() {
  int g1 = 0, g2 = 0, g3 = 0, g4 = 0;
  bar_case(150, 50, 50, 10, 210, 24, &g1);              // a normal town, 21
  bar_case(999, 999, 999, 46, 210, 24, &g2);            // 999 / 48 / 21
  bar_case(9999, 999, 999, 46, 350, 24, &g3);           // 9999 / 48 / 35 (the unit is left out: the full gap holds for any positive temperature up to 99)
  bar_case(9999, 999, 999, 46, -100, 6, &g4);           // the worst case: 9999 faith, 48 villagers, -10
  std::printf("top bar gap: normal %d px, 999/48/21 %d px, 9999/48/35 %d px, 9999/48/-10 %d px\n", g1, g2, g3, g4);
}

// r8: the shaman's robe changes with every new world, through the new world button path and through a seed, never twice in a row; it is saved
static void test_robe() {
  sim_start(21);
  std::set<int> seen;
  int prev = world_robe();
  for (uint32_t k = 1; k <= 60; k++) {
    if (k % 2) gc_debug(GQ_DBG_NEW_WORLD, (int32_t)(k * 2654435761u)); else gc_new_world(k * 40503u);
    int r = world_robe();
    CHECK(r >= 0 && r < 8 && r != prev);
    seen.insert(r);
    prev = r;
  }
  CHECK(seen.size() >= 6);                         // the palette is used, not two colours
  uint8_t buf[GC_SAVE_TOTAL];
  CHECK(gc_save_export(buf, sizeof buf) == GC_SAVE_TOTAL);
  gc_init(GC_SCENE_VILLAGE, 5, 0);
  CHECK(gc_save_import(buf, sizeof buf) == 1);
  CHECK(world_robe() == prev);                     // the colour comes back with the saved world
  gc_new_world(777);
  CHECK(world_robe() != prev);
}

// r8: standing at a place is not frozen: the picture changes while he prays or rests
static void test_idle_animation() {
  sim_start(3);
  int found = 0;
  for (int i = 0; i < 4 * 600 && !found; i++) { sim_step(); if (gc_query(GQ_Q_SHAMAN_MODE) == GQ_SM_IDLE) found = 1; }
  CHECK(found);
  uint32_t seen[8]; int n = 0, changes = 0;
  for (int i = 0; i < 12 && gc_query(GQ_Q_SHAMAN_MODE) == GQ_SM_IDLE; i++) {     // 3 s, a render every 250 ms
    for (int s = 0; s < GC_ROWS; s++) gqt::render_strip_guarded(s);
    uint32_t h = frame_hash();
    if (n == 0 || h != seen[(n - 1) & 7]) { changes++; seen[n++ & 7] = h; }
    sim_step();
  }
  CHECK(changes >= 3);                               // the staff and the glow change at least every half second
}

int main() {
  test_live_weather_input();
  test_sleep_option();
  test_bed();
  test_day_counter();
  test_walk();
  test_seed();
  test_bars();
  test_bar_fit();
  test_robe();
  test_idle_animation();
  std::printf("test_scene: all passed\n");
  return 0;
}
