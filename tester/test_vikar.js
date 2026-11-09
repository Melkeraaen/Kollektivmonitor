// Tester finnVikar() i app.js: hvem skjermen foreslår når den ansvarlige ikke er hjemme.
// Funksjonen hentes ut av app.js uten å kjøre resten av fila (som trenger en nettleser).
//
// Kjøres fra repoet:  node tester/test_vikar.js

const fs = require("fs");
const path = require("path");

const kilde = fs.readFileSync(path.join(__dirname, "..", "app.js"), "utf8");
const start = kilde.indexOf("function finnVikar");
const slutt = kilde.indexOf("\n}\n", start) + 2;
const finnVikar = new Function(`${kilde.slice(start, slutt)}; return finnVikar;`)();

const h = (elias, henrik, sigurd) => [
	{ navn: "Elias", hjemme: elias },
	{ navn: "Henrik", hjemme: henrik },
	{ navn: "Sigurd", hjemme: sigurd }
];

const tilfeller = [
	["V1", "Ansvarlig hjemme", ["Henrik", "Sigurd", h(true, true, true)], { borte: false, vikar: null }],
	["V2", "Ansvarlig borte, neste hjemme", ["Henrik", "Sigurd", h(true, false, true)], { borte: true, vikar: "Sigurd" }],
	["V3", "Ansvarlig og neste borte", ["Henrik", "Sigurd", h(true, false, false)], { borte: true, vikar: "Elias" }],
	["V4", "Ingen hjemme", ["Henrik", "Sigurd", h(false, false, false)], { borte: true, vikar: null }],
	["V5", "Skanningen har stoppet (hjemme = null)", ["Henrik", "Sigurd", null], { borte: false, vikar: null }],
	["V6", "Ansvarlig ikke i config.json", ["Ola", "Sigurd", h(true, true, true)], { borte: false, vikar: null }]
];

const resultater = [];
for (const [id, hva, args, forventet] of tilfeller) {
	const faktisk = finnVikar(...args);
	const ok = faktisk.borte === forventet.borte && faktisk.vikar === forventet.vikar;
	resultater.push({ id, hva, forventet: JSON.stringify(forventet), faktisk: JSON.stringify(faktisk), ok });
	console.log(`${ok ? "OK  " : "FEIL"} ${id} ${hva}: ${JSON.stringify(faktisk)}`);
}

const antallOk = resultater.filter((r) => r.ok).length;
console.log(`\n${antallOk} av ${resultater.length} tester OK`);
fs.writeFileSync(path.join(__dirname, "resultat_vikar.json"), JSON.stringify(resultater, null, 1));
process.exit(antallOk === resultater.length ? 0 : 1);
