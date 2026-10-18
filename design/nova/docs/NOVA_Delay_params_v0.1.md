# NOVA Delay (multi-lignes) — description des paramètres v0.1

Delay à **8 lignes** (« taps ») dans l'esprit de ReaDelay : chaque ligne a son temps, son niveau, son panoramique, son feedback, ses filtres, sa modulation, son décalage de hauteur,
son inversion de phase et un décalage de temps entre les canaux gauche et droit. Réglages communs : feedback global, ping-pong, routage, ducking, saturation, freeze, largeur.
Format : celui de `NOVA_TransientShaper_params_v0.2.md` §0. Groupe `dly_` ; paramètres de ligne `dly_tN_*` (N = 1…8). Valeurs numériques = points de départ à régler à l'écoute.

---

## 1. Principe de traitement

Pour chaque ligne N, un **retard stéréo** : le canal gauche est retardé de `T`, le canal droit de `T + OFFSET` (OFFSET en ms, négatif = le droit arrive avant). Le retard réel du droit est borné à `max(0, T + OFFSET)` : il ne peut pas arriver avant le signal direct (aucun look-ahead, aucune latence ajoutée).

1. **Entrée** : `INPUT` (gain), puis, en mode FREEZE, l'entrée des lignes est coupée.
2. **Ligne N** (dans l'ordre) : retard `T(t) = TIME (ou valeur de note) + modulation` → décalage de hauteur `PITCH` → filtres `LOW CUT` (passe-haut) et `HIGH CUT` (passe-bas), 12 dB/oct
   → inversion de phase éventuelle → niveau `LEVEL` → panoramique `PAN`.
3. **Feedback de ligne** : la sortie de la ligne, après ses filtres et son pitch, est réinjectée à l'entrée de la **même** ligne, multipliée par `FEEDBACK` (−95…+95 %).
   Conséquence voulue : chaque répétition est un peu plus filtrée (et décalée en hauteur) que la précédente (effet « shimmer » avec un pitch non nul).
4. **Routage** (`dly_routing`) : *Parallèle* : toutes les lignes lisent l'entrée. *Cascade* : la ligne N lit la sortie de la ligne N−1 ; les temps s'additionnent.
5. **Feedback global** (`dly_fb_global`) : la somme des sorties des lignes est réinjectée à l'entrée de toutes les lignes (les échos relancent les autres lignes : motifs rythmiques complexes).
6. **Ping-pong** (`dly_pingpong`) : part du feedback (de ligne et global) croisée entre les canaux gauche et droit.
7. **Caractère** (`dly_drive`) : saturation douce dans la boucle de feedback, appliquée à chaque tour (les répétitions se compriment et se colorent).
8. **Limiteur de sécurité** (Settings, activé par défaut) : écrête doucement au-dessus de 0 dBFS dans la boucle, pour qu'un feedback élevé ne s'emballe jamais.
9. **Ducking** : un suiveur d'enveloppe lit le signal d'entrée sec ; au-dessus du seuil, le signal retardé est atténué de `DUCK AMOUNT` (attaque fixe 5 ms, relâchement `DUCK RELEASE`).
10. **Largeur** (`dly_width`, 0…200 %) : traitement M-S du signal retardé. **MIX** : `sortie = (1 − MIX) × sec + MIX × retardé`, puis OUTPUT.

**Panoramique.** Entrée mono : loi de panoramique à puissance constante (−3 dB au centre). Entrée stéréo : balance (gauche et droite atténuées de façon complémentaire).
**Modulation de temps.** LFO sinusoïdal par ligne : `T(t) = T0 × (1 + MOD_DEPTH × sin(2π × MOD_RATE × t + φN))` ; MOD_DEPTH est borné pour que l'excursion reste ≤ 8 ms.
Les phases initiales `φN` des lignes sont réparties pour éviter que toutes les lignes modulent en même temps.
**Décalage de hauteur.** Décaleur à deux grains croisés (taille de grain réglable en Settings) dans la ligne ; le retard propre du décaleur (environ la demi-longueur de grain) est retranché de `T`
quand c'est possible : les échos restent donc calés sur le tempo. Si `T` est plus court que ce retard, le temps réel est le retard minimal du décaleur.
**Changement de temps.** Glide (le temps glisse, la hauteur bouge comme sur un delay à bande) ou Crossfade (fondu entre l'ancien et le nouveau temps), au choix en Settings.
**Latence.** Aucune latence reportée à l'hôte (le délai fait partie de l'effet).

---

## 2. Paramètres d'une ligne (× 8)

Pour la ligne N : `dly_tN_…`, enum `kParamDlyTN…` (ex. `kParamDlyT3Feedback`). Valeurs par défaut des lignes 1 à 8 : voir §4.

| id (sans préfixe `dly_tN_`) | label | type | plage | défaut ligne 1 | unité · échelle | auto | page · contrôle |
|---|---|---|---|---|---|---|---|
| `on` | ON | bool | — | 1 (lignes 2–8 : 0) | — | oui | DELAY (sélecteur de ligne), TAPS |
| `solo` | SOLO | bool | — | 0 | — | oui | DELAY (sélecteur), TAPS |
| `time` | TIME | float | 1…4000 | 500 | ms · log | oui | DELAY (potard), TAPS · grisé si SYNC actif |
| `div` | DIV | enum | 1/1, 1/2, 1/2., 1/2T, 1/4, 1/4., 1/4T, 1/8, 1/8., 1/8T, 1/16, 1/16T | 1/4 | — | non | DELAY (potard, remplace TIME), TAPS · actif si SYNC |
| `level` | LEVEL | float | −60…+6 | 0 | dB | oui | DELAY (potard), TAPS |
| `pan` | PAN | float | −100…+100 | 0 | % · bipolaire | oui | DELAY (potard), TAPS |
| `offset` | R OFFSET | float | −100…+100 | 0 | ms · bipolaire | oui | TAPS (potard secondaire), TAPS |
| `feedback` | FEEDBACK | float | −95…+95 | 30 | % · bipolaire | oui | DELAY (potard), TAPS |
| `pitch` | PITCH | float | −24…+24 | 0 | demi-tons | oui | DELAY (potard), TAPS |
| `lowcut` | LOW CUT | float | 20…2000 | 20 (= inactif) | Hz · log | oui | TAPS (potard secondaire), TAPS |
| `highcut` | HIGH CUT | float | 500…20000 | 20000 (= inactif) | Hz · log | oui | TAPS (potard secondaire), TAPS |
| `invert` | INVERT | bool | — | 0 | — | oui | TAPS (zone), TAPS |
| `mod_depth` | MOD DEPTH | float | 0…100 | 0 | % | oui | TAPS (potard secondaire), TAPS |
| `mod_rate` | MOD RATE | float | 0,05…10 | 0,5 | Hz · log | oui | TAPS (potard secondaire), TAPS |

Total : 8 × 14 = 112 paramètres de ligne. Lissage : TIME 120 ms (glide, cf. Settings), LEVEL / PAN / FEEDBACK / filtres 20 ms.

### Aides des réglages de ligne (une seule fois pour les 8 lignes)

#### dly_feedback
- label : FEEDBACK
- help_short : Part de la ligne réinjectée dans elle-même.
- help_long : Règle le nombre de répétitions de cette ligne. Chaque répétition passe de nouveau par les filtres, le décalage de hauteur et la saturation, donc devient plus sombre et plus colorée. En négatif, chaque répétition est inversée en phase (son plus creux). Au-delà de 80 %, les échos durent très longtemps.
- meta : −95…+95 % · Défaut 30 · Automatisable : oui

#### dly_pitch
- label : PITCH
- help_short : Décale la hauteur des échos de cette ligne.
- help_long : Décale la hauteur de ±24 demi-tons, sans changer le temps de la ligne. Comme la ligne se réécoute avec son feedback, chaque répétition est décalée de nouveau : +12 donne des échos de plus en plus aigus (effet shimmer), −12 de plus en plus graves.
- meta : −24…+24 demi-tons · Défaut 0 · Automatisable : oui

#### dly_offset
- label : R OFFSET
- help_short : Décale le canal droit par rapport au gauche, en ms.
- help_long : Ajoute (ou retire, en négatif) un temps de retard au canal droit seulement. Quelques millisecondes élargissent l'image ; des valeurs plus grandes donnent un effet ping-pong naturel, même avec une entrée mono. Le droit ne peut jamais arriver avant le signal d'entrée : si l'offset négatif dépasse le temps de la ligne, le droit reste à 0 ms (pas de look-ahead).
- meta : −100…+100 ms · Défaut 0 · Automatisable : oui

#### dly_mod_depth
- label : MOD DEPTH
- help_short : Profondeur de la modulation du temps de la ligne.
- help_long : Fait osciller légèrement le temps de la ligne (de 0 à environ ±8 ms). Une petite valeur ajoute du mouvement, comme un delay à bande ou analogique ; une grande valeur fait vibrer la hauteur des échos.
- meta : 0…100 % · Défaut 0 · Automatisable : oui

#### dly_mod_rate
- label : MOD RATE
- help_short : Vitesse de la modulation du temps.
- help_long : Nombre de cycles par seconde du LFO de modulation. N'a d'effet que si MOD DEPTH n'est pas à 0.
- meta : 0,05…10 Hz · Défaut 0,5 · Automatisable : oui

---

## 3. Paramètres globaux

| id | label | type | plage | défaut | unité · échelle | auto | page · contrôle |
|---|---|---|---|---|---|---|---|
| `dly_sync` | SYNC | bool | — | 0 | — | oui | GLOBAL (mini-contrôle) |
| `dly_routing` | ROUTING | enum | 0 Parallèle, 1 Cascade | 0 | — | non | GLOBAL (mini-contrôle) |
| `dly_freeze` | FREEZE | bool | — | 0 | — | oui | GLOBAL (mini-contrôle) |
| `dly_duck_amount` | DUCK AMOUNT | float | 0…100 | 0 | % | oui | GLOBAL (mini-contrôle) |
| `dly_duck_thresh` | DUCK THRESHOLD | float | −60…0 | −30 | dB | oui | GLOBAL (mini-contrôle) |
| `dly_duck_release` | DUCK RELEASE | float | 10…2000 | 300 | ms · log | oui | GLOBAL (mini-contrôle) |
| `dly_fb_global` | FEEDBACK | float | −95…+95 | 0 | % · bipolaire | oui | GLOBAL (potard) |
| `dly_pingpong` | PING-PONG | float | 0…100 | 0 | % | oui | GLOBAL (potard) |
| `dly_drive` | DRIVE | float | 0…100 | 0 | % | oui | GLOBAL (potard) |
| `dly_width` | WIDTH | float | 0…200 | 100 | % | oui | GLOBAL (potard) |
| `dly_input_gain` | INPUT | float | −24…+24 | 0 | dB | oui | GLOBAL (potard) |
| `dly_mix` | MIX | float | 0…100 | 35 | % | oui | DELAY (potard) |
| `dly_output_gain` | OUTPUT | float | −24…+24 | 0 | dB | oui | DELAY (potard) |
| `dly_bypass` | BYPASS | bool | — | 0 | — | oui | bouton de l'en-tête (fondu de 10 ms) |

### Aides des réglages globaux

#### dly_routing
- label : ROUTING
- help_short : Comment les huit lignes sont reliées entre elles.
- help_long : Parallèle : chaque ligne écoute directement l'entrée, et les huit échos s'additionnent. Cascade : chaque ligne écoute la sortie de la précédente, donc les temps s'additionnent (la ligne 3 est retardée de la somme des temps des lignes 1, 2 et 3). Cascade est utile pour construire des motifs rythmiques à partir de petits intervalles.
- meta : Choix : Parallèle, Cascade · Défaut Parallèle

#### dly_pingpong
- label : PING-PONG
- help_short : Renvoie les échos alternativement à gauche et à droite.
- help_long : À 0 %, chaque écho reste du même côté. À 100 %, le feedback traverse les canaux : un écho à gauche donne l'écho suivant à droite, et ainsi de suite. Utilise le feedback de chaque ligne et le feedback global.
- meta : 0…100 % · Défaut 0 · Automatisable : oui

#### dly_duck_amount
- label : DUCK AMOUNT
- help_short : Baisse les échos pendant que le signal joue.
- help_long : Atténue le signal retardé quand le signal d'entrée dépasse DUCK THRESHOLD, puis le laisse revenir pendant les silences (temps réglé par DUCK RELEASE). Garde la voix ou la caisse claire claire et nette tout en laissant les échos s'épanouir entre les phrases ou les coups.
- meta : 0…100 % · Défaut 0 · Automatisable : oui

#### dly_drive
- label : DRIVE
- help_short : Saturation douce dans la boucle de feedback.
- help_long : Chaque passage dans la boucle sature légèrement le signal : les échos successifs deviennent plus denses et plus chauds, comme sur un écho à bande. À 0 %, la boucle est parfaitement propre.
- meta : 0…100 % · Défaut 0 · Automatisable : oui

#### dly_freeze
- label : FREEZE
- help_short : Fige le contenu des lignes en boucle.
- help_long : Coupe l'entrée des lignes et pousse le feedback à son maximum : ce qui est en cours de répétition tourne en boucle indéfiniment, comme une nappe. Désactiver FREEZE rend à l'entrée et au feedback leurs réglages.
- meta : Interrupteur · Défaut 0 · Automatisable : oui

### Réglages de Settings (non automatisables, enregistrés avec le preset)

| id | label | type | plage | défaut |
|---|---|---|---|---|
| `dly_time_mode` | Time change | enum | 0 Glide, 1 Crossfade | 0 |
| `dly_glide_ms` | Glide time | float | 20…500 ms | 120 |
| `dly_safety_limiter` | Feedback safety limiter | bool | — | 1 |
| `dly_pitch_quality` | Pitch quality | enum | 0 Standard (grain 40 ms), 1 High (grain 80 ms) | 0 |
| `dly_pan_law` | Pan law | enum | −3 dB, −4,5 dB, −6 dB | −3 dB |

#### dly_time_mode
- label : Time change
- help_short : Comportement quand on change le temps d'une ligne.
- help_long : Glide : le temps glisse jusqu'à sa nouvelle valeur, ce qui fait varier la hauteur des échos pendant le geste, comme sur un delay à bande. Crossfade : fondu entre l'ancien et le nouveau temps, sans variation de hauteur (plus propre, un peu moins expressif).
- meta : Choix : Glide, Crossfade · Défaut Glide

#### dly_safety_limiter
- label : Feedback safety limiter
- help_short : Évite que le feedback s'emballe.
- help_long : Écrête doucement le signal de la boucle au-dessus de 0 dBFS. Recommandé : sans lui, un feedback élevé (ou négatif et global) peut produire une boucle de plus en plus forte.
- meta : Interrupteur · Défaut actif

---

## 4. Presets d'usine et valeurs par défaut des lignes

Valeurs par défaut des lignes (preset « Default ») : ligne N à `500 × N ms` (1/4 × N en sync), niveau `−3 × (N−1) dB`, pan 0, feedback 30 %, lowcut inactif, highcut 20 kHz, pitch 0, offset 0, pas de modulation ; seule la ligne 1 est active.

| Preset | Lignes actives (temps · niveau · pan) | Autres |
|---|---|---|
| Default | 1 : 500 ms · 0 dB · centre | fb 30 %, MIX 35 % |
| Slapback | 1 : 110 ms · 0 dB · centre | fb 0 %, drive 10 %, MIX 30 % |
| Ping-Pong 1/8 | 1 : 1/8 · 0 dB · G 100 %, 2 : 1/4 · −2 dB · D 100 % | sync, fb 35 % par ligne, ping-pong 60 %, MIX 30 % |
| Tape Echo | 1 : 190 ms · 0 · G 30 %, 2 : 380 ms · −4 · D 30 %, 3 : 570 ms · −8 · centre | fb 45 % (ligne 3), lowcut 150 Hz, highcut 4 kHz, mod 20 % à 0,6 Hz, drive 35 %, MIX 28 % |
| Dotted Rhythm | 1 : 1/8. · 0 · G 50 %, 2 : 1/4 · −3 · D 50 %, 3 : 1/16 · −6 · centre | sync, fb 30 %, MIX 30 % |
| Multi-Tap Bounce | 8 lignes de 90 à 1250 ms (90, 170, 260, 380, 520, 700, 930, 1250), niveaux 0 → −16 dB, pans alternés G/D | highcut décroissant (12 kHz → 3 kHz), MIX 40 % |
| Shimmer Pad | 1 : 1/4 · 0 · centre · pitch +12, 2 : 1/2 · −4 · D 60 % · pitch +7 | fb 60 %, mod 25 %, MIX 40 %, drive 0 |
| Stereo Haas | 1 : 25 ms · G 100 %, 2 : 35 ms · D 100 % | fb 0 %, width 150 %, MIX 50 % |

Un changement de réglage fait passer le preset en « modifié » (astérisque dans l'en-tête).

---

## 5. Sorties d'affichage (lecture seule, non automatisables)

| id | Contenu |
|---|---|
| `dly_duck_gain_out` | gain de ducking courant (0…−24 dB), pour la courbe de la page GLOBAL |
| `dly_hit_phase_out` | position du curseur de simulation (0…1), uniquement si l'UI veut animer la vue temporelle en même temps que le DSP |

Tout le reste (positions des barres, répétitions du feedback, réponses des filtres) est calculé par l'UI à partir des paramètres.
Module vu-mètres : préfixe `meter_`, identique aux autres plugins (IN = entrée après INPUT, OUT après OUTPUT).

---

## 6. Organisation de l'interface (gabarit 1160 × 780, mêmes hauteurs de conteneurs sur les 4 pages)

**Page DELAY**
- Colonne gauche : vu-mètres IN / OUT.
- Panneau central, bouton de vue : TIMELINE (barres des huit lignes, haut = canal gauche, bas = canal droit, répétitions du feedback en plus pâle, glisser pour régler temps et niveau) / FILTERS (réponses cumulées des filtres de la ligne choisie sur les 4 premières répétitions).
- Rangée sous le graphique, 8 sélecteurs de ligne : numéro, ON, SOLO, mini-niveau.
- 7 potards : pour la ligne choisie TIME (ou DIV si SYNC), LEVEL, PAN, FEEDBACK, PITCH ; puis, séparés, MIX et OUTPUT.
- Barre du bas : A/B, LATENCY 0 smp, tempo de l'hôte.

**Page TAPS** : matrice des huit lignes × tous les paramètres de ligne (valeurs éditables au glisser ou à la molette). Rangée de potards pour la ligne choisie : LOW CUT, HIGH CUT, MOD DEPTH, MOD RATE, R OFFSET, INVERT, plus COPY / PASTE / RESET.
**Page GLOBAL** : simulation d'un coup d'entrée avec ses échos, la courbe de ducking et le niveau de sortie ; mini-contrôles SYNC, ROUTING, FREEZE, DUCK AMOUNT, DUCK THRESHOLD, DUCK RELEASE ; potards FEEDBACK, PING-PONG, DRIVE, WIDTH, INPUT.
**Page SETTINGS** : comme les autres plugins, plus une section DELAY (changement de temps, glide, limiteur de sécurité, qualité du pitch, loi de pan).

## 7. Points à valider
1. Décaleur de hauteur : algorithme (deux grains croisés) et coût processeur de 8 lignes × 2 canaux ; qualité « High ».
2. Compensation du retard propre du décaleur dans le temps de la ligne (§1).
3. Cascade : les temps s'additionnent, ce qui peut dépasser 4 s : fixer la limite (plafonnée à 8 s ?).
4. Modulation de temps : excursion maximale (±8 ms) et phases initiales.
5. Nombre de paramètres exposés à l'automation (112 de ligne + 14 globaux) : tout exposer, ou masquer des lignes 5 à 8 par défaut.


> Note routage Cascade : les temps s'additionnent d'une ligne à la suivante et les niveaux (dB) se multiplient (s'additionnent en dB).
