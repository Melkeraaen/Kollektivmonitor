// Henter /api/status og oppdaterer skjermen.

let intervallSek = 10;

function norsk(tall, desimaler) {
	return tall.toFixed(desimaler).replace(".", ",");
}

// Den ansvarlige regnes bare som borte når skanningen faktisk sier det.
// Da foreslås noen som er hjemme, med neste i rekka først.
function finnVikar(ansvarlig, neste, hjemme) {
	if (!ansvarlig || !hjemme) return { borte: false, vikar: null };

	const person = hjemme.find((p) => p.navn === ansvarlig);
	if (!person || person.hjemme) return { borte: false, vikar: null };

	const hjemmeNavn = hjemme.filter((p) => p.hjemme).map((p) => p.navn);
	const vikar = [neste, ...hjemmeNavn].find((navn) => navn && navn !== ansvarlig && hjemmeNavn.includes(navn));
	return { borte: true, vikar: vikar ?? null };
}

function klokkeslett(ms) {
	return new Date(ms).toLocaleTimeString("nb-NO", { hour: "2-digit", minute: "2-digit" });
}

// Feilstatus med siste gyldige mottakstid, f.eks. «Ingen kontakt med bøtta · sist 19:42».
function feilTekst(kilde) {
	if (!kilde) return "Ingen kontakt med serveren";
	return kilde.mottatt ? `${kilde.feil} · sist ${klokkeslett(kilde.mottatt)}` : kilde.feil;
}

function visSoppel(soppel, hjemme) {
	const fyll = document.getElementById("soppel-fyll");
	const ansvarlig = document.getElementById("soppel-ansvarlig");
	const melding = document.getElementById("soppel-melding");

	// Utdaterte verdier står grått og uten varselfarge.
	const utdatert = !soppel || soppel.utdatert;
	const harFyll = soppel && soppel.fyll !== null;
	fyll.textContent = harFyll ? `${soppel.fyll} %` : "–";
	fyll.className = !harFyll ? "stor" : utdatert ? "stor utdatert" : `stor ${soppel.nivaa}`;

	const navn = soppel ? soppel.ansvarlig : null;
	const { borte, vikar } = utdatert ? { borte: false, vikar: null } : finnVikar(navn, soppel.neste, hjemme);
	ansvarlig.textContent = navn ? `Ansvar: ${navn}` : "–";
	ansvarlig.className = utdatert ? "utdatert" : borte ? "borte" : "";

	if (!soppel || soppel.feil) {
		melding.textContent = feilTekst(soppel);
		melding.className = "melding feil";
		return;
	}

	let tekst = "";
	if (harFyll && navn && (soppel.nivaa === "full" || soppel.nivaa === "nesten_full")) {
		if (vikar) tekst = `${vikar} er hjemme og kan ta den`;
		else if (soppel.nivaa === "full") tekst = `${navn}, nå må søpla ut`;
		else tekst = `${navn} bør ta søpla snart`;
	}
	melding.textContent = tekst;
	melding.className = harFyll ? `melding ${soppel.nivaa}` : "melding";
}

const anbefalinger = {
	opp: { tekst: "Skru opp varmen", klasse: "kald" },
	ned: { tekst: "Skru ned varmen", klasse: "varm" },
	uendret: { tekst: "", klasse: "" }
};

function visInneklima(inneklima) {
	const temperatur = document.getElementById("temperatur");
	const fuktighet = document.getElementById("fuktighet");
	const melding = document.getElementById("inneklima-melding");

	// Ved temperaturfeil vises ingen varmeanbefaling.
	if (!inneklima || inneklima.feil) {
		temperatur.textContent = "–";
		temperatur.className = "stor";
		fuktighet.textContent = "–";
		melding.textContent = feilTekst(inneklima);
		melding.className = "melding feil";
		return;
	}

	const anbefaling = anbefalinger[inneklima.anbefaling];
	temperatur.textContent = `${norsk(inneklima.temperatur, 1)}°`;
	temperatur.className = `stor ${anbefaling.klasse}`;

	// AHT20 måler fuktighet med ±2 %, så desimaler gir ingen mening.
	fuktighet.textContent = `${Math.round(inneklima.fuktighet)} % fuktighet`;
	melding.textContent = anbefaling.tekst;
	melding.className = `melding ${anbefaling.klasse}`;
}

function visHjemme(hjemme) {
	const liste = document.getElementById("hjemme");
	liste.replaceChildren();

	if (!hjemme) {
		liste.append(lagPunkt("–", ""));
		return;
	}
	for (const person of hjemme) {
		liste.append(lagPunkt(person.navn, person.hjemme ? "hjemme" : "borte"));
	}
}

function lagPunkt(tekst, klasse) {
	const punkt = document.createElement("li");
	punkt.textContent = tekst;
	punkt.className = klasse;
	return punkt;
}

async function oppdater() {
	try {
		const svar = await fetch("/api/status");
		const status = await svar.json();
		intervallSek = status.oppdateringSek;
		visSoppel(status.soppel, status.hjemme);
		visInneklima(status.inneklima);
		visHjemme(status.hjemme);
	} catch {
		visSoppel(null, null);
		visInneklima(null);
		visHjemme(null);
	}
	setTimeout(oppdater, intervallSek * 1000);
}

oppdater();
