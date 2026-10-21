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
static bool     touchRitarato = false;  // appena tarato: la pagina va ridisegnata
static bool     touchDown    = false;   // dito gia' appoggiato al giro prima
static uint32_t touchLastMs  = 0;

// La taratura sta in NVS, che sopravvive al cambio di partizioni e agli
// aggiornamenti: si fa una volta nella vita della scheda.
//
// Una per formato, pero': i numeri della taratura dicono dove finisce il dito
// RISPETTO AI PIXEL, e girando il pannello i pixel si spostano. Usarne una
// sola vorrebbe dire ritarare ogni volta che si cambia formato; cosi' invece
// ognuno si ricorda la sua, e si tara una volta per ciascuno.
static const char *touchChiaveCal() {
  static char chiave[8];
  snprintf(chiave, sizeof(chiave), "cal%u", (unsigned)(uiRot & 3));
  return chiave;
}

static void touchSaveCal() {
  Preferences p;
  if (!p.begin("touch", false)) return;
  p.putBytes(touchChiaveCal(), touchCal, sizeof(touchCal));
  p.end();
}

static bool touchLoadCal() {
  Preferences p;
  if (!p.begin("touch", true)) return false;
  const char *chiave = touchChiaveCal();
  bool ok = p.isKey(chiave) &&
            p.getBytes(chiave, touchCal, sizeof(touchCal)) == sizeof(touchCal);
  // Chi aggiorna da una versione precedente ha una taratura sola, senza
  // numero: vale per il formato con cui e' stata fatta, cioe' quello di
  // partenza. Si riusa invece di far ritarare per niente.
  if (!ok && (uiRot & 3) == (DISPLAY_ROTATION & 3))
    ok = p.isKey("cal") && p.getBytes("cal", touchCal, sizeof(touchCal)) == sizeof(touchCal);
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
  bool stretta = false;
  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_FG, COL_BG);
  tft.drawString("Taratura del tocco", SW / 2, 110, 4);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString("tocca la PUNTA della freccia in ogni angolo", SW / 2, 150, 2);
  tft.setTextColor(COL_WARN, COL_BG);
  tft.drawString("piu' ti avvicini al bordo, piu' viene precisa", SW / 2, 178, 2);
  delay(1800);

  tft.calibrateTouch(touchCal, COL_WARN, COL_BG, 15);
  tft.setTouch(touchCal);
  touchSaveCal();
  touchReady    = true;
  touchRitarato = true;
  touchDown     = true;          // il dito e' ancora appoggiato: non e' un tocco
  touchLastMs   = millis();
  Serial.printf("[TOUCH] tarato: %u %u %u %u (rot %u)\r\n",
                touchCal[0], touchCal[1], touchCal[2], touchCal[3], touchCal[4]);

  // Ampiezze piccole = angoli toccati troppo dentro: meta' schermo finirebbe
  // "fuori" e i tocchi verrebbero scartati. Meglio dirlo subito che lasciare
  // l'utente a chiedersi perche' il menu risponde a sprazzi.
  stretta = (touchCal[1] < 1500 || touchCal[3] < 1500);

  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  if (stretta) {
    Serial.println(F("[TOUCH] ampiezze piccole: hai toccato troppo dentro, meta' schermo "
                     "restera' insensibile. Tieni premuto BOOT 3 secondi e rifalla."));
    tft.setTextColor(COL_WARN, COL_BG);
    tft.drawString("taratura imprecisa", SW / 2, 130, 4);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.drawString("hai toccato troppo dentro: tieni premuto BOOT 3 secondi", SW / 2, 168, 2);
    tft.drawString("e rifalla toccando proprio sulla punta delle frecce", SW / 2, 192, 2);
    delay(3500);
  } else {
    tft.setTextColor(COL_OK, COL_BG);
    tft.drawString("fatto", SW / 2, 150, 4);
    delay(600);
  }
}

// Il quinto numero della taratura e' la rotazione per cui e' stata fatta:
// TFT_eSPI lo usa per sapere quali assi scambiare. Una taratura fatta da
// sdraiato, usata con il pannello in piedi, non "sbaglia un po'": manda il
// dito da tutt'altra parte. In alto non succede niente, in basso ti apre il
// riquadro sbagliato, e sembra che il tocco sia rotto.
static bool touchCalGiusta() {
  if (touchCal[4] == uiRot) return true;
  Serial.printf("[TOUCH] taratura fatta per la rotazione %u, adesso siamo alla %u: "
                "si butta e si rifa'\r\n", touchCal[4], uiRot);
  return false;
}

static void touchBegin() {
  touchProbe();

  if (touchLoadCal() && touchCalSensata() && touchCalGiusta()) {
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
  tft.drawString("Taratura del tocco", SW / 2, SH / 3, 4);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString("tocca lo schermo per cominciare", SW / 2, SH / 3 + 40, 2);
  tft.drawString(uiVert ? "schermo in piedi" : "schermo sdraiato", SW / 2, SH / 3 + 70, 2);
  tft.setTextColor(COL_WARN, COL_BG);
  tft.drawString("oppure tieni premuto il tasto 3 secondi", SW / 2, SH - 40, 2);

  // Un minuto invece di quindici secondi: cambiando formato la taratura va
  // rifatta per forza, e perdere la finestra vuol dire ritrovarsi senza tocco
  // senza capire perche'.
  if (touchAttendiDito(60000)) {
    touchRunCalibration();
  } else {
    touchReady = false;
    Serial.printf("[TOUCH] nessuna pressione in 60 secondi (soglia %u, riposo %u): il "
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

  // Senza una taratura valida il tocco non puo' funzionare, e all'accensione
  // c'era da azzeccare una finestra di quindici secondi. Meglio cosi': la
  // taratura parte da sola alla prima pressione vera, quando capita.
  if (!touchReady) {
    if (tft.getTouchRawZ() > touchSogliaDito()) {
      Serial.println(F("[TOUCH] pressione rilevata: parte la taratura"));
      touchRunCalibration();
    }
    return false;
  }


#if TOUCH_DEBUG
  // Con TOUCH_DEBUG a 1 il log dice cosa legge davvero il controller: se
  // premendo i numeri non si muovono, il problema e' prima della taratura.
  // Mentre premi stampa fitto, da fermo ogni tre secondi: altrimenti le
  // righe del GPS si perdono in mezzo e non si legge piu' niente.
  {
    static uint32_t ultimoLog = 0;
    const uint16_t z       = tft.getTouchRawZ();
    const bool     dito    = z > touchSogliaDito();
    const uint32_t ogni    = dito ? 300 : 3000;
    if (millis() - ultimoLog >= ogni) {
      ultimoLog = millis();
      uint16_t gx = 0, gy = 0;
      tft.getTouchRaw(&gx, &gy);
      uint16_t cx = 0, cy = 0;
      const bool preso = touchReady && tft.getTouch(&cx, &cy);
      Serial.printf("[TOUCH] %s z=%4u (soglia %u)  grezzo x=%4u y=%4u  tarato %s x=%u y=%u\r\n",
                    dito ? "DITO " : "fermo", z, touchSogliaDito(), gx, gy,
                    preso ? "SI" : "no", cx, cy);
    }
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
