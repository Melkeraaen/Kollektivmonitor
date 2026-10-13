#!/bin/sh
# Finner MAC-adressene som er på nettet, og publiserer dem over MQTT på
# kollektivmonitor/hjemme, én per linje. server.js matcher dem mot config.json.
#
# Eget skript og ikke inni server.js: macOS skjuler ARP-tabellen for
# prosesser som Node starter, men ikke for prosesser startet fra skallet.
cd "$(dirname "$0")"
INTERVALL=$(node -p "require('./config.json').skannSek")

while true; do
	# arp-scan spør hver adresse på nettet og er sikrest, men krever passordfri sudo.
	if TABELL=$(sudo -n arp-scan -q -l 2>/dev/null); then
		:
	elif command -v ip >/dev/null; then
		# Bare REACHABLE er bekreftet nå. STALE blir liggende lenge etter at telefonen har gått.
		TABELL=$(ip neigh show | grep REACHABLE)
	else
		TABELL=$(arp -an)
	fi

	echo "$TABELL" | grep -oiE '([0-9a-f]{1,2}:){5}[0-9a-f]{1,2}' | mosquitto_pub -t kollektivmonitor/hjemme -s
	sleep "$INTERVALL"
done
