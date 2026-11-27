// MiniGodus game core: portable, allocation-free, no libc.
// The firmware and the browser preview both drive it through this API.
#pragma once
#include "platform.h"

#define GC_W 240
#define GC_H 240
#define GC_TILE 16
#define GC_COLS 15
#define GC_ROWS 15
#define GC_STRIP_H 16
#define GC_STRIP_PIXELS (GC_W * GC_STRIP_H)

#ifdef __cplusplus
extern "C" {
#endif

// GC_SCENE_INFO: plain text screen used by the firmware's safe mode.
enum { GC_SCENE_TEST = 0, GC_SCENE_VILLAGE = 1, GC_SCENE_INFO = 2 };

// Start a scene. swap_bytes=1 makes the renderer emit big-endian RGB565
// (what the ST7789 wants over SPI); the browser preview passes 0.
void gc_init(int scene, uint32_t seed, int swap_bytes);

// Advance the simulation by dt milliseconds.
void gc_tick(uint32_t dt_ms);

// Wall clock (from NTP). valid=0 shows --:--.
void gc_set_clock(int hour, int minute, int valid);

// Short status messages rotated in the bottom bar (IP address, AP name...).
// line 0..3, text is copied (max 31 chars). Pass "" to clear.
void gc_set_status(int line, const char* text);

// Colour depth 0..100: darkens mid-tones to fight a washed-out panel (0 = off).
void gc_set_depth(int percent);

// Frames per second x10, shown in the test scene.
void gc_set_fps(int fps_x10);

// Bit n set = strip n (pixel rows n*16..n*16+15) needs re-sending.
uint32_t gc_dirty_mask(void);
void gc_clear_dirty(void);
void gc_mark_all_dirty(void);

// Render one 240x16 strip into out[240*16].
void gc_render_strip(int strip, uint16_t* out);

#include "gq_night.h"   // GC_NIGHT_START_MIN / GC_NIGHT_END_MIN: the built-in night

// ---- persistence (R5, full save R6). The core does no I/O; firmware/store.cpp keeps the bytes in flash.
// The saved game (r6) is GC_SAVE_PARTS parts: part 0 has GC_SAVE_PART0 bytes, every other part GC_SAVE_PARTN. store.cpp writes one part per
// 320-byte flash slot. Parts hold: 0 header, stocks, statistics; 1 villagers, saplings; 2 buildings; 3-5 the map (see savepack.inc.h).
#define GC_SAVE_BYTES  192      // the saved game of r4 and r5 (version 4), one block; still accepted by gc_save_import
#define GC_SAVE_PARTS  6
#define GC_SAVE_PART0  188
#define GC_SAVE_PARTN  300
#define GC_SAVE_TOTAL  (GC_SAVE_PART0 + (GC_SAVE_PARTS - 1) * GC_SAVE_PARTN)
enum { GC_SAVE_OK = 0, GC_SAVE_ERR_LEN, GC_SAVE_ERR_MAGIC, GC_SAVE_ERR_VERSION, GC_SAVE_ERR_CRC };
int      gc_save_part_size(int part);                  // bytes of part 0..GC_SAVE_PARTS-1, 0 for anything else
void     gc_save_begin(void);                          // call once before exporting a record: it gets a new record number
int      gc_save_export_part(int part, uint8_t* buf, int cap);   // village scene only. Returns the part's size, or 0 (nothing changes the state)
// Import is part by part: part 0 first (1 = accepted, the world is rebuilt from it; 0 = rejected, nothing changed), then parts 1.., any order,
// each with its full size (1 = taken, 0 = refused), then gc_save_import_end().
int      gc_save_import_part(int part, const uint8_t* buf, int len);
void     gc_save_import_end(void);
// The same in one block (tests, tools): all parts one after another. Import also accepts a version 4 block of GC_SAVE_BYTES (r4, r5).
int      gc_save_export(uint8_t* buf, int cap);        // village scene only. Returns GC_SAVE_TOTAL, or 0.
int      gc_save_import(const uint8_t* buf, int len);  // 1 = accepted, sanitized, applied. 0 = rejected, nothing changed.
uint32_t gc_save_serial(void);                         // changes whenever the saved state changed
// Day-1 defaults (same as a first boot).
void     gc_reset_game(void);

// ---- worlds (r2)
// Start a new world from a seed. Its landform + climate pair is never the same as the previous world's (the last 4 worlds
// are kept in the save). Same seed and same previous world give the same world. Day counter restarts at 0.
void gc_new_world(uint32_t seed);
// Name of the current world (A-Z, up to 10 characters) into buf (NUL terminated). Returns its length.
int  gc_world_name(char* buf, int cap);

// ---- environment
enum { GC_NET_ONLINE = 0, GC_NET_OFFLINE = 1, GC_NET_AP_ONLY = 2 };
enum { GC_OPT_CARD_PERIOD = 0, GC_OPT_NIGHT_QUIET = 1,
       GC_OPT_SLEEP_MODE = 2 };   // 0 = built-in night (default), 1 = schedule in use: stay up, 2 = go to bed and sleep
void gc_set_date(int year, int month, int day, int weekday, uint32_t epoch);  // year 0 = unknown; weekday 0 = Sunday
void gc_set_clock_quality(int approximate);     // 1 = restored after a restart, not NTP-synced yet
void gc_set_net(int state);                     // GC_NET_ONLINE 0, GC_NET_OFFLINE 1, GC_NET_AP_ONLY 2
void gc_set_option(int key, int value);         // GC_OPT_CARD_PERIOD (minutes, 0 = off), GC_OPT_NIGHT_QUIET (0/1),
                                                // GC_OPT_SLEEP_MODE (0..2, other values are ignored)

// Live weather (R3). Call once a second with the current reading; the values are clamped.
// wx = GQ_W_* 0..6 or GC_WX_NONE (anything else counts as none); temp_c10 = tenths of a degree Celsius
// (-999..999); flags = GC_WXF_*. Not saved; game rules ignore it until calendar.cpp uses it.
#define GC_WX_NONE  (-1)
#define GC_WXF_FAHRENHEIT 1     // display unit
#define GC_WXF_NIGHT      2     // is_day == 0
#define GC_WXF_STALE      4     // age class 2
void gc_set_live_weather(int wx, int temp_c10, int flags);

// ---- introspection for tests, the preview and the status page
// Keys are GQ_Q_* and commands GQ_DBG_* (gq_types.h). Unknown key returns 0, unknown command does nothing.
int32_t gc_query(int key);
void    gc_debug(int cmd, int32_t arg);

#ifdef __cplusplus
}
#endif
