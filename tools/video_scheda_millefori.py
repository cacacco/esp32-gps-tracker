#!/usr/bin/env python3
# Video del montaggio sulla millefori: zoccolo ESP32, piste, cavo a nastro IDC.
# Disegna ogni fotogramma con Pillow e lo manda a ffmpeg senza passare dal disco.
import subprocess, math, sys
from PIL import Image, ImageDraw, ImageFont

W, H, FPS = 1280, 720, 25
F  = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
FB = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
FM = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf'
car = lambda p, d: ImageFont.truetype(p, d)
T  = {n: car(F, n) for n in (13, 15, 17, 19, 22, 26)}
TB = {n: car(FB, n) for n in (13, 15, 17, 19, 22, 26, 30, 40, 52)}
TM = {n: car(FM, n) for n in (11, 12, 13, 14, 16, 18, 22)}

BG, INK, TENUE = (18, 22, 26), (235, 240, 243), (140, 155, 165)
VERDE, RAME, ROSSO, BLU, GIALLO = (53, 196, 106), (224, 164, 106), (232, 83, 79), (74, 163, 255), (240, 169, 59)
GRIGIO, NERO = (168, 180, 190), (96, 106, 116)
PCB, PCB_B, FORO, RING = (28, 70, 54), (44, 104, 80), (14, 18, 20), (196, 150, 96)
ESP_C, STRISCIA, IDCC = (30, 36, 44), (22, 26, 30), (34, 38, 44)

# --- griglia della scheda principale: 27 x 19 fori, passo 2,54 ---------------
P, BX0, BY0 = 24, 150, 150
NX, NY = 27, 19
hx = lambda x: BX0 + (x - 1) * P
hy = lambda y: BY0 + (y - 1) * P

# serigrafia della DevKit v1, con la USB che guarda a sinistra
PIN_ALTO  = ['VIN', 'GND', 'D13', 'D12', 'D14', 'D27', 'D26', 'D25',
             'D33', 'D32', 'D35', 'D34', 'VN', 'VP', 'EN']
PIN_BASSO = ['3V3', 'GND', 'D15', 'D2', 'D4', 'RX2', 'TX2', 'D5',
             'D18', 'D19', 'D21', 'RX0', 'TX0', 'D22', 'D23']
Y_ALTO, Y_BASSO, X_ESP = 5, 15, 3
PISTA = {'3V3': 17, 'GND': 19}
PISTA_X0, PISTA_X1 = 3, 26

def ze(nome):
    """Foro dello zoccolo ESP32. GND e' ambiguo: si prende quello basso."""
    if nome in PIN_BASSO: return (X_ESP + PIN_BASSO.index(nome), Y_BASSO)
    return (X_ESP + PIN_ALTO.index(nome), Y_ALTO)

idc   = lambda n: (25 if n % 2 else 26, 6 + (n - 1) // 2)   # nastro, in verticale
pista = lambda nome, x: (x, PISTA[nome])
GPS_J   = {1: (19, 2), 2: (20, 2), 3: (21, 2)}
TASTO_J = {1: (23, 2), 2: (24, 2)}

FILI = [
    ('Alimentazione della scheda', [
        (ze('3V3'), pista('3V3', 3), '3V3 dello zoccolo  ->  pista rossa', ROSSO),
        (ze('GND'), pista('GND', 4), 'GND dello zoccolo  ->  pista di massa', INK),
    ]),
    ('GPS: tre fili e basta', [
        (ze('VIN'), GPS_J[1], 'VIN  ->  VCC del GPS      (5 V, non 3,3)', GIALLO),
        (GPS_J[2], pista('GND', 20), 'GND del GPS  ->  pista di massa', INK),
        (ze('RX2'), GPS_J[3], 'RX2 = D16  <-  TXD del GPS      unico segnale', VERDE),
    ]),
    ('Pulsante del menu', [
        (ze('D32'), TASTO_J[1], 'D32  ->  un capo del pulsante', GRIGIO),
        (TASTO_J[2], pista('GND', 24), 'altro capo  ->  pista di massa', INK),
    ]),
    ('Display: alimentazione e comandi', [
        (pista('3V3', 25), idc(1), 'pista rossa  ->  nastro 1      VCC display', ROSSO),
        (pista('GND', 26), idc(2), 'massa  ->  nastro 2      GND display', INK),
        (ze('D5'),  idc(3), 'D5  ->  nastro 3      CS', GIALLO),
        (ze('D22'), idc(4), 'D22  ->  nastro 4      RESET', GIALLO),
        (ze('D21'), idc(5), 'D21  ->  nastro 5      DC / RS', GIALLO),
    ]),
    ('Display: SPI, touch e masse', [
        (ze('D23'), idc(6), 'D23  ->  nastro 6      MOSI', BLU),
        (pista('GND', 23), idc(7), 'massa  ->  nastro 7      a fianco dello SCK', INK),
        (ze('D18'), idc(8), 'D18  ->  nastro 8      SCK', BLU),
        (pista('GND', 22), idc(9), 'massa  ->  nastro 9      a fianco dello SCK', INK),
        (ze('D19'), idc(10), 'D19  ->  nastro 10      MISO', BLU),
        (ze('D27'), idc(11), 'D27  ->  nastro 11      T_CS del touch', RAME),
        (ze('D33'), idc(12), 'D33  ->  nastro 12      LED, la luce', ROSSO),
        (pista('GND', 21), idc(16), 'massa  ->  nastro 16      ultima massa', INK),
    ]),
]
N_FILI = sum(len(g[1]) for g in FILI)

# --- schedina del display: 15 x 9 fori ---------------------------------------
P2, DX0, DY0 = 46, 318, 232
DSP = ['VCC', 'GND', 'CS', 'RESET', 'DC', 'MOSI', 'SCK', 'LED',
       'MISO', 'T_CLK', 'T_CS', 'T_DIN', 'T_DO', 'T_IRQ']
dx2 = lambda x: DX0 + (x - 1) * P2
dy2 = lambda y: DY0 + (y - 1) * P2
dp    = lambda nome: (1 + DSP.index(nome), 1)
didc  = lambda n: (4 + (n - 1) // 2, 7 if n % 2 else 8)

FILI_DSP = [
    ('La schedina del display: i collegamenti diretti', [
        (dp('VCC'),   didc(1),  'VCC  ->  1', ROSSO),
        (dp('GND'),   didc(2),  'GND  ->  2', INK),
        (dp('CS'),    didc(3),  'CS  ->  3', GIALLO),
        (dp('RESET'), didc(4),  'RESET  ->  4', GIALLO),
        (dp('DC'),    didc(5),  'DC / RS  ->  5', GIALLO),
        (dp('MOSI'),  didc(6),  'SDI / MOSI  ->  6', BLU),
        (dp('SCK'),   didc(8),  'SCK  ->  8', BLU),
        (dp('MISO'),  didc(10), 'SDO / MISO  ->  10', BLU),
        (dp('T_CS'),  didc(11), 'T_CS  ->  11', RAME),
        (dp('LED'),   didc(12), 'LED  ->  12', ROSSO),
    ]),
    ('La schedina del display: i tre ponticelli e le masse', [
        (dp('T_CLK'), dp('SCK'),  'T_CLK  ->  SCK       stesso pin dell ESP32', RAME),
        (dp('T_DIN'), dp('MOSI'), 'T_DIN  ->  MOSI     stesso pin dell ESP32', RAME),
        (dp('T_DO'),  dp('MISO'), 'T_DO  ->  MISO      stesso pin dell ESP32', RAME),
        (didc(2), didc(7),  'massa  ->  7', INK),
        (didc(2), didc(9),  'massa  ->  9', INK),
        (didc(2), didc(16), 'massa  ->  16', INK),
    ]),
]

NASTRO = [('1', '3V3', 'VCC del display', ROSSO), ('2', 'GND', 'massa', INK),
          ('3', 'D5', 'CS', GIALLO), ('4', 'D22', 'RESET', GIALLO),
          ('5', 'D21', 'DC / RS', GIALLO), ('6', 'D23', 'SDI / MOSI', BLU),
          ('7', 'GND', 'massa', INK), ('8', 'D18', 'SCK', BLU),
          ('9', 'GND', 'massa', INK), ('10', 'D19', 'SDO / MISO', BLU),
          ('11', 'D27', 'T_CS', RAME), ('12', 'D33', 'LED', ROSSO),
          ('13', '-', 'riserva', NERO), ('14', '-', 'riserva', NERO),
          ('15', '-', 'riserva', NERO), ('16', 'GND', 'massa', INK)]

# --- disegno -----------------------------------------------------------------
def testo(d, xy, s, f, col=INK, anc='la'):
    d.text(xy, s, font=f, fill=col, anchor=anc)

def sigla(d, xy, s, f, col, anc='rm'):
    """Etichetta leggibile anche sopra la griglia dei fori."""
    b = d.textbbox(xy, s, font=f, anchor=anc)
    d.rounded_rectangle([b[0] - 5, b[1] - 4, b[2] + 5, b[3] + 4], 4, fill=(12, 15, 18))
    d.text(xy, s, font=f, fill=col, anchor=anc)

def testo_su(im, xy, s, f, col, sotto=False):
    """Etichetta verticale, sopra (o sotto) un foro."""
    b = f.getbbox(s)
    tmp = Image.new('RGBA', (b[2] + 6, b[3] + 6), (0, 0, 0, 0))
    ImageDraw.Draw(tmp).text((2, 0), s, font=f, fill=col)
    tmp = tmp.rotate(90, expand=True)
    y = xy[1] if sotto else xy[1] - tmp.height
    im.paste(tmp, (int(xy[0] - tmp.width / 2), int(y)), tmp)

def intestazione(d, titolo, sotto=None):
    testo(d, (72, 28), titolo, TB[30], INK)
    if sotto: testo(d, (72, 64), sotto, T[17], TENUE)

def didascalia(d, s, col=TENUE):
    d.rectangle([0, 660, W, H], fill=(12, 15, 18))
    testo(d, (72, 690), s, T[19], col, 'lm')

def scheda(d, X0, Y0, nx, ny, passo, fori=True):
    x0, y0 = X0 - passo // 2 - 4, Y0 - passo // 2 - 4
    x1, y1 = X0 + (nx - 1) * passo + passo // 2 + 4, Y0 + (ny - 1) * passo + passo // 2 + 4
    d.rounded_rectangle([x0, y0, x1, y1], 8, fill=PCB, outline=PCB_B, width=2)
    if not fori: return
    r = max(2, passo // 7)
    for i in range(nx):
        for j in range(ny):
            cx, cy = X0 + i * passo, Y0 + j * passo
            d.ellipse([cx - r - 2, cy - r - 2, cx + r + 2, cy + r + 2], outline=RING, width=1)
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=FORO)

def filo(d, a, b, col, px, py, larg=3, curva=0, prog=1.0):
    ax, ay, bx, by = px(a[0]), py(a[1]), px(b[0]), py(b[1])
    ux, uy = bx - ax, by - ay
    L = math.hypot(ux, uy) or 1
    nx, ny = -uy / L, ux / L
    pts, N = [], 28
    for i in range(N + 1):
        t = i / N
        if t > prog: break
        s = math.sin(math.pi * t) * curva
        pts.append((ax + ux * t + nx * s, ay + uy * t + ny * s))
    if len(pts) > 1:
        d.line(pts, fill=col, width=larg, joint='curve')
    if prog >= 1.0:
        for cx, cy in ((ax, ay), (bx, by)):
            d.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], fill=col)

def smorza(col, k):
    return tuple(int(c * k + 26 * (1 - k)) for c in col)

def piste(d, k=1.0):
    for nome, col in (('3V3', ROSSO), ('GND', NERO)):
        y = hy(PISTA[nome])
        d.line([hx(PISTA_X0), y, hx(PISTA_X1), y], fill=smorza(col, k), width=7)
        sigla(d, (hx(PISTA_X0) - 14, y), nome, TM[12], smorza(col, k))

def zoccolo(d, etich=1.0, modulo=True):
    for y in (Y_ALTO, Y_BASSO):
        d.rounded_rectangle([hx(X_ESP) - 11, hy(y) - 11, hx(X_ESP + 14) + 11, hy(y) + 11],
                            5, fill=STRISCIA, outline=(70, 80, 90))
        for i in range(15):
            cx = hx(X_ESP + i)
            d.ellipse([cx - 5, hy(y) - 5, cx + 5, hy(y) + 5], fill=(10, 12, 14), outline=(96, 108, 118))
    if modulo:
        d.rounded_rectangle([hx(X_ESP) - 16, hy(Y_ALTO), hx(X_ESP + 14) + 16, hy(Y_BASSO)],
                            6, fill=ESP_C, outline=(64, 74, 86), width=2)
        d.rounded_rectangle([hx(X_ESP) - 40, hy(10) - 16, hx(X_ESP) - 12, hy(10) + 16],
                            4, fill=(150, 158, 166))
        testo(d, ((hx(X_ESP) + hx(X_ESP + 14)) // 2, hy(10)), 'ESP32', TB[19], (120, 134, 148), 'mm')

def connettori(d, k=1.0):
    x0, x1 = hx(25) - 13, hx(26) + 13
    y0, y1 = hy(6) - 13, hy(13) + 13
    d.rounded_rectangle([x0 - 5, y0 - 5, x1 + 5, y1 + 5], 4, fill=IDCC, outline=smorza((110, 122, 134), k), width=2)
    d.polygon([(x0 - 5, hy(9) + 12), (x0 - 14, hy(9) + 12), (x0 - 14, hy(10) - 12), (x0 - 5, hy(10) - 12)],
              fill=IDCC, outline=smorza((110, 122, 134), k))
    for n in range(1, 17):
        cx, cy = hx(idc(n)[0]), hy(idc(n)[1])
        d.rectangle([cx - 3, cy - 3, cx + 3, cy + 3], fill=smorza((214, 220, 226), k))
    sigla(d, (hx(25) - 20, hy(6)), '1', TM[14], smorza(GIALLO, k))
    for nome, j, dove in (('GPS', GPS_J, 'sx'), ('TASTO', TASTO_J, 'dx')):
        xs = [v[0] for v in j.values()]
        d.rounded_rectangle([hx(min(xs)) - 11, hy(2) - 13, hx(max(xs)) + 11, hy(2) + 13],
                            4, fill=(36, 60, 48), outline=smorza(VERDE, k))
        if dove == 'sx': sigla(d, (hx(min(xs)) - 16, hy(2)), nome, TM[12], smorza(VERDE, k))
        else:            sigla(d, (hx(max(xs)) + 16, hy(2)), nome, TM[12], smorza(VERDE, k), 'lm')

# --- scene -------------------------------------------------------------------
def sc_titolo(t, dur):
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    a = min(1.0, t / 0.8)
    testo(d, (W // 2, 270), 'La scheda millefori', TB[52], tuple(int(c * a) for c in INK), 'mm')
    testo(d, (W // 2, 334), 'due schedine, un cavo piatto, niente che si sfila', T[26],
          tuple(int(c * a) for c in TENUE), 'mm')
    if t > 1.2:
        b = min(1.0, (t - 1.2) / 0.7)
        testo(d, (W // 2, 428), 'ESP32 + display ST7796 + GPS NEO-7M', T[22],
              tuple(int(c * b) for c in RAME), 'mm')
        testo(d, (W // 2, 464), 'Tracker GPS  -  BMW S 1000 XR', T[19],
              tuple(int(c * b) for c in TENUE), 'mm')
    return im

def sc_pezzi(t, dur):
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    intestazione(d, 'Tre pezzi', 'e il cavo che li tiene insieme')
    box = [(96, 'SCHEDA PRINCIPALE', ['millefori 5 x 7 cm', 'zoccolo ESP32', 'piste 3,3 V e massa', 'connettore a nastro'], VERDE),
           (486, 'SCHEDINA DISPLAY', ['millefori 40 x 25 mm', 'striscia da 14 poli', 'connettore a nastro', 'tre ponticelli'], BLU),
           (876, 'CAVO A NASTRO', ['16 poli, 20-30 cm', 'femmina su entrambi', 'la tacca non sbaglia', '10 segnali, 4 masse'], RAME)]
    for i, (x, tit, righe, col) in enumerate(box):
        if t < 0.3 + i * 0.8: continue
        d.rounded_rectangle([x, 150, x + 308, 430], 10, fill=(26, 32, 38), outline=col, width=2)
        testo(d, (x + 154, 186), tit, TB[19], col, 'mm')
        for j, r in enumerate(righe):
            testo(d, (x + 154, 240 + j * 42), r, T[17], INK, 'mm')
    if t > 3.0:
        testo(d, (W // 2, 500), 'Il display si stacca con un gesto. Il resto resta avvitato nella scatola.',
              T[22], INK, 'mm')
    if t > 4.0:
        testo(d, (W // 2, 544), 'Nessun connettore puo sfilarsi da solo: e tutto saldato o a scatto.',
              T[19], VERDE, 'mm')
    didascalia(d, 'L adattatore a morsetti tienilo: resta il banco di prova per le modifiche al volo.', TENUE)
    return im

def sc_griglia(t, dur):
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    intestazione(d, '1. Dove va cosa', 'millefori 5 x 7 cm:  27 fori di lato, 19 di altezza')
    scheda(d, hx(1), hy(1), NX, NY, P)
    if t > 0.8: zoccolo(d, modulo=t > 1.6)
    if t > 2.4: piste(d)
    if t > 3.2: connettori(d)
    voci = [(1.0, 'lo zoccolo dell ESP32 sta al centro', TENUE),
            (1.8, 'la USB guarda a sinistra, verso il bordo', GIALLO),
            (2.6, 'le due piste corrono sotto, lungo il bordo', ROSSO),
            (3.4, 'il nastro esce a destra, i JST in alto', VERDE)]
    for ts, s, col in voci:
        if t > ts:
            testo(d, (830, 190 + voci.index((ts, s, col)) * 48), s, T[19], col, 'lm')
    if t > 4.4:
        testo(d, (830, 420), 'Tutto il resto della scheda', T[17], TENUE, 'lm')
        testo(d, (830, 450), 'resta libero: ci passano i fili,', T[17], TENUE, 'lm')
        testo(d, (830, 480), 'che vanno sul RETRO.', T[17], RAME, 'lm')
    didascalia(d, 'I fori sono a 2,54 mm. Le due file dello zoccolo distano esattamente 10 fori: 25,4 mm.')
    return im

ORDINE = [('1.  le due strisce femmina', 'il pezzo piu basso: si salda per primo'),
          ('2.  il connettore a nastro', 'e i due JST'),
          ('3.  le due piste', 'filo di rame stagnato nudo'),
          ('4.  i venti ponticelli', 'uno per volta, spuntandoli'),
          ('5.  il collaudo', 'prima di infilare l ESP32')]

def sc_ordine(t, dur):
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    intestazione(d, '2. L ordine conta', 'dal piu basso al piu alto: altrimenti la scheda non appoggia')
    for i, (tit, sot) in enumerate(ORDINE):
        if t < 0.4 + i * 0.9: continue
        y = 170 + i * 84
        d.rounded_rectangle([130, y, 1150, y + 66], 9, fill=(26, 32, 38))
        testo(d, (168, y + 24), tit, TB[22], INK, 'lm')
        testo(d, (168, y + 50), sot, T[15], TENUE, 'lm')
    if t > 4.8:
        didascalia(d, 'Saldi prima il connettore alto e poi la striscia bassa? La striscia non tocca piu il rame.', GIALLO)
    else:
        didascalia(d, 'Appoggia la scheda a faccia in giu sul tavolo: il peso tiene il pezzo in sede mentre saldi.')
    return im

def sc_piste(t, dur):
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    intestazione(d, '3. Le due piste', 'filo di rame stagnato nudo, infilato nei fori e saldato piazzola per piazzola')
    scheda(d, hx(1), hy(1), NX, NY, P)
    zoccolo(d, modulo=False)
    k = min(1.0, max(0.0, (t - 0.6) / 1.2))
    if k > 0:
        for nome, col in (('3V3', ROSSO), ('GND', NERO)):
            y = hy(PISTA[nome])
            x1 = hx(PISTA_X0) + (hx(PISTA_X1) - hx(PISTA_X0)) * k
            d.line([hx(PISTA_X0), y, x1, y], fill=col, width=7)
            if k >= 1.0: sigla(d, (hx(PISTA_X0) - 14, y), nome, TM[12], col)
    if t > 2.2:
        testo(d, (830, 200), 'Fila 17:  pista 3,3 V', T[22], ROSSO, 'lm')
        testo(d, (830, 236), 'Fila 19:  pista di massa', T[22], GRIGIO, 'lm')
        testo(d, (830, 288), 'Fra le due resta una fila vuota:', T[17], TENUE, 'lm')
        testo(d, (830, 316), 'e lo spazio che impedisce il corto.', T[17], TENUE, 'lm')
    if t > 3.4:
        testo(d, (830, 390), 'Scalda la piazzola, non il filo.', T[19], RAME, 'lm')
        testo(d, (830, 420), 'Tre piazzole, poi ricontrolla', T[19], RAME, 'lm')
        testo(d, (830, 450), 'che il filo sia ancora dritto.', T[19], RAME, 'lm')
    didascalia(d, 'Tester in continuita fra le due piste: NON deve fischiare. Se fischia, cerchi il corto adesso.', GIALLO)
    return im

USATI = {'VIN': GIALLO, 'D27': RAME, 'D33': ROSSO, 'D32': GRIGIO,
         '3V3': ROSSO, 'GND': GRIGIO, 'RX2': VERDE, 'D5': GIALLO, 'D22': GIALLO,
         'D21': GIALLO, 'D23': BLU, 'D18': BLU, 'D19': BLU}
LEGENDA = [(136, ['VIN   ->  5 V al GPS', 'D27   ->  T_CS del touch',
                  'D33   ->  LED del display', 'D32   ->  pulsante del menu']),
           (700, ['3V3 e GND   ->  le due piste', 'RX2 = D16   ->  TXD del GPS',
                  'D5  D22  D21   ->  CS  RESET  DC', 'D23  D18  D19   ->  MOSI  SCK  MISO'])]
ZX0, ZPASSO, ZY_ALTO, ZY_BASSO = 136, 72, 250, 450

def sc_zoccolo(t, dur):
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    intestazione(d, '4. Lo zoccolo e le sigle', 'due strisce femmina da 15 poli, USB a sinistra, 10 fori fra le due file')
    zx = lambda i: ZX0 + i * ZPASSO
    d.rounded_rectangle([zx(0) - 30, ZY_ALTO, zx(14) + 30, ZY_BASSO], 8,
                        fill=ESP_C, outline=(64, 74, 86), width=2)
    d.rounded_rectangle([zx(0) - 66, 334, zx(0) - 26, 366], 4, fill=(150, 158, 166))
    testo(d, (zx(0) - 46, 386), 'USB', TM[11], TENUE, 'mm')
    testo(d, ((zx(0) + zx(14)) // 2, 350), 'ESP32-WROOM-32  DevKit v1', TB[22], (120, 134, 148), 'mm')
    for riga, (pin, py, su) in enumerate(((PIN_ALTO, ZY_ALTO, True), (PIN_BASSO, ZY_BASSO, False))):
        if t < 0.6 + riga * 1.0: continue
        d.rounded_rectangle([zx(0) - 26, py - 15, zx(14) + 26, py + 15], 6,
                            fill=STRISCIA, outline=(70, 80, 90))
        for i, nome in enumerate(pin):
            col = USATI.get(nome)
            if riga == 0 and nome == 'GND': col = None      # si usa quello basso
            d.ellipse([zx(i) - 11, py - 11, zx(i) + 11, py + 11],
                      fill=(10, 12, 14), outline=col or (96, 108, 118), width=3 if col else 1)
            testo(d, (zx(i), py - 26 if su else py + 26), nome, TM[14],
                  col or (92, 102, 112), 'mb' if su else 'mt')
    if t > 2.8:
        for x, righe in LEGENDA:
            for j, r in enumerate(righe):
                testo(d, (x, 540 + j * 28), r, T[17], INK, 'lm')
    didascalia(d, 'Infila l ESP32 nelle strisce PRIMA di saldarle: cosi restano allineate e poi entra davvero.', GIALLO)
    return im

def scena_fili(gi):
    titolo, lista = FILI[gi]
    fatti = sum(len(FILI[k][1]) for k in range(gi))
    def sc(t, dur):
        im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
        intestazione(d, titolo, f'filo {fatti + 1} - {fatti + len(lista)} di {N_FILI}')
        scheda(d, hx(1), hy(1), NX, NY, P)
        zoccolo(d, modulo=False)
        piste(d)
        connettori(d)
        for k in range(gi):
            for j, (a, b, _, col) in enumerate(FILI[k][1]):
                filo(d, a, b, smorza(col, 0.3), hx, hy, 2, curva=(j % 5 - 2) * 9)
        n = int(t / 0.85)
        for j, (a, b, et, col) in enumerate(lista):
            if j > n: continue
            pr = min(1.0, (t - j * 0.85) / 0.45) if j == n else 1.0
            filo(d, a, b, col if j == n else smorza(col, 0.55), hx, hy,
                 4 if j == n else 2, curva=(j % 5 - 2) * 9, prog=pr)
        for j, (a, b, et, col) in enumerate(lista):
            y = 190 + j * 40
            if j > n:
                testo(d, (830, y), '.', T[19], (54, 62, 70), 'lm')
            else:
                testo(d, (830, y), et, T[17], col if j == n else TENUE, 'lm')
        didascalia(d, 'I ponticelli vanno sul retro, filo rigido: resta dove lo pieghi e non fa ponte con i vicini.')
        return im
    return (sc, 0.85 * len(lista) + 1.6)

def sc_dsp(t, dur):
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    intestazione(d, '5. La schedina del display', 'un ritaglio da 40 x 25 mm: trasforma 14 pin in fila in un connettore a scatto')
    scheda(d, dx2(1), dy2(1), 15, 9, P2)
    if t > 0.6:
        d.rounded_rectangle([dx2(1) - 20, dy2(1) - 20, dx2(14) + 20, dy2(1) + 20], 6,
                            fill=STRISCIA, outline=(70, 80, 90))
        for i in range(14):
            cx = dx2(1 + i)
            d.ellipse([cx - 9, dy2(1) - 9, cx + 9, dy2(1) + 9], fill=(10, 12, 14), outline=(96, 108, 118))
    if t > 1.4:
        for i, nome in enumerate(DSP):
            testo_su(im, (dx2(1 + i), dy2(1) - 26), nome, TM[12],
                     (62, 70, 78) if nome == 'T_IRQ' else INK)
        d = ImageDraw.Draw(im)
    if t > 2.4:
        d.rounded_rectangle([dx2(4) - 26, dy2(7) - 26, dx2(11) + 26, dy2(8) + 26], 5,
                            fill=IDCC, outline=(110, 122, 134), width=2)
        for n in range(1, 17):
            cx, cy = dx2(didc(n)[0]), dy2(didc(n)[1])
            d.rectangle([cx - 5, cy - 5, cx + 5, cy + 5], fill=(214, 220, 226))
            testo(d, (cx, cy + (34 if n % 2 == 0 else -34)), str(n), TM[11], TENUE, 'mm')
        testo(d, (dx2(4) - 34, dy2(7)), '1', TM[13], GIALLO, 'rm')
    if t > 3.6:
        testo(d, (DX0, 648), 'Il T_IRQ resta scollegato. I pin 13, 14 e 15 del nastro sono di riserva.',
              T[19], TENUE, 'lm')
    didascalia(d, 'Leggi le sigle stampate sul display, non la posizione: l ordine cambia da un modulo all altro.', GIALLO)
    return im

def scena_dsp(gi):
    titolo, lista = FILI_DSP[gi]
    def sc(t, dur):
        im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
        intestazione(d, titolo, 'ponticelli di filo rigido, sul retro della schedina')
        scheda(d, dx2(1), dy2(1), 15, 9, P2)
        d.rounded_rectangle([dx2(1) - 20, dy2(1) - 20, dx2(14) + 20, dy2(1) + 20], 6,
                            fill=STRISCIA, outline=(70, 80, 90))
        for i in range(14):
            cx = dx2(1 + i)
            d.ellipse([cx - 9, dy2(1) - 9, cx + 9, dy2(1) + 9], fill=(10, 12, 14), outline=(96, 108, 118))
        d.rounded_rectangle([dx2(4) - 26, dy2(7) - 26, dx2(11) + 26, dy2(8) + 26], 5,
                            fill=IDCC, outline=(110, 122, 134), width=2)
        for n in range(1, 17):
            cx, cy = dx2(didc(n)[0]), dy2(didc(n)[1])
            d.rectangle([cx - 5, cy - 5, cx + 5, cy + 5], fill=(214, 220, 226))
            testo(d, (cx, cy + (34 if n % 2 == 0 else -34)), str(n), TM[11], TENUE, 'mm')
        for i, nome in enumerate(DSP):
            testo_su(im, (dx2(1 + i), dy2(1) - 26), nome, TM[12],
                     (62, 70, 78) if nome == 'T_IRQ' else INK)
        d = ImageDraw.Draw(im)
        for k in range(gi):
            for j, (a, b, _, col) in enumerate(FILI_DSP[k][1]):
                filo(d, a, b, smorza(col, 0.3), dx2, dy2, 2, curva=(j % 4 - 1.5) * 16)
        n = int(t / 0.85)
        for j, (a, b, et, col) in enumerate(lista):
            if j > n: continue
            pr = min(1.0, (t - j * 0.85) / 0.45) if j == n else 1.0
            filo(d, a, b, col if j == n else smorza(col, 0.55), dx2, dy2,
                 4 if j == n else 2, curva=(j % 4 - 1.5) * 16, prog=pr)
        nn = min(n, len(lista) - 1)
        testo(d, (W // 2, 648), lista[nn][2], T[22], lista[nn][3], 'mm')
        didascalia(d, 'I tre ponticelli del touch risparmiano tre fili sul nastro: SCK, MOSI e MISO sono gia quelli.', VERDE)
        return im
    return (sc, 0.85 * len(lista) + 1.6)

def sc_nastro(t, dur):
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    intestazione(d, '6. La mappa del nastro', 'stessa numerazione sulle due schedine: il filo 1 e quello con la riga rossa')
    for i, (n, pin, sig, col) in enumerate(NASTRO):
        if t < 0.25 + i * 0.16: continue
        cx = 120 + (i % 8) * 142
        cy = 190 + (i // 8) * 190
        d.rounded_rectangle([cx, cy, cx + 126, cy + 150], 9, fill=(26, 32, 38), outline=col, width=2)
        testo(d, (cx + 63, cy + 32), n, TB[30], col, 'mm')
        testo(d, (cx + 63, cy + 76), pin, TM[16], INK, 'mm')
        testo(d, (cx + 63, cy + 116), sig, T[13], TENUE, 'mm')
    if t > 3.4:
        testo(d, (W // 2, 612), '9 segnali, 4 masse, 3 di riserva', T[22], INK, 'mm')
    didascalia(d, 'Le masse sui fili 7 e 9 stanno ai lati dello SCK: e il filo piu veloce, ed e il meglio protetto.', VERDE)
    return im

PROVE = ['tester in continuita fra pista 3,3 V e pista di massa: NON deve fischiare',
         'tester fra ogni foro dello zoccolo e la sua destinazione: deve fischiare',
         'infila l ESP32 e attacca la USB: solo lui, niente altro',
         'misura 3,2 - 3,3 V sulla pista di alimentazione',
         'spegni, attacca il GPS: "modulo riconosciuto a 9600 baud"',
         'spegni, attacca il nastro e il display: logo e menu',
         'tieni premuto il tasto 3 secondi: taratura del tocco']

def sc_collaudo(t, dur):
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    intestazione(d, '7. Il collaudo', 'un pezzo per volta, sul banco, prima di chiudere la scatola')
    n = int(t / 1.25)
    for i, p in enumerate(PROVE):
        if i > n: continue
        y = 160 + i * 62
        d.rounded_rectangle([72, y - 22, 1208, y + 24], 9, fill=(32, 42, 38) if i < n else (26, 32, 38))
        col = VERDE if i < n else INK
        d.ellipse([92, y - 12, 114, y + 10], outline=col, width=3, fill=col if i < n else None)
        if i < n:
            d.line([97, y, 102, y + 6], fill=(18, 24, 20), width=3)
            d.line([102, y + 6, 110, y - 5], fill=(18, 24, 20), width=3)
        testo(d, (134, y), p, T[19], INK, 'lm')
    didascalia(d, 'Le prime due prove si fanno a scheda SPENTA e senza ESP32: un corto si trova col tester, non col fumo.', GIALLO)
    return im

def sc_fine(t, dur):
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    a = min(1.0, t / 0.7)
    testo(d, (W // 2, 250), f'{N_FILI} ponticelli, 2 piste, 1 cavo', TB[40], tuple(int(c * a) for c in INK), 'mm')
    testo(d, (W // 2, 310), 'un sabato pomeriggio, fatto con calma', T[26], tuple(int(c * a) for c in TENUE), 'mm')
    if t > 1.0:
        b = min(1.0, (t - 1.0) / 0.7)
        c = tuple(int(x * b) for x in RAME)
        testo(d, (W // 2, 394), 'fascetta sul fascio a 3 cm dal connettore', T[22], c, 'mm')
        testo(d, (W // 2, 430), 'distanziali di nylon, mai di ottone', T[22], c, 'mm')
        testo(d, (W // 2, 466), 'e l adattatore a morsetti non si butta', T[22], c, 'mm')
    return im

SCENE = [(sc_titolo, 3.2), (sc_pezzi, 5.6), (sc_griglia, 6.2), (sc_ordine, 6.4),
         (sc_piste, 5.2), (sc_zoccolo, 6.2)]
SCENE += [scena_fili(g) for g in range(len(FILI))]
SCENE += [(sc_dsp, 5.4)]
SCENE += [scena_dsp(g) for g in range(len(FILI_DSP))]
SCENE += [(sc_nastro, 6.4), (sc_collaudo, 10.0), (sc_fine, 4.6)]

durata = sum(d for _, d in SCENE)
print(f'scene: {len(SCENE)}, fili {N_FILI}, durata {durata:.1f} s', file=sys.stderr)

uscita = sys.argv[1] if len(sys.argv) > 1 else 'montaggio-scheda.mp4'
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
        if n % 400 == 0: print(f'  {n} fotogrammi', file=sys.stderr)
ff.stdin.close()
ff.wait()
print(f'fatto: {n} fotogrammi', file=sys.stderr)
