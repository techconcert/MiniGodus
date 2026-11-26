// Save format: round trip, rejection of every damaged input, total import, and a magic that is not GeekQuest's.
#include "gq_test.h"
#include "../lib/gamecore/src/gq_types.h"

static uint32_t crc(const uint8_t* p, int n) {
  uint32_t c = 0xFFFFFFFFu;
  for (int i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
  return ~c;
}
static void seal(uint8_t* b) { uint32_t c = crc(b + 12, GC_SAVE_BYTES - 12); std::memcpy(b + 8, &c, 4); }

int main() {
  CHECK(GC_SAVE_BYTES == 192 && GQ_SAVE_SIZE == GC_SAVE_BYTES);
  // GeekQuest's save magic (0x53514747, "GGQS") in the same flash area: this one must differ
  CHECK(GQ_SAVE_MAGIC != 0x53514747u);

  uint8_t buf[GC_SAVE_BYTES], buf2[GC_SAVE_BYTES];
  gc_init(GC_SCENE_TEST, 1, 0);
  CHECK(gc_save_export(buf, sizeof buf) == 0);              // only the MiniGodus scene has a game to keep
  sim_start(99, 9 * 60);
  sim_days(2);
  CHECK(gc_save_export(buf, 100) == 0);                     // too small a buffer
  CHECK(gc_save_export(buf, sizeof buf) == GC_SAVE_BYTES);
  uint32_t magic; std::memcpy(&magic, buf, 4);
  CHECK(magic == GQ_SAVE_MAGIC);

  // round trip: a new game accepts it and shows the same world and day
  uint32_t h = frame_hash();
  int day = (int)gc_query(GQ_Q_DAY), seed = (int)gc_query(GQ_Q_SEED);
  sim_start(5, 9 * 60);
  CHECK(gc_query(GQ_Q_SEED) != seed);
  CHECK(gc_save_import(buf, GC_SAVE_BYTES) == 1);
  CHECK((int)gc_query(GQ_Q_DAY) == day && (int)gc_query(GQ_Q_SEED) == seed);
  CHECK(frame_hash() != 0);
  sim_start(99, 9 * 60);
  (void)h;

  // a GeekQuest save is refused, and changes nothing
  uint8_t gq[GC_SAVE_BYTES];
  std::memcpy(gq, buf, sizeof gq);
  uint32_t gqmagic = 0x53514747u; std::memcpy(gq, &gqmagic, 4); seal(gq);
  sim_start(7);
  int d0 = (int)gc_query(GQ_Q_DAY), s0 = (int)gc_query(GQ_Q_SEED);
  CHECK(gc_save_import(gq, GC_SAVE_BYTES) == 0);
  CHECK((int)gc_query(GQ_Q_DAY) == d0 && (int)gc_query(GQ_Q_SEED) == s0);

  // every single flipped bit and every wrong length is rejected
  for (int bit = 0; bit < GC_SAVE_BYTES * 8; bit++) {
    std::memcpy(buf2, buf, sizeof buf);
    buf2[bit / 8] ^= (uint8_t)(1 << (bit % 8));
    CHECK(gc_save_import(buf2, GC_SAVE_BYTES) == 0);
    CHECK((int)gc_query(GQ_Q_SEED) == s0);
  }
  CHECK(gc_save_import(buf, GC_SAVE_BYTES - 1) == 0);
  CHECK(gc_save_import(buf, GC_SAVE_BYTES + 1) == 0);
  CHECK(gc_save_import(buf, 0) == 0);
  CHECK(gc_save_import(nullptr, GC_SAVE_BYTES) == 0);

  // import is total: random bytes with a valid header and CRC leave a valid state
  uint32_t r = 0x9E3779B9u;
  for (int i = 0; i < 2000; i++) {
    for (int k = 0; k < GC_SAVE_BYTES; k++) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; buf2[k] = (uint8_t)r; }
    std::memcpy(buf2, &magic, 4);
    uint16_t v = GQ_SAVE_VERSION, sz = GQ_SAVE_SIZE; std::memcpy(buf2 + 4, &v, 2); std::memcpy(buf2 + 6, &sz, 2);
    seal(buf2);
    sim_start(3);
    CHECK(gc_save_import(buf2, GC_SAVE_BYTES) == 1);
    int m = (int)gc_query(GQ_Q_MINUTE);
    CHECK(m >= 0 && m <= 1439);
    CHECK(gc_query(GQ_Q_SEED) != 0);
    sim_minutes(2);
    CHECK(gc_save_export(buf, sizeof buf) == GC_SAVE_BYTES);
    CHECK(gc_save_import(buf, GC_SAVE_BYTES) == 1);       // what we export we accept
  }
  // other scenes do not import
  gc_init(GC_SCENE_INFO, 1, 0);
  CHECK(gc_save_import(buf, GC_SAVE_BYTES) == 0);

  gc_reset_game();
  std::printf("test_save: all passed\n");
  return 0;
}
