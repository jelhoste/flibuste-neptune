# NOVA Equalizer — description des paramètres v0.1

Égaliseur paramétrique à **bandes libres** (jusqu'à 16), toujours en **phase minimum**. Chaque bande a son type (Bell, Low Shelf, High Shelf, High Pass, Low Pass), sa fréquence, son gain, son Q ou sa pente,
un interrupteur ON et, pour les types à gain, un mode **dynamique** (gain qui suit le niveau du signal dans la bande). Réglages communs : mode de canaux (Stereo / Dual Mono / M-S), oversampling, bypass, analyseur de spectre.
Format de description : celui de `NOVA_TransientShaper_params_v0.2.md` §0 (reprise au §0 ci-dessous). Groupe `eq_` ; paramètres de bande `eq_bN_*` (N = 1…16).
Les textes d'aide déjà écrits (`NOVA_Equalizer_help_v0.1.md`) sont repris **tels quels** avec leurs ids ; les autres textes d'aide de ce fichier sont des propositions à ajouter à ce fichier d'aide (voir §9).
Valeurs numériques non données par la maquette = points de départ à régler à l'écoute et listés au §9.

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

Les textes d'aide sont écrits une seule fois ; la maquette HTML et le code les reprennent tels quels. Pour les paramètres répétés par bande, l'aide est écrite une seule fois (id sans `bN_`, comme pour `dly_tN_*` du delay) ;
exceptions conservées du fichier d'aide : `eq_dynamic` (aide de `eq_bN_dynamic`) et `eq_band_type` (aide de `eq_bN_type` et `eq_bN_slope`).

**Schéma d'ids des bandes** : `eq_bN_<champ>`, enum `kParamEqBN<Champ>` (ex. `kParamEqB3DynThresh`). 16 emplacements fixes ; une bande « existe » quand son `eq_bN_used` vaut 1 (« + ADD BAND » prend le premier emplacement libre, « DELETE BAND » remet l'emplacement à ses valeurs par défaut).

---

## 1. Principe de traitement

### 1.1 Chaîne globale

`entrée → [mode de canaux : L/R ou encodage M-S] → [suréchantillonnage ×OS] → bandes en cascade → [sous-échantillonnage] → [décodage M-S] → sortie`, avec bypass en fondu de 10 ms.
Il n'y a **pas** de gain d'entrée, de gain de sortie, d'auto-gain, de solo de bande, de phase linéaire ni de mode stéréo par bande dans la maquette : ils ne sont pas décrits ici (voir §9). Les vu-mètres IN et OUT sont placés à l'entrée et à la sortie du plugin.

### 1.2 Mode de canaux (`eq_channel_mode`)

Chaque bande porte un champ `eq_bN_channel` (valeur 0 = « A », 1 = « B ») dont le sens dépend du mode :

| Mode | Bandes « A » | Bandes « B » |
|---|---|---|
| Stereo | traitent L **et** R de la même façon (le champ est ignoré : toutes les bandes agissent sur les deux canaux) | — |
| Dual Mono | traitent **L** | traitent **R** |
| M-S | traitent **M** (centre) | traitent **S** (côtés) |

Encodage M-S sans changement de niveau : `M = (L + R) / 2`, `S = (L − R) / 2` ; décodage `L = M + S`, `R = M − S` (une cascade sans bande donne exactement l'entrée).
Chaque canal traité (L, R, M ou S) a sa **propre cascade** : les bandes qui lui sont assignées, dans l'ordre des emplacements. En Stereo, une seule cascade (toutes les bandes) est calculée une fois par canal, avec les mêmes coefficients.
Un changement de mode fait un fondu croisé de 20 ms entre l'ancienne et la nouvelle chaîne (pas de clic).

### 1.3 Topologie d'une bande : biquads en cascade

Chaque bande est un ou plusieurs filtres du 2ᵉ ordre (biquads) en forme **transposée directe II**, calculés en double précision (ou en simple précision si `eq_precision` = 32-BIT). Les bandes d'un même canal se suivent en cascade, dans l'ordre des emplacements ; une bande désactivée (`enabled` = 0) est contournée par un fondu de 10 ms.
Forme générale (coefficients normalisés par `a0`) : `y[n] = b0·x[n] + b1·x[n−1] + b2·x[n−2] − a1·y[n−1] − a2·y[n−2]`.

Notations : `fs'` = fréquence d'échantillonnage **interne** (fs de l'hôte × facteur d'oversampling), `ω0 = 2π · f0 / fs'`, `cosω = cos ω0`, `sinω = sin ω0`, `f0` = `eq_bN_freq` bornée à `0,49 · fs'`.
`α = sinω / (2Q)` avec `Q = eq_bN_q`, `A = 10^(G/40)` avec `G` = gain **effectif** de la bande en dB (`eq_bN_gain`, plus le gain dynamique si la bande est dynamique, §1.5). Formules type RBJ (« Audio EQ Cookbook ») :

| Type | b0 | b1 | b2 | a0 | a1 | a2 |
|---|---|---|---|---|---|---|
| **Bell** | `1 + αA` | `−2cosω` | `1 − αA` | `1 + α/A` | `−2cosω` | `1 − α/A` |
| **Low Shelf** | `A[(A+1) − (A−1)cosω + 2√A·α]` | `2A[(A−1) − (A+1)cosω]` | `A[(A+1) − (A−1)cosω − 2√A·α]` | `(A+1) + (A−1)cosω + 2√A·α` | `−2[(A−1) + (A+1)cosω]` | `(A+1) + (A−1)cosω − 2√A·α` |
| **High Shelf** | `A[(A+1) + (A−1)cosω + 2√A·α]` | `−2A[(A−1) + (A+1)cosω]` | `A[(A+1) + (A−1)cosω − 2√A·α]` | `(A+1) − (A−1)cosω + 2√A·α` | `2[(A−1) − (A+1)cosω]` | `(A+1) − (A−1)cosω − 2√A·α` |
| **High Pass** (section 2ᵉ ordre) | `(1+cosω)/2` | `−(1+cosω)` | `(1+cosω)/2` | `1 + α_k` | `−2cosω` | `1 − α_k` |
| **Low Pass** (section 2ᵉ ordre) | `(1−cosω)/2` | `1 − cosω` | `(1−cosω)/2` | `1 + α_k` | `−2cosω` | `1 − α_k` |

Pour les shelves, le paramètre Q règle la raideur de la transition (`α = sinω / 2Q`, forme « Q » du Cookbook) ; Q = 0,7 donne la forme sans dépassement des presets.

### 1.4 Filtres High Pass / Low Pass : pentes par multiples de 6 dB/oct

`eq_bN_slope` ∈ {6, 12, 18, 24, 30, 36, 42, 48} dB/oct. L'ordre est `n = slope / 6`. La réponse est de type **Butterworth** (plate dans la bande passante, sans bosse), obtenue par cascade :

- `⌊n/2⌋` sections du 2ᵉ ordre (formules HP / LP du tableau ci-dessus), la section k (k = 1…⌊n/2⌋) ayant `Q_k = 1 / (2·cos((2k − 1)·π / (2n)))` et `α_k = sinω / (2·Q_k)`.
  Exemples : 12 dB/oct → Q = 0,7071 ; 24 dB/oct → Q = 0,5412 puis 1,3066 ; 48 dB/oct → quatre sections.
- Si `n` est impair (6, 18, 30, 42 dB/oct) : **une section du 1ᵉʳ ordre** en plus, avec `K = tan(ω0 / 2)` :
  - Low Pass : `b0 = b1 = K/(1+K)`, `a1 = (K−1)/(K+1)`.
  - High Pass : `b0 = 1/(1+K)`, `b1 = −1/(1+K)`, `a1 = (K−1)/(K+1)`.
- À 12 dB/oct, la fréquence `f0` est la fréquence de coupure à −3 dB ; avec une pente plus forte, c'est aussi la fréquence à −3 dB (propriété de Butterworth).

Les filtres n'ont ni gain ni mode dynamique ; leur Q n'est pas exposé (la pente le remplace).

### 1.5 Bande dynamique

Réservée aux types à gain (Bell, Low Shelf, High Shelf) : `eq_bN_dynamic` = 1. Le gain de la bande devient `G_eff = gain + env × range` (en dB), où `env` ∈ [0 ; 1] est le niveau de dépassement lissé :

1. **Détecteur** : la clé est le signal d'entrée de la cascade **avant toutes les bandes**, filtré dans la zone de la bande : un passe-bande (RBJ, gain de crête 0 dB) de fréquence `f0` et de Q `eq_bN_q` pour une Bell ;
   un passe-haut du 2ᵉ ordre à `f0` pour un High Shelf et un passe-bas du 2ᵉ ordre à `f0` pour un Low Shelf. Niveau instantané `L_dB = 20·log10(|x_filtré|)` (valeur absolue, sans fenêtre de moyenne : c'est ce que simule la maquette).
   En Stereo, la clé est le maximum des deux canaux (les deux canaux reçoivent donc le même gain dynamique) ; en Dual Mono et M-S, chaque bande lit son propre canal.
2. **Dépassement** : `over = max(0, L_dB − THRESH)`, cible `c = min(1, over / 24)` (24 dB au-dessus du seuil = effet complet, valeur de la maquette).
3. **Ballistique** : lissage du premier ordre de `c` vers `env`, avec la constante de temps ATTACK quand `c > env` et RELEASE sinon : `env ← c + (env − c) · exp(−1 / (fs' · τ))` à chaque échantillon.
4. **Gain** : `G_eff = gain + env × range`. RANGE négatif = la bande **baisse** quand le niveau dépasse le seuil (compression ciblée, ex. sibilances) ; RANGE positif = la bande monte (expansion).
   `G_eff` est borné à ±36 dB (garde-fou, voir §9).

Les coefficients de la bande sont recalculés tous les 32 échantillons (interpolés entre deux mises à jour) tant que la bande est dynamique ; une bande statique ne recalcule qu'au changement de paramètre.

### 1.6 Lissage et changements de structure

Les mouvements de `freq`, `gain`, `q` sont lissés (20 ms, fréquence interpolée en échelle logarithmique) : les coefficients sont recalculés tous les 32 échantillons pendant un lissage.
Un changement de `type` ou de `slope` (qui change le nombre de sections) et un changement de `eq_bN_channel` ou `eq_bN_used` se font par fondu croisé de 20 ms entre l'ancien et le nouveau filtre.

### 1.7 Oversampling et latence

`eq_oversampling` : None (×1), 2x, 4x. Quand il est actif, **toute la cascade** tourne à `fs' = fs × OS` (montée en fréquence → bandes → descente en fréquence). Intérêts : les coefficients sont calculés à une fréquence plus élevée, donc la déformation des courbes près de la fréquence de Nyquist (« cramping » du Bell et des shelves) est très réduite,
et les bandes dynamiques (gain modulé) produisent moins de repliement. Les filtres de ré-échantillonnage sont des demi-bandes **à phase linéaire** (cascadées pour ×4) ; l'égaliseur lui-même reste en phase minimum. Ils ajoutent une latence fixe, en échantillons de l'hôte, qui dépend du facteur :
`latence = 0` pour None ; sinon retard de groupe des filtres de ré-échantillonnage (valeur fixée par l'implémentation DSP, exposée par `eq_latency_out`).
Si `eq_report_latency` est actif (défaut), cette latence est déclarée à l'hôte (DPF `setLatency`) ; la déclaration est mise à jour à chaque changement d'oversampling. Le chemin du bypass est retardé de la même latence (le fondu de bypass ne change pas la synchronisation).

### 1.8 Courbe de réponse affichée

L'UI calcule elle-même la courbe à partir des paramètres (pas besoin de sortie DSP), avec les **mêmes formules de coefficients** que le DSP (§1.3, §1.4, à `fs' = fs × OS`), pour 200 fréquences réparties en échelle logarithmique de 20 Hz à 20 kHz.
Pour chaque section de coefficients `b0, b1, b2, a1, a2` (a0 = 1) et `ω = 2π f / fs'` :
`H(e^jω) = (b0 + b1·e^(−jω) + b2·e^(−2jω)) / (1 + a1·e^(−jω) + a2·e^(−2jω))`, `dB = 20·log10 |H|`.
La courbe d'une bande est la somme des dB de ses sections ; la **courbe totale** est la somme des dB de toutes les bandes activées de la cascade affichée. Échelle verticale fixe ±20 dB (grille ±20, ±10, 0), horizontale 20 Hz…20 kHz, lignes de repère à 20, 50, 100, 200, 500, 1k, 2k, 5k, 10k, 20k.

- **Courbe statique** (trait épais, dégradé accent → rose) : réglage posé, avec `G = gain` (sans dynamique). Courbes fines par bande : même calcul pour la bande seule, atténuées ; masquées si la bande est désactivée ou si son gain est ~0 (|gain| < 0,05 dB).
- **Courbe « live »** : même calcul avec `G_eff` de chaque bande dynamique, obtenu par `eq_bN_dyn_gain_out` (= `G_eff − gain`). La **zone colorée** entre courbe statique et courbe live montre l'action dynamique ; la ligne principale ne bouge jamais d'elle-même.
- **Points** : x = fréquence, y = gain (0 dB pour High Pass / Low Pass). Un anneau pulsé apparaît autour d'une bande dynamique quand `|dyn_gain|` > 0,05 dB.
- **Cascade affichée** : en Stereo, toutes les bandes. En Dual Mono ou M-S, seulement les bandes du canal choisi par le bouton de la courbe (L / R ou M / S). Le bouton de la courbe est un état d'UI, pas un paramètre.
- La maquette trace des **approximations** (gaussienne pour Bell, sigmoïde pour les shelves, droite de pente pour HP/LP) : elles sont à remplacer par le calcul exact ci-dessus.

### 1.9 Analyseur de spectre

Le DSP calcule un spectre en temps réel et envoie à l'UI le tableau `eq_spectrum_out` (§5).

- **Point de mesure** (`eq_spectrum_point`) : PRE-EQ = signal avant la cascade ; POST-EQ = après (défaut).
- **Canal analysé** (`eq_analyzer_channel`) : STEREO (somme L+R / 2), LEFT, RIGHT, MID, SIDE (calculés par le DSP, même en mode Stereo).
- **FFT** (`eq_fft_size`) : 1024 / 2048 / 4096 points, fenêtre de Hann, recouvrement 50 %. Magnitude en dB regroupée en `48` bandes à répartition logarithmique de 20 Hz à 20 kHz (comme la maquette).
- **Ballistique** : montée immédiate, descente avec la constante `eq_analyzer_decay` (ms). **Peak hold** (`eq_analyzer_peak_hold`) : l'UI garde le maximum de chaque bande jusqu'à la remontée suivante.
- Désactivé (`eq_analyzer_on` = 0), le DSP ne calcule plus de FFT et l'UI n'affiche aucun spectre.

---

## 2. Paramètres d'une bande (× 16)

Pour la bande N : `eq_bN_…`, enum `kParamEqBN…`. Total : 16 × 13 = 208 paramètres de bande. Valeurs par défaut identiques pour les 16 emplacements (une bande « neuve » est une Bell à 1 kHz, 0 dB, Q = 1) ;
sauf `used` (voir §6). Les bandes dynamiques gardent leurs réglages dynamiques même si `dynamic` est à 0.

| id (sans préfixe `eq_bN_`) | label | type | plage | défaut | unité · échelle | auto | smooth | page · contrôle |
|---|---|---|---|---|---|---|---|---|
| `used` | — (existence de la bande) | bool | — | 0 (voir §6) | — | non | — | EQUALIZER · « + ADD BAND » / DELETE BAND / chip × |
| `enabled` | ON | bool | — | 1 | — | oui | fondu 10 ms | EQUALIZER · switch de la barre de bande |
| `channel` | CHANNEL | enum | 0 « A », 1 « B » | 0 | — | non | fondu 20 ms | EQUALIZER · vue du canal (L/R ou M/S), voir §1.2 |
| `type` | TYPE | enum | 0 Bell, 1 Low Shelf, 2 High Shelf, 3 High Pass, 4 Low Pass | 0 (Bell) | — | non | fondu 20 ms | EQUALIZER · pill de type + « ? » |
| `freq` | FREQ | float | 20…20000 | 1000 | Hz · log | oui | 20 ms | EQUALIZER · potard, point de la courbe (axe x) |
| `gain` | GAIN | float | −18…+18 | 0 | dB · linear | oui | 20 ms | EQUALIZER · potard, point de la courbe (axe y) · absent pour HP / LP |
| `q` | Q | float | 0,15…12 | 1,0 | — · linear | oui | 20 ms | EQUALIZER · potard, molette sur le point · absent pour HP / LP |
| `slope` | SLOPE | int | 6, 12, 18, 24, 30, 36, 42, 48 | 12 | dB/oct · pas de 6 | non | fondu 20 ms | EQUALIZER · potard, molette sur le point · seulement pour HP / LP |
| `dynamic` | DYNAMIC | bool | — | 0 | — | oui | 20 ms | EQUALIZER · switch de la barre de bande + « ? » · seulement pour Bell / shelves |
| `dyn_thresh` | THRESH | float | −60…0 | −24 | dB · linear | oui | 20 ms | EQUALIZER · potard · si DYNAMIC |
| `dyn_range` | RANGE | float | −24…+24 | −6 | dB · linear | oui | 20 ms | EQUALIZER · potard · si DYNAMIC |
| `dyn_attack` | ATTACK | float | 0,5…200 | 15 | ms · linear | oui | — | EQUALIZER · potard · si DYNAMIC |
| `dyn_release` | RELEASE | float | 10…1000 | 150 | ms · linear | oui | — | EQUALIZER · potard · si DYNAMIC |

Le défaut du Q est 1,0 (bande neuve de la maquette) ; la maquette utilise 0,7 pour le High Shelf du preset (§6).

### Dépendances et règles d'interface

- `gain`, `q`, `dynamic` : actifs seulement pour Bell, Low Shelf, High Shelf (`hasGain`). Pour High Pass / Low Pass : `gain` ignoré par le DSP (comme `dynamic`) ; l'UI les remet à 0 en changeant de type (comportement de la maquette) et affiche SLOPE à la place de Q.
- `dyn_thresh`, `dyn_range`, `dyn_attack`, `dyn_release` : affichés seulement si `dynamic` = 1 et le type a un gain.
- Ordre de rotation de la pill de type dans la maquette : High Pass → Low Shelf → Bell → High Shelf → Low Pass → High Pass… (l'ordre des valeurs d'enum ci-dessus est celui du fichier d'aide).
- Limites imposées par le glisser sur la courbe (UI seulement, le potard FREQ reste 20…20000) : fréquence d'un High Pass ≤ 4 kHz, d'un Low Pass ≥ 100 Hz ; gain borné à ±18 dB.
- Molette sur un point : Q × 1,08 (vers le haut) ou ÷ 1,08, borné 0,15…12 ; pour HP / LP, pente ± 6 dB/oct, bornée 6…48.

### Blocs de description

#### eq_bN_used
- enum : `kParamEqBNUsed` · type : bool · default : 0 (voir §6) · auto : non · page : EQUALIZER · control : « + ADD BAND », « DELETE BAND », × de la chip
- depends : « + ADD BAND » est grisé quand les 16 emplacements sont utilisés
- help_short : Bande utilisée ou libre.
- help_long : Une bande libre ne traite rien. Ajouter une bande (double-clic sur la courbe, ou « + ADD BAND ») prend le premier emplacement libre ; supprimer une bande (double-clic sur son point, DELETE BAND ou la croix de sa pastille) le libère et remet ses réglages à zéro.
- meta : Structure · Non automatisable · 16 bandes maximum

#### eq_bN_enabled
- enum : `kParamEqBNEnabled` · label : ON · type : bool · default : 1 · auto : oui · smooth : fondu 10 ms · page : EQUALIZER · control : switch (barre de bande)
- help_short : Active ou coupe cette bande.
- help_long : Une bande coupée ne modifie plus le son et sa courbe disparaît, mais ses réglages sont conservés ; son point reste visible, grisé, sur la courbe. Pratique pour comparer avec et sans une bande.
- meta : Interrupteur par bande · Défaut actif · Automatisable : oui

#### eq_bN_channel
- enum : `kParamEqBNChannel` · label : CHANNEL · type : enum · range : 0 A, 1 B · default : 0 · auto : non · smooth : fondu 20 ms · page : EQUALIZER · control : bouton de la courbe (L/R ou M/S), défini à la création
- depends : sans effet en mode Stereo
- help_short : Canal traité par la bande (Dual Mono, M-S).
- help_long : En Dual Mono, la bande agit sur la gauche (L) ou la droite (R). En M-S, sur le centre (M) ou les côtés (S). En Stereo, ce choix est ignoré : la bande agit sur les deux canaux. Une nouvelle bande est assignée au canal affiché par le bouton de la courbe.
- meta : A = L ou M · B = R ou S · Défaut A · Non automatisable

#### eq_bN_type  (aide : `eq_band_type`)
- enum : `kParamEqBNType` · label : TYPE · type : enum · range : 0 Bell, 1 Low Shelf, 2 High Shelf, 3 High Pass, 4 Low Pass · default : 0 · auto : non · smooth : fondu 20 ms
- page : EQUALIZER · control : pill de type + bouton « ? »
- help_short / help_long / meta : voir `eq_band_type` (§ « Aides reprises du fichier d'aide »).

#### eq_bN_freq
- enum : `kParamEqBNFreq` · label : FREQ · type : float
- range : 20…20000 · default : 1000 · unit : Hz · scale : log · auto : oui · smooth : 20 ms
- page : EQUALIZER · control : potard (plages affichées 20 et 20k), ou glisser horizontal du point sur la courbe
- help_short : Fréquence centrale ou de coupure de la bande.
- help_long : Fréquence où la bande agit : centre d'une bosse ou d'un creux (Bell), milieu de la transition d'un Shelf, fréquence de coupure d'un High Pass ou d'un Low Pass. Se règle aussi en glissant le point de la bande sur la courbe, ou par double-clic sur le potard pour saisir une valeur.
- meta : 20…20 000 Hz · Défaut 1000 · Automatisable : oui

#### eq_bN_gain
- enum : `kParamEqBNGain` · label : GAIN · type : float
- range : −18…+18 · default : 0 · unit : dB · scale : linear · auto : oui · smooth : 20 ms
- page : EQUALIZER · control : potard, glisser vertical du point · depends : Bell, Low Shelf, High Shelf
- help_short : Gain appliqué par la bande, en dB.
- help_long : Positif : la bande relève la zone concernée ; négatif : elle l'abaisse. Se règle aussi en glissant le point de la bande vers le haut ou le bas. Pour une bande dynamique, c'est le gain de repos ; le gain réel varie ensuite autour de cette valeur (voir DYNAMIC).
- meta : −18…+18 dB · Défaut 0 · Automatisable : oui

#### eq_bN_q
- enum : `kParamEqBNQ` · label : Q · type : float
- range : 0,15…12 · default : 1,0 · scale : linear · auto : oui · smooth : 20 ms
- page : EQUALIZER · control : potard (2 décimales), molette sur le point · depends : Bell, Low Shelf, High Shelf (remplacé par SLOPE pour High Pass / Low Pass)
- help_short : Largeur de la bande (plus élevé = plus étroit).
- help_long : Pour une Bell, un Q élevé donne une bosse ou un creux très étroit, un Q faible une zone large et douce. Pour un Shelf, il règle la raideur de la transition : Q faible = transition lente et large, Q élevé = transition raide, avec un léger dépassement. Se règle aussi à la molette sur le point de la bande.
- meta : 0,15…12 · Défaut 1,0 · Automatisable : oui

#### eq_bN_slope  (aide : `eq_band_type`)
- enum : `kParamEqBNSlope` · label : SLOPE · type : int
- range : 6, 12, 18, 24, 30, 36, 42, 48 · default : 12 · unit : dB/oct · scale : pas de 6 · auto : non · smooth : fondu 20 ms
- page : EQUALIZER · control : potard (plages affichées 6 et 48), molette sur le point · depends : seulement pour High Pass / Low Pass
- help_short : Raideur de la coupure, en dB par octave.
- help_long : Voir `eq_band_type` (la pente est décrite dans le texte du type de bande). 12 dB/oct est un bon point de départ musical ; 48 dB/oct coupe très net.
- meta : Pente 6…48 dB/oct par pas de 6 · Défaut 12 · Non automatisable

#### eq_bN_dynamic  (aide : `eq_dynamic`)
- enum : `kParamEqBNDynamic` · label : DYNAMIC · type : bool · default : 0 · auto : oui · smooth : 20 ms
- page : EQUALIZER · control : switch de la barre de bande + bouton « ? » · depends : seulement Bell, Low Shelf, High Shelf
- help_short / help_long / meta : voir `eq_dynamic` (§ « Aides reprises du fichier d'aide »).

#### eq_bN_dyn_thresh
- enum : `kParamEqBNDynThresh` · label : THRESH · type : float
- range : −60…0 · default : −24 · unit : dB · scale : linear · auto : oui · smooth : 20 ms
- page : EQUALIZER · control : potard · depends : DYNAMIC actif
- help_short : Niveau à partir duquel la bande réagit.
- help_long : Quand le niveau du signal dans la bande dépasse ce seuil, le gain de la bande change (de RANGE au maximum, atteint 24 dB au-dessus du seuil). En dessous, la bande garde son gain de repos.
- meta : −60…0 dB · Défaut −24 · Automatisable : oui

#### eq_bN_dyn_range
- enum : `kParamEqBNDynRange` · label : RANGE · type : float
- range : −24…+24 · default : −6 · unit : dB · scale : linear · auto : oui · smooth : 20 ms
- page : EQUALIZER · control : potard · depends : DYNAMIC actif
- help_short : Variation maximale du gain de la bande.
- help_long : Quantité de gain ajoutée au gain de repos quand le signal est bien au-dessus du seuil. Négatif : la bande baisse (ex. −6 dB pour calmer une sibilance au-dessus du seuil). Positif : la bande monte quand le signal est fort.
- meta : −24…+24 dB · Défaut −6 · Automatisable : oui

#### eq_bN_dyn_attack
- enum : `kParamEqBNDynAttack` · label : ATTACK · type : float
- range : 0,5…200 · default : 15 · unit : ms · scale : linear · auto : oui · smooth : —
- page : EQUALIZER · control : potard · depends : DYNAMIC actif
- help_short : Rapidité de la réaction quand le niveau monte.
- help_long : Temps que met la bande pour atteindre son gain dynamique quand le signal dépasse le seuil. Court : réaction immédiate, qui peut agir sur les transitoires. Long : la bande laisse passer le début des sons.
- meta : 0,5…200 ms · Défaut 15 · Automatisable : oui

#### eq_bN_dyn_release
- enum : `kParamEqBNDynRelease` · label : RELEASE · type : float
- range : 10…1000 · default : 150 · unit : ms · scale : linear · auto : oui · smooth : —
- page : EQUALIZER · control : potard · depends : DYNAMIC actif
- help_short : Rapidité du retour au gain de repos.
- help_long : Temps que met la bande pour revenir à son gain de repos quand le niveau repasse sous le seuil. Court : la bande « bat » avec le signal ; long : retour doux et discret.
- meta : 10…1000 ms · Défaut 150 · Automatisable : oui

---

## 3. Paramètres globaux

| id | label | type | plage | défaut | unité · échelle | auto | smooth | page · contrôle |
|---|---|---|---|---|---|---|---|---|
| `eq_channel_mode` | STEREO / DUAL MONO / M-S | enum | 0 Stereo, 1 Dual Mono, 2 M-S | 0 | — | non | fondu 20 ms | EQUALIZER · pill de la barre du bas + « ? » |
| `eq_oversampling` | OVERSAMPLING | enum | 0 None, 1 2x, 2 4x | 2 (4x) | — | non | — | EQUALIZER · pill de la barre du bas + « ? » · point de couleur éteint si None (libellé « - ») |
| `eq_bypass` | BYPASS | bool | — | 0 | — | oui | fondu 10 ms | bouton d'alimentation de l'en-tête (icône sans libellé dans la maquette) |
| `eq_analyzer_on` | SPECTRUM | bool | — | 1 | — | non | — | EQUALIZER · switch de la barre de titre de la courbe |

Le mode de phase est fixe : le bouton « MINIMUM PHASE » de la barre du bas est un indicateur statique, pas un paramètre.

### Aides reprises du fichier d'aide (`NOVA_Equalizer_help_v0.1.md`, inchangées)

#### eq_dynamic
- label : DYNAMIC
- help_short : Bande dont le gain suit le niveau du signal.
- help_long : Rend la bande dynamique : son gain évolue selon le niveau dans la bande, comme un compresseur ciblé sur une plage de fréquences. Utile par exemple pour calmer une sibilance seulement quand elle apparaît, au lieu de couper en permanence.
- meta : Interrupteur par bande

#### eq_band_type
- label : TYPE DE BANDE
- help_short : Forme du filtre de la bande.
- help_long : Bell : bosse ou creux centré sur une fréquence. Low Shelf / High Shelf : relève ou abaisse tout le côté grave ou aigu. High Pass / Low Pass : retirent les graves ou les aigus ; leur pente (SLOPE, en dB par octave) règle la rapidité de la coupure : plus elle est forte, plus la coupure est nette. Les filtres n'ont pas de gain et ne peuvent pas être dynamiques.
- meta : Choix : Bell, Low Shelf, High Shelf, High Pass, Low Pass · Pente 6…48 dB/oct

#### eq_channel_mode
- label : STEREO / DUAL MONO / M-S
- help_short : Quels canaux les bandes traitent.
- help_long : Stereo : chaque bande agit de la même façon sur les deux canaux. Dual Mono : chaque bande est assignée à la gauche (L) ou à la droite (R), pour égaliser les deux côtés différemment. M-S : chaque bande est assignée au centre (M, mid) ou aux côtés (S, side), pour agir sur la largeur stéréo sans toucher à l'autre. Le bouton à côté du titre de la courbe choisit quel canal est affiché.
- meta : Choix : Stereo, Dual Mono, M-S

#### eq_oversampling
- label : OVERSAMPLING
- help_short : Traitement à fréquence plus élevée.
- help_long : Limite les déformations des aigus près de la fréquence maximale et les artefacts des bandes dynamiques. Plus la valeur est élevée, plus la charge processeur et la latence augmentent.
- meta : Choix : None, 2x, 4x

#### eq_spectrum_point
- label : Measurement point
- help_short : Où le spectre affiché est mesuré.
- help_long : PRE-EQ mesure le signal avant l'égalisation, POST-EQ après. Comparer les deux permet de voir exactement ce que fait l'égaliseur.
- meta : Choix : PRE-EQ, POST-EQ · Affichage uniquement

### Blocs des paramètres globaux

#### eq_channel_mode
- enum : `kParamEqChannelMode` · label : STEREO / DUAL MONO / M-S · type : enum
- range : 0 Stereo, 1 Dual Mono, 2 M-S · default : 0 · auto : non · smooth : fondu croisé 20 ms
- page : EQUALIZER (barre du bas) · control : pill cyclique + « ? »
- depends : fixe le sens de `eq_bN_channel` (§1.2) et les choix du bouton de la courbe (Stereo : STEREO, L, R ; Dual Mono : L, R ; M-S : M, S)
- help : voir ci-dessus.

#### eq_oversampling
- enum : `kParamEqOversampling` · label : OVERSAMPLING · type : enum
- range : 0 None, 1 2x, 2 4x · default : 2 (4x) · auto : non · page : EQUALIZER (barre du bas) · control : pill cyclique + « ? »
- depends : la latence déclarée suit ce choix (§1.7) · help : voir ci-dessus.

#### eq_bypass
- enum : `kParamEqBypass` · label : BYPASS · type : bool · default : 0 · auto : oui · control : bouton de l'en-tête (bypass sans coupure : fondu de 10 ms, chemin sec retardé de la latence)
- help_short : Contourne tout l'égaliseur.
- help_long : Le signal passe sans traitement, pour comparer avec et sans égaliseur. Le passage se fait en fondu de 10 ms, sans clic, et le son reste synchronisé même avec l'oversampling.
- meta : Interrupteur · Défaut 0 · Automatisable : oui

#### eq_analyzer_on
- enum : `kParamEqAnalyzerOn` · label : SPECTRUM · type : bool · default : 1 · auto : non · page : EQUALIZER · control : switch de la barre de titre
- help_short : Affiche ou masque le spectre derrière la courbe.
- help_long : Active l'analyseur de spectre. Désactivé, il ne consomme plus de processeur. Les réglages de l'analyseur (point de mesure, canal, taille de FFT, vitesse de descente) sont dans SETTINGS.
- meta : Interrupteur · Défaut actif · Affichage uniquement

---

## 4. Réglages de Settings (non automatisables, enregistrés avec le preset)

Sections de la page SETTINGS : GENERAL, AUDIO / DSP, ANALYZER, INTERACTION, ABOUT.

| id | label | type | plage | défaut | section |
|---|---|---|---|---|---|
| `eq_precision` | Internal precision | enum | 0 AUTO (DAW), 1 32-BIT, 2 64-BIT | 0 | AUDIO / DSP |
| `eq_report_latency` | Report latency to host | bool | — | 1 | AUDIO / DSP |
| `eq_spectrum_point` | Measurement point | enum | 0 PRE-EQ, 1 POST-EQ | 1 (POST-EQ) | ANALYZER |
| `eq_analyzer_decay` | Decay speed | float | 0…2000 ms (pas 10) | 300 | ANALYZER |
| `eq_analyzer_peak_hold` | Peak hold | bool | — | 1 | ANALYZER |
| `eq_fft_size` | FFT size | enum | 0 1024, 1 2048, 2 4096 | 1 (2048) | ANALYZER |
| `eq_analyzer_channel` | Channel | enum | 0 STEREO, 1 LEFT, 2 RIGHT, 3 MID, 4 SIDE | 0 | ANALYZER |

#### eq_precision
- label : Internal precision
- help_short : Précision de calcul interne.
- help_long : AUTO suit le format du buffer de l'hôte. 32-BIT est un peu plus léger pour le processeur ; 64-BIT limite les erreurs d'arrondi des filtres graves très étroits et des pentes raides. Peut différer du format envoyé par l'hôte.
- meta : Choix : AUTO (DAW), 32-BIT, 64-BIT · Défaut AUTO

#### eq_report_latency
- label : Report latency to host
- help_short : Déclare la latence de l'oversampling à l'hôte.
- help_long : Actif : l'hôte compense automatiquement la latence ajoutée par l'oversampling (l'égaliseur lui-même reste en phase minimum, sans latence). Désactivé : aucune compensation, utile seulement pour un montage où l'on gère la latence soi-même.
- meta : Interrupteur · Défaut actif

#### eq_analyzer_decay
- label : Decay speed
- help_short : Vitesse de retombée du spectre.
- help_long : Temps que met le spectre à redescendre quand le niveau baisse. Court : lecture nerveuse ; long : lecture plus lisse.
- meta : 0…2000 ms · Défaut 300 · Affichage uniquement

#### eq_analyzer_peak_hold
- label : Peak hold
- help_short : Garde les crêtes du spectre.
- help_long : Maintient le niveau maximal atteint dans chaque zone de fréquence jusqu'à ce qu'il soit dépassé ou retombe, pour repérer les crêtes brèves.
- meta : Interrupteur · Défaut actif · Affichage uniquement

#### eq_fft_size
- label : FFT size
- help_short : Finesse de l'analyse de fréquence.
- help_long : Une FFT plus grande donne un spectre plus précis dans les graves, mais plus lent à réagir. Une FFT plus petite réagit plus vite, avec moins de détail.
- meta : Choix : 1024, 2048, 4096 · Défaut 2048 · Affichage uniquement

#### eq_analyzer_channel
- label : Channel
- help_short : Canal dont le spectre est affiché.
- help_long : STEREO affiche la moyenne des deux canaux ; LEFT, RIGHT, MID ou SIDE affichent un seul canal, même si l'égaliseur est en mode Stereo.
- meta : Choix : STEREO, LEFT, RIGHT, MID, SIDE · Défaut STEREO · Affichage uniquement

Autres réglages de Settings, communs à la série (état d'UI, pas des paramètres DSP) : thème (DARK / HAMMOND / LIGHT / CONTRASTE+), contraste, échelle de l'interface, langue (FR / EN), boutons « ? » (ALWAYS / hover / masqués), sensibilité du glisser des potards (NORMAL / FINE),
double-clic pour saisir une valeur, molette sur potards et points, sample rate (lecture seule), version, documentation, support, mises à jour. Voir `NOVA_Common_modules_v0.1.md`.

---

## 5. Sorties d'affichage (lecture seule, non automatisables)

| id | type | Contenu | Plage |
|---|---|---|---|
| `eq_bN_dyn_gain_out` (× 16) | output float | gain dynamique courant de la bande N = `env × range` (dB), 0 si la bande n'est pas dynamique ; sert à la courbe « live », à la zone colorée et à l'anneau pulsé | −24…+24 dB |
| `eq_spectrum_out` | output (tableau de 48 valeurs) | spectre du canal analysé, en dB, 48 bandes logarithmiques de 20 Hz à 20 kHz, au point de mesure choisi ; envoyé à l'UI à ~30 images/s | −90…0 dBFS |
| `eq_latency_out` | output int | latence déclarée (échantillons hôte) | 0… |

Tout le reste (courbes par bande, courbe totale, position des points, texte de la barre « BAND n · f · gain ») est calculé par l'UI à partir des paramètres et de `eq_bN_dyn_gain_out`.
Module vu-mètres : préfixe `meter_`, identique aux autres plugins, voir `NOVA_Common_modules_v0.1.md` (IN = entrée du plugin, OUT = sortie du plugin ; boutons PEAK IN / PEAK OUT ; non redécrit ici).

---

## 6. Défauts et presets

**Valeurs par défaut d'un emplacement** (`reset` de bande) : tableau du §2. **Preset « Default »** : aucune bande (`eq_bN_used` = 0 pour tous les N), toutes les valeurs du §3 à leur défaut, réponse plate (la maquette prévoit l'état « aucune bande sélectionnée »).

**Preset « Vocal Clarity »** (état initial de la maquette, nom affiché dans l'en-tête) :

| N | used | type | freq | gain | Q | slope | dynamic | thresh | range | attack | release | channel |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 1 | High Pass | 80 Hz | — | — | 12 dB/oct | — | — | — | — | — | A |
| 2 | 1 | Bell | 300 Hz | −3,5 dB | 1,1 | — | 0 | −24 | −6 | 15 | 150 | A |
| 3 | 1 | Bell | 2800 Hz | +2,2 dB | 1,4 | — | 1 | −28 dB | −5 dB | 8 ms | 120 ms | A |
| 4 | 1 | High Shelf | 9000 Hz | +2,5 dB | 0,7 | — | 0 | −24 | −6 | 15 | 150 | A |

Autres globaux : Stereo, oversampling 4x, bypass 0, analyseur actif. La bande sélectionnée au départ est la bande 2 (état d'UI). Un changement de réglage fait passer le preset en « modifié » (astérisque dans l'en-tête).
La maquette ne propose qu'un preset ; aucun autre preset d'usine n'est décrit.

---

## 7. UI seulement (non paramètres)

- Bande sélectionnée, couleur de la bande (palette de 8 teintes `--band-1…8`, `couleur = (rang de création) modulo 8`), bouton de la courbe (STEREO / L / R ou L / R ou M / S), vue active (EQUALIZER / SETTINGS).
- A / B / C / D : 4 emplacements de comparaison (bouton rond de la barre du bas, comme les autres plugins mais avec quatre états dans la maquette).
- Menu de presets (pastille « Vocal Clarity »), thème, contraste, échelle, langue, réglage de l'aide.
- Interactions sur la courbe : glisser un point (fréquence et gain, pointe à moins de 16 px), double-clic dans le vide = ajouter une bande Bell (fréquence et gain de la position, gain borné ±18 dB), double-clic sur un point = supprimer la bande, molette à moins de 24 px d'un point = Q ou pente.

## 8. Organisation de l'interface (gabarit 1160 × 780)

**Page EQUALIZER**
- Colonne gauche : vu-mètres IN / OUT (hauteur alignée sur le panneau de la courbe).
- Panneau SPECTRUM / EQ CURVE : titre, bouton du canal affiché, lecture « BAND n · fréquence · gain », switch SPECTRUM ; en dessous, courbe ±20 dB × 20 Hz…20 kHz avec spectre, courbes par bande, courbe totale, zone dynamique, points déplaçables.
- Bandeau de bandes : une chip par bande (couleur, fréquence, type abrégé, ×) et « + ADD BAND ».
- Panneau de la bande sélectionnée : barre d'outils (numéro, pill de type + « ? », ON, DYNAMIC + « ? » pour les types à gain, DELETE BAND) ; rangée de potards : FREQ, GAIN (types à gain), Q (types à gain) ou SLOPE (HP / LP), puis THRESH, RANGE, ATTACK, RELEASE si DYNAMIC.
- Barre du bas : A/B/C/D, pill STEREO / DUAL MONO / MS + « ? », à droite : indicateur « MINIMUM PHASE » (statique), OVERSAMPLING (point de couleur, pill, « ? »).
- En-tête : logo, preset, contraste, thème, bouton d'alimentation (bypass).

**Page SETTINGS** : cartes GENERAL, AUDIO / DSP, ANALYZER, INTERACTION, ABOUT (§4).

## 9. Points à valider

1. **Nombre de bandes** : la maquette n'impose pas de maximum (palette de 8 couleurs bouclée). Proposition : 16 emplacements fixes (`eq_b1…eq_b16`), soit 208 paramètres de bande ; alternative : 8 (104). Choix à confirmer avec le poids de la liste d'automation.
2. **Assignation des canaux** : la maquette garde **quatre jeux de bandes indépendants** (L, R, M, S), alors que le fichier d'aide dit « chaque bande est assignée à L ou R / M ou S ». Ce fichier suit l'aide (un champ `eq_bN_channel` A/B par bande, un seul pool de 16) ; en conséquence, passer de Dual Mono à M-S réinterprète A/B comme M/S au lieu de retrouver un jeu séparé. À confirmer.
3. **Oversampling 8x** : la maquette propose NONE / 2X / 4X / 8X, l'aide ne cite que None / 2x / 4x. Ce fichier suit l'aide (défaut 4x, comme la maquette) ; ajouter 8x si voulu (aide à mettre à jour).
4. **Gain d'entrée / sortie, auto-gain, solo ou écoute de bande, phase linéaire, mode stéréo par bande, A/B/C/D de paramètres** : absents de la maquette, donc non décrits. À ajouter si souhaités (« MINIMUM PHASE » est un indicateur fixe).
5. **Bypass** : l'en-tête contient un bouton d'alimentation sans libellé ni id ; interprété comme `eq_bypass` (comme `dly_bypass`, `cho_bypass`). À confirmer.
6. **Pentes** : le potard SLOPE de la maquette est continu (6…48) ; ce fichier le quantifie par pas de 6 dB/oct avec une réponse Butterworth (cascade de biquads, section du 1ᵉʳ ordre pour 6/18/30/42). Q de High Pass / Low Pass non exposé. Alternative : Q libre sur la dernière section.
7. **Gain effectif des bandes dynamiques** : formule `gain + env × range`, cible `min(1, dépassement / 24 dB)` (constante 24 de la maquette), détecteur sur le signal d'entrée avant toutes les bandes, filtré dans la zone de la bande (passe-bande pour Bell, passe-haut / passe-bas du 2ᵉ ordre pour les shelves), mesure de crête instantanée, lien stéréo par maximum. Borne de sécurité ±36 dB et recalcul tous les 32 échantillons : valeurs de départ.
8. **Échelles** : la maquette utilise des potards linéaires pour Q (0,15…12), ATTACK (0,5…200) et RELEASE (10…1000) ; une échelle logarithmique serait plus confortable. Valeurs de lissage (20 ms pour freq / gain / q / seuil / range, fondus de 10 et 20 ms) : points de départ.
9. **Automatisation** : `used`, `channel`, `type`, `slope`, mode de canaux, oversampling, analyseur : non automatisables (changent la structure). `type` et `slope` pourraient être rendus automatisables avec le fondu de 20 ms.
10. **Latence** : filtres de ré-échantillonnage à phase linéaire (demi-bandes) ; valeur de la latence (et option de filtres à phase minimale, latence nulle mais phase altérée) à fixer par l'implémentation DSP. Bypass retardé de cette latence.
11. **Défauts** : preset « Default » sans bande ; Q par défaut 1,0 ; Q du High Shelf du preset à 0,7. Contraintes de glisser (HP ≤ 4 kHz, LP ≥ 100 Hz) : UI seulement ou à imposer au DSP ?
12. **Analyseur** : 48 bandes (valeur de la maquette, à augmenter à 96–128 pour un affichage plus fin), fenêtre de Hann, recouvrement 50 %, ~30 images/s, canal analysé réglé dans SETTINGS **et** bouton de la courbe (en Stereo, L / R / STEREO ne ferait que changer l'écoute selon la maquette) : relation entre les deux à trancher. Peak hold calculé côté UI.
13. **Courbe affichée** : la maquette trace des approximations ; le plugin doit calculer l'exacte avec les formules des §1.3 et 1.4 à `fs × OS`. Surcoût UI à valider (16 bandes × 200 points × sections).
14. **Textes d'aide nouveaux** (CHANNEL, FREQ, GAIN, Q, THRESH, RANGE, ATTACK, RELEASE, ON, bypass, précision, latence, analyseur) : propositions de ce fichier, à reporter dans `NOVA_Equalizer_help_v0.1.md` (non modifié ici) avec les ids correspondants.
15. **Total** : 219 paramètres (208 de bande + 4 globaux + 7 de Settings), plus 18 sorties (16 `dyn_gain_out`, `eq_spectrum_out`, `eq_latency_out`) et le module `meter_*`.
