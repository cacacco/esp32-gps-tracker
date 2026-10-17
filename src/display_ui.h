/*
 * display_ui.h - computer di bordo su TFT 4" ST7796 (480x320, SPI).
 *
 * Due schermate, scelte in base allo stato del GPS:
 *   - attesa: satelliti, frasi NMEA, stato della rete. Serve a capire cosa
 *     sta succedendo mentre il modulo cerca il cielo;
 *   - marcia: velocita' grande al centro, direzione, distanza dalla base,
 *     statistiche del viaggio e orologio sincronizzato dal GPS.
 *
 * Si ridisegna solo cio' che cambia: riscrivere tutto a ogni giro su SPI
 * farebbe lampeggiare lo schermo e mangerebbe il tempo del ciclo principale.
 */
#pragma once

#include "config.h"

#if ENABLE_DISPLAY

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <time.h>

#include "turns.h"

// I pin del pannello stanno in platformio.ini: qui si controlla solo che non
// finiscano anche su un'altra funzione. Meglio una compilazione che si ferma
// con un messaggio chiaro che mezza giornata a chiedersi perche' il GPS non
// parla piu' da quando c'e' lo schermo.
#if ENABLE_TOUCH && defined(TOUCH_CS)
  #define DISPLAY_PIN_TOUCH(p) ((p) == TOUCH_CS)
#else
  #define DISPLAY_PIN_TOUCH(p) (false)
#endif
#define DISPLAY_USA_PIN(p) ((p) == TFT_CS   || (p) == TFT_DC   || (p) == TFT_RST || \
                            (p) == TFT_SCLK || (p) == TFT_MOSI || (p) == TFT_MISO || \
                            DISPLAY_PIN_TOUCH(p))
#if DISPLAY_USA_PIN(PIN_LED)
  #error "PIN_LED e' un pin del display: in config.h scegline un altro (32 va bene)"
#endif
#if DISPLAY_USA_PIN(PIN_GPS_PPS)
  #error "PIN_GPS_PPS e' un pin del display: in config.h mettilo a -1"
#endif
#if DISPLAY_USA_PIN(PIN_GPS_RX) || DISPLAY_USA_PIN(PIN_GPS_TX)
  #error "I pin del GPS si sovrappongono al display: controlla config.h"
#endif

struct DisplayData {
  bool        fixValid;
  bool        gpsAlive;
  float       speedKmh;
  const char *cardinal;
  uint32_t    satellites;
  float       hdop;
  float       altitude;
  float       distanceHome;
  bool        outside;
  float       tripKm;
  float       maxKmh;
  uint32_t    movingS;
  uint32_t    epoch;
  uint32_t    sentences;
  uint32_t    searchS;       // da quanti secondi cerca il fix
  const char *netName;
  const char *ipText;        // indirizzo della dashboard, "" se non connesso
  const char *apName;        // nome della rete creata dal tracker
  bool        portal;
  uint16_t    routeTurns;    // svolte caricate
  float       routeKm;       // lunghezza del percorso
  uint32_t    trackPoints;   // punti di traccia salvati
  bool        navActive;
  bool        navOffRoute;
  bool        navFinished;
  float       navRelative;   // gradi rispetto a dove sei rivolto
  float       navToNextM;
  float       navRemainM;
  uint16_t    navTarget;
  uint16_t    navTotal;
  uint8_t     navTurnKind;   // prossima manovra, 0 se non ce ne sono
  uint8_t     navTurnExit;   // uscita della rotonda
  uint8_t     navTurn2Kind;  // la manovra dopo quella in arrivo
  float       navTurn2M;     // quanto manca a quella
  const char *navTurnName;   // strada su cui si prosegue
  float       navTurnM;      // quanto manca alla manovra
  const char *navStopName;   // nome della prossima tappa
  float       navStopM;      // quanto manca
  uint8_t     navStops;
};

static TFT_eSPI tft;

static const uint16_t COL_BG    = TFT_BLACK;
static const uint16_t COL_FG    = TFT_WHITE;
static const uint16_t COL_LABEL = TFT_DARKGREY;
static const uint16_t COL_LINE  = 0x2104;        // grigio molto scuro
static const uint16_t COL_OK    = TFT_GREEN;
static const uint16_t COL_BAD   = TFT_RED;
static const uint16_t COL_WARN  = TFT_ORANGE;

static const uint16_t COL_BTN   = 0x18E3;        // fondo dei pulsanti
static const uint16_t COL_CYAN  = TFT_CYAN;

enum DisplayScreen { SCREEN_NONE, SCREEN_WAIT, SCREEN_DRIVE, SCREEN_NAV };
static DisplayScreen displayScreen = SCREEN_NONE;

#include "touch_ui.h"

// ---------------------------------------------------------------------------
static void displayField(const char *text, int32_t x, int32_t y,
                         uint8_t font, uint16_t pad, uint16_t color) {
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(color, COL_BG);
  tft.setTextPadding(pad);
  tft.drawString(text, x, y, font);
  tft.setTextPadding(0);
}

static void displayClock(uint32_t epoch, char *out, size_t len) {
  if (!epoch) { snprintf(out, len, "--:--"); return; }
  const time_t t = (time_t)epoch + (time_t)TIME_UTC_OFFSET_MIN * 60;
  struct tm local;
  gmtime_r(&t, &local);                          // l'offset e' gia' applicato
  snprintf(out, len, "%02d:%02d", local.tm_hour, local.tm_min);
}

static void displayDuration(uint32_t seconds, char *out, size_t len) {
  if (seconds >= 3600) snprintf(out, len, "%luh %02lum",
                                (unsigned long)(seconds / 3600),
                                (unsigned long)(seconds % 3600 / 60));
  else                 snprintf(out, len, "%lum %02lus",
                                (unsigned long)(seconds / 60),
                                (unsigned long)(seconds % 60));
}

// ---------------------------------------------------------------------------
// Barra superiore: stato dell'area, satelliti, qualita', orologio
// ---------------------------------------------------------------------------
#if ENABLE_TOUCH
// Le tre righette del menu, disegnate a mano: nel font non c'e' il simbolo.
static void drawMenuHint(int16_t x) {
  for (uint8_t i = 0; i < 3; i++) tft.drawFastHLine(x, 13 + i * 5, 14, COL_LABEL);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString("menu", x + 20, 19, 2);
}
#endif

static void drawTopBar(const DisplayData &d, bool force) {
  static int32_t pSat = -1, pHdop = -1;
  static char    pClock[8] = "";
  static int8_t  pFence = -1;
  char buf[32];

  const int8_t fenceNow = d.outside ? 1 : 0;
  if (force || fenceNow != pFence) {
    pFence = fenceNow;
    tft.fillCircle(18, 19, 7, d.outside ? COL_BAD : COL_OK);
  }

  if (force || (int32_t)d.satellites != pSat || (int32_t)(d.hdop * 10) != pHdop) {
    pSat  = (int32_t)d.satellites;
    pHdop = (int32_t)(d.hdop * 10);
    snprintf(buf, sizeof(buf), "%lu sat   HDOP %.1f", (unsigned long)d.satellites, d.hdop);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setTextPadding(200);
    tft.drawString(buf, 34, 19, 2);
    tft.setTextPadding(0);
  }

  displayClock(d.epoch, buf, sizeof(buf));
  if (force || strcmp(buf, pClock) != 0) {
    snprintf(pClock, sizeof(pClock), "%s", buf);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(COL_FG, COL_BG);
    tft.setTextPadding(90);
    tft.drawString(pClock, 472, 19, 4);
    tft.setTextPadding(0);
  }
}

// ---------------------------------------------------------------------------
// Schermata di attesa del fix
// ---------------------------------------------------------------------------
static void drawWaitChrome() {
  tft.fillScreen(COL_BG);
  tft.drawFastHLine(0, 38, 480, COL_LINE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString("ESP32 GPS Tracker", 240, 19, 2);
#if ENABLE_TOUCH
  drawMenuHint(40);
#endif
}

static void drawWait(const DisplayData &d, bool force) {
  static int32_t pSat = -1, pSent = -1;
  static bool    pAlive = false;
  char buf[48];

  if (force || d.gpsAlive != pAlive) {
    pAlive = d.gpsAlive;
    if (d.gpsAlive) displayField("RICERCA SATELLITI", 240, 110, 4, 460, COL_FG);
    else            displayField("NESSUN DATO DAL GPS", 240, 110, 4, 460, COL_BAD);
  }

  if (force || (int32_t)d.satellites != pSat) {
    pSat = (int32_t)d.satellites;
    snprintf(buf, sizeof(buf), "satelliti in vista: %lu", (unsigned long)d.satellites);
    displayField(buf, 240, 160, 2, 460, COL_LABEL);
  }

  if (force || (int32_t)d.sentences != pSent) {
    pSent = (int32_t)d.sentences;
    if (d.gpsAlive) snprintf(buf, sizeof(buf), "frasi NMEA valide: %lu", (unsigned long)d.sentences);
    else            snprintf(buf, sizeof(buf), "controlla TX su D%d, GND e alimentazione", PIN_GPS_RX);
    displayField(buf, 240, 190, 2, 460, COL_LABEL);
  }

  // Il cronometro della ricerca: serve a sapere se il modulo sta partendo a
  // freddo o se c'e' qualcosa che non va.
  static int32_t pCerca = -1;
  if (force || (int32_t)d.searchS != pCerca) {
    pCerca = (int32_t)d.searchS;
    snprintf(buf, sizeof(buf), "in ricerca da %lu:%02lu",
             (unsigned long)(d.searchS / 60), (unsigned long)(d.searchS % 60));
    displayField(buf, 240, 230, 2, 460, d.searchS > 120 ? COL_WARN : COL_LABEL);
  }

  if (force) {
    displayField("l'antenna deve vedere il cielo", 240, 258, 2, 460, COL_LABEL);
    snprintf(buf, sizeof(buf), "%s %s", d.portal ? "access point:" : "rete:", d.netName);
    displayField(buf, 240, 292, 2, 460, d.portal ? COL_WARN : COL_LABEL);
  }
}

// ---------------------------------------------------------------------------
// Schermata di marcia
// ---------------------------------------------------------------------------
static void drawDriveChrome() {
  tft.fillScreen(COL_BG);
  tft.drawFastHLine(0, 38, 480, COL_LINE);
  tft.drawFastHLine(0, 250, 480, COL_LINE);
  tft.drawFastVLine(316, 46, 196, COL_LINE);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString("km/h", 158, 212, 4);
  tft.drawString("DIREZIONE", 400, 70, 2);
  tft.drawString("DALLA BASE", 400, 162, 2);

#if ENABLE_TOUCH
  drawMenuHint(260);                      // fra il contasatelliti e l'orologio
#endif

  static const char *labels[4] = { "VIAGGIO", "MAX", "IN MOVIMENTO", "QUOTA" };
  for (uint8_t i = 0; i < 4; i++) tft.drawString(labels[i], 60 + i * 120, 268, 2);
}

static void drawDrive(const DisplayData &d, bool force) {
  static int32_t pSpeed = -1, pDist = -1, pTrip = -1;
  static int32_t pMax = -1, pMov = -1, pAlt = -99999;
  static char    pCard[6] = "";
  char buf[24];

  // --- velocita' (font a sette segmenti, doppia altezza) ---
  const int32_t speed = (int32_t)(d.speedKmh + 0.5f);
  if (force || speed != pSpeed) {
    pSpeed = speed;
    snprintf(buf, sizeof(buf), "%ld", (long)speed);
    tft.setTextSize(2);
    displayField(buf, 158, 140, 7, 300, d.outside ? COL_WARN : COL_FG);
    tft.setTextSize(1);
  }

  // --- direzione ---
  const char *card = d.cardinal ? d.cardinal : "--";
  if (force || strcmp(card, pCard) != 0) {
    snprintf(pCard, sizeof(pCard), "%s", card);
    tft.setTextSize(2);
    displayField(pCard, 400, 110, 4, 150, COL_FG);
    tft.setTextSize(1);
  }

  // --- distanza dalla base ---
  const int32_t dist = (int32_t)d.distanceHome;
  if (force || dist != pDist) {
    pDist = dist;
    if (dist < 1000) snprintf(buf, sizeof(buf), "%ld m", (long)dist);
    else             snprintf(buf, sizeof(buf), "%.1f km", dist / 1000.0);
    displayField(buf, 400, 196, 4, 150, d.outside ? COL_BAD : COL_OK);
  }

  drawTopBar(d, force);

  // --- banda bassa: statistiche ---
  const int32_t trip = (int32_t)(d.tripKm * 10);
  if (force || trip != pTrip) {
    pTrip = trip;
    snprintf(buf, sizeof(buf), "%.1f", d.tripKm);
    displayField(buf, 60, 294, 4, 110, COL_FG);
  }
  const int32_t maxs = (int32_t)(d.maxKmh + 0.5f);
  if (force || maxs != pMax) {
    pMax = maxs;
    snprintf(buf, sizeof(buf), "%ld", (long)maxs);
    displayField(buf, 180, 294, 4, 110, COL_FG);
  }
  const int32_t mov = (int32_t)(d.movingS / 10);
  if (force || mov != pMov) {
    pMov = mov;
    displayDuration(d.movingS, buf, sizeof(buf));
    displayField(buf, 300, 294, 4, 110, COL_FG);
  }
  const int32_t alt = (int32_t)d.altitude;
  if (force || alt != pAlt) {
    pAlt = alt;
    snprintf(buf, sizeof(buf), "%ld", (long)alt);
    displayField(buf, 420, 294, 4, 110, COL_FG);
  }
}

// ---------------------------------------------------------------------------
// Schermata di navigazione: freccia verso il prossimo punto del percorso
// ---------------------------------------------------------------------------
static const int ARROW_X = 145, ARROW_Y = 152, ARROW_R = 76;

// Una freccia qualunque: serve grande per la direzione e piccola per le due
// manovre in arrivo.
static void drawArrowAt(int cx, int cy, int r, float angleDeg, uint16_t color) {
  const float a = angleDeg * (float)PI / 180.0f;
  // Punta, due spalle e una coda rientrante: due triangoli bastano.
  const int x0 = cx + (int)(sinf(a) * r);
  const int y0 = cy - (int)(cosf(a) * r);
  const float aL = a + 2.44f, aR = a - 2.44f;         // ~140 gradi
  const int xL = cx + (int)(sinf(aL) * r * 0.78f);
  const int yL = cy - (int)(cosf(aL) * r * 0.78f);
  const int xR = cx + (int)(sinf(aR) * r * 0.78f);
  const int yR = cy - (int)(cosf(aR) * r * 0.78f);
  const int xT = cx - (int)(sinf(a) * r * 0.28f);
  const int yT = cy + (int)(cosf(a) * r * 0.28f);

  tft.fillTriangle(x0, y0, xL, yL, xT, yT, color);
  tft.fillTriangle(x0, y0, xR, yR, xT, yT, color);
}

static void drawArrow(float angleDeg, uint16_t color) {
  drawArrowAt(ARROW_X, ARROW_Y, ARROW_R, angleDeg, color);
}

// ---------------------------------------------------------------------------
// Schermata di navigazione, impaginata come un navigatore da auto:
//
//   a sinistra la freccia grande, che e' la direzione in cui andare adesso;
//   a destra in alto la SVOLTA in arrivo - freccia sua, distanza che scende e
//   nome della strada - e sotto POI, la manovra successiva, cosi' sai gia'
//   che dopo la rotonda devi stare a destra. In fondo la velocita'.
//
// Senza svolte caricate il riquadro SVOLTA mostra la prossima tappa: lo
// spazio non resta vuoto e la freccia grande basta a orientarsi.
// ---------------------------------------------------------------------------
static const int SV_X = 330, SV_Y = 100, SV_R = 28;     // freccia della svolta
static const int PO_X = 330, PO_Y = 212, PO_R = 18;     // freccia del "poi"

static void drawNavChrome() {
  tft.fillScreen(COL_BG);
  tft.drawFastHLine(0, 38, 480, COL_LINE);
  tft.drawFastVLine(292, 46, 270, COL_LINE);
  tft.drawFastHLine(296, 168, 184, COL_LINE);
  tft.drawFastHLine(296, 242, 184, COL_LINE);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_LABEL, COL_BG);
#if ENABLE_TOUCH
  drawMenuHint(260);
#endif
  tft.drawString("RESTANO", 144, 238, 2);
  tft.drawString("km/h", 386, 254, 2);
}

static void drawNav(const DisplayData &d, bool force) {
  static int32_t pAngle = -9999, pRemain = -1, pSpeed = -1, pTurnM = -1, pPoiM = -1;
  static int8_t  pState = -1, pMode = -1, pVicina = -1;
  static int32_t pTurnA = -9999, pPoiA = -9999;
  static char    pLinea[40] = "", pStrada[28] = "";
  char buf[32];

  drawTopBar(d, force);

  // ---- freccia grande: dove andare adesso ---------------------------------
  const uint16_t coloreFreccia = d.navFinished ? COL_OK : d.navOffRoute ? COL_WARN : COL_CYAN;
  const int32_t  angolo = (int32_t)d.navRelative;
  const int8_t   stato  = d.navFinished ? 2 : d.navOffRoute ? 1 : 0;
  if (force || abs(angolo - pAngle) >= 4 || stato != pState) {
    pAngle = angolo;
    tft.fillCircle(ARROW_X, ARROW_Y, ARROW_R + 2, COL_BG);
    drawArrow(d.navRelative, coloreFreccia);
  }

  // ---- quanto manca alla fine --------------------------------------------
  const int32_t restano = (int32_t)(d.navRemainM / 100);
  if (force || restano != pRemain) {
    pRemain = restano;
    if (d.navRemainM < 1000) snprintf(buf, sizeof(buf), "%ld m", (long)d.navRemainM);
    else                     snprintf(buf, sizeof(buf), "%.1f km", d.navRemainM / 1000.0f);
    displayField(buf, 144, 268, 4, 270, COL_FG);
  }

  // ---- riga di stato sotto la freccia -------------------------------------
  char linea[40];
  uint16_t coloreLinea = COL_LABEL;
  if (d.navFinished)      { snprintf(linea, sizeof(linea), "ARRIVATO");       coloreLinea = COL_OK; }
  else if (d.navOffRoute) { snprintf(linea, sizeof(linea), "FUORI PERCORSO"); coloreLinea = COL_BAD; }
  else if (d.navStops)    { snprintf(linea, sizeof(linea), "tappa: %s", d.navStopName ? d.navStopName : "--"); }
  else                    { linea[0] = '\0'; }
  if (force || strcmp(linea, pLinea) != 0) {
    snprintf(pLinea, sizeof(pLinea), "%s", linea);
    displayField(linea, 144, 302, 2, 280, linea[0] ? coloreLinea : COL_BG);
  }

  // ---- riquadro SVOLTA ----------------------------------------------------
  const bool    haSvolta = d.navTurnKind != TURN_NONE && !d.navFinished;
  const int8_t  modo     = haSvolta ? 0 : 1;          // 0 = svolta, 1 = tappa
  const bool    vicina   = haSvolta && d.navTurnM <= TURN_WARN_M;

  if (force || modo != pMode) {
    pMode = modo;
    displayField(haSvolta ? "SVOLTA" : "PROSSIMA TAPPA", 386, 58, 2, 180, COL_LABEL);
    pTurnA = -9999; pTurnM = -1; pStrada[0] = '\0';
  }

  // la cornice arancione dice "ci sei quasi" senza bisogno di leggere i numeri
  const bool cambiaVicina = force || (int8_t)vicina != pVicina;
  if (cambiaVicina) tft.drawRoundRect(296, 44, 180, 122, 8, vicina ? COL_WARN : COL_BG);

  const int32_t angoloSvolta = haSvolta ? (int32_t)turnAngle(d.navTurnKind) : -9999;
  if (force || angoloSvolta != pTurnA || cambiaVicina) {
    pTurnA = angoloSvolta;
    tft.fillCircle(SV_X, SV_Y, SV_R + 2, COL_BG);
    if (haSvolta) drawArrowAt(SV_X, SV_Y, SV_R, (float)angoloSvolta, vicina ? COL_WARN : COL_FG);
  }

  const float   distSvolta = haSvolta ? d.navTurnM : d.navStopM;
  const int32_t distTonda  = (int32_t)distSvolta;
  if (force || distTonda != pTurnM || cambiaVicina) {
    pTurnM = distTonda;
    if (!haSvolta && !d.navStops)   snprintf(buf, sizeof(buf), "--");
    else if (distSvolta < 1000)     snprintf(buf, sizeof(buf), "%ld m", (long)distSvolta);
    else                            snprintf(buf, sizeof(buf), "%.1f km", distSvolta / 1000.0f);
    displayField(buf, 420, 100, 4, 100, vicina ? COL_WARN : COL_FG);
  }

  // nome: la strada su cui si prosegue, o la tappa se non ci sono svolte
  char strada[28];
  if (haSvolta && d.navTurnKind == TURN_ROUNDABOUT && d.navTurnExit)
    snprintf(strada, sizeof(strada), "%u uscita: %s", d.navTurnExit, d.navTurnName ? d.navTurnName : "");
  else if (haSvolta)
    snprintf(strada, sizeof(strada), "%s %s", turnWord(d.navTurnKind), d.navTurnName ? d.navTurnName : "");
  else
    snprintf(strada, sizeof(strada), "%s", d.navStops ? (d.navStopName ? d.navStopName : "--") : "nessuna");
  if (force || strcmp(strada, pStrada) != 0) {
    snprintf(pStrada, sizeof(pStrada), "%s", strada);
    displayField(strada, 386, 140, 2, 180, COL_LABEL);
  }

  // ---- riquadro POI -------------------------------------------------------
  const bool    haPoi  = d.navTurn2Kind != TURN_NONE && !d.navFinished;
  const int32_t poiA   = haPoi ? (int32_t)turnAngle(d.navTurn2Kind) : -9999;
  if (force || poiA != pPoiA) {
    pPoiA = poiA;
    displayField(haPoi ? "POI" : "", 386, 180, 2, 180, COL_LABEL);
    tft.fillCircle(PO_X, PO_Y, PO_R + 2, COL_BG);
    if (haPoi) drawArrowAt(PO_X, PO_Y, PO_R, (float)poiA, COL_LABEL);
  }

  const int32_t poiM = haPoi ? (int32_t)d.navTurn2M : -1;
  if (force || poiM != pPoiM) {
    pPoiM = poiM;
    if (!haPoi)                 buf[0] = '\0';
    else if (d.navTurn2M < 1000) snprintf(buf, sizeof(buf), "%ld m", (long)d.navTurn2M);
    else                         snprintf(buf, sizeof(buf), "%.1f km", d.navTurn2M / 1000.0f);
    displayField(buf, 420, 212, 2, 100, COL_LABEL);
  }

  // ---- velocita' ----------------------------------------------------------
  const int32_t velocita = (int32_t)(d.speedKmh + 0.5f);
  if (force || velocita != pSpeed) {
    pSpeed = velocita;
    snprintf(buf, sizeof(buf), "%ld", (long)velocita);
    tft.setTextSize(2);
    displayField(buf, 386, 290, 4, 170, COL_FG);
    tft.setTextSize(1);
  }

  pState  = stato;
  pVicina = (int8_t)vicina;
}

// ---------------------------------------------------------------------------
// Menu
//
// Quattro pulsanti grandi: in auto si deve centrare senza mirare. Le pagine
// sono statiche, si ridisegnano quando ci entri e basta - solo le schermate
// di marcia hanno bisogno del ridisegno parziale continuo.
//
// Attenzione: caricare un GPX o disegnare un percorso resta roba da telefono,
// qui non c'e' tastiera. Il menu fa le cose che hanno senso a bordo: dire
// cosa c'e' in memoria, buttarlo via, e tirare su la rete per farti collegare.
// ---------------------------------------------------------------------------
enum UiPage : uint8_t {
  PAGE_LIVE = 0,      // le schermate di marcia, scelte dallo stato del fix
  PAGE_MENU,
  PAGE_ROUTE,
  PAGE_PHONE,
  PAGE_TRIP,
  PAGE_CONFIRM
};

enum UiAction : uint8_t {
  UI_NONE = 0, UI_ROUTE_CLEAR, UI_TRACK_CLEAR, UI_TRIP_RESET,
  UI_PORTAL_ON, UI_PORTAL_OFF
};

struct Rect { int16_t x, y, w, h; };

static bool rectHit(const Rect &r, int16_t x, int16_t y) {
  return x >= r.x && x < (int16_t)(r.x + r.w) && y >= r.y && y < (int16_t)(r.y + r.h);
}

// due colonne per due righe, margini uguali da tutti i lati
static const Rect MENU_BTN[4] = {
  { 14,  50, 221, 123}, {245,  50, 221, 123},
  { 14, 183, 221, 123}, {245, 183, 221, 123}
};
// tre posti in fondo alle pagine: indietro, azione, azione
static const Rect PAGE_BTN[3] = {
  { 14, 258, 140, 48}, {170, 258, 140, 48}, {326, 258, 140, 48}
};
static const Rect BARRA_MENU = {0, 0, 480, 40};   // tocca qui per il menu

static UiPage    uiPage     = PAGE_MENU;          // all'accensione si parte da qui
static bool      uiDirty    = true;
static UiAction  uiPending  = UI_NONE;            // azione in attesa di conferma
static UiPage    uiBack     = PAGE_MENU;
static char      uiQuestion[48] = "";
static bool      uiPortalOn = false;              // copia per il pulsante del telefono
static bool      uiHasRoute = false;              // copia per il pulsante Cancella
static uint32_t  uiTouchMs  = 0;

// Giro fra i pulsanti col tasto: con un tasto solo non si puo' fare di
// meglio, breve sposta e lungo conferma. Serve quando il touch non c'e'.
static const Rect *uiSelLista[4];
static uint8_t     uiSelCount = 0;
static uint8_t     uiSelIdx   = 0;
static const Rect *uiSelRect  = nullptr;

static void uiGoto(UiPage p) {
  uiPage   = p;
  uiDirty  = true;
  uiSelIdx = 0;
  if (p == PAGE_LIVE) displayScreen = SCREEN_NONE;   // forza il ridisegno della cornice
}

static void uiAsk(UiAction a, const char *domanda) {
  uiPending = a;
  uiBack    = uiPage;
  snprintf(uiQuestion, sizeof(uiQuestion), "%s", domanda);
  uiGoto(PAGE_CONFIRM);
}

static void drawButton(const Rect &r, const char *titolo, const char *sotto, uint16_t bordo) {
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 10, COL_BTN);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 10, bordo);
  // il pulsante scelto col tasto ha una cornice chiara tutt'intorno
  if (&r == uiSelRect) {
    tft.drawRoundRect(r.x - 2, r.y - 2, r.w + 4, r.h + 4, 12, COL_FG);
    tft.drawRoundRect(r.x - 3, r.y - 3, r.w + 6, r.h + 6, 13, COL_FG);
  }
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_FG, COL_BTN);
  const int16_t cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  const bool    due = sotto && *sotto;
  tft.drawString(titolo, cx, due ? cy - 16 : cy, 4);
  if (due) {
    tft.setTextColor(COL_LABEL, COL_BTN);
    tft.drawString(sotto, cx, cy + 20, 2);
  }
}

static void drawPageHead(const char *titolo) {
  tft.fillScreen(COL_BG);
  tft.drawFastHLine(0, 38, 480, COL_LINE);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COL_FG, COL_BG);
  tft.drawString(titolo, 14, 19, 4);
}

static void drawRiga(const char *etichetta, const char *valore, int16_t y) {
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString(etichetta, 20, y, 2);
  tft.setTextColor(COL_FG, COL_BG);
  tft.drawString(valore, 210, y, 4);
}

static void drawMenu(const DisplayData &d) {
  char sotto[40];
  drawPageHead("MENU");
#if PIN_MENU_BUTTON >= 0
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString("tasto BOOT: breve sposta, lungo conferma", 466, 19, 2);
#endif

  drawButton(MENU_BTN[0], "Naviga",
             d.navActive ? "percorso attivo" : d.fixValid ? "velocita' e viaggio"
                                                          : "in attesa del fix", COL_CYAN);

  if (d.navTotal) snprintf(sotto, sizeof(sotto), "%.1f km, %u svolte", d.routeKm, d.routeTurns);
  else            snprintf(sotto, sizeof(sotto), "nessun percorso");
  drawButton(MENU_BTN[1], "Percorso", sotto, COL_WARN);

  if (d.portal)            snprintf(sotto, sizeof(sotto), "%s", d.apName ? d.apName : "rete del tracker");
  else if (d.ipText && *d.ipText) snprintf(sotto, sizeof(sotto), "%s", d.ipText);
  else                     snprintf(sotto, sizeof(sotto), "non connesso");
  drawButton(MENU_BTN[2], "Telefono", sotto, COL_CYAN);

  snprintf(sotto, sizeof(sotto), "%.1f km in questo viaggio", d.tripKm);
  drawButton(MENU_BTN[3], "Viaggio", sotto, COL_OK);
}

static void drawRoutePage(const DisplayData &d) {
  char buf[40];
  drawPageHead("PERCORSO");

  if (d.navTotal) {
    snprintf(buf, sizeof(buf), "%.1f km", d.routeKm);         drawRiga("lunghezza", buf, 70);
    snprintf(buf, sizeof(buf), "%u", d.navTotal);             drawRiga("punti", buf, 110);
    snprintf(buf, sizeof(buf), "%u", d.routeTurns);           drawRiga("svolte", buf, 150);
    snprintf(buf, sizeof(buf), "%u", d.navStops);             drawRiga("tappe", buf, 190);
  } else {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.drawString("nessun percorso in memoria", 240, 120, 4);
  }

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString("per caricarne uno nuovo usa il telefono", 240, 228, 2);

  drawButton(PAGE_BTN[0], "Indietro", "", COL_LINE);
  if (d.navTotal) drawButton(PAGE_BTN[2], "Cancella", "", COL_BAD);
}

static void drawPhonePage(const DisplayData &d) {
  drawPageHead("TELEFONO");

  tft.setTextDatum(MC_DATUM);
  if (d.portal) {
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.drawString("collega il telefono a questa rete", 240, 72, 2);
    tft.setTextColor(COL_FG, COL_BG);
    tft.drawString(d.apName ? d.apName : "GPS-Tracker", 240, 108, 4);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.drawString("password: " AP_PASSWORD, 240, 144, 2);
    tft.setTextColor(COL_OK, COL_BG);
    tft.drawString("http://192.168.4.1", 240, 186, 4);
    drawButton(PAGE_BTN[2], "Spegni", "", COL_WARN);
  } else if (d.ipText && *d.ipText) {
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.drawString("stessa rete del telefono:", 240, 72, 2);
    tft.setTextColor(COL_FG, COL_BG);
    tft.drawString(d.netName ? d.netName : "--", 240, 106, 4);
    tft.setTextColor(COL_OK, COL_BG);
    tft.drawString(d.ipText, 240, 152, 4);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.drawString("oppure http://gps-tracker.local/", 240, 190, 2);
    drawButton(PAGE_BTN[2], "Rete mia", "", COL_CYAN);
  } else {
    tft.setTextColor(COL_BAD, COL_BG);
    tft.drawString("nessuna rete", 240, 100, 4);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.drawString("accendi la rete del tracker e collegati a quella", 240, 150, 2);
    drawButton(PAGE_BTN[2], "Accendi", "", COL_OK);
  }

  drawButton(PAGE_BTN[0], "Indietro", "", COL_LINE);
}

static void drawTripPage(const DisplayData &d) {
  char buf[40];
  drawPageHead("VIAGGIO");

  snprintf(buf, sizeof(buf), "%.1f km", d.tripKm);            drawRiga("percorsi", buf, 70);
  snprintf(buf, sizeof(buf), "%.0f km/h", d.maxKmh);          drawRiga("velocita' max", buf, 110);
  displayDuration(d.movingS, buf, sizeof(buf));               drawRiga("in movimento", buf, 150);
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)d.trackPoints);
  drawRiga("punti registrati", buf, 190);

  drawButton(PAGE_BTN[0], "Indietro", "", COL_LINE);
  drawButton(PAGE_BTN[1], "Azzera", "", COL_WARN);
  drawButton(PAGE_BTN[2], "Canc. traccia", "", COL_BAD);
}

static void drawConfirmPage() {
  drawPageHead("CONFERMA");
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_FG, COL_BG);
  tft.drawString(uiQuestion, 240, 130, 4);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString("non si torna indietro", 240, 172, 2);
  drawButton(PAGE_BTN[0], "No", "", COL_LINE);
  drawButton(PAGE_BTN[2], "Si'", "", COL_BAD);
}

// Il tocco cambia pagina da solo; quello che tocca il resto del firmware
// (cancellare, azzerare, accendere la rete) torna indietro come azione.
static UiAction uiTouch(int16_t x, int16_t y) {
  uiTouchMs = millis();

  switch (uiPage) {
    case PAGE_LIVE:
      if (rectHit(BARRA_MENU, x, y)) uiGoto(PAGE_MENU);
      return UI_NONE;

    case PAGE_MENU:
      if (rectHit(MENU_BTN[0], x, y)) uiGoto(PAGE_LIVE);
      else if (rectHit(MENU_BTN[1], x, y)) uiGoto(PAGE_ROUTE);
      else if (rectHit(MENU_BTN[2], x, y)) uiGoto(PAGE_PHONE);
      else if (rectHit(MENU_BTN[3], x, y)) uiGoto(PAGE_TRIP);
      return UI_NONE;

    case PAGE_CONFIRM:
      if (rectHit(PAGE_BTN[0], x, y)) { uiGoto(uiBack); return UI_NONE; }
      if (rectHit(PAGE_BTN[2], x, y)) {
        const UiAction fatto = uiPending;
        uiPending = UI_NONE;
        uiGoto(uiBack);
        return fatto;
      }
      return UI_NONE;

    case PAGE_ROUTE:
      if (rectHit(PAGE_BTN[0], x, y)) uiGoto(PAGE_MENU);
      else if (rectHit(PAGE_BTN[2], x, y)) uiAsk(UI_ROUTE_CLEAR, "Cancellare il percorso?");
      return UI_NONE;

    case PAGE_TRIP:
      if (rectHit(PAGE_BTN[0], x, y)) { uiGoto(PAGE_MENU); return UI_NONE; }
      if (rectHit(PAGE_BTN[1], x, y)) { uiDirty = true;  return UI_TRIP_RESET; }
      if (rectHit(PAGE_BTN[2], x, y)) uiAsk(UI_TRACK_CLEAR, "Cancellare la traccia?");
      return UI_NONE;

    case PAGE_PHONE:
      if (rectHit(PAGE_BTN[0], x, y)) { uiGoto(PAGE_MENU); return UI_NONE; }
      if (rectHit(PAGE_BTN[2], x, y)) { uiDirty = true; return uiPortalOn ? UI_PORTAL_OFF : UI_PORTAL_ON; }
      return UI_NONE;
  }
  return UI_NONE;
}

// Ricostruisce l'elenco dei pulsanti su cui il tasto puo' fermarsi.
static void uiSelAggiorna() {
  uiSelCount = 0;
  switch (uiPage) {
    case PAGE_MENU:
      for (uint8_t i = 0; i < 4; i++) uiSelLista[uiSelCount++] = &MENU_BTN[i];
      break;
    case PAGE_ROUTE:
      uiSelLista[uiSelCount++] = &PAGE_BTN[0];
      if (uiHasRoute) uiSelLista[uiSelCount++] = &PAGE_BTN[2];
      break;
    case PAGE_PHONE:
    case PAGE_CONFIRM:
      uiSelLista[uiSelCount++] = &PAGE_BTN[0];
      uiSelLista[uiSelCount++] = &PAGE_BTN[2];
      break;
    case PAGE_TRIP:
      for (uint8_t i = 0; i < 3; i++) uiSelLista[uiSelCount++] = &PAGE_BTN[i];
      break;
    default:
      break;
  }
  if (uiSelIdx >= uiSelCount) uiSelIdx = 0;
  uiSelRect = uiSelCount ? uiSelLista[uiSelIdx] : nullptr;
}

// Pressione breve: dalle schermate di marcia apre il menu, dentro il menu
// passa al pulsante successivo.
static UiAction uiNext() {
  if (uiPage == PAGE_LIVE) { uiGoto(PAGE_MENU); return UI_NONE; }
  if (uiSelCount) uiSelIdx = (uint8_t)((uiSelIdx + 1) % uiSelCount);
  uiSelAggiorna();
  uiDirty = true;
  return UI_NONE;
}

// Pressione lunga: vale come un dito sul pulsante evidenziato, cosi' la
// logica e' una sola e non puo' divergere da quella del tocco.
static UiAction uiActivate() {
  if (uiPage == PAGE_LIVE) { uiGoto(PAGE_MENU); return UI_NONE; }
  if (!uiSelRect) return UI_NONE;
  const Rect r = *uiSelRect;
  return uiTouch((int16_t)(r.x + r.w / 2), (int16_t)(r.y + r.h / 2));
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Logo di accensione: la freccia della navigazione dentro il cerchio, con un
// satellite che gira sull'orbita. Disegnato con le primitive invece che con
// un'immagine: un bitmap a colori di questa misura costerebbe decine di
// kilobyte di flash, qui non costa niente e si muove.
// ---------------------------------------------------------------------------
static const int LOGO_X = 240, LOGO_Y = 130;
static const int LOGO_R = 62;          // cerchio
static const int ORB_RX = 96, ORB_RY = 34;   // orbita del satellite

static void drawSplash() {
  tft.fillScreen(COL_BG);
#if SPLASH_MS > 0

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_FG, COL_BG);
  tft.setTextSize(2);
  tft.drawString("GPS TRACKER", 240, 240, 4);
  tft.setTextSize(1);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString("registra i percorsi e ti porta a destinazione", 240, 286, 2);

  const uint8_t  fotogrammi = 26;
  const uint32_t pausa = SPLASH_MS / fotogrammi;
  int satX = LOGO_X + ORB_RX, satY = LOGO_Y;

  for (uint8_t i = 0; i <= fotogrammi; i++) {
    const float f = (float)i / fotogrammi;

    // via il satellite di prima e l'area della freccia, poi si ridisegna
    tft.fillCircle(satX, satY, 6, COL_BG);
    tft.fillCircle(LOGO_X, LOGO_Y, 48, COL_BG);

    tft.drawEllipse(LOGO_X, LOGO_Y, ORB_RX,     ORB_RY,     COL_LINE);
    tft.drawEllipse(LOGO_X, LOGO_Y, ORB_RX - 1, ORB_RY - 1, COL_LINE);
    tft.drawCircle(LOGO_X, LOGO_Y, LOGO_R,     COL_CYAN);
    tft.drawCircle(LOGO_X, LOGO_Y, LOGO_R - 1, COL_CYAN);

    // la freccia entra girando, come quando aggancia il fix
    drawArrowAt(LOGO_X, LOGO_Y, 40, -70.0f + 70.0f * f, COL_CYAN);

    const float giro = f * 2.0f * (float)PI;
    satX = LOGO_X + (int)(cosf(giro) * ORB_RX);
    satY = LOGO_Y + (int)(sinf(giro) * ORB_RY);
    tft.fillCircle(satX, satY, 4, COL_WARN);

    delay(pausa);
  }
#endif
}

static void displayBegin() {
  tft.init();
  tft.setRotation(DISPLAY_ROTATION);
  drawSplash();

#if ENABLE_TOUCH
  touchBegin();            // la prima accensione chiede la taratura
#endif
  uiTouchMs = millis();    // da qui parte il conto del menu
  uiGoto(PAGE_MENU);
}

static void displayUpdate(const DisplayData &d) {
  // Se non tocchi niente il menu si fa da parte: senza touch collegato il
  // tracker si comporta come prima, dopo MENU_TIMEOUT_MS e' in navigazione.
  if (uiPage == PAGE_MENU && millis() - uiTouchMs > MENU_TIMEOUT_MS) uiGoto(PAGE_LIVE);

  uiHasRoute = d.navTotal > 0;
  uiPortalOn = d.portal;

#if ENABLE_TOUCH
  // Dopo una taratura lo schermo e' stato riempito dai mirini: ridisegno tutto.
  if (touchRitarato) {
    touchRitarato = false;
    uiDirty       = true;
    displayScreen = SCREEN_NONE;
  }
#endif

  if (uiPage != PAGE_LIVE) {
    if (!uiDirty) return;             // le pagine sono ferme: si disegnano una volta
    uiDirty = false;
    uiSelAggiorna();
    switch (uiPage) {
      case PAGE_MENU:    drawMenu(d);       break;
      case PAGE_ROUTE:   drawRoutePage(d);  break;
      case PAGE_PHONE:   drawPhonePage(d);  break;
      case PAGE_TRIP:    drawTripPage(d);   break;
      case PAGE_CONFIRM: drawConfirmPage(); break;
      default: break;
    }
    return;
  }

  const DisplayScreen wanted = !d.fixValid          ? SCREEN_WAIT
                             : d.navActive          ? SCREEN_NAV
                                                    : SCREEN_DRIVE;
  const bool force = (wanted != displayScreen);

  if (force) {
    displayScreen = wanted;
    if      (wanted == SCREEN_NAV)   drawNavChrome();
    else if (wanted == SCREEN_DRIVE) drawDriveChrome();
    else                             drawWaitChrome();
  }

  if      (wanted == SCREEN_NAV)   drawNav(d, force);
  else if (wanted == SCREEN_DRIVE) drawDrive(d, force);
  else                             drawWait(d, force);
}

#endif  // ENABLE_DISPLAY
