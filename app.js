// Henter /api/status med jevne mellomrom og fyller de tre boksene.
// Mangler en kilde, står det «–».

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

function visSoppel(soppel, hjemme) {
	const fyll = document.getElementById("soppel-fyll");
	const ansvarlig = document.getElementById("soppel-ansvarlig");
	const melding = document.getElementById("soppel-melding");

	const harFyll = soppel && soppel.fyll !== null;
	fyll.textContent = harFyll ? `${soppel.fyll} %` : "–";
	fyll.className = harFyll ? `stor ${soppel.nivaa}` : "stor";

	const navn = soppel ? soppel.ansvarlig : null;
	const { borte, vikar } = finnVikar(navn, soppel?.neste, hjemme);
	ansvarlig.textContent = navn ? `Ansvar: ${navn}` : "–";
	ansvarlig.className = borte ? "borte" : "";

	let tekst = "";
	if (harFyll && navn && (soppel.nivaa === "full" || soppel.nivaa === "nesten_full")) {
		if (vikar) tekst = `${vikar} kan ta den`;
		else if (soppel.nivaa === "full") tekst = `${navn} må tømme nå`;
		else tekst = `${navn} tømmer snart`;
	}
	melding.textContent = tekst;
	melding.className = harFyll ? `melding ${soppel.nivaa}` : "melding";
}

function visInneklima(inneklima) {
	const temperatur = document.getElementById("temperatur");
	const fuktighet = document.getElementById("fuktighet");

	if (!inneklima) {
		temperatur.textContent = "–";
		temperatur.className = "stor";
		fuktighet.textContent = "–";
		return;
	}

	temperatur.textContent = `${norsk(inneklima.temperatur, 1)}°`;
	temperatur.className = `stor ${inneklima.nivaa}`;
	// AHT20 måler fuktighet med ±2 %, så desimaler gir ingen mening.
	fuktighet.textContent = `${Math.round(inneklima.fuktighet)} % fuktighet`;
}

function visHjemme(hjemme) {
	const liste = document.getElementById("hjemme");
	liste.replaceChildren();

	if (!hjemme) {
		liste.append(lagPunkt("–", ""));
		return;
	}
	for (const person of hjemme) {
		liste.append(lagPunkt(person.navn, person.hjemme ? "" : "borte"));
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
