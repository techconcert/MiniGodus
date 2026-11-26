// Shared internals of the game core (not part of the public API).
#pragma once
#include "gamecore.h"
#include "gq_types.h"
#include "assets_gen.h"

// ---- tile ids in assets/src/tiles.png ----
enum {
  T_GRASS = 0, T_TUFT = 1, T_FLOWER = 2, T_PATH = 3,
  T_WATER = 4, T_WATER2 = 5, T_TREE = 6, T_ROCK = 7,
  T_NONE = 0xFF
};

enum { DIR_DOWN = 0, DIR_UP = 1, DIR_LEFT = 2, DIR_RIGHT = 3 };
enum { SH_TILES = 0, SH_CHARS = 1, SH_ITEMS = 2 };
// chars sheet: figure down 0..2, up 3..5, left 8..10 (right = left flipped); index by DIR_DOWN/UP/LEFT
static const uint8_t HERO_CELL_BASE[3] = {0, 3, 8};

// palette indices
enum { P_OUTLINE = 1, P_SAND = 7, P_WHITE = 13, P_GREY = 14, P_YELLOW = 15 };

// ---- shared state (gamecore.cpp) ----
#if GQ_TEST_SCENE
extern uint8_t  g_ground[GC_ROWS][GC_COLS];
extern uint8_t  g_obj[GC_ROWS][GC_COLS];
#endif
extern uint16_t g_pal[16];      // tinted palette for the world
extern uint16_t g_act[16];      // palette for actors
extern uint16_t g_ui[16];       // untinted palette for HUD / text
extern uint32_t g_dirty;
extern char     g_status[4][32];
extern int      g_hour, g_min, g_clock_valid;

uint32_t gc_rnd();
uint32_t gc_rng_get(void);
void gc_rng_set(uint32_t state);
int gc_scene_id(void);
void mark_rows(int y, int h);
void str_copy(char* dst, const char* src, int cap);
int str_len(const char* s);

// ---- drawing into one 240x16 strip whose top is at y0 ----
gc_cell sheet_cell(int sheet, int cell);
void blit_tile(uint16_t* out, int x, gc_cell tile, int transparent);
void blit_sprite(uint16_t* out, int y0, int sx, int sy, gc_cell cell, int flip, const uint8_t* remap, const uint16_t* pal);
void fill_rect(uint16_t* out, int y0, int x, int y, int w, int h, uint16_t c);
void draw_text(uint16_t* out, int y0, int x, int y, const char* s, uint16_t c, int scale, int shadow);
void draw_text_n(uint16_t* out, int y0, int x, int y, const char* s, int n, uint16_t c, int scale);
#if GQ_TEST_SCENE
void render_ground_row(uint16_t* out, int s, uint8_t water_frame);
#endif

// ---- the MiniGodus scene (godus.cpp) ----
void build_palettes(void);
void world_init(uint32_t seed);
void world_tick(uint32_t dt);
void world_render(int s, uint16_t* out);
void world_status_changed(void);
void world_clock_changed(void);
void world_weather_changed(void);
int  world_weather(void);                        // GQ_W_* shown now
int  world_temp(int* out, int* is_f);            // 1 and whole degrees in the display unit; 0 if none
int32_t world_query(int key);
void world_debug(int cmd, int32_t arg);
void world_new_day(void);

// ---- save.cpp ----
uint32_t gq_save_crc(const uint8_t* p, int n);
int  gq_save_validate(const uint8_t* buf, int len);      // GC_SAVE_*
void gq_save_sanitize(GqSave* s);
void gq_touch(void);
