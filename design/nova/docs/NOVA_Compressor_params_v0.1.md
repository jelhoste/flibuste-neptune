# NOVA Compressor — description des paramètres v0.1

Compresseur stéréo à coude réglable, ballistique à formes d'attaque et de relâchement, détection Peak / RMS, mode Feedforward / Feedback, lookahead, oversampling, sidechain interne / externe / mix / MIDI avec égaliseur de détection (passe-haut, 3 bandes, passe-bas) et trois modes de canaux (Stereo, Dual Mono, M-S).
Source : la maquette `nova_compressor_full_ui.html` (pages COMPRESSOR, SIDECHAIN, SETTINGS) et les textes d'aide `NOVA_Compressor_help_v0.1.md` (ids `comp_*` conservés, `help_short` / `help_long` repris tels quels).
Format : celui de `NOVA_TransientShaper_params_v0.2.md` §0 (rappelé en §0). Groupe `comp_` ; sidechain `sc_` (module commun, voir §2.4) ; vu-mètres `meter_` (module commun). Valeurs numériques = celles de la maquette quand elle en donne, sinon points de départ à régler à l'écoute (marqués « proposé »).

---

## 0. Format de description (générique, réutilisable)

| Champ | Sens |
|---|---|
| `id` | identifiant stable, snake_case, préfixe = groupe (jamais renommé : il sert à la sauvegarde et à l'automation) |
| `enum` | nom dans le code : `kParam` + CamelCase de l'id |
| `label` | nom affiché (court, majuscules sur les potards) |
| `type` | float / int / bool / enum / trigger / output (lecture seule) / state (chaîne) |
| `range`, `default`, `unit` | l'unité est séparée du nom |
| `scale` | linear / log / geometric / bipolar |
| `auto` | automatisable : oui / non |
| `smooth` | lissage DSP du changement de valeur (ms), ou « — » |
| `page`, `control` | où et avec quel contrôle (potard, curseur, pill, switch…) |
| `depends` | conditions de grisage / d'activation |
| `help_short`, `help_long`, `meta` | textes d'aide (écrits une seule fois, repris tels quels par la maquette et le code) |

---

## 1. Principe de traitement

Notations : `x` = niveau de la clé de détection en dB ; `T` = THRESHOLD (dB) ; `R` = RATIO ; `W` = KNEE (dB) ; `GR` = réduction de gain en dB (≥ 0) ; `fs` = fréquence de traitement (fréquence hôte × oversampling).

### 1.1 Chaîne de signal (par canal de traitement)

```
entrée ─┬───────────────────────────────────────────────► retard LOOKAHEAD ─┬─► × gain(GR) ─► × MAKEUP ─► mélange MIX ─► sortie
        │                                                                    │                                ▲
        └─► [source sidechain + GAIN/VOL/Ø] ─► EQ sidechain ─► détecteur ─► courbe statique ─► ballistique ─► GR     │
                                         (LISTEN : on entend ici)                                     dry (retardé) ┘
```

1. **Canaux** : Stereo, Dual Mono ou M-S (§1.7). Chaque « canal de traitement » (L, R, M ou S) a son détecteur et son propre jeu de réglages (§2.1).
2. **Clé de détection** : source sidechain (§1.2) → gain / volumes / phase → égaliseur de détection (§2.5) → détecteur Peak ou RMS (§1.3).
3. **Courbe statique** (§1.4) : niveau de clé → réduction de gain cible.
4. **Ballistique** (§1.5) : lissage attaque / relâchement avec formes → `GR`.
5. **Application** : l'audio, retardé de LOOKAHEAD, est multiplié par `10^(−GR/20)`, puis par MAKEUP, puis mélangé avec l'audio sec **retardé de la même durée** (§1.10).
6. **Feedforward / Feedback** (§1.6) change seulement l'endroit où la clé prend l'audio.
7. **Oversampling** (§1.9) : la chaîne « clé + gain + application » tourne à 1×, 2× ou 4× la fréquence hôte.
8. **BYPASS** : fondu de 10 ms, avec conservation du retard (la latence ne change pas).

### 1.2 Source de la clé (sidechain)

`sc_source` : Internal, External, Mix, MIDI (la maquette propose les quatre ; le texte d'aide actuel n'en cite que trois, voir §7).

| Source | Clé avant EQ | Réglages actifs |
|---|---|---|
| Internal | audio de l'entrée du plugin × `10^(sc_gain_db/20)` | GAIN (`sc_gain_db`) |
| External | entrée sidechain de l'hôte × `10^(sc_gain_db/20)` | GAIN, M/S externe (`sc_ext_ms`) |
| Mix | `± 10^(sc_comp_vol_db/20) × interne ± 10^(sc_ext_vol_db/20) × externe` (signe négatif si Ø actif) | COMP VOL, EXT VOL, Ø de chaque voie |
| MIDI | niveau dérivé de la vélocité des notes MIDI (§1.2.1) | SHAPE (`sc_midi_shape`) ; DETECT / RMS WINDOW masqués |

Aucune entrée externe branchée : la clé externe est le silence (rien n'est compressé en External). **LISTEN** (`sc_listen`) : la sortie du plugin est remplacée par la clé après EQ et gain (point marqué « LISTEN » sur le schéma), sans compression, MAKEUP ni MIX ; les vu-mètres OUT affichent ce signal.

**1.2.1 Source MIDI (proposé).** Sur réception d'une note, `v = vélocité / 127` est transformée par `v' = v^k` avec `k = 1 + 3·s` si `s ≥ 0`, `k = 1 / (1 − 3·s)` si `s < 0` (`s = sc_midi_shape / 100`, formule de la courbe tracée par la maquette). Le niveau de clé est `x = 20·log10(v')` dB (0 dBFS à vélocité 127), maintenu pendant la note puis ramené à −∞ à la fin de la note ; la ballistique du compresseur fait le reste. Canal MIDI : `sc_midi_channel`. La clé MIDI contourne l'EQ de détection et le détecteur. (Ces trois points ne sont pas tranchés par la maquette : §7.)

### 1.3 Détecteur (`comp_detect`)

- **Peak** : `x = 20·log10(|clé|)` instantané, sur le signal suréchantillonné si OVERSAMPLING est actif.
- **RMS** : `x = 10·log10( moyenne_{fenêtre}(clé²) )`, avec une fenêtre glissante rectangulaire de `comp_rms_window` ms (proposé : rectangulaire ; les fenêtres RECTANGULAR / HANN de la section METERING des Settings concernent les vu-mètres, pas ce détecteur).
- La fenêtre RMS est grisée quand DETECT = Peak.

### 1.4 Courbe statique avec coude (formule de la maquette)

Entrée `x` (dB), sortie statique `y(x)` (dB), `GR_cible = x − y(x)` :

```
si x < T − W/2        : y = x                                     (aucune réduction)
si x > T + W/2        : y = T + (x − T)/R                          (compression pleine)
sinon (dans le coude) : y = x + (1/R − 1) · (x − T + W/2)² / (2W)  (coude quadratique)
```

Si `W < 0,01 dB`, le coude est traité comme dur (division par `W` évitée). RATIO 1…10 (pas de mode limiteur : 10:1 est le maximum). La sortie affichée dans la courbe de transfert est `y(x) + MAKEUP`.

### 1.5 Ballistique : attaque, relâchement, formes

`GR_cible` est calculée à chaque échantillon. La réduction `GR` la suit avec un lissage du premier ordre dont la constante de temps dépend du sens du mouvement :

- `GR_cible > GR` : attaque, `τ = ATTACK` ; `GR_cible < GR` : relâchement, `τ = RELEASE` ; `c = exp(−1 / (τ·fs))` (τ = constante de temps à 63 %, comme dans la simulation de la maquette `exp(−Δt/τ)`).

**Formes (ATTACK SHAPE, RELEASE SHAPE, −100…+100 %, 0 = LINEAR).** Proposition de départ : le lissage se fait dans un domaine déformé par une puissance, `a = 3^(shape/100)` (EXP : `a = 1/3` à −100 % ; LOG : `a = 3` à +100 %) ; `GR_REF = 24 dB` (pleine échelle de la réduction de gain dans la maquette).

```
attaque    :  w = (GR / GR_REF)^a        ;  w ← w + (1 − c_att) · (w_cible − w)        ;  GR = GR_REF · w^(1/a)
relâchement:  G = 1 − GR / GR_REF        ;  ψ = G^a ;  ψ ← ψ + (1 − c_rel) · (ψ_cible − ψ)  ;  GR = GR_REF · (1 − ψ^(1/a))
```
(grandeurs bornées à 0…GR_REF dans le domaine déformé.) Effet : à 0 %, `a = 1` : lissage du premier ordre pur (comportement de la maquette). EXP (`a < 1`) : prise progressive puis qui s'accélère à l'attaque, remontée lente au début puis rapide au relâchement. LOG (`a > 1`) : prise vive d'emblée puis qui ralentit, remontée rapide au début puis lente. Le sens exact (EXP / LOG) et la différence avec le texte d'aide actuel (« vitesse constante en dB » au centre) sont à valider (§7).

ATTACK et RELEASE agissent aussi comme temps minimaux : en mode Feedback, voir §1.6.

### 1.6 Feedforward / Feedback (`comp_feedback_mode`)

- **Feedforward** : la clé est lue **avant** le gain (audio retardé de LOOKAHEAD non, voir §1.8 : la clé lit l'entrée non retardée).
- **Feedback** : la clé est lue **après** l'application du gain (avant MAKEUP et MIX), avec un échantillon de retard dans la boucle. Le gain dépend alors de ce qu'il a déjà produit, ce qui adoucit la réaction.
- Ratio effectif en Feedback (coude dur, régime établi) : `R_eff = 2 − 1/R` (donc ≤ 2:1 même à 10:1). L'UI trace la courbe de transfert sans l'effet du mode Feedforward (formule §1.4) ; en Feedback elle peut tracer la courbe de régime établi en résolvant `y = x − GR(y)` (§7).
- En Feedback, la source External n'a pas de sens pour la boucle (la clé externe n'est pas modifiée par le gain) : seule la part interne d'un mode Mix est rebouclée. LOOKAHEAD n'anticipe rien en Feedback (la clé est postérieure au retard) : voir §7.

### 1.7 Modes de canaux (`comp_channel_mode`)

| Mode | Canaux de traitement | Jeux de réglages utilisés | Détection |
|---|---|---|---|
| Stereo | L et R reçoivent le **même** gain | jeu L uniquement | clé liée (§comp_stereo_feed) |
| Dual Mono | L et R séparés | jeux L et R | un détecteur par canal |
| M-S | M = (L+R)/2 et S = (L−R)/2, puis décodage `L = M+S`, `R = M−S` | jeux M et S | un détecteur par canal M / S |

Stereo : `comp_stereo_feed` choisit la clé : L, R ou MIX (proposé : Peak = max(|L|,|R|), RMS = √((L²+R²)/2)). Le sélecteur de canal de la zone « SIGNAL / TRANSFER CURVE » (L / R / MIX en Stereo, L / R en Dual Mono, M / S en M-S) est une vue de l'UI : il choisit quel jeu de potards s'affiche et édite, sauf en Stereo où il pilote `comp_stereo_feed`.
En M-S, la clé interne est le M ou le S de l'audio ; en External, voir `sc_ext_ms`.

### 1.8 Lookahead et latence

`comp_lookahead` (0…100 ms) retarde l'audio (et l'audio sec du mélange) de `L = lookahead × fs_hôte` échantillons ; la clé lit l'entrée **non** retardée. Le compresseur voit donc arriver les pics `L` ms avant qu'ils atteignent le gain. Latence reportée à l'hôte : `lookahead + latence de l'oversampling` (§1.9). À 0, pas de retard ajouté.

### 1.9 Oversampling (`comp_oversampling`)

None, 2x ou 4x. Le suréchantillonnage (filtres polyphasés, proposé : demi-bandes FIR à phase linéaire) concerne le détecteur (crêtes inter-échantillons) et la multiplication par le gain (bandes latérales d'attaques très rapides). Le retard des filtres s'ajoute à la latence reportée (`comp_report_latency` actif). L'audio sec du mélange est retardé de la même durée.

### 1.10 Makeup, mélange, sortie

`wet = audio × 10^(−GR/20) × 10^(MAKEUP/20)` ; `sortie = (1 − MIX/100) × sec + (MIX/100) × wet` ; le sec est retardé comme le chemin traité (lookahead + oversampling). MAKEUP et MIX font partie du jeu de chaque canal (§2.1). Il n'y a ni gain d'entrée, ni gain de sortie, ni gain automatique dans la maquette (voir « absent de la maquette », §5).

---

## 2. Paramètres

### 2.1 Jeux de réglages par canal (page COMPRESSOR)

Neuf réglages, répétés pour quatre canaux de traitement. Le jeu **L** est le jeu principal (seul actif en Stereo) et porte les ids sans suffixe (ceux du fichier d'aide). Les jeux R, M, S ajoutent le suffixe `_r`, `_m`, `_s` à l'id. Les potards de la page COMPRESSOR affichent le jeu du canal choisi dans le sélecteur de la courbe.

| id (jeu L) | label | type | plage | défaut | unité · échelle | auto | smooth | page · contrôle |
|---|---|---|---|---|---|---|---|---|
| `comp_threshold` | THRESHOLD | float | −60…0 | −22,0 | dB · linear | oui | 20 ms | COMPRESSOR · potard ; point déplaçable sur la courbe |
| `comp_ratio` | RATIO | float | 1…10 | 2,8 | :1 · linear | oui | 20 ms | COMPRESSOR · potard |
| `comp_knee` | KNEE | float | 0…24 | 6,0 | dB · linear | oui | 20 ms | COMPRESSOR · curseur sous THRESHOLD |
| `comp_attack` | ATTACK | float | 0,1…100 | 15,0 | ms · log (proposé) | oui | 20 ms | COMPRESSOR · potard |
| `comp_attack_shape` | ATTACK SHAPE | float | −100…+100 | 0 | % · bipolar | oui | 20 ms | COMPRESSOR · curseur « SHAPE » sous ATTACK |
| `comp_release` | RELEASE | float | 10…2000 | 180 | ms · log (proposé) | oui | 20 ms | COMPRESSOR · potard |
| `comp_release_shape` | RELEASE SHAPE | float | −100…+100 | 0 | % · bipolar | oui | 20 ms | COMPRESSOR · curseur « SHAPE » sous RELEASE |
| `comp_makeup` | MAKEUP | float | −12…+12 | +3,2 | dB · linear | oui | 20 ms | COMPRESSOR · potard |
| `comp_mix` | MIX | float | 0…100 | 100 | % · linear | oui | 20 ms | COMPRESSOR · potard (DRY … WET) |

Jeux supplémentaires, mêmes plages et défauts :

| Jeu | Suffixe | Ids | Actif quand |
|---|---|---|---|
| R | `_r` | `comp_threshold_r`, `comp_ratio_r`, `comp_knee_r`, `comp_attack_r`, `comp_attack_shape_r`, `comp_release_r`, `comp_release_shape_r`, `comp_makeup_r`, `comp_mix_r` | `comp_channel_mode` = Dual Mono |
| M | `_m` | idem avec `_m` | `comp_channel_mode` = M-S |
| S | `_s` | idem avec `_s` | `comp_channel_mode` = M-S |

Le jeu L est actif en Stereo et en Dual Mono (canal gauche). Les jeux non actifs conservent leur valeur (ils ne sont pas remis à zéro quand on change de mode) ; l'UI les affiche grisés ou ne les propose pas. Dans la maquette, les jeux R, M et S démarrent comme copies du jeu L.

Enums : `kParamCompThreshold`, `kParamCompThresholdR`, `kParamCompThresholdM`, `kParamCompThresholdS`, etc.

#### comp_threshold
- type : float · range : −60…0 · default : −22,0 · unit : dB · scale : linear · auto : oui · smooth : 20 ms
- page : COMPRESSOR · control : potard et point sur la courbe de transfert (glissé horizontal)
- depends : aucune
- help : pas de « ? »
- meta : −60…0 dB · Défaut −22 · Automatisable : oui (la maquette laisse glisser le point jusqu'à +12 dB ; la plage du potard s'arrête à 0, voir §7)

#### comp_ratio
- type : float · range : 1…10 · default : 2,8 · unit : :1 · scale : linear · auto : oui · smooth : 20 ms
- page : COMPRESSOR · control : potard (affichage « 2.8:1 ») · help : pas de « ? »
- meta : 1:1…10:1 · Défaut 2,8:1 · Automatisable : oui

#### comp_knee
- type : float · range : 0…24 · default : 6 · unit : dB · scale : linear · auto : oui · smooth : 20 ms
- page : COMPRESSOR · control : curseur sous le potard THRESHOLD · help : pas de « ? »
- meta : 0…24 dB · Défaut 6 · Automatisable : oui

#### comp_attack
- type : float · range : 0,1…100 · default : 15 · unit : ms · scale : log (proposé ; le potard de la maquette est linéaire) · auto : oui · smooth : 20 ms
- page : COMPRESSOR · control : potard · help : pas de « ? »
- meta : 0,1…100 ms · Défaut 15 · Automatisable : oui

#### comp_attack_shape
- enum : `kParamCompAttackShape` · label : ATTACK SHAPE · type : float
- range : −100…+100 · default : 0 · unit : % · scale : bipolar · auto : oui · smooth : 20 ms
- page : COMPRESSOR · control : curseur bipolaire « SHAPE » avec icônes EXP (gauche) et LOG (droite) ; valeur affichée « LINEAR » (|v| < 4 %), « EXP x % » (négatif), « LOG x % » (positif)
- depends : aucune
- help_short : Courbe de la prise de compression (LINEAR / EXP / LOG).
- help_long : Choisit la trajectoire du gain pendant le temps d'attaque. Au centre (LINEAR), le gain descend à vitesse constante en dB. D'un côté (EXP), la prise est progressive puis s'accélère : plus discrète sur le début du son. De l'autre (LOG), elle est vive d'emblée puis ralentit : plus nette, elle freine tout de suite l'attaque.
- meta : Curseur −100…+100 % · LINEAR au centre

#### comp_release
- type : float · range : 10…2000 · default : 180 · unit : ms · scale : log (proposé ; le potard de la maquette est linéaire) · auto : oui · smooth : 20 ms
- page : COMPRESSOR · control : potard · help : pas de « ? »
- meta : 10…2000 ms · Défaut 180 · Automatisable : oui

#### comp_release_shape
- enum : `kParamCompReleaseShape` · label : RELEASE SHAPE · type : float
- range : −100…+100 · default : 0 · unit : % · scale : bipolar · auto : oui · smooth : 20 ms
- page : COMPRESSOR · control : curseur bipolaire « SHAPE » sous RELEASE (même affichage que ATTACK SHAPE)
- help_short : Courbe du retour à 0 dB (LINEAR / EXP / LOG).
- help_long : Choisit la trajectoire du gain pendant le relâchement. Au centre (LINEAR), il remonte à vitesse constante en dB. EXP et LOG changent la rapidité du début et de la fin de la remontée : une remontée plus rapide au départ rend le compresseur plus discret sur les notes courtes, une remontée plus lente en fin de course la rend plus douce et musicale.
- meta : Curseur −100…+100 % · LINEAR au centre

#### comp_makeup
- type : float · range : −12…+12 · default : +3,2 · unit : dB · scale : linear · auto : oui · smooth : 20 ms
- page : COMPRESSOR · control : potard (couleur secondaire) · help : pas de « ? »
- meta : −12…+12 dB · Défaut +3,2 · Automatisable : oui

#### comp_mix
- type : float · range : 0…100 · default : 100 · unit : % · scale : linear · auto : oui · smooth : 20 ms
- page : COMPRESSOR · control : potard (repères DRY / WET) · help : pas de « ? »
- meta : 0…100 % · Défaut 100 · Automatisable : oui

### 2.2 Barre du bas (visible sur les trois pages)

| id | label | type | plage | défaut | unité · échelle | auto | smooth | contrôle |
|---|---|---|---|---|---|---|---|---|
| `comp_channel_mode` | STEREO / DUAL MONO / M-S | enum | 0 Stereo, 1 Dual Mono, 2 M-S | 0 | — | non | fondu 10 ms | pill cyclique |
| `comp_feedback_mode` | FEEDFORWARD / FEEDBACK | enum | 0 Feedforward, 1 Feedback | 0 | — | non | fondu 10 ms | pill (deux états) |
| `comp_lookahead` | LOOKAHEAD | float | 0…100 | 1,0 | ms · linear | non | — | valeur glissée verticalement (appui long : saisie) ; voyant éteint à 0 |
| `comp_oversampling` | OVERSAMPLING | enum | 0 None, 1 2x, 2 4x | 2 (4x) | — | non | — | pill cyclique (affiche « - » si None) ; voyant éteint si None |
| `comp_stereo_feed` | STEREO FEED | enum | 0 L, 1 R, 2 MIX | 2 (proposé) | — | non | — | sélecteur de canal de la zone courbe, en mode Stereo uniquement |
| `comp_bypass` | BYPASS | bool | — | 0 | — | oui | fondu 10 ms | bouton « marche/arrêt » de l'en-tête (icône sans action dans la maquette, proposé) |

Enums : `kParamCompChannelMode`, `kParamCompFeedbackMode`, `kParamCompLookahead`, `kParamCompOversampling`, `kParamCompStereoFeed`, `kParamCompBypass`.
Changer de mode de canaux, de mode feedback ou de lookahead en lecture provoque un fondu de 10 ms pour éviter les clics (et, pour le lookahead, un changement de latence reportée).

#### comp_channel_mode
- label : STEREO / DUAL MONO / M-S · type : enum · auto : non
- help_short : Comment les canaux sont compressés.
- help_long : Stereo : les deux canaux reçoivent la même réduction de gain, l'image reste stable. Dual Mono : chaque canal est compressé séparément, l'image peut se déplacer. M-S : le centre (mid) et les côtés (side) sont compressés séparément, pratique pour contrôler la largeur.
- meta : Choix : Stereo, Dual Mono, M-S

#### comp_feedback_mode
- label : FEEDFORWARD / FEEDBACK · type : enum · auto : non
- depends : sans effet si `sc_source` = External (§1.6)
- help_short : Où la détection prend le signal.
- help_long : Feedforward mesure le signal avant la compression : réaction précise, son plus moderne. Feedback le mesure après : réaction plus douce et plus musicale, caractère « vintage » ; le ratio effectif y est un peu moins strict.
- meta : Choix : Feedforward, Feedback

#### comp_lookahead
- label : LOOKAHEAD · type : float · range : 0…100 · default : 1,0 · unit : ms · scale : linear · auto : non
- depends : sans effet en Feedback (§1.6, à valider)
- help_short : Anticipe les attaques ; ajoute de la latence.
- help_long : Retarde le signal pour que le compresseur voie arriver les pics et réagisse avant eux. Plus précis sur les attaques rapides, mais ajoute une latence, reportée à l'hôte.
- meta : Latence = valeur réglée

#### comp_oversampling
- label : OVERSAMPLING · type : enum · range : None, 2x, 4x · default : 4x · auto : non
- help_short : Traitement à fréquence plus élevée.
- help_long : Le signal est traité à une fréquence 2× ou 4× supérieure pour limiter les artefacts de repliement des attaques rapides. Plus la valeur est élevée, plus la charge processeur et la latence augmentent.
- meta : Choix : None, 2x, 4x

#### comp_stereo_feed
- label : (L / R / MIX) · type : enum · default : MIX (proposé ; la maquette s'ouvre sur « L », car le même bouton sert aussi de vue) · auto : non · page : COMPRESSOR · control : bouton de la zone courbe
- depends : actif en mode Stereo seulement ; en Dual Mono et M-S, le bouton ne fait que changer le jeu affiché
- help : pas de « ? » (la maquette ne le prévoit pas)

#### comp_bypass
- type : bool · default : 0 · auto : oui · control : bouton d'en-tête (fondu de 10 ms, latence conservée)

### 2.3 Détection (page SIDECHAIN)

| id | label | type | plage | défaut | unité · échelle | auto | smooth | contrôle · depends |
|---|---|---|---|---|---|---|---|---|
| `comp_detect` | DETECT | enum | 0 Peak, 1 RMS | 0 | — | non | — | pill ; masqué si source = MIDI |
| `comp_rms_window` | RMS WINDOW | float | 1…300 | 10 | ms · linear (proposé : log) | oui | 20 ms | curseur ; grisé si DETECT = Peak ; masqué si source = MIDI ; appui long : saisie |

Enums : `kParamCompDetect`, `kParamCompRmsWindow`.

#### comp_detect
- label : DETECT / RMS WINDOW (l'aide couvre le pill DETECT et le curseur RMS WINDOW) · type : enum
- help_short : Mesure de niveau du détecteur.
- help_long : Peak réagit aux crêtes instantanées : contrôle serré des pics. RMS suit l'énergie moyenne : plus proche de la perception, plus lisse. La fenêtre RMS règle la durée de cette moyenne : courte = plus réactif, longue = plus doux.
- meta : Choix : Peak, RMS

#### comp_rms_window
- type : float · range : 1…300 · default : 10 · unit : ms · auto : oui · smooth : 20 ms
- page : SIDECHAIN · control : mini-curseur sous le pill DETECT · depends : grisé (opacité réduite, non interactif) si `comp_detect` = Peak
- help : couvert par `comp_detect`

### 2.4 Sidechain et égaliseur de détection (page SIDECHAIN)

Module commun `sc_*`, décrit une seule fois dans `NOVA_Common_modules_v0.1.md` §2 (fusion du 2026-10-18). `sc_source` est devenu `sc_source`. Le compresseur force `sc_enable` à 1 et n'a pas d'intégration (`sc_integration` masqué). Aide en ligne de `sc_source` : voir le module commun.

### 2.6 Settings (non automatisables, enregistrés avec le preset)

Réglages DSP et mesure de la page SETTINGS. Les réglages purement UI (thème, contraste, échelle, langue, boutons « ? », sensibilité des potards, double-clic, molette, dossier de presets, import / export, comportement au chargement) sont en §5 ; la norme LUFS et les profils RMS des vu-mètres appartiennent au module `meter_*` (§4).

| id | label | type | plage | défaut | contrôle |
|---|---|---|---|---|---|
| `comp_precision` | Internal precision | enum | 0 AUTO (DAW), 1 32-bit, 2 64-bit | 0 | AUDIO / DSP |
| `comp_report_latency` | Report latency to host | bool | — | 1 | AUDIO / DSP (« compense automatiquement l'oversampling ») |
| `comp_spectrum_point` | Spectrum analyzer — measurement point | enum | 0 PRE-EQ, 1 POST-EQ | 1 | METERING |
| `comp_spectrum_decay` | Spectrum analyzer — decay speed | float | 0…2000 ms (pas 10) | 300 | METERING (stepper) |
| `comp_spectrum_peak_hold` | Spectrum analyzer — peak hold | bool | — | 1 | METERING |
| `comp_spectrum_fft` | Spectrum analyzer — FFT size | enum | 1024, 2048, 4096 | 2048 | METERING |
| `comp_spectrum_channel` | Spectrum analyzer — channel | enum | 0 STEREO, 1 LEFT, 2 RIGHT, 3 MID, 4 SIDE | 0 | METERING |

Lecture seule : « Sample rate » (valeur de l'hôte, 48 000 Hz dans la maquette), « Version ». `sc_midi_channel` (MIDI trigger channel) est en §2.4.

Enums : `kParamCompPrecision`, `kParamCompReportLatency`, `kParamCompSpectrumPoint`, `kParamCompSpectrumDecay`, `kParamCompSpectrumPeakHold`, `kParamCompSpectrumFft`, `kParamCompSpectrumChannel`.

#### comp_report_latency
- type : bool · default : 1 · auto : non
- Actif : la latence due à l'oversampling est ajoutée à la latence reportée (la latence du lookahead est toujours reportée, §1.8). Inactif : l'hôte ne compense que le lookahead (proposé, voir §7).
- help : pas de « ? »

#### comp_spectrum_point
- label : Spectrum analyzer — measurement point · type : enum · auto : non
- help_short : Où le spectre affiché est mesuré.
- help_long : PRE-EQ mesure le signal avant l'égaliseur de sidechain, POST-EQ après. Pour vérifier l'effet de l'égaliseur sur ce que le détecteur écoute, comparer les deux.
- meta : Choix : PRE-EQ, POST-EQ · Affichage uniquement

---

## 3. Valeurs par défaut et presets

Valeurs par défaut des paramètres : celles de la colonne « défaut » des tableaux ci-dessus ; ce sont les valeurs de l'état de la maquette, qui porte le nom de preset « Soft Vocal Master ». Aucun autre preset d'usine n'est défini par la maquette (liste à compléter).

| Preset | THRESHOLD | RATIO | KNEE | ATTACK | RELEASE | SHAPES | MAKEUP | MIX | Autres |
|---|---|---|---|---|---|---|---|---|---|
| Soft Vocal Master (état de la maquette) | −22 dB | 2,8:1 | 6 dB | 15 ms | 180 ms | LINEAR | +3,2 dB | 100 % | Stereo, Feedforward, Internal, Peak, lookahead 1,0 ms, oversampling 4x, EQ actif (HP 80 Hz / 12 dB·oct, LP 16 kHz / 12 dB·oct, bandes à 250 / 1200 / 5000 Hz, 0 dB, Q 1) |

Les jeux R, M et S sont initialisés comme copies du jeu L. Un changement de réglage fait passer le preset en « modifié » (astérisque dans l'en-tête). A/B/C/D mémorisent quatre jeux de réglages (UI seulement, §5).

---

## 4. Sorties d'affichage et de mesure (lecture seule, non automatisables)

| id | Contenu | Plage |
|---|---|---|
| `comp_gr_out_1` | réduction de gain courante, canal 1 (L ou M ; en Stereo, la valeur commune), en dB positifs | 0…+30 |
| `comp_gr_out_2` | réduction de gain courante, canal 2 (R ou S ; en Stereo, égale à `comp_gr_out_1`) | 0…+30 |
| `comp_scope_peak_1` | crête de l'audio d'entrée sur la fenêtre d'une image de l'UI, canal 1 (valeur absolue linéaire) | 0…1 |
| `comp_scope_peak_2` | idem, canal 2 | 0…1 |
| `comp_latency_out` | latence reportée à l'hôte (lookahead + oversampling) | 0…N échantillons |

Ces sorties alimentent la zone « SIGNAL / TRANSFER CURVE » : la maquette trace, derrière la courbe de transfert, l'**historique de l'audio** (220 barres ; 0 de l'audio calé sur le bas du graphe, crête maximale sur le haut) et, par-dessus, l'**historique de la réduction de gain** sous forme de ligne rouge qui descend du haut du graphe (échelle 0…24 dB, `GR_MAXDB = 24`). L'UI construit les deux historiques à partir d'une valeur par image (≈ 60 Hz) : c'est le même problème de transport DSP → UI que pour le Transient Shaper (tampon partagé ou séries sous-échantillonnées, à décider). Le sélecteur de la zone (L / R / MIX, L / R, M / S) choisit quel canal est tracé ; MIX = maximum des deux canaux.

Tout le reste est calculé par l'UI à partir des paramètres : courbe de transfert (formule §1.4 avec MAKEUP ajouté, grille de −60 à +12 dB en entrée), position du point THRESHOLD, réponse de l'EQ de détection (recalculée avec les mêmes biquads que le DSP), grille de fréquences.
**Spectre de l'analyseur** (page SIDECHAIN, 40 bandes sur une échelle logarithmique de 20 Hz à 20 kHz dans la maquette ; FFT 1024 / 2048 / 4096 en Settings) : calculé par le DSP au point `comp_spectrum_point`, sur le canal `comp_spectrum_channel`, et transmis à l'UI (même question de transport que ci-dessus). Pas de paramètre par bande : un tableau de valeurs en dB.

**Module vu-mètres `meter_*`** : identique aux autres plugins, voir `NOVA_Common_modules_v0.1.md` (non redécrit ici). IN = entrée du plugin (avant tout traitement), OUT = sortie finale (après MIX). Spécificités montrées par la maquette du compresseur, à reprendre dans le module commun :
- La barre centrale est un **LRA** (valeur en LU, « 6.2 LU » dans la maquette), calculé selon la norme choisie en Settings : EBU R128 ou ITU-R BS.1770 (`meter_lufs_standard`) ;
- indicateur de saturation (petit voyant) sur IN et sur OUT : allumé au-delà de 0,88 en amplitude (≈ −1,1 dBFS) dans la maquette ;
- bouton PEAK sur IN et sur OUT, qui sélectionne le mode d'affichage (crête avec maintien ou profils RMS) ;
- **profils RMS** gérés en Settings (METERING) : nom, durée en ms (1…2000), fenêtre RECTANGULAR ou HANN, attaque (0…1000 ms), relâchement (0…2000 ms) ; par défaut un profil « RMS1 » (250 ms, RECTANGULAR, 10 ms, 100 ms). Ils s'intègrent au cycle du bouton PEAK. Ils appartiennent au module `meter_*`, pas au compresseur.

---

## 5. UI seulement (non paramètres)

- A/B/C/D : quatre jeux de comparaison (la maquette cycle A → B → C → D → A) ; A = jeu automatisable, B, C, D = jeux temporaires (même logique que les autres plugins).
- Vue de la courbe (sélecteur de canal L / R / MIX ; L / R ; M / S) : édite le jeu affiché, sauf `comp_stereo_feed` en Stereo (§2.2).
- Menu de presets (« Soft Vocal Master »), thème (Dark, Hammond, Light, Contraste+), contraste (niveaux 0 à 4), échelle de l'interface, langue (FR / EN), boutons « ? » (toujours visibles, au survol, masqués), sensibilité des potards (NORMAL / FINE), double-clic = réinitialiser, molette sur les potards.
- Settings PRESETS : dossier par défaut, comportement au chargement (LAST USED / INIT), IMPORT / EXPORT. Section ABOUT : version, documentation, support, mises à jour.

Absent de la maquette (donc non spécifié ici) : gain d'entrée, gain de sortie, gain automatique (auto-gain), liaison stéréo en pourcentage, delta (écoute de la différence), filtre passe-haut de détection hors EQ, bouton de désactivation du sidechain, mode limiteur, ratio infini, plancher de détection, type de bande d'EQ.

---

## 6. Organisation de l'interface (gabarit 1160 × 780, mêmes hauteurs de conteneurs sur les 3 pages)

**Page COMPRESSOR**
- En-tête : logo, preset, contraste, thème, bouton marche/arrêt (bypass).
- Colonne gauche : vu-mètres IN / LRA / OUT.
- Panneau central « SIGNAL / TRANSFER CURVE » : audio en fond, ligne de réduction de gain, courbe de transfert (entrée −60…+12 dB, sortie −60…+12 dB), ligne verticale pointillée et zone teintée à droite du seuil, point THRESHOLD déplaçable ; bouton de canal en haut à droite.
- Rangée de 6 potards : THRESHOLD (+ KNEE), RATIO, ATTACK (+ SHAPE), RELEASE (+ SHAPE), MAKEUP, MIX.
- Barre du bas : A/B/C/D, STEREO / DUAL MONO / M-S, FEEDFORWARD / FEEDBACK, LOOKAHEAD, OVERSAMPLING, chacun avec son « ? ».

**Page SIDECHAIN**
- Panneau central « SIDECHAIN SIGNAL » : pill de source (« ? »), mini-contrôles (slots GAIN / COMP VOL / EXT VOL / SHAPE, Ø), DETECT + RMS WINDOW, LISTEN ; en dessous, l'égaliseur (EQ, SPECTRUM, M/S en External) avec courbe, bandes colorées, spectre.
- Rangée de 5 zones : HIGH-PASS (FREQ, SLOPE), BAND 1, BAND 2, BAND 3 (GAIN, FREQ, Q), LOW-PASS (FREQ, SLOPE).

**Page SETTINGS** : GENERAL, AUDIO / DSP, METERING, PRESETS, INTERACTION, ABOUT. Barre du bas identique aux autres pages.

---

## 7. Points à valider

1. **Formes EXP / LOG et « LINEAR »** : le texte d'aide dit « vitesse constante en dB » au centre, alors que la maquette (simulation du tracé de GR) utilise un lissage du premier ordre (`exp(−Δt/τ)`) et ignore les formes. §1.5 propose le premier ordre au centre et une déformation par puissance pour EXP / LOG. Sens exact de EXP / LOG pour l'attaque et le relâchement à confirmer (la note du fichier d'aide soulève déjà la question). À noter : la courbe de la source MIDI de la maquette (`y = t^k`, k > 1 pour « LOG ») est convexe, donc à l'opposé de l'icône LOG.
2. **Définition de ATTACK / RELEASE** : constante de temps à 63 % (comme la maquette) ou temps pour atteindre 90 % ; échelle des potards (la maquette est linéaire sur 0,1…100 ms et 10…2000 ms ; le fichier propose log).
3. **Feedback** : ratio effectif `2 − 1/R` (≤ 2:1) ; faut-il compenser (courbe tracée en régime établi, ou topologie différente) ? Lookahead et source External sans effet en Feedback : griser les contrôles ?
4. **`comp_stereo_feed`** : la maquette n'a pas de contrôle explicite ; la variable `sidechainFeedChannel` (L / R / MIX, défaut L) n'a pas d'effet. Valeur proposée : MIX par défaut, Peak = max, RMS = somme en puissance. Confirmer que c'est bien le comportement voulu en Stereo.
5. **Source MIDI** (quatrième source de la maquette, absente du texte d'aide) : correspondance vélocité → niveau de clé (§1.2.1), maintien pendant la note, contournement de l'EQ et du détecteur ; mettre à jour `sc_source` (ajouter « MIDI ») dans le fichier d'aide.
6. **`sc_ext_ms`** : sens du switch « M/S » en External (§2.4), et effet réel selon le mode de canaux.
7. **EQ de détection** : pentes continues dans la maquette (6…48 dB/oct) contre des pas de 6 dB/oct proposés ; type des bandes (cloche) ; échelle de Q (log ?) ; positionnement du GAIN de clé avant ou après l'EQ (proposé : avant).
8. **Plage de THRESHOLD** : le point de la courbe peut aller jusqu'à +12 dB dans la maquette, le potard s'arrête à 0 dB (plage retenue : −60…0).
9. **Latence** : filtres d'oversampling (phase linéaire ou minimale), latence exacte par mode ; périmètre de `comp_report_latency` (oversampling seulement, ou lookahead compris) ; comportement du lookahead de 0 à 100 ms en lecture (changement de latence = reparamétrage de l'hôte).
10. **Fenêtre RMS** : fenêtre rectangulaire glissante (proposé) ; échelle linéaire (maquette) ou log ; valeur 1…300 ms.
11. **Jeux par canal** : 4 jeux × 9 réglages = 36 paramètres exposés à l'automation, dont 27 inutiles en Stereo : tout exposer ou n'exposer R / M / S qu'au mode correspondant ? Mode actuel de la maquette : R, M, S commencent comme copies de L.
12. **Transport DSP → UI** des historiques (signal, GR) et du spectre (§4), à décider avec les autres plugins.
13. **Alignement avec `NOVA_Common_modules_v0.1.md`** : noms des ids `sc_*` et `meter_*` (dont LRA, saturation IN/OUT, profils RMS) à reprendre ou renommer une fois ce fichier écrit.
14. **Défauts d'usine** : les défauts de la colonne « défaut » sont ceux de la maquette (preset « Soft Vocal Master »), pas des valeurs « neutres » de compresseur (par exemple MAKEUP +3,2 dB par défaut) ; à confirmer.
15. **BYPASS** : l'icône d'en-tête n'est pas câblée dans la maquette ; `comp_bypass` est proposé.
