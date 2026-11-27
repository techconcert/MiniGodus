// R5 power-cut saving (approved by the owner on 2026-11-11), extended in r6 to the whole game: settings, weather/sleep record and the
// game are kept in flash sectors 0x80000..0x82FFF as a ring of six records of six checksummed slots (lib/gamecore/src/reclog.h).
// Those sectors lie above the running image (CI cap 500 KB) and below the lowest start of a staged firmware update, and are used by
// nothing else.
//   - Loaded once at boot. Never written in safe mode, while a firmware upload runs, or when the image is larger than 500 KB.
//   - A record is valid only when all six of its slots agree (same sequence number, same record checksum). A write cut short by a power
//     loss leaves an invalid record and the previous complete one is loaded.
//   - Written when the settings change (a few seconds later), then at most every 20 minutes for the game and when a new game day starts.
//     A write that fails verification three times switches saving off until the next restart.
//   - After two crashes in a row the saved game is not loaded and not overwritten (the settings still are).
//   - An r5 record (one slot, game version 4) is still read; the game in it is upgraded and the first save replaces it.
// This is the only file that erases or writes flash (lint L2).
#pragma once
#include <Arduino.h>
#include "settings.h"
#include "gqconf.h"

namespace store {

// Reads the newest valid record (read only). Call after safety::begin(), before prefs::begin().
void begin(bool safeMode);
// Call every loop(); does its work at most once a second. Its first call (after gc_init) also loads the saved game into the core.
void loop();

// What the record held at boot (null = no valid record).
const GqSettings* savedSettings();
const GqConf* savedConf();
// Kept for the r5 boot code: the game no longer fits in RAM, so it is streamed into the core by loop(). Always null.
const uint8_t* savedGame();

bool active();                                 // saving is on (not safe mode, flash verified)
void saveSoon();                               // save the game a few seconds from now (a new world was started)

// Status text for the status page ("Saved to flash 12 min ago (record 40)."), empty when there is nothing to say.
void statusLine(char* buf, size_t n);

}  // namespace store
