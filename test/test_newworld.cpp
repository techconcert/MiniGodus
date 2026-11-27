// "Start a new world" (gc_new_world, the button on the clock settings page): the game starts from scratch and nothing else is touched.
// Resources, population, statistics and the monument go back to a fresh world; the live weather, the clock and the options stay.
#include "gq_test.h"
#include "../lib/gamecore/src/gq_types.h"
#include "../lib/gamecore/src/world.h"
#include "../lib/gamecore/src/village.h"

static int vq(int n) { return (int)gc_query(GQ_Q_VILLAGE + n); }
static int g_wall;
static void minute() {
  g_wall = (g_wall + 1) % 1440;
  gc_set_clock(g_wall / 60, g_wall % 60, 1);
  for (int i = 0; i < 4; i++) gc_tick(2000);
}

int main() {
  gc_init(GC_SCENE_VILLAGE, 4321, 0);
  gc_debug(GQ_DBG_SET_SEED, 4321);
  gc_set_option(GC_OPT_SLEEP_MODE, 0);
  gc_set_live_weather(GQ_W_RAIN, 123, GC_WXF_FAHRENHEIT);
  g_wall = 8 * 60;
  gc_set_clock(8, 0, 1);
  for (int m = 0; m < 6 * 1440; m++) { minute(); if (m % 60 == 0) { gc_debug(GQ_DBG_FAITH, 3000); gc_debug(GQ_DBG_FOOD, 500); gc_debug(GQ_DBG_WOOD, 300); } }
  gc_debug(GQ_DBG_ADD_VILLAGERS, 8);
  gc_debug(GQ_DBG_MONUMENT, 3);
  gc_debug(GQ_DBG_FAITH, 3000); gc_debug(GQ_DBG_FOOD, 500); gc_debug(GQ_DBG_WOOD, 300);
  CHECK(vq(0) > 2 && vq(1) >= 100 && vq(2) >= 100 && vq(3) >= 1000);
  CHECK(vq(251) == 3 && vq(254) > 1);
  const int day = (int)gc_query(GQ_Q_DAY), live_wx = (int)gc_query(GQ_Q_LIVE_WX), live_t = (int)gc_query(GQ_Q_LIVE_TEMP), live_f = (int)gc_query(GQ_Q_LIVE_FLAGS);
  CHECK(day >= 5 && live_wx == GQ_W_RAIN && live_t == 123 && live_f == GC_WXF_FAHRENHEIT);

  gc_new_world(0);

  // the game is fresh
  CHECK(vq(0) == 2);                                  // two founders
  CHECK(vq(1) == 18 && vq(2) == 12 && vq(3) == 0);    // food, wood, faith of a new world
  CHECK(vq(4) == 0);                                  // water jugs
  CHECK(vq(11) == 0 && vq(12) == 0);                  // deaths, births
  CHECK(vq(16) == 0 && vq(17) == 0 && vq(18) == 0);   // faith earned and spent, casts
  CHECK(vq(250) == 0 && vq(251) == 0);                // play phase, no monument
  CHECK(vq(253) == 2 && vq(254) == 1);                // peak population, buildings built
  CHECK(vq(257) == 0 && vq(258) == 0 && vq(259) == 0 && vq(260) == 0 && vq(262) == 0);
  CHECK(vq(204) > 0 && vq(206) == 0 && vq(38) == 0 && vq(39) == 0 && vq(37) == 0);   // trees stand, nothing felled, nobody ever sick
  CHECK(gc_query(GQ_Q_DAY) == 0);
  // and the rest is as it was: the live weather and the clock reading
  CHECK((int)gc_query(GQ_Q_LIVE_WX) == live_wx && (int)gc_query(GQ_Q_LIVE_TEMP) == live_t && (int)gc_query(GQ_Q_LIVE_FLAGS) == live_f);
  CHECK(gc_query(GQ_Q_WEATHER) == GQ_W_RAIN);         // the game shows the live reading at once
  std::printf("test_newworld: all passed\n");
  return 0;
}
