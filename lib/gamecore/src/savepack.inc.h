// The full saved game of MiniGodus (release r6), the village's share. Included by village.cpp (it reads and writes the village's statics).
//
// The saved game is a stream of GC_SAVE_TOTAL bytes in GC_SAVE_PARTS parts: part 0 is 188 bytes, the others 300 (they fit the data area of
// a 320-byte flash slot; firmware/store.cpp writes one part per slot and a record is valid only when all six slots are intact).
//   part 0   header of 64 bytes (godus.cpp) + village_pack_head() (stocks, statistics, monument pace, fish stocks, reserved plots)
//   part 1   48 villagers x 5 bytes (position, class, sex, weak, birth cooldown, age, needs, sickness), 24 saplings and stumps x 2
//   part 2   40 buildings x 3 bytes (position, kind, site flag, progress or stage or animals), the ramp bits of the map (128 bytes, r7;
//            an r6 save has the list of the last 10 ramps there: it reads as stray ramp bits; unpacking clears every ramp that joins no two layers)
//   part 3-5 the map, 5 bits per tile (layer 0..7 and object 0..3): terraforming, felled trees, ponds and fish shoals
// Not saved because it is re-made at once: roles and what everybody is doing, the shaman's current project, weather snow (from the snow
// level), puddles, smoke, cloud shadows.
// Unpacking writes straight into the live state without checking; village_unpack_end() makes whatever came in consistent, so any bytes
// leave a valid village.

namespace {
struct Wr {
  uint8_t* p; int n;
  void u8(int v) { p[n++] = (uint8_t)v; }
  void u16(int v) { p[n++] = (uint8_t)v; p[n++] = (uint8_t)(v >> 8); }
  void u32(uint32_t v) { u16((int)(v & 0xFFFF)); u16((int)(v >> 16)); }
};
struct Rd {
  const uint8_t* p; int n;
  int u8() { return p[n++]; }
  int u16() { int a = p[n]; int b = p[n + 1]; n += 2; return a | (b << 8); }
  uint32_t u32() { uint32_t a = (uint32_t)u16(); return a | ((uint32_t)u16() << 16); }
};
enum { MAP_FIRST = 3, MAP_TILES_PER_PART = 480 };
}

void village_pack_head(uint8_t* p) {
  Wr w = {p, 0};
  w.u8(s_phase); w.u8(s_beat); w.u16(s_mon_day);
  w.u16(s_food); w.u16(s_wood); w.u16(s_faith); w.u8(s_water); w.u8(s_snow_lvl);
  w.u16(s_births); w.u16(s_deaths); w.u8(s_peak); w.u16(s_built_tot); w.u16(s_felled); w.u16(s_sick_deaths); w.u16(s_onsets);
  w.u8(s_sick); w.u8(s_lean); w.u16(s_pen_dead); w.u16(s_pen_born); w.u16(s_fish_caught); w.u16(s_settlers);
  w.u32(s_rng);
  for (int i = 0; i < 3; i++) { w.u8(s_plot[i][0]); w.u8(s_plot[i][1]); }
  w.u8(s_fishn);
  for (int k = 0; k < 8; k++) { w.u16(k < s_fishn ? s_fish_a[k] : 0xFFFF); w.u8(k < s_fishn ? s_fish_s[k] : 0); }
}

void village_unpack_head(const uint8_t* p) {
  for (int i = 0; i < MAXV; i++) { V[i].fl = 0; V[i].ra = 0; s_home[i] = 255; s_ill[i] = 0; }
  for (int b = 0; b < MAXB; b++) B[b].tp = 0xFF;
  nature_reset();
  shaman_reset();
  Rd r = {p, 0};
  s_phase = (uint8_t)r.u8(); s_beat = (uint8_t)r.u8(); s_mon_day = (uint16_t)r.u16();
  s_food = (uint16_t)r.u16(); s_wood = (uint16_t)r.u16(); s_faith = (uint16_t)r.u16(); s_water = (uint8_t)r.u8(); s_snow_lvl = (uint8_t)r.u8();
  s_births = (uint16_t)r.u16(); s_deaths = (uint16_t)r.u16(); s_peak = (uint8_t)r.u8(); s_built_tot = (uint16_t)r.u16(); s_felled = (uint16_t)r.u16();
  s_sick_deaths = (uint16_t)r.u16(); s_onsets = (uint16_t)r.u16();
  s_sick = (uint8_t)(r.u8() ? 1 : 0); s_lean = (uint8_t)r.u8(); s_pen_dead = (uint16_t)r.u16(); s_pen_born = (uint16_t)r.u16();
  s_fish_caught = (uint16_t)r.u16(); s_settlers = (uint16_t)r.u16();
  s_rng = r.u32();
  for (int i = 0; i < 3; i++) { s_plot[i][0] = (uint8_t)r.u8(); s_plot[i][1] = (uint8_t)r.u8(); }
  s_fishn = (uint8_t)r.u8();
  for (int k = 0; k < 8; k++) { s_fish_a[k] = (uint16_t)r.u16(); s_fish_s[k] = (uint8_t)r.u8(); }
  // checks
  if (s_phase > PH_EXTINCT) s_phase = PH_PLAY;
  if (s_beat > 3) s_beat = 3;
  if (!s_rng) s_rng = 0x2545F491u;
  if (s_food > 999) s_food = 999;
  if (s_wood > 999) s_wood = 999;
  if (s_faith > 9999) s_faith = 9999;
  if (s_water > 250) s_water = 250;
  if (s_snow_lvl > 3) s_snow_lvl = 0;
  if (s_peak < 2) s_peak = 2;
  if (s_fishn > 8) s_fishn = 8;
  for (int k = 0; k < 8; k++) if (k >= s_fishn || s_fish_a[k] >= WM * WM) { s_fish_a[k] = 0xFFFF; s_fish_s[k] = 0; if (k < s_fishn) s_fishn = (uint8_t)k; }
  for (int i = 0; i < 3; i++) if (s_plot[i][0] > WM - 3 || s_plot[i][1] > WM - 3) s_plot[i][0] = s_plot[i][1] = 255;
  s_vacc = s_anim_acc = 0; s_vanim = 0; s_rr = 0; s_woke = 0; s_last_plan = 255;
  s_src = s_content = s_roles_dirty = 0; s_earned = s_faith;
}

int village_pack_part(int part, uint8_t* p, int cap) {
  Wr w = {p, 0};
  if (part == 1) {
    if (cap < MAXV * 5 + SAP_N * 2) return 0;
    for (int i = 0; i < MAXV; i++) {
      if (!used(i)) { w.u16(0xFFFF); w.u8(0); w.u8(0); w.u8(0); continue; }
      uint32_t a = (uint32_t)(V[i].x & 31) | ((uint32_t)(V[i].y & 31) << 5) | ((uint32_t)vcls(i) << 10) | ((V[i].fl & F_FEM) ? 1u << 12 : 0) |
                   ((V[i].fl & F_WEAK) ? 1u << 13 : 0) | ((uint32_t)cool(i) << 14);
      w.u16((int)a); w.u8(V[i].age); w.u8(V[i].nd); w.u8(s_ill[i]);
    }
    for (int k = 0; k < SAP_N; k++) w.u16(s_sap[k]);
    return w.n;
  }
  if (part == 2) {
    if (cap < MAXB * 3 + WM * WM / 8) return 0;
    for (int b = 0; b < MAXB; b++) {
      if (!bused(b)) { w.u16(0xFFFF); w.u8(0); continue; }
      uint32_t a = (uint32_t)(B[b].x & 31) | ((uint32_t)(B[b].y & 31) << 5) | ((uint32_t)kind_code(btype(b), bvar(b)) << 10) | ((B[b].tp & SITE) ? 1u << 15 : 0);
      w.u16((int)a); w.u8(B[b].st);
    }
    for (int k = 0; k < WM * WM / 8; k++) w.u8(g_ramp[k]);
    return w.n;
  }
  if (part >= MAP_FIRST && part < MAP_FIRST + 3) {
    int lo = (part - MAP_FIRST) * MAP_TILES_PER_PART, hi = lo + MAP_TILES_PER_PART > WM * WM ? WM * WM : lo + MAP_TILES_PER_PART;
    int nbytes = ((hi - lo) * 5 + 7) / 8;
    if (cap < nbytes) return 0;
    for (int i = 0; i < nbytes; i++) p[i] = 0;
    int bit = 0;
    for (int t = lo; t < hi; t++) {
      uint8_t b = g_map[t];
      int v = MT_H(b) | (MT_OBJ(b) << 3);
      for (int k = 0; k < 5; k++, bit++) if ((v >> k) & 1) p[bit >> 3] |= (uint8_t)(1 << (bit & 7));
    }
    return nbytes;
  }
  return 0;
}

void village_unpack_part(int part, const uint8_t* p) {
  Rd r = {p, 0};
  if (part == 1) {
    for (int i = 0; i < MAXV; i++) {
      uint32_t a = (uint32_t)r.u16();
      int age = r.u8(), nd = r.u8(), ill = r.u8();
      if (a == 0xFFFF) { V[i].fl = 0; V[i].ra = 0; s_ill[i] = 0; continue; }
      int cls = (a >> 10) & 3, c = (a >> 14) & 3;
      if (cls > AGE_ELDER) cls = AGE_ADULT;
      V[i].x = (uint8_t)(a & 31); V[i].y = (uint8_t)((a >> 5) & 31);
      V[i].fl = (uint8_t)(F_USED | cls | (((a >> 12) & 1) ? F_FEM : 0) | (((a >> 13) & 1) ? F_WEAK : 0) | ((c & 1) << 5));
      V[i].ra = (uint8_t)((cls == AGE_CHILD ? R_CHILD : R_IDLE) | ((c >> 1) << 7));
      V[i].age = (uint8_t)age; V[i].nd = (uint8_t)nd;
      s_ill[i] = (uint8_t)(((ill & 0x80) ? 0x80 | (ill & 0x60) : 0) | (ill & 31));
      if (sick(i)) s_sick = 1;
    }
    for (int k = 0; k < SAP_N; k++) {
      int e = r.u16();
      s_sap[k] = (e == 0xFFFF || (e >> 14)) ? 0xFFFF : (uint16_t)e;
    }
  } else if (part == 2) {
    for (int b = 0; b < MAXB; b++) {
      uint32_t a = (uint32_t)r.u16();
      int st = r.u8(), tp, var;
      B[b].tp = 0xFF;
      if (a == 0xFFFF || !kind_decode((int)((a >> 10) & 31), &tp, &var)) continue;
      B[b].x = (uint8_t)(a & 31); B[b].y = (uint8_t)((a >> 5) & 31);
      B[b].tp = (uint8_t)(tp | (var << 4) | (((a >> 15) & 1) ? SITE : 0)); B[b].st = (uint8_t)st;
    }
    for (int k = 0; k < WM * WM / 8; k++) g_ramp[k] = (uint8_t)r.u8();
  } else if (part >= MAP_FIRST && part < MAP_FIRST + 3) {
    int lo = (part - MAP_FIRST) * MAP_TILES_PER_PART, hi = lo + MAP_TILES_PER_PART > WM * WM ? WM * WM : lo + MAP_TILES_PER_PART;
    int bit = 0;
    for (int t = lo; t < hi; t++) {
      int v = 0;
      for (int k = 0; k < 5; k++, bit++) if ((p[bit >> 3] >> (bit & 7)) & 1) v |= 1 << k;
      g_map[t] = (uint8_t)((g_map[t] & 0x38) | (v & 7) | ((v >> 3) << 6));       // soil and snow of the generated land stay
    }
  }
}

void village_unpack_end(void) {
  // the map: no objects on water except a shoal, snow only where it can lie; the shaman's bed stands on dry ground
  for (int i = 0; i < WM * WM; i++) {
    uint8_t b = g_map[i];
    int h = MT_H(b);
    if (h <= 1 && MT_OBJ(b) != OB_NONE && MT_OBJ(b) != OB_BUSH) b = (uint8_t)(b & 0x3F);
    if (h <= 2) b &= (uint8_t)~MT_SNOW;
    if (MT_SOIL(b) == SOIL_BUILT) b = (uint8_t)(b & 0xCF);
    g_map[i] = b;
    if (h < 2 || h > 6) world_set_ramp(i % WM, i / WM, 0);
  }
  { uint8_t& b = g_map[cy() * WM + cx()]; if (MT_H(b) < 3 || MT_H(b) > 6) b = (uint8_t)((b & 0x38) | 4); else b &= 0x3F; }
  // buildings: inside the map, on land, not on each other
  for (int b = 0; b < MAXB; b++) {
    if (!bused(b)) continue;
    int tp = btype(b), var = bvar(b), w, h, ok;
    bdim(tp, var, &w, &h);
    int x = B[b].x, y = B[b].y;
    ok = x >= 1 && y >= 1 && x + w < WM && y + h < WM;
    for (int j = 0; j < h && ok; j++)
      for (int i = 0; i < w; i++) {
        uint8_t t = g_map[(y + j) * WM + x + i];
        if (MT_SOIL(t) == SOIL_BUILT || MT_H(t) < (tp == B_FISH ? 0 : 2)) ok = 0;
      }
    if (ok && tp == B_MONUMENT && (B[b].tp & SITE)) ok = 0;
    if (ok && tp == B_MONUMENT) for (int c = 0; c < b; c++) if (bused(c) && btype(c) == B_MONUMENT) ok = 0;
    if (!ok) { B[b].tp = 0xFF; continue; }
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) world_set_built(x + i, y + j, 1);
    if (tp == B_MONUMENT) { if (B[b].st > 5) B[b].st = 5; }
    else if (!(B[b].tp & SITE) && is_crop(tp) && (B[b].st & 3) > 2) B[b].st = 0;
    else if (tp == B_PEN && !(B[b].tp & SITE) && B[b].st > 10) B[b].st = 2;
    else if ((B[b].tp & SITE) && B[b].st >= need_min(tp)) B[b].st = (uint8_t)(need_min(tp) - 1);
  }
  // ramps: only where two neighbouring layers meet (an r6 save has a list of tile numbers in these bytes, which read as stray bits)
  for (int i = 0; i < WM * WM; i++) if (world_ramp(i % WM, i / WM) && !world_ramp_used(i % WM, i / WM)) world_set_ramp(i % WM, i / WM, 0);
  // people stand on ground they can walk on
  for (int i = 0; i < MAXV; i++)
    if (used(i) && !world_walkable(V[i].x, V[i].y)) { V[i].x = (uint8_t)cx(); V[i].y = (uint8_t)cy(); }
  for (int k = 0; k < SAP_N; k++) {
    uint16_t e = s_sap[k];
    if (e != 0xFFFF && (MT_H(g_map[sap_y(e) * WM + sap_x(e)]) < 2 || MT_SOIL(g_map[sap_y(e) * WM + sap_x(e)]) == SOIL_BUILT)) s_sap[k] = 0xFFFF;
  }
  if (!pop() && s_phase <= PH_WORSHIP) { village_reset((int)g_world.start_x * 31 + g_world.start_y); return; }
  if (s_phase == PH_WORSHIP && village_mon_stage() < 5) s_phase = PH_PLAY;
  heal_terrain();                    // steps left over by a project that was under way, footprints levelled: free of charge
  rebuild_homes();
  assign_roles();
  if (s_snow_lvl) snow_apply(s_snow_lvl);
  quarts_rebuild(0, 0, WM - 1, WM - 1);
}
