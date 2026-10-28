/* soppel - ESP32 med HC-SR04 i lokket paa soppelbotta
 *
 * Maaler hvor full botta er, merker naar den er tomt, og holder styr paa
 * hvem i kollektivet som har soppelansvaret. Statusen publiseres som JSON
 * over MQTT paa emnet kollektivmonitor/soppel, og server.js paa Pi-en
 * abonnerer - akkurat som for inneklima-ESP-en.
 *
 * ESP-EN EIER ANSVARET. Hvem som staar for tur ligger i flashminnet her
 * (NVS, via Preferences) og ikke paa Pi-en. Da overlever rulleringen baade
 * at ESP-en mister stroemmen og at Pi-en er av i en uke - og det finnes
 * bare ett sted der sannheten bor. Pi-en viser, den bestemmer ingenting.
 *
 * Maskinvare:  Adafruit HUZZAH32 - ESP32 Feather (CP2104 USB-brikke)
 * Board:       "Adafruit ESP32 Feather" i esp32-kjernen fra Espressif
 * Sensor:      HC-SR04, festet oeverst i botta og pekt rett ned
 * Bibliotek:   PubSubClient (Nick O'Leary) for MQTT
 *
 * KOBLING (sjekk mot det som faktisk sitter paa brettet for du flasher):
 *
 *   HC-SR04 VCC   ->  USB-pinnen (5 V). HC-SR04 er en 5 V-sensor og maaler
 *                     upaalitelig paa 3,3 V.
 *   HC-SR04 GND   ->  GND
 *   HC-SR04 TRIG  ->  GPIO 26 (merket A0). 3,3 V holder som trigger.
 *   HC-SR04 ECHO  ->  spenningsdeler -> GPIO 25 (merket A1)
 *
 *   ECHO er 5 V og ESP32 taaler 3,3 V. Deleren er 1 kOhm fra ECHO til
 *   pinnen og 2 kOhm fra pinnen til GND, som gir 3,3 V. Uten den virker det
 *   gjerne en stund - og saa ikke.
 *
 * FreeRTOS - TRE TASKS, EN KOE, EN MUTEX:
 *
 *   maaleTask     prio 3   Pinger sensoren, tar median av fem ekko og
 *                          legger resultatet i maaleKo.
 *   tilstandTask  prio 2   Venter paa maaleKo. Regner fyllingsgrad og nivaa,
 *                          oppdager tomming, rullerer ansvaret og lagrer.
 *   nettTask      prio 1   Wi-Fi og MQTT. Publiserer hvert 5. sekund.
 *
 *   maaleTask har hoyest prioritet fordi pulseIn() maaler tid: blir tasken
 *   avbrutt midt i et ekko, blir avstanden feil. nettTask har lavest fordi
 *   ingenting gaar galt om en melding kommer 50 ms senere.
 *
 *   Koen skiller maalingen fra logikken - maaleTask vet ingenting om
 *   bottenivaa eller beboere. Mutexen beskytter `status`, som tilstandTask
 *   skriver og nettTask leser naar den publiserer.
 *
 * Meldingen:
 *   {"ok":true,"fyll":72,"nivaa":"delvis","avstand_cm":18.3,
 *    "ansvarlig":"Henrik","neste":"Sigurd",...}
 *   {"ok":false,"feil":"...","ansvarlig":"Henrik","neste":"Sigurd"} naar
 *   det ikke finnes en fersk maaling. Ansvaret er med uansett.
 *
 * Se paa meldingene (og avstand_cm til kalibrering) fra en maskin paa nettet:
 *   mosquitto_sub -h <broker> -t kollektivmonitor/soppel -v
 *
 * Wi-Fi-nettene, brokeren og MQTT-brukeren ligger i wifi_secrets.h i samme
 * mappe, som IKKE er i Git.
 * Kopier wifi_secrets.example.h til wifi_secrets.h og fyll inn. Mangler
 * fila, stopper kompileringen paa #include-linja under - med vilje.
 */

#include <WiFi.h>
#include <WiFiMulti.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <time.h>

#include "wifi_secrets.h"

// ===================================================================
// INNSTILLINGER
// ===================================================================

// Nettene (WIFI_NETT) staar i wifi_secrets.h. Alle maa vaere 2,4 GHz.
static const size_t ANTALL_NETT = sizeof(WIFI_NETT) / sizeof(WIFI_NETT[0]);

// DHCP-hostname, og klient-ID mot MQTT-brokeren.
static const char *VERTSNAVN = "soppel";

static const char *MQTT_EMNE = "kollektivmonitor/soppel";

static const uint8_t PIN_TRIG = 26;
static const uint8_t PIN_ECHO = 25;

/*
 * BOTTA
 *
 * AVSTAND_TOM_CM er fra sensoren ned til bunnen av en tom pose. Botta er
 * ca. 45 cm, men maal det med sensoren paa plass: les avstand_cm i
 * MQTT-meldingen med tom botta. Det tallet skal staa her.
 *
 * AVSTAND_FULL_CM er der vi kaller botta 100 % full. Ikke 0: lokket maa
 * kunne lukkes, og HC-SR04 maaler ikke under ca. 2 cm uansett.
 */
static const float AVSTAND_TOM_CM = 45.0;
static const float AVSTAND_FULL_CM = 8.0;

/*
 * NIVAAENE, i prosent fyllingsgrad.
 *
 *   0 - 15    tom
 *  15 - 75    delvis
 *  75 - 90    nesten full   -> tomming kan registreres etterpaa
 *  90 -       full
 *
 * HYSTERESE er hvor langt forbi en grense tallet maa for nivaaet bytter.
 * Uten den blafrer nivaaet frem og tilbake naar innholdet ligger rett paa
 * en grense - og hvert blaff er en ny farge paa skjermen.
 */
static const float GRENSE_TOM = 15.0;
static const float GRENSE_NESTEN_FULL = 75.0;
static const float GRENSE_FULL = 90.0;
static const float HYSTERESE = 3.0;

/*
 * Hvor lenge botta maa ha staatt tom for det regnes som en tomming.
 *
 * Et minutt er lengre enn det tar aa loefte ut posen og sette inn en ny,
 * saa en haand eller en pose som svinger i aapningen blir ikke tatt for en
 * tom botte.
 */
static const uint32_t TOM_STABIL_MS = 60000;

/*
 * Beboerne, i den rekkefolgen ansvaret gaar. Samme navn som i "Hjemme" paa
 * skjermen.
 *
 * MERK: ansvaret lagres som en plass i denne lista. Endrer du rekkefolgen
 * eller antallet, flytter ansvaret seg til den som staar paa samme plass.
 */
static const char *BEBOERE[] = { "Elias", "Henrik", "Sigurd" };
static const uint8_t ANTALL_BEBOERE = sizeof(BEBOERE) / sizeof(BEBOERE[0]);

// ---- Maaling ----

// Hvor ofte maaleTask leverer en maaling.
static const uint32_t MAALEINTERVALL_MS = 2000;

/*
 * Hver maaling er median av fem ekko. Medianen og ikke snittet: ett ekko
 * som treffer kanten av posen gir et tall som er helt feil, og det drar
 * snittet med seg men flytter ikke medianen.
 */
static const uint8_t EKKO_PER_MAALING = 5;
static const uint8_t MIN_GYLDIGE_EKKO = 3;

// HC-SR04 trenger ca. 60 ms mellom ekko for det forrige har doedd ut.
static const uint32_t EKKO_PAUSE_MS = 60;

// 30 ms tilsvarer ca. 5 m. Kommer det ikke noe ekko innen da, kommer det ikke.
static const uint32_t EKKO_TIMEOUT_US = 30000;

/*
 * Gyldig avstand. Alt lenger enn bunnen pluss litt slakk er ikke innholdet
 * i botta - som regel er lokket aapent og sensoren peker i taket. Det skal
 * hverken tolkes som tom eller full, bare ignoreres.
 */
static const float MIN_AVSTAND_CM = 2.0;
static const float MAKS_AVSTAND_CM = AVSTAND_TOM_CM + 10.0;

/*
 * Hvor lenge siste gyldige maaling faar bli staaende for vi sier fra. Kort,
 * fordi det er her et aapent lokk eller en loes ledning viser seg.
 */
static const uint32_t MAKS_ALDER_MS = 30000;

// Hvor ofte statusen publiseres.
static const uint32_t PUBLISERINGSINTERVALL_MS = 5000;

// Hvor lenge vi venter mellom forsoek paa aa koble til brokeren.
static const uint32_t MQTT_FORSOK_MS = 5000;

// Hvor lenge vi venter paa Wi-Fi for vi river forbindelsen og prover paa nytt.
static const uint32_t WIFI_FORSOK_MS = 20000;

// Hvor lenge wifiMulti.run() venter paa at det valgte nettet svarer.
static const uint32_t WIFI_KOBLE_MS = 10000;

// ===================================================================
// DELT TILSTAND
// ===================================================================

/*
 * MERK: ingen funksjoner over typene under. Arduino-IDE-en lager
 * prototyper for alle funksjoner og setter dem rett foran den FORSTE
 * funksjonen i fila - staar det en funksjon her, havner prototypene foran
 * Status og Maaling, og kompileringen feiler med "does not name a type".
 */

enum Nivaa : uint8_t { TOM, DELVIS, NESTEN_FULL, FULL };

// Det maaleTask legger i koen.
struct Maaling {
  bool gyldig;
  float avstandCm;
};

/*
 * Alt nettTask trenger for aa publisere. tilstandTask skriver, nettTask leser,
 * og begge gjor det bare med statusMutex tatt.
 */
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

// ===================================================================
// LAGRING
// ===================================================================

/*
 * Ansvaret, antall tomminger og om botta er "klar for tomming" lagres.
 * Det siste maa med: gaar stroemmen mens botta er full og den tommes mens
 * ESP-en er av, skal tommingen likevel telle naar den kommer tilbake.
 *
 * Flash taaler rundt 100 000 skrivinger per celle. Vi skriver bare naar noe
 * av dette endrer seg - noen ganger i uka - saa det er ingen fare.
 */
static void lesLager() {
  lager.begin("soppel", true);
  status.ansvarlig = lager.getUChar("ansvarlig", 0);
  status.antallTomminger = lager.getUInt("tomminger", 0);
  status.klarForTomming = lager.getBool("klar", false);
  status.sisteTomming = (time_t) lager.getULong64("sist", 0);
  lager.end();

  // Lista kan ha blitt kortere siden sist.
  if (status.ansvarlig >= ANTALL_BEBOERE) status.ansvarlig = 0;

  Serial.printf("[soppel] Fra lager: %s har ansvaret, %lu tomminger totalt.\n",
                BEBOERE[status.ansvarlig], (unsigned long) status.antallTomminger);
}

// Tar en kopi, ikke selve status - skal kalles UTEN mutexen.
static void skrivLager(const Status &s) {
  lager.begin("soppel", false);
  lager.putUChar("ansvarlig", s.ansvarlig);
  lager.putUInt("tomminger", s.antallTomminger);
  lager.putBool("klar", s.klarForTomming);
  lager.putULong64("sist", (uint64_t) s.sisteTomming);
  lager.end();
}

// ===================================================================
// MAALING - maaleTask
// ===================================================================

/** Ett ekko. Returnerer avstanden i cm, eller NAN uten gyldig ekko. */
static float ettEkko() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  const unsigned long us = pulseIn(PIN_ECHO, HIGH, EKKO_TIMEOUT_US);
  if (us == 0) return NAN;

  // Lyd gaar 0,0343 cm/us, og ekkoet gaar frem og tilbake.
  const float cm = us * 0.0343f / 2.0f;

  if (cm < MIN_AVSTAND_CM || cm > MAKS_AVSTAND_CM) return NAN;
  return cm;
}

/** Median av EKKO_PER_MAALING ekko, eller ugyldig hvis for faa svarte. */
static Maaling maalMedian() {
  float ekko[EKKO_PER_MAALING];
  uint8_t antall = 0;

  for (uint8_t i = 0; i < EKKO_PER_MAALING; i++) {
    const float cm = ettEkko();
    if (!isnan(cm)) ekko[antall++] = cm;
    vTaskDelay(pdMS_TO_TICKS(EKKO_PAUSE_MS));
  }

  if (antall < MIN_GYLDIGE_EKKO) return { false, 0.0f };

  // Innstikksortering. Fem tall - ingenting aa hente paa noe smartere.
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

  // vTaskDelayUntil og ikke vTaskDelay: da blir intervallet 2 s fra start
  // til start, uansett hvor lang tid selve maalingen tok.
  TickType_t neste = xTaskGetTickCount();

  for (;;) {
    const Maaling m = maalMedian();

    /*
     * Koen har plass til en maaling, og xQueueOverwrite erstatter den som
     * ligger der. tilstandTask skal ha den nyeste, ikke en koe av gamle.
     */
    xQueueOverwrite(maaleKo, &m);

    vTaskDelayUntil(&neste, pdMS_TO_TICKS(MAALEINTERVALL_MS));
  }
}

// ===================================================================
// TILSTAND - tilstandTask
// ===================================================================

/** Fyllingsgrad i prosent, 0 - 100. */
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

/**
 * Nytt nivaa med hysterese.
 *
 * Nivaaet gaar opp bare naar tallet er HYSTERESE over grensen, og ned bare
 * naar det er HYSTERESE under. Imellom blir det staaende der det var.
 */
static Nivaa nyttNivaa(Nivaa naa, float p) {
  const Nivaa opp = raattNivaa(p - HYSTERESE);
  if (opp > naa) return opp;

  const Nivaa ned = raattNivaa(p + HYSTERESE);
  if (ned < naa) return ned;

  return naa;
}

/** Klokka er satt via NTP naar den er forbi 2020. */
static time_t naaUnix() {
  const time_t t = time(nullptr);
  return t > 1577836800 ? t : 0;
}

/**
 * Behandler en maaling. Kalles med statusMutex tatt.
 * @returns true naar noe som skal lagres har endret seg.
 */
static bool behandle(const Maaling &m, uint32_t naaMs) {
  if (!m.gyldig) {
    /*
     * En ugyldig maaling roerer ikke nivaaet eller tommingen. Den gamle
     * verdien blir staaende, og meldingen sier fra via alder_s naar den blir
     * for gammel. Aapner noen lokket, skal ikke det se ut som en tom botte.
     */
    strlcpy(status.feil, "ingen gyldig maaling - lokket aapent?", sizeof(status.feil));
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

  // ---- Er botta blitt full nok til at en tomming skal telle? ----

  if (status.nivaa >= NESTEN_FULL && !status.klarForTomming) {
    status.klarForTomming = true;
    return true;
  }

  // ---- Er den tommet? ----

  if (!status.klarForTomming || status.nivaa != TOM) {
    status.tomFraMs = 0;
    return false;
  }

  // 0 betyr "teller ikke", saa et tilfeldig millis() == 0 blir 1.
  if (status.tomFraMs == 0) {
    status.tomFraMs = naaMs ? naaMs : 1;
    Serial.println("[soppel] Botta ser tom ut. Venter paa at det holder seg.");
    return false;
  }

  if (naaMs - status.tomFraMs < TOM_STABIL_MS) return false;

  // Tommet. Ansvaret gaar videre.
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
    /*
     * Venter paa neste maaling, men ikke for alltid. Kommer det ingenting
     * paa tre intervaller, har maaleTask hengt seg - og da skal det staa i
     * meldingen, ikke bare vaere stille.
     */
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

    /*
     * Skrivingen til flash skjer etter at mutexen er gitt fra seg. Den tar
     * noen millisekunder, og saa lenge skal ikke nettTask staa og vente paa
     * aa faa publisere.
     */
    if (lagre) skrivLager(kopi);
  }
}

// ===================================================================
// NETT - nettTask
// ===================================================================

/** Henter en kopi av status. Mutexen holdes bare mens vi kopierer. */
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

/** Publiserer statusen. */
static void publiser() {
  const Status s = lesStatus();
  const char *ansvarlig = BEBOERE[s.ansvarlig];
  const char *neste = BEBOERE[(s.ansvarlig + 1) % ANTALL_BEBOERE];

  char json[384];

  /*
   * Uten fersk maaling sendes ok:false, men ansvaret er kjent uansett - det
   * ligger i flash og ikke i sensoren - saa det sendes med.
   */
  if (!s.harMaaling || (millis() - s.sisteMaalingMs) > MAKS_ALDER_MS) {
    snprintf(json, sizeof(json),
             "{\"ok\":false,\"feil\":\"%s\",\"ansvarlig\":\"%s\",\"neste\":\"%s\"}",
             s.feil[0] ? s.feil : "ingen maaling ennaa", ansvarlig, neste);
    mqtt.publish(MQTT_EMNE, json);
    return;
  }

  /*
   * Fyllingsgraden sendes som heltall. Sensoren maaler paa en halv
   * centimeter naer, og det er over en prosent av botta.
   */
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

/** Kobler til brokeren. Feiler det, proeves det igjen etter MQTT_FORSOK_MS. */
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
  // Hostname maa settes etter mode() og for tilkobling. Se inneklima.ino.
  WiFi.setHostname(VERTSNAVN);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);

  for (size_t i = 0; i < ANTALL_NETT; i++) {
    wifiMulti.addAP(WIFI_NETT[i][0], WIFI_NETT[i][1]);
  }
}

/**
 * Skanner og kobler til det sterkeste av de kjente nettene.
 *
 * wifiMulti.run() blokkerer i noen sekunder (skann + inntil WIFI_KOBLE_MS).
 * Det er greit her: uten nett har nettTask ingenting aa gjore, og de andre
 * taskene gaar videre som foer.
 */
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
  // Meldingen er lengre enn PubSubClients standardbuffer paa 256 bytes.
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

      // Klokka brukes bare til tidspunktet for siste tomming.
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
      // loop() holder forbindelsen i live med ping mot brokeren.
      mqtt.loop();

      if (naa - sistPublisert >= PUBLISERINGSINTERVALL_MS) {
        sistPublisert = naa;
        publiser();
      }
    }

    /*
     * Nettet under setAutoReconnect(): den prover bare nettet vi var paa.
     * Gir den opp, skanner vi paa nytt blant ALLE kjente nett - saa ESP-en
     * finner hotspoten naar hjemmenettet er borte, og omvendt.
     */
    if (!tilkoblet && (naa - forsoktFra) > WIFI_FORSOK_MS) {
      WiFi.disconnect();
      kobleTil();
      forsoktFra = millis();
    }

    /*
     * Gir fra seg CPU-en. Uten denne spinner tasken paa 100 % og sulter
     * alt med samme eller lavere prioritet - blant annet idle-tasken, og da
     * slaar watchdogen til.
     */
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ===================================================================
// OPPSTART
// ===================================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("[soppel] Starter.");

  statusMutex = xSemaphoreCreateMutex();
  maaleKo = xQueueCreate(1, sizeof(Maaling));

  lesLager();

  /*
   * Alle tre paa kjerne 1. Kjerne 0 er Wi-Fi-stakkens, og vi lar den vaere
   * i fred. Stakkstoerrelsene er i bytes (ESP-IDF-varianten av FreeRTOS).
   */
  xTaskCreatePinnedToCore(maaleTask, "maale", 3072, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(tilstandTask, "tilstand", 4096, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(nettTask, "nett", 8192, nullptr, 1, nullptr, 1);
}

/*
 * loop() er selv en FreeRTOS-task i Arduino-kjernen. Den har ingenting aa
 * gjore her - alt arbeid er i taskene over - saa den sletter seg selv.
 */
void loop() {
  vTaskDelete(nullptr);
}
