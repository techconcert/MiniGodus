// Height-aware walking (r7): 20 simulated days on 20 seeds under ASan/UBSan. Nobody steps across a cliff: every step a villager or the shaman
// takes joins two tiles of the same layer, or of layers one apart over a ramp tile (the game core reports each step to a counter, and the
// test also watches the positions itself every tick). Every building can be reached from the green over the steps and ramps that exist,
// ramps are cut and walked, nobody is put on another tile except where the ground was taken from under him (counted), and the planning
// of the paths stays cheap (flow fills per game minute).
//   MOVE_SEEDS=n MOVE_DAYS=n  shorter runs for quick checks.
#include "gq_test.h"
#include "../lib/gamecore/src/gq_types.h"
#include "../lib/gamecore/src/world.h"
#include "../lib/gamecore/src/village.h"
#include <vector>

enum { Q_POP = 0, Q_UNREACH = 30, Q_RAMPS = 32, Q_CASTS = 18, Q_BLD = 40, Q_BINFO = 110, Q_VIL = 60 };
static int vq(int n) { return (int)gc_query(GQ_Q_VILLAGE + n); }

static void minute(int& wall) {
  wall = (wall + 1) % 1440;
  gc_set_clock(wall / 60, wall % 60, 1);
}

// a step between two adjacent tiles is allowed on the level, or between layers one apart over a ramp
static uint8_t s_map0[WM * WM], s_ramp0[WM * WM / 8];       // the land as it was before the tick (the shaman may raise a tile in the same tick)
static bool allowed_in(const uint8_t* map, const uint8_t* ramp, int x, int y, int nx, int ny) {
  int a = y * WM + x, b = ny * WM + nx;
  int d = MT_H(map[a]) - MT_H(map[b]);
  if (d == 0) return true;
  return (d == 1 || d == -1) && (((ramp[a >> 3] >> (a & 7)) & 1) || ((ramp[b >> 3] >> (b & 7)) & 1));
}
static bool step_allowed(int x, int y, int nx, int ny) {
  return allowed_in(g_map, g_ramp, x, y, nx, ny) || allowed_in(s_map0, s_ramp0, x, y, nx, ny);
}

struct Pos { int x, y; };


// ---------------------------------------------------------------------------------------------------- ramp rules on a hand made map
static void set_tile(int x, int y, int h) { g_map[y * WM + x] = (uint8_t)h; }
static void test_ramp_rules() {
  gc_init(GC_SCENE_VILLAGE, 4242, 0);
  set_tile(5, 5, 4); set_tile(6, 5, 5); set_tile(7, 5, 6); set_tile(5, 6, 4);
  for (int i = 0; i < WM * WM / 8; i++) g_ramp[i] = 0;
  CHECK(world_step_ok(5, 5, 5, 6));                         // the level: yes
  CHECK(!world_step_ok(5, 5, 6, 5));                        // one layer, no ramp: no
  world_set_ramp(6, 5, 1);
  CHECK(world_step_ok(5, 5, 6, 5) && world_step_ok(6, 5, 5, 5) && world_step_ok(6, 5, 7, 5));      // over the ramp: both ways, up or down
  CHECK(!world_step_ok(5, 5, 7, 5));                        // never two layers
  CHECK(world_ramp_used(6, 5));
  set_tile(7, 5, 5); set_tile(5, 5, 6);                     // terraforming: the ramp joins layers 5 and 6 now
  CHECK(world_step_ok(5, 5, 6, 5));
  set_tile(5, 5, 3); set_tile(7, 5, 3);                     // the neighbours drop by two: the ramp bit stays but is no step and is not drawn
  CHECK(world_ramp(6, 5) && !world_step_ok(5, 5, 6, 5) && !world_ramp_used(6, 5));
  world_set_height(6, 5, 1);                                // the tile goes under water: the bit is gone
  CHECK(!world_ramp(6, 5));
  world_set_height(6, 5, 4); world_set_ramp(6, 5, 1); world_set_built(6, 5, 1);
  CHECK(!world_ramp(6, 5));                                 // a building stands there: no ramp
  world_set_built(6, 5, 0);
  world_set_ramp(-1, 3, 1); world_set_ramp(3, 99, 1);       // out of the map: nothing happens
  CHECK(!world_ramp(-1, 3) && !world_ramp(3, 99));
  std::printf("ramp rules: passed\n");
}

// a ramp stays on the screen in the snow and at night
static uint16_t s_shot[240 * 240];
static void render_screen() { for (int s = 0; s < GC_ROWS; s++) { gqt::render_strip_guarded(s); std::memcpy(s_shot + s * GC_STRIP_PIXELS, gqt::strip, GC_STRIP_PIXELS * 2); } }
static int tile_diff(const uint16_t* a, const uint16_t* b, int px, int py) {
  int n = 0;
  for (int y = py; y < py + 16; y++) for (int x = px; x < px + 16; x++) if (a[y * 240 + x] != b[y * 240 + x]) n++;
  return n;
}
static void test_ramp_drawn() {
  const int modes = 3;
  for (int mode = 0; mode < modes; mode++) {
    gc_init(GC_SCENE_VILLAGE, 99, 0);
    gc_debug(GQ_DBG_ADD_VILLAGERS, 0);
    for (int y = 8; y < 14; y++) for (int x = 20; x < 28; x++) set_tile(x, y, y < 11 ? 5 : 4);       // a terrace with its cliff, far from the green
    for (int i = 0; i < WM * WM / 8; i++) g_ramp[i] = 0;
    quarts_rebuild(0, 0, WM - 1, WM - 1);
    gc_debug(GQ_DBG_SET_CAM, 250 + 1024 * 100);
    gc_set_clock(mode == 2 ? 23 : 12, 0, 1);
    if (mode == 0) gc_debug(GQ_DBG_WEATHER, GQ_W_CLEAR);
    if (mode == 1) gc_debug(GQ_DBG_WEATHER, GQ_W_SNOW);
    if (mode == 2) gc_debug(GQ_DBG_WEATHER, GQ_W_CLEAR);
    gc_tick(100);
    world_set_ramp(23, 10, 1);                                // the higher tile above the cliff
    int px = 23 * 16 - 250, py = 10 * 16 - 100 + 16;
    render_screen();
    static uint16_t with[240 * 240];
    std::memcpy(with, s_shot, sizeof with);
    world_set_ramp(23, 10, 0);
    render_screen();
    int diff = tile_diff(with, s_shot, px, py);
    std::printf("ramp drawn (mode %d): %d of 256 pixels differ from the bare cliff\n", mode, diff);
    CHECK(diff >= 60);
    long sum = 0;
    for (int y = py; y < py + 16; y++) for (int x = px; x < px + 16; x++) { uint16_t c = with[y * 240 + x]; sum += ((c >> 11) & 31) + ((c >> 5) & 63) / 2 + (c & 31); }
    static long day_sum = 0;
    if (mode == 0) day_sum = sum;
    if (mode == 2) { std::printf("  night brightness %ld, day %ld\n", sum, day_sum); CHECK(sum < day_sum); }      // the night dims the ramp with the land
  }
}

// an r6 saved game has no ramp bits (the list of the last 10 ramps stands where they are): it loads, the village is cut free at once
static void test_old_save() {
  static uint8_t buf[GC_SAVE_TOTAL];
  gc_init(GC_SCENE_VILLAGE, 31337, 0);
  gc_set_depth(40);
  gc_debug(GQ_DBG_ADD_VILLAGERS, 14);
  int wall = 7 * 60 + 59;
  for (int m = 0; m < 6 * 1440; m++) { minute(wall); for (int t = 0; t < 8; t++) gc_tick(500); if (m % 100 == 0) { gc_debug(GQ_DBG_WOOD, 500); gc_debug(GQ_DBG_FAITH, 300); } }
  CHECK(gc_save_export(buf, sizeof buf) == GC_SAVE_TOTAL);
  int ramps_before = vq(Q_RAMPS);
  int off = GC_SAVE_PART0 + GC_SAVE_PARTN + MAXB * 3;       // part 2: after the 40 buildings
  for (int k = 0; k < WM * WM / 8; k++) buf[off + k] = 0;
  for (int k = 0; k < 10; k++) { buf[off + 2 * k] = 0xFF; buf[off + 2 * k + 1] = 0xFF; }      // r6: ten empty ramp entries
  CHECK(gc_save_import(buf, GC_SAVE_TOTAL) == 1);
  std::printf("old save: ramps before %d, after loading %d, unreachable buildings %d\n", ramps_before, vq(Q_RAMPS), vq(Q_UNREACH));
  CHECK(vq(Q_UNREACH) == 0);
  CHECK(vq(Q_RAMPS) >= 1 || ramps_before == 0);
  for (int m = 0; m < 1440; m++) { minute(wall); for (int t = 0; t < 8; t++) gc_tick(500); }
  CHECK(vq(Q_POP) > 0);
}

int main() {
  test_ramp_rules();
  test_ramp_drawn();
  test_old_save();
  int seeds = 20, days = 20;
  if (const char* e = std::getenv("MOVE_SEEDS")) seeds = std::atoi(e);
  if (const char* e = std::getenv("MOVE_DAYS")) days = std::atoi(e);
  long seen_steps = 0, seen_bad = 0, seen_jumps = 0, long_jumps = 0, ramp_steps = 0, flows_total = 0, minutes_total = 0, ramps_cut = 0, unreach_total = 0;
  uint32_t worst_flows_min = 0;
  int only = std::getenv("MOVE_ONLY") ? std::atoi(std::getenv("MOVE_ONLY")) : 0;
  for (int seed = 1; seed <= seeds; seed++) {
    if (only && seed != only) continue;
    gc_init(GC_SCENE_VILLAGE, (uint32_t)seed * 7919u, 0);
    gc_debug(GQ_DBG_FORCE_LAND, seed % 6); gc_debug(GQ_DBG_FORCE_CLIM, (seed / 2) % 4);
    gc_debug(GQ_DBG_SET_SEED, (int32_t)((uint32_t)seed * 7919u));
    gc_set_depth(40);
    gc_set_live_weather(GQ_W_CLEAR, 180, 0);
    uint32_t bad0 = world_audit_get(2), steps0 = world_audit_get(0), ramp0 = world_audit_get(1), jumps0 = world_audit_get(3), long0 = world_audit_get(4);
    std::vector<Pos> last(MAXV, Pos{-1, -1});
    Pos sh_last = {-1, -1};
    int wall = 7 * 60 + 59;
    long unreach_here = 0;
    for (int d = 0; d < days; d++)
      for (int m = 0; m < 1440; m++) {
        minute(wall);
        uint32_t f0 = world_flow_count();
        // one minute in ten is played in 40 ms ticks (nobody takes two steps in a tick), so the positions can be watched from outside
        bool fine = m % 10 == 0;
        if (fine) { for (auto& l : last) l = Pos{-1, -1}; sh_last = Pos{-1, -1}; }
        for (int t = 0; t < (fine ? 40 : 8); t++) {
          std::memcpy(s_map0, g_map, sizeof s_map0); std::memcpy(s_ramp0, g_ramp, sizeof s_ramp0);
          uint32_t j0 = world_audit_get(3);
          if (std::getenv("MOVE_TRACE")) std::fprintf(stderr, "TICK %d %d %d\n", d, m, t);
          int bd0 = vq(11) + vq(12);
          gc_tick(fine ? 40 : 500);
          if (!fine) continue;
          if (world_audit_get(3) != j0 || vq(11) + vq(12) != bd0) { for (auto& l : last) l = Pos{-1, -1}; sh_last = Pos{-1, -1}; continue; }      // somebody was put on another tile, born or died (an index may be used again): no step
          // the same rule, watched from the outside: positions that differ by one tile must be a legal step
          for (int i = 0; i < MAXV; i++) {
            int q = (int)gc_query(GQ_Q_VILLAGE + Q_VIL + i);
            if (q < 0) { last[i] = Pos{-1, -1}; continue; }
            Pos p = {q & 255, (q >> 8) & 255};
            if (last[i].x >= 0 && std::abs(p.x - last[i].x) + std::abs(p.y - last[i].y) == 1) {
              seen_steps++;
              if (!step_allowed(last[i].x, last[i].y, p.x, p.y)) { seen_bad++; std::printf("seed %d day %d: villager %d stepped (%d,%d)->(%d,%d) layers %d->%d (before %d->%d) ramps now %d,%d before %d,%d jumps %u act %d role %d\n", seed, d, i, last[i].x, last[i].y, p.x, p.y, MT_H(g_map[last[i].y * WM + last[i].x]), MT_H(g_map[p.y * WM + p.x]), MT_H(s_map0[last[i].y * WM + last[i].x]), MT_H(s_map0[p.y * WM + p.x]), world_ramp(last[i].x, last[i].y), world_ramp(p.x, p.y), (s_ramp0[(last[i].y * WM + last[i].x) >> 3] >> ((last[i].y * WM + last[i].x) & 7)) & 1, (s_ramp0[(p.y * WM + p.x) >> 3] >> ((p.y * WM + p.x) & 7)) & 1, world_audit_get(3), (q >> 16) & 15, (q >> 20) & 15); }
            }
            last[i] = p;
          }
          Pos s = {(int)((gc_query(GQ_Q_SHAMAN_X) + 8) >> 4), (int)((gc_query(GQ_Q_SHAMAN_Y) + 8) >> 4)};
          if (sh_last.x >= 0 && std::abs(s.x - sh_last.x) + std::abs(s.y - sh_last.y) == 1) {
            seen_steps++;
            if (!step_allowed(sh_last.x, sh_last.y, s.x, s.y)) { seen_bad++; std::printf("seed %d day %d: shaman stepped (%d,%d)->(%d,%d)\n", seed, d, sh_last.x, sh_last.y, s.x, s.y); }
          }
          sh_last = s;
        }
        uint32_t fl = world_flow_count() - f0;
        if (fl > worst_flows_min) worst_flows_min = fl;
        flows_total += fl; minutes_total++;
        if (m % 113 == 0) for (int s = 0; s < GC_ROWS; s++) gqt::render_strip_guarded(s);
        if (std::getenv("MOVE_TRACE")) { static int lu = 0; int u = vq(Q_UNREACH); if (u != lu) std::fprintf(stderr, "UNREACH %d -> %d at day %d minute %d\n", lu, u, d, m); lu = u; }
        if (m == 1439 && vq(Q_UNREACH) > 0) unreach_here++;
      }
    // a ramp may be in the making at this very minute: give the shaman up to four hours to finish what he is at
    for (int extra = 0; extra < 240 && vq(Q_UNREACH) > 0; extra++) { minute(wall); for (int t = 0; t < 8; t++) gc_tick(500); }
    uint32_t bad = world_audit_get(2) - bad0, steps = world_audit_get(0) - steps0, rs = world_audit_get(1) - ramp0, jumps = world_audit_get(3) - jumps0, longj = world_audit_get(4) - long0;
    int ramps = vq(Q_RAMPS);
    std::printf("seed %2d land %d: steps %6u over ramps %4u bad %u jumps %u (long %u) ramps %2d casts %3d unreachable at midnight %ld, at the end %d\n",
                seed, (int)gc_query(GQ_Q_LANDFORM), steps, rs, bad, jumps, longj, ramps, vq(Q_CASTS), unreach_here, vq(Q_UNREACH));
    if (vq(Q_UNREACH) != 0) {
      world_flow((int)gc_query(GQ_Q_START_X), (int)gc_query(GQ_Q_START_Y));
      for (int b = 0; b < MAXB; b++) {
        int q = vq(Q_BINFO + b);
        if (q < 0) continue;
        int bx = q & 255, by = (q >> 8) & 255, bw = (q >> 16) & 15, bh = (q >> 20) & 15, ok = 0;
        for (int j = -1; j <= bh; j++) for (int i = -1; i <= bw; i++) {
          if (((i == -1 || i == bw) && (j == -1 || j == bh)) || (i >= 0 && i < bw && j >= 0 && j < bh)) continue;
          int x = bx + i, y = by + j;
          if (x >= 0 && y >= 0 && x < WM && y < WM && g_dist[y * WM + x] != 255) ok = 1;
        }
        std::printf("  building %d type %d at %d,%d %dx%d site %d %s\n", b, (q >> 24) & 15, bx, by, bw, bh, (q >> 28) & 1, ok ? "" : "UNREACHABLE");
      }
      { int shx = (int)((gc_query(GQ_Q_SHAMAN_X) + 8) >> 4), shy = (int)((gc_query(GQ_Q_SHAMAN_Y) + 8) >> 4); std::printf("  shaman at %d,%d walkable %d dist-from-bed %d green %d,%d act %d\n", shx, shy, world_walkable(shx, shy), g_dist[shy * WM + shx], (int)gc_query(GQ_Q_START_X), (int)gc_query(GQ_Q_START_Y), (int)gc_query(GQ_Q_HERO_ACT)); }
      for (int y = 0; y < WM; y++) { for (int x = 0; x < WM; x++) { uint8_t t = g_map[y * WM + x]; if ((x == (int)((gc_query(GQ_Q_SHAMAN_X) + 8) >> 4)) && y == (int)((gc_query(GQ_Q_SHAMAN_Y) + 8) >> 4)) { std::putchar('@'); continue; } std::putchar(MT_SOIL(t) == SOIL_BUILT ? '#' : (MT_OBJ(t) == OB_TREE ? 't' : (world_ramp(x, y) ? 'r' : '0' + MT_H(t)))); } std::putchar('\n'); }
    }
    CHECK(bad == 0);
    CHECK(vq(Q_UNREACH) == 0);                       // every building can be reached from the green over steps and ramps
    ramp_steps += rs; seen_jumps += jumps; long_jumps += longj; ramps_cut += ramps; unreach_total += unreach_here;
  }
  std::printf("steps watched from outside %ld (bad %ld), over ramps %ld, ramps cut %ld, jumps %ld (long %ld); flow fills per game minute: mean %ld.%02ld, worst %u\n",
              seen_steps, seen_bad, ramp_steps, ramps_cut, seen_jumps, long_jumps, minutes_total ? flows_total / minutes_total : 0,
              minutes_total ? (flows_total * 100 / minutes_total) % 100 : 0, worst_flows_min);
  CHECK(unreach_total * 10 <= (long)seeds * days);    // at most one midnight in ten finds a building cut off for a moment (a ramp in the making)
  CHECK(seen_bad == 0);
  CHECK(seen_steps > 100 * seeds);
  CHECK(ramps_cut >= seeds);                         // ramps are cut ...
  CHECK(ramp_steps >= seeds * 5);                    // ... and walked
  CHECK(long_jumps <= 2);                            // nobody is put on a far tile (the shaman, walled in, returns to the green: very rare)
  CHECK(seen_jumps * 20 <= (long)seeds * days);      // a jump (more than two tiles away, see world_audit_jump: a step aside is a nudge): at most 0.05 per world and day
  CHECK(flows_total <= 8 * minutes_total && worst_flows_min <= 150);      // the paths are cheap: flow fills per game minute
  std::printf("test_move: all passed\n");
  return 0;
}
