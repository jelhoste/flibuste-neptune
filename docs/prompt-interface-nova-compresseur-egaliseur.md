# Prompt de départ : interface soignée du compresseur et de l'égaliseur (style NOVA)

À coller au début d'une nouvelle discussion, dans le dossier `/home/jean/dev/sampler-sans-reproche`.

---

Je développe Neptune (suite de plugins CLAP, C++/JUCE, AGPLv3, dépôt `/home/jean/dev/sampler-sans-reproche`). Je parle français ; réponds en français.

**Commence par lire** : `docs/passage-de-relais.md` (surtout les sections « Compresseur porté de ZL Compressor » et « Égaliseur porté de ZL Equalizer »), `third_party/zl/README-NEPTUNE.md`, et le fichier de mémoire `MEMORY.md` s'il est chargé.

## Règles de travail (inchangées)
- Décris-moi ce que je verrai ou testerai dans Reaper plutôt que le code. Ne devine jamais : vérifie dans le code ou les docs, ou dis que c'est non vérifié. Signale tout conflit avec la spec.
- Avant un gros morceau, pose-moi des questions à choix multiples, puis procède.
- Tu peux commit et pousser sans me demander. Après un push : attends la CI (Linux, Windows, macOS), puis installe les binaires Linux (Neptune, Moussaillon, AnalogSynth, FM6Op, Orgamon, LaFleche, Compresseur, Egaliseur) dans `~/.clap/`, en rangeant les anciens dans `~/neptune-anciens/<horodatage>/`. Si la CI échoue : lis le log, corrige, repousse sans demander.
- Les maquettes HTML sont la référence visuelle : `design/nova/ui/nova_compressor_full_ui.html` et `nova_equalizer_ui.html` (modules partagés : `nova-themes.css`, `nova-controls.css`, `nova-knob-styles.js`, etc.), avec les descriptions de paramètres `design/nova/docs/NOVA_Compressor_params_v0.1.md`, `NOVA_Equalizer_params_v0.1.md`, `NOVA_Common_modules_v0.1.md`, `NOVA_Decisions_transversales.md`. Décisions déjà prises : ZL est la référence pour les fonctions, NOVA pour le visuel.

## État actuel (commit `695b6a0`, 2026-10-18)
- **Cœur DSP porté de ZL (AGPLv3)**, testé (470 cas) : `core/include/neptune/core/CompressorModule.h` (ids 100..187, 8 filtres de clé), `EqualizerModule.h` (16 bandes, ids 100..104 globaux + 120 + 24 par bande), `DynamicsDsp.h`, `Biquad.h`, `Oversampler.h`. Les modules exposent des vu-mètres (`meter(i)`) et des fonctions d'affichage (`CompressorModule::curveOutputDb`, `EqualizerModule::bandResponseDb` / `totalResponseDb`).
- **Interfaces provisoires** : `adapter-juce/src/GenericModuleEditor.*` (une ligne par paramètre, sections repliables, en-tête dessiné par l'appelant), `CompressorEditor.*` (courbe de transfert + vu-mètres IN / GR / OUT), `EqualizerEditor.*` (réponse en fréquence en direct, points numérotés, vu-mètres). Plugins `CompressorPlugin.*` (Compresseur.clap) et `EqualizerPlugin.*` (Egaliseur.clap), et types d'insert « compressor » et « equalizer » dans `InsertOverlay.cpp`. Les trois utilisent la même interface (`ParamHost`).
- Le visuel de La Flèche (shaper de transitoires, `ShaperEditor.*`) est l'autre interface déjà soignée, mais pas encore au style commun.

## Ce que je veux
Remplacer les interfaces provisoires du compresseur et de l'égaliseur par des interfaces soignées, fidèles aux maquettes NOVA, avec des composants communs réutilisables par les autres plugins (boutons, cartes, pastilles, potards, LookAndFeel, thèmes). Interfaces JUCE, même code pour le plugin séparé et pour l'insert Neptune.

**Pose-moi d'abord tes questions de conception** (à choix multiples, une à la fois si possible), au moins sur : les pages de chaque maquette à reprendre et ce qu'on en laisse pour plus tard (fonctions absentes de notre DSP, par exemple la barre LRA, la source MIDI, les jeux par canal du compresseur, les 4 jeux L/R/M/S de l'égaliseur) ; l'interaction de l'égaliseur (glisser les points, molette pour le Q, clic droit, sélection de bande) ; l'analyseur de spectre (FFT d'abord ou après) ; l'historique de réduction du compresseur ; l'ordre de réalisation (composants communs d'abord, puis compresseur, puis égaliseur, ou l'inverse) ; les presets d'usine ; les thèmes et la redimensionnabilité ; la façon de vérifier la fidélité à la maquette (captures côte à côte).

**Leçons de performance JUCE déjà apprises** (rendu logiciel, voir `ShaperEditor.cpp`) : ne jamais agrandir l'interface par `AffineTransform` (> 1) ; mettre en cache une image opaque et la blitter à des positions entières ; utiliser `VBlankAttachment` ; rythmer les défilements sur l'horloge murale, car l'hôte livre l'audio par rafales ; mesurer le temps de peinture avant de conclure.

**Remarque** : ne commence aucun code avant que j'aie répondu à tes questions.
