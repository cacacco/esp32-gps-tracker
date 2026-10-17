/*
 * ESP32 GPS Tracker con geofence
 * ------------------------------
 * Hardware: ESP32 DevKit v1 (ESP32-WROOM-32) + u-blox NEO-7M su UART2.
 *
 * Cosa fa:
 *   - legge e decodifica le frasi NMEA del NEO-7M (TinyGPS++);
 *   - pubblica una dashboard web con mappa live, velocita', satelliti, HDOP;
 *   - registra la traccia su memoria interna (sopravvive allo spegnimento) e
 *     la esporta in formato GPX, un segmento per accensione;
 *   - tiene le statistiche del viaggio e l'odometro totale;
 *   - si configura il WiFi dal telefono, senza ricompilare (portale captive);
 *   - sorveglia un'area circolare (geofence) e, quando il tracker esce o
 *     rientra, invia l'evento a SinricPro (contact sensor + notifica push).
 *
 * Tutte le impostazioni stanno in config.h.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <TinyGPS++.h>
#include <time.h>
#include <sys/time.h>

#include "config.h"
#include "track_store.h"
#include "route.h"
#include "wifi_portal.h"
#include "display_ui.h"
#include "web_ui.h"

#if ENABLE_OTA
  #include <ArduinoOTA.h>
#endif

#if ENABLE_SINRICPRO
  #include <SinricPro.h>
  #include <SinricProContactsensor.h>
#endif

// ---------------------------------------------------------------------------
// Stato globale
// ---------------------------------------------------------------------------
TinyGPSPlus    gps;
HardwareSerial gpsSerial(2);          // UART2: GPIO16 (RX) / GPIO17 (TX)
WebServer      server(80);
Preferences    prefs;

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
FenceState fenceState   = FENCE_UNKNOWN;
FenceState pendingState = FENCE_UNKNOWN;
uint8_t    pendingCount = 0;

uint32_t lastTrackMs  = 0;
double   lastTrackLat = 0, lastTrackLon = 0;
bool     timeSynced   = false;

#if PIN_GPS_PPS >= 0
volatile uint32_t ppsPulses = 0;
void IRAM_ATTR onPpsPulse() { ppsPulses++; }
#endif

// ---------------------------------------------------------------------------
// Utilita'
// ---------------------------------------------------------------------------

// Svuota la seriale del GPS: chiamata anche durante le attese del WiFi, cosi'
// il buffer di ricezione non trabocca mentre siamo fermi ad aspettare.
static void pumpGps() {
  while (gpsSerial.available()) {
    const char c = (char)gpsSerial.read();
    gps.encode(c);
#if GPS_RAW_ECHO
    Serial.write(c);
#endif
  }
}

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

static uint32_t nowEpoch() {
  return timeSynced ? (uint32_t)time(nullptr) : 0;
}

static void isoTimestamp(uint32_t epoch, char *out, size_t len) {
  if (!epoch) { out[0] = 0; return; }
  time_t t = (time_t)epoch;
  struct tm tm_utc;
  gmtime_r(&t, &tm_utc);
  strftime(out, len, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
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
// Riconoscimento del baud rate del modulo
// ---------------------------------------------------------------------------
#if GPS_AUTO_BAUD
static uint32_t detectGpsBaud() {
  static const uint32_t rates[] = { GPS_BAUD, 9600, 38400, 57600, 115200, 19200, 4800 };

  for (uint8_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
    if (i && rates[i] == (uint32_t)GPS_BAUD) continue;   // gia' provato per primo

    gpsSerial.begin(rates[i], SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    const uint32_t before = gps.passedChecksum();
    const uint32_t start  = millis();

    while (millis() - start < 1200) {
      while (gpsSerial.available()) gps.encode(gpsSerial.read());
      if (gps.passedChecksum() > before + 1) {           // due frasi valide: e' lui
        Serial.printf("[GPS] modulo riconosciuto a %lu baud\r\n", (unsigned long)rates[i]);
        return rates[i];
      }
      delay(5);
    }
  }

  Serial.printf("[GPS] nessuna frase valida a nessun baud: resto a %d. "
                "Controlla che il TX del modulo sia sul pin D%d e il GND in comune.\r\n",
                GPS_BAUD, PIN_GPS_RX);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  return GPS_BAUD;
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
// Aggiornamento via WiFi
// ---------------------------------------------------------------------------
#if ENABLE_OTA
static void setupOta() {
  ArduinoOTA.setHostname(HOSTNAME);
  if (strlen(OTA_PASSWORD)) ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println(F("[OTA] aggiornamento in corso: non togliere la corrente"));
    digitalWrite(PIN_LED, HIGH);      // LED fisso: la scheda e' occupata
  });
  ArduinoOTA.onProgress([](unsigned int fatto, unsigned int totale) {
    static uint8_t ultimo = 255;
    const uint8_t pct = totale ? (uint8_t)((uint32_t)fatto * 100 / totale) : 0;
    if (pct % 10 == 0 && pct != ultimo) { ultimo = pct; Serial.printf("[OTA] %u%%\r\n", pct); }
  });
  ArduinoOTA.onEnd([]() { Serial.println(F("[OTA] completato, riavvio")); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[OTA] errore %u\r\n", (unsigned)e); });

  ArduinoOTA.begin();
  Serial.printf("[OTA] pronto: pio run -e ota -t upload (host %s.local)\r\n", HOSTNAME);
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

static void loadSettings() {
  prefs.begin("gpstracker", false);
  // isKey() prima di getDouble(): senza, il primo avvio riempie il log di
  // errori NVS "NOT_FOUND" che non sono errori, e' solo memoria ancora vuota.
  if (prefs.isKey("lat"))    fence.lat    = prefs.getDouble("lat", GEOFENCE_LAT);
  if (prefs.isKey("lon"))    fence.lon    = prefs.getDouble("lon", GEOFENCE_LON);
  if (prefs.isKey("radius")) fence.radius = prefs.getDouble("radius", GEOFENCE_RADIUS_M);
  if (prefs.isKey("odo"))    trip.totalDistanceM = prefs.getDouble("odo", 0);
}

static void updateGeofence() {
  if (!fix.valid) return;

  // L'isteresi non puo' mangiarsi tutto il raggio: con un'area da 25 m e
  // un'isteresi da 25 la soglia interna finirebbe a zero, non si potrebbe mai
  // essere "dentro" e lo stato resterebbe per sempre sconosciuto.
  const double hyst = GEOFENCE_HYSTERESIS_M < fence.radius / 3.0
                    ? (double)GEOFENCE_HYSTERESIS_M : fence.radius / 3.0;

  FenceState observed;
  if (fix.distanceHome > fence.radius + hyst)      observed = FENCE_OUTSIDE;
  else if (fix.distanceHome < fence.radius - hyst) observed = FENCE_INSIDE;
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
// Traccia e statistiche
// ---------------------------------------------------------------------------
static void maybeAddTrackPoint() {
  const uint32_t now = millis();
  if (lastTrackMs && now - lastTrackMs < TRACK_MIN_INTERVAL_MS) return;

  if (lastTrackMs) {
    const double moved = TinyGPSPlus::distanceBetween(fix.lat, fix.lon, lastTrackLat, lastTrackLon);
    if (moved < TRACK_MIN_DISTANCE_M) return;
  }

  TrackPoint p;
  p.lat      = (float)fix.lat;
  p.lon      = (float)fix.lon;
  p.speedKmh = (float)fix.speedKmh;
  p.epoch    = fix.epoch ? fix.epoch : nowEpoch();
  trackAppend(p);

  lastTrackMs  = now;
  lastTrackLat = fix.lat;
  lastTrackLon = fix.lon;
}

static void updateStats() {
  static bool     haveRef   = false;
  static double   refLat    = 0, refLon = 0;
  static uint32_t refMs     = 0;
  static uint32_t movingMs  = 0;

  const uint32_t now = millis();
  if (!haveRef) { haveRef = true; refLat = fix.lat; refLon = fix.lon; refMs = now; return; }

  const double   moved = TinyGPSPlus::distanceBetween(fix.lat, fix.lon, refLat, refLon);
  const uint32_t dt    = now - refMs;
  refLat = fix.lat; refLon = fix.lon; refMs = now;

  // Il filtro sulla velocita' evita che il ballonzolamento del GPS da fermo
  // gonfi i chilometri: sotto i 3 km/h non si accumula niente.
  if (fix.speedKmh >= STATS_MOVING_MIN_KMH && dt < 10000) {
    trip.tripDistanceM  += moved;
    trip.totalDistanceM += moved;
    movingMs            += dt;
    trip.movingTimeS     = movingMs / 1000;
  }
  if (fix.speedKmh > trip.maxSpeedKmh) trip.maxSpeedKmh = (float)fix.speedKmh;
}

static void saveOdometer(bool force) {
  static uint32_t lastSave  = 0;
  static double   lastValue = -1;

  if (!force && millis() - lastSave < STATS_SAVE_MS) return;
  lastSave = millis();
  if (!force && fabs(trip.totalDistanceM - lastValue) < 50) return;   // nulla di nuovo

  lastValue = trip.totalDistanceM;
  prefs.putDouble("odo", trip.totalDistanceM);
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
  pumpGps();

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

    updateStats();
    maybeAddTrackPoint();
    updateGeofence();
    stopsUpdate(fix.lat, fix.lon);   // prima le tappe: la navigazione le usa
    navUpdate(fix.lat, fix.lon, fix.course, fix.speedKmh >= STATS_MOVING_MIN_KMH);
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
  StaticJsonDocument<2048> doc;

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
  doc["distance"]   = fix.distanceHome;
  doc["fenceState"] = fenceState == FENCE_UNKNOWN ? "unknown"
                    : fenceState == FENCE_INSIDE  ? "inside" : "outside";

  JsonObject fenceObj = doc.createNestedObject("fence");
  fenceObj["lat"]    = fence.lat;
  fenceObj["lon"]    = fence.lon;
  fenceObj["radius"] = fence.radius;

  JsonObject tripObj = doc.createNestedObject("trip");
  tripObj["dist"]   = trip.tripDistanceM;
  tripObj["total"]  = trip.totalDistanceM;
  tripObj["max"]    = trip.maxSpeedKmh;
  tripObj["moving"] = trip.movingTimeS;

  JsonObject navObj = doc.createNestedObject("nav");
  navObj["active"]    = nav.active;
  navObj["points"]    = nav.total;
  navObj["target"]    = nav.target;
  navObj["toNext"]    = nav.toNextM;
  navObj["remaining"] = nav.remainingM;
  navObj["rel"]       = nav.relativeDeg;
  navObj["off"]       = nav.offRoute;
  navObj["offDist"]   = nav.offDistM;
  navObj["done"]      = nav.finished;
  navObj["turns"]     = turnCount;
  navObj["turnKind"]  = turnNextKind();
  navObj["turnExit"]  = turnNextExit();
  navObj["turnName"]  = turnNextName();
  navObj["turnDist"]  = turnDistM;
  navObj["stop"]      = stopNextName();
  navObj["stopDist"]  = stopDistM;
  navObj["stopIdx"]   = stopNext;
  navObj["stops"]     = stopsCount;

  JsonObject diag = doc.createNestedObject("diag");
  diag["chars"]     = gps.charsProcessed();
  diag["sentences"] = gps.passedChecksum();
  diag["errors"]    = gps.failedChecksum();
  diag["points"]    = trackPoints;
  diag["backup"]    = trackHasBackup();
#if ENABLE_DISPLAY && ENABLE_TOUCH
  diag["touch"]     = true;
#else
  diag["touch"]     = false;
#endif
  diag["fsfree"]    = trackReady ? (LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024 : 0;
  diag["uptime"]    = millis() / 1000;
  diag["heap"]      = ESP.getFreeHeap();
  diag["rssi"]      = WiFi.RSSI();
  diag["portal"]    = wifiPortalOn;
  diag["ssid"]      = wifiPortalOn ? wifiApName : WiFi.SSID();
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

// Mandare la risposta un pezzetto alla volta e' lento: ogni sendContent e' una
// scrittura TCP a se', e con mille punti il browser va in timeout prima della
// fine. Accumulo in un buffer e spedisco quando e' pieno.
static char   outBuf[1024];
static size_t outLen = 0;

static void outFlush() {
  if (!outLen) return;
  server.sendContent(outBuf, outLen);
  outLen = 0;
}

static void outWrite(const char *txt) {
  const size_t n = strlen(txt);
  if (outLen + n >= sizeof(outBuf)) outFlush();
  if (n >= sizeof(outBuf)) { server.sendContent(txt, n); return; }
  memcpy(outBuf + outLen, txt, n);
  outLen += n;
}

// --- streaming della traccia -----------------------------------------------
// I visitatori scrivono direttamente nella risposta HTTP: la traccia puo'
// essere molto piu' grande della RAM disponibile, quindi non si accumula.
static bool streamNewSegment  = true;
static bool streamFirstSeg    = true;
static bool streamFirstPoint  = true;

// Traccia per la mappa: [[ [lat,lon,km/h,epoch], ... ], ...] un array per viaggio
static void trackJsonVisitor(const TrackPoint &p, bool isBreak) {
  if (isBreak) { streamNewSegment = true; return; }

  if (streamNewSegment) {
    outWrite(streamFirstSeg ? "[" : "],[");
    streamFirstSeg   = false;
    streamNewSegment = false;
    streamFirstPoint = true;
  }

  char buf[80];
  snprintf(buf, sizeof(buf), "%s[%.6f,%.6f,%.1f,%lu]",
           streamFirstPoint ? "" : ",", p.lat, p.lon, p.speedKmh, (unsigned long)p.epoch);
  streamFirstPoint = false;
  outWrite(buf);
}

static void handleTrack() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");

  streamNewSegment = true;
  streamFirstSeg   = true;
  outWrite("[");
  trackForEach(trackJsonVisitor, TRACK_VIEW_POINTS);
  if (!streamFirstSeg) outWrite("]");
  outWrite("]");
  outFlush();
  server.sendContent("");
}

static void gpxVisitor(const TrackPoint &p, bool isBreak) {
  if (isBreak) {
    if (!streamFirstSeg) { outWrite("</trkseg>\n"); streamFirstSeg = true; }
    return;
  }
  if (streamFirstSeg) { outWrite("<trkseg>\n"); streamFirstSeg = false; }

  char buf[160], iso[32];
  isoTimestamp(p.epoch, iso, sizeof(iso));
  if (iso[0]) snprintf(buf, sizeof(buf),
                       "<trkpt lat=\"%.6f\" lon=\"%.6f\"><time>%s</time></trkpt>\n", p.lat, p.lon, iso);
  else        snprintf(buf, sizeof(buf),
                       "<trkpt lat=\"%.6f\" lon=\"%.6f\"/>\n", p.lat, p.lon);
  outWrite(buf);
}

static void sendGpx(const char *path, const char *filename) {
  server.sendHeader("Content-Disposition", String("attachment; filename=\"") + filename + "\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/gpx+xml", "");
  outWrite("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<gpx version=\"1.1\" creator=\"ESP32 GPS Tracker\" "
           "xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
           "<trk><name>ESP32 track</name>\n");
  streamFirstSeg = true;
  trackForEachFile(path, gpxVisitor, 0);
  if (!streamFirstSeg) outWrite("</trkseg>\n");
  outWrite("</trk></gpx>\n");
  outFlush();
  server.sendContent("");
}

static void handleGpx()    { sendGpx(TRACK_PATH, "track.gpx"); }
static void handleGpxOld() { sendGpx(TRACK_PATH_OLD, "track-precedente.gpx"); }

// Il GPX arriva a pezzi e viene letto al volo: sul tracker restano solo i
// punti, non il file.
static void handleRouteUpload() {
  HTTPUpload &up = server.upload();
  if      (up.status == UPLOAD_FILE_START) routeBeginParse();
  else if (up.status == UPLOAD_FILE_WRITE) routeFeed(up.buf, up.currentSize);
  else if (up.status == UPLOAD_FILE_END)   routeEndParse();
}

static void handleRouteDone() {
  char buf[96];
  snprintf(buf, sizeof(buf), "{\"ok\":true,\"points\":%u,\"km\":%.1f}",
           routeCount, routeCount ? routeCum[0] / 1000.0 : 0.0);
  server.send(200, "application/json", buf);
}

static void handleRouteGet() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  outWrite("[");
  char buf[48];
  for (uint16_t i = 0; i < routeCount; i++) {
    snprintf(buf, sizeof(buf), "%s[%.6f,%.6f]", i ? "," : "", routePts[i].lat, routePts[i].lon);
    outWrite(buf);
  }
  outWrite("]");
  outFlush();
  server.sendContent("");
}

// --- tappe ------------------------------------------------------------------
static void jsonEscape(const char *in, char *out, size_t len) {
  size_t j = 0;
  for (size_t i = 0; in[i] && j < len - 2; i++) {
    if (in[i] == '"' || in[i] == '\\') out[j++] = '\\';
    out[j++] = in[i];
  }
  out[j] = 0;
}

static void handleStopsGet() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");

  char buf[160], nome[STOP_NAME_LEN * 2];
  snprintf(buf, sizeof(buf), "{\"next\":%u,\"done\":%s,\"dist\":%.0f,\"list\":[",
           stopNext, stopsDone ? "true" : "false", stopDistM);
  outWrite(buf);

  for (uint8_t i = 0; i < stopsCount; i++) {
    jsonEscape(stops[i].name, nome, sizeof(nome));
    snprintf(buf, sizeof(buf), "%s{\"lat\":%.6f,\"lon\":%.6f,\"name\":\"%s\"}",
             i ? "," : "", stops[i].lat, stops[i].lon, nome);
    outWrite(buf);
  }

  outWrite("]}");
  outFlush();
  server.sendContent("");
}

// Riceve l'elenco completo: la dashboard lo modifica e lo rimanda tutto, cosi'
// non servono mezza dozzina di endpoint per aggiungere, togliere e spostare.
static void handleStopsPost() {
  const String body = server.arg("plain");
  if (!body.length()) { server.send(400, "application/json", "{\"error\":\"corpo vuoto\"}"); return; }

  DynamicJsonDocument doc(6144);
  if (deserializeJson(doc, body)) {
    server.send(400, "application/json", "{\"error\":\"JSON non valido\"}");
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  stopsCount = 0;
  stopNext   = 0;
  stopsDone  = false;
  for (JsonObject o : arr) {
    if (!stopsAdd(o["lat"] | 0.0, o["lon"] | 0.0, o["name"] | "")) break;
  }
  stopsSave();
  if (fix.valid) stopsUpdate(fix.lat, fix.lon);

  Serial.printf("[TAPPE] salvate %u tappe\r\n", stopsCount);
  char out[48];
  snprintf(out, sizeof(out), "{\"ok\":true,\"stops\":%u}", stopsCount);
  server.send(200, "application/json", out);
}

static void handleStopsClear() {
  stopsClear();
  stopsSave();
  server.send(200, "application/json", "{\"ok\":true}");
}

// Riceve il percorso come testo compatto: "lat,lon;lat,lon;...".
// Niente JSON: un percorso stradale e' fatto di migliaia di punti e un
// documento in memoria costerebbe piu' del percorso stesso.
static void handleRouteDraw() {
  const String body = server.arg("plain");
  if (!body.length())        { server.send(400, "application/json", "{\"error\":\"corpo vuoto\"}"); return; }
  if (body.length() > 40000) { server.send(413, "application/json", "{\"error\":\"troppi punti\"}"); return; }

  routeBeginBuild();
  const char *p = body.c_str();
  while (*p) {
    char *end;
    const double lat = strtod(p, &end);
    if (end == p) break;
    p = end;
    while (*p == ',' || *p == ' ') p++;
    const double lon = strtod(p, &end);
    if (end == p) break;
    p = end;
    while (*p == ';' || *p == '\n' || *p == '\r' || *p == ' ') p++;
    routeAddVertex(lat, lon);
  }
  routeEndBuild();

  char out[72];
  snprintf(out, sizeof(out), "{\"ok\":true,\"points\":%u,\"km\":%.1f}",
           routeCount, routeCount ? routeCum[0] / 1000.0 : 0.0);
  server.send(200, "application/json", out);
}

// Le svolte preparate dal telefono, una per riga:
//   lat,lon,tipo,uscita,nome della strada
// Il nome e' l'ultimo campo e arriva fino a fine riga, cosi' le virgole che
// capitano nei nomi delle strade non rompono niente.
static void handleTurnsPost() {
  const String body = server.arg("plain");
  if (!body.length())        { server.send(400, "application/json", "{\"error\":\"corpo vuoto\"}"); return; }
  if (body.length() > 24000) { server.send(413, "application/json", "{\"error\":\"troppe svolte\"}"); return; }

  turnsReset();
  const char *p = body.c_str();
  while (*p) {
    char *end;
    const double lat = strtod(p, &end);
    if (end == p) break;
    p = end; if (*p == ',') p++;
    const double lon = strtod(p, &end);
    if (end == p) break;
    p = end; if (*p == ',') p++;
    const long kind = strtol(p, &end, 10);
    if (end == p) break;
    p = end; if (*p == ',') p++;
    const long exit = strtol(p, &end, 10);
    if (end == p) break;
    p = end; if (*p == ',') p++;

    const char *fine = strchr(p, '\n');
    char nome[TURN_NAME_LEN];
    size_t n = fine ? (size_t)(fine - p) : strlen(p);
    if (n && p[n - 1] == '\r') n--;
    if (n >= sizeof(nome)) n = sizeof(nome) - 1;
    memcpy(nome, p, n);
    nome[n] = '\0';

    turnsAdd(lat, lon, (uint8_t)kind, (uint8_t)exit, nome);
    if (!fine) break;
    p = fine + 1;
  }

  turnsBindToRoute();
  turnsSave();
  Serial.printf("[ROTTA] %u svolte ricevute\r\n", turnCount);

  char out[48];
  snprintf(out, sizeof(out), "{\"ok\":true,\"turns\":%u}", turnCount);
  server.send(200, "application/json", out);
}

static void handleTurnsGet() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");

  char buf[128], nome[TURN_NAME_LEN * 2];
  outWrite("[");
  for (uint16_t i = 0; i < turnCount; i++) {
    jsonEscape(turns[i].name, nome, sizeof(nome));
    snprintf(buf, sizeof(buf), "%s[%.6f,%.6f,%u,%u,\"%s\"]", i ? "," : "",
             turns[i].lat, turns[i].lon, turns[i].kind, turns[i].exit, nome);
    outWrite(buf);
  }
  outWrite("]");
  outFlush();
  server.sendContent("");
}

// Via d'uscita se la taratura del tocco viene male: da qui si butta via e al
// riavvio il display la richiede da capo. Senza questo, un menu che non
// risponde ai tocchi non si sistemerebbe piu' dal telefono.
static void handleTouchReset() {
  Preferences p;
  if (p.begin("touch", false)) { p.clear(); p.end(); }
  server.send(200, "application/json", "{\"ok\":true}");
  delay(200);
  ESP.restart();
}

static void handleRouteClear() {
  routeClear();
  stopsClear();
  server.send(200, "application/json", "{\"ok\":true}");
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
  trackClear();
  lastTrackMs = 0;
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleResetTrip() {
  trip.tripDistanceM = 0;
  trip.maxSpeedKmh   = 0;
  trip.movingTimeS   = 0;
  server.send(200, "application/json", "{\"ok\":true}");
}

static void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/track", HTTP_GET, handleTrack);
  server.on("/api/track/clear", HTTP_POST, handleClearTrack);
  server.on("/api/trip/reset", HTTP_POST, handleResetTrip);
  server.on("/api/fence", HTTP_POST, handleSetFence);
  server.on("/api/route", HTTP_GET, handleRouteGet);
  server.on("/api/route", HTTP_POST, handleRouteDone, handleRouteUpload);
  server.on("/api/route/draw", HTTP_POST, handleRouteDraw);
  server.on("/api/route/steps", HTTP_GET, handleTurnsGet);
  server.on("/api/route/steps", HTTP_POST, handleTurnsPost);
  server.on("/api/touch/reset", HTTP_POST, handleTouchReset);
  server.on("/api/route/clear", HTTP_POST, handleRouteClear);
  server.on("/api/stops", HTTP_GET, handleStopsGet);
  server.on("/api/stops", HTTP_POST, handleStopsPost);
  server.on("/api/stops/clear", HTTP_POST, handleStopsClear);
  server.on("/track.gpx", HTTP_GET, handleGpx);
  server.on("/track-precedente.gpx", HTTP_GET, handleGpxOld);
  wifiRegisterRoutes(server);
  server.onNotFound([]() {
    if (wifiHandleNotFound()) return;   // portale captive attivo
    server.send(404, "text/plain", "404");
  });
  server.begin();
}

// ---------------------------------------------------------------------------
// Riepilogo su monitor seriale
// ---------------------------------------------------------------------------
static void printStatus() {
  static uint32_t lastChars = 0;
  const uint32_t chars  = gps.charsProcessed();
  const uint32_t frasi  = gps.passedChecksum();
  const uint32_t errori = gps.failedChecksum();

  // Caso 1: non e' mai arrivato niente -> il filo non c'e' o il modulo e' spento.
  if (!chars) {
    Serial.printf("[GPS] nessun byte in arrivo: il TX del modulo va sul pin D%d, "
                  "serve il GND in comune e il modulo deve essere alimentato\r\n",
                  PIN_GPS_RX);
    return;
  }

  // Caso 1b: arrivavano e si sono fermati. E' tutta un'altra storia: il
  // cablaggio e' giusto, ma qualcosa lo interrompe.
  if (chars == lastChars) {
    Serial.printf("[GPS] flusso interrotto, fermo a %lu byte: contatto incerto sul filo del TX, "
                  "alimentazione che balla, o modulo che si riavvia\r\n",
                  (unsigned long)chars);
    return;
  }
  lastChars = chars;

  // Caso 2: arrivano byte ma nessuna frase valida -> baud rate sbagliato.
  if (!frasi) {
    Serial.printf("[GPS] %lu byte ricevuti ma nessuna frase NMEA valida (%lu checksum errati): "
                  "baud rate sbagliato? Prova GPS_RAW_ECHO per vedere cosa arriva\r\n",
                  (unsigned long)chars, (unsigned long)errori);
    return;
  }

  // Caso 3: il modulo parla, manca solo l'aggancio dei satelliti.
  if (!fix.valid) {
    Serial.printf("[GPS] modulo OK (%lu frasi valide, %lu errori) - in attesa del fix, "
                  "satelliti in vista: %lu\r\n",
                  (unsigned long)frasi, (unsigned long)errori,
                  (unsigned long)(gps.satellites.isValid() ? gps.satellites.value() : 0));
    return;
  }

  Serial.printf("[GPS] %.6f, %.6f | %.1f km/h | %.0f m slm | sat %lu | HDOP %.1f | %s (%.0f m) | "
                "viaggio %.2f km\r\n",
                fix.lat, fix.lon, fix.speedKmh, fix.altitude,
                (unsigned long)fix.satellites, fix.hdop,
                fenceState == FENCE_OUTSIDE ? "FUORI area" : "dentro area", fix.distanceHome,
                trip.tripDistanceM / 1000.0);
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

#if GPS_AUTO_BAUD
  detectGpsBaud();
#endif

#if GPS_CONFIGURE_UBX
  delay(500);
  configureGps();
#endif

  // Il display e' l'unica cosa che non si vede nel log, e quando non si accende
  // la prima domanda e' sempre "ma il firmware ci sta parlando?". Adesso lo dice.
#if ENABLE_DISPLAY
#if PIN_MENU_BUTTON >= 0
  pinMode(PIN_MENU_BUTTON, INPUT_PULLUP);
#endif
  displayBegin();
  Serial.printf("[DISPLAY] acceso: CS D%d, DC D%d, RESET D%d, SCK D%d, MOSI D%d\r\n",
                TFT_CS, TFT_DC, TFT_RST, TFT_SCLK, TFT_MOSI);
#else
  Serial.println(F("[DISPLAY] spento da config.h: per usarlo metti ENABLE_DISPLAY a 1"));
#endif

  trackBegin();
  routeLoad();
  stopsLoad();
  loadSettings();
  Serial.printf("[GEOFENCE] centro %.6f, %.6f raggio %.0f m\r\n", fence.lat, fence.lon, fence.radius);

  wifiIdleHook = pumpGps;             // il GPS continua a essere letto durante le attese
  wifiBegin();
  setupWebServer();

#if ENABLE_OTA
  setupOta();
#endif

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

#if ENABLE_DISPLAY
  static bool lastDrawForce = false;
  UiAction azione = UI_NONE;

#if PIN_MENU_BUTTON >= 0
  // Il tasto BOOT: una via nel menu che non passa dal vetro del touch.
  {
    static bool     giuPrima = false;
    static uint32_t premutoDa = 0;
    const bool giu = digitalRead(PIN_MENU_BUTTON) == LOW;
    if (giu && !giuPrima) {
      giuPrima  = true;
      premutoDa = millis();
    } else if (!giu && giuPrima) {
      giuPrima = false;
      const uint32_t durata = millis() - premutoDa;
      if (durata > 40) {                    // sotto i 40 ms e' rimbalzo del tasto
#if ENABLE_TOUCH
        if (durata >= BUTTON_CALIB_MS) {
          Serial.println(F("[MENU] tasto tenuto a lungo: ritaro il tocco"));
          touchRunCalibration();
        } else
#endif
        azione = (durata >= BUTTON_LONG_MS) ? uiActivate() : uiNext();
        lastDrawForce = true;
      }
    }
  }
#endif

#if ENABLE_TOUCH
  int16_t tx = 0, ty = 0;
  if (touchPressed(tx, ty)) {
    azione = uiTouch(tx, ty);
    lastDrawForce = true;
  }
#endif

  if (azione != UI_NONE) {
    switch (azione) {
      case UI_ROUTE_CLEAR:
        routeClear();
        Serial.println(F("[MENU] percorso cancellato"));
        break;
      case UI_TRACK_CLEAR:
        trackClear();
        lastTrackMs = 0;
        Serial.println(F("[MENU] traccia cancellata"));
        break;
      case UI_TRIP_RESET:
        trip.tripDistanceM = 0;
        trip.maxSpeedKmh   = 0;
        trip.movingTimeS   = 0;
        Serial.println(F("[MENU] viaggio azzerato"));
        break;
      case UI_PORTAL_ON:
        if (!wifiPortalOn) wifiStartPortal();
        break;
      case UI_PORTAL_OFF:
        if (wifiPortalOn) wifiStopPortal();
        break;
      default:
        break;
    }
  }

  static uint32_t lastDraw = 0;
  if (lastDrawForce || millis() - lastDraw >= DISPLAY_REFRESH_MS) {
    lastDrawForce = false;
    lastDraw = millis();

    // netName vive oltre la chiamata: DisplayData tiene un puntatore, non una copia.
    static String netName, ipText;
    netName = wifiPortalOn ? wifiApName : WiFi.SSID();
    ipText  = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("");

    DisplayData d;
    d.fixValid     = fix.valid;
    d.gpsAlive     = gps.charsProcessed() > 20 && gps.passedChecksum() > 0;
    d.speedKmh     = (float)fix.speedKmh;
    d.cardinal     = TinyGPSPlus::cardinal(fix.course);
    d.satellites   = gps.satellites.isValid() ? gps.satellites.value() : 0;
    d.hdop         = gps.hdop.isValid() ? (float)gps.hdop.hdop() : 0.0f;
    d.altitude     = (float)fix.altitude;
    d.distanceHome = (float)fix.distanceHome;
    d.outside      = (fenceState == FENCE_OUTSIDE);
    d.tripKm       = (float)(trip.tripDistanceM / 1000.0);
    d.maxKmh       = trip.maxSpeedKmh;
    d.movingS      = trip.movingTimeS;
    d.epoch        = fix.epoch ? fix.epoch : nowEpoch();
    d.sentences    = gps.passedChecksum();
    d.netName      = netName.c_str();
    d.ipText       = ipText.c_str();
    d.apName       = wifiApName.c_str();
    d.portal       = wifiPortalOn;
    d.routeTurns   = turnCount;
    d.routeKm      = routeCount ? (float)(routeCum[0] / 1000.0) : 0.0f;
    d.trackPoints  = trackPoints;
    d.navActive    = nav.active;
    d.navOffRoute  = nav.offRoute;
    d.navFinished  = nav.finished;
    d.navRelative  = (float)nav.relativeDeg;
    d.navToNextM   = (float)nav.toNextM;
    d.navRemainM   = (float)nav.remainingM;
    d.navTarget    = nav.target;
    d.navTotal     = nav.total;
    d.navTurnKind  = turnNextKind();
    d.navTurnExit  = turnNextExit();
    d.navTurnName  = turnNextName();
    d.navTurnM     = turnDistM;
    d.navTurn2Kind = turnAfterKind();
    d.navTurn2M    = turnDist2M;
    d.navStopName  = stopNextName();
    d.navStopM     = (float)stopDistM;
    d.navStops     = stopsCount;
    displayUpdate(d);
  }
#endif
  wifiLoop();
  saveOdometer(false);

#if ENABLE_OTA
  ArduinoOTA.handle();
#endif

#if ENABLE_SINRICPRO
  if (WiFi.status() == WL_CONNECTED) SinricPro.handle();
#endif

  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= STATUS_PRINT_MS) {
    lastPrint = millis();
    printStatus();
  }
}
