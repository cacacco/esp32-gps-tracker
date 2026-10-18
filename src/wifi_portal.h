/*
 * wifi_portal.h - gestione WiFi con portale di configurazione.
 *
 * All'avvio prova la rete salvata in memoria non volatile (o, la prima volta,
 * quella scritta in config.h). Se non si collega apre un proprio access point
 * con portale captive: ci si collega col telefono, si sceglie la rete da un
 * elenco e si salva. Nessuna ricompilazione per cambiare WiFi.
 *
 * Mentre il portale e' attivo il tracker continua a registrare: ogni due
 * minuti riprova la rete salvata e, se la trova, chiude l'access point.
 */
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>

#include "config.h"

static WebServer   *wifiServer   = nullptr;
static DNSServer    wifiDns;
static Preferences  wifiPrefs;
static String       wifiSsid;                 // quella a cui siamo attaccati ora
static String       wifiPass;

// Il tracker ricorda piu' di una rete: a casa il WiFi di casa, fuori l'hotspot
// del telefono. All'accensione le prova in ordine e si attacca alla prima che
// trova, senza che tu debba aprire niente.
struct WifiRete { String ssid, pass; };
static WifiRete     wifiReti[WIFI_MAX_RETI];
static uint8_t      wifiCount = 0;
static String       wifiApName;
static bool         wifiPortalOn = false;
static bool         wifiMdnsOn   = false;
static uint32_t     wifiLastTry  = 0;

// Chiamata durante le attese bloccanti: serve a non perdere le frasi NMEA
// mentre si aspetta la connessione.
static void (*wifiIdleHook)() = nullptr;

static const char PORTAL_HTML[] PROGMEM = R"PORTAL(<!doctype html>
<html lang="it">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>GPS Tracker - WiFi</title>
<style>
  :root{--bg:#0f1115;--card:#171a21;--line:#272c36;--fg:#e8ecf3;--muted:#8b94a7;--accent:#4aa3ff;--ok:#35c46a}
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--fg);
       font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
  main{max-width:520px;margin:0 auto;padding:20px 16px 40px}
  h1{font-size:18px;margin:0 0 4px}
  .sub{color:var(--muted);font-size:13px;margin-bottom:18px}
  .card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-bottom:14px}
  label{display:block;font-size:12px;text-transform:uppercase;letter-spacing:.6px;color:var(--muted);margin-bottom:6px}
  input{width:100%;background:#11141a;color:var(--fg);border:1px solid var(--line);
        border-radius:10px;padding:11px 12px;font-size:16px;margin-bottom:14px}
  button{width:100%;background:var(--accent);color:#06101d;border:0;border-radius:10px;
         padding:12px;font-size:15px;font-weight:600;cursor:pointer}
  button.ghost{background:transparent;color:var(--muted);border:1px solid var(--line);font-weight:400;margin-top:8px}
  .net{display:flex;justify-content:space-between;align-items:center;gap:10px;
       padding:11px 2px;border-bottom:1px solid var(--line);cursor:pointer}
  .net:last-child{border-bottom:0}
  .net:active{color:var(--accent)}
  .net .rssi{color:var(--muted);font-size:12px;white-space:nowrap}
  .state{font-size:13px;color:var(--muted)}
  .state b{color:var(--ok)}
</style>
</head>
<body>
<main>
  <h1>GPS Tracker</h1>
  <div class="sub">Configurazione della rete WiFi</div>

  <div class="card"><div class="state" id="state">lettura stato...</div></div>

  <div class="card">
    <form method="POST" action="/wifi/save">
      <label for="ssid">Nome della rete</label>
      <input id="ssid" name="ssid" autocapitalize="none" autocorrect="off" required>
      <label for="pass">Password</label>
      <input id="pass" name="pass" type="password" autocapitalize="none">
      <button type="submit">Salva e riavvia</button>
    </form>
    <form method="POST" action="/wifi/forget">
      <button class="ghost" type="submit">Dimentica tutte le reti</button>
    </form>
  </div>

  <div class="card">
    <label>Reti che il tracker ricorda</label>
    <div id="salvate" class="state">--</div>
    <div class="state" style="margin-top:10px">Le prova in quest'ordine a ogni accensione,
      e si attacca alla prima che trova.</div>
  </div>

  <div class="card">
    <label>Reti trovate</label>
    <div id="nets" class="state">ricerca in corso...</div>
  </div>
</main>
<script>
fetch('/wifi/scan').then(r => r.json()).then(d => {
  document.getElementById('state').innerHTML = d.connected
    ? 'Collegato a <b>' + d.ssid + '</b><br>indirizzo ' + d.ip
    : (d.ssid ? 'Rete salvata: <b>' + d.ssid + '</b>, non raggiungibile' : 'Nessuna rete salvata');
  const sal = document.getElementById('salvate');
  if (!d.salvate || !d.salvate.length) sal.textContent = 'nessuna';
  else {
    sal.innerHTML = '';
    d.salvate.forEach((n, i) => {
      const el = document.createElement('div');
      el.className = 'net';
      el.innerHTML = '<span>' + (i + 1) + '. ' + n + '</span>'
                   + '<span class="rssi" style="cursor:pointer">dimentica</span>';
      el.lastChild.onclick = async () => {
        await fetch('/wifi/forget', {method:'POST', body:new URLSearchParams({ssid:n})});
        location.reload();
      };
      sal.appendChild(el);
    });
  }
  const box = document.getElementById('nets');
  if (!d.nets || !d.nets.length) { box.textContent = 'nessuna rete trovata'; return; }
  box.innerHTML = '';
  d.nets.forEach(n => {
    const el = document.createElement('div');
    el.className = 'net';
    el.innerHTML = '<span>' + (n.lock ? '&#128274; ' : '') + n.ssid + '</span><span class="rssi">' + n.rssi + ' dBm</span>';
    el.onclick = () => { document.getElementById('ssid').value = n.ssid; document.getElementById('pass').focus(); };
    box.appendChild(el);
  });
}).catch(() => { document.getElementById('nets').textContent = 'ricerca non riuscita'; });
</script>
</body>
</html>
)PORTAL";

static bool wifiTryConnect(const String &ssid, const String &pass, uint32_t timeoutMs) {
  if (!ssid.length()) return false;

  Serial.printf("[WiFi] provo \"%s\"", ssid.c_str());
  WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);

  const uint32_t start = millis();
  uint32_t lastDot = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    if (wifiIdleHook) wifiIdleHook();
    if (millis() - lastDot >= 250) { lastDot = millis(); Serial.print('.'); }
    delay(20);
  }

  if (WiFi.status() != WL_CONNECTED) { Serial.println(F(" niente da fare")); return false; }

  Serial.printf(" ok\r\n[WiFi] dashboard su http://%s/ (o http://%s.local/)\r\n",
                WiFi.localIP().toString().c_str(), HOSTNAME);
  if (!wifiMdnsOn && MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    wifiMdnsOn = true;
  }
  return true;
}

static void wifiStartPortal() {
  if (wifiPortalOn) return;

  WiFi.mode(WIFI_AP_STA);                      // AP_STA: cosi' la scansione funziona
  const bool locked = strlen(AP_PASSWORD) >= 8;
  WiFi.softAP(wifiApName.c_str(), locked ? AP_PASSWORD : nullptr);
  wifiDns.start(53, "*", WiFi.softAPIP());     // portale captive: tutto porta qui
  wifiPortalOn = true;
  wifiLastTry  = millis();

  const String ip = WiFi.softAPIP().toString();
  if (locked)
    Serial.printf("[WiFi] portale attivo: rete \"%s\", password \"%s\", poi apri http://%s/\r\n",
                  wifiApName.c_str(), AP_PASSWORD, ip.c_str());
  else
    Serial.printf("[WiFi] portale attivo: rete aperta \"%s\", poi apri http://%s/\r\n",
                  wifiApName.c_str(), ip.c_str());
}

static void wifiStopPortal() {
  if (!wifiPortalOn) return;
  wifiDns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  wifiPortalOn = false;
  Serial.println(F("[WiFi] rete raggiunta: portale chiuso"));
}

static void wifiCarica() {
  wifiCount = 0;
  for (uint8_t i = 0; i < WIFI_MAX_RETI; i++) {
    char chiave[8];
    snprintf(chiave, sizeof(chiave), "ssid%u", i);
    const String s = wifiPrefs.getString(chiave, "");
    if (!s.length()) continue;
    snprintf(chiave, sizeof(chiave), "pass%u", i);
    wifiReti[wifiCount].ssid = s;
    wifiReti[wifiCount].pass = wifiPrefs.getString(chiave, "");
    wifiCount++;
  }
  // chi aggiorna da una versione precedente aveva una coppia sola: diventa la prima
  if (!wifiCount) {
    const String s = wifiPrefs.getString("ssid", "");
    if (s.length()) {
      wifiReti[0].ssid = s;
      wifiReti[0].pass = wifiPrefs.getString("pass", "");
      wifiCount = 1;
    }
  }
}

static void wifiSalvaTutte() {
  for (uint8_t i = 0; i < WIFI_MAX_RETI; i++) {
    char chiave[8];
    snprintf(chiave, sizeof(chiave), "ssid%u", i);
    if (i < wifiCount) wifiPrefs.putString(chiave, wifiReti[i].ssid);
    else               wifiPrefs.remove(chiave);
    snprintf(chiave, sizeof(chiave), "pass%u", i);
    if (i < wifiCount) wifiPrefs.putString(chiave, wifiReti[i].pass);
    else               wifiPrefs.remove(chiave);
  }
  wifiPrefs.remove("ssid");                    // le chiavi vecchie non servono piu'
  wifiPrefs.remove("pass");
}

// La piu' recente va in cima: e' quella che stai usando adesso, ed e' giusto
// che sia la prima a essere provata.
static void wifiAggiungi(const String &ssid, const String &pass) {
  uint8_t dove = WIFI_MAX_RETI;
  for (uint8_t i = 0; i < wifiCount; i++) if (wifiReti[i].ssid == ssid) { dove = i; break; }
  if (dove == WIFI_MAX_RETI) {
    if (wifiCount < WIFI_MAX_RETI) wifiCount++;
    dove = (uint8_t)(wifiCount - 1);           // la piu' vecchia esce dalla lista
  }
  for (uint8_t i = dove; i > 0; i--) wifiReti[i] = wifiReti[i - 1];
  wifiReti[0].ssid = ssid;
  wifiReti[0].pass = pass;
  wifiSalvaTutte();
}

static void wifiDimentica(const String &ssid) {
  if (!ssid.length()) { wifiCount = 0; wifiSalvaTutte(); return; }
  for (uint8_t i = 0; i < wifiCount; i++) {
    if (wifiReti[i].ssid != ssid) continue;
    for (uint8_t j = i; j + 1 < wifiCount; j++) wifiReti[j] = wifiReti[j + 1];
    wifiCount--;
    break;
  }
  wifiSalvaTutte();
}

// Prova le reti salvate, in ordine, ma solo quelle che si vedono davvero:
// aspettare venti secondi una rete che non c'e' e' tempo buttato.
static bool wifiProvaTutte(uint32_t timeoutMs) {
  if (!wifiCount) return false;

  bool visibile[WIFI_MAX_RETI] = { false };
  const int trovate = WiFi.scanNetworks();
  for (int n = 0; n < trovate; n++)
    for (uint8_t i = 0; i < wifiCount; i++)
      if (WiFi.SSID(n) == wifiReti[i].ssid) visibile[i] = true;
  WiFi.scanDelete();

  bool qualcuna = false;
  for (uint8_t i = 0; i < wifiCount; i++) {
    if (!visibile[i]) continue;
    qualcuna = true;
    if (wifiTryConnect(wifiReti[i].ssid, wifiReti[i].pass, timeoutMs)) {
      wifiSsid = wifiReti[i].ssid;
      wifiPass = wifiReti[i].pass;
      return true;
    }
  }

  // nessuna in elenco: puo' essere una rete nascosta, la prima si prova comunque
  if (!qualcuna && wifiTryConnect(wifiReti[0].ssid, wifiReti[0].pass, timeoutMs)) {
    wifiSsid = wifiReti[0].ssid;
    wifiPass = wifiReti[0].pass;
    return true;
  }
  return false;
}

// Scelta a mano, dal display: porta quella rete in cima e ci si attacca subito.
static bool wifiScegli(uint8_t indice) {
  if (indice >= wifiCount) return false;
  const String ssid = wifiReti[indice].ssid;
  const String pass = wifiReti[indice].pass;
  wifiAggiungi(ssid, pass);                    // in cima: sara' la prima al riavvio
  Serial.printf("[WiFi] scelta dal display: %s\r\n", ssid.c_str());

  WiFi.disconnect();
  if (wifiTryConnect(ssid, pass, 8000)) {
    wifiSsid = ssid;
    wifiPass = pass;
    if (wifiPortalOn) wifiStopPortal();
    return true;
  }
  if (!wifiPortalOn) wifiStartPortal();        // non c'e': resti comunque raggiungibile
  return false;
}

// ---------------------------------------------------------------------------
// Rotte HTTP del portale
// ---------------------------------------------------------------------------
static void wifiHandlePage() {
  wifiServer->send_P(200, "text/html", PORTAL_HTML);
}

static void wifiHandleScan() {
  const int found = WiFi.scanNetworks();

  String out = "{\"connected\":";
  out += (WiFi.status() == WL_CONNECTED) ? "true" : "false";
  out += ",\"ssid\":\"" + wifiSsid + "\"";
  out += ",\"ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString()
                                                       : WiFi.softAPIP().toString()) + "\"";
  out += ",\"portal\":";
  out += wifiPortalOn ? "true" : "false";
  out += ",\"salvate\":[";
  for (uint8_t i = 0; i < wifiCount; i++) {
    String nome = wifiReti[i].ssid;
    nome.replace("\\", "\\\\");
    nome.replace("\"", "\\\"");
    if (i) out += ',';
    out += "\"" + nome + "\"";
  }
  out += "]";

  out += ",\"nets\":[";

  for (int i = 0; i < found && i < 20; i++) {
    String ssid = WiFi.SSID(i);
    ssid.replace("\\", "\\\\");
    ssid.replace("\"", "\\\"");
    if (i) out += ',';
    out += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
           ",\"lock\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
  }
  out += "]}";

  WiFi.scanDelete();
  wifiServer->send(200, "application/json", out);
}

static void wifiReplyAndRestart(const String &message) {
  wifiServer->send(200, "text/html",
                   "<!doctype html><meta charset=utf-8>"
                   "<meta name=viewport content='width=device-width,initial-scale=1'>"
                   "<body style='background:#0f1115;color:#e8ecf3;font:16px system-ui;padding:40px;text-align:center'>"
                   + message + "</body>");
  delay(600);
  ESP.restart();
}

static void wifiHandleSave() {
  const String ssid = wifiServer->arg("ssid");
  const String pass = wifiServer->arg("pass");
  if (!ssid.length()) { wifiServer->send(400, "text/plain", "serve il nome della rete"); return; }

  wifiAggiungi(ssid, pass);
  Serial.printf("[WiFi] rete salvata: %s (ne ricorda %u)\r\n", ssid.c_str(), wifiCount);
  wifiReplyAndRestart("Rete <b>" + ssid + "</b> salvata.<br><br>"
                      "Il tracker si riavvia e prova le reti che conosce, questa per prima.");
}

static void wifiHandleForget() {
  const String ssid = wifiServer->arg("ssid");
  wifiDimentica(ssid);
  if (ssid.length()) {                         // una sola: non serve riavviare niente
    Serial.printf("[WiFi] rete dimenticata: %s\r\n", ssid.c_str());
    wifiServer->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  Serial.println(F("[WiFi] tutte le reti dimenticate"));
  wifiReplyAndRestart("Reti dimenticate.<br><br>Al riavvio il tracker apre il proprio access point.");
}

// Con il portale attivo ogni indirizzo sconosciuto porta alla pagina WiFi:
// e' quello che fa comparire la notifica di accesso alla rete sul telefono.
static bool wifiHandleNotFound() {
  if (!wifiPortalOn || !wifiServer) return false;
  wifiServer->sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/wifi", true);
  wifiServer->send(302, "text/plain", "");
  return true;
}

static void wifiRegisterRoutes(WebServer &server) {
  wifiServer = &server;
  server.on("/wifi", HTTP_GET, wifiHandlePage);
  server.on("/wifi/scan", HTTP_GET, wifiHandleScan);
  server.on("/wifi/save", HTTP_POST, wifiHandleSave);
  server.on("/wifi/forget", HTTP_POST, wifiHandleForget);
}

// ---------------------------------------------------------------------------
// Ciclo di vita
// ---------------------------------------------------------------------------
static void wifiBegin() {
  wifiPrefs.begin("wifi", false);
  wifiCarica();

  // Primo avvio: valgono le credenziali di config.h, se sono state compilate.
  if (!wifiCount && strcmp(WIFI_SSID, "LA-TUA-RETE-WIFI") != 0) {
    wifiReti[0].ssid = WIFI_SSID;
    wifiReti[0].pass = WIFI_PASS;
    wifiCount = 1;
  }
  if (wifiCount) wifiSsid = wifiReti[0].ssid;   // da mostrare finche' non si collega

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char suffix[8];
  snprintf(suffix, sizeof(suffix), "-%02x%02x", mac[4], mac[5]);
  wifiApName = String(AP_SSID_PREFIX) + suffix;

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setSleep(false);

  if (!wifiProvaTutte(WIFI_CONNECT_MS)) wifiStartPortal();
}

static void wifiLoop() {
  if (wifiPortalOn) {
    wifiDns.processNextRequest();
    if (millis() - wifiLastTry < WIFI_RETRY_MS) return;
    wifiLastTry = millis();
    if (wifiProvaTutte(8000)) wifiStopPortal();
    return;
  }

  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - wifiLastTry < 30000) return;
  wifiLastTry = millis();
  if (!wifiProvaTutte(8000)) wifiStartPortal();
}
