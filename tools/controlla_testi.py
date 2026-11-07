#!/usr/bin/env python3
"""Controlla che le scritte stiano nello spazio che hanno sul display.

Serve perche' i caratteri lisci hanno larghezze diverse da quelli a matrice
di punti che c'erano prima, e una scritta troppo larga non da' nessun
errore: semplicemente finisce sopra la vicina. E' successo con "IN
MOVIMENTO", che era larga 121 pixel in una colonna da 110 e copriva QUOTA.
Un errore cosi' si vede solo guardando lo schermo nel momento giusto, ed e'
esattamente il tipo di cosa che una macchina deve controllare al posto nostro.

Le misure dei caratteri le prende da tools/crea_font.py, quelle
dell'impaginazione da tools/dumplayout.cpp: nessuno dei due numeri e'
ricopiato qui.

    python3 tools/controlla_testi.py
"""
import json
import os
import re
import struct
import subprocess
import sys

QUI = os.path.dirname(os.path.abspath(__file__))
RADICE = os.path.dirname(QUI)
FONT_DIR = os.path.join(RADICE, 'src', 'font')

# Si leggono le tabelle gia' generate in src/font/, non si rigenerano: cosi'
# si controlla quello che finisce davvero nel firmware, e non serve ne'
# Pillow ne' il file del carattere.
NOMI = {'fontPiccolo': 'fontpiccolo.h', 'fontMedio': 'fontmedio.h',
        'fontGrande': 'fontgrande.h', 'fontEnorme': 'fontenorme.h'}


def leggi_tabella(percorso):
    testo = open(percorso).read()
    corpo = testo[testo.index('{', testo.index('PROGMEM')) + 1:testo.rindex('}')]
    return bytes(int(v, 16) for v in re.findall(r'0[xX]([0-9a-fA-F]{2})', corpo))


def metriche(dati):
    """Solo quello che serve alle larghezze: codice e avanzamento."""
    n = struct.unpack('>i', dati[:4])[0]
    avanzo = {}
    for i in range(n):
        cp, _h, _w, adv, _dY, _dX, _ign = struct.unpack('>7i', dati[24 + i*28:24 + (i+1)*28])
        avanzo[cp] = adv
    return avanzo

# Le 16 direzioni che TinyGPSPlus::cardinal() puo' restituire.
# Due scritte che non si sovrappongono possono comunque leggersi attaccate.
# Fra due etichette vicine ci vuole un varco che si veda.
VARCO_MINIMO = 14

CARDINALI = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE',
             'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW']

# Campi che si cancellano da soli con setTextPadding: il padding deve
# bastare a coprire la scritta piu' larga che ci puo' finire dentro.
# (descrizione, carattere, testi peggiori, chiave della larghezza)
PADDING = [
    ("velocita' in marcia", 'fontEnorme', ['188'],                        'DRV_SPD_W'),
    ("direzione",           'fontGrande', CARDINALI,                      'DRV_DIR_W'),
    ("distanza dalla base", 'fontMedio',  ['8888,8 km', '188 m'],         'DRV_DIR_W'),
    ("valori in basso",     'fontMedio',  ['8888,8', '12h 34m', '-1888'], 'DRV_ST_W'),
    ("distanza alla svolta",'fontGrande', ['188 km', '1,2 km', '900 m', '--'], 'NAV_DST_W'),
    ("velocita' in navigazione", 'fontGrande', ['188'],                   'NAV_SPD_W'),
]

# Etichette fisse che NON si cancellano e stanno una accanto all'altra:
# vanno controllate fra loro, riga per riga.
# (descrizione, carattere, testi, chiave delle X, chiave delle Y)
VICINE = [
    ("etichette in basso", 'fontPiccolo',
     ['VIAGGIO', 'MAX', 'IN MOVIMENTO', 'QUOTA'], 'DRV_ST_X', 'DRV_ST_LY'),
]


def carica_font():
    fonti = {}
    for nome, file in NOMI.items():
        p = os.path.join(FONT_DIR, file)
        if not os.path.exists(p):
            sys.exit('manca %s: genera i caratteri con tools/crea_font.py' % p)
        fonti[nome] = metriche(leggi_tabella(p))
    return fonti


def larghezza(font, nome, testo):
    a = font[nome]
    manca = [c for c in testo if ord(c) not in a]
    return sum(a[ord(c)] for c in testo if ord(c) in a), manca


def misure(rot):
    """Compila dumplayout e gli chiede le misure vere, come fa l'anteprima."""
    exe = os.path.join(QUI, '.dumplayout')
    sorgente = os.path.join(QUI, 'dumplayout.cpp')
    if not os.path.exists(exe) or os.path.getmtime(sorgente) > os.path.getmtime(exe):
        r = subprocess.run(['g++', '-std=gnu++11',
                            '-I', os.path.join(QUI, 'stub'),
                            '-I', os.path.join(RADICE, 'src'),
                            '-o', exe, sorgente],
                           capture_output=True, text=True)
        if r.returncode:
            sys.exit('non riesco a compilare dumplayout:\n' + r.stderr)
    return json.loads(subprocess.check_output([exe, str(rot)]).decode())


def main():
    font = carica_font()
    guasti = 0

    for rot, nome in ((1, 'sdraiato'), (0, 'in piedi')):
        L = misure(rot)
        print('=== %s, %dx%d ===' % (nome, L['SW'], L['SH']))

        for desc, f, testi, chiave in PADDING:
            if chiave not in L:
                print('  %-26s la misura %s non c\'e\': salto' % (desc, chiave))
                continue
            spazio = L[chiave]
            peggio, quale, mancanti = 0, '', []
            for t in testi:
                w, manca = larghezza(font, f, t)
                if manca:
                    mancanti += manca
                if w > peggio:
                    peggio, quale = w, t
            if mancanti:
                print('  %-26s GLIFI MANCANTI: %r' % (desc, ''.join(sorted(set(mancanti)))))
                guasti += 1
            elif peggio > spazio:
                print('  %-26s %-10r %3d px in %3d: SBORDA di %d'
                      % (desc, quale, peggio, spazio, peggio - spazio))
                guasti += 1
            else:
                print('  %-26s %-10r %3d px in %3d  ok' % (desc, quale, peggio, spazio))

        for desc, f, testi, chx, chy in VICINE:
            xs, ys = L[chx], L[chy]
            righe = {}
            for i, t in enumerate(testi):
                w, manca = larghezza(font, f, t)
                if manca:
                    print('  %-26s GLIFI MANCANTI in %r' % (desc, t))
                    guasti += 1
                righe.setdefault(ys[i], []).append((xs[i] - w // 2, xs[i] - w // 2 + w, t))
            sovrapposte = 0
            for y, voci in sorted(righe.items()):
                voci.sort()
                for (s1, d1, t1), (s2, d2, t2) in zip(voci, voci[1:]):
                    if d1 + VARCO_MINIMO > s2:
                        print('  %-26s fra %r e %r restano %d px, ne servono %d (riga y=%d)'
                              % (desc, t1, t2, s2 - d1, VARCO_MINIMO, y))
                        sovrapposte += 1
            guasti += sovrapposte
            if not sovrapposte:
                print('  %-26s %d righe, nessuna sovrapposizione' % (desc, len(righe)))
        print()

    if guasti:
        sys.exit('%d scritte non stanno nel loro spazio' % guasti)
    print('tutte le scritte stanno nel loro spazio')


if __name__ == '__main__':
    main()
