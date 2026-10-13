# KollektivMonitor – skjerm

Skjermdelen av KollektivMonitor (TTK8). En Raspberry Pi viser tre ting:

- **Søppel:** fyllingsgrad og hvem som har tømmeansvaret, fra ESP-en i `esp/soppel/`.
  Er den ansvarlige borte, blir navnet grått og noen som er hjemme foreslås.
- **Inne:** temperatur og luftfuktighet, fra ESP-en i `esp/inneklima/`.
- **Hjemme:** hvem som er på nettverket, ut fra MAC-adressen til telefonene (`hjemme.sh`).

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
    npm install

`arp-scan` gir sikrest «hjemme», og må kunne kjøres uten passord (Mac: `/opt/homebrew/bin/arp-scan`):

    echo "$USER ALL=(root) NOPASSWD: /usr/sbin/arp-scan" | sudo tee /etc/sudoers.d/arp-scan

Uten den leses ARP-cachen (`ip neigh` på Pi, `arp -a` på Mac).

## Konfigurere

    cp config.example.json config.json

Fyll inn navn og MAC-adresse for hver person. Bruk telefonens faste Wi-Fi-adresse.

ESP-ene lastes opp med Arduino IDE og trenger biblioteket PubSubClient. Kopier
`wifi_secrets.example.h` til `wifi_secrets.h` i samme mappe, og fyll inn nettene og
IP-adressen til maskinen som kjører Mosquitto.

## Starte

    ./start.sh    # starter Mosquitto (hvis den ikke kjører), backend, hjemme.sh og skjerm

Se meldingene fra ESP-ene: `mosquitto_sub -h localhost -t 'kollektivmonitor/#' -v`
