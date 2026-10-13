/* inneklima - ESP32-S3 med AHT20
 *
 * Publiserer innetemperatur og luftfuktighet som JSON over MQTT, paa emnet
 * kollektivmonitor/inneklima. server.js paa Pi-en abonnerer og vurderer
 * temperaturen.
 *
 * Brokeren (Mosquitto) kjorer paa samme maskin som skjermen. Adressen
 * staar i wifi_secrets.h, saa flytting fra Mac til Pi betyr ny opplasting.
 *
 * Maskinvare:  ESP32-S3 Feather. Siste fungerende opplasting ble gjort med
 *              board "Adafruit Feather ESP32-S3" i esp32-kjernen fra
 *              Espressif, og den identifiserte brikken som ESP32-S3.
 *              Eldre notater sier "UM FeatherS3" - sjekk hva som staar
 *              paa brettet, og velg boardet som matcher.
 * Sensor:      AHT20 paa I2C. Wire.begin() uten argumenter tar pinnene
 *              boardvalget sier, saa koden er den samme for begge brettene
 *              - det er boardvalget som maa vaere riktig.
 * Bibliotek:   Adafruit AHTX0 (2.0.6) med Adafruit Unified Sensor + BusIO,
 *              og PubSubClient (Nick O'Leary) for MQTT
 *
 * FreeRTOS - TO TASKS OG EN MUTEX, samme oppbygning som esp/soppel/:
 *
 *   sensorTask  prio 2   Leser AHT20 hvert 10. sekund og skriver siste
 *                        maaling til den delte tilstanden.
 *   nettTask    prio 1   Wi-Fi og MQTT. Publiserer hvert 10. sekund.
 *
 *   Mutexen beskytter maalingen, som sensorTask skriver og nettTask leser
 *   naar den publiserer. Uten den kunne en melding faatt temperaturen fra
 *   en maaling og fuktigheten fra den neste.
 *
 * Meldingen:
 *   {"ok":true,"temperatur":21.4,"fuktighet":38.2,"alder_s":3,...}
 *   {"ok":false,"feil":"..."} naar det ikke finnes en gyldig maaling.
 *
 * Se paa meldingene fra en maskin paa nettet:
 *   mosquitto_sub -h <broker> -t kollektivmonitor/inneklima -v
 *
 * Wi-Fi-nettene og brokeren ligger i wifi_secrets.h i samme mappe, som IKKE er i Git.
 * Kopier wifi_secrets.example.h til wifi_secrets.h og fyll inn. Mangler
 * fila, stopper kompileringen paa #include-linja under - med vilje.
 *
 * De laa en periode rett i denne fila med plassholdere i Git. Det er byttet
 * tilbake til en egen fil fordi det naa er flere nett (hjem og hotspot), og
 * fordi et ekte passord i en fil som er i Git ligger i historikken for
 * alltid saa snart noen committer den ved et uhell.
 */

#include <WiFi.h>
#include <WiFiMulti.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>

#include "wifi_secrets.h"

/*
 * Nettene (WIFI_NETT) staar i wifi_secrets.h.
 *
 * MERK: ESP32-S3 har ingen 5 GHz-radio. Nettene maa vaere 2,4 GHz. Er et
 * nett ikke det, finner ESP-en det ikke i det hele tatt - og loggen sier da
 * "finner ikke nettet", ikke "feil passord".
 */
static const size_t ANTALL_NETT = sizeof(WIFI_NETT) / sizeof(WIFI_NETT[0]);

// DHCP-hostname, og klient-ID mot MQTT-brokeren.
static const char *VERTSNAVN = "inneklima";

static const char *MQTT_EMNE = "kollektivmonitor/inneklima";

// Hvor ofte maalingen publiseres. Samme takt som sensoren leses.
static const uint32_t PUBLISERINGSINTERVALL_MS = 10000;

// Hvor lenge vi venter mellom forsoek paa aa koble til brokeren.
static const uint32_t MQTT_FORSOK_MS = 5000;

/*
 * Kalibrering mot selvoppvarming, i grader.
 *
 * ESP32-S3 med Wi-Fi paa varmer seg selv, og sitter sensoren tett paa
 * brettet leser den for hoyt. Mal mot et termometer du stoler paa og legg
 * inn differansen her - staar det 22,8 paa ESP-en og 21,4 paa termometeret,
 * er TEMP_OFFSET -1.4.
 *
 * Standarden er 0.0 med vilje: en ukalibrert avlesning skal vaere raa, ikke
 * korrigert med et tall noen har gjettet. Den beste kalibreringen er
 * uansett aa flytte sensoren bort fra brettet paa en kabelstubb.
 */
static const float TEMP_OFFSET = 0.0;

// Hvor ofte sensoren leses. Inneklima endrer seg langsomt; 10 s er rikelig.
static const uint32_t MAALEINTERVALL_MS = 10000;

/*
 * Hvor lenge vi venter paa Wi-Fi for vi konkluderer med at forsoket er dodt
 * og starter et nytt. setAutoReconnect() tar de vanlige bruddene, men den
 * gir opp hvis ruteren er borte lenge nok - dette er nettet under den.
 */
static const uint32_t WIFI_FORSOK_MS = 20000;

// Hvor lenge wifiMulti.run() venter paa at det valgte nettet svarer.
static const uint32_t WIFI_KOBLE_MS = 10000;

static Adafruit_AHTX0 aht;
static WiFiMulti wifiMulti;
static WiFiClient wifiKlient;
static PubSubClient mqtt(wifiKlient);

/*
 * Siste gyldige maaling. Den blir staaende til en ny kommer inn.
 *
 * sensorTask skriver, nettTask leser, og begge gjor det bare med
 * maalingMutex tatt. feil er en char-tabell og ikke String, saa en kopi av
 * hele structen er en ren minnekopi uten heap.
 */
struct Maaling {
  bool harMaaling;
  float temp;
  float fukt;
  uint32_t maaltMs;
  char feil[40];   // tom naar alt gaar bra
};

static Maaling maaling = { false, 0.0f, 0.0f, 0, "ingen maaling ennaa" };
static SemaphoreHandle_t maalingMutex;

// Brukes bare av sensorTask og nettTask hver for seg - ingen mutex.
static bool sensorKlar = false;
static uint32_t wifiForsoktFra = 0;

/**
 * Alderen paa siste maaling i hele sekunder.
 *
 * Differansen regnes i unsigned aritmetikk, saa den er riktig ogsaa naar
 * millis() ruller rundt etter 49 dogn.
 */
static uint32_t alderSekunder(const Maaling &m) {
  if (!m.harMaaling) return 0;
  return (uint32_t) ((millis() - m.maaltMs) / 1000UL);
}

/** Henter en kopi av maalingen. Mutexen holdes bare mens vi kopierer. */
static Maaling lesMaaling() {
  xSemaphoreTake(maalingMutex, portMAX_DELAY);
  const Maaling m = maaling;
  xSemaphoreGive(maalingMutex);
  return m;
}

/** Setter feilteksten. Forrige gyldige maaling blir staaende. */
static void settFeil(const char *tekst) {
  xSemaphoreTake(maalingMutex, portMAX_DELAY);
  strlcpy(maaling.feil, tekst, sizeof(maaling.feil));
  xSemaphoreGive(maalingMutex);
}

/**
 * Oppetid i sekunder.
 *
 * esp_timer_get_time() gir mikrosekunder siden oppstart som int64, saa
 * tallet fortsetter aa stemme forbi de 49 dognene der millis() ruller rundt.
 */
static uint32_t oppetidSekunder() {
  return (uint32_t) (esp_timer_get_time() / 1000000LL);
}

/** Starter I2C og sensoren. Kalles paa nytt fra loop() hvis den feiler. */
static bool startSensor() {
  // Uten argumenter tar Wire brettets egne pinner - SDA=8, SCL=9 paa FeatherS3.
  Wire.begin();

  if (!aht.begin()) {
    Serial.println("[inneklima] Fant ingen AHT20 paa I2C. Sjekk ledningene.");
    settFeil("fant ingen sensor");
    return false;
  }

  Serial.println("[inneklima] AHT20 funnet.");
  return true;
}

/** Leser sensoren og oppdaterer siste gyldige maaling. */
static void lesSensor() {
  sensors_event_t fukt, temp;

  if (!aht.getEvent(&fukt, &temp)) {
    Serial.println("[inneklima] Sensoren svarte ikke paa avlesning.");
    settFeil("sensoren svarte ikke");
    sensorKlar = false;
    return;
  }

  /*
   * En AHT20 som har mistet kontakten leverer gjerne 0,0 eller NaN i stedet
   * for aa feile. Det slippes ikke gjennom: en tom plass paa skjermen er
   * aerligere enn "Inne 0,0 grader".
   */
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

/** Publiserer siste maaling, eller feilen naar det ikke finnes en. */
static void publiser() {
  const Maaling m = lesMaaling();
  char json[192];

  if (!m.harMaaling) {
    snprintf(json, sizeof(json), "{\"ok\":false,\"feil\":\"%s\"}",
             m.feil[0] ? m.feil : "ingen maaling ennaa");
    mqtt.publish(MQTT_EMNE, json);
    return;
  }

  /*
   * En gammel maaling er ogsaa en feil, men det er server.js som eier den
   * grensen (maxAlderMin i config.json). ESP-en oppgir alderen og lar
   * klienten bestemme - da kan grensen endres uten aa flashe paa nytt.
   *
   * %.1f og ikke %.2f: AHT20 oppgir selv ca. 0,01 i opplosning, men
   * noyaktigheten er +/- 0,3 grader og +/- 2 % RF. Flere desimaler ville
   * vaert presisjon vi ikke har.
   */
  snprintf(json, sizeof(json),
           "{\"ok\":true,\"temperatur\":%.1f,\"fuktighet\":%.1f,"
           "\"alder_s\":%lu,\"uptime_s\":%lu,\"rssi\":%d}",
           m.temp, m.fukt,
           (unsigned long) alderSekunder(m),
           (unsigned long) oppetidSekunder(),
           (int) WiFi.RSSI());

  mqtt.publish(MQTT_EMNE, json);
}

/**
 * Kobler til brokeren. Feiler det, proeves det igjen etter MQTT_FORSOK_MS -
 * maalingene gaar videre i sensorTask saa lenge.
 */
static void kobleTilBroker() {
  Serial.printf("[inneklima] Kobler til MQTT-broker %s:%u ...\n", MQTT_BROKER, MQTT_PORT);

  if (mqtt.connect(VERTSNAVN)) {
    Serial.println("[inneklima] Koblet til brokeren.");
  } else {
    Serial.printf("[inneklima] Brokeren svarte ikke (state %d). Kjorer Mosquitto, "
                  "og er adressen i wifi_secrets.h riktig?\n", mqtt.state());
  }
}

/**
 * Oversetter WiFi.status() til en grunn et menneske kan handle paa.
 *
 * Uten dette sier loggen bare "gir opp og starter et nytt" om og om igjen,
 * og de to vanligste feilene ser helt like ut derfra: nettet finnes ikke
 * (feil SSID, eller 5 GHz-nett som ESP32-S3 ikke ser), og passordet er
 * feil. Det er to helt forskjellige ting aa rette.
 */
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

/**
 * Lister nettene ESP-en faktisk ser, og sier om vaart er blant dem.
 *
 * Dette er den ene proven som avgjor saken naar en ESP ikke vil koble seg
 * paa. Staar nettet i lista, er navnet riktig og radioen naar det - da er
 * det passordet. Staar det IKKE i lista, hjelper det ikke aa skrive
 * passordet om igjen: da er nettet enten utenfor rekkevidde, eller saa er
 * det 5 GHz, som denne brikken ikke har radio for i det hele tatt.
 *
 * Det siste er den vanligste fellen med telefon-hotspot. En iPhone deler
 * som standard paa 5 GHz, og maa settes i "Maksimer kompatibilitet" for aa
 * gaa ned paa 2,4 GHz. Uten den staar nettet fullt synlig paa telefonen og
 * er likevel usynlig for ESP-en.
 *
 * Skanningen blokkerer i et par sekunder. Det er greit her: den kjorer bare
 * naar vi allerede har staatt fast i WIFI_FORSOK_MS.
 */
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

  /*
   * Resultatlista ligger i minnet til den slettes. Uten dette lekker vi
   * noen kilobyte for hver skanning, og en ESP som staar og feiler i et
   * dogn ville skannet 4000 ganger.
   */
  WiFi.scanDelete();
}

/** Setter opp Wi-Fi og legger inn nettene. Kobler ikke til - se kobleTil(). */
static void startWifi() {
  WiFi.mode(WIFI_STA);

  /*
   * Rekkefolgen er ikke likegyldig: hostname maa settes etter at modusen er
   * satt og FOR tilkoblingen, ellers er DHCP-forespoerselen allerede sendt
   * med kjernens standardnavn ("espressif").
   */
  WiFi.setHostname(VERTSNAVN);
  WiFi.setAutoReconnect(true);

  // Sparemodus gir lange pauser i radioen, og da faller MQTT-forbindelsen ut.
  WiFi.setSleep(false);

  for (size_t i = 0; i < ANTALL_NETT; i++) {
    wifiMulti.addAP(WIFI_NETT[i][0], WIFI_NETT[i][1]);
  }
}

/**
 * Skanner og kobler til det sterkeste av de kjente nettene.
 *
 * wifiMulti.run() blokkerer i noen sekunder (skann + inntil WIFI_KOBLE_MS).
 * Det er greit her: uten nett har nettTask ingenting aa gjore, og
 * sensorTask gaar videre som foer.
 */
static void kobleTil() {
  Serial.printf("[inneklima] Leter etter %u kjente nett ...\n", (unsigned) ANTALL_NETT);

  if (wifiMulti.run(WIFI_KOBLE_MS) != WL_CONNECTED) {
    Serial.printf("[inneklima] Kom ikke paa noe nett: %s.\n", wifiGrunn());
  }

  wifiForsoktFra = millis();
}

/**
 * sensorTask - leser AHT20 hvert MAALEINTERVALL_MS.
 *
 * Wire og sensoren brukes bare herfra, saa I2C-bussen trenger ingen egen
 * mutex. Legges det til en task til som snakker I2C, trenger den det.
 */
static void sensorTask(void *) {
  sensorKlar = startSensor();

  // Fast takt fra start til start, uansett hvor lang tid avlesningen tok.
  TickType_t neste = xTaskGetTickCount();

  for (;;) {
    // Falt sensoren ut, prov aa faa den opp igjen for vi leser.
    if (!sensorKlar) sensorKlar = startSensor();

    if (sensorKlar) lesSensor();

    vTaskDelayUntil(&neste, pdMS_TO_TICKS(MAALEINTERVALL_MS));
  }
}

/** nettTask - Wi-Fi og MQTT. */
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
      // loop() holder forbindelsen i live med ping mot brokeren.
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

    /*
     * setAutoReconnect() tar de korte bruddene selv, men bare mot nettet vi
     * var paa. Dette er nettet under den: har det gaatt lenge nok uten at vi
     * kom oss paa, skannes det paa nytt blant ALLE kjente nett - saa ESP-en
     * finner hotspoten naar hjemmenettet er borte, og omvendt.
     */
    if (!tilkoblet && (naa - wifiForsoktFra) > WIFI_FORSOK_MS) {
      Serial.printf("[inneklima] Ikke tilkoblet: %s.\n", wifiGrunn());

      /*
       * Skann bare paa det forste mislykkede forsoeket. Etter det er svaret
       * det samme hver gang, og en full liste hvert 20. sekund gjor
       * seriemonitoren ulesbar akkurat naar man trenger aa lese den.
       */
      if (!harSkannet) {
        harSkannet = true;
        skannNett();
      }

      WiFi.disconnect();
      kobleTil();
    }

    /*
     * Gir fra seg CPU-en. Uten denne spinner tasken paa 100 % og sulter
     * idle-tasken, og da slaar watchdogen til.
     */
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void setup() {
  Serial.begin(115200);

  /*
   * ESP32-S3 har USB-CDC som seriell port, og den er ikke oppe med en gang
   * etter reset. Uten en liten pause forsvinner de forste linjene - blant
   * annet IP-adressen, som er akkurat den man sitter og venter paa.
   */
  delay(1500);

  Serial.println();
  Serial.println("[inneklima] Starter.");

  maalingMutex = xSemaphoreCreateMutex();

  // Begge paa kjerne 1. Kjerne 0 er Wi-Fi-stakkens, og den lar vi vaere i fred.
  xTaskCreatePinnedToCore(sensorTask, "sensor", 4096, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(nettTask, "nett", 8192, nullptr, 1, nullptr, 1);
}

/*
 * loop() er selv en FreeRTOS-task i Arduino-kjernen. Alt arbeid er i
 * taskene over, saa den sletter seg selv.
 */
void loop() {
  vTaskDelete(nullptr);
}
