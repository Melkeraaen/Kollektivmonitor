/* Mal for wifi_secrets.h. Kopier til wifi_secrets.h i samme mappe og fyll
 * inn. wifi_secrets.h er i .gitignore, saa her skal det bare vaere
 * plassholdere.
 *
 * ESP-en kobler til det sterkeste nettet i lista. Alle maa vaere 2,4 GHz
 * (paa iPhone-hotspot: slaa paa "Maksimer kompatibilitet").
 */

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
