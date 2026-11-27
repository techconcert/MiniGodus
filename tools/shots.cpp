// Host renderer: draws a world with the real game core and writes a PPM. Used by tools/make_screenshots.py.
//   shots screen out.ppm seed landform climate [ticks [minutes]]   what the clock shows (240x240), after some walking and
//                                                   'minutes' simulated minutes of village life (clock from 06:00)
//   shots map    out.ppm seed landform climate           the whole 512x512 map (the camera is moved over it)
// Extra arguments after 'minutes' (r4): mode boost
//   mode  town  the camera is put over the village green;  cast  run until the shaman raises his staff, band shows his project;
//         pond  run until a pond is dug (shot just after, the camera follows the shaman)
//   boost  faith added every simulated hour (a rich world, to see the shaman work early)
// Extra argument 10 (r5), comma separated tokens:
//   w<N>  force weather GQ_W_N for the last 'lead' minutes before the picture   t<N>  force the temperature (tenths of a degree)
//   lead=<minutes>  (default 240)   pop=<N>  add N villagers at the start   flash  a lightning flash in the picture
//   shadow  let the cloud shadows drift until one is over the picture
//   sick  one sick villager from the start of the lead   wood0  the village is always out of wood
//   F<c>  put the camera over: i infirmary, l lumberjack, c water carrier, b homes, v the village, h the high land, s saplings, m the monument
// r8: mode L with lab=<label start, _ for a space>: run on until the band reads that, then show it (e.g. lab=PRAYING, lab=WALKING_TO_SITE)
// r6 tokens: mon=<0..5>  the monument stands at that stage (debug)   phase=<0..4>  force the arc phase (1 worship day, 3 summary, 4 extinct summary)
//   extinct  everybody dies   settle=<minutes>  run on that long after the debug commands (the villagers walk to the monument)
// Mode 'i' runs on until a sick villager stands at the finished infirmary, mode 'l' until a lumberjack has felled a tree and a sapling stands.
// landform 0..5 or -1 and climate 0..3 or -1 mean "from the seed".
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../lib/gamecore/src/gamecore.h"
#include "../lib/gamecore/src/gq_types.h"
#include "../lib/gamecore/src/village.h"
#include <string>
#include <cstring>

static void rgb(uint16_t c, unsigned char* o) {
  int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  o[0] = (unsigned char)((r << 3) | (r >> 2)); o[1] = (unsigned char)((g << 2) | (g >> 4)); o[2] = (unsigned char)((b << 3) | (b >> 2));
}

static int vq(int n) { return (int)gc_query(GQ_Q_VILLAGE + n); }

struct Tok { int food = -1, wood = -1, faith = -1; char lab[28] = {0}; int w = -1, t = 9999, lead = 240, pop = 0, mon = -1, phase = -1, settle = 0; bool extinct = false; bool flash = false, sick = false, wood0 = false, shadow = false; char focus = 0; };
static Tok parse(const char* s) {
  Tok k;
  std::string str = s ? s : "";
  size_t i = 0;
  while (i < str.size()) {
    size_t j = str.find(',', i);
    if (j == std::string::npos) j = str.size();
    std::string t = str.substr(i, j - i);
    if (t.rfind("lead=", 0) == 0) k.lead = std::atoi(t.c_str() + 5);
    else if (t.rfind("pop=", 0) == 0) k.pop = std::atoi(t.c_str() + 4);
    else if (t.rfind("mon=", 0) == 0) k.mon = std::atoi(t.c_str() + 4);
    else if (t.rfind("phase=", 0) == 0) k.phase = std::atoi(t.c_str() + 6);
    else if (t.rfind("settle=", 0) == 0) k.settle = std::atoi(t.c_str() + 7);
    else if (t.rfind("lab=", 0) == 0) { std::strncpy(k.lab, t.c_str() + 4, 27); for (char* q = k.lab; *q; q++) if (*q == '_') *q = ' '; }
    else if (t.rfind("food=", 0) == 0) k.food = std::atoi(t.c_str() + 5);
    else if (t.rfind("wood=", 0) == 0) k.wood = std::atoi(t.c_str() + 5);
    else if (t.rfind("faith=", 0) == 0) k.faith = std::atoi(t.c_str() + 6);
    else if (t == "extinct") k.extinct = true;
    else if (t == "flash") k.flash = true;
    else if (t == "sick") k.sick = true;
    else if (t == "wood0") k.wood0 = true;
    else if (t == "shadow") k.shadow = true;
    else if (t.size() >= 2 && t[0] == 'w') k.w = std::atoi(t.c_str() + 1);
    else if (t.size() >= 2 && t[0] == 't') k.t = std::atoi(t.c_str() + 1);
    else if (t.size() == 2 && t[0] == 'F') k.focus = t[1];
    i = j + 1;
  }
  return k;
}

static void look_at(int tx, int ty) {
  int x = tx * 16 - 112, y = ty * 16 - 96;
  gc_debug(GQ_DBG_SET_CAM, (x < 0 ? 0 : (x > 272 ? 272 : x)) + 1024 * (y < 0 ? 0 : (y > 304 ? 304 : y)));
}

static void focus(char c) {
  int sx = (int)gc_query(GQ_Q_START_X), sy = (int)gc_query(GQ_Q_START_Y);
  long ax = 0, ay = 0, n = 0;
  if (c == 'i' || c == 'b' || c == 'v') {
    for (int b = 0; b < 40; b++) {
      int v = vq(110 + b);
      if (v < 0) continue;
      int tp = (v >> 24) & 15;
      if (c == 'i' && tp != B_INFIRM) continue;
      if (c == 'b' && tp > B_MANOR) continue;
      ax += (v & 255) + ((v >> 16) & 15) / 2; ay += ((v >> 8) & 255) + ((v >> 20) & 15) / 2; n++;
    }
  } else if (c == 'l' || c == 'c') {
    for (int i = 0; i < 48; i++) {
      int v = vq(60 + i);
      if (v < 0) continue;
      int role = (v >> 20) & 15;
      if ((c == 'l' && role != R_LUMBER) || (c == 'c' && role != R_CARRIER)) continue;
      ax += v & 255; ay += (v >> 8) & 255; n++;
      break;
    }
  } else if (c == 's') {
    for (int k = 0; k < 24; k++) { int v = vq(211 + k); if (v >= 0) { ax += v & 255; ay += (v >> 8) & 255; n++; } }
  } else if (c == 'm') {
    int mx = 0, my = 0;
    if (village_mon_pos(&mx, &my)) { ax = mx + 1; ay = my + 1; n = 1; }
  } else if (c == 'h') {
    for (int i = 0; i < WM * WM; i++) if (MT_H(g_map[i]) >= 5 && MT_H(g_map[i]) <= 6) { ax += i % WM; ay += i / WM; n++; }
  }
  if (n) look_at((int)(ax / n), (int)(ay / n)); else look_at(sx, sy);
}

static int sick_adult(void) {
  for (int i = 0; i < 48; i++) { int v = vq(60 + i); if (v >= 0 && ((v >> 28) & 3) == AGE_ADULT && !((v >> 27) & 1)) return i; }
  return -1;
}
static bool sick_at_infirmary(void) {
  int bx = -1, by = -1;
  for (int b = 0; b < 40; b++) { int v = vq(110 + b); if (v >= 0 && ((v >> 24) & 15) == B_INFIRM && !((v >> 28) & 1)) { bx = v & 255; by = (v >> 8) & 255; } }
  if (bx < 0) return false;
  for (int i = 0; i < 48; i++) {
    int v = vq(60 + i);
    if (v < 0 || !((v >> 27) & 1) || ((v >> 16) & 7) != A_WORK) continue;
    int x = v & 255, y = (v >> 8) & 255;
    if (x >= bx - 1 && x <= bx + 3 && y >= by - 1 && y <= by + 3) return true;
  }
  return false;
}

int main(int argc, char** argv) {
  if (argc < 6) { std::fprintf(stderr, "usage: shots screen|map out.ppm seed landform climate [ticks]\n"); return 2; }
  bool map = argv[1][0] == 'm';
  uint32_t seed = (uint32_t)std::strtoul(argv[3], 0, 10);
  int lf = std::atoi(argv[4]), cl = std::atoi(argv[5]), ticks = argc > 6 ? std::atoi(argv[6]) : 0;
  gc_init(GC_SCENE_VILLAGE, 1, 0);
  gc_set_depth(40);
  gc_debug(GQ_DBG_FORCE_LAND, lf);
  gc_debug(GQ_DBG_FORCE_CLIM, cl);
  gc_debug(GQ_DBG_SET_SEED, (int32_t)seed);
  int minutes = argc > 7 ? std::atoi(argv[7]) : 0;
  const char* mode = argc > 8 ? argv[8] : "";
  int boost = argc > 9 ? std::atoi(argv[9]) : 0;
  gc_set_clock(10, 24, 1);
  gc_set_live_weather(GQ_W_CLEAR, 214, 0);
  gc_set_status(0, "IP 192.168.1.50");
  Tok tk = parse(argc > 10 ? argv[10] : "");
  if (tk.pop) gc_debug(GQ_DBG_ADD_VILLAGERS, tk.pop);
  if (minutes > 0) {
    int wall = 6 * 60;
    bool sky = mode[0] == 'i' || mode[0] == 'l';
    for (int m = 0; m < minutes; m++) {
      if (m == minutes - tk.lead) {
        if (tk.w >= 0) gc_debug(GQ_DBG_WEATHER, tk.w);
        if (tk.t != 9999) gc_debug(GQ_DBG_TEMP, tk.t);
      }
      wall = (wall + 1) % 1440; gc_set_clock(wall / 60, wall % 60, 1);
      for (int i = 0; i < 8; i++) gc_tick(500);
      if (boost && m % 60 == 0) gc_debug(GQ_DBG_FAITH, (int)gc_query(GQ_Q_VILLAGE + 3) + boost);
      if (tk.wood0 && m % 60 == 0) gc_debug(GQ_DBG_WOOD, 0);
      if (tk.sick && m >= minutes - tk.lead && m % 120 == 0) { int k = sick_adult(); if (k >= 0 && vq(36) == 0) gc_debug(GQ_DBG_SICK, k); }
    }
    (void)sky;
    if (mode[0] == 'i' || mode[0] == 'l') {          // run on until the picture is there
      for (int m = 0; m < 8 * 1440; m++) {
        if (mode[0] == 'i' && sick_at_infirmary()) break;
        if (mode[0] == 'l' && vq(206) >= 3 && vq(205) >= 2) {
          bool at = false;
          for (int i = 0; i < 48; i++) { int v = vq(60 + i); if (v >= 0 && ((v >> 20) & 15) == R_LUMBER && ((v >> 16) & 7) == A_WORK) at = true; }
          if (at) break;
        }
        wall = (wall + 1) % 1440; gc_set_clock(wall / 60, wall % 60, 1);
        for (int i = 0; i < 8; i++) gc_tick(500);
        if (m % 60 == 0) {
          gc_debug(GQ_DBG_FAITH, (int)gc_query(GQ_Q_VILLAGE + 3) + (boost ? boost : 100));
          if (mode[0] == 'i') { gc_debug(GQ_DBG_WOOD, 200); int k = sick_adult(); if (k >= 0 && vq(36) == 0) gc_debug(GQ_DBG_SICK, k); }      // one patient at a time
          else gc_debug(GQ_DBG_WOOD, 0);
        }
      }
    }
    if (mode[0] == 'r' || mode[0] == 'R') {          // r7: run on until villagers are on a ramp (r) or the shaman cuts one (R), camera over it
      bool done = false;
      for (int m = 0; m < 12 * 1440 && !done; m++) {
        wall = (wall + 1) % 1440; gc_set_clock(wall / 60, wall % 60, 1);
        for (int i = 0; i < 8 * 2 && !done; i++) {
          gc_tick(250);
          if (mode[0] == 'R') {
            if (gc_query(GQ_Q_HERO_ACT) == GQ_ACT_CAST && vq(19) == 6) { gc_tick(300); look_at(((int)gc_query(GQ_Q_SHAMAN_X) + 8) >> 4, ((int)gc_query(GQ_Q_SHAMAN_Y) + 8) >> 4); gc_debug(GQ_DBG_BAND, 5); done = true; }
          } else {
            int on = 0, rx = 0, ry = 0;
            for (int k = 0; k < 48; k++) { int v = vq(60 + k); if (v < 0) continue; int x = v & 255, y = (v >> 8) & 255; if (world_ramp_used(x, y)) { on++; rx = x; ry = y; } }
            if (on >= (tk.pop > 20 ? 2 : 1) && ((vq(60 + 0) >= 0) || true)) { look_at(rx, ry); done = true; }
          }
        }
        if (m % 60 == 0) gc_debug(GQ_DBG_FAITH, (int)gc_query(GQ_Q_VILLAGE + 3) + (boost ? boost : 100));
      }
    }
    if (mode[0] == 'c' || mode[0] == 'p') {          // run on until the picture is there
      for (int m = 0; m < 6 * 1440; m++) {
        if (mode[0] == 'c' && gc_query(GQ_Q_HERO_ACT) == GQ_ACT_CAST) break;
        if (mode[0] == 'p' && gc_query(GQ_Q_VILLAGE + 33) > 0) break;
        wall = (wall + 1) % 1440; gc_set_clock(wall / 60, wall % 60, 1);
        for (int i = 0; i < 8; i++) { gc_tick(500); if (mode[0] == 'c' && gc_query(GQ_Q_HERO_ACT) == GQ_ACT_CAST) break; }
        if (boost && m % 60 == 0) gc_debug(GQ_DBG_FAITH, (int)gc_query(GQ_Q_VILLAGE + 3) + boost);
      }
      if (mode[0] == 'c') gc_tick(120);
      gc_debug(GQ_DBG_BAND, 5);
    }
    if (mode[0] == 'L') {                            // r8: run on until the band would read lab=<text> (_ is a space), then show it
      for (int m = 0; m < 10 * 1440; m++) {
        if (!std::strncmp(world_state_label(), tk.lab, std::strlen(tk.lab))) break;
        wall = (wall + 1) % 1440; gc_set_clock(wall / 60, wall % 60, 1);
        for (int i = 0; i < 8 * 4; i++) { gc_tick(125); if (!std::strncmp(world_state_label(), tk.lab, std::strlen(tk.lab))) break; }
        if (boost && m % 60 == 0) gc_debug(GQ_DBG_FAITH, (int)gc_query(GQ_Q_VILLAGE + 3) + boost);
      }
      gc_tick(300);
      gc_debug(GQ_DBG_BAND, 5);
    }
    if (tk.mon >= 0) gc_debug(GQ_DBG_MONUMENT, tk.mon);
    if (tk.phase >= 0) gc_debug(GQ_DBG_PHASE, tk.phase);
    if (tk.extinct) gc_debug(GQ_DBG_EXTINCT, 0);
    for (int m = 0; m < tk.settle; m++) {
      wall = (wall + 1) % 1440; gc_set_clock(wall / 60, wall % 60, 1);
      for (int i = 0; i < 8; i++) gc_tick(500);
    }
    if (tk.food >= 0) gc_debug(GQ_DBG_FOOD, tk.food);
    if (tk.wood >= 0) gc_debug(GQ_DBG_WOOD, tk.wood);
    if (tk.faith >= 0) gc_debug(GQ_DBG_FAITH, tk.faith);
    if (tk.focus) focus(tk.focus);
    if (tk.shadow) {          // let the clouds drift until one is over the middle of the picture
      for (int i = 0; i < 4000; i++) {
        int cam_x = (int)gc_query(GQ_Q_CAM_X), cam_y = (int)gc_query(GQ_Q_CAM_Y), hit = 0;
        for (int k = 0; k < 4; k++) {
          int dx = vq(240 + k) - cam_x - 120, dy = vq(244 + k) - cam_y - 120;
          if (dx > -30 && dx < 30 && dy > -20 && dy < 20) hit = 1;
        }
        if (hit) break;
        gc_tick(250);
      }
    }
    if (tk.flash) { gc_debug(GQ_DBG_FLASH, 0); gc_tick(1); }
    if (mode[0] == 't') {
      // the window of 15 x 13 tiles (between the bars) that shows the most buildings whole, nearest to the green
      int bx[40], by[40], bw[40], bh[40], nb = 0;
      for (int b = 0; b < 40; b++) {
        int v = (int)gc_query(GQ_Q_VILLAGE + 110 + b);
        if (v == -1 || v == 0) continue;
        bx[nb] = v & 255; by[nb] = (v >> 8) & 255; bw[nb] = (v >> 16) & 15; bh[nb] = (v >> 20) & 15; nb++;
      }
      int sx = (int)gc_query(GQ_Q_START_X), sy = (int)gc_query(GQ_Q_START_Y), best = -1, tx0 = sx - 7, ty0 = sy - 6;
      for (int y0 = 0; y0 <= 32 - 13; y0++)
        for (int x0 = 0; x0 <= 32 - 15; x0++) {
          int n = 0;
          for (int k = 0; k < nb; k++) if (bx[k] >= x0 && by[k] >= y0 && bx[k] + bw[k] <= x0 + 15 && by[k] + bh[k] <= y0 + 13) n++;
          int score = n * 100 - (std::abs(x0 + 7 - sx) + std::abs(y0 + 6 - sy));
          if (score > best) { best = score; tx0 = x0; ty0 = y0; }
        }
      int px = tx0 * 16, py = ty0 * 16;
      gc_debug(GQ_DBG_SET_CAM, px + 1024 * py);
    }
  }
  for (int i = 0; i < ticks * 4; i++) gc_tick(250);
  static uint16_t strip[GC_STRIP_PIXELS];
  int W = map ? 512 : 240, H = map ? 512 : 240;
  std::vector<unsigned char> img((size_t)W * H * 3, 0);
  auto grab = [&](int ox, int oy, int s0, int s1) {
    for (int s = s0; s <= s1; s++) {
      gc_render_strip(s, strip);
      for (int y = 0; y < 16; y++)
        for (int x = 0; x < 240; x++) {
          int X = ox + x, Y = oy + s * 16 + y - (map ? 16 : 0) + (map ? 0 : 0);
          if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
          rgb(strip[y * 240 + x], &img[((size_t)Y * W + X) * 3]);
        }
    }
  };
  if (!map) grab(0, 0, 0, 14);
  else {
    for (int cy = 0; cy < 512; cy += 208)
      for (int cx = 0; cx < 512; cx += 240) {
        gc_debug(GQ_DBG_SET_CAM, cx + 1024 * cy);
        int ax = (int)gc_query(GQ_Q_CAM_X), ay = (int)gc_query(GQ_Q_CAM_Y);
        grab(ax, ay, 1, 13);
      }
  }
  FILE* f = std::fopen(argv[2], "wb");
  std::fprintf(f, "P6\n%d %d\n255\n", W, H);
  std::fwrite(img.data(), 1, img.size(), f);
  std::fclose(f);
  return 0;
}
