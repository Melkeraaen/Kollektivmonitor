#!/bin/sh
# Finner MAC-adressene som er på hjemmenettet, og publiserer dem over MQTT på
# kollektivmonitor/hjemme, én per linje. server.js matcher dem mot config.json.
#
# Eget skript og ikke inni server.js: macOS skjuler ARP-tabellen for
# prosesser som Node starter, men ikke for prosesser startet fra skallet.
cd "$(dirname "$0")"
# arp-scan ligger i /usr/sbin på Pi-en, som ikke alltid er med i PATH.
PATH="$PATH:/usr/sbin"
INTERVALL=$(node -p "require('./config.json').skannSek")
# Samme broker som server.js bruker.
MQTT=$(node -p "require('./config.json').mqttUrl")

while true; do
	if TABELL=$(sudo -n arp-scan -q -l 2>/dev/null); then
		# arp-scan spør hver adresse på nettet og er sikrest, men krever passordfri sudo.
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
