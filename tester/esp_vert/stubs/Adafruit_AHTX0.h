#pragma once
#include "arduino_stub.h"
// AHT20 som testen styrer: om den finnes, om avlesningen feiler, og verdiene
struct sensors_event_t { float temperature; float relative_humidity; };
extern bool testAhtFinnes, testAhtSvarer;
extern float testTemp, testFukt;
struct Adafruit_AHTX0 {
  bool begin() { return testAhtFinnes; }
  bool getEvent(sensors_event_t *f, sensors_event_t *t) {
    if (!testAhtSvarer) return false;
    t->temperature = testTemp; f->relative_humidity = testFukt; return true;
  }
};
