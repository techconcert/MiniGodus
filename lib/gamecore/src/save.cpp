// Save format helpers: CRC32, validate, sanitize. Pure functions of their arguments.
// MiniGodus has its own magic (GQ_SAVE_MAGIC), different from GeekQuest's, so a GeekQuest save found in the shared
// flash record is rejected by gq_save_validate (GC_SAVE_ERR_MAGIC) and the other way round.
#include "gc_internal.h"

uint32_t gq_save_crc(const uint8_t* p, int n) {
  uint32_t c = 0xFFFFFFFFu;
  for (int i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

static uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// Checks length, magic, version, size field and CRC. No side effects.
int gq_save_validate(const uint8_t* buf, int len) {
  if (!buf || len != GQ_SAVE_SIZE) return GC_SAVE_ERR_LEN;
  if (rd32(buf) != GQ_SAVE_MAGIC) return GC_SAVE_ERR_MAGIC;
  if (rd16(buf + 4) != GQ_SAVE_VERSION) return GC_SAVE_ERR_VERSION;
  if (rd16(buf + 6) != GQ_SAVE_SIZE) return GC_SAVE_ERR_LEN;
  if (rd32(buf + 8) != gq_save_crc(buf + 12, GQ_SAVE_SIZE - 12)) return GC_SAVE_ERR_CRC;
  return GC_SAVE_OK;
}

// Total: any input leaves a valid state.
void gq_save_sanitize(GqSave* s) {
  s->magic = GQ_SAVE_MAGIC;
  s->version = GQ_SAVE_VERSION;
  s->size = GQ_SAVE_SIZE;
  if (s->rng == 0) s->rng = 0x1234567u;
  if (s->seed == 0) s->seed = 1;
  if (s->minute > 1439) s->minute = 1439;
  for (int i = 0; i < (int)sizeof(s->reserved); i++) s->reserved[i] = 0;
}
