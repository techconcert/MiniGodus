// The villagers of MiniGodus (release r3), the shaman who builds for them (r4), and weather, sickness and wood (r5). See village.h.
//
// Time is the real clock: village_minute() is called once for every minute that passes, so meals happen at 07:30,
// 12:30 and 19:00 and everybody sleeps in the clock's sleep schedule. Walking is real time: village_tick() moves the
// villagers along a distance field; everybody going to the same place shares one field, and one field is computed per
// step round, in the shared scratch buffer g_dist.
#include "village.h"
#include "village_art_ids.h"
#include "monument_art_ids.h"

// Vil.fl: bits 0-1 age class, 2 female, 3 used, 4 weak, 5 and Vil.ra bit 7: a mother's birth cooldown in days (0..3); bits 6-7 facing
enum { F_AGE = 3, F_FEM = 4, F_USED = 8, F_WEAK = 16 };
enum { VSTEP_MS = 90, ANIM_MS = 420 };
enum { K_CENTER = 100, K_WATER = 101, K_TREE = 102, K_SHORE = 103 };            // flow targets besides buildings (index + 1)
enum { SITE = 0x80 };                                                          // Bld.tp: bits 0-3 type, 4-5 variant (size), 7 = a building site, not yet finished

struct Vil { uint8_t x, y, fl, ra, age, nd; };   // tile, flags, role | act << 4, age in days | lifespan variant << 5, hunger | thirst << 3
struct Bld { uint8_t x, y, tp, st; };            // tile, type | variant << 4 | SITE, progress / crop stage / animals in a pen
static_assert(sizeof(Vil) == 6, "a villager is 6 bytes");

static Vil      V[MAXV];
static Bld      B[MAXB];
static uint8_t  s_home[MAXV];                    // building index of each villager's bed, 255 = none
static uint16_t s_food, s_wood, s_faith, s_deaths, s_births;
static uint8_t  s_sick;                                // somebody has fallen sick in this world: an infirmary is wanted
static uint8_t  s_ill[MAXV];                           // r5: bit 7 sick, bits 5-6 days sick, bit 4 a cold night without fire, bits 0-3 exposure points today
static uint16_t s_sick_deaths, s_onsets, s_felled;
static uint32_t s_crop_u, s_crop_n, s_thirst_n, s_burned;      // counters for the tests: crop growth units and chances, thirst steps, wood burnt
static uint8_t  s_hot, s_cold, s_rainy, s_storm, s_snowing, s_fog_now, s_cond, s_prev_night, s_night_now;   // today's conditions, from the weather
static uint8_t  s_fire[(MAXB + 7) / 8];                // homes with a fire lit tonight (chimney smoke)
static uint8_t  s_src, s_content, s_roles_dirty;      // food sources today (bits: grain, fruit, animal, fish), content today, roles need a refresh
static uint8_t  s_water, s_vanim, s_rr, s_woke, s_last_plan;
static uint32_t s_vacc, s_anim_acc, s_rng, s_earned;
// r6: the arc of the world (phase, monument pace, statistics for the summary), fish stocks, animals
static uint8_t  s_phase, s_beat, s_peak, s_lean, s_fishn;
static uint16_t s_mon_day, s_built_tot, s_pen_dead, s_pen_born, s_fish_caught, s_settlers;
static uint16_t s_fish_a[8];                           // fish stock of each water body: the lowest tile number of the body ...
static uint8_t  s_fish_s[8];                           // ... and its stock
enum { MON_MIN_DAY = 21, MON_GAP_DAYS = 2, WORSHIP_END = 20 * 60, SETTLER_EVERY = 3, SETTLER_MAX_POP = 36 };
static int mon_cost(int stage) { static const uint16_t C[6] = {0, 1300, 2200, 3200, 4500, 6500}; return C[stage < 0 ? 0 : (stage > 5 ? 5 : stage)]; }

static uint32_t vr(void) { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }

static int used(int i) { return (V[i].fl & F_USED) != 0; }
static int vrole(int i) { return V[i].ra & 15; }
static int vact(int i) { return (V[i].ra >> 4) & 7; }
static void set_role(int i, int r) { V[i].ra = (uint8_t)((V[i].ra & 0xF0) | r); }
static void set_act(int i, int a) { V[i].ra = (uint8_t)((V[i].ra & 0x8F) | (a << 4)); }
static int cool(int i) { return ((V[i].fl >> 5) & 1) | ((V[i].ra >> 7) << 1); }
static void set_cool(int i, int c) { V[i].fl = (uint8_t)((V[i].fl & ~32) | ((c & 1) << 5)); V[i].ra = (uint8_t)((V[i].ra & 0x7F) | ((c >> 1) << 7)); }
static int vcls(int i) { return V[i].fl & F_AGE; }
static int sick(int i) { return (s_ill[i] & 0x80) != 0; }
static int sick_days(int i) { return (s_ill[i] >> 5) & 3; }
static int fire_at(int b) { return (s_fire[b >> 3] >> (b & 7)) & 1; }
static int hunger(int i) { return V[i].nd & 7; }
static int thirst(int i) { return V[i].nd >> 3; }
static void set_hunger(int i, int h) { V[i].nd = (uint8_t)((V[i].nd & 0xF8) | (h > 7 ? 7 : h)); }
static void set_thirst(int i, int t) { V[i].nd = (uint8_t)((V[i].nd & 7) | ((t > 31 ? 31 : t) << 3)); }
// a drink ends the weakness of thirst at once (hunger weakness ends at the next meal)
static void drink(int i) {
  set_thirst(i, 0);
  if (hunger(i) < HUNGER_WEAK) V[i].fl &= (uint8_t)~F_WEAK;
}

static int life(int i) { int v = V[i].age >> 5; return 20 + v + (v == 7 ? 1 : 0); }     // adult lifespan, 20..28 days
static int days(int i) { return V[i].age & 31; }
static int is_moving(int i) { int a = vact(i); return a == A_GO || a == A_BED || a == A_DRINK || a == A_RET; }

static int cx(void) { return g_world.start_x; }
static int cy(void) { return g_world.start_y; }

static int btype(int b) { return B[b].tp & 15; }
static int bvar(int b) { return (B[b].tp >> 4) & 3; }
static int bused(int b) { return B[b].tp != 0xFF; }
static int bsite(int b) { return bused(b) && (B[b].tp & SITE); }
static int bdone(int b, int tp) { return bused(b) && !(B[b].tp & SITE) && btype(b) == tp; }
static int mon_b(void) { for (int b = 0; b < MAXB; b++) if (bused(b) && !(B[b].tp & SITE) && btype(b) == B_MONUMENT) return b; return -1; }
int village_phase(void) { return s_phase; }
int village_mon_stage(void) { int b = mon_b(); return b < 0 ? 0 : B[b].st; }
int village_mon_type(void) { uint32_t h = G.s.seed * 0x9E3779B1u; h ^= h >> 15; h *= 0x85EBCA6Bu; h ^= h >> 13; return (int)((h >> 4) % 5); }
int village_mon_pos(int* x, int* y) { int b = mon_b(); if (b < 0) return 0; *x = B[b].x; *y = B[b].y; return 1; }
static const uint8_t NVAR[B_COUNT] = {1, 1, 2, 1, 4, 2, 3, 4, 1, 1, 1, 1, 2, 1};
static void bdim(int tp, int var, int* w, int* h) {
  int a = 1, b = 1;
  switch (tp) {
    case B_HUT: case B_LUMBER: case B_GRANARY: case B_SHRINE: a = b = 2; break;
    case B_COTTAGE: case B_ORCHARD: a = (var & 1) ? 2 : 3; b = (var & 1) ? 3 : 2; break;
    case B_MANOR: case B_MONUMENT: a = b = 3; break;
    case B_FIELD: a = 2 + (var & 1); b = 2 + ((var >> 1) & 1); break;
    case B_PEN: if (var == 0) { a = 3; b = 2; } else if (var == 1) { a = 2; b = 3; } else a = b = 3; break;
    case B_FISH: if (var & 1) { a = 1; b = 2; } else { a = 2; b = 1; } break;
    case B_INFIRM: a = var ? 3 : 2; b = 2; break;
    default: break;
  }
  *w = a; *h = b;
}
static void bdims(int b, int* w, int* h) { bdim(btype(b), bvar(b), w, h); }
static int cap_of(int tp) { static const uint8_t C[4] = {2, 4, 6, 8}; return tp < 4 ? C[tp] : 0; }
static int need_min(int tp) { static const uint8_t N[B_COUNT] = {20, 60, 90, 140, 15, 40, 40, 45, 40, 50, 60, 70, 60, 1}; return N[tp]; }
static int cost_wood(int tp) { static const uint8_t C[B_COUNT] = {4, 10, 16, 26, 2, 8, 10, 8, 6, 8, 12, 14, 12, 0}; return C[tp]; }
static int unlock_pop(int tp) { static const uint8_t U[B_COUNT] = {0, 6, 14, 24, 0, 8, 10, 0, 0, 4, 10, 6, 0, 28}; return U[tp]; }
static int is_home(int tp) { return tp <= B_MANOR; }
static int is_crop(int tp) { return tp == B_FIELD || tp == B_ORCHARD; }
// building codes in a save: type and variant in 5 bits
static int kind_code(int tp, int var) { int c = 0; for (int t = 0; t < tp; t++) c += NVAR[t]; return c + var; }
static int kind_decode(int code, int* tp, int* var) { for (int t = 0; t < B_COUNT; t++) { if (code < NVAR[t]) { *tp = t; *var = code; return 1; } code -= NVAR[t]; } return 0; }

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
static int nth_crop(int n) {                 // fields and orchards, cyclic
  int c = count_done(B_FIELD) + count_done(B_ORCHARD);
  if (!c) return -1;
  n %= c;
  for (int b = 0; b < MAXB; b++) if (bused(b) && !(B[b].tp & SITE) && is_crop(btype(b)) && n-- == 0) return b;
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
void village_mark_tile(int tx, int ty) { mark_tile(tx, ty); }

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
static int occupied_homes(void) {          // homes with somebody's bed in them
  int n = 0;
  for (int b = 0; b < MAXB; b++) {
    if (!bused(b) || (B[b].tp & SITE) || !is_home(btype(b))) continue;
    for (int i = 0; i < MAXV; i++) if (used(i) && s_home[i] == b) { n++; break; }
  }
  return n;
}
static int capacity(void) { int n = 0; for (int b = 0; b < MAXB; b++) if (bused(b) && !(B[b].tp & SITE)) n += cap_of(btype(b)); return n; }

// ---------------------------------------------------------------- walking targets
static void seed_around(int b) {
  int w, h, x0 = B[b].x, y0 = B[b].y;
  bdims(b, &w, &h);
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) {
      int x = x0 + i, y = y0 + j;
      if (i == 0) world_flow_seed(x - 1, y, 0);
      if (i == w - 1) world_flow_seed(x + 1, y, 0);
      if (j == 0) world_flow_seed(x, y - 1, 0);
      if (j == h - 1) world_flow_seed(x, y + 1, 0);
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
  if (s_phase == PH_WORSHIP) { int b = mon_b(); return b < 0 ? K_CENTER : b + 1; }          // the worship day: everybody gathers at the monument
  if (sick(i)) { int b = nth_bld(B_INFIRM, 0); return b < 0 ? 0 : b + 1; }          // the sick go to the infirmary, if there is one
  switch (vrole(i)) {
    case R_FARMER: { int b = nth_crop(rank_of(i)); return b < 0 ? 0 : b + 1; }
    case R_HERDER: { int b = nth_bld(B_PEN, rank_of(i)); return b < 0 ? 0 : b + 1; }
    case R_FISHER: { int b = nth_bld(B_FISH, rank_of(i)); return b < 0 ? K_SHORE : b + 1; }
    case R_LUMBER: return K_TREE;
    case R_BUILDER: { int b = first_site(); return b < 0 ? 0 : b + 1; }
    case R_CARRIER: return K_WATER;
    case R_ELDER: { int b = nth_bld(B_SHRINE, rank_of(i)); return b < 0 ? K_CENTER : b + 1; }
    case R_HEALER: { int b = nth_bld(B_INFIRM, 0); return b < 0 ? K_CENTER : b + 1; }
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

static void kill(int i);
#include "shaman.inc.h"
#include "nature.inc.h"

// ---------------------------------------------------------------- roles
static void assign_roles(void) {
  int p = pop(), weak = 0, adults = 0, ill = 0;
  for (int i = 0; i < MAXV; i++) if (used(i)) { if (V[i].fl & F_WEAK) weak++; if (sick(i)) ill++; if (vcls(i) == AGE_ADULT) adults++; }
  int food_low = s_food < p * 6;
  int want[R_COUNT];
  for (int r = 0; r < R_COUNT; r++) want[r] = 0;
  int sites = count_sites();
  want[R_BUILDER] = sites ? (sites >= 2 && adults >= 6 ? 2 : 1) : 0;
  want[R_FARMER] = count_done(B_FIELD) + count_done(B_ORCHARD);
  want[R_HERDER] = count_done(B_PEN);
  want[R_FISHER] = count_done(B_FISH);
  want[R_LUMBER] = s_wood < 8 && adults >= 5 ? 2 : (s_wood < 24 ? 1 : 0);
  int firewood = s_cold && s_wood < (uint16_t)(occupied_homes() * 4 + 20);      // cold: the lumberjacks come first
  if (firewood) { int w = 1 + (adults >= 6) + (adults >= 14); if (w > want[R_LUMBER]) want[R_LUMBER] = w; }
  want[R_CARRIER] = p >= 4 ? 1 + (s_hot && p >= 6) + (s_hot && p >= 14) : 0;      // heat: thirst doubles, more carriers
  want[R_HEALER] = (ill > 0 || weak > 0 || (p >= 12 && count_done(B_INFIRM))) ? 1 + (ill >= 4) : 0;
  static const uint8_t ORDER_FOOD[7] = {R_FARMER, R_HERDER, R_FISHER, R_BUILDER, R_LUMBER, R_CARRIER, R_HEALER};
  static const uint8_t ORDER_OK[7] = {R_BUILDER, R_FARMER, R_HERDER, R_FISHER, R_LUMBER, R_CARRIER, R_HEALER};
  static const uint8_t ORDER_COLD[7] = {R_LUMBER, R_BUILDER, R_FARMER, R_HERDER, R_FISHER, R_CARRIER, R_HEALER};
  const uint8_t* order = food_low ? ORDER_FOOD : (firewood ? ORDER_COLD : ORDER_OK);
  uint8_t ord[7];
  for (int k = 0; k < 7; k++) ord[k] = order[k];
  if (ill > 0 && count_done(B_INFIRM)) {            // the sick are waiting: the healer is hired first
    int at = 0;
    while (at < 6 && ord[at] != R_HEALER) at++;
    for (; at > 0; at--) ord[at] = ord[at - 1];
    ord[0] = R_HEALER;
  }
  order = ord;
  uint8_t taken[MAXV];
  for (int i = 0; i < MAXV; i++) taken[i] = 0;
  for (int i = 0; i < MAXV; i++) {
    if (!used(i)) continue;
    if (vcls(i) == AGE_CHILD) { set_role(i, R_CHILD); taken[i] = 1; }
    else if (vcls(i) == AGE_ELDER) { set_role(i, R_ELDER); taken[i] = 1; }
    else if ((V[i].fl & F_WEAK) || sick(i)) { set_role(i, R_IDLE); taken[i] = 1; }
  }
  for (int i = 0; i < MAXV; i++) if (used(i) && sick(i) && vcls(i) != AGE_ADULT) taken[i] = 1;
  int rot = (int)(vr() % MAXV);
  for (int k = 0; k < 7; k++) {
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
  if (s_phase == PH_WORSHIP) for (int i = 0; i < MAXV; i++) if (used(i)) set_role(i, R_ELDER);      // everybody prays
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
    s_ill[i] = 0;
    V[i].ra = (uint8_t)(age_class == AGE_CHILD ? R_CHILD : R_IDLE);
    return i;
  }
  return -1;
}

static void kill(int i) {
  V[i].fl = 0; s_ill[i] = 0;
  s_deaths++;
  mark_tile(V[i].x, V[i].y);
}

// A walkable tile beside building h (anywhere around it), else the village green
static void adj_spot(int h, int* x, int* y) {
  *x = cx(); *y = cy();
  if (h < MAXB && bused(h)) {
    int w, hh;
    bdims(h, &w, &hh);
    for (int k = 0; k < 2 * (w + hh) + 4; k++) {
      int xx, yy;
      if (k < w) { xx = B[h].x + k; yy = B[h].y + hh; }
      else if (k < w + hh) { xx = B[h].x - 1; yy = B[h].y + k - w; }
      else if (k < 2 * w + hh) { xx = B[h].x + k - w - hh; yy = B[h].y - 1; }
      else { xx = B[h].x + w; yy = B[h].y + k - 2 * w - hh; }
      if (world_walkable(xx, yy)) { *x = xx; *y = yy; return; }
    }
  }
}
static void home_spot(int i, int* x, int* y) { adj_spot(s_home[i], x, y); }

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
  if (pop() > 0 && pop() <= 6 && s_food < (uint16_t)(pop() * 3)) s_food = (uint16_t)(s_food + 2 * pop());      // a small band forages berries and roots when the stores run dry
  // the monument is finished: today is the worship day
  if (s_phase == PH_PLAY && village_mon_stage() >= 5 && G.s.day > s_mon_day) { s_phase = PH_WORSHIP; s_roles_dirty = 1; }
  // food variety: three or more sources eaten in a day make people content (+25% faith) for the rest of that day
  s_src = 0; s_content = 0;
  // animals: they breed when they are fed (a herder tends them and the stores are not empty), a lean spell starves one animal per pen
  // and day, and they die of old age (3% per animal and day); a pen that lost its pair gets a wild pair when things are good again
  int herders = 0;
  for (int i = 0; i < MAXV; i++) if (used(i) && vrole(i) == R_HERDER) herders++;
  {
    int pp = pop(), fed = s_food >= (uint16_t)(pp * 3);
    if (s_food < (uint16_t)(pp * 2)) { if (s_lean < 255) s_lean++; } else s_lean = 0;
    for (int b = 0; b < MAXB; b++)
      if (bdone(b, B_PEN)) {
        int n = B[b].st;
        if (n > 0 && s_lean >= 2) { n--; s_pen_dead++; }
        if (n > 0 && (int)(vr() % 100) < n * 3) { n--; s_pen_dead++; }
        if (n >= 2 && n < 10 && fed && herders) { n++; s_pen_born++; }
        else if (n < 2 && fed && herders && G.s.day % 5 == 0) { n += 2; s_pen_born += 2; }
        B[b].st = (uint8_t)n;
        mark_tile(B[b].x, B[b].y);
      }
  }
  fish_day();
  conditions();
  sickness_day();
  nature_day();
  // Couples: the k-th healthy woman and the k-th healthy man of working age. A couple has a child about every 3 days
  // (the woman's cooldown), when there is a free bed and two days of food to spare.
  for (int i = 0; i < MAXV; i++) if (used(i) && cool(i) > 0) set_cool(i, cool(i) - 1);
  for (int k = 0;; k++) {
    int w = -1, m = -1, n;
    n = k;
    for (int i = 0; i < MAXV && w < 0; i++) if (used(i) && vcls(i) == AGE_ADULT && (V[i].fl & F_FEM) && !(V[i].fl & F_WEAK) && !sick(i) && n-- == 0) w = i;
    n = k;
    for (int i = 0; i < MAXV && m < 0; i++) if (used(i) && vcls(i) == AGE_ADULT && !(V[i].fl & F_FEM) && !(V[i].fl & F_WEAK) && !sick(i) && n-- == 0) m = i;
    if (w < 0 || m < 0) break;
    int p = pop();
    if (cool(w) > 0 || p >= MAXV || p >= capacity() || s_food < (uint16_t)(p * 6)) continue;
    int nf = 0, nm = 0;
    for (int j = 0; j < MAXV; j++) if (used(j)) { if (V[j].fl & F_FEM) nf++; else nm++; }
    int i = add_villager(AGE_CHILD, nf < nm ? 1 : (nf > nm ? 0 : (int)(vr() & 1)), 0, cx(), cy());      // the sexes stay balanced, so there are couples
    if (i < 0) break;
    rebuild_homes();
    int x, y;
    home_spot(i, &x, &y);
    V[i].x = (uint8_t)x; V[i].y = (uint8_t)y;
    V[i].age = (uint8_t)(V[i].age & 0xE0);
    set_cool(w, 3);
    s_births++;
    mark_tile(x, y);
  }
  // settlers: a young couple walks in when there is room and the stores are full (the town needs more hands than births give)
  {
    int p = pop();
    if (s_phase == PH_PLAY && G.s.day >= 2 && G.s.day % SETTLER_EVERY == 0 && p >= 2 && p + 2 <= MAXV && p + 2 <= capacity() && p < SETTLER_MAX_POP && s_food >= (uint16_t)(p * 8 + 20)) {
      for (int k = 0; k < 2; k++) if (add_villager(AGE_ADULT, k, 8, cx(), cy()) >= 0) s_settlers++;
      rebuild_homes();
      s_roles_dirty = 1;
      mark_tile(cx(), cy());
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
    int a = vact(i);
    if (!used(i) || ((a == A_SLEEP || a == A_BED) && !(s_storm && !s_night_now))) continue;      // a storm keeps people home, but they eat there
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

// The arc: statistics, the end of the village, the end of the worship day. Run every minute, and once after a clock jump that played no
// minutes (so a jump over the end of the worship day still ends it, and a dying village still counts down).
void village_arc(int m) {
  int p = pop();
  if (p > s_peak) s_peak = (uint8_t)p;
  if (!p && s_phase <= PH_WORSHIP) { s_phase = PH_DYING; s_beat = 3; }
  else if (s_phase == PH_DYING && s_beat && --s_beat == 0) s_phase = PH_EXTINCT;
  if (s_phase == PH_WORSHIP && m >= WORSHIP_END) s_phase = PH_DONE;
}

void village_minute(int m, int night) {
  int changed = 0;
  if (s_phase >= PH_DONE) return;                              // the world has ended: the summary is on screen, nothing moves any more
  G.serial++;                                                  // the saved state changes every minute: it is written every 20 minutes
  conditions();
  s_night_now = (uint8_t)night;
  int stay = night || s_storm;                                  // asleep, or sheltering from a storm
  if (night != s_prev_night) { if (night && s_prev_night != 255) night_begin(); s_prev_night = (uint8_t)night; }
  if (!night && m == 9 * 60) for (int k = 0; k < (int)sizeof(s_fire); k++) s_fire[k] = 0;      // the fires burn down in the morning
  // eating takes one minute
  for (int i = 0; i < MAXV; i++) if (used(i) && vact(i) == A_EAT) { set_act(i, A_IDLE); mark_tile(V[i].x, V[i].y); }

  // sleep and wake up on the clock's schedule
  for (int i = 0; i < MAXV; i++) {
    if (!used(i)) continue;
    int a = vact(i);
    if (stay) {
      if (a != A_BED && a != A_SLEEP) set_act(i, A_BED);
    } else if (a == A_BED || a == A_SLEEP) {
      set_act(i, A_IDLE); s_woke = 1; mark_tile(V[i].x, V[i].y);
    }
  }
  rebuild_homes();
  // terraforming or a new building may have taken the ground from under somebody
  for (int i = 0; i < MAXV; i++)
    if (used(i) && !world_walkable(V[i].x, V[i].y)) {
      int x, y;
      if (!world_nearest_walkable(V[i].x, V[i].y, &x, &y)) { x = cx(); y = cy(); }
      mark_tile(V[i].x, V[i].y); V[i].x = (uint8_t)x; V[i].y = (uint8_t)y; mark_tile(x, y);
      if (is_moving(i)) set_act(i, A_IDLE);
    }

  if (meal_minute(m)) meal(m);

  // exposure: rain on people outside, cold on people outside
  if (m % 30 == 0 && (s_rainy || s_storm))
    for (int i = 0; i < MAXV; i++) { int a = vact(i); if (used(i) && (a == A_GO || a == A_WORK || a == A_DRINK || a == A_RET) && (s_ill[i] & 15) < 15) s_ill[i]++; }
  if (m % 60 == 0 && s_cold)
    for (int i = 0; i < MAXV; i++) { int a = vact(i); if (used(i) && (a == A_GO || a == A_WORK || a == A_DRINK || a == A_RET) && (s_ill[i] & 15) < 15) s_ill[i]++; }

  // thirst: every 20 minutes awake (every 10 above 28 C), every hour asleep
  if (m % (s_hot ? 10 : 20) == 0)
    for (int i = 0; i < MAXV; i++) {
      if (!used(i) || ((vact(i) == A_SLEEP) && m % 60 != 0)) continue;
      set_thirst(i, thirst(i) + 1); s_thirst_n++;
      if (thirst(i) >= 28) V[i].fl |= F_WEAK;
      if (thirst(i) >= 31 && m % 60 == 0) { set_hunger(i, hunger(i) + 1); if (hunger(i) >= HUNGER_DEATH + 1) kill(i); }
    }
  for (int i = 0; i < MAXV; i++) {
    if (!used(i) || thirst(i) < 12) continue;
    int a = vact(i);
    if (a == A_EAT || a == A_DRINK || a == A_RET) continue;
    if ((a == A_BED || a == A_SLEEP) && !(s_storm && !night && s_water > 0)) continue;      // at home in a storm they drink from the jugs
    if (s_water > 0) { s_water--; drink(i); }
    else set_act(i, A_DRINK);
  }

  // work
  int sb = first_site();
  for (int i = 0; i < MAXV; i++) {
    if (!used(i) || vact(i) != A_WORK || (V[i].fl & F_WEAK) || sick(i)) continue;
    switch (vrole(i)) {
      case R_FARMER: {
        int b = nth_crop(rank_of(i));
        if (b >= 0 && (B[b].st & 3) == 2) {
          s_food = (uint16_t)(s_food + 8); B[b].st = 0; mark_tile(B[b].x, B[b].y);
          s_src |= btype(b) == B_ORCHARD ? 2 : 1;
        }
        break;
      }
      case R_HERDER: {
        int b = nth_bld(B_PEN, rank_of(i));
        if (b >= 0 && m % 15 == 0 && B[b].st >= 2) { s_food = (uint16_t)(s_food + 1 + B[b].st / 4); s_src |= 4; }
        else if (b < 0) set_act(i, A_IDLE);
        break;
      }
      case R_FISHER:
        if (m % 15 == 0 && fish_take(V[i].x, V[i].y)) { s_food = (uint16_t)(s_food + (count_done(B_FISH) ? 3 : 2) + (s_rainy ? 1 : 0)); s_src |= 8; }      // fish bite better in the rain, and only while the water has fish
        break;
      case R_LUMBER:
        if (m % 8 == 0 && !lumberjack_work(V[i].x, V[i].y)) set_act(i, A_IDLE);
        break;
      case R_BUILDER:
        if (sb >= 0 && bsite(sb)) {
          if (++B[sb].st >= need_min(btype(sb))) {
            B[sb].tp &= (uint8_t)~SITE; B[sb].st = btype(sb) == B_PEN ? 2 : 0; changed = 1; s_built_tot++; sb = first_site();
            mark_tile(B[sb < 0 ? 0 : sb].x, B[sb < 0 ? 0 : sb].y);
          }
        } else set_act(i, A_IDLE);
        break;
      default: break;
    }
  }
  if (changed) { for (int b = 0; b < MAXB; b++) if (bused(b)) mark_tile(B[b].x, B[b].y); rebuild_homes(); assign_roles(); }
  // a lumberjack who cannot reach any tree gathers brushwood and fallen boughs instead: a little wood, never none
  if (m % 30 == 0 && !night && !s_storm)
    for (int i = 0; i < MAXV; i++) { int a = vact(i); if (used(i) && vrole(i) == R_LUMBER && (a == A_IDLE || a == A_GO) && !(V[i].fl & F_WEAK) && !sick(i)) s_wood++; }

  // the fields grow by themselves, 4 minutes per timer unit: half as fast again in rain, not at all under snow or frost
  if (m % 4 == 0) {
    int frozen = s_snowing || s_snow_lvl > 0 || world_temp10() < 0;
    for (int b = 0; b < MAXB; b++)
      if (bused(b) && !(B[b].tp & SITE) && is_crop(btype(b)) && (B[b].st & 3) < 2) {
        s_crop_n++;
        if (frozen) continue;
        int add = 1 + ((s_rainy || s_storm) && m % 8 == 0 ? 1 : 0);
        s_crop_u += (uint32_t)add;
        int t = (B[b].st >> 2) + add;
        if (t >= 60) { B[b].st = (uint8_t)((B[b].st & 3) + 1); mark_tile(B[b].x, B[b].y); }
        else B[b].st = (uint8_t)((B[b].st & 3) | (t << 2));
      }
  }
  snow_minute(m);
  nature_minute();

  // faith: awake adults believe, elders pray twice as much
  if (m % 30 == 0) {
    int f = 0, shrine = count_done(B_SHRINE) > 0;
    for (int i = 0; i < MAXV; i++)
      if (used(i) && vact(i) != A_SLEEP && vact(i) != A_BED) { if (vcls(i) == AGE_ADULT) f++; else if (vcls(i) == AGE_ELDER) f += (shrine && vact(i) == A_WORK) ? 3 : 2; }
    int add = f / 2 + (f & 1) + (shrine ? 1 : 0);
    { int n = 0; for (int k = 0; k < 4; k++) if (s_src & (1 << k)) n++; s_content = n >= 3; }       // three sources eaten so far today
    if (s_content) add += (add + 3) / 4;
    int before = s_faith;
    s_faith = (uint16_t)(s_faith + add);
    if (s_faith > 9999) s_faith = 9999;
    s_earned += (uint32_t)(s_faith - before);
  }
  { int p = pop(), cap = (p * 8 > 150 ? p * 8 : 150) + 100 * count_done(B_GRANARY); if (cap > 999) cap = 999; if (s_food > cap) s_food = (uint16_t)cap; }
  if (s_wood > 999) s_wood = 999;
  if (s_water > 250) s_water = 250;

  // morning: new roles; during the day: plan buildings, send idle people to their work
  if (!night) {
    if (s_woke) { s_woke = 0; assign_roles(); }
    if (m % 10 == 0 && s_last_plan != (uint8_t)(m / 10)) { s_last_plan = (uint8_t)(m / 10); if (s_phase == PH_PLAY) shaman_plan(); }
    if (s_roles_dirty) { s_roles_dirty = 0; assign_roles(); }
    for (int i = 0; i < MAXV; i++) {
      if (!used(i)) continue;
      if (s_phase == PH_WORSHIP) { if (vact(i) == A_IDLE && job_key(i)) set_act(i, A_GO); continue; }      // everybody walks to the monument, weak or not
      if (vact(i) == A_IDLE && !(V[i].fl & F_WEAK) && (sick(i) || (vrole(i) != R_CHILD && vrole(i) != R_IDLE)) && !s_storm && job_key(i)) set_act(i, A_GO);
    }
  }
  village_arc(m);
}

// ---------------------------------------------------------------- walking
static void arrive(int i) {
  switch (vact(i)) {
    case A_BED: set_act(i, A_SLEEP); mark_tile(V[i].x, V[i].y); break;
    case A_DRINK: drink(i); set_act(i, A_IDLE); break;
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
        if (nx >= 0 && ny >= 0 && nx < WM && ny < WM && g_dist[ny * WM + nx] == d - 1 && world_step_ok(x, y, nx, ny)) best = dd;
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
      if (a == A_WORK || a == A_EAT || vrole(i) == R_CHILD || vrole(i) == R_ELDER || sick(i)) mark_tile(V[i].x, V[i].y);
    }
    for (int b = 0; b < MAXB; b++) if (bused(b) && fire_at(b)) mark_tile(B[b].x, B[b].y - 1);      // chimney smoke
    { int mb = mon_b(); if (mb >= 0 && B[mb].st >= 5 && s_phase <= PH_WORSHIP) for (int j = 0; j < 3; j++) mark_tile(B[mb].x + 1, B[mb].y + j); }      // the finished monument glows
  }
  if (s_vacc < VSTEP_MS) return;
  int steps = (int)(s_vacc / VSTEP_MS);
  if (steps > 24) steps = 24;
  s_vacc = 0;
  // idle villagers and children fidget around their spot
  static const int8_t D[4][2] = {{0, 1}, {0, -1}, {-1, 0}, {1, 0}};
  for (int i = 0; i < MAXV; i++) {
    if (!used(i) || (vr() & 15)) continue;
    int worship = s_phase == PH_WORSHIP && vact(i) == A_WORK;      // the crowd at the monument shuffles about it
    if (vact(i) != A_IDLE && !worship) continue;
    int dd = (int)(vr() & 3), nx = V[i].x + D[dd][0], ny = V[i].y + D[dd][1];
    int mbx = cx(), mby = cy();
    if (worship) { int mb = mon_b(); if (mb >= 0) { mbx = B[mb].x + 1; mby = B[mb].y + 1; } }
    int dx = nx - mbx, dy = ny - mby;
    if (!world_walkable(nx, ny) || !world_step_ok(V[i].x, V[i].y, nx, ny) || dx * dx + dy * dy > (worship ? 14 : 64)) continue;
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
  // cut ramps
  for (int k = 0; k < 10; k++) {
    if (s_ramp[k] == 0xFFFF) continue;
    int px = (s_ramp[k] & 31) * GC_TILE, py = (s_ramp[k] >> 5) * GC_TILE;
    if (py + GC_TILE <= wy0 || py >= wy0 + GC_STRIP_H || px + GC_TILE <= wx0 || px >= wx0 + GC_W) continue;
    blit_sprite(out, wy0, px - wx0, py, GC_VILLAGE(VA_RAMP), 0, 0, g_wpal);
  }
  draw_forest_ground(out, wx0, wy0);
  // buildings
  for (int b = 0; b < MAXB; b++) {
    if (!bused(b)) continue;
    int tp = btype(b), var = bvar(b), site = (B[b].tp & SITE) != 0, w, h;
    bdims(b, &w, &h);
    int px = B[b].x * GC_TILE, py = B[b].y * GC_TILE;
    if (py + h * GC_TILE <= wy0 || py >= wy0 + GC_STRIP_H || px + w * GC_TILE <= wx0 || px >= wx0 + GC_W) continue;
    if (tp == B_MONUMENT) {                      // 3x3 tiles of art: the stage and the type of this world
      int st = B[b].st, mt = village_mon_type();
      for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
          gc_cell c = st == 0 ? GC_VILLAGE(VA_SITE) : (st == 1 ? GC_MONUMENT(MA_STAGE1 + j * 3 + i) : GC_MONUMENT(MA_BASE + (mt * 4 + st - 2) * 9 + j * 3 + i));
          blit_sprite(out, wy0, px + i * GC_TILE - wx0, py + j * GC_TILE, c, 0, 0, g_wpal);
        }
      continue;
    }
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) {
        int cell = -1;
        if (site) {
          int stage = B[b].st * 3 / need_min(tp);
          cell = VA_SITE + (stage > 2 ? 2 : stage);
          if (tp == B_FISH && ((var & 1) ? j : i) == ((var & 2) ? 0 : 1)) cell = -1;     // the pier is over water
        } else switch (tp) {
          case B_TENT: cell = VA_TENT; break;
          case B_WELL: cell = VA_WELL; break;
          case B_HUT: cell = VA_HUT + j * 2 + i; break;
          case B_COTTAGE: cell = (var & 1 ? VA_COTTAGE_T : VA_COTTAGE_W) + j * w + i; break;
          case B_MANOR: cell = VA_MANOR + j * 3 + i; break;
          case B_LUMBER: cell = VA_LUMBER + j * 2 + i; break;
          case B_GRANARY: cell = VA_GRANARY + j * 2 + i; break;
          case B_SHRINE: cell = VA_SHRINE + j * 2 + i; break;
          case B_INFIRM: cell = (var ? VA_INFIRM_W : VA_INFIRM) + j * w + i; break;
          case B_FIELD: cell = VA_FIELD + (B[b].st & 3); break;
          case B_ORCHARD: cell = VA_ORCHARD + (B[b].st & 3); break;
          case B_PEN: cell = VA_FENCE + (j == 0 ? 0 : (j == h - 1 ? 6 : 3)) + (i == 0 ? 0 : (i == w - 1 ? 2 : 1)); break;
          case B_FISH: {
            int pier = (var & 1) ? j : i, land_at = (var & 2) ? 1 : 0;
            cell = VA_FISH + (pier == land_at ? 0 : 1);
            break;
          }
          default: break;
        }
        int tx = px + i * GC_TILE - wx0, ty = py + j * GC_TILE;
        if (tp == B_PEN && !site) {      // fenced grass: the animals walk about inside
          if (cell >= 0) blit_sprite(out, wy0, tx, ty, GC_VILLAGE(cell), 0, 0, g_wpal);
          int idx = j * w + i, n = B[b].st, kind = (B[b].x + B[b].y) % 3;
          if (idx < n) blit_sprite(out, wy0, tx, ty, GC_VILLAGE(VA_ANIMAL + kind * 2 + ((s_vanim + idx) & 1)), (idx & 1) != 0, 0, g_wpal);
          if (idx + w * h < n) blit_sprite(out, wy0, tx - 4, ty - 6, GC_VILLAGE(VA_ANIMAL + kind * 2 + ((s_vanim + idx + 1) & 1)), 1, 0, g_wpal);
        } else if (cell >= 0) blit_sprite(out, wy0, tx, ty, GC_VILLAGE(cell), 0, 0, g_wpal);
      }
  }
  draw_smoke(out, wx0, wy0);
  {                                              // the finished monument glows: sparkles rise round it
    int mb = mon_b();
    if (mb >= 0 && B[mb].st >= 5 && s_phase <= PH_WORSHIP)
      for (int k = 0; k < 10; k++) {
        uint32_t h = mix32((uint32_t)(k * 977 + s_vanim * 131));
        int sx = B[mb].x * GC_TILE + 4 + (int)(h % 40) - wx0, sy = B[mb].y * GC_TILE + 40 - (int)(((h >> 8) % 36) + ((s_vanim + k) & 3) * 3);
        int dy = sy - wy0;
        if (dy < 0 || dy >= GC_STRIP_H - 1 || sx < 0 || sx >= GC_W - 1) continue;
        uint16_t col = (k & 1) ? g_act[P_WHITE] : g_act[P_YELLOW];
        out[dy * GC_W + sx] = col; out[dy * GC_W + sx + 1] = col; out[(dy + 1) * GC_W + sx] = col;
      }
  }
  // villagers
  uint8_t rm[16];
  // painter's order: villagers lower on the map are drawn after (over) the ones above them
  for (int row = (wy0 > 0 ? wy0 >> 4 : 0); row <= (wy0 > 0 ? wy0 >> 4 : 0) + 2; row++)
  for (int i = 0; i < MAXV; i++) {
    if (!used(i) || V[i].y != row || (vact(i) == A_SLEEP && s_home[i] < MAXB)) continue;      // asleep in a bed: indoors
    int sx = V[i].x * GC_TILE - wx0, sy = V[i].y * GC_TILE;
    if (sy + GC_TILE <= wy0 || sy - 10 >= wy0 + GC_STRIP_H || sx + 16 <= 0 || sx >= GC_W) continue;
    int role = vrole(i), act = vact(i), cls = vcls(i), dir = V[i].fl >> 6;
    for (int k = 0; k < 16; k++) rm[k] = (uint8_t)k;
    rm[9] = (uint8_t)((V[i].fl & F_WEAK) ? 14 : (sick(i) ? 4 : TUNIC[role]));
    rm[5] = (uint8_t)(cls == AGE_ELDER ? 13 : ((V[i].fl & F_FEM) ? 6 : 5));
    int frame = is_moving(i) ? 1 + ((V[i].x + V[i].y) & 1) : 0;
    int cell;
    if (cls == AGE_CHILD) cell = 9 + frame;
    else cell = (dir == 1 ? 3 : (dir >= 2 ? 6 : 0)) + frame;
    int bob = (cls == AGE_CHILD && act == A_IDLE && (s_vanim & 1)) ? -1 : 0;
    if (s_phase == PH_WORSHIP && act == A_WORK) bob = ((s_vanim + i) & 1) ? 2 : 0;      // they bow to the monument
    blit_sprite(out, wy0, sx, sy + bob, GC_VILLAGERS(cell), dir == 3, rm, g_act);
    if (act == A_SLEEP) { blit_sprite(out, wy0, sx, sy - 12, GC_ITEMS(6), 0, 0, g_act); continue; }
    if (act == A_WORK && !(V[i].fl & F_WEAK) && TOOL[role] >= 0) {
      int tb = (s_vanim & 1) ? 1 : -1;
      blit8(out, wy0, sx + (dir == 2 ? -5 : 11), sy + 5 + tb, GC_TOOLS(TOOL[role]));
    } else if (act == A_RET) {
      blit8(out, wy0, sx + (dir == 2 ? -5 : 11), sy + 7, GC_TOOLS(6));
    }
    if (sick(i)) { fill_rect(out, wy0, sx + 7, sy - 6, 2, 5, 0x47E0); fill_rect(out, wy0, sx + 5, sy - 4, 6, 2, 0x47E0); }      // a green cross
    else if (act == A_EAT) blit8(out, wy0, sx + 4, sy - 7, GC_ICONS(1));
    else if (role == R_ELDER && act == A_WORK && ((s_vanim & 1) || s_phase == PH_WORSHIP)) blit8(out, wy0, sx + 4, sy - 7 + (((s_vanim + i) & 1) ? 1 : 0), GC_ICONS(0));
  }
}

// ---------------------------------------------------------------- numbers for the HUD, queries and tests
// Buildings with no walkable tile beside them that the village green can reach (uses the shared g_dist)
static int unreachable_buildings(void) {
  int n = 0;
  world_flow(cx(), cy());
  for (int b = 0; b < MAXB; b++) {
    if (!bused(b)) continue;
    int w, h, ok = 0;
    bdims(b, &w, &h);
    for (int j = -1; j <= h && !ok; j++)
      for (int i = -1; i <= w && !ok; i++) {
        if ((i == -1 || i == w) && (j == -1 || j == h)) continue;
        if (i >= 0 && i < w && j >= 0 && j < h) continue;
        int x = B[b].x + i, y = B[b].y + j;
        if (x >= 0 && y >= 0 && x < WM && y < WM && g_dist[y * WM + x] != 255) ok = 1;
      }
    if (!ok) n++;
  }
  return n;
}

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
    case 15: return s_content;
    case 16: return (int)s_earned;
    case 17: return (int)s_spent;
    case 18: return (int)s_casts;
    case 19: return P.kind;
    case 30: return unreachable_buildings();
    case 250: return s_phase;
    case 251: return village_mon_stage();
    case 252: return village_mon_type();
    case 253: return s_peak;
    case 254: return s_built_tot;
    case 255: { int n = 0; for (int b = 0; b < MAXB; b++) if (bdone(b, B_PEN)) n += B[b].st; return n; }
    case 256: return fish_total();
    case 257: return s_pen_dead;
    case 258: return s_fish_caught;
    case 259: return s_settlers;
    case 260: return s_pen_born;
    case 261: return s_fishn;
    case 262: return s_mon_day;
    case 263: return s_lean;
    case 248: return s_aborts;
    case 31: return s_pers;
    case 32: return count_ramps();
    case 33: return s_ponds;
    case 34: return s_plots;
    case 35: return s_levels;
    case 36: { int n = 0; for (int i = 0; i < MAXV; i++) if (used(i) && sick(i)) n++; return n; }
    case 37: return s_sick;
    case 38: return s_sick_deaths;
    case 39: return s_onsets;
    case 200: return (int)s_crop_u;
    case 201: return (int)s_crop_n;
    case 202: return (int)s_thirst_n;
    case 203: return (int)s_burned;
    case 204: return count_trees();
    case 205: return count_saps(-1);
    case 206: return s_felled;
    case 207: return s_snow_lvl;
    case 208: return s_wet;
    case 209: return occupied_homes();
    case 210: { int n = 0; for (int b = 0; b < MAXB; b++) if (bused(b) && fire_at(b)) n++; return n; }
    case 14: { int n = 0; for (int i = 0; i < MAXV; i++) if (used(i) && vact(i) == A_SLEEP) n++; return n; }
    default: break;
  }
  if (which >= 240 && which < 248) return which < 244 ? s_cs[which - 240][0] : s_cs[which - 244][1];      // cloud shadows: x, then y (world pixels)
  if (which >= 211 && which < 211 + SAP_N) { uint16_t e = s_sap[which - 211]; return e == 0xFFFF ? -1 : (sap_x(e) | (sap_y(e) << 8) | (sap_age(e) << 16) | (sap_kind(e) << 20)); }
  if (which >= 20 && which < 20 + R_COUNT) { int n = 0; for (int i = 0; i < MAXV; i++) if (used(i) && vrole(i) == which - 20) n++; return n; }
  if (which >= 40 && which < 40 + B_COUNT) return count_done(which - 40);
  if (which >= 110 && which < 110 + MAXB) {       // building info: x | y << 8 | w << 16 | h << 20 | type << 24 | site << 28, -1 if unused
    int b = which - 110;
    if (!bused(b)) return -1;
    int w, h;
    bdims(b, &w, &h);
    return B[b].x | (B[b].y << 8) | (w << 16) | (h << 20) | (btype(b) << 24) | ((B[b].tp & SITE) ? 1 << 28 : 0);
  }
  if (which >= 60 && which < 60 + MAXV) { int i = which - 60; return used(i) ? (V[i].x | (V[i].y << 8) | (vact(i) << 16) | (vrole(i) << 20) | (hunger(i) << 24) | (sick(i) << 27) | (vcls(i) << 28) | ((V[i].fl & F_WEAK) ? 1 << 30 : 0)) : -1; }
  return 0;
}

// ---------------------------------------------------------------- new game, save
void village_reset(int mix) {
  for (int i = 0; i < MAXV; i++) { V[i].fl = 0; V[i].ra = 0; s_home[i] = 255; s_ill[i] = 0; }
  for (int b = 0; b < MAXB; b++) B[b].tp = 0xFF;
  nature_reset();
  s_rng = (uint32_t)mix * 2654435761u + 0x9e3779b9u;
  if (!s_rng) s_rng = 1;
  s_food = 18; s_wood = 12; s_faith = 0; s_water = 0; s_deaths = s_births = 0;
  s_sick = 0; s_src = s_content = s_roles_dirty = 0; s_earned = 0;
  s_phase = PH_PLAY; s_beat = 0; s_peak = 2; s_mon_day = 0; s_built_tot = 0; s_settlers = 0;
  shaman_reset();
  s_vacc = s_anim_acc = 0; s_vanim = 0; s_rr = 0; s_woke = 0; s_last_plan = 255;
  place_start_tent();
  s_built_tot = 1;
  add_villager(AGE_ADULT, 0, 8, cx(), cy());
  add_villager(AGE_ADULT, 1, 8, cx(), cy());
  rebuild_homes();
  assign_roles();
}

// The saved game of r4 and r5 (128 bytes inside the 192-byte record of version 4): [0] marker, [1] villagers (<= 16), [2] buildings (<= 18),
// food, wood, faith, water, then 4-byte villagers and 3-byte buildings (x, y, type+variant code, site flag, progress). It is only read
// now (an r5 save is loaded and the next save is a full r6 one); the full save is in savepack.inc.h.
enum { PK_V = 16, PK_B = 18, PK_V0 = 10, PK_B0 = 10 + 4 * 16 };

void village_unpack(const uint8_t* p) {
  if (p[0] != 0x56 || p[1] > PK_V || p[2] > PK_B) { village_reset((int)g_world.start_x * 31 + g_world.start_y); return; }
  for (int i = 0; i < MAXV; i++) { V[i].fl = 0; V[i].ra = 0; s_home[i] = 255; s_ill[i] = 0; }
  for (int b = 0; b < MAXB; b++) B[b].tp = 0xFF;
  nature_reset();
  s_phase = PH_PLAY; s_beat = 0; s_mon_day = 0; s_settlers = 0; s_peak = 2;
  s_rng = 0x2545F491u ^ ((uint32_t)p[3] << 8) ^ p[5];
  s_food = (uint16_t)(p[3] | (p[4] << 8)); s_wood = (uint16_t)(p[5] | (p[6] << 8)); s_faith = (uint16_t)(p[7] | (p[8] << 8)); s_water = p[9];
  if (s_food > 999) s_food = 999;
  if (s_wood > 999) s_wood = 999;
  if (s_faith > 9999) s_faith = 9999;
  s_deaths = s_births = 0; s_vacc = s_anim_acc = 0; s_vanim = 0; s_rr = 0; s_woke = 0; s_last_plan = 255;
  s_sick = 0; s_src = s_content = s_roles_dirty = 0; s_earned = s_faith;
  shaman_reset();
  int nb = 0;
  for (int k = 0; k < p[2]; k++) {
    const uint8_t* q = p + PK_B0 + k * 3;
    uint32_t a = q[0] | (q[1] << 8);
    int x = a & 31, y = (a >> 5) & 31, tp, var;
    if (!kind_decode((a >> 10) & 31, &tp, &var)) continue;
    int w, h;
    bdim(tp, var, &w, &h);
    int ok = x >= 1 && y >= 1 && x + w < WM && y + h < WM;
    for (int j = 0; j < h && ok; j++)
      for (int i = 0; i < w; i++) {
        uint8_t t = g_map[(y + j) * WM + x + i];
        if (MT_SOIL(t) == SOIL_BUILT || MT_H(t) < (tp == B_FISH ? 0 : 2)) ok = 0;
      }
    if (!ok || nb >= MAXB) continue;
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) world_set_built(x + i, y + j, 1);
    B[nb].x = (uint8_t)x; B[nb].y = (uint8_t)y; B[nb].tp = (uint8_t)(tp | (var << 4) | ((a >> 15) & 1 ? SITE : 0));
    B[nb].st = q[2];
    if (!(B[nb].tp & SITE) && is_crop(tp) && (B[nb].st & 3) > 2) B[nb].st = 0;
    if (tp == B_PEN && !(B[nb].tp & SITE) && B[nb].st > 10) B[nb].st = 2;
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
    if ((a >> 14) & 1) { s_ill[i] = 0x80; s_sick = 1; }
  }
  if (!pop()) village_reset((int)g_world.start_x * 31 + g_world.start_y);
  s_peak = (uint8_t)pop(); s_built_tot = (uint16_t)nb;
  heal_terrain();                    // the land is generated again from the seed: make it fit the loaded buildings
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
    case 4: { int f = arg < 0 ? 0 : (arg > 9999 ? 9999 : arg); s_earned += (uint32_t)(f - (int)s_faith); s_faith = (uint16_t)f; break; }
    case 3: for (int k = 0; k < MAXV; k++) if (used(k)) { set_thirst(k, arg < 0 ? 0 : arg); if (arg >= 28) V[k].fl |= F_WEAK; } break;
    case 5:     // arg = villager index: that villager falls sick; -1: everybody
      for (int k = 0; k < MAXV; k++) if (used(k) && (arg < 0 || k == arg) && !sick(k)) { s_ill[k] = (uint8_t)((s_ill[k] & 15) | 0x80); s_sick = 1; s_onsets++; mark_tile(V[k].x, V[k].y); }
      s_roles_dirty = 1;
      break;
    case 7: {   // arg = stage 0..5: the monument stands at that stage (laid out on the best spot, no faith asked)
      int stage = arg < 0 ? 0 : (arg > 5 ? 5 : arg), b = mon_b();
      if (b < 0) {
        s_wood = (uint16_t)(s_wood + cost_wood(B_MONUMENT));
        Cand c;
        if (!pick_site(B_MONUMENT, &c, 0)) break;
        int w, h;
        kdim(B_MONUMENT, 0, &w, &h);
        for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) { world_set_height(c.x + i, c.y + j, c.H); world_clear_object(c.x + i, c.y + j); }
        b = lay_site(B_MONUMENT, 0, c.x, c.y, 0);
        if (b < 0) break;
        heal_terrain();
      }
      B[b].st = (uint8_t)stage;
      s_mon_day = G.s.day;
      for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) mark_tile(B[b].x + i, B[b].y + j);
      break;
    }
    case 8: village_end_world(arg < 0 ? 0 : (arg > PH_EXTINCT ? PH_EXTINCT : (int)arg)); break;
    case 9: for (int k = 0; k < MAXV; k++) if (used(k)) kill(k); break;      // everybody dies: the village is extinct
    case 6: {   // arg = index | class << 8 | 1 << 16 (weak): change that villager's age class
      int k = arg & 255, cls = (arg >> 8) & 3;
      if (k < MAXV && used(k) && cls <= AGE_ELDER) {
        int l = life(k);
        V[k].fl = (uint8_t)((V[k].fl & ~F_AGE) | cls);
        V[k].age = (uint8_t)((V[k].age & 0xE0) | (cls == AGE_ELDER ? l - 4 : (cls == AGE_ADULT ? 8 : 0)));
        if ((arg >> 16) & 1) V[k].fl |= F_WEAK; else V[k].fl &= (uint8_t)~F_WEAK;
        s_roles_dirty = 1;
      }
      break;
    }
    default: break;
  }
}

// debug: force a phase (PH_WORSHIP: the worship day starts now; PH_DONE and PH_EXTINCT: the summary shows)
void village_end_world(int phase) {
  s_phase = (uint8_t)phase; s_beat = 0;
  if (phase == PH_WORSHIP) { if (village_mon_stage() < 5) village_debug(7, 5); s_roles_dirty = 1; assign_roles(); }
  gq_touch();
}

#include "savepack.inc.h"
