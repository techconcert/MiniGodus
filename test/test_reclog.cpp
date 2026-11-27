// r6 flash record log (6 slots of 320 bytes per record): simulated flash (bits only clear on write, an erase sets a whole sector
// to 0xFF) with power cuts at random bytes during erases and slot writes. After every cut the newest COMPLETE record must load
// (all 6 slots with the same sequence and record crc) and it must be the last finished save, or the one being written if that
// happened to finish. A torn record falls back to the previous complete one; the erase never destroys the newest record.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "../lib/gamecore/src/reclog.h"

#define CHECK(c) do { if (!(c)) { std::printf("CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

static uint8_t flash[RL_SECTORS * RL_SECTOR_BYTES];
static uint32_t rng = 12345;
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint8_t* slot_ptr(uint32_t cell, uint32_t k) { return flash + rl_slot_offset(cell, k); }

static void scan(RlScan* s) {
  for (uint32_t c = 0; c < RL_CELLS; c++) {
    RlCell cs; rl_cell_begin(&cs);
    for (uint32_t k = 0; k < RL_SLOTS_PER_CELL; k++) rl_cell_feed(&cs, k, slot_ptr(c, k));
    uint32_t q = 0, ls = 0;
    s->state[c] = (uint8_t)rl_cell_end(&cs, &q, &ls);
    s->seq[c] = q; s->lslot[c] = (uint8_t)ls;
  }
}

static void record_for(uint32_t n, uint8_t* rec) {
  uint32_t x = n * 2654435761u + 1;
  for (uint32_t i = 0; i < RL_RECORD_BYTES; i++) { x = x * 1664525u + 1013904223u; rec[i] = (uint8_t)(x >> 24); }
}

static void build_slot(uint8_t* slot, const uint8_t* rec, uint32_t k, uint32_t seq, uint32_t rcrc) {
  std::memset(slot, 0xFF, RL_SLOT_BYTES);
  std::memcpy(slot + RL_HEAD_BYTES, rec + k * RL_PART_BYTES, RL_PART_BYTES);
  rl_seal(slot, k, seq, rcrc);
}

// cut < 0: completes. Otherwise the power fails after `cut` units (erase bytes, then slot bytes in order). Returns 1 when complete.
static int do_save(uint32_t n, int cut, int* erased_sector) {
  RlScan sc; scan(&sc);
  RlPlan pl; rl_plan(&sc, &pl);
  uint8_t rec[RL_RECORD_BYTES], slot[RL_SLOT_BYTES];
  record_for(n, rec);
  const uint32_t rcrc = rl_crc_done(rl_crc_feed(rl_crc_init(), rec, RL_RECORD_BYTES));
  int left = cut;
  if (erased_sector) *erased_sector = pl.erase_sector;
  if (pl.erase_sector >= 0) {
    uint8_t* sec = flash + (uint32_t)pl.erase_sector * RL_SECTOR_BYTES;
    for (uint32_t i = 0; i < RL_SECTOR_BYTES; i++) {
      if (cut >= 0 && left-- <= 0) return 0;
      sec[i] = 0xFF;
    }
  }
  for (uint32_t k = 0; k < RL_SLOTS_PER_CELL; k++) {
    build_slot(slot, rec, k, pl.next_seq, rcrc);
    uint8_t* dst = slot_ptr((uint32_t)pl.write_cell, k);
    for (uint32_t i = 0; i < RL_SLOT_BYTES; i++) {
      if (cut >= 0 && left-- <= 0) return 0;
      dst[i] &= slot[i];
    }
  }
  return 1;
}

// Loads the newest complete record into rec (1800 bytes). Returns 0 when there is none.
static int load(uint8_t* rec, uint32_t* seq_out) {
  RlScan sc; scan(&sc);
  RlPlan pl; rl_plan(&sc, &pl);
  if (pl.load < 0 || pl.load_legacy) return 0;
  for (uint32_t k = 0; k < RL_SLOTS_PER_CELL; k++) std::memcpy(rec + k * RL_PART_BYTES, slot_ptr((uint32_t)pl.load, k) + RL_HEAD_BYTES, RL_PART_BYTES);
  if (seq_out) *seq_out = pl.next_seq - 1;
  return 1;
}

static int is_record(const uint8_t* got, uint32_t n) { uint8_t want[RL_RECORD_BYTES]; record_for(n, want); return std::memcmp(got, want, RL_RECORD_BYTES) == 0; }

static void test_basic() {
  std::memset(flash, 0xFF, sizeof(flash));
  uint8_t got[RL_RECORD_BYTES];
  CHECK(!load(got, nullptr));
  for (uint32_t n = 1; n <= 300; n++) {
    CHECK(do_save(n, -1, nullptr));
    uint32_t q = 0;
    CHECK(load(got, &q) && q == n && is_record(got, n));
  }
}

static void test_cuts(uint32_t seed, int iterations) {
  rng = seed;
  std::memset(flash, 0xFF, sizeof(flash));
  uint32_t committed = 0;
  int erases = 0, fallbacks = 0;
  for (int it = 1; it <= iterations; it++) {
    uint32_t n = (uint32_t)it;
    int cut = -1;
    if (rnd() % 3 == 0) cut = (int)(rnd() % (RL_SECTOR_BYTES + RL_SLOTS_PER_CELL * RL_SLOT_BYTES + 8));   // sometimes beyond the end: completes
    int es = -1;
    int done = do_save(n, cut, &es);
    if (es >= 0) erases++;
    if (done) committed = n;
    uint8_t got[RL_RECORD_BYTES];
    int have = load(got, nullptr);
    if (committed == 0) {
      if (have) { CHECK(is_record(got, n)); committed = n; }
    } else {
      CHECK(have);
      if (!is_record(got, committed)) { CHECK(is_record(got, n)); committed = n; }
      else if (!done) fallbacks++;                  // a torn record: the previous complete one was loaded
    }
    if (committed && rnd() % 7 == 0) {                           // a second cut right at the next boot is harmless: nothing is written by loading
      CHECK(load(got, nullptr) && is_record(got, committed));
    }
  }
  CHECK(erases > 0 && fallbacks > 0);
}

// Tearing ANY single slot of the newest record (bit damage anywhere in it) leaves the previous complete record.
static void test_every_slot_torn() {
  for (uint32_t victim = 0; victim < RL_SLOTS_PER_CELL; victim++) {
    for (uint32_t extra = 0; extra < 3; extra++) {
      std::memset(flash, 0xFF, sizeof(flash));
      uint32_t last = 7 + extra * 5;                   // vary the ring position (erase or not)
      for (uint32_t n = 1; n <= last; n++) CHECK(do_save(n, -1, nullptr));
      RlScan sc; scan(&sc); RlPlan pl; rl_plan(&sc, &pl);
      const int newest = pl.load;
      for (uint32_t pos = 0; pos < RL_SLOT_BYTES; pos += 37) {
        uint8_t keep = slot_ptr((uint32_t)newest, victim)[pos];
        slot_ptr((uint32_t)newest, victim)[pos] = (uint8_t)(keep & 0x5A);       // bits cleared, as a half-written cell would
        uint8_t got[RL_RECORD_BYTES];
        CHECK(load(got, nullptr));
        if (keep == (uint8_t)(keep & 0x5A)) CHECK(is_record(got, last));
        else CHECK(is_record(got, last - 1));
        slot_ptr((uint32_t)newest, victim)[pos] = keep;
      }
    }
  }
}

// A record made of slots from two different saves (one slot of an old record spliced into the newest cell) is never valid.
static void test_mixed_slots() {
  std::memset(flash, 0xFF, sizeof(flash));
  for (uint32_t n = 1; n <= 9; n++) CHECK(do_save(n, -1, nullptr));
  RlScan sc; scan(&sc); RlPlan pl; rl_plan(&sc, &pl);
  const int newest = pl.load;
  const int prev = (newest + RL_CELLS - 1) % RL_CELLS;
  uint8_t keep[RL_SLOT_BYTES];
  for (uint32_t k = 0; k < RL_SLOTS_PER_CELL; k++) {
    std::memcpy(keep, slot_ptr((uint32_t)newest, k), RL_SLOT_BYTES);
    std::memcpy(slot_ptr((uint32_t)newest, k), slot_ptr((uint32_t)prev, k), RL_SLOT_BYTES);   // same part number, older seq and crc
    uint8_t got[RL_RECORD_BYTES];
    CHECK(load(got, nullptr));
    CHECK(is_record(got, 8));
    std::memcpy(slot_ptr((uint32_t)newest, k), keep, RL_SLOT_BYTES);
  }
}

// The erase never touches the newest record or the one before it: stop the power at every byte of an erase.
static void test_erase_safe() {
  std::memset(flash, 0xFF, sizeof(flash));
  uint32_t n = 0;
  int tested = 0;
  while (tested < 12) {
    n++;
    RlScan sc; scan(&sc); RlPlan pl; rl_plan(&sc, &pl);
    if (pl.erase_sector >= 0 && n > 6) {
      static uint8_t save[sizeof(flash)];
      std::memcpy(save, flash, sizeof(flash));
      for (int cut = 0; cut < (int)RL_SECTOR_BYTES + 400; cut += 211) {
        std::memcpy(flash, save, sizeof(flash));
        CHECK(!do_save(n, cut, nullptr) || cut >= (int)RL_SECTOR_BYTES + 6 * (int)RL_SLOT_BYTES);
        uint8_t got[RL_RECORD_BYTES];
        CHECK(load(got, nullptr) && is_record(got, n - 1));
        // and the one before it is also still there (the torn save must not have cost two records)
        RlScan s2; scan(&s2);
        int valid = 0;
        for (uint32_t c = 0; c < RL_CELLS; c++) valid += s2.state[c] == RL_VALID;
        CHECK(valid >= 2);
      }
      std::memcpy(flash, save, sizeof(flash));
      tested++;
    }
    CHECK(do_save(n, -1, nullptr));
  }
}

static void test_wear() {
  std::memset(flash, 0xFF, sizeof(flash));
  int er[RL_SECTORS] = {0, 0, 0};
  for (int n = 1; n <= 3600; n++) {
    RlScan sc; scan(&sc); RlPlan pl; rl_plan(&sc, &pl);
    if (pl.erase_sector >= 0) er[pl.erase_sector]++;
    CHECK(do_save((uint32_t)n, -1, nullptr));
  }
  // 3600 records: each sector erased once per 6 records = 600 times, evenly (a record is 6 slots, a sector holds 2 records)
  for (uint32_t s = 0; s < RL_SECTORS; s++) CHECK(er[s] >= 599 && er[s] <= 601);
  std::printf("  wear: 3600 saves -> erases per sector %d %d %d (one erase per 6 saves; at a save every 20 minutes a sector sees ~12 erases a day)\n", er[0], er[1], er[2]);
}

static void test_junk_start() {
  for (uint32_t seed = 1; seed <= 20; seed++) {
    rng = seed * 77;
    for (uint32_t i = 0; i < sizeof(flash); i++) flash[i] = (uint8_t)rnd();
    uint8_t got[RL_RECORD_BYTES];
    CHECK(!load(got, nullptr));
    for (uint32_t n = 1; n <= 40; n++) { CHECK(do_save(n, -1, nullptr)); CHECK(load(got, nullptr) && is_record(got, n)); }
  }
}

// r5 records (format 1, one slot each) in the flash: the newest one is found, the first r6 save goes beside it and wins.
static void make_legacy(uint32_t cell_slot, uint32_t seq, uint8_t fill) {
  uint8_t* s = flash + (cell_slot / RL_SLOTS_PER_SECTOR) * RL_SECTOR_BYTES + (cell_slot % RL_SLOTS_PER_SECTOR) * RL_SLOT_BYTES;
  std::memset(s, fill, RL_SLOT_BYTES);
  uint32_t m = RL_MAGIC; std::memcpy(s, &m, 4);
  s[8] = 1; s[9] = 0; s[10] = 0x30; s[11] = 0x01;
  std::memcpy(s + 12, &seq, 4);
  uint32_t x = 0xFFFFFFFFu;
  for (uint32_t i = 8; i < RL_SLOT_BYTES; i++) { x ^= s[i]; for (int b = 0; b < 8; b++) x = (x >> 1) ^ (0xEDB88320u & (0u - (x & 1u))); }
  x ^= 0xFFFFFFFFu;
  std::memcpy(s + 4, &x, 4);
}

static void test_legacy() {
  std::memset(flash, 0xFF, sizeof(flash));
  for (uint32_t i = 0; i < 36; i++) make_legacy(i, 100 + i, (uint8_t)i);      // a full r5 ring, newest = slot 35
  RlScan sc; scan(&sc); RlPlan pl; rl_plan(&sc, &pl);
  CHECK(pl.load >= 0 && pl.load_legacy && pl.next_seq == 136 && pl.load_slot == 5 && pl.load == 5);
  CHECK(pl.erase_sector == 0);                                                // the sector after the newest one's (sector 2)
  uint8_t got[RL_RECORD_BYTES];
  for (int cut = 0; cut < 4500; cut += 173) {                                  // a power cut anywhere in the first save: the r5 record still loads
    static uint8_t keep[sizeof(flash)];
    std::memcpy(keep, flash, sizeof(flash));
    do_save(1, cut, nullptr);
    RlScan s2; scan(&s2); RlPlan p2; rl_plan(&s2, &p2);
    CHECK(p2.load >= 0);
    CHECK(p2.load_legacy || (!p2.load_legacy && load(got, nullptr) && is_record(got, 1)));
    std::memcpy(flash, keep, sizeof(flash));
  }
  CHECK(do_save(1, -1, nullptr));
  CHECK(load(got, nullptr) && is_record(got, 1));
  for (uint32_t n = 2; n < 40; n++) { CHECK(do_save(n, -1, nullptr)); CHECK(load(got, nullptr) && is_record(got, n)); }
}

int main() {
  test_basic();
  test_every_slot_torn();
  test_mixed_slots();
  test_erase_safe();
  test_wear();
  test_junk_start();
  test_legacy();
  for (uint32_t seed = 1; seed <= 40; seed++) test_cuts(seed, 3000);
  std::printf("test_reclog: all passed\n");
  return 0;
}
