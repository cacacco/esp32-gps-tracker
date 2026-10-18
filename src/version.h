#pragma once
// ===========================================================================
// Da dove viene questo firmware
//
// La data e l'ora di compilazione le mette il compilatore da solo. Il commit
// lo aggiunge tools/versione.py, che gira prima della compilazione e chiede a
// git dove sei: se git non c'e' (per esempio compilando con l'Arduino IDE) il
// firmware si costruisce lo stesso e al posto del commit si legge "senza git".
//
// Si legge in tre posti: nel log appena acceso, in fondo alla schermata
// iniziale del display, e in fondo alla dashboard.
// ===========================================================================
#ifndef FW_COMMIT
  #define FW_COMMIT "senza git"
#endif

#define FW_BUILD    __DATE__ " " __TIME__
#define FW_VERSIONE FW_BUILD "  -  " FW_COMMIT
