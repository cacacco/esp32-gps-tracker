// Finto Preferences.h, vedi Arduino.h qui accanto.
#pragma once
#include <Arduino.h>
class Preferences {
 public:
  bool    begin(const char*, bool = false) { return false; }
  void    end() {}
  uint8_t getUChar(const char*, uint8_t def = 0) { return def; }
  size_t  putUChar(const char*, uint8_t) { return 1; }
  bool    getBool(const char*, bool def = false) { return def; }
  size_t  putBool(const char*, bool) { return 1; }
};
