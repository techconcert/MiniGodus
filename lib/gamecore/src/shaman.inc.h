// The shaman of MiniGodus (release r4): the decision maker. Included by village.cpp (it shares the village's statics).
//
// Every 10 game minutes, while he is free, he scores the village's needs (food, housing, water, health, wood, growth,
// faith, monument), picks the most pressing project he can afford and carries it out one tile at a time: godus.cpp walks
// him beside the tile and plays the staff animation, then shaman_apply() pays the faith and changes the tile.
//
// Projects: lay out a building (levelling its plot first), dig a pond, flatten a 3x3 plot for the next home tier.
// A site search picks the spot of lowest cost = faith to level + walking distance from the village green + penalties for
// reserved tiles. Near the village neighbouring tiles differ by at most one layer: after every project he cuts ramps
// until no step is left. Everything lives in about 100 bytes; every search uses the shared distance buffer g_dist.

enum { N_FOOD, N_HOUSING, N_WATER, N_HEALTH, N_WOOD, N_GROWTH, N_FAITH, N_MONUMENT, N_POND, N_SHAPE, N_COUNT };
enum { PK_NONE, PK_BUILD, PK_POND, PK_PLOT, PK_LEVEL, PK_STAGE, PK_ACCESS };     // PK_ACCESS (r7): ramps so that the village can reach a place
enum { SP_POND = B_COUNT, SP_PLOT = B_COUNT + 1, SP_LEVEL = B_COUNT + 2, SP_STAGE = B_COUNT + 3 };      // search kinds besides the buildings
enum { WAIT_MAX = 36, MAX_OPS = 100, RAMP_COST = 2, AC_BUILDING = 0, AC_SHORE = 1, AC_TREE = 2, AC_TILE = 3 };

struct Proj { uint8_t kind, tp, var, x, y, w, h, hgt, ops, wait; };   // hgt 255: no levelling (fishing hut)
static Proj     P;
static uint8_t  s_pers, s_waiting, s_ramping;
enum { NFAIL = 12 };
static uint8_t  s_fail[NFAIL][2], s_failn, s_pneed;
static uint16_t s_turned;                                // sites turned down at planning (a statistic)
static uint16_t s_aborts;                                // projects the shaman gave up (a statistic)                   // spots where a project failed
static uint8_t  s_plot[3][2];                            // reserved flat 3x3 plots (x, 255 = none)
static uint8_t  s_cool[N_COUNT], s_mend_cool, s_acc_cool, s_stuck;
static uint32_t s_spent;
static uint16_t s_casts, s_ponds, s_plots, s_levels;
static uint8_t  s_wx, s_wy, s_has_op;
static int8_t   s_wd, s_ld;
static uint8_t  s_lx, s_ly, s_skip[NFAIL][2], s_skipn;       // the last ramp tile he cut, and tiles that could not be mended (they tug both ways)

static int hh(int x, int y) { return MT_H(g_map[y * WM + x]); }
static int iab(int v) { return v < 0 ? -v : v; }

void shaman_reset(void) {
  P.kind = PK_NONE; s_waiting = s_ramping = s_has_op = 0; s_failn = 0; s_mend_cool = 0;
  for (int i = 0; i < NFAIL; i++) s_fail[i][0] = s_fail[i][1] = 255;
  s_pneed = 0; s_aborts = 0; s_turned = 0;
  for (int i = 0; i < 3; i++) s_plot[i][0] = s_plot[i][1] = 255;
  for (int i = 0; i < N_COUNT; i++) s_cool[i] = 0;
  s_acc_cool = 0; s_stuck = 0; s_spent = 0; s_casts = 0; s_ponds = s_plots = s_levels = 0; s_ld = 0; s_lx = s_ly = 255; s_skipn = 0;
  for (int i = 0; i < NFAIL; i++) s_skip[i][0] = s_skip[i][1] = 255;
  uint32_t h = G.s.seed * 2654435761u;
  h ^= h >> 15;
  s_pers = (uint8_t)((h >> 3) & 3);
}

int shaman_waiting(void) { return s_waiting; }
int shaman_pers(void) { return s_pers; }
int shaman_cast_dir(void) { return s_wd; }
int shaman_stuck(void) { int v = s_stuck; s_stuck = 0; return v; }

static int count_ramps(void) { int n = 0; for (int y = 0; y < WM; y++) for (int x = 0; x < WM; x++) if (world_ramp_used(x, y)) n++; return n; }

// ------------------------------------------------------------------ small queries
static int in_rect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }
static int in_plot(int x, int y) {
  for (int i = 0; i < 3; i++) if (s_plot[i][0] != 255 && in_rect(x, y, s_plot[i][0], s_plot[i][1], 3, 3)) return 1;
  return 0;
}
static int in_proj(int x, int y) { return P.kind != PK_NONE && in_rect(x, y, P.x, P.y, P.w, P.h); }
static int built_at(int x, int y) {
  for (int b = 0; b < MAXB; b++) {
    if (!bused(b)) continue;
    int w, h;
    bdims(b, &w, &h);
    if (in_rect(x, y, B[b].x, B[b].y, w, h)) return b;
  }
  return -1;
}
static int modifiable(int x, int y) {
  if (x < 1 || y < 1 || x >= WM - 1 || y >= WM - 1) return 0;
  uint8_t b = g_map[y * WM + x];
  int h = MT_H(b);
  return h >= 2 && h <= 6 && MT_SOIL(b) != SOIL_BUILT && !in_proj(x, y) && !in_plot(x, y);
}
// faith for one layer step between h and to: the higher layer's number (cost rises with the layer)
static int step_cost(int h, int to) { return h > to ? h : to; }
static int lev(int h, int to) {
  int c = 0;
  if (to > h) for (int l = h + 1; l <= to; l++) c += l; else for (int l = to + 1; l <= h; l++) c += l;
  return c;
}
static int tier(void) { int p = pop(); return p >= 24 ? 3 : (p >= 14 ? 2 : (p >= 6 ? 1 : 0)); }
static int bcount(int tp, int sites) {          // finished (and, with sites, laid out) buildings of a type
  int n = 0;
  for (int b = 0; b < MAXB; b++) if (bused(b) && btype(b) == tp && (sites || !(B[b].tp & SITE))) n++;
  return n;
}
static int beds_planned(void) {
  int n = 0;
  for (int b = 0; b < MAXB; b++) if (bused(b)) n += cap_of(btype(b));
  return n;
}
static int near_coast(void) {
  for (int y = 0; y < WM; y++) for (int x = 0; x < WM; x++) if (hh(x, y) <= 1 && iab(x - cx()) + iab(y - cy()) <= 16) return 1;
  return 0;
}
// 1 while a flattened 3x3 plot is waiting for the next home
static int plot_ready(void) { for (int i = 0; i < 3; i++) if (s_plot[i][0] != 255) return 1; return 0; }
// anchors next to a spot where a project failed are no better: the planner would pick the neighbour and fail the same way
static int bad_spot(int x, int y) {
  for (int i = 0; i < NFAIL; i++) if (s_fail[i][0] != 255 && iab(s_fail[i][0] - x) <= 2 && iab(s_fail[i][1] - y) <= 2) return 1;
  return 0;
}
static void add_fail(int x, int y) { s_fail[s_failn][0] = (uint8_t)x; s_fail[s_failn][1] = (uint8_t)y; s_failn = (uint8_t)((s_failn + 1) % NFAIL); }
// a failed project puts the need that started it on cooldown, so the shaman does something else for a while
static void proj_failed(int x, int y) { add_fail(x, y); s_cool[s_pneed] = 6; if (s_aborts < 65535) s_aborts++; }

// ------------------------------------------------------------------ site search
struct Cand { uint8_t x, y, var, H; int16_t cost, faith; };
enum { NC = 14, NOSITE = -30000 };

static int nvars(int k) { return k == SP_POND ? 2 : (k == SP_PLOT ? 1 : (k == SP_LEVEL ? 3 : (k == SP_STAGE ? 1 : NVAR[k]))); }
static void kdim(int k, int var, int* w, int* h) {
  if (k == SP_POND) { *w = var ? 3 : 2; *h = var ? 2 : 3; }
  else if (k == SP_PLOT) { *w = *h = 3; }
  else if (k == SP_LEVEL) { *w = var == 1 ? 5 : (var == 2 ? 3 : 4); *h = var == 2 ? 5 : (var == 1 ? 3 : 4); }
  else bdim(k, var, w, h);
}
static uint32_t hash2(int x, int y) { uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u; h ^= h >> 13; h *= 0x5bd1e995u; return h ^ (h >> 15); }

// Cost of building kind k, variant var with its top left corner at (ax, ay); NOSITE if the spot is not possible. Needs g_dist
// from the village green. *Hout is the layer the plot is levelled to, *faith the faith it takes (including ramp estimates).
static int eval_site(int k, int var, int ax, int ay, int nolevel, int* Hout, int* faith_out) {
  int w, h;
  kdim(k, var, &w, &h);
  if (ax < 1 || ay < 1 || ax + w > WM - 1 || ay + h > WM - 1 || bad_spot(ax, ay)) return NOSITE;
  if (in_rect(cx(), cy(), ax - 1, ay - 1, w + 2, h + 2) || (P.kind != PK_NONE && ax < P.x + P.w + 1 && ax + w > P.x - 1 && ay < P.y + P.h + 1 && ay + h > P.y - 1)) return NOSITE;
  int home = k < B_COUNT && is_home(k);
  int cost = 0, faith = 0, dist = 255;
  // ---- the fishing hut: land beside water, the pier tile over the water
  if (k == B_FISH) {
    int land_i = (var & 2) ? 1 : 0, lx = ax + ((var & 1) ? 0 : land_i), ly = ay + ((var & 1) ? land_i : 0);
    int wx = ax + ((var & 1) ? 0 : 1 - land_i), wy = ay + ((var & 1) ? 1 - land_i : 0);
    uint8_t lb = g_map[ly * WM + lx], wb = g_map[wy * WM + wx];
    if (MT_H(lb) < 2 || MT_H(lb) > 3 || MT_H(wb) > 1 || MT_SOIL(lb) == SOIL_BUILT || MT_SOIL(wb) == SOIL_BUILT || in_plot(lx, ly)) return NOSITE;
    static const int8_t D[4][2] = {{0, 1}, {0, -1}, {-1, 0}, {1, 0}};
    for (int d = 0; d < 4; d++) {
      int nx = lx + D[d][0], ny = ly + D[d][1];
      if (nx >= 0 && ny >= 0 && nx < WM && ny < WM && g_dist[ny * WM + nx] < dist) dist = g_dist[ny * WM + nx];
    }
    if (dist == 255) return NOSITE;
    for (int b = 0; b < MAXB; b++) if (bused(b) && btype(b) == B_FISH && iab(B[b].x - ax) + iab(B[b].y - ay) < 3) return NOSITE;
    if (MT_OBJ(lb)) faith++;
    *Hout = 255; *faith_out = faith;
    return faith + dist + (int)(hash2(ax, ay) & 1);
  }
  int hmin = 9, hmax = 0, objs = 0, plotn = 0;
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) {
      int x = ax + i, y = ay + j;
      uint8_t b = g_map[y * WM + x];
      int t = MT_H(b);
      if (t < 2 || t > 6) return NOSITE;
      if (MT_SOIL(b) == SOIL_BUILT) {
        int ob = built_at(x, y);
        if (ob < 0 || k >= B_COUNT || !home || (B[ob].tp & SITE) || !is_home(btype(ob)) || btype(ob) >= k) return NOSITE;
      } else if (MT_OBJ(b)) objs++;
      if (in_plot(x, y)) plotn++;
      if (t < hmin) hmin = t;
      if (t > hmax) hmax = t;
    }
  // buildings that would be replaced (upgrade) and keep-clear shores of fishing huts
  int repl_cap = 0, repl_tiles = 0;
  for (int b = 0; b < MAXB; b++) {
    if (!bused(b)) continue;
    int bw_, bh_;
    bdims(b, &bw_, &bh_);
    int ox = B[b].x, oy = B[b].y;
    if (btype(b) == B_FISH) {
      if (k != B_FISH && ax < ox + bw_ + 1 && ax + w > ox - 1 && ay < oy + bh_ + 1 && ay + h > oy - 1) return NOSITE;
    } else if (ax < ox + bw_ && ax + w > ox && ay < oy + bh_ && ay + h > oy) {
      repl_cap += cap_of(btype(b)); repl_tiles += bw_ * bh_;
    }
  }
  if (repl_tiles && (!home || cap_of(k) <= repl_cap + 1)) return NOSITE;
  // the target layer: the cheapest of 3..5 (a pond goes down to 1)
  int H, best = 1 << 20;
  if (k == SP_POND) { H = 1; best = 0; for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) best += lev(hh(ax + i, ay + j), 1); }
  else {
    H = 3;
    for (int T = 3; T <= 5; T++) {
      int c = 0;
      for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) c += lev(hh(ax + i, ay + j), T);
      if (c < best) { best = c; H = T; }
    }
  }
  if ((k == SP_PLOT || k == SP_LEVEL) && plotn) return NOSITE;
  if (k == SP_LEVEL) {                                         // wide land ahead of the town: only where something really changes
    int ch = objs;
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) if (hh(ax + i, ay + j) != H) ch++;
    if (ch < 3) return NOSITE;
  }
  if (nolevel && (best > 0 || objs)) return NOSITE;
  faith = best + objs;
  // every tile that has to change needs a neighbour he can stand on now or can make standable: not an island of rock and water
  if (k != SP_POND && !nolevel)
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) {
        int x = ax + i, y = ay + j;
        if (!MT_OBJ(g_map[y * WM + x]) && hh(x, y) == H) continue;
        static const int8_t D[4][2] = {{0, 1}, {0, -1}, {-1, 0}, {1, 0}};
        int ok = 0;
        for (int d = 0; d < 4 && !ok; d++) {
          int nx = x + D[d][0], ny = y + D[d][1];
          if (nx < 0 || ny < 0 || nx >= WM || ny >= WM) continue;
          ok = g_dist[ny * WM + nx] != 255 || in_rect(nx, ny, ax, ay, w, h) || modifiable(nx, ny);
        }
        if (!ok) return NOSITE;
      }
  // ---- the ring around the plot
  int ring = 0;
  for (int y = ay - 1; y <= ay + h; y++)
    for (int x = ax - 1; x <= ax + w; x++) {
      if (x < 0 || y < 0 || x >= WM || y >= WM || in_rect(x, y, ax, ay, w, h)) continue;
      uint8_t b = g_map[y * WM + x];
      int t = MT_H(b);
      if (k == SP_POND) {
        if (MT_SOIL(b) == SOIL_BUILT) return NOSITE;
        if (t > 2) ring += step_cost(t, 2) * (t - 2);
      } else {
        if (t <= 1) return NOSITE;                                  // never next to water: the lowest land stays free
        if (MT_SOIL(b) == SOIL_BUILT) { if (iab(t - H) > 1) return NOSITE; }
        else if (t - H > 1 || H - t > 1) ring += H * (iab(t - H) - 1);
      }
      if (x == ax - 1 || x == ax + w || y == ay - 1 || y == ay + h) {
        if ((x == ax - 1 || x == ax + w) && (y == ay - 1 || y == ay + h)) continue;    // corners are not steps
        if (g_dist[y * WM + x] + 1 < dist) dist = g_dist[y * WM + x] + 1;
      }
    }
  if (dist >= 250 || (k == SP_POND && (dist < 2 || dist > 12)) || (k == SP_LEVEL && dist > 15)) return NOSITE;
  if (nolevel && ring) return NOSITE;
  faith += ring;
  cost = faith + dist * 2 + plotn * (home || k == SP_PLOT ? -3 : 12) + repl_tiles * 5 + (int)(hash2(ax, ay) & 1);
  if (k == B_LUMBER) {
    int trees = 0;
    for (int y = ay - 3; y < ay + h + 3; y++) for (int x = ax - 3; x < ax + w + 3; x++) if (x >= 0 && y >= 0 && x < WM && y < WM && MT_OBJ(g_map[y * WM + x]) == OB_TREE) trees++;
    cost -= trees < 8 ? trees : 8;
  }
  if (k == SP_LEVEL) {                                         // he opens land towards forests and good soil
    int trees = 0, rich = 0;
    for (int y = ay - 2; y < ay + h + 2; y++) for (int x = ax - 2; x < ax + w + 2; x++) if (x >= 0 && y >= 0 && x < WM && y < WM) { trees += MT_OBJ(g_map[y * WM + x]) == OB_TREE; rich += MT_SOIL(g_map[y * WM + x]) == SOIL_FERTILE; }
    cost -= (trees < 6 ? trees : 6) + (rich < 4 ? rich : 4);
  }
  if (k == B_FIELD)
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) if (MT_SOIL(g_map[(ay + j) * WM + ax + i]) == SOIL_FERTILE) cost -= 1;
  if (k == B_PEN || k == B_ORCHARD || k == B_FIELD) cost -= 0;
  if (home || k == SP_PLOT) {                                   // homes cluster
    static const int8_t D[4][2] = {{0, 1}, {0, -1}, {-1, 0}, {1, 0}};
    for (int d = 0; d < 4; d++) {
      int nx = ax + (D[d][0] > 0 ? w : D[d][0]), ny = ay + (D[d][1] > 0 ? h : D[d][1]);
      if (nx >= 0 && ny >= 0 && nx < WM && ny < WM && MT_SOIL(g_map[ny * WM + nx]) == SOIL_BUILT) { int ob = built_at(nx, ny); if (ob >= 0 && is_home(btype(ob))) { cost -= 2; break; } }
    }
  }
  *Hout = H; *faith_out = faith;
  return cost;
}

// Unmovable tiles near the plot (water, rock, buildings, reserved plots) must be able to meet the plot's layer with
// steps of one layer per tile, otherwise no set of ramps can ever make it walkable.
static int feasible(int x, int y, int w, int h, int H) {
  for (int ty = y - 6; ty < y + h + 6; ty++)
    for (int tx = x - 6; tx < x + w + 6; tx++) {
      if (tx < 0 || ty < 0 || tx >= WM || ty >= WM || in_rect(tx, ty, x, y, w, h)) continue;
      int dx = tx < x ? x - tx : (tx >= x + w ? tx - (x + w - 1) : 0), dy = ty < y ? y - ty : (ty >= y + h ? ty - (y + h - 1) : 0);
      if (dx + dy > 6) continue;
      uint8_t b = g_map[ty * WM + tx];
      int t = MT_H(b);
      if (t >= 2 && t <= 6 && MT_SOIL(b) != SOIL_BUILT && !in_plot(tx, ty)) continue;     // a tile he can move
      if (iab(t - H) > dx + dy) return 0;
    }
  return 1;
}

static int count_reach(void) { int n = 0; for (int i = 0; i < WM * WM; i++) if (g_dist[i] != 255) n++; return n; }

// 1 if a walkable tile of the flow beside the rectangle (corners do not count) is reachable
static int ring_access(int x, int y, int w, int h) {
  for (int j = -1; j <= h; j++)
    for (int i = -1; i <= w; i++) {
      if ((i == -1 || i == w) && (j == -1 || j == h)) continue;
      if (i >= 0 && i < w && j >= 0 && j < h) continue;
      int tx = x + i, ty = y + j;
      if (tx >= 0 && ty >= 0 && tx < WM && ty < WM && g_dist[ty * WM + tx] != 255) return 1;
    }
  return 0;
}

static int ac_goal(int mode, int x, int y, int rx, int ry, int rw, int rh);
// bit 0: a shore can be reached on the flow in g_dist, bit 1: a tree can
static int goals_reached(void) {
  int r = 0;
  for (int y = 0; y < WM && r != 3; y++)
    for (int x = 0; x < WM; x++)
      if (g_dist[y * WM + x] != 255) { if (ac_goal(AC_SHORE, x, y, 0, 0, 0, 0)) r |= 1; if (ac_goal(AC_TREE, x, y, 0, 0, 0, 0)) r |= 2; }
  return r;
}

// Is the village still in one piece with the footprint blocked, and does every building (and the new one) keep a way in?
// (g_dist must hold the flow from the green.)
static int keeps_connected(int ax, int ay, int w, int h, int fish) {
  (void)fish;
  int base = count_reach(), fr = 0, shx, shy, sh_had, rsh_had;
  uint32_t had[2] = {0, 0};
  world_shaman_tile(&shx, &shy);
  sh_had = g_dist[shy * WM + shx] != 255;                                  // g_dist holds the planning flow from the caller
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) if (g_dist[(ay + j) * WM + ax + i] != 255) fr++;
  world_flow(cx(), cy());                                                  // what people can walk to today (over the ramps there are)
  int rgoals = goals_reached();
  rsh_had = g_dist[shy * WM + shx] != 255;
  uint32_t vhad[2] = {0, 0};                                               // the villagers who can walk to the green today keep that way
  for (int i = 0; i < MAXV; i++) if (used(i) && g_dist[V[i].y * WM + V[i].x] != 255) vhad[i >> 5] |= 1u << (i & 31);
  for (int b = 0; b < MAXB; b++) if (bused(b)) { int bw_, bh_; bdims(b, &bw_, &bh_); if (ring_access(B[b].x, B[b].y, bw_, bh_)) had[b >> 5] |= 1u << (b & 31); }
  uint8_t keep[9], keep_r[9];
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) { keep[j * 3 + i] = g_map[(ay + j) * WM + ax + i]; keep_r[j * 3 + i] = (uint8_t)world_ramp(ax + i, ay + j); world_set_built(ax + i, ay + j, 1); }
  world_flow_plan(cx(), cy());
  int sh_in = shx >= ax && shx < ax + w && shy >= ay && shy < ay + h;       // he is not walled in by a plot he stands on (he steps off it)
  int ok = count_reach() >= base - fr - 2 && ring_access(ax, ay, w, h) && (!sh_had || sh_in || g_dist[shy * WM + shx] != 255);      // the shaman is not walled in
  if (ok) {                                                                // and nobody who can get somewhere today loses the way
    world_flow(cx(), cy());
    ok = !rsh_had || sh_in || g_dist[shy * WM + shx] != 255;                       // (land that only loses its way holds no building: the shaman re-cuts ramps)
    for (int i = 0; i < MAXV && ok; i++) if (((vhad[i >> 5] >> (i & 31)) & 1) && g_dist[V[i].y * WM + V[i].x] == 255 && !(V[i].x >= ax && V[i].x < ax + w && V[i].y >= ay && V[i].y < ay + h)) ok = 0;
    if (ok) { int g = goals_reached(); if (count_done(B_WELL)) rgoals &= ~1; if (rgoals & ~g) ok = 0; }          // the shore (without a well) and the trees stay reachable
    for (int b = 0; b < MAXB && ok; b++)
      if (bused(b) && (had[b >> 5] & (1u << (b & 31)))) {
        int bw_, bh_;
        bdims(b, &bw_, &bh_);
        if (!(ax < B[b].x + bw_ && ax + w > B[b].x && ay < B[b].y + bh_ && ay + h > B[b].y) && !ring_access(B[b].x, B[b].y, bw_, bh_)) ok = 0;
      }
  }
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) { g_map[(ay + j) * WM + ax + i] = keep[j * 3 + i]; world_set_ramp(ax + i, ay + j, keep_r[j * 3 + i]); }
  return ok;
}

// The best possible spot for kind k. Returns 1 and fills *out, or 0.
static int pick_site(int k, Cand* out, int nolevel) {
  Cand c[NC];
  int n = 0;
  world_flow_plan(cx(), cy());
  for (int var = 0; var < nvars(k); var++) {
    int w, h;
    kdim(k, var, &w, &h);
    for (int ay = 1; ay + h <= WM - 1; ay++)
      for (int ax = 1; ax + w <= WM - 1; ax++) {
        int H = 0, faith = 0;
        int cost = eval_site(k, var, ax, ay, nolevel, &H, &faith);
        if (cost == NOSITE) continue;
        if (n == NC && cost >= c[NC - 1].cost) continue;
        if (k != B_FISH && !feasible(ax, ay, w, h, H)) continue;
        int pos = n < NC ? n++ : NC - 1;
        while (pos > 0 && c[pos - 1].cost > cost) { c[pos] = c[pos - 1]; pos--; }
        c[pos].x = (uint8_t)ax; c[pos].y = (uint8_t)ay; c[pos].var = (uint8_t)var; c[pos].H = (uint8_t)H; c[pos].cost = (int16_t)cost; c[pos].faith = (int16_t)faith;
      }
  }
  for (int i = 0; i < n; i++) {
    int w, h;
    kdim(k, c[i].var, &w, &h);
    world_flow_plan(cx(), cy());
    if (k != SP_LEVEL && !keeps_connected(c[i].x, c[i].y, w, h, k == B_FISH)) continue;
    *out = c[i];
    return 1;
  }
  return 0;
}

// ------------------------------------------------------------------ laying out and demolishing
static void demolish(int b) {
  int w, h;
  bdims(b, &w, &h);
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) world_set_built(B[b].x + i, B[b].y + j, 0);
  mark_tile(B[b].x, B[b].y);
  B[b].tp = 0xFF;
}

static int lay_site(int tp, int var, int x, int y, int site) {
  int slot = -1, w, h;
  for (int b = 0; b < MAXB; b++) if (!bused(b)) { slot = b; break; }
  if (slot < 0) return -1;
  bdim(tp, var, &w, &h);
  for (int b = 0; b < MAXB; b++) {              // upgrade: smaller homes underneath make room
    if (!bused(b)) continue;
    int bw_, bh_;
    bdims(b, &bw_, &bh_);
    if (x < B[b].x + bw_ && x + w > B[b].x && y < B[b].y + bh_ && y + h > B[b].y) demolish(b);
  }
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) world_set_built(x + i, y + j, 1);
  B[slot].x = (uint8_t)x; B[slot].y = (uint8_t)y; B[slot].tp = (uint8_t)(tp | (var << 4) | (site ? SITE : 0)); B[slot].st = 0;
  if (!site && tp == B_PEN) B[slot].st = 2;
  for (int i = 0; i < 3; i++) if (s_plot[i][0] != 255 && x < s_plot[i][0] + 3 && x + w > s_plot[i][0] && y < s_plot[i][1] + 3 && y + h > s_plot[i][1]) s_plot[i][0] = s_plot[i][1] = 255;
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) mark_tile(x + i, y + j);
  s_roles_dirty = 1;
  rebuild_homes();
  return slot;
}

// the first tent of a new village stands at once, on the best spot that needs no levelling
static void place_start_tent(void) {
  Cand c;
  if (pick_site(B_TENT, &c, 1) || pick_site(B_TENT, &c, 0)) lay_site(B_TENT, 0, c.x, c.y, 0);
}

// ------------------------------------------------------------------ the executor
static int skipped(int x, int y) { for (int q = 0; q < NFAIL; q++) if (s_skip[q][0] == x && s_skip[q][1] == y) return 1; return 0; }
static void add_skip(int x, int y) { s_skip[s_skipn][0] = (uint8_t)x; s_skip[s_skipn][1] = (uint8_t)y; s_skipn = (uint8_t)((s_skipn + 1) % NFAIL); }
// 1 if the shaman can stand beside (x, y): needs g_dist from his own tile (world_flow)
static int op_reachable(int x, int y) {
  static const int8_t D[4][2] = {{0, 1}, {0, -1}, {-1, 0}, {1, 0}};
  for (int d = 0; d < 4; d++) {
    int nx = x + D[d][0], ny = y + D[d][1];
    if (nx >= 0 && ny >= 0 && nx < WM && ny < WM && g_dist[ny * WM + nx] != 255) return 1;
  }
  return 0;
}

// Would changing this tile cut the village in two or shut a building in? (g_dist is used)
static int op_breaks(int x, int y, int d) {
  if (d == 0) return 0;
  world_flow(cx(), cy());
  int shx, shy, goals = goals_reached(), sh_had;
  world_shaman_tile(&shx, &shy);
  sh_had = g_dist[shy * WM + shx] != 255;
  uint32_t had[2] = {0, 0}, vhad[2] = {0, 0};                  // what can be reached today must stay reachable: buildings, villagers, the shaman, the shore and the trees
  for (int b = 0; b < MAXB; b++) if (bused(b)) { int bw_, bh_; bdims(b, &bw_, &bh_); if (ring_access(B[b].x, B[b].y, bw_, bh_)) had[b >> 5] |= 1u << (b & 31); }
  for (int i = 0; i < MAXV; i++) if (used(i) && g_dist[V[i].y * WM + V[i].x] != 255) vhad[i >> 5] |= 1u << (i & 31);
  if (count_done(B_WELL)) goals &= ~1;
  uint8_t keep = g_map[y * WM + x];
  world_set_height(x, y, MT_H(keep) + d);
  world_flow(cx(), cy());
  int broke = (goals & ~goals_reached());
  for (int i = 0; i < MAXV && !broke; i++)
    if (((vhad[i >> 5] >> (i & 31)) & 1) && g_dist[V[i].y * WM + V[i].x] == 255) broke = 1;
  for (int b = 0; b < MAXB && !broke; b++)
    if (bused(b) && (had[b >> 5] & (1u << (b & 31)))) { int bw_, bh_; bdims(b, &bw_, &bh_); if (!ring_access(B[b].x, B[b].y, bw_, bh_)) broke = 1; }
  if (!broke && sh_had) { world_flow_plan(cx(), cy()); broke = g_dist[shy * WM + shx] == 255; }      // he can always have a ramp cut for him (the planning flow)
  g_map[y * WM + x] = keep;
  return broke;
}

static int project_occupied(void) {
  for (int j = 0; j < P.h; j++) for (int i = 0; i < P.w; i++) if (villager_at(P.x + i, P.y + j)) return 1;
  return 0;
}

static int trees_low(void);
static int next_terra(int sx, int sy, int* tx, int* ty, int* dir) {
  int bd = 9999, last_trees = trees_low();       // the last trees of the forest stay, for the wood
  for (int j = 0; j < P.h; j++)
    for (int i = 0; i < P.w; i++) {
      int x = P.x + i, y = P.y + j;
      uint8_t b = g_map[y * WM + x];
      int t = MT_H(b), d = -2;
      if (skipped(x, y)) continue;
      if (MT_OBJ(b) && t >= 2) { if (last_trees && MT_OBJ(b) == OB_TREE) continue; d = 0; }
      else if (P.hgt != 255 && t != P.hgt) d = t < P.hgt ? 1 : -1;
      if (d == -2 || !op_reachable(x, y)) continue;
      if (d < 0 && t <= 3 && villager_at(x, y)) continue;         // r7: not under somebody's feet (the pond would take his ground)
      int dist = iab(x - sx) + iab(y - sy);
      if (dist < bd) { bd = dist; *tx = x; *ty = y; *dir = d; }
    }
  return bd != 9999;
}

static int next_ramp(int sx, int sy, int* tx, int* ty, int* dir) {
  int bd = 9999, pcx = P.kind != PK_NONE ? P.x + P.w / 2 : cx(), pcy = P.kind != PK_NONE ? P.y + P.h / 2 : cy();
  for (int y = 0; y < WM; y++)
    for (int x = 0; x < WM; x++)
      for (int k = 0; k < 2; k++) {
        int nx = x + (k == 0), ny = y + (k == 1);
        if (nx >= WM || ny >= WM) continue;
        int a = hh(x, y), b = hh(nx, ny);
        if (iab(a - b) <= 1) continue;
        int ma = modifiable(x, y), mb = modifiable(nx, ny), mx, my, ox, oy;
        if (ma && mb) {
          if (iab(x - pcx) + iab(y - pcy) >= iab(nx - pcx) + iab(ny - pcy)) { mx = x; my = y; ox = nx; oy = ny; } else { mx = nx; my = ny; ox = x; oy = y; }
        } else if (ma) { mx = x; my = y; ox = nx; oy = ny; }
        else if (mb) { mx = nx; my = ny; ox = x; oy = y; }
        else continue;
        int d = hh(mx, my) < hh(ox, oy) ? 1 : -1;
        if (d < 0 && hh(mx, my) <= 2) continue;               // never flood a beach
        int skip = 0;
        if (skipped(mx, my) || !op_reachable(mx, my)) skip = 1;
        if (skip) continue;
        if (mx == s_lx && my == s_ly && d == -s_ld) {         // it would undo the last cut: this tile is pulled both ways
          add_skip(mx, my);
          continue;
        }
        int dist = iab(mx - sx) + iab(my - sy);
        if (dist < bd) { bd = dist; *tx = mx; *ty = my; *dir = d; }
      }
  return bd != 9999;
}

// ------------------------------------------------------------------ ramps (r7)
// People cross a step of one layer only over a ramp tile. The village needs one where a place it wants to reach (a building, the shore,
// a tree) cannot be reached over the ramps that exist: the cheapest set of steps is found with the planning flow (a step without a ramp
// costs RAMP_PRICE) started from everything the green can reach, and the step nearest to the green is the next ramp to cut.
static int ac_goal(int mode, int x, int y, int rx, int ry, int rw, int rh) {        // is walkable tile (x, y) a place that serves the goal?
  if (mode == AC_SHORE) return water_at(x - 1, y) || water_at(x + 1, y) || water_at(x, y - 1) || water_at(x, y + 1);
  if (mode == AC_TREE) return tree_at(x - 1, y) || tree_at(x + 1, y) || tree_at(x, y - 1) || tree_at(x, y + 1);
  if (mode == AC_TILE) return x == rx && y == ry;
  int i = x - rx, j = y - ry;
  if (i >= 0 && i < rw && j >= 0 && j < rh) return 0;
  if (i >= -1 && i <= rw && j >= 0 && j < rh) return 1;
  return j >= -1 && j <= rh && i >= 0 && i < rw;
}
static int ramp_tile_ok(int x, int y) { return world_walkable(x, y) && !in_plot(x, y) && !in_proj(x, y); }
// 1: *rx, *ry is the next tile to turn into a ramp; 0: nothing to do (the goal is reachable, or there is no way at all, or the shaman cannot
// get to any of the steps: stand = 1 asks that he can stand beside the ramp from where he is)
static int find_ramp(int mode, int gx, int gy, int gw, int gh, int stand, int* rx, int* ry) {
  world_flow(cx(), cy());
  int x, y, bx = -1, by = -1, bd = 255;
  for (y = 0; y < WM; y++) for (x = 0; x < WM; x++) if (g_dist[y * WM + x] != 255 && ac_goal(mode, x, y, gx, gy, gw, gh)) return 0;      // already reachable
  world_flow_reach_to_goal();
  world_flow_run_plan();
  for (y = 0; y < WM; y++)
    for (x = 0; x < WM; x++)
      if (g_dist[y * WM + x] < bd && ac_goal(mode, x, y, gx, gy, gw, gh) && world_walkable(x, y)) { bd = g_dist[y * WM + x]; bx = x; by = y; }
  if (bx < 0) return 0;
  static const int8_t D[4][2] = {{0, 1}, {0, -1}, {-1, 0}, {1, 0}};
  uint8_t cand[8][4];                                                // the steps on the way, the one nearest to the green last: higher tile, lower tile
  int nc = 0;
  for (int guard = 0; guard < 300 && g_dist[by * WM + bx] != 0; guard++) {
    int d = g_dist[by * WM + bx], moved = 0;
    for (int k = 0; k < 4 && !moved; k++) {
      int nx = bx + D[k][0], ny = by + D[k][1];
      if (nx < 0 || ny < 0 || nx >= WM || ny >= WM || !world_walkable(nx, ny)) continue;
      int dh = hh(bx, by) - hh(nx, ny), c;
      if (dh == 0 || ((dh == 1 || dh == -1) && (world_ramp(bx, by) || world_ramp(nx, ny)))) c = 1;
      else if (dh == 1 || dh == -1) c = RAMP_PRICE;
      else continue;
      if (g_dist[ny * WM + nx] + c != d) continue;
      if (c != 1 && nc < 8) {                                         // the higher tile of the pair carries the ramp (it covers the cliff face), else the lower
        cand[nc][0] = (uint8_t)(dh > 0 ? bx : nx); cand[nc][1] = (uint8_t)(dh > 0 ? by : ny);
        cand[nc][2] = (uint8_t)(dh > 0 ? nx : bx); cand[nc][3] = (uint8_t)(dh > 0 ? ny : by);
        nc++;
      }
      bx = nx; by = ny; moved = 1;
    }
    if (!moved) return 0;
  }
  if (!nc) return 0;
  int sx = -1, sy = -1;
  if (stand) { world_shaman_tile(&sx, &sy); world_flow(sx, sy); }
  for (int k = nc - 1; k >= 0; k--)
    for (int t = 0; t < 2; t++) {
      int tx = cand[k][t * 2], ty = cand[k][t * 2 + 1];
      if (!ramp_tile_ok(tx, ty) || (stand && (skipped(tx, ty) || !op_reachable(tx, ty)))) continue;
      *rx = tx; *ry = ty;
      return 1;
    }
  return 0;
}

// the next ramp of the project (the shaman must be able to stand beside the tiles he works on, and a building needs a way in)
static int next_access(int sx, int sy, int* tx, int* ty) {
  (void)sx; (void)sy;
  int rx, ry;
  if (P.kind == PK_ACCESS) { if (!find_ramp(P.var, P.x, P.y, P.w, P.h, 1, &rx, &ry)) return 0; }
  else if (P.kind == PK_BUILD || P.kind == PK_POND || P.kind == PK_PLOT || P.kind == PK_LEVEL) { if (!find_ramp(AC_BUILDING, P.x, P.y, P.w, P.h, 1, &rx, &ry)) return 0; }      // a place to stand beside every tile of it
  else return 0;
  *tx = rx; *ty = ry;
  return 1;
}

// Something the village cannot get to over the ramps it has: the shaman himself (shut in on a terrace), a building, the shore (when there
// is no well), a tree (when wood runs low). Starts a project of ramps for the first of them; 0 when everything is reachable or nothing
// can be done about it.
// 1 and the tile of an idle villager who cannot walk to the green over the ramps there are (g_dist must hold the flow from the green)
static int stranded_villager(int* x, int* y) {
  for (int i = 0; i < MAXV; i++)
    if (used(i) && vact(i) == A_IDLE && g_dist[V[i].y * WM + V[i].x] == 255 && world_walkable(V[i].x, V[i].y)) { *x = V[i].x; *y = V[i].y; return 1; }
  return 0;
}

static int access_project(void) {
  int rx, ry, mode = -1, gx = 0, gy = 0, gw = 1, gh = 1, shore_ok = 0, tree_ok = 0, bad = -1, shx, shy, self_ok;
  world_shaman_tile(&shx, &shy);
  world_flow(cx(), cy());                                              // one flow answers all the questions
  self_ok = g_dist[shy * WM + shx] != 255;
  for (int b = 0; b < MAXB && bad < 0; b++) {
    if (!bused(b)) continue;
    int bw_, bh_;
    bdims(b, &bw_, &bh_);
    if (!ring_access(B[b].x, B[b].y, bw_, bh_)) bad = b;
  }
  for (int y = 0; y < WM; y++)
    for (int x = 0; x < WM; x++)
      if (g_dist[y * WM + x] != 255) { shore_ok |= ac_goal(AC_SHORE, x, y, 0, 0, 0, 0); tree_ok |= ac_goal(AC_TREE, x, y, 0, 0, 0, 0); }
  if (!self_ok) {
    if (find_ramp(AC_TILE, shx, shy, 1, 1, 1, &rx, &ry)) { mode = AC_TILE; gx = shx; gy = shy; }
    else s_stuck = 1;                                                // shut in by buildings and cliffs: no ramp helps, godus.cpp puts him back on the green
  }
  if (mode < 0) {                                                      // r8: an idle villager cut off from the green: ramps to his pocket
    int sx_ = stranded_villager(&gx, &gy);
    if (sx_ && find_ramp(AC_TILE, gx, gy, 1, 1, 1, &rx, &ry)) { mode = AC_TILE; gw = gh = 1; }
  }
  if (mode < 0 && bad >= 0) {
    int bw_, bh_;
    bdims(bad, &bw_, &bh_);
    gx = B[bad].x; gy = B[bad].y; gw = bw_; gh = bh_;
    if (find_ramp(AC_BUILDING, gx, gy, gw, gh, 1, &rx, &ry)) mode = AC_BUILDING;
  }
  if (mode < 0 && !shore_ok && !count_done(B_WELL) && pop() >= 2 && find_ramp(AC_SHORE, 0, 0, 0, 0, 1, &rx, &ry)) mode = AC_SHORE;
  if (mode < 0 && !tree_ok && s_wood < 60 && find_ramp(AC_TREE, 0, 0, 0, 0, 1, &rx, &ry)) mode = AC_TREE;
  if (mode < 0) return 0;
  P.kind = PK_ACCESS; P.tp = 0; P.var = (uint8_t)mode; P.x = (uint8_t)gx; P.y = (uint8_t)gy; P.w = (uint8_t)gw; P.h = (uint8_t)gh; P.hgt = 255; P.ops = 0; P.wait = 0;
  s_has_op = 0; s_ramping = 0;
  for (int i = 0; i < NFAIL; i++) s_skip[i][0] = s_skip[i][1] = 255;
  return 1;
}

// Free of charge (a new village, a loaded game): ramps until every building can be reached and there is a shore and a tree to go to
static void access_free(void) {
  for (int guard = 0; guard < 200; guard++) {
    int rx = 0, ry = 0, found = 0;
    for (int b = 0; b < MAXB && !found; b++) {
      if (!bused(b)) continue;
      int bw_, bh_;
      bdims(b, &bw_, &bh_);
      found = find_ramp(AC_BUILDING, B[b].x, B[b].y, bw_, bh_, 0, &rx, &ry);
    }
    if (!found && !count_done(B_WELL)) found = find_ramp(AC_SHORE, 0, 0, 0, 0, 0, &rx, &ry);
    if (!found) found = find_ramp(AC_TREE, 0, 0, 0, 0, 0, &rx, &ry);
    if (!found) break;
    world_set_ramp(rx, ry, 1);
  }
  quarts_rebuild(0, 0, WM - 1, WM - 1);
}

// A new village on land from which no shore can be walked to (cliffs of two layers or more all round): a well, finished, beside the green
static void start_well(void) {
  Cand c;
  world_flow(cx(), cy());
  if (count_done(B_WELL) || (goals_reached() & 1)) return;
  if (pick_site(B_WELL, &c, 1)) lay_site(B_WELL, c.var, c.x, c.y, 0);
}

static void finish_project(void) {
  Proj p = P;
  P.kind = PK_NONE; s_has_op = 0; s_ramping = 0;
  if (p.kind == PK_ACCESS) { s_acc_cool = 4; return; }
  if (p.kind == PK_STAGE) return;
  if (p.kind == PK_PLOT) {
    int slot = 0;
    for (int i = 0; i < 3; i++) if (s_plot[i][0] == 255) { slot = i; break; }
    s_plot[slot][0] = p.x; s_plot[slot][1] = p.y; s_plots++;
    return;
  }
  if (p.kind == PK_LEVEL) { s_levels++; return; }
  if (p.kind == PK_POND) {
    int fish = 0;
    for (int j = 0; j < p.h && fish < 2; j++)
      for (int i = 0; i < p.w && fish < 2; i++)
        if ((i + j) % 2 == 0 && hh(p.x + i, p.y + j) <= 1) { uint8_t& b = g_map[(p.y + j) * WM + p.x + i]; b = (uint8_t)((b & 0x3F) | (OB_BUSH << 6)); fish++; }
    for (int j = 0; j < p.h; j++) for (int i = 0; i < p.w; i++) mark_tile(p.x + i, p.y + j);
    s_ponds++;
    return;
  }
  // a building: check the plot once more, then lay out the site
  int ok = 1, w = p.w, h = p.h;
  if (p.tp != B_FISH) {
    for (int j = 0; j < h && ok; j++)
      for (int i = 0; i < w; i++) {
        uint8_t b = g_map[(p.y + j) * WM + p.x + i];
        int ob = MT_SOIL(b) == SOIL_BUILT ? built_at(p.x + i, p.y + j) : -1;
        if (MT_H(b) != p.hgt || MT_OBJ(b) || (MT_SOIL(b) == SOIL_BUILT && (ob < 0 || !is_home(btype(ob)) || (B[ob].tp & SITE)))) ok = 0;
      }
  }
  if (ok) {
    world_flow_plan(cx(), cy());
    ok = keeps_connected(p.x, p.y, w, h, p.tp == B_FISH);
  }
  if (ok) {                                   // r7: the ramps are cut by now; a building the village cannot walk to is not laid out
    world_flow(cx(), cy());
    ok = ring_access(p.x, p.y, w, h);
  }
  if (ok && lay_site(p.tp, p.var, p.x, p.y, p.tp == B_MONUMENT ? 0 : 1) >= 0) {
    if (p.tp == B_MONUMENT) s_mon_day = (uint16_t)(G.s.day >= MON_GAP_DAYS ? G.s.day - MON_GAP_DAYS : 0);      // the first stage may start at once
    return;
  }
  s_wood = (uint16_t)(s_wood + cost_wood(p.tp));          // he could not build here: the wood is returned
  proj_failed(p.x, p.y);
}

int shaman_target(int sx, int sy, int* tx, int* ty) {
  s_waiting = 0;
  if (P.kind == PK_NONE) return 0;
  for (int guard = 0; guard < 8; guard++) {
    if (s_has_op) { *tx = s_wx; *ty = s_wy; return 1; }
    if (P.kind == PK_STAGE) {                              // the monument: he walks beside it and raises the next stage
      if (P.ops >= 1) { finish_project(); return 0; }
      world_flow(cx(), cy());
      int bd = 9999, mx = 0, my = 0;
      for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++)
          if (op_reachable(P.x + i, P.y + j) && iab(P.x + i - sx) + iab(P.y + j - sy) < bd) { bd = iab(P.x + i - sx) + iab(P.y + j - sy); mx = P.x + i; my = P.y + j; }
      if (bd == 9999) {
#ifdef MONDBG
        fprintf(stderr,"unreach sx %d sy %d mon %d,%d reach %d\n",sx,sy,P.x,P.y,count_reach());
#endif
        shaman_abort(); return 0; }
      if (s_faith < (uint16_t)mon_cost(village_mon_stage() + 1)) { s_waiting = 1; return 0; }
      s_wx = (uint8_t)mx; s_wy = (uint8_t)my; s_wd = 2; s_has_op = 1;
      continue;
    }
    int x = 0, y = 0, d = 0, found = 0;
    world_flow(sx, sy);                                   // which tiles can he stand beside?
    if (P.ops < MAX_OPS && next_terra(sx, sy, &x, &y, &d)) { found = 1; s_ramping = 0; }
    else if (P.ops < MAX_OPS + 70 && P.kind != PK_NONE && P.kind != PK_ACCESS && next_ramp(sx, sy, &x, &y, &d)) { found = 1; s_ramping = 1; }
    else if (P.ops < MAX_OPS + 110 && next_access(sx, sy, &x, &y)) { found = 1; s_ramping = 1; d = 3; }
    if (!found) {
      if ((P.kind == PK_BUILD || P.kind == PK_POND) && P.wait <= WAIT_MAX && project_occupied()) { s_waiting = 2; return 0; }      // r7: a building is laid out when nobody stands on the plot
      finish_project();
      return 0;
    }
    if (d != 0 && d != 3 && op_breaks(x, y, d)) { add_skip(x, y); continue; }
    int cost = d == 0 ? 1 : (d == 3 ? RAMP_COST : step_cost(hh(x, y), hh(x, y) + d));
    if (s_faith < cost) { s_waiting = 1; return 0; }
    s_wx = (uint8_t)x; s_wy = (uint8_t)y; s_wd = (int8_t)d; s_has_op = 1;
  }
  finish_project();                                       // every tile left would cut the village: he is done here
  return 0;
}

void shaman_abort(void) {
  if (P.kind == PK_BUILD) s_wood = (uint16_t)(s_wood + cost_wood(P.tp));
  if (P.kind == PK_ACCESS) s_acc_cool = 8;
  else if (P.kind != PK_NONE) proj_failed(P.x, P.y);
  P.kind = PK_NONE; s_has_op = 0;
}

static void apply_tile(int x, int y, int d, int free_cost) {
  uint8_t b = g_map[y * WM + x];
  int t = MT_H(b), cost = d == 0 ? 1 : (d == 3 ? RAMP_COST : step_cost(t, t + d));
  if (!free_cost) {
    if (s_faith < cost) return;
    s_faith = (uint16_t)(s_faith - cost);
    s_spent += (uint32_t)cost;
    s_casts++;
  }
  if (d == 3) world_set_ramp(x, y, 1);
  else if (d == 0) {
    if (MT_OBJ(b) == OB_TREE) s_wood = (uint16_t)(s_wood > 997 ? 999 : s_wood + 2);
    world_clear_object(x, y);
  } else {
    world_set_height(x, y, t + d);
    if (t + d <= 1) world_clear_object(x, y);
  }
  quarts_rebuild(x, y, x, y);
  mark_tile(x, y);
}

void shaman_apply(void) {
  if (!s_has_op) return;
  s_has_op = 0;
  if (P.kind == PK_NONE) return;
  if (P.kind == PK_STAGE) {                                // the next monument stage rises
    int b = mon_b();
    if (b >= 0 && B[b].st < 5 && s_faith >= (uint16_t)mon_cost(B[b].st + 1)) {
      int c = mon_cost(B[b].st + 1);
      s_faith = (uint16_t)(s_faith - c); s_spent += (uint32_t)c; s_casts++;
      B[b].st++;
      s_mon_day = G.s.day;
      if (B[b].st == 5) s_built_tot++;
      for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) mark_tile(B[b].x + i, B[b].y + j);
    }
    P.ops++;
    return;
  }
  apply_tile(s_wx, s_wy, s_wd, 0);
  P.ops++;
  if (s_ramping && s_wd != 0 && s_wd != 3) { s_lx = s_wx; s_ly = s_wy; s_ld = s_wd; }
}

// After a load the land is generated from the seed again: level every footprint and cut ramps at once, free of charge.
static void heal_terrain(void) {
  P.kind = PK_NONE;
  for (int b = 0; b < MAXB; b++) {
    if (!bused(b) || btype(b) == B_FISH) continue;
    int w, h;
    bdims(b, &w, &h);
    int H = hh(B[b].x, B[b].y);
    H = H < 3 ? 3 : (H > 5 ? 5 : H);
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) { world_set_height(B[b].x + i, B[b].y + j, H); world_clear_object(B[b].x + i, B[b].y + j); }
  }
  for (int guard = 0; guard < 400; guard++) {
    int x, y, d;
    if (!next_ramp(cx(), cy(), &x, &y, &d)) break;
    apply_tile(x, y, d, 1);
  }
  access_free();
}

// ------------------------------------------------------------------ needs and projects
static const char* const BNAME[B_COUNT] = {"TENT", "HUT", "COTTAGE", "MANOR", "FIELD", "ORCHARD", "PEN", "FISHING HUT", "WELL", "LUMBER HUT", "GRANARY", "SHRINE", "INFIRMARY", "MONUMENT"};
const char* shaman_label(void) {
  static char buf[28];
  if (P.kind == PK_NONE) return 0;
  if (s_waiting && !s_has_op) return s_waiting == 2 ? "WAITING FOR BUILDERS" : "PRAYING FOR FAITH";
  if (P.kind == PK_ACCESS) return "CUTTING RAMPS";
  if (P.kind == PK_POND) return s_ramping ? "CUTTING RAMPS" : "DIGGING POND";
  if (P.kind == PK_PLOT) return s_ramping ? "CUTTING RAMPS" : "FLATTENING PLOT";
  if (P.kind == PK_LEVEL) return s_ramping ? "CUTTING RAMPS" : (P.hgt == 255 ? "CLEARING TREES" : "LEVELLING LAND");
  if (s_ramping) return "CUTTING RAMPS";
  if (P.kind == PK_STAGE) {
    static const char* const MN[5] = {"PYRAMID", "STONE CIRCLE", "GREAT TREE", "OBELISK", "TEMPLE"};
    const char* r = "RAISING THE ";
    int k = 0;
    for (; r[k]; k++) buf[k] = r[k];
    for (const char* q = MN[village_mon_type()]; *q && k < 26; q++) buf[k++] = *q;
    buf[k] = 0;
    return buf;
  }
  const char* a = "BUILDING ";
  int n = 0;
  for (; a[n]; n++) buf[n] = a[n];
  for (const char* q = BNAME[P.tp]; *q && n < 26; q++) buf[n++] = *q;
  buf[n] = 0;
  return buf;
}

// 1 started, 0 nothing possible, 2 not enough faith (the wood is there)
static int start_project(int k, int nolevel_unused) {
  (void)nolevel_unused;
  if (k == SP_STAGE) {
    int b = mon_b();
    if (b < 0 || B[b].st >= 5 || s_faith < (uint16_t)mon_cost(B[b].st + 1)) return 0;
    P.kind = PK_STAGE; P.tp = B_MONUMENT; P.var = 0; P.x = B[b].x; P.y = B[b].y; P.w = P.h = 3; P.hgt = 255; P.ops = 0; P.wait = 0;
    s_has_op = 0; s_ramping = 0;
    for (int i = 0; i < NFAIL; i++) s_skip[i][0] = s_skip[i][1] = 255;
    return 1;
  }
  if (k < B_COUNT && s_wood < (uint16_t)cost_wood(k)) return 0;
  Cand c;
  if (!pick_site(k, &c, 0)) return 0;
  if (c.faith > (int)s_faith) return 2;
  int w, h;
  kdim(k, c.var, &w, &h);
  P.kind = k == SP_POND ? PK_POND : (k == SP_PLOT ? PK_PLOT : (k == SP_LEVEL ? PK_LEVEL : PK_BUILD));
  P.tp = (uint8_t)(k < B_COUNT ? k : 0); P.var = c.var; P.x = c.x; P.y = c.y; P.w = (uint8_t)w; P.h = (uint8_t)h; P.hgt = c.H; P.ops = 0; P.wait = 0;
  s_has_op = 0; s_ramping = 0;
  for (int i = 0; i < NFAIL; i++) s_skip[i][0] = s_skip[i][1] = 255;
  {   // r8: a plot that needs work but that he cannot stand beside (and no ramp would help) is turned down now, not after a walk and some faith
    int needs = 0, sx, sy, x, y, d;
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) {
        uint8_t b = g_map[(P.y + j) * WM + P.x + i];
        if ((MT_OBJ(b) && MT_H(b) >= 2) || (P.hgt != 255 && MT_H(b) != P.hgt)) needs = 1;
      }
    world_shaman_tile(&sx, &sy);
    world_flow(sx, sy);
    if (needs && !next_terra(sx, sy, &x, &y, &d) && !next_ramp(sx, sy, &x, &y, &d) && !next_access(sx, sy, &x, &y)) {
      P.kind = PK_NONE; add_fail(c.x, c.y); s_turned++;
      return 0;
    }
  }
  if (k < B_COUNT) s_wood = (uint16_t)(s_wood - cost_wood(k));
  return 1;
}

// health: the first villager who falls sick (real sickness, not hunger) calls for an infirmary. A hook for r6: the monument's spare faith
static int need_health(int weak) { (void)weak; return s_sick && bcount(B_INFIRM, 1) == 0 ? 110 : 0; }
// the monument: laid out when the town is big enough, then raised in five stages, one every MON_GAP_DAYS days at the most, each paid for in faith
static int mon_open(void) { return pop() >= unlock_pop(B_MONUMENT) && G.s.day >= MON_MIN_DAY; }
static int mon_saving(void) { int b = mon_b(); return s_phase == PH_PLAY && b >= 0 && B[b].st < 5 && mon_open(); }
static int need_monument(void) {
  if (s_phase != PH_PLAY) return 0;
  int b = mon_b();
  if (b < 0) return pop() >= 10 && G.s.day >= 5 && s_wood >= (uint16_t)cost_wood(B_MONUMENT) ? 62 : 0;      // the plot is marked out early, while there is room
  int st = B[b].st;
  if (st >= 5 || !mon_open() || G.s.day < s_mon_day + MON_GAP_DAYS || s_faith < (uint16_t)mon_cost(st + 1)) return 0;
  return 98;
}

static int water_far(void) {            // some home has no water within 8 tiles
  flow_key(K_WATER);
  for (int b = 0; b < MAXB; b++) {
    if (!bused(b) || !is_home(btype(b))) continue;
    int w, h, best = 255;
    bdims(b, &w, &h);
    for (int j = -1; j <= h; j++)
      for (int i = -1; i <= w; i++) {
        int x = B[b].x + i, y = B[b].y + j;
        if (x < 0 || y < 0 || x >= WM || y >= WM) continue;
        if (g_dist[y * WM + x] < best) best = g_dist[y * WM + x];
      }
    if (best > 8) return 1;
  }
  return 0;
}

static int try_kinds(const int* ks, int n, int* faith_short) {
  for (int i = 0; i < n; i++) {
    int r = start_project(ks[i], 0);
    if (r == 1) return 1;
    if (r == 2) *faith_short = 1;
  }
  return 0;
}

// A building the village cannot get to any more (trees grew round it, ground moved): he clears the trees around it.
static int mend(void) {
  world_flow(cx(), cy());
  for (int b = 0; b < MAXB; b++) {
    if (!bused(b)) continue;
    int bw_, bh_;
    bdims(b, &bw_, &bh_);
    if (ring_access(B[b].x, B[b].y, bw_, bh_)) continue;
    int x0 = B[b].x - 2, y0 = B[b].y - 2, x1 = B[b].x + bw_ + 1, y1 = B[b].y + bh_ + 1, trees = 0;
    if (x0 < 1) x0 = 1;
    if (y0 < 1) y0 = 1;
    if (x1 > WM - 2) x1 = WM - 2;
    if (y1 > WM - 2) y1 = WM - 2;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) if (MT_OBJ(g_map[y * WM + x]) && hh(x, y) >= 2) trees++;
    if (!trees || s_faith < (uint16_t)trees) { s_mend_cool = 20; return 0; }
    P.kind = PK_LEVEL; P.tp = 0; P.var = 0; P.x = (uint8_t)x0; P.y = (uint8_t)y0; P.w = (uint8_t)(x1 - x0 + 1); P.h = (uint8_t)(y1 - y0 + 1); P.hgt = 255; P.ops = 0; P.wait = 0;
    s_has_op = 0; s_ramping = 0;
    return 1;
  }
  return 0;
}

static void shaman_plan(void) {
#ifdef MONDBG
  { static int ld=-1; if (G.s.day>=22 && G.s.day!=ld) { ld=G.s.day; int b=mon_b(); fprintf(stderr,"day %d P.kind %d wait %d pop %d mon_b %d st %d need %d gap %d aborts %d faith %d open %d\n",G.s.day,P.kind,s_waiting,pop(),b,b<0?-1:B[b].st,need_monument(),(int)s_mon_day,(int)s_aborts,s_faith,mon_open()); } }
#endif
  for (int i = 0; i < N_COUNT; i++) if (s_cool[i]) s_cool[i]--;
  if (P.kind != PK_NONE) {
    if (s_waiting && ++P.wait > WAIT_MAX) { shaman_abort(); s_waiting = 0; }
    return;
  }
  if (s_acc_cool) s_acc_cool--;
  else if (access_project()) return;
  if (s_mend_cool) s_mend_cool--;
  else if (mend()) return;
  int p = pop();
  if (!p) return;
  int weak = 0;
  for (int i = 0; i < MAXV; i++) if (used(i) && (V[i].fl & F_WEAK)) weak++;
  int fields = bcount(B_FIELD, 1), orch = bcount(B_ORCHARD, 1), pens = bcount(B_PEN, 1), fish = bcount(B_FISH, 1);
  int beds = beds_planned(), tr = tier();
  int coast = near_coast();
  int sc[N_COUNT];
  for (int i = 0; i < N_COUNT; i++) sc[i] = 0;
  int src = (fields > 0) + (orch > 0) + (pens > 0) + (fish > 0);
  if (s_food < (uint16_t)(p * 6)) sc[N_FOOD] = 95;
  else if (fields * 5 < p + 2) sc[N_FOOD] = 75;
  else if ((p >= 10 && src < 4) || (p >= 8 && src < 3)) sc[N_FOOD] = 40;
  if (beds < p) sc[N_HOUSING] = 100; else if (beds < p + 2) sc[N_HOUSING] = 65;
  if (p >= 2 && bcount(B_WELL, 1) < 2 + p / 8 && water_far()) sc[N_WATER] = 85;
  if (!coast && p >= 2) sc[N_POND] = 80;
  sc[N_HEALTH] = need_health(weak);
  if (s_wood < 10 && bcount(B_LUMBER, 1) < 1 + p / 12 && p >= 4) sc[N_WOOD] = 60;
  if (tr >= 1 && !plot_ready()) sc[N_GROWTH] = 45;
  else if (tr >= 1 && beds < p + 3) sc[N_GROWTH] = 50;
  else if (p >= 10 && bcount(B_GRANARY, 1) < 1 + p / 24) sc[N_GROWTH] = 35;
  if (p >= 6 && bcount(B_SHRINE, 1) == 0) sc[N_FAITH] = 45;
  sc[N_MONUMENT] = need_monument();
  // spare faith is put into the land: wide flat ground for the next tiers, a second pond for fish (r6 will keep a reserve for the monument)
  if (s_faith * 5 >= (uint16_t)(60 + 4 * p) * 2 && !mon_saving()) sc[N_SHAPE] = 30;
  // personality
  switch (s_pers) {
    case SH_FARMER: sc[N_FOOD] += sc[N_FOOD] ? 10 : 0; break;
    case SH_HERDER: sc[N_FOOD] += sc[N_FOOD] ? 5 : 0; sc[N_GROWTH] += sc[N_GROWTH] ? 5 : 0; break;
    case SH_FISHER: sc[N_WATER] += sc[N_WATER] ? 8 : 0; sc[N_FOOD] += sc[N_FOOD] ? 5 : 0; break;
    default: sc[N_HOUSING] += sc[N_HOUSING] ? 10 : 0; sc[N_GROWTH] += sc[N_GROWTH] ? 10 : 0; sc[N_WOOD] += sc[N_WOOD] ? 5 : 0; break;
  }
  int tried = 0, saving = 0;
  for (int round = 0; round < N_COUNT; round++) {
    int best = -1, bs = mon_saving() ? 59 : 29;           // raising the monument: only what is needed comes before it
    for (int i = 0; i < N_COUNT; i++) if (!(tried & (1 << i)) && !s_cool[i] && sc[i] > bs) { bs = sc[i]; best = i; }
    if (best < 0) break;
    tried |= 1 << best;
    int ks[6], n = 0, fs = 0, ok = 0;
    switch (best) {
      case N_FOOD: {
        int emergency = s_food < (uint16_t)(p * 6);
        int want_field = fields * 5 < p + 2, can_fish = coast && p >= 3 && fish < 1 + p / 12;
        int can_orch = p >= unlock_pop(B_ORCHARD) && orch < 1 + p / 14, can_pen = p >= unlock_pop(B_PEN) && pens < 1 + p / 20;
        if (!emergency) {
          if (s_pers == SH_FISHER && can_fish) ks[n++] = B_FISH;
          if (s_pers == SH_HERDER && can_pen) ks[n++] = B_PEN;
          if (s_pers == SH_FARMER && can_orch) ks[n++] = B_ORCHARD;
        }
        if (want_field) ks[n++] = B_FIELD;
        if (can_fish) ks[n++] = B_FISH;
        if (can_orch) ks[n++] = B_ORCHARD;
        if (can_pen) ks[n++] = B_PEN;
        if (!n && fields * 4 < p + 4) ks[n++] = B_FIELD;
        break;
      }
      case N_HOUSING: for (int t = tr; t >= 0 && n < 4; t--) ks[n++] = t; break;
      case N_WATER: ks[n++] = B_WELL; break;
      case N_POND: ks[n++] = SP_POND; break;
      case N_HEALTH: ks[n++] = B_INFIRM; break;
      case N_WOOD: ks[n++] = B_LUMBER; break;
      case N_GROWTH:
        if (!plot_ready() && tr >= 1) ks[n++] = SP_PLOT;
        else if (tr >= 1 && beds < p + 3) ks[n++] = tr;
        else ks[n++] = B_GRANARY;
        break;
      case N_FAITH: ks[n++] = B_SHRINE; break;
      case N_MONUMENT: ks[n++] = mon_b() < 0 ? (int)B_MONUMENT : (int)SP_STAGE; break;
      case N_SHAPE:
        if (saving) continue;
        if (p >= 8 && s_ponds < 2 && s_faith >= 40) ks[n++] = SP_POND;
        ks[n++] = SP_LEVEL;
        break;
      default: continue;
    }
    if (saving) {                       // saving up for something big: only what costs no faith
      int m = 0;
      for (int i = 0; i < n; i++) {
        Cand c;
        if (ks[i] < B_COUNT && s_wood < (uint16_t)cost_wood(ks[i])) continue;
        if (pick_site(ks[i], &c, 0) && c.faith == 0) ks[m++] = ks[i];
      }
      n = m;
    }
    ok = try_kinds(ks, n, &fs);
    if (ok) { s_pneed = (uint8_t)best; s_roles_dirty = 1; return; }
    if (fs && bs >= 70) saving = 1;
    if (!fs && !saving) s_cool[best] = 6;
  }
}
