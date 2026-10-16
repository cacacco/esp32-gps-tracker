/*
 * track_store.h - traccia e statistiche su memoria interna (LittleFS).
 *
 * I punti finiscono in un file binario a record fissi di 16 byte, aggiunti in
 * coda: sopravvivono al riavvio e a un'interruzione di corrente. Ogni
 * accensione apre un nuovo segmento, cosi' il GPX distingue i singoli viaggi.
 *
 * Un record con lat, lon ed epoch tutti a zero e' il marcatore di inizio
 * segmento, non una posizione.
 */
#pragma once

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

#include "config.h"

struct TrackPoint {
  float    lat;
  float    lon;
  float    speedKmh;
  uint32_t epoch;
};

struct TripStats {
  double   tripDistanceM;    // distanza di questa accensione
  double   totalDistanceM;   // odometro, salvato in memoria non volatile
  float    maxSpeedKmh;
  uint32_t movingTimeS;
};

// Chiamata per ogni record letto; segmentBreak = inizio di un nuovo viaggio.
typedef void (*TrackVisitor)(const TrackPoint &point, bool segmentBreak);

static const char TRACK_PATH[]     = "/track.bin";
static const char TRACK_PATH_OLD[] = "/track.old.bin";

static TripStats trip = { 0, 0, 0, 0 };

static bool     trackReady    = false;
static bool     trackBackup   = false;   // esiste il file ruotato?
static bool     trackPending  = false;   // marcatore di segmento da scrivere
static uint32_t trackRecords  = 0;       // record nel file, marcatori inclusi
static uint32_t trackPoints   = 0;       // posizioni vere

static bool trackIsBreak(const TrackPoint &p) {
  return p.epoch == 0 && p.lat == 0.0f && p.lon == 0.0f;
}

// Conta i record all'avvio, leggendo a blocchi per non fare 12000 letture.
static void trackScan() {
  trackRecords = 0;
  trackPoints  = 0;

  File f = LittleFS.open(TRACK_PATH, "r");
  if (!f) return;

  TrackPoint buf[32];
  size_t read;
  while ((read = f.read((uint8_t *)buf, sizeof(buf))) >= sizeof(TrackPoint)) {
    const size_t n = read / sizeof(TrackPoint);
    for (size_t i = 0; i < n; i++) {
      trackRecords++;
      if (!trackIsBreak(buf[i])) trackPoints++;
    }
  }
  f.close();
}

static bool trackBegin() {
  if (!LittleFS.begin(true)) {          // true: formatta se la partizione e' vergine
    Serial.println(F("[FS] LittleFS non montabile: traccia solo in volatile"));
    return false;
  }
  trackReady   = true;
  trackPending = true;                  // il primo punto aprira' un nuovo segmento
  trackBackup  = LittleFS.exists(TRACK_PATH_OLD);
  trackScan();
  Serial.printf("[FS] traccia: %lu punti gia' salvati (%lu KB liberi)\r\n",
                (unsigned long)trackPoints,
                (unsigned long)((LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024));
  return true;
}

static bool trackWrite(const TrackPoint &p) {
  File f = LittleFS.open(TRACK_PATH, "a");
  if (!f) return false;
  const bool ok = f.write((const uint8_t *)&p, sizeof(p)) == sizeof(p);
  f.close();
  if (!ok) return false;
  trackRecords++;
  if (!trackIsBreak(p)) trackPoints++;
  return true;
}

// File pieno: quello corrente diventa il backup e se ne apre uno nuovo.
static void trackRotate() {
  LittleFS.remove(TRACK_PATH_OLD);
  LittleFS.rename(TRACK_PATH, TRACK_PATH_OLD);
  trackBackup  = true;
  trackRecords = 0;
  trackPoints  = 0;
  trackPending = false;
  Serial.println(F("[FS] file traccia pieno: ruotato in /track.old.bin"));
}

static bool trackAppend(const TrackPoint &p) {
  if (!trackReady) return false;
  if (trackRecords >= TRACK_FILE_MAX_POINTS) trackRotate();

  if (trackPending) {
    trackPending = false;
    if (trackRecords) {                 // nessun marcatore in testa a un file vuoto
      const TrackPoint brk = { 0, 0, 0, 0 };
      trackWrite(brk);
    }
  }
  return trackWrite(p);
}

// lastN = 0 legge tutto, altrimenti solo gli ultimi N record.
static void trackForEachFile(const char *path, TrackVisitor visit, uint32_t lastN) {
  if (!trackReady || !visit) return;

  File f = LittleFS.open(path, "r");
  if (!f) return;

  const uint32_t records = f.size() / sizeof(TrackPoint);
  if (lastN && records > lastN) f.seek((records - lastN) * sizeof(TrackPoint));

  TrackPoint p;
  while (f.read((uint8_t *)&p, sizeof(p)) == sizeof(p)) visit(p, trackIsBreak(p));
  f.close();
}

static void trackForEach(TrackVisitor visit, uint32_t lastN) {
  trackForEachFile(TRACK_PATH, visit, lastN);
}

// Tenuto in RAM: la dashboard chiede lo stato ogni due secondi, e un
// LittleFS.exists() su un file che non c'e' stampa un errore nel log ogni volta.
static bool trackHasBackup() {
  return trackBackup;
}

static void trackClear() {
  if (!trackReady) return;
  LittleFS.remove(TRACK_PATH);
  LittleFS.remove(TRACK_PATH_OLD);
  trackBackup  = false;
  trackRecords = 0;
  trackPoints  = 0;
  trackPending = true;
  Serial.println(F("[FS] traccia azzerata"));
}
