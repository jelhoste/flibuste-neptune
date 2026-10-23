# Prompt de reprise : suite de Neptune après la convolution (prochaine étape : la Façade)

À coller au début d'une nouvelle discussion, dans le dossier `/home/jean/dev/sampler-sans-reproche`.

---

Je développe Neptune (suite de plugins CLAP, C++/JUCE, AGPLv3, auteur « le Tournedos Rossini », dépôt `/home/jean/dev/sampler-sans-reproche`). Je parle français ; réponds en français.

**Commence par lire** : `docs/passage-de-relais.md` (sections finales : « Convolution », « Écoute libre d'un fichier de la réserve… », « Fichiers Opus »), `docs/roadmap-sampler-clap.md`, `docs/spec-modules.md` (**§15 La Façade**, §3 Sampler), `design/neptune_ui.html` (l'onglet « FAÇADE » grisé de la maquette) et la mémoire du projet (`MEMORY.md`, dont `project-neptune-convolution-2026-10-23.md`). Les anciens prompts (`docs/prompt-suite-neptune-2026-10-23.md`) contiennent d'autres règles et outils de test locaux.

## Règles de travail (inchangées, plus quelques leçons)
- Décris-moi ce que je verrai dans Reaper plutôt que le code. **Ne devine jamais** : vérifie dans le code ou les docs, ou dis « non vérifié ». Mes messages sont parfois dictés : si un passage est ambigu, dis comment tu l'as compris.
- Avant un gros morceau, pose-moi des questions à choix multiples (outil de questions), puis procède.
- Tu peux commit et pousser sans me demander. Après un push : attends la CI (Linux, Windows, macOS ; compte 35 à 40 minutes), **vérifie les trois résultats avant d'installer** (une fois j'ai installé alors que Windows avait échoué : ne le refais pas), puis installe les binaires Linux dans `~/.clap/` : Neptune, Moussaillon, AnalogSynth, FM6Op, Orgamon, LaFleche, Compresseur, Egaliseur, Delai, Choeur, Microdecale, **Convolution** (12 en tout). Télécharge tous les artefacts `<nom>-Linux` (archives `.tar.gz`, `gh run download <id> -n <nom>-Linux`) avant de déplacer quoi que ce soit, range les anciens dans `~/neptune-anciens/<horodatage>/` **sans toucher à `LowLatConv.clap`** (mon ancien plugin DPF : il reste dans `~/.clap/`). Si la CI échoue : lis le log (`gh run view <id> --log-failed`), corrige, repousse sans demander. Un push ne déclenche parfois aucune exécution : `gh workflow run build.yml --ref main`.
- **API GitHub limitée** : ne scrute pas la CI plus d'une fois toutes les 5 minutes (j'ai atteint la limite d'appels). Une seule commande en tâche de fond du genre `until [ "$(gh run view <id> --json status -q .status 2>/dev/null)" = completed ]; do sleep 300; done`, jamais plusieurs boucles ni de `sleep` chaîné au premier plan.
- Textes : tout texte d'interface passe par `core/src/I18n.cpp` (anglais et français, pas de clé en double : `grep -o '^    { "[^"]*"' core/src/I18n.cpp | sort | uniq -d`). Tout nouveau `.cpp` doit être ajouté à **toutes** les cibles qui l'utilisent (`core/CMakeLists.txt`, `adapter-juce/CMakeLists.txt`, `tests/CMakeLists.txt`). Un nouveau plugin se déclare aussi dans `.github/workflows/build.yml` (cible, validation CLAP, archive).
- L'interface (JUCE) n'est compilée que par la CI : relis ton code avec soin (MSVC refuse par exemple qu'une lambda capture implicitement une constante locale : utilise `static constexpr`). Le cœur se teste en local : un venv avec cmake existe (`python3 -m venv <dossier> && <dossier>/bin/pip install cmake ninja`), `cmake -S . -B <tmp> -G Ninja -DNEPTUNE_BUILD_PLUGIN=OFF -DNEPTUNE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release` puis `cmake --build <tmp> --target neptune_core_tests` et `neptune_core_tests` (**618 cas, tous verts** au dernier essai ; la configuration télécharge les dépendances, compte plusieurs minutes). Les tests du cœur tournent avec 8 Mo de pile sous MSVC (un `MixerState` pèse ~144 Ko).
- Mes décisions durables : voir la mémoire (pas de phase linéaire, pas de A/B/C/D, accessibilité abandonnée, **palier 9 (hébergement de plugins tiers) abandonné : on intègre**, pas d'EQ avant la convolution, etc.).

## État actuel (2026-10-23, tout installé dans `~/.clap/` ; j'ai testé tout sous Linux, **Windows reste à tester**)
- Sampler : réserve de fichiers avec lecture libre (bouton triangle par ligne, panneau de détail), liste des zones avec bouton de lecture bascule et panneau de détail, séparateur déplaçable et « Pleine page » (liste ou matrice sur toute la hauteur), boutons plus grands (page ZONES, barre de Neptune), menus au style NOVA, fenêtres MODULATEURS et EMPLACEMENTS superposées.
- Mixeur : tranches au style NOVA (pastille de couleur, puces d'inserts, faders orange, M/S/P en pastilles), zoom réglable mémorisé (120 % par défaut, réglage global `mixer_zoom_percent`), bulle de routage au style NOVA, menus agrandis.
- **Convolution** (nouveau, validé par moi sous Linux avec une vraie IR) : moteur LowLatConv dans le cœur (`core/.../conv/`), module `ConvolutionModule` (insert + plugin CLAP), interface NOVA (IR avec marqueurs B/E, TONE avec égaliseur graphique, LEVELS), formats WAV/AIFF/FLAC/Opus, glisser-déposer, changement de fichier annulable (`ParamHost::uiEditText`). **Reste à tester par moi : la sauvegarde et le rechargement du chemin de l'IR dans un projet.**

## À faire en premier : la Façade (palier 2, spec §15)
Interface personnalisée par instrument : un onglet supplémentaire par instrument, image de fond + contrôles liés aux paramètres existants (potard, curseur, bouton, bouton d'action, menu déroulant, éditeur de courbe, clavier virtuel superposable), positionnement libre, outils d'alignement / matrice, ajout depuis n'importe où par clic droit ou appui long. Cas d'usage : l'orgue liturgique (page d'accueil façon GrandOrgue). **Il n'existe aucun code de Façade aujourd'hui** (vérifié par recherche). Ne code rien avant d'avoir lu §15 et posé tes questions de conception (format de sauvegarde, où vit la Façade : instrument du Sampler ? Orgamon ? tous ? éditeur de mise en page ou fichier ? gestion des images : chemins relatifs, intégration dans le preset ZIP ?).

## Ensuite
1. Faire ensemble **une liste propre de validation** de tout ce qui est marqué « à valider dans Reaper » dans `docs/passage-de-relais.md` (je la testerai, Linux puis Windows).
2. Autres chantiers de la roadmap, dans l'ordre que je choisirai : matrice de modulation (palier 5), orgue liturgique (palier 7 : combinaisons, routeur de notes), presets FM générés et orgue Hammond / setBfree (feu vert nécessaire avant de commencer).
3. Questions ouvertes (à moi de trancher) : identifiant CLAP définitif (domaine), logo, signature macOS, premier tag de Release.

## Limites connues (à mentionner si pertinent)
- Streaming disque : une lecture qui démarre plus loin que la partie préchargée (curseur de la loupe, START très avancé) peut manquer son début (la voix se coupe en fondu de 3 ms et un compteur « streaming : N coupure(s) » s'affiche) ; non mesuré. Les zones en boucle ne sont pas touchées.
- Fichiers dérivés (time-stretch, etc.) : reçoivent le marqueur de volume mais pas les boucles.
- Opus : toujours chargé en entier en mémoire, mono et stéréo seulement ; positions de boucle sur le signal décodé à 48 kHz.
- Fenêtre d'analyse : réglage de l'instrument (version réduite), pas de bande déplaçable sur la forme d'onde.
- Convolution : IR vraie stéréo (4 canaux) en WAV seulement ; AIFF/FLAC/Opus en mono ou stéréo.

**Remarque** : ne commence aucun code de Façade avant d'avoir lu ce qui précède et posé tes questions de conception.
---
