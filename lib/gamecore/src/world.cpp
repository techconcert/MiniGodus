// World generator, quarter autotiler and object drawing (release r2).
// Rules for the core: no heap, no libc, no floating point.
#include "world.h"

uint8_t   g_map[WM * WM];
WorldInfo g_world;
uint8_t   g_dist[WM * WM];

// per tile: four quarter art ids (NW, NE, SW, SE); the kind of the neighbour a quarter blends with follows from its art
// Quarter art ids of a window of tiles around the camera only (a ring buffer, slot = (ty % WH) * WW + tx % WW).
enum { WW = 16, WH = 14 };
static uint8_t g_qa[WW * WH * 4];
static int g_wx0 = -100, g_wy0 = -100;       // top left tile of the window (-100: nothing computed)
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

// distance of the tile (x, y), pushed around by a domain warp of about +-5 tiles, from an ellipse: 1024 at its radius
static int s_wx, s_wy;                       // warped position of the current tile in 1/16 tiles
static void warp_at(int x, int y) {
  s_wx = x * 16 + (vnoise(x * 16 / 7, y * 16 / 7, s_salt + 11) - 128) * 3 / 4 + (vnoise(x * 16 / 3, y * 16 / 3, s_salt + 13) - 128) / 4;
  s_wy = y * 16 + (vnoise(x * 16 / 7 + 900, y * 16 / 7, s_salt + 12) - 128) * 3 / 4 + (vnoise(x * 16 / 3, y * 16 / 3 + 900, s_salt + 14) - 128) / 4;
}
static int d2of(int x, int y, int cx, int cy, int rx, int ry) {
  (void)x; (void)y;
  int dx = s_wx - cx * 16, dy = s_wy - cy * 16;
  return dx * dx * 4 / (rx * rx) + dy * dy * 4 / (ry * ry);
}

static void shape_init(int landform) {
  Shape& s = s_sh;
  s.cx = 16 + (int)(wrnd() % 5) - 2; s.cy = 16 + (int)(wrnd() % 5) - 2;
  s.rx = 12 + (int)(wrnd() % 4); s.ry = 12 + (int)(wrnd() % 4);
  s.side = (int)(wrnd() & 3);
  s.n = 0;
  if (landform == LF_ARCH) {
    int want = 2 + (int)(wrnd() & 1);
    for (int tries = 0; tries < 200 && s.n < want; tries++) {
      int x = 7 + (int)(wrnd() % 18), y = 7 + (int)(wrnd() % 18), ok = 1;
      for (int i = 0; i < s.n; i++) { int dx = x - s.ax[i], dy = y - s.ay[i]; if (dx * dx + dy * dy < 14 * 14) ok = 0; }
      if (!ok) continue;
      s.ax[s.n] = x; s.ay[s.n] = y; s.ar[s.n] = 8 + (int)(wrnd() % 2); s.n++;
    }
    if (s.n < 2) { s.ax[0] = 9; s.ay[0] = 10; s.ar[0] = 7; s.ax[1] = 22; s.ay[1] = 22; s.ar[1] = 7; s.n = 2; }
  } else if (landform == LF_PLATEAU) {
    s.rx = 10 + (int)(wrnd() % 3); s.ry = s.rx;
  } else if (landform == LF_LAKE) {
    s.rx = 13 + (int)(wrnd() % 3); s.ry = 13 + (int)(wrnd() % 3);
  }
}

static int elevation(int landform, int x, int y) {
  const Shape& s = s_sh;
  int n = fbm(x, y, s_salt), nz = n - 128;
  warp_at(x, y);
  switch (landform) {
    case LF_ISLAND: return 256 - d2of(x, y, s.cx, s.cy, s.rx, s.ry) * 150 / 1024 + nz * 15 / 10;
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
      for (int i = 0; i < s.n; i++) best = imax(best, 250 - d2of(x, y, s.ax[i], s.ay[i], s.ar[i], s.ar[i]) * 11 / 100);
      return best + nz * 12 / 10;
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

static int s_rock_e = 262;                    // elevation from which land is rock (7): only the highest peaks
static int height_of(int e) {
  if (e < 60) return 0;
  if (e < 84) return 1;
  if (e < 100) return 2;
  if (e >= s_rock_e) return 7;
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

// Removes everything narrower than 2 tiles from every height band: a tile of height >= t with lower neighbours on both
// sides (left and right, or above and below) is a ribbon, spike or pillar and is lowered; a lower tile between two
// higher ones is a crack and is filled. Coasts and terraces end up at least 2 tiles wide.
enum { LAND_TARGET = 54 };
static void despeckle(void) {
  for (int t = 7; t >= 1; t--)
    for (int y = 0; y < WM; y++)
      for (int x = 0; x < WM; x++) {
        int h = hgt(x, y);
        if (h < t) continue;
        int l = x > 0 ? hgt(x - 1, y) : h, r = x < WM - 1 ? hgt(x + 1, y) : h, u = y > 0 ? hgt(x, y - 1) : h, d = y < WM - 1 ? hgt(x, y + 1) : h;
        if ((l < t && r < t) || (u < t && d < t)) g_map[TI(x, y)] = (uint8_t)((g_map[TI(x, y)] & ~7) | (t - 1));
      }
  for (int t = 1; t <= 7; t++)
    for (int y = 0; y < WM; y++)
      for (int x = 0; x < WM; x++) {
        int h = hgt(x, y);
        if (h >= t) continue;
        int l = x > 0 ? hgt(x - 1, y) : h, r = x < WM - 1 ? hgt(x + 1, y) : h, u = y > 0 ? hgt(x, y - 1) : h, d = y < WM - 1 ? hgt(x, y + 1) : h;
        if ((l >= t && r >= t) || (u >= t && d >= t)) g_map[TI(x, y)] = (uint8_t)((g_map[TI(x, y)] & ~7) | t);
      }
}

// ------------------------------------------------------------------------------------------------ walking
static int walk_ok(uint8_t b) { int h = MT_H(b); return h >= 2 && h <= 6 && MT_OBJ(b) == OB_NONE && MT_SOIL(b) != SOIL_BUILT; }
// A step between neighbouring tiles is possible if they differ by at most one layer (r4: terraforming cuts cliffs, ramps mend them)
int world_step_ok(int x, int y, int nx, int ny) {
  if (nx < 0 || ny < 0 || nx >= WM || ny >= WM) return 0;
  int d = MT_H(g_map[TI(x, y)]) - MT_H(g_map[TI(nx, ny)]);
  return d >= -1 && d <= 1;
}
// Sets the layer of a tile. Water takes no objects; shallow and lower land lose their snow.
void world_set_height(int x, int y, int h) {
  if (x < 0 || y < 0 || x >= WM || y >= WM) return;
  uint8_t b = g_map[TI(x, y)];
  b = (uint8_t)((b & ~7) | (h & 7));
  if (h < 3) b &= (uint8_t)~MT_SNOW;
  g_map[TI(x, y)] = b;
}
int world_walkable(int tx, int ty) { return tx >= 0 && ty >= 0 && tx < WM && ty < WM && walk_ok(g_map[TI(tx, ty)]); }

// Distance field in steps. world_flow_begin clears it, seeds are walkable tiles at distance 0 (or more), world_flow_run
// relaxes it. The target of world_flow itself may be a tile nobody can stand on (a building): its walkable neighbours
// are then the goal, at distance 0.
void world_flow_begin(void) { for (int i = 0; i < WM * WM; i++) g_dist[i] = 255; }
void world_flow_seed(int tx, int ty, int d) { if (tx >= 0 && ty >= 0 && tx < WM && ty < WM && world_walkable(tx, ty)) g_dist[TI(tx, ty)] = (uint8_t)d; }
void world_flow_run(void) {
  for (int pass = 0; pass < 120; pass++) {
    int changed = 0;
    for (int dir = 0; dir < 2; dir++)
      for (int k = 0; k < WM * WM; k++) {
        int i = dir ? WM * WM - 1 - k : k;
        if (!walk_ok(g_map[i])) continue;
        int x = i % WM, y = i / WM, m = g_dist[i];
        int h = MT_H(g_map[i]);
        if (x > 0 && g_dist[i - 1] + 1 < m && iabs(h - MT_H(g_map[i - 1])) <= 1) m = g_dist[i - 1] + 1;
        if (x < WM - 1 && g_dist[i + 1] + 1 < m && iabs(h - MT_H(g_map[i + 1])) <= 1) m = g_dist[i + 1] + 1;
        if (y > 0 && g_dist[i - WM] + 1 < m && iabs(h - MT_H(g_map[i - WM])) <= 1) m = g_dist[i - WM] + 1;
        if (y < WM - 1 && g_dist[i + WM] + 1 < m && iabs(h - MT_H(g_map[i + WM])) <= 1) m = g_dist[i + WM] + 1;
        if (m < g_dist[i]) { g_dist[i] = (uint8_t)m; changed = 1; }
      }
    if (!changed) break;
  }
}
// The walkable tile closest to (x, y) (Manhattan rings); returns 0 if there is none
int world_nearest_walkable(int x, int y, int* ox, int* oy) {
  for (int r = 0; r < WM; r++)
    for (int dy = -r; dy <= r; dy++)
      for (int dx = -r; dx <= r; dx++) {
        if (iabs(dx) + iabs(dy) != r) continue;
        if (world_walkable(x + dx, y + dy)) { *ox = x + dx; *oy = y + dy; return 1; }
      }
  return 0;
}
// Goal: every walkable tile next to (tx, ty) (the tile itself is not a goal)
void world_flow_adjacent(int tx, int ty) {
  world_flow_begin();
  world_flow_seed(tx - 1, ty, 0); world_flow_seed(tx + 1, ty, 0); world_flow_seed(tx, ty - 1, 0); world_flow_seed(tx, ty + 1, 0);
  world_flow_run();
}
void world_flow(int tx, int ty) {
  world_flow_begin();
  if (tx < 0 || ty < 0 || tx >= WM || ty >= WM) return;
  if (world_walkable(tx, ty)) world_flow_seed(tx, ty, 0);
  else { world_flow_seed(tx - 1, ty, 0); world_flow_seed(tx + 1, ty, 0); world_flow_seed(tx, ty - 1, 0); world_flow_seed(tx, ty + 1, 0); }
  world_flow_run();
}

// Occupies (on) a tile with a building: no object, nobody walks there.
void world_set_built(int tx, int ty, int on) {
  if (tx < 0 || ty < 0 || tx >= WM || ty >= WM) return;
  uint8_t& b = g_map[TI(tx, ty)];
  if (on) b = (uint8_t)((b & 0x0F) | (SOIL_BUILT << 4)); else b = (uint8_t)(b & 0x0F);
}
void world_clear_object(int tx, int ty) { if (tx >= 0 && ty >= 0 && tx < WM && ty < WM) g_map[TI(tx, ty)] &= 0x3F; }

// Walkable tiles connected to (tx, ty). Uses the shared distance buffer g_dist like every other search.
int world_flood_area(int tx, int ty) {
  world_flow(tx, ty);
  int n = 0;
  for (int i = 0; i < WM * WM; i++) if (g_dist[i] != 255) n++;
  return n;
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
        int a = world_flood_area(x, y);
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
  // islands and archipelagos are raised until about half of the map is land, with open sea along the border
  int bias = 0, sea_edge = landform == LF_ISLAND || landform == LF_ARCH;
  for (int attempt = 0; attempt < 10; attempt++) {
    int land = 0;
    {   // rock only on the highest peaks: at most 8% of the land (15% on the mountain plateau)
      uint16_t hist[16];
      int land_e = 0, rocky = 0;
      for (int i = 0; i < 16; i++) hist[i] = 0;
      for (int y = 0; y < WM; y++)
        for (int x = 0; x < WM; x++) {
          int e = elevation(landform, x, y) + bias;
          if (e >= 100) land_e++;
          if (e >= 262) { hist[imin(15, (e - 262) / 40)]++; rocky++; }
        }
      int quota = landform == LF_PLATEAU ? 10 : 4;
      s_rock_e = 262;
      for (int k = 0; k < 15 && rocky * 100 > quota * land_e; k++) { rocky -= hist[k]; s_rock_e += 40; }
    }
    for (int y = 0; y < WM; y++)
      for (int x = 0; x < WM; x++) {
        int h = height_of(elevation(landform, x, y) + bias);
        if (sea_edge && (x < 2 || y < 2 || x >= WM - 2 || y >= WM - 2)) h = 0;
        g_map[TI(x, y)] = (uint8_t)h;
        land += h >= 3;
      }
    if (!sea_edge || land * 100 >= WM * WM * LAND_TARGET) break;
    bias += 14;
  }
  for (int pass = 0; pass < 2; pass++) { despeckle(); smooth_heights(); }

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
  g_wx0 = g_wy0 = -100;                      // the caller sets the view (quarts_view) once the camera is placed
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
  if (kind >= KIND_SNOW) return ART_FILL_SNOW + (int)(h % 3);
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
  if ((bx | nb[0] | nb[1] | nb[2]) & MT_SNOW) {
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

static void window_tile(int x, int y) {
  int i = ((y % WH) * WW + (x % WW)) * 4, dummy;
  for (int q = 0; q < 4; q++) g_qa[i + q] = (uint8_t)quarter_art(x, y, q & 1, q >> 1, &dummy);
}

// Moves the window to cover the camera; only the tiles that come into view are computed.
void quarts_view(int cam_x, int cam_y) {
  int nx = (cam_x >> 4) < WM - WW ? (cam_x >> 4) : WM - WW, ny = (cam_y >> 4) < WM - WH ? (cam_y >> 4) : WM - WH;
  if (nx < 0) nx = 0;
  if (ny < 0) ny = 0;
  if (nx == g_wx0 && ny == g_wy0) return;
  int full = g_wx0 < -50 || iabs(nx - g_wx0) >= WW || iabs(ny - g_wy0) >= WH;
  for (int y = ny; y < ny + WH; y++)
    for (int x = nx; x < nx + WW; x++)
      if (full || x < g_wx0 || x >= g_wx0 + WW || y < g_wy0 || y >= g_wy0 + WH) window_tile(x, y);
  g_wx0 = nx; g_wy0 = ny;
}

// The map changed in the tiles x0..x1, y0..y1 (inclusive): recompute them and their neighbours where they are in the window.
void quarts_rebuild(int x0, int y0, int x1, int y1) {
  if (g_wx0 < -50) return;
  x0 = imax(g_wx0, x0 - 1); y0 = imax(g_wy0, y0 - 1); x1 = imin(g_wx0 + WW - 1, x1 + 1); y1 = imin(g_wy0 + WH - 1, y1 + 1);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) window_tile(x, y);
}

static inline int in_window(int x, int y) { return g_wx0 >= -50 && x >= g_wx0 && x < g_wx0 + WW && y >= g_wy0 && y < g_wy0 + WH; }

// 1 if tile (x, y) has a shimmering quarter
int quarts_tile_anim(int x, int y) {
  for (int q = 0; q < 4; q++) {
    int a;
    if (in_window(x, y)) a = g_qa[((y % WH) * WW + (x % WW)) * 4 + q];
    else { int d; a = quarter_art(x, y, q & 1, q >> 1, &d); }
    if (a < ART_AN_COUNT) return 1;
  }
  return 0;
}

// Role lookup for a quarter of a tile of kind `own` that blends with kind `oth`: role 1..13 -> RGB565.
// roles: 1 A dark, 2 A, 3 A light, 4 B dark, 5 B, 6 B light, 7 rim dark, 8 rim light, 9 face dark, 10 face, 11 face light, 12 sparkle, 13 shadow
static void role_table(uint16_t* l, int own, int oth) {
  static const uint8_t RAMP[KIND_COUNT][3] = {
    {2, 2, 3}, {2, 3, 4}, {7, 6, 6}, {8, 9, 10}, {9, 10, 11}, {10, 11, 12}, {11, 12, 12}, {15, 14, 6},
    {11, 12, 4}, {12, 4, 5}, {4, 5, 5}};            // 8..10: snow on height <= 4 (thin, over green), 5 (icy), 6 (white)
  static const uint8_t KH[KIND_COUNT] = {0, 1, 2, 3, 4, 5, 6, 7, 4, 5, 6};    // height of a kind
  const uint16_t* pal = s_pal;
  const uint8_t* a = RAMP[own]; const uint8_t* b = RAMP[oth];
  int up = KH[own] > KH[oth] ? own : (KH[own] < KH[oth] ? oth : (own >= KIND_SNOW ? own : oth));   // owner of the cliff face
  int rock = (up == 7);
  int snowy = up >= KIND_SNOW;
  int shore = own <= 1 || oth <= 1;
  l[0] = pal[0];
  l[1] = pal[a[0]]; l[2] = pal[a[1]]; l[3] = pal[a[2]];
  l[4] = pal[b[0]]; l[5] = pal[b[1]]; l[6] = pal[b[2]];
  l[7] = pal[shore ? 7 : 15];
  l[8] = pal[5];
  // face: shadow, earth or rock, and a warm lip (white under snow, never the water-like pale blue)
  l[9] = pal[15]; l[10] = pal[rock ? 14 : 13]; l[11] = pal[snowy ? 5 : (rock ? 6 : 7)];
  l[12] = pal[own == 9 ? 5 : (own >= KIND_SNOW ? 4 : 5)];
  l[13] = pal[15];
  l[14] = l[2]; l[15] = l[2];
}

void quarts_palette(const uint16_t* pal) { s_pal = pal; }

// The kind of neighbour a quarter blends with, from its art (fill art: none). snow: the tile is snow covered.
static int other_kind(int art, int own, int h, int snow) {
  int lower = snow && h - 1 >= 5 ? snow_kind(h - 1) : (h > 0 ? h - 1 : 0);
  int upper = snow ? snow_kind(h < 6 ? h + 1 : 6) : (h < 7 ? h + 1 : 7);
  if (art < ART_FILL_GROUND) {
    if (art < ART_WATER_UP) return own;
    if (art < ART_WATER_LOW) return 0;
    if (art < ART_SHORE_LOW) return 1;
    return 2;
  }
  if (art < ART_BEACH_UP) return own;
  if (art < ART_BEACH_LOW) return 2;
  if (art < ART_SNOW_UP) return 3;
  if (art < ART_SNOW_LOW) return h;                // snow beside grass of the same height
  if (art < ART_UPN) return snow_kind(h);          // grass beside snow
  if (art < ART_LON) return lower;
  return upper;
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
      int art;
      if (in_window(tx, ty)) art = g_qa[((ty % WH) * WW + (tx % WW)) * 4 + q];
      else { int d; art = quarter_art(tx, ty, qxi, qyi, &d); }
      if (art < ART_AN_COUNT) art |= frame;
      int own = map_kind(g_map[i]);
      int oth = other_kind(art & ~(art < ART_AN_COUNT ? 1 : 0), own, MT_H(g_map[i]), (g_map[i] & MT_SNOW) != 0);
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
