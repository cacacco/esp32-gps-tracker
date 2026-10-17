/*
 * web_ui.h - dashboard servita dall'ESP32 (pagina unica, nessun filesystem
 * da caricare). Le tile della mappa e la libreria Leaflet arrivano da
 * internet: se il dispositivo che apre la pagina e' offline, la mappa resta
 * vuota ma numeri, stato e link a Google Maps continuano a funzionare.
 */
#pragma once

#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"ESPGPS(<!doctype html>
<html lang="it">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 GPS Tracker</title>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
<style>
  :root{
    --bg:#0f1115; --card:#171a21; --line:#272c36; --fg:#e8ecf3;
    --muted:#8b94a7; --ok:#35c46a; --warn:#f0a93b; --bad:#e8534f; --accent:#4aa3ff;
  }
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--fg);
       font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
  header{display:flex;align-items:center;gap:10px;flex-wrap:wrap;
         padding:14px 16px;border-bottom:1px solid var(--line);background:var(--card)}
  h1{font-size:17px;margin:0;font-weight:600;letter-spacing:.2px;margin-right:auto}
  .pill{font-size:12px;font-weight:600;padding:4px 10px;border-radius:999px;
        background:#2a2f3a;color:var(--muted);text-decoration:none}
  .pill.ok{background:rgba(53,196,106,.15);color:var(--ok)}
  .pill.warn{background:rgba(240,169,59,.15);color:var(--warn)}
  .pill.bad{background:rgba(232,83,79,.15);color:var(--bad)}
  a.pill{border:1px solid var(--line)}
  main{padding:16px;max-width:900px;margin:0 auto}
  .banner{display:none;background:rgba(240,169,59,.12);border:1px solid rgba(240,169,59,.35);
          color:var(--warn);border-radius:12px;padding:11px 14px;margin-bottom:14px;font-size:14px}
  .banner a{color:var(--warn)}
  .grid{display:grid;gap:10px;grid-template-columns:repeat(auto-fit,minmax(135px,1fr));margin-bottom:10px}
  .card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:12px 14px}
  .card .label{font-size:11px;text-transform:uppercase;letter-spacing:.6px;color:var(--muted)}
  .card .value{font-size:22px;font-weight:600;margin-top:4px;font-variant-numeric:tabular-nums}
  .card .unit{font-size:13px;color:var(--muted);font-weight:400;margin-left:3px}
  .sep{font-size:11px;text-transform:uppercase;letter-spacing:.6px;color:var(--muted);margin:16px 0 8px}
  #map{height:min(55vh,420px);border-radius:12px;border:1px solid var(--line);background:#11141a}
  .row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:14px 0}
  button,.btn{background:var(--card);color:var(--fg);border:1px solid var(--line);
         border-radius:10px;padding:9px 14px;font-size:14px;cursor:pointer;text-decoration:none}
  button:hover,.btn:hover{border-color:var(--accent);color:var(--accent)}
  input[type=number]{width:92px;background:#11141a;color:var(--fg);border:1px solid var(--line);
         border-radius:10px;padding:9px 10px;font-size:14px}
  label{font-size:13px;color:var(--muted)}
  .coords{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:14px}
  .coords a{color:var(--accent);text-decoration:none}
  .stops{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:4px 12px}
  .stop{display:flex;gap:8px;align-items:center;padding:9px 0;border-bottom:1px solid var(--line)}
  .stop:last-child{border-bottom:0}
  .stop .num{color:var(--muted);font-size:12px;min-width:2ch;text-align:right}
  .stop input{flex:1;min-width:0;background:#11141a;color:var(--fg);border:1px solid var(--line);
              border-radius:8px;padding:8px 10px;font-size:15px}
  .stop.next input{border-color:var(--accent)}
  .stop .dist{color:var(--muted);font-size:12px;white-space:nowrap;font-variant-numeric:tabular-nums}
  .mini{padding:7px 10px;font-size:14px;line-height:1}
  .empty{color:var(--muted);font-size:14px;padding:12px 0}
  .btn.on{border-color:var(--accent);color:var(--accent)}
  .btn.primario{border-color:var(--accent);color:var(--accent);font-weight:600}
  footer{color:var(--muted);font-size:12px;border-top:1px solid var(--line);
         padding:12px 16px;text-align:center;line-height:1.7}
</style>
</head>
<body>
<header>
  <h1>ESP32 GPS Tracker</h1>
  <span class="pill" id="fixPill">connessione...</span>
  <span class="pill" id="fencePill">area: --</span>
  <span class="pill" id="sinricPill" hidden>SinricPro</span>
  <a class="pill" href="/wifi">WiFi</a>
</header>

<main>
  <div class="banner" id="banner"></div>

  <div class="grid">
    <div class="card"><div class="label">Velocita'</div><div class="value"><span id="speed">--</span><span class="unit">km/h</span></div></div>
    <div class="card"><div class="label">Satelliti</div><div class="value" id="sats">--</div>
      <div class="coords" id="cielo">in vista --</div></div>
    <div class="card"><div class="label">HDOP</div><div class="value" id="hdop">--</div></div>
    <div class="card"><div class="label">Altitudine</div><div class="value"><span id="alt">--</span><span class="unit">m</span></div></div>
    <div class="card"><div class="label">Dalla base</div><div class="value"><span id="dist">--</span><span class="unit">m</span></div></div>
    <div class="card"><div class="label">Direzione</div><div class="value" id="course">--</div></div>
  </div>

  <div class="sep">Viaggio in corso</div>
  <div class="grid">
    <div class="card"><div class="label">Percorsi</div><div class="value"><span id="tripDist">--</span><span class="unit">km</span></div></div>
    <div class="card"><div class="label">Velocita' max</div><div class="value"><span id="tripMax">--</span><span class="unit">km/h</span></div></div>
    <div class="card"><div class="label">In movimento</div><div class="value" id="tripMoving">--</div></div>
    <div class="card"><div class="label">Odometro</div><div class="value"><span id="tripTotal">--</span><span class="unit">km</span></div></div>
  </div>

  <div id="map"></div>

  <div class="row">
    <span class="coords" id="coords">in attesa del fix...</span>
  </div>

  <div class="row">
    <span class="coords" id="nav"></span>
  </div>

  <div class="row">
    <label for="radius">Raggio area</label>
    <input type="number" id="radius" min="20" max="50000" step="10" value="150">
    <button id="btnHere">Centra l'area qui</button>
    <label><input type="checkbox" id="follow" checked> segui</label>
  </div>

  <div class="row">
    <a class="btn" href="/track.gpx" download>Scarica GPX</a>
    <a class="btn" id="btnOld" href="/track-precedente.gpx" download hidden>GPX precedente</a>
    <button id="btnTrip">Azzera viaggio</button>
    <button id="btnClear">Cancella traccia salvata</button>
    <button id="btnTouch">Ritara il tocco</button>
  </div>

  <div class="sep">Percorso</div>
  <div class="row">
    <a class="btn primario" href="/percorso">Crea percorso</a>
    <label class="btn" for="gpx">Carica GPX</label>
    <input type="file" id="gpx" accept=".gpx,.xml" hidden>
    <button id="btnRoute" hidden>Cancella percorso</button>
  </div>

  <div class="sep">Tappe</div>
  <div class="stops" id="stopsBox"><div class="empty">nessuna tappa</div></div>
  <div class="row">
    <button id="btnStopHere">Aggiungi dove sono</button>
    <button id="btnStopMap" class="btn">Aggiungi toccando la mappa</button>
    <button id="btnStopsSave" hidden>Salva le modifiche</button>
  </div>
</main>

<footer id="diag">--</footer>

<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<script>
const $ = id => document.getElementById(id);
let map = null, marker = null, circle = null, line = null, firstFit = true;
let routeLine = null, targetMark = null, routePts = [];
let stopLayer = null, stopsArr = [], stopsNext = 0, stopsDirty = false, addOnMap = false;
let turnLayer = null, turnsArr = [];

// come si legge ogni tipo di manovra, nell'ordine dell'enum del firmware
const SVOLTA_PAROLA = ['', 'dritto', 'a sinistra', 'a destra', 'leggera sx',
                       'leggera dx', 'secca sx', 'secca dx', 'inversione',
                       'rotonda', 'arrivo'];

// Il calcolo delle strade lo fa questo servizio pubblico, dal telefono, mentre
// prepari il giro. Il tracker riceve solo il risultato. Per cambiarlo (server
// proprio, altro provider) basta questa riga.
let lastFix = null;

function initMap(){
  if (typeof L === 'undefined') { $('map').innerHTML =
    '<p style="padding:16px;color:#8b94a7">Mappa non disponibile (nessuna connessione a internet su questo dispositivo). I dati numerici restano validi.</p>';
    return; }
  map = L.map('map', {zoomControl:true}).setView([0,0], 2);
  L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',
    {maxZoom:19, attribution:'&copy; OpenStreetMap'}).addTo(map);
  marker = L.circleMarker([0,0], {radius:7, color:'#4aa3ff', fillColor:'#4aa3ff', fillOpacity:.9}).addTo(map);
  circle = L.circle([0,0], {radius:150, color:'#35c46a', weight:2, fillOpacity:.07}).addTo(map);
  line   = L.polyline([], {color:'#4aa3ff', weight:3, opacity:.75}).addTo(map);
  routeLine  = L.polyline([], {color:'#f0a93b', weight:4, opacity:.8, dashArray:'1 8', lineCap:'round'}).addTo(map);
  targetMark = L.circleMarker([0,0], {radius:6, color:'#f0a93b', fillColor:'#f0a93b', fillOpacity:1});
  stopLayer  = L.layerGroup().addTo(map);
  turnLayer  = L.layerGroup().addTo(map);
  map.on('click', e => {
    if (!addOnMap) return;
    stopsArr.push({lat:e.latlng.lat, lon:e.latlng.lng, name:'Tappa ' + (stopsArr.length + 1)});
    stopsDirty = true;
    renderStops();
  });
}

// direzione in gradi da a verso b
// di quanto cambia la direzione, con segno, fra -180 e 180
// distanza in metri tra due punti, per mostrare quanto manca a ogni tappa
function metri(a, b){
  const R = 6371000, r = Math.PI/180;
  const dLat = (b[0]-a[0])*r, dLon = (b[1]-a[1])*r;
  const x = Math.sin(dLat/2)**2 + Math.cos(a[0]*r)*Math.cos(b[0]*r)*Math.sin(dLon/2)**2;
  return 2*R*Math.asin(Math.sqrt(x));
}
function distTxt(m){ return m < 1000 ? Math.round(m) + ' m' : (m/1000).toFixed(1) + ' km'; }

function fmt(v, d=1){ return (v === null || v === undefined || isNaN(v)) ? '--' : Number(v).toFixed(d); }

function fmtTime(s){
  if (!s) return '--';
  const h = Math.floor(s/3600), m = Math.floor(s%3600/60);
  return h ? h + 'h ' + m + 'm' : m ? m + 'm ' + (s%60) + 's' : s + 's';
}

async function refreshStatus(){
  let s;
  try { s = await (await fetch('/api/status')).json(); }
  catch(e){ $('fixPill').className = 'pill bad'; $('fixPill').textContent = 'tracker non raggiungibile'; return; }

  const pill = $('fixPill');
  if (s.fix){ pill.className = 'pill ok'; pill.textContent = 'fix valido'; }
  else if (s.diag.sentences > 0){ pill.className = 'pill warn'; pill.textContent = 'ricerca satelliti...'; }
  else { pill.className = 'pill bad'; pill.textContent = 'nessun dato dal GPS'; }

  const fp = $('fencePill');
  fp.textContent = 'area: ' + (s.fenceState === 'inside' ? 'dentro' : s.fenceState === 'outside' ? 'FUORI' : 'sconosciuta');
  fp.className = 'pill ' + (s.fenceState === 'inside' ? 'ok' : s.fenceState === 'outside' ? 'bad' : '');

  if (s.diag.sinric !== 'disabled'){
    const sp = $('sinricPill');
    sp.hidden = false;
    sp.textContent = 'SinricPro: ' + s.diag.sinric;
    sp.className = 'pill ' + (s.diag.sinric === 'online' ? 'ok' : 'warn');
  }

  const banner = $('banner');
  if (s.diag.portal){
    banner.style.display = 'block';
    banner.innerHTML = 'Il tracker non ha trovato la rete salvata e sta facendo da access point (<b>' +
      s.diag.ssid + '</b>). <a href="/wifi">Scegli una rete</a> per rimetterlo online.';
  } else banner.style.display = 'none';

  $('speed').textContent  = fmt(s.speed);
  $('sats').textContent   = s.sats || 0;
  // "sats" sono quelli usati per la posizione, "inview" quelli che si vedono:
  // senza fix il primo e' zero e non direbbe se l'antenna sta ricevendo
  $('cielo').textContent  = s.inview
    ? `in vista ${s.inview}, segnale ${s.snr} dB`
    : 'nessuno in vista';
  $('hdop').textContent   = fmt(s.hdop, 1);
  $('alt').textContent    = fmt(s.alt, 0);
  $('dist').textContent   = s.fix ? fmt(s.distance, 0) : '--';
  $('course').textContent = s.fix ? (s.cardinal || '--') : '--';

  $('tripDist').textContent   = fmt(s.trip.dist/1000, 2);
  $('tripMax').textContent    = fmt(s.trip.max, 1);
  $('tripMoving').textContent = fmtTime(s.trip.moving);
  $('tripTotal').textContent  = fmt(s.trip.total/1000, 1);

  $('btnOld').hidden = !s.diag.backup;
  $('btnTouch').hidden = !s.diag.touch;      // senza touch il pulsante riavvierebbe e basta

  const n = s.nav || {};
  $('btnRoute').hidden = !n.active;
  if (n.active){
    const testa = n.done ? '<b style="color:#35c46a">Arrivato</b>'
      : n.off ? `<b style="color:#e8534f">Fuori percorso</b> di ${fmt(n.offDist,0)} m` : 'In rotta';
    const tappa = n.stops ? ` &nbsp;·&nbsp; prossima tappa <b>${n.stop}</b> a ${fmt(n.stopDist,0)} m` : '';
    const svolta = n.turnKind ? ` &nbsp;·&nbsp; fra ${fmt(n.turnDist,0)} m <b style="color:#f0a93b">` +
      `${SVOLTA_PAROLA[n.turnKind] || ''}</b>${n.turnName ? ' su ' + n.turnName : ''}` : '';
    $('nav').innerHTML = `${testa} &nbsp;·&nbsp; restano ${fmt(n.remaining/1000,1)} km` + svolta + tappa;
    if (map && routePts[n.target]) targetMark.setLatLng(routePts[n.target]).addTo(map);
  } else {
    $('nav').textContent = '';
    if (map && targetMark) map.removeLayer(targetMark);
  }

  $('coords').innerHTML = s.fix
    ? `${s.lat.toFixed(6)}, ${s.lon.toFixed(6)} &nbsp; <a target="_blank" rel="noopener"
       href="https://www.google.com/maps?q=${s.lat},${s.lon}">apri in Google Maps</a>`
    : 'in attesa del fix...';

  $('diag').textContent =
    `rete ${s.diag.ssid || '--'} | RSSI ${s.diag.rssi} dBm | caratteri NMEA ${s.diag.chars} | ` +
    `frasi valide ${s.diag.sentences} | checksum errati ${s.diag.errors} | ` +
    `punti salvati ${s.diag.points} | memoria libera ${s.diag.fsfree} kB | heap ${(s.diag.heap/1024).toFixed(0)} kB | ` +
    `uptime ${Math.floor(s.diag.uptime/3600)}h ${Math.floor(s.diag.uptime%3600/60)}m`;

  if (!map) return;
  circle.setLatLng([s.fence.lat, s.fence.lon]).setRadius(s.fence.radius);
  if (!document.activeElement || document.activeElement.id !== 'radius') $('radius').value = Math.round(s.fence.radius);
  if (s.fix) lastFix = [s.lat, s.lon];

  if (s.fix){
    marker.setLatLng([s.lat, s.lon]);
    marker.setStyle({color: s.fenceState === 'outside' ? '#e8534f' : '#4aa3ff',
                     fillColor: s.fenceState === 'outside' ? '#e8534f' : '#4aa3ff'});
    if (firstFit){ map.setView([s.lat, s.lon], 16); firstFit = false; }
    else if ($('follow').checked) map.panTo([s.lat, s.lon]);
  } else if (firstFit){
    map.setView([s.fence.lat, s.fence.lon], 15); firstFit = false;
  }
}

function renderStops(){
  const box = $('stopsBox');
  $('btnStopsSave').hidden = !stopsDirty;

  if (!stopsArr.length){
    box.innerHTML = '<div class="empty">Nessuna tappa. Caricane un GPX che ne contenga, oppure aggiungile qui.</div>';
  } else {
    box.innerHTML = '';
    stopsArr.forEach((t, i) => {
      const riga = document.createElement('div');
      riga.className = 'stop' + (i === stopsNext ? ' next' : '');
      const d = lastFix ? distTxt(metri(lastFix, [t.lat, t.lon])) : '';
      riga.innerHTML = `<span class="num">${i+1}</span>`;
      const campo = document.createElement('input');
      campo.value = t.name;
      campo.maxLength = 23;
      campo.oninput = () => { t.name = campo.value; stopsDirty = true; $('btnStopsSave').hidden = false; };
      riga.appendChild(campo);
      const dist = document.createElement('span');
      dist.className = 'dist'; dist.textContent = d;
      riga.appendChild(dist);
      [['\u2191',-1],['\u2193',1]].forEach(([lab, dir]) => {
        const b = document.createElement('button');
        b.className = 'mini'; b.textContent = lab;
        b.onclick = () => {
          const j = i + dir;
          if (j < 0 || j >= stopsArr.length) return;
          [stopsArr[i], stopsArr[j]] = [stopsArr[j], stopsArr[i]];
          stopsDirty = true; renderStops();
        };
        riga.appendChild(b);
      });
      const del = document.createElement('button');
      del.className = 'mini'; del.textContent = '\u2715';
      del.onclick = () => { stopsArr.splice(i, 1); stopsDirty = true; renderStops(); };
      riga.appendChild(del);
      box.appendChild(riga);
    });
  }

  if (!map || !stopLayer) return;
  stopLayer.clearLayers();
  stopsArr.forEach((t, i) => {
    L.circleMarker([t.lat, t.lon], {
      radius: 7, weight: 2,
      color: i === stopsNext ? '#f0a93b' : '#8b94a7',
      fillColor: '#171a21', fillOpacity: 1
    }).bindTooltip(`${i+1}. ${t.name}`, {direction:'top'}).addTo(stopLayer);
  });
}

async function refreshStops(){
  if (stopsDirty) return;                 // non sovrascrivo quello che stai scrivendo
  try {
    const d = await (await fetch('/api/stops')).json();
    stopsArr  = d.list || [];
    stopsNext = d.next || 0;
    renderStops();
  } catch(e){}
}

// Tiene solo i punti in cui devi decidere qualcosa: "prosegui dritto sulla
// stessa strada" sul display sarebbe solo rumore.
// Toglie i punti in mezzo ai rettilinei e tiene quelli nelle curve.
// ---- ricerca degli indirizzi (Nominatim, lo stesso dietro a OpenStreetMap) ----

async function refreshRoute(){
  if (!map) return;
  try {
    const pts = await (await fetch('/api/route')).json();
    routePts = pts.map(p => [p[0], p[1]]);
    routeLine.setLatLngs(routePts);
  } catch(e){}
}

async function refreshTurns(){
  if (!map) return;
  try {
    turnsArr = await (await fetch('/api/route/steps')).json();
    turnLayer.clearLayers();
    turnsArr.forEach((t, i) => L.circleMarker([t[0], t[1]],
      {radius:4, color:'#f0a93b', fillColor:'#0f1115', fillOpacity:1, weight:2})
      .addTo(turnLayer)
      .bindTooltip(`${i+1}. ${SVOLTA_PAROLA[t[2]] || ''}${t[4] ? ' su ' + t[4] : ''}`));
  } catch(e){}
}

async function refreshTrack(){
  if (!map) return;
  try {
    const segs = await (await fetch('/api/track')).json();
    line.setLatLngs(segs.map(seg => seg.map(p => [p[0], p[1]])));
  } catch(e){}
}

$('btnHere').onclick = async () => {
  const r = $('radius').value || 150;
  const res = await fetch(`/api/fence?here=1&radius=${r}`, {method:'POST'});
  if (!res.ok) alert('Serve un fix GPS valido per spostare qui il centro dell\'area.');
  refreshStatus();
};
$('gpx').onchange = async (e) => {
  const f = e.target.files[0];
  if (!f) return;
  $('nav').textContent = 'caricamento di ' + f.name + '...';
  const fd = new FormData();
  fd.append('file', f, f.name);
  try {
    const j = await (await fetch('/api/route', {method:'POST', body: fd})).json();
    alert(j.points ? `Percorso caricato: ${j.points} punti, ${j.km} km`
                   : 'Nessun punto trovato: il file e\' un GPX valido?');
  } catch(err){ alert('Caricamento non riuscito'); }
  e.target.value = '';
  stopsDirty = false;
  refreshRoute(); refreshTurns(); refreshStops(); refreshStatus();
};
$('btnRoute').onclick = async () => {
  if (!confirm('Cancellare il percorso caricato?')) return;
  await fetch('/api/route/clear', {method:'POST'});
  if (routeLine) routeLine.setLatLngs([]);
  if (turnLayer) turnLayer.clearLayers();
  routePts = []; turnsArr = [];
  refreshRoute(); refreshTurns(); refreshStatus();
};
$('btnStopHere').onclick = () => {
  if (!lastFix) { alert('Serve un fix GPS valido.'); return; }
  stopsArr.push({lat:lastFix[0], lon:lastFix[1], name:'Tappa ' + (stopsArr.length + 1)});
  stopsDirty = true;
  renderStops();
};
$('btnStopMap').onclick = () => {
  addOnMap = !addOnMap;
  $('btnStopMap').classList.toggle('on', addOnMap);
  $('btnStopMap').textContent = addOnMap ? 'Tocca la mappa (annulla)' : 'Aggiungi toccando la mappa';
};
$('btnStopsSave').onclick = async () => {
  const r = await fetch('/api/stops', {
    method: 'POST',
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify(stopsArr)
  });
  if (!r.ok) { alert('Salvataggio non riuscito.'); return; }
  stopsDirty = false;
  addOnMap = false;
  $('btnStopMap').classList.remove('on');
  $('btnStopMap').textContent = 'Aggiungi toccando la mappa';
  refreshStops();
};
$('btnTouch').onclick = async () => {
  if (!confirm('Butto via la taratura del tocco del display: il tracker si riavvia e la rifa\' da capo. Procedo?')) return;
  try { await fetch('/api/touch/reset', {method:'POST'}); } catch(e){}
  alert('Il tracker si sta riavviando: segui le istruzioni sul display.');
};
$('btnTrip').onclick = async () => { await fetch('/api/trip/reset', {method:'POST'}); refreshStatus(); };
$('btnClear').onclick = async () => {
  if (!confirm('Cancellare la traccia salvata nella memoria del tracker?')) return;
  await fetch('/api/track/clear', {method:'POST'});
  if (line) line.setLatLngs([]);
  refreshStatus();
};

initMap();
refreshStatus(); refreshTrack(); refreshRoute(); refreshTurns(); refreshStops();
setInterval(refreshStatus, 2000);
setInterval(refreshTrack, 10000);
setInterval(refreshStops, 10000);
</script>
</body>
</html>
)ESPGPS";
