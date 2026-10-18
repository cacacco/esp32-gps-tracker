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
#define WIFI_RETRY_MS          120000     // ogni quanto riprovarle dal portale
#define WIFI_MAX_RETI               3     // quante reti ricorda: casa, hotspot, ...

// ===========================================================================
// 6bis) Navigazione a freccia
//     Il percorso si prepara sul PC (Google Maps, BRouter, Komoot...), si
//     esporta in GPX e si carica dalla dashboard. Il tracker mostra la
//     direzione del prossimo punto e quanto manca: niente calcolo del
//     percorso a bordo, niente ricalcolo se sbagli strada.
// ===========================================================================
#define ROUTE_MAX_POINTS        2000   // ~24 KB di RAM
#define ROUTE_MIN_SPACING_M       15   // distanza minima tra due punti del percorso
#define ROUTE_DRAW_SPACING_M      50   // passo con cui si infittisce un percorso
                                       // disegnato a mano: 2000 punti = 100 km

// ---- svolte --------------------------------------------------------------
// Le manovre non le calcola la ESP32: arrivano gia' pronte dal telefono.
#define TURNS_MAX                200   // svolte memorizzate, 32 byte l'una
#define TURN_NAME_LEN             20   // nome della strada, caratteri
#define TURN_WARN_M              400   // da quanto prima la freccia annuncia la svolta
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
//     Collegamenti (cerca la sigla stampata sulla scheda, non la posizione):
//
//       display        ESP32           display        ESP32
//       -----------------------        -----------------------
//       VCC         -> 3V3             SCK         -> D18
//       GND         -> GND             LED         -> 3V3
//       CS          -> D5              SDO (MISO)  -> D19
//       RESET       -> D22             T_* (touch) -> scollegati
//       DC o RS     -> D21             SD_* (microSD) -> scollegati
//       SDI (MOSI)  -> D23
//
//     Niente da cambiare altrove: il LED di bordo resta su GPIO2 e il PPS
//     del GPS su GPIO4. I pin del pannello si cambiano in platformio.ini.
// ===========================================================================
#define ENABLE_DISPLAY             1
#define DISPLAY_ROTATION           1   // 0-3: gira l'immagine di 90 gradi
#define DISPLAY_REFRESH_MS       250   // ogni quanto aggiornare i numeri
#define SPLASH_MS               1300   // durata del logo all'accensione, 0 per saltarlo
#define TIME_UTC_OFFSET_MIN      120   // orologio: 60 in inverno, 120 in estate

//     Retroilluminazione regolabile. Di serie il filo LED va alla pista 3,3 V
//     e la luce resta al massimo: di notte, sul manubrio, acceca. Mettendola su
//     un pin con PWM il firmware l'abbassa da solo quando fa buio, con l'ora
//     del GPS, e dal telefono la puoi forzare.
//
//       NON spostare quel filo su un pin senza aver prima MISURATO quanta
//       corrente assorbe: la retroilluminazione di un 4" ne vuole 100-150 mA e
//       un pin dell'ESP32 ne regge 20. Alcuni pannelli hanno un driver a bordo
//       e il pin LED e' solo un comando da pochi mA: quelli si pilotano diretti.
//       Gli altri vogliono un transistor. In dubbio lascia -1: funziona come
//       prima, luce sempre accesa. La procedura e' nel README, sezione 5c.
#define PIN_TFT_BL                33   // pin della retroilluminazione, -1 = fissa
#define BL_GIORNO                100   // luminosita' di giorno, in percento
#define BL_NOTTE                  25   // e di notte
#define BL_NOTTE_DA               20   // ora locale in cui si passa alla notte
#define BL_NOTTE_A                 7   // e quella in cui torna giorno

// ---- tocco e menu --------------------------------------------------------
//     Il pannello ha gia' il touch a bordo: servono quattro fili in piu'.
//
//       display        ESP32          (i primi tre sono gli stessi del pannello)
//       -----------------------
//       T_CLK       -> D18
//       T_DIN       -> D23
//       T_DO        -> D19
//       T_CS        -> D27
//       T_IRQ       -> scollegato, non serve
//
//     Senza questi fili metti ENABLE_TOUCH a 0: il menu compare lo stesso
//     all'avvio e dopo MENU_TIMEOUT_MS entra da solo in navigazione.
#define ENABLE_TOUCH               1
#define MENU_TIMEOUT_MS        15000   // quanto resta il menu se non tocchi niente

// Il menu si gira anche con un tasto, senza toccare lo schermo: di serie usa
// il tasto BOOT che la DevKit ha gia' a bordo. Pressione breve = passa al
// pulsante dopo, pressione lunga = conferma. Serve quando il touch non c'e'
// o non funziona. Metti -1 per non usarlo, oppure il pin di un tuo pulsante
// verso massa.
//
//     Con un pulsante esterno sul pannello usa GPIO32 o GPIO33, NON lo 0:
//     il GPIO0 decide come parte l'ESP32, e se il tasto resta premuto o il
//     filo va in corto all'accensione la scheda entra in modalita' caricamento
//     invece di partire. Il pulsante vuole un contatto MOMENTANEO (NA), non
//     uno di quelli che restano dentro.
#define PIN_MENU_BUTTON            0   // 0 = tasto BOOT della DevKit, 32 = pulsante sul pannello
#define BUTTON_LONG_MS           600   // oltre questo tempo la pressione e' lunga
#define BUTTON_CALIB_MS         3000   // tenendolo premuto cosi' a lungo, ritara il tocco
#define TOUCH_MIN_GAP_MS         250   // due tocchi piu' ravvicinati contano per uno
#define TOUCH_DEBUG                0   // a 1 stampa nel log le letture grezze del tocco,
                                       // per capire un tocco che non risponde

// ===========================================================================
// 6d) Aggiornamento via WiFi (OTA)
//     Permette di caricare il firmware senza cavo, con la scheda accesa sulla
//     rete. Il primo caricamento va comunque fatto via USB: e' quello che
//     mette a bordo la capacita' di aggiornarsi.
// ===========================================================================
#define ENABLE_OTA                 1
#define OTA_PASSWORD      "gpstracker"   // "" per nessuna password

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

// ===========================================================================
// 9) Le tue impostazioni, in un file solo tuo
//
//     Questo file e' del progetto e cambia a ogni aggiornamento: se ci metti
//     dentro la password del WiFi, prima o poi "aggiorna.bat" finisce in
//     conflitto fra le tue righe e le mie.
//
//     La soluzione: crea "src/config_local.h" (git lo ignora, non te lo
//     tocca nessuno) e mettici solo quello che vuoi cambiare. Trovi un
//     esempio pronto in "src/config_local.h.esempio": copialo, togli
//     ".esempio" dal nome e compila le due righe del WiFi.
//
//     Quello che scrivi li' vince su tutto quello che c'e' qui sopra.
// ===========================================================================
#if defined(__has_include)
#  if __has_include("config_local.h")
#    include "config_local.h"
#  endif
#endif
