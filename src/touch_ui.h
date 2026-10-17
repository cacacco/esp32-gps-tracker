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

#define STRINGA2(x) #x
#define STRINGA(x)  STRINGA2(x)

static uint16_t touchCal[5]  = {0, 0, 0, 0, 0};
static uint16_t touchZRiposo = 0;       // quanto legge il controller non premuto
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

// Nei parametri di TFT_eSPI il secondo e il quarto numero non sono estremi ma
// AMPIEZZE: quanto si sposta la lettura grezza da un bordo all'altro. Su un
// pannello sano valgono qualche migliaio. Se valgono 1 o 4 vuol dire che i
// quattro angoli hanno dato tutti la stessa lettura, cioe' che la taratura e'
// nata dal rumore e non dal dito.
static bool touchCalSensata() {
  const bool ok = touchCal[1] > 500 && touchCal[1] < 4096 &&
                  touchCal[3] > 500 && touchCal[3] < 4096;
  if (!ok)
    Serial.printf("[TOUCH] taratura da buttare (%u %u %u %u): le ampiezze %u e %u sono "
                  "troppo piccole, i quattro angoli hanno letto lo stesso punto\r\n",
                  touchCal[0], touchCal[1], touchCal[2], touchCal[3],
                  touchCal[1], touchCal[3]);
  return ok;
}

// Soglia oltre la quale una lettura e' un dito e non rumore di fondo.
static uint16_t touchSogliaDito() {
  const uint32_t tripla = (uint32_t)touchZRiposo * 3 + 150;
  return tripla > 300 ? (uint16_t)tripla : 300;
}

// Aspetta una pressione vera. Senza questa attesa, calibrateTouch resterebbe
// fermo per sempre sul primo angolo se il pannello non registra il dito, e
// con lui tutto il tracker: niente GPS, niente WiFi, niente di niente.
static bool touchAttendiDito(uint32_t msMax) {
  const uint16_t soglia = touchSogliaDito();
  const uint32_t fine   = millis() + msMax;
  while (millis() < fine) {
    if (tft.getTouchRawZ() > soglia) return true;
    delay(20);
  }
  return false;
}

// Chiede al controller la pressione, senza taratura di mezzo: serve solo a
// sapere se dall'altra parte del filo c'e' qualcuno.
static void touchProbe() {
  uint16_t minZ = 0xFFFF, maxZ = 0;
  for (uint8_t i = 0; i < 16; i++) {
    const uint16_t z = tft.getTouchRawZ();
    if (z < minZ) minZ = z;
    if (z > maxZ) maxZ = z;
    delay(5);
  }
  touchZRiposo = maxZ;
  const bool morto = (minZ == maxZ) && (minZ == 0 || minZ >= 4000);
  Serial.printf("[TOUCH] controller a riposo: z fra %u e %u%s\r\n", minZ, maxZ,
                morto ? "  -- non risponde: controlla T_CS su D"
                        STRINGA(TOUCH_CS) " e i ponti di T_CLK, T_DIN, T_DO" : "");
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
  touchProbe();

  if (touchLoadCal() && touchCalSensata()) {
    tft.setTouch(touchCal);
    touchReady = true;
    Serial.printf("[TOUCH] taratura caricata: %u %u %u %u (rot %u)\r\n",
                  touchCal[0], touchCal[1], touchCal[2], touchCal[3], touchCal[4]);
    return;
  }

  // Prima di iniziare la taratura mi assicuro che il pannello senta il dito:
  // se non lo sente, tararlo e' impossibile e bloccherebbe l'avvio.
  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_FG, COL_BG);
  tft.drawString("Taratura del tocco", 240, 120, 4);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString("tocca lo schermo per cominciare", 240, 160, 2);

  if (touchAttendiDito(15000)) {
    touchRunCalibration();
  } else {
    touchReady = false;
    Serial.printf("[TOUCH] nessuna pressione in 15 secondi (soglia %u, riposo %u): il "
                  "controller risponde ma il pannello non sente il dito. Guarda il "
                  "connettore del vetro touch sulla basetta, non i fili verso l'ESP32. "
                  "Il tracker prosegue senza tocco.\r\n",
                  touchSogliaDito(), touchZRiposo);
  }
}

// Vero una volta sola per ogni appoggio del dito: il menu non deve scattare
// dieci volte mentre tieni premuto.
static bool touchPressed(int16_t &x, int16_t &y) {
  uint16_t tx = 0, ty = 0;

#if TOUCH_DEBUG
  // Con TOUCH_DEBUG a 1 il log dice cosa legge davvero il controller: se
  // premendo i numeri non si muovono, il problema e' prima della taratura.
  static uint32_t ultimoLog = 0;
  if (millis() - ultimoLog >= 500) {
    ultimoLog = millis();
    uint16_t gx = 0, gy = 0;
    tft.getTouchRaw(&gx, &gy);
    uint16_t cx = 0, cy = 0;
    const bool preso = touchReady && tft.getTouch(&cx, &cy);
    Serial.printf("[TOUCH] grezzo x=%u y=%u z=%u | tarato %s x=%u y=%u\r\n",
                  gx, gy, tft.getTouchRawZ(), preso ? "SI" : "no", cx, cy);
  }
#endif

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
