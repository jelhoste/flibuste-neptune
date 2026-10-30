# Plan : matrice de modulation et LFO dans le synthé soustractif en plugin seul (AnalogSynth)

Écrit le 2026-10-30. Rien n'est codé. Décision de Jean : « allons-y », en commençant par une modulation **globale**, avec l'idée d'étendre ensuite à d'autres plugins de la suite.

## But

Dans Neptune, une piste Synthé a déjà la matrice de modulation de la piste (4 LFO, 4 enveloppes, jusqu'à 32 routes ; onglet MOD). Le plugin **AnalogSynth** seul n'a rien de cela. On lui ajoute une page MOD qui utilise **la même matrice**, sans toucher au synthé dans Neptune.

## Ce qui existe déjà (relu dans le code le 2026-10-30)

| Pièce | Où | Utilité |
|---|---|---|
| `modulation::Matrix`, `compile`, `Modulator` (LFO, enveloppes, sources MIDI) | `core/include/neptune/core/Modulation.h` | le moteur de la matrice ; cœur portable, sans JUCE |
| Décalages de modulation par réglage, lus par le moteur du synthé (`effectiveAt`) | `Parameter.h`, `AnalogSynth.cpp` | le synthé lit déjà les valeurs modulées |
| `midi::State` (CC, pitch bend, aftertouch par canal) | `MidiFilter.h` | sources MIDI de la matrice |
| `ModView` (onglet MOD, 410 lignes) | `adapter-juce/src/ModView.*` | l'interface ; liée aujourd'hui à `NeptuneProcessor` |
| `SnapshotCommand` | `Undo.h` | annulation d'un changement de matrice |
| `Preset::facade` | `Preset.h` | modèle pour un champ de preset facultatif que les autres plugins gardent sans le lire |

## Étapes

**1. Détacher l'interface (Neptune ne doit pas changer).** `ModView` n'appelle que quelques fonctions de `NeptuneProcessor` : piste choisie, matrice de la piste, modification annulable, début et fin de geste, nom d'une destination, liste des réglages, cibles d'insert. On les met derrière une petite interface `ModulationHost`. `NeptuneProcessor` l'implémente avec le même comportement qu'aujourd'hui. Vérification : la liste des comportements de l'onglet MOD de Neptune ne change pas (essai de Jean dans Reaper).

**2. Un « rack » de modulation dans le cœur.** Une classe réutilisable (`ModulationRig`) qui regroupe : la matrice, sa version compilée passée au thread audio sans verrou, le `Modulator`, le `midi::State`, le suivi des touches tenues, et la remise à zéro des décalages du bloc précédent. Même schéma que dans `Engine.cpp` pour une piste. Tests du cœur : un LFO sur un réglage fait bouger le décalage ; sans route, aucun décalage ; retirer une route remet le décalage à zéro ; aller-retour JSON de la matrice.

**3. Branchement dans le plugin AnalogSynth.**
- `processBlock` : les messages MIDI (contrôleurs, pitch bend, pression, notes) alimentent le rack ; le rack est évalué une fois par bloc avant le rendu, comme dans Neptune.
- État du plugin : la matrice est ajoutée au JSON d'état (champ facultatif ; un ancien état se relit comme avant).
- Presets : champ facultatif `modulation` dans le preset, sur le modèle de `Preset::facade` ; un preset sans matrice laisse la matrice du plugin à vide.
- Annulation : un `SnapshotCommand` par changement de matrice, dans la pile d'annulation qui existe déjà.
- Les réglages ne changent pas : mêmes identifiants, même nombre de paramètres pour l'hôte.

**4. Page MOD dans l'éditeur du synthé.** Un onglet de plus dans `NovaSynthEditor`, qui réutilise `ModView` avec l'interface de l'étape 1. Les potards du synthé montrent déjà un anneau de modulation (même mécanisme que dans Neptune).

**5. Essais et livraison.** Tests du cœur ; compilation Linux et Windows de AnalogSynth et de Neptune (l'interface partagée change) ; installation ; mise à jour de `echanges/etat-versions.md` ; message au banc.

## Périmètre de la première version

- **Mêmes limites que Neptune** : 4 LFO, 4 enveloppes, 32 routes. Avantage : le format de la matrice est identique des deux côtés, et on pourra plus tard copier une matrice entre Neptune et le synthé seul.
- **Modulation globale** : les LFO et les enveloppes sont communs à toutes les notes ; les enveloppes de la matrice démarrent à la première touche tenue (comme pour une piste de Neptune).
- Calcul par bloc, comme dans Neptune.

## Ce que cette version ne fait pas

- Pas de modulation **par note** (un LFO ou une enveloppe qui a sa propre phase pour chaque note). Cela touche les voix (`SynthVoices`) et fait l'objet d'une seconde phase, seulement si Jean le demande après avoir entendu la première.
- Pas de nouvelles sources (vélocité par note, touche jouée) : seulement celles de la matrice de Neptune.

## Suite possible, pas décidée

Étendre aux autres plugins qui ont des réglages modulables, avec la même pièce (`ModulationRig`) et la même page : FM6Op et Orgamon d'abord (instruments), puis les effets si cela a un sens. L'étape 2 est faite pour être reprise telle quelle.

## Risques connus

- L'interface de l'onglet MOD est partagée avec Neptune : une erreur dans l'étape 1 se verrait dans Neptune. D'où la vérification dédiée.
- Des LFO calculés par bloc peuvent « escalader » sur un vibrato rapide avec un gros bloc : même limite que Neptune, jamais mesurée.
- Les anciens états et presets du synthé doivent se relire à l'identique (test d'aller-retour octet pour octet, comme pour les autres plugins).
