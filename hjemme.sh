#!/bin/sh
# Finner MAC-adressene på nettet og sender dem over MQTT til server.js.
cd "$(dirname "$0")"
# arp-scan ligger i /usr/sbin på Pi-en.
PATH="$PATH:/usr/sbin"
INTERVALL=$(node -p "require('./config.json').skannSek")

# Samme broker som server.js bruker.
MQTT=$(node -p "require('./config.json').mqttUrl")

while true; do
	if TABELL=$(sudo -n arp-scan -q -l 2>/dev/null); then
		# arp-scan spør hver adresse på nettet
		:
	elif command -v ip >/dev/null; then

		TABELL=$(ip neigh show | grep REACHABLE)
	else
		TABELL=$(arp -an) 
	fi

	echo "$TABELL" | grep -oiE '([0-9a-f]{1,2}:){5}[0-9a-f]{1,2}' | mosquitto_pub -L "$MQTT/kollektivmonitor/hjemme" -s
	sleep "$INTERVALL"
done
