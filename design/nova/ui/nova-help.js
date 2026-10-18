/*
  NOVA — Aide en ligne « ? » (partagée par toutes les UI)
  =============================================================
  Un petit bouton « ? » à côté des seuls contrôles qui méritent une explication. Un clic ouvre un panneau
  (titre, phrase courte, explication, plage/valeurs) ; le survol du bouton affiche la phrase courte.
  Le panneau se ferme par clic ailleurs, Échap ou ✕.

  UTILISATION
    <script>window.NOVA_HELP = {...}</script>   (généré depuis NOVA_<Plugin>_help_*.md par build_help.py)
    <script src="nova-help.js"></script>
    Dans le HTML : <button class="hb" data-h="comp_lookahead">?</button> (ou data-hid sur un conteneur : le bouton est ajouté)
    NovaHelp.init({root: appElement});   // ou une liste de cadres (un panneau par cadre)
    NovaHelp.bindPill(pillEl, labelEl)   // réglage Settings « Help buttons » : ALWAYS / ON HOVER / OFF (mémorisé, clé nova-help)

  Couleurs : jetons de chaque UI avec repli (--bg-card|--bakelite, --ctl-bd|--border|--panel-border, --text, --accent…).
  Haut contraste : bordure blanche 2 px, sans ombre.

  PORTAGE DPF / NANOVG
    - Bouton : disque 15 px, « ? » centré ; zone de survol 20 px. Panneau : rectangle 300 px de large dessiné au-dessus de la scène
      (dernier widget), coin supérieur droit aligné sur le bouton, replié dans la fenêtre. Texte : nvgTextBox avec la police embarquée.
    - Textes : un tableau {id → label, short, long, meta} compilé depuis les fichiers .md (pas de chargement à l'exécution).
    - Mode d'affichage (always / hover / off) : réglage UI, mémorisé dans l'état du plugin.
*/
(function(){
'use strict';
var css=
'.hb{width:15px;height:15px;border-radius:50%;border:1px solid currentColor;background:transparent;color:var(--text-dim,#aaa);font-size:9px;font-weight:700;line-height:1;font-family:inherit;padding:0;cursor:pointer;display:inline-flex;align-items:center;justify-content:center;flex-shrink:0;transition:opacity .15s;vertical-align:middle;margin-left:6px;position:relative;z-index:2}'+
'.hb:hover,.hb.on{color:var(--accent-2,var(--accent,#c97b4a))}'+
'.hb.c{position:absolute;top:4px;right:4px;margin:0}'+
'[data-help=hover] .hb{opacity:0}'+
'[data-help=hover] :hover>.hb,[data-help=hover] .hb:focus-visible,[data-help=hover] .hb.on{opacity:1}'+
'[data-help=off] .hb{display:none}'+
'[data-theme=hc] .hb,html.theme-hc .hb{border:2px solid #fff;color:#fff;background:#000}'+
'.hp{position:absolute;display:none;z-index:200;width:300px;box-sizing:border-box;text-align:left;font-family:inherit;font-weight:400;letter-spacing:0;text-transform:none;background:var(--bg-card,var(--bakelite,#262626));border:1px solid var(--ctl-bd,var(--border,var(--panel-border,#777)));border-top-color:var(--accent,#c97b4a);border-radius:8px;padding:12px 14px;box-shadow:0 14px 36px rgba(0,0,0,.5);color:var(--text,#fff)}'+
'[data-theme=hc] .hp,html.theme-hc .hp{border:2px solid #fff;box-shadow:none;background:#000}'+
'.hp .hh{display:flex;align-items:center;gap:8px}.hp .hl{font-size:12px;font-weight:700;letter-spacing:.8px}'+
'.hp .hi{font-size:9px;color:var(--text-dim2,#888);font-family:ui-monospace,Menlo,Consolas,monospace}'+
'.hp .hx{margin-left:auto;cursor:pointer;color:var(--text-dim,#aaa);font-size:12px;padding:0 2px}.hp .hx:hover{color:var(--text,#fff)}'+
'.hp .hsx{font-size:11px;color:var(--accent-2,var(--accent,#d99566));margin-top:6px;line-height:1.4}'+
'.hp .hbd{font-size:11.5px;line-height:1.5;margin-top:6px}'+
'.hp .hm{font-size:9.5px;color:var(--text-dim2,#888);margin-top:8px;padding-top:6px;border-top:1px solid var(--ctl-bd,var(--border,#555));line-height:1.5}';
var st=document.createElement('style');st.textContent=css;document.head.appendChild(st);

var H=window.NOVA_HELP||{},roots=[],root,hp,cur=null,MODES=['always','hover','off'],LBL=['ALWAYS','ON HOVER','OFF'];
function esc(t){return String(t).replace(/[&<>]/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;'}[c]})}
function close(){roots.forEach(function(r){r.hp.style.display='none'});if(cur)cur.classList.remove('on');cur=null}
function open(btn){
  var id=btn.dataset.h,h=H[id];if(!h)return;
  if(cur===btn){close();return}
  close();cur=btn;btn.classList.add('on');
  var R=roots.filter(function(r){return r.el.contains(btn)})[0]||roots[0];root=R.el;hp=R.hp;
  hp.innerHTML='<div class="hh"><span class="hl">'+esc(h.label)+'</span><span class="hi">'+esc(id)+'</span><span class="hx" title="Fermer">✕</span></div><div class="hsx">'+esc(h.short)+'</div><div class="hbd">'+esc(h.long)+'</div>'+(h.meta?'<div class="hm">'+esc(h.meta)+'</div>':'');
  hp.style.display='block';hp.querySelector('.hx').onclick=close;
  var rr=root.getBoundingClientRect(),br=btn.getBoundingClientRect(),z=(rr.width/root.offsetWidth)||1;
  var bx=(br.left-rr.left)/z,by=(br.top-rr.top)/z,bw=br.width/z,bh=br.height/z,w=hp.offsetWidth,hh=hp.offsetHeight;
  var top=by+bh+6;if(top+hh>root.offsetHeight-8)top=Math.max(8,by-hh-6);
  var left=Math.max(8,Math.min(bx+bw-w,root.offsetWidth-w-8));
  hp.style.left=left+'px';hp.style.top=top+'px';
}
function scan(scope){
  (scope||document).querySelectorAll('[data-hid]').forEach(function(el){
    if(el.querySelector(':scope>.hb'))return;
    var b=document.createElement('button');b.className='hb'+(el.dataset.hpos=='corner'?' c':'');b.dataset.h=el.dataset.hid;b.textContent='?';b.type='button';
    if(el.dataset.hpos=='corner'&&getComputedStyle(el).position=='static')el.style.position='relative';
    el.appendChild(b);
  });
  (scope||document).querySelectorAll('.hb').forEach(function(b){var h=H[b.dataset.h];if(h){b.title=h.short;b.setAttribute('aria-label','Aide : '+h.label)}else b.style.display='none'});
}
function setMode(i){
  document.documentElement.setAttribute('data-help',MODES[i]);
  try{localStorage.setItem('nova-help',MODES[i])}catch(e){}
  close();
}
function getMode(){var m='always';try{m=localStorage.getItem('nova-help')||m}catch(e){}var i=MODES.indexOf(m);return i<0?0:i}
window.NovaHelp={
  init:function(o){
    var els=o.root.nodeType?[o.root]:Array.prototype.slice.call(o.root);
    els.forEach(function(el){if(getComputedStyle(el).position=='static')el.style.position='relative';var p=document.createElement('div');p.className='hp';el.appendChild(p);roots.push({el:el,hp:p})});
    scan();setMode(getMode());
    var q=0;new MutationObserver(function(){if(q)return;q=1;requestAnimationFrame(function(){q=0;scan()})}).observe(document.body,{childList:true,subtree:true});   // contrôles recréés dynamiquement
    document.addEventListener('click',function(e){var b=e.target.closest('.hb');if(b){e.stopPropagation();open(b);return}if(!e.target.closest('.hp'))close()});
    document.addEventListener('keydown',function(e){if(e.key=='Escape')close()});
  },
  scan:scan,close:close,
  bindPill:function(pill,label){
    var l=label||pill,i=getMode();l.textContent=LBL[i];
    pill.addEventListener('click',function(){i=(i+1)%3;l.textContent=LBL[i];setMode(i)});
  }
};
})();
