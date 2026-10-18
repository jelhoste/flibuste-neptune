# NOVA Hammond — description des paramètres v0.1

Orgue à roues phoniques (tonewheels) avec 3 claviers (Upper, Lower, Pedal), 9 tirettes par manuel (2 au pédalier), percussion, vibrato / chorus, drive, cabine Leslie (cornet + rotor de basses), réverbération et volume général. C'est un **instrument** (CLAP DPF) : entrée MIDI, sortie audio stéréo, pas d'entrée audio (la maquette n'a qu'un vu-mètre de sortie).
Source unique : la maquette `nova_hammond_console_ui.html` (état, potards, tirettes, matrice, splits, transpositions, Leslie, barre du bas, presets, vu-mètres) et le fichier d'aide `NOVA_Hammond_help_v0.1.md` (ids conservés, `help_short` / `help_long` repris **tels quels**).
Format : celui de `NOVA_Compressor_params_v0.1.md` / `NOVA_Equalizer_params_v0.1.md` (rappelé au §0). Groupe `ham_` ; vu-mètres `meter_` (module commun, voir `NOVA_Common_modules_v0.1.md`, 19 paramètres, non redécrit ici).

**Convention de ce fichier** : tout ce qui vient de la maquette est écrit comme un fait. Tout ce que la maquette ne donne pas (formules, temps, fréquences, rapports, courbes de niveau, routage exact) est marqué **« proposé »** et repris au §7 « Points à valider ». La maquette n'a **aucun son** : elle ne définit que les contrôles, leurs plages, leurs défauts et leur organisation ; le principe de traitement du §1 est donc surtout « proposé ».

Constat important sur la maquette (détail au §7) : les entrées de navigation **LESLIE** et **SETTINGS** n'ouvrent pas de page propre (elles gardent la mise en page DRAWBARS) ; la maquette ne montre **aucun réglage** de rotor, de micros, de key click ni de précision interne.

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
| `page`, `control` | où et avec quel contrôle (potard, tirette, sélecteur, pill, switch…) |
| `depends` | conditions de grisage / d'activation |
| `help_short` | une ligne (bulle au survol) |
| `help_long` | explication complète (panneau d'aide du bouton « ? ») |

Les textes d'aide sont écrits une seule fois ; la maquette HTML et le code les reprennent tels quels. Pour les paramètres répétés (tirettes, transpositions, canaux MIDI, splits), l'aide est écrite une seule fois sous l'id d'aide du fichier `NOVA_Hammond_help_v0.1.md` (comme `eq_band_type` pour `eq_bN_type` et `eq_bN_slope`).
Les textes d'aide marqués « nouveau » sont des propositions de ce fichier, à ajouter au fichier d'aide (non modifié ici, voir §7).

**Ids** : les ids du fichier d'aide sont conservés tels quels : `ham_drawbars`, `ham_perc_harmonic`, `ham_perc_decay`, `ham_perc_volume`, `ham_chorale`, `ham_split`, `ham_matrix`, `ham_bank_preset`. Quand un id d'aide couvre plusieurs paramètres, les paramètres ont des ids dérivés (`ham_db_u1`…, `ham_split_upper`…, `ham_bank`, `ham_preset`) et pointent vers l'id d'aide. Les paramètres `ham_perc_harmonic`, `ham_perc_decay`, `ham_perc_volume` et `ham_chorale` ont le même id que leur aide.

---

## 1. Principe de traitement

### 1.1 Chaîne globale (ordre proposé : celui des potards de la maquette)

```
MIDI (3 manuels) → routage / splits / transposition (§1.2)
   → générateur à roues phoniques par manuel (tirettes, §1.3) [+ percussion sur Upper (§1.4)]
   → vibrato / chorus par manuel (§1.5)        (Pedal : sans vibrato, proposé)
   → somme des 3 manuels (+ bruit de moteur si MOTOR NOISE)
   → DRIVE (§1.6)  → LESLIE (§1.7)  → REVERB (§1.8)  → VOLUME (§1.9)  → sortie stéréo
```

La maquette ne dessine pas la chaîne ; elle n'impose que l'ordre d'affichage des potards (VIBE, DRIVE, VIB DEPTH, LESLIE, REVERB, VOLUME), que ce fichier reprend comme ordre de traitement (proposé). Sur un orgue réel la réverbération précède la Leslie ; l'ordre est à valider (§7).
Il n'y a **pas** de bypass, d'entrée audio, de gain d'entrée ni d'oversampling dans la maquette : non décrits (le bypass est celui de l'hôte). Latence : 0 échantillon (proposé tant qu'aucun oversampling n'est ajouté, voir §7).

### 1.2 Routage MIDI, splits, transposition

**Manuels** : `Upper`, `Lower`, `Pedal`. Chacun a son canal MIDI (`ham_midi_ch_u/l/p` : « − » = OMNI, ou 1…16 ; défauts 1, 2, 3), sa transposition (`ham_transp_u/l/p`, −24…+24 demi-tons, défaut 0) et ses tirettes. Les splits sont communs : `ham_split_upper` et `ham_split_lower` (note MIDI 0…127, « − » = désactivé, défaut désactivé).

Comportement donné par la maquette / l'aide : SPLIT UPPER et SPLIT LOWER « délimitent la zone de chaque manuel » ; TRANSP décale chaque manuel de −24 à +24 demi-tons « par exemple pour jouer une ligne de basse à la main gauche ». Le détail ci-dessous est **proposé** :

1. Pour chaque note-on `(canal c, note n, vélocité v)` : les manuels candidats sont ceux dont le canal est OMNI ou égal à `c`.
2. **Zones de split** (proposé) : `ham_split_upper` = première note de la zone Upper, `ham_split_lower` = première note de la zone Lower, le pédalier prenant la zone la plus basse :
   - Upper accepte `n ≥ SPLIT_UPPER` (tout si SPLIT UPPER est désactivé) ;
   - Lower accepte `SPLIT_LOWER ≤ n < SPLIT_UPPER` (bornes absentes si désactivées) ;
   - Pedal accepte `n < SPLIT_LOWER` (tout si SPLIT LOWER est désactivé) ;
   - si les deux splits sont désactivés : aucune restriction de zone, le routage se fait uniquement par canal MIDI. Si `SPLIT_LOWER ≥ SPLIT_UPPER`, la zone Lower est vide (proposé).
   Le filtre de zone s'ajoute au filtre de canal (un manuel doit passer les deux). Une même note peut sonner sur plusieurs manuels si plusieurs sont candidats et acceptent la note (cas de canaux OMNI sans split).
3. **Transposition** (proposé) : `n' = n + TRANSP_manuel`, calculée **après** le filtre de zone (le split porte sur la note jouée). Si `n'` sort de 0…127, la note est ignorée pour ce manuel.
4. Le note-off ressort la note `n'` mémorisée au note-on (pas recalculée), pour éviter les notes bloquées quand un split ou une transposition change pendant qu'une touche est tenue.
5. **Vélocité** : l'orgue n'est pas sensible à la vélocité ; elle ne sert qu'aux vu-mètres MIDI (§4) (proposé). Note-on avec vélocité 0 = note-off. Les messages « All notes off » / « All sound off » (CC 123 / 120) coupent les notes (proposé). Pitch bend, sustain (CC 64), aftertouch : non gérés (la maquette n'en parle pas).
6. **Canal contrôleur** `ham_ctrl_midi_ch` (1…16, défaut 1, « CTRL CH ») : « Canal MIDI contrôleur » selon l'info-bulle de la maquette ; aucune table de messages n'est donnée. Proposé : Program Change reçu sur ce canal = `ham_preset` (0…17), Bank Select (CC 0) = `ham_bank` (0…25) ; les CC des autres réglages restent à définir (§7). Ce canal n'est pas un canal de jeu : ses notes ne sont pas routées vers les manuels (proposé).

### 1.3 Générateur à roues phoniques et tirettes

**Principe** (maquette + aide : « chaque tirette ajoute une hauteur, nommée par sa longueur de tuyau en pieds ; 8' est la note jouée ») : pour chaque touche tenue d'un manuel, le son est la somme de neuf sinusoïdes dont les fréquences sont des multiples de la fréquence de la note jouée, chacune réglée par sa tirette (0…8).

| Tirette | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|
| Longueur (pieds) | 16′ | 5⅓′ | 8′ | 4′ | 2⅔′ | 2′ | 1⅗′ | 1⅓′ | 1′ |
| Rapport à la note jouée (`8 / pieds`) | ×0,5 | ×1,5 | ×1 | ×2 | ×3 | ×4 | ×5 | ×6 | ×8 |
| Intervalle | octave sous | quinte au-dessus de la note | note jouée | octave au-dessus | octave + quinte | 2 octaves | 2 oct. + tierce | 2 oct. + quinte | 3 octaves |
| Couleur (UI) | brun | brun | blanc | blanc | noir | blanc | noir | noir | blanc |

Le **Pedal** n'a que 2 tirettes : 16′ (×0,5) et 8′ (×1).
Les rapports sont déduits des longueurs de la maquette (fait) ; les couleurs sont du pur affichage.

**Fréquence** : `f_n = 440 · 2^((n' − 69 + ham_master_tune / 100) / 12)` Hz pour la note transposée `n'` (la référence 440 Hz est **proposée** ; la maquette ne donne que l'accord « TUNE (cents) » −100…+100). Tirette de rang k : fréquence `f_n · rapport_k`.

**Niveau d'une tirette** (valeur entière v = 0…8) — proposé : 3 dB par cran, comme sur l'orgue réel :
`a(v) = 0` si `v = 0`, sinon `a(v) = 10^(−3·(8 − v) / 20)` (8 → 0 dB, 1 → −21 dB). Le lissage empêche les clics quand une tirette bouge.
**Niveau de manuel** (proposé) : la somme des tirettes est normalisée pour qu'une seule tirette à 8 sur une seule note donne −12 dBFS de crête avant DRIVE ; le potard VOLUME fait le reste.
**Harmoniques** (proposé) : une sinusoïde dont la fréquence dépasse `0,45 · fs` est supprimée (le repliement d'octave de l'orgue réel n'est pas exposé).

**Contacts et clics** (proposé, **non exposé par la maquette**, aucun paramètre) : les oscillateurs sont libres (phase continue, pas de remise à zéro au note-on), la touche ouvre un gain avec une attaque de 1 ms et un relâchement de 5 ms, et un bref « key click » (transitoire de 1…3 ms, niveau fixe) peut être ajouté à l'attaque. Un paramètre `ham_key_click` n'est **pas** créé ; voir §7.

**Fuite de roues (« leakage »)** : la maquette contient `state.leakage = 0.16` mais **aucun contrôle** ne l'utilise ; non décrit comme paramètre (§7).

**Bruit de moteur** (`ham_motor_noise`, bouton « MOTOR NOISE », défaut off) : ajoute le bruit mécanique du générateur (ronflement + chuintement) à la somme des manuels. Niveau, spectre et forme **proposés** : ronflement à 60 Hz et ses harmoniques + bruit filtré, niveau fixe −60 dBFS.

### 1.4 Percussion (manuel Upper)

Sélecteurs de la maquette (chacun à 2 choix, texte d'aide repris) : `ham_perc_on` OFF / ON ; `ham_perc_volume` SOFT / NORMAL ; `ham_perc_decay` FAST / SLOW ; `ham_perc_harmonic` 2nd / 3rd.
« La percussion ajoute un claquement bref qui s'éteint, sur une harmonique : 2nd (octave au-dessus) ou 3rd (octave + quinte au-dessus). » :
- harmonique 2nd = sinusoïde à `f_n × 2` (tirette 4′), 3rd = `f_n × 3` (tirette 2⅔′) ;
- enveloppe `e(t) = 10^(−3·t / T)` à partir du note-on (décroissance exponentielle, T = temps pour atteindre −60 dB) ; **proposé** : FAST T = 0,2 s, SLOW T = 1,0 s ;
- niveau : **proposé** NORMAL = 0 dB (même niveau qu'une tirette à 8), SOFT = −6 dB (facteur 0,5) ;
- **proposé** : la percussion ne s'applique qu'au manuel Upper ; elle se redéclenche seulement si aucune autre touche d'Upper n'est tenue (déclenchement unique, comme sur l'orgue réel) ; la tirette 1′ n'est **pas** coupée quand la percussion est active (l'orgue réel la coupe : option à valider).
Percussion OFF : aucun signal percussif (fondu de 5 ms si on coupe en cours de note).

### 1.5 Vibrato / chorus (scanner)

Contrôles de la maquette : `ham_vibe_mode` (VIBE : « − », V1, C1, V2, C2, V3, C3, 7 positions), `ham_vib_upper` (VIB U, défaut on), `ham_vib_lower` (VIB L, défaut off), `ham_vib_depth` (VIB DEPTH, 0…100 %, défaut 42).
Sens (noms standard Hammond) : **V1, V2, V3** = vibrato (signal modulé seul) de profondeur croissante ; **C1, C2, C3** = chorus (signal sec + signal vibrato), même profondeur ; « − » = effet coupé. VIB U / VIB L choisissent les manuels traités ; VIB DEPTH règle la profondeur.
Le reste est **proposé** :
- modèle : ligne à retard modulée par un LFO sinusoïdal (équivalent du scanner à 9 étages), fréquence `f_lfo = 6,8 Hz`, un LFO commun aux manuels ;
- profondeur crête en cents : `c = ham_vib_depth × c_max(niveau)` avec `c_max` = 15 / 30 / 45 cents pour les niveaux 1 / 2 / 3 (V1, C1 = niveau 1 ; V2, C2 = 2 ; V3, C3 = 3) ;
- conversion en retard modulé : `Δd = (2^(c/1200) − 1) / (2π · f_lfo)` (crête du retard autour d'un retard moyen d'environ 2 ms) ;
- chorus : `y = 0,5·(x + x_vib)` ; vibrato : `y = x_vib` ;
- VIBE = « − » : les manuels passent sans traitement, quels que soient VIB U / VIB L ; Pedal : non traité (proposé).

### 1.6 Drive

Contrôles de la maquette : `ham_drive_on` (mini-switch, défaut on), `ham_drive` (DRIVE, 0…100, « CLEAN » → « HOT », défaut 25), `ham_drive_char` (CHAR, 0…100, défaut 50), `ham_drive_gain` (GAIN, 0…100, « Drive output gain », défaut 50). Drive off : contournement par fondu de 10 ms.
Proposé :
- gain avant saturation `g = 1 + 19·drive²` (de 0 à +26 dB) ;
- saturation douce `y = tanh(g·(x + b)) / tanh(g)` avec un biais `b = 0,4 · (CHAR − 50) / 50` (CHAR = 50 : symétrique ; CHAR < 50 : plus doux ; CHAR > 50 : asymétrique, harmoniques paires plus présentes) ; le décalage continu est retiré par un passe-haut du 1er ordre à 20 Hz ;
- gain de sortie `G_dB = (GAIN − 50) × 0,24` (−12…+12 dB, 50 = 0 dB) ;
- sur-échantillonnage ×2 autour de la saturation (proposé, non exposé, §7).

### 1.7 Cabine Leslie (cornet + rotor de basses)

Contrôles de la maquette : `ham_chorale` (CHOR / TREM / BRK, défaut CHOR) et `ham_leslie_mix` (LESLIE, 0…100, « DRY » → « WET », défaut 68). La maquette n'expose **ni rotor, ni micros, ni vitesses, ni accélérations** ; tout ce qui suit est **proposé**, sauf les trois états et leur sens (aide) :
- **CHOR** (Chorale) : rotation lente des rotors, effet d'ensemble doux ;
- **TREM** (Tremolo) : rotation rapide, vibrato intense ;
- **BRK** (Break) : « la Leslie s'arrête de tourner ; le son devient fixe, comme un haut-parleur ordinaire » ; « le passage d'une vitesse à l'autre est progressif, comme l'inertie des rotors » (aide).

Modèle proposé :
1. **Filtre de croisement** à 800 Hz (Linkwitz-Riley, 24 dB/oct) : grave → rotor de basses (tambour), aigu → cornet.
2. **Chaque rotor** a un angle `θ(t)` qui avance à la vitesse courante `ω(t)`. Effets calculés par rotor : (a) modulation d'amplitude `1 + m·cos(θ − φ_micro)` ; (b) effet Doppler par ligne à retard modulée `d(t) = d0 + r·cos(θ − φ_micro)` ; (c) filtrage directionnel (cornet : atténuation des aigus quand le pavillon s'éloigne du micro).
3. **Micros** (non exposés) : 2 micros virtuels fixes à 90° l'un de l'autre, donnant L et R ; angles `φ_L = −45°`, `φ_R = +45°` (proposé).
4. **Vitesses cibles** (proposé, valeurs de Leslie classique) : cornet CHOR 0,8 Hz (48 tr/min), TREM 6,7 Hz (400 tr/min) ; rotor de basses CHOR 0,67 Hz (40 tr/min), TREM 5,7 Hz (342 tr/min) ; BRK : 0.
5. **Inertie** (proposé) : `dω/dt = (ω_cible − ω) / τ` avec `τ` cornet = 0,5 s (accélération) / 1,0 s (décélération), rotor de basses = 2,5 s / 4,0 s ; en BRK la décélération vers 0 utilise `τ` de décélération ; en sortie de BRK les rotors repartent de l'arrêt.
6. **Arrêt** (proposé) : en BRK, quand `ω < 0,02 Hz`, la rotation est gelée à l'angle de repos `θ = φ_micro` (face au micro) pour que le son soit réellement « fixe » ; amplitude et Doppler deviennent constants.
7. **Mélange** `ham_leslie_mix` : `y = cos(m·π/2)·dry + sin(m·π/2)·leslie` (équi-puissance, proposé). `dry` = signal de DRIVE dupliqué en stéréo. À 0 % : Leslie contournée (le rotor continue de tourner en arrière-plan, proposé).

L'état courant de `ham_chorale` pilote aussi la lampe de l'en-tête (§5).

### 1.8 Réverbération

Contrôles : `ham_reverb` (REVERB, 0…100, « OFF » → « 100% », défaut 35 ; l'affichage est « OFF » à 0), `ham_reverb_size` (SIZE, 0…100, défaut 40), `ham_reverb_damp` (DAMP, 0…100, défaut 50). Type, durées et filtres **proposés** :
- réverbération stéréo à lignes à retard rebouclées (FDN à 8 lignes), insérée en « envoi » : `y = dry + (REVERB) · wet` ; `ham_reverb ≤ 0,001` : réverbération coupée (OFF) ;
- SIZE → temps de réverbération T60 = `0,3 · (4 / 0,3)^(SIZE/100)` s (0 → 0,3 s ; 40 → 0,85 s ; 100 → 4 s) ;
- DAMP → fréquence d'un passe-bas dans la boucle `fc = 12000 · (500 / 12000)^(DAMP/100)` Hz (0 → 12 kHz ; 50 → 2,4 kHz ; 100 → 500 Hz).

### 1.9 Volume

`ham_volume` (VOLUME, 0…100, défaut 72) : gain linéaire `g = v²` en amplitude (proposé ; v = 0,72 → −5,7 dB ; 1 → 0 dB), lissé 20 ms. Il n'y a pas de limiteur dans la maquette : saturation franche au-delà de 0 dBFS (le voyant de saturation du module `meter_` s'allume, §4) ; limiteur doux en sortie optionnel (proposé, §7).

### 1.10 Accord général

`ham_master_tune` (TUNE, −100…+100 cents, défaut 0) décale tous les manuels (y compris le pédalier) ; calcul à l'oscillateur, glissement lissé sur 20 ms (proposé). Affichage : « − » à 0, valeur signée sinon.

---

## 2. Paramètres

Total : **51 paramètres** (20 tirettes, 4 percussion, 4 vibrato, 4 drive, 2 Leslie, 3 réverbération, 1 volume, 9 MIDI, 2 barre du bas, 2 banque / preset), auxquels s'ajoutent 2 réglages d'affichage de la matrice (§5), 3 sorties (§4) et le module `meter_*`.
Les valeurs des potards à pourcentage sont stockées en 0…1 (affichage ×100, comme la maquette) ; SIZE, DAMP, CHAR, GAIN et les tirettes sont des entiers.

### 2.1 Tirettes (aide : `ham_drawbars`)

Plage commune : entier 0…8 (0 = tirette rentrée, 8 = sortie à fond) ; unité : cran ; échelle : linear ; auto : oui ; smooth : 10 ms (proposé) ; page DRAWBARS (et LIVE) · tirette (glisser vertical, 9 positions, valeur affichée sous la tirette).

| id | enum | label | pieds | défaut |
|---|---|---|---|---|
| `ham_db_u1` | `kParamHamDbU1` | UPPER 16′ | 16′ | 8 |
| `ham_db_u2` | `kParamHamDbU2` | UPPER 5⅓′ | 5⅓′ | 3 |
| `ham_db_u3` | `kParamHamDbU3` | UPPER 8′ | 8′ | 8 |
| `ham_db_u4` … `ham_db_u9` | `kParamHamDbU4` … `U9` | UPPER 4′, 2⅔′, 2′, 1⅗′, 1⅓′, 1′ | 4′ … 1′ | 0 |
| `ham_db_l1` | `kParamHamDbL1` | LOWER 16′ | 16′ | 0 |
| `ham_db_l2` | `kParamHamDbL2` | LOWER 5⅓′ | 5⅓′ | 0 |
| `ham_db_l3` | `kParamHamDbL3` | LOWER 8′ | 8′ | 8 |
| `ham_db_l4` | `kParamHamDbL4` | LOWER 4′ | 4′ | 8 |
| `ham_db_l5` … `ham_db_l9` | `kParamHamDbL5` … `L9` | LOWER 2⅔′, 2′, 1⅗′, 1⅓′, 1′ | 2⅔′ … 1′ | 0 |
| `ham_db_p1` | `kParamHamDbP1` | PEDAL 16′ | 16′ | 8 |
| `ham_db_p2` | `kParamHamDbP2` | PEDAL 8′ | 8′ | 0 |

- depends : aucune. Les tirettes Upper et Lower ont les mêmes rapports (§1.3). Le troisième emplacement du pédalier que contiennent les presets de la maquette (`p:[8,0,0]`) est ignoré : le Pedal n'a que 2 tirettes (§7).
- Bloc d'aide (id d'aide `ham_drawbars`, repris tel quel) : voir §2.9.

### 2.2 Percussion

| id | enum | label | type | plage | défaut | auto | smooth | page · contrôle |
|---|---|---|---|---|---|---|---|---|
| `ham_perc_on` | `kParamHamPercOn` | PERCUSSION | enum | 0 OFF, 1 ON | 0 (OFF) | oui | fondu 5 ms | DRAWBARS / LIVE · sélecteur à 2 positions |
| `ham_perc_volume` | `kParamHamPercVolume` | VOLUME (perc.) | enum | 0 SOFT, 1 NORMAL | 0 (SOFT) | oui | — | idem · sélecteur « VOLUME » |
| `ham_perc_decay` | `kParamHamPercDecay` | DECAY | enum | 0 FAST, 1 SLOW | 0 (FAST) | oui | — | idem · sélecteur « DECAY » |
| `ham_perc_harmonic` | `kParamHamPercHarmonic` | HARMONIC | enum | 0 2nd, 1 3rd | 0 (2nd) | oui | — | idem · sélecteur « HARMONIC » |

- depends : `ham_perc_volume`, `ham_perc_decay`, `ham_perc_harmonic` n'ont d'effet que si `ham_perc_on` = ON (la maquette ne grise pas les sélecteurs).
- `ham_perc_on` : l'aide du fichier ne lui donne pas d'id (le texte de `ham_perc_volume` cite « le sélecteur OFF / ON de PERCUSSION »). Texte nouveau :
  - help_short : Active ou coupe la percussion.
  - help_long : ON ajoute le claquement percussif défini par HARMONIC, DECAY et VOLUME au début de chaque note jouée sur le manuel Upper. OFF le supprime.
  - meta : Choix : OFF, ON · Défaut OFF
- Les trois autres : help repris tels quels, voir §2.9.

### 2.3 Vibrato / chorus

| id | enum | label | type | plage | défaut | unité · échelle | auto | smooth | page · contrôle |
|---|---|---|---|---|---|---|---|---|---|
| `ham_vibe_mode` | `kParamHamVibeMode` | VIBE | enum | 0 « − », 1 V1, 2 C1, 3 V2, 4 C2, 5 V3, 6 C3 | 0 (« − ») | — | oui | fondu croisé 20 ms | DRAWBARS / LIVE · potard cranté 7 positions (repères autour du potard) + pill |
| `ham_vib_upper` | `kParamHamVibUpper` | VIB U | bool | — | 1 | — | oui | fondu 20 ms | idem · mini-switch (sous VIBE) |
| `ham_vib_lower` | `kParamHamVibLower` | VIB L | bool | — | 0 | — | oui | fondu 20 ms | idem · mini-switch (sous VIBE) |
| `ham_vib_depth` | `kParamHamVibDepth` | VIB DEPTH | float | 0…1 (affiché 0…100) | 0,42 | % · linear | oui | 20 ms | idem · potard |

- depends : `ham_vib_upper`, `ham_vib_lower` et `ham_vib_depth` sans effet si `ham_vibe_mode` = « − ». Les repères du potard VIBE sont placés à 135° + k·45° (k = 0…6) sur l'arc de 270° du potard.
- Textes nouveaux :

#### ham_vibe_mode
- label : VIBE
- help_short : Type de vibrato ou de chorus.
- help_long : « − » : pas d'effet. V1, V2, V3 : vibrato de plus en plus profond (la hauteur ondule). C1, C2, C3 : chorus, mélange du son direct et du vibrato, de plus en plus marqué. VIB U et VIB L choisissent les manuels concernés ; VIB DEPTH règle la profondeur.
- meta : Choix : −, V1, C1, V2, C2, V3, C3 · Défaut −

#### ham_vib_upper / ham_vib_lower
- label : VIB U / VIB L
- help_short : Vibrato actif sur Upper / sur Lower.
- help_long : VIB U applique le vibrato ou le chorus choisi par VIBE au manuel Upper, VIB L au manuel Lower. Le pédalier n'est pas concerné.
- meta : Interrupteurs · Défaut : U actif, L inactif

#### ham_vib_depth
- label : VIB DEPTH
- help_short : Profondeur du vibrato.
- help_long : Règle l'amplitude de l'ondulation de hauteur. À 0 %, plus d'ondulation ; plus la valeur est haute, plus l'effet est marqué. S'ajoute au choix V1/V2/V3 ou C1/C2/C3.
- meta : 0…100 % · Défaut 42

### 2.4 Drive

| id | enum | label | type | plage | défaut | unité · échelle | auto | smooth | page · contrôle |
|---|---|---|---|---|---|---|---|---|---|
| `ham_drive_on` | `kParamHamDriveOn` | DRIVE ON | bool | — | 1 | — | oui | fondu 10 ms | DRAWBARS / LIVE · mini-switch dans la carte DRIVE (rangée CHAR) |
| `ham_drive` | `kParamHamDrive` | DRIVE | float | 0…1 (affiché 0…100) | 0,25 | % · linear | oui | 20 ms | idem · potard, repères CLEAN … HOT |
| `ham_drive_char` | `kParamHamDriveChar` | CHAR | int | 0…100 | 50 | — · linear | oui | 20 ms | idem · « num-pot » (glisser vertical, 1 cran = 8 px) |
| `ham_drive_gain` | `kParamHamDriveGain` | GAIN | int | 0…100 | 50 | — · linear | oui | 20 ms | idem · « num-pot » |

- depends : `ham_drive`, `ham_drive_char`, `ham_drive_gain` sans effet si `ham_drive_on` = 0.
- Textes nouveaux :

#### ham_drive
- label : DRIVE
- help_short : Saturation de l'orgue.
- help_long : De CLEAN (son propre) à HOT (saturation franche). La saturation chauffe le son et ajoute des harmoniques, comme le préampli d'un orgue poussé. CHAR en règle le caractère et GAIN le niveau de sortie du drive. L'interrupteur à côté de CHAR le coupe sans changer les réglages.
- meta : 0…100 · Défaut 25

#### ham_drive_char
- label : CHAR
- help_short : Caractère de la saturation.
- help_long : Fait varier la couleur du drive : vers le bas, saturation douce et symétrique ; vers le haut, saturation plus mordante et asymétrique. 50 est le réglage neutre.
- meta : 0…100 · Défaut 50

#### ham_drive_gain
- label : GAIN (drive)
- help_short : Niveau de sortie du drive.
- help_long : Compense ou renforce le niveau après la saturation, pour retrouver le même volume en poussant DRIVE. 50 laisse le niveau inchangé.
- meta : 0…100 · Défaut 50

### 2.5 Leslie

| id | enum | label | type | plage | défaut | unité · échelle | auto | smooth | page · contrôle |
|---|---|---|---|---|---|---|---|---|---|
| `ham_chorale` | `kParamHamChorale` | CHORALE / CHOR · TREM · BRK | enum | 0 Chor, 1 Trem, 2 Brk | 0 (Chor) | — | oui | inertie des rotors (§1.7, pas un lissage) | DRAWBARS : colonne Leslie (2 boutons CHORALE / TREMOLO + bouton BREAK) ; LIVE : 3 boutons carrés CHOR / TREM / BRK ; en-tête : pill qui fait défiler CHORALE → TREMOLO → BREAK → CHORALE |
| `ham_leslie_mix` | `kParamHamLeslieMix` | LESLIE | float | 0…1 (affiché 0…100) | 0,68 | % · linear | oui | 20 ms | potard, repères DRY … WET |

- depends : `ham_chorale` sans effet audible si `ham_leslie_mix` = 0.
- `ham_chorale` : la maquette stocke l'état en texte (`chorale`, `tremolo`, `break`) ; le plugin utilise l'enum 0 / 1 / 2. Les trois contrôles (colonne, carrés LIVE, pill de l'en-tête) pilotent le même paramètre.
- Aide de `ham_chorale` : texte repris tel quel, voir §2.9. Texte nouveau :

#### ham_leslie_mix
- label : LESLIE
- help_short : Quantité de son passant par la Leslie.
- help_long : À DRY, le son de l'orgue ne passe pas par la cabine Leslie ; à WET, il passe entièrement par la Leslie. Entre les deux, on mélange les deux. La vitesse de rotation se règle avec CHORALE / TREM / BRK.
- meta : 0…100 · Défaut 68

### 2.6 Réverbération

| id | enum | label | type | plage | défaut | unité · échelle | auto | smooth | page · contrôle |
|---|---|---|---|---|---|---|---|---|---|
| `ham_reverb` | `kParamHamReverb` | REVERB | float | 0…1 (affiché « OFF » à 0, sinon 1…100 %) | 0,35 | % · linear | oui | 20 ms | potard, repères OFF … 100% |
| `ham_reverb_size` | `kParamHamReverbSize` | SIZE | int | 0…100 | 40 | — · linear | oui | 50 ms | « num-pot » dans la carte REVERB |
| `ham_reverb_damp` | `kParamHamReverbDamp` | DAMP | int | 0…100 | 50 | — · linear | oui | 50 ms | « num-pot » dans la carte REVERB |

- depends : `ham_reverb_size` et `ham_reverb_damp` sans effet si `ham_reverb` = 0.
- Textes nouveaux :

#### ham_reverb
- label : REVERB
- help_short : Quantité de réverbération.
- help_long : Ajoute de l'espace autour de l'orgue. À OFF, la réverbération est coupée. SIZE règle la taille de la pièce (durée de la queue) et DAMP l'absorption des aigus.
- meta : OFF…100 % · Défaut 35 %

#### ham_reverb_size
- label : SIZE (reverb)
- help_short : Taille de la pièce.
- help_long : Plus la valeur est élevée, plus la pièce est grande et plus la réverbération dure.
- meta : 0…100 · Défaut 40

#### ham_reverb_damp
- label : DAMP (reverb)
- help_short : Absorption des aigus.
- help_long : Plus la valeur est élevée, plus les aigus s'éteignent vite dans la réverbération : elle devient plus sombre et plus douce.
- meta : 0…100 · Défaut 50

### 2.7 Volume

| id | enum | label | type | plage | défaut | unité · échelle | auto | smooth | page · contrôle |
|---|---|---|---|---|---|---|---|---|---|
| `ham_volume` | `kParamHamVolume` | VOLUME | float | 0…1 (affiché 0…100) | 0,72 | % · linear (courbe de niveau proposée, §1.9) | oui | 20 ms | potard, repères 0 … 100 |

#### ham_volume
- label : VOLUME
- help_short : Niveau de sortie de l'orgue.
- help_long : Règle le volume général, après la Leslie et la réverbération. À baisser si le vu-mètre de sortie sature.
- meta : 0…100 · Défaut 72

### 2.8 MIDI : splits, transpositions, canaux (aide splits : `ham_split`)

| id | enum | label | type | plage | défaut | unité · échelle | auto | smooth | page · contrôle |
|---|---|---|---|---|---|---|---|---|---|
| `ham_split_upper` | `kParamHamSplitUpper` | SPLIT UPPER | int | −1 (« − », désactivé), 0…127 | −1 | note MIDI · linear | non | — | MANUALS (titre du panneau claviers) ; sur LIVE, dans la barre de titre DRAWBARS · « num-pot » |
| `ham_split_lower` | `kParamHamSplitLower` | SPLIT LOWER | int | −1 (« − »), 0…127 | −1 | note MIDI · linear | non | — | idem · « num-pot » |
| `ham_transp_u` | `kParamHamTranspU` | TRANSP U | int | −24…+24 | 0 | demi-tons · linear | oui | — (note suivante) | idem · « num-pot » |
| `ham_transp_l` | `kParamHamTranspL` | TRANSP L | int | −24…+24 | 0 | demi-tons · linear | oui | — | idem |
| `ham_transp_p` | `kParamHamTranspP` | TRANSP P | int | −24…+24 | 0 | demi-tons · linear | oui | — | idem |
| `ham_midi_ch_u` | `kParamHamMidiChU` | U : canal | int | 0 (OMNI, « − »), 1…16 | 1 | canal · pas de 1 | non | — | colonne de gauche (sous les vu-mètres MIDI), LIVE : sous les LED · bouton « U : 1 » (glisser vertical 12 px par cran, clic = cran suivant, molette) |
| `ham_midi_ch_l` | `kParamHamMidiChL` | L : canal | int | 0 (OMNI), 1…16 | 2 | idem | non | — | idem · « L : 2 » |
| `ham_midi_ch_p` | `kParamHamMidiChP` | P : canal | int | 0 (OMNI), 1…16 | 3 | idem | non | — | idem · « P : 3 » |
| `ham_ctrl_midi_ch` | `kParamHamCtrlMidiCh` | CTRL CH | int | 1…16 | 1 | canal · pas de 1 | non | — | barre du bas · « num-pot » |

- scale / valeurs : les splits sont des notes MIDI (le « − » de la maquette = −1 ; **à noter** : la maquette affiche aussi « − » pour 0 à cause de son code d'affichage, le plugin doit afficher « 0 » pour la note 0). Les transpositions : même remarque, la maquette affiche « − » pour toute valeur ≤ 0 (**défaut d'affichage**), le plugin doit afficher « 0 », « −12 », « +7 » (signe explicite). Le canal OMNI est codé −1 dans la maquette ; le plugin utilise 0 (proposé) et affiche « − ».
- Pas de lissage : les changements ne s'appliquent qu'aux notes suivantes (une note tenue garde son routage, §1.2 point 4).
- Aides : `ham_split` (SPLIT UPPER, SPLIT LOWER, TRANSP) repris tel quel, voir §2.9. Textes nouveaux :

#### ham_midi_ch_u / ham_midi_ch_l / ham_midi_ch_p
- label : CANAL MIDI U / L / P
- help_short : Canal MIDI de chaque manuel.
- help_long : Chaque manuel (Upper, Lower, Pedal) écoute son canal MIDI, de 1 à 16. « − » (omni) : le manuel écoute tous les canaux. Pratique pour jouer les trois manuels depuis plusieurs claviers, ou un seul clavier avec les splits.
- meta : − (omni), 1…16 · Défaut U 1, L 2, P 3

#### ham_ctrl_midi_ch
- label : CTRL CH
- help_short : Canal MIDI du contrôleur.
- help_long : Canal MIDI sur lequel le plugin reçoit les messages de commande (changement de banque et de preset). Ce canal ne joue pas de notes.
- meta : 1…16 · Défaut 1

### 2.9 Aides reprises du fichier d'aide (`NOVA_Hammond_help_v0.1.md`, inchangées)

#### ham_drawbars (couvre `ham_db_u1`…`ham_db_u9`, `ham_db_l1`…`ham_db_l9`, `ham_db_p1`, `ham_db_p2`)
- label : DRAWBARS
- help_short : Les tirettes règlent le mélange des harmoniques.
- help_long : Chaque tirette ajoute une hauteur, nommée par sa longueur de tuyau en pieds (16', 8', 4'…). 8' est la note jouée ; 16' est une octave plus grave ; 4' une octave plus aiguë ; les autres (5⅓', 2⅔', 2', 1⅗', 1⅓', 1') ajoutent des harmoniques. Plus la tirette est sortie (0 à 8), plus la hauteur est présente.
- meta : 9 tirettes, 0…8 par manuel

#### ham_perc_harmonic
- label : PERCUSSION HARMONIC
- help_short : Harmonique ajoutée au début de la note.
- help_long : La percussion ajoute un claquement bref qui s'éteint, sur une harmonique : 2nd (octave au-dessus) ou 3rd (octave + quinte au-dessus). La 3rd est plus brillante et plus pointue.
- meta : Choix : 2nd, 3rd

#### ham_perc_decay
- label : PERCUSSION DECAY
- help_short : Durée du claquement percussif.
- help_long : Fast : le claquement disparaît très vite, attaque sèche et rythmique. Slow : il dure davantage et sonne comme un son de cloche.
- meta : Choix : Fast, Slow

#### ham_perc_volume
- label : PERCUSSION VOLUME
- help_short : Niveau du claquement percussif.
- help_long : Soft : percussion plus discrète. Normal : percussion bien présente. Elle s'active avec le sélecteur OFF / ON de PERCUSSION.
- meta : Choix : Soft, Normal

#### ham_chorale
- label : CHORALE / CHOR · TREM · BRK
- help_short : Réglages de la cabine Leslie.
- help_long : CHOR (Chorale) : rotation lente des rotors, effet d'ensemble doux. TREM (Tremolo) : rotation rapide, vibrato intense. BRK (Break) : la Leslie s'arrête de tourner ; le son devient fixe, comme un haut-parleur ordinaire. Le passage d'une vitesse à l'autre est progressif, comme l'inertie des rotors.
- meta : Choix : Chor, Trem, Brk

#### ham_split (couvre `ham_split_upper`, `ham_split_lower`, `ham_transp_u`, `ham_transp_l`, `ham_transp_p`)
- label : SPLIT UPPER / SPLIT LOWER / TRANSP
- help_short : Partage du clavier et transposition par manuel.
- help_long : SPLIT UPPER et SPLIT LOWER sont des numéros de note MIDI (0 à 127, « − » = désactivé) qui délimitent la zone de chaque manuel. TRANSP décale chaque manuel (Upper, Lower, Pedal) de −24 à +24 demi-tons, par exemple pour jouer une ligne de basse à la main gauche.
- meta : Split : 0…127 ou off · Transposition : ±24 demi-tons

#### ham_matrix (réglages d'affichage `ham_matrix_cols`, `ham_matrix_rows`, voir §5)
- label : GRILLE / COLONNES / LIGNES
- help_short : Taille de la matrice de presets.
- help_long : La matrice affiche les presets sous forme de dalles. COLONNES et LIGNES règlent combien de dalles sont visibles : plus il y en a, plus chacune est petite. Cela ne change que l'affichage, pas le son. Un appui long sur les flèches les déverrouille.
- meta : Affichage uniquement

#### ham_bank_preset (couvre `ham_bank`, `ham_preset`, voir §2.10)
- label : BANK / PRESET
- help_short : Banques et presets de sons.
- help_long : Un preset mémorise tous les réglages (tirettes, percussion, Leslie…). Les presets sont rangés en banques : les flèches de BANK changent de banque, PRESET charge un son de la banque courante. Charger un preset remplace les réglages en cours.
- meta : Sélection par banque puis preset

### 2.10 Banque et preset (aide : `ham_bank_preset`)

| id | enum | label | type | plage | défaut | unité · échelle | auto | smooth | page · contrôle |
|---|---|---|---|---|---|---|---|---|---|
| `ham_bank` | `kParamHamBank` | BANK | int | 0…25 (affichée A…Z) | 0 (A) | — · pas de 1 | non | — | colonne PRESETS (flèches ‹ › + champ « A ») ; le champ « BANQUE A » en LIVE ; défilement circulaire (Z → A) |
| `ham_preset` | `kParamHamPreset` | PRESET | int | 0…17 (affiché 1…18, avec le nom) | 0 (« Jazz Club B3 ») | — · pas de 1 | non | — | colonne PRESETS : flèches ‹ › + champ nom + liste de 7 lignes ; page MATRICE : dalle ; défilement circulaire (18 → 1) |

- Charger une banque ou un preset remplace les valeurs de tous les paramètres sauf `ham_bank`, `ham_preset`, les canaux MIDI, `ham_ctrl_midi_ch` et `ham_master_tune` (proposé : ces réglages sont ceux du rig, pas du son). **La maquette ne charge que les tirettes** (§3) alors que l'aide parle de « tous les réglages » : point à valider.
- Appui sur ‹ › du preset : preset précédent / suivant dans la banque courante. Si le commutateur « enchaîner les banques » est actif (variable `chainBanks`, **sans contrôle dans la maquette**), le suivant du preset 18 est le preset 1 de la banque suivante (26 × 18 = 468 positions).

### 2.11 Barre du bas

| id | enum | label | type | plage | défaut | unité · échelle | auto | smooth | page · contrôle |
|---|---|---|---|---|---|---|---|---|---|
| `ham_motor_noise` | `kParamHamMotorNoise` | MOTOR NOISE | bool | — | 0 | — | non | fondu 50 ms | barre du bas · interrupteur à bascule |
| `ham_master_tune` | `kParamHamMasterTune` | TUNE (cents) | int | −100…+100 | 0 | cents · linear | oui | 20 ms | barre du bas · « num-pot » (« − » à 0, signe explicite sinon) |
| `ham_ctrl_midi_ch` | voir §2.8 | CTRL CH | | | | | | | barre du bas |

Textes nouveaux :

#### ham_motor_noise
- label : MOTOR NOISE
- help_short : Bruit du moteur de l'orgue.
- help_long : Ajoute le léger ronflement et le souffle mécanique d'un orgue à roues phoniques en marche. À couper pour un son parfaitement propre.
- meta : Interrupteur · Défaut inactif

#### ham_master_tune
- label : TUNE
- help_short : Accord général, en cents.
- help_long : Décale la hauteur de tout l'orgue de −100 à +100 cents (100 cents = un demi-ton). Utile pour s'accorder à un autre instrument.
- meta : −100…+100 cents · Défaut 0

---

## 3. Défauts et presets

**État initial de la maquette** (= défauts du §2) :

| Zone | Valeurs |
|---|---|
| Tirettes Upper | 8, 3, 8, 0, 0, 0, 0, 0, 0 (16′ … 1′) |
| Tirettes Lower | 0, 0, 8, 8, 0, 0, 0, 0, 0 |
| Tirettes Pedal | 8, 0 (16′, 8′) |
| Percussion | OFF · SOFT · FAST · 2nd |
| VIBE / VIB U / VIB L / VIB DEPTH | « − » / on / off / 42 % |
| DRIVE | on · 25 % · CHAR 50 · GAIN 50 |
| LESLIE | CHOR · mix 68 % |
| REVERB / SIZE / DAMP | 35 % · 40 · 50 |
| VOLUME | 72 % |
| Splits | SPLIT UPPER « − », SPLIT LOWER « − » ; TRANSP U / L / P = 0 |
| Canaux MIDI | U 1, L 2, P 3 ; CTRL CH 1 |
| Barre du bas | MOTOR NOISE off, TUNE 0 |
| Banque / preset | A · 1 (« Jazz Club B3 », nom affiché dans l'en-tête) |

**Presets de la maquette** : 26 banques (A…Z) × 18 presets = 468 emplacements. Les 18 noms sont identiques dans toutes les banques : Jazz Club B3, Gospel Swell, Rock Comp, Soft Pads, Perc Lead, Full Organ, Whistle, Bass Pedal, Clean Jazz, Theatre, Dark Soul, Bright Top, Cathedral, Bar Blues, Funk Chop, Slow Ballad, Pipe Mix, Shimmer.
Un preset de la maquette ne contient **que** les tirettes `{u[9], l[9], p[3]}` ; les valeurs sont **factices** : 8 modèles de base × 4 jeux sont dupliqués puis déformés par `(± 1 selon l'indice)` pour remplir les 468 emplacements. Exemple : le preset A-1 chargé donne Upper 7, 3, 8, 0, 0, 1, 0, 0, 1 ; Lower 0, 0, 8, 7, 0, 1, 0, 0, 1 ; Pedal 8, 0 (alors que l'état initial de la maquette est 8,3,8,0… et 0,0,8,8,0…, sans passer par le preset). Aucune valeur n'est une donnée d'usine : **les presets d'usine sont à définir** (nom, tirettes, percussion, vibrato, drive, Leslie, réverbération, volume).
Les 8 modèles de tirettes de départ de la banque A (avant déformation) sont dans `_seedBanks` de la maquette ; ils peuvent servir de base aux presets d'usine.
Un changement de réglage fait-il passer le preset en « modifié » (astérisque) ? La maquette ne le montre pas (pas d'astérisque dans l'en-tête) : à valider.

Contenu proposé d'un preset (état complet, §2.10) : tirettes, percussion, VIBE / VIB U / VIB L / VIB DEPTH, DRIVE, LESLIE, REVERB, VOLUME, splits, transpositions ; hors preset : banque, preset, canaux MIDI, CTRL CH, MOTOR NOISE, TUNE (proposé).

---

## 4. Sorties d'affichage (lecture seule, non automatisables)

| id | type | Contenu | Plage |
|---|---|---|---|
| `ham_midi_u_out` | output float | activité MIDI du manuel Upper (barre verticale « U » ; LED d'activité en LIVE) | 0…1 |
| `ham_midi_l_out` | output float | idem Lower (« L ») | 0…1 |
| `ham_midi_p_out` | output float | idem Pedal (« P ») | 0…1 |

- La maquette **simule** ces niveaux (sinusoïdes) ; la définition réelle est **proposée** : valeur = maximum des vélocités (v/127) des notes tenues du manuel au dernier bloc, avec une montée immédiate et une descente exponentielle (constante 0,3 s). Le maintien de crête (trait) est calculé côté UI : descente `×0,97` par image (≈ 60 images/s dans la maquette), visible seulement si le bouton PEAK est actif. LED d'activité LIVE : allumée tant que la valeur dépasse un seuil (maquette : 0,35 U, 0,32 L, 0,28 P ; proposé : un seul seuil à 0,02, ou « au moins une note tenue »).
- **Vu-mètre de sortie OUT (L / R)** : module commun `meter_*`, voir `NOVA_Common_modules_v0.1.md` (19 paramètres, non redécrit ici). OUT = sortie du plugin, après VOLUME. Le plugin n'a pas de vu-mètre IN. Bouton PEAK (maquette : actif au départ), voyant de saturation (en LIVE, « clip-led ») : comportements du module commun (la maquette allume le voyant quand une barre dépasse 0,72 en simulation ; seuil réel à reprendre du module commun). Trois mises en page de la même mesure : verticale (DRAWBARS), horizontale (LIVE), barres horizontales de la barre latérale (MATRICE).
- **Lampe Leslie** (en-tête, à gauche du libellé) : éteinte en BRK, allumée (accent) en CHOR, allumée avec la couleur « warn » en TREM. Elle suit **l'état demandé** (`ham_chorale`), pas la vitesse réelle des rotors (la maquette n'a pas d'inertie). Sorties facultatives proposées pour faire suivre la lampe à la vitesse réelle : `ham_leslie_horn_out`, `ham_leslie_drum_out` (float 0…1, vitesse du rotor / vitesse TREM) ; à confirmer (§7).

Tout le reste de l'affichage (tirettes, positions des potards, nom de preset, matrice) est calculé par l'UI à partir des paramètres.

---

## 5. UI seulement (non paramètres)

- **Pages** (barre de navigation de gauche) : DRAWBARS (par défaut), LESLIE, LIVE, MATRICE, SETTINGS. Dans la maquette, LESLIE et SETTINGS gardent la mise en page DRAWBARS (§7).
- **Matrice de presets** (page MATRICE, aide `ham_matrix`) : `ham_matrix_cols` (COLONNES, 1…8, défaut 4) et `ham_matrix_rows` (LIGNES, 1…8, défaut 3) : état d'affichage mémorisé avec l'état du plugin (proposé), non automatisable, hors preset. Le contrôle de chaque nombre est un stepper ‹ › **verrouillé** : un appui long (450 ms) sur une flèche le déverrouille et fait un premier pas ; pendant 10 s ensuite, les appuis courts font un pas ; chaque pas prolonge les 10 s. Total de dalles = colonnes × lignes ; les 18 premières montrent les 18 presets de la banque courante (dalle active = preset courant, clic = charge le preset), les suivantes sont vides (« — »).
  - **Champs de dalle** : liste ordonnée « AFFICHAGE DALLES » ; les 6 champs de la maquette sont Morceau, Atelier, Élève, Preset, Banque, Événement (ordre par défaut) ; ▲ ▼ réordonnent la liste ; le 1er champ s'affiche en grand, les 2ᵉ et 3ᵉ en second niveau, les suivants en troisième niveau. Le champ « Preset » vaut le nom du preset, « Banque » vaut « BANQUE x », les trois autres sont des **textes factices de la maquette** (18 morceaux, 18 ateliers, 18 élèves, 1 événement « Concert fin d'année »). Proposé : ces textes deviennent des métadonnées de chaque preset (champs libres `song`, `workshop`, `student`, `event`), enregistrées avec le preset ; l'ordre des champs (permutation des 6) fait partie de l'état d'affichage.
- **Claviers** (page DRAWBARS, panneau MANUALS ; masqués en LIVE) : deux claviers Upper (début MIDI 36) et Lower (début MIDI 24) ; nombre d'octaves affiché 1…10 (défaut 5, stepper « 5 octaves », appui long = répétition) ; les touches s'allument au clic / glisser. Dans la maquette, jouer sur ces touches **ne produit aucun son ni message** ; dans le plugin ils devraient générer des notes pour l'essai (proposé).
- **Contrôles et gestes** : potards à glisser vertical (200 px pour toute la course, soit `0,005` de la course par pixel), molette (+/− 3 % sur le potard, +/− 2 % sur la pill de valeur) ; VIBE : 14 px de glisser par cran, clic sur la pill = cran suivant (7 → 1) ; « num-pots » : 8 px par cran, molette = ±1 ; boutons de canal MIDI : 12 px par cran, clic = cran suivant ; tirettes : clic ou glisser vertical (haut = 0, bas = 8, valeur entière).
- **Autres** : bouton PEAK, thème (cycle Hammond sombre → Compresseur sombre → clair → haut contraste), contraste (niveaux 0…4), échelle de l'interface, boutons « ? », menu de preset de l'en-tête (pastille du nom).
- **Réglages communs à la série** (voir `NOVA_Common_modules_v0.1.md`) : thème, contraste, échelle, langue, aide « ? », sensibilité des potards.

---

## 6. Organisation de l'interface (gabarit 1180 × 1020 dans la maquette)

**En-tête** : logo NOVA / « TONEWHEEL ORGAN », pastille du preset courant, réglage du contraste, bouton de thème, pill LESLIE (lampe + CHORALE / TREMOLO / BREAK, un clic passe à l'état suivant).

**Colonne de gauche** (de haut en bas, alignée sur les lignes de la grille de droite)
1. Vu-mètres : MIDI U, L, P (barres verticales), canaux MIDI U / L / P (boutons), OUT stéréo + PEAK (module `meter_*`).
2. PRESETS : BANK (flèches + champ), PRESET (flèches + nom), liste de 7 lignes.
3. LESLIE : CHORALE / TREMOLO + BREAK.
4. Navigation : DRAWBARS · LESLIE · LIVE · MATRICE · SETTINGS.

**Page DRAWBARS** (grille de droite)
- Panneau DRAWBARS : titre, 3 manuels UPPER / LOWER / PEDAL (9 + 9 + 2 tirettes avec leur longueur en pieds et leur valeur).
- Panneau MANUALS : titre, SPLIT UPPER / SPLIT LOWER / TRANSP U-L-P, stepper d'octaves ; 2 claviers.
- Panneau PERCUSSION : 4 sélecteurs à 2 positions (PERCUSSION OFF/ON, VOLUME SOFT/NORMAL, DECAY FAST/SLOW, HARMONIC 2nd/3rd).
- Rangée de potards : VIBE (+ VIB U / VIB L), DRIVE (+ CHAR et GAIN avec interrupteur), carte PRESET STEPPER (emplacement réservé : la carte est vide dans la maquette), VIB DEPTH, LESLIE, REVERB (+ SIZE et DAMP), VOLUME.
- **Barre du bas** : MOTOR NOISE, CTRL CH, TUNE (cents), lampe d'alimentation.

**Page LIVE** : claviers masqués ; panneau DRAWBARS agrandi (Upper et Lower en colonne à gauche, Pedal à côté) avec SPLIT / TRANSP dans la barre de titre ; vu-mètres : OUT horizontal + PEAK + voyant de saturation, 3 LED d'activité MIDI, boutons de canal ; sélecteurs de percussion ; Leslie en 3 grands boutons carrés CHOR / TREM / BRK ; potards remplacés par des pills de valeur (potard et repères masqués) ; liste de presets avec les noms.

**Page MATRICE** : colonne de gauche = OUT horizontal, GRILLE (COLONNES, LIGNES), AFFICHAGE DALLES (liste ordonnée de champs, défilement), astuce ; panneau MATRICE PRESETS = grille de dalles.

**Pages LESLIE et SETTINGS** : mise en page DRAWBARS sans contenu propre dans la maquette (§7).

---

## 7. Points à valider

1. **Pages LESLIE et SETTINGS vides** : le script ne leur donne aucune mise en page propre. Rien n'est exposé pour les rotors (vitesses, accélérations, micros, croisement), pour le key click ni pour la précision / l'oversampling / la latence. Tout le §1.7 (rotors, micros) et §1.3 (key click, fuite) est « proposé ». À décider : les ajouter comme paramètres (page LESLIE : vitesses CHOR / TREM par rotor, accélération, distance des micros ; SETTINGS : précision, oversampling du drive, polyphonie, sensibilité).
2. **Routage MIDI** (§1.2) : sens exact de SPLIT UPPER / SPLIT LOWER (proposé : première note de la zone ; alternative : dernière note), interaction avec les canaux (le split s'ajoute-t-il au filtre de canal ?), note jouée sur plusieurs manuels, transposition appliquée avant ou après la zone, notes ignorées hors 0…127. Vélocité, pitch bend, sustain, CC (contrôleur) non définis.
3. **CTRL CH** : « Canal MIDI contrôleur » sans table de messages (proposé : Program Change = preset, Bank Select = banque). Messages à définir pour les tirettes, la vitesse Leslie, la percussion, le drive… (CC assignables ?).
4. **Preset = tirettes seulement dans la maquette**, alors que l'aide dit « tous les réglages (tirettes, percussion, Leslie…) ». Le contenu d'un preset du plugin (§3) et la liste des paramètres hors preset (canaux MIDI, CTRL CH, TUNE, MOTOR NOISE) sont à trancher. Presets d'usine à écrire (468 emplacements = valeurs factices) ; 26 banques × 18 presets = 468 emplacements fixes, ou nombre libre ? Indicateur « preset modifié » absent de la maquette.
5. **Variable `chainBanks`** (enchaîner les banques en défilant les presets) : code présent, mais aucun contrôle ni élément `psBankField` / `psPresetField` / `chainBanksSw` dans le HTML ; la carte « PRESET STEPPER » de la rangée de potards est vide. À supprimer ou à dessiner.
6. **Pedal à 3 valeurs** dans les presets (`p:[8,0,0]`) alors que l'interface n'a que 2 tirettes pédale : troisième valeur ignorée (pas de paramètre `ham_db_p3`).
7. **`state.leakage = 0,16`** : présent dans le script, sans contrôle ni effet. Ne pas créer de paramètre sans décision (fuite entre roues = crosstalk, à régler en interne ?).
8. **Percussion** : tout est proposé sauf les choix (2nd / 3rd, fast / slow, soft / normal) : temps (T = 0,2 s / 1,0 s), niveaux (0 / −6 dB), manuel Upper seul, déclenchement unique, coupure de la tirette 1′ (non appliquée ici), harmoniques (×2 / ×3).
9. **Tirettes** : loi de niveau (3 dB par cran, 0 = silence), normalisation de manuel (−12 dBFS), rapports de fréquence (déduits des longueurs : ×0,5 … ×8), référence 440 Hz, suppression des harmoniques au-delà de 0,45·fs, absence de repliement d'octave (foldback), absence de partage des roues entre touches (oscillateurs libres) : à valider avec le développeur DSP.
10. **Vibrato / chorus** : modèle (ligne à retard modulée à 6,8 Hz), profondeurs `c_max` 15 / 30 / 45 cents, rôle exact de VIB DEPTH par rapport à V1…V3 / C1…C3 (la maquette ne dit pas si VIB DEPTH multiplie la profondeur du mode ou la règle seul), Pedal non traité.
11. **Drive** : formule (tanh, pré-gain jusqu'à +26 dB, biais CHAR, GAIN ±12 dB), oversampling ×2 (non exposé), place avant la Leslie.
12. **Leslie** : voir point 1 ; en plus, vitesses (cornet 0,8 / 6,7 Hz, basses 0,67 / 5,7 Hz), inerties (0,5 / 1,0 s et 2,5 / 4,0 s), angle de repos en BRK, croisement 800 Hz, loi de mélange équi-puissance, rotor qui continue en arrière-plan à mix 0.
13. **Ordre de la chaîne** : Leslie avant réverbération (ordre des potards) ; sur l'orgue réel la réverbération est avant. Position du bruit de moteur (somme des manuels, avant DRIVE).
14. **Réverbération** : type (FDN 8 lignes) et lois de SIZE (T60 0,3…4 s, log) / DAMP (12 kHz…500 Hz, log) ; REVERB en envoi (somme) ou en mélange.
15. **VOLUME** : loi quadratique (v²) et absence de limiteur de sortie ; MOTOR NOISE niveau fixe −60 dBFS.
16. **Affichage de la maquette à corriger** : `TRANSP` affiche « − » pour toutes les valeurs ≤ 0 (négatives comprises) ; `SPLIT` affiche « − » pour 0 et pour −1 ; `masterTune` est le seul à afficher le signe. Le plugin doit afficher « 0 », « −12 », « +7 », et « 0 » pour la note MIDI 0. Canal OMNI codé −1 (maquette) et 0 (plugin).
17. **Automatisation** : tout ce qui touche au son est automatisable ; non automatisables : splits, canaux MIDI, CTRL CH, MOTOR NOISE, banque, preset (structure ou état de rig). Les transpositions sont automatisables mais ne s'appliquent qu'aux notes suivantes.
18. **Sorties MIDI** (`ham_midi_*_out`) : définition proposée (vélocité maximale, descente 0,3 s), seuils des LED. Sorties facultatives `ham_leslie_horn_out` / `ham_leslie_drum_out` pour que la lampe de l'en-tête suive la vitesse réelle.
19. **Claviers de l'interface** : la maquette ne génère pas de notes ; à décider si le plugin les fait jouer (vélocité fixe proposée 100). Octave de référence : début Upper = note 36, Lower = note 24 (noms C2 / C1 dans la maquette).
20. **Textes d'aide nouveaux** (`ham_perc_on`, `ham_vibe_mode`, `ham_vib_upper` / `ham_vib_lower`, `ham_vib_depth`, `ham_drive`, `ham_drive_char`, `ham_drive_gain`, `ham_leslie_mix`, `ham_reverb`, `ham_reverb_size`, `ham_reverb_damp`, `ham_volume`, `ham_midi_ch_u/l/p`, `ham_ctrl_midi_ch`, `ham_motor_noise`, `ham_master_tune`) : propositions de ce fichier, à reporter dans `NOVA_Hammond_help_v0.1.md` (non modifié ici).
21. **Total** : 51 paramètres (dont banque et preset), 2 réglages d'affichage (matrice), 3 sorties + 2 facultatives, et le module `meter_*` (OUT seulement ; 19 paramètres décrits dans `NOVA_Common_modules_v0.1.md`).
