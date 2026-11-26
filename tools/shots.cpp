// Host renderer: draws a world with the real game core and writes a PPM. Used by tools/make_screenshots.py.
//   shots screen out.ppm seed landform climate [ticks]   what the clock shows (240x240), after some walking
//   shots map    out.ppm seed landform climate           the whole 512x512 map (the camera is moved over it)
// landform 0..5 or -1 and climate 0..3 or -1 mean "from the seed".
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../lib/gamecore/src/gamecore.h"
#include "../lib/gamecore/src/gq_types.h"

static void rgb(uint16_t c, unsigned char* o) {
  int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  o[0] = (unsigned char)((r << 3) | (r >> 2)); o[1] = (unsigned char)((g << 2) | (g >> 4)); o[2] = (unsigned char)((b << 3) | (b >> 2));
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
  gc_set_clock(10, 24, 1);
  gc_set_live_weather(GQ_W_CLEAR, 214, 0);
  gc_set_status(0, "IP 192.168.1.50");
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
