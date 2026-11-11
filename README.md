# KollektivMonitor

Skjerm for kollektivet (TTK8). En Raspberry Pi viser:

- **Søppel:** hvor full bøtta er og hvem som skal tømme den (`esp/soppel/`).
- **Inne:** temperatur og luftfuktighet (`esp/inneklima/`).
- **Hjemme:** hvem som er hjemme, ut fra telefonenes MAC-adresse (`hjemme.sh`).

ESP-ene sender JSON over MQTT til Mosquitto på Pi-en. `server.js` tar imot og
gir skjermen (`index.html`, `app.js`, `style.css`) dataene på `/api/status`.

Dette er hjemmeversjonen. Versjonen der Pi-en er eget Wi-Fi-tilgangspunkt (demo på
skolen) ligger i branchen `skole-demo`.

## Raspberry Pi

    git clone https://github.com/Melkeraaen/kollektivmonitor-display.git
    cd kollektivmonitor-display
    sudo apt install nodejs npm mosquitto mosquitto-clients chromium arp-scan
    sudo cp mosquitto.conf /etc/mosquitto/conf.d/kollektivmonitor.conf
    sudo systemctl restart mosquitto
    echo "$USER ALL=(root) NOPASSWD: /usr/sbin/arp-scan" | sudo tee /etc/sudoers.d/arp-scan
    npm install
    cp config.example.json config.json

Fyll inn navn og MAC-adresse for hver telefon i `config.json`. Navnene må være like
som i `BEBOERE` i `esp/soppel/soppel.ino`. På iPhone: sett *Privat Wi-Fi-adresse* til
**Fast**.

## ESP-ene

1. Installer bibliotekene PubSubClient og Adafruit AHTX0 i Arduino IDE.
2. Kopier `wifi_secrets.example.h` til `wifi_secrets.h` i begge mappene under `esp/`.
   Fyll inn Wi-Fi-nett, passord og IP-adressen til Pi-en (`hostname -I`).
3. Last opp `esp/soppel/soppel.ino` på board *Adafruit ESP32 Feather*.
4. Last opp `esp/inneklima/inneklima.ino` på board *UM FeatherS3*.

## Starte

Fra en terminal på Pi-ens skrivebord:

    ./start.sh

Skjermen lukkes med `pkill chromium`.
