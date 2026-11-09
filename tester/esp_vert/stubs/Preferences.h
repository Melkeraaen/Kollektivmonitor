#pragma once
#include "arduino_stub.h"
// Flash erstattes av et kart som overlever at status nullstilles, som ved omstart
extern std::map<std::string, uint64_t> testFlash;
struct Preferences {
  std::string ns;
  void begin(const char *n, bool) { ns = n; }
  void end() {}
  uint64_t les(const char *k, uint64_t d) { auto it = testFlash.find(ns + "/" + k); return it == testFlash.end() ? d : it->second; }
  uint8_t getUChar(const char *k, uint8_t d) { return (uint8_t) les(k, d); }
  uint32_t getUInt(const char *k, uint32_t d) { return (uint32_t) les(k, d); }
  bool getBool(const char *k, bool d) { return les(k, d) != 0; }
  uint64_t getULong64(const char *k, uint64_t d) { return les(k, d); }
  void putUChar(const char *k, uint8_t v) { testFlash[ns + "/" + k] = v; }
  void putUInt(const char *k, uint32_t v) { testFlash[ns + "/" + k] = v; }
  void putBool(const char *k, bool v) { testFlash[ns + "/" + k] = v; }
  void putULong64(const char *k, uint64_t v) { testFlash[ns + "/" + k] = v; }
};
