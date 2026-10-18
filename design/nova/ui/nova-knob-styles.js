/*
  NOVA — Styles de potards partagés
  =============================================================
  Chaque "style" est une fonction pure qui dessine un potard sur un
  contexte canvas 2D déjà positionné (aucune lecture de variable CSS
  ici : les couleurs sont fournies toutes faites par l'appelant, ce
  qui rend ce fichier indépendant du nommage de variables de chaque
  plugin).

  UTILISATION DANS UNE UI
  -------------------------------------------------------------
    <script src="nova-knob-styles.js"></script>

    function drawKnob(entry){
      ...calculer cx, cy, radius, angleStart, angleEnd, angle, n...
      const styleName = THEME_KNOB_STYLE[currentTheme] || 'led';
      const renderer = NovaKnobStyles.get(styleName);
      renderer(ctx, { cx, cy, radius, angleStart, angleEnd, angle, n, colors });
    }

  Chaque thème choisit un style par son nom (ex. "led" ou "pot") et
  fournit les couleurs attendues par ce style (voir plus bas). Rien
  n'empêche un thème de définir son propre style au lieu de choisir
  parmi les styles fournis :

    NovaKnobStyles.register('mon-style', function(ctx, o){
      // dessiner o.cx / o.cy / o.radius / o.angleStart / o.angleEnd
      // / o.angle / o.n avec les couleurs de o.colors
    });

  -------------------------------------------------------------
  STYLE "led" — anneau LED avec dégradé d'intensité (NOVA Compresseur)
  colors attendues :
    face   : couleur du disque plat central
    track  : couleur de la piste de fond (rgba conseillé)
    dot    : couleur du point de repère en bout d'arc
    base1  : couleur de départ du dégradé de l'arc rempli (valeur basse)
    base2  : couleur de départ du dégradé, côté clair (valeur basse)
    hot1   : couleur d'arrivée du dégradé à pleine valeur
    hot2   : couleur d'arrivée du dégradé à pleine valeur, côté clair

  OPTION BIPOLAIRE (les deux styles) : o.angleFrom (facultatif)
    Angle d'où part l'arc rempli (par défaut angleStart). Pour un potard
    bipolaire, passer le milieu de la course : l'arc part du centre et va
    vers la gauche ou la droite selon le signe. Passer aussi n = intensité
    (0 au centre, 1 en butée) pour le dégradé du style "led".

  STYLE "pot" — potentiomètre physique avec pointeur (NOVA Hammond)
  colors attendues :
    face1, face2, face3 : dégradé radial du corps du potard (clair → sombre)
    ring    : contour du corps
    accent  : couleur de l'arc indicateur de position
    pointer : couleur du pointeur triangulaire
    center  : couleur du cabochon central
  =============================================================
*/
const NovaKnobStyles = (function(){
  const styles = {};

  function register(name, fn){ styles[name] = fn; }
  function get(name){ return styles[name] || styles.led || null; }

  function mixRGB(hex1, hex2, t){
    hex1 = (hex1 || '#000000').replace('#','');
    hex2 = (hex2 || '#000000').replace('#','');
    const r1=parseInt(hex1.substring(0,2),16), g1=parseInt(hex1.substring(2,4),16), b1=parseInt(hex1.substring(4,6),16);
    const r2=parseInt(hex2.substring(0,2),16), g2=parseInt(hex2.substring(2,4),16), b2=parseInt(hex2.substring(4,6),16);
    return [
      Math.round(r1+(r2-r1)*t),
      Math.round(g1+(g2-g1)*t),
      Math.round(b1+(b2-b1)*t)
    ];
  }
  function rgbStr(rgb){ return `rgb(${rgb[0]},${rgb[1]},${rgb[2]})`; }
  function rgbaStr(rgb, a){ return `rgba(${rgb[0]},${rgb[1]},${rgb[2]},${a})`; }

  function drawLED(ctx, o){
    const {cx, cy, radius, angleStart, angleEnd, angle, n, colors} = o;

    // corps plat
    ctx.beginPath(); ctx.arc(cx,cy,radius*0.72,0,Math.PI*2);
    ctx.fillStyle = colors.face; ctx.fill();

    // piste de fond
    ctx.beginPath(); ctx.arc(cx,cy,radius,angleStart,angleEnd,false);
    ctx.strokeStyle = colors.track; ctx.lineWidth = 5; ctx.stroke();

    // dégradé d'intensité : de base1/base2 (valeur basse) vers hot1/hot2 (pleine valeur)
    const c1 = mixRGB(colors.base1, colors.hot1, n);
    const c2 = mixRGB(colors.base2, colors.hot2, n);

    // halo en couches
    const a0 = (o.angleFrom==null) ? angleStart : o.angleFrom, ccw = angle < a0;
    for(let i=3;i>=1;i--){
      ctx.beginPath(); ctx.arc(cx,cy,radius,a0,angle,ccw);
      ctx.strokeStyle = rgbaStr(c1, 0.07*i);
      ctx.lineWidth = 5+i*3; ctx.lineCap='round'; ctx.stroke();
    }
    // arc rempli
    ctx.beginPath(); ctx.arc(cx,cy,radius,a0,angle,ccw);
    const grad = ctx.createLinearGradient(cx-radius,cy,cx+radius,cy);
    grad.addColorStop(0, rgbStr(c1)); grad.addColorStop(1, rgbStr(c2));
    ctx.strokeStyle = grad; ctx.lineWidth = 5; ctx.lineCap='round'; ctx.stroke();

    // point de repère en bout d'arc
    const ix = cx+Math.cos(angle)*(radius-1), iy = cy+Math.sin(angle)*(radius-1);
    ctx.beginPath(); ctx.arc(ix,iy,2.6,0,Math.PI*2); ctx.fillStyle = colors.dot; ctx.fill();
  }

  function drawPot(ctx, o){
    const {cx, cy, radius, angleStart, angleEnd, angle, colors} = o;

    // corps du potard (dégradé radial)
    const g = ctx.createRadialGradient(cx-3,cy-4,2, cx,cy,radius);
    g.addColorStop(0, colors.face1); g.addColorStop(0.55, colors.face2); g.addColorStop(1, colors.face3);
    ctx.beginPath(); ctx.arc(cx,cy,radius,0,Math.PI*2);
    ctx.fillStyle = g; ctx.fill();
    ctx.strokeStyle = colors.ring; ctx.lineWidth = 1.5; ctx.stroke();

    // arc indicateur de position
    const a0 = (o.angleFrom==null) ? angleStart : o.angleFrom;
    ctx.beginPath(); ctx.arc(cx,cy,radius-5,a0,angle,angle<a0);
    ctx.strokeStyle = colors.accent; ctx.lineWidth = 2.4; ctx.lineCap='round'; ctx.stroke();

    // pointeur triangulaire
    ctx.save();
    ctx.translate(cx,cy); ctx.rotate(angle);
    ctx.beginPath();
    ctx.moveTo(radius-7,0); ctx.lineTo(radius-14,-2.2); ctx.lineTo(radius-14,2.2);
    ctx.fillStyle = colors.pointer; ctx.fill();
    ctx.restore();

    // cabochon central
    ctx.beginPath(); ctx.arc(cx,cy,4.5,0,Math.PI*2);
    ctx.fillStyle = colors.center; ctx.fill();
  }

  register('led', drawLED);
  register('pot', drawPot);

  return { register, get, styles, mixRGB, rgbStr, rgbaStr };
})();
