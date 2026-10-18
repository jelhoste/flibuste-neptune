// Test des rapports de contraste de NovaContrast sur les thèmes d'une UI NOVA.
// Usage : node test_nova_contrast.js nova_lowlatconv_ui.html
const fs = require('fs'), NC = require('./nova-contrast.js');
const html = fs.readFileSync(process.argv[2] || 'nova_lowlatconv_ui.html', 'utf8');
const block = sel => { let i = html.indexOf(sel + '{'); if (i < 0) i = html.indexOf(sel.replace(/=(\w+)\]/, '="$1"]') + '{'); if (i < 0) i = html.indexOf(sel.replace(/=(\w+)\]/, '="$1"]') + ' {'); if (i < 0) return {}; const j = html.indexOf('}', i);
  const o = {}; html.slice(html.indexOf('{', i) + 1, j).split(';').forEach(d => { const k = d.indexOf(':'); if (k > 0) o[d.slice(0, k).trim()] = d.slice(k + 1).trim(); }); return o; };
const root = block(':root');
const css = fs.existsSync('nova-controls.css') ? fs.readFileSync('nova-controls.css', 'utf8') : '';
const skin = n => { const i = css.indexOf('[data-ctl="' + n + '"]{'); if (i < 0) return {}; const j = css.indexOf('}', i), o = {};
  css.slice(i, j).replace(/^[^{]*\{/, '').split(';').forEach(d => { const k = d.indexOf(':'); if (k > 0) o[d.slice(0, k).trim()] = d.slice(k + 1).trim(); }); return o; };
const withSkin = (b, n) => { const o = Object.assign({}, b, skin('glass'), n === 'glass' ? {} : skin(n)); for (let p = 0; p < 3; p++) for (const k in o) o[k] = o[k].replace(/var\((--[\w-]+)\)/g, (_, v) => o[v] !== undefined ? o[v] : _); return o; };
const themes0 = { dark: root, hammond: Object.assign({}, root, block('[data-theme=hammond]')), light: Object.assign({}, root, block('[data-theme=light]')) };
const classMode = html.includes('html.theme-light{');
const themes = classMode
  ? { dark: withSkin(root, 'bezel'), modern: withSkin(Object.assign({}, root, block('html.theme-modern')), 'glass'), light: withSkin(Object.assign({}, root, block('html.theme-light')), 'glass') }
  : { dark: withSkin(themes0.dark, 'glass'), hammond: withSkin(themes0.hammond, 'bezel'), light: withSkin(themes0.light, 'glass') };
let fail = 0; const bad = m => { fail++; console.log('  ECHEC : ' + m); };
for (const [name, base] of Object.entries(themes)) {
  console.log('\n== thème ' + name);
  const prev = {};
  if (Object.keys(NC.compute(base, 0)).length) bad('le niveau 0 modifie des jetons');
  for (let L = 0; L <= 4; L++) {
    const out = NC.compute(base, L), full = Object.assign({}, base, out);
    const bg = NC.parse(full['--bg-app']), pc = NC.parse(full['--panel-bg']), panel = NC.over(pc, bg), card = NC.over(NC.parse(full['--bg-card']), bg);
    const surf = { all: [bg, panel, card], panel: [panel], edge: [panel, bg, card] };
    const row = [];
    NC.ROLES.forEach(([tok, max, sk]) => {
      const c = NC.parse(full[tok]); if (!c) return;
      const ss = Array.isArray(sk) ? sk.map(t => NC.over(NC.parse(full[t]), panel)) : sk === 'knob' ? [NC.over(NC.parse(full['--knob-face']), panel), panel] : surf[sk];
      const r = NC.minR(NC.over(c, panel), ss);
      if (prev[tok] !== undefined && r < prev[tok] - 0.02 && r < max) bad(tok + ' : le contraste baisse au niveau ' + L);
      prev[tok] = r;
      if (L === 4 && r < Math.min(max, 99) - 0.1 && !(NC.minR(NC.over(c, panel), ss) >= max)) bad(tok + ' n\'atteint pas ' + max + ':1 (obtenu ' + r.toFixed(2) + ')');
      if (L === 4 && tok === '--text' && r < 7) bad('--text < 7:1');
      if (L === 4 && (tok === '--text-dim' || tok === '--text-dim2') && r < 4.5) bad(tok + ' < 4.5:1');
      if (L === 0 || L === 4) row.push(tok.replace('--', '') + '=' + r.toFixed(1));
    });
    if (L === 0 || L === 4) console.log(' niveau ' + L + ' : ' + row.join('  '));
  }
}
console.log(fail ? '\n' + fail + ' échec(s)' : '\nTous les contrôles passent.');
process.exit(fail ? 1 : 0);
