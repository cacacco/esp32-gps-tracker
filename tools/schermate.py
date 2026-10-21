#!/usr/bin/env python3
# Anteprima delle schermate leggendo le misure vere dal firmware (dumplayout),
# cosi' l'immagine non puo' raccontare un'impaginazione diversa da quella vera.
import json, math, sys
from PIL import Image, ImageDraw, ImageFont

K = 2
F  = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
BG, FG, LABEL = (0,0,0), (255,255,255), (128,128,128)
LINE, BTN = (32,32,32), (24,28,24)
OK, BAD, WARN, CYAN = (0,255,0), (255,0,0), (255,165,0), (0,255,255)
def car(px): return ImageFont.truetype(F, int(px * K * 0.72))
F2, F4, F4X2, F7X2 = car(16), car(26), car(52), car(96)

class Schermo:
    def __init__(s, L):
        s.L = L
        s.im = Image.new('RGB', (L['SW']*K, L['SH']*K), BG)
        s.d  = ImageDraw.Draw(s.im)
    def txt(s, x, y, t, f, c, anc='mm'): s.d.text((x*K, y*K), t, font=f, fill=c, anchor=anc)
    def hl(s, x, y, w, c): s.d.rectangle([x*K, y*K, (x+w)*K, y*K+K-1], fill=c)
    def vl(s, x, y, h, c): s.d.rectangle([x*K, y*K, x*K+K-1, (y+h)*K], fill=c)
    def rr(s, r, fill=None, outline=None, rad=10):
        s.d.rounded_rectangle([r[0]*K, r[1]*K, (r[0]+r[2])*K, (r[1]+r[3])*K], rad*K,
                              fill=fill, outline=outline, width=K)
    def btn(s, r, lab, sotto, col, scelto=False):
        s.rr(r, fill=BTN, outline=col)
        if scelto: s.rr((r[0]-2, r[1]-2, r[2]+4, r[3]+4), outline=FG, rad=12)
        cx, cy = r[0]+r[2]//2, r[1]+r[3]//2
        s.txt(cx, cy-16 if sotto else cy, lab, F4, FG)
        if sotto: s.txt(cx, cy+20, sotto, F2, LABEL)
    def freccia(s, cx, cy, r, ang, col):
        a = math.radians(ang)
        p = lambda rr, aa: ((cx + math.sin(aa)*rr)*K, (cy - math.cos(aa)*rr)*K)
        s.d.polygon([p(r,a), p(r*.78,a+2.44), p(-r*.28,a)], fill=col)
        s.d.polygon([p(r,a), p(r*.78,a-2.44), p(-r*.28,a)], fill=col)
    def rotonda(s, cx, cy, r, usc, ang, col):
        ra = int(r*0.58)
        for dr in (0,1):
            s.d.ellipse([(cx-ra+dr)*K,(cy-ra+dr)*K,(cx+ra-dr)*K,(cy+ra-dr)*K], outline=col, width=K)
        s.d.rectangle([cx*K,(cy+ra)*K,(cx+2)*K,(cy+r)*K], fill=col)
        a = math.radians(ang)
        x0 = ((cx+math.sin(a)*ra)*K, (cy-math.cos(a)*ra)*K)
        x1 = ((cx+math.sin(a)*(r+1))*K, (cy-math.cos(a)*(r+1))*K)
        s.d.line([x0,x1], fill=col, width=2*K)
        pp = 9 if r > 20 else 6
        s.d.polygon([x1, (x1[0]+math.sin(a+2.44)*pp*K, x1[1]-math.cos(a+2.44)*pp*K),
                         (x1[0]+math.sin(a-2.44)*pp*K, x1[1]-math.cos(a-2.44)*pp*K)], fill=col)
        if usc and ra >= 14: s.txt(cx, cy, str(usc), F4, col)
    def cunei(s, cx, cy, col):
        for i in range(2):
            x = cx+i*9
            s.d.polygon([(x*K,(cy-8)*K),(x*K,(cy+8)*K),((x+7)*K,cy*K)], fill=col)
    def barra(s, fuori=False):
        L = s.L
        s.d.ellipse([11*K,12*K,25*K,26*K], fill=BAD if fuori else OK)
        s.txt(34, 19, '8 sat  1.1' if L['vert'] else '8 sat   HDOP 1.1', F2, LABEL, 'lm')
        s.txt(L['SW']-8, 19, '14:32', F4, FG, 'rm')
        for k in range(3): s.hl(L['BAR_HINT'], 13+k*5, 14, LABEL)
        s.txt(L['BAR_HINT']+20, 19, 'menu', F2, LABEL, 'lm')
        s.hl(0, 38, L['SW'], LINE)

def nav(L):
    s = Schermo(L); s.barra()
    if L['NAV_VL'] >= 0: s.vl(L['NAV_VL'], L['NAV_VL_Y'], L['NAV_VL_H'], LINE)
    for y in L['NAV_HL']:
        if y >= 0: s.hl(L['NAV_HL_X'], y, L['NAV_HL_W'], LINE)
    s.freccia(L['ARROW_X'], L['ARROW_Y'], L['ARROW_R'], 16, CYAN)
    s.txt(L['NAV_REST_X'], L['NAV_REST_LY'], 'RESTANO', F2, LABEL)
    s.txt(L['NAV_REST_X'], L['NAV_REST_VY'], '34,2 km', F4, FG)
    s.txt(L['NAV_LINE_X'], L['NAV_LINE_Y'], '2a uscita > Via Po', F2, FG)
    s.txt(L['NAV_LBL_X'], L['NAV_LBL_Y'], 'SVOLTA', F2, LABEL)
    s.rr(L['SV_BOX'], outline=WARN, rad=8)
    s.rotonda(L['SV_X'], L['SV_Y'], L['SV_R'], 2, 90, WARN)
    s.cunei(L['NAV_MARK_X'], L['SV_Y'], CYAN)
    s.txt(L['NAV_DST_X'], L['NAV_DST_Y'], '90 m', F4X2, WARN)
    s.txt(L['NAV_POI_LX'], L['NAV_POI_LY'], 'DOPO', F2, LABEL)
    s.freccia(L['PO_X'], L['PO_Y'], L['PO_R'], -90, LABEL)
    s.txt(L['NAV_POI_DX'], L['NAV_POI_DY'], '600 m', F2, LABEL)
    s.txt(L['NAV_SPD_X'], L['NAV_SPD_Y'], '72', F4X2, FG)
    return s.im

def marcia(L):
    s = Schermo(L); s.barra()
    s.hl(0, L['DRV_HL1'], L['SW'], LINE)
    if L['DRV_HL2'] >= 0: s.hl(0, L['DRV_HL2'], L['SW'], LINE)
    s.vl(L['DRV_VL'], L['DRV_VL_Y'], L['DRV_VL_H'], LINE)
    s.txt(L['DRV_SPD_X'], L['DRV_SPD_Y'], '72', F7X2, FG)
    s.txt(L['DRV_KMH_X'], L['DRV_KMH_Y'], 'km/h', F4, LABEL)
    s.txt(L['DRV_DIR_X'], L['DRV_DIR_LY'], 'DIREZIONE', F2, LABEL)
    s.txt(L['DRV_DIR_X'], L['DRV_DIR_VY'], 'NE', F4X2, FG)
    s.txt(L['DRV_BASE_X'], L['DRV_BASE_LY'], 'DALLA BASE', F2, LABEL)
    s.txt(L['DRV_BASE_X'], L['DRV_BASE_VY'], '12,4 km', F4, OK)
    for i, (e, v) in enumerate((('VIAGGIO','84,2'),('MAX','143'),('IN MOVIMENTO','1:24'),('QUOTA','310'))):
        s.txt(L['DRV_ST_X'][i], L['DRV_ST_LY'][i], e, F2, LABEL)
        s.txt(L['DRV_ST_X'][i], L['DRV_ST_VY'][i], v, F4, FG)
    return s.im

def menu(L):
    s = Schermo(L)
    s.txt(14, 19, 'MENU', F4, FG, 'lm'); s.hl(0, 38, L['SW'], LINE)
    v = L['vert']
    for i, (t, sot, c) in enumerate((('Naviga','percorso attivo',CYAN),
                                     ('Percorso','184,6 km' if v else '184,6 km, 63 svolte',WARN),
                                     ('Telefono','192.168.1.42',CYAN),
                                     ('Viaggio','84,2 km' if v else '84,2 km in questo viaggio',OK))):
        s.btn(L['MENU_BTN%d' % i], t, sot, c, scelto=(i == 0))
    s.btn(L['MENU_LUCE'], 'Luce   70%   automatica', '', LABEL)
    s.btn(L['MENU_SCHERMO'], 'Schermo   in piedi' if v else 'Schermo', '', LABEL)
    return s.im

def schermo(L):
    s = Schermo(L)
    s.txt(14, 19, 'SCHERMO', F4, FG, 'lm'); s.hl(0, 38, L['SW'], LINE)
    v = L['vert']
    s.btn(L['SCHERMO_BTN0'], 'Sdraiato', '480 x 320' if v else 'in uso adesso',
          LINE if v else OK, scelto=not v)
    s.btn(L['SCHERMO_BTN1'], 'In piedi', 'in uso adesso' if v else '320 x 480',
          OK if v else LINE, scelto=v)
    s.txt(L['SW']//2, L['PAGE_BTN0'][1]-20, 'la scheda si riavvia e chiede di ritarare il tocco', F2, LABEL)
    s.btn(L['PAGE_BTN0'], 'Indietro', '', LINE)
    return s.im

def percorso(L):
    s = Schermo(L)
    s.txt(14, 19, 'PERCORSO', F4, FG, 'lm'); s.hl(0, 38, L['SW'], LINE)
    for y, (e, v) in zip(L['RIGA_Y'], (('lunghezza','184,6 km'),('punti','1998'),
                                       ('svolte','63'),('tappe','4'))):
        s.txt(L['RIGA_LX'], y, e, F2, LABEL, 'lm')
        s.txt(L['RIGA_VX'], y, v, F4, FG, 'lm')
    s.btn(L['ROUTE_RICALC'], 'Ricalcola il rientro', '', WARN, scelto=True)
    s.btn(L['PAGE_BTN0'], 'Indietro', '', LINE)
    s.btn(L['PAGE_BTN1'], 'Nuovo', '', CYAN)
    s.btn(L['PAGE_BTN2'], 'Cancella', '', BAD)
    return s.im

S = sys.argv[1]
for nome, file in (('oriz', 'lay_oriz.json'), ('vert', 'lay_vert.json')):
    L = json.load(open(f'{S}/{file}'))
    for f, etichetta in ((nav,'nav'), (marcia,'marcia'), (menu,'menu'),
                         (schermo,'schermo'), (percorso,'percorso')):
        f(L).save(f'{S}/v-{etichetta}-{nome}.png')
print('dieci schermate disegnate dalle misure vere')
