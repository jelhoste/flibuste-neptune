# Prompt de reprise : suite de Neptune après les interfaces NOVA du compresseur et de l'égaliseur

À coller au début d'une nouvelle discussion, dans le dossier `/home/jean/dev/sampler-sans-reproche`.

---

Je développe Neptune (suite de plugins CLAP, C++/JUCE, AGPLv3, dépôt `/home/jean/dev/sampler-sans-reproche`). Je parle français ; réponds en français.

**Commence par lire** : `docs/passage-de-relais.md` (surtout les sections finales : « Interfaces NOVA » étapes 1 à 3, « Historique d'amplitude du compresseur », « Réglages du compresseur et spectre de l'égaliseur », « Pastilles à glisser... », « Page SETTINGS, presets utilisateur... »), `third_party/zl/README-NEPTUNE.md`, `design/nova/docs/NOVA_Decisions_transversales.md`, et la mémoire du projet (`MEMORY.md`) si elle est chargée.

## Règles de travail (inchangées)
- Décris-moi ce que je verrai ou testerai dans Reaper plutôt que le code. Ne devine jamais : vérifie dans le code ou les docs, ou dis que c'est non vérifié. Signale tout conflit avec la spec.
- Avant un gros morceau, pose-moi des questions à choix multiples, puis procède. Pour reprendre un comportement de ZL Audio (AGPLv3), lis d'abord son code (dépôts `ZL-Audio/ZLCompressor` et `ZL-Audio/ZLEqualizer`, à recloner en superficiel dans le dossier de travail temporaire) et dis-moi ce qu'il fait avant de coder.
- Tu peux commit et pousser sans me demander. Après un push : attends la CI (Linux, Windows, macOS ; macOS est lent, compte 35 à 60 minutes en tout), puis installe les binaires Linux (Neptune, Moussaillon, AnalogSynth, FM6Op, Orgamon, LaFleche, Compresseur, Egaliseur) dans `~/.clap/`, en rangeant les anciens dans `~/neptune-anciens/<horodatage>/`. Si le téléchargement d'un artefact échoue (HTTP 503 « Egress is over the account limit »), réessaie plus tard sans toucher à `~/.clap` : ne déplace les anciens binaires qu'une fois tous les nouveaux téléchargés. Si la CI échoue : lis le log, corrige, repousse sans demander.
- Les maquettes HTML restent la référence visuelle (`design/nova/ui/*.html`), ZL la référence pour les fonctions. Je n'aime pas les phases linéaires : on n'en fait pas. Pas de A/B/C/D : le A/B actuel suffit. Je ne veux ni jeux L/R/M/S par canal ni collision / gel du spectre.

## État actuel (2026-10-20)
- **Kit NOVA** (`adapter-juce/src/NovaKit.*`, `NovaEditor.*`) : `Skin` (palette + facteur d'échelle, aucune transformation : toute dimension passe par `Skin::px`), `Panel`, `Pill` (bouton, bascule, choix, réglage au glisser avec `bindValue`), `Switch`, `NavItem`, `KnobCard` (course d'affichage réglable), `SubSlider`, `VMeter`, `InfoButton` (bulle d'aide), `Text`, fenêtres de réglages, `LookAndFeel` ; `EditorBase` : en-tête (preset, bypass, thème et contraste à gauche du bypass), pages, barre du bas, A/B, modale des potards, page SETTINGS, presets utilisateur (JSON dans `<configuration de la suite>/Presets/<module>/`).
- **Compresseur** (`NovaCompressorEditor`, `NovaHistoryView`) : pages Compressor, Sidechain, Advanced, Settings ; historique d'amplitude d'après ZL (anneau d'analyse du DSP, 18 s par défaut), courbe de transfert orange à droite, égaliseur de clé réglable à la souris et éteint par défaut, gains d'entrée et de sortie sous les vu-mètres, course des potards (attaque 0..200 ms, relâchement 0..800 ms, gain ±12 dB, valeurs libres par la modale).
- **Égaliseur** (`NovaEqualizerEditor`, `NovaSpectrum`) : courbe interactive, bandes (passe-haut à 20 Hz et trois cloches par défaut), page Dynamics, spectre d'après ZL (Normal ou High, éteint par défaut), page Settings.
- Cœur : `AnalyzerRing`, `SpectrumAnalyzer`, `CompressorPresets`, `EqualizerPresets`, hidden params d'affichage enregistrés avec l'état.
- La CI compile aussi `NovaGallery` (rendu hors-ligne des éditeurs en PNG, artefact `NovaGallery`) : c'est l'outil de comparaison avec les maquettes.

## Outils pratiques pour aller vite (à recréer, ils ne sont pas dans le dépôt)
- Vérification de syntaxe JUCE locale en 7 s par fichier : cloner JUCE 8.0.11 et `clap-juce-extensions` (avec ses sous-modules `clap-libs/clap` et `clap-helpers`) dans le dossier de travail temporaire, puis `g++ -std=c++17 -fsyntax-only -Wall -Wextra` avec `-DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_MODULE_AVAILABLE_juce_gui_basics=1` (et les autres modules audio) et les chemins d'inclusion de JUCE, du cœur (`core/include`), de `adapter-juce/src` et de nlohmann_json (`build/_deps/nlohmann_json-src/include`). Il n'y a ni cmake ni JUCE dans le build local : tout le reste passe par la CI.
- Tests du cœur en local : compiler les sources de Catch2 (`build/_deps/catch2-src/src`, avec `generated-includes`) en objets une fois, puis `g++` du fichier de test avec les sources du cœur nécessaires.

## Ce que je veux faire ensuite (dans cet ordre, à discuter)
1. **Source MIDI du sidechain du compresseur** (je l'ai trouvée intéressante). Plan envisagé : le plugin séparé accepte une entrée MIDI (`NEEDS_MIDI_INPUT`, note port CLAP) ; dans `processBlock`, un signal de clé synthétique est construit échantillon par échantillon (niveau de la clé fonction de la vélocité, tenu pendant la note, nul sinon) et passé par `runner_.setKey` comme la clé externe, avec un choix de source à trois états (interne, externe, MIDI) ; les filtres de clé sont contournés pour cette source. Le coût CPU est négligeable. Pas disponible dans un insert de Neptune (pas de MIDI vers les inserts). À me demander avant de coder : la correspondance vélocité vers niveau de clé (plage et courbe), le comportement du relâchement, le choix des canaux MIDI, la façon de le montrer dans l'interface.
2. **Autres plugins de la série** avec le kit : chorus, delay, console Hammond (voir `design/nova/ui/*` et `design/nova/docs/*`), et migration de La Flèche (`ShaperEditor`) vers `EditorBase` et la mise en page à la vraie taille.
3. **Barre LRA du compresseur** (mesure EBU R128 complète : filtre K, blocs de 3 s, double porte), seulement si on trouve un moyen simple.
4. Pages SETTINGS : les sections de la maquette qui manquent (AUDIO / DSP, METERING, INTERACTION), les textes d'aide pour les autres paramètres.
5. Tests sur Windows de ma part : tactile (cibles de 44 px, appui long pour le menu contextuel, équivalents de la molette et du clic droit) ; je te dirai quoi corriger.

À valider d'abord dans Reaper si je ne l'ai pas dit : charge CPU du spectre de l'égaliseur en qualité High, fluidité de l'historique du compresseur, et réglage à l'oreille des presets d'usine (compresseur et égaliseur : ce sont des points de départ).

**Remarque** : ne commence aucun code avant d'avoir lu ce qui précède et de m'avoir posé tes questions sur le point 1.
