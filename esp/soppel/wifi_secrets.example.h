// Mal: kopier til wifi_secrets.h og fyll inn. Nettene maa vaere 2,4 GHz.

#pragma once

static const char *WIFI_NETT[][2] = {
  { "PresenceLab", "passordet-til-hotspoten" },
};

// Mosquitto paa Pi 5, som selv er hotspot. IP og ikke .local, siden
// PubSubClient ikke slaar opp mDNS-navn.
static const char *MQTT_BROKER = "10.42.0.1";
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_BRUKER = "kollektivmonitor";
static const char *MQTT_PASSORD = "passordet-til-mqtt-brukeren";
