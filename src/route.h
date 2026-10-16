/*
 * route.h - percorso da seguire e navigazione a freccia.
 *
 * Il percorso lo prepari sul PC (Google Maps, BRouter, Komoot...) e lo esporti
 * in GPX; dalla dashboard lo carichi sul tracker, che ne estrae i punti e li
 * tiene nella memoria interna. Da li' in poi il display mostra la freccia
 * verso il prossimo punto e quanto manca.
 *
 * Non c'e' calcolo del percorso a bordo e non c'e' ricalcolo se sbagli strada:
 * tutto il lavoro pesante l'ha gia' fatto il PC. Se esci dal tracciato il
 * tracker te lo dice, ma la strada per rientrare la scegli tu.
 */
#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <TinyGPS++.h>

#include "config.h"

struct RoutePoint {
  float lat;
  float lon;
};

struct NavState {
  bool     active;        // c'e' un percorso caricato
  bool     offRoute;      // troppo lontano dal tracciato
  bool     finished;      // arrivato all'ultimo punto
  uint16_t target;        // indice del punto verso cui stai andando
  uint16_t total;
  double   toNextM;       // distanza dal prossimo punto
  double   remainingM;    // distanza che resta fino alla fine
  double   bearingDeg;    // direzione assoluta verso il prossimo punto
  double   relativeDeg;   // la stessa, ma rispetto a dove sei rivolto
  double   offDistM;      // quanto sei lontano dal tracciato
};

static const char ROUTE_PATH[] = "/route.bin";

static RoutePoint routePts[ROUTE_MAX_POINTS];
static float      routeCum[ROUTE_MAX_POINTS];   // metri da qui alla fine
static uint16_t   routeCount = 0;
static uint16_t   navIndex   = 0;               // non torna mai indietro

static NavState nav = { false, false, false, 0, 0, 0, 0, 0, 0, 0 };

// ---------------------------------------------------------------------------
// Distanze cumulative: calcolate una volta al caricamento, cosi' il "quanto
// manca" e' una sottrazione invece di 1200 calcoli a ogni aggiornamento.
// ---------------------------------------------------------------------------
static void routeComputeCum() {
  if (!routeCount) return;
  routeCum[routeCount - 1] = 0;
  for (int32_t i = routeCount - 2; i >= 0; i--) {
    const double seg = TinyGPSPlus::distanceBetween(routePts[i].lat, routePts[i].lon,
                                                    routePts[i + 1].lat, routePts[i + 1].lon);
    routeCum[i] = routeCum[i + 1] + (float)seg;
  }
}

static void navReset() {
  navIndex     = 0;
  nav.active   = routeCount > 0;
  nav.offRoute = false;
  nav.finished = false;
  nav.target   = 0;
  nav.total    = routeCount;
}

// ---------------------------------------------------------------------------
// Memoria interna
// ---------------------------------------------------------------------------
static bool routeSave() {
  File f = LittleFS.open(ROUTE_PATH, "w");
  if (!f) return false;
  const size_t bytes = (size_t)routeCount * sizeof(RoutePoint);
  const bool ok = f.write((const uint8_t *)routePts, bytes) == bytes;
  f.close();
  return ok;
}

static void routeLoad() {
  File f = LittleFS.open(ROUTE_PATH, "r");
  if (!f) return;
  const uint32_t n = f.size() / sizeof(RoutePoint);
  routeCount = (uint16_t)(n > ROUTE_MAX_POINTS ? ROUTE_MAX_POINTS : n);
  f.read((uint8_t *)routePts, (size_t)routeCount * sizeof(RoutePoint));
  f.close();

  routeComputeCum();
  navReset();
  if (routeCount) Serial.printf("[ROTTA] percorso caricato: %u punti, %.1f km\r\n",
                                routeCount, routeCum[0] / 1000.0);
}

static void routeClear() {
  routeCount = 0;
  LittleFS.remove(ROUTE_PATH);
  navReset();
  nav.active = false;
  Serial.println(F("[ROTTA] percorso cancellato"));
}

// ---------------------------------------------------------------------------
// Lettura del GPX, fatta mentre il file arriva dalla rete: non viene mai
// salvato per intero, di un file da mezzo megabyte restano solo i punti.
// ---------------------------------------------------------------------------
static char     gpxTag[256];
static uint16_t gpxTagLen  = 0;
static bool     gpxInTag   = false;
static double   gpxLastLat = 0, gpxLastLon = 0;
static bool     gpxHasLast = false;

static double gpxAttr(const char *p) {
  p += 4;                                        // salta "lat=" o "lon="
  while (*p == '"' || *p == '\'' || *p == ' ') p++;
  return atof(p);
}

static void routeAddPoint(double lat, double lon) {
  if (routeCount >= ROUTE_MAX_POINTS) return;
  if (lat == 0 && lon == 0) return;

  // Un punto ogni ROUTE_MIN_SPACING_M: i GPX di Komoot o Google hanno un punto
  // ogni paio di metri, e ne verrebbero decine di migliaia senza guadagno.
  if (gpxHasLast && TinyGPSPlus::distanceBetween(lat, lon, gpxLastLat, gpxLastLon) < ROUTE_MIN_SPACING_M)
    return;

  routePts[routeCount].lat = (float)lat;
  routePts[routeCount].lon = (float)lon;
  routeCount++;
  gpxLastLat = lat;
  gpxLastLon = lon;
  gpxHasLast = true;
}

static void routeParseTag(const char *tag) {
  if (strncmp(tag, "trkpt", 5) != 0 &&
      strncmp(tag, "rtept", 5) != 0 &&
      strncmp(tag, "wpt", 3)   != 0) return;

  const char *latp = strstr(tag, "lat=");
  const char *lonp = strstr(tag, "lon=");
  if (!latp || !lonp) return;
  routeAddPoint(gpxAttr(latp), gpxAttr(lonp));
}

static void routeBeginParse() {
  routeCount = 0;
  gpxTagLen  = 0;
  gpxInTag   = false;
  gpxHasLast = false;
  nav.active = false;
}

static void routeFeed(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const char c = (char)data[i];
    if (c == '<') { gpxInTag = true; gpxTagLen = 0; continue; }
    if (!gpxInTag) continue;
    if (c == '>') {
      gpxInTag = false;
      gpxTag[gpxTagLen] = 0;
      routeParseTag(gpxTag);
      continue;
    }
    if (gpxTagLen < sizeof(gpxTag) - 1) gpxTag[gpxTagLen++] = c;
  }
}

static void routeEndParse() {
  routeComputeCum();
  navReset();
  routeSave();
  Serial.printf("[ROTTA] caricati %u punti, %.1f km\r\n",
                routeCount, routeCount ? routeCum[0] / 1000.0 : 0.0);
}

// ---------------------------------------------------------------------------
// Navigazione: dove sono sul percorso, dove devo andare, quanto manca
// ---------------------------------------------------------------------------
static void navUpdate(double lat, double lon, double courseDeg, bool courseValid) {
  if (!routeCount) { nav.active = false; return; }
  nav.active = true;
  nav.total  = routeCount;

  // Cerco il punto piu' vicino solo in avanti: cosi' un percorso che passa due
  // volte dalla stessa strada non mi rimanda indietro, e se salto un punto la
  // navigazione prosegue invece di incaponirsi.
  uint16_t best     = navIndex;
  double   bestDist = 1e12;
  const uint16_t last = (uint16_t)min<uint32_t>(navIndex + ROUTE_LOOKAHEAD, routeCount - 1);
  for (uint16_t i = navIndex; i <= last; i++) {
    const double d = TinyGPSPlus::distanceBetween(lat, lon, routePts[i].lat, routePts[i].lon);
    if (d < bestDist) { bestDist = d; best = i; }
  }
  navIndex = best;

  // Il punto da raggiungere e' quello dopo il piu' vicino, a meno che tu non
  // sia ancora lontano da quest'ultimo.
  uint16_t target = best;
  if (bestDist < ROUTE_ARRIVAL_M && best < routeCount - 1) target = best + 1;

  nav.target     = target;
  nav.offDistM   = bestDist;
  nav.offRoute   = bestDist > ROUTE_OFF_ROUTE_M;
  nav.toNextM    = TinyGPSPlus::distanceBetween(lat, lon, routePts[target].lat, routePts[target].lon);
  nav.remainingM = nav.toNextM + routeCum[target];
  nav.bearingDeg = TinyGPSPlus::courseTo(lat, lon, routePts[target].lat, routePts[target].lon);

  // La freccia e' relativa a dove sei rivolto, ma la direzione di marcia del
  // GPS ha senso solo in movimento: da fermo punta al nord.
  double rel = nav.bearingDeg - (courseValid ? courseDeg : 0.0);
  while (rel < -180) rel += 360;
  while (rel >= 180) rel -= 360;
  nav.relativeDeg = rel;

  nav.finished = (target == routeCount - 1) && (nav.toNextM < ROUTE_FINISH_M);
}
