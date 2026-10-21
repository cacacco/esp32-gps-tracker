#!/usr/bin/env python3
# Anteprima delle schermate del tracker: stesse coordinate del firmware,
# prese a mano da display_ui.h. 480x320, ingrandite per guardarle sul PC.
import math, sys
from PIL import Image, ImageDraw, ImageFont

W, H, K = 480, 320, 2            # K = ingrandimento
F  = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
FB = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'

BG, FG, LABEL = (0,0,0), (255,255,255), (128,128,128)
LINE, BTN = (32,32,32), (24,28,24)
OK, BAD, WARN, CYAN = (0,255,0), (255,0,0), (255,165,0), (0,255,255)

# font 2 = 16 px, font 4 = 26 px; setTextSize(2) raddoppia
def car(px, grassetto=False):
    return ImageFont.truetype(FB if grassetto else F, int(px * K * 0.72))
FONT2, FONT4, FONT4X2 = car(16), car(26), car(52)

def tela():
    im = Image.new('RGB', (W*K, H*K), BG)
    return im, ImageDraw.Draw(im)

def testo(d, x, y, s, f, col, anc='mm'):
    d.text((x*K, y*K), s, font=f, fill=col, anchor=anc)

def hline(d, x, y, w, col): d.rectangle([x*K, y*K, (x+w)*K, y*K+K-1], fill=col)
def vline(d, x, y, h, col): d.rectangle([x*K, y*K, x*K+K-1, (y+h)*K], fill=col)

def freccia(d, cx, cy, r, ang, col):
    a = math.radians(ang)
    p = lambda rr, aa: ((cx + math.sin(aa)*rr)*K, (cy - math.cos(aa)*rr)*K)
    x0 = p(r, a); xL = p(r*0.78, a+2.44); xR = p(r*0.78, a-2.44); xT = p(-r*0.28, a)
    d.polygon([x0, xL, xT], fill=col)
    d.polygon([x0, xR, xT], fill=col)

def rotonda(d, cx, cy, r, uscita, ang, col):
    ra = int(r * 0.58)
    for dr in (0, 1):
        d.ellipse([(cx-ra+dr)*K, (cy-ra+dr)*K, (cx+ra-dr)*K, (cy+ra-dr)*K], outline=col, width=K)
    d.rectangle([cx*K, (cy+ra)*K, (cx+2)*K, (cy+r)*K], fill=col)       # da dove entri
    a = math.radians(ang)
    x0 = ((cx + math.sin(a)*ra)*K, (cy - math.cos(a)*ra)*K)
    x1 = ((cx + math.sin(a)*(r+1))*K, (cy - math.cos(a)*(r+1))*K)
    d.line([x0, x1], fill=col, width=2*K)
    pp = 9 if r > 20 else 6
    d.polygon([x1,
               (x1[0] + math.sin(a+2.44)*pp*K, x1[1] - math.cos(a+2.44)*pp*K),
               (x1[0] + math.sin(a-2.44)*pp*K, x1[1] - math.cos(a-2.44)*pp*K)], fill=col)
    if uscita and ra >= 14:
        testo(d, cx, cy, str(uscita), FONT4, col)

def cunei(d, cx, cy, col):            # il segno del cambio di via
    for i in range(2):
        x = cx + i*9
        d.polygon([(x*K, (cy-8)*K), (x*K, (cy+8)*K), ((x+7)*K, cy*K)], fill=col)

def roundrect(d, x, y, w, h, r, fill=None, outline=None, lw=1):
    d.rounded_rectangle([x*K, y*K, (x+w)*K, (y+h)*K], r*K, fill=fill, outline=outline, width=lw*K)

def barra(d, sat='8 sat   HDOP 1.1', ora='14:32', fuori=False):
    d.ellipse([(18-7)*K, (19-7)*K, (18+7)*K, (19+7)*K], fill=BAD if fuori else OK)
    testo(d, 34, 19, sat, FONT2, LABEL, 'lm')
    testo(d, 472, 19, ora, FONT4, FG, 'rm')
    hline(d, 0, 38, 480, LINE)

def nav(svolta, dist, strada, nuova, rotond, uscita, poi, poiM, basso, bassoCol,
        frecciaCol=CYAN, frecciaAng=18, vicina=False, restano='34,2 km', vel='72',
        fuori=False, poiRot=False, poiAng=0):
    im, d = tela()
    barra(d, fuori=fuori)
    vline(d, 292, 46, 270, LINE)
    hline(d, 296, 196, 184, LINE)
    hline(d, 296, 262, 184, LINE)

    freccia(d, 145, 152, 76, frecciaAng, frecciaCol)
    testo(d, 144, 238, 'RESTANO', FONT2, LABEL)
    testo(d, 144, 268, restano, FONT4, FG)
    testo(d, 144, 302, basso, FONT2, bassoCol)

    testo(d, 386, 54, 'SVOLTA', FONT2, LABEL)
    if vicina: roundrect(d, 296, 44, 180, 152, 8, outline=WARN)
    col = WARN if vicina else FG
    if rotond: rotonda(d, 386, 104, 30, uscita, svolta, col)
    else:      freccia(d, 386, 104, 30, svolta, col)
    if nuova:  cunei(d, 302, 104, CYAN)
    testo(d, 386, 164, dist, FONT4X2, col)

    testo(d, 386, 210, 'DOPO', FONT2, LABEL)
    if poiRot: rotonda(d, 342, 242, 15, 0, poiAng, LABEL)
    else:      freccia(d, 342, 242, 15, poi, LABEL)
    testo(d, 414, 242, poiM, FONT2, LABEL)
    testo(d, 386, 290, vel, FONT4X2, FG)
    return im

def percorso():
    im, d = tela()
    d.rectangle([0, 0, W*K, H*K], fill=BG)
    testo(d, 14, 19, 'PERCORSO', FONT4, FG, 'lm')
    hline(d, 0, 38, 480, LINE)
    for y, (et, va) in zip((66, 102, 138, 174),
                           (('lunghezza', '184,6 km'), ('punti', '1998'),
                            ('svolte', '63'), ('tappe', '4'))):
        testo(d, 20, y, et, FONT2, LABEL, 'lm')
        testo(d, 210, y, va, FONT4, FG, 'lm')
    # il tasto nuovo, con la cornice chiara di quello scelto col pulsante
    roundrect(d, 14, 196, 452, 46, 10, fill=BTN, outline=WARN)
    roundrect(d, 12, 194, 456, 50, 12, outline=FG)
    testo(d, 240, 219, 'Ricalcola il rientro', FONT4, FG)
    for r, lab, col in (((14, 258, 140, 48), 'Indietro', LINE),
                        ((170, 258, 140, 48), 'Nuovo', CYAN),
                        ((326, 258, 140, 48), 'Cancella', BAD)):
        roundrect(d, r[0], r[1], r[2], r[3], 10, fill=BTN, outline=col)
        testo(d, r[0]+r[2]//2, r[1]+r[3]//2, lab, FONT4, FG)
    return im

S = sys.argv[1]
nav(90, '180 m', 'a destra > Via Roma', True, False, 0, 0, '1,2 km',
    'a destra > Via Roma', FG, poiRot=True, poiAng=90).save(S + '/s1-cambio-via.png')

nav(90, '90 m', '', True, True, 2, -90, '600 m',
    '2a uscita > Via Po', FG, vicina=True, frecciaAng=-8).save(S + '/s2-rotatoria.png')

nav(90, '1,4 km', '', False, False, 0, 0, '2,1 km', 'RIENTRO  340 m', BAD,
    frecciaCol=WARN, frecciaAng=-62, fuori=True, vel='84').save(S + '/s3-rientro.png')

nav(90, '1,4 km', '', False, False, 0, 0, '2,1 km', 'RICALCOLO IN CORSO', CYAN,
    frecciaCol=WARN, frecciaAng=-62, fuori=True, vel='84').save(S + '/s4-ricalcolo.png')

nav(0, '2,4 km', '', False, False, 0, 40, '3,0 km',
    'dritto Via Emilia', LABEL, frecciaAng=4).save(S + '/s5-stessa-via.png')

percorso().save(S + '/s6-pagina-percorso.png')
print('sei schermate disegnate')
