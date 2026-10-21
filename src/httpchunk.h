#pragma once
// ---------------------------------------------------------------------------
// Dalla risposta HTTP al solo corpo.
//
// Due noie da togliere di mezzo prima di dare il testo al lettore del
// percorso: le righe di intestazione, e il fatto che un server puo' mandare
// il corpo spezzato in pezzi numerati (ogni pezzo preceduto dalla sua
// lunghezza in esadecimale). Chiediamo HTTP/1.0 proprio per evitarlo, ma se
// il server lo manda lo stesso - capita quando in mezzo c'e' un altro
// apparato - i numeri finirebbero dentro al percorso e lo rovinerebbero.
//
// Lavora mentre i byte arrivano: non tiene in memoria niente oltre alla riga
// che sta leggendo.
// ---------------------------------------------------------------------------
#include <Arduino.h>

typedef void (*HttpBodyFn)(const uint8_t *data, size_t len);

#define HTTP_LINE_MAX 96

static HttpBodyFn httpBodyFn   = nullptr;
static uint16_t   httpStatus   = 0;
static bool       httpInBody   = false;
static bool       httpChunked  = false;
static bool       httpFinished = false;
static char       httpLine[HTTP_LINE_MAX];
static uint8_t    httpLineLen  = 0;
static uint32_t   httpChunkLeft = 0;
static bool       httpChunkHdr = false;   // sto leggendo la riga con la lunghezza
static uint32_t   httpBodyBytes = 0;

static void httpBegin(HttpBodyFn fn) {
  httpBodyFn    = fn;
  httpStatus    = 0;
  httpInBody    = false;
  httpChunked   = false;
  httpFinished  = false;
  httpLineLen   = 0;
  httpLine[0]   = '\0';
  httpChunkLeft = 0;
  httpChunkHdr  = false;
  httpBodyBytes = 0;
}

static bool httpLineHas(const char *ago) {
  for (char *p = httpLine; *p; p++) {
    const char *a = ago;
    char *q = p;
    while (*a && *q && tolower((unsigned char)*q) == tolower((unsigned char)*a)) { a++; q++; }
    if (!*a) return true;
  }
  return false;
}

static void httpHeaderLine() {
  if (!httpStatus) {                       // "HTTP/1.1 200 OK"
    const char *sp = strchr(httpLine, ' ');
    if (sp) httpStatus = (uint16_t)strtol(sp + 1, nullptr, 10);
    return;
  }
  if (httpLineHas("transfer-encoding:") && httpLineHas("chunked")) httpChunked = true;
}

static void httpFeed(const uint8_t *data, size_t len) {
  size_t i = 0;
  // ---- intestazioni, fino alla riga vuota ---------------------------------
  while (!httpInBody && i < len) {
    const char c = (char)data[i++];
    if (c == '\r') continue;
    if (c != '\n') {
      if (httpLineLen + 1 < HTTP_LINE_MAX) { httpLine[httpLineLen++] = c; httpLine[httpLineLen] = '\0'; }
      continue;
    }
    if (!httpLineLen) { httpInBody = true; httpChunkHdr = httpChunked; }
    else              { httpHeaderLine(); }
    httpLineLen = 0; httpLine[0] = '\0';
  }
  if (!httpInBody || httpFinished) return;

  // ---- corpo ---------------------------------------------------------------
  if (!httpChunked) {
    if (i < len && httpBodyFn) { httpBodyFn(data + i, len - i); }
    httpBodyBytes += len - i;
    return;
  }

  while (i < len && !httpFinished) {
    if (httpChunkHdr) {                    // riga con la lunghezza del pezzo
      const char c = (char)data[i++];
      if (c == '\r') continue;
      if (c != '\n') {
        if (httpLineLen + 1 < HTTP_LINE_MAX) { httpLine[httpLineLen++] = c; httpLine[httpLineLen] = '\0'; }
        continue;
      }
      if (!httpLineLen) continue;          // il ritorno a capo dopo un pezzo
      httpChunkLeft = (uint32_t)strtoul(httpLine, nullptr, 16);
      httpLineLen = 0; httpLine[0] = '\0';
      httpChunkHdr = false;
      if (!httpChunkLeft) httpFinished = true;   // pezzo di lunghezza zero: finito
      continue;
    }
    size_t quanti = len - i;
    if (quanti > httpChunkLeft) quanti = httpChunkLeft;
    if (quanti && httpBodyFn) httpBodyFn(data + i, quanti);
    httpBodyBytes += quanti;
    i += quanti;
    httpChunkLeft -= quanti;
    if (!httpChunkLeft) httpChunkHdr = true;
  }
}
