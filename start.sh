#!/bin/sh
# Starter MQTT-broker, backend og skjerm. Virker både på Mac og Raspberry Pi.
cd "$(dirname "$0")"

# Bare på Macen startes en egen broker. På Pi-en kjører Mosquitto som tjeneste med
# passord, og en broker herfra ville tatt port 1883 fra den og sluppet inn alle.
if [ "$(uname)" = "Darwin" ] && ! pgrep -x mosquitto >/dev/null; then
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
	# Heter chromium på nyere Raspberry Pi OS og chromium-browser på eldre.
	CHROMIUM=$(command -v chromium || command -v chromium-browser)
	"$CHROMIUM" --kiosk --noerrdialogs --disable-infobars --incognito "http://localhost:$PORT"
fi
