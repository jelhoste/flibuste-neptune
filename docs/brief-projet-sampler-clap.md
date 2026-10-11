# Brief projet — Neptune (sampler CLAP, Flibuste Suite)

Document de référence à joindre en début de nouvelle conversation avec Claude pour reprendre le projet sans perte de contexte. À utiliser conjointement avec `roadmap-sampler-clap.md` (détail des paliers) et `spec-modules.md` (spécifications détaillées de chaque module, interface et comportement précis).

## Contexte

Plugin sampler au format **CLAP** (cible principale : Reaper), nommé **Neptune**, premier produit à rejoindre les effets existants dans la **Flibuste Suite**. Créateur : **le Tournedos Rossini**. Identifiant CLAP provisoire : `com.tournedosrossini.neptune` (à confirmer), binaire `Neptune.clap`. Développeur : utilisateur seul (dev solo), assisté par Claude qui écrit tout le code. Projet de grande ampleur (comparable à Kontakt/Battery/TX16Wx), à construire par paliers fonctionnels, chacun livrable de façon autonome.

> Historique : le projet s'appelait « Sampler et sans reproche » dans les premières versions de ces documents ; le dossier de travail en garde le nom.

**Priorité n°1, transverse à tout le projet : la basse latence.**

---

## Décisions d'architecture actées

### Stack technique
- **C++ avec JUCE** (+ `clap-juce-extensions`, MIT, qui fournit la cible CLAP) — le projet est **open source et gratuit**, donc JUCE est utilisé sous **AGPLv3**. Aucune contrainte de licence JUCE supplémentaire à gérer tant que le projet reste ouvert. (Les conditions exactes des offres JUCE « Personal/Indie » ont pu évoluer avec JUCE 8/9 : à revérifier sur le site officiel seulement si le projet devait un jour être fermé.)
- **Framework : JUCE confirmé après comparaison avec DPF** (DPF : licence ISC, support CLAP officiel, plus léger — mais UI minimale, widgets additionnels dans un projet séparé ; les autres plugins de la Flibuste Suite restent en DPF + NanoVG). Raison du choix : surface d'UI de la spec (éditeur de forme d'onde, mapping à milliers de Zones, Façade, modales, glisser-déposer) et exigence tactile. Points de vigilance acceptés : le support CLAP de JUCE passe par `clap-juce-extensions` (non officiel, certaines fonctions JUCE non traduites, pas d'hébergement CLAP — le palier 9 nécessitera du code d'hébergement CLAP direct) ; JUCE sous AGPLv3 (ou licence payante pour fermer le code un jour). La séparation cœur/adaptateur garde le moteur indépendant du framework.
- **Environnement de développement** : machine **Linux Mint** (compilation et essais locaux, JUCE et `clap-juce-extensions` supportant Linux), **builds Windows/macOS/Linux par GitHub Actions** à chaque push, **test final manuel sous Windows** (+ Reaper) avec le binaire produit par la CI. Si le cycle push→CI→test Windows devient trop pénible, repasser en développement direct sous Windows reste possible.
- Backends audio ciblés sous Windows : **WASAPI**, avec **ASIO** à valider spécifiquement pour la basse latence (standard de facto en studio/live)
- macOS en bonus si possible, non prioritaire
- Point de vigilance conservé : l'abstraction JUCE ne garantit pas un comportement de latence strictement identique entre backends audio (WASAPI vs ASIO vs Linux/macOS). **Les mesures de latence se font à la main sur le plugin compilé, dans l'hôte** — pas en CI (les runners n'ont pas de carte son et ce serait sans objet pour un plugin)

### Gestion des paramètres / automation CLAP
- **Approche hybride** : pool de slots pistes/inserts fixe et généreux, déclaré à l'instanciation (stabilité pour l'automation Reaper), avec UI dynamique qui ne montre que les slots réellement utilisés
- Idem pour les **sorties audio** : templates de sorties (stéréo simple / multi-sorties N) choisis à l'insertion du plugin dans Reaper, stables ensuite
- Distinction claire : sorties physiques vers l'hôte (fixes par template) vs bus internes du mixeur (100% flexibles, ajout/suppression libre)

### RAM / Streaming
- Les deux modes disponibles : tout-RAM et hybride pro (préchargement d'un buffer court + fin du sample streamée disque, type Kontakt)
- Réglable par preset/instrument, préséance utilisateur en session

### MIDI / OSC
- Tous les paramètres commandables en MIDI et OSC, avec **feedback bidirectionnel** (pour contrôleurs motorisés/LED)
- Couche "Paramètre" générique unique servant de base à automation CLAP + MIDI + OSC + undo/redo + modulation
- Séquencement MIDI par étapes : MIDI 1.0 classique → extension note-expression native CLAP (déjà "MIDI 2.0-like" en interne) → MPE en entrée physique → MIDI 2.0 natif (UMP) en dernier, priorité basse (adoption matérielle encore limitée en 2026)

### Undo/redo
- Intégré dès le palier 0, basé sur pattern commande (couvre changements de valeur ET actions structurelles : création/suppression piste, routage, assignation sample)

### Métrage
- Peak / RMS / LUFS, sélectionnable par piste / bus / global
- Calcul en parallèle du chemin de signal (analyse pure, pas de latence ajoutée sur l'audio)

### Presets
- Format de sauvegarde : **JSON structuré et versionné** dès la v1 (schéma numéroté pour rétrocompatibilité), samples référencés en chemin relatif
- Format portable (export/partage) : conteneur **ZIP** (`preset.json` + dossier `samples/`)
- Système d'import/export avec explorateur et mots-clés

### Formats de samples
- Le plus de formats possible en gardant la robustesse → s'appuyer sur bibliothèque mature (ex. libsndfile) plutôt que parseurs maison : WAV/AIFF/FLAC a minima

### Effets stock (isolés dans le plugin, pas de "taxe" de latence liée au fait d'être stock)
- **8 plugins stock** : Utilitaire, Égaliseur (32 bandes, 4 actives par défaut), Compresseur/Limiteur/Expandeur/Gate, Chorus (moderne + vintage BBD 2 boutons), Distorsion/Saturation (mono-bande), IR Loader (convolution), Delay multi-tap (16 blocs), MicroPitch (H3000-style)
- Fonctionnalités communes à tous : copier-coller, bypass, dry/wet, trim entrée/sortie, entrée de clé externe (sidechain), interfaces redimensionnables
- IR loader (convolution) : plugin à part, complexité spécifique (latence structurelle liée à la taille de partition FFT) — palier tardif, avec 3 modes de latence (zéro latence réelle par convolution partitionnée non-uniforme, traditionnel déclaré à l'hôte, décorrélation non déclarée pour usage reverbe en envoi/retour)
- Chaque algo à latence inhérente (compresseur look-ahead, EQ dynamique, oversampling, convolution) doit avoir un mode basse latence, piloté par un **profil de performance global** (Live/basse latence vs Studio/qualité max) plutôt que des toggles manuels dispersés, propageable entre toutes les instances chargées dans le processus Reaper en cours
- Détail complet de chaque plugin : voir `spec-modules.md` sections 7 à 13, 18 et 19

### Module FM (4e type de génération de piste)
- Architecture 6 opérateurs / 32 algorithmes inspirée du DX7, nommage générique (pas de marque déposée)
- Compatibilité sysex complète visée (dump patch/voix + bloc function parameters)
- Moteur basé sur msfa (Apache 2.0, composant de Dexed) comme référence technique — pas le code GPL v3 de Dexed lui-même, pour préserver la liberté de décision future sur la licence du projet
- Interface repensée : diagramme d'algorithme interactif, enveloppes visuelles (réutilise le composant de courbe du projet), macros modernes (Brillance, Vitesse d'enveloppe, Feedback/Drive)
- Détail complet : voir `spec-modules.md` section 17

### Module Synthé soustractif (2e type de génération de piste)
- 3 oscillateurs + sous-oscillateur + bruit, mixeur des sources (niveau/mute/pan individuel), 2 filtres série/parallèle configurable, unisson générique (voix/détune/spread/courbe)
- **3 enveloppes dédiées** : 2 de filtre indépendantes + 1 de volume + 1 de pitch (ajout tardif, même moteur AHDSHR)
- Détail complet : voir `spec-modules.md` section 2

### Éditeur de sample intégré + compléments issus de la comparaison au marché
- **Éditeur de sample non-destructif** : trim/rognage, gain/normalisation, fondu d'entrée/sortie, suppression DC offset, **time-stretch et pitch-shift indépendants** (même moteur, sans contrainte de latence car hors temps réel)
- **Time-stretch/pitch-shift en lecture temps réel** dans le Sampler (distinct du varispeed existant) : ajoute une latence de traitement propre, suit le profil de performance global
- **Enregistrement audio direct** dans le Sampler (capture du flux Entrée audio → Zone automatique)
- **Purge et prévisualisation sans chargement** (optimisation mémoire, appuyée sur le cache de forme d'onde)
- **Limite globale de voix à l'échelle du plugin**, en complément des limites par instrument/piste
- Mode granulaire (Falcon) écarté, cohérent avec l'abandon du Wavetable
- Détail complet : voir `spec-modules.md` section 21

### Audit de fin de conception — compléments finaux
- **Import de bibliothèques SFZ existantes** (fichiers `.sfz` de tiers, parsing direct, conversion en Groupes/Zones internes) — gros bénéfice de contenu immédiatement utilisable
- **Import SF2/SF3 (SoundFont) et GrandOrgue (.organ)** : conversion native complète en Groupes/Zones (pas de mode cloisonné). SF2 depuis la spécification publique, SF3 décompressé une fois à l'import (Ogg → PCM natif), GrandOrgue directement compatible avec l'architecture orgue déjà construite (palier 7, Façade)
- **Édition en lot** sur plusieurs Zones sélectionnées, **recherche/filtre** dans l'éditeur de mapping — essentiels à l'échelle des instruments à 10 000+ samples
- **MIDI Learn** (clic droit → apprendre → mouvement contrôleur → liaison automatique), en plus du contrôle MIDI/OSC déjà complet
- **Cache de samples partagé entre instances** du plugin (même portée que le profil de performance partagé) — évite de charger deux fois le même fichier en RAM
- **Indicateur de charge** (voix actives / distance à la limite globale) affiché en direct
- Détail complet : voir `spec-modules.md` section 24

### Schéma de métadonnées embarquées (complet)
- Chunk/bloc personnalisé dans WAV/AIFF/FLAC (pas de nouveau format propriétaire), champs en **anglais** (convention code/technique du projet)
- Bloc universel (mapping, lecture, round-robin, **marqueur de synchronisation** réutilisant le champ de délai de lecture existant, empreinte de volume multi-fenêtres, pitch détecté, provenance) + espaces de noms par famille (`organ`, `hammond`, `orchestral`, `percussion`, `custom` libre)
- Taxonomie `family`/`subfamily` extensible (menu + texte libre, jamais fermée)
- Détail complet : voir `spec-modules.md` section 23

### Stockage et gestion des données
- Séparation stricte projet (copie complète et résolue, jamais affectée par une modification ultérieure de la bibliothèque) vs bibliothèque de presets (point de départ réutilisable)
- Résolution des chemins samples : dossier du preset en priorité (portabilité artisanale) → chemin absolu → recherche par nom dans le dossier samples par défaut → relocalisation manuelle
- Sauvegarde normale : dossier par instrument avec fichier JSON à l'intérieur, samples jamais copiés automatiquement ; export portable ZIP sur action explicite (chemins réécrits, samples dupliqués) pour une distribution robuste
- Structure Groupes → Zones (Zone = terme formel pour "un sample individuel mappé")
- Détail complet : voir `spec-modules.md` section 20

### Moteur de règles scriptables (palier 10)
- Périmètre restreint : interactions entre samples/voix au sein du module Sampler uniquement, pas transverse à tout le plugin
- Règles "si condition, alors action" par menus déroulants (pas de vrai langage de script pour rester digeste), avec **mode Code optionnel en Lua** (sandboxé, garde-fous CPU) pour les utilisateurs avancés qui veulent plus de puissance
- Détection de cycle à l'exécution, évaluation à taux de contrôle (pas audio-rate)
- Détail complet : voir `spec-modules.md` section 14

### La Façade (interface personnalisée par instrument)
- Onglet supplémentaire par instrument, image de fond + contrôles liés aux paramètres existants (potard/slider/bouton/**bouton d'action**/menu déroulant/éditeur de courbe/**clavier virtuel superposable**), positionnement libre
- Outils d'alignement/répartition/matrice, ajout d'un contrôle depuis n'importe où par clic droit/appui long
- Détail complet : voir `spec-modules.md` section 15

### Moniteur MIDI/OSC (outil de diagnostic transverse)
- Journal des messages entrants/sortants filtrable (réutilise le composant de filtre MIDI du projet), bouton "Panic"
- Clavier virtuel de test, potards CC assignables, boîte de dialogue message libre avec sysex
- Détail complet : voir `spec-modules.md` section 16

### Modulation
- Matrice de modulation MIDI → paramètres, y compris modulation inter-pistes (paramètre ou signal audio d'une autre piste)
- Système de **logique booléenne** (AND/OR/XOR/NAND/NOR/NOT) comme source de modulation discrète — réutilisé aussi par le mode orgue
- Liaison enveloppes/effets ↔ Note On/Off (gate par volume ou par temps écoulé)
- Coût CPU du graphe géré par profil de performance (control-rate vs audio-rate), pas par désactivation binaire

### Mode orgue liturgique (type GrandOrgue, sans réimplémenter GrandOrgue)
Pas un module à part — recomposé avec les briques déjà prévues :
- Un jeu = une piste/table de samples (déjà natif)
- Un tirant de jeu = un mute de piste exposé comme paramètre MIDI/OSC standard (déjà natif)
- Combinaisons (Set/Général/Divisional/General Cancel) = presets filtrés sur l'état des mutes (système de presets déjà prévu)
- **Nouveauté réelle à concevoir** : un **routeur de notes centralisé** entre l'entrée MIDI et les pistes, qui duplique/redistribue le NoteOn selon l'état des tirants et accouplements actifs (condition exprimée via la logique booléenne de la modulation)
- Bus de groupe de pistes = regroupement par manuel/clavier
- Tremulant = simple LFO pitch/volume via le système de modulation général, rien de spécifique
- Vélocité : conservée normalement (les orgues y répondent réellement — vitesse de soupape, chiff d'attaque), couverte par les couches de vélocité multi-samples déjà prévues, pas de cas spécial

### Hébergement de plugins tiers (host-in-plugin)
- Repoussé en toute fin de roadmap (palier 9)
- **Scope restreint au format CLAP uniquement** (pas de VST3/LV2) — élimine la couche de traduction entre formats, le principal poste de complexité du host-in-plugin générique
- Arbitrage latence à trancher à ce palier : hébergement in-process (overhead quasi nul) vs processus séparé (plus robuste à l'isolation, latence/gigue incompressible)
- Reste un des plus gros morceaux techniques du projet, mais nettement plus abordable qu'un host multi-format générique

### i18n & thèmes
- **Anglais par défaut** (audience internationale, projet open source), français disponible via sélecteur (menu général) — dissocié du nom du produit, qui reste "Neptune" quelle que soit la langue d'interface
- **4 thèmes**, repris de l'identité visuelle des plugins NOVA existants de la Flibuste Suite : **sombre**, **clair**, **haut contraste** et **bois** (style orgue Hammond, contrôles « bezel »). Potards « led » (anneau) sur les trois premiers, « pot » (potard physique) sur le thème bois. Réglage de **contraste** par niveaux 0–4 (confort visuel, désactivé sur le thème haut contraste). Maquette de référence : `design/neptune_ui.html`.
- À poser dès le palier 0 (squelette), alimenté au fil de l'eau — coûteux à greffer après coup

### Menu de réglages généraux
- Langue, thème, profil de performance (Live/Studio)
- **Chemins par défaut modifiables** : dossier samples, dossier presets, dossier IR, dossier d'export — réutilise le composant de sélection de fichier déjà nécessaire ailleurs

### Souris/clavier ET tactile — exigence transverse du composant générique (palier 0)
- Taille minimale des zones tactiles (cibles suffisamment grandes pour un doigt)
- Équivalent tactile du clic droit : appui long (menus contextuels)
- Équivalent tactile du double-clic : double-tap (ouverture des modales)
- Contexte visé : PC Windows à écran tactile (type Surface ou moniteur tactile) utilisés avec Reaper comme poste de travail classique — pas d'application mobile séparée

### Accessibilité — mise de côté pour l'instant
- **Décision : l'accessibilité (lecteurs d'écran, conformité WCAG) n'est pas un objectif de la V1.** Elle sera ajoutée plus tard si le besoin apparaît. Le réglage de contraste et le thème haut contraste sont des **confort visuel** pour utilisateurs voyants, pas une fonction d'accessibilité.
- La navigation clavier de base reste souhaitable (double-clic → modale avec champ texte), sans effort dédié.
- Aucune validation avec des utilisateurs de lecteurs d'écran n'est prévue (difficile à faire sérieusement en solo).

### Robustesse, configuration, tests, distribution
- **Autosave périodique + récupération après crash** : dès le palier 0
- **Réglages globaux** (thème, langue, profil de performance) : dossier de configuration standard du système (`%APPDATA%` sous Windows), pas dans le projet Reaper — préférences utilisateur, pas données de projet
- **Tests** : unitaires ciblés sur la logique non-visuelle dès le palier 0 (couche Paramètre, undo/redo, parsing MIDI/sysex, DSP testable isolément) ; pas de tests automatisés sur la mise en page UI, vérification visuelle à chaque palier ; **test de performance automatisé** mesurant le temps d'exécution du callback audio sur un cas de charge représentatif, à comparer avant/après chaque refactorisation importante — complète la relecture de code (détection fiable des anti-patterns temps réel : allocation mémoire dans le callback, mutex dans le chemin audio, copies de buffer inutiles, appels virtuels superflus, I/O bloquantes) par une mesure réelle plutôt qu'une simple impression de robustesse
- **Séparation stricte cœur moteur / adaptateur plateforme** : le cœur (lecture de sample, synthèse, DSP, modulation, traitement MIDI) écrit en C++ portable, sans dépendance directe aux modules JUCE liés à l'OS ; l'intégration CLAP/JUCE/Reaper reste une fine couche d'adaptation séparée, appelant le cœur via une interface simple (`process()`, `noteOn()`, `setParameter()`...). Discipline sans coût de performance si bien faite (passage de pointeurs, pas de copie à la frontière) qui facilite en plus les tests unitaires du cœur (exécutable sans driver audio ni fenêtre graphique) et préserve la possibilité d'un futur portage vers d'autres plateformes (ex. WebAssembly pour un projet séparé) sans toucher au cœur du moteur.
- **Distribution** : pas d'installeur pour l'instant, fichier `.clap` déposé dans le dossier plugins CLAP de l'hôte — à revoir si le projet grandit. Les binaires viennent des artefacts de la CI (`.github/workflows/build.yml`). Sous macOS, la signature/notarisation (compte développeur Apple) sera nécessaire pour une distribution sans avertissement système.
- **Chaîne de build** : CMake ; dépendances externes déclarées en un seul endroit (`cmake/Dependencies.cmake`, FetchContent, versions figées) ; le cœur (`core/`) et ses tests se compilent sans JUCE (`-DNEPTUNE_BUILD_PLUGIN=OFF`) ; l'adaptateur JUCE/CLAP est dans `adapter-juce/`.
- **Accessibilité** : hors périmètre V1 (voir la section dédiée) — choix conscient, pas un oubli
- **Contenu embarqué** : pas de bibliothèque de samples fournie (l'utilisateur apporte les siens), banque de formes d'onde du Synthé/FM incluse (fait partie du moteur)
- **Bus/Master** : le mode Normal/Slim s'applique aussi à eux, cohérent avec les autres types de piste

---

## Roadmap par paliers (résumé — voir fichier roadmap séparé pour le détail complet)

0. **Fondations d'architecture** : couche Paramètre générique, pool de slots, templates de sorties, undo/redo, squelette i18n/thèmes, format JSON versionné, profils de performance
1. **Noyau sampleur** : lecture sample, enveloppe AHDSHR généralisée, filtre par voie, mixeur basique, polyphonie, RAM/streaming
2. **Édition & conception modulaire** : éditeur de sample, mapping graphique, blocs modulaires, métrage, module FM, interface personnalisée par instrument
3. **MIDI & OSC transverses** : MIDI 1.0 → note-expression CLAP → MPE → MIDI 2.0, moniteur MIDI/OSC
4. **Effets stock & routage avancé** : les 8 plugins stock, bus internes, sends d'effets
5. **Modulation avancée** : matrice MIDI, modulation inter-pistes, logique booléenne, liaison Note On/Off
6. **Presets & organisation** : import/export, explorateur, format portable ZIP
7. **Mode orgue liturgique** : mutes/presets/routeur de notes par-dessus l'existant
8. **IR loader (convolution)** : plugin isolé, latence structurelle gérée par profil de performance
9. **Hébergement de plugins tiers** : dernier palier, si le projet est toujours actif, CLAP uniquement
10. **Moteur de règles scriptables** : interactions entre samples/voix, module Sampler uniquement, menus déroulants + mode Code Lua optionnel

---

## Point d'entrée suggéré pour la nouvelle conversation

Palier 0 : poser la couche Paramètre générique, le pool de slots fixe, le système undo/redo, et le format de sauvegarde JSON versionné — avant tout code "métier".
