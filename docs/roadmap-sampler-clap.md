# Roadmap — Neptune (sampler CLAP, Flibuste Suite)

Projet solo, développement en C++/JUCE (+ clap-juce-extensions), mené sous **Linux Mint** avec **builds multiplateformes par GitHub Actions** (Windows, macOS, Linux) et **test final manuel sous Windows**. Open source, gratuit (JUCE sous AGPLv3).
Priorité absolue transverse à tous les paliers : **basse latence**.

---

## Palier 0 — Fondations d'architecture (avant toute fonctionnalité visible)

Ces briques doivent exister *avant* de coder la moindre fonctionnalité "métier", car tout le reste s'appuie dessus et elles sont très coûteuses à greffer après coup.

- Couche **Paramètre générique** unique (valeur, plage, courbe, unité) servant de base à : automation host CLAP, MIDI, OSC, undo/redo, modulation.
- **Pool fixe et généreux de slots** pistes/inserts, déclaré à l'instanciation, UI dynamique par-dessus (masque les slots inutilisés).
- **Templates de sorties audio** (stéréo simple / multi-sorties N pistes), choisis à l'insertion du plugin dans Reaper.
- Système **undo/redo** basé sur pattern commande (couvre changements de valeur ET actions structurelles : création/suppression de piste, routage, assignation de sample).
- Squelette **i18n** : anglais par défaut, français via sélecteur (menu général), même avec peu de chaînes au début.
- Squelette des **4 thèmes** (sombre / clair / haut contraste / bois), identité visuelle alignée sur les plugins NOVA de la Flibuste Suite ; maquette : `design/neptune_ui.html`. Réglage de contraste 0–4 (confort, pas accessibilité).
- **Menu de réglages généraux** : langue, thème, profil de performance, chemins par défaut modifiables (samples/presets/IR/export).
- **Composant générique souris/clavier/tactile** dès la conception : taille minimale des zones tactiles, appui long = clic droit, double-tap = double-clic. (L'accessibilité lecteur d'écran est hors périmètre V1.)
- Format de sauvegarde **JSON versionné** (schéma numéroté dès la v1, pour rétrocompatibilité future).
- Profils de performance (**Live/basse latence** vs **Studio/qualité max**) — mécanisme de bascule, même vide de contenu au début.
- **Autosave périodique + récupération après crash.**
- **Tests unitaires** ciblés sur la logique non-visuelle (Paramètre générique, undo/redo, parsing MIDI/sysex, DSP isolable) — pas sur la mise en page UI.
- **Test de performance automatisé** : temps d'exécution du callback audio mesuré sur un cas de charge représentatif, comparé avant/après chaque refactorisation importante — complète la relecture de code par une mesure réelle.
- **Vérification précoce des fonctions CLAP via `clap-juce-extensions`** (non officiel, certaines fonctions JUCE non traduites) : note-expression/modulation polyphonique, sorties multiples (jusqu'à 64 stéréo), déclaration de latence modifiable à l'exécution (modes de l'IR loader), tempo/transport de l'hôte. À valider dès le palier 0 avec un outil de validation CLAP (`clap-validator`) et dans Reaper, plutôt que de découvrir une limite tard dans le projet.
- **Séparation stricte cœur moteur (C++ portable, sans dépendance JUCE liée à l'OS) / adaptateur plateforme (CLAP/JUCE/Reaper)**, dès les premières briques de code — appel du cœur via une interface simple (`process()`, `noteOn()`, `setParameter()`...). Sans coût de performance si bien fait, facilite les tests unitaires du cœur (exécutable sans driver audio/fenêtre graphique), et préserve la possibilité d'un futur portage plateforme (ex. WebAssembly) sans toucher au cœur du moteur.
- **Réglages globaux** stockés dans le dossier de configuration standard du système, pas dans le projet Reaper.

- **Chaîne de build et CI** : ossature CMake + `cmake/Dependencies.cmake` (versions figées) + workflow GitHub Actions Windows/macOS/Linux (tests du cœur, build du `.clap`, artefacts). **Vérifier très tôt, avec le plugin vide**, qu'un `.clap` produit par la CI se charge dans Reaper sous Windows : valider la chaîne avant d'accumuler du code.

**Livrable** : plugin CLAP qui se charge dans Reaper, une piste, un fader, rien d'autre — mais toute la plomberie interne est en place.

---

## Palier 1 — Noyau sampleur

- Lecture de sample : one-shot, repeat, boucle.
- Enveloppe **AHDSHR généralisée** : masquage des segments inutiles, réglage temps (ms) + forme de courbe, insérable comme insert sur piste. **Types de courbe** : linéaire, concave/convexe (paramétrique), et **linéaire en dB** (décroissance à taux constant sur l'échelle dB plutôt qu'en amplitude — utile en soi, et permet une conversion mathématiquement exacte des enveloppes de decay/relâchement SF2 à l'import, voir `spec-modules.md` section 24).
- Filtre par voie.
- Mixeur basique : pistes avec fader (bascule automatique fader vertical → rotatif selon hauteur dispo), interface responsive/redimensionnable.
- Polyphonie : nb de voix max, mode de vol de voix (oldest/quietest/priority), retrigger vs legato, group choke.
- **Limite globale de voix à l'échelle du plugin** (toutes pistes confondues), en complément des limites par instrument/piste — garde-fou de sécurité CPU/latence.
- Table de samples : note de référence, étendue de notes, couches de vélocité.
- Formats de samples via bibliothèque mature (ex. libsndfile) : WAV/AIFF/FLAC a minima.
- RAM vs streaming disque : les deux modes (tout-RAM / hybride préchargement+stream), réglable par preset, préséance utilisateur.
- **Module Synthé soustractif** (2e type de génération de piste) : 3 oscillateurs + sous-oscillateur + bruit, mixeur des sources, 2 filtres (série/parallèle configurable), unisson générique avec courbe de répartition, 2 enveloppes de filtre indépendantes + enveloppe de volume + enveloppe de pitch dédiée (même moteur AHDSHR généralisé). Voir `spec-modules.md` section 2. **Premier jet livré le 2026-10-13** (1 oscillateur, enveloppe de volume, 1 filtre avec son enveloppe, polyphonie ; voir `passage-de-relais.md`) : le reste de la liste reste à faire.

**Livrable** : sampleur jouable, mono-piste à multi-piste simple, sans effets ni modulation avancée.

---

## Palier 2 — Édition & conception modulaire

- Page d'édition de sample : points de lecture (début/fin), boucles multiples.
- Éditeur graphique de mapping (note/vélocité).
- Conception modulaire : ajout illimité de blocs de samples, préréglages pour clavier piano / batterie / etc.
- **Éditeur de sample intégré non-destructif** : trim/rognage, gain/normalisation, fondu d'entrée/sortie, suppression DC offset, time-stretch et pitch-shift indépendants (même moteur, sans contrainte de latence car hors temps réel). Voir `spec-modules.md` section "Éditeur de sample intégré".
- **Enregistrement audio direct** dans le Sampler (capture du flux Entrée audio, création automatique de Zone) et **purge/prévisualisation sans chargement** (optimisation mémoire, cache de forme d'onde). Voir `spec-modules.md` section 21.
- Métrage (peak/RMS/LUFS), sélectionnable par piste / bus / global. Calcul en parallèle du chemin de signal (pas de latence ajoutée).
- **Module FM (4e type de génération de piste)** : architecture 6 opérateurs / 32 algorithmes inspirée du DX7 (nommage générique, pas de marque déposée), compatibilité sysex complète visée (dump patch/voix + bloc function parameters). Moteur basé sur msfa (Apache 2.0, composant de Dexed) comme référence technique — pas le code GPL v3 de Dexed lui-même, pour préserver la liberté de décision future sur la licence du projet. Interface repensée (diagramme d'algorithme interactif, enveloppes visuelles, macros modernes). Voir `spec-modules.md` section 17 pour le détail complet.
- **La Façade** (interface personnalisée par instrument) : onglet supplémentaire par instrument, image de fond + contrôles liés aux paramètres existants (potard/slider/bouton/bouton d'action/menu déroulant/éditeur de courbe/clavier virtuel superposable), positionnement libre, outils d'alignement/matrice, ajout depuis n'importe où par clic droit/appui long. Couvre le cas d'usage orgue liturgique (page d'accueil façon GrandOrgue). Voir `spec-modules.md` section 15.

---

## Palier 3 — MIDI & OSC transverses

- MIDI 1.0 complet en entrée/sortie (notes, CC, NRPN) sur tous les paramètres, avec feedback.
- OSC bidirectionnel, espace d'adressage dynamique cohérent avec le pool de slots.
- Gestion propre de la boucle de feedback (pas de jitter, pas de bouclage infini).
- Extension **note-expression native CLAP** (équivalent interne "MIDI 2.0-like" indépendant du matériel).
- Support **MPE** en entrée physique.
- MIDI 2.0 natif (UMP) : dernier arrivé, priorité basse (adoption matérielle encore limitée).
- **Moniteur MIDI/OSC** : journal des messages entrants/sortants filtrable, bouton "Panic" (all notes off + reset switches/modulateurs) — voir `spec-modules.md` section 16.

---

## Palier 4 — Effets stock & routage avancé

- Plugins stock : compresseur/limiteur/expandeur/gate, égaliseur multi-bandes, chorus, utilitaire, **distorsion/saturation**, **delay multi-tap**, **MicroPitch** — tous en inserts sur pistes ou sur enveloppes.
- Bus internes du mixeur (sub-mix, regroupement de pistes) — flexibles, ajout/suppression libre car purement internes.
- Sends d'effets : piste de send dédiée par piste émettrice, point d'insertion configurable dans la chaîne, volume/pan sur l'envoi, sommation avant fader ou dans la chaîne.
- Détail complet de chaque plugin stock : voir `spec-modules.md` sections 7 à 13, 18 et 19.

---

## Palier 5 — Modulation avancée

- Matrice de modulation MIDI → paramètres.
- Modulation inter-pistes (par paramètre ou par signal audio d'une autre piste).
- Système de **logique booléenne** (AND/OR/XOR/NAND/NOR/NOT) comme source de modulation discrète — réutilisable ensuite pour le mode orgue (palier 7).
- Liaison des enveloppes/paramètres d'effets au Note On/Off (gate par volume ou par temps écoulé depuis Note On/Off).

---

## Palier 6 — Presets & organisation

- Système de presets importable/exportable, explorateur, mots-clés.
- Format preset portable : conteneur **ZIP** (`preset.json` + `samples/` en chemin relatif).

---

## Palier 7 — Mode orgue liturgique (par-dessus l'existant, pas un module à part)

- Mute de piste exposé comme paramètre MIDI/OSC standard = tirant de jeu (approche initiale). **Alternative désormais disponible** : un jeu peut aussi être représenté comme un Groupe (module Sampler) togglé par un modulateur, au sein d'une seule piste — économise le budget de slots. Les deux approches coexistent, voir `spec-modules.md`.
- Presets filtrés (état on/off d'un groupe de pistes) = combinaisons (Set/Général/Divisional), y compris "general cancel".
- **Routeur de notes centralisé** : couche entre entrée MIDI et pistes, qui duplique/redistribue le NoteOn selon l'état des tirants et accouplements actifs (condition exprimée via la logique booléenne du palier 5).
- Bus de groupe de pistes = regroupement par manuel/clavier.
- Tremulant = LFO pitch/volume via le système de modulation déjà existant (palier 5), rien de spécifique à coder.

---

## Palier 8 — IR loader (convolution)

- Plugin stock dédié, isolé du reste (complexité spécifique : latence structurelle liée à la taille de partition FFT).
- Affichage de l'IR avec référence temporelle.
- Modulation de longueur d'IR, délai avant lecture, stéréo, volume.
- EQ avant traitement et EQ après traitement.
- Mode basse latence dédié (partition FFT plus petite, moins de qualité mais moins de latence) piloté par le profil de performance global (palier 0).

---

## Palier 9 — Hébergement de plugins tiers (host-in-plugin) : ABANDONNÉ (2026-10-23)

Décision de l'utilisateur : on intègre les effets dans Neptune (moteur dans le cœur, interface NOVA, un seul plugin à maintenir) plutôt que d'héberger des plugins tiers. C'est ce qui a été fait pour la convolution (LowLatConv, palier 8) et pour les effets stock. Le texte d'origine de ce palier (CLAP seul, isolation, latence) est retiré ; il reste dans l'historique git.

---

## Palier 10 — Moteur de règles scriptables (interactions entre samples, module Sampler uniquement)

- Périmètre volontairement restreint : interactions **entre samples/voix au sein du module Sampler**, pas une fonctionnalité transverse à tout le plugin (pas pour les modules Synthé soustractif ou FM).
- Objectif : permettre à un utilisateur non-codeur de définir des règles "si condition, alors action" via menus déroulants (pas de vrai langage de script, pas de graphe visuel type node-based — pour rester robuste et digeste).
- S'appuie sur des briques déjà prévues plus tôt dans la roadmap plutôt que d'en inventer de nouvelles : la couche Paramètre générique (palier 0), le système de logique booléenne (palier 5), et le filtre MIDI par piste (canal/type de message/plage, palier 3).
- Points de vigilance à traiter à ce palier : détection de cycle à l'exécution (une règle ne doit pas pouvoir se redéclencher en boucle infinie), et limitation/alerte de charge CPU si une règle est évaluée trop fréquemment.
- Positionné en toute fin de roadmap : un des morceaux les plus délicats du projet avec l'hébergement de plugins tiers (palier 9) et la convolution (palier 8), mais pour des raisons différentes (robustesse logique plutôt que coût DSP).

---

## Backends audio Windows

(Mesures de latence : manuelles, sur le plugin compilé dans l'hôte, pas en CI.)

Dès le palier 1 (lecture de sample), tester WASAPI **et** ASIO séparément — ne pas supposer qu'un réglage bas-latence validé sur l'un se comporte pareil sur l'autre, malgré l'abstraction JUCE. ASIO reste la référence pour l'usage studio/live à basse latence sur Windows ; WASAPI (mode exclusif) est l'alternative native si l'utilisateur n'a pas de driver ASIO tiers.

---

## Notes de suivi

- Chaque palier doit rester livrable de façon autonome (utilisable même si les paliers suivants ne sont pas commencés).
- L'i18n et les thèmes (palier 0) doivent être alimentés au fil de l'eau à chaque palier, pas ajoutés en bloc à la fin.
- Le système de logique booléenne (palier 5), le routeur de notes (palier 7) et le moteur de règles (palier 10) sont les trois briques les plus "nouvelles" par rapport à un sampleur généraliste classique — à concevoir avec le plus de soin.
