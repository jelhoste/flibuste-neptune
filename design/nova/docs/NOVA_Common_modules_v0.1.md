# NOVA — modules communs v0.1 (vu-mètres `meter_*`, sidechain `sc_*`)

Référence unique pour les modules partagés par tous les plugins de la série. Les fichiers de paramètres des plugins renvoient ici et ne les redécrivent pas.
Format de description : celui de `NOVA_TransientShaper_params_v0.2.md` §0.
Source du module vu-mètres : la réponse du DSP de convolution (`NOVA_LowLatConv_BRIEF_DSP_reponses.md` §5, `meters.hpp` testé) pour ce qui existe déjà côté DSP ; les ajouts demandés par les maquettes sont marqués **proposé**.

---

## 1. Module vu-mètres (préfixe `meter_`)

### 1.1 Points de mesure et principe
- **IN** = signal d'entrée de la chaîne **après le gain d'entrée** quand le plugin en a un (convolution), sinon entrée du plugin. **OUT** = sortie finale, après le gain de sortie / MIX. Chaque plugin précise son cas dans son fichier ; le Hammond n'a pas de IN.
- L et R séparés pour crête et RMS ; LUFS calculés sur L+R sommés. Plancher de toutes les valeurs : **−120**. Pas de crête vraie (true peak).
- **Transmission DSP → UI (décision du 2026-10-18)** : cadre JUCE + cœur partagé (voir `NOVA_Decisions_transversales.md`). Le DSP et l'interface sont dans le même binaire : les mesures sont des valeurs atomiques écrites par le DSP à chaque bloc (crête = maximum depuis la dernière lecture de l'interface, donc aucune crête manquée), lues par l'interface à 30–60 Hz. **Ce ne sont plus des paramètres d'hôte** : la liste de l'hôte ne montre que les réglages. Le tableau « sorties » ci-dessous décrit donc des valeurs internes, pas des ids d'automation.

### 1.2 Réglages (seuls paramètres enregistrés avec le preset)
| id | label | type | plage | défaut | auto | notes |
|---|---|---|---|---|---|---|
| `meter_rms_ms` | RMS window | float (ms) | 10…3000 | 300 | non | fenêtre RMS **rectangulaire** calculée par le DSP, partagée IN/OUT ; un profil RMS personnalisé = changer cette valeur |
| `meter_peak_decay` | Peak decay | float (dB/s) | 5…300 | 40 | non | retombée d'affichage de la crête (interface) |

`meter_reset` n'est plus un paramètre : c'est une action de l'interface (clic sur la valeur MAX ou sur le voyant).

### 1.3 Mesures internes (lecture seule, pas de paramètres d'hôte)
| nom interne | contenu | mode d'affichage |
|---|---|---|
| `in_peak_l/r`, `out_peak_l/r` | crête depuis la dernière lecture de l'interface, dBFS | PEAK |
| `in_rms_l/r`, `out_rms_l/r` | RMS sur `meter_rms_ms`, dB (sinusoïde pleine échelle = −3,01) | RMS / profils RMS, et VU |
| `in_lufs_m`, `out_lufs_m` | LUFS momentané (pondération K, ITU-R BS.1770, fenêtre 400 ms) | EBU-M |
| `in_lufs_s`, `out_lufs_s` | LUFS court terme (3 s) | EBU-S |
| `in_lra`, `out_lra` | LRA en LU, norme EBU R128 (**compresseur seulement**) | barre LRA |

Crêtes maximales (valeur MAX, trait de maintien), retombée, remise à zéro et voyant de saturation : **interface seulement**, à partir des crêtes ci-dessus. Plancher de toutes les valeurs : −120. Le DSP s'appuie sur le calcul de loudness du cœur (libebur128, déjà utilisé pour le Master de Neptune).

Les ids `meter_*` des 16 anciennes sorties DPF disparaissent de la liste d'hôte ; les valeurs de vérification du DSP de convolution (1 kHz à −23 dBFS = −23,0 LUFS ; −20 dBFS = crête −20,00 / RMS −23,01) restent les tests de référence.

### 1.4 Ce qui reste côté UI
- **VU** : RMS avec `meter_rms_ms` = 300 ms puis balistique d'affichage (constante de 300 ms ; décalage d'affichage proposé −3 dB).
- Bouton de mode (PEAK → profils RMS → VU → EBU-M → EBU-S, un par vu-mètre IN et OUT), voyant de saturation (allumé quand la crête max dépasse −1 dBFS), clic sur la valeur ou le voyant = remise à zéro des crêtes max.
- **Profils RMS** (Settings → METERING) : nom, durée en ms. Fenêtre **rectangulaire seulement** (HANN retiré de la maquette du compresseur) ; attaque et relâchement = lissage d'affichage.
- **LRA** (compresseur) : valeur en LU entre IN et OUT, norme EBU R128 uniquement (`meter_lufs_standard` retiré).
- Vu-mètre bipolaire GAIN du shaper et vu-mètres de réduction de gain du compresseur : valeurs internes propres au plugin.

---

## 2. Module sidechain (préfixe `sc_`) — fusionné le 2026-10-18

Module standard de la série : activation, source (Internal / External / Mix / MIDI), gain, phases, EQ de détection à 5 zones avec analyseur, LISTEN. Base : la maquette du compresseur (la plus complète). Dans un **plugin indépendant**, « External » est l'entrée sidechain de l'hôte. Dans un **insert de Neptune**, External pourra désigner une piste ou un bus interne (`sc_ext_strip`, `sc_ext_tap`, ids réservés ; routage à écrire plus tard, il touche l'ordre de traitement du moteur).

### 2.1 Source, gains et déclenchement
| id | label | type | plage | défaut | unité · échelle | auto | smooth | contrôle · depends |
|---|---|---|---|---|---|---|---|---|
| `sc_source` | SIDECHAIN SOURCE | enum | 0 Internal, 1 External, 2 Mix, 3 MIDI | 0 | — | non | fondu 10 ms | pill cyclique INTERNAL → EXTERNAL → MIX → MIDI |
| `sc_gain_db` | GAIN | float | −24…+24 | 0 | dB · bipolar | oui | 20 ms | curseur « GAIN » ; Internal et External seulement |
| `sc_comp_vol_db` | COMP VOL | float | −24…+24 | 0 | dB · bipolar | oui | 20 ms | même curseur, rebaptisé en mode Mix |
| `sc_ext_vol_db` | EXT VOL | float | −24…+24 | 0 | dB · bipolar | oui | 20 ms | curseur « EXT VOL » ; Mix seulement |
| `sc_phase_comp` | Ø COMP | bool | — | 0 | — | oui | — | bouton Ø (phase de la voie interne) ; Mix seulement |
| `sc_phase_ext` | Ø EXT | bool | — | 0 | — | oui | — | bouton Ø (phase de la voie externe) ; Mix seulement |
| `sc_midi_shape` | SHAPE (MIDI) | float | −100…+100 | 0 | % · bipolar | oui | 20 ms | courbe cliquable (glisser verticalement) à la place du curseur GAIN ; MIDI seulement ; affiché « LINEAR / EXP x % / LOG x % » |
| `sc_ext_ms` | M/S (externe) | bool | — | 0 | — | non | — | switch dans l'en-tête de l'égaliseur ; visible en External seulement |
| `sc_listen` | LISTEN | bool | — | 0 | — | oui | fondu 10 ms | pill avec voyant |
| `sc_midi_channel` | MIDI trigger channel | enum | 0 ALL, 1…16 | 0 | — | non | — | Settings · AUDIO / DSP |


Ajouts de la fusion :

| id | label | type | plage | défaut | auto | notes |
|---|---|---|---|---|---|---|
| `sc_enable` | SIDECHAIN | bool | — | 1 | oui | activation du module (le compresseur la force à 1 et ne la montre pas ; le shaper la propose) |
| `sc_integration` | INTEGRATION | float (ms) | à fixer avec le plugin | — | oui | **optionnel** : présent seulement pour un plugin sans suiveurs propres ; masqué pour le shaper et le compresseur |
| `sc_ext_strip` | EXTERNAL STRIP | enum | 0 entrée de l'hôte, 1…n pistes / bus | 0 | non | **réservé, Neptune seulement** (insert) |
| `sc_ext_tap` | EXTERNAL TAP | enum | 4 points de captation | 3 | non | **réservé, Neptune seulement** |

Notes :
- Le curseur du « slot A » change de rôle : GAIN (Internal, External), COMP VOL (Mix), courbe SHAPE (MIDI). Le « slot B » (EXT VOL) n'existe qu'en Mix. Un seul jeu de valeurs par rôle : changer de mode ne les efface pas.
- `sc_ext_ms` : proposé : ON = les deux canaux de l'entrée externe sont déjà M et S ; OFF = ils sont L et R (convertis en M/S si le mode de canaux est M-S). La maquette ne dit que « M/S » en External.
- Les autres éléments du module standard absents de la maquette du compresseur (interrupteur d'activation du sidechain, intégration, courbe de transfert libre) ne sont pas repris ici.
- Aide en ligne : seule `sc_source` a un « ? » (texte ci-dessous). `comp_detect` est en §2.3.

### 2.2 Égaliseur de détection
Cinq zones : HIGH-PASS, BAND 1, BAND 2, BAND 3, LOW-PASS. Le switch « EQ » de l'en-tête (`sc_eq_on`) grise les cinq zones et la courbe (opacité réduite, non interactif) et contourne l'égaliseur quand il est désactivé. Le switch « SPECTRUM » (`sc_analyzer_on`) active l'analyseur de fond. L'EQ agit sur la clé uniquement, jamais sur l'audio.

| id | label | type | plage | défaut | unité · échelle | auto | smooth | contrôle |
|---|---|---|---|---|---|---|---|---|
| `sc_eq_on` | EQUALIZER | bool | — | 1 | — | oui | fondu 10 ms | switch de l'en-tête |
| `sc_analyzer_on` | SPECTRUM | bool | — | 1 | — | non | — | switch de l'en-tête |
| `sc_hp_freq` | HIGH-PASS FREQ | float | 20…2000 | 80 | Hz · log | oui | 20 ms | potard FREQ ; point déplaçable sur la courbe (horizontal) |
| `sc_hp_slope` | HIGH-PASS SLOPE | int | 6…48 (pas de 6) | 12 | dB/oct | non | — | potard SLOPE (la maquette est continue, voir §7) |
| `sc_lp_freq` | LOW-PASS FREQ | float | 1000…20000 | 16000 | Hz · log | oui | 20 ms | potard FREQ ; point déplaçable |
| `sc_lp_slope` | LOW-PASS SLOPE | int | 6…48 (pas de 6) | 12 | dB/oct | non | — | potard SLOPE |
| `sc_band1_freq` | BAND 1 FREQ | float | 20…20000 | 250 | Hz · log | oui | 20 ms | potard FREQ ; point déplaçable |
| `sc_band1_gain` | BAND 1 GAIN | float | −18…+18 | 0 | dB · bipolar | oui | 20 ms | potard GAIN ; point déplaçable (vertical) |
| `sc_band1_q` | BAND 1 Q | float | 0,3…12 | 1,0 | — · log (proposé) | oui | 20 ms | curseur Q sous la zone |
| `sc_band2_freq` / `_gain` / `_q` | BAND 2 | float | idem bande 1 | 1200 / 0 / 1,0 | idem | oui | 20 ms | idem |
| `sc_band3_freq` / `_gain` / `_q` | BAND 3 | float | idem bande 1 | 5000 / 0 / 1,0 | idem | oui | 20 ms | idem |


Filtres (proposé) : passe-haut et passe-bas Butterworth, ordre `slope / 6` (cascade de sections du 1er et du 2ᵉ ordre) ; bandes en cloche (peaking) du 2ᵉ ordre. La courbe de la maquette est une approximation visuelle (gaussienne pour les bandes, pente linéaire en dB/octave pour les filtres) ; l'UI doit recalculer la réponse réelle à partir des mêmes biquads que le DSP. Pas de type de bande, ni d'activation par bande, ni d'étagères dans la maquette. Le switch M/S de l'EQ de la maquette n'existe pas pour l'EQ : le `M/S` de l'en-tête est `sc_ext_ms` (§2.4).


### 2.3 Aide en ligne
#### sc_source
- enum : `kParamScSource` · label : SIDECHAIN SOURCE · type : enum · auto : non
- help_short : Ce que le détecteur écoute.
- help_long : Internal : le signal du plugin lui-même. External : l'entrée sidechain de l'hôte (par exemple la grosse caisse pour pomper une basse). Mix : mélange des deux. LISTEN fait entendre le signal de détection pour vérifier ce qui déclenche la compression.
- meta : Choix : Internal, External, Mix


---

## 3. Décisions
Les points 1 à 6 de la discussion de relecture sont tranchés : voir `NOVA_Decisions_transversales.md`.
