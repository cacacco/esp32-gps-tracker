#pragma once
// ---------------------------------------------------------------------------
// Impaginazione: le stesse schermate su due formati.
//
// Il pannello si puo' montare sdraiato (480 x 320) o in piedi (320 x 480).
// Non cambia solo l'orientamento dell'immagine: cambia dove sta ogni cosa.
// Sdraiato la freccia grande sta a sinistra e la svolta a destra, in piedi
// la svolta sta in alto e la freccia sotto, perche' e' li' che c'e' posto.
//
// Tutte le coordinate delle schermate stanno qui dentro come variabili, non
// come numeri sparsi nel codice del disegno: cosi' esiste una sola copia di
// ogni misura, e cambiare formato vuol dire ricalcolarle, non riscrivere
// quattordici schermate.
//
// Il formato scelto resta in memoria NVS e sopravvive agli aggiornamenti.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

struct Rect { int16_t x, y, w, h; };

// ---- lo schermo ------------------------------------------------------------
static uint8_t uiRot  = DISPLAY_ROTATION;
static bool    uiVert = false;                 // in piedi
static int16_t SW = 480, SH = 320;

// ---- barra in alto ---------------------------------------------------------
// BAR_SAT_W e' quanto spazio si pulisce scrivendo i satelliti. Deve bastare
// alla scritta e fermarsi prima del tasto menu, altrimenti lo cancella.
static int16_t BAR_SAT_W, BAR_HINT;

// ---- schermata di attesa del fix -------------------------------------------
static int16_t WAIT_X, WAIT_W, WAIT_TY;
static int16_t WAIT_Y[6];      // titolo, satelliti, frasi, ricerca, antenna, rete

// ---- schermata di marcia ---------------------------------------------------
static int16_t DRV_HL1, DRV_HL2, DRV_VL, DRV_VL_Y, DRV_VL_H;
static int16_t DRV_SPD_X, DRV_SPD_Y, DRV_SPD_W, DRV_KMH_X, DRV_KMH_Y;
static int16_t DRV_DIR_X, DRV_DIR_LY, DRV_DIR_VY, DRV_DIR_W;
static int16_t DRV_BASE_X, DRV_BASE_LY, DRV_BASE_VY;
static int16_t DRV_ST_X[4], DRV_ST_LY[4], DRV_ST_VY[4], DRV_ST_W;
static int16_t DRV_HINT;

// ---- navigazione -----------------------------------------------------------
static int16_t ARROW_X, ARROW_Y, ARROW_R;
static int16_t SV_X, SV_Y, SV_R;               // freccia della svolta
static int16_t PO_X, PO_Y, PO_R;               // freccia del "dopo"
static Rect    SV_BOX;                         // cornice "ci sei quasi"
static int16_t NAV_VL, NAV_VL_Y, NAV_VL_H;     // divisorio verticale, -1 = niente
static int16_t NAV_HL[3], NAV_HL_X, NAV_HL_W;  // divisori orizzontali, -1 = niente
static int16_t NAV_LBL_X, NAV_LBL_Y, NAV_LBL_W;
static int16_t NAV_DST_X, NAV_DST_Y, NAV_DST_W;
static int16_t NAV_POI_LX, NAV_POI_LY, NAV_POI_DX, NAV_POI_DY, NAV_POI_DW;
static int16_t NAV_REST_X, NAV_REST_LY, NAV_REST_VY, NAV_REST_W;
static int16_t NAV_LINE_X, NAV_LINE_Y, NAV_LINE_W;
static int16_t NAV_SPD_X, NAV_SPD_Y, NAV_SPD_W;
static int16_t NAV_MARK_X;                     // cunei del cambio di via
static int16_t NAV_HINT;

// ---- menu e pagine ---------------------------------------------------------
#define UI_NET_RIGHE 3
static Rect MENU_BTN[4], MENU_LUCE, MENU_SCHERMO;
static Rect LUCE_BTN[2], MODO_BTN[3], SCHERMO_BTN[2];
static Rect NET_BTN[UI_NET_RIGHE], PAGE_BTN[3], ROUTE_RICALC, BARRA_MENU;
// La conferma ha i suoi due tasti e non usa quelli delle pagine: in piedi
// quelli si impilano, e "Si'" sarebbe finito sopra "No" con un buco in mezzo.
// Affiancati, No a sinistra e Si' a destra, il gesto e' lo stesso nei due
// formati e non si preme quello sbagliato.
static Rect CONF_NO, CONF_SI;
static int16_t CONF_Q_Y, CONF_SUB_Y;
static int16_t RIGA_LX, RIGA_VX, RIGA_Y[4], LUCE_PCT_Y;

// ---- logo di accensione ----------------------------------------------------
static int16_t LOGO_X, LOGO_Y, LOGO_R, ORB_RX, ORB_RY;

// ---------------------------------------------------------------------------
static void layoutOrizzontale() {
  SW = 480; SH = 320;

  BAR_SAT_W = 200; BAR_HINT = 260;
  WAIT_X = 240; WAIT_W = 460; WAIT_TY = 19;
  const int16_t wy[6] = {110, 160, 190, 230, 258, 292};
  for (uint8_t i = 0; i < 6; i++) WAIT_Y[i] = wy[i];

  DRV_HL1 = 250; DRV_HL2 = -1; DRV_VL = 316; DRV_VL_Y = 46; DRV_VL_H = 196;
  DRV_SPD_X = 158; DRV_SPD_Y = 140; DRV_SPD_W = 300; DRV_KMH_X = 158; DRV_KMH_Y = 212;
  DRV_DIR_X = 400; DRV_DIR_LY = 70; DRV_DIR_VY = 110; DRV_DIR_W = 150;
  DRV_BASE_X = 400; DRV_BASE_LY = 162; DRV_BASE_VY = 196;
  for (uint8_t i = 0; i < 4; i++) {
    DRV_ST_X[i] = (int16_t)(60 + i * 120); DRV_ST_LY[i] = 268; DRV_ST_VY[i] = 294;
  }
  DRV_ST_W = 110; DRV_HINT = BAR_HINT;

  ARROW_X = 145; ARROW_Y = 152; ARROW_R = 76;
  SV_X = 386; SV_Y = 104; SV_R = 30;
  PO_X = 342; PO_Y = 242; PO_R = 15;
  SV_BOX = (Rect){296, 44, 180, 152};
  NAV_VL = 292; NAV_VL_Y = 46; NAV_VL_H = 270;
  NAV_HL[0] = 196; NAV_HL[1] = 262; NAV_HL[2] = -1; NAV_HL_X = 296; NAV_HL_W = 184;
  NAV_LBL_X = 386; NAV_LBL_Y = 54; NAV_LBL_W = 180;
  NAV_DST_X = 386; NAV_DST_Y = 164; NAV_DST_W = 184;
  NAV_POI_LX = 386; NAV_POI_LY = 210; NAV_POI_DX = 414; NAV_POI_DY = 242; NAV_POI_DW = 100;
  NAV_REST_X = 144; NAV_REST_LY = 238; NAV_REST_VY = 268; NAV_REST_W = 270;
  NAV_LINE_X = 144; NAV_LINE_Y = 302; NAV_LINE_W = 280;
  NAV_SPD_X = 386; NAV_SPD_Y = 290; NAV_SPD_W = 170;
  NAV_MARK_X = 302; NAV_HINT = BAR_HINT;

  const Rect mb[4] = {{14,50,221,100},{245,50,221,100},{14,156,221,100},{245,156,221,100}};
  for (uint8_t i = 0; i < 4; i++) MENU_BTN[i] = mb[i];
  // "Luce 100% automatica" scritto in font 4 vuole quasi tutta la riga: al
  // tasto dello schermo basta la parola, lo stato si legge dentro la pagina.
  MENU_LUCE    = (Rect){14, 262, 330, 52};
  MENU_SCHERMO = (Rect){352, 262, 114, 52};
  LUCE_BTN[0] = (Rect){14, 56, 221, 86}; LUCE_BTN[1] = (Rect){245, 56, 221, 86};
  LUCE_PCT_Y  = 170;
  const Rect mo[3] = {{14,200,140,48},{170,200,140,48},{326,200,140,48}};
  for (uint8_t i = 0; i < 3; i++) MODO_BTN[i] = mo[i];
  SCHERMO_BTN[0] = (Rect){14, 80, 221, 110}; SCHERMO_BTN[1] = (Rect){245, 80, 221, 110};
  for (uint8_t i = 0; i < UI_NET_RIGHE; i++)
    NET_BTN[i] = (Rect){14, (int16_t)(52 + i * 64), 452, 56};
  const Rect pb[3] = {{14,258,140,48},{170,258,140,48},{326,258,140,48}};
  for (uint8_t i = 0; i < 3; i++) PAGE_BTN[i] = pb[i];
  ROUTE_RICALC = (Rect){14, 196, 452, 46};
  CONF_NO = (Rect){14, 258, 221, 48}; CONF_SI = (Rect){245, 258, 221, 48};
  CONF_Q_Y = 130; CONF_SUB_Y = 172;
  BARRA_MENU   = (Rect){0, 0, 480, 40};
  RIGA_LX = 20; RIGA_VX = 210;
  const int16_t ry[4] = {66, 102, 138, 174};
  for (uint8_t i = 0; i < 4; i++) RIGA_Y[i] = ry[i];

  LOGO_X = 240; LOGO_Y = 130; LOGO_R = 62; ORB_RX = 96; ORB_RY = 34;
}

static void layoutVerticale() {
  SW = 320; SH = 480;

  // in piedi lo spazio e' poco: scritta corta, e il tasto menu subito dopo
  BAR_SAT_W = 104; BAR_HINT = 146;
  WAIT_X = 160; WAIT_W = 300; WAIT_TY = 19;
  const int16_t wy[6] = {120, 180, 212, 262, 296, 340};
  for (uint8_t i = 0; i < 6; i++) WAIT_Y[i] = wy[i];

  // In piedi la velocita' sta in alto e grossa, sotto la direzione e la
  // distanza affiancate, in fondo le quattro statistiche a due a due.
  DRV_HL1 = 250; DRV_HL2 = 352; DRV_VL = 160; DRV_VL_Y = 258; DRV_VL_H = 86;
  DRV_SPD_X = 160; DRV_SPD_Y = 150; DRV_SPD_W = 300; DRV_KMH_X = 160; DRV_KMH_Y = 218;
  DRV_DIR_X = 80;  DRV_DIR_LY = 272; DRV_DIR_VY = 312; DRV_DIR_W = 140;
  DRV_BASE_X = 240; DRV_BASE_LY = 272; DRV_BASE_VY = 312;
  const int16_t sx[4] = {80, 240, 80, 240};
  const int16_t sl[4] = {374, 374, 428, 428};
  const int16_t sv[4] = {400, 400, 454, 454};
  for (uint8_t i = 0; i < 4; i++) { DRV_ST_X[i] = sx[i]; DRV_ST_LY[i] = sl[i]; DRV_ST_VY[i] = sv[i]; }
  DRV_ST_W = 140; DRV_HINT = BAR_HINT;

  ARROW_X = 160; ARROW_Y = 276; ARROW_R = 66;
  SV_X = 78;  SV_Y = 110; SV_R = 32;
  PO_X = 190; PO_Y = 392; PO_R = 15;
  SV_BOX = (Rect){12, 44, 296, 156};
  NAV_VL = -1; NAV_VL_Y = 0; NAV_VL_H = 0;
  NAV_HL[0] = 204; NAV_HL[1] = 348; NAV_HL[2] = 416; NAV_HL_X = 0; NAV_HL_W = 320;
  NAV_LBL_X = 160; NAV_LBL_Y = 56; NAV_LBL_W = 300;
  NAV_DST_X = 216; NAV_DST_Y = 110; NAV_DST_W = 180;
  NAV_POI_LX = 235; NAV_POI_LY = 362; NAV_POI_DX = 258; NAV_POI_DY = 392; NAV_POI_DW = 86;
  NAV_REST_X = 80; NAV_REST_LY = 362; NAV_REST_VY = 392; NAV_REST_W = 140;
  NAV_LINE_X = 160; NAV_LINE_Y = 172; NAV_LINE_W = 300;
  NAV_SPD_X = 160; NAV_SPD_Y = 448; NAV_SPD_W = 300;
  NAV_MARK_X = 20; NAV_HINT = BAR_HINT;

  // Riquadri piu' bassi e barre piu' alte: in piedi lo spazio in verticale
  // c'e', e quello che serve e' che fra una cosa e l'altra ci sia margine -
  // col guanto non si centra il pixel.
  const Rect mb[4] = {{14,52,146,92},{166,52,146,92},{14,152,146,92},{166,152,146,92}};
  for (uint8_t i = 0; i < 4; i++) MENU_BTN[i] = mb[i];
  MENU_LUCE    = (Rect){14, 264, 292, 68};
  MENU_SCHERMO = (Rect){14, 346, 292, 68};
  LUCE_BTN[0] = (Rect){14, 60, 146, 96}; LUCE_BTN[1] = (Rect){166, 60, 146, 96};
  LUCE_PCT_Y  = 192;
  for (uint8_t i = 0; i < 3; i++) MODO_BTN[i] = (Rect){14, (int16_t)(240 + i * 56), 292, 48};
  SCHERMO_BTN[0] = (Rect){14, 90, 292, 104}; SCHERMO_BTN[1] = (Rect){14, 210, 292, 104};
  for (uint8_t i = 0; i < UI_NET_RIGHE; i++)
    NET_BTN[i] = (Rect){14, (int16_t)(60 + i * 70), 292, 60};
  // In piedi i tre tasti si impilano: larghi tutta la pagina, Indietro sotto.
  PAGE_BTN[0] = (Rect){14, 416, 292, 50};
  PAGE_BTN[1] = (Rect){14, 360, 292, 50};
  PAGE_BTN[2] = (Rect){14, 304, 292, 50};
  ROUTE_RICALC = (Rect){14, 248, 292, 50};
  CONF_NO = (Rect){14, 400, 142, 58}; CONF_SI = (Rect){166, 400, 142, 58};
  CONF_Q_Y = 190; CONF_SUB_Y = 232;
  BARRA_MENU   = (Rect){0, 0, 320, 40};
  RIGA_LX = 16; RIGA_VX = 156;
  const int16_t ry[4] = {72, 116, 160, 204};
  for (uint8_t i = 0; i < 4; i++) RIGA_Y[i] = ry[i];

  LOGO_X = 160; LOGO_Y = 200; LOGO_R = 62; ORB_RX = 110; ORB_RY = 38;
}

// Rotazione 0 e 2 sono in piedi, 1 e 3 sdraiate: dipende da come e' fatto il
// pannello, non da noi.
static bool layoutVertPerRot(uint8_t rot) { return (rot & 1) == 0; }

static void layoutCompute(uint8_t rot) {
  uiRot  = (uint8_t)(rot & 3);
  uiVert = layoutVertPerRot(uiRot);
  if (uiVert) layoutVerticale();
  else        layoutOrizzontale();
}

// ---- memoria ---------------------------------------------------------------
static uint8_t layoutLoadRot() {
  Preferences p;
  if (!p.begin("display", true)) return DISPLAY_ROTATION;
  const uint8_t r = p.getUChar("rot", DISPLAY_ROTATION);
  p.end();
  return (uint8_t)(r & 3);
}

static void layoutSaveRot(uint8_t rot) {
  Preferences p;
  if (!p.begin("display", false)) return;
  p.putUChar("rot", (uint8_t)(rot & 3));
  p.end();
}
