/* Mal for wifi_secrets.h. Kopier til wifi_secrets.h i samme mappe og fyll
 * inn. wifi_secrets.h er i .gitignore, saa her skal det bare vaere
 * plassholdere.
 *
 * ESP-en kobler til det sterkeste nettet i lista. Alle maa vaere 2,4 GHz
 * (paa iPhone-hotspot: slaa paa "Maksimer kompatibilitet").
 */

#pragma once

static const char *WIFI_NETT[][2] = {
  { "hjemmenettet", "passordet-hjemme" },
  { "iPhone-hotspot", "passordet-paa-hotspot" },
};

// Mosquitto paa Pi-en hjemme. IP og ikke .local, siden PubSubClient ikke
// slaar opp mDNS-navn. Reserver adressen i ruteren.
static const char *MQTT_BROKER = "192.168.68.XXX";
static const uint16_t MQTT_PORT = 1883;
