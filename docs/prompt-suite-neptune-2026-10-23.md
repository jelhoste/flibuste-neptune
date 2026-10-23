# Prompt de reprise : suite de Neptune après l'écoute libre, le raccord de boucle, la fenêtre d'analyse et l'Opus

À coller au début d'une nouvelle discussion, dans le dossier `/home/jean/dev/sampler-sans-reproche`.

---

Je développe Neptune (suite de plugins CLAP, C++/JUCE, AGPLv3, auteur « le Tournedos Rossini », dépôt `/home/jean/dev/sampler-sans-reproche`). Je parle français ; réponds en français.

**Commence par lire** : `docs/passage-de-relais.md` (sections finales : « Modulateurs et round-robin par couche », « Écoute libre d'un fichier de la réserve et écoute du raccord », « Fenêtre d'analyse réglable », « Fichiers Opus »), `docs/spec-modules.md` (§3 Sampler, §13.5), et la mémoire du projet (`MEMORY.md`) si elle est chargée. Les anciens prompts (`docs/prompt-suite-neptune-2026-10-21.md`) contiennent les règles de travail détaillées et les outils de test locaux : relis-les.

## Règles de travail (inchangées)
- Décris-moi ce que je verrai dans Reaper plutôt que le code. **Ne devine jamais** : vérifie dans le code ou les docs, ou dis « non vérifié ». Mes messages sont parfois dictés : si un passage est ambigu, dis comment tu l'as compris.
- Avant un gros morceau, pose-moi des questions à choix multiples, puis procède.
- Tu peux commit et pousser sans me demander. Après un push : attends la CI (Linux, Windows, macOS ; compte 35 à 40 minutes), puis installe les 11 binaires Linux (Neptune, Moussaillon, AnalogSynth, FM6Op, Orgamon, LaFleche, Compresseur, Egaliseur, Delai, Choeur, Microdecale) dans `~/.clap/` : télécharge tous les artefacts `<nom>-Linux` (archives `.tar.gz`) avant de déplacer quoi que ce soit, range les anciens dans `~/neptune-anciens/<horodatage>/` **sans toucher à `LowLatConv.clap`** (il reste dans `~/.clap/`). Si la CI échoue : lis le log, corrige, repousse sans demander. Un push ne déclenche parfois aucune exécution : `gh workflow run build.yml --ref main`. Suivi de la CI : `gh run watch <id>` en tâche de fond, jamais de `sleep` chaîné.
- Textes : tout texte d'interface passe par `core/src/I18n.cpp` (anglais et français, pas de clé en double : `grep -o '^    { "[^"]*"' core/src/I18n.cpp | sort | uniq -d`). Tout nouveau `.cpp` doit être ajouté à **toutes** les cibles qui l'utilisent (`core/CMakeLists.txt`, `adapter-juce/CMakeLists.txt`). Vérifie qu'aucune méthode n'est déclarée sans être définie (déjà arrivé).
- L'interface (JUCE) n'est compilée que par la CI : relis ton code avec soin. Le cœur se teste en local : `cmake` existe via un venv (`pip install cmake`), `cmake -S . -B <tmp> -DNEPTUNE_BUILD_PLUGIN=OFF -DNEPTUNE_BUILD_TESTS=ON` puis `neptune_core_tests` (614 cas, tous verts au dernier essai).
- Mes décisions durables : voir la mémoire (pas de phase linéaire, pas de A/B/C/D, accessibilité abandonnée, etc.).

## État actuel (2026-10-23, tout installé dans `~/.clap/`)
Le sampler a : réserve de fichiers (pool) avec glisser-déposer vers les zones, zones avec suivi de vélocité / clavier, conditions MIDI, humanize, note détectée (YIN), héritage Instrument -> Groupe -> zone, modulateurs, round-robin avec emprunt aux couches voisines, marqueur de volume et boucles multiples enregistrés dans le fichier (ou un fichier voisin `.neptune.json`), empreintes de volume, enregistrement, time-stretch / pitch-shift, compensation de niveau au remplacement d'un fichier.
Derniers ajouts (à valider dans Reaper avec moi, je n'ai pas encore tout essayé) :
1. Écoute libre d'un fichier de la réserve (double-clic, ou JOUER quand la réserve est affichée).
2. Source « Le raccord de la boucle » dans la rangée JOUER de la loupe.
3. Curseur « Fenêtre d'analyse » (10 à 2000 ms) dans NIVEAUX, utilisé par la compensation au remplacement.
4. Fichiers Opus (`.opus`, `.ogg` Opus) : libopus v1.6.1, décodage en mémoire à 48 kHz, option CMake `NEPTUNE_WITH_OPUS`.

## À faire en premier : améliorer la vue FILES (réserve) et les grands conteneurs de la page ZONES
Mes idées, à discuter avec moi avant de coder (pose des questions à choix multiples) :
- **Petit bouton de lecture à gauche du nom de chaque fichier** de la vue FILES (écoute libre, un clic pour jouer, un clic pour arrêter ; réutilise `previewFile` de `NovaSamplerEditor`, voir `PoolTable.h` pour les colonnes).
- **Pouvoir déplier pour voir la pleine longueur des infos** d'un fichier (nom complet, chemin, durée, canaux, fréquence, note détectée, contenu, usage).
- **Agrandir le conteneur des zones / fichiers** : trois pistes, à me recommander honnêtement : (a) un séparateur redimensionnable qui comprime le bloc « Notes et vélocité » (`MappingMatrix`) ou le replie complètement, avec retour à l'état actuel ; (b) un affichage **plein écran** comme celui des effets (regarde comment il est fait pour les effets avant de proposer quoi que ce soit) ; (c) les deux. Même question pour le conteneur « Notes et vélocité ». Dis-moi ce qui est faisable dans cette interface.

## Limites connues (à mentionner si pertinent)
- Streaming disque + lecture depuis le curseur au-delà de la partie préchargée : départ possiblement faux.
- Fichiers dérivés (time-stretch, etc.) : reçoivent le marqueur de volume mais pas les boucles.
- Opus : toujours chargé en entier en mémoire (4 octets par échantillon), mono et stéréo seulement, pas de surround ; positions de boucle exprimées sur le signal décodé à 48 kHz.
- La fenêtre d'analyse est un réglage de l'instrument (version réduite) : la bande déplaçable sur la forme d'onde n'existe pas.
- Écoute libre refusée au-delà de 400 Mo.

**Remarque** : ne commence aucun code avant d'avoir lu ce qui précède et posé tes questions de conception sur la vue FILES.
---
