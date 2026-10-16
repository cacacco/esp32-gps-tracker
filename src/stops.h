/*
 * stops.h - le tappe del percorso: posizione piu' un nome.
 *
 * Arrivano dai <wpt> e <rtept> del GPX caricato, oppure le aggiungi tu dalla
 * dashboard - toccando la mappa o salvando la posizione attuale. Restano nella
 * memoria interna e si modificano dal telefono senza ricompilare niente.
 *
 * Sono una cosa diversa dai punti del percorso: quelli sono migliaia e servono
 * a disegnare la strada, queste sono poche e servono a sapere dove stai
 * andando - "Ponte sul Foglia" dice molto piu' di "punto 47 di 340".
 */
#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <TinyGPS++.h>

#include "config.h"

struct Stop {
  float lat;
  float lon;
  char  name[STOP_NAME_LEN];
};

static const char STOPS_PATH[] = "/stops.bin";

static Stop     stops[STOPS_MAX];
static uint8_t  stopsCount = 0;
static uint8_t  stopNext   = 0;     // tappa verso cui stai andando
static double   stopDistM  = 0;     // quanto manca
static bool     stopsDone  = false; // tutte raggiunte

static void stopsSave() {
  File f = LittleFS.open(STOPS_PATH, "w");
  if (!f) return;
  f.write((const uint8_t *)stops, (size_t)stopsCount * sizeof(Stop));
  f.close();
}

static void stopsLoad() {
  File f = LittleFS.open(STOPS_PATH, "r");
  if (!f) return;
  const uint32_t n = f.size() / sizeof(Stop);
  stopsCount = (uint8_t)(n > STOPS_MAX ? STOPS_MAX : n);
  f.read((uint8_t *)stops, (size_t)stopsCount * sizeof(Stop));
  f.close();
  stopNext  = 0;
  stopsDone = false;
  if (stopsCount) Serial.printf("[TAPPE] %u tappe in memoria\r\n", stopsCount);
}

static void stopsClear() {
  stopsCount = 0;
  stopNext   = 0;
  stopsDone  = false;
  LittleFS.remove(STOPS_PATH);
}

static bool stopsAdd(double lat, double lon, const char *name) {
  if (stopsCount >= STOPS_MAX) return false;
  stops[stopsCount].lat = (float)lat;
  stops[stopsCount].lon = (float)lon;
  if (name && *name) snprintf(stops[stopsCount].name, STOP_NAME_LEN, "%s", name);
  else               snprintf(stops[stopsCount].name, STOP_NAME_LEN, "Tappa %u", stopsCount + 1);
  stopsCount++;
  return true;
}

// Avanza quando sei abbastanza vicino. Le tappe si seguono in ordine: se ne
// salti una, quella dopo diventa comunque il traguardo appena ti ci avvicini.
static void stopsUpdate(double lat, double lon) {
  if (!stopsCount) { stopDistM = 0; stopsDone = false; return; }

  while (stopNext < stopsCount) {
    const double d = TinyGPSPlus::distanceBetween(lat, lon, stops[stopNext].lat, stops[stopNext].lon);
    if (d > STOP_ARRIVAL_M) { stopDistM = d; stopsDone = false; return; }
    Serial.printf("[TAPPE] raggiunta: %s\r\n", stops[stopNext].name);
    stopNext++;
  }

  stopDistM = 0;
  stopsDone = true;
}

static const char *stopNextName() {
  if (stopsDone)              return "arrivato";
  if (stopNext < stopsCount)  return stops[stopNext].name;
  return "--";
}
