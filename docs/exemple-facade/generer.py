#!/usr/bin/env python3
"""Fabrique l'exemple de Façade « Console d'orgue » décrit par docs/specification-facade-complete.md.
Sorties : dossier `console-orgue/` (facade.json + assets/) et `Console-orgue.neptunefacade` (zip).
Aucun module externe : le fond PNG est écrit à la main."""
import json, os, struct, zlib, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "console-orgue")
ASSETS = os.path.join(SRC, "assets")
os.makedirs(ASSETS, exist_ok=True)

# ---------------------------------------------------------------- images
def write(name, text):
    with open(os.path.join(ASSETS, name), "w", encoding="utf-8") as f:
        f.write(text)

def svg(w, h, body):
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">\n{body}</svg>\n'

# curseur (tirant d'orgue), vertical : rail 24 x 180, poignée 44 x 26
write("1_rail.svg", svg(24, 180, '  <rect x="8" y="0" width="8" height="180" rx="4" fill="#1b1209" stroke="#6b5a3a" stroke-width="1"/>\n'
                                   '  <g stroke="#6b5a3a" stroke-width="1">' + "".join(f'<line x1="3" y1="{10 + i * 20}" x2="8" y2="{10 + i * 20}"/><line x1="16" y1="{10 + i * 20}" x2="21" y2="{10 + i * 20}"/>' for i in range(9)) + '</g>\n'))
write("2_poignee.svg", svg(44, 26, '  <rect x="1" y="1" width="42" height="24" rx="6" fill="#f1ead6" stroke="#3a2a14" stroke-width="2"/>\n'
                                    '  <rect x="6" y="6" width="32" height="14" rx="3" fill="#d9d2bd"/>\n'
                                    '  <line x1="12" y1="13" x2="32" y2="13" stroke="#3a2a14" stroke-width="2"/>\n'))
# boutons
write("3_bouton_off.svg", svg(120, 44, '  <rect x="2" y="2" width="116" height="40" rx="8" fill="#3b2d1b" stroke="#a98d5a" stroke-width="2"/>\n'))
write("4_bouton_on.svg", svg(120, 44, '  <rect x="2" y="2" width="116" height="40" rx="8" fill="#c8892a" stroke="#ffe0a0" stroke-width="2"/>\n'
                                       '  <rect x="6" y="6" width="108" height="14" rx="6" fill="#ffffff" opacity="0.18"/>\n'))
# potard en deux couches : corps fixe + aiguille (dessinée vers le haut)
write("5_potard_corps.svg", svg(100, 100, '  <circle cx="50" cy="50" r="44" fill="#2a1f12" stroke="#a98d5a" stroke-width="3"/>\n'
                                           '  <circle cx="50" cy="50" r="34" fill="#4a3822"/>\n'))
write("6_potard_aiguille.svg", svg(100, 100, '  <line x1="50" y1="50" x2="50" y2="14" stroke="#ffe0a0" stroke-width="5" stroke-linecap="round"/>\n'
                                             '  <circle cx="50" cy="50" r="6" fill="#ffe0a0"/>\n'))
# cadre de zone découpé en neuf zones
write("7_cadre.svg", svg(100, 100, '  <rect x="3" y="3" width="94" height="94" rx="14" fill="#1d150b" fill-opacity="0.78" stroke="#a98d5a" stroke-width="3"/>\n'))
# voyant
write("8_voyant_off.svg", svg(40, 40, '  <circle cx="20" cy="20" r="14" fill="#3a1410" stroke="#6b5a3a" stroke-width="2"/>\n'))
write("9_voyant_on.svg", svg(40, 40, '  <circle cx="20" cy="20" r="14" fill="#ff5a3c" stroke="#ffe0a0" stroke-width="2"/>\n'
                                       '  <circle cx="15" cy="14" r="4" fill="#ffffff" opacity="0.5"/>\n'))
# touches du clavier
write("10_touche_blanche.svg", svg(24, 110, '  <rect x="1" y="0" width="22" height="108" rx="3" fill="#f6f1e4" stroke="#3a2a14" stroke-width="1.5"/>\n'))
write("11_touche_blanche_on.svg", svg(24, 110, '  <rect x="1" y="0" width="22" height="108" rx="3" fill="#e8b25a" stroke="#3a2a14" stroke-width="1.5"/>\n'))
write("12_touche_noire.svg", svg(16, 70, '  <rect x="1" y="0" width="14" height="68" rx="2" fill="#1a1208" stroke="#000000" stroke-width="1"/>\n'))
write("13_touche_noire_on.svg", svg(16, 70, '  <rect x="1" y="0" width="14" height="68" rx="2" fill="#c8892a" stroke="#000000" stroke-width="1"/>\n'))

def png(path, w, h, pixel):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            raw += bytes(pixel(x, y))
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))

def bois(x, y):   # fond uni brun, veinures légères
    v = 0.5 + 0.5 * __import__("math").sin(y * 0.9 + 3.0 * __import__("math").sin(x * 0.012))
    base = 40 + int(14 * v) + int(10 * y / 600)
    return (base + 14, base - 4, base - 20)
png(os.path.join(ASSETS, "0_fond.png"), 1000, 600, bois)

# ---------------------------------------------------------------- Façade
def a(n): return "asset:assets/" + n
styles = [
    {"id": 1, "name": "Tirant", "track": a("1_rail.svg"), "handle": a("2_poignee.svg"), "text_colour": "#f1ead6", "text_size": 12, "text_position": 2},
    {"id": 2, "name": "Bouton", "off": a("3_bouton_off.svg"), "on": a("4_bouton_on.svg"), "text_colour": "#f1ead6", "text_size": 40},
    {"id": 3, "name": "Potard", "body": a("5_potard_corps.svg"), "pointer": a("6_potard_aiguille.svg"), "angles": [-135, 135], "pivot": [50, 50], "text_colour": "#f1ead6", "text_size": 14, "text_position": 2},
    {"id": 4, "name": "Cadre", "frame": a("7_cadre.svg"), "slice": [20, 20, 20, 20], "text_colour": "#ffe0a0", "text_size": 70, "text_position": 1},
    {"id": 5, "name": "Voyant", "off": a("8_voyant_off.svg"), "on": a("9_voyant_on.svg")},
    {"id": 6, "name": "Clavier", "key_white": a("10_touche_blanche.svg"), "key_white_on": a("11_touche_blanche_on.svg"), "key_black": a("12_touche_noire.svg"), "key_black_on": a("13_touche_noire_on.svg")},
]
widgets, nid = [], [0]
def add(**kw):
    nid[0] += 1
    kw = {"id": nid[0], **kw}
    widgets.append(kw)
    return nid[0]

def zone(x, y, w, h, title):
    return add(type=6, x=x, y=y, w=w, h=h, text=title, layout=0, style=4)

footages = ["16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'"]
# clavier supérieur : 9 tirants 9000..9008
zu = zone(20, 20, 620, 240, "Clavier supérieur")
for i, f in enumerate(footages):
    add(type=8, x=40 + 66 * i, y=54, w=48, h=176, param=9000 + i, text=f, vertical=1, style=1, parent=zu)
# clavier inférieur : 9 tirants 9009..9017 + pédale 9018, 9019
zl = zone(20, 270, 620, 240, "Clavier inférieur et pédale")
for i, f in enumerate(footages):
    add(type=8, x=40 + 54 * i, y=304, w=44, h=176, param=9009 + i, text=f, vertical=1, style=1, parent=zl)
add(type=8, x=40 + 54 * 9 + 6, y=304, w=44, h=176, param=9018, text="Péd 16'", vertical=1, style=1, parent=zl)
add(type=8, x=40 + 54 * 10 + 6, y=304, w=44, h=176, param=9019, text="Péd 8'", vertical=1, style=1, parent=zl)
# colonne de droite
zv = zone(660, 20, 320, 100, "Vibrato")
add(type=3, x=670, y=52, w=150, h=56, param=9024, text="Vibrato", parent=zv)
add(type=1, x=830, y=60, w=65, h=40, param=9025, text="Haut", style=2, parent=zv)
add(type=1, x=905, y=60, w=65, h=40, param=9026, text="Bas", style=2, parent=zv)
zs = zone(660, 130, 320, 130, "Leslie et réverbération")
add(type=3, x=670, y=164, w=150, h=56, param=9027, text="Leslie", parent=zs)
add(type=0, x=830, y=164, w=70, h=90, param=9028, text="Mélange", style=3, parent=zs)
add(type=0, x=900, y=164, w=70, h=90, param=9033, text="Réverb", style=3, parent=zs)
zp = zone(660, 270, 320, 110, "Pistons")
# instantanés complets des 9 tirants supérieurs : (16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1')
def snap(values): return [[9000 + i, float(v)] for i, v in enumerate(values)]
add(type=4, x=670, y=310, w=90, h=44, text="Plein jeu", style=2, parent=zp, snap=snap([8, 8, 8, 8, 8, 8, 8, 8, 8]))
add(type=4, x=770, y=310, w=90, h=44, text="Flûtes", style=2, parent=zp, snap=snap([0, 0, 8, 0, 0, 0, 0, 0, 0]))
add(type=4, x=870, y=310, w=90, h=44, text="Zéro", style=2, parent=zp, mode=1, snap=snap([0] * 9))
zd = zone(660, 390, 320, 120, "Saturation")
add(type=1, x=670, y=430, w=80, h=44, param=9029, text="Drive", style=2, parent=zd)
add(type=9, x=760, y=438, w=28, h=28, param=9029, source=0, style=5, parent=zd)
add(type=0, x=800, y=424, w=70, h=80, param=9030, text="Entrée", style=3, parent=zd)
add(type=0, x=890, y=424, w=70, h=80, param=9032, text="Sortie", style=3, parent=zd)
add(type=7, x=20, y=520, w=960, h=72, ch=1, first=36, hold=0, vel=100, wheels=1, style=6)
page1 = {"name": "Orgue", "image": a("0_fond.png"), "fit": 2, "styles": styles, "widgets": widgets}

# page 2 : table d'essai des autres types (étiquette, vumètres, courbe, Midi, Set)
widgets, nid[0] = [], 0
add(type=2, x=20, y=16, w=500, h=40, text="Console : contrôles et mesures")
z1 = zone(20, 70, 300, 250, "Niveaux")
add(type=12, x=60, y=110, w=44, h=190, vertical=1, source=0, parent=z1)
add(type=12, x=140, y=110, w=44, h=190, vertical=1, source=1, parent=z1)
add(type=12, x=220, y=110, w=44, h=190, vertical=1, source=2, parent=z1)
z2 = zone(340, 70, 400, 250, "Courbe de vélocité")
add(type=5, x=360, y=104, w=360, h=200, parent=z2)
z3 = zone(760, 70, 220, 250, "Messages MIDI")
add(type=11, x=780, y=110, w=180, h=44, text="Panique (CC 123)", style=2, parent=z3, midi={"kind": 2, "channel": 1, "d1": 123, "d2": 0}, target=1, fader=0)
add(type=11, x=780, y=170, w=180, h=44, text="Programme 1", style=2, parent=z3, midi={"kind": 3, "channel": 1, "d1": 0, "d2": 0}, target=3, fader=0)
add(type=11, x=780, y=230, w=180, h=44, text="Expression (CC 11)", style=2, parent=z3, midi={"kind": 2, "channel": 1, "d1": 11, "d2": 100}, target=1, fader=1)
z4 = zone(20, 340, 960, 150, "Pistons programmables")
add(type=10, x=40, y=390, w=90, h=44, text="Set", style=2, parent=z4)
for i in range(5):
    add(type=4, x=150 + 110 * i, y=390, w=100, h=44, text=f"Piston {i + 1}", style=2, parent=z4)
page2 = {"name": "Console", "image": a("0_fond.png"), "fit": 2,
         "styles": [dict(s) for s in styles if s["id"] in (2, 4)], "widgets": widgets}

doc = {"format": 1, "track": 0, "facade": {**page1, "pages": [page2]}}
with open(os.path.join(SRC, "facade.json"), "w", encoding="utf-8") as f:
    json.dump(doc, f, ensure_ascii=False, indent=1)

zpath = os.path.join(HERE, "Console-orgue.neptunefacade")
with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
    z.write(os.path.join(SRC, "facade.json"), "facade.json")
    for n in sorted(os.listdir(ASSETS)):
        z.write(os.path.join(ASSETS, n), "assets/" + n)
print("ecrit", zpath, os.path.getsize(zpath), "octets")
