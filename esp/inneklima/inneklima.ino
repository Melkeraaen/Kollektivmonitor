// Inneklima: maaler temperatur og luftfuktighet med AHT20.

#include <WiFi.h>
#include <WiFiMulti.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>

#include "wifi_secrets.h"

// ESP32-S3 ser bare 2,4 GHz-nett
static const size_t ANTALL_NETT = sizeof(WIFI_NETT) / sizeof(WIFI_NETT[0]);

// Brukes som hostname og MQTT-klient-ID
static const char *VERTSNAVN = "inneklima";

static const char *MQTT_EMNE = "kollektivmonitor/inneklima";

static const uint32_t PUBLISERINGSINTERVALL_MS = 10000;
static const uint32_t MQTT_FORSOK_MS = 5000;

// Korrigering for at brettet varmer sensoren. Maal mot et termometer:
// 22,8 paa ESP-en og 21,4 paa termometeret gir -1.4. Ukalibrert = 0.
static const float TEMP_OFFSET = 0.0;

static const uint32_t MAALEINTERVALL_MS = 10000;

// Eldre maaling enn dette sendes som ok:false med feilen, som i esp/soppel/
static const uint32_t MAKS_ALDER_MS = 30000;

// Hvor lenge vi venter paa Wi-Fi for vi prover paa nytt
static const uint32_t WIFI_FORSOK_MS = 20000;

// Hvor lenge wifiMulti.run() venter paa nettet
static const uint32_t WIFI_KOBLE_MS = 10000;

static Adafruit_AHTX0 aht;
static WiFiMulti wifiMulti;
static WiFiClient wifiKlient;
static PubSubClient mqtt(wifiKlient);

// Siste gyldige maaling. Brukes bare med maalingMutex tatt.
struct Maaling {
  bool harMaaling;
  float temp;
  float fukt;
  uint32_t maaltMs;
  char feil[40];   // tom naar alt gaar bra
};

static Maaling maaling = { false, 0.0f, 0.0f, 0, "ingen maaling ennaa" };
static SemaphoreHandle_t maalingMutex;

// Brukes bare av en task hver, saa ingen mutex
static bool sensorKlar = false;
static uint32_t wifiForsoktFra = 0;

static uint32_t alderSekunder(const Maaling &m) {
  if (!m.harMaaling) return 0;
  return (uint32_t) ((millis() - m.maaltMs) / 1000UL);
}

static Maaling lesMaaling() {
  xSemaphoreTake(maalingMutex, portMAX_DELAY);
  const Maaling m = maaling;
  xSemaphoreGive(maalingMutex);
  return m;
}

// Forrige gyldige maaling blir staaende
static void settFeil(const char *tekst) {
  xSemaphoreTake(maalingMutex, portMAX_DELAY);
  strlcpy(maaling.feil, tekst, sizeof(maaling.feil));
  xSemaphoreGive(maalingMutex);
}

static uint32_t oppetidSekunder() {
  return (uint32_t) (esp_timer_get_time() / 1000000LL);
}

static bool startSensor() {
  // Brettets egne pinner, SDA=8 og SCL=9 paa FeatherS3
  Wire.begin();

  if (!aht.begin()) {
    Serial.println("[inneklima] Fant ingen AHT20 paa I2C. Sjekk ledningene.");
    settFeil("fant ingen sensor");
    return false;
  }

  Serial.println("[inneklima] AHT20 funnet.");
  return true;
}

static void lesSensor() {
  sensors_event_t fukt, temp;

  if (!aht.getEvent(&fukt, &temp)) {
    Serial.println("[inneklima] Sensoren svarte ikke paa avlesning.");
    settFeil("sensoren svarte ikke");
    sensorKlar = false;
    return;
  }

  // En AHT20 som har mistet kontakten kan gi NaN i stedet for aa feile
  if (isnan(temp.temperature) || isnan(fukt.relative_humidity)) {
    Serial.println("[inneklima] Avlesningen var NaN.");
    settFeil("avlesningen var NaN");
    return;
  }

  const float t = temp.temperature + TEMP_OFFSET;
  const float f = fukt.relative_humidity;

  xSemaphoreTake(maalingMutex, portMAX_DELAY);
  maaling.temp = t;
  maaling.fukt = f;
  maaling.maaltMs = millis();
  maaling.harMaaling = true;
  maaling.feil[0] = '\0';
  xSemaphoreGive(maalingMutex);

  Serial.printf("[inneklima] %.1f C, %.1f %%\n", t, f);
}

static void publiser() {
  const Maaling m = lesMaaling();
  char json[192];

  if (!m.harMaaling || (millis() - m.maaltMs) > MAKS_ALDER_MS) {
    snprintf(json, sizeof(json), "{\"ok\":false,\"feil\":\"%s\"}",
             m.feil[0] ? m.feil : "ingen maaling ennaa");
    mqtt.publish(MQTT_EMNE, json);
    return;
  }

  // Alderen sendes med, saa server.js ogsaa kan se hvor fersk maalingen er.
  // En desimal holder, sensoren er noyaktig til +/- 0,3 grader og 2 % RF.
  snprintf(json, sizeof(json),
           "{\"ok\":true,\"temperatur\":%.1f,\"fuktighet\":%.1f,"
           "\"alder_s\":%lu,\"uptime_s\":%lu,\"rssi\":%d}",
           m.temp, m.fukt,
           (unsigned long) alderSekunder(m),
           (unsigned long) oppetidSekunder(),
           (int) WiFi.RSSI());

  mqtt.publish(MQTT_EMNE, json);
}

static void kobleTilBroker() {
  Serial.printf("[inneklima] Kobler til MQTT-broker %s:%u ...\n", MQTT_BROKER, MQTT_PORT);

  if (mqtt.connect(VERTSNAVN, MQTT_BRUKER, MQTT_PASSORD)) {
    Serial.println("[inneklima] Koblet til brokeren.");
  } else {
    Serial.printf("[inneklima] Brokeren svarte ikke (state %d). Kjorer Mosquitto, "
                  "og stemmer adressen, brukeren og passordet i wifi_secrets.h?\n", mqtt.state());
  }
}

static const char *wifiGrunn() {
  switch (WiFi.status()) {
    case WL_NO_SSID_AVAIL:
      return "finner ikke nettet - sjekk navnet, og at det er 2,4 GHz";
    case WL_CONNECT_FAILED:
      return "nektet - som regel feil passord";
    case WL_CONNECTION_LOST:
      return "mistet forbindelsen";
    case WL_IDLE_STATUS:
    case WL_DISCONNECTED:
      return "ikke tilkoblet ennaa";
    default:
      return "ukjent tilstand";
  }
}

// Lister nettene ESP-en ser. Er vaart nett med, er det passordet som er feil.
// Er det ikke med, er det feil navn, utenfor rekkevidde eller 5 GHz.
static void skannNett() {
  Serial.println("[inneklima] Skanner etter nett ...");

  const int16_t antall = WiFi.scanNetworks();

  if (antall <= 0) {
    Serial.println("[inneklima] Fant ingen nett i det hele tatt. Er antennen tilkoblet?");
    WiFi.scanDelete();
    return;
  }

  bool fantVaart = false;

  for (int16_t i = 0; i < antall; i++) {
    const String navn = WiFi.SSID(i);

    bool vaart = false;
    for (size_t n = 0; n < ANTALL_NETT; n++) {
      if (navn == WIFI_NETT[n][0]) vaart = true;
    }
    if (vaart) fantVaart = true;

    Serial.printf("[inneklima]   %s%-32s %4d dBm  kanal %ld\n",
                  vaart ? "-> " : "   ", navn.c_str(),
                  (int) WiFi.RSSI(i), (long) WiFi.channel(i));
  }

  if (fantVaart) {
    Serial.println("[inneklima] Et av nettene i wifi_secrets.h ER synlig (merket ->). "
                   "Kommer vi likevel ikke paa, er det passordet.");
  } else {
    Serial.println("[inneklima] Ingen av nettene i wifi_secrets.h er blant nettene over. "
                   "Enten er navnet feilskrevet, eller saa er nettet 5 GHz - denne "
                   "brikken ser bare 2,4 GHz. Paa iPhone: slaa paa \"Maksimer "
                   "kompatibilitet\".");
  }

  // Frigjor resultatlista, ellers lekker hver skanning minne
  WiFi.scanDelete();
}

static void startWifi() {
  WiFi.mode(WIFI_STA);

  // Hostname maa settes etter mode() og for tilkobling, ellers sendes
  // DHCP-forespoerselen med standardnavnet
  WiFi.setHostname(VERTSNAVN);
  WiFi.setAutoReconnect(true);

  // Med sparemodus faller MQTT-forbindelsen ut
  WiFi.setSleep(false);

  for (size_t i = 0; i < ANTALL_NETT; i++) {
    wifiMulti.addAP(WIFI_NETT[i][0], WIFI_NETT[i][1]);
  }
}

// Kobler til det sterkeste kjente nettet. Blokkerer noen sekunder.
static void kobleTil() {
  Serial.printf("[inneklima] Leter etter %u kjente nett ...\n", (unsigned) ANTALL_NETT);

  if (wifiMulti.run(WIFI_KOBLE_MS) != WL_CONNECTED) {
    Serial.printf("[inneklima] Kom ikke paa noe nett: %s.\n", wifiGrunn());
  }

  wifiForsoktFra = millis();
}

// Bare denne tasken bruker I2C, saa bussen trenger ingen mutex
static void sensorTask(void *) {
  sensorKlar = startSensor();

  TickType_t neste = xTaskGetTickCount();

  for (;;) {
    // Prov aa starte sensoren igjen hvis den har falt ut
    if (!sensorKlar) sensorKlar = startSensor();

    if (sensorKlar) lesSensor();

    vTaskDelayUntil(&neste, pdMS_TO_TICKS(MAALEINTERVALL_MS));
  }
}

static void nettTask(void *) {
  startWifi();
  kobleTil();

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);

  bool varTilkoblet = false;
  bool harSkannet = false;
  uint32_t sisteMqttForsok = 0;
  uint32_t sistPublisert = 0;

  for (;;) {
    const uint32_t naa = millis();
    const bool tilkoblet = (WiFi.status() == WL_CONNECTED);

    if (tilkoblet && !varTilkoblet) {
      Serial.printf("[inneklima] Tilkoblet \"%s\". IP %s, RSSI %d dBm\n",
                    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }

    if (tilkoblet && !mqtt.connected() && (naa - sisteMqttForsok) > MQTT_FORSOK_MS) {
      sisteMqttForsok = naa;
      kobleTilBroker();
    }

    if (mqtt.connected()) {
      mqtt.loop();

      if (naa - sistPublisert >= PUBLISERINGSINTERVALL_MS) {
        sistPublisert = naa;
        publiser();
      }
    }

    if (!tilkoblet && varTilkoblet) {
      Serial.println("[inneklima] Mistet Wi-Fi.");
      wifiForsoktFra = naa;
    }

    varTilkoblet = tilkoblet;

    // setAutoReconnect() prover bare samme nett. Gir den opp, skannes alle
    // kjente nett paa nytt, saa ESP-en kan bytte mellom hjem og hotspot.
    if (!tilkoblet && (naa - wifiForsoktFra) > WIFI_FORSOK_MS) {
      Serial.printf("[inneklima] Ikke tilkoblet: %s.\n", wifiGrunn());

      // Skann bare forste gang, ellers fylles seriemonitoren opp
      if (!harSkannet) {
        harSkannet = true;
        skannNett();
      }

      WiFi.disconnect();
      kobleTil();
    }

    // Uten denne sulter idle-tasken og watchdogen slaar til
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void setup() {
  Serial.begin(115200);

  // USB-serieporten paa S3 trenger litt tid, ellers forsvinner de forste linjene
  delay(1500);

  Serial.println();
  Serial.println("[inneklima] Starter.");

  maalingMutex = xSemaphoreCreateMutex();

  // Begge paa kjerne 1, kjerne 0 er Wi-Fi sin
  xTaskCreatePinnedToCore(sensorTask, "sensor", 4096, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(nettTask, "nett", 8192, nullptr, 1, nullptr, 1);
}

// Alt skjer i taskene, saa loop-tasken sletter seg selv
void loop() {
  vTaskDelete(nullptr);
}
