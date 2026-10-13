// Tar imot status fra de to ESP-ene og fra hjemme.sh over MQTT, og leverer
// alt som JSON på /api/status. Serverer også index.html, style.css og app.js.
// Krever Node 18 eller nyere og en MQTT-broker (Mosquitto).

const http = require("http");
const fs = require("fs");
const path = require("path");
const mqtt = require("mqtt");

const config = JSON.parse(fs.readFileSync(path.join(__dirname, "config.json"), "utf8"));

let soppel = null; // { fyll, nivaa, alderS, mottatt }
let ansvar = null; // { ansvarlig, neste, mottatt }
let inneklima = null; // { temperatur, fuktighet, alderS, mottatt }
let sisteSkann = 0; // tidspunkt for siste melding fra hjemme.sh
const sistSett = {}; // navn -> tidspunkt personen sist ble sett på nettet

function mottaSoppel(data) {
	// Ansvaret sendes også når sensoren er nede, så det lagres for seg.
	if (typeof data.ansvarlig === "string") {
		ansvar = { ansvarlig: data.ansvarlig, neste: data.neste ?? null, mottatt: Date.now() };
	}
	if (data.ok === true && typeof data.fyll === "number" && typeof data.nivaa === "string") {
		soppel = { fyll: data.fyll, nivaa: data.nivaa, alderS: data.alder_s ?? 0, mottatt: Date.now() };
	}
}

function mottaInneklima(data) {
	if (data.ok === true && typeof data.temperatur === "number" && typeof data.fuktighet === "number") {
		inneklima = {
			temperatur: data.temperatur,
			fuktighet: data.fuktighet,
			alderS: data.alder_s ?? 0,
			mottatt: Date.now()
		};
	}
}

// macOS skriver oktetter uten ledende null (aa:bb:cc:dd:ee:3).
function normaliserMac(mac) {
	return mac.trim().toLowerCase().split(":").map((del) => del.padStart(2, "0")).join(":");
}

// Meldingen fra hjemme.sh er MAC-adressene på nettet, én per linje.
function mottaHjemme(tekst) {
	const macer = tekst.split("\n").filter((linje) => linje.trim()).map(normaliserMac);
	for (const person of config.personer) {
		if (macer.includes(normaliserMac(person.mac))) sistSett[person.navn] = Date.now();
	}
	sisteSkann = Date.now();
}

const mqttKlient = mqtt.connect(config.mqttUrl);

mqttKlient.on("connect", () => {
	console.log(`mqtt: koblet til ${config.mqttUrl}`);
	mqttKlient.subscribe(["kollektivmonitor/soppel", "kollektivmonitor/inneklima", "kollektivmonitor/hjemme"]);
});

mqttKlient.on("message", (emne, melding) => {
	try {
		if (emne === "kollektivmonitor/hjemme") mottaHjemme(melding.toString());
		if (emne === "kollektivmonitor/soppel") mottaSoppel(JSON.parse(melding.toString()));
		if (emne === "kollektivmonitor/inneklima") mottaInneklima(JSON.parse(melding.toString()));
	} catch (feil) {
		console.log(`mqtt: ugyldig melding på ${emne}: ${feil.message}`);
	}
});

mqttKlient.on("error", (feil) => console.log(`mqtt: ${feil.message}`));

// Alderen er ESP-ens egen alder pluss tiden siden vi fikk svaret. Uten det
// siste leddet ville en ESP som slutter å svare sett fersk ut for alltid.
function erFersk(maaling) {
	if (!maaling) return false;
	const alderS = maaling.alderS + (Date.now() - maaling.mottatt) / 1000;
	return alderS <= config.maxAlderMin * 60;
}

function temperaturNivaa(temperatur) {
	if (temperatur < config.temperatur.lav) return "kald";
	if (temperatur > config.temperatur.hoy) return "varm";
	return "ok";
}

function lagStatus() {
	const naa = Date.now();
	const ansvarFersk = ansvar && naa - ansvar.mottatt <= config.maxAlderMin * 60 * 1000;
	const skannFersk = naa - sisteSkann <= config.hjemmeSek * 1000;

	return {
		oppdateringSek: config.oppdateringSek,
		soppel: {
			fyll: erFersk(soppel) ? soppel.fyll : null,
			nivaa: erFersk(soppel) ? soppel.nivaa : null,
			ansvarlig: ansvarFersk ? ansvar.ansvarlig : null,
			neste: ansvarFersk ? ansvar.neste : null
		},
		inneklima: erFersk(inneklima)
			? {
				temperatur: inneklima.temperatur,
				fuktighet: inneklima.fuktighet,
				nivaa: temperaturNivaa(inneklima.temperatur)
			}
			: null,
		// null når hjemme.sh har stoppet, så skjermen ikke viser alle som borte.
		hjemme: skannFersk
			? config.personer.map((person) => ({
				navn: person.navn,
				hjemme: naa - (sistSett[person.navn] ?? 0) <= config.hjemmeSek * 1000
			}))
			: null
	};
}

const filer = {
	"/": ["index.html", "text/html"],
	"/style.css": ["style.css", "text/css"],
	"/app.js": ["app.js", "text/javascript"]
};

const server = http.createServer((foresporsel, svar) => {
	if (foresporsel.url === "/api/status") {
		svar.writeHead(200, { "Content-Type": "application/json" });
		svar.end(JSON.stringify(lagStatus()));
		return;
	}

	const fil = filer[foresporsel.url];
	if (!fil) {
		svar.writeHead(404);
		svar.end();
		return;
	}
	svar.writeHead(200, { "Content-Type": `${fil[1]}; charset=utf-8` });
	fs.createReadStream(path.join(__dirname, fil[0])).pipe(svar);
});

server.listen(config.port, "127.0.0.1", () => {
	console.log(`KollektivMonitor kjører på http://localhost:${config.port}`);
});
