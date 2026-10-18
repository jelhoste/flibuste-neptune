# NOVA — UI du plugin de convolution à basse latence (LowLatConv)

Archive de passage de relais : maquettes HTML validées de l'interface, socle de design partagé
(jetons, thèmes, contrôles, contraste, barre de défilement), et tout ce qu'il faut pour porter
l'UI en **DPF + NanoVG** (format CLAP). Le DSP et les aspects DPF ont été traités dans une autre
discussion ; ce document décrit **ce que l'UI doit faire et comment elle est dessinée**.

> Les maquettes HTML sont la **référence visuelle et comportementale**. Les données y sont
> simulées (IR, spectre, niveaux). Aucune ligne de C++ / NanoVG n'a encore été écrite ni compilée.

---

## 1. Contenu de l'archive

```
nova-ui-series/
├─ README.md                           ce fichier
├─ ui/                                 tout ce qui est ouvrable dans un navigateur (liens relatifs : ne pas séparer)
│  ├─ nova_lowlatconv_ui.html          ★ UI de convolution : 3 pages (Convolution / Equalizer / Settings)
│  ├─ nova_compressor_full_ui.html     compresseur NOVA (base de la convolution)
│  ├─ nova_equalizer_ui.html           égaliseur NOVA
│  ├─ nova_hammond_console_ui.html     orgue Hammond NOVA
│  ├─ nova-controls.css                identités de contrôles : toggles, curseurs, pills (glass / bezel / hc)
│  ├─ nova-contrast.js                 réglage de contraste (niveaux 0–4), algorithme portable en C++
│  ├─ nova-scrollbar.js                barre de défilement thémée + spécification NanoVG en en-tête
│  ├─ nova-knob-styles.js              rendus de potards : "led" (anneau) et "pot" (potard physique)
│  ├─ nova-themes.css                  jetons partagés des thèmes Clair et Haut contraste (documentation)
│  └─ test_nova_contrast.js            test automatique des rapports de contraste (node, sans dépendance)
└─ docs/
   ├─ NOVA_LowLatConv_BRIEF_DSP_reponses.md   réponses du DSP (59 paramètres, state, vu-mètres, affichage de l'IR)
   └─ LowLatConv_brief_DSP.txt                le brief d'origine (questions posées au DSP)
```

Le fichier `LowLatConvParams.h` à jour (59 paramètres) est dans l'archive du DSP (`plugin/LowLatConvParams.h`) :
il fait foi pour les indices, plages et libellés. Ce README cite les indices de l'annexe de `docs/…reponses.md`.

**Ouvrir une maquette** : double-clic sur le `.html` (Chrome / Edge / Firefox récents). Les thèmes se changent
avec le bouton soleil de l'en-tête, le contraste avec la pill « − ◐ 0 + » à sa gauche.
**Tester le contraste** : `cd ui && node test_nova_contrast.js nova_lowlatconv_ui.html` (doit afficher « Tous les contrôles passent »).

---

## 2. Gabarit commun (identique sur les 3 pages)

Cadre 1160 × 780 px (la maquette applique `zoom` pour s'adapter à la fenêtre ; en NanoVG : `nvgScale`).

| Zone | Dimensions |
|---|---|
| En-tête | hauteur 58, fond `--header-bg-css`, bordure basse |
| Corps | grille `190px | 1fr` × `minmax(0,1fr) | 190px`, marges de 14 px entre conteneurs |
| Colonne gauche, haut | vu-mètres (pages 1 et 2) ou menu des sections (page 3) |
| Colonne gauche, bas | menu de navigation : CONVOLUTION / EQUALIZER / SETTINGS (162 px de haut) |
| Barre du bas | hauteur 56 |

**Règle validée** : les hauteurs des conteneurs du haut et du bas sont **identiques sur les 3 pages**
(mesurées : haut 462 px à y=140, bas 162 px à y=616). Ne pas laisser une page changer les rangées.

Rappel des identités de conteneurs : `glass` (rayon 14/18, ombres douces, fonds translucides) pour Sombre, Clair,
Haut contraste ; `bezel` (rayon 10/14, texture bois, liseré) uniquement pour le thème Hammond.

---

## 3. Page 1 — Convolution

### 3.1 En-tête
- **Pastille IR** (au centre) : icône dossier + nom du fichier + puce de statut (`NO IR` / `LOADING` / `LOADED` / `ERROR`).
  Un clic ouvre le sélecteur de fichier (voir §6). En erreur, le nom est affiché en rouge.
- **Pill de contraste** puis **bouton de thème** à droite (voir §8).

### 3.2 Panneau « IMPULSE RESPONSE »
- Bouton **WAVE / SPECTRUM** (une seule vue à la fois) + lecture « légende | durée » (`pIRLengthMs`).
- **WAVE** : enveloppe min/max par colonne (800 colonnes). Trace L = remplissage plein, trace R = contour fin
  (pour ne pas dépendre de la couleur). Axe temps en secondes, de 0 à la durée de l'IR, 6 graduations (5 intervalles égaux).
- **SPECTRUM** : module en dB, axe fréquence logarithmique 20 Hz → 20 kHz, 256 points, plage −100 → 0 dB.
  Trace L pleine avec remplissage, trace R **en pointillé** (NanoVG n'a pas de pointillé : tracer des segments).
- **Poignées Begin / End** (vue WAVE seulement) : lignes pointillées avec petit drapeau « B » / « E » en haut ;
  on les glisse directement. Zones hors [Begin, End] assombries. Begin ≤ 500 ms ; End = 0 → pas de coupe
  (poignée à l'extrémité droite). La valeur est appliquée **au relâchement**.
- Messages au centre quand il n'y a rien à dessiner : « Aucune réponse impulsionnelle chargée. » (statut 0) ou
  « Impossible de lire le fichier : vérifiez le chemin ou le format (.wav). » (statut 3).

### 3.3 Rangée des 10 réglages d'IR (sous le graphique)
Dix mini-groupes (titre 8 px, contrôle, valeur). **Non automatisables** ; chaque modification déclenche
une reconstruction de l'IR (statut Loading → Loaded). Begin/End ne sont **jamais grisés**, même avec auto-trim/auto-length.

| Contrôle UI | Paramètre (indice) | Type / plage / affichage |
|---|---|---|
| INPUT | `pInputMode` (28) | pill cyclique : STEREO, SUM, LEFT, RIGHT |
| STRETCH | `pStretch` (29) | curseur 50–200 %, affiché `100%` |
| AUTO TRIM | `pAutoTrim` (30) | toggle |
| BEGIN | `pBegin` (31) | curseur 0–500 ms |
| END | `pEnd` (32) | curseur 0–10 000 ms, `OFF` à 0 |
| AUTO LEN | `pAutoLen` (33) | toggle |
| ATTACK | `pAttack` (34) | curseur 0–500 ms |
| DECAY | `pDecay` (35) | curseur 0–60 dB |
| REVERSE | `pReverse` (36) | toggle |
| NORM | `pNorm` (37) | pill cyclique : OFF, PEAK, ENERGY |

### 3.4 Rangée de 6 potards (automatisables)
Carte de 60 px de potard, titre, valeur, bornes. Glisser vertical (0,006 par pixel de la plage normalisée ; ×0,3 en
sensibilité « FINE »), molette ±0,02, double-clic = valeur par défaut. Les arcs partent de 135° et finissent à 405°.

| Potard | Paramètre | Échelle / affichage |
|---|---|---|
| INPUT | `pInGain` (0) | −60…+30 dB, linéaire, `0.0 dB` |
| PRE-DELAY | `pPreDelay` (4) | 0…2000 ms, **non linéaire** : valeur = 2000 · n^2,5, `120 ms` |
| DRY | `pDry` (2) | −100…+30 dB linéaire ; **`-inf` quand ≤ −100** ; défaut −100 |
| WET | `pWet` (3) | idem DRY ; défaut 0 dB |
| WIDTH | `pWidth` (5) | 0…200 %, `100%` |
| OUTPUT | `pOutGain` (1) | −60…+30 dB |

DRY, WET et OUTPUT utilisent la variante « pink » du titre (couleur secondaire).

### 3.5 Barre du bas
- Bouton **A/B** (cycle A ↔ B). Recommandation : le jeu A/B couvre **tous** les paramètres, IR et chemin compris
  (le DSP regroupe les changements en une seule reconstruction avec fondu de 21 ms). Le DSP ne connaît pas l'A/B :
  c'est à l'UI de stocker les deux jeux.
- Pastille **LATENCY 0 smp** (constante) et, uniquement si le pré-délai ≥ 1 ms, pastille
  **« + PRE-DELAY X ms (WET) »** (le pré-délai n'est pas de la latence).
- À droite : `48 kHz · 2 ch` (à brancher sur `getSampleRate()`).

---

## 4. Page 2 — Equalizer (EQ de sortie, signal wet uniquement)

Panneau du haut : courbe de réponse (grille ±20 dB, fréquences 20 Hz–20 kHz log) avec :
- **Switch EQ** général (`pEqOn`, 6) et **switch IR SPECTRUM** (UI seulement).
- **Spectre de l'IR en fond** (normalisé en hauteur, rempli) + courbe pointillée « IR + EQ ».
- 5 points déplaçables : HP et LP (fréquence seule, sur la ligne 0 dB), bandes 1–3 (fréquence + gain, bornés à ±18 dB).
  Chaque point porte une étiquette (HP, 1, 2, 3, LP) ; point désactivé = creux. Déplacer un point **active** sa zone.
  Molette sur un point : Q de la bande (×1,08 / ×0,93, borné 0,1–10) ou pente HP/LP (12/24/48).
- ⚠ Les courbes de réponse de la maquette sont des **approximations d'affichage** (gaussienne, sigmoïde, pente en dB/oct).
  Pour que la courbe colle au DSP, calculer la vraie réponse des biquads.

Rangée du bas : 5 zones de même largeur — HIGH-PASS, BAND 1, BAND 2, BAND 3, LOW-PASS. En-tête de zone :
titre, bouton cyclique (type ou pente), switch ON. Corps : potards GAIN et FREQ (bandes) ou FREQ (HP/LP),
et un curseur Q pour les bandes.

| Zone | ON | Type / pente | Fréquence | Gain | Q |
|---|---|---|---|---|---|
| HIGH-PASS | `pHpOn` (22) | `pHpSlope` (24) : 12/24/48 dB/oct | `pHpFreq` (23) 20–1000 Hz log | — | — |
| BAND 1 | `pB1On` (7) | `pB1Type` (8) : BELL / LOW SHELF / HIGH SHELF | `pB1Freq` (9) 20–20 k log | `pB1Gain` (10) ±18 dB | `pB1Q` (11) 0,1–10 log |
| BAND 2 | `pB2On` (12) | `pB2Type` (13) | `pB2Freq` (14) | `pB2Gain` (15) | `pB2Q` (16) |
| BAND 3 | `pB3On` (17) | `pB3Type` (18) | `pB3Freq` (19) | `pB3Gain` (20) | `pB3Q` (21) |
| LOW-PASS | `pLpOn` (25) | `pLpSlope` (27) | `pLpFreq` (26) 1000–20 k log | — | — |

Valeurs de démonstration de la maquette (HP actif à 80 Hz, trois bandes réglées) : **ne pas reprendre** ; les défauts du DSP sont tous « off ».
Quand `pEqOn` est coupé, toute la rangée et la courbe sont grisées (opacité 0,35).

---

## 5. Page 3 — Settings

Menu des sections à gauche (à la place des vu-mètres, même boîte) : General, Audio / DSP, Impulse responses, Metering,
Interaction, About ; la section visible est surlignée pendant le défilement. Contenu à droite, avec **barre de
défilement thémée** (voir `nova-scrollbar.js`).

| Réglage | Nature |
|---|---|
| Theme (cycle des 4 thèmes) | UI — à mémoriser |
| Interface scale (75/100/125/150 %) | UI — à mémoriser (maquette : applique `zoom`) |
| Language FR/EN | maquette seulement (change le libellé) |
| Sample rate, Reported latency (0 smp) | lecture seule (hôte / constante) |
| Default folder + BROWSE, On new instance (LAST USED / NONE) | **propositions de la maquette**, sans équivalent DSP : à garder ou retirer |
| Peak decay (5–300 dB/s) | `pMeterPeakDecay` (53) |
| RMS profiles (liste nom + ms, ajout/suppression) | chaque profil = une valeur de `pMeterRmsMs` (40), 10–3000 ms ; ils s'intègrent au cycle du bouton de mode des vu-mètres |
| Knob drag sensitivity (NORMAL / FINE) | UI |
| Double-click resets parameter, Mouse wheel on knobs | UI |
| About : version, documentation, support, mises à jour | maquette |

---

## 6. Chargement et affichage de l'IR (rappel de `docs/…reponses.md`)

- Le chemin est un **state `"ir_path"`** (jamais un paramètre). `requestStateFile()` ne marche pas sous CLAP dans ce DPF :
  utiliser `openFileBrowser()` puis `setState("ir_path", chemin)`. Définir `DISTRHO_UI_FILE_BROWSER 1`.
- **L'UI calcule elle-même** les données d'affichage (WAVE 800 colonnes min/max, SPECTRUM 256 points, plancher −120 dB)
  avec le code partagé du DSP (`ir_display.hpp`, `LowLatConvIR.hpp`) dans un thread de fond ; rafraîchir quand le chemin
  change **et** quand l'un des paramètres `pInputMode … pNorm` change.
- Statuts : `pIRStatus` 0 No IR, 1 Loading, 2 Loaded, 3 Error ; longueur = `pIRLengthMs`. En erreur le chemin reste affiché (rouge).
- Traces L/R selon le nombre de canaux de l'IR : voir le tableau de `docs/…reponses.md` §2 (1 canal : L = R ; 4 canaux : dépend du mode d'entrée).
- Le sélecteur ne filtre pas les extensions : l'UI doit afficher l'erreur si le fichier n'est pas un WAV.

---

## 7. Vu-mètres (colonne gauche, pages 1 et 2)

Deux groupes **IN** et **OUT** (2 barres L/R chacun), un bouton de mode par groupe, une valeur numérique + une ligne
secondaire, une LED de saturation. **Pas de LRA.** Échelle d'affichage −60 → 0 (dBFS ou LUFS) ; plancher des données −120.

- **IN** = après le gain d'entrée ; **OUT** = après le gain de sortie.
- Cycle du bouton de mode : `PEAK` → profils RMS (un par profil défini en Settings) → `VU` → `EBU-M` → `EBU-S`.
- **PEAK** : afficher telle quelle la crête du DSP (`pMInPeakL/R`, `pMOutPeakL/R` ; attaque instantanée, maintien 50 ms,
  retombée linéaire `pMeterPeakDecay`). **Ne pas ajouter une seconde retombée côté UI** (ou alors mettre `pMeterPeakDecay` à 300).
  Trait blanc sur chaque barre = **crête max exacte** (`pMInPeakMax*`, `pMOutPeakMax*`) jusqu'à la remise à zéro.
  Ligne secondaire « MAX x.x ». Cliquer sur la valeur ou sur la LED envoie `pMeterReset = 1`.
- **LED de saturation** : allumée quand la crête max ≥ −1 dBFS, **dans tous les modes**, jusqu'à la remise à zéro.
- **RMS** : `pMInRmsL/R`, `pMOutRmsL/R` ; la fenêtre `pMeterRmsMs` est **partagée IN/OUT** (changer le profil d'un groupe change l'autre).
  La ligne secondaire affiche la fenêtre en ms.
- **VU** : RMS avec `pMeterRmsMs = 300` + balistique d'affichage côté UI.
- **EBU-M / EBU-S** : `pMInLufsM/S`, `pMOutLufsM/S`, affichés en LUFS (fenêtres 400 ms / 3 s).
- Dans la maquette seule la fonction `meters()` simule les données : c'est le seul endroit à remplacer.

---

## 8. Système de design (partagé par les 4 UI)

### 8.1 Thèmes et identités
| Thème | Valeur interne | Potards (`THEME_KNOB_STYLE`) | Contrôles (`THEME_CTL` → `data-ctl`) |
|---|---|---|---|
| Sombre | `dark` | `led` (anneau) | `glass` |
| Hammond (orgue) | `hammond` | `pot` (physique) | `bezel` |
| Clair | `light` | `led` | `glass` |
| Haut contraste | `hc` | `led` | `hc` |

Le thème choisit : les couleurs (jetons CSS), le style de potard, l'identité des conteneurs et l'identité des contrôles
(`nova-controls.css` : tailles, rayons, ombres, dégradés des toggles, curseurs et pills). **Aucune couleur ni taille en dur
sur ces composants** : tout passe par les jetons ; NanoVG doit lire les mêmes valeurs (une palette par thème et par identité).

### 8.2 Réglage de contraste (`nova-contrast.js`)
Niveau 0 (base, rien ne change) à 4. Chaque rôle (texte, texte atténué, accents, bordures, pistes, poignées, couleurs de bandes…)
est recalculé pour atteindre un rapport de contraste cible, en gardant sa teinte. Cibles au niveau 4 : texte 12:1, texte atténué 8:1,
secondaire 5:1, accents 5:1, bordures 3,5:1. Les fonds translucides deviennent opaques. **Jamais appliqué au thème haut contraste**
(la pill est alors grisée). Le niveau est un réglage d'interface, pas un paramètre : le mémoriser (state DPF et/ou fichier de
préférences utilisateur partagé entre les plugins NOVA — à confirmer côté DPF). L'algorithme (luminance relative WCAG +
recherche de la plus petite variation vers le blanc ou le noir) se porte en ~40 lignes de C++ ; `test_nova_contrast.js` sert de
spécification de test (3 thèmes × 5 niveaux).

### 8.3 Barre de défilement (`nova-scrollbar.js`)
Rail 12 px (rayon `--sbr` : 5 en glass, 1 en bezel), poignée de 6 px (min. 28 px de long) : couleur du texte atténué, accent clair au survol,
accent plein en glissant ; clic sur le rail = page ; masquée s'il n'y a rien à faire défiler. Spécification complète en en-tête du fichier.

### 8.4 Contrôles
- **Toggle** : fente + pouce ; glass = pilule à pouce rond blanc ; bezel = fente creuse à pouce rectangulaire (orange quand allumé) ;
  hc = contour blanc épais, pouce blanc (éteint) ou noir sur fond jaune (allumé).
- **Curseur linéaire** : rail + remplissage + poignée (ronde en glass, rectangulaire crème en bezel).
- **Pill / bouton** : fond de carte, bordure, ombre creuse en bezel, survol = accent en bezel/hc.

---

## 9. Passage à NanoVG : ce qui se transpose et ce qui ne se transpose pas

Rien n'a été compilé ; à valider par un petit widget d'essai.
- **Direct** : rectangles arrondis (y compris à coins inégaux), dégradés linéaires et radiaux, transparence, traits arrondis,
  texte avec espacement des lettres, mélange de couleurs en C++.
- **À contourner** : ombres portées/intérieures (→ dégradé « box »), lueurs / `shadowBlur` (→ traits superposés plus larges et plus transparents),
  **pointillés** (→ découpe du tracé en segments), texture de bois du Hammond (lignes à la main ou omise), transitions CSS
  (→ animation manuelle), polices (→ **embarquer une police**, rendu un peu différent du navigateur), `color-mix()` (→ calculer à la main :
  `--acc-35` = accent à 35 % d'opacité, etc.), `zoom` (→ `nvgScale`).
- Les canvas de la maquette (potards, courbes, IR, spectre) sont des tracés de chemins : ils se traduisent presque ligne à ligne
  (arcs de potard 135°→405°, rayon `taille/2 − 6`, 2 styles dans `nova-knob-styles.js`).

---

## 10. Limites connues de la maquette / décisions à trancher

1. **Données simulées** : IR, spectre, niveaux, nom de fichier (3 fichiers factices dont un « en erreur »).
2. **Courbes d'EQ** approximatives (voir §4).
3. **Auto-trim et mode d'entrée** ne modifient pas le dessin de l'IR dans la maquette (c'est le code partagé du DSP qui le fera).
4. **Propositions sans équivalent DSP** : « Default folder », « On new instance », « Language ».
5. **A/B** : périmètre à décider (recommandation §3.5). **Presets internes** : aucun côté DSP ; format à décider.
6. **Alerte de crête à seuil** (type Peak Watcher) : possible côté UI en comparant `pMInPeakMax*` à un seuil ; non maquettée.
7. **Réglage de contraste, thème et échelle** : choisir où les mémoriser (state, fichier utilisateur, ou les deux).
8. Dans le thème Hammond, le pouce éteint des toggles et la poignée crème des curseurs sont des couleurs fixes (non modifiées par le contraste).
9. FLAC non pris en charge par le DSP (ajout possible avec `dr_flac.h`).
