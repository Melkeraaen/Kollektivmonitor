# KollektivMonitor – demoversjonen

Versjonen til demo på skolen. Raspberry Pi 5 er selv Wi-Fi-tilgangspunkt
(`PresenceLab`, 10.42.0.1), ESP-ene og telefonene kobler seg til det, og
`hjemme.sh` leser hvilke telefoner som er tilkoblet tilgangspunktet. Mosquitto krever
innlogging. Versjonen som skanner hjemmenettet ligger på `main`.
Koden ellers er den samme.

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

Krever Node 18 eller nyere og Mosquitto. For Pi-en, se «Kjøre på Raspberry Pi 5» under.

    brew install mosquitto                                   # Mac
    npm install

På Macen gir `arp-scan` sikrest «hjemme», og må kunne kjøres uten passord:

    echo "$USER ALL=(root) NOPASSWD: /opt/homebrew/bin/arp-scan" | sudo tee /etc/sudoers.d/arp-scan

Uten den leses ARP-cachen (`arp -a`).

## Konfigurere

    cp config.example.json config.json

Fyll inn navn og MAC-adresse for hver person. Bruk telefonens faste Wi-Fi-adresse.

ESP-ene lastes opp med Arduino IDE og trenger biblioteket PubSubClient. Kopier
`wifi_secrets.example.h` til `wifi_secrets.h` i samme mappe, og fyll inn nettet,
IP-adressen til maskinen som kjører Mosquitto og MQTT-brukeren.

## Starte

    ./start.sh    # starter Mosquitto (hvis den ikke kjører), backend, hjemme.sh og skjerm

Se meldingene fra ESP-ene: `mosquitto_sub -h localhost -t 'kollektivmonitor/#' -v`

## Kjøre på Raspberry Pi 5

Pi-en er allerede hotspot for et annet prosjekt (`PresenceLab`, 2,4 GHz, `10.42.0.1`),
og Mosquitto kjører som tjeneste med passord. KollektivMonitor bruker begge, med egen
MQTT-bruker og egne emner, og endrer ikke oppsettet.

**1. Pakker og kode.** `arp-scan` skal ikke installeres: den ville skannet `eth0`,
som er NTNU-nettet. `hjemme.sh` spør hotspoten i stedet.

    sudo apt update && sudo apt install -y nodejs npm
    cd ~ && git clone https://github.com/Melkeraaen/kollektivmonitor-display.git
    cd kollektivmonitor-display && npm install

**2. MQTT-bruker.** Ikke kopier inn `mosquitto.conf`, den kolliderer med lytterne som
finnes. Legg brukeren til i passordfila uten `-c` (den lager fila på nytt og sletter de
andre brukerne). Bruk bare bokstaver og tall i passordet, siden det står i en URL.

    sudo mosquitto_passwd /etc/mosquitto/presence-passwd kollektivmonitor
    sudo systemctl reload mosquitto
    mosquitto_sub -u kollektivmonitor -P <passord> -t 'kollektivmonitor/#' -W 3   # «Timed out» = innlogget

**3. Hotspot.** Finnes allerede. Sjekk den, og hent Wi-Fi-passordet til `wifi_secrets.h`
i begge ESP-mappene:

    iw dev wlan0 info | grep -E 'ssid|type'        # ssid PresenceLab, type AP
    nmcli -s -g 802-11-wireless-security.psk connection show PresenceLab

**4. `config.json`.**

    cp config.example.json config.json

Sett `"mqttUrl": "mqtt://kollektivmonitor:<passord>@localhost:1883"`. Koble telefonene
til `PresenceLab` (iPhone: *Privat Wi-Fi-adresse* = **Fast**) og les av MAC-adressene:

    iw dev wlan0 station dump | grep ^Station

Navnene må være skrevet likt som `BEBOERE` i `esp/soppel/soppel.ino`.

**5. Starte.** Systemet startes for hånd, ikke ved oppstart, siden Pi-en også brukes
til andre ting. Kjør det fra en terminal på Pi-ens eget skrivebord, ikke over SSH, for
der finner ikke Chromium skjermen:

    cd ~/kollektivmonitor-display && ./start.sh

Skjermen viser «Venter på …» til ESP-ene sender. Alt+F4 lukker ikke fullskjermen.
Bruk `pkill chromium` over SSH, eller fra tekstkonsollen (Ctrl+Alt+F2, logg inn, og
Ctrl+Alt+F7 tilbake). Da stopper også backend og `hjemme.sh`.

**6. Sjekk.** Mens systemet kjører, over SSH:

    curl -s localhost:8080/api/status
    mosquitto_sub -u kollektivmonitor -P <passord> -t 'kollektivmonitor/#' -v
