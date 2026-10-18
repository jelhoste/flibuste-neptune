# NOVA — série d'interfaces (maquettes HTML → DPF + NanoVG / CLAP)

Archive de passage de relais, version à jour. Les maquettes HTML sont la **référence visuelle et comportementale** ;
les données y sont simulées. Aucune ligne de C++ / NanoVG n'a encore été écrite ni compilée pour ces UI.

## 1. Contenu

```
nova-ui-series/
├─ README.md                               ce fichier (vue d'ensemble + plugin Transient Shaper)
├─ ui/                                     tout ce qui s'ouvre dans un navigateur (liens relatifs : ne pas séparer)
│  ├─ nova_chorus_ui.html                  ★ NOUVEAU : chorus, 3 pages (Chorus / Tone / Settings), presets dont Juno-60
│  ├─ nova_transient_shaper_ui.html        ★ NOUVEAU : transient shaper, 3 pages (Shaper / Sidechain / Settings)
│  ├─ nova_lowlatconv_ui.html              convolution à basse latence, 3 pages (Convolution / Equalizer / Settings)
│  ├─ nova_compressor_full_ui.html         compresseur NOVA
│  ├─ nova_equalizer_ui.html               égaliseur NOVA
│  ├─ nova_hammond_console_ui.html         orgue Hammond NOVA
│  ├─ nova-controls.css                    identités de contrôles : toggles, curseurs, pills (glass / bezel / hc)
│  ├─ nova-contrast.js                     réglage de contraste (niveaux 0–4), algorithme portable en C++
│  ├─ nova-scrollbar.js                    barre de défilement thémée + spécification NanoVG en en-tête
│  ├─ nova-resize.js                       ★ NOUVEAU : poignée de coin (redimensionnement à rapport d'aspect constant) + spécification DPF / NanoVG
│  ├─ nova-help.js                         ★ NOUVEAU : boutons « ? » + panneau d'aide partagés (modes ALWAYS / ON HOVER / OFF) + spécification NanoVG
│  ├─ build_help.py                        injecte un fichier NOVA_<Plugin>_help_*.md dans une UI (entre /*HELP_BEGIN*/ et /*HELP_END*/)
│  ├─ nova-knob-styles.js                  rendus de potards : "led" (anneau) et "pot" (potard physique)
│  ├─ nova-themes.css                      jetons partagés des thèmes Clair et Haut contraste (documentation)
│  └─ test_nova_contrast.js                test automatique des rapports de contraste (node, sans dépendance)
└─ docs/
   ├─ NOVA_TransientShaper_params_v0.2.md  ★ description des paramètres du shaper (format générique + textes d'aide)
   ├─ NOVA_Compressor_params_v0.1.md       ★ paramètres et traitement du compresseur
   ├─ NOVA_Equalizer_params_v0.1.md        ★ paramètres et traitement de l'égaliseur (16 bandes)
   ├─ NOVA_Hammond_params_v0.1.md          ★ paramètres et traitement du Hammond / Leslie
   ├─ NOVA_Chorus_params_v0.1.md, NOVA_Delay_params_v0.1.md   paramètres du chorus et du delay
   ├─ NOVA_Common_modules_v0.1.md          ★ modules communs : vu-mètres meter_* (19 paramètres) et sidechain sc_*
   ├─ README_LowLatConv.md                 README détaillé du plugin de convolution (pages, vu-mètres, design system, NanoVG)
   ├─ NOVA_LowLatConv_BRIEF_DSP_reponses.md  réponses du DSP de convolution
   └─ LowLatConv_brief_DSP.txt             brief d'origine posé au DSP de convolution
```

**Ouvrir une maquette** : double-clic sur le `.html` (Chrome / Edge / Firefox récents). Thème : bouton soleil de l'en-tête ;
contraste : pill « − ◐ 0 + » à sa gauche.
**Tester le contraste** : `cd ui && node test_nova_contrast.js nova_transient_shaper_ui.html` (« Tous les contrôles passent »).
Les cinq UI passent le test à la date de l'archive.

Le socle (gabarit 1160 × 780, jetons, identités de contrôles, contraste, barre de défilement, potards) est décrit dans
`docs/README_LowLatConv.md` §2 et §8 ; il est **commun à toutes les UI**, shaper compris. Rien n'y a été changé pour le shaper.

---

## 2. Transient Shaper (large bande) — ce qu'il faut savoir

Méthode suivie : UI descriptive d'abord, puis code. Le fichier `docs/NOVA_TransientShaper_params_v0.2.md` est **la référence
des paramètres** (ids, plages, défauts, échelles, automation, dépendances, textes d'aide). La maquette en dérive ; en cas
de divergence, c'est ce fichier qui fait foi, sauf mention contraire ci-dessous.

### 2.1 Principe de traitement (résumé ; détail et formules dans le fichier de paramètres §1)
Deux suiveurs d'enveloppe (rapide / lent) lisent la clé de détection, en dB. Différence positive = attaque, négative = sustain.
Gain = RANGE × (attack_gain × A' + sustain_gain × S') × w, lissé par SHAPE SMOOTHING. `w` est la pondération du réglage LEVEL
(−100…+100 %, deux sens, seuil et coude). Un plancher de silence reste actif. Les trois temps visibles sont ATTACK TIME,
SUSTAIN TIME et SHAPE SMOOTHING (−50…+50 % relatif au mode auto, 0 = auto, mapping géométrique ×2^(v/50)).
La maquette exécute cet algorithme en JavaScript (8 kHz, 1,6 s de batterie simulée) : `recompute()` dans le script.

### 2.2 Pages
- **SHAPER** : colonne gauche IN / **GAIN bipolaire** (deux barres A et S, ±RANGE, remplace le LRA) / OUT ; panneau central avec
  bouton de vue **WAVE** (signal, sortie, enveloppes rapide et lente, gain appliqué rempli au-dessus/au-dessous de l'axe, curseur de
  lecture) ou **RESPONSE** (courbe LEVEL, poignée ronde = seuil, deux carrés = coude, glissables) ; bouton de jeu (SET 1 / SET 2, actif
  en Dual Mono et M-S) ; rangée de 6 mini-contrôles (ATTACK TIME, SUSTAIN TIME, SHAPE SMOOTHING, RANGE, KNEE, LINK) ; 6 potards
  (ATTACK, SUSTAIN, LEVEL, THRESHOLD, OUTPUT, MIX) ; barre du bas : A/B, mode des canaux, DELTA, latence, LOOKAHEAD, OVERSAMPLING.
  KNEE et THRESHOLD sont atténués quand LEVEL = 0 ; LINK quand le mode n'est pas Stereo.
- **SIDECHAIN** : module standard du compresseur **sans** DETECT / RMS WINDOW (l'intégration est masquée : le shaper a ses suiveurs).
  Source Internal / External / Mix, gains, phase, LISTEN, interrupteur SIDECHAIN (désactivé = la clé est le signal propre, l'EQ s'applique
  toujours), EQ (HP, LP, 3 bandes) avec analyseur de spectre débrayable (simulé ici).
- **SETTINGS** : sections GENERAL, AUDIO / DSP, DETECTORS, METERING, SC ANALYZER, PRESETS, INTERACTION, ABOUT. DETECTORS contient
  le mode Peak/RMS, le rapport rapide/lent et les temps dérivés (lecture seule). Ces réglages changent le son : ce sont des
  **paramètres du plugin non automatisables, enregistrés avec les presets**, simplement affichés dans Settings.
- Hauteurs de conteneurs identiques sur les 3 pages (règle validée).

### 2.3 Aide en ligne (nouveau, à reprendre en NanoVG)
Bouton « ? » discret dans chaque carte de potard, chaque mini-contrôle, la barre du bas et certaines lignes de Settings.
Un clic ouvre un panneau (titre, id du paramètre, `help_short` en accent, `help_long`, ligne « Plage · Défaut · Automatisable »)
fermé par clic extérieur, ✕ ou Échap. Réglage « Help buttons » dans Settings : ALWAYS / ON HOVER / OFF.
Les textes viennent du fichier de paramètres (`help_short`, `help_long`) : source unique pour la maquette et le code.
Transposition NanoVG : panneau dessiné en dernier dans `onDisplay` (au-dessus de tout), texte avec `nvgTextBox` (retour à la ligne),
couleurs lues dans les jetons du thème, taille suivant l'échelle de l'interface, hit-test comme un bouton ordinaire.
Les chaînes sont à stocker par langue (le réglage Language est prévu dans Settings).

### 2.4 Points ouverts pour la discussion code / DSP
1. **Transport des enveloppes DSP → UI** (signal, enveloppes, gain appliqué pour le panneau central) : à décider. L'UI DPF n'a pas accès
   à l'audio ; pistes : tampon partagé si UI et DSP sont dans le même binaire, ou séries sous-échantillonnées exposées par le DSP (à voir
   comme le fait déjà le compresseur). Sorties de lecture seule prévues : `shp_gain_attack_db`, `shp_gain_sustain_db` + module `meter_*`.
2. **Formules proposées, à valider au code** : mode auto du SHAPE SMOOTHING (max(1 ms, 0,3 × plus court des deux temps), borné à 50 ms) ;
   échelle de détection (12 dB d'écart entre suiveurs = effet maximal) ; coude centré sur le seuil ; suiveur rapide = temps lent ÷ rapport
   (attaque minimale 0,05 ms).
3. **Exposition à l'automation** : jeux 1 et 2 complets, ou jeu 1 seul en mode Stereo comme le compresseur.
4. Abandonné : PEAK SAFE. Gardés : OVERSAMPLING (None / 2x / 4x comme le compresseur, 4x par défaut), LOOKAHEAD 0–10 ms (latence reportée).
5. Le module sidechain (`sc_*`) et les vu-mètres (`meter_*`, 19 paramètres) sont ceux déjà définis pour la série ; seuls les ids du shaper (`shp_*`)
   sont décrits dans le fichier de paramètres.

### 2.5 Limites connues de la maquette
- Le signal, les enveloppes, les niveaux IN/OUT et l'analyseur de spectre du sidechain sont **simulés** ; le panneau WAVE recalcule l'effet
  à chaque réglage (comportement indicatif, pas la précision d'un DSP à 48 kHz).
- Le preset, A/B, le choix de langue, la plupart des réglages de Settings et les contrôles du sidechain externe ne pilotent rien.
- Le premier descriptif (`NOVA_TransientShaper_spec.md`, v0.1) est **remplacé** par la v0.2 et n'est pas inclus.

---

### 2.6 Redimensionnement par la poignée de coin (toutes les UI)
Poignée en bas à droite de chaque cadre : on la glisse pour agrandir ou réduire l'interface (50 % → 200 %, rapport d'aspect conservé,
accroche à 100 % et aux raccourcis, bulle « largeur × hauteur · % » pendant le glissement, double-clic = 100 %). Le réglage
« Interface scale » de Settings est conservé : il propose 75 / 100 / 125 / 150 % et affiche la taille réelle (ex. « 115% »).
La taille est mémorisée (localStorage dans la maquette ; dans l'état du plugin en vrai). Spécification DPF / NanoVG / CLAP en en-tête de
`ui/nova-resize.js` (ResizeHandle de DGL à vérifier dans la version de DPF utilisée, `setGeometryConstraints`, `can_resize` / `adjust_size` en CLAP).

### 2.7 Aide en ligne sur les autres UI (nouveau)
Le « ? » n'existe que sur les contrôles qui méritent une explication (pas THRESHOLD, RATIO, ATTACK, RELEASE, MAKEUP, MIX…). Textes dans
`docs/NOVA_<Plugin>_help_v0.1.md` (même format que le fichier de paramètres du shaper : `help_short`, `help_long`, `meta`), injectés par
`ui/build_help.py`. Module commun `ui/nova-help.js` (`NovaHelp.init({root})`, `NovaHelp.bindPill(...)` pour le réglage « Help buttons »,
mémorisé sous la clé `nova-help`). Le shaper garde pour l'instant sa propre copie du même mécanisme.
- Convolution : INPUT, STRETCH, AUTO TRIM, BEGIN, END, AUTO LEN, ATTACK/DECAY de l'IR, NORM, PRE-DELAY, titre de l'EQ de sortie (wet uniquement).
- Compresseur : FEEDFORWARD/FEEDBACK, SHAPE d'attaque et de relâchement, mode de canaux, LOOKAHEAD, OVERSAMPLING, source du sidechain, DETECT/RMS WINDOW, point de mesure du spectre (Settings).
- EQ : type de bande (et pentes HP/LP), DYNAMIC, mode de canaux, OVERSAMPLING, point de mesure du spectre (Settings).
- Hammond : DRAWBARS, PERCUSSION (HARMONIC / DECAY / VOLUME), Leslie CHORALE / TREM / BRK (BRK = break : la Leslie s'arrête de tourner), SPLIT + TRANSP, matrice GRILLE, BANK / PRESET.
Point à confirmer avec le DSP : le sens exact de EXP / LOG pour les formes d'attaque et de relâchement du compresseur (texte volontairement prudent).
Le Hammond n'a pas de réglage « Help buttons » (sa page Settings est vide dans la maquette) : il suit la valeur mémorisée par les autres UI.

### 2.8 Chorus (nouveau) — `ui/nova_chorus_ui.html`, `docs/NOVA_Chorus_params_v0.1.md`
Chorus stéréo standard et paramétrable (RATE, DEPTH relatif au retard central, DELAY, WIDTH = déphasage G/D du LFO, FEEDBACK, MIX, OUTPUT ; mini-contrôles SYNC / DIVISION /
SHAPE / VOICES ; page TONE : LOW CUT, HIGH CUT, caractère BBD sur la voie traitée uniquement). **Le son du chorus du Juno-60 (celui du TAL-Chorus-LX) n'est pas un mode** :
ce sont trois presets d'usine (« Juno-60 I », « II », « I+II ») dans un menu de presets déroulant. Les valeurs sont une approximation à caler à l'écoute et par mesure.
Le graphique central a deux vues : DELAY (retard gauche / droite au cours du temps, point animé) et RESPONSE (enveloppe min–max du filtre en peigne sur un cycle de LFO).
Un seul paramètre de sortie d'affichage suffit : `cho_lfo_phase_out` (phase du LFO). Même socle que les autres UI (thèmes, contraste, poignée, `nova-help.js`).
Points à valider : voir §7 du fichier de paramètres.

---

## 3. Autres plugins

Convolution : voir `docs/README_LowLatConv.md` (pages, vu-mètres, chargement de l'IR, passage à NanoVG, décisions ouvertes).
Compresseur, égaliseur, orgue Hammond : maquettes inchangées dans leur contenu, migrées sur le même socle (jetons, contrôles, contraste,
barre de défilement).

### 2.9 Delay multi-lignes (`ui/nova_delay_ui.html`)
8 lignes (TAP 1–8) : ON/SOLO, temps libre ou synchro, niveau, pan, feedback, pitch, filtres, inversion de phase, modulation (depth/rate), décalage temporel R en ms. Globaux : routage parallèle/cascade, ping-pong, ducking, drive, freeze, width. Pages DELAY / TAPS / GLOBAL / SETTINGS. Paramètres : `docs/NOVA_Delay_params_v0.1.md`.
