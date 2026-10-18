# NOVA Transient Shaper (large bande) — description des paramètres v0.2

Remplace le descriptif v0.1 (NOVA_TransientShaper_spec.md) pour tout ce qui concerne les paramètres.
Décisions intégrées : LEVEL à deux sens, deux jeux ATTACK/SUSTAIN + LINK, 3 temps visibles
(ATTACK TIME, SUSTAIN TIME, SHAPE SMOOTHING), intégration du module sidechain masquée, aide en ligne.
Les valeurs numériques sont des points de départ à régler à l'écoute.

---

## 0. Format de description (générique, réutilisable)

Un bloc par paramètre, avec ces champs :

| Champ | Sens |
|---|---|
| `id` | identifiant stable, snake_case, préfixe = groupe (jamais renommé : il sert à la sauvegarde et à l'automation) |
| `enum` | nom dans le code : `kParam` + CamelCase de l'id |
| `label` | nom affiché (court, majuscules sur les potards) |
| `group` | groupe d'affichage et préfixe |
| `type` | float / int / bool / enum / trigger / output (lecture seule) / state (chaîne) |
| `range`, `default`, `unit` | l'unité est séparée du nom |
| `scale` | linear / log / geometric / bipolar |
| `auto` | automatisable : oui / non |
| `smooth` | lissage DSP du changement de valeur (ms), ou « — » |
| `page`, `control` | où et avec quel contrôle (potard, curseur, pill, switch, enum…) |
| `depends` | conditions de grisage / d'activation |
| `help_short` | une ligne (bulle au survol) |
| `help_long` | explication complète (panneau d'aide du bouton « ? ») |

Les textes d'aide sont écrits une seule fois ici ; la maquette HTML et le code les reprennent tels quels.

---

## 1. Principe de traitement

Deux suiveurs d'enveloppe lisent la clé de détection (signal propre ou sidechain après EQ/courbe) :
un **rapide** et un **lent**, calculés en dB.

- `D(t) = env_rapide_dB − env_lent_dB`
- `D > 0` : le rapide devance le lent → **attaque**. `D < 0` : le rapide est retombé sous le lent → **sustain**.
- Gain (dB) = `RANGE × (attack_gain × A' + sustain_gain × S') × w`, avec les gains en ±100 %, `A' = clamp(max(D,0) / 12, 0, 1)` et `S' = clamp(max(−D,0) / 12, 0, 1)`
  (proposition : un écart de 12 dB entre les deux suiveurs = effet maximal ; RANGE = changement de gain maximal à ±100 %), puis lissé par **SHAPE SMOOTHING**.
- **ATTACK TIME** = temps d'attaque du suiveur lent (jusqu'où après un front on parle encore d'attaque).
- **SUSTAIN TIME** = temps de relâchement du suiveur lent (jusqu'où dans la queue on parle encore de sustain).
- Le suiveur rapide en est dérivé : attaque = ATTACK TIME / rapport, relâchement = SUSTAIN TIME / rapport (rapport = `shp_det_fast_ratio`, 10 par défaut, attaque minimale 0,05 ms).
- Chaque suiveur est un lissage du premier ordre sur la valeur absolue de la clé (mode Peak) ou sur son carré (mode RMS), converti en dB. En stéréo, `shp_stereo_link` mélange les clés des deux canaux.

### Dépendance au niveau (LEVEL, −100…+100 %)
`L = level_dependency / 100` ; `c` = part « au-dessus du seuil » du niveau du transitoire, progressive grâce au coude :
`c = lissage(0,5 + (niveau_dB − threshold_dB) / knee_dB)` (lissage = smoothstep borné à 0…1), de 0 (sous le seuil moins la moitié du coude) à 1 (au-dessus du seuil plus la moitié du coude).
Le niveau du transitoire est celui du suiveur rapide.

| LEVEL | Poids `w` (attaque ET sustain) | Effet |
|---|---|---|
| 0 | `w = 1` | uniforme : coup doux et coup fort traités pareil |
| > 0 | `w = (1 − L) + L·c` | de plus en plus réservé aux transitoires **forts** |
| < 0 | `w = (1 − |L|) + |L|·(1 − c)` | de plus en plus réservé aux transitoires **doux** |

Un **plancher de silence** (`shp_noise_floor`) reste actif même à LEVEL = 0 pour ne jamais amplifier le bruit de fond.

### Mode automatique du SHAPE SMOOTHING (proposition à valider côté DSP)
`smoothing_auto_ms = max(1 ms, 0,3 × min(ATTACK TIME, SUSTAIN TIME))`, borné à 50 ms.
Le réglage utilisateur multiplie cette valeur par `2^(v/50)` : −50 % → ×0,5 ; 0 % → auto ; +50 % → ×2.
Le plancher de 1 ms évite la distorsion basse fréquence sur les graves (grosse caisse) quand l'attaque est très courte.

### Modes de canaux
Stereo : jeu 1 seul (jeu 2 inactif). Dual Mono : 1 = L, 2 = R. M-S : 1 = M, 2 = S.
En mode Stereo, `shp_stereo_link` règle la part de détection commune aux deux canaux.

---

## 2. Paramètres du shaper (groupe `shp`)

### 2.1 Jeux de réglages (2 exemplaires)

#### shp_attack_gain_1 / shp_attack_gain_2
- enum : `kParamShpAttackGain1`, `kParamShpAttackGain2` · label : ATTACK · type : float
- range : −100…+100 · default : 0 · unit : % · scale : bipolar · auto : oui · smooth : 10 ms
- page : SHAPER · control : potard (carte ATTACK) · depends : jeu 2 actif seulement en Dual Mono et M-S
- help_short : Renforce (+) ou adoucit (−) le début des sons.
- help_long : Règle l'effet sur l'attaque, c'est-à-dire le claquement initial d'un coup de caisse claire, d'une attaque de médiator ou d'une percussion.
  Vers le positif, l'attaque devient plus marquée ; vers le négatif, elle est arrondie. À 0 %, l'attaque n'est pas touchée.
  L'ampleur maximale dépend de RANGE. La durée de ce qui compte comme attaque se règle avec ATTACK TIME.

#### shp_sustain_gain_1 / shp_sustain_gain_2
- enum : `kParamShpSustainGain1`, `kParamShpSustainGain2` · label : SUSTAIN · type : float
- range : −100…+100 · default : 0 · unit : % · scale : bipolar · auto : oui · smooth : 10 ms
- page : SHAPER · control : potard (carte SUSTAIN) · depends : comme ci-dessus
- help_short : Allonge (+) ou raccourcit (−) la résonance après l'attaque.
- help_long : Règle l'effet sur la queue du son : résonance de la caisse, ambiance de la salle, tenue d'une note.
  Vers le positif, la résonance est plus présente et plus longue ; vers le négatif, le son est plus sec.
  À 0 %, la queue n'est pas touchée. L'ampleur maximale dépend de RANGE, la durée de ce qui compte comme sustain de SUSTAIN TIME.

### 2.2 Dépendance au niveau

#### shp_level_dependency
- enum : `kParamShpLevelDependency` · label : LEVEL · type : float
- range : −100…+100 · default : 0 · unit : % · scale : bipolar · auto : oui · smooth : 20 ms
- page : SHAPER · control : potard (carte LEVEL)
- help_short : Fait dépendre l'effet du volume du transitoire.
- help_long : À 0 %, l'effet est identique quel que soit le volume : un coup doux et un coup fort sont traités pareil.
  Vers le positif, l'effet est de plus en plus réservé aux transitoires forts (au-dessus du seuil) ; vers le négatif, aux transitoires doux (sous le seuil).
  Utile pour dynamiser un jeu fort sans toucher aux nuances, ou au contraire pour relever les notes fantômes. Le seuil et le coude se règlent avec THRESHOLD et KNEE.

#### shp_level_threshold
- enum : `kParamShpLevelThreshold` · label : THRESHOLD · type : float
- range : −60…0 · default : −24 · unit : dB · scale : linear · auto : oui · smooth : 20 ms
- page : SHAPER · control : potard (carte THRESHOLD) + point déplaçable sur la vue RESPONSE · depends : grisé si LEVEL = 0
- help_short : Niveau qui sépare les transitoires doux des forts.
- help_long : Niveau de transitoire à partir duquel il est considéré comme fort. Sous ce niveau, il est considéré comme doux.
  N'a d'effet que si LEVEL n'est pas à 0 %. Se règle aussi à la souris sur la vue RESPONSE du graphique.

#### shp_level_knee
- enum : `kParamShpLevelKnee` · label : KNEE · type : float
- range : 0…24 · default : 6 · unit : dB · scale : linear · auto : oui · smooth : 20 ms
- page : SHAPER · control : mini-contrôle sous le graphique + poignée sur la vue RESPONSE · depends : grisé si LEVEL = 0
- help_short : Douceur du passage entre doux et fort.
- help_long : À 0 dB, le passage au seuil est net. Une valeur plus grande rend la transition progressive autour du seuil.
  N'a d'effet que si LEVEL n'est pas à 0 %.

### 2.3 Temps et amplitude

#### shp_attack_time
- enum : `kParamShpAttackTime` · label : ATTACK TIME · type : float
- range : 0,1…100 · default : 10 · unit : ms · scale : log · auto : oui · smooth : 20 ms
- page : SHAPER · control : mini-contrôle sous le graphique
- help_short : Durée de ce qui compte comme attaque.
- help_long : Court (0,1 à 5 ms) : seul le claquement initial est ciblé. Long (30 ms et plus) : le corps de la note entre aussi dans l'attaque.
  C'est le temps d'attaque du suiveur lent ; il détermine la forme de l'enveloppe détectée, pas la vitesse du gain appliqué (voir SHAPE SMOOTHING).

#### shp_sustain_time
- enum : `kParamShpSustainTime` · label : SUSTAIN TIME · type : float
- range : 10…1000 · default : 150 · unit : ms · scale : log · auto : oui · smooth : 20 ms
- page : SHAPER · control : mini-contrôle sous le graphique
- help_short : Jusqu'où dans la queue on parle encore de sustain.
- help_long : Court : seul le début de la résonance est ciblé. Long : la détection suit toute la queue du son, y compris l'ambiance.
  C'est le temps de relâchement du suiveur lent ; comme ATTACK TIME, il définit ce qui est détecté, pas la vitesse du gain.

#### shp_shape_smoothing
- enum : `kParamShpShapeSmoothing` · label : SHAPE SMOOTHING · type : float
- range : −50…+50 · default : 0 (Auto) · unit : % · scale : geometric (×2^(v/50)) · auto : oui · smooth : —
- page : SHAPER · control : mini-contrôle sous le graphique
- help_short : Lissage du gain appliqué, relatif au mode auto.
- help_long : Règle la vitesse à laquelle le gain calculé évolue et revient à 0 dB. 0 % correspond au réglage automatique, calculé à partir d'ATTACK TIME et de SUSTAIN TIME.
  Vers le négatif, le gain suit plus vite : effet plus précis mais risque d'artefacts sur les graves. Vers le positif, le gain est plus doux et plus arrondi.
  Comme il est relatif à l'auto, il ne peut pas être réglé de façon incohérente avec les deux temps.

#### shp_range
- enum : `kParamShpRange` · label : RANGE · type : float
- range : 6…24 · default : 15 · unit : dB · scale : linear · auto : oui · smooth : 20 ms
- page : SHAPER · control : mini-contrôle sous le graphique
- help_short : Variation de gain maximale à ±100 %.
- help_long : Plafond du gain ajouté ou retranché lorsque ATTACK ou SUSTAIN sont à fond. Une valeur plus faible protège contre les excès ; une valeur plus forte permet des transformations extrêmes.

#### shp_stereo_link
- enum : `kParamShpStereoLink` · label : LINK · type : float
- range : 0…100 · default : 100 · unit : % · scale : linear · auto : oui · smooth : 20 ms
- page : SHAPER · control : mini-contrôle sous le graphique · depends : actif en mode Stereo seulement
- help_short : Part de détection commune aux deux canaux.
- help_long : À 100 %, les deux canaux reçoivent le même gain, ce qui conserve l'image stéréo. À 0 %, chaque canal est traité séparément, ce qui élargit l'effet mais peut déplacer l'image.

### 2.4 Sortie et mélange

#### shp_output_gain
- enum : `kParamShpOutputGain` · label : OUTPUT · type : float · range : −24…+24 · default : 0 · unit : dB · scale : linear · auto : oui · smooth : 20 ms
- page : SHAPER · control : potard
- help_short : Niveau de sortie.
- help_long : Compense le niveau après traitement, par exemple si l'attaque renforcée fait monter les crêtes.

#### shp_mix
- enum : `kParamShpMix` · label : MIX · type : float · range : 0…100 · default : 100 · unit : % · scale : linear · auto : oui · smooth : 20 ms
- page : SHAPER · control : potard
- help_short : Mélange signal traité / signal d'origine.
- help_long : 100 % : signal entièrement traité. Moins : mélange avec le signal d'origine (compression parallèle des transitoires).

### 2.5 Barre du bas

#### shp_channel_mode
- enum : `kParamShpChannelMode` · label : STEREO / DUAL MONO / M-S · type : enum (0 Stereo, 1 Dual Mono, 2 M-S) · default : 0 · auto : non
- page : SHAPER · control : switch 3 états
- help_short : Mode de traitement des canaux.
- help_long : Stereo : un seul jeu de réglages pour les deux canaux. Dual Mono : un jeu pour la gauche, un pour la droite. M-S : un jeu pour le centre (mid), un pour les côtés (side).

#### shp_delta
- enum : `kParamShpDelta` · label : DELTA · type : bool · default : 0 · auto : oui
- page : SHAPER · control : bouton
- help_short : N'écoute que ce que le traitement ajoute ou retire.
- help_long : Sort la différence entre le signal traité et le signal d'origine, pour entendre exactement ce que le shaper modifie.

#### shp_lookahead
- enum : `kParamShpLookahead` · label : LOOKAHEAD · type : float · range : 0…10 · default : 0 · unit : ms · scale : linear · auto : non
- page : SHAPER · control : curseur · depends : modifie la latence reportée à l'hôte
- help_short : Anticipe la détection ; ajoute de la latence.
- help_long : Retarde le signal pour que la détection voie les attaques avant qu'elles arrivent, ce qui rend l'effet sur l'attaque plus précis. La latence correspondante est reportée à l'hôte.

#### shp_oversampling
- enum : `kParamShpOversampling` · label : OVERSAMPLING · type : enum (0 None, 1 2x, 2 4x) · default : 2 (comme le compresseur) · auto : non
- page : SHAPER · control : bouton cyclique NONE / 2X / 4X, identique à celui du compresseur · depends : modifie la latence reportée à l'hôte (Settings : « Report latency to host »)
- help_short : Suréchantillonnage interne.
- help_long : Traite le signal à une fréquence plus élevée pour limiter les artefacts de repliement quand l'attaque est renforcée fortement. Plus la valeur est haute, plus le traitement est précis et plus la charge processeur et la latence augmentent.

#### shp_bypass
- enum : `kParamShpBypass` · label : BYPASS · type : bool · default : 0 · auto : oui
- page : en-tête · control : bouton
- help_short : Contourne le plugin.
- help_long : Laisse passer le signal sans traitement, à latence constante.

### 2.6 Settings

#### shp_noise_floor
- enum : `kParamShpNoiseFloor` · label : Plancher de détection · type : float · range : −100…−40 · default : −70 · unit : dBFS · auto : non
- page : SETTINGS (Audio / DSP)
- help_short : Niveau sous lequel le shaper n'agit plus.
- help_long : Sous ce niveau, la détection est ignorée pour ne jamais amplifier le bruit de fond, même quand LEVEL est à 0 %.

### 2.7 Détecteurs (page SETTINGS, section « Détecteurs »)
Ces réglages changent le son : ce sont des paramètres du plugin (non automatisables), enregistrés avec les presets et la session, et non des préférences d'interface.

#### shp_det_mode
- enum : `kParamShpDetMode` · label : Détection · type : enum (0 Peak, 1 RMS) · default : 0 · auto : non
- help_short : Type de mesure des enveloppes.
- help_long : Peak suit les crêtes instantanées : réaction très vive, idéal pour les percussions. RMS suit l'énergie moyenne : détection plus douce et plus stable, mieux adaptée aux sons tenus ou aux signaux denses.

#### shp_det_fast_ratio
- enum : `kParamShpDetFastRatio` · label : Rapport rapide / lent · type : float · range : 3…30 · default : 10 · unit : × · scale : log · auto : non
- help_short : Vitesse du suiveur rapide par rapport au lent.
- help_long : Le suiveur rapide va X fois plus vite que le lent, en attaque comme en relâchement. Une valeur élevée sépare mieux attaque et sustain, mais rend la détection plus sensible aux irrégularités du signal ; une valeur faible donne un effet plus doux.

---

## 3. Sorties de mesure et d'affichage (lecture seule, non automatisables)

| id | Contenu | Plage |
|---|---|---|
| `shp_gain_attack_db` | gain appliqué par la part attaque (vu-mètre bipolaire) | −24…+24 dB |
| `shp_gain_sustain_db` | gain appliqué par la part sustain | −24…+24 dB |

Module vu-mètres (préfixe `meter_`, voir `NOVA_Common_modules_v0.1.md` §1) : seuls `meter_rms_ms` et `meter_peak_decay` sont des paramètres (décision du 2026-10-18) ; crête / RMS / LUFS-M / LUFS-S pour IN et OUT sont des valeurs internes lues par l'interface, crêtes max et remise à zéro côté interface.
IN mesuré après le gain d'entrée, OUT après OUTPUT.

**Question technique ouverte** : le panneau central veut afficher en continu le signal, les deux enveloppes et le gain appliqué.
L'UI DPF n'a pas accès à l'audio : il faut décider du transport (tampon partagé si UI et DSP sont dans le même binaire,
ou séries sous-échantillonnées exposées par le DSP).

---

## 4. Module sidechain (préfixe `sc_`)

Module standard de la série (entrée stéréo, activation, audio/MIDI, Internal/External/Mix, courbe de transfert, EQ avec analyseur, LISTEN).
**Particularité du shaper** : le contrôle d'intégration (`sc_integration`) est masqué, car le shaper possède ses propres suiveurs.
L'EQ de détection remplace une version multibande : par exemple isoler les graves pour ne traiter que la grosse caisse.

---

## 5. UI seulement (non paramètres)

A/B (A = jeu automatisable, B = jeu temporaire non automatisable, action « copier B dans A »), vue du panneau central (WAVE / RESPONSE),
thème, contraste, échelle, langue, réglage de l'aide (boutons « ? » toujours visibles / au survol / masqués).

---

## 6. Organisation de l'interface (gabarit 1160 × 780, mêmes hauteurs de conteneurs sur les 3 pages)

**Page SHAPER**
- Colonne gauche : vu-mètres IN / OUT + vu-mètre bipolaire GAIN (à la place du LRA).
- Panneau central, bouton de vue : WAVE (signal grisé, enveloppes rapide et lente, gain appliqué rempli au-dessus/au-dessous de l'axe) / RESPONSE (courbe LEVEL, seuil et coude déplaçables).
- Rangée sous le graphique, 6 mini-contrôles : ATTACK TIME, SUSTAIN TIME, SHAPE SMOOTHING, RANGE, KNEE, LINK.
- 6 potards : ATTACK, SUSTAIN, LEVEL, THRESHOLD, OUTPUT, MIX, chacun avec son bouton « ? ».
- Barre du bas : A/B, STEREO / DUAL MONO / M-S, DELTA, LOOKAHEAD.

**Page SIDECHAIN** : module standard. **Page SETTINGS** : comme le compresseur + plancher de détection + section « Détecteurs » (mode Peak/RMS, rapport rapide/lent) + réglage de l'aide.

---

## 7. Décisions (relecture du 2026-10-18)
1. **Mode auto du SHAPE SMOOTHING** : formule du §1 retenue telle quelle (`max(1 ms, 0,3 × min(ATTACK, SUSTAIN))`, borné à 50 ms, réglage × `2^(v/50)`).
2. **Détecteurs en Settings** : `shp_det_mode` et `shp_det_fast_ratio` retenus (§2.7), enregistrés avec le preset, non automatisables.
3. **PEAK SAFE** : abandonné. OVERSAMPLING repris (§2.5).
4. **Transport DSP → UI** : tampon partagé sans verrou (`NOVA_Decisions_transversales.md` §1) ; les sorties du §3 sont des valeurs internes, pas des paramètres d'hôte.
5. **Automation** : jeux 1 et 2 complets exposés (`NOVA_Decisions_transversales.md` §4).
6. **Sidechain** : module commun `sc_*` (`NOVA_Common_modules_v0.1.md` §2) ; `sc_integration` masqué (le shaper a ses propres suiveurs).
