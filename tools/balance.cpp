// Balancing over many seeds (release r6): runs whole worlds headless, with the real game core, until the monument is finished and
// the worship day is over, the village dies out, or a day limit is reached. Reports when worlds end and how big the towns get.
//
//   g++ -std=c++17 -O2 -o balance tools/balance.cpp lib/gamecore/src/*.cpp
//   ./balance FIRST_SEED COUNT [MAX_DAYS [TRACE]]       e.g. ./balance 1 100 70
//   ./balance 7 1 70 1                                  one seed, a line per day
//
// The weather is simulated from the seed (clear, cloudy, rain, storms, fog; temperature 4 to 26 C over a slow cycle), the clock
// runs minute by minute from 08:00 and the core is ticked 4 times per game minute (2 s each: the villagers walk about 20 tiles).
// Exit code 0 when the targets hold: no world ends before day 30, the median end is between day 30 and 45, at most 3% extinct.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../lib/gamecore/src/gamecore.h"
#include "../lib/gamecore/src/gq_types.h"

static int vq(int n) { return (int)gc_query(GQ_Q_VILLAGE + n); }
static uint32_t hsh(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; return x ^ (x >> 16); }

struct Res { uint32_t seed; int end_day, phase, peak, births, deaths, built, unlock_day, mon_day, stage_day[6], pop_d20, pop_d30, towns_day; int land, clim; };

static void weather_for(uint32_t seed, int day) {
  uint32_t h = hsh(seed * 31u + (uint32_t)day * 977u), r = h % 100;
  int w = r < 50 ? GQ_W_CLEAR : (r < 70 ? GQ_W_CLOUDY : (r < 85 ? GQ_W_RAIN : (r < 88 ? GQ_W_STORM : (r < 94 ? GQ_W_FOG : GQ_W_WINDY))));
  int phase = (int)((hsh(seed) % 60) + day) % 60;
  int t10 = 150 + (phase < 30 ? phase * 7 - 105 : (60 - phase) * 7 - 105);     // 4.5 .. 25.5 C
  if (t10 < 40) t10 = 40;
  if (t10 < 80 && r % 4 == 0) w = GQ_W_SNOW;
  gc_set_live_weather(w, t10, 0);
}

static Res run(uint32_t seed, int max_days, int trace, int tpm, int tdt) {
  Res r = {}; r.towns_day = -1;
  r.seed = seed;
  gc_init(GC_SCENE_VILLAGE, 1, 0);
  gc_debug(GQ_DBG_SET_SEED, (int32_t)seed);
  r.land = (int)gc_query(GQ_Q_LANDFORM); r.clim = (int)gc_query(GQ_Q_CLIMATE);
  int wall = 8 * 60;
  gc_set_clock(8, 0, 1);
  int last_stage = 0;
  r.unlock_day = r.mon_day = -1;
  for (int i = 0; i < 6; i++) r.stage_day[i] = -1;
  for (int d = 0; d < max_days; d++) {
    weather_for(seed, d);
    for (int m = 0; m < 1440; m++) {
      wall = (wall + 1) % 1440;
      gc_set_clock(wall / 60, wall % 60, 1);
      for (int i = 0; i < tpm; i++) gc_tick((uint32_t)tdt);
      if ((int)gc_query(GQ_Q_PHASE) >= 3) break;
    }
    int day = (int)gc_query(GQ_Q_DAY), pop = vq(0), ph = (int)gc_query(GQ_Q_PHASE), st = (int)gc_query(GQ_Q_MON_STAGE);
    if (d + 1 == 20) r.pop_d20 = pop;
    if (d + 1 == 30) r.pop_d30 = pop;
    if (pop >= 20 && r.towns_day < 0) r.towns_day = d + 1;
    if (st > last_stage) { for (int k = last_stage + 1; k <= st; k++) r.stage_day[k] = day + 1; last_stage = st; }
    if (r.mon_day < 0 && vq(40 + 13) > 0) r.mon_day = day + 1;
    if (trace)
      std::printf("day %2d pop %2d food %3d wood %3d faith %4d bld %2d births %2d deaths %2d | tent %d hut %d cot %d man %d fld %d orch %d pen %d fish %d | mon %d phase %d animals %d fishstock %d settlers %d\n", day + 1, pop, vq(1), vq(2), vq(3), vq(6), vq(12), vq(11),
                  vq(40), vq(41), vq(42), vq(43), vq(44), vq(45), vq(46), vq(47), st, ph, vq(255), vq(256), vq(259));
    if (ph >= 3) { r.end_day = day + 1; r.phase = ph; break; }
  }
  r.peak = vq(253); r.births = vq(12); r.deaths = vq(11); r.built = vq(254);
  if (!r.end_day) { r.end_day = 0; r.phase = 0; }
  return r;
}

int main(int argc, char** argv) {
  if (argc < 3) { std::fprintf(stderr, "usage: balance FIRST_SEED COUNT [MAX_DAYS [TRACE [TICKS_PER_MINUTE [TICK_MS]]]]\n"); return 2; }
  uint32_t first = (uint32_t)std::strtoul(argv[1], 0, 10);
  int count = std::atoi(argv[2]), max_days = argc > 3 ? std::atoi(argv[3]) : 70, trace = argc > 4 ? std::atoi(argv[4]) : 0;
  int tpm = argc > 5 ? std::atoi(argv[5]) : 4, tdt = argc > 6 ? std::atoi(argv[6]) : 2000 / (tpm ? tpm / 2 : 1);
  if (argc <= 6) tdt = 8000 / tpm;      // 8 s of game time per minute
  std::vector<Res> all;
  for (int k = 0; k < count; k++) {
    Res r = run(first + (uint32_t)k * 7919u, max_days, trace, tpm, tdt);
    all.push_back(r);
    std::printf("seed %10u land %d clim %d: %s day %2d  peak %2d  births %3d deaths %3d built %3d  town(20) day %2d  pop d20 %2d d30 %2d  unlock %2d stages", r.seed, r.land, r.clim,
                r.phase == 3 ? "MONUMENT" : (r.phase == 4 ? "EXTINCT " : "running "), r.end_day, r.peak, r.births, r.deaths, r.built, r.towns_day, r.pop_d20, r.pop_d30, r.mon_day);
    for (int i = 1; i <= 5; i++) std::printf(" %d", r.stage_day[i]);
    std::printf("\n");
    std::fflush(stdout);
  }
  std::vector<int> ends, peaks;
  int ext = 0, run_on = 0, early = 0;
  for (auto& r : all) {
    if (r.phase == 4) ext++;
    if (r.phase == 0) run_on++;
    if (r.phase != 0) { ends.push_back(r.end_day); if (r.end_day < 30) early++; }
    peaks.push_back(r.peak);
  }
  std::sort(ends.begin(), ends.end()); std::sort(peaks.begin(), peaks.end());
  auto q = [](std::vector<int>& v, int pct) { return v.empty() ? 0 : v[(v.size() - 1) * pct / 100]; };
  std::printf("\n%d worlds: ended %d (monument %d, extinct %d), still running at day %d: %d\n", count, (int)ends.size(), (int)ends.size() - ext, ext, max_days, run_on);
  std::printf("end day: min %d, 10%% %d, median %d, 90%% %d, max %d;  before day 30: %d\n", q(ends, 0), q(ends, 10), q(ends, 50), q(ends, 90), q(ends, 100), early);
  std::printf("peak population: min %d, median %d, max %d\n", q(peaks, 0), q(peaks, 50), q(peaks, 100));
  int med = q(ends, 50);
  bool ok = early == 0 && med >= 30 && med <= 45 && ext * 100 <= count * 3;
  std::printf("targets (no end before day 30, median 30-45, extinct <= 3%%): %s\n", ok ? "met" : "NOT met");
  return ok ? 0 : 1;
}
