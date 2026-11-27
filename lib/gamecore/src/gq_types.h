// Shared types of the MiniGodus game core: ids, query keys and the saved state.
// The saved layout is versioned by MG_SAVE_VERSION; the magic is MiniGodus' own (GeekQuest uses a different one in the
// same flash area, so switching firmware between the two never loads the other game).
#pragma once
#include "platform.h"
#include <stddef.h>   // offsetof

enum { GQ_SPRING, GQ_SUMMER, GQ_AUTUMN, GQ_WINTER };
enum { GQ_W_CLEAR, GQ_W_CLOUDY, GQ_W_RAIN, GQ_W_STORM, GQ_W_FOG, GQ_W_SNOW, GQ_W_WINDY, GQ_W_COUNT };
// What the shaman is doing (GQ_Q_HERO_ACT). The firmware only cares about GQ_ACT_SLEEP.
enum { GQ_ACT_NONE, GQ_ACT_WANDER, GQ_ACT_SLEEP, GQ_ACT_CAST, GQ_ACT_COUNT };

// ---- gc_query keys
enum {
  GQ_Q_DAY,             // world day, 0 = first day (saved)
  GQ_Q_MINUTE,          // wall clock minute of the day, 0..1439
  GQ_Q_WEATHER,         // weather shown: live reading, else clear
  GQ_Q_HERO_ACT,        // GQ_ACT_*
  GQ_Q_HERO_ABED,       // 1 while the shaman lies in bed
  GQ_Q_LIVE_WX,         // live weather as stored by gc_set_live_weather: 0..6, or -1 (GC_WX_NONE)
  GQ_Q_LIVE_TEMP,       // tenths of a degree Celsius as stored (clamped)
  GQ_Q_LIVE_FLAGS,      // GC_WXF_*
  GQ_Q_SLEEP_MODE,      // GC_OPT_SLEEP_MODE as stored
  GQ_Q_SHAMAN_X,        // shaman position in pixels
  GQ_Q_SHAMAN_Y,
  GQ_Q_SEED,            // world seed (saved)
  GQ_Q_LANDFORM,        // 0 island, 1 coast, 2 river valley, 3 lake basin, 4 archipelago, 5 mountain plateau
  GQ_Q_CLIMATE,         // 0 temperate, 1 savanna, 2 northern, 3 tropical
  GQ_Q_CAM_X,           // camera, world pixels (map is 512x512)
  GQ_Q_CAM_Y,
  GQ_Q_START_X,         // start tile (the shaman's bed)
  GQ_Q_START_Y,
  GQ_Q_START_AREA,      // walkable tiles connected to the start tile
  GQ_Q_MAP_HASH,        // FNV-1a over the 1024 map bytes
  GQ_Q_HERO_ON_LAND,    // 1 while the tile under the shaman's centre is one he may walk on
  GQ_Q_TEMP10,          // tenths of a degree Celsius the rules use (150 without a reading)
  GQ_Q_PHASE,           // r6: 0 playing, 1 worship day, 2 the village is dying out, 3 summary (monument finished), 4 summary (extinct)
  GQ_Q_MON_STAGE,       // r6: monument stage 0 (none or only marked) .. 5 (finished)
  GQ_Q_MON_TYPE,        // r6: 0 pyramid, 1 stone circle, 2 great tree, 3 obelisk, 4 temple
  GQ_Q_COUNT,
  GQ_Q_VILLAGE = 64     // GQ_Q_VILLAGE + n: village numbers (see village_stat in village.cpp: 0 population, 1 food, 2 wood, 3 faith, ...)
};

// ---- gc_debug commands (preview and tests)
enum { GQ_DBG_SET_DAY, GQ_DBG_NEXT_DAY, GQ_DBG_FORCE_ACT /*arg = GQ_ACT_**/,
       GQ_DBG_NEW_WORLD,      // arg = seed (0 = next random): a new world that never repeats the previous landform + climate
       GQ_DBG_SET_SEED,       // arg = seed: exactly the world of that seed (no repeat rule), for the preview's seed picker
       GQ_DBG_FORCE_LAND,     // arg = landform for the next world, -1 = from the seed
       GQ_DBG_FORCE_CLIM,     // arg = climate for the next world, -1 = from the seed
       GQ_DBG_SET_CAM,        // arg = x + 1024 * y (world pixels), clamped; the camera follows the shaman again after he moves
       GQ_DBG_FOOD,           // arg = food stock
       GQ_DBG_WOOD,           // arg = wood stock
       GQ_DBG_ADD_VILLAGERS,  // arg = number of adults to add
       GQ_DBG_THIRST,         // arg = thirst of every villager (0..31)
       GQ_DBG_FAITH,          // arg = faith stock
       GQ_DBG_BAND,           // arg = band slot to show now (0..3 status, 4 day line, 5 shaman project)
       GQ_DBG_WEATHER,        // arg = GQ_W_* forces the weather (and its icon), -1 gives it back to the live reading
       GQ_DBG_TEMP,           // arg = tenths of a degree Celsius forces the temperature, 9999 gives it back to the live reading
       GQ_DBG_SICK,           // arg = villager index (0..47): falls sick; -1 everybody
       GQ_DBG_AGE,            // arg = villager index | age class << 8 (1 adult, 2 elder) | 1 << 16 (weak)
       GQ_DBG_FLASH,          // a lightning flash now
       GQ_DBG_MONUMENT,       // r6: arg = stage 0..5: the monument stands at that stage (laid out if needed, no faith asked)
       GQ_DBG_PHASE,          // r6: arg = phase: 1 the worship day starts now (monument finished), 3 summary (monument), 4 summary (extinct), 0 back to play
       GQ_DBG_EXTINCT };      // r6: every villager dies

// ---- the saved state in memory, 192 bytes, little-endian, no padding. The saved game itself is GC_SAVE_PARTS parts (gamecore.h): part 0
// starts with the first 64 bytes of this struct. Version 4 (r4, r5) was this struct whole, with the village in `reserved`; it is still read.
// The core only exports and imports bytes; firmware/store.cpp keeps them in flash.
#define GQ_SAVE_SIZE    192
#define GQ_SAVE_HEAD    188   // size of part 0 of a version 5 saved game
#define GQ_SAVE_MAGIC   0x444F474Du   // bytes "MGOD": MiniGodus (GeekQuest's is 0x53514747)
#define GQ_SAVE_VERSION 5
#define GQ_SAVE_VERSION_V4 4
typedef struct {
  uint32_t magic;            //   0
  uint16_t version;          //   4
  uint16_t size;             //   6
  uint32_t crc32;            //   8  over bytes 12..191
  uint32_t seq;              //  12
  uint32_t epoch;            //  16
  uint32_t play_min;         //  20
  uint32_t rng;              //  24
  uint32_t seed;             //  28  world seed
  uint16_t day;              //  32
  uint16_t minute;           //  34
  uint8_t  landform, climate;//  36  current world (same as hist 0)
  uint8_t  hist_n, pad0;     //  38  1..4 worlds remembered, newest first
  uint32_t hist_seed[4];     //  40  the last 4 worlds: seed, landform, climate
  uint8_t  hist_land[4];     //  56
  uint8_t  hist_clim[4];     //  60
  uint8_t  reserved[128];    //  64
} GqSave;

static_assert(sizeof(GqSave) == GQ_SAVE_SIZE, "GqSave must be 192 bytes");
static_assert(offsetof(GqSave, magic) == 0, "magic");
static_assert(offsetof(GqSave, version) == 4, "version");
static_assert(offsetof(GqSave, size) == 6, "size");
static_assert(offsetof(GqSave, crc32) == 8, "crc32");
static_assert(offsetof(GqSave, seq) == 12, "seq");
static_assert(offsetof(GqSave, day) == 32, "day");
static_assert(offsetof(GqSave, hist_seed) == 40, "hist_seed");
static_assert(offsetof(GqSave, reserved) == 64, "reserved");

typedef struct {
  GqSave   s;             // the saved part
  uint32_t serial;        // bumped by gq_touch() on state changes
  uint8_t  net, clock_approx, card_period, night_quiet;
  uint8_t  sleep_mode;     // GC_OPT_SLEEP_MODE
  uint8_t  live_wx1;       // live weather: GQ_W_* + 1, 0 = none (so that zero-initialised means none)
  uint8_t  live_flags;     // GC_WXF_*
  int16_t  live_temp10;    // tenths of a degree Celsius
  int16_t  year; uint8_t month, mday, wday;
} GqState;
extern GqState G;
