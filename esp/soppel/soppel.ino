/* soppel - ESP32 med HC-SR04 i lokket paa soppelbotta
 *
 * Maaler hvor full botta er, merker naar den er tomt, og holder styr paa
 * hvem som har soppelansvaret. Statusen publiseres som JSON paa
 * kollektivmonitor/soppel. Ansvaret lagres i flash (Preferences) her og
 * ikke paa Pi-en, saa det overlever at noen av dem er av en stund.
 *
 * Board:     "Adafruit ESP32 Feather" (HUZZAH32) i esp32-kjernen
 * Sensor:    HC-SR04, festet oeverst i botta og pekt rett ned
 * Bibliotek: PubSubClient
 *
 * Kobling:
 *   VCC  -> USB (5 V), sensoren maaler daarlig paa 3,3 V
 *   GND  -> GND
 *   TRIG -> GPIO 26 (A0)
 *   ECHO -> spenningsdeler -> GPIO 25 (A1)
 *   ECHO er 5 V. Deleren er 1 kOhm fra ECHO til pinnen og 2 kOhm til GND.
 *
 * Tasks:
 *   maaleTask     prio 3  median av fem ekko, legges i maaleKo
 *   tilstandTask  prio 2  fyllingsgrad, nivaa, tomming og ansvar
 *   nettTask      prio 1  Wi-Fi og MQTT, publiserer hvert 5. sekund
 *   maaleTask har hoyest prioritet fordi pulseIn() maaler tid. statusMutex
 *   beskytter status, som tilstandTask skriver og nettTask leser.
 *
 * Meldingen:
 *   {"ok":true,"fyll":72,"nivaa":"delvis","avstand_cm":18.3,
 *    "ansvarlig":"Henrik","neste":"Sigurd",...}
 *   {"ok":false,"feil":"...","ansvarlig":"Henrik","neste":"Sigurd"}
 *
 * Kopier wifi_secrets.example.h til wifi_secrets.h og fyll inn.
 */

#include <WiFi.h>
#include <WiFiMulti.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <time.h>

#include "wifi_secrets.h"

static const size_t ANTALL_NETT = sizeof(WIFI_NETT) / sizeof(WIFI_NETT[0]);

// Brukes som hostname og MQTT-klient-ID
static const char *VERTSNAVN = "soppel";

static const char *MQTT_EMNE = "kollektivmonitor/soppel";

static const uint8_t PIN_TRIG = 26;
static const uint8_t PIN_ECHO = 25;

// Avstand fra sensoren til bunnen av en tom pose, og til der botta regnes
// som full. TOM er maalt med avstand_cm i meldingen med tom botte.
static const float AVSTAND_TOM_CM = 45.0;
static const float AVSTAND_FULL_CM = 8.0;

// Nivaaer i prosent: tom < 15, delvis, nesten full > 75, full > 90.
// Hysteresen hindrer at nivaaet blafrer naar fyllingen ligger paa en grense.
static const float GRENSE_TOM = 15.0;
static const float GRENSE_NESTEN_FULL = 75.0;
static const float GRENSE_FULL = 90.0;
static const float HYSTERESE = 3.0;

// Botta maa ha vaert tom saa lenge for det telles som tomming, saa en haand
// i aapningen ikke teller.
static const uint32_t TOM_STABIL_MS = 20000;

// Rekkefolgen ansvaret gaar i. Ansvaret lagres som plass i lista, saa
// endres lista, flytter ansvaret seg.
static const char *BEBOERE[] = { "Elias", "Henrik", "Sigurd" };
static const uint8_t ANTALL_BEBOERE = sizeof(BEBOERE) / sizeof(BEBOERE[0]);

static const uint32_t MAALEINTERVALL_MS = 2000;

// Median og ikke snitt, siden ett ekko fra kanten av posen kan bli helt feil
static const uint8_t EKKO_PER_MAALING = 5;
static const uint8_t MIN_GYLDIGE_EKKO = 3;

static const uint32_t EKKO_PAUSE_MS = 60;
static const uint32_t EKKO_TIMEOUT_US = 30000;  // ca. 5 m

// Lenger enn bunnen er som regel aapent lokk, og ignoreres
static const float MIN_AVSTAND_CM = 2.0;
static const float MAKS_AVSTAND_CM = AVSTAND_TOM_CM + 10.0;

// Eldre maaling enn dette sendes som ok:false
static const uint32_t MAKS_ALDER_MS = 30000;

static const uint32_t PUBLISERINGSINTERVALL_MS = 5000;
static const uint32_t MQTT_FORSOK_MS = 5000;
static const uint32_t WIFI_FORSOK_MS = 20000;
static const uint32_t WIFI_KOBLE_MS = 10000;

// Ingen funksjoner over typene under. Arduino IDE setter prototypene foran
// den forste funksjonen, og da faar man "does not name a type".

enum Nivaa : uint8_t { TOM, DELVIS, NESTEN_FULL, FULL };

struct Maaling {
  bool gyldig;
  float avstandCm;
};

struct Status {
  bool harMaaling;
  float avstandCm;
  float fyllProsent;
  Nivaa nivaa;
  uint32_t sisteMaalingMs;

  uint8_t ansvarlig;          // plass i BEBOERE
  uint32_t antallTomminger;
  bool klarForTomming;        // har vaert nesten full siden sist tomming
  uint32_t tomFraMs;          // 0 = teller ikke
  time_t sisteTomming;        // Unix-tid, 0 = ukjent

  char feil[48];              // tom naar alt er bra
};

static Status status = {};
static SemaphoreHandle_t statusMutex;
static QueueHandle_t maaleKo;

static Preferences lager;
static WiFiMulti wifiMulti;
static WiFiClient wifiKlient;
static PubSubClient mqtt(wifiKlient);

static const char *nivaaNavn(Nivaa n) {
  switch (n) {
    case TOM: return "tom";
    case DELVIS: return "delvis";
    case NESTEN_FULL: return "nesten_full";
    case FULL: return "full";
  }
  return "ukjent";
}

// ---- Lagring ----

// klarForTomming lagres ogsaa, saa en tomming mens ESP-en er av telles.
// Det skrives bare naar noe endrer seg, saa flash-slitasje er ikke noe problem.
static void lesLager() {
  lager.begin("soppel", true);
  status.ansvarlig = lager.getUChar("ansvarlig", 0);
  status.antallTomminger = lager.getUInt("tomminger", 0);
  status.klarForTomming = lager.getBool("klar", false);
  status.sisteTomming = (time_t) lager.getULong64("sist", 0);
  lager.end();

  // Lista kan ha blitt kortere siden sist
  if (status.ansvarlig >= ANTALL_BEBOERE) status.ansvarlig = 0;

  Serial.printf("[soppel] Fra lager: %s har ansvaret, %lu tomminger totalt.\n",
                BEBOERE[status.ansvarlig], (unsigned long) status.antallTomminger);
}

// Tar en kopi og kalles uten mutexen
static void skrivLager(const Status &s) {
  lager.begin("soppel", false);
  lager.putUChar("ansvarlig", s.ansvarlig);
  lager.putUInt("tomminger", s.antallTomminger);
  lager.putBool("klar", s.klarForTomming);
  lager.putULong64("sist", (uint64_t) s.sisteTomming);
  lager.end();
}

// ---- Maaling ----

// Avstand i cm, eller NAN uten gyldig ekko
static float ettEkko() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  const unsigned long us = pulseIn(PIN_ECHO, HIGH, EKKO_TIMEOUT_US);
  if (us == 0) return NAN;

  // Lydfart 0,0343 cm/us, og ekkoet gaar frem og tilbake
  const float cm = us * 0.0343f / 2.0f;

  if (cm < MIN_AVSTAND_CM || cm > MAKS_AVSTAND_CM) return NAN;
  return cm;
}

static Maaling maalMedian() {
  float ekko[EKKO_PER_MAALING];
  uint8_t antall = 0;

  for (uint8_t i = 0; i < EKKO_PER_MAALING; i++) {
    const float cm = ettEkko();
    if (!isnan(cm)) ekko[antall++] = cm;
    vTaskDelay(pdMS_TO_TICKS(EKKO_PAUSE_MS));
  }

  if (antall < MIN_GYLDIGE_EKKO) return { false, 0.0f };

  // Innstikksortering, det er bare fem tall
  for (uint8_t i = 1; i < antall; i++) {
    const float v = ekko[i];
    int8_t j = i - 1;
    while (j >= 0 && ekko[j] > v) {
      ekko[j + 1] = ekko[j];
      j--;
    }
    ekko[j + 1] = v;
  }

  return { true, ekko[antall / 2] };
}

static void maaleTask(void *) {
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);

  // vTaskDelayUntil gir fast takt uansett hvor lang tid maalingen tar
  TickType_t neste = xTaskGetTickCount();

  for (;;) {
    const Maaling m = maalMedian();

    // Koen har plass til en maaling, og den nyeste overskriver
    xQueueOverwrite(maaleKo, &m);

    vTaskDelayUntil(&neste, pdMS_TO_TICKS(MAALEINTERVALL_MS));
  }
}

// ---- Tilstand ----

static float fyllProsent(float avstandCm) {
  const float p = (AVSTAND_TOM_CM - avstandCm) / (AVSTAND_TOM_CM - AVSTAND_FULL_CM) * 100.0f;
  return constrain(p, 0.0f, 100.0f);
}

static Nivaa raattNivaa(float p) {
  if (p >= GRENSE_FULL) return FULL;
  if (p >= GRENSE_NESTEN_FULL) return NESTEN_FULL;
  if (p <= GRENSE_TOM) return TOM;
  return DELVIS;
}

// Bytter bare nivaa naar fyllingen er HYSTERESE forbi grensen
static Nivaa nyttNivaa(Nivaa naa, float p) {
  const Nivaa opp = raattNivaa(p - HYSTERESE);
  if (opp > naa) return opp;

  const Nivaa ned = raattNivaa(p + HYSTERESE);
  if (ned < naa) return ned;

  return naa;
}

// 0 hvis klokka ikke er satt fra NTP enda (foer 2020)
static time_t naaUnix() {
  const time_t t = time(nullptr);
  return t > 1577836800 ? t : 0;
}

// Kalles med statusMutex tatt. Returnerer true naar noe skal lagres.
static bool behandle(const Maaling &m, uint32_t naaMs) {
  if (!m.gyldig) {
    // Gamle verdier blir staaende, saa et aapent lokk ikke ser ut som tom botte
    strlcpy(status.feil, "ingen gyldig måling - lokket åpent?", sizeof(status.feil));
    return false;
  }

  status.harMaaling = true;
  status.feil[0] = '\0';
  status.avstandCm = m.avstandCm;
  status.fyllProsent = fyllProsent(m.avstandCm);
  status.sisteMaalingMs = naaMs;

  const Nivaa forrige = status.nivaa;
  status.nivaa = nyttNivaa(forrige, status.fyllProsent);

  if (status.nivaa != forrige) {
    Serial.printf("[soppel] %s -> %s (%.0f %%)\n",
                  nivaaNavn(forrige), nivaaNavn(status.nivaa), status.fyllProsent);
  }

  // Full nok til at en tomming skal telle?
  if (status.nivaa >= NESTEN_FULL && !status.klarForTomming) {
    status.klarForTomming = true;
    return true;
  }

  // Tommet?
  if (!status.klarForTomming || status.nivaa != TOM) {
    status.tomFraMs = 0;
    return false;
  }

  // 0 betyr "teller ikke", saa millis() == 0 blir 1
  if (status.tomFraMs == 0) {
    status.tomFraMs = naaMs ? naaMs : 1;
    Serial.println("[soppel] Botta ser tom ut. Venter paa at det holder seg.");
    return false;
  }

  if (naaMs - status.tomFraMs < TOM_STABIL_MS) return false;

  // Tommet, ansvaret gaar videre
  const uint8_t tommet = status.ansvarlig;
  status.ansvarlig = (status.ansvarlig + 1) % ANTALL_BEBOERE;
  status.antallTomminger++;
  status.klarForTomming = false;
  status.tomFraMs = 0;
  status.sisteTomming = naaUnix();

  Serial.printf("[soppel] Tomming registrert (%s). Naa er det %s sin tur.\n",
                BEBOERE[tommet], BEBOERE[status.ansvarlig]);
  return true;
}

static void tilstandTask(void *) {
  Maaling m;

  for (;;) {
    // Ingenting paa tre intervaller betyr at maaleTask har hengt seg
    if (xQueueReceive(maaleKo, &m, pdMS_TO_TICKS(3 * MAALEINTERVALL_MS)) != pdTRUE) {
      xSemaphoreTake(statusMutex, portMAX_DELAY);
      strlcpy(status.feil, "maaleTask svarer ikke", sizeof(status.feil));
      xSemaphoreGive(statusMutex);
      continue;
    }

    xSemaphoreTake(statusMutex, portMAX_DELAY);
    const bool lagre = behandle(m, millis());
    const Status kopi = status;
    xSemaphoreGive(statusMutex);

    // Flash-skriving tar noen ms, saa den gjores etter at mutexen er gitt fra seg
    if (lagre) skrivLager(kopi);
  }
}

// ---- Nett ----

static Status lesStatus() {
  xSemaphoreTake(statusMutex, portMAX_DELAY);
  const Status s = status;
  xSemaphoreGive(statusMutex);
  return s;
}

static uint32_t alderSekunder(const Status &s) {
  if (!s.harMaaling) return 0;
  return (uint32_t) ((millis() - s.sisteMaalingMs) / 1000UL);
}

static uint32_t oppetidSekunder() {
  return (uint32_t) (esp_timer_get_time() / 1000000LL);
}

static void publiser() {
  const Status s = lesStatus();
  const char *ansvarlig = BEBOERE[s.ansvarlig];
  const char *neste = BEBOERE[(s.ansvarlig + 1) % ANTALL_BEBOERE];

  char json[384];

  // Ansvaret sendes med selv uten fersk maaling
  if (!s.harMaaling || (millis() - s.sisteMaalingMs) > MAKS_ALDER_MS) {
    snprintf(json, sizeof(json),
             "{\"ok\":false,\"feil\":\"%s\",\"ansvarlig\":\"%s\",\"neste\":\"%s\"}",
             s.feil[0] ? s.feil : "ingen maaling ennaa", ansvarlig, neste);
    mqtt.publish(MQTT_EMNE, json);
    return;
  }

  // Fyll som heltall, sensoren er ikke mer noyaktig enn det
  snprintf(json, sizeof(json),
           "{\"ok\":true,\"fyll\":%d,\"nivaa\":\"%s\",\"avstand_cm\":%.1f,"
           "\"ansvarlig\":\"%s\",\"neste\":\"%s\",\"tomminger\":%lu,"
           "\"klar_for_tomming\":%s,\"siste_tomming\":%lld,"
           "\"alder_s\":%lu,\"uptime_s\":%lu,\"rssi\":%d}",
           (int) roundf(s.fyllProsent), nivaaNavn(s.nivaa), s.avstandCm,
           ansvarlig, neste, (unsigned long) s.antallTomminger,
           s.klarForTomming ? "true" : "false", (long long) s.sisteTomming,
           (unsigned long) alderSekunder(s), (unsigned long) oppetidSekunder(),
           (int) WiFi.RSSI());

  mqtt.publish(MQTT_EMNE, json);
}

static void kobleTilBroker() {
  Serial.printf("[soppel] Kobler til MQTT-broker %s:%u ...\n", MQTT_BROKER, MQTT_PORT);

  if (mqtt.connect(VERTSNAVN, MQTT_BRUKER, MQTT_PASSORD)) {
    Serial.println("[soppel] Koblet til brokeren.");
  } else {
    Serial.printf("[soppel] Brokeren svarte ikke (state %d). Kjorer Mosquitto, "
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

static void startWifi() {
  WiFi.mode(WIFI_STA);
  // Hostname maa settes etter mode() og for tilkobling
  WiFi.setHostname(VERTSNAVN);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);

  for (size_t i = 0; i < ANTALL_NETT; i++) {
    wifiMulti.addAP(WIFI_NETT[i][0], WIFI_NETT[i][1]);
  }
}

// Kobler til det sterkeste kjente nettet. Blokkerer noen sekunder.
static void kobleTil() {
  Serial.printf("[soppel] Leter etter %u kjente nett ...\n", (unsigned) ANTALL_NETT);

  if (wifiMulti.run(WIFI_KOBLE_MS) != WL_CONNECTED) {
    Serial.printf("[soppel] Kom ikke paa noe nett: %s.\n", wifiGrunn());
  }
}

static void nettTask(void *) {
  startWifi();
  kobleTil();

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  // Meldingen er lengre enn standardbufferet paa 256 bytes
  mqtt.setBufferSize(512);

  bool varTilkoblet = false;
  uint32_t forsoktFra = millis();
  uint32_t sisteMqttForsok = 0;
  uint32_t sistPublisert = 0;

  for (;;) {
    const uint32_t naa = millis();
    const bool tilkoblet = (WiFi.status() == WL_CONNECTED);

    if (tilkoblet && !varTilkoblet) {
      Serial.printf("[soppel] Tilkoblet \"%s\". IP %s, RSSI %d dBm\n",
                    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());

      // Klokka brukes bare til tidspunktet for siste tomming
      configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");
    }

    if (!tilkoblet && varTilkoblet) {
      Serial.println("[soppel] Mistet Wi-Fi.");
      forsoktFra = naa;
    }

    varTilkoblet = tilkoblet;

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

    // setAutoReconnect() prover bare samme nett. Gir den opp, skannes alle
    // kjente nett paa nytt, saa ESP-en kan bytte mellom hjem og hotspot.
    if (!tilkoblet && (naa - forsoktFra) > WIFI_FORSOK_MS) {
      WiFi.disconnect();
      kobleTil();
      forsoktFra = millis();
    }

    // Uten denne sulter idle-tasken og watchdogen slaar til
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("[soppel] Starter.");

  statusMutex = xSemaphoreCreateMutex();
  maaleKo = xQueueCreate(1, sizeof(Maaling));

  lesLager();

  // Alle paa kjerne 1, kjerne 0 er Wi-Fi sin. Stakk i bytes.
  xTaskCreatePinnedToCore(maaleTask, "maale", 3072, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(tilstandTask, "tilstand", 4096, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(nettTask, "nett", 8192, nullptr, 1, nullptr, 1);
}

// Alt skjer i taskene, saa loop-tasken sletter seg selv
void loop() {
  vTaskDelete(nullptr);
}
