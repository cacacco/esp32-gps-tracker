#pragma once
/*
 * map_ui.h - la pagina per costruire il percorso, su /percorso.
 *
 * Sta a parte dalla dashboard di proposito: li' si guarda come va il tracker,
 * qui si prepara un giro e basta. L'impianto e' quello delle indicazioni di
 * Google Maps - una riga per ogni tappa, la mappa grande sotto, il riassunto
 * in fondo - perche' e' quello che tutti sanno gia' usare.
 *
 * Il calcolo delle strade e la ricerca degli indirizzi li fa il telefono
 * mentre e' online; alla ESP32 arrivano solo il tracciato, le svolte e le
 * tappe, con le stesse richieste che usa la dashboard.
 */

static const char MAP_HTML[] PROGMEM = R"MAPUI(<!doctype html>
<html lang="it">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Crea percorso</title>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
<style>
  :root{--blu:#1a73e8;--testo:#202124;--tenue:#5f6368;--filo:#dadce0;--sfondo:#fff}
  *{box-sizing:border-box}
  html,body{height:100%;margin:0;font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;color:var(--testo)}
  #map{position:absolute;inset:0;z-index:1}

  .pannello{position:absolute;z-index:2;top:max(10px,env(safe-area-inset-top));
    left:max(10px,env(safe-area-inset-left));right:max(10px,env(safe-area-inset-right));
    max-width:400px;background:var(--sfondo);border-radius:10px;
    box-shadow:0 2px 10px rgba(0,0,0,.28);padding:10px 12px 12px}
  .testa{display:flex;align-items:center;gap:10px;margin-bottom:8px}
  .testa h1{font-size:16px;font-weight:500;margin:0;flex:1}
  .testa a{color:var(--tenue);text-decoration:none;font-size:13px}

  .riga{display:flex;align-items:center;gap:9px;padding:3px 0}
  .bollo{width:22px;height:22px;border-radius:50%;background:var(--blu);color:#fff;
    font:600 12px/22px system-ui;text-align:center;flex:none}
  .riga input{flex:1;min-width:0;border:1px solid var(--filo);border-radius:7px;
    padding:8px 10px;font-size:14px;font-family:inherit;color:var(--testo)}
  .riga input:focus{outline:2px solid var(--blu);outline-offset:-1px;border-color:var(--blu)}
  .riga .via{border:0;background:transparent;color:var(--tenue);font-size:17px;
    line-height:1;padding:4px 6px;cursor:pointer;border-radius:50%;flex:none}
  .riga .via:hover{background:#f1f3f4}
  .riga .tolgo:hover{color:#d93025}
  .riga .via:disabled{opacity:.25;cursor:default;background:transparent}
  .inverti{border:0;background:transparent;color:var(--blu);font:500 13px system-ui;
    cursor:pointer;padding:4px 6px;border-radius:6px}
  .inverti:hover{background:#f1f3f4}
  .inverti:disabled{color:#9aa0a6;cursor:default;background:transparent}

  .piu{border:0;background:transparent;color:var(--blu);font:500 14px system-ui;
    cursor:pointer;padding:7px 4px;margin-left:31px}
  .piu:hover{text-decoration:underline}

  .sugg{position:absolute;z-index:3;background:var(--sfondo);border-radius:8px;
    box-shadow:0 2px 10px rgba(0,0,0,.3);overflow:hidden;max-width:400px}
  .sugg button{display:block;width:100%;text-align:left;border:0;background:transparent;
    padding:10px 12px;font:14px system-ui;color:var(--testo);cursor:pointer;line-height:1.3}
  .sugg button:hover{background:#f1f3f4}

  .barra{position:absolute;z-index:2;left:0;right:0;bottom:0;background:var(--sfondo);
    border-top:1px solid var(--filo);display:flex;align-items:center;gap:12px;
    padding:12px max(14px,env(safe-area-inset-left)) calc(12px + env(safe-area-inset-bottom)) max(14px,env(safe-area-inset-right))}
  .barra .dati{flex:1;min-width:0}
  .barra .km{font-size:19px;font-weight:500}
  .barra .nota{font-size:12.5px;color:var(--tenue)}
  .barra button{background:var(--blu);color:#fff;border:0;border-radius:22px;
    padding:11px 20px;font:500 15px system-ui;cursor:pointer}
  .barra button:disabled{background:#e8eaed;color:#9aa0a6;cursor:default}

  .num{background:var(--blu);color:#fff;border-radius:50%;width:24px;height:24px;
    display:flex;align-items:center;justify-content:center;font:600 12px system-ui;
    border:2px solid #fff;box-shadow:0 1px 4px rgba(0,0,0,.4)}
  .qui{position:absolute;z-index:2;right:max(10px,env(safe-area-inset-right));bottom:96px;
    width:44px;height:44px;border-radius:50%;border:0;background:var(--sfondo);
    box-shadow:0 2px 8px rgba(0,0,0,.3);font-size:19px;cursor:pointer}
</style>
</head>
<body>
<div id="map"></div>

<div class="pannello">
  <div class="testa">
    <h1>Crea percorso</h1>
    <button class="inverti" id="inverti" title="percorso al contrario" disabled>&#8645; inverti</button>
    <a href="/">&larr; cruscotto</a>
  </div>
  <div id="righe"></div>
  <button class="piu" id="piu">+ Aggiungi tappa</button>
</div>

<div class="sugg" id="sugg" hidden></div>
<button class="qui" id="qui" title="parti da dove sono">&#9678;</button>

<div class="barra">
  <div class="dati">
    <div class="km" id="km">--</div>
    <div class="nota" id="nota">cerca un indirizzo, o tocca la mappa</div>
  </div>
  <button id="salva" disabled>Salva</button>
</div>

<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<script>
const $ = id => document.getElementById(id);
const OSRM = 'https://router.project-osrm.org/route/v1/driving/';
const NOMI = 'https://nominatim.openstreetmap.org/search';
const MAX_PUNTI = 1800, MAX_PER_CHIAMATA = 25;

// una riga = una tappa del giro. Due all'inizio, come su qualunque navigatore.
let righe = [{nome:'', lat:null, lon:null}, {nome:'', lat:null, lon:null}];
let strada = null, svolte = [], mappa = null, linea = null, segni = null, attesa = 0;

const SVOLTA = {left:2, right:3, 'slight left':4, 'slight right':5,
                'sharp left':6, 'sharp right':7, uturn:8};
const PAROLA = ['','dritto','a sinistra','a destra','leggera sx','leggera dx',
                'secca sx','secca dx','inversione','rotonda','arrivo'];

function tipo(m){
  const t = m.type || '';
  if (t === 'arrive') return 10;
  if (t === 'roundabout' || t === 'rotary' || t === 'roundabout turn') return 9;
  return SVOLTA[m.modifier] || 1;
}
const utile = st => {
  const t = st.maneuver.type;
  if (t === 'depart' || t === 'notification') return false;
  return !((t === 'continue' || t === 'new name') && (st.maneuver.modifier || 'straight') === 'straight');
};

const metri = (a, b) => {
  const R = 6371000, r = Math.PI/180;
  const dLat = (b[0]-a[0])*r, dLon = (b[1]-a[1])*r;
  const x = Math.sin(dLat/2)**2 + Math.cos(a[0]*r)*Math.cos(b[0]*r)*Math.sin(dLon/2)**2;
  return 2*R*Math.asin(Math.sqrt(x));
};
function rotta(a, b){
  const r = Math.PI/180, dLon = (b[1]-a[1])*r;
  const y = Math.sin(dLon)*Math.cos(b[0]*r);
  const x = Math.cos(a[0]*r)*Math.sin(b[0]*r) - Math.sin(a[0]*r)*Math.cos(b[0]*r)*Math.cos(dLon);
  return (Math.atan2(y,x)*180/Math.PI + 360) % 360;
}
const delta = (a, b) => (b - a + 540) % 360 - 180;

// toglie i punti in mezzo ai rettilinei e tiene quelli nelle curve
function rarefai(pts){
  let passo = 25, out = pts;
  for (let g = 0; g < 7; g++){
    out = [pts[0]];
    let ultimo = pts[0];
    for (let i = 1; i < pts.length - 1; i++){
      const curva = Math.abs(delta(rotta(pts[i-1], pts[i]), rotta(pts[i], pts[i+1])));
      if (metri(ultimo, pts[i]) < passo && curva < 20) continue;
      out.push(pts[i]); ultimo = pts[i];
    }
    out.push(pts[pts.length-1]);
    if (out.length <= MAX_PUNTI) break;
    passo *= 2;
  }
  return out.slice(0, MAX_PUNTI);
}

// ---- le righe --------------------------------------------------------------
function disegnaRighe(){
  const box = $('righe');
  box.innerHTML = '';
  righe.forEach((t, i) => {
    const r = document.createElement('div');
    r.className = 'riga';

    const b = document.createElement('span');
    b.className = 'bollo';
    b.textContent = i + 1;

    const inp = document.createElement('input');
    inp.type = 'search';
    inp.value = t.nome;
    inp.placeholder = i === 0 ? 'Partenza' : i === righe.length - 1 ? 'Destinazione' : 'Tappa';
    inp.autocomplete = 'off';
    inp.oninput  = () => cerca(i, inp);
    inp.onkeydown = e => { if (e.key === 'Enter'){ e.preventDefault(); cerca(i, inp, true); } };
    inp.onblur   = () => setTimeout(() => { $('sugg').hidden = true; }, 180);

    r.append(b, inp);

    const su = document.createElement('button');
    su.className = 'via';
    su.textContent = '↑';
    su.title = 'sposta su';
    su.disabled = i === 0;
    su.onclick = () => sposta(i, -1);

    const giu = document.createElement('button');
    giu.className = 'via';
    giu.textContent = '↓';
    giu.title = 'sposta giu';
    giu.disabled = i === righe.length - 1;
    giu.onclick = () => sposta(i, 1);

    r.append(su, giu);

    if (righe.length > 2){
      const x = document.createElement('button');
      x.className = 'via tolgo';
      x.textContent = '×';
      x.title = 'togli questa tappa';
      x.onclick = () => { righe.splice(i, 1); disegnaRighe(); aggiorna(); };
      r.append(x);
    }
    box.appendChild(r);
  });
}

// Scambia una tappa con quella sopra o sotto: l'ordine delle righe e' l'ordine
// in cui si passa, quindi il percorso si rifa' da solo.
function sposta(i, verso){
  const j = i + verso;
  if (j < 0 || j >= righe.length) return;
  const t = righe[i]; righe[i] = righe[j]; righe[j] = t;
  disegnaRighe();
  aggiorna();
}

// ---- ricerca degli indirizzi ----------------------------------------------
function cerca(i, input, subito){
  clearTimeout(attesa);
  const q = input.value.trim();
  righe[i].nome = q;
  if (q.length < 3) { $('sugg').hidden = true; return; }
  attesa = setTimeout(async () => {
    const box = $('sugg');
    const r = input.getBoundingClientRect();
    box.style.left = r.left + 'px';
    box.style.top  = (r.bottom + 4) + 'px';
    box.style.width = r.width + 'px';
    box.hidden = false;
    box.innerHTML = '<button disabled>cerco...</button>';
    try {
      const url = NOMI + '?format=jsonv2&limit=5&accept-language=it&q=' + encodeURIComponent(q);
      const risp = await fetch(url);
      const esiti = await risp.json();
      box.innerHTML = '';
      if (!esiti.length){ box.innerHTML = '<button disabled>nessun risultato</button>'; return; }
      esiti.forEach(e => {
        const b = document.createElement('button');
        b.textContent = e.display_name;
        b.onclick = () => {
          righe[i] = {nome: e.name || e.display_name.split(',')[0], lat:+e.lat, lon:+e.lon};
          box.hidden = true;
          disegnaRighe();
          aggiorna(true);
        };
        box.appendChild(b);
      });
    } catch(err){
      box.innerHTML = '<button disabled>ricerca non riuscita: serve internet</button>';
    }
  }, subito ? 0 : 450);
}

// ---- percorso --------------------------------------------------------------
const pronte = () => righe.filter(t => t.lat !== null);

async function calcola(){
  const p = pronte();
  if (p.length < 2) return null;
  const coords = [], passi = [];
  for (let da = 0; da < p.length - 1; da += MAX_PER_CHIAMATA - 1){
    const pezzo = p.slice(da, da + MAX_PER_CHIAMATA);
    if (pezzo.length < 2) break;
    const ultimo = da + MAX_PER_CHIAMATA - 1 >= p.length - 1;
    const url = OSRM + pezzo.map(t => t.lon.toFixed(6) + ',' + t.lat.toFixed(6)).join(';')
              + '?overview=full&geometries=geojson&steps=true';
    const r = await fetch(url);
    if (!r.ok) throw new Error('servizio strade: ' + r.status);
    const j = await r.json();
    if (j.code !== 'Ok' || !j.routes || !j.routes.length) throw new Error(j.message || 'nessuna strada');
    const rt = j.routes[0];
    const g = rt.geometry.coordinates.map(c => [c[1], c[0]]);
    for (let i = coords.length ? 1 : 0; i < g.length; i++) coords.push(g[i]);
    for (const leg of rt.legs) for (const st of leg.steps){
      if (!utile(st)) continue;
      if (st.maneuver.type === 'arrive' && !ultimo) continue;
      passi.push([st.maneuver.location[1], st.maneuver.location[0], tipo(st.maneuver),
                  st.maneuver.exit || 0, (st.name||'').replace(/[\r\n,]/g,' ').trim().slice(0,19)]);
    }
  }
  return {coords, passi, durata: 0};
}

async function aggiorna(centra){
  segni.clearLayers();
  pronte().forEach((t, i) => {
    const icona = L.divIcon({className:'', iconSize:[24,24], iconAnchor:[12,12],
                             html:'<div class="num">' + (i+1) + '</div>'});
    L.marker([t.lat, t.lon], {icon:icona, draggable:true})
      .on('dragend', e => {
        const q = e.target.getLatLng();
        const vero = righe.indexOf(pronte()[i]);
        righe[vero].lat = q.lat; righe[vero].lon = q.lng;
        righe[vero].nome = q.lat.toFixed(4) + ', ' + q.lng.toFixed(4);
        disegnaRighe(); aggiorna();
      })
      .addTo(segni);
  });

  if (pronte().length < 2){
    linea.setLatLngs([]); strada = null; svolte = [];
    $('km').textContent = '--';
    $('nota').textContent = 'cerca un indirizzo, o tocca la mappa';
    $('salva').disabled = true;
    $('inverti').disabled = true;
    return;
  }
  $('inverti').disabled = false;

  $('nota').textContent = 'calcolo le strade...';
  try {
    const res = await calcola();
    strada = rarefai(res.coords);
    svolte = res.passi;
    linea.setLatLngs(strada);
    let km = 0;
    for (let i = 1; i < strada.length; i++) km += metri(strada[i-1], strada[i]);
    $('km').textContent = (km/1000).toFixed(1) + ' km';
    $('nota').textContent = svolte.length + ' svolte, ' + pronte().length + ' tappe';
    $('salva').disabled = false;
    if (centra) mappa.fitBounds(linea.getBounds(), {padding:[60,60]});
  } catch(e){
    strada = null;
    linea.setLatLngs(pronte().map(t => [t.lat, t.lon]));
    $('km').textContent = '--';
    $('nota').textContent = 'strade non calcolate (' + e.message + '): salvo a linee dritte';
    $('salva').disabled = false;
  }
}

// ---- salvataggio -----------------------------------------------------------
$('salva').onclick = async () => {
  const p = pronte();
  const punti = strada || p.map(t => [t.lat, t.lon]);
  $('salva').disabled = true;
  $('nota').textContent = 'salvo sul tracker...';
  try {
    const corpo = punti.map(q => q[0].toFixed(6) + ',' + q[1].toFixed(6)).join(';');
    const r1 = await fetch('/api/route/draw', {method:'POST', headers:{'Content-Type':'text/plain'}, body:corpo});
    if (!r1.ok) throw new Error('percorso rifiutato');
    const j = await r1.json();

    let n = 0;
    if (svolte.length){
      const testo = svolte.map(t => t[0].toFixed(6)+','+t[1].toFixed(6)+','+t[2]+','+t[3]+','+t[4]).join('\n');
      const r2 = await fetch('/api/route/steps', {method:'POST', headers:{'Content-Type':'text/plain'}, body:testo});
      if (r2.ok) n = (await r2.json()).turns || 0;
    }
    // le tappe cercate per nome diventano le tappe del tracker
    await fetch('/api/stops', {method:'POST', headers:{'Content-Type':'application/json'},
      body: JSON.stringify(p.map((t, i) => ({lat:t.lat, lon:t.lon, name:(t.nome || ('Tappa ' + (i+1))).slice(0,23)})))});

    $('nota').textContent = 'salvato: ' + j.points + ' punti, ' + j.km + ' km, ' + n + ' svolte, ' + p.length + ' tappe';
  } catch(e){
    $('nota').textContent = 'salvataggio non riuscito: ' + e.message;
  }
  $('salva').disabled = false;
};

$('inverti').onclick = () => { righe.reverse(); disegnaRighe(); aggiorna(true); };
$('piu').onclick = () => { righe.splice(righe.length - 1, 0, {nome:'', lat:null, lon:null}); disegnaRighe(); };

$('qui').onclick = async () => {
  try {
    const s = await (await fetch('/api/status')).json();
    if (!s.fix) { $('nota').textContent = 'il tracker non ha ancora il fix'; return; }
    righe[0] = {nome:'dove sono', lat:s.lat, lon:s.lon};
    disegnaRighe();
    mappa.setView([s.lat, s.lon], 15);
    aggiorna(true);
  } catch(e){ $('nota').textContent = 'non riesco a leggere la posizione'; }
};

// ---- mappa -----------------------------------------------------------------
mappa = L.map('map', {zoomControl:false}).setView([43.8513, 12.7605], 13);
L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',
  {maxZoom:19, attribution:'&copy; OpenStreetMap'}).addTo(mappa);
L.control.zoom({position:'bottomleft'}).addTo(mappa);
linea = L.polyline([], {color:'#1a73e8', weight:6, opacity:.85}).addTo(mappa);
segni = L.layerGroup().addTo(mappa);

// un tocco sulla mappa riempie la prima riga libera, come trascinare un segnalino
mappa.on('click', e => {
  const vuota = righe.findIndex(t => t.lat === null);
  const t = {nome: e.latlng.lat.toFixed(4) + ', ' + e.latlng.lng.toFixed(4),
             lat: e.latlng.lat, lon: e.latlng.lng};
  if (vuota >= 0) righe[vuota] = t; else righe.push(t);
  disegnaRighe();
  aggiorna();
});

// se il tracker ha gia' un fix, la mappa parte da li'
fetch('/api/status').then(r => r.json()).then(s => {
  if (s.fix) mappa.setView([s.lat, s.lon], 14);
}).catch(() => {});

disegnaRighe();
</script>
</body>
</html>
)MAPUI";

static void mapRegisterRoutes(WebServer &srv) {
  srv.on("/percorso", HTTP_GET, [&srv]() {
    srv.sendHeader("Cache-Control", "no-store");
    srv.send_P(200, "text/html", MAP_HTML);
  });
}
