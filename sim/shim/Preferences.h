// Desktop stand-in: nothing is stored, every read returns its default.
#pragma once

#include <cstdint>

class Preferences {
 public:
  bool begin(const char*, bool = false) { return true; }
  void end() {}
  bool isKey(const char*) { return false; }
  bool remove(const char*) { return true; }
  uint8_t getUChar(const char*, uint8_t def = 0) { return def; }
  bool getBool(const char*, bool def = false) { return def; }
  double getDouble(const char*, double def = 0) { return def; }
  void putUChar(const char*, uint8_t) {}
  void putBool(const char*, bool) {}
  void putDouble(const char*, double) {}
};
