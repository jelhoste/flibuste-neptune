# Passage de relais — état du projet Neptune

Note destinée à une nouvelle session de travail. Elle complète `brief-projet-sampler-clap.md`, `roadmap-sampler-clap.md` et `spec-modules.md` (qui restent la référence fonctionnelle). Dernière mise à jour : 2026-10-11.

## Règles de travail de l'utilisateur

- **Règle absolue : ne jamais deviner.** Regarder le code ou la documentation, ou demander à l'utilisateur. Quand une information n'est pas vérifiée, le dire.
- L'utilisateur est francophone ; répondre en français. Il aime qu'on le prévienne si une décision de design entre en conflit avec une fonctionnalité.
- Les actions visibles de l'extérieur (créer un dépôt, publier, pousser une release) se confirment avant.
- Il ne lit pas le code : les résumés doivent dire ce qui change *pour lui* (ce qu'il verra ou testera dans Reaper).

## Le projet en une phrase

**Neptune** : sampler CLAP (JUCE + clap-juce-extensions), open source (AGPLv3), basse latence avant tout, dans la **Flibuste Suite** (les autres plugins, série « NOVA », sont en DPF + NanoVG). Créateur : *le Tournedos Rossini*. Dépôt : https://github.com/jelhoste/flibuste-neptune (public).

## Où on en est : palier 0 (fondations)

| Point | État | Détail |
|---|---|---|
| 1. Couche Paramètre générique | **fait**, testé | `core/include/neptune/core/Parameter.h` : `ParamSpec`, `ParameterSet` (sans verrou, version par paramètre, texte affichage/saisie). |
| 2. Undo/redo par commandes | **fait**, testé | `core/include/neptune/core/Undo.h` : gestes fusionnés, transactions, profondeur bornée. **Pas encore branché** à l'interface (qui n'a pas de contrôles). |
| 3. Format JSON versionné | **fait**, testé, CI verte, sauvegarde/rechargement de projet validés sous Windows et Linux (macOS non testé) | `core/.../State.h` (`save`/`load`, schéma v1, tout-ou-rien) et `StateMigration.h` (chaîne de migrations, vide en v1). nlohmann/json v3.12.0. Reste pour plus tard : chemins de samples, ZIP, sous-arbre partiel (spec §20), quand il y aura des samples. |
| 4. Vérification précoce CLAP/clap-juce-extensions | **partiellement fait** | Validé sous Windows et Linux : 676 paramètres exposés, valeurs affichées, automation, sauvegarde de projet, paramètre discret 1..32. **Reste à vérifier** : note-expression / modulation polyphonique, sorties multiples (jusqu'à 64 stéréo), latence modifiable à l'exécution, tempo/transport hôte, `clap-validator`. |
| 5. Réglages globaux, autosave, récupération après crash | **à faire** | |
| Autres éléments du palier 0 (roadmap) | **à faire** | squelettes i18n et thèmes, profils de performance Live/Studio, templates de sorties, composant générique potard/modale, test de performance du callback audio. |

Le moteur ne produit que du silence. Le plugin charge, expose 676 paramètres « sonde » (`core/.../ProbePool.h`, 32 pistes × 21 + 4) et affiche leurs valeurs en direct.

## Architecture du dépôt

```
core/            moteur C++ portable, SANS JUCE (Engine, Parameter, ProbePool, State, Undo)
adapter-juce/    fine couche JUCE/CLAP : NeptuneParameter, PluginProcessor, PluginEditor
tests/           Catch2 : tests du cœur (37 au 2026-10-11)
cmake/           Dependencies.cmake : toutes les dépendances, versions figées
design/          maquette HTML (neptune_ui.html) + briques de style NOVA copiées telles quelles
docs/            brief, roadmap, spec, ce fichier
.github/         workflow CI (Linux, Windows, macOS) + job de Release sur tag v*
```

Règle d'architecture : le cœur n'inclut jamais JUCE. L'adaptateur ne contient aucune logique métier.

Options CMake : `NEPTUNE_BUILD_PLUGIN` (ON), `NEPTUNE_BUILD_TESTS` (ON), `NEPTUNE_PROBE_TRACKS` (32), `NEPTUNE_WITH_AUDIO_IO` (OFF : libsndfile 1.2.2 + libebur128 v1.2.6, prêts mais inutilisés).

## Build, tests, CI

- **Environnement de développement** : Linux Mint. `cmake` n'est **pas installé** et les bibliothèques de développement de JUCE (apt) non plus : le plugin complet ne se compile donc **que par la CI** ; les tests du cœur se compilent localement avec un `cmake` en environnement virtuel :

  ```bash
  python3 -m venv /chemin/venv && /chemin/venv/bin/pip install cmake ninja
  export PATH=/chemin/venv/bin:$PATH
  cmake -S . -B build -G Ninja -DNEPTUNE_BUILD_PLUGIN=OFF
  cmake --build build && ctest --test-dir build --output-on-failure
  ```

- **Pour compiler le plugin en local** : installer les paquets listés dans `.github/workflows/build.yml` (étape « Dépendances système »), puis `cmake --build build --target Neptune_CLAP`.
- **CI** : à chaque push, build + tests du cœur + `.clap` archivé (`Neptune-Windows.zip`, `Neptune-Linux.tar.gz`, `Neptune-macOS.tar.gz`). Durée : 4 à 6 minutes. Le job de **Release** sur tag `v*` n'a **jamais été exécuté**.
- **Récupérer un binaire** : `gh run download <id> -R jelhoste/flibuste-neptune -n Neptune-Linux`. Chrome bloque parfois les archives contenant un `.clap` non signé : envoyer le `.clap` brut, ou le copier dans `~/.clap/` (Reaper sous Linux le lit déjà) puis *Re-scan*.
- **Tests utilisateur** : Windows et Linux validés sur chaque jalon ; **macOS jamais testé** (il compile en CI).

## Pièges déjà rencontrés (à ne pas refaire)

1. **clap-juce-extensions n'a pas de vraies versions** : le tag `0.26.0` pointe vers un commit de 2022. On fige un commit de `main` (`7adee3a`, CI amont verte avec JUCE 8.0.11). JUCE est figé à `8.0.11` ; JUCE 9.0.x existe, la CI amont le teste aussi, non adopté par prudence.
2. **Noms de tests Catch2 en ASCII uniquement** : `ctest` les passe en ligne de commande et Windows casse les accents.
3. **Identifiants de paramètres** : le `clap_id` est le hachage du texte de l'ID de paramètre JUCE (wrapper). On utilise `std::to_string(ParamId)`. Les ids du cœur sont **stables** : ne jamais les renuméroter (automation des projets). Schéma de la sonde : maître 1–4 ; piste *t* à `1000 + 100·t` (+0 volume, +1 pan, +2 mute, +3 solo, +4 type, +10…17 bypass d'inserts, +20…27 niveaux d'envois).
4. **Paramètres discrets** : seul `isBoolean()` est marqué « stepped » côté CLAP ; les sélecteurs à N valeurs sont vus comme un paramètre normalisé 0–1 avec texte. Cela fonctionne dans Reaper (validé).
5. **Cœur lié dans le `.clap`** : `neptune_core` doit rester en `POSITION_INDEPENDENT_CODE ON`, sinon l'édition de liens Linux échoue (révélé par nlohmann/json).
6. **Aperçu de la maquette** : le volet d'aperçu de l'application charge un fichier HTML en `data:` sans ses scripts voisins ; servir `design/` en HTTP (`python3 -m http.server --directory design`).

## Décisions actées (en plus du brief)

- Licence AGPLv3, dépôt public, développement sous Linux + CI GitHub, test final manuel sous Windows. Tests de latence : manuels, dans l'hôte, jamais en CI.
- Accessibilité (lecteurs d'écran) : **hors périmètre V1**. Le contraste NOVA 0–4 est du confort visuel.
- 4 thèmes : sombre, clair, haut contraste, bois (Hammond). Identité visuelle alignée sur les plugins NOVA ; les maquettes NOVA (`~/Bureau/nova tst/nova-ui-series`) servent d'inspiration, pas de code.
- Vues principales (onglets d'en-tête) : **Mixer**, **Mapping** (plein écran, liste de Zones rétractable, vue matrice), **Sample** (éditeur plein écran), **Façade** (plus tard). Vu-mètres dans la barre du bas.
- État du plugin : JSON UTF-8 versionné (`"format":"neptune-state"`, `schema_version`, `parameters` id → valeur réelle). L'ancien binaire `'NPTS'` n'est plus lu (il n'a servi qu'aux jalons de test).

## Format d'état JSON (point 3) : décisions et suite

- Chargement **tout ou rien** : document invalide ou plus récent → refus, aucun paramètre modifié. Plus ancien → migration silencieuse (`registeredMigrations()` : un saut n → n+1 par version ; un test garde-fou échoue si on monte `currentSchemaVersion` sans l'enregistrer).
- Toutes les valeurs sont sauvegardées (pas seulement celles ≠ défaut) : changer un défaut plus tard ne modifie pas un preset existant. Un paramètre absent du fichier revient à son défaut au chargement.
- À faire quand les samples existeront (palier 1/6) : chemins de samples (relatif au preset, absolu, recherche par nom, relocalisation — spec §20), export ZIP portable, presse-papier = sous-arbre JSON, extensions `.neptunepreset` / `.neptunecache`.

## Questions encore ouvertes

- Identifiant CLAP définitif (`com.tournedosrossini.neptune` est provisoire : quel domaine ?).
- Logo (trident provisoire dans la maquette).
- Signature/notarisation macOS pour une distribution propre.
- Test du job de Release (premier tag à poser).
- Conditions exactes de licence JUCE 8/9 pour une éventuelle édition fermée (non nécessaire tant que le projet reste ouvert).
