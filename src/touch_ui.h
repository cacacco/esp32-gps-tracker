#pragma once
// ---------------------------------------------------------------------------
// Tocco sul pannello.
//
// Incluso da display_ui.h subito dopo l'oggetto tft, di cui si serve: il
// controller del touch sta sullo stesso bus SPI del display e TFT_eSPI lo
// gestisce da solo, basta dirgli con che CS parlarci.
//
// La taratura (dove finisce il dito rispetto ai pixel) cambia da pannello a
// pannello: si fa una volta toccando i quattro angoli e resta in memoria.
// ---------------------------------------------------------------------------

#if ENABLE_TOUCH
#ifndef TOUCH_CS
  #error "ENABLE_TOUCH e' a 1 ma manca TOUCH_CS: aggiungilo ai build_flags"
#endif

#include <Preferences.h>

static uint16_t touchCal[5]  = {0, 0, 0, 0, 0};
static bool     touchReady   = false;
static bool     touchDown    = false;   // dito gia' appoggiato al giro prima
static uint32_t touchLastMs  = 0;

// La taratura sta in NVS, che sopravvive al cambio di partizioni e agli
// aggiornamenti: si fa una volta nella vita della scheda.
static void touchSaveCal() {
  Preferences p;
  if (!p.begin("touch", false)) return;
  p.putBytes("cal", touchCal, sizeof(touchCal));
  p.end();
}

static bool touchLoadCal() {
  Preferences p;
  if (!p.begin("touch", true)) return false;
  const bool ok = p.isKey("cal") &&
                  p.getBytes("cal", touchCal, sizeof(touchCal)) == sizeof(touchCal);
  p.end();
  return ok;
}

static void touchRunCalibration() {
  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_FG, COL_BG);
  tft.drawString("Taratura del tocco", 240, 120, 4);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString("tocca la punta della freccia in ogni angolo", 240, 160, 2);
  delay(1200);

  tft.calibrateTouch(touchCal, COL_WARN, COL_BG, 15);
  tft.setTouch(touchCal);
  touchSaveCal();
  touchReady = true;

  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_OK, COL_BG);
  tft.drawString("fatto", 240, 150, 4);
  delay(600);
}

static void touchBegin() {
  if (touchLoadCal()) {
    tft.setTouch(touchCal);
    touchReady = true;
    Serial.println(F("[TOUCH] taratura caricata dalla memoria"));
  } else {
    Serial.println(F("[TOUCH] prima accensione: taratura del tocco"));
    touchRunCalibration();
  }
}

// Vero una volta sola per ogni appoggio del dito: il menu non deve scattare
// dieci volte mentre tieni premuto.
static bool touchPressed(int16_t &x, int16_t &y) {
  uint16_t tx = 0, ty = 0;
  const bool giu = touchReady && tft.getTouch(&tx, &ty);

  if (!giu) { touchDown = false; return false; }
  if (touchDown) return false;
  touchDown = true;

  const uint32_t ora = millis();
  if (ora - touchLastMs < TOUCH_MIN_GAP_MS) return false;
  touchLastMs = ora;

  x = (int16_t)tx;
  y = (int16_t)ty;
  return true;
}

#endif  // ENABLE_TOUCH
