// The r2 world: 32x32 map (1 byte per tile), the generator, and the per-tile quarter tables the renderer uses.
// Internal to the game core (not part of gamecore.h). No heap, no libc.
#pragma once
#include "gc_internal.h"
#include "world_art_ids.h"

#define WM 32                       // map is WM x WM tiles
#define WM_PIX (WM * GC_TILE)       // 512

enum { LF_ISLAND, LF_COAST, LF_RIVER, LF_LAKE, LF_ARCH, LF_PLATEAU, LF_COUNT };
enum { CL_TEMPERATE, CL_SAVANNA, CL_NORTHERN, CL_TROPICAL, CL_COUNT };

// The map byte: bits 0-2 height (0 deep water, 1 shallow, 2 beach, 3-6 land layers, 7 rock),
// bit 3 snow cover, bits 4-5 soil (0 plain, 1 fertile, 2 dry), bits 6-7 object (OB_*).
#define MT_H(b)    ((b) & 7)
#define MT_SNOW    8
#define MT_SOIL(b) (((b) >> 4) & 3)
#define MT_OBJ(b)  ((b) >> 6)
enum { SOIL_PLAIN, SOIL_FERTILE, SOIL_DRY, SOIL_BUILT /* a building stands here */ };
enum { OB_NONE, OB_TREE, OB_BUSH, OB_ROCK };   // OB_BUSH on a water tile is a shoal of fish

// tile kinds for colours: the height, or 8 + 0/1/2 for snow cover on height <= 4 / 5 / 6 (so that snow layers differ)
enum { KIND_COUNT = 11, KIND_SNOW = 8 };

extern uint8_t g_map[WM * WM];
extern uint16_t g_wpal[16];     // world palette of the current climate (RGB565, colour depth applied)
extern uint8_t  g_wframe;       // water shimmer frame 0/1

struct WorldInfo { uint8_t landform, climate; uint8_t start_x, start_y; char name[12]; };
extern WorldInfo g_world;

static inline int snow_kind(int h) { return KIND_SNOW + (h >= 6 ? 2 : (h == 5 ? 1 : 0)); }
static inline int map_kind(uint8_t b) { return (b & MT_SNOW) ? snow_kind(MT_H(b)) : MT_H(b); }

// generator: seed, landform and climate are all fixed by the caller (see world_pick)
void world_generate(uint32_t seed, int landform, int climate);
// landform and climate for a seed, never the same pair as (prev_l, prev_c) (prev_l < 0: no previous world)
void world_pick(uint32_t seed, int prev_l, int prev_c, int* landform, int* climate);
int  world_walkable(int tx, int ty);               // 1 if the shaman may stand there
int  world_flood_area(int tx, int ty);             // walkable tiles connected to (tx, ty)
uint32_t world_map_hash(void);

// renderer tables
void quarts_view(int cam_x, int cam_y);                // quarter table window around the camera (call when the camera moves)
void quarts_rebuild(int x0, int y0, int x1, int y1);   // the map changed in these tiles (inclusive)
void quarts_palette(const uint16_t* pal);              // role lookup tables for the current palette
void quarts_render(uint16_t* out, int wx0, int wy0, int frame);   // terrain of the 240x16 strip whose top is world (wx0, wy0)
int  quarts_tile_anim(int x, int y);                   // 1 if the tile has a shimmering quarter
void draw_objects(uint16_t* out, int wx0, int wy0);

// flow field to a target tile (distance in steps, 255 = unreachable); used by the shaman
extern uint8_t g_dist[WM * WM];
void world_flow(int tx, int ty);
void world_flow_begin(void);                          // several goals: begin, seed each goal tile, run
void world_flow_seed(int tx, int ty, int d);
void world_flow_run(void);
void world_set_built(int tx, int ty, int on);
void world_clear_object(int tx, int ty);
