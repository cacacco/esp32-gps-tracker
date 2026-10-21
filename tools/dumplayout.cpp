// Stampa le misure di layout.h cosi' come sono, in JSON: l'anteprima le
// legge da qui invece di ricopiarle, cosi' non possono divergere.
#include <cstdio>
#include "layout.h"

static void r(const char *n, const Rect &v, bool virgola = true) {
  printf("\"%s\":[%d,%d,%d,%d]%s", n, v.x, v.y, v.w, v.h, virgola ? "," : "");
}
static void i(const char *n, int v) { printf("\"%s\":%d,", n, v); }

int main(int argc, char **argv) {
  layoutCompute(argc > 1 ? (uint8_t)atoi(argv[1]) : 1);
  printf("{");
  i("SW", SW); i("SH", SH); i("vert", uiVert ? 1 : 0);
  i("WAIT_X", WAIT_X); i("WAIT_W", WAIT_W);
  printf("\"WAIT_Y\":[%d,%d,%d,%d,%d,%d],",
         WAIT_Y[0], WAIT_Y[1], WAIT_Y[2], WAIT_Y[3], WAIT_Y[4], WAIT_Y[5]);
  i("DRV_HL1", DRV_HL1); i("DRV_HL2", DRV_HL2);
  i("DRV_VL", DRV_VL); i("DRV_VL_Y", DRV_VL_Y); i("DRV_VL_H", DRV_VL_H);
  i("DRV_SPD_X", DRV_SPD_X); i("DRV_SPD_Y", DRV_SPD_Y); i("DRV_SPD_W", DRV_SPD_W);
  i("DRV_KMH_X", DRV_KMH_X); i("DRV_KMH_Y", DRV_KMH_Y);
  i("DRV_DIR_X", DRV_DIR_X); i("DRV_DIR_LY", DRV_DIR_LY); i("DRV_DIR_VY", DRV_DIR_VY);
  i("DRV_DIR_W", DRV_DIR_W);
  i("DRV_BASE_X", DRV_BASE_X); i("DRV_BASE_LY", DRV_BASE_LY); i("DRV_BASE_VY", DRV_BASE_VY);
  printf("\"DRV_ST_X\":[%d,%d,%d,%d],", DRV_ST_X[0], DRV_ST_X[1], DRV_ST_X[2], DRV_ST_X[3]);
  printf("\"DRV_ST_LY\":[%d,%d,%d,%d],", DRV_ST_LY[0], DRV_ST_LY[1], DRV_ST_LY[2], DRV_ST_LY[3]);
  printf("\"DRV_ST_VY\":[%d,%d,%d,%d],", DRV_ST_VY[0], DRV_ST_VY[1], DRV_ST_VY[2], DRV_ST_VY[3]);
  i("DRV_ST_W", DRV_ST_W); i("DRV_HINT", DRV_HINT);
  i("ARROW_X", ARROW_X); i("ARROW_Y", ARROW_Y); i("ARROW_R", ARROW_R);
  i("SV_X", SV_X); i("SV_Y", SV_Y); i("SV_R", SV_R);
  i("PO_X", PO_X); i("PO_Y", PO_Y); i("PO_R", PO_R);
  r("SV_BOX", SV_BOX);
  i("NAV_VL", NAV_VL); i("NAV_VL_Y", NAV_VL_Y); i("NAV_VL_H", NAV_VL_H);
  printf("\"NAV_HL\":[%d,%d,%d],", NAV_HL[0], NAV_HL[1], NAV_HL[2]);
  i("NAV_HL_X", NAV_HL_X); i("NAV_HL_W", NAV_HL_W);
  i("NAV_LBL_X", NAV_LBL_X); i("NAV_LBL_Y", NAV_LBL_Y); i("NAV_LBL_W", NAV_LBL_W);
  i("NAV_DST_X", NAV_DST_X); i("NAV_DST_Y", NAV_DST_Y); i("NAV_DST_W", NAV_DST_W);
  i("NAV_POI_LX", NAV_POI_LX); i("NAV_POI_LY", NAV_POI_LY);
  i("NAV_POI_DX", NAV_POI_DX); i("NAV_POI_DY", NAV_POI_DY); i("NAV_POI_DW", NAV_POI_DW);
  i("NAV_REST_X", NAV_REST_X); i("NAV_REST_LY", NAV_REST_LY);
  i("NAV_REST_VY", NAV_REST_VY); i("NAV_REST_W", NAV_REST_W);
  i("NAV_LINE_X", NAV_LINE_X); i("NAV_LINE_Y", NAV_LINE_Y); i("NAV_LINE_W", NAV_LINE_W);
  i("NAV_SPD_X", NAV_SPD_X); i("NAV_SPD_Y", NAV_SPD_Y); i("NAV_SPD_W", NAV_SPD_W);
  i("NAV_MARK_X", NAV_MARK_X); i("NAV_HINT", NAV_HINT);
  for (int k = 0; k < 4; k++) { char n[12]; snprintf(n, sizeof(n), "MENU_BTN%d", k); r(n, MENU_BTN[k]); }
  r("MENU_LUCE", MENU_LUCE); r("MENU_SCHERMO", MENU_SCHERMO);
  r("LUCE_BTN0", LUCE_BTN[0]); r("LUCE_BTN1", LUCE_BTN[1]);
  for (int k = 0; k < 3; k++) { char n[12]; snprintf(n, sizeof(n), "MODO_BTN%d", k); r(n, MODO_BTN[k]); }
  r("SCHERMO_BTN0", SCHERMO_BTN[0]); r("SCHERMO_BTN1", SCHERMO_BTN[1]);
  for (int k = 0; k < 3; k++) { char n[12]; snprintf(n, sizeof(n), "NET_BTN%d", k); r(n, NET_BTN[k]); }
  for (int k = 0; k < 3; k++) { char n[12]; snprintf(n, sizeof(n), "PAGE_BTN%d", k); r(n, PAGE_BTN[k]); }
  r("ROUTE_RICALC", ROUTE_RICALC);
  i("RIGA_LX", RIGA_LX); i("RIGA_VX", RIGA_VX);
  printf("\"RIGA_Y\":[%d,%d,%d,%d],", RIGA_Y[0], RIGA_Y[1], RIGA_Y[2], RIGA_Y[3]);
  i("LUCE_PCT_Y", LUCE_PCT_Y);
  i("LOGO_X", LOGO_X); i("LOGO_Y", LOGO_Y); i("LOGO_R", LOGO_R);
  i("ORB_RX", ORB_RX); printf("\"ORB_RY\":%d}", ORB_RY);
  return 0;
}
