#!/usr/bin/env python3
"""Converte un carattere TTF/OTF nel formato VLW che TFT_eSPI sa disegnare,
e lo scrive come tabella C dentro src/font/.

Perche' serve: i font numerati di TFT_eSPI (2, 4, 8...) sono a matrice di
punti, senza sfumatura sui bordi, e quello grande e' a sette segmenti. Con
SMOOTH_FONT acceso la libreria sa invece disegnare caratteri veri, con i
bordi sfumati, ma vuole il formato VLW, che nasce da uno sketch Processing.
Questo script fa la stessa cosa in Python, cosi' il carattere si rigenera
senza installare altro.

Il formato, letto dal sorgente di TFT_eSPI (Extensions/Smooth_font.cpp):

  intestazione, 24 byte: sei interi big-endian a 32 bit
      numero di glifi, versione (11), dimensione, scartato, ascent, descent

  un record da 28 byte per glifo: sette interi big-endian
      unicode, altezza, larghezza, avanzamento, dY, dX, ignorato
      dY = quanto il glifo sale sopra la linea di base
      dX = scostamento orizzontale dal cursore

  poi tutte le bitmap di seguito, nello stesso ordine dei record:
      un byte di trasparenza per pixel, larghezza x altezza byte ciascuna

Le misure non si scalano: setTextSize() non ha effetto sui caratteri lisci,
quindi ogni grandezza che serve a schermo e' un file a se'. Per questo i
grandi hanno pochi glifi: a 72 pixel l'alfabeto intero sarebbe 216 KB.

    python3 tools/crea_font.py            # genera tutto in src/font/
    python3 tools/crea_font.py --prova    # solo la verifica, non scrive
"""
import os
import struct
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("serve Pillow:  pip install pillow")

QUI = os.path.dirname(os.path.abspath(__file__))
DEST = os.path.join(QUI, '..', 'src', 'font')

ASCII = ''.join(chr(c) for c in range(32, 127))
MAIUSC = ' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.,:%-/'
CIFRE = ' 0123456789.,:-'

# nome in C, file del carattere, pixel, caratteri, a cosa serve
PIANO = [
    ('fontPiccolo', 'Inter-Medium.otf',   16, ASCII,  'etichette e sottotitoli'),
    ('fontMedio',   'Inter-Medium.otf',   26, ASCII,  'valori e titoli dei tasti'),
    ('fontGrande',  'Inter-SemiBold.otf', 48, MAIUSC, 'direzione, percentuali'),
    ('fontEnorme',  'Inter-SemiBold.otf', 72, CIFRE,  'la velocita\''),
]

CERCA_IN = [
    '/usr/share/fonts/opentype/inter',
    '/usr/share/fonts/truetype/inter',
    os.path.join(QUI, 'font'),
]

I32 = lambda v: struct.pack('>i', int(v))


def trova(nome):
    for d in CERCA_IN:
        p = os.path.join(d, nome)
        if os.path.exists(p):
            return p
    sys.exit('non trovo %s: cercato in %s' % (nome, ', '.join(CERCA_IN)))


def costruisci(percorso, px, caratteri):
    f = ImageFont.truetype(percorso, px)
    ascent, descent = f.getmetrics()
    glifi = []
    for ch in caratteri:
        mask, (ox, oy) = f.getmask2(ch, mode='L')
        w, h = mask.size
        dati = bytes(mask) if w and h else b''
        if len(dati) != w * h:
            sys.exit('bitmap incoerente per %r' % ch)
        if w > 255 or h > 255:
            sys.exit('%r a %dpx e\' %dx%d: il formato regge fino a 255' % (ch, px, w, h))
        glifi.append({'cp': ord(ch), 'w': w, 'h': h, 'adv': round(f.getlength(ch)),
                      'dY': ascent - oy, 'dX': ox, 'bmp': dati})

    out = bytearray()
    out += I32(len(glifi)) + I32(11) + I32(px) + I32(0) + I32(ascent) + I32(descent)
    for g in glifi:
        out += (I32(g['cp']) + I32(g['h']) + I32(g['w']) + I32(g['adv'])
                + I32(g['dY']) + I32(g['dX']) + I32(0))
    for g in glifi:
        out += g['bmp']
    return bytes(out), glifi


# --------------------------------------------------------------------------
# Rilettura: la stessa aritmetica di drawGlyph, per verificare il file senza
# avere una scheda in mano.
def leggi(dati):
    n, _v, dim, _x, asc, desc = struct.unpack('>6i', dati[:24])
    glifi, p, bmp = {}, 24, 24 + n * 28
    for _ in range(n):
        cp, h, w, adv, dY, dX, _i = struct.unpack('>7i', dati[p:p + 28])
        p += 28
        glifi[cp] = {'w': w, 'h': h, 'adv': adv, 'dY': dY, 'dX': dX, 'off': bmp}
        bmp += w * h
    if bmp != len(dati):
        sys.exit('le bitmap finiscono a %d ma il file e\' lungo %d' % (bmp, len(dati)))
    return {'glifi': glifi, 'dati': dati, 'dim': dim,
            'maxAscent': max(g['dY'] for g in glifi.values())}


def disegna(font, testo, w, h):
    im = Image.new('L', (w, h), 0)
    px = im.load()
    cx = 0
    for ch in testo:
        g = font['glifi'].get(ord(ch))
        if g is None:
            continue
        cy = font['maxAscent'] - g['dY']
        for y in range(g['h']):
            for x in range(g['w']):
                a = font['dati'][g['off'] + x + g['w'] * y]
                if a and 0 <= cx + g['dX'] + x < w and 0 <= cy + y < h:
                    px[cx + g['dX'] + x, cy + y] = a
        cx += g['adv']
    return im


def verifica(percorso, px, caratteri):
    """Confronta il disegno fatto rileggendo il VLW con il carattere disegnato
    direttamente: se un byte e' fuori posto, qui si vede."""
    dati, _ = costruisci(percorso, px, caratteri)
    f = leggi(dati)
    testo = ''.join(c for c in '0123456789 ,.:-' if ord(c) in f['glifi'])
    W, H = px * len(testo), px * 2
    mio = disegna(f, testo, W, H)

    rif = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(rif)
    tt = ImageFont.truetype(percorso, px)
    cx = 0
    for ch in testo:
        d.text((cx, f['maxAscent']), ch, font=tt, fill=255, anchor='ls')
        cx += round(tt.getlength(ch))

    scarto = max(abs(a - b) for a, b in zip(mio.getdata(), rif.getdata()))
    return len(dati), scarto


def tabella(nome, dati, commento):
    righe = ['#pragma once',
             '// Generato da tools/crea_font.py: non modificare a mano.',
             '// %s' % commento,
             '// Carattere Inter, licenza SIL OFL 1.1 - vedi LICENZA-Inter.txt',
             '#include <Arduino.h>',
             '',
             'const uint8_t %s[] PROGMEM = {' % nome]
    for i in range(0, len(dati), 16):
        righe.append('  ' + ','.join('0x%02X' % b for b in dati[i:i + 16]) + ',')
    righe.append('};')
    righe.append('')
    return '\n'.join(righe)


def main():
    solo_prova = '--prova' in sys.argv
    if not solo_prova:
        os.makedirs(DEST, exist_ok=True)

    totale = 0
    guasti = 0
    for nome, file_font, px, car, uso in PIANO:
        p = trova(file_font)
        n_byte, scarto = verifica(p, px, car)
        esito = 'esatto' if scarto == 0 else 'SCARTO %d' % scarto
        if scarto:
            guasti += 1
        totale += n_byte
        print('%-12s %3dpx  %3d glifi  %7d byte  verifica: %-8s  %s'
              % (nome, px, len(car), n_byte, esito, uso))
        if not solo_prova:
            dati, _ = costruisci(p, px, car)
            with open(os.path.join(DEST, nome.lower() + '.h'), 'w') as fh:
                fh.write(tabella(nome, dati, '%s - %s a %d pixel' % (uso, file_font, px)))

    print('-' * 78)
    print('%-12s %34d byte  (%.1f KB)' % ('totale', totale, totale / 1024))
    if guasti:
        sys.exit('VERIFICA FALLITA su %d caratteri: non scrivo niente di buono' % guasti)
    if not solo_prova:
        print('scritti in src/font/')


if __name__ == '__main__':
    main()
