#!/bin/sh
# Starter MQTT-broker, backend og skjerm. Virker både på Mac og Raspberry Pi.
cd "$(dirname "$0")"

# På Pi-en kjører Mosquitto som tjeneste med mosquitto.conf. Kjører den ikke, startes den her.
if ! pgrep -x mosquitto >/dev/null; then
	mosquitto -c mosquitto.conf &
	BROKER=$!
fi

node server.js &
SERVER=$!
# Serveren må rekke å abonnere først, ellers går den første skanningen tapt.
sleep 2
./hjemme.sh &
HJEMME=$!
trap 'kill $SERVER $HJEMME $BROKER 2>/dev/null' EXIT INT TERM

PORT=$(node -p "require('./config.json').port")

if [ "$(uname)" = "Darwin" ]; then
	open "http://localhost:$PORT"
	wait $SERVER
else
	CHROMIUM=$(command -v chromium || command -v chromium-browser)
	"$CHROMIUM" --kiosk --noerrdialogs --disable-infobars --incognito --password-store=basic "http://localhost:$PORT"
fi
