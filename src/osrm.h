#pragma once
// ---------------------------------------------------------------------------
// Lettura della risposta del servizio strade, mentre arriva.
//
// La risposta di un percorso lungo e' centinaia di kilobyte: non entra in
// memoria e non avrebbe senso salvarla. Qui viene letta un carattere per
// volta, e di tutto quel testo restano solo i punti del percorso e le manovre.
//
// Non e' un lettore JSON completo. Tiene il conto di dove si trova - quale
// livello, dentro quale chiave - ma memorizza solo i campi che servono. La
// geometria dei singoli passi, che e' la parte piu' grossa della risposta,
// viene buttata via mentre scorre invece di essere accumulata: e' per questo
// che un percorso da 300 km si legge con un paio di centinaia di byte di RAM.
//
// La linea del percorso arriva codificata (una lettera ogni cinque bit di
// differenza dal punto precedente) e viene decodificata punto per punto:
// ogni coppia completata esce subito e il testo da cui viene e' gia' scordato.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include "config.h"
#include "turns.h"

#define OSRM_DEPTH 16      // livelli di annidamento seguiti
#define OSRM_KEY   12      // "geometry", "maneuver", "location" ci stanno
#define OSRM_VAL   24      // valori di testo tenuti: nomi, tipo, modificatore

typedef void (*OsrmVertexFn)(double lat, double lon);
typedef void (*OsrmTurnFn)(double lat, double lon, uint8_t kind, uint8_t exit,
                           uint8_t flags, const char *name);

enum OsrmCtx : uint8_t {
  OC_ROOT = 0, OC_ROUTES, OC_ROUTE, OC_LEGS, OC_LEG, OC_STEPS, OC_STEP,
  OC_MANEUVER, OC_LOCATION, OC_SKIP
};
enum OsrmSink : uint8_t { SK_NONE = 0, SK_POLY, SK_NAME, SK_TYPE, SK_MOD, SK_CODE };

static OsrmVertexFn osrmVertexFn = nullptr;
static OsrmTurnFn   osrmTurnFn   = nullptr;

static uint8_t  osrmDepth;
static uint8_t  osrmLost;                      // graffe aperte oltre OSRM_DEPTH
static uint8_t  osrmCtx[OSRM_DEPTH];
static bool     osrmIsObj[OSRM_DEPTH];
static bool     osrmWantKey[OSRM_DEPTH];
static uint16_t osrmIdx[OSRM_DEPTH];
static char     osrmKey[OSRM_DEPTH][OSRM_KEY];

static bool     osrmInStr, osrmEsc, osrmInNum, osrmKeyMode;
static uint8_t  osrmUni;                       // cifre di \uXXXX ancora da saltare
static uint8_t  osrmSink;
static char     osrmVal[OSRM_VAL];
static uint8_t  osrmValLen;
static char     osrmNum[24];
static uint8_t  osrmNumLen;

static int32_t  polyLat, polyLon;              // in centomillesimi di grado
static uint32_t polyAcc;
static uint8_t  polyShift;
static bool     polyLatDone;

static char     osrmStepName[TURN_NAME_LEN];   // passo in corso
static char     osrmManType[OSRM_VAL], osrmManMod[OSRM_VAL];
static uint8_t  osrmManExit;
static double   osrmManLat, osrmManLon;
static bool     osrmManHas;

static char     osrmPrevName[TURN_NAME_LEN];   // strada da cui si arriva
static bool     osrmArriveHas;                 // di arrivi ce n'e' uno per tappa
static double   osrmArriveLat, osrmArriveLon;
static char     osrmArriveName[TURN_NAME_LEN];

static bool     osrmCodeOk;
static uint16_t osrmTurnsOut;
static uint32_t osrmVertsOut;

// Copia tagliando: i nomi piu' lunghi del campo si accorciano, non si
// perdono. snprintf qui farebbe la stessa cosa ma con un avviso a ogni
// compilazione, perche' il compilatore non sa che il taglio e' voluto.
static void osrmCopy(char *dst, size_t size, const char *src) {
  size_t n = strlen(src);
  if (n > size - 1) n = size - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

// ---------------------------------------------------------------------------
// La linea codificata
// ---------------------------------------------------------------------------
static void polyReset() {
  polyLat = polyLon = 0; polyAcc = 0; polyShift = 0; polyLatDone = false;
}

static void polyChar(char c) {
  const int b = (int)(unsigned char)c - 63;
  if (b < 0) { polyAcc = 0; polyShift = 0; return; }
  if (polyShift > 30) { polyAcc = 0; polyShift = 0; return; }   // testo rovinato
  polyAcc |= (uint32_t)(b & 0x1f) << polyShift;
  polyShift += 5;
  if (b & 0x20) return;                                          // continua
  const int32_t d = (polyAcc & 1) ? (int32_t)~(polyAcc >> 1) : (int32_t)(polyAcc >> 1);
  polyAcc = 0; polyShift = 0;
  if (!polyLatDone) { polyLat += d; polyLatDone = true; return; }
  polyLon += d; polyLatDone = false;
  osrmVertsOut++;
  if (osrmVertexFn) osrmVertexFn(polyLat / 1e5, polyLon / 1e5);
}

// ---------------------------------------------------------------------------
// Dove siamo dentro la risposta
// ---------------------------------------------------------------------------
static uint8_t osrmChildCtx(uint8_t pCtx, const char *pKey, bool obj, uint16_t pIdx) {
  if (pCtx == OC_SKIP) return OC_SKIP;
  if (obj) {
    switch (pCtx) {
      case OC_ROUTES: return pIdx == 0 ? OC_ROUTE : OC_SKIP;   // solo il primo
      case OC_LEGS:   return OC_LEG;
      case OC_STEPS:  return OC_STEP;
      case OC_STEP:   return strcmp(pKey, "maneuver") == 0 ? OC_MANEUVER : OC_SKIP;
      default:        return OC_SKIP;
    }
  }
  if (pCtx == OC_ROOT     && strcmp(pKey, "routes")   == 0) return OC_ROUTES;
  if (pCtx == OC_ROUTE    && strcmp(pKey, "legs")     == 0) return OC_LEGS;
  if (pCtx == OC_LEG      && strcmp(pKey, "steps")    == 0) return OC_STEPS;
  if (pCtx == OC_MANEUVER && strcmp(pKey, "location") == 0) return OC_LOCATION;
  return OC_SKIP;
}

static uint8_t osrmSinkFor(uint8_t ctx, const char *key) {
  if (ctx == OC_ROUTE    && strcmp(key, "geometry") == 0) return SK_POLY;
  if (ctx == OC_STEP     && strcmp(key, "name")     == 0) return SK_NAME;
  if (ctx == OC_MANEUVER && strcmp(key, "type")     == 0) return SK_TYPE;
  if (ctx == OC_MANEUVER && strcmp(key, "modifier") == 0) return SK_MOD;
  if (ctx == OC_ROOT     && strcmp(key, "code")     == 0) return SK_CODE;
  return SK_NONE;
}

// Le stesse regole che usa il telefono quando prepara il percorso: le due
// strade devono dare le stesse manovre, altrimenti il ricalcolo cambierebbe
// le indicazioni sotto il naso a chi guida.
static uint8_t osrmKind(const char *type, const char *mod) {
  if (strcmp(type, "arrive") == 0)          return TURN_ARRIVE;
  if (strcmp(type, "roundabout") == 0 ||
      strcmp(type, "rotary") == 0 ||
      strcmp(type, "roundabout turn") == 0) return TURN_ROUNDABOUT;
  if (strcmp(mod, "left")         == 0) return TURN_LEFT;
  if (strcmp(mod, "right")        == 0) return TURN_RIGHT;
  if (strcmp(mod, "slight left")  == 0) return TURN_SLIGHT_LEFT;
  if (strcmp(mod, "slight right") == 0) return TURN_SLIGHT_RIGHT;
  if (strcmp(mod, "sharp left")   == 0) return TURN_SHARP_LEFT;
  if (strcmp(mod, "sharp right")  == 0) return TURN_SHARP_RIGHT;
  if (strcmp(mod, "uturn")        == 0) return TURN_UTURN;
  return TURN_STRAIGHT;
}

static bool osrmUseful(const char *type, const char *mod) {
  if (strcmp(type, "depart") == 0 || strcmp(type, "notification") == 0) return false;
  if ((strcmp(type, "continue") == 0 || strcmp(type, "new name") == 0) &&
      (!*mod || strcmp(mod, "straight") == 0)) return false;
  return true;
}

static void osrmStepEnd() {
  if (osrmManHas) {
    const uint8_t kind = osrmKind(osrmManType, osrmManMod);
    if (kind == TURN_ARRIVE) {
      osrmArriveHas = true;
      osrmArriveLat = osrmManLat;
      osrmArriveLon = osrmManLon;
      osrmCopy(osrmArriveName, sizeof(osrmArriveName), osrmStepName);
    } else if (osrmUseful(osrmManType, osrmManMod)) {
      uint8_t flags = 0;
      // la strada cambia nome: e' una svolta vera, non una curva
      if (osrmStepName[0] && strcmp(osrmStepName, osrmPrevName) != 0) flags |= TURN_F_NEWROAD;
      if (kind == TURN_ROUNDABOUT) {
        if (strstr(osrmManMod, "left"))  flags |= TURN_F_EXIT_L;
        if (strstr(osrmManMod, "right")) flags |= TURN_F_EXIT_R;
      }
      osrmTurnsOut++;
      if (osrmTurnFn) osrmTurnFn(osrmManLat, osrmManLon, kind, osrmManExit, flags, osrmStepName);
    }
  }
  // il confronto vale anche per i passi scartati: la strada precedente e'
  // quella del passo prima, che sia stato mostrato o no
  osrmCopy(osrmPrevName, sizeof(osrmPrevName), osrmStepName);
  osrmStepName[0] = 0; osrmManType[0] = 0; osrmManMod[0] = 0;
  osrmManExit = 0; osrmManHas = false;
}

static void osrmPush(bool obj) {
  uint8_t ctx;
  if (osrmDepth == 0 && obj) ctx = OC_ROOT;        // l'oggetto piu' esterno
  else ctx = osrmChildCtx(osrmCtx[osrmDepth], osrmKey[osrmDepth], obj, osrmIdx[osrmDepth]);

  if (osrmDepth + 1 >= OSRM_DEPTH) { osrmLost++; return; }
  osrmDepth++;
  osrmCtx[osrmDepth]     = ctx;
  osrmIsObj[osrmDepth]   = obj;
  osrmWantKey[osrmDepth] = obj;
  osrmIdx[osrmDepth]     = 0;
  osrmKey[osrmDepth][0]  = 0;
  if (ctx == OC_STEP) {
    osrmStepName[0] = 0; osrmManType[0] = 0; osrmManMod[0] = 0;
    osrmManExit = 0; osrmManHas = false;
  }
}

static void osrmPop() {
  if (osrmLost) { osrmLost--; return; }
  if (!osrmDepth) return;
  if (osrmCtx[osrmDepth] == OC_STEP) osrmStepEnd();
  osrmDepth--;
  osrmKey[osrmDepth][0] = 0;
}

static void osrmNumEnd() {
  if (!osrmInNum) return;
  osrmInNum = false;
  osrmNum[osrmNumLen] = 0;
  const uint8_t d = osrmDepth;
  if (osrmCtx[d] == OC_LOCATION) {
    const double v = atof(osrmNum);
    if (osrmIdx[d] == 0)      osrmManLon = v;      // il servizio da' prima la longitudine
    else if (osrmIdx[d] == 1) osrmManLat = v;
  } else if (osrmCtx[d] == OC_MANEUVER && strcmp(osrmKey[d], "exit") == 0) {
    const long v = strtol(osrmNum, nullptr, 10);
    osrmManExit = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
  }
  osrmNumLen = 0;
}

static void osrmStrChar(char c) {
  if (!osrmKeyMode && osrmSink == SK_POLY) { polyChar(c); return; }
  if (osrmValLen + 1 < OSRM_VAL) { osrmVal[osrmValLen++] = c; osrmVal[osrmValLen] = 0; }
}

static void osrmStrEnd() {
  osrmInStr = false;
  const uint8_t d = osrmDepth;
  if (osrmKeyMode) {
    osrmCopy(osrmKey[d], OSRM_KEY, osrmVal);
    osrmWantKey[d] = false;
    return;
  }
  switch (osrmSink) {
    case SK_NAME: osrmCopy(osrmStepName, sizeof(osrmStepName), osrmVal); break;
    case SK_TYPE: osrmCopy(osrmManType, sizeof(osrmManType), osrmVal); osrmManHas = true; break;
    case SK_MOD:  osrmCopy(osrmManMod,  sizeof(osrmManMod),  osrmVal); break;
    case SK_CODE: osrmCodeOk = (strcmp(osrmVal, "Ok") == 0); break;
    default: break;
  }
  osrmSink = SK_NONE;
}

static void osrmStrStart() {
  osrmInStr = true; osrmEsc = false; osrmUni = 0;
  osrmValLen = 0; osrmVal[0] = 0;
  const uint8_t d = osrmDepth;
  osrmKeyMode = osrmIsObj[d] && osrmWantKey[d];
  osrmSink    = osrmKeyMode ? (uint8_t)SK_NONE : osrmSinkFor(osrmCtx[d], osrmKey[d]);
  if (osrmSink == SK_POLY) polyReset();
}

// ---------------------------------------------------------------------------
// Ingresso
// ---------------------------------------------------------------------------
static void osrmBegin(OsrmVertexFn v, OsrmTurnFn t) {
  osrmVertexFn = v; osrmTurnFn = t;
  osrmDepth = 0; osrmLost = 0;
  osrmCtx[0] = OC_ROOT; osrmIsObj[0] = false; osrmWantKey[0] = false;
  osrmIdx[0] = 0; osrmKey[0][0] = 0;
  osrmInStr = osrmEsc = osrmInNum = osrmKeyMode = false;
  osrmUni = 0; osrmSink = SK_NONE; osrmValLen = 0; osrmNumLen = 0;
  osrmVal[0] = 0;
  polyReset();
  osrmStepName[0] = 0; osrmManType[0] = 0; osrmManMod[0] = 0;
  osrmManExit = 0; osrmManLat = osrmManLon = 0; osrmManHas = false;
  osrmPrevName[0] = 0;
  osrmArriveHas = false; osrmArriveName[0] = 0;
  osrmArriveLat = osrmArriveLon = 0;
  osrmCodeOk = false; osrmTurnsOut = 0; osrmVertsOut = 0;
}

static void osrmFeed(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const char c = (char)data[i];
    if (osrmInStr) {
      if (osrmUni)        { osrmUni--; if (!osrmUni) osrmStrChar('?'); continue; }
      if (osrmEsc) {
        osrmEsc = false;
        switch (c) {
          case 'u': osrmUni = 4; break;
          case 'n': case 'r': case 't': case 'b': case 'f': osrmStrChar(' '); break;
          default:  osrmStrChar(c); break;
        }
        continue;
      }
      if (c == '\\') { osrmEsc = true; continue; }
      if (c == '"')  { osrmStrEnd();   continue; }
      osrmStrChar(c);
      continue;
    }
    switch (c) {
      case '{': osrmNumEnd(); osrmPush(true);  break;
      case '[': osrmNumEnd(); osrmPush(false); break;
      case '}':
      case ']': osrmNumEnd(); osrmPop();       break;
      case '"': osrmNumEnd(); osrmStrStart();  break;
      case ':': break;
      case ',':
        osrmNumEnd();
        if (osrmIsObj[osrmDepth]) { osrmWantKey[osrmDepth] = true; osrmKey[osrmDepth][0] = 0; }
        else                      { osrmIdx[osrmDepth]++; }
        break;
      default:
        if (c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E' ||
            (c >= '0' && c <= '9')) {
          if (osrmNumLen + 1 < (uint8_t)sizeof(osrmNum)) osrmNum[osrmNumLen++] = c;
          osrmInNum = true;
        }
        break;
    }
  }
}

// L'arrivo arriva per ultimo: ce n'e' uno per ogni tappa e vale solo l'ultimo.
static void osrmEnd() {
  osrmNumEnd();
  if (osrmArriveHas && osrmTurnFn) {
    osrmTurnsOut++;
    osrmTurnFn(osrmArriveLat, osrmArriveLon, TURN_ARRIVE, 0, 0, osrmArriveName);
  }
}
