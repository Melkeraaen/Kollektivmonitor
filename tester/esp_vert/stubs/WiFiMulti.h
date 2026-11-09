#pragma once
#include "WiFi.h"
struct WiFiMulti { void addAP(const char *, const char *) {} int run(uint32_t = 0) { return WL_CONNECTED; } };
