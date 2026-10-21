#pragma once
// ---------------------------------------------------------------------------
// Ricalcolo del percorso.
//
// Quando sbagli strada succedono due cose, e la prima funziona sempre:
//
//   1. la freccia grande smette di puntare al punto dove eri e punta al
//      RIENTRO, cioe' al punto del percorso piu' comodo da riprendere. Non
//      serve rete, e' solo geometria: anche in mezzo alle montagne senza
//      campo sai da che parte tornare.
//
//   2. se il tracker ha rete - normalmente l'hotspot del telefono - chiede
//      al servizio strade come arrivarci davvero, e il tratto calcolato
//      prende il posto del pezzo di percorso che hai saltato.
//
// Il punto importante e' cosa NON fa: non ricalcola da capo fino a
// destinazione. Il giro lo hai scelto tu, strada per strada, e un ricalcolo
// "vai a destinazione" ti rispedirebbe in tangenziale. Qui si ricuce solo lo
// strappo: davanti il tratto nuovo, dietro il tuo giro intatto.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "config.h"
#include "route.h"
#include "osrm.h"
#include "httpchunk.h"

static uint8_t  rrStato     = RR_FERMO;
static char     rrMsg[40]   = "";
static uint32_t rrOffSince  = 0;        // da quando sei fuori percorso
static uint32_t rrLastTry   = 0;
static uint32_t rrMsgUntil  = 0;
static uint16_t rrFatti     = 0;        // ricalcoli riusciti in questo viaggio
static uint16_t rrRejoin    = 0;        // punto su cui rientrare
static bool     rrChiesto   = false;    // richiesto a mano dal menu o dalla dashboard

// Il tratto di rientro si raccoglie qui e solo alla fine, se e' andato tutto
// bene, entra nel percorso vero: una risposta a meta' non lo rovina.
static RoutePoint rrPts[REROUTE_MAX_LEG];
static uint16_t   rrPtsN;
static Turn       rrTurns[REROUTE_MAX_TURNS];
static uint16_t   rrTurnsN;
static double     rrLastLat, rrLastLon;
static bool       rrHasLast;

static void rrVertex(double lat, double lon) {
  if (rrPtsN >= REROUTE_MAX_LEG) return;
  if (rrHasLast &&
      TinyGPSPlus::distanceBetween(rrLastLat, rrLastLon, lat, lon) < ROUTE_MIN_SPACING_M) return;
  rrPts[rrPtsN].lat = (float)lat;
  rrPts[rrPtsN].lon = (float)lon;
  rrPtsN++;
  rrLastLat = lat; rrLastLon = lon; rrHasLast = true;
}

// L'arrivo del tratto non e' l'arrivo del giro: li' il percorso riprende.
static void rrTurn(double lat, double lon, uint8_t kind, uint8_t exit,
                   uint8_t flags, const char *name) {
  if (kind == TURN_ARRIVE || rrTurnsN >= REROUTE_MAX_TURNS) return;
  Turn &t = rrTurns[rrTurnsN++];
  t.lat   = (float)lat;
  t.lon   = (float)lon;
  t.idx   = 0;
  t.kind  = kind;
  t.exit  = exit;
  t.flags = flags;
  osrmCopy(t.name, TURN_NAME_LEN, name ? name : "");
}

// ---------------------------------------------------------------------------
// Dove rientrare: il punto piu' vicino, ma spostato un po' avanti. Rientrare
// esattamente sul piu' vicino vorrebbe dire tornare indietro fin dove hai
// sbagliato, che e' proprio quello che non vuoi.
// ---------------------------------------------------------------------------
static uint16_t rerouteRejoinIdx(double lat, double lon) {
  double d = 0;
  const uint16_t vicino = routeNearestAll(lat, lon, &d);
  uint16_t j = vicino;
  while (j + 1 < routeCount && (routeCum[vicino] - routeCum[j]) < REROUTE_AHEAD_M) j++;
  return j;
}

// ---------------------------------------------------------------------------
// Il tratto nuovo prende il posto dell'inizio: la coda, cioe' il resto del
// giro che avevi programmato, non si tocca.
// ---------------------------------------------------------------------------
static bool rerouteSplice(uint16_t rejoin) {
  if (rrPtsN < 2 || !routeCount) return false;
  if (rejoin >= routeCount) rejoin = (uint16_t)(routeCount - 1);

  uint16_t coda = (uint16_t)(routeCount - rejoin);
  // se non ci sta tutto si dirada la coda, non si taglia: meglio un giro
  // intero meno fitto che mezzo giro preciso
  while ((uint32_t)rrPtsN + coda > ROUTE_MAX_POINTS && coda > 2) {
    const uint16_t nuova = (uint16_t)((coda + 1) / 2);
    for (uint16_t i = 1; i < nuova; i++) routePts[rejoin + i] = routePts[rejoin + i * 2];
    routePts[rejoin + nuova - 1] = routePts[rejoin + coda - 1];   // l'arrivo resta l'arrivo
    coda = nuova;
  }

  memmove(&routePts[rrPtsN], &routePts[rejoin], (size_t)coda * sizeof(RoutePoint));
  memcpy(routePts, rrPts, (size_t)rrPtsN * sizeof(RoutePoint));
  routeCount = (uint16_t)(rrPtsN + coda);

  // le svolte prima del rientro non valgono piu'; quelle dopo restano, e
  // davanti ci vanno quelle del tratto nuovo
  uint16_t saltate = 0;
  while (saltate < turnCount && turns[saltate].idx < rejoin) saltate++;
  uint16_t resto = (uint16_t)(turnCount - saltate);
  if ((uint32_t)rrTurnsN + resto > TURNS_MAX) resto = (uint16_t)(TURNS_MAX - rrTurnsN);
  memmove(&turns[rrTurnsN], &turns[saltate], (size_t)resto * sizeof(Turn));
  memcpy(turns, rrTurns, (size_t)rrTurnsN * sizeof(Turn));
  turnCount = (uint16_t)(rrTurnsN + resto);

  routeComputeCum();
  turnsBindToRoute();
  navReset();
  routeSave();
  turnsSave();
  return true;
}

// ---------------------------------------------------------------------------
// La richiesta al servizio strade
// ---------------------------------------------------------------------------
static void rrBody(const uint8_t *d, size_t n) { osrmFeed(d, n); }

static bool rerouteFetch(double lat, double lon, double dlat, double dlon) {
  WiFiClientSecure cli;
  cli.setInsecure();                 // il servizio e' pubblico: non c'e' niente da farsi dire
  cli.setTimeout(REROUTE_TIMEOUT_MS / 1000);
  if (!cli.connect(REROUTE_HOST, 443)) {
    snprintf(rrMsg, sizeof(rrMsg), "servizio irraggiungibile");
    return false;
  }

  char req[360];
  snprintf(req, sizeof(req),
           "GET /route/v1/driving/%.6f,%.6f;%.6f,%.6f"
           "?overview=full&geometries=polyline&steps=true&alternatives=false HTTP/1.0\r\n"
           "Host: " REROUTE_HOST "\r\n"
           "User-Agent: gps-tracker\r\n"
           "Connection: close\r\n\r\n",
           lon, lat, dlon, dlat);
  cli.print(req);

  rrPtsN = 0; rrTurnsN = 0; rrHasLast = false;
  osrmBegin(rrVertex, rrTurn);
  httpBegin(rrBody);

  uint8_t buf[512];
  const uint32_t inizio = millis();
  while ((cli.connected() || cli.available()) && millis() - inizio < REROUTE_TIMEOUT_MS) {
    const int n = cli.read(buf, sizeof(buf));
    if (n > 0) httpFeed(buf, (size_t)n);
    else       delay(2);
  }
  cli.stop();
  osrmEnd();

  if (httpStatus != 200) { snprintf(rrMsg, sizeof(rrMsg), "servizio: errore %u", httpStatus); return false; }
  if (!osrmCodeOk)       { snprintf(rrMsg, sizeof(rrMsg), "nessuna strada per rientrare");     return false; }
  if (rrPtsN < 2)        { snprintf(rrMsg, sizeof(rrMsg), "risposta senza percorso");          return false; }
  return true;
}

static void rerouteMsg(uint8_t stato, const char *testo, uint32_t perMs) {
  rrStato = stato;
  if (testo) snprintf(rrMsg, sizeof(rrMsg), "%s", testo);
  rrMsgUntil = millis() + perMs;
}

// Chiesto a mano dal display o dalla dashboard.
static void rerouteRequest() { rrChiesto = true; }

static bool rerouteBusy() { return rrStato == RR_CORSO; }

// ---------------------------------------------------------------------------
// Il giro di controllo, chiamato dal loop principale
// ---------------------------------------------------------------------------
static void reroutePoll(double lat, double lon, bool fixValid) {
#if !ENABLE_REROUTE
  (void)lat; (void)lon; (void)fixValid;
#else
  // il messaggio di esito resta scritto qualche secondo, poi sparisce
  if (rrMsgUntil && (int32_t)(millis() - rrMsgUntil) > 0) {
    rrMsgUntil = 0; rrMsg[0] = '\0';
    if (rrStato != RR_CORSO) rrStato = RR_FERMO;
  }

  if (!fixValid || !nav.active || routeCount < 2 || nav.finished) {
    rrOffSince = 0; rrChiesto = false;
    return;
  }

  // Il giro prima si limita a dire "sto ricalcolando": cosi' la schermata fa
  // in tempo a scriverlo prima che la richiesta blocchi tutto per qualche
  // secondo. Chi guida vede perche' il display si e' fermato.
  if (rrStato == RR_CORSO) {
    const uint16_t rejoin = rrRejoin;
    rrLastTry = millis();
    const bool ok = rerouteFetch(lat, lon, routePts[rejoin].lat, routePts[rejoin].lon) &&
                    rerouteSplice(rejoin);
    if (ok) {
      rrFatti++;
      rrOffSince = 0;
      char m[40];
      snprintf(m, sizeof(m), "rientro calcolato: %u punti", rrPtsN);
      rerouteMsg(RR_FATTO, m, 6000);
      Serial.printf("[ROTTA] rientro: %u punti, %u svolte, percorso ora %u punti\r\n",
                    rrPtsN, rrTurnsN, routeCount);
    } else {
      rerouteMsg(RR_ERRORE, rrMsg[0] ? rrMsg : "ricalcolo fallito", 6000);
      Serial.printf("[ROTTA] ricalcolo fallito: %s\r\n", rrMsg);
    }
    return;
  }

  const bool fuori = nav.offRoute;
  if (!fuori && !rrChiesto) { rrOffSince = 0; return; }
  if (!rrOffSince) rrOffSince = millis();

  const bool scaduto = (millis() - rrOffSince) >= (uint32_t)REROUTE_AFTER_S * 1000;
  if (!rrChiesto && !scaduto) {
    if (rrStato == RR_FERMO) rrStato = RR_ATTESA;
    return;
  }
  if (!rrChiesto && rrLastTry && (millis() - rrLastTry) < (uint32_t)REROUTE_COOLDOWN_S * 1000) return;

  rrChiesto = false;
  if (WiFi.status() != WL_CONNECTED) {
    rerouteMsg(RR_SENZARETE, "senza rete: segui la freccia", 5000);
    rrLastTry = millis();
    return;
  }
  if (ESP.getFreeHeap() < REROUTE_MIN_HEAP) {
    rerouteMsg(RR_ERRORE, "memoria insufficiente", 5000);
    rrLastTry = millis();
    return;
  }

  rrRejoin = rerouteRejoinIdx(lat, lon);
  rerouteMsg(RR_CORSO, "ricalcolo in corso", 20000);
#endif
}
