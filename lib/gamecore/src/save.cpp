// Save format helpers: CRC32, validate, sanitize. Pure functions of their arguments.
// MiniGodus has its own magic (GQ_SAVE_MAGIC), different from GeekQuest's, so a GeekQuest save found in the shared
// flash record is rejected by gq_save_validate (GC_SAVE_ERR_MAGIC) and the other way round.
#include "gc_internal.h"
#include "world.h"

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
  if (!buf || (len != GQ_SAVE_SIZE && len != GQ_SAVE_HEAD)) return GC_SAVE_ERR_LEN;
  if (rd32(buf) != GQ_SAVE_MAGIC) return GC_SAVE_ERR_MAGIC;
  int v = rd16(buf + 4);
  if (v != (len == GQ_SAVE_SIZE ? GQ_SAVE_VERSION_V4 : GQ_SAVE_VERSION)) return GC_SAVE_ERR_VERSION;
  if (rd16(buf + 6) != len) return GC_SAVE_ERR_LEN;
  if (rd32(buf + 8) != gq_save_crc(buf + 12, len - 12)) return GC_SAVE_ERR_CRC;
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
  if (s->pad0 > 8) s->pad0 = 0;      // r8: the robe colour of this world + 1 (0 = not recorded: the seed gives one); reserved holds the village, village_unpack checks it itself
  if (s->hist_n > 4) s->hist_n = 4;
  for (int i = 0; i < 4; i++) {
    if (s->hist_land[i] >= LF_COUNT) s->hist_land[i] = 0;
    if (s->hist_clim[i] >= CL_COUNT) s->hist_clim[i] = 0;
  }
  if (s->hist_n == 0) {      // no world recorded: the one the seed gives
    int l, c;
    world_pick(s->seed, -1, -1, &l, &c);
    s->hist_n = 1; s->hist_seed[0] = s->seed; s->hist_land[0] = (uint8_t)l; s->hist_clim[0] = (uint8_t)c;
  }
  s->hist_seed[0] = s->seed;
  s->landform = s->hist_land[0];
  s->climate = s->hist_clim[0];
}
