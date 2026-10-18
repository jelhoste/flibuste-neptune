/*
  NOVA — Redimensionnement par la poignée de coin (partagé par toutes les UI)
  =============================================================
  Une petite poignée en bas à droite du cadre du plugin : on la glisse pour agrandir ou réduire
  l'interface en CONSERVANT LE RAPPORT D'ASPECT (le cadre est mis à l'échelle, jamais déformé).
  Le réglage « Interface scale » de Settings reste, comme raccourci (75 / 100 / 125 / 150 %) et
  comme affichage de la taille actuelle (ex. « 112% »).

  -------------------------------------------------------------
  UTILISATION DANS UNE UI
  -------------------------------------------------------------
    <script src="nova-resize.js"></script>

    const NR = NovaResize.attach({
      frames: document.querySelectorAll('.app-frame'),   // un ou plusieurs cadres (mis à l'échelle ensemble)
      w: 1160, h: 780,                                   // taille de base du cadre, en px
      margin: 48,                                        // marge gardée autour tant que l'utilisateur n'a rien choisi
      onScale: (s) => { ... }                           // redimensionner les canvas, etc.
    });
    NR.bindPill(document.getElementById('setScale'), labelEl);  // raccourcis de Settings (labelEl : élément qui porte le texte)

  Comportement :
    - tant que l'utilisateur n'a pas redimensionné : échelle = min(1, fenêtre / taille de base) (comme avant) ;
    - dès qu'il glisse la poignée ou choisit un raccourci : échelle libre 50 % → 200 %, mémorisée (localStorage, clé « nova-scale »,
      partagée entre toutes les UI de la série) ;
    - pendant le glissement : bulle « largeur × hauteur · % » ; accroche à 100 % (et aux raccourcis) à ±3 % ;
    - double-clic sur la poignée : retour à 100 %.

  -------------------------------------------------------------
  PORTAGE DPF / NANOVG (spécification)
  -------------------------------------------------------------
  - DPF fournit un widget de poignée de coin dans DGL (classe ResizeHandle, à vérifier dans la version de DPF utilisée) et
    Window::setGeometryConstraints(largeurMin, hauteurMin, true) pour conserver le rapport d'aspect.
  - Taille de base 1160 × 780 (Hammond 1180 × 1020) ; taille minimale = 50 %, maximale = 200 % (ou limite de l'hôte).
  - Dessin : toute la scène est dessinée à la taille de base, sous nvgScale(échelle) où échelle = largeurFenêtre / largeurBase.
    Les coordonnées souris sont divisées par l'échelle avant le test de survol (déjà le cas pour les poignées d'EQ de la maquette).
  - En CLAP, le plugin déclare la fenêtre redimensionnable (can_resize) et l'hôte peut accepter, ajuster ou refuser la taille
    (adjust_size) : prévoir de relire la taille réellement obtenue. Mémoriser la dernière taille dans l'état du plugin.
  - Aspect de la poignée : trois traits diagonaux (couleur --text-dim2, --accent au survol), zone de capture 16 × 16 px, curseur
    de redimensionnement diagonal.
*/
(function (g) {
  'use strict';
  const MIN = 0.5, MAX = 2, PRESETS = [0.75, 1, 1.25, 1.5], KEY = 'nova-scale';
  const clamp = (v, a, b) => Math.max(a, Math.min(b, v));

  function css() {
    if (document.getElementById('nova-resize-css')) return;
    const st = document.createElement('style');
    st.id = 'nova-resize-css';
    st.textContent =
      '.nova-grip{position:absolute;right:2px;bottom:2px;width:16px;height:16px;z-index:80;cursor:nwse-resize;touch-action:none;color:var(--text-dim2);opacity:.7;display:flex;align-items:flex-end;justify-content:flex-end}' +
      '.nova-grip:hover,.nova-grip.on{color:var(--accent);opacity:1}' +
      '.nova-grip svg{display:block;pointer-events:none}' +
      '.nova-grip-tip{position:absolute;right:22px;bottom:6px;z-index:80;display:none;padding:4px 9px;border-radius:12px;font-size:11px;font-weight:700;letter-spacing:.4px;white-space:nowrap;' +
      'background:var(--bg-card);color:var(--text);border:1px solid var(--ctl-bd,var(--border));pointer-events:none}' +
      '[data-theme=hc] .nova-grip-tip,html.theme-hc .nova-grip-tip{border:2px solid #fff}';
    document.head.appendChild(st);
  }

  function attach(o) {
    css();
    const frames = Array.prototype.slice.call(o.frames), W = o.w, H = o.h, margin = o.margin == null ? 48 : o.margin;
    let user = false, scale = 1;
    const labels = [], zoomers = [];
    try { const v = parseFloat(localStorage.getItem(KEY)); if (v >= MIN && v <= MAX) { user = true; scale = v; } } catch (e) {}

    function apply() {
      frames.forEach(f => { f.style.zoom = scale; });
      zoomers.forEach(z => { z.style.zoom = 1 / scale; });   // la poignée garde sa taille à l'écran
      labels.forEach(l => { l.textContent = Math.round(scale * 100) + '%'; });
      o.onScale && o.onScale(scale, user);
    }
    function refit() {
      if (!user) scale = Math.min(1, (window.innerWidth - margin) / W);
      apply();
    }
    function set(s) {
      user = true; scale = clamp(s, MIN, MAX);
      try { localStorage.setItem(KEY, scale); } catch (e) {}
      apply();
    }

    frames.forEach(f => {
      if (getComputedStyle(f).position === 'static') f.style.position = 'relative';
      const grip = document.createElement('div');
      grip.className = 'nova-grip';
      grip.title = 'Glisser pour redimensionner (double-clic : 100 %)';
      grip.innerHTML = '<svg width="14" height="14" viewBox="0 0 14 14"><path d="M13 5L5 13M13 9L9 13M13 1L1 13" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/></svg>';
      const tip = document.createElement('div');
      tip.className = 'nova-grip-tip';
      f.appendChild(grip); f.appendChild(tip); zoomers.push(grip, tip);

      let d = null;
      grip.addEventListener('pointerdown', e => {
        d = { x: e.clientX, y: e.clientY, s: scale };
        grip.setPointerCapture(e.pointerId); grip.classList.add('on'); tip.style.display = 'block';
        e.preventDefault();
      });
      grip.addEventListener('pointermove', e => {
        if (!d) return;
        let s = d.s + ((e.clientX - d.x) / W + (e.clientY - d.y) / H) / 2;
        s = clamp(s, MIN, MAX);
        [1].concat(PRESETS).forEach(p => { if (Math.abs(s - p) < 0.03) s = p; });
        set(s);
        tip.textContent = Math.round(W * scale) + ' × ' + Math.round(H * scale) + ' · ' + Math.round(scale * 100) + '%';
      });
      const end = () => { d = null; grip.classList.remove('on'); tip.style.display = 'none'; };
      grip.addEventListener('pointerup', end);
      grip.addEventListener('pointercancel', end);
      grip.addEventListener('dblclick', () => set(1));
    });

    window.addEventListener('resize', refit);

    // raccourcis de Settings : clic = taille prédéfinie suivante ; le texte affiche la taille réelle
    function bindPill(btn, label) {
      if (!btn) return;
      const l = label || btn;
      labels.push(l); l.textContent = Math.round(scale * 100) + '%';
      btn.addEventListener('click', () => {
        const nx = PRESETS.find(p => p > scale + 0.005);
        set(nx == null ? PRESETS[0] : nx);
      });
    }

    refit();
    return { get: () => scale, set, refit, bindPill };
  }

  const api = { attach };
  if (typeof module !== 'undefined' && module.exports) module.exports = api; else g.NovaResize = api;
})(typeof window !== 'undefined' ? window : this);
