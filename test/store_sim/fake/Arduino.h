// Host stand-in for the few Arduino/ESP calls firmware/store.cpp makes (test/store_sim only). The flash is a shared memory block
// holding the three sectors 0x80000..0x82FFF; a countdown cuts the power (the process exits) in the middle of an erase or a write.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define PROGMEM
#define PSTR(x) (x)
#define snprintf_P snprintf
uint32_t millis();
struct EspClass {
  uint32_t getSketchSize();
  uint32_t getFlashChipRealSize();
  bool flashRead(uint32_t addr, uint32_t* data, size_t size);
  bool flashWrite(uint32_t addr, uint32_t* data, size_t size);
  bool flashEraseSector(uint32_t sector);
};
extern EspClass ESP;
