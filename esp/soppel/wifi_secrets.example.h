// Mal: kopier til wifi_secrets.h og fyll inn. Nettene maa vaere 2,4 GHz.

#pragma once

static const char *WIFI_NETT[][2] = {
  { "hjemmenettet", "passordet-hjemme" },
  { "iPhone-hotspot", "passordet-paa-hotspot" },
};

// Mosquitto paa Pi-en hjemme. IP og ikke .local, siden PubSubClient ikke
// slaar opp mDNS-navn. Reserver adressen i ruteren.
static const char *MQTT_BROKER = "192.168.68.XXX";
static const uint16_t MQTT_PORT = 1883;
