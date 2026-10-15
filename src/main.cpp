/*
 * ESP32 GPS Tracker con geofence
 * ------------------------------
 * Hardware: ESP32 DevKit v1 (ESP32-WROOM-32) + u-blox NEO-7M su UART2.
 *
 * Cosa fa:
 *   - legge e decodifica le frasi NMEA del NEO-7M (TinyGPS++);
 *   - pubblica una dashboard web con mappa live, velocita', satelliti, HDOP;
 *   - registra la traccia percorsa e la esporta in formato GPX;
 *   - sorveglia un'area circolare (geofence) e, quando il tracker esce o
 *     rientra, invia l'evento a SinricPro (contact sensor + notifica push).
 *
 * Tutte le impostazioni stanno in config.h.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <TinyGPS++.h>
#include <time.h>
#include <sys/time.h>

#include "config.h"
#include "web_ui.h"

#if ENABLE_SINRICPRO
  #include <SinricPro.h>
  #include <SinricProContactsensor.h>
#endif

// ---------------------------------------------------------------------------
// Stato globale
// ---------------------------------------------------------------------------
TinyGPSPlus   gps;
HardwareSerial gpsSerial(2);          // UART2: GPIO16 (RX) / GPIO17 (TX)
WebServer     server(80);
Preferences   prefs;

struct Fix {
  bool     valid        = false;      // posizione utilizzabile (qualita' sufficiente)
  double   lat          = 0;
  double   lon          = 0;
  double   altitude     = 0;          // metri
  double   speedKmh     = 0;
  double   course       = 0;          // gradi, 0 = Nord
  uint32_t satellites   = 0;
  double   hdop         = 99;
  uint32_t epoch        = 0;          // UTC
  double   distanceHome = 0;          // metri dal centro del geofence
  uint32_t lastValidMs  = 0;
};
Fix fix;

struct Geofence {
  double lat    = GEOFENCE_LAT;
  double lon    = GEOFENCE_LON;
  double radius = GEOFENCE_RADIUS_M;
};
Geofence fence;

enum FenceState { FENCE_UNKNOWN, FENCE_INSIDE, FENCE_OUTSIDE };
FenceState fenceState    = FENCE_UNKNOWN;
FenceState pendingState  = FENCE_UNKNOWN;
uint8_t    pendingCount  = 0;

struct TrackPoint {
  float    lat;
  float    lon;
  float    speedKmh;
  uint32_t epoch;
};
TrackPoint track[TRACK_MAX_POINTS];
uint16_t   trackCount = 0;            // punti validi in memoria
uint16_t   trackHead  = 0;            // prossima posizione di scrittura
uint32_t   lastTrackMs = 0;
double     lastTrackLat = 0, lastTrackLon = 0;

bool timeSynced = false;

#if PIN_GPS_PPS >= 0
volatile uint32_t ppsPulses = 0;
void IRAM_ATTR onPpsPulse() { ppsPulses++; }
#endif

// ---------------------------------------------------------------------------
// Utilita'
// ---------------------------------------------------------------------------

// Converte data/ora NMEA in timestamp UTC. 0 se il GPS non ha ancora l'ora.
static uint32_t gpsEpoch() {
  if (!gps.date.isValid() || !gps.time.isValid() || gps.date.year() < 2020) return 0;
  struct tm t = {};
  t.tm_year = gps.date.year() - 1900;
  t.tm_mon  = gps.date.month() - 1;
  t.tm_mday = gps.date.day();
  t.tm_hour = gps.time.hour();
  t.tm_min  = gps.time.minute();
  t.tm_sec  = gps.time.second();
  time_t epoch = mktime(&t);          // TZ e' impostata a UTC in setup()
  return epoch > 0 ? (uint32_t)epoch : 0;
}

// Timestamp corrente: orologio di sistema se gia' sincronizzato col GPS.
static uint32_t nowEpoch() {
  if (!timeSynced) return 0;
  return (uint32_t)time(nullptr);
}

static void isoTimestamp(uint32_t epoch, char *out, size_t len) {
  if (!epoch) { out[0] = 0; return; }
  time_t t = (time_t)epoch;
  struct tm tm_utc;
  gmtime_r(&t, &tm_utc);
  strftime(out, len, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
}

// Punto i-esimo della traccia, 0 = il piu' vecchio.
static const TrackPoint &trackAt(uint16_t i) {
  uint16_t first = (trackCount == TRACK_MAX_POINTS) ? trackHead : 0;
  return track[(first + i) % TRACK_MAX_POINTS];
}

// ---------------------------------------------------------------------------
// Configurazione del modulo GPS via protocollo UBX (opzionale)
// ---------------------------------------------------------------------------
#if GPS_CONFIGURE_UBX
static void sendUbx(uint8_t cls, uint8_t id, const uint8_t *payload, uint16_t len) {
  uint8_t header[6] = { 0xB5, 0x62, cls, id, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8) };
  uint8_t ckA = 0, ckB = 0;
  for (uint8_t i = 2; i < 6; i++) { ckA += header[i]; ckB += ckA; }
  for (uint16_t i = 0; i < len; i++) { ckA += payload[i]; ckB += ckA; }
  gpsSerial.write(header, sizeof(header));
  if (len) gpsSerial.write(payload, len);
  gpsSerial.write(ckA);
  gpsSerial.write(ckB);
  gpsSerial.flush();
}

static void configureGps() {
  const uint16_t measRate = 1000 / GPS_UPDATE_RATE_HZ;   // ms tra due misure
  const uint8_t cfgRate[6] = {
    (uint8_t)(measRate & 0xFF), (uint8_t)(measRate >> 8), // measRate
    0x01, 0x00,                                          // navRate = 1 ciclo
    0x01, 0x00                                           // timeRef = GPS
  };
  sendUbx(0x06, 0x08, cfgRate, sizeof(cfgRate));         // UBX-CFG-RATE
  Serial.printf("[GPS] rate impostato a %d Hz\r\n", GPS_UPDATE_RATE_HZ);
}
#endif

// ---------------------------------------------------------------------------
// SinricPro
// ---------------------------------------------------------------------------
#if ENABLE_SINRICPRO
bool sinricEnabled = true;            // spegnibile dall'app (power state)

bool onPowerState(const String &deviceId, bool &state) {
  sinricEnabled = state;
  Serial.printf("[SinricPro] sorveglianza %s\r\n", state ? "attiva" : "disattivata");
  return true;
}

static void setupSinricPro() {
  SinricProContactsensor &contact = SinricPro[CONTACT_ID];
  contact.onPowerState(onPowerState);
  SinricPro.onConnected([]() { Serial.println(F("[SinricPro] connesso")); });
  SinricPro.onDisconnected([]() { Serial.println(F("[SinricPro] disconnesso")); });
  SinricPro.begin(APP_KEY, APP_SECRET);
}

// open = true  -> fuori dall'area
static void reportFence(bool open, bool notify) {
  if (!sinricEnabled) return;
  SinricProContactsensor &contact = SinricPro[CONTACT_ID];
  contact.sendContactEvent(open);

  if (!notify) return;
  char msg[180];
  snprintf(msg, sizeof(msg),
           "%s l'area (%.0f m dal centro) - https://maps.google.com/?q=%.6f,%.6f",
           open ? "Tracker USCITO da" : "Tracker rientrato in",
           fix.distanceHome, fix.lat, fix.lon);
  contact.sendPushNotification(msg);   // max 1 notifica al minuto (limite SDK)
}
#endif

// ---------------------------------------------------------------------------
// Geofence
// ---------------------------------------------------------------------------
static void saveGeofence() {
  prefs.putDouble("lat", fence.lat);
  prefs.putDouble("lon", fence.lon);
  prefs.putDouble("radius", fence.radius);
}

static void loadGeofence() {
  prefs.begin("gpstracker", false);
  fence.lat    = prefs.getDouble("lat", GEOFENCE_LAT);
  fence.lon    = prefs.getDouble("lon", GEOFENCE_LON);
  fence.radius = prefs.getDouble("radius", GEOFENCE_RADIUS_M);
}

static void updateGeofence() {
  if (!fix.valid) return;

  FenceState observed;
  if (fix.distanceHome > fence.radius + GEOFENCE_HYSTERESIS_M)      observed = FENCE_OUTSIDE;
  else if (fix.distanceHome < fence.radius - GEOFENCE_HYSTERESIS_M) observed = FENCE_INSIDE;
  else return;                        // fascia di isteresi: nessuna decisione

  if (observed == fenceState) { pendingCount = 0; return; }

  if (observed != pendingState) { pendingState = observed; pendingCount = 1; }
  else                          { pendingCount++; }
  if (pendingCount < GEOFENCE_CONFIRM_SAMPLES) return;

  const bool firstDecision = (fenceState == FENCE_UNKNOWN);
  fenceState   = observed;
  pendingCount = 0;

  Serial.printf("[GEOFENCE] stato: %s (%.0f m dal centro)\r\n",
                fenceState == FENCE_OUTSIDE ? "FUORI" : "DENTRO", fix.distanceHome);

#if ENABLE_SINRICPRO
  // Alla prima determinazione allineo solo lo stato: nessuna notifica push.
  reportFence(fenceState == FENCE_OUTSIDE, !firstDecision);
#else
  (void)firstDecision;
#endif
}

// ---------------------------------------------------------------------------
// Traccia
// ---------------------------------------------------------------------------
static void maybeAddTrackPoint() {
  if (!fix.valid) return;

  const uint32_t now = millis();
  if (trackCount && now - lastTrackMs < TRACK_MIN_INTERVAL_MS) return;

  if (trackCount) {
    const double moved = TinyGPSPlus::distanceBetween(fix.lat, fix.lon, lastTrackLat, lastTrackLon);
    if (moved < TRACK_MIN_DISTANCE_M) return;
  }

  const uint32_t ts = fix.epoch ? fix.epoch : nowEpoch();
  track[trackHead] = { (float)fix.lat, (float)fix.lon, (float)fix.speedKmh, ts };
  trackHead = (trackHead + 1) % TRACK_MAX_POINTS;
  if (trackCount < TRACK_MAX_POINTS) trackCount++;

  lastTrackMs  = now;
  lastTrackLat = fix.lat;
  lastTrackLon = fix.lon;
}

// ---------------------------------------------------------------------------
// Lettura GPS
// ---------------------------------------------------------------------------
static bool fixIsUsable() {
  if (!gps.location.isValid()) return false;
  if (gps.satellites.isValid() && gps.satellites.value() < MIN_SATELLITES) return false;
  if (gps.hdop.isValid() && gps.hdop.hdop() > MAX_HDOP) return false;
  return gps.location.age() < FIX_TIMEOUT_MS;
}

static void readGps() {
  while (gpsSerial.available()) gps.encode(gpsSerial.read());

  if (gps.location.isUpdated() && fixIsUsable()) {
    fix.valid       = true;
    fix.lat         = gps.location.lat();
    fix.lon         = gps.location.lng();
    fix.altitude    = gps.altitude.isValid() ? gps.altitude.meters() : 0;
    fix.speedKmh    = gps.speed.isValid() ? gps.speed.kmph() : 0;
    fix.course      = gps.course.isValid() ? gps.course.deg() : 0;
    fix.satellites  = gps.satellites.isValid() ? gps.satellites.value() : 0;
    fix.hdop        = gps.hdop.isValid() ? gps.hdop.hdop() : 99;
    fix.epoch       = gpsEpoch();
    fix.lastValidMs = millis();
    fix.distanceHome = TinyGPSPlus::distanceBetween(fix.lat, fix.lon, fence.lat, fence.lon);

    if (!timeSynced && fix.epoch) {   // orologio di sistema allineato al GPS
      struct timeval tv;
      tv.tv_sec  = (time_t)fix.epoch;
      tv.tv_usec = 0;
      settimeofday(&tv, nullptr);
      timeSynced = true;
      Serial.println(F("[GPS] orologio di sistema sincronizzato"));
    }

    maybeAddTrackPoint();
    updateGeofence();
  }

  // Fix scaduto: la posizione resta visibile ma marcata come non valida.
  if (fix.valid && millis() - fix.lastValidMs > FIX_TIMEOUT_MS) {
    fix.valid = false;
    Serial.println(F("[GPS] fix perso"));
  }
}

// ---------------------------------------------------------------------------
// LED di stato (lampeggio non bloccante)
//   veloce continuo  = nessun dato dal modulo (controlla i collegamenti)
//   lento 1 Hz       = dati NMEA ricevuti, fix non ancora valido
//   doppio flash     = fix valido ma fuori dall'area
//   flash breve 3 s  = tutto regolare, dentro l'area
// ---------------------------------------------------------------------------
static void updateLed() {
  const uint32_t now = millis();
  const bool gpsAlive = gps.charsProcessed() > 20;
  uint16_t period, onTime;

  if (!gpsAlive)                        { period = 200;  onTime = 100; }
  else if (!fix.valid)                  { period = 1000; onTime = 500; }
  else if (fenceState == FENCE_OUTSIDE) { period = 500;  onTime = 60;  }
  else                                  { period = 3000; onTime = 80;  }

  digitalWrite(PIN_LED, (now % period) < onTime ? HIGH : LOW);
}

// ---------------------------------------------------------------------------
// Web server
// ---------------------------------------------------------------------------
static void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

static void handleStatus() {
  StaticJsonDocument<1024> doc;

  doc["fix"]        = fix.valid;
  doc["lat"]        = fix.lat;
  doc["lon"]        = fix.lon;
  doc["alt"]        = fix.altitude;
  doc["speed"]      = fix.speedKmh;
  doc["course"]     = fix.course;
  doc["cardinal"]   = TinyGPSPlus::cardinal(fix.course);
  doc["sats"]       = gps.satellites.isValid() ? gps.satellites.value() : 0;
  doc["hdop"]       = gps.hdop.isValid() ? gps.hdop.hdop() : 0;
  doc["epoch"]      = fix.epoch;
  doc["age"]        = fix.valid ? (millis() - fix.lastValidMs) / 1000 : 0;
  doc["distance"]   = fix.distanceHome;
  doc["inside"]     = (fenceState == FENCE_INSIDE);
  doc["fenceState"] = fenceState == FENCE_UNKNOWN ? "unknown"
                    : fenceState == FENCE_INSIDE  ? "inside" : "outside";

  JsonObject fenceObj = doc.createNestedObject("fence");
  fenceObj["lat"]    = fence.lat;
  fenceObj["lon"]    = fence.lon;
  fenceObj["radius"] = fence.radius;

  JsonObject diag = doc.createNestedObject("diag");
  diag["chars"]     = gps.charsProcessed();
  diag["sentences"] = gps.sentencesWithFix();
  diag["errors"]    = gps.failedChecksum();
  diag["points"]    = trackCount;
  diag["uptime"]    = millis() / 1000;
  diag["heap"]      = ESP.getFreeHeap();
  diag["rssi"]      = WiFi.RSSI();
#if PIN_GPS_PPS >= 0
  diag["pps"]       = ppsPulses;
#endif
#if ENABLE_SINRICPRO
  diag["sinric"]    = SinricPro.isConnected() ? "online" : "offline";
#else
  diag["sinric"]    = "disabled";
#endif

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// Traccia compatta: [[lat, lon, km/h, epoch], ...]
static void handleTrack() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[");
  char buf[80];
  for (uint16_t i = 0; i < trackCount; i++) {
    const TrackPoint &p = trackAt(i);
    snprintf(buf, sizeof(buf), "%s[%.6f,%.6f,%.1f,%lu]",
             i ? "," : "", p.lat, p.lon, p.speedKmh, (unsigned long)p.epoch);
    server.sendContent(buf);
  }
  server.sendContent("]");
  server.sendContent("");
}

static void handleGpx() {
  server.sendHeader("Content-Disposition", "attachment; filename=\"track.gpx\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/gpx+xml", "");
  server.sendContent(F("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                       "<gpx version=\"1.1\" creator=\"ESP32 GPS Tracker\" "
                       "xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
                       "<trk><name>ESP32 track</name><trkseg>\n"));
  char buf[160], iso[32];
  for (uint16_t i = 0; i < trackCount; i++) {
    const TrackPoint &p = trackAt(i);
    isoTimestamp(p.epoch, iso, sizeof(iso));
    if (iso[0]) snprintf(buf, sizeof(buf), "<trkpt lat=\"%.6f\" lon=\"%.6f\"><time>%s</time></trkpt>\n", p.lat, p.lon, iso);
    else        snprintf(buf, sizeof(buf), "<trkpt lat=\"%.6f\" lon=\"%.6f\"/>\n", p.lat, p.lon);
    server.sendContent(buf);
  }
  server.sendContent(F("</trkseg></trk></gpx>\n"));
  server.sendContent("");
}

static void handleSetFence() {
  if (server.hasArg("here")) {
    if (!fix.valid) { server.send(409, "application/json", "{\"error\":\"nessun fix valido\"}"); return; }
    fence.lat = fix.lat;
    fence.lon = fix.lon;
  } else {
    if (server.hasArg("lat")) fence.lat = server.arg("lat").toDouble();
    if (server.hasArg("lon")) fence.lon = server.arg("lon").toDouble();
  }
  if (server.hasArg("radius")) {
    double r = server.arg("radius").toDouble();
    if (r >= 20 && r <= 50000) fence.radius = r;
  }

  saveGeofence();
  fenceState   = FENCE_UNKNOWN;       // nuova area: lo stato va ricalcolato
  pendingState = FENCE_UNKNOWN;
  pendingCount = 0;
  if (fix.valid) fix.distanceHome = TinyGPSPlus::distanceBetween(fix.lat, fix.lon, fence.lat, fence.lon);

  Serial.printf("[GEOFENCE] nuovo centro %.6f, %.6f raggio %.0f m\r\n", fence.lat, fence.lon, fence.radius);
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleClearTrack() {
  trackCount = 0;
  trackHead  = 0;
  server.send(200, "application/json", "{\"ok\":true}");
}

static void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/track", HTTP_GET, handleTrack);
  server.on("/api/track/clear", HTTP_POST, handleClearTrack);
  server.on("/api/fence", HTTP_POST, handleSetFence);
  server.on("/track.gpx", HTTP_GET, handleGpx);
  server.onNotFound([]() { server.send(404, "text/plain", "404"); });
  server.begin();
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
static void connectWiFi(uint32_t timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.printf("[WiFi] connessione a %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  const uint32_t start = millis();
  uint32_t lastDot = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    // L'attesa del WiFi non deve far traboccare il buffer seriale del GPS.
    while (gpsSerial.available()) gps.encode(gpsSerial.read());
    if (millis() - lastDot >= 250) { lastDot = millis(); Serial.print('.'); }
    delay(20);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf(" ok\r\n[WiFi] dashboard su http://%s/ (o http://%s.local/)\r\n",
                  WiFi.localIP().toString().c_str(), HOSTNAME);
    if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println(F(" fallita: il tracker continua a registrare, riprovo piu' tardi"));
  }
}

// ---------------------------------------------------------------------------
// Riepilogo su monitor seriale
// ---------------------------------------------------------------------------
static void printStatus() {
  if (!gps.charsProcessed()) {
    Serial.println(F("[GPS] nessun dato: controlla TX/RX incrociati, GND comune e baud rate"));
    return;
  }
  if (!fix.valid) {
    Serial.printf("[GPS] in attesa del fix - satelliti in vista: %lu, frasi NMEA: %lu, errori: %lu\r\n",
                  (unsigned long)(gps.satellites.isValid() ? gps.satellites.value() : 0),
                  (unsigned long)gps.sentencesWithFix(), (unsigned long)gps.failedChecksum());
    return;
  }
  Serial.printf("[GPS] %.6f, %.6f | %.1f km/h | %.0f m slm | sat %lu | HDOP %.1f | %s (%.0f m)\r\n",
                fix.lat, fix.lon, fix.speedKmh, fix.altitude,
                (unsigned long)fix.satellites, fix.hdop,
                fenceState == FENCE_OUTSIDE ? "FUORI area" : "dentro area", fix.distanceHome);
}

// ---------------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println(F("\r\n\r\n=== ESP32 GPS Tracker ==="));

  setenv("TZ", "UTC0", 1);            // i timestamp del GPS sono UTC
  tzset();

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  gpsSerial.setRxBufferSize(1024);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);

#if PIN_GPS_PPS >= 0
  pinMode(PIN_GPS_PPS, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_GPS_PPS), onPpsPulse, RISING);
#endif

#if GPS_PASSTHROUGH
  Serial.println(F("[MODO] ponte seriale USB <-> GPS attivo (u-center). Disattivalo in config.h."));
  return;
#endif

#if GPS_CONFIGURE_UBX
  delay(500);
  configureGps();
#endif

  loadGeofence();
  Serial.printf("[GEOFENCE] centro %.6f, %.6f raggio %.0f m\r\n", fence.lat, fence.lon, fence.radius);

  connectWiFi(20000);
  setupWebServer();

#if ENABLE_SINRICPRO
  setupSinricPro();
#endif
}

void loop() {
#if GPS_PASSTHROUGH
  while (Serial.available())    gpsSerial.write(Serial.read());
  while (gpsSerial.available()) Serial.write(gpsSerial.read());
  return;
#endif

  readGps();
  updateLed();
  server.handleClient();

#if ENABLE_SINRICPRO
  if (WiFi.status() == WL_CONNECTED) SinricPro.handle();
#endif

  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= STATUS_PRINT_MS) {
    lastPrint = millis();
    printStatus();
  }

  static uint32_t lastWiFiCheck = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - lastWiFiCheck >= 30000) {
    lastWiFiCheck = millis();
    connectWiFi(8000);
  }
}
