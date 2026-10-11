/*
  NOVA — Réglage de contraste partagé
  =============================================================
  Calcule, pour un niveau de 0 (réglage de base, rien ne change) à 4
  (maximum), les jetons de couleur qui portent le contraste. Ce n'est PAS
  un filtre : on recalcule des couleurs, en gardant leur teinte, pour que
  chaque rôle atteigne un rapport de contraste cible (WCAG) par rapport aux
  surfaces sur lesquelles il est posé. Le même algorithme (luminance
  relative + rapport cible) se porte tel quel en C++ pour NanoVG.

  Le thème "haut contraste" n'est jamais modifié : l'UI doit appeler
  apply(0) quand ce thème est actif et désactiver la pill.

  -------------------------------------------------------------
  UTILISATION DANS UNE UI
  -------------------------------------------------------------
    <script src="nova-contrast.js"></script>

    // après avoir posé data-theme (ou la classe de thème) sur <html> :
    NovaContrast.apply(theme === 'hc' ? 0 : niveau);   // 0..4
    // puis relire les couleurs mises en cache pour les canvas et redessiner.

  Jetons gérés (voir ROLES) : --text, --text-dim, --text-dim2, --accent,
  --accent-2, --pink, --pink-2, --knob-hot, --knob-hot-2, --band-1/2/3,
  --band-1..8, --handle, --border, --ctl-bd, --pb/--pbt (ou --panel-border/-top), --grid, --knob-track, --logo-ink et le
  fond --panel-bg (rendu progressivement opaque). Les couleurs absentes
  ou non analysables (dégradés...) sont simplement ignorées.

  Chaque UI doit exposer ces jetons ; toute couleur écrite en dur dans le
  CSS ou le canvas restera à contraste fixe.

  -------------------------------------------------------------
  TEST AUTOMATIQUE : node test_nova_contrast.js <fichier.html>
  =============================================================
*/
(function (g) {
  'use strict';
  const LEVELS = 4;

  // [jeton, rapport de contraste visé au niveau max, surfaces de référence]
  // surfaces : 'all' (fond, panneau, carte, en-tête), 'panel', 'edge' (panneau,
  // fond, carte : pour les bordures), 'knob' (corps du potard + panneau),
  // ou une liste de jetons (ex. le pictogramme posé sur le dégradé du logo).
  const ROLES = [
    ['--text', 12, 'all'], ['--text-dim', 8, 'all'], ['--text-dim2', 5, 'all'],
    ['--cream', 12, 'all'], ['--ivory', 12, 'all'], ['--cream-dim', 8, 'all'], ['--gold', 5, 'all'], ['--gold-dim', 5, 'all'],   // orgue Hammond
    ['--accent', 5, 'all'], ['--accent-2', 5, 'all'], ['--pink', 5, 'all'], ['--pink-2', 5, 'all'],
    ['--knob-hot-2', 4.5, 'all'], ['--knob-hot', 3, 'panel'],
    ['--band-1', 3.5, 'panel'], ['--band-2', 3.5, 'panel'], ['--band-3', 3.5, 'panel'],
    ['--band-4', 3.5, 'panel'], ['--band-5', 3.5, 'panel'], ['--band-6', 3.5, 'panel'], ['--band-7', 3.5, 'panel'], ['--band-8', 3.5, 'panel'],
    ['--handle', 3, 'panel'],
    ['--border', 3.5, 'edge'], ['--ctl-bd', 3.5, 'edge'], ['--pb', 3.5, 'edge'], ['--pbt', 3.5, 'edge'], ['--panel-border', 3.5, 'edge'], ['--panel-border-top', 3.5, 'edge'],
    ['--grid', 2.2, 'panel'], ['--knob-track', 3, 'knob'],
    ['--logo-ink', 4.5, ['--accent', '--pink']]
  ];
  const BASE_TOKENS = ['--bg-app', '--panel-bg', '--bg-card', '--header-bg', '--knob-face']
    .concat(ROLES.map(r => r[0]));

  const clamp = (v, a, b) => Math.max(a, Math.min(b, v));

  function parse(s) {
    if (!s) return null;
    s = String(s).trim().toLowerCase();
    let m;
    if ((m = /^#([0-9a-f]{3})$/.exec(s))) { const h = m[1]; return [0, 1, 2].map(i => parseInt(h[i] + h[i], 16)).concat(1); }
    if ((m = /^#([0-9a-f]{6})$/.exec(s))) return [0, 2, 4].map(i => parseInt(m[1].slice(i, i + 2), 16)).concat(1);
    if ((m = /^rgba?\(([^)]+)\)$/.exec(s))) {
      const p = m[1].split(/[\s,\/]+/).filter(Boolean).map(parseFloat);
      if (p.length >= 3 && p.slice(0, 3).every(isFinite)) return [p[0], p[1], p[2], p.length > 3 && isFinite(p[3]) ? p[3] : 1];
    }
    return null;
  }
  const lin = c => { c /= 255; return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4); };
  const lum = c => 0.2126 * lin(c[0]) + 0.7152 * lin(c[1]) + 0.0722 * lin(c[2]);
  const ratio = (a, b) => { const x = lum(a), y = lum(b); return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05); };
  const over = (fg, bg) => { const a = fg.length > 3 ? fg[3] : 1; return [0, 1, 2].map(i => fg[i] * a + bg[i] * (1 - a)); };
  const mix = (a, b, t) => [0, 1, 2].map(i => a[i] + (b[i] - a[i]) * t);
  const hex = c => '#' + c.map(v => Math.round(clamp(v, 0, 255)).toString(16).padStart(2, '0')).join('');
  const minR = (c, surfs) => Math.min.apply(null, surfs.map(s => ratio(c, s)));

  // Plus petit déplacement de la couleur (vers le blanc OU vers le noir) qui atteint le rapport visé
  // sur TOUTES les surfaces ; sinon, la meilleure couleur possible.
  function push(col, surfs, target) {
    const dirs = [[255, 255, 255], [0, 0, 0]], N = 128;
    let best = null;
    dirs.forEach(d => {
      for (let i = 0; i <= N; i++) {
        const c = mix(col, d, i / N);
        if (minR(c, surfs) >= target - 1e-9) { if (!best || i / N < best.t) best = { t: i / N, c }; break; }
      }
    });
    if (best) return best.c;
    let bc = col, br = minR(col, surfs);
    dirs.forEach(d => { for (let i = 0; i <= N; i++) { const c = mix(col, d, i / N), r = minR(c, surfs); if (r > br) { br = r; bc = c; } } });
    return bc;
  }

  // base : { '--jeton': 'valeur CSS' } du thème courant. Retourne uniquement les jetons à surcharger.
  function compute(base, level) {
    level = clamp(level | 0, 0, LEVELS);
    const out = {};
    if (!level) return out;
    const k = level / LEVELS;
    const pb = parse(base['--bg-app']) || [0, 0, 0, 1];
    const bg = [pb[0], pb[1], pb[2]];
    let panel = bg;
    const pc = parse(base['--panel-bg']);
    if (pc) {
      const a = pc[3] + (1 - pc[3]) * k;
      if (pc[3] < 1) out['--panel-bg'] = a >= 0.999 ? hex(pc.slice(0, 3)) : 'rgba(' + pc[0] + ',' + pc[1] + ',' + pc[2] + ',' + a.toFixed(3) + ')';
      panel = over([pc[0], pc[1], pc[2], a], bg);
    }
    const cc = parse(base['--bg-card']);
    const card = cc ? over(cc, bg) : panel;
    const hc = parse(base['--header-bg']);
    const header = hc ? over(hc, bg) : null;
    const fc = parse(base['--knob-face']);
    const face = fc ? over(fc, panel) : panel;
    const S = { all: [bg, panel, card].concat(header ? [header] : []), panel: [panel], edge: [panel, bg, card], knob: [face, panel] };

    ROLES.forEach(([tok, max, sk]) => {
      const col = parse(base[tok]);
      if (!col) return;
      const surfs = Array.isArray(sk)
        ? sk.map(t => { const c = parse(out[t] || base[t]); return c ? over(c, panel) : null; }).filter(Boolean)
        : S[sk];
      if (!surfs || !surfs.length) return;
      const eff = over(col, panel);
      const cur = minR(eff, surfs);
      if (max <= cur + 1e-6) return;                 // déjà assez contrasté : on ne touche pas
      // rôle dérivé d'autres jetons (ex. pictogramme sur dégradé) : on vise directement le rapport max
      out[tok] = hex(push(eff, surfs, Array.isArray(sk) ? max : cur + (max - cur) * k));
    });
    return out;
  }

  // Version navigateur : surcharge les jetons sur <html> (style inline), niveau 0 = on retire tout.
  function apply(level, el) {
    el = el || document.documentElement;
    const managed = BASE_TOKENS.filter(t => t !== '--bg-app' && t !== '--bg-card' && t !== '--header-bg' && t !== '--knob-face').concat('--panel-bg');
    managed.forEach(t => el.style.removeProperty(t));
    level = clamp(level | 0, 0, LEVELS);
    if (!level) return {};
    const cs = getComputedStyle(el), base = {};
    BASE_TOKENS.forEach(t => { base[t] = cs.getPropertyValue(t).trim(); });
    const out = compute(base, level);
    Object.keys(out).forEach(t => el.style.setProperty(t, out[t]));
    return out;
  }

  const api = { LEVELS, ROLES, compute, apply, parse, ratio, over, minR };
  if (typeof module !== 'undefined' && module.exports) module.exports = api; else g.NovaContrast = api;
})(typeof window !== 'undefined' ? window : this);
