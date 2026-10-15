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

#if PIN_LED == 2
  #error "Con il display GPIO2 e' il pin DC: in config.h sposta PIN_LED su 32"
#endif
#if PIN_GPS_PPS == 4
  #error "Con il display GPIO4 e' il pin RESET: in config.h metti PIN_GPS_PPS a -1"
#endif

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <time.h>

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
  const char *netName;
  bool        portal;
};

static TFT_eSPI tft;

static const uint16_t COL_BG    = TFT_BLACK;
static const uint16_t COL_FG    = TFT_WHITE;
static const uint16_t COL_LABEL = TFT_DARKGREY;
static const uint16_t COL_LINE  = 0x2104;        // grigio molto scuro
static const uint16_t COL_OK    = TFT_GREEN;
static const uint16_t COL_BAD   = TFT_RED;
static const uint16_t COL_WARN  = TFT_ORANGE;

enum DisplayScreen { SCREEN_NONE, SCREEN_WAIT, SCREEN_DRIVE };
static DisplayScreen displayScreen = SCREEN_NONE;

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
// Schermata di attesa del fix
// ---------------------------------------------------------------------------
static void drawWaitChrome() {
  tft.fillScreen(COL_BG);
  tft.drawFastHLine(0, 38, 480, COL_LINE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString("ESP32 GPS Tracker", 240, 19, 2);
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

  if (force) {
    displayField("l'antenna deve vedere il cielo", 240, 230, 2, 460, COL_LABEL);
    snprintf(buf, sizeof(buf), "%s %s", d.portal ? "access point:" : "rete:", d.netName);
    displayField(buf, 240, 285, 2, 460, d.portal ? COL_WARN : COL_LABEL);
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

  static const char *labels[4] = { "VIAGGIO", "MAX", "IN MOVIMENTO", "QUOTA" };
  for (uint8_t i = 0; i < 4; i++) tft.drawString(labels[i], 60 + i * 120, 268, 2);
}

static void drawDrive(const DisplayData &d, bool force) {
  static int32_t pSpeed = -1, pSat = -1, pDist = -1, pTrip = -1;
  static int32_t pMax = -1, pMov = -1, pAlt = -99999, pHdop = -1;
  static char    pCard[6] = "", pClock[8] = "";
  static int8_t  pFence = -1;
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

  // --- banda alta: stato area, satelliti, HDOP, orologio ---
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
static void displayBegin() {
  tft.init();
  tft.setRotation(DISPLAY_ROTATION);
  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_FG, COL_BG);
  tft.drawString("ESP32 GPS Tracker", 240, 150, 4);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString("avvio...", 240, 190, 2);
}

static void displayUpdate(const DisplayData &d) {
  const DisplayScreen wanted = d.fixValid ? SCREEN_DRIVE : SCREEN_WAIT;
  const bool force = (wanted != displayScreen);

  if (force) {
    displayScreen = wanted;
    if (wanted == SCREEN_DRIVE) drawDriveChrome();
    else                        drawWaitChrome();
  }

  if (wanted == SCREEN_DRIVE) drawDrive(d, force);
  else                        drawWait(d, force);
}

#endif  // ENABLE_DISPLAY
