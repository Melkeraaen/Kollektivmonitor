// Tester Pi-delen (server.js) med simulerte ESP-er og et simulert hjemme.sh.
// Starter en egen Mosquitto på port 18830 og server.js med korte tidsgrenser
// i en midlertidig mappe, sender meldinger og sjekker /api/status.
//
// Kjøres fra repoet:  node tester/test_pi.js
// Krever Node 18+, npm install og mosquitto i PATH.

const { spawn } = require("child_process");
const fs = require("fs");
const os = require("os");
const path = require("path");
const mqtt = require("mqtt");

const REPO = path.join(__dirname, "..");
const MQTT_PORT = 18830;
const HTTP_PORT = 8090;

// Korte tider så testen går fort. 0.1 min = 6 s.
const config = {
	port: HTTP_PORT,
	oppdateringSek: 1,
	maxAlderMin: 0.1,
	mqttUrl: `mqtt://localhost:${MQTT_PORT}`,
	temperatur: { lav: 20, hoy: 24, hysterese: 0.5 },
	skannSek: 1,
	hjemmeSek: 4,
	personer: [
		{ navn: "Elias", mac: "AA:BB:CC:DD:EE:01" },
		{ navn: "Henrik", mac: "AA:BB:CC:DD:EE:02" },
		{ navn: "Sigurd", mac: "AA:BB:CC:DD:EE:03" }
	]
};

const vent = (ms) => new Promise((r) => setTimeout(r, ms));
const resultater = [];

function sjekk(id, hva, forventet, faktisk, ok) {
	resultater.push({ id, hva, forventet, faktisk, ok });
	console.log(`${ok ? "OK  " : "FEIL"} ${id} ${hva}\n      forventet: ${forventet}\n      faktisk:   ${faktisk}`);
}

async function status() {
	const svar = await fetch(`http://localhost:${HTTP_PORT}/api/status`);
	return svar.json();
}

async function main() {
	const mappe = fs.mkdtempSync(path.join(os.tmpdir(), "km-test-"));
	fs.copyFileSync(path.join(REPO, "server.js"), path.join(mappe, "server.js"));
	fs.symlinkSync(path.join(REPO, "node_modules"), path.join(mappe, "node_modules"));
	fs.writeFileSync(path.join(mappe, "config.json"), JSON.stringify(config));
	fs.writeFileSync(path.join(mappe, "mosquitto.conf"), `listener ${MQTT_PORT} 127.0.0.1\nallow_anonymous true\n`);

	const broker = spawn("mosquitto", ["-c", path.join(mappe, "mosquitto.conf")], { stdio: "ignore" });
	await vent(500);
	const server = spawn("node", ["server.js"], { cwd: mappe, stdio: "ignore" });
	await vent(1500);
	const klient = mqtt.connect(config.mqttUrl);
	await new Promise((r) => klient.on("connect", r));

	const send = async (emne, data) => {
		klient.publish(`kollektivmonitor/${emne}`, typeof data === "string" ? data : JSON.stringify(data));
		await vent(300);
	};
	const soppelOk = { ok: true, fyll: 82, nivaa: "nesten_full", avstand_cm: 15.2, ansvarlig: "Henrik", neste: "Sigurd", alder_s: 1 };

	try {
		// P1 Før noen melding
		let s = await status();
		sjekk("P1", "Oppstart uten meldinger", "«Venter på bøtta», «Venter på temperatursensoren», alle verdier null",
			`${s.soppel.feil}, ${s.inneklima.feil}, fyll=${s.soppel.fyll}, temp=${s.inneklima.temperatur}`,
			s.soppel.feil === "Venter på bøtta" && s.inneklima.feil === "Venter på temperatursensoren" &&
			s.soppel.fyll === null && s.inneklima.temperatur === null);

		// P2 Gyldig søppelmelding
		await send("soppel", soppelOk);
		s = await status();
		sjekk("P2", "Gyldig søppelmelding", "fyll 82, nesten_full, ansvar Henrik, ingen feil",
			`fyll=${s.soppel.fyll}, ${s.soppel.nivaa}, ${s.soppel.ansvarlig}, feil=${s.soppel.feil}`,
			s.soppel.fyll === 82 && s.soppel.nivaa === "nesten_full" && s.soppel.ansvarlig === "Henrik" && s.soppel.feil === null);

		// P3 Sensorfeil fra ESP32
		await send("soppel", { ok: false, feil: "ingen gyldig måling - lokket åpent?", ansvarlig: "Henrik", neste: "Sigurd" });
		s = await status();
		sjekk("P3", "ok:false fra ESP32 (lokket åpent)", "fyll null, ansvar Henrik vises fortsatt, «Sensorfeil: ...»",
			`fyll=${s.soppel.fyll}, ansvar=${s.soppel.ansvarlig}, ${s.soppel.feil}`,
			s.soppel.fyll === null && s.soppel.ansvarlig === "Henrik" && s.soppel.feil.startsWith("Sensorfeil"));

		// P4 For gammel måling
		await send("soppel", { ...soppelOk, alder_s: 600 });
		s = await status();
		sjekk("P4", "Måling eldre enn grensen (alder_s)", "fyll null, «Sensorfeil: målingen er for gammel»",
			`fyll=${s.soppel.fyll}, ${s.soppel.feil}`,
			s.soppel.fyll === null && s.soppel.feil === "Sensorfeil: målingen er for gammel");

		// P5 Ugyldig JSON, så ugyldig felt
		await send("soppel", soppelOk);
		await send("soppel", "{ikke json");
		s = await status();
		const p5a = s.soppel.feil === "Ugyldig melding fra bøtta" && s.soppel.utdatert === true && s.soppel.fyll === 82;
		await send("soppel", soppelOk);
		await send("soppel", { ok: true, fyll: "mye", nivaa: "full" });
		const s2 = await status();
		const p5b = s2.soppel.feil === "Ugyldig melding fra bøtta";
		sjekk("P5", "Ugyldig melding (ikke JSON / fyll som tekst)", "«Ugyldig melding fra bøtta», siste verdi 82 vises som utdatert",
			`${s.soppel.feil}, utdatert=${s.soppel.utdatert}, fyll=${s.soppel.fyll}; tekstfelt: ${s2.soppel.feil}`, p5a && p5b);

		// P6 Gyldig melding etter ugyldig fjerner feilen
		await send("soppel", soppelOk);
		s = await status();
		sjekk("P6", "Gyldig melding etter ugyldig", "feil null, utdatert false",
			`feil=${s.soppel.feil}, utdatert=${s.soppel.utdatert}`, s.soppel.feil === null && s.soppel.utdatert === false);

		// P7 Varmeanbefaling med hysterese
		const temp = [21.0, 19.9, 20.2, 20.5, 22.0, 24.1, 23.6, 23.5];
		const forventet = ["uendret", "opp", "opp", "uendret", "uendret", "ned", "ned", "uendret"];
		const faktisk = [];
		for (const t of temp) {
			await send("inneklima", { ok: true, temperatur: t, fuktighet: 35.0, alder_s: 1 });
			faktisk.push((await status()).inneklima.anbefaling);
		}
		sjekk("P7", `Varmeanbefaling for ${temp.join(", ")} °C`, forventet.join(", "), faktisk.join(", "),
			faktisk.join() === forventet.join());

		// P8 Feil fra temperatursensoren
		await send("inneklima", { ok: false, feil: "fant ingen sensor" });
		s = await status();
		sjekk("P8", "ok:false fra ESP32-S3", "temperatur og anbefaling null, «Sensorfeil: fant ingen sensor»",
			`temp=${s.inneklima.temperatur}, anbefaling=${s.inneklima.anbefaling}, ${s.inneklima.feil}`,
			s.inneklima.temperatur === null && s.inneklima.anbefaling === null && s.inneklima.feil === "Sensorfeil: fant ingen sensor");
		await send("inneklima", { ok: true, temperatur: 21.0, fuktighet: 35.0, alder_s: 1 });

		// P9 Hvem er hjemme. Sigurd sin MAC sendes uten ledende null, slik macOS skriver den.
		await send("hjemme", "aa:bb:cc:dd:ee:02\naa:bb:cc:dd:ee:3\n11:22:33:44:55:66");
		s = await status();
		const hj = (navn) => s.hjemme.find((p) => p.navn === navn).hjemme;
		sjekk("P9", "Skanning med Henrik og Sigurd (MAC uten ledende null)", "Elias borte, Henrik og Sigurd hjemme",
			`Elias=${hj("Elias")}, Henrik=${hj("Henrik")}, Sigurd=${hj("Sigurd")}`,
			!hj("Elias") && hj("Henrik") && hj("Sigurd"));

		// P10 Henrik forsvinner fra skanningen, skanningen fortsetter
		for (let i = 0; i < 6; i++) await send("hjemme", "aa:bb:cc:dd:ee:3"), await vent(700);
		s = await status();
		sjekk("P10", `Henrik ute av skanningen i over hjemmeSek (${config.hjemmeSek} s)`, "Henrik borte, Sigurd hjemme",
			`Henrik=${hj("Henrik")}, Sigurd=${hj("Sigurd")}`, !hj("Henrik") && hj("Sigurd"));

		// P11 Skanningen stopper
		await vent(config.hjemmeSek * 1000 + 500);
		await send("soppel", soppelOk);
		s = await status();
		sjekk("P11", "Ingen skanning på over hjemmeSek", "hjemme = null (skjermen viser «–»)", `hjemme=${JSON.stringify(s.hjemme)}`,
			s.hjemme === null);

		// P12 Meldinger uteblir
		await vent(config.maxAlderMin * 60 * 1000 + 1000);
		s = await status();
		sjekk("P12", `Ingen melding på over maxAlderMin (${config.maxAlderMin * 60} s i testen)`,
			"«Ingen kontakt med bøtta», siste fyll og ansvar vises utdatert, temperatur null",
			`${s.soppel.feil}, utdatert=${s.soppel.utdatert}, fyll=${s.soppel.fyll}, ansvar=${s.soppel.ansvarlig}; ${s.inneklima.feil}, temp=${s.inneklima.temperatur}`,
			s.soppel.feil === "Ingen kontakt med bøtta" && s.soppel.utdatert && s.soppel.fyll === 82 &&
			s.soppel.ansvarlig === "Henrik" && s.inneklima.feil === "Ingen kontakt med temperatursensoren" && s.inneklima.temperatur === null);
	} finally {
		klient.end(true);
		server.kill();
		broker.kill();
		fs.rmSync(mappe, { recursive: true, force: true });
	}

	const antallOk = resultater.filter((r) => r.ok).length;
	console.log(`\n${antallOk} av ${resultater.length} tester OK`);
	fs.writeFileSync(path.join(__dirname, "resultat_pi.json"), JSON.stringify(resultater, null, 1));
	process.exit(antallOk === resultater.length ? 0 : 1);
}

main().catch((feil) => {
	console.error(feil);
	process.exit(1);
});
