#pragma once
// ---------------------------------------------------------------------------
// Svolte del percorso.
//
// La ESP32 non calcola nessun itinerario: le svolte gliele prepara il telefono
// quando disegni il percorso, chiedendole a un servizio di calcolo strade
// mentre e' ancora online. A bordo resta solo l'essenziale - dove si svolta,
// come si svolta e su che strada si prosegue - cosi' in auto, senza rete,
// resta tutto in memoria.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"

enum TurnKind : uint8_t {
  TURN_NONE = 0,
  TURN_STRAIGHT,
  TURN_LEFT,
  TURN_RIGHT,
  TURN_SLIGHT_LEFT,
  TURN_SLIGHT_RIGHT,
  TURN_SHARP_LEFT,
  TURN_SHARP_RIGHT,
  TURN_UTURN,
  TURN_ROUNDABOUT,
  TURN_ARRIVE,
  TURN_KINDS
};

struct Turn {
  float    lat, lon;
  uint16_t idx;                  // punto del percorso piu' vicino, legato dopo
  uint8_t  kind;
  uint8_t  exit;                 // uscita della rotonda, 0 se non serve
  char     name[TURN_NAME_LEN];  // strada su cui si prosegue
};

static const char TURNS_PATH[] = "/turns.bin";

static Turn     turns[TURNS_MAX];
static uint16_t turnCount = 0;
static uint16_t turnNext  = 0;   // manovra verso cui stai andando
static float    turnDistM = 0;   // quanto manca, lungo il percorso
static float    turnDist2M = 0;  // e quanto manca a quella dopo ancora

// Come si legge la manovra ad alta voce. Corto: deve stare su una riga del
// display insieme al nome della strada.
static const char *turnWord(uint8_t kind) {
  switch (kind) {
    case TURN_STRAIGHT:     return "dritto";
    case TURN_LEFT:         return "a sinistra";
    case TURN_RIGHT:        return "a destra";
    case TURN_SLIGHT_LEFT:  return "leggera sx";
    case TURN_SLIGHT_RIGHT: return "leggera dx";
    case TURN_SHARP_LEFT:   return "secca sx";
    case TURN_SHARP_RIGHT:  return "secca dx";
    case TURN_UTURN:        return "inversione";
    case TURN_ROUNDABOUT:   return "rotonda";
    case TURN_ARRIVE:       return "arrivo";
    default:                return "";
  }
}

// Di quanto gira la freccia sul display per quella manovra.
static float turnAngle(uint8_t kind) {
  switch (kind) {
    case TURN_LEFT:         return  -90;
    case TURN_RIGHT:        return   90;
    case TURN_SLIGHT_LEFT:  return  -40;
    case TURN_SLIGHT_RIGHT: return   40;
    case TURN_SHARP_LEFT:   return -135;
    case TURN_SHARP_RIGHT:  return  135;
    case TURN_UTURN:        return  180;
    case TURN_ROUNDABOUT:   return   90;   // la rotonda si prende a destra
    default:                return    0;
  }
}

static void turnsReset() {
  turnCount = 0;
  turnNext  = 0;
  turnDistM = 0;
}

static void turnsClear() {
  turnsReset();
  LittleFS.remove(TURNS_PATH);
}

static bool turnsAdd(double lat, double lon, uint8_t kind, uint8_t exit, const char *name) {
  if (turnCount >= TURNS_MAX) return false;
  if (kind == TURN_NONE || kind >= TURN_KINDS) return false;
  Turn &t = turns[turnCount];
  t.lat  = (float)lat;
  t.lon  = (float)lon;
  t.idx  = 0;
  t.kind = kind;
  t.exit = exit;
  snprintf(t.name, sizeof(t.name), "%s", name ? name : "");
  turnCount++;
  return true;
}

static bool turnsSave() {
  if (!turnCount) { LittleFS.remove(TURNS_PATH); return true; }
  File f = LittleFS.open(TURNS_PATH, "w");
  if (!f) return false;
  const size_t bytes = (size_t)turnCount * sizeof(Turn);
  const bool ok = f.write((const uint8_t *)turns, bytes) == bytes;
  f.close();
  return ok;
}

// Carica solo i byte: legare le manovre al percorso tocca a route.h, che e'
// l'unico che sa dove passa il percorso.
static void turnsLoadRaw() {
  turnsReset();
  // Senza questo controllo, al primo avvio dopo l'aggiornamento il log si apre
  // con un errore rosso del filesystem per un file che e' normale non esista.
  if (!LittleFS.exists(TURNS_PATH)) return;
  File f = LittleFS.open(TURNS_PATH, "r");
  if (!f) return;
  const uint32_t n = f.size() / sizeof(Turn);
  turnCount = (uint16_t)(n > TURNS_MAX ? TURNS_MAX : n);
  f.read((uint8_t *)turns, (size_t)turnCount * sizeof(Turn));
  f.close();
  for (uint16_t i = 0; i < turnCount; i++) turns[i].name[TURN_NAME_LEN - 1] = '\0';
}

static const char *turnNextName() {
  return (turnCount && turnNext < turnCount) ? turns[turnNext].name : "";
}

static uint8_t turnNextKind() {
  return (turnCount && turnNext < turnCount) ? turns[turnNext].kind : (uint8_t)TURN_NONE;
}

static uint8_t turnNextExit() {
  return (turnCount && turnNext < turnCount) ? turns[turnNext].exit : 0;
}

// La manovra dopo quella in arrivo: sul display sta nel riquadro "poi",
// come sui navigatori da auto.
static uint8_t turnAfterKind() {
  return (turnCount && turnNext + 1 < turnCount) ? turns[turnNext + 1].kind : (uint8_t)TURN_NONE;
}

static const char *turnAfterName() {
  return (turnCount && turnNext + 1 < turnCount) ? turns[turnNext + 1].name : "";
}
