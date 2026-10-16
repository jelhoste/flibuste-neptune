# setBfree — moteur d'orgue à roues phoniques

Source : https://github.com/pantherb/setBfree (commit `2f41983`, septembre 2026). Licence : **GNU GPL version 2 ou ultérieure** (en-tête
de chaque fichier : « either version 2, or (at your option) any later version »), compatible avec l'AGPLv3 de Neptune. Copyrights :
Fredrik Kilander (2003-2004), Robin Gareus (2008-2018), Ken Restivo (2010), Will Panther (2012) ; voir `AUTHORS` et
`COPYING-GPL-2.0.txt`.

On n'utilise que le moteur sonore, pas le plugin LV2 ni l'interface :

| Dossier | Fichiers repris | Rôle |
|---|---|---|
| `src/` | `tonegen.c/.h`, `vibrato.c/.h` (+ en-têtes `cfgParser.h`, `global_inst.h`, `main.h`, `midi.h`, `midi_types.h`, `program.h`) | générateur de roues, diaphonie, percussion, key click, scanner vibrato / chorus |
| `b_whirl/` | `whirl.c/.h`, `eqcomp.c/.h` | cabine Leslie (cornet et tambour, micros) |
| `b_overdrive/` | `overdrive.c/.h`, `filterTools.c/.h` | préampli à lampes (le code des fonctions de transfert est déjà généré dans `overdrive.c`) |
| `b_reverb/` | `reverb.c/.h` | réverbération à ressorts |

Non repris : `midi.c`, `program.c`, `state.c`, `cfgParser.c` (analyse de fichiers), `main.c`, `lv2.c`, interface, `overmaker`,
`ovt_*`, convolution. Leur rôle utile est tenu par `core/src/OrganCore.c` (registre de « fonctions de contrôle » à la place du
module MIDI, aides de lecture de configuration copiées de `cfgParser.c`).

## Modifications faites par Neptune

- `b_overdrive/overdrive.c` : les `printf` de débogage des fonctions de contrôle sont retirés (ils s'exécutaient dans le thread audio).
- `src/tonegen.h/.c` : `userTaperOn` / `userTaperDb[9][5]` et `neptuneUserTaperDb()` : correction de niveau (dB) par tirette et par plage d'octave, multipliée au modèle de pondération (`taperingModel`) dans `applyManualDefaults` ; sans effet tant que `userTaperOn` vaut 0 (mode classique).
- `compat/strings.h` : `<strings.h>` pour MSVC (`strcasecmp` -> `_stricmp`). Le reste des sources est inchangé.

## Limites connues

- La fréquence d'échantillonnage (`SampleRateD`) est globale au processus : deux instances à des fréquences différentes se
  partagent la dernière valeur.
- La réverb de setBfree n'a qu'un niveau (mix) : pas de taille ni d'amortissement.
- Le moteur utilise `rand()` (phases du Leslie et du scanner) : deux rendus ne sont pas identiques à l'échantillon près.
