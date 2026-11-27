// firmware/store.cpp (the real file) on a simulated flash: whole-game save and load through 6-slot records, power cuts at many
// points of erases and writes, the r5 one-slot record, and the quarantine after crashes. Every "boot" and every save runs in a
// forked child so that the store's statics start fresh and a power cut is a plain process exit.
#include <sys/mman.h>
#include <sys/wait.h>
#include <cstring>
#include "gq_test.h"
#include "gq_types.h"
#include "store.h"
#include "reclog.h"
#include "slotlog.h"

static const uint32_t BASE = 0x80000u;
struct Shared { uint8_t flash[3 * 4096]; uint32_t sig_loaded, sig_new, done, loaded_day, loaded_seed, flash_writes, flash_erases, outside; };
static Shared* S;
static int g_cut = -1, g_crashes = 0;
static uint32_t g_ms = 100000;
static GqSettings g_set;
static GqConf g_conf;

uint32_t millis() { return g_ms; }
uint32_t EspClass::getSketchSize() { return 400000; }
uint32_t EspClass::getFlashChipRealSize() { return 4u << 20; }
static bool inside(uint32_t addr, size_t n) { if (addr < BASE || addr + n > BASE + sizeof(S->flash)) { S->outside++; return false; } return true; }
bool EspClass::flashRead(uint32_t addr, uint32_t* data, size_t size) {
  if (!inside(addr, size)) return false;
  memcpy(data, S->flash + (addr - BASE), size);
  return true;
}
bool EspClass::flashWrite(uint32_t addr, uint32_t* data, size_t size) {
  if (!inside(addr, size) || (addr & 3) || (size & 3)) return false;
  const uint8_t* src = (const uint8_t*)data;
  S->flash_writes++;
  for (size_t i = 0; i < size; i++) {
    if (g_cut >= 0 && g_cut-- == 0) _exit(42);
    S->flash[addr - BASE + i] &= src[i];
  }
  return true;
}
bool EspClass::flashEraseSector(uint32_t sector) {
  const uint32_t addr = sector * 4096u;
  if (!inside(addr, 4096)) return false;
  S->flash_erases++;
  for (uint32_t i = 0; i < 4096; i++) {
    if (g_cut >= 0 && g_cut-- == 0) _exit(42);
    S->flash[addr - BASE + i] = 0xFF;
  }
  return true;
}
EspClass ESP;
namespace safety { bool updating() { return false; } int crashes() { return g_crashes; } }
namespace prefs { const GqSettings& settings() { return g_set; } }
namespace config { const GqConf& get() { return g_conf; } }

static int q(int n) { return (int)gc_query(GQ_Q_VILLAGE + n); }
static uint32_t sig() {
  uint32_t h = 2166136261u;
  const int ids[] = {GQ_Q_DAY, GQ_Q_SEED, GQ_Q_LANDFORM, GQ_Q_CLIMATE, GQ_Q_PHASE, GQ_Q_MON_STAGE, GQ_Q_MON_TYPE};
  for (int id : ids) h = (h ^ (uint32_t)gc_query(id)) * 16777619u;
  for (int i = 0; i <= 14; i++) h = (h ^ (uint32_t)q(i)) * 16777619u;
  for (int i = 250; i <= 262; i++) h = (h ^ (uint32_t)q(i)) * 16777619u;
  return h;
}

// What a fork does. BOOT: start the game, begin(), first loop() (loads), report. ADVANCE: boot, then a new world of seed 1000+g runs
// for a while and the next loop() after 21 minutes saves it (the power may fail inside that save). EDIT: boot, then a settings change
// is saved (with crashes = 2 the saved game stays quarantined).
enum { BOOT, ADVANCE, EDIT };
static void child(int mode, int g, int cut, int crashes) {
  g_cut = -1; g_crashes = crashes;
  sim_start(5, 9 * 60);
  store::begin(false);
  g_ms += 2000;
  store::loop();
  S->sig_loaded = sig(); S->loaded_day = (uint32_t)gc_query(GQ_Q_DAY); S->loaded_seed = (uint32_t)gc_query(GQ_Q_SEED);
  if (mode == BOOT) { sim_minutes(3); S->done = 1; _exit(0); }
  if (mode == ADVANCE) {
    gc_new_world((uint32_t)(1000 + g));
    sim_minutes(40);
    S->sig_new = sig();
    g_ms += 21u * 60 * 1000;
  } else {
    g_set.brightness = (uint8_t)(g_set.brightness + 1);
    g_ms += 10000;
  }
  g_cut = cut;
  store::loop();
  g_cut = -1;
  S->done = 1;
  _exit(0);
}

// 0 = the child finished, 1 = the power was cut
static int run(int mode, int g, int cut, int crashes) {
  S->done = 0;
  fflush(stdout);
  pid_t p = fork();
  CHECK(p >= 0);
  if (p == 0) child(mode, g, cut, crashes);
  int st = 0;
  CHECK(waitpid(p, &st, 0) == p);
  if (WIFEXITED(st) && WEXITSTATUS(st) == 42) return 1;
  CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
  return 0;
}

static void make_legacy_slot(uint32_t slot, uint32_t seq, const uint8_t* game192) {
  uint8_t pay[SL_PAYLOAD_BYTES];
  memset(pay, 0, sizeof(pay));
  memcpy(pay, &g_set, sizeof(g_set));
  memcpy(pay + 48, &g_conf, sizeof(g_conf));
  memcpy(pay + 48 + 64, game192, 192);
  uint8_t img[SL_SLOT_BYTES];
  sl_build(img, seq, pay);
  const uint32_t off = (slot / SL_SLOTS_PER_SECTOR) * 4096u + (slot % SL_SLOTS_PER_SECTOR) * SL_SLOT_BYTES;
  memcpy(S->flash + off, img, sizeof(img));
}

int main() {
  S = (Shared*)mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  CHECK(S != MAP_FAILED);
  gqs_defaults(&g_set);
  gqc_defaults(&g_conf);

  // ---- 1. empty flash: the first boot saves, the next boot loads exactly that game
  memset(S->flash, 0xFF, sizeof(S->flash));
  CHECK(run(BOOT, 0, -1, 0) == 0);
  uint32_t first_sig = S->sig_loaded;
  CHECK(S->flash_writes >= 6);                                  // a record is six slots
  CHECK(run(BOOT, 0, -1, 0) == 0);
  CHECK(S->sig_loaded == first_sig);

  // ---- 2. generation after generation: every reboot loads the state that was saved last (30 saves wrap the ring more than twice)
  uint32_t prev = first_sig;
  for (int g = 1; g <= 30; g++) {
    CHECK(run(ADVANCE, g, -1, 0) == 0);
    CHECK(S->sig_loaded == prev);                               // it started from the previous save
    uint32_t now_sig = S->sig_new;
    CHECK(now_sig != prev);
    CHECK(run(BOOT, 0, -1, 0) == 0);
    CHECK(S->sig_loaded == now_sig);
    prev = now_sig;
  }
  CHECK(S->outside == 0);                                       // nothing outside 0x80000..0x82FFF was ever touched
  std::printf("  30 saves: %u flash erases, %u slot writes\n", S->flash_erases, S->flash_writes);

  // ---- 3. power cuts at every kind of point: the loaded game is the previous save, or the new one when the cut came after the last byte
  static uint8_t snap[sizeof(S->flash)];
  int cuts_total = 0, torn = 0, completed = 0;
  for (int g = 100; g < 108; g++) {
    memcpy(snap, S->flash, sizeof(snap));
    uint32_t before = prev;
    CHECK(run(BOOT, 0, -1, 0) == 0 && S->sig_loaded == before);
    static const int cuts[] = {0, 1, 2, 3, 4, 100, 319, 320, 321, 330, 640, 1000, 1700, 1919, 1920, 1921, 2500, 4095, 4096, 4097, 4200, 4500, 5000, 5900, 6015, 6016, 6017, 7000};
    uint32_t nsig = 0;
    for (int cut : cuts) {
      memcpy(S->flash, snap, sizeof(snap));
      int cutres = run(ADVANCE, g, cut, 0);
      nsig = S->sig_new;
      cuts_total++;
      CHECK(run(BOOT, 0, -1, 0) == 0);
      if (cutres) { CHECK(S->sig_loaded == before); torn++; }
      else { CHECK(S->sig_loaded == nsig); completed++; }
      CHECK(S->outside == 0);
    }
    memcpy(S->flash, snap, sizeof(snap));
    CHECK(run(ADVANCE, g, -1, 0) == 0);
    prev = S->sig_new;
    CHECK(run(BOOT, 0, -1, 0) == 0 && S->sig_loaded == prev);
    (void)nsig;
  }
  CHECK(torn > 90 && completed >= 8);
  std::printf("  %d power cuts: %d fell back to the previous complete save, %d finished\n", cuts_total, torn, completed);

  // ---- 4. a torn newest record: damage each of its six slots; the previous complete record loads
  for (int victim = 0; victim < 6; victim++) {
    memcpy(snap, S->flash, sizeof(snap));
    uint32_t newest_sig = prev;
    CHECK(run(ADVANCE, 200 + victim, -1, 0) == 0);
    uint32_t after = S->sig_new;
    RlScan sc; RlPlan pl;
    for (uint32_t c = 0; c < RL_CELLS; c++) {
      RlCell cs; rl_cell_begin(&cs);
      for (uint32_t k = 0; k < RL_SLOTS_PER_CELL; k++) rl_cell_feed(&cs, k, S->flash + rl_slot_offset(c, k));
      uint32_t qs = 0, ls = 0;
      sc.state[c] = (uint8_t)rl_cell_end(&cs, &qs, &ls); sc.seq[c] = qs; sc.lslot[c] = (uint8_t)ls;
    }
    rl_plan(&sc, &pl);
    uint8_t* slot = S->flash + rl_slot_offset((uint32_t)pl.load, (uint32_t)victim);
    for (int i = 40; i < 300; i += 53) slot[i] ^= 0x24;            // damaged bits (a half written slot)
    CHECK(run(BOOT, 0, -1, 0) == 0);
    CHECK(S->sig_loaded == newest_sig);
    (void)after;
    memcpy(S->flash, snap, sizeof(snap));
    CHECK(run(BOOT, 0, -1, 0) == 0 && S->sig_loaded == newest_sig);
  }

  // ---- 5. an r5 record (one slot, game version 4): it loads, the game runs on, the first save replaces it, later boots load the new one
  {
    uint8_t v4[192];
    FILE* f = fopen("test/fixtures/save_v4_r5.bin", "rb");
    CHECK(f && fread(v4, 1, 192, f) == 192);
    fclose(f);
    memset(S->flash, 0xFF, sizeof(S->flash));
    for (uint32_t i = 0; i < 36; i++) { uint8_t junk[192]; memset(junk, (int)i, sizeof(junk)); make_legacy_slot(i, 50 + i, i == 35 ? v4 : junk); }   // a full r5 ring: the newest holds the real game
    CHECK(run(BOOT, 0, -1, 0) == 0);
    CHECK(S->loaded_seed == 777 && S->loaded_day == 9);
    CHECK(run(EDIT, 0, -1, 0) == 0);                              // the settings change is saved: first r6 record (erases a sector)
    CHECK(run(BOOT, 0, -1, 0) == 0);
    CHECK(S->loaded_seed == 777 && S->loaded_day >= 9);           // the upgraded game came back
    uint32_t up = S->sig_loaded;
    CHECK(run(BOOT, 0, -1, 0) == 0 && S->sig_loaded == up);
    // a power cut anywhere during that first save keeps the r5 game
    memset(S->flash, 0xFF, sizeof(S->flash));
    for (uint32_t i = 0; i < 36; i++) { uint8_t junk[192]; memset(junk, (int)i, sizeof(junk)); make_legacy_slot(i, 50 + i, i == 35 ? v4 : junk); }
    memcpy(snap, S->flash, sizeof(snap));
    for (int cut = 0; cut < 6200; cut += 331) {
      memcpy(S->flash, snap, sizeof(snap));
      run(EDIT, 0, cut, 0);
      CHECK(run(BOOT, 0, -1, 0) == 0);
      CHECK(S->loaded_seed == 777 && S->loaded_day >= 9);
    }
  }

  // ---- 5b. an r5 record with the game quarantined (two crashes): settings save, the r5 game bytes are carried over and load later
  {
    uint8_t v4[192];
    FILE* f = fopen("test/fixtures/save_v4_r5.bin", "rb");
    CHECK(f && fread(v4, 1, 192, f) == 192);
    fclose(f);
    memset(S->flash, 0xFF, sizeof(S->flash));
    for (uint32_t i = 0; i < 36; i++) { uint8_t junk[192]; memset(junk, (int)i, sizeof(junk)); make_legacy_slot(i, 50 + i, i == 35 ? v4 : junk); }
    CHECK(run(EDIT, 0, -1, 2) == 0);
    for (int i = 0; i < 3; i++) CHECK(run(EDIT, 0, -1, 2) == 0);
    CHECK(run(BOOT, 0, -1, 0) == 0);
    CHECK(S->loaded_seed == 777 && S->loaded_day == 9);          // the r5 game came through the quarantine
  }

  // ---- 6. after two crashes the saved game is not loaded and not overwritten, but settings still save and the game is carried over
  {
    memset(S->flash, 0xFF, sizeof(S->flash));
    CHECK(run(BOOT, 0, -1, 0) == 0);
    CHECK(run(ADVANCE, 7, -1, 0) == 0);
    uint32_t saved = S->sig_new;
    CHECK(run(BOOT, 0, -1, 2) == 0);
    CHECK(S->sig_loaded != saved);                                // not loaded: a fresh world
    for (int i = 0; i < 4; i++) CHECK(run(EDIT, 0, -1, 2) == 0);  // settings saves, one per boot, wrapping through the ring
    CHECK(run(BOOT, 0, -1, 0) == 0);
    CHECK(S->sig_loaded == saved);                                // the game is still the one saved before the crashes
    for (int i = 0; i < 10; i++) CHECK(run(EDIT, 0, -1, 2) == 0);
    CHECK(run(BOOT, 0, -1, 0) == 0 && S->sig_loaded == saved);
  }
  std::printf("sim_store: all passed\n");
  return 0;
}
