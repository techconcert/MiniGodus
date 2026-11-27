// See store.h.
#include "store.h"
#include "reclog.h"
#include "prefs.h"
#include "config.h"
#include "safety.h"
#include "gamecore.h"
#include "gq_types.h"

namespace store {

static const uint32_t BASE = 0x80000UL;          // first of three 4 KB sectors
static const uint32_t MAX_IMAGE = 0x7D000UL;     // a larger image could reach the store
static const uint32_t OFF_SET = RL_HEAD_BYTES, OFF_CONF = RL_HEAD_BYTES + GQS_SETTINGS_BYTES, OFF_GAME0 = RL_HEAD_BYTES + GQS_SETTINGS_BYTES + 64;
static const uint32_t OFF_ONE_SET = 16, OFF_ONE_CONF = 16 + GQS_SETTINGS_BYTES, OFF_ONE_GAME = 16 + GQS_SETTINGS_BYTES + 64;   // r5 record
static const uint32_t GAME_EVERY_MS = 20UL * 60 * 1000;
static const uint32_t SETTINGS_WAIT_MS = 5000;
static const uint32_t DAY_WAIT_MS = 60UL * 1000;

static_assert(GQS_SETTINGS_BYTES + 64 + GC_SAVE_PART0 == RL_PART_BYTES, "slot 0: settings, config and the first game part");
static_assert(RL_HEAD_BYTES + RL_PART_BYTES == RL_SLOT_BYTES, "slot layout");
static_assert(GC_SAVE_PARTN == RL_PART_BYTES && GC_SAVE_PARTS == RL_SLOTS_PER_CELL, "one game part per slot");
static_assert(GQS_SETTINGS_BYTES + 64 + GC_SAVE_TOTAL == RL_RECORD_BYTES, "record size");
static_assert(sizeof(GqConf) == 64, "GqConf is 64 bytes");
static_assert(OFF_ONE_GAME + GC_SAVE_BYTES == RL_SLOT_BYTES, "r5 record layout");

alignas(4) static uint8_t s_slot[RL_SLOT_BYTES];  // scratch for one slot: scanning, loading, then the record being written
static RlScan s_scan;
static RlPlan s_boot;                              // the plan made at boot: where the loaded record is
static GqSettings s_set;                           // the settings and the config of the loaded record (the rest streams from flash)
static GqConf s_conf;
static bool s_on = false, s_have = false, s_gameQ = false, s_loaded = false, s_soon = false;
static uint8_t s_fails = 0;
static uint32_t s_t = 0, s_lastWrite = 0, s_lastOk = 0, s_seqSaved = 0, s_serial = 0, s_soonT = 0;
static uint16_t s_day = 65535;
static GqSettings s_lastSet;
static GqConf s_lastConf;
static uint8_t s_burst = 0;
static uint32_t s_burstT = 0;
static bool s_wrote = false;

static uint32_t slotAddr(uint32_t cell, uint32_t k) { return BASE + rl_slot_offset(cell, k); }
static bool readSlot(uint32_t cell, uint32_t k) { return ESP.flashRead(slotAddr(cell, k), (uint32_t*)s_slot, RL_SLOT_BYTES); }

// Checks one cell in flash (slot by slot). Returns the RL_* state; *seq / *lslot as rl_cell_end.
static bool checkCell(uint32_t cell, int* state, uint32_t* seq, uint32_t* lslot) {
  RlCell c;
  rl_cell_begin(&c);
  for (uint32_t k = 0; k < RL_SLOTS_PER_CELL; k++) {
    if (!readSlot(cell, k)) return false;
    rl_cell_feed(&c, k, s_slot);
  }
  *seq = 0; *lslot = 0;
  *state = rl_cell_end(&c, seq, lslot);
  return true;
}

void begin(bool safeMode) {
  s_on = !safeMode && ESP.getSketchSize() <= MAX_IMAGE && ESP.getFlashChipRealSize() >= 4UL * 1024 * 1024;
  if (!s_on) return;
  for (uint32_t c = 0; c < RL_CELLS; c++) {
    int st = RL_BLANK; uint32_t q = 0, ls = 0;
    if (!checkCell(c, &st, &q, &ls)) { s_on = false; return; }
    s_scan.state[c] = (uint8_t)st; s_scan.seq[c] = q; s_scan.lslot[c] = (uint8_t)ls;
  }
  rl_plan(&s_scan, &s_boot);
  if (s_boot.load >= 0) {
    const uint32_t k = s_boot.load_legacy ? (uint32_t)s_boot.load_slot : 0u;
    if (readSlot((uint32_t)s_boot.load, k)) {
      memcpy(&s_set, s_slot + (s_boot.load_legacy ? OFF_ONE_SET : OFF_SET), sizeof(s_set));
      memcpy(&s_conf, s_slot + (s_boot.load_legacy ? OFF_ONE_CONF : OFF_CONF), sizeof(s_conf));
      s_have = true;
      s_seqSaved = s_boot.next_seq - 1;
    }
  }
  s_gameQ = safety::crashes() >= 2;          // two crashes in a row: do not trust the saved game
  s_lastWrite = millis() - (GAME_EVERY_MS - 5UL * 60 * 1000);   // the first game save comes 5 minutes after boot
  s_burstT = millis();
}

const GqSettings* savedSettings() { return s_have ? &s_set : nullptr; }
const GqConf* savedConf() { return s_have ? &s_conf : nullptr; }
const uint8_t* savedGame() { return nullptr; }

// Streams the loaded record's game into the core, one slot at a time (the record was checked at boot).
static void loadGame() {
  s_loaded = true;
  if (!s_on || !s_have || s_gameQ) return;
  if (s_boot.load_legacy) {
    if (readSlot((uint32_t)s_boot.load, (uint32_t)s_boot.load_slot)) gc_save_import(s_slot + OFF_ONE_GAME, GC_SAVE_BYTES);
    return;
  }
  for (uint32_t k = 0; k < RL_SLOTS_PER_CELL; k++) {
    if (!readSlot((uint32_t)s_boot.load, k)) { if (k) gc_save_import_end(); return; }
    const uint8_t* p = s_slot + RL_HEAD_BYTES + (k == 0 ? GQS_SETTINGS_BYTES + 64 : 0);
    if (k == 0 && p[4] == GQ_SAVE_VERSION_V4 && p[5] == 0) {      // an r5 game block carried over by a quarantined save (see fillSlot)
      uint8_t g[GC_SAVE_BYTES];
      memcpy(g, p, GC_SAVE_PART0);
      if (readSlot((uint32_t)s_boot.load, 1)) { memcpy(g + GC_SAVE_PART0, s_slot + RL_HEAD_BYTES, GC_SAVE_BYTES - GC_SAVE_PART0); gc_save_import(g, GC_SAVE_BYTES); }
      return;
    }
    if (!gc_save_import_part((int)k, p, gc_save_part_size((int)k))) { if (k) gc_save_import_end(); return; }
  }
  gc_save_import_end();
}

// Payload of slot k of the record that is going to be written, at s_slot + RL_HEAD_BYTES.
// Normally the live game; with the saved game quarantined, the old record's slot (the game bytes are carried over untouched).
static void fillSlot(uint32_t k, const GqSettings& st, const GqConf& cf) {
  memset(s_slot, RL_ERASED, RL_SLOT_BYTES);
  uint8_t* pay = s_slot + RL_HEAD_BYTES;
  if (!s_gameQ) {
    if (k == 0) {
      memcpy(pay, &st, sizeof(st));
      memcpy(pay + GQS_SETTINGS_BYTES, &cf, sizeof(cf));
      if (gc_save_export_part(0, pay + GQS_SETTINGS_BYTES + 64, GC_SAVE_PART0) == 0) memset(pay + GQS_SETTINGS_BYTES + 64, 0, GC_SAVE_PART0);   // not the village: nothing to keep
    } else if (gc_save_export_part((int)k, pay, GC_SAVE_PARTN) == 0) {
      memset(pay, 0, GC_SAVE_PARTN);
    }
    return;
  }
  const bool legacy = s_have && s_boot.load_legacy && s_boot.load >= 0;
  const bool old = s_have && !s_boot.load_legacy && s_boot.load >= 0;
  if (legacy && k < 2 && readSlot((uint32_t)s_boot.load, (uint32_t)s_boot.load_slot)) {
    // The r5 game block (192 bytes, game version 4) is carried over untouched: its first 188 bytes take the place of slot 0's game part,
    // the last 4 bytes start slot 1. loadGame() recognises it by its version.
    if (k == 0) {
      memmove(s_slot + OFF_GAME0, s_slot + OFF_ONE_GAME, GC_SAVE_PART0);
      memset(s_slot, RL_ERASED, RL_HEAD_BYTES);
    } else {
      uint8_t tail[GC_SAVE_BYTES - GC_SAVE_PART0];
      memcpy(tail, s_slot + OFF_ONE_GAME + GC_SAVE_PART0, sizeof(tail));
      memset(s_slot, RL_ERASED, RL_HEAD_BYTES);
      memset(pay, 0, RL_PART_BYTES);
      memcpy(pay, tail, sizeof(tail));
    }
  } else if (old && readSlot((uint32_t)s_boot.load, k)) {
    memset(s_slot, RL_ERASED, RL_HEAD_BYTES);                   // the payload is already in place
  } else {
    memset(pay, 0, RL_PART_BYTES);
  }
  if (k == 0) { memcpy(pay, &st, sizeof(st)); memcpy(pay + GQS_SETTINGS_BYTES, &cf, sizeof(cf)); }
}

static bool save() {
  RlPlan p;
  rl_plan(&s_scan, &p);
  const GqSettings& st = prefs::settings();
  const GqConf& cf = config::get();
  if (!s_gameQ) gc_save_begin();             // the record gets its own game number; the two passes below see the same state
  uint32_t crc = rl_crc_init();              // pass 1: the checksum of all 1800 bytes (every slot carries it)
  for (uint32_t k = 0; k < RL_SLOTS_PER_CELL; k++) {
    fillSlot(k, st, cf);
    crc = rl_crc_feed(crc, s_slot + RL_HEAD_BYTES, RL_PART_BYTES);
  }
  crc = rl_crc_done(crc);
  bool ok = true;
  if (p.erase_sector >= 0) ok = ESP.flashEraseSector(BASE / 4096UL + (uint32_t)p.erase_sector);
  for (uint32_t k = 0; k < RL_SLOTS_PER_CELL && ok; k++) {   // pass 2: slot by slot, each read back and compared
    fillSlot(k, st, cf);
    rl_seal(s_slot, k, p.next_seq, crc);
    const uint32_t addr = slotAddr((uint32_t)p.write_cell, k);
    ok = ESP.flashWrite(addr, (uint32_t*)s_slot, RL_SLOT_BYTES);
    if (ok) {
      alignas(4) uint8_t b[64];
      for (uint32_t off = 0; off < RL_SLOT_BYTES && ok; off += sizeof(b)) {
        ok = ESP.flashRead(addr + off, (uint32_t*)b, sizeof(b)) && memcmp(b, s_slot + off, sizeof(b)) == 0;
      }
    }
  }
  if (ok) {                                  // the whole record must now check out as one (same sequence, same checksum)
    int state = RL_JUNK; uint32_t q = 0, ls = 0;
    ok = checkCell((uint32_t)p.write_cell, &state, &q, &ls) && state == RL_VALID && q == p.next_seq;
  }
  rl_applied(&s_scan, &p, ok ? 1 : 0);
  const uint32_t now = millis();
  s_lastWrite = now;
  if (ok) {
    s_wrote = true;
    s_seqSaved = p.next_seq; s_lastOk = now; s_fails = 0;
    s_lastSet = st; s_lastConf = cf;
    s_serial = gc_save_serial();
    s_day = (uint16_t)gc_query(GQ_Q_DAY);
    if (s_gameQ) { s_have = true; s_boot.load = p.write_cell; s_boot.load_legacy = 0; s_boot.load_slot = 0; }   // later saves carry this record over
  } else if (++s_fails >= 3) {
    s_on = false;                              // flash that does not verify: stop using it until the next restart
  }
  return ok;
}

void saveSoon() { s_soon = true; s_soonT = millis(); }

void loop() {
  if (!s_loaded) loadGame();                   // first call, after gc_init() and before the first game tick
  if (!s_on) return;
  const uint32_t now = millis();
  if (now - s_t < 1000) return;
  s_t = now;
  if (safety::updating()) return;
  static bool init = false;
  if (!init) {                                 // baseline: what is in RTC now equals what the record holds (or defaults)
    init = true;
    s_lastSet = prefs::settings();
    s_lastConf = config::get();
    s_serial = gc_save_serial();
    s_day = (uint16_t)gc_query(GQ_Q_DAY);
    if (!s_have) { s_lastSet.crc ^= 1; }       // nothing saved yet: the first loop saves the settings
  }
  if (now - s_burstT >= 10UL * 60 * 1000) { s_burstT = now; s_burst = 0; }
  if (s_burst >= 6) return;                    // never more than 6 writes in 10 minutes, whatever happens
  const uint32_t since = now - s_lastWrite;
  const bool setDirty = memcmp(&prefs::settings(), &s_lastSet, sizeof(GqSettings)) != 0 || memcmp(&config::get(), &s_lastConf, sizeof(GqConf)) != 0;
  const bool gameDirty = !s_gameQ && gc_save_serial() != s_serial;
  const bool newDay = !s_gameQ && (uint16_t)gc_query(GQ_Q_DAY) != s_day;
  const bool soon = s_soon && now - s_soonT >= SETTINGS_WAIT_MS && !s_gameQ;
  if ((setDirty && since >= SETTINGS_WAIT_MS) || soon || (newDay && since >= DAY_WAIT_MS) || (gameDirty && since >= GAME_EVERY_MS)) {
    s_soon = false;
    s_burst++;
    save();
  }
}

bool active() { return s_on; }

void statusLine(char* buf, size_t n) {
  if (!n) return;
  buf[0] = 0;
  if (!s_on) { snprintf_P(buf, n, PSTR("Saving to flash is off (safe mode, or the flash did not verify).")); return; }
  if (s_wrote) snprintf_P(buf, n, PSTR("Saved to flash %u min ago (record %u)%s."), (unsigned)((millis() - s_lastOk) / 60000), (unsigned)s_seqSaved, s_gameQ ? ", saved game kept aside after crashes" : "");
  else if (s_have) snprintf_P(buf, n, PSTR("Loaded from flash (record %u)%s."), (unsigned)s_seqSaved, s_gameQ ? ", saved game kept aside after crashes" : "");
  else snprintf_P(buf, n, PSTR("Nothing saved to flash yet."));
}

}  // namespace store
