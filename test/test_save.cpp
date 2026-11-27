// Save format (version 5, 1688 bytes in 6 parts): round trip, rejection of a damaged head, total import of damaged bodies,
// the version 4 record of r4 and r5 (loads, then upgrades), the part by part API the firmware uses, and a magic that is not GeekQuest's.
#include "gq_test.h"
#include "../lib/gamecore/src/gq_types.h"

static uint32_t crc(const uint8_t* p, int n) {
  uint32_t c = 0xFFFFFFFFu;
  for (int i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
  return ~c;
}
static void seal(uint8_t* b, int len) { uint32_t c = crc(b + 12, len - 12); std::memcpy(b + 8, &c, 4); }
static int q(int n) { return (int)gc_query(GQ_Q_VILLAGE + n); }
static uint8_t buf[GC_SAVE_TOTAL], buf2[GC_SAVE_TOTAL];

// everything that is saved, as numbers
static uint32_t state_sig() {
  uint32_t h = 2166136261u;
  const int ids[] = {GQ_Q_DAY, GQ_Q_SEED, GQ_Q_MINUTE, GQ_Q_LANDFORM, GQ_Q_CLIMATE, GQ_Q_PHASE, GQ_Q_MON_STAGE, GQ_Q_MON_TYPE};
  for (int id : ids) { h = (h ^ (uint32_t)gc_query(id)) * 16777619u; }
  for (int i = 0; i <= 14; i++) { h = (h ^ (uint32_t)q(i)) * 16777619u; }
  for (int i = 250; i <= 262; i++) { h = (h ^ (uint32_t)q(i)) * 16777619u; }
  return h;
}

static void load_file(const char* path, uint8_t* out, int n) {
  FILE* f = std::fopen(path, "rb");
  CHECK(f != nullptr);
  CHECK((int)std::fread(out, 1, (size_t)n, f) == n);
  std::fclose(f);
}

int main() {
  CHECK(GC_SAVE_BYTES == 192 && GQ_SAVE_SIZE == GC_SAVE_BYTES);
  CHECK(GC_SAVE_TOTAL == 1688 && GC_SAVE_PARTS == 6 && GC_SAVE_PART0 + 5 * GC_SAVE_PARTN == GC_SAVE_TOTAL);
  CHECK(GQ_SAVE_VERSION == 5 && GQ_SAVE_VERSION_V4 == 4);
  CHECK(GQ_SAVE_MAGIC != 0x53514747u);                     // GeekQuest's magic ("GGQS") in the shared flash area must differ

  gc_init(GC_SCENE_TEST, 1, 0);
  CHECK(gc_save_export(buf, sizeof buf) == 0);              // only the MiniGodus scene has a game to keep
  sim_start(99, 9 * 60);
  sim_days(5);
  CHECK(gc_save_export(buf, 100) == 0);                     // too small a buffer
  CHECK(gc_save_export(buf, sizeof buf) == GC_SAVE_TOTAL);
  uint32_t magic; std::memcpy(&magic, buf, 4);
  CHECK(magic == GQ_SAVE_MAGIC);
  CHECK(q(0) >= 2);

  // round trip: a new game accepts it and shows the same world, day, people, buildings and map
  uint32_t sig = state_sig();
  int day = (int)gc_query(GQ_Q_DAY), seed = (int)gc_query(GQ_Q_SEED);
  uint8_t again[GC_SAVE_TOTAL];
  sim_start(5, 9 * 60);
  CHECK(gc_query(GQ_Q_SEED) != seed);
  CHECK(gc_save_import(buf, GC_SAVE_TOTAL) == 1);
  CHECK((int)gc_query(GQ_Q_DAY) == day && (int)gc_query(GQ_Q_SEED) == seed);
  CHECK(state_sig() == sig);
  CHECK(gc_save_export(again, sizeof again) == GC_SAVE_TOTAL);
  // the same state exports the same bytes (apart from the record number in the head and its checksum)
  int diff = 0;
  for (int i = 0; i < GC_SAVE_TOTAL; i++) if (buf[i] != again[i]) diff++;
  if (diff > 24) { for (int i = 0; i < GC_SAVE_TOTAL; i++) if (buf[i] != again[i]) std::printf("diff at %d\n", i); }
  CHECK(diff <= 24);
  CHECK(std::memcmp(buf + GC_SAVE_PART0, again + GC_SAVE_PART0, GC_SAVE_TOTAL - GC_SAVE_PART0) == 0);   // villagers, buildings, map: identical

  // the part by part API (what store.cpp uses) gives the same as the one block
  sim_start(7);
  gc_save_begin();
  int at = 0;
  for (int k = 0; k < GC_SAVE_PARTS; k++) { int n = gc_save_part_size(k); CHECK(n == (k ? GC_SAVE_PARTN : GC_SAVE_PART0)); CHECK(gc_save_export_part(k, again + at, n) == n); at += n; }
  CHECK(at == GC_SAVE_TOTAL && gc_save_part_size(6) == 0 && gc_save_part_size(-1) == 0);
  sim_start(5);
  CHECK(gc_save_import_part(0, again, GC_SAVE_PART0) == 1);
  for (int k = 1; k < GC_SAVE_PARTS; k++) CHECK(gc_save_import_part(k, again + GC_SAVE_PART0 + (k - 1) * GC_SAVE_PARTN, GC_SAVE_PARTN) == 1);
  gc_save_import_end();
  CHECK(gc_save_import_part(1, again, GC_SAVE_PARTN) == 0);   // no import is open any more
  sim_minutes(10);

  // a GeekQuest save is refused, and changes nothing
  std::memcpy(buf2, buf, sizeof buf2);
  uint32_t gqmagic = 0x53514747u; std::memcpy(buf2, &gqmagic, 4); seal(buf2, GQ_SAVE_HEAD);
  sim_start(7);
  int d0 = (int)gc_query(GQ_Q_DAY), s0 = (int)gc_query(GQ_Q_SEED);
  CHECK(gc_save_import(buf2, GC_SAVE_TOTAL) == 0);
  CHECK((int)gc_query(GQ_Q_DAY) == d0 && (int)gc_query(GQ_Q_SEED) == s0);

  // every single flipped bit in the head (bytes 0..187: header, world, stats, seed history) is rejected, every wrong length too
  for (int bit = 0; bit < GQ_SAVE_HEAD * 8; bit++) {
    std::memcpy(buf2, buf, sizeof buf2);
    buf2[bit / 8] ^= (uint8_t)(1 << (bit % 8));
    CHECK(gc_save_import(buf2, GC_SAVE_TOTAL) == 0);
    CHECK((int)gc_query(GQ_Q_SEED) == s0);
  }
  CHECK(gc_save_import(buf, GC_SAVE_TOTAL - 1) == 0);
  CHECK(gc_save_import(buf, GC_SAVE_TOTAL + 1) == 0);
  CHECK(gc_save_import(buf, GC_SAVE_BYTES - 1) == 0);
  CHECK(gc_save_import(buf, 0) == 0);
  CHECK(gc_save_import(nullptr, GC_SAVE_TOTAL) == 0);

  // import is total: a damaged body (villagers, buildings, map) with an intact head always leaves a playable state
  uint32_t r = 0x9E3779B9u;
  for (int i = 0; i < 150; i++) {
    std::memcpy(buf2, buf, sizeof buf2);
    int from = GQ_SAVE_HEAD + (int)(r % (uint32_t)(GC_SAVE_TOTAL - GQ_SAVE_HEAD));
    for (int k = from; k < GC_SAVE_TOTAL && k < from + 1 + (i % 7) * 40; k++) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; buf2[k] = (uint8_t)r; }
    r ^= r << 13; r ^= r >> 17; r ^= r << 5;
    sim_start(3);
    CHECK(gc_save_import(buf2, GC_SAVE_TOTAL) == 1);
    int m = (int)gc_query(GQ_Q_MINUTE);
    CHECK(m >= 0 && m <= 1439);
    sim_minutes(2);
    CHECK(gc_save_export(buf2, sizeof buf2) == GC_SAVE_TOTAL);
    CHECK(gc_save_import(buf2, GC_SAVE_TOTAL) == 1);        // what we export we accept
  }
  // ... and so is a random head body with a valid header and CRC
  for (int i = 0; i < 300; i++) {
    for (int k = 0; k < GC_SAVE_TOTAL; k++) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; buf2[k] = (uint8_t)r; }
    std::memcpy(buf2, &magic, 4);
    uint16_t v = GQ_SAVE_VERSION, sz = GQ_SAVE_HEAD; std::memcpy(buf2 + 4, &v, 2); std::memcpy(buf2 + 6, &sz, 2);
    seal(buf2, GQ_SAVE_HEAD);
    sim_start(3);
    CHECK(gc_save_import(buf2, GC_SAVE_TOTAL) == 1);
    CHECK(gc_query(GQ_Q_SEED) != 0 && (int)gc_query(GQ_Q_MINUTE) <= 1439);
    sim_minutes(2);
  }

  // the version 4 record of r4 and r5 (192 bytes, fixture made by the r5 build: seed 777, day 9) loads and then upgrades
  uint8_t v4[GC_SAVE_BYTES];
  load_file("test/fixtures/save_v4_r5.bin", v4, GC_SAVE_BYTES);
  sim_start(3);
  CHECK(gc_save_import(v4, GC_SAVE_BYTES) == 1);
  CHECK((int)gc_query(GQ_Q_SEED) == 777 && (int)gc_query(GQ_Q_DAY) == 9);
  CHECK(q(0) >= 2 && q(0) <= 48);
  CHECK((int)gc_query(GQ_Q_PHASE) == 0);                    // an old world is still in play
  sim_days(2);                                              // it keeps running
  CHECK(q(0) >= 1);
  uint32_t sig4 = state_sig();
  CHECK(gc_save_export(buf, sizeof buf) == GC_SAVE_TOTAL);  // upgraded: the next save is version 5
  uint16_t ver; std::memcpy(&ver, buf + 4, 2);
  CHECK(ver == GQ_SAVE_VERSION);
  sim_start(3);
  CHECK(gc_save_import(buf, GC_SAVE_TOTAL) == 1);
  CHECK(state_sig() == sig4);
  for (int bit = 0; bit < GC_SAVE_BYTES * 8; bit++) {       // a damaged v4 record is rejected as before
    std::memcpy(buf2, v4, GC_SAVE_BYTES);
    buf2[bit / 8] ^= (uint8_t)(1 << (bit % 8));
    CHECK(gc_save_import(buf2, GC_SAVE_BYTES) == 0);
  }

  // other scenes do not import
  gc_init(GC_SCENE_INFO, 1, 0);
  CHECK(gc_save_import(buf, GC_SAVE_TOTAL) == 0);
  CHECK(gc_save_import(v4, GC_SAVE_BYTES) == 0);

  gc_reset_game();
  std::printf("test_save: all passed\n");
  return 0;
}
