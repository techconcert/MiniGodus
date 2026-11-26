// The villagers of MiniGodus (release r3). See village.h.
//
// Time is the real clock: village_minute() is called once for every minute that passes, so meals happen at 07:30,
// 12:30 and 19:00 and everybody sleeps in the clock's sleep schedule. Walking is real time: village_tick() moves the
// villagers along a distance field; everybody going to the same place shares one field, and one field is computed per
// step round, in the shared scratch buffer g_dist.
#include "village.h"

enum { F_AGE = 3, F_FEM = 4, F_USED = 8, F_WEAK = 16, F_INSIDE = 32 };       // Vil.fl, bits 6-7: facing
enum { VSTEP_MS = 90, ANIM_MS = 420 };
enum { K_CENTER = 100, K_WATER = 101, K_TREE = 102, K_SHORE = 103 };            // flow targets besides buildings (index + 1)
enum { SITE = 0x80 };                                                          // Bld.tp: a building site, not yet finished

struct Vil { uint8_t x, y, fl, ra, age, nd; };   // tile, flags, role | act << 4, age in days | lifespan variant << 5, hunger | thirst << 3
struct Bld { uint8_t x, y, tp, st; };            // tile, type (| SITE), progress / field stage
static_assert(sizeof(Vil) == 6, "a villager is 6 bytes");

static Vil      V[MAXV];
static Bld      B[MAXB];
static uint8_t  s_home[MAXV];                    // building index of each villager's bed, 255 = none
static uint16_t s_food, s_wood, s_faith, s_deaths, s_births;
static uint8_t  s_water, s_vanim, s_rr, s_woke, s_last_plan;
static uint32_t s_vacc, s_anim_acc, s_rng;

static uint32_t vr(void) { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }

static int used(int i) { return (V[i].fl & F_USED) != 0; }
static int vrole(int i) { return V[i].ra & 15; }
static int vact(int i) { return V[i].ra >> 4; }
static void set_role(int i, int r) { V[i].ra = (uint8_t)((V[i].ra & 0xF0) | r); }
static void set_act(int i, int a) { V[i].ra = (uint8_t)((V[i].ra & 15) | (a << 4)); }
static int vcls(int i) { return V[i].fl & F_AGE; }
static int hunger(int i) { return V[i].nd & 7; }
static int thirst(int i) { return V[i].nd >> 3; }
static void set_hunger(int i, int h) { V[i].nd = (uint8_t)((V[i].nd & 0xF8) | (h > 7 ? 7 : h)); }
static void set_thirst(int i, int t) { V[i].nd = (uint8_t)((V[i].nd & 7) | ((t > 31 ? 31 : t) << 3)); }
static int life(int i) { int v = V[i].age >> 5; return 20 + v + (v == 7 ? 1 : 0); }     // adult lifespan, 20..28 days
static int days(int i) { return V[i].age & 31; }
static int is_moving(int i) { int a = vact(i); return a == A_GO || a == A_BED || a == A_DRINK || a == A_RET; }

static int cx(void) { return g_world.start_x; }
static int cy(void) { return g_world.start_y; }

static int btype(int b) { return B[b].tp & 7; }
static int bused(int b) { return B[b].tp != 0xFF; }
static int bsite(int b) { return bused(b) && (B[b].tp & SITE); }
static int bdone(int b, int tp) { return bused(b) && !(B[b].tp & SITE) && btype(b) == tp; }
static int bw(int tp) { return tp == B_HUT ? 2 : 1; }
static int cap_of(int tp) { return tp == B_TENT ? 2 : (tp == B_HUT ? 4 : 0); }
static int need_min(int tp) { static const uint8_t N[B_COUNT] = {20, 60, 15, 45, 40}; return N[tp]; }
static int cost_wood(int tp) { static const uint8_t C[B_COUNT] = {4, 10, 2, 8, 6}; return C[tp]; }

static int count_done(int tp) { int n = 0; for (int b = 0; b < MAXB; b++) if (bdone(b, tp)) n++; return n; }
static int count_sites(void) { int n = 0; for (int b = 0; b < MAXB; b++) if (bsite(b)) n++; return n; }
static int pop(void) { int n = 0; for (int i = 0; i < MAXV; i++) if (used(i)) n++; return n; }
static int nth_bld(int tp, int n) {          // the n-th finished building of a type, cyclic; -1 if there is none
  int c = count_done(tp);
  if (!c) return -1;
  n %= c;
  for (int b = 0; b < MAXB; b++) if (bdone(b, tp) && n-- == 0) return b;
  return -1;
}
static int first_site(void) { for (int b = 0; b < MAXB; b++) if (bsite(b)) return b; return -1; }
static int rank_of(int i) { int n = 0, r = vrole(i); for (int j = 0; j < i; j++) if (used(j) && vrole(j) == r) n++; return n; }

static void mark_tile(int tx, int ty) {
  (void)tx;
  int x, y;
  world_cam(&x, &y);
  mark_rows(ty * GC_TILE - y + GC_STRIP_H - 12, 40);
}

// ---------------------------------------------------------------- beds
static void rebuild_homes(void) {
  uint8_t cap[MAXB];
  for (int b = 0; b < MAXB; b++) cap[b] = bused(b) && !(B[b].tp & SITE) ? (uint8_t)cap_of(btype(b)) : 0;
  int b = 0;
  for (int i = 0; i < MAXV; i++) {
    s_home[i] = 255;
    if (!used(i)) continue;
    while (b < MAXB && !cap[b]) b++;
    if (b >= MAXB) continue;
    s_home[i] = (uint8_t)b;
    if (--cap[b] == 0) b++;
  }
}
static int capacity(void) { int n = 0; for (int b = 0; b < MAXB; b++) if (bused(b) && !(B[b].tp & SITE)) n += cap_of(btype(b)); return n; }

// ---------------------------------------------------------------- walking targets
static void seed_around(int b) {
  int w = bw(btype(b)), x0 = B[b].x, y0 = B[b].y;
  for (int j = 0; j < w; j++)
    for (int i = 0; i < w; i++) {
      int x = x0 + i, y = y0 + j;
      if (i == 0) world_flow_seed(x - 1, y, 0);
      if (i == w - 1) world_flow_seed(x + 1, y, 0);
      if (j == 0) world_flow_seed(x, y - 1, 0);
      if (j == w - 1) world_flow_seed(x, y + 1, 0);
    }
}
static int water_at(int x, int y) { return x >= 0 && y >= 0 && x < WM && y < WM && MT_H(g_map[y * WM + x]) <= 1; }
static int tree_at(int x, int y) { return x >= 0 && y >= 0 && x < WM && y < WM && MT_OBJ(g_map[y * WM + x]) == OB_TREE && MT_H(g_map[y * WM + x]) >= 2; }

static void flow_key(int key) {
  world_flow_begin();
  if (key >= 1 && key <= MAXB) { if (bused(key - 1)) seed_around(key - 1); }
  else if (key == K_CENTER) world_flow_seed(cx(), cy(), 0);
  else if (key == K_WATER && count_done(B_WELL)) { for (int b = 0; b < MAXB; b++) if (bdone(b, B_WELL)) seed_around(b); }
  else if (key == K_WATER || key == K_SHORE) {
    for (int y = 0; y < WM; y++) for (int x = 0; x < WM; x++)
      if (water_at(x - 1, y) || water_at(x + 1, y) || water_at(x, y - 1) || water_at(x, y + 1)) world_flow_seed(x, y, 0);
  } else if (key == K_TREE) {
    for (int y = 0; y < WM; y++) for (int x = 0; x < WM; x++)
      if (tree_at(x - 1, y) || tree_at(x + 1, y) || tree_at(x, y - 1) || tree_at(x, y + 1)) world_flow_seed(x, y, 0);
  }
  world_flow_run();
}

static int job_key(int i) {
  switch (vrole(i)) {
    case R_FARMER: { int b = nth_bld(B_FIELD, rank_of(i)); return b < 0 ? 0 : b + 1; }
    case R_FISHER: { int b = nth_bld(B_FISH, rank_of(i)); return b < 0 ? K_SHORE : b + 1; }
    case R_LUMBER: return K_TREE;
    case R_BUILDER: { int b = first_site(); return b < 0 ? 0 : b + 1; }
    case R_CARRIER: return K_WATER;
    case R_HEALER: case R_ELDER: return K_CENTER;
    default: return 0;
  }
}
static int key_of(int i) {
  switch (vact(i)) {
    case A_BED: return s_home[i] < MAXB ? s_home[i] + 1 : K_CENTER;
    case A_DRINK: return K_WATER;
    case A_RET: return K_CENTER;
    case A_GO: return job_key(i);
    default: return 0;
  }
}

// ---------------------------------------------------------------- building sites
struct Cand { uint8_t x, y; };

static int free_land(int x, int y) {
  if (x < 1 || y < 1 || x >= WM - 1 || y >= WM - 1) return -1;
  uint8_t b = g_map[y * WM + x];
  int h = MT_H(b);
  if (h < 3 || h > 4 || MT_OBJ(b) != OB_NONE || MT_SOIL(b) == SOIL_BUILT) return -1;
  return h;
}

static int fits(int tp, int x, int y) {
  int w = bw(tp);
  if (tp == B_FISH) {
    uint8_t b = g_map[y * WM + x];
    int h = MT_H(b);
    return x >= 1 && x < WM - 2 && y >= 1 && y < WM - 1 && h >= 2 && h <= 3 && MT_OBJ(b) == OB_NONE && MT_SOIL(b) != SOIL_BUILT
        && MT_H(g_map[y * WM + x + 1]) <= 1;
  }
  int h0 = free_land(x, y);
  if (h0 < 0) return 0;
  for (int j = 0; j < w; j++)
    for (int i = 0; i < w; i++) {
      if (free_land(x + i, y + j) != h0) return 0;
      if (x + i >= cx() - 1 && x + i <= cx() + 1 && y + j >= cy() - 1 && y + j <= cy() + 1) return 0;      // keep the village green clear
    }
  return 1;
}

static int count_reach(void) { int n = 0; for (int i = 0; i < WM * WM; i++) if (g_dist[i] != 255) n++; return n; }

// Puts a building (or site) of type tp near the village green without cutting the map in two. Returns the index or -1.
static int place(int tp, int site) {
  int slot = -1;
  for (int b = 0; b < MAXB; b++) if (!bused(b)) { slot = b; break; }
  if (slot < 0) return -1;
  Cand cand[10];
  int nc = 0;
  world_flow(cx(), cy());
  int base = count_reach();
  int start = (int)(vr() % (WM * WM));
  int w = tp == B_FISH ? 1 : bw(tp);
  for (int d = 2; d < 40 && nc < 10; d++)
    for (int k = 0; k < WM * WM && nc < 10; k++) {
      int i = (k + start) % (WM * WM);
      if (g_dist[i] != d) continue;
      int x = i % WM, y = i / WM;
      if (fits(tp, x, y)) { cand[nc].x = (uint8_t)x; cand[nc].y = (uint8_t)y; nc++; }
    }
  for (int c = 0; c < nc; c++) {
    int x = cand[c].x, y = cand[c].y;
    uint8_t keep[4];
    for (int j = 0; j < w; j++) for (int i = 0; i < w; i++) { keep[j * 2 + i] = g_map[(y + j) * WM + x + i]; world_set_built(x + i, y + j, 1); }
    world_flow(cx(), cy());
    if (count_reach() == base - w * w) {
      B[slot].x = (uint8_t)x; B[slot].y = (uint8_t)y; B[slot].tp = (uint8_t)(tp | (site ? SITE : 0)); B[slot].st = 0;
      mark_tile(x, y); mark_tile(x, y + 1);
      return slot;
    }
    for (int j = 0; j < w; j++) for (int i = 0; i < w; i++) g_map[(y + j) * WM + x + i] = keep[j * 2 + i];
  }
  return -1;
}

static int plan(void) {
  int p = pop(), sites = count_sites();
  if (sites >= (p >= 12 ? 2 : 1) || p == 0) return 0;
  int fields = count_done(B_FIELD), fish = count_done(B_FISH), wells = count_done(B_WELL);
  for (int b = 0; b < MAXB; b++) if (bsite(b)) { int t = btype(b); if (t == B_FIELD) fields++; else if (t == B_FISH) fish++; else if (t == B_WELL) wells++; }
  int beds = capacity();
  for (int b = 0; b < MAXB; b++) if (bsite(b)) beds += cap_of(btype(b));
  int food_low = s_food < p * 6;
  int want = -1;
  if (food_low && fields * 5 < p + 2) want = B_FIELD;
  else if (beds < p + 1) want = (s_wood >= cost_wood(B_HUT) ? B_HUT : B_TENT);
  else if (fields * 5 < p + 2) want = B_FIELD;
  else if (!wells && p >= 3) want = B_WELL;
  else if (fish < 1 + p / 16 && p >= 4) want = B_FISH;
  else if (wells < 1 + p / 14 && p >= 8) want = B_WELL;
  if (want < 0 || s_wood < cost_wood(want)) return 0;
  if (place(want, 1) < 0) return 0;
  s_wood = (uint16_t)(s_wood - cost_wood(want));
  return 1;
}

// ---------------------------------------------------------------- roles
static void assign_roles(void) {
  int p = pop(), weak = 0, adults = 0;
  for (int i = 0; i < MAXV; i++) if (used(i)) { if (V[i].fl & F_WEAK) weak++; if (vcls(i) == AGE_ADULT) adults++; }
  int food_low = s_food < p * 6;
  int want[R_COUNT];
  for (int r = 0; r < R_COUNT; r++) want[r] = 0;
  int sites = count_sites();
  want[R_BUILDER] = sites ? (sites >= 2 && adults >= 6 ? 2 : 1) : 0;
  want[R_FARMER] = count_done(B_FIELD);
  want[R_FISHER] = count_done(B_FISH);
  want[R_LUMBER] = s_wood < 8 && adults >= 5 ? 2 : (s_wood < 24 ? 1 : 0);
  want[R_CARRIER] = p >= 4 ? 1 : 0;
  want[R_HEALER] = (weak > 0 || p >= 12) ? 1 : 0;
  static const uint8_t ORDER_FOOD[6] = {R_FARMER, R_FISHER, R_BUILDER, R_LUMBER, R_CARRIER, R_HEALER};
  static const uint8_t ORDER_OK[6] = {R_BUILDER, R_FARMER, R_FISHER, R_LUMBER, R_CARRIER, R_HEALER};
  const uint8_t* order = food_low ? ORDER_FOOD : ORDER_OK;
  uint8_t taken[MAXV];
  for (int i = 0; i < MAXV; i++) taken[i] = 0;
  for (int i = 0; i < MAXV; i++) {
    if (!used(i)) continue;
    if (vcls(i) == AGE_CHILD) { set_role(i, R_CHILD); taken[i] = 1; }
    else if (vcls(i) == AGE_ELDER) { set_role(i, R_ELDER); taken[i] = 1; }
    else if (V[i].fl & F_WEAK) { set_role(i, R_IDLE); taken[i] = 1; }
  }
  int rot = (int)(vr() % MAXV);
  for (int k = 0; k < 6; k++) {
    int r = order[k];
    for (int n = 0; n < want[r]; n++)
      for (int q = 0; q < MAXV; q++) {
        int i = (q + rot) % MAXV;
        if (!used(i) || taken[i]) continue;
        set_role(i, r); taken[i] = 1;
        break;
      }
  }
  for (int i = 0; i < MAXV; i++) {
    if (!used(i) || taken[i]) continue;
    set_role(i, food_low ? R_FISHER : (s_wood < 60 ? R_LUMBER : R_IDLE));
  }
  for (int i = 0; i < MAXV; i++) if (used(i) && (vact(i) == A_GO || vact(i) == A_WORK)) set_act(i, A_IDLE);
}

// ---------------------------------------------------------------- people
static int add_villager(int age_class, int female, int age_days, int x, int y) {
  for (int i = 0; i < MAXV; i++) {
    if (used(i)) continue;
    V[i].x = (uint8_t)x; V[i].y = (uint8_t)y;
    V[i].fl = (uint8_t)(F_USED | age_class | (female ? F_FEM : 0) | ((vr() & 3) << 6));
    V[i].age = (uint8_t)((age_days & 31) | ((vr() & 7) << 5));
    V[i].nd = 0;
    V[i].ra = (uint8_t)(age_class == AGE_CHILD ? R_CHILD : R_IDLE);
    return i;
  }
  return -1;
}

static void kill(int i) {
  V[i].fl = 0;
  s_deaths++;
  mark_tile(V[i].x, V[i].y);
}

static void home_spot(int i, int* x, int* y) {
  int h = s_home[i];
  *x = cx(); *y = cy();
  if (h < MAXB && bused(h)) {
    static const int8_t D[4][2] = {{0, 2}, {-1, 0}, {2, 0}, {0, -1}};
    for (int k = 0; k < 4; k++) {
      int xx = B[h].x + D[k][0], yy = B[h].y + D[k][1];
      if (world_walkable(xx, yy)) { *x = xx; *y = yy; return; }
    }
  }
}

void village_new_day(int night) {
  (void)night;
  for (int i = 0; i < MAXV; i++) {
    if (!used(i)) continue;
    if (days(i) < 31) V[i].age++;
    int d = days(i), l = life(i);
    if (d >= l) { kill(i); continue; }
    if (vcls(i) == AGE_CHILD && d >= GROW_UP_DAYS) { V[i].fl = (uint8_t)((V[i].fl & ~F_AGE) | AGE_ADULT); set_role(i, R_IDLE); }
    else if (vcls(i) == AGE_ADULT && d >= l - 4) { V[i].fl = (uint8_t)((V[i].fl & ~F_AGE) | AGE_ELDER); set_role(i, R_ELDER); }
  }
  rebuild_homes();
  // the forest regrows: a few saplings a day, on free land away from the village green
  int trees = 0;
  for (int i = 0; i < WM * WM; i++) if (MT_OBJ(g_map[i]) == OB_TREE) trees++;
  for (int k = 0; k < 4 && trees < 60; k++) {
    int x = (int)(vr() % WM), y = (int)(vr() % WM);
    uint8_t b = g_map[y * WM + x];
    int dx = x - cx(), dy = y - cy();
    if (MT_H(b) < 3 || MT_H(b) > 5 || MT_OBJ(b) != OB_NONE || MT_SOIL(b) == SOIL_BUILT || dx * dx + dy * dy < 9) continue;
    g_map[y * WM + x] = (uint8_t)((b & 0x3F) | (OB_TREE << 6));
    trees++;
    mark_tile(x, y);
  }
  // a child is born when there is a free bed, two days of food to spare and a fertile couple
  int p = pop(), m = 0, f = 0;
  for (int i = 0; i < MAXV; i++)
    if (used(i) && vcls(i) == AGE_ADULT && !(V[i].fl & F_WEAK)) { if (V[i].fl & F_FEM) f++; else m++; }
  if (p < MAXV && m && f && p < capacity() && s_food >= (uint16_t)(p * 6)) {
    int x, y;
    int i = add_villager(AGE_CHILD, (int)(vr() & 1), 0, cx(), cy());
    if (i >= 0) {
      rebuild_homes();
      home_spot(i, &x, &y);
      V[i].x = (uint8_t)x; V[i].y = (uint8_t)y;
      V[i].age = (uint8_t)(V[i].age & 0xE0);
      s_births++;
      mark_tile(x, y);
    }
  }
}

// ---------------------------------------------------------------- the clock
static void meal(int m) {
  (void)m;
  int healer = 0;
  for (int i = 0; i < MAXV; i++) if (used(i) && vrole(i) == R_HEALER && vact(i) != A_SLEEP) healer = 1;
  int start = s_deaths + (int)(vr() % MAXV);
  for (int k = 0; k < MAXV; k++) {
    int i = (k + start) % MAXV;
    if (!used(i) || vact(i) == A_SLEEP || vact(i) == A_BED) continue;
    if (s_food > 0) {
      s_food--;
      set_hunger(i, 0);
      if (thirst(i) < 28) V[i].fl &= (uint8_t)~F_WEAK;
      if (vact(i) != A_DRINK && vact(i) != A_RET) set_act(i, A_EAT);
      mark_tile(V[i].x, V[i].y);
    } else {
      set_hunger(i, hunger(i) + 1);
      if (hunger(i) >= HUNGER_WEAK) V[i].fl |= F_WEAK;
      if (hunger(i) >= HUNGER_DEATH + healer) kill(i);
    }
  }
}

static int meal_minute(int m) { return m == MEAL_1 || m == MEAL_2 || m == MEAL_3; }

void village_minute(int m, int night) {
  int changed = 0;
  // eating takes one minute
  for (int i = 0; i < MAXV; i++) if (used(i) && vact(i) == A_EAT) { set_act(i, A_IDLE); mark_tile(V[i].x, V[i].y); }

  // sleep and wake up on the clock's schedule
  for (int i = 0; i < MAXV; i++) {
    if (!used(i)) continue;
    int a = vact(i);
    if (night) {
      if (a != A_BED && a != A_SLEEP) { set_act(i, A_BED); V[i].fl &= (uint8_t)~F_INSIDE; }
    } else if (a == A_BED || a == A_SLEEP) {
      set_act(i, A_IDLE); V[i].fl &= (uint8_t)~F_INSIDE; s_woke = 1; mark_tile(V[i].x, V[i].y);
    }
  }
  rebuild_homes();

  if (meal_minute(m)) meal(m);

  // thirst: every 20 minutes awake, every hour asleep
  if (m % 20 == 0)
    for (int i = 0; i < MAXV; i++) {
      if (!used(i) || ((vact(i) == A_SLEEP) && m % 60 != 0)) continue;
      set_thirst(i, thirst(i) + 1);
      if (thirst(i) >= 28) V[i].fl |= F_WEAK;
      if (thirst(i) >= 31 && m % 60 == 0) { set_hunger(i, hunger(i) + 1); if (hunger(i) >= HUNGER_DEATH + 1) kill(i); }
    }
  for (int i = 0; i < MAXV; i++) {
    if (!used(i) || thirst(i) < 12) continue;
    int a = vact(i);
    if (a == A_BED || a == A_SLEEP || a == A_EAT || a == A_DRINK || a == A_RET) continue;
    if (s_water > 0) { s_water--; set_thirst(i, 0); }
    else set_act(i, A_DRINK);
  }

  // work
  int sb = first_site();
  for (int i = 0; i < MAXV; i++) {
    if (!used(i) || vact(i) != A_WORK || (V[i].fl & F_WEAK)) continue;
    switch (vrole(i)) {
      case R_FARMER: {
        int b = nth_bld(B_FIELD, rank_of(i));
        if (b >= 0 && (B[b].st & 3) == 2) { s_food = (uint16_t)(s_food + 8); B[b].st = 0; mark_tile(B[b].x, B[b].y); }
        break;
      }
      case R_FISHER:
        if (m % 15 == 0) s_food = (uint16_t)(s_food + (count_done(B_FISH) ? 3 : 2));
        break;
      case R_LUMBER:
        if (m % 8 == 0) {
          static const int8_t D[4][2] = {{0, 1}, {0, -1}, {-1, 0}, {1, 0}};
          int found = 0;
          for (int k = 0; k < 4 && !found; k++) {
            int tx = V[i].x + D[k][0], ty = V[i].y + D[k][1];
            if (tree_at(tx, ty)) { world_clear_object(tx, ty); s_wood = (uint16_t)(s_wood + 3); found = 1; mark_tile(tx, ty); }
          }
          if (!found) set_act(i, A_IDLE);
        }
        break;
      case R_BUILDER:
        if (sb >= 0 && bsite(sb)) {
          if (++B[sb].st >= need_min(btype(sb))) {
            B[sb].tp &= (uint8_t)~SITE; B[sb].st = 0; changed = 1; sb = first_site();
            mark_tile(B[sb < 0 ? 0 : sb].x, B[sb < 0 ? 0 : sb].y);
          }
        } else set_act(i, A_IDLE);
        break;
      default: break;
    }
  }
  if (changed) { for (int b = 0; b < MAXB; b++) if (bused(b)) mark_tile(B[b].x, B[b].y); rebuild_homes(); assign_roles(); }

  // the fields grow by themselves, 4 minutes per timer unit
  if (m % 4 == 0)
    for (int b = 0; b < MAXB; b++)
      if (bdone(b, B_FIELD) && (B[b].st & 3) < 2) {
        uint8_t t = (uint8_t)((B[b].st >> 2) + 1);
        if (t >= 60) { B[b].st = (uint8_t)((B[b].st & 3) + 1); mark_tile(B[b].x, B[b].y); }
        else B[b].st = (uint8_t)((B[b].st & 3) | (t << 2));
      }

  // faith: awake adults believe, elders pray twice as much
  if (m % 30 == 0) {
    int f = 0;
    for (int i = 0; i < MAXV; i++)
      if (used(i) && vact(i) != A_SLEEP && vact(i) != A_BED) { if (vcls(i) == AGE_ADULT) f++; else if (vcls(i) == AGE_ELDER) f += 2; }
    s_faith = (uint16_t)(s_faith + f / 2 + (f & 1));
    if (s_faith > 999) s_faith = 999;
  }
  if (s_food > 999) s_food = 999;
  if (s_wood > 999) s_wood = 999;
  if (s_water > 250) s_water = 250;

  // morning: new roles; during the day: plan buildings, send idle people to their work
  if (!night) {
    if (s_woke) { s_woke = 0; assign_roles(); }
    if (m % 10 == 0 && s_last_plan != (uint8_t)(m / 10)) { s_last_plan = (uint8_t)(m / 10); if (plan()) assign_roles(); }
    for (int i = 0; i < MAXV; i++) {
      if (!used(i)) continue;
      if (vact(i) == A_IDLE && !(V[i].fl & F_WEAK) && vrole(i) != R_CHILD && vrole(i) != R_IDLE && job_key(i)) set_act(i, A_GO);
    }
  }
}

// ---------------------------------------------------------------- walking
static void arrive(int i) {
  switch (vact(i)) {
    case A_BED: set_act(i, A_SLEEP); if (s_home[i] < MAXB) V[i].fl |= F_INSIDE; mark_tile(V[i].x, V[i].y); break;
    case A_DRINK: set_thirst(i, 0); set_act(i, A_IDLE); break;
    case A_GO: set_act(i, vrole(i) == R_CARRIER ? A_RET : A_WORK); break;
    case A_RET: s_water = (uint8_t)(s_water > 242 ? 250 : s_water + 8); set_act(i, A_IDLE); break;
    default: break;
  }
}

static void move_group(int key, int steps) {
  static const int8_t D[4][2] = {{0, 1}, {0, -1}, {-1, 0}, {1, 0}};     // down, up, left, right
  for (int i = 0; i < MAXV; i++) {
    if (!used(i) || !is_moving(i) || key_of(i) != key) continue;
    for (int s = 0; s < steps + 1; s++) {
      int x = V[i].x, y = V[i].y, d = g_dist[y * WM + x];
      if (d == 0) { arrive(i); break; }
      if (d == 255) { set_act(i, A_IDLE); break; }
      if (s == steps) break;
      int best = -1;
      for (int k = 0; k < 5 && best < 0; k++) {
        int dd = (k == 0) ? (V[i].fl >> 6) : k - 1;
        if (k > 0 && dd == (V[i].fl >> 6)) continue;
        int nx = x + D[dd][0], ny = y + D[dd][1];
        if (nx >= 0 && ny >= 0 && nx < WM && ny < WM && g_dist[ny * WM + nx] == d - 1) best = dd;
      }
      if (best < 0) { set_act(i, A_IDLE); break; }
      mark_tile(x, y);
      V[i].x = (uint8_t)(x + D[best][0]); V[i].y = (uint8_t)(y + D[best][1]);
      V[i].fl = (uint8_t)((V[i].fl & 0x3F) | (best << 6));
      mark_tile(V[i].x, V[i].y);
    }
  }
}

void village_tick(uint32_t dt, int night) {
  (void)night;
  s_vacc += dt;
  s_anim_acc += dt;
  if (s_anim_acc >= ANIM_MS) {
    s_anim_acc = 0; s_vanim++;
    for (int i = 0; i < MAXV; i++) {
      if (!used(i)) continue;
      int a = vact(i);
      if (a == A_WORK || a == A_EAT || vrole(i) == R_CHILD || vrole(i) == R_ELDER) mark_tile(V[i].x, V[i].y);
    }
  }
  if (s_vacc < VSTEP_MS) return;
  int steps = (int)(s_vacc / VSTEP_MS);
  if (steps > 24) steps = 24;
  s_vacc = 0;
  // idle villagers and children fidget around their spot
  static const int8_t D[4][2] = {{0, 1}, {0, -1}, {-1, 0}, {1, 0}};
  for (int i = 0; i < MAXV; i++) {
    if (!used(i) || (vact(i) != A_IDLE) || (vr() & 15)) continue;
    int dd = (int)(vr() & 3), nx = V[i].x + D[dd][0], ny = V[i].y + D[dd][1];
    int dx = nx - cx(), dy = ny - cy();
    if (!world_walkable(nx, ny) || dx * dx + dy * dy > 64) continue;
    mark_tile(V[i].x, V[i].y);
    V[i].x = (uint8_t)nx; V[i].y = (uint8_t)ny; V[i].fl = (uint8_t)((V[i].fl & 0x3F) | (dd << 6));
    mark_tile(nx, ny);
  }
  int key = 0;
  for (int k = 0; k < MAXV && !key; k++) {
    int i = (s_rr + k) % MAXV;
    if (!used(i) || !is_moving(i)) continue;
    key = key_of(i);
    if (!key) set_act(i, A_IDLE);
    else s_rr = (uint8_t)(i + 1);
  }
  if (!key) return;
  flow_key(key);
  move_group(key, steps);
}

// ---------------------------------------------------------------- drawing
static void blit8(uint16_t* out, int y0, int x, int y, const uint8_t* ic) {
  for (int r = 0; r < 8; r++) {
    int py = y + r - y0;
    if (py < 0 || py >= GC_STRIP_H) continue;
    for (int c = 0; c < 8; c++) {
      uint8_t v = GC_RD8(ic + r * 4 + (c >> 1));
      uint8_t p = (c & 1) ? (v & 15) : (v >> 4);
      if (p && x + c >= 0 && x + c < GC_W) out[py * GC_W + x + c] = g_act[p];
    }
  }
}

static const uint8_t TUNIC[R_COUNT] = {4, 15, 3, 8, 12, 6, 13, 10, 9, 7};
static const int8_t TOOL[R_COUNT] = {-1, 0, 1, 2, 3, 4, 5, 6, -1, -1};     // GC_TOOLS cell by role

void village_draw(uint16_t* out, int wx0, int wy0) {
  // buildings
  for (int b = 0; b < MAXB; b++) {
    if (!bused(b)) continue;
    int tp = btype(b), site = (B[b].tp & SITE) != 0, w = bw(tp);
    int px = B[b].x * GC_TILE, py = B[b].y * GC_TILE;
    int pw = (tp == B_FISH ? 2 : w) * GC_TILE, ph = w * GC_TILE;
    if (py + ph <= wy0 || py >= wy0 + GC_STRIP_H || px + pw <= wx0 || px >= wx0 + GC_W) continue;
    for (int j = 0; j < (tp == B_FISH ? 1 : w); j++)
      for (int i = 0; i < (tp == B_FISH ? 2 : w); i++) {
        int cell;
        if (site) cell = (tp == B_FISH && i == 1) ? -1 : 1;
        else if (tp == B_TENT) cell = 0;
        else if (tp == B_WELL) cell = 2;
        else if (tp == B_HUT) cell = 3 + j * 2 + i;
        else if (tp == B_FIELD) cell = 7 + (B[b].st & 3);
        else cell = 10 + i;
        if (cell >= 0) blit_sprite(out, wy0, px + i * GC_TILE - wx0, py + j * GC_TILE, GC_VILLAGE(cell), 0, 0, g_wpal);
      }
  }
  // villagers
  uint8_t rm[16];
  for (int i = 0; i < MAXV; i++) {
    if (!used(i) || (V[i].fl & F_INSIDE)) continue;
    int sx = V[i].x * GC_TILE - wx0, sy = V[i].y * GC_TILE;
    if (sy + GC_TILE <= wy0 || sy - 10 >= wy0 + GC_STRIP_H || sx + 16 <= 0 || sx >= GC_W) continue;
    int role = vrole(i), act = vact(i), cls = vcls(i), dir = V[i].fl >> 6;
    for (int k = 0; k < 16; k++) rm[k] = (uint8_t)k;
    rm[9] = (uint8_t)((V[i].fl & F_WEAK) ? 14 : TUNIC[role]);
    rm[5] = (uint8_t)(cls == AGE_ELDER ? 13 : ((V[i].fl & F_FEM) ? 6 : 5));
    int frame = is_moving(i) ? 1 + ((V[i].x + V[i].y) & 1) : 0;
    int cell;
    if (cls == AGE_CHILD) cell = 9 + frame;
    else cell = (dir == 1 ? 3 : (dir >= 2 ? 6 : 0)) + frame;
    int bob = (cls == AGE_CHILD && act == A_IDLE && (s_vanim & 1)) ? -1 : 0;
    blit_sprite(out, wy0, sx, sy + bob, GC_VILLAGERS(cell), dir == 3, rm, g_act);
    if (act == A_SLEEP) { blit_sprite(out, wy0, sx, sy - 12, GC_ITEMS(6), 0, 0, g_act); continue; }
    if (act == A_WORK && !(V[i].fl & F_WEAK) && TOOL[role] >= 0) {
      int tb = (s_vanim & 1) ? 1 : -1;
      blit8(out, wy0, sx + (dir == 2 ? -5 : 11), sy + 5 + tb, GC_TOOLS(TOOL[role]));
    } else if (act == A_RET) {
      blit8(out, wy0, sx + (dir == 2 ? -5 : 11), sy + 7, GC_TOOLS(6));
    }
    if (act == A_EAT) blit8(out, wy0, sx + 4, sy - 7, GC_ICONS(1));
    else if (role == R_ELDER && act == A_WORK && (s_vanim & 1)) blit8(out, wy0, sx + 4, sy - 7, GC_ICONS(0));
  }
}

// ---------------------------------------------------------------- numbers for the HUD, queries and tests
int village_stat(int which) {
  switch (which) {
    case 0: return pop();
    case 1: return s_food;
    case 2: return s_wood;
    case 3: return s_faith;
    case 4: return s_water;
    case 5: { int n = 0; for (int i = 0; i < MAXV; i++) if (used(i) && (V[i].fl & F_WEAK)) n++; return n; }
    case 6: { int n = 0; for (int b = 0; b < MAXB; b++) if (bused(b) && !(B[b].tp & SITE)) n++; return n; }
    case 7: return count_sites();
    case 8: case 9: case 10: { int n = 0; for (int i = 0; i < MAXV; i++) if (used(i) && vcls(i) == which - 8) n++; return n; }
    case 11: return s_deaths;
    case 12: return s_births;
    case 13: return capacity();
    case 14: { int n = 0; for (int i = 0; i < MAXV; i++) if (used(i) && vact(i) == A_SLEEP) n++; return n; }
    default: break;
  }
  if (which >= 20 && which < 20 + R_COUNT) { int n = 0; for (int i = 0; i < MAXV; i++) if (used(i) && vrole(i) == which - 20) n++; return n; }
  if (which >= 40 && which < 40 + B_COUNT) return count_done(which - 40);
  if (which >= 60 && which < 60 + MAXV) { int i = which - 60; return used(i) ? (V[i].x | (V[i].y << 8) | (vact(i) << 16) | (vrole(i) << 20) | (hunger(i) << 24)) : -1; }
  return 0;
}

// ---------------------------------------------------------------- new game, save
void village_reset(int mix) {
  for (int i = 0; i < MAXV; i++) { V[i].fl = 0; V[i].ra = 0; s_home[i] = 255; }
  for (int b = 0; b < MAXB; b++) B[b].tp = 0xFF;
  s_rng = (uint32_t)mix * 2654435761u + 0x9e3779b9u;
  if (!s_rng) s_rng = 1;
  s_food = 18; s_wood = 12; s_faith = 0; s_water = 0; s_deaths = s_births = 0;
  s_vacc = s_anim_acc = 0; s_vanim = 0; s_rr = 0; s_woke = 0; s_last_plan = 255;
  int t = place(B_TENT, 0);
  (void)t;
  add_villager(AGE_ADULT, 0, 8, cx(), cy());
  add_villager(AGE_ADULT, 1, 8, cx(), cy());
  rebuild_homes();
  assign_roles();
}

// 128 bytes: [0] marker, [1] villagers (<= 16), [2] buildings (<= 18), food, wood, faith, water, then 4-byte villagers
// and 3-byte buildings. A full multi-slot save of every villager and building is release r6.
enum { PK_V = 16, PK_B = 18, PK_V0 = 10, PK_B0 = 10 + 4 * 16 };

void village_pack(uint8_t* p) {
  for (int i = 0; i < 128; i++) p[i] = 0;
  p[0] = 0x56;
  int nv = 0, nb = 0;
  for (int i = 0; i < MAXV && nv < PK_V; i++) {
    if (!used(i)) continue;
    uint8_t* q = p + PK_V0 + nv * 4;
    uint32_t a = (uint32_t)(V[i].x & 31) | ((uint32_t)(V[i].y & 31) << 5) | ((uint32_t)(V[i].fl & F_AGE) << 10) | ((V[i].fl & F_FEM) ? 1u << 12 : 0) | ((V[i].fl & F_WEAK) ? 1u << 13 : 0);
    q[0] = (uint8_t)a; q[1] = (uint8_t)(a >> 8); q[2] = V[i].age; q[3] = V[i].nd;
    nv++;
  }
  for (int b = 0; b < MAXB && nb < PK_B; b++) {
    if (!bused(b)) continue;
    uint8_t* q = p + PK_B0 + nb * 3;
    uint32_t a = (uint32_t)(B[b].x & 31) | ((uint32_t)(B[b].y & 31) << 5) | ((uint32_t)(btype(b) & 7) << 10) | ((B[b].tp & SITE) ? 1u << 13 : 0);
    q[0] = (uint8_t)a; q[1] = (uint8_t)(a >> 8); q[2] = B[b].st;
    nb++;
  }
  p[1] = (uint8_t)nv; p[2] = (uint8_t)nb;
  p[3] = (uint8_t)s_food; p[4] = (uint8_t)(s_food >> 8);
  p[5] = (uint8_t)s_wood; p[6] = (uint8_t)(s_wood >> 8);
  p[7] = (uint8_t)s_faith; p[8] = (uint8_t)(s_faith >> 8);
  p[9] = s_water;
}

void village_unpack(const uint8_t* p) {
  if (p[0] != 0x56 || p[1] > PK_V || p[2] > PK_B) { village_reset((int)g_world.start_x * 31 + g_world.start_y); return; }
  for (int i = 0; i < MAXV; i++) { V[i].fl = 0; V[i].ra = 0; s_home[i] = 255; }
  for (int b = 0; b < MAXB; b++) B[b].tp = 0xFF;
  s_rng = 0x2545F491u ^ ((uint32_t)p[3] << 8) ^ p[5];
  s_food = (uint16_t)(p[3] | (p[4] << 8)); s_wood = (uint16_t)(p[5] | (p[6] << 8)); s_faith = (uint16_t)(p[7] | (p[8] << 8)); s_water = p[9];
  if (s_food > 999) s_food = 999;
  if (s_wood > 999) s_wood = 999;
  if (s_faith > 999) s_faith = 999;
  s_deaths = s_births = 0; s_vacc = s_anim_acc = 0; s_vanim = 0; s_rr = 0; s_woke = 0; s_last_plan = 255;
  int nb = 0;
  for (int k = 0; k < p[2]; k++) {
    const uint8_t* q = p + PK_B0 + k * 3;
    uint32_t a = q[0] | (q[1] << 8);
    int x = a & 31, y = (a >> 5) & 31, tp = (a >> 10) & 7;
    if (tp >= B_COUNT) continue;
    int w = bw(tp);
    int ok = x >= 1 && y >= 1 && x + w < WM && y + w < WM;
    for (int j = 0; j < w && ok; j++) for (int i = 0; i < w; i++) if (MT_H(g_map[(y + j) * WM + x + i]) < 2 || MT_SOIL(g_map[(y + j) * WM + x + i]) == SOIL_BUILT) ok = 0;
    if (!ok || nb >= MAXB) continue;
    for (int j = 0; j < w; j++) for (int i = 0; i < w; i++) world_set_built(x + i, y + j, 1);
    B[nb].x = (uint8_t)x; B[nb].y = (uint8_t)y; B[nb].tp = (uint8_t)(tp | ((a >> 13) & 1 ? SITE : 0));
    B[nb].st = q[2];
    if (!(B[nb].tp & SITE) && tp == B_FIELD && (B[nb].st & 3) > 2) B[nb].st = 0;
    nb++;
  }
  for (int k = 0; k < p[1]; k++) {
    const uint8_t* q = p + PK_V0 + k * 4;
    uint32_t a = q[0] | (q[1] << 8);
    int x = a & 31, y = (a >> 5) & 31, cls = (a >> 10) & 3;
    if (cls > AGE_ELDER) cls = AGE_ADULT;
    if (!world_walkable(x, y)) { x = cx(); y = cy(); }
    int i = add_villager(cls, (a >> 12) & 1, q[2] & 31, x, y);
    if (i < 0) break;
    V[i].age = q[2];
    V[i].nd = q[3];
    if ((a >> 13) & 1) V[i].fl |= F_WEAK;
  }
  if (!pop()) village_reset((int)g_world.start_x * 31 + g_world.start_y);
  rebuild_homes();
  assign_roles();
}

void village_debug(int cmd, int32_t arg) {
  switch (cmd) {
    case 0: s_food = (uint16_t)(arg < 0 ? 0 : (arg > 999 ? 999 : arg)); break;
    case 1: s_wood = (uint16_t)(arg < 0 ? 0 : (arg > 999 ? 999 : arg)); break;
    case 2: {   // add an adult couple's worth of people: arg villagers
      for (int k = 0; k < arg; k++) add_villager(AGE_ADULT, k & 1, 8, cx(), cy());
      rebuild_homes(); assign_roles();
      break;
    }
    default: break;
  }
}
