#!/usr/bin/env python3
"""Génère les deux packs de styles d'orgue de Neptune (format .neptunestyle, voir docs/style-pack-specification.en.md) :

  organ-wood-brass     bois et laiton à l'ancienne (couleurs fixes)
  organ-flat-modern    plat et moderne (couleurs du thème de l'application : {{accent}}, {{card}}, {{border}}...)

Sortie : assets/style-packs/<pack>/<style>/*.svg + style.json (dossier de travail, accepté tel quel par Neptune)
         assets/style-packs/<pack>.neptunestyle (le même contenu en zip).
Seulement le sous-ensemble de SVG que JUCE sait dessiner : formes, dégradés, opacité ; ni filtre, ni texte, ni CSS.
Usage : python3 scripts/gen_organ_style_packs.py
"""
import json
import math
import os
import shutil
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'assets', 'style-packs')


def svg(w, h, body, defs=''):
    d = f'<defs>\n{defs}</defs>\n' if defs else ''
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">\n{d}{body}</svg>\n'


def stops(items):
    return ''.join(f'<stop offset="{o}" stop-color="{c}"/>' for o, c in items)


def lg(i, x1, y1, x2, y2, items):
    return f'<linearGradient id="{i}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}">{stops(items)}</linearGradient>\n'


def rg(i, cx, cy, r, items, fx=None, fy=None):
    f = f' fx="{fx}" fy="{fy}"' if fx is not None else ''
    return f'<radialGradient id="{i}" cx="{cx}" cy="{cy}" r="{r}"{f}>{stops(items)}</radialGradient>\n'


def grain(w, h, rows, colour, opacity, seed=1, x0=0):
    """Veines horizontales ondulées (chemins), pour le bois."""
    out = []
    for r in range(rows):
        y = h * (r + 0.5) / rows
        amp = 0.8 + ((r * 7 + seed * 3) % 5) * 0.5
        d = f'M{x0} {y:.1f}'
        step = max(w / 6, 8)
        x = x0
        k = 0
        while x < x0 + w:
            nx = min(x + step, x0 + w)
            cy = y + (amp if (k + r + seed) % 2 == 0 else -amp)
            d += f' Q{(x + nx) / 2:.1f} {cy:.1f} {nx:.1f} {y + ((r * 13 + k * 5 + seed) % 5 - 2) * 0.35:.1f}'
            x = nx
            k += 1
        out.append(f'<path d="{d}" fill="none" stroke="{colour}" stroke-width="1" opacity="{opacity}"/>')
    return '\n'.join(out) + '\n'


# ---------------------------------------------------------------- bois et laiton
BRASS = [(0, '#f6e2a0'), (0.35, '#d4ab52'), (0.7, '#a37d2c'), (1, '#6e5218')]
BRASS_BRIGHT = [(0, '#fff3c0'), (0.35, '#f0c862'), (0.7, '#c99a34'), (1, '#8a6a20')]
WOOD = [(0, '#5a361c'), (0.5, '#43280f'), (1, '#2e1a0b')]
WOOD_LIT = [(0, '#74492a'), (0.5, '#5a3820'), (1, '#40260f')]
IVORY = [(0, '#e9dfc0'), (1, '#cdbf98')]
IVORY_LIT = [(0, '#fff8e0'), (1, '#efe2b8')]
AMBER = '#ffb347'


def wood_brass():
    P = {}

    # --- plaquette de tirant (3:1)
    def tablet(on):
        w, h = 180, 60
        defs = (lg('wood', 0, 0, 0, 1, WOOD_LIT if on else WOOD) + lg('brass', 0, 0, 0, 1, BRASS_BRIGHT if on else BRASS)
                + lg('plate', 0, 0, 0, 1, IVORY_LIT if on else IVORY))
        shadow = '<rect x="5" y="6" width="174" height="52" rx="9" fill="#000000" opacity="0.4"/>\n' if on else ''
        top = 1 if on else 2
        body = (shadow
                + f'<rect x="2" y="{top}" width="176" height="56" rx="9" fill="url(#wood)" stroke="#170c04" stroke-width="2"/>\n'
                + grain(168, 52, 7, '#150a03', 0.35, 2, 6).replace('<path', f'<path transform="translate(0 {top + 2})"')
                + f'<rect x="8" y="{top + 6}" width="164" height="44" rx="5" fill="none" stroke="url(#brass)" stroke-width="3"/>\n'
                + f'<rect x="14" y="{top + 11}" width="152" height="34" rx="3" fill="url(#plate)" stroke="#6b5a36" stroke-width="1"/>\n'
                + f'<rect x="15" y="{top + 12}" width="150" height="3" rx="1.5" fill="#ffffff" opacity="{0.55 if on else 0.3}"/>\n'
                + (f'<rect x="20" y="{top + 41}" width="140" height="2" rx="1" fill="{AMBER}"/>\n' if on else '')
                + ''.join(f'<circle cx="{cx}" cy="{top + 28}" r="3.5" fill="url(#brass)" stroke="#4a3610" stroke-width="0.8"/>'
                          f'<line x1="{cx - 2}" y1="{top + 28 - 1}" x2="{cx + 2}" y2="{top + 28 + 1}" stroke="#3a2a0c" stroke-width="1"/>\n' for cx in (7, 173)))
        return svg(w, h, body, defs)
    P['tablet'] = ({'off.svg': tablet(False), 'on.svg': tablet(True)},
                   {'name': 'Drawstop tablet (wood and brass)', 'text': {'colour': '#2b1b0b', 'size': 40, 'position': 'center', 'bold': True}})

    # --- tirant rond (1:1)
    def knob(on):
        defs = (rg('wood', 0.4, 0.35, 0.8, [(0, '#7a4e2c'), (1, '#2e1a0b')]) + lg('brass', 0, 0, 1, 1, BRASS_BRIGHT if on else BRASS)
                + rg('face', 0.4, 0.3, 0.8, IVORY_LIT if on else IVORY))
        sh = '<circle cx="54" cy="56" r="45" fill="#000000" opacity="0.4"/>\n' if on else ''
        body = (sh + '<circle cx="50" cy="50" r="46" fill="url(#wood)" stroke="#170c04" stroke-width="2"/>\n'
                + '<circle cx="50" cy="50" r="40" fill="none" stroke="url(#brass)" stroke-width="5"/>\n'
                + '<circle cx="50" cy="50" r="32" fill="url(#face)" stroke="#6b5a36" stroke-width="1"/>\n'
                + '<path d="M26 40 Q50 22 74 40" fill="none" stroke="#ffffff" stroke-width="3" stroke-linecap="round" opacity="0.4"/>\n'
                + (f'<circle cx="50" cy="50" r="35.5" fill="none" stroke="{AMBER}" stroke-width="1.6"/>\n' if on else ''))
        return svg(100, 100, body, defs)
    P['drawstop-round'] = ({'off.svg': knob(False), 'on.svg': knob(True)},
                           {'name': 'Round drawstop (wood and brass)', 'text': {'colour': '#2b1b0b', 'size': 22, 'position': 'center', 'bold': True}})

    # --- piston (1:1)
    def piston(on):
        defs = (rg('dome', 0.38, 0.3, 0.85, [(0, '#fff3c0'), (0.5, '#e0b650'), (1, '#8a6a20')] if on else [(0, '#e8d08a'), (0.5, '#b58e3c'), (1, '#6a4f18')])
                + lg('ring', 0, 0, 0, 1, [(0, '#8a6a20'), (1, '#f0d27a')]))
        dy = 2 if on else 0
        body = ('<circle cx="50" cy="50" r="47" fill="#2a1809" stroke="#170c04" stroke-width="2"/>\n'
                + '<circle cx="50" cy="50" r="42" fill="none" stroke="url(#ring)" stroke-width="3"/>\n'
                + f'<circle cx="50" cy="{50 + dy}" r="35" fill="url(#dome)" stroke="#5a4210" stroke-width="1.5"/>\n'
                + f'<ellipse cx="42" cy="{38 + dy}" rx="14" ry="7" fill="#ffffff" opacity="{0.5 if on else 0.28}"/>\n'
                + (f'<circle cx="50" cy="50" r="46" fill="none" stroke="{AMBER}" stroke-width="2" opacity="0.7"/>\n' if on else ''))
        return svg(100, 100, body, defs)
    P['piston'] = ({'off.svg': piston(False), 'on.svg': piston(True)},
                   {'name': 'Brass piston', 'text': {'colour': '#2b1b0b', 'size': 30, 'position': 'center', 'bold': True}})

    # --- voyant (1:1)
    def lamp(on):
        defs = (rg('glass', 0.4, 0.35, 0.75, [(0, '#fff0b0'), (0.45, '#ffb020'), (1, '#b85a00')] if on else [(0, '#7a3a28'), (1, '#2b0d08')])
                + lg('brass', 0, 0, 1, 1, BRASS))
        body = ((f'<circle cx="20" cy="20" r="19.5" fill="{AMBER}" opacity="0.35"/>\n' if on else '')
                + '<circle cx="20" cy="20" r="15" fill="url(#brass)" stroke="#4a3610" stroke-width="1"/>\n'
                + '<circle cx="20" cy="20" r="11.5" fill="url(#glass)" stroke="#3a2a0c" stroke-width="0.8"/>\n'
                + f'<ellipse cx="16.5" cy="16" rx="4" ry="2.4" fill="#ffffff" opacity="{0.6 if on else 0.25}"/>\n')
        return svg(40, 40, body, defs)
    P['lamp'] = ({'off.svg': lamp(False), 'on.svg': lamp(True)}, {'name': 'Lamp (amber jewel)'})

    # --- curseurs
    def track_h():
        defs = lg('wood', 0, 0, 0, 1, WOOD) + lg('brass', 0, 0, 0, 1, BRASS)
        return svg(300, 24, '<rect x="0.75" y="0.75" width="298.5" height="22.5" rx="11" fill="url(#wood)" stroke="url(#brass)" stroke-width="1.5"/>\n'
                   '<rect x="8" y="9" width="284" height="6" rx="3" fill="#0f0702"/>\n<rect x="8" y="15" width="284" height="1.2" fill="#8a6038" opacity="0.7"/>\n', defs)

    def handle_h():
        defs = lg('b', 0, 0, 1, 0, [(0, '#8a6a20'), (0.3, '#f6e2a0'), (0.6, '#d4ab52'), (1, '#6e5218')])
        ridges = ''.join(f'<line x1="5" y1="{y}" x2="19" y2="{y}" stroke="#4a3610" stroke-width="1" opacity="0.7"/>' for y in (16, 20, 28, 32))
        return svg(24, 48, '<rect x="1" y="1" width="22" height="46" rx="6" fill="url(#b)" stroke="#2e1f08" stroke-width="1.6"/>\n' + ridges
                   + '<rect x="9" y="22" width="6" height="4" rx="1" fill="#2e1f08"/>\n', defs)
    P['slider-h'] = ({'track.svg': track_h(), 'handle.svg': handle_h()}, {'name': 'Horizontal slider (wood and brass)'})

    def track_v():
        defs = lg('wood', 0, 0, 1, 0, WOOD) + lg('brass', 1, 0, 0, 0, BRASS)
        return svg(24, 300, '<rect x="0.75" y="0.75" width="22.5" height="298.5" rx="11" fill="url(#wood)" stroke="url(#brass)" stroke-width="1.5"/>\n'
                   '<rect x="9" y="8" width="6" height="284" rx="3" fill="#0f0702"/>\n<rect x="15" y="8" width="1.2" height="284" fill="#8a6038" opacity="0.7"/>\n', defs)

    def handle_v():
        defs = lg('b', 0, 0, 0, 1, [(0, '#8a6a20'), (0.3, '#f6e2a0'), (0.6, '#d4ab52'), (1, '#6e5218')])
        ridges = ''.join(f'<line x1="{x}" y1="5" x2="{x}" y2="19" stroke="#4a3610" stroke-width="1" opacity="0.7"/>' for x in (16, 20, 28, 32))
        return svg(48, 24, '<rect x="1" y="1" width="46" height="22" rx="6" fill="url(#b)" stroke="#2e1f08" stroke-width="1.6"/>\n' + ridges
                   + '<rect x="22" y="9" width="4" height="6" rx="1" fill="#2e1f08"/>\n', defs)
    P['slider-v'] = ({'track.svg': track_v(), 'handle.svg': handle_v()}, {'name': 'Vertical slider (wood and brass)'})

    # --- pédale d'expression : rail vertical de bois, poignée en forme de semelle de laiton
    def exp_track():
        defs = lg('wood', 0, 0, 1, 0, [(0, '#3a2210'), (0.5, '#5a3820'), (1, '#3a2210')]) + lg('brass', 0, 0, 1, 0, BRASS)
        ticks = ''.join(f'<line x1="14" y1="{y}" x2="30" y2="{y}" stroke="#d4ab52" stroke-width="1.2" opacity="0.8"/>' for y in range(20, 290, 27))
        return svg(44, 300, '<rect x="1" y="1" width="42" height="298" rx="6" fill="url(#wood)" stroke="#170c04" stroke-width="2"/>\n'
                   '<rect x="5" y="5" width="3" height="290" fill="url(#brass)"/><rect x="36" y="5" width="3" height="290" fill="url(#brass)"/>\n' + ticks, defs)

    def exp_handle():
        defs = lg('b', 0, 0, 0, 1, [(0, '#fff3c0'), (0.4, '#d4ab52'), (1, '#7a5a1c')])
        ribs = ''.join(f'<line x1="8" y1="{y}" x2="72" y2="{y}" stroke="#4a3610" stroke-width="1.4" opacity="0.65"/>' for y in (9, 14, 19, 24, 29))
        return svg(80, 36, '<rect x="1" y="1" width="78" height="34" rx="8" fill="url(#b)" stroke="#2e1f08" stroke-width="1.8"/>\n' + ribs, defs)
    P['expression-pedal'] = ({'track.svg': exp_track(), 'handle.svg': exp_handle()}, {'name': 'Expression pedal (vertical)'})

    # --- potard à deux couches
    ticks = ''.join(f'<line x1="{32 + 29 * math.sin(math.radians(a)):.2f}" y1="{32 - 29 * math.cos(math.radians(a)):.2f}" '
                    f'x2="{32 + 25.5 * math.sin(math.radians(a)):.2f}" y2="{32 - 25.5 * math.cos(math.radians(a)):.2f}" stroke="#f3e6c4" stroke-width="1.4" opacity="0.8"/>'
                    for a in range(-135, 136, 27))
    kdefs = rg('b', 0.38, 0.3, 0.85, [(0, '#fff3c0'), (0.5, '#d4ab52'), (1, '#7a5a1c')]) + rg('w', 0.4, 0.35, 0.8, [(0, '#7a4e2c'), (1, '#2e1a0b')])
    body = svg(64, 64, '<circle cx="32" cy="32" r="31" fill="url(#w)" stroke="#170c04" stroke-width="1.5"/>\n' + ticks
               + '<circle cx="32" cy="32" r="21" fill="url(#b)" stroke="#4a3610" stroke-width="1.2"/>\n'
               '<circle cx="32" cy="32" r="14" fill="none" stroke="#8a6a20" stroke-width="0.8"/>\n', kdefs)
    pointer = svg(64, 64, '<rect x="30" y="12" width="4" height="17" rx="2" fill="#2a1809"/>\n<circle cx="32" cy="32" r="3" fill="#2a1809"/>\n')
    P['knob'] = ({'body.svg': body, 'pointer.svg': pointer},
                 {'name': 'Brass knob', 'pointer_angles': [-135, 135], 'pivot': [50, 50]})

    # --- clavier : ivoire et ébène
    def white(on):
        defs = lg('g', 0, 0, 0, 1, [(0, '#f3dca0'), (0.85, '#e2b862'), (1, '#c9983a')] if on else [(0, '#fffdf3'), (0.86, '#efe5c8'), (1, '#cfc09a')])
        h = 106 if on else 110
        return svg(22, 110, f'<path d="M0.5 0 L21.5 0 L21.5 {h - 6} Q21.5 {h} 17 {h} L5 {h} Q0.5 {h} 0.5 {h - 6} Z" fill="url(#g)" stroke="#8b7d5a" stroke-width="1"/>\n'
                   f'<rect x="2" y="{h - 14}" width="18" height="10" rx="3" fill="#b8a574" opacity="0.35"/>\n', defs)

    def black(on):
        defs = lg('g', 0, 0, 1, 0, [(0, '#5a4220'), (0.5, '#a8721c'), (1, '#4a3418')] if on else [(0, '#3b3835'), (0.45, '#1e1c1a'), (1, '#0c0b0a')])
        h = 63 if on else 66
        return svg(13, 66, f'<path d="M0.5 0 L12.5 0 L12.5 {h - 4} Q12.5 {h} 9 {h} L4 {h} Q0.5 {h} 0.5 {h - 4} Z" fill="url(#g)" stroke="#000000" stroke-width="1"/>\n'
                   f'<rect x="2.5" y="2" width="3" height="{h - 16}" rx="1.5" fill="#ffffff" opacity="{0.1 if on else 0.2}"/>\n', defs)
    P['keys'] = ({'key_white.svg': white(False), 'key_white_on.svg': white(True), 'key_black.svg': black(False), 'key_black_on.svg': black(True)},
                 {'name': 'Ivory and ebony keys', 'key_lit_colour': '#ffb347'})

    # --- cadre de zone : bois avec bordure de laiton et clous aux coins (le centre est uni)
    fdefs = lg('wood', 0, 0, 0, 1, [(0, '#4a2c18'), (1, '#33200f')]) + lg('brass', 0, 0, 1, 1, BRASS)
    studs = ''.join(f'<circle cx="{x}" cy="{y}" r="4.2" fill="url(#brass)" stroke="#4a3610" stroke-width="0.8"/>' for x, y in ((12, 12), (108, 12), (12, 68), (108, 68)))
    frame = svg(120, 80, '<rect x="1" y="1" width="118" height="78" rx="12" fill="url(#wood)" stroke="#170c04" stroke-width="2"/>\n'
                '<rect x="4" y="4" width="112" height="72" rx="9" fill="none" stroke="url(#brass)" stroke-width="2.5"/>\n' + studs, fdefs)
    P['frame'] = ({'frame.svg': frame}, {'name': 'Wood panel with brass edge', 'slice': [20, 25, 20, 25], 'slice_scale': 100})
    return P


# ---------------------------------------------------------------- plat et moderne
def flat_modern():
    P = {}
    ACC = '{{accent}}'

    def tablet(on):
        body = (f'<rect x="2" y="2" width="176" height="56" rx="12" fill="{{{{card}}}}" stroke="{ACC if on else "{{border}}"}" stroke-width="{3 if on else 1.5}"/>\n'
                + (f'<rect x="14" y="48" width="152" height="4" rx="2" fill="{ACC}"/>\n' if on else '<rect x="14" y="48" width="152" height="4" rx="2" fill="{{border}}" opacity="0.5"/>\n'))
        return svg(180, 60, body)
    P['tablet'] = ({'off.svg': tablet(False), 'on.svg': tablet(True)}, {'name': 'Flat tab', 'text': {'size': 40, 'position': 'center', 'bold': True}})

    def knob(on):
        body = (f'<circle cx="50" cy="50" r="46" fill="{{{{card}}}}" stroke="{ACC if on else "{{border}}"}" stroke-width="{5 if on else 2}"/>\n'
                + (f'<circle cx="50" cy="50" r="36" fill="{ACC}" opacity="0.18"/>\n' if on else ''))
        return svg(100, 100, body)
    P['drawstop-round'] = ({'off.svg': knob(False), 'on.svg': knob(True)}, {'name': 'Flat round stop', 'text': {'size': 22, 'position': 'center', 'bold': True}})

    def piston(on):
        body = (f'<circle cx="50" cy="50" r="44" fill="{ACC if on else "{{card}}"}" stroke="{ACC if on else "{{border}}"}" stroke-width="3"/>\n'
                + ('' if on else '<circle cx="50" cy="50" r="34" fill="none" stroke="{{border}}" stroke-width="1.5" opacity="0.6"/>\n'))
        return svg(100, 100, body)
    P['piston'] = ({'off.svg': piston(False), 'on.svg': piston(True)}, {'name': 'Flat piston', 'text': {'size': 30, 'position': 'center', 'bold': True}})

    def lamp(on):
        return svg(40, 40, (f'<circle cx="20" cy="20" r="19" fill="{ACC}" opacity="0.25"/>\n' if on else '')
                   + f'<circle cx="20" cy="20" r="12" fill="{ACC if on else "{{card}}"}" stroke="{ACC if on else "{{border}}"}" stroke-width="2"/>\n')
    P['lamp'] = ({'off.svg': lamp(False), 'on.svg': lamp(True)}, {'name': 'Flat lamp'})

    P['slider-h'] = ({'track.svg': svg(300, 24, '<rect x="0" y="9" width="300" height="6" rx="3" fill="{{border}}"/>\n'),
                      'handle.svg': svg(24, 48, f'<rect x="2" y="2" width="20" height="44" rx="8" fill="{ACC}"/>\n<rect x="10" y="14" width="4" height="20" rx="2" fill="#ffffff" opacity="0.6"/>\n')},
                     {'name': 'Flat horizontal slider'})
    P['slider-v'] = ({'track.svg': svg(24, 300, '<rect x="9" y="0" width="6" height="300" rx="3" fill="{{border}}"/>\n'),
                      'handle.svg': svg(48, 24, f'<rect x="2" y="2" width="44" height="20" rx="8" fill="{ACC}"/>\n<rect x="14" y="10" width="20" height="4" rx="2" fill="#ffffff" opacity="0.6"/>\n')},
                     {'name': 'Flat vertical slider'})
    P['expression-pedal'] = ({'track.svg': svg(44, 300, '<rect x="2" y="2" width="40" height="296" rx="10" fill="{{card}}" stroke="{{border}}" stroke-width="1.5"/>\n'
                                              + ''.join(f'<line x1="14" y1="{y}" x2="30" y2="{y}" stroke="{{{{border}}}}" stroke-width="1.5"/>' for y in range(20, 290, 27))),
                              'handle.svg': svg(80, 36, f'<rect x="2" y="2" width="76" height="32" rx="10" fill="{ACC}"/>\n'
                                                + ''.join(f'<line x1="14" y1="{y}" x2="66" y2="{y}" stroke="#ffffff" stroke-width="2" opacity="0.5"/>' for y in (12, 18, 24)))},
                             {'name': 'Flat expression pedal'})
    P['knob'] = ({'body.svg': svg(64, 64, '<circle cx="32" cy="32" r="29" fill="{{card}}" stroke="{{border}}" stroke-width="2.5"/>\n'),
                  'pointer.svg': svg(64, 64, f'<rect x="30" y="8" width="4" height="20" rx="2" fill="{ACC}"/>\n')},
                 {'name': 'Flat knob', 'pointer_angles': [-135, 135], 'pivot': [50, 50]})
    P['keys'] = ({'key_white.svg': svg(22, 110, '<rect x="0.5" y="0" width="21" height="108" rx="4" fill="#f7f7f5" stroke="#9a9a96" stroke-width="1"/>\n'),
                  'key_white_on.svg': svg(22, 110, f'<rect x="0.5" y="0" width="21" height="108" rx="4" fill="{ACC}" opacity="0.85" stroke="#9a9a96" stroke-width="1"/>\n'),
                  'key_black.svg': svg(13, 66, '<rect x="0.5" y="0" width="12" height="65" rx="3" fill="#232326" stroke="{{border}}" stroke-width="1"/>\n'),
                  'key_black_on.svg': svg(13, 66, f'<rect x="0.5" y="0" width="12" height="65" rx="3" fill="{ACC}"/>\n')},
                 {'name': 'Flat keys'})
    P['frame'] = ({'frame.svg': svg(120, 80, '<rect x="1" y="1" width="118" height="78" rx="14" fill="{{card}}" stroke="{{border}}" stroke-width="2"/>\n')},
                  {'name': 'Flat panel', 'slice': [20, 25, 20, 25], 'slice_scale': 100})
    return P


PACKS = {
    'organ-wood-brass': ({'format': 1, 'name': 'Organ: wood and brass', 'author': 'Jean Lhoste, with Claude', 'licence': 'AGPL-3.0-or-later', 'version': '1.0',
                          'description': 'Drawstop tablets and knobs, pistons, lamps, sliders, expression pedal, knob and ivory keys in aged wood and brass.'}, wood_brass),
    'organ-flat-modern': ({'format': 1, 'name': 'Organ: flat and modern', 'author': 'Jean Lhoste, with Claude', 'licence': 'AGPL-3.0-or-later', 'version': '1.0',
                           'description': 'The same controls, flat, drawn with the theme colours of the application (light, dark, high contrast).'}, flat_modern),
}


def main():
    os.makedirs(OUT, exist_ok=True)
    for pack_id, (info, build) in PACKS.items():
        root = os.path.join(OUT, pack_id)
        shutil.rmtree(root, ignore_errors=True)
        os.makedirs(root)
        with open(os.path.join(root, 'pack.json'), 'w', encoding='utf-8') as f:
            json.dump(info, f, indent=2, ensure_ascii=False)
        files = ['pack.json']
        for style, (images, meta) in build().items():
            os.makedirs(os.path.join(root, style))
            for name, text in images.items():
                with open(os.path.join(root, style, name), 'w', encoding='utf-8') as f:
                    f.write(text)
                files.append(f'{style}/{name}')
            with open(os.path.join(root, style, 'style.json'), 'w', encoding='utf-8') as f:
                json.dump(meta, f, indent=2, ensure_ascii=False)
            files.append(f'{style}/style.json')
        archive = os.path.join(OUT, pack_id + '.neptunestyle')
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
            for rel in sorted(files):
                z.write(os.path.join(root, rel), rel)
        print(f'{pack_id}: {len(files)} files -> {os.path.relpath(archive)}')


if __name__ == '__main__':
    main()
