#!/usr/bin/env python3
# Genera il video del montaggio su millefori: disegna la basetta fotogramma per
# fotogramma e li manda a ffmpeg senza passare dal disco.
import subprocess, math, sys
from PIL import Image, ImageDraw, ImageFont

W, H, FPS = 1280, 720, 25
F   = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
FB  = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
FM  = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf'
car = lambda p, d: ImageFont.truetype(p, d)
T   = {n: car(F, n) for n in (15, 17, 19, 22, 26)}
TB  = {n: car(FB, n) for n in (17, 19, 22, 26, 30, 44, 58)}
TM  = {n: car(FM, n) for n in (14, 16, 18, 22)}

BG, GRID, INK, TENUE = (18, 22, 26), (46, 56, 64), (235, 240, 243), (140, 155, 165)
VERDE, RAME, ROSSO, BLU, GIALLO = (53, 196, 106), (224, 164, 106), (232, 83, 79), (74, 163, 255), (240, 169, 59)

# --- la basetta: griglia di fori, 26 colonne x 18 righe -----------------------
COLS, ROWS, PASSO = 26, 20, 24
BX, BY = 72, 132                       # angolo in alto a sinistra della griglia
foro = lambda c, r: (BX + c * PASSO, BY + r * PASSO)

# Dove sta ogni cosa sulla basetta (colonna, riga)
ESP_C1, ESP_C2, ESP_R0, ESP_R1 = 4, 14, 1, 15
RAIL_3V3, RAIL_GND = 18, 19
POS = {
    'morsetti': (1, 1, 2, 1), 'gps': (18, 0, 21, 0),
    'display': (22, 2, 22, 14), 'pulsante': (20, 16, 21, 16),
}
# Punti di attacco sull'ESP32: nomi, non posizioni vere (la serigrafia comanda)
PIN_SX = ['VIN', 'GND', 'D13', 'D12', 'D14', 'D27', 'D26', 'D25',
          'D33', 'D32', 'D35', 'D34', 'VN', 'VP', 'EN']
PIN_DX = ['D23', 'D22', 'TX0', 'RX0', 'D21', 'GND', 'D19', 'D18',
          'D5', 'D17', 'D16', 'D4', 'D0', 'D2', '3V3']

def pin_esp(nome):
    if nome in PIN_SX: return foro(ESP_C1, ESP_R0 + PIN_SX.index(nome))
    if nome in PIN_DX: return foro(ESP_C2, ESP_R0 + PIN_DX.index(nome))
    raise KeyError(nome)

def pin_conn(gruppo, i):
    c0, r0, c1, r1 = POS[gruppo]
    return foro(c0, r0) if c0 == c1 and r0 == r1 else (
        foro(c0 + i, r0) if r0 == r1 else foro(c0, r0 + i))

def pin_rail(quale, x):
    return (x, foro(0, RAIL_3V3 if quale == '3V3' else RAIL_GND)[1])

# --- i fili: (da, a, etichetta, colore) --------------------------------------
def rail(nome): return ('rail', nome)
FILI = [
    ('ALIMENTAZIONE', [
        (('conn', 'morsetti', 0), ('esp', 'VIN'), 'morsetto +  ->  VIN', ROSSO),
        (('conn', 'morsetti', 1), ('esp', 'GND'), 'morsetto -  ->  GND', INK),
        (('esp', '3V3'), rail('3V3'), '3V3  ->  pista 3,3 V', ROSSO),
        (('esp', 'GND'), rail('GND'), 'GND  ->  pista massa', INK),
    ]),
    ('GPS  -  3 fili', [
        (('conn', 'gps', 0), rail('3V3'), 'VCC  ->  pista 3,3 V', ROSSO),
        (('conn', 'gps', 1), rail('GND'), 'GND  ->  pista massa', INK),
        (('conn', 'gps', 2), ('esp', 'D16'), 'TXD  ->  D16', VERDE),
    ]),
    ('DISPLAY  -  9 fili', [
        (('conn', 'display', 0), rail('3V3'), 'VCC  ->  pista 3,3 V', ROSSO),
        (('conn', 'display', 1), rail('GND'), 'GND  ->  pista massa', INK),
        (('conn', 'display', 2), ('esp', 'D5'),  'CS  ->  D5', GIALLO),
        (('conn', 'display', 3), ('esp', 'D22'), 'RESET  ->  D22', GIALLO),
        (('conn', 'display', 4), ('esp', 'D21'), 'DC / RS  ->  D21', BLU),
        (('conn', 'display', 5), ('esp', 'D23'), 'SDI / MOSI  ->  D23', BLU),
        (('conn', 'display', 6), ('esp', 'D18'), 'SCK  ->  D18', BLU),
        (('conn', 'display', 7), rail('3V3'), 'LED  ->  pista 3,3 V', ROSSO),
        (('conn', 'display', 8), ('esp', 'D19'), 'SDO / MISO  ->  D19', BLU),
    ]),
    ('TOUCH  -  4 fili', [
        (('conn', 'display', 9),  ('esp', 'D18'), 'T_CLK  ->  D18  (con SCK)', RAME),
        (('conn', 'display', 10), ('esp', 'D23'), 'T_DIN  ->  D23  (con SDI)', RAME),
        (('conn', 'display', 11), ('esp', 'D19'), 'T_DO  ->  D19  (con SDO)', RAME),
        (('conn', 'display', 12), ('esp', 'D27'), 'T_CS  ->  D27', RAME),
    ]),
    ('PULSANTE  -  2 fili', [
        (('conn', 'pulsante', 0), ('esp', 'D33'), 'un capo  ->  D33', VERDE),
        (('conn', 'pulsante', 1), rail('GND'), 'altro capo  ->  massa', INK),
    ]),
]
N_FILI = sum(len(g[1]) for g in FILI)

def punto(rif, altro=None):
    if rif[0] == 'esp':  return pin_esp(rif[1])
    if rif[0] == 'conn': return pin_conn(rif[1], rif[2])
    x = altro[0] if altro else BX + 10 * PASSO
    return pin_rail(rif[1], x)

# --- disegno -----------------------------------------------------------------
def sfondo():
    im = Image.new('RGB', (W, H), BG)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([BX - 30, BY - 30, BX + (COLS - 1) * PASSO + 30,
                         BY + (ROWS - 1) * PASSO + 30], 14, fill=(26, 32, 38), outline=GRID, width=2)
    for c in range(COLS):
        for r in range(ROWS):
            x, y = foro(c, r)
            d.ellipse([x - 3, y - 3, x + 3, y + 3], fill=GRID)
    return im

BASE = sfondo()

def testo(d, xy, s, f, col=INK, anc='la'):
    d.text(xy, s, font=f, fill=col, anchor=anc)

def blocco(d, x0, y0, x1, y1, col, etichetta, dove='sopra', raggio=9):
    d.rounded_rectangle([x0, y0, x1, y1], raggio, fill=col)
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    if dove == 'sopra':   testo(d, (cx, y0 - 13), etichetta, TB[17], col, 'mm')
    elif dove == 'sinistra': testo(d, (x0 - 10, cy), etichetta, TB[17], col, 'rm')
    else:                 testo(d, (cx, y1 + 13), etichetta, TB[17], col, 'mm')

def disegna_strip(d, col, r0, r1, acceso=1.0):
    x, y0 = foro(col, r0); _, y1 = foro(col, r1)
    c = tuple(int(v * acceso) for v in (90, 105, 118))
    d.rounded_rectangle([x - 11, y0 - 11, x + 11, y1 + 11], 7, fill=c)
    for r in range(r0, r1 + 1):
        _, yy = foro(col, r)
        d.ellipse([x - 4, yy - 4, x + 4, yy + 4], fill=(20, 24, 28))

def disegna_esp(d, acceso=1.0):
    x0, y0 = foro(ESP_C1, ESP_R0); x1, y1 = foro(ESP_C2, ESP_R1)
    c = tuple(int(v * acceso) for v in (47, 59, 77))
    d.rounded_rectangle([x0 + 16, y0 - 14, x1 - 16, y1 + 14], 8, fill=c)
    testo(d, ((x0 + x1) // 2, (y0 + y1) // 2 - 12), 'ESP32', TB[22], (225, 235, 245), 'mm')
    testo(d, ((x0 + x1) // 2, (y0 + y1) // 2 + 12), 'DevKit v1', T[15], (150, 170, 190), 'mm')

def disegna_connettori(d, quali):
    if 'morsetti' in quali:
        a = foro(*POS['morsetti'][:2]); b = foro(*POS['morsetti'][2:])
        blocco(d, a[0] - 13, a[1] - 13, b[0] + 13, b[1] + 13, ROSSO, '5 V')
    if 'gps' in quali:
        a = foro(*POS['gps'][:2]); b = foro(*POS['gps'][2:])
        blocco(d, a[0] - 13, a[1] - 13, b[0] + 13, b[1] + 13, VERDE, 'GPS')
    if 'display' in quali:
        a = foro(*POS['display'][:2]); b = foro(*POS['display'][2:])
        blocco(d, a[0] - 13, a[1] - 13, b[0] + 13, b[1] + 13, RAME, 'DISPLAY', 'sopra')
    if 'pulsante' in quali:
        a = foro(*POS['pulsante'][:2]); b = foro(*POS['pulsante'][2:])
        blocco(d, a[0] - 13, a[1] - 13, b[0] + 13, b[1] + 13, (110, 125, 140), 'TASTO', 'sinistra')

def disegna_piste(d, frazione=1.0):
    for quale, col, nome in (('3V3', ROSSO, 'pista 3,3 V'), ('GND', (170, 185, 195), 'pista massa')):
        y = pin_rail(quale, 0)[1]
        x0 = foro(0, 0)[0]
        x1 = x0 + (COLS - 1) * PASSO * max(0.0, min(1.0, frazione))
        if x1 > x0:
            d.line([x0, y, x1, y], fill=col, width=7)
        testo(d, (x0, y - 20), nome, T[15], TENUE)

def disegna_condensatori(d):
    x, y = foro(8, RAIL_3V3 - 1)
    d.rounded_rectangle([x - 12, y - 20, x + 12, y + 20], 11, fill=(70, 90, 110), outline=INK, width=2)
    testo(d, (x, y), '+', TB[17], INK, 'mm')
    testo(d, (x + 22, y), '470 uF', T[15], TENUE, 'lm')
    x2, y2 = foro(12, RAIL_3V3 - 1)
    d.rounded_rectangle([x2 - 11, y2 - 13, x2 + 11, y2 + 13], 5, fill=(120, 110, 70))
    testo(d, (x2 + 20, y2), '100 nF', T[15], TENUE, 'lm')

def disegna_filo(d, filo, avanzamento=1.0, acceso=False):
    a = punto(filo[0]); b = punto(filo[1], a)
    mx = (a[0] + b[0]) // 2
    via = [a, (mx, a[1]), (mx, b[1]), b]
    # lunghezze dei tratti per far "crescere" il filo
    tot = sum(math.dist(via[i], via[i + 1]) for i in range(3)) or 1
    fatto, pts = tot * avanzamento, [via[0]]
    for i in range(3):
        seg = math.dist(via[i], via[i + 1])
        if fatto >= seg: pts.append(via[i + 1]); fatto -= seg
        else:
            t = fatto / seg if seg else 0
            pts.append((via[i][0] + (via[i+1][0]-via[i][0]) * t,
                        via[i][1] + (via[i+1][1]-via[i][1]) * t))
            break
    col = filo[3]
    if len(pts) > 1:
        d.line(pts, fill=col, width=6 if acceso else 4, joint='curve')
    if acceso:
        for p in (a, pts[-1]):
            d.ellipse([p[0]-6, p[1]-6, p[0]+6, p[1]+6], fill=col)

def pannello(d, titolo, voci, indice=-1):
    x0 = 790
    testo(d, (x0, 110), titolo, TB[22], INK)
    for i, v in enumerate(voci):
        y = 152 + i * 33
        if i == indice:
            d.rounded_rectangle([x0 - 10, y - 11, 1258, y + 18], 7, fill=(40, 52, 62))
        col = INK if i <= indice else TENUE
        testo(d, (x0, y), ('v ' if i < indice else '  ') + v, TM[14], col)

def intestazione(d, titolo, sotto=None):
    testo(d, (72, 34), titolo, TB[30], INK)
    if sotto: testo(d, (72, 70), sotto, T[17], TENUE)

def didascalia(d, s, col=TENUE):
    d.rectangle([0, 648, W, H], fill=(12, 15, 18))
    testo(d, (72, 684), s, T[19], col, 'lm')

# --- scene -------------------------------------------------------------------
def frame_vuoto():
    return BASE.copy()

def sc_titolo(t, dur):
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    a = min(1.0, t / 0.8)
    testo(d, (W//2, 300), 'Montaggio su millefori', TB[58],
          tuple(int(c * a) for c in INK), 'mm')
    testo(d, (W//2, 362), f'ESP32 GPS Tracker  -  {N_FILI} fili, uno per volta', T[26],
          tuple(int(c * a) for c in TENUE), 'mm')
    if t > 1.4:
        b = min(1.0, (t - 1.4) / 0.6)
        d.line([W//2 - 160, 408, W//2 + 160, 408], fill=tuple(int(c*b) for c in VERDE), width=3)
    return im

def sc_basetta(t, dur):
    im = frame_vuoto(); d = ImageDraw.Draw(im)
    intestazione(d, 'La basetta', 'millefori 7x9 cm, fori a 2,54 mm')
    didascalia(d, 'Ogni foro ha la sua piazzola di rame, isolata dalle altre: sei tu a decidere cosa collega cosa.')
    return im

def sc_strip(t, dur):
    im = frame_vuoto(); d = ImageDraw.Draw(im)
    intestazione(d, '1. Le strip femmina', "l'ESP32 va infilato, mai saldato direttamente")
    a = min(1.0, t / 1.2)
    disegna_strip(d, ESP_C1, ESP_R0, ESP_R1, a)
    disegna_strip(d, ESP_C2, ESP_R0, ESP_R1, a)
    if t > 1.6: disegna_esp(d, min(1.0, (t - 1.6) / 1.0))
    didascalia(d, 'Salda prima i due pin agli angoli, controlla che siano dritte, poi gli altri 26.')
    return im

def sc_connettori(t, dur):
    im = frame_vuoto(); d = ImageDraw.Draw(im)
    intestazione(d, '2. I connettori', 'alimentazione, GPS, display, pulsante')
    disegna_strip(d, ESP_C1, ESP_R0, ESP_R1); disegna_strip(d, ESP_C2, ESP_R0, ESP_R1); disegna_esp(d)
    quali = [q for i, q in enumerate(('morsetti', 'gps', 'display', 'pulsante')) if t > 0.5 + i * 0.7]
    disegna_connettori(d, quali)
    didascalia(d, 'Stesso trucco: due pin agli angoli, controlli, poi tutti gli altri.')
    return im

def sc_piste(t, dur):
    im = frame_vuoto(); d = ImageDraw.Draw(im)
    intestazione(d, '3. Le due piste', 'un filo rigido nudo per il 3,3 V, uno per la massa')
    disegna_strip(d, ESP_C1, ESP_R0, ESP_R1); disegna_strip(d, ESP_C2, ESP_R0, ESP_R1); disegna_esp(d)
    disegna_connettori(d, ('morsetti', 'gps', 'display', 'pulsante'))
    disegna_piste(d, t / 1.8)
    if t > 2.2: disegna_condensatori(d)
    didascalia(d, 'Saldale ogni cinque o sei fori, non solo alle estremita. Poi i due condensatori.')
    return im

def scena_fili(gruppo):
    titolo, fili = FILI[gruppo]
    per_filo = 1.15
    def f(t, dur):
        im = frame_vuoto(); d = ImageDraw.Draw(im)
        intestazione(d, titolo, 'uno per volta, spuntando la lista')
        disegna_connettori(d, ('morsetti', 'gps', 'display', 'pulsante'))
        disegna_piste(d); disegna_condensatori(d)
        # i fili dei gruppi precedenti restano, spenti
        disegna_esp(d)
        disegna_strip(d, ESP_C1, ESP_R0, ESP_R1); disegna_strip(d, ESP_C2, ESP_R0, ESP_R1)
        for g in range(gruppo):
            for filo in FILI[g][1]:
                d_col = tuple(int(c * 0.35) for c in filo[3])
                disegna_filo(d, (filo[0], filo[1], filo[2], d_col))
        n = int(t / per_filo)
        for i, filo in enumerate(fili):
            if i < n: disegna_filo(d, filo)
            elif i == n: disegna_filo(d, filo, (t - i * per_filo) / per_filo, acceso=True)
        pannello(d, titolo.split('  -')[0], [x[2] for x in fili], min(n, len(fili) - 1))
        didascalia(d, 'Le posizioni dei pin sul disegno sono indicative: sulla scheda leggi la serigrafia.')
        return im
    return f, per_filo * len(fili) + 0.8

PROVE = [
    'tester in continuita: 3,3 V e massa NON devono fischiare',
    'controlla i fili uno per uno',
    'solo ESP32 sulla USB: deve comparire il log',
    'misura 3,2 - 3,3 V sulla pista',
    'attacca il GPS: "modulo riconosciuto a 9600 baud"',
    'attacca il display: logo e menu',
    'tieni premuto il tasto 3 secondi: taratura del tocco',
]

def sc_collaudo(t, dur):
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    intestazione(d, 'Il collaudo', 'un pezzo per volta, sul banco, con la USB')
    n = int(t / 1.25)
    for i, p in enumerate(PROVE):
        y = 150 + i * 62
        if i > n: continue
        acceso = i == n and (t - i * 1.25) < 0.9
        col = VERDE if i < n else (INK if acceso else TENUE)
        d.rounded_rectangle([72, y - 22, 1208, y + 24], 9,
                            fill=(32, 42, 38) if i < n else (26, 32, 38))
        d.ellipse([92, y - 12, 114, y + 10], outline=col, width=3,
                  fill=col if i < n else None)
        if i < n:
            d.line([97, y, 102, y + 6], fill=(18, 24, 20), width=3)
            d.line([102, y + 6, 110, y - 5], fill=(18, 24, 20), width=3)
        testo(d, (134, y), p, T[19] if i >= n else T[19], INK if i <= n else TENUE, 'lm')
    didascalia(d, 'Se sbagli un filo, vuoi scoprirlo con il solo ESP32 attaccato: non con tutto collegato.', GIALLO)
    return im

def sc_fine(t, dur):
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    a = min(1.0, t / 0.7)
    testo(d, (W//2, 276), f'{N_FILI} fili, una sera', TB[44], tuple(int(c*a) for c in INK), 'mm')
    testo(d, (W//2, 338), 'fatta con calma, senza fretta', T[26], tuple(int(c*a) for c in TENUE), 'mm')
    if t > 1.0:
        b = min(1.0, (t - 1.0) / 0.7)
        testo(d, (W//2, 420), 'scarico di trazione su ogni cavo che esce', T[22],
              tuple(int(c*b) for c in RAME), 'mm')
        testo(d, (W//2, 456), 'e una foto della basetta prima di chiuderla', T[22],
              tuple(int(c*b) for c in RAME), 'mm')
    return im

SCENE = [(sc_titolo, 3.0), (sc_basetta, 2.6), (sc_strip, 4.2), (sc_connettori, 4.2), (sc_piste, 4.4)]
for g in range(len(FILI)):
    f, dur = scena_fili(g)
    SCENE.append((f, dur))
SCENE += [(sc_collaudo, 10.0), (sc_fine, 4.0)]

durata = sum(d for _, d in SCENE)
print(f'scene: {len(SCENE)}, durata {durata:.1f} s, fotogrammi {int(durata*FPS)}', file=sys.stderr)

uscita = sys.argv[1] if len(sys.argv) > 1 else 'montaggio.mp4'
ff = subprocess.Popen(
    ['ffmpeg', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{W}x{H}',
     '-framerate', str(FPS), '-i', '-', '-c:v', 'libx264', '-preset', 'medium',
     '-crf', '23', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', uscita],
    stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

n = 0
for fn, dur in SCENE:
    for i in range(int(dur * FPS)):
        ff.stdin.write(fn(i / FPS, dur).tobytes())
        n += 1
        if n % 250 == 0: print(f'  {n} fotogrammi', file=sys.stderr)
ff.stdin.close()
ff.wait()
print(f'fatto: {n} fotogrammi', file=sys.stderr)
