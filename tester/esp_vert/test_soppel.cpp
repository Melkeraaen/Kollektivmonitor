// Vertstest av esp/soppel/soppel.ino. Fila inkluderes uendret, og Arduino,
// FreeRTOS, Wi-Fi, MQTT og flash erstattes av stubs/. Testen kaller funksjonene
// i tilstandTask og maaleTask direkte og styrer tiden selv.
#include "arduino_stub.h"
#include "WiFi.h"

uint32_t testMillis = 1000;
std::vector<unsigned long> testEkko;
SerialStub Serial;
WiFiStub WiFi;
std::string testSistEmne, testSistMelding;
std::map<std::string, uint64_t> testFlash;

#include "../../esp/soppel/soppel.ino"

static int antall = 0, antallOk = 0;
static void sjekk(const char *id, const char *hva, bool ok, const char *detalj) {
  antall++;
  if (ok) antallOk++;
  printf("%s %s %s: %s\n", ok ? "OK  " : "FEIL", id, hva, detalj);
}

// Avstand -> ekkotid i mikrosekunder, motsatt av ettEkko()
static unsigned long us(float cm) { return (unsigned long) lroundf(cm * 2.0f / 0.0343f); }

// Avstand som gir en gitt fyllingsgrad
static float cmFor(float prosent) { return AVSTAND_TOM_CM - prosent / 100.0f * (AVSTAND_TOM_CM - AVSTAND_FULL_CM); }

// Ett steg i tilstandTask: gyldig maaling med gitt fyllingsgrad ved gitt tid
static bool steg(float prosent, uint32_t t) {
  testMillis = t;
  const bool lagre = behandle({ true, cmFor(prosent) }, t);
  if (lagre) skrivLager(status);
  return lagre;
}

static void nullstill() {
  status = {};
  testFlash.clear();
}

int main() {
  char d[256];

  // E1 Fyllingsgrad fra avstand
  {
    const float f45 = fyllProsent(45), f8 = fyllProsent(8), f265 = fyllProsent(26.5), f50 = fyllProsent(50), f5 = fyllProsent(5);
    snprintf(d, sizeof d, "45 cm=%.1f %%, 26,5 cm=%.1f %%, 8 cm=%.1f %%, 50 cm=%.1f %%, 5 cm=%.1f %%", f45, f265, f8, f50, f5);
    sjekk("E1", "Fyllingsgrad fra avstand", f45 == 0 && fabsf(f265 - 50) < 0.01f && f8 == 100 && f50 == 0 && f5 == 100, d);
  }

  // E2 Nivaa med hysterese, opp og ned med 0,5 prosentpoengs steg
  {
    const char *navn[] = { "TOM", "DELVIS", "NESTEN_FULL", "FULL" };
    float opp[4] = { -1, -1, -1, -1 }, ned[4] = { -1, -1, -1, -1 };
    Nivaa n = TOM;
    for (float p = 0; p <= 100.01f; p += 0.5f) { Nivaa m = nyttNivaa(n, p); if (m != n) opp[m] = p; n = m; }
    for (float p = 100; p >= -0.01f; p -= 0.5f) { Nivaa m = nyttNivaa(n, p); if (m != n) ned[m] = p; n = m; }
    snprintf(d, sizeof d, "opp: DELVIS ved %.1f, NESTEN_FULL ved %.1f, FULL ved %.1f; ned: NESTEN_FULL ved %.1f, DELVIS ved %.1f, TOM ved %.1f",
             opp[DELVIS], opp[NESTEN_FULL], opp[FULL], ned[NESTEN_FULL], ned[DELVIS], ned[TOM]);
    const bool ok = opp[DELVIS] == 18.5f && opp[NESTEN_FULL] == 78.0f && opp[FULL] == 93.0f &&
                    ned[NESTEN_FULL] == 86.5f && ned[DELVIS] == 71.5f && ned[TOM] == 12.0f;
    sjekk("E2", "Hysterese mellom nivaaene", ok, d);
    (void) navn;
  }

  // E3 Svingning rundt 75 % gir ikke blafring
  {
    Nivaa n = DELVIS; int bytter = 0;
    for (int i = 0; i < 100; i++) { Nivaa m = nyttNivaa(n, (i % 2) ? 73.5f : 76.5f); if (m != n) bytter++; n = m; }
    snprintf(d, sizeof d, "100 maalinger vekselvis 73,5 og 76,5 %%: %d nivaabytter", bytter);
    sjekk("E3", "Ingen blafring rundt en grense", bytter == 0, d);
  }

  // E4 Normal tomming: full, saa tom i 20 s. Ansvaret gaar videre en gang.
  {
    nullstill(); lesLager();
    uint32_t t = 1000;
    for (int i = 0; i < 5; i++) steg(20 + i * 18, t += 2000);  // 20 -> 92 %
    const bool klar = status.klarForTomming;
    uint8_t foer = status.ansvarlig;
    uint32_t tomStart = t + 2000; uint32_t bytteVed = 0;
    for (int i = 0; i < 40; i++) { steg(3, t += 2000); if (!bytteVed && status.ansvarlig != foer) bytteVed = t; }
    snprintf(d, sizeof d, "klar etter fylling=%d, ansvar %s -> %s, byttet %.0f s etter foerste tomme maaling, tomminger=%u",
             klar, BEBOERE[foer], BEBOERE[status.ansvarlig], bytteVed ? (bytteVed - tomStart) / 1000.0 : -1.0, (unsigned) status.antallTomminger);
    sjekk("E4", "Tomming etter 20 s stabilt tom", klar && status.ansvarlig == 1 && status.antallTomminger == 1 &&
          bytteVed - tomStart >= TOM_STABIL_MS && bytteVed - tomStart <= TOM_STABIL_MS + 2000, d);
  }

  // E5 Kort tom periode (haand i aapningen, pose loeftet ut i 10 s) teller ikke
  {
    nullstill(); lesLager();
    uint32_t t = 1000;
    steg(50, t += 2000); steg(80, t += 2000); steg(95, t += 2000);
    for (int i = 0; i < 5; i++) steg(5, t += 2000);    // 10 s tom
    for (int i = 0; i < 5; i++) steg(60, t += 2000);   // tilbake
    snprintf(d, sizeof d, "ansvar=%s, tomminger=%u, klar=%d", BEBOERE[status.ansvarlig], (unsigned) status.antallTomminger, status.klarForTomming);
    sjekk("E5", "10 s tom bøtte teller ikke som tømming", status.ansvarlig == 0 && status.antallTomminger == 0 && status.klarForTomming, d);
  }

  // E6 Tom bøtte uten at den har vært nesten full teller ikke
  {
    nullstill(); lesLager();
    uint32_t t = 1000;
    for (int i = 0; i < 10; i++) steg(50, t += 2000);
    for (int i = 0; i < 60; i++) steg(5, t += 2000);   // 120 s tom
    snprintf(d, sizeof d, "maks 50 %%, saa 120 s tom: ansvar=%s, tomminger=%u", BEBOERE[status.ansvarlig], (unsigned) status.antallTomminger);
    sjekk("E6", "Tomming krever at bøtta har vært nesten full", status.ansvarlig == 0 && status.antallTomminger == 0, d);
  }

  // E7 Bøtta står tom lenge etter tomming: ansvaret byttes bare én gang
  {
    nullstill(); lesLager();
    uint32_t t = 1000;
    steg(95, t += 2000);
    for (int i = 0; i < 300; i++) steg(2, t += 2000);  // 10 min tom
    snprintf(d, sizeof d, "10 min tom etter full: tomminger=%u, ansvar=%s", (unsigned) status.antallTomminger, BEBOERE[status.ansvarlig]);
    sjekk("E7", "Ett bytte per tømming", status.antallTomminger == 1 && status.ansvarlig == 1, d);
  }

  // E8 Ugyldig maaling (lokket aapent) endrer verken verdier eller ansvar
  {
    nullstill(); lesLager();
    uint32_t t = 1000;
    steg(95, t += 2000);
    const float fyllFoer = status.fyllProsent;
    for (int i = 0; i < 60; i++) { testMillis = t += 2000; behandle({ false, 0 }, t); }
    snprintf(d, sizeof d, "120 s uten gyldig ekko: fyll %.0f -> %.0f %%, ansvar=%s, feil=\"%s\"", fyllFoer, status.fyllProsent,
             BEBOERE[status.ansvarlig], status.feil);
    sjekk("E8", "Lokket åpent gir feil, ikke tømming", status.fyllProsent == fyllFoer && status.ansvarlig == 0 && status.feil[0], d);
  }

  // E9 Omstart: ansvar og klarForTomming hentes fra flash
  {
    nullstill(); lesLager();
    uint32_t t = 1000;
    steg(95, t += 2000);
    for (int i = 0; i < 35; i++) steg(2, t += 2000);   // tomming, Henrik har ansvaret
    steg(85, t += 2000);                                 // nesten full igjen, klar lagres
    const uint8_t a = status.ansvarlig; const bool k = status.klarForTomming;
    status = {};                                         // "strømbrudd"
    lesLager();
    snprintf(d, sizeof d, "foer: %s, klar=%d. etter omstart: %s, klar=%d, tomminger=%u", BEBOERE[a], k,
             BEBOERE[status.ansvarlig], status.klarForTomming, (unsigned) status.antallTomminger);
    sjekk("E9", "Ansvar overlever omstart (flash)", status.ansvarlig == a && status.klarForTomming == k && status.antallTomminger == 1, d);
  }

  // E10 Ugyldig plass i flash (lista er gjort kortere) gir første beboer
  {
    nullstill(); testFlash["soppel/ansvarlig"] = 7; lesLager();
    snprintf(d, sizeof d, "lagret plass 7 med %u beboere: ansvar=%s", (unsigned) ANTALL_BEBOERE, BEBOERE[status.ansvarlig]);
    sjekk("E10", "Ugyldig lagret ansvar", status.ansvarlig == 0, d);
  }

  // E11 Median av fem ekko tåler én og to feil
  {
    testEkko = { us(20.0), us(20.4), us(39.0), us(19.8), us(20.1) };   // ett ekko fra kanten av posen
    Maaling m1 = maalMedian();
    testEkko = { us(20.0), 0, us(20.4), us(70.0), us(19.9) };                 // timeout og et utenfor bøtta
    Maaling m2 = maalMedian();
    testEkko = { us(20.0), 0, 0, us(80.0), us(19.9) };                         // bare to gyldige
    Maaling m3 = maalMedian();
    snprintf(d, sizeof d, "1 avvik: %.1f cm; timeout + 70 cm: %.1f cm (gyldig=%d); bare 2 gyldige: gyldig=%d",
             m1.avstandCm, m2.avstandCm, m2.gyldig, m3.gyldig);
    sjekk("E11", "Median og forkasting av ekko", m1.gyldig && fabsf(m1.avstandCm - 20.1f) < 0.2f && m2.gyldig &&
          fabsf(m2.avstandCm - 20.0f) < 0.2f && !m3.gyldig, d);
  }

  // E12 Meldingsformat som server.js forventer
  {
    nullstill(); lesLager();
    testMillis = 5000; publiser();
    const std::string utenMaaling = testSistMelding;
    steg(80, 7000); testMillis = 8000; publiser();
    const std::string medMaaling = testSistMelding;
    testMillis = 8000 + MAKS_ALDER_MS + 1000; publiser();
    const std::string gammel = testSistMelding;
    printf("      uten maaling: %s\n      med maaling:  %s\n      gammel:       %s\n", utenMaaling.c_str(), medMaaling.c_str(), gammel.c_str());
    const bool ok = utenMaaling.find("\"ok\":false") != std::string::npos && utenMaaling.find("\"ansvarlig\":\"Elias\"") != std::string::npos &&
                    medMaaling.find("\"ok\":true,\"fyll\":80,\"nivaa\":\"nesten_full\"") != std::string::npos &&
                    gammel.find("\"ok\":false") != std::string::npos && medMaaling.size() < 512 && testSistEmne == "kollektivmonitor/soppel";
    snprintf(d, sizeof d, "emne %s, lengde %zu byte (buffer 512)", testSistEmne.c_str(), medMaaling.size());
    sjekk("E12", "Meldingsformat", ok, d);
  }

  printf("\n%d av %d tester OK\n", antallOk, antall);
  return antallOk == antall ? 0 : 1;
}
