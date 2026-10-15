# Modèle de données — Système de gestion des presets

Ce document reprend les décisions prises en conversation et les traduit en un schéma concret, prêt à servir de base d'implémentation. Format : JSON, un fichier par preset (voir §1), un manifeste léger par banque (§2), une configuration globale au niveau plugin (§3).

---

## 1. Fichier preset individuel

Extension proposée : `.flipreset`. Un fichier = un preset, indépendamment de sa banque d'appartenance (l'appartenance à une banque est une métadonnée indicative, pas structurelle — voir §2).

```jsonc
{
  "schemaVersion": 1,

  // Identifiant stable, indépendant du nom (utile pour dérivation, traçabilité, refs internes)
  "id": "b3f1a2c4-...-uuid",

  // Nom affiché — dans le champ compact, le stepper, le gestionnaire
  "name": "Ave Maria - Jeu doux",

  // Taxonomie libre avec autocomplétion suggérée (pas de liste fermée) :
  // instruments -> ex. "Lead", "Pad", "Registration liturgique"...
  // effets      -> ex. "Dynamique", "EQ/Filtre", "Temporel", "Modulation", "Saturation", "Utilitaire"
  "type": "Registration liturgique",

  // Plusieurs caractéristiques possibles, même logique d'autocomplétion
  "characteristics": ["doux", "chaud", "8 pieds"],

  // Note utilisateur, 0 = non noté, 1 à 3 étoiles
  "rating": 0,

  // Verrouillage utilisateur (distinct de la protection factory) : si true, "Save" est redirigé
  // vers "Save As" comme pour un preset factory. Activable/désactivable librement par l'utilisateur.
  "locked": false,

  // Champs personnalisés libres, créés par l'utilisateur, multi-valeurs par clé.
  // Recherche à facettes : OR entre valeurs d'une même clé, AND entre clés différentes.
  "customFields": [
    { "key": "Morceau", "value": "Ave Maria" },
    { "key": "Morceau", "value": "Panis Angelicus" },
    { "key": "Concert", "value": "Fin d'année 2026" }
  ],

  // Renseigné uniquement si ce preset dérive d'un preset factory via "Save As".
  // Purement informatif, aucun effet sur le comportement.
  "derivedFrom": null,

  // Valeurs des paramètres "capturés" au sens du switch par-voix (voir §3 pour les défauts globaux).
  // Seuls les paramètres capturés sont stockés ici ; un paramètre absent = non capturé pour ce preset,
  // donc valeur courante (globale/live) inchangée au chargement.
  "parameters": {
    "env.attack": 0.0,
    "env.release": 2.4,
    "filter.cutoff": 3200,
    "osc.waveform": "drawbar_88800000"
  },

  // Exceptions au défaut global de capture, par paramètre, pour CE preset uniquement.
  // Absent = comportement par défaut global appliqué (voir §3).
  "parameterCaptureOverrides": {
    // ex. "output.volume": "captured"  -> ce preset choisit de capturer un paramètre
    //     normalement libre par défaut
  },

  // Effets uniquement — cf. §5 (spécificités effets).
  "reverbEngine": "light",       // "light" | "convolution" | null si non applicable
  "reverbIrRef": null,           // id de la réponse impulsionnelle partagée, si "convolution"
  "declaredLatencySamples": 0,   // renégociation hôte nécessaire si ça change entre presets

  // Sustain pédale : notes tenues à la pédale traitées comme tenues au doigt lors d'un switch.
  // Défaut global avec override possible par preset (voir §3).
  "pedalHeldBehaviorOverride": null  // null = défaut global | "isolate" | "immediate"
}
```

---

## 2. Manifeste de banque (`banks.json`)

Un seul fichier pour tout le plugin, séparé des fichiers preset — renommer une banque ne touche donc aucun fichier preset.

```jsonc
{
  "schemaVersion": 1,
  "banks": [
    {
      "index": 2,                     // 0-127, aligné MIDI Bank Select
      "name": "Concert Noël 2026",    // défaut si non renseigné : lettre calculée (A-Z, A1-Z1, ...)
      "customFields": [
        { "key": "Concert", "value": "Fin d'année 2026" }
      ],
      "wrapToNextBank": false,        // comportement du stepper en fin de banque
      "presets": [
        // liste ordonnée d'ids de preset (référence vers les fichiers du §1), index = program 0-127
        "b3f1a2c4-...-uuid",
        null,
        "9a02ff31-...-uuid"
      ]
    }
  ]
}
```

Le fichier factory n'a pas d'entrée dans ce manifeste : il vit dans une bibliothèque séparée, hors de l'espace adressable 0-127 (voir §4).

---

## 3. Configuration globale du plugin (défauts, pas par preset)

```jsonc
{
  "schemaVersion": 1,

  // Défaut de capture par paramètre — "captured" = fait partie de l'identité du preset,
  // snapshotté par voix ; "free" = état global, jamais snapshotté, lu en direct par toutes les voix.
  "parameterCaptureDefaults": {
    "env.attack": "captured",
    "env.release": "captured",
    "filter.cutoff": "captured",
    "osc.waveform": "captured",
    "output.volume": "free",
    "leslie.speed": "free",
    "expression.pedal": "free"
  },

  // Défaut global du comportement pédale de sustain, overridable par preset (§1).
  "pedalHeldBehaviorDefault": "isolate",  // "isolate" (garde l'ancien son) | "immediate"

  // Réverbe partagée par génération (voir discussion) — bornes de sécurité.
  "reverbPool": {
    "maxConvolutionInstances": 2,
    "maxLightInstances": 3,
    "forcedFadeOutMs": { "light": 30, "convolution": 150 }
  },

  // Corbeille
  "trash": {
    "retentionDays": 14,
    "maxEntries": 50
  }
}
```

---

## 4. Espaces de stockage

- **Factory** : lecture seule, livré avec le plugin, hors adressage 0-127. Copie explicite vers une banque utilisateur pour devenir jouable (les fichiers copiés deviennent des presets utilisateur ordinaires, `derivedFrom` renseigné).
- **Utilisateur** : dossier applicatif standard de l'OS, lecture/écriture. C'est là que vivent `banks.json`, la config globale (§3), et tous les fichiers preset utilisateur (§1).
- **Corbeille** : sous-dossier horodaté (`trash/AAAA-MM-JJ_HHhMM_<label>/`), purge selon §3.

---

## 5. Notes de versioning

- `schemaVersion` présent dans chaque type de fichier (preset, banque, config).
- Paramètre absent d'un fichier ancien → valeur par défaut appliquée (même mécanisme que "non capturé").
- Clé inconnue dans un fichier plus récent → ignorée silencieusement (pas d'erreur).
- Changements de structure profonds → chaîne de fonctions de migration `v1→v2`, `v2→v3`, appliquées successivement.
- Import inter-utilisateurs avec version supérieure à celle connue localement → import tenté quand même, avertissement non bloquant sur les réglages ignorés.

---

## 6. Extensions de fichiers

Communes à toute la gamme de plugins (instruments et effets partagent le même modèle) :

- Preset unique : `.flipreset`
- Bundle de banque exportée : `.flibank`

## 7. Format du bundle de banque exportée (`.tbank`)

Archive zip. Réutilise tel quel le schéma preset du §1 — aucun format dupliqué.

```
mon-export.flibank  (= fichier zip)
├── bundle-manifest.json
│   {
│     "schemaVersion": 1,
│     "bankName": "Concert Noël 2026",
│     "bankCustomFields": [{ "key": "Concert", "value": "Fin d'année 2026" }],
│     "presetOrder": ["<id1>", "<id2>", "..."]
│   }
└── presets/
    ├── <id1>.flipreset
    ├── <id2>.flipreset
    └── ...
```

`presetOrder` référence des ids, pas des positions 0-127 — l'import réassigne les positions dans la banque cible choisie par l'utilisateur (voir §4, geste explicite requis, pas d'automatique).

## 8. Structure de la bibliothèque factory

Même schéma de fichier preset que l'utilisateur (§1), racine séparée, organisée par catégorie pour la maintenance côté développeur — l'utilisateur ne navigue jamais cette arborescence directement (recherche à facettes uniquement) :

```
factory/
├── instruments/
│   └── <id>.flipreset
└── effects/
    └── <id>.flipreset
```

---

## 9. Brainstorm — paramètres capturés/libres par type d'instrument

À affiner au fil de l'implémentation réelle de chaque instrument ; point de départ basé sur le principe : capturé = identité sonore snapshottée par voix, libre = état global (physique/partagé/performance).

### Orgue Hammond (tonewheel)

| Paramètre | Statut | Pourquoi |
|---|---|---|
| Tirettes (niveaux des 9 drawbars) | Capturé | Identité sonore du jeu |
| Percussion (on/off, harmonique, decay, volume) | Capturé | Fait partie du timbre déclenché à l'attaque |
| Key click | Capturé | Transitoire propre à chaque attaque |
| Vitesse Leslie | Libre | Objet physique unique et partagé |
| Volume de sortie général | Libre | Performance, pas identité sonore |

### Orgue liturgique

| Paramètre | Statut | Pourquoi |
|---|---|---|
| Jeux tirés (registration) | Capturé | Identité sonore du jeu |
| Accouplements | Capturé | Fait partie de la registration |
| Tremblant | Libre | Système de vent partagé physiquement, même logique que le Leslie |
| Pédale d'expression (boîte expressive) | Libre | Contrôle de performance en temps réel |
| Volume de sortie général | Libre | Idem Hammond |

### Synthé soustractif générique

| Paramètre | Statut | Pourquoi |
|---|---|---|
| Forme d'onde, accordage, unisson/detune | Capturé | Identité sonore |
| Enveloppes (ampli, filtre, modulation) | Capturé | Cas motivant la conception par-voix |
| Cutoff/résonance filtre | Capturé | Identité sonore |
| LFO (si routé en modulation de timbre) | Capturé | Fait partie du caractère du son |
| Glide/portamento | Libre | Technique de jeu, pas propriété du son |
| Pitch bend range, mapping molette/macro | Libre | Configuration de performance/contrôleur |
| Volume de sortie général | Libre | Constante dans les trois familles |

**Régularité observée** : le volume de sortie général est systématiquement libre ; tout système physique partagé unique (Leslie, tremblant) suit la même logique — candidat à un défaut recommandé plutôt qu'à redéfinir à chaque instrument.

---

## 10. Statuts de capture (version finale)

Suite à l'analyse des cas concrets (§9 et discussion), le modèle se stabilise sur deux statuts principaux, plus un cas réservé aux effets à traîne propre :

- **`captured`** (par voix) — snapshotté à l'attaque, figé jusqu'au relâchement. Oscillateurs, enveloppes, filtre, tirettes, jeux tirés, key click, pitch bend range/mapping (interprétation figée par voix, pas la valeur live elle-même).
- **`free`** (global, immédiat) — pas de snapshot, change immédiatement pour toutes les voix au switch. Leslie, tremblant (par clavier), volume de sortie du preset (cascade de gain sur bus master, avant le volume maître du plugin qui, lui, n'est jamais lié aux presets), pédale d'expression, glide/portamento.
- **`bus`** (par génération) — instance dédiée par génération de preset, tant qu'une voix de cette génération est active ou qu'une traîne est encore audible ; pool borné avec extinction forcée au-delà d'une limite (voir §3, `reverbPool`). Réservé aux effets qui excitent une résonance indépendante du signal d'entrée : réverbe, delay avec feedback.

Cas particulier `pitch bend` : par défaut la voix qui se relâche ignore les messages de pitch bend post-switch, seule la nouvelle voix applique la nouvelle configuration. Option par preset pour inverser ce comportement (nouveau réglage appliqué aux deux).

Cas particulier `locked` (déjà en §1) : verrouillage utilisateur d'un preset (indépendant de la protection factory), redirige Save vers Save As.

---

## 11. Grille concert (Live Set)

Vue complémentaire au stepper séquentiel — accès direct par tuile plutôt que défilement linéaire, utile quand l'ordre réel diverge du programme prévu (réorganisation en direct). Ne modifie ni ne duplique aucune donnée existante : référence des presets déjà stockés.

```jsonc
{
  "schemaVersion": 1,
  "gridId": "grid-uuid-...",
  "name": "Concert fin d'année 2026",
  "locked": true,                 // verrou global de la GRILLE (disposition), distinct du "locked" par preset (§1, contenu sonore)
  "linkedConcertField": "Concert de fin d'année 2026",  // optionnel : pré-remplissage suggéré depuis le champ personnalisé "Concert", non recalculé automatiquement ensuite
  "tiles": [
    { "position": 0, "presetRef": "<id1>" },
    { "position": 1, "presetRef": "<id2>" },
    { "position": 2, "presetRef": null }   // tuile vide, réservée mais non assignée
  ]
}
```

**Comportements associés (UI, pas de champ de données supplémentaire) :**

- Tuiles verrouillées par défaut ; appui long sur le cadenas de la grille pour déverrouiller.
- Assignation possible depuis la grille déverrouillée (tap tuile → choisir preset), ou depuis le preset lui-même ("assigner à un bloc", sans déverrouillage nécessaire).
- Alerte de confirmation avant d'écraser une tuile déjà occupée, quelle que soit l'origine de l'assignation.
- Réorganisation par glisser-déposer via une poignée dans un coin de la tuile (pas la tuile entière), pour que le tap simple reste dédié à l'édition/assignation.
- Vérification d'intégrité automatique et silencieuse au chargement de la grille (existence des `presetRef`) ; badge discret sur la grille uniquement si un problème est détecté ; détail et réparation dans le menu général, jamais sur les commandes live.
- Slot vide adressé en concert (MIDI PC sans correspondance) : le preset actuel reste chargé, aucune interruption ni choix demandé dans l'instant. La confirmation ("passer sur un init ?") n'a lieu que dans le gestionnaire/en préparation, jamais en contexte live.
