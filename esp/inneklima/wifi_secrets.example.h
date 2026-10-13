/* wifi_secrets.example.h - mal for wifi_secrets.h
 *
 * Kopier denne til wifi_secrets.h i SAMME mappe og fyll inn nettene og
 * brokeren. wifi_secrets.h staar i .gitignore og havner aldri i Git; denne
 * fila gjor det, og skal derfor bare ha plassholdere.
 *
 * ESP-en skanner og kobler til det sterkeste av nettene som er i naerheten,
 * saa det holder med en opplasting for baade hjemmenettet og hotspoten.
 * Alle nettene maa vaere 2,4 GHz. Paa iPhone: slaa paa "Maksimer
 * kompatibilitet" under Personlig Hotspot.
 *
 * Lista kan ha ett nett eller flere.
 */

#pragma once

static const char *WIFI_NETT[][2] = {
  { "hjemmenettet", "passordet-hjemme" },
  { "iPhone-hotspot", "passordet-paa-hotspot" },
};

/*
 * MQTT-brokeren (Mosquitto) paa maskinen som kjorer skjermen. IP-adresse,
 * ikke .local-navn: PubSubClient slaar ikke opp mDNS-navn. Reserver
 * adressen i ruteren saa den ikke flytter seg.
 */
static const char *MQTT_BROKER = "192.168.68.XXX";
static const uint16_t MQTT_PORT = 1883;
