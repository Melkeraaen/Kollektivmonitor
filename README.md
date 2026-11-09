# KollektivMonitor – hjemmeversjonen

Versjonen for bruk i kollektivet. ESP-ene, telefonene og Raspberry Pi-en er på det
vanlige hjemmenettet, og `hjemme.sh` skanner hjemmenettet for å se hvem som er hjemme.
Versjonen med Pi 5 som eget Wi-Fi-tilgangspunkt (til demo på skolen) ligger i
branchen `skole-demo`. Koden ellers er den samme.

En Raspberry Pi viser tre ting:

- **Søppel:** fyllingsgrad og hvem som har tømmeansvaret, fra ESP-en i `esp/soppel/`.
  Er den ansvarlige borte og bøtta nesten full eller full, blir navnet grått og noen
  som er hjemme foreslås.
- **Inne:** temperatur og luftfuktighet, fra ESP-en i `esp/inneklima/`.
- **Hjemme:** hvem som er på hjemmenettet, ut fra MAC-adressen til telefonene (`hjemme.sh`).

ESP-ene publiserer status som JSON over MQTT (`kollektivmonitor/soppel` og
`kollektivmonitor/inneklima`) til Mosquitto, og `hjemme.sh` publiserer MAC-adressene
på nettet (`kollektivmonitor/hjemme`). `server.js` abonnerer, kontrollerer
meldingene og leverer dem på `/api/status`. `index.html`, `style.css` og `app.js`
henter dette med jevne mellomrom. Mangler en kilde, står det «–».

## Installere

Krever Node 18 eller nyere og Mosquitto.

    brew install mosquitto                                   # Mac
    sudo apt install nodejs npm mosquitto mosquitto-clients chromium arp-scan  # Pi
    sudo cp mosquitto.conf /etc/mosquitto/conf.d/kollektivmonitor.conf   # Pi
    sudo systemctl restart mosquitto                         # Pi
    npm install

`mosquitto.conf` lar brokeren lytte på hele hjemmenettet uten passord, så ESP-ene når
den. Bruk den bare på et nett du stoler på.

`arp-scan` gir sikrest «hjemme», og må kunne kjøres uten passord
(Mac: `/opt/homebrew/bin/arp-scan`):

    echo "$USER ALL=(root) NOPASSWD: /usr/sbin/arp-scan" | sudo tee /etc/sudoers.d/arp-scan

Uten den leses ARP-cachen (`ip neigh` på Pi, `arp -a` på Mac). Da kan en telefon i
dvale bli vist som borte.

## Konfigurere

    cp config.example.json config.json

Fyll inn navn og MAC-adresse for hver person. Bruk telefonens faste Wi-Fi-adresse
(iPhone: *Privat Wi-Fi-adresse* = **Fast** for hjemmenettet). Navnene må være skrevet
likt som `BEBOERE` i `esp/soppel/soppel.ino`. `mqttUrl` er `mqtt://localhost:1883`.

ESP-ene lastes opp med Arduino IDE og trenger biblioteket PubSubClient (inneklima også
Adafruit AHTX0). I `wifi_secrets.h` i begge ESP-mappene må `MQTT_BROKER` være
IP-adressen til Pi-en på hjemmenettet (`hostname -I` på Pi-en). Reserver adressen i
ruteren, så den ikke flytter seg.

## Starte

    ./start.sh    # starter Mosquitto (hvis den ikke kjører), backend, hjemme.sh og skjerm

Se meldingene fra ESP-ene: `mosquitto_sub -h localhost -t 'kollektivmonitor/#' -v`

## Tester

    ./tester/kjor_alle.sh

Se `tester/README.md`.
