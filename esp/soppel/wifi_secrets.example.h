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
  { "PresenceLab", "geir" },
};

/*
 * MQTT-brokeren (Mosquitto) paa Pi 5. Pi-en er selv hotspot (PresenceLab),
 * saa adressen er alltid 10.42.0.1. IP-adresse, ikke .local-navn:
 * PubSubClient slaar ikke opp mDNS-navn.
 *
 * Brokeren deles med et annet prosjekt paa Pi-en og krever brukernavn og
 * passord. KollektivMonitor har sin egen bruker og holder seg til emnene
 * kollektivmonitor/...
 */
static const char *MQTT_BROKER = "10.42.0.1";
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_BRUKER = "kollektivmonitor";
static const char *MQTT_PASSORD = "passordet-til-mqtt-brukeren";
