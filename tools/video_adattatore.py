#!/usr/bin/env python3
# Video del montaggio con l'adattatore a morsetti per ESP32 30 pin.
# Disegna ogni fotogramma con Pillow e lo manda a ffmpeg senza passare dal disco.
import subprocess, math, sys
from PIL import Image, ImageDraw, ImageFont

W, H, FPS = 1280, 720, 25
F  = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
FB = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
FM = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf'
car = lambda p, d: ImageFont.truetype(p, d)
T  = {n: car(F, n) for n in (13, 15, 17, 19, 22, 26)}
TB = {n: car(FB, n) for n in (13, 15, 17, 19, 22, 26, 30, 44, 58)}
TM = {n: car(FM, n) for n in (12, 13, 14, 16, 18)}

BG, GRID, INK, TENUE = (18, 22, 26), (46, 56, 64), (235, 240, 243), (140, 155, 165)
VERDE, RAME, ROSSO, BLU, GIALLO = (53, 196, 106), (224, 164, 106), (232, 83, 79), (74, 163, 255), (240, 169, 59)
GRIGIO = (168, 180, 190)
PCB, PCB_B, MORS, VITE = (24, 86, 62), (36, 116, 86), (150, 190, 120), (196, 202, 208)

# --- geometria della scheda --------------------------------------------------
BX0, BX1 = 430, 700                 # bordi della basetta verde
BY0, BY1 = 112, 566
MOR_Y0, MOR_PASSO = 146, 28         # primo morsetto e passo
VITE_SX, VITE_DX = 454, 676         # centro delle viti
CH_SX, CH_DX = 398, 706             # corsie verticali dei fili
CH_MOD, GIRO_Y, CH_GIRO = 766, 578, 794   # corsie dei moduli e giro basso
corsia = lambda lato, i: (CH_SX - (14 - i) * 3) if lato == 'sx' else (CH_DX + (14 - i) * 4)
RAIL_Y = {'3V3': 614, 'GND': 640}
RAIL_X0, RAIL_X1 = 300, 1086

# serigrafia dell'adattatore, dall'alto in basso
PIN_SX = ['EN', 'VP', 'VN', 'D34', 'D35', 'D32', 'D33', 'D25',
          'D26', 'D27', 'D14', 'D12', 'D13', 'GND', 'VIN']
PIN_DX = ['D23', 'D22', 'TX0', 'RX0', 'D21', 'D19', 'D18', 'D5',
          'TX2', 'RX2', 'D4', 'D2', 'D15', 'GND', '3V3']

MOD = {
    'display': (800, 108, 1120, 404, RAME,   'DISPLAY  ST7796'),
    'gps':     (800, 432, 1120, 552, VERDE,  'GPS  NEO-7M'),
    'conv':    (150, 150,  340, 250, GIALLO, 'CONVERTITORE  5 V'),
    'tasto':   (150, 300,  340, 380, GRIGIO, 'PULSANTE'),
}
# quante prese servono su ogni modulo, nell'ordine in cui le useremo
MOD_PIN = {
    'display': ['VCC', 'GND', 'CS', 'RESET', 'DC / RS', 'SDI / MOSI', 'SCK',
                'LED', 'SDO / MISO', 'T_CLK', 'T_DIN', 'T_DO', 'T_CS'],
    'gps':     ['VCC', 'GND', 'TXD', 'RXD', 'PPS'],
    'conv':    ['+ 5 V', 'massa'],
    'tasto':   ['un capo', 'altro capo'],
}

def mor(lato, i):
    y = MOR_Y0 + i * MOR_PASSO
    return ((VITE_SX, y) if lato == 'sx' else (VITE_DX, y))

def mor_i(lato, nome):
    return (PIN_SX if lato == 'sx' else PIN_DX).index(nome)

def presa(nome_mod, pin):
    x0, y0, x1, y1 = MOD[nome_mod][:4]
    n = len(MOD_PIN[nome_mod]); i = MOD_PIN[nome_mod].index(pin)
    y = y0 + (i + 1) * (y1 - y0) / (n + 1)
    return ((x0, y) if x0 > BX1 else (x1, y))

# --- i fili ------------------------------------------------------------------
# (lato, morsetto, modulo, pin_modulo, pista, etichetta, colore)
# Uno fra morsetto e pista e' sempre None: un filo va da un morsetto a un modulo,
# da un morsetto a una pista, oppure da un modulo a una pista.
FILI = [
    ('ALIMENTAZIONE', [
        ('sx', 'VIN', 'conv', '+ 5 V', None,  'convertitore + 5 V  ->  VIN',  GIALLO),
        ('sx', 'GND', 'conv', 'massa', None,  'convertitore massa  ->  GND',  INK),
        ('dx', '3V3', None,   None,    '3V3', '3V3  ->  pista 3,3 V',         ROSSO),
        ('dx', 'GND', None,   None,    'GND', 'GND  ->  pista massa',         INK),
    ]),
    ('GPS', [
        ('dx', 'RX2', 'gps', 'TXD', None,  'TXD del GPS  ->  RX2',  VERDE),
        ('dx', 'TX2', 'gps', 'RXD', None,  'RXD del GPS  ->  TX2',  VERDE),
        ('dx', 'D4',  'gps', 'PPS', None,  'PPS  ->  D4',           VERDE),
        (None, None,  'gps', 'VCC', '3V3', 'VCC  ->  pista 3,3 V',  ROSSO),
        (None, None,  'gps', 'GND', 'GND', 'GND  ->  pista massa',  INK),
    ]),
    ('DISPLAY', [
        ('dx', 'D5',  'display', 'CS',         None,  'CS  ->  D5',           GIALLO),
        ('dx', 'D22', 'display', 'RESET',      None,  'RESET  ->  D22',       GIALLO),
        ('dx', 'D21', 'display', 'DC / RS',    None,  'DC / RS  ->  D21',     BLU),
        ('dx', 'D23', 'display', 'SDI / MOSI', None,  'SDI / MOSI  ->  D23',  BLU),
        ('dx', 'D18', 'display', 'SCK',        None,  'SCK  ->  D18',         BLU),
        ('dx', 'D19', 'display', 'SDO / MISO', None,  'SDO / MISO  ->  D19',  BLU),
        (None, None,  'display', 'VCC',        '3V3', 'VCC  ->  pista 3,3 V', ROSSO),
        (None, None,  'display', 'LED',        '3V3', 'LED  ->  pista 3,3 V', ROSSO),
        (None, None,  'display', 'GND',        'GND', 'GND  ->  pista massa', INK),
    ]),
    ('TOUCH', [
        ('dx', 'D18', 'display', 'T_CLK', None, 'T_CLK  ->  D18  (seconda vite)', RAME),
        ('dx', 'D23', 'display', 'T_DIN', None, 'T_DIN  ->  D23  (seconda vite)', RAME),
        ('dx', 'D19', 'display', 'T_DO',  None, 'T_DO  ->  D19  (seconda vite)',  RAME),
        ('sx', 'D27', 'display', 'T_CS',  None, 'T_CS  ->  D27',                  RAME),
    ]),
    ('PULSANTE', [
        ('sx', 'D32', 'tasto', 'un capo',    None,  'un capo  ->  D32',      GRIGIO),
        (None, None,  'tasto', 'altro capo', 'GND', 'altro capo  ->  massa', INK),
    ]),
]
N_FILI = sum(len(g[1]) for g in FILI)

def percorso(filo):
    """Punti da morsetto (o da modulo) alla destinazione, a segmenti dritti."""
    lato, pin, nome_mod, pin_mod, pista = filo[:5]
    if lato is None:                                   # da un modulo alla pista
        b = presa(nome_mod, pin_mod)
        i = MOD_PIN[nome_mod].index(pin_mod)
        ch = (CH_MOD + i * 3) if MOD[nome_mod][0] > BX1 else (CH_SX - 42 + i * 3)
        return [b, (ch, b[1]), (ch, RAIL_Y[pista])]
    a = mor(lato, mor_i(lato, pin))
    bordo = (BX1, a[1]) if lato == 'dx' else (BX0, a[1])
    ch = corsia(lato, mor_i(lato, pin))
    if nome_mod is None:                               # da un morsetto alla pista
        return [a, bordo, (ch, a[1]), (ch, RAIL_Y[pista])]
    b = presa(nome_mod, pin_mod)
    if (lato == 'dx') == (MOD[nome_mod][0] > BX1):     # modulo dalla stessa parte
        return [a, bordo, (ch, a[1]), (ch, b[1]), b]
    return [a, bordo, (ch, a[1]), (ch, GIRO_Y), (CH_GIRO, GIRO_Y), (CH_GIRO, b[1]), b]

# --- disegno -----------------------------------------------------------------
def testo(d, xy, s, f, col=INK, anc='la'):
    d.text(xy, s, font=f, fill=col, anchor=anc)

def intestazione(d, titolo, sotto=None):
    testo(d, (72, 30), titolo, TB[30], INK)
    if sotto: testo(d, (72, 66), sotto, T[17], TENUE)

def didascalia(d, s, col=TENUE):
    d.rectangle([0, 664, W, H], fill=(12, 15, 18))
    testo(d, (72, 692), s, T[19], col, 'lm')

def disegna_scheda(d, etichette=1.0):
    d.rounded_rectangle([BX0, BY0, BX1, BY1], 10, fill=PCB, outline=PCB_B, width=2)
    for fx, fy in ((BX0 + 16, BY0 + 14), (BX1 - 16, BY0 + 14),
                   (BX0 + 16, BY1 - 14), (BX1 - 16, BY1 - 14)):
        d.ellipse([fx - 7, fy - 7, fx + 7, fy + 7], fill=(206, 212, 216))
    for x in (VITE_SX, VITE_DX):
        d.rounded_rectangle([x - 16, MOR_Y0 - 16, x + 16, MOR_Y0 + 14 * MOR_PASSO + 16],
                            6, fill=MORS)
    for i in range(15):
        for lato, x in (('sx', VITE_SX), ('dx', VITE_DX)):
            y = MOR_Y0 + i * MOR_PASSO
            d.ellipse([x - 10, y - 10, x + 10, y + 10], fill=VITE, outline=(120, 128, 134))
            d.line([x - 6, y - 2, x + 6, y + 3], fill=(120, 128, 134), width=3)
            if etichette > 0:
                col = tuple(int(c * etichette) for c in (214, 226, 232))
                if lato == 'sx': testo(d, (x + 22, y), PIN_SX[i], TM[13], col, 'lm')
                else:            testo(d, (x - 22, y), PIN_DX[i], TM[13], col, 'rm')

def disegna_strip(d, acceso=1.0):
    c = tuple(int(v * acceso) for v in (34, 40, 46))
    for x in (528, 604):
        d.rounded_rectangle([x - 9, MOR_Y0 - 6, x + 9, MOR_Y0 + 14 * MOR_PASSO + 6], 5, fill=c)
        for i in range(15):
            y = MOR_Y0 + i * MOR_PASSO
            d.ellipse([x - 4, y - 4, x + 4, y + 4], fill=(14, 16, 20))

def disegna_esp(d, giu=1.0):
    dy = int((1 - giu) * -120)
    x0, y0, x1, y1 = 518, 158 + dy, 614, 520 + dy
    d.rounded_rectangle([x0, y0, x1, y1], 8, fill=(47, 59, 77))
    d.rounded_rectangle([x0 + 22, y0 - 16, x1 - 22, y0 + 6], 4, fill=(120, 128, 136))
    testo(d, ((x0 + x1) // 2, (y0 + y1) // 2 - 10), 'ESP32', TB[17], (225, 235, 245), 'mm')
    testo(d, ((x0 + x1) // 2, (y0 + y1) // 2 + 12), 'DevKit', T[13], (150, 170, 190), 'mm')

def disegna_moduli(d, quali=None, prese_fatte=()):
    for nome, (x0, y0, x1, y1, col, tit) in MOD.items():
        if quali is not None and nome not in quali: continue
        d.rounded_rectangle([x0, y0, x1, y1], 10, fill=(26, 32, 38), outline=col, width=2)
        a_destra = x0 > BX1
        testo(d, (x0 + 14 if a_destra else x1 - 14, y0 - 14), tit, TB[15], col,
              'lm' if a_destra else 'rm')
        for pin in MOD_PIN[nome]:
            px, py = presa(nome, pin)
            fatto = (nome, pin) in prese_fatte
            d.ellipse([px - 4, py - 4, px + 4, py + 4], fill=col if fatto else (60, 72, 82))
            testo(d, (px + 14 if a_destra else px - 14, py), pin, TM[13],
                  INK if fatto else (96, 110, 122), 'lm' if a_destra else 'rm')

def disegna_piste(d, frazione=1.0, con_cond=True):
    for quale, col, nome in (('3V3', ROSSO, 'pista 3,3 V'), ('GND', GRIGIO, 'pista massa')):
        y = RAIL_Y[quale]
        x1 = RAIL_X0 + (RAIL_X1 - RAIL_X0) * min(1.0, frazione)
        d.rounded_rectangle([RAIL_X0, y - 5, x1, y + 5], 5, fill=col)
        if frazione > 0.9:
            testo(d, (RAIL_X0 - 12, y), nome, T[15], TENUE, 'rm')
    if con_cond and frazione >= 1.0:
        for x, et in ((880, '3 x 100 uF'), (1000, '100 nF')):
            d.rounded_rectangle([x - 13, RAIL_Y['3V3'] - 2, x + 13, RAIL_Y['GND'] + 2],
                                5, fill=(120, 110, 70))
            testo(d, (x, RAIL_Y['3V3'] - 20), et, T[13], TENUE, 'mm')

def disegna_filo(d, filo, avanzamento=1.0, acceso=False, spento=False):
    via = percorso(filo)
    tot = sum(math.dist(via[i], via[i + 1]) for i in range(len(via) - 1)) or 1
    resta, pts = tot * avanzamento, [via[0]]
    for i in range(len(via) - 1):
        seg = math.dist(via[i], via[i + 1])
        if resta >= seg:
            pts.append(via[i + 1]); resta -= seg
        else:
            t = resta / seg if seg else 0
            pts.append((via[i][0] + (via[i+1][0] - via[i][0]) * t,
                        via[i][1] + (via[i+1][1] - via[i][1]) * t))
            break
    col = tuple(int(c * 0.34) for c in filo[6]) if spento else filo[6]
    if len(pts) > 1:
        d.line(pts, fill=col, width=6 if acceso else 4, joint='curve')
    if acceso:
        for p in (via[0], pts[-1]):
            d.ellipse([p[0]-6, p[1]-6, p[0]+6, p[1]+6], fill=col)

def barra(d, fatti, totale, gruppo):
    x0, x1, y = 72, 744, 96
    d.rounded_rectangle([x0, y - 4, x1, y + 4], 4, fill=(40, 50, 58))
    if fatti:
        d.rounded_rectangle([x0, y - 4, x0 + (x1 - x0) * fatti / totale, y + 4], 4, fill=VERDE)
    testo(d, (x1, y - 22), f'filo {min(fatti + 1, totale)} di {totale}  -  {gruppo}',
          TM[13], TENUE, 'rm')

# --- scene -------------------------------------------------------------------
def sc_titolo(t, dur):
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    a = min(1.0, t / 0.8)
    testo(d, (W//2, 296), "Montaggio con l'adattatore", TB[58], tuple(int(c*a) for c in INK), 'mm')
    testo(d, (W//2, 358), f'ESP32 GPS Tracker  -  {N_FILI} fili a vite, zero saldature',
          T[26], tuple(int(c*a) for c in TENUE), 'mm')
    if t > 1.4:
        b = min(1.0, (t - 1.4) / 0.6)
        d.line([W//2 - 170, 404, W//2 + 170, 404], fill=tuple(int(c*b) for c in VERDE), width=3)
    return im

def sc_scheda(t, dur):
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    intestazione(d, "1. L'adattatore", 'terminal adapter per ESP32 30 pin')
    disegna_scheda(d, min(1.0, max(0.0, (t - 0.8) / 1.0)))
    if t > 2.2:
        testo(d, (150, 300), 'Ogni pin dell\'ESP32', T[19], INK)
        testo(d, (150, 330), 'esce su una vite.', T[19], INK)
        testo(d, (150, 378), 'Niente saldature:', T[19], TENUE)
        testo(d, (150, 408), 'si smonta tutto', T[19], TENUE)
        testo(d, (150, 438), 'in due minuti.', T[19], TENUE)
    didascalia(d, 'Avvitalo al fondo della scatola con i quattro fori agli angoli: cosi non balla.')
    return im

def sc_esp(t, dur):
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    intestazione(d, "2. L'ESP32 si infila", 'la presa USB verso il basso, dal lato di VIN e 3V3')
    disegna_scheda(d)
    disegna_strip(d, min(1.0, t / 0.8))
    if t > 1.2: disegna_esp(d, min(1.0, (t - 1.2) / 1.4))
    if t > 3.0:
        testo(d, (150, 330), 'Controlla pin per pin', T[19], INK)
        testo(d, (150, 360), 'prima di spingere:', T[19], INK)
        testo(d, (150, 408), 'VIN con VIN,', T[19], GIALLO)
        testo(d, (150, 438), '3V3 con 3V3.', T[19], ROSSO)
    didascalia(d, 'Se entra a fatica non forzare: controlla di non averlo girato di 180 gradi.')
    return im

def sc_piste(t, dur):
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    intestazione(d, '3. Le due piste', 'un pezzetto di millefori da 3x4 cm, fuori dall\'adattatore')
    disegna_scheda(d); disegna_strip(d); disegna_esp(d)
    disegna_piste(d, min(1.0, t / 1.6))
    if t > 2.4:
        testo(d, (72, 300), 'Il morsetto 3V3 e uno solo,', T[19], INK)
        testo(d, (72, 330), 'ma a 3,3 V devono', T[19], INK)
        testo(d, (72, 360), 'arrivare tre fili.', T[19], INK)
    didascalia(d, 'Due fili soli dall\'adattatore alle piste, e tutto il resto si attacca li.')
    return im

def scena_fili(gruppo):
    titolo, fili = FILI[gruppo]
    per_filo = 1.3
    fatti_prima = sum(len(FILI[g][1]) for g in range(gruppo))
    def f(t, dur):
        im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
        n = int(t / per_filo)
        prese = set()
        for g in range(gruppo):
            prese |= {(x[2], x[3]) for x in FILI[g][1] if x[2]}
        prese |= {(x[2], x[3]) for x in fili[:n] if x[2]}
        intestazione(d, f'{titolo}  -  {len(fili)} fili', 'uno per volta, stringendo la vite')
        barra(d, fatti_prima + min(n, len(fili)), N_FILI, titolo)
        disegna_moduli(d, prese_fatte=prese)
        disegna_piste(d)
        disegna_scheda(d); disegna_strip(d); disegna_esp(d)
        for g in range(gruppo):
            for filo in FILI[g][1]:
                disegna_filo(d, filo, spento=True)
        for i, filo in enumerate(fili):
            if i < n:    disegna_filo(d, filo)
            elif i == n: disegna_filo(d, filo, (t - i * per_filo) / per_filo, acceso=True)
        if n < len(fili):
            didascalia(d, fili[n][5], INK)
        else:
            didascalia(d, 'Tira piano ogni filo appena stretto: se esce, la vite non era chiusa.', VERDE)
        return im
    return f, per_filo * len(fili) + 1.0

def sc_ingresso(t, dur):
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    intestazione(d, "4. L'ingresso dalla moto", 'fusibile, diodo, convertitore: in quest\'ordine')
    tappe = [
        ('batteria', '+12 V', (210, 100, 100)),
        ('fusibile 2 A', 'a 15 cm dal +', GIALLO),
        ('diodo 1N4007', 'fascetta a destra', RAME),
        ('convertitore', '12 V  ->  5 V', VERDE),
        ('VIN', "dell'adattatore", BLU),
    ]
    for i, (tit, sot, col) in enumerate(tappe):
        if t < 0.5 + i * 0.9: continue
        x = 96 + i * 222
        d.rounded_rectangle([x, 250, x + 182, 360], 10, fill=(26, 32, 38), outline=col, width=2)
        testo(d, (x + 91, 290), tit, TB[17], col, 'mm')
        testo(d, (x + 91, 322), sot, T[13], TENUE, 'mm')
        if i and t > 0.5 + i * 0.9 - 0.3:
            d.line([x - 38, 305, x - 6, 305], fill=TENUE, width=3)
            d.polygon([(x - 6, 305), (x - 18, 299), (x - 18, 311)], fill=TENUE)
    if t > 4.8:
        testo(d, (96, 430), 'La fascetta del diodo guarda il convertitore.', T[22], INK)
        testo(d, (96, 466), 'Al contrario non si rompe niente: semplicemente non parte.', T[19], TENUE)
    didascalia(d, 'Il fusibile protegge il filo, non l\'elettronica: per questo va vicino alla batteria.', GIALLO)
    return im

PROVE = [
    'viti: tirare ogni filo, uno per uno',
    'tester in continuita fra 3,3 V e massa: NON deve fischiare',
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
        if i > n: continue
        y = 150 + i * 62
        acceso = i == n
        d.rounded_rectangle([72, y - 22, 1208, y + 24], 9,
                            fill=(32, 42, 38) if i < n else (26, 32, 38))
        col = VERDE if i < n else INK
        d.ellipse([92, y - 12, 114, y + 10], outline=col, width=3, fill=col if i < n else None)
        if i < n:
            d.line([97, y, 102, y + 6], fill=(18, 24, 20), width=3)
            d.line([102, y + 6, 110, y - 5], fill=(18, 24, 20), width=3)
        testo(d, (134, y), p, T[19], INK if not acceso else INK, 'lm')
    didascalia(d, 'Se sbagli un filo, vuoi scoprirlo con il solo ESP32 attaccato: non con tutto collegato.', GIALLO)
    return im

def sc_fine(t, dur):
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    a = min(1.0, t / 0.7)
    testo(d, (W//2, 268), f'{N_FILI} fili, zero saldature', TB[44], tuple(int(c*a) for c in INK), 'mm')
    testo(d, (W//2, 330), 'un pomeriggio, fatto con calma', T[26], tuple(int(c*a) for c in TENUE), 'mm')
    if t > 1.0:
        b = min(1.0, (t - 1.0) / 0.7)
        c = tuple(int(x*b) for x in RAME)
        testo(d, (W//2, 412), 'niente stagno sulla punta dei fili a vite', T[22], c, 'mm')
        testo(d, (W//2, 448), 'una goccia di smalto sulle viti strette', T[22], c, 'mm')
        testo(d, (W//2, 484), 'e ricontrollale dopo il primo giro', T[22], c, 'mm')
    return im

SCENE = [(sc_titolo, 3.0), (sc_scheda, 5.0), (sc_esp, 5.6), (sc_piste, 4.6)]
for g in range(len(FILI)):
    SCENE.append(scena_fili(g))
SCENE += [(sc_ingresso, 6.4), (sc_collaudo, 10.0), (sc_fine, 4.4)]

durata = sum(d for _, d in SCENE)
print(f'scene: {len(SCENE)}, fili {N_FILI}, durata {durata:.1f} s', file=sys.stderr)

uscita = sys.argv[1] if len(sys.argv) > 1 else 'montaggio-adattatore.mp4'
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
        if n % 300 == 0: print(f'  {n} fotogrammi', file=sys.stderr)
ff.stdin.close()
ff.wait()
print(f'fatto: {n} fotogrammi', file=sys.stderr)
