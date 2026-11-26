// The villagers of MiniGodus (release r3): people, their needs and daily roles, buildings, food, wood and faith.
// Internal to the game core. No heap, no libc, no floating point. Everything lives in a few hundred bytes of static RAM:
// a villager is 6 bytes (up to 48), a building 4 bytes (up to 32). Pathfinding and site search share the one distance
// scratch buffer g_dist of the world.
#pragma once
#include "world.h"

enum { MAXV = 48, MAXB = 32 };
enum { R_CHILD, R_FARMER, R_HERDER, R_FISHER, R_LUMBER, R_BUILDER, R_HEALER, R_CARRIER, R_ELDER, R_IDLE, R_COUNT };
enum { B_TENT, B_HUT, B_FIELD, B_FISH, B_WELL, B_COUNT };
enum { AGE_CHILD, AGE_ADULT, AGE_ELDER };
// what a villager is doing
enum { A_IDLE, A_GO, A_WORK, A_EAT, A_BED, A_SLEEP, A_DRINK, A_RET };

enum { MEAL_1 = 7 * 60 + 30, MEAL_2 = 12 * 60 + 30, MEAL_3 = 19 * 60 };
enum { GROW_UP_DAYS = 5, HUNGER_WEAK = 4, HUNGER_DEATH = 6 };

void village_reset(int seed_mix);                  // a new world: one couple and a tent
void village_minute(int minute, int night);        // the clock moved on by one minute (clock valid)
void village_new_day(int night);                   // midnight: ageing, births
void village_tick(uint32_t dt, int night);         // movement, animation
void village_draw(uint16_t* out, int wx0, int wy0);
int  village_stat(int which);                      // GQ_Q_* style numbers, see village.cpp
void village_pack(uint8_t* p);                     // 128 bytes for GqSave.reserved
void village_unpack(const uint8_t* p);             // total: any bytes leave a valid village
void village_debug(int cmd, int32_t arg);

// provided by godus.cpp
void world_cam(int* x, int* y);
