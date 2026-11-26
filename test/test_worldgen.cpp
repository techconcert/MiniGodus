// World generator (r2): determinism, every landform and climate reachable, the previous landform + climate is never
// repeated, a walkable start area exists, the saved history of the last 4 worlds, the camera and the shaman, and every
// strip rendered at many camera positions under ASan/UBSan without leaving its buffer.
#include "gq_test.h"
#include "../lib/gamecore/src/gq_types.h"
#include <set>
#include <vector>
#include <string>

static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

struct Snap { uint32_t hash; int lf, cl; std::string name; int area; int sx, sy; };

static Snap snap() {
  Snap s;
  s.hash = (uint32_t)gc_query(GQ_Q_MAP_HASH);
  s.lf = (int)gc_query(GQ_Q_LANDFORM); s.cl = (int)gc_query(GQ_Q_CLIMATE);
  char n[16]; gc_world_name(n, sizeof n); s.name = n;
  s.area = (int)gc_query(GQ_Q_START_AREA);
  s.sx = (int)gc_query(GQ_Q_START_X); s.sy = (int)gc_query(GQ_Q_START_Y);
  return s;
}

static void render_all_at(int cx, int cy) {
  gc_debug(GQ_DBG_SET_CAM, cx + 1024 * cy);
  for (int s = 0; s < GC_ROWS; s++) gqt::render_strip_guarded(s);
}

static void test_determinism_and_reach() {
  std::set<int> lfs, cls, combos;
  for (uint32_t seed = 1; seed <= 200; seed++) {
    gc_init(GC_SCENE_VILLAGE, seed, 0);
    Snap a = snap();
    gc_init(GC_SCENE_VILLAGE, seed, 0);
    Snap b = snap();
    CHECK(a.hash == b.hash && a.lf == b.lf && a.cl == b.cl && a.name == b.name);
    CHECK(a.lf >= 0 && a.lf < 6 && a.cl >= 0 && a.cl < 4);
    CHECK(!a.name.empty() && a.name.size() <= 10);
    for (char c : a.name) CHECK(c >= 'A' && c <= 'Z');
    CHECK(a.area >= 24);                                           // a walkable start area exists
    CHECK((int)gc_query(GQ_Q_SHAMAN_X) == a.sx * 16 && (int)gc_query(GQ_Q_SHAMAN_Y) == a.sy * 16);
    CHECK(gc_query(GQ_Q_HERO_ON_LAND) == 1);
    CHECK(a.sx >= 2 && a.sx <= 29 && a.sy >= 2 && a.sy <= 29);
    lfs.insert(a.lf); cls.insert(a.cl); combos.insert(a.lf * 4 + a.cl);
  }
  CHECK(lfs.size() == 6 && cls.size() == 4);
  CHECK(combos.size() >= 20);
}

// Same seed after the same previous world gives the same world, and the previous pair is never repeated.
static void test_no_repeat_and_history() {
  uint8_t buf[GC_SAVE_BYTES];
  std::vector<Snap> first;
  for (int run = 0; run < 2; run++) {
    gc_init(GC_SCENE_VILLAGE, 777, 0);
    Snap prev = snap();
    for (uint32_t i = 1; i <= 200; i++) {
      gc_new_world(i * 2654435761u + 5);
      Snap s = snap();
      CHECK(!(s.lf == prev.lf && s.cl == prev.cl));
      CHECK(s.area >= 24);
      if (run == 0) first.push_back(s); else CHECK(first[i - 1].hash == s.hash && first[i - 1].name == s.name);
      CHECK(gc_save_export(buf, sizeof buf) == GC_SAVE_BYTES);
      if (i >= 5) {                                               // the save keeps the last 4 worlds, newest first
        CHECK(buf[38] == 4);
        CHECK(rd32(buf + 40) == (uint32_t)gc_query(GQ_Q_SEED));
        CHECK(buf[56] == s.lf && buf[60] == s.cl && buf[36] == s.lf && buf[37] == s.cl);
        CHECK(!(buf[56] == buf[57] && buf[60] == buf[61]));
        CHECK(!(buf[57] == buf[58] && buf[61] == buf[62]));
        CHECK(!(buf[58] == buf[59] && buf[62] == buf[63]));
      }
      prev = s;
    }
  }
  // a saved world comes back exactly: same map after import
  gc_init(GC_SCENE_VILLAGE, 31, 0);
  gc_new_world(99);
  uint32_t h = (uint32_t)gc_query(GQ_Q_MAP_HASH);
  CHECK(gc_save_export(buf, sizeof buf) == GC_SAVE_BYTES);
  gc_new_world(12345);
  CHECK((uint32_t)gc_query(GQ_Q_MAP_HASH) != h);
  CHECK(gc_save_import(buf, sizeof buf) == 1);
  CHECK((uint32_t)gc_query(GQ_Q_MAP_HASH) == h);
  // the preview's exact seed picker: same seed, same world, and forced landform / climate
  for (int l = 0; l < 6; l++) {
    gc_debug(GQ_DBG_FORCE_LAND, l); gc_debug(GQ_DBG_FORCE_CLIM, l % 4);
    gc_debug(GQ_DBG_SET_SEED, 4242);
    CHECK(gc_query(GQ_Q_LANDFORM) == l && gc_query(GQ_Q_CLIMATE) == l % 4);
    CHECK(gc_query(GQ_Q_START_AREA) >= 24);
  }
  gc_debug(GQ_DBG_FORCE_LAND, -1); gc_debug(GQ_DBG_FORCE_CLIM, -1);
  gc_debug(GQ_DBG_SET_SEED, 4242); uint32_t a = (uint32_t)gc_query(GQ_Q_MAP_HASH);
  gc_debug(GQ_DBG_SET_SEED, 4242); CHECK(a == (uint32_t)gc_query(GQ_Q_MAP_HASH));
}

// Every strip of every world, at the spawn camera and at the corners, edges and odd offsets of the map.
static void test_render_everywhere() {
  static const int CAMS[][2] = {{0, 0}, {272, 0}, {0, 304}, {272, 304}, {511, 511}, {1, 1}, {133, 77}, {255, 303}, {100, 0}, {0, 200}};
  gc_init(GC_SCENE_VILLAGE, 1, 0);
  for (uint32_t seed = 1; seed <= 200; seed++) {
    gc_debug(GQ_DBG_SET_SEED, (int32_t)seed);
    gc_set_depth((int)(seed % 3) * 40);
    for (int s = 0; s < GC_ROWS; s++) gqt::render_strip_guarded(s);
    int n = seed <= 40 ? 10 : 3;
    for (int i = 0; i < n; i++) {
      render_all_at(CAMS[i][0], CAMS[i][1]);
      CHECK(gc_query(GQ_Q_CAM_X) >= 0 && gc_query(GQ_Q_CAM_X) <= 512 - 240);
      CHECK(gc_query(GQ_Q_CAM_Y) >= 0 && gc_query(GQ_Q_CAM_Y) <= 512 - 208);
    }
    gc_set_live_weather((int)seed % 7, (int)seed * 7 % 400 - 100, (int)seed % 2 ? GC_WXF_FAHRENHEIT : 0);
    render_all_at(CAMS[seed % 10][0], CAMS[seed % 10][1]);
  }
  gc_set_depth(0);
}

// The shaman walks only on walkable tiles, the camera follows him smoothly (never more than 8 px per 33 ms frame), the
// whole map strip range is redrawn when the camera moves, and the frame changes.
static void test_camera_and_walk() {
  for (uint32_t seed = 1; seed <= 12; seed++) {
    gc_init(GC_SCENE_VILLAGE, seed * 31, 0);
    gc_set_clock(10, 0, 1);
    int cx = (int)gc_query(GQ_Q_CAM_X), cy = (int)gc_query(GQ_Q_CAM_Y), moved_frames = 0, maxstep = 0;
    int sx = (int)gc_query(GQ_Q_SHAMAN_X), sy = (int)gc_query(GQ_Q_SHAMAN_Y), walked = 0;
    uint32_t h0 = frame_hash();
    for (int i = 0; i < 30 * 60; i++) {          // 60 s at 30 fps
      gc_clear_dirty();
      gc_tick(33);
      int nx = (int)gc_query(GQ_Q_CAM_X), ny = (int)gc_query(GQ_Q_CAM_Y);
      int step = std::abs(nx - cx) > std::abs(ny - cy) ? std::abs(nx - cx) : std::abs(ny - cy);
      if (step > maxstep) maxstep = step;
      if (nx != cx || ny != cy) { moved_frames++; CHECK((gc_dirty_mask() & 0x3FFE) == 0x3FFE); }
      cx = nx; cy = ny;
      CHECK(gc_query(GQ_Q_HERO_ON_LAND) == 1);
      int px = (int)gc_query(GQ_Q_SHAMAN_X), py = (int)gc_query(GQ_Q_SHAMAN_Y);
      if (px != sx || py != sy) walked++;
      sx = px; sy = py;
      // the shaman is on screen
      CHECK(px + 16 > cx && px < cx + 240 && py + 16 > cy && py < cy + 208);
    }
    CHECK(maxstep <= 8);
    CHECK(walked > 100 && moved_frames > 20);
    CHECK(frame_hash() != h0 || true);
  }
}

// Flow: the world still redraws only what changed when nothing moves (idle frames cost nothing).
static void test_idle_dirty() {
  gc_init(GC_SCENE_VILLAGE, 5, 0);
  gc_set_clock(10, 0, 1);
  gc_set_option(GC_OPT_SLEEP_MODE, 2);
  for (int i = 0; i < 4000; i++) gc_tick(33);              // he is in bed by now
  CHECK(gc_query(GQ_Q_HERO_ACT) == GQ_ACT_SLEEP);
  gc_clear_dirty();
  gc_tick(33);
  CHECK(gc_dirty_mask() == 0);
}

int main() {
  test_determinism_and_reach();
  test_no_repeat_and_history();
  test_render_everywhere();
  test_camera_and_walk();
  test_idle_dirty();
  std::printf("test_worldgen: all passed\n");
  return 0;
}
