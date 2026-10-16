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
// 6) Registrazione della traccia (su memoria interna: sopravvive allo
//    spegnimento, quindi in auto non perdi il viaggio quando togli il contatto)
// ===========================================================================
#define TRACK_MIN_DISTANCE_M      10.0   // nuovo punto dopo almeno 10 m
#define TRACK_MIN_INTERVAL_MS     5000   // ...e non piu' di uno ogni 5 s
#define TRACK_FILE_MAX_POINTS    12000   // ~190 KB: oltre, il file ruota e il
                                         // precedente resta come /track.old.bin
#define TRACK_VIEW_POINTS          600   // quanti punti disegnare sulla mappa

// Statistiche del viaggio
#define STATS_MOVING_MIN_KMH       3.0   // sotto questa velocita' sei fermo
#define STATS_SAVE_MS           120000   // ogni quanto salvare l'odometro totale

// ===========================================================================
// 6b) Portale WiFi
//     Se la rete salvata non risponde, il tracker apre un suo access point:
//     ti colleghi col telefono e scegli la rete da una pagina web. Le
//     credenziali qui sopra servono solo come primo tentativo.
// ===========================================================================
#define AP_SSID_PREFIX     "GPS-Tracker"  // diventa "GPS-Tracker-a3f1"
#define AP_PASSWORD        "gpstracker"   // min 8 caratteri, "" per rete aperta
#define WIFI_CONNECT_MS         20000     // quanto aspettare la rete all'avvio
#define WIFI_RETRY_MS          120000     // ogni quanto riprovarla dal portale

// ===========================================================================
// 6bis) Navigazione a freccia
//     Il percorso si prepara sul PC (Google Maps, BRouter, Komoot...), si
//     esporta in GPX e si carica dalla dashboard. Il tracker mostra la
//     direzione del prossimo punto e quanto manca: niente calcolo del
//     percorso a bordo, niente ricalcolo se sbagli strada.
// ===========================================================================
#define ROUTE_MAX_POINTS        1200   // ~14 KB di RAM
#define ROUTE_MIN_SPACING_M       15   // distanza minima tra due punti del percorso
#define ROUTE_ARRIVAL_M           25   // entro questo raggio il punto e' raggiunto
#define ROUTE_OFF_ROUTE_M         60   // oltre questa distanza sei fuori percorso
#define ROUTE_FINISH_M            30   // arrivo sull'ultimo punto
#define ROUTE_LOOKAHEAD           40   // quanti punti guardare avanti per ritrovarsi

// Tappe con nome: lette dai <wpt> del GPX, oppure aggiunte dalla dashboard.
#define STOPS_MAX                 40
#define STOP_NAME_LEN             24   // caratteri del nome, terminatore incluso
#define STOP_ARRIVAL_M           100   // entro questo raggio la tappa e' raggiunta

// ===========================================================================
// 6c) Display TFT 4" ST7796 (480x320, SPI) - facoltativo
//
//     Collegamenti: VCC->3V3, GND->GND, CS->D15, RESET->D4, DC/RS->D2,
//     SDI(MOSI)->D23, SCK->D18, LED->3V3, SDO(MISO)->D19.
//     Il touch non serve: i pin T_* si possono lasciare scollegati.
//
//     ATTENZIONE: con il display, GPIO2 e GPIO4 sono del pannello. Metti
//     PIN_LED a 32 e PIN_GPS_PPS a -1, altrimenti la compilazione si ferma.
// ===========================================================================
#define ENABLE_DISPLAY             0
#define DISPLAY_ROTATION           1   // 0-3: gira l'immagine di 90 gradi
#define DISPLAY_REFRESH_MS       250   // ogni quanto aggiornare i numeri
#define TIME_UTC_OFFSET_MIN      120   // orologio: 60 in inverno, 120 in estate

// ===========================================================================
// 7) Varie
// ===========================================================================
#define SERIAL_BAUD           115200
#define STATUS_PRINT_MS         5000   // riepilogo su monitor seriale

// Eco sul monitor dei byte grezzi che arrivano dal GPS: si vede subito se il
// modulo parla (testo NMEA leggibile) o se il baud rate e' sbagliato
// (caratteri a caso). Da tenere a 0 nell'uso normale.
#define GPS_RAW_ECHO               0

// Se al baud configurato non arriva nessuna frase valida, all'avvio prova gli
// altri baud comuni: parecchi moduli clone escono di fabbrica diversi da 9600.
#define GPS_AUTO_BAUD              1

// Modalita' ponte seriale: USB <-> GPS, per usare u-center sul PC.
// Con 1 il tracker non fa altro: serve solo a configurare il modulo.
#define GPS_PASSTHROUGH            0

// Invia al NEO-7M la configurazione UBX all'avvio (rate di aggiornamento).
// Lasciare 0 se il modulo e' gia' a posto: il default di fabbrica e' 1 Hz.
#define GPS_CONFIGURE_UBX          0
#define GPS_UPDATE_RATE_HZ         1   // 1, 2, 5 o 10 Hz
