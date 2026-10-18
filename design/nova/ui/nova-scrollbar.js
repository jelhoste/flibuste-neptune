/*
  NOVA — Barre de défilement partagée
  =============================================================
  Remplace la barre native du navigateur par une barre aux couleurs du
  thème. Elle se compose d'un rail (.nsb) et d'une poignée (.nst) placés à
  côté de la zone qui défile ; le défilement reste celui du navigateur
  (molette, tactile, clavier), seule la barre est redessinée.

  STRUCTURE HTML (voir l'UI de convolution) :
    <div class="scw">                       <- conteneur, position:relative
      <div class="sc" id="sc"></div>        <- zone qui défile (overflow-y:auto,
                                               scrollbar-width:none, padding-right)
      <div class="nsb" id="scBar"><div class="nst"></div></div>
    </div>
    NovaScroll.attach(document.getElementById('sc'), document.getElementById('scBar'));

  La barre se masque toute seule quand il n'y a rien à faire défiler, et se
  met à jour quand le contenu ou la taille change (ResizeObserver).
  Elle fonctionne aussi sous `zoom` CSS (le glisser est corrigé du facteur).

  -------------------------------------------------------------
  SPÉCIFICATION POUR NANOVG (même rendu dans le plugin)
  -------------------------------------------------------------
  Rail    : largeur 12 px, rayon --sbr, fond --ov, contour 1 px --pb
  Poignée : largeur 6 px (2 px de marge), longueur = rail * visible/total,
            minimum 28 px, rayon --sbr ; couleur --text-dim ;
            survol --accent-2 ; glissement --accent
  Comportement : glisser la poignée, clic sur le rail = page précédente /
            suivante (90 % de la hauteur visible), molette = défilement,
            rail masqué si total <= visible.
  Thème haut contraste : rail noir, contour 2 px blanc, poignée blanche.
  =============================================================
*/
(function (g) {
  'use strict';
  function attach(el, bar, opt) {
    opt = opt || {};
    const minThumb = opt.minThumb || 28, th = bar.firstElementChild;
    let drag = null;
    const scale = () => { const r = el.getBoundingClientRect(); return el.offsetHeight ? r.height / el.offsetHeight : 1; };
    function update() {
      const ch = el.clientHeight, sh = el.scrollHeight, need = sh > ch + 1;
      bar.classList.toggle('on', need);
      if (!need) return;
      const track = bar.clientHeight, len = Math.min(track, Math.max(minThumb, Math.round(track * ch / sh))), max = sh - ch;
      th.style.height = len + 'px';
      th.style.transform = 'translateY(' + (max ? el.scrollTop / max * (track - len) : 0) + 'px)';
    }
    el.addEventListener('scroll', update, { passive: true });
    if (g.ResizeObserver) { const ro = new ResizeObserver(update); ro.observe(el); Array.prototype.forEach.call(el.children, c => ro.observe(c)); }
    new MutationObserver(update).observe(el, { childList: true, subtree: true });
    g.addEventListener('resize', update);
    th.addEventListener('pointerdown', e => {
      e.preventDefault(); th.setPointerCapture(e.pointerId);
      const track = bar.clientHeight, len = th.offsetHeight;
      drag = { y: e.clientY, top: el.scrollTop, k: (el.scrollHeight - el.clientHeight) / Math.max(1, track - len), s: scale() };
      bar.classList.add('drag');
    });
    th.addEventListener('pointermove', e => { if (drag) el.scrollTop = drag.top + (e.clientY - drag.y) / drag.s * drag.k; });
    const end = () => { drag = null; bar.classList.remove('drag'); };
    th.addEventListener('pointerup', end); th.addEventListener('pointercancel', end);
    bar.addEventListener('pointerdown', e => {
      if (e.target !== bar) return;
      el.scrollBy({ top: (e.clientY < th.getBoundingClientRect().top ? -1 : 1) * el.clientHeight * 0.9, behavior: 'smooth' });
    });
    update();
    return { update };
  }
  g.NovaScroll = { attach };
})(typeof window !== 'undefined' ? window : this);
