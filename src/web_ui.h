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
  header{display:flex;align-items:center;gap:12px;flex-wrap:wrap;
         padding:14px 16px;border-bottom:1px solid var(--line);background:var(--card)}
  h1{font-size:17px;margin:0;font-weight:600;letter-spacing:.2px}
  .pill{font-size:12px;font-weight:600;padding:4px 10px;border-radius:999px;
        background:#2a2f3a;color:var(--muted)}
  .pill.ok{background:rgba(53,196,106,.15);color:var(--ok)}
  .pill.warn{background:rgba(240,169,59,.15);color:var(--warn)}
  .pill.bad{background:rgba(232,83,79,.15);color:var(--bad)}
  main{padding:16px;max-width:900px;margin:0 auto}
  .grid{display:grid;gap:10px;grid-template-columns:repeat(auto-fit,minmax(135px,1fr));margin-bottom:14px}
  .card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:12px 14px}
  .card .label{font-size:11px;text-transform:uppercase;letter-spacing:.6px;color:var(--muted)}
  .card .value{font-size:22px;font-weight:600;margin-top:4px;font-variant-numeric:tabular-nums}
  .card .unit{font-size:13px;color:var(--muted);font-weight:400;margin-left:3px}
  #map{height:min(55vh,420px);border-radius:12px;border:1px solid var(--line);background:#11141a}
  .row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:14px 0}
  button,.btn{background:var(--card);color:var(--fg);border:1px solid var(--line);
         border-radius:10px;padding:9px 14px;font-size:14px;cursor:pointer;text-decoration:none}
  button:hover,.btn:hover{border-color:var(--accent);color:var(--accent)}
  button:disabled{opacity:.45;cursor:not-allowed}
  input[type=number]{width:92px;background:#11141a;color:var(--fg);border:1px solid var(--line);
         border-radius:10px;padding:9px 10px;font-size:14px}
  label{font-size:13px;color:var(--muted)}
  .coords{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:14px}
  .coords a{color:var(--accent);text-decoration:none}
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
</header>

<main>
  <div class="grid">
    <div class="card"><div class="label">Velocita'</div><div class="value"><span id="speed">--</span><span class="unit">km/h</span></div></div>
    <div class="card"><div class="label">Satelliti</div><div class="value" id="sats">--</div></div>
    <div class="card"><div class="label">HDOP</div><div class="value" id="hdop">--</div></div>
    <div class="card"><div class="label">Altitudine</div><div class="value"><span id="alt">--</span><span class="unit">m</span></div></div>
    <div class="card"><div class="label">Dalla base</div><div class="value"><span id="dist">--</span><span class="unit">m</span></div></div>
    <div class="card"><div class="label">Direzione</div><div class="value" id="course">--</div></div>
  </div>

  <div id="map"></div>

  <div class="row">
    <span class="coords" id="coords">in attesa del fix...</span>
  </div>

  <div class="row">
    <label for="radius">Raggio area</label>
    <input type="number" id="radius" min="20" max="50000" step="10" value="150">
    <button id="btnHere">Centra l'area qui</button>
    <button id="btnClear">Svuota traccia</button>
    <a class="btn" href="/track.gpx" download>Scarica GPX</a>
    <label><input type="checkbox" id="follow" checked> segui</label>
  </div>
</main>

<footer id="diag">--</footer>

<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<script>
const $ = id => document.getElementById(id);
let map = null, marker = null, circle = null, line = null, firstFit = true;

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
}

function fmt(v, d=1){ return (v === null || v === undefined || isNaN(v)) ? '--' : Number(v).toFixed(d); }

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

  $('speed').textContent  = fmt(s.speed);
  $('sats').textContent   = s.sats || 0;
  $('hdop').textContent   = fmt(s.hdop, 1);
  $('alt').textContent    = fmt(s.alt, 0);
  $('dist').textContent   = s.fix ? fmt(s.distance, 0) : '--';
  $('course').textContent = s.fix ? (s.cardinal || '--') : '--';

  $('coords').innerHTML = s.fix
    ? `${s.lat.toFixed(6)}, ${s.lon.toFixed(6)} &nbsp; <a target="_blank" rel="noopener"
       href="https://www.google.com/maps?q=${s.lat},${s.lon}">apri in Google Maps</a>`
    : 'in attesa del fix...';

  $('diag').textContent =
    `caratteri NMEA ${s.diag.chars} | frasi con fix ${s.diag.sentences} | checksum errati ${s.diag.errors} | ` +
    `punti traccia ${s.diag.points} | RSSI ${s.diag.rssi} dBm | heap ${(s.diag.heap/1024).toFixed(0)} kB | ` +
    `uptime ${Math.floor(s.diag.uptime/3600)}h ${Math.floor(s.diag.uptime%3600/60)}m`;

  if (!map) return;
  circle.setLatLng([s.fence.lat, s.fence.lon]).setRadius(s.fence.radius);
  if (!document.activeElement || document.activeElement.id !== 'radius') $('radius').value = Math.round(s.fence.radius);
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

async function refreshTrack(){
  if (!map) return;
  try {
    const pts = await (await fetch('/api/track')).json();
    line.setLatLngs(pts.map(p => [p[0], p[1]]));
  } catch(e){}
}

$('btnHere').onclick = async () => {
  const r = $('radius').value || 150;
  const res = await fetch(`/api/fence?here=1&radius=${r}`, {method:'POST'});
  if (!res.ok) alert('Serve un fix GPS valido per spostare qui il centro dell\'area.');
  refreshStatus();
};
$('btnClear').onclick = async () => {
  await fetch('/api/track/clear', {method:'POST'});
  if (line) line.setLatLngs([]);
};

initMap();
refreshStatus(); refreshTrack();
setInterval(refreshStatus, 2000);
setInterval(refreshTrack, 10000);
</script>
</body>
</html>
)ESPGPS";
