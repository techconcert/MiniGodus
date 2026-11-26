// World generator, quarter autotiler and object drawing (release r2).
// Rules for the core: no heap, no libc, no floating point.
#include "world.h"

uint8_t   g_map[WM * WM];
WorldInfo g_world;
uint8_t   g_dist[WM * WM];

// per tile: four quarter art ids (NW, NE, SW, SE); the kind of the neighbour a quarter blends with follows from its art
alignas(4) static uint8_t g_qa[WM * WM * 4];
static uint32_t g_anim[WM];                 // bit x set: tile (x, row) has an animated quarter
static const uint16_t* s_pal;               // world palette (g_wpal): the role tables are computed per quarter, not stored

static inline int TI(int x, int y) { return y * WM + x; }

// ------------------------------------------------------------------------------------------------ small helpers
static uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return x;
}
static uint32_t s_rng;
static uint32_t wrnd(void) { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }
static uint32_t s_salt;

static int lattice(int x, int y, uint32_t salt) { return (int)(hash32((uint32_t)x * 0x1f1f1f1fU ^ (uint32_t)y * 0x9e3779b1U ^ salt) & 255); }

// value noise, coordinates in 1/16 cells, result 0..255
static int vnoise(int fx, int fy, uint32_t salt) {
  int x0 = fx >> 4, y0 = fy >> 4, tx = fx & 15, ty = fy & 15;
  int sx = tx * tx * (48 - 2 * tx) / 16, sy = ty * ty * (48 - 2 * ty) / 16;     // smoothstep, 0..256
  int a = lattice(x0, y0, salt), b = lattice(x0 + 1, y0, salt), c = lattice(x0, y0 + 1, salt), d = lattice(x0 + 1, y0 + 1, salt);
  int top = a + ((b - a) * sx >> 8), bot = c + ((d - c) * sx >> 8);
  return top + ((bot - top) * sy >> 8);
}
// three octaves with cells of 8, 4 and 2 tiles
static int fbm(int x, int y, uint32_t salt) {
  return (vnoise(x * 2, y * 2, salt) * 4 + vnoise(x * 4, y * 4, salt + 1) * 2 + vnoise(x * 8, y * 8, salt + 2)) / 7;
}
static int iabs(int v) { return v < 0 ? -v : v; }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static inline int hgt(int x, int y) { return MT_H(g_map[TI(x, y)]); }

// ------------------------------------------------------------------------------------------------ landform and climate
void world_pick(uint32_t seed, int prev_l, int prev_c, int* landform, int* climate) {
  uint32_t s = seed;
  for (int i = 0; i < 64; i++) {
    uint32_t h = hash32(s ^ 0x6d2b79f5U);
    int l = (int)(h % LF_COUNT), c = (int)((h >> 8) % CL_COUNT);
    if (prev_l < 0 || l != prev_l || c != prev_c) { *landform = l; *climate = c; return; }
    s = h + 0x9e3779b9U;
  }
  *landform = prev_l;
  *climate = (prev_c + 1) % CL_COUNT;
}

// ------------------------------------------------------------------------------------------------ elevation per landform
// e is about 0..300: below 60 deep water, below 84 shallow, below 100 beach, then 40 per land layer, 262 and up rock.
struct Shape { int cx, cy, rx, ry, side; int ax[3], ay[3], ar[3]; int n; };
static Shape s_sh;

static int d2of(int x, int y, int cx, int cy, int rx, int ry) {
  int dx = x - cx, dy = y - cy;
  return dx * dx * 1024 / (rx * rx) + dy * dy * 1024 / (ry * ry);
}

static void shape_init(int landform) {
  Shape& s = s_sh;
  s.cx = 16 + (int)(wrnd() % 5) - 2; s.cy = 16 + (int)(wrnd() % 5) - 2;
  s.rx = 10 + (int)(wrnd() % 4); s.ry = 10 + (int)(wrnd() % 4);
  s.side = (int)(wrnd() & 3);
  s.n = 0;
  if (landform == LF_ARCH) {
    int want = 2 + (int)(wrnd() & 1);
    for (int tries = 0; tries < 200 && s.n < want; tries++) {
      int x = 7 + (int)(wrnd() % 18), y = 7 + (int)(wrnd() % 18), ok = 1;
      for (int i = 0; i < s.n; i++) { int dx = x - s.ax[i], dy = y - s.ay[i]; if (dx * dx + dy * dy < 13 * 13) ok = 0; }
      if (!ok) continue;
      s.ax[s.n] = x; s.ay[s.n] = y; s.ar[s.n] = 5 + (int)(wrnd() % 3); s.n++;
    }
    if (s.n < 2) { s.ax[0] = 9; s.ay[0] = 10; s.ar[0] = 6; s.ax[1] = 22; s.ay[1] = 22; s.ar[1] = 6; s.n = 2; }
  } else if (landform == LF_PLATEAU) {
    s.rx = 10 + (int)(wrnd() % 3); s.ry = s.rx;
  } else if (landform == LF_LAKE) {
    s.rx = 13 + (int)(wrnd() % 3); s.ry = 13 + (int)(wrnd() % 3);
  }
}

static int elevation(int landform, int x, int y) {
  const Shape& s = s_sh;
  int n = fbm(x, y, s_salt), nz = n - 128;
  switch (landform) {
    case LF_ISLAND: return 232 - d2of(x, y, s.cx, s.cy, s.rx, s.ry) * 190 / 1024 + nz * 11 / 20;
    case LF_COAST: {
      int u = s.side == 0 ? x : s.side == 1 ? 31 - x : s.side == 2 ? y : 31 - y;
      return 30 + u * 7 + nz * 9 / 10;
    }
    case LF_RIVER: {
      int a = (s.side & 1) ? y : x, b = (s.side & 1) ? x : y;
      int xr = 256 + (vnoise(b * 16 / 6, 0, s_salt + 40) - 128) * 7 / 8 + (vnoise(b * 16 / 3, 0, s_salt + 41) - 128) / 4;
      int dist = iabs(a * 16 + 8 - xr);
      return 40 + dist * 100 / 100 + nz * 25 / 100;
    }
    case LF_LAKE: return 36 + d2of(x, y, s.cx, s.cy, s.rx, s.ry) * 22 / 100 + nz * 45 / 100;
    case LF_ARCH: {
      int best = -300;
      for (int i = 0; i < s.n; i++) best = imax(best, 232 - d2of(x, y, s.ax[i], s.ay[i], s.ar[i], s.ar[i]) * 20 / 100);
      return best + nz * 5 / 10;
    }
    default: {   // plateau
      int d2 = d2of(x, y, s.cx, s.cy, s.rx, s.ry);
      int p = d2 < 260 ? 240 : 240 - (d2 - 260) * 25 / 100;
      p += nz * 4 / 10;
      int u = (s.side == 0 ? x : s.side == 1 ? 31 - x : s.side == 2 ? y : 31 - y) + nz / 14;
      int low = 112 + (fbm(x, y, s_salt + 90) - 128) * 3 / 10;
      if (u < 9) low -= (9 - u) * 22;
      return imax(p, low);
    }
  }
}

static int height_of(int e) {
  if (e < 60) return 0;
  if (e < 84) return 1;
  if (e < 100) return 2;
  if (e >= 262) return 7;
  return 3 + imin(3, (e - 100) / 40);
}

// Neighbouring tiles differ by at most one step: the higher one is lowered.
static void smooth_heights(void) {
  for (int pass = 0; pass < 60; pass++) {
    int changed = 0;
    for (int y = 0; y < WM; y++)
      for (int x = 0; x < WM; x++) {
        int h = hgt(x, y), m = h;
        if (x > 0) m = imin(m, hgt(x - 1, y) + 1);
        if (x < WM - 1) m = imin(m, hgt(x + 1, y) + 1);
        if (y > 0) m = imin(m, hgt(x, y - 1) + 1);
        if (y < WM - 1) m = imin(m, hgt(x, y + 1) + 1);
        if (m < h) { g_map[TI(x, y)] = (uint8_t)((g_map[TI(x, y)] & ~7) | m); changed = 1; }
      }
    if (!changed) break;
  }
}

// ------------------------------------------------------------------------------------------------ walking
static int walk_ok(uint8_t b) { int h = MT_H(b); return h >= 2 && h <= 6 && MT_OBJ(b) == OB_NONE; }
int world_walkable(int tx, int ty) { return tx >= 0 && ty >= 0 && tx < WM && ty < WM && walk_ok(g_map[TI(tx, ty)]); }

// stack of tile numbers kept in the quarter table (it is rebuilt after generation anyway)
static int flood(int sx, int sy, int mark) {
  for (int i = 0; i < WM * WM; i++) g_dist[i] = 0;
  if (!world_walkable(sx, sy)) return 0;
  uint8_t* st = g_qa;
  int sp = 0, count = 0;
  st[0] = (uint8_t)TI(sx, sy); st[1] = (uint8_t)(TI(sx, sy) >> 8); sp = 1;
  g_dist[TI(sx, sy)] = 1;
  static const int8_t D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  while (sp) {
    sp--;
    int t = st[sp * 2] | (st[sp * 2 + 1] << 8);
    count++;
    int x = t % WM, y = t / WM;
    for (int d = 0; d < 4; d++) {
      int nx = x + D[d][0], ny = y + D[d][1];
      if (!world_walkable(nx, ny) || g_dist[TI(nx, ny)]) continue;
      g_dist[TI(nx, ny)] = 1;
      st[sp * 2] = (uint8_t)TI(nx, ny); st[sp * 2 + 1] = (uint8_t)(TI(nx, ny) >> 8); sp++;
    }
  }
  (void)mark;
  return count;
}
int world_flood_area(int tx, int ty) {
  int n = flood(tx, ty, 0);
  quarts_rebuild(0, 0, WM - 1, WM - 1);      // the flood used the quarter table as its stack
  return n;
}

void world_flow(int tx, int ty) {
  for (int i = 0; i < WM * WM; i++) g_dist[i] = 255;
  if (!world_walkable(tx, ty)) return;
  g_dist[TI(tx, ty)] = 0;
  for (int pass = 0; pass < 120; pass++) {
    int changed = 0;
    for (int dir = 0; dir < 2; dir++)
      for (int k = 0; k < WM * WM; k++) {
        int i = dir ? WM * WM - 1 - k : k;
        if (!walk_ok(g_map[i])) continue;
        int x = i % WM, y = i / WM, m = g_dist[i];
        if (x > 0 && g_dist[i - 1] + 1 < m) m = g_dist[i - 1] + 1;
        if (x < WM - 1 && g_dist[i + 1] + 1 < m) m = g_dist[i + 1] + 1;
        if (y > 0 && g_dist[i - WM] + 1 < m) m = g_dist[i - WM] + 1;
        if (y < WM - 1 && g_dist[i + WM] + 1 < m) m = g_dist[i + WM] + 1;
        if (m < g_dist[i]) { g_dist[i] = (uint8_t)m; changed = 1; }
      }
    if (!changed) break;
  }
}

// ------------------------------------------------------------------------------------------------ names
static void make_name(uint32_t seed) {
  static const char* const A[] = {"KA", "TO", "MI", "RA", "EL", "DA", "VO", "NE", "SU", "LI", "BRA", "THO", "ZE", "OR", "AN", "KOR", "MAL", "SEN", "TAR", "VAL"};
  static const char* const B[] = {"MA", "RO", "NI", "LU", "DE", "SA", "VI", "TE", "KO", "PA", "REN", "DOR", "LIN", "MOR"};
  static const char* const C[] = {"IA", "OS", "AR", "EN", "UM", "ON", "A", "IS", "OR", "ETH", "ANA", "UL"};
  uint32_t h = hash32(seed ^ 0x51ed270bU);
  char* n = g_world.name;
  int p = 0;
  const char* parts[3] = {A[h % 20], B[(h >> 8) % 14], C[(h >> 16) % 12]};
  int np = (h >> 24) & 1 ? 3 : 2;
  if (np == 2) parts[1] = parts[2];
  for (int i = 0; i < np; i++)
    for (const char* q = parts[i]; *q && p < 10; q++) n[p++] = *q;
  n[p] = 0;
}

// ------------------------------------------------------------------------------------------------ generator
// Fallback for land without a flat spot: level a 5x5 patch at the height of the nearest land tile and ramp it down.
static void carve_start(int cx, int cy) {
  cx = imax(6, imin(WM - 7, cx)); cy = imax(6, imin(WM - 7, cy));
  int hc = imax(3, imin(6, hgt(cx, cy)));
  for (int y = 0; y < WM; y++)
    for (int x = 0; x < WM; x++) {
      int r = imax(iabs(x - cx), iabs(y - cy));
      if (r > 6) continue;
      int want = r <= 2 ? hc : imax(0, hc - (r - 2));
      if (r <= 2 || hgt(x, y) < want) g_map[TI(x, y)] = (uint8_t)want;
    }
  smooth_heights();
}

static int is_flat(int x, int y, int cross) {
  if (x < 3 || y < 3 || x > WM - 4 || y > WM - 4) return 0;
  int h = hgt(x, y);
  if (h < 3 || h > 6) return 0;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
      if ((!cross || !dx || !dy) && hgt(x + dx, y + dy) != h) return 0;
  return 1;
}

static int find_start(int* ox, int* oy, int cross) {
  int sx = 0, sy = 0, n = 0;
  for (int y = 0; y < WM; y++) for (int x = 0; x < WM; x++) if (hgt(x, y) >= 3) { sx += x; sy += y; n++; }
  if (!n) return 0;
  int cx = sx / n, cy = sy / n, tried = 0, best = 0;
  for (int r = 0; r < WM && tried < 24; r++)
    for (int y = cy - r; y <= cy + r && tried < 24; y++)
      for (int x = cx - r; x <= cx + r && tried < 24; x++) {
        if (x < 0 || y < 0 || x >= WM || y >= WM) continue;
        if (imax(iabs(x - cx), iabs(y - cy)) != r || !is_flat(x, y, cross)) continue;
        tried++;
        int a = flood(x, y, 0);
        if (a > best) { best = a; *ox = x; *oy = y; }
        if (a >= 60) return a;
      }
  return best;
}

static void scatter(int landform, int climate) {
  // clusters of trees from a second noise field, plus a few single trees
  static const uint8_t THR[CL_COUNT] = {150, 190, 140, 135};
  static const uint8_t DENS[CL_COUNT] = {60, 35, 65, 65};
  for (int y = 0; y < WM; y++)
    for (int x = 0; x < WM; x++) {
      uint8_t& b = g_map[TI(x, y)];
      int h = MT_H(b), cn = fbm(x + 40, y + 17, s_salt + 200);
      uint32_t r = wrnd() % 100;
      int north_higher = y > 0 && hgt(x, y - 1) > h;
      if (h >= 3 && h <= 6) {
        if (!north_higher && ((cn > THR[climate] && r < DENS[climate]) || r < 2)) b = (uint8_t)((b & 0x3F) | (OB_TREE << 6));
        else if (!north_higher && MT_SOIL(b) == SOIL_FERTILE && r < 8) b = (uint8_t)((b & 0x3F) | (OB_BUSH << 6));
        else if (!north_higher && h >= 4 && r < (landform == LF_PLATEAU ? 7u : 3u)) b = (uint8_t)((b & 0x3F) | (OB_ROCK << 6));
      } else if (h == 2 && climate == CL_TROPICAL && !north_higher && r < 12) {
        b = (uint8_t)((b & 0x3F) | (OB_TREE << 6));      // palms on the beach
      } else if (h <= 1 && r < 4) {
        b = (uint8_t)((b & 0x3F) | (OB_BUSH << 6));      // fish
      }
    }
}

void world_generate(uint32_t seed, int landform, int climate) {
  if (landform < 0 || landform >= LF_COUNT) landform = 0;
  if (climate < 0 || climate >= CL_COUNT) climate = 0;
  g_world.landform = (uint8_t)landform; g_world.climate = (uint8_t)climate;
  s_rng = hash32(seed ^ 0xa511e9b3U) | 1;
  s_salt = hash32(seed + 77);
  shape_init(landform);
  for (int y = 0; y < WM; y++)
    for (int x = 0; x < WM; x++) g_map[TI(x, y)] = (uint8_t)height_of(elevation(landform, x, y));
  smooth_heights();

  // ground: snow caps in the north, fertile soil by the water, dry soil on the savanna
  for (int y = 0; y < WM; y++)
    for (int x = 0; x < WM; x++) {
      uint8_t& b = g_map[TI(x, y)];
      int h = MT_H(b), near_water = 0;
      for (int dy = -2; dy <= 2 && !near_water; dy++)
        for (int dx = -2; dx <= 2; dx++) {
          int nx = x + dx, ny = y + dy;
          if (nx >= 0 && ny >= 0 && nx < WM && ny < WM && hgt(nx, ny) <= 1) { near_water = 1; break; }
        }
      if (climate == CL_NORTHERN && h >= 3 && h <= 6 && (h >= 5 || (h == 4 && fbm(x, y, s_salt + 300) > 150))) b |= MT_SNOW;
      if (h == 3 || h == 4) {
        if (near_water && climate != CL_NORTHERN) b = (uint8_t)(b | (SOIL_FERTILE << 4));
        else if (climate == CL_SAVANNA && !near_water) b = (uint8_t)(b | (SOIL_DRY << 4));
      }
    }
  scatter(landform, climate);

  // a flat start with a big enough connected walkable area; carve one if the land has none
  int sx = 16, sy = 16;
  int area = find_start(&sx, &sy, 0);
  if (area < 24) area = find_start(&sx, &sy, 1);
  if (area < 24) {
    int cx = 16, cy = 16, n = 0, tx = 0, ty = 0, bd = 9999;
    for (int y = 0; y < WM; y++) for (int x = 0; x < WM; x++) if (hgt(x, y) >= 3) { tx += x; ty += y; n++; }
    if (n) {
      tx /= n; ty /= n;
      for (int y = 6; y < WM - 6; y++) for (int x = 6; x < WM - 6; x++)
        if (hgt(x, y) >= 3 && (x - tx) * (x - tx) + (y - ty) * (y - ty) < bd) { bd = (x - tx) * (x - tx) + (y - ty) * (y - ty); cx = x; cy = y; }
    }
    carve_start(cx, cy);
    sx = imax(6, imin(WM - 7, cx)); sy = imax(6, imin(WM - 7, cy));
    for (int y = sy - 5; y <= sy + 5; y++)
      for (int x = sx - 5; x <= sx + 5; x++) g_map[TI(x, y)] &= 0x07;      // plain ground, no snow, no objects
  }
  for (int y = sy - 2; y <= sy + 2; y++)
    for (int x = sx - 2; x <= sx + 2; x++)
      if (x >= 0 && y >= 0 && x < WM && y < WM) g_map[TI(x, y)] &= 0x3F;
  g_world.start_x = (uint8_t)sx; g_world.start_y = (uint8_t)sy;
  make_name(seed);
  quarts_rebuild(0, 0, WM - 1, WM - 1);
}

uint32_t world_map_hash(void) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < WM * WM; i++) h = (h ^ g_map[i]) * 16777619u;
  return h;
}

// ------------------------------------------------------------------------------------------------ quarter autotiler
// Corner based: each 8x8 quarter looks at its own tile and the three tiles that touch its corner (horizontal,
// vertical, diagonal). The art is painted for the north-west corner; the east quarters are drawn mirrored.
enum { SH_EH, SH_EV, SH_OUT };

static int fill_art(int kind, int x, int y, int q, int soil) {
  uint32_t h = hash32((uint32_t)(x * 4 + (q & 1)) * 73856093U ^ (uint32_t)(y * 4 + (q >> 1)) * 19349663U ^ 0x1234U);
  if (kind <= 1) return ART_FILL_WATER + (int)(h % 4) * 2;
  if (kind == 2) return ART_FILL_SAND + (int)(h % 4);
  if (kind == 7) return ART_FILL_ROCK + (int)(h % 4);
  if (kind == KIND_SNOW) return ART_FILL_SNOW + (int)(h % 3);
  int r = (int)(h % 16);
  if (soil == SOIL_DRY) return ART_FILL_GROUND + (r < 8 ? 0 : (r < 13 ? 1 : 2));
  if (soil == SOIL_FERTILE) return ART_FILL_GROUND + (r < 3 ? 0 : r < 5 ? 1 : r < 8 ? 2 : r < 11 ? 3 : r < 13 ? 4 : 5);
  return ART_FILL_GROUND + (r < 5 ? 0 : r < 9 ? 1 : r < 12 ? 2 : r < 14 ? 3 : r < 15 ? 4 : 5);
}

static int shape_of(int bh, int bv) { return (bh && bv) ? SH_OUT : (bh ? SH_EH : SH_EV); }

// art id of one quarter; *other = kind of the tile it blends with
static int quarter_art(int x, int y, int qx, int qy, int* other) {
  uint8_t bx = g_map[TI(x, y)];
  int hx = MT_H(bx), kx = map_kind(bx), soil = MT_SOIL(bx);
  int dx = qx ? 1 : -1, dy = qy ? 1 : -1;
  int nx[3] = {x + dx, x, x + dx}, ny[3] = {y, y + dy, y + dy};   // H, V, D
  uint8_t nb[3];
  for (int i = 0; i < 3; i++) {
    int ax = nx[i] < 0 || nx[i] >= WM ? x : nx[i], ay = ny[i] < 0 || ny[i] >= WM ? y : ny[i];
    nb[i] = g_map[TI(ax, ay)];
  }
  int hi[3], lo[3], any_hi = 0, any_lo = 0;
  for (int i = 0; i < 3; i++) {
    hi[i] = MT_H(nb[i]) > hx; lo[i] = MT_H(nb[i]) < hx;
    any_hi |= hi[i]; any_lo |= lo[i];
  }
  *other = kx;
  int q = qy * 2 + qx;
  if (any_hi || any_lo) {
    const int* set = any_hi ? hi : lo;
    int yi = set[1] ? 1 : (set[0] ? 0 : 2);
    int hy = MT_H(nb[yi]);
    *other = map_kind(nb[yi]);
    int lowmode = any_hi;
    int lo_h = imin(hx, hy), hi_h = imax(hx, hy);
    if (lo_h >= 3) {                                   // terraces: cliffs
      if (lowmode) {
        if (qy == 0) {
          if (hi[1]) return hi[0] ? ART_LON + 2 : (hi[2] ? ART_LON + 0 : ART_LON + 1);
          return hi[0] ? ART_LON + 3 : fill_art(kx, x, y, q, soil);
        }
        return hi[0] ? ART_LOS : fill_art(kx, x, y, q, soil);
      }
      int bh = lo[0], bv = lo[1];
      if (!bh && !bv) return fill_art(kx, x, y, q, soil);
      return (qy == 0 ? ART_UPN : ART_UPS) + shape_of(bh, bv);
    }
    int base;
    if (hi_h <= 1) base = lowmode ? ART_WATER_LOW : ART_WATER_UP;
    else if (lo_h <= 1) base = lowmode ? ART_SHORE_LOW : ART_SHORE_UP;
    else base = lowmode ? ART_BEACH_LOW : ART_BEACH_UP;
    int bh = set[0], bv = set[1];
    if (!bh && !bv) return fill_art(kx, x, y, q, soil);
    int step = (base < ART_AN_COUNT) ? 2 : 1;
    return base + shape_of(bh, bv) * step;
  }
  // same height: a snow edge
  if (kx == KIND_SNOW || MT_SNOW & (nb[0] | nb[1] | nb[2])) {
    int diff[3];
    int any = 0;
    for (int i = 0; i < 3; i++) { diff[i] = ((nb[i] & MT_SNOW) != 0) != ((bx & MT_SNOW) != 0); any |= diff[i]; }
    if (any) {
      int yi = diff[1] ? 1 : (diff[0] ? 0 : 2);
      *other = map_kind(nb[yi]);
      int bh = diff[0], bv = diff[1];
      if (!bh && !bv) return fill_art(kx, x, y, q, soil);
      return ((bx & MT_SNOW) ? ART_SNOW_UP : ART_SNOW_LOW) + shape_of(bh, bv);
    }
  }
  return fill_art(kx, x, y, q, soil);
}

void quarts_rebuild(int x0, int y0, int x1, int y1) {
  x0 = imax(0, x0 - 1); y0 = imax(0, y0 - 1); x1 = imin(WM - 1, x1 + 1); y1 = imin(WM - 1, y1 + 1);
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      int i = TI(x, y), oth[4];
      uint32_t anim = 0;
      for (int q = 0; q < 4; q++) {
        int a = quarter_art(x, y, q & 1, q >> 1, &oth[q]);
        g_qa[i * 4 + q] = (uint8_t)a;
        if (a < ART_AN_COUNT) anim = 1;
      }
      if (anim) g_anim[y] |= 1u << x; else g_anim[y] &= ~(1u << x);
    }
  }
}

int quarts_anim_rows(int ty) { return (ty >= 0 && ty < WM) ? (int)g_anim[ty] : 0; }

// Role lookup for a quarter of a tile of kind `own` that blends with kind `oth`: role 1..13 -> RGB565.
// roles: 1 A dark, 2 A, 3 A light, 4 B dark, 5 B, 6 B light, 7 rim dark, 8 rim light, 9 face dark, 10 face, 11 face light, 12 sparkle, 13 shadow
static void role_table(uint16_t* l, int own, int oth) {
  static const uint8_t RAMP[KIND_COUNT][3] = {
    {2, 2, 3}, {2, 3, 4}, {7, 6, 6}, {8, 9, 10}, {9, 10, 11}, {10, 11, 12}, {11, 12, 12}, {15, 14, 4}, {4, 5, 5}};
  const uint16_t* pal = s_pal;
  const uint8_t* a = RAMP[own]; const uint8_t* b = RAMP[oth];
  int upper = own > oth ? own : oth;                 // which tile owns the cliff face material
  int upk = (own == KIND_SNOW || oth == KIND_SNOW) ? KIND_SNOW : upper;
  int rock = (upper == 7);
  int shore = own <= 1 || oth <= 1;
  l[0] = pal[0];
  l[1] = pal[a[0]]; l[2] = pal[a[1]]; l[3] = pal[a[2]];
  l[4] = pal[b[0]]; l[5] = pal[b[1]]; l[6] = pal[b[2]];
  l[7] = pal[shore ? 7 : 15];
  l[8] = pal[5];
  l[9] = pal[15]; l[10] = pal[rock ? 14 : 13]; l[11] = pal[rock ? 4 : (upk == KIND_SNOW ? 5 : 7)];
  l[12] = pal[own == KIND_SNOW ? 4 : 5];
  l[13] = pal[15];
  l[14] = l[2]; l[15] = l[2];
}

void quarts_palette(const uint16_t* pal) { s_pal = pal; }

// The kind of neighbour a quarter blends with, from its art (fill art: none)
static int other_kind(int art, int own, int h) {
  if (art < ART_FILL_GROUND) {
    if (art < ART_WATER_UP) return own;
    if (art < ART_WATER_LOW) return 0;
    if (art < ART_SHORE_UP) return 1;
    if (art < ART_SHORE_LOW) return 1;
    return 2;
  }
  if (art < ART_BEACH_UP) return own;
  if (art < ART_BEACH_LOW) return 2;
  if (art < ART_SNOW_UP) return 3;
  if (art < ART_SNOW_LOW) return h;
  if (art < ART_UPN) return KIND_SNOW;
  if (art < ART_LON) return h > 0 ? h - 1 : 0;
  return h < 7 ? h + 1 : 7;
}

// Draws the quarters of the strip (240x16) whose top is world pixel row wy0 and whose left is world column wx0.
void quarts_render(uint16_t* out, int wx0, int wy0, int frame) {
  uint16_t lut[16];
  int lut_own = -1, lut_oth = -1;
  for (int qy = wy0 >> 3; qy <= (wy0 + 15) >> 3; qy++) {
    int py = qy * 8 - wy0;
    int r0 = py < 0 ? -py : 0, r1 = py + 8 > GC_STRIP_H ? GC_STRIP_H - py : 8;
    int ty = qy >> 1, qyi = qy & 1;
    for (int qx = wx0 >> 3; qx <= (wx0 + GC_W - 1) >> 3; qx++) {
      int px = qx * 8 - wx0;
      int c0 = px < 0 ? -px : 0, c1 = px + 8 > GC_W ? GC_W - px : 8;
      int tx = qx >> 1, qxi = qx & 1, i = TI(tx, ty);
      int q = qyi * 2 + qxi;
      int art = g_qa[i * 4 + q];
      if (art < ART_AN_COUNT) art |= frame;
      int own = map_kind(g_map[i]);
      int oth = other_kind(art & ~(art < ART_AN_COUNT ? 1 : 0), own, MT_H(g_map[i]));
      if (own != lut_own || oth != lut_oth) { role_table(lut, own, oth); lut_own = own; lut_oth = oth; }
      const uint8_t* src = GC_QUARTS(art);
      for (int r = r0; r < r1; r++) {
        uint16_t* dst = out + (py + r) * GC_W + px;
        uint8_t b0 = GC_RD8(src + r * 4), b1 = GC_RD8(src + r * 4 + 1), b2 = GC_RD8(src + r * 4 + 2), b3 = GC_RD8(src + r * 4 + 3);
        uint8_t px8[8] = {(uint8_t)(b0 >> 4), (uint8_t)(b0 & 15), (uint8_t)(b1 >> 4), (uint8_t)(b1 & 15),
                          (uint8_t)(b2 >> 4), (uint8_t)(b2 & 15), (uint8_t)(b3 >> 4), (uint8_t)(b3 & 15)};
        if (qxi) { for (int c = c0; c < c1; c++) dst[c] = lut[px8[7 - c]]; }
        else     { for (int c = c0; c < c1; c++) dst[c] = lut[px8[c]]; }
      }
    }
  }
}

// ------------------------------------------------------------------------------------------------ objects
// objects.png: 0-2 temperate trees, 3-5 savanna, 6-8 northern pines (8 = snowy), 9-11 tropical, 12-15 bushes,
// 16 boulder, 17 ore boulder, 18-19 fish
static int object_cell(int x, int y, uint8_t b, int frame) {
  uint32_t h = hash32((uint32_t)x * 2654435761U ^ (uint32_t)y * 40503U);
  int o = MT_OBJ(b), c = g_world.climate;
  if (o == OB_TREE) {
    int v = (int)(h % 3);
    if (c == CL_NORTHERN) v = (b & MT_SNOW) ? 2 : (int)(h & 1);
    if (c == CL_TROPICAL && MT_H(b) == 2) v = (int)(h & 1);      // palms on the beach
    return c * 3 + v;
  }
  if (o == OB_BUSH) return MT_H(b) <= 1 ? 18 + frame : 12 + c;
  return (h & 3) == 0 ? 17 : 16;
}

void draw_objects(uint16_t* out, int wx0, int wy0) {
  int ty0 = (wy0 - 2) >> 4, ty1 = (wy0 + 15 + 6) >> 4, tx0 = (wx0 - 16) >> 4, tx1 = (wx0 + GC_W - 1) >> 4;
  if (ty0 < 0) ty0 = 0;
  if (ty1 > WM - 1) ty1 = WM - 1;
  if (tx0 < 0) tx0 = 0;
  if (tx1 > WM - 1) tx1 = WM - 1;
  for (int ty = ty0; ty <= ty1; ty++)
    for (int tx = tx0; tx <= tx1; tx++) {
      uint8_t b = g_map[TI(tx, ty)];
      if (!MT_OBJ(b)) continue;
      int cell = object_cell(tx, ty, b, g_wframe);
      // trees stand a little higher than their tile so the crown overlaps the tile above
      int oy = (MT_OBJ(b) == OB_TREE) ? -3 : 0;
      blit_sprite(out, wy0, tx * GC_TILE - wx0, ty * GC_TILE + oy, GC_OBJECTS(cell), 0, 0, g_wpal);
    }
}
