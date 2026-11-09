#pragma once
#include "arduino_stub.h"
enum { WL_IDLE_STATUS, WL_NO_SSID_AVAIL, WL_CONNECTED, WL_CONNECT_FAILED, WL_CONNECTION_LOST, WL_DISCONNECTED };
#define WIFI_STA 1
struct IPStub { std::string toString() const { return "10.42.0.2"; } };
struct WiFiStub {
  int status() { return WL_CONNECTED; }
  void mode(int) {}
  void setHostname(const char *) {}
  void setAutoReconnect(bool) {}
  void setSleep(bool) {}
  void disconnect() {}
  int RSSI(int = 0) { return -55; }
  std::string SSID(int = 0) { return "PresenceLab"; }
  IPStub localIP() { return {}; }
  int scanNetworks() { return 0; }
  void scanDelete() {}
  long channel(int) { return 11; }
};
extern WiFiStub WiFi;
class WiFiClient {};
