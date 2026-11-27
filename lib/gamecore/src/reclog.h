// Flash record log of release r6 (pure C++: no Arduino, no heap, no libc, no floating point). Decides where records go and checks
// them; firmware/store.cpp does the flash access, one 320 byte slot at a time.
//
// The area 0x80000..0x82FFF is 3 sectors of 4 KB. Slot i is at byte (i / 12) * 4096 + (i % 12) * 320 (the last 256 bytes of every
// sector stay unused). A record is a CELL of 6 slots, two cells per sector, six cells in all:
//   slot: u32 magic "GQSV" | u32 crc32 over bytes 8..319 | u16 fmt (2) | u16 part (0..5) | u32 seq | u32 record crc | 300 bytes
// The record is 1800 bytes: settings 48, config 64, then the saved game (1688 bytes) cut at 188 / 300 / 300 / 300 / 300 / 300.
// The record crc is the crc32 of those 1800 bytes and is repeated in every slot. A cell is a valid record only when all 6 slots
// pass their own crc, hold the right part number, the same seq and the same record crc, and the 6 payloads give that record crc.
// An r5 record (fmt 1, one slot with settings, config and the game of 192 bytes) is still read; it is replaced by the first save.
// Writing goes to the next blank cell after the newest record. When no cell is blank, the sector after the newest record's sector
// is erased first (it holds the two oldest cells), so the newest record AND the one before it are never in a sector being erased.
// A write cut short by a power loss leaves a cell that is not valid (never loaded, never reused until its sector is erased).
#pragma once
#include "platform.h"

#define RL_SECTORS          3u
#define RL_SECTOR_BYTES     4096u
#define RL_SLOT_BYTES       320u
#define RL_SLOTS_PER_SECTOR 12u
#define RL_SLOTS_PER_CELL   6u
#define RL_CELLS            6u
#define RL_CELLS_PER_SECTOR 2u
#define RL_HEAD_BYTES       20u
#define RL_PART_BYTES       300u                 // payload of one slot
#define RL_RECORD_BYTES     1800u                // 6 x 300
#define RL_MAGIC            0x56534751u          // "GQSV"
#define RL_FMT_ONE          1u                   // r5: one slot, payload 304 bytes at offset 16
#define RL_FMT_CELL         2u

#define RL_ERASED 0xFFu                          // an erased flash byte
enum { RL_BLANK = 0, RL_VALID = 1, RL_JUNK = 2, RL_LEGACY = 3 };

// Flash address offset (from the area base) of slot k of cell c.
static inline uint32_t rl_slot_offset(uint32_t cell, uint32_t k) {
  uint32_t i = cell * RL_SLOTS_PER_CELL + k;
  return (i / RL_SLOTS_PER_SECTOR) * RL_SECTOR_BYTES + (i % RL_SLOTS_PER_SECTOR) * RL_SLOT_BYTES;
}

// Seals a slot image: the 300 payload bytes are already at slot + RL_HEAD_BYTES.
void rl_seal(uint8_t* slot, uint32_t part, uint32_t seq, uint32_t record_crc);

// Streaming crc of the 1800 record bytes (feed them in order, then done).
uint32_t rl_crc_init(void);
uint32_t rl_crc_feed(uint32_t state, const uint8_t* p, uint32_t n);
uint32_t rl_crc_done(uint32_t state);

// Checks one cell, slot by slot: begin, then feed slots 0..5 in order, then end.
typedef struct {
  uint32_t seq, record_crc, running, legacy_seq;
  uint8_t  blank, bad, v2, v1, legacy_slot;
} RlCell;
void rl_cell_begin(RlCell* c);
void rl_cell_feed(RlCell* c, uint32_t k, const uint8_t* slot);
// RL_BLANK (all slots erased), RL_VALID (a complete record, *seq set), RL_LEGACY (no cell record, but an r5 record in slot
// *legacy_slot with sequence *seq), RL_JUNK.
int  rl_cell_end(RlCell* c, uint32_t* seq, uint32_t* legacy_slot);

typedef struct {
  uint8_t  state[RL_CELLS];       // RL_BLANK / RL_VALID / RL_JUNK / RL_LEGACY
  uint8_t  lslot[RL_CELLS];       // legacy: slot within the cell
  uint32_t seq[RL_CELLS];         // valid and legacy cells
} RlScan;

typedef struct {
  int load;           // cell to load, or -1 (nothing valid)
  int load_legacy;    // 1: the cell holds an r5 record in slot load_slot
  int load_slot;
  int write_cell;     // cell the next record goes to
  int erase_sector;   // sector (0..2) to erase before writing, or -1
  uint32_t next_seq;  // sequence number of the next record
} RlPlan;

void rl_plan(const RlScan* scan, RlPlan* plan);
// After carrying out a plan: record the new state (erase first, then the written cell).
void rl_applied(RlScan* scan, const RlPlan* plan, int ok);
