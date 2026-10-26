# Combinaisons d'orgue (palier 7) — spécification

Statut : **cœur écrit et testé** (`core/include/neptune/core/Combinations.h`, `core/src/Combinations.cpp`, `tests/test_combinations.cpp`). Interface, pistons de la Façade, MIDI et OSC : **pas encore faits** (voir « Étapes suivantes »).

Décisions de l'utilisateur (2026-10-26, par questions à choix) : voir « Décisions ». Ce qui suit les complète par des choix de détail, signalés « à confirmer » quand ils ne viennent pas de l'utilisateur.

## Vocabulaire

- **Piston** : un bouton qui rappelle une **mémoire** (une combinaison enregistrée).
- **Général** : mémoire qui règle toute la console (jeux, accouplements, transpositions).
- **Divisionnel** : mémoire propre à un clavier (une piste) ; ne règle que les jeux de ce clavier.
- **Annulation générale** (« général cancel ») : repousse tous les jeux et accouplements.
- **Séquenceur** : liste ordonnée de généraux parcourue pas à pas (« suivant », « précédent »).
- **Set** : touche d'enregistrement. On règle la console, on appuie sur Set, puis sur un piston : l'état courant y est mémorisé.

## Décisions

| Sujet | Décision |
|---|---|
| Ce qu'enregistre un général | Les **jeux** (modulateurs des Groupes du Sampler, ids `14000 + 16·t + i`), les **32 tirants du routeur** (ids `15000 + i`) et les **transpositions de règles** (ids `15100 + i`). Pas le mixeur ni les réglages sonores des pistes. |
| Ce que fait un divisionnel | Ne règle que les **jeux d'un seul clavier** (les 16 modulateurs de sa piste). Les autres claviers, les accouplements et les transpositions ne bougent pas. |
| Organisation | **Banques** de pistons numérotés, chaque mémoire peut porter un **nom**. |
| Annulation générale | **Tout repousser** : jeux à 0 %, tirants à 0, transpositions à 0. Les mémoires ne changent pas. |
| Enregistrement | **Set + piston**. |
| Séquenceur | Liste de généraux **pas à pas** : « suivant » et « précédent ». |
| Rappel | Pistons de la Façade, notes et CC MIDI, changement de programme MIDI, OSC. |
| Stockage | Dans le **projet** (`MixerState`), donc dans les presets globaux `.neptuneset`, et annulable. |

## Choix de détail (à confirmer)

- **Taille** : 10 banques de 10 pistons (numérotés 1 à 10 à l'écran). Les généraux ont leurs banques ; chaque clavier (piste) a les siennes pour ses divisionnels. La **banque courante** est unique, partagée par les généraux et les divisionnels (comme sur un orgue à banques). Stockage clairsemé : seules les mémoires enregistrées occupent de la place.
- **Set** : deux façons, qui coexistent. *Maintenu* : tant que la touche est tenue, un piston enregistre. *Armé* : un appui arme Set, le prochain piston enregistre puis Set se désarme. Enregistrer dans un piston déjà plein **écrase** (l'écran demandera confirmation ; le cœur n'en sait rien).
- **Rappel d'une mémoire vide** : ne change rien (le piston est signalé « vide »).
- **Rappel d'une mémoire** : règle les paramètres qu'elle contient. Un paramètre qui n'existe plus est ignoré. Un paramètre absent de la mémoire (ajouté depuis) n'est pas touché.
- **Séquenceur** : position de départ « avant le premier pas » ; « suivant » va au pas 1 ; aux extrémités il reste en place (pas de bouclage). Si l'on rappelle directement un général qui figure dans la liste, la position se cale sur lui. La position n'est pas enregistrée dans le projet (état de jeu).
- **Changement de programme MIDI** : programme 0 à 9 = piston 1 à 10 de la banque courante (général). Au-delà : ignoré. Un autre mapping (divisionnels, changement de banque) se décidera quand l'interface existera.
- **Jeux = modulateurs** : le mute de piste (autre représentation d'un jeu, cf. palier 7) **n'est pas** couvert pour l'instant : l'annulation générale couperait toutes les pistes, y compris des pistes qui ne sont pas des jeux. À rediscuter si besoin.
- **Annulation générale et modulateurs** : tous les modulateurs de toutes les pistes passent à 0, y compris ceux qui ne sont pas des jeux d'orgue (articulations, par exemple). À rediscuter (restreindre aux pistes d'un clavier de la Façade ?).

## Modèle de données (cœur)

`neptune::core::combi` :

- `Memory` : `used` (enregistrée), `name` (64 caractères au plus), `values` : liste de (ParamId, valeur réelle). Le contenu est la valeur réelle (pas normalisée), comme l'instantané du widget Action de la Façade.
- `Library` : mémoires des généraux (clef = banque·10 + piston), des divisionnels (clef = (piste·10 + banque)·10 + piston) et la **séquence** (liste de clefs de généraux, 64 pas au plus). `operator==`, `toJson`, `fromJson` (lecture tolérante, bornes appliquées).
- `consoleParams` / `stopParams (piste)` : les identifiants concernés, filtrés sur ceux que le jeu de paramètres connaît.
- `capture`, `recall`, `cancelAll` : fonctions pures sur un `ParameterSet`. `recall` et `cancelAll` rendent la liste des changements réellement faits (pour l'annulation et le retour visuel).
- `Controller` : état de jeu (banque courante, Set armé ou maintenu, position du séquenceur) et les actions : `general`, `divisional`, `cancel`, `sequenceNext`, `sequencePrevious`, `programChange`, changement de banque. Chaque action rend un résultat (`Recalled`, `Stored`, `Empty`, `Ignored`).

Stockage : `MixerState::combinations` (JSON `combinations`, absent si vide).

```json
"combinations": {
  "generals": [ { "bank": 0, "piston": 0, "name": "Plein-jeu", "values": [[14000, 100], [15000, 1]] } ],
  "divisionals": [ { "track": 1, "bank": 0, "piston": 2, "name": "Fonds 8'", "values": [[14016, 100]] } ],
  "sequence": [ { "bank": 0, "piston": 0 }, { "bank": 0, "piston": 1 } ]
}
```

## Étapes suivantes (non faites)

1. **Branchement du moteur et de l'adaptateur** : `NeptuneProcessor` possède un `Controller` ; les actions s'exécutent sur le fil de message, avec une entrée d'annulation (comme `uiEditText`), pour que les rappels soient annulables.
2. **Déclencheurs** : paramètres-impulsions « Combination General 1..10 », « Divisional 1..10 » par clavier, « Cancel », « Set », « Next », « Previous », « Bank + / − », pour qu'ils soient pilotés par CC, notes, OSC (`/neptune/combination/...`), automation et Façade. Changement de programme MIDI via `Controller::programChange`.
3. **Façade** : widgets piston (voyant allumé quand l'état courant égale la mémoire), Set, Annulation générale, suivant / précédent, affichage de la banque et du pas.
4. **Onglet ou fenêtre de gestion** : noms, copie, effacement, édition de la séquence.
5. Mise à jour de `docs/passage-de-relais.md` et de la roadmap (palier 7).
