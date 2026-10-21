// L'elenco dei suggerimenti non deve mai finire sopra la casella in cui
// stai scrivendo. Con la tastiera aperta lo spazio sotto sparisce, e allora
// deve aprirsi verso l'alto.
import { posizionaSugg } from './suggcore.mjs';

let errori = 0;
const ck = (c, m) => { if (!c) { console.log('  FALLITA: ' + m); errori++; } };

// finti oggetti: bastano getBoundingClientRect, style e offsetHeight
function finto(rect, altezzaNaturale) {
  const box = {
    style: {},
    get offsetHeight(){
      const m = parseFloat(box.style.maxHeight);
      return (box.style.maxHeight !== 'none' && !isNaN(m))
        ? Math.min(altezzaNaturale, m) : altezzaNaturale;
    }
  };
  const input = { getBoundingClientRect: () => rect };
  return { box, input };
}
const num = v => parseFloat(v);

function prova(nome, rect, vv, hBox, verso) {
  global.window = { visualViewport: vv, innerHeight: 800, innerWidth: 400 };
  const { box, input } = finto(rect, hBox);
  posizionaSugg(box, input);
  const top = num(box.style.top), left = num(box.style.left);
  const larg = num(box.style.width), maxH = num(box.style.maxHeight);
  const cima = vv ? vv.offsetTop : 0;
  const fondo = cima + (vv ? vv.height : 800);

  // non deve mai coprire la casella
  const h = box.offsetHeight;
  const copre = top < rect.bottom && top + h > rect.top;
  ck(!copre, `${nome}: l'elenco copre la casella (elenco ${top}..${top+h}, casella ${rect.top}..${rect.bottom})`);
  ck(top >= cima - 0.5, `${nome}: l'elenco esce dal bordo alto (${top} < ${cima})`);
  ck(larg === rect.width, `${nome}: larghezza ${larg} invece di ${rect.width}`);
  ck(left >= 8 - 0.5, `${nome}: esce dal bordo sinistro`);
  ck(maxH > 0 && maxH <= Math.max(100, (vv ? vv.height : 800) / 2) + 0.5,
     `${nome}: altezza massima fuori misura (${maxH})`);
  if (verso === 'sotto') ck(top >= rect.bottom, `${nome}: doveva aprirsi sotto, e' a ${top}`);
  if (verso === 'sopra') ck(top + box.offsetHeight <= rect.top + 0.5, `${nome}: doveva aprirsi sopra, e' a ${top}`);
  if (errori === 0 || true) console.log(`  ${nome}: elenco a ${top}, casella ${rect.top}..${rect.bottom} -> ${verso}`);
}

const casella = (top, h = 36) => ({top, bottom: top + h, left: 40, right: 320, width: 280});

// schermo libero, casella in alto: si apre sotto
prova('senza tastiera, prima tappa', casella(120), null, 200, 'sotto');

// tastiera aperta: la finestra utile e' alta 380 e parte da 0
const tastiera = {offsetTop: 0, height: 380, width: 400};
prova('tastiera aperta, prima tappa', casella(120), tastiera, 200, 'sotto');

// quarta tappa, in basso nella finestra utile: deve aprirsi SOPRA
prova('tastiera aperta, quarta tappa', casella(300), tastiera, 200, 'sopra');

// pagina spostata in su dalla tastiera (iOS): la finestra utile non parte da 0
const spostata = {offsetTop: 90, height: 360, width: 400};
prova('pagina spostata, quarta tappa', casella(380), spostata, 200, 'sopra');
prova('pagina spostata, prima tappa', casella(140), spostata, 160, 'sotto');

// elenco altissimo e poco spazio: non deve uscire dal bordo alto
prova('elenco piu alto dello schermo', casella(300), tastiera, 600, null);

// casella attaccata al bordo destro: l'elenco rientra
global.window = { visualViewport: tastiera, innerHeight: 800, innerWidth: 400 };
{
  const { box, input } = finto({top: 100, bottom: 136, left: 180, right: 460, width: 280}, 150);
  posizionaSugg(box, input);
  ck(num(box.style.left) + 280 <= 400 - 8 + 0.5, 'casella a destra: l\'elenco sborda');
  console.log(`  bordo destro: left ${num(box.style.left)} (schermo 400, elenco 280)`);
}

console.log(errori ? `\n${errori} PROVE FALLITE` : '\nL ELENCO NON COPRE MAI LA CASELLA');
process.exit(errori ? 1 : 0);
