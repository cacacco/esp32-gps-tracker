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
#include "version.h"

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
  uint32_t    satellites;    // quelli usati per la posizione
  uint8_t     satInVista;    // quelli che si vedono, anche senza fix
  uint8_t     snr;           // segnale del piu' forte, dB
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
  const char *netLista[WIFI_MAX_RETI];   // le reti ricordate, in ordine di prova
  uint8_t     netCount;
  uint8_t     blPercento;    // luminosita' adesso, in percento
  uint8_t     blModo;        // 0 automatica, 1 giorno, 2 notte, 3 a mano
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

  // Qui serve quello che si VEDE, non quello che entra nel calcolo: senza
  // fix il secondo e' sempre zero e non distingue un guasto dal maltempo.
  if (force || (int32_t)d.satInVista != pSat) {
    pSat = (int32_t)d.satInVista;
    if (d.satInVista) snprintf(buf, sizeof(buf), "%u satelliti in vista, segnale %u dB",
                               d.satInVista, d.snr);
    else              snprintf(buf, sizeof(buf), "nessun satellite in vista");
    displayField(buf, 240, 160, 2, 460, d.satInVista ? COL_LABEL : COL_WARN);
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
static const int SV_X = 386, SV_Y = 104, SV_R = 30;     // freccia della svolta
static const int PO_X = 342, PO_Y = 242, PO_R = 15;     // freccia del "dopo"

static void drawNavChrome() {
  tft.fillScreen(COL_BG);
  tft.drawFastHLine(0, 38, 480, COL_LINE);
  tft.drawFastVLine(292, 46, 270, COL_LINE);
  tft.drawFastHLine(296, 196, 184, COL_LINE);
  tft.drawFastHLine(296, 262, 184, COL_LINE);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_LABEL, COL_BG);
#if ENABLE_TOUCH
  drawMenuHint(260);
#endif
  tft.drawString("RESTANO", 144, 238, 2);
}

static void drawNav(const DisplayData &d, bool force) {
  static int32_t pAngle = -9999, pRemain = -1, pSpeed = -1, pTurnM = -1, pPoiM = -1;
  static int8_t  pState = -1, pMode = -1, pVicina = -1;
  static int32_t pTurnA = -9999, pPoiA = -9999;
  static char    pLinea[40] = "";
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
  else                    { linea[0] = '\0'; }

  // ---- riquadro SVOLTA ----------------------------------------------------
  const bool    haSvolta = d.navTurnKind != TURN_NONE && !d.navFinished;
  const int8_t  modo     = haSvolta ? 0 : 1;          // 0 = svolta, 1 = tappa
  const bool    vicina   = haSvolta && d.navTurnM <= TURN_WARN_M;

  if (force || modo != pMode) {
    pMode = modo;
    displayField(haSvolta ? "SVOLTA" : "PROSSIMA TAPPA", 386, 54, 2, 180, COL_LABEL);
    pTurnA = -9999; pTurnM = -1; pLinea[0] = '\0';
  }

  // la cornice arancione dice "ci sei quasi" senza bisogno di leggere i numeri
  const bool cambiaVicina = force || (int8_t)vicina != pVicina;
  if (cambiaVicina) tft.drawRoundRect(296, 44, 180, 152, 8, vicina ? COL_WARN : COL_BG);

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
    // scritto grande il doppio, deve stare in 184 pixel: oltre i 10 km si
    // perdono i decimali, che a quella distanza non servono a nessuno
    if (!haSvolta && !d.navStops)    snprintf(buf, sizeof(buf), "--");
    else if (distSvolta < 1000)      snprintf(buf, sizeof(buf), "%ld m", (long)distSvolta);
    else if (distSvolta < 10000)     snprintf(buf, sizeof(buf), "%.1f km", distSvolta / 1000.0f);
    else                             snprintf(buf, sizeof(buf), "%ld km", (long)(distSvolta / 1000.0f));
    tft.setTextSize(2);
    displayField(buf, 386, 164, 4, 184, vicina ? COL_WARN : COL_FG);
    tft.setTextSize(1);
  }

  // nome: la strada su cui si prosegue, o la tappa se non ci sono svolte
  char strada[28];
  if (haSvolta && d.navTurnKind == TURN_ROUNDABOUT && d.navTurnExit)
    snprintf(strada, sizeof(strada), "%u uscita: %s", d.navTurnExit, d.navTurnName ? d.navTurnName : "");
  else if (haSvolta)
    snprintf(strada, sizeof(strada), "%s %s", turnWord(d.navTurnKind), d.navTurnName ? d.navTurnName : "");
  else
    snprintf(strada, sizeof(strada), "%s", d.navStops ? (d.navStopName ? d.navStopName : "--") : "nessuna");
  // La riga larga sotto la freccia: l'avviso se c'e', altrimenti la strada.
  // Li' ci stanno 280 pixel invece di 180, e i nomi delle vie sono lunghi.
  const char *basso = linea[0] ? linea : strada;
  if (force || strcmp(basso, pLinea) != 0) {
    snprintf(pLinea, sizeof(pLinea), "%s", basso);
    displayField(basso, 144, 302, 2, 280, linea[0] ? coloreLinea : COL_LABEL);
  }

  // ---- riquadro POI -------------------------------------------------------
  const bool    haPoi  = d.navTurn2Kind != TURN_NONE && !d.navFinished;
  const int32_t poiA   = haPoi ? (int32_t)turnAngle(d.navTurn2Kind) : -9999;
  if (force || poiA != pPoiA) {
    pPoiA = poiA;
    displayField(haPoi ? "DOPO" : "", 386, 210, 2, 180, COL_LABEL);
    tft.fillCircle(PO_X, PO_Y, PO_R + 2, COL_BG);
    if (haPoi) drawArrowAt(PO_X, PO_Y, PO_R, (float)poiA, COL_LABEL);
  }

  const int32_t poiM = haPoi ? (int32_t)d.navTurn2M : -1;
  if (force || poiM != pPoiM) {
    pPoiM = poiM;
    if (!haPoi)                 buf[0] = '\0';
    else if (d.navTurn2M < 1000) snprintf(buf, sizeof(buf), "%ld m", (long)d.navTurn2M);
    else                         snprintf(buf, sizeof(buf), "%.1f km", d.navTurn2M / 1000.0f);
    displayField(buf, 414, 242, 2, 100, COL_LABEL);
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
  PAGE_NEWROUTE,      // aspetta che il telefono mandi un percorso
  PAGE_PHONE,
  PAGE_NETS,          // sceglie a quale rete ricordata attaccarsi
  PAGE_LUCE,          // regola la luminosita' a mano
  PAGE_TRIP,
  PAGE_CONFIRM
};

enum UiAction : uint8_t {
  UI_NONE = 0, UI_ROUTE_CLEAR, UI_TRACK_CLEAR, UI_TRIP_RESET,
  UI_PORTAL_ON, UI_PORTAL_OFF, UI_NET_SELECT,
  UI_BL_GIU, UI_BL_SU, UI_BL_AUTO
};

// quale rete e' stata toccata: l'azione da sola non basta a dirlo
static uint8_t uiNetScelta = 0;

struct Rect { int16_t x, y, w, h; };

static bool rectHit(const Rect &r, int16_t x, int16_t y) {
  return x >= r.x && x < (int16_t)(r.x + r.w) && y >= r.y && y < (int16_t)(r.y + r.h);
}

// due colonne per due righe, margini uguali da tutti i lati
static const Rect MENU_BTN[4] = {
  { 14,  50, 221, 100}, {245,  50, 221, 100},
  { 14, 156, 221, 100}, {245, 156, 221, 100}
};
// la quinta voce del menu: una barra larga sotto i quattro quadrati
static const Rect MENU_LUCE = {14, 262, 452, 52};
// i due tasti della pagina Luce: grandi, si centrano col guanto
static const Rect LUCE_BTN[2] = { {14, 56, 221, 110}, {245, 56, 221, 110} };
// tre righe larghe per le reti ricordate, sopra la barra dei pulsanti
static const uint8_t UI_NET_RIGHE = 3;
static const Rect NET_BTN[UI_NET_RIGHE] = {
  {14, 52, 452, 56}, {14, 116, 452, 56}, {14, 180, 452, 56}
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
static uint8_t   uiNetCount = 0;                  // quante reti ricorda: serve al tocco
static bool      uiBlManuale = false;             // luce regolata a mano
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
#if PIN_MENU_BUTTON == 0
  tft.drawString("tasto BOOT: breve sposta, lungo conferma", 466, 19, 2);
#else
  tft.drawString("tasto: breve sposta, lungo conferma", 466, 19, 2);
#endif
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

#if PIN_TFT_BL >= 0
  snprintf(sotto, sizeof(sotto), "Luce   %u%%   %s", d.blPercento,
           d.blModo == 3 ? "a mano" : d.blModo == 1 ? "giorno" : d.blModo == 2 ? "notte"
                                                                                : "automatica");
  drawButton(MENU_LUCE, sotto, "", COL_LABEL);
#endif
}

#if PIN_TFT_BL >= 0
// Due pulsanti grandi e niente altro: si usa col guanto, di notte, fermi al
// semaforo. Il livello si vede anche da come cambia lo schermo mentre premi.
static void drawLucePage(const DisplayData &d) {
  char buf[16];
  drawPageHead("LUCE");

  drawButton(LUCE_BTN[0], "-", "piu' scura", COL_LABEL);
  drawButton(LUCE_BTN[1], "+", "piu' chiara", COL_LABEL);

  snprintf(buf, sizeof(buf), "%u%%", d.blPercento);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_FG, COL_BG);
  tft.setTextSize(2);
  displayField(buf, 240, 200, 4, 200, COL_FG);
  tft.setTextSize(1);

  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString(d.blModo == 3 ? "regolata a mano" : "si regola da sola con l'ora", 240, 240, 2);

  drawButton(PAGE_BTN[0], "Indietro", "", COL_LINE);
  if (d.blModo == 3) drawButton(PAGE_BTN[2], "Automatica", "", COL_OK);
}
#endif

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

  drawButton(PAGE_BTN[0], "Indietro", "", COL_LINE);
  drawButton(PAGE_BTN[1], "Nuovo", "", COL_CYAN);
  if (d.navTotal) drawButton(PAGE_BTN[2], "Cancella", "", COL_BAD);
}

// Il percorso si disegna sul telefono, non qui: lo schermo e' piccolo e senza
// mappa. Questa pagina fa l'unica cosa che serve davvero - dirti dove andare a
// scriverlo - e poi resta a guardare finche' il percorso non arriva.
static void drawNewRoutePage(const DisplayData &d) {
  char buf[52];
  drawPageHead("NUOVO PERCORSO");
  tft.setTextDatum(MC_DATUM);

  const bool inRete = d.portal || (d.ipText && *d.ipText);

  if (!inRete) {
    tft.setTextColor(COL_BAD, COL_BG);
    tft.drawString("serve il telefono collegato", 240, 100, 4);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.drawString("accendi la rete del tracker, collega il telefono,", 240, 148, 2);
    tft.drawString("e l'indirizzo compare qui da solo", 240, 172, 2);
    drawButton(PAGE_BTN[2], "Accendi", "", COL_OK);
  } else {
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.drawString("apri questo indirizzo sul telefono", 240, 72, 2);
    snprintf(buf, sizeof(buf), "http://%s/percorso",
             d.portal ? "192.168.4.1" : (d.ipText ? d.ipText : "--"));
    tft.setTextColor(COL_OK, COL_BG);
    tft.drawString(buf, 240, 108, 4);

    if (d.portal) {
      tft.setTextColor(COL_WARN, COL_BG);
      tft.drawString("sulla rete del tracker non c'e' internet:", 240, 150, 2);
      tft.drawString("le strade e le svolte non si possono calcolare", 240, 172, 2);
    } else {
      tft.setTextColor(COL_LABEL, COL_BG);
      tft.drawString("spunta \"segui le strade\" per avere le svolte", 240, 152, 2);
    }

    if (d.navTotal) {
      snprintf(buf, sizeof(buf), "in memoria: %.1f km, %u svolte", d.routeKm, d.routeTurns);
      tft.setTextColor(COL_FG, COL_BG);
    } else {
      snprintf(buf, sizeof(buf), "nessun percorso in memoria");
      tft.setTextColor(COL_LABEL, COL_BG);
    }
    tft.drawString(buf, 240, 210, 2);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.drawString("questa riga cambia da sola appena salvi", 240, 232, 2);
  }

  drawButton(PAGE_BTN[0], "Indietro", "", COL_LINE);
}

// Le reti che il tracker ricorda: una per riga, si tocca quella che vuoi.
static void drawNetsPage(const DisplayData &d) {
  drawPageHead("RETI");

  if (!d.netCount) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.drawString("nessuna rete salvata", 240, 120, 4);
    tft.drawString("salvane una dal telefono, pagina /wifi", 240, 160, 2);
  }

  const uint8_t quante = d.netCount < UI_NET_RIGHE ? d.netCount : UI_NET_RIGHE;
  for (uint8_t i = 0; i < quante; i++) {
    const char *nome = d.netLista[i] ? d.netLista[i] : "--";
    // quella attiva si riconosce senza leggere: e' l'unica col bordo acceso
    const bool attiva = d.ipText && *d.ipText && d.netName && strcmp(nome, d.netName) == 0;
    drawButton(NET_BTN[i], nome, attiva ? "collegato" : "tocca per collegarti",
               attiva ? COL_OK : COL_LINE);
  }

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString("il collegamento puo' chiedere qualche secondo", 240, 248, 2);

  drawButton(PAGE_BTN[0], "Indietro", "", COL_LINE);
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

  // la carta d'identita' del firmware: qui si legge con calma, lo splash dura un attimo
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_FG, COL_BG);
  tft.drawString("firmware  " FW_VERSIONE, 240, 232, 2);

  drawButton(PAGE_BTN[0], "Indietro", "", COL_LINE);
  if (d.netCount > 1) drawButton(PAGE_BTN[1], "Reti", "", COL_CYAN);
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
#if PIN_TFT_BL >= 0
      else if (rectHit(MENU_LUCE, x, y))   uiGoto(PAGE_LUCE);
#endif
      return UI_NONE;

#if PIN_TFT_BL >= 0
    case PAGE_LUCE:
      if (rectHit(PAGE_BTN[0], x, y)) { uiGoto(PAGE_MENU); return UI_NONE; }
      if (rectHit(LUCE_BTN[0], x, y)) { uiDirty = true; return UI_BL_GIU; }
      if (rectHit(LUCE_BTN[1], x, y)) { uiDirty = true; return UI_BL_SU; }
      if (rectHit(PAGE_BTN[2], x, y)) { uiDirty = true; return UI_BL_AUTO; }
      return UI_NONE;
#endif

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
      else if (rectHit(PAGE_BTN[1], x, y)) uiGoto(PAGE_NEWROUTE);
      else if (rectHit(PAGE_BTN[2], x, y)) uiAsk(UI_ROUTE_CLEAR, "Cancellare il percorso?");
      return UI_NONE;

    case PAGE_NEWROUTE:
      if (rectHit(PAGE_BTN[0], x, y)) { uiGoto(PAGE_ROUTE); return UI_NONE; }
      if (rectHit(PAGE_BTN[2], x, y) && !uiPortalOn) { uiDirty = true; return UI_PORTAL_ON; }
      return UI_NONE;

    case PAGE_TRIP:
      if (rectHit(PAGE_BTN[0], x, y)) { uiGoto(PAGE_MENU); return UI_NONE; }
      if (rectHit(PAGE_BTN[1], x, y)) { uiDirty = true;  return UI_TRIP_RESET; }
      if (rectHit(PAGE_BTN[2], x, y)) uiAsk(UI_TRACK_CLEAR, "Cancellare la traccia?");
      return UI_NONE;

    case PAGE_PHONE:
      if (rectHit(PAGE_BTN[0], x, y)) { uiGoto(PAGE_MENU); return UI_NONE; }
      if (rectHit(PAGE_BTN[1], x, y) && uiNetCount > 1) { uiGoto(PAGE_NETS); return UI_NONE; }
      if (rectHit(PAGE_BTN[2], x, y)) { uiDirty = true; return uiPortalOn ? UI_PORTAL_OFF : UI_PORTAL_ON; }
      return UI_NONE;

    case PAGE_NETS:
      if (rectHit(PAGE_BTN[0], x, y)) { uiGoto(PAGE_PHONE); return UI_NONE; }
      for (uint8_t i = 0; i < UI_NET_RIGHE && i < uiNetCount; i++) {
        if (!rectHit(NET_BTN[i], x, y)) continue;
        uiNetScelta = i;
        uiGoto(PAGE_PHONE);                    // al ritorno mostra com'e' andata
        return UI_NET_SELECT;
      }
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
#if PIN_TFT_BL >= 0
      uiSelLista[uiSelCount++] = &MENU_LUCE;
#endif
      break;
#if PIN_TFT_BL >= 0
    case PAGE_LUCE:
      uiSelLista[uiSelCount++] = &LUCE_BTN[0];
      uiSelLista[uiSelCount++] = &LUCE_BTN[1];
      uiSelLista[uiSelCount++] = &PAGE_BTN[0];
      if (uiBlManuale) uiSelLista[uiSelCount++] = &PAGE_BTN[2];
      break;
#endif
    case PAGE_ROUTE:
      uiSelLista[uiSelCount++] = &PAGE_BTN[0];
      uiSelLista[uiSelCount++] = &PAGE_BTN[1];
      if (uiHasRoute) uiSelLista[uiSelCount++] = &PAGE_BTN[2];
      break;
    case PAGE_NEWROUTE:
      uiSelLista[uiSelCount++] = &PAGE_BTN[0];
      if (!uiPortalOn) uiSelLista[uiSelCount++] = &PAGE_BTN[2];
      break;
    case PAGE_PHONE:
      uiSelLista[uiSelCount++] = &PAGE_BTN[0];
      if (uiNetCount > 1) uiSelLista[uiSelCount++] = &PAGE_BTN[1];
      uiSelLista[uiSelCount++] = &PAGE_BTN[2];
      break;
    case PAGE_NETS:
      uiSelLista[uiSelCount++] = &PAGE_BTN[0];
      for (uint8_t i = 0; i < UI_NET_RIGHE && i < uiNetCount; i++)
        uiSelLista[uiSelCount++] = &NET_BTN[i];
      break;
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
  tft.setTextColor(COL_LINE, COL_BG);
  tft.drawString(FW_VERSIONE, 240, 308, 1);

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

#if PIN_TFT_BL >= 0
#define BL_CANALE 7                              // canale PWM, sui core Arduino 2.x
// La luce del pannello: di giorno al massimo, di notte abbassata. L'ora la da'
// il GPS, quindi non serve nessun orologio a bordo. "auto" si puo' forzare dal
// telefono quando il cielo non e' d'accordo con l'orologio.
static uint8_t blPercento = 255;                 // 255 = non ancora impostata
static uint8_t blModo     = 0;      // 0 = automatica, 1 = giorno, 2 = notte, 3 = a mano
static uint8_t blManuale  = BL_GIORNO;           // il livello scelto col display

static void blApplica(uint8_t percento) {
  if (percento > 100) percento = 100;
  blPercento = percento;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_TFT_BL, (uint32_t)percento * 255 / 100);
#else
  ledcWrite(BL_CANALE, (uint32_t)percento * 255 / 100);
#endif
}

static void blBegin() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_TFT_BL, 5000, 8);
#else
  ledcSetup(BL_CANALE, 5000, 8);
  ledcAttachPin(PIN_TFT_BL, BL_CANALE);
#endif
  blApplica(BL_GIORNO);                          // si accende sempre al massimo
}

static bool blEnotte(uint32_t epoch) {
  const time_t t = (time_t)epoch + (time_t)TIME_UTC_OFFSET_MIN * 60;
  struct tm ora;
  gmtime_r(&t, &ora);                            // l'offset e' gia' dentro
  return (BL_NOTTE_DA > BL_NOTTE_A)              // la notte scavalca mezzanotte
       ? (ora.tm_hour >= BL_NOTTE_DA || ora.tm_hour < BL_NOTTE_A)
       : (ora.tm_hour >= BL_NOTTE_DA && ora.tm_hour < BL_NOTTE_A);
}

// Un gradino per volta, mai sotto il 10%: uno schermo nero non si recupera
// piu', perche' per rialzare la luce dovresti vedere i pulsanti.
static void blPasso(int8_t verso) {
  int16_t v = (int16_t)(blModo == 3 ? blManuale : blPercento) + verso * 10;
  if (v > 100) v = 100;
  if (v < 10)  v = 10;
  blManuale = (uint8_t)v;
  blModo    = 3;
  blApplica(blManuale);
}

static void blAggiorna(uint32_t epoch) {
  uint8_t voluta;
  if (blModo == 1)      voluta = BL_GIORNO;
  else if (blModo == 2) voluta = BL_NOTTE;
  else if (blModo == 3) voluta = blManuale;
  else if (!epoch)      return;                  // senza ora non si indovina
  else                  voluta = blEnotte(epoch) ? BL_NOTTE : BL_GIORNO;
  if (voluta != blPercento) blApplica(voluta);
}
#endif

static void displayBegin() {
#if PIN_TFT_BL >= 0
  blBegin();                                     // prima di tutto: niente buio
#endif
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
#if PIN_TFT_BL >= 0
  blAggiorna(d.epoch);
#endif
  // Se non tocchi niente il menu si fa da parte: senza touch collegato il
  // tracker si comporta come prima, dopo MENU_TIMEOUT_MS e' in navigazione.
  if (uiPage == PAGE_MENU && millis() - uiTouchMs > MENU_TIMEOUT_MS) uiGoto(PAGE_LIVE);

  uiHasRoute = d.navTotal > 0;
  uiPortalOn = d.portal;
  uiNetCount = d.netCount;
  uiBlManuale = (d.blModo == 3);

#if ENABLE_TOUCH
  // Dopo una taratura lo schermo e' stato riempito dai mirini: ridisegno tutto.
  if (touchRitarato) {
    touchRitarato = false;
    uiDirty       = true;
    displayScreen = SCREEN_NONE;
  }
#endif

  if (uiPage != PAGE_LIVE) {
    if (uiPage == PAGE_NEWROUTE) {    // questa invece aspetta: la rete o il percorso
      static uint16_t ultimiPunti = 0xFFFF;
      static uint8_t  ultimaRete  = 0xFF;
      const uint8_t rete = d.portal ? 2 : (d.ipText && *d.ipText) ? 1 : 0;
      if (d.navTotal != ultimiPunti || rete != ultimaRete) {
        ultimiPunti = d.navTotal; ultimaRete = rete; uiDirty = true;
      }
    }
    if (!uiDirty) return;             // le altre sono ferme: si disegnano una volta
    uiDirty = false;
    uiSelAggiorna();
    switch (uiPage) {
      case PAGE_MENU:    drawMenu(d);       break;
      case PAGE_ROUTE:   drawRoutePage(d);  break;
      case PAGE_NEWROUTE: drawNewRoutePage(d); break;
      case PAGE_PHONE:   drawPhonePage(d);  break;
      case PAGE_NETS:    drawNetsPage(d);   break;
#if PIN_TFT_BL >= 0
      case PAGE_LUCE:    drawLucePage(d);   break;
#endif
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
