// Configurazione TFT_eSPI per il display 4" ST7796 480x320 SPI su ESP32 DevKit v1.
//
// Serve SOLO per l'Arduino IDE: copia questo file dentro la cartella della
// libreria (Documenti/Arduino/libraries/TFT_eSPI/) sovrascrivendo User_Setup.h.
// Con PlatformIO non serve: le stesse impostazioni stanno in platformio.ini.
//
// Dopo un aggiornamento della libreria va ricopiato.

#define USER_SETUP_LOADED 1

#define ST7796_DRIVER
#define TFT_WIDTH  320
#define TFT_HEIGHT 480

// Collegamenti (vedi config.h e il README)
#define TFT_MISO 19   // SDO
#define TFT_MOSI 23   // SDI
#define TFT_SCLK 18   // SCK
#define TFT_CS   15   // CS
#define TFT_DC    2   // DC/RS
#define TFT_RST   4   // RESET
#define TOUCH_CS 33   // T_CS, solo se colleghi il touch

// Font caricati: il 7 e' quello a sette segmenti usato per la velocita'
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
