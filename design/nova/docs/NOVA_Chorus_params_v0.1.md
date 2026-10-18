# NOVA Chorus — description des paramètres v0.1

Chorus stéréo standard et entièrement paramétrable. Le son du chorus du Juno-60 (celui que reproduit le TAL-Chorus-LX) n'est **pas un mode** du plugin :
c'est un groupe de **presets** (« Juno-60 I », « II », « I+II »). Les valeurs numériques sont des points de départ à régler à l'écoute (et à comparer au chorus de
référence) ; elles ne reproduisent pas le code de TAL.
Format de description : celui de `NOVA_TransientShaper_params_v0.2.md` §0 (id snake_case, préfixe de groupe `cho_`, enum `kParam` + CamelCase, unité séparée, `help_short`, `help_long`).

---

## 1. Principe de traitement

Une ligne à retard modulée par un LFO par canal. `τ(t)` = temps de retard instantané.

- Retard : `τ(t) = max(0,05 ms ; DELAY × (1 + DEPTH × L(t)))`, avec `L(t)` ∈ [−1 ; +1] la sortie du LFO. DEPTH est donc **relatif** au retard central :
  à 100 %, le retard oscille entre 0 et 2 × DELAY. Exemple : DELAY 3,35 ms et DEPTH 54 % → retard entre 1,5 ms et 5,2 ms.
- LFO : forme `cho_lfo_shape` (triangle ou sinus). Fréquence = RATE (Hz) ou, si SYNC est actif, la division choisie au tempo de l'hôte.
- Stéréo : la phase du LFO du canal droit est décalée de `WIDTH × 180°` par rapport à la gauche (0 % : en phase, 100 % : en opposition, comme sur le Juno).
  Entrée mono : dupliquée sur les deux canaux avant la ligne à retard.
- Voix : `cho_voices` lignes à retard par canal, leurs LFO déphasés de `360° / voix`. Les sorties sont sommées et divisées par le nombre de voix (gain unité aux basses fréquences).
- Feedback : une partie de la sortie de ligne (après les filtres et le caractère BBD) est réinjectée à l'entrée de la ligne, signe conservé (−90…+90 %).
- Caractère BBD (`cho_bbd`) : assombrit (passe-bas du 2ᵉ ordre de 20 kHz à 6 kHz en suivant `20 kHz × 0,3^BBD`), ajoute un peu de saturation douce et un bruit de fond très faible
  (jusqu'à −75 dBFS à 100 %). Appliqué à la voie traitée uniquement.
- Filtres de la voie traitée : passe-haut `cho_lowcut` et passe-bas `cho_highcut`, pente 6 / 12 / 24 dB/oct.
- Mélange : `sortie = (1 − MIX) × sec + MIX × traité`, puis OUTPUT.
- Interpolation de la ligne : linéaire ou cubique (Settings). Pas de latence : le retard modulé fait partie de l'effet, pas de la latence reportée à l'hôte.

---

## 2. Paramètres (groupe `cho`)

### 2.1 Modulation

#### cho_rate
- enum : `kParamChoRate` · label : RATE · type : float
- range : 0,05…10 · default : 0,5 · unit : Hz · scale : log · auto : oui · smooth : 20 ms
- page : CHORUS · control : potard · depends : grisé si SYNC actif
- help_short : Vitesse de la modulation.
- help_long : Nombre de cycles du LFO par seconde. Lent (0,2 à 1 Hz) : ondulation douce et large, effet chorus classique. Rapide (5 Hz et plus) : l'effet se rapproche du vibrato.
- meta : 0,05…10 Hz · Défaut 0,5 · Automatisable : oui

#### cho_sync
- enum : `kParamChoSync` · label : SYNC · type : bool
- default : 0 · auto : oui · page : CHORUS · control : switch (mini-contrôle)
- help_short : Cale la vitesse du LFO sur le tempo de l'hôte.
- help_long : Actif : la vitesse n'est plus donnée par RATE mais par la division choisie (DIVISION), suivant le tempo du morceau. Utile pour un effet rythmique ou un chorus qui respire en mesure.
- meta : Interrupteur · Défaut 0 · Automatisable : oui

#### cho_sync_div
- enum : `kParamChoSyncDiv` · label : DIVISION · type : enum
- range : 1/1, 1/2, 1/2., 1/2T, 1/4, 1/4., 1/4T, 1/8, 1/8., 1/8T, 1/16 · default : 1/2 · auto : non · page : CHORUS · control : pill (mini-contrôle)
- depends : grisé si SYNC inactif
- help_short : Durée d'un cycle de LFO, en valeur de note.
- help_long : Un cycle complet du LFO dure la valeur de note choisie. « . » = pointée, « T » = triolet.
- meta : Choix · Défaut 1/2

#### cho_depth
- enum : `kParamChoDepth` · label : DEPTH · type : float
- range : 0…100 · default : 50 · unit : % · scale : linear · auto : oui · smooth : 20 ms
- page : CHORUS · control : potard
- help_short : Amplitude de la modulation, relative au retard central.
- help_long : Part du retard central (DELAY) dont le retard varie. À 0 %, le retard ne bouge pas : il ne reste qu'un filtre en peigne fixe. Autour de 50 %, effet chorus classique. À 100 %, le retard varie entre 0 et deux fois DELAY : effet très marqué.
- meta : 0…100 % · Défaut 50 · Automatisable : oui

#### cho_delay
- enum : `kParamChoDelay` · label : DELAY · type : float
- range : 0,5…30 · default : 7 · unit : ms · scale : log · auto : oui · smooth : 50 ms
- page : CHORUS · control : potard
- help_short : Retard central de la ligne.
- help_long : Temps de retard autour duquel le LFO fait varier. Court (1 à 5 ms) : son coloré, proche d'un flanger léger. Moyen (10 à 20 ms) : chorus classique. Plus long : doublage plus lâche, sensation de deux instruments. Combiné à DEPTH, il fixe l'étendue réelle de la variation.
- meta : 0,5…30 ms · Défaut 7 · Automatisable : oui

#### cho_lfo_shape
- enum : `kParamChoLfoShape` · label : SHAPE · type : enum
- range : 0 Triangle, 1 Sinus · default : 0 · auto : non · page : CHORUS · control : pill (mini-contrôle)
- help_short : Forme du LFO.
- help_long : Le triangle donne une variation de hauteur régulière, comme sur le Juno. Le sinus ralentit aux extrêmes, pour une modulation un peu plus douce.
- meta : Choix : Triangle, Sinus · Défaut Triangle

#### cho_voices
- enum : `kParamChoVoices` · label : VOICES · type : int
- range : 1…4 · default : 1 · auto : non · page : CHORUS · control : pill (mini-contrôle)
- help_short : Nombre de lignes à retard par canal.
- help_long : Chaque voix est une ligne avec son propre LFO, déphasé des autres. Plus il y en a, plus le son est dense, large et épais (effet ensemble), mais moins l'oscillation individuelle est audible.
- meta : 1…4 · Défaut 1

### 2.2 Stéréo et feedback

#### cho_width
- enum : `kParamChoWidth` · label : WIDTH · type : float
- range : 0…100 · default : 100 · unit : % · scale : linear · auto : oui · smooth : 20 ms
- page : CHORUS · control : potard
- help_short : Écart de phase du LFO entre gauche et droite.
- help_long : À 0 %, les deux canaux sont modulés en même temps : l'effet reste au centre. À 100 %, la droite est en opposition de phase avec la gauche : image très large, sensation de mouvement entre les deux enceintes. Sur une source mono, c'est ce réglage qui crée la largeur.
- meta : 0…100 % (0…180°) · Défaut 100 · Automatisable : oui

#### cho_feedback
- enum : `kParamChoFeedback` · label : FEEDBACK · type : float
- range : −90…+90 · default : 0 · unit : % · scale : bipolar · auto : oui · smooth : 20 ms
- page : CHORUS · control : potard
- help_short : Réinjecte une partie du signal traité dans la ligne.
- help_long : À 0 %, chorus pur. En positif, le son se répète et prend un caractère de flanger résonant ; en négatif, la résonance est inversée (son plus creux, plus métallique). À utiliser avec un retard court pour un vrai flanger.
- meta : −90…+90 % · Défaut 0 · Automatisable : oui

### 2.3 Voie traitée (page TONE)

#### cho_lowcut
- enum : `kParamChoLowcut` · label : LOW CUT · type : float
- range : 20…1000 · default : 20 (= inactif) · unit : Hz · scale : log · auto : oui · page : TONE · control : potard + switch + pill de pente (zone LOW CUT)
- help_short : Retire les graves de la voie traitée.
- help_long : Filtre passe-haut placé sur le signal traité uniquement : le signal sec garde ses graves. Évite que le chorus rende les graves flous.
- meta : 20…1000 Hz · Défaut inactif · Automatisable : oui

#### cho_lowcut_slope · cho_highcut_slope
- type : enum · range : 6, 12, 24 dB/oct · default : 12 · auto : non · control : pill de la zone
- help_short : Pente du filtre.

#### cho_lowcut_on · cho_highcut_on · cho_bbd_on
- type : bool · default : 0 (lowcut/highcut), 1 (bbd) · auto : oui · control : switch de la zone

#### cho_highcut
- enum : `kParamChoHighcut` · label : HIGH CUT · type : float
- range : 1000…20000 · default : 20000 (= inactif) · unit : Hz · scale : log · auto : oui · page : TONE · control : potard + switch + pill de pente (zone HIGH CUT)
- help_short : Retire les aigus de la voie traitée.
- help_long : Filtre passe-bas placé sur le signal traité uniquement. Assombrit l'effet pour qu'il se fonde dans le mix.
- meta : 1…20 kHz · Défaut inactif · Automatisable : oui

#### cho_bbd
- enum : `kParamChoBbd` · label : BBD · type : float
- range : 0…100 · default : 0 · unit : % · scale : linear · auto : oui · page : TONE · control : potard (zone BBD CHARACTER)
- help_short : Caractère de ligne à retard analogique.
- help_long : Simule les défauts d'une ligne à retard analogique (BBD) : aigus adoucis, légère saturation, un peu de bruit de fond. À 0 %, le traitement est parfaitement propre. Les presets Juno-60 l'utilisent à environ 60 %.
- meta : 0…100 % · Défaut 0 · Automatisable : oui

### 2.4 Sortie

#### cho_mix
- enum : `kParamChoMix` · label : MIX · type : float
- range : 0…100 · default : 50 · unit : % · scale : linear · auto : oui · smooth : 20 ms · page : CHORUS · control : potard

#### cho_output_gain
- enum : `kParamChoOutputGain` · label : OUTPUT · type : float
- range : −24…+24 · default : 0 · unit : dB · scale : linear · auto : oui · smooth : 20 ms · page : CHORUS · control : potard

#### cho_bypass
- type : bool · default : 0 · auto : oui · control : bouton de l'en-tête (bypass sans coupure : fondu de 10 ms)

### 2.5 Settings (non automatisables, enregistrés avec le preset)

#### cho_interp
- label : Interpolation
- type : enum · range : 0 Linéaire, 1 Cubique · default : 1 · page : SETTINGS (MODULATION)
- help_short : Qualité de lecture de la ligne à retard.
- help_long : Cubique : plus propre, surtout avec un retard long et un DEPTH élevé. Linéaire : un peu plus léger pour le processeur et très légèrement plus sombre.
- meta : Choix : Linéaire, Cubique · Défaut Cubique

#### cho_lfo_start
- label : LFO start
- type : enum · range : 0 Libre, 1 À chaque lecture · default : 0 · page : SETTINGS (MODULATION)
- help_short : Phase de départ du LFO.
- help_long : Libre : le LFO tourne en continu. À chaque lecture : il repart de la même phase au démarrage du transport, pour que chaque lecture sonne identique (utile pour le rendu).
- meta : Choix : Libre, À chaque lecture · Défaut Libre

Autres réglages de Settings, communs à la série : précision interne, latence reportée (0 pour le chorus), module vu-mètres (`meter_*`), presets, interaction, aide « ? ».

---

## 3. Sorties d'affichage (lecture seule, non automatisables)

| id | Contenu | Plage |
|---|---|---|
| `cho_lfo_phase_out` | phase du LFO du canal gauche, voix 1 (la droite et les autres voix s'en déduisent avec WIDTH et VOICES) | 0…1 |

Une seule sortie suffit à animer le graphique central (point qui parcourt les courbes de retard) : tout le reste est calculé par l'UI à partir des paramètres.
Module vu-mètres : préfixe `meter_`, identique aux autres plugins (IN après le gain d'entrée absent ici : IN = entrée du plugin, OUT après OUTPUT).

---

## 4. Presets d'usine

Les valeurs ci-dessous sont des points de départ. Les trois presets « Juno-60 » sont une approximation du chorus du Juno-60 (lignes BBD, LFO triangle, déphasage gauche / droite) à valider à l'écoute et par mesure.

| Preset | RATE Hz | DEPTH % | DELAY ms | SHAPE | VOICES | WIDTH % | FEEDBACK % | BBD % | MIX % |
|---|---|---|---|---|---|---|---|---|---|
| Juno-60 I | 0,51 | 54 | 3,35 | Tri | 1 | 100 | 0 | 60 | 50 |
| Juno-60 II | 0,86 | 54 | 3,35 | Tri | 1 | 100 | 0 | 60 | 50 |
| Juno-60 I+II | 9,75 | 8 | 3,35 | Tri | 1 | 100 | 0 | 60 | 50 |
| Classic Chorus | 0,8 | 35 | 12 | Sin | 2 | 80 | 0 | 0 | 50 |
| Slow Ensemble | 0,25 | 45 | 15 | Sin | 4 | 100 | 0 | 15 | 45 |
| Subtle Doubler | 0,6 | 15 | 18 | Sin | 1 | 60 | 0 | 0 | 35 |
| Vibrato | 5,5 | 20 | 4 | Sin | 1 | 0 | 0 | 0 | 100 |
| Slow Flanger | 0,1 | 80 | 3 | Tri | 1 | 100 | 60 | 0 | 50 |
| Default | 0,5 | 50 | 7 | Tri | 1 | 100 | 0 | 0 | 50 |

Presets Juno : filtres inactifs, OUTPUT 0 dB. Le changement d'un seul réglage fait passer le preset en « modifié » (astérisque dans l'en-tête).

---

## 5. UI seulement (non paramètres)

A/B (comme les autres plugins), vue du panneau central (DELAY / RESPONSE), menu de presets, thème, contraste, échelle, langue, réglage de l'aide.

## 6. Organisation de l'interface (gabarit 1160 × 780, mêmes hauteurs de conteneurs sur les 3 pages)

**Page CHORUS**
- Colonne gauche : vu-mètres IN / OUT.
- Panneau central, bouton de vue : DELAY (retard gauche / droite au cours du temps, point animé par `cho_lfo_phase_out`) / RESPONSE (réponse en fréquence : enveloppe min–max du peigne sur un cycle de LFO, avec le trait du retard instantané).
- Rangée sous le graphique, 4 mini-contrôles : SYNC, DIVISION, SHAPE, VOICES.
- 7 potards : RATE, DEPTH, DELAY, WIDTH, FEEDBACK, MIX, OUTPUT.
- Barre du bas : A/B, LATENCY 0 smp, tempo de l'hôte.

**Page TONE** : réponse de la voie traitée (LOW CUT, HIGH CUT, BBD) avec poignées déplaçables ; trois zones : LOW CUT, HIGH CUT, BBD CHARACTER.
**Page SETTINGS** : comme les autres plugins, plus une section MODULATION (interpolation, phase de départ du LFO).

## 7. Points à valider
1. Valeurs des presets Juno-60 (rate, profondeur, retard central, caractère BBD) : à caler à l'écoute et par mesure.
2. Formule de DEPTH relatif au retard (§1) et plage de DELAY.
3. Feedback pris après les filtres et le BBD : choix à confirmer côté DSP.
4. Niveau de bruit et de saturation du caractère BBD.
