# KollektivMonitor – demoversjonen

Skjerm for kollektivet (TTK8). En Raspberry Pi viser:

- **Søppel:** hvor full bøtta er og hvem som skal tømme den (`esp/soppel/`).
- **Inne:** temperatur og luftfuktighet (`esp/inneklima/`).
- **Hjemme:** hvem som er hjemme, ut fra telefonenes MAC-adresse (`hjemme.sh`).

ESP-ene sender JSON over MQTT til Mosquitto på Pi-en. `server.js` tar imot og
gir skjermen (`index.html`, `app.js`, `style.css`) dataene på `/api/status`.

Dette er versjonen for demo på skolen. Raspberry Pi 5 er selv Wi-Fi-tilgangspunkt
(`PresenceLab`, 10.42.0.1), og Mosquitto krever innlogging. Hjemmeversjonen ligger
på `main`. Koden ellers er den samme.

## Raspberry Pi 5

    sudo apt install nodejs npm
    git clone -b skole-demo https://github.com/Melkeraaen/kollektivmonitor-display.git
    cd kollektivmonitor-display
    npm install
    cp config.example.json config.json

Ikke installer `arp-scan` og ikke kopier `mosquitto.conf`. Hotspoten og Mosquitto
finnes fra før.

Lag en MQTT-bruker (uten `-c`, ellers slettes de andre brukerne):

    sudo mosquitto_passwd /etc/mosquitto/presence-passwd kollektivmonitor
    sudo systemctl reload mosquitto

I `config.json`: sett `"mqttUrl": "mqtt://kollektivmonitor:<passord>@localhost:1883"`,
og fyll inn navn og MAC-adresse for hver telefon. Navnene må være like som i
`BEBOERE` i `esp/soppel/soppel.ino`. Koble telefonene til `PresenceLab` og les av
MAC-adressene:

    iw dev wlan0 station dump | grep ^Station

## ESP-ene

1. Installer bibliotekene PubSubClient og Adafruit AHTX0 i Arduino IDE.
2. Kopier `wifi_secrets.example.h` til `wifi_secrets.h` i begge mappene under `esp/`.
   Fyll inn passordet til hotspoten og MQTT-brukeren.
3. Last opp `esp/soppel/soppel.ino` på board *Adafruit ESP32 Feather*.
4. Last opp `esp/inneklima/inneklima.ino` på board *UM FeatherS3*.

## Starte

Fra en terminal på Pi-ens skrivebord (ikke over SSH):

    ./start.sh

Skjermen lukkes med `pkill chromium`.

## Tester

    ./tester/kjor_alle.sh

Tester ESP-koden, `app.js` og `server.js` uten maskinvare. Se `tester/README.md`.
