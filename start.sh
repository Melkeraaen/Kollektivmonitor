#!/bin/sh
# Starter MQTT-broker, backend og skjerm. Virker både på Mac og Raspberry Pi.
cd "$(dirname "$0")"

# På Pi-en kjører Mosquitto som tjeneste. Kjører den ikke, startes den her.
if ! pgrep -x mosquitto >/dev/null; then
	mosquitto -c mosquitto.conf &
	BROKER=$!
fi

node server.js &
SERVER=$!
./hjemme.sh &
HJEMME=$!
trap 'kill $SERVER $HJEMME $BROKER 2>/dev/null' EXIT INT TERM

PORT=$(node -p "require('./config.json').port")
sleep 2

if [ "$(uname)" = "Darwin" ]; then
	open "http://localhost:$PORT"
	wait $SERVER
else
	# Heter chromium på nyere Raspberry Pi OS og chromium-browser på eldre.
	CHROMIUM=$(command -v chromium || command -v chromium-browser)
	"$CHROMIUM" --kiosk --noerrdialogs --disable-infobars --incognito "http://localhost:$PORT"
fi
