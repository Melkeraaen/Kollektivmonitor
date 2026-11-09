// Vertstest av esp/inneklima/inneklima.ino, samme oppsett som test_soppel.cpp.
#include "arduino_stub.h"
#include "WiFi.h"
#include "Wire.h"

uint32_t testMillis = 1000;
std::vector<unsigned long> testEkko;
SerialStub Serial;
WiFiStub WiFi;
WireStub Wire;
std::string testSistEmne, testSistMelding;
bool testAhtFinnes = true, testAhtSvarer = true;
float testTemp = 21.43f, testFukt = 38.26f;

#include "../../esp/inneklima/inneklima.ino"

static int antall = 0, antallOk = 0;
static void sjekk(const char *id, const char *hva, bool ok, const std::string &detalj) {
  antall++;
  if (ok) antallOk++;
  printf("%s %s %s: %s\n", ok ? "OK  " : "FEIL", id, hva, detalj.c_str());
}
static bool har(const std::string &s, const char *del) { return s.find(del) != std::string::npos; }

int main() {
  maalingMutex = xSemaphoreCreateMutex();

  // A1 Ingen sensor ved oppstart
  testAhtFinnes = false;
  sensorKlar = startSensor();
  publiser();
  sjekk("A1", "Ingen AHT20 ved oppstart", !sensorKlar && har(testSistMelding, "{\"ok\":false,\"feil\":\"fant ingen sensor\"}"), testSistMelding);

  // A2 Sensoren kobles til, normal måling
  testAhtFinnes = true;
  sensorKlar = startSensor();
  testMillis = 11000; lesSensor();
  testMillis = 12000; publiser();
  sjekk("A2", "Normal måling", sensorKlar && har(testSistMelding, "\"ok\":true,\"temperatur\":21.4,\"fuktighet\":38.3,\"alder_s\":1") &&
        testSistEmne == "kollektivmonitor/inneklima", testSistMelding);

  // A3 NaN fra sensoren: forrige måling beholdes og blir eldre
  testTemp = NAN;
  testMillis = 21000; lesSensor();
  testMillis = 22000; publiser();
  const Maaling m = lesMaaling();
  sjekk("A3", "NaN fra sensoren", har(testSistMelding, "\"temperatur\":21.4") && har(testSistMelding, "\"alder_s\":11") &&
        std::string(m.feil) == "avlesningen var NaN", testSistMelding + "  feil=\"" + m.feil + "\"");

  // A4 Sensoren svarer ikke: startes på nytt neste runde
  testTemp = 22.0f; testAhtSvarer = false;
  testMillis = 31000; lesSensor();
  const bool falt = !sensorKlar;
  testAhtSvarer = true;
  if (!sensorKlar) sensorKlar = startSensor();
  testMillis = 41000; lesSensor();
  testMillis = 41500; publiser();
  sjekk("A4", "Sensoren faller ut og kommer tilbake", falt && sensorKlar && har(testSistMelding, "\"temperatur\":22.0,") &&
        har(testSistMelding, "\"alder_s\":0"), testSistMelding);

  // A5 Sensoren er borte i over 30 s: ESP-en melder feilen i stedet for gamle verdier
  testAhtSvarer = false;
  testMillis = 51000; lesSensor();
  testMillis = 61000; if (!sensorKlar) sensorKlar = startSensor(); publiser();
  const std::string etter20s = testSistMelding;
  testMillis = 72000; if (!sensorKlar) sensorKlar = startSensor(); publiser();
  sjekk("A5", "Sensoren borte i over 30 s gir ok:false", har(etter20s, "\"ok\":true") &&
        har(testSistMelding, "{\"ok\":false,\"feil\":\"") && !har(testSistMelding, "temperatur"),
        "etter 20 s: " + etter20s + "  etter 31 s: " + testSistMelding);
  testAhtSvarer = true;

  printf("\n%d av %d tester OK\n", antallOk, antall);
  return antallOk == antall ? 0 : 1;
}
