#!/bin/sh
# Kjører alle testene som ikke trenger maskinvare. Kjøres fra repoet:
#   ./tester/kjor_alle.sh
# Krever g++, Node 18+, npm install og mosquitto i PATH.
cd "$(dirname "$0")/.."
FEIL=0
UT=$(mktemp -d)

for t in soppel inneklima; do
	echo "== Vertstest $t.ino"
	g++ -std=c++17 -Wall -Wno-unused-function -Wno-unused-variable \
		-Itester/esp_vert/stubs -include arduino_stub.h -x c++ \
		tester/esp_vert/test_$t.cpp -o "$UT/test_$t" && "$UT/test_$t" || FEIL=1
	echo
done

echo "== app.js: forslag når den ansvarlige er borte"
node tester/test_vikar.js || FEIL=1
echo

echo "== server.js med simulerte ESP-er og hjemme.sh"
node tester/test_pi.js || FEIL=1

rm -rf "$UT"
exit $FEIL
