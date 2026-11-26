// MiniGodus scene (placeholder for release r1): a grass and water field with a little shaman who wanders and goes to
// bed, a top bar (time, day, weather icon, temperature) and a bottom band that cycles the status lines.
// The real world (32x32 map, villagers, faith) arrives in later releases; the interface to the firmware stays.
// Rules for the core: no heap, no libc, no floating point.
#include "gc_internal.h"

GqState G;

enum { BAND_STRIP = GC_ROWS - 1, MOVE_MS = 25, WATER_MS = 600, BAND_MS = 3000, MAX_PATH = GC_COLS * GC_ROWS };
enum { BED_X = 7, BED_Y = 11 };                   // the shaman's bed tile

struct Shaman { int16_t x, y; uint8_t dir, step, moved, act, abed; };
static Shaman   s_sh;
static uint8_t  s_path[MAX_PATH];                 // tile indices (y*15+x) of the walk, last = next
static int      s_path_n;
static uint32_t s_move_acc, s_water_acc, s_band_acc, s_pause, s_play_ms;
static uint8_t  s_water_frame;
static int      s_band_cur;                       // 0..3 status line, 4 = day line
static int      s_last_min = -1;
static uint8_t  s_going_to_bed;

static int  idx(int x, int y) { return y * GC_COLS + x; }
static int  walkable(int x, int y) {
  if (x < 0 || y < 1 || x >= GC_COLS || y >= GC_ROWS - 1) return 0;     // the bars cover the first and the last row
  return g_ground[y][x] != T_WATER && g_ground[y][x] != T_WATER2 && g_obj[y][x] == T_NONE;
}

static void build_map(uint32_t seed) {
  gc_rng_set(seed);
  for (int y = 0; y < GC_ROWS; y++)
    for (int x = 0; x < GC_COLS; x++) {
      uint32_t r = gc_rnd() % 12;
      g_ground[y][x] = r < 8 ? T_GRASS : (r < 11 ? T_TUFT : T_FLOWER);
      g_obj[y][x] = T_NONE;
    }
  // a lake: a blob of water around a centre that is never on the bed or the top and bottom bars
  int cx = 3 + (int)(gc_rnd() % 9), cy = 4 + (int)(gc_rnd() % 5);
  int rx = 2 + (int)(gc_rnd() % 2), ry = 2 + (int)(gc_rnd() % 2);
  for (int y = 1; y < GC_ROWS - 1; y++)
    for (int x = 0; x < GC_COLS; x++) {
      int dx = (x - cx) * ry, dy = (y - cy) * rx;
      if (dx * dx + dy * dy <= rx * rx * ry * ry + (int)(gc_rnd() % 5)) g_ground[y][x] = T_WATER;
    }
  // the bed spot and a short path to it
  for (int x = BED_X - 1; x <= BED_X + 1; x++) { g_ground[BED_Y][x] = T_PATH; g_obj[BED_Y][x] = T_NONE; }
  // trees and rocks on grass
  for (int i = 0; i < 16; i++) {
    int x = (int)(gc_rnd() % GC_COLS), y = 1 + (int)(gc_rnd() % (GC_ROWS - 2));
    if (g_ground[y][x] == T_WATER || g_ground[y][x] == T_PATH) continue;
    if (y == BED_Y && x >= BED_X - 2 && x <= BED_X + 2) continue;
    g_obj[y][x] = (i & 3) == 3 ? T_ROCK : T_TREE;
  }
}

// Breadth first search over walkable tiles. Fills s_path (last element = first step). 1 if reachable.
static int find_path(int sx, int sy, int tx, int ty) {
  static uint8_t from[MAX_PATH];
  static uint8_t queue[MAX_PATH];
  static uint8_t seen[MAX_PATH];
  for (int i = 0; i < MAX_PATH; i++) seen[i] = 0;
  int qh = 0, qt = 0;
  queue[qt++] = (uint8_t)idx(sx, sy);
  seen[idx(sx, sy)] = 1;
  const int goal = idx(tx, ty);
  while (qh < qt) {
    int cur = queue[qh++];
    if (cur == goal) {
      s_path_n = 0;
      for (int c = goal; c != idx(sx, sy); c = from[c]) s_path[s_path_n++] = (uint8_t)c;
      return 1;
    }
    int x = cur % GC_COLS, y = cur / GC_COLS;
    static const int8_t D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int d = 0; d < 4; d++) {
      int nx = x + D[d][0], ny = y + D[d][1];
      if (!walkable(nx, ny) || seen[idx(nx, ny)]) continue;
      seen[idx(nx, ny)] = 1;
      from[idx(nx, ny)] = (uint8_t)cur;
      queue[qt++] = (uint8_t)idx(nx, ny);
    }
  }
  return 0;
}

static void walk_to(int tx, int ty) {
  int sx = (s_sh.x + 8) / GC_TILE, sy = (s_sh.y + 8) / GC_TILE;
  if (!walkable(sx, sy)) { s_path_n = 0; return; }
  if (!find_path(sx, sy, tx, ty)) s_path_n = 0;
}

static void pick_wander_target(void) {
  for (int tries = 0; tries < 20; tries++) {
    int x = (int)(gc_rnd() % GC_COLS), y = 1 + (int)(gc_rnd() % (GC_ROWS - 2));
    if (!walkable(x, y)) continue;
    walk_to(x, y);
    if (s_path_n) return;
  }
}

static void place_shaman(void) {
  s_sh.x = (int16_t)(BED_X * GC_TILE); s_sh.y = (int16_t)(BED_Y * GC_TILE);
  s_sh.dir = DIR_DOWN; s_sh.step = s_sh.moved = 0;
  s_sh.act = GQ_ACT_WANDER; s_sh.abed = 0;
  s_path_n = 0; s_pause = 1000; s_going_to_bed = 0;
}

static void reset_state(void) {
  G.s.magic = GQ_SAVE_MAGIC; G.s.version = GQ_SAVE_VERSION; G.s.size = GQ_SAVE_SIZE;
  G.s.seq = 0; G.s.play_min = 0; G.s.day = 0; G.s.minute = 0;
  for (int i = 0; i < (int)sizeof(G.s.reserved); i++) G.s.reserved[i] = 0;
  G.serial++;
  s_play_ms = 0;
}

static void start_world(uint32_t seed) {
  G.s.seed = seed ? seed : 1;
  build_map(G.s.seed);
  G.s.rng = gc_rng_get();
  place_shaman();
  s_move_acc = s_water_acc = s_band_acc = 0;
  s_water_frame = 0; s_band_cur = 0; s_last_min = -1;
  gc_mark_all_dirty();
}

void world_init(uint32_t seed) {
  reset_state();
  start_world(seed);
}

// ---------------------------------------------------------------- helpers for the HUD
static void fmt2(char* p, int v) { p[0] = (char)('0' + (v / 10) % 10); p[1] = (char)('0' + v % 10); }

int world_weather(void) { return G.live_wx1 ? (int)G.live_wx1 - 1 : GQ_W_CLEAR; }

// Whole degrees in the display unit, rounded half away from zero. 0 when there is no live reading.
int world_temp(int* out, int* is_f) {
  if (!G.live_wx1) return 0;
  int f = (G.live_flags & GC_WXF_FAHRENHEIT) ? 1 : 0;
  int v = G.live_temp10;
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

void world_clock_changed(void) {
  int m = g_hour * 60 + g_min;
  if (!g_clock_valid) return;
  if (s_last_min >= 0 && m < s_last_min && s_last_min - m > 600) world_new_day();
  s_last_min = m;
  G.s.minute = (uint16_t)m;
}

void world_new_day(void) {
  if (G.s.day < 65535) G.s.day++;
  gq_touch();
  g_dirty |= 1u << BAND_STRIP;
}

void gq_touch(void) { G.serial++; }

void world_status_changed(void) { g_dirty |= 1u << BAND_STRIP; }
void world_weather_changed(void) { g_dirty |= 1u; }

// ------------------------------------------------------------------- simulation
void world_tick(uint32_t dt) {
  s_play_ms += dt;
  if (s_play_ms >= 60000) { s_play_ms -= 60000; G.s.play_min++; }

  // bed time and wake up
  int bed = want_bed();
  if (bed && s_sh.act != GQ_ACT_SLEEP && !s_going_to_bed) {
    s_going_to_bed = 1;
    walk_to(BED_X, BED_Y);
  } else if (!bed && (s_sh.act == GQ_ACT_SLEEP || s_going_to_bed)) {
    s_going_to_bed = 0;
    s_sh.act = GQ_ACT_WANDER; s_sh.abed = 0; s_pause = 500;
    mark_rows(s_sh.y - 16, 48);
  }

  s_move_acc += dt;
  while (s_move_acc >= MOVE_MS) {
    s_move_acc -= MOVE_MS;
    if (s_sh.act == GQ_ACT_SLEEP) continue;
    if (s_path_n == 0) {
      if (s_going_to_bed) {
        int tx = (s_sh.x + 8) / GC_TILE, ty = (s_sh.y + 8) / GC_TILE;
        if (tx == BED_X && ty == BED_Y) { s_sh.act = GQ_ACT_SLEEP; s_sh.abed = 1; s_sh.dir = DIR_DOWN; mark_rows(s_sh.y - 16, 48); }
        else walk_to(BED_X, BED_Y);
        if (s_path_n == 0 && s_sh.act != GQ_ACT_SLEEP) { s_sh.act = GQ_ACT_SLEEP; s_sh.abed = 1; }   // cannot reach: lie down where he is
        continue;
      }
      if (s_pause > (uint32_t)MOVE_MS) { s_pause -= MOVE_MS; continue; }
      s_pause = 800 + gc_rnd() % 2500;
      pick_wander_target();
      continue;
    }
    int next = s_path[s_path_n - 1];
    int tx = (next % GC_COLS) * GC_TILE, ty = (next / GC_COLS) * GC_TILE;
    if (s_sh.x == tx && s_sh.y == ty) { s_path_n--; continue; }
    mark_rows(s_sh.y - 16, 48);
    if (s_sh.x != tx) { int d = tx > s_sh.x ? 1 : -1; s_sh.x = (int16_t)(s_sh.x + d); s_sh.dir = d > 0 ? DIR_RIGHT : DIR_LEFT; }
    else { int d = ty > s_sh.y ? 1 : -1; s_sh.y = (int16_t)(s_sh.y + d); s_sh.dir = d > 0 ? DIR_DOWN : DIR_UP; }
    if (++s_sh.moved >= 6) { s_sh.moved = 0; s_sh.step = (uint8_t)((s_sh.step + 1) & 3); }
    mark_rows(s_sh.y - 16, 48);
  }

  s_water_acc += dt;
  if (s_water_acc >= WATER_MS) {
    s_water_acc -= WATER_MS;
    s_water_frame ^= 1;
    for (int y = 0; y < GC_ROWS; y++)
      for (int x = 0; x < GC_COLS; x++)
        if (g_ground[y][x] == T_WATER) { g_dirty |= 1u << y; break; }
  }

  s_band_acc += dt;
  if (s_band_acc >= BAND_MS) {
    s_band_acc -= BAND_MS;
    for (int i = 0; i < 5; i++) {
      s_band_cur = (s_band_cur + 1) % 5;
      if (s_band_cur == 4 || g_status[s_band_cur][0]) break;
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

static void draw_top(uint16_t* out, int y0) {
  const uint16_t ink = g_ui[P_WHITE];
  fill_rect(out, y0, 0, 0, GC_W, GC_STRIP_H, g_ui[P_OUTLINE]);
  char t[6] = {'-', '-', ':', '-', '-', 0};
  if (g_clock_valid) { fmt2(t, g_hour); fmt2(t + 3, g_min); }
  draw_text(out, y0, 4, 1, t, ink, 2, 0);
  // the day in the middle
  char d[10] = {'D', 'A', 'Y', ' ', 0, 0, 0, 0};
  int n = G.s.day + 1, p = 4;
  if (n >= 10000) n = 9999;
  if (n >= 1000) d[p++] = (char)('0' + n / 1000);
  if (n >= 100) d[p++] = (char)('0' + (n / 100) % 10);
  if (n >= 10) d[p++] = (char)('0' + (n / 10) % 10);
  d[p++] = (char)('0' + n % 10);
  d[p] = 0;
  draw_text(out, y0, 78, 5, d, g_ui[P_GREY], 1, 0);
  // temperature at the right edge, weather icon left of it
  int deg, f;
  char tb[8];
  int len = 0;
  if (world_temp(&deg, &f)) {
    int a = deg < 0 ? -deg : deg;
    if (deg < 0) tb[len++] = '-';
    if (a >= 100) tb[len++] = (char)('0' + a / 100);
    if (a >= 10) tb[len++] = (char)('0' + (a / 10) % 10);
    tb[len++] = (char)('0' + a % 10);
    tb[len++] = f ? 'F' : 'C';
  } else {
    tb[len++] = '-'; tb[len++] = '-';
  }
  tb[len] = 0;
  int tx = GC_W - 4 - len * 12;
  draw_text(out, y0, tx, 1, tb, ink, 2, 0);
  weather_icon(out, y0, tx - 17, 2, world_weather(), (G.live_flags & GC_WXF_NIGHT) ? 1 : 0);
}

static void draw_band(uint16_t* out, int y0) {
  fill_rect(out, y0, 0, y0, GC_W, GC_STRIP_H, g_ui[P_OUTLINE]);
  char buf[32];
  if (s_band_cur == 4) {
    int n = G.s.day + 1, p = 0;
    static const char H[] = "DAY ";
    for (int i = 0; H[i]; i++) buf[p++] = H[i];
    if (n >= 10000) n = 9999;
    if (n >= 1000) buf[p++] = (char)('0' + n / 1000);
    if (n >= 100) buf[p++] = (char)('0' + (n / 100) % 10);
    if (n >= 10) buf[p++] = (char)('0' + (n / 10) % 10);
    buf[p++] = (char)('0' + n % 10);
    static const char T[] = "  THE SHAMAN WALKS";
    for (int i = 0; T[i]; i++) buf[p++] = T[i];
    buf[p] = 0;
  } else {
    str_copy(buf, g_status[s_band_cur], 32);
  }
  int len = str_len(buf);
  int scale = len > 19 ? 1 : 2;
  int x = (GC_W - len * 6 * scale) / 2;
  if (x < 2) x = 2;
  draw_text(out, y0, x, y0 + (scale == 2 ? 1 : 4), buf, g_ui[P_WHITE], scale, 0);
}

void world_render(int s, uint16_t* out) {
  int y0 = s * GC_STRIP_H;
  render_ground_row(out, s, s_water_frame);
  // the shaman, and a Z over his head in bed
  if (s_sh.y < y0 + GC_STRIP_H && s_sh.y + 16 > y0) {
    static const uint8_t cycle[4] = {0, 1, 0, 2};
    if (s_sh.act == GQ_ACT_SLEEP) {
      fill_rect(out, y0, s_sh.x + 1, s_sh.y + 9, 14, 6, 0xA145);     // a blanket
      blit_sprite(out, y0, s_sh.x, s_sh.y + 2, GC_CHARS(HERO_CELL_BASE[DIR_DOWN]), 0, 0, g_act);
    } else {
      int row = s_sh.dir == DIR_RIGHT ? (int)DIR_LEFT : (int)s_sh.dir;
      blit_sprite(out, y0, s_sh.x, s_sh.y, GC_CHARS(HERO_CELL_BASE[row] + cycle[s_sh.step]), s_sh.dir == DIR_RIGHT, 0, g_act);
    }
  }
  if (s_sh.act == GQ_ACT_SLEEP && s_sh.y - 14 < y0 + GC_STRIP_H && s_sh.y + 2 > y0)
    blit_sprite(out, y0, s_sh.x, s_sh.y - 14, GC_ITEMS(6), 0, 0, g_act);   // Z z z
  if (s == 0) draw_top(out, y0);
  if (s == BAND_STRIP) draw_band(out, y0);
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
extern "C" int gc_save_export(uint8_t* buf, int cap) {
  if (gc_scene_id() != GC_SCENE_VILLAGE || !buf || cap < GC_SAVE_BYTES) return 0;
  G.s.rng = gc_rng_get();
  G.s.seq++;
  G.s.crc32 = gq_save_crc((const uint8_t*)&G.s + 12, GQ_SAVE_SIZE - 12);
  const uint8_t* p = (const uint8_t*)&G.s;
  for (int i = 0; i < GQ_SAVE_SIZE; i++) buf[i] = p[i];
  return GC_SAVE_BYTES;
}

extern "C" int gc_save_import(const uint8_t* buf, int len) {
  if (gc_scene_id() != GC_SCENE_VILLAGE) return 0;
  if (gq_save_validate(buf, len) != GC_SAVE_OK) return 0;
  uint8_t* p = (uint8_t*)&G.s;
  for (int i = 0; i < GQ_SAVE_SIZE; i++) p[i] = buf[i];
  gq_save_sanitize(&G.s);
  s_play_ms = 0;
  start_world(G.s.seed);
  gc_rng_set(G.s.rng);
  return 1;
}

extern "C" uint32_t gc_save_serial(void) { return G.serial; }

extern "C" void gc_reset_game(void) {
  uint32_t seed = G.s.seed;
  reset_state();
  start_world(seed);
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
    default: return 0;
  }
}

void world_debug(int cmd, int32_t arg) {
  if (cmd == GQ_DBG_SET_DAY) { G.s.day = (uint16_t)(arg < 0 ? 0 : (arg > 65535 ? 65535 : arg)); gq_touch(); g_dirty |= 1u | (1u << BAND_STRIP); }
  else if (cmd == GQ_DBG_NEXT_DAY) world_new_day();
  else if (cmd == GQ_DBG_FORCE_ACT) {
    if (arg == GQ_ACT_SLEEP) { s_going_to_bed = 1; walk_to(BED_X, BED_Y); }
    else if (arg == GQ_ACT_WANDER) { s_going_to_bed = 0; s_sh.act = GQ_ACT_WANDER; s_sh.abed = 0; s_pause = 0; }
  }
}

extern "C" int32_t gc_query(int key) { return world_query(key); }
extern "C" void gc_debug(int cmd, int32_t arg) { world_debug(cmd, arg); }
