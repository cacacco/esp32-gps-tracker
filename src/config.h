/*
 * config.h - tutte le impostazioni del GPS Tracker in un unico punto.
 *
 * Hardware:
 *   - ESP32 DevKit v1 (ESP32-WROOM-32, 30 pin)
 *   - Modulo GPS u-blox NEO-7M (GY-NEO7M) con antenna esterna
 *
 * Modifica i valori qui sotto, poi compila e carica. Nessun altro file
 * va toccato per l'uso normale.
 */
#pragma once

// ===========================================================================
// 1) WiFi
// ===========================================================================
#define WIFI_SSID       "LA-TUA-RETE-WIFI"
#define WIFI_PASS       "LA-TUA-PASSWORD"
#define HOSTNAME        "gps-tracker"     // dashboard su http://gps-tracker.local

// ===========================================================================
// 2) SinricPro (opzionale)
//    Metti ENABLE_SINRICPRO a 1 e compila SOLO se hai un account su
//    https://portal.sinric.pro con un device di tipo "Contact Sensor".
//    Il tracker usa il contact sensor come allarme geofence:
//       aperto  = il tracker e' USCITO dall'area
//       chiuso  = il tracker e' DENTRO l'area
// ===========================================================================
#define ENABLE_SINRICPRO  0
#define APP_KEY           "YOUR-APP-KEY"     // "de0bxxxx-1x3x-4x3x-ax2x-5dabxxxxxxxx"
#define APP_SECRET        "YOUR-APP-SECRET"  // "5f36xxxx-x3x7-...-333d65xxxxxx"
#define CONTACT_ID        "YOUR-DEVICE-ID"   // "5dc1564130xxxxxxxxxxxxxx"

// ===========================================================================
// 3) Collegamenti (vedi docs/wiring.svg e il README)
//
//    GPS VCC -> VIN (5V)       GPS GND -> GND
//    GPS TX  -> GPIO16 (RX2)   GPS RX  -> GPIO17 (TX2)
//    GPS PPS -> GPIO4 (facoltativo)
// ===========================================================================
#define PIN_GPS_RX       16   // ESP32 riceve (collegato al TX del GPS)
#define PIN_GPS_TX       17   // ESP32 trasmette (collegato al RX del GPS)
#define PIN_GPS_PPS       4   // -1 per disabilitare
#define PIN_LED           2   // LED blu integrato sulla DevKit v1
#define GPS_BAUD       9600   // baud di fabbrica del NEO-7M

// ===========================================================================
// 4) Qualita' minima del fix: sotto queste soglie la posizione viene
//    ignorata (niente traccia, niente allarmi su rimbalzi del segnale).
// ===========================================================================
#define MIN_SATELLITES    4
#define MAX_HDOP        5.0   // 1 = ottimo, 2-5 = buono, >5 = scarso
#define FIX_TIMEOUT_MS 10000  // oltre questo tempo senza posizione valida -> "no fix"

// ===========================================================================
// 5) Geofence (area di casa / base).
//    Il centro si puo' cambiare al volo dalla dashboard ("Centra qui") e
//    viene salvato in memoria non volatile: questi valori sono solo il
//    default al primo avvio.
// ===========================================================================
#define GEOFENCE_LAT             45.464200   // esempio: Duomo di Milano
#define GEOFENCE_LON              9.190000
#define GEOFENCE_RADIUS_M            150.0   // raggio in metri
#define GEOFENCE_HYSTERESIS_M         25.0   // isteresi: evita allarmi sul bordo
#define GEOFENCE_CONFIRM_SAMPLES         4   // letture consecutive per confermare

// ===========================================================================
// 6) Registrazione della traccia (in RAM, si azzera al riavvio)
// ===========================================================================
#define TRACK_MAX_POINTS           400   // ~400 punti = circa 6 KB di RAM
#define TRACK_MIN_DISTANCE_M      10.0   // nuovo punto dopo almeno 10 m
#define TRACK_MIN_INTERVAL_MS     5000   // ...e non piu' di uno ogni 5 s

// ===========================================================================
// 7) Varie
// ===========================================================================
#define SERIAL_BAUD           115200
#define STATUS_PRINT_MS         5000   // riepilogo su monitor seriale

// Modalita' ponte seriale: USB <-> GPS, per usare u-center sul PC.
// Con 1 il tracker non fa altro: serve solo a configurare il modulo.
#define GPS_PASSTHROUGH            0

// Invia al NEO-7M la configurazione UBX all'avvio (rate di aggiornamento).
// Lasciare 0 se il modulo e' gia' a posto: il default di fabbrica e' 1 Hz.
#define GPS_CONFIGURE_UBX          0
#define GPS_UPDATE_RATE_HZ         1   // 1, 2, 5 o 10 Hz
