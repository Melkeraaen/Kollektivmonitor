// Tar imot meldingene over MQTT og gir skjermen dataene på /api/status.

const http = require("http");
const fs = require("fs");
const path = require("path");
const mqtt = require("mqtt");

const config = JSON.parse(fs.readFileSync(path.join(__dirname, "config.json"), "utf8"));
const grenseMs = config.maxAlderMin * 60 * 1000;

// Siste kontrollerte innhold fra hver ESP. sisteMelding er tidspunktet for
// siste melding med gyldig format, og brukes til å oppdage at meldinger uteblir.
const soppel = { maaling: null, ansvar: null, sisteMelding: 0, sensorFeil: null, formatFeil: false };
const inneklima = { maaling: null, sisteMelding: 0, sensorFeil: null, formatFeil: false, anbefaling: "uendret" };

let sisteSkann = 0; // tidspunkt for siste melding fra hjemme.sh
const sistSett = {}; // navn -> tidspunkt personen sist ble sett på nettet

// Felles kontroll for begge ESP-ene. Kaster feil ved ugyldig format, og
// returnerer true når meldingen har en gyldig og fersk måling.
function kontrollerMelding(kilde, data) {
	if (typeof data.ok !== "boolean") throw new Error("mangler feltet ok");
	kilde.sisteMelding = Date.now();
	kilde.formatFeil = false;

	if (!data.ok) {
		kilde.sensorFeil = typeof data.feil === "string" ? data.feil : "ukjent feil";
		return false;
	}
	if (typeof data.alder_s === "number" && data.alder_s * 1000 > grenseMs) {
		kilde.sensorFeil = "målingen er for gammel";
		return false;
	}
	kilde.sensorFeil = null;
	return true;
}

function mottaSoppel(data) {
	if (data.ok === true && (typeof data.fyll !== "number" || typeof data.nivaa !== "string")) {
		throw new Error("mangler fyll eller nivaa");
	}
	const gyldig = kontrollerMelding(soppel, data);

	// Ansvaret sendes også når sensoren er nede, så det lagres for seg.
	if (typeof data.ansvarlig === "string") {
		soppel.ansvar = { ansvarlig: data.ansvarlig, neste: data.neste ?? null };
	}
	if (gyldig) soppel.maaling = { fyll: data.fyll, nivaa: data.nivaa, mottatt: Date.now() };
}

function mottaInneklima(data) {
	if (data.ok === true && (typeof data.temperatur !== "number" || typeof data.fuktighet !== "number")) {
		throw new Error("mangler temperatur eller fuktighet");
	}
	if (!kontrollerMelding(inneklima, data)) return;

	inneklima.maaling = { temperatur: data.temperatur, fuktighet: data.fuktighet, mottatt: Date.now() };
	vurderTemperatur(data.temperatur);
}

// Varmeanbefaling med hysterese: anbefalingen slipper først når temperaturen
// er et stykke inne i det greie området igjen, så den ikke blafrer på en grense.
function vurderTemperatur(temperatur) {
	const { lav, hoy, hysterese } = config.temperatur;

	if (temperatur < lav) inneklima.anbefaling = "opp";
	else if (temperatur > hoy) inneklima.anbefaling = "ned";
	else if (inneklima.anbefaling === "opp" && temperatur >= lav + hysterese) inneklima.anbefaling = "uendret";
	else if (inneklima.anbefaling === "ned" && temperatur <= hoy - hysterese) inneklima.anbefaling = "uendret";
}

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
	// Bare vert og port. mqttUrl kan inneholde brukernavn og passord.
	console.log(`mqtt: koblet til ${new URL(config.mqttUrl).host}`);
	mqttKlient.subscribe(["kollektivmonitor/soppel", "kollektivmonitor/inneklima", "kollektivmonitor/hjemme"]);
});

mqttKlient.on("message", (emne, melding) => {
	const kilde = { "kollektivmonitor/soppel": soppel, "kollektivmonitor/inneklima": inneklima }[emne];
	try {
		if (emne === "kollektivmonitor/hjemme") mottaHjemme(melding.toString());
		if (kilde === soppel) mottaSoppel(JSON.parse(melding.toString()));
		if (kilde === inneklima) mottaInneklima(JSON.parse(melding.toString()));
	} catch (feil) {
		if (kilde) kilde.formatFeil = true;
		console.log(`mqtt: ugyldig melding på ${emne}: ${feil.message}`);
	}
});

mqttKlient.on("error", (feil) => console.log(`mqtt: ${feil.message}`));

// Feilstatus for en kilde, som tekst til skjermen, eller null når alt er bra.
function feilstatus(kilde, navn) {
	if (kilde.formatFeil) return `Ugyldig melding fra ${navn}`;
	if (!kilde.sisteMelding) return `Venter på ${navn}`;
	if (Date.now() - kilde.sisteMelding > grenseMs) return `Ingen kontakt med ${navn}`;
	if (kilde.sensorFeil) return `Sensorfeil: ${kilde.sensorFeil}`;
	return null;
}

function lagStatus() {
	const naa = Date.now();
	const soppelFeil = feilstatus(soppel, "bøtta");
	const klimaFeil = feilstatus(inneklima, "temperatursensoren");
	// Uteblir meldingene, vises sist mottatte søppelstatus og ansvar som utdatert.
	// Ved sensorfeil er bare fyllingsgraden utilgjengelig.
	const soppelUtdatert = soppel.formatFeil || naa - soppel.sisteMelding > grenseMs;
	const fyllGyldig = soppel.maaling && (soppelUtdatert || !soppel.sensorFeil);
	const skannFersk = naa - sisteSkann <= config.hjemmeSek * 1000;

	return {
		oppdateringSek: config.oppdateringSek,
		soppel: {
			fyll: fyllGyldig ? soppel.maaling.fyll : null,
			nivaa: fyllGyldig ? soppel.maaling.nivaa : null,
			ansvarlig: soppel.ansvar ? soppel.ansvar.ansvarlig : null,
			neste: soppel.ansvar ? soppel.ansvar.neste : null,
			utdatert: soppelUtdatert,
			mottatt: soppel.maaling ? soppel.maaling.mottatt : null,
			feil: soppelFeil
		},
		// Ved temperaturfeil vises verken temperatur eller ny varmeanbefaling.
		inneklima: {
			temperatur: klimaFeil ? null : inneklima.maaling.temperatur,
			fuktighet: klimaFeil ? null : inneklima.maaling.fuktighet,
			anbefaling: klimaFeil ? null : inneklima.anbefaling,
			mottatt: inneklima.maaling ? inneklima.maaling.mottatt : null,
			feil: klimaFeil
		},
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
