// MiniGodus scene (releases r2 and r4): a generated 32x32 world, a camera that follows a placeholder shaman, the top bar with
// time, weather, temperature and the four resource counters, and the bottom band.
// The villagers, buildings and faith economy arrive in later releases; the interface to the firmware stays.
// Rules for the core: no heap, no libc, no floating point.
#include "village.h"
#include "village_art_ids.h"
#include "monument_art_ids.h"

GqState G;
uint8_t g_wframe;

enum { BAND_STRIP = GC_ROWS - 1, MAP_STRIPS = 0x3FFE /* strips 1..13 */, MOVE_MS = 25, WATER_MS = 600, BAND_MS = 3000, HUD_ALT_MS = 3500,
       CAM_W = GC_W, CAM_H = 13 * GC_STRIP_H };

struct Shaman { int16_t x, y; uint8_t dir, step, moved, act, abed; };
static Shaman   s_sh;                              // world pixels (the map is 512x512)
static int16_t  s_mx, s_my;                        // pixel target of the step in progress
static uint8_t  s_moving, s_walking, s_goal_x, s_goal_y, s_goal_adj;
static uint8_t  s_work, s_wtx, s_wty, s_applied, s_retry;       // the shaman's project: 1 walking beside the tile, 2 casting
static int      s_cast, s_band_base;
enum { CAST_MS = 800 };
static int      s_cam_x, s_cam_y, s_cam_free;     // camera (top left, world pixels)
static uint32_t s_move_acc, s_water_acc, s_band_acc, s_hud_acc, s_pause, s_play_ms;
static uint8_t  s_hud_alt;
static int      s_band_cur;                       // 0..3 status line, 4 = day line, 5 = the shaman's project
static int      s_last_min = -1;
static uint8_t  s_going_to_bed;
static int8_t   s_force_l = -1, s_force_c = -1;
static uint32_t s_next_seed = 0x2545F491u;
static uint32_t s_sum_ms;                          // time on the summary screen (not saved)
enum { SUM_NOCLOCK_MS = 12 * 3600 * 1000, SUM_MAX_MS = 36 * 3600 * 1000 };   // no valid clock: 12 h of uptime; a schedule that never sleeps: 36 h
static uint8_t  s_ph_seen, s_sum_slept;          // the phase the screen was drawn for; the summary has seen the night
static uint8_t  s_imp;                           // a saved game is being imported part by part
// the four counters of the top bar, by icon id: faith, food, wood, population (village_stat numbers)
static const uint8_t HUD_STAT[4] = {3, 1, 2, 0};

void world_cam(int* x, int* y) { *x = s_cam_x; *y = s_cam_y; }
void world_shaman_tile(int* x, int* y) { *x = (s_sh.x + 8) >> 4; *y = (s_sh.y + 8) >> 4; if (*x < 0) *x = 0; if (*y < 0) *y = 0; if (*x >= WM) *x = WM - 1; if (*y >= WM) *y = WM - 1; }

static int  bed_tx(void) { return g_world.start_x; }
static int  bed_ty(void) { return g_world.start_y; }

static int cam_clamp_x(int v) { return v < 0 ? 0 : (v > WM_PIX - CAM_W ? WM_PIX - CAM_W : v); }
static int cam_clamp_y(int v) { return v < 0 ? 0 : (v > WM_PIX - CAM_H ? WM_PIX - CAM_H : v); }

static void walk_to(int tx, int ty) {
  s_goal_x = (uint8_t)tx; s_goal_y = (uint8_t)ty; s_goal_adj = 0;
  world_flow(tx, ty);
  int cx = s_sh.x >> 4, cy = s_sh.y >> 4;
  s_walking = (g_dist[cy * WM + cx] != 255 && g_dist[cy * WM + cx] != 0) ? 1 : 0;
}

// Walk to a tile beside (tx, ty). Returns 0 if there is no way, 1 otherwise (s_walking says whether he has to move).
static int walk_beside(int tx, int ty) {
  s_goal_x = (uint8_t)tx; s_goal_y = (uint8_t)ty; s_goal_adj = 1;
  world_flow_adjacent(tx, ty);
  int cx = s_sh.x >> 4, cy = s_sh.y >> 4, d = g_dist[cy * WM + cx];
  if (d == 255) { s_walking = 0; return 0; }
  s_walking = d != 0;
  return 1;
}

static void pick_wander_target(void) {
  int cx = s_sh.x >> 4, cy = s_sh.y >> 4;
  for (int tries = 0; tries < 6; tries++) {
    int bx = bed_tx(), by = bed_ty(), r = 19, mx, my;
    if (village_phase() == PH_WORSHIP && village_mon_pos(&mx, &my)) { bx = mx + 1; by = my + 3; r = 7; }      // the worship day: he stands among his people
    int x = bx + (int)(gc_rnd() % (uint32_t)r) - r / 2, y = by + (int)(gc_rnd() % (uint32_t)r) - r / 2;     // he stays near his village
    if (!world_walkable(x, y) || (x == cx && y == cy)) continue;
    walk_to(x, y);
    if (s_walking) return;
  }
}

static void place_shaman(void) {
  s_sh.x = (int16_t)(bed_tx() * GC_TILE); s_sh.y = (int16_t)(bed_ty() * GC_TILE);
  s_sh.dir = DIR_DOWN; s_sh.step = s_sh.moved = 0;
  s_sh.act = GQ_ACT_WANDER; s_sh.abed = 0;
  s_moving = s_walking = 0; s_pause = 1000; s_going_to_bed = 0; s_work = 0; s_cast = 0; s_goal_adj = 0;
  s_cam_free = 0;
  s_cam_x = cam_clamp_x(s_sh.x + 8 - CAM_W / 2);
  s_cam_y = cam_clamp_y(s_sh.y + 8 - CAM_H / 2);
  quarts_view(s_cam_x, s_cam_y);
}

static void reset_state(int keep_world) {
  G.s.magic = GQ_SAVE_MAGIC; G.s.version = GQ_SAVE_VERSION; G.s.size = GQ_SAVE_SIZE;
  G.s.seq = 0; G.s.play_min = 0; G.s.day = 0; G.s.minute = 0;
  if (!keep_world) {
    G.s.hist_n = 0; G.s.landform = G.s.climate = 0;
    for (int i = 0; i < 4; i++) { G.s.hist_seed[i] = 0; G.s.hist_land[i] = G.s.hist_clim[i] = 0; }
  }
  for (int i = 0; i < (int)sizeof(G.s.reserved); i++) G.s.reserved[i] = 0;
  G.s.pad0 = 0;
  G.serial++;
  s_play_ms = 0;
}

// Generates the world for (seed, landform, climate) and puts the shaman in his bed spot.
static void start_world(uint32_t seed, int landform, int climate, int fresh) {
  G.s.seed = seed ? seed : 1;
  world_generate(G.s.seed, landform, climate);
  G.s.landform = g_world.landform; G.s.climate = g_world.climate;
  G.s.rng = gc_rng_get();
  if (fresh) village_reset((int)seed);
  build_palettes();
  place_shaman();
  s_move_acc = s_water_acc = s_band_acc = s_hud_acc = 0;
  g_wframe = 0; s_hud_alt = 0; s_band_cur = 0; s_last_min = -1; s_ph_seen = 0; s_sum_slept = 0;
  gc_mark_all_dirty();
}

// A new world: never the previous landform + climate.
static void new_world(uint32_t seed, int exact) {
  if (!seed) seed = 1;
  int l, c;
  if (exact) world_pick(seed, -1, -1, &l, &c);
  else world_pick_hist(seed, G.s.hist_land, G.s.hist_clim, G.s.hist_n, &l, &c);      // both differ from the last world, and none of the last 4 pairs again
  if (s_force_l >= 0) l = s_force_l;
  if (s_force_c >= 0) c = s_force_c;
  for (int i = 3; i > 0; i--) { G.s.hist_seed[i] = G.s.hist_seed[i - 1]; G.s.hist_land[i] = G.s.hist_land[i - 1]; G.s.hist_clim[i] = G.s.hist_clim[i - 1]; }
  G.s.hist_seed[0] = seed; G.s.hist_land[0] = (uint8_t)l; G.s.hist_clim[0] = (uint8_t)c;
  if (G.s.hist_n < 4) G.s.hist_n++;
  G.s.day = 0; G.s.minute = 0; G.s.play_min = 0; s_play_ms = 0;
  start_world(seed, l, c, 1);
  gq_touch();
}

void world_init(uint32_t seed) {
  reset_state(0);
  new_world(seed, 1);
}

// ---------------------------------------------------------------- helpers for the HUD
static void fmt2(char* p, int v) { p[0] = (char)('0' + (v / 10) % 10); p[1] = (char)('0' + v % 10); }

// The weather the game uses: a debug override, else the live reading, else clear (no reading: mild and dry).
static int8_t  s_fw = -1;                          // forced weather, -1 none
static int16_t s_ft;                               // forced temperature (tenths of a degree C) when s_ft_on
static uint8_t s_ft_on;
int world_weather(void) { return s_fw >= 0 ? (int)s_fw : (G.live_wx1 ? (int)G.live_wx1 - 1 : GQ_W_CLEAR); }
int world_has_reading(void) { return s_fw >= 0 || G.live_wx1; }
int world_temp10(void) { return s_ft_on ? (int)s_ft : (G.live_wx1 ? (int)G.live_temp10 : 150); }

// Whole degrees in the display unit, rounded half away from zero. 0 when there is no live reading.
int world_temp(int* out, int* is_f) {
  if (!G.live_wx1 && !s_ft_on) return 0;
  int f = (G.live_flags & GC_WXF_FAHRENHEIT) ? 1 : 0;
  int v = world_temp10();
  if (f) v = v * 9 / 5 + 320;
  int r = v >= 0 ? (v + 5) / 10 : -((-v + 5) / 10);
  *out = r; *is_f = f;
  return 1;
}

static int is_night(void) {
  if (!g_clock_valid) return 0;
  int m = g_hour * 60 + g_min;
  return m >= GC_NIGHT_START_MIN || m < GC_NIGHT_END_MIN;
}

static int want_bed(void) {
  if (G.sleep_mode == 2) return 1;
  if (G.sleep_mode == 1) return 0;
  return is_night();
}

static int night_at(int m) {
  if (G.sleep_mode == 2) return 1;
  if (G.sleep_mode == 1) return 0;
  return m >= GC_NIGHT_START_MIN || m < GC_NIGHT_END_MIN;
}

void world_clock_changed(void) {
  int m = g_hour * 60 + g_min;
  if (!g_clock_valid) return;
  int d = s_last_min >= 0 ? (m - s_last_min + 1440) % 1440 : 0;
  if (d >= 1 && d <= 2) {                      // the village lives minute by minute; a longer jump is not played back
    for (int k = 1; k <= d; k++) {
      int mm = (s_last_min + k) % 1440;
      if (mm == 0) world_new_day();
      village_minute(mm, night_at(mm));
    }
  } else if (d > 2 && d < 720) {
    if (s_last_min + d >= 1440) world_new_day();   // a forward jump over midnight is a new day; a jump back (time zone, NTP) is not
    village_arc(m);                                // the minutes are not played, but the end of the worship day and of a dying village still count
  }
  s_last_min = m;
  G.s.minute = (uint16_t)m;
}

void world_new_day(void) {
  if (G.s.day < 65535) G.s.day++;
  village_new_day(want_bed());
  gq_touch();
  g_dirty |= 1u << BAND_STRIP;
}

void gq_touch(void) { G.serial++; }

void world_status_changed(void) { g_dirty |= 1u << BAND_STRIP; }
void world_weather_changed(void) { g_dirty |= 1u | MAP_STRIPS; }

// ------------------------------------------------------------------- simulation
static void mark_anim_strips(void) {
  int tx0 = s_cam_x >> 4, tx1 = (s_cam_x + CAM_W - 1) >> 4, ty0 = s_cam_y >> 4, ty1 = (s_cam_y + CAM_H - 1) >> 4;
  for (int y = ty0; y <= ty1 && y < WM; y++)
    for (int x = tx0; x <= tx1 && x < WM; x++)
      if (quarts_tile_anim(x, y)) { mark_rows(y * GC_TILE - s_cam_y + GC_STRIP_H, GC_TILE); break; }
}

static void follow_camera(void) {
  if (s_cam_free) return;
  int lead_x = s_sh.dir == DIR_RIGHT ? 24 : (s_sh.dir == DIR_LEFT ? -24 : 0);
  int lead_y = s_sh.dir == DIR_DOWN ? 16 : (s_sh.dir == DIR_UP ? -16 : 0);
  if (!s_moving) lead_x = lead_y = 0;
  int tx = cam_clamp_x(s_sh.x + 8 + lead_x - CAM_W / 2), ty = cam_clamp_y(s_sh.y + 8 + lead_y - CAM_H / 2);
  int dx = tx - s_cam_x, dy = ty - s_cam_y;
  int sx = dx / 8, sy = dy / 8;
  if (!sx && dx) sx = dx > 0 ? 1 : -1;
  if (!sy && dy) sy = dy > 0 ? 1 : -1;
  sx = sx > 6 ? 6 : (sx < -6 ? -6 : sx);
  sy = sy > 6 ? 6 : (sy < -6 ? -6 : sy);
  s_cam_x += sx; s_cam_y += sy;
  if (sx || sy) quarts_view(s_cam_x, s_cam_y);
}

// at a tile boundary: choose the next tile of the walk
static void begin_step(void) {
  if (s_goal_adj) world_flow_adjacent(s_goal_x, s_goal_y); else world_flow(s_goal_x, s_goal_y);     // g_dist is shared with the villagers: recompute for this step
  int tx = s_sh.x >> 4, ty = s_sh.y >> 4;
  int d = g_dist[ty * WM + tx];
  if (d == 0 || d == 255) { s_walking = 0; return; }
  static const int8_t D[4][2] = {{0, 1}, {0, -1}, {-1, 0}, {1, 0}};     // DIR_DOWN, UP, LEFT, RIGHT
  int best = -1;
  for (int k = 0; k < 5 && best < 0; k++) {
    int dd = (k == 0) ? (int)s_sh.dir : k - 1;                           // current direction first
    if (k > 0 && dd == (int)s_sh.dir) continue;
    int nx = tx + D[dd][0], ny = ty + D[dd][1];
    if (nx < 0 || ny < 0 || nx >= WM || ny >= WM) continue;
    if (g_dist[ny * WM + nx] == d - 1 && world_step_ok(tx, ty, nx, ny)) best = dd;
  }
  if (best < 0) { s_walking = 0; return; }
  world_audit_step(tx, ty, tx + D[best][0], ty + D[best][1]);
  s_mx = (int16_t)((tx + D[best][0]) * GC_TILE); s_my = (int16_t)((ty + D[best][1]) * GC_TILE);
  s_sh.dir = (uint8_t)best;
  s_moving = 1;
}

// A seed that has not been used before: the generator's state, the wall clock and the minutes played.
static uint32_t fresh_seed(void) {
  gc_rnd();
  uint32_t seed = gc_rng_get() ^ s_next_seed ^ (G.s.epoch * 2654435761u) ^ (G.s.play_min * 40503u);
  s_next_seed = s_next_seed * 1664525u + 1013904223u;
  return seed ? seed : 1;
}

// The summary is on screen (the monument is finished and the worship day is over, or the village is extinct): it stays until the
// morning after, when the clock wakes up; the sleep schedule still darkens the screen at night (the firmware does that).
static void phase_tick(uint32_t dt) {
  int ph = village_phase();
  if (ph != s_ph_seen) {
    s_ph_seen = (uint8_t)ph;
    s_sum_slept = 0; s_sum_ms = 0;
    g_dirty |= 0x7FFFu;
    if (ph == PH_WORSHIP) { s_band_cur = 5; s_cam_free = 0; }
    gq_touch();
  }
  if (ph >= PH_DONE) {
    // The summary ends when the clock wakes up next: want_bed() follows the user's sleep schedule (mode 2 = asleep, 1 = schedule in use and
    // awake) or the built-in night (mode 0, up at GC_NIGHT_END_MIN). Without a valid clock, or if it never sleeps, an uptime limit applies.
    s_sum_ms += dt;
    if (want_bed()) s_sum_slept = 1;
    else if (s_sum_slept && g_clock_valid) new_world(fresh_seed(), 0);      // the morning after: a new, very different world
    else if (s_sum_ms >= (g_clock_valid ? SUM_MAX_MS : SUM_NOCLOCK_MS)) new_world(fresh_seed(), 0);
  }
}

void world_tick(uint32_t dt) {
  s_play_ms += dt;
  if (s_play_ms >= 60000) { s_play_ms -= 60000; G.s.play_min++; }
  phase_tick(dt);

  // bed time and wake up
  int bed = want_bed();
  if (bed && s_sh.act != GQ_ACT_SLEEP && !s_going_to_bed) {
    s_going_to_bed = 1;
    s_work = 0; if (s_sh.act == GQ_ACT_CAST) s_sh.act = GQ_ACT_WANDER;
    walk_to(bed_tx(), bed_ty());
  } else if (!bed && (s_sh.act == GQ_ACT_SLEEP || s_going_to_bed)) {
    s_going_to_bed = 0;
    s_sh.act = GQ_ACT_WANDER; s_sh.abed = 0; s_pause = 500;
    g_dirty |= MAP_STRIPS;
  }

  village_tick(dt, bed);
  weather_tick(dt, bed);
  int cam_moved = 0, sh_moved = 0;
  s_move_acc += dt;
  while (s_move_acc >= MOVE_MS) {
    s_move_acc -= MOVE_MS;
    if (s_sh.act == GQ_ACT_SLEEP) continue;
    if (s_sh.act == GQ_ACT_CAST) {       // the staff is raised: the tile changes halfway
      if (s_cast > (int)MOVE_MS) {
        s_cast -= MOVE_MS;
        if (!s_applied && s_cast < CAST_MS / 2) { s_applied = 1; shaman_apply(); g_dirty |= MAP_STRIPS; }
        if ((s_cast & 63) < MOVE_MS) mark_rows(s_wty * GC_TILE - s_cam_y + GC_STRIP_H - 6, 36);
        continue;
      }
      if (!s_applied) { s_applied = 1; shaman_apply(); g_dirty |= MAP_STRIPS; }
      s_sh.act = GQ_ACT_WANDER; s_pause = 120; s_cast = 0;
      mark_rows(s_sh.y - s_cam_y + GC_STRIP_H - 18, 52);
      continue;
    }
    if (!s_moving) {
      if (s_walking) begin_step();
      if (!s_moving) {
        {   // the ground may have been taken from under his feet (a new building, a pond)
          int tx = s_sh.x >> 4, ty = s_sh.y >> 4, ox, oy;
          if (!world_walkable(tx, ty) && !s_going_to_bed && world_nearest_connected(tx, ty, bed_tx(), bed_ty(), &ox, &oy)) {
            world_audit_jump(((s_sh.x >> 4) > ox ? (s_sh.x >> 4) - ox : ox - (s_sh.x >> 4)) + ((s_sh.y >> 4) > oy ? (s_sh.y >> 4) - oy : oy - (s_sh.y >> 4)));
            s_sh.x = (int16_t)(ox * GC_TILE); s_sh.y = (int16_t)(oy * GC_TILE); s_walking = 0; s_work = 0; g_dirty |= MAP_STRIPS;
          }
        }
        if (shaman_stuck()) {                       // walled in (a pocket no ramp can open): back to the green, the one jump he makes
          world_audit_jump(9);
          s_sh.x = (int16_t)(bed_tx() * GC_TILE); s_sh.y = (int16_t)(bed_ty() * GC_TILE); s_walking = 0; s_work = 0; g_dirty |= MAP_STRIPS;
        }
        if (s_going_to_bed) {
          int tx = s_sh.x >> 4, ty = s_sh.y >> 4;
          if (tx == bed_tx() && ty == bed_ty()) {
            s_sh.act = GQ_ACT_SLEEP; s_sh.abed = 1; s_sh.dir = DIR_DOWN; g_dirty |= MAP_STRIPS;
          } else {
            walk_to(bed_tx(), bed_ty());
            if (!s_walking) { s_sh.act = GQ_ACT_SLEEP; s_sh.abed = 1; g_dirty |= MAP_STRIPS; }   // cannot reach: lie down where he is
          }
          continue;
        }
        if (s_work == 1 && !s_walking) {            // arrived beside the tile: raise the staff
          int tx = s_sh.x >> 4, ty = s_sh.y >> 4, dx = (int)s_wtx - tx, dy = (int)s_wty - ty;
          if ((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) == 1) {
            s_sh.dir = (uint8_t)(dx > 0 ? DIR_RIGHT : (dx < 0 ? DIR_LEFT : (dy < 0 ? DIR_UP : DIR_DOWN)));
            s_sh.act = GQ_ACT_CAST; s_cast = CAST_MS; s_applied = 0; s_work = 2;
            mark_rows(s_sh.y - s_cam_y + GC_STRIP_H - 18, 52);
            continue;
          }
          if (++s_retry > 3 || !walk_beside(s_wtx, s_wty)) { shaman_abort(); s_work = 0; }
          continue;
        }
        if (s_work == 2) s_work = 0;
        if (s_pause > (uint32_t)MOVE_MS) { s_pause -= MOVE_MS; continue; }
        {
          int tx, ty;
          if (shaman_target(s_sh.x >> 4, s_sh.y >> 4, &tx, &ty)) {
            s_wtx = (uint8_t)tx; s_wty = (uint8_t)ty; s_retry = 0; s_work = 1;
            if (!walk_beside(tx, ty)) {
              // maybe he is shut in a pocket the village cannot reach: he steps back to his bed and tries again
              world_flow(bed_tx(), bed_ty());
              if (g_dist[(s_sh.y >> 4) * WM + (s_sh.x >> 4)] == 255) {
                world_audit_jump(9);
                s_sh.x = (int16_t)(bed_tx() * GC_TILE); s_sh.y = (int16_t)(bed_ty() * GC_TILE); s_walking = 0; g_dirty |= MAP_STRIPS;
              }
              if (!walk_beside(tx, ty)) { shaman_abort(); s_work = 0; s_pause = 400; }
            }
            continue;
          }
        }
        s_pause = 800 + gc_rnd() % 2500;
        pick_wander_target();
        continue;
      }
    }
    // one pixel towards the target tile
    if (s_sh.x != s_mx) { s_sh.x = (int16_t)(s_sh.x + (s_mx > s_sh.x ? 1 : -1)); }
    else if (s_sh.y != s_my) { s_sh.y = (int16_t)(s_sh.y + (s_my > s_sh.y ? 1 : -1)); }
    sh_moved = 1;
    if (s_sh.x == s_mx && s_sh.y == s_my) s_moving = 0;
    if (++s_sh.moved >= 6) { s_sh.moved = 0; s_sh.step = (uint8_t)((s_sh.step + 1) & 3); }
    int ox = s_cam_x, oy = s_cam_y;
    follow_camera();
    if (ox != s_cam_x || oy != s_cam_y) cam_moved = 1;
  }
  if (cam_moved) g_dirty |= MAP_STRIPS;
  else if (sh_moved) mark_rows(s_sh.y - s_cam_y + GC_STRIP_H - 18, 52);
  if (sh_moved) s_cam_free = 0;

  s_water_acc += dt;
  if (s_water_acc >= WATER_MS) {
    s_water_acc -= WATER_MS;
    g_wframe ^= 1;
    mark_anim_strips();
  }

  s_hud_acc += dt;
  if (s_hud_acc >= HUD_ALT_MS) { s_hud_acc -= HUD_ALT_MS; s_hud_alt = (uint8_t)((s_hud_alt + 1) & 1); g_dirty |= 1u; }

  s_band_acc += dt;
  if (s_band_acc >= BAND_MS) {
    s_band_acc -= BAND_MS;
    if (s_band_cur != 5 && (shaman_label() || village_phase() == PH_WORSHIP)) s_band_cur = 5;        // the project (or the worship day) every other time
    else {
      for (int i = 0; i < 5; i++) {
        s_band_base = (s_band_base + 1) % 5;
        if (s_band_base == 4 || g_status[s_band_base][0]) break;
      }
      s_band_cur = s_band_base;
    }
    g_dirty |= 1u << BAND_STRIP;
  }
}

// -------------------------------------------------------------------- drawing
static void cloud(uint16_t* out, int y0, int x, int y, uint16_t c) {
  fill_rect(out, y0, x + 2, y + 4, 9, 5, c);
  fill_rect(out, y0, x + 4, y + 2, 4, 3, c);
  fill_rect(out, y0, x + 1, y + 5, 11, 3, c);
}

// A 13x13 weather icon.
static void weather_icon(uint16_t* out, int y0, int x, int y, int w, int night) {
  const uint16_t YEL = 0xFFE0, WHT = 0xFFFF, GRY = 0xC618, DGR = 0x8410, BLU = 0x5D9F;
  switch (w) {
    case GQ_W_CLEAR:
      if (night) {
        fill_rect(out, y0, x + 3, y + 1, 6, 11, WHT); fill_rect(out, y0, x + 2, y + 3, 8, 7, WHT);
        fill_rect(out, y0, x + 6, y + 2, 4, 9, g_ui[P_OUTLINE]);
      } else {
        fill_rect(out, y0, x + 3, y + 2, 7, 9, YEL); fill_rect(out, y0, x + 2, y + 3, 9, 7, YEL);
        fill_rect(out, y0, x + 6, y, 1, 2, YEL); fill_rect(out, y0, x + 6, y + 11, 1, 2, YEL);
        fill_rect(out, y0, x, y + 6, 2, 1, YEL); fill_rect(out, y0, x + 11, y + 6, 2, 1, YEL);
      }
      break;
    case GQ_W_CLOUDY: cloud(out, y0, x, y + 1, GRY); break;
    case GQ_W_RAIN:
      cloud(out, y0, x, y, GRY);
      fill_rect(out, y0, x + 3, y + 10, 1, 3, BLU); fill_rect(out, y0, x + 6, y + 10, 1, 3, BLU); fill_rect(out, y0, x + 9, y + 10, 1, 3, BLU);
      break;
    case GQ_W_STORM:
      cloud(out, y0, x, y, DGR);
      fill_rect(out, y0, x + 6, y + 8, 2, 2, YEL); fill_rect(out, y0, x + 5, y + 10, 2, 2, YEL); fill_rect(out, y0, x + 4, y + 12, 1, 1, YEL);
      break;
    case GQ_W_FOG:
      fill_rect(out, y0, x + 1, y + 3, 11, 1, GRY); fill_rect(out, y0, x + 2, y + 6, 10, 1, GRY); fill_rect(out, y0, x, y + 9, 11, 1, GRY);
      break;
    case GQ_W_SNOW:
      cloud(out, y0, x, y, GRY);
      fill_rect(out, y0, x + 3, y + 10, 2, 2, WHT); fill_rect(out, y0, x + 7, y + 10, 2, 2, WHT); fill_rect(out, y0, x + 5, y + 12, 1, 1, WHT);
      break;
    default:   // windy
      fill_rect(out, y0, x, y + 3, 9, 1, WHT); fill_rect(out, y0, x + 9, y + 2, 2, 1, WHT); fill_rect(out, y0, x + 10, y + 3, 1, 2, WHT);
      fill_rect(out, y0, x + 2, y + 6, 10, 1, WHT); fill_rect(out, y0, x + 1, y + 9, 8, 1, WHT);
      break;
  }
}

static int put_num(char* p, int n) {      // 0..9999
  if (n < 0) n = 0;
  if (n > 9999) n = 9999;
  int k = 0;
  if (n >= 1000) p[k++] = (char)('0' + n / 1000);
  if (n >= 100) p[k++] = (char)('0' + (n / 100) % 10);
  if (n >= 10) p[k++] = (char)('0' + (n / 10) % 10);
  p[k++] = (char)('0' + n % 10);
  p[k] = 0;
  return k;
}

// Left (r7): two counters at a time, each as tall as the clock: the 8x8 icon doubled to 16x16 and digits of the clock's size. The pairs
// alternate every 3.5 s: faith and population, then food and wood. 9999 faith and 48 villagers (or 999 food and 999 wood) end at x = 114,
// the clock starts at x = 119 or later, so they never touch.
static int draw_counter(uint16_t* out, int y0, int x, int id) {
  const uint8_t* ic = GC_ICONS(id);
  for (int r = 0; r < 16; r++) {
    int py = r - y0;
    if (py < 0 || py >= GC_STRIP_H) continue;
    for (int c = 0; c < 16; c++) {
      uint8_t v = GC_RD8(ic + (r >> 1) * 4 + (c >> 2));
      uint8_t p = ((c >> 1) & 1) ? (v & 15) : (v >> 4);
      if (p) out[py * GC_W + x + c] = g_ui[p];
    }
  }
  char nb[5];
  int len = put_num(nb, village_stat(HUD_STAT[id]));
  draw_text(out, y0, x + 18, 1, nb, g_ui[P_WHITE], 2, 0);
  return x + 18 + len * 12;
}
static void draw_counters(uint16_t* out, int y0) {
  static const uint8_t ORDER[4] = {0, 3, 1, 2};         // icon ids: 0 faith, 1 food, 2 wood, 3 population
  int pair = (s_hud_alt & 1) * 2;
  int x = draw_counter(out, y0, 2, ORDER[pair]);
  draw_counter(out, y0, x + 4, ORDER[pair + 1]);
}

static void draw_top(uint16_t* out, int y0) {
  const uint16_t ink = g_ui[P_WHITE];
  fill_rect(out, y0, 0, 0, GC_W, GC_STRIP_H, g_ui[P_OUTLINE]);
  draw_counters(out, y0);
  char t[6] = {'-', '-', ':', '-', '-', 0};
  if (g_clock_valid) { fmt2(t, g_hour); fmt2(t + 3, g_min); }
  // temperature at the right edge, weather icon left of it, the clock right aligned a few pixels left of the icon
  int deg, f;
  char tb[8];
  int len = 0;
  if (world_temp(&deg, &f)) {
    int a = deg < 0 ? -deg : deg;
    if (deg < 0) tb[len++] = '-';
    if (a >= 100) tb[len++] = (char)('0' + a / 100);
    if (a >= 10) tb[len++] = (char)('0' + (a / 10) % 10);
    tb[len++] = (char)('0' + a % 10);
    if (len < 4) tb[len++] = f ? 'F' : 'C';          // four characters (-10) leave the unit out so that the clock keeps its room
  } else {
    tb[len++] = '-'; tb[len++] = '-';
  }
  tb[len] = 0;
  int tx = GC_W - 4 - len * 12;
  draw_text(out, y0, tx, 1, tb, ink, 2, 0);
  weather_icon(out, y0, tx - 17, 2, world_weather(), ((G.live_flags & GC_WXF_NIGHT) || is_night() || want_bed()) ? 1 : 0);
  draw_text(out, y0, tx - 17 - 4 - 60, 1, t, ink, 2, 0);
}

static void draw_band(uint16_t* out, int y0) {
  fill_rect(out, y0, 0, y0, GC_W, GC_STRIP_H, g_ui[P_OUTLINE]);
  char buf[40];
  if (s_band_cur == 4) {
    int n = G.s.day + 1, p = 0;
    static const char H[] = "DAY ";
    for (int i = 0; H[i]; i++) buf[p++] = H[i];
    if (n >= 10000) n = 9999;
    if (n >= 1000) buf[p++] = (char)('0' + n / 1000);
    if (n >= 100) buf[p++] = (char)('0' + (n / 100) % 10);
    if (n >= 10) buf[p++] = (char)('0' + (n / 10) % 10);
    buf[p++] = (char)('0' + n % 10);
    static const char T[] = " OF ";
    for (int i = 0; T[i]; i++) buf[p++] = T[i];
    for (int i = 0; g_world.name[i] && p < 38; i++) buf[p++] = g_world.name[i];
    buf[p] = 0;
  } else if (s_band_cur == 5) {
    const char* l = shaman_label();
    if (!l && village_phase() == PH_WORSHIP) l = "WORSHIP DAY";
    str_copy(buf, l ? l : "", 32);
  } else {
    str_copy(buf, g_status[s_band_cur], 32);
  }
  int len = str_len(buf);
  int scale = len > 19 ? 1 : 2;
  int x = (GC_W - len * 6 * scale) / 2;
  if (x < 2) x = 2;
  draw_text(out, y0, x, y0 + (scale == 2 ? 1 : 4), buf, g_ui[P_WHITE], scale, 0);
}

// The summary screen (a whole 240x240 picture): world name, days, peak population, births, deaths, buildings built.
static void draw_value_row(uint16_t* out, int y0, int y, const char* label, int v) {
  char nb[6];
  int len = put_num(nb, v);
  draw_text(out, y0, 14, y, label, g_ui[P_WHITE], 2, 0);
  draw_text(out, y0, GC_W - 14 - len * 12, y, nb, g_ui[P_YELLOW], 2, 0);
}

static void draw_summary(int s, uint16_t* out) {
  int y0 = s * GC_STRIP_H;
  fill_rect(out, y0, 0, y0, GC_W, GC_STRIP_H, g_ui[P_OUTLINE]);
  const uint16_t gold = g_ui[P_YELLOW];
  fill_rect(out, y0, 3, 3, GC_W - 6, 1, gold); fill_rect(out, y0, 3, GC_H - 4, GC_W - 6, 1, gold);
  fill_rect(out, y0, 3, 3, 1, GC_H - 6, gold); fill_rect(out, y0, GC_W - 4, 3, 1, GC_H - 6, gold);
  int ext = village_phase() == PH_EXTINCT;
  const char* t1 = ext ? "THE VILLAGE IS GONE" : "THE MONUMENT STANDS";
  draw_text(out, y0, (GC_W - str_len(t1) * 12) / 2, 10, t1, gold, 2, 0);
  const char* nm = g_world.name;
  draw_text(out, y0, (GC_W - str_len(nm) * 12) / 2, 30, nm, g_ui[P_WHITE], 2, 0);
  // the monument as it stood, 48x48
  int mx, my;
  if (village_mon_pos(&mx, &my)) {
    int st = village_mon_stage(), mt = village_mon_type();
    for (int j = 0; j < 3; j++) {
      int py = 52 + j * GC_TILE;
      if (py + GC_TILE <= y0 || py >= y0 + GC_STRIP_H) continue;
      for (int i = 0; i < 3; i++) {
        gc_cell c = st == 0 ? GC_VILLAGE(VA_SITE) : (st == 1 ? GC_MONUMENT(MA_STAGE1 + j * 3 + i) : GC_MONUMENT(MA_BASE + (mt * 4 + st - 2) * 9 + j * 3 + i));
        blit_sprite(out, y0, 96 + i * GC_TILE, py, c, 0, 0, g_wpal);
      }
    }
  }
  else {                                                     // no monument stands: a cairn of three stones where it would have been
    const uint16_t gr = g_ui[P_GREY], wh = g_ui[P_WHITE], ol = g_ui[P_OUTLINE];
    fill_rect(out, y0, 88, 96, 64, 2, gr);
    fill_rect(out, y0, 104, 82, 32, 14, gr);  fill_rect(out, y0, 104, 82, 32, 1, wh); fill_rect(out, y0, 119, 82, 2, 14, ol);
    fill_rect(out, y0, 111, 70, 18, 12, wh);  fill_rect(out, y0, 111, 81, 18, 1, ol);
    fill_rect(out, y0, 116, 60, 8, 10, gr);   fill_rect(out, y0, 116, 69, 8, 1, ol);
  }
  draw_value_row(out, y0, 108, "DAYS", G.s.day + 1);
  draw_value_row(out, y0, 128, "PEAK VILLAGERS", village_stat(253));
  draw_value_row(out, y0, 148, "BIRTHS", village_stat(12));
  draw_value_row(out, y0, 168, "DEATHS", village_stat(11));
  draw_value_row(out, y0, 188, "BUILT", village_stat(254));
  const char* t2 = "A NEW WORLD AT DAWN";
  draw_text(out, y0, (GC_W - str_len(t2) * 6) / 2, 218, t2, g_ui[P_GREY], 1, 0);
}

void world_render(int s, uint16_t* out) {
  int y0 = s * GC_STRIP_H;
  if (s_ph_seen >= PH_DONE) { draw_summary(s, out); return; }
  if (s == 0) { draw_top(out, y0); return; }
  if (s == BAND_STRIP) { draw_band(out, y0); return; }
  int wx0 = s_cam_x, wy0 = s_cam_y + y0 - GC_STRIP_H;
  quarts_render(out, wx0, wy0, g_wframe);
  weather_ground(out, wx0, wy0);
  draw_objects(out, wx0, wy0);
  village_draw(out, wx0, wy0);
  // the shaman, and a Z over his head in bed
  int sx = s_sh.x - wx0;
  if (s_sh.y < wy0 + GC_STRIP_H && s_sh.y + 16 > wy0) {
    static const uint8_t cycle[4] = {0, 1, 0, 2};
    if (s_sh.act == GQ_ACT_SLEEP) {
      fill_rect(out, wy0, sx + 1, s_sh.y + 9, 14, 6, 0xA145);     // a blanket
      blit_sprite(out, wy0, sx, s_sh.y + 2, GC_SHAMAN(0), 0, 0, g_act);
    } else {
      int row = s_sh.dir == DIR_RIGHT ? (int)DIR_LEFT : (int)s_sh.dir;
      int cell = (row == DIR_DOWN ? 0 : row == DIR_UP ? 3 : 6) + cycle[s_sh.step];
      if (s_sh.act == GQ_ACT_CAST) cell = 9 + (row == DIR_DOWN ? 0 : row == DIR_UP ? 2 : 4) + ((s_cast / 160) & 1);
      static const uint8_t ROBE[4] = {3, 7, 9, 12};              // farmer green, herder sand, fisher blue, builder red
      uint8_t rm[16];
      for (int k = 0; k < 16; k++) rm[k] = (uint8_t)k;
      rm[12] = ROBE[shaman_pers() & 3];
      blit_sprite(out, wy0, sx, s_sh.y, GC_SHAMAN(cell), s_sh.dir == DIR_RIGHT, rm, g_act);
    }
  }
  if (s_sh.act == GQ_ACT_CAST) {              // sparkles round the tile that changes
    int ox = s_wtx * GC_TILE - wx0, oy = s_wty * GC_TILE;
    for (int k = 0; k < 7; k++) {
      uint32_t h = (uint32_t)(k * 2654435761u) ^ (uint32_t)((s_cast / 80) * 40503u + 977u);
      h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
      int px = ox - 2 + (int)(h % 20), py = oy - 2 - (int)((h >> 8) % 20) + 10 + (int)(((CAST_MS - s_cast) / 40) % 6) * -1;
      uint16_t col = (h >> 17) & 1 ? g_act[P_WHITE] : g_act[P_YELLOW];
      int dy = py - wy0;
      if (dy >= 0 && dy < GC_STRIP_H && px >= 0 && px < GC_W) {
        out[dy * GC_W + px] = col;
        if (dy + 1 < GC_STRIP_H && (k & 1)) out[(dy + 1) * GC_W + px] = col;
      }
    }
  }
  if (s_sh.act == GQ_ACT_SLEEP && s_sh.y - 14 < wy0 + GC_STRIP_H && s_sh.y + 2 > wy0)
    blit_sprite(out, wy0, sx, s_sh.y - 14, GC_ITEMS(6), 0, 0, g_act);   // Z z z
  weather_sky(out, wx0, wy0, s);
}

// ----------------------------------------------------------------------- environment API
extern "C" void gc_set_date(int year, int month, int day, int weekday, uint32_t epoch) {
  G.year = (int16_t)(year < 0 ? 0 : (year > 9999 ? 9999 : year));
  G.month = (uint8_t)(month < 1 || month > 12 ? 0 : month);
  G.mday = (uint8_t)(day < 1 || day > 31 ? 0 : day);
  G.wday = (uint8_t)(weekday < 0 || weekday > 6 ? 0 : weekday);
  if (epoch) G.s.epoch = epoch;
}

extern "C" void gc_set_clock_quality(int approximate) { G.clock_approx = approximate ? 1 : 0; }

extern "C" void gc_set_net(int state) {
  if (state < GC_NET_ONLINE || state > GC_NET_AP_ONLY) return;
  G.net = (uint8_t)state;
}

extern "C" void gc_set_option(int key, int value) {
  if (key == GC_OPT_CARD_PERIOD) G.card_period = (uint8_t)(value < 0 ? 0 : (value > 240 ? 240 : value));
  else if (key == GC_OPT_NIGHT_QUIET) G.night_quiet = value ? 1 : 0;
  else if (key == GC_OPT_SLEEP_MODE) { if (value >= 0 && value <= 2) G.sleep_mode = (uint8_t)value; }
}

extern "C" void gc_set_live_weather(int wx, int temp_c10, int flags) {
  uint8_t w1 = (wx >= 0 && wx < GQ_W_COUNT) ? (uint8_t)(wx + 1) : 0;
  int t = temp_c10 < -999 ? -999 : (temp_c10 > 999 ? 999 : temp_c10);
  uint8_t fl = (uint8_t)(flags & 7);
  if (w1 == G.live_wx1 && t == G.live_temp10 && fl == G.live_flags) return;
  G.live_wx1 = w1; G.live_temp10 = (int16_t)t; G.live_flags = fl;
  world_weather_changed();
}

// ------------------------------------------------------------------- save, query, debug
extern "C" int gc_save_part_size(int part) { return part == 0 ? GC_SAVE_PART0 : (part > 0 && part < GC_SAVE_PARTS ? GC_SAVE_PARTN : 0); }

extern "C" void gc_save_begin(void) {
  if (gc_scene_id() != GC_SCENE_VILLAGE) return;
  G.s.rng = gc_rng_get();
  G.s.seq++;
}

extern "C" int gc_save_export_part(int part, uint8_t* buf, int cap) {
  int n = gc_save_part_size(part);
  if (gc_scene_id() != GC_SCENE_VILLAGE || !buf || !n || cap < n) return 0;
  for (int i = 0; i < n; i++) buf[i] = 0;
  if (part == 0) {
    GqSave h = G.s;
    h.version = GQ_SAVE_VERSION; h.size = GQ_SAVE_HEAD; h.crc32 = 0;
    const uint8_t* p = (const uint8_t*)&h;
    for (int i = 0; i < 64; i++) buf[i] = p[i];
    village_pack_head(buf + 64);
    uint32_t c = gq_save_crc(buf + 12, GQ_SAVE_HEAD - 12);
    for (int i = 0; i < 4; i++) buf[8 + i] = (uint8_t)(c >> (8 * i));
    return n;
  }
  return village_pack_part(part, buf, cap) ? n : 0;
}

extern "C" int gc_save_import_part(int part, const uint8_t* buf, int len) {
  int n = gc_save_part_size(part);
  if (gc_scene_id() != GC_SCENE_VILLAGE || !buf || !n || len != n) return 0;
  if (part == 0) {
    if (gq_save_validate(buf, len) != GC_SAVE_OK) return 0;
    uint8_t* p = (uint8_t*)&G.s;
    for (int i = 0; i < 64; i++) p[i] = buf[i];
    for (int i = 64; i < (int)sizeof(G.s); i++) p[i] = 0;
    gq_save_sanitize(&G.s);
    s_play_ms = 0;
    start_world(G.s.seed, G.s.landform, G.s.climate, 0);
    village_unpack_head(buf + 64);
    s_imp = 1;
    return 1;
  }
  if (!s_imp) return 0;
  village_unpack_part(part, buf);
  return 1;
}

extern "C" void gc_save_import_end(void) {
  if (!s_imp) return;
  s_imp = 0;
  village_unpack_end();
  gc_rng_set(G.s.rng);
  s_ph_seen = (uint8_t)village_phase();
  s_sum_slept = 0;
  gc_mark_all_dirty();
}

extern "C" int gc_save_export(uint8_t* buf, int cap) {
  if (gc_scene_id() != GC_SCENE_VILLAGE || !buf || cap < GC_SAVE_TOTAL) return 0;
  gc_save_begin();
  int at = 0;
  for (int k = 0; k < GC_SAVE_PARTS; k++) {
    int n = gc_save_export_part(k, buf + at, cap - at);
    if (!n) return 0;
    at += n;
  }
  return at;
}

extern "C" int gc_save_import(const uint8_t* buf, int len) {
  if (gc_scene_id() != GC_SCENE_VILLAGE || !buf) return 0;
  if (len == GC_SAVE_BYTES) {                       // an r4 or r5 saved game: the village is in the reserved bytes
    if (gq_save_validate(buf, len) != GC_SAVE_OK) return 0;
    uint8_t* p = (uint8_t*)&G.s;
    for (int i = 0; i < GQ_SAVE_SIZE; i++) p[i] = buf[i];
    gq_save_sanitize(&G.s);
    s_play_ms = 0;
    start_world(G.s.seed, G.s.landform, G.s.climate, 0);
    village_unpack(G.s.reserved);
    for (int i = 0; i < (int)sizeof(G.s.reserved); i++) G.s.reserved[i] = 0;
    gc_rng_set(G.s.rng);
    s_ph_seen = (uint8_t)village_phase(); s_sum_slept = 0;
    G.serial++;
    return 1;
  }
  if (len != GC_SAVE_TOTAL || !gc_save_import_part(0, buf, GC_SAVE_PART0)) return 0;
  int at = GC_SAVE_PART0;
  for (int k = 1; k < GC_SAVE_PARTS; k++, at += GC_SAVE_PARTN) gc_save_import_part(k, buf + at, GC_SAVE_PARTN);
  gc_save_import_end();
  return 1;
}

extern "C" uint32_t gc_save_serial(void) { return G.serial; }

extern "C" void gc_reset_game(void) {
  reset_state(1);
  start_world(G.s.seed, G.s.landform, G.s.climate, 1);
}

extern "C" void gc_new_world(uint32_t seed) {
  if (gc_scene_id() != GC_SCENE_VILLAGE) return;
  new_world(seed ? seed : fresh_seed(), 0);
}

extern "C" int gc_world_name(char* buf, int cap) {
  if (!buf || cap < 1) return 0;
  str_copy(buf, g_world.name, cap);
  return str_len(buf);
}

int32_t world_query(int key) {
  switch (key) {
    case GQ_Q_DAY: return G.s.day;
    case GQ_Q_MINUTE: return g_clock_valid ? g_hour * 60 + g_min : G.s.minute;
    case GQ_Q_WEATHER: return world_weather();
    case GQ_Q_HERO_ACT: return s_sh.act;
    case GQ_Q_HERO_ABED: return s_sh.abed;
    case GQ_Q_LIVE_WX: return G.live_wx1 ? (int)G.live_wx1 - 1 : GC_WX_NONE;
    case GQ_Q_LIVE_TEMP: return G.live_temp10;
    case GQ_Q_LIVE_FLAGS: return G.live_flags;
    case GQ_Q_SLEEP_MODE: return G.sleep_mode;
    case GQ_Q_SHAMAN_X: return s_sh.x;
    case GQ_Q_SHAMAN_Y: return s_sh.y;
    case GQ_Q_SEED: return (int32_t)G.s.seed;
    case GQ_Q_LANDFORM: return g_world.landform;
    case GQ_Q_CLIMATE: return g_world.climate;
    case GQ_Q_CAM_X: return s_cam_x;
    case GQ_Q_CAM_Y: return s_cam_y;
    case GQ_Q_START_X: return g_world.start_x;
    case GQ_Q_START_Y: return g_world.start_y;
    case GQ_Q_START_AREA: return world_flood_area(g_world.start_x, g_world.start_y);     // uses the shared g_dist: the shaman recomputes it per step
    case GQ_Q_MAP_HASH: return (int32_t)world_map_hash();
    case GQ_Q_TEMP10: return world_temp10();
    case GQ_Q_PHASE: return village_stat(250);
    case GQ_Q_MON_STAGE: return village_stat(251);
    case GQ_Q_MON_TYPE: return village_stat(252);
    case GQ_Q_HERO_ON_LAND: return world_walkable((s_sh.x + 8) >> 4, (s_sh.y + 8) >> 4) ? 1 : 0;
    default: return key >= GQ_Q_VILLAGE ? village_stat(key - GQ_Q_VILLAGE) : 0;
  }
}

void world_debug(int cmd, int32_t arg) {
  if (cmd == GQ_DBG_SET_DAY) { G.s.day = (uint16_t)(arg < 0 ? 0 : (arg > 65535 ? 65535 : arg)); gq_touch(); g_dirty |= 1u | (1u << BAND_STRIP); }
  else if (cmd == GQ_DBG_NEXT_DAY) world_new_day();
  else if (cmd == GQ_DBG_FORCE_ACT) {
    if (arg == GQ_ACT_SLEEP) { s_going_to_bed = 1; walk_to(bed_tx(), bed_ty()); }
    else if (arg == GQ_ACT_WANDER) { s_going_to_bed = 0; s_sh.act = GQ_ACT_WANDER; s_sh.abed = 0; s_pause = 0; g_dirty |= MAP_STRIPS; }
  }
  else if (cmd == GQ_DBG_NEW_WORLD) {
    uint32_t seed = (uint32_t)arg;
    if (!seed) { gc_rnd(); seed = gc_rng_get() ^ s_next_seed; s_next_seed = s_next_seed * 1664525u + 1013904223u; }
    new_world(seed, 0);
  }
  else if (cmd == GQ_DBG_FAITH) village_debug(4, arg);
  else if (cmd == GQ_DBG_BAND) { s_band_cur = arg < 0 || arg > 5 ? 0 : arg; g_dirty |= 1u << BAND_STRIP; }
  else if (cmd == GQ_DBG_FOOD) village_debug(0, arg);
  else if (cmd == GQ_DBG_WOOD) village_debug(1, arg);
  else if (cmd == GQ_DBG_ADD_VILLAGERS) village_debug(2, arg);
  else if (cmd == GQ_DBG_THIRST) village_debug(3, arg);
  else if (cmd == GQ_DBG_WEATHER) { s_fw = (int8_t)(arg >= 0 && arg < GQ_W_COUNT ? arg : -1); world_weather_changed(); }
  else if (cmd == GQ_DBG_TEMP) { s_ft_on = arg != 9999; s_ft = (int16_t)(arg < -999 ? -999 : (arg > 999 ? 999 : arg)); world_weather_changed(); }
  else if (cmd == GQ_DBG_SICK) village_debug(5, arg);
  else if (cmd == GQ_DBG_AGE) village_debug(6, arg);
  else if (cmd == GQ_DBG_FLASH) weather_flash();
  else if (cmd == GQ_DBG_MONUMENT) village_debug(7, arg);
  else if (cmd == GQ_DBG_PHASE) village_debug(8, arg);
  else if (cmd == GQ_DBG_EXTINCT) village_debug(9, 0);
  else if (cmd == GQ_DBG_SET_SEED) new_world((uint32_t)arg, 1);
  else if (cmd == GQ_DBG_FORCE_LAND) s_force_l = (int8_t)(arg >= 0 && arg < LF_COUNT ? arg : -1);
  else if (cmd == GQ_DBG_FORCE_CLIM) s_force_c = (int8_t)(arg >= 0 && arg < CL_COUNT ? arg : -1);
  else if (cmd == GQ_DBG_SET_CAM) {
    s_cam_x = cam_clamp_x((int)(arg & 1023)); s_cam_y = cam_clamp_y((int)((arg >> 10) & 1023));
    s_cam_free = 1;
    quarts_view(s_cam_x, s_cam_y);
    g_dirty |= MAP_STRIPS;
  }
}

extern "C" int32_t gc_query(int key) { return world_query(key); }
extern "C" void gc_debug(int cmd, int32_t arg) { world_debug(cmd, arg); }
