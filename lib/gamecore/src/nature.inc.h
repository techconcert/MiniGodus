// Weather, sickness, firewood and the forest of MiniGodus (release r5). Included by village.cpp after shaman.inc.h (it shares the
// village's statics). No heap, no libc, no floating point.
//
// Weather comes from the live Open-Meteo reading (or a debug override). Rules:
//   rain       fields grow half as fast again, puddles on low tiles, fishers catch more, villagers working outside collect exposure
//   storm      like rain, heavier, lightning, and everybody shelters at home (nobody works outside while it lasts)
//   snow       snow settles on the high layers (reversible: it melts), fields stop, firewood is needed (it counts as cold)
//   above 28 C thirst steps come twice as often and more water carriers are hired
//   below 8 C  each occupied home burns 1 wood a night (chimney smoke), lumberjacks come first; a home without wood is a cold home
//   fog        haze;  clear and cloudy: cloud shadows drift over the land;  no reading: mild (15 C) and dry, no clouds
// Sickness: once a day each villager rolls against a risk that rises with hunger, thirst, rain and cold exposure and cold nights.
// A sick villager does not work and goes to the infirmary. The infirmary with a healer cures in 1 day, an infirmary alone in 2, none in 3.
// Untreated, a sick elder or weak villager may die; a healthy fed adult never dies of it.
// Wood: lumberjacks fell trees (a stump stays), plant a sapling on the stump, saplings grow back into trees in 4 to 5 days, and the
// forest seeds new saplings by itself, so wood never runs out for good (the lumberjacks leave the last trees alone).

enum { SAP_N = 24, TREE_FLOOR = 6, TREE_TARGET = 36, NP = 32, MAPSTRIPS = 0x3FFE };

static uint16_t s_sap[SAP_N];                 // stumps and saplings: x | y << 5 | age days << 10 | sapling << 13, 0xFFFF = free
static uint32_t s_wsnow[WM];                  // tiles where the weather put snow (one bit each); the permanent snow caps are not in it
static uint8_t  s_snow_lvl, s_wet, s_wet_shown;
static uint8_t  s_px[NP], s_py[NP], s_pkind, s_pn, s_pstep, s_dim, s_wlast, s_cstep, s_nshadow;
static uint16_t s_pacc, s_cacc;
static int16_t  s_flash, s_bolt_cd;
static uint8_t  s_bolt_x, s_bolt_end;
static uint32_t s_bolt_seed, s_vis;
static int16_t  s_cs[4][2];                   // cloud shadows, world pixels

static uint32_t vrnd(void) { s_vis ^= s_vis << 13; s_vis ^= s_vis >> 17; s_vis ^= s_vis << 5; return s_vis; }

static void nature_reset(void) {
  for (int k = 0; k < SAP_N; k++) s_sap[k] = 0xFFFF;
  for (int i = 0; i < WM; i++) s_wsnow[i] = 0;
  s_snow_lvl = s_wet = s_wet_shown = 0;
  s_sick_deaths = s_onsets = s_felled = 0;
  s_crop_u = s_crop_n = s_thirst_n = s_burned = 0;
  s_hot = s_cold = s_rainy = s_storm = s_snowing = s_fog_now = 0;
  s_cond = 255; s_prev_night = 255; s_night_now = 0;
  for (int k = 0; k < (int)sizeof(s_fire); k++) s_fire[k] = 0;
  s_pkind = 0; s_pn = 0; s_pstep = 0; s_dim = 0; s_wlast = 255; s_cstep = 0; s_nshadow = 0; s_pacc = s_cacc = 0;
  s_flash = 0; s_bolt_cd = 6000;
  if (!s_vis) s_vis = 0x9E3779B9u;
  for (int i = 0; i < 4; i++) { s_cs[i][0] = (int16_t)(30 + i * 140); s_cs[i][1] = (int16_t)(50 + i * 120); }
}

// ------------------------------------------------------------------------------------------------ conditions
static void conditions(void) {
  int w = world_weather(), t = world_temp10();
  s_rainy = w == GQ_W_RAIN; s_storm = w == GQ_W_STORM; s_snowing = w == GQ_W_SNOW; s_fog_now = w == GQ_W_FOG;
  s_hot = t > 280;
  s_cold = (t < 80 || s_snowing) ? 1 : 0;
  uint8_t c = (uint8_t)(s_hot | (s_cold << 1) | (s_rainy << 2) | (s_storm << 3) | (s_snowing << 4));
  if (c != s_cond) { s_cond = c; s_roles_dirty = 1; }
}

// night falls: in the cold every occupied home burns a log; a home without wood is a cold home (its people risk sickness)
static void night_begin(void) {
  for (int k = 0; k < (int)sizeof(s_fire); k++) s_fire[k] = 0;
  if (!s_cold) return;
  for (int b = 0; b < MAXB; b++) {
    if (!bused(b) || (B[b].tp & SITE) || !is_home(btype(b))) continue;
    int occ = 0;
    for (int i = 0; i < MAXV; i++) if (used(i) && s_home[i] == b) occ++;
    if (!occ) continue;
    if (s_wood > 0) { s_wood--; s_burned++; s_fire[b >> 3] |= (uint8_t)(1 << (b & 7)); mark_tile(B[b].x, B[b].y - 1); }
    else for (int i = 0; i < MAXV; i++) if (used(i) && s_home[i] == b) s_ill[i] |= 16;
  }
}

// ------------------------------------------------------------------------------------------------ sickness
static void sickness_day(void) {
  int infirm = count_done(B_INFIRM) > 0, healer = 0;
  for (int i = 0; i < MAXV; i++) if (used(i) && vrole(i) == R_HEALER && !sick(i) && !(V[i].fl & F_WEAK) && vcls(i) == AGE_ADULT) healer = 1;
  for (int i = 0; i < MAXV; i++) {
    if (!used(i)) continue;
    int frail = (V[i].fl & F_WEAK) != 0 || vcls(i) == AGE_ELDER;
    if (sick(i)) {
      int d = sick_days(i) + 1;
      int need = infirm ? (healer ? 1 : 2) : 3;
      int dpct = infirm ? (healer ? 0 : 6) : 25;                  // untreated, the frail may die
      if (frail && dpct && (int)(vr() % 100) < dpct) { kill(i); s_sick_deaths++; continue; }
      if (d >= need) { s_ill[i] = 0; s_roles_dirty = 1; } else s_ill[i] = (uint8_t)(0x80 | (d << 5));
      mark_tile(V[i].x, V[i].y);
      continue;
    }
    int h = hunger(i) > 3 ? 3 : hunger(i);
    int ex = s_ill[i] & 15;
    int risk = 1 + 3 * h + thirst(i) / 4 + 2 * (ex > 10 ? 10 : ex) + ((s_ill[i] & 16) ? 12 : 0) + (frail ? 3 : 0);
    if (risk > 60) risk = 60;
    s_ill[i] = 0;
    if ((int)(vr() % 100) < risk) { s_ill[i] = 0x80; s_sick = 1; s_onsets++; s_roles_dirty = 1; mark_tile(V[i].x, V[i].y); }
  }
}

// ------------------------------------------------------------------------------------------------ snow and puddles
static void snow_apply(int lvl) {
  int thr = lvl == 0 ? 9 : (lvl == 1 ? 6 : (lvl == 2 ? 5 : 4));      // the lowest layer the snow reaches
  for (int i = 0; i < WM * WM; i++) {
    uint8_t b = g_map[i];
    int h = MT_H(b);
    if ((s_wsnow[i >> 5] >> (i & 31)) & 1) { b = (uint8_t)(b & ~MT_SNOW); s_wsnow[i >> 5] &= ~(1u << (i & 31)); }
    if (h >= thr && h <= 6 && MT_SOIL(b) != SOIL_BUILT && !(b & MT_SNOW)) { b |= MT_SNOW; s_wsnow[i >> 5] |= 1u << (i & 31); }
    g_map[i] = b;
  }
  quarts_rebuild(0, 0, WM - 1, WM - 1);
  g_dirty |= MAPSTRIPS;
}

static void snow_minute(int m) {
  int lvl = s_snow_lvl;
  if (s_snowing) { if (m % 12 == 0 && lvl < 3) lvl++; }
  else if (lvl > 0) { int t = world_temp10(), every = t >= 50 ? 10 : (t >= 0 ? 20 : 60); if (m % every == 0) lvl--; }
  if (s_rainy || s_storm) { int w = s_wet + (s_storm ? 8 : 4); s_wet = (uint8_t)(w > 255 ? 255 : w); }
  else if (s_wet) s_wet = (uint8_t)(s_wet > 2 ? s_wet - 2 : 0);
  if (lvl != s_snow_lvl) { s_snow_lvl = (uint8_t)lvl; snow_apply(lvl); }
  int d = (int)s_wet - (int)s_wet_shown;
  if (d >= 24 || d <= -24 || (!s_wet && s_wet_shown)) { s_wet_shown = s_wet; g_dirty |= MAPSTRIPS; }
}

// ------------------------------------------------------------------------------------------------ the forest
static int  sap_x(uint16_t e) { return e & 31; }
static int  sap_y(uint16_t e) { return (e >> 5) & 31; }
static int  sap_age(uint16_t e) { return (e >> 10) & 7; }
static int  sap_kind(uint16_t e) { return (e >> 13) & 1; }          // 0 stump, 1 sapling
static uint16_t sap_pack(int x, int y, int age, int kind) { return (uint16_t)(x | (y << 5) | (age << 10) | (kind << 13)); }

static int count_trees(void) { int n = 0; for (int i = 0; i < WM * WM; i++) if (MT_OBJ(g_map[i]) == OB_TREE && MT_H(g_map[i]) >= 2) n++; return n; }
static int count_saps(int kind) { int n = 0; for (int k = 0; k < SAP_N; k++) if (s_sap[k] != 0xFFFF && (kind < 0 || sap_kind(s_sap[k]) == kind)) n++; return n; }
static int sap_find(int x, int y) { for (int k = 0; k < SAP_N; k++) if (s_sap[k] != 0xFFFF && sap_x(s_sap[k]) == x && sap_y(s_sap[k]) == y) return k; return -1; }
static int sap_put(int x, int y, int age, int kind) {
  for (int k = 0; k < SAP_N; k++) if (s_sap[k] == 0xFFFF) { s_sap[k] = sap_pack(x, y, age, kind); return k; }
  return -1;
}

// free ground where a tree may stand: layers 3 to 5, nothing on it, off every building, plot and project, not on the green,
// and never beside a building (the doors stay open)
static int sap_ok(int x, int y) {
  if (x < 2 || y < 2 || x >= WM - 2 || y >= WM - 2) return 0;
  uint8_t b = g_map[y * WM + x];
  int h = MT_H(b);
  if (h < 3 || h > 5 || MT_OBJ(b) != OB_NONE || MT_SOIL(b) == SOIL_BUILT || in_plot(x, y) || in_proj(x, y)) return 0;
  int dx = x - cx(), dy = y - cy();
  if (dx * dx + dy * dy < 9) return 0;
  for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) if (built_at(x + i, y + j) >= 0) return 0;
  return 1;
}

// A lumberjack beside a tree or stump: plant a sapling on a stump, else fell a tree. 1 if he did something.
static int lumberjack_work(int x, int y) {
  static const int8_t D[4][2] = {{0, 1}, {0, -1}, {-1, 0}, {1, 0}};
  for (int k = 0; k < 4; k++) {
    int tx = x + D[k][0], ty = y + D[k][1], e = sap_find(tx, ty);
    if (e < 0 || sap_kind(s_sap[e])) continue;
    if (sap_ok(tx, ty)) s_sap[e] = sap_pack(tx, ty, 0, 1); else s_sap[e] = 0xFFFF;
    mark_tile(tx, ty);
    return 1;
  }
  if (count_trees() <= TREE_FLOOR) return 0;                      // the last trees stay
  for (int k = 0; k < 4; k++) {
    int tx = x + D[k][0], ty = y + D[k][1];
    if (!tree_at(tx, ty) || in_plot(tx, ty) || in_proj(tx, ty)) continue;
    world_clear_object(tx, ty);
    s_wood = (uint16_t)(s_wood + 3 + (count_done(B_LUMBER) ? 1 : 0));
    s_felled++;
    sap_put(tx, ty, 0, 0);
    mark_tile(tx, ty);
    return 1;
  }
  return 0;
}

static void nature_day(void) {
  // stumps rot away after 3 days; saplings become trees after 4 to 5 days, never where that would shut a way
  for (int k = 0; k < SAP_N; k++) {
    uint16_t e = s_sap[k];
    if (e == 0xFFFF) continue;
    int x = sap_x(e), y = sap_y(e), age = sap_age(e) < 7 ? sap_age(e) + 1 : 7, kind = sap_kind(e);
    mark_tile(x, y);
    if (!kind) { s_sap[k] = age >= 3 ? 0xFFFF : sap_pack(x, y, age, 0); continue; }
    int need = 4 + ((x * 7 + y * 3) & 1);
    if (age < need) { s_sap[k] = sap_pack(x, y, age, 1); continue; }
    if (!sap_ok(x, y)) { s_sap[k] = 0xFFFF; continue; }
    uint8_t b = g_map[y * WM + x];
    world_flow(cx(), cy());
    int base = count_reach(), fr = g_dist[y * WM + x] != 255 ? 1 : 0;
    g_map[y * WM + x] = (uint8_t)((b & 0x3F) | (OB_TREE << 6));
    world_flow(cx(), cy());
    if (count_reach() != base - fr) { g_map[y * WM + x] = b; s_sap[k] = sap_pack(x, y, need, 1); continue; }      // a tree never closes a path: wait
    s_sap[k] = 0xFFFF;
  }
  // the forest seeds itself: saplings beside the trees that are left (anywhere when the forest is nearly gone)
  int trees = count_trees(), saps = count_saps(1);
  int tries = trees + saps < 12 ? 5 : 3;
  for (int k = 0; k < tries && trees + saps < TREE_TARGET; k++) {
    int x, y, start = (int)(vr() % (WM * WM)), at = -1;
    for (int q = 0; q < WM * WM && at < 0; q++) { int i = (start + q) & (WM * WM - 1); if (MT_OBJ(g_map[i]) == OB_TREE && MT_H(g_map[i]) >= 2) at = i; }
    if (at >= 0 && trees + saps >= 12) { x = at % WM + (int)(vr() % 5) - 2; y = at / WM + (int)(vr() % 5) - 2; }
    else { x = (int)(vr() % WM); y = (int)(vr() % WM); }
    if (sap_ok(x, y) && sap_find(x, y) < 0 && sap_put(x, y, 0, 1) >= 0) saps++;
  }
}

// ------------------------------------------------------------------------------------------------ drawing: stumps, saplings, smoke
static void draw_forest_ground(uint16_t* out, int wx0, int wy0) {
  for (int k = 0; k < SAP_N; k++) {
    uint16_t e = s_sap[k];
    if (e == 0xFFFF) continue;
    int px = sap_x(e) * GC_TILE - wx0, py = sap_y(e) * GC_TILE;
    if (py + GC_TILE <= wy0 || py >= wy0 + GC_STRIP_H || px + GC_TILE <= 0 || px >= GC_W) continue;
    if (!sap_kind(e)) {
      fill_rect(out, wy0, px + 4, py + 9, 8, 5, g_wpal[13]);
      fill_rect(out, wy0, px + 4, py + 8, 8, 2, g_wpal[12]);
      fill_rect(out, wy0, px + 5, py + 8, 5, 1, g_wpal[11]);
      fill_rect(out, wy0, px + 4, py + 13, 8, 1, g_wpal[15]);
    } else if (sap_age(e) < 2) {
      fill_rect(out, wy0, px + 7, py + 10, 2, 4, g_wpal[13]);
      fill_rect(out, wy0, px + 6, py + 7, 4, 3, g_wpal[3]);
      fill_rect(out, wy0, px + 7, py + 6, 2, 1, g_wpal[4]);
    } else {
      fill_rect(out, wy0, px + 7, py + 8, 2, 6, g_wpal[13]);
      fill_rect(out, wy0, px + 4, py + 3, 8, 6, g_wpal[3]);
      fill_rect(out, wy0, px + 5, py + 2, 6, 2, g_wpal[4]);
      fill_rect(out, wy0, px + 4, py + 8, 8, 1, g_wpal[2]);
    }
  }
}

static void draw_smoke(uint16_t* out, int wx0, int wy0) {
  static const uint16_t COL[6] = {0xFFFF, 0xEF7D, 0xDEFB, 0xC638, 0xB596, 0xA514};
  for (int b = 0; b < MAXB; b++) {
    if (!bused(b) || !fire_at(b)) continue;
    int w, h;
    bdims(b, &w, &h);
    int cxp = B[b].x * GC_TILE + (btype(b) == B_TENT ? 8 : w * GC_TILE - 6) - wx0, base = B[b].y * GC_TILE + 2;
    if (base - 22 >= wy0 + GC_STRIP_H || base + 2 <= wy0 || cxp + 5 < 0 || cxp >= GC_W) continue;
    for (int k = 0; k < 3; k++) {
      int rise = ((int)s_vanim + k * 2) % 6;
      fill_rect(out, wy0, cxp + ((rise + k) & 1), base - 3 - rise * 3, rise > 3 ? 2 : 3, rise > 3 ? 2 : 3, COL[rise]);
    }
  }
}

// ------------------------------------------------------------------------------------------------ drawing: weather over the map
static uint32_t mix32(uint32_t h) { h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; return h ^ (h >> 16); }
static inline uint16_t blend50(uint16_t a, uint16_t b) { return (uint16_t)(((a & 0xF7DE) >> 1) + ((b & 0xF7DE) >> 1)); }

void weather_flash(void) {
  s_flash = 160;
  s_bolt_x = (uint8_t)(40 + vrnd() % 160); s_bolt_end = (uint8_t)(90 + vrnd() % 110); s_bolt_seed = vrnd();
  g_dirty |= MAPSTRIPS;
}

static int shadows_wanted(int night) {
  int w = world_weather();
  if (night || !world_has_reading()) return 0;
  return w == GQ_W_CLEAR ? 2 : (w == GQ_W_CLOUDY ? 4 : (w == GQ_W_WINDY ? 3 : 0));
}

void weather_tick(uint32_t dt, int night) {
  int w = world_weather();
  int kind = w == GQ_W_RAIN ? 1 : (w == GQ_W_STORM ? 2 : (w == GQ_W_SNOW ? 3 : 0));
  int n = shadows_wanted(night);
  uint8_t sig = (uint8_t)(w | (world_has_reading() << 3) | (night << 4));
  if (sig != s_wlast) { s_wlast = sig; g_dirty |= MAPSTRIPS; }      // fog, clouds, night dimming come and go
  if (night != s_dim) { s_dim = (uint8_t)night; g_dirty |= MAPSTRIPS; }
  if (kind != s_pkind) {
    s_pkind = (uint8_t)kind;
    s_pn = (uint8_t)(kind == 1 ? 24 : (kind == 2 ? 32 : (kind == 3 ? 24 : 0)));
    for (int i = 0; i < s_pn; i++) { s_px[i] = (uint8_t)(vrnd() % 240); s_py[i] = (uint8_t)(vrnd() % 208); }
    g_dirty |= MAPSTRIPS;
  }
  if (kind) {
    s_pacc = (uint16_t)(s_pacc + dt);
    if (s_pacc >= 100) {
      s_pacc = (uint16_t)(s_pacc - 100); s_pstep++;
      for (int i = 0; i < s_pn; i++) {
        int x = s_px[i], y = s_py[i];
        if (kind == 3) { y += 3; x += (((s_pstep + i) & 7) < 4) ? 1 : -1; }
        else { y += kind == 2 ? 17 : 13; if (kind == 2) x -= 3; }
        if (x < 0) x += 240;
        if (x >= 240) x -= 240;
        if (y >= 208) { y -= 208; if (kind != 3 || (vrnd() & 1)) x = (int)(vrnd() % 240); }
        s_px[i] = (uint8_t)x; s_py[i] = (uint8_t)y;
      }
      g_dirty |= MAPSTRIPS;
    }
  }
  if (kind == 2) {
    s_bolt_cd = (int16_t)(s_bolt_cd - (int)dt);
    if (s_bolt_cd <= 0) { weather_flash(); s_bolt_cd = (int16_t)(5000 + (int)(vrnd() % 15000)); }
  }
  if (s_flash > 0) { s_flash = (int16_t)(s_flash - (int)dt); if (s_flash <= 0) { s_flash = 0; g_dirty |= MAPSTRIPS; } }
  if (n) {
    s_cacc = (uint16_t)(s_cacc + dt);
    if (s_cacc >= 250) {
      s_cacc = (uint16_t)(s_cacc - 250); s_cstep++;
      int cam_x, cam_y;
      world_cam(&cam_x, &cam_y);
      for (int i = 0; i < n; i++) {
        s_cs[i][0] = (int16_t)(s_cs[i][0] + (w == GQ_W_WINDY ? 3 : 1));
        if ((s_cstep & 3) == 0) s_cs[i][1]++;
        if (s_cs[i][0] > WM_PIX + 40) s_cs[i][0] = -40;
        if (s_cs[i][1] > WM_PIX + 20) s_cs[i][1] = -20;
        int sx = s_cs[i][0] - cam_x, sy = s_cs[i][1] - cam_y + GC_STRIP_H;
        if (sx > -44 && sx < GC_W + 44) mark_rows(sy - 20, 42);
      }
    }
  }
}

void weather_ground(uint16_t* out, int wx0, int wy0) {
  if (s_wet < 16) return;
  int ty0 = wy0 >> 4, ty1 = (wy0 + GC_STRIP_H - 1) >> 4, tx0 = wx0 >> 4, tx1 = (wx0 + GC_W - 1) >> 4;
  if (ty0 < 0) ty0 = 0;
  if (tx0 < 0) tx0 = 0;
  if (ty1 > WM - 1) ty1 = WM - 1;
  if (tx1 > WM - 1) tx1 = WM - 1;
  for (int ty = ty0; ty <= ty1; ty++)
    for (int tx = tx0; tx <= tx1; tx++) {
      uint8_t b = g_map[ty * WM + tx];
      int h = MT_H(b);
      if (h < 2 || h > 3 || MT_OBJ(b) || MT_SOIL(b) == SOIL_BUILT) continue;        // puddles collect on the low land
      uint32_t hs = hash2(tx, ty);
      if ((hs & 255) >= s_wet) continue;
      int px = tx * GC_TILE + 2 + (int)((hs >> 8) % 5) - wx0, py = ty * GC_TILE + 5 + (int)((hs >> 12) % 6), w = 6 + (int)((hs >> 16) % 3);
      fill_rect(out, wy0, px + 1, py, w - 2, 1, 0x3C9B);
      fill_rect(out, wy0, px, py + 1, w, 2, 0x5D9F);
      fill_rect(out, wy0, px + 1, py + 3, w - 2, 1, 0x3C9B);
      if (s_rainy || s_storm) fill_rect(out, wy0, px + 2 + g_wframe * 2, py + 1, 2, 1, 0xAF7F);      // ripples
    }
}

// Over everything on the map strip s (1..13): haze, cloud shadows, night, lightning, rain and snow.
void weather_sky(uint16_t* out, int wx0, int wy0, int s) {
  int w = world_weather(), y0 = s * GC_STRIP_H;
  int fs = 1 + s_bolt_end / GC_STRIP_H;
  int flashing = s_flash > 0 && (s == fs || s == fs + 1);
  if (w == GQ_W_FOG) {
    const uint16_t fog = 0xCE79;
    for (int i = 0; i < GC_STRIP_PIXELS; i++) {
      uint16_t a = blend50(out[i], fog), b = blend50(a, fog);      // a 50% haze, b 75%
      out[i] = s <= 3 ? b : (s <= 6 ? blend50(a, b) : (s <= 9 ? a : blend50(a, out[i])));      // thicker in the distance (the top of the screen)
    }
  }
  int n = shadows_wanted(s_dim);
  for (int k = 0; k < n; k++) {
    int cxp = s_cs[k][0], cyp = s_cs[k][1];
    int ya = cyp - 18 > wy0 ? cyp - 18 : wy0, yb = cyp + 18 < wy0 + GC_STRIP_H ? cyp + 18 : wy0 + GC_STRIP_H - 1;
    int xa = cxp - 40 > wx0 ? cxp - 40 : wx0, xb = cxp + 40 < wx0 + GC_W ? cxp + 40 : wx0 + GC_W - 1;
    for (int y = ya; y <= yb; y++)
      for (int x = xa; x <= xb; x++) {
        int dx = x - cxp, dy = y - cyp, v = dx * dx * 324 + dy * dy * 1600;      // an ellipse of 80 x 36 pixels
        if (v >= 518400) continue;
        uint16_t* p = &out[(y - wy0) * GC_W + x - wx0];
        uint16_t c = *p, q = (uint16_t)((c >> 2) & 0x39E7);       // a quarter darker at the edge, three eighths in the middle
        if (v < 233280) q = (uint16_t)(q + ((c >> 3) & 0x18E3));
        *p = (uint16_t)(c - q);
      }
  }
  if (s_dim && !flashing)
    for (int i = 0; i < GC_STRIP_PIXELS; i++) out[i] = (uint16_t)(((out[i] >> 1) & 0x7BEF) + 0x0003);
  if (flashing)
    for (int i = 0; i < GC_STRIP_PIXELS; i++) out[i] = blend50(out[i], 0xFFFF);
  if (s_flash > 0) {         // the bolt: a jagged line from the top of the map
    for (int r = 0; r < GC_STRIP_H; r++) {
      int my = y0 + r - GC_STRIP_H;
      if (my < 0 || my > s_bolt_end) continue;
      int sl = (s_bolt_seed & 1) ? 1 : -1;
      int x = s_bolt_x + sl * (my / 6) + (int)(mix32(s_bolt_seed + (uint32_t)(my / 12) * 977u) % 15) - 7;
      int xp = s_bolt_x + sl * ((my - 1) / 6) + (int)(mix32(s_bolt_seed + (uint32_t)((my - 1 < 0 ? 0 : my - 1) / 12) * 977u) % 15) - 7;
      int a = x < xp ? x : xp, b = (x < xp ? xp : x) + 1;
      for (int c = a; c <= b + 1; c++) if (c >= 0 && c < GC_W) out[r * GC_W + c] = (c == a || c == b + 1) ? 0xFFE0 : 0xFFFF;
    }
  }
  if (s_pkind == 3) {
    for (int i = 0; i < s_pn; i++) {
      int ay = GC_STRIP_H + s_py[i], x = s_px[i];
      for (int r = 0; r < 2; r++) {
        int yy = ay + r - y0;
        if (yy < 0 || yy >= GC_STRIP_H) continue;
        out[yy * GC_W + x] = 0xFFFF;
        if (x + 1 < GC_W) out[yy * GC_W + x + 1] = 0xFFFF;
      }
    }
  } else if (s_pkind) {
    int len = s_pkind == 2 ? 6 : 4;
    for (int i = 0; i < s_pn; i++) {
      int ay = GC_STRIP_H + s_py[i];
      for (int r = 0; r < len; r++) {
        int yy = ay + r - y0, x = s_px[i] - (s_pkind == 2 ? r / 2 : 0);
        if (yy < 0 || yy >= GC_STRIP_H || x < 0 || x >= GC_W) continue;
        out[yy * GC_W + x] = r == len - 1 ? 0xDF7F : 0x9E7F;
      }
    }
  }
}
