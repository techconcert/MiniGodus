#include "reclog.h"

static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint32_t rd16(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static void wr32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

uint32_t rl_crc_init(void) { return 0xFFFFFFFFu; }
uint32_t rl_crc_feed(uint32_t s, const uint8_t* p, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    s ^= p[i];
    for (int b = 0; b < 8; b++) s = (s >> 1) ^ (0xEDB88320u & (0u - (s & 1u)));
  }
  return s;
}
uint32_t rl_crc_done(uint32_t s) { return s ^ 0xFFFFFFFFu; }
static uint32_t crc_all(const uint8_t* p, uint32_t n) { return rl_crc_done(rl_crc_feed(rl_crc_init(), p, n)); }

void rl_seal(uint8_t* slot, uint32_t part, uint32_t seq, uint32_t record_crc) {
  wr32(slot, RL_MAGIC);
  slot[8] = (uint8_t)RL_FMT_CELL; slot[9] = 0;
  slot[10] = (uint8_t)part; slot[11] = 0;
  wr32(slot + 12, seq);
  wr32(slot + 16, record_crc);
  wr32(slot + 4, crc_all(slot + 8, RL_SLOT_BYTES - 8));
}

void rl_cell_begin(RlCell* c) {
  c->seq = c->record_crc = c->legacy_seq = 0;
  c->running = rl_crc_init();
  c->blank = c->bad = c->v2 = c->v1 = c->legacy_slot = 0;
}

void rl_cell_feed(RlCell* c, uint32_t k, const uint8_t* slot) {
  int blank = 1;
  for (uint32_t i = 0; i < RL_SLOT_BYTES; i++) if (slot[i] != 0xFF) { blank = 0; break; }
  if (blank) { c->blank++; c->bad = 1; return; }
  if (rd32(slot) != RL_MAGIC || rd32(slot + 4) != crc_all(slot + 8, RL_SLOT_BYTES - 8)) { c->bad = 1; return; }
  const uint32_t fmt = rd16(slot + 8);
  if (fmt == RL_FMT_ONE) {
    if (rd16(slot + 10) != 304u) { c->bad = 1; return; }
    c->bad = 1;                                   // a cell holding r5 slots is not a record
    const uint32_t q = rd32(slot + 12);
    if (!c->v1 || q > c->legacy_seq) { c->legacy_seq = q; c->legacy_slot = (uint8_t)k; }
    c->v1++;
    return;
  }
  if (fmt != RL_FMT_CELL || rd16(slot + 10) != k) { c->bad = 1; return; }
  const uint32_t q = rd32(slot + 12), rc = rd32(slot + 16);
  if (c->v2 == 0) { c->seq = q; c->record_crc = rc; }
  else if (q != c->seq || rc != c->record_crc) c->bad = 1;
  c->v2++;
  c->running = rl_crc_feed(c->running, slot + RL_HEAD_BYTES, RL_PART_BYTES);
}

int rl_cell_end(RlCell* c, uint32_t* seq, uint32_t* legacy_slot) {
  if (c->blank == RL_SLOTS_PER_CELL) return RL_BLANK;
  if (!c->bad && c->v2 == RL_SLOTS_PER_CELL && rl_crc_done(c->running) == c->record_crc) { *seq = c->seq; return RL_VALID; }
  if (c->v2 == 0 && c->v1) { *seq = c->legacy_seq; *legacy_slot = c->legacy_slot; return RL_LEGACY; }
  return RL_JUNK;
}

void rl_plan(const RlScan* s, RlPlan* p) {
  int newest = -1;
  for (uint32_t i = 0; i < RL_CELLS; i++)
    if ((s->state[i] == RL_VALID || s->state[i] == RL_LEGACY) && (newest < 0 || s->seq[i] > s->seq[newest])) newest = (int)i;
  p->load = newest;
  p->load_legacy = newest >= 0 && s->state[newest] == RL_LEGACY;
  p->load_slot = p->load_legacy ? s->lslot[newest] : 0;
  p->erase_sector = -1;
  p->next_seq = newest < 0 ? 1u : s->seq[newest] + 1u;
  const uint32_t start = newest < 0 ? 0u : (uint32_t)newest + 1u;
  for (uint32_t k = 0; k < RL_CELLS; k++) {
    uint32_t i = (start + k) % RL_CELLS;
    if (s->state[i] == RL_BLANK) { p->write_cell = (int)i; return; }
  }
  const uint32_t sec = newest < 0 ? 0u : ((uint32_t)newest / RL_CELLS_PER_SECTOR + 1u) % RL_SECTORS;
  p->erase_sector = (int)sec;
  p->write_cell = (int)(sec * RL_CELLS_PER_SECTOR);
}

void rl_applied(RlScan* s, const RlPlan* p, int ok) {
  if (p->erase_sector >= 0)
    for (uint32_t i = 0; i < RL_CELLS_PER_SECTOR; i++) s->state[(uint32_t)p->erase_sector * RL_CELLS_PER_SECTOR + i] = RL_BLANK;
  const uint32_t w = (uint32_t)p->write_cell;
  if (ok) { s->state[w] = RL_VALID; s->seq[w] = p->next_seq; }
  else s->state[w] = RL_JUNK;
}
