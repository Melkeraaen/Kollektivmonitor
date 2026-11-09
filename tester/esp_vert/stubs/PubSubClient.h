#pragma once
#include "WiFi.h"
// Husker siste publiserte melding, saa testen kan sjekke den
extern std::string testSistEmne, testSistMelding;
struct PubSubClient {
  PubSubClient(WiFiClient &) {}
  void setServer(const char *, uint16_t) {}
  void setBufferSize(uint16_t) {}
  bool connect(const char *, const char *, const char *) { return true; }
  bool connected() { return true; }
  int state() { return 0; }
  void loop() {}
  bool publish(const char *e, const char *m) { testSistEmne = e; testSistMelding = m; return true; }
};
