# Tester uten maskinvare

    ./tester/kjor_alle.sh

Krever g++, Node 18+, `npm install` og `mosquitto` i PATH.

- `esp_vert/test_soppel.cpp` og `test_inneklima.cpp` kompilerer `.ino`-filene uendret på PC.
  Arduino, FreeRTOS, Wi-Fi, MQTT og flash er byttet ut med det som ligger i `esp_vert/stubs/`.
  Testen styrer tiden (`millis()`) og ekkoene (`pulseIn()`) selv.
- `test_vikar.js` tester `finnVikar()` i `app.js`.
- `test_pi.js` starter en egen Mosquitto på port 18830 og `server.js` med korte tidsgrenser,
  sender meldinger som ESP-ene og `hjemme.sh` ville gjort, og sjekker `/api/status`.

Testene sier ingenting om timing på ESP32, sensorene eller Wi-Fi. Det må testes på maskinvaren.
