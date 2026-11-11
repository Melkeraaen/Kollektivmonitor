#!/bin/sh
# Finner MAC-adressene på nettet og sender dem over MQTT til server.js.
cd "$(dirname "$0")"
# iw ligger i /usr/sbin, som ikke alltid er med i PATH når skrivebordet starter skriptet.
PATH="$PATH:/usr/sbin"
INTERVALL=$(node -p "require('./config.json').skannSek")
# Adresse, bruker og passord til brokeren, samme som server.js bruker.
MQTT=$(node -p "require('./config.json').mqttUrl")

while true; do
	if iw dev wlan0 info 2>/dev/null | grep -q 'type AP'; then
		# Pi-en er selv hotspot, og radioen vet hvem som er tilkoblet. Ingen spørres,
		# så en sovende telefon telles med, og ingen andre nett blir skannet.
		TABELL=$(iw dev wlan0 station dump | grep '^Station')
	elif TABELL=$(sudo -n arp-scan -q -l 2>/dev/null); then
		# arp-scan spør hver adresse på nettet, men krever passordfri sudo. Brukes på Macen.
		:
	elif command -v ip >/dev/null; then
		# Bare REACHABLE er bekreftet nå. STALE blir liggende lenge etter at telefonen har gått.
		TABELL=$(ip neigh show | grep REACHABLE)
	else
		TABELL=$(arp -an)
	fi

	echo "$TABELL" | grep -oiE '([0-9a-f]{1,2}:){5}[0-9a-f]{1,2}' | mosquitto_pub -L "$MQTT/kollektivmonitor/hjemme" -s
	sleep "$INTERVALL"
done
